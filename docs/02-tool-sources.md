# Where a tool comes from

A member that runs a program — cmake, vcpkg, glslangValidator, nvcc, appimagetool
— answers one question before it can plan anything: which program. This page is
the account of that question for the three people it concerns: whoever writes a
plugin, whoever designs one, and whoever uses one. It is written as scenarios,
because each audience meets the mechanism from a different side.

The mechanism is mcpp 2026.10.1.3 and `mcpp.plugins.tool` (0.19.0). The engine's
own account is mcpp `docs/23-the-project-environment.md`; the design record is
`.agents/docs/2026-10-01-ecosystem-build-plugin-framework-design.md`.

## 1. The four places an answer can come from

```
1. the build program's choice    o.cmake = "/usr/bin/cmake"
                                 o.cmake = tool::root("/opt/cmake")
                                 o.cmake = tool::on_path()
2. the member's legacy variable  MCPP_SLANGC, MCPP_GLSLC (kept, never extended)
3. the engine's override         [xlings.overrides] in the root or workspace manifest
                                 MCPP_XLINGS_OVERRIDE_<NS>_<NAME>
                                 ~/.mcpp/config.toml
4. the declared payload          installed with the feature, or declared
                                 `provision = "on-request"` and asked for here
```

Two rules hold the whole mechanism together.

**A choice never asks for the payload.** The first level that answers returns,
and only the fourth calls `mcpp::xpkg_request`. That is what makes "the build
program names its own tool" mean "the payload is not downloaded"; it is a
property of the code path, not a convention.

**The environment outranks the manifest, and the manifest outranks the machine.**
CI and distribution packaging have to replace a tool without editing the
manifest, and a project's statement is more specific than a fact about one
machine. Every level names itself in the output, so an environment variable
quietly displacing a project's decision is not possible.

An override is read from the root manifest only — or the workspace manifest, when
a member is built. A dependency that writes the table is refused and named:
**which payloads a package needs is its own statement; where they come from is the
project's.**

## 2. Five classes, and why the output distinguishes them

| class | meaning | how a build reports it |
|---|---|---|
| `managed` | the ecosystem resolved a range and installed it | exactly as before this mechanism existed |
| `pinned` | the ecosystem installed it, at a stated version | the same |
| `custom` | a person or a machine stated where it comes from, with a version | `Using …  [custom · mcpp.toml:22]`, bright cyan |
| `program` | the build program named the program | `Using cmake (mcpp.deps.cmake) ← /usr/bin/cmake  [program · build.mcpp:9]` |
| `host` | stated without a version (a bare name on PATH) | `[host · PATH, version not stated]`, yellow verb |

The line the classes draw is single: **did the ecosystem choose, or did a person
or a machine choose.** `managed` and `pinned` are together the default, and the
default's output is unchanged — that is the criterion for a seamless upgrade, and
the engine's e2e cases and the framework lab assert it.

`host` is its own class because it binds the artifact to the machine's state. The
test is whether a version was stated, not whether the path looks like a system
directory, which would be a guess.

## 3. Replacing the default tool from `build.mcpp`

This is the chapter for a project that wants its own tool rather than the one the
plugin declares, stated in the build program and nowhere else -- no
`[xlings.overrides]`, no environment variable. Chapter 4 covers those.

### 3.1 Four ways, each a whole build program

Every example here is a complete `build.mcpp` that compiles. A member reads one
option (section 3.6 is the index); what differs is what the project states.

**A program this machine already has.** The plainest form: a string assigns to the
option, so a project written before 0.19.0 keeps compiling unchanged.

```cpp
// build.mcpp -- features = ["deps-vcpkg"]
import std;
import mcpp;
import mcpp.deps.vcpkg;

int main() {
    mcpp::deps::vcpkg::options o;
    o.libraries = { "fmt" };
    o.vcpkg     = "/opt/vcpkg/vcpkg";
    return mcpp::deps::vcpkg::use(o) ? 0 : 1;
}
```

**A tree, with the member looking inside it.** `tool::root` states a directory and
lets the member apply its own layout: `deps-cmake` looks for `cmake` in `bin` and
in `CMake.app/Contents/bin`, `deps-vcpkg` directly under the root.

```cpp
// build.mcpp -- features = ["deps-cmake"]
import std;
import mcpp;
import mcpp.deps.cmake;
import mcpp.plugins.tool;

int main() {
    mcpp::deps::cmake::options o;
    o.source    = "greet";
    o.libraries = { "greet" };
    o.cmake     = mcpp::plugins::tool::root("/opt/cmake-3.31.6");
    return mcpp::deps::cmake::use(o) ? 0 : 1;
}
```

**Whatever is on PATH, as a stated choice.** A member never falls back to PATH on
its own; `tool::on_path()` is how a project asks for that, so the log records that
the build depends on the machine.

```cpp
// build.mcpp -- features = ["rules-spirv"]
import std;
import mcpp;
import mcpp.rules.spirv;
import mcpp.plugins.tool;

int main() {
    mcpp::rules::spirv::options o;
    o.compiler = mcpp::plugins::tool::on_path();
    return mcpp::rules::spirv::compile(o) ? 0 : 1;
}
```

**A root that is the whole toolkit.** `rules-cuda` takes one tree holding nvcc, the
runtime, CCCL and cuRAND, rather than a program:

```cpp
// build.mcpp -- features = ["rules-cuda"]
import std;
import mcpp;
import mcpp.rules.cuda;
import mcpp.plugins.tool;

int main() {
    mcpp::rules::cuda::options o;
    o.toolkit = mcpp::plugins::tool::root("/usr/local/cuda-12.9");
    return mcpp::rules::cuda::compile(o) ? 0 : 1;
}
```

`rules-qt`, `rules-ascendc` and `dist-apk` state a root with a plain string,
because their option has always been a directory:

```cpp
// build.mcpp -- features = ["rules-qt"]
import std;
import mcpp;
import mcpp.rules.qt;

int main() {
    mcpp::rules::qt::options o;
    o.modules = { "Core", "Widgets" };
    o.root    = "/opt/Qt/6.11.1/gcc_64";
    return mcpp::rules::qt::compile(o) ? 0 : 1;
}
```

**The spelling `deps-vcpkg` has had since 0.18.1** states the same thing as
`tool::root`, and is still read:

```cpp
o.vcpkg_root = "/opt/vcpkg";
```

`mcpp.plugins.tool` needs no extra feature: each `deps-*` and `rules-*` feature
implies `plugins-core`. It does need the `import` shown above, though -- a feature
makes a module available, not visible. Assigning a plain string needs no import;
naming `tool::root` or `tool::on_path` does, and without it the compiler says
`declaration of 'root' must be imported from module 'mcpp.plugins.tool' before it
is required`.

### 3.2 Where a relative path points

A relative path is relative to the package root, which is where a project keeps
a vendored copy:

```cpp
o.vcpkg = mcpp::plugins::tool::root("third_party/vcpkg");
```

### 3.3 Deciding inside the build program

**The choice is a value, so the decision is ordinary code.** A project that ships a
vendored copy and also honours what CI provides writes both, and registers the
variable so the build re-plans when it changes:

```cpp
// build.mcpp -- features = ["deps-vcpkg"]
import std;
import mcpp;
import mcpp.deps.vcpkg;
import mcpp.plugins.tool;

int main() {
    mcpp::deps::vcpkg::options o;
    o.libraries = { "fmt" };

    // the copy in the repository, relative to the package root
    o.vcpkg = mcpp::plugins::tool::root("third_party/vcpkg");

    // and the machine's own, where CI provides one
    if (const char* r = std::getenv("VCPKG_ROOT"); r && *r) {
        mcpp::rerun_if_env_changed("VCPKG_ROOT");
        o.vcpkg = mcpp::plugins::tool::root(r);
    }
    return mcpp::deps::vcpkg::use(o) ? 0 : 1;
}
```

Leaving the option untouched in some branch is also a decision: that branch takes
the ecosystem's `xim:vcpkg`. `mcpp::plugins::toolchain::env("VCPKG_ROOT")` is the
same two lines as the `getenv` pair, for a project that already enables
`plugins-toolchain`.

### 3.4 A stated choice that fails is not replaced

The member
refuses and names what it consulted, rather than falling back to the payload:

```
warning: my-app: mcpp.deps.vcpkg: no vcpkg.
  consulted: options::vcpkg = root("/opt/vcpkg") (no vcpkg there)
  …
  So this plan installs nothing.
```

That is deliberate. Replacing a decision that was made explicitly, and failed,
with a different source would make "was the one I named actually used" impossible
to answer from the output.

### 3.5 What makes this avoid the download

`deps-vcpkg` declares its payload
`provision = "on-request"`, so the choice is read before anything is provisioned.
For a member whose payload is eager -- `rules-cuda`'s toolkit, which the plan
reads a version out of -- naming the tool in `build.mcpp` does not prevent the
download, and `[xlings.overrides]` is the way out. Section 5.2 is the table.

```
       Using cmake (mcpp.deps.cmake) ← /usr/bin/cmake  [program · build.mcpp:9]
    Finished dev [unoptimized + debuginfo] in 6.4s · program: cmake (mcpp.deps.cmake)
```

Measured in `speak-agent/mcpp-framework-lab`, cold on three runners, against a
control that provisions the payload:

| platform | control | the host's cmake named | difference | `xim:cmake` |
|---|---|---|---|---|
| ubuntu-24.04 | 29.5 s | 6.4 s | 23.2 s | 61.9 MB downloaded, 207 MB installed |
| macos-15 | 19.6 s | 5.4 s | 14.8 s | 85.9 MB, 265 MB |
| windows-2022 | 20.0 s | 5.8 s | 14.2 s | 51.9 MB, 152 MB |

**What is saved is the one-time cost of provisioning into an `MCPP_HOME`, not a
per-build cost.** A fourth build — the control with the payload already installed
— took 6.4, 5.0 and 5.7 s, which is what naming the host's cmake costs. A CI with
a warm payload cache does not save this twice.


### 3.6 Which option, and what it looks for

Each member names its tool through one option, and states where it looks under a
root. A path assigned as a string is taken as the program itself in every case.

| member | option | program names | under a root it looks in | variable it still reads |
|---|---|---|---|---|
| `deps-vcpkg` | `options::vcpkg` (and `options::vcpkg_root`) | `vcpkg` | the root itself | -- |
| `deps-cmake`, `deps-archive` | `options::cmake` | `cmake` | `bin`, `CMake.app/Contents/bin` | -- |
| `rules-spirv` | `options::compiler` | `glslangValidator`, `glslang`; or `glslc` | `bin`, the root | `MCPP_GLSLANG`, `MCPP_GLSLC` |
| `rules-slang` | `options::compiler` | `slangc` | `bin`, the root | `MCPP_SLANGC` |
| `rules-sycl` | `options::compiler` | `clang++` | `bin`, the root | -- |
| `rules-cuda` | `options::toolkit` | `nvcc` | `bin`, the root | -- |
| `rules-hip` | `options::toolkit` | `hipcc`, `clang++` | `bin`, the root | -- |
| `rules-ascendc` | `options::toolkit` | a root, no program | -- | -- |
| `rules-qt` | `options::root`, `options::extra_roots` | a root, no program | -- | `QT_ROOT_DIR` |
| `dist-appimage` | `options::tool` | `appimagetool` | the root, `bin` | -- |
| `dist-wix` | `options::tool` | `wix` | `tool/tools/net6.0/any` | -- |
| `dist-apk` | `options::build_tools`, `options::platform`, `options::jdk`, `options::bundletool_dir`, `options::kotlin`, `options::coursier` | roots, no program | -- | -- |

A member whose option names a **root rather than a program** -- `rules-qt`,
`rules-ascendc`, `dist-apk` -- takes `tool::root(...)` or a plain directory path,
because what it needs is the tree, not one executable.

### 3.7 Confirming nothing was downloaded

```bash
MCPP_NO_AUTO_INSTALL=1 mcpp build     # success means no payload was asked for
mcpp why payload xim:vcpkg
```

```
sources:
  payload:xim:vcpkg  /opt/vcpkg/vcpkg
      program · build.mcpp:9  for my-app
      considered: options::vcpkg = root("/opt/vcpkg"); payload xim:vcpkg (not requested by this build)
```

`not requested by this build` is the written proof. On a machine that already
holds the payload this is the only reliable check, because the build would have
succeeded either way.

## 4. For whoever uses a plugin: the other ways

### 4.1 Write nothing

```toml
[build-dependencies.mcpp]
plugins = { version = "0.19.0", features = ["deps-cmake"], host-module = true }
```

The member resolves nothing stated, reaches the fourth level, finds `xim:cmake`
declared on request and not installed, asks for it, and the engine installs it and
runs the program again. The output is byte-for-byte what it was before this
mechanism existed.

One difference from 0.18.1: **a payload declared and not used is not installed.**
A build that never reaches the member — because it compiles only its own modules,
or because `--format` took another branch — downloads nothing for it.

### 4.2 State it in the manifest instead of the build program

```toml
[xlings.overrides]
"xim:cmake" = "/usr/bin/cmake"                                  # a path is a program
"xim:vcpkg" = { root = "/opt/vcpkg" }                           # a tree
"xim:slang" = { program = "slangc", version = "2026.14.1" }     # checked against every requirement
```

This path needs no support from the member: the engine removes the payload from
the provisioning set, `mcpp::xpkg_dir` answers the root the override implies, and
`mcpp::xpkg_source` answers `override`. **A plugin that has not migrated to
`mcpp.plugins.tool` benefits from it unchanged**, as long as it reads its
directory from `mcpp::xpkg_dir`.

A stated `version` is compared with every requirement a package of the graph
made, and a version below one is refused naming both sides. Without a `version`
nothing can be compared, and the build records a note naming the requirement that
went unchecked. The engine never runs a program to ask its version: every tool
spells `--version` differently, and that is the plugin's knowledge.

### 4.3 Replace a tool for one CI job

```bash
MCPP_XLINGS_OVERRIDE_XIM_VCPKG=/opt/vcpkg/vcpkg mcpp build
MCPP_XLINGS_OVERRIDE_XIM_CMAKE=path:cmake mcpp build     # looked up on PATH: the host class
```

### 4.4 Share one tool between every project on a machine

```toml
# ~/.mcpp/config.toml — a fact about this machine, not in the repository
[xlings.overrides]
"xim:vcpkg" = { root = "/opt/vcpkg" }
```

### 4.5 Build without the network, and audit what a build used

```bash
MCPP_NO_AUTO_INSTALL=1 mcpp build     # refused, naming what it would have installed
mcpp build --managed-only             # refuses any source that is not the ecosystem's
mcpp why payload xim:cmake            # what was consulted, and what each answered
```

`mcpp why` reads the record the build wrote, so it answers on a machine that
already holds the payload:

```
sources:
  payload:xim:cmake  /usr/bin/cmake
      program · build.mcpp:9  for cmake-consumer
      considered: options::cmake = "/usr/bin/cmake"; payload xim:cmake (not requested by this build)
```

### 4.6 Bring a whole toolchain

A toolchain is the same question one layer down, and it is answered in the
manifest or by the root build program, never by a plugin:

```toml
[toolchain]
default = { path = "/opt/llvm-trunk" }          # a tree this machine already has
# or
default = { configure = "build.mcpp" }          # the build program states it
bootstrap = "llvm@22.1.8"                       # the one that compiles build programs
```

`mcpp.plugins.toolchain` (feature `plugins-toolchain`) builds that statement:
`layout`, `prefixed`, `managed`, `from_env_script` (a vendor SDK's
`environment-setup-*`), `newest_under`, `env`, the `with_launcher` /
`with_sysroot` / `with_family` / `with_tool` refinements, and `configure(fn)` /
`use(d)`.

## 5. For whoever writes a plugin

### 5.1 A member that runs a program

Three pieces, written once per tool:

```cpp
// 1. the option, as a choice -- a string still assigns to it
struct options {
    mcpp::plugins::tool::choice cmake;
};

// 2. the spec: this member's own knowledge, and nothing else
inline mcpp::plugins::tool::spec cmake_spec(std::string who) {
    return { .who = std::move(who), .package = "cmake", .programs = {"cmake"},
             .bin_dirs = {"bin", "CMake.app/Contents/bin"}, .option = "options::cmake" };
}

// 3. one resolve, one refusal
auto t = mcpp::plugins::tool::resolve(cmake_spec("mcpp.deps.cmake"), opt.cmake);
if (t.pending()) return true;        // asked for; the engine runs this program again
if (!t) { warn(mcpp::plugins::tool::describe_missing(cmake_spec("mcpp.deps.cmake"), t));
          return false; }
run(t.program);
```

`choice` is constructible from a string, so a member migrating from
`std::string` costs its users nothing: `o.cmake = "/usr/bin/cmake"` still
compiles, and the line that wrote it reaches the build's output and
`mcpp why tool cmake`.

`describe_missing` is the one refusal text, and it lists every way to name the
tool. A member does not write its own.

### 5.2 The pair that makes "named here, not downloaded" true

A `tool::choice` alone is not enough. Provisioning of an eagerly declared payload
happens in prepare, **before any build program runs**, so a payload declared
plainly is installed even when the build program goes on to name another program.
Nothing written in `build.mcpp` can undo it. That was the original symptom behind
mcpp#755.

The manifest half:

```toml
[feature-xlings.deps-cmake]
"xim:cmake" = { version = ">=3.31", provision = "on-request" }
```

So the author's decision is one question:

| when the member needs the tool | how to declare it | after a user names their own |
|---|---|---|
| while **planning** (it runs cmake, vcpkg, appimagetool, bundletool) | `provision = "on-request"` | nothing is downloaded |
| **to plan at all** (it reads a version out of the toolkit's headers, an SDK file, an API level from a platform directory) | plainly, which is eager | still downloaded; the user skips it with `[xlings.overrides]` |

`rules-cuda`, `rules-sycl`, `rules-qt` and `dist-apk` carry their main payloads
the second way, and that is timing, not an oversight: moving provisioning after
the build program would make them fail to plan.

### 5.3 Ask for a payload only on the branch that uses it

`dist-apk` declares five payloads for every Android build and one more only for
`--format aab`:

```toml
"xim:bundletool" = { version = "1.18.3", provision = "on-request" }
```

The member requests it while planning a bundle, so an ordinary `--format apk`
build installs nothing for it. The engine installs every request of one
invocation together and runs only the programs that asked; the asking run is
discarded before its directives are applied, so there is no half-applied state.
Three rounds at most, and a program that asks for something new in the third is
refused by name.

### 5.4 A tool that is a tree, not a program

```cpp
auto f = mcpp::plugins::tool::resolve(nvcc_spec(), opt.toolkit);
t.nvcc_root = t.cudart_root = t.crt_root = t.curand_root = t.cccl_root = f.root;
```

Take `.root` for a toolkit and `.program` for a program. The two were one string
in `rules-sycl` while it took the payload directory and appended
`/bin/clang++`; after the migration `-L` pointed at `<root>/bin/clang++/lib` and
the SYCL consumer failed with `ld.lld: error: unable to find library -lsycl`. The
whole collection was audited for that swap.

### 5.5 Keep the variable a member already read

```cpp
.legacy_env = "MCPP_SLANGC"
```

It answers at the second level, before the engine's override. `rules-slang` also
keeps its historical silent PATH fallback, with a warning that names the correct
spelling (`options::compiler = tool::on_path()`) and the date it is removed.

### 5.6 A member that only looks

A member that reports what a machine has, rather than running it, passes
`.request = false`, so looking never pulls a payload down. `rules-qt` does this
when it resolves an SDK root: it reports which of the three levels named one,
and a report is not a reason to download anything.

### 5.7 Prove it, without installing anything

`mcpp.plugins.testing` runs a member against a stated context and compares the
directives it emitted:

```cpp
{ "tool: a choice names the program and asks for no payload",
  t::row::linux_libcxx().file("pkg/opt/bin/cmake")
      .xpkg("xim", "cmake", "{root}/payload")
      .xpkg_source("xim", "cmake", "pending"),      // declared on request, not installed
  [] { /* resolve with a choice */ },
  [](const t::result& r, t::checker& c) {
      c.expect(r.has_line("source=choice", "opt/bin/cmake"), "the choice is the source");
      c.expect(!r.has_line("mcpp:xpkg-request="), "a choice requests no payload");
  } },
```

`!has_line("mcpp:xpkg-request=")` is the written proof that nothing was asked
for. The kit clears the real build's `MCPP_XPKG_*` variables, which a case must
not inherit: a case that inherited `pending` from the outer build asked for the
payload and planned nothing wherever it was absent, and passed wherever it was
installed.

In CI the same criterion reads `resolution.json`, so it holds on a runner that
already has the payload:

```
payload:xim:cmake   not requested by this build
```

## 6. For whoever designs a plugin

**The project names a mechanism; the plugin declares that mechanism's payloads.**
A project writes `features = ["deps-cmake"]`, not `xim:cmake`. A feature states a
mechanism, so a payload at a fixed version does not belong in a feature's name —
0.15.0 removed the features that did that. Where a payload comes from is the
project's statement, which is why `[xlings.overrides]` is read from the root and
refused in a dependency.

**One answer, recorded once.** An explicit source produces exactly one `Using`
line, one entry in the `Finished` summary, one record in `resolution.json`, and
one section of `mcpp why`. All four read the same `SourceDecision`, so three
cannot agree while the fourth lags. A member contributes to it by calling
`resolve`, which states `mcpp:decision=`; it does not print its own.

**The default stays the default.** The ecosystem's path is the one a project
takes by writing nothing, and its output is unchanged by this mechanism. Anything
else gets a line of its own, in a colour and with a tag, precisely so that reading
a log cannot confuse the two.

**One version of a package exists.** Several declarations of the same payload
unify to one winner, and `xpkg_dir` answers the version this build installed
rather than the spelling some manifest wrote. An override takes part in version
checking but not in deciding: it states where a package comes from, not which
version is wanted, so its key carries no version.

**What stays outside.** A host tool a dependency publishes as `kind = "bin"` is
still named through `[tools.overrides]`: the two key spaces differ, and merging
them would put two kinds of key in one table. `mcpp.lock` records the result of
dependency resolution and deliberately records no toolchain. `tools = { ld = … }`
is read by a clang tree, where a linker reaches the link as `--ld-path`; a gcc
tree that states it is refused where the declaration is read, because gcc selects
a linker by the name `ld` inside a `-B` directory and a stated tool that takes no
part in the build is what this mechanism exists to prevent.

## 7. Every official member that drives a tool, and how it declares it

| member | tool | declaration | reason |
|---|---|---|---|
| `deps-vcpkg` | `xim:vcpkg` | on request | it runs vcpkg while planning |
| `deps-cmake`, `deps-archive` | `xim:cmake` | on request | it runs cmake while planning |
| `dist-appimage` | `xim:appimagetool` | on request | it runs the tool while planning |
| `dist-apk` | `xim:bundletool` | on request | only `--format aab` uses it |
| `dist-apk` | build tools, platform, JDK, keystore | eager | the plan reads an API level from the platform directory |
| `dist-apk-kotlin`, `dist-apk-maven` | `xim:kotlin`, `xim:coursier` | eager | a project that selects the feature compiles Kotlin or resolves a Maven graph |
| `dist-wix` | `xim:wix` | eager | the plan checks the payload's files |
| `rules-cuda`, `rules-hip` | toolkit components | eager | the plan reads versions out of the toolkit |
| `rules-sycl` | `xim:dpcpp`, `xim:gcc`, `xim:glibc`, `xim:linux-headers` | eager | the compile line is built from the C and C++ libraries |
| `rules-spirv`, `rules-slang`, `rules-ascendc` | `xim:shaderc` / `glslang`, `xim:slang`, `xim:cann-toolkit` | eager | the plan classifies the compiler it found |
| `dist-apple` | `xim:macapp-run`, `xim:apple-device-tools` | eager, under `when = "run"` | only `mcpp run` needs them, and a tier states that without a request |
| `rules-qt` | none | the project declares `xim:qt-base` in its own `[xlings]` table | the SDK a program links is the project's choice |
| `rules-metal`, `rules-swift` | none | the host's Xcode | the platform publishes no payload for them |

Every one of them is named by `options::<tool>` in `build.mcpp`, by
`[xlings.overrides]`, by `MCPP_XLINGS_OVERRIDE_<NS>_<NAME>` or in
`~/.mcpp/config.toml`. The four on-request entries are the ones for which naming
it means no download at all.
