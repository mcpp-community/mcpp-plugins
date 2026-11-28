# mcpp-plugins

> mcpp 的官方构建插件——一个成员,一个 feature,一个模块。

[English](README.md) | 简体中文

[![ci](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-plugins)](https://github.com/mcpp-community/mcpp-plugins/releases)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

[文档索引](docs/README.md) · [mcpp](https://github.com/mcpp-community/mcpp) · [包索引 mcpp-index](https://mcpplibs.github.io/mcpp-index/) · [发布](https://github.com/mcpp-community/mcpp-plugins/releases)

mcpp 是一个带有构建插件框架的通用引擎;插件来自本包(`mcpp:plugins`)、来自
第三方,或来自项目自身,而 `build.mcpp` 是项目使用它们的地方:它按成员声明
的名字导入成员模块,配置并调用它。这条边是 `[build-dependencies]` 条目——
库服务于构建程序,本包永不抵达目标。

## 快速开始

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

## 家族

| 家族 | 模块 | 回答的问题 |
|---|---|---|
| rules | `mcpp.rules.<x>` | 一类翻译单元如何被一个 mcpp 不直接驱动的编译器编译 |
| tools | `mcpp.tools.<x>` | 构建程序自己要做什么(见 [`tools/README.md`](tools/README.md)) |
| dist | `mcpp.dist.<x>` | 链接产出什么,以及用户以什么形式安装它 |
| deps | `mcpp.deps.<x>` | 一个库从哪里来 |
| identity | `mcpp.plugins` | 库根,先于每个成员编译;它声明这个集合的版本 |

第三方插件把模块命名为 `mcpp.<它的 namespace>.*`;`core`、`plugins`、`deps`、
`rules`、`dist`、`tools` 属于本包与引擎。家族的完整论述,以及引擎如何把文件
路由给 rule,见 [docs/00](docs/00-engine-and-rules.md);成员运行的工具从哪里
来,见 [docs/02](docs/02-tool-sources.md)。

## 插件

最常用的几个:

| 成员 | 模块 | 行为 | 文档 |
|---|---|---|---|
| `rules-cuda` | `mcpp.rules.cuda` | 经 clang(LLVM 工具链)或 nvcc(GCC 工具链)编译 CUDA(`*.cu`)。 | [docs](docs/10-rules.md#rules-cuda) |
| `rules-sycl` | `mcpp.rules.sycl` | 用 DPC++ 编译 SYCL(`*.sycl`)。 | [docs](docs/10-rules.md#rules-sycl) |
| `rules-spirv` | `mcpp.rules.spirv` | 把 GLSL 与 HLSL 着色器阶段编译为 SPIR-V,结果可内嵌或放置。 | [docs](docs/10-rules.md#rules-spirv) |
| `rules-qt` | `mcpp.rules.qt` | 以 action 运行 `moc`、`uic`、`rcc` 与 Qt 的翻译工具,链接 Qt 模块并放置其运行时。 | [docs](docs/11-rules-qt.md) |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 以 action 安装 `vcpkg.json` manifest(用 mcpp 解析的 toolset),并把前缀映射进构建。 | [docs](docs/40-deps.md#deps-vcpkg) |
| `dist-apk` | `mcpp.dist.apk` | 把原生闭包打成已签名的 APK 或 App Bundle,带 Java、Kotlin 与 Maven 库。 | [docs](docs/32-dist-apk.md) |

<details>
<summary><b>全部 23 个成员</b>——feature、模块、mcpp 下限、行为、文档、维护者</summary>

| feature | 模块 | mcpp 下限 | 行为 | 文档 | 维护者 |
|---|---|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.10.1.3 | 用 BiSheng 以混合模式编译 Ascend C(`*.asc`),目标文件并入普通链接。 | [rules](docs/10-rules.md#rules-ascendc) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.10.1.3 | 经 clang(LLVM 工具链)或 nvcc(GCC 工具链)编译 CUDA(`*.cu`)。 | [rules](docs/10-rules.md#rules-cuda) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-hip` | `mcpp.rules.hip` | 2026.10.1.3 | 在 NVIDIA 平台上用项目自带的 clang 编译 HIP(`*.hip`)。 | [rules](docs/10-rules.md#rules-hip) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-metal` | `mcpp.rules.metal` | 2026.9.8.1 | 用宿主的 Xcode 把 `.metal` 着色器编成 Metal 库,并部署在程序旁。 | [rules](docs/10-rules.md#rules-metal) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-qt` | `mcpp.rules.qt` | 2026.10.1.3 | 以 action 运行 `moc`、`uic`、`rcc` 与 Qt 的翻译工具,链接 Qt 模块并放置其运行时。 | [rules-qt](docs/11-rules-qt.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-qml` | `mcpp.rules.qml` | 2026.10.1.3;携带 QML 插件库的包 2026.11.23.1 | 构建 QML 应用:各模块的 `moc`、`qmltyperegistrar`、`qmlcachegen`、`rcc`、`qsb` 链为 action,程序导入的 Qt QML 模块放在程序旁,并写 `qt.conf`。隐含 `rules-qt`。 | [rules-qml](docs/12-rules-qml.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-slang` | `mcpp.rules.slang` | 2026.10.1.3 | 编译 Slang(`*.slang`),结果可内嵌或放置。 | [rules](docs/10-rules.md#rules-slang) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.10.1.3 | 把 GLSL 与 HLSL 着色器阶段编译为 SPIR-V,结果可内嵌或放置。 | [rules](docs/10-rules.md#rules-spirv) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-swift` | `mcpp.rules.swift` | 2026.9.8.1 | 把包的 `.swift` 源码编成一个模块,供 C 与 C++ 源码调用。 | [rules](docs/10-rules.md#rules-swift) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.10.1.3 | 用 DPC++ 编译 SYCL(`*.sycl`)。 | [rules](docs/10-rules.md#rules-sycl) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | 把数据文件写进程序编译内含的头文件。 | [tools-embed](docs/20-tools-embed.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | 生成代码孤岛的 `extern "C"` 边界与 C++ 模块。 | [tools-island](docs/21-tools-island.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `tools-steps` | `mcpp.tools.steps` | 2026.10.1.3 | 把工作不止一条命令的 action 描述为后端运行的可移植操作;被 `dist-apk` 与 `dist-apple` 隐含。 | [tools-steps](docs/22-tools-steps.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.10.1.3 | 把 `mcpp pack` 暂存的树做成 AppImage(Linux)。 | [dist](docs/30-dist.md#dist-appimage) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-deb` | `mcpp.dist.deb` | 2026.10.30.1 | 把暂存树安装在 `/opt/<name>` 下,带启动器、桌面入口与图标,产出 Debian 包(Linux 目标)。 | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-rpm` | `mcpp.dist.rpm` | 2026.10.30.1 | 同样的安装形态,产出 RPM 包(Linux 目标)。 | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-wix` | `mcpp.dist.wix` | 2026.10.1.3 | 用暂存树构建 MSI,以及串联它的 Burn bundle(Windows)。 | [dist](docs/30-dist.md#dist-wix) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.14.2 | 布置 macOS 或 iOS 应用 bundle,签名并写成磁盘映像。 | [dist-apple](docs/31-dist-apple.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-web` | `mcpp.dist.web` | 2026.9.13.1 | 把 `wasm32-emscripten` 程序及其文件复制进带 `index.html` 的 web 目录。 | [dist](docs/30-dist.md#dist-web) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `dist-apk` | `mcpp.dist.apk` | 2026.10.1.3 | 把原生闭包打成已签名的 APK 或 App Bundle,带 Java、Kotlin 与 Maven 库。 | [dist-apk](docs/32-dist-apk.md) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 2026.10.1.3 | 以 action 安装 `vcpkg.json` manifest(用 mcpp 解析的 toolset),并把前缀映射进构建。 | [deps](docs/40-deps.md#deps-vcpkg) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `deps-cmake` | `mcpp.deps.cmake` | 2026.10.1.3;交叉构建 2026.10.30.1 | 以 action 用 Ninja 与 mcpp 解析的 toolset 构建并安装 CMake 子项目——交叉目标时用引擎契约的编译器与措辞;同键的构建复用安装,并把前缀映射进构建。 | [deps](docs/40-deps.md#deps-cmake) | [@Sunrisepeak](https://github.com/Sunrisepeak) |
| `deps-archive` | `mcpp.deps.archive` | 2026.10.1.3 | 解压项目自带的 zip 归档,把它的树放在程序旁。 | [deps](docs/40-deps.md#deps-archive) | [@Sunrisepeak](https://github.com/Sunrisepeak) |

一些 feature 添加子能力:`dist-apk-kotlin`、`dist-apk-maven`,以及 deps 成员
隐含的 `deps`。`plugins-core`——`mcpp.plugins.declare`、
`mcpp.plugins.toolset`、`mcpp.plugins.fs`、`mcpp.plugins.tool`——是每个成员
都隐含的通用库;`plugins-testing`、`plugins-toolchain`、
`plugins-platform` 与 `plugins-progress` 各自添加自己的模块。分层与引擎
下限见 [docs/01 §3](docs/01-plugin-development.md#3-layers-names-and-the-engine-floor)。

</details>

## 贡献与维护

**维护者。** 本包以及今天每一个成员:
[@Sunrisepeak](https://github.com/Sunrisepeak)。按族的 review 路由:
[`.github/CODEOWNERS`](.github/CODEOWNERS)——新维护者改一行即可接管一个族。

**贡献。** 变更经分支上的 pull request;一个 PR 只做一件事;提交与文字用
英文。新成员 = 一个模块(长出一个文件时改为目录——[docs/01
§5](docs/01-plugin-development.md#5-a-new-plugin))、`mcpp.toml` 里的一个
feature、`tests/` 下一个由 CI 构建并断言的消费者、上表里的一行、`docs/` 下的
一页、一次版本提升。成员遵循的约定见
[docs/01](docs/01-plugin-development.md);引擎拥有什么、成员拥有什么的划分
见 [docs/00](docs/00-engine-and-rules.md#adding-a-member-what-the-engine-owns-and-what-the-member-owns)。
