# mcpp-plugins

The build plugins the mcpp project maintains, published as one package,
`mcpp:plugins`. A consumer selects the members it needs through features and
imports each one from `build.mcpp` under the module name the member declares.

```toml
[build-dependencies.mcpp]
plugins = { version = "0.6.0", features = ["rules-spirv"], host-module = true }
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
| dist | `mcpp.dist.<x>` | what comes out of the link, and in what form a user installs it: an `.msi`, an AppImage, a signed `.app` |
| identity | `mcpp.plugins` | the lib root, compiled before every member; it states the collection's version |

The three families answer three different questions, and the prefix is which
one a member answers:

```
rules-*   how is this translation unit compiled
tools-*   what does the build program need to do itself
dist-*    what comes out of the link, and in what form a user installs it
```

A `dist-*` member fits neither of the first two definitions: it does not
compile a translation unit and it does not do its work while the build program
runs. It consumes **link outputs** through a `role = "artifact"` action, reached
with `mcpp pack --format <name>` (mcpp 2026.9.11.1+). The prefix matters because
the taxonomy is load-bearing -- a consumer reading `rules-wix` would expect a
compiler it does not drive and a translation unit, and there is neither.

The `mcpp.` prefix is reserved for this package: mcpp warns when a module under
it is declared by a package outside the `mcpp` namespace. `mcpp.build.*` is the
engine's own module family and is not used here.

## Members

| feature | module | since mcpp | what it needs |
|---|---|---|---|
| `rules-ascendc` | `mcpp.rules.ascendc` | 2026.9.6.6 | `[build] accel = "ascend8.5+{dav-c220}"`, a constrained glob for `*.asc`. Compiles with BiSheng in MIXED mode, so the object carries the device binary and a host-callable launcher and joins the ordinary link -- no registration file and no device-link step. Its own engine needs are `.asc` in the device-source table and `mcpp::link_flag` for the `-rpath-link` the toolkit's shared libraries require, both 2026.9.6.5 |
| `rules-cuda` | `mcpp.rules.cuda` | 2026.9.6.6 | `[build] accel = "cuda…"`, a constrained glob for `*.cu`; the clang route with an LLVM toolchain, the nvcc route with a GCC one |
| `rules-hip` | `mcpp.rules.hip` | 2026.9.6.6 | `[build] accel = "hip, cuda12.9+{sm_89}"`, a constrained glob for `*.hip`. On the NVIDIA platform HIP is a header layer over the CUDA runtime, so the compiler is the project's own clang and there is no ROCm on the machine |
| `rules-slang` | `mcpp.rules.slang` | 2026.9.7.1 | `[build] accel = "vulkan1.2"`, a constrained glob for `*.slang`. Slang is a different language from GLSL rather than a second driver for it -- its own module system, generics, and targets beyond SPIR-V -- so it is a rule of its own. `.slang` is **not** in the engine's device-source table: this feature declares `device_extensions = [".slang"]` and `rule_module = "mcpp.rules.slang"`, and the engine routes it from there. That is the criterion for the whole arrangement -- a new device language costs no engine release |
| `rules-spirv` | `mcpp.rules.spirv` | 2026.9.6.6 | `[build] accel = "vulkan1.2"`, a constrained glob for the shader stages; compiles each shader through a `role = "source"` action and states which of the two compilers produced it |
| `rules-sycl` | `mcpp.rules.sycl` | 2026.9.6.6 | `[build] accel = "sycl"` or `"sycl, cuda12.9+{sm_89}"`, a constrained glob for `*.sycl`, and `compat:sycl-runtime` so the artifact can reach `libsycl.so.9` at run time. Its own engine need is `.sycl` in the device-source table, 2026.9.6.1 |
| `tools-embed` | `mcpp.tools.embed` | 2026.9.5.4 | nothing beyond mcpp: it reads a file and writes a header while the build program runs. The floor is the release whose fast path compares a declared file input, without which an edit to the data does not reach the binary |
| `tools-island` | `mcpp.tools.island` | 2026.9.7.1 | nothing beyond mcpp: it reads marked entry points out of an island's own source and writes the `extern "C"` boundary header its compiler reads and the module the C++ side imports. Not a device rule -- it claims no extension, and a project calls it from its own `build.mcpp` |
| `dist-appimage` | `mcpp.dist.appimage` | 2026.9.11.1 | `xim:appimagetool`, which this feature declares on the `cfg(linux)` axis. Linux only. Turns the tree `mcpp pack` staged into one AppImage: the staged bundle is already an AppDir bar three files, so the member writes an `AppRun`, a `.desktop` entry and an icon into it and invokes one tool -- it never copies or re-lays-out a tree that can be hundreds of megabytes |
| `dist-wix` | `mcpp.dist.wix` | 2026.9.11.1 | the WiX 6 CLI on `PATH` or in `MCPP_WIX`, which is not redistributable through this ecosystem and is therefore located rather than installed -- the `msvc@system` shape. Windows only. Renders a `.wxs` and passes the program in as a preprocessor variable, because a bind path that resolves to nothing is silent |
| `dist-apple` | `mcpp.dist.apple` | 2026.9.11.2 | the base macOS install (`ditto`, and `codesign` only when an identity is given). macOS now; iOS when the target row is wired, which is a payload rather than a redesign. **The floor is one release higher than its siblings** and the reason is not this member: under 2026.9.11.1 `mcpp pack` staged before dispatching and let a staging failure fail the command, so on a Mach-O program -- which the built-in closure walk refuses, because it uses `LD_TRACE_LOADED_OBJECTS` and dyld answers that by running the program -- every dispatched format was unreachable, including one that reads no staged tree. 2026.9.11.2 makes staging a service to the provider |

### Each rule brings its own environment

A project names the rule and nothing else:

```toml
[build-dependencies.mcpp]
plugins = { version = "0.6.0", features = ["rules-cuda"], host-module = true }
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
collect. `tests/spirv-sidecar` asserts both halves: found from the root, and
reported missing from `/tmp`.

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

## `tools-embed`'s four entry points

| entry point | inputs | outputs | shape |
|---|---|---|---|
| `file()` | one | one header | one array, one `_size`, included by name |
| `files()` | N | N headers | one `file()` call per input; `options::identifier` is refused, because it names one symbol and there are several |
| `group()` | N | N headers + one generated interface | the same N headers, handed to `mcpp::plugins::surface` so a consumer writes one `import` and names no generated file -- see "What a consumer names" above |
| `table()` | N | one header | one array of rows, each carrying its input's key beside its bytes; the consumer iterates or looks a row up by key |

`table()` is for a set the consumer wants to walk rather than name member by
member, where `files()`'s one accessor per input and `group()`'s one function
per input are both the wrong shape. The motivating case is a shader set:

```cpp
mcpp::tools::embed::table_options opt;
opt.name_space = "myapp";
opt.identifier = "shaders";
opt.row_type   = "shader_entry";
mcpp::tools::embed::table({ "shaders/Standard.vert", "shaders/Standard.frag" }, opt);
```

which produces, in one header, a struct and an array of it:

```cpp
struct shader_entry { const char* key; const unsigned char* data; std::size_t size; };
inline constexpr shader_entry shaders[] = {
    { "Standard.vert", /* ... */, /* ... */ },
    { "Standard.frag", /* ... */, /* ... */ },
};
```

**The row struct is generated beside the array it describes, for the reason
0.2.6 fixed for `mcpp.rules.spirv`'s header:** a generated header has to be
includable on its own with nothing else. A consumer never declares the row
type by hand, so it cannot declare one that has drifted from what the array
actually holds.

**Each row's bytes are a numeric array, never a raw string literal.** A raw
string literal delimits on a fixed marker (`)"` closes `R"(...)"`), and no byte
sequence in an arbitrary payload is excluded strongly enough to promise it
never contains that marker: shader source can carry it by accident, and a
binary payload can carry it by construction. The motivating case for this
entry point built its shader table by concatenating file contents into one
string in a build script -- exactly this bug: a shader containing that
four-character sequence truncates the string at that point, and every shader
concatenated after it goes missing, with nothing but an unrelated compiler
error to show for it.

**A row's key is a choice, not a convention.** `table_options::key` selects
between the file name with its extension (`Standard.vert`, the default,
because a bare stem would collide with `Standard.frag`), the bare stem, and the
path relative to the manifest directory -- for a nested input set where two
directories hold a file of the same name, which the file name alone cannot
tell apart. Two inputs that derive one key are refused, naming both: the same
shape `mcpp.rules.spirv` refuses two shaders sharing one output name, because
the alternative is a table that silently keeps the last row sharing a key and
drops the rest, and a build that did that would still link and run.

**`table_options` is its own type rather than a second meaning for `options`'s
fields.** `options::identifier` names one symbol, so `files()` refuses a caller
who sets it for several inputs rather than silently applying it to the first.
A table writes exactly one array regardless of how many inputs feed it, so
there is always exactly one name to give: `table_options::identifier` (default
`embedded_table`) and `table_options::row_type` (default `embedded_file`) name
the array and the struct. `out_dir`, `name_space`, `elem` and `width` mean what
they mean in `options`, `element::word32`'s "not a multiple of 4" refusal
included -- checked per input, since a table has several.

## `tools-island`: an island's boundary

`mcpp::plugins::surface` generates the whole interface for a **data** payload,
because an address and a size are all there is to decide.
`mcpp.tools.island` generates what is mechanical about a **code** island -- and
only that. It is a member like `tools-embed` rather than part of the lib root:
nothing in this collection uses it, a project does.

The entry points are marked where they are defined, and the signature exists
once:

```c
// src/kernels/saxpy.cu -- no include: the generated header arrives through the
// compiler's forced-include flag, which is what defines the marker as nothing.

MCPP_EXPORT_C
int saxpy_device(float a, const float* x, const float* y, float* out, unsigned n) { ... }
```

```cpp
// build.mcpp
mcpp::tools::island::options opt;
opt.module_name  = "myapp.kernels";
opt.out_dir      = std::string(mcpp::out_dir()) + "/island";
opt.roots        = { root + "/src/backends/cuda", root + "/src/backends/cpu" };
opt.layout_root  = root + "/src/backends/cuda";   // default: roots.front()
opt.strip_prefix = "myapp_";                       // optional short spelling

const auto entries = mcpp::tools::island::scan(opt);
const auto out     = mcpp::tools::island::emit(*entries, opt);
mcpp::include_dir(out->include_dir.c_str());
mcpp::generated(out->interface_file.c_str());
```

Two files come out of that one marked declaration: the `extern "C"` header the
device translation unit includes, guards and `__cplusplus` dance included, and
the module the C++ side imports.

**The names arrive in the module's own namespace (0.5.0).** `docs/42` states one
rule for both lanes -- the module name and the namespace are one identifier path
-- and this generator did not follow it: every entry point was at global scope,
so `import myapp.kernels` bought a file name and nothing else. It follows it
now, and a directory below the layout root extends the path exactly as a payload
tree's does:

```
src/backends/cuda/image/blur.cu   myapp_blur   ->  myapp::kernels::image::myapp_blur
src/backends/cuda/saxpy.cu        myapp_saxpy  ->  myapp::kernels::myapp_saxpy
```

**A root is a tree, and one of them supplies the shape.** Overlapping roots are
refused: a file reachable from two of them has two namespace paths, and which
one it got would depend on the order of the list. `options::roots` names
the directories implementations live under; `options::layout_root` names the one
whose directory structure decides where entry points live, and defaults to the
first. Every other root only has to define the names, so a fallback tree may be
one flat file or six directories and may be reorganised without renaming
anything a consumer wrote. This is a naming role and not a rank: every root
compiles, links, and is equally a backend.

A file list would not do: its common ancestor moves when a file is added, and a
consumer's qualified name would move with it. Roots also must not come from
`mcpp::device_sources()`, which `accel` narrows to nothing under `--no-accel` --
the device tree would vanish and the namespace would come from the fallback.

`scan` registers every file it reads as a declared input and each root as a
glob, so **adding** a file re-runs the program. Declared inputs are hashed
contents, and a new file changes none of them.

**A short spelling, when the prefix repeats the namespace.** An island's symbol
is global to the whole program, so an entry point carries a package prefix
whether or not it sits in a namespace. `options::strip_prefix` emits a second
spelling beside the first:

```cpp
export namespace myapp::kernels::image {
using ::myapp_blur;                          // the authored name; this is the symbol
inline constexpr auto blur = myapp_blur;     // the short name, for the call site
}
```

Both are exported and the authored one stays canonical -- it is what `nm`, a
link error, a profiler and `dlsym` show. A `constexpr` function pointer costs
the artifact nothing: the pair is one symbol. A short name that collides with
another entry point's name in the same namespace is refused, naming both.

The C++ side is usually a **seam module** of the project rather than a consumer
directly: `app.cppm` imports the generated module and turns pointers and a count
back into spans, and it is the one place a backend can be exchanged. That means
one module interface of the project imports a module interface written into the
build directory during the same build; the ordering comes from the scan seeing
the import, and nothing has to be declared for it.

**Three layers, and each overrides the one above.** `scan` reads the marked
declarations out of the roots, which puts the signature beside the definition;
`island::declared` builds an entry from a declaration the scan cannot see, for
`emit` to take directly; and a project that wants neither writes its own header
and its own module wrapper. The default is the one that keeps the signature in
one place.

**The marker selects.** An island has internal functions, and a generator that
exported whatever the file contained would make the boundary an accident of the
file's contents. `MCPP_EXPORT_C` names the mechanism rather than the domain --
what is marked is exported across a generated boundary with C linkage -- and
deliberately does not end in `_API`, a suffix that conventionally expands to a
visibility attribute where this expands to nothing. It is configurable through
`options::marker`.

**The scan is not a C parser.** From the marker it copies verbatim to the
parenthesis closing the parameter list, matching nesting, so a signature that
wraps across lines or carries a macro travels through unexamined.

**The island writes no `#include` either.** `force_include_flags` returns the
flags that make the compiler read the generated header before the island's first
line -- `-include <path>` for gcc and clang, `/FI<path>` for MSVC -- so a project
using this generator has no header in its source tree and no line naming one.

**Those flags go to the RULE, not to the project.** The island is compiled by a
driver mcpp did not invoke, which inherits nothing from `mcpp::cflag` or
`mcpp::cxxflag`, so each device rule takes them on its own command line:

```cpp
mcpp::rules::cuda::options opt;
opt.flags = mcpp::tools::island::force_include_flags(out->header_file,
                                                     mcpp::compiler());
```

`cuda`, `hip`, `sycl` and `ascendc` all carry `options::flags`, appended last and
passed through unexamined. The project-wide channels are not merely too wide for
this, they are wrong for it: `cxxflag` forces the header into every C++
translation unit, including the seam `.cppm`, and a module interface unit must
begin with `export module` -- declarations ahead of that line are ill-formed.

A HOST fallback compiled by mcpp itself has no such command line, so it writes
one ordinary `#include` of the generated header. The asymmetry is between "a
compiler this project can flag on its own" and "every C++ translation unit",
not between the two halves' importance.

An `#include` of the generated header would name a file its author never opens,
and it buys no self-containment: that translation unit could not be compiled
outside mcpp with or without the line, because the file it names does not exist
until mcpp writes it.

**The check that include used to do is still there.** The compiler sees the
declarations, so a definition whose signature drifted from its declaration fails
where it was written rather than at the link:

```
src/kernels/saxpy.c:22:5: error: conflicting types for 'scale_device'
```

**Two checks, and they answer different questions.**

One name declared twice in ONE root is a collision. C language linkage does not
mangle, so those are one symbol, and a namespace that appeared to separate them
would promise an isolation the linker does not provide -- measured: two modules
re-exporting one `extern "C"` name into two namespaces give `&a::f == &b::f`.
It is refused, naming both files and saying that two implementations of one
entry point belong in two roots. That refusal is what makes the namespaces
honest: a name exists in exactly one of them.

One name in SEVERAL roots is one entry point implemented several times, which is
the ordinary shape of a seam -- a device island and a host fallback, exactly one
of them in any link. Every root is read unconditionally, because all of them
exist on disk in either build and which one is compiled is the manifest's
decision rather than a condition the build program repeats. The declarations
must then agree verbatim, and two that declare it **differently** are refused,
naming both files and both signatures:

```
mcpp.tools.island: two definitions of `island_scale` declare it differently.
  src/kernels/image/scale.c
    int island_scale(float a, float* out, unsigned n)
  src/cpu/ops.c
    int island_scale(float a, float* out, double n)
  C language linkage does not mangle, so these never meet at the link:
  whichever one is in the artifact reads its arguments by its own signature.
```

Nothing else in the toolchain catches that. The two halves are never in one
translation unit and never in one link, and C linkage does not mangle, so a
build with disagreeing halves is clean and the artifact reads its arguments by
whichever signature it was compiled with. `scan` is the only point at which both
texts exist at once.

**A root that yields nothing is an error.** A misspelled path or a marker that
never arrived would otherwise produce a module exporting nothing, and the
failure would surface as an unresolved name in a consumer three files away.

**The declaration still exists once.** Without this, a project writes it twice --
in a header, and again wherever the C++ side reaches it. C language linkage does
not mangle, so two copies that disagree are one symbol: the link is clean and
each side reads the arguments by its own ABI, with no compile error and no link
error. That is the copy this removes.

**The module re-exports names, not signatures.** `using ::saxpy_device;` inside
the module's namespace needs the identifier and nothing else, so the generator
has no C parser in it and the header stays the only place a signature is
written. Measured on both
implementations this package supports: a consumer that imports the module and
never includes the header calls the entry point and links against an
implementation built by a different driver, under GCC 16.1 and clang 22.1.8.

**The C++ interface is still yours.** A seam that turns raw pointers into
`std::optional<std::vector<float>>` is a design decision, and no generator makes
it well. This removes the boilerplate around the boundary, not the boundary's
design. `emit_module = false` emits the header alone for a project that keeps a
hand-written seam that includes rather than imports.

### Why the generators live here and not in mcpp

The engine's `mcpp` module is compiled into the mcpp binary and carries the
**protocol** -- what a build program can tell mcpp: `action`, `generated`,
`include_dir`, `fact`, `floor`. A generator is a **library on top of** that
protocol: it reads declarations, writes files, and hands them back through
`mcpp::generated`. It extends nothing.

So the line is: the engine's module carries the protocol, and generators are
libraries. Both of these are opinionated and will change -- what a `payload`
looks like, how a namespace is derived, which storages exist -- and code inside
the engine changes only with an engine release, which is the coupling this
version's `device_extensions` work exists to remove.

A rule package outside this collection reaches them by depending on
`mcpp:plugins` and activating no feature: the lib root alone, one small module.
That is a package dependency it chooses, not a coupling the engine imposes. If
they stabilise, moving them into the engine later is a low-risk step; the reverse
is not.

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
dist/<x>.cppm      export module mcpp.dist.<x>;
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

1. One file, `rules/<x>.cppm`, `tools/<x>.cppm` or `dist/<x>.cppm`, declaring
   its module name.
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
