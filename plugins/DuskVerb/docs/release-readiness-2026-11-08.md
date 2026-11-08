# DuskVerb 2 1.0 readiness implementation

This change set contains the release fixes and the missing studio-processor UI.
It is not a published release; the remaining acceptance work is listed below.

## Source and UI provenance

- Base: `main`, `b86f3e7707653ef6cf7252971c726dc952b0177d`.
- Missing UI branch: `ui/duskverb2-design`, `5305202bac1bd9e9768f4935745676290db903c6`.
- The branch has 12 commits absent from main, including `d9969648` (studio processor face), and is 59 commits behind main. Its UI and follow-up fixes were reconciled into the current source, preserving the newer DSP, 24 presets, reserved algorithm slots, Mix Lock and instance-owned A/B state.
- Build framework: `dusk-audio/DAF`, `68b07f25c9b9f2d1d014764615148806c7ce97e3`, in isolated checkouts. The existing sibling DAF checkout was not modified.
- Release configuration, tuning hooks off, local plugin installation off.

## Corrected behavior

- VST3 exposes the same canonical class ID on Windows, Linux and macOS. Windows preview migration instructions accompany the change.
- Tail reporting follows applied Freeze, supported engines, long decay/damping and outgoing preset fades. AU uses the framework's one-hour frozen-tail fallback.
- Edited post-tank EQ survives repeated preparation and rate/block changes while untouched factory voicing remains intact.
- The graphite face and amber display replace the older UI. Output history is sampled at 15 Hz independently of host block size; mono tracks meter their actual output. History and partial peaks clear on reset.
- Decay entry appears at the amber readout. Shared entry handling retains typed text on re-click, balances host gestures, and handles Enter, invalid input, Escape and field switching.
- A complete host state restore cancels unfinished entry for the outgoing sound; ordinary parameter automation preserves the entry.
- Preset identity survives editor reopening. Imported legacy XML retains the correct engine, accepts JUCE's bare default parameters, and rejects invalid names and malformed recalls transactionally.
- New nonlinear sounds remain selectable; unavailable controls dim and unsupported Freeze/Gate switches disappear. Mix Lock remains available below Bus.
- Native macOS/Windows CI now runs DuskVerb's tests. Fixed MSVC lambda compilation failures and a test DSP instance exceeding Windows' default stack.
- Manual, screenshots, release notes and the local unpublished website draft reflect the current interface and 24-preset bank.

## Regression evidence

Actual failing controls and restored results, retained under `/tmp`:

| Regression | Failing control | Corrected result |
|---|---|---|
| Windows VST3 identity | `13 checks, 1 failures`; old ID `445046206173636C764473446B737544` | Expected public ID `2046504473616C63764473446B737544` |
| CLAP frozen-tail contract | Original binary: 5 failures in 9,232 checks; frozen output still audible after 32 seconds | Integrated hosted suite |
| VST3 frozen-tail contract | Original binary: 6 failures | Integrated hosted suite |
| Post-tank EQ reprepare | Restore step removed: `8 check(s) FAILED` | `DuskVerbCoreTests: all checks passed` |
| Entry position | Original shared widget: 2 position assertions failed | Shared knob suite passes at multiple scales |
| Output-history bridge | Feed disconnected: `36 check(s) FAILED` | `DuskVerbCoreTests: all checks passed` |
| Host restore during value entry | `FAIL: host restore cancels unfinished entry for the previous preset` (real CLAP editor) | The same permanent UI gate passes, including the opposite automation case |
| Multi-Comp fleet meter gate | `FAIL knob beyond its range: press, outward drag and wheel 0 writes, -25.00; inward drag -25.00` | `PASS knob beyond its range: press, outward drag and wheel 0 writes, -25.00; inward drag -12.00` after isolating each case's ImGui context |

Neighboring paths checked: zero and variable blocks, mono/stereo/mono-to-stereo,
bypass, supported and unsupported Freeze, reset, repeated prepare, rate changes,
queued/live preset recalls, outgoing crossfades, factory versus unnamed snapshots,
factory EQ versus explicitly edited gains, Mix Lock on/off, INIT and A/B.

## Integrated-build evidence

- GitHub Actions [DAF build run 42479837947](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42479837947), commit `349292c5e04ffa89516d50c6b1142a799fab051f`: **all four DuskVerb jobs passed** (Linux x86_64, Linux ARM64, macOS and Windows). The fleet run finished with 35 successful jobs and two failures, both Multi-Comp's `MultiCompMeterRender` test on Linux. The failures reproduce locally: a previous case's click history opens text entry in the next case, preventing its intended drag. Giving each interaction case its own ImGui context fixes the test without changing plugin code; the follow-up CI result is recorded below.
- Follow-up [Multi-Comp run 42483440711](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42483440711), commit `ef45e5f29c2753df0ba19180e04f2b574645daeb`: **all four platform jobs passed**, including the formerly failing Linux x86_64 and ARM64 meter gate. Release publication was skipped.
- After the test-isolation fix, Multi-Comp's full Linux release build succeeded and **27/27 release-gating tests passed, 452.14 seconds**, using the same pinned DAF source as CI. This excludes the existing `report-only` parity test, as CI does. Plain presses and outward drags/wheel steps still preserve out-of-range saved values; inward drags and typed edits work. All meter-render cases and the real editor's drag/resize checks pass. DuskVerb's real-editor regression also passed again (**18.93 seconds**).
- Linux x86_64: **all 31 registered tests pass across the core/host and display-enabled runs**. The broad run passed 29 tests and its resize test was blocked by sandbox X11 access (601.18 seconds). After the final editor-state fix, the five affected hosted/resize/drag checks pass with an isolated display (18.49 seconds), including the expanded permanent UI regression.
- Windows x64/MSVC: **27/27 tests passed, 580.08 seconds**, including native VST3 identity, CLAP state/tail and the repaired test stack allocation. After the final UI-only adjustment, all three affected CLAP/VST3 hosted checks passed again (3.00 seconds).
- AddressSanitizer, UndefinedBehaviorSanitizer and leak detection: **`DuskVerbCoreTests: all checks passed`, exit 0**. LeakSanitizer required a run outside the sandbox to inspect threads.

- macOS universal x86_64/arm64 build: **30/30 tests passed, 69.96 seconds**, executed natively on arm64. Includes native AU state/routing/reset/tail and VST3 identity checks. Bundle signature verification passed; this is not Developer ID notarization. After the final UI-only adjustment, all three AU/CLAP/VST3 hosted checks passed again (0.77 seconds).
- Real Linux editor: typed `2.5s` reports `2.50 s` to the host. Both drag phases produced 12 edits, including a focus change. Factory navigation reaches Nonlinear Huge; Bus/Mix Lock, INIT and help pass real-pointer checks.
- Audio preservation: **192 renders** (24 presets × impulse/noise/snare/sustained synthetic inputs × 48/96 kHz) compare byte-for-byte equal to the pre-fix baseline. Maximum peak **0.237914**, observed C++ `new`/`new[]` allocations during processing **0**. This does not instrument every possible allocator or lock.
- CLAP validator: **42 tests run, 34 passed, 0 failed, 0 warnings, 8 skipped**, using the existing preset-discovery exclusions. DAF has no CLAP preset-discovery implementation; that path is not claimed as covered.
- VST3 pluginval: **strictness 8, SUCCESS**, GUI tests skipped; editor interactions were checked separately.
- `actionlint`, manual preflight and `git diff --check` pass. The rebuilt three-page PDF was visually inspected.

## Validation artifacts and logs

Local review artifacts are in `/tmp/duskverb2-release-artifacts/`, including the
manual, corrected screenshot and explicitly labelled validation binaries. These
are not public release downloads. Build and test logs use the
`/tmp/duskverb2-integrated-*` prefix; final editor tests use
`/tmp/duskverb2-ui-state-{red,green}.log`. The native source snapshots are isolated
under `/tmp/duskverb2-readiness-20261108` on macOS and
`C:/tmp/duskverb2-readiness-20261108` on Windows.

The fleet-test correction's failing and passing output is retained in
`/tmp/multicomp-meter-red.log` and `/tmp/multicomp-meter-fixed-cases.log`.
The full Multi-Comp build and gate logs are
`/tmp/multicomp-meter-full-build.log` and `/tmp/multicomp-meter-full-ctest.log`;
the repeated DuskVerb editor check is `/tmp/multicomp-duskverb-ui-check.log`.

The website draft, screenshot and PDF were merged in
[website PR #22](https://github.com/dusk-audio/dusk-audio.github.io/pull/22);
`published: false` was retained. No plugin release tag has been created.

## Packaged-download acceptance follow-up

The original CI archives from run `42479837947` were unpacked into isolated test
folders. REAPER used separate profiles and actual audio engines (ALSA null on
Linux, CoreAudio on macOS, Dummy Audio on Windows). Linux's installed plugin
folders were hidden from the test process so older copies could not satisfy the
scan. These are fresh scans and profiles, not clean operating-system installs.

| Available machine | Real-host results |
|---|---|
| Linux x86_64, REAPER 7.85 | VST3, CLAP and LV2 discovered; non-default edits, three editor open/close cycles, full static parameter restore, non-automated parameter restore and all five automation points passed. All nine eight-second bounces were finite and below full scale. Before/after automated renders matched exactly in each format, with measurable tails after the input ended. |
| macOS 27.0.1 / Apple M3, REAPER 7.79 | VST3, CLAP, LV2 and AU discovered; the same parameter, automation-point and editor lifecycle checks passed. VST3, CLAP and LV2 produced reverb tails and a measurable difference between automated and static renders. **Its AU rendered dry**, because DAF refused REAPER's latency query; a CI build with the DAF fix renders wet. See the AU section below. |
| Windows 11 Home 10.0.26200 / x64, REAPER 7.78 | VST3 and CLAP passed the same scan, state, automation-point and editor checks. Both produced finite, unclipped reverb bounces and responded measurably to automation. The final statically linked runtime build repeated these checks successfully. VST3 before/after audio matched exactly. |

The state test deliberately corrupts restored Decay Time in its negative control:
`FAIL vst3 automated session restores param 7 expected=0.41999998688698 actual=0.12300000339746`.
Removing that corruption gives `ACCEPTANCE PREPARE PASS formats=3` on Linux.
It also asserts that requested non-default values actually reached the plugin;
merely saving and comparing defaults is insufficient. The automated Dry/Wet value
is checked through its saved envelope, because REAPER legitimately evaluates it
at the current cursor position while loading a project.

Windows CLAP repeats of the *same unchanged project* also differ (observed maximum
sample difference `0.00775313`). A direct CLAP control with identical state and
identical pre-roll matched exactly; changing only silent pre-roll from 100 to 200
blocks produced maximum difference `0.0117903724`. Modulator phase can therefore
vary without a saved-state failure. macOS VST3 before/after bounces also varied;
bit-identical REAPER bounces are not claimed for those cases. These controls do
not replace listening acceptance or prove that every observed host variation has
the same cause.

### Windows runtime packaging fix

The original Windows CI CLAP and VST3 import `MSVCP140.dll`, `VCRUNTIME140.dll` and
`VCRUNTIME140_1.dll`, but the archive does not supply a runtime installer. DuskVerb's
CMake configuration now selects the static MSVC runtime for its targets. The new
Windows workflow gate inspects both shipping binaries and rejects external MSVC
runtime imports. This is scoped to DuskVerb; other plugins are unchanged.

- Old package control: the exact gate exits **1**, reporting
  `duskverb-2.clap requires an unbundled VC++ runtime`.
- Rebuilt package: the same gate passes; both binaries import only Windows system
  DLLs. Full native MSVC suite: **27/27 passed, 357.66 seconds**.
- Rebuilt VST3 and CLAP pass the REAPER acceptance described above.
- `actionlint` and `git diff --check` pass. The change is committed as `86d78b4b`.
  Fresh CI,
  [run 42502583390](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42502583390),
  built it on all four targets, with the temporary DAF pin described in the AU
  section. The gate passed on the CI binaries: both import only Windows system
  DLLs (`COMDLG32`, `dwmapi`, `GDI32`, `OPENGL32`, `KERNEL32`, `USER32`, `SHELL32`,
  `ole32`, `IMM32`). The run's native MSVC suite passed **28/28 (215.09 seconds)**;
  macOS passed 30/30 and both Linux targets 31/31. The CI-built Windows package
  has not been run in REAPER on the Windows machine; the REAPER results above
  come from the local rebuild. The post-merge
  [run 42504718251](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42504718251)
  passed the gate and the same suites again.

### AU played dry in REAPER (cause found, fixed in DAF)

The untouched CI AU produced only dry signal in REAPER 7.79 and 7.85 on the
available Mac, both after session restore and when freshly inserted. Automation
changed host-visible values but not the bounce. The eight-second bounce equals
the stimulus apart from REAPER's default item fades: it peaks at
`0.20123291015625`, the stimulus peak, and is silent from the moment the
stimulus ends at 3.0 seconds. Apple's AUMatrixReverb in the same project
produces a tail (RMS `0.0001619286`), and a track-mute control produces a
silent file. Realtime playback was dry as well: the track meter showed no tail
beyond its own decay, so the problem was never specific to offline rendering.

**Cause.** DAF's AU wrapper refused `kAudioUnitProperty_Latency` with
`kAudioUnitErr_InvalidProperty` (-10879) for any plugin built without
`DAF_PLUGIN_WANT_LATENCY`, and DuskVerb 2 does not define it. REAPER keeps
calling `AudioUnitRender` on a unit whose latency query fails, but mixes the
track without the unit's output. Apple's AU SDK base class answers this query
for every unit, which is why AUs built on it never show the problem.

It was found with a logging build of DAF's AU wrapper, built from the same
sources as CI and run in REAPER in the Mac's GUI session. Each build changed one
thing on top of the logging, and each bounced the same fresh-insert project:

| Change | Bounce |
|---|---|
| None (logging only) | Dry. REAPER sets an input render callback; the unit pulls the stimulus and writes wet audio into REAPER's own output buffers, returning `noErr` with the buffer pointers unchanged and no silence flag. |
| Accept `kAudioUnitProperty_OfflineRender` | Dry |
| Also copy the output into the input-callback buffers | Dry |
| Write a 0.5 test tone into REAPER's output buffer | Dry: the tone never reached the bounce |
| Log `Process`, `ProcessMultiple` and `ComplexRender` | Dry: REAPER never calls them |
| Answer the latency query with 0, and nothing else | **Wet**: peak `0.178849816`, the unit's own output peak, with a tail to 4.51 seconds |

Three statements in the first diagnosis were wrong or unsupported:

- The `au-before`, `au-after` and `au-static` bounces made by `mac-render.sh`
  did not test the AU. That script starts REAPER over SSH, and a process started
  over SSH sees no user-installed AU, so REAPER opened those projects with
  DuskVerb 2 missing. AU tests must start REAPER in the GUI session with `open`,
  as every run in this section did.
- The "zeroed at the public `AudioUnitRender` boundary" result cannot have come
  from an injected library: REAPER uses the hardened runtime without
  `allow-dyld-environment-variables`, so `DYLD_INSERT_LIBRARIES` never loads into
  it. The conclusion that REAPER discards the unit's output stands; the test
  tone above shows it directly.
- The native AU suite's 22,221 checks never asked a plugin without the macro for
  its latency. DAF's `AUWrapper` test now does.

**Fix.** [dusk-audio/DAF#112](https://github.com/dusk-audio/DAF/pull/112) makes
every unit answer the latency query on the global scope: 0 seconds for a plugin
without `DAF_PLUGIN_WANT_LATENCY`, and the plugin's latency, as before, for one
with it.

- `AUWrapper` fails on DAF `68b07f25` with "AU must describe its latency without
  DAF_PLUGIN_WANT_LATENCY" and passes with the fix. All eight macOS AU tests pass
  on the M3, and DAF's Linux, macOS and Wayland CI passed on the PR's head,
  `b68711a0`.
- DuskVerb 2 built universal on the Mac against the fix passes its 30 macOS
  tests. In REAPER its fresh-insert bounce is identical, bit for bit, to the
  latency-only probe's.
- Merged on 2026-11-08 as `0abd02e3`, whose tree is identical to the tested
  `b68711a0`. Plugin CI builds whatever DAF `main` is when it runs, so every
  build since, release tags included, has the fix, with no pin to change.

**Acceptance on a CI package.**
[Run 42502583390](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42502583390)
built DuskVerb 2 with the plugin CI pipeline from `86d78b4b`, with
`.github/daf-ref` pointed at `b68711a0` on a temporary branch,
`ci/daf-pr112-acceptance`, deleted after the merge. Its universal, ad-hoc signed
macOS AU answers the latency query with 0 seconds. It was installed in place of
the original CI AU and tested in REAPER 7.79 in the GUI session, next to the same
package's VST3. Every run logged the plugin binaries REAPER mapped; the first
fresh insert finished between two samples, so it was repeated to capture the
list, and both of its bounces are identical.

| Check | Result |
|---|---|
| Fresh insert, eight-second bounce | Wet: peak `0.178858161`, tail to 4.51 seconds |
| The same fresh insert with the package's VST3 | Bit-identical to the AU |
| Realtime playback, track peak after the input ends at 3.0 seconds | Above the no-FX meter decay in every 0.25-second window to 5.0 seconds, for example `1.9e-4` against `4.4e-6` at 3.50 to 3.75 seconds |
| Automated project, bounced before and after a save and reload | Bit-identical, for the AU and for the VST3 |
| Automated project against the static one | Different (maximum `0.113`): automation reaches the audio |
| Static project, AU against VST3 | Bit-identical |
| Automated project, AU against VST3 | Different, maximum `6.1e-3` (RMS `4.0e-4`) |

The package's VST3 renders bit-identically to the original CI package's VST3.
The DuskVerb 2 AU built on the Mac differed from the CI VST3 by up to `2.67e-5`;
the CI-built AU matches it exactly, so that difference came from the local build,
not from the AU format.

Afterwards the installed AU was restored to the original CI package, compared
byte for byte and its signature verified.

**Repeated after the merge.**
[Run 42504718251](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42504718251)
built DuskVerb 2 from `68914f9c`, the commit PR #346 merged, with
`.github/daf-ref` at `main`, which resolved to `0abd02e3`. It passed on all four
targets (macOS 30/30, Windows 28/28, both Linux targets 31/31), its Windows gate
passed, and its macOS AU answers the latency query with 0 seconds. The same ten
REAPER runs on its macOS package, each with its mapped binaries logged, gave
bounces bit-identical to the pre-merge package's in all eight bounced cases, and
the realtime tail again stayed above the no-FX meter decay.

Open: with automation the AU and VST3 bounces differ by up to `6.1e-3`, while
without it they are identical, so REAPER's automation reaches the two formats
differently. A likely cause is AU parameter changes taking effect once per render
call where VST3's are sample-accurate. That has not been verified; include
automated passages in the listening pass.

**Other plugins.** Tape Echo 2 and Ring Out, both shipped, and Chord Analyzer 2
are also built without `DAF_PLUGIN_WANT_LATENCY`. Loaded directly from the
release archives, the Tape Echo 2 1.0.8 and Ring Out 1.1.0 AU binaries refuse
the latency query with -10879, exactly as DuskVerb 2's original CI AU does. The
fixed DuskVerb 2 build answers 0 seconds, and TapeMachine 2 1.0.10, which
defines the macro, answers 0.00127 seconds once initialized. Their AUs are
therefore expected to play dry in REAPER until new releases are built from
`main`. CI builds of both against the fix, on the same temporary branch, answer 0
seconds:
[run 42502585122](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42502585122)
for Tape Echo 2 and
[run 42502586942](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42502586942)
for Ring Out, both green on all four targets; so do builds from `main` after the
merge,
[run 42504720533](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42504720533)
and
[run 42504722602](https://github.com/dusk-audio/dusk-audio-plugins/actions/runs/42504722602).
Tape Echo 2 could not be checked in
REAPER on the available Mac: REAPER loaded an August development copy from
`/Library/Audio/Plug-Ins/Components` even with 1.0.8 installed in the user
folder, and that copy aborts REAPER during a bounce with heap corruption.

Evidence on the Linux machine: renders, probe logs, the red and green test logs
and the CI-package acceptance (`ci2/`) are in
`~/Downloads/dusk-capture/dv2-au-latency-2026-11-08/`; the Tape Echo 2 and Ring
Out archives that were checked are in
`~/Downloads/dusk-capture/te2-au-latency-2026-11-08/`. The harness (GUI-session REAPER runner, render comparer, in-process latency probe
and the AU wrapper logging patch) is `tools/mac-reaper-au/` in the private
tools repository (dusk-audio-tools PR #16).

### Compatibility and distribution decision

Marc explicitly selected **existing ad-hoc macOS signing**. Developer ID signing
and notarization are not requirements for this release. All four macOS formats
pass strict signature verification and contain x86_64 and arm64 slices. Deployment
metadata is macOS **10.15 for Intel** and **11.0 for arm64**; metadata is not runtime
proof on those older systems.

The actual Linux x86_64 CI binaries passed native CLAP (**9,321 checks**), VST3
identity (**13 checks**) and LV2 state (**zero failures**) hosts inside an Ubuntu
22.04 / glibc 2.35 container. Linux ARM64 passed its original Ubuntu 22.04 CI job;
its maximum imported GLIBC symbol version is 2.34. No ARM64 desktop DAW run is
claimed. An Intel Mac/Catalina and clean Windows 10 machine are unavailable, and
Marc explicitly accepted recording those gaps while using the available machines.

Evidence is under `/tmp/duskverb2-acceptance/`: `logs/runtime-ctest.log`,
`logs/runtime-imports.log`, `logs/windows-runtime-red.log`,
`logs/windows-runtime-acceptance.log`, `logs/ubuntu22-hosts.log`,
`logs/mac-package-compatibility.log`, `logs/reaper-state-red.log`, the per-platform
acceptance logs, saved REAPER projects and WAV renders. Windows rebuilt results
are in `windows-runtime-results/`; the original CI results remain separate.

## Remaining release acceptance

1. Done: [dusk-audio/DAF#112](https://github.com/dusk-audio/DAF/pull/112) is
   merged, a CI package built after the merge passed the same AU checks, and the
   temporary `ci/daf-pr112-acceptance` branch is deleted. Every recorded AU
   bounce used a CI package; none used a release archive.
2. Done: the Windows runtime change is committed as `86d78b4b` and fresh CI built
   it; see the Windows runtime section. The release archives still come from the
   release workflow on the final commit.
3. Open: bounce the AU from the release archive in REAPER, as above, once the
   release workflow has built it. Logic Pro and other AU hosts have not been
   tested.
4. Complete human listening and interactive DAW acceptance. This pass verifies
   audio numerically and uses public REAPER APIs for editor lifecycle; it does not
   claim subjective listening, Logic testing, or manual screenshots on remote
   desktop sessions. Intel/Catalina and clean Windows 10 remain explicitly
   unverified, as agreed.

No release tag or publication was performed. The website remains unpublished.
The existing 192-render comparison proves preservation of existing voicing, not
new reference-anchor calibration. Private tuner `full_check`/`fleet_audit` and
clean-system download acceptance remain unclaimed.
