#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#
# Resolve the DAF revision a build should use to a full commit SHA on
# dusk-audio/DAF.
#
#   resolve_daf_ref.sh [BRANCH|SHA]    print the commit SHA to build
#   resolve_daf_ref.sh --configured    print the ref in .github/daf-ref, unresolved
#   resolve_daf_ref.sh --github-output resolve .github/daf-ref and write `sha=`
#                                      to $GITHUB_OUTPUT (and a job summary line);
#                                      the only form a workflow may use
#
# The ref comes from, in order: the argument, $DAF_REF (a local override for
# docker/build_release.sh), then .github/daf-ref, the single place the
# repository sets it. In CI (GITHUB_ACTIONS=true) an argument, DAF_REF and
# DAF_URL are all refused outright: an `env: DAF_REF:` left in a workflow (the
# old pin's shape) or a SHA passed to this script would otherwise override
# .github/daf-ref without anyone noticing.
# --configured takes no other argument and ignores $DAF_REF (which CI refuses
# anyway); it is what docker/check_daf_checkout.sh compares with.
#
# A branch name is resolved with `git ls-remote`, once per run, and every job is
# handed that one SHA, so a merge to DAF main during a run cannot put two
# framework revisions into one release. A full 40-character SHA (any case) is
# how builds are held on a known revision; it must be on some branch of
# DAF_URL, so a local or unpushed commit (or one only reachable from a fork or a
# pull request) fails here and not later in every build job.
# Tags and abbreviated SHAs are not accepted. In .github/daf-ref, text after a
# '#' is a comment. At most one argument is taken: nothing may follow a flag.
#
# Prints the result on stdout. On any failure it exits non-zero with a message
# naming the ref; it never falls back to another revision.
set -euo pipefail
# A caller's GIT_DIR (git exports it to hooks) would redirect the probe
# repository below into the caller's own repository.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR

# One argument at most: a ref after --github-output (or --configured) would
# otherwise be taken as the ref to build, past the CI refusal below.
if [ $# -gt 1 ]; then
    echo "resolve_daf_ref: expected at most one argument, got $#: $*" >&2
    exit 1
fi

if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
    if [ -n "${DAF_REF:-}${DAF_URL:-}" ]; then
        echo "resolve_daf_ref: DAF_REF/DAF_URL are set in CI; the revision is set only in .github/daf-ref" >&2
        exit 1
    fi
    if [ -n "${1:-}" ] && [ "$1" != "--configured" ] && [ "$1" != "--github-output" ]; then
        echo "resolve_daf_ref: a ref argument ('$1') was passed in CI; the revision is set only in .github/daf-ref" >&2
        exit 1
    fi
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REF_FILE="$REPO_ROOT/.github/daf-ref"
DAF_URL="${DAF_URL:-https://github.com/dusk-audio/DAF.git}"

configured_ref() {
    if [ ! -f "$REF_FILE" ]; then
        echo "resolve_daf_ref: $REF_FILE is missing" >&2
        return 1
    fi
    local ref
    # Drop comments and all whitespace (CR included), keep the first value left.
    ref="$(sed -e 's/#.*//' -e 's/[[:space:]]//g' "$REF_FILE" | grep -v '^$' | head -n 1 || true)"
    if [ -z "$ref" ]; then
        echo "resolve_daf_ref: $REF_FILE names no ref" >&2
        return 1
    fi
    normalize "$ref"
}

# A full SHA in either case becomes lowercase; anything else is left as written.
normalize() {
    if [[ "$1" =~ ^[0-9a-fA-F]{40}$ ]]; then
        printf '%s\n' "$1" | tr 'A-F' 'a-f'
    else
        printf '%s\n' "$1"
    fi
}

if [ "${1:-}" = "--configured" ]; then
    configured_ref
    exit
fi

# --github-output: resolve .github/daf-ref and hand the result to the workflow
# here, so a workflow's resolve step is one fixed line with nothing that could
# substitute another SHA.
github_output=0
if [ "${1:-}" = "--github-output" ]; then
    if [ -z "${GITHUB_OUTPUT:-}" ]; then
        echo "resolve_daf_ref: --github-output needs GITHUB_OUTPUT (run it in a GitHub Actions step)" >&2
        exit 1
    fi
    github_output=1
    shift
fi
emit() {
    if [ "$github_output" = 1 ]; then
        echo "sha=$1" >> "$GITHUB_OUTPUT"
        echo "DAF $ref -> $1"
        if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
            echo "Building with dusk-audio/DAF \`$1\` (.github/daf-ref: $ref)" >> "$GITHUB_STEP_SUMMARY"
        fi
    else
        printf '%s\n' "$1"
    fi
    exit 0
}

if [ -n "${1:-}" ]; then
    ref="$1"
elif [ -n "${DAF_REF:-}" ]; then
    ref="$DAF_REF"
else
    ref="$(configured_ref)"
fi

ref="$(normalize "$ref")"

# Network calls get three attempts: the whole CI run depends on this one call,
# and a transient GitHub error should not fail it before any build starts.
retry() {
    local attempt
    for attempt in 1 2 3; do
        "$@" && return 0
        [ "$attempt" = 3 ] || sleep "${DAF_RETRY_DELAY:-5}"
    done
    return 1
}

if [[ "$ref" =~ ^[0-9a-f]{40}$ ]]; then
    # The commit must be on a DAF branch, not merely fetchable: GitHub serves
    # commits from any fork in the same network through this URL, so a SHA that
    # was never merged into dusk-audio/DAF could otherwise be built. Fetch every
    # branch's commits (no trees or blobs, so it stays small) and ask which
    # branch contains it.
    probe="$(mktemp -d)"
    trap 'rm -rf "$probe"' EXIT
    git init -q "$probe"
    if ! retry git -C "$probe" fetch -q --filter=tree:0 "$DAF_URL" '+refs/heads/*:refs/remotes/daf/*' 2>/dev/null; then
        echo "resolve_daf_ref: could not reach $DAF_URL to check commit $ref" >&2
        exit 1
    fi
    if ! git -C "$probe" cat-file -e "$ref^{commit}" 2>/dev/null \
       || [ -z "$(git -C "$probe" for-each-ref --contains "$ref" refs/remotes/daf/ 2>/dev/null)" ]; then
        echo "resolve_daf_ref: commit $ref is not on any branch of $DAF_URL (unpushed, only on a fork or pull request, or mistyped)" >&2
        exit 1
    fi
    emit "$ref"
fi

ls_remote() { out="$(git ls-remote "$DAF_URL" "refs/heads/$ref")"; }
if ! retry ls_remote; then
    echo "resolve_daf_ref: could not reach $DAF_URL to resolve branch '$ref'" >&2
    exit 1
fi
sha="$(printf '%s\n' "$out" | cut -f1)"
if ! [[ "$sha" =~ ^[0-9a-f]{40}$ ]]; then
    echo "resolve_daf_ref: branch '$ref' does not exist on $DAF_URL (or names several refs)" >&2
    exit 1
fi
emit "$sha"
