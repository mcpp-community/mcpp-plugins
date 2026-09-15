#!/usr/bin/env bash
# dist-apk 0.11.0: Kotlin beside Java, R classes, a library from source, local
# archives, a Maven graph, the manifest merge, and an unsigned package.
#
#   1. The application (a Kotlin activity, a Java helper, its res/), a library
#      from source (Java and Kotlin, res/, a manifest, assets), a local AAR and
#      a local JAR pack into one signed APK. Its dex carries every class of
#      each, the R classes of all three packages and the Kotlin stdlib; its
#      resources carry every string, with the application's value where the
#      library defines the same name; its manifest carries the library's and
#      the archive's permissions and the library's meta-data, not the
#      permission the application removed with tools:node="remove", and no
#      `tools:` attribute; the archive's native library and both assets are in
#      the package, and its library for an ABI the application does not build
#      is not.
#   2. `options::sign = false` writes an aligned package that apksigner does not
#      verify.
#   3. `--format aab` from the same inputs gives an App Bundle bundletool
#      validates, carrying the dex and the archive's native library.
#   4. A library element the application's manifest states differently is
#      refused, naming the element and the library.
#   5. A Maven graph (androidx.startup:startup-runtime:1.1.1): no lock is
#      refused naming MCPP_DIST_APK_MAVEN=update; `update` writes the lock and
#      packs, with the provider's ${applicationId} substituted; an ordinary pack
#      reads the cache, and still does with repositories nothing can reach, which
#      the lock does not require; an empty cache is refused naming
#      MCPP_DIST_APK_MAVEN=fetch; `fetch` restores it and packs; a coordinate the
#      lock was not resolved for is refused as stale.
#
# Usage: MCPP=<mcpp 2026.9.14.2+> ./check-apk-libraries.sh   (run from this directory)
# APK_LIBS_MAVEN_REPOSITORIES may name mirrors for criterion 5.
set -eu

MCPP="${MCPP:-mcpp}"
TARGET=x86_64-linux-android
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
packed() { sed -n 's/^ *Packed //p' "$1" | tail -1; }
unset APK_LIBS_UNSIGNED APK_LIBS_CONFLICT APK_LIBS_MAVEN APK_LIBS_MAVEN_MORE MCPP_DIST_APK_MAVEN || true

"$MCPP" self env > libs-env.log
MCPP_HOME_DIR=$(awk -F'= *' '/^MCPP_HOME/{print $2; exit}' libs-env.log)
[ -n "$MCPP_HOME_DIR" ] || fail "could not read MCPP_HOME" libs-env.log
XPKGS="$MCPP_HOME_DIR/registry/data/xpkgs"
tool() {
    local d
    for d in "$XPKGS/xim-x-$1"/*/; do
        [ -e "$d$2" ] && { echo "$d$2"; return 0; }
    done
    return 1
}

pack() {  # pack <log> [--format aab]
    local log="$1"; shift
    "$MCPP" pack --target "$TARGET" --format "${1:-apk}" > "$log" 2>&1
}

# classes <apk or aab dex root listing> : every class descriptor in the dex files
classes() {  # classes <archive> <out>
    local dir
    dir=$(mktemp -d)
    (cd "$dir" && "$JAR" xf "$OLDPWD/$1")
    : > "$2"
    find "$dir" -name 'classes*.dex' | sort | while read -r dex; do
        "$DEXDUMP" "$dex" 2>/dev/null | sed -n "s/^ *Class descriptor *: '\(.*\)'$/\1/p" >> "$2"
    done
    rm -rf "$dir"
}

rm -rf target maven.lock .coursier-cache

# ── 1. everything, signed ─────────────────────────────────────────────────
echo "== 1. Kotlin, Java, a library, an AAR and a JAR in one signed APK =="
# The archives are built with the JDK the pack provisions, so the first pack
# runs before they exist, to provision it; the member refuses it for want of
# the archives, and the pack after make-archives.sh is the measured one.
pack libs-pack1.log || true
JAR=$(tool jdk-temurin bin/jar) || fail "no jar under $XPKGS/xim-x-jdk-temurin" libs-pack1.log
JAVAC=$(tool jdk-temurin bin/javac) || fail "no javac under $XPKGS/xim-x-jdk-temurin" libs-pack1.log
./make-archives.sh "$JAVAC" "$JAR" target/archives > libs-archives.log 2>&1 || fail "the archives were not built" libs-archives.log
pack libs-pack1.log || fail "the pack exited non-zero" libs-pack1.log
apk=$(packed libs-pack1.log)
[ -f "$apk" ] || fail "the pack reported no APK" libs-pack1.log
AAPT2=$(tool android-build-tools aapt2) || fail "no aapt2"
DEXDUMP=$(tool android-build-tools dexdump) || fail "no dexdump"
APKSIGNER=$(tool android-build-tools bin/apksigner) || fail "no apksigner"
ZIPALIGN=$(tool android-build-tools zipalign) || fail "no zipalign"

"$APKSIGNER" verify "$apk" > libs-verify1.log 2>&1 || fail "apksigner does not verify the default package" libs-verify1.log

classes "$apk" libs-classes1.log
for c in 'Lorg/mcpp/apklibs/MainActivity;' 'Lorg/mcpp/apklibs/JavaHelper;' \
         'Lorg/mcpp/apklibs/lib/LibGreeter;' 'Lorg/mcpp/apklibs/lib/LibKotlin;' \
         'Lorg/mcpp/apklibs/R$string;' 'Lorg/mcpp/apklibs/lib/R$string;' 'Lorg/mcpp/aar/R$string;' \
         'Lorg/mcpp/aar/AarThing;' 'Lorg/mcpp/jar/JarThing;' 'Lkotlin/jvm/internal/Intrinsics;'; do
    grep -qxF "$c" libs-classes1.log || fail "the dex does not carry $c" libs-classes1.log
done
echo "ok: one dex set with the application's Kotlin and Java, the library's Java and Kotlin, three R packages, the archives' classes and the Kotlin stdlib"

"$AAPT2" dump resources "$apk" > libs-res1.log 2>&1
for r in app_greeting lib_greeting aar_greeting shared_name; do
    grep -q "string/$r\$" libs-res1.log || fail "the resources do not carry string/$r" libs-res1.log
done
grep -A1 'string/shared_name$' libs-res1.log | grep -q '"from the application"' \
    || fail "string/shared_name is not the application's value" libs-res1.log
echo "ok: the resources carry every string, and the application's shared_name wins over the library's"

"$AAPT2" dump xmltree "$apk" --file AndroidManifest.xml > libs-xml1.log 2>&1
"$AAPT2" dump permissions "$apk" > libs-perm1.log 2>&1
grep -q "android.permission.VIBRATE" libs-perm1.log || fail "the library's permission was not merged" libs-perm1.log
grep -q "android.permission.WAKE_LOCK" libs-perm1.log || fail "the archive's permission was not merged" libs-perm1.log
grep -q "android.permission.CAMERA" libs-perm1.log && fail "the permission the application removed is in the package" libs-perm1.log
grep -q 'org.mcpp.apklibs.lib.FLAVOUR' libs-xml1.log || fail "the library's meta-data was not merged" libs-xml1.log
grep -q 'tools' target/.build-mcpp/out/dist-apk/AndroidManifest.xml \
    && fail "the merged manifest still carries a tools: attribute" target/.build-mcpp/out/dist-apk/AndroidManifest.xml
echo "ok: the manifest merges the library's and the archive's permissions and meta-data, drops the removed one, and keeps no tools: attribute"

"$JAR" tf "$apk" > libs-list1.log
for f in lib/x86_64/libaar-native.so lib/x86_64/libapk-consumer-libraries.so assets/lib-asset.txt assets/aar-asset.txt; do
    grep -qxF "$f" libs-list1.log || fail "the package does not list $f" libs-list1.log
done
grep -q '^lib/arm64-v8a/' libs-list1.log && fail "the archive's arm64-v8a library was packaged, and the application builds x86_64 alone" libs-list1.log
echo "ok: the archive's native library for the application's ABI, not its other ABI, and both assets are packaged"

# ── 2. unsigned ───────────────────────────────────────────────────────────
echo "== 2. options::sign = false =="
APK_LIBS_UNSIGNED=1 pack libs-pack2.log || fail "the unsigned pack exited non-zero" libs-pack2.log
apk=$(packed libs-pack2.log)
[ -f "$apk" ] || fail "the unsigned pack reported no APK" libs-pack2.log
"$APKSIGNER" verify "$apk" > libs-verify2.log 2>&1 && fail "apksigner verifies a package built with sign = false" libs-verify2.log
"$ZIPALIGN" -c -p 4 "$apk" > libs-align2.log 2>&1 || fail "the unsigned package is not aligned" libs-align2.log
"$JAR" tf "$apk" | grep -Eq '^META-INF/.*\.(RSA|EC|DSA)$' && fail "the unsigned package carries a signature block"
echo "ok: an aligned package with no signature"

# ── 3. an App Bundle ──────────────────────────────────────────────────────
echo "== 3. --format aab from the same inputs =="
pack libs-pack3.log aab || fail "the aab pack exited non-zero" libs-pack3.log
aab=$(packed libs-pack3.log)
[ -f "$aab" ] || fail "the aab pack reported no bundle" libs-pack3.log
BUNDLETOOL=$(tool bundletool bin/bundletool) || fail "no bundletool"
"$BUNDLETOOL" validate --bundle="$aab" > libs-validate3.log 2>&1 || fail "bundletool does not validate the bundle" libs-validate3.log
"$JAR" tf "$aab" > libs-list3.log
grep -qxF base/dex/classes.dex libs-list3.log || fail "the bundle has no base/dex/classes.dex" libs-list3.log
grep -qxF base/lib/x86_64/libaar-native.so libs-list3.log || fail "the bundle lacks the archive's native library" libs-list3.log
echo "ok: the bundle validates and carries the dex and the archive's native library"

# ── 4. a conflicting element ──────────────────────────────────────────────
echo "== 4. a library element the application states differently =="
APK_LIBS_CONFLICT=1 pack libs-pack4.log && fail "a conflicting manifest element was packed" libs-pack4.log
grep -q 'org.mcpp.apklibs.lib.FLAVOUR' libs-pack4.log || fail "the refusal does not name the element" libs-pack4.log
grep -q 'the library org.mcpp.apklibs.lib' libs-pack4.log || fail "the refusal does not name the library" libs-pack4.log
echo "ok: refused, naming the element and the library"

# ── 5. Maven ──────────────────────────────────────────────────────────────
echo "== 5. a Maven graph through its lock =="
export APK_LIBS_MAVEN=1
APK_LIBS_MAVEN=1 pack libs-pack5a.log && fail "a Maven dependency with no lock was packed" libs-pack5a.log
grep -q 'MCPP_DIST_APK_MAVEN=update' libs-pack5a.log || fail "the missing-lock refusal does not name the update" libs-pack5a.log
echo "ok: no lock is refused, naming MCPP_DIST_APK_MAVEN=update"

MCPP_DIST_APK_MAVEN=update pack libs-pack5b.log || fail "MCPP_DIST_APK_MAVEN=update did not pack" libs-pack5b.log
[ -f maven.lock ] || fail "no lock was written" libs-pack5b.log
for c in androidx.startup:startup-runtime:1.1.1 androidx.tracing:tracing:1.0.0 androidx.annotation:annotation:1.1.0; do
    grep -q "^artifact $c " maven.lock || fail "the lock does not record $c" maven.lock
done
apk=$(packed libs-pack5b.log)
classes "$apk" libs-classes5.log
grep -qxF 'Landroidx/startup/InitializationProvider;' libs-classes5.log || fail "the dex does not carry the AAR's provider" libs-classes5.log
"$AAPT2" dump xmltree "$apk" --file AndroidManifest.xml > libs-xml5.log 2>&1
grep -q 'org.mcpp.apklibs.androidx-startup' libs-xml5.log || fail "the provider's \${applicationId} was not substituted" libs-xml5.log
echo "ok: update writes a lock of three artifacts and packs the graph, the provider's authority substituted"

pack libs-pack5c.log || fail "an ordinary pack with a lock and a warm cache failed" libs-pack5c.log
echo "ok: an ordinary pack reads the locked artifacts from the cache"

# A repository nothing can reach: an ordinary pack does not ask it, and the lock
# does not require the repositories it was resolved from.
APK_LIBS_MAVEN_REPOSITORIES="https://repository.invalid/maven2" pack libs-pack5g.log \
    || fail "a lock resolved from other repositories was refused" libs-pack5g.log
echo "ok: other repositories with the same lock and a warm cache pack, reaching none of them"

rm -rf .coursier-cache
pack libs-pack5d.log && fail "a pack with an empty cache succeeded without fetching" libs-pack5d.log
grep -q 'MCPP_DIST_APK_MAVEN=fetch' libs-pack5d.log || fail "the empty-cache refusal does not name the fetch" libs-pack5d.log
MCPP_DIST_APK_MAVEN=fetch pack libs-pack5e.log || fail "MCPP_DIST_APK_MAVEN=fetch did not pack" libs-pack5e.log
echo "ok: an empty cache is refused naming the fetch, and fetch restores exactly the lock"

APK_LIBS_MAVEN_MORE=1 pack libs-pack5f.log && fail "a coordinate the lock was not resolved for was packed" libs-pack5f.log
grep -Eq 'stale|Resolve again with MCPP_DIST_APK_MAVEN=update' libs-pack5f.log || fail "the stale lock is not named" libs-pack5f.log
echo "ok: a coordinate the lock does not record is refused as stale"

echo "PASS: dist-apk's Kotlin, R classes, libraries, archives, Maven graph and unsigned package"
