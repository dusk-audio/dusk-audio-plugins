#!/usr/bin/env bash
# Compare the local DAF and DAF-Widgets checkouts against the SHAs CI builds
# with. A local build that uses different framework source than CI is a release
# hazard: the binaries users get are not the ones that were tested by hand.
#
#   ./docker/check_daf_pins.sh          report only, exit 1 on drift
#   ./docker/check_daf_pins.sh --fix    check the pinned SHAs out locally
#
# Docker release builds fetch the pins directly and are unaffected either way;
# this is about ad-hoc local cmake builds that point DAF_PATH at a working tree.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW="$REPO_ROOT/.github/workflows/daf-build.yml"

DAF_PATH="${DAF_PATH:-$REPO_ROOT/../DAF}"
DAFWIDGETS_PATH="${DAFWIDGETS_PATH:-$REPO_ROOT/../DAF-Widgets}"

FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

pin_from_workflow() {
    grep -E "^\s+$1:" "$WORKFLOW" | head -1 | awk '{print $2}'
}

DAF_PIN="$(pin_from_workflow DAF_REF)"
DAFWIDGETS_PIN="$(pin_from_workflow DAFWIDGETS_REF)"

drift=0

check_one() {
    local name="$1" path="$2" pin="$3" expect_remote="$4"

    if [ ! -d "$path/.git" ]; then
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

    case "$remote" in
        *"$expect_remote"*) ;;
        *)
            echo "REMOTE   $name: origin is $remote, expected $expect_remote"
            drift=1
            ;;
    esac

    if [ "$head" != "$pin" ]; then
        echo "DRIFT    $name: local $(echo "$head" | cut -c1-12), CI builds $(echo "$pin" | cut -c1-12)"
        drift=1
        if [ "$FIX" = "1" ]; then
            echo "         checking out the pinned commit"
            git -C "$path" fetch -q origin
            git -C "$path" checkout -q "$pin"
            git -C "$path" submodule update -q --init --recursive
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
echo "  DAF-Widgets  $DAFWIDGETS_PIN"
echo

check_one "DAF"         "$DAF_PATH"         "$DAF_PIN"         "dusk-audio/DAF"
check_one "DAF-Widgets" "$DAFWIDGETS_PATH"  "$DAFWIDGETS_PIN"  "dusk-audio/DAF-Widgets"

# The submodule has to come from our fork too, otherwise a build still pulls
# source from a repository we do not control.
if [ -f "$DAF_PATH/.gitmodules" ]; then
    pugl_url="$(git -C "$DAF_PATH" config -f .gitmodules --get submodule.dgl/src/pugl-upstream.url || echo '')"
    case "$pugl_url" in
        *dusk-audio/pugl*) echo "OK       pugl .gitmodules: dusk-audio/pugl" ;;
        *)                 echo "REMOTE   pugl .gitmodules: $pugl_url, expected dusk-audio/pugl"; drift=1 ;;
    esac
fi

# ...and .gitmodules only describes the next fetch. What a build actually
# compiles is whatever sits in the working tree now, which can be a different
# remote at a different revision: a checkout created before the fork was
# repointed keeps its old origin forever, and .gitmodules saying the right thing
# hides it. Check the checkout itself against the gitlink DAF records.
PUGL_PATH="$DAF_PATH/dgl/src/pugl-upstream"
if [ -d "$DAF_PATH/.git" ]; then
    pugl_pin="$(git -C "$DAF_PATH" rev-parse -q --verify HEAD:dgl/src/pugl-upstream 2>/dev/null || echo '')"

    if [ -z "$pugl_pin" ]; then
        echo "MISSING  pugl checkout: DAF records no gitlink at dgl/src/pugl-upstream"
        drift=1
    elif [ ! -e "$PUGL_PATH/.git" ]; then
        echo "MISSING  pugl checkout: nothing at $PUGL_PATH; run git submodule update --init --recursive"
        drift=1
    else
        pugl_head="$(git -C "$PUGL_PATH" rev-parse HEAD)"
        pugl_remote="$(git -C "$PUGL_PATH" remote get-url origin 2>/dev/null || echo '(none)')"

        case "$pugl_remote" in
            *dusk-audio/pugl*) ;;
            *)
                echo "REMOTE   pugl checkout: origin is $pugl_remote, expected dusk-audio/pugl"
                echo "         .gitmodules is not evidence: this tree compiles from that remote"
                drift=1
                ;;
        esac

        if [ "$pugl_head" != "$pugl_pin" ]; then
            echo "DRIFT    pugl checkout: local $(echo "$pugl_head" | cut -c1-12), DAF pins $(echo "$pugl_pin" | cut -c1-12)"
            drift=1
            if [ "$FIX" = "1" ]; then
                echo "         checking out the pinned commit"
                git -C "$DAF_PATH" submodule update -q --init --recursive
            fi
        else
            echo "OK       pugl checkout: $(echo "$pugl_head" | cut -c1-12)"
        fi
    fi
fi

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
