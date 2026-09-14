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
// WHAT THIS MEMBER DOES NOT DO. It does not run `mcpp run --format apk` --
// that is `adb-run`, a session `xim:android-platform-tools` registers, and
// this member's only obligation to it is `assets/mcpp-run.json`, so the
// runner can start the application without `aapt2` on the machine that runs
// it. It does not resolve an emulator or a device; `ANDROID_SERIAL` is the
// caller's configuration, not this member's (rule 4, 2026-09-12 design
// record: "a name is cache-safe, a path is not").

module;
#include <cstdio>

export module mcpp.dist.apk;

import std;
import mcpp;
import mcpp.plugins;

// `std::format` is header-only and used throughout; `std::println` is not --
// see `rules/spirv.cppm` for the libc++-on-macOS-14 measurement that this
// collection's every member has followed since.

export namespace mcpp::dist::apk {

// ─── Options ───────────────────────────────────────────────────────────────

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

    // Where the produced file lands. Empty means `<out_dir>/<target>.apk`, or
    // `<out_dir>/<target>.aab` for `--format aab`.
    std::string output;
    std::string out_dir = std::string(mcpp::out_dir());
};

// ─── The plan ────────────────────────────────────────────────────────────

struct step {
    const char*               id;
    const char*               role;
    const char*               description;
    std::vector<std::string>  argv;
    std::vector<std::string>  inputs;
    std::string               output;
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
inline std::vector<std::string> required_manifest_tokens(bool has_code) {
    std::vector<std::string> v = {"application_id", "activity"};
    if (!has_code) v.push_back("lib_name");
    return v;
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
inline std::string default_manifest_template(bool has_code) {
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
    if (!has_code) {
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
inline bool render_manifest(const std::string& templateText, bool has_code,
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
    for (auto const& tok : required_manifest_tokens(has_code)) {
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

// What the engine wrote beside the staged tree: `<stage>.stage-manifest`
// (mcpp's docs/50, "The stage manifest"). Only the header and the `needs`
// lines are read; the file list that follows them is not.
struct stage_manifest {
    bool                     found = false;
    bool                     walked = true;
    std::string              reason;
    struct need { std::string name; std::string where; };
    std::vector<need>        needs;   // `where`: a staged path, `platform` or `unresolved`
};

inline stage_manifest read_stage_manifest(std::string stage) {
    stage_manifest m;
    while (stage.size() > 1 && (stage.back() == '/' || stage.back() == '\\'))
        stage.pop_back();
    std::ifstream in(stage + ".stage-manifest", std::ios::binary);
    if (!in) return m;
    m.found = true;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "closure = not-walked") { m.walked = false; continue; }
        if (line.starts_with("reason = ")) { m.reason = line.substr(9); continue; }
        if (!line.starts_with("needs\t")) continue;
        const auto second = line.find('\t', 6);
        if (second == std::string::npos) continue;
        m.needs.push_back({ line.substr(6, second - 6), line.substr(second + 1) });
    }
    return m;
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
    const auto manifest = read_stage_manifest(stage);
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

    if (opt.java_sources.empty() && !opt.activity.empty()) {
        // Not fatal -- an explicit activity with no Java host is simply
        // unused -- but the project almost certainly meant `java_sources`
        // too, and level 0's activity is never a name this member reads.
        mcpp::warning("mcpp.dist.apk: options::activity is set with no "
                      "options::java_sources; level 0 always uses "
                      "android.app.NativeActivity and ignores it");
    }
    if (!opt.java_sources.empty() && opt.activity.empty()) {
        return refuse(p, "java_sources without activity",
            "mcpp.dist.apk: options::java_sources is set, so this is a level-1 "
            "(Java-hosted) package, and options::activity is required: the "
            "manifest has no other way to name the launchable activity.");
    }
    const bool hasCode = !opt.java_sources.empty();

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

    // ── signing: a keystore, an alias and a password ───────────────────
    //
    // `apksigner` takes a password as `pass:<value>` or `env:<NAME>`;
    // `jarsigner` takes the same two as `-storepass <value>` or
    // `-storepass:env <NAME>`. Both spellings are derived from one decision.
    std::string keystoreFile, keystoreAlias, keystorePassArg;
    std::vector<std::string> jarsignerPass;
    if (opt.keystore.empty()) {
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
    const std::string activityName = hasCode ? opt.activity : std::string("android.app.NativeActivity");

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
        manifestTemplateText = default_manifest_template(hasCode);
    }

    const std::string versionName = mcpp::package_version() ? mcpp::package_version() : "";
    const std::string versionCode = version_code_for(versionName);
    std::string manifestBytes, manifestReason, manifestMessage;
    if (!render_manifest(manifestTemplateText, hasCode, appId, label, activityName,
                         target, minSdk, targetSdk, versionName, versionCode,
                         manifestBytes, manifestReason, manifestMessage)) {
        return refuse(p, manifestReason, manifestMessage);
    }

    const fs::path outDir = fs::path(opt.out_dir) / (bundle ? "dist-aab" : "dist-apk");
    const std::string manifestPath = (outDir / "AndroidManifest.xml").string();
    if (!write_if_different(manifestPath, manifestBytes)) {
        return refuse(p, "cannot write AndroidManifest.xml", std::format(
            "mcpp.dist.apk: cannot write {}.", manifestPath));
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
    for (auto const& leg : legs)
        for (auto const& so : leg.libraries)
            collect_tree(so, work / "lib" / leg.abi / fs::path(so).filename(), libInputs);

    const fs::path assetsDir = work / "assets";
    std::vector<std::string> assetInputs;
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
    const std::string copyThenJar = helper("copy-then-jar.sh",
        "#!/bin/sh\n"
        "# mcpp.dist.apk helper. Do not edit.\n"
        "set -e\n"
        "src=\"$1\"; dst=\"$2\"; jar=\"$3\"; shift 3\n"
        "cp \"$src\" \"$dst\"\n"
        "\"$jar\" uf \"$dst\" \"$@\"\n");
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
    const std::string runD8 = helper("run-d8.sh",
        "#!/bin/sh\n"
        "# mcpp.dist.apk helper. Do not edit.\n"
        "set -e\n"
        "d8=\"$1\"; classesdir=\"$2\"; shift 2\n"
        "classes=$(find \"$classesdir\" -name '*.class')\n"
        "if [ -z \"$classes\" ]; then\n"
        "    echo \"run-d8.sh: no .class file under $classesdir\" >&2\n"
        "    exit 1\n"
        "fi\n"
        "\"$d8\" \"$@\" $classes\n");
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
        "if [ \"$dex\" != - ]; then mkdir -p \"$module/dex\"; cp \"$dex\" \"$module/dex/classes.dex\"; fi\n"
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
    if (!assembled.empty()) link.argv.push_back(assembled.back());
    link.argv.push_back("-o"); link.argv.push_back(link.output);
    link.inputs = { manifestPath, androidJar };
    if (!assembled.empty()) link.inputs.push_back(assembled.back());
    p.steps.push_back(link);

    std::vector<std::string> javaOutputs; // classes.dex, when level 1
    if (hasCode) {
        // ONE `javac` OVER EVERY ROOT'S `.java` FILES. The member compiles
        // what it is given (design record §3.3) and a second root is more of
        // the same input, not a second step -- `javaFiles` below is one flat
        // list across every root, and one `javac` invocation compiles all of
        // it into one `classesDir`, exactly as it did over one root before.
        std::vector<std::string> javaFiles;
        for (auto const& root : opt.java_sources) {
            if (!is_dir(root)) {
                return refuse(p, "java_sources directory not found", std::format(
                    "mcpp.dist.apk: options::java_sources root '{}' is not a "
                    "directory.", root));
            }
            const std::size_t before = javaFiles.size();
            { std::error_code ec;
              for (auto& e : fs::recursive_directory_iterator(root, ec)) {
                  if (ec) break;
                  if (e.is_regular_file(ec) && e.path().extension() == ".java")
                      javaFiles.push_back(e.path().string());
              }
            }
            if (javaFiles.size() == before) {
                return refuse(p, "no .java sources", std::format(
                    "mcpp.dist.apk: options::java_sources root '{}' carries no "
                    ".java file.", root));
            }
            // THE RE-RUN QUESTION (design record §3.3). `glob_fingerprint`
            // walks the PACKAGE ROOT and matches paths relative to it; a
            // root outside that walk (a dependency's unpack directory)
            // matches nothing, and the fingerprint would be the same as "no
            // files" -- a criterion whose "no" reads as silence. So a
            // project root (under `mcpp::manifest_dir()`) is declared with
            // the glob, as today; a dependency root is not: its file set
            // changes only with the dependency's version, already in the
            // build's fingerprint, and each of its files is already an
            // input of the `javac` action below.
            //
            // THE PATTERN IS MANIFEST-RELATIVE (`root_in_project`'s return
            // value), NOT `root` ITSELF, which is absolute: the engine's
            // glob fingerprint compares each candidate file made relative to
            // the package root against the pattern, so an absolute pattern
            // is compared against a relative candidate and never matches --
            // see `root_in_project`'s own header for the measurement.
            if (auto rel = root_in_project(root))
                mcpp::rerun_if_changed_glob((*rel + "/**/*.java").c_str());
        }

        const std::string classesDir = (outDir / "classes").string();
        step javacStep;
        javacStep.id = id("apk:javac", "aab:javac");
        javacStep.role = "artifact";
        javacStep.description = "JAVAC";
        javacStep.output = classesDir + "/.stamp";
        javacStep.argv = { runAndStamp, javacStep.output, javac,
                          "-source", "17", "-target", "17",
                          "-cp", androidJar, "-d", classesDir };
        for (auto const& f : javaFiles) javacStep.argv.push_back(f);
        javacStep.inputs = javaFiles;
        javacStep.inputs.push_back(androidJar);
        p.steps.push_back(javacStep);

        const std::string dexDir = (outDir / "dex").string();
        step d8Step;
        d8Step.id = id("apk:d8", "aab:d8");
        d8Step.role = "artifact";
        d8Step.description = "D8";
        d8Step.output = dexDir + "/classes.dex";
        d8Step.argv = { runD8, d8, classesDir, "--min-api", minSdk, "--lib", androidJar,
                       "--output", dexDir };
        d8Step.inputs = { javacStep.output, androidJar };
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

        step build;
        build.id = "aab:bundle";
        build.role = "artifact";
        build.description = "BUNDLETOOL BUILD-BUNDLE";
        build.output = (outDir / "unsigned.aab").string();
        build.argv = { bundletool, "build-bundle", "--modules=" + baseModule.output,
                       "--output=" + build.output, "--overwrite" };
        build.inputs = { baseModule.output };
        p.steps.push_back(build);

        step sign;
        sign.id = "aab:sign";
        sign.role = "artifact";
        sign.description = "JARSIGNER";
        p.output = !opt.output.empty() ? opt.output
                 : (fs::path(opt.out_dir) / (target + ".aab")).string();
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
    libs.argv = { copyThenJar, link.output, libs.output, jar,
                 "-C", work.string(), "lib",
                 "-C", work.string(), "assets" };
    if (!javaOutputs.empty()) {
        libs.argv.push_back("-C");
        libs.argv.push_back((outDir / "dex").string());
        libs.argv.push_back("classes.dex");
    }
    libs.inputs = { link.output };
    for (auto const& f : libInputs)   libs.inputs.push_back(f);
    for (auto const& f : assetInputs) libs.inputs.push_back(f);
    for (auto const& f : javaOutputs) libs.inputs.push_back(f);
    p.steps.push_back(libs);

    step align;
    align.id = "apk:align";
    align.role = "artifact";
    align.description = "ZIPALIGN";
    align.output = (outDir / "aligned.apk").string();
    // `-f`: OVERWRITE. Measured -- `zipalign` refuses by default when its own
    // output already exists ("Output file '...' exists"), which every
    // rebuild after the first hits, because ninja does not delete a stale
    // output before an edge reruns it.
    align.argv = { zipalign, "-f", "-p", "4", libs.output, align.output };
    align.inputs = { libs.output };
    p.steps.push_back(align);

    step sign;
    sign.id = "apk:sign";
    sign.role = "artifact";
    sign.description = "APKSIGNER";
    p.output = !opt.output.empty() ? opt.output
             : (fs::path(opt.out_dir) / (target + ".apk")).string();
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
        a.id          = s.id;
        a.role        = s.role;
        a.description = s.description;
        for (auto const& tok : s.argv)   a.arg(tok.c_str());
        for (auto const& in  : s.inputs) a.input(in.c_str());
        a.output(s.output.c_str());
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
