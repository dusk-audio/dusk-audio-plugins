# DuskVerb 2 release candidate

Updated 2026-09-12. This is an unpublished, uncommitted candidate, not release
approval. The approved native UI is retained. No engine coefficients or source
files under `src/dsp/` were changed.

Ready for the human to commit and push the reviewed plugin and consolidated DAF
changes. Publish DAF first, then update the plugin CI/Docker pins to that exact
revision before committing the plugin release work. This is source readiness;
the remaining compatibility checks below concern release qualification.

## Fixes completed in the release pass

- VST3 processing queues now apply factory programs, including zero-frame
  flushes, before ordinary parameter edits. Program identity storage is atomic;
  the callback does not invoke host restart or UI methods.
- LV2 program callbacks defer full-state serialization and its lock to the
  worker. Host save independently reads current state.
- Preparation warms the buildup-diffuser buffer capacity on both engines to
  the supported maximum. Preset swaps retain their original delay sizes,
  masks and modulation state without allocating on the audio thread.
- Frequency formatting remains stable across host text conversion at 100 Hz.
- CLAP rejects trailing state bytes before committing staged state, including
  when the terminator crosses a short-read boundary.
- Concurrent sanitized preset names publish without replacing each other;
  intentional same-name re-save and failed-write safety remain supported.
- Windows native resizing now preserves default `WM_WINDOWPOSCHANGED`
  processing, so WGL receives `WM_SIZE` and resizes its drawable. Previously,
  shrinking from 1200×800 to 1050×700 left a 100-pixel black band and clipped
  the bottom controls. The fix is in consolidated DAF's Pugl tree; DSP is unchanged.
- The release command registry, manual source/PDF and website inventory include
  DuskVerb 2. Website status remains `in-dev`; nothing was published.
- Knob activation now measures from the mouse press position, excluding approach
  movement delivered in the same UI frame. This prevents initial jumps while
  preserving normal/fine sensitivity and balanced host edit gestures. The shared
  headless regression reproduced nine failures before the fix and passes all 15
  checks afterward, across three pointer approaches.

## Evidence

Logs and frozen binaries are in
`/home/marc/.cache/duskverb2-implementation-20260912/`.

| Gate | Result | Evidence |
|---|---|---|
| Original JUCE comparison, 20 programs × 6 stems, 48 kHz/512 | 120/120 sample-exact | `candidate-release-rt/comparison.csv` |
| Additional rate/block matrix on the capacity fix | 480/480 sample-exact | `matrix-release-rt/comparison.csv` |
| Linux CTest | 13/13 passed | `release-linux-complete.log` |
| Final shared-knob regression and builds | Linux 14/14, Windows 11/11, universal macOS 12/12 passed | `knob-final-linux-tests.log`, `knob-final-windows-build.log`, `knob-final-macos-build.log` |
| Knob activation negative control | Nine failures before; all 15 checks pass after | `knob-activation-before.log`, `knob-activation-after.log` |
| Native Apple Silicon CTest | 11/11 passed | `release-macos-complete.log` |
| Universal macOS (arm64 + x86_64) build | Built both slices; 11/11 tests and 169/169 AU checks passed on Apple Silicon | `release-macos-universal.log`, `release-macos-universal-au.log` |
| Windows MSVC x64 CTest | 10/10 passed, including native CLAP and VST3 | `release-windows-after.log` |
| Windows CTest after native resize fix | 10/10 passed, 9.53 seconds | `windows-resize-rebuild.log` |
| Native Windows resize regression | Failed before fix; growth and shrink pass after fix; actual REAPER editor visually verified at 1050×700 | `windows-resize-before.log`, `windows-resize-after.log`, `windows-resize-fixed.png` |
| Windows native knobs | All 24 visible knobs changed their intended host parameters | `windows-control-sweep.log`, `windows-control-sweep.json` |
| Final Windows knob and workflow recheck | 24/24 bindings, 23 continuous drag sensitivities and 14 workflow checks passed; trim retains snapping; minimum-size rendering visually verified | `knob-final-native-sweep.log`, `knob-final-native-sensitivity.log`, `knob-final-native-actions.log`, `windows-final-minimum.png` |
| Final Windows native gesture undo/redo | Exact Mix drag and full-set restoration in both directions passed using one-shot probes | `knob-final-oneshot-undo.log` |
| Windows native workflows | 14 checks passed: invalid text correction, switches, independent A/B recall and Copy | `windows-ui-actions.log` |
| Windows tempo-sync UI | Selection, retained free-value editing and return to Free passed, preserving JUCE behavior | `windows-sync-check-final.log` |
| Windows REAPER project restore | Saved controls restored after instance destruction and project reload | `windows-reaper-session.log` |
| Windows REAPER instance/session isolation | 17 checks passed, including full parameter sets, two independent instances and fresh replacement defaults | `windows-reaper-isolation.log` |
| macOS and Windows GUI validators | pluginval strictness 8 exit 0 with editor, editor-while-processing and editor-automation tests enabled | `macos-pluginval-gui.log`, `windows-pluginval-gui-fixed.log` |
| Final macOS and Windows GUI validators | Rebuilt shared-knob candidates passed strictness 8, exit 0 | `knob-final-macos-pluginval.log`, `knob-final-windows-pluginval.log` |
| Windows validators | CLAP 33 passed, 0 failures/warnings, 11 feature/platform skips; pluginval strictness 8 exit 0 with GUI tests skipped | `windows-clap-validator.log`, `windows-pluginval-waited.log` |
| Windows concurrent preset publication | Distinct colliding names, re-save and separate sessions passed | `windows-preset-concurrency.log` |
| Apple Silicon native AU host checks | 169/169 passed | `release-macos-final.log` |
| CLAP validator | 34 passed, 0 failed, 0 warnings, 10 unsupported-feature skips | `release-clap-validator-clean.log` |
| CLAP short-read hosted tests | 2,126 passed | `clap-short-after.log` |
| VST3 pluginval strictness 8 | SUCCESS | `release-pluginval.log` |
| VST3 all-20-program transition allocation/blocking instrumentation | 0 failures | `v3-program-rt-all.log` |
| LV2 program allocation/blocking instrumentation | 0 failures | `lv2-program-rt.log` |
| ThreadSanitizer state publication | 20,000 concurrent recalls, 0 failures | `control-state-tsan.log` |
| Shared consumers | 4K EQ, Multi-Comp, TapeMachine, Tape Echo and Multi-Q builds/tests passed; affected checks rebuilt after final CLAP change | `release-consumer-final-summary.log` and per-consumer logs |
| Final shared-knob consumers | Rebuilt all five: 4/4, 9/9, 4/4, 5/5 and 2/2 tests passed respectively | `knob-final-consumers.log` |
| Manual | Preflight and PDF visual inspection passed | `release-manual-build.log` |
| Candidate packages | Linux x86_64, Windows x86_64 and macOS universal staged locally with manuals, licenses and verified file hashes | `release-package-verify.log`, `release-candidate-manifest.json` |

The full sound matrix uses a frozen binary before the final display-formatting
and CLAP-parser fixes; those changes do not touch DSP conversion or processing.
The package manifest hashes the final built files and source inputs. The
ThreadSanitizer result covers the control-state fixture, not every host/UI path.
Realtime instrumentation covers the listed allocation/blocking primitives, not
all possible I/O or platform calls.

Regression evidence includes 12 failing queued-program comparisons before the
VST3 fix, two failing deferred-work checks before the LV2 fix, six failing
VST3 callback allocation checks before capacity preparation, and failing text
round-trip/trailing-state tests before their fixes. Windows exposed a test-only stack overflow (0xC00000FD);
heap-owned DSP fixtures fixed it without changing the plugin. Negative controls injected
callback allocation and produced 204 VST3 / 2 LV2 failures. See the corresponding
`*-before.log` and `*-negative.log` files.

## Release gates still open

1. **Dependency reproducibility:** CI still pins DAF
   `867183d73b8fea20892eb8de49fb8c8b108c4910`, which does not contain this work.
   The consolidated DAF worktree is based on `c393724c9ab01ff52707a4bdc3fe1f587994a4b5`
   plus uncommitted changes. The human must commit/publish the framework changes,
   then pin that exact revision consistently in CI/Docker before release builds.
   Never substitute the current base SHA for the uncommitted changes.
   The latest remote `main`, `f17a0d575acff13c2627aabff168d2de0fe71010`, was
   checked and does not contain these changes either.
2. **Remaining compatibility qualification:** Logic insert/discovery menus
   and mixed-DPI/native-Wayland scenarios are untested.
   Logic is unavailable on the accessible Mac. Windows
   native controls and REAPER sessions are now covered, as are macOS validator
   GUI tests; these do not prove the remaining host/display combinations.

## Preserved baseline and test limitations

- Intel macOS runtime qualification is excluded from release requirements by
  Marc's explicit instruction. The existing universal binary still includes an
  x86_64 slice; its presence does not imply Intel runtime validation.
- **Original calibration baseline:** previous protected audits found identical
   original/port scoreboards with 308 inherited anchor failures. Vocal Plate,
   Ambience and Tiled Room also miss commanded calibration targets in the
   original. No thresholds or sounds were changed to hide these results; this
   remains a documented original limitation. Preserving this behavior follows
   the requirement not to change the original sound; this pass does not retune it.
- The first REAPER script assumed factory programs were entries in the host's
  preset API. REAPER instead exposes them as `Current Program`. Script-created
  undo points also did not isolate the intended baseline, so those failed harness
  attempts are retained and are not claimed as passes. Actual mouse-gesture
  undo/redo was verified through host parameter readback. REAPER's startup
  evaluation dialog was dismissed before the native gesture tests.
- The original native knob sweep used names as keys, which merged the plugin
  and host controls both named `Bypass`. The final sweep retains both and checks
  all 96 host parameter entries. The indexed isolation test also checks the full set.
- Windows resize test commands and independent CMake driver are retained as
  `windows-size-test.ps1` and `pugl-size-CMakeLists.txt`. The permanent regression
  is in DAF's existing Pugl `test_size.c`, included in its Meson test suite.
- The final continuously running REAPER bridge produced unreliable undo results,
  including duplicate command consumption. Those failed harness logs are retained.
  Stopping the bridges and using one-shot, deferred readbacks with a single undo
  or redo call passed full-parameter comparisons on the final binary.

No commits, tags, installation, signing or publication were performed. The
staged packages in `release-staging/` are for evaluation until these gates are resolved.
