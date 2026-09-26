# `deps-vcpkg`, `deps-cmake` and `deps-archive`

The `deps-*` members answer where a library comes from: a vcpkg manifest, a CMake subproject, or an archive the project keeps. Each installation is an action with declared inputs and outputs, and the prefix reaches the build by name (include directory, libraries by full path, runtime search directory), so planning installs nothing.

## `deps-vcpkg`

Module `mcpp.deps.vcpkg`; engine floor: 2026.9.26.2 (mcpp#702).

**Needs and behaviour.** `xim:vcpkg` (the tool and the scripts released with it), which this feature declares on the host axis. From 0.13.0. Installs a `vcpkg.json` manifest as a `prepare` action and maps `<install root>/<triplet>/<triplet>` into the build by name, its shared-library directory a runtime search directory; `mcpp emit build-database` installs nothing. See [the section below](#deps-vcpkg-the-libraries-a-vcpkg-manifest-names)

## `deps-cmake`

Module `mcpp.deps.cmake`; engine floor: 2026.9.26.2 (mcpp#702).

**Needs and behaviour.** `xim:cmake`, which this feature declares on the host axis. From 0.13.0. Configures, builds and installs a CMake subproject as one `prepare` action whose inputs are the subproject's files, and maps the prefix as `deps-vcpkg` does

## `deps-archive`

Module `mcpp.deps.archive`; engine floor: 2026.9.26.2.

**Needs and behaviour.** `xim:cmake`, which this feature declares on the host axis. From 0.14.0. Extracts a zip archive the project keeps and places its tree beside the program: one action names every member as an output, read from the archive's central directory while the build program runs, and each is deployed, so `mcpp run` finds the files and `mcpp pack` carries them. See [the section below](#deps-vcpkg-the-libraries-a-vcpkg-manifest-names)

## `deps-vcpkg`: the libraries a vcpkg manifest names

```toml
[build-dependencies.mcpp]
plugins = { version = "0.15.0", features = ["deps-vcpkg"], host-module = true }
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
| `triplet` | empty derives it from the target: `x64-windows`, `arm64-windows`, `x64-mingw-dynamic`, `x64-linux`, `arm64-linux`, `x64-osx`, `arm64-osx`; on Linux under a libc++ toolchain the generated `x64-linux-libcxx` or `arm64-linux-libcxx` (0.15.0); a custom triplet is found through the manifest's `overlay-triplets` |
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
(`<root>/vcpkg/vcpkg-running.lock`), so two workspace members installing one
root run one after the other. vcpkg fetches its
own CMake, Ninja and 7-Zip, and on Windows a portable git; on Linux and macOS
its documented host prerequisites (git, curl, zip, unzip, tar, a C compiler)
are the host's. Ports are compiled with vcpkg's default toolchain for the
triplet: the Visual Studio toolset on Windows, the host compiler elsewhere. The
program links them, so its C++ standard library must be theirs. On Windows every
compiler of the MSVC ABI, mcpp's clang included, uses Microsoft's, and on macOS
every compiler uses libc++, so a port links as it stands. Linux has two
libraries that do not link with each other: the host compiler uses libstdc++,
and mcpp's clang uses libc++ (`std::__1::`). Under a libc++ toolchain the
default triplet is therefore a generated one, `<arch>-linux-libcxx`, whose
ports build with mcpp's clang through vcpkg's chain-loaded toolchain file; the
clang's own configuration names libc++ and the C library mcpp links against.
Under a gcc toolchain the default triplet is vcpkg's own (0.15.0).

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
toolchain file. The subproject is compiled with the toolchain CMake selects by
default unless `cache_args` names a compiler or a toolchain file; on Linux under
a libc++ toolchain the compilers are mcpp's clang, as `deps-vcpkg` builds its
ports (0.15.0). `deploy` places files of the prefix
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
