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

module;
#include <cstdio>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

export module mcpp.dist.apk;

import std;
import mcpp;
import mcpp.plugins;

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

// ─── Internals ─────────────────────────────────────────────────────────────

namespace fs = std::filesystem;

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}
inline bool is_dir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

// Is `root` under the package root, `mcpp::manifest_dir()`, and if so, what
// is its manifest-relative form? A project root is declared with
// `rerun_if_changed_glob` below (a file appearing there re-runs the build
// program); a dependency root is not -- its file set changes only with the
// dependency's version, already in the build's fingerprint, and the glob's
// own walk does not reach outside the package root regardless (design
// record §3.3). Same shape as `mcpp.tools.island`'s overlap check:
// `weakly_canonical` plus `lexically_relative`, never the iterator that
// poisons an importer under GCC 16 / MSVC (`mcpp::plugins::names::
// relative_to`'s own header) -- safe here because this member is its own
// module and imports no sibling that would inherit the instantiation.
//
// THE RETURNED PATH IS MANIFEST-RELATIVE, NOT ABSOLUTE, BECAUSE THE GLOB
// PATTERN MUST BE. The engine matches a glob by comparing the CANDIDATE made
// relative to the package root against the pattern
// (`modules/manifest/src/glob.cppm`, `path_matches_glob`); an absolute
// pattern is compared against a relative candidate and never matches
// anything, so the fingerprint is always the empty set and a `.java` file
// appearing never changes it -- the criterion's "no" reading as silence
// (design record, rule 8). `opt.java_sources`'s own roots are absolute
// (`mcpp::manifest_dir()` composed with a subdirectory), so declaring the
// glob with `root` itself, not this function's return value, was exactly
// that defect.
inline std::optional<std::string> root_in_project(const std::string& root) {
    std::error_code ec;
    const auto a = fs::weakly_canonical(root, ec);
    if (ec) return std::nullopt;
    const auto b = fs::weakly_canonical(mcpp::manifest_dir(), ec);
    if (ec) return std::nullopt;
    const auto rel = a.lexically_relative(b).generic_string();
    if (rel.empty() || rel.starts_with("..")) return std::nullopt;
    return rel;
}

inline bool write_if_different(const fs::path& path, std::string_view bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
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

// `<abi>/`, from the row's arch and no other input -- see `artifact_naming`
// for the engine's own row-decides-the-name principle, applied here to
// Android's own ABI vocabulary rather than a file suffix.
inline std::string abi_for() {
    const std::string a = mcpp::target_arch();
    if (a == "aarch64") return "arm64-v8a";
    if (a == "x86_64")  return "x86_64";
    return {};
}

// Index loop, not a range-for: GCC 16.1.0 refuses to inline
// `__normal_iterator<char*, basic_string<char>>::operator*() const` when a
// module interface unit that imports `std` also range-for's (or otherwise
// takes `basic_string`'s own iterator over) a `std::string` or
// `std::filesystem::path` --
//
//   bits/stl_iterator.h:1089:7: error: inlining failed in call to
//   'always_inline' '... operator*() const'
//
// -- the same family of defect #18 met under MSVC's `_Path_iterator` (see
// `mcpp::plugins::names::relative_to` and `components()` in
// `src/plugins.cppm`), here on `libstdc++`'s `basic_string` iterator instead
// of the filesystem one. `s[i]` indexes the string directly and never forms
// that iterator, so it compiles under both.
inline std::string replace_dashes(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == '-') s[i] = '_';
    return s;
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

// `xim:android-platform`'s own resolved directory is named after the
// version it resolved -- "36-r2", "35-r2", "34-r3" (`pkgs/a/android-
// platform.lua`'s own `extract_dir()` recovers the API level the identical
// way, from the leading digits of its OWN version string) -- so the level a
// project pinned is read back from the directory `xpkg_dir` already
// answered, rather than duplicated as a second option this member could
// disagree with.
inline std::string api_level_from_platform_dir(const std::string& dir) {
    if (dir.empty()) return {};
    const std::string leaf = fs::path(dir).filename().string();
    std::string digits;
    for (std::size_t i = 0; i < leaf.size(); ++i) {
        if (leaf[i] >= '0' && leaf[i] <= '9') digits += leaf[i];
        else break;
    }
    return digits;
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

// The eight tokens a manifest template may use. `version_name` is the
// package version as written; `version_code` is Android's monotonic integer
// derived from its leading numeric segments (`version_code_for`).
inline const std::vector<std::string>& manifest_tokens() {
    static const std::vector<std::string> v = {
        "application_id", "label", "activity", "lib_name", "min_sdk", "target_sdk",
        "version_name", "version_code"};
    return v;
}

// `android:versionCode` from a version string: major * 1000000 + minor * 1000
// + patch over the first three dot-separated numeric segments, so that every
// release a project cuts orders after the one before it, and "1" when the
// string starts with no number at all -- Android refuses 0 and a manifest
// without the attribute installs but never updates.
inline std::string version_code_for(const std::string& version) {
    long long code = 0, seg = 0;
    int segments = 0;
    bool digits = false;
    const long long weights[3] = {1000000, 1000, 1};
    for (std::size_t i = 0; i <= version.size() && segments < 3; ++i) {
        const char c = i < version.size() ? version[i] : '\0';
        if (c >= '0' && c <= '9') { seg = seg * 10 + (c - '0'); digits = true; continue; }
        if (!digits) break;
        code += std::min<long long>(seg, 999) * weights[segments] ;
        ++segments; seg = 0; digits = false;
        if (c != '.') break;
    }
    if (segments == 0) return "1";
    if (code <= 0) return "1";
    return std::to_string(code);
}

// Every `{{...}}` a template names, in first-appearance order, duplicates
// dropped -- what both checks in `render_manifest` below read.
inline std::vector<std::string> tokens_in(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while ((i = text.find("{{", i)) != std::string::npos) {
        const auto close = text.find("}}", i + 2);
        if (close == std::string::npos) break;
        std::string name = text.substr(i + 2, close - (i + 2));
        if (std::ranges::find(out, name) == out.end()) out.push_back(std::move(name));
        i = close + 2;
    }
    return out;
}

// The tokens `assets/mcpp-run.json` is ALSO written from -- a template that
// omits one silently ships a run sidecar the manifest disagrees with.
// `application_id` and `activity` are required unconditionally; `lib_name`
// joins them at level 0, where the manifest's own `<meta-data>` element is
// the only place the loaded library's name is recorded.
inline std::vector<std::string> required_manifest_tokens(bool has_code, bool native_activity) {
    static_cast<void>(has_code);
    std::vector<std::string> v = {"application_id", "activity"};
    if (native_activity) v.push_back("lib_name");
    return v;
}
inline std::vector<std::string> required_manifest_tokens(bool has_code) {
    return required_manifest_tokens(has_code, !has_code);
}

// THE BUILT-IN DEFAULT, EXPRESSED WITH THE TOKENS. This is `manifest_xml`'s
// 0.8.0 output verbatim, with every literal value it used to compute
// replaced by the token that value now comes through -- so level 0 with no
// project template renders byte-identical to what 0.8.0 wrote
// (`tests/apk-consumer`). `{{activity}}` carries the level-0 constant
// (`android.app.NativeActivity`) as well as a level-1 project's own class,
// because the run sidecar needs the activity name at both levels and the
// required-token check reads the TEMPLATE TEXT, not the level -- so the same
// token has to appear on both of this function's two branches.
// `{{lib_name}}`'s `<meta-data>` element exists only at level 0: it
// announces which shared object `NativeActivity` should load, and a
// Java-hosted activity finds its own native library another way.
//
// NO XML COMMENT MARKS THE THREE REQUIRED TOKENS IN THIS STRING, ON PURPOSE:
// this exact text is compared byte-for-byte against 0.8.0's output, which
// carried none, and a template with no author to read a comment gains
// nothing from one. The design record's "mark the required tokens" is done
// here instead, in the `REQUIRED` labels on the C++ lines that build them.
// A NATIVE ACTIVITY MAY STILL CARRY CODE (0.11.0): a level-0 application whose
// libraries or archives bring classes keeps `android.app.NativeActivity` and its
// `lib_name` element, with `android:hasCode="true"` so that the dex is loaded.
inline std::string default_manifest_template(bool has_code, bool native_activity) {
    std::string a =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
        "    package=\"{{application_id}}\">\n"                     // REQUIRED
        "    <uses-sdk android:minSdkVersion=\"{{min_sdk}}\" "
        "android:targetSdkVersion=\"{{target_sdk}}\"/>\n"
        "    <application android:label=\"{{label}}\" android:hasCode=\"" +
        std::string(has_code ? "true" : "false") + "\">\n"
        "        <activity android:name=\"{{activity}}\" "          // REQUIRED
        "android:exported=\"true\">\n";
    if (native_activity) {
        a += "            <meta-data android:name=\"android.app.lib_name\" "
             "android:value=\"{{lib_name}}\"/>\n";                  // REQUIRED at level 0
    }
    a += "            <intent-filter>\n"
         "                <action android:name=\"android.intent.action.MAIN\"/>\n"
         "                <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
         "            </intent-filter>\n"
         "        </activity>\n"
         "    </application>\n"
         "</manifest>\n";
    return a;
}
inline std::string default_manifest_template(bool has_code) {
    return default_manifest_template(has_code, !has_code);
}

// Checks a manifest template and substitutes it, or refuses (returning
// `false` with `reason` and `message` set) naming exactly what is wrong; the
// caller reports `message` (see `refuse`).
//
// THIS CHECK IS `dist-apk`'S OWN, DELIBERATELY NOT `dist-web`'S. A manifest
// has a closed, six-token vocabulary this member itself defines; a web page
// template may legitimately carry `{{ }}` for a front-end framework (Vue,
// Mustache, ...) this member never reads, so `dist-web` leaves an unknown
// token literal for that project's own tooling to read. An unknown token
// here would otherwise reach `aapt2` unsubstituted and fail there with a
// worse message, and the refusal belongs where the name is known -- the same
// rule read against two different facts, not an inconsistency between the
// two members.
inline bool render_manifest(const std::string& templateText, bool has_code, bool native_activity,
                            const std::string& appId, const std::string& label,
                            const std::string& activityName, const std::string& libName,
                            const std::string& minSdk, const std::string& targetSdk,
                            const std::string& versionName, const std::string& versionCode,
                            std::string& out, std::string& reason,
                            std::string& message) {
    for (auto const& tok : tokens_in(templateText)) {
        if (std::ranges::find(manifest_tokens(), tok) == manifest_tokens().end()) {
            message = "mcpp.dist.apk: the manifest template names an unknown "
                      "token '{{" + tok + "}}'; expected one of application_id, "
                      "label, activity, lib_name, min_sdk, target_sdk, "
                      "version_name, version_code.";
            reason = "unknown manifest template token '" + tok + "'";
            return false;
        }
    }
    for (auto const& tok : required_manifest_tokens(has_code, native_activity)) {
        if (templateText.find("{{" + tok + "}}") == std::string::npos) {
            message = "mcpp.dist.apk: the manifest template does not use '{{" + tok
                    + "}}', and assets/mcpp-run.json, which adb-run starts the "
                      "application from, is written from the same value: add {{"
                    + tok + "}} to the template.";
            reason = "manifest template missing required token '" + tok + "'";
            return false;
        }
    }
    out = templateText;
    out = replace_all_copy(std::move(out), "{{application_id}}", appId);
    out = replace_all_copy(std::move(out), "{{label}}", label);
    out = replace_all_copy(std::move(out), "{{activity}}", activityName);
    out = replace_all_copy(std::move(out), "{{lib_name}}", libName);
    out = replace_all_copy(std::move(out), "{{min_sdk}}", minSdk);
    out = replace_all_copy(std::move(out), "{{target_sdk}}", targetSdk);
    out = replace_all_copy(std::move(out), "{{version_name}}", versionName);
    out = replace_all_copy(std::move(out), "{{version_code}}", versionCode);
    return true;
}
inline bool render_manifest(const std::string& templateText, bool has_code,
                            const std::string& appId, const std::string& label,
                            const std::string& activityName, const std::string& libName,
                            const std::string& minSdk, const std::string& targetSdk,
                            const std::string& versionName, const std::string& versionCode,
                            std::string& out, std::string& reason,
                            std::string& message) {
    return render_manifest(templateText, has_code, !has_code, appId, label, activityName, libName,
                           minSdk, targetSdk, versionName, versionCode, out, reason, message);
}

inline std::string run_json(const std::string& app_id, const std::string& activity_name) {
    // Hand-built, not through a JSON library this collection does not
    // depend on: two string fields, neither of which this member lets
    // through unescaped input (`app_id` is derived from the package's own
    // identifiers, `activity_name` is a Java class name or the literal
    // `android.app.NativeActivity`).
    return std::format("{{\"package\": \"{}\", \"activity\": \"{}\"}}\n",
                       app_id, activity_name);
}

// Copies `src` (a file or a directory, recursively) into `dstRoot/relName`,
// listing every regular file it wrote -- so the caller can declare each one
// as an action input rather than the directory itself ("name the input; do
// not harvest a directory", docs/31).
inline void collect_tree(const fs::path& src, const fs::path& dst,
                         std::vector<std::string>& out) {
    std::error_code ec;
    if (fs::is_regular_file(src, ec)) {
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        out.push_back(dst.string());
        return;
    }
    for (auto& e : fs::recursive_directory_iterator(src, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto rel = fs::relative(e.path(), src, ec);
        auto to  = dst / rel;
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(e.path(), to, fs::copy_options::overwrite_existing, ec);
        out.push_back(to.string());
    }
}

namespace xml = mcpp::plugins::xml;

// ─── Paths a project names ─────────────────────────────────────────────────

// A project names its directories relative to its manifest; the actions this
// member declares run later, from the build directory, where a relative path
// names nothing (the defect `dist/apple.cppm` measured with `ditto ios-icons`).
// Every path option is therefore resolved here, once, against
// `mcpp::manifest_dir()`; an absolute path is left alone.
inline std::string resolve_path(const std::string& p) {
    if (p.empty()) return p;
    const fs::path path(p);
    if (path.is_absolute()) return p;
    return (fs::path(mcpp::manifest_dir()) / path).lexically_normal().string();
}

inline void resolve_paths(std::vector<std::string>& v) {
    for (auto& p : v) p = resolve_path(p);
}

// Every regular file under `root` whose extension is `ext`, sorted.
inline std::vector<std::string> files_with_extension(const std::string& root, std::string_view ext) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) && e.path().extension() == ext)
            out.push_back(e.path().string());
    }
    std::ranges::sort(out);
    return out;
}

inline bool dir_has_files(const std::string& root) {
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec)) return true;
    }
    return false;
}

inline std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string join(const std::vector<std::string>& v, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

// ─── SHA-256 ───────────────────────────────────────────────────────────────

// FIPS 180-4, over bytes held in memory. Here for one purpose: a Maven lock
// records each artifact's digest, and a build that reads a cached artifact
// checks it against the lock before the file reaches a package. The inputs are
// libraries of a few megabytes, so reading a file whole is the simple choice.
inline std::string sha256_hex(std::string_view data) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string msg(data);
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8u;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bits >> (i * 8)) & 0xffu));
    const auto rotr = [](std::uint32_t x, int n) -> std::uint32_t { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const auto byte = [&](int j) {
                return static_cast<std::uint32_t>(static_cast<unsigned char>(msg[off + static_cast<std::size_t>(i * 4 + j)]));
            };
            w[i] = (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string hex;
    hex.reserve(64);
    for (int i = 0; i < 8; ++i) hex += std::format("{:08x}", h[i]);
    return hex;
}

inline std::string sha256_file(const std::string& path) {
    return is_file(path) ? sha256_hex(read_file(path)) : std::string();
}

// ─── Running a tool at plan time ─────────────────────────────────────────

// POSIX single quotes: the only character that needs care inside them is the
// quote itself. This member runs on the hosts that build Android rows, which
// are Linux and macOS (xim:android-ndk publishes no Windows table).
inline std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\'') out += "'\\''";
        else out += s[i];
    }
    return out + "'";
}

// Runs `argv` through `/bin/sh`, returning its exit status and its combined
// output -- the same `popen` the rules members use to probe a tool at plan
// time. Only the explicit Maven modes and AAR extraction call it; an ordinary
// build of an unchanged project runs nothing here.
inline int run_capture(const std::vector<std::string>& argv, std::string& output) {
    std::string cmd;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        cmd += shell_quote(argv[i]);
    }
    cmd += " 2>&1";
    output.clear();
#if defined(_WIN32)
    FILE* p = ::_popen(cmd.c_str(), "r");
#else
    FILE* p = ::popen(cmd.c_str(), "r");
#endif
    if (!p) return -1;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) output.append(buf, n);
#if defined(_WIN32)
    // Compiled on every host (`tests/all-rules-compile`); an Android row is
    // never packed on Windows, so this branch only has to be well-formed.
    return ::_pclose(p);
#else
    const int status = ::pclose(p);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// ─── Merging library manifests into the application's ────────────────────

// What `mcpp.dist.apk` merges, and the rules it applies. This is a SUBSET of
// the Android Gradle plugin's manifest merger, stated rather than implied:
//
//   - `<uses-permission>`, `<uses-permission-sdk-23>`, `<permission>`,
//     `<uses-feature>` and the other children of `<manifest>` are added when
//     the application has no element of the same kind and `android:name`;
//     `<queries>` children are unioned;
//   - the children of a library's `<application>` -- activities, aliases,
//     services, receivers, providers, `<meta-data>`, `<uses-library>` -- are
//     added the same way; a library's `<application>` attribute the
//     application does not state is added;
//   - an identical element is not added twice; a DIFFERENT element under the
//     same name is refused, naming it, unless the application's own element
//     says `tools:node="replace"` (the application's wins) or
//     `tools:node="remove"` (neither is kept); a differing `<application>`
//     attribute is refused unless the application lists it in `tools:replace`;
//   - a library's `<uses-sdk android:minSdkVersion>` above the application's
//     floor is refused, as Gradle refuses it;
//   - `${applicationId}` is substituted everywhere, and any other `${...}`
//     placeholder is refused, naming it -- no placeholder source exists here;
//   - every `tools:` attribute and the `xmlns:tools` declaration are removed
//     from the result.
//
// Anything a project needs beyond that is stated in its own manifest template,
// which this member never edits except to add what the libraries declare.
struct manifest_source {
    std::string label;   // what a refusal names: a library, an AAR, a coordinate
    std::string text;
};

inline std::string manifest_key(const xml::node& n) {
    if (n.name == "uses-feature") {
        const std::string nm = xml::attr_of(n, "android:name");
        return nm.empty() ? "uses-feature@glEsVersion=" + xml::attr_of(n, "android:glEsVersion")
                          : "uses-feature:" + nm;
    }
    return n.name + ":" + xml::attr_of(n, "android:name");
}

inline void strip_tools(xml::node& n) {
    std::erase_if(n.attrs, [](const std::pair<std::string, std::string>& a) {
        return a.first.starts_with("tools:") || a.first == "xmlns:tools";
    });
    for (auto& c : n.children) strip_tools(c);
}

inline std::string canonical_xml(xml::node n) {
    strip_tools(n);
    std::string out;
    xml::write(n, out, 0);
    return out;
}

inline bool substitute_placeholders(xml::node& n, const std::string& appId,
                                    const std::string& label, std::string& message) {
    for (auto& a : n.attrs) {
        a.second = replace_all_copy(a.second, "${applicationId}", appId);
        const auto p = a.second.find("${");
        if (p != std::string::npos) {
            const auto e = a.second.find('}', p);
            message = std::format(
                "mcpp.dist.apk: {} uses the manifest placeholder '{}' in {}=\"{}\"; "
                "only ${{applicationId}} is substituted here.", label,
                a.second.substr(p, e == std::string::npos ? std::string::npos : e - p + 1),
                a.first, a.second);
            return false;
        }
    }
    for (auto& c : n.children)
        if (!substitute_placeholders(c, appId, label, message)) return false;
    return true;
}

inline xml::node* find_child(xml::node& parent, std::string_view name) {
    for (auto& c : parent.children) if (c.name == name) return &c;
    return nullptr;
}

// Removes every element the application marked `tools:node="remove"`: a marker
// that says "this is not wanted", not an element to ship.
inline void drop_removed(xml::node& n) {
    std::erase_if(n.children, [](const xml::node& c) { return xml::attr_of(c, "tools:node") == "remove"; });
    for (auto& c : n.children) drop_removed(c);
}

inline bool merge_manifests(std::string& appText, const std::vector<manifest_source>& libs,
                            const std::string& appId, const std::string& minSdk,
                            std::string& reason, std::string& message) {
    xml::node app;
    std::string err;
    if (!xml::parse(appText, app, err) || app.name != "manifest") {
        reason = "the application manifest cannot be read";
        message = "mcpp.dist.apk: the application's manifest cannot be read for merging: "
                + (err.empty() ? std::string("its root element is not <manifest>") : err);
        return false;
    }
    if (!substitute_placeholders(app, appId, "the application's manifest", message)) {
        reason = "unknown manifest placeholder";
        return false;
    }
    xml::node* application = find_child(app, "application");
    if (!application) {
        app.children.push_back(xml::node{"application", {}, {}, {}});
        application = &app.children.back();
    }
    const auto parse_number = [](const std::string& s) {
        long v = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return -1L;
            v = v * 10 + (s[i] - '0');
        }
        return s.empty() ? -1L : v;
    };

    for (auto const& lib : libs) {
        xml::node m;
        if (!xml::parse(lib.text, m, err) || m.name != "manifest") {
            reason = "a library manifest cannot be read";
            message = std::format("mcpp.dist.apk: the manifest of {} cannot be read: {}", lib.label,
                                  err.empty() ? std::string("its root element is not <manifest>") : err);
            return false;
        }
        if (!substitute_placeholders(m, appId, lib.label, message)) {
            reason = "unknown manifest placeholder";
            return false;
        }
        for (auto& c : m.children) {
            if (c.name.empty()) continue;
            if (c.name == "uses-sdk") {
                const long want = parse_number(xml::attr_of(c, "android:minSdkVersion"));
                const long have = parse_number(minSdk);
                if (want > 0 && have > 0 && want > have) {
                    reason = "a library needs a higher minSdkVersion";
                    message = std::format(
                        "mcpp.dist.apk: {} declares android:minSdkVersion {}, above this "
                        "application's floor {}. Raise the row's min_api_level, or do not "
                        "depend on it.", lib.label, want, have);
                    return false;
                }
                continue;
            }
            if (c.name == "application") {
                const std::string replaced = xml::attr_of(*application, "tools:replace");
                for (auto const& a : c.attrs) {
                    if (a.first.starts_with("tools:")) continue;
                    const std::string mine = xml::attr_of(*application, a.first);
                    if (mine.empty()) { xml::set_attr(*application, a.first, a.second); continue; }
                    if (mine == a.second) continue;
                    bool listed = false;
                    std::size_t start = 0;
                    while (start <= replaced.size()) {
                        auto comma = replaced.find(',', start);
                        if (comma == std::string::npos) comma = replaced.size();
                        if (xml::trim_copy(replaced.substr(start, comma - start)) == a.first) { listed = true; break; }
                        start = comma + 1;
                    }
                    if (!listed) {
                        reason = "conflicting <application> attribute";
                        message = std::format(
                            "mcpp.dist.apk: <application {}> is \"{}\" in the application and "
                            "\"{}\" in {}. Add tools:replace=\"{}\" to the application's "
                            "<application> to keep its value.", a.first, mine, a.second, lib.label, a.first);
                        return false;
                    }
                }
                for (auto& cc : c.children) {
                    if (cc.name.empty()) continue;
                    const std::string key = manifest_key(cc);
                    xml::node* mine = nullptr;
                    for (auto& x : application->children)
                        if (!x.name.empty() && manifest_key(x) == key) { mine = &x; break; }
                    if (!mine) { application->children.push_back(cc); continue; }
                    const std::string node = xml::attr_of(*mine, "tools:node");
                    if (node == "remove" || node == "replace") continue;
                    if (canonical_xml(*mine) == canonical_xml(cc)) continue;
                    reason = "conflicting manifest element";
                    message = std::format(
                        "mcpp.dist.apk: <{} android:name=\"{}\"> differs between the application "
                        "and {}. State tools:node=\"replace\" on the application's element to keep "
                        "it, or tools:node=\"remove\" to drop both.", cc.name,
                        xml::attr_of(cc, "android:name"), lib.label);
                    return false;
                }
                continue;
            }
            if (c.name == "queries") {
                xml::node* q = find_child(app, "queries");
                if (!q) {
                    auto pos = std::ranges::find_if(app.children, [](const xml::node& x) { return x.name == "application"; });
                    q = &*app.children.insert(pos, xml::node{"queries", {}, {}, {}});
                    application = find_child(app, "application");
                }
                for (auto& qc : c.children) {
                    if (qc.name.empty()) continue;
                    const std::string canon = canonical_xml(qc);
                    const bool present = std::ranges::any_of(q->children, [&](const xml::node& x) {
                        return canonical_xml(x) == canon;
                    });
                    if (!present) q->children.push_back(qc);
                }
                continue;
            }
            const std::string key = manifest_key(c);
            xml::node* mine = nullptr;
            for (auto& x : app.children)
                if (!x.name.empty() && manifest_key(x) == key) { mine = &x; break; }
            if (mine) {
                const std::string node = xml::attr_of(*mine, "tools:node");
                if (node == "remove" || node == "replace") continue;
                if (canonical_xml(*mine) == canonical_xml(c)) continue;
                reason = "conflicting manifest element";
                message = std::format(
                    "mcpp.dist.apk: <{} android:name=\"{}\"> differs between the application and "
                    "{}. State tools:node=\"replace\" on the application's element to keep it, or "
                    "tools:node=\"remove\" to drop both.", c.name, xml::attr_of(c, "android:name"), lib.label);
                return false;
            }
            auto pos = std::ranges::find_if(app.children, [](const xml::node& x) { return x.name == "application"; });
            app.children.insert(pos, c);
            application = find_child(app, "application");
        }
    }

    drop_removed(app);
    strip_tools(app);
    appText = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    xml::write(app, appText, 0);
    return true;
}

// ─── JSON ─────────────────────────────────────────────────────────────────
//
// coursier's report is read with the collection's shared reader, the one
// `mcpp::plugins::graph` reads the engine's graph document with.
using json_value = mcpp::plugins::json::value;
using mcpp::plugins::json::parse_json;

// ─── The Maven lock ───────────────────────────────────────────────────────

// One line per fact, sorted, so that a diff of a lock says what changed:
//
//   request <group:artifact:version>        what the project asked for
//   repository <url>                        where resolution looked, in order
//   artifact <coordinate> <aar|jar> <path> <sha256>
//
// `<path>` is relative to the cache root, which is where coursier keeps the
// file under `<scheme>/<host>/<path>` (`--cache` is always passed, so the root
// this member computes and the one coursier writes to are the same directory).
struct maven_artifact {
    std::string coordinate;
    std::string type;
    std::string path;
    std::string sha256;
};

struct maven_lock {
    bool                        found = false;
    std::vector<std::string>    requests;
    std::vector<std::string>    repositories;
    std::vector<maven_artifact> artifacts;
};

inline maven_lock read_maven_lock(const std::string& path) {
    maven_lock lock;
    if (!is_file(path)) return lock;
    lock.found = true;
    std::istringstream in(read_file(path));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;
        if (kind == "request")    { std::string v; fields >> v; lock.requests.push_back(v); }
        else if (kind == "repository") { std::string v; fields >> v; lock.repositories.push_back(v); }
        else if (kind == "artifact") {
            maven_artifact a;
            fields >> a.coordinate >> a.type >> a.path >> a.sha256;
            lock.artifacts.push_back(std::move(a));
        }
    }
    return lock;
}

inline std::string render_maven_lock(const maven_lock& lock) {
    std::string out =
        "# mcpp.dist.apk Maven lock. Written by MCPP_DIST_APK_MAVEN=update; commit it.\n"
        "# An ordinary build reads only these artifacts, from the cache, and checks each digest.\n";
    for (auto const& r : lock.requests)     out += "request " + r + "\n";
    for (auto const& r : lock.repositories) out += "repository " + r + "\n";
    for (auto const& a : lock.artifacts)
        out += "artifact " + a.coordinate + " " + a.type + " " + a.path + " " + a.sha256 + "\n";
    return out;
}

// Where coursier's cache is: the option, then `COURSIER_CACHE`, then coursier's
// own default on this host, so a developer's `cs` and this member share one.
inline std::string maven_cache_root(const std::string& option) {
    if (!option.empty()) return option;
    if (const char* env = std::getenv("COURSIER_CACHE"); env && *env) return env;
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
    if (std::string(mcpp::host()).find("apple") != std::string::npos ||
        std::string(mcpp::host()).find("macos") != std::string::npos)
        return h + "/Library/Caches/Coursier/v1";
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) return std::string(xdg) + "/coursier/v1";
    return h + "/.cache/coursier/v1";
}

// `group:artifact:version`, three non-empty parts and a fixed version: a range
// would make the lock the only place the version is decided.
inline bool plain_coordinate(const std::string& c) {
    int colons = 0;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (c[i] == ':') ++colons;
        if (c[i] == '[' || c[i] == '(' || c[i] == ',' || c[i] == '+' || c[i] == ' ') return false;
    }
    return colons == 2 && !c.starts_with(":") && !c.ends_with(":") && c.find("::") == std::string::npos;
}

// coursier names an AAR-packaged module `group:artifact:aar:version`; the lock
// records the coordinate a project writes.
inline std::string plain_form(const std::string& coord) {
    return replace_all_copy(coord, ":aar:", ":");
}

// ─── An AAR, unpacked ─────────────────────────────────────────────────────

// What an Android library archive contributes: its manifest (merged), its
// `res/` (linked under its own package), its classes (`classes.jar`, and any
// `libs/*.jar` it carries), its native libraries (`jni/<abi>/`) and its
// `assets/`. `R.txt`, `proguard.txt`, `lint.jar` and `public.txt` are not
// read: this member does not shrink, lint or compute non-transitive R classes.
struct library_input {
    std::string              label;
    std::string              package;
    std::string              resources;
    std::string              manifest;
    std::string              assets;
    std::string              jni;
    std::vector<std::string> java;
    std::vector<std::string> kotlin;
    std::vector<std::string> jars;
};

// Unpacked with the JDK's `jar` at plan time, once per archive content: a stamp
// beside the tree records the archive's digest, and a matching stamp skips the
// work on the next plan.
inline bool unpack_aar(const std::string& aar, const fs::path& dest, const std::string& jarTool,
                       library_input& out, std::string& message) {
    const std::string digest = sha256_file(aar);
    if (digest.empty()) {
        message = std::format("mcpp.dist.apk: the AAR {} was not found.", aar);
        return false;
    }
    const fs::path stamp = dest / ".mcpp-aar-sha256";
    if (read_file(stamp.string()) != digest) {
        std::error_code ec;
        fs::remove_all(dest, ec);
        fs::create_directories(dest, ec);
        std::string output;
        const int rc = run_capture({"/bin/sh", "-c", "cd \"$1\" && \"$2\" xf \"$3\"", "unpack-aar",
                                    dest.string(), jarTool, aar}, output);
        if (rc != 0) {
            message = std::format("mcpp.dist.apk: unpacking {} with {} failed ({}): {}", aar, jarTool, rc, output);
            return false;
        }
        write_if_different(stamp, digest);
    }
    const std::string manifest = (dest / "AndroidManifest.xml").string();
    if (!is_file(manifest)) {
        message = std::format("mcpp.dist.apk: {} carries no AndroidManifest.xml, so it is not an AAR.", aar);
        return false;
    }
    xml::node m;
    std::string err;
    if (!xml::parse(read_file(manifest), m, err) || m.name != "manifest") {
        message = std::format("mcpp.dist.apk: the manifest of {} cannot be read: {}", aar, err);
        return false;
    }
    out.package  = xml::attr_of(m, "package");
    out.manifest = manifest;
    if (is_dir((dest / "res").string()) && dir_has_files((dest / "res").string()))       out.resources = (dest / "res").string();
    if (is_dir((dest / "assets").string()) && dir_has_files((dest / "assets").string())) out.assets = (dest / "assets").string();
    if (is_dir((dest / "jni").string()))                                                  out.jni = (dest / "jni").string();
    if (is_file((dest / "classes.jar").string())) out.jars.push_back((dest / "classes.jar").string());
    for (auto const& j : files_with_extension((dest / "libs").string(), ".jar")) out.jars.push_back(j);
    return true;
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

// Android's own ABI names, the directory names a several-triple tree stages
// under (`lib/<abi>/`) and an APK stores its native libraries under.
inline bool is_android_abi(std::string_view name) {
    return name == "arm64-v8a" || name == "x86_64" || name == "armeabi-v7a" || name == "x86";
}

// One ABI's native libraries, as the staged tree carries them.
struct native_leg {
    std::string              abi;
    std::vector<std::string> libraries;   // absolute paths, sorted
};

inline std::vector<std::string> shared_objects_in(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (e.is_regular_file(ec) && e.path().extension() == ".so")
            out.push_back(e.path().string());
    }
    std::ranges::sort(out);
    return out;
}

// Does the manifest ask for the native libraries to be loaded from the APK in
// place (`<application android:extractNativeLibs="false">`, 0.11.1)? The
// platform can map a library only from an entry stored uncompressed on a page
// boundary, so such a package is laid out that way; a compressed library in it
// fails to install.
inline bool loads_native_libraries_in_place(const std::string& manifest) {
    xml::node root;
    std::string error;
    if (!xml::parse(manifest, root, error) || root.name != "manifest") return false;
    const xml::node* application = find_child(root, "application");
    return application && xml::attr_of(*application, "android:extractNativeLibs") == "false";
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
    const std::string buildTools = mcpp::xpkg_dir("xim", "android-build-tools");
    if (buildTools.empty()) {
        return refuse(p, "android-build-tools not found",
            "mcpp.dist.apk: xim:android-build-tools was not found. Declare it "
            "under [target.'cfg(env = \"android\")'.feature-xlings.dist-apk] in "
            "the consuming project, or install it directly.");
    }
    const std::string platformDir = mcpp::xpkg_dir("xim", "android-platform");
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

    // `apksigner`/`d8`: the JAVA_HOME/PATH wrapper `xim:android-build-tools`
    // itself writes under its own `bin/` (see that package's header) --
    // POSIX only, which is the coverage this member's fixture and CI both
    // run under; a Windows consumer's `apksigner.bat`/`d8.bat` already
    // honour `JAVA_HOME` through that package's own `config()` and need no
    // different path here.
    const std::string aapt2     = (fs::path(buildTools) / "aapt2").string();
    const std::string zipalign  = (fs::path(buildTools) / "zipalign").string();
    const std::string apksigner = (fs::path(buildTools) / "bin" / "apksigner").string();
    const std::string d8        = (fs::path(buildTools) / "bin" / "d8").string();
    for (auto const& [name, path] : {
             std::pair{"aapt2", aapt2}, std::pair{"zipalign", zipalign},
             std::pair{"apksigner", apksigner}, std::pair{"d8", d8}}) {
        if (!is_file(path)) {
            return refuse(p, std::format("{} not found", name), std::format(
                "mcpp.dist.apk: {} was not found at {}; {} does not look like the "
                "xim:android-build-tools payload.", name, path, buildTools));
        }
    }

    // `javac`/`jar`/`jarsigner`: NONE is part of `xim:android-build-tools`
    // (only `apksigner` and `d8` are wrapped there, see that package's header)
    // -- all three come from `xim:jdk-temurin` directly, declared on this
    // member's own table rather than assumed reachable through
    // android-build-tools' runtime dependency, which provisions the JDK for
    // ITS OWN wrappers and does not make it visible to a consumer's build
    // program (docs/31, "declare the tool where it will be looked up").
    const std::string jdkHome = mcpp::xpkg_dir("xim", "jdk-temurin");
    if (jdkHome.empty()) {
        return refuse(p, "jdk-temurin not found",
            "mcpp.dist.apk: xim:jdk-temurin was not found (declare it under "
            "[target.'cfg(env = \"android\")'.feature-xlings.dist-apk]).");
    }
    const std::string javac     = (fs::path(jdkHome) / "bin" / "javac").string();
    const std::string jar       = (fs::path(jdkHome) / "bin" / "jar").string();
    const std::string jarsigner = (fs::path(jdkHome) / "bin" / "jarsigner").string();
    for (auto const& [name, path] : {std::pair{"javac", javac}, std::pair{"jar", jar},
                                     std::pair{"jarsigner", jarsigner}}) {
        if (!is_file(path)) {
            return refuse(p, std::format("{} not found", name), std::format(
                "mcpp.dist.apk: {} was not found at {}; {} does not look like a "
                "JDK payload.", name, path, jdkHome));
        }
    }

    // `bundletool`: only an App Bundle needs it. The launcher `xim:bundletool`
    // writes runs the JDK it was installed against.
    std::string bundletool;
    if (bundle) {
        const std::string btDir = mcpp::xpkg_dir("xim", "bundletool");
        bundletool = btDir.empty() ? std::string()
                                   : (fs::path(btDir) / "bin" / "bundletool").string();
        if (!is_file(bundletool)) {
            return refuse(p, "bundletool not found", std::format(
                "mcpp.dist.apk: an Android App Bundle is built by bundletool, and "
                "xim:bundletool was not found{}. It is declared under "
                "[target.'cfg(env = \"android\")'.feature-xlings.dist-apk].",
                btDir.empty() ? std::string() : std::format(" at {}", bundletool)));
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
        const auto coursier = [&]() -> std::string {
            const std::string dir = mcpp::xpkg_dir("xim", "coursier");
            return dir.empty() ? std::string() : (fs::path(dir) / "bin" / "cs").string();
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
            std::vector<std::string> argv = {cs, "fetch", "--cache", cacheRoot};
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
            std::vector<std::string> argv = {cs, "fetch", "--cache", cacheRoot};
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
    std::string kotlinc, kotlinStdlib;
    if (needsKotlin) {
        const std::string kdir = mcpp::xpkg_dir("xim", "kotlin");
        if (!kdir.empty()) {
            kotlinc      = (fs::path(kdir) / "bin" / "kotlinc").string();
            kotlinStdlib = (fs::path(kdir) / "kotlinc" / "lib" / "kotlin-stdlib.jar").string();
        }
        if (!is_file(kotlinc) || !is_file(kotlinStdlib)) {
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
        const std::string ksDir = mcpp::xpkg_dir("xim", "android-debug-keystore");
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

    // ── the small helper scripts this pipeline needs (argv only, no shell
    // between the engine and the tool). Each is written at plan time, only
    // when its bytes differ, and is marked executable. ─────────────────────
    auto helper = [&](const char* name, std::string_view body) {
        const std::string path = (outDir / name).string();
        write_if_different(path, body);
        std::error_code ec;
        fs::permissions(path,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
            fs::perm_options::add, ec);
        return path;
    };
    // Copies the linked archive and adds the staged `lib/`, `assets/` and the
    // dex with `jar`: aapt2 has no flag for native libraries.
    // `--dex <dir>` adds every `classes*.dex` d8 wrote there: a package whose
    // classes pass the 64K-method limit of one dex gets `classes2.dex` and on,
    // and which of them exist is known only when d8 has run. `--stored <dir>
    // <entry>` adds an entry uncompressed, which native libraries loaded in
    // place must be.
    const std::string copyThenJar = helper("copy-then-jar.sh",
        "#!/bin/sh\n"
        "# mcpp.dist.apk helper. Do not edit.\n"
        "# copy-then-jar.sh <src> <dst> <jar> [--dex <dir>] [--stored <dir> <entry>] <jar update arguments>...\n"
        "set -e\n"
        "src=\"$1\"; dst=\"$2\"; jar=\"$3\"; shift 3\n"
        "dex=\"\"; stored_dir=\"\"; stored=\"\"\n"
        "if [ \"${1:-}\" = --dex ]; then dex=\"$2\"; shift 2; fi\n"
        "if [ \"${1:-}\" = --stored ]; then stored_dir=\"$2\"; stored=\"$3\"; shift 3; fi\n"
        "cp \"$src\" \"$dst\"\n"
        "if [ -n \"$stored\" ]; then \"$jar\" --update --no-compress --file \"$dst\" -C \"$stored_dir\" \"$stored\"; fi\n"
        "if [ \"$#\" -gt 0 ]; then \"$jar\" uf \"$dst\" \"$@\"; fi\n"
        "if [ -n \"$dex\" ]; then (cd \"$dex\" && \"$jar\" uf \"$dst\" classes*.dex); fi\n");
    const std::string runAndStamp = helper("run-and-stamp.sh",
        "#!/bin/sh\n"
        "set -e\n"
        "stamp=\"$1\"; shift\n"
        "\"$@\"\n"
        "mkdir -p \"$(dirname \"$stamp\")\"\n"
        "touch \"$stamp\"\n");
    // `d8` DOES NOT ACCEPT A DIRECTORY, MEASURED (2026-09-12, d8 9.2.4,
    // `xim:android-build-tools` 37.0.0): `d8 --output <dir> <classesDir>`
    // fails in the tool itself, `Unsupported source file type`, one frame
    // into `BaseCommand$Builder.addProgramFiles`. javac's own output set is
    // not knowable at plan time, so this wrapper finds the `.class` files
    // `d8`'s command line needs at COMMAND time instead.
    // Its inputs are directories of classes (javac's, kotlinc's), expanded, and
    // archives (JARs, an AAR's classes.jar, the Kotlin stdlib), passed as they
    // are; `--` ends them. Earlier dex files are removed first, so a package
    // that shrinks below the multidex limit does not keep a stale `classes2.dex`.
    const std::string runD8 = helper("run-d8.sh",
        "#!/bin/sh\n"
        "# mcpp.dist.apk helper. Do not edit.\n"
        "# run-d8.sh <d8> <classes directory | archive>... -- <d8 option>...\n"
        "set -e\n"
        "d8=\"$1\"; shift\n"
        "inputs=\"\"\n"
        "while [ \"$#\" -gt 0 ] && [ \"$1\" != -- ]; do\n"
        "    if [ -d \"$1\" ]; then inputs=\"$inputs $(find \"$1\" -name '*.class')\"; else inputs=\"$inputs $1\"; fi\n"
        "    shift\n"
        "done\n"
        "[ \"$#\" -gt 0 ] && shift\n"
        "if [ -z \"$(echo $inputs)\" ]; then\n"
        "    echo \"run-d8.sh: no .class file and no archive to dex\" >&2\n"
        "    exit 1\n"
        "fi\n"
        "out=\"\"; prev=\"\"\n"
        "for a in \"$@\"; do [ \"$prev\" = --output ] && out=\"$a\"; prev=\"$a\"; done\n"
        "if [ -n \"$out\" ]; then rm -f \"$out\"/classes*.dex; fi\n"
        "\"$d8\" \"$@\" $inputs\n");
    // THE BASE MODULE OF AN APP BUNDLE. `aapt2 link --proto-format` writes the
    // manifest at the archive's root beside `resources.pb` and `res/`;
    // bundletool reads a module whose manifest is under `manifest/`, whose dex
    // is under `dex/` and whose native libraries and assets are under `lib/`
    // and `assets/`. This lays the linked archive out that way. A level-0
    // package has no dex, which the fourth argument states as `-`: an empty
    // argument does not survive the action's argv.
    const std::string makeModule = bundle ? helper("make-module.sh",
        "#!/bin/sh\n"
        "# mcpp.dist.apk helper. Do not edit.\n"
        "set -e\n"
        "proto=\"$1\"; module=\"$2\"; work=\"$3\"; dex=\"$4\"; jar=\"$5\"; out=\"$6\"\n"
        "rm -rf \"$module\" \"$out\"\n"
        "mkdir -p \"$module/manifest\"\n"
        "(cd \"$module\" && \"$jar\" xf \"$proto\")\n"
        "mv \"$module/AndroidManifest.xml\" \"$module/manifest/AndroidManifest.xml\"\n"
        "if [ -d \"$work/lib\" ]; then cp -R \"$work/lib\" \"$module/lib\"; fi\n"
        "if [ -d \"$work/assets\" ]; then cp -R \"$work/assets\" \"$module/assets\"; fi\n"
        "if [ \"$dex\" != - ]; then mkdir -p \"$module/dex\"; cp \"$(dirname \"$dex\")\"/classes*.dex \"$module/dex/\"; fi\n"
        "\"$jar\" cfM \"$out\" -C \"$module\" .\n") : std::string();

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
            kt.argv = { runAndStamp, kt.output, kotlinc, "-jvm-target", "17", "-no-reflect",
                        "-classpath", join(classpath, ":"), "-d", kotlinClassesDir };
            for (auto const& f : kotlinFiles) kt.argv.push_back(f);
            for (auto const& f : javaFiles)   kt.argv.push_back(f);
            kt.inputs = kotlinFiles;
            for (auto const& f : javaFiles) kt.inputs.push_back(f);
            for (auto const& c : classpath) kt.inputs.push_back(c);
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
        javacStep.argv = { runAndStamp, javacStep.output, javac,
                          "-source", "17", "-target", "17",
                          "-cp", join(javacClasspath, ":"), "-d", classesDir };
        for (auto const& f : javaFiles) javacStep.argv.push_back(f);
        javacStep.inputs = javaFiles;
        for (auto const& c : classpath) javacStep.inputs.push_back(c);
        if (!kotlinStamp.empty()) javacStep.inputs.push_back(kotlinStamp);
        p.steps.push_back(javacStep);

        const std::string dexDir = (outDir / "dex").string();
        step d8Step;
        d8Step.id = id("apk:d8", "aab:d8");
        d8Step.role = "artifact";
        d8Step.description = "D8";
        d8Step.output = dexDir + "/classes.dex";
        d8Step.argv = { runD8, d8, classesDir };
        if (!kotlinFiles.empty()) d8Step.argv.push_back(kotlinClassesDir);
        for (auto const& a : archivesToDex) d8Step.argv.push_back(a);
        if (!kotlinFiles.empty()) d8Step.argv.push_back(kotlinStdlib);
        d8Step.argv.push_back("--");
        for (auto const& a : {std::string("--min-api"), minSdk, std::string("--lib"), androidJar,
                              std::string("--output"), dexDir})
            d8Step.argv.push_back(a);
        d8Step.inputs = { javacStep.output, androidJar };
        if (!kotlinStamp.empty()) { d8Step.inputs.push_back(kotlinStamp); d8Step.inputs.push_back(kotlinStdlib); }
        for (auto const& a : archivesToDex) d8Step.inputs.push_back(a);
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
        baseModule.argv = { makeModule, link.output, (outDir / "module").string(), work.string(),
                        javaOutputs.empty() ? std::string("-") : javaOutputs.front(),
                        jar, baseModule.output };
        baseModule.inputs = { link.output };
        for (auto const& f : libInputs)   baseModule.inputs.push_back(f);
        for (auto const& f : assetInputs) baseModule.inputs.push_back(f);
        for (auto const& f : javaOutputs) baseModule.inputs.push_back(f);
        p.steps.push_back(baseModule);

        p.output = !opt.output.empty() ? opt.output
                 : (fs::path(opt.out_dir) / (target + ".aab")).string();
        step build;
        build.id = "aab:bundle";
        build.role = "artifact";
        build.description = "BUNDLETOOL BUILD-BUNDLE";
        build.output = opt.sign ? (outDir / "unsigned.aab").string() : p.output;
        build.argv = { bundletool, "build-bundle", "--modules=" + baseModule.output,
                       "--output=" + build.output, "--overwrite" };
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
    libs.argv = { copyThenJar, link.output, libs.output, jar };
    if (!javaOutputs.empty()) {
        // After the three fixed operands and before the `jar` arguments: see
        // `copy-then-jar.sh`.
        libs.argv.push_back("--dex");
        libs.argv.push_back((outDir / "dex").string());
    }
    if (inPlaceLibraries) {
        libs.argv.insert(libs.argv.end(), { "--stored", work.string(), "lib" });
    } else {
        libs.argv.insert(libs.argv.end(), { "-C", work.string(), "lib" });
    }
    libs.argv.insert(libs.argv.end(), { "-C", work.string(), "assets" });
    libs.inputs = { link.output };
    for (auto const& f : libInputs)   libs.inputs.push_back(f);
    for (auto const& f : assetInputs) libs.inputs.push_back(f);
    for (auto const& f : javaOutputs) libs.inputs.push_back(f);
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
    sign.argv = { apksigner, "sign", "--ks", keystoreFile,
                 "--ks-pass", keystorePassArg,
                 "--ks-key-alias", keystoreAlias,
                 "--key-pass", keystorePassArg,
                 "--out", sign.output, align.output };
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
