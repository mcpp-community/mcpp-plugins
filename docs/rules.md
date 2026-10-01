# Device and language rules

The rules that compile one kind of translation unit with a compiler mcpp does not drive: GPU and accelerator languages (`rules-ascendc`, `rules-cuda`, `rules-hip`, `rules-sycl`), shader languages (`rules-spirv`, `rules-slang`, `rules-metal`) and Swift (`rules-swift`). Each section states the module, the engine floor, what the rule needs and how it compiles. The environment each rule brings and the extensions it claims are in [engine-and-rules.md](engine-and-rules.md).

## `rules-ascendc`

Module `mcpp.rules.ascendc`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "ascend8.5+{dav-c220}"`, a constrained glob for `*.asc`. Compiles with BiSheng in MIXED mode, so the object carries the device binary and a host-callable launcher and joins the ordinary link -- no registration file and no device-link step. Its own engine needs are `.asc` in the device-source table and `mcpp::link_flag` for the `-rpath-link` the toolkit's shared libraries require, both 2026.9.6.5

## `rules-cuda`

Module `mcpp.rules.cuda`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "cuda…"`, a constrained glob for `*.cu`; the clang route with an LLVM toolchain, the nvcc route with a GCC one

## `rules-hip`

Module `mcpp.rules.hip`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "hip, cuda12.9+{sm_89}"`, a constrained glob for `*.hip`. On the NVIDIA platform HIP is a header layer over the CUDA runtime, so the compiler is the project's own clang and there is no ROCm on the machine

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

Module `mcpp.rules.sycl`; engine floor: 2026.10.1.3 (0.19.0, mcpp#755; 2026.9.6.6 before).

**Needs and behaviour.** `[build] accel = "sycl"` or `"sycl, cuda12.9+{sm_89}"`, a constrained glob for `*.sycl`, and `compat:sycl-runtime` so the artifact can reach `libsycl.so.9` at run time. Its own engine need is `.sycl` in the device-source table, 2026.9.6.1
