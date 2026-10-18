#!/usr/bin/env bash
# Tests for .github/scripts/early_access.sh against a fake `gh` and a throwaway
# git repo, so nothing touches GitHub. Run: ./tests/check_early_access.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/.github/scripts/early_access.sh"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail=0
pass=0
check() { # check NAME EXPECTED ACTUAL
  if [ "$2" = "$3" ]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); printf 'FAIL %s\n  expected: %s\n  actual:   %s\n' "$1" "$2" "$3"
  fi
}
contains() { # contains NAME NEEDLE HAYSTACK
  if [[ "$3" == *"$2"* ]]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); printf 'FAIL %s\n  missing: %s\n' "$1" "$2"
  fi
}
lacks() { # lacks NAME NEEDLE HAYSTACK
  if [[ "$3" != *"$2"* ]]; then pass=$((pass + 1)); else
    fail=$((fail + 1)); printf 'FAIL %s\n  unexpected: %s\n' "$1" "$2"
  fi
}

# Fake gh: logs each call; `release create` also logs its notes file; `api` prints
# $FAKE_RELEASES (the TSV the real --jq filter would produce).
mkdir -p "$WORK/bin"
cat > "$WORK/bin/gh" <<'EOF'
#!/usr/bin/env bash
echo "gh $*" >> "$GH_LOG"
case "$1 $2" in
  "release view") exit "${FAKE_VIEW_EXIT:-1}" ;;
  "release create"|"release edit")
    while [ $# -gt 0 ]; do
      if [ "$1" = "--notes-file" ]; then { echo "--- notes"; cat "$2"; echo "--- end"; } >> "$GH_LOG"; fi
      shift
    done ;;
  "api repos/"*) printf '%s' "${FAKE_RELEASES:-}" ;;
esac
EOF
chmod +x "$WORK/bin/gh"
export PATH="$WORK/bin:$PATH" GH_LOG="$WORK/gh.log"

# A repo with one tag per case. Tag dates are fixed so public_on is predictable.
git init -q "$WORK/repo"
cd "$WORK/repo"
git -c user.name=t -c user.email=t@t commit -q --allow-empty -m init
tag() { # tag NAME MESSAGE
  GIT_COMMITTER_DATE="2026-10-17T12:00:00Z" git -c user.name=t -c user.email=t@t tag -a "$1" -m "$2"
}
tag multi-comp-2-v1.0.0 "Multi-Comp 2 1.0.0"
tag multi-comp-2-v1.1.0-alpha1 "Multi-Comp 2 alpha"
tag multi-comp-2-v1.1.0-rc.2 "Multi-Comp 2 rc"
tag duskverb-2-v1.1.0-beta "$(printf 'DuskVerb 2 beta\n\nEarly-Access: no')"
tag v2.0.0 "all plugins"
tag v2.0.0-beta "all plugins beta"

plan() { # plan TAG -> key=value lines
  GITHUB_OUTPUT="" EARLY_ACCESS_DAYS="${DAYS-14}" HAS_EARLY_TOKEN="${TOKEN-true}" \
    bash "$SCRIPT" plan "$1" | grep -E '^(early|members_only|public_on)='
}
oneline() { tr '\n' ' ' | sed 's/ $//'; }

# --- plan ---
check "stable tag is early with a date" \
  "early=true members_only=false public_on=2026-10-31" "$(plan multi-comp-2-v1.0.0 | oneline)"
check "alpha tag is members-only with no date" \
  "early=true members_only=true public_on=" "$(plan multi-comp-2-v1.1.0-alpha1 | oneline)"
check "dotted rc tag is members-only" \
  "early=true members_only=true public_on=" "$(plan multi-comp-2-v1.1.0-rc.2 | oneline)"
check "Early-Access: no makes a prerelease public" \
  "early=false members_only=false public_on=" "$(plan duskverb-2-v1.1.0-beta | oneline)"
check "feature off keeps prereleases public" \
  "early=false members_only=false public_on=" "$(DAYS=0 plan multi-comp-2-v1.1.0-alpha1 | oneline)"
check "missing token keeps prereleases public" \
  "early=false members_only=false public_on=" "$(TOKEN=false plan multi-comp-2-v1.1.0-alpha1 | oneline)"
check "all-plugins tag is never early" \
  "early=false members_only=false public_on=" "$(plan v2.0.0 | oneline)"
check "all-plugins prerelease is never early" \
  "early=false members_only=false public_on=" "$(plan v2.0.0-beta | oneline)"

# --- notes ---
printf 'Body\n' > "$WORK/b1"
EARLY=true MEMBERS_ONLY=true PUBLIC_ON="" EARLY_ACCESS_DAYS=14 bash "$SCRIPT" notes "$WORK/b1"
contains "members-only notes carry the members-only marker" "<!-- early-access members-only -->" "$(cat "$WORK/b1")"
lacks "members-only notes carry no public-on marker" "public-on=" "$(cat "$WORK/b1")"

printf 'Body\n' > "$WORK/b2"
EARLY=true MEMBERS_ONLY=false PUBLIC_ON=2026-10-31 EARLY_ACCESS_DAYS=14 bash "$SCRIPT" notes "$WORK/b2"
contains "early notes carry the public-on marker" "<!-- early-access public-on=2026-10-31 -->" "$(cat "$WORK/b2")"

printf 'Body\n' > "$WORK/b3"
EARLY=false MEMBERS_ONLY=false PUBLIC_ON="" EARLY_ACCESS_DAYS=14 bash "$SCRIPT" notes "$WORK/b3"
lacks "public notes carry no marker" "<!-- early-access" "$(cat "$WORK/b3")"

# --- mirror ---
: > "$GH_LOG"
EARLY_ACCESS_REPO=o/early MEMBERS_ONLY=true PUBLIC_ON="" \
  bash "$SCRIPT" mirror multi-comp-2-v1.1.0-alpha1 "$WORK/b1" a.zip
log=$(cat "$GH_LOG")
contains "members-only mirror is a GitHub prerelease" "--prerelease" "$(grep '^gh release create' "$GH_LOG")"
contains "members-only mirror header" "**Members-only prerelease for Dusk Audio Patreon members.**" "$log"
lacks "members-only mirror strips the marker" "<!-- early-access members-only -->" "$log"

: > "$GH_LOG"
EARLY_ACCESS_REPO=o/early MEMBERS_ONLY=false PUBLIC_ON=2026-10-31 \
  bash "$SCRIPT" mirror multi-comp-2-v1.0.0 "$WORK/b2" a.zip
log=$(cat "$GH_LOG")
lacks "early mirror is not a prerelease" "--prerelease" "$(grep '^gh release create' "$GH_LOG")"
contains "early mirror header names the date" "Free for everyone on 2026-10-31." "$log"
lacks "early mirror strips the public-on marker" "public-on=" "$log"

# --- publish-due ---
b64() { printf '%s' "$1" | base64 -w0; }
: > "$GH_LOG"
FAKE_RELEASES=$(printf '%s\t%s\n%s\t%s\n%s\t%s\n' \
  due-v1.0.0 "$(b64 'x <!-- early-access public-on=2000-01-01 -->')" \
  later-v1.0.0 "$(b64 'x <!-- early-access public-on=2999-01-01 -->')" \
  parked-v1.0.0 "$(b64 'a draft with no marker')") \
GITHUB_REPOSITORY=o/plugins bash "$SCRIPT" publish-due > "$WORK/pd.out" 2>&1
edits=$(grep '^gh release edit' "$GH_LOG" | oneline)
check "publish-due publishes only the due draft" "gh release edit due-v1.0.0 -R o/plugins --draft=false" "$edits"

echo "early_access: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
