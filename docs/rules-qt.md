# `rules-qt`

`mcpp.rules.qt` runs Qt's code generators (`moc`, `uic`, `rcc`) and Linguist tools (`lupdate`, `lrelease`, `lconvert`) as build actions, links the Qt modules and places their runtime beside the program. The SDK is the project's choice.

## `rules-qt`

Module `mcpp.rules.qt`; engine floor: 2026.9.26.2 (mcpp#702).

**Needs and behaviour.** A Qt 6 SDK: `rules-qt-xim` declares `xim:qt` 6.11.1 on the target axis, `rules-qt-xim-base` declares `xim:qt-base` instead (qtbase and qttools with the QtQml library lupdate loads, about a third of the download; 0.14.0), `rules-qt-xim-addons` adds `xim:qt-addons` (the additional libraries) as a second prefix, and `options::root` names an SDK from elsewhere. From 0.13.0. `moc` for every header under the package root that declares `Q_OBJECT`, `Q_GADGET` or `Q_NAMESPACE` and for a source that includes its own `<stem>.moc`; `uic` for `.ui`, `rcc` for `.qrc`, `lrelease` for `.ts` (named in `[build] sources` or in the options), each a `role = "source"` action with declared inputs. The modules are linked by full path, and the SDK's `bin/` (Windows) or `lib/` is a runtime search directory: the program's run path, `mcpp run`'s load path, `mcpp pack`'s closure, and on Windows the Qt DLLs the program imports placed beside it by the engine. The plugin directories `deploy_plugins` names are deployed beside the program. See [the section below](#rules-qt-qts-code-generators-and-linguist-tools)

## `rules-qt`: Qt's code generators and Linguist tools

```toml
[build-dependencies.mcpp]
plugins = { version = "0.14.1", features = ["rules-qt-xim"], host-module = true }

[build]
sources = ["src/*.cpp", "res/*.qrc", "i18n/*.ts", "ui/*.ui"]
```

```cpp
import mcpp.rules.qt;

int main() {
    mcpp::rules::qt::options o;
    o.modules        = { "Core", "Gui", "Widgets", "Network" };
    o.deploy_plugins = { "platforms", "styles", "imageformats" };
    o.i18n.tr_function_alias = { "translate+=appTr" };
    return mcpp::rules::qt::compile(o) ? 0 : 1;
}
```

| feature | SDK |
|---|---|
| `rules-qt` | `options::root`; nothing is downloaded |
| `rules-qt-xim` | `xim:qt` 6.11.1: the official base package without documentation (qtbase, qtsvg, qtdeclarative, qttools, qttranslations) |
| `rules-qt-xim-base` | `xim:qt-base` 6.11.1 in place of `xim:qt`: qtbase and qttools with the QtQml library `lupdate` loads, about a third of the download (56–73 MB); every module a widgets or console program links, without Qt Quick, qtsvg or Qt's own translations |
| `rules-qt-xim-addons` | adds `xim:qt-addons` 6.11.1, every additional library, as a second prefix |

| option | meaning |
|---|---|
| `modules` | `Core`, `QtCore` and `Qt6Core` name one module; each is linked by full path and defines `QT_<MODULE>_LIB` |
| `private_modules` | modules whose private headers are included |
| `moc`, `moc_headers` | `moc_scan::project_headers` (default) scans the package's headers by content; `moc_scan::listed` takes `moc_headers` only |
| `forms`, `resources` | `.ui` and `.qrc` files beside those in `[build] sources` |
| `i18n` | `.ts` files beside those in `[build] sources`; `update_sources` runs `lupdate` before `lrelease`, as an action whose output is the `.ts` file it rewrites; `tr_function_alias`; `deploy_to` (default `translations`); `out_dir`, where `lrelease` writes (default `<out dir>/qt/translations`); `qt_languages`, Qt's own strings for each language: the catalogs of the linked modules combined by `lconvert` into `qt_<language>.qm` under `deploy_to`, the file windeployqt writes |
| `deploy_plugins` | plugin directories placed beside the program; default `platforms` |
| `deploy_software_gl` | Windows: `opengl32sw.dll` and `d3dcompiler_47.dll` beside the program |
| `root`, `extra_roots` | an SDK, and further prefixes |

Generated files are written under `<out dir>/qt/`: `moc_<stem>.cpp` and
`qrc_<stem>.cpp` are compiled, `ui_<stem>.h` and `<stem>.moc` are included
(the directory is an include directory), and `translations/<stem>.qm` is
deployed. Under an MSVC compiler the rule adds `/Zc:__cplusplus` and
`/permissive-`; on Linux, `-fPIC`, which Qt's headers require. A resource
compiled into a static library is registered with `Q_INIT_RESOURCE(<stem>)`, as
Qt documents; in a program it registers itself.

A missing SDK, module or tool is a warning: the rule states what it can, and
the build is where the absence fails.

On Linux, Qt's official QtCore links glib, zstd and zlib and the shared
`libstdc++`. `rules-qt-xim` and `rules-qt-xim-base` declare the first three on Linux, and the rule
declares their `lib/` directories as runtime search directories; the program
states `[build] cxx_runtime = "toolchain-coupled"` (mcpp's docs/20), so the
process has one C++ runtime. The statement is project-wide because mcpp reads a
`[target.<triple>]` table only when a target is named (mcpp#704). On macOS and
under clang on the MSVC ABI mcpp reports the contract it delivers instead. Modules that load QtGui are not
served on Linux: QtGui loads `libdbus-1.so.3`, which the ecosystem does not
publish, and mcpp's runtime closure check refuses the program. On macOS the
modules are frameworks under `lib/`: the rule compiles with `-F<root>/lib` and
links each framework's binary by its full path.
