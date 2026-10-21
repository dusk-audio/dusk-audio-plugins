#!/usr/bin/env bash
# Print the changelog a release's notes are built from: the annotated tag's
# message without its title line.
#
#   release_changelog.sh TAG
#
# actions/checkout fetches a tag ref as `+<commit>:refs/tags/<tag>`, which
# replaces the annotated tag with a lightweight one even after `fetch-tags:
# true`. `%(contents)` of a lightweight tag is the commit message, so every
# release used to go out without the changelog its tag carried. Fetch the
# annotated tag again, and refuse to continue without one: empty release notes
# are a defect, not a fallback.
#
# Blank lines inside the message are kept (a paragraph after a list must not
# merge into its last item); leading and trailing blank lines are dropped, and
# so is the `Early-Access: no` switch early_access.sh reads from the tag.
#
# Env: RELEASE_CHANGELOG_REMOTE (default origin).
set -euo pipefail

tag="${1:?usage: release_changelog.sh TAG}"
remote="${RELEASE_CHANGELOG_REMOTE:-origin}"

git fetch --quiet --force --no-tags "$remote" "refs/tags/$tag:refs/tags/$tag" \
  || { echo "::error::Could not fetch tag $tag from $remote" >&2; exit 1; }

if [ "$(git cat-file -t "refs/tags/$tag")" != tag ]; then
  echo "::error::$tag is a lightweight tag; release notes come from an annotated tag's message" >&2
  exit 1
fi

changelog=$(git tag -l --format='%(contents)' "$tag" \
  | tail -n +2 \
  | grep -viE '^Early-Access:[[:space:]]*no[[:space:]]*$' \
  | sed '/[^[:space:]]/,$!d' || true)

if [ -z "$changelog" ]; then
  echo "::error::$tag has no changelog below its title line" >&2
  exit 1
fi
printf '%s\n' "$changelog"
