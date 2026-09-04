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
#   ./.github/scripts/check_fork_sources.sh [daf-checkout-path] [daf-widgets-path]
#
# The optional arguments point at checked-out framework trees. With them the
# guard also inspects what those trees actually are -- their origin remotes, and
# pugl's revision against the gitlink DAF records -- rather than only the config
# that describes a future fetch. Without them only this repository's build
# config is scanned.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DAF_CHECKOUT="${1:-}"
DAFWIDGETS_CHECKOUT="${2:-}"

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

# Everything above reads configuration, which describes what a future fetch
# would do. It cannot tell you what the tree in front of you was built from: a
# checkout made before a fork was repointed keeps its old origin, and a correct
# .gitmodules sitting next to it makes the tree look clean. So when a checkout
# path is supplied, ask the checkout.
# Reduce a remote URL to the "owner/repo" it names on github.com, or to nothing
# if it names something else. Matching a substring instead accepts far too much:
# not-dusk-audio/pugl, gitlab.com/dusk-audio/pugl and
# github.com/attacker/mirror-dusk-audio/pugl all contain "dusk-audio/pugl".
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
        echo "        this is the tree being built, not a .gitmodules entry"
        failed=1
    fi
}

check_checkout_origin "DAF" "$DAF_CHECKOUT" "dusk-audio/DAF"
check_checkout_origin "DAF-Widgets" "$DAFWIDGETS_CHECKOUT" "dusk-audio/DAF-Widgets"

# pugl lives inside the DAF checkout, and its revision matters as well as its
# origin: a submodule left at some other commit compiles source DAF does not
# pin, which is exactly as wrong as fetching it from upstream.
if [ -n "$DAF_CHECKOUT" ] && [ -e "$DAF_CHECKOUT/.git" ]; then
    pugl_path="$DAF_CHECKOUT/dgl/src/pugl-upstream"
    check_checkout_origin "pugl" "$pugl_path" "dusk-audio/pugl"

    if [ -e "$pugl_path/.git" ]; then
        pugl_pin="$(git -C "$DAF_CHECKOUT" rev-parse -q --verify HEAD:dgl/src/pugl-upstream 2>/dev/null || echo '')"
        pugl_head="$(git -C "$pugl_path" rev-parse HEAD 2>/dev/null || echo '')"

        # An empty pin with a checkout present is not "nothing to compare": it
        # means DAF records no gitlink at that path, so whatever is sitting
        # there is unpinned source that no revision check can vouch for.
        # Skipping the comparison would let it through unverified.
        if [ -z "$pugl_pin" ]; then
            echo "FAIL  pugl checkout exists at $pugl_path, but DAF records no gitlink there"
            echo "        nothing pins this source, so its revision cannot be verified"
            failed=1
        elif [ -z "$pugl_head" ]; then
            echo "FAIL  pugl checkout at $pugl_path has no readable HEAD"
            failed=1
        elif [ "$pugl_pin" != "$pugl_head" ]; then
            echo "FAIL  pugl checkout is at ${pugl_head:0:12}, but DAF pins ${pugl_pin:0:12}"
            echo "        run git submodule update --init --recursive in the DAF checkout"
            failed=1
        fi
    fi
fi

if [ "$failed" = "1" ]; then
    echo
    echo "Every framework dependency must come from dusk-audio. See the fork policy"
    echo "in CLAUDE.md and docker/check_daf_pins.sh for the local equivalent."
    exit 1
fi

echo "OK    framework sources are all dusk-audio forks"
