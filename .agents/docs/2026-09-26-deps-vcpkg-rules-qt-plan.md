# 实施计划：`deps-vcpkg`、`rules-qt`、`deps-cmake` 的跨仓库交付

状态：执行中 · 2026-09-26 · 设计见 `2026-09-26-deps-vcpkg-rules-qt-design.md`（第 4 版）。

## 1. 第 3 轮决定（设计第 4 版据此修改）

| # | 决定 | 对设计的影响 |
|---|---|---|
| D6 | 附加模块合为一个包 | 确认 `xim:qt-addons`；按 xim 的载荷私有规则，它有**自己的前缀**，不写入 `xim:qt` 的目录 |
| D8 | 先做 mcpp / xlings 生态的通用能力；GalTranslPP 的 D 阶段只在 fork 上做初步验证 | 引擎需求 2 在 mcpp 中实现：构建程序声明运行时库目录 |
| D9 | 免装 VS 不是目标 | 删除 `toolchain::msvc_xim`；`xim:msvc` 由 mcpp 自动匹配，插件不声明 |

另有一项由实测带来的简化：vcpkg-tool 每个发布附带 `vcpkg-standalone-bundle.tar.gz`（3.4 MB，脚本与该工具
版本严格对应，`vcpkg-bundle.json` 含 `"usegitregistry": true`）。以它为 `VCPKG_ROOT` 时，`builtin-baseline`
清单经 git registry 解析，registry 落在 vcpkg 的每用户 registries 缓存。已在本机实测：对 GalTranslPP 的
baseline `ea1a7396` 执行 `vcpkg install --dry-run` 解析出 `fmt 12.2.0`，缓存 137 MB。因此设计第 3 版 §3.6 的托管克隆、
按工具检出与文件锁全部取消。

## 2. 仓库与交付物

| 仓库 | 单个 PR 的内容 | 发布 |
|---|---|---|
| mcpp-community/mcpp | `mcpp::runtime_library_dir()` 与 check stamp 前移（#701、#702，CI 除 main 上既有的 xcode-27 失败外全绿） | **暂不合入**（2026-09-26 决定）；插件不依赖它 |
| openxlings/xim-pkgindex | `xim:vcpkg` 2026.7.27（工具 + standalone bundle）、`xim:qt` 6.11.1、`xim:qt-addons` 6.11.1；`xim:7zip` 说明 `7z.dll` 位置；测试 | 合入即发布索引；`xlings-res/vcpkg` 资源用 `gh` 与 `gtc` 双端上传 |
| mcpp-community/mcpp-plugins | `0.13.0`：`deps-vcpkg` + `mcpp-vcpkg`、`rules-qt` + `rules-qt-xim` + `rules-qt-xim-addons`、`deps-cmake` + `mcpp-cmake`；fixtures；CI 引擎版本；README | tag `v0.13.0`，GitHub release，`gtc` 上传 `mcpp-res/mcpp-plugins` |
| mcpp-community/mcpp-index | 登记 `mcpp:plugins 0.13.0` | 合入即发布 |
| Sunrisepeak/GalTranslPP（fork） | 临时 PR 2：迁移 A–C，D 阶段初步验证 | 不合入 |

## 3. 依赖关系

```
T2 xim 包（vcpkg/qt/qt-addons）──► T2r 索引合入 ──┐
T3 插件实现（以 mcpp 2026.9.26.1 为下界）──────────┴──► T3c 插件 CI 全绿 ──► T3r 插件发布 0.13.0 ──► T4 mcpp-index ──► T5 GalTranslPP PR 2
T1 引擎 #702（暂不合入；合入后插件后续版本改用 runtime_library_dir）
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
| T0 设计第 4 版、本计划 | 进行中 |
| T1 引擎 | 未开始 |
| T2 xim 包 | Qt 归档 sha256 计算中 |
| T3 插件 | 未开始 |
| T4 / T5 | 未开始 |
