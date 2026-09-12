#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Local fixtures exercise origin, revision and in-tree provenance without network access.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/harness/.github/scripts" "$WORK/harness/.github/workflows" "$WORK/harness/docker"
cp "$REPO_ROOT/.github/scripts/check_fork_sources.sh" "$WORK/harness/.github/scripts/"
cp "$REPO_ROOT/docker/check_daf_pins.sh" "$WORK/harness/docker/"
cp "$REPO_ROOT/.github/workflows/"daf-*.yml "$WORK/harness/.github/workflows/"
FORK_GUARD="$WORK/harness/.github/scripts/check_fork_sources.sh"
PIN_GUARD="$WORK/harness/docker/check_daf_pins.sh"
failures=0

git_q() { git -c user.email=t@t -c user.name=t -c init.defaultBranch=main -c commit.gpgsign=false "$@"; }
check() {
    local label="$1" expected_status="$2" expected_text="$3" status=0 out
    shift 3
    out="$("$@" 2>&1)" || status=$?
    if [ "$status" -eq "$expected_status" ] && printf '%s' "$out" | grep -q "$expected_text"; then
        printf '  ok    %s\n' "$label"
    else
        printf '  FAIL  %s (exit %s)\n%s\n' "$label" "$status" "$out"
        failures=$((failures + 1))
    fi
}

mkdir -p "$WORK/daf/dgl/src/pugl-upstream" "$WORK/daf/widgets/imgui"
git_q -C "$WORK/daf" init -q
printf 'pugl\n' > "$WORK/daf/dgl/src/pugl-upstream/source.c"
printf 'widgets\n' > "$WORK/daf/widgets/imgui/DearImGui.hpp"
git_q -C "$WORK/daf" add .
git_q -C "$WORK/daf" commit -qm 'vendor framework trees'
git_q -C "$WORK/daf" remote add origin git@github.com:dusk-audio/DAF.git
pin="$(git_q -C "$WORK/daf" rev-parse HEAD)"
printf 'env:\n  DAF_REF: %s\n' "$pin" > "$WORK/harness/.github/workflows/daf-build.yml"

check 'fork guard accepts in-tree components' 0 'OK' "$FORK_GUARD" "$WORK/daf"
check 'pin guard accepts the single revision' 0 'OK' env DAF_PATH="$WORK/daf" "$PIN_GUARD"
git_q -C "$WORK/daf" worktree add -q --detach "$WORK/worktree" "$pin"
check 'pin guard accepts a git worktree' 0 'OK' env DAF_PATH="$WORK/worktree" "$PIN_GUARD"
check 'fork guard accepts a git worktree' 0 'OK' "$FORK_GUARD" "$WORK/worktree"

for remote in https://github.com/DISTRHO/DPF.git \
              https://github.com/not-dusk-audio/DAF.git \
              https://gitlab.com/dusk-audio/DAF.git \
              https://github.com/attacker/mirror-dusk-audio/DAF.git; do
    git_q -C "$WORK/daf" remote set-url origin "$remote"
    check "fork guard rejects $remote" 1 'checkout compiles from' "$FORK_GUARD" "$WORK/daf"
    check "pin guard rejects $remote" 1 'REMOTE' env DAF_PATH="$WORK/daf" "$PIN_GUARD"
done
git_q -C "$WORK/daf" remote set-url origin https://github.com/dusk-audio/DAF.git

printf 'changed\n' >> "$WORK/daf/widgets/imgui/DearImGui.hpp"
check 'fork guard rejects modified vendored source' 1 'differs from' "$FORK_GUARD" "$WORK/daf"
check 'pin guard rejects dirty source' 1 'DIRTY' env DAF_PATH="$WORK/daf" "$PIN_GUARD"
git_q -C "$WORK/daf" commit -qam 'change widgets'
check 'pin guard rejects another revision' 1 'DRIFT' env DAF_PATH="$WORK/daf" "$PIN_GUARD"
git_q -C "$WORK/daf" reset -q --hard "$pin"

mkdir "$WORK/daf/widgets/.git"
check 'fork guard rejects a nested widget checkout' 1 'nested checkout' "$FORK_GUARD" "$WORK/daf"
rmdir "$WORK/daf/widgets/.git"
printf '[submodule "pugl"]\n' > "$WORK/daf/.gitmodules"
check 'fork guard rejects legacy submodule configuration' 1 'submodules' "$FORK_GUARD" "$WORK/daf"
rm "$WORK/daf/.gitmodules"
git_q -C "$WORK/daf" rm -qr dgl/src/pugl-upstream
git_q -C "$WORK/daf" update-index --add --cacheinfo "160000,$pin,dgl/src/pugl-upstream"
git_q -C "$WORK/daf" commit -qm 'legacy gitlink'
check 'fork guard rejects gitlinks instead of subtrees' 1 'tracked tree' "$FORK_GUARD" "$WORK/daf"

check 'config-only scan still passes' 0 'OK' "$FORK_GUARD"
printf 'repository: DISTRHO/DPF\n' >> "$WORK/harness/.github/workflows/daf-build.yml"
check 'config-only scan rejects forbidden source' 1 'fetches framework source' "$FORK_GUARD"

if [ "$failures" -ne 0 ]; then
    echo "$failures provenance guard test(s) failed"
    exit 1
fi
echo 'all provenance guard tests passed'
