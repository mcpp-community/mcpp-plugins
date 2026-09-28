# 实施计划：`deps-vcpkg`、`rules-qt`、`deps-cmake` 的跨仓库交付

状态：执行中 · 2026-09-26（引擎下界 2026.9.26.2） · 设计见 `2026-09-26-deps-vcpkg-rules-qt-design.md`（第 4 版）。

## 1. 第 3 轮决定（设计第 4 版据此修改）

| # | 决定 | 对设计的影响 |
|---|---|---|
| D6 | 附加模块合为一个包 | 确认 `xim:qt-addons`；按 xim 的载荷私有规则，它有**自己的前缀**，不写入 `xim:qt` 的目录 |
| D8 | 先做 mcpp / xlings 生态的通用能力；验证工程的 D 阶段只在 fork 上做初步验证 | 引擎需求 2 在 mcpp 中实现：构建程序声明运行时库目录 |
| D9 | 免装 VS 不是目标 | 删除 `toolchain::msvc_xim`；`xim:msvc` 由 mcpp 自动匹配，插件不声明 |

另有一项由实测带来的简化：vcpkg-tool 每个发布附带 `vcpkg-standalone-bundle.tar.gz`（3.4 MB，脚本与该工具
版本严格对应，`vcpkg-bundle.json` 含 `"usegitregistry": true`）。以它为 `VCPKG_ROOT` 时，`builtin-baseline`
清单经 git registry 解析，registry 落在 vcpkg 的每用户 registries 缓存。已在本机实测：对验证工程的
baseline `ea1a7396` 执行 `vcpkg install --dry-run` 解析出 `fmt 12.2.0`，缓存 137 MB。因此设计第 3 版 §3.6 的托管克隆、
按工具检出与文件锁全部取消。

## 2. 仓库与交付物

| 仓库 | 单个 PR 的内容 | 发布 |
|---|---|---|
| mcpp-community/mcpp | SPEC-007 与 #702 的合规设计（`runtime_search_dir`、`prepare`、stamp 规则、Windows DLL 放置），由 mcpp 侧实现 | 下一个 mcpp 发布；插件 PR 等待它 |
| openxlings/xim-pkgindex | `xim:vcpkg` 2026.7.27（工具 + standalone bundle）、`xim:qt` 6.11.1、`xim:qt-addons` 6.11.1；`xim:7zip` 说明 `7z.dll` 位置；测试 | 合入即发布索引；`xlings-res/vcpkg` 资源用 `gh` 与 `gtc` 双端上传 |
| mcpp-community/mcpp-plugins | `0.13.0`：`deps-vcpkg` + `mcpp-vcpkg`、`rules-qt` + `rules-qt-xim` + `rules-qt-xim-addons`、`deps-cmake` + `mcpp-cmake`；fixtures；CI 引擎版本；README | tag `v0.13.0`，GitHub release，`gtc` 上传 `mcpp-res/mcpp-plugins` |
| mcpp-community/mcpp-index | 登记 `mcpp:plugins 0.13.0` | 合入即发布 |
| 验证工程（fork） | 临时 PR 2：迁移 A–C，D 阶段初步验证 | 不合入 |

## 3. 依赖关系

```
T2 xim 包（vcpkg/qt/qt-addons）──► T2r 索引合入（xim-pkgindex#878，已合入）
T3 插件按 SPEC-007 实现（PR 已开，等待）──┐
T1 mcpp#702 实现并发布（mcpp 侧）─────────┴──► T3c 插件 CI 全绿 ──► T3r 插件发布 0.13.0 ──► T4 mcpp-index ──► T5验证工程PR 2
```

- T1、T2、T3 并行：T3 在本机用 T1 的源码构建验证 Linux 路径；Windows 与 macOS 路径只能由 CI 验证。
- T3 的 CI 依赖 T1r（`MCPP_VERSION` 指向新引擎）与 T2r（索引里有 `xim:vcpkg`、`xim:qt`）。
- T5 用索引中的 `mcpp:plugins 0.13.0`，不用路径依赖，以验证用户实际得到的东西。

## 4. 多角度检查项（实施与 review 共用）

| 角度 | 检查项 |
|---|---|
| 架构 | `deps-*` 家族只描述、重活在 action；插件拥有它驱动的 xim 包；运行时目录经引擎通道而非逐文件复制 |
| 稳定性 | emit 在未安装依赖时返回 0；安装边只在输入变化时重跑；各项目的 buildtrees/packages 隔离；vcpkg 自身对安装目录加锁 |
| 优雅简洁 | 零配置可用；registry 管理交给 vcpkg；一个选项结构 + 一个入口函数 |
| 用户体验 | 所有拒绝都给出下一步；缺依赖时 warning 写明 `mcpp build` 会安装它 |
| 兼容性 | 旧引擎给出明确的版本下界提示；已有 `vcpkg_installed/` 布局不变；自定义 triplet 与 overlay 照常 |
| 跨平台 | Windows x86_64 / Linux x86_64 / macOS：`deps-vcpkg` 三平台 fixture；`rules-qt` Windows + Linux（macOS 视 Qt 框架布局而定） |
| 一致性 | 选项、警告格式、`mcpp::fact` 版本记录与既有成员一致；README 成员表同格式 |
| 无感升级 | 插件次版本号前移；不改变已有成员的行为；引擎新增指令，旧程序不受影响 |
| 测试覆盖 | 每个功能一个正向 fixture + 一个拒绝/边界断言；`all-rules-compile` 覆盖新模块在三平台编译 |

## 5. 进度

| 任务 | 状态 |
|---|---|
| T0 设计第 4 版、本计划 | 完成 |
| T1 引擎 | mcpp#702 由 mcpp 侧合入，随 2026.9.26.2 发布 |
| T2 xim 包 | xim-pkgindex#878、#879 已合入；`xlings-res/vcpkg` 双端资源已校验 |
| T3 插件 | PR #29；Linux 各 fixture 在 2026.9.26.2 上通过；Windows、macOS 由 CI 验证 |
| T3r / T4 / T5 | 等待 T3c |

实测发现两项引擎缺口，均已报告：

- 主机构建不读取主机三元组的 `[target.<triple>]` 段（mcpp#704），因此 Qt fixture 在 `[build]` 中声明
  `cxx_runtime = "toolchain-coupled"`。
- `path` 包的宿主工具以整棵目录树的 stamp 为键，嵌套其中的消费方写出的文件改变该键（mcpp#705）。fixture
  位于插件仓库之内，因此其安装位于 `target/` 之下，检查脚本的日志写入 `target/ci/`，第二次构建以
  `--profile dev` 绕过快路径，使 Linux 与 Windows、macOS 走同一条规划路径。从索引取得插件的项目不受影响。

## 6. 0.14.0：运行时数据与精简 Qt（验证工程验证后的第二轮）

验证工程在 0.13.1 上构建成功后仍有两个手动步骤（解压嵌入式 Python、运行 `Release.py`），Qt 下载量为
完整基础包。第二轮把固定步骤程序化，并补齐 windeployqt 的 Qt 翻译：

| 交付 | 仓库 | 机制 |
|---|---|---|
| `deps-archive` | mcpp-plugins | 配置期读 zip 中央目录，一个 `artifact` action 解压并逐一命名输出（SPEC-007 R3.2），每个输出 `mcpp::deploy`；`mcpp run` 与 `mcpp pack` 均可见 |
| `deps-vcpkg` / `deps-cmake` 的 `deploy` | mcpp-plugins | 前缀中的文件由命名输出的复制 action 取出再部署；不依赖引擎「deploy 等待 prepare」 |
| `translations::qt_languages` | mcpp-plugins | `lconvert` 合并所链接模块的目录为 `qt_<lang>.qm`，即 windeployqt 的产物 |
| `rules-qt-xim-base` / `xim:qt-base` | mcpp-plugins、xim-pkgindex、xlings-res | qtbase + qttools + qttranslations + 单独重发布的 QtQml 库；下载量约为 `xim:qt` 的三分之一 |
| `libs/qtsdk.lua` | xim-pkgindex | qt、qt-base、qt-addons 共享的下载、镜像、解压与标记逻辑 |

实测：库包中的 `artifact` 输出经 `mcpp::deploy` 传播到依赖它的程序的 `bin/` 与 `mcpp pack` 目录；依赖包中无下游
消费者的 `artifact` action 随下游构建执行。

## 7. 0.14.1：Linux 上依赖的 C++ 标准库与程序一致

Windows 上 MSVC ABI 的编译器共用 Microsoft 的标准库，macOS 上共用 libc++；Linux 上主机编译器使用
libstdc++，mcpp 的 clang 使用 libc++，两者的 `std::` 符号互不链接。0.14.1 在 Linux 的 libc++ 工具链下：

| 交付 | 机制 |
|---|---|
| `deps-vcpkg` 生成的 triplet | 默认 triplet 为 `<arch>-linux-libcxx`，经 `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` 以 mcpp 的 clang 编译端口，再接 vcpkg 自身的 Linux 工具链；clang 的配置文件给出 libc++ 与 mcpp 链接的 C 库 |
| 每个 triplet 一个 vcpkg 安装 | vcpkg 的清单模式从安装中移除其余 triplet 的包；前缀改为 `<install root>/<triplet>/<triplet>`，两个工具链的前缀并存，切换不重装 |
| `deps-cmake` | 未经 `cache_args` 指定编译器或工具链文件时，传入 mcpp 的 clang |

判据（Linux CI `vcpkg-libcxx`）：llvm 下 `libfmt.a` 含 `std::__1::` 符号且程序运行；默认工具链构建后 libc++
前缀仍在；切回 llvm 不重跑安装；deps-cmake 子项目的 `CMAKE_CXX_COMPILER` 为 mcpp 的 clang。

验证工程上游仅支持 Windows（README 所述，代码直接调用 WinAPI），Linux 的判据由本仓库的 fixture 承担；
验证工程PR2 作为 Windows 回归验证。
