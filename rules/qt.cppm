// mcpp.rules.qt -- how the files a Qt program is made of become part of it.
//
// A Qt 6 program has four kinds of input no C++ compiler reads:
//
//   a header declaring `Q_OBJECT`, `Q_GADGET` or `Q_NAMESPACE`
//                     -> `moc`      -> `moc_<name>.cpp`, compiled
//   `<form>.ui`       -> `uic`      -> `ui_<form>.h`, included
//   `<res>.qrc`       -> `rcc`      -> `qrc_<res>.cpp`, compiled
//   `<lang>.ts`       -> `lrelease` -> `<lang>.qm`, deployed beside the program
//
// and each is one action with declared inputs and outputs, so an edited header
// re-runs one `moc` and an edited translation one `lrelease`. The rule also
// puts the modules on the build -- include directories, the libraries by full
// path, `QT_<MODULE>_LIB` -- and places what Qt loads at run time and no
// import table names: its plugins (`platforms/qwindows.dll`, `styles/`,
// `imageformats/`), which is the job `windeployqt` exists for.
//
// WHERE QT COMES FROM IS NOT THIS RULE'S QUESTION. `options::root` names an SDK;
// empty takes the `xim:qt` payload, which `rules-qt-xim` declares, and
// `xim:qt-addons` beside it when `rules-qt-xim-addons` declares that. A project
// using a Qt from elsewhere names `rules-qt` alone and downloads nothing.
//
// WHAT THE PROGRAM LOADS, THE ENGINE FINDS. The SDK's shared-library directory
// is a runtime search directory (mcpp's SPEC-007 R4.1): the program's run path
// on ELF and Mach-O, `mcpp run`'s load path, `mcpp pack`'s closure, and on
// Windows the DLLs the program imports placed beside it (R4.3) -- the part of
// `windeployqt`'s job an import table can answer. The rest of it the rule does:
// the plugin directories `deploy_plugins` names are deployed beside the
// program (R4.2), because Qt finds them relative to it and no import table
// names them.
//
// A MISSING SDK IS A WARNING. `mcpp emit build-database` plans a project on
// machines that never built it; the rule says what it could not find and
// declares nothing, so the plan succeeds and the build is where it fails.
//
// `lupdate` REWRITES SOURCES, so it is off unless `translations::update_sources`
// asks for it; then it is an action whose output is the `.ts` file itself, and
// `lrelease` reads that file, the order Qt's Visual Studio integration runs
// them in.

module;
#include <cctype>

export module mcpp.rules.qt;

import std;
import mcpp;
import mcpp.plugins;

export namespace mcpp::rules::qt {

enum class moc_scan {
    project_headers,   // every header under the package root that declares a meta-object
    listed,            // only `options::moc_headers`
};

struct translations {
    // `.ts` files, beside those the project names in `[build] sources`.
    std::vector<std::string> ts;
    // Run `lupdate` over the sources before `lrelease`. It writes into the
    // `.ts` files, which are part of the source tree.
    bool update_sources = false;
    // `lupdate -tr-function-alias` values, e.g. `translate+=appTr`.
    std::vector<std::string> tr_function_alias;
    // The files `lupdate` reads. Empty takes the package's C++ files.
    std::vector<std::string> sources;
    // Where the `.qm` files are placed, relative to the program.
    std::string deploy_to = "translations";
    // Where `lrelease` writes them. Empty is `<out dir>/qt/translations`; a
    // project whose own release step copies them names a directory it knows.
    std::string out_dir;
};

struct options {
    // Qt modules, `Core` / `QtCore` / `Qt6Core` alike, in link order.
    std::vector<std::string> modules = { "Core" };
    // Modules whose private headers the package includes (`QtWidgets/private/…`).
    std::vector<std::string> private_modules;
    moc_scan moc = moc_scan::project_headers;
    std::vector<std::string> moc_headers;
    // `.ui` and `.qrc` files, beside those named in `[build] sources`.
    std::vector<std::string> forms;
    std::vector<std::string> resources;
    translations i18n;
    // Plugin directories placed beside the program (`platforms`, `styles`,
    // `imageformats`, `tls`, …). `platforms` is what any GUI program needs.
    std::vector<std::string> deploy_plugins = { "platforms" };
    // Windows: `opengl32sw.dll` (Mesa's software OpenGL) and `d3dcompiler_47.dll`
    // beside the program, as `windeployqt` places them by default.
    bool deploy_software_gl = false;
    // The SDK. Empty takes `xim:qt`, then `xim:qt-addons` as a second prefix.
    std::string root;
    std::vector<std::string> extra_roots;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The SDK ───────────────────────────────────────────────────────────────

namespace detail {

inline std::string generic(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

inline bool is_windows() { return std::string_view(mcpp::target_os()) == "windows"; }
inline bool is_macos()   { return std::string_view(mcpp::target_os()) == "macos"; }
inline bool host_windows() { return std::string(mcpp::host()).find("windows") != std::string::npos; }

inline std::filesystem::path absolute_from_root(const std::string& p) {
    std::filesystem::path path(p);
    if (path.is_relative()) path = std::filesystem::path(mcpp::manifest_dir()) / path;
    return path.lexically_normal();
}

inline void warn(const std::string& message) {
    std::cerr << message << '\n';
    std::string folded;
    bool space = false;
    for (char c : message) {
        if (c == '\n') { space = true; continue; }
        if (space) { if (c == ' ') continue; folded += ' '; space = false; }
        folded += c;
    }
    mcpp::warning(folded.c_str());
}

// `Core`, `QtCore` and `Qt6Core` name one module.
inline std::string module_name(std::string m) {
    if (m.starts_with("Qt6")) m = m.substr(3);
    else if (m.starts_with("Qt")) m = m.substr(2);
    return m;
}

// By index, as `mcpp.deps.vcpkg` lowers a triplet line: GCC 16 cannot inline a
// non-const std::string iterator in a module unit.
inline std::string upper(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = char(std::toupper(static_cast<unsigned char>(s[i])));
    return s;
}

// A Qt tool: `bin/` on Windows, `libexec/` for the code generators elsewhere
// (Qt 6 moved `moc`, `uic` and `rcc` there), `bin/` for the Linguist tools.
inline std::string tool(std::span<const std::filesystem::path> roots, const char* name) {
    std::error_code ec;
    const std::string file = std::string(name) + (host_windows() ? ".exe" : "");
    for (auto const& r : roots)
        for (auto const* sub : { "bin", "libexec" })
            if (std::filesystem::is_regular_file(r / sub / file, ec)) return generic(r / sub / file);
    return {};
}

// Files under the package root with one of `exts`, skipping build output and
// version control. Sorted, so the plan does not depend on directory order.
inline std::vector<std::filesystem::path> project_files(std::initializer_list<std::string_view> exts) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path root = mcpp::manifest_dir();
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto name = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (name == "target" || name == ".git" || name == "mcpp-generated" ||
                name == "vcpkg_installed" || name == "node_modules")
                it.disable_recursion_pending();
            continue;
        }
        const auto ext = it->path().extension().string();
        for (auto e : exts) if (ext == e) { out.push_back(it->path()); break; }
    }
    std::ranges::sort(out);
    return out;
}

inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

inline bool declares_meta_object(const std::string& text) {
    for (auto m : { "Q_OBJECT", "Q_GADGET", "Q_NAMESPACE" }) {
        for (std::size_t at = text.find(m); at != std::string::npos; at = text.find(m, at + 1)) {
            const bool left  = at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_');
            const std::size_t end = at + std::strlen(m);
            // `Q_OBJECT`, `Q_GADGET_EXPORT(…)`, `Q_NAMESPACE_EXPORT(…)`.
            const bool right = end >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])))
                               || text.compare(end, 7, "_EXPORT") == 0;
            if (left && right) return true;
        }
    }
    return false;
}

// The device sources the project names, of one extension.
inline std::vector<std::string> device(std::string_view ext) {
    std::vector<std::string> out;
    const std::string all = mcpp::device_sources();
    std::size_t i = 0;
    while (i <= all.size()) {
        auto nl = all.find('\n', i);
        std::string one = all.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? all.size() + 1 : nl + 1;
        while (!one.empty() && (one.back() == ' ' || one.back() == '\r')) one.pop_back();
        if (one.size() > ext.size() && one.ends_with(ext)) out.push_back(one);
    }
    return out;
}

// The `<file>` entries of a `.qrc`, resolved against its directory: what `rcc`
// reads, so each is an input of the action that runs it.
inline std::vector<std::string> qrc_files(const std::filesystem::path& qrc) {
    std::vector<std::string> out;
    mcpp::plugins::xml::node doc;
    std::string error;
    if (!mcpp::plugins::xml::parse(read_file(qrc), doc, error)) return out;
    std::function<void(const mcpp::plugins::xml::node&)> walk = [&](const mcpp::plugins::xml::node& n) {
        if (n.name == "file") {
            std::string text = mcpp::plugins::xml::trim_copy(n.text);
            if (!text.empty()) out.push_back(generic(qrc.parent_path() / text));
        }
        for (auto const& c : n.children) walk(c);
    };
    walk(doc);
    return out;
}

// The directory `include/Qt<Module>/<version>` private headers live in.
inline std::filesystem::path private_dir(const std::filesystem::path& root, const std::string& module) {
    std::error_code ec;
    const auto base = is_macos() ? root / "lib" / ("Qt" + module + ".framework") / "Headers"
                                 : root / "include" / ("Qt" + module);
    for (auto const& e : std::filesystem::directory_iterator(base, ec))
        if (e.is_directory(ec) && !e.path().filename().string().empty() &&
            std::isdigit(static_cast<unsigned char>(e.path().filename().string()[0])))
            return e.path();
    return {};
}

} // namespace detail

// The SDKs this build uses: `options::root` and `extra_roots`, or the xim
// payloads. Empty when none is present.
inline std::vector<std::filesystem::path> roots(const options& opt = {}) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    auto add = [&](const std::string& r) {
        if (r.empty()) return;
        const auto p = detail::absolute_from_root(r);
        if (std::filesystem::is_directory(p, ec)) out.push_back(p);
    };
    if (!opt.root.empty()) add(opt.root);
    else {
        // `xim:qt` is the full base; `xim:qt-base` is its qtbase + qttools
        // subset (`rules-qt-xim-base`). A project that declares both uses the
        // full one.
        const std::string full = mcpp::xpkg_dir("xim", "qt");
        add(full.empty() ? std::string(mcpp::xpkg_dir("xim", "qt-base")) : full);
        add(mcpp::xpkg_dir("xim", "qt-addons"));
    }
    for (auto const& r : opt.extra_roots) add(r);
    return out;
}

// The first SDK, for a project that hands it to something else (a CMake
// subproject's `CMAKE_PREFIX_PATH`). Empty when none is present.
inline std::string root(const options& opt = {}) {
    auto r = roots(opt);
    return r.empty() ? std::string() : detail::generic(r.front());
}

// ─── What the program loads ────────────────────────────────────────────────

// The SDK's shared-library directories, and on Linux those of the libraries
// Qt's official QtCore expects the distribution to provide (glib, zstd, zlib,
// declared from xim by `rules-qt-xim` and `rules-qt-xim-base`), as runtime search directories
// (SPEC-007 R4.1). The engine renders them as the program's run path, puts
// them on `mcpp run`'s load path -- which reaches a library's dependencies
// where the program's run path does not, because QtCore carries a RUNPATH of
// its own -- searches them for `mcpp pack`'s closure, and on Windows places
// the Qt DLLs the program imports beside it (R4.3).
inline void runtime_directories(std::span<const std::filesystem::path> roots) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto const& r : roots) {
        const std::string dir = detail::generic(r / (detail::is_windows() ? "bin" : "lib"));
        mcpp::runtime_search_dir(dir.c_str());
    }
    if (detail::is_windows() || detail::is_macos()) return;
    for (auto const* pkg : { "glib", "zstd", "zlib" }) {
        const std::string dir = mcpp::xpkg_dir("xim", pkg);
        if (dir.empty()) {
            detail::warn(std::format("mcpp.rules.qt: xim:{} is not installed, and QtCore on Linux "
                                     "loads it; `rules-qt-xim` declares it.", pkg));
            continue;
        }
        if (fs::is_directory(fs::path(dir) / "lib", ec))
            mcpp::runtime_search_dir(detail::generic(fs::path(dir) / "lib").c_str());
    }
}

// ─── The rule ──────────────────────────────────────────────────────────────

inline bool compile(options opt = {}) {
    namespace fs = std::filesystem;
    using detail::generic;
    constexpr std::string_view who = "mcpp.rules.qt";
    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());

    const auto sdks = roots(opt);
    if (sdks.empty()) {
        detail::warn(std::format(
            "{}: no Qt SDK: options::root is {} and xpkg_dir(\"xim\", \"qt\") answered \"{}\". "
            "Nothing Qt-specific is planned. The `rules-qt-xim` feature declares `xim:qt` and "
            "`rules-qt-xim-base` declares `xim:qt-base`, which `mcpp build` provisions; a Qt "
            "from elsewhere is named with options::root.",
            who, opt.root.empty() ? "empty" : "'" + opt.root + "' (no such directory)",
            std::string(mcpp::xpkg_dir("xim", "qt"))));
        return true;
    }
    const fs::path main = sdks.front();
    const fs::path gen  = fs::path(opt.out_dir) / "qt";
    std::error_code ec;
    fs::create_directories(gen, ec);

    // ── the modules ──
    std::vector<std::string> modules;
    for (auto const& m : opt.modules) {
        const auto name = detail::module_name(m);
        if (std::ranges::find(modules, name) == modules.end()) modules.push_back(name);
    }
    std::vector<std::string> missing;
    for (auto const& r : sdks) {
        if (detail::is_macos()) {
            const std::string f = "-F" + generic(r / "lib");
            mcpp::cxxflag(f.c_str());
        } else {
            mcpp::include_dir(generic(r / "include").c_str());
        }
    }
    for (auto const& m : modules) {
        bool found = false;
        for (auto const& r : sdks) {
            fs::path lib, headers;
            if (detail::is_windows()) {
                lib = r / "lib" / ("Qt6" + m + ".lib");
                headers = r / "include" / ("Qt" + m);
            } else if (detail::is_macos()) {
                lib = r / "lib" / ("Qt" + m + ".framework") / ("Qt" + m);
                headers = r / "lib" / ("Qt" + m + ".framework") / "Headers";
            } else {
                lib = r / "lib" / ("libQt6" + m + ".so");
                headers = r / "include" / ("Qt" + m);
            }
            if (!fs::exists(lib, ec)) continue;
            mcpp::include_dir(generic(headers).c_str());
            mcpp::link_flag(generic(lib).c_str());
            const std::string def = "QT_" + detail::upper(m) + "_LIB";
            mcpp::define(def.c_str());
            found = true;
            break;
        }
        if (!found) missing.push_back(m);
    }
    for (auto const& m : opt.private_modules) {
        const auto name = detail::module_name(m);
        for (auto const& r : sdks) {
            const auto dir = detail::private_dir(r, name);
            if (dir.empty()) continue;
            mcpp::include_dir(generic(dir).c_str());
            mcpp::include_dir(generic(dir / ("Qt" + name)).c_str());
            break;
        }
    }
    if (!missing.empty()) {
        // An incomplete SDK is a state of the machine: said, and the rest of
        // the configuration stated, so the link is where it fails (R1.2).
        std::string list;
        for (auto const& m : missing) list += " " + m;
        detail::warn(std::format(
            "{}: module(s){} not found in {}. The base package carries Core, Gui, Widgets, "
            "Network, Svg, Qml, Quick and the other qtbase/qtdeclarative modules; the "
            "additional libraries (Multimedia, Charts, WebSockets, ...) are `xim:qt-addons`, "
            "which the `rules-qt-xim-addons` feature declares.",
            who, list, generic(main)));
    }

    // ── the compiler and the loader ──
    if (std::string_view(mcpp::compiler()) == "msvc") {
        // Qt 6 refuses a `__cplusplus` that does not state the standard, which
        // is cl.exe's default.
        mcpp::cxxflag("/Zc:__cplusplus");
        mcpp::cxxflag("/permissive-");
    }
    if (!detail::is_windows() && !detail::is_macos()) {
        // Qt's Linux libraries are built with `-reduce-relocations`, and its
        // headers refuse position-dependent code: `-fPIC`, as Qt's own CMake
        // package requires of every consumer.
        mcpp::cxxflag("-fPIC");
    }
    runtime_directories(sdks);

    // ── moc ──
    const std::string moc = detail::tool(sdks, "moc");
    std::vector<fs::path> headers;
    if (opt.moc == moc_scan::listed) {
        for (auto const& h : opt.moc_headers) headers.push_back(detail::absolute_from_root(h));
    } else {
        for (auto const* g : { "**/*.h", "**/*.hpp", "**/*.hxx" }) mcpp::rerun_if_changed_glob(g);
        for (auto const& h : detail::project_files({ ".h", ".hpp", ".hxx" })) {
            mcpp::rerun_if_changed(generic(h).c_str());
            if (detail::declares_meta_object(detail::read_file(h))) headers.push_back(h);
        }
    }
    // A source that includes its own `<stem>.moc` declares a meta-object in the
    // source itself; its moc output is a header that source includes.
    std::vector<fs::path> inlineMoc;
    if (opt.moc == moc_scan::project_headers) {
        for (auto const* g : { "**/*.cpp", "**/*.cc", "**/*.cxx" }) mcpp::rerun_if_changed_glob(g);
        for (auto const& c : detail::project_files({ ".cpp", ".cc", ".cxx" })) {
            const auto text = detail::read_file(c);
            if (text.find("\"" + c.stem().string() + ".moc\"") != std::string::npos &&
                detail::declares_meta_object(text))
                inlineMoc.push_back(c);
        }
    }
    if ((!headers.empty() || !inlineMoc.empty()) && moc.empty()) {
        detail::warn(std::format("{}: `moc` not found under {} (bin/ or libexec/); no meta-object "
                                 "code is generated.", who, generic(main)));
        headers.clear();
        inlineMoc.clear();
    }
    // EVERY GENERATED NAME HAS ONE SOURCE. Each generator names its output
    // after the input's stem, in one directory, so two inputs with one stem in
    // different directories would be two actions writing one file; refused
    // naming both, for moc, uic, rcc and lrelease alike.
    std::map<std::string, fs::path> claimed;
    auto claim = [&](const std::string& outName, const fs::path& in) -> bool {
        auto [it, fresh] = claimed.try_emplace(outName, in);
        if (fresh) return true;
        std::cerr << std::format("{}: two files produce `{}`: {} and {}. Generated files are "
                                 "named after the input's stem; rename one.\n",
                                 who, outName, generic(it->second), generic(in));
        return false;
    };
    auto mocOne = [&](const fs::path& in, const std::string& outName) -> bool {
        if (!claim(outName, in)) return false;
        const std::string out = generic(gen / outName);
        const std::string dep = out + ".d";
        const std::string src = generic(in);
        const std::string id  = "qt:moc:" + outName;
        const std::string desc = "MOC " + in.filename().string();
        mcpp::action a;
        a.id = id.c_str();
        a.role = mcpp::roles::source;
        a.description = desc.c_str();
        a.depfile = dep.c_str();
        a.arg(moc.c_str()).arg(src.c_str()).arg("-o").arg(out.c_str())
         .arg("--output-dep-file").arg("--dep-file-path").arg(dep.c_str())
         .input(src.c_str()).output(out.c_str()).submit();
        return true;
    };
    for (auto const& h : headers)
        if (!mocOne(h, "moc_" + h.stem().string() + ".cpp")) return false;
    for (auto const& c : inlineMoc)
        if (!mocOne(c, c.stem().string() + ".moc")) return false;

    // ── uic ──
    std::vector<std::string> forms = detail::device(".ui");
    for (auto const& f : opt.forms) forms.push_back(f);
    if (!forms.empty()) {
        const std::string uic = detail::tool(sdks, "uic");
        if (uic.empty()) {
            detail::warn(std::format("{}: `uic` not found under {}; no form is generated.", who, generic(main)));
            forms.clear();
        }
        for (auto const& f : forms) {
            if (!claim("ui_" + fs::path(f).stem().string() + ".h", detail::absolute_from_root(f))) return false;
            const std::string in  = generic(detail::absolute_from_root(f));
            const std::string out = generic(gen / ("ui_" + fs::path(f).stem().string() + ".h"));
            const std::string id  = "qt:uic:" + fs::path(f).stem().string();
            const std::string desc = "UIC " + fs::path(f).filename().string();
            mcpp::action a;
            a.id = id.c_str();
            a.role = mcpp::roles::source;
            a.description = desc.c_str();
            a.arg(uic.c_str()).arg(in.c_str()).arg("-o").arg(out.c_str())
             .input(in.c_str()).output(out.c_str()).submit();
        }
    }
    if (!forms.empty() || !inlineMoc.empty()) mcpp::include_dir(generic(gen).c_str());

    // ── rcc ──
    std::vector<std::string> resources = detail::device(".qrc");
    for (auto const& r : opt.resources) resources.push_back(r);
    if (!resources.empty()) {
        const std::string rcc = detail::tool(sdks, "rcc");
        if (rcc.empty()) {
            detail::warn(std::format("{}: `rcc` not found under {}; no resource is compiled.", who, generic(main)));
            resources.clear();
        }
        for (auto const& r : resources) {
            const fs::path qrc = detail::absolute_from_root(r);
            const std::string stem = qrc.stem().string();
            if (!claim("qrc_" + stem + ".cpp", qrc)) return false;
            const std::string in  = generic(qrc);
            const std::string out = generic(gen / ("qrc_" + stem + ".cpp"));
            const std::string id  = "qt:rcc:" + stem;
            const std::string desc = "RCC " + qrc.filename().string();
            mcpp::rerun_if_changed(in.c_str());
            mcpp::action a;
            a.id = id.c_str();
            a.role = mcpp::roles::source;
            a.description = desc.c_str();
            a.arg(rcc.c_str()).arg("--name").arg(stem.c_str()).arg(in.c_str()).arg("-o").arg(out.c_str())
             .input(in.c_str());
            for (auto const& f : detail::qrc_files(qrc)) a.input(f.c_str());
            a.output(out.c_str()).submit();
        }
    }

    // ── translations ──
    std::vector<std::string> ts = detail::device(".ts");
    for (auto const& t : opt.i18n.ts) ts.push_back(t);
    if (!ts.empty()) {
        const std::string lrelease = detail::tool(sdks, "lrelease");
        const std::string lupdate  = detail::tool(sdks, "lupdate");
        if (lrelease.empty() || (opt.i18n.update_sources && lupdate.empty())) {
            detail::warn(std::format("{}: `{}` not found under {}; it is part of qttools, and no "
                                     "translation is released.",
                                     who, lrelease.empty() ? "lrelease" : "lupdate", generic(main)));
            ts.clear();
        }
        std::vector<std::string> sources;
        if (opt.i18n.update_sources) {
            if (!opt.i18n.sources.empty()) {
                for (auto const& s : opt.i18n.sources) sources.push_back(generic(detail::absolute_from_root(s)));
            } else {
                for (auto const* g : { "**/*.cpp", "**/*.h", "**/*.hpp", "**/*.ixx", "**/*.cppm" })
                    mcpp::rerun_if_changed_glob(g);
                for (auto const& f : detail::project_files({ ".cpp", ".h", ".hpp", ".ixx", ".cppm" }))
                    sources.push_back(generic(f));
            }
        }
        for (auto const& t : ts) {
            const fs::path file = detail::absolute_from_root(t);
            const std::string stem = file.stem().string();
            if (!claim(stem + ".qm", file)) return false;
            const std::string in = generic(file);
            const fs::path qmDir = opt.i18n.out_dir.empty() ? gen / "translations"
                                                            : detail::absolute_from_root(opt.i18n.out_dir);
            const std::string qm = generic(qmDir / (stem + ".qm"));
            if (opt.i18n.update_sources) {
                const std::string id = "qt:lupdate:" + stem;
                const std::string desc = "LUPDATE " + file.filename().string();
                mcpp::action u;
                u.id = id.c_str();
                // The file lupdate writes is named before it runs, so the action
                // names it as its output (SPEC-007 R3.2) and needs neither a
                // stamp nor a `prepare` directory. `lrelease` takes the same file
                // as its input, which orders the two.
                u.role = mcpp::roles::source;
                u.description = desc.c_str();
                u.arg(lupdate.c_str()).arg("-silent").arg("-extensions").arg("cpp,h,hpp,ixx,cppm");
                for (auto const& a : opt.i18n.tr_function_alias) u.arg("-tr-function-alias").arg(a.c_str());
                for (auto const& s : sources) u.arg(s.c_str()).input(s.c_str());
                u.arg("-ts").arg(in.c_str()).output(in.c_str()).submit();
            }
            const std::string id = "qt:lrelease:" + stem;
            const std::string desc = "LRELEASE " + file.filename().string();
            mcpp::action r;
            r.id = id.c_str();
            r.role = mcpp::roles::source;
            r.description = desc.c_str();
            r.arg(lrelease.c_str()).arg("-silent").arg(in.c_str()).arg("-qm").arg(qm.c_str()).input(in.c_str());
            r.output(qm.c_str()).submit();
            mcpp::deploy(qm.c_str(), opt.i18n.deploy_to.c_str());
        }
    }

    // ── what Qt loads at run time ──
    for (auto const& dir : opt.deploy_plugins) {
        bool any = false;
        for (auto const& r : sdks) {
            const fs::path pdir = r / "plugins" / dir;
            if (!fs::is_directory(pdir, ec)) continue;
            std::vector<fs::path> files;
            for (auto const& e : fs::directory_iterator(pdir, ec))
                if (e.is_regular_file(ec)) files.push_back(e.path());
            std::ranges::sort(files);
            for (auto const& f : files) {
                const auto ext = f.extension().string();
                const bool lib = detail::is_windows() ? ext == ".dll"
                               : detail::is_macos()   ? ext == ".dylib" : ext == ".so";
                if (!lib) continue;
                // The Windows SDK ships each plugin twice, `qwindows.dll` and
                // the debug `qwindowsd.dll`; the program links the release
                // libraries, so it loads the release plugin.
                if (detail::is_windows()) {
                    const auto stem = f.stem().string();
                    if (stem.ends_with("d") &&
                        fs::exists(f.parent_path() / (stem.substr(0, stem.size() - 1) + ".dll"), ec))
                        continue;
                }
                mcpp::deploy(generic(f).c_str(), dir.c_str());
                any = true;
            }
        }
        if (!any)
            detail::warn(std::format("{}: no plugin directory `{}` under {}/plugins; nothing placed "
                                     "for it.", who, dir, generic(main)));
    }
    if (opt.deploy_software_gl && detail::is_windows()) {
        for (auto const* f : { "opengl32sw.dll", "d3dcompiler_47.dll" })
            for (auto const& r : sdks)
                if (fs::is_regular_file(r / "bin" / f, ec)) {
                    mcpp::deploy(generic(r / "bin" / f).c_str(), ".");
                    break;
                }
    }
    return true;
}

} // namespace mcpp::rules::qt
