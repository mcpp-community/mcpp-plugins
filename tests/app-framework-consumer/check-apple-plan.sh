#!/usr/bin/env bash
# Plan-level check for dist-apple's closure, signing, runner and disk image on
# the macOS row (mcpp#634, B1 to B3).
#
# The real bundle is measured on the macOS runner. This script measures, on any
# host, what the compiled build program PLANS under the environment the engine
# sets for `mcpp pack --format app` and `--format dmg` on macOS: the build
# program's target accessors read that environment and nothing else, so a
# process given the same variables takes the branches a real pack takes. The
# staged tree and its stage manifest are fabricated in the shape the engine
# writes (mcpp's docs/50, "The stage manifest").
#
# Usage: MCPP=<mcpp> ./check-apple-plan.sh   (run from this directory)
set -e

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
command -v python3 >/dev/null || fail "python3 is required to read the planned actions"

rm -rf target
"$MCPP" build > build.log 2>&1 || fail "the host build failed to compile build.mcpp" build.log
BIN=target/.build-mcpp/build.mcpp.bin
[ -x "$BIN" ] || fail "no compiled build.mcpp.bin at $BIN" build.log

# stage_tree <closure> : a staged tree with the program, its dylib and a
# deployed resource, and the stage manifest the engine writes for it.
stage_tree() {
    local stage
    stage=$(mktemp -d)
    mkdir -p "$stage/bin/data"
    head -c 5000 /dev/urandom > "$stage/bin/app-framework-consumer"
    head -c 4000 /dev/urandom > "$stage/bin/libapp-framework-dep.dylib"
    echo hello > "$stage/bin/data/greeting.txt"
    {
        printf 'closure = %s\n' "$1"
        if [ "$1" = not-walked ]; then
            printf 'reason = the loader finds no file for @rpath/libmissing.dylib\n'
        fi
        printf 'needs\t/usr/lib/libSystem.B.dylib\tplatform\n'
        printf 'needs\t@rpath/libapp-framework-dep.dylib\tbin/libapp-framework-dep.dylib\n'
        if [ "$1" = not-walked ]; then
            printf 'needs\t@rpath/libmissing.dylib\tunresolved\n'
        fi
        printf '5000 bin/app-framework-consumer\n'
        printf '4000 bin/libapp-framework-dep.dylib\n'
    } > "$stage.stage-manifest"
    echo "$stage"
}

# run_program <format> <os> <env> <stage> <log>
run_program() {
    local out
    out=$(mktemp -d)
    env -i PATH="$PATH" \
        MCPP_PACK_FORMAT="$1" MCPP_TARGET_OS="$2" MCPP_TARGET_ENV="$3" \
        MCPP_PACK_STAGE_DIR="$4" MCPP_MANIFEST_DIR="$PWD" \
        MCPP_PKG_NAME=app-framework-consumer MCPP_PKG_VERSION=0.1.0 \
        MCPP_OUT_DIR="$out" \
        "$BIN" > "$5" 2>&1 || true
}

# check <log> <python assertions reading `actions` (id -> action) and `lines`>
check() {
    python3 - "$1" "$2" <<'PY' || exit 1
import json, sys
log, code = sys.argv[1], sys.argv[2]
lines = open(log).read().splitlines()
actions = {}
for l in lines:
    if l.startswith("mcpp:action="):
        a = json.loads(l[len("mcpp:action="):])
        actions[a["id"]] = a
def fail(msg):
    print("FAIL: " + msg)
    print("--- " + log + " ---")
    print("\n".join(lines))
    sys.exit(1)
exec(code)
PY
}

echo "== --format app on macOS, with a staged dylib =="
stage=$(stage_tree walked)
run_program app macos "" "$stage" /tmp/apple-plan-app.log
check /tmp/apple-plan-app.log '
fw = actions.get("mcpp.dist.apple.framework.libapp-framework-dep.dylib")
if not fw: fail("no framework step for the staged dylib")
if not fw["outputs"] or not fw["outputs"][0].endswith("/AppFrameworkConsumer.app/Contents/Frameworks/libapp-framework-dep.dylib"):
    fail("the framework step does not write Contents/Frameworks/libapp-framework-dep.dylib")
if fw["command"][-2:] != ["sign", "-"]:
    fail("the framework step does not sign ad hoc: " + repr(fw["command"]))
if any(i.startswith("mcpp.dist.apple.resource.libapp") for i in actions):
    fail("the staged dylib is also planned as a resource")
if "mcpp.dist.apple.resource.data" not in actions:
    fail("the deployed resource is not planned")
sign = actions.get("mcpp.dist.apple.codesign")
if not sign: fail("no codesign step on macOS without an identity")
if sign["command"][:4] != ["codesign", "--force", "--sign", "-"] or "--timestamp" in sign["command"]:
    fail("the bundle is not signed ad hoc: " + repr(sign["command"]))
if fw["outputs"][0] not in sign["inputs"]:
    fail("the bundle signature does not follow the framework")
bundle = actions.get("mcpp.dist.apple.bundle")
if not bundle or sign["outputs"][0] not in bundle["inputs"]:
    fail("the bundle step does not follow the signature")
if any(k.startswith("mcpp.dist.apple.dmg") for k in actions):
    fail("--format app planned a disk image")
if "mcpp:link-flag=-Wl,-rpath,@executable_path/../Frameworks" not in lines:
    fail("no framework rpath was emitted for the link")
if "mcpp:runner-named=app:macapp-run" not in lines:
    fail("the app runner was not supplied")
'
echo "ok: the dylib is a framework signed ad hoc, not a resource; the bundle is signed after it; the rpath and the runner are declared"

echo "== --format dmg on macOS =="
run_program dmg macos "" "$stage" /tmp/apple-plan-dmg.log
check /tmp/apple-plan-dmg.log '
bundle = actions.get("mcpp.dist.apple.bundle")
st = actions.get("mcpp.dist.apple.dmg-stage")
img = actions.get("mcpp.dist.apple.dmg")
if not (bundle and st and img): fail("the disk image steps are not planned")
if bundle["outputs"][0] not in st["inputs"]: fail("the image staging does not take the bundle")
if st["outputs"][0] not in img["inputs"]: fail("hdiutil does not take the staging directory")
cmd = img["command"]
if cmd[:2] != ["hdiutil", "create"] or "UDZO" not in cmd or not cmd[-1].endswith("/AppFrameworkConsumer.dmg"):
    fail("the image command is not hdiutil create -format UDZO ... AppFrameworkConsumer.dmg: " + repr(cmd))
consumed = set(i for a in actions.values() for i in a["inputs"])
terminals = [a["id"] for a in actions.values() if not set(a["outputs"]) & consumed]
if terminals != ["mcpp.dist.apple.dmg"]:
    fail("the image is not the sole terminal artifact: " + repr(terminals))
'
echo "ok: the bundle is staged beside the link and the .dmg is the sole terminal artifact"

echo "== an incomplete closure is reported =="
stage2=$(stage_tree not-walked)
run_program app macos "" "$stage2" /tmp/apple-plan-notwalked.log
check /tmp/apple-plan-notwalked.log '
if not any(l.startswith("mcpp:warning=") and "libmissing.dylib" in l and "closure is incomplete" in l for l in lines):
    fail("the incomplete closure is not reported, naming the unresolved library")
if "mcpp.dist.apple.bundle" not in actions:
    fail("an incomplete closure stopped the bundle")
'
echo "ok: an incomplete closure is a warning naming the library, and the bundle is still planned"

echo "== --format dmg on iOS is refused =="
run_program dmg ios sim "$stage" /tmp/apple-plan-dmg-ios.log
check /tmp/apple-plan-dmg-ios.log '
if actions: fail("an iOS disk image planned actions")
if not any(l.startswith("mcpp:warning=") and "a .dmg is a macOS disk image" in l for l in lines):
    fail("the refusal is not a warning")
'
echo "ok: an iOS disk image is refused, as a warning"

echo "== the iOS simulator row =="
run_program app ios sim "$stage" /tmp/apple-plan-ios.log
check /tmp/apple-plan-ios.log '
fw = actions.get("mcpp.dist.apple.framework.libapp-framework-dep.dylib")
if not fw or not fw["outputs"][0].endswith("/AppFrameworkConsumer.app/Frameworks/libapp-framework-dep.dylib"):
    fail("the simulator bundle does not carry the dylib in Frameworks/")
if fw["command"][-1] != "nosign": fail("the simulator framework is signed")
if "mcpp.dist.apple.codesign" in actions: fail("the simulator bundle is signed")
if "mcpp:link-flag=-Wl,-rpath,@executable_path/Frameworks" not in lines:
    fail("no iOS framework rpath was emitted")
if any(l.startswith("mcpp:runner-named=app:") for l in lines):
    fail("the macOS runner was supplied on iOS")
'
echo "ok: the simulator bundle carries Frameworks/, unsigned, with the iOS rpath and no macOS runner"

echo "PASS: dist-apple's closure, signing, runner and disk image, at the plan level"
