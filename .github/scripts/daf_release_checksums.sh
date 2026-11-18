#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Hash published basenames and reject missing, extra or duplicate release assets.
set -euo pipefail
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <asset-base> <plugin-base> <dist-directory>" >&2
    exit 2
fi
asset_base=$1
plugin_base=$2
dist_dir=$3
test -d "$dist_dir"
expected_assets=("${asset_base}-linux.zip" "${asset_base}-linux-arm64.zip" "${asset_base}-macos.zip" "${asset_base}-windows.zip")
if [ "$plugin_base" = "chord-analyzer-2" ]; then
    expected_assets+=("chord-analyzer-headless-linux.zip" "chord-analyzer-headless-linux-arm64.zip")
fi
: > "$dist_dir/SHA256SUMS"
find "$dist_dir" -type f -name '*.zip' -print0 | sort -z |
while IFS= read -r -d '' archive; do
    archive_base=$(basename "$archive")
    known=false
    for expected in "${expected_assets[@]}"; do
        if [ "$archive_base" = "$expected" ]; then known=true; break; fi
    done
    if [ "$known" != true ]; then
        printf 'Unexpected release ZIP: %s\n' "$archive_base" >&2
        exit 1
    fi
    (cd "$(dirname "$archive")" && shasum -a 256 "$archive_base")
done >> "$dist_dir/SHA256SUMS"
# A total count cannot distinguish a complete release from an unrelated or
# duplicate ZIP hiding a missing desktop format/headless architecture.
diff -u <(printf '%s\n' "${expected_assets[@]}" | sort) \
        <(awk '{print $2}' "$dist_dir/SHA256SUMS" | sort)
