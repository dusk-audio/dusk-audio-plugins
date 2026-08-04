# Tape Echo 2 release checklist

The production version is declared in `dpf-plugin/CMakeLists.txt`. A release tag
must be named `tape-echo-2-v<version>`. The base version before the first `-`
must match that value exactly; suffixes such as `-rc.1` are allowed.

## Local release candidate

1. Configure a clean Release build with local installation disabled.
2. Build the `tape_echo-vst3`, `tape_echo-clap`, `tape_echo-lv2`, and
   `tape_echo-au` targets (Audio Unit is macOS-only).
3. Parse every LV2 Turtle file with `serdi`, then discover
   `https://dusk-audio.github.io/plugins/tape-echo` in an isolated `LV2_PATH`
   with `lv2info`. All 13 factory programs must appear.
4. Run pluginval at strictness 8 against the exact VST3 bundle and run the
   repository CLAP validator gate. On macOS, install the candidate Audio Unit
   and run `auval -v aufx DsTE Dusk`.
5. Run the complete black-box comparison campaign documented in
   `dusk-audio-tools/plugins/TapeEcho/tests/reference_comparison/README.md`.
   `factory_program_state_compare.py --strict` and
   `regression_gate.py --reference-bank` must both pass. Confirm that all final
   reports contain the candidate VST3 binary hash.
6. Exercise the editor resize, all 12 modes, preset recall, Power, Input Send,
   all Mix positions (including exact dry-only/wet-only endpoints),
   New/Used/Old tape states, tempo changes, and output automation in at least
   one AU, VST3, CLAP, and LV2 host as applicable. Restore an older project and
   verify hidden Dry Level/Wet Solo state and automation still affect the DSP.

## Tagged release

The `DPF build and release` workflow builds Linux x64/ARM64, macOS universal,
and Windows x64 artifacts. By default, the workflow ad-hoc signs the macOS
bundles (a valid signature, but with no Apple identity behind it); they are not
Developer ID signed or notarized. The workflow warns, packages them, and the
generated release notes disclose that status. Gatekeeper warns users on first
launch, so they must right-click and choose Open, or clear the quarantine
attribute. If
`MACOS_SIGNING_ENABLED=true` and the Developer ID plus notarization secrets are
configured, the workflow instead signs the AU, VST3, CLAP, and LV2 bundles,
notarizes the applicable bundles, and validates them.

Before creating the tag, publish `tape-echo-2-manual.pdf` at the website manual
path used by the workflow and review the generated release notes and archive
layout. Tagging, publishing, and website changes are intentionally separate
operator actions; local validation does not perform them.
