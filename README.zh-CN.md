# mcpp-plugins

> mcpp 的官方构建插件——一个插件成员,一个 feature,一个模块。

[![ci](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-plugins)](https://github.com/mcpp-community/mcpp-plugins/releases)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

[English](README.md) | 简体中文 · [文档索引](docs/README.md) · [mcpp](https://github.com/mcpp-community/mcpp) · [mcpp-index](https://mcpp.index.xlings.org) · [发布](https://github.com/mcpp-community/mcpp-plugins/releases)

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
| identity | `mcpp.plugins` | 库根,先于每个插件成员编译;它声明这个集合的版本 |

第三方插件把模块命名为 `mcpp.<它的 namespace>.*`;`core`、`plugins`、`deps`、
`rules`、`dist`、`tools` 属于本包与引擎。家族的完整论述,以及引擎如何把文件
路由给 rule,见 [docs/00](docs/00-engine-and-rules.md);成员运行的工具从哪里
来,见 [docs/02](docs/02-tool-sources.md)。

## 插件

| feature | 行为 | 文档 |
|---|---|---|
| `rules-cuda` | 经 clang(LLVM 工具链)或 nvcc(GCC 工具链)编译 CUDA(`*.cu`)。 | [docs](docs/10-rules.md#rules-cuda) |
| `rules-sycl` | 用 DPC++ 编译 SYCL(`*.sycl`)。 | [docs](docs/10-rules.md#rules-sycl) |
| `rules-spirv` | 把 GLSL 与 HLSL 着色器阶段编译为 SPIR-V,结果可内嵌或放置。 | [docs](docs/10-rules.md#rules-spirv) |
| `rules-qt` | 以 action 运行 `moc`、`uic`、`rcc` 与 Qt 的翻译工具,链接 Qt 模块并放置其运行时。 | [docs](docs/11-rules-qt.md) |
| `deps-vcpkg` | 以 action 安装 `vcpkg.json` manifest(用 mcpp 解析的 toolset),并把前缀映射进构建。 | [docs](docs/40-deps.md#deps-vcpkg) |
| `dist-apk` | 把原生闭包打成已签名的 APK 或 App Bundle,带 Java、Kotlin 与 Maven 库。 | [docs](docs/32-dist-apk.md) |

<details>
<summary><b>全部 23 个插件成员</b>——feature、行为、文档;模块与引擎下限见<a href="docs/README.md#the-plugins">文档索引</a></summary>

| feature | 行为 | 文档 |
|---|---|---|
| `rules-ascendc` | 用 BiSheng 以混合模式编译 Ascend C(`*.asc`),目标文件并入普通链接。 | [docs](docs/10-rules.md#rules-ascendc) |
| `rules-cuda` | 经 clang(LLVM 工具链)或 nvcc(GCC 工具链)编译 CUDA(`*.cu`)。 | [docs](docs/10-rules.md#rules-cuda) |
| `rules-hip` | 在 NVIDIA 平台上用项目自带的 clang 编译 HIP(`*.hip`)。 | [docs](docs/10-rules.md#rules-hip) |
| `rules-metal` | 用宿主的 Xcode 把 `.metal` 着色器编成 Metal 库,并部署在程序旁。 | [docs](docs/10-rules.md#rules-metal) |
| `rules-qt` | 以 action 运行 `moc`、`uic`、`rcc` 与 Qt 的翻译工具,链接 Qt 模块并放置其运行时。 | [docs](docs/11-rules-qt.md) |
| `rules-qml` | 构建 QML 应用:各模块的 `moc`、`qmltyperegistrar`、`qmlcachegen`、`rcc`、`qsb` 链为 action,程序导入的 Qt QML 模块放在程序旁,并写 `qt.conf`。隐含 `rules-qt`。 | [docs](docs/12-rules-qml.md) |
| `rules-slang` | 编译 Slang(`*.slang`),结果可内嵌或放置。 | [docs](docs/10-rules.md#rules-slang) |
| `rules-spirv` | 把 GLSL 与 HLSL 着色器阶段编译为 SPIR-V,结果可内嵌或放置。 | [docs](docs/10-rules.md#rules-spirv) |
| `rules-swift` | 把包的 `.swift` 源码编成一个模块,供 C 与 C++ 源码调用。 | [docs](docs/10-rules.md#rules-swift) |
| `rules-sycl` | 用 DPC++ 编译 SYCL(`*.sycl`)。 | [docs](docs/10-rules.md#rules-sycl) |
| `tools-embed` | 把数据文件写进程序编译内含的头文件。 | [docs](docs/20-tools-embed.md) |
| `tools-island` | 生成代码孤岛的 `extern "C"` 边界与 C++ 模块。 | [docs](docs/21-tools-island.md) |
| `tools-steps` | 把工作不止一条命令的 action 描述为后端运行的可移植操作;被 `dist-apk` 与 `dist-apple` 隐含。 | [docs](docs/22-tools-steps.md) |
| `dist-appimage` | 把 `mcpp pack` 暂存的树做成 AppImage(Linux)。 | [docs](docs/30-dist.md#dist-appimage) |
| `dist-deb` | 把暂存树安装在 `/opt/<name>` 下,带启动器、桌面入口与图标,产出 Debian 包(Linux 目标)。 | [docs](docs/30-dist.md#dist-deb-and-dist-rpm) |
| `dist-rpm` | 同样的安装形态,产出 RPM 包(Linux 目标)。 | [docs](docs/30-dist.md#dist-deb-and-dist-rpm) |
| `dist-wix` | 用暂存树构建 MSI,以及串联它的 Burn bundle(Windows)。 | [docs](docs/30-dist.md#dist-wix) |
| `dist-apple` | 布置 macOS 或 iOS 应用 bundle,签名并写成磁盘映像。 | [docs](docs/31-dist-apple.md) |
| `dist-web` | 把 `wasm32-emscripten` 程序及其文件复制进带 `index.html` 的 web 目录。 | [docs](docs/30-dist.md#dist-web) |
| `dist-apk` | 把原生闭包打成已签名的 APK 或 App Bundle,带 Java、Kotlin 与 Maven 库。 | [docs](docs/32-dist-apk.md) |
| `deps-vcpkg` | 以 action 安装 `vcpkg.json` manifest(用 mcpp 解析的 toolset),并把前缀映射进构建。 | [docs](docs/40-deps.md#deps-vcpkg) |
| `deps-cmake` | 以 action 用 Ninja 与 mcpp 解析的 toolset 构建并安装 CMake 子项目,并把前缀映射进构建。 | [docs](docs/40-deps.md#deps-cmake) |
| `deps-archive` | 解压项目自带的 zip 归档,把它的树放在程序旁。 | [docs](docs/40-deps.md#deps-archive) |

</details>

## 贡献与维护

项目目前主要由 [Sunrisepeak](https://github.com/Sunrisepeak) 维护——项目的
维护和发展需要更多贡献者与维护者。如果你对某些插件感兴趣并期望参与贡献和
维护,从简单的、自己感兴趣的方向开始:使用它、介绍与教学、反馈问题、修复
bug、优化改进、维护一个或多个插件:

- [提出或提交一个新插件](https://github.com/mcpp-community/mcpp-plugins/issues/new?template=new-plugin.yml)
- [申请维护一个插件成员](https://github.com/mcpp-community/mcpp-plugins/issues/new?template=maintainer-application.yml)

## 维护者

| 维护者 | 主要负责的插件成员 |
|---|---|
| [Sunrisepeak](https://github.com/Sunrisepeak) | 项目主要负责人 |
