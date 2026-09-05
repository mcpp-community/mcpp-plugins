# mcpp-plugins

The build plugins the mcpp project maintains, published as one package,
`mcpp:plugins`. A consumer selects the members it needs through features and
imports each one from `build.mcpp` under the module name the member declares.

```toml
[dependencies.mcpp]
plugins = { version = "0.1.0", features = ["rules-spirv"], host-module = true }
```

```cpp
// build.mcpp
import std;
import mcpp;
import mcpp.rules.spirv;

int main() {
    mcpp::rules::spirv::options opt;
    opt.includes = { "shaders" };
    return mcpp::rules::spirv::compile(opt) ? 0 : 1;
}
```

## Naming

| family | module name | purpose |
|---|---|---|
| rules | `mcpp.rules.<x>` | how one kind of translation unit is compiled by a compiler mcpp does not drive: the spelling of its flags, the probe of its toolkit, the actions it submits |
| tools | `mcpp.tools.<x>` | a build-time utility independent of any compiler; see `tools/README.md` |
| identity | `mcpp.plugins` | the lib root, compiled before every member; it states the collection's version |

The `mcpp.` prefix is reserved for this package: mcpp warns when a module under
it is declared by a package outside the `mcpp` namespace. `mcpp.build.*` is the
engine's own module family and is not used here.

## Members

| feature | module | since mcpp | what it needs |
|---|---|---|---|
| `rules-cuda` | `mcpp.rules.cuda` | 2026.9.5.2 | the toolkit named in `[xlings.workspace]` (`xim:cuda-nvcc`, `xim:cuda-cudart`), `[build] accel = "cuda…"`, a constrained glob for `*.cu`; the clang route with an LLVM toolchain, the nvcc route with a GCC one |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.9.5.3 | `xim:glslang` in `[xlings.workspace]`, `[build] accel = "vulkan1.2"`, a constrained glob for the shader stages; emits one header per shader through a `role = "source"` action |

The floor is the mcpp release whose engine carries what the member relies on:
`rules-spirv` needs the device-source table that classifies shader extensions,
which 2026.9.5.3 introduced. The index descriptor states the floor; a project
on an older mcpp is refused at resolution rather than at the first shader.

## How the engine sees this package

mcpp compiles every module interface unit among a host-module package's
resolved sources as a host module of its own, the lib root first (2026.9.5.3+).
`[features.<f>] sources` is what puts a member into that set, so the module set
a consumer can import is exactly its feature set. A member is compiled alone,
in the same command as the consumer's `build.mcpp`, and may import `std`,
`mcpp` and `mcpp.plugins`.

## Layout

```
mcpp.toml          the package: one feature per member
src/plugins.cppm   export module mcpp.plugins;
rules/<x>.cppm     export module mcpp.rules.<x>;
tools/<x>.cppm     export module mcpp.tools.<x>;
tests/<consumer>/  one project per member, built by CI with the pinned mcpp
```

## Adding a member

1. One file, `rules/<x>.cppm` or `tools/<x>.cppm`, declaring its module name.
   The engine owns the graph — the accelerator axis, the constrained globs,
   the action edges, the fingerprint — and the member owns the spelling: which
   tool, which flags, what is generated. A member does not read `/usr`; a tool
   comes from `mcpp::toolchain_dir()`, `mcpp::xpkg_dir()` or an explicit option,
   and a missing one is refused naming the `[xlings.workspace]` entry to add.
2. A feature in `mcpp.toml` adding that one source.
3. A consumer under `tests/` that builds through it and asserts on the
   artefact, and a row in the table above with the mcpp floor.
4. A version bump: mcpp identifies an installed package by `(name, version)`,
   so a changed payload under an unchanged version is not reinstalled.

## Releases

A tag `v<version>` publishes the source archive; the index descriptor
(`mcpp-index/pkgs/m/mcpp.plugins.lua`) names the GitHub archive and its GitCode
mirror with one sha256.
