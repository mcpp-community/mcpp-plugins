// mcpp.rules.swift -- how one package's Swift sources become one object file
// the package's images link, stated once.
//
// THE DIVISION OF LABOUR IS `mcpp.rules.metal`'S. The ENGINE owns the graph:
// `.swift` is a device extension this feature declares, so a `.swift` file the
// project names in `[build] sources` reaches this rule through
// `mcpp::device_sources()` instead of being refused, and every command below is
// an action with declared inputs and outputs. The RULE owns the spelling: which
// SDK, which compiler, which flags, and which runtime the link needs
// (mcpp-community/mcpp#647 E2).
//
// ONE MODULE, ONE OBJECT. Swift compiles a module, not a file: a declaration in
// one source is visible in every other source of the same module without an
// import. The package's sources are therefore compiled together, in one
// whole-module invocation (`-wmo`), into one object named after the package,
// through an action whose role is `object`, so the object joins the link of every
// image the package produces.
//
// A HEADER FOR THE C AND C++ SIDE. `swiftc -emit-objc-header-path` writes the
// declarations Swift exports to C and Objective-C (`@_cdecl` functions and
// `@objc` classes) as a header. It is produced by a second action whose role is
// `source`, so every compile edge of this package waits for it, and its directory
// is added with `mcpp::include_dir`, so the package's C++ sources include it as
// `#include "<module>-Swift.h"`. The header action type-checks and emits no
// object, which keeps the object action free of an output the link must not
// receive.
//
// C FROM SWIFT. `options::bridging_header` names a C header whose declarations
// Swift sees without an import (`-import-objc-header`). It is an input of both
// actions, so editing it recompiles the module.
//
// THE RUNTIME. A Swift object refers to the Swift runtime and records the
// libraries it needs as linker options inside the object; the link finds them
// through two search directories, the toolchain's `usr/lib/swift/<platform>` and
// the SDK's `usr/lib/swift`, which reach the link through `mcpp::link_flag`. The
// runtime itself is part of the operating system from macOS 10.14.4 and iOS 12.2,
// so the program's run path names `/usr/lib/swift`.
//
// THE TOOLCHAIN IS LOCATED, NOT INSTALLED. `swiftc` ships with Xcode and with the
// Command Line Tools, neither of which is redistributable, so no payload is
// declared. Before planning anything the rule asks `xcrun --sdk <sdk>
// --show-sdk-path` and `xcrun --sdk <sdk> --find swiftc`, and refuses naming the
// command that answered nothing. The commands run through `xcrun --sdk <sdk>`, so
// the compiler targets the SDK of the row.
//
// ROWS. macOS and the iOS rows (the simulator and a device). Every other row is
// refused by name: Swift on Linux and Windows is a different toolchain and a
// different runtime, neither of which this rule knows how to link.
//
// WHAT THIS RULE DOES NOT DO, stated so that its absence is not mistaken for a
// defect:
//   - A Swift `import` of ANOTHER package's Swift module. That needs the other
//     package's `.swiftmodule` directory on this package's search path, and a
//     package's compile-interface directories are private to it by design
//     (`mcpp::include_dir` colours the declaring package's own translation units
//     only). It waits for an engine channel that publishes such a directory to
//     dependents.
//   - A C++ translation unit of another package including this package's
//     generated header, for the same reason.
//   - Swift Package Manager dependencies. A SwiftPM package is not a package in
//     the mcpp graph.

module;
#include <cstdio>

export module mcpp.rules.swift;

import std;
import mcpp;
import mcpp.plugins;

// Nothing here uses `std::println`, for the reason `rules/metal.cppm` gives.

export namespace mcpp::rules::swift {

struct options {
    // The Swift module name. Empty means the package name, with every character
    // that is not a letter, a digit or `_` replaced by `_` (a module name is an
    // identifier; `swift-consumer` is not one).
    std::string module_name;
    // A C header Swift sees without an import (`-import-objc-header`). Relative
    // paths resolve against the package root. Empty passes none.
    std::string bridging_header;
    // `-D` for Swift's conditional compilation (`#if NAME`).
    std::vector<std::string> defines;
    // Further arguments for both `swiftc` invocations, after the rule's own.
    std::vector<std::string> flags;
    // `false` compiles without the header action and adds no include directory,
    // for a package whose C++ side declares the Swift functions itself.
    bool emit_header = true;
    // The SDK `xcrun` compiles against. Empty derives it from the target row:
    // `macosx`, `iphoneos` or `iphonesimulator`.
    std::string sdk;
    // The deployment target in the `-target` triple. Empty takes the row's
    // minimum platform version from the engine, and a default when the engine
    // states none (14.0 on macOS, 17.0 on iOS).
    std::string min_os_version;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── What the engine said ──────────────────────────────────────────────────

// The SDK a target row compiles against, or empty on a row Swift does not
// serve.
inline std::string sdk_for(const options& opt) {
    if (!opt.sdk.empty()) return opt.sdk;
    const std::string os = mcpp::target_os();
    if (os == "macos") return "macosx";
    if (os == "ios")
        return std::string(mcpp::target_env()) == "sim" ? "iphonesimulator" : "iphoneos";
    return {};
}

// The `-target` triple swiftc takes: Apple's architecture spelling, the
// platform, the deployment version, and `-simulator` on the simulator row.
inline std::string swift_target_for(const options& opt, const std::string& sdk) {
    std::string arch = mcpp::target_arch();
    if (arch == "aarch64" || arch.empty()) arch = "arm64";
    std::string version = opt.min_os_version;
    if (version.empty()) version = mcpp::min_platform_version();
    if (sdk == "macosx") {
        if (version.empty()) version = "14.0";
        return arch + "-apple-macosx" + version;
    }
    if (version.empty()) version = "17.0";
    return arch + "-apple-ios" + version + (sdk == "iphonesimulator" ? "-simulator" : "");
}

// The platform directory name under a toolchain's `usr/lib/swift/`.
inline std::string platform_dir_for(const std::string& sdk) {
    return sdk;   // `macosx`, `iphoneos`, `iphonesimulator`: the same spelling
}

inline std::string module_name_for(const options& opt) {
    std::string name = opt.module_name;
    if (name.empty()) {
        const char* pkg = mcpp::package_name();
        name = (pkg && *pkg) ? pkg : "swift_module";
    }
    // Indices rather than iterators: a string iterator's operators are
    // `always_inline` in libstdc++, and GCC 16 does not carry their bodies
    // through this module's interface to the build program that inlines this
    // function ("inlining failed ... function body not available", measured).
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!std::isalnum(c) && name[i] != '_') name[i] = '_';
    }
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0])))
        name.insert(0, 1, '_');
    return name;
}

// `mcpp::device_sources()` is the package's whole device set, one path per
// line, and a rule takes the extensions it claims (see `rules/spirv.cppm`).
inline std::vector<std::string> device_swift_sources() {
    std::vector<std::string> out;
    const std::string all = mcpp::device_sources();
    std::size_t i = 0;
    while (i <= all.size()) {
        auto nl = all.find('\n', i);
        std::string one = all.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? all.size() + 1 : nl + 1;
        while (!one.empty() && (one.back() == ' ' || one.back() == '\r')) one.pop_back();
        if (one.size() > 6 && one.substr(one.size() - 6) == ".swift") out.push_back(one);
    }
    return out;
}

// ─── The toolchain ─────────────────────────────────────────────────────────

// The first line a command prints, or empty. `popen` is POSIX; this module is
// compiled on every host (`tests/all-rules-compile`), so Windows is spelled.
inline std::string first_line_of(const std::string& cmd) {
#if defined(_WIN32)
    FILE* p = ::_popen(cmd.c_str(), "r");
#else
    FILE* p = ::popen(cmd.c_str(), "r");
#endif
    if (!p) return {};
    char buf[1024];
    std::string line;
    if (std::fgets(buf, sizeof buf, p)) line = buf;
#if defined(_WIN32)
    ::_pclose(p);
#else
    ::pclose(p);
#endif
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

inline std::string sdk_path(const std::string& sdk) {
#if defined(_WIN32)
    (void)sdk;
    return {};
#else
    const std::string path = first_line_of(
        "/usr/bin/xcrun --sdk " + sdk + " --show-sdk-path 2>/dev/null");
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec) ? path : std::string();
#endif
}

inline std::string find_swiftc(const std::string& sdk) {
#if defined(_WIN32)
    (void)sdk;
    return {};
#else
    const std::string path = first_line_of(
        "/usr/bin/xcrun --sdk " + sdk + " --find swiftc 2>/dev/null");
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec) ? path : std::string();
#endif
}

// ─── The rule ──────────────────────────────────────────────────────────────

// Compiles `sources` (absolute, or relative to the package root) as one Swift
// module. A build that names no `.swift` file has nothing to do, which is not a
// mistake: a project names the Swift sources on the rows that compile them.
inline bool compile(std::span<const std::string> sources, options opt = {}) {
    if (sources.empty()) return true;

    const std::string sdk = sdk_for(opt);
    if (sdk.empty()) {
        std::cerr << std::format(
            "mcpp.rules.swift: {} Swift source(s) reached this rule on a '{}' "
            "target; this rule compiles Swift for macOS and iOS only. Condition "
            "the sources on the row, e.g. [target.'cfg(any(os = \"macos\", os = \"ios\"))'.build] "
            "sources = [\"swift/*.swift\"].",
            sources.size(), std::string(mcpp::target_os())) << '\n';
        mcpp::warning("mcpp.rules.swift: Swift sources on a row this rule does not serve");
        return false;
    }
    const std::string sdkRoot = sdk_path(sdk);
    if (sdkRoot.empty()) {
        std::cerr << std::format(
            "mcpp.rules.swift: the {0} SDK was not found: `xcrun --sdk {0} "
            "--show-sdk-path` answered nothing. The macOS SDK ships with Xcode "
            "and with the Command Line Tools, the iOS SDKs with Xcode alone.",
            sdk) << '\n';
        return false;
    }
    const std::string swiftc = find_swiftc(sdk);
    if (swiftc.empty()) {
        std::cerr << std::format(
            "mcpp.rules.swift: no Swift compiler was found for the {0} SDK: "
            "`xcrun --sdk {0} --find swiftc` answered nothing. It ships with "
            "Xcode and with the Command Line Tools.", sdk) << '\n';
        return false;
    }
    // `<toolchain>/usr/bin/swiftc`: the toolchain root is three components up.
    const auto toolchainRoot = std::filesystem::path(swiftc).parent_path().parent_path().parent_path();

    const std::string root = mcpp::manifest_dir();
    const auto resolve = [&](const std::string& p) {
        return std::filesystem::path(p).is_absolute() ? p : root + "/" + p;
    };
    std::vector<std::string> absSources;
    for (auto const& s : sources) absSources.push_back(resolve(s));
    std::string bridging;
    if (!opt.bridging_header.empty()) {
        bridging = resolve(opt.bridging_header);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(bridging, ec)) {
            std::cerr << std::format(
                "mcpp.rules.swift: `options::bridging_header` ({}) was not found", bridging) << '\n';
            return false;
        }
    }

    const std::string module = module_name_for(opt);
    const std::string target = swift_target_for(opt, sdk);
    const auto gen = std::filesystem::path(opt.out_dir) / "swift" / module;
    std::error_code ec;
    std::filesystem::create_directories(gen, ec);

    // The arguments both invocations share.
    const auto common = [&](mcpp::action& a) {
        a.arg("/usr/bin/xcrun").arg("--sdk").arg(sdk.c_str()).arg("swiftc");
        a.arg("-target").arg(target.c_str());
        a.arg("-module-name").arg(module.c_str());
        a.arg("-parse-as-library");
        for (auto const& d : opt.defines) a.arg(("-D" + d).c_str());
        if (!bridging.empty()) {
            a.arg("-import-objc-header").arg(bridging.c_str());
            a.input(bridging.c_str());
        }
        for (auto const& f : opt.flags) a.arg(f.c_str());
        for (auto const& s : absSources) { a.arg(s.c_str()); a.input(s.c_str()); }
    };

    if (opt.emit_header) {
        const std::string header = (gen / (module + "-Swift.h")).string();
        const std::string id   = "swift-header:" + module;
        const std::string desc = "SWIFT HEADER " + module;
        mcpp::action h;
        h.id          = id.c_str();
        h.role        = "source";     // a header: ordered before compilation
        h.description = desc.c_str();
        common(h);
        h.arg("-typecheck").arg("-emit-objc-header-path").arg(header.c_str());
        h.output(header.c_str());
        h.submit();
        mcpp::include_dir(gen.string().c_str());
    }

    const std::string object = (gen / (module + ".o")).string();
    {
        const std::string id   = "swift-object:" + module;
        const std::string desc = "SWIFTC " + module;
        mcpp::action o;
        o.id          = id.c_str();
        o.role        = "object";     // joins the link of every image of the package
        o.description = desc.c_str();
        common(o);
        o.arg("-wmo").arg("-emit-object").arg("-o").arg(object.c_str());
        o.output(object.c_str());
        o.submit();
    }

    const std::string platform = platform_dir_for(sdk);
    mcpp::link_flag(("-L" + (toolchainRoot / "usr" / "lib" / "swift" / platform).string()).c_str());
    mcpp::link_flag(("-L" + (std::filesystem::path(sdkRoot) / "usr" / "lib" / "swift").string()).c_str());
    mcpp::link_flag("-Wl,-rpath,/usr/lib/swift");

    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());
    return true;
}

// The `.swift` files the project named in `[build] sources`, as one module.
inline bool compile(options opt = {}) {
    const auto list = device_swift_sources();
    return compile(std::span<const std::string>(list), std::move(opt));
}

} // namespace mcpp::rules::swift
