// Compile SYCL translation units and hand the objects to the link.
//
// THE SHAPE THIS RULE IS DIFFERENT FROM THE OTHER THREE, AND WHY
//
// `mcpp.rules.cuda` and `mcpp.rules.hip` drive a compiler over a source
// written in a dialect; `mcpp.rules.spirv` drives one over a shader. A SYCL
// translation unit is ORDINARY C++ -- there is no dialect to see -- and what
// makes it a device unit is that a second compiler with a device back end
// consumes it. That is exactly what `SourceKind::Device` states, so the file
// carries the extension `.sycl` and the engine routes it here through the
// same constrained glob that carries `.cu`.
//
// The alternative, letting a glob carry `.cpp`, was rejected in the engine
// for a reason this rule depends on: one extension would mean two things
// depending on which glob matched first, and the seam a device build is
// written around -- device code reaches the program only through an
// `extern "C"` header -- is legible precisely because the file name says
// which side of it a unit is on.
//
// TWO EDGES PER BUILD, NOT ONE PER SOURCE
//
// A SYCL object carries its device image but nothing registers that image
// with the runtime. The registration is produced by a DEVICE LINK
// (`-fsycl-link`), which reads every device object and emits one further host
// object holding the wrapper. So this rule submits N compile actions and one
// action that consumes their outputs -- the chained-action shape the engine
// orders by the graph rather than by declaration order.
//
// THREE PAYLOADS AND ONE FLAG EACH, ALL OF WHICH EXIST TO KEEP THE HOST OUT
//
//   xim:dpcpp      the compiler. Its clang has the SYCL front end; mcpp's
//                  own clang does not, which is the whole reason a second
//                  compiler appears here at all.
//   xim:gcc        `--gcc-install-dir`. Left alone, dpcpp's clang takes its
//                  C++ standard library headers from the host's GCC. Measured:
//                  without this flag the include search list contains
//                  /usr/include/c++, and the unit compiles against a library
//                  no part of this ecosystem chose.
//   xim:cuda-nvcc  `--cuda-path`, for the NVIDIA back end's libdevice. Also
//                  measured: without it, clang finds the host's CUDA
//                  installation and says nothing.
//
// WHY THE LINK CARRIES `-l:libstdc++.so.6` AND NOT `-lstdc++`
//
// The SYCL unit and `libsycl.so` are compiled against libstdc++; mcpp's own
// clang toolchain uses libc++. Both may be in one process because the seam is
// `extern "C"` and no C++ type crosses it -- but the libstdc++ half must
// actually be linked. `-lstdc++` does not do that: clang's driver treats it
// as a selector for the C++ standard library and REWRITES it to `-lc++`
// under `-stdlib=libc++`, so the flag disappears from the link line with no
// diagnostic and `std::cerr` comes back undefined from inside a SYCL header.
// `-l:<soname>` names a file and is not rewritten.
//
// AND THE BUILD SAYS SO, WHICH IS CORRECT AND EXPECTED.
//
// Two C++ runtimes in one image means two sets of unwinder symbols, and mcpp's
// duplicate-symbol check reports it:
//
//   warning: 68 symbols in this image are also provided by a library it loads
//     _Unwind_DeleteException() ... also provided by libgcc_s.so.1
//
// That warning is accurate and is the reason the seam discipline is not
// optional here: a SYCL exception must be caught inside the device translation
// unit and turned into a return code, because the runtime that threw it is not
// the one the caller would unwind with. The fixture in tests/sycl-consumer
// does exactly that, and its comment says why.
module;
#include <cstdlib>
#include <cstdio>

export module mcpp.rules.sycl;

import std;
import mcpp;

export namespace mcpp::rules::sycl {

struct options {
    // Header search paths for the island. Relative entries resolve against the
    // package root; an ABSOLUTE entry is passed through unchanged.
    std::vector<std::string> includes;
    // An explicit compiler path wins over the payload. Set it when a project
    // pins a DPC++ other than the one the workspace installed.
    std::string compiler;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── What the engine said ──────────────────────────────────────────────────

// THE PROGRAMMING MODEL AND THE DEVICE ARE TWO CHUNKS, NOT ONE.
//
//   accel = "sycl"                      -- SPIR-V, compiled by the runtime for
//                                          whatever device it finds: a CPU, a
//                                          Level Zero GPU, an OpenCL device.
//   accel = "sycl, cuda12.9+{sm_89}"    -- ahead of time for NVIDIA.
//
// The device is spelled the way every other rule in this ecosystem spells it,
// so `sm_89` does not acquire a second spelling because the source file says
// `.sycl` instead of `.cu`.
struct target {
    bool sycl = false;
    std::vector<std::string> cuda_archs;   // {"sm_89"}
    std::vector<std::string> amd_archs;    // {"gfx1100"}
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
        if (chunk.starts_with("sycl")) t.sycl = true;
        else if (chunk.starts_with("cuda")) t.cuda_archs = parse_arch_set(chunk);
        else if (chunk.starts_with("hip"))  t.amd_archs  = parse_arch_set(chunk);
    }
    return t;
}

// ─── The payloads ──────────────────────────────────────────────────────────

inline std::string payload(const char* name) {
    const char* d = mcpp::xpkg_dir(name);
    return d ? d : "";
}

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// `<gcc>/lib/gcc/<triple>/<version>` -- the directory clang means by
// `--gcc-install-dir`. Discovered rather than spelled, because the triple and
// the version are the payload's, not this rule's.
inline std::string gcc_install_dir(const std::string& gcc_root) {
    std::error_code ec;
    const auto base = std::filesystem::path(gcc_root) / "lib" / "gcc";
    for (std::filesystem::directory_iterator t{base, ec}, end; t != end; t.increment(ec)) {
        if (!t->is_directory(ec)) continue;
        for (std::filesystem::directory_iterator v{t->path(), ec}, vend; v != vend; v.increment(ec))
            if (v->is_directory(ec)) return v->path().string();
    }
    return {};
}

// `dpcpp --version` states the release and the intel/llvm revision it was
// built from. Stated as a fact so a build log answers "which SYCL compiler"
// without anyone reproducing the build.
inline std::string compiler_version(const std::string& exe) {
    FILE* p = ::popen(("\"" + exe + "\" --version 2>/dev/null").c_str(), "r");
    if (!p) return {};
    std::string text;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) text += buf;
    ::pclose(p);
    for (auto line : split(text, '\n')) {
        auto at = line.find("DPC++ compiler ");
        if (at == std::string_view::npos) continue;
        auto rest = trim(line.substr(at + std::string_view("DPC++ compiler ").size()));
        auto sp = rest.find(' ');
        return std::string(sp == std::string_view::npos ? rest : rest.substr(0, sp));
    }
    return {};
}

// ─── The rule ──────────────────────────────────────────────────────────────

struct edge {
    std::string id, description, role;
    std::vector<std::string> command, inputs, outputs;
};

inline std::vector<std::string> device_sources() {
    std::vector<std::string> out;
    for (auto part : split(std::string_view(mcpp::device_sources()), '\n'))
        if (auto s = trim(part); !s.empty()) out.emplace_back(s);
    return out;
}

inline std::vector<edge> plan(std::span<const std::string> sources, options opt = {}) {
    std::vector<edge> out;
    const std::string root = mcpp::manifest_dir();
    if (root.empty()) {
        std::println(std::cerr,
            "mcpp.rules.sycl: no mcpp build context -- this runs from build.mcpp");
        return out;
    }

    const auto tg = parse_target(mcpp::accel());
    if (!tg.amd_archs.empty() && tg.cuda_archs.empty()) {
        std::println(std::cerr,
            "mcpp.rules.sycl: [build] accel names AMD architectures and this ecosystem\n"
            "  publishes no ROCm payload yet, so nothing could link or run the result.\n"
            "  Available today: accel = \"sycl\" (SPIR-V, any device the runtime finds)\n"
            "  or accel = \"sycl, cuda12.9+{{sm_89}}\" (ahead of time for NVIDIA).");
        return out;
    }

    const std::string dpcpp = payload("dpcpp");
    const std::string gcc   = payload("gcc");
    const std::string cuda  = tg.cuda_archs.empty() ? std::string{} : payload("cuda-nvcc");
    // THE C LIBRARY IS THE SAME QUESTION AS THE C++ ONE, ONE LAYER DOWN.
    //
    // `--gcc-install-dir` below names the C++ standard library because, left
    // alone, dpcpp's clang reads the host's. Measured 2026-09-06, the C library
    // was never named, and its search list said so:
    //
    //   …/xim-x-gcc/15.1.0/…/include/c++/15.1.0   <- ecosystem, correct
    //   …/xim-x-dpcpp/7.1.0/lib/clang/22/include
    //   /usr/local/include                         <- the HOST
    //   /usr/include/x86_64-linux-gnu
    //   /usr/include
    //
    // No ecosystem glibc path at all. So `<cstdio>` in a `.sycl` unit reached
    // libstdc++ from the payload and `<stdio.h>` from the host.
    //
    // FORWARDING THE SYSROOT DOES NOT ANSWER IT HERE. `toolchain_sysroot()` is
    // the documented answer for a second compiler, and it is EMPTY under an
    // llvm toolchain -- which is what a SYCL project pins, because mcpp's own
    // clang has no SYCL front end. The LLVM payload's clang does not need it
    // (it is configured with the ecosystem glibc); dpcpp's clang is a
    // different clang and is not.
    const std::string glibc = payload("glibc");
    const std::string uapi  = payload("linux-headers");

    std::string missing;
    // The payload is needed for the COMPILER, so a project that named its own
    // does not need it. Asking for both would tell someone who has already
    // solved this to solve it again.
    if (dpcpp.empty() && opt.compiler.empty()) missing += "    \"xim:dpcpp\" = \"7.1.0\"\n";
    if (gcc.empty())   missing += "    \"xim:gcc\"   = \"15.1.0\"\n";
    // UNPINNED ON PURPOSE, and this is the one detail that makes the
    // declaration portable. The C library version is the RUNTIME BINDING's
    // choice, not the project's: the same tree resolved glibc 2.44 on one
    // machine and 2.44.2 on a runner. `xpkg_dir` with a pin answers for
    // exactly that version or for nothing, so a pinned entry here refuses on
    // the machine whose binding chose the other one -- measured, as a CI
    // failure telling a project to declare something it had declared.
    // `""` means "present, any version", which is the only thing a project can
    // truthfully say about a library it does not select.
    if (glibc.empty()) missing += "    \"xim:glibc\" = \"\"\n";
    if (uapi.empty())  missing += "    \"xim:linux-headers\" = \"\"\n";
    if (!tg.cuda_archs.empty() && cuda.empty())
        missing += "    \"xim:cuda-nvcc\" = \"12.9.86\"\n";
    if (!missing.empty()) {
        std::println(std::cerr,
            "mcpp.rules.sycl: the SYCL island needs payloads this project has not declared.\n"
            "  Add to mcpp.toml:\n\n  [xlings.workspace]\n{}\n"
            "  `xim:gcc` is not a second toolchain: it is the C++ standard library the SYCL\n"
            "  unit compiles against, and `xim:glibc` with `xim:linux-headers` is the C\n"
            "  library underneath it. Without them dpcpp's clang reads the HOST's headers,\n"
            "  which is measurable in its include search list and invisible on its command\n"
            "  line.",
            missing);
        return out;
    }

    auto exe = opt.compiler;
    if (exe.empty()) exe = dpcpp + "/bin/clang++";
    if (!is_file(exe)) {
        std::println(std::cerr,
            "mcpp.rules.sycl: {} is not a file. The dpcpp payload publishes its SYCL\n"
            "  compiler under clang's own name; set options::compiler to name another.", exe);
        return out;
    }
    if (auto v = compiler_version(exe); !v.empty()) mcpp::fact("dpcpp", v.c_str());

    const auto gid = gcc_install_dir(gcc);
    if (gid.empty()) {
        std::println(std::cerr,
            "mcpp.rules.sycl: the xim:gcc payload at {} has no lib/gcc/<triple>/<version>\n"
            "  directory, which is what --gcc-install-dir names.", gcc);
        return out;
    }

    // The target selection, once, shared by the compile and the device link:
    // the two must agree or the device link searches for an image the compile
    // did not produce and says so as a warning rather than an error.
    std::vector<std::string> targeting;
    if (!tg.cuda_archs.empty()) {
        targeting = { "-fsycl-targets=nvptx64-nvidia-cuda", "--cuda-path=" + cuda };
        for (auto const& a : tg.cuda_archs) {
            targeting.push_back("-Xsycl-target-backend");
            targeting.push_back("--cuda-gpu-arch=" + a);
        }
    }

    std::vector<std::string> front{ exe, "-fsycl", "-std=c++17", "-O2", "-fPIC",
                                    "--gcc-install-dir=" + gid };
    // The C library, ahead of whatever the compiler would have found. This is
    // the shape mcpp uses for its own translation units, and it puts the
    // ecosystem's glibc at the front of the search list; `/usr/include` stays
    // last, as a fallback for C headers no payload provides, which is what the
    // engine does too.
    front.push_back("-isystem" + glibc + "/include");
    front.push_back("-isystem" + uapi + "/include");
    front.insert(front.end(), targeting.begin(), targeting.end());

    // The link line gets its directories from here, not from the manifest: the
    // rule resolved the payload, so the rule names where its libraries are.
    mcpp::link_search((dpcpp + "/lib").c_str());
    mcpp::link_lib("sycl");
    // See the file header for why this is not `-lstdc++`.
    mcpp::link_lib(":libstdc++.so.6");

    // SPIR-V IS NOT A DEVICE, AND A BUILD THAT NAMES NO DEVICE SHOULD BE TOLD.
    //
    // `accel = "sycl"` alone compiles to SPIR-V and leaves the choice of
    // device to the runtime, which is the right default for a machine with a
    // Level Zero or OpenCL device. It is NOT right for a machine whose only
    // device is CUDA: that back end does not consume SPIR-V, and the failure
    // arrives from inside the SYCL scheduler --
    // `ProgramManager::getDeviceImage` -- which is neither in the caller's
    // frame nor in the queue's asynchronous handler, so no amount of care in
    // the program catches it. Measured on an RTX 4080: `terminate called after
    // throwing an instance of 'sycl::_V1::exception'`, with no message of the
    // program's own.
    //
    // An advisory rather than a refusal, because the SPIR-V form is correct
    // and portable and the rule cannot know the machine's devices -- which is
    // exactly why it is worth saying at build time rather than leaving to a
    // crash at the first kernel.
    if (tg.cuda_archs.empty())
        mcpp::warning("mcpp.rules.sycl: [build] accel names sycl with no device, so the "
                      "kernels are compiled to SPIR-V and the runtime chooses. A back end "
                      "that cannot consume SPIR-V -- CUDA is one -- fails inside the SYCL "
                      "scheduler, where the program cannot catch it. Name the device to "
                      "compile ahead of time: accel = \"sycl, cuda12.9+{sm_89}\".");

    std::println("mcpp.rules.sycl: {} -- {} for {}",
                 tg.cuda_archs.empty() ? "SPIR-V, compiled by the runtime"
                                       : "ahead of time, NVIDIA back end",
                 std::filesystem::path(exe).filename().string(),
                 tg.cuda_archs.empty() ? std::string("any device") : tg.cuda_archs.front());

    std::vector<std::string> objects;
    for (auto const& src : sources) {
        const auto stem = std::filesystem::path(src).stem().string();
        const auto obj  = opt.out_dir + "/" + stem + ".sycl.o";
        edge e;
        e.id          = "sycl:" + stem;
        e.role        = "object";
        e.description = "dpcpp -fsycl " + src;
        e.command     = front;
        for (auto const& inc : opt.includes)
            e.command.push_back("-I" + (std::filesystem::path(inc).is_absolute()
                                        ? inc : root + "/" + inc));
        // `-x c++` is not optional. `.sycl` is this ecosystem's spelling and
        // no compiler knows it; without this the driver classifies the file as
        // a LINKER INPUT, warns `'linker' input unused`, exits 0 and produces
        // nothing.
        e.command.insert(e.command.end(), { "-x", "c++", "-c", root + "/" + src, "-o", obj });
        e.inputs  = { root + "/" + src };
        e.outputs = { obj };
        out.push_back(std::move(e));
        objects.push_back(obj);
    }

    // The device link. Its output is an ordinary host object holding the
    // registration for every device image above, so it takes the `object`
    // role too and the engine puts it on the link line beside them.
    const auto wrapper = opt.out_dir + "/sycl_device_link.o";
    edge d;
    d.id          = "sycl:device-link";
    d.role        = "object";
    d.description = "dpcpp -fsycl-link (device images -> registration)";
    d.command     = { exe, "-fsycl", "-fPIC", "--gcc-install-dir=" + gid };
    d.command.insert(d.command.end(), targeting.begin(), targeting.end());
    d.command.push_back("-fsycl-link");
    for (auto const& o : objects) d.command.push_back(o);
    d.command.insert(d.command.end(), { "-o", wrapper });
    d.inputs  = objects;
    d.outputs = { wrapper };
    out.push_back(std::move(d));

    return out;
}

inline bool submit(std::span<const edge> edges) {
    for (auto const& e : edges) {
        mcpp::action a;
        a.id          = e.id.c_str();
        a.role        = e.role.c_str();
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
    const auto sources = device_sources();
    if (sources.empty()) {
        mcpp::warning("[build] accel names sycl but no constrained glob matched a device "
                      "source; nothing was compiled for it");
        return true;
    }
    auto edges = plan(sources, std::move(opt));
    if (edges.empty()) return false;
    return submit(edges);
}

} // namespace mcpp::rules::sycl
