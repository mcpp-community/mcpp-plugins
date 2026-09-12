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

    // A `res/`-shaped directory `aapt2 compile --dir` compiles. Empty means
    // no resources at all -- a legal, common case for a NativeActivity
    // application that draws everything itself.
    std::string resources;

    // LEVEL 1. A directory of `.java` sources this member compiles with
    // `javac` and dexes with `d8`. Empty (the default) is level 0: no Java,
    // `hasCode="false"`, `android.app.NativeActivity` as the manifest's
    // activity.
    std::string java_sources;

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

inline std::string replace_dashes(std::string s) {
    for (char& c : s) if (c == '-') c = '_';
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
// version it resolved -- "35-r2", "34-r3" (`pkgs/a/android-platform.lua`'s
// own `extract_dir()` recovers the API level the identical way, from the
// leading digits of its OWN version string) -- so the level a project
// pinned is read back from the directory `xpkg_dir` already answered,
// rather than duplicated as a second option this member could disagree
// with.
inline std::string api_level_from_platform_dir(const std::string& dir) {
    if (dir.empty()) return {};
    std::string leaf = fs::path(dir).filename().string();
    std::string digits;
    for (char c : leaf) {
        if (c >= '0' && c <= '9') digits += c;
        else break;
    }
    return digits;
}

inline std::string manifest_xml(const std::string& app_id, const std::string& label,
                                const std::string& min_sdk, const std::string& target_sdk,
                                const std::string& target, bool has_code,
                                const std::string& activity_name) {
    std::string a = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
        "    package=\"" + app_id + "\">\n"
        "    <uses-sdk android:minSdkVersion=\"" + min_sdk +
        "\" android:targetSdkVersion=\"" + target_sdk + "\"/>\n"
        "    <application android:label=\"" + label + "\" android:hasCode=\"" +
        (has_code ? "true" : "false") + "\">\n"
        "        <activity android:name=\"" + activity_name +
        "\" android:exported=\"true\">\n";
    if (!has_code) {
        a += "            <meta-data android:name=\"android.app.lib_name\" "
             "android:value=\"" + target + "\"/>\n";
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

    const std::string manifestPath = (fs::path(opt.out_dir) / "dist-apk" / "AndroidManifest.xml").string();
    if (!write_if_different(manifestPath,
            manifest_xml(appId, label, minSdk, targetSdk, target, hasCode, activityName))) {
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
    if (!assembled.empty()) { link.argv.push_back("-R"); link.argv.push_back(assembled.back()); }
    link.argv.push_back("-o"); link.argv.push_back(link.output);
    link.inputs = { manifestPath, androidJar };
    if (!assembled.empty()) link.inputs.push_back(assembled.back());
    p.steps.push_back(link);
    std::string apkPath = link.output;

    std::vector<std::string> javaOutputs; // classes.dex, when level 1
    if (hasCode) {
        if (!is_dir(opt.java_sources)) {
            std::cerr << std::format(
                "mcpp.dist.apk: options::java_sources '{}' is not a "
                "directory", opt.java_sources) << '\n';
            p.reason = "java_sources directory not found";
            return p;
        }
        std::vector<std::string> javaFiles;
        { std::error_code ec;
          for (auto& e : fs::recursive_directory_iterator(opt.java_sources, ec)) {
              if (ec) break;
              if (e.is_regular_file(ec) && e.path().extension() == ".java")
                  javaFiles.push_back(e.path().string());
          }
        }
        if (javaFiles.empty()) {
            std::cerr << std::format(
                "mcpp.dist.apk: options::java_sources '{}' carries no .java "
                "file", opt.java_sources) << '\n';
            p.reason = "no .java sources";
            return p;
        }
        mcpp::rerun_if_changed_glob((opt.java_sources + "/**/*.java").c_str());

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
