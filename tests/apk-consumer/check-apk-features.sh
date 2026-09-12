#!/usr/bin/env bash
# End-to-end checks for dist-apk's manifest template (design record
# `2026-09-13-four-upstream-asks-from-a-ui-framework.md`, §3.2 / §9.2 P1) and
# Java-array (§3.3 / P2) changes, criteria (a) to (e) of §3.5 read against
# this fixture. `build.mcpp` here reads two environment variables this
# script sets to reach each configuration without a second fixture -- see
# its own header.
#
# Usage: MCPP=<mcpp 2026.9.13.1+> ./check-apk-features.sh   (run from this
# directory, after `tests/apk-consumer`'s own level-0 CI step, whose target/
# this script clears and rebuilds itself)
set -e

MCPP="${MCPP:-mcpp}"
TARGET=x86_64-linux-android
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }

"$MCPP" self env > mcpp-env.txt
MCPP_HOME_DIR=$(awk -F'= *' '/^MCPP_HOME/{print $2; exit}' mcpp-env.txt)
[ -n "$MCPP_HOME_DIR" ] || fail "could not read MCPP_HOME" mcpp-env.txt
BT=$(find "$MCPP_HOME_DIR/registry/data/xpkgs/xim-x-android-build-tools" -mindepth 1 -maxdepth 1 -type d | head -1)
AAPT2="$BT/aapt2"
DEXDUMP="$BT/dexdump"
[ -x "$AAPT2" ] || fail "aapt2 not found under $BT"
[ -x "$DEXDUMP" ] || fail "dexdump not found under $BT"

# ── (a) level 0, no template: byte-identical to 0.8.0's manifest ───────────
echo "== (a) level 0, no template =="
rm -rf target
unset APK_CONSUMER_TEMPLATE APK_CONSUMER_LEVEL1 || true
"$MCPP" build --target "$TARGET" > build-a.log 2>&1 || fail "build failed" build-a.log
"$MCPP" pack --format apk --target "$TARGET" > pack-a.log 2>&1 || fail "pack failed" pack-a.log
MANIFEST=target/.build-mcpp/out/dist-apk/AndroidManifest.xml
cmp fixtures/expected-manifest-level0-0.8.0.xml "$MANIFEST" \
    || fail "level 0's manifest is not byte-identical to 0.8.0's" "$MANIFEST" fixtures/expected-manifest-level0-0.8.0.xml
echo "ok: level 0 with no template renders 0.8.0's manifest byte-for-byte"

# ── (b) a template with uses-permission and a receiver ─────────────────────
echo "== (b) a template with uses-permission and receiver =="
rm -rf target
export APK_CONSUMER_TEMPLATE=manifest-template-good.xml
"$MCPP" build --target "$TARGET" > build-b.log 2>&1 || fail "build failed" build-b.log
"$MCPP" pack --format apk --target "$TARGET" > pack-b.log 2>&1 || fail "pack failed" pack-b.log
APK=$(find target -name 'apk-consumer.apk' | head -1)
[ -n "$APK" ] || fail "no apk-consumer.apk" pack-b.log
"$AAPT2" dump xmltree "$APK" --file AndroidManifest.xml > xmltree-b.log 2>&1
grep -q 'android.permission.INTERNET' xmltree-b.log || fail "uses-permission missing from the linked apk" xmltree-b.log
grep -q 'org.mcpp.apkconsumer.SampleReceiver' xmltree-b.log || fail "receiver missing from the linked apk" xmltree-b.log
echo "ok: aapt2 dump xmltree on the linked base.apk lists both the permission and the receiver"

# ── (c),(d): plan-time refusals name the token ──────────────────────────────
#
# mcpp discards a build program's captured stdout/stderr when it exits 0
# (`build_program.cppm`: the capture is surfaced only on a non-zero exit or a
# timeout) -- and a `plan_for` refusal in this member never makes build.mcpp
# exit non-zero, `submit()` returns true for `applies=false` exactly as
# every other refusal in this member does (design record §3.2, "printing to
# stderr exactly as its other refusals do"). So the refusal message itself
# is checked by re-invoking the ALREADY-COMPILED build.mcpp binary directly,
# with the documented MCPP_* build-program contract (docs/30) reconstructed
# from the real payloads a prior build just resolved. Every variable set
# below is one docs/30 already publishes; the compiled binary's own path is
# the only thing this script assumes about mcpp's own layout.
echo "== (c),(d) plan-time refusals name the token =="
BIN=target/.build-mcpp/build.mcpp.bin
[ -x "$BIN" ] || fail "no compiled build.mcpp to re-invoke" pack-b.log
STAGE_DIR=$(find target/dist -mindepth 1 -maxdepth 1 -type d | head -1)
[ -n "$STAGE_DIR" ] || fail "no staged tree from the prior pack" pack-b.log
PLATFORM=$(find "$MCPP_HOME_DIR/registry/data/xpkgs/xim-x-android-platform" -mindepth 1 -maxdepth 1 -type d | head -1)
KEYSTORE=$(find "$MCPP_HOME_DIR/registry/data/xpkgs/xim-x-android-debug-keystore" -mindepth 1 -maxdepth 1 -type d | head -1)
JDK=$(find "$MCPP_HOME_DIR/registry/data/xpkgs/xim-x-jdk-temurin" -mindepth 1 -maxdepth 1 -type d | head -1)

run_build_program() {
    env -i \
        MCPP_TARGET_ARCH=x86_64 MCPP_TARGET_ENV=android \
        MCPP_TARGET_MIN_PLATFORM_VERSION=24 \
        MCPP_OUT_DIR="$PWD/target/.build-mcpp/out" \
        MCPP_MANIFEST_DIR="$PWD" \
        MCPP_PKG_NAME=apk-consumer MCPP_PKG_VERSION=0.3.0 \
        MCPP_PACK_FORMAT=apk MCPP_PACK_STAGE_DIR="$STAGE_DIR" \
        MCPP_XPKG_XIM_ANDROID_BUILD_TOOLS_DIR="$BT" \
        MCPP_XPKG_XIM_ANDROID_PLATFORM_DIR="$PLATFORM" \
        MCPP_XPKG_XIM_ANDROID_DEBUG_KEYSTORE_DIR="$KEYSTORE" \
        MCPP_XPKG_XIM_JDK_TEMURIN_DIR="$JDK" \
        APK_CONSUMER_TEMPLATE="$1" \
        "$BIN" > "$2" 2>&1
}

run_build_program manifest-template-missing-appid.xml refusal-c.log
grep -qF "does not use '{{application_id}}'" refusal-c.log \
    || fail "the missing-token refusal does not name application_id" refusal-c.log
grep -qF "assets/mcpp-run.json" refusal-c.log \
    || fail "the missing-token refusal does not name assets/mcpp-run.json" refusal-c.log
echo "ok: a template missing {{application_id}} is refused, naming the token and assets/mcpp-run.json"

run_build_program manifest-template-bogus-token.xml refusal-d.log
grep -qF "unknown token '{{bogus}}'" refusal-d.log \
    || fail "the unknown-token refusal does not name bogus" refusal-d.log
echo "ok: a template with {{bogus}} is refused, naming the token"

# Functional confirmation through the ordinary CLI: `mcpp pack` itself fails
# when the manifest template is bad, because dist-apk declared the format
# and submitted no artifact action for it.
export APK_CONSUMER_TEMPLATE=manifest-template-missing-appid.xml
if "$MCPP" pack --format apk --target "$TARGET" > pack-c.log 2>&1; then
    fail "mcpp pack succeeded with a manifest template missing a required token" pack-c.log
fi
grep -q "no action claimed" pack-c.log || fail "mcpp pack did not refuse the bad template" pack-c.log
echo "ok: mcpp pack itself refuses (no action claimed for --format apk)"
unset APK_CONSUMER_TEMPLATE

# ── (e) two Java roots produce one classes.dex with classes from both ─────
echo "== (e) two Java roots =="
rm -rf target
export APK_CONSUMER_LEVEL1=1
"$MCPP" build --target "$TARGET" > build-e.log 2>&1 || fail "build failed" build-e.log
"$MCPP" pack --format apk --target "$TARGET" > pack-e.log 2>&1 || fail "pack failed" pack-e.log
DEX=$(find target -name 'classes.dex' | head -1)
[ -n "$DEX" ] || fail "no classes.dex" pack-e.log
"$DEXDUMP" -l plain "$DEX" > dexdump-e.log 2>&1 || fail "dexdump failed" dexdump-e.log
grep -q "org.mcpp.apkconsumer.MainActivity;" dexdump-e.log \
    || fail "classes.dex does not contain MainActivity (the project's own root)" dexdump-e.log
grep -q "org.mcpp.apkconsumer.ExternalHelper;" dexdump-e.log \
    || fail "classes.dex does not contain ExternalHelper (the external root)" dexdump-e.log
echo "ok: one classes.dex, carrying classes from both the project root and the external root"

# ── (f) the re-run direction, both ways (§3.3's third bullet) ──────────────
#
# THE EVIDENCE IS THE CACHE'S OWN RECORDED GLOB FINGERPRINT, NOT DEX CONTENT
# ALONE. Measured on this engine: `mcpp pack` re-runs build.mcpp on this
# fixture on every invocation regardless of any cache -- true before this
# fix and after it, and unrelated to the glob -- so a dex-content check by
# itself cannot tell "the fingerprint tracks the real file set" apart from
# "build.mcpp always reran anyway"; both give the same dex either way. What
# DOES distinguish them is the hash `build.mcpp.cache` records for the
# `java/**/*.java` pattern: with the pattern absolute (this defect, fixed by
# this same commit), the recorded hash is the empty set's and does not
# change when a `.java` file is added or removed, because
# `path_matches_glob` (`modules/manifest/src/glob.cppm`) compares the
# pattern against each candidate made relative to the package root, and an
# absolute pattern matches no relative candidate. With the pattern
# manifest-relative, the hash changes with the real file set. Both readings
# were taken locally against the pre-fix code and are recorded in this
# pull request's own body.
echo "== (f) the re-run direction: project root vs external root =="
CACHE=target/.build-mcpp/build.mcpp.cache
[ -f "$CACHE" ] || fail "no build.mcpp.cache after (e)'s pack" pack-e.log
glob_hash() { grep -m1 -E "java/\*\*/\*\.java$" "$CACHE" | grep -v external | awk '{print $2}'; }
before=$(glob_hash)
[ -n "$before" ] || fail "no relative java/**/*.java glob recorded in the cache" "$CACHE"
grep -E "^glob " "$CACHE" | grep -q "apk-consumer-external-java" && \
    fail "a glob was recorded for the external root, which this design never declares one for" "$CACHE"
echo "ok: no glob is recorded for the external root"

cat > java/org/mcpp/apkconsumer/AddedLater.java <<'JAVA'
package org.mcpp.apkconsumer;
public class AddedLater { public static int marker() { return 1; } }
JAVA
"$MCPP" pack --format apk --target "$TARGET" > pack-f1.log 2>&1 \
    || fail "pack failed after adding a project-root .java file" pack-f1.log
after=$(glob_hash)
[ "$before" != "$after" ] \
    || fail "the project root's glob fingerprint did not change when a .java file was added" "$CACHE"
echo "ok: the project root's glob fingerprint changed ($before -> $after)"
DEX=$(find target -name 'classes.dex' | head -1)
"$DEXDUMP" -l plain "$DEX" > dexdump-f1.log 2>&1 || fail "dexdump failed" dexdump-f1.log
grep -q "org.mcpp.apkconsumer.AddedLater;" dexdump-f1.log \
    || fail "classes.dex does not contain AddedLater after it was added under the project root" dexdump-f1.log
echo "ok: classes.dex contains AddedLater once it is added under the project root"
rm -f java/org/mcpp/apkconsumer/AddedLater.java

# The external root: added here only to show the positive side of §3.3's
# reading (the file is compiled once a rebuild happens, exactly as any
# other input the javac action already declares) -- NOT to claim this
# specific `pack` skipped a rebuild, which the header above explains this
# engine does not do for this fixture regardless of the glob.
cat > ../apk-consumer-external-java/org/mcpp/apkconsumer/AddedExternally.java <<'JAVA'
package org.mcpp.apkconsumer;
public class AddedExternally { public static int marker() { return 2; } }
JAVA
"$MCPP" pack --format apk --target "$TARGET" > pack-f2.log 2>&1 \
    || fail "pack failed after adding an external-root .java file" pack-f2.log
grep -E "^glob " "$CACHE" | grep -q "apk-consumer-external-java" && \
    fail "a glob was recorded for the external root after adding a file to it" "$CACHE"
DEX=$(find target -name 'classes.dex' | head -1)
"$DEXDUMP" -l plain "$DEX" > dexdump-f2.log 2>&1 || fail "dexdump failed" dexdump-f2.log
grep -q "org.mcpp.apkconsumer.AddedExternally;" dexdump-f2.log \
    || fail "classes.dex does not contain AddedExternally" dexdump-f2.log
echo "ok: still no glob recorded for the external root, after adding a file to it"
rm -f ../apk-consumer-external-java/org/mcpp/apkconsumer/AddedExternally.java

unset APK_CONSUMER_LEVEL1

rm -f build-*.log pack-*.log xmltree-*.log refusal-*.log dexdump-*.log mcpp-env.txt
echo "PASS: dist-apk's manifest template and Java-array criteria (a) to (f)"
