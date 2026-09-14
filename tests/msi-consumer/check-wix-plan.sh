#!/usr/bin/env bash
# Plan-level check for dist-wix's `msi` and `setup` formats (mcpp#634, B4).
#
# The bundle is built on the Windows runner (`check-setup.sh`). This script runs
# on any host: it runs the compiled build program under the environment the
# engine sets for `mcpp pack --format msi` and `--format setup` on a Windows
# target, with a fabricated `xim:wix` directory that holds the two files the
# member looks for, and reads what it plans and writes.
#
#   1. `--format setup` plans the MSI and a bundle action whose inputs include
#      the MSI, so the bundle is the only terminal artifact, and whose command
#      loads the extension and names the MSI as the variable `Msi`.
#   2. Both generated documents are well-formed XML. `wix build` refused a
#      bundle document whose comment held `--` (WIX0104, windows-2022), and
#      an XML parser refuses the same document on every host.
#   3. `--format msi` plans no bundle.
#   4. A bundle named `setup.exe` is refused before `wix` runs, as a warning.
#
# Usage: MCPP=<mcpp> ./check-wix-plan.sh   (run from this directory)
set -eu

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
command -v python3 > /dev/null || fail "python3 is required to read the planned actions"

rm -rf target
"$MCPP" build > wix-plan-build.log 2>&1 || fail "the host build failed to compile build.mcpp" wix-plan-build.log
BIN=target/.build-mcpp/build.mcpp.bin
[ -x "$BIN" ] || fail "no compiled build.mcpp.bin at $BIN" wix-plan-build.log

work=$(mktemp -d)
wix="$work/xpkg-wix"
mkdir -p "$wix/tool/tools/net6.0/any" "$wix/bal/wixext5"
: > "$wix/tool/tools/net6.0/any/wix.exe"
: > "$wix/bal/wixext5/WixToolset.BootstrapperApplications.wixext.dll"

# run_program <format> <out dir> <log> [bundle output]
run_program() {
    mkdir -p "$2"
    env -i PATH="$PATH" \
        MCPP_PACK_FORMAT="$1" MCPP_TARGET_OS=windows MCPP_TARGET_ARCH=x86_64 \
        MCPP_TARGET_ENV=msvc MCPP_PACK_STAGE_DIR="" MCPP_MANIFEST_DIR="$PWD" \
        MCPP_PKG_NAME=msi-consumer MCPP_PKG_NAMESPACE= MCPP_PKG_VERSION=0.3.0 \
        MCPP_PKG_AUTHORS=mcpp-community MCPP_OUT_DIR="$2" \
        MCPP_XPKG_XIM_WIX_DIR="$wix" MSI_CONSUMER_BUNDLE_OUTPUT="${4:-}" \
        "$BIN" > "$3" 2>&1 || true
}

# check <log> <python assertions over `actions` (id -> action), `lines`, `out`>
check() {
    python3 - "$1" "$2" "$3" <<'PY' || exit 1
import json, sys
log, out, code = sys.argv[1], sys.argv[2], sys.argv[3]
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

echo "== 1, 2. --format setup =="
run_program setup "$work/setup" "$work/setup.log"
check "$work/setup.log" "$work/setup" '
import xml.dom.minidom, os
msi = actions.get("mcpp.dist.wix")
bundle = actions.get("mcpp.dist.wix.bundle")
if not msi or not bundle: fail("the MSI and the bundle are not both planned: " + repr(sorted(actions)))
if msi["outputs"] != [out + "/MsiConsumer-x64.msi"]: fail("the MSI is not MsiConsumer-x64.msi: " + repr(msi["outputs"]))
if bundle["outputs"] != [out + "/MsiConsumer-x64.exe"]: fail("the bundle is not MsiConsumer-x64.exe: " + repr(bundle["outputs"]))
if msi["outputs"][0] not in bundle["inputs"]: fail("the bundle does not take the MSI as an input")
cmd = bundle["command"]
ext = "WixToolset.BootstrapperApplications.wixext.dll"
if "-ext" not in cmd or not cmd[cmd.index("-ext") + 1].endswith(ext): fail("the bundle command loads no extension: " + repr(cmd))
if "-d" not in cmd or cmd[cmd.index("-d") + 1] != "Msi=" + msi["outputs"][0]: fail("the bundle command does not name the MSI as Msi: " + repr(cmd))
consumed = set(i for a in actions.values() for i in a["inputs"])
terminals = [a["id"] for a in actions.values() if not set(a["outputs"]) & consumed]
if terminals != ["mcpp.dist.wix.bundle"]: fail("the bundle is not the sole terminal artifact: " + repr(terminals))
for name in ("MsiConsumer.wxs", "MsiConsumer-bundle.wxs"):
    path = os.path.join(out, name)
    if not os.path.isfile(path): fail("no generated " + name)
    try:
        doc = xml.dom.minidom.parse(path)
    except Exception as e:
        fail(name + " is not well-formed XML: " + str(e))
bundle_doc = open(os.path.join(out, "MsiConsumer-bundle.wxs")).read()
if "bal:WixStandardBootstrapperApplication" not in bundle_doc or "<MsiPackage SourceFile=\"$(Msi)\" />" not in bundle_doc:
    fail("the bundle document does not chain $(Msi) under the stock bootstrapper application")
'
echo "ok: the bundle follows the MSI and is the sole terminal artifact, and both documents are well-formed XML"

echo "== 3. --format msi =="
run_program msi "$work/msi" "$work/msi.log"
check "$work/msi.log" "$work/msi" '
if "mcpp.dist.wix" not in actions: fail("--format msi planned no MSI")
if "mcpp.dist.wix.bundle" in actions: fail("--format msi planned a bundle")
'
echo "ok: no bundle under --format msi"

echo "== 4. a bundle named setup.exe =="
run_program setup "$work/refused" "$work/refused.log" "$work/refused/SETUP.exe"
check "$work/refused.log" "$work/refused" '
if actions: fail("a bundle named setup.exe planned actions: " + repr(sorted(actions)))
if not any(l.startswith("mcpp:warning=") and "WIX0388" in l for l in lines):
    fail("the refusal is not a warning naming WIX0388")
'
echo "ok: refused as a warning, before wix runs"

echo "PASS: dist-wix plans the MSI and the bundle, and writes well-formed documents"
