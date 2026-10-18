#!/usr/bin/env bash
# Two-week (EARLY_ACCESS_DAYS) early access for plugin releases.
#
# A release that qualifies is published to this repo as a DRAFT (invisible to
# the public) and mirrored to the private EARLY_ACCESS_REPO, which the Patreon
# build gate (dusk-audio.github.io/builds-gate) serves to members. The daily
# early-access-publish workflow flips the draft public on its date and merges the
# website branch /release-plugin left for it.
#
# A prerelease tag (X.Y.Z-alpha1, -beta, -rc2, ...) is members-only instead: it
# gets no public release at all, only the mirror, and has no public date. The
# gate lists it while it is that plugin's newest mirrored release; the stable
# release that follows goes through the normal window above.
#
#   early_access.sh plan TAG               -> GITHUB_OUTPUT early=true|false, members_only=true|false,
#                                             public_on=YYYY-MM-DD (empty when members-only)
#   early_access.sh notes BODY_FILE        -> append the Patreon section (+ public-on marker if early)
#   early_access.sh mirror TAG BODY FILES… -> create the release in EARLY_ACCESS_REPO
#   early_access.sh publish-due [TAG]      -> publish due drafts (or TAG now), merge website branches
#
# Env: EARLY_ACCESS_DAYS (repo variable; unset/0 = feature off), EARLY_ACCESS_REPO,
#      HAS_EARLY_TOKEN, PUBLIC_ON, EARLY, MEMBERS_ONLY, GH_TOKEN, WEBSITE_TOKEN,
#      GITHUB_REPOSITORY.
set -euo pipefail

MARKER_RE='<!-- early-access public-on=([0-9]{4}-[0-9]{2}-[0-9]{2}) -->'
MEMBERS_ONLY_MARKER='<!-- early-access members-only -->'
STABLE_TAG_RE='^[a-z0-9-]+-v[0-9]+\.[0-9]+\.[0-9]+$'
PRERELEASE_TAG_RE='^[a-z0-9-]+-v[0-9]+\.[0-9]+\.[0-9]+-[A-Za-z0-9]+(\.[A-Za-z0-9]+)*$'
WEBSITE_REPO="dusk-audio/dusk-audio.github.io"

out() { if [ -n "${GITHUB_OUTPUT:-}" ]; then echo "$1" >> "$GITHUB_OUTPUT"; fi; echo "$1"; }

# Public date = the tag's creation day (UTC) + EARLY_ACCESS_DAYS, so CI and
# /release-plugin compute the same day whenever each of them runs.
public_on_for_tag() {
  local tag="$1" days="$2" ts
  ts=$(git for-each-ref "refs/tags/$tag" --format='%(taggerdate:unix)')
  [ -n "$ts" ] || ts=$(git log -1 --format=%ct "$tag")
  date -u -d "@$((ts + days * 86400))" +%F
}

cmd_plan() {
  local tag="$1" days="${EARLY_ACCESS_DAYS:-0}" reason="" members_only=false
  [[ "$days" =~ ^[0-9]+$ ]] || days=0
  if [[ "$tag" =~ $PRERELEASE_TAG_RE ]]; then members_only=true; fi
  if [ "$days" -eq 0 ]; then
    reason="EARLY_ACCESS_DAYS is not set"
  elif [[ ! "$tag" =~ $STABLE_TAG_RE ]] && [ "$members_only" != true ]; then
    reason="$tag is not a single-plugin release tag"
  elif git tag -l --format='%(contents)' "$tag" | grep -qiE '^Early-Access:[[:space:]]*no[[:space:]]*$'; then
    reason="tag message opts out (Early-Access: no)"
  elif [ "${HAS_EARLY_TOKEN:-false}" != "true" ]; then
    echo "::warning::EARLY_ACCESS_DAYS=$days but the EARLY_ACCESS_TOKEN secret is missing; releasing publicly."
    reason="EARLY_ACCESS_TOKEN secret is missing"
  fi
  if [ -n "$reason" ]; then
    echo "Public release now: $reason."
    out "early=false"
    out "members_only=false"
    out "public_on="
    return
  fi
  if [ "$members_only" = true ]; then
    echo "Members-only prerelease: no public release until a stable tag."
    out "early=true"
    out "members_only=true"
    out "public_on="
    return
  fi
  local when
  when=$(public_on_for_tag "$tag" "$days")
  echo "Early access: members now, public on $when."
  out "early=true"
  out "members_only=false"
  out "public_on=$when"
}

cmd_notes() {
  local body="$1"
  {
    echo ""
    echo "### Stay in the loop"
    echo ""
    local perks="official Dusk Studio builds"
    if [ "${EARLY:-false}" = "true" ] && [[ "${EARLY_ACCESS_DAYS:-0}" =~ ^[1-9][0-9]*$ ]]; then
      perks="new releases ${EARLY_ACCESS_DAYS} days early and official Dusk Studio builds"
    fi
    echo "Dusk Audio plugins are free and open source. [Follow Dusk Audio free on Patreon](https://www.patreon.com/cw/DuskAudio) to hear about new releases, or [become a member](https://www.patreon.com/cw/DuskAudio/membership) to get ${perks}."
    if [ "${MEMBERS_ONLY:-false}" = "true" ]; then
      echo ""
      echo "$MEMBERS_ONLY_MARKER"
    elif [ "${EARLY:-false}" = "true" ]; then
      echo ""
      echo "<!-- early-access public-on=${PUBLIC_ON:?PUBLIC_ON is not set} -->"
    fi
  } >> "$body"
}

cmd_mirror() {
  local tag="$1" body="$2"; shift 2
  : "${EARLY_ACCESS_REPO:?EARLY_ACCESS_REPO is not set}"
  local notes pre=()
  notes=$(mktemp)
  {
    # builds-gate keys on these first lines: "Free for everyone on <date>" lists a
    # release until that date, "Members-only prerelease" lists it while it is the
    # plugin's newest.
    if [ "${MEMBERS_ONLY:-false}" = "true" ]; then
      echo "**Members-only prerelease for Dusk Audio Patreon members.** Not public until the stable release."
      pre=(--prerelease)
    else
      echo "**Early access for Dusk Audio Patreon members.** Free for everyone on ${PUBLIC_ON:?PUBLIC_ON is not set}."
    fi
    echo ""
    grep -vE "$MARKER_RE|$MEMBERS_ONLY_MARKER" "$body"
  } > "$notes"
  # A re-run updates the existing mirror in place, so a failed upload never
  # leaves members without the build they already had.
  if gh release view "$tag" -R "$EARLY_ACCESS_REPO" >/dev/null 2>&1; then
    gh release upload "$tag" "$@" -R "$EARLY_ACCESS_REPO" --clobber
    gh release edit "$tag" -R "$EARLY_ACCESS_REPO" --notes-file "$notes"
  else
    gh release create "$tag" "$@" -R "$EARLY_ACCESS_REPO" --title "$tag" --notes-file "$notes" "${pre[@]}"
  fi
}

cmd_publish_due() {
  local only="${1:-}" today repo="${GITHUB_REPOSITORY:?}"
  today=$(date -u +%F)
  local published=0 early_dates=""
  while IFS=$'\t' read -r tag body_b64; do
    [ -n "$tag" ] || continue
    local body when
    body=$(printf '%s' "$body_b64" | base64 -d)
    [[ "$body" =~ $MARKER_RE ]] || continue
    when="${BASH_REMATCH[1]}"
    if [ -n "$only" ]; then
      [ "$tag" = "$only" ] || continue
    elif [[ "$when" > "$today" ]]; then
      echo "Not yet: $tag (public on $when)"
      continue
    fi
    echo "Publishing $tag (public on $when)"
    gh release edit "$tag" -R "$repo" --draft=false >/dev/null
    published=$((published + 1))
    early_dates="$early_dates $when"
  done < <(gh api "repos/$repo/releases" --paginate \
             --jq '.[] | select(.draft) | [.tag_name, (.body // "" | @base64)] | @tsv')
  echo "Published $published draft release(s)."
  if [ -n "$only" ] && [ "$published" -eq 0 ]; then
    echo "::error::$only is not an early-access draft release; nothing was published."
    exit 1
  fi
  merge_website_branches "$today" "$early_dates" "$only"
}

# /release-plugin pushes its website commit to early-access/<public date> instead
# of main. A scheduled run merges every branch whose date has arrived. A manual
# run for one tag merges only that release's branch: other due branches wait for
# the scheduled run that publishes their drafts, so the site never links to a
# release that is still a draft.
merge_website_branches() {
  local today="$1" published_dates="$2" only="${3:-}" branch when
  if [ -z "${WEBSITE_TOKEN:-}" ]; then
    echo "::warning::WEBSITE_TOKEN is not set; website branches were not merged."
    return
  fi
  for branch in $(GH_TOKEN="$WEBSITE_TOKEN" gh api "repos/$WEBSITE_REPO/branches" --paginate \
                    --jq '.[].name | select(startswith("early-access/"))'); do
    when="${branch#early-access/}"
    if [ -n "$only" ]; then
      [[ " $published_dates " == *" $when "* ]] || continue
    elif [[ "$when" > "$today" ]]; then
      continue
    fi
    echo "Merging website branch $branch"
    GH_TOKEN="$WEBSITE_TOKEN" gh api "repos/$WEBSITE_REPO/merges" \
      -f base=main -f head="$branch" -f commit_message="Publish early-access release ($branch)" >/dev/null \
      || { echo "::error::Could not merge $branch into the website; merge it by hand."; exit 1; }
    GH_TOKEN="$WEBSITE_TOKEN" gh api -X DELETE "repos/$WEBSITE_REPO/git/refs/heads/$branch" >/dev/null
  done
}

case "${1:-}" in
  plan) shift; cmd_plan "$@" ;;
  notes) shift; cmd_notes "$@" ;;
  mirror) shift; cmd_mirror "$@" ;;
  publish-due) shift; cmd_publish_due "$@" ;;
  *) echo "usage: $0 plan|notes|mirror|publish-due ..." >&2; exit 2 ;;
esac
