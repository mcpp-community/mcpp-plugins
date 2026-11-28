// mcpp.dist.apk.internal.util -- the shared file, host and text helpers of dist/apk.

export module mcpp.dist.apk.internal.util;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.plugins.tool;

namespace mcpp::dist::apk {

namespace fs = std::filesystem;

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}
inline bool is_dir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

inline bool host_is_windows();

inline std::string host_program(const fs::path& dir, std::string_view name) {
    return (dir / (std::string(name) + (host_is_windows() ? ".exe" : ""))).string();
}
inline const char* class_path_separator() { return host_is_windows() ? ";" : ":"; }

// Is `root` under the package root, `mcpp::manifest_dir()`, and if so, what
// is its manifest-relative form? A project root is declared with
// `rerun_if_changed_glob` below (a file appearing there re-runs the build
// program); a dependency root is not -- its file set changes only with the
// dependency's version, already in the build's fingerprint, and the glob's
// own walk does not reach outside the package root regardless (design
// record §3.3). Same shape as `mcpp.tools.island`'s overlap check:
// `weakly_canonical` plus `lexically_relative`, never the iterator that
// poisons an importer under GCC 16 / MSVC (`mcpp::plugins::names::
// relative_to`'s own header) -- safe here because this member is its own
// module and imports no sibling that would inherit the instantiation.
//
// THE RETURNED PATH IS MANIFEST-RELATIVE, NOT ABSOLUTE, BECAUSE THE GLOB
// PATTERN MUST BE. The engine matches a glob by comparing the CANDIDATE made
// relative to the package root against the pattern
// (`modules/manifest/src/glob.cppm`, `path_matches_glob`); an absolute
// pattern is compared against a relative candidate and never matches
// anything, so the fingerprint is always the empty set and a `.java` file
// appearing never changes it -- the criterion's "no" reading as silence
// (design record, rule 8). `opt.java_sources`'s own roots are absolute
// (`mcpp::manifest_dir()` composed with a subdirectory), so declaring the
// glob with `root` itself, not this function's return value, was exactly
// that defect.
//
// A root inside the build's output -- `mcpp::out_dir()`, or a directory the
// glob walk never enters (`target`, `.mcpp`, `.git`) -- is GENERATED: the
// build program that writes its files already runs on every build, and a glob
// there can never match anything, which the engine reports as a warning. It is
// answered as outside the project, so it is an action input and no glob.
inline std::optional<std::string> root_in_project(const std::string& root) {
    std::error_code ec;
    const auto a = fs::weakly_canonical(root, ec);
    if (ec) return std::nullopt;
    const auto b = fs::weakly_canonical(mcpp::manifest_dir(), ec);
    if (ec) return std::nullopt;
    const auto rel = a.lexically_relative(b).generic_string();
    if (rel.empty() || rel.starts_with("..")) return std::nullopt;
    const auto out = fs::weakly_canonical(std::string(mcpp::out_dir()), ec);
    if (!ec) {
        const auto inside = a.lexically_relative(out).generic_string();
        if (!inside.empty() && !inside.starts_with("..")) return std::nullopt;
    }
    const auto first = rel.substr(0, rel.find('/'));
    if (first == "target" || first == ".mcpp" || first == ".git") return std::nullopt;
    return rel;
}

inline bool write_if_different(const fs::path& path, std::string_view bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
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

// `<abi>/`, from the row's arch and no other input -- see `artifact_naming`
// for the engine's own row-decides-the-name principle, applied here to
// Android's own ABI vocabulary rather than a file suffix.
inline std::string abi_for() {
    const std::string a = mcpp::target_arch();
    if (a == "aarch64") return "arm64-v8a";
    if (a == "x86_64")  return "x86_64";
    return {};
}


// Index loop, not a range-for: GCC 16.1.0 refuses to inline
// `__normal_iterator<char*, basic_string<char>>::operator*() const` when a
// module interface unit that imports `std` also range-for's (or otherwise
// takes `basic_string`'s own iterator over) a `std::string` or
// `std::filesystem::path` --
//
//   bits/stl_iterator.h:1089:7: error: inlining failed in call to
//   'always_inline' '... operator*() const'
//
// -- the same family of defect #18 met under MSVC's `_Path_iterator` (see
// `mcpp::plugins::names::relative_to` and `components()` in
// `src/plugins.cppm`), here on `libstdc++`'s `basic_string` iterator instead
// of the filesystem one. `s[i]` indexes the string directly and never forms
// that iterator, so it compiles under both.
inline std::string replace_dashes(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == '-') s[i] = '_';
    return s;
}

// `xim:android-platform`'s own resolved directory is named after the
// version it resolved -- "36-r2", "35-r2", "34-r3" (`pkgs/a/android-
// platform.lua`'s own `extract_dir()` recovers the API level the identical
// way, from the leading digits of its OWN version string) -- so the level a
// project pinned is read back from the directory `xpkg_dir` already
// answered, rather than duplicated as a second option this member could
// disagree with.
inline std::string api_level_from_platform_dir(const std::string& dir) {
    if (dir.empty()) return {};
    const std::string leaf = fs::path(dir).filename().string();
    std::string digits;
    for (std::size_t i = 0; i < leaf.size(); ++i) {
        if (leaf[i] >= '0' && leaf[i] <= '9') digits += leaf[i];
        else break;
    }
    return digits;
}

inline std::string replace_all_copy(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// Copies `src` (a file or a directory, recursively) into `dstRoot/relName`,
// listing every regular file it wrote -- so the caller can declare each one
// as an action input rather than the directory itself ("name the input; do
// not harvest a directory", docs/31).
inline void collect_tree(const fs::path& src, const fs::path& dst,
                         std::vector<std::string>& out) {
    std::error_code ec;
    if (fs::is_regular_file(src, ec)) {
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        out.push_back(dst.string());
        return;
    }
    for (auto& e : fs::recursive_directory_iterator(src, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto rel = fs::relative(e.path(), src, ec);
        auto to  = dst / rel;
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(e.path(), to, fs::copy_options::overwrite_existing, ec);
        out.push_back(to.string());
    }
}

namespace xml = mcpp::plugins::xml;

// ─── Paths a project names ─────────────────────────────────────────────────

// A project names its directories relative to its manifest; the actions this
// member declares run later, from the build directory, where a relative path
// names nothing (the defect `dist/apple.cppm` measured with `ditto ios-icons`).
// Every path option is therefore resolved here, once, against
// `mcpp::manifest_dir()`; an absolute path is left alone.
inline std::string resolve_path(const std::string& p) {
    if (p.empty()) return p;
    const fs::path path(p);
    if (path.is_absolute()) return p;
    return (fs::path(mcpp::manifest_dir()) / path).lexically_normal().string();
}

inline void resolve_paths(std::vector<std::string>& v) {
    for (auto& p : v) p = resolve_path(p);
}

// Every regular file under `root` whose extension is `ext`, sorted.
inline std::vector<std::string> files_with_extension(const std::string& root, std::string_view ext) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) && e.path().extension() == ext)
            out.push_back(e.path().string());
    }
    std::ranges::sort(out);
    return out;
}

inline bool dir_has_files(const std::string& root) {
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec)) return true;
    }
    return false;
}

inline std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

// ─── Running a tool at plan time ─────────────────────────────────────────

// Runs `argv`, returning its exit status and its combined output, through
// `mcpp::plugins::tool::capture`: no shell between, so no argument is quoted.
// Only the explicit Maven modes and AAR extraction call it; an ordinary build
// of an unchanged project runs nothing here.
int run_capture(const std::vector<std::string>& argv, std::string& output,
                       const std::filesystem::path& cwd = {}) {
    auto r = mcpp::plugins::tool::capture(argv, /*with_errors=*/true, cwd);
    output = std::move(r.out);
    return r.status;
}

// Android's own ABI names, the directory names a several-triple tree stages
// under (`lib/<abi>/`) and an APK stores its native libraries under.
inline bool is_android_abi(std::string_view name) {
    return name == "arm64-v8a" || name == "x86_64" || name == "armeabi-v7a" || name == "x86";
}

// One ABI's native libraries, as the staged tree carries them.
struct native_leg {
    std::string              abi;
    std::vector<std::string> libraries;   // absolute paths, sorted
};

inline std::vector<std::string> shared_objects_in(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) && e.path().extension() == ".so")
            out.push_back(e.path().string());
    }
    std::ranges::sort(out);
    return out;
}
} // namespace mcpp::dist::apk

module :private;

namespace mcpp::dist::apk {
// THE HOST, NOT THE TARGET: every tool here runs on the machine that builds.
// A Windows host names its programs `<name>.exe` and separates a class path
// with `;`.
inline bool host_is_windows() {
    return std::string_view(mcpp::host()).find("windows") != std::string_view::npos;
}
} // namespace mcpp::dist::apk

