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
// TWO COMPILERS, AND THE RULE STATES WHICH ONE IT USED.
//
// glslang and glslc are the two reference GLSL compilers and their command
// lines are not interchangeable. glslang's `-x --vn <name>` emits a COMPLETE C
// declaration -- `const uint32_t <name>[] = { ... };` -- while glslc's
// `-mfmt=c` emits a BARE INITIALISER LIST, `{ ... }`, which is not a
// translation unit on its own. A rule that takes both therefore has to write
// the declaration around glslc's output itself, which `wrap_glslc_output`
// below does.
//
// An earlier revision of this file supported glslang alone, and said why:
// nothing in this ecosystem published glslc, "and a route with no payload
// behind it is a claim rather than a feature". `xim:shaderc` now publishes it,
// so the route exists. Neither is a default over the other. Discovery takes
// what the project named, then what the environment named, then whichever
// payload the workspace installed, and the build log states which it found --
// two compilers that produce an equivalent header from one shader are still
// two different answers to "what compiled this".

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

// Split on one character. Written out rather than taken from <ranges> for the
// reason `mcpp.rules.cuda` records: GCC 16 refuses the ranges split view
// instantiated inside an exported inline function when build.mcpp imports the
// module, and clang does not.
inline std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i <= s.size();) {
        auto j = s.find(sep, i);
        out.push_back(s.substr(i, j == std::string_view::npos ? s.size() - i : j - i));
        if (j == std::string_view::npos) break;
        i = j + 1;
    }
    return out;
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
// WHICH COMPILER, AND WHICH OF THE TWO IT IS.
//
// The flavour is decided by the program's own name rather than by asking it,
// because the answer is needed before any flag can be chosen and the two
// disagree about almost every flag. A path a project or the environment names
// is classified the same way, so `MCPP_GLSLC=/opt/bin/glslc` needs no second
// variable to say what it is.
enum class flavour { none, glslang, glslc };

struct compiler {
    std::string path;
    flavour     kind = flavour::none;
    // Set when discovery already said why it failed, so the caller does not
    // follow a precise message with a generic one that contradicts it.
    bool        reported = false;
    explicit operator bool() const { return kind != flavour::none && !path.empty(); }
    const char* name() const { return kind == flavour::glslc ? "glslc" : "glslang"; }
};

inline flavour classify(const std::string& path) {
    const auto stem = std::filesystem::path(path).stem().string();
    if (stem == "glslc") return flavour::glslc;
    if (stem == "glslang" || stem == "glslangValidator") return flavour::glslang;
    return flavour::none;
}

// HOW THIS HOST SPELLS A PROGRAM, decided where the build program is compiled.
//
// The build program runs on the machine doing the building, so these are
// properties of the HOST and not of the target being compiled for -- a cross
// build from Linux to Windows still looks for `glslc`, because that is the
// binary about to be executed.
#if defined(_WIN32)
inline constexpr std::string_view kExeSuffix = ".exe";
inline constexpr char             kPathSep   = ';';
#else
inline constexpr std::string_view kExeSuffix = "";
inline constexpr char             kPathSep   = ':';
#endif

// The first of `<dir>/<name>` and `<dir>/<name>.exe` that exists. Both are
// tried on every host rather than only the one whose suffix matches: a payload
// repacked with the other convention is then found instead of silently missed,
// and the cost is one `stat`.
inline std::string program_in(const std::filesystem::path& dir, std::string_view name) {
    std::string bare(name);
    for (auto const& n : { bare, bare + std::string(kExeSuffix) }) {
        auto p = (dir / n).string();
        if (is_file(p)) return p;
    }
    return {};
}

inline std::string first_on_path(const char* exe) {
    const char* path = std::getenv("PATH");
    if (!path || !*path) return {};
    std::string_view sv(path);
    for (std::size_t i = 0; i <= sv.size();) {
        auto sep = sv.find(kPathSep, i);
        auto dir = sv.substr(i, sep == std::string_view::npos ? sv.size() - i : sep - i);
        i = sep == std::string_view::npos ? sv.size() + 1 : sep + 1;
        if (dir.empty()) continue;
        if (auto p = program_in(std::filesystem::path(dir), exe); !p.empty()) return p;
    }
    return {};
}

// Discovery, in the order a project can predict: what it named, what the
// environment named, the payload the workspace installed, then the PATH. The
// PATH comes last on purpose -- a host shader compiler is a fine fallback and
// a poor default, because it makes the SPIR-V depend on a machine rather than
// on a declaration.
inline compiler find_compiler(const options& opt) {
    if (!opt.compiler.empty()) {
        auto k = classify(opt.compiler);
        if (k == flavour::none) {
            // Named but unrecognised: taking it as glslang would pass glslang's
            // flags to something that is not glslang, and the error would name
            // a flag rather than this decision.
            std::println(stderr,
                "mcpp.rules.spirv: options::compiler names '{}', which is neither glslang\n"
                "  nor glslc by program name, and the two share almost no flags. Rename the\n"
                "  program or point at the real one.", opt.compiler);
            return { .reported = true };
        }
        return { opt.compiler, k };
    }
    if (const char* e = std::getenv("MCPP_GLSLC");   e && *e) return { e, flavour::glslc };
    if (const char* e = std::getenv("MCPP_GLSLANG"); e && *e) return { e, flavour::glslang };

    if (const char* dir = mcpp::xpkg_dir("glslang"); dir && *dir)
        for (const char* exe : {"glslangValidator", "glslang"})
            if (auto p = program_in(std::filesystem::path(dir) / "bin", exe); !p.empty())
                return { p, flavour::glslang };
    if (const char* dir = mcpp::xpkg_dir("shaderc"); dir && *dir)
        if (auto p = program_in(std::filesystem::path(dir) / "bin", "glslc"); !p.empty())
            return { p, flavour::glslc };

    for (const char* exe : {"glslangValidator", "glslang"})
        if (auto p = first_on_path(exe); !p.empty()) return { p, flavour::glslang };
    if (auto p = first_on_path("glslc"); !p.empty()) return { p, flavour::glslc };
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

// THE OPTIMISER IS OPTIONAL AND ITS ABSENCE IS NOT A BUILD ERROR.
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

inline std::string compiler_version(const compiler& cc) {
    const std::string text = run_and_capture("\"" + cc.path + "\" --version 2>/dev/null");
    // glslc: `shaderc v2026.3 2fbab05...` on the first line. glslang:
    // `Glslang Version: 11:15.1.0`, whose first field is the SPIR-V generator
    // magic and whose second is the release.
    const std::string_view key = cc.kind == flavour::glslc ? "shaderc v" : "Glslang Version:";
    for (auto line : split(text, '\n')) {
        auto at = line.find(key);
        if (at == std::string_view::npos) continue;
        auto rest = trim(line.substr(at + key.size()));
        if (cc.kind == flavour::glslc) {
            auto sp = rest.find(' ');
            return std::string(sp == std::string_view::npos ? rest : rest.substr(0, sp));
        }
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

// NEWLINE-SEPARATED, not `;`. A path may contain a semicolon and cannot
// contain a newline, which is why the engine chose it — and why a splitter
// that guesses wrong still works for exactly one shader and silently produces
// one impossible path for two.
// ─── This rule's share of the device sources ───────────────────────────────
//
// `mcpp::device_sources()` is the package's WHOLE device set, not this rule's
// share of it. A project with two backends puts a `.comp` and a `.cu` in one
// list, and every rule in that build program reads the same variable. Taking
// all of it works for exactly as long as a build has one rule in it, and then
// fails on the second -- not by dropping anything, but by handing a compiler a
// file it does not accept, with a message about that file's contents rather
// than about the rule that should have had it.
//
// So each rule takes the extensions it CLAIMS and leaves the rest to whoever
// claims those. A file no rule claims is not silently dropped either: the
// engine refuses a device source that reached no action, which is the one
// place that can see every rule's share at once.
constexpr std::string_view kClaimed[] = { ".comp", ".vert", ".frag", ".geom", ".tesc", ".tese", ".mesh", ".task",
    ".rgen", ".rint", ".rahit", ".rchit", ".rmiss", ".rcall",
    // Stage-less, and claimed on purpose: `stage_of` refuses them by name
    // and says which extensions carry a stage. Left unclaimed they would
    // reach the engine's "no action compiles it" instead, which is true
    // but says nothing about stages.
    ".glsl", ".hlsl", };

inline bool claims_extension(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto name  = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot   = name.rfind('.');
    if (dot == std::string_view::npos) return false;
    const auto ext = name.substr(dot);
    for (auto e : kClaimed) if (e == ext) return true;
    return false;
}

inline std::vector<std::string> device_shaders() {
    std::vector<std::string> out;
    std::string_view all(mcpp::device_sources());
    for (std::size_t i = 0; i <= all.size();) {
        auto sep = all.find('\n', i);
        auto one = trim(all.substr(i, sep == std::string_view::npos ? all.size() - i : sep - i));
        i = sep == std::string_view::npos ? all.size() + 1 : sep + 1;
        if (!one.empty() && claims_extension(one)) out.emplace_back(one);
    }
    return out;
}

// ─── The rule ──────────────────────────────────────────────────────────────

// glslc's `-mfmt=c` writes a BARE INITIALISER LIST and nothing else, so the
// declaration around it has to come from somewhere. It is written here rather
// than by a shell fragment in the action because an action is an argv, not a
// command line, and there is no shell in it to redirect or concatenate with.
//
// The header this writes is CONSTANT for a given shader name -- it names the
// symbol and includes the sibling the action produces -- so it is written at
// plan time, before any action runs. Its content does not depend on the
// shader's text, which is why nothing has to re-derive it when the shader
// changes: ninja rebuilds the `.inc`, the `#include` picks it up.
inline bool wrap_glslc_output(const std::string& header, const std::string& inc,
                              const std::string& sym) {
    std::ofstream out{header, std::ios::trunc};
    if (!out) {
        std::println(stderr, "mcpp.rules.spirv: cannot write {}", header);
        return false;
    }
    out << "// Generated by mcpp.rules.spirv. glslc emits an initialiser list;\n"
           "// this declaration is what makes it a translation unit.\n"
           "#pragma once\n"
           "#include <cstdint>\n"
           "static const uint32_t " << sym << "[] =\n"
           "#include \"" << std::filesystem::path(inc).filename().string() << "\"\n"
           ";\n";
    return out.good();
}

inline bool compile(std::span<const std::string> shaders, options opt = {}) {
    if (shaders.empty()) return true;

    const auto cc = find_compiler(opt);
    if (!cc) {
        if (cc.reported) return false;
        std::println(stderr,
            "mcpp.rules.spirv: no shader compiler found.\n"
            "  This rule DECLARES glslang, so a project normally writes nothing. Check, in "
            "order:\n"
            "  mcpp older than 2026.9.6.6; `features = [\"rules-spirv\"]` missing from the\n"
            "  [build-dependencies] edge; or a build that names no Vulkan accelerator.\n"
            "  To use glslc instead, or to pin a different version, name it in your own\n"
            "  project and it wins:\n"
            "  [target.'cfg(accelerator = \"vulkan\")'.xlings.workspace]\n"
            "  \"xim:glslang\" = \"15.1.0\"     # glslangValidator\n"
            "  \"xim:shaderc\" = \"2026.3\"     # glslc\n"
            "or name it: MCPP_GLSLANG=/path/to/glslangValidator, MCPP_GLSLC=/path/to/glslc,\n"
            "or set options::compiler.");
        return false;
    }
    // The fact is keyed on the flavour, not on a shared name: which of the two
    // compiled a shader is part of the answer, and a build log that recorded
    // both under one key could not tell them apart.
    if (const auto v = compiler_version(cc); !v.empty()) mcpp::fact(cc.name(), v.c_str());

    bool optimize = opt.optimize;
    // glslc always links its optimiser; glslang links spirv-opt only when
    // built with ENABLE_OPT, and the payload this ecosystem publishes is not.
    if (optimize && cc.kind == flavour::glslang && !has_optimizer(cc.path)) {
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

    // TWO SHADERS THAT DIFFER ONLY BY DIRECTORY PRODUCE ONE HEADER AND ONE
    // SYMBOL, AND THAT HAS TO BE REFUSED HERE.
    //
    // The output name is the stem and the stage, as this rule documents, so
    // `shaders/ui/text.vert` and `shaders/world/text.vert` both resolve to
    // `text_vert.h` declaring `text_vert_spv`. Disambiguating by directory is
    // not the fix: the SYMBOL would still collide the moment both headers
    // reached one translation unit, and the naming rule is what consumers write
    // `#include` lines against.
    //
    // Measured before this check existed: ninja caught it -- `multiple rules
    // generate .../text_vert.h` -- so it was never silent. What it did not do
    // is name the two SHADERS, say which rule produced them, or state the way
    // out; and it arrives as a graph-loading failure rather than as this rule's
    // refusal. A project with one shader per stage never meets it, which is why
    // it survived: a graphics project organising shaders by purpose is the
    // first to have two.
    {
        std::map<std::string, std::string> seen;   // output stem -> first source
        for (auto const& src : shaders) {
            const std::filesystem::path p(src);
            const auto stage = stage_of(p.extension().string());
            if (stage.empty()) continue;           // reported below, per source
            const auto key = p.stem().string() + "_" + std::string(stage);
            auto [it, fresh] = seen.try_emplace(key, src);
            if (!fresh) {
                std::println(std::cerr,
                    "mcpp.rules.spirv: two shaders map to one output.\n"
                    "    {}\n"
                    "    {}\n"
                    "  both produce `{}.h` declaring `{}`, because the name is the "
                    "shader's stem\n"
                    "  and its stage -- the directory is not part of it, and could not "
                    "be: two\n"
                    "  headers reaching one translation unit would still collide on the "
                    "symbol.\n"
                    "  fix: rename one of them, or compile only one.",
                    it->second, src, key, symbol_of(p.stem().string(), stage));
                return false;
            }
        }
    }

    for (auto const& src : shaders) {
        const std::filesystem::path p(src);
        const auto stage = stage_of(p.extension().string());
        if (stage.empty()) {
            std::println(stderr,
                "mcpp.rules.spirv: {} has no shader stage. Both compilers derive the stage "
                "from the extension; rename it to one of .comp .vert .frag .geom .tesc "
                ".tese .mesh .task .rgen .rint .rahit .rchit .rmiss .rcall", src);
            return false;
        }
        const auto sym  = symbol_of(p.stem().string(), stage);
        const auto base = (std::filesystem::path(gen)
                           / (p.stem().string() + "_" + std::string(stage))).string();
        const auto header = base + ".h";
        const auto input  = std::filesystem::path(src).is_absolute()
                          ? src : root + "/" + src;

        // `id` and `description` are raw pointers the action reads at
        // `submit()`; `arg`/`input`/`output` copy, these two do not. Held in
        // named strings for the life of the statement that submits.
        const std::string id   = "spirv:" + src;
        const std::string desc = std::string(cc.name()) + " " + src;
        // For glslc the action's output is the initialiser list; for glslang it
        // is the header itself.
        const std::string inc    = base + ".inc";
        const std::string output = cc.kind == flavour::glslc ? inc : header;

        if (cc.kind == flavour::glslc && !wrap_glslc_output(header, inc, sym)) return false;

        mcpp::action a;
        a.id          = id.c_str();
        a.role        = "source";     // a header: ordered before compilation
        a.description = desc.c_str();
        a.arg(cc.path.c_str());
        if (cc.kind == flavour::glslc) {
            // glslc spells it as one token; glslang takes a separate argument.
            a.arg(("--target-env=" + env).c_str());
        } else {
            a.arg("-V");
            a.arg("--target-env"); a.arg(env.c_str());
        }
        // The stage, in each compiler's own spelling of the same idea. glslc
        // accepts glslang's short names (`comp`, `vert`, ...) so the table
        // above serves both -- but its `-S` means "emit assembly", so passing
        // glslang's flag to it would produce a text file the program then
        // includes as if it were data.
        if (cc.kind == flavour::glslc) {
            a.arg(("-fshader-stage=" + std::string(stage)).c_str());
        } else {
            a.arg("-S"); a.arg(std::string(stage).c_str());
        }
        if (optimize) a.arg(cc.kind == flavour::glslc ? "-O" : "-Os");
        for (auto const& d : opt.defines) a.arg(("-D" + d).c_str());
        for (auto const& i : opt.includes)
            a.arg(("-I" + (std::filesystem::path(i).is_absolute() ? i : root + "/" + i)).c_str());
        if (cc.kind == flavour::glslc) {
            // `-mfmt=c` is the initialiser list; the declaration around it was
            // written above.
            a.arg("-mfmt=c");
        } else {
            // `-x --vn` is what makes glslang's output a C declaration rather
            // than a binary: a `const uint32_t <sym>[]` the program includes.
            a.arg("-x");
            a.arg("--vn"); a.arg(sym.c_str());
        }
        a.arg("-o"); a.arg(output.c_str());
        a.arg(input.c_str());
        a.input(input.c_str());
        a.output(output.c_str());
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
    // Several rules in one build program is the ordinary shape for a project
    // with several backends, and each is called unconditionally -- the build
    // program cannot know which backends this build named without parsing
    // `accel` itself, which is what the rule already does. A rule whose
    // backend this build does not name has nothing to do, and that is not a
    // mistake and must not be reported as one.
    if (!parse_target(mcpp::accel()).present) return true;
    const auto shaders = device_shaders();
    if (shaders.empty()) {
        mcpp::warning("[build] accel names vulkan but no constrained glob matched a "
                      "shader; nothing was compiled for it");
        return true;
    }
    return compile(std::span<const std::string>(shaders), std::move(opt));
}

} // namespace mcpp::rules::spirv
