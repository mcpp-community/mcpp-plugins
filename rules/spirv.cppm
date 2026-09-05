// mcpp.rules.spirv — how a GLSL translation unit becomes SPIR-V, stated once.
//
// THE MODULE NAME IS `mcpp.rules.<x>` AND THE PACKAGE NAMESPACE IS `mcpp`.
// Both halves are the rule-package specification, not a preference. The module
// name is declared by this source rather than derived from the package name
// (spec I1), and `mcpp.*` is reserved for rules the mcpp project maintains
// (I8) -- which is enforced as a warning keyed on the package NAMESPACE, so a
// rule carrying this module name under any other namespace is told that it
// claims an origin it does not have.
//
// The division of labour is the one `mcpp.rules.cuda` established, and it is the
// point of both packages: the ENGINE owns the graph — the accelerator axis,
// the constrained source globs that route `shaders/*.comp` here instead of to
// the C++ compiler, the action edges and their ordering, the fingerprint — and
// does not know the word "vulkan" or the word "glslang". The RULE owns the
// spelling: which compiler, which flags, what the generated symbol is called.
//
// WHAT COMES OUT IS A HEADER, NOT AN OBJECT.
//
// A SPIR-V module is data the program hands to `vkCreateShaderModule`, not
// code the linker places. Two shapes are possible: a `.spv` file beside the
// binary, which makes the program's correctness depend on its working
// directory, or a C array compiled into it. This rule emits the second, so the
// artifact carries its shaders and a `mcpp pack` of it has nothing further to
// collect.
//
// `role = "source"` is what makes that work. It is the one role the engine
// orders BEFORE compilation (`action_precedes_compilation`), which is exactly
// what a generated header needs and exactly what an `artifact` role would not
// give: an artifact output is ordered against the LINK, and the header has to
// exist before the first translation unit that includes it is compiled.
//
// GLSLANG, AND ONLY GLSLANG, DELIBERATELY.
//
// `glslc` (shaderc) is the other reference compiler and is not supported here.
// Not for a reason of principle — this rule would take it — but because
// nothing in this ecosystem publishes it, and a route with no payload behind
// it is a claim rather than a feature. `xim:glslang` exists and is what the
// example installs. If glslc is ever packaged, `-mfmt=c` emits a bare
// initialiser list where glslang's `-x --vn` emits a complete declaration, so
// the two produce different headers and the rule would have to say which.
module;
#include <cctype>
#include <cstdio>

export module mcpp.rules.spirv;

import std;
import mcpp;

export namespace mcpp::rules::spirv {

struct options {
    // The Vulkan environment the SPIR-V targets. Left empty, it is taken from
    // the accelerator axis — `accel = "vulkan1.2"` in the manifest — which is
    // the same route `mcpp.rules.cuda` takes for `sm_89`, and the reason neither
    // rule needs a second place to state what the build is for.
    std::string target_env;
    // `-I` for GLSL `#include`, `-D` for its preprocessor. Relative entries
    // resolve against the package root; an absolute entry is passed through.
    std::vector<std::string> includes;
    std::vector<std::string> defines;
    // glslang's optimiser (`-Os`), which is spirv-opt linked into it.
    bool optimize = true;
    // An explicit compiler path wins over discovery. Set it when a project
    // pins a glslang other than the one the workspace installed.
    std::string compiler;
    std::string out_dir = std::string(mcpp::out_dir());
};

// Where the generated headers are written, and what the build program passes
// to `mcpp::include_dir` so `#include "scale_comp.h"` resolves.
inline std::string include_dir(const options& opt) {
    return (std::filesystem::path(opt.out_dir) / "spirv").string();
}

// ─── What the engine said ──────────────────────────────────────────────────

// The `vulkan` chunk of `mcpp::accel()`. Unlike CUDA's, it carries no
// architecture set: SPIR-V is the portable form, and which GPU executes it is
// decided when the driver compiles it, not here. A rule that demanded an
// architecture would be inventing a requirement its device API does not have.
struct target {
    std::string version;      // "1.2", from `vulkan1.2`
    bool present = false;
};

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t')) s.remove_suffix(1);
    return s;
}

inline target parse_target(std::string_view accel) {
    target t;
    for (std::size_t i = 0; i <= accel.size();) {
        auto comma = accel.find(',', i);
        auto chunk = trim(comma == std::string_view::npos ? accel.substr(i)
                                                          : accel.substr(i, comma - i));
        i = comma == std::string_view::npos ? accel.size() + 1 : comma + 1;
        if (!chunk.starts_with("vulkan")) continue;
        t.present = true;
        auto rest = chunk.substr(std::string_view("vulkan").size());
        // A bare `vulkan` is legitimate and means "whatever the loader offers";
        // the target environment then falls back to the default below.
        auto plus = rest.find('+');
        t.version = std::string(trim(plus == std::string_view::npos ? rest
                                                                   : rest.substr(0, plus)));
    }
    return t;
}

// ─── The compiler ──────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Discovery, in the order a project can predict: what it named, what the
// environment named, the payload the workspace installed, then the PATH. The
// PATH comes last on purpose — a host glslang is a fine fallback and a poor
// default, because it makes the SPIR-V depend on a machine rather than on a
// declaration.
inline std::string find_compiler(const options& opt) {
    if (!opt.compiler.empty()) return opt.compiler;
    if (const char* e = std::getenv("MCPP_GLSLANG"); e && *e) return e;

    if (const char* dir = mcpp::xpkg_dir("glslang"); dir && *dir) {
        for (const char* exe : {"glslangValidator", "glslang"}) {
            auto p = (std::filesystem::path(dir) / "bin" / exe).string();
            if (is_file(p)) return p;
        }
    }
    for (const char* exe : {"glslangValidator", "glslang"}) {
        if (const char* path = std::getenv("PATH"); path && *path) {
            std::string_view sv(path);
            for (std::size_t i = 0; i <= sv.size();) {
                auto sep = sv.find(':', i);
                auto dir = sv.substr(i, sep == std::string_view::npos ? sv.size() - i : sep - i);
                i = sep == std::string_view::npos ? sv.size() + 1 : sep + 1;
                if (dir.empty()) continue;
                auto p = (std::filesystem::path(dir) / exe).string();
                if (is_file(p)) return p;
            }
        }
    }
    return {};
}

// `Glslang Version: 11:15.1.0` — the first field is the SPIR-V generator
// magic, the second is the release. The release is what a floor compares, and
// stating it as a fact is what makes a build log answer "which compiler
// produced this SPIR-V" without anyone having to reproduce the build.
inline std::string run_and_capture(const std::string& cmd) {
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return {};
    std::string text;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) text += buf;
    ::pclose(p);
    return text;
}

// ⚠️ THE OPTIMISER IS OPTIONAL AND ITS ABSENCE IS NOT A BUILD ERROR.
//
// glslang links spirv-opt only when built with `ENABLE_OPT`, and the payload
// this ecosystem publishes today is not:
//
//   glslangValidator: Error: -Os not available; optimizer not linked
//
// Passing `-Os` to such a binary fails the compile. A shader that is
// unoptimised is still a correct shader, so the flag is dropped and the build
// says so once — refusing would make an optional pass a requirement, and
// passing it silently would let the manifest claim an optimisation that did
// not happen.
inline bool has_optimizer(const std::string& exe) {
    const auto out = run_and_capture("\"" + exe + "\" -Os --version 2>&1");
    return out.find("optimizer not linked") == std::string::npos;
}

inline std::string compiler_version(const std::string& exe) {
    const std::string text = run_and_capture("\"" + exe + "\" --version 2>/dev/null");
    for (std::size_t i = 0; i <= text.size();) {
        auto nl = text.find('\n', i);
        std::string_view line(text.data() + i,
                              (nl == std::string::npos ? text.size() : nl) - i);
        i = nl == std::string::npos ? text.size() + 1 : nl + 1;
        auto at = line.find("Glslang Version:");
        if (at == std::string_view::npos) continue;
        auto rest = trim(line.substr(at + std::string_view("Glslang Version:").size()));
        auto colon = rest.find(':');
        if (colon != std::string_view::npos) rest = rest.substr(colon + 1);
        while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r'))
            rest.remove_suffix(1);
        return std::string(trim(rest));
    }
    return {};
}

// ─── Shaders ───────────────────────────────────────────────────────────────

// glslang infers the stage from the extension, and so does this table — but
// the table is consulted rather than trusted: an extension that is not a stage
// is refused by name instead of being handed to a compiler that will refuse it
// with a less specific message.
inline std::string_view stage_of(std::string_view ext) {
    if (ext == ".comp") return "comp";
    if (ext == ".vert") return "vert";
    if (ext == ".frag") return "frag";
    if (ext == ".geom") return "geom";
    if (ext == ".tesc") return "tesc";
    if (ext == ".tese") return "tese";
    if (ext == ".mesh") return "mesh";
    if (ext == ".task") return "task";
    if (ext == ".rgen") return "rgen";
    if (ext == ".rint") return "rint";
    if (ext == ".rahit") return "rahit";
    if (ext == ".rchit") return "rchit";
    if (ext == ".rmiss") return "rmiss";
    if (ext == ".rcall") return "rcall";
    return {};
}

// `shaders/scale.comp` -> `scale_comp_spv`, and the header that declares it is
// `scale_comp.h`. Derived rather than configurable: a name a project chooses
// per shader is a name the project has to keep in agreement with its own
// `#include`, and this rule already decides the file name.
inline std::string symbol_of(std::string_view stem, std::string_view stage) {
    std::string s;
    for (char c : stem)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    s += '_';
    s += stage;
    s += "_spv";
    return s;
}

// ⚠️ NEWLINE-SEPARATED, not `;`. A path may contain a semicolon and cannot
// contain a newline, which is why the engine chose it — and why a splitter
// that guesses wrong still works for exactly one shader and silently produces
// one impossible path for two.
inline std::vector<std::string> device_shaders() {
    std::vector<std::string> out;
    std::string_view all(mcpp::device_sources());
    for (std::size_t i = 0; i <= all.size();) {
        auto sep = all.find('\n', i);
        auto one = trim(all.substr(i, sep == std::string_view::npos ? all.size() - i : sep - i));
        i = sep == std::string_view::npos ? all.size() + 1 : sep + 1;
        if (!one.empty()) out.emplace_back(one);
    }
    return out;
}

// ─── The rule ──────────────────────────────────────────────────────────────

inline bool compile(std::span<const std::string> shaders, options opt = {}) {
    if (shaders.empty()) return true;

    const auto exe = find_compiler(opt);
    if (exe.empty()) {
        std::println(stderr,
            "mcpp.rules.spirv: no glslang found. Install one into the workspace\n"
            "  [xlings.workspace]\n"
            "  \"xim:glslang\" = \"15.1.0\"\n"
            "or name it: MCPP_GLSLANG=/path/to/glslangValidator, or set "
            "options::compiler.");
        return false;
    }
    if (const auto v = compiler_version(exe); !v.empty())
        mcpp::fact("glslang", v.c_str());

    bool optimize = opt.optimize;
    if (optimize && !has_optimizer(exe)) {
        mcpp::warning("mcpp.rules.spirv: this glslang was built without spirv-opt "
                      "(-Os not available; optimizer not linked); shaders are "
                      "compiled unoptimised");
        optimize = false;
    }

    auto env = opt.target_env;
    if (env.empty()) {
        const auto t = parse_target(mcpp::accel());
        env = t.version.empty() ? "vulkan1.0" : "vulkan" + t.version;
    }

    const std::string root = mcpp::manifest_dir();
    const auto gen = include_dir(opt);
    std::error_code ec;
    std::filesystem::create_directories(gen, ec);

    for (auto const& src : shaders) {
        const std::filesystem::path p(src);
        const auto stage = stage_of(p.extension().string());
        if (stage.empty()) {
            std::println(stderr,
                "mcpp.rules.spirv: {} has no shader stage. glslang derives the stage from "
                "the extension; rename it to one of .comp .vert .frag .geom .tesc "
                ".tese .mesh .task .rgen .rint .rahit .rchit .rmiss .rcall", src);
            return false;
        }
        const auto sym    = symbol_of(p.stem().string(), stage);
        const auto header = (std::filesystem::path(gen)
                             / (p.stem().string() + "_" + std::string(stage) + ".h")).string();
        const auto input  = std::filesystem::path(src).is_absolute()
                          ? src : root + "/" + src;

        // ⚠️ `id` and `description` are raw pointers the action reads at
        // `submit()`; `arg`/`input`/`output` copy, these two do not. Held in
        // named strings for the life of the statement that submits.
        const std::string id   = "spirv:" + src;
        const std::string desc = "glslang " + src;

        mcpp::action a;
        a.id          = id.c_str();
        a.role        = "source";     // a header: ordered before compilation
        a.description = desc.c_str();
        a.arg(exe.c_str());
        a.arg("-V");
        a.arg("--target-env"); a.arg(env.c_str());
        a.arg("-S"); a.arg(std::string(stage).c_str());
        if (optimize) a.arg("-Os");
        for (auto const& d : opt.defines) a.arg(("-D" + d).c_str());
        for (auto const& i : opt.includes)
            a.arg(("-I" + (std::filesystem::path(i).is_absolute() ? i : root + "/" + i)).c_str());
        // `-x --vn` is what makes the output a C declaration rather than a
        // binary: a `const uint32_t <sym>[]` the program includes.
        a.arg("-x");
        a.arg("--vn"); a.arg(sym.c_str());
        a.arg("-o"); a.arg(header.c_str());
        a.arg(input.c_str());
        a.input(input.c_str());
        a.output(header.c_str());
        a.submit();
    }

    mcpp::include_dir(gen.c_str());
    return true;
}

// The whole manifest's worth: the shaders the constrained glob routed here.
// A build that names no accelerator has none, and the seam's CPU side carries
// the program — the same shape `mcpp::rules::cuda::compile()` has, for the same
// reason.
inline bool compile(options opt = {}) {
    if (!*mcpp::accel()) return true;
    const auto shaders = device_shaders();
    if (shaders.empty()) {
        mcpp::warning("[build] accel names vulkan but no constrained glob matched a "
                      "shader; nothing was compiled for it");
        return true;
    }
    return compile(std::span<const std::string>(shaders), std::move(opt));
}

} // namespace mcpp::rules::spirv
