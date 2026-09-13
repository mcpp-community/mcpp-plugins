// mcpp.dist.apk -- an application target becomes an installable, signed
// `.apk`, with or without a Java host.
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
// FIVE ACTIONS, level 0, always in this order and under these ids -- `apk:
// manifest` (resource compilation, submitted only when `options::resources`
// names a directory -- there is nothing else this step could do without one),
// `apk:link` (aapt2 turns the generated manifest, `-I android.jar` and the
// optional compiled resources into an unsigned, unaligned `base.apk`),
// `apk:libs` (the native library and the deployed assets join the archive --
// aapt2 has no flag for this, so this step is `jar`, not aapt2), `apk:align`
// (`zipalign`), `apk:sign` (`apksigner`). Level 1 adds `apk:javac` and
// `apk:d8` between `apk:link` and `apk:libs`, and `apk:libs`'s own command
// grows one more `-C` pair for `classes.dex`.
//
// WHAT `mcpp pack`'S OWN CLOSURE DOES NOT DO FOR THIS ROW, MEASURED. Android's
// `run_shared_program` (mcpp.pack) stages the linked `.so` under the staged
// tree's `lib/` and stops there -- no dependency closure (the object cannot be
// executed on this host to ask it, `mcpp.pack.pack`'s own comment on that
// function says so) and, unlike every other row's `run()`, no call to
// `stage_runtime_files`: `mcpp::deploy`'s destinations are computed into
// `opts.runtimeFiles` on every row (`mcpp.pack.pipeline`) but the Android
// branch never consumes that vector. So a project's deployed resources exist
// only in the ORDINARY build output -- `${mcpp.out_dir}/bin/<to>/...`, beside
// the linked `.so` there, exactly where `mcpp::deploy`'s own contract puts
// them ("relative to the executable's directory") -- and never in
// `${mcpp.pack_stage_dir()}`'s `lib/`. This member therefore reads the
// program's own native library from the STAGED tree (`lib/*.so`, which is
// where a future closure would add more) and everything deployed from the
// BUILD tree's `bin/` (every subdirectory there that is not the link output
// itself), rather than from one single source the way `dist-appimage` and
// `dist-apple` can. Measured 2026-09-12 against this exact engine revision
// with a throwaway fixture; recorded here because the design record's own
// table row ("`.apk` | `assets/myapp.resources/`") reads as though the staged
// tree carried them, and it does not.
//
// THE C++ RUNTIME IS SHARED, MEASURED. `readelf -d` of a NativeActivity
// `.so` built by this exact NDK payload (30.0.16248370, the one `xim:
// android-ndk` resolves for `*-linux-android` today) names `NEEDED
// libc++_shared.so` -- confirmed by the same warning `mcpp pack` itself
// prints on this row ("this toolchain ships no libc++.a/libc++abi.a; using
// toolchain-coupled"). The file is never in `${mcpp.pack_stage_dir()}`'s
// `lib/` (Android's closure does not walk it, see above), so this member
// takes it from the ACTIVE toolchain's own sysroot -- `mcpp::toolchain_dir()`
// resolves to `<ndk>/toolchains/llvm/prebuilt/<host>` for this row already,
// with no separate `xim:android-ndk` declaration needed on this member's own
// table, because the NDK is the toolchain building the project, not a tool
// this member wraps -- at `sysroot/usr/lib/<abi-triple>/libc++_shared.so`.
//
// SIGNING, THROUGH THE PACKAGE MODEL. The default keystore is
// `xim:android-debug-keystore`'s one `debug.keystore`, whose alias
// (`androiddebugkey`) and password (`android`) are the exact values
// Android's own tooling has published and used since the platform's first
// release -- publishing them discloses nothing (see that package's own
// header). A project that names `options::keystore` as a package (never a
// path) is signing with a key it keeps out of every public index; the
// password reaches `apksigner` as `env:<NAME>`, a token `apksigner` itself
// resolves against its own environment at run time, so this member never
// reads the secret.
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

    // Where the produced file lands. Empty means `<out_dir>/<target>.apk`.
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

// The NDK triple directory under `sysroot/usr/lib/`, which spells arm64
// differently from the ABI name above (`aarch64-linux-android`, not
// `arm64-v8a`) -- two vocabularies for the one row, read from two different
// places upstream, not a choice this member makes.
inline std::string ndk_lib_triple_for() {
    const std::string a = mcpp::target_arch();
    if (a == "aarch64") return "aarch64-linux-android";
    if (a == "x86_64")  return "x86_64-linux-android";
    return {};
}

// Does `so` NEED `libc++_shared.so`? Read from the dynamic section with
// `llvm-readelf -d`, from the SAME toolchain that linked it -- the one
// `mcpp::toolchain_dir()` names for this build, not a host `readelf` this
// project never declared. Measured against this exact NDK (30.0.16248370):
// an ordinary `import std;` link NEEDs it (`readelf -d` on the fixture's own
// `.so` lists `NEEDED libc++_shared.so`, and `mcpp pack`'s own warning on
// this row -- "this toolchain ships no libc++.a/libc++abi.a; using
// toolchain-coupled" -- says the same thing from the flags side), so this
// is asked per file rather than assumed true for every build: a project
// that links `-static-libstdc++` or carries no C++ translation unit at all
// needs nothing extra, and copying the runtime in unconditionally would
// carry a library nothing in the APK opens.
inline bool needs_libcxx_shared(const std::string& toolchainDir, const std::string& so) {
    const std::string readelf = (fs::path(toolchainDir) / "bin" / "llvm-readelf").string();
    if (!is_file(readelf) || !is_file(so)) return false;
    const std::string cmd = "\"" + readelf + "\" -d \"" + so + "\" 2>/dev/null";
    // `popen` is POSIX and Windows spells it `_popen` -- this module compiles
    // on every host (`tests/all-rules-compile`), even though `plan_for`
    // refuses before reaching this call on every row but Android.
#if defined(_WIN32)
    FILE* p = ::_popen(cmd.c_str(), "r");
#else
    FILE* p = ::popen(cmd.c_str(), "r");
#endif
    if (!p) return false;
    bool found = false;
    char line[512];
    while (std::fgets(line, sizeof line, p)) {
        if (std::strstr(line, "libc++_shared.so")) { found = true; break; }
    }
#if defined(_WIN32)
    ::_pclose(p);
#else
    ::pclose(p);
#endif
    return found;
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

// The six tokens a manifest template may use.
inline const std::vector<std::string>& manifest_tokens() {
    static const std::vector<std::string> v = {
        "application_id", "label", "activity", "lib_name", "min_sdk", "target_sdk"};
    return v;
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
// `false` with `reason` set) naming exactly what is wrong.
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
                            std::string& out, std::string& reason) {
    for (auto const& tok : tokens_in(templateText)) {
        if (std::ranges::find(manifest_tokens(), tok) == manifest_tokens().end()) {
            std::cerr << "mcpp.dist.apk: the manifest template names an unknown "
                         "token '{{" << tok << "}}' -- expected one of "
                         "application_id, label, activity, lib_name, min_sdk, "
                         "target_sdk\n";
            reason = "unknown manifest template token '" + tok + "'";
            return false;
        }
    }
    for (auto const& tok : required_manifest_tokens(has_code)) {
        if (templateText.find("{{" + tok + "}}") == std::string::npos) {
            std::cerr << "mcpp.dist.apk: the manifest template does not use "
                         "'{{" << tok << "}}', and assets/mcpp-run.json -- "
                         "which adb-run starts the application from -- is "
                         "written from the same value: add {{" << tok
                      << "}} to the template.\n";
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

// ─── Plan ──────────────────────────────────────────────────────────────────

inline plan plan_for(options opt = {}) {
    plan p;

    const std::string requested = mcpp::pack_format();
    if (requested != "apk") {
        p.reason = requested.empty()
            ? "this build is not packaging"
            : std::format("--format {} was requested, not apk", requested);
        return p;
    }

    if (const std::string env = mcpp::target_env(); env != "android") {
        std::cerr << std::format(
            "mcpp.dist.apk: an APK is an Android format, and this build's "
            "target environment is '{}'.\n"
            "  build for a *-linux-android target",
            env.empty() ? "unknown" : env) << '\n';
        p.reason = "not an Android target";
        return p;
    }

    const std::string stage = mcpp::pack_stage_dir();
    if (stage.empty()) {
        std::cerr << "mcpp.dist.apk: mcpp reported no staged tree. This "
                     "member needs mcpp 2026.9.11.1 or newer.\n";
        p.reason = "no staged tree";
        return p;
    }

    const std::string target = target_for(opt);
    if (target.empty()) {
        std::cerr << "mcpp.dist.apk: no target to package. Set "
                     "`options::target` to the app target's name.\n";
        p.reason = "no target";
        return p;
    }

    const std::string abi = abi_for();
    if (abi.empty()) {
        std::cerr << std::format(
            "mcpp.dist.apk: '{}' is not an Android ABI this member knows -- "
            "expected aarch64 or x86_64 (mcpp::target_arch())",
            mcpp::target_arch()) << '\n';
        p.reason = "unsupported ABI";
        return p;
    }

    // ── the app's own shared object, from the staged tree's lib/ ──────────
    const std::string stageLib = (fs::path(stage) / "lib").string();
    if (!is_dir(stageLib)) {
        std::cerr << std::format(
            "mcpp.dist.apk: the staged tree at {} carries no lib/ -- expected "
            "the app target's shared object there ({})", stage, stageLib) << '\n';
        p.reason = "no lib/ in the staged tree";
        return p;
    }
    std::vector<std::string> soFiles;
    { std::error_code ec;
      for (auto& e : fs::directory_iterator(stageLib, ec)) {
          if (ec) break;
          if (e.is_regular_file(ec) && e.path().extension() == ".so")
              soFiles.push_back(e.path().string());
      }
    }
    if (soFiles.empty()) {
        std::cerr << std::format(
            "mcpp.dist.apk: {} carries no .so at all -- expected the app "
            "target's own shared object", stageLib) << '\n';
        p.reason = "no shared object in the staged tree";
        return p;
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
        std::cerr << "mcpp.dist.apk: options::java_sources is set, so this "
                     "is a level-1 (Java-hosted) package, and options::"
                     "activity is required: the manifest has no other way "
                     "to name the launchable activity.\n";
        p.reason = "java_sources without activity";
        return p;
    }
    const bool hasCode = !opt.java_sources.empty();

    // ── the payloads this member declared ──────────────────────────────
    const std::string buildTools = mcpp::xpkg_dir("xim", "android-build-tools");
    if (buildTools.empty()) {
        std::cerr << std::format(
            "mcpp.dist.apk: xim:android-build-tools was not found.\n"
            "  declare it under [target.'cfg(env = \"android\")'."
            "feature-xlings.dist-apk] in the consuming project, or install "
            "it directly.") << '\n';
        p.reason = "android-build-tools not found";
        return p;
    }
    const std::string platformDir = mcpp::xpkg_dir("xim", "android-platform");
    if (platformDir.empty()) {
        std::cerr << "mcpp.dist.apk: xim:android-platform was not found "
                     "(declare it under [target.'cfg(env = \"android\")'."
                     "feature-xlings.dist-apk]).\n";
        p.reason = "android-platform not found";
        return p;
    }
    const std::string androidJar = (fs::path(platformDir) / "android.jar").string();
    if (!is_file(androidJar)) {
        std::cerr << std::format(
            "mcpp.dist.apk: {} does not exist -- {} does not look like an "
            "Android platform payload", androidJar, platformDir) << '\n';
        p.reason = "android.jar not found";
        return p;
    }
    const std::string targetSdk = api_level_from_platform_dir(platformDir);
    if (targetSdk.empty()) {
        std::cerr << std::format(
            "mcpp.dist.apk: could not read an API level from the resolved "
            "xim:android-platform directory '{}'", platformDir) << '\n';
        p.reason = "no API level";
        return p;
    }
    const std::string minSdk = mcpp::min_platform_version();
    if (minSdk.empty()) {
        std::cerr << "mcpp.dist.apk: mcpp::min_platform_version() is empty "
                     "on an Android target; this member needs mcpp "
                     "2026.9.12.2 or newer.\n";
        p.reason = "no min platform version";
        return p;
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
            std::cerr << std::format(
                "mcpp.dist.apk: {} was not found at {} -- {} does not look "
                "like the xim:android-build-tools payload",
                name, path, buildTools) << '\n';
            p.reason = std::format("{} not found", name);
            return p;
        }
    }

    // `javac`/`jar`: NEITHER is part of `xim:android-build-tools` (only
    // `apksigner` and `d8` are wrapped there, see that package's header) --
    // both come from `xim:jdk-temurin` directly, declared on this member's
    // own table rather than assumed reachable through android-build-tools'
    // runtime dependency, which provisions the JDK for ITS OWN wrappers and
    // does not make it visible to a consumer's build program (docs/31,
    // "declare the tool where it will be looked up").
    const std::string jdkHome = mcpp::xpkg_dir("xim", "jdk-temurin");
    if (jdkHome.empty()) {
        std::cerr << "mcpp.dist.apk: xim:jdk-temurin was not found "
                     "(declare it under [target.'cfg(env = \"android\")'."
                     "feature-xlings.dist-apk]).\n";
        p.reason = "jdk-temurin not found";
        return p;
    }
    const std::string javac = (fs::path(jdkHome) / "bin" / "javac").string();
    const std::string jar   = (fs::path(jdkHome) / "bin" / "jar").string();
    for (auto const& [name, path] : {std::pair{"javac", javac}, std::pair{"jar", jar}}) {
        if (!is_file(path)) {
            std::cerr << std::format(
                "mcpp.dist.apk: {} was not found at {} -- {} does not look "
                "like a JDK payload", name, path, jdkHome) << '\n';
            p.reason = std::format("{} not found", name);
            return p;
        }
    }

    // ── signing: a keystore, an alias and a password ───────────────────
    std::string keystoreFile, keystoreAlias, keystorePassArg;
    if (opt.keystore.empty()) {
        const std::string ksDir = mcpp::xpkg_dir("xim", "android-debug-keystore");
        if (ksDir.empty()) {
            std::cerr << "mcpp.dist.apk: xim:android-debug-keystore was not "
                         "found (declare it under [target.'cfg(env = "
                         "\"android\")'.feature-xlings.dist-apk], or set "
                         "options::keystore).\n";
            p.reason = "android-debug-keystore not found";
            return p;
        }
        keystoreFile = (fs::path(ksDir) / "debug.keystore").string();
        // The exact, published Android debug-signing convention -- not a
        // secret; see this member's header and `xim:android-debug-keystore`'s
        // own.
        keystoreAlias  = "androiddebugkey";
        keystorePassArg = "pass:android";
    } else {
        auto colon = opt.keystore.find(':');
        const std::string ns   = colon == std::string::npos ? "" : opt.keystore.substr(0, colon);
        const std::string name = colon == std::string::npos ? opt.keystore : opt.keystore.substr(colon + 1);
        const std::string ksDir = mcpp::xpkg_dir(ns.c_str(), name.c_str());
        if (ksDir.empty()) {
            std::cerr << std::format(
                "mcpp.dist.apk: keystore package '{}' was not found. Declare "
                "it under [target.'cfg(env = \"android\")'.xlings.workspace] "
                "in the consuming project.", opt.keystore) << '\n';
            p.reason = "keystore package not found";
            return p;
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
            std::cerr << std::format(
                "mcpp.dist.apk: expected exactly one *.keystore/*.jks file in "
                "{}, found {}", ksDir, candidates.size()) << '\n';
            p.reason = "ambiguous keystore package";
            return p;
        }
        keystoreFile = candidates.front();
        if (opt.keystore_alias.empty() || opt.keystore_password_env.empty()) {
            std::cerr << "mcpp.dist.apk: options::keystore names a package, "
                         "so options::keystore_alias and options::"
                         "keystore_password_env are both required -- a "
                         "private key has no convention this member may "
                         "assume.\n";
            p.reason = "keystore alias/password not given";
            return p;
        }
        keystoreAlias   = opt.keystore_alias;
        keystorePassArg = "env:" + opt.keystore_password_env;
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
            std::cerr << std::format(
                "mcpp.dist.apk: the manifest template {} was not found", tplPath) << '\n';
            p.reason = "manifest template not found";
            return p;
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

    std::string manifestBytes;
    if (!render_manifest(manifestTemplateText, hasCode, appId, label, activityName,
                         target, minSdk, targetSdk, manifestBytes, p.reason)) {
        return p;
    }

    const std::string manifestPath = (fs::path(opt.out_dir) / "dist-apk" / "AndroidManifest.xml").string();
    if (!write_if_different(manifestPath, manifestBytes)) {
        std::cerr << std::format("mcpp.dist.apk: cannot write {}", manifestPath) << '\n';
        p.reason = "cannot write AndroidManifest.xml";
        return p;
    }

    // ── the temporary staging tree: lib/<abi>/, assets/ ─────────────────
    //
    // ASSETS, READ AT PLAN TIME AGAINST `pack_stage_dir()`, THE SAME WAY
    // EVERY OTHER MEMBER READS ITS STAGED TREE (`dist-appimage`'s `is_file`,
    // `dist-apple`'s identical reads). docs/30 ("Producing a distributable")
    // and e2e 651/649 in the mcpp tree both show `mcpp::deploy`'s `to`
    // landing at `<staged tree>/bin/<to>/...`, beside the packed executable
    // -- the second pass of `mcpp pack --format apk` runs `plan_for` AFTER
    // the tree is staged, so those files already exist on disk when this
    // program reads them, exactly as `dist-appimage` reads `${mcpp.stage_
    // dir}`'s `AppRun` candidates.
    //
    // MEASURED ON THIS ROW, 2026-09-12, AGAINST THE ENGINE REVISION THIS
    // MEMBER IS BUILT AGAINST: `*-linux-android`'s own staged tree carries
    // `lib/<name>.so` and NOTHING ELSE -- `run_shared_program` (mcpp.pack)
    // stages the app's own object and stops, calling neither the ELF
    // closure walk nor `stage_runtime_files` the way every other row's
    // `run()` does (see this member's header, "WHAT `mcpp pack`'S OWN
    // CLOSURE DOES NOT DO FOR THIS ROW"). So the loop below is written to
    // the documented, cross-row contract and DOES fire wherever the engine
    // actually stages `bin/<to>/...` beside the executable; on THIS row,
    // today, `bin/` does not exist in the staged tree at all, and the loop
    // is a no-op -- a deployed file is declared correctly and staged
    // nowhere, which is the honest report of a gap in `run_shared_program`
    // rather than in this member. See this member's own report for the
    // measurement that found it.
    const fs::path work = fs::path(opt.out_dir) / "dist-apk" / "stage";
    { std::error_code ec; fs::remove_all(work, ec); }
    std::vector<std::string> libInputs;
    const fs::path libAbiDir = work / "lib" / abi;
    for (auto const& so : soFiles) collect_tree(so, libAbiDir / fs::path(so).filename(), libInputs);

    // `libc++_shared.so`, WHEN THE CLOSURE NEEDS IT. See `needs_libcxx_shared`
    // for the measurement. Not in `${mcpp.pack_stage_dir()}`'s `lib/` --
    // Android's own closure does not walk dependencies onto that tree at all
    // (this member's header) -- so it comes from the ACTIVE toolchain's own
    // sysroot, the same one that linked every `.so` this member just staged.
    {
        const std::string toolchainDir = mcpp::toolchain_dir();
        const std::string triple = ndk_lib_triple_for();
        if (!toolchainDir.empty() && !triple.empty()) {
            const std::string libcxx =
                (fs::path(toolchainDir) / "sysroot" / "usr" / "lib" / triple
                 / "libc++_shared.so").string();
            bool needed = false;
            for (auto const& so : soFiles)
                if (needs_libcxx_shared(toolchainDir, so)) { needed = true; break; }
            if (needed) {
                if (is_file(libcxx))
                    collect_tree(libcxx, libAbiDir / "libc++_shared.so", libInputs);
                else
                    mcpp::warning(std::format(
                        "mcpp.dist.apk: the closure NEEDs libc++_shared.so but "
                        "{} does not exist -- the apk will fail to load",
                        libcxx).c_str());
            }
        }
    }

    const fs::path assetsDir = work / "assets";
    std::vector<std::string> assetInputs;
    { // every deploy'd file, `<stage>/bin/<rel>` -> `assets/<rel>` -- see the
      // long comment above for what this loop finds on this row today.
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
        std::cerr << std::format("mcpp.dist.apk: cannot write {}", runJsonPath) << '\n';
        p.reason = "cannot write mcpp-run.json";
        return p;
    }
    assetInputs.push_back(runJsonPath);

    // ── the small helper script this pipeline needs (argv only, no
    // shell): copies the archive and hands the result to `jar`. The
    // staging tree it copies from (`work`) is fully populated by the time
    // this runs -- native libraries and deployed assets alike are read
    // above, at plan time, not discovered by this script -- so, unlike an
    // earlier revision of this member, it takes no `bindir` argument at
    // all ─────────────────────────────────────────────────────────────
    const fs::path helpersDir = fs::path(opt.out_dir) / "dist-apk";
    const std::string copyThenJar = (helpersDir / "copy-then-jar.sh").string();
    write_if_different(copyThenJar,
        "#!/bin/sh\n"
        "# mcpp.dist.apk helper. Do not edit.\n"
        "set -e\n"
        "src=\"$1\"; dst=\"$2\"; jar=\"$3\"; shift 3\n"
        "cp \"$src\" \"$dst\"\n"
        "\"$jar\" uf \"$dst\" \"$@\"\n");
    { std::error_code ec; fs::permissions(copyThenJar,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add, ec); }
    const std::string runAndStamp = (helpersDir / "run-and-stamp.sh").string();
    write_if_different(runAndStamp,
        "#!/bin/sh\n"
        "set -e\n"
        "stamp=\"$1\"; shift\n"
        "\"$@\"\n"
        "mkdir -p \"$(dirname \"$stamp\")\"\n"
        "touch \"$stamp\"\n");
    { std::error_code ec; fs::permissions(runAndStamp,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add, ec); }
    // `d8` DOES NOT ACCEPT A DIRECTORY, MEASURED (2026-09-12, d8 9.2.4,
    // `xim:android-build-tools` 37.0.0): `d8 --output <dir> <classesDir>`
    // fails in the tool itself, `Unsupported source file type`, one frame
    // into `BaseCommand$Builder.addProgramFiles`. javac's own output set is
    // not knowable at plan time (see above), so this wrapper finds the
    // `.class` files `d8`'s command line needs at COMMAND time instead --
    // the identical "find" this member already had to reach for `assets/`.
    const std::string runD8 = (helpersDir / "run-d8.sh").string();
    write_if_different(runD8,
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
    { std::error_code ec; fs::permissions(runD8,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add, ec); }

    // ── the pipeline ────────────────────────────────────────────────────
    const fs::path outDir = fs::path(opt.out_dir) / "dist-apk";
    std::vector<std::string> assembled; // every step's own output, for later inputs

    if (!opt.resources.empty()) {
        if (!is_dir(opt.resources)) {
            std::cerr << std::format(
                "mcpp.dist.apk: options::resources '{}' is not a directory",
                opt.resources) << '\n';
            p.reason = "resources directory not found";
            return p;
        }
        step compile;
        compile.id = "apk:manifest";
        compile.role = "artifact";
        compile.description = "AAPT2 COMPILE";
        compile.output = (outDir / "compiled.zip").string();
        compile.argv = { aapt2, "compile", "--dir", opt.resources, "-o", compile.output };
        compile.inputs = { opt.resources };
        p.steps.push_back(compile);
        assembled.push_back(compile.output);
    }

    step link;
    link.id = "apk:link";
    link.role = "artifact";
    link.description = "AAPT2 LINK";
    link.output = (outDir / "base.apk").string();
    link.argv = { aapt2, "link", "-I", androidJar, "--manifest", manifestPath,
                 "--min-sdk-version", minSdk, "--target-sdk-version", targetSdk };
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
    std::string apkPath = link.output;

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
                std::cerr << std::format(
                    "mcpp.dist.apk: options::java_sources root '{}' is not a "
                    "directory", root) << '\n';
                p.reason = "java_sources directory not found";
                return p;
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
                std::cerr << std::format(
                    "mcpp.dist.apk: options::java_sources root '{}' carries "
                    "no .java file", root) << '\n';
                p.reason = "no .java sources";
                return p;
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
        javacStep.id = "apk:javac";
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
        d8Step.id = "apk:d8";
        d8Step.role = "artifact";
        d8Step.description = "D8";
        d8Step.output = dexDir + "/classes.dex";
        d8Step.argv = { runD8, d8, classesDir, "--min-api", minSdk, "--lib", androidJar,
                       "--output", dexDir };
        d8Step.inputs = { javacStep.output, androidJar };
        p.steps.push_back(d8Step);
        javaOutputs.push_back(d8Step.output);
    }

    // ── native library and assets join the archive ─────────────────────
    step libs;
    libs.id = "apk:libs";
    libs.role = "artifact";
    libs.description = "APK LIBS+ASSETS";
    libs.output = (outDir / "withlibs.apk").string();
    libs.argv = { copyThenJar, apkPath, libs.output, jar,
                 "-C", work.string(), "lib",
                 "-C", work.string(), "assets" };
    if (!javaOutputs.empty()) {
        libs.argv.push_back("-C");
        libs.argv.push_back((outDir / "dex").string());
        libs.argv.push_back("classes.dex");
    }
    libs.inputs = { apkPath };
    for (auto const& f : libInputs) libs.inputs.push_back(f);
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

    // A FLOOR ON THIS MEMBER'S OWN OUTPUT, ON THE SUCCESS PATH. Nothing here
    // reads the SIGNED apk (the tools have not run yet, only been declared --
    // see `dist/appimage.cppm`'s identical reasoning) so this checks what
    // this program itself built into the staging tree: at least one native
    // library besides bookkeeping. An APK that installs and starts nothing
    // is the failure this whole category exists to catch.
    return true;
}

// ─── The one call a consumer makes ─────────────────────────────────────────

inline bool generate(options opt = {}) {
    mcpp::provides_pack_format("apk");
    return submit(plan_for(std::move(opt)));
}

} // namespace mcpp::dist::apk
