export module mcpp.plugins:graph;

import std;
import :json;


// mcpp::plugins::graph -- the resolved dependency graph the engine states.
//
// mcpp 2026.9.16.1 gives the ROOT project's build program `MCPP_GRAPH_FILE`
// (`mcpp::graph_file()`), a document naming every package of the resolved graph,
// dependencies before their requesters, each with its manifest directory and its
// `[package.metadata]` verbatim (mcpp docs/30, "Reading the resolved graph"). A
// member that merges what libraries contribute reads a table of its own out of
// that metadata: `[package.metadata.dist-apk]`, `[package.metadata.dist-apple]`.
//
// READ THROUGH THE ENVIRONMENT, NOT `mcpp::graph_file()`. This unit does not
// import `mcpp` (see the top of this file), and a member that called the
// accessor would not compile against an engine older than the one that added it.
// An unset or empty variable is an older engine, or a dependency's program, and
// reads as a graph with no packages, which leaves every member's behaviour as it
// was.
export namespace mcpp::plugins::graph {

struct package {
    std::string canonical;      // `namespace.name@version`
    std::string name;
    std::string namespace_;
    std::string version;
    bool        root = false;
    std::string manifest_dir;   // absolute
    json::value metadata;       // the package's [package.metadata], an object
};

struct document {
    bool                 present = false;   // MCPP_GRAPH_FILE named a file
    std::string          path;
    std::vector<package> packages;          // dependencies before requesters
};

// The graph this build program was given, or the reason it cannot be read. A
// variable naming a file that is missing or not a `mcpp.graph` document is an
// error rather than an empty graph: the engine promised a document, and reading
// nothing would silently drop every contribution.
inline std::expected<document, std::string> read() {
    document d;
    const char* env = std::getenv("MCPP_GRAPH_FILE");
    if (!env || !*env) return d;
    d.present = true;
    d.path = env;
    std::ifstream in(d.path, std::ios::binary);
    if (!in)
        return std::unexpected(std::format("the graph document {} cannot be read", d.path));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::value doc;
    if (!json::parse_json(text, doc) || doc.type != json::value::kind::object)
        return std::unexpected(std::format("the graph document {} is not JSON", d.path));
    if (json::string_of(doc.get("kind")) != "mcpp.graph")
        return std::unexpected(std::format("the graph document {} is not a mcpp.graph document", d.path));
    const json::value* list = doc.get("packages");
    if (!list || list->type != json::value::kind::array)
        return std::unexpected(std::format("the graph document {} has no packages array", d.path));
    for (auto const& entry : list->items) {
        if (entry.type != json::value::kind::object) continue;
        package p;
        if (const json::value* id = entry.get("package")) {
            p.canonical  = json::string_of(id->get("canonical"));
            p.name       = json::string_of(id->get("name"));
            p.namespace_ = json::string_of(id->get("namespace"));
            p.version    = json::string_of(id->get("version"));
        }
        if (const json::value* r = entry.get("root"))
            p.root = r->type == json::value::kind::boolean && r->text == "true";
        p.manifest_dir = json::string_of(entry.get("manifest_dir"));
        if (const json::value* m = entry.get("metadata"); m && m->type == json::value::kind::object)
            p.metadata = *m;
        d.packages.push_back(std::move(p));
    }
    return d;
}

// The package's `[package.metadata.<tool>]` table, or null.
inline const json::value* table_of(const package& p, std::string_view tool) {
    const json::value* t = p.metadata.get(tool);
    return t && t->type == json::value::kind::object ? t : nullptr;
}

// A path a package states, made absolute against its manifest directory.
inline std::string resolve(const package& p, const std::string& path) {
    if (path.empty() || std::filesystem::path(path).is_absolute()) return path;
    return (std::filesystem::path(p.manifest_dir) / path).lexically_normal().string();
}

// How a diagnostic names a package: its canonical identity, or its name.
inline std::string label_of(const package& p) {
    return !p.canonical.empty() ? p.canonical : p.name;
}

} // namespace mcpp::plugins::graph
