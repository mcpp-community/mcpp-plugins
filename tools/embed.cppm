// mcpp.tools.embed - a data file becomes a header the program compiles in.
//
// WHY THIS IS A TOOL AND NOT A RULE. A rule (`mcpp.rules.<x>`) states how a
// translation unit is compiled by a compiler mcpp does not drive: it submits an
// action, the engine schedules it, and the work happens in the build graph. A
// tool states something a build program needs that no compiler performs. This
// one reads bytes and writes a header, so there is no external program to
// schedule and no action to submit; the file is written while `build.mcpp`
// runs, before the engine plans anything.
//
// THE OUTPUT IS A HEADER, FOR THE REASON `mcpp.rules.spirv` GIVES. A data file
// beside the binary makes the program's correctness depend on its working
// directory. A header compiled into the program does not, and `mcpp pack` of
// that program has nothing further to collect.
//
// IT DOES NOT REWRITE AN UNCHANGED HEADER. Writing the same bytes again would
// still move the file's mtime, and every translation unit that includes it
// would rebuild on a build where nothing changed. The comparison is on content
// and is the reason this tool is safe to call unconditionally from a build
// program that runs on every configure.
//
// WHAT IT DELIBERATELY DOES NOT DO. It does not compress, does not chunk a
// large file across several arrays, and does not emit a `std::span` accessor.
// Each is reachable from what it emits, and a tool in this collection earns a
// feature by being needed by more than one consumer.
module;
#include <cctype>
#include <cstdio>

export module mcpp.tools.embed;

import std;
import mcpp;
// The lib root, which carries `mcpp::plugins::surface` -- the declarations a
// consumer names. `group()` below hands its payloads to it, so a set of files
// embedded by this tool and a set of shaders compiled by `mcpp.rules.spirv`
// reach a consumer through the same shape.
import mcpp.plugins;
import mcpp.plugins.declare;


// WHY NOTHING HERE USES `std::println`, AND WHY THAT IS NOT A STYLE CHOICE.
//
// `std::print` and `std::println` are not header-only. Both of their overloads
// reach into the libc++ DYLIB -- `__is_posix_terminal(FILE*)` for the stdout
// form and `__get_ostream_file(ostream&)` for the stream form -- and those
// symbols were added to that library in a version macOS 14 does not ship. A
// build program's link resolves `-lc++` to the system copy there, so a rule
// that printed with `std::println` compiled and then failed to link:
//
//   ld64.lld: error: undefined symbol: std::__1::__is_posix_terminal(__sFILE*)
//
// naming neither the call that needed it nor the reason. Measured on
// macos-14; macos-15 has the symbol, which is why nothing saw this until a
// rule was first compiled on the older of the two supported releases.
//
// `std::format` is header-only and has no such dependency, so every message in
// this file is formatted and then streamed.

export namespace mcpp::tools::embed {

// The element the array is made of. A byte array is the general answer; a
// 32-bit word array is what an API that takes `const uint32_t*` wants -- SPIR-V
// is the case that exists in this repository -- and asking for it here is
// cheaper than a reinterpret_cast at every call site, which is undefined
// behaviour on an under-aligned byte array.
enum class element { byte_, word32 };

struct options {
    // Where the header is written. Empty means `<out_dir>/include/mcpp.tools.embed`,
    // which is added to the include path; a caller that names a directory owns
    // adding it.
    std::string out_dir;
    // The C++ identifier the array is called. Empty derives it from the input
    // file name: every character that is not alphanumeric becomes `_`, and a
    // leading digit is prefixed with `_`.
    std::string identifier;
    // An optional namespace for the two symbols. Nested namespaces are written
    // with `::` and emitted as a C++17 nested definition.
    std::string name_space;
    element     elem = element::byte_;
    // A trailing zero byte, so the array is usable as a C string. It is counted
    // by neither `_size` nor the array's declared bound comment; the array
    // simply has one more element than `_size` says.
    bool        null_terminate = false;
    // Bytes per line in the generated file. Only the file's shape depends on
    // it; no consumer can observe it.
    unsigned    width = 16;
};

// How a `table()` row's key is derived from its input path. `table()` refuses
// two inputs that derive one key -- see `table()` below -- so this also
// decides which inputs may sit in one table together.
enum class key_kind {
    // "Standard.vert". The default, because it is what tells `Standard.vert`
    // from `Standard.frag` apart and the bare stem below cannot.
    file_name,
    // "Standard" -- the file name with its extension removed. Collides with
    // `file_name` whenever two inputs differ only by extension, which is the
    // ordinary shape of a shader set, so this is an opt-in for a caller who
    // has made the stem unique some other way rather than a safer default.
    stem,
    // The input's path relative to `mcpp::manifest_dir()`, e.g.
    // "shaders/ui/panel.vert". For a nested input set where two directories
    // hold a file of the same name -- where `file_name` collides -- this is
    // the way out.
    relative_path,
};

// `table()`'s options. A separate type from `options` rather than a second
// meaning for its fields: `options::identifier` names one symbol, so
// `files()` refuses a caller who sets it for several inputs rather than
// silently applying it to the first -- see `files()`. A table writes exactly
// one array and one struct regardless of how many inputs feed it, so there is
// always exactly one name to give, and `table_options::identifier` defaults
// to one instead of being refused or left for the caller to discover is
// required.
struct table_options {
    // Means what it means in `options`.
    std::string out_dir;
    // The one symbol this call writes: it names the array and the header's
    // file name (`<identifier>.h`), the same way `identifier_for` names
    // `file()`'s. Defaulted rather than derived, because there is no single
    // input this call can derive a name from the way `file()` derives one
    // from each input's own file name.
    std::string identifier = "embedded_table";
    // The generated row struct's name. Two `table()` calls sharing a
    // namespace need this and `identifier` to differ, the same way two
    // `file()` calls sharing a namespace need different `options::identifier`s:
    // the type is as much a symbol as the array is, and this generator does
    // not check that it is unique any more than `file()` checks `identifier`.
    std::string row_type = "embedded_file";
    // Means what it means in `options`.
    std::string name_space;
    // Means what it means in `options`, including `word32`'s "not a multiple
    // of 4" refusal -- checked here per input, since a table has several.
    element     elem = element::byte_;
    // Means what it means in `options`.
    unsigned    width = 16;
    key_kind    key = key_kind::file_name;
};

// ---- internals -------------------------------------------------------------

// The accessor's own name, so a file called `default.bin` must not produce
// `default()`. The lib root owns that decision; this is the one caller that
// needs it here.
inline std::string sanitise(std::string_view stem) {
    return mcpp::plugins::surface::identifier(stem, "data");
}

inline std::string default_dir() {
    const char* out = mcpp::out_dir();
    return (std::filesystem::path(out && *out ? out : ".") / "include" / "mcpp.tools.embed").string();
}

inline std::string identifier_for(const std::filesystem::path& input, const options& opt) {
    if (!opt.identifier.empty()) return opt.identifier;
    auto stem = input.filename().string();
    return sanitise(stem);
}

// The header a given input produces, without producing it. A consumer that
// wants to `#include` it by an explicit path rather than by name asks here.
inline std::string header_path(const std::filesystem::path& input, const options& opt = {}) {
    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    return (std::filesystem::path(dir) / (identifier_for(input, opt) + ".h")).string();
}

// The header `table()` will produce, without producing it. Mirrors
// `header_path()`: a consumer that wants to `#include` it by an explicit path
// rather than by name asks here. Takes no input path, unlike `header_path()`,
// because a table's file name comes from `table_options::identifier` rather
// than from any one of its inputs.
inline std::string table_header_path(const table_options& opt = {}) {
    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    return (std::filesystem::path(dir) / (opt.identifier + ".h")).string();
}

inline bool write_if_different(const std::filesystem::path& path, std::string_view text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == text) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// The key `table()` puts in a row, from an input already resolved to an
// absolute path. `root` is `mcpp::manifest_dir()`, threaded through rather
// than read here so this stays a function of its arguments alone -- the same
// reason `identifier_for` above takes `input` rather than resolving it itself.
inline std::string table_key_for(const std::filesystem::path& absolute,
                                 const std::string& root, key_kind kind) {
    switch (kind) {
        case key_kind::stem:
            return absolute.stem().string();
        case key_kind::relative_path:
            // `generic_string()`, not `string()`: the key is compared and then
            // written into a generated file, and `lexically_relative` on
            // Windows appends the PREFERRED separator -- `\` -- which would
            // make one input's key differ by host for no reason a caller
            // wrote. `mcpp::plugins::names::namespace_of` documents the same
            // fix for the same reason: it was measured on windows-2022, where
            // the equivalent path arithmetic produced a namespace segment no
            // Linux or macOS run of the same fixture ever saw.
            return absolute.lexically_relative(root).generic_string();
        case key_kind::file_name:
        default:
            return absolute.filename().string();
    }
}

// A row's key becomes a C string literal in the generated header, and two
// characters cannot appear in one unescaped: an unescaped quote would close
// the literal early, and an unescaped backslash would fold the character
// after it into an escape sequence instead of leaving it as itself. Both are
// ordinary characters in a POSIX file name, so this is not a hypothetical the
// way it would be for an identifier.
inline std::string quote_for_literal(std::string_view s) {
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// The numeric array `file()` writes, factored out because `table()` writes
// one such array per row instead of one per call. Takes no `null_terminate`:
// `table_options` has no such field -- a row's `size` is exact by
// construction, one member on one row, rather than a shared option applied
// across a whole file -- and `file()` keeps its own copy of this loop rather
// than being rewritten to call through a helper it has no use for, so its
// already-measured output does not change.
inline std::string element_array(std::string_view name, std::string_view bytes,
                                 element elem, unsigned width) {
    const bool word = elem == element::word32;
    const auto count = word ? bytes.size() / 4 : bytes.size();
    std::string text = word ? "inline constexpr std::uint32_t " : "inline constexpr unsigned char ";
    text += name;
    text += "[] = {";
    const unsigned per_line = width == 0 ? 16 : width;
    for (std::size_t i = 0; i < count; ++i) {
        if (i % per_line == 0) text += "\n    ";
        if (word) {
            const auto b = reinterpret_cast<const unsigned char*>(bytes.data()) + i * 4;
            text += std::format("0x{:08x}u,", static_cast<std::uint32_t>(b[0])
                                            | (static_cast<std::uint32_t>(b[1]) << 8)
                                            | (static_cast<std::uint32_t>(b[2]) << 16)
                                            | (static_cast<std::uint32_t>(b[3]) << 24));
        } else {
            text += std::format("0x{:02x},", static_cast<unsigned>(
                static_cast<unsigned char>(bytes[i])));
        }
        if (i + 1 < count) text += ' ';
    }
    text += "\n};\n";
    return text;
}

// What `table()`'s duplicate-key refusal tells a caller to do about it, which
// depends on which derivation produced the collision: the fix for one is
// switching away from it, and the fix for another is that switching to it is
// what was already tried.
inline std::string_view key_collision_hint(key_kind kind) {
    switch (kind) {
        case key_kind::file_name:
            return "Two inputs with the same name in different directories collide "
                   "under the default; table_options::key = key_kind::relative_path "
                   "tells them apart by directory.";
        case key_kind::stem:
            return "key_kind::stem drops the extension, so two inputs differing only "
                   "by it collide; key_kind::file_name, the default, keeps it.";
        case key_kind::relative_path:
        default:
            return "key_kind::relative_path is already the input's path below the "
                   "manifest directory, so this is the same input named twice.";
    }
}

// ---- the tool ---------------------------------------------------------------

// One file. Returns false and explains on stderr when the input cannot be read
// or the header cannot be written; a build program returns that value.
inline bool file(const std::filesystem::path& input, options opt = {}) {
    const std::string root = mcpp::manifest_dir();
    const auto absolute = input.is_absolute()
                        ? input : std::filesystem::path(root) / input;

    std::ifstream in(absolute, std::ios::binary);
    if (!in) {
        std::cerr << std::format("mcpp.tools.embed: cannot read {}", absolute.string()) << '\n';
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    if (opt.elem == element::word32 && bytes.size() % 4 != 0) {
        std::cerr << std::format("mcpp.tools.embed: {} is {} bytes, which is not a multiple of 4, and "
            "element::word32 was asked for", absolute.string(), bytes.size()) << '\n';
        return false;
    }

    const auto id  = identifier_for(absolute, opt);
    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    const auto out = std::filesystem::path(dir) / (id + ".h");

    std::string text;
    text += "// Generated by mcpp.tools.embed from ";
    text += absolute.filename().string();
    text += ". Do not edit.\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n";

    std::vector<std::string> opened;
    if (!opt.name_space.empty()) {
        text += "namespace " + opt.name_space + " {\n\n";
    }

    const bool word = opt.elem == element::word32;
    const auto count = word ? bytes.size() / 4 : bytes.size();
    text += word ? "inline constexpr std::uint32_t " : "inline constexpr unsigned char ";
    text += id;
    text += "[] = {";
    const unsigned per_line = opt.width == 0 ? 16 : opt.width;
    for (std::size_t i = 0; i < count; ++i) {
        if (i % per_line == 0) text += "\n    ";
        if (word) {
            const auto b = reinterpret_cast<const unsigned char*>(bytes.data()) + i * 4;
            text += std::format("0x{:08x}u,", static_cast<std::uint32_t>(b[0])
                                            | (static_cast<std::uint32_t>(b[1]) << 8)
                                            | (static_cast<std::uint32_t>(b[2]) << 16)
                                            | (static_cast<std::uint32_t>(b[3]) << 24));
        } else {
            text += std::format("0x{:02x},", static_cast<unsigned>(
                static_cast<unsigned char>(bytes[i])));
        }
        if (i + 1 < count) text += ' ';
    }
    if (opt.null_terminate && !word) {
        if (count % per_line == 0) text += "\n    ";
        text += "0x00,";
    }
    text += "\n};\n\n";
    text += std::format("inline constexpr std::size_t {}_size = {};\n", id, count);
    if (!opt.name_space.empty()) text += "\n} // namespace " + opt.name_space + "\n";

    if (!write_if_different(out, text)) {
        std::cerr << std::format("mcpp.tools.embed: cannot write {}", out.string()) << '\n';
        return false;
    }

    // The build program is cached on its inputs, so a file it reads is a file
    // it must declare: without this, editing the data leaves the header from
    // the previous build in place and the program compiles yesterday's bytes.
    mcpp::rerun_if_changed(absolute.string().c_str());
    if (opt.out_dir.empty()) mcpp::include_dir(dir.c_str());
    return true;
}

// Several files, sharing one set of options. The identifier is derived per
// file, so `options::identifier` is refused here rather than silently applied
// to the first input only.
inline bool files(std::span<const std::string> inputs, options opt = {}) {
    if (!opt.identifier.empty()) {
        std::cerr << std::format("mcpp.tools.embed: options::identifier names one "
                             "symbol and files() writes several; call file() per input") << '\n';
        return false;
    }
    for (auto const& one : inputs)
        if (!file(one, opt)) return false;
    return true;
}

// Several files, reached through ONE declaration a consumer imports.
//
// `files()` writes a header per input and leaves the consumer to include each
// by name. `group()` writes those same headers and then hands them to
// `mcpp::plugins::surface`, so the consumer writes one `import` and names no
// generated file -- the same surface `mcpp.rules.spirv` produces, from the same
// generator, because a payload that was already on disk and one a compiler
// produced are the same thing to whoever consumes it.
//
// The group's own name is required rather than derived. A rule knows what its
// payloads are for and can name the module `<package>.shaders`; a tool called
// on an arbitrary set of files does not, and a derived name would be a guess
// that two calls in one build program could collide on.
inline bool group(std::span<const std::string> inputs,
                  const std::string& module_name,
                  mcpp::plugins::surface::kind surface
                      = mcpp::plugins::surface::default_surface(),
                  options opt = {}) {
    if (inputs.empty()) return true;
    if (module_name.empty()) {
        std::cerr << "mcpp.tools.embed: group() needs a module name; it is what a "
                     "consumer imports and the namespace the declarations sit in\n";
        return false;
    }
    if (!files(inputs, opt)) return false;

    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    std::vector<mcpp::plugins::surface::item> items;
    for (auto const& one : inputs) {
        const auto absolute = std::filesystem::path(one).is_absolute()
                            ? std::filesystem::path(one)
                            : std::filesystem::path(mcpp::manifest_dir()) / one;
        const auto id = identifier_for(absolute, opt);
        // `files()` wrote `<id>.h` beside its siblings, so the include is the
        // bare name: this tool's generated tree is flat, unlike a rule's, which
        // mirrors the source tree it globbed.
        //
        // The symbol is QUALIFIED by `options::name_space`, because that is
        // where `file()` put the array. Passing the bare name compiles for a
        // caller that left the option empty and fails for one that did not,
        // which is the shape of a defect that only the second test finds.
        const auto sym = opt.name_space.empty() ? id : opt.name_space + "::" + id;
        items.push_back({ .identifier  = id,
                          .name_space  = {},
                          .data_header = id + ".h",
                          .data_symbol = sym,
                          // `<id>_size` IS A COUNT OF ELEMENTS, AND THE SURFACE
                          // REPORTS BYTES. For `element::byte_` the two are the
                          // same number and the distinction is invisible; for
                          // `word32` it is four times out.
                          //
                          // `sizeof` is not the answer either: `null_terminate`
                          // appends a zero byte that `_size` deliberately does
                          // not count, so `sizeof` is one too many. Measured on
                          // a group of null-terminated text payloads, which
                          // reported one extra byte and failed the fixture's
                          // comparison on a trailing NUL.
                          .data_size_expr = opt.elem == element::word32
                                          ? sym + "_size * 4"
                                          : sym + "_size" });
    }

    mcpp::plugins::surface::options so;
    so.surface     = surface;
    so.elem        = opt.elem == element::word32
                   ? mcpp::plugins::surface::element::word32
                   : mcpp::plugins::surface::element::byte_;
    so.module_name = module_name;
    so.out_dir     = dir;
    so.produced_by = "mcpp.tools.embed";
    // Answered here, not read there: `mcpp.plugins.surface` compiles into a
    // plain binary as well as into this build program, so it takes its inputs.
    so.target_os          = mcpp::target_os();
    so.has_gas_assembler  = std::string_view(mcpp::compiler()) != "msvc";

    const auto out = mcpp::plugins::surface_for(items, so);
    return out.ok;
}

// N inputs, ONE header, ONE table: every row carries its input's key beside
// its bytes, and the consumer iterates the array or looks a row up by key.
// `files()` writes N headers for N inputs and leaves each included by name;
// this is the shape for a set the consumer wants to walk rather than name
// member by member -- a shader set is the motivating case, where a renderer
// wants "every shader the project has" rather than one accessor per shader.
//
// THE ROW STRUCT IS GENERATED HERE, BESIDE THE ARRAY, FOR THE REASON 0.2.6
// FIXED FOR `mcpp.rules.spirv`'s HEADER: a generated header has to be
// includable on its own. Leaving the row type for the consumer to declare by
// hand would make it a second copy of a decision -- the field order and the
// element type both have to match this generator exactly -- and the failure
// mode of a mismatch is a device API reading a struct through the wrong
// layout, not a compile error.
//
// EACH ROW'S BYTES ARE A NUMERIC ARRAY, NEVER A RAW STRING LITERAL. A raw
// string literal delimits on a fixed marker -- `)"` closes `R"(...)"` -- and no
// byte sequence in an arbitrary payload is excluded strongly enough to promise
// it never contains that marker: shader source can carry it by accident, and a
// binary payload can carry it by construction. The motivating case for this
// entry point built its shader table by concatenating each file's text into
// one string in the build script, which is this exact bug -- any shader
// containing that four-character sequence truncates the string at that point,
// and every shader concatenated after it goes missing, with nothing but an
// unrelated compiler error to show for it. A numeric array has no delimiter
// for a payload to contain.
inline bool table(std::span<const std::string> inputs, table_options opt = {}) {
    if (inputs.empty()) return true;
    if (opt.identifier.empty()) {
        std::cerr << "mcpp.tools.embed: table() needs `table_options::identifier`; "
                     "it names the one array and header this call writes\n";
        return false;
    }
    if (opt.row_type.empty()) {
        std::cerr << "mcpp.tools.embed: table() needs `table_options::row_type`; "
                     "it names the struct each row is an instance of\n";
        return false;
    }

    const std::string root = mcpp::manifest_dir();

    struct resolved {
        std::filesystem::path absolute;
        std::string           original;   // as the caller wrote it, for diagnostics
        std::string           key;
    };
    std::vector<resolved> rows;
    rows.reserve(inputs.size());
    for (auto const& one : inputs) {
        const std::filesystem::path p(one);
        const auto absolute = p.is_absolute() ? p : std::filesystem::path(root) / p;
        rows.push_back({ absolute, one, table_key_for(absolute, root, opt.key) });
    }

    // TWO INPUTS PRODUCING ONE KEY ARE REFUSED HERE, BEFORE EITHER IS READ.
    //
    // A row is looked up by key, so letting this through would mean whichever
    // row is kept depends on iteration order and the other is lost with no
    // message at all -- the failure this shape is most exposed to, because a
    // build that silently dropped a row still links and runs. `mcpp.rules.spirv`
    // refuses the equivalent collision (two shaders producing one output) the
    // same way: naming both inputs rather than only the one seen second.
    {
        std::map<std::string, std::string> seen;   // key -> first input
        for (auto const& r : rows) {
            auto [it, fresh] = seen.try_emplace(r.key, r.original);
            if (!fresh) {
                std::cerr << std::format(
                    "mcpp.tools.embed: table() found two inputs that produce one key.\n"
                    "    {}\n"
                    "    {}\n"
                    "  both produce the key `{}`. A row is looked up by key, so keeping\n"
                    "  both would mean the second silently replaces the first rather\n"
                    "  than joining it. {}",
                    it->second, r.original, r.key, key_collision_hint(opt.key)) << '\n';
                return false;
            }
        }
    }

    const bool word = opt.elem == element::word32;
    std::vector<std::string> bytes(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        std::ifstream in(rows[i].absolute, std::ios::binary);
        if (!in) {
            std::cerr << std::format("mcpp.tools.embed: cannot read {}",
                                     rows[i].absolute.string()) << '\n';
            return false;
        }
        bytes[i].assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (word && bytes[i].size() % 4 != 0) {
            std::cerr << std::format("mcpp.tools.embed: {} is {} bytes, which is not a "
                "multiple of 4, and element::word32 was asked for",
                rows[i].absolute.string(), bytes[i].size()) << '\n';
            return false;
        }
    }

    const auto id  = opt.identifier;
    const auto dir = opt.out_dir.empty() ? default_dir() : opt.out_dir;
    const auto out = std::filesystem::path(dir) / (id + ".h");

    std::string text;
    text += std::format("// Generated by mcpp.tools.embed::table() from {} inputs. Do "
                        "not edit.\n", rows.size());
    text += "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n";

    if (!opt.name_space.empty()) text += "namespace " + opt.name_space + " {\n\n";

    text += std::format(
        "// One row per input: its key, a pointer to its bytes, and how many\n"
        "// elements `data` points to -- words under element::word32, bytes\n"
        "// otherwise, so a byte count is size * sizeof(*data). Declared here,\n"
        "// beside the array below, so this header is includable on its own\n"
        "// with nothing else.\n"
        "struct {} {{\n"
        "    const char* key;\n"
        "    const {}* data;\n"
        "    std::size_t size;\n"
        "}};\n\n", opt.row_type, word ? "std::uint32_t" : "unsigned char");

    // EACH ROW'S BYTES ARE THEIR OWN ARRAY, NAMED BY INDEX RATHER THAN BY A
    // SANITISED KEY. The key itself is checked for collisions above; a
    // sanitised form of it is not, and two keys that sanitise to one
    // identifier -- "a.b" and "a_b" both become "a_b" -- would collide here if
    // this used it. That would be a second collision check guarding a name
    // nothing outside this function ever reads. The index cannot collide.
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto rowName = std::format("{}_{}", id, i);
        text += element_array(rowName, bytes[i], opt.elem, opt.width);
    }

    text += std::format("\ninline constexpr {} {}[] = {{\n", opt.row_type, id);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto count = word ? bytes[i].size() / 4 : bytes[i].size();
        text += std::format("    {{ {}, {}_{}, {} }},\n",
                            quote_for_literal(rows[i].key), id, i, count);
    }
    text += "};\n\n";
    text += std::format("inline constexpr std::size_t {}_size = {};\n", id, rows.size());

    if (!opt.name_space.empty()) text += "\n} // namespace " + opt.name_space + "\n";

    if (!write_if_different(out, text)) {
        std::cerr << std::format("mcpp.tools.embed: cannot write {}", out.string()) << '\n';
        return false;
    }

    // Same reason `file()` states it: the build program is cached on its
    // inputs, so a file it reads has to be declared, and this holds for every
    // one of the N inputs here rather than the one input `file()` has.
    for (auto const& r : rows) mcpp::rerun_if_changed(r.absolute.string().c_str());
    if (opt.out_dir.empty()) mcpp::include_dir(dir.c_str());
    return true;
}

} // namespace mcpp::tools::embed
