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

    # THE MECHANISM (0.17.0). On the Visual Studio row the instance mcpp
    # resolved is selected and the standard triplet is used, so the ABI hash is
    # the one vcpkg computes by itself. On the Linux GCC row (this job's
    # default) vcpkg's own detection is kept: the payload driver runs with
    # flags only mcpp's command lines carry. On the clang rows (macOS here) a
    # derived triplet names the resolved tools.
    if is_windows; then
        grep -rqs 'VCPKG_VISUAL_STUDIO_PATH=' target --include=build.ninja ||
            fail "the installation does not select the Visual Studio instance mcpp resolved"
        [ -d target/vcpkg_installed/x64-windows/x64-windows ] || fail "the instance mechanism did not use x64-windows"
        echo "ok: the installation selects the Visual Studio instance and keeps the standard triplet"
    elif is_macos; then
        ls -d target/vcpkg_installed/*-mcpp-*/*-mcpp-* > /dev/null 2>&1 ||
            fail "no derived <base>-mcpp-<hash> prefix under target/vcpkg_installed"
        grep -rqs 'MCPP_VCPKG_CXX=' target --include=build.ninja || fail "the installation does not hand vcpkg the resolved compiler"
        echo "ok: the installation names the resolved compiler in a derived triplet"
    else
        ls -d target/vcpkg_installed/*-linux/*-linux > /dev/null 2>&1 ||
            fail "the GCC row did not keep the standard <arch>-linux triplet"
        ! grep -rqs 'MCPP_VCPKG_CXX=' target --include=build.ninja || fail "the GCC row named its compiler to vcpkg"
        echo "ok: the GCC row keeps vcpkg's detection and the standard triplet"
    fi

    if is_windows; then
        # The pack collects it from the runtime search directory.
        ls target/vcpkg_installed/x64-windows/x64-windows/bin/fmt.dll > /dev/null || fail "x64-windows built no fmt.dll"
        run_directly vcpkg-consumer | tee target/ci/direct.log
        grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/direct.log ||
            fail "started from the build directory, the program did not find fmt.dll"
        "$MCPP" pack --format dir | tee target/ci/pack.log
        find target/dist -iname 'fmt.dll' | grep -q . || fail "the packed tree carries no fmt.dll"
        find target/dist -path '*/licenses/fmt/copyright' | grep -q . || fail "the packed tree carries no deployed copyright"
        echo "ok: the packed tree carries fmt.dll"
    fi
}

# LINUX UNDER A libc++ TOOLCHAIN. The host compiler vcpkg and CMake find uses
# libstdc++, whose `std::` symbols a libc++ program cannot link. The ports
# build with mcpp's clang through a derived triplet `<arch>-linux-mcpp-<hash>`
# (0.17.0; `<arch>-linux-libcxx` before); fmt's interface returns
# `std::string`, so the link itself is the criterion. The prefix is then shown
# to survive the default toolchain's own installation, which vcpkg would remove
# if the two triplets shared one.
vcpkg_libcxx() {
    local llvm="${MCPP_LLVM:-llvm@22.1.8}" gen
    cd "$ROOT/tests/vcpkg-consumer"
    rm -rf target vcpkg_installed
    mkdir -p target/ci
    "$MCPP" build --toolchain "$llvm" 2>&1 | tee target/ci/libcxx-build.log
    "$MCPP" run --toolchain "$llvm" | tee target/ci/libcxx-run.log
    grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/libcxx-run.log || fail "the libc++ program did not print through fmt"
    local lib; lib=$(find target/vcpkg_installed -path "*-mcpp-*/lib/libfmt.a" | head -1)
    [ -n "$lib" ] || fail "no derived <arch>-linux-mcpp-<hash> prefix with libfmt.a"
    gen=$(basename "$(dirname "$(dirname "$lib")")")
    find target -path '*deps-vcpkg/triplets/*' -name "$gen.cmake" -exec grep -l 'clang' {} + | grep -q . ||
        fail "the derived triplet $gen does not record the clang toolset"
    grep -q 'std::__1::' <(nm -C "$lib") || fail "$lib is not built against libc++"
    echo "ok: under $llvm the ports build with mcpp's clang and the program links them"

    "$MCPP" build 2>&1 | tee target/ci/default-build.log
    "$MCPP" run | tee target/ci/default-run.log
    grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/default-run.log || fail "the default toolchain's program did not print through fmt"
    [ -f "$lib" ] || fail "the default toolchain's installation removed the $gen prefix"
    [ "$(ls -d target/vcpkg_installed/*/ | wc -l)" -ge 2 ] ||
        fail "the default toolchain did not install a prefix of its own"
    local stamp; stamp=$(find target -path '*deps-vcpkg*' -name "$gen.stamp" | head -1)
    [ -n "$stamp" ] || fail "no $gen installation stamp"
    touch -r "$stamp" target/ci/before-switch-back
    sleep 1
    "$MCPP" build --toolchain "$llvm" --profile dev > target/ci/switch-back.log 2>&1 ||
        { cat target/ci/switch-back.log; fail "the build switched back to $llvm failed"; }
    [ -z "$(find "$stamp" -newer target/ci/before-switch-back)" ] ||
        fail "switching back to $llvm re-ran its installation"
    echo "ok: the two toolchains' prefixes coexist, and switching back installs nothing"

    # deps-cmake takes the same compilers. The criterion reads CMake's own
    # cache, so the installation is built, not taken from deps-cmake's.
    cd "$ROOT/tests/cmake-consumer"
    rm -rf target
    mkdir -p target/ci
    MCPP_DEPS_CMAKE_CACHE=off "$MCPP" build --toolchain "$llvm" 2>&1 | tee target/ci/libcxx-build.log
    "$MCPP" run --toolchain "$llvm" | grep -q '^cmake-consumer: greet says 42$' || fail "the libc++ cmake-consumer did not run"
    grep -rqs 'CMAKE_CXX_COMPILER:[A-Z]*=.*xim-x-llvm.*/clang++' target --include=CMakeCache.txt ||
        fail "deps-cmake configured the subproject without mcpp's clang"
    echo "ok: deps-cmake configures the subproject with mcpp's clang under $llvm"
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
    # The two installations may run at once; vcpkg's own lock on the root makes
    # the second wait only when asked to, and fails it otherwise ("failed to
    # take lock", measured on the validation project under 0.15.0).
    [ "$(grep -rl -- '--x-wait-for-lock' --include=build.ninja . | wc -l)" -ge 2 ] ||
        fail "a member's installation does not wait for the root's lock"
    echo "ok: each member's installation waits for another one of the same root"
}

cmake_consumer() {
    cd "$ROOT/tests/cmake-consumer"
    rm -rf target
    mkdir -p target/ci
    "$MCPP" build 2>&1 | tee target/ci/build.log
    "$MCPP" run | tee target/ci/run.log
    grep -q '^cmake-consumer: greet says 42$' target/ci/run.log || fail "the program did not call the subproject's library"
    assert_not_rerun "$(stamp_of deps-cmake)"
    # The builds after an edit are planned as well.
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

# AN INSTALLATION IS BUILT ONCE (0.18.0). deps-cmake keeps what a subproject
# installed under a key that holds no path of the machine, and copies it into
# the prefix of any later build with the same key. Only a toolset the engine
# identifies is keyed; on Linux the default GCC row lets CMake find its own
# compiler, so the criteria run under mcpp's clang there.
cmake_cache() {
    # `${tc[@]+…}`: bash 3.2 (macOS) calls an empty array unbound under `set -u`.
    local tc=()
    if [ -n "${MSVC_MANAGED:-}" ]; then tc=(--toolchain "$MSVC_MANAGED")
    elif ! is_windows && ! is_macos; then tc=(--toolchain "${MCPP_LLVM:-llvm@22.1.8}"); fi
    local cache="$ROOT/target/deps-cmake-cache" root="$ROOT"
    rm -rf "$cache"
    mkdir -p "$cache"
    # cmake is a native program on Windows: it reads the host's path syntax.
    if command -v cygpath > /dev/null; then cache=$(cygpath -m "$cache"); root=$(cygpath -m "$ROOT"); fi
    export MCPP_DEPS_CMAKE_CACHE="$cache"
    entries() { find "$MCPP_DEPS_CMAKE_CACHE" -mindepth 2 -maxdepth 2 -type d | wc -l | tr -d ' '; }
    configured() { find target -name CMakeCache.txt | grep -q .; }
    build() {
        "$MCPP" build ${tc[@]+"${tc[@]}"} > "target/ci/$1.log" 2>&1 || { cat "target/ci/$1.log"; fail "the build '$1' failed"; }
    }
    fresh() { rm -rf target; mkdir -p target/ci; }

    cd "$ROOT/tests/cmake-consumer"
    fresh
    build first
    [ "$(entries)" = 1 ] || fail "the first build kept $(entries) installations, not 1"
    configured || fail "the first build did not configure the subproject"
    echo "ok: the first build configured the subproject and kept its installation"

    fresh
    build taken
    configured && fail "a kept installation was configured again"
    "$MCPP" run ${tc[@]+"${tc[@]}"} | grep -q '^cmake-consumer: greet says 42$' || fail "the program did not run on a kept installation"
    [ "$(entries)" = 1 ] || fail "taking an installation kept another one"
    echo "ok: a build without target/ took the kept installation and did not configure"

    # Another checkout of the same subproject, at another path, has the same key.
    local moved="$ROOT/target/moved/cmake-consumer"
    rm -rf "$ROOT/target/moved"
    mkdir -p "$moved"
    cp -r build.mcpp greet src "$moved/"
    sed "s|path = \"../..\"|path = \"$root\"|" mcpp.toml > "$moved/mcpp.toml"
    (cd "$moved" && fresh && build moved && ! configured) || fail "a checkout at another path did not take the kept installation"
    [ "$(entries)" = 1 ] || fail "a checkout at another path kept another installation"
    echo "ok: a checkout at another path took the kept installation"

    # An edited source is another installation.
    cp greet/greet.c "$ROOT/target/greet.c.orig"
    trap 'cp "$ROOT/target/greet.c.orig" "$ROOT/tests/cmake-consumer/greet/greet.c"' RETURN
    echo '/* edited */' >> greet/greet.c
    build edited
    configured || fail "an edited subproject was not configured"
    [ "$(entries)" = 2 ] || fail "an edited subproject did not keep a second installation"
    cp "$ROOT/target/greet.c.orig" greet/greet.c
    trap - RETURN
    echo "ok: an edited source built and kept a second installation"

    # So is one CMake compiles with other flags from the environment.
    fresh
    CFLAGS=-DMCPP_DEPS_CMAKE_KEY_CASE build flags
    configured || fail "other CFLAGS did not configure the subproject"
    [ "$(entries)" = 3 ] || fail "other CFLAGS did not keep a third installation"
    echo "ok: the environment CMake reads its flags from is part of the key"

    fresh
    MCPP_DEPS_CMAKE_CACHE=off build off
    configured || fail "with the cache off the subproject was not configured"
    [ "$(entries)" = 3 ] || fail "with the cache off an installation was kept"
    echo "ok: MCPP_DEPS_CMAKE_CACHE=off builds and keeps nothing"

    cd "$ROOT/tests/cmake-not-relocatable"
    fresh
    build pinned
    "$MCPP" run ${tc[@]+"${tc[@]}"} | grep -q '^cmake-not-relocatable: pinned says 7$' || fail "the not-relocatable program did not run"
    [ -z "$(find "$MCPP_DEPS_CMAKE_CACHE" -maxdepth 1 -name pinned -type d)" ] ||
        [ -z "$(find "$MCPP_DEPS_CMAKE_CACHE/pinned" -mindepth 1)" ] ||
        fail "an installation whose files name its own prefix was kept"
    echo "ok: an installation that names its own prefix was built and not kept"
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
        # The VC++ runtime Qt's DLLs import travels with them, so the program
        # does not depend on the target machine's VC++ Redistributable.
        for dll in msvcp140.dll vcruntime140.dll vcruntime140_1.dll; do
            find target -path '*/bin/*' -iname "$dll" | grep -q . || fail "$dll was not placed beside the program"
            find target/dist -iname "$dll" | grep -q . || fail "the packed tree carries no $dll"
        done
        echo "ok: the VC++ runtime is beside the program and in the packed tree"
    fi
    if ! is_windows && ! is_macos; then
        # QtGui's runtime closure comes from the payload: the program runs
        # under the ecosystem's loader, which reads no host library directory.
        "$MCPP" pack --format dir | tee target/ci/pack.log
        for so in libQt6Widgets.so.6 libdbus-1.so.3 libxkbcommon.so.0 libfontconfig.so.1; do
            find target/dist -name "$so*" | grep -q . || fail "the packed tree carries no $so"
        done
        echo "ok: the packed tree carries Qt and QtGui's runtime closure"
    fi
}

# The SDK at each level `rules-qt` consults, read back from the fact the rule
# records (`rules-qt.sdk=<level>: <root>`, in the build program's cache).
qt_sdk_consumer() {
    cd "$ROOT/tests/qt-sdk-consumer"
    rm -rf target
    mkdir -p target/ci
    sdk_fact() { grep -h -o 'rules-qt\.sdk=[^"]*' target/.build-mcpp/build.mcpp.cache | tail -1; }
    "$MCPP" build 2>&1 | tee target/ci/build.log
    "$MCPP" run | tee target/ci/run.log
    grep -qE '^qt-sdk-consumer: Qt 6\.' target/ci/run.log || fail "the program did not load QtCore"
    local fact root; fact=$(sdk_fact)
    case "$fact" in "rules-qt.sdk=xlings: "*) ;; *) fail "the project's payload was not the SDK: $fact" ;; esac
    root=${fact#rules-qt.sdk=xlings: }
    echo "ok: the SDK is the payload the project declares ($root)"

    # The payload's root, named for one machine, then by the build program.
    QT_ROOT_DIR="$root" "$MCPP" build > target/ci/env-build.log 2>&1 || { cat target/ci/env-build.log; fail "the build under QT_ROOT_DIR failed"; }
    fact=$(sdk_fact)
    [ "$fact" = "rules-qt.sdk=QT_ROOT_DIR: $root" ] || fail "QT_ROOT_DIR was not the SDK: $fact"
    echo "ok: QT_ROOT_DIR names the SDK, and a change re-plans the build"
    QT_SDK_CONSUMER_ROOT="$root" QT_ROOT_DIR=/nonexistent "$MCPP" build > target/ci/options-build.log 2>&1 ||
        { cat target/ci/options-build.log; fail "the build under options::root failed"; }
    fact=$(sdk_fact)
    [ "$fact" = "rules-qt.sdk=options: $root" ] || fail "options::root was not the SDK: $fact"
    echo "ok: options::root names the SDK ahead of QT_ROOT_DIR"
}

qt_import_only() {
    cd "$ROOT/tests/qt-import-only"
    rm -rf target
    mkdir -p target/ci
    "$MCPP" build 2>&1 | tee target/ci/build.log
    # A build program that ran was compiled first, into the package's
    # target/: the program file is the evidence. The line mcpp prints for it
    # is not (mcpp 2026.9.29.5 replaced `build.mcpp running <package>` with one
    # line per program, and a search for the old words would pass either way).
    ! find target -name 'build.mcpp.bin' -o -name 'build.mcpp.exe' | grep -q . ||
        fail "mcpp synthesised a build program for a package with no Qt source (mcpp#715)"
    ! grep -q 'no Qt SDK' target/ci/build.log ||
        fail "a package with no Qt source reported a missing SDK"
    echo "ok: a package that enables rules-qt only for its module runs no build program and reports nothing"
}

# THE TEST KIT (0.17.0). The deps members' decisions -- which mechanism, which
# triplet, which C runtime -- tested against stated build contexts on every
# host, with nothing installed. The build program is the test; the verdicts
# are counted against the cases the fixture declares.
plugin_logic() {
    cd "$ROOT/tests/plugin-logic"
    rm -rf target
    mkdir -p target/ci
    # On the row without Visual Studio the build program itself needs the
    # managed toolset, which that job names in MSVC_MANAGED.
    "$MCPP" build ${MSVC_MANAGED:+--toolchain "$MSVC_MANAGED"} > target/ci/build.log 2>&1 ||
        { cat target/ci/build.log; fail "a plugin-logic case failed"; }
    local results declared passed
    results=$(find target -path '*plugins-testing/results.txt' | head -1)
    [ -n "$results" ] || fail "the kit wrote no results.txt"
    declared=$(grep -cE '^        \{ "' build.mcpp)
    passed=$(grep -c '^PASS ' "$results")
    cat "$results"
    [ "$declared" -gt 0 ] && [ "$passed" -eq "$declared" ] ||
        fail "$passed of $declared declared cases passed"
    echo "ok: $passed of $declared plugin-logic cases passed"
}

# WINDOWS, THE PROGRAM'S C RUNTIME (0.17.0). Run on the Visual Studio row. A
# self-contained program links the C runtime statically, so its ports must be
# built against the same one: the default triplet becomes x64-windows-static,
# and a project triplet that says otherwise is refused naming both statements
# (a static library of the other runtime fails the link with /failifmismatch;
# a DLL of it puts a second C++ runtime into the process without a word).
vcpkg_crt() {
    is_windows || { echo "skip: the C runtime linkage is an MSVC-ABI question"; return 0; }
    local d="$ROOT/tests/.ci-vcpkg-crt"
    rm -rf "$d"
    cp -r "$ROOT/tests/vcpkg-consumer" "$d"
    trap 'rm -rf "$ROOT/tests/.ci-vcpkg-crt"' RETURN
    cd "$d"
    rm -rf target vcpkg_installed
    printf '\n[build]\ncxx_runtime = "self-contained"\n' >> mcpp.toml
    mkdir -p target/ci
    "$MCPP" build 2>&1 | tee target/ci/static-build.log
    "$MCPP" run | tee target/ci/static-run.log
    grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/static-run.log || fail "the self-contained program did not run"
    [ -d target/vcpkg_installed/x64-windows-static/x64-windows-static ] ||
        fail "a self-contained program's ports were not installed with x64-windows-static"
    ! find target -path '*/bin/*' -iname 'fmt.dll' | grep -q . || fail "a self-contained program received fmt.dll"
    echo "ok: a self-contained program's ports link the C runtime statically, and the link agrees"

    sed -i 's|^    o.libraries = { "fmt" };|    o.libraries = { "fmt" };\n    o.triplet = "x64-windows";|' build.mcpp
    grep -q 'o.triplet = "x64-windows";' build.mcpp || fail "the fixture edit did not apply"
    if "$MCPP" build > target/ci/contradiction.log 2>&1; then
        cat target/ci/contradiction.log; fail "a triplet contradicting the program's C runtime was accepted"
    fi
    grep -q "the triplet 'x64-windows' links the C runtime dynamic" target/ci/contradiction.log ||
        { cat target/ci/contradiction.log; fail "the refusal does not name the triplet's linkage"; }
    echo "ok: a triplet that contradicts the program's C runtime is refused, naming both statements"
}

# WINDOWS WITHOUT VISUAL STUDIO (0.17.0). Run on the row whose Visual Studio is
# masked, with the managed toolset `$MSVC_MANAGED` (xim:msvc@<version>). The
# toolset is named in a derived triplet: a CMake port (fmt) builds, the program
# links it, and a CMake subproject builds with Ninja and the toolset's cl.exe.
vcpkg_managed() {
    : "${MSVC_MANAGED:?MSVC_MANAGED names the managed toolset, e.g. xim:msvc@14.44.35207}"
    cd "$ROOT/tests/vcpkg-consumer"
    rm -rf target vcpkg_installed
    mkdir -p target/ci
    "$MCPP" build --toolchain "$MSVC_MANAGED" 2>&1 | tee target/ci/managed-build.log
    "$MCPP" run --toolchain "$MSVC_MANAGED" | tee target/ci/managed-run.log
    grep -qE '^vcpkg-consumer: fmt [0-9]+ says 42$' target/ci/managed-run.log || fail "the program did not print through fmt"
    ls -d target/vcpkg_installed/x64-windows-mcpp-*/x64-windows-mcpp-*/bin/fmt.dll > /dev/null 2>&1 ||
        fail "no derived x64-windows-mcpp-<hash> prefix with fmt.dll"
    grep -rqs -- '--host-triplet=x64-windows-mcpp-' target --include=build.ninja || fail "the host triplet is not the derived one"
    echo "ok: without Visual Studio, a CMake port builds with the managed toolset named in a derived triplet"

    # The criterion reads CMake's own cache: the installation is built, not
    # taken from deps-cmake's.
    cd "$ROOT/tests/cmake-consumer"
    rm -rf target
    mkdir -p target/ci
    MCPP_DEPS_CMAKE_CACHE=off "$MCPP" build --toolchain "$MSVC_MANAGED" 2>&1 | tee target/ci/managed-build.log
    "$MCPP" run --toolchain "$MSVC_MANAGED" | grep -q '^cmake-consumer: greet says 42$' || fail "the cmake-consumer did not run"
    grep -rqs 'CMAKE_GENERATOR:INTERNAL=Ninja' target --include=CMakeCache.txt || fail "the subproject was not configured with Ninja"
    grep -rqsi 'CMAKE_CXX_COMPILER:[A-Z]*=.*xim-x-msvc.*cl.exe' target --include=CMakeCache.txt ||
        fail "the subproject was not configured with the managed toolset's cl.exe"
    echo "ok: without Visual Studio, a CMake subproject builds with Ninja and the managed toolset"
}

# The same row: a port that needs MSBuild is refused by name.
vcpkg_msbuild_refused() {
    : "${MSVC_MANAGED:?MSVC_MANAGED names the managed toolset}"
    cd "$ROOT/tests/vcpkg-msbuild-port"
    rm -rf target
    mkdir -p target/ci
    if "$MCPP" build --toolchain "$MSVC_MANAGED" > target/ci/build.log 2>&1; then
        cat target/ci/build.log; fail "an MSBuild port built without Visual Studio"
    fi
    grep -q "builds with MSBuild" target/ci/build.log ||
        { tail -60 target/ci/build.log; fail "the MSBuild port failed without the plugin's reason"; }
    echo "ok: an MSBuild port is refused by name without Visual Studio"
}

# The same row: a port that configures with make under msys (icu) finds
# link.exe on the PATH vcpkg keeps.
vcpkg_make_port() {
    : "${MSVC_MANAGED:?MSVC_MANAGED names the managed toolset}"
    cd "$ROOT/tests/vcpkg-make-port"
    rm -rf target
    mkdir -p target/ci
    if ! "$MCPP" build --toolchain "$MSVC_MANAGED" > target/ci/build.log 2>&1; then
        tail -80 target/ci/build.log
        # autoconf records the invocation and the failing test in config.log,
        # which vcpkg's own report does not show.
        local cfg
        cfg=$(find "$(cygpath -u "$LOCALAPPDATA" 2>/dev/null || echo "$HOME")/vcpkg/mcpp" -path '*icu*' -name config.log 2>/dev/null | head -1)
        [ -n "$cfg" ] && { echo "--- $cfg"; head -40 "$cfg"; echo "..."; grep -n "invalid variable\|error" "$cfg" | head -20; }
        fail "the make-based port did not build with the managed toolset"
    fi
    "$MCPP" run --toolchain "$MSVC_MANAGED" | tee target/ci/run.log
    grep -qE '^vcpkg-make-port: icu [0-9]' target/ci/run.log || fail "the program did not read icu's version"
    echo "ok: a make-based port builds with the managed toolset first on the kept PATH"
}

# 0.19.0 (mcpp#755): a tool the build program names is used, and the payload the
# member declares `provision = "on-request"` is NOT asked for.
#
# THE CRITERION IS THE ENGINE'S OWN RECORD, NOT THE STORE. A runner may already
# hold `xim:cmake` -- every other case here installs it -- so "nothing was
# downloaded" cannot be read from the store, and a clean `MCPP_HOME` would cost
# a download per case. `resolution.json` states what each payload's source was
# and whether this build asked for it, which is the decision itself. The pair is
# the criterion: the same project asks for the payload when nothing names a
# tool, and does not when the build program names one.
tool_sources() {
    cd "$ROOT/tests/cmake-consumer"
    local cmake; cmake=$(command -v cmake || true)
    [ -n "$cmake" ] || { echo "SKIP: no cmake on this host to name"; return 0; }

    # `considered` of the payload entry, and the class of each subject.
    record() {
        python3 - <<'PYEOF'
import glob, json, sys
paths = glob.glob("target/*/*/resolution.json")
if not paths:
    print("NO-RECORD"); raise SystemExit(0)
doc = json.load(open(sorted(paths)[0]))
for d in doc.get("sources", []):
    print(d["subject"], d["class"], "|", "; ".join(d.get("considered", [])), sep="\t")
PYEOF
    }

    cp build.mcpp build.mcpp.bak
    restore() { mv -f build.mcpp.bak build.mcpp 2>/dev/null || true; }
    trap restore EXIT

    # The control: nothing names a tool, so the member asks for the payload.
    rm -rf target
    NO_COLOR=1 "$MCPP" build >/dev/null 2>&1 || fail "the control build failed"
    record | grep -q "payload:xim:cmake.*installed on request" \
        || fail "the control did not ask for xim:cmake: $(record)"

    # The build program names the host's cmake, through the member's option.
    python3 - "$cmake" <<'PYEOF'
import pathlib, sys
p = pathlib.Path("build.mcpp")
t = p.read_text()
marker = "    o.shared    = true;"
assert marker in t, t
p.write_text(t.replace(marker, marker + '\n    o.cmake     = "%s";' % sys.argv[1].replace("\\", "/"), 1))
PYEOF
    rm -rf target
    local out
    out=$(MCPP_NO_AUTO_INSTALL=1 NO_COLOR=1 "$MCPP" build 2>&1) \
        || fail "a named cmake still needed the payload: $out"
    case "$out" in
        *"Using cmake (mcpp.deps.cmake)"*"[program · build.mcpp:"*) ;;
        *) fail "the build did not report the tool's source: $out" ;;
    esac
    case "$out" in
        *"program: cmake (mcpp.deps.cmake)"*) ;;
        *) fail "the Finished line did not summarise the source: $out" ;;
    esac
    record | grep -q "payload:xim:cmake.*not requested by this build" \
        || fail "the payload was asked for although the program named a cmake: $(record)"
    MCPP_NO_AUTO_INSTALL=1 "$MCPP" run | grep -q '^cmake-consumer: greet says 42$' \
        || fail "the program built with the named cmake does not run"

    # The same statement as an engine override, with the payload untouched.
    restore
    rm -rf target
    out=$(MCPP_XLINGS_OVERRIDE_XIM_CMAKE="$cmake" MCPP_NO_AUTO_INSTALL=1 NO_COLOR=1 "$MCPP" build 2>&1) \
        || fail "an override still needed the payload: $out"
    case "$out" in
        *"Using xim:cmake"*"[custom · env MCPP_XLINGS_OVERRIDE_XIM_CMAKE]"*) ;;
        *) fail "the build did not report the override: $out" ;;
    esac
    record | grep -q "payload:xim:cmake.*custom.*overridden" \
        || fail "the record does not state the override: $(record)"

    # `--managed-only` refuses the same build, naming the payload.
    rm -rf target
    out=$(MCPP_XLINGS_OVERRIDE_XIM_CMAKE="$cmake" MCPP_NO_AUTO_INSTALL=1 NO_COLOR=1 \
          "$MCPP" build --managed-only 2>&1 || true)
    case "$out" in
        *managed-only*xim:cmake*) ;;
        *) fail "--managed-only did not refuse the override: $out" ;;
    esac
    trap - EXIT
    rm -rf target
    echo "OK: a named tool is used, the payload is not asked for, and both are reported"
}

case "${1:-}" in
    tool-sources)        tool_sources ;;
    vcpkg-consumer)      vcpkg_consumer ;;
    vcpkg-libcxx)        vcpkg_libcxx ;;
    archive-consumer)    archive_consumer ;;
    vcpkg-workspace)     vcpkg_workspace ;;
    cmake-consumer)      cmake_consumer ;;
    cmake-cache)         cmake_cache ;;
    qt-consumer)         qt_consumer ;;
    qt-widgets-consumer) qt_widgets_consumer ;;
    qt-sdk-consumer)     qt_sdk_consumer ;;
    qt-import-only)      qt_import_only ;;
    plugin-logic)        plugin_logic ;;
    vcpkg-crt)           vcpkg_crt ;;
    vcpkg-managed)       vcpkg_managed ;;
    vcpkg-msbuild-refused) vcpkg_msbuild_refused ;;
    vcpkg-make-port)     vcpkg_make_port ;;
    *) echo "usage: $0 vcpkg-consumer|vcpkg-libcxx|archive-consumer|vcpkg-workspace|cmake-consumer|cmake-cache|qt-consumer|qt-widgets-consumer|qt-sdk-consumer|qt-import-only|plugin-logic|tool-sources|vcpkg-crt|vcpkg-managed|vcpkg-msbuild-refused|vcpkg-make-port"; exit 2 ;;
esac
