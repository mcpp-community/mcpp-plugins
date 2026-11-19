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
// HERE A MEMBER DESCRIBES THE STEP AND A RUNNER RUNS IT. `program` is a list
// of portable operations -- run a command, touch, make, remove and copy
// files, list files into a named list, fail when a list is empty -- with
// every value taken verbatim, so a path keeps its spaces, semicolons and
// backslashes on every host. A member never writes the runner's language.
//
// THE RUNNER IS THIS PACKAGE'S OWN PROGRAM, `steps` (0.27.0). `command` writes
// the program as a step file (`mcpp::plugins::steps`, in the lib root, which
// the runner reads), only when its text changes, and returns the action's
// argument vector, `{<runner>, <file>}`, with both as the inputs it declares,
// so a changed program or runner runs the action again and an unchanged one
// does not. The feature that adds this module declares the runner as its tool
// (`[features.tools-steps] tools = ["steps"]`): it is built once for the
// machine that builds, and every build program that links this library with
// the feature active receives its path (mcpp 2026.11.19.1, SPEC-007 R6.3),
// a framework's user as well as the package that names this collection.
//
// The runner was `cmake -P` over a rendered script in 0.25.0 and 0.26.0, and
// each member that ran steps declared `xim:cmake` for it. mcpp-community/mcpp#955
// asked the engine to interpret these operations; the engine instead lets a
// plugin carry its own program, so the operations stay this package's
// vocabulary, versioned with it.
//
// Loops here index rather than iterate: an inline function of an imported
// module that iterates a string fails to compile in the importer under
// GCC 16.1 ("inlining failed in call to always_inline").

export module mcpp.tools.steps;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.plugins.fs;

export namespace mcpp::tools::steps {

using op_kind = mcpp::plugins::steps::op_kind;
using op      = mcpp::plugins::steps::op;

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

// ─── The runner: `steps`, a program of this package (0.27.0) ──────────────

// The runner's path in this build program, from the feature's tool. Empty only
// where the engine gives this program no such tool, and the engine then
// reports the undeclared tool itself (`build-program/undeclared-tool`); a
// member refuses with `missing_runner` rather than plan an action without a
// program.
inline std::string runner() { return mcpp::dep_bin("mcpp.plugins", "steps"); }

// The refusal of a member that has no runner; `who` is the member's module.
inline std::string missing_runner(std::string_view who) {
    return std::format(
        "{}: the runner of its step programs, `steps` of mcpp.plugins, is not available to this "
        "build program. The feature `tools-steps` declares it, and mcpp 2026.11.19.1 or newer "
        "gives it to every build program that links mcpp.plugins with that feature active.",
        who);
}

// What an action declares to run a step program.
struct invocation {
    std::vector<std::string> argv;     // the runner, then the step file
    std::vector<std::string> inputs;   // the runner and the step file
};

// Writes `p` as `<base>.steps` unless the file already holds it, and returns
// how an action runs it with `runner`, the path `runner()` answered. The
// comment line names `who`, so a file found in an output directory says what
// wrote it.
inline invocation command(const std::string& runner, const std::filesystem::path& base,
                          std::string_view who, const program& p) {
    std::filesystem::path file = base;
    file += ".steps";
    mcpp::plugins::fs::write_if_changed(file, mcpp::plugins::steps::write(p.ops(),
        "Written by " + std::string(who) + " through mcpp.tools.steps. Do not edit."));
    const std::string path = mcpp::plugins::fs::generic(file);
    return { .argv = { runner, path }, .inputs = { runner, path } };
}

} // namespace mcpp::tools::steps
