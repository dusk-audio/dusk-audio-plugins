#!/usr/bin/env bash
# Fail the build if anything would fetch framework source from upstream.
#
# DAF is a hard fork under dusk-audio. Pugl and widgets are tracked trees in
# the same checkout, so their source is covered by the single DAF revision.
#
#   ./.github/scripts/check_fork_sources.sh [daf-checkout-path]
#
# Without a checkout argument, only this repository's build config is scanned.
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

# Match the exact host and owner/repo, not a lookalike containing the name.
# Twin of the function in docker/check_daf_pins.sh; keep the two in step.
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

check_checkout_origin() {
    local name="$1" path="$2" expect="$3"

    [ -n "$path" ] || return 0
    if [ ! -e "$path/.git" ]; then
        echo "FAIL  $name: no git checkout at $path, so its source cannot be verified"
        failed=1
        return 0
    fi

    local remote
    remote="$(git -C "$path" remote get-url origin 2>/dev/null || echo '(none)')"
    if [ "$(github_repo_of "$remote")" != "$expect" ]; then
        echo "FAIL  $name checkout compiles from $remote, expected github.com/$expect"
        failed=1
    fi
}

check_checkout_origin "DAF" "$DAF_CHECKOUT" "dusk-audio/DAF"

if [ -n "$DAF_CHECKOUT" ] && [ -e "$DAF_CHECKOUT/.git" ]; then
    if [ -e "$DAF_CHECKOUT/.gitmodules" ]; then
        echo "FAIL  DAF uses submodules instead of the consolidated tree"
        failed=1
    fi
    for component in dgl/src/pugl-upstream widgets; do
        path="$DAF_CHECKOUT/$component"
        if [ -e "$path/.git" ]; then
            echo "FAIL  $component is a nested checkout"
            failed=1
        elif [ "$(git -C "$DAF_CHECKOUT" cat-file -t "HEAD:$component" 2>/dev/null || true)" != tree ]; then
            echo "FAIL  $component is not a tracked tree in DAF"
            failed=1
        elif [ -n "$(git -C "$DAF_CHECKOUT" status --porcelain -- "$component")" ]; then
            echo "FAIL  $component differs from the DAF revision"
            failed=1
        fi
    done
fi

if [ "$failed" = "1" ]; then
    echo
    echo "Every framework dependency must come from dusk-audio. See the fork policy"
    echo "in CLAUDE.md and docker/check_daf_pins.sh for the local equivalent."
    exit 1
fi

echo "OK    framework sources are all dusk-audio forks"
