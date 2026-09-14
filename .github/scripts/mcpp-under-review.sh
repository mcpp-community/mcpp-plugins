#!/usr/bin/env bash
# The mcpp a job's steps run, when that is not the released one.
#
# `MCPP_SOURCE_REF` names a branch or tag of mcpp-community/mcpp. Empty, the
# steps run the release `MCPP_VERSION` names, which the step before this one
# fetched into `$MCPP`. Set, this script builds mcpp at that reference with the
# released mcpp and points `$MCPP` at the result, so that a change to the engine
# is measured against this collection before either is released.
#
# A SCRIPT, NOT A STEP COPIED INTO EACH JOB. Two copies drift, and a job that
# installs mcpp without this channel builds manifests written for the engine
# under review with the released engine, which accepts a key it does not know
# and proceeds without the semantics the key asks for.
#
# WHAT THIS SCRIPT WRITES IS NOT EVIDENCE THAT IT TOOK EFFECT. `GITHUB_ENV`
# governs the steps that follow, so the job's next step compares what `$MCPP`
# names and prints with what this script built.
#
# Environment: MCPP (the released mcpp), MCPP_SOURCE_REF, RUNNER_TEMP, GITHUB_ENV.
set -euo pipefail

: "${MCPP:?the released mcpp, which the step before this one fetches}"
: "${GITHUB_ENV:?}"
: "${RUNNER_TEMP:?}"

echo "MCPP_RELEASED=$MCPP" >> "$GITHUB_ENV"
if [ -z "${MCPP_SOURCE_REF:-}" ]; then
    echo "MCPP_SOURCE_REF is empty: the steps run the released $("$MCPP" --version | head -1)"
    exit 0
fi

# In Git Bash on Windows `RUNNER_TEMP` is a Windows path (`D:\a\_temp`), and the
# binary path this script exports is used by bash in every later step, so the
# directory is spelled in bash's own syntax.
temp="$RUNNER_TEMP"
if command -v cygpath > /dev/null; then temp=$(cygpath -u "$temp"); fi
src="$temp/mcpp-src"
rm -rf "$src"
git clone --quiet --depth 1 --branch "$MCPP_SOURCE_REF" \
    https://github.com/mcpp-community/mcpp.git "$src"
echo "READING source: mcpp-community/mcpp $MCPP_SOURCE_REF at $(git -C "$src" rev-parse HEAD)"

# The clone's `.xlings.json` pins the mcpp that builds mcpp in that repository's
# own CI, and the pin does not move with this job's release: a build inside the
# checkout obeys it and installs a version this job did not choose. Removed, the
# released mcpp above builds the source.
rm -f "$src/.xlings.json"

(cd "$src" && "$MCPP" build)

# A fresh clone holds no earlier build, so what remains is this build's product;
# the count is asserted rather than assumed.
built=$(find "$src/target" -type f \( -name mcpp -o -name mcpp.exe \))
count=$(printf '%s\n' "$built" | grep -c . || true)
if [ "$count" != 1 ]; then
    echo "::error::expected one mcpp binary from $MCPP_SOURCE_REF, found $count"
    printf '%s\n' "$built" | sed 's/^/    /'
    exit 1
fi
version=$("$built" --version | head -1)
echo "READING under review: $version at $built"
{
    echo "MCPP=$built"
    echo "MCPP_UNDER_REVIEW=$built"
    echo "MCPP_UNDER_REVIEW_VERSION=$version"
} >> "$GITHUB_ENV"
