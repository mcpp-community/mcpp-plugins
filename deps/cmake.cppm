// mcpp.deps.cmake -- a CMake subproject, built and installed as a build action
// and mapped into the build.
//
// A project that carries a library as a CMake subproject -- a git submodule, a
// vendored directory -- writes:
//
//   mcpp::deps::cmake::options o;
//   o.source     = "3rdParty/widgets";
//   o.cache_args = { "-DWIDGETS_BUILD_EXAMPLES=OFF" };
//   o.libraries  = { "widgets" };
//   o.shared     = true;
//   return mcpp::deps::cmake::use(o) ? 0 : 1;
//
// and the subproject is configured, built and installed into a prefix under
// this package's output directory by ONE `prepare` action (mcpp's SPEC-007
// R3.3), `cmake -P` over a script this program writes, whose `output_dir` is
// that prefix and whose inputs are the
// subproject's files, so an edit to it rebuilds it and nothing else does. The prefix is then mapped exactly as `mcpp.deps.vcpkg` maps its own:
// include directory, libraries by full path, the shared libraries deployed
// beside the program.
//
// THE COMPILER IS THE PROGRAM'S (0.17.0). What the subproject builds is linked
// into the program, so it is built with the toolset mcpp resolved
// (`mcpp.plugins.toolset`). A toolset from a Visual Studio instance keeps
// CMake's Visual Studio generator, pointed at that instance
// (`CMAKE_GENERATOR_INSTANCE`) and toolset version (`-T version=`); any other
// toolset is named by path, with the Ninja generator and mcpp's own ninja, and
// runs with the environment the engine runs it with. On the MSVC ABI the C
// runtime follows the program's (`CMAKE_MSVC_RUNTIME_LIBRARY`). A project that
// passes either compiler or a toolchain file through `cache_args` decides,
// and `options::toolset` set to `detected` keeps 0.16.0's behaviour -- CMake's
// default generator and the toolset it finds -- until 2027-03-28.
//
// `xim:cmake` is declared by this feature; `options::cmake` names another.

export module mcpp.deps.cmake;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.deps;
import mcpp.plugins.fs;
import mcpp.plugins.toolset;

export namespace mcpp::deps::cmake {

// Where the subproject installs each kind of file, relative to its prefix.
// A subproject whose `install()` rules nest them (`<prefix>/Widgets/include`)
// names the nesting here.
struct layout {
    std::string include = "include";
    std::string lib     = "lib";
    std::string bin     = "bin";
};

struct options {
    // The subproject's source directory, relative to the package root.
    std::string source;
    // Names the action and the prefix. Empty takes the source directory's name.
    std::string name;
    // `-D…`, `-G …` and any other argument for the configure step.
    std::vector<std::string> cache_args;
    // Prefixes the subproject's `find_package` searches (`CMAKE_PREFIX_PATH`),
    // e.g. `mcpp::rules::qt::root()`.
    std::vector<std::string> prefix_path;
    std::string config = "Release";
    layout dirs;
    // Library names in link order, as `mcpp.deps.vcpkg` takes them.
    std::vector<std::string> libraries;
    // Whether those libraries are shared: decides the file names off Windows
    // and whether the prefix's shared-library directory reaches `mcpp run`
    // and `mcpp pack`.
    bool shared = false;
    // The `cmake` executable. Empty is the `xim:cmake` payload.
    std::string cmake;
    // Files of the prefix placed beside the program, as `mcpp.deps.vcpkg`
    // takes them (`{"bin/tool.cfg", "."}`).
    std::vector<mcpp::plugins::fs::deploy_entry> deploy;
    // Which toolset builds the subproject (0.17.0). The default `resolved` is
    // the toolset mcpp builds the program with: a Visual Studio instance keeps
    // CMake's Visual Studio generator, pointed at that instance and toolset
    // version; any other toolset is named, with the Ninja generator and
    // mcpp's own ninja. `detected` lets CMake find its own toolset, as 0.16.0
    // did, until 2027-03-28. Compilers or a toolchain file in `cache_args`
    // decide instead. See docs/deps.md.
    mcpp::plugins::toolset::choice toolset;
    // `default` keeps the generator the mechanism implies; `ninja` uses the
    // Ninja generator with mcpp's ninja under an instance as well.
    enum class generator_kind { default_, ninja };
    generator_kind generator = generator_kind::default_;
    // The C runtime linkage on the MSVC ABI, "static" or "dynamic"
    // (`CMAKE_MSVC_RUNTIME_LIBRARY`). Empty follows the program's C++ runtime
    // contract (`mcpp::msvc_crt_linkage()`).
    std::string crt_linkage;
};

// The prefix, by name (SPEC-007 R1.3).
struct prefix {
    std::string root, include, lib, bin;
    // The copies `options::deploy` produced, for a project's own layout.
    std::vector<mcpp::plugins::fs::deployed_file> deployed;
    // How the toolset reached CMake: "instance", "chain" or "detected".
    std::string mechanism;
    explicit operator bool() const { return !root.empty(); }
};

inline std::string cmake_exe(const options& opt) {
    namespace fs = std::filesystem;
    if (!opt.cmake.empty()) return mcpp::deps::generic(mcpp::deps::absolute_from_root(opt.cmake));
    const std::string dir = mcpp::xpkg_dir("xim", "cmake");
    if (dir.empty()) return {};
    const bool win = std::string(mcpp::host()).find("windows") != std::string::npos;
    std::error_code ec;
    for (auto const& sub : { fs::path("bin"), fs::path("CMake.app") / "Contents" / "bin" }) {
        const auto exe = fs::path(dir) / sub / (win ? "cmake.exe" : "cmake");
        if (fs::is_regular_file(exe, ec)) return mcpp::deps::generic(exe);
    }
    return {};
}

inline prefix use(const options& opt) {
    namespace fs = std::filesystem;
    namespace ts = mcpp::plugins::toolset;
    constexpr std::string_view who = "mcpp.deps.cmake";
    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());

    std::error_code ec;
    const fs::path source = mcpp::deps::absolute_from_root(opt.source);
    if (opt.source.empty() || !fs::is_regular_file(source / "CMakeLists.txt", ec)) {
        std::cerr << std::format(
            "{}: options::source must name a directory holding CMakeLists.txt; got '{}'.\n"
            "  A git submodule that was not checked out is an empty directory: "
            "`git submodule update --init`.\n", who, opt.source);
        return {};
    }

    // THE TOOLSET. Compilers or a toolchain file among the cache arguments
    // are the project's decision, and so is a generator it names.
    auto names = [&](std::initializer_list<std::string_view> keys) {
        return std::ranges::any_of(opt.cache_args, [&](const std::string& x) {
            return std::ranges::any_of(keys, [&](std::string_view k) { return x.starts_with(k); });
        });
    };
    const bool chosen = std::ranges::any_of(opt.cache_args, [](const std::string& x) {
        return x.contains("CMAKE_C_COMPILER") || x.contains("CMAKE_CXX_COMPILER")
            || x.contains("CMAKE_TOOLCHAIN_FILE");
    });
    const bool generatorChosen = names({"-G", "-DCMAKE_GENERATOR"});
    const bool wantNinja = opt.generator == options::generator_kind::ninja;
    auto tools = wantNinja ? ts::resolve_named(opt.toolset) : ts::resolve(opt.toolset);
    if (!tools) {
        std::cerr << std::format("{}: {}\n", who, tools.error());
        return {};
    }
    const std::string crt = opt.crt_linkage.empty() ? tools->crt : opt.crt_linkage;
    if (!crt.empty() && crt != "static" && crt != "dynamic") {
        std::cerr << std::format("{}: options::crt_linkage is '{}'; it is \"static\" or \"dynamic\".\n",
                                 who, crt);
        return {};
    }
    const std::string ninja = mcpp::ninja_program();
    const bool named    = !chosen && tools->how == ts::mechanism::chain && !tools->cxx.empty();
    const bool useNinja = named && !generatorChosen && !ninja.empty();
    const bool instance = !chosen && !generatorChosen && tools->how == ts::mechanism::instance;

    // One build directory per toolset statement: CMake refuses a cache made
    // with another generator or instance. What 0.16.0 configured stays in
    // `build/`.
    std::string key;
    if (useNinja) key = "ninja;" + tools->identity + ";" + tools->cxx;
    else if (named) key = "named;" + tools->identity + ";" + tools->cxx;
    else if (instance) key = "instance;" + tools->instance_dir + ";" + tools->toolset_version;
    const std::string name = opt.name.empty() ? source.filename().string() : opt.name;
    const fs::path base   = fs::path(mcpp::out_dir()) / "deps-cmake" / name;
    const fs::path build  = base / (key.empty() ? std::string("build") : "build-" + mcpp::deps::short_name(key));
    const fs::path root   = base / "install";

    prefix p;
    p.root      = mcpp::deps::generic(root);
    p.include   = mcpp::deps::generic(root / opt.dirs.include);
    p.lib       = mcpp::deps::generic(root / opt.dirs.lib);
    p.bin       = mcpp::deps::generic(root / opt.dirs.bin);
    p.mechanism = std::string(ts::name(named ? ts::mechanism::chain
                                       : instance ? ts::mechanism::instance : ts::mechanism::detected));

    const std::string cmake = cmake_exe(opt);
    if (cmake.empty()) {
        mcpp::deps::warn(std::format(
            "{}: no cmake (xpkg_dir(\"xim\", \"cmake\") answered \"{}\"), so this plan builds "
            "nothing. The `deps-cmake` feature declares `xim:cmake`; `mcpp build` provisions it "
            "before this program runs.", who, std::string(mcpp::xpkg_dir("xim", "cmake"))));
    } else {
        // ONE ACTION, THREE STEPS. `cmake -P` runs a script this program
        // writes: configure (every time -- over an existing cache CMake re-runs
        // only what changed, and the arguments may have changed, which is why
        // the action ran at all), then build and install.
        const std::string stamp  = mcpp::deps::generic(base / (name + ".stamp"));
        const std::string id     = "deps-cmake:" + name;
        const std::string desc   = "CMAKE " + name;
        const fs::path    script = base / (name + ".cmake");
        using mcpp::deps::bracket;
        std::vector<std::string> configure{
            "-S", mcpp::deps::generic(source), "-B", mcpp::deps::generic(build),
            "-DCMAKE_INSTALL_PREFIX=" + p.root, "-DCMAKE_BUILD_TYPE=" + opt.config };
        if (!opt.prefix_path.empty()) {
            std::string joined;
            for (auto const& d : opt.prefix_path) {
                if (!joined.empty()) joined += ';';
                joined += mcpp::deps::generic(mcpp::deps::absolute_from_root(d));
            }
            configure.push_back("-DCMAKE_PREFIX_PATH=" + joined);
        }
        if (useNinja) {
            configure.push_back("-G");
            configure.push_back("Ninja");
            configure.push_back("-DCMAKE_MAKE_PROGRAM=" + ts::forward(ninja));
        }
        if (named) {
            configure.push_back("-DCMAKE_C_COMPILER=" + tools->cc);
            configure.push_back("-DCMAKE_CXX_COMPILER=" + tools->cxx);
            if (tools->msvc_abi) {
                if (!tools->rc.empty()) configure.push_back("-DCMAKE_RC_COMPILER=" + tools->rc);
                if (!tools->mt.empty()) configure.push_back("-DCMAKE_MT=" + tools->mt);
                if (!tools->ld.empty()) configure.push_back("-DCMAKE_LINKER=" + tools->ld);
            }
        }
        if (instance) {
            // CMake's documented selection of a Visual Studio instance and of
            // a toolset version within it.
            configure.push_back("-DCMAKE_GENERATOR_INSTANCE=" + tools->instance_dir);
            if (!tools->toolset_version.empty() && tools->toolset_version != tools->instance_default) {
                configure.push_back("-T");
                configure.push_back("version=" + tools->toolset_version);
            }
        }
        // The C runtime follows the program's (CMake 3.15+, policy CMP0091).
        if (tools->msvc_abi && !crt.empty() && !names({"-DCMAKE_MSVC_RUNTIME_LIBRARY"})) {
            configure.push_back("-DCMAKE_POLICY_DEFAULT_CMP0091=NEW");
            configure.push_back(std::string("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>")
                                + (crt == "dynamic" ? "DLL" : ""));
        }
        for (auto const& x : opt.cache_args) configure.push_back(x);
        std::string text = "# Written by mcpp.deps.cmake: configure, build and install " + name + ".\n"
                           "# toolset: " + ts::describe(*tools) + "\n"
                           "execute_process(COMMAND ${CMAKE_COMMAND}";
        for (auto const& x : configure) text += "\n    " + bracket(x);
        text += "\n    RESULT_VARIABLE rc)\n"
                "if(NOT rc EQUAL 0)\n  message(FATAL_ERROR \"configure exited ${rc}\")\nendif()\n"
                "execute_process(COMMAND ${CMAKE_COMMAND} --build " + bracket(mcpp::deps::generic(build)) +
                " --config " + bracket(opt.config) + " --target install --parallel\n    RESULT_VARIABLE rc)\n"
                "if(NOT rc EQUAL 0)\n  message(FATAL_ERROR \"build and install exited ${rc}\")\nendif()\n";
        mcpp::plugins::fs::write_if_changed(script, text);
        const std::string scriptS = mcpp::deps::generic(script);
        mcpp::action a;
        a.id          = id.c_str();
        a.role        = mcpp::roles::prepare;
        a.description = desc.c_str();
        a.arg(cmake.c_str()).arg("-P").arg(scriptS.c_str());
        // Named tools run with the environment the engine runs them with, and
        // their directories first on PATH.
        if (named) {
            for (auto const& [k, v] : tools->env) a.env(k.c_str(), v.c_str());
            if (!tools->path_dirs.empty()) {
                const char sep = ts::path_separator();
                std::string path;
                for (auto const& d : tools->path_dirs) {
                    if (!path.empty()) path += sep;
                    std::string n = d;
                    if (ts::host_is_windows())
                        for (std::size_t i = 0; i < n.size(); ++i) if (n[i] == '/') n[i] = '\\';
                    path += n;
                }
                const char* cur = std::getenv("PATH");
                if (cur && *cur) { path += sep; path += cur; }
                a.env("PATH", path.c_str());
            }
        }
        a.input(cmake.c_str());
        a.input(scriptS.c_str());
        for (auto const& f : mcpp::deps::files_under(source)) a.input(f.c_str());
        mcpp::deps::watch_tree(source);
        a.output(stamp.c_str());
        a.output_dir(p.root.c_str());
        a.submit();
        p.deployed = mcpp::plugins::fs::deploy_after("deps-cmake-" + name, stamp, fs::path(p.root), opt.deploy);
    }

    mcpp::include_dir(p.include.c_str());
    mcpp::deps::link_libraries(root / opt.dirs.lib, opt.libraries, opt.shared);
    if (opt.shared) mcpp::deps::runtime_directory(p.bin, p.lib);
    return p;
}

} // namespace mcpp::deps::cmake
