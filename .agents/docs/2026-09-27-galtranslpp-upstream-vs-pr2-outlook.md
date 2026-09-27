# GalTranslPP：PR2 与上游最新的差异，及后续构建优化的上限

日期：2026-09-27。

| 对象 | 提交 | 说明 |
|---|---|---|
| 共同基点 | `8a2c76a` | PR2 分叉处 |
| 上游最新 | julixian/GalTranslPP `e64f93e`（2026-09-27 03:45，"update build system"） | 基点之后 2 个提交，48 个文件，+1319/−730 |
| PR2 | Sunrisepeak/GalTranslPP `43ee88f`（`ci/mcpp-plugins-deps-qt`） | 基点之后 15 个提交，21 个文件，+771/−567；plugins 0.15.2，CI run 36272623612 全绿 |

## 1. 结论

1. **构建架构已趋同，依赖获取策略相反。**
   - 上游采纳了 PR2 的构建结构：共享构建逻辑作为模块 `gpp.build` 导入，rules-qt 的翻译流程，deps-vcpkg 与 deps-cmake 的机制，以 `${mcpp.self} stage` 动作写出 Release 布局，runtime-stage。两边都删除了旧的 5 个头文件与 `qt-root.txt`。
   - 分歧在取得方式：上游要求开发者预装 vcpkg、CMake、Qt，并在源码中填写 Qt 路径；Python 解压、`Release.py`、`windeployqt` 保留为构建后的手动步骤。PR2 由 xlings 取得全部工具，构建本身完成部署。
2. **差异规模大，PR2 不能直接变基。** 两边改动重叠 17 个文件；试合并产生 10 处冲突：4 个成员的 `mcpp.toml` 与 `build.mcpp`、`how-to-build.md`、`Release.py`。下一版应基于 `e64f93e` 重做，而不是合并 PR2。
3. **上游有三处优于 PR2，应当吸收：**
   - 插件经 `reexport = true` 只在 `gpp.build` 声明一次；
   - Windows 专属的 defines、系统库、链接选项放进 `[target.windows.*]` 或按驱动生成；
   - 源码中的 `#pragma comment(lib, …)` 已删除，系统库由清单声明。
4. **上游复制了插件的约 508 行代码（`deps/{tools,vcpkg,cmake}.ixx`），暴露了插件的缺口。** 起因是 `deps-vcpkg`、`deps-cmake` 这两个 feature 无条件声明 `xim:vcpkg`、`xim:cmake`，只要启用就会下载。上游为了用本机工具，只能改用内部 feature `deps`（清单注释写明"消费方不应直接使用"），再在本地复制并修改这两个模块。复制的代码依赖插件内部接口（`absolute_from_root`、`deploy_after`、`link_libraries`），插件升级时可能失效。
5. **本次新验证的事实：GalTranslPP 今天就能把所有声明集中到一处。** 见 §3。

## 2. 差异表

| 维度 | 上游 `e64f93e` | PR2 `43ee88f` | 更优 |
|---|---|---|---|
| 插件版本 | 0.15.1；只在 `gpp.build` 声明，`reexport = true` 转交各成员（1 处） | 0.15.2；根 `[workspace.dependencies]`、`gpp.build` 各写一次（2 处）；GPPCLI、GPPGUI、Updater 与 `gpp.build` 各列 features | 上游 |
| 插件 features | `deps`（内部 feature）、`rules-qt` | `deps-vcpkg`、`deps-cmake`、`deps-archive`、`rules-qt` | PR2（公开接口） |
| vcpkg | 本机安装；`gpp-build.ixx` 中的 `vcpkg_root` 或 PATH；使用复制并修改的模块 | `xim:vcpkg`，可用 `options.root` 改用本机 | 各有取向 |
| CMake | 本机安装；`cmake_executable` 或 PATH | `xim:cmake` | 各有取向 |
| Qt 来源 | 必填：`qt_root = R"(D:\Qt\6.11.1\msvc2022_64)"`，为空或无效即报错；不读 `QT_ROOT_DIR` | `gpp::qt_root` → `QT_ROOT_DIR` → 成员声明的 `xim:qt-base` 6.11.1 | PR2 |
| Qt 运行时部署 | `deploy_plugins = {}`、`qt_languages = {}`；手动执行 `windeployqt` ×2 | 由构建部署 platforms/styles/imageformats 与 `qt_zh_CN.qm` | PR2（需维护者确认取向） |
| 嵌入式 Python | 手动解压 zip | `deps-archive` 在构建时解压 | PR2 |
| BaseConfig、SampleProject | `Release.py` 复制 | 构建部署，并写入 Release 布局 | PR2 |
| 7z.dll | 仓库内二进制 `3rdParty/7z.dll`（1.9 MB），`Release.py` 与 runtime-stage 复制 | `xim:7zip`，已从仓库删除 | PR2 |
| OpenCC 数据 | 构建复制到 `Release/<成员>/BaseConfig/opencc` | 构建部署到程序旁，并写入 Release 布局 | 相同（PR2 另支持 `mcpp run`） |
| Updater 发布 | Updater 自身的构建程序写入 GPPGUI（`Updater.exe`）与 GUICORE（`Updater_new.exe`） | GPPGUI 取工具边产物 `dep_bin("gpp.updater")` 再复制 | 上游（不依赖宿主工具产物，避开 mcpp#711） |
| Windows 专属设置 | 各成员 `[target.windows.build]`；`/DEBUG`、`/OPT` 按驱动在代码中生成；runtime-stage 在 `[target.windows.build-dependencies]` | defines 放在 `[workspace.build]`，所有目标通用；`-Wl,/DEBUG` 写在 profile；runtime-stage 无条件声明 | 上游 |
| 源码 | 删除 `#pragma comment`；`Tool.cpp` 的 `#ifdef` 改为包住整个函数；Updater 改用 `QCoreApplication` 与 `import boost`；GUI 的 GIL 释放移出 try；ElaWidgetTools 子模块更新 | 未改源码 | 上游 |
| vcpkg 目录 | `vcpkg-scripts/{ports,triplets}`，`install_root` 显式 | 原 `ports/`，默认 `install_root` | 上游（目录更清晰） |
| `mcpp run` / `mcpp pack` | 程序旁缺少 BaseConfig 与 Qt 插件，不能直接运行或打包 | 可用 | PR2 |
| 文档 | 手动安装 8 项工具，填写路径，构建后 3 步 | 手动安装 git、xlings、VS Build Tools；`> 注:` 说明可配置项 | PR2 |
| CI | 无 | 验证工作流（三道检查） | PR2 |
| 仓库内构建代码 | 1139 行（其中复制的插件代码 508 行，`Release.py` 43 行） | 741 行 | PR2 |
| 首次构建所需手动操作 | 约 13 步 | 4 步 | PR2 |

## 3. 本次验证：声明可集中到 `gpp.build`

**探针**：一个工作区，成员 `app` 以 `host-module` 依赖一个非成员的路径包 `shared`（对应 `gpp.build`）。mcpp 2026.9.26.2，plugins 0.15.2。工程位于 `scratchpad/reexport-probe`。

| `shared` 中的声明 | 成员构建程序观察到的结果 |
|---|---|
| `mcpp.plugins = { …, features = ["deps-archive", "rules-qt"], host-module = true, reexport = true }` | 成员可以 `import mcpp.rules.qt` 与 `import mcpp.deps.archive` |
| `"probe.stage" = { path = …, tools = ["stage"], reexport = true }` | `dep_bin("probe.stage", "stage")` 返回宿主工具路径 |
| `[xlings.workspace] "xim:qt-base" = "6.11.1"` | `xpkg_dir("xim", "qt-base")` 与 `rules::qt::root()` 均为 `…/xim-x-qt-base/6.11.1` |
| 删除上一行（对照组） | 两者均为空 |

**推论**：插件版本、features、runtime-stage、`xim:qt-base` 与 `xim:7zip` 都可以只在 `gpp.build` 声明一次。

- PR2 为 mcpp#713、#714 所做的逐成员声明因此不再需要。这两个 issue 对根清单仍然成立，但不再阻碍本项目。
- 附带发现：`gpp.build` 声明了 Qt 之后，插件合成的规则程序会走"有 SDK"的分支。在 Linux 上它会输出 libc++ 提示；在 Windows 上预计不输出。0.15.2 的静默条件只覆盖"无 SDK"，应推广为"无 build.mcpp 且无设备源时，不论有无 SDK 都直接返回"（见 §4 的 P2）。

## 4. 后续优化能做到什么地步

### 4.1 分层与前提

| 层 | 内容 | 前提 | 由谁完成 |
|---|---|---|---|
| S0 | 上游 `e64f93e` 现状 | — | — |
| S1 | PR2（参照） | mcpp 2026.9.26.2、plugins 0.15.2 | 已完成 |
| S2 | 基于 `e64f93e` 重做，保留其结构与本机工具模式；吸收 PR2 的自动部署（Python、BaseConfig、SampleProject、Qt 插件与翻译、7z）；所有声明集中到 `gpp.build`；Qt 按 `qt_root` → `QT_ROOT_DIR` → `xim:qt-base` 的顺序选取 | 现有发布版即可 | 我方（GalTranslPP 新 PR） |
| S3 | S2 + plugins 0.16：P1、P2 | 插件发布 | 我方（mcpp-plugins） |
| S4 | S3 + 下一版 mcpp：#705、#707、#710、#711、#716、#712 | mcpp 发布；mcpp main 在 2026.9.26.2 之后只有一个文档提交，尚无相关修复 | mcpp 上游 |
| S5 | 长期：不再需要 VS Build Tools（让 vcpkg 通过 chainload 使用 mcpp 的 clang）；action 可设环境与工作目录（#708）；feature 可声明自身所需的工具（#709） | vcpkg triplet 工作、mcpp 能力 | 生态 |

插件侧的两项：

- **P1**：`deps-vcpkg`、`deps-cmake`、`deps-archive` 不再无条件声明工具载荷。工具按 `options` → 消费方声明的 `xim:*` → PATH 的顺序选取，与 rules-qt 选取 Qt 的方式一致。于是"注释掉一行"就能在自动模式与本机模式之间切换，上游复制的 508 行可以删除。
- **P2**：合成的规则程序在没有 build.mcpp 且没有设备源时，一律直接返回。

### 4.2 各层对比

测量来源：
- "实测"均为 GitHub Windows runner、vcpkg 二进制缓存命中、fast-release，来自 run 36272623612；
- "预计"未经测量；
- S0 没有测量：CI 上需要 Qt 在线安装器，无法运行。

| 指标 | S0 上游 | S1 PR2 | S2 | S3 | S4 | S5 |
|---|---|---|---|---|---|---|
| 手动安装 | git、xlings、mcpp、VS BT、CMake、vcpkg（克隆+bootstrap）、Qt（在线安装器，需账户）、Python | git、xlings、mcpp、VS BT | 同 S0（本机模式）；Qt 可改由 xim 取得 | 自动模式：同 S1；本机模式：同 S0 | 同 S3 | git、xlings、mcpp |
| 必须配置的路径 | 1（`qt_root`） | 0 | 0 | 0 | 0 | 0 |
| 构建后手动步骤 | 3（解压 Python、`Release.py`、`windeployqt` ×2） | 0 | 0 | 0 | 0 | 0 |
| 声明位置：插件版本 / Qt / 7zip / runtime-stage | 1 / 源码路径 / 仓库二进制 / 3 | 2 / 4 / 4 / 3 | 1 / 1 / 1 / 1 | 同 S2 | 同 S2 | 同 S2 |
| 仓库内构建代码 | 1139 行 | 741 行 | 约 1200 行（预计） | 约 700 行（预计） | 同 S3 | 同 S3 |
| 自动模式 ⇄ 本机模式 | 仅本机 | 仅自动（vcpkg/Qt 可指定路径，但仍会下载） | Qt 可切换；vcpkg/CMake 仅本机 | 全部可切换，注释掉一行即可 | 同 S3 | 同 S3 |
| `mcpp run` / `mcpp pack` | 否 | 是 | 是 | 是 | 是 | 是 |
| 首次 emit（包缓存为空） | 未测 | 258 s（实测） | 约同 S1 | 约同 S1 | 下降（预计）：不再安装第二套 LLVM（#710），emit 不再完整构建宿主工具（#707）；幅度待实测 | 同 S4 |
| GPPCLI / GPPGUI 编译 | 未测 | 6.2 / 18.2 min（实测） | 约同 S1 | 约同 S1 | 约同 S1 | 约同 S1 |
| 无改动的第二次构建 | 未测 | 0.8 min，无安装（实测） | 约同 S1 | 约同 S1 | 约同 S1 | 约同 S1 |
| 构建后再次 emit | 约同 S1（同样存在 Updater 工具边） | 62 s（实测） | 约同 S1 | 约同 S1 | 秒级（预计，同 profile 时）：#705 修复后，Updater 不再被重建 | 同 S4 |
| 删除载荷后的恢复 | — | 需手动删除 provisioning 印记 | 同 S1 | 同 S1 | 自动重新安装（#716） | 同 S4 |
| 交叉构建时的 Updater | 正确（由自身构建发布） | 取宿主产物（#711） | 正确（沿用上游做法） | 正确 | 正确 | 正确 |

### 4.3 上限的解读

- **S2 是现有发布版下的上限。** 首次构建只需 4 步（安装 xlings、安装 mcpp、克隆、`mcpp build`），所有版本只写在一处，同时保留上游偏好的本机工具模式。它唯一的缺口是 vcpkg 与 CMake 无法按需改由 xim 取得。
- **S3 使两种模式对称。** 同一份仓库既能零配置构建，也能使用本机工具，切换方式是注释掉 `gpp.build` 中的一行；上游复制的插件代码可以全部删除。
- **S4 只改变耗时与健壮性，不改变操作步骤。** 收益集中在 emit：构建后的 emit 从 62 s 降到秒级，首次 emit 省去第二套 LLVM。编译时间由源码规模决定，各层不变；要继续缩短，只能靠二进制缓存（vcpkg 已缓存）或减少 LTO/opt，与构建系统无关。
- **S5 才能去掉 VS Build Tools。** 这取决于 vcpkg 的 triplet 能否通过 chainload 使用 mcpp 的 clang，以及 ElaWidgetTools 的 CMake 构建能否同样改用 clang，不在短期范围内。

## 5. 建议的下一步

1. **GalTranslPP 新 PR（S2）**：
   - 从 `e64f93e` 开分支，保留上游结构、源码修正与 `reexport`；
   - 把插件、runtime-stage、`xim:qt-base`、`xim:7zip` 集中到 `gpp.build`；
   - Qt 选取顺序：`qt_root` 为空时交给 rules-qt 的默认顺序；
   - 迁入 PR2 的自动部署，删除 `Release.py`；
   - `how-to-build.md` 以 `> 注:` 并列两种模式；
   - 在 Windows CI 上实测。
   - 需要与维护者确认：Qt 插件与 `qt_zh_CN` 是否由构建部署（上游当前明确置空），以及默认采用哪种模式。
2. **plugins 0.16（S3 前提）**：实现 P1 与 P2，并新增 fixture：只使用本机 vcpkg/CMake 时不下载 xim 工具；无设备源的包在有 SDK 时保持静默。
3. **mcpp**：S4 所需的修复均已有 issue（#705、#707、#710、#711、#712、#716），无需新提。可以补一条观察：一个宿主模块包含多个模块单元时，需要手动把无内部依赖的模块指定为 `lib.path`（见上游 `gpp-build/mcpp.toml` 的注释）。它是否提成 issue，待 S2 实测后决定。
