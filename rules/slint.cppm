// mcpp.rules.slint -- how a `.slint` file becomes part of a C++ program.
//
// Slint describes a user interface in its own language; `slint-compiler`
// turns each `.slint` file into a C++ header (and, with
// `compilation_units`, that many `.cpp` files) the program includes and
// compiles. One action per input, with the depfile the compiler writes, so an
// edited `.slint` file -- or one it imports -- regenerates that input alone.
// This is what upstream's CMake function `slint_target_sources` does, and its
// arguments and target properties are this rule's options:
//
//   NAMESPACE                  options::ns              --cpp-namespace
//   LIBRARY_PATHS name=path    options::library_paths   -L
//   COMPILATION_UNITS n        options::compilation_units  --cpp-file × n
//   EDITION                    options::edition         --edition
//   SLINT_STYLE                options::style           --style
//   SLINT_EMBED_RESOURCES      options::embed_resources --embed-resources
//   SLINT_SCALE_FACTOR         options::scale_factor    --scale-factor
//   SLINT_TRANSLATION_DOMAIN   options::translation_domain
//   SLINT_BUNDLE_TRANSLATIONS  options::bundle_translations
//
// A MODULE PER INPUT, IF ASKED. With `options::module`, the rule also writes
// a module interface unit for each input: the generated header in its global
// module fragment, and `export using` for what the `.slint` file exports and
// slint-compiler generates -- each component that inherits `Window` (through
// any chain of components), each exported struct, enum and global, under
// their exported names (`export { A as B }`), following `export … from`
// into the files it names. A program then writes `import app.ui;` instead of
// including the header. The header cannot simply be included inside
// `export { … }`: Clang refuses to export the out-of-class member
// definitions slint-compiler writes ("cannot export … not at namespace
// scope"). `options::exports` names the exports itself where the reading is
// not enough.
//
// WHERE SLINT COMES FROM IS THE PROJECT'S CHOICE, in this order; the first
// that answers decides, and `mcpp::fact("rules-slint.sdk", …)` records it:
//
//   1. `options::compiler` / `options::root` in the build program;
//   2. the environment variable `SLINT_COMPILER` (upstream CMake's cache
//      variable of the same name);
//   3. a Slint SDK the program depends on: the directory of `compat.slint`
//      (or `options::sdk_package`) in the dependency graph, whose include
//      directories and library the package itself provides;
//   4. a Slint payload the project declares in its own `[xlings]` table
//      (`xim:slint`), whose include directory, library and run-time
//      directory the rule then puts on the build, as `rules-qt` does for Qt.
//
// The rule declares no SDK and pins no version: the compiler and the runtime
// a program links come from one SDK, so the generated code and the library
// always agree.
//
// A MISSING SDK IS A WARNING. `mcpp emit build-database` plans a project on
// machines that never built it; the rule says what it could not find and
// declares nothing, so the plan succeeds and the build is where it fails.
export module mcpp.rules.slint;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.plugins.tool;

export namespace mcpp::rules::slint {

struct library_path {
    std::string name;   // the name `import { X } from "@name"` uses
    std::string path;   // a directory or a `.slint` file, relative to the package root
};

struct options {
    // `.slint` files, beside those named in `[build] sources`.
    std::vector<std::string> sources;
    std::string ns;                                // --cpp-namespace
    std::vector<library_path> library_paths;       // -L name=path
    std::vector<std::string> include_paths;        // -I
    unsigned compilation_units = 0;                // 0: the header holds everything
    std::string edition;                           // --edition
    std::string style;                             // --style (fluent, material, cupertino, cosmic, qt, native)
    std::string embed_resources;                   // as-absolute-path | embed-files | embed-for-software-renderer …
    std::string scale_factor;                      // --scale-factor
    std::string translation_domain;
    std::string bundle_translations;               // a directory of `<lang>/LC_MESSAGES/<domain>.po`
    bool no_default_translation_context = false;
    // A module interface unit per input: `module` for a single input,
    // `<module>.<stem>` for each of several. Empty writes no module.
    std::string module;
    // The names the module exports, when the reading of the `.slint` file is
    // not to be used. Applies to every input.
    std::vector<std::string> exports;
    // The SDK (see the header). `compiler` is a tool choice: a program path,
    // `tool::on_path()`, `tool::root(dir)`.
    mcpp::plugins::tool::choice compiler;
    std::string root;
    std::string sdk_package = "compat.slint";
    std::string out_dir = std::string(mcpp::out_dir());
};

namespace detail {

namespace fs = std::filesystem;

inline std::string generic(const fs::path& p) {
    std::string s = p.lexically_normal().generic_string();
    return s;
}

inline void warn(const std::string& message) {
    std::cerr << message << '\n';
    mcpp::warning(message.c_str());
}

inline bool is_windows() { return std::string_view(mcpp::target_os()) == "windows"; }

inline fs::path absolute_from_root(const std::string& p) {
    fs::path path(p);
    return path.is_absolute() ? path : fs::path(mcpp::manifest_dir()) / path;
}

inline std::vector<std::string> device_slint() {
    std::vector<std::string> out;
    const std::string all = mcpp::device_sources();
    std::size_t i = 0;
    while (i <= all.size()) {
        auto nl = all.find('\n', i);
        std::string one = all.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? all.size() + 1 : nl + 1;
        while (!one.empty() && (one.back() == ' ' || one.back() == '\r')) one.pop_back();
        if (one.size() > 6 && one.ends_with(".slint")) out.push_back(one);
    }
    return out;
}

inline std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// ── what a `.slint` file exports and slint-compiler generates ──────────────

// The text with comments removed and string literals emptied, so a keyword
// inside either is not read as one. The string of an `import`/`export … from`
// is kept, since it names a file.
inline std::string code_of(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (text.compare(i, 2, "/*") == 0) {
            auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            out += ' ';
        } else if (text[i] == '"') {
            std::size_t j = i + 1;
            while (j < text.size() && text[j] != '"') j += text[j] == '\\' ? 2 : 1;
            std::string literal = text.substr(i, std::min(j + 1, text.size()) - i);
            const auto tail = std::string_view(out).substr(out.size() > 16 ? out.size() - 16 : 0);
            out += tail.find("from") != std::string_view::npos ? literal : std::string("\"\"");
            i = j + 1;
        } else {
            out += text[i++];
        }
    }
    return out;
}

struct file_exports {
    std::map<std::string, std::string> components;     // name -> base ("" when none)
    std::set<std::string> data;                         // exported struct, enum, global
    std::vector<std::pair<std::string, std::string>> exported;   // (local name, exported name)
    std::vector<std::pair<std::string, fs::path>> from;          // (`{list}` or "*", file)
    std::map<std::string, fs::path> imported;           // local name -> file it comes from
};

inline fs::path resolve_import(const fs::path& from_dir, const std::string& spec,
                               const std::vector<library_path>& libs, const std::vector<std::string>& includes) {
    if (spec.starts_with("@")) {
        const auto slash = spec.find('/');
        const std::string name = spec.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
        for (auto const& l : libs)
            if (l.name == name) {
                fs::path base = absolute_from_root(l.path);
                return slash == std::string::npos ? base : base / spec.substr(slash + 1);
            }
        return {};
    }
    fs::path rel = from_dir / spec;
    std::error_code ec;
    if (fs::exists(rel, ec)) return rel;
    for (auto const& inc : includes) {
        fs::path p = absolute_from_root(inc) / spec;
        if (fs::exists(p, ec)) return p;
    }
    return {};   // std-widgets.slint and the other built-in libraries
}

inline file_exports read_exports(const fs::path& file, const options& opt) {
    file_exports fe;
    const std::string code = code_of(read_file(file));
    const fs::path dir = file.parent_path();
    static const std::regex component(R"re(\b(export\s+)?component\s+([A-Za-z_][\w-]*)\s*(?:inherits\s+([A-Za-z_][\w-]*))?)re");
    static const std::regex data(R"re(\bexport\s+(struct|enum|global)\s+([A-Za-z_][\w-]*))re");
    static const std::regex list(R"re(\bexport\s*\{([^}]*)\}\s*(?:from\s*"([^"]*)")?)re");
    static const std::regex star(R"re(\bexport\s*\*\s*from\s*"([^"]*)")re");
    static const std::regex importing(R"re(\bimport\s*\{([^}]*)\}\s*from\s*"([^"]*)")re");
    for (std::sregex_iterator it(code.begin(), code.end(), component), end; it != end; ++it) {
        fe.components[(*it)[2]] = (*it)[3];
        if ((*it)[1].matched) fe.exported.push_back({(*it)[2], (*it)[2]});
    }
    for (std::sregex_iterator it(code.begin(), code.end(), data), end; it != end; ++it) {
        fe.data.insert((*it)[2]);
        fe.exported.push_back({(*it)[2], (*it)[2]});
    }
    auto names = [](const std::string& body) {
        std::vector<std::pair<std::string, std::string>> out;
        static const std::regex item(R"re(([A-Za-z_][\w-]*)(?:\s+as\s+([A-Za-z_][\w-]*))?)re");
        for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it)
            out.push_back({(*it)[1], (*it)[2].matched ? std::string((*it)[2]) : std::string((*it)[1])});
        return out;
    };
    for (std::sregex_iterator it(code.begin(), code.end(), importing), end; it != end; ++it) {
        fs::path target = resolve_import(dir, (*it)[2], opt.library_paths, opt.include_paths);
        for (auto const& [name, local] : names((*it)[1])) fe.imported[local] = target;
    }
    for (std::sregex_iterator it(code.begin(), code.end(), list), end; it != end; ++it) {
        const bool re_export = (*it)[2].matched;
        fs::path target = re_export ? resolve_import(dir, (*it)[2], opt.library_paths, opt.include_paths) : fs::path{};
        for (auto const& [name, as] : names((*it)[1])) {
            fe.exported.push_back({name, as});
            if (re_export) fe.imported[name] = target;
        }
    }
    for (std::sregex_iterator it(code.begin(), code.end(), star), end; it != end; ++it)
        fe.from.push_back({"*", resolve_import(dir, (*it)[1], opt.library_paths, opt.include_paths)});
    return fe;
}

// The names slint-compiler generates for `file` (its exported windows,
// structs, enums and globals, under their exported names), every file it
// reads recorded in `read`.
inline std::vector<std::string> generated_names(const fs::path& file, const options& opt, std::set<fs::path>& read) {
    std::map<fs::path, file_exports> files;
    std::function<const file_exports&(const fs::path&)> load = [&](const fs::path& f) -> const file_exports& {
        auto it = files.find(f);
        if (it != files.end()) return it->second;
        read.insert(f);
        return files.emplace(f, read_exports(f, opt)).first->second;
    };
    // Is `name`, as known in `f`, a component that (eventually) inherits Window?
    std::function<bool(const fs::path&, const std::string&, int)> is_window = [&](const fs::path& f, const std::string& name, int depth) -> bool {
        if (depth > 32) return false;
        if (name == "Window" || name == "Dialog") return true;
        const auto& fe = load(f);
        if (auto c = fe.components.find(name); c != fe.components.end())
            return !c->second.empty() && is_window(f, c->second, depth + 1);
        if (auto i = fe.imported.find(name); i != fe.imported.end() && !i->second.empty())
            return is_window(i->second, name, depth + 1);
        return false;
    };
    std::function<bool(const fs::path&, const std::string&, int)> is_data = [&](const fs::path& f, const std::string& name, int depth) -> bool {
        if (depth > 32) return false;
        const auto& fe = load(f);
        if (fe.data.contains(name)) return true;
        if (auto i = fe.imported.find(name); i != fe.imported.end() && !i->second.empty())
            return is_data(i->second, name, depth + 1);
        return false;
    };
    std::vector<std::string> out;
    std::function<void(const fs::path&, int)> collect = [&](const fs::path& f, int depth) {
        if (depth > 32) return;
        const auto& fe = load(f);
        for (auto const& [local, as] : fe.exported)
            if (is_window(f, local, 0) || is_data(f, local, 0)) out.push_back(as);
        for (auto const& [what, target] : fe.from)
            if (!target.empty()) collect(target, depth + 1);
    };
    collect(file, 0);
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    // slint-compiler names C++ entities as written, with `-` as `_`.
    for (auto& n : out) std::ranges::replace(n, '-', '_');
    return out;
}

// ── the SDK ────────────────────────────────────────────────────────────────

struct sdk {
    std::string compiler;
    std::string root;        // set when the SDK is not a package of the graph
    std::string level;
};

inline sdk locate(const options& opt) {
    namespace tool = mcpp::plugins::tool;
    sdk out;
    const std::string exe = is_windows() ? "slint-compiler.exe" : "slint-compiler";
    if (!opt.compiler.is_default() || !opt.root.empty()) {
        tool::spec s{.who = "mcpp.rules.slint", .ns = "xim", .package = "slint",
                     .programs = {"slint-compiler"}, .bin_dirs = {"bin", ""},
                     .option = "options::compiler", .legacy_env = "", .request = false};
        auto f = tool::resolve(s, opt.root.empty() ? opt.compiler : tool::root(opt.root));
        if (f) return {f.program, opt.root.empty() ? f.root : opt.root, "options"};
    }
    if (const char* env = std::getenv("SLINT_COMPILER"); env && *env)
        return {generic(env), {}, "SLINT_COMPILER"};
    if (const std::string dir = mcpp::dep_dir(opt.sdk_package.c_str()); !dir.empty()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->path().filename() == exe && it->path().parent_path().filename() == "bin")
                return {generic(it->path()), {}, opt.sdk_package};
    }
    tool::spec s{.who = "mcpp.rules.slint", .ns = "xim", .package = "slint",
                 .programs = {"slint-compiler"}, .bin_dirs = {"bin"},
                 .option = "options::compiler", .legacy_env = "", .request = false};
    if (auto f = tool::resolve(s); f) return {f.program, f.root, "xim:slint"};
    return out;
}

}  // namespace detail

inline bool compile(options opt = {}) {
    namespace fs = std::filesystem;
    using detail::generic;
    const char* who = "mcpp.rules.slint";
    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());

    std::vector<std::string> inputs = detail::device_slint();
    for (auto const& s : opt.sources) inputs.push_back(s);
    if (inputs.empty()) return true;

    const detail::sdk sdk = detail::locate(opt);
    if (sdk.compiler.empty()) {
        detail::warn(std::format(
            "{}: {} `.slint` file(s) and no slint-compiler: depend on `compat.slint` (or "
            "`mcpplibs.mwrap.slint`), declare `xim:slint` in `[xlings]`, set SLINT_COMPILER, or "
            "name it with options::compiler. Nothing is generated.", who, inputs.size()));
        return true;
    }
    mcpp::fact("rules-slint.sdk", std::format("{}: {}", sdk.level, sdk.compiler).c_str());

    // An SDK that is not a package of the graph: its headers, library and
    // run-time directory are the rule's to put on the build.
    if (!sdk.root.empty()) {
        const fs::path root(sdk.root);
        mcpp::include_dir(generic(root / "include" / "slint").c_str());
        mcpp::link_search(generic(root / "lib").c_str());
        mcpp::link_lib("slint_cpp");
        mcpp::runtime_search_dir(generic(root / (detail::is_windows() ? "bin" : "lib")).c_str());
    }

    const fs::path gen = fs::path(opt.out_dir) / "slint";
    std::error_code ec;
    fs::create_directories(gen, ec);
    mcpp::include_dir(generic(gen).c_str());

    std::map<std::string, fs::path> claimed;
    std::vector<fs::path> files;
    for (auto const& i : inputs) files.push_back(detail::absolute_from_root(i));
    for (auto const& f : files) {
        const std::string stem = f.stem().string();
        auto [it, fresh] = claimed.try_emplace(stem, f);
        if (!fresh) {
            std::cerr << std::format("{}: two inputs generate `{}.h`: {} and {}. Rename one.\n",
                                     who, stem, generic(it->second), generic(f));
            return false;
        }
        const std::string in  = generic(f);
        const std::string out = generic(gen / (stem + ".h"));
        const std::string dep = out + ".d";
        mcpp::action a;
        const std::string id = "slint:" + stem;
        const std::string desc = "SLINT " + f.filename().string();
        a.id = id.c_str();
        a.role = mcpp::roles::source;
        a.description = desc.c_str();
        a.depfile = dep.c_str();
        a.arg(sdk.compiler.c_str()).arg(in.c_str()).arg("-o").arg(out.c_str()).arg("--depfile").arg(dep.c_str());
        std::vector<std::string> keep;   // the strings an action argument points into
        auto add = [&](std::string flag, std::string value) {
            keep.push_back(std::move(flag)); a.arg(keep.back().c_str());
            keep.push_back(std::move(value)); a.arg(keep.back().c_str());
        };
        keep.reserve(64);
        if (!opt.ns.empty()) { keep.push_back("--cpp-namespace=" + opt.ns); a.arg(keep.back().c_str()); }
        for (auto const& l : opt.library_paths) add("-L", l.name + "=" + generic(detail::absolute_from_root(l.path)));
        for (auto const& p : opt.include_paths) add("-I", generic(detail::absolute_from_root(p)));
        if (!opt.edition.empty()) add("--edition", opt.edition);
        if (!opt.style.empty()) add("--style", opt.style);
        if (!opt.embed_resources.empty()) add("--embed-resources", opt.embed_resources);
        if (!opt.scale_factor.empty()) add("--scale-factor", opt.scale_factor);
        if (!opt.translation_domain.empty()) add("--translation-domain", opt.translation_domain);
        if (!opt.bundle_translations.empty()) add("--bundle-translations", generic(detail::absolute_from_root(opt.bundle_translations)));
        if (opt.no_default_translation_context) a.arg("--no-default-translation-context");
        a.input(in.c_str()).output(out.c_str());
        std::vector<std::string> units;
        for (unsigned u = 0; u < opt.compilation_units; ++u)
            units.push_back(generic(gen / std::format("{}_{}.cpp", stem, u)));
        for (auto const& u : units) { add("--cpp-file", u); a.output(u.c_str()); }
        a.submit();
        for (auto const& u : units) mcpp::source(u.c_str());
        mcpp::rerun_if_changed(in.c_str());

        if (opt.module.empty()) continue;
        std::set<fs::path> read;
        std::vector<std::string> names = opt.exports.empty() ? detail::generated_names(f, opt, read) : opt.exports;
        for (auto const& r : read) mcpp::rerun_if_changed(generic(r).c_str());
        const std::string module = files.size() == 1 ? opt.module : opt.module + "." + stem;
        std::string unit = std::format(
            "// Generated by mcpp.rules.slint from {} -- do not edit.\n"
            "module;\n#include <array>\n#include <limits>\n#include <slint.h>\n#include \"{}.h\"\n\n"
            "export module {};\n\n", f.filename().string(), stem, module);
        if (opt.ns.empty()) {
            for (auto const& n : names) unit += std::format("export using ::{};\n", n);
        } else {
            unit += std::format("export namespace {} {{\n", opt.ns);
            for (auto const& n : names) unit += std::format("using {}::{};\n", opt.ns, n);
            unit += "}\n";
        }
        const fs::path unitPath = gen / (stem + ".cppm");
        if (detail::read_file(unitPath) != unit) {
            std::ofstream os(unitPath, std::ios::binary);
            os << unit;
        }
        mcpp::source(generic(unitPath).c_str());
    }
    return true;
}

}  // namespace mcpp::rules::slint
