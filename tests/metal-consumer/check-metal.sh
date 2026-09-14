#!/usr/bin/env bash
# rules-metal on macOS (mcpp#634, B6).
#
#   1. The fixture's two shaders, and a variant of one of them, compile to three
#      Metal libraries placed beside the program; the program opens each one and
#      finds the Metal library magic.
#   2. The dependency file the compiler writes reaches the graph: editing the
#      header two of the compilations include recompiles those two and not the
#      third, which no rule could have declared as an input.
#
# THE TOOLCHAIN IS A PROPERTY OF THE RUNNER. When `xcrun --sdk macosx --find
# metal` or `--find metallib` answers nothing, the rule refuses naming that
# command. This script then asserts the refusal, records that the two criteria
# above are unmeasured on the runner, and exits 0 with a warning annotation
# saying so, rather than reporting a library it did not build.
#
# Usage: MCPP=<mcpp> ./check-metal.sh   (run from this directory, on macOS)
set -eu

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
reading() { printf 'READING %s: %s\n' "$1" "$2"; }

missing=""
for tool in metal metallib; do
    found=$(xcrun --sdk macosx --find "$tool" 2>/dev/null || true)
    reading "xcrun-find-$tool" "${found:-nothing}"
    [ -n "$found" ] || { [ -n "$missing" ] || missing="$tool"; }
done

rm -rf target
if [ -n "$missing" ]; then
    rc=0
    "$MCPP" build > metal-build.log 2>&1 || rc=$?
    [ "$rc" -ne 0 ] || fail "the build succeeded, and xcrun finds no $missing" metal-build.log
    grep -q "xcrun --sdk macosx --find $missing" metal-build.log \
        || fail "the build failed without naming the command that found no $missing" metal-build.log
    reading refusal "$(grep -m1 'mcpp.rules.metal:' metal-build.log)"
    reading criterion "UNMEASURED on this runner: no $missing, so no Metal library was built"
    echo "::warning title=rules-metal unmeasured::xcrun --sdk macosx --find $missing answered nothing on this runner; the rule refused naming that command, and no Metal library was built or checked"
    exit 0
fi
reading metal-version "$(xcrun --sdk macosx metal --version 2>&1 | head -1)"

# ── 1. three libraries beside the program ─────────────────────────────────
echo "== 1. the shaders compile to Metal libraries the program finds =="
"$MCPP" build > metal-build.log 2>&1 || fail "mcpp build failed" metal-build.log
gen=target/.build-mcpp/out/metal
for name in scale tint tint_red; do
    [ -f "$gen/$name.air" ] || fail "no $gen/$name.air" metal-build.log
    [ -f "$gen/$name.metallib" ] || fail "no $gen/$name.metallib" metal-build.log
    magic=$(head -c 4 "$gen/$name.metallib")
    reading "$name.metallib" "$(wc -c < "$gen/$name.metallib" | tr -d ' ') bytes, magic '$magic'"
    [ "$magic" = MTLB ] || fail "$gen/$name.metallib does not begin with MTLB"
done
rc=0
"$MCPP" run > metal-run.log 2>&1 || rc=$?
[ "$rc" -eq 0 ] || fail "the program exited $rc" metal-run.log
grep -qx 'metal-consumer ok' metal-run.log || fail "the program did not find its libraries" metal-run.log
echo "ok: scale, tint and tint_red are Metal libraries, deployed where the program looks"

# ── 2. the dependency file ─────────────────────────────────────────────────
echo "== 2. an edited header recompiles the shaders that include it =="
header=shaders/tint_common.h
cp "$header" "$header.orig"
trap 'mv -f "$header.orig" "$header"' EXIT
stamp() { stat -f %m "$gen/$1.air"; }
before="$(stamp scale) $(stamp tint) $(stamp tint_red)"
sleep 2
sed 's/#define TINT_R 0.0h/#define TINT_R 0.5h/' "$header.orig" > "$header"
cmp -s "$header" "$header.orig" && fail "the header edit changed nothing" "$header"
"$MCPP" build > metal-rebuild.log 2>&1 || fail "the rebuild failed" metal-rebuild.log
after="$(stamp scale) $(stamp tint) $(stamp tint_red)"
reading air-mtimes "scale tint tint_red: before $before, after $after"
set -- $before; b_scale=$1 b_tint=$2 b_red=$3
set -- $after;  a_scale=$1 a_tint=$2 a_red=$3
[ "$a_tint" != "$b_tint" ] || fail "tint.air was not recompiled after its header changed" metal-rebuild.log
[ "$a_red" != "$b_red" ] || fail "tint_red.air was not recompiled after its header changed" metal-rebuild.log
[ "$a_scale" = "$b_scale" ] || fail "scale.air was recompiled, and it includes nothing that changed" metal-rebuild.log
echo "ok: the two compilations that include the header were recompiled, the third was not"

echo "PASS: rules-metal compiles Metal libraries, and the compiler's dependency file reaches the graph"
