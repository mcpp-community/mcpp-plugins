#!/usr/bin/env bash
# A second `mcpp pack --format web` with nothing changed copies nothing
# (design record `2026-09-13-four-upstream-asks-from-a-ui-framework.md`,
# §4.3 / §9.2 P3) -- the criterion `${mcpp.self} stage --verify content`
# gives for free and `cp` never could, since `stage` writes only on a
# content difference.
#
# A sibling of `check-web-plan.sh` rather than an addition to it, so that
# script keeps passing exactly as it did before this batch (its own
# criterion). `ninja -n` is not used here: this repository's own `.ninja_log`
# is written by mcpp's INTERNAL ninja, and a system `ninja` of a different
# version reads that log, decides it is "too old", and starts over -- which
# reports every edge as pending regardless of whether anything actually
# reran. mtimes are the criterion the design record itself offers as the
# alternative, and they do not depend on which ninja binary happens to be on
# the runner's PATH.
#
# Usage: MCPP=<mcpp 2026.9.13.1+> ./check-web-idempotent.sh   (run from this
# directory, after `check-web-plan.sh`, or on its own -- it packs fresh)
set -e

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }

rm -rf target
"$MCPP" build --target wasm32-emscripten > build.log 2>&1 \
    || fail "mcpp build --target wasm32-emscripten failed" build.log
"$MCPP" pack --format web --target wasm32-emscripten > pack1.log 2>&1 \
    || fail "the first mcpp pack --format web failed" pack1.log
WEB=$(find target -type d -name web | head -1)
[ -n "$WEB" ] || fail "no web/ directory was produced" pack1.log

mapfile -t files < <(cd "$WEB" && find . -type f | sort)
before=$(for f in "${files[@]}"; do stat -c '%Y %n' "$WEB/$f"; done)

# A full second of separation: some filesystems keep only whole-second
# mtimes, and a stage that DID rewrite a file one second later would still
# have to show a changed reading.
sleep 1

"$MCPP" pack --format web --target wasm32-emscripten > pack2.log 2>&1 \
    || fail "the second mcpp pack --format web failed" pack2.log
after=$(for f in "${files[@]}"; do stat -c '%Y %n' "$WEB/$f"; done)

[ "$before" = "$after" ] || {
    echo "FAIL: a second pack changed at least one file's mtime, so something was recopied"
    diff <(echo "$before") <(echo "$after")
    exit 1
}
echo "ok: every staged file's mtime is unchanged after a second, no-op pack"

rm -f build.log pack1.log pack2.log
echo "PASS: a second mcpp pack --format web copies nothing"
