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

# THE BUNDLE STEP IS THE PLAN'S SOLE TERMINAL ARTIFACT (#622: "mcpp run
# --format app produced 4 distributables ... needs exactly one"). `info`,
# `layout`, `icon` and `codesign` are parallel -- none consumes another's
# output -- so without a step naming the bundle directory itself as ITS
# output, `mcpp run --format app` has as many terminal artifacts as steps ran
# and no single operand to hand the runner. This checks the property, not
# just the id's presence: every OTHER planned step's own output must be named
# among `bundle`'s own inputs, and `bundle`'s own output (the `.app`
# directory) must be named in no step's inputs anywhere in the plan -- the
# one thing left that nothing consumes.
# Both restricted to the named FIELD, never the whole JSON line: `command`
# (ditto's own argv) legitimately names the bundle directory as a
# destination on every row, and matching against the whole line would read
# that as "consumes", which it is not -- only `inputs` decides the graph's
# edges.
quoted_outputs() { # $1 = one action's JSON line
    echo "$1" | grep -oP '(?<="outputs":\[)[^]]*' | grep -oP '"[^"]*"'
}
quoted_inputs() { # $1 = one action's JSON line
    echo "$1" | grep -oP '(?<="inputs":\[)[^]]*' | grep -oP '"[^"]*"'
}
check_terminal_bundle() {
    local log="$1" bundle_line bundle_out other_id other_line q found_any=0
    bundle_line=$(grep -o '"id":"mcpp\.dist\.apple\.bundle"[^}]*}' "$log") \
        || fail "no bundle step planned" "$log"
    [ -n "$bundle_line" ] || fail "no bundle step planned" "$log"
    bundle_out=$(quoted_outputs "$bundle_line")
    [ "$(echo "$bundle_out" | wc -l)" -eq 1 ] \
        || fail "the bundle step does not declare exactly one output" "$log"
    echo "$bundle_out" | grep -q '\.app"$' \
        || fail "the bundle step's own output is not the .app directory" "$log"
    local bundle_inputs
    bundle_inputs=$(quoted_inputs "$bundle_line")
    for other_id in mcpp.dist.apple.info-plist mcpp.dist.apple.layout \
                    mcpp.dist.apple.icon mcpp.dist.apple.provisioning-profile \
                    mcpp.dist.apple.codesign; do
        other_line=$(grep -o "\"id\":\"$other_id\"[^}]*}" "$log") || true
        [ -n "$other_line" ] || continue
        found_any=1
        while IFS= read -r q; do
            [ -n "$q" ] || continue
            grep -qF "$q" <<<"$bundle_inputs" \
                || fail "the bundle step does not depend on $other_id's own output ($q)" "$log"
        done <<<"$(quoted_outputs "$other_line")"
        # And the reverse: no OTHER step may already name the bundle
        # directory among its OWN inputs -- that would make it, not
        # `bundle`, the one this plan's earlier steps are ordered against.
        grep -qF "$bundle_out" <<<"$(quoted_inputs "$other_line")" \
            && fail "$other_id names the bundle directory among its own inputs" "$log"
    done
    [ "$found_any" -eq 1 ] || fail "no content step (info/layout/icon/codesign) was planned to check against" "$log"
    # Nothing anywhere in the plan consumes the bundle step's own output (as
    # an INPUT, never as an argv destination) -- the property that makes it
    # the sole terminal artifact.
    while IFS= read -r other_line; do
        [ -n "$other_line" ] || continue
        grep -qF "$bundle_out" <<<"$(quoted_inputs "$other_line")" \
            && fail "something in the plan still consumes the bundle directory as an input; it is not the sole terminal" "$log"
    done <<<"$(grep -o '"id":"mcpp\.dist\.apple\.[a-z-]*"[^}]*}' "$log" | grep -v '"id":"mcpp\.dist\.apple\.bundle"')"
    echo "ok: mcpp.dist.apple.bundle is the plan's sole terminal artifact ($bundle_out)"
}

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
        # The entry script the engine writes at the tree's root beside a Mach-O
        # program (mcpp 2026.9.14.2+), which a bundle must not execute.
        printf '#!/bin/sh\nhere=$(cd "$(dirname "$0")" && pwd)\nexec "$here/bin/ios-app-consumer" "$@"\n' \
            > "$stage/ios-app-consumer"
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
        IOS_APP_CONSUMER_INFO_PLIST="${IOS_APP_CONSUMER_INFO_PLIST:-}" \
        IOS_APP_CONSUMER_PROFILE="${IOS_APP_CONSUMER_PROFILE:-}" \
        IOS_APP_CONSUMER_NO_IDENTITY="${IOS_APP_CONSUMER_NO_IDENTITY:-}" \
        IOS_APP_CONSUMER_OMIT_KEYS="${IOS_APP_CONSUMER_OMIT_KEYS:-}" \
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
grep 'mcpp.dist.apple.layout' "$log" | grep -q '"command":\["ditto","[^"]*/bin/ios-app-consumer",' \
    || fail "the bundle executable is not the staged program bin/ios-app-consumer" "$log"
grep -q 'holds only' "$log" \
    && fail "a non-empty staged tree still produced the 0-byte staged-tree warning" "$log"
echo "ok: flat layout, the staged program as the executable, no codesign, a named warning, and every iOS-only plist key"
check_terminal_bundle "$log"

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
check_terminal_bundle "$log"

echo "== iOS Simulator row, a manifest directory with no ios-icons/ (#622 B2 defect 2) =="
badmanifest=$(mktemp -d)
run_row simbadicon ios sim /tmp/ios-plan-sim-badicon.log "" "$badmanifest" > /dev/null
log=/tmp/ios-plan-sim-badicon.log
grep -q 'options::icon' "$log" || fail "the missing-icon refusal did not name options::icon" "$log"
grep -qF "$badmanifest/ios-icons" "$log" \
    || fail "the missing-icon refusal did not resolve options::icon against MCPP_MANIFEST_DIR" "$log"
grep -q 'mcpp.dist.apple.layout' "$log" \
    && fail "a layout step was planned even though the icon directory was refused" "$log"
grep -q 'mcpp.dist.apple.bundle' "$log" \
    && fail "a bundle step was planned even though the icon directory was refused" "$log"
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
check_terminal_bundle "$log"

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
check_terminal_bundle "$log"

# ── 0.11.0 ─────────────────────────────────────────────────────────────────
echo "== 0.11.0: the simulator row supplies no device runner =="
grep -q 'mcpp:runner-named=app:devicectl-run' /tmp/ios-plan-sim.log \
    && fail "the simulator row supplies devicectl-run" /tmp/ios-plan-sim.log
grep -q 'mcpp:runner-named=app:devicectl-run' /tmp/ios-plan-device.log \
    || fail "the device row does not supply the runner named app, devicectl-run" /tmp/ios-plan-device.log
echo "ok: the device row supplies devicectl-run under the name app, and the simulator row does not"

echo "== 0.11.0: a project's Info.plist entries =="
export IOS_APP_CONSUMER_INFO_PLIST="$PWD/info-plist/usage.plist"
outdir=$(run_row siminfo ios sim /tmp/ios-plan-sim-info.log)
plist="$outdir/IosAppConsumer-Info.plist"
[ -f "$plist" ] || fail "no Info.plist written with options::info_plist" /tmp/ios-plan-sim-info.log
python3 - "$plist" <<'PY' || fail "the Info.plist does not carry the project's entries as a property list" "$plist"
import plistlib, sys
d = plistlib.load(open(sys.argv[1], "rb"))
assert d["NSCameraUsageDescription"].startswith("Scans"), d
assert d["UIDeviceFamily"] == [1], d["UIDeviceFamily"]
assert d["CFBundleURLTypes"][0]["CFBundleURLSchemes"] == ["mcpp-fixture"], d
assert d["CFBundleIdentifier"] == "ios-app-consumer", d
assert d["CFBundleSupportedPlatforms"] == ["iPhoneSimulator"], d
PY
[ "$(grep -c '<key>UIDeviceFamily</key>' "$plist")" -eq 1 ] || fail "UIDeviceFamily is stated more than once" "$plist"
echo "ok: the project's entries join the plist, its UIDeviceFamily replaces the default, and the derived keys are the member's"
check_terminal_bundle /tmp/ios-plan-sim-info.log

export IOS_APP_CONSUMER_INFO_PLIST="$PWD/info-plist/derived.plist"
run_row simderived ios sim /tmp/ios-plan-sim-derived.log > /dev/null
grep -q 'sets CFBundleIdentifier, which this member derives' /tmp/ios-plan-sim-derived.log \
    || fail "an Info.plist entry the member derives was not refused by name" /tmp/ios-plan-sim-derived.log
grep -q 'mcpp.dist.apple.layout' /tmp/ios-plan-sim-derived.log \
    && fail "steps were planned although the Info.plist was refused" /tmp/ios-plan-sim-derived.log
unset IOS_APP_CONSUMER_INFO_PLIST
echo "ok: an entry this member derives is refused, naming the key"

# ── 0.12.0 ─────────────────────────────────────────────────────────────────
echo "== 0.12.0: options::omit_keys leaves a defaulted key out =="
export IOS_APP_CONSUMER_OMIT_KEYS="LSRequiresIPhoneOS"
outdir=$(run_row simomit ios sim /tmp/ios-plan-sim-omit.log)
plist="$outdir/IosAppConsumer-Info.plist"
[ -f "$plist" ] || fail "no Info.plist written with options::omit_keys" /tmp/ios-plan-sim-omit.log
grep -q '<key>LSRequiresIPhoneOS</key>' "$plist" && fail "LSRequiresIPhoneOS was written although omitted" "$plist"
grep -q '<key>UIDeviceFamily</key>' "$plist" || fail "omitting one default removed another" "$plist"
check_terminal_bundle /tmp/ios-plan-sim-omit.log
export IOS_APP_CONSUMER_OMIT_KEYS="NSHighResolutionCapable,UIDeviceFamily"
outdir=$(run_row macosomit macos "" /tmp/ios-plan-macos-omit.log)
plist="$outdir/IosAppConsumer-Info.plist"
grep -q '<key>NSHighResolutionCapable</key>' "$plist" && fail "NSHighResolutionCapable was written on macOS although omitted" "$plist"
grep -q '<key>CFBundleExecutable</key>' "$plist" || fail "the macOS plist lost a derived key" "$plist"
echo "ok: an omitted default is not written, on iOS and on macOS, and the rest of the plist is unchanged"

export IOS_APP_CONSUMER_OMIT_KEYS="CFBundleIdentifier"
run_row simomitderived ios sim /tmp/ios-plan-sim-omit-derived.log > /dev/null
grep -q 'names CFBundleIdentifier, which this member derives' /tmp/ios-plan-sim-omit-derived.log \
    || fail "omitting a derived key was not refused by name" /tmp/ios-plan-sim-omit-derived.log
grep -q 'mcpp.dist.apple.layout' /tmp/ios-plan-sim-omit-derived.log \
    && fail "steps were planned although omit_keys was refused" /tmp/ios-plan-sim-omit-derived.log
export IOS_APP_CONSUMER_OMIT_KEYS="NSCameraUsageDescription"
run_row simomitother ios sim /tmp/ios-plan-sim-omit-other.log > /dev/null
grep -q 'names NSCameraUsageDescription, which this member does not write' /tmp/ios-plan-sim-omit-other.log \
    || fail "omitting a key this member does not default was not refused by name" /tmp/ios-plan-sim-omit-other.log
export IOS_APP_CONSUMER_OMIT_KEYS="UIDeviceFamily"
export IOS_APP_CONSUMER_INFO_PLIST="$PWD/info-plist/usage.plist"
run_row simomitboth ios sim /tmp/ios-plan-sim-omit-both.log > /dev/null
grep -q 'names UIDeviceFamily, and `options::info_plist`' /tmp/ios-plan-sim-omit-both.log \
    || fail "a key both omitted and set by info_plist was not refused" /tmp/ios-plan-sim-omit-both.log
unset IOS_APP_CONSUMER_OMIT_KEYS IOS_APP_CONSUMER_INFO_PLIST
echo "ok: omitting a derived key, a key this member does not write, or a key info_plist sets is refused by name"

# A provisioning profile is a CMS-signed plist; the member reads the plist from
# between its markers, so a plan needs only those bytes framed by binary data.
make_profile() {  # make_profile <file> <application-identifier>
    {
        printf '\x30\x82\x0b\x2a\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x07\x02\xa0'
        cat <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>AppIDName</key>
	<string>fixture</string>
	<key>Entitlements</key>
	<dict>
		<key>application-identifier</key>
		<string>$2</string>
		<key>com.apple.developer.team-identifier</key>
		<string>ABCDE12345</string>
		<key>get-task-allow</key>
		<true/>
	</dict>
	<key>Name</key>
	<string>fixture profile</string>
</dict>
</plist>
P
        printf '\xa0\x82\x03\x00\x30\x82'
    } > "$1"
}
profiles=$(mktemp -d)
make_profile "$profiles/development.mobileprovision" "ABCDE12345.ios-app-consumer"
make_profile "$profiles/other.mobileprovision" "ABCDE12345.org.example.other"
make_profile "$profiles/wildcard.mobileprovision" "ABCDE12345.*"

echo "== 0.11.0: the device row embeds a provisioning profile and signs with its entitlements =="
export IOS_APP_CONSUMER_PROFILE="$profiles/development.mobileprovision"
outdir=$(run_row deviceprofile ios "" /tmp/ios-plan-device-profile.log)
log=/tmp/ios-plan-device-profile.log
grep -q '"id":"mcpp.dist.apple.provisioning-profile"' "$log" || fail "no step embeds the profile" "$log"
grep '"id":"mcpp.dist.apple.provisioning-profile"' "$log" | grep -q 'IosAppConsumer.app/embedded.mobileprovision' \
    || fail "the profile is not embedded as embedded.mobileprovision" "$log"
ents="$outdir/IosAppConsumer-entitlements.plist"
grep '"id":"mcpp.dist.apple.codesign"' "$log" | grep -qF "\"--entitlements\",\"$ents\"" \
    || fail "codesign does not sign with the entitlements derived from the profile" "$log"
python3 - "$ents" <<'PY' || fail "the derived entitlements are not the profile's" "$ents"
import plistlib, sys
d = plistlib.load(open(sys.argv[1], "rb"))
assert d["application-identifier"] == "ABCDE12345.ios-app-consumer", d
assert d["get-task-allow"] is True, d
PY
echo "ok: embedded.mobileprovision is planned, and codesign signs with the profile's own entitlements"
check_terminal_bundle "$log"

export IOS_APP_CONSUMER_PROFILE="$profiles/wildcard.mobileprovision"
outdir=$(run_row devicewild ios "" /tmp/ios-plan-device-wild.log)
python3 - "$outdir/IosAppConsumer-entitlements.plist" <<'PY' || fail "a wildcard profile does not sign with the bundle's own identifier" /tmp/ios-plan-device-wild.log "$outdir/IosAppConsumer-entitlements.plist"
import plistlib, sys
d = plistlib.load(open(sys.argv[1], "rb"))
assert d["application-identifier"] == "ABCDE12345.ios-app-consumer", d
assert d["com.apple.developer.team-identifier"] == "ABCDE12345", d
PY
echo "ok: a wildcard profile covers the bundle, and the signature states ABCDE12345.ios-app-consumer"

export IOS_APP_CONSUMER_PROFILE="$profiles/other.mobileprovision"
run_row deviceother ios "" /tmp/ios-plan-device-other.log > /dev/null
grep -q 'does not cover the bundle identifier' /tmp/ios-plan-device-other.log \
    || fail "a profile for another identifier was not refused" /tmp/ios-plan-device-other.log
export IOS_APP_CONSUMER_PROFILE="$profiles/development.mobileprovision"
run_row simprofile ios sim /tmp/ios-plan-sim-profile.log > /dev/null
grep -q 'is embedded in an iOS device bundle' /tmp/ios-plan-sim-profile.log \
    || fail "a profile on the simulator row was not refused" /tmp/ios-plan-sim-profile.log
export IOS_APP_CONSUMER_NO_IDENTITY=1
run_row deviceanon ios "" /tmp/ios-plan-device-anon.log > /dev/null
grep -q '`options::identity` is not' /tmp/ios-plan-device-anon.log \
    || fail "a profile without an identity was not refused" /tmp/ios-plan-device-anon.log
unset IOS_APP_CONSUMER_PROFILE IOS_APP_CONSUMER_NO_IDENTITY
echo "ok: a profile for another identifier, on the simulator row, or without an identity is refused"

echo "PASS: dist-apple's iOS row, at the plan level"
