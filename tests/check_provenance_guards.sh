#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#
# check_provenance_guards.sh — tests for the two framework-provenance guards.
#
# docker/check_daf_pins.sh and .github/scripts/check_fork_sources.sh decide
# whether a build tree is made of the source we think it is. Both used to answer
# that from configuration alone: workflow fields and .gitmodules. Configuration
# describes the next fetch, not the tree in front of you, so a checkout created
# before a fork was repointed kept its old origin and both guards approved it
# (issue #243).
#
# These fixtures are throwaway git repositories, so the cases below can be built
# exactly: a correct checkout, one on the wrong remote, one at the wrong
# revision, and the case that motivated the issue -- a .gitmodules that names our
# fork sitting next to a checkout that does not come from it.
#
# No network: remotes are set with `git remote add` and never fetched.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIN_GUARD="$REPO_ROOT/docker/check_daf_pins.sh"
FORK_GUARD="$REPO_ROOT/.github/scripts/check_fork_sources.sh"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

failures=0

pass() { printf '  ok    %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; failures=$((failures + 1)); }

git_q() { git -c user.email=t@t -c user.name=t -c init.defaultBranch=main -c commit.gpgsign=false "$@"; }

# A minimal repository with one commit and a named origin.
make_repo() {
    local dir="$1" origin="$2"
    mkdir -p "$dir"
    git_q -C "$dir" init -q
    echo seed > "$dir/file.txt"
    git_q -C "$dir" add file.txt
    git_q -C "$dir" commit -qm seed
    git_q -C "$dir" remote add origin "$origin"
}

# A DAF-shaped tree: .gitmodules for pugl, a nested pugl checkout, and a gitlink
# recorded at whichever pugl commit is asked for. Built with update-index rather
# than `git submodule add` so the recorded revision can disagree with the
# checkout, which is the whole point of two of the cases.
#   $1 dir  $2 DAF origin  $3 pugl origin  $4 gitmodules url  $5 pin=head|stale
make_daf() {
    local dir="$1" dafOrigin="$2" puglOrigin="$3" modulesUrl="$4" pinMode="$5"
    make_repo "$dir" "$dafOrigin"

    mkdir -p "$dir/dgl/src"
    local pugl="$dir/dgl/src/pugl-upstream"
    make_repo "$pugl" "$puglOrigin"
    local puglHead stalePin
    puglHead="$(git_q -C "$pugl" rev-parse HEAD)"
    echo second > "$pugl/file.txt"
    git_q -C "$pugl" commit -qam second
    stalePin="$puglHead"                       # the first commit, now behind
    puglHead="$(git_q -C "$pugl" rev-parse HEAD)"

    cat > "$dir/.gitmodules" <<EOF
[submodule "dgl/src/pugl-upstream"]
	path = dgl/src/pugl-upstream
	url = $modulesUrl
EOF

    local recorded="$puglHead"
    [ "$pinMode" = stale ] && recorded="$stalePin"
    git_q -C "$dir" update-index --add --cacheinfo "160000,$recorded,dgl/src/pugl-upstream"
    git_q -C "$dir" add .gitmodules
    git_q -C "$dir" commit -qm "record pugl"
}

# The pin guard reports on DAF and DAF-Widgets too, and those rows will always
# read DRIFT against fixtures, since the fixture SHAs are not the CI pins. Only
# the pugl lines are under test here, so assert on those.
pin_guard_says() {
    local dafPath="$1" expected="$2" label="$3"
    local out
    out="$(DAF_PATH="$dafPath" DAFWIDGETS_PATH="$dafPath" "$PIN_GUARD" 2>&1 || true)"
    if printf '%s' "$out" | grep -q "$expected"; then
        pass "$label"
    else
        fail "$label -- expected a line matching: $expected"
        printf '%s\n' "$out" | sed 's/^/        /'
    fi
}

fork_guard_exits() {
    local dafPath="$1" widgetsPath="$2" wantFail="$3" label="$4" expected="${5:-}"
    local out status=0
    out="$("$FORK_GUARD" "$dafPath" "$widgetsPath" 2>&1)" || status=$?

    if [ "$wantFail" = yes ] && [ "$status" -eq 0 ]; then
        fail "$label -- guard passed, expected failure"
        printf '%s\n' "$out" | sed 's/^/        /'
        return
    fi
    if [ "$wantFail" = no ] && [ "$status" -ne 0 ]; then
        fail "$label -- guard failed, expected pass"
        printf '%s\n' "$out" | sed 's/^/        /'
        return
    fi
    if [ -n "$expected" ] && ! printf '%s' "$out" | grep -q "$expected"; then
        fail "$label -- expected a line matching: $expected"
        printf '%s\n' "$out" | sed 's/^/        /'
        return
    fi
    pass "$label"
}

echo "provenance guards"

# --- 1. a correct checkout is approved ------------------------------------
make_daf "$WORK/good" "git@github.com:dusk-audio/DAF.git" \
         "https://github.com/dusk-audio/pugl.git" \
         "https://github.com/dusk-audio/pugl.git" head
make_repo "$WORK/widgets-good" "https://github.com/dusk-audio/DAF-Widgets.git"

pin_guard_says "$WORK/good" "OK       pugl checkout:" "pin guard accepts a matching pugl checkout"
fork_guard_exits "$WORK/good" "$WORK/widgets-good" no "fork guard accepts checkouts from our forks"

# --- 2. wrong origin ------------------------------------------------------
make_daf "$WORK/badremote" "git@github.com:dusk-audio/DAF.git" \
         "https://github.com/DISTRHO/pugl.git" \
         "https://github.com/DISTRHO/pugl.git" head

pin_guard_says "$WORK/badremote" "REMOTE   pugl checkout:" "pin guard rejects a pugl checkout from upstream"
fork_guard_exits "$WORK/badremote" "$WORK/widgets-good" yes \
    "fork guard rejects a pugl checkout from upstream" "pugl checkout compiles from"

# --- 3. wrong revision ----------------------------------------------------
make_daf "$WORK/badsha" "git@github.com:dusk-audio/DAF.git" \
         "https://github.com/dusk-audio/pugl.git" \
         "https://github.com/dusk-audio/pugl.git" stale

pin_guard_says "$WORK/badsha" "DRIFT    pugl checkout:" "pin guard rejects a pugl checkout at the wrong revision"
fork_guard_exits "$WORK/badsha" "$WORK/widgets-good" yes \
    "fork guard rejects a pugl checkout at the wrong revision" "but DAF pins"

# --- 4. the case from #243: honest .gitmodules, dishonest checkout --------
make_daf "$WORK/misleading" "git@github.com:dusk-audio/DAF.git" \
         "https://github.com/DISTRHO/pugl.git" \
         "https://github.com/dusk-audio/pugl.git" head

pin_guard_says "$WORK/misleading" "OK       pugl .gitmodules:" \
    "pin guard still reads .gitmodules as declared"
pin_guard_says "$WORK/misleading" "REMOTE   pugl checkout:" \
    "pin guard rejects an upstream checkout that .gitmodules vouches for"
fork_guard_exits "$WORK/misleading" "$WORK/widgets-good" yes \
    "fork guard rejects an upstream checkout that .gitmodules vouches for" \
    "pugl checkout compiles from"

# --- 5. a DAF-Widgets checkout from upstream ------------------------------
make_repo "$WORK/widgets-bad" "https://github.com/DISTRHO/DPF-Widgets.git"
fork_guard_exits "$WORK/good" "$WORK/widgets-bad" yes \
    "fork guard rejects a DAF-Widgets checkout from upstream" "DAF-Widgets checkout compiles from"

# --- 6. no paths supplied: config-only scan still works -------------------
fork_guard_exits "" "" no "fork guard still scans config alone when given no paths"

echo
if [ "$failures" = 0 ]; then
    echo "all provenance guard tests passed"
else
    echo "$failures provenance guard test(s) failed"
    exit 1
fi
