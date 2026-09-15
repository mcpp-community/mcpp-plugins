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
// HOW MANY ACTIONS, AND WHY EACH IS SEPARATE:
//
//   1. install `Info.plist`         (always)
//   2. lay out the program          (always)
//   3. one per deployed resource    (one per entry the staged tree carries)
//   4. one per closure dylib        (copied to the framework directory, signed)
//   5. install the icon             (only when `options::icon` is set)
//   6. codesign the bundle          (always on macOS; on an iOS device row only
//                                    when `options::identity` is set)
//   7. the bundle itself            (always, last)
//   8. two for `--format dmg`       (stage the bundle beside an `Applications`
//                                    link, then `hdiutil create`)
//
// 1, 3 and 5 are separate from 2 because they have different INPUTS:
// `Info.plist` is regenerated whenever package metadata changes, the icon only
// when the project's icon file changes, and the program only when it is
// relinked. One action for all of them would make every one of those changes
// re-run every copy. 6 is last of the CONTENT steps and depends on the OUTPUTS
// of every step before it, because a code signature covers the bundle's content
// at signing time -- signing before the content is in place is either a failure
// (an incomplete bundle) or a signature that the next file added invalidates.
//
// 7 EXISTS BECAUSE THE CONTENT STEPS ARE PARALLEL, AND `mcpp run` NEEDS ONE
// OPERAND. Each of them writes a file inside the bundle and consumes none of
// the others' outputs, so a request that submits only those has as many
// terminal artifacts (outputs nothing else consumes) as steps actually ran,
// never one. `mcpp run --format app` resolves to THE terminal
// artifact, so a plan with more than one has none it can hand the runner.
// Step 7's output is the bundle DIRECTORY -- the actual distributable of this
// format -- and its inputs are every other step's output, so it is always
// the plan's sole terminal, in both the `Contents/`-shaped and flat-iOS
// layouts. Under `--format dmg` the bundle is an input of the image, and the
// `.dmg` is the terminal instead.
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
// ON macOS THE BUNDLE IS SIGNED AD HOC UNLESS AN IDENTITY IS GIVEN. A bundle
// that carries a framework and no signature fails `codesign --verify --deep
// --strict` ("code has no resources but signature indicates they must be
// present", measured on macos-15, mcpp#635 run 2): the program the linker
// signed is sealed, and the bundle around it is not. Signing ad hoc needs no
// identity and no keychain, so it is the default, dylibs first and the bundle
// second (the same run measured that order verifying). `options::identity`
// signs with that identity instead, with `--timestamp`. Notarisation is out of
// scope entirely, and not merely deferred: it requires uploading the bundle to
// Apple over the network and waiting on a ticket, and a build must not reach
// the network -- the same rule `dist/appimage.cppm` states for appimagetool's
// runtime-stub download, here applying to a step this member does not attempt.
//
// THE CLOSURE'S DYLIBS GO TO THE FRAMEWORK DIRECTORY (mcpp 2026.9.14.2+). The
// engine reads a Mach-O program's closure and stages the dylibs it resolves
// beside the program in `bin/`, naming each in the stage manifest's `needs`
// lines. This member copies those into `Contents/Frameworks/` (`Frameworks/` on
// iOS), keeps them out of the resource directory, and gives the program the
// rpath that finds them there -- `@executable_path/../Frameworks`, or
// `@executable_path/Frameworks` on iOS -- through `mcpp::link_flag` at link
// time, so no file is edited after it is linked and no load command is
// rewritten. An rpath edit after the link was measured and not taken: it fails
// on a program linked without header padding ("larger updated load commands do
// not fit") and invalidates the linker's signature (mcpp#635 run 4). The rpath
// is added to every link of a macOS or iOS program whose build program calls
// `generate()`, packed or not, because the link happens before the pass that
// learns `--format`.
//
// `mcpp run --format app` REACHES THE BUNDLE THROUGH `macapp-run` ON macOS.
// This member supplies the runner named `app` (`xim:macapp-run`, which executes
// the bundle's `CFBundleExecutable` in the foreground, so its output and exit
// status are the program's), and mcpp uses the runner named after a format for
// that format (mcpp 2026.9.14.2+). A project that declares its own
// `[target.<triple>.runners] app` keeps it. The iOS rows keep the runner their
// manifests name (`simctl-run`).
//
// `--format dmg` IS A DISK IMAGE OF THE BUNDLE. The bundle and a link to
// `/Applications` are staged in one directory and `hdiutil create -format UDZO`
// writes the image, the layout a user drags from. Measured on macos-15
// (mcpp#635 run 2): the image is created, `hdiutil verify` accepts it, and it
// attaches read-only with the bundle and the link. macOS only.
//
// iOS IS THE SAME SHAPE, A FLAT LAYOUT INSTEAD OF `Contents/`, AND THREE
// KEYS `-format app` NEVER WROTE (#622 B1). The target row and the SDK
// (`aarch64-ios-sim`, `aarch64-ios`) are the engine's; what was missing here
// was the branch, not a mechanism -- every action below is the same four
// steps the header above already lists, addressed at the bundle's own root
// rather than `Contents/`.
//
// FLAT, BECAUSE THAT IS WHAT AN iOS BUNDLE IS. There is no `Contents/`
// subdirectory on this platform: the executable, `Info.plist` and every
// resource sit directly under `<Name>.app/`. So `execDir`, `plistDst` and
// `resourceDir` below are the bundle root itself on this branch and
// `Contents/MacOS`, `Contents/Info.plist`, `Contents/Resources` on the
// macOS one -- one predicate, read once, rather than four `os == "macos"`
// checks scattered through the function.
//
// `CFBundleSupportedPlatforms` READS THE SIMULATOR FROM `target_env()`, NOT
// FROM A SEPARATE OPTION. `aarch64-ios-sim` and `aarch64-ios` are two
// triples for one OS (`triple.cppm`'s own comment: "the simulator is
// deliberately not a row [of its own identity]... it has its own SDK"), so
// the engine already carries the distinction this key needs; restating it
// as an `options` field would be a second copy of what `target_env()`
// answers.
//
// SIGNING SPLITS ON THE SAME PREDICATE. A simulator bundle installs
// unsigned -- `simctl install` does not check a signature -- so
// `options::identity` is ignored there rather than attempted and left to
// fail inside `codesign`, which cannot produce a device-shaped signature
// for a simulator binary in any case. The device row signs only with an
// identity, because a device refuses an ad-hoc signature; its `codesign`
// step is the macOS one with an identity: same argv shape, same
// hardened-runtime and entitlements flags.
//
// ICONS TAKE A DIRECTORY ON THIS ROW, A FILE ON THE OTHER. macOS names one
// `.icns`; iOS's convention is a set of flat PNGs at several pixel sizes,
// listed by stem under `CFBundleIcons` / `CFBundlePrimaryIcon` /
// `CFBundleIconFiles`. Generating the required sizes from a source image is
// Xcode's `actool`, which is not redistributable and not reimplemented
// here (the same boundary `codesign` and `wix.exe` already draw): this
// member copies whatever PNGs the project already has into the bundle root
// and lists their stems. A project supplying the wrong sizes gets a bundle
// that installs and a Home Screen icon Apple's UI does not like -- a
// cosmetic failure, not a build one.
//
// A PROJECT'S OWN Info.plist ENTRIES, AND A DEVICE BUNDLE THAT INSTALLS (0.11.0).
// `options::info_plist` names a plist whose top-level entries join the bundle's
// -- the usage descriptions, URL types and background modes an Xcode project's
// Info.plist carries -- while the keys this member derives stay its own and are
// refused by name. On the iOS device row, `options::provisioning_profile` is
// embedded as `embedded.mobileprovision` and, unless `options::entitlements`
// names a file, supplies the entitlements the bundle is signed with; the
// profile's plist is read from the file itself, so a plan made on any host
// checks the bundle identifier against it. `mcpp run --format app` on that row
// reaches the device through the runner named `app` this member supplies,
// `devicectl-run` (`xim:apple-device-tools`), as `macapp-run` serves macOS.

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

    // On macOS: a project-supplied icon FILE, copied into
    // `Contents/Resources/` and named by `CFBundleIconFile`. Finder
    // specifically expects `.icns` (or the newer `.icon` bundle) to render
    // an application icon; this member does not validate or convert the
    // format, only wires up whatever file is named. Empty omits
    // `Contents/Resources/` and `CFBundleIconFile` entirely -- macOS runs a
    // bundle with no custom icon without complaint.
    //
    // On iOS: a project-supplied DIRECTORY of flat PNGs -- a file here is
    // refused, naming the directory shape iOS expects. Every `*.png` in it
    // is copied to the bundle's root and its stem (the filename without
    // `.png`) is listed under `CFBundleIcons` / `CFBundlePrimaryIcon` /
    // `CFBundleIconFiles`, so a project names its icon set once, at
    // whatever sizes it has generated, rather than once per size in this
    // member's own vocabulary. Empty omits the bundle icon entirely, as on
    // macOS.
    std::string icon;

    // `LSMinimumSystemVersion` on macOS, `MinimumOSVersion` on iOS.
    //
    // ON iOS THIS IS AN OVERRIDE, NOT THE ONLY SOURCE. mcpp 2026.9.12.2
    // exposes the compiled deployment target as `mcpp::min_platform_version()`
    // (#622 A11) -- the same value the linked Mach-O's `LC_BUILD_VERSION`
    // carries -- and this member reads it first, falling back to this field
    // only when the engine reports nothing (a target row the engine does not
    // yet compute a floor for). A project therefore states
    // `[build] ios_deployment_target` once and this key is free.
    //
    // ON macOS THIS STAYS THE ONLY SOURCE, deliberately: `min_platform_version`
    // answers non-empty on macOS only when mcpp itself runs on a Mac
    // (`mcpp::platform::macos::deployment_target` is guarded `#if defined
    // (__APPLE__)`, because `MACOSX_DEPLOYMENT_TARGET` and the SDK default
    // it falls back to are both properties of the machine RUNNING mcpp, not
    // of the target triple), so reading it here would make a `.app` built
    // by a Linux packaging host silently lose the key a macOS host would
    // have set. Extending the engine reading to macOS is future work the
    // design record recommends and this member does not take, so that the
    // existing macOS fixture's bundle is unchanged by this row's addition.
    std::string minimum_system_version;

    // A `codesign` identity -- a name or hash `security find-identity` would
    // list. Empty means an ad-hoc signature on macOS and no signature on the
    // iOS rows; see the header comment's signing paragraphs. Ignored on the
    // iOS Simulator row regardless of this value, and a non-empty value there
    // produces a `mcpp::warning` naming why rather than a signature.
    std::string identity;

    // `--options runtime`, the hardened runtime, only meaningful together
    // with `identity`: without signing there is no runtime flag to attach it
    // to.
    bool hardened_runtime = false;

    // `--entitlements <path>`, likewise only applied when `identity` is also
    // set. Validated to exist when named, the same as `icon`.
    std::string entitlements;

    // A PLIST OF THE PROJECT'S OWN Info.plist ENTRIES (0.11.0), manifest-relative
    // or absolute: every entry of its top-level `<dict>` joins the bundle's
    // Info.plist. The keys this member derives from the options and the engine
    // -- `CFBundleExecutable`, `CFBundleIdentifier`, `CFBundleName`, the two
    // version keys, `CFBundlePackageType`, the minimum OS keys,
    // `CFBundleSupportedPlatforms`, `CFBundleIconFile` and `CFBundleIcons` -- are
    // refused by name, because an option or the engine states each; the three
    // it only defaults (`UIDeviceFamily`, `LSRequiresIPhoneOS`,
    // `NSHighResolutionCapable`) are replaced by the project's value.
    std::string info_plist;

    // DEFAULTED Info.plist KEYS TO LEAVE OUT (0.12.0). A property list has no
    // null, so `info_plist` can replace a default's value but cannot remove the
    // key; a project whose other build states neither `NSHighResolutionCapable`
    // nor `LSRequiresIPhoneOS` names them here (#649 P1). Only the three keys
    // this member defaults are accepted: a key it derives is refused by name, as
    // `info_plist` refuses it, and so is any other key, which this member never
    // writes. A key named here and also set by `info_plist` is refused, because
    // the two statements contradict each other. A key that does not apply to the
    // row (`UIDeviceFamily` on macOS) is accepted and changes nothing.
    std::vector<std::string> omit_keys;

    // AN iOS DEVICE BUNDLE'S PROVISIONING PROFILE (0.11.0), manifest-relative or
    // absolute: embedded as `embedded.mobileprovision`, and, when `entitlements`
    // is empty, the source of the entitlements the bundle is signed with (the
    // profile's own `Entitlements` dictionary). Its `application-identifier`
    // must cover `bundle_id`. Only the device row (`aarch64-ios`) takes one, and
    // it needs `identity`: a device refuses an ad-hoc signature.
    std::string provisioning_profile;

    // Where the produced bundle lands. Empty means `<out_dir>/<app_name>.app`.
    std::string output;

    // `--format dmg`: the volume's name, and where the image lands. Empty
    // means `app_name` and `<out_dir>/<app_name>.dmg`.
    std::string volume_name;
    std::string dmg;

    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ──────────────────────────────────────────────────────────────

// One chained action per row of the header comment's table, and a submit
// that walks them in dependency order. `generate()` being exactly
// `submit(plan_for())` is what keeps a project's edit -- an extra resource,
// a different signing flag -- from becoming a reimplementation of this
// member, the same trade `dist/appimage.cppm` makes.
struct step {
    // Owned strings rather than literals: the resource steps below name one
    // action per deployed entry, and an id derived from a file name has to
    // outlive the function that formed it.
    std::string               id;
    const char*               role;
    std::string               description;
    std::vector<std::string>  argv;
    std::vector<std::string>  inputs;
    // MORE THAN ONE OUTPUT ON THE iOS ICON STEP: a directory of PNGs copies
    // to a directory of PNGs, and the graph rule is "name the output
    // files" -- plural, when a single command produces several. Every other
    // step still declares exactly one; a vector costs those nothing.
    std::vector<std::string>  outputs;
};

struct plan {
    // False when this build is not `mcpp pack --format app`, which is every
    // ordinary build. `reason` then says which of the several ways.
    bool              applies = false;
    std::string       reason;
    std::string       bundle_path; // the <Name>.app directory
    std::string       dmg_path;    // the .dmg, under --format dmg
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

// Records a refusal on stderr and as a `mcpp::warning`. A member that refuses
// submits no action and its build program exits 0, and the engine discards the
// output of a build program that succeeded; its own error, "no action claimed
// --format 'app'", names no reason. The warning channel is one line per
// directive, so line breaks are folded into spaces.
inline plan& refuse(plan& p, std::string reason, const std::string& message) {
    std::cerr << message << '\n';
    std::string folded;
    folded.reserve(message.size());
    bool space = false;
    for (std::size_t i = 0; i < message.size(); ++i) {
        const char c = message[i];
        if (c == '\n' || c == '\r') { space = true; continue; }
        if (space) {
            if (c == ' ') continue;
            folded += ' ';
            space = false;
        }
        folded += c;
    }
    mcpp::warning(folded.c_str());
    p.reason = std::move(reason);
    return p;
}

// A helper script, written into `<out_dir>/dist-apple/` at plan time when its
// bytes differ, and run by `/bin/sh` so that no permission bit is needed.
inline std::string helper_script(const std::string& out_dir, const char* name,
                                 std::string_view body) {
    const auto path = std::filesystem::path(out_dir) / "dist-apple" / name;
    write_if_different(path, body);
    return path.string();
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

// The program a bundle executes, found in the staged tree: `bin/<target>`
// first, then the tree's top-level entry, then `run.sh`.
//
// THE PROGRAM, NOT THE TREE'S ENTRY SCRIPT. From the release that reads a
// Mach-O program's closure (mcpp 2026.9.14.2), the engine also writes
// `<tree>/<target>`, a shell script that executes `bin/<target>` from the
// tree's root. The search used to take that root entry first, which is the
// order `dist/appimage.cppm` needs, and a bundle then executed the script,
// which executed `Contents/MacOS/bin/<target>` -- a file no bundle carries:
// "cannot execute: No such file or directory", exit 126 (macos-15, run
// 34821164486). The script has no work to do inside a bundle, where
// `CFBundleExecutable` names the program directly, and the framework rpath
// `@executable_path/../Frameworks` resolves against the program's own
// directory, which has to be `Contents/MacOS/`. The root entry and `run.sh`
// remain for a tree that carries no `bin/<target>`.
inline std::string launcher_in(const std::string& stage, const std::string& target) {
    for (auto candidate : {stage + "/bin/" + target,
                           stage + "/" + target,
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
// only the basename of whatever `launcher_in` finds, and the layout step
// copies that file to the top of the executable directory under that name, so
// the name and the file agree wherever in the staged tree it was found.
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
// formatting the lines by hand. `LSMinimumSystemVersion` / `MinimumOSVersion`
// and `CFBundleIconFile` are omitted rather than emitted empty when their
// inputs are empty -- an empty string in either key is a claim as
// unsupported as omitting the key, so omission is the honest one.
//
// `is_ios` AND `is_sim` DECIDE FOUR KEYS, NOT ONE BRANCH. `MinimumOSVersion`
// replaces `LSMinimumSystemVersion`; `CFBundleSupportedPlatforms`,
// `UIDeviceFamily` and `LSRequiresIPhoneOS` are iOS-only and absent from
// every macOS bundle this member has ever written, which is the
// byte-identical property the macOS fixture depends on; `NSHighResolutionCapable`
// is a macOS concept (Retina-aware drawing on a platform that also has
// non-Retina displays) with nothing to opt into on iOS, so it is macOS-only
// in the other direction.
inline std::string plist_document(const std::string& executable, const std::string& bundle_id,
                                  const std::string& name, const std::string& version,
                                  bool is_ios, bool is_sim,
                                  const std::string& min_os_version,
                                  // The project's own entries (0.11.0), already
                                  // formatted, inserted before `</dict>`; and the
                                  // defaulted keys they replace.
                                  const std::string& extra_entries,
                                  const std::vector<std::string>& replaced_keys,
                                  // macOS: the icon FILE's basename, extension
                                  // included, exactly as `CFBundleIconFile` has
                                  // always taken it. iOS: unused (see
                                  // `ios_icon_stems`) and always empty.
                                  const std::string& mac_icon_name,
                                  // iOS: every PNG stem found in
                                  // `options::icon`'s directory. macOS: unused
                                  // and always empty.
                                  const std::vector<std::string>& ios_icon_stems) {
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
    if (!min_os_version.empty())
        doc += std::format("    <key>{}</key>\n    <string>{}</string>\n",
                           is_ios ? "MinimumOSVersion" : "LSMinimumSystemVersion",
                           plist_escape(min_os_version));
    if (is_ios) {
        // `env == "sim"` is the engine's own distinction between the two
        // rows (triple.cppm: "the simulator is deliberately not a row of
        // its own identity... it has its own SDK"); this key is the one
        // place a bundle has to restate it, because `simctl install` and a
        // real device's installer both read it to refuse the other kind.
        doc += std::format(
            "    <key>CFBundleSupportedPlatforms</key>\n    <array>\n"
            "        <string>{}</string>\n    </array>\n",
            is_sim ? "iPhoneSimulator" : "iPhoneOS");
        // `[1, 2]`: iPhone and iPad. Nothing here narrows a project to one
        // idiom -- that is a project decision (a size class, a storyboard)
        // this member has no basis for making.
        const auto replaced = [&](std::string_view k) {
            return std::ranges::find(replaced_keys, k) != replaced_keys.end();
        };
        if (!replaced("UIDeviceFamily"))
            doc += "    <key>UIDeviceFamily</key>\n    <array>\n"
                   "        <integer>1</integer>\n        <integer>2</integer>\n    </array>\n";
        if (!replaced("LSRequiresIPhoneOS"))
            doc += "    <key>LSRequiresIPhoneOS</key>\n    <true/>\n";
        if (!ios_icon_stems.empty()) {
            doc += "    <key>CFBundleIcons</key>\n    <dict>\n"
                   "        <key>CFBundlePrimaryIcon</key>\n        <dict>\n"
                   "            <key>CFBundleIconFiles</key>\n            <array>\n";
            for (auto const& stem : ios_icon_stems)
                doc += std::format("                <string>{}</string>\n", plist_escape(stem));
            doc += "            </array>\n        </dict>\n    </dict>\n";
        }
    } else {
        if (std::ranges::find(replaced_keys, std::string_view("NSHighResolutionCapable")) == replaced_keys.end())
            doc += "    <key>NSHighResolutionCapable</key>\n    <true/>\n";
        // Not in the letter of this member's key list, but added
        // deliberately: without it, an icon file this member went to the
        // trouble of copying into `Contents/Resources/` is inert -- nothing
        // in the bundle would ever reference it, and Finder would show the
        // generic application icon regardless of `options::icon`. Wiring the
        // file up once it exists is what makes the option do what its name
        // says.
        if (!mac_icon_name.empty())
            doc += std::format("    <key>CFBundleIconFile</key>\n    <string>{}</string>\n",
                               plist_escape(mac_icon_name));
    }
    doc += extra_entries;
    doc += "</dict>\n</plist>\n";
    return doc;
}
inline std::string plist_document(const std::string& executable, const std::string& bundle_id,
                                  const std::string& name, const std::string& version,
                                  bool is_ios, bool is_sim,
                                  const std::string& min_os_version,
                                  const std::string& mac_icon_name,
                                  const std::vector<std::string>& ios_icon_stems) {
    return plist_document(executable, bundle_id, name, version, is_ios, is_sim, min_os_version,
                          std::string(), std::vector<std::string>(), mac_icon_name, ios_icon_stems);
}

inline std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string resolve_path(const std::string& p) {
    if (p.empty() || std::filesystem::path(p).is_absolute()) return p;
    return (std::filesystem::path(mcpp::manifest_dir()) / p).lexically_normal().string();
}

// The keys `plist_document` derives, which a project's plist may not restate,
// and the keys it only defaults, which a project's plist replaces.
inline const std::vector<std::string>& derived_plist_keys() {
    static const std::vector<std::string> v = {
        "CFBundleExecutable", "CFBundleIdentifier", "CFBundleName", "CFBundleShortVersionString",
        "CFBundleVersion", "CFBundlePackageType", "LSMinimumSystemVersion", "MinimumOSVersion",
        "CFBundleSupportedPlatforms", "CFBundleIconFile", "CFBundleIcons"};
    return v;
}
inline const std::vector<std::string>& defaulted_plist_keys() {
    static const std::vector<std::string> v = {"UIDeviceFamily", "LSRequiresIPhoneOS", "NSHighResolutionCapable"};
    return v;
}

// The top-level `<dict>` of a property list, or null. A document whose root is
// `<dict>` itself is accepted as well as a full `<plist>`.
inline const mcpp::plugins::xml::node* top_dict(const mcpp::plugins::xml::node& root) {
    if (root.name == "dict") return &root;
    if (root.name != "plist") return nullptr;
    for (auto const& c : root.children) if (c.name == "dict") return &c;
    return nullptr;
}

// The value element after each `<key>` of a `<dict>`, as (key, value) pairs.
inline bool dict_entries(const mcpp::plugins::xml::node& dict,
                         std::vector<std::pair<std::string, const mcpp::plugins::xml::node*>>& out,
                         std::string& error) {
    namespace xml = mcpp::plugins::xml;
    for (std::size_t i = 0; i < dict.children.size(); ++i) {
        const auto& k = dict.children[i];
        if (k.name.empty()) continue;
        if (k.name != "key") { error = "<" + k.name + "> appears where a <key> is expected"; return false; }
        const std::string key = k.children.size() == 1 && k.children.front().name.empty()
            ? xml::trim_copy(k.children.front().text) : std::string();
        std::size_t j = i + 1;
        while (j < dict.children.size() && dict.children[j].name.empty()) ++j;
        if (key.empty() || j >= dict.children.size()) { error = "a <key> without a value"; return false; }
        out.emplace_back(key, &dict.children[j]);
        i = j;
    }
    return true;
}

// Reads `options::info_plist`: the entries to add, as the plist text
// `plist_document` inserts, and the defaulted keys they replace.
inline bool read_info_plist_fragment(const std::string& path, std::string& entries,
                                     std::vector<std::string>& replaced, std::string& message) {
    namespace xml = mcpp::plugins::xml;
    xml::node root;
    std::string err;
    if (!xml::parse(read_text(path), root, err)) {
        message = std::format("mcpp.dist.apple: `options::info_plist` ({}) cannot be read: {}", path, err);
        return false;
    }
    const xml::node* dict = top_dict(root);
    if (!dict) {
        message = std::format("mcpp.dist.apple: `options::info_plist` ({}) has no top-level <dict>.", path);
        return false;
    }
    std::vector<std::pair<std::string, const xml::node*>> pairs;
    if (!dict_entries(*dict, pairs, err)) {
        message = std::format("mcpp.dist.apple: `options::info_plist` ({}): {}", path, err);
        return false;
    }
    std::vector<std::string> seen;
    for (auto const& [key, value] : pairs) {
        if (std::ranges::find(derived_plist_keys(), key) != derived_plist_keys().end()) {
            message = std::format(
                "mcpp.dist.apple: `options::info_plist` ({}) sets {}, which this member derives "
                "from its options and the engine; set the option instead.", path, key);
            return false;
        }
        if (std::ranges::find(seen, key) != seen.end()) {
            message = std::format("mcpp.dist.apple: `options::info_plist` ({}) sets {} twice.", path, key);
            return false;
        }
        seen.push_back(key);
        if (std::ranges::find(defaulted_plist_keys(), key) != defaulted_plist_keys().end())
            replaced.push_back(key);
        entries += "    <key>" + key + "</key>\n";
        xml::write(*value, entries, 1);
    }
    return true;
}

// A provisioning profile is a CMS-signed property list. The plist is stored in
// the signed content as-is, so it is read from between its `<?xml` and
// `</plist>` rather than through `security cms -D`, which exists only on macOS
// and would make a plan made elsewhere unable to check anything. The signature
// is the device's to verify, not this member's.
inline bool read_profile(const std::string& path, const std::string& bundleId,
                         std::string& entitlementsPlist, std::string& message) {
    namespace xml = mcpp::plugins::xml;
    const std::string bytes = read_text(path);
    const auto b = bytes.find("<?xml");
    const auto e = bytes.find("</plist>");
    if (b == std::string::npos || e == std::string::npos || e < b) {
        message = std::format(
            "mcpp.dist.apple: `options::provisioning_profile` ({}) carries no property list, so "
            "it is not a provisioning profile.", path);
        return false;
    }
    xml::node root;
    std::string err;
    if (!xml::parse(std::string_view(bytes).substr(b, e + 8 - b), root, err)) {
        message = std::format("mcpp.dist.apple: the property list in {} cannot be read: {}", path, err);
        return false;
    }
    const xml::node* dict = top_dict(root);
    std::vector<std::pair<std::string, const xml::node*>> pairs;
    if (!dict || !dict_entries(*dict, pairs, err)) {
        message = std::format("mcpp.dist.apple: the property list in {} has no readable top-level <dict>.", path);
        return false;
    }
    const xml::node* entitlements = nullptr;
    for (auto const& [key, value] : pairs) if (key == "Entitlements" && value->name == "dict") entitlements = value;
    std::vector<std::pair<std::string, const xml::node*>> granted;
    if (!entitlements || !dict_entries(*entitlements, granted, err)) {
        message = std::format("mcpp.dist.apple: the provisioning profile {} states no Entitlements dictionary.", path);
        return false;
    }
    std::string appIdentifier;
    for (auto const& [key, value] : granted)
        if (key == "application-identifier" && value->children.size() == 1 && value->children.front().name.empty())
            appIdentifier = xml::trim_copy(value->children.front().text);
    const auto dot = appIdentifier.find('.');
    const std::string pattern = dot == std::string::npos ? std::string() : appIdentifier.substr(dot + 1);
    const bool covers = !pattern.empty() &&
        (pattern == bundleId || pattern == "*" ||
         (pattern.ends_with(".*") && bundleId.starts_with(pattern.substr(0, pattern.size() - 1))));
    if (!covers) {
        message = std::format(
            "mcpp.dist.apple: the provisioning profile {} is for the application identifier '{}', "
            "which does not cover the bundle identifier '{}'. Set `options::bundle_id` to match, "
            "or use the profile made for it.", path,
            appIdentifier.empty() ? std::string("(none)") : appIdentifier, bundleId);
        return false;
    }
    // A WILDCARD PROFILE GRANTS `<team>.*`, AND THE SIGNATURE STATES THE BUNDLE.
    // Xcode signs with the application identifier the bundle is, which the
    // wildcard covers, rather than with the wildcard itself; the other
    // entitlements are the profile's as it states them.
    xml::node signedWith = *entitlements;
    if (pattern != bundleId) {
        const std::string exact = appIdentifier.substr(0, dot + 1) + bundleId;
        for (std::size_t k = 0; k + 1 < signedWith.children.size(); ++k) {
            auto const& key = signedWith.children[k];
            auto& value = signedWith.children[k + 1];
            if (key.name == "key" && key.children.size() == 1 &&
                xml::trim_copy(key.children.front().text) == "application-identifier" &&
                value.name == "string" && value.children.size() == 1 && value.children.front().name.empty())
                value.children.front().text = exact;
        }
    }
    entitlementsPlist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                        "<plist version=\"1.0\">\n";
    xml::write(signedWith, entitlementsPlist, 0);
    entitlementsPlist += "</plist>\n";
    return true;
}

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    // NOT THIS PASS. Every ordinary build lands here, and the empty
    // `pack_format()` is what says so -- see `generate` for why the
    // DECLARATION must not be gated the same way.
    const std::string requested = mcpp::pack_format();
    if (requested != "app" && requested != "dmg") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not app or dmg", requested);
        return p;
    }
    const bool dmg = requested == "dmg";

    // macOS or iOS only, and this is a refusal rather than a silent skip: a
    // user who typed `--format app` on Linux asked for something that does
    // not exist there, and the engine has already accepted the value
    // because the graph declared it.
    const std::string os = mcpp::target_os();
    const bool isIos = (os == "ios");
    if (os != "macos" && !isIos) {
        return refuse(p, "not a macOS or iOS target", std::format(
            "mcpp.dist.apple: a .app bundle is a macOS or iOS format, and "
            "this build targets '{}'.\n"
            "  use: --format tar, or build for a macOS or iOS target",
            os.empty() ? "unknown" : os));
    }
    if (dmg && isIos) {
        return refuse(p, "a disk image on an iOS target",
            "mcpp.dist.apple: a .dmg is a macOS disk image, and this build "
            "targets iOS. Use --format app for an iOS bundle.");
    }
    // `aarch64-ios-sim` and `aarch64-ios` share one OS and diverge only in
    // `env` (triple.cppm's own words: "the simulator is deliberately not a
    // row [of its own identity]"). Every place this member reads the
    // simulator/device split reads it from here, once.
    const bool isSim = isIos && (std::string(mcpp::target_env()) == "sim");

    // THE STAGED TREE IS OPTIONAL, AND THAT IS THE WHOLE FINDING.
    //
    // This required it, and on macOS it cannot exist: `mcpp pack`'s built-in
    // closure walk refuses a Mach-O program, because it uses
    // `LD_TRACE_LOADED_OBJECTS` and dyld answers that by RUNNING the program.
    // mcpp 2026.9.11.2 made staging a service rather than a precondition, so
    // the dispatch now reaches this member -- and the member then refused for
    // the same underlying reason, one layer up, with
    //
    //   error: no action claimed --format 'app'
    //
    // from the engine, because a member's stderr on a successful build is
    // discarded. A refusal nobody can read.
    //
    // A `.app` needs ONE program, not a tree. `${mcpp.target_file:<name>}` is
    // what names it -- the same placeholder `dist/wix.cppm` uses for exactly
    // this reason, and what section 6 of the design record recommends for a
    // member that packages a named target. The staged tree is still preferred
    // when it exists, because a `--mode vendored` tree carries the program's
    // dependencies beside it and a bundle should keep them; without one the
    // bundle carries the program alone, which is correct for a self-contained
    // Mach-O and is what the platform's own default produces.
    const std::string stage = mcpp::pack_stage_dir();

    const std::string target = target_for(opt);
    if (target.empty()) {
        return refuse(p, "no target", "mcpp.dist.apple: no target to bundle. Set "
                     "`options::target` to the program target's name.");
    }

    const std::string launcher = stage.empty()
        ? std::format("${{mcpp.target_file:{}}}", target)
        : launcher_in(stage, target);
    if (launcher.empty()) {
        return refuse(p, "no launcher in the staged tree", std::format(
            "mcpp.dist.apple: the staged tree at {0} carries no launcher for "
            "target '{1}'.\n"
            "  expected one of: {0}/bin/{1}, {0}/{1}, {0}/run.sh",
            stage, target));
    }
    // CFBundleExecutable is a bare filename (see the note above). With no
    // staged tree the launcher is a PLACEHOLDER the engine expands later, so
    // its basename cannot be taken from the string -- the target's own name is
    // what the expansion will produce.
    const std::string executableName = stage.empty()
        ? target : bundle_executable_name(launcher);

    // `options::icon` IS RESOLVED AGAINST THE MANIFEST DIRECTORY HERE, ONCE,
    // BEFORE ANY VALIDATION OR ACTION ARGV USES IT -- ON BOTH ROWS.
    //
    // This program's own cwd is the manifest directory when mcpp runs it (the
    // usual case a project author sees, and why a bare relative path like
    // `"ios-icons"` validates below without complaint). But the ACTIONS this
    // member declares -- the `ditto` calls in the layout and icon steps -- are
    // graph edges ninja runs later, with the BUILD directory as their cwd, not
    // the manifest directory. A relative `options::icon` therefore reached
    // `ditto` as a path that does not exist from where `ditto` was standing:
    //
    //   ditto ios-icons .../IosAppConsumer.app
    //   ditto: Cannot get the real path for source 'ios-icons'
    //
    // failing inside the graph, on a host with no way to run this member
    // again to explain why. Resolving here, against `mcpp::manifest_dir()`,
    // makes every later use -- the validation immediately below and the
    // `icon.argv` this function builds further down -- see the same absolute
    // path regardless of which directory the thing reading it is standing in.
    // An already-absolute `options::icon` is left alone.
    if (!opt.icon.empty()) {
        std::filesystem::path iconPath(opt.icon);
        if (!iconPath.is_absolute())
            opt.icon = (std::filesystem::path(mcpp::manifest_dir()) / iconPath).string();
    }

    // macOS: `options::icon` is a FILE. iOS: it is a DIRECTORY of flat PNGs
    // (see the `options::icon` comment and the header's icon paragraph) --
    // two different validations of the same field, because the two
    // platforms' icon conventions are not the same shape and this member
    // does not invent a third field to hold the distinction. Both refusals
    // name `options::icon` and the (now-resolved) path, so a project sees
    // exactly what this member read rather than a bare relative name it typed.
    std::vector<std::string> iosIconStems;
    if (!opt.icon.empty()) {
        if (isIos) {
            std::error_code ec;
            if (!std::filesystem::is_directory(opt.icon, ec)) {
                return refuse(p, "icon is not a directory", std::format(
                    "mcpp.dist.apple: `options::icon` ({}) is not a "
                    "directory. Set it to a directory of flat PNGs "
                    "(one per size Apple's Home Screen and Settings need); "
                    "this member lists their stems under `CFBundleIcons` "
                    "and does not generate sizes itself.", opt.icon));
            }
            for (auto const& e : std::filesystem::directory_iterator(opt.icon, ec)) {
                if (ec) break;
                if (e.is_regular_file(ec) && e.path().extension() == ".png")
                    iosIconStems.push_back(e.path().stem().string());
            }
            std::sort(iosIconStems.begin(), iosIconStems.end());
            if (iosIconStems.empty()) {
                return refuse(p, "icon directory carries no PNGs", std::format(
                    "mcpp.dist.apple: `options::icon` ({}) carries no "
                    "*.png files.", opt.icon));
            }
        } else if (!is_file(opt.icon)) {
            return refuse(p, "icon not found", std::format(
                "mcpp.dist.apple: `options::icon` ({}) was not found",
                opt.icon));
        }
    }
    // Resolved against the manifest for the reason `options::icon` is above.
    opt.entitlements         = resolve_path(opt.entitlements);
    opt.info_plist           = resolve_path(opt.info_plist);
    opt.provisioning_profile = resolve_path(opt.provisioning_profile);
    std::string extraEntries;
    std::vector<std::string> replacedKeys;
    if (!opt.info_plist.empty()) {
        if (!is_file(opt.info_plist)) {
            return refuse(p, "info_plist not found", std::format(
                "mcpp.dist.apple: `options::info_plist` ({}) was not found", opt.info_plist));
        }
        mcpp::rerun_if_changed(opt.info_plist.c_str());
        std::string message;
        if (!read_info_plist_fragment(opt.info_plist, extraEntries, replacedKeys, message))
            return refuse(p, "unusable info_plist", message);
    }
    for (std::size_t i = 0; i < opt.omit_keys.size(); ++i) {
        const auto& key = opt.omit_keys[i];
        if (std::ranges::find(derived_plist_keys(), key) != derived_plist_keys().end()) {
            return refuse(p, "omit_keys names a derived key", std::format(
                "mcpp.dist.apple: `options::omit_keys` names {}, which this member derives "
                "from its options and the engine; a derived key cannot be omitted.", key));
        }
        if (std::ranges::find(defaulted_plist_keys(), key) == defaulted_plist_keys().end()) {
            return refuse(p, "omit_keys names a key this member does not default", std::format(
                "mcpp.dist.apple: `options::omit_keys` names {}, which this member does not "
                "write; only UIDeviceFamily, LSRequiresIPhoneOS and NSHighResolutionCapable "
                "can be omitted.", key));
        }
        if (std::find(opt.omit_keys.begin(), opt.omit_keys.begin() + static_cast<std::ptrdiff_t>(i), key)
                != opt.omit_keys.begin() + static_cast<std::ptrdiff_t>(i)) {
            return refuse(p, "omit_keys names a key twice", std::format(
                "mcpp.dist.apple: `options::omit_keys` names {} twice.", key));
        }
        if (std::ranges::find(replacedKeys, key) != replacedKeys.end()) {
            return refuse(p, "omit_keys and info_plist name one key", std::format(
                "mcpp.dist.apple: `options::omit_keys` names {}, and `options::info_plist` ({}) "
                "sets it; state one of the two.", key, opt.info_plist));
        }
    }
    // An omitted default is written the way a replaced one is: not at all. The
    // replaced list is what `plist_document` consults, so the two share it.
    for (auto const& key : opt.omit_keys) replacedKeys.push_back(key);
    if (!opt.entitlements.empty() && !is_file(opt.entitlements)) {
        return refuse(p, "entitlements not found", std::format(
            "mcpp.dist.apple: the entitlements file {} was not found", opt.entitlements));
    }

    const char* pv = mcpp::package_version();
    const std::string version = !opt.version.empty() ? opt.version
                               : (pv && *pv ? std::string(pv) : std::string());
    if (version.empty()) {
        return refuse(p, "no version", "mcpp.dist.apple: no version to state. Set "
                     "`[package] version` or `options::version`.");
    }

    const std::string name       = app_name_for(opt);
    const std::string bundleId   = bundle_id_for(opt);
    const std::string bundlePath = !opt.output.empty() ? opt.output
                                  : (std::filesystem::path(opt.out_dir) / (name + ".app")).string();
    // FLAT ON iOS, `Contents/`-SHAPED ON macOS -- the one difference the
    // header comment names, read here so every step below just names
    // `execDir` / `plistDst` / `resourceDir` and never spells `Contents`
    // itself. On macOS these are exactly the three paths this member wrote
    // before iOS existed.
    const std::string execDir     = isIos ? bundlePath : bundlePath + "/Contents/MacOS";
    const std::string plistDst    = isIos ? bundlePath + "/Info.plist" : bundlePath + "/Contents/Info.plist";
    const std::string resourceDir = isIos ? bundlePath : bundlePath + "/Contents/Resources";

    const std::string macIconName = (!isIos && !opt.icon.empty())
        ? std::filesystem::path(opt.icon).filename().string() : std::string();
    // #622 A11: the engine's own floor first, the option as a fallback --
    // and only on iOS. See the `options::minimum_system_version` comment for
    // why macOS does not take this fallback path.
    const std::string engineMinVersion = mcpp::min_platform_version();
    const std::string minOsVersion = isIos
        ? (!engineMinVersion.empty() ? engineMinVersion : opt.minimum_system_version)
        : opt.minimum_system_version;
    const std::string plistBytes = plist_document(executableName, bundleId, name, version,
                                                  isIos, isSim, minOsVersion,
                                                  extraEntries, replacedKeys,
                                                  macIconName, iosIconStems);

    // THE PROVISIONING PROFILE (0.11.0), checked before any step is planned.
    std::string effectiveEntitlements = opt.entitlements;
    if (!opt.provisioning_profile.empty()) {
        if (!isIos || isSim) {
            return refuse(p, "a provisioning profile off the device row", std::format(
                "mcpp.dist.apple: `options::provisioning_profile` is embedded in an iOS device "
                "bundle (aarch64-ios), and this build targets {}.", isIos ? "the iOS Simulator" : os));
        }
        if (opt.identity.empty()) {
            return refuse(p, "a provisioning profile without an identity",
                "mcpp.dist.apple: `options::provisioning_profile` is set and `options::identity` "
                "is not; a device refuses an ad-hoc signature, so a device bundle is signed with "
                "the identity the profile was made for.");
        }
        if (!is_file(opt.provisioning_profile)) {
            return refuse(p, "provisioning profile not found", std::format(
                "mcpp.dist.apple: `options::provisioning_profile` ({}) was not found",
                opt.provisioning_profile));
        }
        mcpp::rerun_if_changed(opt.provisioning_profile.c_str());
        std::string entitlementsPlist, message;
        if (!read_profile(opt.provisioning_profile, bundleId, entitlementsPlist, message))
            return refuse(p, "unusable provisioning profile", message);
        if (opt.entitlements.empty()) {
            const std::string derived = (std::filesystem::path(opt.out_dir) / (name + "-entitlements.plist")).string();
            if (!write_if_different(derived, entitlementsPlist)) {
                return refuse(p, "cannot write the entitlements", std::format(
                    "mcpp.dist.apple: cannot write {}", derived));
            }
            effectiveEntitlements = derived;
        }
    }
    const std::string plistSrc = (std::filesystem::path(opt.out_dir) / (name + "-Info.plist")).string();
    if (!write_if_different(plistSrc, plistBytes)) {
        return refuse(p, "cannot write Info.plist", std::format("mcpp.dist.apple: cannot write {}", plistSrc));
    }

    p.bundle_path = bundlePath;
    p.appdir      = stage;

    // THE CLOSURE THE ENGINE STAGED (mcpp 2026.9.14.2+). Every `needs` line
    // whose staged path is under `bin/` is a dylib the engine placed beside
    // the program; it goes to the framework directory and not to the
    // resources. An incomplete closure is reported, not refused: a bundle
    // without one of its libraries is the project's to judge, and the message
    // names what is missing.
    std::vector<std::string> frameworks;   // staged paths relative to `bin/`
    if (!stage.empty()) {
        const auto staged = mcpp::plugins::stage::read_manifest(stage);
        for (auto const& n : staged.needs)
            if (n.where.size() > 4 && n.where.starts_with("bin/"))
                frameworks.push_back(n.where.substr(4));
        std::ranges::sort(frameworks);
        frameworks.erase(std::unique(frameworks.begin(), frameworks.end()), frameworks.end());
        if (staged.found && !staged.walked) {
            std::string names;
            for (auto const& n : staged.needs)
                if (n.where == "unresolved") names += (names.empty() ? "" : ", ") + n.name;
            mcpp::warning(std::format(
                "mcpp.dist.apple: the program's closure is incomplete{}, so the "
                "bundle does not carry every library the program loads: {}",
                names.empty() ? std::string() : " (" + names + ")",
                staged.reason.empty() ? std::string("no reason was given") : staged.reason).c_str());
        }
    }
    const std::string frameworksDir = isIos ? bundlePath + "/Frameworks"
                                            : bundlePath + "/Contents/Frameworks";
    // WHO SIGNS WHAT. macOS signs every bundle, ad hoc without an identity;
    // an iOS device row signs only with one; the simulator row never signs.
    const bool signs = !isSim && (!isIos || !opt.identity.empty());
    const std::string signingIdentity = opt.identity.empty() ? std::string("-") : opt.identity;

    // Every action's output that later steps may need to depend on, gathered
    // as they are declared so the final, conditional codesign step can name
    // exactly the ones that ran.
    std::vector<std::string> assembled;

    step info;
    info.id          = "mcpp.dist.apple.info-plist";
    info.role        = "artifact";
    info.description = "INFO.PLIST";
    info.argv         = { "ditto", plistSrc, plistDst };
    info.inputs       = { plistSrc };
    info.outputs      = { plistDst };
    p.steps.push_back(info);
    assembled.push_back(plistDst);

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
    //
    // WITH NO STAGED TREE IT COPIES THE ONE PROGRAM. `${mcpp.stage_dir}`
    // REFUSES when there is no tree -- that is the engine's contract, and a
    // member that names it unconditionally cannot run on a target whose
    // built-in staging is refused. So the source is the tree when there is
    // one and the program when there is not, and the `inputs` entry is the
    // same either way: the program is what this bundle is FOR, and naming it
    // is what orders this action after the link.
    //
    // WITH A STAGED TREE IT COPIES THE PROGRAM, NOT THE TREE. The engine
    // stages a Mach-O program as `bin/<name>` with every deployed file under
    // `bin/<to>/...` beside it (mcpp#630, item 3a: staging precedes the
    // closure walk, and the tree is handed over with `closure = not-walked`
    // in its manifest). Copying that tree whole into `Contents/MacOS/` put
    // the executable at `Contents/MacOS/bin/<name>`, which is not where
    // `CFBundleExecutable` says it is, and the resources beside a program
    // rather than in `Contents/Resources/`, where `NSBundle` looks. So the
    // launcher goes to the executable directory by itself, and the staged
    // `bin/` entries that are not the launcher go to the bundle's resource
    // destination below, at the relative path `mcpp::deploy`'s `to` gave
    // them. `${mcpp.stage_dir}` is still named as an input of the launcher
    // copy, which is what orders every step here after staging and gives
    // the action its dependency on the tree's manifest.
    layout.argv         = stage.empty()
        ? std::vector<std::string>{ "ditto",
              std::format("${{mcpp.target_file:{}}}", target),
              execDir + "/" + executableName }
        : std::vector<std::string>{ "ditto", launcher, execDir + "/" + executableName };
    layout.inputs       = { std::format("${{mcpp.target_file:{}}}", target) };
    if (!stage.empty()) layout.inputs.push_back("${mcpp.stage_dir}");
    layout.outputs      = { execDir + "/" + executableName };
    p.steps.push_back(layout);
    assembled.push_back(execDir + "/" + executableName);

    // THE DEPLOYED FILES, AT THE BUNDLE'S RESOURCE DESTINATION.
    //
    // Read at plan time against the staged tree, the way `dist-apk` reads
    // its assets: the second pass of `mcpp pack --format app` runs this
    // program after the tree is staged, so the entries exist on disk here.
    // One action per top-level entry under `bin/` that is not the launcher,
    // a file or a whole directory, each named by its own path so that the
    // graph carries an edge per deployed thing rather than one edge for
    // "everything". `ditto <dir> <dir>` copies the source's CONTENTS into
    // the destination, which is the flat layout iOS wants at the bundle
    // root and the `Contents/Resources/<to>/` layout macOS wants.
    if (!stage.empty()) {
        const std::filesystem::path stageBin = std::filesystem::path(stage) / "bin";
        const std::filesystem::path launcherPath = std::filesystem::path(launcher);
        std::error_code ec;
        std::vector<std::filesystem::path> entries;
        if (std::filesystem::is_directory(stageBin, ec))
            for (auto const& e : std::filesystem::directory_iterator(stageBin, ec))
                if (!ec && e.path() != launcherPath) entries.push_back(e.path());
        std::ranges::sort(entries);
        for (auto const& e : entries) {
            const std::string rel = e.filename().string();
            // A dylib of the closure is a framework, not a resource. A
            // directory is copied whole, so a dylib staged inside one (an
            // `@executable_path/<dir>/<file>` install name) is copied to the
            // framework directory as well.
            if (std::ranges::find(frameworks, rel) != frameworks.end()) continue;
            step res;
            res.id          = "mcpp.dist.apple.resource." + rel;
            res.role        = "artifact";
            res.description = "APP RESOURCE " + rel;
            res.argv        = { "ditto", e.string(), resourceDir + "/" + rel };
            res.inputs      = { e.string(), "${mcpp.stage_dir}" };
            res.outputs     = { resourceDir + "/" + rel };
            p.steps.push_back(res);
            assembled.push_back(resourceDir + "/" + rel);
        }
    }

    // THE FRAMEWORKS: each copied from the staged tree and, when the bundle is
    // signed, signed before the bundle is -- a bundle signature seals nested
    // code that is already signed.
    if (!frameworks.empty()) {
        const std::string copyFramework = helper_script(opt.out_dir, "copy-framework.sh",
            "#!/bin/sh\n"
            "# mcpp.dist.apple helper. Do not edit.\n"
            "# copy-framework.sh <source> <destination> sign|nosign [identity]\n"
            "set -e\n"
            "src=\"$1\"; dst=\"$2\"; mode=\"$3\"; identity=\"$4\"\n"
            "mkdir -p \"$(dirname \"$dst\")\"\n"
            "ditto \"$src\" \"$dst\"\n"
            "if [ \"$mode\" = sign ]; then\n"
            "    if [ \"$identity\" = - ]; then\n"
            "        codesign --force --sign - \"$dst\"\n"
            "    else\n"
            "        codesign --force --sign \"$identity\" --timestamp \"$dst\"\n"
            "    fi\n"
            "fi\n");
        for (auto const& rel : frameworks) {
            const std::string source = (std::filesystem::path(stage) / "bin" / rel).string();
            const std::string dest   = frameworksDir + "/" + rel;
            step fw;
            fw.id          = "mcpp.dist.apple.framework." + rel;
            fw.role        = "artifact";
            fw.description = "APP FRAMEWORK " + rel;
            fw.argv        = { "/bin/sh", copyFramework, source, dest };
            if (signs) { fw.argv.push_back("sign"); fw.argv.push_back(signingIdentity); }
            else       { fw.argv.push_back("nosign"); }
            fw.inputs      = { source, "${mcpp.stage_dir}" };
            fw.outputs     = { dest };
            p.steps.push_back(fw);
            assembled.push_back(dest);
        }
    }

    if (!opt.icon.empty()) {
        step icon;
        icon.id          = "mcpp.dist.apple.icon";
        icon.role        = "artifact";
        icon.description = "APP ICON";
        if (isIos) {
            // `ditto <dir> <dir>` copies SRC's CONTENTS into DST (see the
            // header comment), so every PNG in `opt.icon` lands directly at
            // the bundle root in one invocation -- the flat layout iOS
            // wants, from a directory a project already has.
            icon.argv   = { "ditto", opt.icon, resourceDir };
            icon.inputs = { opt.icon };
            for (auto const& stem : iosIconStems)
                icon.outputs.push_back(resourceDir + "/" + stem + ".png");
        } else {
            icon.argv    = { "ditto", opt.icon, resourceDir + "/" + macIconName };
            icon.inputs  = { opt.icon };
            icon.outputs = { resourceDir + "/" + macIconName };
        }
        p.steps.push_back(icon);
        for (auto const& o : icon.outputs) assembled.push_back(o);
    }

    if (!opt.provisioning_profile.empty()) {
        step profile;
        profile.id          = "mcpp.dist.apple.provisioning-profile";
        profile.role        = "artifact";
        profile.description = "EMBEDDED.MOBILEPROVISION";
        profile.argv        = { "ditto", opt.provisioning_profile, bundlePath + "/embedded.mobileprovision" };
        profile.inputs      = { opt.provisioning_profile };
        profile.outputs     = { bundlePath + "/embedded.mobileprovision" };
        p.steps.push_back(profile);
        assembled.push_back(profile.outputs.front());
    }

    // CODESIGN IS SKIPPED, NOT ATTEMPTED, ON THE SIMULATOR ROW -- see the
    // header comment's signing paragraph. The warning fires at PLAN time
    // (not only on the success path `submit`'s own floor check uses)
    // because it is a property of the ROW and `options::identity`, decided
    // before any action runs, and `mcpp::warning` is replayed on a cache hit
    // like every other advisory this collection emits -- so it does not
    // vanish the second time a project packs the same simulator build.
    if (!opt.identity.empty() && isIos && isSim) {
        mcpp::warning(
            "mcpp.dist.apple: `options::identity` is ignored on the iOS "
            "Simulator row -- simulator bundles install unsigned, and "
            "codesign cannot produce a device-shaped signature for one. Set "
            "identity for a device build (aarch64-ios) instead.");
    } else if (signs) {
        step sign;
        sign.id          = "mcpp.dist.apple.codesign";
        sign.role        = "artifact";
        sign.description = opt.identity.empty() ? "CODESIGN (AD HOC)" : "CODESIGN";
        // AD HOC TAKES NO TIMESTAMP AND NO RUNTIME OPTIONS: a timestamp is a
        // statement by Apple's service about an identity, and the hardened
        // runtime and entitlements are this option set's, which states them
        // together with an identity.
        sign.argv = { "codesign", "--force", "--sign", signingIdentity };
        if (!opt.identity.empty()) {
            sign.argv.push_back("--timestamp");
            if (opt.hardened_runtime) { sign.argv.push_back("--options"); sign.argv.push_back("runtime"); }
            if (!effectiveEntitlements.empty()) {
                sign.argv.push_back("--entitlements");
                sign.argv.push_back(effectiveEntitlements);
            }
        }
        sign.argv.push_back(bundlePath);
        // Depends on every other step's output, because codesign covers the
        // bundle's content at signing time -- see the header comment.
        sign.inputs = assembled;
        if (!opt.identity.empty() && !effectiveEntitlements.empty())
            sign.inputs.push_back(effectiveEntitlements);
        // codesign has no flag to write a receipt to an arbitrary path, so
        // this names the one file signing a BUNDLE (rather than a flat
        // Mach-O) is documented to write as part of embedding the signature:
        // `Contents/_CodeSignature/CodeResources` on macOS, and the same
        // relative path under the bundle root on the iOS device row's flat
        // layout. This is documented Apple codesign behaviour, not
        // something measured here -- codesign does not run on Linux.
        sign.outputs = { (isIos ? bundlePath : bundlePath + "/Contents") + "/_CodeSignature/CodeResources" };
        p.steps.push_back(sign);
        assembled.push_back(sign.outputs.front());
    }

    // THE BUNDLE DIRECTORY IS THIS PLAN'S OWN TERMINAL ARTIFACT.
    //
    // `mcpp run --format <fmt>` hands the runner the request's TERMINAL
    // ARTIFACT -- the output of an introduced action no other introduced
    // action consumes. Every step above writes a file INSIDE the bundle
    // (`Info.plist`, the executable, an icon, codesign's own stamp), and
    // none of those files is an input of any of the others in a chain that
    // ends in one: `info`, `layout`, `icon` and `sign` are four parallel
    // steps, so without this one the plan has four terminals and `mcpp run`
    // has no single operand to pass on -- exactly the failure `dist-apple`'s
    // iOS row hit (#622: "produced 4 distributables ... needs exactly one").
    //
    // The distributable of `--format app` is the BUNDLE, not any one file in
    // it, so this step's own output is the bundle directory itself, and its
    // inputs are every other step's output declared so far -- codesign's
    // stamp included, when it ran, so the bundle is not the terminal until
    // signing (the last thing that can still fail) has happened. A directory
    // is an acceptable action output (the engine verifies `is_regular_file
    // || is_directory`); `touch` has nothing to write, only a mtime to
    // refresh, and refreshing it is what makes ninja record the edge as run
    // rather than replay a stale one.
    step bundle;
    bundle.id          = "mcpp.dist.apple.bundle";
    bundle.role        = "artifact";
    bundle.description = "APP BUNDLE";
    bundle.argv         = { "/usr/bin/touch", bundlePath };
    bundle.inputs        = assembled;
    bundle.outputs       = { bundlePath };
    p.steps.push_back(bundle);

    // `--format dmg`: the bundle beside an `Applications` link, then the image.
    // The staging directory is emptied and refilled by its own step, so a file
    // removed from the bundle does not survive into the next image; `-ov`
    // replaces an image a previous pack wrote.
    if (dmg) {
        const std::string volume = !opt.volume_name.empty() ? opt.volume_name : name;
        const std::string dmgPath = !opt.dmg.empty() ? opt.dmg
            : (std::filesystem::path(opt.out_dir) / (name + ".dmg")).string();
        const std::string dmgStage = (std::filesystem::path(opt.out_dir) / "dist-apple" / "dmg").string();
        const std::string stageDmg = helper_script(opt.out_dir, "stage-dmg.sh",
            "#!/bin/sh\n"
            "# mcpp.dist.apple helper. Do not edit.\n"
            "# stage-dmg.sh <bundle> <staging directory> <bundle name>\n"
            "set -e\n"
            "bundle=\"$1\"; stage=\"$2\"; name=\"$3\"\n"
            "rm -rf \"$stage\"\n"
            "mkdir -p \"$stage\"\n"
            "ditto \"$bundle\" \"$stage/$name\"\n"
            "ln -s /Applications \"$stage/Applications\"\n");

        step staging;
        staging.id          = "mcpp.dist.apple.dmg-stage";
        staging.role        = "artifact";
        staging.description = "DMG STAGE";
        staging.argv        = { "/bin/sh", stageDmg, bundlePath, dmgStage,
                                std::filesystem::path(bundlePath).filename().string() };
        staging.inputs      = { bundlePath };
        staging.outputs     = { dmgStage };
        p.steps.push_back(staging);

        step image;
        image.id          = "mcpp.dist.apple.dmg";
        image.role        = "artifact";
        image.description = "HDIUTIL CREATE";
        image.argv        = { "hdiutil", "create", "-volname", volume, "-srcfolder", dmgStage,
                              "-format", "UDZO", "-ov", dmgPath };
        image.inputs      = { dmgStage };
        image.outputs     = { dmgPath };
        p.steps.push_back(image);
        p.dmg_path = dmgPath;
    }

    p.applies = true;
    return p;
}

// ─── Submit ────────────────────────────────────────────────────────────────

inline bool submit(const plan& p) {
    if (!p.applies) return true;
    for (auto const& s : p.steps) {
        mcpp::action a;
        a.id          = s.id.c_str();
        a.role        = s.role;
        a.description = s.description.c_str();
        for (auto const& tok : s.argv)    a.arg(tok.c_str());
        for (auto const& in  : s.inputs)  a.input(in.c_str());
        for (auto const& out : s.outputs) a.output(out.c_str());
        a.submit();
    }

    // A FLOOR ON THIS MEMBER'S OWN OUTPUT, ON THE SUCCESS PATH -- AND ONLY
    // WHEN A STAGED TREE IS THE THING BEING MEASURED.
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
    //
    // `p.appdir` IS `pack_stage_dir()`, WHICH THE HEADER COMMENT ALREADY
    // DOCUMENTS AS OPTIONAL. When it is empty -- the common iOS case, since
    // the host running mcpp cannot always execute an iOS Mach-O to walk its
    // closure -- there is no tree to measure at all: the layout step above
    // already took the single-binary path (`${mcpp.target_file:<target>}`),
    // and `recursive_directory_iterator` on an empty path opens nothing,
    // leaving `bytes` at zero. Running the check anyway turned that "no tree
    // was ever asked for" into "the staged tree at  holds only 0 bytes",
    // naming a path that is blank because none exists -- a warning about a
    // defect that was never present. So this floor applies only when a
    // staged tree exists to be measured.
    if (!p.appdir.empty()) {
        std::error_code ec;
        std::uintmax_t bytes = 0;
        for (auto const& e : std::filesystem::recursive_directory_iterator(p.appdir, ec)) {
            if (ec) break;
            if (e.is_regular_file(ec)) bytes += std::filesystem::file_size(e.path(), ec);
        }
        // Loose on purpose, matching `dist/appimage.cppm`'s own bound: this
        // exists to catch "nothing was staged", not to police a size budget.
        // Unlike that member, nothing is written INTO the staged tree here --
        // `Info.plist` and the icon live outside it until the layout and
        // install steps run -- so even a low bound is already suspicious.
        if (bytes < 4u * 1024u) {
            static char msg[512];
            std::snprintf(msg, sizeof msg,
                "mcpp.dist.apple: the staged tree at %s holds only %llu bytes, "
                "which is not a program; the .app will not launch anything",
                p.appdir.c_str(), static_cast<unsigned long long>(bytes));
            mcpp::warning(msg);
        }
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
//
// THE FRAMEWORK RPATH AND THE `app` RUNNER ARE DECLARED ON EVERY PASS, TOO. The
// program is linked before the pass that learns `--format`, so the rpath that
// finds `Contents/Frameworks/` has to reach every link; and `mcpp run --format
// app` looks the runner up in the pass that runs the bundle, which is not a
// packaging pass.
inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("app");
    mcpp::provides_pack_format("dmg");
    const std::string os = mcpp::target_os();
    if (os == "macos") {
        mcpp::link_flag("-Wl,-rpath,@executable_path/../Frameworks");
        mcpp::runner("app", "macapp-run");
    } else if (os == "ios") {
        mcpp::link_flag("-Wl,-rpath,@executable_path/Frameworks");
        // THE DEVICE ROW'S RUNNER (0.11.0). The simulator row keeps the runner
        // its manifest names (`simctl-run`); a device bundle is installed and
        // launched by `devicectl-run` from `xim:apple-device-tools`, declared
        // `when = "run"` on that row alone.
        if (std::string(mcpp::target_env()) != "sim")
            mcpp::runner("app", "devicectl-run");
    }
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::apple
