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
    ninja=$(ls "$d"/target/*/*/build.ninja 2>/dev/null | head -1)
    if [ -z "$ninja" ]; then
        echo "ASSERT-FAIL: no build.ninja under $d/target (build it first)"
        fails=$((fails + 1)); continue
    fi

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
    keep=$(printf '%s\n' $cmd | grep -E '^(--sysroot=|-isystem|--gcc-install-dir=|-x|cuda|c\+\+|--cuda-path=|-fsycl)$|^-isystem' | tr '\n' ' ')
    # `-x <lang>` arrives as two tokens; keep whichever language the rule chose.
    lang=$(printf '%s\n' $cmd | grep -A 1 -x -- '-x' | tail -1)
    [ -n "$lang" ] || lang=c++

    list=$("$exe" $keep -x "$lang" -E -v /dev/null 2>&1 |
           sed -n '/#include <...> search starts here/,/End of search list/p')
    if [ -z "$list" ]; then
        echo "ASSERT-FAIL: $exe printed no search list"
        fails=$((fails + 1)); continue
    fi

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
