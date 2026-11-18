# `tools-script`

`mcpp.tools.script` is how a member runs an action whose work is more than one command: `cmake -P` over a CMake script the member writes, on every host and with no shell.

## `tools-script`

Module `mcpp.tools.script`; engine floor: 2026.10.1.3; implied by `deps`, `dist-apk` and `dist-apple`.

**Needs and behaviour.** `cmake` 3.31 or newer, which each member that writes a script declares on its own table as `"xim:cmake" = { version = ">=3.31", provision = "on-request" }` and resolves with `cmake_spec(<module>)` through `mcpp.plugins.tool`, so a plan that writes no script installs nothing. A script runs its steps in order and fails on the first that fails. The member writes it at plan time, only when its text changes, and the action declares `cmake` and the script as inputs, so a changed script or tool runs the action again and an unchanged one does not.

| function | what it gives |
|---|---|
| `cmake_spec(who)` | the `xim:cmake` payload in its two layouts (`bin/`, and `CMake.app/Contents/bin` on macOS), for `mcpp::plugins::tool::resolve` |
| `bracket(value)`, `brackets(values)` | a value as a CMake bracket argument, taken verbatim: spaces, semicolons and backslashes stay in it |
| `run(argv, lists, working_directory)` | one command as `execute_process(... COMMAND_ERROR_IS_FATAL ANY)`, followed by the values of the CMake list variables `lists` names; its output is the build's |
| `command(cmake, script, who, text)` | writes the script and returns the action's `argv` (`<cmake> -P <script>`) and the inputs it declares |

A file set known only when the step runs is a CMake list the script builds, with `file(GLOB)` or `file(GLOB_RECURSE)`, whose results are sorted, and passes to `run` by name.

`mcpp.deps` re-exports `cmake_spec` and `bracket` under `mcpp::deps`, the names the deps members used before 0.25.0.
