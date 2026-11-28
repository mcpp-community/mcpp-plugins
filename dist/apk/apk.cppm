// mcpp.dist.apk -- an application target becomes an installable, signed
// `.apk`, or a signed Android App Bundle (`.aab`), with or without a Java host.
//
// WHY THIS IS NEITHER A RULE NOR A TOOL. A rule states how a translation unit
// is compiled by a compiler mcpp does not drive. A tool states something the
// build program needs that no compiler performs, and does it while the
// program runs. This member does neither: it consumes the LINK OUTPUT of a
// `kind = "app"` target -- a shared object on `*-linux-android` (#622 A3) --
// and produces something a user installs. That is the third category
// `dist/appimage.cppm` establishes.
//
// LEVEL 0 AND LEVEL 1. A `NativeActivity` application needs no Java at all
// (`hasCode="false"`): the manifest, the native library and whatever assets
// were deployed are enough. `options::java_sources` adds a second tier --
// `javac`, `d8`, a real `<activity>` -- for a project that hosts its native
// code from a Java/Kotlin activity. Both tiers share every step below except
// the two that compile and dex Java sources, which level 0 never submits.
//
// THE STEPS, under these ids and in this order. `apk:manifest` compiles
// `options::resources` with aapt2 and is submitted only when that option names
// a directory. `apk:link` links the generated manifest, `-I android.jar` and
// the compiled resources into an unsigned, unaligned `base.apk`. Level 1 adds
// `apk:javac` and `apk:d8`. `apk:libs` adds the native libraries, the deployed
// assets and the dex with `jar` (aapt2 has no flag for native libraries),
// `apk:align` runs `zipalign` and `apk:sign` runs `apksigner`.
//
// `--format aab` SHARES EVERYTHING BUT THE LAST THREE STEPS. `aab:link` asks
// aapt2 for the protocol-buffer form bundletool reads (`--proto-format`);
// `aab:module` lays the linked archive out as a bundle's base module
// (`manifest/`, `dex/`, `lib/<abi>/`, `assets/`); `aab:bundle` runs `bundletool
// build-bundle`; and `aab:sign` signs the bundle with `jarsigner`, because an
// App Bundle carries a JAR signature and not an APK signature scheme.
//
// THE NATIVE CLOSURE COMES FROM THE STAGED TREE (mcpp 2026.9.14.2+). The engine
// reads the application object's closure from the files and stages it: the
// object, every library the graph built for it, and the NDK's
// `libc++_shared.so` when the object needs it, under `lib/` for one triple or
// under `lib/<abi>/` for several, with one `needs` line per name in the stage
// manifest (mcpp's docs/50, "The stage manifest"). This member copies those
// files into the package and reads the manifest to refuse a tree it cannot
// trust: an incomplete closure, a `needs` line whose staged file is absent,
// and a manifest with no `needs` line, which only an engine older than
// 2026.9.14.2 writes. Before that release the member walked `DT_NEEDED` itself
// at command time and recorded the walk in a stamp outside the staged tree, so
// a second pack of an unchanged project found the stamp current and produced a
// package without the dependency's library (measured on 0.9.3). The deployed
// files are staged under the tree's `bin/` on this row as on every other, and
// become `assets/`.
//
// EVERY REFUSAL IS ALSO A `mcpp::warning`. A member that refuses submits no
// action and its build program exits 0, and the engine discards the output of
// a build program that succeeded; its own error, "no action claimed --format
// 'apk'", then named no reason (measured on 0.9.3 with two triples).
//
// SIGNING, THROUGH THE PACKAGE MODEL. The default keystore is
// `xim:android-debug-keystore`'s one `debug.keystore`, whose alias
// (`androiddebugkey`) and password (`android`) are the exact values
// Android's own tooling has published and used since the platform's first
// release -- publishing them discloses nothing (see that package's own
// header). A project that names `options::keystore` as a package (never a
// path) is signing with a key it keeps out of every public index; the
// password reaches `apksigner` as `env:<NAME>` and `jarsigner` as
// `-storepass:env <NAME>`, tokens each tool resolves against its own
// environment at run time, so this member never reads the secret.
//
// KOTLIN, R CLASSES AND LIBRARIES (0.11.0). Level 1 compiles Kotlin beside
// Java (`options::kotlin_sources`, with `kotlinc` from `xim:kotlin`, which the
// `dist-apk-kotlin` feature declares), and links the R classes a project's
// code reads its resources through (`aapt2 link --java`). It takes Android
// libraries the way a Gradle project does: from source (`options::libraries`),
// as local archives (`options::aars`, `options::jars`) and as Maven coordinates
// (`options::maven`). A library's resources link under its own package, its
// manifest is merged into the application's (`merge_manifests` states the
// subset and its rules), its classes are dexed with the application's, and an
// AAR's native libraries and assets join the package.
//
// MAVEN, AND A BUILD THAT DOES NOT REACH THE NETWORK. A build must not reach
// the network (`dist/appimage.cppm`), so a Maven graph is resolved into a lock
// file only when the developer asks -- `MCPP_DIST_APK_MAVEN=update` -- and
// fetched into coursier's cache only when asked -- `MCPP_DIST_APK_MAVEN=fetch`.
// An ordinary build reads the locked artifacts from the cache, checks each
// digest against the lock, and refuses naming the command to run when one is
// missing or the lock no longer matches the project's coordinates.
// `xim:coursier`, which the `dist-apk-maven` feature declares, resolves.
//
// UNSIGNED, WHEN ASKED (0.11.0). `options::sign = false` writes the aligned
// package without a signature: the form a release pipeline that signs
// elsewhere wants, and the form a Gradle release build produces without a
// signing configuration.
//
// WHAT THIS MEMBER DOES NOT DO. It does not run `mcpp run --format apk` --
// that is `adb-run`, a session `xim:android-platform-tools` registers, and
// this member's only obligation to it is `assets/mcpp-run.json`, so the
// runner can start the application without `aapt2` on the machine that runs
// it. It does not resolve an emulator or a device; `ANDROID_SERIAL` is the
// caller's configuration, not this member's (rule 4, 2026-09-12 design
// record: "a name is cache-safe, a path is not").

export module mcpp.dist.apk;

import std;
import mcpp;
import mcpp.plugins;
import mcpp.plugins.tool;
import mcpp.tools.steps;

import mcpp.dist.apk.internal.util;
import mcpp.dist.apk.internal.manifest;
import mcpp.dist.apk.internal.libraries;

// `std::format` is header-only and used throughout; `std::println` is not --
// see `rules/spirv.cppm` for the libc++-on-macOS-14 measurement that this
// collection's every member has followed since.

export namespace mcpp::dist::apk {

// ─── Options ───────────────────────────────────────────────────────────────

// An Android library built from source (0.11.0): what a Gradle library module
// contributes to an application. Paths are manifest-relative or absolute.
struct library {
    // The package its R class is generated under. Required when `resources`
    // is set; a library whose code reads no resources may leave it empty.
    std::string package;
    // A `res/`-shaped directory, linked under `package`.
    std::string resources;
    // An `AndroidManifest.xml` whose declarations are merged into the
    // application's.
    std::string manifest;
    // A directory whose files join the package's `assets/`; a file the build
    // program deploys under the same name wins.
    std::string assets;
    std::vector<std::string> java_sources;
    std::vector<std::string> kotlin_sources;
};

struct options {
    // The `app` target this member packages. Empty means the package name.
    std::string target;

    // The manifest's `package` attribute. Empty means `<namespace>.<name>`
    // with every `-` replaced by `_` (Android package identifiers may not
    // carry one), or `app.<name>` when the project declares no namespace.
    std::string application_id;

    // `<application android:label>`. Empty means the package name.
    std::string label;

    // A project file, package-root-relative, that replaces the built-in
    // manifest. Six tokens are substituted verbatim wherever they appear --
    // `{{application_id}}`, `{{label}}`, `{{activity}}`, `{{lib_name}}`,
    // `{{min_sdk}}`, `{{target_sdk}}` -- and everything else in the file is
    // the project's, verbatim: permissions, receivers, meta-data, an icon,
    // an activity-alias, `configChanges`. This member adds nothing to it
    // (design record `2026-09-13-four-upstream-asks-from-a-ui-framework.md`,
    // §3.2).
    //
    // THREE TOKENS ARE REQUIRED, NOT MERELY SUBSTITUTED, because their value
    // is also written to `assets/mcpp-run.json`, which `adb-run` reads to
    // start the application without `aapt2` on the machine that runs it: a
    // template missing `{{application_id}}` or `{{activity}}` is refused at
    // plan time, naming the token and that reader; `{{lib_name}}` joins them
    // at level 0 (`java_sources` empty), because the manifest's own
    // `<meta-data>` element is the only place the loaded library's name is
    // recorded. An unknown `{{...}}` token is refused too, naming it -- see
    // the render function below for why that check belongs to this member
    // and is not proposed for `dist-web`.
    //
    // Empty means the built-in default, which is `manifest_xml`'s own 0.8.0
    // output expressed with these tokens; level 0 with no template renders a
    // manifest byte-identical to 0.8.0's (`tests/apk-consumer`).
    std::string manifest_template;

    // A `res/`-shaped directory `aapt2 compile --dir` compiles and `aapt2
    // link` links as the application's OWN resources -- its launcher icon,
    // colours, strings. Empty means no resources at all -- a legal, common
    // case for a NativeActivity application that draws everything itself.
    std::string resources;

    // LEVEL 1. One or more directories of `.java` sources; one `javac` over
    // every root's files and one `d8` over the result (the member compiles
    // what it is given, and a second root is more of the same input, not a
    // second step). A project with a path-dependency framework that also
    // hosts Java lists that dependency's own directory alongside its own
    // rather than merging the two trees itself. Empty (the default) is
    // level 0: no Java, `hasCode="false"`, `android.app.NativeActivity` as
    // the manifest's activity. A single string is still accepted in a
    // `build.mcpp`: a one-element initialiser list is the same spelling.
    std::vector<std::string> java_sources;

    // LEVEL 1, REQUIRED WHEN `java_sources` IS SET. The fully-qualified
    // activity class the manifest names as `<activity android:name>` and the
    // launcher intent-filter targets. Ignored at level 0, where the activity
    // is always `android.app.NativeActivity`.
    std::string activity;

    // A package name (`ns:name`), never a path (rule 9, 2026-09-12 design
    // record): where the signing keystore comes from. Empty means
    // `xim:android-debug-keystore`'s published debug key, whose alias and
    // password are hardcoded below because they are the documented, public
    // convention and not a secret. A non-empty value is resolved with
    // `xpkg_dir` exactly as this member resolves its own declared payloads;
    // `keystore_alias` and `keystore_password_env` are then both required,
    // because a private key has no convention this member may assume.
    std::string keystore;
    // THE PAYLOADS THIS MEMBER RUNS (0.19.0, mcpp#755): each a root holding
    // that part of the Android pipeline, or the default -- the payload this
    // member declares, or an override of it. One named here is not downloaded.
    // `platform` is a root carrying `android.jar`; `keystore_package` above
    // stays the way a keystore package is named.
    mcpp::plugins::tool::choice build_tools, platform, jdk, bundletool_dir, kotlin, coursier;
    // The runner of the steps of more than one command (0.25.0,
    // `mcpp.tools.steps`; `cmake` until mcpp#955): the `xim:cmake` payload,
    // asked for when a package is planned, or a program named here.
    mcpp::plugins::tool::choice cmake;
    std::string keystore_alias;
    // The NAME of an environment variable `apksigner` itself reads
    // (`--ks-pass env:<NAME>`) -- this member never reads the secret; only
    // the tool does, at run time, in its own process.
    std::string keystore_password_env;

    // `false` writes the aligned package unsigned (0.11.0): no `apksigner` for
    // an APK and no `jarsigner` for an App Bundle, for a pipeline that signs
    // elsewhere. With `keystore` it is refused, because the two say opposite
    // things.
    bool sign = true;

    // `true` packs the native libraries as the engine staged them (0.11.1).
    // By default each is stripped with the build's own `llvm-strip
    // --strip-unneeded`, which keeps the dynamic symbols the loader reads and
    // drops the symbol table and the debug information -- what the Android
    // Gradle plugin does to every library it packages, and what `mcpp pack`
    // reports it did.
    //
    // FROM 0.12.0 THE ENGINE'S DECISION GOVERNS AS WELL. An engine that strips
    // what the graph built (mcpp 2026.9.16.1, #649 E5) tells a build program
    // whether this packaging pass strips (`MCPP_PACK_STRIP`, "1" or "0") and
    // where `--debug-symbols` sends the separated debug information
    // (`MCPP_PACK_DEBUG_SYMBOLS_DIR`), so `mcpp pack --no-strip` and
    // `--debug-symbols <dir>` reach the libraries this member packs as they
    // reach the ones the engine stages. A library is stripped unless either
    // this option or the engine says to keep it. An older engine publishes
    // neither variable, and the member strips as before.
    //
    // THE ENGINE STRIPS FIRST. That engine strips the libraries the graph built
    // when it stages them, before this member reads the tree, so this option
    // keeps this member's own strip off and cannot restore what the engine
    // removed: `mcpp pack --no-strip` is what ships the symbols. With this
    // option set and the engine stripping, the member says so.
    bool keep_debug_symbols = false;

    // LEVEL 1, KOTLIN (0.11.0). One or more directories of `.kt` sources,
    // compiled by `kotlinc` with every Java root as its reference sources,
    // before `javac` compiles the Java against the Kotlin classes; the Kotlin
    // standard library is dexed into the package. The compiler is
    // `xim:kotlin`, which the `dist-apk-kotlin` feature declares -- a project
    // names that feature instead of `dist-apk`. Either this or `java_sources`
    // makes a level-1 package, and both may be set.
    std::vector<std::string> kotlin_sources;

    // ANDROID LIBRARIES FROM SOURCE (0.11.0), highest priority first: a library
    // listed earlier wins a resource both define, as Gradle's dependency order
    // does, and the application's own `resources` win over every library.
    std::vector<library> libraries;

    // LIBRARIES THE GRAPH CONTRIBUTES (0.12.0). With mcpp 2026.9.16.1 the root
    // project's build program receives the resolved graph, and every package in
    // it other than the application that states `[package.metadata.dist-apk]`
    // contributes a library of the shape above (`package`, `resources`,
    // `manifest`, `assets`, `java_sources`, `kotlin_sources`) and archives
    // (`jars`, `aars`), its paths relative to that package's directory. A
    // framework's library therefore reaches every application that depends on
    // it without being listed in the application's build program.
    //
    // RANKED BELOW THE APPLICATION'S OWN ENTRIES, and among themselves a package
    // above the packages it depends on: the graph lists dependencies first, so
    // contributions are taken in reverse. An application's `libraries` still win
    // a resource any contribution defines, and a library wins over the framework
    // beneath it, which is the order a Gradle build gives the same modules.
    //
    // `false` reads no contribution, for an application that lists every
    // library itself. Under an older engine, or in a dependency's own build
    // program, there is no graph, and nothing is contributed either way.
    bool graph_libraries = true;

    // LOCAL ARCHIVES (0.11.0). A JAR joins the classpath and the dex. An AAR
    // contributes its classes, its resources (under the package its manifest
    // names), its manifest, its native libraries and its assets. Archives
    // rank after `libraries`, in the order listed.
    std::vector<std::string> jars;
    std::vector<std::string> aars;

    // MAVEN (0.11.0). `group:artifact:version` coordinates with fixed versions,
    // resolved with their transitive dependencies into `maven_lock`; see the
    // header's Maven paragraph for when the network is used. Empty
    // `maven_repositories` means Google's Maven repository, then Maven
    // Central; an empty `maven_lock` means `maven.lock` beside the manifest;
    // an empty `maven_cache` means `COURSIER_CACHE`, then coursier's own
    // default for this host. Resolved AARs and JARs rank after `aars`.
    std::vector<std::string> maven;
    std::vector<std::string> maven_repositories;
    std::string maven_lock;
    std::string maven_cache;

    // Where the produced file lands. Empty means `<out_dir>/<target>.apk`, or
    // `<out_dir>/<target>.aab` for `--format aab`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ────────────────────────────────────────────────────────────

struct step {
    // Owned strings: a library's resource step is named after its position.
    std::string               id;
    const char*               role;
    std::string               description;
    std::vector<std::string>  argv;
    std::vector<std::string>  inputs;
    std::string               output;
    // Further files the same command writes that a later step reads -- the R
    // classes `aapt2 link --java` generates beside the linked archive.
    std::vector<std::string>  more_outputs;
};

struct plan {
    // False when this build is not `mcpp pack --format apk`, which is every
    // ordinary build. `reason` then says which of the several ways.
    bool              applies = false;
    std::string       reason;
    std::string       output;   // the final, signed .apk
    std::vector<step> steps;
    explicit operator bool() const { return applies; }
};

inline std::string target_for(const options& opt) {
    if (!opt.target.empty()) return opt.target;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string();
}

inline std::string application_id_for(const options& opt, const std::string& target) {
    if (!opt.application_id.empty()) return opt.application_id;
    const char* ns = mcpp::package_namespace();
    const char* nm = mcpp::package_name();
    const std::string name = (nm && *nm) ? std::string(nm) : target;
    if (ns && *ns) return replace_dashes(std::string(ns) + "." + name);
    return replace_dashes("app." + name);
}

inline std::string label_for(const options& opt) {
    if (!opt.label.empty()) return opt.label;
    const char* n = mcpp::package_name();
    return (n && *n) ? std::string(n) : std::string("app");
}

// ─── The staged tree ───────────────────────────────────────────────────────

// Records a refusal three ways, because each reaches a different reader. The
// short `reason` is for a caller that inspects the plan; stderr is for a
// build program that exits non-zero; `mcpp::warning` is for the ordinary case,
// a member that refuses, submits nothing and lets the program exit 0 -- the
// engine discards the output of a build program that succeeded, and then
// reports only "no action claimed --format 'apk'" (measured on 0.9.3 with two
// triples). The warning channel is one line per directive, so line breaks in
// the message are folded into spaces.
// THE ROOT OF ONE OF THIS MEMBER'S PAYLOADS (0.19.0, mcpp#755): what the
// build program named, an override, or the payload. `pending` is set when the
// payload was asked for, and the engine then installs it and runs this program
// again -- the member returns without planning.
inline std::string payload_root(std::string_view package,
                                const mcpp::plugins::tool::choice& named,
                                bool& pending) {
    pending = false;
    mcpp::plugins::tool::spec s{ .who = "mcpp.dist.apk", .package = std::string(package),
                                 .programs = {}, .option = "options::" + std::string(package) };
    if (!named.is_default()) return mcpp::plugins::tool::resolve(s, named).root;
    if (std::string_view(mcpp::xpkg_source("xim", s.package.c_str())) == "pending") {
        (void)mcpp::xpkg_request("xim", s.package.c_str());
        pending = true;
        return {};
    }
    auto f = mcpp::plugins::tool::resolve(s);
    return f.root;
}

inline plan& refuse(plan& p, std::string reason, const std::string& message) {
    // A reason with no message: the payload was asked for and the engine runs
    // this program again (0.19.0), so there is nothing to tell anyone yet.
    if (message.empty()) { p.reason = std::move(reason); return p; }
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

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    const std::string requested = mcpp::pack_format();
    if (requested != "apk" && requested != "aab") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not apk or aab", requested);
        return p;
    }
    const bool bundle = requested == "aab";
    // Step ids name the format, so a build log says which package a step made.
    const auto id = [&](const char* apkId, const char* aabId) { return bundle ? aabId : apkId; };

    if (const std::string env = mcpp::target_env(); env != "android") {
        return refuse(p, "not an Android target", std::format(
            "mcpp.dist.apk: an {} is an Android format, and this build's "
            "target environment is '{}'. Build for a *-linux-android target.",
            bundle ? "AAB" : "APK", env.empty() ? "unknown" : env));
    }

    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty()) {
        return refuse(p, "no staged tree",
            "mcpp.dist.apk: mcpp reported no staged tree. This member needs "
            "mcpp 2026.9.14.2 or newer.");
    }

    const std::string target = target_for(opt);
    if (target.empty()) {
        return refuse(p, "no target",
            "mcpp.dist.apk: no target to package. Set `options::target` to the "
            "app target's name.");
    }

    // ── the native closure, read from the staged tree ─────────────────────
    //
    // The engine states the closure it staged in the stage manifest. A
    // manifest without a single `needs` line comes from an engine that staged
    // the application object alone, and packing that tree would produce a
    // package whose object cannot load, so it is refused rather than packed.
    const auto manifest = mcpp::plugins::stage::read_manifest(stage);
    if (!manifest.found) {
        return refuse(p, "no stage manifest", std::format(
            "mcpp.dist.apk: the staged tree {} has no stage manifest beside it. "
            "This member reads the native closure the engine staged, which needs "
            "mcpp 2026.9.14.2 or newer.", stage));
    }
    if (manifest.needs.empty()) {
        return refuse(p, "stage manifest without needs lines", std::format(
            "mcpp.dist.apk: the stage manifest of {} states no `needs` line, so "
            "the engine that staged it did not read the application's native "
            "closure: its libraries would be missing from the package. This "
            "member needs mcpp 2026.9.14.2 or newer, which stages the closure "
            "under lib/ and states it in the manifest.", stage));
    }
    if (!manifest.walked) {
        std::string names;
        for (auto const& n : manifest.needs)
            if (n.where == "unresolved") names += (names.empty() ? "" : ", ") + n.name;
        return refuse(p, "incomplete native closure", std::format(
            "mcpp.dist.apk: the application's native closure is incomplete ({}), "
            "so a package built from this tree would not load: {}",
            names.empty() ? std::string("no name given") : names,
            manifest.reason.empty() ? std::string("the stage manifest gives no reason")
                                    : manifest.reason));
    }
    for (auto const& n : manifest.needs) {
        if (n.where == "platform") continue;
        if (!is_file((fs::path(stage) / n.where).string())) {
            return refuse(p, "a staged library is missing", std::format(
                "mcpp.dist.apk: the stage manifest names {} for {}, and the "
                "staged tree {} does not carry that file.", n.where, n.name, stage));
        }
    }

    // One flat `lib/` for one triple, `lib/<abi>/` for several.
    const fs::path stageLib = fs::path(stage) / "lib";
    if (!is_dir(stageLib.string())) {
        return refuse(p, "no lib/ in the staged tree", std::format(
            "mcpp.dist.apk: the staged tree {} carries no lib/, where the engine "
            "stages the app target's shared object and its closure.", stage));
    }
    std::vector<native_leg> legs;
    {
        std::vector<std::string> abis;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(stageLib, ec)) {
            if (ec) break;
            if (e.is_directory(ec) && is_android_abi(e.path().filename().string()))
                abis.push_back(e.path().filename().string());
        }
        std::ranges::sort(abis);
        for (auto const& abi : abis)
            legs.push_back({ abi, shared_objects_in(stageLib / abi) });
        if (legs.empty()) {
            const std::string abi = abi_for();
            if (abi.empty()) {
                return refuse(p, "unsupported ABI", std::format(
                    "mcpp.dist.apk: '{}' is not an Android ABI this member knows; "
                    "expected aarch64 or x86_64 (mcpp::target_arch()).",
                    mcpp::target_arch()));
            }
            legs.push_back({ abi, shared_objects_in(stageLib) });
        }
    }
    const std::string objectName = "lib" + target + ".so";
    for (auto const& leg : legs) {
        const bool hasObject = std::ranges::any_of(leg.libraries, [&](const std::string& f) {
            return fs::path(f).filename().string() == objectName;
        });
        if (!hasObject) {
            return refuse(p, "no application object in the staged tree", std::format(
                "mcpp.dist.apk: the staged tree carries no {} for the {} ABI. "
                "`options::target` names '{}', and the engine stages that "
                "target's shared object under lib/.", objectName, leg.abi, target));
        }
    }

    // ── the libraries the graph contributes (0.12.0) ─────────────────────
    //
    // Appended after the application's own `libraries`, `jars` and `aars`,
    // which keeps them highest; the graph's packages are taken requesters first
    // (the reverse of the document's order). Their paths are made absolute
    // against each package's directory here, so the manifest-relative
    // resolution below leaves them as they are. `labels` names each library in
    // a diagnostic: empty for the application's own.
    std::vector<std::string> labels(opt.libraries.size());
    if (opt.graph_libraries) {
        auto graph = mcpp::plugins::graph::read();
        if (!graph) {
            return refuse(p, "unreadable graph document", std::format(
                "mcpp.dist.apk: {}; the libraries its packages contribute cannot be collected. "
                "Set options::graph_libraries = false to list every library in options::libraries.",
                graph.error()));
        }
        for (std::size_t k = graph->packages.size(); k-- > 0;) {
            const auto& pkg = graph->packages[k];
            if (pkg.root) continue;
            const json_value* t = mcpp::plugins::graph::table_of(pkg, "dist-apk");
            if (!t) continue;
            const std::string who = std::format("the [package.metadata.dist-apk] of {}",
                                                mcpp::plugins::graph::label_of(pkg));
            library l;
            std::vector<std::string> jarList, aarList;
            for (auto const& [key, v] : t->members) {
                const bool isString = v.type == json_value::kind::string;
                const bool isList   = v.type == json_value::kind::array
                    && std::ranges::all_of(v.items, [](const json_value& e) {
                           return e.type == json_value::kind::string; });
                const auto wants = [&](bool ok, const char* shape) -> bool {
                    if (!ok) {
                        refuse(p, "malformed graph contribution", std::format(
                            "mcpp.dist.apk: {} states `{}` as something other than {}.",
                            who, key, shape));
                    }
                    return ok;
                };
                const auto strings = [&]() {
                    std::vector<std::string> out;
                    for (auto const& e : v.items) out.push_back(mcpp::plugins::graph::resolve(pkg, e.text));
                    return out;
                };
                if (key == "package") {
                    if (!wants(isString, "a string")) return p;
                    l.package = v.text;
                } else if (key == "resources" || key == "manifest" || key == "assets") {
                    if (!wants(isString, "a path")) return p;
                    const std::string path = mcpp::plugins::graph::resolve(pkg, v.text);
                    (key == "resources" ? l.resources : key == "manifest" ? l.manifest : l.assets) = path;
                } else if (key == "java_sources" || key == "kotlin_sources"
                           || key == "jars" || key == "aars") {
                    if (!wants(isList, "a list of paths")) return p;
                    auto list = strings();
                    if (key == "java_sources")        l.java_sources   = std::move(list);
                    else if (key == "kotlin_sources") l.kotlin_sources = std::move(list);
                    else if (key == "jars")           jarList          = std::move(list);
                    else                              aarList          = std::move(list);
                } else {
                    // Warned, not refused: a package published for a newer
                    // collection may state a key this one does not read, and
                    // refusing it would break every application built with this
                    // version.
                    mcpp::warning(std::format(
                        "mcpp.dist.apk: {} states `{}`, which this version of the member does "
                        "not read; it is ignored.", who, key).c_str());
                }
            }
            const bool isLibrary = !l.package.empty() || !l.resources.empty() || !l.manifest.empty()
                || !l.assets.empty() || !l.java_sources.empty() || !l.kotlin_sources.empty();
            if (isLibrary) {
                opt.libraries.push_back(std::move(l));
                labels.push_back(std::format("the library {} ({})",
                    opt.libraries.back().package.empty() ? mcpp::plugins::graph::label_of(pkg)
                                                         : opt.libraries.back().package, who));
            }
            for (auto& j : jarList) opt.jars.push_back(std::move(j));
            for (auto& a : aarList) opt.aars.push_back(std::move(a));
        }
    }

    // Every path option, resolved against the manifest once (`resolve_path`).
    opt.resources = resolve_path(opt.resources);
    resolve_paths(opt.java_sources);
    resolve_paths(opt.kotlin_sources);
    for (auto& l : opt.libraries) {
        l.resources = resolve_path(l.resources);
        l.manifest  = resolve_path(l.manifest);
        l.assets    = resolve_path(l.assets);
        resolve_paths(l.java_sources);
        resolve_paths(l.kotlin_sources);
    }
    resolve_paths(opt.jars);
    resolve_paths(opt.aars);
    opt.maven_lock  = resolve_path(opt.maven_lock.empty() ? std::string("maven.lock") : opt.maven_lock);
    opt.maven_cache = resolve_path(opt.maven_cache);

    // LEVEL 1 IS THE APPLICATION HOSTING ITS OWN ACTIVITY, which it does when it
    // compiles code of its own, Java or Kotlin. Code a library brings does not
    // change the activity (see `default_manifest_template`).
    const bool appHosted = !opt.java_sources.empty() || !opt.kotlin_sources.empty();
    if (!appHosted && !opt.activity.empty()) {
        // Not fatal -- an explicit activity with no Java host is simply
        // unused -- but the project almost certainly meant `java_sources`
        // too, and level 0's activity is never a name this member reads.
        mcpp::warning("mcpp.dist.apk: options::activity is set with no "
                      "options::java_sources or options::kotlin_sources; level 0 "
                      "always uses android.app.NativeActivity and ignores it");
    }
    if (appHosted && opt.activity.empty()) {
        return refuse(p, "java_sources without activity",
            "mcpp.dist.apk: options::java_sources or options::kotlin_sources is set, "
            "so this is a level-1 (Java-hosted) package, and options::activity is "
            "required: the manifest has no other way to name the launchable activity.");
    }
    const bool nativeActivity = !appHosted;
    if (!opt.sign && !opt.keystore.empty()) {
        return refuse(p, "sign = false with a keystore",
            "mcpp.dist.apk: options::sign is false and options::keystore names a key; "
            "the two say opposite things. Drop one of them.");
    }

    // ── the payloads this member declared ──────────────────────────────
    bool payloadPending = false;
    const std::string buildTools = payload_root("android-build-tools", opt.build_tools, payloadPending);
    if (payloadPending) return refuse(p, "android-build-tools requested", {});
    if (buildTools.empty()) {
        return refuse(p, "android-build-tools not found",
            "mcpp.dist.apk: xim:android-build-tools was not found. Declare it "
            "under [target.'cfg(env = \"android\")'.feature-xlings.dist-apk] in "
            "the consuming project, or install it directly.");
    }
    const std::string platformDir = payload_root("android-platform", opt.platform, payloadPending);
    if (payloadPending) return refuse(p, "android-platform requested", {});
    if (platformDir.empty()) {
        return refuse(p, "android-platform not found",
            "mcpp.dist.apk: xim:android-platform was not found (declare it under "
            "[target.'cfg(env = \"android\")'.feature-xlings.dist-apk]).");
    }
    const std::string androidJar = (fs::path(platformDir) / "android.jar").string();
    if (!is_file(androidJar)) {
        return refuse(p, "android.jar not found", std::format(
            "mcpp.dist.apk: {} does not exist; {} does not look like an Android "
            "platform payload.", androidJar, platformDir));
    }
    const std::string targetSdk = api_level_from_platform_dir(platformDir);
    if (targetSdk.empty()) {
        return refuse(p, "no API level", std::format(
            "mcpp.dist.apk: could not read an API level from the resolved "
            "xim:android-platform directory '{}'.", platformDir));
    }
    const std::string minSdk = mcpp::min_platform_version();
    if (minSdk.empty()) {
        return refuse(p, "no min platform version",
            "mcpp.dist.apk: mcpp::min_platform_version() is empty on an Android "
            "target; this member needs mcpp 2026.9.12.2 or newer.");
    }

    // `aapt2` and `zipalign` are native programs of `xim:android-build-tools`;
    // `apksigner` and `d8` are its jars, `lib/apksigner.jar` and `lib/d8.jar`.
    const std::string aapt2        = host_program(buildTools, "aapt2");
    const std::string zipalign     = host_program(buildTools, "zipalign");
    const std::string apksignerJar = (fs::path(buildTools) / "lib" / "apksigner.jar").string();
    const std::string d8Jar        = (fs::path(buildTools) / "lib" / "d8.jar").string();
    for (auto const& [name, path] : {
             std::pair{"aapt2", aapt2}, std::pair{"zipalign", zipalign},
             std::pair{"apksigner.jar", apksignerJar}, std::pair{"d8.jar", d8Jar}}) {
        if (!is_file(path)) {
            return refuse(p, std::format("{} not found", name), std::format(
                "mcpp.dist.apk: {} was not found at {}; {} does not look like the "
                "xim:android-build-tools payload.", name, path, buildTools));
        }
    }

    // `java`/`javac`/`jar`/`jarsigner`: NONE is part of `xim:android-build-tools`
    // -- all four come from `xim:jdk-temurin` directly, declared on this
    // member's own table rather than assumed reachable through
    // android-build-tools' runtime dependency, which provisions the JDK for
    // ITS OWN wrappers and does not make it visible to a consumer's build
    // program (docs/31, "declare the tool where it will be looked up").
    const std::string jdkHome = payload_root("jdk-temurin", opt.jdk, payloadPending);
    if (payloadPending) return refuse(p, "jdk-temurin requested", {});
    if (jdkHome.empty()) {
        return refuse(p, "jdk-temurin not found",
            "mcpp.dist.apk: xim:jdk-temurin was not found (declare it under "
            "[target.'cfg(env = \"android\")'.feature-xlings.dist-apk]).");
    }
    const fs::path jdkBin = fs::path(jdkHome) / "bin";
    const std::string java      = host_program(jdkBin, "java");
    const std::string javac     = host_program(jdkBin, "javac");
    const std::string jar       = host_program(jdkBin, "jar");
    const std::string jarsigner = host_program(jdkBin, "jarsigner");
    for (auto const& [name, path] : {std::pair{"java", java}, std::pair{"javac", javac},
                                     std::pair{"jar", jar}, std::pair{"jarsigner", jarsigner}}) {
        if (!is_file(path)) {
            return refuse(p, std::format("{} not found", name), std::format(
                "mcpp.dist.apk: {} was not found at {}; {} does not look like a "
                "JDK payload.", name, path, jdkHome));
        }
    }

    // EVERY JAVA TOOL RUNS FROM ITS JAR ON THIS JDK'S `java`, NOT THROUGH A
    // LAUNCHER (0.24.0). The launchers are scripts: POSIX shell scripts, and
    // on Windows `.bat` files, which CreateProcess starts only by handing them
    // to cmd.exe and which mcpp refuses as an action's program when an
    // argument carries a cmd.exe metacharacter. Each command below is what
    // that tool's own launcher runs, with its default heap: `d8` and
    // `apksigner` as build-tools 37.0.0's scripts state them, bundletool and
    // coursier as `java -jar`, which their xim launchers are, and kotlinc as
    // JetBrains' `kotlinc/bin/kotlinc` runs the JVM compiler. One command on
    // every host, so the Linux fixtures run what a Windows host runs.
    const std::vector<std::string> d8 = {
        java, "-Xmx2G", "-cp", d8Jar, "com.android.tools.r8.D8"};
    const std::vector<std::string> apksigner = {java, "-Xmx1024M", "-jar", apksignerJar};

    // `bundletool`: only an App Bundle needs it. `xim:bundletool` keeps
    // upstream's jar at its root as `bundletool-all.jar`.
    std::vector<std::string> bundletool;
    if (bundle) {
        const std::string btDir = payload_root("bundletool", opt.bundletool_dir, payloadPending);
        if (payloadPending) return refuse(p, "bundletool requested", {});
        const std::string btJar = btDir.empty() ? std::string()
                                                : (fs::path(btDir) / "bundletool-all.jar").string();
        bundletool = {java, "-jar", btJar};
        if (!is_file(btJar)) {
            return refuse(p, "bundletool not found", std::format(
                "mcpp.dist.apk: an Android App Bundle is built by bundletool, and "
                "xim:bundletool was not found{}. It is declared under "
                "[target.'cfg(env = \"android\")'.feature-xlings.dist-apk].",
                btDir.empty() ? std::string() : std::format(" at {}", btJar)));
        }
    }

    // ── the libraries: from source, as archives, from Maven (0.11.0) ────
    //
    // Gathered in priority order, highest first: `libraries`, then `jars` and
    // `aars`, then what the Maven lock names. Only resources have a priority
    // to speak of; classes, manifests and assets are unions.
    std::vector<library_input> contributions;
    for (std::size_t i = 0; i < opt.libraries.size(); ++i) {
        const auto& l = opt.libraries[i];
        library_input in;
        in.label   = i < labels.size() && !labels[i].empty()
                   ? labels[i]
                   : l.package.empty() ? std::format("options::libraries[{}]", i)
                                       : std::format("the library {}", l.package);
        in.package = l.package;
        if (!l.resources.empty()) {
            if (!is_dir(l.resources)) {
                return refuse(p, "library resources not found", std::format(
                    "mcpp.dist.apk: {} names resources '{}', which is not a directory.",
                    in.label, l.resources));
            }
            if (l.package.empty()) {
                return refuse(p, "library resources without a package",
                    i < labels.size() && !labels[i].empty()
                    ? std::format(
                        "mcpp.dist.apk: {} has resources and no package; its R class needs "
                        "one. Set `package` in that [package.metadata.dist-apk] table.", labels[i])
                    : std::format(
                        "mcpp.dist.apk: options::libraries[{}] has resources and no package; its "
                        "R class needs one. Set options::libraries[{}].package.", i, i));
            }
            in.resources = l.resources;
        }
        if (!l.manifest.empty()) {
            if (!is_file(l.manifest)) {
                return refuse(p, "library manifest not found", std::format(
                    "mcpp.dist.apk: {} names the manifest '{}', which does not exist.",
                    in.label, l.manifest));
            }
            mcpp::rerun_if_changed(l.manifest.c_str());
            in.manifest = l.manifest;
        }
        if (!l.assets.empty()) {
            if (!is_dir(l.assets)) {
                return refuse(p, "library assets not found", std::format(
                    "mcpp.dist.apk: {} names assets '{}', which is not a directory.",
                    in.label, l.assets));
            }
            in.assets = l.assets;
        }
        in.java   = l.java_sources;
        in.kotlin = l.kotlin_sources;
        contributions.push_back(std::move(in));
    }
    for (auto const& j : opt.jars) {
        if (!is_file(j)) {
            return refuse(p, "jar not found", std::format(
                "mcpp.dist.apk: options::jars names '{}', which does not exist.", j));
        }
        library_input in;
        in.label = j;
        in.jars.push_back(j);
        contributions.push_back(std::move(in));
    }
    std::vector<std::pair<std::string, std::string>> archives;   // label, file
    for (auto const& a : opt.aars) archives.emplace_back(a, a);

    if (!opt.maven.empty()) {
        std::vector<std::string> requested = opt.maven;
        for (auto const& c : requested) {
            if (!plain_coordinate(c)) {
                return refuse(p, "not a plain Maven coordinate", std::format(
                    "mcpp.dist.apk: options::maven names '{}'; write group:artifact:version "
                    "with a fixed version.", c));
            }
        }
        std::ranges::sort(requested);
        requested.erase(std::unique(requested.begin(), requested.end()), requested.end());
        const std::vector<std::string> repositories = opt.maven_repositories.empty()
            ? std::vector<std::string>{"https://maven.google.com", "https://repo1.maven.org/maven2"}
            : opt.maven_repositories;
        const std::string cacheRoot = maven_cache_root(opt.maven_cache);
        mcpp::rerun_if_changed(opt.maven_lock.c_str());
        mcpp::rerun_if_env_changed("MCPP_DIST_APK_MAVEN");
        mcpp::rerun_if_env_changed("COURSIER_CACHE");
        const char* modeEnv = std::getenv("MCPP_DIST_APK_MAVEN");
        const std::string mode = modeEnv ? modeEnv : "";
        if (!mode.empty() && mode != "update" && mode != "fetch") {
            return refuse(p, "unknown MCPP_DIST_APK_MAVEN", std::format(
                "mcpp.dist.apk: MCPP_DIST_APK_MAVEN is '{}'. It is 'update', which resolves "
                "options::maven and rewrites {}, or 'fetch', which downloads what the lock "
                "names into {}.", mode, opt.maven_lock, cacheRoot));
        }
        // `xim:coursier` keeps upstream's `coursier.jar` at its root; its
        // launchers run it with `java -jar`, and so does this.
        const auto coursier = [&]() -> std::string {
            bool pend = false;
            const std::string dir = payload_root("coursier", opt.coursier, pend);
            return dir.empty() ? std::string() : (fs::path(dir) / "coursier.jar").string();
        };
        const auto no_coursier = [&](plan& pl) -> plan& {
            return refuse(pl, "coursier not found", std::format(
                "mcpp.dist.apk: MCPP_DIST_APK_MAVEN={} runs coursier from xim:coursier, which "
                "was not found. Name the dist-apk-maven feature of mcpp:plugins instead of "
                "dist-apk; it declares the resolver.", mode));
        };

        if (mode == "update") {
            const std::string cs = coursier();
            if (!is_file(cs)) return no_coursier(p);
            const std::string report = (fs::path(opt.out_dir) / "dist-apk-maven.json").string();
            std::vector<std::string> argv = {java, "-jar", cs, "fetch", "--cache", cacheRoot};
            for (auto const& r : repositories) { argv.push_back("-r"); argv.push_back(r); }
            argv.push_back("-A");
            argv.push_back("aar,jar");
            argv.push_back("--json-output-file");
            argv.push_back(report);
            for (auto const& c : requested) argv.push_back(c);
            std::string output;
            if (const int rc = run_capture(argv, output); rc != 0) {
                return refuse(p, "Maven resolution failed", std::format(
                    "mcpp.dist.apk: coursier could not resolve {} (exit {}): {}",
                    join(requested, ", "), rc, output));
            }
            json_value doc;
            if (!parse_json(read_file(report), doc) || !doc.get("dependencies")) {
                return refuse(p, "unreadable coursier report", std::format(
                    "mcpp.dist.apk: coursier's report {} cannot be read.", report));
            }
            maven_lock fresh;
            fresh.found        = true;
            fresh.requests     = requested;
            fresh.repositories = repositories;
            // Compared as canonical paths: coursier may report a file through a
            // symlink the option did not name (`/tmp` is `/private/tmp` on macOS).
            const auto canonical = [](const std::string& path) {
                std::error_code ec;
                const auto c = fs::weakly_canonical(path, ec);
                return (ec ? fs::path(path).lexically_normal() : c).generic_string();
            };
            const std::string rootPrefix = canonical(cacheRoot) + "/";
            for (auto const& d : doc.get("dependencies")->items) {
                const json_value* coord = d.get("coord");
                const json_value* file  = d.get("file");
                if (!coord || !file) continue;
                const std::string f = canonical(file->text);
                if (!f.starts_with(rootPrefix)) {
                    return refuse(p, "Maven artifact outside the cache", std::format(
                        "mcpp.dist.apk: coursier placed {} outside the cache {}.", f, cacheRoot));
                }
                maven_artifact a;
                a.coordinate = plain_form(coord->text);
                a.type       = f.ends_with(".aar") ? "aar" : "jar";
                a.path       = f.substr(rootPrefix.size());
                a.sha256     = sha256_file(f);
                fresh.artifacts.push_back(std::move(a));
            }
            std::ranges::sort(fresh.artifacts, {}, &maven_artifact::coordinate);
            if (!write_if_different(opt.maven_lock, render_maven_lock(fresh))) {
                return refuse(p, "cannot write the Maven lock", std::format(
                    "mcpp.dist.apk: cannot write {}.", opt.maven_lock));
            }
            mcpp::warning(std::format(
                "mcpp.dist.apk: MCPP_DIST_APK_MAVEN=update wrote {} ({} artifacts for {}); commit it.",
                opt.maven_lock, fresh.artifacts.size(), join(requested, ", ")).c_str());
        }

        const maven_lock lock = read_maven_lock(opt.maven_lock);
        if (!lock.found) {
            return refuse(p, "no Maven lock", std::format(
                "mcpp.dist.apk: options::maven names {}, and there is no lock at {}. Resolve "
                "once with MCPP_DIST_APK_MAVEN=update set, then commit the lock.",
                join(requested, ", "), opt.maven_lock));
        }
        // STALE MEANS OTHER COORDINATES. The repositories a lock was resolved from
        // are recorded and not compared: every artifact is checked against its
        // digest, so a mirror that serves the same bytes is as good a source, and
        // a developer behind one does not invalidate the lock for everyone else.
        {
            std::vector<std::string> locked = lock.requests;
            std::ranges::sort(locked);
            if (locked != requested) {
                return refuse(p, "stale Maven lock", std::format(
                    "mcpp.dist.apk: {} was resolved for {}, and the project now names {}. "
                    "Resolve again with MCPP_DIST_APK_MAVEN=update set.",
                    opt.maven_lock, join(locked, ", "), join(requested, ", ")));
            }
        }
        const auto not_cached = [&]() {
            std::vector<const maven_artifact*> out;
            for (auto const& a : lock.artifacts)
                if (sha256_file((fs::path(cacheRoot) / a.path).string()) != a.sha256)
                    out.push_back(&a);
            return out;
        };
        auto missing = not_cached();
        if (!missing.empty() && mode == "fetch") {
            const std::string cs = coursier();
            if (!is_file(cs)) return no_coursier(p);
            std::vector<std::string> argv = {java, "-jar", cs, "fetch", "--cache", cacheRoot};
            for (auto const& r : repositories) { argv.push_back("-r"); argv.push_back(r); }
            argv.push_back("-A");
            argv.push_back("aar,jar");
            // `--intransitive <module>` per module (coursier 2.x: the option takes the
            // module, it is not a switch): exactly the locked artifacts, nothing resolved.
            for (auto const* a : missing) { argv.push_back("--intransitive"); argv.push_back(a->coordinate); }
            std::string output;
            if (const int rc = run_capture(argv, output); rc != 0) {
                return refuse(p, "Maven fetch failed", std::format(
                    "mcpp.dist.apk: coursier could not fetch the locked artifacts (exit {}): {}",
                    rc, output));
            }
            missing = not_cached();
            if (!missing.empty()) {
                std::string names;
                for (auto const* a : missing) names += (names.empty() ? "" : ", ") + a->coordinate;
                return refuse(p, "Maven artifact differs from the lock", std::format(
                    "mcpp.dist.apk: after fetching, {} still do not match the digests {} records: "
                    "the repository serves different bytes than it did when the lock was written.",
                    names, opt.maven_lock));
            }
        }
        if (!missing.empty()) {
            std::string names;
            for (auto const* a : missing) names += (names.empty() ? "" : ", ") + a->coordinate;
            return refuse(p, "Maven artifacts not in the cache", std::format(
                "mcpp.dist.apk: the cache {} does not hold {} with the digests {} records. "
                "Fetch them once with MCPP_DIST_APK_MAVEN=fetch set.",
                cacheRoot, names, opt.maven_lock));
        }
        for (auto const& a : lock.artifacts) {
            const std::string f = (fs::path(cacheRoot) / a.path).string();
            if (a.type == "aar") {
                archives.emplace_back(a.coordinate, f);
            } else {
                library_input in;
                in.label = a.coordinate;
                in.jars.push_back(f);
                contributions.push_back(std::move(in));
            }
        }
    }

    for (std::size_t i = 0; i < archives.size(); ++i) {
        const auto& [label, file] = archives[i];
        library_input in;
        in.label = label;
        std::string name = fs::path(file).stem().string();
        for (std::size_t k = 0; k < name.size(); ++k)
            if (!std::isalnum(static_cast<unsigned char>(name[k])) && name[k] != '-' && name[k] != '.') name[k] = '_';
        const fs::path dest = fs::path(opt.out_dir) / "dist-apk-aar" / std::format("{}-{}", i, name);
        std::string message;
        if (!unpack_aar(file, dest, jar, in, message)) return refuse(p, "cannot unpack an AAR", message);
        if (!in.resources.empty() && in.package.empty()) {
            return refuse(p, "AAR without a package", std::format(
                "mcpp.dist.apk: {} carries resources and its manifest names no package, so its "
                "R class has no name.", label));
        }
        contributions.push_back(std::move(in));
    }

    bool needsKotlin = !opt.kotlin_sources.empty();
    bool hasDex = appHosted;
    for (auto const& c : contributions) {
        if (!c.kotlin.empty()) needsKotlin = true;
        if (!c.java.empty() || !c.kotlin.empty() || !c.jars.empty()) hasDex = true;
    }
    std::vector<std::string> kotlinc;
    std::string kotlinStdlib;
    if (needsKotlin) {
        const std::string kdir = payload_root("kotlin", opt.kotlin, payloadPending);
        if (payloadPending) return refuse(p, "kotlin requested", {});
        std::string preloader, compiler;
        if (!kdir.empty()) {
            const fs::path lib = fs::path(kdir) / "kotlinc" / "lib";
            preloader    = (lib / "kotlin-preloader.jar").string();
            compiler     = (lib / "kotlin-compiler.jar").string();
            kotlinStdlib = (lib / "kotlin-stdlib.jar").string();
            // JetBrains' `kotlinc/bin/kotlinc`, with its default heap.
            kotlinc = {java, "-Xmx512M", "-Xms128M", "-cp", preloader,
                       "org.jetbrains.kotlin.preloading.Preloader", "-cp", compiler,
                       "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler"};
        }
        if (!is_file(preloader) || !is_file(compiler) || !is_file(kotlinStdlib)) {
            return refuse(p, "kotlin not found",
                "mcpp.dist.apk: Kotlin sources are compiled by kotlinc from xim:kotlin, which "
                "was not found. Name the dist-apk-kotlin feature of mcpp:plugins instead of "
                "dist-apk; it declares the compiler.");
        }
    }

    // ── signing: a keystore, an alias and a password ───────────────────
    //
    // `apksigner` takes a password as `pass:<value>` or `env:<NAME>`;
    // `jarsigner` takes the same two as `-storepass <value>` or
    // `-storepass:env <NAME>`. Both spellings are derived from one decision.
    std::string keystoreFile, keystoreAlias, keystorePassArg;
    std::vector<std::string> jarsignerPass;
    if (!opt.sign) {
        // Unsigned: no keystore is resolved, so none has to be declared.
    } else if (opt.keystore.empty()) {
        const std::string ksDir = payload_root("android-debug-keystore",
                                               mcpp::plugins::tool::choice{}, payloadPending);
        if (payloadPending) return refuse(p, "android-debug-keystore requested", {});
        if (ksDir.empty()) {
            return refuse(p, "android-debug-keystore not found",
                "mcpp.dist.apk: xim:android-debug-keystore was not found (declare "
                "it under [target.'cfg(env = \"android\")'.feature-xlings.dist-apk], "
                "or set options::keystore).");
        }
        keystoreFile = (fs::path(ksDir) / "debug.keystore").string();
        // The exact, published Android debug-signing convention -- not a
        // secret; see this member's header and `xim:android-debug-keystore`'s
        // own.
        keystoreAlias   = "androiddebugkey";
        keystorePassArg = "pass:android";
        jarsignerPass   = { "-storepass", "android", "-keypass", "android" };
    } else {
        auto colon = opt.keystore.find(':');
        const std::string ns   = colon == std::string::npos ? "" : opt.keystore.substr(0, colon);
        const std::string name = colon == std::string::npos ? opt.keystore : opt.keystore.substr(colon + 1);
        const std::string ksDir = mcpp::xpkg_dir(ns.c_str(), name.c_str());
        if (ksDir.empty()) {
            return refuse(p, "keystore package not found", std::format(
                "mcpp.dist.apk: keystore package '{}' was not found. Declare it "
                "under [target.'cfg(env = \"android\")'.xlings.workspace] in the "
                "consuming project.", opt.keystore));
        }
        std::vector<std::string> candidates;
        { std::error_code ec;
          for (auto& e : fs::directory_iterator(ksDir, ec)) {
              if (ec) break;
              auto ext = e.path().extension();
              if (e.is_regular_file(ec) && (ext == ".keystore" || ext == ".jks"))
                  candidates.push_back(e.path().string());
          }
        }
        if (candidates.size() != 1) {
            return refuse(p, "ambiguous keystore package", std::format(
                "mcpp.dist.apk: expected exactly one *.keystore/*.jks file in {}, "
                "found {}.", ksDir, candidates.size()));
        }
        keystoreFile = candidates.front();
        if (opt.keystore_alias.empty() || opt.keystore_password_env.empty()) {
            return refuse(p, "keystore alias/password not given",
                "mcpp.dist.apk: options::keystore names a package, so "
                "options::keystore_alias and options::keystore_password_env are "
                "both required: a private key has no convention this member may "
                "assume.");
        }
        keystoreAlias   = opt.keystore_alias;
        keystorePassArg = "env:" + opt.keystore_password_env;
        jarsignerPass   = { "-storepass:env", opt.keystore_password_env,
                            "-keypass:env", opt.keystore_password_env };
    }

    // ── the manifest and the run sidecar, written now (plan time) ─────────
    const std::string appId = application_id_for(opt, target);
    const std::string label = label_for(opt);
    const std::string activityName = appHosted ? opt.activity : std::string("android.app.NativeActivity");

    std::string manifestTemplateText;
    if (!opt.manifest_template.empty()) {
        const std::string tplPath =
            (fs::path(mcpp::manifest_dir()) / opt.manifest_template).string();
        if (!is_file(tplPath)) {
            return refuse(p, "manifest template not found", std::format(
                "mcpp.dist.apk: the manifest template {} was not found.", tplPath));
        }
        // The template is declared so an edit to it reaches the graph -- the
        // one thing the ask's workaround (overwriting the manifest after
        // `plan_for()` returns) cannot do, because it depends on this
        // member's own internal path.
        mcpp::rerun_if_changed(tplPath.c_str());
        std::ifstream in(tplPath, std::ios::binary);
        manifestTemplateText.assign((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    } else {
        manifestTemplateText = default_manifest_template(hasDex, nativeActivity);
    }

    const std::string versionName = mcpp::package_version() ? mcpp::package_version() : "";
    const std::string versionCode = version_code_for(versionName);
    std::string manifestBytes, manifestReason, manifestMessage;
    if (!render_manifest(manifestTemplateText, hasDex, nativeActivity, appId, label, activityName,
                         target, minSdk, targetSdk, versionName, versionCode,
                         manifestBytes, manifestReason, manifestMessage)) {
        return refuse(p, manifestReason, manifestMessage);
    }
    {
        std::vector<manifest_source> libraryManifests;
        for (auto const& c : contributions)
            if (!c.manifest.empty()) libraryManifests.push_back({c.label, read_file(c.manifest)});
        if (!libraryManifests.empty() &&
            !merge_manifests(manifestBytes, libraryManifests, appId, minSdk, manifestReason, manifestMessage)) {
            return refuse(p, manifestReason, manifestMessage);
        }
    }

    const fs::path outDir = fs::path(opt.out_dir) / (bundle ? "dist-aab" : "dist-apk");
    const std::string manifestPath = (outDir / "AndroidManifest.xml").string();
    if (!write_if_different(manifestPath, manifestBytes)) {
        return refuse(p, "cannot write AndroidManifest.xml", std::format(
            "mcpp.dist.apk: cannot write {}.", manifestPath));
    }
    const bool inPlaceLibraries = loads_native_libraries_in_place(manifestBytes);

    // THE ENGINE'S STRIP DECISION (0.12.0), read through the environment
    // rather than an `mcpp::` accessor so the member still builds, and still
    // strips, under an engine that predates the accessor. See
    // `options::keep_debug_symbols`.
    const char* engineStrip = std::getenv("MCPP_PACK_STRIP");
    const bool engineKeeps = engineStrip && std::string_view(engineStrip) == "0";
    if (opt.keep_debug_symbols && engineStrip && std::string_view(engineStrip) == "1") {
        mcpp::warning("mcpp.dist.apk: options::keep_debug_symbols is set, and the engine stripped "
                      "the libraries it staged before this member read them; pass "
                      "`mcpp pack --no-strip` to ship their symbols.");
    }
    const char* engineDebugDir = std::getenv("MCPP_PACK_DEBUG_SYMBOLS_DIR");
    const std::string debugDir = engineDebugDir ? engineDebugDir : "";

    // THE BUILD'S OWN llvm-strip (0.11.1): the one beside the compiler mcpp
    // resolved for this row, the NDK's. With a debug-symbols directory,
    // `llvm-objcopy` beside it separates the debug information first, in the
    // order the engine's own strip uses (`src/pack/strip.cppm`): a copy of the
    // debug sections while the library still has them, then the stripped
    // library with a `.gnu_debuglink` naming that copy.
    std::string llvmStrip;
    std::string llvmObjcopy;
    if (!opt.keep_debug_symbols && !engineKeeps) {
        const std::string toolchain = mcpp::toolchain_dir();
        const fs::path candidate = fs::path(toolchain) / "bin" / "llvm-strip";
        if (!toolchain.empty() && is_file(candidate.string())) {
            llvmStrip = candidate.string();
        } else {
            mcpp::warning(std::format(
                "mcpp.dist.apk: no llvm-strip beside the toolchain ({}); the native libraries are packed "
                "with their debug information.", toolchain.empty() ? "none reported" : toolchain).c_str());
        }
        if (!llvmStrip.empty() && !debugDir.empty()) {
            const fs::path objcopy = fs::path(toolchain) / "bin" / "llvm-objcopy";
            if (is_file(objcopy.string())) {
                llvmObjcopy = objcopy.string();
            } else {
                mcpp::warning(std::format(
                    "mcpp.dist.apk: --debug-symbols names {}, and there is no llvm-objcopy beside the "
                    "toolchain ({}) to separate the libraries' debug information; they are stripped "
                    "without it.", debugDir, toolchain).c_str());
            }
        }
    }

    // ── the temporary staging tree: lib/<abi>/, assets/ ─────────────────
    //
    // Populated at plan time from the engine's staged tree, which the second
    // pass of `mcpp pack` runs this program after. It is emptied first, so a
    // library the graph no longer builds does not survive into the next
    // package, and every file copied here is an input of the step that adds
    // it to the archive.
    const fs::path work = outDir / "stage";
    { std::error_code ec; fs::remove_all(work, ec); }
    std::vector<std::string> libInputs;
    // A library reaches `lib/<abi>/` stripped, as an action of its own, or
    // copied as it is when the debug information is kept. A file that is not
    // an ELF object -- an archive can carry anything under `jni/` -- is copied
    // as it is with a warning, as the Android Gradle plugin packs a library it
    // cannot strip.
    const auto is_elf = [](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {};
        return in.read(magic, 4) && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
    };
    // A LIBRARY THE ENGINE STAGED IS ALREADY WHAT THE ENGINE DECIDED. An engine
    // that publishes its strip decision (`MCPP_PACK_STRIP` is set) has stripped
    // those libraries, and separated their debug information into
    // `--debug-symbols`, before this member reads the tree; stripping them again
    // finds no debug information, writes an empty `.debug` file and points the
    // packed copy at it (measured with mcpp 2026.9.16.1). They are packed as
    // staged. The member's own strip applies to what the engine did not stage --
    // an archive's native libraries -- and to every library under an older
    // engine, which decides nothing.
    const bool engineDecided = engineStrip && *engineStrip;
    // ONE DESTINATION HAS ONE CLAIMANT, AND THE FIRST CLAIM WINS. `lib/<abi>/`
    // is flat, so the application's own library and an archive's native library
    // of the same name address one file; so do two archives that carry it. Both
    // loops below walk highest priority first -- the application's staged
    // libraries, then `contributions`, which is ordered application first and
    // each package above the packages it depends on -- so the first claim is the
    // one the priority order names, and a later one is reported rather than
    // written. Writing it would place two steps with one id and one output.
    std::map<std::string, std::string> claimed;   // "<abi>/<leaf>" -> claimant
    const auto place_library = [&](const std::string& so, const std::string& abi,
                                   bool staged, std::string_view claimant) {
        const std::string leafKey = abi + "/" + fs::path(so).filename().string();
        if (auto [it, fresh] = claimed.try_emplace(leafKey, std::string(claimant)); !fresh) {
            mcpp::warning(std::format(
                "mcpp.dist.apk: {} and {} both carry lib/{}; {} is packed, because it comes "
                "first in the priority order, and {} is left out.",
                it->second, claimant, leafKey, it->second, claimant).c_str());
            return;
        }
        const fs::path dst = work / "lib" / abi / fs::path(so).filename();
        if (llvmStrip.empty() || (staged && engineDecided)) {
            collect_tree(so, dst, libInputs);
            return;
        }
        if (!is_elf(so)) {
            mcpp::warning(std::format(
                "mcpp.dist.apk: {} is not an ELF object, so it is packed without being stripped.", so).c_str());
            collect_tree(so, dst, libInputs);
            return;
        }
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        const std::string leaf = fs::path(so).filename().string();
        if (!llvmObjcopy.empty()) {
            // One subdirectory per ABI: a package carries the same library name
            // once for every ABI, and a flat directory would let the last one
            // overwrite the others' debug information.
            const fs::path debugFile = fs::path(debugDir) / abi / (leaf + ".debug");
            fs::create_directories(debugFile.parent_path(), ec);
            step keep;
            keep.id          = std::format("{}:debug:{}:{}", bundle ? "aab" : "apk", abi, leaf);
            keep.role        = "artifact";
            keep.description = "LLVM-OBJCOPY --only-keep-debug " + abi + "/" + leaf;
            keep.output      = debugFile.string();
            keep.argv        = { llvmObjcopy, "--only-keep-debug", so, keep.output };
            keep.inputs      = { so };
            p.steps.push_back(keep);

            step strip;
            strip.id          = std::format("{}:strip:{}:{}", bundle ? "aab" : "apk", abi, leaf);
            strip.role        = "artifact";
            strip.description = "LLVM-OBJCOPY --strip-unneeded " + abi + "/" + leaf;
            strip.output      = dst.string();
            strip.argv        = { llvmObjcopy, "--strip-unneeded",
                                  "--add-gnu-debuglink=" + debugFile.string(), so, strip.output };
            strip.inputs      = { so, keep.output };
            p.steps.push_back(strip);
            libInputs.push_back(strip.output);
            return;
        }
        step strip;
        strip.id          = std::format("{}:strip:{}:{}", bundle ? "aab" : "apk", abi, leaf);
        strip.role        = "artifact";
        strip.description = "LLVM-STRIP " + abi + "/" + leaf;
        strip.output      = dst.string();
        strip.argv        = { llvmStrip, "--strip-unneeded", "-o", strip.output, so };
        strip.inputs      = { so };
        p.steps.push_back(strip);
        libInputs.push_back(strip.output);
    };
    for (auto const& leg : legs)
        for (auto const& so : leg.libraries)
            place_library(so, leg.abi, /*staged=*/true, "the application");

    // An AAR's native libraries, for every ABI this package carries.
    for (auto const& c : contributions) {
        if (c.jni.empty()) continue;
        for (auto const& leg : legs) {
            const fs::path abiDir = fs::path(c.jni) / leg.abi;
            if (!is_dir(abiDir.string())) {
                mcpp::warning(std::format(
                    "mcpp.dist.apk: {} carries native libraries, and none for {}; a class that "
                    "loads them fails on that ABI.", c.label, leg.abi).c_str());
                continue;
            }
            for (auto const& so : shared_objects_in(abiDir))
                place_library(so, leg.abi, /*staged=*/false, c.label);
        }
    }

    const fs::path assetsDir = work / "assets";
    std::vector<std::string> assetInputs;
    // Library and archive assets first, so a file the build program deploys
    // under the same name replaces theirs. `collect_tree` overwrites, so the
    // last write wins; `contributions` is highest priority first, and is walked
    // backwards here for the same reason the resources below are -- a package
    // above the packages it depends on decides the file they share.
    for (std::size_t k = contributions.size(); k-- > 0;)
        if (!contributions[k].assets.empty())
            collect_tree(contributions[k].assets, assetsDir, assetInputs);
    { // every deploy'd file, `<stage>/bin/<rel>` -> `assets/<rel>`: the engine
      // stages `mcpp::deploy`'s destinations under `bin/` on this row as on
      // every other.
      const fs::path stageBin = fs::path(stage) / "bin";
      std::error_code ec;
      if (fs::is_directory(stageBin, ec)) {
          for (auto& e : fs::recursive_directory_iterator(stageBin, ec)) {
              if (ec) break;
              if (!e.is_regular_file(ec)) continue;
              auto rel = fs::relative(e.path(), stageBin, ec);
              collect_tree(e.path(), assetsDir / rel, assetInputs);
          }
      }
    }
    const std::string runJsonPath = (assetsDir / "mcpp-run.json").string();
    if (!write_if_different(runJsonPath, run_json(appId, activityName))) {
        return refuse(p, "cannot write mcpp-run.json", std::format(
            "mcpp.dist.apk: cannot write {}.", runJsonPath));
    }
    assetInputs.push_back(runJsonPath);

    // ── the steps of more than one command (0.25.0) ────────────────────
    //
    // Four steps are more than one command, or need a file set known only when
    // they run: javac and kotlinc write classes no plan can list, so a stamp
    // stands for them; d8 takes no directory, so the classes are listed when it
    // runs; the linked archive gains the native libraries, the assets and every
    // `classes*.dex` d8 wrote; an App Bundle's base module is the linked archive
    // laid out as bundletool reads it. Each is a step program
    // (`mcpp.tools.steps`): this member describes it, and its runner runs it,
    // so no step needs a shell on any host and the runner and the written
    // program are inputs of the step.
    namespace steps = mcpp::tools::steps;
    // Resolved here, once the project's own inputs have been checked, so a
    // refusal about them is not hidden behind a tool that is still to come.
    const auto runnerSpec = steps::runner_spec("mcpp.dist.apk");
    const auto runnerTool = mcpp::plugins::tool::resolve(runnerSpec, opt.cmake);
    if (runnerTool.pending()) return refuse(p, "cmake requested", {});
    if (!runnerTool) {
        return refuse(p, "cmake not found",
            mcpp::plugins::tool::describe_missing(runnerSpec, runnerTool)
            + "\n  So this plan packs nothing.");
    }
    const std::string runner = runnerTool.program;
    const auto attach = [&](step& s, std::string_view name, const steps::program& program) {
        const auto how = steps::command(runner, outDir / "steps" / std::string(name), "mcpp.dist.apk", program);
        s.argv = how.argv;
        s.inputs.insert(s.inputs.end(), how.inputs.begin(), how.inputs.end());
    };
    const auto joined = [](std::vector<std::string> head, const std::vector<std::string>& tail) {
        head.insert(head.end(), tail.begin(), tail.end());
        return head;
    };

    // ── the pipeline ────────────────────────────────────────────────────
    std::vector<std::string> assembled; // every step's own output, for later inputs

    if (!opt.resources.empty()) {
        if (!is_dir(opt.resources)) {
            return refuse(p, "resources directory not found", std::format(
                "mcpp.dist.apk: options::resources '{}' is not a directory.",
                opt.resources));
        }
        step compile;
        compile.id = id("apk:manifest", "aab:manifest");
        compile.role = "artifact";
        compile.description = "AAPT2 COMPILE";
        compile.output = (outDir / "compiled.zip").string();
        compile.argv = { aapt2, "compile", "--dir", opt.resources, "-o", compile.output };
        compile.inputs = { opt.resources };
        p.steps.push_back(compile);
        assembled.push_back(compile.output);
    }
    const std::string appUnit = assembled.empty() ? std::string() : assembled.back();

    // THE LIBRARIES' RESOURCES, EACH COMPILED ON ITS OWN, LOWEST PRIORITY FIRST.
    // `contributions` is highest first, so it is walked backwards.
    std::vector<std::string> libraryUnits;
    for (std::size_t k = contributions.size(); k-- > 0;) {
        const auto& c = contributions[k];
        if (c.resources.empty()) continue;
        step unit;
        unit.id          = std::format("{}:res:{}", bundle ? "aab" : "apk", k);
        unit.role        = "artifact";
        unit.description = "AAPT2 COMPILE " + c.label;
        unit.output      = (outDir / std::format("compiled-library-{}.zip", k)).string();
        unit.argv        = { aapt2, "compile", "--dir", c.resources, "-o", unit.output };
        unit.inputs      = { c.resources };
        p.steps.push_back(unit);
        libraryUnits.push_back(unit.output);
    }

    step link;
    link.id = id("apk:link", "aab:link");
    link.role = "artifact";
    link.description = bundle ? "AAPT2 LINK (PROTO)" : "AAPT2 LINK";
    link.output = (outDir / (bundle ? "base-proto.zip" : "base.apk")).string();
    link.argv = { aapt2, "link", "-I", androidJar, "--manifest", manifestPath,
                 "--min-sdk-version", minSdk, "--target-sdk-version", targetSdk };
    // The protocol-buffer form is the one bundletool reads; an APK is the
    // binary form a device installs. A bundle also needs a version code, which
    // bundletool refuses the base module without ("Version code not found in
    // manifest", measured with 1.18.3) and the built-in manifest does not
    // write: aapt2 injects the package version's into a manifest that states
    // none, and leaves a template's own value alone.
    if (bundle) {
        link.argv.push_back("--proto-format");
        link.argv.push_back("--version-code"); link.argv.push_back(versionCode);
        if (!versionName.empty()) {
            link.argv.push_back("--version-name"); link.argv.push_back(versionName);
        }
    }
    // POSITIONAL, NOT `-R`. `-R` is aapt2's overlay: a compilation unit whose
    // resources must each override one the base already defines, and a
    // project's `res/` IS the base -- linked with `-R`, its first colour
    // failed as `color/ic_launcher_background does not override an existing
    // resource` (0.9.0, measured by HuxerUI's Android row). A positional
    // unit is the base.
    //
    // WITH LIBRARIES, THE LOWEST-PRIORITY LIBRARY IS THE BASE AND EVERYTHING
    // ABOVE IT OVERLAYS IT, the application last, with `--auto-add-overlay` so
    // that an overlay may also define resources the base does not have -- the
    // property 0.9.0's overlay lacked, and why the application alone is still
    // linked positionally.
    if (libraryUnits.empty()) {
        if (!appUnit.empty()) link.argv.push_back(appUnit);
    } else {
        link.argv.push_back(libraryUnits.front());
        for (std::size_t k = 1; k < libraryUnits.size(); ++k) {
            link.argv.push_back("-R");
            link.argv.push_back(libraryUnits[k]);
        }
        if (!appUnit.empty()) {
            link.argv.push_back("-R");
            link.argv.push_back(appUnit);
        }
        link.argv.push_back("--auto-add-overlay");
    }
    // THE R CLASSES (0.11.0), whenever classes are compiled: the application's
    // under its package, and the same ids under every library package, which
    // is how a library's code reaches the resources it brought.
    const fs::path genDir = outDir / "gen";
    std::vector<std::string> rJava;
    if (hasDex) {
        std::vector<std::string> extraPackages;
        for (auto const& c : contributions)
            if (!c.package.empty() && c.package != appId &&
                std::ranges::find(extraPackages, c.package) == extraPackages.end())
                extraPackages.push_back(c.package);
        link.argv.push_back("--java");
        link.argv.push_back(genDir.string());
        if (!extraPackages.empty()) {
            link.argv.push_back("--extra-packages");
            link.argv.push_back(join(extraPackages, ":"));
        }
        const auto rPath = [&](const std::string& package) {
            return (genDir / replace_all_copy(package, ".", "/") / "R.java").string();
        };
        rJava.push_back(rPath(appId));
        for (auto const& e : extraPackages) rJava.push_back(rPath(e));
        link.more_outputs = rJava;
    }
    link.argv.push_back("-o"); link.argv.push_back(link.output);
    link.inputs = { manifestPath, androidJar };
    if (!appUnit.empty()) link.inputs.push_back(appUnit);
    for (auto const& u : libraryUnits) link.inputs.push_back(u);
    p.steps.push_back(link);

    std::vector<std::string> javaOutputs; // classes.dex, when there are classes
    if (hasDex) {
        // THE SOURCE ROOTS: the application's and every library's, each refused
        // when it is not a directory or holds no source of its language.
        //
        // THE RE-RUN QUESTION (design record §3.3). `glob_fingerprint` walks the
        // PACKAGE ROOT and matches paths relative to it; a root outside that
        // walk (a dependency's unpack directory) matches nothing, and the
        // fingerprint would be the same as "no files". So a project root (under
        // `mcpp::manifest_dir()`) is declared with the glob; a dependency root is
        // not -- its file set changes only with the dependency's version, already
        // in the build's fingerprint, and each of its files is an input of the
        // compile below. THE PATTERN IS MANIFEST-RELATIVE (`root_in_project`'s
        // return value), NOT `root` ITSELF: an absolute pattern never matches --
        // see `root_in_project`'s own header for the measurement.
        std::vector<std::string> javaFiles, kotlinFiles;
        struct root_list { const std::vector<std::string>* roots; std::string option; };
        std::vector<root_list> javaRoots   = { {&opt.java_sources, "options::java_sources"} };
        std::vector<root_list> kotlinRoots = { {&opt.kotlin_sources, "options::kotlin_sources"} };
        for (auto const& c : contributions) {
            if (!c.java.empty())   javaRoots.push_back({&c.java, c.label + " java_sources"});
            if (!c.kotlin.empty()) kotlinRoots.push_back({&c.kotlin, c.label + " kotlin_sources"});
        }
        const auto gather = [&](const std::vector<root_list>& lists, const char* ext,
                                std::vector<std::string>& into, std::string& reason,
                                std::string& message) -> bool {
            for (auto const& list : lists) {
                for (auto const& root : *list.roots) {
                    if (!is_dir(root)) {
                        reason  = std::format("{} directory not found", ext[1] == 'j' ? "java_sources" : "kotlin_sources");
                        message = std::format("mcpp.dist.apk: {} root '{}' is not a directory.", list.option, root);
                        return false;
                    }
                    const auto found = files_with_extension(root, ext);
                    if (found.empty()) {
                        reason  = std::format("no {} sources", ext);
                        message = std::format("mcpp.dist.apk: {} root '{}' carries no {} file.", list.option, root, ext);
                        return false;
                    }
                    into.insert(into.end(), found.begin(), found.end());
                    if (auto rel = root_in_project(root))
                        mcpp::rerun_if_changed_glob((*rel + "/**/*" + ext).c_str());
                }
            }
            return true;
        };
        {
            std::string reason, message;
            if (!gather(javaRoots, ".java", javaFiles, reason, message) ||
                !gather(kotlinRoots, ".kt", kotlinFiles, reason, message))
                return refuse(p, reason, message);
        }
        for (auto const& r : rJava) javaFiles.push_back(r);

        std::vector<std::string> classpath = { androidJar };
        std::vector<std::string> archivesToDex;
        for (auto const& c : contributions)
            for (auto const& j : c.jars) { classpath.push_back(j); archivesToDex.push_back(j); }

        const std::string classesDir       = (outDir / "classes").string();
        const std::string kotlinClassesDir = (outDir / "classes-kotlin").string();
        std::string kotlinStamp;
        if (!kotlinFiles.empty()) {
            // KOTLIN FIRST, WITH THE JAVA AS REFERENCE SOURCES: `kotlinc` reads
            // the `.java` files to resolve what the Kotlin names, and writes
            // classes for the Kotlin only; `javac` then compiles the Java against
            // them. The R classes are among the Java, generated by the link.
            step kt;
            kt.id          = id("apk:kotlinc", "aab:kotlinc");
            kt.role        = "artifact";
            kt.description = "KOTLINC";
            kt.output      = kotlinClassesDir + "/.stamp";
            std::vector<std::string> command = joined(kotlinc, {
                "-jvm-target", "17", "-no-reflect",
                "-classpath", join(classpath, class_path_separator()), "-d", kotlinClassesDir });
            command.insert(command.end(), kotlinFiles.begin(), kotlinFiles.end());
            command.insert(command.end(), javaFiles.begin(), javaFiles.end());
            kt.inputs = kotlinFiles;
            for (auto const& f : javaFiles) kt.inputs.push_back(f);
            for (auto const& c : classpath) kt.inputs.push_back(c);
            attach(kt, id("apk-kotlinc", "aab-kotlinc"),
                   steps::program{}.remove(kotlinClassesDir).run(command).touch(kt.output));
            p.steps.push_back(kt);
            kotlinStamp = kt.output;
        }

        std::vector<std::string> javacClasspath = classpath;
        if (!kotlinFiles.empty()) javacClasspath.push_back(kotlinClassesDir);
        step javacStep;
        javacStep.id = id("apk:javac", "aab:javac");
        javacStep.role = "artifact";
        javacStep.description = "JAVAC";
        javacStep.output = classesDir + "/.stamp";
        std::vector<std::string> javacCommand = { javac, "-source", "17", "-target", "17",
            "-cp", join(javacClasspath, class_path_separator()), "-d", classesDir };
        javacCommand.insert(javacCommand.end(), javaFiles.begin(), javaFiles.end());
        javacStep.inputs = javaFiles;
        for (auto const& c : classpath) javacStep.inputs.push_back(c);
        if (!kotlinStamp.empty()) javacStep.inputs.push_back(kotlinStamp);
        // THE DIRECTORY IS EMPTIED FIRST: a compiler adds classes and never
        // removes one, so a source that left the package -- a library dropped
        // from the graph, a file deleted -- would otherwise keep its class in
        // the directory d8 lists, and in the package.
        attach(javacStep, id("apk-javac", "aab-javac"),
               steps::program{}.remove(classesDir).run(javacCommand).touch(javacStep.output));
        p.steps.push_back(javacStep);

        const std::string dexDir = (outDir / "dex").string();
        step d8Step;
        d8Step.id = id("apk:d8", "aab:d8");
        d8Step.role = "artifact";
        d8Step.description = "D8";
        d8Step.output = dexDir + "/classes.dex";
        // `d8` DOES NOT ACCEPT A DIRECTORY, MEASURED (2026-09-12, d8 9.2.4,
        // `xim:android-build-tools` 37.0.0): `d8 --output <dir> <classesDir>`
        // fails in the tool itself, `Unsupported source file type`, one frame
        // into `BaseCommand$Builder.addProgramFiles`. javac's own output set is
        // not knowable at plan time, so a directory of classes (javac's,
        // kotlinc's) is listed when the step runs, and an archive (a JAR, an
        // AAR's classes.jar, the Kotlin stdlib) is passed as it is. Earlier dex
        // files are removed first, so a package that shrinks below the multidex
        // limit does not keep a stale `classes2.dex`.
        std::vector<std::string> classDirs = { classesDir };
        if (!kotlinFiles.empty()) classDirs.push_back(kotlinClassesDir);
        std::vector<std::string> d8Archives = archivesToDex;
        if (!kotlinFiles.empty()) d8Archives.push_back(kotlinStdlib);
        steps::program d8Program;
        for (auto const& d : classDirs) d8Program.glob("inputs", d + "/*.class", { .recursive = true });
        d8Program.append("inputs", d8Archives)
                 .require("inputs", "mcpp.dist.apk: no .class file and no archive to dex")
                 .remove_glob(dexDir + "/classes*.dex")
                 .run(joined(d8, { "--min-api", minSdk, "--lib", androidJar, "--output", dexDir }),
                      { .append_list = "inputs" });
        d8Step.inputs = { javacStep.output, androidJar };
        if (!kotlinStamp.empty()) { d8Step.inputs.push_back(kotlinStamp); d8Step.inputs.push_back(kotlinStdlib); }
        for (auto const& a : archivesToDex) d8Step.inputs.push_back(a);
        attach(d8Step, id("apk-d8", "aab-d8"), d8Program);
        p.steps.push_back(d8Step);
        javaOutputs.push_back(d8Step.output);
    }

    if (bundle) {
        // ── an Android App Bundle: the base module, the bundle, the signature ─
        step baseModule;
        baseModule.id = "aab:module";
        baseModule.role = "artifact";
        baseModule.description = "AAB BASE MODULE";
        baseModule.output = (outDir / "base.zip").string();
        // THE BASE MODULE OF AN APP BUNDLE. `aapt2 link --proto-format` writes
        // the manifest at the archive's root beside `resources.pb` and `res/`;
        // bundletool reads a module whose manifest is under `manifest/`, whose
        // dex is under `dex/` and whose native libraries and assets are under
        // `lib/` and `assets/`. This lays the linked archive out that way.
        const std::string moduleDir = (outDir / "module").generic_string();
        const std::string workDir = work.generic_string();
        steps::program moduleProgram;
        moduleProgram.remove(moduleDir)
                     .remove(baseModule.output)
                     .mkdir(moduleDir + "/manifest")
                     .run({ jar, "xf", link.output }, { .working_directory = moduleDir })
                     .rename(moduleDir + "/AndroidManifest.xml", moduleDir + "/manifest/AndroidManifest.xml")
                     .copy_tree(workDir + "/lib", moduleDir + "/lib", true)
                     .copy_tree(workDir + "/assets", moduleDir + "/assets", true);
        if (!javaOutputs.empty()) {
            moduleProgram.copy_glob(fs::path(javaOutputs.front()).parent_path().generic_string() + "/classes*.dex",
                                    moduleDir + "/dex");
        }
        moduleProgram.run({ jar, "cfM", baseModule.output, "-C", moduleDir, "." });
        baseModule.inputs = { link.output };
        for (auto const& f : libInputs)   baseModule.inputs.push_back(f);
        for (auto const& f : assetInputs) baseModule.inputs.push_back(f);
        for (auto const& f : javaOutputs) baseModule.inputs.push_back(f);
        attach(baseModule, "aab-module", moduleProgram);
        p.steps.push_back(baseModule);

        p.output = !opt.output.empty() ? opt.output
                 : (fs::path(opt.out_dir) / (target + ".aab")).string();
        step build;
        build.id = "aab:bundle";
        build.role = "artifact";
        build.description = "BUNDLETOOL BUILD-BUNDLE";
        build.output = opt.sign ? (outDir / "unsigned.aab").string() : p.output;
        build.argv = bundletool;
        build.argv.insert(build.argv.end(), { "build-bundle", "--modules=" + baseModule.output,
                                              "--output=" + build.output, "--overwrite" });
        build.inputs = { baseModule.output };
        p.steps.push_back(build);
        if (!opt.sign) {
            p.applies = true;
            return p;
        }

        step sign;
        sign.id = "aab:sign";
        sign.role = "artifact";
        sign.description = "JARSIGNER";
        sign.output = p.output;
        sign.argv = { jarsigner, "-keystore", keystoreFile };
        for (auto const& a : jarsignerPass) sign.argv.push_back(a);
        sign.argv.push_back("-signedjar"); sign.argv.push_back(sign.output);
        sign.argv.push_back(build.output);
        sign.argv.push_back(keystoreAlias);
        sign.inputs = { build.output, keystoreFile };
        p.steps.push_back(sign);

        p.applies = true;
        return p;
    }

    // ── native libraries, assets and dex join the archive ──────────────
    step libs;
    libs.id = "apk:libs";
    libs.role = "artifact";
    libs.description = "APK LIBS+ASSETS";
    libs.output = (outDir / "withlibs.apk").string();
    // The linked archive, copied, gains the staged `lib/` and `assets/` and
    // the dex through `jar`: aapt2 has no flag for native libraries. A library
    // loaded in place is added uncompressed (`--no-compress`). Every
    // `classes*.dex` d8 wrote is added: a package whose classes pass the
    // 64K-method limit of one dex gets `classes2.dex` and on, and which of
    // them exist is known only when d8 has run.
    steps::program libsProgram;
    libsProgram.copy_file(link.output, libs.output);
    if (inPlaceLibraries) {
        libsProgram.run({ jar, "--update", "--no-compress", "--file", libs.output, "-C", work.string(), "lib" })
                   .run({ jar, "uf", libs.output, "-C", work.string(), "assets" });
    } else {
        libsProgram.run({ jar, "uf", libs.output, "-C", work.string(), "lib", "-C", work.string(), "assets" });
    }
    if (!javaOutputs.empty()) {
        const std::string dexDir = (outDir / "dex").generic_string();
        libsProgram.glob("dex", dexDir + "/classes*.dex", { .relative_to = dexDir })
                   .require("dex", "mcpp.dist.apk: d8 wrote no classes*.dex in " + dexDir)
                   .run({ jar, "uf", libs.output }, { .working_directory = dexDir, .append_list = "dex" });
    }
    libs.inputs = { link.output };
    for (auto const& f : libInputs)   libs.inputs.push_back(f);
    for (auto const& f : assetInputs) libs.inputs.push_back(f);
    for (auto const& f : javaOutputs) libs.inputs.push_back(f);
    attach(libs, "apk-libs", libsProgram);
    p.steps.push_back(libs);

    step align;
    align.id = "apk:align";
    align.role = "artifact";
    align.description = "ZIPALIGN";
    p.output = !opt.output.empty() ? opt.output
             : (fs::path(opt.out_dir) / (target + ".apk")).string();
    // Unsigned, the aligned archive is the package.
    align.output = opt.sign ? (outDir / "aligned.apk").string() : p.output;
    // `-f`: OVERWRITE. Measured -- `zipalign` refuses by default when its own
    // output already exists ("Output file '...' exists"), which every
    // rebuild after the first hits, because ninja does not delete a stale
    // output before an edge reruns it.
    // A library loaded in place is aligned to a 16 KB page, which a 4 KB
    // device reads as well; `apksigner sign` aligns a stored library to the
    // same 16 KB by default, so a signed package keeps it.
    align.argv = inPlaceLibraries
        ? std::vector<std::string>{ zipalign, "-f", "-P", "16", "4", libs.output, align.output }
        : std::vector<std::string>{ zipalign, "-f", "-p", "4", libs.output, align.output };
    align.inputs = { libs.output };
    p.steps.push_back(align);
    if (!opt.sign) {
        p.applies = true;
        return p;
    }

    step sign;
    sign.id = "apk:sign";
    sign.role = "artifact";
    sign.description = "APKSIGNER";
    sign.output = p.output;
    sign.argv = joined(apksigner, { "sign", "--ks", keystoreFile,
                                    "--ks-pass", keystorePassArg,
                                    "--ks-key-alias", keystoreAlias,
                                    "--key-pass", keystorePassArg,
                                    "--out", sign.output, align.output });
    sign.inputs = { align.output, keystoreFile };
    p.steps.push_back(sign);

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
        for (auto const& tok : s.argv)   a.arg(tok.c_str());
        for (auto const& in  : s.inputs) a.input(in.c_str());
        a.output(s.output.c_str());
        for (auto const& out : s.more_outputs) a.output(out.c_str());
        a.submit();
    }

    // No floor on the output here: the signed package does not exist while
    // this program runs (the tools have been declared, not run), and what an
    // empty package would lack -- the application object -- is refused by
    // `plan_for` before any step is planned.
    return true;
}

// ─── The one call a consumer makes ─────────────────────────────────────────

inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("apk");
    mcpp::provides_pack_format("aab");
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::apk

