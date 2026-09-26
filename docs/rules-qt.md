# `rules-qt`

`mcpp.rules.qt` runs Qt's code generators (`moc`, `uic`, `rcc`) and Linguist tools (`lupdate`, `lrelease`, `lconvert`) as build actions, links the Qt modules and places their runtime beside the program. The rule declares no SDK and pins no version: the project names the Qt it builds with, in `build.mcpp` or in its own `[xlings]` table. Module `mcpp.rules.qt`; engine floor 2026.9.26.2 (mcpp#702); from 0.13.0.

## Use

```toml
[build-dependencies.mcpp]
plugins = { version = "0.15.2", features = ["rules-qt"], host-module = true }

# The SDK and its version are the project's declaration.
[target.'cfg(any(windows, linux, macos))'.xlings.workspace]
"xim:qt-base" = "6.11.1"

[build]
sources = ["src/*.cpp", "res/*.qrc", "i18n/*.ts", "ui/*.ui"]
```

```cpp
// build.mcpp
import mcpp.rules.qt;

int main() {
    mcpp::rules::qt::options o;
    o.modules        = { "Core", "Gui", "Widgets", "Network" };
    o.deploy_plugins = { "platforms", "styles", "imageformats" };
    o.i18n.tr_function_alias = { "translate+=appTr" };
    return mcpp::rules::qt::compile(o) ? 0 : 1;
}
```

## The SDK

The first of three levels that names an SDK decides, and the rule records which as the fact `rules-qt.sdk` (0.15.0):

| level | how | scope |
|---|---|---|
| 1. build program | `options::root`, `options::extra_roots` | any value `build.mcpp` computes |
| 2. environment | `QT_ROOT_DIR`, the variable install-qt-action and aqtinstall set; a change re-plans the build | one machine |
| 3. declared payload | `xim:qt`, else `xim:qt-base`, with `xim:qt-addons` as a second prefix, at the version the project's `[xlings]` table states | one project, provisioned by `mcpp build` |

| payload | contents |
|---|---|
| `xim:qt-base` | qtbase, qttools with the QtQml library `lupdate` loads, and qttranslations; about a third of `xim:qt`'s download |
| `xim:qt` | qtbase, qtsvg, qtdeclarative, qttools, qttranslations |
| `xim:qt-addons` | the additional libraries, beside `xim:qt` |

Each payload carries its runtime closure: the loader and the libraries Qt loads on Linux, and the VC++ runtime on Windows x64. 0.15.0 removed the features `rules-qt-xim`, `rules-qt-xim-base` and `rules-qt-xim-addons`, which declared a payload at a fixed version. A feature states a mechanism; the SDK a program links is the project's choice. A project that used one of those features declares the payload instead, as above.

A project that does not use a payload comments out its declaration, because `mcpp build` provisions every declared payload whether or not a higher level names another SDK. It then names its SDK with `QT_ROOT_DIR` or with `options::root` in `build.mcpp`:

```toml
[target.'cfg(any(windows, linux, macos))'.xlings.workspace]
# "xim:qt-base" = "6.11.1"
```

```cpp
mcpp::rules::qt::options o;
o.root = "D:/Qt/6.11.1/msvc2022_64";   // or leave empty and set QT_ROOT_DIR
```

A package that enables `rules-qt` only to import `mcpp.rules.qt`, and writes no `build.mcpp`, runs the program mcpp synthesises. When it has no SDK and no `.ui`, `.qrc` or `.ts` of its own, that program reports nothing (0.15.2; mcpp#715).

## Options

| option | meaning |
|---|---|
| `modules` | `Core`, `QtCore` and `Qt6Core` name one module; each is linked by full path and defines `QT_<MODULE>_LIB` |
| `private_modules` | modules whose private headers are included |
| `moc`, `moc_headers` | `moc_scan::project_headers` (default) scans the package's headers by content; `moc_scan::listed` takes `moc_headers` only |
| `forms`, `resources` | `.ui` and `.qrc` files beside those in `[build] sources` |
| `i18n` | `.ts` files beside those in `[build] sources`; `update_sources` runs `lupdate` as an action whose output is the `.ts` it rewrites; `tr_function_alias`; `deploy_to` (default `translations`); `out_dir` (default `<out dir>/qt/translations`); `qt_languages`: the linked modules' catalogs combined by `lconvert` into `qt_<language>.qm`, the file windeployqt writes |
| `deploy_plugins` | plugin directories placed beside the program; default `platforms` |
| `deploy_software_gl` | Windows: `opengl32sw.dll` and `d3dcompiler_47.dll` beside the program |
| `root`, `extra_roots` | level 1 of the SDK lookup |

`roots()` and `root()` return the SDK the lookup chose, for a program that hands it on (a CMake subproject's `CMAKE_PREFIX_PATH`).

## Behaviour

- `moc` runs for every header under the package root that declares `Q_OBJECT`, `Q_GADGET` or `Q_NAMESPACE`, and for a source that includes its own `<stem>.moc`; `uic` for `.ui`, `rcc` for `.qrc`, `lrelease` for `.ts`. Each is a `role = "source"` action with declared inputs; the outputs are under `<out dir>/qt/`.
- The SDK's `bin/` (Windows) or `lib/` is a runtime search directory: the program's run path, `mcpp run`'s load path, `mcpp pack`'s closure, and on Windows the Qt DLLs placed beside the program. The plugin directories `deploy_plugins` names are deployed beside the program.
- Under an MSVC compiler the rule adds `/Zc:__cplusplus` and `/permissive-`; on Linux, `-fPIC`. On macOS the modules are frameworks under `lib/`, compiled with `-F<root>/lib` and linked by their binaries' full paths. A resource compiled into a static library is registered with `Q_INIT_RESOURCE(<stem>)`.
- A missing SDK, module or tool is a warning: the plan states what it can, and the build is where the absence fails.

## Runtime closure

A program packed by `mcpp pack` starts on a machine that has only its operating system:

| platform | Qt loads | provided by |
|---|---|---|
| Linux | glib, zstd, zlib, libdbus, fontconfig, freetype, X11, xkbcommon, EGL/GL, the xcb libraries | the `xim:qt` and `xim:qt-base` payloads: they declare these packages with `xim:glibc`, so xlings patches every file of the payload to the ecosystem's loader and a RUNPATH over them (openxlings/xim-pkgindex#884); Widgets programs pass mcpp's runtime closure check and run under that loader |
| Windows | system DLLs, and the VC++ runtime (`MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`) | the payloads place the redistributable VC++ runtime in `bin/` (windows-x86_64), so it reaches the program's directory with Qt's DLLs |
| macOS | system frameworks and libc++ | the system |

The rule names none of these libraries (0.15.0; 0.13.0 and 0.14.0 declared glib, zstd and zlib on Linux and served QtCore only). A Qt from elsewhere carries what its installer arranged. On Linux, QtNetwork additionally loads `libgssapi_krb5` and `libbrotlidec`, which the ecosystem does not publish yet.

Qt's official Linux build uses the shared libstdc++, so a Linux program states `[build] cxx_runtime = "toolchain-coupled"` (mcpp's docs/20) and builds with a gcc toolchain; under a libc++ toolchain the rule warns. The statement is project-wide because mcpp reads a `[target.<triple>]` table only when a target is named (mcpp#704).
