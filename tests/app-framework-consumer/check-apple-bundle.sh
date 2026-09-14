#!/usr/bin/env bash
# dist-apple on macOS, measured on the bundle and the image (mcpp#634, B1 to B3).
#
# The plan-level half is `check-apple-plan.sh`; this half needs the tools only
# a Mac has (`otool`, `codesign`, `hdiutil`) and runs the program.
#
#   1. The program is linked with the rpath `@executable_path/../Frameworks`,
#      as written: the engine anchors no rpath that begins with a loader token
#      (mcpp 2026.9.14.2+), and the member adds it through `mcpp::link_flag`.
#   2. The bundle carries the dependency's dylib in `Contents/Frameworks/` and
#      not among the resources, keeps the deployed file under
#      `Contents/Resources/`, and is signed ad hoc so that `codesign --verify
#      --deep --strict` accepts it.
#   3. The bundled program loads the framework copy and exits 7 with its
#      output; a copy of the bundle without the framework stops with "Library
#      not loaded".
#   4. `mcpp run --format app`, with no runner in the manifest, runs the bundle
#      through `macapp-run` and returns the program's 7 with its output and
#      arguments; `--runner app` does the same.
#   5. `mcpp pack --format dmg` writes an image `hdiutil verify` accepts, which
#      attaches read-only holding the bundle and an `Applications` link.
#
# Usage: MCPP=<mcpp 2026.9.14.2+> ./check-apple-bundle.sh   (run from this directory)
set -eu

MCPP="${MCPP:-mcpp}"
NAME=AppFrameworkConsumer
EXE=app-framework-consumer
DYLIB=libapp-framework-dep.dylib
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
reading() { printf 'READING %s: %s\n' "$1" "$2"; }
packed() { sed -n 's/^ *Packed //p' "$1" | tail -1; }
work=$(mktemp -d)

# ── 1. the rpath, at link time ─────────────────────────────────────────────
echo "== 1. the program's rpath =="
rm -rf target
"$MCPP" build > bundle-build.log 2>&1 || fail "mcpp build failed" bundle-build.log
if find target -name '*.app' | grep -q .; then fail "a plain build produced a bundle"; fi
programs=$(find target -type f -name "$EXE" ! -path '*/.build-mcpp/*' ! -path '*.dSYM/*')
[ "$(printf '%s\n' "$programs" | grep -c .)" = 1 ] \
    || fail "expected one linked $EXE under target/, found: $(echo $programs)" bundle-build.log
otool -l "$programs" > bundle-loadcommands.log
rpaths=$(awk '/cmd LC_RPATH/ { getline; getline; print $2 }' bundle-loadcommands.log)
reading rpaths "$(echo $rpaths)"
printf '%s\n' "$rpaths" | grep -qxF '@executable_path/../Frameworks' \
    || fail "the program has no LC_RPATH @executable_path/../Frameworks" bundle-loadcommands.log
if printf '%s\n' "$rpaths" | grep -q '.@executable_path'; then
    fail "an rpath carries @executable_path after a directory, anchored" bundle-loadcommands.log
fi
# The dependency's install name is the engine's choice and is recorded, not
# asserted: criterion 3 is what shows the bundle loads the framework copy.
otool -L "$programs" > bundle-needed.log
reading install-name "$(grep "$DYLIB" bundle-needed.log | sed 's/^[[:space:]]*//' | head -1)"
echo "ok: LC_RPATH @executable_path/../Frameworks, as written"

# ── 2. the bundle ──────────────────────────────────────────────────────────
echo "== 2. the bundle carries the framework and is signed ad hoc =="
"$MCPP" pack --format app > bundle-pack.log 2>&1 || fail "mcpp pack --format app failed" bundle-pack.log
app=$(packed bundle-pack.log)
case "$app" in *"/$NAME.app") ;; *) fail "the pack reported '$app', not $NAME.app" bundle-pack.log ;; esac
[ -d "$app" ] || fail "the reported bundle $app is not a directory" bundle-pack.log
[ -f "$app/Contents/Frameworks/$DYLIB" ] || { find "$app" | sort; fail "no Contents/Frameworks/$DYLIB"; }
if find "$app/Contents/Resources" "$app/Contents/MacOS" -name '*.dylib' 2>/dev/null | grep -q .; then
    find "$app" | sort; fail "a dylib is under Contents/Resources or Contents/MacOS"
fi
[ -f "$app/Contents/Resources/data/greeting.txt" ] || { find "$app" | sort; fail "the deployed file is not under Contents/Resources"; }
codesign --verify --deep --strict --verbose=2 "$app" > bundle-codesign.log 2>&1 \
    || fail "codesign --verify --deep --strict refused the bundle" bundle-codesign.log
reading codesign-verify "$(tr '\n' ' ' < bundle-codesign.log)"
# A linked arm64 program is already signed ad hoc by the linker, so the bundle's
# own signature is read from its sealed resources, and the framework's from
# the absence of the linker's flag.
codesign -dv "$app" > bundle-signature.log 2>&1 || true
reading bundle-signature "$(grep -E '^(Signature|CodeDirectory|Sealed Resources)' bundle-signature.log | tr '\n' ' ')"
grep -q '^Signature=adhoc' bundle-signature.log || fail "the bundle's signature is not ad hoc" bundle-signature.log
grep -q '^Sealed Resources' bundle-signature.log || fail "the bundle's resources are not sealed" bundle-signature.log
codesign -dv "$app/Contents/Frameworks/$DYLIB" > bundle-framework-signature.log 2>&1 || true
reading framework-signature "$(grep -E '^(Signature|CodeDirectory)' bundle-framework-signature.log | tr '\n' ' ')"
grep -q '^Signature=adhoc' bundle-framework-signature.log \
    || fail "the framework is not signed" bundle-framework-signature.log
if grep -q 'linker-signed' bundle-framework-signature.log; then
    fail "the framework carries the linker's signature, not one of its own" bundle-framework-signature.log
fi
echo "ok: Contents/Frameworks/$DYLIB, nothing loadable among the resources, and codesign --verify --deep --strict passes"

# ── 3. the bundled program ─────────────────────────────────────────────────
echo "== 3. the bundled program loads the framework =="
rc=0
DYLD_PRINT_LIBRARIES=1 "$app/Contents/MacOS/$EXE" > bundle-run-out.log 2> bundle-run-err.log || rc=$?
reading bundle-run "exit=$rc $(cat bundle-run-out.log)"
[ "$rc" -eq 7 ] || fail "the bundled program exited $rc, not 7" bundle-run-out.log bundle-run-err.log
grep -qx 'framework-1-2-3 argc=1' bundle-run-out.log || fail "the bundled program's output is missing" bundle-run-out.log
loaded=$(grep "$DYLIB" bundle-run-err.log | head -1)
reading loaded "$loaded"
# dyld may print the rpath expansion unnormalised (`Contents/MacOS/../Frameworks`).
case "$loaded" in *"/$NAME.app/Contents/Frameworks/$DYLIB" | *"/$NAME.app/Contents/MacOS/../Frameworks/$DYLIB") ;;
    *) fail "the program did not load the framework copy" bundle-run-err.log ;; esac
cp -R "$app" "$work/"
rm "$work/$NAME.app/Contents/Frameworks/$DYLIB"
rc=0
"$work/$NAME.app/Contents/MacOS/$EXE" > bundle-noframework.log 2>&1 || rc=$?
reading without-framework "exit=$rc $(head -2 bundle-noframework.log | tr '\n' ' ')"
[ "$rc" -ne 0 ] || fail "the program ran without its framework" bundle-noframework.log
grep -q 'Library not loaded' bundle-noframework.log || fail "the failure is not 'Library not loaded'" bundle-noframework.log
echo "ok: exit 7 through the framework copy; without it, exit $rc and 'Library not loaded'"

# ── 4. mcpp run --format app ───────────────────────────────────────────────
echo "== 4. mcpp run --format app, through the runner dist-apple supplies =="
# The manifest's keys, not its prose: the header comment names the runner this
# member supplies, so the comments are removed before the search.
if sed 's/#.*$//' mcpp.toml | grep -q 'runner'; then fail "the fixture's manifest names a runner" mcpp.toml; fi
# mcpp_run <variant> <mcpp run arguments...>
mcpp_run() {
    local variant="$1"; shift
    rc=0
    "$MCPP" run "$@" > "bundle-mcpp-run-$variant.log" 2>&1 || rc=$?
    reading "mcpp-run-$variant" "exit=$rc $(grep -m1 '`macapp-run' "bundle-mcpp-run-$variant.log" || echo 'no status line names macapp-run')"
    [ "$rc" -eq 7 ] || fail "mcpp run $* exited $rc, not 7" "bundle-mcpp-run-$variant.log"
    # The status line's verb is `Running` when the format selects the runner
    # and the runner's own name (`App`) when `--runner` does (macos-15, run
    # 34825033706), so the criterion is the command the line names.
    grep -qE '^ *[A-Z][A-Za-z-]* `macapp-run ' "bundle-mcpp-run-$variant.log" \
        || fail "no status line names macapp-run" "bundle-mcpp-run-$variant.log"
    grep -qx 'framework-1-2-3 argc=2' "bundle-mcpp-run-$variant.log" \
        || fail "the program's output with one argument is missing" "bundle-mcpp-run-$variant.log"
}
mcpp_run format --format app -- extra
mcpp_run runner --format app --runner app -- extra
echo "ok: mcpp run --format app, and with --runner app, return 7 with the program's output and argument"

# ── 5. the disk image ──────────────────────────────────────────────────────
echo "== 5. mcpp pack --format dmg =="
"$MCPP" pack --format dmg > bundle-dmg.log 2>&1 || fail "mcpp pack --format dmg failed" bundle-dmg.log
dmg=$(packed bundle-dmg.log)
case "$dmg" in *"/$NAME.dmg") ;; *) fail "the pack reported '$dmg', not $NAME.dmg" bundle-dmg.log ;; esac
[ -f "$dmg" ] || fail "the reported image $dmg does not exist" bundle-dmg.log
hdiutil verify "$dmg" > bundle-hdiutil-verify.log 2>&1 || fail "hdiutil verify refused the image" bundle-hdiutil-verify.log
reading hdiutil-verify "$(grep -i 'checksum' bundle-hdiutil-verify.log | tail -1)"
mnt="$work/mnt"
mkdir -p "$mnt"
hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" > bundle-attach.log 2>&1 \
    || fail "hdiutil attach failed" bundle-attach.log
detach() { hdiutil detach "$mnt" > /dev/null 2>&1 || hdiutil detach -force "$mnt" > /dev/null 2>&1 || true; }
trap detach EXIT
reading image-root "$(ls -1 "$mnt" | tr '\n' ' ')"
[ -x "$mnt/$NAME.app/Contents/MacOS/$EXE" ] || fail "the image does not hold the bundle's program"
[ -f "$mnt/$NAME.app/Contents/Frameworks/$DYLIB" ] || fail "the image's bundle lacks the framework"
[ -L "$mnt/Applications" ] || fail "the image holds no Applications link"
[ "$(readlink "$mnt/Applications")" = /Applications ] || fail "the Applications link points to $(readlink "$mnt/Applications")"
codesign --verify --deep --strict "$mnt/$NAME.app" > bundle-image-codesign.log 2>&1 \
    || fail "the bundle inside the image does not verify" bundle-image-codesign.log
detach
trap - EXIT
echo "ok: the image verifies and attaches read-only with the signed bundle and Applications -> /Applications"

echo "PASS: dist-apple's framework, rpath, ad-hoc signature, app runner and disk image, on the bundle"
