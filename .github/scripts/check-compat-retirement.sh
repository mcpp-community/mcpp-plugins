#!/usr/bin/env bash
# Compatibility units retire themselves (mcpp#734, design §3.7).
#
# A compatibility unit keeps a behaviour for six months after the release that
# replaced it. Its header states the date:
#
#   // COMPATIBILITY UNIT            (# COMPATIBILITY UNIT in mcpp.toml)
#   //   retires:     2027-03-28
#
# This check lists every unit and fails when one is past its date, so that a
# deferral cannot outlive its reason unnoticed. Every file under a `compat/`
# directory must carry the header; a file there without one is reported, which
# keeps the list from being empty because the marker was misspelt.
#
#   bash .github/scripts/check-compat-retirement.sh
#   COMPAT_TODAY=2027-03-29 bash .github/scripts/check-compat-retirement.sh   # the failure, on demand
set -euo pipefail
cd "$(dirname "$0")/../.."

today="${COMPAT_TODAY:-$(date -u +%Y-%m-%d)}"
fail=0
units=0

# Every file under a compat/ directory, outside build outputs and fixtures.
while IFS= read -r f; do
    grep -qE '^(//|#) COMPATIBILITY UNIT$' "$f" || { echo "FAIL: $f is under compat/ and has no COMPATIBILITY UNIT header"; fail=1; }
done < <(find . -path ./tests -prune -o -path '*/target' -prune -o -path '*/compat/*' -type f -print | sort)

# Every header, wherever it is: its retirement date.
while IFS=$'\t' read -r file date; do
    units=$((units + 1))
    if [ -z "$date" ]; then
        echo "FAIL: $file: a COMPATIBILITY UNIT without a 'retires:' date"
        fail=1
    elif [[ "$today" > "$date" ]]; then
        echo "FAIL: $file: retired on $date (today is $today); remove the unit and what it keeps"
        fail=1
    else
        echo "ok: $file retires on $date"
    fi
done < <(find . -path ./tests -prune -o -path '*/target' -prune -o -path ./.git -prune -o \
              \( -name '*.cppm' -o -name 'mcpp.toml' \) -type f -print | sort |
         xargs awk '
             /^(\/\/|#) COMPATIBILITY UNIT$/ { if (pending) print FILENAME "\t"; pending = 1; next }
             pending && /retires:/ { match($0, /[0-9]{4}-[0-9]{2}-[0-9]{2}/);
                                     print FILENAME "\t" substr($0, RSTART, RLENGTH); pending = 0 }
             ENDFILE { if (pending) print FILENAME "\t"; pending = 0 }')

[ "$units" -gt 0 ] || { echo "FAIL: no COMPATIBILITY UNIT header found"; fail=1; }
echo "$units compatibility unit(s)"
exit "$fail"
