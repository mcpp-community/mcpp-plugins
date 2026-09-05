# mcpp.tools.*

Build-time utilities that are not rules. A rule (`mcpp.rules.<x>`) states how
one kind of translation unit is compiled by a compiler mcpp does not drive: it
submits an action and the engine schedules it. A tool states something a build
program needs that no compiler performs, and does it while `build.mcpp` runs.

| member | feature | what it does |
|---|---|---|
| `mcpp.tools.embed` | `tools-embed` | writes a data file into a header the program compiles in, as a byte array or a 32-bit word array |

Each member is one file, `tools/<x>.cppm`, declaring `export module
mcpp.tools.<x>;`, added to the source set by the feature `tools-<x>` in
`mcpp.toml`, and importing nothing but `std`, `mcpp` and `mcpp.plugins`. A
utility written for a single consumer belongs in that consumer's `build.mcpp`;
a member here is one that more than one consumer needs.
