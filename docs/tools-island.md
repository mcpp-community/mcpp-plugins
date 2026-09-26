# `tools-island`

`mcpp.tools.island` reads the marked entry points of a code island's own source and writes the `extern "C"` boundary header the island's compiler reads and the module the C++ side imports.

## `tools-island`

Module `mcpp.tools.island`; engine floor: 2026.9.7.1.

**Needs and behaviour.** nothing beyond mcpp: it reads marked entry points out of an island's own source and writes the `extern "C"` boundary header its compiler reads and the module the C++ side imports. Not a device rule -- it claims no extension, and a project calls it from its own `build.mcpp`

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
