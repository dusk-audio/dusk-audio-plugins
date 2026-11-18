#!/usr/bin/env bash
# Tests for .github/scripts/release_changelog.sh against throwaway local repos, so
# nothing touches GitHub. Run: ./tests/check_release_changelog.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/.github/scripts/release_changelog.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1

fail=0
pass=0
check() { # check NAME EXPECTED ACTUAL
  if [ "$2" = "$3" ]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); printf 'FAIL %s\n  expected: %q\n  actual:   %q\n' "$1" "$2" "$3"
  fi
}

# The release side: annotated tags as /release-plugin writes them.
git init -q "$WORK/origin"
cd "$WORK/origin"
git -c user.name=t -c user.email=t@t commit -q --allow-empty -m "Bump: the commit message, never the notes"
gtag() { git -c user.name=t -c user.email=t@t tag "$@"; }
gtag -a multi-comp-2-v1.0.0 --cleanup=verbatim -F - <<'EOF'
Multi-Comp 2 v1.0.0

Intro paragraph.

### Known issues

- First issue
- Second issue

Closing paragraph.
EOF
gtag -a duskverb-2-v1.1.0 --cleanup=verbatim -F - <<'EOF'
DuskVerb 2 v1.1.0

- One change

Early-Access: no
EOF
gtag -a title-only-v1.0.0 -m "Title only"
gtag light-v1.0.0

# The runner side: a clone whose tag refs are what actions/checkout leaves for
# the tag that triggered the run, a lightweight ref to the commit.
git clone -q "$WORK/origin" "$WORK/runner"
cd "$WORK/runner"
for t in multi-comp-2-v1.0.0 duskverb-2-v1.1.0; do git tag -f "$t" "$t^{commit}" >/dev/null; done
check "the fixture reproduces checkout's lightweight overwrite" \
  "commit" "$(git cat-file -t refs/tags/multi-comp-2-v1.0.0)"

run() { bash "$SCRIPT" "$1" 2>"$WORK/err"; }

expected=$(printf 'Intro paragraph.\n\n### Known issues\n\n- First issue\n- Second issue\n\nClosing paragraph.')
check "an overwritten tag's message comes back with its blank lines" "$expected" "$(run multi-comp-2-v1.0.0)"
check "the annotated tag is restored locally" "tag" "$(git cat-file -t refs/tags/multi-comp-2-v1.0.0)"
check "the Early-Access switch is not part of the notes" "- One change" "$(run duskverb-2-v1.1.0)"

if run title-only-v1.0.0 >/dev/null; then check "a title-only tag fails" "exit 1" "exit 0"
else check "a title-only tag fails" "1" "$(grep -c 'no changelog' "$WORK/err")"; fi
if run light-v1.0.0 >/dev/null; then check "a lightweight tag fails" "exit 1" "exit 0"
else check "a lightweight tag fails" "1" "$(grep -c 'lightweight' "$WORK/err")"; fi
if run missing-v1.0.0 >/dev/null; then check "a missing tag fails" "exit 1" "exit 0"
else check "a missing tag fails" "1" "$(grep -c 'Could not fetch' "$WORK/err")"; fi

echo "release_changelog: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
