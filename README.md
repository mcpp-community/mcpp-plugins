# mcpp-plugins

The build plugins the mcpp project maintains, published as one package,
`mcpp:plugins`. mcpp is a general engine with a framework for build plugins;
plugins come from this package, from third parties, or from the project
itself, and `build.mcpp` is where a project uses them. A consumer selects the
members it needs through features, imports each one from `build.mcpp` under the
module name the member declares, and configures it there.

```toml
[build-dependencies.mcpp]
plugins = { version = "0.15.0", features = ["rules-spirv"], host-module = true }
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

The edge is a `[build-dependencies]` entry, not a `[dependencies]` entry:
`host-module = true` states which build-time product is wanted, and the section
states that the package does not reach the target. A rule's library is never
linked into the artifact (mcpp docs/05 §2.6.1).

## Naming

| family | module | answers |
|---|---|---|
| rules | `mcpp.rules.<x>` | how one kind of translation unit is compiled by a compiler mcpp does not drive |
| tools | `mcpp.tools.<x>` | what the build program does itself (see [`tools/README.md`](tools/README.md)) |
| dist | `mcpp.dist.<x>` | what comes out of the link, and in what form a user installs it |
| deps | `mcpp.deps.<x>` | where a library comes from |
| identity | `mcpp.plugins` | the lib root, compiled before every member; it states the collection's version |

The `mcpp.` prefix is reserved for this package. The full account of the
families, and of how the engine routes a file to a rule, is in
[docs/engine-and-rules.md](docs/engine-and-rules.md).

## Members

| feature | module | mcpp floor | what it does | doc |
|---|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.9.6.6 | Compiles Ascend C (`*.asc`) with BiSheng in mixed mode, so the object joins the ordinary link. | [rules](docs/rules.md#rules-ascendc) |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.9.6.6 | Compiles CUDA (`*.cu`) through clang with an LLVM toolchain or nvcc with a GCC one. | [rules](docs/rules.md#rules-cuda) |
| `rules-hip` | `mcpp.rules.hip` | 2026.9.6.6 | Compiles HIP (`*.hip`) with the project's clang on the NVIDIA platform. | [rules](docs/rules.md#rules-hip) |
| `rules-metal` | `mcpp.rules.metal` | 2026.9.8.1 | Compiles `.metal` shaders into Metal libraries with the host's Xcode and deploys them beside the program. | [rules](docs/rules.md#rules-metal) |
| `rules-qt` | `mcpp.rules.qt` | 2026.9.26.2 | Runs `moc`, `uic`, `rcc` and Qt's Linguist tools as actions, links the Qt modules and places their runtime. | [rules-qt](docs/rules-qt.md) |
| `rules-slang` | `mcpp.rules.slang` | 2026.9.7.1 | Compiles Slang (`*.slang`) and embeds or places the result. | [rules](docs/rules.md#rules-slang) |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.9.6.6 | Compiles GLSL and HLSL shader stages to SPIR-V and embeds or places the result. | [rules](docs/rules.md#rules-spirv) |
| `rules-swift` | `mcpp.rules.swift` | 2026.9.8.1 | Compiles a package's `.swift` sources into one module the C and C++ sources call. | [rules](docs/rules.md#rules-swift) |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.9.6.6 | Compiles SYCL (`*.sycl`) with DPC++. | [rules](docs/rules.md#rules-sycl) |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | Writes a data file into a header the program compiles in. | [tools-embed](docs/tools-embed.md) |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | Generates the `extern "C"` boundary and the C++ module of a code island. | [tools-island](docs/tools-island.md) |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.9.11.1 | Turns the tree `mcpp pack` stages into an AppImage (Linux). | [dist](docs/dist.md#dist-appimage) |
| `dist-wix` | `mcpp.dist.wix` | 2026.9.11.1 | Builds an MSI of the staged tree, and a Burn bundle chaining it (Windows). | [dist](docs/dist.md#dist-wix) |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.14.2 | Lays out a macOS or iOS application bundle, signs it and writes a disk image. | [dist-apple](docs/dist-apple.md) |
| `dist-web` | `mcpp.dist.web` | 2026.9.13.1 | Copies a `wasm32-emscripten` program and its files into a web directory with an `index.html`. | [dist](docs/dist.md#dist-web) |
| `dist-apk` | `mcpp.dist.apk` | 2026.9.14.2 | Packs the native closure into a signed APK or App Bundle, with Java, Kotlin and Maven libraries. | [dist-apk](docs/dist-apk.md) |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 2026.9.26.2 | Installs a `vcpkg.json` manifest as an action and maps the prefix into the build. | [deps](docs/deps.md#deps-vcpkg) |
| `deps-cmake` | `mcpp.deps.cmake` | 2026.9.26.2 | Builds and installs a CMake subproject as an action and maps the prefix into the build. | [deps](docs/deps.md#deps-cmake) |
| `deps-archive` | `mcpp.deps.archive` | 2026.9.26.2 | Extracts a zip archive the project keeps and places its tree beside the program. | [deps](docs/deps.md#deps-archive) |

Some features add a sub-capability to a member: `dist-apk-kotlin` and
`dist-apk-maven` (Kotlin sources, a Maven graph), and `surface` and `deps`,
which the members imply. A feature states a mechanism and the tools that
mechanism runs; the libraries and SDKs a program links are the project's
declaration (0.15.0 removed `rules-qt-xim*`; see [rules-qt](docs/rules-qt.md)).

## Layout

```
mcpp.toml            the package: one feature per member
src/plugins.cppm     export module mcpp.plugins;  the version, mcpp::plugins::surface
                     (the declarations a consumer names) and mcpp::plugins::xml
rules/<x>.cppm       export module mcpp.rules.<x>;
tools/<x>.cppm       export module mcpp.tools.<x>;
dist/<x>.cppm        export module mcpp.dist.<x>;
deps/<x>.cppm        export module mcpp.deps.<x>;  deps/deps.cppm is shared
tests/<consumer>/    one project per member, built by CI with the pinned mcpp
docs/                one page per member or group of members
```

## Adding a member

A member is one module file, one feature in `mcpp.toml`, a consumer under
`tests/` that CI builds and asserts on, a row in the table above with its mcpp
floor, and a version bump. The conventions a member follows -- the same for a
third-party or a project's own plugin -- are in
[docs/plugin-development.md](docs/plugin-development.md); the division between
what the engine owns and what the member owns is in
[docs/engine-and-rules.md](docs/engine-and-rules.md#adding-a-member-what-the-engine-owns-and-what-the-member-owns).

## Releases

A tag `v<version>` publishes the source archive; the index descriptor
(`mcpp-index/pkgs/m/mcpp.plugins.lua`) names the GitHub archive and its GitCode
mirror with one sha256.
