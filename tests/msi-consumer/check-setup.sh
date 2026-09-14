#!/usr/bin/env bash
# dist-wix `--format setup` on Windows (mcpp#634, B4).
#
#   1. `mcpp pack --format setup` writes a Burn bundle named after the product
#      and the architecture, never `setup.exe`, beside the MSI it chains.
#   2. The bundle carries that MSI: `wix burn extract` takes the attached
#      container apart, and one of the payloads inside is byte-for-byte the
#      MSI the first action wrote. A payload is identified by its content, not
#      by its file name: extracted without the bootstrapper application, whose
#      manifest maps the names, the payloads keep their ids (`a0`, `a1`; the
#      second is the MSI's cabinet), measured on windows-2022.
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
# The artifact a pack reports on its `Packed` line, with `/` separators so that
# `dirname` reads a Windows spelling.
packed() { sed -n 's/^ *Packed //p' "$1" | tail -1 | tr -d '\r' | tr '\\' '/'; }
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
"$MCPP" self env > setup-env.log
home=$(awk -F'= *' '/^MCPP_HOME/{print $2; exit}' setup-env.log | tr -d '\r')
[ -n "$home" ] || fail "could not read MCPP_HOME" setup-env.log
command -v cygpath > /dev/null && home=$(cygpath -u "$home")
wix="$home/registry/data/xpkgs/xim-x-wix/5.0.2-1/tool/tools/net6.0/any/wix.exe"
[ -f "$wix" ] || fail "no wix.exe of xim:wix 5.0.2-1 at $wix"
out="$PWD/target/setup-extract"
rm -rf "$out"
"$wix" burn extract "$(cygpath -w "$bundle")" -oba "$(cygpath -w "$out/ba")" -o "$(cygpath -w "$out/payloads")" \
    > setup-extract.log 2>&1 || fail "wix burn extract refused the bundle" setup-extract.log
# A listing file rather than process substitution, which Git Bash emulates.
find "$out/payloads" -type f | sort > setup-payloads.log
reading extracted "$(sed "s|^$out/payloads/||" setup-payloads.log | tr '\n' ' ')"
[ -s setup-payloads.log ] || fail "wix burn extract wrote no payload" setup-extract.log
match=""
while IFS= read -r payload; do
    if cmp -s "$payload" "$msi"; then match="$payload"; break; fi
done < setup-payloads.log
[ -n "$match" ] || fail "no payload in the bundle is byte-for-byte $msi ($(size "$msi") bytes)" setup-payloads.log setup-extract.log
reading msi-payload "${match#$out/payloads/}, $(size "$match") bytes"
echo "ok: the bundle's container holds the MSI, byte-for-byte"

echo "PASS: dist-wix writes a Burn bundle that chains the MSI"
