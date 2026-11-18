# mcpp.tools.*

Build-time utilities that are not rules. A rule (`mcpp.rules.<x>`) states how
one kind of translation unit is compiled by a compiler mcpp does not drive: it
submits an action and the engine schedules it. A tool states something a build
program needs that no compiler performs, and does it while `build.mcpp` runs.

| member | feature | what it does |
|---|---|---|
| `mcpp.tools.embed` | `tools-embed` | writes a data file into a header the program compiles in, as a byte array or a 32-bit word array |
| `mcpp.tools.island` | `tools-island` | writes the `extern "C"` boundary and the C++ module of a code island |
| `mcpp.tools.script` | `tools-script` | runs an action whose work is more than one command as `cmake -P` over a script a member writes; the members that need it imply it |

Each member is one file, `tools/<x>.cppm`, declaring `export module
mcpp.tools.<x>;`, added to the source set by the feature `tools-<x>` in
`mcpp.toml`, and importing nothing but `std`, `mcpp`, `mcpp.plugins` and the
general library of `plugins-core`. A
utility written for a single consumer belongs in that consumer's `build.mcpp`;
a member here is one that more than one consumer needs.
