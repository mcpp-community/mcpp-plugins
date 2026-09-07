// Compile HIP device translation units and hand the objects to the link.
//
// WHAT HIP IS ON EACH PLATFORM, AND WHY THAT DECIDES THIS FILE
//
// HIP has two implementations behind one API. On AMD hardware it is a runtime
// library that talks to ROCm. On NVIDIA hardware it is a HEADER LAYER: every
// entry point is an inline wrapper over the CUDA one, `hipError_t` is
// `cudaError_t` under a typedef, and the object a compiler produces links
// against the CUDA runtime and nothing of ROCm's. `hip/hip_runtime.h` selects
// between the two on `__HIP_PLATFORM_AMD__` and `__HIP_PLATFORM_NVIDIA__` and
// refuses when neither or both is defined.
//
// So on the NVIDIA platform this rule compiles a `.hip` unit with the SAME
// compiler `mcpp.rules.cuda` uses on its clang route -- the project's own
// toolchain clang, `-x cuda` -- plus the HIP include directory and that one
// macro. There is no hipcc, no second toolchain, and no ROCm on the machine.
// The payload is `xim:hip-nvidia`, which contains no binaries because there
// is nothing binary to contain.
//
// WHY NOT hipcc EVEN WHERE IT EXISTS. `hipcc` is a driver that reads
// HIP_PLATFORM, picks nvcc or amdclang, and forwards. Every decision it makes
// is one this rule has already made from the declaration, and it would make
// them again from the environment -- which is the shape this ecosystem
// removes rather than layers on.
//
// WHAT THE RULE TELLS THE ENGINE
//
// Objects, through `mcpp::action` (role "object"); the CUDA library
// directories, through `mcpp::link_search`; and the HIP version it compiled
// against, through `mcpp::fact`, so a build log answers "which HIP" without
// anyone reproducing the build.
module;
#include <cstdlib>
#include <cstdio>

export module mcpp.rules.hip;

import std;
import mcpp;


// WHY NOTHING HERE USES `std::println`, AND WHY THAT IS NOT A STYLE CHOICE.
//
// `std::print` and `std::println` are not header-only. Both of their overloads
// reach into the libc++ DYLIB -- `__is_posix_terminal(FILE*)` for the stdout
// form and `__get_ostream_file(ostream&)` for the stream form -- and those
// symbols were added to that library in a version macOS 14 does not ship. A
// build program's link resolves `-lc++` to the system copy there, so a rule
// that printed with `std::println` compiled and then failed to link:
//
//   ld64.lld: error: undefined symbol: std::__1::__is_posix_terminal(__sFILE*)
//
// naming neither the call that needed it nor the reason. Measured on
// macos-14; macos-15 has the symbol, which is why nothing saw this until a
// rule was first compiled on the older of the two supported releases.
//
// `std::format` is header-only and has no such dependency, so every message in
// this file is formatted and then streamed.

export namespace mcpp::rules::hip {

// The two implementations, named. `automatic` reads the accelerator axis.
enum class platform { automatic, nvidia, amd };

struct options {
    platform which = platform::automatic;
    // Header search paths for the island. Relative entries resolve against the
    // package root; an ABSOLUTE entry is passed through unchanged, which is
    // the form `mcpp::dep_dir` answers with -- a device compiler is a separate
    // driver and inherits nothing from the C++ side's include configuration.
    std::vector<std::string> includes;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── What the engine said ──────────────────────────────────────────────────

// THE PROGRAMMING MODEL AND THE DEVICE ARE TWO CHUNKS, NOT ONE.
//
// `accel = "hip, cuda12.9+{sm_89}"` says: reach the device through HIP, and
// the device is the one `mcpp.rules.cuda` would have named the same way. That
// split is the point -- a device is spelled once in this ecosystem however
// many programming models reach it, so `sm_89` does not acquire a second
// spelling because the source file says `hip` instead of `cu`.
struct target {
    bool hip = false;                    // the `hip` chunk is present
    std::string cuda_version;            // "12.9", from a `cuda12.9` chunk
    std::vector<std::string> cuda_archs; // {"sm_89"}
    std::vector<std::string> amd_archs;  // {"gfx1100"}, from `hip+{gfx1100}`
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

inline std::vector<std::string> parse_arch_set(std::string_view chunk) {
    std::vector<std::string> archs;
    auto open = chunk.find('{');
    auto close = chunk.find('}', open == std::string_view::npos ? 0 : open);
    if (open == std::string_view::npos || close == std::string_view::npos) return archs;
    for (auto a : split(chunk.substr(open + 1, close - open - 1), ','))
        if (auto t = trim(a); !t.empty()) archs.emplace_back(t);
    return archs;
}

inline target parse_target(std::string_view accel) {
    target t;
    for (auto raw : split(accel, ',')) {
        auto chunk = trim(raw);
        if (chunk.starts_with("hip")) {
            t.hip = true;
            t.amd_archs = parse_arch_set(chunk);
        } else if (chunk.starts_with("cuda")) {
            auto rest = chunk.substr(std::string_view("cuda").size());
            auto plus = rest.find('+');
            t.cuda_version = std::string(trim(
                plus == std::string_view::npos ? rest : rest.substr(0, plus)));
            // A version may be followed by a space-separated clause such as
            // `ptx>=89`; the version is the leading numeric run.
            if (auto sp = t.cuda_version.find(' '); sp != std::string::npos)
                t.cuda_version.resize(sp);
            t.cuda_archs = parse_arch_set(chunk);
        }
    }
    return t;
}

// ─── The payloads ──────────────────────────────────────────────────────────

struct toolkit {
    std::string hip_root, nvcc_root, cudart_root, curand_root, cccl_root, profiler_root;

    std::vector<std::string> include_dirs() const {
        std::vector<std::string> out;
        // HIP first: its `hip/hip_runtime.h` is the entry point, and its
        // `nvidia_detail` headers include the CUDA ones by their own names.
        if (!hip_root.empty()) out.push_back(hip_root + "/include");
        for (auto const* r : { &cudart_root, &nvcc_root, &cccl_root, &curand_root, &profiler_root })
            if (!r->empty() && std::filesystem::is_directory(*r + "/include"))
                out.push_back(*r + "/include");
        if (!cccl_root.empty() && std::filesystem::is_directory(cccl_root + "/include/cccl"))
            out.push_back(cccl_root + "/include/cccl");
        return out;
    }
    std::vector<std::string> lib_dirs() const {
        std::vector<std::string> out;
        for (auto const* r : { &cudart_root, &nvcc_root })
            for (auto const* sub : { "/lib", "/lib64" })
                if (!r->empty() && std::filesystem::is_directory(*r + sub))
                    out.push_back(*r + sub);
        return out;
    }
};

inline std::string payload(const char* name) {
    const char* d = mcpp::xpkg_dir(name);
    return d ? d : "";
}

// The HIP version, read from the header the payload generates rather than
// from the package version: they are two different statements and only the
// header is what the compiler will actually see.
inline std::string hip_version(const std::string& hip_root) {
    std::ifstream in{hip_root + "/include/hip/hip_version.h"};
    if (!in) return {};
    int major = -1, minor = -1, patch = -1;
    for (std::string line; std::getline(in, line);) {
        const char* fields[] = { "HIP_VERSION_MAJOR", "HIP_VERSION_MINOR", "HIP_VERSION_PATCH" };
        int* slots[] = { &major, &minor, &patch };
        for (int i = 0; i < 3; ++i) {
            auto at = line.find(fields[i]);
            if (at == std::string::npos) continue;
            auto rest = trim(std::string_view(line).substr(at + std::strlen(fields[i])));
            int v = 0;
            if (std::from_chars(rest.data(), rest.data() + rest.size(), v).ec == std::errc{})
                *slots[i] = v;
        }
    }
    if (major < 0 || minor < 0) return {};
    return patch < 0 ? std::format("{}.{}", major, minor)
                     : std::format("{}.{}.{}", major, minor, patch);
}

// ─── The rule ──────────────────────────────────────────────────────────────

struct edge {
    std::string id, description;
    std::vector<std::string> command, inputs, outputs;
};

// ─── This rule's share of the device sources ───────────────────────────────
//
// `mcpp::device_sources()` is the package's WHOLE device set, not this rule's
// share of it. A project with two backends puts a `.hip` and a `.comp` in one
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
constexpr std::string_view kClaimed[] = { ".hip" };

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

inline std::vector<edge> plan(std::span<const std::string> sources, options opt = {}) {
    std::vector<edge> out;
    const std::string root = mcpp::manifest_dir();
    if (root.empty()) {
        std::cerr << std::format("mcpp.rules.hip: no mcpp build context -- this runs from build.mcpp") << '\n';
        return out;
    }

    const auto tg = parse_target(mcpp::accel());
    auto which = opt.which;
    if (which == platform::automatic)
        which = tg.cuda_archs.empty() && !tg.amd_archs.empty() ? platform::amd
                                                               : platform::nvidia;

    if (which == platform::amd) {
        // Stated rather than approximated. The AMD platform needs a ROCm
        // runtime and device library, and this ecosystem publishes neither
        // yet; compiling for it would produce an object nothing on this
        // machine can link or run.
        std::cerr << std::format("mcpp.rules.hip: [build] accel names AMD architectures ({}) and no ROCm\n"
            "  payload is published in this ecosystem yet, so nothing could link or run\n"
            "  the result. The NVIDIA platform is available today:\n"
            "    accel = \"hip, cuda12.9+{{sm_89}}\"\n"
            "  which reaches the device through the CUDA runtime, with HIP as the API.",
            tg.amd_archs.empty() ? std::string("none") : tg.amd_archs.front()) << '\n';
        return out;
    }

    if (tg.cuda_archs.empty()) {
        // A device build that names no device is refused here, not at run time
        // as `no kernel image is available for execution`.
        std::cerr << std::format("mcpp.rules.hip: [build] accel names no device architecture (accel = \"{}\").\n"
            "  On the NVIDIA platform HIP compiles through the CUDA back end, and the\n"
            "  device is spelled the way every other rule in this ecosystem spells it:\n"
            "    accel = \"hip, cuda12.9+{{sm_89}}\"\n"
            "  The set a build compiles for is a decision; the machine's own hardware is\n"
            "  a poor default for it.",
            mcpp::accel()) << '\n';
        return out;
    }

    toolkit tk{ payload("hip-nvidia"), payload("cuda-nvcc"), payload("cuda-cudart"),
                payload("libcurand"), payload("cuda-cccl"), payload("cuda-profiler-api") };

    // Each missing payload is named with the line that adds it. `hip-nvidia`
    // is this rule's own; the other four are the CUDA back end the NVIDIA
    // platform compiles through, and cuRAND and CCCL are on the list for the
    // reason `mcpp.rules.cuda` records: clang's CUDA wrapper includes
    // `curand_mtgp32_kernel.h` for every device unit, and that header includes
    // `<nv/target>` from CCCL, so a unit that calls neither still needs both.
    struct need { const char* pkg; const std::string* root; const char* version; };
    const need needs[] = {
        { "hip-nvidia",  &tk.hip_root,    "7.2.4"       },
        { "cuda-nvcc",   &tk.nvcc_root,   "12.9.86"     },
        { "cuda-cudart", &tk.cudart_root, "12.9.79"     },
        { "libcurand",   &tk.curand_root, "10.3.10.19"  },
        { "cuda-cccl",   &tk.cccl_root,   "12.9.27"     },
        // `nvidia_hip_runtime_api.h` includes <cuda_profiler_api.h> at its
        // second line. CUDA ships it in its own component, and a machine with
        // a host CUDA installation finds it there without saying so -- which
        // is how this entry came to be missing.
        { "cuda-profiler-api", &tk.profiler_root, "12.9.79" },
    };
    std::string missing;
    for (auto const& n : needs)
        if (n.root->empty())
            missing += std::format("    \"xim:{}\" = \"{}\"\n", n.pkg, n.version);
    if (!missing.empty()) {
        std::cerr << std::format("mcpp.rules.hip: the HIP island needs payloads that are not installed.\n"
            "  This rule DECLARES them, so a project normally writes nothing. Check, in "
            "order:\n"
            "  mcpp older than 2026.9.6.6; `features = [\"rules-hip\"]` missing from the\n"
            "  [build-dependencies] edge; or a build that names no HIP accelerator.\n"
            "  To pin different versions, name them in your own project and they win:\n\n"
            "  [target.'cfg(accelerator = \"hip\")'.xlings.workspace]\n{}\n"
            "  They are PAYLOADS: the version is the project's choice, not the machine's.",
            missing) << '\n';
        return out;
    }

    if (auto v = hip_version(tk.hip_root); !v.empty()) mcpp::fact("hip", v.c_str());

    // THE TOOLCHAIN'S clang++ BY PATH, NOT `mcpp::compiler()`.
    //
    // That function answers with the compiler's IDENTITY -- the string
    // `clang` -- and putting an identity where an argv[0] belongs runs
    // whichever `clang` the action's PATH happens to offer. It works on a
    // machine whose PATH already has the payload and is a host leak
    // everywhere else. `mcpp.rules.cuda` takes the same path for the same
    // reason.
    const std::string tcdir = mcpp::toolchain_dir();
    // The suffix is the host's. This lane reaches only Linux today -- the
    // NVIDIA-platform header package is published for it alone -- so the
    // Windows spelling is not exercised by anything. It is written anyway,
    // because the alternative is a path that is wrong on a host this rule
    // will one day be asked about, and a wrong path reports itself as a
    // missing toolchain.
#if defined(_WIN32)
    const std::string cc = tcdir + "/bin/clang++.exe";
#else
    const std::string cc = tcdir + "/bin/clang++";
#endif
    if (tcdir.empty() || !std::filesystem::exists(cc)) {
        std::cerr << std::format("mcpp.rules.hip: the NVIDIA platform compiles through clang, and this "
            "project's\n  toolchain has no clang++ at {}.\n"
            "  Select an LLVM toolchain:  [toolchain] default = \"llvm@22.1.8\"", cc) << '\n';
        return out;
    }

    // `-x cuda`, and the macro is what makes it HIP. The unit's language is
    // CUDA C++ as far as the compiler is concerned; `hip/hip_runtime.h` maps
    // the HIP API onto it inline.
    std::vector<std::string> front{
        cc, "-x", "cuda", "-std=c++17", "-O2", "-fPIC",
        "-D__HIP_PLATFORM_NVIDIA__", "-Wno-unknown-cuda-version",
        "--cuda-path=" + tk.nvcc_root,
    };

    // THE C++ STANDARD LIBRARY IS NAMED, NOT INHERITED, AND THAT IS WHAT THIS
    // RULE NEEDS AND `mcpp.rules.cuda` DOES NOT.
    //
    // A bare CUDA kernel includes no C++ standard library header. The HIP
    // headers do -- `nvidia_hip_runtime_api.h` reaches <limits> -- so the
    // device pass compiles one, and WHICH one is decided by the compiler's
    // defaults unless something says otherwise. Measured: on a developer
    // machine clang's own configuration supplied this ecosystem's libc++ and
    // the build was clean; on a runner the same clang fell back to detecting
    // the host's GCC and read `/usr/include/c++/14`, whose <limits> declares
    // `__float128` -- which the NVPTX device target does not support:
    //
    //   .../include/c++/14/limits:2089:27: error: __float128 is not supported
    //   on this target
    //
    // Nineteen of those, from a header no part of this ecosystem chose. The
    // include search list is where this is visible; the command line is not,
    // which is why a check that greps the command line for `/usr` reported
    // nothing wrong on both machines.
    {
        std::error_code ec;
        const std::string cxx1 = tcdir + "/include/c++/v1";
        if (std::filesystem::is_directory(cxx1, ec)) {
            front.push_back("-nostdinc++");
            front.push_back("-isystem" + cxx1);
            // The per-triple overlay beside it, whose directory name is the
            // toolchain's own spelling of the target and not one this rule
            // can derive: `x86_64-unknown-linux-gnu` where `mcpp::target()`
            // says `x86_64-linux-gnu`. Found rather than constructed.
            for (std::filesystem::directory_iterator d{tcdir + "/include", ec}, end;
                 d != end; d.increment(ec)) {
                if (!d->is_directory(ec)) continue;
                const auto over = d->path() / "c++" / "v1";
                if (std::filesystem::is_directory(over, ec))
                    front.push_back("-isystem" + over.string());
            }
        }
    }
    for (auto const& a : tg.cuda_archs) front.push_back("--cuda-gpu-arch=" + a);
    for (auto const& inc : tk.include_dirs()) front.push_back("-I" + inc);
    // NVIDIA's own headers refuse libc++ for a nvcc host pass that is not
    // happening here: `crt/host_defines.h` stops with "libc++ is not supported
    // on x86 system" under `__CUDACC__ && _LIBCPP_VERSION`, and clang defines
    // `__CUDACC__` itself when compiling CUDA. The escape hatch is NVIDIA's
    // own and is passed only on this route.
    front.push_back("-D_ALLOW_UNSUPPORTED_LIBCPP");

    // The link line gets its directories from here, not from the manifest: the
    // rule resolved the payload, so the rule names where its libraries are.
    for (auto const& d : tk.lib_dirs()) mcpp::link_search(d.c_str());

    std::cout << std::format("mcpp.rules.hip: NVIDIA platform -- HIP {} over CUDA {}, {} for {}",
                 hip_version(tk.hip_root), tg.cuda_version.empty() ? "?" : tg.cuda_version,
                 std::filesystem::path(cc).filename().string(), tg.cuda_archs.front()) << '\n';

    for (auto const& src : sources) {
        const auto stem = std::filesystem::path(src).stem().string();
        const auto obj  = opt.out_dir + "/" + stem + ".hip.o";
        edge e;
        e.id          = "hip:" + stem;
        e.description = "clang -x cuda (HIP/NVIDIA) " + src;
        e.command     = front;
        for (auto const& inc : opt.includes)
            e.command.push_back("-I" + (std::filesystem::path(inc).is_absolute()
                                        ? inc : root + "/" + inc));
        e.command.insert(e.command.end(), { "-c", root + "/" + src, "-o", obj });
        e.inputs  = { root + "/" + src };
        e.outputs = { obj };
        out.push_back(std::move(e));
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

// The whole manifest's worth: the units the constrained glob routed here. A
// build that names no accelerator has none, and the seam's CPU side carries
// the program -- the same shape the other rules in this collection have, for
// the same reason.
inline bool compile(options opt = {}) {
    if (!*mcpp::accel()) return true;
    // Several rules in one build program is the ordinary shape for a project
    // with several backends, and each is called unconditionally -- the build
    // program cannot know which backends this build named without parsing
    // `accel` itself, which is what the rule already does. A rule whose
    // backend this build does not name has nothing to do, and that is not a
    // mistake and must not be reported as one.
    if (!parse_target(mcpp::accel()).hip) return true;
    const auto sources = device_sources();
    if (sources.empty()) {
        mcpp::warning("[build] accel names hip but no constrained glob matched a `.hip`; "
                      "nothing was compiled for it");
        return true;
    }
    auto edges = plan(sources, std::move(opt));
    if (edges.empty()) return false;
    return submit(edges);
}

} // namespace mcpp::rules::hip
