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
is_macos() { [ "$(uname -s)" = Darwin ]; }

# The stamp an installation action leaves: the file mcpp writes when a `prepare`
# action's command succeeds. Its modification time is the criterion for "the
# installation did not run again".
stamp_of() { find target -path "*$1*" -name '*.stamp' | head -1; }

# The program as the build left it, started without `mcpp run`: on Windows the
# engine has placed the DLLs it imports beside it (SPEC-007 R4.3), so it starts
# from the build directory as it does from the packed tree.
run_directly() {
    local exe; exe=$(find target -path '*/bin/*' -name "$1.exe" | head -1)
    [ -n "$exe" ] || fail "no $1.exe under target/"
    "$exe"
}

# A second build with nothing changed must not run the installation again.
assert_not_rerun() {
    local stamp="$1"
    [ -n "$stamp" ] && [ -f "$stamp" ] || fail "no installation stamp under target/"
    mkdir -p target/ci
    touch -r "$stamp" target/ci/before-second-build
    sleep 1
    # `--profile dev` names the default profile and declines mcpp's fast path,
    # so the second build is planned on every host, as it is on Windows and
    # macOS without the flag, and a host tool whose key moved is rebuilt.
    "$MCPP" build --profile dev > target/ci/second-build.log 2>&1 ||
        { cat target/ci/second-build.log; fail "the second build failed"; }
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
    [ ! -d target/vcpkg_installed ] || fail "emit build-database installed something"
    echo "ok: emit succeeded before the installation and named its include directory"

    "$MCPP" build 2>&1 | tee target/ci/build.log
    "$MCPP" run | tee target/ci/run.log
    grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/run.log || fail "the program did not print through fmt"
    assert_not_rerun "$(stamp_of deps-vcpkg)"
    # A file of the prefix the build program named in options::deploy.
    find target -path '*/bin/licenses/fmt/copyright' | grep -q . ||
        fail "share/fmt/copyright was not deployed beside the program"
    echo "ok: a file of the prefix is deployed beside the program"

    if is_windows; then
        # The pack collects it from the runtime search directory.
        ls target/vcpkg_installed/x64-windows/bin/fmt.dll > /dev/null || fail "x64-windows built no fmt.dll"
        run_directly vcpkg-consumer | tee target/ci/direct.log
        grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/direct.log ||
            fail "started from the build directory, the program did not find fmt.dll"
        "$MCPP" pack --format dir | tee target/ci/pack.log
        find target/dist -iname 'fmt.dll' | grep -q . || fail "the packed tree carries no fmt.dll"
        find target/dist -path '*/licenses/fmt/copyright' | grep -q . || fail "the packed tree carries no deployed copyright"
        echo "ok: the packed tree carries fmt.dll"
    fi
}

vcpkg_workspace() {
    cd "$ROOT/tests/vcpkg-workspace"
    rm -rf target app-a/target app-b/target vcpkg_installed
    mkdir -p target/ci
    "$MCPP" build 2>&1 | tee target/ci/build.log
    "$MCPP" run -p app-a | tee target/ci/run-a.log
    "$MCPP" run -p app-b | tee target/ci/run-b.log
    grep -qE '^app-a: fmt [0-9]+$' target/ci/run-a.log || fail "app-a did not run"
    grep -qE '^app-b: fmt [0-9]+$' target/ci/run-b.log || fail "app-b did not run"
    # Each member builds into its own target/ and declares its own installation.
    [ "$(find . -path '*/target/*' -path '*deps-vcpkg*' -name '*.stamp' | wc -l)" -ge 2 ] ||
        fail "each member did not declare its own installation"
    echo "ok: two members that share no dependency both installed and linked one prefix"
}

cmake_consumer() {
    cd "$ROOT/tests/cmake-consumer"
    rm -rf target
    mkdir -p target/ci
    "$MCPP" build 2>&1 | tee target/ci/build.log
    "$MCPP" run | tee target/ci/run.log
    grep -q '^cmake-consumer: greet says 42$' target/ci/run.log || fail "the program did not call the subproject's library"
    assert_not_rerun "$(stamp_of deps-cmake)"
    # The builds after an edit are planned as well. The subproject lies inside
    # this repository, the tree mcpp stamps for the plugins' host tool
    # (mcpp#705), so an edit also rebuilds `mcpp-deps`; the build that is
    # expected to re-run the installation absorbs that rebuild.
    touch greet/greet.c
    "$MCPP" build --profile dev > target/ci/third-build.log 2>&1 || { cat target/ci/third-build.log; fail "the rebuild failed"; }
    # The installed library is the product of the rebuild, whatever the engine
    # does with the action's stamp.
    [ -n "$(find target -path '*deps-cmake*/install/*' -name '*greet*' -newer target/ci/before-second-build)" ] ||
        fail "an edited subproject source did not rebuild the subproject"
    echo "ok: an edited subproject source rebuilt the subproject"
    # And not again: the engine moves the stamp past the input that changed
    # (mcpp's SPEC-007 R3.5).
    assert_not_rerun "$(stamp_of deps-cmake)"

    # A file ADDED to the subproject is an input too: the build program watches
    # the tree, so the next plan names it and the installation runs again.
    trap 'rm -f "$ROOT/tests/cmake-consumer/greet/added.txt"' RETURN
    touch -r "$(stamp_of deps-cmake)" target/ci/before-added-file
    sleep 1
    echo added > greet/added.txt
    "$MCPP" build --profile dev > target/ci/added-build.log 2>&1 || { cat target/ci/added-build.log; fail "the build after adding a file failed"; }
    [ -n "$(find "$(stamp_of deps-cmake)" -newer target/ci/before-added-file)" ] ||
        fail "a file added to the subproject did not re-run its installation"
    echo "ok: a file added to the subproject re-ran its installation"
}

archive_consumer() {
    cd "$ROOT/tests/archive-consumer"
    rm -rf target
    mkdir -p target/ci
    # PLANNING EXTRACTS NOTHING. The listing is read from the archive's central
    # directory; the extraction is an action.
    "$MCPP" emit build-database --format json > target/ci/db.json 2> target/ci/emit.log ||
        { cat target/ci/emit.log; fail "emit build-database failed before any extraction"; }
    [ -z "$(find target -path '*deps-archive*' -type f)" ] || fail "emit build-database extracted something"
    echo "ok: emit planned the archive and extracted nothing"

    "$MCPP" build 2>&1 | tee target/ci/build.log
    "$MCPP" run | tee target/ci/run.log
    grep -q "^archive-consumer: 'greetings from the archive', 'a file two levels down'$" target/ci/run.log ||
        fail "the program did not read the archive's files from beside itself"
    # The C++ file among the data `#error`s if compiled; the build succeeded,
    # and the file is beside the program as data.
    find target -path '*/bin/runtime/bundle/not-compiled.cpp' | grep -q . ||
        fail "an archive member was not deployed beside the program"
    echo "ok: the archive's tree is beside the program, a C++ member included, as data"

    local copy
    copy=$(find target -path '*deps-archive/bundle/bundle/greeting.txt' | head -1)
    [ -n "$copy" ] || fail "no extracted copy under target/"
    touch -r "$copy" target/ci/before-second-build
    sleep 1
    "$MCPP" build --profile dev > target/ci/second-build.log 2>&1 ||
        { cat target/ci/second-build.log; fail "the second build failed"; }
    [ -z "$(find "$copy" -newer target/ci/before-second-build)" ] ||
        fail "a second build with nothing changed extracted the archive again"
    echo "ok: a second build with nothing changed did not extract the archive again"

    touch assets/bundle.zip
    "$MCPP" build --profile dev > target/ci/third-build.log 2>&1 ||
        { cat target/ci/third-build.log; fail "the build after touching the archive failed"; }
    [ -n "$(find "$copy" -newer target/ci/before-second-build)" ] ||
        fail "a changed archive was not extracted again"
    echo "ok: a changed archive was extracted again"

    "$MCPP" pack --format dir > target/ci/pack.log 2>&1 || { cat target/ci/pack.log; fail "mcpp pack failed"; }
    find target/dist -path '*/runtime/bundle/nested/deep.txt' | grep -q . ||
        fail "the packed tree does not carry the archive's files"
    # A PE program sits at the root of the packed tree, an ELF or Mach-O one
    # under bin/; the deployed files are beside it either way.
    local packed
    packed=$(find target/dist -type f \( -name archive-consumer -o -name archive-consumer.exe \) | head -1 || true)
    [ -n "$packed" ] || fail "no packed program under target/dist"
    "$packed" | grep -q "^archive-consumer: 'greetings from the archive'" ||
        fail "the packed program did not read the archive's files"
    echo "ok: the packed tree carries the archive's files, and the packed program reads them"
}

qt_consumer() {
    cd "$ROOT/tests/qt-consumer"
    rm -rf target
    mkdir -p target/ci
    "$MCPP" build -v 2>&1 | tee target/ci/build.log
    # lupdate writes the `.ts` in the package root and lrelease reads it after;
    # neither claims the package's directory as a construction output.
    local lu lr
    lu=$(grep -n '/lupdate[^ ]* ' target/ci/build.log | head -1 | cut -d: -f1)
    lr=$(grep -n '/lrelease[^ ]* ' target/ci/build.log | head -1 | cut -d: -f1)
    [ -n "$lu" ] && [ -n "$lr" ] && [ "$lu" -lt "$lr" ] || fail "lupdate did not run before lrelease"
    ! grep -q 'output_dir' target/ci/build.log || fail "the build reports a construction directory over the package's sources"
    echo "ok: lupdate updated the .ts before lrelease read it"
    "$MCPP" run | tee target/ci/run.log
    grep -qE "^qt-consumer: signal 42, resource 'greetings from rcc', translation 'hallo', Qt 6\." target/ci/run.log ||
        fail "moc, rcc or lrelease did not reach the program"
    find target -name 'qt_consumer_de.qm' | grep -q . || fail "no .qm was produced"
    echo "ok: moc (header and inline), rcc and lrelease reached the program"
    grep -q "Qt's own 'Abbrechen'$" target/ci/run.log ||
        fail "Qt's own strings were not translated from the combined qt_de.qm"
    echo "ok: lconvert combined Qt's catalogs into qt_de.qm, and the program loads it"
    "$MCPP" build --profile dev -v > target/ci/second-build.log 2>&1 ||
        { cat target/ci/second-build.log; fail "the second build failed"; }
    ! grep -qE '/(lupdate|lrelease)[^ ]* ' target/ci/second-build.log ||
        fail "a second build with nothing changed re-ran lupdate or lrelease"
    echo "ok: a second build with nothing changed ran neither lupdate nor lrelease"
}

qt_widgets_consumer() {
    cd "$ROOT/tests/qt-widgets-consumer"
    rm -rf target
    mkdir -p target/ci
    "$MCPP" build 2>&1 | tee target/ci/build.log
    QT_QPA_PLATFORM=offscreen "$MCPP" run | tee target/ci/run.log
    grep -q "^qt-widgets-consumer: platform offscreen, label 'made by uic'$" target/ci/run.log ||
        fail "the platform plugin or the uic form did not reach the program"
    if is_macos; then
        find target -path '*/bin/platforms/libqoffscreen.dylib' | grep -q . ||
            fail "no platforms/libqoffscreen.dylib was deployed beside the program"
    fi
    if is_windows; then
        QT_QPA_PLATFORM=offscreen run_directly qt-widgets-consumer | tee target/ci/direct.log
        grep -q "^qt-widgets-consumer: platform offscreen" target/ci/direct.log ||
            fail "started from the build directory, the program did not find the Qt DLLs"
        "$MCPP" pack --format dir | tee target/ci/pack.log
        find target/dist -iname 'Qt6Widgets.dll' | grep -q . || fail "the packed tree carries no Qt6Widgets.dll"
        find target/dist -ipath '*platforms/qoffscreen.dll' | grep -q . ||
            fail "the packed tree carries no platforms/qoffscreen.dll"
        echo "ok: the packed tree carries the Qt modules and the platform plugins"
    fi
}

case "${1:-}" in
    vcpkg-consumer)      vcpkg_consumer ;;
    archive-consumer)    archive_consumer ;;
    vcpkg-workspace)     vcpkg_workspace ;;
    cmake-consumer)      cmake_consumer ;;
    qt-consumer)         qt_consumer ;;
    qt-widgets-consumer) qt_widgets_consumer ;;
    *) echo "usage: $0 vcpkg-consumer|archive-consumer|vcpkg-workspace|cmake-consumer|qt-consumer|qt-widgets-consumer"; exit 2 ;;
esac
