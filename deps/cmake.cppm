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
// R3.3) whose `output_dir` is that prefix and whose inputs are the
// subproject's files, so an edit to it rebuilds it and nothing else does. The prefix is then mapped exactly as `mcpp.deps.vcpkg` maps its own:
// include directory, libraries by full path, the shared libraries deployed
// beside the program.
//
// THE COMPILER IS CMAKE'S OWN CHOICE. A subproject is configured the way its
// authors build it -- on Windows, CMake's default generator and the Visual
// Studio toolset it finds -- unless the project passes `-G`, `-DCMAKE_CXX_COMPILER`
// or a toolchain file through `cache_args`. Keeping the C runtime and the C++
// standard library consistent with the program is the project's decision, as it
// is with any prebuilt library.
//
// `xim:cmake` is declared by this feature; `options::cmake` names another.

export module mcpp.deps.cmake;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.deps;

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
    std::vector<mcpp::deps::deploy_entry> deploy;
};

// The prefix, by name (SPEC-007 R1.3).
struct prefix {
    std::string root, include, lib, bin;
    // The copies `options::deploy` produced, for a project's own layout.
    std::vector<mcpp::deps::deployed_file> deployed;
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
    const std::string name = opt.name.empty() ? source.filename().string() : opt.name;
    const fs::path base   = fs::path(mcpp::out_dir()) / "deps-cmake" / name;
    const fs::path build  = base / "build";
    const fs::path root   = base / "install";

    prefix p;
    p.root    = mcpp::deps::generic(root);
    p.include = mcpp::deps::generic(root / opt.dirs.include);
    p.lib     = mcpp::deps::generic(root / opt.dirs.lib);
    p.bin     = mcpp::deps::generic(root / opt.dirs.bin);

    const std::string cmake = cmake_exe(opt);
    if (cmake.empty()) {
        mcpp::deps::warn(std::format(
            "{}: no cmake (xpkg_dir(\"xim\", \"cmake\") answered \"{}\"), so this plan builds "
            "nothing. The `deps-cmake` feature declares `xim:cmake`; `mcpp build` provisions it "
            "before this program runs.", who, std::string(mcpp::xpkg_dir("xim", "cmake"))));
    } else if (const std::string tool = mcpp::deps::launcher(who, "deps-cmake"); tool.empty()) {
        return {};
    } else {
        const std::string stamp = mcpp::deps::generic(base / (name + ".stamp"));
        const std::string id    = "deps-cmake:" + name;
        const std::string desc  = "CMAKE " + name;
        mcpp::action a;
        a.id          = id.c_str();
        a.role        = mcpp::roles::prepare;
        a.description = desc.c_str();
        a.arg(tool.c_str()).arg("cmake")
         .arg("--cmake").arg(cmake.c_str())
         .arg("--source").arg(mcpp::deps::generic(source).c_str())
         .arg("--build").arg(mcpp::deps::generic(build).c_str())
         .arg("--prefix").arg(p.root.c_str())
         .arg("--config").arg(opt.config.c_str())
         .arg("--");
        if (!opt.prefix_path.empty()) {
            std::string joined;
            for (auto const& d : opt.prefix_path) {
                if (!joined.empty()) joined += ';';
                joined += mcpp::deps::generic(mcpp::deps::absolute_from_root(d));
            }
            a.arg(("-DCMAKE_PREFIX_PATH=" + joined).c_str());
        }
        for (auto const& x : opt.cache_args) a.arg(x.c_str());
        a.input(tool.c_str());
        a.input(cmake.c_str());
        for (auto const& f : mcpp::deps::files_under(source)) a.input(f.c_str());
        mcpp::deps::watch_tree(source);
        a.output(stamp.c_str());
        a.output_dir(p.root.c_str());
        a.submit();
        p.deployed = mcpp::deps::deploy_after("deps-cmake-" + name, stamp, fs::path(p.root), opt.deploy);
    }

    mcpp::include_dir(p.include.c_str());
    mcpp::deps::link_libraries(root / opt.dirs.lib, opt.libraries, opt.shared);
    if (opt.shared) mcpp::deps::runtime_directory(p.bin, p.lib);
    return p;
}

} // namespace mcpp::deps::cmake
