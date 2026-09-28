# `deps-vcpkg`, `deps-cmake` and `deps-archive`

The `deps-*` members answer where a library comes from: a vcpkg manifest, a CMake subproject, or an archive the project keeps. Each installation is an action with declared inputs and outputs, and the prefix reaches the build by name (include directory, libraries by full path, runtime search directory), so planning installs nothing.

## `deps-vcpkg`

Module `mcpp.deps.vcpkg`; engine floor: 2026.9.28.3 (0.17.0, mcpp#734; 2026.9.26.2 before).

**Needs and behaviour.** `xim:vcpkg` (the tool and the scripts released with it), which this feature declares on the host axis. From 0.13.0. Installs a `vcpkg.json` manifest as a `prepare` action and maps `<install root>/<triplet>/<triplet>` into the build by name, its shared-library directory a runtime search directory; `mcpp emit build-database` installs nothing. See [the section below](#deps-vcpkg-the-libraries-a-vcpkg-manifest-names)

## `deps-cmake`

Module `mcpp.deps.cmake`; engine floor: 2026.9.28.3 (0.17.0, mcpp#734; 2026.9.26.2 before).

**Needs and behaviour.** `xim:cmake`, which this feature declares on the host axis. From 0.13.0. Configures, builds and installs a CMake subproject as one `prepare` action whose inputs are the subproject's files, and maps the prefix as `deps-vcpkg` does

## `deps-archive`

Module `mcpp.deps.archive`; engine floor: 2026.9.26.2.

**Needs and behaviour.** `xim:cmake`, which this feature declares on the host axis. From 0.14.0. Extracts a zip archive the project keeps and places its tree beside the program: one action names every member as an output, read from the archive's central directory while the build program runs, and each is deployed, so `mcpp run` finds the files and `mcpp pack` carries them. See [the section below](#deps-vcpkg-the-libraries-a-vcpkg-manifest-names)

## `deps-vcpkg`: the libraries a vcpkg manifest names

```toml
[build-dependencies.mcpp]
plugins = { version = "0.17.0", features = ["deps-vcpkg"], host-module = true }
```

```cpp
// build.mcpp
import std;
import mcpp;
import mcpp.deps.vcpkg;

int main() {
    mcpp::deps::vcpkg::options o;
    o.libraries = { "fmt", "spdlog" };
    return mcpp::deps::vcpkg::use(o) ? 0 : 1;
}
```

`use()` finds `vcpkg.json` at or above the package root, so every member of a
workspace finds a manifest kept at the workspace root. It then:

- declares `vcpkg install …` as a `mcpp::roles::prepare` action whose output
  directory is the prefix `<install root>/<triplet>/<triplet>` and whose inputs are `vcpkg.json`,
  `vcpkg-configuration.json` and every file of the manifest's overlay ports and
  triplets. The package's compile and link edges wait for it; it runs under
  `mcpp build` when an input changed, and never under `mcpp emit
  build-database`;
- adds `<prefix>/include` as an include directory;
- links each name in `options::libraries` by its full path under
  `<prefix>/lib` (`fmt` is `fmt.lib` on Windows, `libfmt.a` or
  `libfmt.so` elsewhere; a name with an extension is a file name);
- for a triplet that links dynamically, declares `bin/` (Windows) or `lib/`
  (elsewhere) with `mcpp::runtime_search_dir`: the program's run path on ELF and
  Mach-O, `mcpp run`'s load path, `mcpp pack`'s closure, and on Windows the
  DLLs the program imports placed beside it after the link;
- copies each file `options::deploy` names out of the prefix, by an action
  that names the copy as its output, and deploys the copy beside the program;
- returns the prefix (`root`, `include`, `lib`, `bin`, `share`, `triplet`, and
  `deployed`, the copies, for a project that lays out a directory of its own).

A vcpkg tool that is not installed is a warning and not a failure: the plan
states every path it can, and the build is where the absence fails.

| option | meaning |
|---|---|
| `triplet` | the base triplet; empty derives it from the target: `x64-windows` (`x64-windows-static` when the program links the C runtime statically), `arm64-windows`, `x64-mingw-dynamic`, `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`. Under the `chain` mechanism the installation uses a triplet derived from it (below). A custom triplet is found through the manifest's `overlay-triplets` |
| `toolset` | which toolset builds the ports: `{.toolset = resolved or detected, .cc = abi_native or row}`; the default is `resolved` with `abi_native` (0.17.0, see [the toolset](#the-toolset-instance-chain-detected)) |
| `crt_linkage` | the ports' C runtime linkage on the MSVC ABI, `"static"` or `"dynamic"`; empty follows the program's C++ runtime contract (0.17.0) |
| `libraries` | the link, in order; a name that matches no installed file fails the link, naming its path |
| `manifest_root` | the directory holding `vcpkg.json` |
| `install_root` | empty is vcpkg's default, `<manifest root>/vcpkg_installed`. Each triplet is its own vcpkg installation, `<install root>/<triplet>`, because vcpkg's manifest mode removes from an installation the packages of every other triplet (0.15.0; 0.14.0's prefix `<install root>/<triplet>` is no longer read) |
| `overlay_triplets` | further overlay-triplet directories |
| `install_args` | arguments appended to `vcpkg install` |
| `vcpkg_root` | a vcpkg root other than the `xim:vcpkg` payload |
| `deploy` | files of the prefix the program reads at run time, each `{file, to}`: `file` relative to the prefix root, `to` the directory beside the program (`{"share/opencc/t2s.json", "BaseConfig/opencc"}`) |

The payload `xim:vcpkg` is vcpkg-tool's release binary with the standalone
bundle published beside it, so the scripts a port calls are the ones that tool
was released with. A `builtin-baseline` manifest resolves through vcpkg's git
registry, into vcpkg's per-user registry cache; no clone of microsoft/vcpkg is
made per project. The action is vcpkg itself, configured by arguments
alone (0.15.0; 0.13.0-0.14.0 ran it through a program of this package):
`--vcpkg-root` names the payload, whatever `VCPKG_ROOT` the shell has;
`--disable-metrics`; the downloads and each installation's build trees go to
vcpkg's per-user directory (`%LOCALAPPDATA%\vcpkg` on Windows,
`$XDG_CACHE_HOME/vcpkg` or `~/.cache/vcpkg` elsewhere), beside vcpkg's default
binary cache, under a short name, because Windows tools still enforce MAX_PATH;
an existing `VCPKG_DOWNLOADS` is kept. vcpkg locks the installation root itself
(`<root>/vcpkg/vcpkg-running.lock`), and `--x-wait-for-lock` makes two
workspace members installing one root run one after the other; without it the
second fails, "failed to take lock" (0.15.1). vcpkg fetches its
own CMake, Ninja and 7-Zip, and on Windows a portable git; on Linux and macOS
its documented host prerequisites (git, curl, zip, unzip, tar, a C compiler)
are the host's.

### The toolset: `instance`, `chain`, `detected`

The ports are linked into the program, so they are built with the toolset mcpp
builds the program with (0.17.0). The member reads the toolset from the engine
(mcpp 2026.9.28.3, `mcpp::abi_tool`, `mcpp::tool_env`, `mcpp::toolset_identity`,
`mcpp::msvc_instance_dir`) through `mcpp.plugins.toolset`, and the way it reaches
vcpkg follows where the toolset came from:

| mechanism | when | what vcpkg receives | port kinds that build |
|---|---|---|---|
| `instance` | an MSVC toolset from a Visual Studio instance (`msvc@system`, the default on a machine with Visual Studio) | `VCPKG_VISUAL_STUDIO_PATH` naming that instance; the standard triplet, or a derived one with `VCPKG_PLATFORM_TOOLSET_VERSION` when the resolved toolset is not the instance's default | CMake, make and MSBuild |
| `chain` | a managed MSVC toolset (`xim:msvc@<version>`), and the clang toolsets of the Linux and macOS rows | a derived triplet `<base>-mcpp-<hash>` that chain-loads a toolchain naming the tools, and the tools' environment | CMake and make; an MSBuild port is refused by name |
| `detected` | `options.toolset.toolset = detected`; and, under `resolved`, the Linux GCC row | nothing: vcpkg finds its own toolset, as in 0.16.0 | as vcpkg's own detection allows |

**The Linux GCC row.** mcpp runs its GCC payload with a sysroot, a binutils
directory and a link model (the payload's dynamic linker and C library) that
its own command lines add; the driver alone is not a complete toolset, and
vcpkg's compiler detection fails with it on a machine whose host compiler does
not fill the gaps (measured on the plugins' CI). The clang payloads carry their
configuration in their own `.cfg` files and are complete. On the GCC row the
host compiler's libstdc++ is the program's C++ library, so `resolved` keeps
vcpkg's detection there and the member states why in `mcpp.plugins.toolset`'s
`reason`.

**The derived triplet.** The base triplet's text is copied into it, not
included, because vcpkg hashes a triplet file's content and not the files it
includes. The member appends the chain-loaded toolchain
(`${CMAKE_CURRENT_LIST_DIR}/mcpp-chain-<system>.cmake`), the environment
variables that pass through untracked (`MCPP_VCPKG_CC`, `MCPP_VCPKG_CXX`,
`MCPP_VCPKG_RC`, `MCPP_VCPKG_MT`, `MCPP_VCPKG_ROOT`, and on the MSVC ABI
`INCLUDE` and `LIB`), a comment with the toolset's identity, and on the MSVC ABI
`VCPKG_CRT_LINKAGE`. The toolchain file reads each tool from the environment
through `file(TO_CMAKE_PATH)` and then includes vcpkg's own
`scripts/toolchains/<system>.cmake`, so ports keep vcpkg's standard flags.
Neither file holds a path, so the triplet's name, and vcpkg's ABI hash, depend
on the toolset's identity and its compilers and not on where they are
installed: the same toolset on another machine restores the same binary
packages. On the MSVC ABI the derived triplet is also the host triplet, and the
installation runs with the toolset's directories first on a `PATH` that vcpkg
keeps (`VCPKG_KEEP_ENV_VARS=PATH`), because a make-based port (icu) finds
`link.exe` there.

**An MSBuild port under `chain`.** vcpkg would run MSBuild with
`/p:PlatformToolset=external` and fail without naming the cause. The derived
triplet stops such a port with a message saying that it needs a Visual Studio
instance, and that the toolchain `msvc@system` or `toolset = detected` builds
it.

**The C runtime (MSVC ABI).** The ports' C runtime is the program's: the
default triplet is `x64-windows-static` for a program that links the C runtime
statically (`cxx_runtime = "self-contained"` or `linkage = "static"`), and a
triplet named in `triplet` whose `VCPKG_CRT_LINKAGE` contradicts the program's
is refused naming both statements. A static library built against the other C
runtime fails the link with `/failifmismatch`, and a DLL built against it puts
a second C++ runtime into the process without a word. `crt_linkage` states the
ports' linkage explicitly and wins.

**Linux.** Linux has two C++ standard libraries that do not link with each
other. Under mcpp's clang the derived triplet names that clang, so the ports
use libc++ (`std::__1::`) as the program does; under mcpp's GCC the host
compiler's libstdc++ is the program's library, and vcpkg's detection is kept.

**Upgrading from 0.16.0.** On the clang rows of Linux and macOS, and on Windows
with a managed toolset, the installation moves to a derived triplet, so each
port is built once more (or restored from a binary cache that already holds
it); the prefixes of 0.16.0 (`<arch>-linux-libcxx`) are left where they are. The
Linux GCC row is unchanged. With
Visual Studio and the dynamic C runtime nothing changes: the standard triplet
and the instance vcpkg selects by itself give the same ABI hash. A program that
links the C runtime statically moves to `x64-windows-static`.

**`detected`** keeps the behaviour of 0.16.0 until 2027-03-28 (a compatibility
unit, `src/compat/detected_toolset.cppm`) and prints a note once per build. On
the Linux libc++ row it names mcpp's clang, as 0.16.0 did.

**Binary caches.** vcpkg reads `VCPKG_BINARY_SOURCES` and
`VCPKG_DEFAULT_BINARY_CACHE` from the environment, and the installation passes
the environment on; whether to host a shared cache is the project's decision.

Not supported: vcpkg's classic mode; the debug libraries under `debug/lib`.

## `deps-cmake`: a CMake subproject

```cpp
import mcpp.deps.cmake;

int main() {
    mcpp::deps::cmake::options o;
    o.source     = "3rdParty/widgets";
    o.cache_args = { "-DWIDGETS_BUILD_EXAMPLES=OFF" };
    o.libraries  = { "widgets" };
    o.shared     = true;
    return mcpp::deps::cmake::use(o) ? 0 : 1;
}
```

One `prepare` action, `cmake -P` over a script the member writes
(`<out dir>/deps-cmake/<name>.cmake`), configures, builds and installs the
subproject into `<out dir>/deps-cmake/<name>/install`, its declared output
directory; its inputs are the script and the subproject's files, so an edit to the subproject rebuilds it. The prefix is mapped as `deps-vcpkg`
maps its own. `layout` names install directories other than `include/`, `lib/`
and `bin/`; `prefix_path` becomes `CMAKE_PREFIX_PATH` (`mcpp::rules::qt::root()`
for a subproject that finds Qt); `cache_args` carries `-D…`, `-G …` and a
toolchain file. The subproject is compiled with the toolset mcpp resolved
(0.17.0), by the mechanisms `deps-vcpkg` uses: under `instance` CMake's default
generator (Visual Studio) is kept and pointed at the instance
(`CMAKE_GENERATOR_INSTANCE`) and the toolset version (`-T version=`, when it is
not the instance's default); under `chain` the Ninja generator runs with mcpp's
own ninja (`mcpp::ninja_program()`), the compilers are named by path, and the
action runs with the tools' environment and their directories first on `PATH`.
On the MSVC ABI `CMAKE_MSVC_RUNTIME_LIBRARY` follows the program's C runtime
(policy CMP0091). Each toolset statement configures its own build directory,
because CMake refuses a cache made with another generator or instance; the
directory 0.16.0 configured stays `build/`. A compiler, a toolchain file or a
generator in `cache_args` is the project's decision and wins. The options are
`toolset` and `crt_linkage`, as `deps-vcpkg` takes them, and `generator`
(`default`, or `ninja` for the Ninja generator under an instance as well).
`deploy` places files of the prefix
beside the program, as `deps-vcpkg` takes it.

## `deps-archive`: files a program reads at run time, from an archive

```cpp
import mcpp.deps.archive;

int main() {
    mcpp::deps::archive::options o;
    o.archive = "assets/python-embed.zip";
    o.to      = "runtime";
    return mcpp::deps::archive::unpack(o) ? 0 : 1;
}
```

`unpack()` reads the archive's central directory while the build program runs,
so every file it holds is named before anything is extracted. One `artifact`
action, `cmake -P` over a script the member writes, empties
`<out dir>/deps-archive/<name>` and extracts the archive there with
`file(ARCHIVE_EXTRACT ... TOUCH)`, naming each file as an output; each file is then
deployed under `to`, so `mcpp run` finds it beside the program and `mcpp pack`
carries it. The action's inputs are the archive and the tool, so an edited
archive is extracted again and an unchanged one is not; `mcpp emit
build-database` extracts nothing. `result::files` lists the extracted copies
with their paths beside the program, for a project that lays out a directory of
its own. A C++ file inside the archive is data: the action's outputs never join
the package's compile set.

Only zip is read, because a compressed tar has no index to list without
decompressing it while planning; a ZIP64 archive is refused by name, as is a
member whose path would leave the extraction directory. A missing `cmake` is a
warning, and the plan then extracts and deploys nothing.
