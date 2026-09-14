#!/usr/bin/env bash
# dist-wix `--format setup` on Windows (mcpp#634, B4).
#
#   1. `mcpp pack --format setup` writes a Burn bundle named after the product
#      and the architecture, never `setup.exe`, beside the MSI it chains.
#   2. The bundle carries that MSI: `wix burn extract` takes the attached
#      container apart, and the package inside is byte-for-byte the MSI the
#      first action wrote.
#
# The bundle's user interface is WiX's stock bootstrapper application, which
# `wix build` finds only through the extension `xim:wix` 5.0.2-1 carries; a
# bundle definition that names it builds only with that extension loaded.
#
# Usage: MCPP=<mcpp> ./check-setup.sh   (run from this directory, on Windows, under Git Bash)
set -eu

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
reading() { printf 'READING %s: %s\n' "$1" "$2"; }
packed() { sed -n 's/^ *Packed //p' "$1" | tail -1 | tr -d '\r'; }
size() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1"; }

# ── 1. the bundle ──────────────────────────────────────────────────────────
echo "== 1. mcpp pack --format setup =="
"$MCPP" pack --format setup > setup-pack.log 2>&1 || fail "mcpp pack --format setup failed" setup-pack.log
bundle=$(packed setup-pack.log)
reading packed "$bundle"
case "$bundle" in *MsiConsumer-x64.exe) ;; *) fail "the pack reported '$bundle', not MsiConsumer-x64.exe" setup-pack.log ;; esac
[ -f "$bundle" ] || fail "the reported bundle $bundle does not exist" setup-pack.log
msi="$(dirname "$bundle")/MsiConsumer-x64.msi"
[ -f "$msi" ] || fail "no MSI beside the bundle at $msi" setup-pack.log
if find target -iname 'setup.exe' | grep -q .; then fail "a file named setup.exe was written"; fi
reading sizes "bundle $(size "$bundle") bytes, msi $(size "$msi") bytes"
echo "ok: $(basename "$bundle") beside $(basename "$msi")"

# ── 2. the MSI inside the bundle ───────────────────────────────────────────
echo "== 2. the bundle carries the MSI =="
"$MCPP" self env > setup-env.txt
home=$(awk -F'= *' '/^MCPP_HOME/{print $2; exit}' setup-env.txt | tr -d '\r')
[ -n "$home" ] || fail "could not read MCPP_HOME" setup-env.txt
command -v cygpath > /dev/null && home=$(cygpath -u "$home")
wix="$home/registry/data/xpkgs/xim-x-wix/5.0.2-1/tool/tools/net6.0/any/wix.exe"
[ -f "$wix" ] || fail "no wix.exe of xim:wix 5.0.2-1 at $wix"
out="$PWD/setup-extract"
rm -rf "$out"
"$wix" burn extract "$(cygpath -w "$bundle")" -o "$(cygpath -w "$out")" > setup-extract.log 2>&1 \
    || fail "wix burn extract refused the bundle" setup-extract.log
inside=$(find "$out" -type f -iname '*.msi')
reading extracted "$(find "$out" -type f | sed "s|^$out/||" | tr '\n' ' ')"
[ "$(printf '%s\n' "$inside" | grep -c .)" = 1 ] || fail "expected one MSI in the bundle, found: $(echo $inside)" setup-extract.log
cmp -s "$inside" "$msi" || fail "the MSI inside the bundle ($(size "$inside") bytes) differs from $msi ($(size "$msi") bytes)"
echo "ok: the bundle's container holds the MSI, byte-for-byte"

echo "PASS: dist-wix writes a Burn bundle that chains the MSI"
