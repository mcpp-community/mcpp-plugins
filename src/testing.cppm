// mcpp.plugins.testing -- the test kit for build plugins (L2 of the
// build-plugin architecture, mcpp#734; feature `plugins-testing`).
//
// A plugin's logic is a function of the build context, and its effect is the
// set of directives and actions it emits, which are `mcpp:` lines on standard
// output. The kit runs a plugin function against a STATED context -- the
// target, the toolchain facts, the profile, the directories, files the
// function reads -- and hands the emitted lines, and the files the function
// wrote, to a check. No foreign tool is installed or run: a context that
// describes a Visual Studio row is tested on a Linux machine.
//
// A test is a build program:
//
//   import std;
//   import mcpp.plugins.testing;
//   import mcpp.deps.vcpkg;
//   namespace t = mcpp::plugins::testing;
//
//   int main(int argc, char** argv) {
//       return t::run(argc, argv, {
//           { "the managed toolset is chained",
//             t::row::windows_managed(),
//             [] { return mcpp::deps::vcpkg::use({ .libraries = {"fmt"} }) ? 0 : 1; },
//             [](t::result const& r, t::checker& c) {
//                 c.expect(r.has_line("mcpp:action=", "--host-triplet="), "the host triplet is derived");
//             } },
//       });
//   }
//
// HOW. The build program is run by mcpp as usual; `run` starts the same
// executable once per case (`--mcpp-plugins-testing-case=<n>`) with the case's
// context in its environment and its standard output in a file, and runs the
// check on what it wrote. A separate process per case is what makes the
// context complete: every accessor of `mcpp.core` reads the environment, and
// a function's `static` state starts fresh. A failed check fails the build
// program, which fails the build and prints the report. The verdicts are also
// written to `<out_dir>/plugins-testing/results.txt`, one `PASS` or `FAIL`
// line per case, for a CI step to count.

module;
#include <stdlib.h>
#if !defined(_WIN32)
#include <unistd.h>     // environ -- the kit clears the real build's MCPP_XPKG_*
#endif

export module mcpp.plugins.testing;

import std;
import mcpp;

namespace mcpp::plugins::testing::detail {

inline void set_env(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    ::_putenv_s(name.c_str(), value.c_str());
#else
    if (value.empty()) ::unsetenv(name.c_str());
    else ::setenv(name.c_str(), value.c_str(), 1);
#endif
}

// Every key an accessor of `mcpp.core` reads that a plugin's logic depends on.
// A case sets each of them -- empty unless its context states a value -- so
// nothing of the real build reaches the function under test.
inline constexpr std::string_view kKeys[] = {
    "MCPP_TARGET", "MCPP_TARGET_OS", "MCPP_TARGET_ARCH", "MCPP_TARGET_ENV", "MCPP_HOST",
    "MCPP_PROFILE", "MCPP_ACCEL", "MCPP_OUT_DIR", "MCPP_MANIFEST_DIR", "MCPP_TOOLCHAIN_DIR",
    "MCPP_TOOLCHAIN_SYSROOT", "MCPP_COMPILER", "MCPP_CXX_STDLIB", "MCPP_TARGET_SYSROOT",
    "MCPP_PKG_NAME", "MCPP_PKG_NAMESPACE", "MCPP_PKG_VERSION", "MCPP_PACK_FORMAT",
    "MCPP_TOOL_CC", "MCPP_TOOL_CXX", "MCPP_TOOL_LD", "MCPP_TOOL_AR", "MCPP_TOOL_RC",
    "MCPP_TOOL_AS", "MCPP_TOOL_MT",
    "MCPP_ABI_TOOL_CC", "MCPP_ABI_TOOL_CXX", "MCPP_ABI_TOOL_LD", "MCPP_ABI_TOOL_AR",
    "MCPP_ABI_TOOL_RC", "MCPP_ABI_TOOL_AS", "MCPP_ABI_TOOL_MT",
    "MCPP_TOOL_ENV", "MCPP_TOOLSET_IDENTITY", "MCPP_MSVC_INSTANCE_DIR", "MCPP_NINJA",
    "MCPP_CXX_RUNTIME", "MCPP_MSVC_CRT_LINKAGE",
    // Sources (0.19.0, mcpp#755): which phase is running. The per-payload keys
    // are stated by `context::xpkg*`, which names them itself, and the ones the
    // real build set are cleared by `inherited_payload_keys`.
    "MCPP_PHASE",
};

// EVERY `MCPP_XPKG_*` THE REAL BUILD SET, so that a case sees only the payloads
// it states. These cases run inside a build program, and the engine gives that
// program one `_DIR`, `_PROGRAM` and `_SOURCE` for each payload its package
// declares -- `pending` among them, for a payload declared
// `provision = "on-request"` and not installed. Inherited, that made the eight
// vcpkg cases ask for `xim:vcpkg` and plan nothing on a host where it was not
// installed, while passing on one where it was; the symptom was a plan with the
// prefix mapping but no install action (measured on macOS arm64, 0.19.0).
//
// Enumerated rather than listed: the keys are derived from package names, so no
// fixed list can cover the next payload a member reads.
inline std::vector<std::string> inherited_payload_keys() {
#if defined(_WIN32)
    char** env = _environ;
#else
    char** env = environ;
#endif
    std::vector<std::string> out;
    for (char** e = env; e && *e; ++e) {
        std::string_view entry(*e);
        const auto eq = entry.find('=');
        if (eq == std::string_view::npos) continue;
        const auto name = entry.substr(0, eq);
        if (name.starts_with("MCPP_XPKG_")) out.emplace_back(name);
    }
    return out;
}

inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace mcpp::plugins::testing::detail

export namespace mcpp::plugins::testing {

// A stated build context: environment values for the accessors, and files
// that exist before the function runs. `{root}` in a value or a path is the
// case's own scratch directory, created empty for every case.
struct context {
    std::vector<std::pair<std::string, std::string>> values;
    std::vector<std::pair<std::string, std::string>> files;

    context& set(std::string key, std::string value) {
        for (auto& kv : values) if (kv.first == key) { kv.second = std::move(value); return *this; }
        values.emplace_back(std::move(key), std::move(value));
        return *this;
    }
    context& file(std::string path, std::string content = {}) {
        files.emplace_back(std::move(path), std::move(content));
        return *this;
    }
    // `MCPP_XPKG_<NS>_<NAME>_<SUFFIX>`, spelled as the engine spells it.
    std::string xpkg_key(std::string_view ns, std::string_view name,
                         std::string_view suffix) const {
        std::string key = "MCPP_XPKG_";
        auto put = [&](std::string_view s) {
            for (std::size_t i = 0; i < s.size(); ++i) {
                const char c = s[i];
                key += (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A')
                     : ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '_';
            }
        };
        if (!ns.empty()) { put(ns); key += '_'; }
        put(name);
        key += '_';
        key += suffix;
        return key;
    }
    // `xpkg_dir(ns, name)`: the directory of a declared payload.
    context& xpkg(std::string_view ns, std::string_view name, std::string dir) {
        return set(xpkg_key(ns, name, "DIR"), std::move(dir));
    }
    // `xpkg_source(ns, name)` (0.19.0): "payload", "override" or "pending".
    context& xpkg_source(std::string_view ns, std::string_view name, std::string source) {
        return set(xpkg_key(ns, name, "SOURCE"), std::move(source));
    }
    // `xpkg_program(ns, name)` (0.19.0): the program an override named.
    context& xpkg_program(std::string_view ns, std::string_view name, std::string program) {
        return set(xpkg_key(ns, name, "PROGRAM"), std::move(program));
    }
    // The phase a case runs in (0.19.0): "toolchain" for a toolchain phase.
    context& phase(std::string name) { return set("MCPP_PHASE", std::move(name)); }
};

// Contexts that describe the rows a plugin meets. Paths are under `{root}`,
// so they exist only where a case creates them.
namespace row {

inline context linux_gcc() {
    return context{}
        .set("MCPP_TARGET", "x86_64-linux-gnu").set("MCPP_HOST", "x86_64-linux-gnu")
        .set("MCPP_TARGET_OS", "linux").set("MCPP_TARGET_ARCH", "x86_64").set("MCPP_TARGET_ENV", "gnu")
        .set("MCPP_PROFILE", "dev").set("MCPP_COMPILER", "gcc").set("MCPP_CXX_STDLIB", "libstdc++")
        .set("MCPP_TOOLCHAIN_DIR", "{root}/gcc")
        .set("MCPP_TOOL_CC", "{root}/gcc/bin/gcc").set("MCPP_TOOL_CXX", "{root}/gcc/bin/g++")
        .set("MCPP_TOOL_LD", "{root}/gcc/bin/g++").set("MCPP_TOOL_AR", "{root}/gcc/bin/gcc-ar")
        .set("MCPP_ABI_TOOL_CC", "{root}/gcc/bin/gcc").set("MCPP_ABI_TOOL_CXX", "{root}/gcc/bin/g++")
        .set("MCPP_ABI_TOOL_LD", "{root}/gcc/bin/g++").set("MCPP_ABI_TOOL_AR", "{root}/gcc/bin/gcc-ar")
        .set("MCPP_TOOLSET_IDENTITY", "gcc 16.1.0").set("MCPP_NINJA", "{root}/ninja/ninja")
        .set("MCPP_CXX_RUNTIME", "toolchain-coupled")
        .set("MCPP_OUT_DIR", "{root}/out").set("MCPP_MANIFEST_DIR", "{root}/pkg");
}

inline context linux_libcxx() {
    return linux_gcc()
        .set("MCPP_COMPILER", "clang").set("MCPP_CXX_STDLIB", "libc++")
        .set("MCPP_TOOLCHAIN_DIR", "{root}/llvm")
        .set("MCPP_TOOL_CC", "{root}/llvm/bin/clang").set("MCPP_TOOL_CXX", "{root}/llvm/bin/clang++")
        .set("MCPP_TOOL_LD", "{root}/llvm/bin/clang++").set("MCPP_TOOL_AR", "{root}/llvm/bin/llvm-ar")
        .set("MCPP_ABI_TOOL_CC", "{root}/llvm/bin/clang").set("MCPP_ABI_TOOL_CXX", "{root}/llvm/bin/clang++")
        .set("MCPP_ABI_TOOL_LD", "{root}/llvm/bin/clang++").set("MCPP_ABI_TOOL_AR", "{root}/llvm/bin/llvm-ar")
        .set("MCPP_TOOLSET_IDENTITY", "clang 22.1.8");
}

// The MSVC ABI with cl.exe from `tools`, the SDK's rc.exe and mt.exe, and the
// environment the engine runs them with.
inline context windows_msvc(const std::string& tools, const std::string& version) {
    const std::string bin = tools + "/bin/Hostx64/x64";
    const std::string sdk = "{root}/sdk/bin/10.0.26100.0/x64";
    return context{}
        .set("MCPP_TARGET", "x86_64-pc-windows-msvc").set("MCPP_HOST", "x86_64-pc-windows-msvc")
        .set("MCPP_TARGET_OS", "windows").set("MCPP_TARGET_ARCH", "x86_64").set("MCPP_TARGET_ENV", "msvc")
        .set("MCPP_PROFILE", "dev").set("MCPP_COMPILER", "msvc").set("MCPP_CXX_STDLIB", "msvc-stl")
        .set("MCPP_TOOL_CC", bin + "/cl.exe").set("MCPP_TOOL_CXX", bin + "/cl.exe")
        .set("MCPP_TOOL_LD", bin + "/link.exe").set("MCPP_TOOL_AR", bin + "/lib.exe")
        .set("MCPP_TOOL_RC", sdk + "/rc.exe").set("MCPP_TOOL_MT", sdk + "/mt.exe")
        .set("MCPP_ABI_TOOL_CC", bin + "/cl.exe").set("MCPP_ABI_TOOL_CXX", bin + "/cl.exe")
        .set("MCPP_ABI_TOOL_LD", bin + "/link.exe").set("MCPP_ABI_TOOL_AR", bin + "/lib.exe")
        .set("MCPP_ABI_TOOL_RC", sdk + "/rc.exe").set("MCPP_ABI_TOOL_MT", sdk + "/mt.exe")
        .set("MCPP_TOOL_ENV", "INCLUDE=" + tools + "/include;{root}/sdk/Include/ucrt\n"
                              "LIB=" + tools + "/lib/x64;{root}/sdk/Lib/ucrt/x64\n"
                              "PATH=" + bin + ";" + sdk)
        .set("MCPP_TOOLSET_IDENTITY", "msvc " + version + "; sdk 10.0.26100.0")
        .set("MCPP_NINJA", "{root}/ninja/ninja.exe")
        .set("MCPP_CXX_RUNTIME", "toolchain-coupled").set("MCPP_MSVC_CRT_LINKAGE", "dynamic")
        .set("MCPP_OUT_DIR", "{root}/out").set("MCPP_MANIFEST_DIR", "{root}/pkg");
}

// `msvc@system`: the toolset of a Visual Studio instance at `{root}/vs`, which
// selects `default_version` by itself.
inline context windows_visual_studio(const std::string& version = "14.44.35207",
                                     const std::string& default_version = "14.44.35207") {
    return windows_msvc("{root}/vs/VC/Tools/MSVC/" + version, version)
        .set("MCPP_MSVC_INSTANCE_DIR", "{root}/vs")
        .file("vs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt", default_version + "\n");
}

// A managed MSVC toolset: no Visual Studio instance.
inline context windows_managed(const std::string& version = "14.44.35207") {
    return windows_msvc("{root}/msvc/VC/Tools/MSVC/" + version, version);
}

} // namespace row

// What one case produced.
struct result {
    int         exit_code = 0;
    std::vector<std::string> lines;   // standard output, one entry per line
    std::string errors;               // standard error
    std::filesystem::path root;       // the case's scratch directory

    // A line that starts with `prefix` and contains every one of `parts`.
    bool has_line(std::string_view prefix, std::initializer_list<std::string_view> parts = {}) const {
        return !line(prefix, parts).empty();
    }
    bool has_line(std::string_view prefix, std::string_view part) const {
        return has_line(prefix, {part});
    }
    std::string line(std::string_view prefix, std::initializer_list<std::string_view> parts = {}) const {
        for (auto const& l : lines) {
            if (!l.starts_with(prefix)) continue;
            bool all = true;
            for (auto p : parts) if (l.find(p) == std::string::npos) { all = false; break; }
            if (all) return l;
        }
        return {};
    }
    std::size_t count(std::string_view prefix) const {
        std::size_t n = 0;
        for (auto const& l : lines) if (l.starts_with(prefix)) ++n;
        return n;
    }
    // A file the function wrote, relative to the case's scratch directory.
    std::string file(std::string_view relative) const {
        return detail::read_file(root / std::filesystem::path(std::string(relative)));
    }
    // Every file under a directory of the scratch directory, relative to it.
    std::vector<std::string> files_under(std::string_view relative) const {
        std::vector<std::string> out;
        std::error_code ec;
        const auto dir = root / std::filesystem::path(std::string(relative));
        for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) out.push_back(it->path().lexically_relative(root).generic_string());
        std::ranges::sort(out);
        return out;
    }
};

struct checker {
    std::vector<std::string> failures;
    void expect(bool ok, std::string_view what) {
        if (!ok) failures.emplace_back(what);
    }
};

struct test_case {
    std::string name;
    context     ctx;
    std::function<int()> body;
    std::function<void(const result&, checker&)> check;
};

namespace detail2 {
inline std::string expand(std::string text, const std::string& root) {
    for (std::size_t at = text.find("{root}"); at != std::string::npos; at = text.find("{root}", at + root.size()))
        text.replace(at, 6, root);
    return text;
}
inline std::string quoted(const std::string& s) { return "\"" + s + "\""; }
} // namespace detail2

// Runs `cases` (see the header). Returns the build program's exit code: 0
// when every case passes.
inline int run(int argc, char** argv, std::span<const test_case> cases) {
    namespace fs = std::filesystem;
    constexpr std::string_view flag = "--mcpp-plugins-testing-case=";
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (!a.starts_with(flag)) continue;
        const std::size_t n = static_cast<std::size_t>(std::stoul(std::string(a.substr(flag.size()))));
        return n < cases.size() && cases[n].body ? cases[n].body() : 97;
    }

    std::error_code ec;
    const fs::path self = fs::absolute(fs::path(argc > 0 ? argv[0] : ""), ec);
    const std::string out = mcpp::out_dir();
    const fs::path base = (out.empty() ? fs::temp_directory_path() : fs::path(out)) / "plugins-testing";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    // The union of the keys any case states, beside the kit's own list.
    std::vector<std::string> keys;
    for (auto k : detail::kKeys) keys.emplace_back(k);
    for (auto& k : detail::inherited_payload_keys())
        if (std::ranges::find(keys, k) == keys.end()) keys.push_back(std::move(k));
    for (auto const& c : cases)
        for (auto const& kv : c.ctx.values)
            if (std::ranges::find(keys, kv.first) == keys.end()) keys.push_back(kv.first);

    std::string report, verdicts;
    int failed = 0;
    for (std::size_t n = 0; n < cases.size(); ++n) {
        const auto& c = cases[n];
        const fs::path root = base / std::format("case-{}", n);
        fs::create_directories(root, ec);
        const std::string rootS = root.generic_string();
        for (auto const& [path, content] : c.ctx.files) {
            const fs::path f = root / fs::path(detail2::expand(path, rootS));
            fs::create_directories(f.parent_path(), ec);
            std::ofstream(f, std::ios::binary) << detail2::expand(content, rootS);
        }
        for (auto const& k : keys) {
            std::string v;
            for (auto const& kv : c.ctx.values) if (kv.first == k) v = detail2::expand(kv.second, rootS);
            detail::set_env(k, v);
        }
        const fs::path so = root / "stdout.txt", se = root / "stderr.txt";
        std::string cmd = detail2::quoted(self.string()) + " " + std::string(flag) + std::to_string(n)
                        + " > " + detail2::quoted(so.string()) + " 2> " + detail2::quoted(se.string());
#if defined(_WIN32)
        cmd = "\"" + cmd + "\"";   // cmd.exe strips one outer pair of quotes
#endif
        const int status = std::system(cmd.c_str());

        result r;
        r.exit_code = status;
        r.root      = root;
        r.errors    = detail::read_file(se);
        std::istringstream in(detail::read_file(so));
        for (std::string l; std::getline(in, l); ) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            r.lines.push_back(l);
        }
        checker ck;
        if (c.check) c.check(r, ck);
        if (ck.failures.empty()) {
            verdicts += std::format("PASS {}\n", c.name);
        } else {
            ++failed;
            verdicts += std::format("FAIL {}\n", c.name);
            report += std::format("FAIL: {}\n", c.name);
            for (auto const& f : ck.failures) report += std::format("  expected: {}\n", f);
            report += std::format("  exit status {}; standard output ({} lines):\n", status, r.lines.size());
            for (auto const& l : r.lines) report += "    " + l + "\n";
            if (!r.errors.empty()) report += "  standard error:\n" + r.errors + "\n";
        }
    }
    std::ofstream(base / "results.txt", std::ios::binary) << verdicts;
    for (auto const& k : keys) detail::set_env(k, "");
    std::cerr << report
              << std::format("mcpp.plugins.testing: {} of {} cases passed\n", cases.size() - failed, cases.size());
    return failed == 0 ? 0 : 1;
}

inline int run(int argc, char** argv, std::initializer_list<test_case> cases) {
    return run(argc, argv, std::span<const test_case>(cases.begin(), cases.size()));
}

} // namespace mcpp::plugins::testing
