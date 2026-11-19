#!/usr/bin/env bash
# tools-steps on this host (0.27.0): a step program run by the runner the
# feature carries, `steps`, with no shell and no `xim:cmake`.
#
#   1. Every operation, under a root whose name holds a space, a semicolon and
#      a non-ASCII character, over a link to a directory and read-only entries:
#      the build succeeds only when `steps-probe`'s check of the tree holds,
#      since only that check writes the step's output.
#   2. An unchanged step program does not run again.
#   3. `require` on an empty list fails the step with the member's message, and
#      the operation after it does not run.
#   4. A program that exits 3 fails the step, named with its status, and the
#      operation after it does not run.
#   5. The build program running again with another case does not rewrite the
#      first step's file, so that step still does not run again.
#
# Usage: MCPP=<mcpp> ./check-steps.sh   (run from this directory)
set -e

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
runs() { wc -l < "$out/steps-runs.txt" | tr -d ' '; }

# A run that failed inside the step can leave its read-only directory behind.
if [ -d target ]; then chmod -R u+w target; fi
rm -rf target
unset STEPS_CONSUMER_CASE
"$MCPP" build > steps-build.log 2>&1 || fail "the operations did not all do what they say" steps-build.log
out=$(sh ../out-dir.sh)
grep -qE '^verified [0-9]+ checks$' "$out/steps-verified.txt" \
    || fail "the step's check wrote no report" "$out/steps-verified.txt" steps-build.log
[ "$(runs)" = 1 ] || fail "the step ran $(runs) times in one build" "$out/steps-runs.txt"
echo "ok: every operation, $(cat "$out/steps-verified.txt")"

"$MCPP" build > steps-rebuild.log 2>&1 || fail "the second build failed" steps-rebuild.log
[ "$(runs)" = 1 ] || fail "an unchanged step program ran again" steps-rebuild.log
echo "ok: an unchanged step program does not run again"

STEPS_CONSUMER_CASE=require "$MCPP" build > steps-require.log 2>&1 \
    && fail "require on an empty list did not fail the step" steps-require.log
grep -q "the list 'none' is empty, as this case intends" steps-require.log \
    || fail "the failed require does not print the member's message" steps-require.log
[ ! -e "$out/steps-require.txt" ] || fail "an operation after the failed require ran"
echo "ok: require on an empty list fails the step with its message"

STEPS_CONSUMER_CASE=status "$MCPP" build > steps-status.log 2>&1 \
    && fail "a program that exits 3 did not fail the step" steps-status.log
grep -q "exited with status 3" steps-status.log \
    || fail "the failed run does not name the status" steps-status.log
[ ! -e "$out/steps-status.txt" ] || fail "an operation after the failed run ran"
echo "ok: a program's non-zero status fails the step, named"

"$MCPP" build > steps-again.log 2>&1 || fail "the build without a case failed" steps-again.log
[ "$(runs)" = 1 ] || fail "the step ran again after the build program ran with another case" steps-again.log
"$MCPP" run > steps-run.log 2>&1 || fail "the program does not run" steps-run.log
grep -q '^steps-consumer ok' steps-run.log || fail "the program printed nothing" steps-run.log
echo "PASS: tools-steps runs every operation with its own runner on this host"
