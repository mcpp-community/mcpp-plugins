# `tools-steps`

`mcpp.tools.steps` is how a member runs an action whose work is more than one command: the member describes the step as portable operations, and `steps`, a program of this package, runs them on every host, with no shell.

## `tools-steps`

Module `mcpp.tools.steps`; engine floor: 2026.11.19.1; implied by `dist-apk` and `dist-apple`.

**Needs and behaviour.** The runner `steps` (`tools/steps_main.cpp`), which the feature declares as its tool, `tools = ["steps"]` (0.27.0). An edge that activates the feature acts as if it had written `tools = ["steps"]`: the engine builds the runner once for the machine that builds, keeps it in the tool store, and gives its path to every build program that links this package with the feature active, a framework's user included (mcpp SPEC-007 R6.3, 2026.11.19.1). `runner()` answers that path, and `missing_runner(<module>)` is the refusal of a member that has none. The operations run in order and the first that fails fails the step: the runner names the step file, the operation and its position, and the cause on standard error, and exits 1. `command` writes the program beside the path it is given, as `<path>.steps`, only when its text changes, and returns the action's argument vector, `{<runner>, <file>}`, and the inputs it declares, the same two, so a changed program or runner runs the action again and an unchanged one does not.

| operation of `program` | what it does when the step runs |
|---|---|
| `run(argv, {working_directory, append_list})` | runs a program; a non-zero status fails the step and its output is the step's; `append_list` adds a list's values as further arguments |
| `touch(file)`, `mkdir(dir)` | creates or updates a file, creating its directory; creates a directory |
| `remove(path)`, `remove_glob(pattern)` | removes a file or a tree, or the files a pattern matches; absent is not an error |
| `copy_file(from, to)`, `copy_tree(from, to, if_exists)`, `copy_glob(pattern, dir)`, `rename(from, to)`, `symlink(target, link)` | file operations; `copy_tree` copies a directory's contents, and with `if_exists` skips a source that is not a directory |
| `glob(list, pattern, {recursive, relative_to, exclude})` | appends the matching files, sorted, to a named list; from 0.26.0 `exclude` drops a match whose whole path matches one of its patterns, where `*` crosses a `/` |
| `append(list, values)`, `require(list, message)` | appends values known at plan time; fails the step with `message` when the list is empty |

Every value is taken verbatim, so a path keeps its spaces, semicolons, quotes, backslashes and non-ASCII text on Linux, macOS and Windows: the runner starts a program with its argument vector and no shell, through mcpplibs.os, which on Windows quotes each argument by the rule the started program splits its command line with, and it converts every name from UTF-8. A list is named by letters, digits and `_`, and starts empty. A pattern's wildcards (`*`, `?`, `[...]`, negated by `!` or `^`) apply to its last component, and `recursive` matches it in every subdirectory.

**The semantics of 0.25.0 and 0.26.0.** Those releases rendered a program as a CMake script and ran `cmake -P` over it, with the `xim:cmake` each member that ran steps declared, which the index offers only as CMake 4. Where the table leaves a detail open, the runner decides it as that backend did:

- the matches of a pattern are sorted by their bytes, and each is spelled as the pattern's directory was written, then `/` and the rest; a pattern whose directory is relative, or that has none, is the working directory's;
- a name is matched without regard to the case of ASCII letters on Windows and macOS and exactly elsewhere; an exclusion is compared exactly on every host; `relative_to` drops the names a match shares with it from the root, compared as names are matched;
- a directory is never a match; a flat pattern passes over a link to a directory, and a recursive one does not enter the link but matches it by its name;
- `copy_tree` and `copy_glob` copy a link as a link and a file with its permissions and its time; `copy_tree` gives each directory its source's permissions, `to` included; a file in the way of a copy is replaced, read-only or not; `copy_file` copies a file's contents and permissions;
- `remove` and `remove_glob` remove read-only files and directories as well;
- `run` starts a program named by a relative path with a directory from its `working_directory`, and looks a bare name up on `PATH`.

Three details differ from that backend, each where CMake altered a value or read a byte as a character: a value that holds `;` stays one value of its list, where CMake split it; a list is empty only when it holds no value, where CMake also took for empty a list whose one value it reads as false (`""`, `0`, `OFF`, `NO`, `FALSE`, `N`, `IGNORE`, `NOTFOUND`, or a value ending in `-NOTFOUND`); and `?` or a bracket expression stands for one character of UTF-8 text, compared without case wherever the rest of the name is, where CMake read one byte and compared a bracket expression as written against a name it had put in lower case.

**The step file.** `command` writes the program in a format of counted values, and the runner reads it with the same code, `mcpp::plugins::steps` in the lib root, so the writer and the reader do not drift. The first line names the format, `mcpp-steps 1`; a line that begins with `#` where a record could begin is a comment, and `command` writes one naming the member. Each operation is one record: its name; its flags, `-` for none or `r` (`recursive`) and `e` (`if_exists`) in that order; the operands `a` and `b`; the list it reads or extends; and the count of its values, then each value. The fields are separated by single spaces and the record ends with a line break. An operand, a list name and a value are each written as `<bytes>:<value>`, the value's length in bytes, a colon and the bytes, so nothing is quoted or escaped and a value may hold any byte, a line break included:

```
mcpp-steps 1
# Written by mcpp.dist.apk through mcpp.tools.steps. Do not edit.
remove - 16:/out/classes dir 0: 0: 0
run - 0: 0: 0: 4 10:/jdk/javac 2:-d 16:/out/classes dir 6:A.java
```

The file is internal: a member builds a `program` and never writes the format, and the runner refuses a file whose first line names another format or whose counts do not hold.

**Before 0.27.0.** The runner was `cmake -P` over the rendered script: each member resolved `cmake` with `runner_spec(<module>)` through `mcpp.plugins.tool`, declared `"xim:cmake" = { version = ">=3.31", provision = "on-request" }` on its own table, and took another `cmake` through `options::cmake`. mcpp-community/mcpp#955 asked the engine to run step programs itself; the engine instead lets a feature carry its package's own program to every build program that links the package (mcpp 2026.11.19.1), so the operations remain this package's and change with its version. 0.27.0 removes the CMake backend, `runner_spec`, `cmake_backend::render` and the members' `options::cmake` and `xim:cmake`.
