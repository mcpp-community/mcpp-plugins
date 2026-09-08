// mcpp.plugins.declare -- the build-program half of the surface.
//
// WHY THIS IS A SECOND UNIT AND NOT MORE OF THE LIB ROOT.
//
// `mcpp.plugins.surface` decides every name a consumer sees and writes every
// generated file. It imports only `std`, and that is load-bearing rather than
// tidy: the same code has to compile into `mcpp-embed`, an ordinary program,
// because a payload can only be a declared input of the edge that embeds it if
// that edge is an ACTION -- and an action's command is a binary.
//
// This unit is the other side. It knows `mcpp::action`, `mcpp::generated`,
// `mcpp::dep_bin` and the rest of the build-program contract, and it decides
// HOW the generation reaches the graph. Keeping the two apart is what lets one
// of them exist twice.
//
// A second unit beside the lib root was not possible before mcpp 2026.9.8.1:
// the units a host-module package contributes were ordered by PATH, so
// `rules/spirv.cppm` was compiled before `src/declare.cppm` and importing it
// failed with "failed to read compiled module". They are ordered by their
// import graph now, which is what makes this file expressible.

export module mcpp.plugins.declare;

import std;
import mcpp;
import mcpp.plugins;

export namespace mcpp::plugins {

// WHERE THE GENERATION HAPPENS, AND WHY IT DEPENDS ON THE STORAGE.
//
// Two of the three storages can be generated at PLAN time, and one cannot.
//
//   header    The bytes reach the artifact through generated data headers that
//             the payload's own compiler writes as an action output. The edge
//             from a changed payload to the artifact already exists, through
//             that action. Generating the surface here costs nothing and needs
//             no program.
//
//   sidecar   The bytes are never compiled. The accessor opens the file at RUN
//             time, so a rebuilt payload is picked up by the next run with
//             nothing in the graph to keep current.
//
//   object    The bytes become a section, through `.incbin` in a generated
//             assembly source. The assembly's TEXT is plan-time knowledge; the
//             OBJECT it produces is the payload's bytes. A file written at plan
//             time cannot be an edge to a build-time product, so the object was
//             assembled once and every later payload change was a green build
//             over stale bytes -- reproduced against `mcpp:plugins` 0.3.0 in a
//             sandbox, from published packages.
//
//             So under this storage the generation IS an action, its command is
//             `mcpp-embed`, and the payloads are its declared inputs. The edge
//             is then an ordinary one, expressed with the graph primitive the
//             engine already has.
//
// TWO ACTIONS, NOT ONE, AND THAT IS NOT A LIMITATION BUT A FACT ABOUT INPUTS.
// The interface is a function of the item list alone; the body additionally of
// the payloads. One action would rewrite the interface whenever a payload
// changed and rebuild every BMI importing it. (`mcpp::action::provides` is also
// per-action rather than per-output, so a single action declaring three outputs
// and one module makes the scanner report "already provided by" -- measured.)
struct declared {
    surface::emitted files;
    bool             ok = false;
};

// Writes the manifest `mcpp-embed` reads. Plan-time knowledge only: every value
// here is something the rule computed from the glob and the options.
inline bool write_manifest(const std::string& path,
                           std::span<const surface::item> items,
                           const surface::options& opt,
                           std::string_view which)
{
    auto surface_name = opt.surface == surface::kind::c_header ? "c_header" : "module";
    auto storage_name = opt.store == surface::storage::object  ? "object"
                      : opt.store == surface::storage::sidecar ? "sidecar" : "header";
    auto element_name = opt.elem == surface::element::byte_ ? "byte" : "word32";
    std::string m;
    m += std::format("# written by mcpp.plugins.declare; read by mcpp-embed\n"
                     "surface {}\nstorage {}\nelement {}\nmodule {}\noutdir {}\n"
                     "by {}\nos {}\ngas {}\nemit {}\n",
                     surface_name, storage_name, element_name, opt.module_name,
                     opt.out_dir, opt.produced_by, opt.target_os,
                     opt.has_gas_assembler ? 1 : 0, which);
    for (auto const& it : items) {
        m += std::format("item {}\n", it.identifier);
        for (auto const& n : it.name_space)     m += std::format("ns {}\n", n);
        if (!it.data_header.empty())            m += std::format("header {}\n", it.data_header);
        if (!it.data_symbol.empty())            m += std::format("symbol {}\n", it.data_symbol);
        if (!it.payload_path.empty())           m += std::format("payload {}\n", it.payload_path);
        if (!it.sidecar_name.empty())           m += std::format("sidecar {}\n", it.sidecar_name);
        if (!it.data_size_expr.empty())         m += std::format("size {}\n", it.data_size_expr);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << std::format("mcpp.plugins.declare: cannot write {}\n", path);
        return false;
    }
    out << m;
    out.close();
    // Checked on close as well as on open: a full filesystem fails here and
    // nowhere else, and a silently short manifest would produce a surface
    // missing its last payloads.
    if (!out) {
        std::cerr << std::format("mcpp.plugins.declare: failed while writing {}\n", path);
        return false;
    }
    return true;
}

// Generate the surface and tell mcpp about it, by whichever route the storage
// requires. Returns the file names so the caller can put a directory on the
// include path or state the module it provides.
inline declared surface_for(std::span<const surface::item> items,
                            const surface::options& opt)
{
    declared d;
    d.files = surface::outputs(opt);
    if (items.empty()) { d.ok = true; return d; }

    if (d.files.store != surface::storage::object) {
        // Plan time is correct here: nothing this writes is an edge to a
        // build-time product.
        if (!surface::write(items, opt, surface::half::interface_)) return d;
        if (!surface::write(items, opt, surface::half::body)) return d;
        mcpp::generated(d.files.interface_file.c_str());
        mcpp::generated(d.files.impl_file.c_str());
        if (!d.files.include_dir.empty()) mcpp::include_dir(d.files.include_dir.c_str());
        d.ok = true;
        return d;
    }

    // ── object storage: the generation is an action ─────────────────────────
    const std::string tool = mcpp::dep_bin("plugins", "mcpp-embed");
    if (tool.empty()) {
        // NAMED, NOT GUESSED. The tool is default-off, so a project that asks
        // for this storage without asking for the tool gets the one message
        // that says what to add and where.
        std::cerr << std::format(
            "{}: storage::object needs the `mcpp-embed` tool, and this build did not "
            "ask for it.\n"
            "  The generated assembly names each payload in `.incbin`, so it has to be\n"
            "  produced by an action whose inputs are those payloads -- and an action's\n"
            "  command is a program. Add it to the dependency that already brings the\n"
            "  rules in:\n\n"
            "      [build-dependencies.mcpp]\n"
            "      plugins = {{ version = \"...\", features = [...], host-module = true,\n"
            "                  tools = [\"mcpp-embed\"] }}\n\n"
            "  Or use storage::header, which needs no program.\n",
            opt.produced_by.empty() ? "mcpp.plugins.declare" : opt.produced_by);
        return d;
    }

    const std::string base = opt.out_dir + "/" + opt.module_name;
    const std::string mi   = base + ".surface-interface.txt";
    const std::string mb   = base + ".surface-body.txt";
    if (!write_manifest(mi, items, opt, "interface")) return d;
    if (!write_manifest(mb, items, opt, "body")) return d;

    // The interface: the item list decides it, so the manifest is its only
    // input. `provides` is what lets a generated `.cppm` be a graph node
    // without being scanned.
    // THE ID CARRIES THE PRODUCER, NOT ONLY THE MODULE.
    //
    // `mcpp.rules.spirv` and `mcpp.rules.slang` both default their module to
    // `<package>.shaders`, so a project driving both would submit two actions
    // under one id if the id were the module alone. That collision is not
    // introduced here -- two rules defaulting to one module name is a question
    // of its own -- but an id that cannot collide costs nothing.
    const std::string who = opt.produced_by.empty() ? std::string("mcpp.plugins")
                                                    : opt.produced_by;
    mcpp::action iface;
    const std::string ifaceId = who + ":" + opt.module_name + ":surface-interface";
    iface.id          = ifaceId.c_str();
    iface.role        = "source";
    iface.description = "mcpp-embed interface";
    iface.arg(tool.c_str()).arg(mi.c_str())
         .input(mi.c_str())
         .output(d.files.interface_file.c_str());
    if (opt.surface == surface::kind::module_) iface.provides(opt.module_name.c_str());
    iface.submit();

    // The body: the item list AND the payloads. This is the edge the whole
    // round is about -- a payload named here is a payload the graph knows the
    // object depends on.
    mcpp::action body;
    const std::string bodyId = who + ":" + opt.module_name + ":surface-body";
    body.id          = bodyId.c_str();
    body.role        = "source";
    body.description = "mcpp-embed body";
    body.arg(tool.c_str()).arg(mb.c_str())
        .input(mb.c_str())
        .output(d.files.impl_file.c_str())
        .output(d.files.assembly_file.c_str());
    for (auto const& it : items)
        if (!it.payload_path.empty()) body.input(it.payload_path.c_str());
    body.submit();

    if (!d.files.include_dir.empty()) mcpp::include_dir(d.files.include_dir.c_str());
    d.ok = true;
    return d;
}

} // namespace mcpp::plugins
