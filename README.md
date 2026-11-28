# mcpp-plugins

> The official build plugins for mcpp — one member, one feature, one module.

**English** | [简体中文](README.zh-CN.md)

[![ci](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-plugins)](https://github.com/mcpp-community/mcpp-plugins/releases)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

[Documentation](docs/README.md) · [mcpp](https://github.com/mcpp-community/mcpp) · [Package index](https://mcpplibs.github.io/mcpp-index/) · [Releases](https://github.com/mcpp-community/mcpp-plugins/releases)

mcpp is a general engine with a framework for build plugins; plugins come from
this package (`mcpp:plugins`), from third parties, or from the project itself,
and `build.mcpp` is where a project uses them: it imports the member's module
under the name the member declares, configures it, and calls it. The edge is a
`[build-dependencies]` entry — the library serves the build program, and the
package never reaches the target.

## Quick start

```toml
[build-dependencies.mcpp]
plugins = { version = "0.27.0", features = ["rules-spirv"] }
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

A third-party plugin names its modules `mcpp.<its namespace>.*`; `core`,
`plugins`, `deps`, `rules`, `dist`, `tools` belong to this package and the
engine. The full account of the families, and of how the engine routes a file
to a rule, is in [docs/00](docs/00-engine-and-rules.md); where a tool a member
runs comes from is in [docs/02](docs/02-tool-sources.md).

## Plugins

The most used:

| member | module | what it does | doc |
|---|---|---|---|
| `rules-cuda` | `mcpp.rules.cuda` | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [docs](docs/10-rules.md#rules-cuda) |
| `rules-sycl` | `mcpp.rules.sycl` | Compiles SYCL (`*.sycl`) with DPC++. | [docs](docs/10-rules.md#rules-sycl) |
| `rules-spirv` | `mcpp.rules.spirv` | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [docs](docs/10-rules.md#rules-spirv) |
| `rules-qt` | `mcpp.rules.qt` | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [docs](docs/11-rules-qt.md) |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | Installs a `vcpkg.json` manifest as an action with the toolset mcpp resolved, and maps the prefix into the build. | [docs](docs/40-deps.md#deps-vcpkg) |
| `dist-apk` | `mcpp.dist.apk` | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [docs](docs/32-dist-apk.md) |

<details>
<summary><b>All 23 members</b> — feature, module, mcpp floor, behaviour, doc, maintainer</summary>

| feature | module | mcpp floor | what it does | doc | maintainer |
|---|---|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.10.1.3 | Compiles Ascend C (`*.asc`) with BiSheng in mixed mode, so the object joins the ordinary link. | [rules](docs/10-rules.md#rules-ascendc) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.10.1.3 | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [rules](docs/10-rules.md#rules-cuda) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-hip` | `mcpp.rules.hip` | 2026.10.1.3 | Compiles HIP (`*.hip`) with the project's clang on the NVIDIA platform. | [rules](docs/10-rules.md#rules-hip) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-metal` | `mcpp.rules.metal` | 2026.9.8.1 | Compiles `.metal` shaders into Metal libraries with the host's Xcode and deploys them beside the program. | [rules](docs/10-rules.md#rules-metal) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-qt` | `mcpp.rules.qt` | 2026.10.1.3 | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [rules-qt](docs/11-rules-qt.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-qml` | `mcpp.rules.qml` | 2026.10.1.3; a pack that carries the QML plugins' libraries 2026.11.23.1 | Builds a QML application: each module's `moc`, `qmltyperegistrar`, `qmlcachegen`, `rcc` and `qsb` chain as actions, Qt's QML modules the program imports placed beside it, and a `qt.conf`. Implies `rules-qt`. | [rules-qml](docs/12-rules-qml.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-slang` | `mcpp.rules.slang` | 2026.10.1.3 | Compiles Slang (`*.slang`) and embeds or places the result. | [rules](docs/10-rules.md#rules-slang) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.10.1.3 | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [rules](docs/10-rules.md#rules-spirv) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-swift` | `mcpp.rules.swift` | 2026.9.8.1 | Compiles a package's `.swift` sources into one module the C and C++ sources call. | [rules](docs/10-rules.md#rules-swift) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.10.1.3 | Compiles SYCL (`*.sycl`) with DPC++. | [rules](docs/10-rules.md#rules-sycl) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | Writes a data file into a header the program compiles in. | [tools-embed](docs/20-tools-embed.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | Generates the `extern "C"` boundary and the C++ module of a code island. | [tools-island](docs/21-tools-island.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `tools-steps` | `mcpp.tools.steps` | 2026.10.1.3 | Describes an action whose work is more than one command as portable operations a backend runs; implied by `dist-apk` and `dist-apple`. | [tools-steps](docs/22-tools-steps.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.10.1.3 | Turns the tree `mcpp pack` stages into an AppImage (Linux). | [dist](docs/30-dist.md#dist-appimage) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-deb` | `mcpp.dist.deb` | 2026.10.30.1 | Installs the staged tree under `/opt/<name>` with a launcher, a desktop entry and an icon, as a Debian package (Linux targets). | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-rpm` | `mcpp.dist.rpm` | 2026.10.30.1 | The same installation as an RPM package (Linux targets). | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-wix` | `mcpp.dist.wix` | 2026.10.1.3 | Builds an MSI of the staged tree, and a Burn bundle chaining it (Windows). | [dist](docs/30-dist.md#dist-wix) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.14.2 | Lays out a macOS or iOS application bundle, signs it and writes a disk image. | [dist-apple](docs/31-dist-apple.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-web` | `mcpp.dist.web` | 2026.9.13.1 | Copies a `wasm32-emscripten` program and its files into a web directory with an `index.html`. | [dist](docs/30-dist.md#dist-web) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-apk` | `mcpp.dist.apk` | 2026.10.1.3 | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [dist-apk](docs/32-dist-apk.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 2026.10.1.3 | Installs a `vcpkg.json` manifest as an action with the toolset mcpp resolved, and maps the prefix into the build. | [deps](docs/40-deps.md#deps-vcpkg) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `deps-cmake` | `mcpp.deps.cmake` | 2026.10.1.3; a cross build 2026.10.30.1 | Builds and installs a CMake subproject as an action with Ninja and the toolset mcpp resolved -- for another target, the compilers and words of the engine's contract -- keeps the installation for any build with the same key, and maps the prefix into the build. | [deps](docs/40-deps.md#deps-cmake) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `deps-archive` | `mcpp.deps.archive` | 2026.10.1.3 | Extracts a zip archive the project keeps and places its tree beside the program. | [deps](docs/40-deps.md#deps-archive) | [@Sunrisepeak](https://github.com/Sunrisepeak) |

Some features add a sub-capability: `dist-apk-kotlin`, `dist-apk-maven`, and
`deps`, which the deps members imply. `plugins-core` — `mcpp.plugins.declare`,
`mcpp.plugins.toolset`, `mcpp.plugins.fs`, `mcpp.plugins.tool` — is the general
library every member implies; `plugins-testing`, `plugins-toolchain`,
`plugins-platform` and `plugins-progress` add their own modules. Their layers
and the engine floor are in
[docs/01 §3](docs/01-plugin-development.md#3-layers-names-and-the-engine-floor).

</details>

## Contributing and maintenance

**Maintainers.** The package and, today, every member:
[@Sunrisepeak](https://github.com/Sunrisepeak). Review routing by family:
[`.github/CODEOWNERS`](.github/CODEOWNERS) — a new maintainer takes a family
by editing one line.

**Contributing.** A change goes through a pull request from a branch; one pull
request does one thing; commits and text are English. A new member is a module
(or a directory when it outgrows one file — [docs/01
§5](docs/01-plugin-development.md#5-a-new-plugin)), a feature in `mcpp.toml`,
a consumer under `tests/` that CI builds and asserts on, a row in the table
above, a page under `docs/`, and a version bump. The conventions a member
follows are in [docs/01](docs/01-plugin-development.md); the division between
what the engine owns and what the member owns is in
[docs/00](docs/00-engine-and-rules.md#adding-a-member-what-the-engine-owns-and-what-the-member-owns).
