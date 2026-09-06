# mcpp-plugins

The build plugins the mcpp project maintains, published as one package,
`mcpp:plugins`. A consumer selects the members it needs through features and
imports each one from `build.mcpp` under the module name the member declares.

```toml
[build-dependencies.mcpp]
plugins = { version = "0.2.3", features = ["rules-spirv"], host-module = true }
```

`[build-dependencies]`, not `[dependencies]`. The two keys answer separate
questions: `host-module = true` says which build-time product is wanted, and
the section says whether the package reaches the target. A rule package answers
"no" to the second -- its library must never be linked into the artifact while
its rule is still needed -- which is the case docs/05 section 2.6.1 exists for.
Writing it under `[dependencies]` also works, which is precisely why the
distinction has to be stated rather than left to a failure to teach.

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
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.9.6.5 | `xim:cann-toolkit` in `[xlings.workspace]`, `[build] accel = "ascend8.5+{dav-c220}"`, a constrained glob for `*.asc`. Compiles with BiSheng in MIXED mode, so the object carries the device binary and a host-callable launcher and joins the ordinary link -- no registration file and no device-link step. The floor is the release whose device-source table carries `.asc` and whose `mcpp::link_flag` can emit the `-rpath-link` the toolkit's own shared libraries need |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.9.5.2 | the toolkit named in `[xlings.workspace]` (`xim:cuda-nvcc`, `xim:cuda-cudart`, and `xim:libcurand` for the clang route, whose wrapper includes a cuRAND header unconditionally), `[build] accel = "cuda…"`, a constrained glob for `*.cu`; the clang route with an LLVM toolchain, the nvcc route with a GCC one |
| `rules-hip` | `mcpp.rules.hip` | 2026.9.5.2 | `xim:hip-nvidia` plus the CUDA back end it compiles through (`xim:cuda-nvcc`, `xim:cuda-cudart`, `xim:libcurand`, `xim:cuda-cccl`), `[build] accel = "hip, cuda12.9+{sm_89}"`, a constrained glob for `*.hip`. On the NVIDIA platform HIP is a header layer over the CUDA runtime, so the compiler is the project's own clang and there is no ROCm on the machine |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.9.5.3 | `xim:glslang` or `xim:shaderc` in `[xlings.workspace]`, `[build] accel = "vulkan1.2"`, a constrained glob for the shader stages; emits one header per shader through a `role = "source"` action, and states which of the two compilers produced it |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.9.6.1 | `xim:dpcpp` (the compiler), `xim:gcc` (the C++ standard library the unit compiles against, not a second toolchain) and `xim:cuda-nvcc` for an NVIDIA target; `[build] accel = "sycl"` or `"sycl, cuda12.9+{sm_89}"`, a constrained glob for `*.sycl`, and `compat:sycl-runtime` so the artifact can reach `libsycl.so.9` at run time. The floor is the release whose device-source table carries `.sycl` |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | nothing beyond mcpp: it reads a file and writes a header while the build program runs. The floor is the release whose fast path compares a declared file input, without which an edit to the data does not reach the binary |

### Each rule takes the extensions it claims

`mcpp::device_sources()` is the package's WHOLE device set, not one rule's
share of it, and every rule in a build program reads the same variable. A
project with two backends puts a `.cu` and a `.comp` in that one list.

Each rule therefore selects the extensions it claims and leaves the rest:

| feature | claims |
|---|---|
| `rules-ascendc` | `.asc`, `.cce` |
| `rules-cuda` | `.cu` |
| `rules-hip` | `.hip` |
| `rules-sycl` | `.sycl` |
| `rules-spirv` | `.comp .vert .frag .geom .tesc .tese .mesh .task .rgen .rint .rahit .rchit .rmiss .rcall`, and `.glsl` / `.hlsl` so that a stage-less name is refused by name rather than by absence |

A rule whose backend this build does not name returns immediately, so a build
program may call every rule it imports unconditionally and `--no-accel`
compiles nothing.

A device source that NO rule claims is not silently dropped: mcpp refuses a
device source that reached no action, naming the file. That is the engine's
half of this rule and it needs 2026.9.6.5.

The floor is the mcpp release whose engine carries what the member relies on:
`rules-spirv` needs the device-source table that classifies shader extensions,
which 2026.9.5.3 introduced; `tools-embed` needs the fast path to compare a
declared file input, which 2026.9.5.4 introduced; and `rules-sycl` needs `.sycl`
in that same device-source table, which 2026.9.6.1 introduced. The index
descriptor states the highest floor among the members, so it is the floor of the
collection rather than of any one feature; a project on an older mcpp is refused
at resolution rather than at the first shader.

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

## What a rule may drive, and what it may not

A member drives a compiler the ecosystem resolved and no other. `xim:dpcpp` is
`mcpp.rules.sycl`'s compiler; `xim:gcc` is the C++ standard library it compiles
against; `xim:cuda-nvcc` is the back end it emits for. None of the three is a
default: each is read from `mcpp::xpkg_dir`, and a member that cannot find one
refuses and prints the `[xlings.workspace]` line that would add it.

Two of those three were added because a build that already worked was found to
be reading the host. Without `--gcc-install-dir` the SYCL unit compiled against
`/usr/include/c++`; without `--cuda-path` clang found the host's CUDA
installation. Neither said anything: both are visible only in the compiler's
own include search list, and only on a machine that has those directories.

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
