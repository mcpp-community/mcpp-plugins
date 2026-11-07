# Device and language rules

The rules that compile one kind of translation unit with a compiler mcpp does not drive: GPU and accelerator languages (`rules-ascendc`, `rules-cuda`, `rules-hip`, `rules-sycl`), shader languages (`rules-spirv`, `rules-slang`, `rules-metal`) and Swift (`rules-swift`). Each section states the module, the engine floor, what the rule needs and how it compiles. The environment each rule brings and the extensions it claims are in [engine-and-rules.md](engine-and-rules.md).

## `rules-ascendc`

Module `mcpp.rules.ascendc`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "ascend8.5+{dav-c220}"`, a constrained glob for `*.asc`. Compiles with BiSheng in MIXED mode, so the object carries the device binary and a host-callable launcher and joins the ordinary link -- no registration file and no device-link step. Its own engine needs are `.asc` in the device-source table and `mcpp::link_flag` for the `-rpath-link` the toolkit's shared libraries require, both 2026.9.6.5

## `rules-cuda`

Module `mcpp.rules.cuda`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "cuda…"`, a constrained glob for `*.cu`; the clang route with an LLVM toolchain, the nvcc route with a GCC one

The rule supplies toolkit search directories. The consumer selects the CUDA runtime link policy; for the Linux static-runtime arrangement used by the fixtures, the CUDA configuration states `ldflags = ["-lcudart_static", "-lrt", "-lpthread", "-ldl"]`.

## `rules-hip`

Module `mcpp.rules.hip`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "hip+{sm_89,sm_90}"`, a constrained glob for `*.hip`. On the NVIDIA platform HIP is a header layer over the CUDA runtime, so the compiler is the project's own clang and there is no ROCm on the machine

## `rules-metal`

Module `mcpp.rules.metal`; engine floor: 2026.9.8.1.

**Needs and behaviour.** the Metal toolchain of the macOS host's Xcode, located rather than installed: Xcode is not redistributable, so no payload is declared. `.metal` sources the project names on a macOS or iOS row become one `xcrun --sdk <sdk> metal` action per shader (`-MMD`, so an edited `#include` recompiles the shaders that include it) and one `xcrun --sdk <sdk> metallib` action per library, placed beside the program with `mcpp::deploy` under `metallib/`, which `dist-apple` maps into the bundle's resources. `compile(shaders)` compiles one source several times with definitions of its own, one library per `shader`; `options::library` links every shader into one library (`default` is the one `newDefaultLibrary` finds). Before planning anything the rule asks `xcrun --sdk <sdk> --show-sdk-path` and `--find metal` / `--find metallib`, and refuses naming the command that answered nothing, because a missing SDK and a missing compiler have different remedies (Xcode 26 installs the Metal toolchain as a separate component). A shader on any other row is refused naming the row. CI compiles the fixture on `macos-15` and checks each library's magic, and that a header edit recompiles only the shaders that include it

## `rules-slang`

Module `mcpp.rules.slang`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.7.1 before).

**Needs and behaviour.** `[build] accel = "vulkan1.2"`, a constrained glob for `*.slang`. Slang is a different language from GLSL rather than a second driver for it -- its own module system, generics, and targets beyond SPIR-V -- so it is a rule of its own. `.slang` is **not** in the engine's device-source table: this feature declares `device_extensions = [".slang"]` and `rule_module = "mcpp.rules.slang"`, and the engine routes it from there. That is the criterion for the whole arrangement -- a new device language costs no engine release. Since 0.7.0 it has the same `options::storage` axis as `rules-spirv` (header / object / sidecar), `options::extra_args` for the arguments the rule has no field for, and `options::per_file` for what one shader gets that the others do not -- a project with a `-fvk-use-gl-layout` and one shader needing `-emit-spirv-via-glsl` writes both without leaving one `compile()` call

## `rules-spirv`

Module `mcpp.rules.spirv`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "vulkan1.2"`, a constrained glob for the shader stages; compiles each shader through a `role = "source"` action and states which of the two compilers produced it

## `rules-swift`

Module `mcpp.rules.swift`; engine floor: 2026.9.8.1.

**Needs and behaviour.** the Swift compiler of the macOS host's Xcode or Command Line Tools, located rather than installed, as `rules-metal` locates its toolchain. From 0.12.0. The `.swift` sources a project names on a macOS or iOS row compile as one module named after the package: one whole-module `xcrun --sdk <sdk> swiftc -wmo -emit-object -target <triple>` action whose role is `object`, so the object joins every image of the package, and one `swiftc -typecheck -emit-objc-header-path` action whose role is `source`, whose directory `mcpp::include_dir` adds, so the package's C and C++ sources include `<module>-Swift.h`. `options::bridging_header` names a C header Swift sees without an import. The link receives the toolchain's `usr/lib/swift/<platform>` and the SDK's `usr/lib/swift` as search directories and `/usr/lib/swift` as a run path through `mcpp::link_flag`. Before planning anything the rule asks `xcrun --sdk <sdk> --show-sdk-path` and `--find swiftc`, and refuses naming the command that answered nothing; a Swift source on any other row is refused naming the row. Not supported: a Swift `import` of another package's module, and another package's C++ including this package's generated header, which both need an engine channel that publishes a package's interface directory to its dependents; and SwiftPM dependencies. CI builds `tests/swift-consumer` on `macos-15` -- a C++ program calling a `@_cdecl` Swift function that calls back into C -- and runs it

## `rules-sycl`

Module `mcpp.rules.sycl`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before); an llvm build needs 2026.10.26.1 (0.20.0).

**One C++ runtime, libstdc++ (0.20.0).** `libsycl.so` is compiled against libstdc++ and throws `sycl::exception` on it, and a typed `catch` does not match across two C++ runtimes in one process (mcpp#838). The program's own C++ library is therefore libstdc++: a gcc toolchain, or llvm with `gcc.libstdcxx` in the graph (`[target.'cfg(accelerator = "sycl")'.dependencies] gcc.libstdcxx = "16.1.0.1"`). On Linux the rule refuses any other C++ library before a unit is compiled, naming both remedies, and `compat:sycl-runtime` states the same requirement. The rule names no C++ library on the link line (0.19 added `-l:libstdc++.so.6` beside an llvm build's libc++); `--gcc-install-dir` follows the build's GCC: the graph's supplier (`MCPP_CXX_ABI_ROOT`), else the gcc toolchain's payload, else the `xim:gcc` the rule declares.

**Needs and behaviour.** `[build] accel = "sycl"` or `"sycl+{nvptx64-sm_89}"`, a constrained glob for `*.sycl`, and `compat:sycl-runtime` so the artifact can reach `libsycl.so.9` at run time. Its own engine need is `.sycl` in the device-source table, 2026.9.6.1


**SYCL targets belong to SYCL.** `sycl+{nvptx64-sm_89,nvptx64-sm_90}` produces both NVIDIA images. `sycl+{spir64,nvptx64-sm_89}` carries portable SPIR-V beside an NVIDIA image. `sycl` and `sycl+{spir64}` request portable SPIR-V; `sm_89` remains an alias for `nvptx64-sm_89`. An unknown target is refused before compilation. AMD `gfx*` requests are recognized but refused because this rule does not yet supply a ROCm compiler/runtime closure.

Each selected implementation owns its targets. A repository may retain CUDA and SYCL source groups while selecting only `sycl+{nvptx64-sm_89}`; the CUDA group is excluded by its source constraint and the CUDA rule need not be imported. Selecting `cuda+{sm_90},sycl+{nvptx64-sm_89}` selects both implementations with different targets. The rule passes the same target vector to compilation and device linking. Its CUDA compiler payload is requested only for a SYCL NVIDIA target; `options::cuda` accepts the same explicit choice as other rule tools.

The older `sycl,cuda+{sm_89}` form still inherits CUDA targets when the SYCL member has no targets, with a migration warning. Both named implementations remain selected. Explicit SYCL targets are never replaced by a CUDA member. Move the device target to the SYCL member to select only SYCL. GPU execution still requires a compatible device and driver; CI verifies the emitted images and the CPU fallback separately.

**A static library of several NVIDIA architectures is not supported.** DPC++ 7.1.0's `clang-offload-bundler` matches the five-part bundle IDs its driver writes (`sycl-nvptx64-nvidia-cuda-sm_89`) to every NVPTX request, so unbundling an archive for one architecture yields the members of all of them and the device link reports duplicate definitions (mcpp#899, an SDK defect). Objects of several architectures, a library of one architecture, and SPIR-V beside one NVIDIA architecture link. The rule keeps every requested architecture; it neither drops one nor reuses one architecture's code for another.


The `tests/multi-accel-consumer` fixture selects `cuda+{sm_90},sycl+{nvptx64-sm_89}` and imports both rules. CI inspects each rule's actual image target and runs both host objects without submitting GPU work. This establishes build/link coexistence with one C++ runtime; device execution, interoperation and synchronization require their own runtime checks.
