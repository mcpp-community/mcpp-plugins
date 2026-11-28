export module mcpp.plugins:package;

import std;
import :json;


// mcpp::plugins::package -- the metadata of a staged package.
//
// `mcpp pack` (2026.10.27.1+, SPEC-014 §4.5) writes `mcpp-package.json` at the
// root of every staged tree: the package, its three axes, its legs, the systems
// whose loaders it carries, how it is started, and every file with its role,
// its leg and its digest. It is the input of a package format's provider, of
// `mcpp pack --merge` and of `mcpp emit xpkg`. A provider reads it here rather
// than walking the tree and deciding for itself which file is the program: the
// engine has already decided, once, for every format.
//
// SCHEMA 1 AND ONLY SCHEMA 1. A document of another schema is refused rather
// than read as far as it goes: a field whose meaning changed would be read
// with the old one.
export namespace mcpp::plugins::package {

struct file {
    std::string path;     // relative to the tree's root, `/`-separated
    std::string role;     // program | image | library | loader | launcher | resource | metadata
    std::string leg;      // the leg's triple; empty when every leg shares the file
    std::string sha256;
};

struct info {
    int                      schema = 0;
    std::string              name;
    std::string              version;
    std::string              platform;   // `<package>@<version>`; empty: none
    std::string              linkage;    // static | shared | declared
    std::string              mode;       // system | vendored | self-contained | static
    std::vector<std::string> legs;       // canonical triples
    std::vector<std::string> systems;    // `<os>-<arch>` of the loaders under libexec/
    std::vector<std::string> runner;     // the platform's runner argv; empty: native
    // The entry of each architecture (universal packages), in the document's
    // order: architecture, then the entry's path in the tree.
    std::vector<std::pair<std::string, std::string>> entry;
    std::string              program;    // the program's path (one native leg)
    std::vector<file>        files;
    std::string              root;       // the tree the document was read from
};

namespace detail {

inline std::vector<std::string> strings_of(const json::value* v) {
    std::vector<std::string> out;
    if (!v || v->type != json::value::kind::array) return out;
    for (auto const& item : v->items)
        if (item.type == json::value::kind::string) out.push_back(item.text);
    return out;
}

} // namespace detail

// The document at `<stage_dir>/mcpp-package.json`, or why it cannot be read.
inline std::expected<info, std::string> read(std::string_view stage_dir) {
    std::string root(stage_dir);
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    const std::string path = root + "/mcpp-package.json";
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected(std::format("{} cannot be read: the tree is not a staged package", path));
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::value doc;
    if (!json::parse_json(text, doc) || doc.type != json::value::kind::object)
        return std::unexpected(std::format("{} is not a JSON object", path));
    info p;
    p.root = root;
    if (const json::value* s = doc.get("schema"); s && s->type == json::value::kind::number)
        p.schema = std::atoi(s->text.c_str());
    if (p.schema != 1)
        return std::unexpected(std::format(
            "{} states schema {}; this collection reads schema 1 (mcpp SPEC-014 §4.5)", path, p.schema));
    p.name     = json::string_of(doc.get("name"));
    p.version  = json::string_of(doc.get("version"));
    p.platform = json::string_of(doc.get("platform"));
    p.linkage  = json::string_of(doc.get("linkage"));
    p.mode     = json::string_of(doc.get("mode"));
    p.legs     = detail::strings_of(doc.get("legs"));
    p.systems  = detail::strings_of(doc.get("systems"));
    if (const json::value* launch = doc.get("launch"); launch && launch->type == json::value::kind::object) {
        p.runner  = detail::strings_of(launch->get("runner"));
        p.program = json::string_of(launch->get("program"));
        if (const json::value* e = launch->get("entry"); e && e->type == json::value::kind::object)
            for (auto const& m : e->members)
                if (m.second.type == json::value::kind::string) p.entry.emplace_back(m.first, m.second.text);
    }
    const json::value* files = doc.get("files");
    if (!files || files->type != json::value::kind::array)
        return std::unexpected(std::format("{} has no files array", path));
    for (auto const& f : files->items) {
        if (f.type != json::value::kind::object) continue;
        p.files.push_back({ json::string_of(f.get("path")), json::string_of(f.get("role")),
                            json::string_of(f.get("leg")), json::string_of(f.get("sha256")) });
    }
    return p;
}

// The files of `p` with `role`, in the document's order.
inline std::vector<file> files_with_role(const info& p, std::string_view role) {
    std::vector<file> out;
    for (auto const& f : p.files)
        if (f.role == role) out.push_back(f);
    return out;
}

// The entry of `arch`, or empty.
inline std::string entry_of(const info& p, std::string_view arch) {
    for (auto const& [a, path] : p.entry)
        if (a == arch) return path;
    return {};
}

} // namespace mcpp::plugins::package
