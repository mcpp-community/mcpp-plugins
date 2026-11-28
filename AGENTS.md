# AGENTS.md

`mcpp-plugins` is the package of official build plugins for mcpp
(`mcpp:plugins`). One plugin is one module — a single `.cppm`, or a directory
`<family>/<x>/` whose `<x>.cppm` keeps the module a consumer imports while
`internal/` holds the machinery — one feature in `mcpp.toml`, one consumer
under `tests/`, one page under `docs/`. This file states the rules that hold
across the repository in their shortest form; each links to the
document that states it in full. Where this file and a linked document differ,
the linked document holds.

## 1. Where a fact lives

| Tree | States |
|---|---|
| [`docs/`](docs/README.md) | one page per plugin or family, and the plugin conventions ([01](docs/01-plugin-development.md)) |
| [`README.zh-CN.md`](README.zh-CN.md) | the README in Chinese; English is the source of truth, and a lagging section is stated in its header |
| [`mcpp.toml`](mcpp.toml) | the package: one feature per plugin, the declared payloads, the engine floor `[package] mcpp` |
| [`tests/<plugin>/`](tests/) | one consumer project per plugin; CI builds each and asserts on its output |
| [`tools/README.md`](tools/README.md) | the tools family's own account |
| [`.agents/docs/`](.agents/docs/) | design records: the reasoning, the measurements, what was refuted |

A change of behaviour updates the plugin's page and its consumer in the same
pull request.

## 2. Architecture

- **A plugin provides a mechanism, not a policy.** No fixed version, path or
  library list a project could reasonably choose. Configuration is `build.mcpp`
  first ([01 §2](docs/01-plugin-development.md#2-rules)).
- **Four families answer four questions.** `rules-*` how a translation unit is
  compiled by a compiler mcpp does not drive; `tools-*` what the build program
  does itself; `dist-*` what comes out of the link, and in what form a user
  installs it; `deps-*` where a library comes from. The prefix is load-bearing
  ([00](docs/00-engine-and-rules.md)).
- **Names are three segments.** `mcpp.<family>.<plugin>`; the second segments
  `core`, `plugins`, `deps`, `rules`, `dist`, `tools` belong to the namespace,
  and a third-party plugin names its modules `mcpp.<its namespace>.*`
  ([01 §3](docs/01-plugin-development.md#3-layers-names-and-the-engine-floor)).
- **Three layers, one way down.** The engine's `mcpp` (L1), the general library
  `mcpp.plugins.*` (L2, feature `plugins-core`; every plugin implies it), the
  plugins (L3). A layer imports only lower layers.
- **Every plugin states an engine floor.** The collection's floor is
  `[package] mcpp`; the index descriptor states the highest floor among the
  plugins, so a project on an older engine is refused at resolution.
- **A tool resolves in one order, for every plugin:** the build program's
  choice, the plugin's legacy variable, the engine's override, the declared
  payload. A choice never asks for the payload — a build program that names its
  own tool must not trigger a download
  ([02](docs/02-tool-sources.md)).

## 3. Code

- C++23 modules: `import std;`, plus the module that defines a type. No unit
  includes a header or branches on the platform in the preprocessor.
- The code compiles under the four lanes CI runs: GCC with libstdc++, clang
  with libc++, clang with libstdc++'s `std` module (an editor's clangd), and
  MSVC where a plugin claims Windows. No compiler's leniency is relied on; the
  known differences and their workarounds are stated in
  [01 §4](docs/01-plugin-development.md#4-the-c-a-plugin-is-written-in).
- A behaviour kept past the release that replaced it lives in `compat/`, one
  unit per behaviour, with a retirement date six months on;
  `.github/scripts/check-compat-retirement.sh` fails once a date has passed.

## 4. Build and test

- CI (`.github/workflows/ci.yml`) builds every fixture under `tests/` with the
  pinned mcpp on the GCC lane (`consumers`), and adds `toolchain-llvm`,
  `toolchain-editor`, `rules-cross-platform` and `deps-managed-msvc`.
- Every documented behaviour has a criterion: a CI check, on every platform the
  plugin claims ([01 §2](docs/01-plugin-development.md#2-rules), rule 9).
- A change to a plugin builds its fixture, not only the package.

## 5. Change workflow

- Every change goes through a pull request; `main` is never pushed to directly.
  Commits and pull-request text are English, declarative, without emoji or
  attribution trailers. One pull request does one thing; all required checks
  pass before merge.
- Review follows [`.github/CODEOWNERS`](.github/CODEOWNERS): the family rows
  route it; a change to the feature set or the engine floor in `mcpp.toml`
  needs the package owner.
- Adding a plugin is the checklist in
  [01 §5](docs/01-plugin-development.md#5-a-new-plugin): a module file, a
  feature, a consumer, a table row with its floor, a page, a version bump.
- A release changes the version in `mcpp.toml` and tags `v<version>`; the tag
  publishes the source archive the index descriptor names.
