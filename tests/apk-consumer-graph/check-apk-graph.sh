#!/usr/bin/env bash
# dist-apk collects library contributions from the resolved graph (0.12.0).
#
# `lib/` states `[package.metadata.dist-apk]` with its resources, and the
# application names it only as a dependency. Criteria, by engine:
#
#   mcpp 2026.9.16.1 or newer (the root build program receives the graph):
#     1. the library's string resource is in the APK with the library's value;
#     2. `options::graph_libraries = false` packs no contribution;
#     3. an application library defining the same resource wins it;
#     4. a malformed contribution is refused, naming the package and the key.
#   an older engine (no graph):
#     5. the pack succeeds, and the APK carries no contribution.
#
# Usage: MCPP=<mcpp> ./check-apk-graph.sh   (run from this directory)
set -eu

MCPP="${MCPP:-mcpp}"
TARGET=x86_64-linux-android
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }
packed() { sed -n 's/^ *Packed //p' "$1" | tail -1; }

"$MCPP" self env > graph-env.log
MCPP_HOME_DIR=$(awk -F'= *' '/^MCPP_HOME/{print $2; exit}' graph-env.log)
[ -n "$MCPP_HOME_DIR" ] || fail "could not read MCPP_HOME" graph-env.log
BT=$(find "$MCPP_HOME_DIR/registry/data/xpkgs/xim-x-android-build-tools" -mindepth 1 -maxdepth 1 -type d | head -1)
AAPT2="$BT/aapt2"
[ -x "$AAPT2" ] || fail "aapt2 not found under $BT" graph-env.log

# marker_of <apk> : the value of string/graph_contribution_marker, or empty.
marker_of() {
    "$AAPT2" dump resources "$1" 2>/dev/null \
        | awk '/string\/graph_contribution_marker$/ { getline; sub(/^ *\(\) "/, ""); sub(/"$/, ""); print; exit }'
}

pack() {  # pack <log>
    rm -rf target
    "$MCPP" pack --format apk --target "$TARGET" > "$1" 2>&1 || fail "mcpp pack failed" "$1"
    local apk; apk=$(packed "$1")
    [ -n "$apk" ] && [ -f "$apk" ] || fail "no APK reported" "$1"
    echo "$apk"
}

engine=$("$MCPP" --version | awk '{print $2}')
unset APK_GRAPH_OFF APK_GRAPH_OWN || true

if [ "$(printf '%s\n%s\n' 2026.9.16.1 "$engine" | sort -V | head -1)" != 2026.9.16.1 ]; then
    echo "== (5) $engine gives build programs no graph =="
    apk=$(pack graph-old.log)
    value=$(marker_of "$apk")
    [ -z "$value" ] || fail "an engine without a graph packed a contribution ($value)" graph-old.log
    echo "ok: the pack succeeds under $engine and carries no contribution"
    rm -rf target graph-*.log
    echo "PASS: dist-apk's graph contribution, older-engine leg ($engine)"
    exit 0
fi

echo "== (1) the library's contribution reaches the APK =="
apk=$(pack graph-1.log)
value=$(marker_of "$apk")
[ "$value" = "from-the-graph" ] || fail "the library's resource is not in the APK (got: '${value}')" graph-1.log
echo "ok: string/graph_contribution_marker = from-the-graph"

echo "== (2) options::graph_libraries = false =="
export APK_GRAPH_OFF=1
apk=$(pack graph-2.log)
value=$(marker_of "$apk")
[ -z "$value" ] || fail "graph_libraries = false still packed the contribution ($value)" graph-2.log
unset APK_GRAPH_OFF
echo "ok: no contribution is read"

echo "== (3) the application's own library ranks above the graph's =="
export APK_GRAPH_OWN=1
apk=$(pack graph-3.log)
value=$(marker_of "$apk")
[ "$value" = "from-the-application" ] || fail "the graph's library won a resource the application's library defines (got: '${value}')" graph-3.log
unset APK_GRAPH_OWN
echo "ok: string/graph_contribution_marker = from-the-application"

echo "== (4) a malformed contribution is refused, naming the package and the key =="
COPY="../.apk-consumer-graph-malformed"
rm -rf "$COPY"
mkdir -p "$COPY"
cp -r mcpp.toml build.mcpp src app-res lib "$COPY"/
sed -i.bak 's/^resources = "res"$/resources = 3/' "$COPY/lib/mcpp.toml"
grep -q '^resources = 3$' "$COPY/lib/mcpp.toml" || fail "the malformed copy was not written" "$COPY/lib/mcpp.toml"
if ( cd "$COPY" && "$MCPP" pack --format apk --target "$TARGET" > refuse.log 2>&1 ); then
    fail "a contribution stating resources = 3 was packed" "$COPY/refuse.log"
fi
grep -q 'mcpp.apk-graph-lib@0.1.0 states `resources` as something other than a path' "$COPY/refuse.log" \
    || fail "the refusal does not name the package and the key" "$COPY/refuse.log"
rm -rf "$COPY"
echo "ok: the refusal names mcpp.apk-graph-lib@0.1.0 and resources"

rm -rf target graph-*.log
echo "PASS: dist-apk collects library contributions from the resolved graph ($engine)"
