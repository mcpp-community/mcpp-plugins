#!/usr/bin/env bash
# dist-apk reads the native closure the engine staged (mcpp#634, B5).
#
# Five criteria. The first four run `mcpp pack` and read the package with the
# Android tools; the fifth runs the compiled build program against a fabricated
# stage, so it holds whichever engine packs.
#
#   1. Two packs in a row both carry the dependency's library. Under 0.9.3 the
#      second APK lacked it: the member walked the object's NEEDED entries
#      behind a stamp that outlived the stage the plan wipes, so the walk did
#      not run again.
#   2. Two `--target` rows give one APK whose native code lists both ABIs, each
#      with the application object, the dependency and `libc++_shared.so`.
#      Under 0.9.3 this pack exited 1: the member did not descend into
#      `lib/<abi>/`.
#   3. `--format aab` gives an App Bundle that bundletool validates, that
#      jarsigner verifies, and from which bundletool builds a universal APK
#      carrying the same libraries.
#   4. A refusal reaches the user: `--format apk` for the host row prints the
#      member's reason. Under 0.9.3 the reason went to the build program's
#      stderr, which the engine discards when the program exits 0, and the
#      user read only "no action claimed".
#   5. A stage manifest without `needs` lines, which every engine below
#      2026.9.14.2 writes, is refused through `mcpp::warning`, naming that
#      floor.
#
# Usage: MCPP=<mcpp 2026.9.14.2+> ./check-apk-closure.sh   (run from this directory)
set -eu

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
# The path of the artifact a pack reports on its `Packed` line.
packed() { sed -n 's/^ *Packed //p' "$1" | tail -1; }

"$MCPP" self env > mcpp-env.txt
MCPP_HOME_DIR=$(awk -F'= *' '/^MCPP_HOME/{print $2; exit}' mcpp-env.txt)
[ -n "$MCPP_HOME_DIR" ] || fail "could not read MCPP_HOME" mcpp-env.txt
XPKGS="$MCPP_HOME_DIR/registry/data/xpkgs"

# tool <package> <path under the package> : the file in whichever installed
# version carries it.
tool() {
    local d
    for d in "$XPKGS/xim-x-$1"/*/; do
        [ -e "$d$2" ] && { echo "$d$2"; return 0; }
    done
    return 1
}

# lists <archive> <listing file> : the member names of a zip archive.
lists() { "$JAR" tf "$1" > "$2"; }

# ── 1. two packs in a row ──────────────────────────────────────────────────
echo "== 1. two packs in a row carry the dependency's library =="
rm -rf target
for n in 1 2; do
    "$MCPP" pack --target x86_64-linux-android --format apk > "closure-pack$n.log" 2>&1 \
        || fail "pack $n exited non-zero" "closure-pack$n.log"
    apk=$(packed "closure-pack$n.log")
    [ -f "$apk" ] || fail "pack $n reported no APK" "closure-pack$n.log"
    if [ "$n" = 1 ]; then
        JAR=$(tool jdk-temurin bin/jar) || fail "no jar under $XPKGS/xim-x-jdk-temurin"
        JARSIGNER=$(tool jdk-temurin bin/jarsigner) || fail "no jarsigner under $XPKGS/xim-x-jdk-temurin"
        AAPT2=$(tool android-build-tools aapt2) || fail "no aapt2 under $XPKGS/xim-x-android-build-tools"
        APKSIGNER=$(tool android-build-tools bin/apksigner) || fail "no apksigner under $XPKGS/xim-x-android-build-tools"
    fi
    lists "$apk" "closure-list$n.log"
    for f in lib/x86_64/libapk-consumer-shared.so lib/x86_64/libapk-consumer-dep.so \
             lib/x86_64/libc++_shared.so; do
        grep -qxF "$f" "closure-list$n.log" || fail "the APK of pack $n does not list $f" "closure-list$n.log"
    done
    echo "ok: pack $n: $(grep -c '^lib/' "closure-list$n.log") native libraries, the dependency's among them"
done

# ── 2. two triples, one APK ────────────────────────────────────────────────
echo "== 2. two triples give one APK listing both ABIs =="
"$MCPP" pack --target x86_64-linux-android --target aarch64-linux-android --format apk \
    > closure-multi.log 2>&1 || fail "the two-triple pack exited non-zero" closure-multi.log
apk=$(packed closure-multi.log)
[ -f "$apk" ] || fail "the two-triple pack reported no APK" closure-multi.log
lists "$apk" closure-multi-list.log
for abi in x86_64 arm64-v8a; do
    for f in libapk-consumer-shared.so libapk-consumer-dep.so libc++_shared.so; do
        grep -qxF "lib/$abi/$f" closure-multi-list.log \
            || fail "the two-triple APK does not list lib/$abi/$f" closure-multi-list.log
    done
done
"$AAPT2" dump badging "$apk" > closure-badging.log 2>&1 || fail "aapt2 could not read the APK" closure-badging.log
tr -d '\r' < closure-badging.log | grep -qx "native-code: 'arm64-v8a' 'x86_64'" \
    || fail "aapt2 does not report native-code 'arm64-v8a' 'x86_64'" closure-badging.log
"$APKSIGNER" verify "$apk" > closure-verify.log 2>&1 || fail "apksigner does not verify the APK" closure-verify.log
echo "ok: one signed APK, $(tr -d '\r' < closure-badging.log | grep '^native-code:')"

# ── 3. an App Bundle ───────────────────────────────────────────────────────
echo "== 3. --format aab =="
"$MCPP" pack --target x86_64-linux-android --format aab > closure-aab.log 2>&1 \
    || fail "the aab pack exited non-zero" closure-aab.log
aab=$(packed closure-aab.log)
case "$aab" in *.aab) ;; *) fail "the aab pack reported '$aab', not an .aab" closure-aab.log ;; esac
[ -f "$aab" ] || fail "the reported bundle $aab does not exist" closure-aab.log
BUNDLETOOL=$(tool bundletool bin/bundletool) || fail "no bundletool under $XPKGS/xim-x-bundletool"
lists "$aab" closure-aab-list.log
for f in BundleConfig.pb base/manifest/AndroidManifest.xml base/resources.pb \
         base/lib/x86_64/libapk-consumer-shared.so base/lib/x86_64/libapk-consumer-dep.so \
         base/lib/x86_64/libc++_shared.so base/assets/mcpp-run.json; do
    grep -qxF "$f" closure-aab-list.log || fail "the bundle does not list $f" closure-aab-list.log
done
"$BUNDLETOOL" validate --bundle="$aab" > closure-validate.log 2>&1 \
    || fail "bundletool validate refused the bundle" closure-validate.log
"$JARSIGNER" -verify "$aab" > closure-jarsigner.log 2>&1 || fail "jarsigner -verify failed" closure-jarsigner.log
grep -q '^jar verified' closure-jarsigner.log || fail "jarsigner did not report 'jar verified'" closure-jarsigner.log
work=$(mktemp -d)
"$BUNDLETOOL" build-apks --bundle="$aab" --output="$work/universal.apks" --mode=universal \
    > closure-build-apks.log 2>&1 || fail "bundletool build-apks --mode=universal failed" closure-build-apks.log
(cd "$work" && "$JAR" xf universal.apks universal.apk) || fail "the .apks set carries no universal.apk"
lists "$work/universal.apk" closure-universal-list.log
grep -qxF lib/x86_64/libapk-consumer-dep.so closure-universal-list.log \
    || fail "the universal APK does not carry the dependency's library" closure-universal-list.log
echo "ok: the bundle validates, is signed, and yields a universal APK with the dependency's library"

# ── 4. a refusal reaches the user ──────────────────────────────────────────
echo "== 4. a refusal prints its reason =="
rc=0
"$MCPP" pack --format apk > closure-refusal.log 2>&1 || rc=$?
[ "$rc" -ne 0 ] || fail "an APK for the host row was not refused" closure-refusal.log
grep -q 'mcpp.dist.apk: an APK is an Android format' closure-refusal.log \
    || fail "the refusal's reason is not in mcpp pack's output (exit $rc)" closure-refusal.log
echo "ok: exit $rc, and the output names the reason: $(grep -m1 'mcpp.dist.apk:' closure-refusal.log)"

# ── 5. an engine below the floor ───────────────────────────────────────────
#
# The engine's output is not read here, so the build program is run with the
# environment a pack sets (mcpp's docs/30) and a stage manifest in the form an
# engine below 2026.9.14.2 writes: header and file list, no `needs` line.
echo "== 5. a stage manifest without needs lines is refused, naming the floor =="
BIN=target/.build-mcpp/build.mcpp.bin
[ -x "$BIN" ] || fail "no compiled build.mcpp to run" closure-refusal.log
stage="$work/apk-consumer-shared-0.2.0-x86_64-linux-android"
mkdir -p "$stage/lib"
head -c 4096 /dev/zero > "$stage/lib/libapk-consumer-shared.so"
printf 'closure = not-walked\nreason = the Android closure is not read\n4096 lib/libapk-consumer-shared.so\n' \
    > "$stage.stage-manifest"
env -i PATH="$PATH" \
    MCPP_TARGET_ARCH=x86_64 MCPP_TARGET_OS=linux MCPP_TARGET_ENV=android \
    MCPP_OUT_DIR="$work/out" MCPP_MANIFEST_DIR="$PWD" \
    MCPP_PKG_NAME=apk-consumer-shared MCPP_PKG_VERSION=0.2.0 \
    MCPP_PACK_FORMAT=apk MCPP_PACK_STAGE_DIR="$stage" \
    "$BIN" > closure-floor.log 2>&1 || true
if grep -q '^mcpp:action=' closure-floor.log; then
    fail "a stage without needs lines planned actions" closure-floor.log
fi
grep '^mcpp:warning=' closure-floor.log | grep -q 'no `needs` line.*2026\.9\.14\.2 or newer' \
    || fail "the refusal is not a warning naming mcpp 2026.9.14.2" closure-floor.log
echo "ok: refused, as a warning naming the engine floor"

echo "PASS: dist-apk reads the staged closure, packs two ABIs into one APK, builds an App Bundle, and reports its refusals"
