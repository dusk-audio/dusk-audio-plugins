#!/usr/bin/env bash
# Compare the local DAF checkout against the SHA CI builds
# with. A local build that uses different framework source than CI is a release
# hazard: the binaries users get are not the ones that were tested by hand.
#
#   ./docker/check_daf_pins.sh          report only, exit 1 on drift
#   ./docker/check_daf_pins.sh --fix    check the pinned SHA out locally
#
# Docker release builds fetch the pins directly and are unaffected either way;
# this is about ad-hoc local cmake builds that point DAF_PATH at a working tree.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW="$REPO_ROOT/.github/workflows/daf-build.yml"

DAF_PATH="${DAF_PATH:-$REPO_ROOT/../DAF}"

FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

# Reduce a remote URL to the "owner/repo" it names on github.com, or to nothing
# if it names something else. A substring test accepts far too much:
# not-dusk-audio/pugl, gitlab.com/dusk-audio/pugl and
# github.com/attacker/mirror-dusk-audio/pugl all contain "dusk-audio/pugl".
# Twin of the function in .github/scripts/check_fork_sources.sh.
github_repo_of() {
    local url="$1" path=""
    case "$url" in
        git@github.com:*)       path="${url#git@github.com:}" ;;
        ssh://git@github.com/*) path="${url#ssh://git@github.com/}" ;;
        https://github.com/*)   path="${url#https://github.com/}" ;;
        http://github.com/*)    path="${url#http://github.com/}" ;;
        *) return 0 ;;
    esac
    path="${path%/}"
    path="${path%.git}"
    printf '%s' "${path%/}"
}

pin_from_workflow() {
    grep -E "^\s+$1:" "$WORKFLOW" | head -1 | awk '{print $2}'
}

DAF_PIN="$(pin_from_workflow DAF_REF)"

drift=0

check_one() {
    local name="$1" path="$2" pin="$3" expect_remote="$4"

    if [ ! -e "$path/.git" ]; then
        echo "MISSING  $name: no git checkout at $path"
        drift=1
        return
    fi

    local head remote dirty
    head="$(git -C "$path" rev-parse HEAD)"
    remote="$(git -C "$path" remote get-url origin 2>/dev/null || echo '(none)')"
    # tr: BSD wc pads its count with leading spaces, so a bare comparison
    # against "0" is always false and every checkout reports DIRTY.
    dirty="$(git -C "$path" status --porcelain | wc -l | tr -d "[:space:]")"

    if [ "$(github_repo_of "$remote")" != "$expect_remote" ]; then
        echo "REMOTE   $name: origin is $remote, expected github.com/$expect_remote"
        drift=1
    fi

    if [ "$head" != "$pin" ]; then
        echo "DRIFT    $name: local $(echo "$head" | cut -c1-12), CI builds $(echo "$pin" | cut -c1-12)"
        drift=1
        if [ "$FIX" = "1" ]; then
            echo "         checking out the pinned commit"
            # Report rather than die. set -e would otherwise abort the whole run
            # on an offline machine, halfway through, with no explanation and
            # the remaining checks never reported.
            if ! git -C "$path" fetch -q origin 2>/dev/null; then
                echo "         (could not reach origin; trying with the objects already here)"
            fi
            if ! git -C "$path" checkout -q "$pin" 2>/dev/null; then
                echo "         could not check out $pin: fetch it and re-run"
            fi
        fi
    else
        echo "OK       $name: $(echo "$head" | cut -c1-12)"
    fi

    if [ "$dirty" != "0" ]; then
        echo "DIRTY    $name: $dirty uncommitted file(s); a local build includes changes CI never sees"
        drift=1
    fi
}

echo "CI pins (from .github/workflows/daf-build.yml):"
echo "  DAF          $DAF_PIN"
echo

check_one "DAF"         "$DAF_PATH"         "$DAF_PIN"         "dusk-audio/DAF"


echo
if [ "$drift" = "0" ]; then
    echo "Local checkouts match what CI builds."
else
    if [ "$FIX" = "1" ]; then
        echo "Pinned commits checked out. Re-run without --fix to confirm."
    else
        echo "Local builds will not match CI. Re-run with --fix, or bump the pins if the"
        echo "local commits are the ones that should ship."
    fi
    exit 1
fi
