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
  "release view")
    if [[ "$*" == *"--json body"* ]]; then
      printf '%s\n' "${FAKE_VIEW_BODY:-}"
      exit "${FAKE_VIEW_EXIT:-0}"
    fi
    # FAKE_VIEW_DRAFT stands for what `--json isDraft --jq .isDraft` prints for a release that exists.
    if [ -n "${FAKE_VIEW_DRAFT:-}" ]; then echo "$FAKE_VIEW_DRAFT"; exit 0; fi
    exit "${FAKE_VIEW_EXIT:-1}" ;;
  "release create"|"release edit")
    while [ $# -gt 0 ]; do
      if [ "$1" = "--notes-file" ]; then { echo "--- notes"; cat "$2"; echo "--- end"; } >> "$GH_LOG"; fi
      shift
    done ;;
  "api repos/dusk-audio/dusk-audio.github.io/branches")
    printf '%s\n' "${FAKE_WEBSITE_BRANCHES-main}"
    exit "${FAKE_WEBSITE_BRANCHES_EXIT:-0}" ;;
  "api repos/"*)
    # The published-release count already_released asks for, or the publish-due listing.
    if [[ "$*" == *'| length'* ]]; then
      [ "${FAKE_API_EXIT:-0}" -eq 0 ] || exit "$FAKE_API_EXIT"
      echo "${FAKE_PUBLISHED:-0}"
    else
      printf '%s' "${FAKE_RELEASES:-}"
    fi ;;
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

# --- early access only for a plugin's first public release ---
rplan() { # rplan PUBLISHED_COUNT TAG -> key=value lines, with GitHub reachable
  ( export GITHUB_REPOSITORY=o/plugins FAKE_PUBLISHED="$1" FAKE_API_EXIT="${FAKE_API_EXIT:-0}"; plan "$2" )
}
check "an update to a released plugin goes public now" \
  "early=false members_only=false public_on=" "$(rplan 22 multi-comp-2-v1.0.0 | oneline)"
check "a plugin's first release stays early" \
  "early=true members_only=false public_on=2026-10-31" "$(rplan 0 multi-comp-2-v1.0.0 | oneline)"
check "a prerelease of a released plugin is public" \
  "early=false members_only=false public_on=" "$(rplan 3 multi-comp-2-v1.1.0-alpha1 | oneline)"
check "a prerelease of an unreleased plugin stays members-only" \
  "early=true members_only=true public_on=" "$(rplan 0 multi-comp-2-v1.1.0-alpha1 | oneline)"
check "an unreachable GitHub keeps the first-release plan" \
  "early=true members_only=false public_on=2026-10-31" "$(FAKE_API_EXIT=1 rplan 0 multi-comp-2-v1.0.0 | oneline)"
: > "$GH_LOG"; rplan 0 multi-comp-2-v1.0.0 > /dev/null
contains "the query counts the tag's own plugin" 'startswith("multi-comp-2-v")' "$(cat "$GH_LOG")"
check "a plugin name ends before its version" "multi-comp-2" "$(bash -c "source <(sed -n '/^plugin_of()/p' '$SCRIPT'); plugin_of multi-comp-2-v1.0.0-alpha1")"

# --- website-ref ---
wref() { # wref TAG -> the ref line
  GITHUB_OUTPUT="" GITHUB_REF="" EARLY_ACCESS_DAYS="${DAYS-14}" HAS_EARLY_TOKEN="${TOKEN-true}" \
    bash "$SCRIPT" website-ref "$1"
}
check "early-access release reads its dated website branch" "ref=early-access/2026-10-31" "$(wref multi-comp-2-v1.0.0)"
check "members-only prerelease reads website main" "ref=main" "$(wref multi-comp-2-v1.1.0-alpha1)"
check "Early-Access: no reads website main" "ref=main" "$(wref duskverb-2-v1.1.0-beta)"
check "feature off reads website main" "ref=main" "$(DAYS=0 wref multi-comp-2-v1.0.0)"
check "missing token reads website main" "ref=main" "$(TOKEN=false wref multi-comp-2-v1.0.0)"
check "all-plugins tag reads website main" "ref=main" "$(wref v2.0.0)"
check "an unknown tag reads website main" "ref=main" "$(wref no-such-v9.9.9)"
check "the tag defaults to GITHUB_REF" "ref=early-access/2026-10-31" \
  "$(GITHUB_OUTPUT="" GITHUB_REF=refs/tags/multi-comp-2-v1.0.0 EARLY_ACCESS_DAYS=14 HAS_EARLY_TOKEN=true bash "$SCRIPT" website-ref)"
check "an update's website change is on main" "ref=main" \
  "$( export GITHUB_REPOSITORY=o/plugins FAKE_PUBLISHED=5; wref multi-comp-2-v1.0.0 )"
check "a branch build reads website main" "ref=main" \
  "$(GITHUB_OUTPUT="" GITHUB_REF=refs/heads/main EARLY_ACCESS_DAYS=14 HAS_EARLY_TOKEN=true bash "$SCRIPT" website-ref)"
# Publication precedes the website merge; only a removed branch permits main.
wref_on() { # wref_on DRAFT TAG: as wref, with the release on GitHub (DRAFT true/false, or "" for none yet)
  FAKE_VIEW_DRAFT="$1" GITHUB_REPOSITORY=dusk-audio/dusk-audio-plugins GITHUB_OUTPUT="" GITHUB_REF="" \
    EARLY_ACCESS_DAYS=14 HAS_EARLY_TOKEN=true bash "$SCRIPT" website-ref "$2" 2>/dev/null
}
check "a published release with its website branch removed reads main" "ref=main" "$(wref_on false multi-comp-2-v1.0.0)"
check "a published release with an unmerged website branch keeps that branch" "ref=early-access/2026-10-31" \
  "$(FAKE_WEBSITE_BRANCHES=$'main\nearly-access/2026-10-31' wref_on false multi-comp-2-v1.0.0)"
check "an unrelated website branch does not prevent main" "ref=main" \
  "$(FAKE_WEBSITE_BRANCHES=$'main\nearly-access/2026-10-31-extra' wref_on false multi-comp-2-v1.0.0)"
check "a failed website query preserves the dated branch" "ref=early-access/2026-10-31" \
  "$(FAKE_WEBSITE_BRANCHES='' FAKE_WEBSITE_BRANCHES_EXIT=1 wref_on false multi-comp-2-v1.0.0)"
check "a failed paginated query cannot prove the branch was removed" "ref=early-access/2026-10-31" \
  "$(FAKE_WEBSITE_BRANCHES=main FAKE_WEBSITE_BRANCHES_EXIT=1 wref_on false multi-comp-2-v1.0.0)"
check "a draft release reads its dated branch" "ref=early-access/2026-10-31" "$(wref_on true multi-comp-2-v1.0.0)"
check "a first release becoming public keeps its unmerged website branch" "ref=early-access/2026-10-31" \
  "$(FAKE_PUBLISHED=1 FAKE_VIEW_BODY='<!-- early-access public-on=2026-10-31 -->' \
     FAKE_WEBSITE_BRANCHES=$'main\nearly-access/2026-10-31' wref_on false multi-comp-2-v1.0.0)"
check "a published release keeps its recorded date when the window changes" "ref=early-access/2026-11-07" \
  "$(FAKE_PUBLISHED=1 FAKE_VIEW_BODY='<!-- early-access public-on=2026-11-07 -->' \
     FAKE_WEBSITE_BRANCHES=$'main\nearly-access/2026-11-07' wref_on false multi-comp-2-v1.0.0)"
check "a published first release uses main after its recorded branch is removed" "ref=main" \
  "$(FAKE_PUBLISHED=1 FAKE_VIEW_BODY='<!-- early-access public-on=2026-10-31 -->' wref_on false multi-comp-2-v1.0.0)"
check "a failed branch query preserves a published release's recorded date" "ref=early-access/2026-10-31" \
  "$(FAKE_PUBLISHED=1 FAKE_VIEW_BODY='<!-- early-access public-on=2026-10-31 -->' \
     FAKE_WEBSITE_BRANCHES_EXIT=1 wref_on false multi-comp-2-v1.0.0)"
check "a public update without an early-access marker uses main" "ref=main" \
  "$(FAKE_PUBLISHED=1 FAKE_VIEW_BODY='Public update' wref_on false multi-comp-2-v1.0.0)"
check "an unavailable release body keeps the planned website branch" "ref=early-access/2026-10-31" \
  "$(FAKE_VIEW_BODY='<!-- early-access public-on=2026-11-07 -->' FAKE_VIEW_EXIT=1 wref_on true multi-comp-2-v1.0.0)"
check "a release not created yet reads its dated branch" "ref=early-access/2026-10-31" "$(wref_on "" multi-comp-2-v1.0.0)"
# actions/checkout leaves a lightweight tag behind; its commit here is a week older than the tag.
# website-ref fetches the annotated tag from origin again and dates the release by it.
git init -q --bare "$WORK/origin.git"
git init -q "$WORK/clone"
(
  cd "$WORK/clone"
  GIT_COMMITTER_DATE="2026-10-10T12:00:00Z" git -c user.name=t -c user.email=t@t commit -q --allow-empty -m old
  GIT_COMMITTER_DATE="2026-10-17T12:00:00Z" git -c user.name=t -c user.email=t@t tag -a ring-out-v1.2.0 -m "Ring Out 1.2.0"
  git remote add origin "$WORK/origin.git"
  git push -q origin HEAD:refs/heads/main refs/tags/ring-out-v1.2.0
  git tag -d ring-out-v1.2.0 >/dev/null
  git tag ring-out-v1.2.0 HEAD
)
check "a lightweight checkout tag is dated by the annotated tag" "ref=early-access/2026-10-31" \
  "$(cd "$WORK/clone" && GITHUB_OUTPUT="" GITHUB_REF="" EARLY_ACCESS_DAYS=14 HAS_EARLY_TOKEN=true \
       bash "$SCRIPT" website-ref ring-out-v1.2.0 2>/dev/null)"

# --- fetch_manual_pdf.sh, against a fake curl that serves only $FAKE_URLS ---
FETCH="$ROOT/.github/scripts/fetch_manual_pdf.sh"
cat > "$WORK/bin/curl" <<'EOF'
#!/usr/bin/env bash
url="" out=""
while [ $# -gt 0 ]; do case "$1" in -o) out="$2"; shift ;; http*) url="$1" ;; esac; shift; done
echo "$url" >> "$CURL_LOG"
if [ -n "$FAKE_URLS" ] && grep -qxF "$url" <<< "$FAKE_URLS"; then echo pdf > "$out"; exit 0; fi
exit 22
EOF
chmod +x "$WORK/bin/curl"
export CURL_LOG="$WORK/curl.log"
site=https://raw.githubusercontent.com/dusk-audio/dusk-audio.github.io
fm() { : > "$CURL_LOG"; FAKE_URLS="$1" bash "$FETCH" x "$WORK/x-manual.pdf" "$2" 2>&1; }
out=$(fm "$site/early-access/2026-10-31/assets/manuals/x-manual.pdf" early-access/2026-10-31)
contains "manual comes from the early-access branch" "from website early-access/2026-10-31" "$out"
check "main is not asked when the branch has the manual" "1" "$(wc -l < "$CURL_LOG" | tr -d ' ')"
out=$(fm "$site/main/assets/manuals/x-manual.pdf" early-access/2026-10-31)
contains "a missing branch falls back to main" "from website main" "$out"
contains "the fallback warns" "::warning::No manual PDF on website branch early-access/2026-10-31" "$out"
out=$(fm "" main)
contains "no manual anywhere warns" "::warning::User manual PDF not found" "$out"
check "no manual anywhere leaves no file" "absent" "$([ -e "$WORK/x-manual.pdf" ] && echo present || echo absent)"
out=$(fm "$site/main/assets/manuals/x-manual.pdf" 'odd ref;x')
contains "an odd ref reads main" "from website main" "$out"

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
