#!/usr/bin/env bash
# Download a plugin's user manual PDF into a release archive's staging directory.
#
#   fetch_manual_pdf.sh SLUG OUT_FILE [REF]
#
# REF is the website branch that holds the release's site change (early_access.sh website-ref). A dated
# early-access release names early-access/<public date>, where /release-plugin left the regenerated PDF; website
# main still has the previous version's until that date. A failed download from REF falls back to main with a
# warning. A missing manual is never fatal: the archive ships without one and the step warns, so a new plugin
# with no published manual does not block its release.
set -uo pipefail
slug="$1" out="$2" ref="${3:-main}"
base="https://raw.githubusercontent.com/dusk-audio/dusk-audio.github.io"
[[ "$ref" =~ ^[A-Za-z0-9][A-Za-z0-9._/-]*$ ]] || ref=main
for r in "$ref" main; do
  url="$base/$r/assets/manuals/$slug-manual.pdf"
  if curl -fL --silent --show-error "$url" -o "$out"; then
    echo "bundled $slug-manual.pdf from website $r"
    exit 0
  fi
  [ "$r" = main ] && break
  echo "::warning::No manual PDF on website branch $r; falling back to main, which may hold the previous version's."
done
rm -f "$out"
echo "::warning::User manual PDF not found at $url (non-fatal)"
