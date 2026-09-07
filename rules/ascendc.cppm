// mcpp.rules.ascendc — how an Ascend C translation unit becomes a Da Vinci
// object, stated once.
//
// THE MODULE NAME IS `mcpp.rules.<x>` AND THE PACKAGE NAMESPACE IS `mcpp`,
// which is the rule-package specification (I1 and I8) rather than a
// preference: the name is declared by this source, and `mcpp.*` is reserved
// for rules the mcpp project maintains.
//
// The division of labour is the one `mcpp.rules.cuda` established. The ENGINE
// owns the graph -- the accelerator axis, the constrained source globs that
// route `*.asc` here instead of to the C++ compiler, the action edges, the
// fingerprint -- and does not know the word "ascend" or the word "bisheng".
// The RULE owns the spelling.
//
// WHY ASCEND IS ALREADY ISLAND-SHAPED. CANN's own operator libraries split
// `op_kernel/` from `op_host/`, and CMake registers ASC as a LANGUAGE of its
// own (`FindASC.cmake`, `CMAKE_ASC_COMPILE_OBJECT`). The seam mcpp needs is one
// Ascend already draws; nothing here imposes a shape on the vendor.
//
// THE COMPILE LINE, taken from the toolkit's own `CMakeASCInformation.cmake`
// and then measured rather than trusted:
//
//     bisheng <DEFINES> <INCLUDES> -fPIC <FLAGS> -o <OBJECT> -c -x asc <SOURCE>
//
// Three things that file does not say, each found by running it:
//
//   * `ASCEND_HOME_PATH` MUST BE IN THE ENVIRONMENT, and it points at the
//     ARCH directory (`<toolkit>/x86_64-linux`), not at the toolkit root. The
//     compiler loads `$ASCEND_HOME_PATH/lib64/plugin/asc/libasc_plugin.so`,
//     and without the variable it says `PlugIn Err: can not find
//     ASCEND_HOME_PATH` -- before it has looked at the source at all.
//   * THAT PLUGIN NEEDS `libmmpa.so`, which lives in the same tree's `lib64`
//     and is not on any default search path. Without it the plugin fails to
//     load and the failure surfaces as `unknown type name '__aicore__'`, a
//     message about the source that is not about the source.
//   * `-DTILING_KEY_VAR=0` and eleven include directories are what the
//     toolkit's `device_intf_pub` interface target carries. A kernel that
//     includes `kernel_operator.h` -- which every real one does -- needs all
//     of them.
//
// Measured on CANN 8.5.0, on a machine with no NPU: the command below turns a
// three-line kernel into `ELF 64-bit LSB relocatable, *unknown arch 0x1029*`,
// which is Da Vinci device code.
//
// WHAT THIS RULE DOES NOT YET DO. It compiles; it does not register. A CANN
// operator is reached at run time through per-SoC JSON that maps an operator
// signature to a binary file name, and emitting that is a second piece of work
// with its own contract. Compiling without registering is useful on its own --
// it is what tells a project its kernels are valid for a target it does not
// own -- and it is stated here so the boundary is not mistaken for an
// oversight.

module;

#include <cstdlib>

export module mcpp.rules.ascendc;

import std;
import mcpp;

export namespace mcpp::rules::ascendc {

// ─── What the engine said ──────────────────────────────────────────────────

// The `ascend` chunk of `mcpp::accel()`, e.g. `ascend8.5+{dav-c220}`. The
// architecture set is the Da Vinci core generation the kernels are compiled
// for, and it is a SET for the same reason CUDA's is: one artifact may carry
// code for several parts.
struct target {
    std::string version;                 // "8.5", from `ascend8.5`
    std::vector<std::string> archs;      // {"dav-c220"}
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
        auto open = accel.find('{', i), close = accel.find('}', i);
        // A comma inside the architecture set does not separate chunks.
        if (open != std::string_view::npos && close != std::string_view::npos
            && comma != std::string_view::npos && comma > open && comma < close)
            comma = accel.find(',', close);
        auto chunk = trim(comma == std::string_view::npos ? accel.substr(i)
                                                          : accel.substr(i, comma - i));
        i = comma == std::string_view::npos ? accel.size() + 1 : comma + 1;
        if (!chunk.starts_with("ascend")) continue;
        t.present = true;
        auto plus = chunk.find('+');
        t.version = std::string(trim(chunk.substr(6, plus == std::string_view::npos
                                                      ? chunk.size() - 6 : plus - 6)));
        if (plus != std::string_view::npos) {
            auto o = chunk.find('{', plus), c = chunk.find('}', plus);
            if (o != std::string_view::npos && c != std::string_view::npos)
                for (auto part : split(chunk.substr(o + 1, c - o - 1), ','))
                    if (auto a = trim(part); !a.empty()) t.archs.emplace_back(a);
        }
    }
    return t;
}

// ─── This rule's share of the device sources ───────────────────────────────
//
// `mcpp::device_sources()` is the package's WHOLE device set, not this rule's
// share of it. A project with two backends puts a `.asc` and a `.comp` in one
// list, and every rule in that build program reads the same variable. Taking
// all of it works for exactly as long as a build has one rule in it, and then
// fails on the second -- not by dropping anything, but by handing a compiler a
// file it does not accept.
//
// `.cce` is the older spelling of the same language and is claimed beside it.
constexpr std::string_view kClaimed[] = { ".asc", ".cce" };

inline bool claims_extension(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto name  = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot   = name.rfind('.');
    if (dot == std::string_view::npos) return false;
    const auto ext = name.substr(dot);
    for (auto e : kClaimed) if (e == ext) return true;
    return false;
}

inline std::vector<std::string> device_sources() {
    std::vector<std::string> out;
    for (auto part : split(std::string_view(mcpp::device_sources()), '\n'))
        if (auto s = trim(part); !s.empty() && claims_extension(s)) out.emplace_back(s);
    return out;
}

// ─── Locating the toolkit ──────────────────────────────────────────────────

inline std::string xpkg(const char* name) {
    if (const char* d = mcpp::xpkg_dir("xim", name); d && *d) return d;
    return {};
}

// The toolkit the project declared, and the ARCH directory inside it.
//
// The arch segment is read from the tree rather than derived from the host
// triple: the toolkit publishes `x86_64-linux` and `aarch64-linux`, exactly
// one of which exists in a given install, so asking the filesystem is both
// complete and self-checking.
struct toolkit {
    std::string root;        // <xpkg>/cann
    std::string arch_root;   // <xpkg>/cann/<arch>-linux

    std::string bisheng()   const { return arch_root + "/ccec_compiler/bin/bisheng"; }
    std::string lib64()     const { return arch_root + "/lib64"; }
    std::string simulator() const { return arch_root + "/simulator"; }
    // What the HOST half of the island needs: `acl/acl.h` to launch the kernel
    // and `libascendcl.so` to link against. These belong on the C++ line, not
    // on BiSheng's, and they are the rule's to supply for the reason the CUDA
    // rule supplies its own -- a manifest that names an absolute payload path
    // is a manifest that only builds on the machine it was written on.
    std::string host_include() const { return arch_root + "/include"; }
    // The DRIVER's link-time stubs, which the toolkit ships so a program can
    // be linked on a machine that has no NPU. `libascend_hal.so` belongs to
    // the driver -- the role `libcuda.so.1` plays for CUDA -- and the real one
    // is in ABI lockstep with the kernel module, so it is resolved at RUN time
    // against whatever the machine has. Linking against the stub is what makes
    // a device build possible on a development machine at all.
    std::string driver_stubs(std::string_view arch) const {
        return arch_root + "/devlib/linux/" + std::string(arch);
    }

    // The eleven directories the toolkit's own `device_intf_pub` target
    // carries. A kernel that includes `kernel_operator.h` needs all of them,
    // and every real kernel includes it.
    std::vector<std::string> include_dirs() const {
        static constexpr const char* kTails[] = {
            "asc/impl/adv_api", "asc/impl/basic_api", "asc/impl/utils",
            "asc/include", "asc/include/adv_api", "asc/include/basic_api",
            "asc/include/aicpu_api", "asc/include/utils",
            "tikcpp/tikcfw", "tikcpp/tikcfw/interface", "tikcpp/tikcfw/impl",
        };
        std::vector<std::string> out;
        for (auto const* tail : kTails) {
            auto d = arch_root + "/" + tail;
            if (std::filesystem::is_directory(d)) out.push_back(d);
        }
        return out;
    }
};

inline std::optional<toolkit> find_toolkit() {
    toolkit t;
    const auto pkg = xpkg("cann-toolkit");
    if (pkg.empty()) {
        std::println(std::cerr,
            "mcpp.rules.ascendc: the CANN toolkit is not installed.\n"
            "  This rule DECLARES it, so a project normally writes nothing. Check, in "
            "order:\n"
            "  mcpp older than 2026.9.6.6; `features = [\"rules-ascendc\"]` missing from "
            "the\n"
            "  [build-dependencies] edge; or a build that names no Ascend accelerator.\n"
            "  To pin a different version, name it in your own project and it wins:\n"
            "    [target.'cfg(accelerator = \"ascend\")'.xlings.workspace]\n"
            "    \"xim:cann-toolkit\" = \"8.5.0\"\n"
            "  It carries both halves this rule needs: the device compiler and,\n"
            "  for a machine with no NPU, the per-SoC simulators.");
        return std::nullopt;
    }
    t.root = pkg + "/cann";
    for (auto const* a : { "x86_64-linux", "aarch64-linux" }) {
        auto candidate = t.root + "/" + a;
        if (std::filesystem::is_directory(candidate)) { t.arch_root = candidate; break; }
    }
    if (t.arch_root.empty() || !std::filesystem::exists(t.bisheng())) {
        std::println(std::cerr,
            "mcpp.rules.ascendc: `xim:cann-toolkit` is installed at '{}' but has no\n"
            "  device compiler under <arch>-linux/ccec_compiler/bin/bisheng.\n"
            "  The install is incomplete; reinstall the package.", pkg);
        return std::nullopt;
    }
    return t;
}

// ─── The rule ──────────────────────────────────────────────────────────────

struct options {
    // Include directories of the PROJECT, added after the toolkit's own.
    std::vector<std::string> includes;
    // Extra flags, appended last so they win.
    std::vector<std::string> flags;
    // Where the objects go. Empty = the build program's OUT_DIR.
    std::string out_dir;
};

struct edge {
    std::string id, description;
    std::vector<std::string> command, inputs, outputs;
};

inline std::vector<edge> plan(std::span<const std::string> sources, options opt = {}) {
    std::vector<edge> out;
    const std::string root = mcpp::manifest_dir();
    if (root.empty()) {
        std::println(std::cerr,
            "mcpp.rules.ascendc: no mcpp build context -- this runs from build.mcpp");
        return out;
    }
    const auto tg = parse_target(mcpp::accel());
    if (!tg.present || tg.archs.empty()) {
        // The same refusal `mcpp.rules.cuda` makes, for the same reason: a
        // device build that names no device is refused HERE rather than at run
        // time, where it is a kernel that does not exist for the part present.
        std::println(std::cerr,
            "mcpp.rules.ascendc: [build] accel names no Da Vinci architecture "
            "(accel = \"{}\").\n"
            "  Write e.g.  accel = \"ascend8.5+{{dav-c220}}\"  -- the set a build\n"
            "  compiles for is a decision, and the machine's own hardware is a poor\n"
            "  default for it.", mcpp::accel());
        return out;
    }
    auto tk = find_toolkit();
    if (!tk) return out;

    const std::string outDir = opt.out_dir.empty() ? std::string(mcpp::out_dir())
                                                   : opt.out_dir;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    for (auto const& src : sources) {
        const std::filesystem::path p(src);
        for (auto const& arch : tg.archs) {
            const auto stem = p.stem().string();
            const auto obj  = outDir + "/" + stem + "." + arch + ".o";

            edge e;
            e.id          = "ascendc:" + stem + ":" + arch;
            e.description = "bisheng -x asc " + src + " (" + arch + ")";

            // `env`, not a shell: an action is an argv, and both variables are
            // load-bearing (see the header). Setting them in the argv keeps the
            // action reproducible and keeps the caller's environment out of it.
            e.command.push_back("env");
            e.command.push_back("ASCEND_HOME_PATH=" + tk->arch_root);
            e.command.push_back("LD_LIBRARY_PATH=" + tk->lib64());
            e.command.push_back(tk->bisheng());
            e.command.push_back("-x");
            e.command.push_back("asc");
            e.command.push_back("--cce-aicore-arch=" + arch);
            // NOT `--cce-aicore-only`, and the difference decides whether the
            // result can join an ordinary link at all.
            //
            // With it, BiSheng emits a Da Vinci object -- `ELF 64-bit LSB
            // relocatable, *unknown arch 0x1029*` -- which the host linker
            // cannot place. Without it, the same source yields an x86-64
            // object carrying BOTH the device binary (registered at load time
            // through `AscendDevBinaryRegister`) and a HOST-CALLABLE launcher
            // for each `__global__` function. That second object is what
            // mcpp's model needs: a device unit becomes an object and the
            // ordinary link takes it, with no registration file and no
            // separate device-link step.
            //
            // The launcher's signature is `(blockDim, l2ctrl, stream, ...the
            // kernel's own parameters)` and it is C++-MANGLED even when the
            // kernel is declared `extern "C"` -- measured. Rather than have
            // the host half depend on two compilers agreeing about mangling,
            // the seam should be an `extern "C"` wrapper in the same `.asc`
            // file, which this rule compiles along with it.
            e.command.push_back("-fPIC");
            e.command.push_back("-DTILING_KEY_VAR=0");
            for (auto const& d : tk->include_dirs()) e.command.push_back("-I" + d);
            for (auto const& d : opt.includes)
                e.command.push_back("-I" + (std::filesystem::path(d).is_absolute()
                                            ? d : root + "/" + d));
            for (auto const& f : opt.flags) e.command.push_back(f);
            e.command.push_back("-c");
            e.command.push_back(std::filesystem::path(src).is_absolute() ? src
                                                                        : root + "/" + src);
            e.command.push_back("-o");
            e.command.push_back(obj);

            e.inputs  = { std::filesystem::path(src).is_absolute() ? src : root + "/" + src };
            e.outputs = { obj };
            out.push_back(std::move(e));
        }
    }
    return out;
}

inline bool submit(std::span<const edge> edges) {
    for (auto const& e : edges) {
        mcpp::action a;
        a.id          = e.id.c_str();
        a.role        = "object";     // the linkable artifact itself
        a.description = e.description.c_str();
        for (auto const& c : e.command) a.arg(c.c_str());
        for (auto const& i : e.inputs)  a.input(i.c_str());
        for (auto const& o : e.outputs) a.output(o.c_str());
        a.submit();
    }
    return true;
}

// Everything from the manifest: the architectures from `[build] accel`, the
// sources from the constrained glob in `[build] sources`.
inline bool compile(options opt = {}) {
    if (!*mcpp::accel()) return true;
    // Several rules in one build program is the ordinary shape for a project
    // with several backends, and each is called unconditionally. A rule whose
    // backend this build does not name has nothing to do, and that is not a
    // mistake and must not be reported as one.
    if (!parse_target(mcpp::accel()).present) return true;
    const auto sources = device_sources();
    if (sources.empty()) {
        mcpp::warning("[build] accel names ascend but no constrained glob matched a "
                      "`.asc`; nothing was compiled for it");
        return true;
    }
    auto edges = plan(sources, std::move(opt));
    if (edges.empty()) return false;
    if (!submit(edges)) return false;

    // The HOST half's line. `include_dir` is private to this package's own
    // translation units, which is right: a consumer of this project has no
    // business seeing the toolkit's headers. `link_search` reaches the final
    // link, which is what `-lascendcl` in the manifest then resolves against.
    if (auto tk = find_toolkit()) {
        if (std::filesystem::is_directory(tk->host_include()))
            mcpp::include_dir(tk->host_include().c_str());
        if (std::filesystem::is_directory(tk->lib64())) {
            mcpp::link_search(tk->lib64().c_str());
            // `-rpath-link`, AND `-L` IS NOT A SUBSTITUTE FOR IT. GNU ld
            // resolves a shared library's OWN `DT_NEEDED` entries through
            // `-rpath-link`, `-rpath` and `LD_LIBRARY_PATH` -- never through
            // `-L`, which only finds libraries named on the command line. The
            // toolkit's `libmmpa.so` needs `libc_sec.so` and its `libprofapi.so`
            // needs `libascendalog.so`, both siblings in this same directory,
            // and without this the link fails on symbols belonging to libraries
            // nobody named: `memset_s`, `CheckLogLevel`.
            //
            // Computed rather than declarable, which is what `mcpp::link_flag`
            // exists for -- `link_lib` names a library and `link_search` names
            // a directory, and neither can say this. It needs mcpp 2026.9.6.5.
            mcpp::link_flag(("-Wl,-rpath-link," + tk->lib64()).c_str());
            // AND `-rpath`, which is a different question from `-rpath-link`.
            // The first is where the LINKER looks; this is where the loader
            // will. mcpp gives an artifact a private interpreter that does not
            // consult the host's /usr/lib, so without this the program links
            // and then cannot start -- which mcpp's own runtime-closure check
            // reports, naming every one of these libraries.
            mcpp::link_flag(("-Wl,-rpath," + tk->lib64()).c_str());
            // …and the driver stub directory, for the same reason and one
            // more: `libascend_dump.so` in lib64 names the driver's own
            // `drvHdc*` symbols, so even a program that never touches the
            // driver cannot be linked without something to resolve them.
            const auto stubs = tk->driver_stubs(
                tk->arch_root.ends_with("aarch64-linux") ? "aarch64" : "x86_64");
            if (std::filesystem::is_directory(stubs)) {
                mcpp::link_search(stubs.c_str());
                mcpp::link_flag(("-Wl,-rpath-link," + stubs).c_str());
            }
            // A MIXED-MODE OBJECT CARRIES REGISTRATION CODE, AND THAT CODE HAS
            // TO RESOLVE. Compiling without `--cce-aicore-only` puts the device
            // binary in the object along with a constructor that registers it
            // -- `AscendDevBinaryRegister`, `AscendFunctionRegister`,
            // `AscendKernelLaunchWithFlagV2` -- and those live in
            // `libascendc_runtime.a`, which no project would know to name.
            // The rule names it, for the same reason it names the include
            // directories: it is the rule's knowledge that the object needs it.
            // …AND THAT ARCHIVE HAS ITS OWN CLOSURE. `libascendc_runtime.a`
            // is static, so its undefined symbols become the program's:
            // `mmGetTid` from `libmmpa` and `MsprofSysCycleTime` /
            // `MsprofReportApi` from `libprofapi`. Both are in the same
            // directory and neither is nameable by a project that never asked
            // for a profiler. Named in dependency order, because a static
            // archive is resolved once and in the order it is given.
            static constexpr const char* kRuntimeClosure[] = {
                "ascendc_runtime", "ascend_dump", "runtime", "mmpa", "profapi", "c_sec",
            };
            for (auto const* lib : kRuntimeClosure) {
                const auto a  = tk->lib64() + "/lib" + lib + ".a";
                const auto so = tk->lib64() + "/lib" + lib + ".so";
                if (std::filesystem::exists(a) || std::filesystem::exists(so))
                    mcpp::link_lib(lib);
            }
        }
    }
    return true;
}

} // namespace mcpp::rules::ascendc
