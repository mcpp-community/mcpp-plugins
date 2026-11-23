// mcpp.rules.qml -- how a QML application becomes part of a program.
//
// A Qt Quick program has inputs no C++ compiler reads, and a runtime that
// opens more than the program's import table names. `qml_module` is the unit a
// project states -- a URI, a version, the `.qml`/`.js` files, the headers of the
// C++ types it exposes -- and for each one the rule declares the chain Qt's
// `qt_add_qml_module` runs, as actions with declared inputs and outputs:
//
//   header           -> moc --output-json  -> qmltyperegistrar -> registrations.cpp
//                                                              -> <module>.qmltypes
//   .qml .js .mjs    -> qmlcachegen        -> one .cpp each (the compiled form)
//   qmldir, files    -> rcc                -> qrc_<module>.cpp
//   the files        -> qmlcachegen loader -> <module>_qmlcache_loader.cpp
//   .vert .frag      -> qsb                -> embedded in the module's resources
//
// Every generated `.cpp` is a `role = "source"` action output, compiled and
// linked into the program, so a module of the application is static: it is in
// the executable and `QQmlEngine::loadFromModule("uri", "Type")` finds it.
//
// WHAT THE PROGRAM LOADS, THE RULE PLACES. A QML module of Qt itself
// (`QtQuick.Controls`, `QtQuick.Layouts`) is a directory of files and, usually,
// a plugin library, opened when a `.qml` file imports it. No import table names
// it, which is the job `windeployqt --qmldir` exists for. The rule reads the
// `import` statements of the project's `.qml` files and follows the SDK's
// `qmldir` files and the `.qml` files of the modules they name (the closure
// `qmlimportscanner` computes, by text, at plan time), then deploys each module
// under `qml/`, and a `qt.conf` that points Qt at them. The libraries a plugin
// loads are the engine's: `mcpp pack` takes what the build deploys beside the
// program as a root of its runtime closure.
//
// A MISSING SDK OR TOOL IS A WARNING, as in `mcpp.rules.qt`: the plan says what
// it could not find and declares what it can, so `mcpp emit build-database`
// plans a project on a machine that never built it.

export module mcpp.rules.qml;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.plugins.fs;
import mcpp.rules.qt;

export namespace mcpp::rules::qml {

// One QML module of the application.
struct qml_module {
    // `Probe`, `com.acme.ui`: required.
    std::string uri;
    // `major.minor`.
    std::string version = "1.0";
    // The `.qml`, `.js` and `.mjs` files, as paths or globs relative to the
    // package root. Any other file is a resource.
    std::vector<std::string> sources;
    // The directory the module's files are relative to. Empty is the directory
    // the sources have in common.
    std::string root;
    // Other files of the module (images, data), as paths or globs.
    std::vector<std::string> resources;
    // The headers of the C++ types the module registers. Empty takes every
    // header `moc` read that uses a `QML_*` macro, when the project has one
    // module; with several, each header is assigned here.
    std::vector<std::string> headers;
    // `depends` lines of the module's `qmldir`: modules it needs loaded. Stated,
    // not derived. Empty by default.
    std::vector<std::string> depends;
    // `import` lines of the module's `qmldir`: modules a user of this one gets
    // imported with it. Stated, not derived.
    std::vector<std::string> imports;
    // `.vert`, `.frag`, `.comp` files compiled by `qsb` and embedded as
    // `shaders/<file>.qsb`.
    std::vector<std::string> shaders;
    std::vector<std::string> qsb_args = { "--qt6" };
    // Compile the files ahead of time (`qmlcachegen`). `false` embeds the
    // sources only.
    bool aot = true;
    // The resource prefix; the module's files are under `<prefix>/<uri as a path>`.
    std::string prefix = "/qt/qml";
};

struct options {
    std::vector<qml_module> modules;
    // Everything `mcpp.rules.qt` takes: the modules, the plugins, the SDK, the
    // translations. `Qml` is always linked.
    qt::options qt;
    // Plugin directories a Qt Quick program needs, placed beside the program
    // with `qt.deploy_plugins`. Only those the SDK has are placed. Set it empty
    // to place none.
    std::vector<std::string> quick_plugins = { "xcbglintegrations", "platforminputcontexts", "imageformats" };
    // Qt modules the program loads that no `.qml` file imports (`Qt.createComponent`
    // with a computed name): `"QtQuick.Controls.Material"`.
    std::vector<std::string> imports;
    // When not empty, an `optional import` of a style is followed only for
    // these (`"Material"`, `"Basic"`); empty follows every style, so any can be chosen.
    std::vector<std::string> styles;
    // Directories of QML modules beyond the SDK's (each holds `<uri as a path>/qmldir`).
    std::vector<std::string> import_paths;
    // Place the Qt modules the program imports beside it (`qml/`).
    bool deploy_imports = true;
    // Write `qt.conf` and place it beside the program: `Prefix = .`,
    // `Qml2Imports = qml`. Qt then takes its data, translations and QML modules
    // from the program's directory. A project that has its own `qt.conf` keeps it.
    bool write_qt_conf = true;
    // Run `qmllint` on every `.qml` file as a check.
    bool lint = false;
    bool lint_warnings_are_errors = false;
};

// The directory holding every module's `qmldir`, the import root of the tools.
inline std::string import_root(const options& opt) {
    return qt::detail::generic(std::filesystem::path(opt.qt.out_dir) / "qml");
}

namespace detail {

namespace fs = std::filesystem;
using qt::detail::generic;
using qt::detail::warn;

// ─── Text ──────────────────────────────────────────────────────────────────

inline bool word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// `com.acme.ui`: identifiers joined by dots. A uri names a directory, so the
// grammar is also what keeps a path out of one.
inline bool valid_uri(const std::string& u) {
    if (u.empty()) return false;
    bool start = true;
    for (std::size_t i = 0; i < u.size(); ++i) {
        const char c = u[i];
        if (start) {
            if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) return false;
            start = false;
        } else if (c == '.') {
            start = true;
        } else if (!word_char(c)) {
            return false;
        }
    }
    return !start;
}

inline std::string replaced(std::string s, char from, char to) {
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == from) s[i] = to;
    return s;
}
inline std::string uri_path(const std::string& uri) { return replaced(uri, '.', '/'); }
inline std::string sanitize(const std::string& uri) { return replaced(uri, '.', '_'); }

inline bool parse_version(const std::string& v, int& major, int& minor) {
    const auto dot = v.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= v.size()) return false;
    int a = 0, b = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i == dot) continue;
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
        int& n = i < dot ? a : b;
        n = n * 10 + (v[i] - '0');
        if (n > 65535) return false;
    }
    major = a;
    minor = b;
    return true;
}

inline std::string xml_escape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += s[i];
        }
    }
    return out;
}

inline std::string trimmed(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(s[i]))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += s[i];
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// A QML or JavaScript text without its comments and the contents of its
// strings, so an `import` inside either is not read as one.
inline std::string without_comments_and_strings(const std::string& t) {
    std::string out;
    std::size_t i = 0;
    while (i < t.size()) {
        const char c = t[i];
        if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') {
            while (i < t.size() && t[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
            const auto end = t.find("*/", i + 2);
            const std::size_t stop = end == std::string::npos ? t.size() : end + 2;
            // A line break inside the comment stays one: `import` is read a line at a time.
            out += ' ';
            for (; i < stop; ++i)
                if (t[i] == '\n') out += '\n';
        } else if (c == '"' || c == '\'' || c == '`') {
            std::size_t j = i + 1;
            // Only a template literal spans lines; a quote left open ends at the line.
            while (j < t.size() && t[j] != c && (c == '`' || t[j] != '\n')) j += t[j] == '\\' ? 2 : 1;
            out += c;
            out += c;
            i = j < t.size() && t[j] == c ? j + 1 : j;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

inline bool version_token(const std::string& s) {
    if (s.empty() || !std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    for (std::size_t i = 0; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i])) && s[i] != '.') return false;
    return true;
}

// The modules a `.qml` file imports: `import <uri> [version] [as Name]`, one
// per line, with an optional `;`. `import "dir"` and `import "File.js" as U`
// name files and are not modules.
inline std::vector<std::string> imports_of(const std::string& text) {
    std::vector<std::string> out;
    const std::string clean = without_comments_and_strings(text);
    std::size_t at = 0;
    while (at <= clean.size()) {
        auto nl = clean.find('\n', at);
        std::string line = clean.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? clean.size() + 1 : nl + 1;
        line = trimmed(line);
        if (!line.empty() && line.back() == ';') line.pop_back();
        const auto w = words(line);
        if (w.size() < 2 || w[0] != "import" || !valid_uri(w[1])) continue;
        bool shape = w.size() == 2
                  || (w.size() == 3 && version_token(w[2]))
                  || (w.size() == 4 && w[2] == "as")
                  || (w.size() == 5 && version_token(w[2]) && w[3] == "as");
        if (shape) out.push_back(w[1]);
    }
    return out;
}

inline bool declares_singleton(const std::string& text) {
    const std::string clean = without_comments_and_strings(text);
    for (std::size_t at = clean.find("pragma"); at != std::string::npos; at = clean.find("pragma", at + 1)) {
        std::size_t i = at + 6;
        while (i < clean.size() && (clean[i] == ' ' || clean[i] == '\t')) ++i;
        if (clean.compare(i, 9, "Singleton") == 0 && (i + 9 >= clean.size() || !word_char(clean[i + 9]))) return true;
    }
    return false;
}

// A header that uses one of the macros `qmltyperegistrar` reads.
inline bool declares_qml_types(const std::string& text) {
    for (auto m : { "QML_ELEMENT", "QML_NAMED_ELEMENT", "QML_SINGLETON", "QML_ANONYMOUS", "QML_FOREIGN",
                    "QML_UNCREATABLE", "QML_EXTENDED", "QML_ATTACHED", "QML_VALUE_TYPE", "QML_INTERFACE" }) {
        const std::size_t n = std::strlen(m);
        for (std::size_t at = text.find(m); at != std::string::npos; at = text.find(m, at + 1)) {
            const bool left = at == 0 || !word_char(text[at - 1]);
            const bool right = at + n >= text.size() || !word_char(text[at + n]);
            if (left && right) return true;
        }
    }
    return false;
}

// ─── Files ─────────────────────────────────────────────────────────────────

inline bool has_wildcard(const std::string& s) { return s.find_first_of("*?") != std::string::npos; }

// `*` and `?` do not cross a `/`; `**/` is any number of directories.
inline bool glob_match(std::string_view p, std::string_view t) {
    if (p.empty()) return t.empty();
    if (p.starts_with("**")) {
        std::string_view rest = p.substr(2);
        const bool dirs = rest.starts_with("/");
        if (dirs) rest.remove_prefix(1);
        for (std::size_t i = 0; i <= t.size(); ++i) {
            if (dirs && !(i == 0 || t[i - 1] == '/')) continue;
            if (glob_match(rest, t.substr(i))) return true;
        }
        return false;
    }
    if (p[0] == '*') {
        for (std::size_t i = 0; i <= t.size(); ++i) {
            if (glob_match(p.substr(1), t.substr(i))) return true;
            if (i < t.size() && t[i] == '/') break;
        }
        return false;
    }
    if (t.empty()) return false;
    if (p[0] == '?') return t[0] != '/' && glob_match(p.substr(1), t.substr(1));
    return p[0] == t[0] && glob_match(p.substr(1), t.substr(1));
}

inline bool skipped_directory(const fs::path& dir) {
    const auto name = dir.filename().string();
    return name == "target" || name == ".git" || name == "mcpp-generated" || name == "vcpkg_installed" ||
           name == "node_modules" || mcpp::plugins::tree::build_output(dir);
}

// The files a path or glob (relative to the package root) names, sorted. A file
// the plan reads is declared with `rerun_if_changed`; a glob, with the glob.
inline std::vector<fs::path> expand(const std::string& given) {
    std::string pattern = given;
    if (pattern.starts_with("./")) pattern.erase(0, 2);
    std::vector<fs::path> out;
    std::error_code ec;
    if (!has_wildcard(pattern)) {
        const auto p = qt::detail::absolute_from_root(pattern);
        if (fs::is_regular_file(p, ec)) out.push_back(p);
        return out;
    }
    mcpp::rerun_if_changed_glob(pattern.c_str());
    const auto first = pattern.find_first_of("*?");
    const auto slash = pattern.rfind('/', first);
    const fs::path root = mcpp::manifest_dir();
    const fs::path base = slash == std::string::npos ? root : root / pattern.substr(0, slash);
    for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec)) {
            if (skipped_directory(it->path())) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (glob_match(pattern, generic(it->path().lexically_relative(root)))) out.push_back(it->path());
    }
    std::ranges::sort(out);
    return out;
}

inline std::vector<fs::path> expand_all(const std::vector<std::string>& patterns, const std::string& who,
                                        const std::string& what) {
    std::vector<fs::path> out;
    for (auto const& p : patterns) {
        auto found = expand(p);
        if (found.empty())
            warn(std::format("{}: `{}` of {} names no file.", who, p, what));
        for (auto const& f : found)
            if (std::ranges::find(out, f) == out.end()) out.push_back(f);
    }
    std::ranges::sort(out);
    return out;
}

inline bool is_script(const fs::path& p) {
    const auto e = p.extension().string();
    return e == ".qml" || e == ".js" || e == ".mjs";
}

// The directory the files have in common, by whole directory names.
inline fs::path common_directory(const std::vector<fs::path>& files) {
    if (files.empty()) return {};
    auto parts = [](const fs::path& f) {
        std::vector<std::string> out;
        const std::string g = generic(f.parent_path());
        std::size_t at = 0;
        while (at <= g.size()) {
            const auto slash = g.find('/', at);
            out.push_back(g.substr(at, slash == std::string::npos ? std::string::npos : slash - at));
            at = slash == std::string::npos ? g.size() + 1 : slash + 1;
        }
        return out;
    };
    std::vector<std::string> common = parts(files.front());
    for (auto const& f : files) {
        const auto d = parts(f);
        std::size_t n = 0;
        while (n < common.size() && n < d.size() && common[n] == d[n]) ++n;
        common.resize(n);
    }
    std::string out;
    for (std::size_t i = 0; i < common.size(); ++i) out += (i ? "/" : "") + common[i];
    return fs::path(out.empty() ? "/" : out);
}

// ─── Qt's own modules: the import closure ──────────────────────────────────

struct module_dir {
    std::string uri;
    fs::path dir;    // holds the module's `qmldir`
};

struct imports_found {
    std::vector<module_dir> modules;       // in uri order
    std::vector<std::string> unresolved;   // imported, and in no SDK or import path
};

inline bool is_style_import(const std::string& uri, const std::vector<std::string>& styles) {
    if (styles.empty()) return true;
    const auto dot = uri.rfind('.');
    const std::string last = dot == std::string::npos ? uri : uri.substr(dot + 1);
    return std::ranges::find(styles, last) != styles.end();
}

// The modules an import of `uri` brings: the `import`, `default import`,
// `optional import` and `depends` lines of its `qmldir`, and the `import`
// statements of the `.qml` files in its directory (not in a directory that is a
// module of its own). Measured equal to `qmlimportscanner`'s answer on Qt 6.11.1.
inline std::vector<std::string> module_dependencies(const fs::path& dir, const std::vector<std::string>& styles) {
    std::vector<std::string> deps;
    std::istringstream in(qt::detail::read_file(dir / "qmldir"));
    for (std::string line; std::getline(in, line); ) {
        const auto w = words(line);
        if (w.empty()) continue;
        if ((w[0] == "optional" || w[0] == "default") && w.size() >= 3 && w[1] == "import") {
            if (w[0] == "default" || is_style_import(w[2], styles)) deps.push_back(w[2]);
        } else if ((w[0] == "import" || w[0] == "depends") && w.size() >= 2) {
            deps.push_back(w[1]);
        }
    }
    std::error_code ec;
    std::vector<fs::path> pending = { dir };
    while (!pending.empty()) {
        const fs::path d = pending.back();
        pending.pop_back();
        std::vector<fs::path> entries;
        for (auto const& e : fs::directory_iterator(d, ec)) entries.push_back(e.path());
        std::ranges::sort(entries);
        for (auto const& p : entries) {
            if (fs::is_directory(p, ec)) {
                if (!fs::exists(p / "qmldir", ec)) pending.push_back(p);
            } else if (p.extension() == ".qml") {
                for (auto const& i : imports_of(qt::detail::read_file(p))) deps.push_back(i);
            }
        }
    }
    return deps;
}

inline imports_found import_closure(std::vector<std::string> roots, const std::vector<std::string>& own,
                                    const std::vector<fs::path>& import_dirs, const std::vector<std::string>& styles) {
    imports_found out;
    std::set<std::string> seen;
    std::error_code ec;
    while (!roots.empty()) {
        const std::string uri = roots.back();
        roots.pop_back();
        if (!seen.insert(uri).second || std::ranges::find(own, uri) != own.end()) continue;
        fs::path dir;
        for (auto const& d : import_dirs)
            if (fs::is_regular_file(d / uri_path(uri) / "qmldir", ec)) { dir = d / uri_path(uri); break; }
        if (dir.empty()) { out.unresolved.push_back(uri); continue; }
        out.modules.push_back({ uri, dir });
        for (auto const& d : module_dependencies(dir, styles)) roots.push_back(d);
    }
    std::ranges::sort(out.modules, {}, &module_dir::uri);
    std::ranges::sort(out.unresolved);
    return out;
}

// The files of a Qt module that go beside the program: everything in its
// directory except what is not run time, and except a directory that is a
// module of its own (it is deployed when it is imported).
inline std::vector<fs::path> module_files(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    std::vector<fs::path> pending = { dir };
    while (!pending.empty()) {
        const fs::path d = pending.back();
        pending.pop_back();
        std::vector<fs::path> entries;
        for (auto const& e : fs::directory_iterator(d, ec)) entries.push_back(e.path());
        std::ranges::sort(entries);
        for (auto const& p : entries) {
            if (fs::is_directory(p, ec)) {
                if (p.filename() == "designer" || fs::exists(p / "qmldir", ec)) continue;
                pending.push_back(p);
                continue;
            }
            const auto ext = p.extension().string();
            if (ext == ".qmltypes" || ext == ".prl" || ext == ".debug" || ext == ".pdb") continue;
            // The Windows SDK ships each plugin twice, `x.dll` and the debug `xd.dll`.
            if (qt::detail::is_windows() && ext == ".dll") {
                const auto stem = p.stem().string();
                if (stem.ends_with("d") && fs::exists(p.parent_path() / (stem.substr(0, stem.size() - 1) + ".dll"), ec))
                    continue;
            }
            out.push_back(p);
        }
    }
    std::ranges::sort(out);
    return out;
}

// ─── Generated text ────────────────────────────────────────────────────────

inline std::string qmldir_text(const qml_module& m, bool types, const std::string& resource_dir,
                               const std::vector<std::pair<fs::path, std::string>>& files) {
    std::string t = "module " + m.uri + "\n";
    if (types) t += "typeinfo " + sanitize(m.uri) + ".qmltypes\n";
    t += "prefer :" + resource_dir + "/\n";
    for (auto const& d : m.depends) t += "depends " + d + "\n";
    for (auto const& i : m.imports) t += "import " + i + "\n";
    for (auto const& [file, rel] : files) {
        if (file.extension() != ".qml") continue;
        const std::string name = file.stem().string();
        if (name.empty() || !std::isupper(static_cast<unsigned char>(name[0]))) continue;
        t += (declares_singleton(qt::detail::read_file(file)) ? "singleton " : "") + name + " " + m.version + " " + rel + "\n";
    }
    return t;
}

inline std::string qrc_text(const std::string& resource_prefix, const std::string& qmldir,
                            const std::vector<std::pair<std::string, std::string>>& files /* alias, path */) {
    std::string t = "<RCC>\n  <qresource prefix=\"" + xml_escape(resource_prefix) + "\">\n";
    t += "    <file alias=\"qmldir\">" + xml_escape(qmldir) + "</file>\n";
    for (auto const& [alias, path] : files)
        t += "    <file alias=\"" + xml_escape(alias) + "\">" + xml_escape(path) + "</file>\n";
    t += "  </qresource>\n</RCC>\n";
    return t;
}

// ─── Actions ───────────────────────────────────────────────────────────────

struct step {
    std::string id;
    std::string description;
    std::vector<std::string> argv;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::pair<std::string, std::string>> env;
    std::string depfile;        // a Make-style file the command writes, when it can
    const char* role = mcpp::roles::source;
};

inline void submit(const step& s) {
    mcpp::action a;
    a.id = s.id.c_str();
    a.role = s.role;
    a.description = s.description.c_str();
    if (!s.depfile.empty()) a.depfile = s.depfile.c_str();
    for (auto const& x : s.argv) a.arg(x.c_str());
    for (auto const& x : s.inputs) a.input(x.c_str());
    for (auto const& x : s.outputs) a.output(x.c_str());
    for (auto const& [k, v] : s.env) a.env(k.c_str(), v.c_str());
    a.submit();
}

inline std::string flattened(const std::string& rel) {
    return replaced(replaced(rel, '/', '_'), '.', '_');
}

// What a module is once its files and headers are known.
struct planned {
    const qml_module* module = nullptr;
    std::string san, upath, resource_dir;
    int major = 1, minor = 0;
    fs::path root;
    std::vector<std::pair<fs::path, std::string>> files;       // scripts, then resources: file, relative path
    std::vector<fs::path> shaders;
    std::vector<qt::moc_unit> units;                           // its C++ types
    fs::path dir, work, qmldir, qmltypes, qrc;
};

inline bool unsupported_target() {
    const std::string_view os = mcpp::target_os();
    return os != "linux" && os != "windows" && os != "macos";
}

inline std::string joined(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
    return out;
}

// A `qt.conf` the project carries itself: beside its sources, in `res/`, or as
// `/qt/etc/qt.conf` in a resource file (Qt reads that one before any file).
inline bool project_has_qt_conf(const qt::options& q) {
    std::error_code ec;
    const fs::path root = mcpp::manifest_dir();
    mcpp::rerun_if_changed_glob("**/qt.conf");
    for (auto const* p : { "qt.conf", "res/qt.conf", "resources/qt.conf" })
        if (fs::is_regular_file(root / p, ec)) return true;
    std::vector<std::string> qrcs = qt::detail::device(".qrc");
    for (auto const& r : q.resources) qrcs.push_back(r);
    for (auto const& r : qrcs)
        if (qt::detail::read_file(qt::detail::absolute_from_root(r)).find("prefix=\"/qt/etc\"") != std::string::npos) return true;
    return false;
}

} // namespace detail

// ─── The rule ──────────────────────────────────────────────────────────────

inline bool compile(options opt) {
    namespace fs = std::filesystem;
    using detail::generic;
    constexpr std::string_view who = "mcpp.rules.qml";

    if (detail::unsupported_target()) {
        detail::warn(std::format("{}: QML is built for linux, windows and macos; {} is not planned.", who,
                                 std::string(mcpp::target_os())));
        return true;
    }

    // ── what the project states, checked before anything is declared ──
    std::set<std::string> uris, names;
    for (auto const& m : opt.modules) {
        int a, b;
        if (!detail::valid_uri(m.uri)) {
            std::cerr << std::format("{}: `{}` is not a module uri: identifiers joined by dots (`com.acme.ui`).\n", who, m.uri);
            return false;
        }
        if (!detail::parse_version(m.version, a, b)) {
            std::cerr << std::format("{}: the version `{}` of {} is not `major.minor`.\n", who, m.version, m.uri);
            return false;
        }
        if (!uris.insert(m.uri).second || !names.insert(detail::sanitize(m.uri)).second) {
            std::cerr << std::format("{}: the module {} is stated twice (or `a.b` and `a_b`, which name one generated file).\n", who, m.uri);
            return false;
        }
        if (m.prefix.empty() || m.prefix.front() != '/' || m.prefix.back() == '/') {
            std::cerr << std::format("{}: the prefix `{}` of {} is a resource path such as `/qt/qml`.\n", who, m.prefix, m.uri);
            return false;
        }
    }

    // ── Qt: the generators, with the type description the registrar reads ──
    if (std::ranges::find(opt.qt.modules, "Qml") == opt.qt.modules.end() &&
        std::ranges::find(opt.qt.modules, "QtQml") == opt.qt.modules.end() &&
        std::ranges::find(opt.qt.modules, "Qt6Qml") == opt.qt.modules.end())
        opt.qt.modules.push_back("Qml");
    opt.qt.moc_json = true;
    opt.qt.moc_includes = true;
    if (opt.qt.i18n.update_sources)
        for (auto const* e : { "qml", "js" })
            if (std::ranges::find(opt.qt.i18n.extensions, std::string(e)) == opt.qt.i18n.extensions.end())
                opt.qt.i18n.extensions.push_back(e);
    // The plugin directories a Qt Quick program needs, the ones the SDK has.
    {
        std::error_code ec;
        const auto roots = qt::roots(opt.qt);
        for (auto const& dir : opt.quick_plugins) {
            if (std::ranges::find(opt.qt.deploy_plugins, dir) != opt.qt.deploy_plugins.end()) continue;
            for (auto const& r : roots)
                if (std::filesystem::is_directory(r / "plugins" / dir, ec)) { opt.qt.deploy_plugins.push_back(dir); break; }
        }
    }
    qt::outcome built = qt::compile_ex(opt.qt);
    if (!built.ok) return false;
    const auto& sdks = built.source.roots;
    if (sdks.empty()) return true;          // `mcpp.rules.qt` has said so
    mcpp::fact("rules-qml.sdk", generic(sdks.front()).c_str());

    const fs::path main = sdks.front();
    const fs::path gen = fs::path(opt.qt.out_dir) / "qml";
    std::error_code ec;
    fs::create_directories(gen, ec);

    // The import directories: the project's, then every SDK's `qml/`.
    std::vector<fs::path> import_dirs;
    for (auto const& p : opt.import_paths) import_dirs.push_back(qt::detail::absolute_from_root(p));
    for (auto const& r : sdks)
        if (fs::is_directory(r / "qml", ec)) import_dirs.push_back(r / "qml");

    const std::string rcc = qt::detail::tool(sdks, "rcc");
    const std::string registrar = qt::detail::tool(sdks, "qmltyperegistrar");
    const std::string cachegen = qt::detail::tool(sdks, "qmlcachegen");

    // ── the modules: files, headers ──
    std::vector<detail::planned> plan;
    for (auto const& m : opt.modules) {
        for (auto const& r : sdks)
            if (fs::is_regular_file(r / "qml" / detail::uri_path(m.uri) / "qmldir", ec)) {
                std::cerr << std::format("{}: the module {} is a module of the Qt SDK at {}; name your own.\n", who, m.uri,
                                         generic(r / "qml" / detail::uri_path(m.uri)));
                return false;
            }
        detail::planned p;
        p.module = &m;
        p.san = detail::sanitize(m.uri);
        p.upath = detail::uri_path(m.uri);
        p.resource_dir = m.prefix + "/" + p.upath;
        detail::parse_version(m.version, p.major, p.minor);
        std::vector<fs::path> scripts, rest;
        auto place = [&](const fs::path& f) {
            auto& v = detail::is_script(f) ? scripts : rest;
            if (std::ranges::find(v, f) == v.end()) v.push_back(f);
        };
        for (auto const& f : detail::expand_all(m.sources, std::string(who), "module " + m.uri)) place(f);
        for (auto const& f : detail::expand_all(m.resources, std::string(who), "module " + m.uri)) place(f);
        std::ranges::sort(scripts);
        std::ranges::sort(rest);
        std::vector<fs::path> everything = scripts;
        everything.insert(everything.end(), rest.begin(), rest.end());
        p.root = m.root.empty() ? detail::common_directory(everything) : qt::detail::absolute_from_root(m.root);
        for (auto const& f : everything) {
            const std::string rel = generic(f.lexically_relative(p.root));
            if (rel.empty() || rel.starts_with("..")) {
                std::cerr << std::format("{}: {} is not under the root {} of module {}.\n", who, generic(f), generic(p.root), m.uri);
                return false;
            }
            p.files.emplace_back(f, rel);
        }
        for (auto const& s : detail::expand_all(m.shaders, std::string(who), "module " + m.uri)) p.shaders.push_back(s);
        p.dir = gen / p.upath;
        p.work = gen / p.san;
        p.qmldir = p.dir / "qmldir";
        p.qmltypes = p.dir / (p.san + ".qmltypes");
        p.qrc = p.work / (p.san + ".qrc");
        plan.push_back(std::move(p));
    }

    // Which header belongs to which module. Every header `moc` read that uses a
    // QML macro belongs to exactly one.
    {
        std::map<std::string, std::string> owner;    // header -> uri
        std::vector<const qt::moc_unit*> qml_units;
        for (auto const& u : built.mocs)
            if (detail::declares_qml_types(qt::detail::read_file(u.header))) qml_units.push_back(&u);
        for (auto& p : plan) {
            for (auto const& h : detail::expand_all(p.module->headers, std::string(who), "module " + p.module->uri)) {
                const std::string g = generic(h);
                const auto unit = std::ranges::find_if(built.mocs, [&](auto const& u) { return u.header == g; });
                if (unit == built.mocs.end()) {
                    detail::warn(std::format("{}: {} is listed for module {} and `moc` read no meta-object in it.", who, g, p.module->uri));
                    continue;
                }
                if (auto [it, fresh] = owner.try_emplace(g, p.module->uri); !fresh) {
                    std::cerr << std::format("{}: {} is listed for both {} and {}.\n", who, g, it->second, p.module->uri);
                    return false;
                }
                p.units.push_back(*unit);
            }
        }
        const bool explicit_headers = std::ranges::any_of(plan, [](auto const& p) { return !p.module->headers.empty(); });
        for (auto const* u : qml_units) {
            if (owner.contains(u->header)) continue;
            if (plan.size() == 1 && !explicit_headers) {
                plan.front().units.push_back(*u);
            } else {
                std::cerr << std::format("{}: {} registers QML types and belongs to no module; list it in the `headers` of one.\n",
                                         who, u->header);
                return false;
            }
        }
    }

    // ── the tools a module needs ──
    const bool have_modules = !plan.empty();
    bool aot_possible = !cachegen.empty();
    if (have_modules && rcc.empty()) {
        detail::warn(std::format("{}: `rcc` not found under {}; no QML module is built. QML needs the `xim:qt` payload: "
                                 "`xim:qt-base` carries the QtQml library only, without Quick.", who, generic(main)));
        plan.clear();
    }
    if (have_modules && !plan.empty() && cachegen.empty())
        detail::warn(std::format("{}: `qmlcachegen` not found under {}; the files are embedded as source, not compiled.", who, generic(main)));
    const bool can_register = !registrar.empty();
    if (have_modules && !plan.empty() && !can_register)
        detail::warn(std::format("{}: `qmltyperegistrar` not found under {}; C++ types are not registered.", who, generic(main)));

    // Every module's metadata is an input of every other's compiled form.
    std::vector<std::string> metadata;
    for (auto const& p : plan) {
        metadata.push_back(generic(p.qmldir));
        if (!p.units.empty() && can_register) metadata.push_back(generic(p.qmltypes));
    }
    std::string foreign_sdk;
    {
        std::vector<std::string> sdk_json;
        auto add_json = [&](const std::string& module) {
            std::string lower = module;
            for (std::size_t i = 0; i < lower.size(); ++i) lower[i] = char(std::tolower(static_cast<unsigned char>(lower[i])));
            for (auto const& r : sdks) {
                const fs::path f = r / "metatypes" / ("qt6" + lower + "_metatypes.json");
                if (fs::is_regular_file(f, ec)) { sdk_json.push_back(generic(f)); return; }
            }
        };
        add_json("Core");
        for (auto const& m : built.linked) if (m != "Core") add_json(m);
        foreign_sdk = detail::joined(sdk_json, ",");
    }
    std::set<std::string> include_dirs;
    std::set<std::string> claimed;
    auto claim = [&](const std::string& name, const std::string& from) {
        if (claimed.insert(name).second) return true;
        std::cerr << std::format("{}: two files produce `{}` (one is {}); rename one.\n", who, name, from);
        return false;
    };

    for (auto const& p : plan) {
        const auto& m = *p.module;
        fs::create_directories(p.dir, ec);
        fs::create_directories(p.work, ec);
        const bool types = !p.units.empty() && can_register;

        // The files the plan reads, and what it writes from them.
        for (auto const& [file, rel] : p.files)
            if (file.extension() == ".qml") mcpp::rerun_if_changed(generic(file).c_str());
        mcpp::plugins::fs::write_if_changed(p.qmldir, detail::qmldir_text(m, types, p.resource_dir, p.files));

        std::vector<std::pair<std::string, std::string>> qrc_files;
        for (auto const& [file, rel] : p.files) {
            if (!claim(p.san + "/" + rel, generic(file))) return false;
            qrc_files.emplace_back(rel, generic(file));
        }
        // Shaders are compiled when `qsb` is there; without it they are left out of the
        // resources, so the build still links and the warning says what is missing.
        std::string qsb;
        if (!p.shaders.empty()) {
            qsb = qt::detail::tool(sdks, "qsb");
            if (qsb.empty())
                detail::warn(std::format("{}: `qsb` not found; it is part of the additional libraries (`xim:qt-addons`), and "
                                         "the shaders of {} are not compiled.", who, m.uri));
        }
        std::vector<std::string> qsb_outputs;
        if (!qsb.empty()) {
            fs::create_directories(p.work / "shaders", ec);
            for (auto const& s : p.shaders) {
                const std::string name = s.filename().string() + ".qsb";
                if (!claim(p.san + "/shaders/" + name, generic(s))) return false;
                const std::string out = generic(p.work / "shaders" / name);
                qrc_files.emplace_back("shaders/" + name, out);
                qsb_outputs.push_back(out);
            }
        }
        mcpp::plugins::fs::write_if_changed(p.qrc, detail::qrc_text(p.resource_dir, generic(p.qmldir), qrc_files));

        // C++ types: the type description, then the registrations.
        if (types) {
            // What the registrar reads as its own, and what it may only refer to: the
            // SDK's types and every other moc'd header, a plain base class included.
            std::vector<std::string> jsons, foreign;
            if (!foreign_sdk.empty()) foreign.push_back(foreign_sdk);
            const std::size_t sdk_entries = foreign.size();
            for (auto const& u : p.units) jsons.push_back(u.json);
            for (auto const& u : built.mocs)
                if (!u.json.empty() && std::ranges::find(jsons, u.json) == jsons.end()) foreign.push_back(u.json);
            const std::string regs = generic(p.work / (p.san + "_qmltyperegistrations.cpp"));
            if (!claim(p.san + "_qmltyperegistrations.cpp", p.module->uri)) return false;
            detail::step s;
            s.id = "qml:registrar:" + m.uri;
            s.description = "QMLTYPEREGISTRAR " + m.uri;
            s.argv = { registrar, "--generate-qmltypes=" + generic(p.qmltypes), "--import-name=" + m.uri,
                       "--major-version=" + std::to_string(p.major), "--minor-version=" + std::to_string(p.minor),
                       "--foreign-types=" + detail::joined(foreign, ","), "-o", regs };
            for (auto const& j : jsons) s.argv.push_back(j);
            s.inputs = jsons;
            for (std::size_t i = sdk_entries; i < foreign.size(); ++i) s.inputs.push_back(foreign[i]);
            s.outputs = { regs, generic(p.qmltypes) };
            detail::submit(s);
            for (auto const& u : p.units) include_dirs.insert(generic(fs::path(u.header).parent_path()));
        }

        // Shaders.
        for (std::size_t i = 0; i < qsb_outputs.size(); ++i) {
            detail::step s;
            s.id = "qml:qsb:" + m.uri + ":" + p.shaders[i].filename().string();
            s.description = "QSB " + p.shaders[i].filename().string();
            s.argv = { qsb };
            for (auto const& a : m.qsb_args) s.argv.push_back(a);
            s.argv.push_back("-o");
            s.argv.push_back(qsb_outputs[i]);
            s.argv.push_back(generic(p.shaders[i]));
            s.inputs = { generic(p.shaders[i]) };
            s.outputs = { qsb_outputs[i] };
            // A library directory the tool loads from, for an SDK whose tools are not
            // already linked to it.
            if (!qt::detail::is_windows() && !qt::detail::is_macos()) {
                std::vector<std::string> libs;
                for (auto const& r : sdks) libs.push_back(generic(r / "lib"));
                s.env.emplace_back("LD_LIBRARY_PATH", detail::joined(libs, ":"));
            }
            detail::submit(s);
        }

        // The resources.
        {
            const std::string out = generic(p.work / ("qrc_" + p.san + ".cpp"));
            if (!claim("qrc_" + p.san + ".cpp", m.uri)) return false;
            detail::step s;
            s.id = "qml:rcc:" + m.uri;
            s.description = "RCC " + m.uri;
            // `rcc --depfile` lists the files the qrc names: what the action reads
            // beyond the inputs it declares (a file the qrc gains is one).
            s.depfile = out + ".d";
            s.argv = { rcc, "--name", p.san, "--depfile", s.depfile, generic(p.qrc), "-o", out };
            s.inputs = { generic(p.qrc), generic(p.qmldir) };
            for (auto const& [file, rel] : p.files) s.inputs.push_back(generic(file));
            for (auto const& q : qsb_outputs) s.inputs.push_back(q);
            s.outputs = { out };
            detail::submit(s);
        }

        // The compiled form of each file, and the loader that registers them.
        if (m.aot && aot_possible) {
            std::string rsp = "--resource\n" + generic(p.qrc) + "\n";
            for (auto const& [file, rel] : p.files) {
                if (!detail::is_script(file)) continue;
                const std::string out = generic(p.work / (p.san + "_" + detail::flattened(rel) + ".cpp"));
                if (!claim(p.san + "_" + detail::flattened(rel) + ".cpp", generic(file))) return false;
                detail::step s;
                s.id = "qml:cachegen:" + m.uri + ":" + rel;
                s.description = "QMLCACHEGEN " + rel;
                s.argv = { cachegen, "--bare", "--resource-path", p.resource_dir + "/" + rel };
                for (auto const& d : import_dirs) { s.argv.push_back("-I"); s.argv.push_back(generic(d)); }
                s.argv.insert(s.argv.end(), { "-I", generic(gen), "-i", generic(p.qmldir), "--resource", generic(p.qrc), "-o", out, generic(file) });
                s.inputs = { generic(file), generic(p.qrc) };
                for (auto const& x : metadata) s.inputs.push_back(x);
                s.outputs = { out };
                detail::submit(s);
                rsp += p.resource_dir + "/" + rel + "\n";
            }
            const fs::path rsp_file = p.work / "loader.rsp";
            mcpp::plugins::fs::write_if_changed(rsp_file, rsp);
            const std::string loader = generic(p.work / (p.san + "_qmlcache_loader.cpp"));
            if (!claim(p.san + "_qmlcache_loader.cpp", m.uri)) return false;
            detail::step s;
            s.id = "qml:cachegen:" + m.uri + ":loader";
            s.description = "QMLCACHEGEN " + m.uri + " loader";
            // The output must end in `qmlcache_loader.cpp`: that name is how
            // `qmlcachegen` is told to write the loader and not one file.
            s.argv = { cachegen, "--resource-name", "qmlcache_" + p.san, "-o", loader, "@" + generic(rsp_file) };
            s.inputs = { generic(rsp_file), generic(p.qrc) };
            s.outputs = { loader };
            detail::submit(s);
        }

        // The check.
        if (opt.lint) {
            const std::string lint = qt::detail::tool(sdks, "qmllint");
            if (lint.empty()) {
                detail::warn(std::format("{}: `qmllint` not found under {}; the files are not checked.", who, generic(main)));
            } else {
                for (auto const& [file, rel] : p.files) {
                    if (file.extension() != ".qml") continue;
                    detail::step s;
                    s.role = mcpp::roles::check;
                    s.id = "qml:lint:" + m.uri + ":" + rel;
                    s.description = "QMLLINT " + rel;
                    s.argv = { lint, "-I", generic(gen) };
                    for (auto const& d : import_dirs) { s.argv.push_back("-I"); s.argv.push_back(generic(d)); }
                    if (opt.lint_warnings_are_errors) { s.argv.push_back("--max-warnings"); s.argv.push_back("0"); }
                    s.argv.push_back(generic(file));
                    s.inputs = { generic(file) };
                    for (auto const& x : metadata) s.inputs.push_back(x);
                    detail::submit(s);
                }
            }
        }
    }
    for (auto const& d : include_dirs) mcpp::include_dir(d.c_str());

    // ── Qt's own modules the program imports ──
    if (opt.deploy_imports) {
        std::vector<std::string> roots = opt.imports, own;
        for (auto const& p : plan) own.push_back(p.module->uri);
        for (auto const& m : opt.modules) {
            // A module whose tools did not run is still the project's.
            if (std::ranges::find(own, m.uri) == own.end()) own.push_back(m.uri);
        }
        for (auto const& p : plan)
            for (auto const& [file, rel] : p.files)
                if (file.extension() == ".qml")
                    for (auto const& i : detail::imports_of(qt::detail::read_file(file))) roots.push_back(i);
        const auto found = detail::import_closure(roots, own, import_dirs, opt.styles);

        std::vector<std::string> notes, missing_qt;
        for (auto const& u : found.unresolved) (u.starts_with("Qt") ? missing_qt : notes).push_back(u);
        if (!missing_qt.empty())
            detail::warn(std::format("{}: the Qt module(s) {} are imported and not in the SDK ({}/qml); a program that loads "
                                     "them will not find them. QML needs `xim:qt`; `xim:qt-base` carries the QtQml library only.",
                                     who, detail::joined(missing_qt, ", "), generic(main)));
        if (!notes.empty())
            std::cerr << std::format("{}: {} import(s) are in no SDK or import path and are not placed: {}. A type "
                                     "registered by the program at run time is one.\n", who, notes.size(), detail::joined(notes, ", "));

        for (auto const& mod : found.modules) {
            const fs::path origin = [&] {
                for (auto const& d : import_dirs)
                    if (fs::is_regular_file(d / detail::uri_path(mod.uri) / "qmldir", ec)) return d;
                return fs::path();
            }();
            for (auto const& f : detail::module_files(mod.dir)) {
                const std::string to = "qml/" + generic(f.parent_path().lexically_relative(origin));
                mcpp::deploy(generic(f).c_str(), to.c_str());
            }
        }
    }

    // ── qt.conf ──
    if (opt.write_qt_conf && (opt.deploy_imports || !plan.empty())) {
        if (detail::project_has_qt_conf(opt.qt)) {
            detail::warn(std::format("{}: the project has its own qt.conf, so none is written. Qt then finds the deployed QML "
                                     "modules only if that file sets `Qml2Imports = qml` and `Prefix = .`.", who));
        } else {
            std::string conf = "[Paths]\nPrefix = .\nQml2Imports = qml\n";
            if (opt.qt.i18n.deploy_to != "translations") conf += "Translations = " + opt.qt.i18n.deploy_to + "\n";
            const fs::path file = gen / "qt.conf";
            mcpp::plugins::fs::write_if_changed(file, conf);
            mcpp::deploy(generic(file).c_str(), ".");
        }
    }
    return true;
}

} // namespace mcpp::rules::qml
