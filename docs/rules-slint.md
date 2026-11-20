# `rules-slint`

`mcpp.rules.slint` compiles [Slint](https://slint.dev) user interfaces: each `.slint` file becomes, through `slint-compiler`, a C++ header (and on request a number of `.cpp` units and a C++ module) the program uses. It is what upstream's CMake function `slint_target_sources` does. The rule declares no SDK and pins no version: the compiler and the runtime the program links come from one Slint SDK the project chooses. Module `mcpp.rules.slint`; engine floor 2026.10.1.3; from 0.27.0.

## Use

```toml
[build-dependencies.mcpp]
plugins = { version = "0.27.0", features = ["rules-slint"], host-module = true }

[dependencies]
mcpplibs.mwrap.slint = "1.19.0"      # or compat.slint: the SDK as a package

[build]
sources = ["src/*.cpp", "ui/app.slint"]
```

```cpp
// build.mcpp
import mcpp.rules.slint;

int main() {
    mcpp::rules::slint::options o;
    o.module = "app.ui";              // also write `export module app.ui;`
    return mcpp::rules::slint::compile(o) ? 0 : 1;
}
```

```cpp
// src/main.cpp
import app.ui;                         // or: #include "app.h"

int main() {
    auto ui = AppWindow::create();
    ui->run();
}
```

## Options and upstream CMake

| `slint_target_sources` / target property | option | `slint-compiler` |
|---|---|---|
| the `.slint` inputs | `[build] sources`, `options::sources` | the input |
| `NAMESPACE` | `ns` | `--cpp-namespace` |
| `LIBRARY_PATHS name=path` | `library_paths` | `-L name=path` |
| — | `include_paths` | `-I` |
| `COMPILATION_UNITS n` | `compilation_units` | `--cpp-file` × n, each compiled |
| `EDITION` | `edition` | `--edition` |
| `SLINT_STYLE` | `style` | `--style` |
| `SLINT_EMBED_RESOURCES` | `embed_resources` | `--embed-resources` |
| `SLINT_SCALE_FACTOR` | `scale_factor` | `--scale-factor` |
| `SLINT_TRANSLATION_DOMAIN` | `translation_domain` | `--translation-domain` |
| `SLINT_BUNDLE_TRANSLATIONS` | `bundle_translations` | `--bundle-translations` |

Each input is one action with the depfile `slint-compiler --depfile` writes, so an edit of the input or of a file it imports regenerates that input alone, and a build with nothing changed regenerates nothing. Two inputs with one stem are refused, since both would write `<stem>.h`.

## The module

With `options::module`, the rule writes a module interface unit per input (`module` for one input, `<module>.<stem>` for several): the generated header in its global module fragment and `export using` for what the input exports and `slint-compiler` generates, under the exported names:

- each exported component that inherits `Window` or `Dialog`, directly or through components of its own (`slint-compiler` generates nothing for an exported component that is not a window);
- each exported `struct`, `enum` and `global`;
- the aliases of `export { A as B }`, and what `export { … } from "…"` and `export * from "…"` re-export, read from the files they name (`library_paths` and `include_paths` resolve them; the built-in libraries such as `std-widgets.slint` export nothing of their own).

The rule reads the `.slint` files while the build program runs, so a change to their exports re-plans the build. `options::exports` names the exports instead, for a file the reading does not cover. The header is not included inside `export { … }`: Clang refuses to export the out-of-class member definitions `slint-compiler` writes.

## The SDK

The first that answers decides, recorded as the fact `rules-slint.sdk`:

| level | how |
|---|---|
| 1. build program | `options::compiler` (a tool choice: a path, `tool::on_path()`) or `options::root` (an SDK laid out as upstream's: `bin/`, `lib/`, `include/slint/`) |
| 2. environment | `SLINT_COMPILER`, the name of upstream CMake's cache variable |
| 3. a package of the graph | the directory of `compat.slint` (`options::sdk_package`), which the program depends on directly or through `mcpplibs.mwrap.slint` |
| 4. declared payload | `xim:slint`, at the version the project's `[xlings]` table states |

At levels 1 and 4 the SDK is not a package, so the rule puts it on the build: `include/slint` on the include path, `slint_cpp` on the link and its directory among the program's run-time directories (`lib`, or `bin` on Windows, where the DLL is placed beside the program). At level 3 the package does that itself. With none, the rule warns and plans nothing.
