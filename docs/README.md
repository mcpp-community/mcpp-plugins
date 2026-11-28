# mcpp-plugins Documentation

This tree is the manual for the plugins of `mcpp:plugins` and for the
conventions a build plugin follows. Each page states what a plugin does, how a
consumer writes it, and what its current limits are. The reasoning behind a
design, and what was measured and refuted on the way, lives in the dated
records under [`../.agents/docs/`](../.agents/docs/) — a page cites no record.

## Start here

| To | Read |
|---|---|
| use a plugin | its page below, and the [quick start](../README.md#quick-start) in the README |
| write a plugin of your own | [01 — Plugin Development](01-plugin-development.md) |
| maintain or take a plugin | [03 — Maintainers](03-maintainers.md) |
| know why a name, a floor or a claim is what it is | [00 — How the Plugins Meet the Engine](00-engine-and-rules.md) |
| name a tool a plugin runs | [02 — Where a Tool Comes From](02-tool-sources.md) |

## The plugins

The README's tables carry feature, behaviour, doc and maintainer; this is the
same list with the module each feature compiles and the mcpp release its
behaviour first needed. From 0.21.0 the package as a whole needs mcpp
2026.10.30.1 (`[package] mcpp`); cross CMake subprojects that link executables
or shared libraries require mcpp 2026.11.7.1 or later, and cross configuration
requires CMake 3.19 or later.

| feature | module | mcpp floor | what it does | doc |
|---|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.10.1.3 | Compiles Ascend C (`*.asc`) with BiSheng in mixed mode, so the object joins the ordinary link. | [rules](10-rules.md#rules-ascendc) |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.10.1.3 | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [rules](10-rules.md#rules-cuda) |
| `rules-hip` | `mcpp.rules.hip` | 2026.10.1.3 | Compiles HIP (`*.hip`) with the project's clang on the NVIDIA platform. | [rules](10-rules.md#rules-hip) |
| `rules-metal` | `mcpp.rules.metal` | 2026.9.8.1 | Compiles `.metal` shaders into Metal libraries with the host's Xcode and deploys them beside the program. | [rules](10-rules.md#rules-metal) |
| `rules-qt` | `mcpp.rules.qt` | 2026.10.1.3 | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [rules-qt](11-rules-qt.md) |
| `rules-qml` | `mcpp.rules.qml` | 2026.10.1.3; a pack that carries the QML plugins' libraries 2026.11.23.1 | Builds a QML application: each module's `moc`, `qmltyperegistrar`, `qmlcachegen`, `rcc` and `qsb` chain as actions, Qt's QML modules the program imports placed beside it, and a `qt.conf`. Implies `rules-qt`. | [rules-qml](12-rules-qml.md) |
| `rules-slang` | `mcpp.rules.slang` | 2026.10.1.3 | Compiles Slang (`*.slang`) and embeds or places the result. | [rules](10-rules.md#rules-slang) |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.10.1.3 | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [rules](10-rules.md#rules-spirv) |
| `rules-swift` | `mcpp.rules.swift` | 2026.9.8.1 | Compiles a package's `.swift` sources into one module the C and C++ sources call. | [rules](10-rules.md#rules-swift) |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.10.1.3 | Compiles SYCL (`*.sycl`) with DPC++. | [rules](10-rules.md#rules-sycl) |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | Writes a data file into a header the program compiles in. | [tools-embed](20-tools-embed.md) |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | Generates the `extern "C"` boundary and the C++ module of a code island. | [tools-island](21-tools-island.md) |
| `tools-steps` | `mcpp.tools.steps` | 2026.10.1.3 | Describes an action whose work is more than one command as portable operations a backend runs; implied by `dist-apk` and `dist-apple`. | [tools-steps](22-tools-steps.md) |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.10.1.3 | Turns the tree `mcpp pack` stages into an AppImage (Linux). | [dist](30-dist.md#dist-appimage) |
| `dist-deb` | `mcpp.dist.deb` | 2026.10.30.1 | Installs the staged tree under `/opt/<name>` with a launcher, a desktop entry and an icon, as a Debian package (Linux targets). | [dist](30-dist.md#dist-deb-and-dist-rpm) |
| `dist-rpm` | `mcpp.dist.rpm` | 2026.10.30.1 | The same installation as an RPM package (Linux targets). | [dist](30-dist.md#dist-deb-and-dist-rpm) |
| `dist-wix` | `mcpp.dist.wix` | 2026.10.1.3 | Builds an MSI of the staged tree, and a Burn bundle chaining it (Windows). | [dist](30-dist.md#dist-wix) |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.14.2 | Lays out a macOS or iOS application bundle, signs it and writes a disk image. | [dist-apple](31-dist-apple.md) |
| `dist-web` | `mcpp.dist.web` | 2026.9.13.1 | Copies a `wasm32-emscripten` program and its files into a web directory with an `index.html`. | [dist](30-dist.md#dist-web) |
| `dist-apk` | `mcpp.dist.apk` | 2026.10.1.3 | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [dist-apk](32-dist-apk.md) |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 2026.10.1.3 | Installs a `vcpkg.json` manifest as an action with the toolset mcpp resolved, and maps the prefix into the build. | [deps](40-deps.md#deps-vcpkg) |
| `deps-cmake` | `mcpp.deps.cmake` | 2026.10.1.3; a cross build 2026.10.30.1 | Builds and installs a CMake subproject as an action with Ninja and the toolset mcpp resolved -- for another target, the compilers and words of the engine's contract -- keeps the installation for any build with the same key, and maps the prefix into the build. | [deps](40-deps.md#deps-cmake) |
| `deps-archive` | `mcpp.deps.archive` | 2026.10.1.3 | Extracts a zip archive the project keeps and places its tree beside the program. | [deps](40-deps.md#deps-archive) |

Some features add a sub-capability: `dist-apk-kotlin` and `dist-apk-maven`
(Kotlin sources, a Maven graph), and `deps`, which the deps plugins imply.

**The general library.** `plugins-core` — `mcpp.plugins.declare`,
`mcpp.plugins.toolset`, `mcpp.plugins.fs`, `mcpp.plugins.tool` — is the
general library every plugin implies, and the modules a third-party plugin
built on it names on its dependency edge; `plugins-testing`,
`plugins-toolchain`, `plugins-platform` and `plugins-progress` add their own.
The layers, names and the engine floor are in
[01 §3](01-plugin-development.md#3-layers-names-and-the-engine-floor);
`surface`, the name of `plugins-core` before 0.17.0, is kept until
2027-03-28.

## Pages

The first digit is the part, so a number says where a page belongs:

| | |
|---|---|
| `0x` | the framework: naming, conventions, where a tool comes from |
| `1x` | the rules: device and language compilation |
| `2x` | the tools: what the build program does itself |
| `3x` | the distribution: what comes out of the link |
| `4x` | the dependencies: where a library comes from |

### 0x — The framework

- [00 — How the Plugins Meet the Engine](00-engine-and-rules.md) — the families and their names, each rule's environment and claimed extensions, the engine floor, what a consumer names
- [01 — Plugin Development](01-plugin-development.md) — the conventions every plugin follows, the C++ a plugin is written in, a new plugin's checklist
- [02 — Where a Tool Comes From](02-tool-sources.md) — the one order every plugin resolves a tool in, and every way a project names its own
- [03 — Maintainers](03-maintainers.md) — the participation path, what a maintainer does and knows, how to apply

### 1x — Rules

- [10 — Device and Language Rules](10-rules.md) — `rules-ascendc`, `rules-cuda`, `rules-hip`, `rules-metal`, `rules-slang`, `rules-spirv`, `rules-swift`, `rules-sycl`
- [11 — `rules-qt`](11-rules-qt.md) — Qt's code generators, modules and runtime; Linguist tools
- [12 — `rules-qml`](12-rules-qml.md) — a QML application on `rules-qt`

### 2x — Tools

- [20 — `tools-embed`](20-tools-embed.md) — a data file compiled into the program
- [21 — `tools-island`](21-tools-island.md) — the `extern "C"` boundary and module of a code island
- [22 — `tools-steps`](22-tools-steps.md) — an action whose work is more than one command

### 3x — Distribution

- [30 — `dist-appimage`, `dist-deb`, `dist-rpm`, `dist-wix`, `dist-web`](30-dist.md)
- [31 — `dist-apple`](31-dist-apple.md) — a macOS or iOS bundle, signed, as a disk image
- [32 — `dist-apk`](32-dist-apk.md) — a signed APK or App Bundle, with Java, Kotlin and Maven libraries

### 4x — Dependencies

- [40 — `deps-vcpkg`, `deps-cmake`, `deps-archive`](40-deps.md)
