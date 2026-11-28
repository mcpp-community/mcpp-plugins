# mcpp-plugins

> mcpp 的官方构建插件——一个成员,一个 feature,一个模块。

[English](README.md) | 简体中文

本文是 [README.md](README.md) 的中文镜像;英文为权威版本,两处不一致时以英文为准。

[![ci](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-plugins/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-plugins)](https://github.com/mcpp-community/mcpp-plugins/releases)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

mcpp 是一个带有构建插件框架的通用引擎;插件来自本包(`mcpp:plugins`)、来自
第三方,或来自项目自身,而 `build.mcpp` 是项目使用它们的地方:它按成员声明
的名字导入成员模块,配置并调用它。这条边是 `[build-dependencies]` 条目,而
不是 `[dependencies]`:`host-module = true` 声明要的是哪个构建期产物,该小节
声明这个包不抵达目标——rule 的库永远不会被链接进产物(mcpp docs/05 §2.6.1)。

## 快速开始

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

## 家族

| 家族 | 模块 | 回答的问题 |
|---|---|---|
| rules | `mcpp.rules.<x>` | 一类翻译单元如何被一个 mcpp 不直接驱动的编译器编译 |
| tools | `mcpp.tools.<x>` | 构建程序自己要做什么(见 [`tools/README.md`](tools/README.md)) |
| dist | `mcpp.dist.<x>` | 链接产出什么,以及用户以什么形式安装它 |
| deps | `mcpp.deps.<x>` | 一个库从哪里来 |
| identity | `mcpp.plugins` | 库根,先于每个成员编译;它声明这个集合的版本 |

第三方插件把模块命名为 `mcpp.<它的 namespace>.*`;`core`、`plugins`、`deps`、
`rules`、`dist`、`tools` 这几个第二段属于本包与引擎。家族的完整论述,以及
引擎如何把文件路由给 rule,见 [docs/00](docs/00-engine-and-rules.md);成员
运行的工具从哪里来,见 [docs/02](docs/02-tool-sources.md)。

## 成员

自 0.21.0 起整个包需要 mcpp 2026.10.30.1(`[package] mcpp`);列中给出每个
成员的行为首次所需的发布版。链接可执行文件或共享库的交叉 CMake 子项目需要
mcpp 2026.11.7.1 或更高,交叉配置需要 CMake 3.19 或更高。

<details>
<summary><b>23 个成员</b>——feature、模块、mcpp 下限、行为、文档、维护者</summary>

| feature | 模块 | mcpp 下限 | 行为 | 文档 | 维护者 |
|---|---|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.10.1.3 | 用 BiSheng 以混合模式编译 Ascend C(`*.asc`),目标文件并入普通链接。 | [rules](docs/10-rules.md#rules-ascendc) | @Sunrisepeak |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.10.1.3 | 经 clang(LLVM 工具链)或 nvcc(GCC 工具链)编译 CUDA(`*.cu`)。 | [rules](docs/10-rules.md#rules-cuda) | @Sunrisepeak |
| `rules-hip` | `mcpp.rules.hip` | 2026.10.1.3 | 在 NVIDIA 平台上用项目自带的 clang 编译 HIP(`*.hip`)。 | [rules](docs/10-rules.md#rules-hip) | @Sunrisepeak |
| `rules-metal` | `mcpp.rules.metal` | 2026.9.8.1 | 用宿主的 Xcode 把 `.metal` 着色器编成 Metal 库,并部署在程序旁。 | [rules](docs/10-rules.md#rules-metal) | @Sunrisepeak |
| `rules-qt` | `mcpp.rules.qt` | 2026.10.1.3 | 以 action 运行 `moc`、`uic`、`rcc` 与 Qt 的翻译工具,链接 Qt 模块并放置其运行时。 | [rules-qt](docs/11-rules-qt.md) | @Sunrisepeak |
| `rules-qml` | `mcpp.rules.qml` | 2026.10.1.3;携带 QML 插件库的包 2026.11.23.1 | 构建 QML 应用:各模块的 `moc`、`qmltyperegistrar`、`qmlcachegen`、`rcc`、`qsb` 链为 action,程序导入的 Qt QML 模块放在程序旁,并写 `qt.conf`。隐含 `rules-qt`。 | [rules-qml](docs/12-rules-qml.md) | @Sunrisepeak |
| `rules-slang` | `mcpp.rules.slang` | 2026.10.1.3 | 编译 Slang(`*.slang`),结果可内嵌或放置。 | [rules](docs/10-rules.md#rules-slang) | @Sunrisepeak |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.10.1.3 | 把 GLSL 与 HLSL 着色器阶段编译为 SPIR-V,结果可内嵌或放置。 | [rules](docs/10-rules.md#rules-spirv) | @Sunrisepeak |
| `rules-swift` | `mcpp.rules.swift` | 2026.9.8.1 | 把包的 `.swift` 源码编成一个模块,供 C 与 C++ 源码调用。 | [rules](docs/10-rules.md#rules-swift) | @Sunrisepeak |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.10.1.3 | 用 DPC++ 编译 SYCL(`*.sycl`)。 | [rules](docs/10-rules.md#rules-sycl) | @Sunrisepeak |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | 把数据文件写进程序编译内含的头文件。 | [tools-embed](docs/20-tools-embed.md) | @Sunrisepeak |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | 生成代码孤岛的 `extern "C"` 边界与 C++ 模块。 | [tools-island](docs/21-tools-island.md) | @Sunrisepeak |
| `tools-steps` | `mcpp.tools.steps` | 2026.10.1.3 | 把工作不止一条命令的 action 描述为后端运行的可移植操作;被 `dist-apk` 与 `dist-apple` 隐含。 | [tools-steps](docs/22-tools-steps.md) | @Sunrisepeak |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.10.1.3 | 把 `mcpp pack` 暂存的树做成 AppImage(Linux)。 | [dist](docs/30-dist.md#dist-appimage) | @Sunrisepeak |
| `dist-deb` | `mcpp.dist.deb` | 2026.10.30.1 | 把暂存树安装在 `/opt/<name>` 下,带启动器、桌面入口与图标,产出 Debian 包(Linux 目标)。 | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | @Sunrisepeak |
| `dist-rpm` | `mcpp.dist.rpm` | 2026.10.30.1 | 同样的安装形态,产出 RPM 包(Linux 目标)。 | [dist](docs/30-dist.md#dist-deb-and-dist-rpm) | @Sunrisepeak |
| `dist-wix` | `mcpp.dist.wix` | 2026.10.1.3 | 用暂存树构建 MSI,以及串联它的 Burn bundle(Windows)。 | [dist](docs/30-dist.md#dist-wix) | @Sunrisepeak |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.14.2 | 布置 macOS 或 iOS 应用 bundle,签名并写成磁盘映像。 | [dist-apple](docs/31-dist-apple.md) | @Sunrisepeak |
| `dist-web` | `mcpp.dist.web` | 2026.9.13.1 | 把 `wasm32-emscripten` 程序及其文件复制进带 `index.html` 的 web 目录。 | [dist](docs/30-dist.md#dist-web) | @Sunrisepeak |
| `dist-apk` | `mcpp.dist.apk` | 2026.10.1.3 | 把原生闭包打成已签名的 APK 或 App Bundle,带 Java、Kotlin 与 Maven 库。 | [dist-apk](docs/32-dist-apk.md) | @Sunrisepeak |
| `deps-vcpkg` | `mcpp.deps.vcpkg` | 2026.10.1.3 | 以 action 安装 `vcpkg.json` manifest(用 mcpp 解析的 toolset),并把前缀映射进构建。 | [deps](docs/40-deps.md#deps-vcpkg) | @Sunrisepeak |
| `deps-cmake` | `mcpp.deps.cmake` | 2026.10.1.3;交叉构建 2026.10.30.1 | 以 action 用 Ninja 与 mcpp 解析的 toolset 构建并安装 CMake 子项目——交叉目标时用引擎契约的编译器与措辞;同键的构建复用安装,并把前缀映射进构建。 | [deps](docs/40-deps.md#deps-cmake) | @Sunrisepeak |
| `deps-archive` | `mcpp.deps.archive` | 2026.10.1.3 | 解压项目自带的 zip 归档,把它的树放在程序旁。 | [deps](docs/40-deps.md#deps-archive) | @Sunrisepeak |

一些 feature 为成员添加子能力:`dist-apk-kotlin` 与 `dist-apk-maven`(Kotlin
源码、一个 Maven 图),以及 `deps`(deps 成员隐含它)。

**通用库。** `plugins-core` 给构建程序 `mcpp.plugins.declare`、
`mcpp.plugins.toolset`、`mcpp.plugins.fs` 与 `mcpp.plugins.tool`;每个成员都
隐含它,只用这些模块的构建程序在依赖边上点名它。`plugins-testing`、
`plugins-toolchain` 与 `plugins-platform` 添加 `mcpp.plugins.testing`、
`mcpp.plugins.toolchain` 与 `mcpp.plugins.platform`(连同
`mcpp::plugins::package`);`plugins-progress` 添加可选的 action 遥测及其原子
快照写入器;`mcpp.plugins.accel` 读取实现自有的目标请求。分层、命名与引擎
下限见 [docs/01 §3](docs/01-plugin-development.md#3-layers-names-and-the-engine-floor)。
`surface`(`plugins-core` 在 0.17.0 之前的名字)保留至 2027-03-28。

</details>

## 布局

```
mcpp.toml            这个包:每个成员一个 feature
src/plugins.cppm     export module mcpp.plugins;  版本、mcpp::plugins::surface
                     (消费者点名的声明)、mcpp::plugins::xml 与
                     mcpp::plugins::package(暂存包的元数据)
src/declare.cppm,    构建程序的通用库(`plugins-core`):
src/toolset.cppm,    mcpp.plugins.declare、mcpp.plugins.toolset、mcpp.plugins.fs
src/fs.cppm
src/testing.cppm     mcpp.plugins.testing(`plugins-testing`)
src/platform.cppm    mcpp.plugins.platform(`plugins-platform`)
src/compat/, deps/compat/
                     兼容单元,各自在其头文件注明的日期退役
rules/<x>.cppm       export module mcpp.rules.<x>;
tools/<x>.cppm       export module mcpp.tools.<x>;
dist/<x>.cppm        export module mcpp.dist.<x>;
deps/<x>.cppm        export module mcpp.deps.<x>;  deps/deps.cppm 是共享单元
<family>/<x>/        一个文件装不下的成员:<x>.cppm 保留消费者导入的模块名,
                     internal/ 承载机制;模块名不变
tests/<consumer>/    每个成员一个项目,CI 用固定的 mcpp 构建
docs/                每个成员或成员组一页([索引](docs/README.md))
```

## 添加一个成员

一个成员是:一个模块文件,`mcpp.toml` 里的一个 feature,`tests/` 下一个由
CI 构建并断言的消费者,上表里带 mcpp 下限的一行,`docs/` 下的一页,以及一次
版本提升。它的代码是 GCC 加 libstdc++、clang 加 libc++ 与编辑器 clangd 加
libstdc++ 都接受的 C++23,CI 在三者之下构建或读取每个 fixture。成员遵循的
约定见 [docs/01](docs/01-plugin-development.md);引擎拥有什么、成员拥有什么
的划分见
[docs/00](docs/00-engine-and-rules.md#adding-a-member-what-the-engine-owns-and-what-the-member-owns)。

## 发布

tag `v<版本号>` 发布源码归档;索引描述符
(`mcpp-index/pkgs/mcpp/mcpp.plugins.lua`)以同一个 sha256 命名 GitHub 归档
及其 GitCode 镜像。
