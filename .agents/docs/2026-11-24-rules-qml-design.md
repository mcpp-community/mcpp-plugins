---
subject: rules-qml
status: implemented in mcpp-plugins#66 (revision 5: the engine fixes mcpp#1011, so the plugin_closure workaround and mcpp.plugins.binary are not in the PR)
---

# `rules-qml`：mcpp 对 QML 的一等支持（单 PR、三平台、含测试）

读者：本仓库维护者（review 用）。本文是设计，不含实现。
证据来自 2026-11-24 在本机（Linux x86_64，`xim:qt` 6.11.1，gcc 16.1，LLVM 20.1.7 / 22.1.8 / 23.1.3 / 23.1.6，mcpp 2026.10.31.3 / 2026.11.7.2 / 2026.11.20.1）做的实验；原型代码在
[`2026-11-24-rules-qml-spikes/`](2026-11-24-rules-qml-spikes/README.md)。
Windows 与 macOS 没有实测机器：凡依赖它们的结论都标"推断"，并各配一个 CI 断言（§10.3）。

修订记录：第 1 版把 8 个问题留给 review。第 2 版对每个问题做了决定并用实验验证；验证推翻了第 1 版的两处设计（文本闭包的算法、`mcpp_qml_keep()`），并发现了引擎的闭包缺口（Q9）。第 3 版：按你的意见补充 LLVM 验证（Q10），闭包补丁改为两个规则都默认开、无感、带标注与金丝雀；并处理了一次独立评审的全部意见（§16）。验证中纠正了我自己的一处错误：先前报告的"clang + libstdc++"其实用的是 libc++（Q10）。第 4 版：工具链与 `rules-qt` 对齐、Linux 不做 LLVM（Q10）；引擎 issue 已提交（mcpp#1011）；`qsb` 不可运行的根因是 `xim:qt-addons` 没声明 `xim:glibc`，已给 xim-pkgindex 提修复 PR（Q5）。

## 修订 5：实现时的变更（以此为准，下文 Q2、Q9、§7 `link_plugin_libs`、§10 中涉及它们的部分已被取代）

- **mcpp#1011 已在引擎侧修复**（mcpp 分支 `feat/kernel-abi-supplier`，提交 `b31e1da2`：`mcpp::deploy` 放到程序旁的同格式对象成为 `mcpp pack` 闭包的根）。按你的意见，不再带 workaround：**`plugin_closure`、`link_plugin_libs`、`ENGINE-GAP` 标记与 `check-engine-gaps.sh`、金丝雀都不进本 PR**。唯一的读取依赖表的使用者随之消失，所以**`mcpp.plugins.binary`（含 `plugins-binary`、`binary-reader` fixture）也不进 PR**；它的实现与 180 个真实 ELF 对照的结果保留在 PR 的提交历史（`d0ae156` 等）与 `spikes/binary.hpp`。
- 本机用该分支临时构建的 mcpp（2026.11.23.1，只读使用）验证：`qml-deploy` 打包后 1058 个依赖均在包内（含 `libQt6QuickLayouts.so.6`、`libQt6XcbQpa.so.6`），SDK 目录改名后打包程序能运行；`qt-widgets-consumer` 的包含 `libQt6XcbQpa.so.6`。
- **合入条件**：该修复随 mcpp 发布后，把包的 mcpp 下限与 README 中 `rules-qml`、`rules-qt` 的下限列提到该版本，CI 的 `MCPP_BOOTSTRAP` 固定到它；此前 PR 保持 draft。
- 其他与设计不同处：`qml::options` 没有自己的 `out_dir`（用 `qt.out_dir`）；`rcc` 动作带 `--depfile`（仓库 CI 要求每个规则声明 depfile）；`.qmltypes` 之外的非 SDK 插件依赖不再警告（随读取器一起去掉）；Windows 上含 `<` `>` 的资源名用例改为运行时创建、主机不能创建则不断言。

## 0. 核心点（先看这里）

1. **新增独立成员 `rules-qml`**（模块 `mcpp.rules.qml`），隐含 `rules-qt`，不并入。不认领 `.qml` 扩展名，因此和 `tools-*`、`dist-*` 一样**不写 `rule_module`**，消费者自己写 `host-module = true`（隐含的 `rules-qt` 仍有自己的 `rule_module` 并认领 `.ui/.qrc/.ts`，互不影响）。
2. **一次 `qml::compile(options)` 完成整条链路**：moc(JSON) → `qmltyperegistrar` → `qmldir` → qrc → `rcc` → `qmlcachegen`(AOT) → 编进程序。等价于 `qt_add_qml_module`；应用自带模块静态链接进可执行文件，用 `loadFromModule` 加载。整条链路已在真实引擎里用原型跑通（点分 uri、C++ 类型、单例、`.js`、嵌套目录、资源文件，输出 `cpp=84 singleton=red js=42 nested=7 resource=hello-resource`）。
3. **部署是难点**，设计由三部分组成，均已验证：
   - **模块闭包**用计划期纯文本解析得到，规则是"qmldir 的 `import`/`default import`/`optional import`/`depends` 行 + 模块目录里所有 `.qml` 的 `import` 语句"。第一版漏了后一半，与 `qmlimportscanner` 差 16 个模块；修正后在 4 个样本（含 Qt 全部 40 个顶层模块）上与 scanner **完全一致**。
   - **补库**：读已部署插件的依赖表，把属于 SDK 的库链进程序（Linux 用 `--push-state,--no-as-needed` 包住以防被 `--as-needed` 丢弃，macOS 用 `-needed_library`），Windows 用 `deploy` 放到程序旁。读取器 `mcpp.plugins.binary` 对 180 个真实 ELF 与 `readelf` 一致，对 mingw 构建的 PE、lld 构建的 Mach-O（含 fat）与 `objdump`/`otool` 一致，1500 个截断/损坏输入无崩溃。
   - **`qt.conf`** 默认写入并部署（`Prefix = .`，`Qml2Imports = qml`）。
4. **新发现的缺口（Q9）：`mcpp pack` 的闭包只从程序本身出发，不带已部署插件的依赖。** 这既影响 QML 插件，也影响现有 `rules-qt` 的平台插件（`libqxcb.so` 需要的 `libQt6XcbQpa.so.6` 不在包里；现有 CI 只在 offscreen 下运行所以没发现）。处理方式：**规则里加一个对用户完全无感的临时补丁（`rules-qt` 与 `rules-qml` 都默认开，用户不用配置任何东西），源码与文档都醒目标注为"引擎缺口补丁"；补丁是幂等的，引擎修好后它变成无影响的冗余；CI 里有一个"金丝雀"，一旦引擎自己带上这些库就提示删除补丁。** 同时向 mcpp 提了 issue：**mcpp-community/mcpp#1011**（§15）。
5. **测试**：`plugin-logic` 纯逻辑用例（含逐前缀截断的读取器用例）；4 个三平台 fixture（`qml-consumer`、`qml-modules`、`qml-deploy`、`qml-import-only`）加 1 个仅 Linux 且带探测门的 `qml-shaders`；各平台用其默认工具链自带的工具（Linux `readelf`/`nm`，macOS 与 Windows `llvm-readobj`/`llvm-nm`）与 `qmlimportscanner` 对照；打包闭包用静态分析断言，并在**把 SDK 目录挪走**的条件下运行。
6. **生态侧**：`xim:qt` 已含全部 QML 工具，不新增。`xim:qt-addons` 的 `qsb` 起不来是因为它的 Linux 条目没声明运行时依赖（所以没被 elfpatch）；我查清根因、在隔离环境里真实安装验证后，给 xim-pkgindex 提了修复 PR（#1262，Q5）。合入并发布之前，`qsb` 的真实运行测试按探测门执行，之后自动翻转。
7. **工具链与 `rules-qt` 对齐，不引入新的选择**：`rules-qt` 的 fixture 没有 `[toolchain]` 覆盖；Linux 只加 `cxx_runtime = "toolchain-coupled"`（Qt 官方 Linux 构建用共享 libstdc++，所以用 gcc），Windows、macOS 用引擎对该主机的默认工具链（mcpp 的 LLVM clang 等）。`rules-qml` 的 fixture 逐字沿用这几行。**Linux 不支持 clang + Qt**（引擎拒绝 libc++，`rules-qt` 已有警告），所以不在 Linux 上做 LLVM 的 CI；我本机做的 LLVM 验证（4 个版本编译、链接、运行生成的 C++ 通过）作为证据保留（Q10），不进 CI。
8. **继承的限制（实测）**：项目路径含空格或非 ASCII 字符时，gcc 的模块依赖扫描失败，连现有 `qt-consumer` 也一样；与 QML 无关，写进限制，测试用 ASCII 路径。

## 1. 目标与非目标

目标（本 PR 全部实现）：

| # | 能力 | 对应 CMake |
|---|---|---|
| G1 | 应用自带 QML 模块（uri、版本、`.qml`/`.js`/`.mjs`、单例、资源文件、嵌套目录） | `qt_add_qml_module` |
| G2 | C++ 类型注册（`QML_ELEMENT` 等）与 `.qmltypes` | 同上 |
| G3 | AOT 编译（`qmlcachegen`）及加载器 | 同上，默认开 |
| G4 | 多模块、点分 uri、模块间依赖 | 多次调用 |
| G5 | Qt 自带 QML 模块的部署与打包闭包 | `windeployqt --qmldir` |
| G6 | `qt.conf` | `windeployqt` / 手写 |
| G7 | 着色器 `qsb` 并嵌入模块资源 | `qt_add_shaders` |
| G8 | `qmllint` 检查动作 | `<target>_qmllint` |
| G9 | QML/JS 中 `qsTr` 的翻译提取 | 内置 |
| G10 | Linux / Windows / macOS 构建、运行、`mcpp pack --format dir` | — |
| G11 | 已部署插件（含 `rules-qt` 的平台插件）的闭包补全 | `windeployqt` 的依赖扫描 |

非目标（文档的"限制"一节写明）：

- **`qmltc`**（QML 编成 C++ 类，可做 `import app.ui;`）：覆盖的 QML 子集有限，另做。
- **QML 插件做成独立共享库**（`qt_add_qml_plugin`）：应用自带模块一律静态进可执行文件。
- **来自依赖包的 QML 模块**：只提供 `import_paths` 入口。由此，**跨包静态库里的模块注册可能被链接器丢弃**（Q8），文档说明：在代码里引用生成的 `qml_register_types_<uri>`。
- **macOS `.app` bundle 与 `dist-apple`** 的 QML 布局（Q7）。
- **`.qmlls.ini` 生成**（mcpp-ide）、**`qmlformat`**、**`.qmllint.ini`**、**Qt 5**。
- **Android、iOS、WebAssembly 目标**：QML 需要对应的 Qt SDK；`qml::compile` 在 `target_os` 不是 linux、windows、macos 时报一条警告并跳过，不声明任何动作。
- **一个包里有多个目标**：生成的源文件会进入包的每个目标（实测：`kind = "lib"` 加 `bin` 时两者都编译这些对象）。文档要求 QML 模块只放在一个目标的包里，不构造反例。
- **项目路径含空格或非 ASCII 字符**：gcc 的模块依赖扫描报 `cannot determine '#include' translation … malformed string 'ERROR … INCLUDE-TRANSLATE'`（实测，现有 `qt-consumer` 在 `/tmp/qtc-测试` 同样失败，在 ASCII 路径下成功；空格同样）。继承自工具链，不是本 PR 引入，也不在本 PR 修；文档写明，fixture 用 ASCII 路径。
- **`.js` 里的 `.import`、`.mjs` 的 `import` 语句**：文本闭包不解析（`qmlimportscanner` 对它们的处理未验证），用 `options::imports` 补。
- **`qtquickcontrols2.conf` / `QQuickStyle::setStyle` 选择的样式**：运行时才知道，所以默认部署全部样式；想缩小用 `styles`。

## 2. 决定与验证

| # | 决定 | 证据 |
|---|---|---|
| Q1 | 部署闭包用**计划期文本解析**；`qmlimportscanner` 只作 CI 对照基准 | 见下 |
| Q2 | 依赖表读取器做成**新通用模块** `mcpp.plugins.binary`（feature `plugins-binary`，纯 `std`），由 `rules-qt`、`rules-qml` 使用 | 见下 |
| Q3 | `write_qt_conf` **默认开**；`Translations` 随 `i18n.deploy_to` 生成；检测到用户自己的 `qt.conf` 则不写并警告 | 见下 |
| Q4 | 版本 **0.27.0**；README 的 mcpp 下限列写 **2026.10.1.3**（与 `rules-qt` 相同，列的语义是"该成员的行为最早需要的版本"），整个包仍是 2026.10.30.1 | 见下 |
| Q5 | `qsb` 实现；argv 由单元用例固定；真实运行放**带探测门**的 Linux fixture | 见下 |
| Q6 | `aot` **默认开** | 见下 |
| Q7 | macOS bundle（`dist-apple`）**不纳入**，本 PR 测 `pack --format dir` | 范围决定，无本机证据 |
| Q8 | **取消** `mcpp_qml_keep()`，限制写进文档 | 见下 |
| Q9（新） | 闭包缺口的根因在引擎（已提 mcpp#1011，§15）；本 PR 带**对用户无感、两个规则都默认开、幂等、有标注与金丝雀**的临时补丁 `plugin_closure` | 见下 |
| Q10（新） | LLVM：生成的 C++ 在 4 个 LLVM 版本上编译并运行通过；macOS、Windows 的 clang 本身无法验证，由三平台 CI 覆盖 | 见下 |

### Q1 证据：计划期能跑子进程，但不该用；文本闭包已与 scanner 对齐

- 在 `build.mcpp` 的 `main()` 里 `std::system` 运行 `qmlimportscanner` 可行（生成了 56 KB 的 JSON）。但 mcpp 自己的文档（`docs/30-build-mcpp.md`）明确建议：工具放进动作的命令里运行，不要在 `main()` 里运行，否则 `emit build-database` 在工具不可得时会让整个计划丢失这个包的指令。
- 文本闭包的第一版（只读 qmldir 的 `import`/`depends`，不跟随 `optional`）与 scanner 比较：app1 少 16 个模块。原因两个：Qt 模块目录里的 `.qml` 文件自己也 `import` 别的模块；`optional import … auto`（样式）scanner 会跟随。
- 修正后的算法（`spikes/closure.py`）：
  1. 起点：用户所有 `.qml` 的 `import` 语句（去注释与字符串，不含应用自带模块的 uri）+ `options::imports`；
  2. 对每个 uri 找 `<sdk>/qml/<uri 路径>/qmldir`（及 `import_paths`），跟随其中 `import`、`default import`、`optional import`、`depends`；
  3. 并读取该模块目录下**所有 `.qml`**（递归，但不进入自带 `qmldir` 的子目录，它们是别的模块）的 `import`。
- 结果：4 个样本（Controls；Layouts/Window/Shapes/Dialogs 混合；Material+labs+Effects+Templates；Qt 全部 40 个顶层模块）上 `scanner-only` 与 `mine-only` 均为空（24/27/15/49 个模块）。
- `import` 语句的识别：`import <uri> [版本] [as 名]`，允许行尾 `;`；一行里多条 `import` 不识别（QML 惯例是一行一条）。
- 样式默认全部跟随（与 `windeployqt` 一致，保证运行时选任何样式都不缺）；`options::styles` 非空时，`optional import` 只跟随其中列出的。实测规模（`QtQuick` + `QtQuick.Controls`）：

  | 配置 | 模块 | 部署文件 | QML 体积 | 需补的 Qt 库 |
  |---|---|---|---|---|
  | 默认（全部样式） | 24 | 1231 | 6.8 MB | 30 个，99 MB |
  | `styles = {"Material"}` | 15 | 173 | 0.7 MB | 22 个，91 MB |
  | `styles = {"Basic"}`（只剩默认样式） | 12 | 100 | 0.4 MB | 20 个，89 MB |

### Q2 证据：读取器正确，且可安全读不可信文件

`spikes/binary.hpp`（120 行，纯 `std`）：

| 格式 | 对照 | 结果 |
|---|---|---|
| ELF（`DT_NEEDED`） | `readelf -d`，SDK 里 `qml/`、`plugins/`、`lib/`、`libexec/` 的 180 个真实文件（全是 64 位小端） | 180/180 逐集合一致 |
| PE（导入表） | mingw 构建的 64 位 `a.dll`（导入 `b.dll`）、`b.dll`、`m.exe`，`x86_64-w64-mingw32-objdump -p` 与 `llvm-objdump -p` | 一致 |
| Mach-O（`LC_LOAD_DYLIB`；fat） | LLVM 的 `clang` + `ld64.lld` 构建 arm64、x86_64 的 dylib，手工拼 fat；`llvm-otool -L` | 一致。注意：`otool -L` 与 `llvm-readobj --needed-libs` 的结果都**含文件自己的 `LC_ID_DYLIB`**，读取器不返回它；用例与 CI 对照都要先把它去掉 |
| 健壮性 | 5 个样本各 300 次截断/随机改字节（共 1500 次），ASan + UBSan | 0 次崩溃，823 次报告错误 |

**覆盖的边界**：真实样本只含 64 位 ELF、64 位 PE（普通导入表）、64 位 Mach-O 的 `LC_LOAD_DYLIB`（thin 与 fat）。**没有真实样本**、只由 `plugin-logic` 里合成的最小样本覆盖的有：32 位 ELF/PE/Mach-O、PE 的延迟导入表、Mach-O 的 weak/reexport/upward/lazy 命令、`FAT_MAGIC_64`；大端 ELF 不读，返回错误。**LLVM 的 `llvm-readobj --needed-libs` 对 ELF、PE、Mach-O（含 fat）都能给出依赖**（实测），所以三平台 CI 可以用同一个工具做对照基准（Q10）。读文件用 `std::filesystem::path` 打开（不用窄字符串路径），保证 Windows 上非 ASCII 路径可读。这些路径对 Qt 的桌面 SDK 都不会出现，所以不影响本设计，但文档不声称它们经过真实文件验证。

设计约束：读取器只返回文件里的名字，**调用方只接受**不含 `/`、`\`、`..` 且在 SDK 库目录里确有同名文件的名字；整个文件读入内存，上限 1 GiB，超过报错。

### Q3 证据：`qt.conf` 的真实效果

- 只要程序旁有 `qt.conf`，`Prefix` 就变成该目录，**无法省略**（试了只写 `Qml2Imports`、写 `./qml`、以及加 `Data`/`Settings` 覆盖，`Prefix` 都是程序目录）。同时被改变的有 `Data`、`Settings`、`Libraries`、`Translations`（变为 `<程序目录>/translations`，恰是 `rules-qt` 的 `i18n.deploy_to` 默认值）。`Plugins` 保持默认；程序目录仍在 `libraryPaths` 里，所以 `<程序目录>/platforms` 照常被找到。
- 没有 `qt.conf` 时，开发树里 Qt 的路径指向 SDK，打包树里默认的 QML 路径是 `<包根>/qml`（实测：把 `qml/` 放在包根就能找到），而 `deploy` 只能放在程序目录之下，到不了包根。
- 有 `qt.conf` 时开发树与打包树**行为一致**（都只用部署出来的 `qml/`）：一份不完整的部署（实测：只部署了两个模块）在 `mcpp run` 时就失败（`roots=0`），而不是开发时靠 SDK 兜底、打包后才失败。完整闭包部署后 `mcpp run` 通过（`roots=1`）。
- 替代方案"把模块直接放到程序目录"（也实测可行，不需要 `qt.conf`）被否决：程序目录会被 `QtQuick/`、`QtQml/` 等污染，且开发树会兜底到 SDK，掩盖闭包缺口。
- 副作用写进文档；`write_qt_conf = false` 关闭。
- `qt.conf` 的内容不是固定文本：`Translations` 取 `qt.i18n.deploy_to`（默认 `translations`，非默认值时写出，否则 Qt 会在 `<程序目录>/translations` 找而丢失翻译）。
- 项目已有自己的 `qt.conf`（包根或 `res/` 下有 `qt.conf`，或 qrc 里含 `/qt/etc/qt.conf`——Qt 的内嵌 `qt.conf` 优先于文件）：不写、不部署，打印一条警告说明由项目负责 `Qml2Imports`。
- macOS `.app` bundle 里 `Prefix = .` 的含义不同（`dist-apple` 把部署文件放到 `Contents/Resources`，Qt 在 bundle 里读 `Resources/qt.conf`），**推断**它与 `qml/` 同落在 `Resources` 下因而仍然成立，未验证，属于 Q7 的范围。

### Q4 证据：下限与 README 的列

- README 的 mcpp 下限列含义是"该成员的行为最早需要的版本"（`rules-qt` 写 2026.10.1.3，低于包下限 2026.10.30.1）。`rules-qml` 自己只用 `rules-qt` 已需要的东西（`mcpp.plugins.tool`，协议 15，mcpp#755）和更早的 `action::env`（协议 13，`src/build/hostprogram.cppm:441`）、`deploy`、`link_flag`，所以该列写 **2026.10.1.3**；整个包仍需要 2026.10.30.1（README 已有这句话）。
- 验证到的范围：spike 的动作链（`action`、`roles`、`include_dir`）在 mcpp 2026.10.31.3、2026.11.7.2、2026.11.20.1 上 `mcpp run` 都得到 `roots=1`；`deploy`、`link_flag`、`qt.conf` 的端到端原型只在 2026.11.20.1 上跑过（这些 API 早已存在，现有规则在更低版本上使用）。包下限 2026.10.30.1 本身与 2026.10.1.3 本机都没有，**已验证的最低版本是 2026.10.31.3**，README 注明。CI 固定版本 `MCPP_BOOTSTRAP = 2026.11.7.1`。

### Q5：`qsb` 在 `xim:qt-addons` 里不能运行——是包的依赖没配齐，已提修复 PR（openxlings/xim-pkgindex#1262，已合入）

**回答你的问题：是，Linux 条目没有声明运行时依赖。** `pkgs/q/qt-addons.lua` 的 Linux 只有 `runtime = { "xim:qt@6.11.1" }` 和 `build = { "xim:7zip" }`。`docs/10-adding-a-package.md` 说明：声明 `xim:glibc`（loader 提供者）才会让 xlings 在 `install()` 之后做 elfpatch，把每个可执行文件的解释器和每个 ELF 的 RPATH 改成生态的。`qt.lua` 与 `qt-base.lua` 声明了，所以它们的 `moc` 等是补丁过的；`qt-addons` 没声明，它的 220 个 ELF 文件原样保留。

**症状与证据**（本机，已安装的 `xim:qt-addons` 6.11.1）：
- `readelf -l bin/qsb`：解释器是系统的 `/lib64/ld-linux-x86-64.so.2`；`readelf -d`：`RUNPATH $ORIGIN/../lib`（对照 `xim:qt` 的 `moc`：解释器是 `…/abi/linux-x86_64-glibc/ld-linux-x86-64.so.2`，RPATH 是一串生态的库目录）。
- 在 mcpp 动作里运行：`libQt6Core.so.6: version 'Qt_6.11' not found`（回落到发行版自己的 Qt）。把 `xim:qt` 的 lib 放进 `LD_LIBRARY_PATH` 后改为 `GLIBC_2.43 not found, required by xim:glib`。

**我的第一版修复不够**：只加 `xim:glibc`。在隔离的 `XLINGS_HOME` 里真实安装后，`readelf` 显示 RPATH 只含 `qt-addons/lib`、`xim:qt/lib`、glibc 的 abi 目录，`qsb` 报 `libxkbcommon.so.0: cannot open shared object file`。结论：**补丁写入的 RPATH 是本配方自己声明的包的库目录，不含 `xim:qt` 的传递依赖**，所以本包的 ELF 直接用到的库要在这里再声明一遍。我在 PR 的注释里写明了这一点。

**修复（只改 Linux）**，范围由实测决定：
- 对载荷的 220 个 ELF 逐个读 `DT_NEEDED`，找出不在本包 `lib/` 也不在 `xim:qt` 的 `lib/` 里的名字，映射到生态里提供它们的包：`xim:glibc`（libc、libm、ld-linux）、`xim:gcc-runtime`（libstdc++、libgcc_s）、`xim:libglvnd`（libGL、libEGL）、`xim:libxkbcommon`、`xim:libX11`、`xim:libXext`、`xim:libXrandr`（FFmpeg 后端）、`xim:libdrm`、`xim:wayland`（客户端、服务端、egl）、`xim:zlib`、`xim:glib`。
- `exports.runtime.libdirs = { "lib" }`；Linux 的 6.11.1 条目 `revision = 1`（与 `qt-base.lua` 同，使机器上未补丁的 revision 0 被替换）；`install()` 写、`installed()` 查 runtime 标记，**只在 Linux**，Windows 与 macOS 已装的 128–160 MB 载荷不会被判为过期。
- 生态里没有的、所以没声明的：`libbz2`、`libpulse`、`libbluetooth`、`libpcsclite`、`libspeechd`，以及 `libtiff.so.5`（`xim:libtiff` 是 `.so.6`）。它们只被 Qt Multimedia 的 FFmpeg 后端、Qt NFC、Qt Connectivity 的 `sdpscanner`、Qt TextToSpeech、TIFF 图像插件用。

**验证**（隔离 `XLINGS_HOME`，真实安装 `xim:qt` 再安装修改后的 `xim:qt-addons`）：解释器是生态的，RPATH 列出所有声明的包；`bin/qsb --version` 输出 `qsb 6.11.1`；`qsb --qt6 -o t.frag.qsb t.frag` 编出 1072 字节的 `.qsb`；20 个可执行文件里 19 个能启动，只有 `libexec/sdpscanner` 仍因缺 `libbluetooth.so.3` 起不来（生态没有这个包）。仓库自己的测试：新增 2 个静态测试（在旧配方上失败、新配方上通过）和 3 个 verify 测试；`pytest -m "static or isolation"` 6866 通过，`tests/n/test_node.py` 的 2 个失败在改动前后都存在（与本改动无关）。

**PR 状态**（#1262）：CI 全部通过——`ci_xpkg_test` 的静态与隔离测试、`install` 作业在 Linux、macOS、Windows 三平台、`runtime_revision_gate`、`required`。Linux 的 `install` 作业用修改后的配方真实安装了 `xim:qt-addons`（40 个包），并运行了仓库自己的依赖闭包检查（`dep-closure-check.sh`）：220 个 ELF，报告的 host 独有 SONAME 恰是我列出的那 8 个（`libbz2`、`libpulse`、`libbluetooth`、`libpcsclite`、`libspeechd`、`libtiff.so.5`、两个 WebEngine 库），与我独立统计的结果一致。**合入状态：已由你合入**（2026-11-23 19:06 UTC，合入提交 `53ddeb3`）。合入前被分支保护的"需要评审"拦住，我没有绕过保护。
**我在 PR 里如实写了一个评审点**：该脚本的规则 D2 是"解释器指向生态 loader 的载荷，host 独有的 SONAME 是硬失败"。本修复让 `qt-addons` 用上生态 loader，仍有这 8 个无提供者的 SONAME。脚本之所以只当备注通过，是因为它用解释器路径含 `/xpkgs/` 来识别"生态 loader"，而补丁后的路径是 `<home>/abi/linux-x86_64-glibc/ld-linux-…`，不含 `/xpkgs/`——所以 D2 对任何这样打补丁的载荷都不生效（脚本的潜在缺陷，本 PR 不改）。受影响的只有需要这些库的可选功能（Qt Multimedia 的 PulseAudio/FFmpeg 后端、NFC、`sdpscanner`、speech-dispatcher 插件、TIFF 插件）；mcpp 构建的程序本来就跑在生态 loader 下，所以预期它们之前也找不到 host 的库，但我没有运行过这些功能，这是预期，不是测量。

**对本设计的影响**（修复已合入，待索引发布到使用者）：
- 规则侧不变：`qsb` 动作仍用 `action::env` 给出 SDK 库目录，`qml-shaders` 仍先探测 `qsb --version` 再决定真实断言还是 `::notice::` 跳过。**修复合入并发布到索引后，探测会自动通过，门自动翻转，不需要改规则**；CI 用的是已发布的索引，旧载荷（revision 0）会因 revision 1 被替换。
- 之后可以把探测门删掉，让 `qml-shaders` 直接断言（后续项，见 §11）。
- G7 在门翻转之前仍然只有命令行级覆盖（§10.3 如实写明）。

### Q6 证据：AOT 的代价与收益

40 个中等大小的 `.qml`（每个约 25 行，含绑定与函数）：

| | 构建（冷） | 可执行文件 | 目标文件数 |
|---|---|---|---|
| `aot = true` | 10.3 s | 4.3 MB | 145 |
| `aot = false` | 4.2 s | 0.1 MB | 103 |

即每个文件约 +0.15 s、+105 KB。同时 AOT 把 QML 语法错误提前到构建期（`Error compiling qml file: …: Unexpected token`，构建失败并点名文件与位置），不开则要到运行时才看到。Qt 自己的 CMake 默认也开。默认开，`aot = false` 是逃生口；`rules-qml` 文档写明代价。

### Q8 证据：模块内无需保活

- 在同一个包里加一个 `kind = "lib"` 目标，生成的对象进了 `libmodlib.a`，但同包的可执行文件仍然直接编译了这些对象（`nm` 在可执行文件里找到 `qml_register_types_…`，运行 `cpp=84 …` 通过）。
- 用普通 gcc 复现了归档成员被丢弃的工具链事实（引用才保留；直接传目标文件也保留）：这只发生在**别的包**的静态库里，而那是非目标（§1）。
- 因此第 1 版的 `<out>/qml/mcpp_qml.h` 与 `mcpp_qml_keep()` 取消；文档的"限制"写明做法。

### Q9：闭包缺口的根因在引擎；本 PR 的补库是文档化的临时补丁

**根因（读了引擎代码确认）**：`mcpp pack` 的闭包只从**程序本身**的导入表出发（`src/pack/pack.cppm` 的 `read_closure` 以 `in.object = program` 为唯一根）。`mcpp::deploy` 放到程序旁的插件库（manifest 里记为 `role: library`）不是闭包的根，所以只被 `dlopen` 的插件所依赖的库不会被带上。

**症状**：
- QML：部署 `QtQuick/Layouts` 的插件后，包里没有 `libQt6QuickLayouts.so.6`；Controls 的 `libQt6QuickControls2Impl.so.6` 同样缺。
- **现有 `rules-qt` 的平台插件也有同一缺口**：对已部署 `platforms/` 的打包目录逐个读依赖，缺 `libQt6XcbQpa.so.6`（`libqxcb.so`）、`libQt6WaylandClient.so.6`（`libqwayland.so`）、`libQt6EglFSDeviceIntegration.so.6`（`libqeglfs.so`）。真实 X11 桌面上，xcb 平台插件会因缺 `libQt6XcbQpa` 加载失败；现有 CI 只在 offscreen 下运行，所以没发现。（我没有显示环境，只在静态依赖层面确认，运行层面的失败是推断。）

**与仓库规则的关系**：`docs/plugin-development.md` 的规则 4（运行时闭包属于提供二进制的包；插件只命名库目录）与规则 8（引擎缺口要报告，不要打补丁；已有的补丁要在文档里记录 issue 号并在引擎支持后移除）。因此：

1. **向 mcpp 提 issue**（你同意后已提交）：**mcpp-community/mcpp#1011**，建议能力是"`mcpp pack` 把 `role: library` 的已部署文件也作为闭包的根"。正文见 §15。提交前搜索过，没有重复（相关的 #615、#634、#630、#701 都是已关闭的、讨论别的缺口）。
2. **本 PR 带 `link_plugin_libs` 补丁，按规则 8 标注**（这是你的决定：先 workaround，用户与开发者层面无感，mcpp 修复后自动没问题）：
   - **无感**：`rules-qt`、`rules-qml` 都默认开，不需要任何配置；补丁失败（读不了某个文件、平台读取器报错）只是 `mcpp::warning`，不让构建失败。字段 `plugin_closure` 保留，只在"引擎缺口"一节里出现，用于金丝雀与应急关闭。
   - **标注**：仓库已有同类机制——`compat/` 单元的 `COMPATIBILITY UNIT` 头与 `check-compat-retirement.sh`（到期未删则 CI 失败）。引擎缺口没有固定日期，所以不用日期，而是仿照它做一个**同构的标记与检查**：补丁代码整块以下面的头开头，`.github/scripts/check-engine-gaps.sh` 列出所有这样的块，要求每块都有 `issue:` 与 `remove when:`，缺任何一项则 CI 失败（和 compat 检查一样防止"标记拼错导致清单为空"：凡含 `ENGINE-GAP` 字样而不合格式的也报错）：

     ```
     // ENGINE-GAP WORKAROUND
     //   gap:         mcpp pack's runtime closure starts at the program; deployed libraries are not roots
     //   issue:       mcpp#1011
     //   remove when: the engine takes deployed role=library files into the closure
     //   note:        idempotent: linking a library the engine also stages changes nothing
     ```

     `docs/rules-qt.md`、`docs/rules-qml.md` 各有"引擎缺口"一节，写 mcpp#1011 与移除条件（规则 8）；README 不提。
   - **自动退场**：补丁是幂等的——引擎一旦把已部署库的依赖纳入闭包，我们多链的同一批库对结果没有任何影响（同一个文件、同一个目的地）。引擎没有向 build 程序暴露版本接口（`hostprogram.cppm` 只有 `package_version` 等，已查），所以不做版本门；退场靠下一条。
   - **金丝雀**：`qml-deploy` 的脚本多做一次构建：用环境变量让 fixture 的 `build.mcpp` 设 `plugin_closure = false`，打包，再用读取器检查闭包。若这次包里**已经**没有缺库，说明引擎修好了，脚本打印 `NOTICE: the engine now closes deployed libraries (mcpp#1011); remove the plugin_closure workaround` 并以成功退出（不失败，是提示）；仍缺库则打印 `ok: the engine gap still exists (mcpp#1011)`。引擎修好后，CI 日志里会自动出现这条提示，不需要有人记得。
   - **链接旗标要防"未引用的库被丢弃"**（评审指出的风险，已用 lld 实测）：Linux 上 lld 默认保留按全路径给出的库（NEEDED 保留），但加 `--as-needed`（Ubuntu 的 gcc 默认就是）后被丢弃（NEEDED 为 0）；用 `-Wl,--push-state,--no-as-needed <库> -Wl,--pop-state` 包住后保留（实测，且包外的库仍按 `--as-needed` 处理）。macOS：`ld64.lld` 默认保留，`-dead_strip_dylibs` 丢弃，`-needed_library <路径>` 保留（实测，直接用 `ld64.lld` 与手工构建的 dylib）。因此补库在 ELF 上用 push/pop 包住，在 Mach-O 上用 `-needed_library`。
   - 对现有 `rules-qt` 使用者的影响：链接行多出几个 SDK 库（Linux 上 `XcbQpa`、`WaylandClient`、`EglFSDeviceIntegration` 及其传递依赖）。实测多链 22 个 Qt 库对启动无可测影响（25 次平均 31 ms 对 31 ms）。Windows 上只是多放几个 DLL 到程序旁。回归防线：现有的 `qt-consumer`、`qt-widgets-consumer`（三平台 CI 都跑）。macOS、Windows 的补丁路径本身没有实测（§9），但失败只降级为警告。
3. 回归断言放在 `qml-consumer`：Linux 步骤断言打包目录含 `libQt6XcbQpa.so.6`；`plugin-logic` 固定 `plugin_closure=false` 与旧链接行逐字节一致、`true` 时旧链接行是新链接行的子序列。

### Q10：工具链与 LLVM（与 `rules-qt` 对齐；Linux 不做 LLVM）

**`rules-qt` 现在怎么做（已读它的 fixture、文档与 CI）**：三个 `qt-*` fixture 都没有 `[toolchain]`；唯一的平台差异是 Linux 的两行 `[target.x86_64-linux-gnu]`、`[target.aarch64-linux-gnu]` 里的 `cxx_runtime = "toolchain-coupled"`（Qt 官方 Linux 构建用共享 libstdc++，所以在 Linux 上用 gcc；换成 libc++ 工具链时规则会警告）。Windows 与 macOS 不写任何覆盖，用引擎对该主机的默认工具链（你说明是 mcpp 的 LLVM clang）；规则对编译器的唯一分支是 `mcpp::compiler() == "msvc"` 时加 `/Zc:__cplusplus` 与 `/permissive-`，Linux 加 `-fPIC`，macOS 用 `-F`。**`rules-qml` 的 fixture 逐字沿用这几行，不新增任何工具链选择**，`rules-qml` 本身也不读 `compiler()`（它生成的 C++ 与编译器无关，下面的验证证明了这一点）。按你的意见，**Linux 不支持 LLVM + Qt 就不管**：不为它在 CI 里加步骤。

**不能整链用 clang 构建（Linux）**：`xim:qt` 是 libstdc++ 的官方 Linux 构建。`mcpp build --toolchain llvm@23.1.3` 对现有 `qt-consumer` 就报 `error: a type is caught or cast across C++ runtimes [cxx-runtime-collision]`，`rules-qt` 也有对应警告（既有行为，不是本设计引入）。所以 Linux 上无法通过 mcpp 验证"clang 编译整个 QML 程序"。

**因此直接验证生成的 C++ 在 clang 下的可编译性与行为**（`spikes/llvm-check.sh`）：

| 验证 | LLVM 版本 | 结果 |
|---|---|---|
| 8 个生成文件（moc 输出、类型注册、qrc、6 个 AOT 单元含 `.js`、加载器）`-fsyntax-only`，libc++ | 20.1.7、22.1.8、23.1.3、23.1.6 | 全部通过 |
| 同上，`-O1 -c` 真实代码生成，libc++ | 20.1.7、23.1.6 | 全部通过 |
| 9 个源文件（再加 `main.cpp`）用 clang + **libstdc++ 16.1 头文件**编译，lld 链接，**运行** | 20.1.7、22.1.8、23.1.3、23.1.6 | 0 个编译失败，输出 `cpp=84 singleton=red js=42 nested=7 resource=hello-resource`，与 gcc 构建逐字相同 |

**纠正我自己的错误**：我最初报告的"libstdc++ 一组"其实用的是 libc++。原因是 mcpp 的 `clang++.cfg` 无条件追加 `-nostdinc++ -stdlib=libc++ -isystem …/c++/v1`，所以 `clang++ -stdlib=libstdc++ …` 静默地仍用 libc++（`#error` 探针与链接时的 `std::__1::cout` 暴露了它）。正确做法是用 C 驱动 `clang -x c++ --no-default-config`，显式给出 gcc 的 libstdc++ 头文件与 glibc 头文件，并先用 `_LIBCPP_VERSION`/`__GLIBCXX__` 探针证明运行库（脚本里有）。上表第三行是在证明过运行库之后的结果；第一、二行确是 libc++。这个陷阱写进 fixture 的注释，以免 CI 脚本重蹈覆辙。

**结论**：QML 生成的 C++（含 `qmlcachegen` 的 AOT 代码）对 clang 20 到 23、两种 C++ 运行库在 Linux 上都是可移植的。

**未验证**：macOS 的 clang + `ld64.lld` + Qt 框架（libc++ 是该平台原生运行库，Qt 官方 macOS 构建也是 libc++，**预期**比 Linux 更顺，但无实测）；Windows 的 clang（MSVC ABI 或 MinGW）与 Qt 官方的 MSVC 构建 DLL。两者由三平台 CI 的 fixture 用各自的默认工具链覆盖。

**对设计的影响**：
- CI 的依赖对照基准**各平台用其默认工具链自带的工具**（与"工具链对齐 `rules-qt`"一致，不要求 Linux 装 LLVM）：Linux 用 `readelf -d`（gcc 工具链带 binutils，本机实测 180/180）；macOS 与 Windows 用 `llvm-readobj --needed-libs`（该平台的默认工具链是 LLVM，自带；ELF、PE、Mach-O 含 fat 都实测可用）。`llvm-readobj` 的归一化：去掉 ELF 输出里的 `LoadName` 噪声行、Mach-O 的自身 `LC_ID_DYLIB`、fat 的重复项。这也避开了 Windows 上 `dumpbin` 需要 VS 环境的问题。
- AOT 生效的断言用符号：Linux `nm -C <可执行文件> | grep -c QmlCacheGeneratedCode`，macOS `llvm-nm -C`（实测 llvm-nm：AOT 构建 27 个符号，关闭 AOT 为 0）；Windows 的发布版 PE 没有符号表，改为断言生成文件与其目标文件存在。
- **不**新增 Linux 的 clang CI 步骤（上面的对齐决定）。`llvm-check.sh` 与上面的结果只作为设计证据保留在 spikes 里；Windows、macOS 上生成代码与它们的默认 clang 的兼容性由三平台 fixture 本身覆盖。

## 3. 其他实测发现（进入了设计）

| 发现 | 影响 |
|---|---|
| `qmlcachegen` 靠**输出文件名以 `qmlcache_loader.cpp` 结尾**进入加载器模式；其他名字会被当作编译单个 `.qml`，报 `Too many input files` 或 `No resource path for file` | 加载器输出必须命名为 `<sanitize>_qmlcache_loader.cpp`，写入 §6 的约束，并由 `plugin-logic` 固定 |
| `moc` 不带 `-I` 时丢弃 `QML.Element`（警告 `Potential QML registration macro was found, but no header containing it was included`）；带 `-I<sdk>/include -I<sdk>/include/QtCore -I<sdk>/include/QtQml` 即可，不需要单独加 `QtQmlIntegration` | `rules-qt` 加 `moc_includes` |
| `moc --output-json` 写 `<out>.json`（`moc_backend.cpp.json`） | moc 动作多声明一个输出 |
| `qmltyperegistrar` 不带 `--foreign-types` 会警告基类找不到 | 传 `<sdk>/metatypes/qt6<module>_metatypes.json` |
| 生成的 `.cpp` 作为 `role = "source"` 动作的输出，自动被编译并链接 | 链路无需引擎改动 |
| 同一个 `.qml`/`.js` 的 AOT 单元、`qrc`、`qmldir` 缺一不可：`qmlcachegen` 需要 `-i <qmldir>`、`--resource <qrc>`、`--resource-path`、`-I <sdk>/qml`、`-I <生成根>` | §6 |
| 嵌套文件在 `qmldir` 里写相对路径（`Inner 1.0 sub/Inner.qml`），`.js` 用 `import "Util.js" as U` | G1 |
| 多链 22 个 Qt 库：启动 31 ms 对 31 ms（25 次平均） | 补库用链接而非自写加载代码 |
| 按版本化文件名（`libQt6X.so.6`、`libicudata.so.73`）`link_flag` 可行，不依赖 `.so` 符号链接 | §7 `link_plugin_libs` |
| 改一个 `.qml`，只重新生成该文件的 `*_qml.cpp` 与 `qrc_*.cpp` | 增量断言用修改时间（§10.3） |
| 引擎的闭包只从程序出发，已部署的插件库不是根（`pack.cppm`） | Q9 |
| `qmlcachegen` 在语法错误时让构建失败并点名位置 | Q6 的收益 |
| `deploy` 逐文件；Controls 闭包 1231 个文件，规划与 5 秒内构建完成 | 递归由规则枚举，规模可接受 |
| 多模块：`app.ui` 的 `qmldir` 不写 `depends app.core` 也能解析并运行（`sum=19`）；跨模块的 C++ 继承（`Derived : Base`，分属两个模块）在 `.qmltypes` 里是 `prototype: "Base"`，注册器无"找不到基类"警告 | 不自动生成 `depends`；`depends`、`imports` 只作显式字段；`--foreign-types` 含其他模块的 JSON |
| C++ `QML_SINGLETON` 类可用（`Theme.radius` 得 9），不需要 `qmldir` 条目 | 单例只有 `pragma Singleton` 的 `.qml` 才写 `singleton` 行 |
| `lupdate -extensions qml,js` 能从 `.qml`、`.js` 提取 `qsTr` 的 3 个字符串 | G9 可行，加 fixture（§10.2） |
| 生成的 `.cpp` 是否在可执行文件里可观察：AOT 构建有 27 个 `QmlCacheGeneratedCode` 符号，关闭 AOT 为 0；`QT_LOGGING_RULES` 的调试日志观察不到 | AOT 断言用符号（§10.3） |
| `mcpp` 的 `clang++.cfg` 无条件追加 libc++ 头文件 | Q10 的陷阱 |
| 项目路径含空格或非 ASCII：gcc 模块依赖扫描失败，现有 `qt-consumer` 同样 | §1 的继承限制 |

## 4. 架构

### 4.1 在家族中的位置

`.qml` 是一种 C++ 编译器读不懂、要经 Qt 生成器处理的输入，属 `rules` 家族，与 `rules-qt` 并列。成员文件 `rules/qml.cppm`：

```toml
[features.plugins-binary]
sources = ["src/binary.cppm"]            # 纯 std，不依赖 mcpp；`plugins-core` 之外的独立单元

[features.rules-qml]
sources = ["rules/qml.cppm"]
implies = ["rules-qt", "plugins-core", "plugins-binary"]
# 不写 rule_module 与 device_extensions：见下
```

`rules-qt` 也改为 `implies = ["plugins-core", "plugins-binary"]`（Q9 的补库用到读取器）。

不认领 `.qml`、不写 `rule_module`：现有成员里凡是不认领扩展名的（`tools-*`、`dist-*`）都不写 `rule_module`，由消费者在依赖上写 `host-module = true`。`rule_module` 的作用是"认领扩展名并让引擎自动路由，同时隐含 `host-module`"，而 `.qml` 不适合认领：一个 QML 模块需要 uri、版本、单例，`[build] sources` 表达不了，必须在 `build.mcpp` 里声明。实际行为：`rules-qml` 隐含 `rules-qt`，后者仍带自己的 `rule_module = "mcpp.rules.qt"` 并认领 `.ui/.qrc/.ts`，所以只启用 `rules-qml` 的项目里这三类文件照常被路由到 `rules-qt`；消费者显式写 `host-module = true` 是约定，写了也不会与隐含的重复冲突（`tests/qml-import-only` 固定这一点）。

```toml
[build-dependencies.mcpp]
plugins = { version = "0.27.0", features = ["rules-qml"], host-module = true }
```

### 4.2 与 `rules-qt` 的关系：一次调用

`qml::options` 内嵌 `qt::options`。`qml::compile` 先调 `qt::compile_ex`（§7），拿到 SDK、moc 单元与输出目录，再声明 QML 部分。用户只写一个调用，不会出现两次 moc。

```cpp
import std;
import mcpp;
import mcpp.rules.qml;

int main() {
    mcpp::rules::qml::options o;
    o.qt.modules = { "Core", "Gui", "Qml", "Quick", "QuickControls2" };
    o.modules = {{ .uri = "Probe", .sources = { "qml/Probe/*.qml" } }};
    return mcpp::rules::qml::compile(o) ? 0 : 1;
}
```

### 4.3 代码组织

| 文件 | 内容 |
|---|---|
| `rules/qml.cppm` | 成员本体，约 700–900 行（`qt.cppm` 为 773 行） |
| `src/binary.cppm` | `mcpp.plugins.binary`：`needed(path) -> result`（`names`、`error`），约 130 行，原型见 spikes |
| `rules/qt.cppm` | §7 的加法式改动 |

## 5. 公开 API

```cpp
export module mcpp.rules.qml;
import mcpp.rules.qt;

export namespace mcpp::rules::qml {

struct qml_module {
    std::string uri;                          // 必填；"Probe"、"com.acme.ui"
    std::string version = "1.0";              // "主.次"
    std::vector<std::string> sources;         // .qml/.js/.mjs，文件或 glob，相对包根
    std::string root;                         // 映射到模块根的目录；空 = sources 的公共目录
    std::vector<std::string> resources;       // 其他文件（图片等），相对 root 嵌入
    std::vector<std::string> headers;         // 注册类型的头文件；空 = moc 扫描到、含 QML_* 宏的头文件
    std::vector<std::string> depends;         // qmldir 的 `depends` 行（工具与加载依赖），显式给出，默认无
    std::vector<std::string> imports;         // qmldir 的 `import` 行（使用本模块的人自动 import），显式给出，默认无
    std::vector<std::string> shaders;         // .vert/.frag/.comp，经 qsb 嵌入为 shaders/<name>.qsb
    std::vector<std::string> qsb_args = { "--qt6" };
    bool aot = true;                          // qmlcachegen；false 只嵌入源文件
    std::string prefix = "/qt/qml";           // 资源前缀
};

struct options {
    std::vector<qml_module> modules;
    qt::options qt;                           // modules、deploy_plugins、root、i18n、plugin_closure……同 rules-qt（plugin_closure 默认 true，两处一致）
    std::vector<std::string> quick_plugins = { /* Qt Quick 所需插件目录，见 §8 */ };   // 与 qt.deploy_plugins 取并集；用户设值则替换这个默认
    std::vector<std::string> imports;         // 额外部署的运行时 QML 模块（动态加载的，如 "QtQuick.Controls.Material"）
    std::vector<std::string> styles;          // 非空时，optional import 只跟随列出的样式（"Material"）；空 = 全部
    std::vector<std::string> import_paths;    // 额外的 QML 模块目录（含 <uri 路径>/qmldir）；-I 与部署来源
    bool deploy_imports = true;               // 部署运行时模块及其库
    bool write_qt_conf  = true;               // 写 qt.conf 并部署到程序旁
    bool lint = false;
    bool lint_warnings_are_errors = false;
    std::string out_dir = std::string(mcpp::out_dir());
};

bool compile(options opt);
std::string import_root(const options&);      // <out>/qml
}
```

### 5.1 `qml_module` 的取值规则

- `sources` 的每个 glob 在计划期展开并排序，调 `rerun_if_changed_glob`；**计划期读过内容的每个文件**（`.qml` 的 import 与 `pragma Singleton`、qrc 引用的文件、qmldir）逐个调 `rerun_if_changed`，不能只靠 glob（`rules/qt.cppm` 对读过内容的头文件也逐个声明）。SDK 里为闭包读的 `qmldir` 与 `.qml` 不逐个声明（成千上万个且随 SDK 版本整体变化），改为 `mcpp::fact("rules-qml.sdk", <根>)` 记录 SDK 根，换 SDK 即重新计划。
- 资源路径 = `prefix + "/" + uri 的 '.' 换 '/' + "/" + 相对 root 的路径`（点分 uri 与嵌套文件已实测）。
- `qmldir` 类型表：首字母大写的 `.qml` 写成 `Name 版本 相对路径`，含 `pragma Singleton` 的写成 `singleton Name 版本 相对路径`；小写开头的不进表。
- `uri` 不能与 SDK 里已有的模块 uri 相同（报错并点名）；`uri` 与 `.qml` 文件名要匹配 §8 的文法，文件名含非 ASCII 字符时警告（`qmlcachegen` 的 `@rsp` 在 Windows 上对此未验证）。
- **qrc 与 qmldir 里的路径要做 XML 转义与正斜杠规范化**（`&`、`<`、`>`、`"`）；路径一律用 `generic_string()`，Windows 的反斜杠不进入生成文件。
- 同一个头文件被分给两个模块 → 报错并点名。无 C++ 类型的模块（只有 `.qml`）不跑注册器，已实测可用（多模块原型里的 `app.ui`）。
- `headers` 默认：moc 扫描结果里含 `QML_ELEMENT`、`QML_NAMED_ELEMENT`、`QML_SINGLETON`、`QML_ANONYMOUS`、`QML_FOREIGN`、`QML_UNCREATABLE`、`QML_EXTENDED` 之一的头文件。多模块时用 `headers` 显式分配；未分配的 QML 头文件**报错**，不默默归第一个模块。

### 5.2 生成物命名

按 `sanitize(uri)`（`.`→`_`）命名，全部经 `rules-qt` 的 `claim()` 查重名：

```
<out>/qml/<uri 路径>/qmldir
<out>/qml/<uri 路径>/<sanitize>.qmltypes
<out>/qml/<sanitize>/<sanitize>.qrc
<out>/qml/<sanitize>/qrc_<sanitize>.cpp
<out>/qml/<sanitize>/<sanitize>_qmltyperegistrations.cpp
<out>/qml/<sanitize>/<sanitize>_<相对路径展平>.cpp         每个 .qml/.js/.mjs 一个（AOT）
<out>/qml/<sanitize>/<sanitize>_qmlcache_loader.cpp        名字必须以 qmlcache_loader.cpp 结尾（§3）
```

`<out>/qml` 是所有模块共享的导入根，`qmlcachegen` 的 `-I` 指向它。

### 5.3 缺少 Quick 时

`qt.modules` 含 `Quick`/`QuickControls2` 而 SDK 没有（`xim:qt-base`）：沿用"缺模块是警告"，并追加"QML 需要 `xim:qt`；`xim:qt-base` 只含 QtQml 库，没有 Quick"。缺 `qmlcachegen`/`qmltyperegistrar`/`rcc` 同样是警告并跳过对应步骤，计划不失败（`emit build-database` 要在没有 SDK 的机器上能计划）。

### 5.4 目标平台与目标数

`mcpp::target_os()` 不是 linux、windows、macos 时（Android、iOS、WebAssembly）：一条警告，不声明任何动作，返回 true。一个包有多个目标时的行为见 §1 非目标。

## 6. 每个模块的动作（步骤 1–6 的命令行已在真实引擎里跑通，含两个互相依赖的模块；步骤 7、8 未能运行）

| 序 | 动作 id | 命令（`<Q>` = SDK 根） | 输入 | 输出 |
|---|---|---|---|---|
| 1 | `qt:moc:<stem>`（`rules-qt`，`moc_json`、`moc_includes` 为真） | `moc <h> -o moc_<stem>.cpp --output-json --output-dep-file … -I<Q>/include -I<Q>/include/Qt<M>…` | 头文件 | `moc_<stem>.cpp`、`moc_<stem>.cpp.json` |
| 2 | `qml:registrar:<uri>` | `qmltyperegistrar --generate-qmltypes=<…>.qmltypes --import-name=<uri> --major-version=M --minor-version=m --foreign-types=<Q>/metatypes/qt6core_metatypes.json,…,<其他模块 json> -o <…>_qmltyperegistrations.cpp <本模块各 json>` | 本模块 moc json | 注册 cpp、`.qmltypes` |
| 3 | （计划期写文件，`fs::write_if_changed`） | `qmldir`、`.qrc`（`<file alias="相对路径">绝对路径</file>`，前缀 `/qt/qml/<uri 路径>`；XML 转义，正斜杠） | — | — |
| 4 | `qml:rcc:<uri>` | `rcc --name <sanitize> <qrc> -o qrc_<sanitize>.cpp` | qrc、qmldir、全部源文件 | `qrc_<sanitize>.cpp` |
| 5 | `qml:cachegen:<uri>:<file>` ×N | `qmlcachegen --bare --resource-path <资源路径> -I <Q>/qml -I <out>/qml -i <qmldir> --resource <qrc> -o <…>.cpp <file>` | 源文件、**所有模块**的 qmldir 与 `.qmltypes`、qrc | 每文件一个 cpp |
| 6 | `qml:cachegen:<uri>:loader` | `qmlcachegen --resource-name qmlcache_<sanitize> -o <…>_qmlcache_loader.cpp @<rsp>`；rsp：`--resource`、`<qrc>`，随后每个资源路径一行 | rsp、qrc | loader cpp |
| 7 | `qml:qsb:<uri>:<shader>` ×M | `qsb <qsb_args> -o <…>.qsb <shader>`，`action::env` 给 SDK 库目录 | 着色器 | `.qsb`，加入模块 qrc |
| 8 | `qml:lint:<uri>:<file>` ×N（`lint = true`） | `qmllint -I <out>/qml -I <Q>/qml <file>`；**每个文件一个动作**，避免 Windows 约 32K 的命令行上限 | 源文件、全部 qmldir 与 `.qmltypes` | 检查结果（`lint_warnings_are_errors` 时加 `--max-warnings 0`） |

要点：

- **全部是 `role = "source"`**；输出是 `.cpp` 的自动进入编译（已验证）。
- 第 5 步把**所有模块**的 `qmldir`、`.qmltypes` 声明为输入（保守），多模块互相 `import` 才解析得了；任一模块变化，依赖它的缓存单元重新生成。
- 无 C++ 类型的模块跳过 1、2；`aot = false` 跳过 5、6。
- Windows：`qmlcachegen`、`qmltyperegistrar` 在 `<Q>/bin/*.exe`；`rules-qt` 的 `detail::tool` 同时搜 `bin` 与 `libexec`。每个 `qmlcachegen` 与 `qmllint` 动作各处理一个文件，只有加载器读一个 `@rsp`（行数等于文件数，不受命令行上限影响）。
- 第 4 步输入含 qrc 引用的全部文件（同 `rules-qt::qrc_files`）：改一个 `.qml` 只重跑它的 `qmlcachegen` 与 `rcc`。

## 7. `rules-qt` 的加法式改动（0.27.0）

| # | 改动 | 默认 | 原因 |
|---|---|---|---|
| a | `options::moc_json` | `false`，现有 moc 动作 argv 不变 | §6 第 2 步需要 JSON；为真时 moc 追加 `--output-json` 并声明第二个输出 |
| b | `options::moc_includes` | `false` | 实测：不带 `-I` moc 丢 `QML.Element`。为真时传 `<sdk>/include` 与各链接模块的 `include/Qt<M>` |
| c | `compile_ex(options) -> result` | `compile` 内部调它并返回 `result.ok` | `result` 含 SDK、`gen` 目录、moc 单元（头文件、cpp、json）、已链接模块，供 `qml::compile` 复用 |
| d | `i18n.extensions`、`i18n.qml_sources` | 现状不变 | `lupdate` 的 `-extensions` 加 `qml,js`，扫描 `.qml`/`.js`（G9） |
| e | 把 `tool`、`project_files`、`claim` 等辅助函数留在 `detail`，`qml.cppm` 通过 `qt::detail` 使用 | 无新公共面 | 同一个包同一个版本内的协作，不是契约（规则 5）；`compile_ex` 同样只在注释里标明"供 `mcpp.rules.qml` 使用，不是契约" |
| f | `options::plugin_closure`、导出 `link_plugin_libs(files)` | **`true`**（Q9，无感、幂等、带标注与金丝雀） | 引擎缺口补丁：补全已部署插件的 SDK 库依赖。源码用 `ENGINE-GAP WORKAROUND` 标记，文档记 issue 号与移除条件（规则 8） |

a–e 是新字段与新函数，默认行为不变；f 默认开，只**增加**链接输入，不改变任何现有输入。`plugin-logic` 固定：`moc_json`、`moc_includes`、`plugin_closure` 为假时，moc、uic、rcc、lrelease 的动作 argv 与链接行和改动前字节一致；`plugin_closure` 为真时，旧链接行是新链接行的子序列。

`Qml` 模块在 SDK 里的头文件模块 `QmlIntegration` 无库文件，对它只加 include 目录，不进"缺模块"警告。

### `link_plugin_libs(files)`

对一组已部署的插件库：

1. 对每个文件调 `binary::needed`；
2. 只保留名字满足"不含 `/`、`\`、`..`，且 SDK 库目录里确有该文件"的项，对保留项**递归**求闭包（SDK 库的依赖也要）；
3. 平台处理：
   - Linux：`link_flag` 依次给 `-Wl,--push-state,--no-as-needed`、`<sdk>/lib/<名字>`、`-Wl,--pop-state`（用 `needed` 给出的完整文件名，如 `libQt6QuickLayouts.so.6`、`libicudata.so.73`，不依赖 `.so` 符号链接；已实测：27 个 Qt 库按版本化文件名链接，可执行文件的 `NEEDED` 含它们，`mcpp pack` 带上，`roots=1`；push/pop 包法在 `--as-needed` 默认的链接下实测保留）；
   - macOS：Mach-O 名字形如 `@rpath/QtFoo.framework/Versions/A/QtFoo`，取框架名映射到 `<sdk>/lib/QtFoo.framework/QtFoo`，`link_flag` 给 `-Wl,-needed_library,<路径>`（`ld64.lld` 直接实测保留；未经 clang 驱动与 mcpp 实测）；
   - Windows：`mcpp::deploy(<sdk>/bin/<名字>, ".")`（引擎本来就会放程序直接导入的 DLL，插件独有的要规则放）；
4. 已链接的模块不重复；读取失败是警告并说明包可能不完整。来自 `import_paths`（非 SDK）的插件的非 SDK 依赖不处理，**警告一次**"该插件依赖 X，不在 SDK 里，需要项目自己保证"，不再默默忽略。
5. 整个函数与调用点在 `ENGINE-GAP WORKAROUND` 标记之下（Q9）。

## 8. 运行时 QML 模块的部署

闭包算法见 Q1。部署文件：闭包内每个模块目录 `<Q>/qml/<relativePath>/` 下的文件（递归，不进入自带 `qmldir` 的子目录、`designer/`），部署到 `qml/<relativePath>/`；排除 `*.qmltypes`、`*.prl`、`*.debug`、`*.pdb`；Windows 上 `xxd.dll` 与 `xx.dll` 并存时取 release（规则同 `rules-qt` 的 `deploy_plugins`）。对所有部署的插件库调 `link_plugin_libs`。

写 `qt.conf` 到 `<out>/qml/qt.conf`：

```
[Paths]
Prefix = .
Qml2Imports = qml
Translations = <qt.i18n.deploy_to>        # 仅在不是默认的 translations 时写出
```

并 `deploy` 到 `.`。检测到项目自己的 `qt.conf` 时不写（Q3）。副作用见 Q3，写进 `docs/rules-qml.md`。

**Qt Quick 所需的插件目录**用一个独立字段 `options::quick_plugins`，与 `qt.deploy_plugins` 取并集，而不是"检测 `deploy_plugins` 是否为默认值再替换"（`std::vector` 分不清"没设置"和"设成了默认值"，评审指出）。`quick_plugins` 的默认值：Linux `xcbglintegrations`、`platforminputcontexts`、`imageformats`；Windows/macOS `imageformats`、`platforminputcontexts`（目录不存在则只是 `rules-qt` 已有的警告）。用户把它设为空即不加。**这一项是推断**（真实 GL 窗口无法在无显示环境测试），由打包清单断言与 offscreen 运行约束，可按 CI 调整。`plugin_closure` 在两个规则里默认都是 `true`，不存在"未设置"问题。

动态加载而 `import` 里看不到的模块，用 `options::imports` 补；这是文本闭包与 `qmlimportscanner` 共有的限制。

**输入校验与未解析的 import**：

- 模块 `uri` 必须匹配 `[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*`，否则报错；`.qml` 里的 `import` 语句同一文法，所以由 `uri` 推出的目录与 `deploy` 的目标路径不会含 `..` 或绝对路径（`deploy` 要求 `to` 不含 `..`）。
- `import` 的 uri 既不在 SDK、`import_paths`，也不是自带模块时（例如它由 `main()` 里的 `qmlRegisterType` 在运行时注册），**不是错误也不是每次一条警告**：汇总成一条普通信息（"N 个 import 未在 SDK 或 import_paths 找到，未部署：…"），因为运行时注册的类型是合法用法；只有以 `Qt` 开头的 uri 找不到时升级为 `mcpp::warning`（多半是 SDK 缺模块，如 `xim:qt-base` 没有 Quick）。

## 9. 三平台

| 项 | Linux | Windows | macOS |
|---|---|---|---|
| 工具位置 | `libexec/`（moc、qmlcachegen、qmltyperegistrar）、`bin/`（qsb、qmllint） | 均在 `bin/*.exe` | `libexec/`，同 Linux |
| 链接 | `-fPIC`，`libQt6*.so` 全路径 | `Qt6*.lib` | `-F<Q>/lib` + 框架二进制全路径 |
| QML 插件库 | `libqtquick2plugin.so` | `qtquick2plugin.dll`（排除 `d` 版） | `libqtquick2plugin.dylib` |
| 补库 | `link_flag`（**已验证**） | `deploy(dll, ".")`（推断） | `link_flag` 框架（推断） |
| 工具链（与 `rules-qt` 一致） | gcc；fixture 加 `cxx_runtime = "toolchain-coupled"`（Linux 的两行） | 引擎的默认工具链，不覆盖（未验证，由 CI 覆盖） | 引擎的默认工具链（LLVM clang，libc++ 原生），不覆盖（未验证，由 CI 覆盖） |
| 依赖读取器 | ELF，180 个真实文件对照 `readelf`；CI 同 | PE，mingw 样本对照；CI 上对 SDK 的 DLL 用 `llvm-readobj --needed-libs` 对照 | Mach-O，lld 样本对照；CI 上对 SDK 的 dylib 用 `llvm-readobj --needed-libs` 对照 |
| 防止未引用的库被链接器丢弃 | `--push-state,--no-as-needed`（**已验证**） | 不适用（DLL 直接 `deploy`） | `-needed_library`（`ld64.lld` 直接验证，未经 mcpp） |
| `qt.conf` | **已验证** | 推断：Windows 把 DLL 放程序旁，`Prefix=.` 即程序目录 | 推断；`Prefix` 在 macOS 的处理由 CI 确认 |
| 运行测试 | `QT_QPA_PLATFORM=offscreen`、`QT_QUICK_BACKEND=software` | 同 | 同 |
| 风险 | 低 | 中 | 中高（Mach-O 补库、`qt.conf`、clang 与框架无实测） |

PE 与 Mach-O **读取器**的正确性已用真实构建的样本验证（样本由 mingw 与 LLVM 在 Linux 上生成，不是 Windows/macOS 上的 Qt 产物）；**未验证**的是它们与 `mcpp pack`、`link_flag`、`deploy` 的集成，以及 Windows/macOS 上 Qt 官方二进制的真实依赖。每个推断项在 §10.3 有断言，失败时设计不变，改平台分支细节。

## 10. 测试方案

### 10.1 `tests/plugin-logic`（三平台都跑，不装不跑任何东西）

| 用例 | 断言 |
|---|---|
| 资源路径 | 点分 uri、`root`、前缀 → 期望资源路径；嵌套文件 |
| `qmldir` 文本 | 类型、单例、显式 `depends`/`imports`、小写文件不入表；不自动生成 `depends`；字节级确定（连跑两次不重写） |
| qrc 文本 | XML 转义（路径含 `&`、`<`）、正斜杠、`alias` 相对路径 |
| `qt.conf` | 内容；`deploy_to` 非默认时写 `Translations`；包根有自己的 `qt.conf` 时不写并警告；`write_qt_conf=false` 不写 |
| uri 与路径校验 | 非法 uri（`a..b`、`../x`、数字开头）报错；与 SDK 模块同名的 uri 报错；同一头文件分给两个模块报错 |
| 目标平台 | `target_os` 为 android/ios/emscripten 时一条警告、无动作 |
| `rerun_if_changed` | 计划期读过的每个 `.qml`、qrc 引用的文件都声明了 |
| `quick_plugins` | 与 `qt.deploy_plugins` 取并集；设为空则不加；`qt.deploy_plugins` 的用户值不被覆盖 |
| 动作清单 | 给定模块 → 期望动作 id 集合；`aot=false` 少 5、6；无 C++ 类型少 1、2；`shaders` 多 7；`lint` 多 8 |
| 命令行 | 第 2、5、6 步 argv 与 §6 一致（含 `--foreign-types` 文件集合与顺序、加载器输出文件名以 `qmlcache_loader.cpp` 结尾） |
| 重名与归属 | 两模块同 `sanitize(uri)`；两个同名 `.qml` 映射到同一资源路径；多模块未分配的 QML 头文件 → 报错并点名 |
| 缺工具/缺 Quick | 警告含"xim:qt"，计划不失败，不声明对应动作 |
| 文本闭包 | 构造假 SDK 的 `qml/` 树：`default import`、`optional import … auto`、`depends`、目录里 `.qml` 的 import（含嵌套目录且不进入自带 qmldir 的子目录）、循环 import、注释与字符串里的 `import` 不算、`styles` 限制、自带模块 uri 不当运行时模块 |
| 部署清单 | 排除 `.qmltypes`、`.prl`、`designer/`；Windows 调试版去重 |
| `binary::needed`：合成样本 | 测试里用函数**在内存/临时文件中构造**最小的 ELF（32/64）、PE（32/64，含延迟导入）、Mach-O（thin、fat、`LC_ID_DYLIB` 不返回）；不提交二进制样本 |
| `binary::needed`：截断扫描 | 对每个合成样本的**每个前缀长度**与一组**固定种子**的单字节改动调用，断言返回结果或错误、不崩溃（普通构建）。sanitizer 版不进 CI（评审指出模块单元与 sanitizer 驱动的组合不合 CI 的约定），1500 个输入的 ASan+UBSan 结果记录在 Q2，由原型复现 |
| `link_plugin_libs` | 假 SDK 库目录与假插件：名字过滤（`../x`、含 `/`）、递归闭包、已链接模块不重复、各平台的产出（ELF：push/pop 包住的 `link_flag`；Mach-O：`-needed_library`；PE：`deploy`）、非 SDK 依赖只警告一次 |
| `rules-qt` 不变性 | `moc_json`、`moc_includes` 为假时，现有 moc、uic、rcc、lrelease 动作 argv 与改动前一致；`plugin_closure=false` 与旧行为一致 |

### 10.2 Fixture（每个一个 `tests/<name>/`，结构同现有 `tests/qt-*`）

| fixture | 平台 | 内容 | 目的 |
|---|---|---|---|
| `qml-consumer` | 三平台 | 模块 `Probe`：`Main.qml`、`pragma Singleton` 的 `.qml`、C++ `QML_SINGLETON`、`QML_ELEMENT` 的 C++ 类（带 `Q_INVOKABLE`）、`.js`、嵌套 `.qml`、一个资源文件；`QtQuick` + `QtQuick.Controls`；一个带 `qsTr` 的 `.qml` 与 `.ts`（`i18n.update_sources = true`，断言 `.ts` 含该字符串，并有 `.qm` 部署）；程序 offscreen 加载 `loadFromModule`，断言各项属性值 | G1–G4、G6、G9，同 `qml-feat` 原型 |
| `qml-modules` | 三平台 | `app.core`（C++ 类型 `Base`、`Card.qml`）与 `app.ui`（点分 uri，`C++ Derived : Base`，QML 里实例化 `Derived` 并读继承来的 `Base` 属性、使用 `Card`）；一个无 C++ 类型的模块 | 多模块、`foreign-types`、缓存依赖（同 `multi-module` 原型，`sum=19`） |
| `qml-deploy` | 三平台 | `import` 了 `QtQuick.Layouts`、`QtQuick.Controls.Material`、`Qt.labs.*` 之类，不链接对应库；`mcpp pack --format dir` 后脱离 SDK 运行 | G5、G11 |
| `qml-import-only` | 三平台 | 只为 import `mcpp.rules.qml`，无 `build.mcpp`、无 QML | 同 `qt-import-only`：不报缺 SDK |
| `qml-shaders` | 仅 Linux，带探测门 | 编一个 `.frag`，断言 `.qsb` 被嵌入并被 `ShaderEffect` 加载 | G7 |

**工具链**：fixture 的 `mcpp.toml` 沿用 `qt-consumer` 的写法：`[target.'cfg(any(windows, linux, macos))'.xlings.workspace] "xim:qt" = "6.11.1"`（QML 要 `xim:qt`，不是 `xim:qt-base`），Linux 的 `[target.x86_64-linux-gnu]` 与 `[target.aarch64-linux-gnu]` 两行 `cxx_runtime = "toolchain-coupled"`，没有 `[toolchain]`。

**变体**：`qml-consumer` 的脚本用环境变量让 `build.mcpp` 构建三个变体，各自断言一个行为：`noaot`（`aot = false`：无 `*_qml.cpp`、无 `QmlCacheGeneratedCode` 符号，程序仍能运行）、`styles`（`styles = {"Basic"}`：打包目录里没有 `QtQuick/Controls/Material`）、`noconf`（`write_qt_conf = false`：包里无 `qt.conf`）。`import_paths` 与 `imports` 由 `qml-deploy` 覆盖（`imports` 部署一个只在 C++ 里动态加载的模块）。

程序在 `main` 里缺省设置 `QT_QPA_PLATFORM=offscreen`、`QT_QUICK_BACKEND=software`，用具体属性值断言并打印固定格式的行；加载失败时非零退出并打印引擎的警告。

### 10.3 CI 断言（`.github/scripts/check-deps-and-qt.sh` 增加函数，三个 job 各接入）

通用：

1. `qml-consumer`：`mcpp build` + `mcpp run`，输出匹配固定行。
2. AOT 生效：每个 `.qml`/`.js` 有对应生成的 `*.cpp` 与 `*_qmlcache_loader.cpp`；Linux、macOS 上 `llvm-nm -C <可执行文件> | grep -c QmlCacheGeneratedCode` 大于 0（原型实测 AOT 为 27，关闭为 0）；Windows 的发布版没有符号表，改为断言生成的 `.cpp` 与对应目标文件存在。（评审指出"字符串存在"证明不了 AOT 被使用，符号才是。）
3. 增量：改一个 `.qml` 重建，用**生成文件的修改时间**断言（不读构建日志，其格式不是契约）：该文件的 `*_qml.cpp` 与 `qrc_*.cpp` 变了，其余 `*_qml.cpp`、`*_qmltyperegistrations.cpp`、加载器没变（原型实测正是如此）。改文件前 `sleep 2.2`，以覆盖 HFS+（1 秒）与 NTFS/FAT（2 秒）的时间戳粒度。
4. `qml-modules`：同 1，另在**运行时**断言继承：程序实例化 `app.ui` 的 `Derived` 并读继承来的 `Base` 属性，值正确（评审指出 `.qmltypes` 文本格式不是契约，所以不断言文本）。
5. `qml-import-only`：构建，输出不含 "no Qt SDK"。
6. `qml-deploy`：
   - **scanner 对照**：运行 `qmlimportscanner -rootPath <模块目录> -importPath <Q>/qml`，取 JSON 中 `type=module` 的 `relativePath` 集合 S；断言打包目录 `bin/qml/<p>/qmldir` 对每个 `p ∈ S` 都存在（允许多，不允许少；`styles` 非空时只对非样式模块断言）。
   - **打包闭包的静态分析**：对包内每个 ELF/PE/Mach-O 读依赖（Linux `readelf -d`，macOS 与 Windows `llvm-readobj --needed-libs`，归一化见 Q10），断言其依赖里凡是 SDK 提供的库，都在 `lib/`（Linux、macOS）或程序旁（Windows）。
   - **脱离 SDK 运行**：把**SDK 根目录改名挪走**再运行打包出来的程序，结束后改回（评审指出 `env -i` 挡不住写进 RUNPATH/rpath 的绝对路径，只有 SDK 真的不在才能证明闭包完整）。Linux 另需把程序的解释器指向生态 loader（`patchelf --set-interpreter`，做法见 spike），因为包的 libc 来自生态；Windows、macOS 直接运行。程序输出固定行即通过；任何一步在某平台无法执行时，降级为上一条的静态断言并打印 `::notice::`。
   - `bin/qt.conf` 存在且内容为 §8。
7. 读取器对照（各平台用默认工具链自带的工具，见上）：对 SDK 里的全部插件库与库，`binary::needed` 与它的依赖集合逐个一致（Linux 用 `readelf` 实测 180/180）。
8. `check-engine-gaps.sh`：每个 `ENGINE-GAP` 块都有 `issue:` 与 `remove when:`（Q9）。
9. Q9：`qml-consumer` 的 Linux 步骤断言打包目录含 `libQt6XcbQpa.so.6`；`qt-widgets-consumer` 的 Linux 步骤同样断言（现有 `rules-qt` 使用者的缺口因默认开而补上）；金丝雀见 Q9（`qml-deploy` 脚本里，设 `plugin_closure=false` 再打包，已无缺库则打印 `NOTICE … remove the plugin_closure workaround`，否则打印 `ok: the engine gap still exists`，两种都成功退出）。

平台特有：

- Linux：包里存在 `libQt6QuickLayouts.so.6`（原型里丢失的那个）。
- Windows：`target/dist` 里存在 `Qt6QuickLayouts.dll` 与 `qml/QtQuick/Layouts/qquicklayoutsplugin.dll`；从构建目录直接运行也能找到（同 `qt-widgets-consumer`）。
- macOS：包里存在 `qml/QtQuick/Layouts/libqquicklayoutsplugin.dylib`，`llvm-readobj --needed-libs` 看到的框架依赖都已满足。

`qml-shaders`（仅 Linux）：先 `qsb --version` 在规则会使用的同一环境下探测；失败则打印 `::notice::SKIP qml-shaders: <原因>` 并成功退出，通过则做真实断言。G7 因此在 CI 中是"argv 已固定、真实运行带门"，评审指出的"永远不会失败"在 `xim:qt-addons` 修复前成立，写进文档的限制。

### 10.4 CI 接入与耗时

- Linux `consumers` job：5 个 fixture（`xim:qt` 约 187 MB，job 已为 `qt-widgets-consumer` 下载；新 fixture 声明同一版本 `6.11.1` 即命中缓存）。`qml-shaders` 需要 `xim:qt-addons`（约 128 MB），只在 Linux 下载，探测失败时下载也已发生，所以该 fixture 放在 job 最后一步。
- `rules-cross-platform` job 的 macOS、Windows：前 4 个 fixture，接在现有 `qt-*` 步骤之后。
- `lint = true` 与 `qmllint`：Linux 上一个子用例，对故意写错的文件断言点名；macOS、Windows 只断言动作声明（`plugin-logic`）。`qmllint` 在 `xim:qt` 的 `bin/`，实测存在；未在本机运行过，是第一次 CI 才会验证的项。
- 耗时**未测量**：原型一次构建约 5–10 秒，每个 fixture 还要打包、运行、读依赖；主要成本是 SDK 下载，缓存键含 `tests/*/mcpp.toml`，新 fixture 声明同一版本即命中。落地后以第一次 CI 的实际耗时为准，必要时调整 job 的 `timeout-minutes`。

## 11. 文件变更清单

| 文件 | 变更 |
|---|---|
| `rules/qml.cppm` | 新增 |
| `src/binary.cppm` | 新增（`mcpp.plugins.binary`） |
| `rules/qt.cppm` | §7 的 a–f；头注释增加"见 `mcpp.rules.qml`" |
| `mcpp.toml` | `version` 0.26.1 → 0.27.0；`[features.plugins-binary]`、`[features.rules-qml]`；`rules-qt` 的 `implies` 增加 `plugins-binary`；`src/plugins.cppm` 的版本常量同步 |
| `docs/rules-qml.md` | 新增：开头一段摘要，随后用法、选项表、动作清单、部署与闭包、`qt.conf` 副作用、限制（§1 非目标）、平台表、"引擎缺口"一节（同上）；遵守 `plugin-development.md` 规则 10 |
| `docs/rules-qt.md` | `moc_json`、`moc_includes`、`compile_ex`、`i18n.extensions`、`plugin_closure`；新增"引擎缺口"一节（平台插件的闭包缺口、issue 号、移除条件、金丝雀）；指向 `rules-qml` |
| `docs/engine-and-rules.md` | `rules-qml` 不认领扩展名、不写 `rule_module` 的原因 |
| `docs/plugin-development.md` | §3 的分层表加入 L2 单元 `mcpp.plugins.binary`；规则 8 的"已有补丁"一节指向 `check-engine-gaps.sh` |
| `.github/scripts/check-engine-gaps.sh` | 新增（Q9 的标记检查），并接入现有 `check-compat-retirement.sh` 所在的步骤 |
| `README.md` | Members 表增加 `rules-qml`（mcpp 下限列 2026.10.1.3，注明整个包需要 2026.10.30.1、最低验证 2026.10.31.3）；`plugins-binary` 进"通用库"一段；示例版本号 |
| `tests/plugin-logic/…` | §10.1 |
| `tests/qml-consumer`、`qml-modules`、`qml-deploy`、`qml-import-only`、`qml-shaders` | §10.2 |
| `.github/scripts/check-deps-and-qt.sh` | 新函数，**以及**文件末尾的 `usage` 行和 `case` 分发（现有脚本两处都要改） |
| `.github/workflows/ci.yml` | §10.3、§10.4 |

后续项（不在本 PR）：`dist-apple` bundle 的 `Contents/Resources/qml`；`qmltc`；依赖包提供 QML 模块的贡献机制；`xim:qt-addons` 修复（xim-pkgindex#1262）发布后撤掉 `qml-shaders` 的探测门。

## 12. 单 PR 内的提交顺序

1. `mcpp.plugins.binary` 与单测（合成样本、截断扫描）。
2. `rules-qt`：`moc_json`、`moc_includes`、`compile_ex`、导出函数，`plugin-logic` 的"现有 argv 不变"用例（先证明零回归）。
3. `rules-qt`：`link_plugin_libs`、`plugin_closure`（默认开，带 `ENGINE-GAP WORKAROUND` 标记）、`check-engine-gaps.sh` 与对应单测（单独一个提交；`docs/rules-qt.md` 的"引擎缺口"一节随它提交，便于你单独审这一处行为变更）。
4. `rules-qml` 的模块链路（§6），及其 `plugin-logic` 用例。
5. 部署：文本闭包、`qt.conf`、补库（§8）及用例。
6. `qsb`、`qmllint`、`lupdate` 扩展。
7. fixture 与 CI（含 `check-engine-gaps.sh` 接入）。
8. 文档、README、版本号。

## 13. 风险与残余未知

| 风险 | 状态 |
|---|---|
| Windows、macOS 上补库、`qt.conf`、PE/Mach-O 与 `mcpp pack` 的集成 | 未验证；每项有 CI 断言；读取器本身已验证 |
| `deploy_plugins` 里 Quick 所需目录（`xcbglintegrations` 等） | 推断，无法在无显示环境验证 |
| Q9 的运行层面失败（缺 `libQt6XcbQpa` 的真实 X11 启动） | 只在静态依赖层面确认 |
| `plugin_closure` 是引擎缺口的补丁，违反"不打补丁"的字面要求 | 按规则 8 标注（issue、文档、`ENGINE-GAP WORKAROUND` 标记、移除条件）；默认开且无感；幂等；金丝雀在引擎修好后自动提示；失败只降级为警告 |
| `rules-qt` 默认开补丁后，macOS/Windows 的补丁路径没有实测，却影响现有使用者 | 现有 `qt-consumer`、`qt-widgets-consumer` 在三平台 CI 都跑，构成回归防线；读取失败只是警告；`plugin_closure = false` 可应急关闭 |
| 文本闭包要读 SDK 里成千上万个 `.qml` 文件的 `import`（计划期） | 原型：Controls 闭包 1231 个文件、全部 40 个顶层模块的闭包都在秒级；落地时若计划变慢，改为只读目录里 `.qml` 的前若干行或缓存 |
| 测试里"脱离 SDK 运行"在 Windows/macOS 的做法 | 未验证（Windows 去掉 `PATH` 里的 SDK；macOS 去掉框架路径），失败时降级为静态依赖断言 |
| 文本闭包与 scanner 在 Qt 其他版本上的一致性 | 只验证了 6.11.1；CI 的 scanner 对照会在升级 Qt 时报警 |
| 动态加载的模块（`Qt.createComponent` 的字符串、运行时拼的 import） | 与 scanner 同样看不到；`options::imports` 补 |
| `qmlcachegen` 的参数随 Qt 版本变化（`--direct-calls`、`--module-id` 等 CMake 里的可选参数我们没传） | 只用了在 6.11.1 实测可用的最小集；升级 Qt 时由 fixture 发现 |
| `xim:qt-addons` 的 `qsb` 不可运行 | 根因已查明并提 PR（xim-pkgindex#1262）；合入并发布前由探测门保护，发布后自动翻转 |
| 读取器读不可信文件 | 1500 次模糊输入无崩溃，名字由调用方过滤，大小上限 1 GiB |
| macOS、Windows 的 clang 本身（Q10） | 无法验证；与 `rules-qt` 一致地不覆盖工具链，由三平台 CI 的 fixture 用各自的默认工具链覆盖；Linux 上生成代码对 clang 的可移植性已验证（4 个 LLVM 版本，仅作证据） |
| `-needed_library` 经 clang 驱动与 mcpp 的行为 | `ld64.lld` 直接实测，未经驱动；CI 的静态闭包断言兜底 |
| 一个包多个目标、项目路径含空格/非 ASCII | 已写入非目标与限制，不处理 |
| 约 30 个库的链接在 Windows/macOS 上的影响 | Linux 上启动时间无可测差异；其他平台未测，因为 Windows 走 `deploy` 而非链接 |
| 25 次平均 31 ms 对 31 ms 只在一个平台、无方差 | 结论只是"无可测影响"，不据此宣称零代价 |

## 14. xlings 生态侧（不阻塞本 PR）

- `xim:qt`：不需要新增工具。
- **`xim:qt-addons`**：Linux 条目没有声明运行时依赖，所以可执行文件没被 elfpatch，`qsb` 等起不来。**已提修复 PR openxlings/xim-pkgindex#1262**（Q5）；仍缺的 `libbz2`、`libpulse`、`libbluetooth`、`libpcsclite`、`libspeechd` 与 `libtiff.so.5` 需要生态先有对应的包，是后续项。
- Qt Quick 在 Linux 需要 GL 驱动库：slint-m 已遇到同一问题（libglvnd 只是分发器），其解是依赖 `compat.glx-runtime`。建议在 `pkgs/q/qt.lua` 的 linux 运行时依赖里评估同样加入；本 PR 的 offscreen + software 渲染不依赖它。
- `xim:qt-base` 只含 QtQml 库这一事实，已在 `rules-qml` 的警告与文档里说明。

## 15. 向 mcpp 提的 issue（已提交：mcpp-community/mcpp#1011）

标题：`mcpp pack: take the dependencies of deployed shared libraries into the runtime closure`。正文要点：

- **现象**：`mcpp::deploy` 放到程序旁的共享库（`mcpp-package.json` 里 `role: library`）自己依赖的库不会被 `mcpp pack` 带上；闭包读取以程序本身为唯一根（`src/pack/pack.cppm`）。
- **复现**（Linux x86_64，`xim:qt` 6.11.1，mcpp 2026.11.20.1）：部署 `qml/QtQuick/Layouts/libqquicklayoutsplugin.so` 与 `plugins/platforms/*.so` 后 `mcpp pack --format dir`，包里没有 `libQt6QuickLayouts.so.6`、`libQt6XcbQpa.so.6`、`libQt6WaylandClient.so.6`、`libQt6EglFSDeviceIntegration.so.6`；对照：程序自己按全路径链接 `libQt6QuickLayouts.so` 后就被带上。
- **期望**：已部署的 `role: library` 文件也是闭包的根，或提供 `mcpp::runtime_closure_root(file)` 之类的指令。
- **现在的绕过**：`plugin_closure`（幂等，源码有标记，CI 有金丝雀）。
- 明确写了"xcb 平台插件在真实显示上加载失败是推断，没有观察到"。

## 16. 独立评审的处理

一次不受我先入为主影响的独立评审通读了本文与仓库规则，给出下列意见。我逐条核对了代码或做了实验，结论如下（"采纳"指已改入正文）。

| # | 意见（摘要） | 核实 | 处理 |
|---|---|---|---|
| 1 | 闭包补丁违反 `plugin-development.md` 规则 4、8；建议不打补丁、默认关或放进带到期日的 `compat/` | 属实：规则 8 原文是"引擎缺口要报告，不打补丁" | **你的决定**：先 workaround、默认开、无感、引擎修好后自动无事。我采纳了其中的"标注与强制移除"思想，做成与 `compat/` 同构的 `ENGINE-GAP` 标记 + `check-engine-gaps.sh`（因没有固定日期，不用到期日），并加金丝雀（Q9） |
| 2 | `plugin_closure`、`deploy_plugins` 用普通类型分不清"未设置"与"设成默认值" | 属实 | 采纳：`plugin_closure` 两处都默认 `true`；Quick 插件目录改用独立字段 `quick_plugins` 取并集（§8） |
| 3 | `qt.conf` 固定文本，`deploy_to` 非默认会丢翻译；未处理项目自带 `qt.conf` | 属实 | 采纳（Q3）：`Translations` 随 `deploy_to`；检测并警告用户自己的 `qt.conf`。内嵌到 qrc 的方案未采纳：文件方案已实测，内嵌的 `Prefix` 相对谁未验证 |
| 4 | README 下限列语义；`action::env` 是否存在 | README 列语义属实；`action::env` 存在（`hostprogram.cppm:441`，协议 13） | 采纳（Q4）：列写 2026.10.1.3 并注明；Q4 证据改为只声称验证过的 API |
| 5 | §6 的"都跑通"过度概括：单模块、`qsb`/`qmllint` 未运行、G4/C++ 单例无证据；延迟导入等没有真实样本；`closure.py` 写死路径、只认整行 `import` | 属实 | 采纳：补做多模块（`sum=19`）、跨模块继承、C++ `QML_SINGLETON`、无 C++ 类型模块、`lupdate` 的 qml 提取；§6 标题如实写明；Q2 列出只有合成样本覆盖的路径；`closure.py` 容忍行尾 `;`（4 个样本仍与 scanner 一致）。"40 个模块的样本用的是 Qt 自己的文件而不是用户代码"：属实，写在 Q1 的覆盖说明里，结论限于 SDK 内部的 import 图 |
| 6 | 规则 9：`qml-shaders` 永远不会失败；G9 无 fixture；`aot=false`、`styles` 等无运行用例 | 属实 | 采纳：G9 加进 `qml-consumer`；加三个变体（§10.2）；`qml-shaders` 的跳过用 `::notice::` 可见，并在限制里承认 G7 在 `xim:qt-addons` 修复前只有 argv 级覆盖 |
| 7 | 计划期读 `.qml` 内容，只声明 glob 不够 | 属实（`rules/qt.cppm` 对读过的头文件逐个声明） | 采纳（§5.1） |
| 8 | 文档清单缺项：`plugin-development.md` L2 表、脚本的 usage/case；交叉引用错；`--format` 写法；`compile_ex`、`tool_path` 扩大契约；`rule_module` 的说法含糊 | 属实 | 采纳（§4.1、§7 e、§11、§15、spikes README） |
| 9 | 测试：`env -i` 挡不住 RUNPATH；`.qmltypes` 文本非契约；mtime 粒度；`grep -a` 不证明 AOT；随机改字节无种子、sanitizer 与模块单元的组合；`dumpbin` 需要 VS | 属实 | 采纳（§10.1、§10.3）：挪走 SDK 目录；运行时断言继承；`sleep 2.2`；`llvm-nm` 符号；固定种子、sanitizer 不进 CI；统一用 `llvm-readobj`（也是你关于 LLVM 的意见的落点） |
| S1 | `link_flag` 的库可能被 `--as-needed` 丢弃 | **实测属实**（lld `--as-needed` 下 NEEDED 为 0） | 采纳（Q9、§7）：push/pop 包住；Mach-O 用 `-needed_library` |
| S2 | macOS bundle 里 `Prefix = .` 含义不同 | 无法验证 | 写入 Q3，归入 Q7 的范围，标明推断 |
| S3 | `qmllint` 多文件超 Windows 命令行；非 ASCII 路径 | 合理 | 采纳：`qmllint` 每文件一个动作；加载器 rsp 的行数等于文件数；非 ASCII 文件名警告 |
| S4 | qrc 未做 XML 转义 | 合理 | 采纳（§5.1、§10.1） |
| S5 | `std::ifstream` 窄字符串路径在 Windows 上读不了非 ASCII | 合理 | 采纳（Q2）：用 `std::filesystem::path` 打开 |
| S6 | 31 ms 对 31 ms 只在一个平台、无方差 | 属实 | 采纳（§13）：只宣称"无可测影响" |
| 缺 | 多目标/`kind=lib` 的包；交叉目标；`qtquickcontrols2.conf`；无 C++ 类型模块；uri 与 SDK 模块同名；`depends` 含糊；两模块共用头文件；`import_paths` 的非 SDK 依赖；`qmlformat`、`.qmllint.ini`、`.js` 的 `.import` | 合理 | 采纳（§1 非目标、§5.1、§5.4、§7、Q-证据）；其中一个包多目标实测确有影响（两个目标都编译生成的对象），写成限制 |
