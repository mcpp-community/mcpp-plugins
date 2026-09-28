# 通用构建插件设计：`deps-vcpkg`、`rules-qt`、`deps-cmake` 及其 xlings 依赖

状态：第 4 版（实现版；§0.1 的更正块取代正文中与之冲突的内容），实施中 · 2026-09-26 · 实施计划见 `2026-09-26-deps-vcpkg-rules-qt-plan.md` ·
基于 mcpp 2026.9.25.1、mcpp-plugins 0.12.0（`origin/main` dea1f09）、xim-pkgindex `origin/main`、验证工程上游
`main` 2c7d6bf（2026-09-25）· 起因：在验证工程（mcpp 工作区 + vcpkg + Qt 6，Windows/MSVC）上调查
mcpp-language-server issue 23 时，整理出一批**与该项目无关、任何 mcpp 项目都能复用**的构建能力。§1–§6、§8 只设计这批
通用能力；§7 用验证工程做真实案例，核算"插件都做完以后，一个真实项目迁移要花多少、能省多少"。

标注约定：**【已核实】** 有出处（文档章节、索引文件、发布资产、CI 实测、源码计数）；**【待验证】** 是设计依赖但尚未
实测的假设，全部列入 §9 Phase 0。

## 0. 摘要与已定决策

| 交付物 | 放在哪 | 服务对象 | 状态 |
|---|---|---|---|
| `deps-vcpkg`（新的 `deps-*` 家族） | mcpp-plugins | 任何用 vcpkg manifest 模式的 mcpp 项目 | 新增 |
| `rules-qt`（+ `rules-qt-xim`、`rules-qt-xim-addons`） | mcpp-plugins | 任何 Qt 6 项目（Qt 5 以后再支持） | 新增 |
| `deps-cmake` | mcpp-plugins | 任何自带 CMake 子项目（submodule、vendored）的 mcpp 项目 | 新增，第二批 |
| `mcpp-vcpkg`、`mcpp-cmake` 启动器 | mcpp-plugins `[targets.*]` | 上面两个插件的 action 命令 | 新增 |
| `xim:vcpkg` | xim-pkgindex | **只含 vcpkg 工具本体** | 新增 |
| vcpkg registry 共享克隆 | vcpkg 的每用户目录（与其默认二进制缓存同级） | 所有项目共享，与工具独立 | 由 `mcpp-vcpkg` 管理，见 §3.6 |
| `xim:qt` + `xim:qt-addons` | xim-pkgindex | Qt 6 SDK：官方基础包 + **全部附加模块合为一个包** | 新增，首个版本 6.11.1 |
| `xim:cmake`/`ninja`/`git`/`7zip`/`msvc`/`python`/`wix` | xim-pkgindex | 工具与运行时 | 已有 |
| 引擎侧需求 4 项 | mcpp | 让插件"只描述、不做重活"成立 | 提给上游 |

已定决策：

| # | 轮次 | 问题 | 决定 |
|---|---|---|---|
| D1 | 1 | 新开 `deps-*` 家族 | 接受 |
| D2 | 1 | `xim:vcpkg` 的 registry 与体积 | registry 与工具**独立、可复用**：`xim:vcpkg` 只装工具；registry 每台机器一份，所有项目共享（§3.6） |
| D3 | 1 | `xim:qt` 的粒度 | 按预构建包的惯例，即 Qt 官方安装器的分包；首个版本 6.11.1（§6.2） |
| D4 | 1 | Qt 5 | 本期不做，记为后续（§10） |
| D5 | 1 | 7z 运行时 | 按行业惯例：Windows 把官方 `7z.dll` 放在 exe 旁边；Linux/macOS 用发行版 p7zip 的 `7z.so`（§6.3） |
| D6 | 2 | 附加模块能否做成一个包 | **能，建议做成一个包 `xim:qt-addons`**（依据与代价见 §6.2；待确认，§10-1） |
| D7 | 2 | registry 共享克隆放哪 | vcpkg 的每用户目录（§3.6）；第 4 版起不再有托管克隆，见 §0.1 |
| D6′ | 3 | D6 确认 | `xim:qt-addons` 为一个包，**自有前缀**（xim 载荷私有），`rules-qt` 同时加入两个前缀 |
| D8 | 3 | 生态通用能力先行 | 通用缺口由 mcpp 以通用机制补上（SPEC-007，mcpp#702）；插件按 SPEC-007 实现并等待该发布（§0.1-5）；验证工程的 D 阶段在 fork 上初步验证 |
| D9 | 3 | 免装 VS | 不是目标；删除 `toolchain::msvc_xim`，`xim:msvc` 由 mcpp 自动匹配 |

### 0.1 第 4 版更正（实现时的决定，取代正文中冲突的部分）

1. **registry 不再由插件管理**（取代 §3.6 的托管克隆、按工具检出与文件锁）。vcpkg-tool 每个发布附带
   `vcpkg-standalone-bundle.tar.gz`（3.4 MB），其 `vcpkg-bundle.json` 为 `{"deployment":"OneLiner","usegitregistry":true}`，
   脚本与该工具版本严格对应。`xim:vcpkg` = 工具 + 该 bundle，安装目录即 `VCPKG_ROOT`。【已核实】本机以此为根，对
  验证工程的 baseline 执行 `vcpkg install --dry-run`，输出 "Fetching registry information from
   https://github.com/microsoft/vcpkg"，解析出 `fmt 12.2.0`，registry 缓存 137 MB。继承的 `VCPKG_ROOT` 不使用，
   `options::vcpkg_root` 可显式指定。
2. **启动器合为一个 `mcpp-deps`**（取代 `mcpp-vcpkg`、`mcpp-cmake`），子命令 `vcpkg`、`cmake`；消费者写
   `tools = ["mcpp-deps"]`。它设置 `VCPKG_ROOT`、`VCPKG_DISABLE_METRICS`，对安装根加排他锁，把 buildtrees/packages
   放在 `<vcpkg 每用户目录>/mcpp/<安装根的 FNV 哈希>/`、downloads 放在 `<vcpkg 每用户目录>/downloads`（未设
   `VCPKG_DOWNLOADS` 时）。`check` 动作的 stamp 由 mcpp 在命令成功后写入（docs/30，2026.8.29.1+），启动器不写。
3. **不声明 cmake/ninja/7zip/git，也不设 `VCPKG_FORCE_SYSTEM_BINARIES`**（取代 §3.4）。vcpkg 自取经测试的
   CMake 3.x、Ninja、7-Zip（Windows 另取便携 git）；强制使用 `xim:cmake` 4.x 会让 `cmake_minimum_required` 低于
   3.5 的 port 失败。`xim:git` 没有 macOS 版本、Linux 只有 x86_64，无法作为跨平台依赖。Linux/macOS 的 git、curl、
   zip、unzip、tar 与 C 编译器按 vcpkg 文档属于主机前提。
4. **工具链选项 v1 只保留 vcpkg 默认**（取代 §3.3(4) 的三选项表）。自定义工具链经项目自己的 overlay triplet
   （`VCPKG_CHAINLOAD_TOOLCHAIN_FILE`）表达，插件不生成。
5. **按 mcpp SPEC-007 实现，等待 mcpp#702 所在的发布**（取代 §3.3(3)、§3.3(1) 的 `check` 角色与本条的早先写法）。
   mcpp 侧 2026-09-26 定下 SPEC-007（`docs/specs/build-plugins.md`，草案 v0.1）及 #702 的合规设计：
   - 运行时目录：`mcpp::runtime_search_dir(dir)`（协议 12，写入 `LinkIntent::runtimeSearchDirs`）。三个成员都用它声明
     前缀或 SDK 的共享库目录；ELF/Mach-O 的运行路径、`mcpp run`、`mcpp pack` 与运行时校验都由引擎完成；Windows 上
     引擎在链接后把程序导入的 DLL 放到程序旁（R4.3）。插件不写 `-Wl,-rpath`（R4.4），不逐个 deploy DLL，不以
     `--no-as-needed` 预加载。
   - 施工：安装与 `lupdate` 用 `mcpp::roles::prepare` 加 `output_dir`（R3.3、R3.4）；stamp 由引擎创建并更新（R3.5）。
   - 规划：构建程序只按名字引用 prepare 目录，不读其中内容、不以其中文件为重运行依据（R1.3）；缺 SDK、缺模块、缺工具
     都只是 warning（R1.2）。
   此前的两个中间方案（依赖本会话 #702 的 `runtime_library_dir`；以及只用已发布引擎的 deploy/`--no-as-needed` 变通）
   都已撤下。插件 PR 在 #702 所在的 mcpp 发布之后合入与发布；本地以映射到 #702 现有实现的临时桩验证了全部 Linux 判据。
6. **工作区**（§3.3(5)）：每个调用 `use()` 的成员各声明一条安装边；【已核实】引擎 `blocking` 只排序本包的编译边
   （`src/build/ninja_backend.cppm`）。并发由启动器的安装根锁串行化，不依赖 vcpkg 自身的锁。
7. **依赖方的 deploy 会进入可执行文件布局**（关闭 §4.3 的【待验证】）：【已核实】`deploy` 与 `link-*` 同属
   LinkIntent，由消费者的 `resolve_runtime_contract` 合并（`src/build/plan.cppm`）。库成员生成的 `.qm` 无需可执行成员重复声明。
8. **`rules-qt` 的平台细节**：Linux/macOS 上 `moc`/`uic`/`rcc` 在 `libexec/`；macOS 的模块是 framework，以
   `-F<root>/lib` 编译、以 framework 内二进制的完整路径链接；Linux 加 `-fPIC`（Qt 以 `-reduce-relocations` 构建）；
   Windows 的插件目录跳过 `*d.dll` 调试副本。
9. **`deps-cmake` 使用 CMake 默认工具链与生成器**（取代 §5 的"生成 toolchain file"）：与子项目作者的构建方式一致；
   项目可经 `cache_args` 指定 `-G`、编译器或 toolchain file。
10. **fixture 规则**：CI 中 `all-rules-compile` 的成员检查由"名单相等"改为"每个带 sources 的成员都被列出，列出的
    名字都已声明"。只声明载荷的 feature（`rules-qt-xim`、`dist-apk-kotlin`）不再强制列入，避免在三个平台下载 Qt。

## 1. 原则

1. **只做通用、可复用的**。插件里不出现任何项目知识，项目差异全部走参数。与 `tools/README.md` 的准入一致："A utility
   written for a single consumer belongs in that consumer's `build.mcpp`; a member here is one that more than one
   consumer needs."
2. **插件拥有它驱动的包列表**。插件编译期要用的 xim 包由插件在 `[target.<cfg>.feature-xlings.<feature>]` 声明，
   项目不重复写；**程序运行时要的东西由项目声明**（mcpp.toml 中 "A RULE OWNS THE LIST OF PACKAGES IT DRIVES" /
   "WHAT IS NOT HERE" 两段）。
3. **构建程序只描述，重活全部是 action**。【已核实】`mcpp emit build-database` 执行构建程序且无"规划模式"信号
   （mcpp `docs/specs/build-database.md` R2.5；`docs/30` 环境契约表中无此变量）；构建程序有 600 s 上限
   （`docs/30` "Current limitations"）；`mcpp::action` 是 ninja 边，只在 `mcpp build` 时执行，不受该上限约束
   （`docs/30` "Declaring work instead of doing it"）。
4. **缺环境不失败**。构建程序遇到缺失的前缀/工具时输出确定的路径并 `mcpp::warning`，真正的失败留给 action 边——
   这样 IDE（mcppls 通过 emit 取模型）在一台尚未安装依赖的机器上也能拿到可信模型。反例是验证工程现状：构建
   程序缺 Qt 或缺一个 vcpkg `.lib` 就 `return 1`，emit 整体失败（CI 实测 `MCPP_BUILD_DATABASE_PLAN_FAILED`）。
5. **不写别人的东西**。插件只写 `out_dir`、自己管理的目录（§3.6 的托管克隆）和用户显式要求写的位置；用户已有的
   vcpkg 克隆、项目源码树默认只读（lupdate 见 §4.3）。
6. **前缀即含义**。沿用 README 的分类并新增一类，见 §2。

## 2. 新家族 `deps-*`（D1）

现有三类各回答一个问题（README "Naming"）：`rules-*` 某类翻译单元怎么编译；`tools-*` 构建程序自己要做什么；
`dist-*` 链接产物以什么形式交付。**"一个库从哪里来"是第四个问题**，它：

- 不编译项目的翻译单元（不是 rule），
- 重活不能在构建程序里做（`tools/README.md`："A tool … does it while `build.mcpp` runs"——vcpkg 安装恰恰不能），
- 产物是头文件、库和运行时 DLL，被编译和链接消费（不是 dist）。

因此新增 `deps-*`：`deps-vcpkg`、`deps-cmake`，以后 `deps-conan`、`deps-prefix` 同属此类。形态沿用 `tools-*`/`dist-*`：
无 `rule_module`/`device_extensions`，消费者写 `host-module = true` 并按名 import。落地时 README 的 "Naming" 表与
`mcpp.toml` 的分类注释同步加一行。

## 3. `deps-vcpkg`

### 3.1 范围

- 做：vcpkg **manifest 模式**（项目根有 `vcpkg.json`）；安装作为构建图的一条边；把已安装前缀映射为 include、link、
  运行时 DLL；向项目暴露前缀路径（数据文件如 `share/opencc` 由项目自己 deploy）；二进制缓存；共享 registry（§3.6）；
  让 vcpkg 用 xlings 提供的 cmake/ninja/git；可选地让 vcpkg 用 mcpp 解析出的工具链编库。
- 不做：classic 模式；替代 vcpkg 的版本解析；为头文件生成 C++ 模块接口（仍是项目或 index 包的事）。

### 3.2 消费者写法

```toml
[build-dependencies.mcpp]
plugins = { version = "0.13.0", features = ["deps-vcpkg"], host-module = true, tools = ["mcpp-vcpkg"] }
```

```cpp
// build.mcpp
import std; import mcpp; import mcpp.deps.vcpkg;
int main() {
    mcpp::deps::vcpkg::options o;
    o.triplet   = "x64-windows";                  // 自定义 triplet 由 vcpkg-configuration.json 的 overlay 指定
    o.libraries = { "fmt", "spdlog" };            // 显式、可 review；见 3.3(2)
    o.toolchain = mcpp::deps::vcpkg::toolchain::vcpkg_default;   // 见 3.3(4)
    const auto prefix = mcpp::deps::vcpkg::use(o);  // 缺前缀时仍返回确定路径并 warning，见原则 4
    // 数据文件由项目按需 deploy，例如 prefix / "share/opencc"
    return prefix.empty() ? 1 : 0;                  // 只有参数错误才为空
}
```

### 3.3 机制

**(1) 安装边**：一个 `mcpp::action`，`role = "check"`、`blocking = true`（【已核实】`docs/30` 角色表："blocking = true
makes the package's compile edges wait for it"）。

- 命令：`mcpp-vcpkg install --root <项目根> --triplet <t> …`，经 `mcpp::dep_bin("plugins", "mcpp-vcpkg")` 取得（沿用
  `mcpp-embed` 的方式：`[targets.mcpp-vcpkg]` + 消费者 `tools = ["mcpp-vcpkg"]`，【已核实】`src/declare.cppm`）。
- 为什么要启动器：【已核实】`mcpp::action` 没有设置环境变量的接口（`docs/30` 只有 `rerun-if-env-changed`），而 vcpkg
  需要 `VCPKG_ROOT`（§3.6）、`VCPKG_BINARY_SOURCES`、`VCPKG_FORCE_SYSTEM_BINARIES`、`VCPKG_DISABLE_METRICS` 和指向
  xlings 工具的 `PATH`。启动器是普通程序，设置环境后启动 vcpkg 并等待它结束（期间持有 §3.6 的共享锁），不依赖 shell（与 `dist-web` 用
  `${mcpp.self} stage` 代替 `cp` 的理由相同）。若引擎将来提供 `action.env(...)`（§8 需求 4），启动器仍保留 registry
  管理（§3.6），只是不再承担环境设置。
- 目录隔离：`--x-buildtrees-root`、`--x-packages-root` 指向项目自己的 `out_dir`，`downloads/` 共享。共享克隆的默认
  buildtrees/packages 在克隆内，两个项目同时编同一个 port 会互相覆盖；按项目隔离后与 vcpkg 自身的锁语义无关，
  一定安全。（验证工程的 CI 已经这样用 `--x-buildtrees-root`。）
- 输入：`vcpkg.json`、`vcpkg-configuration.json`、overlay ports/triplets 目录（`input()` + `rerun_if_changed_glob`）。
  输出：stamp。只在这些输入变化时重跑；emit 永不执行；不受 600 s 限制。
- 二进制缓存：**不覆盖，用 vcpkg 的默认位置**（【已核实】vcpkg 文档 "Default binary cache"：Windows
  `%LOCALAPPDATA%\vcpkg\archives`，其他 `$XDG_CACHE_HOME/vcpkg/archives` 或 `~/.cache/vcpkg/archives`）。这样用户
  已有的缓存直接复用，与 D7 的目录选择一致；CI 用 `VCPKG_BINARY_SOURCES` / `VCPKG_DEFAULT_BINARY_CACHE` 覆盖即可。

**(2) 映射（构建程序内，只读、保证成功）**

- `include_dir(<install_root>/<triplet>/include)`：路径在安装前即可确定。`install_root` 默认 `<manifest 根>/vcpkg_installed`
  （vcpkg 的默认值，已有项目的目录布局不变）。
- 链接：以 `options.libraries` 为准，**一律用确定的完整路径** `mcpp::link_flag(<prefix>/lib/<name>.lib)`（ELF/Mach-O 上
  `lib<name>.a/.so/.dylib`）。链接边排在编译边之后、编译边又等待安装边，所以链接时文件必然已存在，不必在规划期判断
  存在与否。用完整路径的理由验证工程的 `vcpkg_link.hpp` 写过："An exact file path cannot resolve to a same-named
  system library"。之所以要显式列表：首次构建时构建程序先于安装边运行，vcpkg 的 `info/*.list` 尚不存在。
- 校验：`info/*.list` 存在时，比对列表与实际产物，不一致给 `mcpp::warning`；`rerun_if_changed(stamp)` 让安装完成后
  下一次构建程序按实际结果重新校验。
- 缺失：前缀不存在时不 `return 1`，只 warning（原则 4）。IDE 在首次构建前能拿到正确的 include 路径，但头文件尚不存在；
  mcppls 可据此提示"先运行一次 `mcpp build`"。

**(3) 运行时（动态 triplet）**：DLL 在 `<prefix>/bin`。`mcpp run`/`mcpp test` 要能找到它们，`mcpp pack` 要把它们
收进闭包。【待验证】PE 下 pack 的闭包搜索目录是否包含构建程序声明的目录（`docs/10` 只写清了 Android 行：输出目录、
各包 `[runtime] library_dirs`/`link_library_dirs`、编译器搜索路径）。过渡方案：插件对 `info/*.list` 中的每个 DLL
调 `mcpp::deploy` 放到 exe 旁边（run 与 pack 都成立）；根本方案见 §8 需求 2。

**(4) 工具链**：三个选项，决定"vcpkg 用什么编译器编 port"。

| 选项 | 编译器 | 需要 VS 实例 | port 兼容性 | 用途 |
|---|---|---|---|---|
| `vcpkg_default`（默认） | vcpkg 自己找的 cl | **需要**（vcpkg 用 vswhere 找 VS 并加载 vcvars） | 最好 | 已装 VS 的机器、CI runner |
| `msvc_xim` | `xim:msvc` 的 cl，经 chainload | 不需要 | 与默认相同（同一个编译器） | **免装 VS** |
| `project` | `mcpp::toolchain_dir()`（如 LLVM clang），经 chainload | 不需要 | 个别 port 不支持 clang | 全链路同一编译器 |

- 【已核实】vcpkg 文档 `VCPKG_LOAD_VCVARS_ENV`："By default, this is ON for Windows triplets that do not specify
  `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` … triplets specifying `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`, this defaults to OFF"。
  所以**只有 chainload 路线能做到免装 VS**；`xim:msvc` 只解决了 mcpp 自己的编译，vcpkg 默认路线仍要 VS。
- 后两项由插件在 `out_dir` 生成 overlay triplet 与 chainload 工具链文件。【已核实】vcpkg 在 Windows 上用干净环境编
  port，`INCLUDE`/`LIB` 要经 `VCPKG_ENV_PASSTHROUGH` 传入（文档同页）；或者工具链文件直接写 `/I`、`/LIBPATH`。
  哪种可行【待验证】。
- **项目自带自定义 triplet 时**（验证工程的 `gpp-x64-windows-release` 就是）：生成的 triplet 先 `include()` 项目
  triplet，再追加 chainload 设置，不替换它；triplet 名随之变为 `<原名>-mcpp-<选项>`，安装目录、映射一律按新名。
- 同时检查 triplet 的 `VCPKG_CRT_LINKAGE` 与项目运行库（如 `-fms-runtime-lib=dll`）是否一致，不一致则 warning。

**(5) 工作区**：`blocking` 的顺序是"per package"（【已核实】`docs/30`），被依赖成员的安装边管不到不依赖它的成员
（验证工程的 `Updater` 不依赖 core，却要链接 `bit7z`，§7.1）。默认做法：**每个调用 `use()` 的成员各声明一条
安装边**，命令相同、stamp 各在自己的 `out_dir`。第一条真正安装，其余的 `vcpkg install` 发现已安装、几秒内结束；
并发时由 vcpkg 对安装目录加的锁串行化【待验证：manifest 模式对 `vcpkg_installed` 的锁】。锁不成立时退回"同一个
stamp 由每个成员声明为 `input()`、只由一个成员声明为 `output()`"；根本方案是 §8 需求 3。

### 3.4 插件声明的 xlings 依赖

```toml
[target.windows.feature-xlings.deps-vcpkg]
"xim:vcpkg" = ">=2026.7.27"   # 只含工具，见 §3.6、§6.1
"xim:cmake" = ">=3.31"        # 【已核实】xim 有 cmake 4.4.2（linux/macosx/windows）
"xim:ninja" = ">=1.12"        # 【已核实】ninja 1.12.1
"xim:git"   = ">=2.40"        # 【已核实】git 2.51.1（windows）/ 2.53.0（linux）；registry 管理要用
"xim:7zip"  = ">=26.02"       # 【已核实】7zip 26.02，三平台；vcpkg 解包用
[target.'cfg(any(linux, macos))'.feature-xlings.deps-vcpkg]
"xim:vcpkg" = ">=2026.7.27"
"xim:cmake" = ">=3.31"
"xim:ninja" = ">=1.12"
"xim:git"   = ">=2.40"
```

用 floor（`>=`）而不是精确版本：这些工具与项目没有 ABI 耦合（沿用 mcpp.toml 中"THE SHAPE OF EACH DEFAULT IS A
JUDGEMENT ABOUT COUPLING"的判断）。`xim:msvc` 不在这里：只有选了 `toolchain::msvc_xim` 的项目才要，由项目的
`sysroot = "xim:msvc@…"` 带入。【待验证】`VCPKG_FORCE_SYSTEM_BINARIES=1` 在 Windows 上会让 vcpkg 使用 PATH 中的
cmake/ninja/7zip，而不是自行下载到 `downloads/tools`。【待验证】Linux 上 vcpkg 还要求 `curl`、`zip`、`unzip`、`tar`、
`pkg-config`：优先来自 xim（`xim:curl` 已有，其余待查），缺的由主机提供并在文档中写明。

### 3.5 测试（fixtures，放入现有 CI 矩阵）

| fixture | 平台 | 断言 |
|---|---|---|
| `vcpkg-consumer`（fmt，动态 triplet） | windows-2022、ubuntu-24.04 | 构建、运行；`mcpp pack` 的 zip 含 `fmt.dll` |
| `vcpkg-consumer-static` | 同上 | 静态 triplet，无 DLL |
| `vcpkg-consumer-msvc-xim` | windows-2022 | `toolchain::msvc_xim` 下 port 由 `xim:msvc` 的 cl 编译，构建日志中不出现 vcvars |
| `vcpkg-consumer-project-toolchain` | windows-2022 | `toolchain::project` 下 port 由 mcpp 的 clang 编译；自定义 triplet 被 include 而非替换 |
| `vcpkg-emit-safe` | 同上 | 无 `vcpkg_installed` 时 `mcpp emit build-database` 返回 0，include 路径已在库中 |
| `vcpkg-shared-registry` | 同上 | 两个 baseline 不同的项目共用一份托管克隆，第二个项目只 `git fetch` 不重新克隆；两者并发构建不冲突 |
| `vcpkg-user-root` | 同上 | 设置了 `VCPKG_ROOT` 时直接使用，且构建前后该克隆的 `git status`、refs 不变 |
| `vcpkg-workspace` | 同上 | 两个互不依赖的成员都 `use()`，只安装一次，两者都能链接 |
| 二次构建 | 同上 | 输入未变时安装边不重跑 |

### 3.6 registry：与工具独立、每台机器一份（D2、D7）

【已核实】vcpkg 官方就把两者分开发布：工具来自 microsoft/vcpkg-tool 的发布资产（2026-07-27：`vcpkg.exe`、
`vcpkg-arm64.exe`、`vcpkg-glibc`、`vcpkg-glibc-arm64`、`vcpkg-muslc`、`vcpkg-macos`，单文件），registry 是
microsoft/vcpkg 仓库（GitHub 统计约 111 MB）。因此 **`xim:vcpkg` 只装工具**；registry 由 `mcpp-vcpkg` 按项目的两种
manifest 写法处理：

| 项目写法 | vcpkg 的要求 | 本设计的做法 |
|---|---|---|
| `vcpkg-configuration.json` 的 `default-registry` 为 `git` 类型（带 `baseline`） | 工具自己把 registry 取到它的每用户 registries 缓存，所有项目共享 | 什么都不用做（`X_VCPKG_REGISTRIES_CACHE` 可覆盖）【待验证：各平台默认路径】 |
| `vcpkg.json` 的 `builtin-baseline`（验证工程就是这种） | `VCPKG_ROOT` 必须是含该 baseline 提交的 microsoft/vcpkg 克隆 | 见下 |

`builtin-baseline` 的 `VCPKG_ROOT` 按顺序选：

1. **用户已有的克隆**：`VCPKG_ROOT` 已设置、是 microsoft/vcpkg 克隆、且含 baseline 提交（`git cat-file -e`）时直接用，
   **只读**（原则 5）：不 fetch、不 checkout，buildtrees/packages 已按 §3.3(1) 移出。缺提交时不碰它，落到第 2 项，
   并在 warning 里写明原因。
2. **托管克隆**（D7）：放在 vcpkg 的每用户目录下，与它的默认二进制缓存 `archives/` 同级——Windows
   `%LOCALAPPDATA%\vcpkg\mcpp-registry`，其他 `$XDG_CACHE_HOME/vcpkg/mcpp-registry` 或 `~/.cache/vcpkg/mcpp-registry`。
   第一次完整克隆（不能浅克隆：vcpkg 要从 git 对象读取 baseline 时刻的版本数据库和 port 树）；之后缺 baseline 时只
   `git fetch`。目录名带 `mcpp-`，表明它归 mcpp 管，不与用户自己放在这里的东西冲突。它在 cache 目录下，被清理后
   下次自动重建。

**托管克隆检出哪个版本**：port 的版本来自 baseline，但 `scripts/`（port 构建时调用的 CMake 函数）永远来自工作区的
检出。所以检出必须**同时**满足：

- 与工具匹配：【已核实】registry 的每个提交都在 `scripts/vcpkg-tool-metadata.txt` 写明所需工具，例如发布标签
  `2026.07.29` 写的是 `VCPKG_TOOL_RELEASE_TAG=2026-07-27`，正是 `xim:vcpkg` 的首个版本。
- 不旧于项目 baseline：新 port 可能用到旧脚本里没有的函数。

做法：检出**上游主干上"所需工具不新于已装 `xim:vcpkg`"的最后一个提交**，即下一次"Release vcpkg-tool"提交的前一个
（只需遍历改过 `vcpkg-tool-metadata.txt` 的少数提交）。项目 baseline 自身要求的工具比已装的新时报错，写明 baseline、
它要求的工具、已装工具三个版本，提示升级 `xim:vcpkg`；否则 baseline 一定在这个检出之前，两条都满足。这样**工具版本
决定脚本版本、baseline 决定 port 版本**，不会因为上游 registry 前进而让所有项目一起失败（第 2 版的问题，见 §11）。

不按发布标签检出：【已核实】验证工程的 baseline `ea1a7396`（2026-08-08）比最新标签 `2026.07.29` 晚 129 个提交，
但要求的工具同样是 2026-07-27。按标签检出会把一个完全兼容的项目判为"baseline 太新"。项目在两个标签之间选 baseline
很常见。

- 共用一份克隆的前提：带版本的 builtin registry 从 git 对象读取 baseline 时刻的 port 版本，不同 baseline 的项目
  可以共用一份检出。【待验证】
- 并发：`mcpp-vcpkg` 对托管克隆加文件锁——`fetch`/`checkout` 持排他锁，`vcpkg install` 持共享锁，所以正在编的项目
  不会看到脚本在中途被换掉。

## 4. `rules-qt`（+ `rules-qt-xim`、`rules-qt-xim-addons`）

### 4.1 为什么是一个 rule、为什么通用

所有 Qt 6 项目都要：moc（含 `Q_OBJECT`/`Q_GADGET`/`Q_NAMESPACE` 的头文件 → `moc_*.cpp`）、uic（`.ui` → `ui_*.h`）、
rcc（`.qrc` → `qrc_*.cpp`）、lrelease（`.ts` → `.qm`，可选 lupdate）、链接所用模块、部署运行时。符合 `rules-*` 的定义
"how one kind of translation unit is compiled by a compiler mcpp does not drive"；`rules-swift` 也负责给自身运行时加
链接搜索目录，是先例（README `rules-swift` 行）。

部署是"通用"的关键理由之一：Qt 插件（`platforms/qwindows.dll`、`styles/`、`imageformats/`）由 `QPluginLoader` 在运行时
加载，**不在 PE 导入表里**，`mcpp pack` 按导入表收集 DLL（【已核实】`docs/10` "Windows (PE)"）收不到——这正是
`windeployqt` 存在的原因，每个 Qt 项目都要解决。

### 4.2 声明与写法

```toml
[features.rules-qt]
sources           = ["rules/qt.cppm"]
implies           = ["surface"]
rule_module       = "mcpp.rules.qt"
device_extensions = [".ui", ".qrc", ".ts"]

# Qt 从哪来与规则无关：用 vcpkg 的 qtbase 或本机 Qt 的项目不应下载 SDK。
# 沿用 dist-apk-kotlin 的理由："provisioning runs before the build program learns what it will compile"。
[features.rules-qt-xim]
implies = ["rules-qt"]

[target.'cfg(any(windows, linux, macos))'.feature-xlings.rules-qt-xim]
"xim:qt" = "6.11.1"     # 精确版本：moc 输出与 Qt 库版本耦合；项目可改选其他版本

# 用到附加模块（Multimedia、Charts…）的项目再开这一项；只用 qtbase/qtdeclarative 的项目不下载。
[features.rules-qt-xim-addons]
implies = ["rules-qt-xim"]

[target.'cfg(any(windows, linux, macos))'.feature-xlings.rules-qt-xim-addons]
"xim:qt-addons" = "6.11.1"   # 必须与 xim:qt 同版本，§4.3 校验
```

`.ts` 同时是 TypeScript 的扩展名；不会误判，因为 feature 必须按名激活（mcpp.toml "The feature is still requested BY
NAME"），且只对项目 `sources` 中的文件生效。

```cpp
import mcpp.rules.qt;
int main() {
    mcpp::rules::qt::options o;
    o.modules        = { "Core", "Gui", "Widgets", "Network" };
    o.moc            = mcpp::rules::qt::moc_scan::project_headers;
    o.translations   = { .ts = { "i18n/app_en.ts" }, .tr_function_alias = { "translate+=appTr" } };
    o.deploy_plugins = { "platforms", "styles", "imageformats" };
    return mcpp::rules::qt::compile(o) ? 0 : 1;
}
```

无 `build.mcpp` 时（零配置）：处理 sources 里的 `.ui`/`.qrc`/`.ts`，扫描项目头文件跑 moc，只链接 `Core`，部署
`platforms`——够跑一个最小程序；其余都要写选项。

### 4.3 机制

- **Qt 根目录**：`options.root` > `mcpp::xpkg_dir("xim", "qt")`（【已核实】`docs/30` "Finding an `[xlings.workspace]`
  payload"，且对依赖方声明的包同样有效）> 未找到时 warning（原则 4）。探测 `bin/moc`、`uic`、`rcc`、`lrelease`。
  `xim:qt-addons` 装进同一个前缀（§6.2），规则不需要第二个根目录；模块不在前缀里时报错并提示开
  `rules-qt-xim-addons`。所用附加模块的 `lib/cmake/Qt6<Module>/Qt6<Module>ConfigVersion.cmake` 与 `lib/cmake/Qt6/Qt6ConfigVersion.cmake` 版本不一致时报错（项目只改了其中一个包的版本）。
- **moc**：按内容扫描项目头文件（`rerun_if_changed_glob` 声明输入），每个一条 `role = "source"` action，带 depfile
  （`moc --output-dep-file`，【待验证】版本下限）；`.cpp` 内含 `Q_OBJECT` 时生成 `<name>.moc` 头并 `include_dir(out_dir)`。
- **uic / rcc**：`role = "source"`；rcc 的依赖用 `rcc --list` 在规划期取得资源清单作为 `input()`，或 depfile【待验证】。
- **lrelease**：`.ts` → `.qm`，`role = "source"`（产物不参与编译），再 `mcpp::deploy` 到 `translations/`。
- **lupdate**：会**改写源码树里的 `.ts`**，默认关闭；`o.translations.update_sources = true` 时作为 `role = "check"`、
  `blocking = true` 的 action，lrelease 依赖它的 stamp（验证工程现行做法，§7.2）。与 emit "never writes into the
  project tree"（specs R2.1）的精神一致：只在显式要求的 build 中写。
- **链接**：`include_dir(<root>/include/Qt<Module>)`、`link_search(<root>/lib)`、`link_lib("Qt6<Module>")`、
  `QT_<MODULE>_LIB` 等定义；编译器族为 msvc/clang-cl 时加 `/Zc:__cplusplus`（Qt 6 对 MSVC 的要求；clang 驱动不需要）；
  Windows 下 GUI 子系统入口（`Qt6EntryPoint`）随 `windows_subsystem` 处理。`o.private_modules = { "Widgets" }` 加
  `include/Qt<Module>/<ver>`（ElaWidgetTools 这类库要 `WidgetsPrivate`，§7.2）。
- **部署**：模块 DLL 由 pack 的导入表闭包收集（前提同 §3.3(3)【待验证】）；插件目录用 `mcpp::deploy` 放到
  `platforms/` 等子目录（【已核实】构建程序的 `mcpp::deploy` 见 `docs/30` "Deploying what the program generated"，
  pack 将其放在 `bin/<to>/`，README `dist-apple` 行；清单侧 `[runtime] deploy` 的 `to` 语义见 `docs/04` §2.11）。
  `d3dcompiler_47.dll`、`opengl32sw.dll` 按 windeployqt 的默认行为可选部署（`o.deploy_software_gl`）。
- **库成员的翻译**：库成员（如验证工程core）生成的 `.qm` 要进可执行成员的 `translations/`。【待验证】依赖方
  构建程序的 `mcpp::deploy` 是否随依赖进入可执行文件的 run/pack 布局；不成立时由可执行成员的 `translations` 选项
  直接列出库的 `.ts`。

### 4.4 测试

`qt-consumer`（Widgets，含 `Q_OBJECT` 头、`.ui`、`.qrc`、`.ts`），windows-2022 与 ubuntu-24.04：构建、无显示环境下以
`QT_QPA_PLATFORM=offscreen` 运行；pack 的 zip 含 `platforms/qwindows.dll` 与 `translations/*.qm`；emit 在无 Qt 时返回 0；
`qt-consumer-zero-config` 验证无 `build.mcpp` 的路径；`qt-consumer-workspace` 验证库成员的 `.qm` 进入可执行文件布局；
`qt-consumer-addons` 链接一个附加模块（如 `WebSockets`）。

## 5. `deps-cmake`（第二批）

项目里常见"带一个 CMake 子项目"（submodule/vendored），今天只能靠外部脚本先编好（验证工程的 ElaWidgetTools
就是：一个 Python 脚本调 CMake，Qt 路径写死在脚本里，§7.1）。插件把 configure / build / install 作为三条 action
（`role = "check"` 链，最后一条 blocking），安装到 `out_dir` 下的前缀，再按 §3.3(2) 的方式映射。

```cpp
mcpp::deps::cmake::options o;
o.source     = "../3rdParty/ElaWidgetTools";
o.cache_args = { "-DQT_SDK_DIR=" + qt_root, "-DELAWIDGETTOOLS_BUILD_EXAMPLE=OFF" };
o.layout     = { .include = "ElaWidgetTools/include", .lib = "ElaWidgetTools/lib", .bin = "ElaWidgetTools/bin" };
o.libraries  = { "ElaWidgetTools" };
mcpp::deps::cmake::use(o);
```

- 工具链：生成 CMake toolchain file，编译器取 `mcpp::toolchain_dir()`，与项目一致；也可选 `msvc_xim`（同 §3.3(4)）。
- 依赖前缀：`options.prefix_path` 透传 `CMAKE_PREFIX_PATH`；与 `rules-qt` 组合时传 Qt 根目录（`mcpp::rules::qt::root()`）。
- 安装布局：默认 `include/`、`lib/`、`bin/`；子项目自有布局时用 `o.layout` 指定（ElaWidgetTools 装在
  `<prefix>/ElaWidgetTools/{include,lib,bin}`）。
- 输入：`rerun_if_changed_glob(<source>/**/CMakeLists.txt)` 与源码 glob；子项目源码变化时重编。
- xlings：`"xim:cmake" = ">=3.31"`、`"xim:ninja" = ">=1.12"`（插件声明）。
- 同 `deps-vcpkg`：库列表显式声明、链接用完整路径；缺前缀只 warning。

## 6. xlings 生态依赖

### 6.1 清单

| 包 | 状态 | 平台 | 本设计中的用途 | 要做的事 |
|---|---|---|---|---|
| `xim:vcpkg` | **缺** | win（x64、arm64）/ linux（glibc、musl、arm64）/ macos | vcpkg 工具本体 | 新增，只含 vcpkg-tool 单文件（§3.6） |
| `xim:qt` | **缺** | win（msvc2022_64、arm64）、linux、macos | Qt 6 基础包 | 新增，§6.2 |
| `xim:qt-addons` | **缺** | 同上 | Qt 6 全部附加模块 | 新增，§6.2 |
| `xim:cmake` / `ninja` / `git` | 已有 | 三平台 | vcpkg、deps-cmake 的构建工具；registry 管理 | 无 |
| `xim:7zip` | 已有（26.02；上游已发 26.03） | 三平台 | vcpkg 解包；Windows 上兼作 `7z.dll` 来源 | 在包说明中写明 `7z.dll` 的位置，§6.3 |
| `xim:msvc` + `windows-sdk` | 已有 | windows | 固定 MSVC 工具集；`toolchain::msvc_xim` 的编译器 | 无；项目写 `[target.x86_64-windows-msvc] sysroot = "xim:msvc@14.44.35207"`（mcpp 2026.9.24.1） |
| `xim:python` | 已有 | 三平台 | 嵌入 Python（pybind11 场景） | 【已核实】Windows 为 python-build-standalone `install_only`，有 3.13.12 与 3.12.13；是否含 `include/` 与 `libs/python3X.lib` 【待验证】 |
| `xim:wix` | 已有 | windows | `dist-wix` | 无 |

按原则 2：`xim:qt`/`xim:qt-addons` 由 `rules-qt-xim*` 声明、构建工具由 `deps-*` 声明；`7z.dll`、Python 运行时这类
"程序运行时要的"由项目在 `[xlings.workspace]` 声明（【已核实】`docs/23` 的 `when` 取值为 `build`/`run`/`dev`；构建和
运行都要的不写 `when`）。

### 6.2 `xim:qt` 与 `xim:qt-addons`（D3、D6）

- **`xim:qt`** = 官方基础包 `qt.qt6.<ver>.<arch>`。【已核实】6.11.1 / win64_msvc2022_64 的基础包含 8 个归档：qtbase、
  qtsvg、qtdeclarative、qtdoc、qttools、qttranslations、d3dcompiler_47、opengl32sw，合计压缩 245 MB、解压 2.26 GB。
  xim 安装除 `qtdoc` 以外的全部归档（文档对构建无用）。qtbase 已含 Core/Gui/Widgets/Network 以及 `qjpeg`/`qgif`/`qico`
  图像插件，大多数桌面程序只需要这一个包。
- **`xim:qt-addons`** = 全部附加模块 `qt.qt6.<ver>.addons.*.<arch>` 合为**一个包**，装进 `xim:qt` 的同一个前缀，
  依赖同版本的 `xim:qt`。

**为什么能做成一个包**（回答第 2 轮问题 1）：

| 依据 | 数据 |
|---|---|
| 官方安装器本来就有这个分组 | 【已核实】`Updates.xml` 里有父节点 `qt.qt6.6111.addons`（"Additional Libraries"），勾它即装全部附加模块 |
| 体积可接受 | 【已核实】34 个附加模块（不含调试符号）合计压缩 **178 MB**、解压 **1.76 GB**，比基础包还小 |
| 不影响链接 | 规则只链接 `o.modules` 列出的模块（§4.3），装了不等于用了 |
| 省掉一个开放问题 | 不再需要 34 个包名，也就不需要 xim 维护者确认 `qt-<module>` 的命名 |

**代价**：只要一个小模块（如 WebSockets 解压 1.8 MB）也得下载 178 MB；其中约 90% 来自 7 个大模块（Multimedia 479 MB、
Quick3DPhysics 445 MB、Quick3D 273 MB、LanguageServer 141 MB、gRPC 88 MB、ActiveQt 78 MB、Qt3D 73 MB，均为解压后）。
可接受，因为只有开了 `rules-qt-xim-addons` 的项目才下载，且 xlings 按版本缓存、全机共享。若以后某个模块单独太大，
再把它拆出去，不影响现有写法。

**许可**：附加模块中相当一部分只以 GPLv3 或商业许可提供（`Updates.xml` 对 addons 节点的说明："Most of the additional
libraries are available under commercial licenses from The Qt Company, or under GPL v3"）。安装不产生义务，链接才会；
由于规则只链接显式列出的模块，项目不会误用。包说明里写明这一点，并列出 LGPL 与 GPL-only 模块。

- 调试符号包（`debug_information`、`debug_info`）不收（基础包的调试符号解压 4.05 GB）。
- **首个版本 6.11.1**（验证工程的要求），平台先做 win64_msvc2022_64，再补 linux、macos、arm64。
- 【已核实】下载方式：6.11 起在线仓库按架构分目录（`qt6_6111/qt6_6111_msvc2022_64/Updates.xml`），aqtinstall 3.3.0 不认；
  每个归档以安装前缀为根（`bin/`、`include/`、`lib/`）。本设计的验证 CI 已按此方式装成功。
- 许可：xim 只是从 Qt 官方仓库下载；包说明注明 Qt 的 LGPL 义务（动态链接、允许替换）。

### 6.3 7z 运行时（D5）

【已核实】7-Zip 官方发布两种给程序用的库：完整安装包里的 `7z.dll`（全部格式，含 RAR 解压）和 "7-Zip Extra"
（`7z2603-extra.7z`）里的 `7za.dll`/`7zxa.dll`（不含 RAR）。行业惯例（bit7z 等封装库的用法）：

- **Windows**：把 `7z.dll`（需要 RAR 时）或 `7za.dll` 放在 exe 旁边随程序发布。`xim:7zip` 的 Windows 安装本来就是完整
  安装包展开，【已核实】`7z.dll` 就在 `xpkg_dir("xim", "7zip")` 下，所以项目一行 `mcpp::deploy` 即可，不再把 DLL 提交
  进仓库。
- **Linux / macOS**：【已核实】官方只发 `7zz` 程序，没有共享库；惯例是用发行版 p7zip 的 `7z.so`，由主机提供，不在
  xim 范围内。
- 许可：`7z.dll` 为 LGPL，RAR 解压部分带 unRAR 限制；包说明里注明。
- `xim:7zip` 要做的只有一件事：在包说明里写明 Windows 安装目录中 `7z.dll` 的位置是约定（不是偶然），这样项目可以依赖它。

## 7. 真实案例：验证工程迁移的成本与收益

本节回答："§3–§6 都做完以后，一个真实项目要花多少才能用上，能省下什么。"数据全部来自验证工程上游 `main`
2c7d6bf 的源码计数、它的 `how-to-build.md`、子模块源码和本设计的验证 CI（验证工程 PR #1）。

### 7.1 现状（【已核实】源码计数）

工作区 5 个成员：`验证工程`（core，库）、`GPPVersion`（库）、`cli`、`gui`、`Updater`（可执行）。依赖关系：
CLI、GUI 依赖 core；**Updater 不依赖 core，但自己链接 `bit7z`**。

| 部分 | 规模 | 做什么 |
|---|---|---|
| `mcpp-build-scripts/*.hpp` | 4 个头文件 383 行 | Qt 路径与模块（51）、vcpkg 链接（19）、lupdate/lrelease（62）、moc/rcc 与发布布局（251） |
| `mcpp-build-scripts/qt-root.txt` | 1 行 | **提交进仓库的本机路径** `D:/Qt/6.11.1/msvc2022_64` |
| `mcpp-build-scripts/runtime-stage` | 232 行（工具成员） | 解析 PE 导入表，把运行时 DLL 复制进 `Release/` 各目录 |
| `Release.py` | 47 行 | 复制 opencc 数据、BaseConfig、SampleProject、`7z.dll` 到三个发布目录 |
| 5 个 `mcpp.toml` + 根 | 240 行 | 其中 4 处写死 `../vcpkg_installed/gpp-x64-windows-release/include`，2 处写死 ElaWidgetTools 安装路径 |
| 4 个 `build.mcpp` | 79 行 | 调用上面的头文件 |
| `3rdParty/7z.dll` | 1.9 MB | 提交进仓库的二进制 |
| 子模块 ElaWidgetTools | CMake 项目 | 用自带的 `build.py` 编；Qt 路径 `D:/Qt/6.11.1/msvc2022_64` **写死在脚本里**；需要 `WidgetsPrivate` |
| 子模块 pybind11（fork） | 附带 Python 头文件与 `python312.lib` | 与仓库内 `Python-3.12.10-embed-amd64.zip` 配套；代码里写死 `BaseConfig/Python-3.12.10-embed-amd64` |
| vcpkg | 22 个 port，自定义 triplet `gpp-x64-windows-release`，2 个 overlay port（mecab、proxy） | `builtin-baseline` 写法 |
| Qt 用量 | 45 个含 `Q_OBJECT` 的头文件（全在 gui）、1 个 `.qrc`、4 个 `.ts`、0 个 `.ui` | 模块：Core、Gui、Widgets、Network；无附加模块 |

新开发者的准备工作（`how-to-build.md`，111 行）：

- 手动安装 8 样东西：git、xlings、mcpp、CMake、Python、VS Build Tools、vcpkg（克隆 + bootstrap + 加 PATH）、Qt（在线
  安装器，**要注册 Qt 账号**）。
- 手动改 2 个文件里的本机路径：ElaWidgetTools 的 `build.py`、`qt-root.txt`。
- 按顺序手动执行 8 条命令：`build.py`、`vcpkg install --triplet …`、`mcpp build` ×2、解压 Python embed 包、
  `Release.py`、`windeployqt` ×2。

CI（验证 workflow）：Qt 靠一段 25 行的内联 Python 从 Qt 仓库下载；vcpkg 靠克隆 + bootstrap + install 加 4 个缓存步骤；
【已核实】vcpkg 冷安装 51.6 分钟。

### 7.2 映射：每一块由什么替代

| 现状 | 替代 | 依赖哪个交付物 / 前提 |
|---|---|---|
| `qt_config.hpp` + `qt-root.txt` | `rules-qt-xim`：根目录来自 `xpkg_dir` | `xim:qt`、`rules-qt` |
| `vcpkg_link.hpp` + 4 处写死的 include 路径 | `deps-vcpkg`：core 的 `libraries` 列 20 个库，Updater 列 2 个 | `deps-vcpkg`；Updater 各自一条安装边（§3.3(5)） |
| `mcpp_translations.hpp` | `rules-qt` 的 `translations`，`update_sources = true`、`tr_function_alias = {"translate+=gppTr"}` | 保持现有"构建时更新 `.ts`"的行为 |
| `mcpp_actions.hpp` 的 moc/rcc（59 行）与 Qt 检查 | `rules-qt` | 无 |
| core 的 `.qm` 复制到 CLI/GUI | `rules-qt` 库成员翻译 | §4.3【待验证】 |
| `windeployqt` ×2（手动） | `rules-qt` 的 `deploy_plugins` | pack 闭包【待验证】，否则插件逐个 deploy |
| `3rdParty/7z.dll` + Release.py 中的复制 | `[xlings.workspace] "xim:7zip"` + 一行 `mcpp::deploy` | §6.3 |
| Release.py 中 opencc 数据 | `use()` 返回的前缀 + `deploy(prefix / "share/opencc")` | §3.2 |
| Release.py 中 BaseConfig、SampleProject | 项目 `build.mcpp` 里的 deploy；GUICORE 要排除 4 项 | deploy 是否支持排除模式【待验证】，否则保留项目现有的 `copy` 助手 |
| ElaWidgetTools `build.py` + 写死的 Qt 路径 | `deps-cmake`，`QT_SDK_DIR` 取 `rules::qt::root()`，布局用 `o.layout` | `deps-cmake`（Phase 3） |
| vcpkg 克隆 + bootstrap | `xim:vcpkg` + 托管克隆（§3.6） | 无 |
| VS Build Tools | mcpp 侧 `sysroot = "xim:msvc@…"`；vcpkg 侧**只有** `toolchain::msvc_xim` 能免装 VS | §3.3(4)【待验证】chainload 细节 |
| CMake、Python（开发者手装） | CMake 由插件声明；Python 只剩 embed 包解压（可改成一条用 `xim:7zip` 的 action） | 无 |
| **不替代**：`runtime-stage` + 发布布局代码 | 保留。`Release/cli`、`gui`、`GUICORE`、私有镜像目录是这个项目的发布约定，不是通用能力 | 只有改为从 `mcpp pack` 产物派生布局，或引擎提供 §8 需求 2，`runtime-stage` 才能删 |
| **不替代**：pybind11 fork 附带的 Python | 保留。换成 `xim:python` 会让头文件（3.12.13）与运行时 embed 包（3.12.10）的补丁版本不一致，而版本号写死在代码里 | 若要换，先把 embed 包也改由 xim 提供，属项目自己的决定 |

### 7.3 迁移成本（插件发布之后，验证工程一侧，一人）

| 阶段 | 内容 | 人日 | 前提 |
|---|---|---|---|
| A | 5 个 `mcpp.toml` 去掉写死路径、加插件；4 个 `build.mcpp` 改写；删 `qt_config`/`vcpkg_link`/`mcpp_translations`、moc/rcc 部分和 `qt-root.txt`；CI 去掉 Qt 下载与 vcpkg bootstrap；新旧 `Release/` 目录逐文件对比 | 1.5–2 | Phase 1、2 |
| B | Release.py、`7z.dll`、`windeployqt`、Python embed 包解压并入构建（deploy 与一条用 `xim:7zip` 的 action）；更新 `how-to-build.md` | 1–1.5 | 同上 |
| C | ElaWidgetTools 改用 `deps-cmake` | 1 | Phase 3 |
| D（可选） | 发布改为从 `mcpp pack` 派生，删 `runtime-stage`；vcpkg 改 `toolchain::msvc_xim` 实现免装 VS | 1–2 | §8 需求 2 或 pack 方案；§3.3(4) 验证通过 |
| **合计** | A–C 必做 | **3.5–4.5** | D 另计 1–2 |

对比：通用插件本身 27–37 人日（§9），验证工程只占其中 3.5–4.5。第二个及以后的项目不再分摊插件开发，只付
自己的 A–C。

### 7.4 收益（A–C 完成后）

| 指标 | 现在 | 迁移后 |
|---|---|---|
| 手动安装的工具 | 8 样（含需注册账号的 Qt 安装器） | 2 样（git、xlings）+ VS Build Tools；做完 D 后为 2 样 |
| 提交进仓库的本机路径 | 2 处（`qt-root.txt`、ElaWidgetTools `build.py`） | 0 |
| 手动执行的命令 | 8 条，有顺序要求 | `mcpp build` ×2（或一次工作区构建） |
| 项目自写的构建辅助代码 | 662 行（383 + 232 + 47） | 约 410 行：删 207 行（Qt/vcpkg/翻译/moc/rcc）和 Release.py 47 行，发布布局 ~175 行与 `runtime-stage` 232 行仍在；做完 D 后约 130 行 |
| 提交进仓库的二进制 | `7z.dll` 1.9 MB | 0 |
| IDE（mcppls） | 缺 Qt 或 vcpkg 时 emit 失败，退回兜底模型（issue 23 的起因之一） | emit 总是成功（原则 4），首次构建前 include 路径已正确 |
| CI 冷构建 | vcpkg 51.6 分钟 | **不变**：port 还是那 22 个、编译器还是 cl；省的是 bootstrap 与脚本维护，不是编译时间 |
| CI 热构建 | 两级缓存（`vcpkg_installed` + 二进制缓存）靠 workflow 手写 | 一级：缓存 vcpkg 默认的 `archives/` 即可 |

### 7.5 这个案例暴露出的设计缺口（已并入本版）

1. Updater 不依赖 core 却要 vcpkg 库 → §3.3(5) 改为每个成员各自声明安装边。
2. 自定义 triplet + chainload → §3.3(4) 改为 include 项目 triplet 而非替换。
3. "免装 VS"在 vcpkg 侧需要 chainload → §3.3(4) 新增 `toolchain::msvc_xim`。
4. 数据文件在 vcpkg 前缀里（opencc）→ §3.2 `use()` 返回前缀。
5. 现行项目依赖"构建时 lupdate" → §4.3 该选项的行为照搬现行实现（blocking check + stamp）。
6. CMake 子项目有非标准安装布局、需要 Qt 私有头 → §5 `o.layout`、§4.3 `private_modules`。
7. 发布布局与 embed Python 是项目约定 → §7.2 明确不替代，第 2 版"可删除约 600 行"的说法过高。
8. baseline 落在两个 registry 发布标签之间（2026-08-08，晚于标签 129 个提交）→ §3.6 按提交而非标签选检出。

## 8. 引擎侧需求（提给 mcpp）

| # | 需求 | 为什么 | 没有它时的退路 |
|---|---|---|---|
| 1 | 构建程序可感知"规划模式"（或 emit 时构建程序失败只降级） | 原则 3/4 目前靠插件自律；任何项目的构建程序出错都会让 IDE 退回兜底模型 | 插件自律：缺失只 warning |
| 2 | 构建程序声明运行时搜索目录（run 的 PATH、pack 的 PE 闭包），且项目能在 build 时取得闭包结果 | vcpkg/Qt 的 DLL 在各自 `bin/`；验证工程的 `runtime-stage` 就是在自己补这一块 | 逐个 `mcpp::deploy`；项目保留自己的闭包工具 |
| 3 | blocking action 对依赖方也生效，或工作区级 setup action | 多成员工作区共用一次安装 | 每个成员各一条安装边（§3.3(5)） |
| 4 | `mcpp::action` 设置环境变量 | 外部工具普遍靠环境变量配置 | 插件自带启动器（本设计采用） |

另：`[workspace.profile.*]` 继承与本插件无关，但同一消费者会遇到（【已核实】`docs/07` §4.1、specs §9 只允许三类继承；
验证工程5 个成员重复写了同样的 `[profile.release]`/`[profile.fast-release]`）。

## 9. 计划与工作量（一人估算，含 fixtures 与 CI，Windows 优先）

| 阶段 | 内容 | 人日 |
|---|---|---|
| Phase 0 验证 | 本文全部【待验证】项（汇总见 §11） | 2.5–3.5 |
| Phase 1 | `xim:vcpkg`、`xim:qt`（Windows）、`deps-vcpkg` + `mcpp-vcpkg`（registry 选择、按工具检出、锁、目录隔离、三种工具链）+ fixtures | 9–12 |
| Phase 2 | `rules-qt` + fixtures；`xim:qt-addons`；`xim:qt*` Linux/macOS | 8–10 |
| Phase 3 | `deps-cmake` + `mcpp-cmake` + fixtures | 3.5–5.5 |
| 消费者验证 |验证工程迁移 A–C（§7.3，fork 分支 + 其 CI） | 3.5–4.5 |
| **合计** | | **27–37**（第 2 版 24–33；增加的部分见 §11） |

**验证场已就绪**：验证工程 PR #1 的 workflow 已在 windows-2025 上搭好 xlings、mcpp 2026.9.25.1、
LLVM 22.1.8、Qt 6.11.1（直接读 Qt 仓库）、vcpkg（`gpp-x64-windows-release`，22 个 port）；【已核实】vcpkg 冷安装
51.6 分钟，结果已缓存。

## 10. 仍开放的问题与后续

1. ~~D6~~、~~D 阶段~~、~~免装 VS~~：第 3 轮已定（D6′、D8、D9）。
2. **后续**：Qt 5 支持（moc/uic/rcc 参数差异小，包名与部署规则不同）；`deps-conan`、`deps-prefix`；`xim:qt*` 其余平台
   与版本；工作区级 profile 继承（§8 另注）。

已关闭：registry 共享克隆的位置（D7）；`xim:qt-<module>` 命名（随 D6 消失）。

## 11. 自我 review 记录

### 第 3 版相对第 2 版

改动（按影响排序）：

1. **托管克隆的检出版本**（§3.6）：第 2 版让克隆跟随上游最新、再比较工具版本。上游 registry 一旦要求比 `xim:vcpkg`
   更新的工具，**所有项目会同时失败**，直到 xim 跟进。改为检出"主干上与已装工具匹配的最后一个提交"。本版起草时
   曾写成"与工具匹配的最新发布标签"，用验证工程的 baseline 实测（比最新标签晚 129 个提交、工具要求相同）后改掉。
2. **用户已有的 `VCPKG_ROOT` 改为只读**（§3.6、原则 5）：第 2 版会对它 `git fetch`，等于改写用户的仓库。
3. **按项目隔离 buildtrees/packages**（§3.3(1)）：第 2 版没提，共享克隆时两个项目同时编同一个 port 会互相覆盖。
4. **二进制缓存改用 vcpkg 默认位置**（§3.3(1)）：第 2 版放在 mcpp 缓存目录，与 D7 的"复用用户已有的 vcpkg"矛盾。
   另外第 2 版"放默认数据目录就能复用用户已有 vcpkg"的理由不成立：复用靠的是识别 `VCPKG_ROOT`，不是目录位置；
   目录位置带来的实际好处是与默认二进制缓存同级。
5. **"免装 VS"的真实条件**（§3.3(4)）：第 2 版把"工具链一致"当作纯可选项，并在 §6.1 暗示 `xim:msvc` 就能免装 VS。
   已核实 vcpkg 默认路线必须找到 VS 实例，只有 chainload 能绕开；新增 `toolchain::msvc_xim`。
6. **工作区默认做法**（§3.3(5)）：由"共享 stamp 技巧"改为"每个成员各一条安装边"，更简单，且验证工程的
   Updater 正好需要。
7. **§7 重写**：第 2 版的验证工程样例写了 `xim:python = 3.12.13`（与 embed 包 3.12.10 不一致）、称可删除约 600 行
   （`runtime-stage` 和发布布局其实删不掉）、完全没提 ElaWidgetTools（`deps-cmake` 对这个项目不是可有可无）。
8. 附加模块合为一个包（D6），§0、§4.2、§4.3、§6.1、§6.2 同步；新增 `rules-qt-xim-addons`。
9. 小项：自定义 triplet 包装；`use()` 返回前缀；`private_modules`；库成员翻译；rcc 依赖；fixtures 增加 4 个。

工作量 +3～4 人日：Phase 0 多 4 个验证项；Phase 1 多按工具检出、目录隔离与两种 chainload；Phase 3 多布局选项；
消费者验证按 §7.3 的逐阶段估算上调。

残余风险（按影响排序）：

1. **PE 闭包是否搜索插件声明的目录**（§3.3(3)、§4.3）：影响两个插件的部署路径；退路是逐个 `deploy`，功能不受影响。
2. **chainload 在 Windows 上传递 MSVC 环境**（§3.3(4)）：影响"免装 VS"。不成立时仍可用 `vcpkg_default`，只是要装 VS。
3. **vcpkg manifest 模式对安装目录的锁**（§3.3(5)）：影响多成员并发；退路是共享 stamp。
4. **依赖方 deploy 是否进入可执行文件布局**（§4.3）：影响库成员翻译；退路是可执行成员直接列 `.ts`。
5. **极旧 baseline 与当前检出的脚本不兼容**（§3.6）：报错写明 baseline 与脚本版本，由项目升级 baseline 或固定旧的 `xim:vcpkg`。
6. **lupdate 默认关闭**会让已习惯"构建时自动更新 `.ts`"的项目多一行配置；有意的取舍（不写源码树）。

### 第 2 版相对第 1 版（保留）

链接一律用完整路径（首次构建规划期文件必然不存在，条件判断会让两次构建的链接行不同）；补 Linux 主机依赖与
`xim:git`；新增 registry 分离；`.ts` 不会误判；零配置行为；`/Zc:__cplusplus`；软件 GL 可选部署；修正 `when`；
按 D3、D5 重写 §6.2、§6.3。

### 【待验证】汇总（Phase 0）

PE 闭包目录；vcpkg manifest 模式对安装目录的锁；chainload 下 `INCLUDE`/`LIB` 的传递（`VCPKG_ENV_PASSTHROUGH` 或
工具链文件直写）；`VCPKG_FORCE_SYSTEM_BINARIES` 在 Windows 上的效果；Linux 主机依赖在 xim 中的覆盖；chainload clang
的 port 兼容性；registries 缓存各平台默认路径；共享克隆读旧 baseline；依赖方 deploy 进入可执行文件布局；deploy 的排除
模式；`xim:python` 的开发文件；moc/rcc 的 depfile 版本下限。
