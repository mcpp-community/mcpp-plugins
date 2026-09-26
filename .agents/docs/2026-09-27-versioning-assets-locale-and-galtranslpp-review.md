# 版本号、资源命名、locale 与 GalTranslPP 构建体验评估

日期：2026-09-27。范围：0.15.x 交付遗留的三个问题的最佳形态，以及 GalTranslPP PR2 相对 VS 与早期 mcpp 构建的对比。
所有结论均经本地实验或源码核对；证据列于各节末尾。

## 1. Qt 版本号 6.11.1.1

### 1.1 事实

| 组件 | 行为 | 证据 |
|---|---|---|
| xlings 安装判定 | 仅依据 `(ns, name, version)` 对应的载荷目录与 ledger；不存在配方修订号、构建号或载荷哈希 | `xlings/src/core/xim/install_state.cpp:35-80`；`xim-pkgindex/pkgs/g/glibc.lua:93-101` |
| xlings 版本解析 | 先按字面键查找；未命中时按区间解析，三段精确版本以三段为下限比较，`12.9.1` 选中 `12.9.1.4` | `semver.cppm:255-264`；本地 `xlings info libcublas 12.9.1` → 12.9.1.4 |
| xlings 区间安装 | 已安装版本满足区间时不升级：`~6.11.1` 在装有 6.11.1 的机器上保持 6.11.1 | 本地实验：同步索引后以 `~6.11.1` 构建 qt-consumer，仍使用 6.11.1 |
| mcpp 载荷定位 | 先取字面目录；三段精确版本视为 pin，字面目录缺失即返回空，不接受 6.11.1.x | `mcpp/src/xlings/xlings.cppm:941-951` |
| 生态先例 | 第四段作为打包修订：fontconfig 2.15.0.1、libglvnd 1.7.0.1、llvm-dev 20.1.7.1 | `xim-pkgindex/pkgs/` |

### 1.2 结论（2026-09-27 修订）

6.11.1 与 6.11.1.1 均于 2026-09-26 发布，项目 CI 之外没有消费者。持有修正前 6.11.1 载荷的只有三处：
plugins CI 缓存、GalTranslPP CI 缓存与本机沙箱，均可清除。修订号的价值在于保护已有用户，此处不存在该前提。

- **最佳形态：只保留一个键 `6.11.1`，内容为修正后的配方；删除 `6.11.1.1`。** 消费方写上游版本 `6.11.1`，xlings 与 mcpp 都按字面解析，两侧一致。
- 清理：两处 CI 的缓存 key 升级，本机沙箱删除旧载荷。
- 约定：包在索引中没有外部消费者时，就地修正；有外部消费者后，使用第四段修订号，直到 xlings 支持 `revision`（§1.3）。

### 1.3 根治方向（生态层，非本次交付）

1. xlings：配方声明 `revision`，ledger 记录之；修订不一致即重装。此后打包修正不再需要新版本键，用户写上游版本 `6.11.1`。
2. mcpp：载荷定位与 xlings 采用同一解析规则（或直接询问 xlings 选定的版本），消除"一个问题两个答案"。

## 2. brotli 的 `-r1` 资源名

### 2.1 事实

- GitHub `xlings-res/brotli@1.2.0`：规范名 `brotli-1.2.0-linux-{x86_64,aarch64}.tar.gz` 已是修正内容，sha256 与 `-r1` 相同（`0b20a7cc…`、`07f52426…`）。
- GitCode 同名资源仍是缺头文件的旧内容；`gtc` 无删除与覆盖能力。
- xlings 下载缓存以 URL 文件名为键；配方声明 sha256 时，缓存文件校验失败即重新下载（`downloader.cpp:508-525`）。因此同名替换对下载层是安全的。
- 受影响面：已安装旧 brotli 载荷的机器不会重装（见 §1.1），但缺失的仅是头文件；Qt 只加载 `.so`，无功能影响。

### 2.2 最佳形态与步骤

目标：两端恢复规范名，配方不再引用 `-r1`。

1. **（需你手动）** 在 GitCode `xlings-res/brotli` 的 1.2.0 release 中删除 `brotli-1.2.0-linux-x86_64.tar.gz`、`brotli-1.2.0-linux-aarch64.tar.gz` 与 `SHA256SUMS`。
2. 以规范名向 GitCode 上传修正文件，并在两端上传规范名的 `SHA256SUMS`；随后逐字节核对两端。
3. xim-pkgindex PR：brotli 的 URL 改回规范名，sha256 不变。
4. 索引发布后，删除两端的 `-r1` 资源（GitHub 由我执行，GitCode 由你手动）。

约定（写入发布流程）：资源发布前先在本地以配方安装一次并检查载荷；已发布资源若有误，同名替换并更新 sha256，不另起文件名。

## 3. xlings glibc 缺少 UTF-8 locale

### 3.1 事实

- xim glibc 载荷不含 `lib/locale` 与 `locale-archive`；libc 编译期 locale 路径为占位符 `/nonexistent/xlings-use-rpath-not-default-search/lib/locale`，不存在。
- 因此 `setlocale(LC_ALL, "")` 在任何主机上都回落到 `C`，Qt 报告 "failed to switch to UTF-8"。
- 本地实验：用载荷自带的 `localedef` 生成 `C.utf8`（416 KB），经 `LOCPATH` 提供后，Qt 输出变为 "has switched to C.UTF-8"，即功能恢复。

### 3.2 影响

- GalTranslPP：无影响（仅 Windows）。
- Linux 上的 Qt 程序：Qt 6 在 Unix 上的 local8Bit 固定为 UTF-8，Qt 自身文本处理不受影响；受影响的是程序直接调用的 C 库多字节与宽字符函数（`mbstowcs`、`std::locale("")` 等）。

### 3.3 最佳形态

归属 xim glibc 配方（非插件、非 mcpp）：

1. 载荷内置 `lib/locale/C.utf8`（416 KB）；
2. 安装时将占位符前缀改写为载荷前缀（与 elfpatch 改写解释器同理），使 libc 默认 locale 路径指向载荷自身。

两项完成后无需 `LOCPATH`，`mcpp run` 与 `mcpp pack` 产物行为一致。不采用 `LOCPATH` 注入方案：它对打包后由用户直接启动的程序无效。

## 4. GalTranslPP：VS → mcpp → 优化后的 mcpp

### 4.1 首次构建所需操作（Windows）

| | VS（ebd2a90 之前） | mcpp（上游 8a2c76a） | 优化后 mcpp（PR2，plugins 0.15.1） |
|---|---|---|---|
| 手动安装 | VS 2026 IDE（指定 MSVC v14.50）、CMake、git、vcpkg（克隆、bootstrap、integrate）、Qt 在线安装器（需账户）、Qt VS Tools 插件 | xlings、mcpp、VS Build Tools、CMake、Python、git、vcpkg（克隆、bootstrap、PATH）、Qt 在线安装器 | git、xlings、VS Build Tools |
| Qt 路径配置 | 3 处：Qt VS Tools 导入、ElaWidgetTools `CMakeLists.txt`、vcxproj `QtInstall` | 2 处：`build.py`、`qt-root.txt` | 0 处 |
| 依赖构建 | 手动运行 `build.bat` 编译 ElaWidgetTools，并检查 3 个产物 | 手动运行 `build.py` 与 `vcpkg install --triplet …` | 由构建完成（`deps-cmake`、`deps-vcpkg`） |
| 构建命令 | IDE 中切换 Release 并逐项目生成 | `mcpp build -p GPPCLI/GPPGUI` | 同左 |
| 运行时部署 | `Release.bat` | 解压 Python、`Release.py`、`windeployqt` ×2 | 由构建完成：Qt 库、插件与翻译、Ela、vcpkg DLL、7z.dll、Python、OpenCC、BaseConfig、VC++ 运行时 |
| 分发 | 手动整理 | 手动整理 | `mcpp pack --format dir` |
| 手动步骤合计 | 约 12 | 约 13 | 4（安装 xlings、安装 mcpp、克隆、`mcpp build`） |

### 4.2 CI 耗时（GitHub Windows runner）

| 阶段 | 早期 mcpp + 手动 vcpkg（run 36164741493） | plugins 0.14.0（run 36234085633） | plugins 0.15.1（run 36260450278） |
|---|---|---|---|
| vcpkg 二进制缓存 | 未命中，独立安装 51 分钟 | 命中 678 MB，缺失部分在 emit 中源码编译（保存后增至 999 MB） | 命中 999 MB，完整 |
| `mcpp emit`（全新 checkout） | 0.4 分钟（依赖已预装） | 53 分钟 | 4.8 分钟 |
| GPPCLI（fast-release） | — | 6.5 分钟 | 6.3 分钟 |
| GPPGUI（fast-release） | — | 18.0 分钟 | 19.7 分钟 |
| 无改动的第二次构建 | — | 0.8 分钟，无安装 | 0.8 分钟，无安装 |

更正：53 → 4.8 分钟的主要原因是 vcpkg 二进制缓存从部分命中变为完整命中，而非移除 mcpp-deps。

全新 checkout 上 emit 的 4.8 分钟包含：

1. **宿主工具构建**：GPPGUI 以 `tools = ["Updater"]` 取得 Updater.exe，emit 为此以宿主身份完整构建 Updater（Qt 与 vcpkg 程序），runtime_stage 同理。
2. **第二套 LLVM**：宿主工具构建不继承工作区的 `[toolchain] windows = "llvm@22.1.8"`，日志显示 "no toolchain configured — installing llvm@20.1.7"，因此额外下载一套 LLVM 并编译其 std 模块。
3. **vcpkg 安装**：Updater 的构建会执行 vcpkg install，此时从二进制缓存恢复约 22 个库。
4. **构建程序**：5 个成员的 build.mcpp 及其 host 模块（mcpp.plugins、gpp.build）各编译一次。

已构建过一次后，宿主工具按"包源码 + 宿主工具链"缓存，vcpkg 前缀已存在，构建程序显示 "up to date (cached)"，因此 emit 预计为秒级。
同条件下的"第二次构建"为 0.8 分钟，可作旁证；emit 本身未单独测量。

另：Updater 作为发布物，语义上是目标产物而非宿主工具（交叉编译时架构即不同）。
以宿主工具取得它，既导致重复构建，也引入第二套工具链。正确形态需要 mcpp 提供"目标产物依赖"或工作区级发布布局。

### 4.3 可配置项

| 配置项 | 方式 | 当前状态 |
|---|---|---|
| Qt SDK | build.mcpp `qt.root` → `QT_ROOT_DIR` → 成员声明的 `xim:qt-base` | 可用；设置 `QT_ROOT_DIR` 时 `xim:qt-base` 仍会被下载（mcpp 无条件安装声明的包） |
| vcpkg 根目录 | build.mcpp `options.root`，缺省为 `xim:vcpkg` | 可用；有意不读 `VCPKG_ROOT`，以保证版本确定 |
| vcpkg 下载与二进制缓存 | `VCPKG_DOWNLOADS`、`VCPKG_DEFAULT_BINARY_CACHE` | 可用 |
| 工具链 | 根 `mcpp.toml` `[toolchain] windows = "llvm@22.1.8"` | 可用 |
| 优化级别 | `--profile release` / `fast-release` | 可用 |

### 4.4 与最佳形态的差距

| # | 差距 | 处理 |
|---|---|---|
| G1 | `how-to-build.md` 写 "Qt 6.11.1"，清单写 `6.11.1.1`，且未说明可配置项 | 改 PR2 文档：版本统一为 `6.11.1`，以 `> 注:` 说明 `QT_ROOT_DIR`、vcpkg 缓存变量与 `fast-release` |
| G2 | 版本重复声明：`mcpp.plugins` 在 5 个成员中各写 `0.15.1`；`xim:qt-base` 与 `xim:7zip` 在 4 个成员中各写一次 | `mcpp.plugins` 改用已有的 `[workspace.dependencies]` 加 `.workspace = true`（需验证它能否与 `features`、`host-module` 同用）。xlings 条目没有逐项继承机制：先验证根清单的 `[target.windows.xlings.workspace]` 被继承后 `xpkg_dir` 是否对成员生效；不生效则向 mcpp 提出 xlings 条目的 `.workspace = true` |
| G3 | `QT_ROOT_DIR` 生效时仍下载 `xim:qt-base` | mcpp 侧能力：声明可被覆盖的 xlings 包；记为 mcpp issue 候选 |
| G4 | 仍需手动安装 VS Build Tools（vcpkg 的 MSVC triplet 与 ElaWidgetTools） | 长期项：vcpkg chainload mcpp 的 clang；现阶段保留为文档前提 |
| G5 | 成员清单中 7z 的注释位于 Qt 条目之上 | 随 G1 修正 |
| G6 | PR2 为验证分支（`ci(temporary)` 提交） | 向上游提交时压缩为一次迁移提交，CI 工作流按上游需要保留或删除 |
| G7 | emit 的首次开销来自宿主工具与第二套 LLVM（§4.2） | mcpp：宿主工具继承工作区工具链；提供目标产物依赖。在此之前，GalTranslPP 可先在 CI 中固定默认工具链 |

### 4.5 结论

构建体验已达到当前生态能力下的最佳形态：首次构建为 4 个步骤，路径配置为零，运行时完整部署，第二次构建无安装。
剩余差距中，G1 与 G5 属于文档修正，可立即完成；G2 需一次 CI 验证；G3 与 G4 取决于 mcpp 与工具链能力，不影响当前可用性。

## 5. 待决事项

| 事项 | 负责方 |
|---|---|
| 删除 GitCode brotli 旧资源（§2.2 第 1 步与第 4 步） | 你 |
| brotli 恢复规范名（§2.2 第 2、3 步） | 我，在你完成删除后执行 |
| PR2 文档修正（G1、G5）与 G2 验证 | 我 |
| glibc locale（§3.3）、xlings 修订号（§1.3-1）、mcpp 解析一致性（§1.3-2）与可覆盖声明（G3） | 各仓库 issue，待你确认后提交 |

## 6. 0.15.2 之后的状态（2026-09-27）

执行方案见 `2026-09-27-plugins-0.15.2-plan.md`。本文件前几节的结论按如下更新：

| 项 | 状态 |
|---|---|
| §1 Qt 版本 | 索引只保留 `6.11.1`（xim-pkgindex#891）；消费方书写上游版本 |
| §2 brotli | 两端均为规范名，内容一致；GitHub 的 `-r1` 已删除 |
| §3 locale | 已提交 openxlings/xlings#621 |
| G1、G5 | 已完成：`how-to-build.md` 用 `> 注:` 列出 Qt、vcpkg、vcpkg 缓存、工具链与插件版本的配置方式 |
| G2 | 部分成立：成员以 `.workspace = true` 继承插件版本；`gpp.build` 不是成员，无法继承（mcpp#714）；`xim:qt-base` 仍逐成员声明（mcpp#713） |
| G3 | 属预期行为：不使用 xim 提供的 Qt 时，注释掉声明，再设置 `QT_ROOT_DIR` 或 `gpp::qt_root` |
| G4 | 不变：仍需 VS Build Tools |
| G7 | 已提交 mcpp#710（宿主工具不使用工作区工具链）、#711（缺少目标产物依赖） |
| emit | 首次 258 s（包缓存为空）；构建后再次 emit 62 s，原因见执行方案 §6 |
