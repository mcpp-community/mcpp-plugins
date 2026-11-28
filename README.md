# mcpp-plugins

> The official build plugins for mcpp — one plugin, one feature, one module.

[![ci](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-plugins)](https://github.com/mcpp-community/mcpp-plugins/releases)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

**English** | [简体中文](README.zh-CN.md) · [Documentation](docs/README.md) · [mcpp](https://github.com/mcpp-community/mcpp) · [mcpp-index](https://mcpp.index.xlings.org) · [Releases](https://github.com/mcpp-community/mcpp-plugins/releases)

mcpp is a general engine with a framework for build plugins; plugins come from
this package (`mcpp:plugins`), from third parties, or from the project itself,
and `build.mcpp` is where a project uses them: it imports the plugin's module
under the name the plugin declares, configures it, and calls it. The edge is a
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
| identity | `mcpp.plugins` | the lib root, compiled before every plugin; it states the collection's version |

A third-party plugin names its modules `mcpp.<its namespace>.*`; `core`,
`plugins`, `deps`, `rules`, `dist`, `tools` belong to this package and the
engine. The full account of the families, and of how the engine routes a file
to a rule, is in [docs/00](docs/00-engine-and-rules.md); where a tool a plugin
runs comes from is in [docs/02](docs/02-tool-sources.md).

## Plugins

| feature | what it does | doc |
|---|---|---|
| `rules-cuda` | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [docs](docs/10-rules.md#rules-cuda) |
| `rules-sycl` | Compiles SYCL (`*.sycl`) with DPC++. | [docs](docs/10-rules.md#rules-sycl) |
| `rules-spirv` | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [docs](docs/10-rules.md#rules-spirv) |
| `rules-qt` | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [docs](docs/11-rules-qt.md) |
| `deps-vcpkg` | Installs a `vcpkg.json` manifest as an action with the toolset mcpp resolved, and maps the prefix into the build. | [docs](docs/40-deps.md#deps-vcpkg) |
| `dist-apk` | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [docs](docs/32-dist-apk.md) |

<details>
<summary><b>All 23 plugins</b> — feature, behaviour, doc; the modules and engine floors are in the <a href="docs/README.md#the-plugins">documentation index</a></summary>

| feature | what it does | doc |
|---|---|---|
| `rules-ascendc` | Compiles Ascend C (`*.asc`) with BiSheng in mixed mode, so the object joins the ordinary link. | [docs](docs/10-rules.md#rules-ascendc) |
| `rules-cuda` | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [docs](docs/10-rules.md#rules-cuda) |
| `rules-hip` | Compiles HIP (`*.hip`) with the project's clang on the NVIDIA platform. | [docs](docs/10-rules.md#rules-hip) |
| `rules-metal` | Compiles `.metal` shaders into Metal libraries with the host's Xcode and deploys them beside the program. | [docs](docs/10-rules.md#rules-metal) |
| `rules-qt` | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [docs](docs/11-rules-qt.md) | 
| `rules-qml` | Builds a QML application: each module's `moc`, `qmltyperegistrar`, `qmlcachegen`, `rcc` and `qsb` chain as actions, Qt's QML modules the program imports placed beside it, and a `qt.conf`. Implies `rules-qt`. | [docs](docs/12-rules-qml.md) |
| `rules-slang` | Compiles Slang (`*.slang`) and embeds or places the result. | [docs](docs/10-rules.md#rules-slang) |
| `rules-spirv` | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [docs](docs/10-rules.md#rules-spirv) |
| `rules-swift` | Compiles a package's `.swift` sources into one module the C and C++ sources call. | [docs](docs/10-rules.md#rules-swift) |
| `rules-sycl` | Compiles SYCL (`*.sycl`) with DPC++. | [docs](docs/10-rules.md#rules-sycl) |
| `tools-embed` | Writes a data file into a header the program compiles in. | [docs](docs/20-tools-embed.md) |
| `tools-island` | Generates the `extern "C"` boundary and the C++ module of a code island. | [docs](docs/21-tools-island.md) |
| `tools-steps` | Describes an action whose work is more than one command as portable operations a backend runs; implied by `dist-apk` and `dist-apple`. | [docs](docs/22-tools-steps.md) |
| `dist-appimage` | Turns the tree `mcpp pack` stages into an AppImage (Linux). | [docs](docs/30-dist.md#dist-appimage) |
| `dist-deb` | Installs the staged tree under `/opt/<name>` with a launcher, a desktop entry and an icon, as a Debian package (Linux targets). | [docs](docs/30-dist.md#dist-deb-and-dist-rpm) |
| `dist-rpm` | The same installation as an RPM package (Linux targets). | [docs](docs/30-dist.md#dist-deb-and-dist-rpm) |
| `dist-wix` | Builds an MSI of the staged tree, and a Burn bundle chaining it (Windows). | [docs](docs/30-dist.md#dist-wix) |
| `dist-apple` | Lays out a macOS or iOS application bundle, signs it and writes a disk image. | [docs](docs/31-dist-apple.md) |
| `dist-web` | Copies a `wasm32-emscripten` program and its files into a web directory with an `index.html`. | [docs](docs/30-dist.md#dist-web) |
| `dist-apk` | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [docs](docs/32-dist-apk.md) |
| `deps-vcpkg` | Installs a `vcpkg.json` manifest as an action with the toolset mcpp resolved, and maps the prefix into the build. | [docs](docs/40-deps.md#deps-vcpkg) |
| `deps-cmake` | Builds and installs a CMake subproject as an action with Ninja and the toolset mcpp resolved, and maps the prefix into the build. | [docs](docs/40-deps.md#deps-cmake) |
| `deps-archive` | Extracts a zip archive the project keeps and places its tree beside the program. | [docs](docs/40-deps.md#deps-archive) |

</details>

## Contributing and maintenance

The project is maintained mainly by
[@Sunrisepeak](https://github.com/Sunrisepeak) — its upkeep and growth need
more contributors and maintainers. If some plugins interest you and you want
to contribute and maintain, start from the simple and from what interests
you: use them, introduce and teach them, report issues, fix bugs, improve
them, maintain one or more of them:

- [propose or submit a new plugin](https://github.com/mcpp-community/mcpp-plugins/issues/new?template=new-plugin.yml)
- [apply to maintain one](https://github.com/mcpp-community/mcpp-plugins/issues/new?template=maintainer-application.yml)

## Maintainers

| maintainer | plugins primarily maintained |
|---|---|
| [@Sunrisepeak](https://github.com/Sunrisepeak) | primary maintainer of the project |
