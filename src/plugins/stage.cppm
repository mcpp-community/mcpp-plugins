export module mcpp.plugins:stage;

import std;

// mcpp::plugins::stage -- what the engine wrote beside a staged tree.
//
// SHARED BECAUSE TWO DIST MEMBERS READ IT. `mcpp pack` writes
// `<staged tree>.stage-manifest` (mcpp's docs/50, "The stage manifest"): a
// header, one `needs` line per library name the closure read (mcpp
// 2026.9.14.2+), and the list of staged files. `dist-apk` reads it to trust the
// native libraries under `lib/`, and `dist-apple` reads it to tell the dylibs
// staged beside a program from the resources staged there. Only the header and
// the `needs` lines are parsed; the file list is not.
export namespace mcpp::plugins::stage {

struct need {
    std::string name;    // as the needing object spells it
    std::string where;   // a staged path relative to the tree, `platform` or `unresolved`
};

struct manifest {
    bool              found  = false;   // the file exists beside the tree
    bool              walked = true;    // `closure = walked`
    std::string       reason;           // `reason = ...`, with `closure = not-walked`
    std::vector<need> needs;
};

inline manifest read_manifest(std::string tree) {
    manifest m;
    while (tree.size() > 1 && (tree.back() == '/' || tree.back() == '\\'))
        tree.pop_back();
    std::ifstream in(tree + ".stage-manifest", std::ios::binary);
    if (!in) return m;
    m.found = true;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "closure = not-walked") { m.walked = false; continue; }
        if (line.starts_with("reason = ")) { m.reason = line.substr(9); continue; }
        if (!line.starts_with("needs\t")) continue;
        const auto second = line.find('\t', 6);
        if (second == std::string::npos) continue;
        m.needs.push_back({ line.substr(6, second - 6), line.substr(second + 1) });
    }
    return m;
}

} // namespace mcpp::plugins::stage
