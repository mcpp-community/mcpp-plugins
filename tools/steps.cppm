// mcpp.tools.steps -- an action whose work is more than one command (0.25.0).
//
// An action runs one program with one argument vector, and no shell stands
// between the engine and it: ninja starts it directly, with `CreateProcess` on
// Windows. Some steps are more than one command, or need a file set that
// exists only when they run -- a compiler whose outputs no plan can list,
// then the stamp that stands for them; a tool that takes no directory, so its
// inputs are listed when it runs; an archive that gains files an earlier step
// named only by running. Before 0.25.0 each member answered this its own way:
// `/bin/sh` scripts, which a Windows host cannot start; a Java program on the
// JDK a member already needed; `cmake -P` over a script.
//
// HERE A MEMBER DESCRIBES THE STEP AND A BACKEND RUNS IT. `program` is a list
// of portable operations -- run a command, touch, make, remove and copy
// files, list files into a named list, fail when a list is empty -- with
// every value taken verbatim, so a path keeps its spaces, semicolons and
// backslashes on every host. A member never writes the backend's language.
//
// THE BACKEND IS `cmake -P` UNTIL THE ENGINE RUNS A STEP PROGRAM ITSELF
// (mcpp-community/mcpp#955). `command` renders the program as a CMake script,
// written only when its text changes, and returns the action's argument
// vector and the inputs it declares -- the runner and the script, so a changed
// program or runner runs the action again and an unchanged one does not.
// `runner_spec` is the runner the member resolves through `mcpp.plugins.tool`;
// a member that runs steps declares it on its own table,
// `"xim:cmake" = { version = ">=3.31", provision = "on-request" }`, so a plan
// that runs none installs nothing. When the engine runs step programs, these
// two functions change and no member does.
//
// Loops here index rather than iterate: an inline function of an imported
// module that iterates a string fails to compile in the importer under
// GCC 16.1 ("inlining failed in call to always_inline").

export module mcpp.tools.steps;

import std;
import mcpp;
import mcpp.plugins.fs;
import mcpp.plugins.tool;

export namespace mcpp::tools::steps {

struct run_options {
    // The directory the command runs in; empty is the build's.
    std::string working_directory;
    // A list whose values follow the argument vector, as further arguments.
    std::string append_list;
};

struct glob_options {
    // Descend into subdirectories.
    bool recursive = false;
    // Give each match relative to this directory rather than absolute.
    std::string relative_to;
    // Drop a match whose path, as it is given, matches one of these patterns
    // (0.26.0). Unlike the pattern that finds the files, an exclusion applies
    // to the whole path: `*` matches any run of characters, `/` included, and
    // `?` one character; `*/platform/ios/App/*` drops everything below such a
    // directory at any depth.
    std::vector<std::string> exclude;
};

enum class op_kind {
    run, touch, mkdir, remove, remove_glob, copy_file, copy_tree, copy_glob,
    rename, symlink, glob, append, require,
};

// One operation. `values` is the argument vector of `run`, the values of
// `append` and the exclusions of `glob`; `a` and `b` are an operation's
// operands; `list` names the list an operation reads or extends.
struct op {
    op_kind                  kind;
    std::vector<std::string> values;
    std::string              a, b, list;
    bool                     recursive = false;
    bool                     if_exists = false;
};

// The operations of one step, run in order; the first that fails fails the
// step. A list is named by letters, digits and `_`, and starts empty.
class program {
public:
    // Runs a program. A non-zero status fails the step; the program's output
    // is the step's.
    program& run(std::vector<std::string> argv, run_options o = {}) {
        op x{ op_kind::run };
        x.values = std::move(argv);
        x.a = std::move(o.working_directory);
        x.list = std::move(o.append_list);
        return add(std::move(x));
    }
    // Creates the file, or updates its time, and its directory.
    program& touch(std::string file)               { return add({ op_kind::touch, {}, std::move(file) }); }
    program& mkdir(std::string dir)                { return add({ op_kind::mkdir, {}, std::move(dir) }); }
    // Removes a file or a directory tree; one that does not exist is not an error.
    program& remove(std::string path)              { return add({ op_kind::remove, {}, std::move(path) }); }
    // Removes the files a pattern (`*`, `?`, `[...]` in the last component) matches.
    program& remove_glob(std::string pattern)      { return add({ op_kind::remove_glob, {}, std::move(pattern) }); }
    // Copies a file, creating the destination's directory.
    program& copy_file(std::string from, std::string to) {
        return add({ op_kind::copy_file, {}, std::move(from), std::move(to) });
    }
    // Copies the contents of directory `from` into directory `to`. With
    // `if_exists`, a `from` that is not a directory when the step runs is skipped.
    program& copy_tree(std::string from, std::string to, bool if_exists = false) {
        op x{ op_kind::copy_tree, {}, std::move(from), std::move(to) };
        x.if_exists = if_exists;
        return add(std::move(x));
    }
    // Copies the files a pattern matches into a directory, creating it.
    program& copy_glob(std::string pattern, std::string to_dir) {
        return add({ op_kind::copy_glob, {}, std::move(pattern), std::move(to_dir) });
    }
    program& rename(std::string from, std::string to) {
        return add({ op_kind::rename, {}, std::move(from), std::move(to) });
    }
    // A symbolic link at `link` naming `target`.
    program& symlink(std::string target, std::string link) {
        return add({ op_kind::symlink, {}, std::move(target), std::move(link) });
    }
    // Appends the files a pattern matches, sorted, to a list.
    program& glob(std::string list, std::string pattern, glob_options o = {}) {
        op x{ op_kind::glob, std::move(o.exclude), std::move(pattern), std::move(o.relative_to), std::move(list) };
        x.recursive = o.recursive;
        return add(std::move(x));
    }
    // Appends values known when the plan is made to a list.
    program& append(std::string list, std::vector<std::string> values) {
        op x{ op_kind::append, std::move(values) };
        x.list = std::move(list);
        return add(std::move(x));
    }
    // Fails the step with `message` when the list is empty.
    program& require(std::string list, std::string message) {
        op x{ op_kind::require, {}, std::move(message) };
        x.list = std::move(list);
        return add(std::move(x));
    }

    const std::vector<op>& ops() const { return ops_; }
    bool empty() const { return ops_.empty(); }

private:
    program& add(op x) { ops_.push_back(std::move(x)); return *this; }
    std::vector<op> ops_;
};

// ─── The backend: `cmake -P` (until mcpp-community/mcpp#955) ───────────────

namespace cmake_backend {

// A value as a bracket argument, with a bracket no value contains.
inline std::string bracket(std::string_view value) {
    std::string eq = "==";
    while (value.find("]" + eq + "]") != std::string_view::npos) eq += '=';
    return "[" + eq + "[" + std::string(value) + "]" + eq + "]";
}

inline std::string brackets(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ' ';
        out += bracket(values[i]);
    }
    return out;
}

inline bool valid_list_name(const std::string& name) {
    if (name.empty()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!std::isalnum(c) && c != '_') return false;
    }
    return true;
}

inline std::string var(const std::string& list) { return "mcpp_steps_" + list; }

// An exclusion pattern as an anchored regular expression over the whole path:
// `*` is any run of characters, `?` one, a bracket expression stays one, and
// every other character stands for itself.
inline std::string exclusion_regex(const std::string& pattern) {
    std::string out = "^";
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '*') {
            out += ".*";
        } else if (c == '?') {
            out += '.';
        } else if (c == '[') {
            const std::size_t close = pattern.find(']', i + 1);
            if (close == std::string::npos) { out += "\\["; continue; }
            std::string set = pattern.substr(i + 1, close - i - 1);
            if (!set.empty() && set[0] == '!') set[0] = '^';
            out += "[" + set + "]";
            i = close;
        } else {
            if (std::string_view(".+()^$|\\{}").find(c) != std::string_view::npos) out += '\\';
            out += c;
        }
    }
    return out + "$";
}

inline std::string parent_of(const std::string& path) {
    return std::filesystem::path(path).parent_path().generic_string();
}

// The script. A list name outside letters, digits and `_` is a member's
// mistake and is refused here rather than rendered into the script.
inline std::string render(const program& p) {
    std::string out;
    std::vector<std::string> lists;
    const auto& ops = p.ops();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const std::string& l = ops[i].list;
        if (l.empty()) continue;
        if (!valid_list_name(l))
            throw std::invalid_argument("mcpp.tools.steps: list name '" + l + "' is not letters, digits and _");
        if (std::find(lists.begin(), lists.end(), l) == lists.end()) {
            lists.push_back(l);
            out += "set(" + var(l) + ")\n";
        }
    }
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const op& o = ops[i];
        switch (o.kind) {
        case op_kind::run:
            out += "execute_process(COMMAND " + brackets(o.values);
            if (!o.list.empty()) out += " ${" + var(o.list) + "}";
            if (!o.a.empty()) out += "\n    WORKING_DIRECTORY " + bracket(o.a);
            out += "\n    COMMAND_ERROR_IS_FATAL ANY)\n";
            break;
        case op_kind::touch:
            if (!parent_of(o.a).empty()) out += "file(MAKE_DIRECTORY " + bracket(parent_of(o.a)) + ")\n";
            out += "file(TOUCH " + bracket(o.a) + ")\n";
            break;
        case op_kind::mkdir:
            out += "file(MAKE_DIRECTORY " + bracket(o.a) + ")\n";
            break;
        case op_kind::remove:
            out += "file(REMOVE_RECURSE " + bracket(o.a) + ")\n";
            break;
        case op_kind::remove_glob:
            out += "file(GLOB mcpp_steps_matched LIST_DIRECTORIES false " + bracket(o.a) + ")\n"
                   "if(mcpp_steps_matched)\n"
                   "    file(REMOVE ${mcpp_steps_matched})\n"
                   "endif()\n";
            break;
        case op_kind::copy_file:
            if (!parent_of(o.b).empty()) out += "file(MAKE_DIRECTORY " + bracket(parent_of(o.b)) + ")\n";
            out += "file(COPY_FILE " + bracket(o.a) + " " + bracket(o.b) + ")\n";
            break;
        case op_kind::copy_tree: {
            // A trailing `/` copies the directory's contents rather than the directory.
            std::string body = "file(MAKE_DIRECTORY " + bracket(o.b) + ")\n"
                               "file(COPY " + bracket(o.a + "/") + " DESTINATION " + bracket(o.b) + ")\n";
            out += o.if_exists ? "if(IS_DIRECTORY " + bracket(o.a) + ")\n" + body + "endif()\n" : body;
            break;
        }
        case op_kind::copy_glob:
            out += "file(GLOB mcpp_steps_matched LIST_DIRECTORIES false " + bracket(o.a) + ")\n"
                   "file(MAKE_DIRECTORY " + bracket(o.b) + ")\n"
                   "if(mcpp_steps_matched)\n"
                   "    file(COPY ${mcpp_steps_matched} DESTINATION " + bracket(o.b) + ")\n"
                   "endif()\n";
            break;
        case op_kind::rename:
            out += "file(RENAME " + bracket(o.a) + " " + bracket(o.b) + ")\n";
            break;
        case op_kind::symlink:
            out += "file(CREATE_LINK " + bracket(o.a) + " " + bracket(o.b) + " SYMBOLIC)\n";
            break;
        case op_kind::glob:
            out += std::string(o.recursive ? "file(GLOB_RECURSE" : "file(GLOB") + " mcpp_steps_matched LIST_DIRECTORIES false";
            if (!o.b.empty()) out += " RELATIVE " + bracket(o.b);
            out += " " + bracket(o.a) + ")\n";
            for (std::size_t k = 0; k < o.values.size(); ++k)
                out += "list(FILTER mcpp_steps_matched EXCLUDE REGEX " + bracket(exclusion_regex(o.values[k])) + ")\n";
            out += "list(APPEND " + var(o.list) + " ${mcpp_steps_matched})\n";
            break;
        case op_kind::append:
            if (!o.values.empty()) out += "list(APPEND " + var(o.list) + " " + brackets(o.values) + ")\n";
            break;
        case op_kind::require:
            out += "if(NOT " + var(o.list) + ")\n"
                   "    message(FATAL_ERROR " + bracket(o.a) + ")\n"
                   "endif()\n";
            break;
        }
    }
    return out;
}

} // namespace cmake_backend

// The program a member resolves to run steps: `xim:cmake` in its two layouts
// (`bin/` and, on macOS, `CMake.app/Contents/bin`). `who` is the member's
// module, for the decision record and the refusal text.
inline mcpp::plugins::tool::spec runner_spec(std::string who) {
    return { .who = std::move(who), .package = "cmake", .programs = {"cmake"},
             .bin_dirs = {"bin", "CMake.app/Contents/bin"}, .option = "options::cmake" };
}

// What an action declares to run a step program.
struct invocation {
    std::vector<std::string> argv;     // the runner, then its arguments
    std::vector<std::string> inputs;   // the runner and the written program
};

// Writes `p` beside `base` (the backend adds its extension) unless the file
// already holds it, and returns how an action runs it with `runner`, the
// program `runner_spec` resolved. The first line names `who`, so a file found
// in an output directory says what wrote it.
inline invocation command(const std::string& runner, const std::filesystem::path& base,
                          std::string_view who, const program& p) {
    std::filesystem::path file = base;
    file += ".cmake";
    mcpp::plugins::fs::write_if_changed(file,
        "# Written by " + std::string(who) + " through mcpp.tools.steps. Do not edit.\n"
        + cmake_backend::render(p));
    const std::string path = mcpp::plugins::fs::generic(file);
    return { .argv = { runner, "-P", path }, .inputs = { runner, path } };
}

} // namespace mcpp::tools::steps
