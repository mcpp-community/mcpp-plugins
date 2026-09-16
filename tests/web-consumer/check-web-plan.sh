#!/usr/bin/env bash
# End-to-end check for `dist-web` (#622 B3): the one-command form.
#
#   mcpp pack --format web --target wasm32-emscripten
#
# An earlier revision of this script worked around a host-toolchain defect
# that made every `build.mcpp` -- not only this member's -- fail to compile
# under `--target wasm32-emscripten` (prepare.cppm's `host_tc_for_build_program`
# resolved the row's own default pin as if it were a host-native compiler).
# That is fixed (mcpp commit 23974c5e, "a build program under a target row's
# pin resolves the host's compiler, not the row's", #622 e2e 657), so this
# script now runs the real command directly and asserts its real output.
#
# Usage: MCPP=<mcpp with the 23974c5e fix> ./check-web-plan.sh   (run from
# this directory)
set -e

MCPP="${MCPP:-mcpp}"
fail() { echo "FAIL: $1"; shift; for f in "$@"; do echo "--- $f ---"; cat "$f" 2>/dev/null; done; exit 1; }

command -v node >/dev/null || fail "node is required to run the produced launcher" /dev/null

rm -rf target

echo "== a plain build has no distribution edge =="
"$MCPP" build --target wasm32-emscripten > build.log 2>&1 \
    || fail "mcpp build --target wasm32-emscripten failed" build.log
if find target -type d -name web | grep -q .; then
    echo "FAIL: a plain build produced a web/ directory"; exit 1
fi
echo "ok: no web/ directory before packaging"

echo "== mcpp pack --format web --target wasm32-emscripten =="
"$MCPP" pack --format web --target wasm32-emscripten > pack.log 2>&1 \
    || fail "mcpp pack --format web --target wasm32-emscripten failed" pack.log
WEB=$(find target -type d -name web | head -1)
[ -n "$WEB" ] || fail "no web/ directory was produced" pack.log
echo "ok: $WEB"

echo "== the file list is exactly the stem family, the deployed asset, and index.html =="
mapfile -t files < <(cd "$WEB" && find . -type f | sed 's#^\./##' | sort)
want=(assets/greeting.txt index.html web-consumer.js web-consumer.wasm)
[ "${files[*]}" = "${want[*]}" ] || {
    echo "FAIL: the produced directory's file list is not exactly the expected set"
    echo "  got:    ${files[*]}"
    echo "  wanted: ${want[*]}"
    exit 1; }
echo "ok: exactly js, wasm, the deployed asset and index.html -- nothing else"

# THE DEPLOYED ASSET LANDS AT ITS STAGED PATH, NOT NESTED UNDER ITSELF.
# `mcpp::deploy`'s `to` names a DIRECTORY (`assets`), and the file keeps its
# own basename (`greeting.txt`) -- the same rule `[[runtime.deploy]]` and
# `types.cppm`'s `DeployEntry` document. A `to` that names the full
# destination path instead (`"assets/greeting.txt"`) deploys the file INTO a
# directory of that name, landing it at `bin/assets/greeting.txt/greeting.txt`
# -- a directory named after itself, holding the real file. Measured on this
# fixture before `build.mcpp`'s deploy call was corrected. This step asserts
# both directions: the file is a regular file at exactly its intended
# relative path, and no such wrapper directory exists.
[ -f "$WEB/assets/greeting.txt" ] \
    || fail "assets/greeting.txt is not a regular file at its staged path" pack.log
[ -d "$WEB/assets/greeting.txt" ] \
    && fail "assets/greeting.txt is a directory -- the file landed inside a directory named after itself"
grep -q 'hello from build.mcpp' "$WEB/assets/greeting.txt" \
    || fail "the deployed asset's content did not survive the copy" "$WEB/assets/greeting.txt"
echo "ok: assets/greeting.txt is the file, at exactly its staged relative path"

grep -q 'web-consumer.js' "$WEB/index.html" || fail "index.html does not load the launcher" "$WEB/index.html"
grep -q '<title>web-consumer</title>' "$WEB/index.html" || fail "index.html's title is not the package name" "$WEB/index.html"
echo "ok: index.html loads the launcher and titles the page from [package] name"

out=$(node "$WEB/web-consumer.js")
[ "$out" = "1-2-3" ] || fail "node did not print 1-2-3 (got: $out)"
echo "ok: node $WEB/web-consumer.js prints 1-2-3"

echo "== options::page names the page, and refuses a name that is not a bare *.html file =="
# A copy of this fixture beside it, so its path dependency on the collection
# (`../..`) still resolves; the copy's build program sets `options::page`.
PAGE_FIXTURE="../.web-consumer-page"
rm -rf "$PAGE_FIXTURE"
mkdir -p "$PAGE_FIXTURE"
cp -r mcpp.toml build.mcpp src "$PAGE_FIXTURE"/
sed -i.bak 's|    opt.target = "web-consumer";|    opt.target = "web-consumer";\n    opt.page   = "web-consumer.html";|' "$PAGE_FIXTURE/build.mcpp"
grep -q 'opt.page   = "web-consumer.html"' "$PAGE_FIXTURE/build.mcpp" \
    || fail "the named-page fixture was not written" "$PAGE_FIXTURE/build.mcpp"
( cd "$PAGE_FIXTURE" && "$MCPP" pack --format web --target wasm32-emscripten > pack.log 2>&1 ) \
    || fail "mcpp pack --format web with options::page failed" "$PAGE_FIXTURE/pack.log"
PWEB=$(find "$PAGE_FIXTURE/target" -type d -name web | head -1)
[ -f "$PWEB/web-consumer.html" ] || fail "options::page = web-consumer.html produced no such page" "$PAGE_FIXTURE/pack.log"
[ -e "$PWEB/index.html" ] && fail "options::page named another page, and index.html was written as well"
grep -q 'web-consumer.js' "$PWEB/web-consumer.html" || fail "the named page does not load the launcher" "$PWEB/web-consumer.html"
echo "ok: options::page = web-consumer.html writes that page and no index.html"

sed -i.bak 's|    opt.page   = "web-consumer.html";|    opt.page   = "pages/web-consumer.html";|' "$PAGE_FIXTURE/build.mcpp"
if ( cd "$PAGE_FIXTURE" && "$MCPP" pack --format web --target wasm32-emscripten > refuse.log 2>&1 ); then
    fail "options::page = pages/web-consumer.html was accepted" "$PAGE_FIXTURE/refuse.log"
fi
grep -q "has a directory component" "$PAGE_FIXTURE/refuse.log" \
    || fail "the refusal does not say why the page name was refused" "$PAGE_FIXTURE/refuse.log"
echo "ok: a page name with a directory component is refused, and the refusal says why"
rm -rf "$PAGE_FIXTURE"

rm -f build.log pack.log
echo "PASS: dist-web produces a static directory, node runs it, 1-2-3; options::page names the page"
