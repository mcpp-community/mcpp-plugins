# Plugin development

mcpp is a general build engine with a framework for build plugins. Plugins come from this package, from third parties, or from the project itself, and `build.mcpp` is where a project uses them: it imports a plugin's module, configures it, and calls it. This page states the conventions every plugin in this package follows. A third-party plugin or a project's own plugin is held to the same conventions, and mcpp's SPEC-007 (docs/specs/build-plugins.md) is the engine contract they rest on.

## 1. Division of responsibility

| layer | owns | does not own |
|---|---|---|
| mcpp | the build graph, actions and their stamps, the runtime closure check, deployment, packing, and the toolchain facts (`mcpp::compiler()`, `mcpp::cxx_stdlib()`, ...) | any particular library, SDK or tool |
| plugin | a mechanism: one kind of input turned into actions with declared inputs and outputs, and a prefix or SDK mapped into the build | versions; the runtime closure of what it maps; workarounds for engine gaps |
| xlings package | a binary payload and its runtime closure on every platform it serves | build logic |
| project | policy: which libraries and SDKs, at which versions, where, with which options | — |

## 2. Rules

1. **A plugin provides a mechanism, not a policy.** A plugin contains no fixed version, path or library list that a project could reasonably want to choose.
2. **A feature states a mechanism and the tools it runs.** `[feature-xlings.<feature>]` may declare a tool the plugin's actions execute (`xim:vcpkg`, `xim:cmake`), with a floor when the tool is a floor. It never declares a library or an SDK that is linked into the program. The project declares those in its own `[xlings]` table.
3. **Configuration is `build.mcpp` first.** Each setting is taken from the first of three levels that gives it:
   1. the plugin's `options`, set in `build.mcpp`, where the value may be computed;
   2. an environment variable, only under a name the ecosystem already uses (`QT_ROOT_DIR`), and read with `rerun_if_env_changed`;
   3. a payload the project declares.

   The level that decided is recorded with `mcpp::fact`. What a plugin returns (a prefix, an SDK root, the deployed files) is its extension point: `build.mcpp` may declare its own actions over it.
4. **The runtime closure belongs to the package that ships the binaries.** A payload whose libraries load others carries or declares them on every platform, so a packed program starts on a machine with only its operating system. A plugin names the payload's library directories as runtime search directories and nothing beyond them.
5. **Internals are not part of the contract.** The contract is the set of feature names, `options` fields, returned types and documented environment variables. A consumer's manifest names no program the plugin uses internally. An action's command is the tool itself (`vcpkg`, or `cmake -P` over a script the plugin writes), never a helper program built from the plugin's package.
6. **Work is an action.** Installation, extraction and generation are actions with declared inputs and outputs (SPEC-007 R1.1, R3). A build program plans, and `mcpp emit build-database` never installs.
7. **Absence is a warning.** A missing tool or SDK is a `mcpp::warning`, and the plan states every path it can (R1.2). A plan never depends on what an action produced (R1.3).
8. **Engine gaps are reported, not patched.** A capability the engine lacks is filed as an mcpp issue. A workaround that already exists is recorded in the plugin's documentation with the issue number and is removed when the engine provides the capability.
9. **Every behaviour has a criterion.** Each documented behaviour has a CI check, run on every platform the plugin claims.
10. **Documentation is concise.** A plugin's page opens with one summary paragraph, followed by use, options and behaviour, in plain declarative sentences. The README is an index.

## 3. Layers, names and the engine floor

| layer | modules | provided by |
|---|---|---|
| L1 `mcpp.core` | `mcpp.core`, spelled `mcpp` as well (the two are permanently equivalent) | the engine |
| L2 general library | `mcpp.plugins.declare`, `mcpp.plugins.toolset`, `mcpp.plugins.fs`, `mcpp.plugins.tool`; `mcpp.plugins.testing`, `mcpp.plugins.toolchain`, `mcpp.plugins.platform` | this package, feature `plugins-core` (`plugins-testing` for the kit, `plugins-toolchain` for the toolchain builders, `plugins-platform` for a platform's plugin and a package format's provider) |
| L3 plugins | `mcpp.deps.*`, `mcpp.rules.*`, `mcpp.dist.*`, `mcpp.tools.*` here; `mcpp.<namespace>.*` elsewhere | this package, by feature; any package |

A layer depends only on the layers below it. L2 knows no foreign tool: it turns
the engine's facts into what a plugin needs (`mcpp.plugins.toolset` decides how
a resolved toolset reaches a foreign build system, `instance`, `chain` or
`detected`), and the plugin writes the foreign system's own files.

**Module names.** A third-party plugin names its modules `mcpp.<namespace>.*`,
where `<namespace>` is its package's namespace (`mcpp.acme.protobuf`). The
second segments `core`, `plugins`, `deps`, `rules`, `dist` and `tools` belong to
namespace `mcpp`; the engine warns when another package uses them, and
mcpp-index refuses such a package.

**Using L2 from another package.**

```toml
[build-dependencies.mcpp]
plugins = { version = "0.21.0", features = ["plugins-core"], host-module = true, reexport = true }
```

`reexport = true` is needed when the plugin's consumers import L2 modules in
their own build programs.

**The engine floor.** A plugin states the first mcpp release it needs in
`[package] mcpp = ">=<release>"`; an older engine stops before any other work
and names the upgrade. This package's floor is 2026.10.27.1, the release that
speaks protocol 17 (the platform's directives and the `platform` phase, mcpp
SPEC-013) and writes `mcpp-package.json` beside every staged tree (SPEC-014);
it carries protocol 15, which states where each tool and payload comes from.

**Where a member's tool comes from.** A member that runs a program resolves it
through `mcpp.plugins.tool`, which answers in one order for every member:

1. the build program's own choice — `o.cmake = "/usr/bin/cmake"`, or
   `tool::root(dir)`, `tool::on_path()`;
2. the variable that member has always read (`MCPP_SLANGC`), kept for the
   projects that write it;
3. the engine's override (`[xlings.overrides]`, `MCPP_XLINGS_OVERRIDE_*`,
   `config.toml`) — the payload is then not installed at all;
4. the declared payload, asked for here when it is declared
   `provision = "on-request"`.

A member writes one `tool::spec` per tool (the package, the program names, the
directories under a root, the option's name, a legacy variable) and calls
`resolve`:

```cpp
inline const mcpp::plugins::tool::spec& cmake_spec() { /* … */ }

auto t = mcpp::plugins::tool::resolve(cmake_spec(), opt.cmake);
if (t.pending()) return true;        // asked for; the engine runs this program again
if (!t) { mcpp::deps::warn(mcpp::plugins::tool::describe_missing(cmake_spec(), t)); return false; }
run(t.program);
```

A choice **never asks for the payload**, which is what makes "the build program
names its own tool" mean "the payload is not downloaded". `resolve` records the
answer with `mcpp::decision`, so the build reports the source and
`mcpp why tool <name>` can answer. `describe_missing` is the one refusal text,
listing every way to name the tool.

An option field is a `tool::choice`, which a string constructs, so
`o.cmake = "/usr/bin/cmake"` keeps working and the line that wrote it is what a
build reports.

A choice alone does not prevent a download: an eagerly declared payload is
provisioned before any build program runs. The pair that makes "named here, not
downloaded" true, the scenarios on both sides of it, and how each official member
declares its tools are in [tool-sources.md](tool-sources.md).

**Stating the build toolchain.** `mcpp.plugins.toolchain` (feature
`plugins-toolchain`) builds the statement a root build program makes in its
toolchain phase, for a project whose `[toolchain]` says
`configure = "build.mcpp"`: `layout`, `prefixed`, `managed`, `from_env_script`,
`newest_under`, `env`, the `with_launcher` / `with_sysroot` / `with_family` /
`with_tool` refinements, and `configure(fn)` / `use(d)`.

**A platform's plugin and a package format's provider.** A platform package
(mcpp SPEC-013) states its targets' configuration in `[platform]` and the part
it computes in a plugin (`[platform] plugin = "build.mcpp"`), which runs in the
`platform` phase. `mcpp.plugins.platform` (feature `plugins-platform`) states
each field of that phase as one checked call:

| call | directive | accepts |
|---|---|---|
| `link::driver(d)` | `link-driver` | `"lld"` (ld.lld itself links) or `"compiler"` |
| `link::start(symbol)` | `link-start` | a symbol |
| `link::input_file(path)` | `link-input=file:` | a file; a relative path is the platform package's own |
| `link::input_from(as, argv)` | `link-input=from:` | a program's standard output saved as `as` (no `:`), `argv[0]` a tool's name |
| `artifact_names(exe, shared)` | `artifact-name` | `{name}` once, an executable's at its start; an empty pattern leaves the kind unstated |
| `seal(steps)` | `seal` | at least one step, each a non-empty argv; `{}` the artifact, `{needed}` the shared artifacts it needs |
| `run_host(where)` | `run-host` | `"same-arch"` or `"any"` |

A malformed value is refused at the call: nothing is stated, the call returns a
`statement` whose `reason` names the value and what the call accepts, and the
reason is written to standard error and stated as a warning. Every token is one
line, since a directive is.

`provide_format(name, make)` provides a package format (SPEC-014 §6) in both of
its halves. The name is declared on every pass (`mcpp::provides_pack_format`), so
that `mcpp pack --pkg-format <unknown>` names it among the formats the graph
provides; only in the pass that asks for it is the staged package read and
`make(const package::info&, mcpp::action&)` called, and the action -- role
`artifact`, id and description the format's name unless `make` sets them -- is
submitted when `make` returns true. The engine's own formats (`tar`, `dir`,
`zip`, `single-file`) are refused as names.

```cpp
import mcpp;
import mcpp.plugins.platform;
namespace pf = mcpp::plugins::platform;

int main() {
    if (!pf::link::driver("lld") || !pf::artifact_names("{name}.img", "lib{name}.so")) return 1;
    pf::provide_format("img", [](const mcpp::plugins::package::info& p, mcpp::action& a) {
        static std::string out;
        out = p.name + "-" + p.version + ".img";
        a.arg("bundle").arg(out.c_str()).arg("${mcpp.stage_dir}").output(out.c_str());
        return true;
    });
    return 0;
}
```

**The staged package.** `mcpp::plugins::package` (in `mcpp.plugins`, std-only,
so an ordinary program reads it too) reads the `mcpp-package.json` `mcpp pack`
writes at the root of a staged tree (SPEC-014 §4.5): `read(stage_dir)` returns
the package -- `name`, `version`, `platform`, `linkage`, `mode`, `legs`,
`systems`, `runner`, `entry` (each architecture's entry, in the document's
order), `program`, and `files`, each with its `path`, `role`, `leg` and `sha256`
-- or the reason it cannot, a document of another schema than 1 among them.
`files_with_role(p, role)` and `entry_of(p, arch)` select from it. A provider
reads which file is the program, an image or a resource here, rather than
deciding again from the tree.

**Testing a plugin.** `mcpp.plugins.testing` runs a plugin function in a child
process against a stated build context (`row::windows_visual_studio()`,
`row::windows_managed()`, `row::linux_gcc()`, `row::linux_libcxx()`, or a
context built with `set`, `file`, `xpkg`, `xpkg_source`, `xpkg_program`,
`phase` and `pack`, the last a packaging pass's format and staged tree) and
hands the lines it emitted and the
files it wrote to a check. The test is a build program; a failed case fails the
build and prints the report. `tests/plugin-logic` is this package's own use.

**Compatibility units.** A behaviour kept after the release that replaced it
lives in a `compat/` directory, one unit per behaviour, whose header states
what it keeps, since when, its retirement date (six months later), its
replacement and the note it prints once per build.
`.github/scripts/check-compat-retirement.sh` fails once a date has passed.

## 4. The C++ a plugin is written in

A plugin is C++23 that three combinations of compiler and standard library accept, because each of them compiles it for some user:

| combination | who compiles the plugin with it | CI |
|---|---|---|
| GCC 16 with libstdc++ | `mcpp build` on Linux by default | `consumers` |
| clang (LLVM 22) with libc++ | `mcpp build --toolchain llvm@22.1.8` on Linux, and every build on macOS | `toolchain-llvm`, `rules-cross-platform` |
| clang with libstdc++'s `std` module | an editor's clangd (mcppls) reading a project built with GCC | `toolchain-editor` |

MSVC is a fourth for the members that claim Windows, checked by `rules-cross-platform` and `deps-managed-msvc`. The third combination is never used by a build, so only the editor lane sees what it rejects; it runs `mcppls diagnostics build.mcpp` on every fixture and fails on any error.

Known differences, each one met in this package:

- **A type that holds a container of itself does not use `std::pair` or `std::tuple` for the element.** Under clang with libstdc++'s `std` module, `std::vector<std::pair<std::string, value>>` inside `value` instantiates the pair while `value` is incomplete and fails with "incomplete type used in type trait expression"; GCC and clang with libc++ accept it. The element is a plain struct, and a member function that walks the container is defined after both types are complete (`mcpp::plugins::json::member`, 0.19.1).
- **No compiler's leniency is relied on.** An extension, a missing `typename`, or a template accepted only because one compiler instantiates it later is a failure in another lane.
- **A build program does not use `std::println`.** On macOS 14 the build program links the system libc++, which lacks a symbol it needs; `std::format` into a string and `std::cout` work everywhere.
- **A module does not instantiate `std::filesystem::path`'s iterator.** Under MSVC 14.52 every importer that then touches `std::filesystem` fails inside the STL; `mcpp::plugins::names::components` splits a path as a string instead.

## 5. A new plugin

A plugin in this package consists of:
- a module file under `rules/`, `tools/`, `dist/` or `deps/`;
- a feature in `mcpp.toml`;
- a fixture under `tests/` that CI builds and asserts on;
- a row in the README's member table with its mcpp floor;
- a page under `docs/`;
- a version bump.

A project's own plugin is a module in the project (a path dependency with `host-module = true`) and follows §2 in the same way.
