// mcpp.tools.island -- see the block comment below.
//
// A MEMBER RATHER THAN PART OF THE LIB ROOT, AND THAT IS A CONSISTENCY FIX.
//
// The lib root carries what MEMBERS share: `mcpp::plugins::surface` is there
// because three rules produce the same declarations for a consumer and a fourth
// copy would drift. Nothing in this collection uses the island generator -- a
// project does, directly from its own `build.mcpp`, exactly as it uses
// `mcpp.tools.embed`. Leaving it in the lib root gave it to every consumer
// whether or not they asked, while `tools-embed` next to it required a feature.
module;
#include <cctype>

export module mcpp.tools.island;

import std;
import mcpp;
// For `write_if_different` and for the path-to-namespace derivations, which the
// lib root owns because the shader lane and this one must answer a directory
// named `default` the same way.
import mcpp.plugins;


// mcpp.tools.island -- the boundary a device island is reached across.
//
// WHAT THIS GENERATES AND WHY IT IS NOT THE SAME THING AS `surface`.
//
// `surface` writes the whole interface for a DATA payload, because an address
// and a size are all there is to decide. An island is CODE, and its C++
// interface -- which functions, which types, what happens on failure -- is a
// design decision no generator makes well. That interface stays hand-written.
//
// What is mechanical is everything AROUND the entry points: an include guard,
// an `extern "C"` block, the `__cplusplus` dance, the namespace the C++ side
// reaches them through, and a module wrapper. Ten lines of boilerplate per one
// line of content, written the same way in every project that has an island.
//
// THE DECLARATION STILL EXISTS ONCE. It moves from a hand-written header into
// the build program, and both artefacts are produced from it -- the header the
// device compiler includes and the module the C++ side imports. Splitting a
// declaration across those two is the failure this removes, and it is the worst
// one available at this boundary: C language linkage does not mangle, so two
// copies that disagree are one symbol, the link is clean, and each side reads
// the arguments by its own ABI.
//
// `export using ::name;` IS WHAT MAKES IT WORK WITHOUT A C PARSER.
//
// The module re-exports names rather than restating signatures, so the
// generator needs only the identifier before the `(`. Measured with GCC 16.1:
// a consumer that imports the module and never includes the header calls the
// entry point and links against an implementation compiled by a DIFFERENT
// driver, which is the arrangement a device island actually has.
//
// THE NAMES ARE IN A NAMESPACE, AND THE NAMESPACE IS THE MODULE'S PATH.
//
// `docs/42` states one rule for both lanes: the module name and the namespace
// are one identifier path. The shader lane obeys it; this generator did not,
// and put every entry point at global scope, so `import app.kernels` bought a
// file name and nothing else. It obeys it now: a root's directories extend the
// namespace exactly as a payload tree's do.
//
// A NAMESPACE OVER A FLAT SYMBOL IS A LOOKUP ALIAS, AND THE CHECK IS WHAT MAKES
// IT HONEST. Measured 2026-09-08 with clang++ (DPC++ 7.1.0), `-std=c++23`: two
// modules re-exporting one `extern "C"` name into two namespaces produce two
// spellings of ONE entity -- `&a::f == &b::f`. The shader lane does not have
// this problem because it composes its own symbols and can put the path in
// them; an island's symbol is written by its author and this generator only
// reads it. So `scan` refuses two entry points with one name in one root: a
// name then exists in exactly one namespace, and the namespace cannot lie about
// what a call resolves to.
//
// IT IS OPTIONAL, AND A PROJECT THAT WRITES ITS OWN HEADER KEEPS IT. Nothing
// here is required to have an island; it removes boilerplate from projects that
// want it removed.
export namespace mcpp::tools::island {

struct options {
    // The module the C++ side imports: `myapp.kernels`. The header is named
    // after it too, so one name places both files, and it is also the namespace
    // the entry points arrive in: `myapp::kernels::…`.
    std::string module_name;
    // Where they are written. The caller puts this on the include path so the
    // device translation unit can include the header.
    std::string out_dir;
    // For the generated files' first line: "mcpp.rules.cuda".
    std::string produced_by;
    // False emits the header alone, for a project that wants the boilerplate
    // removed but keeps a hand-written seam that includes rather than imports.
    bool emit_module = true;

    // WHERE THE ISLANDS ARE, AND WHY THIS IS A TREE RATHER THAN A FILE LIST.
    //
    // A root is a directory that holds implementations. Its internal structure
    // is what extends the namespace, so it is DECLARED rather than inferred
    // from the set of files handed in -- a file list's common ancestor moves
    // when a file is added, and a consumer's qualified name would move with it.
    //
    // Several roots mean one entry point implemented several times: a device
    // island and a host fallback are two roots, and exactly one of them is in
    // any link. A single file is also a legal root, for a project that keeps
    // one implementation beside another in one directory.
    //
    // THEY MUST NOT DEPEND ON THE ACCELERATOR. `mcpp::device_sources()` is
    // narrowed by `accel` and is empty under `--no-accel`; roots taken from it
    // would lose the device tree in a CPU-only build, and the entry point's
    // namespace would then come from the fallback tree instead. Roots are
    // directories on disk, and `accel` decides only what is compiled.
    std::vector<std::string> roots;

    // The root whose directory structure says WHERE entry points live. Every
    // other root supplies implementations and its structure is never read for
    // naming, so a fallback tree may be one flat file or six directories and
    // may be reorganised without renaming anything a consumer wrote.
    //
    // This is a naming role and not a rank: every root compiles, links and is
    // equally a backend. Empty means `roots.front()`.
    //
    // An entry point the layout root does not declare -- a kernel only one
    // backend has -- takes the namespace of the first root that does.
    std::string layout_root;

    // The files a root is read for. Headers are deliberately absent: a project
    // that declares its entry points in a `.cuh` and defines them in a `.cu`
    // would otherwise hand the uniqueness check two files for one name and be
    // refused for a layout that is correct.
    std::vector<std::string> extensions;

    // Non-empty emits a second spelling beside each entry point that carries
    // it: `inline constexpr auto blur = opkit_blur;`.
    //
    // An island's symbol is global to the whole program, so an entry point
    // carries a package prefix whether or not it sits in a namespace, and the
    // namespace then repeats what the prefix already said. The authored name is
    // always emitted and stays canonical -- it is what `nm`, a link error, a
    // profiler and `dlsym` show. This is a convenience at the call site.
    std::string strip_prefix;

    // The marker `scan()` looks for. It is defined as nothing by the generated
    // header, so the island includes that header and then writes the marker in
    // front of each entry point it exports.
    //
    // THE NAME SAYS THE MECHANISM, NOT THE DOMAIN. What is marked is exported
    // across a generated boundary, with C linkage; both halves are in the name
    // and neither narrows it. Two alternatives were considered:
    //
    //   - `MCPP_ISLAND_EXPORT` is precise inside docs/20's vocabulary and
    //     narrow outside it. The generator does not know what compiler produced
    //     the object, and works for any `extern "C"` boundary -- a C library
    //     shim has one and is not an island.
    //   - anything ending `_API` was rejected outright. That suffix
    //     conventionally expands to a visibility attribute
    //     (`__declspec(dllexport)`, `visibility("default")`), and this expands
    //     to nothing. Borrowing it would promise something it does not do, and
    //     would collide with a project that later wants the real thing.
    //
    // `MCPP_<verb>_<qualifier>` also leaves room: a future marker read by a
    // different generator joins the family rather than inventing a second
    // shape.
    std::string marker = "MCPP_EXPORT_C";
};

// One marked entry point. `decl` is what the header will contain, copied
// verbatim from the island; `name_space` is where the C++ side reaches it.
struct entry {
    std::string decl;
    std::string name;
    std::vector<std::string> name_space;
    std::string origin;                  // the file it was first seen in
};

struct emitted {
    std::string header_file;      // the generated boundary header
    std::string interface_file;   // the `.cppm`; empty when `emit_module` is false
    std::string include_dir;      // for `mcpp::include_dir`
    std::string module_name;      // empty when `emit_module` is false
};

// The extensions a root is read for when `options::extensions` is empty. Every
// language a device compiler consumes that produces an OBJECT, plus the C and
// C++ a host implementation of the same boundary is written in. A shading
// language is absent: a `.comp` is data by the time it reaches a link and
// carries no `extern "C"` entry point.
inline std::vector<std::string> default_extensions() {
    return {".c", ".cc", ".cpp", ".cxx", ".cu", ".hip", ".sycl",
            ".asc", ".cce", ".cl", ".metal"};
}

// The flags that make a compiler read the generated header before the island's
// first line, so the island writes neither an include nor anything else.
//
// WHY THIS IS THE DEFAULT RATHER THAN AN INCLUDE LINE. The header is generated:
// it is not in the source tree, and a project using this generator has no
// hand-written header at all. An `#include` of it is therefore a line naming a
// file its author never opens, and it buys no self-containment -- that `.c`
// could not be compiled outside mcpp with or without it, because the file it
// names does not exist until mcpp writes it.
//
// NOTHING IS LOST BY REMOVING IT. The compiler still sees the declarations, so
// a definition whose signature drifted from its declaration still fails where it
// was written rather than at the link, which is the second job the include did.
// A project that prefers the line keeps it: the header carries a guard, so
// including it as well is a no-op.
//
// `/FI` takes the path as one token and `-include` takes it as two, which is why
// this returns a vector rather than a string.
inline std::vector<std::string> force_include_flags(const std::string& header,
                                                    std::string_view compilerId) {
    if (compilerId == "msvc") return { "/FI" + header };
    return { "-include", header };
}

// The identifier immediately before the first `(`. That is the whole parse this
// needs: the module re-exports the NAME and the header carries the signature
// verbatim, so nothing here has to understand a C declaration.
inline std::string entry_name(std::string_view decl) {
    const auto paren = decl.find('(');
    if (paren == std::string_view::npos) return {};
    auto end = paren;
    while (end > 0 && (decl[end - 1] == ' ' || decl[end - 1] == '\t')) --end;
    auto begin = end;
    while (begin > 0) {
        const char c = decl[begin - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') --begin;
        else break;
    }
    return std::string(decl.substr(begin, end - begin));
}

// An entry point the scan cannot see -- generated by something else, or behind
// a macro this does not expand. The project states the declaration and where it
// belongs, and everything downstream is identical.
inline entry declared(std::string decl, std::vector<std::string> name_space = {}) {
    entry e;
    e.name = entry_name(decl);
    e.decl = std::move(decl);
    e.name_space = std::move(name_space);
    e.origin = "declared in the build program";
    return e;
}

// ─── the scan ──────────────────────────────────────────────────────────────

namespace detail {

// Runs of whitespace collapsed, so a wrapped signature reads as one
// declaration and two halves that wrote it differently still compare equal.
//
// INDEXED RATHER THAN A RANGE-FOR, AND THAT IS NOT A STYLE CHOICE.
//
// `for (char c : decl)` over a `std::string` inside an exported inline function
// makes GCC 16 instantiate `std::string::iterator` in this BMI, and the
// consumer's build program then fails to compile with
//
//   error: inlining failed in call to 'always_inline'
//   __normal_iterator<char*, basic_string<char>>::operator*():
//   function body not available
//
// in `<bits/stl_iterator.h>`, naming neither this file nor this loop. Indexing
// touches no iterator type and compiles.
inline std::string flatten(std::string_view decl) {
    std::string flat;
    bool space = false;
    for (std::size_t k = 0; k < decl.size(); ++k) {
        const char c = decl[k];
        if (c == '\n' || c == '\t' || c == '\r' || c == ' ') {
            if (!flat.empty()) space = true;
        } else {
            if (space) flat += ' ';
            space = false;
            flat += c;
        }
    }
    return flat;
}

inline bool has_extension(const std::filesystem::path& p,
                          std::span<const std::string> exts) {
    const auto e = p.extension().string();
    for (auto const& want : exts) if (e == want) return true;
    return false;
}

// The files of one root, sorted. THE ORDER IS PART OF THE CONTRACT: the walk
// order of a directory is the filesystem's, so without the sort the generated
// files differ between two runs that changed nothing, `write_if_different`
// rewrites them, the force-included header's timestamp moves and every island
// translation unit rebuilds.
inline bool files_under(const std::string& root, std::span<const std::string> exts,
                        std::vector<std::string>& out, std::string& base) {
    std::error_code ec;
    const std::filesystem::path p(root);
    if (std::filesystem::is_regular_file(p, ec)) {
        base = p.parent_path().string();
        out.push_back(p.string());
        return true;
    }
    if (!std::filesystem::is_directory(p, ec)) return false;
    base = p.string();
    for (std::filesystem::recursive_directory_iterator it(p, ec), end; it != end;
         it.increment(ec)) {
        if (ec) return false;
        if (!it->is_regular_file(ec)) continue;
        if (has_extension(it->path(), exts)) out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return true;
}

// The declarations one file marks, in source order.
inline bool marked_in(const std::string& src, const std::string& marker,
                      std::vector<std::string>& decls) {
    std::ifstream in(src);
    if (!in) {
        std::cerr << std::format("mcpp.tools.island: cannot read {}\n", src);
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    for (std::size_t at = text.find(marker); at != std::string::npos;
         at = text.find(marker, at + 1)) {
        // The marker's own definition in the generated header is not an entry
        // point. Skipped by requiring a `(` before the next `;` or `{`, which a
        // `#define` line does not have.
        std::size_t i = at + marker.size();
        int depth = 0;
        bool sawOpen = false;
        std::size_t end = std::string::npos;
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '(') { ++depth; sawOpen = true; }
            else if (c == ')') {
                if (--depth == 0) { end = i; break; }
            } else if (!sawOpen && (c == ';' || c == '{' || c == '\n')) {
                if (c == '\n') continue;   // a signature may wrap
                break;                      // `;` or `{` with no `(`
            }
        }
        if (end == std::string::npos) {
            if (!sawOpen) continue;         // a `#define` of the marker
            std::cerr << std::format(
                "mcpp.tools.island: {} carries `{}` whose parameter list does not "
                "close.\n  A marked entry point is one declaration, and the generator "
                "copies it verbatim.\n", src, marker);
            return false;
        }
        auto flat = flatten(text.substr(at + marker.size(),
                                        end + 1 - (at + marker.size())));
        if (!flat.empty()) decls.push_back(std::move(flat));
    }
    return true;
}

inline std::string joined(std::span<const std::string> segs) {
    std::string s;
    for (auto const& one : segs) { if (!s.empty()) s += "::"; s += one; }
    return s;
}

} // namespace detail

// THE ENTRY POINTS, TAKEN FROM WHERE THEY ARE DEFINED.
//
// The signature lives beside the definition and exists once, which is the
// arrangement a reader expects and the one that cannot drift.
//
// IT IS NOT A C PARSER, AND DOES NOT NEED TO BE. From the marker it copies
// verbatim up to the parenthesis that closes the parameter list, matching
// nesting so a function pointer parameter does not end it early. What it copies
// is what the header will contain, so anything the island's compiler accepts in
// a declaration -- a macro, a qualifier, a multi-line signature -- travels
// through unexamined.
//
// TWO REFUSALS, AND THEY ANSWER DIFFERENT QUESTIONS.
//
// One name twice in ONE root is a collision: C language linkage does not
// mangle, so those are one symbol, and a namespace that appeared to separate
// them would be a lookup alias promising an isolation the linker does not
// provide. Refused, naming both files.
//
// One name in SEVERAL roots is one entry point implemented several times --
// the ordinary shape of a seam, where exactly one implementation is in any
// link. The declarations must then agree verbatim, and this is the only place
// in the toolchain where both texts exist at once: the two never meet at the
// link, so nothing else can compare them.
inline std::optional<std::vector<entry>> scan(const options& opt) {
    if (opt.roots.empty()) {
        std::cerr << "mcpp.tools.island: options::roots is empty; there is nothing "
                     "to scan.\n  A root is the directory an island's sources live "
                     "under.\n";
        return std::nullopt;
    }
    const std::string layout = opt.layout_root.empty() ? opt.roots.front()
                                                       : opt.layout_root;
    if (std::find(opt.roots.begin(), opt.roots.end(), layout) == opt.roots.end()) {
        std::cerr << std::format(
            "mcpp.tools.island: layout_root `{}` is not one of the roots.\n"
            "  The root that supplies the shape has to be a root.\n", layout);
        return std::nullopt;
    }
    const auto exts = opt.extensions.empty() ? default_extensions() : opt.extensions;

    // OVERLAPPING ROOTS ARE REFUSED. A file reachable from two of them has two
    // namespace paths, and which one it got would depend on the order of the
    // list. It would also be read twice and merged with itself, so the entry
    // would look like two implementations agreeing -- a misconfiguration that
    // produces a plausible result is worse than one that stops.
    for (std::size_t i = 0; i < opt.roots.size(); ++i) {
        std::error_code ec;
        const auto a = std::filesystem::weakly_canonical(opt.roots[i], ec);
        for (std::size_t j = i + 1; j < opt.roots.size(); ++j) {
            const auto b = std::filesystem::weakly_canonical(opt.roots[j], ec);
            const auto& outer = a.native().size() <= b.native().size() ? a : b;
            const auto& inner = a.native().size() <= b.native().size() ? b : a;
            const auto rel = inner.lexically_relative(outer);
            const auto reltext = rel.generic_string();
            if (reltext.empty() || reltext.starts_with("..")) continue;
            std::cerr << std::format(
                "mcpp.tools.island: the roots `{}` and `{}` overlap.\n"
                "  A file reachable from both has two namespace paths, and which one "
                "it got\n  would depend on the order of this list. Roots are separate "
                "implementation trees.\n",
                outer.string(), inner.string());
            return std::nullopt;
        }
    }

    struct record {
        entry e;
        std::size_t root = 0;
        bool from_layout = false;
    };
    std::vector<record> found;
    auto find_by_name = [&](std::string_view n) -> record* {
        for (auto& r : found) if (r.e.name == n) return &r;
        return nullptr;
    };

    for (std::size_t ri = 0; ri < opt.roots.size(); ++ri) {
        const auto& root = opt.roots[ri];
        std::vector<std::string> files;
        std::string base;
        if (!detail::files_under(root, exts, files, base)) {
            std::cerr << std::format(
                "mcpp.tools.island: `{}` is neither a directory nor a file.\n"
                "  Roots are where an island's sources live, on disk.\n", root);
            return std::nullopt;
        }
        for (auto const& f : files) {
            mcpp::rerun_if_changed(f.c_str());
            std::vector<std::string> decls;
            if (!detail::marked_in(f, opt.marker, decls)) return std::nullopt;
            const auto ns = mcpp::plugins::names::namespace_of(f, base);
            for (auto& d : decls) {
                const auto name = entry_name(d);
                if (name.empty()) {
                    std::cerr << std::format(
                        "mcpp.tools.island: cannot find an entry point name in `{}`\n"
                        "  in {}.\n", d, f);
                    return std::nullopt;
                }
                if (auto* seen = find_by_name(name)) {
                    if (seen->root == ri) {
                        std::cerr << std::format(
                            "mcpp.tools.island: `{}` is declared twice in the root `{}`.\n"
                            "  {}\n  {}\n"
                            "  C language linkage does not mangle, so these are one "
                            "symbol and\n  a namespace would not separate them. If they "
                            "are two implementations of\n  one entry point, they belong "
                            "in two roots.\n",
                            name, root, seen->e.origin, f);
                        return std::nullopt;
                    }
                    if (seen->e.decl != d) {
                        std::cerr << std::format(
                            "mcpp.tools.island: two definitions of `{}` declare it "
                            "differently.\n"
                            "  {}\n    {}\n"
                            "  {}\n    {}\n"
                            "  C language linkage does not mangle, so these never meet "
                            "at the link:\n  whichever one is in the artifact reads its "
                            "arguments by its own signature.\n",
                            name, seen->e.origin, seen->e.decl, f, d);
                        return std::nullopt;
                    }
                    // The shape comes from one stated root. A second
                    // implementation adds nothing to where the entry point
                    // lives, unless the layout root is the one adding it.
                    if (!seen->from_layout && root == layout) {
                        seen->e.name_space = ns;
                        seen->from_layout = true;
                    }
                    continue;
                }
                record r;
                r.e.decl = d;
                r.e.name = name;
                r.e.name_space = ns;
                r.e.origin = f;
                r.root = ri;
                r.from_layout = (root == layout);
                found.push_back(std::move(r));
            }
        }
        // ADDING A FILE HAS TO RE-RUN THIS PROGRAM. Declared inputs are hashed
        // contents, so a new file changes none of them; the glob's fingerprint
        // is the sorted set of matching paths, which is exactly the question
        // "which files are here". The pattern is relative to the manifest
        // directory, so a root outside it registers its files and nothing else.
        //
        // ONLY FOR A DIRECTORY ROOT. A single file is its own root, and its
        // parent directory is not part of it: globbing that parent would make
        // an unrelated file beside it an input to this program.
        std::error_code dirEc;
        if (std::filesystem::is_directory(std::filesystem::path(root), dirEc)) {
            const auto rel = std::filesystem::path(base).lexically_relative(
                                 std::filesystem::path(mcpp::manifest_dir()));
            const auto reltext = rel.generic_string();
            if (!reltext.empty() && !reltext.starts_with("..")) {
                const std::string prefix = reltext == "." ? std::string()
                                                          : reltext + "/";
                for (auto const& e : exts)
                    mcpp::rerun_if_changed_glob((prefix + "**/*" + e).c_str());
            }
        }
    }

    if (found.empty()) {
        std::string where;
        for (auto const& r : opt.roots) { if (!where.empty()) where += ", "; where += r; }
        std::cerr << std::format(
            "mcpp.tools.island: no entry point marked `{}` under {}.\n"
            "  A root that yields nothing is a misspelled path or a marker that "
            "never arrived,\n  and an empty module fails later and less clearly.\n",
            opt.marker, where);
        return std::nullopt;
    }

    std::vector<entry> out;
    out.reserve(found.size());
    for (auto& r : found) out.push_back(std::move(r.e));
    // Grouped and stable: the generated files are a function of the tree.
    std::sort(out.begin(), out.end(), [](const entry& a, const entry& b) {
        const auto an = detail::joined(a.name_space), bn = detail::joined(b.name_space);
        return an == bn ? a.name < b.name : an < bn;
    });
    return out;
}

// ─── the emission ──────────────────────────────────────────────────────────

inline std::optional<emitted> emit(std::span<const entry> entries,
                                   const options& opt) {
    if (entries.empty()) return emitted{};
    if (opt.module_name.empty() || opt.out_dir.empty()) {
        std::cerr << "mcpp.tools.island: module_name and out_dir are required\n";
        return std::nullopt;
    }
    const auto by  = opt.produced_by.empty() ? std::string("mcpp.tools.island")
                                             : opt.produced_by;
    const auto dir = std::filesystem::path(opt.out_dir);

    const auto modSegs = mcpp::plugins::names::split_module_name(opt.module_name);
    if (modSegs.empty()) {
        std::cerr << std::format("mcpp.tools.island: `{}` is not a usable module name\n",
                                 opt.module_name);
        return std::nullopt;
    }
    for (auto const& seg : modSegs) {
        if (mcpp::plugins::names::identifier(seg, "x") != seg) {
            std::cerr << std::format(
                "mcpp.tools.island: `{}` is not a usable module name: the segment `{}` "
                "is not a C++ identifier.\n  Each segment becomes a namespace.\n",
                opt.module_name, seg);
            return std::nullopt;
        }
    }

    for (auto const& e : entries) {
        if (e.name.empty()) {
            std::cerr << std::format(
                "mcpp.tools.island: cannot find an entry point name in `{}`.\n"
                "  Each entry is a C declaration, e.g.\n"
                "    \"int saxpy_device(float a, const float* x, unsigned n)\"\n", e.decl);
            return std::nullopt;
        }
    }

    emitted out;
    const auto guard = [&] {
        std::string g = "MCPP_ISLAND_";
        for (std::size_t i = 0; i < opt.module_name.size(); ++i) {
            const char c = opt.module_name[i];
            g += std::isalnum(static_cast<unsigned char>(c))
               ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
        }
        return g + "_H";
    }();

    // THE HEADER IS FLAT AND HAS NO NAMESPACES, and that is not an omission. It
    // is read by a C or a device compiler, and neither has a namespace to read.
    // The module is a view onto it.
    std::string h;
    h += std::format("// Generated by mcpp.tools.island for {0}. Do not edit.\n"
                     "//\n"
                     "// The island's boundary. Included by the device translation unit, which\n"
                     "// is compiled by a compiler mcpp did not resolve -- so the interface is\n"
                     "// `extern \"C\"`, because the two sides share no C++ ABI.\n"
                     "#ifndef {1}\n#define {1}\n"
                     "// Defined as nothing so the island can mark its entry points and\n"
                     "// still compile: the marker is for the generator to find, not for\n"
                     "// the compiler to act on.\n"
                     "#ifndef {2}\n#define {2}\n#endif\n"
                     "#ifdef __cplusplus\nextern \"C\" {{\n#endif\n\n",
                     by, guard, opt.marker);
    for (auto const& e : entries) h += e.decl + ";\n";
    h += "\n#ifdef __cplusplus\n}\n#endif\n#endif\n";

    out.header_file = (dir / (opt.module_name + ".h")).string();
    out.include_dir = dir.string();
    if (!mcpp::plugins::surface::write_if_different(out.header_file, h)) {
        std::cerr << std::format("mcpp.tools.island: cannot write {}\n", out.header_file);
        return std::nullopt;
    }
    if (!opt.emit_module) return out;

    std::string m;
    m += std::format("// Generated by mcpp.tools.island for {0}. Do not edit.\n"
                     "//\n"
                     "// The C++ side imports this instead of including the header. Names are\n"
                     "// re-exported rather than restated, so this file carries no second copy\n"
                     "// of a signature -- which at a C-linkage boundary is the copy that can\n"
                     "// disagree without anything noticing.\n"
                     "module;\n#include \"{1}\"\n"
                     "export module {2};\n\n", by,
                     std::filesystem::path(out.header_file).filename().string(),
                     opt.module_name);

    // One block per namespace. `entries` arrives grouped, and a caller that
    // built the list itself gets the same grouping from this loop as long as it
    // kept equal namespaces adjacent.
    std::vector<std::string> shortNames;      // per entry, empty when there is none
    shortNames.resize(entries.size());
    if (!opt.strip_prefix.empty()) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].name.starts_with(opt.strip_prefix)) continue;
            auto s = mcpp::plugins::names::identifier(
                entries[i].name.substr(opt.strip_prefix.size()), "entry");
            if (s.empty() || s == entries[i].name) continue;
            shortNames[i] = std::move(s);
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (shortNames[i].empty()) continue;
            for (std::size_t j = 0; j < entries.size(); ++j) {
                if (i == j) continue;
                const bool sameNs = entries[i].name_space == entries[j].name_space;
                if (!sameNs) continue;
                if (shortNames[i] == shortNames[j] || shortNames[i] == entries[j].name) {
                    std::cerr << std::format(
                        "mcpp.tools.island: `{}` and `{}` both reach `{}` once `{}` is "
                        "stripped.\n  {}\n  {}\n  A short name is a second spelling of "
                        "one entry point, not a shared one.\n",
                        entries[i].name, entries[j].name, shortNames[i],
                        opt.strip_prefix, entries[i].origin, entries[j].origin);
                    return std::nullopt;
                }
            }
        }
    }

    std::string openNs;
    bool open = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::vector<std::string> full = modSegs;
        for (auto const& seg : entries[i].name_space) full.push_back(seg);
        const auto path = detail::joined(full);
        if (!open || path != openNs) {
            if (open) m += "}\n\n";
            m += std::format("export namespace {} {{\n", path);
            openNs = path;
            open = true;
        }
        m += std::format("using ::{};\n", entries[i].name);
        if (!shortNames[i].empty())
            m += std::format("inline constexpr auto {} = {};\n",
                             shortNames[i], entries[i].name);
    }
    if (open) m += "}\n";

    out.interface_file = (dir / (opt.module_name + ".cppm")).string();
    out.module_name    = opt.module_name;
    if (!mcpp::plugins::surface::write_if_different(out.interface_file, m)) {
        std::cerr << std::format("mcpp.tools.island: cannot write {}\n", out.interface_file);
        return std::nullopt;
    }
    return out;
}

} // namespace mcpp::tools::island
