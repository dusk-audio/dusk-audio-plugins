#!/usr/bin/env bash
# Compare the local DAF checkout with the DAF commit CI would build right now.
# A local build that uses different framework source than CI is a release
# hazard: the binaries users get are not the ones that were tested by hand.
#
#   ./docker/check_daf_checkout.sh          report only; exit 0 on a match, else 1
#   ./docker/check_daf_checkout.sh --fix    fast-forward a clean checkout
#
# CI builds the ref in .github/daf-ref: normally a branch (main), resolved from
# the checkout's own origin here; or a full SHA, when builds are held back. The
# ref is read from that file only. $DAF_REF is deliberately ignored: CI never
# sets it, so honouring it here would let this check agree with something CI
# does not build.
#
# Environment:
#   DAF_PATH         checkout to check (default: ../DAF next to this repo)
#   DAF_SKIP_FETCH   1 = fetch nothing (offline use, and the provenance tests):
#                    a branch is compared with its last fetch of origin/<branch>,
#                    a held-back SHA with HEAD only, not checked against origin.
#                    A match still exits 0, saying what was not refreshed or
#                    checked, and --fix never moves onto an unchecked held-back
#                    SHA. Without it, a failed fetch leaves the result
#                    UNVERIFIED (exit 1).
set -euo pipefail
# git exports GIT_DIR to hooks; it would point every git -C below elsewhere.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESOLVER="$REPO_ROOT/.github/scripts/resolve_daf_ref.sh"

DAF_PATH="${DAF_PATH:-$REPO_ROOT/../DAF}"

FIX=0
[ "${1:-}" = "--fix" ] && FIX=1

# Reduce a remote URL to the "owner/repo" it names on github.com, or to nothing
# if it names something else. A substring test accepts far too much:
# not-dusk-audio/DAF, gitlab.com/dusk-audio/DAF and
# github.com/attacker/mirror-dusk-audio/DAF all contain "dusk-audio/DAF".
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

short() { printf '%s' "$1" | cut -c1-12; }

# --configured reads .github/daf-ref only (never $DAF_REF), and exits 1 with a
# message if the file is missing or names nothing.
REF="$("$RESOLVER" --configured)"

if [ ! -e "$DAF_PATH/.git" ]; then
    echo "MISSING  DAF: no git checkout at $DAF_PATH"
    exit 1
fi

# A checkout of some other repository is not "behind"; nothing below is
# meaningful for it, and --fix must never fast-forward onto a foreign remote.
remote="$(git -C "$DAF_PATH" remote get-url origin 2>/dev/null || echo '(none)')"
if [ "$(github_repo_of "$remote")" != "dusk-audio/DAF" ]; then
    echo "REMOTE   DAF: origin is $remote, expected github.com/dusk-audio/DAF"
    echo
    echo "Point origin at the dusk-audio fork before comparing; nothing was fetched or changed."
    exit 1
fi

# verified: yes = refreshed from origin, skipped = DAF_SKIP_FETCH, no = fetch
# failed, rejected = CI's resolver will refuse the held-back commit.
verified=yes
fetch() {
    if [ "${DAF_SKIP_FETCH:-0}" = "1" ]; then
        verified=skipped
        return 0
    fi
    if ! git -C "$DAF_PATH" fetch -q origin "$1" 2>/dev/null; then
        echo "UNVERIFIED DAF: could not fetch $2 from origin; the comparison below uses the last fetch"
        verified=no
    fi
}

# The commit CI would build: a held-back SHA as written, or the branch head.
held=0
if [[ "$REF" =~ ^[0-9a-f]{40}$ ]]; then
    held=1
    expected="$REF"
    # Fetched even when present locally: CI can only build it if origin has it.
    fetch "$expected" "commit $(short "$expected")"
    # And CI's resolver also requires it to be on an origin branch; apply the
    # same rule here so the two cannot disagree.
    if [ "$verified" = "yes" ]; then
        if ! reason="$(DAF_URL="$remote" "$RESOLVER" "$expected" 2>&1 >/dev/null)"; then
            echo "REJECTED DAF: ${reason#resolve_daf_ref: }"
            verified=rejected
        fi
    fi
    if ! git -C "$DAF_PATH" cat-file -e "$expected^{commit}" 2>/dev/null; then
        echo "UNKNOWN  DAF: held-back commit $(short "$expected") is not in $DAF_PATH; fetch it and re-run"
        exit 1
    fi
else
    # Explicit destination: a single-branch clone would otherwise only update FETCH_HEAD.
    fetch "+refs/heads/$REF:refs/remotes/origin/$REF" "origin/$REF"
    expected="$(git -C "$DAF_PATH" rev-parse --verify -q "refs/remotes/origin/$REF" || true)"
    if [ -z "$expected" ]; then
        echo "UNKNOWN  DAF: origin/$REF has never been fetched into $DAF_PATH"
        exit 1
    fi
fi

head="$(git -C "$DAF_PATH" rev-parse HEAD)"
# tr: BSD wc pads its count with leading spaces, so a bare comparison
# against "0" is always false and every checkout reports DIRTY.
dirty="$(git -C "$DAF_PATH" status --porcelain | wc -l | tr -d "[:space:]")"
mismatch=0

# merge-base --is-ancestor: 0 = yes, 1 = no, anything else = could not tell.
is_ancestor() {
    local status=0
    git -C "$DAF_PATH" merge-base --is-ancestor "$1" "$2" 2>/dev/null || status=$?
    return "$status"
}

fixed=0
if [ "$head" = "$expected" ]; then
    echo "OK       DAF: $(short "$head") ($REF)"
else
    mismatch=1
    s=0; is_ancestor "$head" "$expected" || s=$?
    t=0; is_ancestor "$expected" "$head" || t=$?
    if [ "$s" -gt 1 ] || [ "$t" -gt 1 ]; then
        relation="not comparable with"
    elif [ "$s" = 0 ]; then
        relation="behind"
    elif [ "$t" = 0 ]; then
        relation="ahead of"
    elif [ -z "$(git -C "$DAF_PATH" merge-base "$head" "$expected" 2>/dev/null || true)" ]; then
        relation="unrelated to"
    else
        relation="diverged from"
    fi
    if [ "$verified" = "yes" ]; then
        echo "DRIFT    DAF: local $(short "$head") is $relation $REF, which CI builds as $(short "$expected")"
    else
        echo "DRIFT    DAF: local $(short "$head") is $relation $REF, which .github/daf-ref names as $(short "$expected")"
    fi
    if [ "$FIX" = "1" ]; then
        if [ "$verified" != "yes" ] && [ "$verified" != "skipped" ]; then
            echo "         not fixing: $(short "$expected") could not be confirmed as what CI builds"
        elif [ "$relation" != "behind" ]; then
            if [ "$held" = "1" ]; then
                echo "         not fixing: builds are held at $(short "$expected"); check it out with"
                echo "         git -C \"$DAF_PATH\" checkout $expected"
            else
                echo "         not fixing: local has commits CI does not build; push them or reset deliberately"
            fi
        elif [ "$held" = "1" ] && [ "$verified" = "skipped" ]; then
            # Only a local cat-file check ran: the commit may be local or unpushed.
            echo "         not fixing: DAF_SKIP_FETCH=1, so $(short "$expected") was not checked against origin; re-run without it"
        elif [ "$dirty" != "0" ]; then
            echo "         not fixing: uncommitted changes would be carried along; commit or stash them first"
        elif ! git -C "$DAF_PATH" merge -q --ff-only "$expected" 2>/dev/null; then
            echo "         could not fast-forward to $(short "$expected")"
        else
            echo "         fast-forwarded to $(short "$expected")"
            fixed=1
        fi
    elif [ "$held" = "1" ] && [ "$relation" != "behind" ] && [ "$verified" != "rejected" ]; then
        echo "         builds are held at $(short "$expected"); git -C \"$DAF_PATH\" checkout $expected"
    fi
fi

if [ "$dirty" != "0" ]; then
    echo "DIRTY    DAF: $dirty uncommitted file(s); a local build includes changes CI never sees"
    mismatch=1
fi

echo
if [ "$verified" = "rejected" ]; then
    echo "CI will refuse .github/daf-ref: the held-back commit $(short "$expected") is on no branch of origin."
    exit 1
fi
if [ "$mismatch" = "0" ] && [ "$verified" = "yes" ]; then
    echo "Local DAF matches what CI builds."
    exit 0
fi
if [ "$mismatch" = "0" ] && [ "$verified" = "skipped" ]; then
    if [ "$held" = "1" ]; then
        echo "Local DAF is at the held-back commit $(short "$REF") (DAF_SKIP_FETCH=1: not checked against origin)."
    else
        echo "Local DAF matches the last fetch of $REF (DAF_SKIP_FETCH=1: not refreshed)."
    fi
    exit 0
fi
if [ "$mismatch" = "0" ]; then
    echo "Local DAF matches the last fetch of $REF, but that could not be refreshed."
elif [ "$fixed" = "1" ]; then
    echo "Re-run without --fix to confirm."
elif [ "$held" = "1" ]; then
    echo "Local builds will not match CI: check out the held-back commit shown above."
else
    echo "Local builds will not match CI. Re-run with --fix to fast-forward a clean"
    echo "checkout, or push the local DAF commits if they are the ones that should ship."
fi
exit 1
