# `dist-apk`

`mcpp.dist.apk` packs the native closure `mcpp pack` stages into a signed APK or an App Bundle for Android, with an optional Java or Kotlin layer, Android libraries and a Maven graph.

## `dist-apk`

Module `mcpp.dist.apk`; engine floor: 2026.10.1.3 from 0.19.0 (mcpp#755), whose protocol 15 states where a tool comes from and installs `xim:bundletool` only when a bundle asks for it; 2026.9.14.2 from 0.10.0, which stages the closure this member reads; before it 2026.9.13.1, raised alongside `dist-web` in the same 0.9.0 release: this member's own manifest-template and Java-array changes ask nothing new of the engine, but this collection publishes one package at one version, and this is the release CI verifies it under from here on.

**Needs and behaviour.** `xim:android-build-tools`, `xim:android-platform` (versioned by API level, read back for `targetSdkVersion`), `xim:jdk-temurin` (`javac`/`jar`/`jarsigner`; `android-build-tools`' own runtime dependency provisions a JDK for its OWN wrappers only), `xim:android-debug-keystore`, `xim:bundletool` (0.10.0, for `--format aab`), all on the `cfg(env = "android")` axis. Generates `AndroidManifest.xml` and signs with the published Android debug key by default. Level 0 needs no Java (`hasCode="false"`, `android.app.NativeActivity`); `options::java_sources` adds `javac` + `d8` and a real `<activity>`. `options::manifest_template` renders a project manifest with six tokens substituted verbatim; `{{application_id}}` and `{{activity}}` are required always and `{{lib_name}}` at level 0, each refused by name at plan time when missing (naming `assets/mcpp-run.json`, which `adb-run` reads them from too) or when the template names an unknown token; empty renders 0.8.0's manifest byte-identically. From 0.9.3 the manifest template gains `{{version_name}}` / `{{version_code}}` (the package version, and `major * 1000000 + minor * 1000 + patch`). `options::resources` is a project's own `res/`, linked as the application's base resources from 0.9.1 (0.9.0 linked it as an aapt2 overlay, which refuses every resource the base does not already define -- a launcher icon could not be supplied). `options::java_sources` is an array: one `javac` over every root's `.java` files and one `d8` over the result, so a project's own sources and a path dependency's join without being merged into one directory first, and `rerun_if_changed_glob` is declared only for a root under `mcpp::manifest_dir()` -- a dependency root's files are already inputs of the `javac` action and its version is already in the build's fingerprint. Android only -- an `app` target is a shared object on this row (#622 A3). From 0.10.0, with mcpp 2026.9.14.2, the member reads the native closure the engine stages -- the application object, the graph's shared libraries and `libc++_shared.so` under `lib/` for one `--target`, `lib/<abi>/` for several -- and packs every ABI the tree carries into one APK; its own `NEEDED` walk and the stamp that lost a dependency's library on a second pack are gone. A stage manifest without `needs` lines comes from an engine below 2026.9.14.2 and is refused naming that floor, as is an incomplete closure, naming the unresolved libraries; every refusal is also a `mcpp::warning`, because the engine discards a build program's output when it exits 0. `--format aab` shares every step but the last three: `aapt2 link --proto-format` (with `--version-code` / `--version-name`, which bundletool requires), a base module in the layout bundletool reads, `bundletool build-bundle` from `xim:bundletool` (declared on the same axis), and `jarsigner` with the same keystore. CI packages both level 0 and level 1 and checks the archive (`mcpp::deploy`'d files under `assets/`), and on `tests/apk-consumer-shared` two packs in a row, a two-ABI APK (`aapt2` reports both, `apksigner` verifies it), an App Bundle (`bundletool validate`, `jarsigner -verify`, a universal APK from `bundletool build-apks`), a refusal's reason in `mcpp pack`'s output, and the floor refusal; the runner has no emulator or device, so the two rows that actually run were measured locally on 2026-09-12, through `adb-run`, against a KVM-accelerated x86_64 emulator and a physical arm64-v8a phone, both printing `1-2-3` and exiting 0. From 0.11.0: Kotlin sources (`dist-apk-kotlin`, which declares `xim:kotlin`), R classes, Android libraries from source, local AARs and JARs, a Maven graph through a lock file (`dist-apk-maven`, which declares `xim:coursier`), a manifest merge, and an unsigned package -- see [`dist-apk`: Kotlin, libraries and a Maven graph](#dist-apk-kotlin-libraries-and-a-maven-graph). From 0.11.1 the native libraries are packed as the Android Gradle plugin packs them: each is stripped with the build's own `llvm-strip --strip-unneeded` (the NDK's, beside the compiler `mcpp::toolchain_dir()` reports; `options::keep_debug_symbols` packs them as staged), and a manifest stating `android:extractNativeLibs="false"` gets them stored uncompressed on a 16 KB page, which loading them from the APK in place requires; CI checks both, and the symbol table `keep_debug_symbols` keeps. From 0.12.0 the engine's strip decision governs these libraries as well: under mcpp 2026.9.16.1, which publishes it to build programs as `MCPP_PACK_STRIP` and `MCPP_PACK_DEBUG_SYMBOLS_DIR`, that engine strips and separates the libraries it stages itself and the member packs them as staged, while the libraries the engine did not stage -- an archive's `jni/` libraries -- follow the same decision inside the member: `--no-strip` packs them as they are, and `--debug-symbols <dir>` separates each one's debug information into `<dir>/<abi>/<library>.debug` with `llvm-objcopy` before stripping it and linking the packed copy to that file (`.gnu_debuglink`). A library is stripped unless either `keep_debug_symbols` or the engine says to keep it, and under an older engine, which publishes neither variable, the member strips every library as before. Because that engine strips before the member reads the tree, `keep_debug_symbols` keeps only the member's own strip off and `--no-strip` is what ships the symbols of the libraries the graph built; the member warns when the option is set and the engine stripped. From 0.12.0 a library in the resolved graph contributes libraries and archives through `[package.metadata.dist-apk]`, ranked below the application's own -- see [A library states its contribution](#a-library-states-its-contribution-packagemetadatadist-apk)

## `dist-apk`: Kotlin, libraries and a Maven graph

From 0.11.0 a level-1 package takes what a Gradle application module takes.
Every path option is relative to the manifest or absolute.

| option | what it adds | payload |
|---|---|---|
| `kotlin_sources` | Directories of `.kt` sources. `kotlinc` compiles them first (`-jvm-target 17`), with every Java root as reference sources; `javac` then compiles the Java against the Kotlin classes, and `d8` dexes both with `kotlin-stdlib.jar`. This option or `java_sources` makes a level-1 package, and both may be set | `xim:kotlin`, declared by the `dist-apk-kotlin` feature |
| (none) | R classes. A package with code links with `aapt2 link --java`, and the `R` classes of the application, of each library and of each archive are compiled with the sources (`--extra-packages`) | |
| `libraries` | Android libraries from source, each with `package` (its R package), `resources`, `manifest`, `assets`, `java_sources` and `kotlin_sources`. A library listed earlier wins a resource both define, and the application's own `resources` win over every library | |
| `aars`, `jars` | Local archives. A JAR joins the classpath and the dex. An AAR contributes `classes.jar`, `res/` (under the package its manifest names), its manifest, `jni/<abi>/*.so` and `assets/`. Archives rank after `libraries` | |
| `maven`, `maven_repositories`, `maven_lock`, `maven_cache` | `group:artifact:version` coordinates, resolved with their transitive dependencies into a lock file. The resolved AARs and JARs are then handled like `aars` and `jars`, and rank after them. The defaults are Google's Maven repository then Maven Central, `maven.lock` beside the manifest, and `COURSIER_CACHE` then coursier's own cache directory for the host | `xim:coursier`, declared by the `dist-apk-maven` feature |
| `graph_libraries` | From 0.12.0, `true` by default: the libraries and archives the resolved graph's packages state in `[package.metadata.dist-apk]` (see below). `false` reads none, for an application that lists every library itself | |
| `sign` | `false` writes the aligned package unsigned, with no `apksigner` for an APK and no `jarsigner` for an App Bundle, for a pipeline that signs elsewhere. It is refused together with `keystore` | |

Both features imply `dist-apk`, so a project names them in its place:

```toml
[build-dependencies.mcpp]
plugins = { version = "0.19.0", features = ["dist-apk-kotlin", "dist-apk-maven"], host-module = true }
```

They are features and not payloads of `dist-apk` because a feature is what a
project selects, and selecting is the statement: a payload of `dist-apk` would
belong to every Android consumer, and the Kotlin compiler is 90 MB. A project
that selects `dist-apk-kotlin` compiles Kotlin, so `xim:kotlin` is installed with
the feature; `xim:bundletool`, which only `--format aab` uses, is asked for while
that bundle is planned (`provision = "on-request"`, 0.19.0), so an ordinary
`--format apk` build installs nothing for it. Kotlin sources without the feature
are refused, naming it.

**An ordinary build does not reach the network.** A Maven graph is resolved and
downloaded only when the developer asks:

| `MCPP_DIST_APK_MAVEN` | what `mcpp pack` does |
|---|---|
| unset | Reads each locked artifact from the cache and checks its sha256 against the lock. A missing lock is refused, and so is a lock resolved for other coordinates, or an artifact absent from the cache. Each refusal names the mode that fixes it. The lock records the repositories it was resolved from without requiring them: a mirror that serves the same bytes passes the digest check |
| `update` | `cs fetch` resolves the coordinates, downloads the graph and writes the lock, one line per file: `artifact <coordinate> <aar\|jar> <path in the cache> <sha256>` |
| `fetch` | Downloads exactly the locked artifacts into the cache (`--intransitive`, one per module), for a machine that has the lock and an empty cache |

Commit the lock. Nothing is shrunk or obfuscated, and an archive's ProGuard
rules are ignored.

**The manifest merge is a stated subset of the Android Gradle plugin's
merger** (`merge_manifests` in `dist/apk.cppm`):

- An element is added when the application has none of the same kind and
  `android:name`. This covers the children of `<manifest>`
  (`<uses-permission>`, `<uses-feature>`, `<permission>`, ...) and of
  `<application>` (activities, services, receivers, providers, `<meta-data>`,
  ...). `<queries>` children are unioned, and an identical element is added
  once.
- A different element under the same name is refused, naming the element and
  the library. The exceptions are an application element that says
  `tools:node="replace"`, which keeps the application's, and one that says
  `tools:node="remove"`, which keeps neither. A differing `<application>`
  attribute is refused unless the application lists it in `tools:replace`.
- A library whose `minSdkVersion` is above the application's is refused.
- `${applicationId}` is substituted. Any other `${...}` placeholder is refused.
- `tools:` attributes and the `xmlns:tools` declaration are removed from the
  result.

Anything beyond that belongs in the project's `manifest_template`.

`tests/apk-consumer-libraries` measures the options together on Linux:

- one signed APK from a Kotlin and Java application, a library from source, an
  AAR and a JAR, with every class dexed, three R packages, the application's
  value for a resource the library also defines, and the merged manifest;
- `sign = false`;
- an App Bundle from the same inputs;
- a manifest conflict refused by name;
- the Maven modes, each refusal included.

**The build host is Linux.** Every payload publishes a macOS table, and the
member's plan runs on macOS, but mcpp 2026.9.14.2 does not link an Android row
on a macOS host: the application's own link fails with `ld64.lld: error: unknown
argument '-soname'` before this member runs, because the engine states the
target triple on that host's link line only for an Apple row
(mcpp-community/mcpp#647, E3). The Windows host has no Android row, because the
Windows NDK carries no libc++ module surface (`xim-pkgindex`,
`pkgs/a/android-ndk.lua`).

### A library states its contribution: `[package.metadata.dist-apk]`

From 0.12.0, with mcpp 2026.9.16.1, a library states in its own manifest what it
contributes to an Android package, and every application that depends on it
carries that without naming the library in its build program:

```toml
# the library's mcpp.toml
[package.metadata.dist-apk]
package        = "org.example.widgets"   # its R package
resources      = "android/res"
manifest       = "android/AndroidManifest.xml"
assets         = "android/assets"
java_sources   = ["android/java"]
kotlin_sources = ["android/kotlin"]
jars           = ["android/libs/support.jar"]
aars           = ["android/libs/player.aar"]
```

Every key is optional, and each has the meaning of the `library` field or the
option of the same name; paths are relative to the library's directory. The
engine hands the application's build program the resolved graph
(`MCPP_GRAPH_FILE`), and `dist-apk` reads the table of every package in it other
than the application. The contributions rank below the application's own
`libraries`, `jars` and `aars`, and a package ranks above the packages it depends
on, so an application's resource overrides a library's and a library's overrides
the framework beneath it. A key of the wrong shape is refused naming the package
and the key; a key this version does not read is reported with a warning and
ignored, so a library may state a key a newer collection reads. Under an older
engine there is no graph, and nothing is contributed. CI checks both engines on
`tests/apk-consumer-graph`.
