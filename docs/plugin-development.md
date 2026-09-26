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

## 3. A new plugin

A plugin in this package consists of:
- a module file under `rules/`, `tools/`, `dist/` or `deps/`;
- a feature in `mcpp.toml`;
- a fixture under `tests/` that CI builds and asserts on;
- a row in the README's member table with its mcpp floor;
- a page under `docs/`;
- a version bump.

A project's own plugin is a module in the project (a path dependency with `host-module = true`) and follows §2 in the same way.
