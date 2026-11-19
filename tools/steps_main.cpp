// steps -- the runner of a step program (mcpp.tools.steps, 0.27.0).
//
//   steps <step file>
//
// Runs the operations the file holds (`mcpp::plugins::steps`, which
// `mcpp.tools.steps` writes while a build program plans) in order, and stops at
// the first that fails, naming it on standard error; the status is then 1.
//
// A PROGRAM OF THIS PACKAGE, NOT A VOCABULARY OF THE ENGINE. The feature
// `tools-steps` declares it as its tool, so the engine builds it once for the
// machine that builds and gives its path to every build program that links
// mcpp.plugins with that feature active (mcpp 2026.11.19.1). The operations
// change with this package and never wait for an engine release
// (mcpp-community/mcpp#955).
//
// NO SHELL, ON EVERY HOST. A program is started with its argument vector as it
// is (mcpplibs.os over openkal, which on Windows quotes each argument by the
// rule the started program splits its command line with), and every name is
// converted from UTF-8 by mcpplibs.os rather than through the process's code
// page. So a value keeps its spaces, semicolons, quotes, backslashes and
// non-ASCII text on Linux, macOS and Windows alike.
//
// THE SEMANTICS ARE THE ONES 0.25.0 STATED AND ITS BACKEND IMPLEMENTED, so that
// no member changes a step description. The backend was `cmake -P` of the
// `xim:cmake` it asked for, which the index offers only as CMake 4; where CMake
// decided a detail the statement left open, this program decides it the same
// way, as measured with CMake 4.5: a pattern's matches are sorted and spelled
// as the pattern's directory was written, then `/` and the rest; a pattern that
// names no absolute directory is the working directory's; a name is matched
// without case where the host compares names without it (Windows, macOS), and
// an exclusion is compared exactly; a recursive pattern enters no linked
// directory and matches the link by its name; a copied tree keeps a link as a
// link, a file's time and every permission, a directory's included; a
// read-only file or directory is removed and replaced as any other; a program
// named by a relative place is found from the directory it runs in. Three
// details differ, where CMake altered a value or read a byte as a character: a
// value that holds `;` stays one value of its list; a list is empty only when
// it holds no value; and `?` or a bracket expression stands for one character
// of UTF-8 text, compared without case as the rest of the name is.

import std;
import mcpp.plugins;
import mcpplibs.os;

namespace {

namespace os = mcpplibs::os;
namespace fs = std::filesystem;
namespace st = mcpp::plugins::steps;

using done  = std::expected<void, std::string>;
using lists = std::map<std::string, std::vector<std::string>>;

fs::path path_of(std::string_view utf8) { return os::fs::from_utf8(utf8); }

std::string shown(std::string_view value) { return std::format("'{}'", value); }

void say(std::string_view text) { (void)os::stdio::write_error(text); }

// ─── Names and patterns ────────────────────────────────────────────────────

// The characters of UTF-8 text, as code points. A byte that begins no valid
// sequence stands for itself above the code points, so a name that is not
// UTF-8 is still matched by its own bytes.
std::vector<char32_t> characters(std::string_view text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t n = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3
                            : (lead >> 3) == 0x1E ? 4 : 0;
        char32_t c = n == 1 ? lead : n == 2 ? (lead & 0x1Fu) : n == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
        bool whole = n != 0 && i + n <= text.size();
        for (std::size_t k = 1; whole && k < n; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            whole = (next >> 6) == 0x2;
            c = (c << 6) | (next & 0x3Fu);
        }
        if (!whole) {
            out.push_back(0x110000u + lead);
            ++i;
            continue;
        }
        out.push_back(c);
        i += n;
    }
    return out;
}

// One element of a pattern: a character, `?`, `*`, or a bracket expression.
struct token {
    enum class kind { character, any, star, set } what = kind::character;
    char32_t c = 0;
    bool     negated = false;
    std::vector<std::pair<char32_t, char32_t>> ranges;
};

// The `]` that closes the bracket expression opened at `open`, or `cs.size()`.
// A `]` first in the expression, after a `!` or `^` that negates it, stands
// for itself.
std::size_t closing_bracket(const std::vector<char32_t>& cs, std::size_t open) {
    std::size_t j = open + 1;
    if (j < cs.size() && (cs[j] == U'!' || cs[j] == U'^')) ++j;
    if (j < cs.size() && cs[j] == U']') ++j;
    while (j < cs.size() && cs[j] != U']') ++j;
    return j;
}

token bracket(const std::vector<char32_t>& cs, std::size_t open, std::size_t close) {
    token t{ token::kind::set };
    std::size_t j = open + 1;
    if (cs[j] == U'!' || cs[j] == U'^') {
        t.negated = true;
        ++j;
    }
    while (j < close) {
        if (j + 2 < close && cs[j + 1] == U'-') {
            t.ranges.emplace_back(cs[j], cs[j + 2]);
            j += 3;
        } else {
            t.ranges.emplace_back(cs[j], cs[j]);
            ++j;
        }
    }
    return t;
}

// A pattern's elements: `*`, `?`, `[...]` (negated by `!` or `^`, `a-z` a
// range), and every other character for itself -- a `[` no `]` closes, and a
// backslash, included. This is the grammar 0.25.0's backend read for a name
// and, with `*` crossing `/`, for an exclusion.
std::vector<token> compile(std::string_view pattern) {
    const auto cs = characters(pattern);
    std::vector<token> out;
    for (std::size_t i = 0; i < cs.size(); ++i) {
        if (cs[i] == U'*') {
            if (out.empty() || out.back().what != token::kind::star) out.push_back({ token::kind::star });
            continue;
        }
        if (cs[i] == U'?') {
            out.push_back({ token::kind::any });
            continue;
        }
        const std::size_t close = cs[i] == U'[' ? closing_bracket(cs, i) : cs.size();
        if (close == cs.size()) {
            out.push_back({ token::kind::character, cs[i] });
            continue;
        }
        out.push_back(bracket(cs, i, close));
        i = close;
    }
    return out;
}

// The other case of an ASCII letter: names are compared without case only as
// far as the hosts that do so compare them for every name.
char32_t other_case(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - (U'a' - U'A');
    if (c >= U'A' && c <= U'Z') return c + (U'a' - U'A');
    return c;
}

bool one(const token& t, char32_t c, bool fold) {
    if (t.what == token::kind::any) return true;
    if (t.what == token::kind::character) return t.c == c || (fold && t.c == other_case(c));
    const auto in = [&](char32_t x) {
        return std::ranges::any_of(t.ranges, [&](const auto& r) { return r.first <= x && x <= r.second; });
    };
    return (in(c) || (fold && in(other_case(c)))) != t.negated;
}

// Whether the pattern matches the whole text. A `*` matches any run of
// characters, and the match resumes after the last one when a later element
// fails.
bool matches(const std::vector<token>& pattern, const std::vector<char32_t>& text, bool fold) {
    std::size_t p = 0, t = 0, star = pattern.size(), resume = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p].what == token::kind::star) {
            star = p++;
            resume = t;
        } else if (p < pattern.size() && one(pattern[p], text[t], fold)) {
            ++p;
            ++t;
        } else if (star != pattern.size()) {
            p = star + 1;
            t = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p].what == token::kind::star) ++p;
    return p == pattern.size();
}

// A separator of the host's names: `/`, and on Windows `\` as well.
bool separator(char c) {
    if constexpr (os::is_windows) return c == '/' || c == '\\';
    else return c == '/';
}

// Whether a pattern's directory, as written before its last separator, is a
// root: nothing (`/x`), or on Windows a drive (`C:/x`).
bool names_root(std::string_view dir) {
    if constexpr (os::is_windows) return dir.empty() || (dir.size() == 2 && dir[1] == ':');
    else return dir.empty();
}

// The files a pattern matches, sorted. The wildcards of its last component are
// matched against each name of its directory, and with `recursive` against
// each name of every directory below it; a directory is never a match. A link
// to a directory is passed over by a flat pattern, and a recursive one does not
// enter it but matches it by its name, as CMake 4's recursive glob (the
// backend's `xim:cmake`) followed no link.
std::vector<std::string> glob(std::string_view pattern, bool recursive) {
    std::size_t cut = std::string_view::npos;
    for (std::size_t i = 0; i < pattern.size(); ++i)
        if (separator(pattern[i])) cut = i;
    const bool has_dir = cut != std::string_view::npos;
    const std::string_view name = has_dir ? pattern.substr(cut + 1) : pattern;
    std::string spelled = has_dir ? std::string(pattern.substr(0, cut)) : std::string();
    // A pattern with no directory, or a relative one, is the working
    // directory's, so a match is always absolute.
    const bool root = has_dir && names_root(spelled);
    if (!root && (!has_dir || !os::fs::is_absolute(path_of(spelled)))) {
        const std::string here = os::fs::generic_utf8(fs::current_path());
        spelled = has_dir ? here + "/" + spelled : here;
    }
    std::vector<std::string> found;
    if (name.empty()) return found;
    const auto tokens = compile(name);
    const bool fold = os::conventions().case_insensitive_names;
    // Each directory still to list, with the spelling of a match under it.
    std::vector<std::pair<fs::path, std::string>> pending{ { path_of(root ? spelled + "/" : spelled), spelled } };
    while (!pending.empty()) {
        const auto [dir, prefix] = std::move(pending.back());
        pending.pop_back();
        std::error_code ec;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string leaf = os::fs::utf8(it->path().filename());
            std::error_code kind;
            const bool linked = it->is_symlink(kind);
            if (it->is_directory(kind) && !(recursive && linked)) {
                if (recursive) pending.emplace_back(it->path(), prefix + "/" + leaf);
                continue;
            }
            if (matches(tokens, characters(leaf), fold)) found.push_back(prefix + "/" + leaf);
        }
    }
    std::ranges::sort(found);
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

// The names of a path made absolute, its root's included, with `.` and `..`
// resolved as text.
std::vector<std::string> names(const std::string& path) {
    fs::path whole = path_of(path);
    if (!os::fs::is_absolute(whole)) whole = fs::current_path() / whole;
    std::vector<std::string> out;
    for (auto const& part : whole.lexically_normal())
        if (std::string name = os::fs::generic_utf8(part); !name.empty()) out.push_back(std::move(name));
    return out;
}

bool same_name(std::string_view a, std::string_view b, bool fold) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char32_t x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x != y && !(fold && other_case(x) == y)) return false;
    }
    return true;
}

// `file` relative to `base` as the backend's `GLOB RELATIVE` gave it: the
// names both begin with are dropped, compared without case where the host
// compares names so; each other name of `base` becomes `..`; and a file that
// shares not even the root with `base` (another drive) keeps its own path.
std::string relative(const std::string& file, const std::string& base) {
    const auto f = names(file), b = names(base);
    const bool fold = os::conventions().case_insensitive_names;
    std::size_t same = 0;
    while (same < f.size() && same < b.size() && same_name(f[same], b[same], fold)) ++same;
    if (same == 0) return file;
    std::string out;
    for (std::size_t k = same; k < b.size(); ++k) out += out.empty() ? ".." : "/..";
    for (std::size_t k = same; k < f.size(); ++k) out += (out.empty() ? "" : "/") + f[k];
    return out;
}

// ─── The operations ────────────────────────────────────────────────────────

done run_program(const st::op& o, const lists& named) {
    std::vector<std::string> argv = o.values;
    if (const auto it = named.find(o.list); !o.list.empty() && it != named.end())
        argv.insert(argv.end(), it->second.begin(), it->second.end());
    if (argv.empty() || argv.front().empty()) return std::unexpected(std::string("there is no program to run"));
    os::process::spawn_options start;
    start.program = path_of(argv.front());
    // A program named by a relative place is found from the directory it runs
    // in, as `exec` after a change of directory finds it.
    if (!o.a.empty() && os::fs::names_a_place(argv.front()) && !os::fs::is_absolute(start.program))
        start.program = path_of(o.a) / start.program;
    start.argv0 = argv.front();
    start.args.assign(argv.begin() + 1, argv.end());
    if (!o.a.empty()) start.cwd = path_of(o.a);
    start.in = start.out = start.err = os::process::stdio::inherit;
    // The failure's description names the program, so these messages do not.
    auto child = os::process::process::spawn(start);
    if (!child) return std::unexpected(std::format("it cannot be started: {}", child.error().message()));
    const auto status = child->wait();
    if (!status) return std::unexpected(std::format("its end cannot be awaited: {}", status.error().message()));
    if (status->success()) return {};
    if (status->by_environment)
        return std::unexpected(std::format("it was ended by its environment (condition {})", status->code));
    return std::unexpected(std::format("it exited with status {}", status->code));
}

done touch_file(const st::op& o) {
    const fs::path file = path_of(o.a);
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    if (ec) return std::unexpected(std::format("cannot make the directory of {}: {}", shown(o.a), ec.message()));
    if (!fs::exists(file, ec)) {
        std::ofstream made(file, std::ios::binary | std::ios::app);
        if (!made) return std::unexpected(std::format("cannot create {}", shown(o.a)));
        return {};
    }
    fs::last_write_time(file, fs::file_time_type::clock::now(), ec);
    if (ec) return std::unexpected(std::format("cannot update the time of {}: {}", shown(o.a), ec.message()));
    return {};
}

done make_directory(const st::op& o) {
    const fs::path dir = path_of(o.a);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!ec && fs::is_directory(dir, ec)) return {};
    return std::unexpected(std::format("cannot make the directory {}: {}", shown(o.a),
                                       ec ? ec.message() : std::string("something else is there")));
}

// The entry, and every entry under it, made writable by its owner, as the
// backend made them before it removed them: a read-only file fails a removal on
// Windows, and a read-only directory one everywhere. A link is left as it is,
// since its permissions are its target's.
void make_writable(const fs::path& path) {
    std::error_code ignored, ec;
    const auto writable = [&](const fs::path& p) {
        if (!fs::is_symlink(fs::symlink_status(p, ignored)))
            fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ignored);
    };
    writable(path);
    if (!fs::is_directory(fs::symlink_status(path, ignored))) return;
    for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec))
        writable(it->path());
}

done remove_entry(std::string_view name, const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (!ec) return {};
    make_writable(path);
    ec.clear();
    fs::remove_all(path, ec);
    if (ec) return std::unexpected(std::format("cannot remove {}: {}", shown(name), ec.message()));
    return {};
}

done remove_matches(const st::op& o) {
    for (auto const& f : glob(o.a, false))
        if (auto removed = remove_entry(f, path_of(f)); !removed) return removed;
    return {};
}

// Whatever is at `to`, unless it is a directory, removed before a copy or a
// link takes its place: the backend replaced a read-only file as any other.
std::error_code clear_way(const fs::path& to) {
    std::error_code absent, ec;
    // A missing `to` is an error of `symlink_status` under libstdc++, and not
    // one of this copy.
    if (const auto there = fs::symlink_status(to, absent); !fs::exists(there) || fs::is_directory(there)) return {};
    if (fs::remove(to, ec); !ec) return {};
    make_writable(to);
    ec.clear();
    fs::remove(to, ec);
    return ec;
}

// A file or a link copied as the backend's `file(COPY)` copied it: a link as a
// link, a file with its permissions and its time.
done copy_entry(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    const bool link = fs::is_symlink(fs::symlink_status(from, ec));
    if (!ec) ec = clear_way(to);
    if (!ec && link) {
        fs::copy_symlink(from, to, ec);
    } else if (!ec) {
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        const auto when = ec ? fs::file_time_type{} : fs::last_write_time(from, ec);
        if (!ec) fs::last_write_time(to, when, ec);
    }
    if (!ec) return {};
    return std::unexpected(std::format("cannot copy {} to {}: {}", shown(os::fs::generic_utf8(from)),
                                       shown(os::fs::generic_utf8(to)), ec.message()));
}

done copy_one_file(const st::op& o) {
    const fs::path from = path_of(o.a), to = path_of(o.b);
    std::error_code ec;
    // What is at `to` stays when there is nothing to copy over it.
    if (!fs::is_regular_file(from, ec)) return std::unexpected(std::format("{} is not a file", shown(o.a)));
    if (to.has_parent_path()) fs::create_directories(to.parent_path(), ec);
    if (ec) return std::unexpected(std::format("cannot make the directory of {}: {}", shown(o.b), ec.message()));
    if (fs::exists(to, ec) && fs::equivalent(from, to, ec)) return {};
    ec = clear_way(to);
    if (!ec) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return std::unexpected(std::format("cannot copy {} to {}: {}", shown(o.a), shown(o.b), ec.message()));
    return {};
}

// A directory's contents copied into `o.b` as the backend's `file(COPY <dir>/)`
// copied them. Each directory is open to its owner while the copy fills it and
// takes its source's permissions after, `o.b` the source's own, so a read-only
// tree copies, and copies again over its copy.
done copy_tree_contents(const st::op& o) {
    const fs::path from = path_of(o.a), to = path_of(o.b);
    std::error_code ec;
    if (!fs::is_directory(from, ec)) {
        if (o.if_exists) return {};
        return std::unexpected(std::format("{} is not a directory", shown(o.a)));
    }
    std::vector<std::pair<fs::path, fs::perms>> modes;
    const auto open_directory = [&](const fs::path& source, const fs::path& dir) {
        std::error_code e;
        fs::create_directories(dir, e);
        if (!e) modes.emplace_back(dir, fs::status(source, e).permissions());
        if (!e) fs::permissions(dir, fs::perms::owner_all, fs::perm_options::add, e);
        if (e) return done(std::unexpected(std::format("cannot make the directory {}: {}",
                                                       shown(os::fs::generic_utf8(dir)), e.message())));
        return done{};
    };
    if (auto opened = open_directory(from, to); !opened) return opened;
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path dest = to / it->path().lexically_relative(from);
        std::error_code kind;
        auto copied = fs::is_directory(it->symlink_status(kind)) ? open_directory(it->path(), dest)
                                                                 : copy_entry(it->path(), dest);
        if (!copied) return copied;
    }
    if (ec) return std::unexpected(std::format("cannot list {}: {}", shown(o.a), ec.message()));
    for (auto it = modes.rbegin(); it != modes.rend(); ++it) {
        fs::permissions(it->first, it->second, ec);
        if (ec) return std::unexpected(std::format("cannot set the permissions of {}: {}",
                                                   shown(os::fs::generic_utf8(it->first)), ec.message()));
    }
    return {};
}

done copy_matches(const st::op& o) {
    const fs::path to = path_of(o.b);
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) return std::unexpected(std::format("cannot make the directory {}: {}", shown(o.b), ec.message()));
    for (auto const& f : glob(o.a, false)) {
        const fs::path from = path_of(f);
        if (auto copied = copy_entry(from, to / from.filename()); !copied) return copied;
    }
    return {};
}

done rename_entry(const st::op& o) {
    const fs::path from = path_of(o.a), to = path_of(o.b);
    auto moved = os::fs::rename(from, to, true);
    // Windows does not replace a read-only file; the backend made it writable
    // and tried again.
    if (std::error_code ignored; !moved && fs::is_regular_file(fs::symlink_status(to, ignored))) {
        fs::permissions(to, fs::perms::owner_write, fs::perm_options::add, ignored);
        moved = os::fs::rename(from, to, true);
    }
    if (!moved)
        return std::unexpected(std::format("cannot rename {} to {}: {}", shown(o.a), shown(o.b), moved.error().message()));
    return {};
}

// A symbolic link at `b` naming `a`, as written. An entry already at `b` that
// is not a directory is replaced, as the backend replaced it.
done make_link(const st::op& o) {
    const fs::path target = path_of(o.a), link = path_of(o.b);
    std::error_code absent, ec;
    if (fs::is_directory(fs::symlink_status(link, absent)))
        return std::unexpected(std::format("cannot link {}: a directory is there", shown(o.b)));
    if (const auto in_the_way = clear_way(link))
        return std::unexpected(std::format("cannot replace {}: {}", shown(o.b), in_the_way.message()));
    // Whether the target is a directory decides the kind of link on Windows; a
    // relative target is read from the link's directory, as the system reads it.
    const fs::path resolved = os::fs::is_absolute(target) ? target : link.parent_path() / target;
    const bool directory = fs::is_directory(resolved, ec);
    if (auto made = os::fs::create_link(target, link, directory); !made)
        return std::unexpected(std::format("cannot link {} to {}: {}", shown(o.b), shown(o.a), made.error().message()));
    return {};
}

done glob_into(const st::op& o, lists& named) {
    std::vector<std::string> found = glob(o.a, o.recursive);
    if (!o.b.empty()) {
        for (auto& f : found) f = relative(f, o.b);
        std::ranges::sort(found);
        found.erase(std::unique(found.begin(), found.end()), found.end());
    }
    std::vector<std::vector<token>> excluded;
    for (auto const& e : o.values) excluded.push_back(compile(e));
    auto& list = named[o.list];
    for (auto const& f : found) {
        const auto text = characters(f);
        if (std::ranges::none_of(excluded, [&](const auto& e) { return matches(e, text, false); }))
            list.push_back(f);
    }
    return {};
}

done append_values(const st::op& o, lists& named) {
    auto& list = named[o.list];
    list.insert(list.end(), o.values.begin(), o.values.end());
    return {};
}

done require_values(const st::op& o, const lists& named) {
    if (const auto it = named.find(o.list); it != named.end() && !it->second.empty()) return {};
    return std::unexpected(o.a);
}

done perform(const st::op& o, lists& named) {
    switch (o.kind) {
    case st::op_kind::run:         return run_program(o, named);
    case st::op_kind::touch:       return touch_file(o);
    case st::op_kind::mkdir:       return make_directory(o);
    case st::op_kind::remove:      return remove_entry(o.a, path_of(o.a));
    case st::op_kind::remove_glob: return remove_matches(o);
    case st::op_kind::copy_file:   return copy_one_file(o);
    case st::op_kind::copy_tree:   return copy_tree_contents(o);
    case st::op_kind::copy_glob:   return copy_matches(o);
    case st::op_kind::rename:      return rename_entry(o);
    case st::op_kind::symlink:     return make_link(o);
    case st::op_kind::glob:        return glob_into(o, named);
    case st::op_kind::append:      return append_values(o, named);
    case st::op_kind::require:     return require_values(o, named);
    }
    return std::unexpected(std::string("an operation this runner does not know"));
}

// The operation as a failure names it.
std::string describe(const st::op& o) {
    switch (o.kind) {
    case st::op_kind::run:
        return std::format("run {}", o.values.empty() ? std::string("''") : shown(o.values.front()));
    case st::op_kind::copy_file: case st::op_kind::copy_tree: case st::op_kind::copy_glob:
    case st::op_kind::rename: case st::op_kind::symlink:
        return std::format("{} {} {}", st::name_of(o.kind), shown(o.a), shown(o.b));
    case st::op_kind::glob:
        return std::format("glob {} into '{}'", shown(o.a), o.list);
    case st::op_kind::append: case st::op_kind::require:
        return std::format("{} '{}'", st::name_of(o.kind), o.list);
    default:
        return std::format("{} {}", st::name_of(o.kind), shown(o.a));
    }
}

int run_file(const std::string& file) {
    const auto text = os::fs::read(path_of(file));
    if (!text) {
        say(std::format("steps: {} cannot be read: {}\n", file, text.error().message()));
        return 1;
    }
    const auto ops = st::read(*text);
    if (!ops) {
        say(std::format("steps: {}: {}\n", file, ops.error()));
        return 1;
    }
    lists named;
    for (std::size_t i = 0; i < ops->size(); ++i) {
        const st::op& o = (*ops)[i];
        if (const auto outcome = perform(o, named); !outcome) {
            say(std::format("steps: {}: operation {} of {}, {}: {}\n", file, i + 1, ops->size(),
                            describe(o), outcome.error()));
            return 1;
        }
    }
    return 0;
}

} // namespace

int main() {
    const auto args = os::env::arguments();
    if (args.size() != 2) {
        say("usage: steps <step file>\n"
            "  runs, in order, the operations of a step program that mcpp.tools.steps wrote\n");
        return 2;
    }
    try {
        return run_file(args[1]);
    } catch (const std::exception& e) {
        say(std::format("steps: {}: {}\n", args[1], e.what()));
        return 1;
    }
}
