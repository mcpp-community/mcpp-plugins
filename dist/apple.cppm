// mcpp.dist.apple -- a staged tree becomes a `.app` bundle, and is optionally
// signed.
//
// WHY THIS IS NEITHER A RULE NOR A TOOL. A rule states how a translation unit
// is compiled by a compiler mcpp does not drive. A tool states something the
// build program needs that no compiler performs, and does it while the
// program runs. This member does neither: it consumes a STAGED TREE -- itself
// built from link outputs -- and produces something a user installs. That is
// the third category `dist/appimage.cppm` establishes, and the prefix says
// which of the three questions a member answers.
//
// THE ENGINE HOLDS THE DISPATCH AND NOT THE FORMAT. `mcpp pack --format app`
// finds the package that declared the name; nothing about `Info.plist`'s
// keys, the bundle layout, or `codesign`'s flags is in mcpp. Apple's own
// notarisation service is a further release mcpp does not control even less
// than the bundle format is, which is one reason it is out of scope below.
//
// UNLIKE AN AppImage, THIS IS A RE-LAYOUT, AND THAT IS WHY IT COSTS MORE THAN
// THREE FILES. `mcpp pack --mode vendored` stages `bin/`, `lib/` and a
// top-level launcher -- a tree that already satisfies what an AppImage wants,
// because an AppImage has no opinion about its own internal layout beyond
// `AppRun`. A `.app` bundle does have an opinion: it wants the executable and
// its closure under `Contents/MacOS/`, configuration under `Contents/`, and
// resources under `Contents/Resources/`. Reaching that from the staged tree
// is therefore a MOVE, not an ADDITION, and a build program is a bad place to
// perform it: the staged tree can be hundreds of megabytes, and a build
// program is a poor substitute for a shell utility written and optimised for
// exactly this copy. So the assembly is done as the declared COMMAND of one
// or more actions, which is a graph edge ninja schedules and can skip on a
// cache hit, rather than as file I/O this program performs every time it
// runs.
//
// `ditto` IS THE TOOL FOR EVERY COPY BELOW, AND FOR ONE STATED REASON: `cp -R
// SRC DST`'s meaning depends on whether `DST` already exists -- copying
// SRC's CONTENTS into an existing `DST`, but creating a new `DST` as a peer
// of `SRC` when it does not -- which is exactly the kind of environment-
// dependent behaviour that turns "it worked when I tested it" into "it
// nested one directory deeper on a clean machine". `ditto SRC DST` does not
// have that branch: `DST`'s contents become a copy of `SRC`'s contents either
// way, it creates every intermediate directory `DST` needs, and it preserves
// resource forks and permissions, which a plain `cp -R` is not guaranteed to.
// It is part of the base macOS install, not Xcode, so it needs no discovery
// and no `options::tool` the way `wix` does in `dist/wix.cppm` -- there is
// exactly one `ditto`, at a fixed path, on every Mac this can run on.
//
// HOW MANY ACTIONS, AND WHY EACH IS SEPARATE. Up to four:
//
//   1. install `Info.plist`         (always)
//   2. lay out the staged tree      (always)
//   3. install the icon             (only when `options::icon` is set)
//   4. codesign the bundle          (only when `options::identity` is set)
//
// 1 and 3 are separate from 2 because they have different INPUTS: `Info.plist`
// is regenerated whenever package metadata changes, the icon only when the
// project's icon file changes, and the staged tree only when the program or
// its closure changes. One action for all three would make every one of those
// changes re-run the multi-hundred-megabyte copy. 4 is last and depends on
// the OUTPUTS of whichever of 1 to 3 actually ran, because a code signature
// covers the bundle's content at signing time -- signing before the content
// is in place is either a failure (an incomplete bundle) or a signature that
// the next file added invalidates.
//
// `Info.plist` IS WRITTEN AT PLAN TIME, BUT NOT DIRECTLY TO ITS FINAL PATH,
// AND THE DIFFERENCE MATTERS. It is configuration, so `write_if_different`
// is the right way to produce its bytes -- the same reasoning
// `dist/appimage.cppm` gives for its own desktop entry. But this member
// writes it to a location of its own choosing and declares that file as the
// INPUT of the action that copies it into `Contents/Info.plist`, rather than
// writing directly to the bundle path. A stray write to a path no action
// declares is invisible to the graph: nothing would notice `Info.plist`
// changing, and `dist/appimage.cppm`'s own header comment already measured
// what that costs -- "a metadata-only change would leave the previous
// [artifact] in place, reported as up to date." Declaring the plan-time file
// as this action's input is what makes a version bump reach the bundle.
//
// CODESIGN IS OFF BY DEFAULT. An unsigned `.app` builds and runs locally on
// the machine that built it; a member that signed by default would fail
// every build on a machine with no identity in its keychain, which is most
// of them. Notarisation is out of scope entirely, and not merely deferred:
// it requires uploading the bundle to Apple over the network and waiting on
// a ticket, and a build must not reach the network -- the same rule
// `dist/appimage.cppm` states for appimagetool's runtime-stub download,
// here applying to a step this member does not attempt at all rather than
// one it works around.
//
// iOS IS THE SAME SHAPE PLUS A TARGET ROW THE ENGINE DOES NOT YET HAVE, NOT A
// REDESIGN. An iOS app is the same `Contents`-free flat bundle format's
// sibling with its own signing and provisioning-profile rules; what is
// missing is not logic in this file but a target triple and an SDK mcpp does
// not resolve today. This member gains iOS when that row lands. It is not
// implemented here.

module;
#include <cstdio>

export module mcpp.dist.apple;

import std;
import mcpp;
import mcpp.plugins;

// Nothing here uses `std::println`, and that is not a style choice: both of
// its overloads reach into the libc++ dylib for symbols macOS 14 does not
// ship, so a member that printed with it compiled and then failed to link.
// `std::format` is header-only. The full measurement is in `rules/spirv.cppm`.

export namespace mcpp::dist::apple {

// ─── Options ───────────────────────────────────────────────────────────────

struct options {
    // The program target this bundle wraps. Empty means the package name,
    // which is the target `mcpp pack` itself selects by convention.
    std::string target;

    // The bundle's own name -- the `<Name>` in `<Name>.app`, and
    // `CFBundleName`. Empty means the target name, then the package name,
    // then "app".
    std::string app_name;

    // `CFBundleIdentifier`. Empty derives a reversed-DNS-shaped identifier
    // from `package_namespace()` and `package_name()`; see `bundle_id_for`.
    std::string bundle_id;

    // `CFBundleShortVersionString` and `CFBundleVersion`, used as given, with
    // no conversion: unlike `dist/wix.cppm`'s MSI version, Apple's bundle
    // version keys have no field-width ceiling this member has measured, so
    // mcpp's own four-segment date version needs nothing done to it here.
    // Empty means `package_version()`.
    std::string version;

    // A project-supplied icon file, copied into `Contents/Resources/` and
    // named by `CFBundleIconFile`. Finder specifically expects `.icns` (or
    // the newer `.icon` bundle) to render an application icon; this member
    // does not validate or convert the format, only wires up whatever file
    // is named. Empty omits `Contents/Resources/` and `CFBundleIconFile`
    // entirely -- macOS runs a bundle with no custom icon without complaint.
    std::string icon;

    // `LSMinimumSystemVersion`. mcpp does not expose the compiled deployment
    // target to a build program, so this member does not guess one; empty
    // omits the key, and a project that needs the floor enforced states it.
    std::string minimum_system_version;

    // A `codesign` identity -- a name or hash `security find-identity` would
    // list. Empty means unsigned, which is the default; see the header
    // comment for why signing is opt-in rather than automatic.
    std::string identity;

    // `--options runtime`, the hardened runtime, only meaningful together
    // with `identity`: without signing there is no runtime flag to attach it
    // to.
    bool hardened_runtime = false;

    // `--entitlements <path>`, likewise only applied when `identity` is also
    // set. Validated to exist when named, the same as `icon`.
    std::string entitlements;

    // Where the produced bundle lands. Empty means `<out_dir>/<app_name>.app`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ──────────────────────────────────────────────────────────────

// One chained action per row of the header comment's table, and a submit
// that walks them in dependency order. `generate()` being exactly
// `submit(plan_for())` is what keeps a project's edit -- an extra resource,
// a different signing flag -- from becoming a reimplementation of this
// member, the same trade `dist/appimage.cppm` makes.
struct step {
    const char*               id;
    const char*                role;
    const char*               description;
    std::vector<std::string>  argv;
    std::vector<std::string>  inputs;
    std::string               output;
};

struct plan {
    // False when this build is not `mcpp pack --format app`, which is every
    // ordinary build. `reason` then says which of the several ways.
    bool              applies = false;
    std::string       reason;
    std::string       bundle_path; // the <Name>.app directory
    std::string       appdir;      // pack_stage_dir(), kept for the floor check
    std::vector<step> steps;
    explicit operator bool() const { return applies; }
};

// ─── Internals ─────────────────────────────────────────────────────────────

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && std::filesystem::is_regular_file(p, ec);
}

// Written only when the bytes differ, for the reason `dist/appimage.cppm`
// gives: rewriting identical bytes moves the mtime, and a moved mtime on a
// declared input is indistinguishable from a changed one -- so a second pack
// of an unchanged project would re-copy the bundle and re-sign it.
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

inline std::string app_name_for(const options& opt) {
    if (!opt.app_name.empty()) return opt.app_name;
    if (!opt.target.empty())   return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string("app");
}

// The staged tree's top-level launcher -- the same search
// `dist/appimage.cppm` performs, and for the same reason: `mcpp pack` writes
// one per mode and names it after the binary, so this member states no
// convention mcpp has not already put on disk.
inline std::string launcher_in(const std::string& stage, const std::string& target) {
    for (auto candidate : {stage + "/" + target,
                           stage + "/bin/" + target,
                           stage + "/run.sh"})
        if (is_file(candidate)) return candidate;
    return {};
}

// CFBundleExecutable MUST BE A BARE FILENAME, NOT A PATH.
//
// Apple's own bundle documentation describes it as the executable's name
// within `Contents/MacOS/`, and real-world bundle tooling has hit this
// directly enough to be a filed CMake defect: "CFBundleExecutable path in a
// bundle should not be a relative path into bundle." So this member takes
// only the basename of whatever `launcher_in` finds. That is exactly correct
// when the staged tree's launcher sits at the tree's ROOT, which is the
// default `--mode vendored` shape `dist/appimage.cppm`'s own header comment
// describes ("a top-level launcher"). A staged tree whose launcher were
// nested under `bin/` instead would still copy correctly by the layout step
// below, but the resulting bundle would name an executable that is not at
// the top of `Contents/MacOS/`, and this member does not flatten that case --
// it is not exercised by the default staging mode, and nothing here can
// exercise it on Linux to find out how macOS actually responds.
inline std::string bundle_executable_name(const std::string& launcher_path) {
    return std::filesystem::path(launcher_path).filename().string();
}

inline std::string sanitize_bundle_id_component(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9') || c == '-' || c == '.';
        out += ok ? c : '-';
    }
    return out;
}

// A reversed-DNS-SHAPED identifier, not a literal reversal of a domain --
// mcpp packages carry `(namespace, name)`, not a domain to reverse. Sanitised
// to the character set Apple documents for `CFBundleIdentifier`
// (alphanumeric, `-`, `.`), because a package name or namespace is free text
// as far as this member is concerned.
inline std::string bundle_id_for(const options& opt) {
    if (!opt.bundle_id.empty()) return opt.bundle_id;
    const char* nsC = mcpp::package_namespace();
    const char* nmC = mcpp::package_name();
    const std::string ns = (nsC && *nsC) ? sanitize_bundle_id_component(nsC) : std::string();
    const std::string nm = (nmC && *nmC) ? sanitize_bundle_id_component(nmC) : std::string();
    if (!ns.empty() && !nm.empty()) return ns + "." + nm;
    if (!nm.empty()) return nm;
    if (!ns.empty()) return ns;
    return "app";
}

inline std::string plist_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out += c;
        }
    }
    return out;
}

// Written as XML text directly rather than by shelling out to `plutil`: the
// content is a handful of string keys this member already holds, and adding
// a second host tool to discover and invoke would buy nothing over
// formatting the eleven lines by hand. `LSMinimumSystemVersion` and
// `CFBundleIconFile` are omitted rather than emitted empty when their inputs
// are empty -- an empty string in either key is a claim as unsupported as
// omitting the key, so omission is the honest one.
inline std::string plist_document(const std::string& executable, const std::string& bundle_id,
                                  const std::string& name, const std::string& version,
                                  const std::string& min_system_version,
                                  const std::string& icon_name) {
    std::string doc;
    doc += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    doc += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    doc += "<plist version=\"1.0\">\n<dict>\n";
    doc += std::format("    <key>CFBundleExecutable</key>\n    <string>{}</string>\n",
                       plist_escape(executable));
    doc += std::format("    <key>CFBundleIdentifier</key>\n    <string>{}</string>\n",
                       plist_escape(bundle_id));
    doc += std::format("    <key>CFBundleName</key>\n    <string>{}</string>\n",
                       plist_escape(name));
    doc += std::format("    <key>CFBundleShortVersionString</key>\n    <string>{}</string>\n",
                       plist_escape(version));
    doc += std::format("    <key>CFBundleVersion</key>\n    <string>{}</string>\n",
                       plist_escape(version));
    doc += "    <key>CFBundlePackageType</key>\n    <string>APPL</string>\n";
    if (!min_system_version.empty())
        doc += std::format("    <key>LSMinimumSystemVersion</key>\n    <string>{}</string>\n",
                           plist_escape(min_system_version));
    doc += "    <key>NSHighResolutionCapable</key>\n    <true/>\n";
    // Not in the letter of this member's key list, but added deliberately:
    // without it, an icon file this member went to the trouble of copying
    // into `Contents/Resources/` is inert -- nothing in the bundle would ever
    // reference it, and Finder would show the generic application icon
    // regardless of `options::icon`. Wiring the file up once it exists is
    // what makes the option do what its name says.
    if (!icon_name.empty())
        doc += std::format("    <key>CFBundleIconFile</key>\n    <string>{}</string>\n",
                           plist_escape(icon_name));
    doc += "</dict>\n</plist>\n";
    return doc;
}

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    // NOT THIS PASS. Every ordinary build lands here, and the empty
    // `pack_format()` is what says so -- see `generate` for why the
    // DECLARATION must not be gated the same way.
    const std::string requested = mcpp::pack_format();
    if (requested != "app") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not app", requested);
        return p;
    }

    // macOS only, and this is a refusal rather than a silent skip: a user who
    // typed `--format app` on Linux asked for something that does not exist
    // there, and the engine has already accepted the value because the graph
    // declared it.
    if (const std::string os = mcpp::target_os(); os != "macos") {
        std::cerr << std::format(
            "mcpp.dist.apple: a .app bundle is a macOS format, and this "
            "build targets '{}'.\n"
            "  use: --format tar, or build for a macOS target",
            os.empty() ? "unknown" : os) << '\n';
        p.reason = "not a macOS target";
        return p;
    }

    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty()) {
        std::cerr << "mcpp.dist.apple: mcpp reported no staged tree. This "
                     "member needs mcpp 2026.9.11.1 or newer.\n";
        p.reason = "no staged tree";
        return p;
    }

    const std::string target = target_for(opt);
    if (target.empty()) {
        std::cerr << "mcpp.dist.apple: no target to bundle. Set "
                     "`options::target` to the program target's name.\n";
        p.reason = "no target";
        return p;
    }

    const std::string launcher = launcher_in(stage, target);
    if (launcher.empty()) {
        std::cerr << std::format(
            "mcpp.dist.apple: the staged tree at {0} carries no launcher for "
            "target '{1}'.\n"
            "  expected one of: {0}/{1}, {0}/bin/{1}, {0}/run.sh",
            stage, target) << '\n';
        p.reason = "no launcher in the staged tree";
        return p;
    }
    const std::string executableName = bundle_executable_name(launcher);

    if (!opt.icon.empty() && !is_file(opt.icon)) {
        std::cerr << std::format("mcpp.dist.apple: the icon {} was not found", opt.icon) << '\n';
        p.reason = "icon not found";
        return p;
    }
    if (!opt.entitlements.empty() && !is_file(opt.entitlements)) {
        std::cerr << std::format(
            "mcpp.dist.apple: the entitlements file {} was not found", opt.entitlements) << '\n';
        p.reason = "entitlements not found";
        return p;
    }

    const char* pv = mcpp::package_version();
    const std::string version = !opt.version.empty() ? opt.version
                               : (pv && *pv ? std::string(pv) : std::string());
    if (version.empty()) {
        std::cerr << "mcpp.dist.apple: no version to state. Set "
                     "`[package] version` or `options::version`.\n";
        p.reason = "no version";
        return p;
    }

    const std::string name       = app_name_for(opt);
    const std::string bundleId   = bundle_id_for(opt);
    const std::string bundlePath = !opt.output.empty() ? opt.output
                                  : (std::filesystem::path(opt.out_dir) / (name + ".app")).string();
    const std::string contents   = bundlePath + "/Contents";

    const std::string iconName = opt.icon.empty() ? std::string()
                                : std::filesystem::path(opt.icon).filename().string();
    const std::string plistBytes = plist_document(executableName, bundleId, name, version,
                                                  opt.minimum_system_version, iconName);
    const std::string plistSrc = (std::filesystem::path(opt.out_dir) / (name + "-Info.plist")).string();
    if (!write_if_different(plistSrc, plistBytes)) {
        std::cerr << std::format("mcpp.dist.apple: cannot write {}", plistSrc) << '\n';
        p.reason = "cannot write Info.plist";
        return p;
    }

    p.bundle_path = bundlePath;
    p.appdir      = stage;

    // Every action's output that later steps may need to depend on, gathered
    // as they are declared so the final, conditional codesign step can name
    // exactly the ones that ran.
    std::vector<std::string> assembled;

    step info;
    info.id          = "mcpp.dist.apple.info-plist";
    info.role        = "artifact";
    info.description = "INFO.PLIST";
    info.argv         = { "ditto", plistSrc, contents + "/Info.plist" };
    info.inputs       = { plistSrc };
    info.output       = contents + "/Info.plist";
    p.steps.push_back(info);
    assembled.push_back(info.output);

    step layout;
    layout.id          = "mcpp.dist.apple.layout";
    layout.role        = "artifact";
    layout.description = "APP LAYOUT";
    // The whole staged tree, named through the placeholder rather than the
    // absolute path this program read, so the path in the graph and the path
    // here cannot disagree -- the same reasoning `dist/appimage.cppm` gives
    // for its own `${mcpp.stage_dir}` argument. Naming it here also earns
    // this action the engine's automatic dependency on the staged tree's
    // manifest (`docs/30-build-mcpp.md`, "An action that names
    // `${mcpp.stage_dir}` gains a dependency on the tree's manifest").
    layout.argv         = { "ditto", "${mcpp.stage_dir}", contents + "/MacOS" };
    layout.inputs       = { std::format("${{mcpp.target_file:{}}}", target) };
    layout.output       = contents + "/MacOS/" + executableName;
    p.steps.push_back(layout);
    assembled.push_back(layout.output);

    if (!opt.icon.empty()) {
        step icon;
        icon.id          = "mcpp.dist.apple.icon";
        icon.role        = "artifact";
        icon.description = "APP ICON";
        icon.argv         = { "ditto", opt.icon, contents + "/Resources/" + iconName };
        icon.inputs       = { opt.icon };
        icon.output       = contents + "/Resources/" + iconName;
        p.steps.push_back(icon);
        assembled.push_back(icon.output);
    }

    if (!opt.identity.empty()) {
        step sign;
        sign.id          = "mcpp.dist.apple.codesign";
        sign.role        = "artifact";
        sign.description = "CODESIGN";
        sign.argv = { "codesign", "--force", "--sign", opt.identity, "--timestamp" };
        if (opt.hardened_runtime) { sign.argv.push_back("--options"); sign.argv.push_back("runtime"); }
        if (!opt.entitlements.empty()) {
            sign.argv.push_back("--entitlements");
            sign.argv.push_back(opt.entitlements);
        }
        sign.argv.push_back(bundlePath);
        // Depends on every other step's output, because codesign covers the
        // bundle's content at signing time -- see the header comment.
        sign.inputs = assembled;
        if (!opt.entitlements.empty()) sign.inputs.push_back(opt.entitlements);
        // codesign has no flag to write a receipt to an arbitrary path, so
        // this names the one file signing a BUNDLE (rather than a flat
        // Mach-O) is documented to write as part of embedding the signature:
        // `Contents/_CodeSignature/CodeResources`, a manifest of the signed
        // resources' hashes. This is documented Apple codesign behaviour,
        // not something measured here -- codesign does not run on Linux.
        sign.output = contents + "/_CodeSignature/CodeResources";
        p.steps.push_back(sign);
    }

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
        for (auto const& tok : s.argv)   a.arg(tok.c_str());
        for (auto const& in  : s.inputs) a.input(in.c_str());
        a.output(s.output.c_str());
        a.submit();
    }

    // A FLOOR ON THIS MEMBER'S OWN OUTPUT, ON THE SUCCESS PATH.
    //
    // The assembled bundle does not exist when this program runs -- ditto and
    // codesign have not been invoked yet, only declared -- so what this
    // member measures instead is the staged tree the layout step is about to
    // copy, exactly as `dist/appimage.cppm` measures its own AppDir before
    // appimagetool runs. This catches "nothing was linked or staged"; it
    // cannot catch a `ditto` that fails partway, a codesign that fails to
    // verify, or the bare-filename assumption `bundle_executable_name`
    // documents not holding for a non-default staging layout -- all of those
    // happen after this program has already exited.
    std::error_code ec;
    std::uintmax_t bytes = 0;
    for (auto const& e : std::filesystem::recursive_directory_iterator(p.appdir, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec)) bytes += std::filesystem::file_size(e.path(), ec);
    }
    // Loose on purpose, matching `dist/appimage.cppm`'s own bound: this
    // exists to catch "nothing was staged", not to police a size budget.
    // Unlike that member, nothing is written INTO the staged tree here --
    // `Info.plist` and the icon live outside it until the layout and install
    // steps run -- so even a low bound is already suspicious.
    if (bytes < 4u * 1024u) {
        static char msg[512];
        std::snprintf(msg, sizeof msg,
            "mcpp.dist.apple: the staged tree at %s holds only %llu bytes, "
            "which is not a program; the .app will not launch anything",
            p.appdir.c_str(), static_cast<unsigned long long>(bytes));
        mcpp::warning(msg);
    }
    return true;
}

// ─── The one call a consumer makes ─────────────────────────────────────────

// DECLARE UNCONDITIONALLY, SUBMIT CONDITIONALLY -- and both halves are here so
// a consumer cannot do one without the other.
//
// The declaration is what lets the engine answer a question the requesting
// build cannot: `mcpp pack --format bogus` names what is available, and
// `--help` says "any format the resolved graph provides". Both read the set
// collected from a pass that asked for nothing. A member that declared only
// when asked still works for its author -- they always pass their own
// format -- and makes the set unknowable for everyone else.
inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("app");
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::apple
