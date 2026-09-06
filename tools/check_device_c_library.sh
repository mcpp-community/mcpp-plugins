#!/usr/bin/env bash
# Every device compile must find the ecosystem's C library before any host one.
#
# THE OBJECT IS THE COMPILER'S OWN SEARCH LIST, NOT THE BUILD LOG. An implicit
# include search never appears on a command line, and a build log also carries
# the ENGINE's compile lines -- which do name the ecosystem glibc. Grepping the
# log therefore passes for a rule whose device compile names nothing, because
# the flag it found belonged to a different compile. Measured: the HIP rule's
# device command carries neither `--sysroot` nor `-isystem <glibc>`, and is
# nonetheless correct, because the compiler it drives is configured with the
# ecosystem glibc. Only the search list can tell those two apart from the case
# that is wrong.
#
# TWO ADMISSIBLE ANSWERS, and both are the same property seen from either side:
# the rule names the C library, or the compiler it drives already knows it.
# What is refused is a device compile that reaches a host path with no
# ecosystem path above it.
set -u
MCPP="${MCPP:-mcpp}"
fails=0

for d in "$@"; do
    printf -- '--- %s ---\n' "$d"

    # BUILD IT HERE, INTO AN EMPTY TARGET DIRECTORY.
    #
    # Inspecting whatever a previous step left is a guess about that step. The
    # fixture steps in CI end with `build --no-accel`, whose build.ninja has no
    # device action at all, and taking the first directory that sorts found
    # exactly that -- "declares no action to inspect", three times, for three
    # fixtures that were correct. The accel build is what this check is about,
    # so it runs it.
    #
    # SELECTING BY CONTENT WAS NOT ENOUGH EITHER, AND THAT IS WHY `target/` IS
    # REMOVED FIRST. A build directory is named by a fingerprint, so a tree that
    # has been built more than once holds one per configuration AND one per
    # engine or payload version it was built with. Selecting the first that
    # declares an action therefore answered from a directory this run did not
    # write: measured 2026-09-07 on the SYCL example, where the graph the check
    # read had been produced the previous day by mcpp.plugins 0.2.0 -- before
    # the fix this check exists to guard -- while the graph the same command had
    # just written carried both `-isystem` flags. The check reported a defect in
    # a build that was correct, and would have reported success for a broken one
    # just as readily.
    #
    # The object of a check has to be produced by the check. Removing `target/`
    # costs a full rebuild of one fixture and buys the guarantee that exactly
    # one graph exists to read.
    printf '  removing %s/target so the graph read is the graph this run wrote\n' "$d"
    rm -rf "$d/target"
    ( cd "$d" && "$MCPP" build >/dev/null 2>&1 ) || {
        echo "ASSERT-FAIL: $d does not build with its accelerator on"
        fails=$((fails + 1)); continue; }

    ninja=""
    for candidate in "$d"/target/*/*/build.ninja; do
        [ -f "$candidate" ] || continue
        if grep -q '^rule mcpp_action_' "$candidate"; then ninja="$candidate"; break; fi
    done
    if [ -z "$ninja" ]; then
        echo "ASSERT-FAIL: no build.ninja under $d/target declares a device action"
        fails=$((fails + 1)); continue
    fi
    printf '  reading %s\n' "$ninja"

    # The device compile is the first declared action's command. Its C-library
    # inputs are the flags below; everything else (`-c`, `-o`, the source) is
    # dropped so the compiler can be asked to print its search list instead of
    # compiling.
    cmd=$(grep -A 2 '^rule mcpp_action_0' "$ninja" | grep -m1 'command = ' | sed 's/^ *command = //')
    if [ -z "$cmd" ]; then
        echo "ASSERT-FAIL: $d declares no action to inspect"
        fails=$((fails + 1)); continue
    fi
    exe=$(printf '%s' "$cmd" | awk '{print $1}')

    # ONLY THE FLAGS THAT MOVE THE C LIBRARY, and the probe asks in plain C++.
    #
    # Carrying the rule's language machinery over was a mistake: the CUDA and
    # HIP commands start with `-x cuda`, the probe added its own `-x c++`, and
    # a compiler given two of them prints no search list at all -- which the
    # check then reported as a defect in three fixtures, two of which were
    # correct. Where `features.h` comes from does not depend on the language
    # being compiled, so the probe does not name one.
    keep=$(printf '%s\n' $cmd |
           grep -E '^(--sysroot=|--gcc-install-dir=|-isystem)' | tr '\n' ' ')

    probe_err="$(mktemp)"
    list=$("$exe" $keep -x c++ -E -v /dev/null 2>"$probe_err" |
           sed -n '/#include <...> search starts here/,/End of search list/p')
    if [ -z "$list" ]; then
        # `-v` writes the search list to stderr, so read it there too before
        # concluding the compiler said nothing.
        list=$(sed -n '/#include <...> search starts here/,/End of search list/p' "$probe_err")
    fi
    if [ -z "$list" ]; then
        echo "ASSERT-FAIL: $exe printed no search list; it said:"
        head -5 "$probe_err" | sed 's/^/    /'
        rm -f "$probe_err"
        fails=$((fails + 1)); continue
    fi
    rm -f "$probe_err"

    # THE C LIBRARY SPECIFICALLY, not "any ecosystem path". The compiler's own
    # resource directory lives in the store too, so a looser pattern matched
    # dpcpp's `include/` and reported success for a search list whose only C
    # library was the host's -- verified by removing the fix and watching this
    # check stay green. What provides `features.h` is the glibc payload, or a
    # sysroot's `usr/include` beneath the registry.
    eco=$(printf '%s\n' "$list" |
          grep -nE 'xim-x-glibc[^ ]*/include|registry/[^ ]*/usr/include' |
          head -1 | cut -d: -f1)
    host=$(printf '%s\n' "$list" | grep -nE '^ */(usr|opt)/' | head -1 | cut -d: -f1)
    if [ -z "$eco" ]; then
        echo "ASSERT-FAIL: no ecosystem C library on the device compiler's search list"
        printf '%s\n' "$list" | head -12
        fails=$((fails + 1))
    elif [ -n "$host" ] && [ "$host" -lt "$eco" ]; then
        echo "ASSERT-FAIL: a host path precedes every ecosystem path (host at $host, ecosystem at $eco)"
        printf '%s\n' "$list" | head -12
        fails=$((fails + 1))
    else
        echo "ok: ecosystem at position $eco, first host path at ${host:-none}"
    fi
done

if [ "$fails" -ne 0 ]; then
    printf 'FAIL: %s device compile(s) reach a host C library first\n' "$fails"
    exit 1
fi
printf 'PASS: every device compile finds the ecosystem C library first\n'
