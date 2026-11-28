# mcpp-plugins

> The official build plugins for mcpp — one member, one feature, one module.

**English** | [简体中文](README.zh-CN.md)

[![ci](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-plugins)](https://github.com/mcpp-community/mcpp-plugins/releases)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

mcpp is a general engine with a framework for build plugins; plugins come from
this package (`mcpp:plugins`), from third parties, or from the project itself,
and `build.mcpp` is where a project uses them: it imports the member's module
under the name the member declares, configures it, and calls it. The edge is a
`[build-dependencies]` entry, not a `[dependencies]` entry: `host-module = true`
states which build-time product is wanted, and the section states that the
package does not reach the target — a rule's library is never linked into the
artifact (mcpp docs/05 §2.6.1).

## Quick start

```toml
[build-dependencies.mcpp]
plugins = { version = "0.27.0", features = ["rules-spirv"], host-module = true }
```

```cpp
// build.mcpp
import std;
import mcpp;
import mcpp.rules.spirv;

int main() {
    mcpp::rules::spirv::options opt;
    opt.includes = { "shaders" };
    return mcpp::rules::spirv::compile(opt) ? 0 : 1;
}
```

## Families

| family | module | answers |
|---|---|---|
| rules | `mcpp.rules.<x>` | how one kind of translation unit is compiled by a compiler mcpp does not drive |
| tools | `mcpp.tools.<x>` | what the build program does itself (see [`tools/README.md`](tools/README.md)) |
| dist | `mcpp.dist.<x>` | what comes out of the link, and in what form a user installs it |
| deps | `mcpp.deps.<x>` | where a library comes from |
| identity | `mcpp.plugins` | the lib root, compiled before every member; it states the collection's version |

A third-party plugin names its modules `mcpp.<its namespace>.*`; the second
segments `core`, `plugins`, `deps`, `rules`, `dist`, `tools` belong to this
package and the engine. The full account of the families, and of how the engine
routes a file to a rule, is in [docs/00](docs/00-engine-and-rules.md); where a
tool a member runs comes from is in [docs/02](docs/02-tool-sources.md).

## Members

From 0.21.0 the package as a whole needs mcpp 2026.10.30.1 (`[package] mcpp`);
the column states the release each member's behaviour first needed. Cross CMake
subprojects that link executables or shared libraries require mcpp
2026.11.7.1 or later, and cross configuration requires CMake 3.19 or later.

<details>
<summary><b>The 23 members</b> — feature, module, mcpp floor, behaviour, doc, maintainer</summary>

| feature | module | mcpp floor | what it does | doc | maintainer |
|---|---|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.10.1.3 | Compiles Ascend C (`*.asc`) with BiSheng in mixed mode, so the object joins the ordinary link. | [rules](docs/10-rules.md#rules-ascendc) | @Sunrisepeak |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.10.1.3 | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [rules](docs/10-rules.md#rules-cuda) | @Sunrisepeak |
| `rules-hip` | `mcpp.rules.hip` | 2026.10.1.3 | Compiles HIP (`*.hip`) with the project's clang on the NVIDIA platform. | [rules](docs/10-rules.md#rules-hip) | @Sunrisepeak |
| `rules-metal` | `mcpp.rules.metal` | 2026.9.8.1 | Compiles `.metal` shaders into Metal libraries with the host's Xcode and deploys them beside the program. | [rules](docs/10-rules.md#rules-metal) | @Sunrisepeak |
| `rules-qt` | `mcpp.rules.qt` | 2026.10.1.3 | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [rules-qt](docs/11-rules-qt.md) | @Sunrisepeak |
| `rules-qml` | `mcpp.rules.qml` | 2026.10.1.3; a pack that carries the QML plugins' libraries 2026.11.23.1 | Builds a QML application: each module's `moc`, `qmltyperegistrar`, `qmlcachegen`, `rcc` and `qsb` chain as actions, Qt's QML modules the program imports placed beside it, and a `qt.conf`. Implies `rules-qt`. | [rules-qml](docs/12-rules-qml.md) | @Sunrisepeak |
| `rules-slang` | `mcpp.rules.slang` | 2026.10.1.3 | Compiles Slang (`*.slang`) and embeds or places the result. | [rules](docs/10-rules.md#rules-slang) | @Sunrisepeak |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.10.1.3 | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [rules](docs/10-rules.md#rules-spirv) | @Sunrisepeak |
| `rules-swift` | `mcpp.rules.swift` | 2026.9.8.1 | Compiles a package's `.swift` sources into one module the C and C++ sources call. | [rules](docs/10-rules.md#rules-swift) | @Sunrisepeak |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.10.1.3 | Compiles SYCL (`*.sycl`) with DPC++. | [rules](docs/10-rules.md#rules-sycl) | @Sunrisepeak |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | Writes a data file into a header the program compiles in. | [tools-embed](docs/20-tools-embed.md) | @Sunrisepeak |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | Generates the `extern "C"` boundary and the C++ module of a code island. | [tools-island](docs/21-tools-island.md) | @Sunrisepeak |
| `tools-steps` | `mcpp.tools.steps` | 2026.10.1.3 | Describes an action whose work is more than one command as portable operations a backend runs; implied by `dist-apk` and `dist-apple`. | [tools-steps](docs/22-tools-steps.md) | @Sunrisepeak |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.10.1.3 | Turns the tree `mcpp pack` stages into an AppImage (Linux). | [dist](docs/30-dist.md#dist-appimage) | @Sunrisepeak |
| `dist-deb` | `mcpp.dist.deb` | 2026.10.30.1 | Installs the staged tree under `/opt/<name>` with a launcher, a desktop entry and an icon, as a Debian package (Linux targets). | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | @Sunrisepeak |
| `dist-rpm` | `mcpp.dist.rpm` | 2026.10.30.1 | The same installation as an RPM package (Linux targets). | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | @Sunrisepeak |
| `dist-wix` | `mcpp.dist.wix` | 2026.10.1.3 | Builds an MSI of the staged tree, and a Burn bundle chaining it (Windows). | [dist](docs/30-dist.md#dist-wix) | @Sunrisepeak |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.14.2 | Lays out a macOS or iOS application bundle, signs it and writes a disk image. | [dist-apple](docs/31-dist-apple.md) | @Sunrisepeak |
| `dist-web` | `mcpp.dist.web` | 2026.9.13.1 | Copies a `wasm32-emscripten` program and its files into a web directory with an `index.html`. | [dist](docs/30-dist.md#dist-web) | @Sunrisepeak |
| `dist-apk` | `mcpp.dist.apk` | 2026.10.1.3 | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [dist-apk](docs/32-dist-apk.md) | @Sunrisepeak |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 2026.10.1.3 | Installs a `vcpkg.json` manifest as an action with the toolset mcpp resolved, and maps the prefix into the build. | [deps](docs/40-deps.md#deps-vcpkg) | @Sunrisepeak |
| `deps-cmake` | `mcpp.deps.cmake` | 2026.10.1.3; a cross build 2026.10.30.1 | Builds and installs a CMake subproject as an action with Ninja and the toolset mcpp resolved -- for another target, the compilers and words of the engine's contract -- keeps the installation for any build with the same key, and maps the prefix into the build. | [deps](docs/40-deps.md#deps-cmake) | @Sunrisepeak |
| `deps-archive` | `mcpp.deps.archive` | 2026.10.1.3 | Extracts a zip archive the project keeps and places its tree beside the program. | [deps](docs/40-deps.md#deps-archive) | @Sunrisepeak |

Some features add a sub-capability to a member: `dist-apk-kotlin` and
`dist-apk-maven` (Kotlin sources, a Maven graph), and `deps`, which the deps
members imply.

**The general library.** `plugins-core` gives a build program
`mcpp.plugins.declare`, `mcpp.plugins.toolset`, `mcpp.plugins.fs` and
`mcpp.plugins.tool`; every member implies it, and a build program that uses
only these modules names it on the dependency edge. `plugins-testing`,
`plugins-toolchain` and `plugins-platform` add `mcpp.plugins.testing`,
`mcpp.plugins.toolchain` and `mcpp.plugins.platform` (with
`mcpp::plugins::package`); `plugins-progress` adds the optional action
telemetry and its atomic snapshot writer; `mcpp.plugins.accel` reads
implementation-owned target requests. The layers, names and the engine floor
are in [docs/01 §3](docs/01-plugin-development.md#3-layers-names-and-the-engine-floor).
`surface`, the name of `plugins-core` before 0.17.0, is kept until 2027-03-28.

</details>

## Layout

```
mcpp.toml            the package: one feature per member
src/plugins.cppm     export module mcpp.plugins;  the version, mcpp::plugins::surface
                     (the declarations a consumer names), mcpp::plugins::xml and
                     mcpp::plugins::package (a staged package's metadata)
src/declare.cppm,    the general library for build programs (`plugins-core`):
src/toolset.cppm,    mcpp.plugins.declare, mcpp.plugins.toolset, mcpp.plugins.fs
src/fs.cppm
src/testing.cppm     mcpp.plugins.testing (`plugins-testing`)
src/platform.cppm    mcpp.plugins.platform (`plugins-platform`)
src/compat/, deps/compat/
                     compatibility units, each retired on the date its header states
rules/<x>.cppm       export module mcpp.rules.<x>;
tools/<x>.cppm       export module mcpp.tools.<x>;
dist/<x>.cppm        export module mcpp.dist.<x>;
deps/<x>.cppm        export module mcpp.deps.<x>;  deps/deps.cppm is shared
tests/<consumer>/    one project per member, built by CI with the pinned mcpp
docs/                one page per member or group of members ([index](docs/README.md))
```

## Adding a member

A member is one module file, one feature in `mcpp.toml`, a consumer under
`tests/` that CI builds and asserts on, a row in the table above with its mcpp
floor, a page under `docs/`, and a version bump. Its code is the C++23 that GCC
with libstdc++, clang with libc++ and an editor's clangd with libstdc++ all
accept, and CI builds or reads every fixture under each of the three. The
conventions a member follows are in
[docs/01](docs/01-plugin-development.md); the division between what the engine
owns and what the member owns is in
[docs/00](docs/00-engine-and-rules.md#adding-a-member-what-the-engine-owns-and-what-the-member-owns).

## Releases

A tag `v<version>` publishes the source archive; the index descriptor
(`mcpp-index/pkgs/mcpp/mcpp.plugins.lua`) names the GitHub archive and its
GitCode mirror with one sha256.
