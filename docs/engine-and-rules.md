# How the members meet the engine

How a rule reaches the engine: each rule declares its own environment and the file extensions it claims, a consumer names a member through features and the declarations a payload generates, and the engine compiles each member as a host module. The section on what a rule may drive states the one constraint every member shares: it drives only a compiler the ecosystem resolved.

## Naming, in full

The four families answer four different questions, and the prefix is which
one a member answers:

```
rules-*   how is this translation unit compiled
tools-*   what does the build program need to do itself
dist-*    what comes out of the link, and in what form a user installs it
deps-*    where does a library come from
```

A `dist-*` member fits neither of the first two definitions: it does not
compile a translation unit and it does not do its work while the build program
runs. It consumes **link outputs** through a `role = "artifact"` action, reached
with `mcpp pack --format <name>` (mcpp 2026.9.11.1+). The prefix matters because
the taxonomy is load-bearing -- a consumer reading `rules-wix` would expect a
compiler it does not drive and a translation unit, and there is neither.

A `deps-*` member compiles none of the project's translation units and does not
do its work while the build program runs: the installation is a `prepare`
action (mcpp's SPEC-007, docs/specs/build-plugins.md), which fills a declared
output directory and which the package's compile and link edges wait for, and
whose command is the installer itself -- `vcpkg`, or `cmake -P` over a script
the member writes -- so the package builds no program for it. The build
program refers to the prefix by name -- include directory, libraries by full
path, runtime search directory -- and never by what is in it, so a plan made
before the installation is the plan made after it.

The `mcpp.` prefix is reserved for this package: mcpp warns when a module under
it is declared by a package outside the `mcpp` namespace. `mcpp.build.*` is the
engine's own module family and is not used here.

## Each rule brings its own environment

A project names the rule and nothing else:

```toml
[build-dependencies.mcpp]
plugins = { version = "0.19.0", features = ["rules-cuda"], host-module = true }
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

## Each rule takes the extensions it claims

`mcpp::device_sources()` is the package's WHOLE device set, not one rule's
share of it, and every rule in a build program reads the same variable. A
project with two backends puts a `.cu` and a `.comp` in that one list.

Each rule therefore selects the extensions it claims and leaves the rest:

| feature | claims |
|---|---|
| `rules-ascendc` | `.asc`, `.cce` |
| `rules-cuda` | `.cu` |
| `rules-hip` | `.hip` |
| `rules-metal` | `.metal` |
| `rules-qt` | `.ui`, `.qrc`, `.ts`; a header is found by what it declares, since `.h` is already C++ |
| `rules-slang` | `.slang` |
| `rules-swift` | `.swift` |
| `rules-sycl` | `.sycl` |
| `rules-spirv` | `.comp .vert .frag .geom .tesc .tese .mesh .task .rgen .rint .rahit .rchit .rmiss .rcall`, and `.glsl` / `.hlsl` so that a stage-less name is refused by name rather than by absence |

A rule whose backend this build does not name returns immediately, so a build
program may call every rule it imports unconditionally and `--no-accel`
compiles nothing.

A device source that NO rule claims is not silently dropped: mcpp refuses a
device source that reached no action, naming the file. That is the engine's
half of this rule and it needs 2026.9.6.5.

The floor is the mcpp release whose engine carries what the member relies on.
From 0.4.0 every rule shares one: **2026.9.8.1**, the release in which a
package's host modules are ordered by their IMPORT GRAPH rather than by their
paths. This package needs that: `src/declare.cppm` is imported by every member,
and `rules/` sorts before `src/`, so before that release the members were
compiled first and failed with "failed to read compiled module".

**The floor could have been avoided, and was not.** `src/declare.cppm` sorts
after `rules/`, which is exactly why it needs the ordering fix -- and naming it
`aa_declare.cppm` at the package root would make the old PATH order happen to be
correct, so 0.4.0 would run on 2026.9.7.1 with no floor move at all. That is
declined on purpose: it encodes a load-bearing constraint in a filename with
nothing enforcing it, which is the fragility the engine fix removes. A file
renamed for a reason nobody can see is a defect waiting for the rename that
looks harmless.

0.5.0, 0.5.1 and 0.5.2 do not move it. Naming an island's entry points is a
change to what this package generates, not to what it asks the engine for.

0.7.1 does not move it either, and records a compiler rather than an engine:
under MSVC 14.52 (36629 and 36725, measured on xrgui's CI) a module that has
instantiated `std::filesystem::path`'s iterator poisons every importer that
touches `path` again -- `filesystem(1572): error C2801: '_Path_iterator<...>::operator =='
must be a non-static member`. Nothing in this package instantiates that
iterator now: the lib root reads paths apart as strings
(`mcpp::plugins::names::components`), and the members' relative-path
arithmetic is `mcpp::plugins::names::relative_to`.

The previous shared floor was 2026.9.7.1, the release that reads
`device_extensions` and `rule_module`, reports `[language] modules` and the
package's own name to a build program, writes the build program a declared rule
set describes, and gives `mcpp::action` its `depfile` field. A client below it
does not get a degraded surface; it gets a build in which the rules never route
-- the file falls through to the ordinary source scan and mcpp says it has no
role for the extension.

The previous shared floor was 2026.9.6.6, the release in which a payload a
DEPENDENCY declared is both installed and answerable. Before it a rule could
declare `>=8.5.0`, have it installed, and still be told by `xpkg_dir` that
nothing was there -- which is why each rule's list used to be repeated in every
project that used it. The earlier per-member floors are still the floors of the
rules themselves (`rules-spirv` needs the shader extensions in the device-source
table, 2026.9.5.3; `tools-embed` needs the fast path to compare a declared file
input, 2026.9.5.4; `rules-sycl` needs `.sycl` in that table, 2026.9.6.1), and
they are all below the shared one.

`rules-slang` is the member that does NOT appear in that list, and its absence
is the point: `.slang` is in no engine table at any version. The feature
declares the extension and the module that compiles it, so the release it needs
is the one that reads those two keys rather than the one that would have carried
its extension.

0.10.0 moves the collection's floor to **2026.9.14.2**, and the reason is two
members rather than the rules: `dist-apk` and `dist-apple` read the native
closure the engine stages and names in the stage manifest's `needs` lines,
`dist-apple`'s framework rpath needs the engine to leave an `@executable_path`
rpath as written, and `mcpp run --format app` reaches the runner named after the
format only from that release. `rules-metal` itself needs nothing past
2026.9.8.1. An older engine is not left to fail silently: `dist-apk` refuses a
stage without `needs` lines, naming the release.

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

### From a file name to a call

Every name a consumer writes is derived, and derived one way, so nothing has to
be looked up:

```
base directory   shaders/                  derived: the shallowest directory
                                           every payload shares
file             shaders/post/tone.frag
                         └── the path below the base
module           myapp.shaders             package name + group
namespace        myapp::shaders::post      the module name segment by segment,
                                           then the directory's segments
identifier       tone_frag                 stem + stage, non-identifier
                                           characters replaced by `_`
call             myapp::shaders::post::tone_frag()
```

`myapp` comes from the package unless the project sets `options::module_name`;
the base directory is derived unless it sets `options::base_dir`.

Three invariants, and each exists because its absence was a defect:

- **The module name and the namespace are the same identifier path**, `.` for
  `::`. A reader never has to learn which namespace a module opens.
- **The directory reaches the generated file's path and the linker symbol, not
  only the namespace.** Two shaders sharing a stem in different directories
  produced byte-identical generated headers, and GCC's `#pragma once` treats two
  files with the same size and content as the same file -- so the second include
  did nothing and both accessors returned the first array, while the program
  printed the right magic number twice.
- **The stage is always part of the identifier**, so `blur.comp` and `blur.frag`
  do not collide. Uniformly rather than only when needed: conditional naming is
  worse than verbose naming.



**The interface is a function, and it names no standard-library type.** A
variable cannot keep one shape across the ways bytes can be stored, because
`constexpr` and `extern` are mutually exclusive. A std type in the interface is
worse than it looks: measured with GCC 16.1 on a 1 MB payload, an interface
returning `std::span` produced a 1 313 968-byte BMI against 1 808 bytes for the
std-free equivalent, and that cost is fixed rather than proportional to the
payload -- it is `<span>`'s templates, present whether the payload is 16 KB or
16 MB. A consumer that wants a `std::span` constructs one from the two members.

**Where the bytes live is a second, independent choice.** The surface decides how
a consumer names a payload; `storage` decides where it sits. The declarations are
identical under all three, so a project changes this and no consumer changes.

| storage | the payload is | reach for it when |
|---|---|---|
| `header` (default) | a C array in generated source, compiled in | almost always |
| `object` | a section, through `.incbin` in a generated `.S` | total payload is large |
| `sidecar` | a file beside the artifact, read at run time | hot reload, or a payload too large to link |

**Every payload a compile reads is in the build graph.** Two mechanisms carry
that, and which one applies is decided by WHEN the thing is known.

A shader's `#include` is discovered by the compiler while it runs, so it arrives
afterwards, in a depfile. `mcpp::action::depfile` carries it and all six rules
pass one -- each spelling measured against the tool rather than read from its
help text.

An `.incbin` is discovered by nobody. The assembler opens the file at assembly
time; the generated `.S`'s own text does not change when the payload does; the
object is assembled once. Measured on 0.3.0, in a sandbox against the published
packages: editing a shader left the program printing the previous payload's byte
count, with a green build.

Asking the assembler does not fix it, and that was measured rather than assumed.
The compiler driver's `-MD` is a preprocessor channel that never sees `.incbin`;
GNU as names it in its own `--MD`; clang's integrated assembler has no
dependency output of any kind. Tracking it that way would work under GCC and
fail silently under Clang -- worse than failing under both.

**So under `object` storage the generation is an ACTION and the payloads are its
declared inputs.** That is the one graph primitive the engine has, used for what
it is: a payload changes, the action reruns, its outputs count as new, and the
edge that assembles them reruns. The command is `mcpp-embed`, built from this
package through `tools = ["mcpp-embed"]` -- not published separately, because
docs/05 section 2.14 states what that costs: "the tool's version IS the
dependency's version, so a `protoc` that does not match its runtime is not
expressible."

`header` and `sidecar` need none of it, and that was checked rather than
assumed. Under `header` the bytes reach the artifact through generated data
headers the payload's own compiler already writes as action outputs; under
`sidecar` they are never compiled at all. So the default path builds no tool,
and a consumer that never opts into object storage writes nothing extra.

**Which one is a measurement, not a preference.** With GCC 16.1 on 100 payloads
of 16 KB each -- the size of an ordinary compute shader:

```
header route   compile 0.64s + link 0.44s                = 1.10s
object route   convert 1.28s + compile 0.44s + link 0.46s = 2.17s
```

The header route is faster, because at that size neither route has a measurable
marginal cost and the total is decided by how many processes start; one compiler
invocation absorbs many headers. The crossover is the TOTAL embedded byte count
rather than the payload count: below about 1 MB the header route wins, and above
about 4 MB the compiler's slightly superlinear curve loses by an order of
magnitude (2.31s against 0.116s). Source expansion is a constant 2.75x.

**`object` needs a GAS assembler.** Every gcc and clang toolchain has one on all
three platforms; MSVC does not, and mcpp refuses `.S` under it, so the emitter
falls back to `header` there and says so once. The surface does not change, so a
consumer compiled either way is the same source.

**`sidecar` states its cost rather than hiding it.** The accessor opens a path
relative to the working directory, so the program finds its payloads when run
from the package root and does not when run from elsewhere -- which is why it is
not the default, and why `mcpp pack` of such a program has something further to
collect. `tests/spirv-sidecar` and `tests/slang-sidecar` assert both halves: found
from the root, and reported missing from `/tmp`.

**The default follows the project.** `[language] modules = true` gives the
module surface, `false` gives a header with the same declarations. mcpp reports
the setting as `MCPP_LANGUAGE_MODULES`; an engine that does not report it leaves
the header surface in place, so an older engine keeps the behaviour every
consumer of this package had before the surface existed.

**The generator is not shader-specific, and three members already share it.**
`mcpp::plugins::surface` knows about a run of bytes, a name, where it lives and
how it is reached; it does not know what SPIR-V is. `rules-spirv`, `rules-slang`
and `tools-embed` all call it, which is what keeps their generated declarations
from drifting.

It lives in this package's lib root, so a rule package outside this collection
reaches it by depending on `mcpp:plugins` and activating no feature -- the lib
root alone, which is one small module. That works and is the intended path;
whether the generator should become a package of its own is an open question and
not one this version answers.


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

## Adding a member: what the engine owns and what the member owns

1. One file, `rules/<x>.cppm`, `tools/<x>.cppm`, `dist/<x>.cppm` or `deps/<x>.cppm`, declaring
   its module name.
   The engine owns the graph — the accelerator axis, the constrained globs,
   the action edges, the fingerprint — and the member owns the spelling: which
   tool, which flags, what is generated. A member does not read `/usr`; a tool
   comes from `mcpp::toolchain_dir()`, `mcpp::xpkg_dir()` or an explicit option,
   and a missing one is refused naming the `[xlings.workspace]` entry to add.
2. A feature in `mcpp.toml` adding that one source.
3. A consumer under `tests/` that builds through it and asserts on the
   artefact, and a row in the README's members table with the mcpp floor.
4. A version bump: mcpp identifies an installed package by `(name, version)`,
   so a changed payload under an unchanged version is not reinstalled.
