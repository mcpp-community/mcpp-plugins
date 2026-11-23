#!/usr/bin/env bash
# A workaround for a gap in mcpp is marked, and the mark is checked (0.27.0).
#
# docs/plugin-development.md rule 8: an engine gap is reported to mcpp, and a
# workaround that exists is recorded with the issue number and removed when the
# engine provides the capability. This check keeps the record from rotting, the
# way check-compat-retirement.sh keeps a deferral from outliving its reason. A
# workaround begins with this header, wherever it is in a unit:
#
#   // ENGINE-GAP WORKAROUND
#   //   gap:         what the engine does not do
#   //   issue:       mcpp#1011
#   //   remove when: the engine capability that makes the workaround redundant
#   //   note:        (optional) why the workaround is harmless meanwhile
#
# and a place that calls it says `// ENGINE-GAP WORKAROUND (mcpp#1011)`. The check
# fails when
#
#   - a header lacks `gap:`, an `issue:` of the form `mcpp#<number>`, or `remove when:`;
#   - a line names ENGINE-GAP in any other form (a misspelt header would otherwise
#     make the list empty and the check pass);
#   - an issue is not named in the documentation under docs/.
#
# No header at all is fine: it is the state after the last workaround is removed.
#
#   bash .github/scripts/check-engine-gaps.sh
set -euo pipefail
cd "$(dirname "$0")/../.."

fail=0
blocks=0
issues=""

mapfile -t files < <(find . -path ./tests -prune -o -path '*/target' -prune -o -path ./.git -prune \
                          -o -path ./.agents -prune -o -path ./docs -prune \
                          -o \( -name '*.cppm' -o -name '*.mcpp' -o -name 'mcpp.toml' \) -type f -print | sort)

for f in "${files[@]}"; do
    # Every line that names ENGINE-GAP is a header, a call site, or an error.
    while IFS=: read -r n text; do
        text="${text#"${text%%[![:space:]]*}"}"
        case "$text" in
            "// ENGINE-GAP WORKAROUND") ;;
            "// ENGINE-GAP WORKAROUND (mcpp#"[0-9]*")"*) ;;
            *) echo "FAIL: $f:$n: a line names ENGINE-GAP and is neither the header nor a call site: $text"; fail=1 ;;
        esac
    done < <(grep -nE 'ENGINE[-_ ]GAP' "$f" || true)
done

while IFS=$'\t' read -r file line gap issue when; do
    blocks=$((blocks + 1))
    ok=1
    [ -n "$gap" ]   || { echo "FAIL: $file:$line: ENGINE-GAP WORKAROUND without 'gap:'"; ok=0; }
    [ -n "$when" ]  || { echo "FAIL: $file:$line: ENGINE-GAP WORKAROUND without 'remove when:'"; ok=0; }
    if [[ ! "$issue" =~ ^mcpp#[0-9]+$ ]]; then
        echo "FAIL: $file:$line: ENGINE-GAP WORKAROUND needs 'issue: mcpp#<number>' (got '$issue')"; ok=0
    else
        issues="$issues $issue"
    fi
    if [ "$ok" = 1 ]; then echo "ok: $file:$line $issue"; else fail=1; fi
done < <(printf '%s\0' "${files[@]}" | xargs -0 awk '
    function emit() { if (inblock) print FILENAME "\t" start "\t" gap "\t" issue "\t" when; inblock = 0 }
    function field(s, key) { sub("^//[ ]+" key ":[ ]*", "", s); return s }
    /^\/\/ ENGINE-GAP WORKAROUND$/ { emit(); inblock = 1; start = FNR; gap = ""; issue = ""; when = ""; next }
    inblock && /^\/\/[ ]+gap:/         { gap = field($0, "gap"); next }
    inblock && /^\/\/[ ]+issue:/       { issue = field($0, "issue"); next }
    inblock && /^\/\/[ ]+remove when:/ { when = field($0, "remove when"); next }
    inblock && /^\/\/[ ]+(note|[ ])/   { next }
    inblock                            { emit() }
    ENDFILE { emit() }')

for issue in $(printf '%s\n' $issues | sort -u); do
    grep -rqF "$issue" docs/ || { echo "FAIL: $issue is not named under docs/ (rule 8: the documentation records the workaround and its issue)"; fail=1; }
done

echo "$blocks engine-gap workaround(s)"
exit "$fail"
