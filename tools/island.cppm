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
// For `write_if_different`, which the lib root owns because every generator in
// this package needs the same "do not touch a file whose content is unchanged"
// rule.
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
// an `extern "C"` block, the `__cplusplus` dance, and a module wrapper whose
// only content is a global module fragment and a re-export. Ten lines of
// boilerplate per one line of content, written the same way in every project
// that has an island. That is what this generates.
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
// IT IS OPTIONAL, AND A PROJECT THAT WRITES ITS OWN HEADER KEEPS IT. Nothing
// here is required to have an island; it removes boilerplate from projects that
// want it removed.
export namespace mcpp::tools::island {

struct options {
    // The module the C++ side imports: `myapp.kernels`. The header is named
    // after it too, so one name places both files.
    std::string module_name;
    // Where they are written. The caller puts this on the include path so the
    // device translation unit can include the header.
    std::string out_dir;
    // For the generated files' first line: "mcpp.rules.cuda".
    std::string produced_by;
    // False emits the header alone, for a project that wants the boilerplate
    // removed but keeps a hand-written seam that includes rather than imports.
    bool emit_module = true;
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

struct emitted {
    std::string header_file;      // the generated boundary header
    std::string interface_file;   // the `.cppm`; empty when `emit_module` is false
    std::string include_dir;      // for `mcpp::include_dir`
    std::string module_name;      // empty when `emit_module` is false
};

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

// THE ENTRY POINTS, TAKEN FROM WHERE THEY ARE DEFINED.
//
// `emit` takes a list of declarations, which is exact and requires the project
// to write each signature in its build program. This reads them out of the
// island instead, so the signature lives beside the definition and exists once
// -- which is the arrangement a reader expects and the one that cannot drift.
//
// IT IS NOT A C PARSER, AND DOES NOT NEED TO BE. From the marker it copies
// verbatim up to the parenthesis that closes the parameter list, matching
// nesting so a function pointer parameter does not end it early. What it copies
// is what the header will contain, so anything the island's compiler accepts in
// a declaration -- a macro, a qualifier, a multi-line signature -- travels
// through unexamined.
//
// A marker with no `(` after it is refused rather than skipped: a marked entry
// point that produced no declaration would leave the island defining a function
// nothing declares, and the consumer's failure would be an unresolved name in a
// different file.
inline std::optional<std::vector<std::string>>
scan(std::span<const std::string> sources, const options& opt) {
    std::vector<std::string> entries;
    for (auto const& src : sources) {
        std::ifstream in(src);
        if (!in) {
            std::cerr << std::format("mcpp.tools.island: cannot read {}\n", src);
            return std::nullopt;
        }
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        for (std::size_t at = text.find(opt.marker); at != std::string::npos;
             at = text.find(opt.marker, at + 1)) {
            // The marker's own definition in the generated header is not an
            // entry point. Skipped by requiring a `(` before the next `;` or
            // `{`, which a `#define` line does not have.
            std::size_t i = at + opt.marker.size();
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
                    "copies it verbatim.\n", src, opt.marker);
                return std::nullopt;
            }
            auto decl = text.substr(at + opt.marker.size(),
                                    end + 1 - (at + opt.marker.size()));
            // Collapse the runs of whitespace a wrapped signature carries, so
            // the header reads as one declaration per line.
            // INDEXED RATHER THAN A RANGE-FOR, AND THAT IS NOT A STYLE CHOICE.
            //
            // `for (char c : decl)` over a `std::string` inside an exported
            // inline function makes GCC 16 instantiate `std::string::iterator`
            // in this BMI, and the consumer's build program then fails to
            // compile with
            //
            //   error: inlining failed in call to 'always_inline'
            //   __normal_iterator<char*, basic_string<char>>::operator*():
            //   function body not available
            //
            // in `<bits/stl_iterator.h>`, naming neither this file nor this
            // loop. Indexing touches no iterator type and compiles.
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
            if (!flat.empty()) entries.push_back(std::move(flat));
        }
    }
    return entries;
}

inline std::optional<emitted> emit(std::span<const std::string> entries,
                                   const options& opt) {
    if (entries.empty()) return emitted{};
    if (opt.module_name.empty() || opt.out_dir.empty()) {
        std::cerr << "mcpp.tools.island: module_name and out_dir are required\n";
        return std::nullopt;
    }
    const auto by  = opt.produced_by.empty() ? std::string("mcpp.tools.island")
                                             : opt.produced_by;
    const auto dir = std::filesystem::path(opt.out_dir);

    // A name the generator could not find is refused rather than skipped: a
    // declaration that produced no re-export would compile, and the consumer's
    // failure would be an unresolved name three files away.
    std::vector<std::string> names;
    for (auto const& e : entries) {
        auto n = entry_name(e);
        if (n.empty()) {
            std::cerr << std::format(
                "mcpp.tools.island: cannot find an entry point name in `{}`.\n"
                "  Each entry is a C declaration, e.g.\n"
                "    \"int saxpy_device(float a, const float* x, unsigned n)\"\n", e);
            return std::nullopt;
        }
        names.push_back(std::move(n));
    }

    emitted out;
    const auto guard = [&] {
        std::string g = "MCPP_ISLAND_";
        for (char c : opt.module_name)
            g += std::isalnum(static_cast<unsigned char>(c))
               ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
        return g + "_H";
    }();

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
    for (auto const& e : entries) h += e + ";\n";
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
    for (auto const& n : names) m += std::format("export using ::{};\n", n);

    out.interface_file = (dir / (opt.module_name + ".cppm")).string();
    out.module_name    = opt.module_name;
    if (!mcpp::plugins::surface::write_if_different(out.interface_file, m)) {
        std::cerr << std::format("mcpp.tools.island: cannot write {}\n", out.interface_file);
        return std::nullopt;
    }
    return out;
}

} // namespace mcpp::tools::island
