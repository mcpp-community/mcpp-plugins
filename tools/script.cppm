// mcpp.tools.script -- an action whose work is more than one command
// (0.25.0; 0.15.0-0.24.0 in `mcpp.deps`, for the deps members only).
//
// A member's action runs one program. When the work is several commands, or a
// file set known only when it runs -- install then copy, run a compiler then
// touch the stamp that stands for its outputs, list a directory then pass what
// it holds -- the action is `cmake -P <script>` over a script the member
// writes: one argument vector, on every host, with no shell between the engine
// and a tool (docs/plugin-development.md, rule 5). The script runs its steps
// in order and fails on the first that fails.
//
// A MEMBER OF ITS OWN, NOT PART OF `plugins-core`: the general library knows
// no foreign tool (docs/plugin-development.md, section 3), and this one runs
// CMake. Every member that writes a script implies `tools-script`.
//
// Three things make that the same mechanism in every member, and are here:
//
//   1. THE TOOL. `cmake_spec` is `xim:cmake` in its two layouts, found by the
//      one resolver (`mcpp.plugins.tool`). The member's feature declares
//      `"xim:cmake" = { version = ">=3.31", provision = "on-request" }`, so
//      only a plan that writes a script installs it.
//   2. THE VALUES. Every value is a bracket argument (`bracket`), so a path or
//      an argument keeps its spaces, semicolons and backslashes; `run` is one
//      command as `execute_process`, failing the script when it fails, with
//      the command's own output passed through.
//   3. THE EDGE. `command` writes the script only when its text changes
//      (`mcpp.plugins.fs::write_if_changed`), so its time stamp moves only
//      when the work does, and returns the argument vector and the inputs the
//      action declares: the tool and the script, so a changed script or tool
//      runs the action again.

export module mcpp.tools.script;

import std;
import mcpp;
import mcpp.plugins.fs;
import mcpp.plugins.tool;

export namespace mcpp::tools::script {

// The `cmake` a script runs: the payload a member's feature declares, in its
// two layouts (`bin/` and, on macOS, `CMake.app/Contents/bin`). `who` is the
// member's module, for the decision record and the refusal text.
inline mcpp::plugins::tool::spec cmake_spec(std::string who) {
    return { .who = std::move(who), .package = "cmake", .programs = {"cmake"},
             .bin_dirs = {"bin", "CMake.app/Contents/bin"}, .option = "options::cmake" };
}

// A value as a CMake bracket argument: taken verbatim, with no variable
// reference, escape or list separator read in it.
inline std::string bracket(std::string_view text) {
    return "[==[" + std::string(text) + "]==]";
}

// Several values, each a bracket argument, separated by spaces.
inline std::string brackets(const std::vector<std::string>& values) {
    std::string out;
    for (auto const& v : values) {
        if (!out.empty()) out += ' ';
        out += bracket(v);
    }
    return out;
}

// One command: `argv` as written, then the values of the CMake list variables
// named in `lists` (each `${name}`, so a list the script built at run time
// becomes arguments), in `working_directory` when one is given. A command that
// fails ends the script with a failure; its output reaches the build's.
inline std::string run(const std::vector<std::string>& argv,
                       const std::vector<std::string>& lists = {},
                       const std::string& working_directory = {}) {
    std::string out = "execute_process(COMMAND " + brackets(argv);
    for (auto const& l : lists) out += " ${" + l + "}";
    if (!working_directory.empty()) out += "\n    WORKING_DIRECTORY " + bracket(working_directory);
    out += "\n    COMMAND_ERROR_IS_FATAL ANY)\n";
    return out;
}

// What an action declares to run a script.
struct invocation {
    std::vector<std::string> argv;     // `<cmake> -P <script>`
    std::vector<std::string> inputs;   // the tool and the script
};

// Writes `text` to `script` unless it already holds it, and returns how an
// action runs it. `cmake` is the program `cmake_spec` resolved. The header
// line names the member, so a script found in an output directory says what
// wrote it.
inline invocation command(const std::string& cmake, const std::filesystem::path& script,
                          std::string_view who, const std::string& text) {
    mcpp::plugins::fs::write_if_changed(script,
        "# Written by " + std::string(who) + ". Do not edit.\n" + text);
    const std::string path = mcpp::plugins::fs::generic(script);
    return { .argv = { cmake, "-P", path }, .inputs = { cmake, path } };
}

} // namespace mcpp::tools::script
