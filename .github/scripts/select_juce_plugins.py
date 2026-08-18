#!/usr/bin/env python3
"""Pick the JUCE plugins a change actually needs built.

juce-compile-check.yml used to run its full twelve-plugin matrix for every
pull request, including one that touched a single plugin. That is not free:
each job re-runs checkout, apt and a full CMake configure of the whole tree
before compiling anything, and twelve simultaneous jobs queue against the
runner pool, so the wasted legs delay the ones that matter.

The blast-radius argument the workflow was built on still holds, but it only
applies to code that actually crosses trees. So:

  * a change under plugins/shared/, cmake/, the root CMakeLists or this
    workflow reaches every plugin -> build all of them, as before
  * a change confined to one plugin's own directory -> build that plugin

Emits GitHub Actions outputs:

  plugins  JSON array of Linux target names for the matrix
  windows  JSON array of Windows target names (the small fixed subset,
           intersected with the selection)
  reason   one line for the job summary
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

# Directory prefix -> the juce_add_plugin targets it produces. chord-analyzer
# declares two; spectrum-analyzer and groovemind name theirs through a CMake
# variable, so neither is greppable from the source and both are listed here by
# hand. Keep this in sync with the matrix in juce-compile-check.yml.
PLUGIN_DIRS: dict[str, list[str]] = {
    "plugins/4k-eq/": ["FourKEQ"],
    "plugins/multi-comp/": ["MultiComp"],
    "plugins/TapeMachine/": ["TapeMachine"],
    "plugins/tape-echo/": ["TapeEcho"],
    "plugins/multi-q/": ["MultiQ"],
    "plugins/convolution-reverb/": ["ConvolutionReverb"],
    "plugins/DuskVerb/": ["DuskVerb"],
    "plugins/DuskAmp/": ["DuskAmp"],
    "plugins/chord-analyzer/": ["ChordAnalyzer", "ChordAnalyzerMIDI"],
    "plugins/spectrum-analyzer/": ["SpectrumAnalyzer"],
    "plugins/groovemind/": ["GrooveMind"],
}

ALL_PLUGINS = [target for targets in PLUGIN_DIRS.values() for target in targets]

# The Windows leg deliberately covers four distinct shapes rather than the whole
# fleet; see the comment above compile-windows in the workflow.
WINDOWS_PLUGINS = ["FourKEQ", "MultiComp", "SpectrumAnalyzer", "ChordAnalyzer"]

# Paths that reach the whole fleet. plugins/shared/ alone is included by eleven
# plugins, and DuskFilters.hpp is compiled by the JUCE TapeMachine through the
# cross-tree include.
FLEET_WIDE = (
    "plugins/shared/",
    "plugins/shared-dpf/dsp/DuskFilters.hpp",
    "cmake/",
    "CMakeLists.txt",
    ".github/workflows/juce-compile-check.yml",
    ".github/scripts/select_juce_plugins.py",
    # Every Linux compile job runs this installer before building anything, so a
    # change to it is exercised by -- and must trigger -- the whole fleet.
    ".github/scripts/apt_install.sh",
)

# Subtrees inside a plugin directory that no JUCE target compiles. The workflow's
# own paths filter already subtracts these; repeated here so a DPF-only change
# that slips through the filter still selects nothing rather than everything.
NON_JUCE_SUBTREES = ("/dpf-plugin/", "/core/")


def changed_files(base_ref: str) -> list[str]:
    """Files this PR touches, or an empty list if the range cannot be resolved."""
    try:
        out = subprocess.run(
            ["git", "diff", "--name-only", f"origin/{base_ref}...HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout
    except subprocess.CalledProcessError as exc:
        print(f"::warning::could not diff against origin/{base_ref}: {exc}", file=sys.stderr)
        return []
    return [line for line in out.splitlines() if line]


def select(files: list[str]) -> tuple[list[str], str]:
    if not files:
        return ALL_PLUGINS, "could not determine changed files; building the full fleet"

    for path in files:
        if path.startswith(FLEET_WIDE):
            return ALL_PLUGINS, f"{path} reaches every plugin; building the full fleet"

    selected: list[str] = []
    for path in files:
        if any(sub in "/" + path for sub in NON_JUCE_SUBTREES):
            continue
        for prefix, targets in PLUGIN_DIRS.items():
            if path.startswith(prefix):
                selected += [t for t in targets if t not in selected]

    if not selected:
        return [], "no JUCE plugin sources changed"

    # Keep the matrix in the workflow's declared order for a stable check list.
    ordered = [t for t in ALL_PLUGINS if t in selected]
    return ordered, f"changed plugins: {', '.join(ordered)}"


def main() -> int:
    base_ref = os.environ.get("BASE_REF", "")
    event = os.environ.get("EVENT_NAME", "")

    if event != "pull_request" or not base_ref:
        plugins, reason = ALL_PLUGINS, f"event '{event or 'unknown'}' builds the full fleet"
    else:
        plugins, reason = select(changed_files(base_ref))

    windows = [p for p in WINDOWS_PLUGINS if p in plugins]

    print(f"::notice::{reason}")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(f"plugins={json.dumps(plugins)}\n")
            handle.write(f"windows={json.dumps(windows)}\n")
            handle.write(f"reason={reason}\n")
    else:
        print(json.dumps({"plugins": plugins, "windows": windows, "reason": reason}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
