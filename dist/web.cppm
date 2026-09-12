// mcpp.dist.web -- the wasm32-emscripten stem family becomes a static
// directory a browser can load, with an `index.html` this member writes.
//
// WHY THIS IS NEITHER A RULE NOR A TOOL, AND WHY IT COMPILES NO TRANSLATION
// UNIT. Same shape `dist/appimage.cppm` and `dist/apple.cppm` already state:
// this member consumes what the LINK and `mcpp pack`'s STAGING already
// produced, and turns it into something a browser installs -- a directory
// served over HTTP rather than a file a package manager unpacks, but the
// same third category.
//
// WHY THE STAGED TREE ALREADY HOLDS EVERYTHING THIS MEMBER NEEDS, AND WHY
// THAT MAKES THIS MEMBER SMALL. #622 A5 gave `wasm32-emscripten` a fixed
// naming (`bin/<name>.js`, with `bin/<name>.wasm` an implicit link output)
// and taught `mcpp pack` to stage "every file the link wrote with this
// stem" beside it (`.data` when `--preload-file` is present, `.worker.js`,
// `.wasm.map`); #622 A4 taught `mcpp::deploy` to land its files at the same
// executable-relative path, which for this row is also under `bin/`. So
// `${mcpp.stage_dir}/bin/` already contains exactly the file set the A4
// table's web row names ("the same relative path in the static directory,
// served beside `<name>.js`") -- this member's own job is to copy that one
// directory to `<out_dir>/web/`, dropping the `bin/` prefix a browser has
// no use for, and to write the one file neither the link nor `mcpp::deploy`
// produces: a page that loads the script.
//
// REFUSED BY NAME ON EVERY OTHER TARGET, deliberately, and not merely
// because no other row happens to produce a `.js` launcher: `--format web`
// on an ELF or Mach-O row would otherwise silently ship a native binary
// inside a directory named for a browser, which is a worse failure than a
// refusal naming the one target this format serves.
//
// `cp` PER FILE, ARGV ONLY, NO SHELL -- AND THAT IS WHY THIS MEMBER IS
// POSIX-HOST ONLY FOR NOW. `dist/appimage.cppm`'s own copies are single
// files handed straight to `appimagetool`'s own argument list; `dist/
// apple.cppm` copies whole directories with `ditto`, which exists only on
// macOS. Neither precedent is a portable multi-file copier this member
// could reuse on Windows, so it declares one `cp SRC DST` action per file in
// the discovered set instead of shelling out to a recursive copy -- exactly
// the shape `mcpp::action` is built for (a graph edge per file, skippable on
// a cache hit), and the one the design record's implementation notes accept
// ("one cp/copy per file is acceptable"). The README states the POSIX-host
// limitation; lifting it needs either a `copy`-argv branch on the host OS or
// a small copier this member carries itself, and neither is written here
// because nothing in this collection has needed one yet.
//
// WHY `index.html` IS WRITTEN AT PLAN TIME TO A SIDE FILE AND COPIED, RATHER
// THAN WRITTEN DIRECTLY TO ITS FINAL PATH. `dist/apple.cppm`'s own header
// gives the reason and it applies unchanged: a stray write to a path no
// action declares is invisible to the graph, so a template or title change
// would leave the previous page in place, reported as up to date. Declaring
// the side file as the copy action's input is what makes an edit reach
// `<out_dir>/web/index.html`.

module;
#include <cstdio>

export module mcpp.dist.web;

import std;
import mcpp;
import mcpp.plugins;

export namespace mcpp::dist::web {

// ─── Options ───────────────────────────────────────────────────────────────

struct options {
    // The program target this directory wraps. Empty means the package
    // name, which is the target `mcpp pack` itself selects by convention --
    // the same field every other `dist-*` member's `options` starts with.
    std::string target;

    // A project file, package-root-relative, rendered with `{{name}}` (the
    // target's name, the `.js` launcher's stem) and `{{title}}` (the page
    // `<title>`, see below) substituted verbatim wherever they appear.
    // Empty uses the built-in default, which loads the script with a plain
    // `<script src="<name>.js">` -- correct for a `bin` whose `main` runs on
    // load. A project whose page instead calls an exported factory (a
    // `-sMODULARIZE=1` build, #622's own manifest example) supplies its own
    // template, because which shape the page needs is the program's
    // contract, not this member's guess.
    std::string template_file;

    // `{{title}}` in the template. Empty means `[package] name`.
    std::string title;

    // Where the produced directory lands. Empty means `<out_dir>/web`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ──────────────────────────────────────────────────────────────

struct step {
    const char*               id;
    const char*               role;
    const char*               description;
    std::vector<std::string>  argv;
    std::vector<std::string>  inputs;
    std::vector<std::string>  outputs;
};

struct plan {
    // False when this build is not `mcpp pack --format web`, which is every
    // ordinary build. `reason` then says which of the several ways.
    bool              applies = false;
    std::string       reason;
    std::string       web_dir;   // <out_dir>/web
    std::string       stage_bin; // ${mcpp.stage_dir}/bin, kept for the floor check
    std::vector<step> steps;
    explicit operator bool() const { return applies; }
};

// ─── Internals ─────────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Written only when the bytes differ, for the reason every other member in
// this collection gives: rewriting identical bytes moves the mtime, and a
// moved mtime on a declared input is indistinguishable from a changed one --
// so a second pack of an unchanged project would re-copy `index.html`.
inline bool write_if_different(const std::filesystem::path& path,
                               std::string_view bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (std::ifstream in(path, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        if (old == bytes) return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

inline std::string target_for(const options& opt) {
    if (!opt.target.empty()) return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string();
}

inline std::string replace_all_copy(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// No bundler, no hashing, and no shape a project cannot already read: a
// `<script>` tag and nothing else. A modularised program supplies its own
// template rather than this member growing a second default.
inline std::string default_template(const std::string& name, const std::string& title) {
    return std::format(
        "<!doctype html>\n"
        "<meta charset=\"utf-8\">\n"
        "<title>{0}</title>\n"
        "<script src=\"{1}.js\"></script>\n",
        title, name);
}

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    // NOT THIS PASS. Every ordinary build lands here, and the empty
    // `pack_format()` is what says so -- see `generate` for why the
    // DECLARATION must not be gated the same way.
    const std::string requested = mcpp::pack_format();
    if (requested != "web") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not web", requested);
        return p;
    }

    // wasm32-emscripten only, and this is a refusal rather than a silent
    // skip: a user who typed `--format web` on a native row asked for
    // something that does not exist there, and the engine has already
    // accepted the value because the graph declared it.
    if (const std::string os = mcpp::target_os(); os != "emscripten") {
        std::cerr << std::format(
            "mcpp.dist.web: a web directory wraps a wasm32-emscripten "
            "program, and this build targets '{}'.\n"
            "  use: --format tar, or build for --target wasm32-emscripten",
            os.empty() ? "unknown" : os) << '\n';
        p.reason = "not the emscripten target";
        return p;
    }

    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty()) {
        std::cerr << "mcpp.dist.web: mcpp reported no staged tree. This "
                     "member needs mcpp 2026.9.12.2 or newer.\n";
        p.reason = "no staged tree";
        return p;
    }

    const std::string target = target_for(opt);
    if (target.empty()) {
        std::cerr << "mcpp.dist.web: no target to wrap. Set "
                     "`options::target` to the program target's name.\n";
        p.reason = "no target";
        return p;
    }

    // THE LAUNCHER'S NAME IS NOT DISCOVERED -- IT IS THE ONE #622 A5 FIXED.
    // Every `wasm32-emscripten` `bin`/`app` names its launcher
    // `bin/<name>.js`, on every host, by the naming this member's own
    // engine floor guarantees; there is no second convention to search the
    // way `dist/appimage.cppm`'s `launcher_in` has to.
    const std::string stageBin  = stage + "/bin";
    const std::string launcher  = stageBin + "/" + target + ".js";
    if (!is_file(launcher)) {
        std::cerr << std::format(
            "mcpp.dist.web: the staged tree at {0} carries no launcher for "
            "target '{1}'.\n"
            "  expected: {0}/{1}.js",
            stageBin, target) << '\n';
        p.reason = "no launcher in the staged tree";
        return p;
    }

    if (!opt.template_file.empty()) {
        const std::string tpl =
            (std::filesystem::path(mcpp::manifest_dir()) / opt.template_file).string();
        if (!is_file(tpl)) {
            std::cerr << std::format(
                "mcpp.dist.web: the template {} was not found", tpl) << '\n';
            p.reason = "template not found";
            return p;
        }
    }

    const char* nm = mcpp::package_name();
    const std::string title = !opt.title.empty() ? opt.title
                             : (nm && *nm ? std::string(nm) : target);

    std::string templateBytes;
    if (!opt.template_file.empty()) {
        std::ifstream in((std::filesystem::path(mcpp::manifest_dir()) / opt.template_file),
                          std::ios::binary);
        templateBytes.assign((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    } else {
        templateBytes = default_template(target, title);
    }
    templateBytes = replace_all_copy(std::move(templateBytes), "{{name}}", target);
    templateBytes = replace_all_copy(std::move(templateBytes), "{{title}}", title);

    const std::string webDir = !opt.output.empty() ? opt.output
                              : (std::filesystem::path(opt.out_dir) / "web").string();

    const std::string indexSrc =
        (std::filesystem::path(opt.out_dir) / (target + "-index.html")).string();
    if (!write_if_different(indexSrc, templateBytes)) {
        std::cerr << std::format("mcpp.dist.web: cannot write {}", indexSrc) << '\n';
        p.reason = "cannot write index.html";
        return p;
    }

    p.web_dir   = webDir;
    p.stage_bin = stageBin;

    // ── The stem family and every deploy'd file, discovered where they
    // already live: `${mcpp.stage_dir}/bin/`. #622 A5's implicit `.wasm`
    // output, an optional `.data`, and #622 A4's deploy'd files (staged
    // executable-relative, which on this row IS `bin/`-relative) all land
    // here through the engine's OWN staging, so this member reads the
    // directory rather than re-deriving the stem family or a deploy list a
    // build program has no accessor for. Sorted for a deterministic plan.
    std::vector<std::string> relFiles;
    {
        std::error_code ec;
        for (auto const& e : std::filesystem::recursive_directory_iterator(stageBin, ec)) {
            if (ec) break;
            if (!e.is_regular_file(ec)) continue;
            relFiles.push_back(
                std::filesystem::relative(e.path(), stageBin).generic_string());
        }
        std::ranges::sort(relFiles);
    }
    if (relFiles.empty()) {
        // Cannot happen: `launcher` above already proved `<target>.js`
        // exists under `stageBin`. Kept as a named refusal rather than an
        // assertion, because a member's job on a broken invariant is to say
        // so, not to crash the build program that is packaging someone
        // else's project.
        std::cerr << std::format(
            "mcpp.dist.web: {} carries no files to stage", stageBin) << '\n';
        p.reason = "staged bin/ is empty";
        return p;
    }

    for (auto const& rel : relFiles) {
        const std::string dst = webDir + "/" + rel;
        // Directories are created here, at plan time -- cheap (a handful of
        // path segments, never hundreds of megabytes) and the same trade
        // `write_if_different` already makes for `index.html`'s own parent.
        // The CONTENT copy is the action; an empty directory existing a
        // build early is not a cache-visible effect.
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(dst).parent_path(), ec);

        step s;
        s.id          = "mcpp.dist.web.file";
        s.role        = "artifact";
        s.description = "WEB FILE";
        // `${mcpp.stage_dir}` in the argv, not the absolute path this
        // program just read `stageBin` from -- the same reasoning every
        // other member gives: the path in the graph and the path here
        // cannot disagree, and naming it earns this action the engine's
        // automatic dependency on the staged tree's manifest.
        const std::string src = "${mcpp.stage_dir}/bin/" + rel;
        s.argv    = { "cp", src, dst };
        s.inputs  = { src };
        s.outputs = { dst };
        p.steps.push_back(std::move(s));
    }

    step page;
    page.id          = "mcpp.dist.web.index";
    page.role        = "artifact";
    page.description = "INDEX.HTML";
    page.argv    = { "cp", indexSrc, webDir + "/index.html" };
    page.inputs  = { indexSrc };
    page.outputs = { webDir + "/index.html" };
    p.steps.push_back(std::move(page));

    p.applies = true;
    return p;
}

// ─── Submit ────────────────────────────────────────────────────────────────

inline bool submit(const plan& p) {
    if (!p.applies) return true;
    for (auto const& s : p.steps) {
        mcpp::action a;
        a.id          = s.id;
        a.role        = s.role;
        a.description = s.description;
        for (auto const& tok : s.argv)    a.arg(tok.c_str());
        for (auto const& in  : s.inputs)  a.input(in.c_str());
        for (auto const& out : s.outputs) a.output(out.c_str());
        a.submit();
    }

    // A FLOOR ON THIS MEMBER'S OWN OUTPUT, ON THE SUCCESS PATH -- the same
    // shape `dist/appimage.cppm` and `dist/apple.cppm` both give: the copies
    // have not run yet when this program exits, only been declared, so what
    // is measured is the staged tree the copy steps are about to read.
    std::error_code ec;
    std::uintmax_t bytes = 0;
    for (auto const& e : std::filesystem::recursive_directory_iterator(p.stage_bin, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec)) bytes += std::filesystem::file_size(e.path(), ec);
    }
    // Loose on purpose, matching this collection's own bound: this exists to
    // catch "nothing was linked or staged", not to police a size budget. A
    // wasm launcher plus its module is comfortably larger than this on any
    // program that links `import std`.
    if (bytes < 4u * 1024u) {
        static char msg[512];
        std::snprintf(msg, sizeof msg,
            "mcpp.dist.web: %s holds only %llu bytes, which is not a "
            "program; the page will load nothing useful",
            p.stage_bin.c_str(), static_cast<unsigned long long>(bytes));
        mcpp::warning(msg);
    }
    return true;
}

// ─── The one call a consumer makes ─────────────────────────────────────────

// DECLARE UNCONDITIONALLY, SUBMIT CONDITIONALLY -- and both halves are here
// so a consumer cannot do one without the other. See `dist/appimage.cppm`'s
// own comment on this function for why the declaration cannot be gated the
// same way `plan_for` gates its submission.
inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("web");
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::web
