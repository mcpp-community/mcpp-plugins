#!/usr/bin/env bash
# Plan-level check for the iOS row of `dist-apple` (#622 B1).
#
# See mcpp.toml's header comment for why this is the plan level and not a
# real pack: no host reachable by this repository's CI has an iphonesimulator
# or iphoneos SDK, so `--target aarch64-ios-sim` and `aarch64-ios` both refuse
# before `build.mcpp` runs, on every runner including the macOS one (Xcode's
# SDKs are a separate download `xcrun --sdk iphonesimulator --show-sdk-path`
# does not find on a bare `macos-15` image). What this script measures
# instead is the compiled build program's actual behaviour under the exact
# environment contract the engine would set -- `mcpp::target_os()` and its
# siblings are `std::getenv` and nothing else (`hostprogram.cppm`), so a
# process given the same variables takes the same branches `plan_for` would.
#
# Usage: MCPP=<fresh mcpp> ./check-ios-plan.sh   (run from this directory)
set -e

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }

rm -rf target
"$MCPP" build > build.log 2>&1 || fail "the host build failed to compile build.mcpp" build.log
BIN=target/.build-mcpp/build.mcpp.bin
[ -x "$BIN" ] || fail "no compiled build.mcpp.bin at $BIN" build.log
echo "ok: build.mcpp compiled to $BIN"

run_row() {
    # $1=label $2=MCPP_TARGET_OS $3=MCPP_TARGET_ENV(or empty) $4=out log path
    # $5=stage mode: "" (default) fabricates a non-empty staged tree, exactly
    #    as every row did before this parameter existed; "EMPTY" instead sends
    #    MCPP_PACK_STAGE_DIR="" -- the shape a host that cannot walk an iOS
    #    Mach-O's closure actually hands this member.
    # $6=MCPP_MANIFEST_DIR override, empty by default (matches every row's
    #    behaviour before this parameter existed: the fixture's own directory,
    #    read through this script's cwd rather than through the variable).
    local label="$1" os="$2" envv="$3" log="$4" stage_mode="${5:-}" manifest="${6:-}"
    local stage out
    if [ "$stage_mode" = "EMPTY" ]; then
        stage=""
    else
        stage=$(mktemp -d)
        mkdir -p "$stage/bin"
        head -c 5000 /dev/urandom > "$stage/bin/ios-app-consumer"
    fi
    out=$(mktemp -d)
    env -i PATH="$PATH" \
        MCPP_PACK_FORMAT=app \
        MCPP_TARGET_OS="$os" \
        MCPP_TARGET_ENV="$envv" \
        MCPP_PACK_STAGE_DIR="$stage" \
        MCPP_MANIFEST_DIR="$manifest" \
        MCPP_TARGET_MIN_PLATFORM_VERSION="$([ "$os" = ios ] && echo 18.0)" \
        MCPP_PKG_NAME=ios-app-consumer \
        MCPP_PKG_VERSION=0.3.0 \
        MCPP_OUT_DIR="$out" \
        "$BIN" > "$log" 2>&1
    echo "$out"
}

echo "== iOS Simulator row =="
outdir=$(run_row sim ios sim /tmp/ios-plan-sim.log)
log=/tmp/ios-plan-sim.log
plist="$outdir/IosAppConsumer-Info.plist"
[ -f "$plist" ] || fail "no Info.plist written for the simulator row" "$log"
grep -q 'mcpp.dist.apple.layout' "$log" || fail "no layout step planned" "$log"
grep 'mcpp.dist.apple.layout' "$log" | grep -q '/Contents/' \
    && fail "the simulator layout step still names Contents/ -- not flat" "$log"
grep -q '"id":"mcpp.dist.apple.codesign"' "$log" \
    && fail "a codesign step was planned on the simulator row" "$log"
grep -q 'is ignored on the iOS Simulator row' "$log" \
    || fail "no warning explains why signing was skipped on the simulator row" "$log"
grep -q '<key>CFBundleSupportedPlatforms</key>' "$plist" || fail "no CFBundleSupportedPlatforms key" "$plist"
grep -A2 '<key>CFBundleSupportedPlatforms</key>' "$plist" | grep -q 'iPhoneSimulator' \
    || fail "CFBundleSupportedPlatforms does not name iPhoneSimulator" "$plist"
grep -q '<key>UIDeviceFamily</key>' "$plist" || fail "no UIDeviceFamily key" "$plist"
grep -q '<key>LSRequiresIPhoneOS</key>' "$plist" || fail "no LSRequiresIPhoneOS key" "$plist"
grep -A1 '<key>MinimumOSVersion</key>' "$plist" | grep -q '18.0' \
    || fail "MinimumOSVersion did not carry the engine's floor" "$plist"
grep -q 'LSMinimumSystemVersion' "$plist" && fail "the iOS plist carries the macOS key" "$plist"
grep -q '<key>CFBundleIcons</key>' "$plist" || fail "no CFBundleIcons key for the directory icon" "$plist"
grep -q 'AppIcon60x60@2x</string>' "$plist" || fail "the icon list is missing a stem"  "$plist"
grep -q 'AppIcon76x76@2x~ipad</string>' "$plist" || fail "the icon list is missing a stem" "$plist"
# THE STAGED-TREE SIDE OF THE mcpp.stage_dir / target_file DECISION: this row
# fabricates a non-empty MCPP_PACK_STAGE_DIR (see run_row), so the layout step
# must name the tree, and the floor check in `submit()` must find enough
# bytes in it to stay quiet.
grep 'mcpp.dist.apple.layout' "$log" | grep -q '\${mcpp\.stage_dir}' \
    || fail "the layout step did not name \${mcpp.stage_dir} for a non-empty staged tree" "$log"
grep -q 'holds only' "$log" \
    && fail "a non-empty staged tree still produced the 0-byte staged-tree warning" "$log"
echo "ok: flat layout, no codesign, a named warning, and every iOS-only plist key"

echo "== iOS Simulator row, an empty pack_stage_dir (#622 B2 defect 1) =="
run_row simnostage ios sim /tmp/ios-plan-sim-nostage.log EMPTY > /dev/null
log=/tmp/ios-plan-sim-nostage.log
grep -q 'mcpp.dist.apple.layout' "$log" || fail "no layout step planned with an empty stage dir" "$log"
grep 'mcpp.dist.apple.layout' "$log" | grep -q '\${mcpp\.target_file:ios-app-consumer}' \
    || fail "the layout step did not name \${mcpp.target_file:...} with an empty stage dir" "$log"
grep 'mcpp.dist.apple.layout' "$log" | grep -q '\${mcpp\.stage_dir}' \
    && fail "the layout step named \${mcpp.stage_dir} even though pack_stage_dir() was empty" "$log"
grep -q 'holds only' "$log" \
    && fail "an empty pack_stage_dir produced the misleading 0-byte staged-tree warning" "$log"
echo "ok: an empty pack_stage_dir takes the single-binary path, named through \${mcpp.target_file:...}, with no staged-tree warning"

echo "== iOS Simulator row, a manifest directory with no ios-icons/ (#622 B2 defect 2) =="
badmanifest=$(mktemp -d)
run_row simbadicon ios sim /tmp/ios-plan-sim-badicon.log "" "$badmanifest" > /dev/null
log=/tmp/ios-plan-sim-badicon.log
grep -q 'options::icon' "$log" || fail "the missing-icon refusal did not name options::icon" "$log"
grep -qF "$badmanifest/ios-icons" "$log" \
    || fail "the missing-icon refusal did not resolve options::icon against MCPP_MANIFEST_DIR" "$log"
grep -q 'mcpp.dist.apple.layout' "$log" \
    && fail "a layout step was planned even though the icon directory was refused" "$log"
echo "ok: a relative options::icon resolves against MCPP_MANIFEST_DIR, and a missing directory is refused at plan time, naming options::icon and the path"

echo "== iOS device row =="
outdir=$(run_row device ios "" /tmp/ios-plan-device.log)
log=/tmp/ios-plan-device.log
plist="$outdir/IosAppConsumer-Info.plist"
grep -q '"id":"mcpp.dist.apple.codesign"' "$log" || fail "no codesign step planned on the device row" "$log"
grep -q 'is ignored on the iOS Simulator row' "$log" \
    && fail "the simulator warning fired on the device row" "$log"
grep -A2 '<key>CFBundleSupportedPlatforms</key>' "$plist" | grep -q 'iPhoneOS' \
    || fail "CFBundleSupportedPlatforms does not name iPhoneOS on the device row" "$plist"
echo "ok: codesign is planned, and CFBundleSupportedPlatforms names iPhoneOS"

echo "== macOS row is unaffected =="
outdir=$(run_row macos macos "" /tmp/ios-plan-macos.log)
log=/tmp/ios-plan-macos.log
plist="$outdir/IosAppConsumer-Info.plist"
grep 'mcpp.dist.apple.layout' "$log" | grep -q '/Contents/MacOS' \
    || fail "the macOS layout step no longer names Contents/MacOS" "$log"
grep -q '"id":"mcpp.dist.apple.icon"' "$log" \
    && fail "an icon step was planned on macOS, where this fixture sets no icon" "$log"
for key in CFBundleSupportedPlatforms UIDeviceFamily LSRequiresIPhoneOS MinimumOSVersion CFBundleIcons; do
    grep -q "<key>$key</key>" "$plist" && fail "the macOS plist carries the iOS-only key $key" "$plist"
done
grep -q '<key>NSHighResolutionCapable</key>' "$plist" || fail "the macOS plist lost NSHighResolutionCapable" "$plist"
echo "ok: the macOS row's steps and plist are unchanged by the iOS branch"

echo "PASS: dist-apple's iOS row, at the plan level"
