#!/usr/bin/env bash
# rules-swift on macOS (mcpp#647 E2).
#
#   1. A C++ program calls a Swift function exported with `@_cdecl`, which calls
#      back into a C function through the bridging header; the program prints a
#      line from each side and exits 0 with the answer 42.
#   2. The header the rule generates exists; whether it declares the exported
#      function for a plain C++ caller depends on the Swift version, and is
#      recorded as a reading.
#   3. On the iOS simulator row, when this runner has the iphonesimulator SDK,
#      the same fixture builds; running it needs a booted simulator and is left
#      to `simctl-run`, so this step checks the link only.
#
# THE TOOLCHAIN IS A PROPERTY OF THE RUNNER, as in `tests/metal-consumer`: when
# `xcrun --sdk macosx --find swiftc` answers nothing, the script asserts the
# rule's refusal naming that command and exits 0 with a warning annotation.
#
# Usage: MCPP=<mcpp> ./check-swift.sh   (run from this directory, on macOS)
set -eu

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
reading() { printf 'READING %s: %s\n' "$1" "$2"; }

found=$(xcrun --sdk macosx --find swiftc 2>/dev/null || true)
reading xcrun-find-swiftc "${found:-nothing}"

rm -rf target
if [ -z "$found" ]; then
    rc=0
    "$MCPP" build > swift-build.log 2>&1 || rc=$?
    [ "$rc" -ne 0 ] || fail "the build succeeded, and xcrun finds no swiftc" swift-build.log
    grep -q "xcrun --sdk macosx --find swiftc" swift-build.log \
        || fail "the build failed without naming the command that found no swiftc" swift-build.log
    reading criterion "UNMEASURED on this runner: no swiftc"
    echo "::warning title=rules-swift unmeasured::xcrun --sdk macosx --find swiftc answered nothing on this runner"
    exit 0
fi

"$MCPP" build > swift-build.log 2>&1 || fail "mcpp build failed" swift-build.log
"$MCPP" run > swift-run.log 2>&1 || fail "mcpp run failed" swift-run.log
reading run "$(tr '\n' '|' < swift-run.log)"
grep -q '^swift side: base 2, from C 40$' swift-run.log || fail "the Swift side did not print its line" swift-run.log
grep -q '^swift-consumer: cpp side, answer 42$' swift-run.log || fail "the C++ side did not print the answer 42" swift-run.log
echo "ok: C++ calls Swift, Swift calls C, and both sides print"

header=$(find target -name 'swift_consumer-Swift.h' | head -1)
[ -n "$header" ] || fail "no generated swift_consumer-Swift.h" swift-build.log
[ -s "$header" ] || fail "the generated header is empty" "$header"
if grep -q 'swift_consumer_answer' "$header"; then
    reading header "declares swift_consumer_answer"
else
    reading header "does not declare swift_consumer_answer with this Swift version"
fi
echo "ok: the rule generated the package's Swift header"

if xcrun --sdk iphonesimulator --show-sdk-path > /dev/null 2>&1; then
    rm -rf target
    "$MCPP" build --target aarch64-ios-sim > swift-sim.log 2>&1 || fail "the iOS simulator build failed" swift-sim.log
    bin=$(find target -type f -name swift-consumer -perm -u+x | head -1)
    [ -n "$bin" ] || fail "no swift-consumer program for the simulator" swift-sim.log
    reading ios-sim "$(file "$bin")"
    echo "ok: the fixture links for the iOS simulator"
else
    reading ios-sim "UNMEASURED on this runner: no iphonesimulator SDK"
fi

rm -rf target swift-build.log swift-run.log swift-sim.log
echo "PASS: rules-swift compiles a package's Swift sources into its program"
