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
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.9.6.6 | `[build] accel = "ascend8.5+{dav-c220}"`, a constrained glob for `*.asc`. Compiles with BiSheng in MIXED mode, so the object carries the device binary and a host-callable launcher and joins the ordinary link -- no registration file and no device-link step. Its own engine needs are `.asc` in the device-source table and `mcpp::link_flag` for the `-rpath-link` the toolkit's shared libraries require, both 2026.9.6.5 |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.9.6.6 | `[build] accel = "cuda…"`, a constrained glob for `*.cu`; the clang route with an LLVM toolchain, the nvcc route with a GCC one |
| `rules-hip` | `mcpp.rules.hip` | 2026.9.6.6 | `[build] accel = "hip, cuda12.9+{sm_89}"`, a constrained glob for `*.hip`. On the NVIDIA platform HIP is a header layer over the CUDA runtime, so the compiler is the project's own clang and there is no ROCm on the machine |
| `rules-slang` | `mcpp.rules.slang` | see below | `[build] accel = "vulkan1.2"`, a constrained glob for `*.slang`. Slang is a different language from GLSL rather than a second driver for it -- its own module system, generics, and targets beyond SPIR-V -- so it is a rule of its own. Its engine need is `.slang` in the device-source table |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.9.6.6 | `[build] accel = "vulkan1.2"`, a constrained glob for the shader stages; compiles each shader through a `role = "source"` action and states which of the two compilers produced it |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.9.6.6 | `[build] accel = "sycl"` or `"sycl, cuda12.9+{sm_89}"`, a constrained glob for `*.sycl`, and `compat:sycl-runtime` so the artifact can reach `libsycl.so.9` at run time. Its own engine need is `.sycl` in the device-source table, 2026.9.6.1 |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | nothing beyond mcpp: it reads a file and writes a header while the build program runs. The floor is the release whose fast path compares a declared file input, without which an edit to the data does not reach the binary |

### Each rule brings its own environment

A project names the rule and nothing else:

```toml
[build-dependencies.mcpp]
plugins = { version = "0.2.4", features = ["rules-cuda"], host-module = true }
```

The payloads each rule drives are declared **here**, under the feature that
selects the rule and the accelerator it serves:

```toml
[target.'cfg(accelerator = "cuda")'.feature-xlings.rules-cuda]
"xim:cuda-nvcc"   = "12.9.86"
"xim:cuda-cudart" = "12.9.79"
```

Two gates, and both must open before a byte is downloaded. The feature says
whether the rule is wanted; the selector says whether this build compiles for
the device. A CPU-only build opens neither.

**The shape of each default is a judgement about coupling.** An exact version
where the payload is coupled to something the rule cannot see -- a CUDA runtime
must not be newer than the driver it will meet, so the 12.9 line is offered and
a project with newer machines names 13.x itself. A floor (`>=`) where no such
coupling exists: a shader compiler, a SYCL compiler, a CANN toolkit.

mcpp reads the difference. A bare version is a **choice**, so a project pinning
a different one wins and the override is reported; a `>=` is a **requirement**,
so a project pinning below it is refused naming both sides. Either way one
version is installed. To override:

```toml
[target.'cfg(accelerator = "cuda")'.xlings.workspace]
"xim:cuda-nvcc" = "13.3.33"
```

**What is not here:** anything the produced program chooses to run *on*. A
Vulkan ICD (`xim:mesa-lavapipe`) is a device, and a rule that declared one would
force a software renderer onto consumers that have a GPU. The runtime adapters
(`compat:cuda-runtime`, `compat:sycl-runtime`, `compat:vulkan-runtime`) stay in
the project for that reason and for a second one: this package is reached
through a `[build-dependencies]` edge, so its own `[dependencies]` deliberately
do not reach the consumer's target.

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
| `rules-slang` | `.slang` |
| `rules-sycl` | `.sycl` |
| `rules-spirv` | `.comp .vert .frag .geom .tesc .tese .mesh .task .rgen .rint .rahit .rchit .rmiss .rcall`, and `.glsl` / `.hlsl` so that a stage-less name is refused by name rather than by absence |

A rule whose backend this build does not name returns immediately, so a build
program may call every rule it imports unconditionally and `--no-accel`
compiles nothing.

A device source that NO rule claims is not silently dropped: mcpp refuses a
device source that reached no action, naming the file. That is the engine's
half of this rule and it needs 2026.9.6.5.

The floor is the mcpp release whose engine carries what the member relies on.
From 0.2.4 every rule shares one: **2026.9.6.6**, the release in which a payload
a DEPENDENCY declared is both installed and answerable. Before it a rule could
declare `>=8.5.0`, have it installed, and still be told by `xpkg_dir` that
nothing was there -- which is why each rule's list used to be repeated in every
project that used it. The earlier per-member floors are still the floors of the
rules themselves (`rules-spirv` needs the shader extensions in the device-source
table, 2026.9.5.3; `tools-embed` needs the fast path to compare a declared file
input, 2026.9.5.4; `rules-sycl` needs `.sycl` in that table, 2026.9.6.1), and
they are all below the shared one.

The index descriptor states the highest floor among the members, so it is the
floor of the collection rather than of any one feature; a project on an older
mcpp is refused at resolution rather than at the first shader.

## What a consumer names

A member that embeds a payload -- `rules-spirv`, `rules-slang`, `tools-embed` --
does not leave the consumer to include a generated header. All three hand their
payloads to one generator, `mcpp::plugins::surface`, so what a consumer writes
is the same whichever produced them:

```cpp
import myapp.shaders;

const auto s = myapp::shaders::blur_comp();
VkShaderModuleCreateInfo ci{ .codeSize = s.size_bytes, .pCode = s.code };
```

**The module name and the namespace are one identifier path.** `myapp.shaders`
gives `myapp::shaders`, and a payload's directory below the globbed tree adds a
segment: `shaders/post/tone.comp` is `myapp::shaders::post::tone_comp()`. The
name is derived from the package unless the project sets `options::module_name`.

**The interface is a function, and it names no standard-library type.** A
variable cannot keep one shape across the ways bytes can be stored, because
`constexpr` and `extern` are mutually exclusive. A std type in the interface is
worse than it looks: measured with GCC 16.1 on a 1 MB payload, an interface
returning `std::span` produced a 1 313 968-byte BMI against 1 808 bytes for the
std-free equivalent, and that cost is fixed rather than proportional to the
payload -- it is `<span>`'s templates, present whether the payload is 16 KB or
16 MB. A consumer that wants a `std::span` constructs one from the two members.

**The default follows the project.** `[language] modules = true` gives the
module surface, `false` gives a header with the same declarations. mcpp reports
the setting as `MCPP_LANGUAGE_MODULES`; an engine that does not report it leaves
the header surface in place, so an older engine keeps the behaviour every
consumer of this package had before the surface existed.

**One copy of the bytes.** A generated data header declares a `static` array, so
before this every translation unit that included one carried its own copy.
Exactly one translation unit -- the generated implementation -- includes them
now, and every consumer reaches the same array through the accessor.

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
src/plugins.cppm   export module mcpp.plugins;  the lib root: the version, and
                   mcpp::plugins::surface, which every member that embeds a
                   payload uses to write the declarations a consumer names
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
