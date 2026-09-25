#!/usr/bin/env bash
# The criteria for `deps-vcpkg`, `deps-cmake` and `rules-qt`, one function per
# fixture, the same on every host. A CI step names the fixture:
#
#   bash .github/scripts/check-deps-and-qt.sh vcpkg-consumer
#
# Each function fails with the reason it failed. Host differences are the
# host's: a DLL is looked for on Windows, where a triplet builds fmt as one, and
# nowhere else.
set -euo pipefail

: "${MCPP:?MCPP names the mcpp under test}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

fail() { echo "FAIL: $*"; exit 1; }

is_windows() { case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) return 0 ;; *) return 1 ;; esac; }

# The stamp an installation action leaves: the file mcpp writes when a `check`
# action's command succeeds. Its modification time is the criterion for "the
# installation did not run again".
stamp_of() { find target -path "*$1*" -name '*.stamp' | head -1; }

# A second build with nothing changed must not run the installation again.
assert_not_rerun() {
    local stamp="$1"
    [ -n "$stamp" ] && [ -f "$stamp" ] || fail "no installation stamp under target/"
    mkdir -p target/ci
    touch -r "$stamp" target/ci/before-second-build
    sleep 1
    "$MCPP" build > target/ci/second-build.log 2>&1 || { cat target/ci/second-build.log; fail "the second build failed"; }
    [ -z "$(find "$stamp" -newer target/ci/before-second-build)" ] ||
        fail "the second build re-ran the installation ($stamp is newer)"
    echo "ok: a second build with nothing changed did not re-run the installation"
}

vcpkg_consumer() {
    cd "$ROOT/tests/vcpkg-consumer"
    rm -rf target vcpkg_installed
    # PLANNING NEVER INSTALLS, AND STILL STATES THE PATHS. An editor asks for
    # the build database on machines that never built; the build program must
    # succeed there and name the include directory the build will fill.
    mkdir -p target/ci
    "$MCPP" emit build-database --format json > target/ci/db.json 2> target/ci/emit.log ||
        { cat target/ci/emit.log; fail "emit build-database failed before any installation"; }
    grep -q 'vcpkg_installed' target/ci/db.json || fail "the database names no vcpkg_installed include directory"
    [ ! -d vcpkg_installed ] || fail "emit build-database installed something"
    echo "ok: emit succeeded before the installation and named its include directory"

    "$MCPP" build 2>&1 | tee build.log
    "$MCPP" run | tee run.log
    grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' run.log || fail "the program did not print through fmt"
    assert_not_rerun "$(stamp_of deps-vcpkg)"

    if is_windows; then
        # The DLL reached the run through the runtime library directory; the
        # pack must carry it, which only the same directory can make it do.
        ls vcpkg_installed/x64-windows/bin/fmt.dll > /dev/null || fail "x64-windows built no fmt.dll"
        "$MCPP" pack --format dir | tee pack.log
        find target/dist -iname 'fmt.dll' | grep -q . || fail "the packed tree carries no fmt.dll"
        echo "ok: the packed tree carries fmt.dll"
    fi
}

vcpkg_workspace() {
    cd "$ROOT/tests/vcpkg-workspace"
    rm -rf target app-a/target app-b/target vcpkg_installed
    "$MCPP" build 2>&1 | tee build.log
    "$MCPP" run -p app-a | tee run-a.log
    "$MCPP" run -p app-b | tee run-b.log
    grep -qE '^app-a: fmt [0-9]+$' run-a.log || fail "app-a did not run"
    grep -qE '^app-b: fmt [0-9]+$' run-b.log || fail "app-b did not run"
    # Each member builds into its own target/ and declares its own installation.
    [ "$(find . -path '*/target/*' -path '*deps-vcpkg*' -name '*.stamp' | wc -l)" -ge 2 ] ||
        fail "each member did not declare its own installation"
    echo "ok: two members that share no dependency both installed and linked one prefix"
}

cmake_consumer() {
    cd "$ROOT/tests/cmake-consumer"
    rm -rf target
    "$MCPP" build 2>&1 | tee build.log
    "$MCPP" run | tee run.log
    grep -q '^cmake-consumer: greet says 42$' run.log || fail "the program did not call the subproject's library"
    assert_not_rerun "$(stamp_of deps-cmake)"
    touch greet/greet.c
    "$MCPP" build > target/ci/third-build.log 2>&1 || { cat target/ci/third-build.log; fail "the rebuild failed"; }
    [ -n "$(find "$(stamp_of deps-cmake)" -newer target/ci/before-second-build)" ] ||
        fail "an edited subproject source did not rebuild the subproject"
    echo "ok: an edited subproject source rebuilt the subproject"
    # And not again: the stamp moved past the edited file (mcpp 2026.9.27.1).
    assert_not_rerun "$(stamp_of deps-cmake)"
}

qt_consumer() {
    cd "$ROOT/tests/qt-consumer"
    rm -rf target
    "$MCPP" build 2>&1 | tee build.log
    "$MCPP" run | tee run.log
    grep -qE "^qt-consumer: signal 42, resource 'greetings from rcc', translation 'hallo', Qt 6\." run.log ||
        fail "moc, rcc or lrelease did not reach the program"
    find target -name 'qt_consumer_de.qm' | grep -q . || fail "no .qm was produced"
    echo "ok: moc (header and inline), rcc and lrelease reached the program"
}

qt_widgets_consumer() {
    cd "$ROOT/tests/qt-widgets-consumer"
    rm -rf target
    "$MCPP" build 2>&1 | tee build.log
    QT_QPA_PLATFORM=offscreen "$MCPP" run | tee run.log
    grep -q "^qt-widgets-consumer: platform offscreen, label 'made by uic'$" run.log ||
        fail "the platform plugin or the uic form did not reach the program"
    if is_windows; then
        "$MCPP" pack --format dir | tee pack.log
        find target/dist -iname 'Qt6Widgets.dll' | grep -q . || fail "the packed tree carries no Qt6Widgets.dll"
        find target/dist -ipath '*platforms/qoffscreen.dll' | grep -q . ||
            fail "the packed tree carries no platforms/qoffscreen.dll"
        echo "ok: the packed tree carries the Qt modules and the platform plugins"
    fi
}

case "${1:-}" in
    vcpkg-consumer)      vcpkg_consumer ;;
    vcpkg-workspace)     vcpkg_workspace ;;
    cmake-consumer)      cmake_consumer ;;
    qt-consumer)         qt_consumer ;;
    qt-widgets-consumer) qt_widgets_consumer ;;
    *) echo "usage: $0 vcpkg-consumer|vcpkg-workspace|cmake-consumer|qt-consumer|qt-widgets-consumer"; exit 2 ;;
esac
