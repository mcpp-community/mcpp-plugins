# mcpp 构建插件框架：实施计划与任务依赖

日期：2026-10-01。状态：执行中。

依据：`2026-10-01-ecosystem-build-plugin-framework-design.md` v3（下称“设计”）及其 §0 的已定事项。本记录覆盖 2026-10-01 /goal 的要求：
- D6 一步到位，不分期交付；
- 每个仓库单 PR，标题带版本号；
- CI 全绿；
- 自我 review，包括生态级 review；
- 发布：GitHub 加 GitCode（本地 gtc），mcpp-index 与 xim-pkgindex 登记；
- 在 xlings subos 沙箱中以 CN 镜像做真实验证；
- 关闭已完成的 issue；
- 文档与代码注释不含表情符号。

## 1. 交付物与版本

| 仓库 | PR | 版本 | 内容 |
|---|---|---|---|
| mcpp-community/mcpp | 1 个 | 发布当日的 `YYYY.M.D.N` | 核心：决策记录与输出语义、E1、E2、决策指令、自定义工具链（路径与 toolchain 阶段）、规范与文档（中英）、测试 |
| mcpp-community/mcpp-plugins | 1 个 | 0.19.0 | `mcpp.plugins.tool`、`mcpp.plugins.toolchain`、成员迁移、按需条目、文档、fixture、CI |
| speak-agent/llvm-macos27-lab | 1 个以上 | — | LLVM `arm64e.x1` 修复在 macOS 27 / Xcode 27 上的可用性；裁剪后的工具链资产 |
| speak-agent/mcpp-framework-lab | 1 个以上 | — | 工具来源、输出、可观察性的验收 |
| speak-agent/mcpp-toolchain-lab | 1 个以上 | — | 自定义工具链的验收（含 macOS 27） |
| mcpplibs/mcpp-index | 1 个 | — | 登记 plugins 0.19.0 |
| openxlings/xim-pkgindex | release 自动开出的 bump PR | — | 登记 mcpp 新版本 |

设计 §10 中的 S4（xim `llvm` 23.1.3 打包）取决于上游发布。它不在本 goal 的关键路径上：若上游在收尾前发布了 23.1.3，就按设计执行；否则在 mcpp#669 中记录 lab 的结果，并留作后续。

## 2. 任务与依赖

```
T0  issues（mcpp、plugins）与 lab 仓库
T1  核心：来源类、决策记录、渲染（status 行、标签、Finished 汇总）、resolution.json、
    mcpp why tool/payload、--managed-only
T2  E1 覆盖（清单、target cfg、全局配置、环境变量；供给过滤；统一校验；
    fillXpkgDirs 的 DIR/PROGRAM/SOURCE；xpkg_program、xpkg_source）      ← T1
T3  E2 按需（provision 键；xpkg_request/xpkg_pending；xpkg-request 指令；
    批量供给与选择性重跑；离线拒绝；plan_only note）                   ← T1, T2
T4  decision 指令（插件把工具决定写进记录）                            ← T1
T5  自定义工具链按路径（[toolchain] 表、MCPP_TOOLCHAIN=path:、bootstrap 键；
    探测；不做 post-install；指纹含内容 hash；lock 记为 local）          ← T1
T6  toolchain 阶段（configure = "build.mcpp"；mcpp:toolchain 指令；mcpp::phase）  ← T4, T5
T7  mcpp 规范与文档（中英）、CHANGELOG、设计记录                          ← T1–T6 的语义
T8  mcpp 单测与 e2e                                                      ← 各项随实现
T9  plugins 0.19.0（tool、toolchain、成员迁移、on-request、文档、fixture、CI）
                                                                         ← T2, T3, T4, T6 的宿主接口
T10 llvm-macos27-lab                                                     （独立，最早开始）
T11 framework-lab、toolchain-lab                                         ← T9（以及 T10 的资产）
T12 两个 PR 的 CI 全绿；以 mcpp_source_ref 指向 mcpp 分支做 plugins 预验证
T13 自我 review（含生态级）
T14 发布 mcpp → xim-pkgindex bump PR 合入 → bootstrap pin
T15 发布 plugins 0.19.0 → gtc → mcpp-index
T16 沙箱验证（xlings subos，CN 镜像）；lab 改指已发布版本复跑
T17 issue 评论与关闭；设计记录追加状态行
```

**并行**：
- T10 不依赖任何实现，最早开始；
- T1 完成后，T2/T4/T5 可以并行；
- T7 与 T9 的文档部分在接口冻结后与实现并行；
- 后台 agent 同时最多 3 个。

## 3. 质量门（每个视角在哪里被检查）

| 视角 | 检查 |
|---|---|
| 架构 | 核心只承担语义与接口；具体工具的知识留在 L2 与成员中（设计 §3 原则 4）；新模块的 import 指向类型的提供者 |
| 稳定性 | 默认构建的输出与行为逐字节不变（framework-lab `default` case 与 e2e）；已有 e2e 全部通过 |
| 简洁 | 两个入口共用一个描述 schema；一份决策记录驱动全部输出 |
| 用户体验 | 显式选择一行 `Using`；错误首行写明来源；报错给出四种指定方式 |
| 兼容性 | 旧插件不改代码即可从 E1 受益；`tool::choice` 可由字符串隐式构造；新键在旧引擎上会被拒或忽略，提 floor |
| 跨平台 | 单测与 e2e 覆盖 Linux、macOS、Windows；Windows 路径与 `.exe`；macOS 27 由 lab 覆盖 |
| 一致性 | 优先级只写一份（核心一份、L2 一份）；文档中英结构一一对应 |
| 无感升级 | 不写任何新键的项目在新版本上行为不变；插件 0.19.0 的成员在默认路径上下载与输出不变 |
| 测试覆盖 | 每个新清单键、指令、查询函数与命令都有单测或 e2e；每条拒绝都有一个反例用例 |

## 4. 执行记录

### 2026-10-01

**issue**：mcpp#755（引擎）、plugins#41（0.19.0）。

**mcpp 侧**（分支 `feat/build-sources`，PR mcpp#758，版本 2026.10.1.3）：
- 清单层：`[xlings.overrides]`（根清单、`[target.'cfg(..)']`、环境变量、config.toml）、
  条目的 `provision` 键、`[toolchain]` 的表形式与 `bootstrap`；
- 协议 15：`xpkg_source`、`xpkg_program`、`xpkg_request`、`xpkg_pending`、`phase`、
  `decision`、`toolchain`;
- prepare：`sources.cpp`(决策记录、覆盖、按需供给、`--managed-only`)与
  `local_toolchain.cpp`(按路径命名的工具链、工具链阶段、两遍 prepare);
- 输出:`ui::source` 的 `Using` 行、`Finished` 的汇总、`resolution.json` 的 `sources`、
  `mcpp why sources|tool|payload` 与 `mcpp.why.sources`;
- 测试:`tests/unit/test_sources.cpp`(15 例)与 e2e 873-877。

**plugins 侧**（分支 `feat/0.19.0-tool-sources`，0.19.0）：`mcpp.plugins.tool`、
`mcpp.plugins.toolchain`、13 个成员迁移、5 个按需条目、`plugin-logic` 新增 5 例、
CI 新增 `tool-sources` 判据。

**两处发现，都由 CI 的其他平台给出**：
1. `std::to_string` 在 libc++ 上对文件系统时钟的 rep 与 `uintmax_t` 同时有重载，macOS
   编译失败;三处改为 `std::format` 加显式类型。
2. 从 `mcpp.toolchain.model` 导出
   `std::vector<std::pair<std::string, std::filesystem::path>>` 使 clang 20.1.7 在
   Windows 上为**另一个模块**的 `mcpp::pack::interface_set_digest` 生成代码时崩溃——
   该函数实例化同一个特化并按指向 `first` 的成员指针排序。改为具名结构
   `ToolOverride`。这与 `modules/manifest/src/types.cppm` 记下的 GCC 16 截断 BMI 是
   同一类危害:报错点名的文件与改动无关。

**本机验证**：单测 144 通过;e2e 873-877 通过;按关键词选出的既有 e2e 62 个通过
(219 在已发布的 2026.10.1.2 上同样失败,658 需要连接 Android 设备);plugins 的 11 个
consumer fixture 与 27 例 `plugin-logic` 通过;`tests/cmake-consumer` 在
`MCPP_NO_AUTO_INSTALL=1` 下由 `build.mcpp` 点名 cmake 即可构建。

**另外四处发现,三处由 CI 的其他平台给出,一处由手工验证给出**:
3. 一个导入很多宿主模块的构建程序,其编译命令每个模块带一条
   `-fmodule-file=<name>=<path>`(绝对路径)。`all-rules-compile` 导入十五个,在集合多出
   一个模块的那一刻越过 Windows 上 `capture_exec` 所经 shell 的 8191 字节,只报
   `The command line is too long.`——既不点名长度也不点名原因,正是
   `mcpp.build.cmdlimits` 要让人认出来的那一类。改为超过该模块所述预算时走 `@file`,
   引号规则由导出的 `response_file_body` 与每种语法一个单测覆盖(命令本身无法在不越限的宿主上
   复现)。第二遍才对:响应文件不是一种格式——clang 与 GCC 按 GNU 方式 tokenize,反斜杠是转义,
   于是照原样写入的 Windows 路径被吃掉分隔符
   (`no such file or directory: 'D:amcpp-plugins...'`);这两个驱动改为每个参数套单引号,
   cl 与 clang-cl 保持 Windows 引号规则。
4. `mcpp.plugins.testing` 把真实构建的 `MCPP_XPKG_*` 留在环境里。这些用例运行在一个构建
   程序内部,而引擎为该程序声明的每个载荷设置 `_DIR`、`_PROGRAM`、`_SOURCE`——包括
   `provision = "on-request"` 且无人请求时的 `pending`。于是 8 个 vcpkg 用例在载荷未安装的
   机器上请求 `xim:vcpkg` 并什么都不规划,在已安装的机器上照常通过:macOS arm64 与 Windows
   各 19/27,本机 27/27。键由包名派生,所以按前缀枚举而不是列表。同一个二进制、同一个继承值
   两次测量:修前 19/27,修后 27/27。
5. spirv fixture 的 CI 步骤断言一条状态行,却只捕获 stdout。mcpp 自 2026.10.1.1 起把叙述写到
   标准错误,所以日志为空,步骤报告「规则没有运行」而它运行了。
6. `platform::fs::which()` 对一个同时是 shell 内建命令的名字返回空:`command -v true` 打印
   `true` 而不是路径,因为 shell 回答的是它自己会运行的东西。于是一个按裸名写的载荷覆盖在
   带 /usr/bin/true 的机器上被拒为「not found on PATH」。改为对 shell 返回的裸词走一遍 PATH。

**一处可测量的生态效果**:`provision = "on-request"` 之后,macOS 与 Windows 的 `rules` 作业
不再安装 `xim:vcpkg`(main 上会装)。这既是本次要的节省,也正是它暴露了第 4 处发现。

**llvm-macos27-lab**（speak-agent，公开）：`release/23.x` 的 `21ef2ddb8060`（含
`ee66426152f9`）构建的 lld 在 `xcode-27`(macOS 27.0、Xcode 27.0、SDK 27.0)上链接并
运行 C、C++23(含 `std::format`)与 `import std;`;反例以 23.1.2 原装 lld 失败并点出
`arm64e.x1`,`macos-15` 对照两者皆成功。官方 23.1.2 的 macOS 包确实带 libc++ 头文件、
库与 `std` 模块源码(与 §12 R20a 的疑问相反)。
