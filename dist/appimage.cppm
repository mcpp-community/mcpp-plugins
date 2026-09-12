// mcpp.dist.appimage -- a linked program and its closure become one AppImage.
//
// WHY THIS IS NEITHER A RULE NOR A TOOL. A rule states how a translation unit
// is compiled by a compiler mcpp does not drive. A tool states something the
// build program needs that no compiler performs, and does it while the program
// runs. This member does neither: it consumes LINK OUTPUTS and produces
// something a user installs. That is a third category, and the `dist-` prefix
// says which of the three questions a member answers -- a consumer reading
// `rules-appimage` would expect a compiler and a translation unit, and there is
// neither.
//
// THE ENGINE HOLDS THE DISPATCH AND NOT THE FORMAT. `mcpp pack --format
// appimage` finds the package that declared the name and hands it the staged
// tree; nothing about squashfs, the type-2 runtime or the desktop-entry
// specification is in mcpp. Binding any of it there would couple an mcpp
// release to a release mcpp does not control -- the same argument the project
// already made for languages, where Slang is supported without being named in
// the engine.
//
// THE STAGED TREE IS ALREADY AN AppDir, BAR THREE FILES, and that is the whole
// reason this member is small. `mcpp pack --mode vendored` stages `bin/`,
// `lib/` and a top-level launcher whose `$ORIGIN` rewriting makes the tree
// relocatable -- which is what an AppImage is. AppImage additionally requires
// an `AppRun`, a top-level `.desktop` entry and an icon; the payload's own
// layout inside the directory is free, because `AppRun` is the entry point.
// So this member writes three small files and invokes one tool, and never
// copies or re-lays-out a tree that can be hundreds of megabytes.
//
// WHY THE THREE FILES ARE WRITTEN AT PLAN TIME AND DECLARED AS ACTION INPUTS.
// Writing them is configuration, not construction, and the second pass of
// `mcpp pack --format appimage` runs the build program after the tree has been
// staged, so the directory exists. Declaring them as inputs is what makes a
// change to `[package] description` reach the graph: the build program re-runs
// on any change to the package metadata it was told, rewrites the entry, and
// the edge is dirty because a declared input changed. Without the declaration
// the edge depends only on the program binary, and a metadata-only change would
// leave the previous AppImage in place, reported as up to date.

module;
#include <cstdio>

export module mcpp.dist.appimage;

import std;
import mcpp;
import mcpp.plugins;

// Nothing here uses `std::println`, and that is not a style choice: both of its
// overloads reach into the libc++ dylib for symbols macOS 14 does not ship, so
// a member that printed with it compiled and then failed to link. `std::format`
// is header-only. The full measurement is in `rules/spirv.cppm`.

export namespace mcpp::dist::appimage {

// ─── Options ───────────────────────────────────────────────────────────────

struct options {
    // The program target this AppImage wraps. Empty means the package name,
    // which is the target `mcpp pack` itself selects by convention.
    //
    // NAMED, NEVER DISCOVERED. `${mcpp.target_file:<name>}` refuses an unknown
    // target rather than expanding to an empty path, and a path that resolves
    // to nothing is the failure this category is most exposed to: a WiX action
    // that bound a directory and harvested it produced a valid, empty, 52 KB
    // installer with no diagnostic when the bind path resolved to nothing.
    std::string target;

    // What the desktop entry says. Each empty field is taken from `[package]`,
    // which mcpp reports to the build program from 2026.9.11.1: a project that
    // states its version once does not state it again here, and a second copy
    // is one that drifts with nothing able to detect it.
    std::string app_name;         // `Name=`;    default: the target
    std::string comment;          // `Comment=`; default: `[package] description`
    std::string version;          // `X-AppImage-Version=`; default: `[package] version`
    // Freedesktop main categories, semicolon-terminated by the writer. The
    // specification requires at least one, and `Utility` is the honest answer
    // for a program whose category nothing has stated.
    std::vector<std::string> categories;
    // `Terminal=true` for a program that expects a terminal. A wrong answer
    // here is visible only to a desktop launcher, never to a build.
    bool terminal = true;

    // A PNG a project supplies. Empty uses the built-in placeholder, which
    // exists so that an AppImage can be produced with nothing declared:
    // appimagetool requires an icon and refuses without one, and a member whose
    // first use needs a graphic asset is a member nobody tries.
    std::string icon;

    // An explicit `appimagetool` wins over discovery. Set it to pin a build
    // other than the one the workspace installed.
    std::string tool;
    // The type-2 runtime stub the produced AppImage starts with.
    //
    // IT IS DECLARED BECAUSE THE TOOL WOULD OTHERWISE FETCH IT. Measured on
    // appimagetool 1.9.1: with no `--runtime-file`, every invocation downloads
    // the stub from a GitHub release, and with that address unreachable the
    // build fails inside the tool. A build must not reach the network -- one
    // that does is neither reproducible nor usable offline -- so the stub comes
    // from the declared payload and is passed explicitly.
    std::string runtime;

    // Where the produced file lands. Empty means
    // `<out_dir>/<target>-<arch>.AppImage`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ──────────────────────────────────────────────────────────────

// What this member would submit, without submitting it.
//
// A PLAN AND A SUBMIT, BECAUSE A DISTRIBUTABLE IS THE LAST THING BEFORE A
// USER'S HANDS. It is therefore the part of a build most likely to need a
// project-specific edit -- one extra file, a different compression level, a
// second signature -- and `generate()` being exactly `submit(plan_for())` is
// what keeps such an edit from becoming a reimplementation of this member.
struct plan {
    // False when this build is not `mcpp pack --format appimage`, which is
    // every ordinary build. `reason` then says which of the several ways.
    bool                     applies = false;
    std::string              reason;
    std::string              output;
    std::string              appdir;
    std::vector<std::string> argv;
    std::vector<std::string> inputs;
    explicit operator bool() const { return applies; }
};

// ─── Internals ─────────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// The 70 bytes of a 1x1 opaque PNG.
//
// A CONSTANT RATHER THAN A GENERATED FILE, because generating any PNG needs a
// deflate implementation and a member of this collection does not earn one to
// draw a placeholder. Verified as `PNG image data, 1 x 1, 8-bit/color RGBA,
// non-interlaced`.
inline constexpr unsigned char kPlaceholderIcon[]{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xfc, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0xab, 0xce, 0x36, 0x89, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// Written only when the bytes differ, for the reason `mcpp.tools.embed` gives:
// rewriting identical bytes moves the mtime, and a moved mtime on a declared
// input is indistinguishable from a changed input -- so a second pack of an
// unchanged project would rebuild the AppImage.
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

// `Name=` and the file stem. Not sanitised beyond what a desktop entry cannot
// carry: the value a user sees is the project's to choose.
inline std::string app_name_for(const options& opt) {
    if (!opt.app_name.empty()) return opt.app_name;
    if (!opt.target.empty())   return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string("app");
}

inline std::string target_for(const options& opt) {
    if (!opt.target.empty()) return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string();
}

// The staged tree's top-level launcher, which is what `AppRun` execs.
//
// `mcpp pack` writes one per mode and names it after the binary; under
// `--mode static` it is the program itself. Both are answered by looking, so
// this member states no convention mcpp has not already put on disk.
inline std::string launcher_in(const std::string& stage, const std::string& target) {
    for (auto candidate : {stage + "/" + target,
                           stage + "/bin/" + target,
                           stage + "/run.sh"})
        if (is_file(candidate)) return candidate;
    return {};
}

// appimagetool's own architecture vocabulary, from the target mcpp resolved.
// It is passed as `ARCH` in the tool's environment and it also names the
// runtime stub, so a wrong answer produces an AppImage that cannot start on
// the machine it was built for.
inline std::string arch_for() {
    const std::string a = mcpp::target_arch();
    return a.empty() ? std::string("x86_64") : a;
}

// Where the payload lives. Both are legitimate and only one exists.
inline std::string payload_dir_of(const std::string& tool) {
    return std::filesystem::path(tool).parent_path().string();
}

inline std::string discover_tool(const options& opt) {
    if (!opt.tool.empty()) return opt.tool;
    // THE PAYLOAD THIS MEMBER DECLARED, and nothing else.
    //
    // `xpkg_dir` answers from `MCPP_XPKG_*_DIR`, which mcpp sets for the
    // package being built -- so a member that runs a payload tool declares the
    // payload itself rather than relying on a consumer's manifest. There is
    // deliberately no PATH fallback: a host `appimagetool` would make the
    // produced AppImage depend on a machine rather than on a declaration, and
    // an AppImage is the artifact for which that matters most.
    const std::string dir = mcpp::xpkg_dir("xim", "appimagetool");
    if (dir.empty()) return {};
    const std::string exe = (std::filesystem::path(dir) / "appimagetool").string();
    return is_file(exe) ? exe : std::string();
}

inline std::string discover_runtime(const options& opt, const std::string& tool) {
    if (!opt.runtime.empty()) return opt.runtime;
    if (tool.empty()) return {};
    const auto dir = payload_dir_of(tool);
    for (auto name : {"runtime-" + arch_for(), std::string("runtime")}) {
        const auto p = (std::filesystem::path(dir) / name).string();
        if (is_file(p)) return p;
    }
    return {};
}

inline std::string desktop_entry(const options& opt, const std::string& name,
                                 const std::string& icon_stem) {
    std::string categories;
    if (opt.categories.empty()) categories = "Utility;";
    else for (auto const& c : opt.categories) categories += c + ";";

    const char* d = mcpp::package_description();
    const char* v = mcpp::package_version();
    const std::string comment = !opt.comment.empty() ? opt.comment
                              : (d && *d ? std::string(d) : std::string());
    const std::string version = !opt.version.empty() ? opt.version
                              : (v && *v ? std::string(v) : std::string());

    std::string text = "[Desktop Entry]\nType=Application\n";
    text += std::format("Name={}\n", name);
    if (!comment.empty()) text += std::format("Comment={}\n", comment);
    text += std::format("Exec={}\n", name);
    text += std::format("Icon={}\n", icon_stem);
    text += std::format("Categories={}\n", categories);
    text += std::format("Terminal={}\n", opt.terminal ? "true" : "false");
    if (!version.empty()) text += std::format("X-AppImage-Version={}\n", version);
    return text;
}

// `AppRun` hands control to the tree's own launcher, with `APPDIR` exported
// because that launcher may be the `$ORIGIN`-relative one `mcpp pack` wrote
// and a caller may have set neither.
//
// `exec` rather than a call, so the AppImage's process IS the program: a
// wrapper that waits would break signal delivery and a shell's job control,
// and `"$@"` unquoted would split an argument containing a space.
inline std::string apprun(const std::string& launcher_rel) {
    return std::format(
        "#!/bin/sh\n"
        "# Generated by mcpp.dist.appimage. Do not edit.\n"
        "APPDIR=\"$(dirname \"$(readlink -f \"$0\")\")\"\n"
        "export APPDIR\n"
        "exec \"$APPDIR/{}\" \"$@\"\n", launcher_rel);
}

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    // NOT THIS PASS. Every ordinary build lands here, and the empty
    // `pack_format()` is what says so -- see `generate` for why the DECLARATION
    // must not be gated the same way.
    const std::string requested = mcpp::pack_format();
    if (requested != "appimage") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not appimage", requested);
        return p;
    }

    // Linux only, and this is a refusal rather than a silent skip: a user who
    // typed `--format appimage` on a Mac asked for something that does not
    // exist there, and the engine has already accepted the value because the
    // graph declared it.
    if (const std::string os = mcpp::target_os(); os != "linux") {
        std::cerr << std::format(
            "mcpp.dist.appimage: an AppImage is a Linux format, and this build "
            "targets '{}'.\n"
            "  use: --format tar, or build for a Linux target",
            os.empty() ? "unknown" : os) << '\n';
        p.reason = "not a Linux target";
        return p;
    }

    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty()) {
        std::cerr << "mcpp.dist.appimage: mcpp reported no staged tree. This "
                     "member needs mcpp 2026.9.11.1 or newer.\n";
        p.reason = "no staged tree";
        return p;
    }

    const std::string target = target_for(opt);
    if (target.empty()) {
        std::cerr << "mcpp.dist.appimage: no target to wrap. Set "
                     "`options::target` to the program target's name.\n";
        p.reason = "no target";
        return p;
    }

    const std::string tool = discover_tool(opt);
    if (tool.empty()) {
        // NAMES WHERE IT LOOKED, not a manifest the reader does not own. The
        // payload is declared by this package's own feature, so a consumer who
        // sees this has an installation problem rather than a declaration to
        // add.
        std::cerr << std::format(
            "mcpp.dist.appimage: appimagetool was not found.\n"
            "  looked for: {}/appimagetool  (the `xim:appimagetool` payload "
            "this feature declares)\n"
            "  set `options::tool` to name one explicitly.",
            mcpp::xpkg_dir("xim", "appimagetool")) << '\n';
        p.reason = "appimagetool not found";
        return p;
    }
    const std::string runtime = discover_runtime(opt, tool);
    if (runtime.empty()) {
        std::cerr << std::format(
            "mcpp.dist.appimage: the type-2 runtime stub was not found beside "
            "{}.\n"
            "  Without it appimagetool downloads the stub on every invocation, "
            "which a build must not do.\n"
            "  set `options::runtime`, or install a `xim:appimagetool` that "
            "carries `runtime-{}`.",
            payload_dir_of(tool), arch_for()) << '\n';
        p.reason = "runtime stub not found";
        return p;
    }

    const std::string launcher = launcher_in(stage, target);
    if (launcher.empty()) {
        std::cerr << std::format(
            "mcpp.dist.appimage: the staged tree at {0} carries no launcher for "
            "target '{1}'.\n"
            "  expected one of: {0}/{1}, {0}/bin/{1}, {0}/run.sh",
            stage, target) << '\n';
        p.reason = "no launcher in the staged tree";
        return p;
    }
    // Strings, not `lexically_relative`: see `mcpp::plugins::names::relative_to`.
    const auto launcher_rel = mcpp::plugins::names::relative_to(launcher, stage);

    // ── The three files AppImage requires, written into the staged tree ────
    const std::string name = app_name_for(opt);
    const auto stagePath   = std::filesystem::path(stage);
    const auto desktop     = stagePath / (name + ".desktop");
    const auto icon        = stagePath / (name + ".png");
    const auto dirIcon     = stagePath / ".DirIcon";
    const auto runFile     = stagePath / "AppRun";

    std::string iconBytes;
    if (!opt.icon.empty()) {
        std::ifstream in(opt.icon, std::ios::binary);
        if (!in) {
            std::cerr << std::format(
                "mcpp.dist.appimage: cannot read the icon {}", opt.icon) << '\n';
            p.reason = "icon unreadable";
            return p;
        }
        iconBytes.assign((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    } else {
        iconBytes.assign(reinterpret_cast<const char*>(kPlaceholderIcon),
                         sizeof kPlaceholderIcon);
    }

    const bool wrote =
        write_if_different(runFile, apprun(launcher_rel))
        && write_if_different(desktop, desktop_entry(opt, name, name))
        && write_if_different(icon, iconBytes)
        // `.DirIcon` is what the AppImage's own thumbnailer reads. A copy
        // rather than a symlink: mksquashfs stores a symlink as a symlink, and
        // one pointing at a sibling inside the image is resolved by no reader
        // that has not mounted it.
        && write_if_different(dirIcon, iconBytes);
    if (!wrote) {
        std::cerr << std::format(
            "mcpp.dist.appimage: cannot write the AppDir metadata into {}",
            stage) << '\n';
        p.reason = "cannot write AppDir metadata";
        return p;
    }
    std::error_code ec;
    std::filesystem::permissions(runFile,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, ec);

    p.output = !opt.output.empty() ? opt.output
             : (std::filesystem::path(opt.out_dir)
                / std::format("{}-{}.AppImage", name, arch_for())).string();
    p.appdir = stage;

    p.argv = {
        tool,
        // Without this the tool self-mounts through libfuse, which a container
        // and most CI runners do not have. Measured with `fusermount` off the
        // PATH: `No suitable fusermount binary found on the $PATH` and exit
        // 127; with this flag the same invocation succeeds.
        "--appimage-extract-and-run",
        "--runtime-file", runtime,
        // The staged tree, named through the placeholder rather than the
        // absolute path this program read, so the path in the graph and the
        // path here cannot disagree.
        "${mcpp.stage_dir}",
        p.output,
    };
    p.inputs = {
        std::format("${{mcpp.target_file:{}}}", target),
        runFile.string(),
        desktop.string(),
        icon.string(),
    };
    p.applies = true;
    return p;
}

// ─── Submit ────────────────────────────────────────────────────────────────

inline bool submit(const plan& p) {
    if (!p.applies) return true;
    mcpp::action a;
    a.id          = "mcpp.dist.appimage";
    a.role        = "artifact";
    a.description = "APPIMAGE";
    for (auto const& tok : p.argv) a.arg(tok.c_str());
    for (auto const& in  : p.inputs) a.input(in.c_str());
    a.output(p.output.c_str());
    a.submit();

    // A FLOOR ON THIS MEMBER'S OWN OUTPUT, ON THE SUCCESS PATH.
    //
    // A packaging step that succeeds while carrying nothing is the failure this
    // whole category is most exposed to, and stderr on a successful build is
    // discarded -- so the only channel that survives is `mcpp::warning`, which
    // is also replayed on a cache hit. The tree is measured here, before the
    // tool runs, because that is where this program is: an AppDir with no
    // executable payload produces a valid AppImage that does nothing.
    // IT COUNTS FILES THE MEMBER DID NOT WRITE, NOT BYTES.
    //
    // A size floor was the first version of this check and it was wrong on its
    // first real fixture: a stripped hello-world stages at 14999 bytes, under
    // the 16 KB bound, so a correct AppImage was reported as carrying no
    // program. A size is a PROXY for the question, and the question is
    // answerable directly -- does the tree hold anything besides the four
    // files this member just put there.
    //
    // The floor belongs on the success path and in `mcpp::warning` because
    // stderr on a successful build is discarded, and because a warning is
    // replayed on a cache hit: an advisory that appeared once and then vanished
    // would read as "resolved".
    std::error_code ec;
    std::size_t carried = 0;
    for (auto const& e : std::filesystem::recursive_directory_iterator(p.appdir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        const auto name = e.path().filename().string();
        if (e.path().parent_path() == std::filesystem::path(p.appdir)
            && (name == "AppRun" || name == ".DirIcon"
                || name.ends_with(".desktop") || name.ends_with(".png")))
            continue;
        ++carried;
    }
    if (carried == 0) {
        static char msg[512];
        std::snprintf(msg, sizeof msg,
            "mcpp.dist.appimage: the staged tree at %s holds nothing but the "
            "AppDir metadata this member wrote; the AppImage will start and do "
            "nothing",
            p.appdir.c_str());
        mcpp::warning(msg);
    }
    return true;
}

// ─── The one call a consumer makes ─────────────────────────────────────────

// DECLARE UNCONDITIONALLY, SUBMIT CONDITIONALLY -- and both halves are here so
// a consumer cannot do one without the other.
//
// The declaration is what lets the engine answer a question the requesting
// build cannot: `mcpp pack --format bogus` names what IS available, and
// `--help` says "any format the resolved graph provides". Both read the set
// collected from a pass that asked for nothing. A member that declared only
// when asked still works for its author -- they always pass their own format --
// and makes the set unknowable for everyone else.
inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("appimage");
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::appimage
