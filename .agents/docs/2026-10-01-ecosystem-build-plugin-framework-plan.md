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

7. `rules-sycl` 的迁移把「载荷目录」与「要运行的程序」当成了一个字符串。此前
   `dpcpp = payload("dpcpp")` 是目录,代码在两处分别接上 `/bin/clang++` 与 `/lib`;解析器
   回答的是程序,于是 `-L` 指向 `<root>/bin/clang++/lib`,SYCL 消费方在 `compat.opencl`
   处以 `ld.lld: error: unable to find library -lsycl` 失败。改为两个值,并按同一类错误
   审计了全部迁移成员(toolkit 类取 `.root`,工具类取 `.program`,没有第二处把目录接在
   程序路径后)。**本机没装 dpcpp(208 MB),所以本机的 fixture 轮次跳过了这个用例——
   这正是它只能由 CI 发现的原因。**修后本机复现通过:`tests/sycl-consumer` 构建成功,
   `compat.opencl` 正常编译。
8. GNU 响应文件要双写反斜杠,而不是套单引号。LLVM 的 GNU tokenizer 在引号**内**也把反斜杠
   当转义(与 POSIX shell 不同),所以第一版修法无效。clang 22.1.8 实测:响应文件写
   `'-DX=a\b'` 得到 `X=ab`,写 `-DX=a\\b` 得到 `X=a\b`。

9. 被陈述的链接器只在 Linux 的 clang 分支进入链接(`--ld-path` 写在那一支里)。macOS 的
   Apple 链接形状拿不到它:工具进了指纹(改 wrapper 会让快速路径失效),却不参与链接,
   而且什么都不说。由 toolchain-lab 在 macos-15 上量到(`build.ninja` 里没有 `--ld-path`)。
   现在在所有形状之后追加一次;gcc 的树陈述 `ld` 改为在声明处拒绝,并补了反例用例。
   **引擎自己的 e2e 875 断言了这条,但在没装 llvm 载荷的宿主上 SKIP,而 macOS CI 正是这样的
   宿主——这就是 lab 存在的理由。**
10. 一个被陈述的程序路径必须按宿主自己的规则补可执行后缀。Windows 上 `command -v cmake`
    回答 `C:/Program Files/CMake/bin/cmake`(对应 `cmake.exe`),而进程创建会自己补 `.exe`;
    解析器坚持原样拼写,于是拒绝了宿主本可以运行的程序(CI 实测:cmake 消费方报
    `options::cmake = "C:/Program Files/CMake/bin/cmake" (not found)`,随后因为子工程没有配置
    而编译失败)。L2 与引擎的覆盖路径现在用同一条规则,各补一个用例。

11. gcc 16.1.0 拒绝在 `warn@mcpp.rules.qt` 里内联 `std` 模块带来的
    `__gnu_cxx::__normal_iterator::operator*` 与 `operator++`——两者是 `always_inline`,
    而这个文件多 import 了一个模块之后就报 `inlining failed in call to 'always_inline'`,
    点名的却是 `bits/stl_iterator.h`。集合里其他 range-for 都正常。改为按下标遍历(不碰迭代器,
    因此不依赖 BMI 跨模块边界带了什么)。本机以 `MCPP_TOOLCHAIN=gcc@16.1.0` 两次测量:修前 2 个
    错误,修后 0 个,qt 消费方构建通过。与引擎记下的 clang 20.1.7 崩溃同属一类:报错点名的文件
    与改动无关。

**一处可测量的生态效果**:`provision = "on-request"` 之后,macOS 与 Windows 的 `rules` 作业
不再安装 `xim:vcpkg`(main 上会装)。这既是本次要的节省,也正是它暴露了第 4 处发现。

**mcpp-toolchain-lab 已完结**:PR #1 已合入,`main` 在 `3d33c26a`。八个用例(path-llvm、
env-path、launcher-and-ld、fast-path、toolchain-phase、phase-refuses-a-flag、managed-only、
lock-local)在 linux 与 macos-15 全部通过;xcode-27 除已知红的 `toolchain-phase`(#669)外通过。
它给出了第 9 处发现的前后读数:同一个 `launcher-and-ld` 用例,在 `77b632fd` 上
「the linker wrapper ran during the link: no」,在 `cb918615` 与 `136eb2a7` 上为 yes,
`build.ninja` 的 ldflags 末尾出现
`--ld-path=/Users/runner/work/_temp/lab-work/launcher-and-ld/ld-wrapper`,链接出的程序运行并打印
`toolchain-lab: sum=6 count=3 greeting=hello 42`。`lock-local` 按引擎的实际行为改写:lock 里有
解析出的依赖、没有工具链。

**mcpp-framework-lab 已完结**:PR #1 squash 合入 `90c630f1`,10 个用例 × 3 平台 = 29 通过、
1 跳过(`override-bare-name` 在 Windows 跳过:shell 内建是 POSIX 的概念,而引擎在 Windows 用
`where`,它只报告程序——引擎自己的用例同理跳过)。用例:control、choice-build-mcpp、
override-env、override-manifest、override-from-dependency、override-bare-name、managed-only、
why、timing、default。除判据外还加了一条:从 `CMakeCache.txt` 读 `CMAKE_COMMAND`,确认真正
运行的 cmake 就是被陈述的那一个。

**「下载耗时减少多少」的读数**(run 36888453587,全部冷测:`xim:cmake` 事先未安装,control
自己的日志里有 `Downloading xim:cmake`):

| 平台 | control(秒) | 点名宿主 cmake(秒) | 差 | xim:cmake 载荷 |
|---|---|---|---|---|
| ubuntu-24.04 | 29.5 | 6.4 / 6.3 | 23.2 | 61.9 MB 下载 3.5 s,安装后 207 MB |
| macos-15 | 19.6 | 5.4 / 4.3 | 14.8 | 85.9 MB 下载 9.4 s,安装后 265 MB |
| windows-2022 | 20.0 | 5.8 / 5.7 | 14.2 | 51.9 MB 下载 10.0 s,安装后 152 MB |

**省下的是什么**:第四次构建(control,但载荷已安装)为 6.4 / 5.0 / 5.7 秒,与点名宿主 cmake
基本相同。所以省下的是把载荷供给进一个 `MCPP_HOME` 的一次性成本,不是每次构建的成本——这正是
`provision = "on-request"` 要省的那一项,也说明它对已有缓存的 CI 不会再省第二次。

**llvm-macos27-lab 已完结**：PR #1 已 squash 合入(`3f2cceaf2cac`),主干运行 36878561820 六个
作业全绿,工具链资产已发布并校验:tag `toolchain-21ef2ddb8060`、
`llvm-23.1.2-x1-macos-arm64.tar.xz`、77,959,884 字节、
sha256 `40b9fa16be5628a5d277824f961faa33dadbf84d9520cff9fe2aeb9b0b217ebf`(下载后 `sha256sum -c`
通过)。lld 报 `LLD 23.1.3 (…21ef2ddb…)`,包含 `ee66426152f9` 与 `532fa5afbe2b`。一处与预期不同:
原装 lld 的报错词是 `unknown target` 而不是 `unknown architecture`,两者都点出 `arm64e.x1`,
负例判据因此接受两种措辞,并仍要求 `could not load TAPI file` 与链接失败。另一处事实:同一台
`xcode-27` runner 上,原装 lld 对随附的 `MacOSX26.5.sdk` 链接正常——只有 27.x SDK 列出
`arm64e.x1`。冷构建 lld 2033 秒(3 核 7 GB),ccache 命中后 77 秒。

**llvm-macos27-lab**（speak-agent，公开）：`release/23.x` 的 `21ef2ddb8060`（含
`ee66426152f9`）构建的 lld 在 `xcode-27`(macOS 27.0、Xcode 27.0、SDK 27.0)上链接并
运行 C、C++23(含 `std::format`)与 `import std;`;反例以 23.1.2 原装 lld 失败并点出
`arm64e.x1`,`macos-15` 对照两者皆成功。官方 23.1.2 的 macOS 包确实带 libc++ 头文件、
库与 `std` 模块源码(与 §12 R20a 的疑问相反)。

## 5. 生态级 review

按「这条机制在生态的每个接缝处是否闭合」来看,而不是按仓库看。

**1. 链条闭合。** 引擎给出语义与接口(协议 15 的六个访问器与两条指令、三个清单键、来源记录),
L2 把它们变成一个解析器与一个工具链描述,成员只写自己的知识,消费方什么都不用写。闭合的读数有
三处:`tests/cmake-consumer` 在 `MCPP_NO_AUTO_INSTALL=1` 下由 `build.mcpp` 点名 cmake 即可构建;
framework-lab 的 8 个用例在 ubuntu-24.04、macos-15、windows-2022 上全绿;`plugin-logic` 27 例
覆盖 13 个成员的决定。

**2. 版本地板与无感升级。** 0.19.0 的地板是 2026.10.1.3。两个方向都量过:新插件在旧引擎上,
2026.10.1.2 只说 `unknown key 'provision'`,不提版本——因为地板检查需要那次解析没能产出的文档;
这成了第 7 处修复(解析失败路径从文件文本读地板)。旧插件在新引擎上,默认路径的输出与行为逐字
不变,由既有 e2e 与 framework-lab 的 `default` 用例断言。

**3. 默认代价的真实变化。** `provision = "on-request"` 之后,plugins CI 的 macOS 与 Windows
`rules` 作业日志里 `xim:vcpkg` 出现 0 次(main 上这两个作业会 provisioning
`xim:vcpkg@>=2026.7.27`)。节省是真的,而它也正是第 4 处发现的来源:少装一个载荷,会让依赖
「装过」的测试环境露出假设。

**4. 可观察性是一条路径,不是一个开关。** 一个显式来源 = 一行 `Using`(类与出处)+ 一条
`Finished` 汇总项 + `resolution.json` 的一条 `sources` 记录 + `mcpp why` 的一段。四处读的是同一条
`SourceDecision`,所以不可能三处一致、一处落后。

**5. 新增的风险,按发生的那次记。** (a) 构建程序的环境是「在构建中跑的测试宿主」的契约的一部分:
引擎多设一个变量,就可能改变一个这样的宿主的答案(第 4 处发现);(b) 命令长度是个有名字的通道,
而宿主模块数量是它的无界载荷(第 3 处发现);(c) 一个按路径命名的工具链把产物与机器状态绑在一起,
所以身份按内容而不是按版本,并写进 stamp 供快速路径比较。

**6. 没做的事,以及为什么不做。** 工具链描述数据化(核心读描述文件)——`[toolchain] { path }`
的字段已经与那份描述同形,所以它是后续的第三种载体,而不是改写;`mcpp.lock` 不记工具链——lock 的
内容是依赖解析的结果;`kind = "bin"` 的宿主工具仍走 `[tools.overrides]`——键空间与语义不同,
合并会让一张表有两种键。
