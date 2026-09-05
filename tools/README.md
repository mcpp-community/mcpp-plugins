# mcpp.tools.*

Build-time utilities that are not rules. A rule (`mcpp.rules.<x>`) states how
one kind of translation unit is compiled by a compiler mcpp does not drive; a
tool states something a build program needs that is independent of any
compiler: generating a header from a data file, computing a value that several
rules share, checking an invariant of the source tree.

The directory is empty in 0.1.0. A member is added when a second consumer
needs it; a utility written for one consumer belongs in that consumer's
`build.mcpp`. Each member is one file, `tools/<x>.cppm`, declaring
`export module mcpp.tools.<x>;`, added to the source set by the feature
`tools-<x>` in `mcpp.toml`, and importing nothing but `std`, `mcpp` and
`mcpp.plugins`.
