export module mcpp.plugins:tree;

import std;


// mcpp::plugins::tree -- the directories a walk of a source tree leaves out.
//
// SHARED BECAUSE TWO MEMBERS WALK A SOURCE TREE BY DEFAULT. `rules-qt` finds
// the headers moc reads and the sources lupdate reads under the package, and
// `deps` takes every file of a subproject as an input of its installation, and
// `deps-cmake` as part of the installation's key. Neither may take what ANOTHER
// build system generated there: a Visual Studio project built in the same
// checkout leaves its moc and uic output under `x64/Release/`, and lupdate
// given those files rewrites the translations, and a CMake build directory an
// IDE made inside a subproject changes the key on every build.
//
// Such a directory is known by what its build system leaves in it, not by its
// name: `x64/` is as often a directory of architecture-specific sources.
export namespace mcpp::plugins::tree {

// Whether `dir` is an output directory of another build system:
//
//   CMakeCache.txt   a CMake build tree (an IDE's `cmake-build-*`, `out/build/*`)
//   .qmake.stash     a qmake build tree
//   CACHEDIR.TAG     a directory its tool tags as a cache (https://bford.info/cachedir/)
//   <name>.tlog/     an MSBuild intermediate directory (`x64/Release/`), where a
//                    Visual Studio project writes the sources it generates
//
// A walk asks this of the directories under its root, never of the root: a
// subproject configured in its own source directory holds CMakeCache.txt too.
inline bool build_output(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(dir / "CMakeCache.txt", ec) || fs::is_regular_file(dir / ".qmake.stash", ec))
        return true;
    if (fs::is_regular_file(dir / "CACHEDIR.TAG", ec)) {
        std::ifstream in(dir / "CACHEDIR.TAG", std::ios::binary);
        std::string head(43, '\0');
        in.read(head.data(), 43);
        if (head == "Signature: 8a477f597d28d172789f06886806bc55") return true;
    }
    for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->path().extension() == ".tlog" && it->is_directory(ec)) return true;
    return false;
}

} // namespace mcpp::plugins::tree
