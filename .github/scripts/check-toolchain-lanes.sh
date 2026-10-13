#!/usr/bin/env bash
# The two lanes that read this collection's C++ with something other than the
# default Linux toolchain. A CI step names the lane:
#
#   bash .github/scripts/check-toolchain-lanes.sh llvm
#   bash .github/scripts/check-toolchain-lanes.sh editor
#
# THREE COMBINATIONS READ THIS CODE, AND EACH ONE HAS REFUSED SOMETHING THE
# OTHERS ACCEPT. GCC with libstdc++ builds it by default on Linux. clang with
# libc++ builds it under `--toolchain llvm@…`, as on macOS. clang with
# libstdc++'s `std` module is what an editor's clangd uses to read a project
# built with GCC -- and no build ever compiled that combination, so a
# `std::pair<std::string, value>` inside `value` (0.19.1) failed only there:
# every build passed and every build program in an editor showed a module that
# was "not found". docs/plugin-development.md §4 states the rule this enforces.
#
# Environment: MCPP (llvm); MCPP_EDITOR and MCPPLS (editor).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LLVM="${MCPP_LLVM:-llvm@22.1.8}"

fail() { echo "FAIL: $*"; exit 1; }

# ── llvm: clang with libc++ ───────────────────────────────────────────────────
#
# FULL BUILDS for the fixtures whose build needs nothing tied to libstdc++, each
# with the criterion its program prints. `all-rules-compile` imports every
# member and calls it, so the whole collection is compiled under libc++ by its
# first line; the others run what the members produce.
#
# CONFIGURE ONLY for the Qt fixtures. Qt's official Linux binaries are built
# against libstdc++ and a libc++ program cannot link them, which is the Qt
# fixtures' business and not this lane's. `--configure-only` still compiles and
# runs the build program -- the member and the library under libc++ -- and that
# is the part this lane is for.
#
# NOT HERE: the fixtures that pin a device toolchain (cuda, hip, sycl,
# multi-rule) already build with llvm@22.1.8 in the consumers job, and the
# deps fixtures' libc++ rows are `vcpkg-libcxx` and `cmake-cache` there.
llvm_full=(
    "all-rules-compile|^all-rules-compile ok"
    "plugin-logic|"
    "embed-consumer|^size=33 "
    "embed-module-consumer|^all ok"
    "embed-table-consumer|^all ok"
    "island-interface|^all ok"
    "archive-consumer|^archive-consumer: 'greetings from the archive'"
    "spirv-module-consumer|^all ok"
)
llvm_configure_only=(qt-consumer qt-widgets-consumer qt-sdk-consumer qt-import-only)

llvm_lane() {
    : "${MCPP:?MCPP names the mcpp under test}"
    "$MCPP" toolchain install llvm "${LLVM#llvm@}"
    local entry fixture expect results declared passed
    for entry in "${llvm_full[@]}"; do
        fixture=${entry%%|*}; expect=${entry#*|}
        echo "── $fixture under $LLVM"
        cd "$ROOT/tests/$fixture"
        rm -rf target; mkdir -p target/ci
        "$MCPP" build --toolchain "$LLVM" > target/ci/llvm-build.log 2>&1 ||
            { cat target/ci/llvm-build.log; fail "$fixture does not build under $LLVM"; }
        grep -q -- '-stdlib=libc++' target/*/*/build.ninja ||
            fail "$fixture was not linked against libc++: the lane did not build what it says it built"
        if [ "$fixture" = plugin-logic ]; then
            # The kit's verdicts, counted against the cases the fixture declares,
            # as check-deps-and-qt.sh counts them under the default toolchain.
            results=$(find target -path '*plugins-testing/results.txt' | head -1)
            [ -n "$results" ] || fail "the kit wrote no results.txt under $LLVM"
            declared=$(grep -cE '^        \{ "' build.mcpp)
            passed=$(grep -c '^PASS ' "$results")
            [ "$declared" -gt 0 ] && [ "$passed" -eq "$declared" ] ||
                { cat "$results"; fail "$passed of $declared plugin-logic cases passed under $LLVM"; }
            echo "ok: $passed of $declared plugin-logic cases passed under $LLVM"
            continue
        fi
        "$MCPP" run --toolchain "$LLVM" > target/ci/llvm-run.log 2>&1 ||
            { cat target/ci/llvm-run.log; fail "$fixture built under $LLVM and did not run"; }
        grep -qE "$expect" target/ci/llvm-run.log ||
            { cat target/ci/llvm-run.log; fail "$fixture ran under $LLVM without printing '$expect'"; }
        echo "ok: $fixture builds and runs under $LLVM"
    done
    for fixture in "${llvm_configure_only[@]}"; do
        echo "── $fixture under $LLVM, configured"
        cd "$ROOT/tests/$fixture"
        rm -rf target; mkdir -p target/ci
        "$MCPP" build --configure-only --toolchain "$LLVM" > target/ci/llvm-configure.log 2>&1 ||
            { cat target/ci/llvm-configure.log; fail "$fixture's build program does not compile or run under $LLVM"; }
        echo "ok: $fixture's build program compiles and runs under $LLVM"
    done
}

# ── editor: clang with libstdc++, through mcppls ──────────────────────────────
#
# What an editor shows for each fixture's `build.mcpp`, read by mcppls with its
# own clangd over the default (GCC) toolchain's standard library. A module the
# build program imports that clangd cannot compile is an error on the import
# line, so `counts.error` is the criterion; every diagnostic goes to the job
# summary, so a failure names the line without downloading anything.
#
# A FILE THAT WAS NOT READ IS A FAILURE, NOT A PASS. mcppls says so with
# `build-program-context` when the mcpp it runs describes no build programs
# (before 2026.10.11.1); zero errors from a file nobody compiled is the silent
# pass this lane exists to prevent.
editor_lane() {
    : "${MCPP_EDITOR:?MCPP_EDITOR names the mcpp mcppls runs}"
    : "${MCPPLS:?MCPPLS names the mcppls binary of the payload}"
    export PATH="$(dirname "$MCPP_EDITOR"):$PATH"
    export MCPPLS_CACHE_DIR="${RUNNER_TEMP:-/tmp}/mcppls-cache"
    local summary="${GITHUB_STEP_SUMMARY:-/dev/null}" failed=0 checked=0 dir fixture out
    {
        echo "### build.mcpp in the editor ($("$MCPPLS" --version | head -1), $("$MCPP_EDITOR" --version | head -1))"
        echo
        echo "| fixture | errors | warnings | diagnostics |"
        echo "|---|---|---|---|"
    } >> "$summary"
    for dir in "$ROOT"/tests/*/; do
        fixture=$(basename "$dir")
        [ -f "$dir/build.mcpp" ] || continue
        echo "── $fixture"
        out="${RUNNER_TEMP:-/tmp}/editor-$fixture.json"
        # CONFIGURED FIRST, as a project is before anyone opens it: the
        # configuration provisions what the build program's context names --
        # the toolchain a fixture pins, the Qt SDK, a shader compiler -- and
        # without it the build database describes no build program, so the
        # editor finds not even `mcpp` (measured on a fresh runner).
        (cd "$dir" && "$MCPP_EDITOR" build --configure-only) > "${out%.json}.configure.log" 2>&1 ||
            { cat "${out%.json}.configure.log"
              echo "| $fixture | — | — | \`mcpp build --configure-only\` failed |" >> "$summary"; failed=1; continue; }
        # mcppls exits 1 when it reports an error and still writes the report,
        # so the report decides; only a run that wrote none is its own failure.
        local status=0
        (cd "$dir" && "$MCPPLS" diagnostics build.mcpp --format json --timeout 300) > "$out" || status=$?
        python3 -c 'import json, sys; json.load(open(sys.argv[1]))' "$out" 2> /dev/null ||
            { echo "| $fixture | — | — | mcppls diagnostics exited $status without a report |" >> "$summary"; failed=1; continue; }
        checked=$((checked + 1))
        python3 - "$fixture" "$out" "$summary" <<'EOF' || failed=1
import json, sys
fixture, path, summary = sys.argv[1:4]
report = json.load(open(path))
counts = report["counts"]
found = [d for f in report["files"] for d in f["diagnostics"]]
unread = [d for d in found if d.get("code") == "build-program-context"]
incomplete = [f["file"] for f in report["files"] if not f.get("complete", True)]
cells = "<br>".join(
    f"{d['severity']} {d['location']['file']}:{d['location']['line']} `{d.get('code', '')}` {d['message'][:240]}".replace("|", "\\|")
    for d in found) or "none"
with open(summary, "a") as out:
    out.write(f"| {fixture} | {counts['error']} | {counts['warning']} | {cells} |\n")
for d in found:
    print(f"  {d['severity']} {d['location']['file']}:{d['location']['line']} [{d.get('code', '')}] {d['message']}")
if counts["error"] or unread or incomplete:
    why = "errors" if counts["error"] else "not read" if unread else "incomplete"
    print(f"FAIL: {fixture}'s build.mcpp in the editor: {why}")
    sys.exit(1)
print(f"ok: {fixture}'s build.mcpp has no error in the editor")
EOF
    done
    [ "$checked" -gt 0 ] || fail "no fixture's build.mcpp was checked"
    [ "$failed" = 0 ] || fail "an editor reports errors in a fixture's build.mcpp (the job summary lists them)"
    echo "ok: $checked build programs read in the editor without an error"
}

case "${1:-}" in
    llvm)   llvm_lane ;;
    editor) editor_lane ;;
    *) echo "usage: $0 llvm|editor"; exit 2 ;;
esac
