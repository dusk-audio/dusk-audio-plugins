#!/usr/bin/env bash
# Fail the build if anything would fetch framework source from upstream.
#
# DAF, DAF-Widgets and pugl are hard forks under dusk-audio with local patches
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
#   ./.github/scripts/check_fork_sources.sh [daf-checkout-path]
#
# The optional argument points at a checked-out DAF so the submodule URL is
# checked too; without it only this repository's build config is scanned.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DAF_CHECKOUT="${1:-}"

# Scan the files that decide where source comes from. Documentation may discuss
# upstream freely, so it is deliberately out of scope.
CONFIG_FILES=(
    "$REPO_ROOT"/.github/workflows/daf-build.yml
    "$REPO_ROOT"/.github/workflows/daf-release.yml
    "$REPO_ROOT"/.github/workflows/daf-au-test.yml
    "$REPO_ROOT"/docker/build_release.sh
)

failed=0

for f in "${CONFIG_FILES[@]}"; do
    [ -f "$f" ] || continue

    # Strip comments before matching: a comment explaining the fork policy is
    # allowed to name upstream, a `repository:` field is not.
    # Match any DISTRHO/ repository, not a fixed list of names. The list used to
    # be DPF/DPF-Widgets/pugl and was silently rewritten to DAF/DAF-Widgets by the
    # 2026-08-22 rename, which matches nothing upstream and disarmed this guard.
    if hits="$(sed 's/#.*$//' "$f" | grep -nE 'DISTRHO/' || true)"; [ -n "$hits" ]; then
        echo "FAIL  ${f#"$REPO_ROOT"/} fetches framework source from upstream:"
        echo "$hits" | sed 's/^/        /'
        failed=1
    fi
done

if [ -n "$DAF_CHECKOUT" ] && [ -f "$DAF_CHECKOUT/.gitmodules" ]; then
    if hits="$(grep -nE 'url *= *.*DISTRHO/' "$DAF_CHECKOUT/.gitmodules" || true)"; [ -n "$hits" ]; then
        echo "FAIL  the DAF checkout's .gitmodules fetches a submodule from upstream:"
        echo "$hits" | sed 's/^/        /'
        echo "        fix it in dusk-audio/DAF, not here: a fork inherits this file"
        failed=1
    fi
fi

if [ "$failed" = "1" ]; then
    echo
    echo "Every framework dependency must come from dusk-audio. See the fork policy"
    echo "in CLAUDE.md and docker/check_daf_pins.sh for the local equivalent."
    exit 1
fi

echo "OK    framework sources are all dusk-audio forks"
