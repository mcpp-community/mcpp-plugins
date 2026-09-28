// mcpp.plugins.fs -- deterministic file generation and placement for build
// programs (L2 of the build-plugin architecture, mcpp#734).
//
// Two things every plugin that writes or places files needs, and that none of
// them should write twice:
//
//   1. A GENERATED FILE KEEPS ITS TIME STAMP WHILE ITS CONTENT DOES NOT CHANGE.
//      A build program runs on every plan; a file it rewrites unconditionally
//      is a changed input of every action that reads it, and that action runs
//      again on every build. `write_if_changed` writes only a changed text.
//
//   2. A FILE THAT AN ACTION PRODUCES IS PLACED BESIDE THE PROGRAM THROUGH A
//      COPY THE GRAPH NAMES. `mcpp::deploy` takes a file the build graph knows
//      how to produce. A file a `prepare` action installs is not one: its name
//      is unknown to the graph until the action has run. `deploy_after` copies
//      each named file out of such a directory by an action that names it as
//      an output (SPEC-007 R3.2), and deploys the copy. Everything is declared
//      before anything exists, so `mcpp emit build-database` plans it on a
//      machine that never built.
//
// Until 0.17.0 these lived in `mcpp.deps`, which re-exports them under their
// old names through a compatibility unit (`deps/compat/names.cppm`).

export module mcpp.plugins.fs;

import std;
import mcpp;

export namespace mcpp::plugins::fs {

inline std::string generic(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

// Writes `content` to `file` unless the file already holds it, so a file the
// build program generates keeps its time stamp from one plan to the next and
// the action that reads it does not run again. Returns whether it wrote.
inline bool write_if_changed(const std::filesystem::path& file, const std::string& content) {
    namespace stdfs = std::filesystem;
    std::error_code ec;
    if (stdfs::is_regular_file(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        const std::string old{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        if (old == content) return false;
    }
    stdfs::create_directories(file.parent_path(), ec);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << content;
    return true;
}

// One file of a directory to place beside the program: `file` relative to the
// directory (`share/opencc/t2s.json`), `to` the directory beside the program
// it is placed in (`BaseConfig/opencc`; empty or `.` is the program's own
// directory).
struct deploy_entry {
    std::string file;
    std::string to;
};

// A file placed by `deploy_after`: `path`, the copy the build produces (a node
// of the build graph, which a project's own actions may take as an input), and
// `to`, its path beside the program.
struct deployed_file {
    std::string path;
    std::string to;
};

// Copies each entry's file out of `root` once the action that writes `stamp`
// has run, and deploys the copy. `who` names the plugin in action ids. Two or
// more deployed files become one placement edge in the engine (mcpp 2026.9.28.3,
// `mcpp stage --list`), so the count of entries costs no per-file process.
inline std::vector<deployed_file> deploy_after(std::string_view who, const std::string& stamp,
                                               const std::filesystem::path& root,
                                               std::span<const deploy_entry> entries) {
    namespace stdfs = std::filesystem;
    std::vector<deployed_file> out;
    const stdfs::path base = stdfs::path(mcpp::out_dir()) / "deps-deploy" / std::string(who);
    for (auto const& e : entries) {
        const stdfs::path rel = stdfs::path(e.file).lexically_normal();
        const std::string src  = generic(root / rel);
        const std::string copy = generic(base / rel);
        // Index loops over a string: GCC 16 fails to inline a string iterator
        // inside a module's interface.
        std::string key = rel.generic_string();
        for (std::size_t i = 0; i < key.size(); ++i) if (key[i] == '/') key[i] = '.';
        const std::string id   = std::format("{}:deploy:{}", who, key);
        const std::string desc = std::format("DEPLOY {}", rel.generic_string());
        mcpp::action a;
        a.id          = id.c_str();
        a.role        = mcpp::roles::artifact;
        a.description = desc.c_str();
        a.arg("${mcpp.self}").arg("stage").arg("--output").arg(copy.c_str()).arg(src.c_str())
         .input(stamp.c_str()).output(copy.c_str()).submit();
        const std::string dir = e.to.empty() ? std::string(".") : e.to;
        mcpp::deploy(copy.c_str(), dir.c_str());
        const std::string to = generic(stdfs::path(dir) / rel.filename());
        out.push_back({copy, to});
    }
    return out;
}

} // namespace mcpp::plugins::fs
