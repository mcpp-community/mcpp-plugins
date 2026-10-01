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
| L2 general library | `mcpp.plugins.declare`, `mcpp.plugins.toolset`, `mcpp.plugins.fs`, `mcpp.plugins.tool`; `mcpp.plugins.testing`, `mcpp.plugins.toolchain` | this package, feature `plugins-core` (`plugins-testing` for the kit, `plugins-toolchain` for the toolchain builders) |
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
plugins = { version = "0.19.0", features = ["plugins-core"], host-module = true, reexport = true }
```

`reexport = true` is needed when the plugin's consumers import L2 modules in
their own build programs.

**The engine floor.** A plugin states the first mcpp release it needs in
`[package] mcpp = ">=<release>"`; an older engine stops before any other work
and names the upgrade. This package's floor is 2026.10.1.3, the release that
states where each tool and payload comes from (protocol 15).

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

**Testing a plugin.** `mcpp.plugins.testing` runs a plugin function in a child
process against a stated build context (`row::windows_visual_studio()`,
`row::windows_managed()`, `row::linux_gcc()`, `row::linux_libcxx()`, or a
context built with `set`, `file`, `xpkg`, `xpkg_source`, `xpkg_program` and
`phase`) and hands the lines it emitted and the
files it wrote to a check. The test is a build program; a failed case fails the
build and prints the report. `tests/plugin-logic` is this package's own use.

**Compatibility units.** A behaviour kept after the release that replaced it
lives in a `compat/` directory, one unit per behaviour, whose header states
what it keeps, since when, its retirement date (six months later), its
replacement and the note it prints once per build.
`.github/scripts/check-compat-retirement.sh` fails once a date has passed.

## 4. A new plugin

A plugin in this package consists of:
- a module file under `rules/`, `tools/`, `dist/` or `deps/`;
- a feature in `mcpp.toml`;
- a fixture under `tests/` that CI builds and asserts on;
- a row in the README's member table with its mcpp floor;
- a page under `docs/`;
- a version bump.

A project's own plugin is a module in the project (a path dependency with `host-module = true`) and follows §2 in the same way.
