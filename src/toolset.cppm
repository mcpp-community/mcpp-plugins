// mcpp.plugins.toolset -- the resolved toolchain, translated for a foreign
// build system (L2 of the build-plugin architecture, mcpp#734).
//
// A plugin that drives vcpkg, CMake, Meson or make hands it the tools the
// program is built with, so that what it builds links with the program. The
// engine states the facts (mcpp 2026.9.28.3, protocol 14): each role's tool,
// the target ABI's native tools, the environment the engine runs them with,
// a path-free identity of the toolset, the Visual Studio instance it came
// from, and the C++ runtime contract. This module decides HOW those facts
// reach a foreign system, once for every plugin:
//
//   instance   An MSVC toolset from a Visual Studio instance (`msvc@system`)
//              is used through that instance. The foreign system's own
//              toolset loading stays in place, pointed at the instance mcpp
//              resolved, so MSBuild is present and every kind of project
//              builds.
//   chain      Any other toolset -- a managed MSVC toolset, and the clang
//              toolsets of the other rows -- is named: absolute tool paths, the
//              environment they run with, and the directories that go first
//              on PATH. On the MSVC ABI without an instance no MSBuild exists.
//   detected   Nothing is named, and the foreign system finds its own
//              toolset. `source::detected` asks for it (kept by
//              `src/compat/detected_toolset.cppm` until 2027-03-28); `resolved`
//              answers it on the Linux GCC row, whose payload driver is not a
//              complete handover (see `resolve`).
//
// NONE OF THIS NAMES A FOREIGN SYSTEM. vcpkg's triplet and CMake's generator
// are written by the plugins that drive them (`mcpp.deps.vcpkg`,
// `mcpp.deps.cmake`); this module stops at the tools.

export module mcpp.plugins.toolset;

import std;
import mcpp;
import mcpp.plugins.compat.detected_toolset;

export namespace mcpp::plugins::toolset {

// Who chooses the tools: the engine's resolution (`resolved`), or the foreign
// system's own detection (`detected`, a compatibility behaviour).
enum class source   { resolved, detected };
// Which compiler `resolved` hands over. `abi_native` is the target ABI's own
// compiler (`cl.exe` on the MSVC ABI whichever driver the row uses; the row's
// compiler elsewhere). `row` is the compiler the row itself runs, for example
// clang on an LLVM row that targets the MSVC ABI.
enum class compiler { abi_native, row };

// Embedded in each plugin's options.
struct choice {
    source   toolset = source::resolved;
    compiler cc      = compiler::abi_native;
};

enum class mechanism { instance, chain, detected };

inline std::string_view name(mechanism m) {
    return m == mechanism::instance ? "instance" : m == mechanism::chain ? "chain" : "detected";
}

struct resolved_tools {
    mechanism   how = mechanism::detected;
    std::string instance_dir;       // `instance`: the Visual Studio instance
    std::string toolset_version;    // `instance`: e.g. 14.44.35207; also set under `chain` on the MSVC ABI
    std::string instance_default;   // `instance`: the version the instance selects by itself
    // Absolute paths, forward slashes. `chain` sets `cc` and `cxx` always and
    // the others where the row has them; `detected` sets `cc` and `cxx` only
    // where detection cannot agree with the program (the Linux libc++ row).
    std::string cc, cxx, ld, ar, rc, mt;
    // `chain` on the MSVC ABI: the environment the tools run with, without
    // PATH (INCLUDE, LIB, LIBPATH, ...).
    std::vector<std::pair<std::string, std::string>> env;
    // `chain`: the tools' own directories, in order, without duplicates: the
    // PATH a foreign system needs for them.
    std::vector<std::string> path_dirs;
    std::string identity;           // toolset_identity(): "msvc 14.44.35207; sdk 10.0.26100.0", "clang 22.1.8"
    std::string crt;                // msvc_crt_linkage(): "static", "dynamic", or empty off the MSVC ABI
    bool        msvc_abi = false;
    // Why `resolved` answered `detected`: empty unless the resolved toolset
    // cannot be handed over by its tools alone.
    std::string reason;
};

inline bool msvc_abi() {
    return std::string_view(mcpp::target_os()) == "windows"
        && std::string_view(mcpp::target_env()) == "msvc";
}

inline bool host_is_windows() {
    return std::string_view(mcpp::host()).find("windows") != std::string_view::npos;
}

// `a;b` on a Windows host, `a:b` elsewhere.
inline char path_separator() { return host_is_windows() ? ';' : ':'; }

inline std::string forward(std::string_view p) {
    std::string s(p);
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == '\\') s[i] = '/';
    return s;
}

// The toolset version out of an identity "msvc 14.44.35207; sdk 10.0.26100.0".
inline std::string msvc_version_of(std::string_view identity) {
    if (!identity.starts_with("msvc ")) return {};
    identity.remove_prefix(5);
    const auto end = identity.find(';');
    return std::string(identity.substr(0, end));
}

// The toolset version a Visual Studio instance selects when none is named:
// `VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt`, which the
// Visual Studio installer writes and CMake and vcpkg both read.
inline std::string instance_default_version(const std::filesystem::path& instance) {
    std::ifstream in(instance / "VC" / "Auxiliary" / "Build" / "Microsoft.VCToolsVersion.default.txt");
    std::string v;
    std::getline(in, v);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\n')) v.pop_back();
    return v;
}

// tool_env() is one KEY=value per line.
inline std::vector<std::pair<std::string, std::string>> parse_env(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        out.emplace_back(std::string(line.substr(0, eq)), std::string(line.substr(eq + 1)));
    }
    return out;
}

inline bool same_key(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// The value of `key` in `env`; Windows environment names compare without case.
inline std::string env_value(const std::vector<std::pair<std::string, std::string>>& env,
                             std::string_view key) {
    for (auto const& [k, v] : env) if (same_key(k, key)) return v;
    return {};
}

namespace detail {
inline std::string tool_of(compiler which, const char* role) {
    return forward(which == compiler::abi_native ? mcpp::abi_tool(role) : mcpp::tool(role));
}

inline void add_dir(std::vector<std::string>& dirs, const std::string& file) {
    if (file.empty()) return;
    const std::string dir = std::filesystem::path(file).parent_path().generic_string();
    if (dir.empty()) return;
    for (auto const& d : dirs) if (same_key(d, dir)) return;
    dirs.push_back(dir);
}

inline std::expected<resolved_tools, std::string> chain(const choice& c, resolved_tools r) {
    r.how = mechanism::chain;
    r.cc  = tool_of(c.cc, "cc");
    r.cxx = tool_of(c.cc, "cxx");
    r.ld  = tool_of(c.cc, "ld");
    r.ar  = tool_of(c.cc, "ar");
    r.rc  = tool_of(c.cc, "rc");
    r.mt  = tool_of(c.cc, "mt");
    if (r.cxx.empty())
        return std::unexpected(std::format(
            "the resolved toolchain states no {} C++ compiler for target '{}'; the engine "
            "states the tools from mcpp 2026.9.28.3 onward.",
            c.cc == compiler::abi_native ? "ABI-native" : "row", std::string(mcpp::target())));
    if (r.cc.empty()) r.cc = r.cxx;
    if (r.msvc_abi) {
        // The environment WITHOUT its PATH. The engine's PATH is the cl.exe
        // directory followed by the whole PATH of the process that ran mcpp --
        // under Git Bash that holds Git's own msys tools and runtime, and a
        // make-based port configured against them failed (`configure: error:
        // invalid variable name: '0'`, icu, the plugins' CI). The tools'
        // own directories are the PATH a foreign system needs: the toolset's
        // bin (cl, link, lib) and the SDK's (rc, mt).
        for (auto const& kv : parse_env(mcpp::tool_env()))
            if (!same_key(kv.first, "PATH")) r.env.push_back(kv);
        for (auto const* t : { &r.cxx, &r.cc, &r.ld, &r.rc, &r.mt }) add_dir(r.path_dirs, *t);
    } else {
        for (auto const* t : { &r.cxx, &r.cc }) add_dir(r.path_dirs, *t);
    }
    return r;
}
} // namespace detail

// The tools a foreign build system is handed, and how.
inline std::expected<resolved_tools, std::string> resolve(const choice& c = {}) {
    resolved_tools r;
    r.identity = mcpp::toolset_identity();
    r.crt      = mcpp::msvc_crt_linkage();
    r.msvc_abi = msvc_abi();
    if (r.msvc_abi) r.toolset_version = msvc_version_of(r.identity);

    if (c.toolset == source::detected) {
        const auto kept = mcpp::plugins::compat::detected_toolset();
        // Where detection cannot agree with the program -- the host compiler
        // on Linux uses libstdc++ while the program uses libc++ -- 0.16.0
        // already named mcpp's clang, and so does this.
        if (kept.name_compilers) return detail::chain({source::resolved, compiler::row}, r);
        r.how = mechanism::detected;
        return r;
    }
    if (r.identity.empty())
        return std::unexpected(std::string(
            "the engine states no toolset identity: build information for build programs "
            "arrived in mcpp 2026.9.28.3 (protocol 14). Pin \"mcpp\": \"2026.9.28.3\" or newer "
            "in .xlings.json, or set the plugin's `toolset` option to `detected`."));

    // THE GCC PAYLOAD IS NOT A COMPLETE HANDOVER. mcpp runs it with a sysroot,
    // a binutils directory and a link model (`--sysroot`, `-B`, the payload's
    // dynamic linker and C library) that its own command lines add and a
    // foreign build system does not receive; given the driver alone, vcpkg's
    // compiler detection failed on CI while it passed on a machine whose host
    // compiler filled the gaps. The clang payloads carry their configuration in
    // their own `.cfg` files and are complete. On the GCC row the host
    // compiler's libstdc++ is the same C++ library, so the foreign system's
    // detection agrees with the program, as it did before 0.17.0.
    if (!r.msvc_abi && std::string_view(mcpp::compiler()) == "gcc") {
        r.how    = mechanism::detected;
        r.reason = "the GCC payload runs with a sysroot, binutils and a link model that only mcpp's "
                   "own command lines carry; the host compiler's libstdc++ is the program's C++ library";
        return r;
    }

    const std::string instance = forward(mcpp::msvc_instance_dir());
    if (r.msvc_abi && !instance.empty() && c.cc == compiler::abi_native) {
        r.how              = mechanism::instance;
        r.instance_dir     = instance;
        r.instance_default = instance_default_version(std::filesystem::path(instance));
        r.cc = r.cxx = detail::tool_of(compiler::abi_native, "cxx");
        return r;
    }
    return detail::chain(c, r);
}

// The tools named even where an instance exists: a plugin whose foreign
// system is driven without the instance's own loading (CMake's Ninja
// generator) asks for this. `detected` is answered as by `resolve`.
inline std::expected<resolved_tools, std::string> resolve_named(const choice& c = {}) {
    auto r = resolve(c);
    if (!r || r->how != mechanism::instance) return r;
    resolved_tools base;
    base.identity        = r->identity;
    base.crt             = r->crt;
    base.msvc_abi        = r->msvc_abi;
    base.toolset_version = r->toolset_version;
    return detail::chain(c, base);
}

// A one-line account of a resolution, for a plugin's own diagnostics and for
// the identity comment of a generated file.
inline std::string describe(const resolved_tools& r) {
    std::string s = std::format("{} ({})", r.identity.empty() ? std::string("unknown toolset") : r.identity,
                                name(r.how));
    if (r.how == mechanism::instance) s += std::format(", instance {}", r.instance_dir);
    if (!r.crt.empty()) s += std::format(", crt {}", r.crt);
    return s;
}

} // namespace mcpp::plugins::toolset
