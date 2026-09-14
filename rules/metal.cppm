// mcpp.rules.metal -- how a Metal shader becomes a Metal library, stated once.
//
// THE DIVISION OF LABOUR IS `mcpp.rules.spirv`'S. The ENGINE owns the graph:
// `.metal` is a device extension this feature declares, so a `.metal` file the
// project names in `[build] sources` reaches this rule through
// `mcpp::device_sources()` instead of being refused, and every command below is
// an action with declared inputs and outputs. The RULE owns the spelling: which
// SDK, which two tools, which flags, and where the library is placed.
//
// TWO TOOLS AND ONE INTERMEDIATE. `metal` compiles a shader to Apple's
// intermediate representation (`.air`), and `metallib` links one or more of
// those into a Metal library (`.metallib`), the file an application loads with
// `newLibraryWithURL:` or, named `default.metallib` in its bundle's resources,
// with `newDefaultLibrary`. Each is one action, so an edited shader recompiles
// its own `.air` and relinks only the libraries that contain it.
//
// THE TOOLCHAIN IS LOCATED, NOT INSTALLED. Both tools ship with Xcode, which is
// not redistributable, so no payload is declared. Before planning anything the
// rule asks `xcrun --sdk <sdk> --show-sdk-path`, then `xcrun --sdk <sdk> --find
// metal` and `--find metallib`, and refuses naming the command that answered
// nothing: a missing SDK and a missing compiler are different remedies. Xcode
// 26 installs the Metal toolchain as a separate component (`xcodebuild
// -downloadComponent MetalToolchain`), which is the absence a machine with
// Xcode and its SDKs can still have.
// The commands themselves run through `xcrun --sdk <sdk>`, because `xcrun`
// sets the SDK the compiler targets; an absolute tool path would compile for
// macOS whatever the row is.
//
// THE LIBRARY IS A FILE BESIDE THE PROGRAM. It is placed with `mcpp::deploy`
// under `options::deploy_to` (`metallib/` by default), relative to the
// program's directory, which is where `dist-apple` finds a deployed file and
// maps it under the bundle's resource directory.
//
// THE SAME SOURCE MAY PRODUCE SEVERAL LIBRARIES. A renderer that compiles one
// fragment shader once per blend mode passes one `shader` per output, each with
// its own definitions and name; the sources need not be in the project's tree.

module;
#include <cstdio>

export module mcpp.rules.metal;

import std;
import mcpp;
import mcpp.plugins;

// Nothing here uses `std::println`: both of its overloads reach into the libc++
// dylib for symbols macOS 14 does not ship, so a build program that printed
// with it compiled and then failed to link. The measurement is in
// `rules/spirv.cppm`.

export namespace mcpp::rules::metal {

struct options {
    // `-D` for the Metal preprocessor and `-I` for `#include`, applied to every
    // shader. Relative include directories resolve against the package root.
    std::vector<std::string> defines;
    std::vector<std::string> includes;
    // `-std=<value>` (`metal3.1`, `ios-metal2.4`). Empty lets the compiler take
    // the default of the SDK it compiles against.
    std::string language_standard;
    // Where the libraries are placed, relative to the program's directory:
    // `mcpp::deploy`'s destination.
    std::string deploy_to = "metallib";
    // Empty compiles one library per shader, named after the shader. A name
    // links every shader into one library `<library>.metallib`; `default` gives
    // the library `newDefaultLibrary` finds in a bundle's resources.
    std::string library;
    // The SDK `xcrun` compiles against. Empty derives it from the target row:
    // `macosx`, `iphoneos` or `iphonesimulator`.
    std::string sdk;
    std::string out_dir = std::string(mcpp::out_dir());
};

// One compilation of a shader.
struct shader {
    // Absolute, or relative to the package root.
    std::string source;
    // The stem of the library it produces. Empty means the source's stem.
    std::string name;
    // Definitions for this compilation alone, after `options::defines`.
    std::vector<std::string> defines;
};

// ─── What the engine said ──────────────────────────────────────────────────

// The SDK a target row compiles against, or empty on a row Metal does not
// serve.
inline std::string sdk_for(const options& opt) {
    if (!opt.sdk.empty()) return opt.sdk;
    const std::string os = mcpp::target_os();
    if (os == "macos") return "macosx";
    if (os == "ios")
        return std::string(mcpp::target_env()) == "sim" ? "iphonesimulator" : "iphoneos";
    return {};
}

// `mcpp::device_sources()` is the package's whole device set, one path per
// line, and a rule takes the extensions it claims (see `rules/spirv.cppm`).
inline std::vector<std::string> device_shaders() {
    std::vector<std::string> out;
    const std::string all = mcpp::device_sources();
    std::size_t i = 0;
    while (i <= all.size()) {
        auto nl = all.find('\n', i);
        std::string one = all.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? all.size() + 1 : nl + 1;
        while (!one.empty() && (one.back() == ' ' || one.back() == '\r')) one.pop_back();
        if (one.size() > 6 && one.substr(one.size() - 6) == ".metal") out.push_back(one);
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

// The SDK's path, or empty when `xcrun` cannot locate it.
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

inline std::string find_tool(const std::string& sdk, const char* tool) {
#if defined(_WIN32)
    (void)sdk; (void)tool;
    return {};
#else
    const std::string path = first_line_of(
        "/usr/bin/xcrun --sdk " + sdk + " --find " + tool + " 2>/dev/null");
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec) ? path : std::string();
#endif
}

inline std::string stem_of(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// ─── The rule ──────────────────────────────────────────────────────────────

inline bool compile(std::span<const shader> shaders, options opt = {}) {
    if (shaders.empty()) return true;

    const std::string sdk = sdk_for(opt);
    if (sdk.empty()) {
        std::cerr << std::format(
            "mcpp.rules.metal: {} Metal shader(s) reached this rule on a '{}' "
            "target; Metal compiles for macOS and iOS only. Condition the "
            "shaders on the row, e.g. [target.'cfg(any(os = \"macos\", os = \"ios\"))'.build] "
            "sources = [\"shaders/*.metal\"].",
            shaders.size(), std::string(mcpp::target_os())) << '\n';
        return false;
    }
    if (sdk_path(sdk).empty()) {
        std::cerr << std::format(
            "mcpp.rules.metal: the {0} SDK was not found: `xcrun --sdk {0} "
            "--show-sdk-path` answered nothing. The macOS SDK ships with Xcode "
            "and with the Command Line Tools, the iOS SDKs with Xcode alone.",
            sdk) << '\n';
        return false;
    }
    const std::string metalTool = find_tool(sdk, "metal");
    const std::string metallibTool = find_tool(sdk, "metallib");
    if (metalTool.empty() || metallibTool.empty()) {
        std::cerr << std::format(
            "mcpp.rules.metal: the Metal toolchain was not found for the {0} SDK: "
            "`xcrun --sdk {0} --find {1}` answered nothing. It ships with Xcode, "
            "and Xcode 26 installs it as a separate component: "
            "`xcodebuild -downloadComponent MetalToolchain`.",
            sdk, metalTool.empty() ? "metal" : "metallib") << '\n';
        return false;
    }

    const std::string root = mcpp::manifest_dir();
    const auto gen = std::filesystem::path(opt.out_dir) / "metal";
    std::error_code ec;
    std::filesystem::create_directories(gen, ec);

    // Two compilations with one output name would be two actions writing one
    // file; refused by naming both, as `rules/spirv.cppm` refuses two shaders
    // mapping to one header.
    {
        std::map<std::string, std::string> seen;
        for (auto const& s : shaders) {
            const std::string name = s.name.empty() ? stem_of(s.source) : s.name;
            auto [it, fresh] = seen.try_emplace(name, s.source);
            if (!fresh) {
                std::cerr << std::format(
                    "mcpp.rules.metal: two compilations produce `{}.air`: {} and {}. "
                    "Give one of them a `shader::name`.", name, it->second, s.source) << '\n';
                return false;
            }
        }
    }

    std::vector<std::string> airs;
    for (auto const& s : shaders) {
        const std::string source = std::filesystem::path(s.source).is_absolute()
            ? s.source : root + "/" + s.source;
        const std::string name = s.name.empty() ? stem_of(s.source) : s.name;
        const std::string air  = (gen / (name + ".air")).string();
        const std::string dep  = air + ".d";

        const std::string id   = "metal:" + name;
        const std::string desc = "METAL " + name;
        mcpp::action a;
        a.id          = id.c_str();
        a.role        = "source";
        a.description = desc.c_str();
        a.arg("/usr/bin/xcrun").arg("--sdk").arg(sdk.c_str()).arg("metal");
        if (!opt.language_standard.empty())
            a.arg(("-std=" + opt.language_standard).c_str());
        for (auto const& d : opt.defines) a.arg(("-D" + d).c_str());
        for (auto const& d : s.defines)   a.arg(("-D" + d).c_str());
        for (auto const& i : opt.includes)
            a.arg(("-I" + (std::filesystem::path(i).is_absolute() ? i : root + "/" + i)).c_str());
        // WHAT THE SHADER `#include`s, which only the compiler knows. The
        // `metal` driver is clang's and writes a Makefile-style dependency
        // file as clang does.
        a.arg("-MMD").arg("-MF").arg(dep.c_str());
        a.depfile = dep.c_str();
        a.arg("-c").arg(source.c_str()).arg("-o").arg(air.c_str());
        a.input(source.c_str());
        a.output(air.c_str());
        a.submit();
        airs.push_back(air);
    }

    auto link = [&](const std::string& name, std::span<const std::string> inputs) {
        const std::string lib  = (gen / (name + ".metallib")).string();
        const std::string id   = "metallib:" + name;
        const std::string desc = "METALLIB " + name;
        mcpp::action a;
        a.id          = id.c_str();
        a.role        = "source";
        a.description = desc.c_str();
        a.arg("/usr/bin/xcrun").arg("--sdk").arg(sdk.c_str()).arg("metallib");
        for (auto const& in : inputs) { a.arg(in.c_str()); a.input(in.c_str()); }
        a.arg("-o").arg(lib.c_str());
        a.output(lib.c_str());
        a.submit();
        mcpp::deploy(lib.c_str(), opt.deploy_to.c_str());
    };
    if (opt.library.empty()) {
        for (std::size_t i = 0; i < shaders.size(); ++i) {
            const std::string name = shaders[i].name.empty() ? stem_of(shaders[i].source)
                                                             : shaders[i].name;
            link(name, std::span<const std::string>(&airs[i], 1));
        }
    } else {
        link(opt.library, airs);
    }

    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());
    return true;
}

// The shaders the project named in `[build] sources`, one library each unless
// `options::library` names one for all of them. A build whose sources name no
// `.metal` file has nothing to do, which is not a mistake: a project names the
// shaders on the rows that compile them.
inline bool compile(options opt = {}) {
    std::vector<shader> list;
    for (auto const& path : device_shaders()) list.push_back({ path, {}, {} });
    return compile(std::span<const shader>(list), std::move(opt));
}

} // namespace mcpp::rules::metal
