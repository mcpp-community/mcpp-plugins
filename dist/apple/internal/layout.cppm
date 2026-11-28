// mcpp.dist.apple.internal.layout -- file, path and launcher-name helpers of mcpp.dist.apple.

export module mcpp.dist.apple.internal.layout;

import std;
import mcpp;

namespace mcpp::dist::apple::detail {

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Written only when the bytes differ, for the reason `dist/appimage.cppm`
// gives: rewriting identical bytes moves the mtime, and a moved mtime on a
// declared input is indistinguishable from a changed one -- so a second pack
// of an unchanged project would re-copy the bundle and re-sign it.
inline bool write_if_different(const std::filesystem::path& path,
                               std::string_view bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        if (old == bytes) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

inline std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string resolve_path(const std::string& p) {
    if (p.empty() || std::filesystem::path(p).is_absolute()) return p;
    return (std::filesystem::path(mcpp::manifest_dir()) / p).lexically_normal().string();
}

// The program a bundle executes, found in the staged tree: `bin/<target>`
// first, then the tree's top-level entry, then `run.sh`.
//
// THE PROGRAM, NOT THE TREE'S ENTRY SCRIPT. From the release that reads a
// Mach-O program's closure (mcpp 2026.9.14.2), the engine also writes
// `<tree>/<target>`, a shell script that executes `bin/<target>` from the
// tree's root. The search used to take that root entry first, which is the
// order `dist/appimage.cppm` needs, and a bundle then executed the script,
// which executed `Contents/MacOS/bin/<target>` -- a file no bundle carries:
// "cannot execute: No such file or directory", exit 126 (macos-15, run
// 34821164486). The script has no work to do inside a bundle, where
// `CFBundleExecutable` names the program directly, and the framework rpath
// `@executable_path/../Frameworks` resolves against the program's own
// directory, which has to be `Contents/MacOS/`. The root entry and `run.sh`
// remain for a tree that carries no `bin/<target>`.
inline std::string launcher_in(const std::string& stage, const std::string& target) {
    for (auto candidate : {stage + "/bin/" + target,
                           stage + "/" + target,
                           stage + "/run.sh"})
        if (is_file(candidate)) return candidate;
    return {};
}

// CFBundleExecutable MUST BE A BARE FILENAME, NOT A PATH.
//
// Apple's own bundle documentation describes it as the executable's name
// within `Contents/MacOS/`, and real-world bundle tooling has hit this
// directly enough to be a filed CMake defect: "CFBundleExecutable path in a
// bundle should not be a relative path into bundle." So this member takes
// only the basename of whatever `launcher_in` finds, and the layout step
// copies that file to the top of the executable directory under that name, so
// the name and the file agree wherever in the staged tree it was found.
inline std::string bundle_executable_name(const std::string& launcher_path) {
    return std::filesystem::path(launcher_path).filename().string();
}

inline std::string sanitize_bundle_id_component(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9') || c == '-' || c == '.';
        out += ok ? c : '-';
    }
    return out;
}

} // namespace mcpp::dist::apple::detail
