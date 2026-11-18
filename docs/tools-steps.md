# `tools-steps`

`mcpp.tools.steps` is how a member runs an action whose work is more than one command: the member describes the step as portable operations, and a backend runs it on every host, with no shell.

## `tools-steps`

Module `mcpp.tools.steps`; engine floor: 2026.10.1.3; implied by `dist-apk` and `dist-apple`.

**Needs and behaviour.** A runner, resolved by the member with `runner_spec(<module>)` through `mcpp.plugins.tool`. Until mcpp runs step programs itself (mcpp-community/mcpp#955) the runner is `cmake` 3.31 or newer, which each member that runs steps declares on its own table as `"xim:cmake" = { version = ">=3.31", provision = "on-request" }`, so a plan that runs no step installs nothing. The operations run in order and the first that fails fails the step. `command` writes the program beside the path it is given only when its text changes, and returns the action's argument vector and the inputs it declares, the runner and the program, so a changed program or runner runs the action again and an unchanged one does not.

| operation of `program` | what it does when the step runs |
|---|---|
| `run(argv, {working_directory, append_list})` | runs a program; a non-zero status fails the step and its output is the step's; `append_list` adds a list's values as further arguments |
| `touch(file)`, `mkdir(dir)` | creates or updates a file, creating its directory; creates a directory |
| `remove(path)`, `remove_glob(pattern)` | removes a file or a tree, or the files a pattern matches; absent is not an error |
| `copy_file(from, to)`, `copy_tree(from, to, if_exists)`, `copy_glob(pattern, dir)`, `rename(from, to)`, `symlink(target, link)` | file operations; `copy_tree` copies a directory's contents, and with `if_exists` skips a source that is not a directory |
| `glob(list, pattern, {recursive, relative_to})` | appends the matching files, sorted, to a named list |
| `append(list, values)`, `require(list, message)` | appends values known at plan time; fails the step with `message` when the list is empty |

Every value is taken verbatim, so a path keeps its spaces, semicolons and backslashes. A list is named by letters, digits and `_`, and starts empty. A pattern's wildcards (`*`, `?`, `[...]`) apply to its last component, and `recursive` matches it in every subdirectory.

The backend is internal. Today it renders the program as a CMake script and the action runs `cmake -P` over it; when the engine runs step programs, `runner_spec` and `command` change and no member does. `cmake_backend::render` is exported so a test can read what a program becomes.
