#!/usr/bin/env bash
# End-to-end check for `dist-web` (#622 B3), worked around one engine defect.
#
# THE INTENDED FORM IS ONE COMMAND, AND IT IS NOT WHAT THIS SCRIPT RUNS:
#
#   mcpp pack --target wasm32-emscripten --format web
#
# On the engine this member was developed and reviewed against
# (mcpp 2026.9.12.2, built from `feat/622-ui-framework-on-three-rows`),
# EVERY `build.mcpp` -- not only this member's -- fails to compile under
# `--target wasm32-emscripten`:
#
#   error: build.mcpp failed to compile (exit 1):
#   ... AssertionError (emcc.py, compile_source_file, phase_compile_inputs)
#
# Measured cause: `prepare.cppm`'s `host_tc_for_build_program` resolves the
# HOST toolchain for `build.mcpp` from the closure-captured `tcSpec`, read
# LAZILY at the point a `build.mcpp` is found to exist -- but the row's own
# default toolchain pin (`tcSpec = targetPinCandidate;`, prepare.cppm
# around line 7233, the same assignment the "target default for
# wasm32-emscripten, replacing your gcc@16.1.0" message reports) has ALREADY
# overwritten that variable by the time this runs, for every target whose
# row carries one. `em++` cannot compile a host-native binary under any
# invocation, so `build.mcpp` is asked to compile itself with a compiler
# that structurally cannot produce a program the host can run: this is not
# specific to `dist-web`, `mcpp.plugins`, or this fixture -- a project whose
# ENTIRE `build.mcpp` is `import std; int main(){}` fails identically.
# `riscv64`/Android rows are not obviously affected, because their default
# pins are cross-CONFIGURATIONS of a compiler family (gcc, clang) that also
# has a native mode; `emsdk`'s em++ has none. Reported rather than worked
# around in the engine: see this task's final report for the full
# repro and the suggested fix site.
#
# WHAT THIS SCRIPT MEASURES INSTEAD, split across the one seam the defect
# does not reach:
#
#   1. A REAL wasm32-emscripten build, of the SAME `src/main.cpp`, through a
#      throwaway package with NO `build.mcpp` at all -- unaffected, because
#      the defect is specific to a build.mcpp existing. `mcpp pack --format
#      dir` stages the real `.js`/`.wasm` pair the emsdk payload produced.
#   2. This fixture's OWN `build.mcpp`, compiled for the HOST (an ordinary
#      `mcpp build`, no `--target`) -- also unaffected, because the defect
#      is specific to a CROSS target. This compiles to an ordinary ELF at
#      `target/.build-mcpp/build.mcpp.bin`.
#   3. That compiled binary, run DIRECTLY (bypassing mcpp's own
#      orchestration) with the environment contract `--target
#      wasm32-emscripten --format web` would have set -- `mcpp::target_os()`
#      and its siblings are `std::getenv` and nothing else
#      (`hostprogram.cppm`), so this exercises exactly the code `plan_for`
#      and `submit` run, against the REAL staged tree from step 1 plus one
#      hand-placed file standing in for `mcpp::deploy` (which, like every
#      `build.mcpp` directive, needs the orchestrator this defect removes).
#   4. Every `mcpp:action=` line step 3 printed, parsed with `jq` and
#      EXECUTED by this script -- standing in for ninja, which never ran.
#
# The resulting `target/web/` directory, and the fact that `node` runs the
# `.js` inside it and prints `1-2-3`, are real; only the orchestration that
# would normally produce them is simulated here.
#
# Usage: MCPP=<fresh mcpp> ./check-web-plan.sh   (run from this directory)
set -e

MCPP="${MCPP:-mcpp}"
HERE="$PWD"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }

command -v jq >/dev/null || fail "jq is required to parse the planned mcpp:action= lines" /dev/null
command -v node >/dev/null || fail "node is required to run the produced launcher" /dev/null

echo "== step 1: a real wasm32-emscripten build, with no build.mcpp =="
REALDIR=$(mktemp -d)
mkdir -p "$REALDIR/src"
cat > "$REALDIR/mcpp.toml" <<EOF
[package]
name    = "web-consumer"
version = "0.1.0"

[targets.web-consumer]
kind = "bin"
main = "src/main.cpp"
EOF
cp src/main.cpp "$REALDIR/src/main.cpp"
( cd "$REALDIR" && "$MCPP" pack --target wasm32-emscripten --format dir > pack.log 2>&1 ) \
    || fail "the real wasm32-emscripten build failed" "$REALDIR/pack.log"
STAGE=$(find "$REALDIR/target/dist" -maxdepth 1 -type d -name 'web-consumer-*-wasm32-emscripten')
[ -n "$STAGE" ] || fail "no staged directory from the real build" "$REALDIR/pack.log"
[ -f "$STAGE/bin/web-consumer.js" ]   || fail "the real build produced no bin/web-consumer.js"
[ -f "$STAGE/bin/web-consumer.wasm" ] || fail "the real build produced no bin/web-consumer.wasm"
echo "ok: real bin/web-consumer.js and bin/web-consumer.wasm, from the emsdk payload"

# Stand in for `mcpp::deploy(generated, "assets/greeting.txt")`, which
# build.mcpp.bin below calls but which nothing here can act on, per this
# script's header comment.
mkdir -p "$STAGE/bin/assets"
printf 'hello from build.mcpp\n' > "$STAGE/bin/assets/greeting.txt"

echo "== step 2: this fixture's build.mcpp, compiled for the HOST =="
rm -rf target
"$MCPP" build > build.log 2>&1 || fail "the host build of this fixture failed" build.log
BIN=target/.build-mcpp/build.mcpp.bin
[ -x "$BIN" ] || fail "no compiled build.mcpp.bin at $BIN" build.log
echo "ok: build.mcpp compiled to $BIN"

echo "== step 3: run it directly under the wasm32-emscripten + --format web contract =="
OUT=$(mktemp -d)
PLANLOG=$(mktemp)
env -i PATH="$PATH" \
    MCPP_PACK_FORMAT=web \
    MCPP_TARGET_OS=emscripten \
    MCPP_PACK_STAGE_DIR="$STAGE" \
    MCPP_PKG_NAME=web-consumer \
    MCPP_PKG_VERSION=0.1.0 \
    MCPP_MANIFEST_DIR="$HERE" \
    MCPP_OUT_DIR="$OUT" \
    "$BIN" > "$PLANLOG" 2>&1
grep -q '"id":"mcpp.dist.web.index"' "$PLANLOG" || fail "no index.html action was planned" "$PLANLOG"
n=$(grep -c '"id":"mcpp.dist.web.file"' "$PLANLOG")
[ "$n" -eq 3 ] || fail "expected 3 file actions (js, wasm, the deployed asset), got $n" "$PLANLOG"
echo "ok: 3 file actions and 1 index.html action planned"

echo "== step 4: execute the planned actions (standing in for ninja) =="
grep '^mcpp:action=' "$PLANLOG" | sed 's/^mcpp:action=//' | while read -r line; do
    mapfile -t argv < <(echo "$line" | jq -r '.command[]')
    for i in "${!argv[@]}"; do
        argv[$i]="${argv[$i]//\$\{mcpp.stage_dir\}/$STAGE}"
    done
    "${argv[@]}"
done

echo "== assertions on target/web/ =="
WEB="$OUT/web"
mapfile -t files < <(cd "$WEB" && find . -type f | sed 's#^\./##' | sort)
want=(assets/greeting.txt index.html web-consumer.js web-consumer.wasm)
[ "${files[*]}" = "${want[*]}" ] || {
    echo "FAIL: the produced directory's file list is not exactly the expected set"
    echo "  got:    ${files[*]}"
    echo "  wanted: ${want[*]}"
    exit 1; }
echo "ok: exactly js, wasm, the deployed asset and index.html -- nothing else"

grep -q 'web-consumer.js' "$WEB/index.html" || fail "index.html does not load the launcher" "$WEB/index.html"
grep -q '<title>web-consumer</title>' "$WEB/index.html" || fail "index.html's title is not the package name" "$WEB/index.html"
echo "ok: index.html loads the launcher and titles the page from [package] name"

grep -q 'hello from build.mcpp' "$WEB/assets/greeting.txt" || fail "the deployed asset's content did not survive the copy" "$WEB/assets/greeting.txt"

out=$(node "$WEB/web-consumer.js")
[ "$out" = "1-2-3" ] || fail "node did not print 1-2-3 (got: $out)"
echo "ok: node $WEB/web-consumer.js prints 1-2-3"

rm -rf "$REALDIR" "$OUT" "$PLANLOG"
echo "PASS: dist-web produces a static directory, node runs it, 1-2-3"
