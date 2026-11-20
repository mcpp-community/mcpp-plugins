#!/usr/bin/env bash
# rules-slint end to end: the fixture builds with the SDK the project declares
# (xim:slint), runs headless, and the rule's actions behave as one per input:
# the module exports what the input exports, an edit of a file the input
# imports regenerates it, a build with nothing changed regenerates nothing.
set -euo pipefail
: "${MCPP:?MCPP names the mcpp under test}"
fail() { echo "FAIL: $*"; exit 1; }
cd "$(dirname "$0")"
rm -rf target
mkdir -p target/ci
"$MCPP" build -v 2>&1 | tee target/ci/build.log
grep -qE '/slint-compiler[^ ]* .*app\.slint' target/ci/build.log || fail "slint-compiler did not run on ui/app.slint"
unit=$(find target -path '*slint/app.cppm' | head -1)
[ -n "$unit" ] || fail "no module unit was written"
for name in AppWindow Colors Mode Point Settings; do
    grep -qx "export using ::$name;" "$unit" || fail "app.cppm does not export $name"
done
! grep -q 'Badge' "$unit" || fail "app.cppm exports Badge, which slint-compiler does not generate (not a window)"
echo "ok: the module exports the window (through its base), the struct, the enum, the global and the alias"
"$MCPP" run | tee target/ci/run.log
grep -qx 'counter=43 calls=1 origin=3,4 title=changed background=#202020 mode=1' target/ci/run.log ||
    fail "the program did not see the generated component"
echo "ok: the program imports app.ui and drives the component headless"
"$MCPP" build -v > target/ci/second.log 2>&1 || { cat target/ci/second.log; fail "the second build failed"; }
! grep -qE '/slint-compiler[^ ]* ' target/ci/second.log || fail "a build with nothing changed ran slint-compiler"
touch ui/palette.slint
"$MCPP" build -v > target/ci/third.log 2>&1 || { cat target/ci/third.log; fail "the build after the edit failed"; }
grep -qE '/slint-compiler[^ ]* .*app\.slint' target/ci/third.log || fail "an edit of palette.slint (imported by app.slint) did not regenerate app.h"
echo "ok: nothing changed regenerates nothing; an imported file's edit regenerates its importer"
