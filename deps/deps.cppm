// mcpp.deps -- what the `deps-*` members share.
//
// A `deps-*` member answers "where does a library come from": it installs a
// prefix through a program mcpp does not drive (vcpkg, CMake) and maps the
// prefix into the build -- include directories, the link, and the directories
// the program's shared libraries are found in at run time. The two members
// differ in which program installs the prefix; everything else is here.
//
// TWO RULES EVERY MEMBER FOLLOWS, AND WHY THEY ARE THE SAME RULE.
//
//   1. The installation is an ACTION, never work the build program does.
//      `mcpp emit build-database` runs build programs to plan (mcpp's
//      docs/specs/build-database.md), a build program has a 600-second limit,
//      and an installation can take an hour. An action is a ninja edge: it runs
//      under `mcpp build` only, as long as it needs, and only when its inputs
//      changed.
//
//   2. A missing prefix is a WARNING, not a failure. The first plan of a
//      project runs before the action has installed anything, and an editor
//      asks for the plan on machines that never built. A build program that
//      exits 1 there takes the whole build database with it; one that states
//      the paths it WILL use lets the editor resolve every include the moment
//      the first build finishes.
//
// This unit imports `mcpp`, so it exists only inside a build program. The
// program that performs the installation is `mcpp-deps` (tools/deps_main.cpp),
// an ordinary executable built from this package.

export module mcpp.deps;

import std;
import mcpp;
import mcpp.plugins;

export namespace mcpp::deps {

// Prints `message` and records it as a `mcpp::warning`, folded onto one line.
// The engine discards a build program's output when it exits 0, so a note that
// only went to stderr would be invisible on exactly the builds that succeed.
inline void warn(const std::string& message) {
    std::cerr << message << '\n';
    std::string folded;
    folded.reserve(message.size());
    bool space = false;
    for (char c : message) {
        if (c == '\n' || c == '\r') { space = true; continue; }
        if (space) {
            if (c == ' ') continue;
            folded += ' ';
            space = false;
        }
        folded += c;
    }
    mcpp::warning(folded.c_str());
}

// The installer program, or empty after saying which line brings it.
inline std::string launcher(std::string_view member, std::string_view feature) {
    const std::string tool = mcpp::dep_bin("plugins", "mcpp-deps");
    if (!tool.empty()) return tool;
    std::cerr << std::format(
        "{0}: the installation runs as a build action, and an action's command is\n"
        "  a program: `mcpp-deps`, built from this package. Ask for it on the edge\n"
        "  that brings the member in:\n\n"
        "      [build-dependencies.mcpp]\n"
        "      plugins = {{ version = \"{1}\", features = [\"{2}\"], host-module = true,\n"
        "                  tools = [\"mcpp-deps\"] }}\n",
        member, mcpp::plugins::version, feature);
    return {};
}

inline bool is_windows() { return std::string_view(mcpp::target_os()) == "windows"; }
inline bool is_macos()   { return std::string_view(mcpp::target_os()) == "macos"; }

// Relative paths are the package root's, as every other path a member takes.
inline std::filesystem::path absolute_from_root(const std::string& p) {
    std::filesystem::path path(p);
    if (path.is_relative()) path = std::filesystem::path(mcpp::manifest_dir()) / path;
    return path.lexically_normal();
}

inline std::string generic(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

// The nearest directory at or above `start` that holds `file`. A workspace
// keeps one `vcpkg.json` at its root and its members one level down, so the
// member's own directory is where the search begins, not where it ends.
inline std::filesystem::path find_upward(std::filesystem::path start, std::string_view file) {
    std::error_code ec;
    for (auto dir = start.lexically_normal(); !dir.empty(); ) {
        if (std::filesystem::is_regular_file(dir / file, ec)) return dir;
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

// Every regular file under `dir`, for an action's inputs. Version-control and
// build-output directories are skipped: they change on every build and are no
// part of what the installer reads.
inline std::vector<std::string> files_under(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (auto it = std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto name = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (name == ".git" || name == "target" || name == "out" || name == "build" ||
                name == "Install" || name == "vcpkg_installed")
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec)) out.push_back(generic(it->path()));
    }
    std::ranges::sort(out);
    return out;
}

// The file a library name denotes under `lib_dir`. A name that already carries
// an extension is a file name and is taken as written; otherwise the target's
// convention decides: `<name>.lib` on Windows (an import library and a static
// library have the same name there), `lib<name>.so` / `.dylib` for a shared
// library elsewhere, `lib<name>.a` for a static one.
//
// A FULL PATH, NOT `-l<name>`. `-lz` resolves through every search directory
// on the line, so a system `libz` can stand in for the prefix's without a
// word; a path names exactly one file.
inline std::filesystem::path library_file(const std::filesystem::path& lib_dir,
                                          const std::string& name, bool shared) {
    std::filesystem::path given(name);
    if (given.is_absolute()) return given;
    if (given.has_extension()) {
        const auto ext = given.extension().string();
        if (ext == ".lib" || ext == ".a" || ext == ".so" || ext == ".dylib" || ext == ".tbd")
            return lib_dir / given;
    }
    if (is_windows()) return lib_dir / (name + ".lib");
    const std::string stem = name.starts_with("lib") ? name : "lib" + name;
    if (!shared) return lib_dir / (stem + ".a");
    return lib_dir / (stem + (is_macos() ? ".dylib" : ".so"));
}

// Links each library by its full path, and says which ones are missing once
// the prefix exists. The path is stated whether or not the file exists yet:
// the link edge runs after the installation, and a line that depended on what
// happened to be on disk at plan time would differ between the first build and
// the second.
inline void link_libraries(const std::filesystem::path& lib_dir,
                           std::span<const std::string> names, bool shared,
                           bool prefix_exists, std::string_view member) {
    std::vector<std::string> missing;
    std::error_code ec;
    for (auto const& name : names) {
        const auto file = library_file(lib_dir, name, shared);
        mcpp::link_flag(generic(file).c_str());
        if (prefix_exists && !std::filesystem::exists(file, ec)) missing.push_back(generic(file));
    }
    if (missing.empty()) return;
    std::string present;
    for (auto const& e : std::filesystem::directory_iterator(lib_dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const auto ext = e.path().extension().string();
        if (ext == ".lib" || ext == ".a" || ext == ".so" || ext == ".dylib")
            present += "\n    " + e.path().filename().string();
    }
    std::string list;
    for (auto const& m : missing) list += "\n    " + m;
    warn(std::format("{}: {} listed librar{} not found in the installed prefix:{}\n"
                     "  `libraries` names files in {} (a name, or a file name with its "
                     "extension). Present there:{}",
                     member, missing.size(), missing.size() == 1 ? "y is" : "ies are", list,
                     generic(lib_dir), present.empty() ? std::string(" (none)") : present));
}

// The prefix's shared libraries, placed beside the program: `mcpp run` and a
// program started by hand find them there, and `mcpp pack` carries every
// deployed file. Off Windows the program also carries the library directory as
// a run path, so a library installed with CMake's default `@rpath/` install
// name is found on macOS.
//
// PLACED, NOT SEARCHED. A build program on a released engine has no directive
// that adds a directory to the program's run-time search set (the manifest's
// `[runtime] library_dirs` is a fixed list); `mcpp::deploy` is the channel it
// has. A file is deployed when it exists at
// plan time, so on a project's first build the libraries an installation is
// about to produce are placed by the next plan -- which `mcpp run` performs,
// because the installation changed the files this program declared it reads.
inline void deploy_shared(const std::string& bin, const std::string& lib) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = is_windows() ? fs::path(bin) : fs::path(lib);
    std::vector<fs::path> files;
    for (auto const& e : fs::directory_iterator(dir, ec)) {
        const auto name = e.path().filename().string();
        const bool shared = is_windows() ? e.path().extension() == ".dll"
                          : is_macos()   ? e.path().extension() == ".dylib"
                                         : (name.ends_with(".so") || name.find(".so.") != std::string::npos);
        if (shared && (e.is_regular_file(ec) || e.is_symlink(ec))) files.push_back(e.path());
    }
    std::ranges::sort(files);
    for (auto const& f : files) mcpp::deploy(generic(f).c_str(), ".");
    if (!is_windows()) {
        const std::string rpath = "-Wl,-rpath," + lib;
        mcpp::link_flag(rpath.c_str());
    }
}

} // namespace mcpp::deps
