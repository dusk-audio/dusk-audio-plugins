#!/usr/bin/env bash
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#
# check_daf_plugin_names.sh — enforce the DAF compiled-artefact naming standard.
#
# The name passed to daf_add_plugin() becomes the filename of every artefact the
# plugin ships: <name>.vst3, <name>.clap, <name>.lv2/<name>.so, <name>.component.
# Users see these in their plugin folders, so they must read like the rest of the
# release: lowercase, hyphen separated, matching the slug the plugin is tagged,
# packaged and documented under.
#
# Rules:
#   1. No underscores. Hyphens only, lowercase alphanumerics between them.
#   2. The name must equal the plugin's release slug, so the download folder,
#      the git tag, the manual filename and the binary all read the same.
#
# Run from anywhere; exits non-zero and names every offender.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# plugin CMakeLists -> the release slug it must use as its artefact name.
# Add a row here when a new DAF plugin lands; a plugin with no row is reported
# rather than silently skipped, so the standard cannot be sidestepped by
# forgetting this table.
expected_slug_for() {
    case "$1" in
        plugins/4k-eq/daf-plugin/CMakeLists.txt)           echo "4k-eq-2" ;;
        plugins/TapeMachine/daf-plugin/CMakeLists.txt)     echo "tapemachine-2" ;;
        plugins/multi-comp/daf-plugin/CMakeLists.txt)      echo "multi-comp-2" ;;
        plugins/multi-q/daf-plugin/CMakeLists.txt)         echo "multi-q-2" ;;
        plugins/tape-echo/daf-plugin/CMakeLists.txt)       echo "tape-echo-2" ;;
        plugins/sunset-circuits/daf-plugin/CMakeLists.txt) echo "sunset-circuits" ;;
        *) echo "" ;;
    esac
}

status=0
found=0

while IFS= read -r cmakelists; do
    name=$(sed -nE 's/^[[:space:]]*daf_add_plugin\(([A-Za-z0-9_.+-]+).*/\1/p' "$cmakelists" | head -1)
    [ -n "$name" ] || continue
    found=$((found + 1))

    # Rule 1: naming shape.
    if ! printf '%s' "$name" | grep -qE '^[a-z0-9]+(-[a-z0-9]+)*$'; then
        echo "ERROR: $cmakelists: daf_add_plugin($name) is not lowercase-hyphen form."
        echo "       Compiled artefacts would ship as '$name.vst3' / '$name.clap'."
        status=1
    fi

    # Rule 2: name matches the release slug.
    expected=$(expected_slug_for "$cmakelists")
    if [ -z "$expected" ]; then
        echo "ERROR: $cmakelists: no expected slug recorded in $0."
        echo "       Add a row to expected_slug_for() naming this plugin's release slug."
        status=1
    elif [ "$name" != "$expected" ]; then
        echo "ERROR: $cmakelists: daf_add_plugin($name) != release slug '$expected'."
        echo "       Artefacts, tag, zip and manual must all use '$expected'."
        status=1
    fi
done < <(git ls-files 'plugins/*/daf-plugin/CMakeLists.txt')

if [ "$found" -eq 0 ]; then
    echo "ERROR: found no daf_add_plugin() declarations; the glob above is stale."
    exit 1
fi

if [ "$status" -eq 0 ]; then
    echo "DAF artefact names OK ($found plugins, lowercase-hyphen, matching release slugs)"
fi
exit "$status"
