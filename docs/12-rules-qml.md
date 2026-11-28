# `rules-qml`

`mcpp.rules.qml` builds a QML application: for each QML module the project states, it declares the chain Qt's `qt_add_qml_module` runs (`moc`, `qmltyperegistrar`, `qmlcachegen`, `rcc`, `qsb`) as actions, so the module is compiled into the executable and loaded with `loadFromModule`; it places the Qt QML modules the program imports beside it, and writes a `qt.conf` that points Qt at them. It builds on [rules-qt](11-rules-qt.md), which it implies, and takes its options. Module `mcpp.rules.qml`; engine floor 2026.10.1.3 (the floor of `rules-qt`; the package needs 2026.10.30.1, and the lowest release the fixtures were run on is 2026.10.31.3), and 2026.11.23.1 for a `mcpp pack` that carries the libraries the QML plugins load (mcpp#1011); from 0.27.0.

## Use

```toml
[build-dependencies.mcpp]
plugins = { version = "0.27.0", features = ["rules-qml"], host-module = true }

# QML needs Qt Quick, which `xim:qt` carries and `xim:qt-base` does not.
[target.'cfg(any(windows, linux, macos))'.xlings.workspace]
"xim:qt" = "6.11.1"

[build]
sources = ["src/*.cpp"]
```

```cpp
// build.mcpp
import std;
import mcpp;
import mcpp.rules.qml;

int main() {
    namespace qml = mcpp::rules::qml;
    qml::options o;
    o.qt.modules = { "Core", "Gui", "Qml", "Quick" };

    qml::qml_module probe;
    probe.uri       = "Probe";
    probe.root      = "qml/Probe";
    probe.sources   = { "qml/Probe/**/*.qml", "qml/Probe/**/*.js" };
    probe.resources = { "qml/Probe/data/*" };
    o.modules = { probe };
    return qml::compile(o) ? 0 : 1;
}
```

```cpp
// main.cpp
QQmlComponent component(&engine);
component.loadFromModule("Probe", "Main");
```

`rules-qml` claims no file extension and names no `rule_module`: a `.qml` file needs a uri and a version that `[build] sources` cannot state, so the project writes `host-module = true` and states its modules in `build.mcpp`. The `rules-qt` it implies keeps its own claim of `.ui`, `.qrc` and `.ts`. One `qml::compile` call also runs what `qt::compile` runs, with `options::qt`; call one of them, not both.

## A module

| field | meaning |
|---|---|
| `uri` | `Probe`, `com.acme.ui`: identifiers joined by dots; the directory `com/acme/ui` in the resources |
| `version` | `major.minor`, default `1.0` |
| `sources` | `.qml`, `.js` and `.mjs` files, paths or globs (`*`, `?`, `**/`) relative to the package root; any other file is a resource |
| `root` | the directory the module's files are relative to; empty is the directory the sources share |
| `resources` | other files (images, data) |
| `headers` | the headers of the module's C++ types. Empty takes every header `moc` read that uses `QML_ELEMENT` or a related macro, when the project has one module; with several modules each header is listed in one, and a header nobody lists is refused |
| `depends`, `imports` | `depends` and `import` lines of the `qmldir`, as the project states them |
| `shaders`, `qsb_args` | `.vert`, `.frag`, `.comp` compiled by `qsb` (default `--qt6`) and embedded as `shaders/<file>.qsb` |
| `aot` | default `true`: `qmlcachegen` compiles each file and a loader registers them; `false` embeds the sources only |
| `prefix` | resource prefix, default `/qt/qml`, where Qt's import path already looks |

A `.qml` file whose name starts with a capital letter is a type of the module; one that contains `pragma Singleton` is a singleton. A C++ singleton (`QML_SINGLETON`) needs no line in the `qmldir`.

## Options

| option | meaning |
|---|---|
| `modules` | the QML modules of the application |
| `qt` | every option of `rules-qt`; `Qml` is always linked, and `moc` is given the SDK's include directories and writes the type description |
| `quick_plugins` | plugin directories a Qt Quick program needs (`xcbglintegrations`, `platforminputcontexts`, `imageformats`), placed with `qt.deploy_plugins`; only those the SDK has |
| `imports` | Qt modules the program loads and no `.qml` file imports (`Qt.createComponent` with a computed name) |
| `styles` | when not empty, an `optional import` of a Qt Quick Controls style is followed for these only; empty places every style |
| `import_paths` | directories of QML modules beyond the SDK's, each holding `<uri as a path>/qmldir` |
| `deploy_imports` | default `true`: place the Qt modules the program imports |
| `write_qt_conf` | default `true`; see below |
| `lint`, `lint_warnings_are_errors` | run `qmllint` on every `.qml` file as a check |

`qt.i18n.update_sources` also reads `qsTr` in `.qml` and `.js` files.

## The actions

For each module, in dependency order, all `role = "source"` with declared inputs and outputs, so an edited file re-runs only what reads it:

| id | runs | writes |
|---|---|---|
| `qt:moc:<header>` | `moc --output-json` with the SDK's include directories | `moc_<stem>.cpp`, `moc_<stem>.cpp.json` |
| `qml:registrar:<uri>` | `qmltyperegistrar`, the SDK's and the other modules' type descriptions as foreign types, so a type may derive from a type of another module | `<module>_qmltyperegistrations.cpp`, `<module>.qmltypes` |
| `qml:qsb:<uri>:<shader>` | `qsb` | `shaders/<file>.qsb` |
| `qml:rcc:<uri>` | `rcc` over the qrc the rule writes (the files, the `qmldir`, the shaders) | `qrc_<module>.cpp` |
| `qml:cachegen:<uri>:<file>` | `qmlcachegen`, one per `.qml`, `.js`, `.mjs` | `<module>_<file>.cpp` |
| `qml:cachegen:<uri>:loader` | `qmlcachegen --resource-name`, writing the loader that registers the compiled files | `<module>_qmlcache_loader.cpp` |
| `qml:lint:<uri>:<file>` | `qmllint`, role `check` | |

The `qmldir`, the qrc and the loader's response file are written at plan time and only when their content changes. The generated `.cpp` files are compiled and linked into the program, so a module is static: nothing but the executable carries it. Generated names come from the uri (`.` becomes `_`); two modules or files that would write one name are refused.

## What the program loads

A Qt module (`QtQuick.Controls`, `QtQuick.Layouts`) is a directory of files and usually a plugin library, opened when a `.qml` file imports it. No import table names it. The rule reads the `import` statements of the project's `.qml` files (not those in comments or strings) and follows the SDK's `qmldir` files (`import`, `default import`, `optional import`, `depends`) and the `.qml` files of each module directory, which import further modules: the closure `qmlimportscanner` computes, read from text at plan time. On Qt 6.11.1 it equals the scanner's on four samples, among them all forty top-level modules. Each module is placed under `qml/<uri as a path>/`, without type descriptions, `designer/` and debug files, and a directory that is a module of its own is placed when it is imported. A `.qml` file imported only from another module is not the project's; `import "file.js"` and `import "dir"` name files and are not modules; a module registered by the program at run time (`qmlRegisterType`) is noted, not an error, and a `Qt*` module the SDK lacks is a warning.

The sizes are worth knowing. `QtQuick` with `QtQuick.Controls` places 24 modules, 1231 files and 6.8 MB with every style; `styles = { "Material" }` places 15 modules and 173 files; `{ "Basic" }`, 12 modules and 100 files. The style a program uses is chosen at run time (`QQuickStyle`, `qtquickcontrols2.conf`), so the default places all of them.

The plugin libraries of those modules load Qt libraries the program does not link (`libQt6QuickLayouts.so.6`). `mcpp pack` takes what the build deploys beside the program as a root of its runtime closure, so it carries them (mcpp#1011, from 2026.11.23.1; an older engine builds and runs the program, and its pack leaves those libraries out). On macOS `mcpp pack` does not stage Qt's frameworks yet, for `rules-qt` as well, so CI checks `qml/` and `qt.conf` in the directory the program is built into.

## `qt.conf`

A program found beside a `qt.conf` takes its `Prefix` from that file's directory, so the rule writes one with `Prefix = .` and `Qml2Imports = qml`, and places it beside the program. Three things follow. Qt's data, settings and library paths are rebased onto the program's directory as well; `Translations` is `<program>/translations`, which is `i18n.deploy_to`, and a different `deploy_to` is written into the file. The program and the packed tree behave alike: with the file, Qt looks for QML modules only in the deployed `qml/`, so an incomplete closure fails in `mcpp run`, where it can be fixed, and not after packing. And the plugin directories stay where `rules-qt` places them, beside the program, which Qt searches as a library path.

A project that has its own `qt.conf` (at the package root, in `res/`, or as `/qt/etc/qt.conf` in a resource file) keeps it: none is written, and a warning says what the file has to set. `write_qt_conf = false` writes none.

## Limits

- **Not planned for** `qmltc` (QML compiled to C++ classes), QML plugins as shared libraries (`qt_add_qml_plugin`), QML modules that come from a dependency package (`import_paths` takes their directory; a module in a static library of another package may be dropped by the linker, and a reference to its `qml_register_types_<uri>` keeps it), a macOS `.app` bundle's layout, Android, iOS and WebAssembly (the call warns and declares nothing), a package with more than one target (the generated sources go into each target), `qmlformat`, and Qt 5.
- **Paths with a space or a non-ASCII character** in the project's directory fail in gcc's module dependency scan; `qt-consumer` fails the same way. The fixtures use ASCII paths.
- `.js` files that `.import` another, and `.mjs` `import` statements, are not read by the closure; name the module in `imports`. Neither does it see a module whose name a program composes at run time.
- `qsb` is part of the additional libraries, `xim:qt-addons`, which the project declares beside `xim:qt`. A published `xim:qt-addons` older than xim-pkgindex#1262 leaves `qsb` on the system's loader, and it does not start; the fixture skips with a notice where it does not.
- The generated C++ builds with gcc 16 and with clang 20 to 23. On Linux `xim:qt` is built with libstdc++, so the build uses a gcc toolchain, as `rules-qt` states; Windows and macOS use the engine's default toolchain.

## Platforms

| | Linux | Windows | macOS |
|---|---|---|---|
| plugins | `libqtquick2plugin.so` | `qtquick2plugin.dll` (not the `d` build) | `libqtquick2plugin.dylib` |
| fixtures | built, run, packed, run without the SDK | built, run | built, run |

The Windows and macOS paths are covered by CI.
