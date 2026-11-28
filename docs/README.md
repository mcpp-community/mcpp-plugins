# mcpp-plugins Documentation

This tree is the manual for the members of `mcpp:plugins` and for the
conventions a build plugin follows. Each page states what a member does, how a
consumer writes it, and what its current limits are. The reasoning behind a
design, and what was measured and refuted on the way, lives in the dated
records under [`../.agents/docs/`](../.agents/docs/) — a page cites no record.

## Start here

| To | Read |
|---|---|
| use a member | its page below, and the [quick start](../README.md#quick-start) in the README |
| write a plugin of your own | [01 — Plugin Development](01-plugin-development.md) |
| know why a name, a floor or a claim is what it is | [00 — How the Members Meet the Engine](00-engine-and-rules.md) |
| name a tool a member runs | [02 — Where a Tool Comes From](02-tool-sources.md) |

## Pages

The first digit is the part, so a number says where a page belongs:

| | |
|---|---|
| `0x` | the framework: naming, conventions, where a tool comes from |
| `1x` | the rules: device and language compilation |
| `2x` | the tools: what the build program does itself |
| `3x` | the distribution: what comes out of the link |
| `4x` | the dependencies: where a library comes from |

### 0x — The framework

- [00 — How the Members Meet the Engine](00-engine-and-rules.md) — the families and their names, each rule's environment and claimed extensions, the engine floor, what a consumer names
- [01 — Plugin Development](01-plugin-development.md) — the conventions every plugin follows, the C++ a plugin is written in, a new member's checklist
- [02 — Where a Tool Comes From](02-tool-sources.md) — the one order every member resolves a tool in, and every way a project names its own

### 1x — Rules

- [10 — Device and Language Rules](10-rules.md) — `rules-ascendc`, `rules-cuda`, `rules-hip`, `rules-metal`, `rules-slang`, `rules-spirv`, `rules-swift`, `rules-sycl`
- [11 — `rules-qt`](11-rules-qt.md) — Qt's code generators, modules and runtime; Linguist tools
- [12 — `rules-qml`](12-rules-qml.md) — a QML application on `rules-qt`

### 2x — Tools

- [20 — `tools-embed`](20-tools-embed.md) — a data file compiled into the program
- [21 — `tools-island`](21-tools-island.md) — the `extern "C"` boundary and module of a code island
- [22 — `tools-steps`](22-tools-steps.md) — an action whose work is more than one command

### 3x — Distribution

- [30 — `dist-appimage`, `dist-deb`, `dist-rpm`, `dist-wix`, `dist-web`](30-dist.md)
- [31 — `dist-apple`](31-dist-apple.md) — a macOS or iOS bundle, signed, as a disk image
- [32 — `dist-apk`](32-dist-apk.md) — a signed APK or App Bundle, with Java, Kotlin and Maven libraries

### 4x — Dependencies

- [40 — `deps-vcpkg`, `deps-cmake`, `deps-archive`](40-deps.md)
