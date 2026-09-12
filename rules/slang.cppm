// mcpp.rules.slang -- how a Slang translation unit becomes SPIR-V, stated once.
//
// WHY THIS IS A RULE OF ITS OWN RATHER THAN A THIRD FLAVOUR OF rules.spirv.
//
// glslang and glslc are two DRIVERS for one language: they compile the same
// `.comp`, and `mcpp.rules.spirv` chooses between them because the choice
// changes nothing a consumer sees. Slang is a different LANGUAGE. It has its
// own extension, its own module system (`import`, not `#include`), generics,
// and a target set that includes DXIL, Metal and WGSL -- for which the Vulkan
// axis `rules.spirv` reads has no answer at all. Folding it in would have put
// the rule ahead of the engine's own vocabulary.
//
// WHAT IS AND IS NOT IN SCOPE HERE.
//
// SPIR-V under a Vulkan accelerator, and nothing else. Slang can emit DXIL and
// Metal, and this rule deliberately does not, because `[build] accel` cannot
// yet express either and a rule that accepted a target the manifest could not
// name would be answering a question nobody asked it.
//
// THE COMPILER'S EMBEDDED OUTPUT NAMES TYPES IT DOES NOT INCLUDE.
//
// `-source-embed-style u32` writes a complete declaration:
//
//   const uint32_t t_slang_spv[] =
//   { 0x07230203, ... };
//   const size_t t_slang_spv_sizeInBytes = 172;
//
// and includes nothing, so the file names `uint32_t` and `size_t` with no
// declaration for either. That is the same defect `mcpp.rules.spirv` fixed in
// 0.2.6 for glslang's `-x --vn`, found the same way -- a program whose FIRST
// include was the generated header -- so this rule takes the same shape from
// the start: the compiler writes `<base>.inc` and the rule writes `<base>.h`
// around it.
//
// THE SIZE COMES FROM THE COMPILER, NOT FROM `sizeof`.
//
// `-source-embed-style` emits `<name>_sizeInBytes` beside the array. It equals
// `sizeof` for this style and would not for a style that terminates its output,
// so the generated accessor uses what the compiler stated rather than a
// coincidence that holds today.
module;
#include <cctype>
#include <cstdio>

export module mcpp.rules.slang;

import std;
import mcpp;
// The lib root, which carries `mcpp::plugins::surface` -- the declarations a
// consumer names, shared with `mcpp.rules.spirv` so a project that has both
// reaches them through one shape.
import mcpp.plugins;
import mcpp.plugins.declare;

// `std::println` is avoided here for the reason every file in this package
// records: it is not header-only, and the symbols its overloads reach for were
// added to libc++ in a version macOS 14 does not ship.

export namespace mcpp::rules::slang {

struct options {
    // The SPIR-V version, as Slang spells a profile: `spirv_1_5`. Left empty it
    // is derived from the accelerator axis, which is where `[build] accel`
    // already states what the build is for.
    std::string profile;
    // `-I` for `#include` and `import`, `-D` for the preprocessor. Relative
    // entries resolve against the package root; an absolute entry passes
    // through.
    //
    // A SLANG MODULE PACKAGE NEEDS NO NEW CONCEPT. It is an ordinary mcpp
    // package whose `include_dirs` names its `.slang` directory, and a consumer
    // adds that directory here with `mcpp::dep_dir`. Another build system had
    // to invent a scope API for this because it had no package-level dependency
    // to reuse; this one does.
    std::vector<std::string> includes;
    std::vector<std::string> defines;
    // `-O` levels Slang accepts. Empty leaves the compiler's own default.
    std::string optimization = "3";
    // ARGUMENTS THIS RULE HAS NO FIELD FOR, passed to slangc verbatim.
    //
    // slangc takes some two hundred options -- `-fvk-use-gl-layout`,
    // `-fvk-use-entrypoint-name`, `-emit-spirv-directly`, `-floating-point-mode`,
    // `-g` -- and a rule that grew a field for each would be a copy of `slangc
    // -h` that drifts. The rule owns the arguments that decide WHAT is produced
    // (target, profile, output, the embedding); everything else is the
    // project's, and this is where it goes. Appended after the rule's own flags
    // and before `-o`, so a project's argument can never trail the output name.
    std::vector<std::string> extra_args;

    // WHAT ONE SHADER GETS THAT THE OTHERS DO NOT.
    //
    // One `compile()` call writes one surface, so a project cannot call it twice
    // with two option sets: the second call would rewrite the generated module
    // with only its own shaders. A per-file table keeps the one call and the one
    // surface. The key is the shader's path as the constrained glob names it --
    // package-relative, `/`-separated -- and a key that names no shader in this
    // build is refused with the shaders that were seen, because a typo that
    // silently applied nothing is the failure this table would otherwise hide.
    struct overrides {
        std::vector<std::string> defines;      // `-D`, added to `options::defines`
        std::vector<std::string> extra_args;   // added after `options::extra_args`
    };
    std::map<std::string, overrides> per_file;

    // An explicit compiler path wins over discovery.
    std::string compiler;
    std::string out_dir = std::string(mcpp::out_dir());

    // ── What a consumer names ────────────────────────────────────────────────
    // Identical to `mcpp.rules.spirv`, from the same generator. See
    // `mcpp::plugins::surface`.
    mcpp::plugins::surface::kind surface = mcpp::plugins::surface::default_surface();

    // Where the compiled SPIR-V lives. `header` compiles it in as generated
    // source, `object` as a section reached through `.incbin`, `sidecar` as a
    // file beside the artifact. The same axis `mcpp.rules.spirv` has, with the
    // same default, for the measurement recorded on
    // `mcpp::plugins::surface::storage`.
    //
    // It changes what this rule asks the compiler for. Under `header` slangc is
    // told to embed (`-source-embed-style u32`); under the other two it writes a
    // bare `.spv`, which is the one shape every consumer of a file wants.
    mcpp::plugins::surface::storage storage = mcpp::plugins::surface::storage::header;

    std::string module_name;
    std::string base_dir;
};

// Where the generated files are written.
inline std::string include_dir(const options& opt) {
    return (std::filesystem::path(opt.out_dir) / "slang").string();
}

// ─── What the engine said ──────────────────────────────────────────────────

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
        auto plus = rest.find('+');
        t.version = std::string(trim(plus == std::string_view::npos ? rest
                                                                   : rest.substr(0, plus)));
    }
    return t;
}

// THE PROFILE IS DERIVED FROM THE VULKAN VERSION, BECAUSE THAT IS WHAT DECIDES
// IT. Each Vulkan release admits SPIR-V up to a fixed version, and a module
// emitted above it is refused by the loader at `vkCreateShaderModule` rather
// than at build time. The table is the one in the Vulkan specification's
// appendix; an unknown version falls back to the floor every Vulkan
// implementation accepts rather than guessing upward.
inline std::string profile_for(std::string_view vulkanVersion) {
    // Vulkan 1.4 requires SPIR-V 1.6 support and admits nothing newer, so the
    // row is the same as 1.3's; without it a 1.4 build fell to the floor below.
    if (vulkanVersion == "1.4") return "spirv_1_6";
    if (vulkanVersion == "1.3") return "spirv_1_6";
    if (vulkanVersion == "1.2") return "spirv_1_5";
    if (vulkanVersion == "1.1") return "spirv_1_3";
    return "spirv_1_0";
}

// ─── The compiler ──────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Windows spells the separator differently and its paths contain the character
// the other platforms separate on, so this is not one constant used twice.
#if defined(_WIN32)
inline constexpr char kPathSep = ';';
inline constexpr const char* kExeSuffix = ".exe";
#else
inline constexpr char kPathSep = ':';
inline constexpr const char* kExeSuffix = "";
#endif

inline std::string first_on_path(const char* exe) {
    const char* path = std::getenv("PATH");
    if (!path || !*path) return {};
    std::string_view sv(path);
    for (std::size_t i = 0; i <= sv.size();) {
        auto sep = sv.find(kPathSep, i);
        auto dir = sv.substr(i, sep == std::string_view::npos ? sv.size() - i : sep - i);
        i = sep == std::string_view::npos ? sv.size() + 1 : sep + 1;
        if (dir.empty()) continue;
        auto p = (std::filesystem::path(dir) / (std::string(exe) + kExeSuffix)).string();
        if (is_file(p)) return p;
    }
    return {};
}

// Discovery, in the order a project can predict: what it named, what the
// environment named, the payload the rule declared, then the PATH. The PATH
// comes last on purpose -- a host slangc is a fine fallback and a poor default,
// because it makes the SPIR-V depend on a machine rather than on a declaration.
inline std::string find_compiler(const options& opt) {
    if (!opt.compiler.empty()) return opt.compiler;
    if (const char* e = std::getenv("MCPP_SLANGC"); e && *e) return e;
    if (const char* dir = mcpp::xpkg_dir("slang"); dir && *dir)
        if (auto p = (std::filesystem::path(dir) / "bin" / (std::string("slangc") + kExeSuffix)).string();
            is_file(p)) return p;
    return first_on_path("slangc");
}

inline std::string run_and_capture(const std::string& cmd) {
#if defined(_WIN32)
    FILE* p = ::_popen(cmd.c_str(), "r");
#else
    FILE* p = ::popen(cmd.c_str(), "r");
#endif
    if (!p) return {};
    std::string text;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) text += buf;
#if defined(_WIN32)
    ::_pclose(p);
#else
    ::pclose(p);
#endif
    return text;
}

// `slangc -v` prints the release on its own, e.g. `2026.14.1`.
inline std::string compiler_version(const std::string& exe) {
    auto text = run_and_capture("\"" + exe + "\" -v 2>&1");
    for (auto& c : text) if (c == '\r') c = '\n';
    auto nl = text.find('\n');
    return std::string(trim(nl == std::string::npos ? text : text.substr(0, nl)));
}

// ─── This rule's share of the device sources ───────────────────────────────
//
// Each rule takes the extensions it CLAIMS and leaves the rest to whoever
// claims those, for the reason `mcpp.rules.spirv` records: a project with two
// backends puts every device source in one list, and a rule that took all of it
// would hand a compiler a file it does not accept and report the file's
// contents rather than the rule that should have had it.
constexpr std::string_view kClaimed[] = { ".slang" };

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

// ─── Naming ────────────────────────────────────────────────────────────────

// The path-to-namespace derivations come from the lib root. They were written
// here and again in the other rule that has a namespaced surface; one function
// is what makes a directory named `default` get one answer rather than two that
// agree by inspection.
using mcpp::plugins::names::common_base_dir;
using mcpp::plugins::names::namespace_of;

// The array the embedded output declares. The namespace path is part of it for
// the reason `mcpp.rules.spirv` records: one translation unit includes every
// generated data header, and two byte-identical headers collapse silently under
// GCC's `#pragma once`.
inline std::string symbol_of(std::span<const std::string> name_space, std::string_view stem) {
    std::string s;
    for (auto const& seg : name_space) { s += seg; s += '_'; }
    for (char c : stem)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    s += "_slang_spv";
    return s;
}

// ─── The rule ──────────────────────────────────────────────────────────────

// The public header around the compiler's embedded output. See the note at the
// top: `-source-embed-style u32` names `uint32_t` and `size_t` and includes
// nothing, so a program whose first include is the generated header does not
// compile without this.
inline bool write_header(const std::string& header, const std::string& inc) {
    std::ofstream out{header, std::ios::trunc};
    if (!out) {
        std::cerr << std::format("mcpp.rules.slang: cannot write {}", header) << '\n';
        return false;
    }
    out << "// Generated by mcpp.rules.slang.\n"
           "// slangc's embedded output names `uint32_t` and `size_t` and includes\n"
           "// nothing; what this adds is the types it names and a guard.\n"
           "#pragma once\n"
           "#include <cstdint>\n"
           "#include <cstddef>\n"
           "#include \"" << std::filesystem::path(inc).filename().string() << "\"\n";
    return out.good();
}

// The path as `per_file` keys it and as `mcpp::device_sources()` lists it are
// both package-relative, but one may have been typed on Windows and the other
// derived there: separators are unified and a leading `./` dropped before the
// two are compared.
//
// NOT `std::filesystem::path::lexically_normal()`, AND THE REASON IS A COMPILER.
// This unit is compiled as a module interface that imports `std`, and MSVC
// 14.52 (measured on both 14.52.36629 and 14.52.36725, xrgui's CI) refuses to
// instantiate `_Path_iterator`'s hidden-friend `operator==` there:
//
//   include\filesystem(1572): error C2801: '..._Path_iterator<...>::operator =='
//   must be a non-static member
//
// `lexically_normal` walks the path's components and is the one call 0.7.0
// added that reaches that operator. The two normalisations this key needs are
// string operations, so nothing is lost by not going through `path` at all.
inline std::string key_of(std::string_view path) {
    std::string s(path);
    for (auto& c : s) if (c == '\\') c = '/';
    while (s.starts_with("./")) s.erase(0, 2);
    return s;
}

inline bool compile(std::span<const std::string> shaders, options opt = {}) {
    if (shaders.empty()) return true;

    const auto cc = find_compiler(opt);
    if (cc.empty()) {
        std::cerr <<
            "mcpp.rules.slang: no Slang compiler found.\n"
            "  This rule DECLARES xim:slang, so a project normally writes nothing. Check,\n"
            "  in order: mcpp older than 2026.9.6.6; `features = [\"rules-slang\"]` missing\n"
            "  from the [build-dependencies] edge; or a build that names no Vulkan\n"
            "  accelerator.\n"
            "  To pin a different version, name it in your own project and it wins:\n"
            "    [target.'cfg(accelerator = \"vulkan\")'.xlings.workspace]\n"
            "    \"xim:slang\" = \"2026.14.1\"\n"
            "  or name the program: MCPP_SLANGC=/path/to/slangc, or set options::compiler.\n";
        return false;
    }
    if (const auto v = compiler_version(cc); !v.empty()) mcpp::fact("slangc", v.c_str());

    auto profile = opt.profile;
    if (profile.empty()) profile = profile_for(parse_target(mcpp::accel()).version);

    const std::string root = mcpp::manifest_dir();
    const auto gen = include_dir(opt);
    std::error_code ec;
    std::filesystem::create_directories(gen, ec);

    const std::string baseDir = opt.base_dir.empty() ? common_base_dir(shaders) : opt.base_dir;
    const std::string moduleName =
        opt.module_name.empty()
            ? mcpp::plugins::surface::module_root_for(
                  mcpp::package_name(),
                  std::filesystem::path(mcpp::manifest_dir()).filename().string())
              + ".shaders"
            : opt.module_name;

    // Two shaders whose stem and directory both match would produce one output,
    // which is refused here rather than left to whichever action ran last.
    {
        std::map<std::string, std::string> seen;
        for (auto const& src : shaders) {
            std::string key;
            for (auto const& seg : namespace_of(src, baseDir)) key += seg + "/";
            key += std::filesystem::path(src).stem().string();
            auto [it, fresh] = seen.try_emplace(key, src);
            if (!fresh) {
                std::cerr << std::format(
                    "mcpp.rules.slang: two shaders map to one output.\n"
                    "    {}\n    {}\n"
                    "  both produce `{}.h`. The shader's directory below `{}` is part of\n"
                    "  the name, so this is two shaders with one name in one directory.\n"
                    "  fix: rename one of them, or compile only one.",
                    it->second, src, key,
                    baseDir.empty() ? std::string("the package root") : baseDir) << '\n';
                return false;
            }
        }
    }

    // EVERY `per_file` KEY MUST NAME A SHADER IN THIS CALL. Resolved up front so
    // that a key with a typo is refused before any action is submitted, naming
    // what it could have matched -- rather than applying nothing and building
    // a module the project believes carries the option.
    std::map<std::string, const options::overrides*> overridesBySource;
    {
        std::map<std::string, std::string> keyed;   // normalised key -> shader
        for (auto const& src : shaders) keyed.emplace(key_of(src), src);
        for (auto const& [key, ov] : opt.per_file) {
            const auto it = keyed.find(key_of(key));
            if (it == keyed.end()) {
                std::cerr << std::format(
                    "mcpp.rules.slang: `per_file` names `{}`, and no shader in this build "
                    "has that path.\n"
                    "  The key is the path as the constrained glob names it, relative to the\n"
                    "  package root. The shaders this call compiles are:\n", key);
                for (auto const& src : shaders) std::cerr << "    " << src << '\n';
                return false;
            }
            overridesBySource.emplace(it->second, &ov);
        }
    }

    const bool embedAsSource = opt.storage == mcpp::plugins::surface::storage::header;

    std::vector<mcpp::plugins::surface::item> items;

    for (auto const& src : shaders) {
        const std::filesystem::path p(src);
        const auto ns   = namespace_of(src, baseDir);
        const auto sym  = symbol_of(ns, p.stem().string());
        auto       dir  = std::filesystem::path(gen);
        for (auto const& seg : ns) dir /= seg;
        std::filesystem::create_directories(dir, ec);
        const auto base   = (dir / p.stem().string()).string();
        const auto header = base + ".h";
        // SLANGC APPENDS THE EMBEDDING LANGUAGE'S EXTENSION TO `-o`.
        //
        // `-o scale.inc` writes `scale.inc.h`, because `-source-embed-language`
        // defaults to C/C++ and the driver adds that language's suffix unless
        // the name already ends in it. An action whose declared output is
        // `scale.inc` therefore names a file the command never writes, and the
        // failure lands two edges away: the generated implementation includes
        // the wrapper, the wrapper includes a file that is not there, and the
        // message is `fatal error: scale.inc: No such file or directory` with
        // nothing pointing at the flag that caused it.
        //
        // Measured against slangc 2026.14.1. Naming the output `.h` outright
        // makes the file the compiler writes and the file this rule declares
        // the same one, without depending on that appending rule at all.
        const auto inc    = base + "_embed.h";
        // Declared here rather than beside the action, because the item below
        // names it: under `object` the surface writes `.incbin` of this path
        // before any action runs, and under `sidecar` the accessor opens it.
        const std::string spv = base + ".spv";
        const std::string output = embedAsSource ? inc : spv;
        const auto input  = p.is_absolute() ? src : root + "/" + src;

        // The wrapper header exists only to make the compiler's embedded output
        // a translation unit. The other two storages never read a header, so
        // writing one would leave a file nothing includes.
        if (embedAsSource && !write_header(header, inc)) return false;

        std::string headerRel;
        for (auto const& seg : ns) headerRel += seg + "/";
        headerRel += p.stem().string() + ".h";
        // Where a sidecar is found at run time: relative to the package root,
        // which is where `mcpp run` starts the program. The cost of that is
        // stated on `mcpp::plugins::surface::storage::sidecar`.
        const auto sidecarName =
            std::filesystem::path(spv).lexically_relative(root).generic_string();
        items.push_back({ .identifier     = p.stem().string(),
                          .name_space     = ns,
                          .data_header    = headerRel,
                          .data_symbol    = sym,
                          .payload_path   = spv,
                          .sidecar_name   = sidecarName,
                          // What the compiler stated, rather than `sizeof`.
                          // Only the embedded output declares it; a file's size
                          // is read when the file is.
                          .data_size_expr = embedAsSource ? sym + "_sizeInBytes"
                                                          : std::string{} });

        const options::overrides* ov = nullptr;
        if (auto it = overridesBySource.find(src); it != overridesBySource.end()) ov = it->second;

        const std::string id   = "slang:" + src;
        const std::string desc = "slangc " + src;

        mcpp::action a;
        a.id          = id.c_str();
        a.role        = "source";     // a header: ordered before compilation
        a.description = desc.c_str();
        a.arg(cc.c_str());
        a.arg("-target"); a.arg("spirv");
        a.arg("-profile"); a.arg(profile.c_str());
        if (!opt.optimization.empty()) a.arg(("-O" + opt.optimization).c_str());
        for (auto const& d : opt.defines) a.arg(("-D" + d).c_str());
        if (ov) for (auto const& d : ov->defines) a.arg(("-D" + d).c_str());
        for (auto const& i : opt.includes)
            a.arg(("-I" + (std::filesystem::path(i).is_absolute() ? i : root + "/" + i)).c_str());
        // The project's own arguments, global first so a per-file one that
        // contradicts it comes later and wins under slangc's last-wins rule.
        for (auto const& x : opt.extra_args) a.arg(x.c_str());
        if (ov) for (auto const& x : ov->extra_args) a.arg(x.c_str());
        if (embedAsSource) {
            // The embedded form, and the name it declares.
            a.arg("-source-embed-style"); a.arg("u32");
            a.arg("-source-embed-name");  a.arg(sym.c_str());
        }
        // Under `object` and `sidecar` neither flag is passed and slangc writes
        // a bare SPIR-V module.
        a.arg("-o"); a.arg(output.c_str());
        // What the shader `#include`s, which only slangc can know. `a.input()`
        // below names the `.slang` and is fixed here, before the compiler has
        // read a line; a shader including a second `.slang` therefore had no
        // edge to it, and editing that file left a stale module behind a green
        // build. slangc computes the answer while parsing and writes it out.
        //
        // Measured: `slangc ... -depfile s.d` writes
        // `out.spv: <entry>.slang <included>.slang`.
        const std::string dep = output + ".d";
        a.arg("-depfile"); a.arg(dep.c_str());
        a.depfile = dep.c_str();
        a.arg(input.c_str());
        a.input(input.c_str());
        a.output(output.c_str());
        a.submit();
    }

    mcpp::include_dir(gen.c_str());

    mcpp::plugins::surface::options so;
    so.surface     = opt.surface;
    so.store       = opt.storage;
    so.elem        = mcpp::plugins::surface::element::word32;
    so.module_name = moduleName;
    so.out_dir     = gen;
    so.produced_by = "mcpp.rules.slang";
    // Answered here, not read there: `mcpp.plugins.surface` compiles into a
    // plain binary as well as into this build program, so it takes its inputs.
    so.target_os          = mcpp::target_os();
    so.has_gas_assembler  = std::string_view(mcpp::compiler()) != "msvc";

    const auto out = mcpp::plugins::surface_for(items, so);
    if (!out.ok) return false;

    // The degradation is reported here because the generator cannot report it:
    // `mcpp::warning` is a build-program channel and `mcpp.plugins.surface`
    // deliberately has none. Whoever sets `options::storage` owns this.
    if (out.files.store != so.store)
        mcpp::warning("mcpp.rules.slang: object storage needs a GAS assembler and this "
                      "toolchain has none; the payload is compiled in as generated source "
                      "instead. The declarations a consumer sees are unchanged.");

    mcpp::fact("mcpp.plugins", std::string(mcpp::plugins::version).c_str());
    return true;
}

// The whole manifest's worth: the shaders the constrained glob routed here. A
// build that names no accelerator has none, and the seam's CPU side carries the
// program -- the same shape every other rule in this package has.
inline bool compile(options opt = {}) {
    if (!*mcpp::accel()) return true;
    if (!parse_target(mcpp::accel()).present) return true;
    const auto shaders = device_shaders();
    if (shaders.empty()) {
        mcpp::warning("[build] accel names vulkan but no constrained glob matched a "
                      "`.slang`; nothing was compiled for it");
        return true;
    }
    return compile(std::span<const std::string>(shaders), std::move(opt));
}

} // namespace mcpp::rules::slang
