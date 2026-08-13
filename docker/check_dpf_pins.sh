#!/usr/bin/env bash
# Compare the local DPF and DPF-Widgets checkouts against the SHAs CI builds
# with. A local build that uses different framework source than CI is a release
# hazard: the binaries users get are not the ones that were tested by hand.
#
#   ./docker/check_dpf_pins.sh          report only, exit 1 on drift
#   ./docker/check_dpf_pins.sh --fix    check the pinned SHAs out locally
#
# Docker release builds fetch the pins directly and are unaffected either way;
# this is about ad-hoc local cmake builds that point DPF_PATH at a working tree.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKFLOW="$REPO_ROOT/.github/workflows/dpf-build.yml"

DPF_PATH="${DPF_PATH:-$REPO_ROOT/../DPF}"
DPFWIDGETS_PATH="${DPFWIDGETS_PATH:-$REPO_ROOT/../DPF-Widgets}"

FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

pin_from_workflow() {
    grep -E "^\s+$1:" "$WORKFLOW" | head -1 | awk '{print $2}'
}

DPF_PIN="$(pin_from_workflow DPF_REF)"
DPFWIDGETS_PIN="$(pin_from_workflow DPFWIDGETS_REF)"

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
    dirty="$(git -C "$path" status --porcelain | wc -l)"

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

echo "CI pins (from .github/workflows/dpf-build.yml):"
echo "  DPF          $DPF_PIN"
echo "  DPF-Widgets  $DPFWIDGETS_PIN"
echo

check_one "DPF"         "$DPF_PATH"         "$DPF_PIN"         "dusk-audio/DPF"
check_one "DPF-Widgets" "$DPFWIDGETS_PATH"  "$DPFWIDGETS_PIN"  "dusk-audio/DPF-Widgets"

# The submodule has to come from our fork too, otherwise a build still pulls
# source from a repository we do not control.
if [ -f "$DPF_PATH/.gitmodules" ]; then
    pugl_url="$(git -C "$DPF_PATH" config -f .gitmodules --get submodule.dgl/src/pugl-upstream.url || echo '')"
    case "$pugl_url" in
        *dusk-audio/pugl*) echo "OK       pugl submodule: dusk-audio/pugl" ;;
        *)                 echo "REMOTE   pugl submodule: $pugl_url, expected dusk-audio/pugl"; drift=1 ;;
    esac
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
