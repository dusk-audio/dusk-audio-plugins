#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Local fixtures exercise origin, revision and in-tree provenance without network access.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
# Fail unless the suite reaches its end: after a fatal expansion error, bash 3.2
# (macOS /bin/bash) runs the EXIT trap with $? already 0, so without this a
# suite that died half way would exit 0 and read as a pass.
completed=0
trap 'rm -rf "$WORK"; [ "$completed" = 1 ] || exit 1' EXIT
mkdir -p "$WORK/harness/.github/scripts" "$WORK/harness/.github/workflows" "$WORK/harness/docker"
cp "$REPO_ROOT/.github/scripts/check_fork_sources.sh" "$WORK/harness/.github/scripts/"
cp "$REPO_ROOT/docker/check_daf_checkout.sh" "$WORK/harness/docker/"
cp "$REPO_ROOT/.github/scripts/resolve_daf_ref.sh" "$WORK/harness/.github/scripts/"
cp "$REPO_ROOT/.github/scripts/verify_daf_checkout.sh" "$WORK/harness/.github/scripts/"
cp "$REPO_ROOT/.github/workflows/"daf-*.yml "$WORK/harness/.github/workflows/"
FORK_GUARD="$WORK/harness/.github/scripts/check_fork_sources.sh"
CHECKOUT_GUARD="$WORK/harness/docker/check_daf_checkout.sh"
RESOLVER="$WORK/harness/.github/scripts/resolve_daf_ref.sh"
VERIFIER="$WORK/harness/.github/scripts/verify_daf_checkout.sh"
REF_FILE="$WORK/harness/.github/daf-ref"
# Run with a clean slate whatever the caller exported: CI and developers may set
# DAF_REF or DAF_URL for other scripts, and neither may change these results.
unset DAF_REF DAF_URL
# The fixtures exercise the resolver as a local tool; in CI (where this suite
# also runs) it refuses DAF_REF/DAF_URL, which is tested explicitly below.
unset GITHUB_ACTIONS
# CI runs this suite as a step: the resolver fixtures below must never write to
# that step's real output or job summary, which record the DAF commit built.
unset GITHUB_OUTPUT GITHUB_STEP_SUMMARY
# git exports GIT_DIR to hooks; it would redirect every fixture repository.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR
# A developer's global config (e.g. an insteadOf rewrite of git@github.com: to
# https) must not send the offline fixtures to the network.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
# The resolver retries network calls; the fixtures' failures are deliberate.
export DAF_RETRY_DELAY=0
# The fixture has no reachable origin: compare with the fetched ref directly.
export DAF_SKIP_FETCH=1
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
check_same() {
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s (%s != %s)\n' "$1" "$2" "$3"
        failures=$((failures + 1))
    fi
}
set_ref() { printf '# fixture\n%s\n' "$1" > "$REF_FILE"; }

# Print the ref of every dusk-audio/DAF checkout in the given workflow files, one
# line each, or "MISSING ref". Comments, quotes and case are normalised, the
# one-line flow form `with: { repository: ..., ref: ... }` is handled, and a
# checkout with no ref (even as a file's last step) is reported.
daf_checkout_refs() {
    awk '{
        line = $0; sub(/#.*/, "", line); gsub(/["\047]/, "", line); low = tolower(line)
        if (low ~ /repository:[[:space:]]*dusk-audio\/daf(\.git)?[[:space:]]*([,}]|$)/) {
            if (want) print "MISSING ref"
            if (low ~ /ref:/) { print line; want = 0 } else { want = 1 }
            next
        }
        if (want && low ~ /^[[:space:]]*ref:/) { print line; want = 0; next }
        if (want && low ~ /^[[:space:]]*-[[:space:]]/) { print "MISSING ref"; want = 0 }
    } END { if (want) print "MISSING ref" }' "$@"
}
# A ref is acceptable only as exactly the resolved job output, nothing around it
# (no "inputs.x || ...", no literal).
literal_refs() {
    daf_checkout_refs "$@" | tr -s ' ' \
        | grep -vxE ' ?ref: \$\{\{ (needs\.(plan|setup)\.outputs\.daf_sha|steps\.daf\.outputs\.sha) \}\}' || true
}
# Print every workflow file that hands a daf_sha job output to its jobs but
# does not resolve DAF exactly once: more than one id: daf step, or the resolve
# step's output used anywhere but that daf_sha: line. A build job that resolved
# DAF again would take DAF main as of its own start, so a merge during the run
# could put two DAF commits into one release.
resolve_once_violations() {
    awk 'function report() { if (outputs && (ids != 1 || direct)) print file }
    FNR == 1 { if (NR > 1) report(); file = FILENAME; outputs = 0; ids = 0; direct = 0 }
    {
        line = $0; sub(/#.*/, "", line); gsub(/["\047]/, "", line); low = tolower(line)
        # Case-sensitive, like the daf_sha output check below: the DAF_SHA env
        # var the release jobs set is not a job output.
        if (line ~ /^[[:space:]]*daf_sha:/) outputs = 1
        else if (low ~ /steps\.daf\.outputs\.sha/) direct = 1
        if (low ~ /^[[:space:]]*(-[[:space:]]+)?id:[[:space:]]*daf[[:space:]]*$/) ids++
    } END { if (NR > 0) report() }' "$@"
}
# Print file:line of every dusk-audio/DAF checkout step that is not IMMEDIATELY
# followed by an active verify step: a `run:` calling verify_daf_checkout.sh
# with no `if:` and no `continue-on-error:`. Comments do not count.
unverified_checkouts() {
    awk 'FNR == 1 { n++; kind[n] = "boundary" }
    {
        line = $0; sub(/#.*/, "", line); gsub(/["\047]/, "", line); low = tolower(line)
        if (low ~ /^[[:space:]]*-[[:space:]]+(name|uses|run|id|if|shell|with|env|working-directory|continue-on-error|timeout-minutes):/) {
            n++; kind[n] = ""; bad[n] = 0; where[n] = FILENAME ":" FNR
        } else if (low ~ /^[[:space:]]*steps:[[:space:]]*$/) {
            n++; kind[n] = "boundary"
        }
        if (low ~ /repository:[[:space:]]*dusk-audio\/daf(\.git)?[[:space:]]*([,}]|$)/) kind[n] = "checkout"
        if (kind[n] == "checkout" && low ~ /^[[:space:]]*ref:/) {
            expr = low; sub(/^[[:space:]]*ref:[[:space:]]*/, "", expr); sub(/[[:space:]]+$/, "", expr); ref[n] = expr
        }
        if (low ~ /verify_daf_checkout\.sh/ && kind[n] != "checkout") {
            kind[n] = "verify"
            # The run: value must be exactly the script and one ${{ }} argument
            # on the run: line itself: nothing before it (no "true ||"),
            # nothing after it (no "|| true"), and no multi-line block.
            if (low ~ /^[[:space:]]*(-[[:space:]]+)?run:[[:space:]]*\.\/\.github\/scripts\/verify_daf_checkout\.sh[[:space:]]+daf[[:space:]]+\$\{\{[^}]*\}\}[[:space:]]*$/) {
                arg = low; sub(/^.*verify_daf_checkout\.sh[[:space:]]+daf[[:space:]]+/, "", arg); sub(/[[:space:]]+$/, "", arg)
                vexpr[n] = arg
            } else {
                bad[n] = 1
            }
        }
        # Explicit shell: bash only; a missing key means pwsh on Windows runners.
        if (low ~ /^[[:space:]]*(-[[:space:]]+)?shell:[[:space:]]*bash[[:space:]]*$/) shellok[n] = 1
        if (low ~ /^[[:space:]]*(-[[:space:]]+)?(if|continue-on-error):/) bad[n] = 1
    } END {
        for (i = 1; i <= n; i++)
            if (kind[i] == "checkout" && !(i < n && kind[i + 1] == "verify" && !bad[i + 1] && shellok[i + 1] && vexpr[i + 1] == ref[i]))
                print where[i]
    }' "$@"
}
# The verify step must pass the SAME expression as the checkout's ref, as its
# only argument: a verify step that compares against something else (or ends in
# "|| true") would pass while checking nothing.
# Name the fixture job each line belongs to (so fixtures can grow without
# renumbering expectations).
fixture_jobs() {
    local file="$1" line
    while IFS= read -r line; do
        awk -v L="$line" 'NR <= L && /^  [A-Za-z0-9_-]+:[[:space:]]*$/ { job = $1 } NR == L { sub(/:$/, "", job); print job; exit }' "$file"
    done | tr '\n' ' '
}
# What these scanners cannot see: a checkout whose repository is built from an
# expression (${{ ... }}/DAF), a folded (>-) value, or a `git clone` in a run
# step. A `ref:` written before `repository:` reads as MISSING ref: fails safe.

cat > "$WORK/fixture-workflow.yml" <<'YAML'
steps:
  - uses: actions/checkout@v4
    with:
      repository: 'Dusk-Audio/DAF'   # quoted, mixed case, commented
      ref: main
  - uses: actions/checkout@v4
    with: { repository: dusk-audio/DAF, ref: v1 }
  - uses: actions/checkout@v4
    with:
      repository: "dusk-audio/DAF"
      ref: ${{ needs.plan.outputs.daf_sha }}
  - uses: actions/checkout@v4
    with:
      repository: dusk-audio/DAF
YAML
fixture="$(daf_checkout_refs "$WORK/fixture-workflow.yml" | tr -s ' ' | tr '\n' '|')"
check_same 'workflow scanner finds quoted, flow and ref-less checkouts' "$fixture" \
    ' ref: main| with: { repository: dusk-audio/DAF, ref: v1 }| ref: ${{ needs.plan.outputs.daf_sha }}|MISSING ref|'
check_same 'workflow scanner accepts only the resolved output' "$(literal_refs "$WORK/fixture-workflow.yml" | wc -l | tr -d ' ')" 3

cat > "$WORK/fixture-verify.yml" <<'YAML'
jobs:
  good:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - name: Verify DAF revision
        shell: bash
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
  commented:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      # checked by verify_daf_checkout.sh DAF elsewhere
      - name: Build
        run: make
  disabled:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - name: Verify DAF revision
        shell: bash
        if: false
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
  last:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
  next-job:
    steps:
      - shell: bash
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
  or-true:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - shell: bash
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}" || true
  self-compare:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - shell: bash
        run: ./.github/scripts/verify_daf_checkout.sh DAF "$(git -C DAF rev-parse HEAD)"
  other-expr:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - shell: bash
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ steps.other.outputs.sha }}"
  prefix:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - shell: bash
        run: true || ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
  block:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - shell: bash
        run: |
          trap 'exit 0' EXIT
          ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
  odd-shell:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - shell: bash {0}
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
  no-shell:
    steps:
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ needs.plan.outputs.daf_sha }}
      - name: Verify DAF revision
        run: ./.github/scripts/verify_daf_checkout.sh DAF "${{ needs.plan.outputs.daf_sha }}"
YAML
check_same 'verify scanner flags every job whose verify step is missing, disabled or neutralised' \
    "$(unverified_checkouts "$WORK/fixture-verify.yml" | sed 's/.*://' | fixture_jobs "$WORK/fixture-verify.yml")" \
    'commented disabled last or-true self-compare other-expr prefix block odd-shell no-shell '
cat > "$WORK/fixture-refs.yml" <<'YAML'
steps:
  - uses: actions/checkout@v4
    with:
      repository: dusk-audio/DAF
      ref: ${{ inputs.daf_sha || needs.plan.outputs.daf_sha }}
YAML
check_same 'workflow scanner rejects a ref that merely contains the resolved output' \
    "$(literal_refs "$WORK/fixture-refs.yml" | wc -l | tr -d ' ')" 1

# Every dusk-audio/DAF checkout in the REAL workflows must take its ref from the
# resolved job output, never a literal: one hard-coded ref is how a stale pin
# (or a second knob that disagrees with .github/daf-ref) creeps back in. And
# each must be followed by a verification step, so an empty output cannot
# silently check out DAF's default branch.
workflows=()
for wf in "$REPO_ROOT"/.github/workflows/*.yml "$REPO_ROOT"/.github/workflows/*.yaml; do
    [ -f "$wf" ] && workflows+=("$wf")
done
checkouts="$(daf_checkout_refs "${workflows[@]}" | wc -l | tr -d ' ')"
literal="$(literal_refs "${workflows[@]}" | tr -s ' ' | tr '\n' '|')"
unverified="$(unverified_checkouts "${workflows[@]}" | tr '\n' ' ')"
check_same 'every workflow DAF checkout uses the resolved ref' "${literal:-none}" none
check_same "workflows check DAF out ($checkouts checkouts found)" "$([ "$checkouts" -gt 0 ] && echo some || echo none)" some
check_same 'every workflow DAF checkout is followed by an active verify step' "${unverified:-none}" none
# No workflow may set DAF_REF or DAF_URL: that was the old pin's shape, and the
# resolver refuses both in CI. Every daf_sha job output must be the resolve
# step's output, and every step with id: daf must run the resolver.
daf_env="$(cat "${workflows[@]}" | sed 's/#.*//' | grep -cE '^[[:space:]]*DAF_(REF|URL)[[:space:]]*:' || true)"
check_same 'no workflow sets DAF_REF or DAF_URL' "$daf_env" 0
bad_outputs="$(cat "${workflows[@]}" | sed 's/#.*//' | grep -E '^[[:space:]]*daf_sha:' \
    | grep -vcE '^[[:space:]]*daf_sha:[[:space:]]*\$\{\{ steps\.daf\.outputs\.sha \}\}[[:space:]]*$' || true)"
check_same 'every daf_sha job output is the resolve step output' "$bad_outputs" 0
# Every step with id: daf (however written; at least one) must be exactly the
# one-line `run: ./.github/scripts/resolve_daf_ref.sh --github-output`, so the
# SHA it outputs can only come from .github/daf-ref.
# Prints: steps with id: daf, how many are that exact line, how many are not.
resolve_step_counts() {
    awk '{
        line = $0; sub(/#.*/, "", line); gsub(/["\047]/, "", line); low = tolower(line)
        stepstart = (low ~ /^[[:space:]]*-[[:space:]]+[a-z-]+:/)
        # A bare key indented less than a step key (8) ends the step. No regex
        # interval: mawk 1.3.4, the Ubuntu 22.04 awk, reads {8,} literally.
        match(low, /^[[:space:]]*/); shallow = (RLENGTH < 8)
        if (want && (stepstart || low ~ /^[[:space:]]*[a-z_-]+:[[:space:]]*$/ && shallow)) {
            if (runs == 1 && exact == 1) ok++; else bad++
            want = 0
        }
        if (low ~ /^[[:space:]]*(-[[:space:]]+)?id:[[:space:]]*daf[[:space:]]*$/) { ids++; want = 1; runs = 0; exact = 0 }
        if (want && low ~ /^[[:space:]]*(-[[:space:]]+)?run:/) {
            runs++
            if (low ~ /^[[:space:]]*(-[[:space:]]+)?run:[[:space:]]*\.\/\.github\/scripts\/resolve_daf_ref\.sh[[:space:]]+--github-output[[:space:]]*$/) exact = 1
        }
    } END { if (want) { if (runs == 1 && exact == 1) ok++; else bad++ } print ids + 0, ok + 0, bad + 0 }' "$@"
}
read -r daf_ids daf_ok daf_bad <<< "$(resolve_step_counts "${workflows[@]}")"
check_same "every id: daf step is exactly the resolver's --github-output line ($daf_ids found)" \
    "$([ "$daf_ids" -gt 0 ] && [ "$daf_bad" = 0 ] && [ "$daf_ok" = "$daf_ids" ] && echo yes || echo no)" yes
# A step-level env: or with: before run: belongs to the step; a job key does not.
cat > "$WORK/fixture-stepenv.yml" <<'YAML'
jobs:
  plan:
    steps:
      - name: Resolve DAF revision
        id: daf
        env:
          DAF_RETRY_DELAY: 10
        run: ./.github/scripts/resolve_daf_ref.sh --github-output
  other:
    steps:
      - id: daf
    outputs:
      x: y
YAML
check_same 'resolve step scanner keeps a step env: in the step and ends it at a job key' \
    "$(resolve_step_counts "$WORK/fixture-stepenv.yml")" '2 1 1'
once="$(resolve_once_violations "${workflows[@]}" | tr '\n' ' ')"
check_same 'workflows with a daf_sha output resolve DAF exactly once' "${once:-none}" none
# The mistake it catches: a build job given its own resolve step (copied from
# daf-au-test.yml, a single job), and a checkout of the plan job's step output.
cat > "$WORK/fixture-reresolve.yml" <<'YAML'
jobs:
  plan:
    outputs:
      daf_sha: ${{ steps.daf.outputs.sha }}
    steps:
      - id: daf
        run: ./.github/scripts/resolve_daf_ref.sh --github-output
  build:
    steps:
      - id: daf
        run: ./.github/scripts/resolve_daf_ref.sh --github-output
YAML
cat > "$WORK/fixture-direct.yml" <<'YAML'
jobs:
  plan:
    outputs:
      daf_sha: ${{ steps.daf.outputs.sha }}
    steps:
      - id: daf
        run: ./.github/scripts/resolve_daf_ref.sh --github-output
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ steps.daf.outputs.sha }}
YAML
# daf-au-test.yml's shape (one job checking out its own resolve output) plus a
# DAF_SHA env var: not a daf_sha job output, so exempt.
cat > "$WORK/fixture-envsha.yml" <<'YAML'
jobs:
  test:
    steps:
      - id: daf
        run: ./.github/scripts/resolve_daf_ref.sh --github-output
      - uses: actions/checkout@v4
        with:
          repository: dusk-audio/DAF
          ref: ${{ steps.daf.outputs.sha }}
      - run: ./build.sh
        env:
          DAF_SHA: ${{ steps.daf.outputs.sha }}
YAML
check_same 'resolve-once scanner flags a second resolve step and a direct output use' \
    "$(resolve_once_violations "$WORK/fixture-reresolve.yml" "$WORK/fixture-direct.yml" "$WORK/fixture-envsha.yml" "$REPO_ROOT/.github/workflows/daf-au-test.yml" | sed 's|.*/||' | tr '\n' ' ')" \
    'fixture-reresolve.yml fixture-direct.yml '
check 'resolver refuses a ref argument in CI' 1 'argument' env GITHUB_ACTIONS=true "$RESOLVER" 2f3a685885007a03e8c3e94d82c3b52653bd9a77
printf '      repository: Dusk-Audio/DAF.git\n      ref: main\n' > "$WORK/fixture-dotgit.yml"
check_same 'workflow scanner sees a repository written with .git' "$(literal_refs "$WORK/fixture-dotgit.yml" | wc -l | tr -d ' ')" 1
check 'resolver refuses DAF_REF in CI' 1 'set only in .github/daf-ref' env GITHUB_ACTIONS=true DAF_REF=main "$RESOLVER"
check 'resolver refuses DAF_URL in CI' 1 'set only in .github/daf-ref' env GITHUB_ACTIONS=true DAF_URL=file:///x "$RESOLVER"

# Windows runners check out with core.autocrlf=true; the scripts CI runs there
# only work if .gitattributes keeps them LF.
eol_attrs="$(git -C "$REPO_ROOT" check-attr eol -- .github/scripts/verify_daf_checkout.sh .github/scripts/resolve_daf_ref.sh .github/daf-ref | sed 's/.*: //' | tr '\n' ' ')"
check_same '.gitattributes keeps the CI scripts and daf-ref LF on Windows' "$eol_attrs" 'lf lf lf '

mkdir -p "$WORK/daf/dgl/src/pugl-upstream" "$WORK/daf/widgets/imgui"
git_q -C "$WORK/daf" init -q
printf 'pugl\n' > "$WORK/daf/dgl/src/pugl-upstream/source.c"
printf 'widgets\n' > "$WORK/daf/widgets/imgui/DearImGui.hpp"
git_q -C "$WORK/daf" add .
git_q -C "$WORK/daf" commit -qm 'vendor framework trees'
git_q -C "$WORK/daf" remote add origin git@github.com:dusk-audio/DAF.git
pin="$(git_q -C "$WORK/daf" rev-parse HEAD)"
# What CI builds: origin/main, as if just fetched.
git_q -C "$WORK/daf" update-ref refs/remotes/origin/main "$pin"
set_ref main

check 'fork guard accepts in-tree components' 0 'OK' "$FORK_GUARD" "$WORK/daf"
check 'checkout guard accepts origin/main' 0 'OK' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
check 'checkout guard says a skipped fetch was not refreshed' 0 'not refreshed' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
check 'checkout guard ignores a caller GIT_DIR' 0 'OK' env GIT_DIR="$WORK/nonexistent" DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
check 'checkout guard ignores DAF_REF in the environment' 0 'OK' env DAF_REF=0000000000000000000000000000000000000000 DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
check 'checkout guard reports a failed fetch as UNVERIFIED' 1 'UNVERIFIED' env -u DAF_SKIP_FETCH GIT_ALLOW_PROTOCOL=file DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"

check 'resolver reads .github/daf-ref' 0 '^main$' "$RESOLVER" --configured
check 'resolver --configured ignores DAF_REF' 0 '^main$' env DAF_REF=other "$RESOLVER" --configured
git_q clone -q --bare "$WORK/daf" "$WORK/daf-remote.git"
check 'resolver passes a full SHA that is on the remote' 0 "$pin" env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" "$pin"
check 'resolver rejects a SHA that is not on the remote' 1 'is not on' env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" 1111111111111111111111111111111111111111
# A commit the remote has, but only under a non-branch ref (like a fork's PR head).
git_q -C "$WORK/daf" commit -q --allow-empty -m 'never merged'
offbranch="$(git_q -C "$WORK/daf" rev-parse HEAD)"
git_q -C "$WORK/daf" push -q "$WORK/daf-remote.git" "$offbranch:refs/pull/1/head"
git_q -C "$WORK/daf" reset -q --hard "$pin"
check 'resolver rejects a SHA that is on no DAF branch' 1 'is not on any branch' env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" "$offbranch"

# A caller's GIT_DIR must not redirect the resolver's probe into its repository.
env GIT_DIR="$WORK/daf/.git" DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" "$pin" >/dev/null 2>&1 || true
check_same 'resolver ignores a caller GIT_DIR' "$(git_q -C "$WORK/daf" for-each-ref refs/remotes/daf | wc -l | tr -d ' ')" 0

check 'verifier rejects an empty expected SHA' 1 'expected a resolved' "$VERIFIER" "$WORK/daf" ''
check 'verifier rejects an uppercase SHA' 1 'expected a resolved' "$VERIFIER" "$WORK/daf" "$(printf '%s' "$pin" | tr 'a-f' 'A-F')"
check 'verifier rejects a different commit' 1 'but this run resolved' "$VERIFIER" "$WORK/daf" 1111111111111111111111111111111111111111
check 'verifier accepts the resolved commit' 0 "is $pin" "$VERIFIER" "$WORK/daf" "$pin"

# The guard's live-fetch paths, offline: origin keeps its github.com URL (so the
# fork check passes) while a stand-in for ssh serves the local bare repository.
git_q -C "$WORK/daf-remote.git" config uploadpack.allowAnySHA1InWant true
git_q -C "$WORK/daf-remote.git" config uploadpack.allowFilter true
cat > "$WORK/fake-ssh" <<'SSH'
#!/bin/sh
exec "$(git --exec-path)/git-upload-pack" "$FAKE_DAF_REMOTE"
SSH
chmod +x "$WORK/fake-ssh"
git_q clone -q "$WORK/daf-remote.git" "$WORK/daf-ssh"
git_q -C "$WORK/daf-ssh" remote set-url origin git@github.com:dusk-audio/DAF.git
# Every git call on that checkout goes through the stand-in; none may reach
# a real ssh (or GitHub with the developer's key).
fake_ssh_env=(GIT_SSH_COMMAND="$WORK/fake-ssh" GIT_SSH_VARIANT=simple FAKE_DAF_REMOTE="$WORK/daf-remote.git")
ssh_guard() {
    env -u DAF_SKIP_FETCH "${fake_ssh_env[@]}" DAF_PATH="$WORK/daf-ssh" "$CHECKOUT_GUARD" "$@"
}
set_ref main
check 'checkout guard fetches origin/main and matches it' 0 'matches what CI builds' ssh_guard
set_ref "$pin"
check 'checkout guard accepts a held-back SHA on an origin branch' 0 'matches what CI builds' ssh_guard
env "${fake_ssh_env[@]}" git -C "$WORK/daf-ssh" fetch -q origin "$offbranch"
git_q -C "$WORK/daf-ssh" checkout -q --detach "$offbranch"
set_ref "$offbranch"
git_q -C "$WORK/daf-ssh" checkout -q --detach "$pin"
check 'checkout guard --fix will not move onto a rejected commit' 1 'CI will refuse' ssh_guard --fix
check_same 'checkout guard --fix left HEAD alone for a rejected commit' "$(git_q -C "$WORK/daf-ssh" rev-parse HEAD)" "$pin"
git_q -C "$WORK/daf-ssh" checkout -q --detach "$offbranch"
check 'checkout guard rejects a held-back SHA on no origin branch' 1 'CI will refuse' ssh_guard
set_ref main
check 'resolver lowercases an uppercase SHA' 0 "$pin" env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" "$(printf '%s' "$pin" | tr 'a-f' 'A-F')"
check 'resolver resolves the configured branch' 0 "$pin" env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER"
gh_out="$WORK/github-output"; : > "$gh_out"
gh_summary="$WORK/github-summary"; : > "$gh_summary"
env GITHUB_OUTPUT="$gh_out" GITHUB_STEP_SUMMARY="$gh_summary" DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" --github-output >/dev/null
check_same 'resolver --github-output writes the resolved SHA' "$(cat "$gh_out")" "sha=$pin"
check_same 'resolver --github-output records the SHA in the job summary' "$(cat "$gh_summary")" "Building with dusk-audio/DAF \`$pin\` (.github/daf-ref: main)"
: > "$gh_out"
check 'resolver refuses a ref after --github-output in CI' 1 'at most one argument' env GITHUB_ACTIONS=true GITHUB_OUTPUT="$gh_out" "$RESOLVER" --github-output 2f3a685885007a03e8c3e94d82c3b52653bd9a77
check_same 'resolver wrote no output for a refused ref' "$(cat "$gh_out")" ''
check 'resolver --github-output needs GITHUB_OUTPUT' 1 'needs GITHUB_OUTPUT' env -u GITHUB_OUTPUT "$RESOLVER" --github-output
check 'resolver lets DAF_REF override the file' 0 "$pin" env DAF_REF="$pin" DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER"
printf 'main   # comment\r\n' > "$REF_FILE"
check 'resolver strips comments and CR from .github/daf-ref' 0 '^main$' "$RESOLVER" --configured
set_ref main
check 'resolver rejects an unknown branch' 1 "branch 'nope' does not exist" env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" nope
check 'resolver names the branch when origin is unreachable' 1 "to resolve branch 'main'" env GIT_ALLOW_PROTOCOL=file "$RESOLVER"
check 'resolver rejects an abbreviated SHA' 1 'does not exist' env DAF_URL="file://$WORK/daf-remote.git" "$RESOLVER" "${pin:0:12}"

git_q -C "$WORK/daf" worktree add -q --detach "$WORK/worktree" "$pin"
check 'checkout guard accepts a git worktree' 0 'OK' env DAF_PATH="$WORK/worktree" "$CHECKOUT_GUARD"
check 'fork guard accepts a git worktree' 0 'OK' "$FORK_GUARD" "$WORK/worktree"

for remote in https://github.com/DISTRHO/DPF.git \
              https://github.com/not-dusk-audio/DAF.git \
              https://gitlab.com/dusk-audio/DAF.git \
              https://github.com/attacker/mirror-dusk-audio/DAF.git; do
    git_q -C "$WORK/daf" remote set-url origin "$remote"
    check "fork guard rejects $remote" 1 'checkout compiles from' "$FORK_GUARD" "$WORK/daf"
    check "checkout guard rejects $remote" 1 'REMOTE' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
done

# A foreign origin whose branch is ahead: --fix must not move onto it.
git_q -C "$WORK/daf" commit -q --allow-empty -m 'foreign commit'
foreign="$(git_q -C "$WORK/daf" rev-parse HEAD)"
git_q -C "$WORK/daf" reset -q --hard "$pin"
git_q -C "$WORK/daf" update-ref refs/remotes/origin/main "$foreign"
check 'checkout guard --fix refuses a foreign origin' 1 'nothing was fetched or changed' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD" --fix
check_same 'checkout guard --fix left HEAD alone under a foreign origin' "$(git_q -C "$WORK/daf" rev-parse HEAD)" "$pin"
git_q -C "$WORK/daf" update-ref refs/remotes/origin/main "$pin"
git_q -C "$WORK/daf" remote set-url origin https://github.com/dusk-audio/DAF.git

printf 'changed\n' >> "$WORK/daf/widgets/imgui/DearImGui.hpp"
check 'fork guard rejects modified vendored source' 1 'differs from' "$FORK_GUARD" "$WORK/daf"
check 'checkout guard rejects dirty source' 1 'DIRTY' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
git_q -C "$WORK/daf" commit -qam 'change widgets'
ahead="$(git_q -C "$WORK/daf" rev-parse HEAD)"
check 'checkout guard rejects unpushed local commits' 1 'is ahead of main' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
check 'checkout guard --fix leaves unpushed commits alone' 1 'not fixing' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD" --fix
git_q -C "$WORK/daf" update-ref refs/remotes/origin/main "$ahead"
git_q -C "$WORK/daf" reset -q --hard "$pin"
check 'checkout guard rejects a stale checkout' 1 'is behind main' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
printf 'local edit\n' > "$WORK/daf/untracked.txt"
check 'checkout guard --fix will not carry uncommitted changes' 1 'uncommitted changes would be carried' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD" --fix
check_same 'checkout guard --fix left a dirty checkout where it was' "$(git_q -C "$WORK/daf" rev-parse HEAD)" "$pin"
rm "$WORK/daf/untracked.txt"
check 'checkout guard --fix fast-forwards a clean checkout' 1 'fast-forwarded' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD" --fix
check 'checkout guard accepts the fast-forwarded checkout' 0 'OK' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"

set_ref "$pin"
check 'checkout guard holds to a SHA in .github/daf-ref' 1 'is ahead of' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
check 'checkout guard tells how to reach a held-back SHA' 1 'checkout '"$pin" env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD" --fix
check 'checkout guard confirms a held-back SHA with origin' 1 'UNVERIFIED' env -u DAF_SKIP_FETCH GIT_ALLOW_PROTOCOL=file DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
git_q -C "$WORK/daf" reset -q --hard "$pin"
check 'checkout guard accepts the held-back SHA, saying it was not checked' 0 'not checked against origin' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
set_ref "$ahead"
check 'checkout guard --fix will not move onto a held-back SHA it did not check' 1 'not checked against origin' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD" --fix
check_same 'checkout guard --fix left HEAD alone for an unchecked held-back SHA' "$(git_q -C "$WORK/daf" rev-parse HEAD)" "$pin"
set_ref "$pin"
git_q -C "$WORK/daf" checkout -q --orphan unrelated
git_q -C "$WORK/daf" commit -q -m 'unrelated history'
check 'checkout guard reports an unrelated history' 1 'is unrelated to' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
git_q -C "$WORK/daf" checkout -q -f main
git_q -C "$WORK/daf" reset -q --hard "$pin"
set_ref 1111111111111111111111111111111111111111
check 'checkout guard names a held-back SHA it does not have' 1 'UNKNOWN' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
rm "$REF_FILE"
check 'checkout guard fails loudly without .github/daf-ref' 1 'daf-ref is missing' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
printf '# only a comment\n' > "$REF_FILE"
check 'checkout guard fails loudly on an empty .github/daf-ref' 1 'names no ref' env DAF_PATH="$WORK/daf" "$CHECKOUT_GUARD"
git_q -C "$WORK/daf" update-ref refs/remotes/origin/main "$pin"
set_ref main

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
completed=1
echo 'all provenance guard tests passed'
