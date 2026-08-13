#!/usr/bin/env bash
# Fail the build if anything would fetch framework source from upstream.
#
# DPF, DPF-Widgets and pugl are hard forks under dusk-audio with local patches
# that upstream does not have. A build that pulls any of them from DISTRHO does
# not contain those patches, and the divergence is silent: it compiles, it runs,
# and the binary simply is not the one that was tested.
#
# Two leaks have happened for real, and neither was a typo:
#   - workflow `repository:` fields copied from upstream examples, which name
#     DISTRHO because that is where upstream lives;
#   - the `url` in the fork's own .gitmodules, which GitHub copies verbatim when
#     a fork is created, so pugl kept pointing at DISTRHO long after the fork.
#
#   ./.github/scripts/check_fork_sources.sh [dpf-checkout-path]
#
# The optional argument points at a checked-out DPF so the submodule URL is
# checked too; without it only this repository's build config is scanned.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DPF_CHECKOUT="${1:-}"

# Scan the files that decide where source comes from. Documentation may discuss
# upstream freely, so it is deliberately out of scope.
CONFIG_FILES=(
    "$REPO_ROOT"/.github/workflows/dpf-build.yml
    "$REPO_ROOT"/.github/workflows/dpf-release.yml
    "$REPO_ROOT"/.github/workflows/dpf-au-test.yml
    "$REPO_ROOT"/docker/build_release.sh
)

failed=0

for f in "${CONFIG_FILES[@]}"; do
    [ -f "$f" ] || continue

    # Strip comments before matching: a comment explaining the fork policy is
    # allowed to name upstream, a `repository:` field is not.
    if hits="$(sed 's/#.*$//' "$f" | grep -nE 'DISTRHO/(DPF|DPF-Widgets|pugl)' || true)"; [ -n "$hits" ]; then
        echo "FAIL  ${f#"$REPO_ROOT"/} fetches framework source from upstream:"
        echo "$hits" | sed 's/^/        /'
        failed=1
    fi
done

if [ -n "$DPF_CHECKOUT" ] && [ -f "$DPF_CHECKOUT/.gitmodules" ]; then
    if hits="$(grep -nE 'url *= *.*DISTRHO/' "$DPF_CHECKOUT/.gitmodules" || true)"; [ -n "$hits" ]; then
        echo "FAIL  the DPF checkout's .gitmodules fetches a submodule from upstream:"
        echo "$hits" | sed 's/^/        /'
        echo "        fix it in dusk-audio/DPF, not here: a fork inherits this file"
        failed=1
    fi
fi

if [ "$failed" = "1" ]; then
    echo
    echo "Every framework dependency must come from dusk-audio. See the fork policy"
    echo "in CLAUDE.md and docker/check_dpf_pins.sh for the local equivalent."
    exit 1
fi

echo "OK    framework sources are all dusk-audio forks"
