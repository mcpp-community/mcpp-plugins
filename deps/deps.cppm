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
//   1. The installation is an ACTION, never work the build program does
//      (mcpp's SPEC-007 R1.1). `mcpp emit build-database` runs build programs
//      to plan, a build program has a 600-second limit, and an installation
//      can take an hour. The action's role is `prepare` (R3.3): it fills a
//      directory whose file names are unknown until it has run, the package's
//      compile and link edges wait for it, and the engine writes its stamp.
//      The build program refers to the directory by name -- include
//      directory, libraries by full path, runtime search directory -- and
//      never by what is in it (R1.3).
//
//   2. A missing tool is a WARNING, not a failure (R1.2). An editor asks for
//      the plan on machines that never built; a build program that exits 1
//      there takes the whole build database with it, and one that states the
//      paths it WILL use lets the editor resolve every include the moment the
//      first build finishes.
//
// This unit imports `mcpp`, so it exists only inside a build program. Each
// action's command is the installer itself -- `vcpkg`, or `cmake -P` over a
// script the member writes -- so the package brings no program of its own and
// a consumer's edge names none (0.15.0; 0.13.0-0.14.0 ran `mcpp-deps`).

export module mcpp.deps;

import std;
import mcpp;
import mcpp.plugins;
// The 0.16.0 names of the helpers that moved to `mcpp.plugins.fs` and
// `mcpp.plugins.toolset` (a compatibility unit, until 2027-03-28).
export import mcpp.deps.compat;

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

// A CMake script this build program writes, for an action whose work is more
// than one command: `cmake -P <script>` is one argument vector, and the script
// runs the steps in order and fails on the first that fails. Every value is a
// bracket argument, so a path or a `-D` value keeps its spaces and semicolons.
// The script is written only when its text changes, so its time stamp -- an
// input of the action -- moves only when the work does.
inline std::string bracket(std::string_view text) {
    return "[==[" + std::string(text) + "]==]";
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

// Re-runs the build program when the SET of files under `dir` changes, so a
// file added to a subproject or an overlay becomes an input of the action the
// next plan declares; `files_under` names the files that exist now. The
// pattern is relative to the package root, as `rerun_if_changed_glob` takes it.
inline void watch_tree(const std::filesystem::path& dir) {
    const auto rel = dir.lexically_relative(std::filesystem::path(mcpp::manifest_dir()));
    const std::string pattern = (rel.empty() ? std::string(".") : rel.generic_string()) + "/**";
    mcpp::rerun_if_changed_glob(pattern.c_str());
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

// Links each library by its full path. The path is stated whether or not the
// file exists yet: the link edge runs after the installation, and a link line
// that depended on what happened to be on disk at plan time would differ
// between the first build and the second (SPEC-007 R1.3). A name that matches
// no installed file fails the link, naming the path.
inline void link_libraries(const std::filesystem::path& lib_dir,
                           std::span<const std::string> names, bool shared) {
    for (auto const& name : names)
        mcpp::link_flag(generic(library_file(lib_dir, name, shared)).c_str());
}

// The directory a prefix's shared libraries are loaded from: `bin/` on
// Windows, `lib/` elsewhere, declared with `mcpp::runtime_search_dir`
// (SPEC-007 R4.1). The engine renders it as the program's run path on ELF and
// Mach-O, puts it on `mcpp run`'s load path, searches it for `mcpp pack`'s
// closure, carries a dependency's declaration to the consumer's executable,
// and on Windows places the DLLs the program imports from it beside the
// program (R4.3).
inline void runtime_directory(const std::string& bin, const std::string& lib) {
    const std::string& dir = is_windows() ? bin : lib;
    mcpp::runtime_search_dir(dir.c_str());
}

// vcpkg's own per-user directory: where its default binary cache
// (`archives/`) and registry cache (`registries/`) already live (vcpkg's
// "Default binary cache" documentation). Scratch and downloads go beside them.
inline std::filesystem::path vcpkg_user_dir() {
    namespace fs = std::filesystem;
    auto env = [](const char* n) { const char* v = std::getenv(n); return std::string(v ? v : ""); };
    if (std::string_view(mcpp::host()).find("windows") != std::string_view::npos) {
        if (auto v = env("LOCALAPPDATA"); !v.empty()) return fs::path(v) / "vcpkg";
        if (auto v = env("APPDATA"); !v.empty()) return fs::path(v) / "vcpkg";
    } else {
        if (auto v = env("XDG_CACHE_HOME"); !v.empty()) return fs::path(v) / "vcpkg";
        if (auto v = env("HOME"); !v.empty()) return fs::path(v) / ".cache" / "vcpkg";
    }
    return fs::temp_directory_path() / "vcpkg";
}

// FNV-1a over `text`: a short directory name, stable across runs and
// compilers, which `std::hash` does not promise. Index loop: GCC 16 does not
// inline a string's iterators across a module boundary.
inline std::string short_name(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < text.size(); ++i) {
        h ^= static_cast<unsigned char>(text[i]);
        h *= 1099511628211ull;
    }
    return std::format("{:08x}", std::uint32_t(h ^ (h >> 32)));
}

} // namespace mcpp::deps
