# Multi-Comp 2 / Opto (LA-2A) — handoff, 2026-08-22 snapshot

You are the planner and reviewer; delegate implementation to Codex via the
`/codex` skill. Verify everything before acting — including the claims in this
file. The campaign log memory `mc2-la2a-campaign.md` is the long-form record;
this file is the working state.

**Artefact names changed after this file was written.** DAF plugins now compile
to their release slug, so the binaries this document tells you to hunt for are
`multi-comp-2.*` and `tapemachine-2.*`, not `multi_comp_2.*` / `tape_machine_2.*`.
The names below have been updated; older Logic caches, AUScan logs and `lsof`
output captured before the rename will still show the underscore forms.

**Snapshot authority (updated 2026-08-24):** the campaign branch
`multi-comp-2/opto-campaign-snapshot` lives in the worktree
`/private/tmp/dusk-audio-plugins-opto-campaign`. The snapshot has MERGED to
`origin/main` through PR #228 (`582ab99`: ff57a16 dense-programme parity gate,
0110ff9 AU sidechain-bus tests, 8d602d7 2x pin); the branch now carries the PR
0.85/1.00 parity-gate rows. The Opto code paths are unchanged since the
`d090a7b` state these measurements were taken on, so the gate table below is
current (Bus/VCA detector fixes landed after it via PR #224). Re-measure
before using any row as a DSP-decision baseline. The worktree path named in section 2 (`.../mc2-port`) is historical
and gone. The original authority statement follows for provenance: HEAD
`d090a7b` was based on `origin/main` at `f7edaea`; the measurements were first
recorded from the historical pre-rename `e0a4f21` worktree plus its listed
changes, then the campaign state was ported and verified with identical gate
output. Do not substitute files from the unrelated primary checkout.

---

## 0. The two most important lessons in this project

**A. The measurement summaries have been wrong more often than the code.** Six
refuted claims to date (fixed-ratio law, 3.34x release split, ~6x exposure
stretch, +3.64 dB rebound, even-dominant harmonics, a defective memory-curve
window). Read per-point data, never a verdict sentence; re-measure before
building on any number.

**B. macOS-green proves nothing about the shipped Linux binary.** This session
found Opto gain reduction differing by up to 1.5 dB between macOS and Linux
builds of IDENTICAL source: an absolute 1e-12 silence threshold sat in the
oversampler FIR tail's cancellation region, where above-vs-below depends on
FMA contraction. The hold-vs-discharge asymmetry compounded every burst.
Fixed in `9812639` (silence = |input| below max(detectorPeak*1e-4, 1e-9)
sustained 0.5 ms). Rules earned:
- never gate DSP on instantaneous |sample| vs an absolute epsilon; use an
  envelope-relative floor plus a short hold
- any binary branch with a margin-independent consequence (hold vs discharge)
  is a rounding-difference amplifier — audit for these when platforms disagree
- run the Opto core suite on Linux before pushing DSP changes. Local repro
  that matched CI to 6 decimals in minutes:
  ```bash
  podman machine start   # if needed
  podman run --rm --arch amd64 -v "$PWD/plugins:/src/plugins:ro" \
    docker.io/library/gcc:12 bash -c \
    'g++ -O3 -std=c++17 -I/src/plugins/multi-comp/core \
     -I/src/plugins/shared-daf/dsp \
     /src/plugins/multi-comp/core/tests/MultiCompCoreTests.cpp \
     /src/plugins/multi-comp/core/MultiCompDSP.cpp -o /tmp/t && /tmp/t'
  ```
  A second tripwire on macOS: build the core test with `-ffp-contract=off`
  and compare gate outputs against the normal build.

---

## 1. Scope — binding owner decisions

- **DSP remains Opto only.** On 2026-08-22 the owner explicitly authorised Opto
  UI work to proceed in parallel while further reference DSP measurements are
  pending. Later that day the owner explicitly widened UI shell scope only:
  remove the visible Global and Sidechain bands and make the shared header
  conform to TapeMachine 2 / Tape Echo 2. That shell change applies across all
  modes; it does not authorise DSP or per-mode panel changes. Keep UI and DSP
  iterations isolated. Bugs found in
  other modes get issues, not fixes (#220 was closed 2026-08-23 by the same
  fix). The
  `external ? sidechain : input` dead-link bug was fixed for VCA and Bus by
  452b13b (PR #224); the remaining instance is processStudioVCA
  (MultiCompModes.hpp:1443) — needs its own issue.
- **The hardware reference is the only oracle.** Old JUCE implementation is
  not a reference; its Opto golden vectors were deleted deliberately.
- **Parameter laws follow the reference** ("stick with parity, the goal is to
  match not deviate"). Controls with no reference equivalent (Stereo Link,
  Mix, Oversampling, Drive) are product decisions.
- Commit/push authorization was granted for THIS session's work and used.
  Treat it as spent: default back to never commit/push without a fresh
  instruction. Adversarial review must run clean before every push.

## 2. Repository state

Worktree (all work happens here): `/private/tmp/dusk-audio-plugins-opto-campaign`
(branch `multi-comp-2/opto-campaign-snapshot`). The build-recipe,
measurements-pointer, and DAF-pin bullets below remain CURRENT; the
worktree/branch lineage bullets are historical provenance for the 2026-08-22
measurements — the `mc2-port` scratchpad worktree they name is gone; do not
recreate it.

Historical worktree of the 2026-08-22 snapshot:
`/private/tmp/claude-502/-Users-marckorte-projects-dusk-audio-plugins/d3a95f36-6e62-4446-9f5b-32717dcb08a6/scratchpad/mc2-port`

- **Authoritative 2026-08-22 campaign snapshot:** worktree branch
  `multi-comp-2/opto-campaign-snapshot` at `d090a7b`, based on `origin/main`
  `f7edaea` (merged PR #223). Its tracked tree contains the faithfully ported
  campaign changes and the selective DAF build-workflow edit (historical: since
  PRs #225-#228, `origin/main` contains the full gate set).
- **Historical pre-rename 2026-08-22 source:** branch
  `multi-comp-2/review-followups` at `e0a4f21`, plus its five then-uncommitted
  files, with `origin/main` at `c1f9ae4`. This identifies the source from which
  the current snapshot was ported; it is stale and is not a worktree to use.
- **Historical 2026-08-21 handoff refs:** `main=662d6d5` and review branch
  `bad1c54`. They identify the earlier post-PR-#221 state only and are not the
  source for the current gate table.
- The ported campaign files are `.github/workflows/daf-build.yml`,
  `MultiCompModes.hpp`, `MultiCompCoreTests.cpp`,
  `MultiCompPluginLayerTests.cpp`, and `MultiCompUI.cpp`. They are committed in
  the authoritative snapshot. The workflow change remains owner-controlled and
  outside the Opto task; do not modify it.
- (HISTORICAL: the `b/` build directory belonged to the dead mc2-port worktree.
  The current worktree's build dir is `build-validation/`.) Configure command:
  ```bash
  cmake -U 'SDL2*' -U 'pkgcfg_lib_SDL2*' -S plugins/multi-comp/daf-plugin \
    -B b -DCMAKE_BUILD_TYPE=Release -DDAF_PATH=$HOME/projects/DAF \
    -DDAFWIDGETS_PATH=$HOME/projects/DAF-Widgets -DDUSK_DAF_INSTALL_LOCAL=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
  ```
- DAF fork pins live in `.github/workflows/daf-build.yml` (`DAF_REF` /
  `DAFWIDGETS_REF`). Do not modify `~/projects/DAF` / `~/projects/DAF-Widgets`
  or point builds away from these hard forks.
- Measurements: `~/projects/dusk-audio-tools/plugins/MultiComp/measurements/`
  (17 JSON + probe scripts). Read before measuring anything new.

## 3. Where Opto stands — authoritative 2026-08-22 macOS re-run

(2026-08-24: `testOptoDenseProgrammeParity` also carries PR 0.85/1.00
gap-tripwire rows snapshotting the current build — ceilings and the tightening
obligation live in that test's comment block, not here.)

| gate | current |
| --- | --- |
| static law (sine), 4 deltas | -0.021 / -0.145 / -0.085 / -0.196 dB |
| broadband static law | fitted RMS 0.073 dB, held-out -24 dBFS +0.016 dB |
| detector weighting | mean -0.237 dB; shape RMS 0.097 dB |
| detector memory, 16 pts (gate since reworked by bf2e797/1a3e018 into the symmetric 'output memory' probe — re-measure before citing) | RMS 0.070 dB, worst 0.142 dB |
| make-up taper | worst 0.0029 dB |
| output ceiling | worst 0.230 dB, peak +4.68 dBFS |
| burst-rate sweep, 2/5/10/20/40 Hz | +0.407 / +0.563 / +0.187 / +0.384 / +0.792 dB |
| crest sweep | -0.141 / -0.540 / **+0.615 held out** / -0.709 dB |
| dense programme, PR 0.4/0.55/0.7/0.85/1.0 (renderer A/B metric; the in-test gate rows use a different stimulus and scoring — never cross-compare) | +0.224 / +0.033 / +1.000 / +1.488 / +2.012 dB |
| harmonics | fundamental 0.0002 dB; compressed H2-H5 worst 0.071 dB |
| sample-rate invariance | 0.002 dB spread |
| asymmetric stereo | -0.19 / -0.25 dB, symmetric |
| non-Opto golden vectors | unchanged |

The older 2026-08-21 tree was verified to <0.001 dB across
macOS-clang-arm64, Linux-gcc-x86-64, Linux-arm64, and both fp-contract modes.
That result is historical. The current follow-up tree passes on default macOS,
macOS with `-ffp-contract=off`, and Linux GCC 12. Linux matches the no-contract
macOS detector-weighting rows within 0.000229 dB, but default macOS differs in
the low-frequency rows by up to 0.004563 dB; shape RMS is 0.096646 dB on
default macOS, 0.098245 dB without contraction, and 0.098219 dB on Linux. No
gate changes pass/fail state, but the old cross-platform <0.001 dB claim does
not apply to this default-clang weighting baseline. No production DSP change
was retained in this follow-up; future DSP work must still run all three checks.

### The remaining emulation gap (open, structural)

The reference's crest response is NON-MONOTONIC (10.19 → 14.07 → 11.33 →
8.52 dB GR at constant -24 dBFS RMS). On the authoritative snapshot our
residuals are -0.141 / -0.540 / +0.615 held out / -0.709 dB; the four-row RMS
is 0.546 dB. The older +0.71/-1.28 dB residuals and 0.96 → 0.75 RMS
comparison describe the historical 2026-08-21 silence-gate trajectory only.
Five earlier mechanisms were tried and rejected WITH numbers (see the crest
test's comment block and campaign log): 0.8 ms integrator, 6000 dB/s cell slew,
0.4 ms reservoir + slew, shorter reservoirs, duration-gated charge cap. Each
fixed one row by breaking burst-rate or a neighbour. A real fix needs
structure the model lacks: two detector paths combining nonlinearly or a
short-event charge-saturation mechanism distinct from the rejected
duration-gated cap. The follow-up audit below also excludes simple
repetition-blend scaling and a calibrated power/RMS rectifier. Do NOT fit
another correction term on top.

**Dense-programme A/B was re-run on the renamed `d090a7b` VST3.** On material
with 17.794665 dB crest, the authoritative PR sweep is:

| PR | reference GR | our GR | total-RMS residual | frame p50 | frame p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0.40 | 1.664 | 1.887 | +0.224 | +0.264 | +0.805 |
| 0.55 | 5.956 | 5.989 | +0.033 | +0.152 | +0.881 |
| 0.70 | 12.928 | 13.927 | +1.000 | +1.196 | +2.239 |
| 0.85 | 19.286 | 20.774 | +1.488 | +1.656 | +2.843 |
| 1.00 | 21.192 | 23.204 | +2.012 | +2.206 | +3.403 |

Total-RMS residual mean/worst/RMS is +0.951/+2.012/1.209 dB. An independent
mine-side render reproduced our PR-0.55 and PR-0.85 GR as 5.989099 and
20.774064 dB. The campaign table reports PR 1.0 as +2.012 dB; the earlier
artifact-level calculation was +2.012752 dB. That 0.000752 dB reporting
difference changes no conclusion or gate. Restricting the earlier three-row
analysis to the input duration changes its deltas by no more than 0.001445 dB.
The older +1.66 dB PR-0.7 result and +0.3 to +0.8 dB crest-interpolated estimate
are historical 2026-08-21 context only; the five-point sweep falsifies that
estimate. The near match at PR 0.4-0.55 followed by monotonic +1.000 to +2.012
dB over-compression establishes PR as a missing dynamic axis. The single-PR
crest gate cannot predict this programme result, and the static-law gates
exclude a corresponding steady-state curve error.
Renderer: `~/projects/dusk-audio-plugins/build/tests/duskverb_render/duskverb_render`,
reference AU `/Library/Audio/Plug-Ins/Components/uaudio_teletronix_la-2a_tc.component`,
harness in `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/`.
**Render our build with `--vst3`, never `--au`** (AU resolves through the
global registry and can silently load a stale installed build). GR is always
`output(PR=0) - output(PR=x)` at the same Gain, never input - output.

### 2026-08-22 follow-up audit and excluded mechanisms

- The detector-weighting mean removal is authoritative for the shape score,
  but it is no longer blind to absolute level. The gate now separately pins the
  mean at -0.237425 dB and the 1 kHz row at -0.174332 dB while retaining the
  0.096646 dB mean-removed shape RMS; the static sine and broadband gates own
  absolute level accuracy. Breaking the integration calibration moved the
  anchors to -0.297932/-0.258481 dB and failed the new assertion; restoring it
  returned the full suite to PASS.
- The five burst-rate periods divide 48 kHz exactly, each burst is exactly 480
  samples, and reference/control use the same 6.0-7.5 s scoring window and
  stimulus. No hardcoded divisor, count, or rate mismatch explains the
  one-sided residual. The bias remains an emulation gap, not an arithmetic fix.
- `testOptoLimitDynamics` measures charge/attack t90, not release. At PR 1.0 /
  -12 dBFS the residual changes from +0.604 ms after a 10 ms exposure to
  -10.437 ms after 100 ms and -12.333 ms after 1 s; the held-out PR 1.0 /
  -3 dBFS / 1 s row is -25.104 ms. This duration-dependent sign change links
  the extreme row to short-event charge (gap 4), not release decay (gap 3).
- Scaling the existing repetition blend to 75% was falsified: crest residuals
  became -0.141/-1.629/-0.709/-1.643 dB and fitted RMS rose from 0.521 to
  1.338 dB. A power/RMS rectifier, with its 997 Hz calibration derived rather
  than fitted, raised short-event recovery RMS from 0.701056 to 1.032525 dB and
  worst error from 1.385892 to 3.064520 dB. Both experiments were reverted;
  there is no production DSP change from this follow-up.
- A diagnostic run over all 60,000 sixteen-sample blocks of the authoritative
  dense stimulus found that the integrated follower already controls the hard
  `min(peak, integrated)` in 92.0733% of blocks. Mean peak/integrated separation
  rises from +1.219 dB in the loudest input-RMS quintile to +2.784 dB in the
  quietest. A blend bounded between the paths can therefore only raise the
  detector in the valleys. The parameter-free below-minimum competition
  `min^2/max` was tested as the remaining direction: crest residuals became
  -0.176/-1.903/-0.869/-0.433 dB, fitted RMS rose from 0.521 to 1.131 dB, and
  worst error rose from 0.709 to 1.903 dB. It also crossed the compressed
  harmonic-regime floor. The experiment and diagnostic scaffolding were
  reverted before the restored full-suite PASS.
- The purported remaining “charge saturation” candidate is not absent from
  this source. `followTarget` already makes charge rate depend on remaining
  capacity, using fitted fast/slow exponents 5.1/1.5 and a fast minimum rate of
  0.150. A second reservoir cannot be identified from the existing single-event
  and periodic-burst corpus without becoming another correction term.
- A temporary 16-sample-block diagnostic swept the dense stimulus across all
  five PR values. Blocks ended with cell state above the instantaneous target
  84.181-87.921% of the time. Mean total over-target retention was
  0.960528/1.791087/2.097717/2.108555/2.006334 dB at PR
  0.4/0.55/0.7/0.85/1.0. At PR 1.0 that retention was distributed across the
  fast/mid/slow cells as 0.663660/0.860702/0.534533 dB; no single population
  owns the error. `repetitionBlend` was exactly zero at every sampled block.
  This does not support a fast-charge-only or repetition-only edit, and it does
  not identify the reference's PR-dependent event law without event-aligned
  reference residuals. The diagnostic scaffolding was reverted.
- The existing Compress-mode PR-1.0/-24 dBFS/1 s attack report reaches t90 in
  9.493 ms versus 15.917 ms on the reference, so a one-knob reduction of the
  high-drive fast-rate pivot from 18 to 15 dB was tested. It failed its nearest
  neighbour before programme scoring: short-event charge fitted RMS/worst rose
  from 0.414244/0.716022 to 0.469745/0.994778 dB and tripped the gate. The pivot
  was restored to 18 dB. A simple high-drive attack slowdown is therefore not
  the shared fix.

**Next required reference measurement:** sweep dynamic behaviour across PR
0.4/0.55/0.7/0.85/1.0 rather than treating event pairing as the missing axis.
For each PR, use the same level- and crest-controlled dynamic stimulus and its
stationary control, retain the PR=0 render at the same Gain, and report
total-RMS GR plus frame p50/p95 and event-aligned attack/inter-event residuals.
This localises whether the divergence after PR 0.55 is acquired while charging
or retained between events. The earlier paired-event charge-capacity matrix is
a downstream discriminator only if the PR sweep demonstrates history-dependent
retention; it is no longer the next measurement. No further DSP structure is
justified until the PR-dependent dynamic error is localised.

## 4. Review findings addressed vs declined (CodeRabbit, two batches)

Fixed: frames==0 guard in run() (still publishes latched latency), __FILE__
backslash normalisation, geometry diagnostic shipped false, linear
calibration constant, report* renames, crossover static_assert.

Declined, with reasons — do not silently re-apply:
- **Shrink UI minimum size to half design**: conflicts with the deliberate
  cross-port geometry contract (min = design size, aspect locked) that
  matches the four correctly-rendering DAF ports; any change is an all-ports
  design decision (see [[daf-ui-conformity]], #213).
- **Derive test threshold offsets from production constants**: would make the
  gate tautological — the test must fail when the curve constants change.
- **Split fast/slow test targets**: suite is ~10 s; not a bottleneck.
- **PR=0 baseline caching in measureOptoStaticGr**: same reason.
- **Centralize orderedCrossovers vs MultiCompDSP::crossoverTargets()**: real
  duplication (clamp chains are semantically identical today) but cross-layer
  (core must stay framework-free); small standalone task if wanted.

## 5. Parallel Opto UI restyle (#212), shell conformity, and geometry (#213)

- Reference UI 950 x 263 (2U, 3.6:1), vector primitives only, NO third-party
  artwork/marks. LA-2A skin on the Opto panel only; other modes and chrome
  keep the Dusk dark theme. Panel readouts: Peak Reduction and Gain 0-100,
  Comp/Limit, meter reads GR.
- The stable visible Opto contract is Peak Reduction, Gain, Comp/Limit, the GR
  meter, compact Mix and 0-500 Hz Sidechain HP trims, plus the shell's
  eight-button Mode row and Bypass control. The other removed Global/Sidechain
  controls remain host-visible parameters and keep their state/automation
  ownership; they are no longer rendered as utility bands. Remaining emulation
  work is internal and is not expected to add Opto faceplate controls.
- The GR needle now applies a display-only, elapsed-time 300 ms RC response;
  Peak Reduction, Gain, and Mix value bubbles are unitless. A GR/+4/+10 meter
  selector is not implemented: the existing output bridge publishes block peak,
  and no authoritative RMS integration or +4/+10 digital calibration has been
  specified. Do not infer those choices from the reference artwork.
- The uncommitted 2026-08-22 shell pass now uses the Tape Echo 2 header contract:
  `MULTI-COMP / MC-2 / version`, previous/next factory preset, preset browser,
  INIT, SAVE, and right-aligned `DUSK AUDIO`. The Opto editor is 1120 x 380;
  other modes remain 1120 x 486. Both are aspect locked with the active design
  size as their minimum. `design-qa.md` records the native 2x comparison and
  interaction evidence.
- The geometry-contract fix (now in main) is still UNVERIFIED in Logic (AU
  cache needs restart/rescan). `kGeometryDiagnosticEnabled`
  (MultiCompUI.cpp) is now false; flip true only while collecting a host
  measurement, and flip back.

## 6. Codex practicalities

- `~/.claude/scripts/codex-delegate.sh`; `codex` binary resolves from the
  VS Code extension dir — do not conclude it needs installing.
- `-r <session>` SILENTLY DOWNGRADES to luna: pass `-m sol` on every resume;
  `-e xhigh` for DSP phases. Footer prints tokens + session id.
- A resume fails with "already has an active writer" if the original process
  is still running — check `ps aux | grep codex` before resuming; wait, don't
  kill.
- Codex cannot host the reference AU (sandbox blocks AudioComponent
  registration) and its provider refuses memory-safety harnesses. Codex
  writes code and unit tests; Claude runs every reference render and writes
  any UB/memory harness in-session.
- Briefs must carry: mandatory reading, constraints, hard scope walls,
  verification obligation with real output, report format. State that
  removing a fitted correction is a legitimate outcome when it is.

## 7. Measurement discipline (carry all of it)

1. Read a probe at its START (first ~4 ms); sanity-check retained reduction
   at t=0 against the static law.
2. Identical extraction on both sides — any change re-derives the reference.
3. Hold out points when fitting; report held-out error separately.
4. A control must contain the mechanism being tested for.
5. A suspiciously clean number is arithmetic, not tuning.
6. Ask what a new gate structurally cannot see (dual-mono corpus hid a dead
   stereo link; sine-only hid a 0.9 dB broadband error; 13 dB crest ceiling
   hid the 20 dB crest gap; macOS-only CI hid a 1.5 dB platform split).
7. If a fix improves its own gate and worsens others, it is a symptom fix.
8. One knob per iteration, fresh measurement between. State hypothesis and
   falsifying experiment BEFORE structural edits.

## 8. Report style the owner expects

Terse. Numbers, not adjectives. Report your own errors plainly and move on.
When two gates conflict, stop optimising and ask what single mechanism
produces both readings.

## 9. Logic "black box" AU investigation — 2026-08-28, OPEN

Symptom: the `aufx DsMc Dusk` AU opens in Logic Pro as an empty dark rectangle
with the plugin name centred. Same `.component` renders in REAPER.

**The plugin is exonerated in every configuration reproducible outside Logic.**
Measured with the committed `plugins/shared-daf/tests/DafAUViewProbe.mm`
against the Aug 28 14:38 build, which is byte-identical to the installed bundle:

| configuration | result |
|---|---|
| in-process, view instantiated | renders, 666 distinct colours in the GL front buffer |
| out of process (`LoadOutOfProcess`) | renders; host gets `_RemoteAUv2ViewFactory` + `NSRemoteView` |
| host `setFrameSize` 1012x257 and 700x200 right after creation | renders |
| 1000 blocks rendered through both input buses with the editor open | renders, `worstStatus=0` |
| the above under `MallocGuardEdges`/`MallocScribble`/`MallocCheckHeapEach` | clean, disposes cleanly |

Do not re-derive these. In particular the OpenGL-over-ViewBridge theory is dead:
out-of-process hosting composites the GL view correctly.

Also ruled out by evidence: Info.plist shape, linked frameworks, exported symbols
and ObjC class names (identical to `tapemachine-2`); code signing (the broken
plugin verifies clean, the *working* `tapemachine-2` does not); the committed
`MultiCompAUComponentBundlePath.mm`, which produces the same string as DAF's own
fallback and is a no-op in the normal case; `scanUserPresets`, which uses the
`std::error_code` overloads throughout and cannot throw out of the view factory.

Logic's own scan is clean: `~/Library/Caches/AudioUnitCache/Logs/AUScan*.plist`
records `Multi-Comp 2 start` / `took 0.000000 seconds`, no error.

**`auval` cannot catch this class of bug.** Its `Cocoa Views Available: 1` comes
from enumerating the class name; it never calls `uiViewForAudioUnit:`. That is
why `DafAUViewProbe.mm` exists, and why it belongs in `daf-au-test.yml`.

**Next step, and the only one left:** Logic running with the plugin inserted and
the blank window open, then `lsof` across the `Logic Pro` and
`AUHostingServiceXPC_arrow` pids to see which process maps the bundle. Note that
`AUHostingServiceXPC_arrow` loading a component is NOT proof of out-of-process
hosting — Logic's startup scan runs there too, and was observed loading
known-good `tapemachine-2` alongside Multi-Comp 2.

### Two real defects found alongside, both tracked and unfixed

1. `DAF::PluginAU::reallocAudioBufferList(bool)` heap-corrupts on `Uninitialize`
   (`~/Library/Logs/DiagnosticReports/REAPER-2026-08-28-110312.ips`, imageIndex 24
   is this bundle), and auval prints ~20 `AU Reports Processing in Place; Input
   buffer[0] ... and Output buffer[0] ... are not the same` on the same path.
   Multi-Comp 2 is the only DAF plugin with two AU input elements. The probe's
   render path does not trip it; the bus-reconfiguration sequences in
   `MultiCompAUTests.cpp` are the better vehicle. Tracked as
   [dusk-audio/DAF#21](https://github.com/dusk-audio/DAF/issues/21).
2. `docker/check_daf_pins.sh:84-90` and `.github/scripts/check_fork_sources.sh`
   both decide pugl's provenance from `.gitmodules`, never from the checkout.
   Local `DAF/dgl/src/pugl-upstream` origin is `https://github.com/DISTRHO/pugl.git`
   on `5e2621d` while DAF HEAD pins `43d8e34` (object not even fetched), and both
   guards report OK. Every local AU build compiled upstream DISTRHO pugl.
   `DAF-Widgets` is also off-pin (local `1c09e1ef`, CI `91e0004e`). Tracked as
   [#243](https://github.com/dusk-audio/dusk-audio-plugins/issues/243).

## 10. Milestone #2 closeout audit — 2026-08-30

The live milestone had two open issues at audit time: #200 (DAF AU true
sidechain bus) and #210 (Opto dense-programme parity).

- #200's requested production path is present in the pinned DAF checkout at
  `dfc50729` (the grouped-input-bus implementation is `cd445a50`). The committed
  `MultiCompAUTest` sees two AU input elements and passes stereo, mono,
  disconnected, disabled and routed-sidechain cases. Both the normal and
  `-ffp-contract=off` CTest runs passed 4/4, including `MultiCompAU`.
- #210's old issue-body figures (RMS 1.600/3.163 dB at PR 0.85/1.00) no longer
  describe current production. The current paired results are PR 0.40:
  mean -0.019567 dB, RMS 0.427058 dB, correlation 0.870237; PR 0.70:
  +0.440839/0.742100/0.854521; PR 0.85:
  +0.584630/0.921499/0.811253; PR 1.00:
  +0.616529/0.987459/0.822303. The four-row parity gate now constrains all
  three metrics at every PR. Removing the production startup-attack scale made
  PR 1.00 correlation fall to 0.734804 and the gate fail; restoring it returns
  the figures above and passes.
- The FET/1176 lifecycle test previously poisoned a dead sentinel rather than
  the state used by the callback. The sentinel is removed and the test now
  poisons/checks both input peaks and both active/silent counters across
  repeated prepare, reset, mode exit and settled bypass. Removing the bypass
  active-counter reset produces the expected focused failure; restoring it
  passes with the counter saturated at 24 and post-window blend exactly 0.
- Current FET audio DSP remains the accepted Wave 27/30 state. No scalar DSP
  candidate survived the recorded controls, so no speculative audio change was
  made in this closeout. The meter-film comparison is still measurement-blocked:
  UAD independent repeats exceed the 0.5-degree repeatability limit at 27/504
  points, with a 22.172-degree maximum. Collect continuous timestamped,
  nonblocking film before scoring another visual-meter candidate.
- Verification on this source: native CTest 4/4 in 40.21 seconds; no-FMA CTest
  4/4 in 43.49 seconds. The Linux GCC 12 tripwire could not run because the
  existing Podman VM returned to `stopped` immediately after reporting a
  successful start; this is an unverified platform, not a test failure.

## 11. FET Wave 31: shallow-knee parity — 2026-08-30

Wave 28 identified the next measurable frontier: the accepted detector entered
the 4:1 knee too early, and its 100 Hz/1 kHz split contradicted the reference.
The new `--fet-knee-onset` gate uses nine independently captured points per
frequency from -14 through -6 dB driven level, plus held-out 40 and 60 Hz
endpoints from Wave 24. On the pre-change production path it failed with
0.724184 dB worst absolute error and 0.120661 dB worst frequency split. The
accepted path reads 0.011842 dB and 0.000574 dB respectively; the bounds are
0.04 and 0.03 dB.

The implemented mechanism is a shallow, raw-input peak cell for vintage 4:1
only. It holds peaks for 30 ms, releases over 250 ms, and applies the difference
between the existing colour-envelope reduction and the measured onset table as
a post-colour scalar, bounded to +/-0.85 dB (the measured pre-change miss tops
out at 0.724 dB). The original transformed detector and FET envelope remain the
harmonic lookup coordinate. The auxiliary cell fades over -5 to -4 dB and clears
immediately at the accepted-law join, leaving deeper reduction, other ratios,
Studio FET, All-buttons and the established harmonic surface on their existing
paths. Its state is part of `FETState`, so repeated prepare and reset clear it
with the rest of that aggregate.

Controls that shaped the result:

- Replacing the main envelope target passed the onset rows but moved the
  harmonic coordinate; low-frequency H3 missed by as much as 5.895 dB.
- A post-colour scalar without a true peak hold amplitude-modulated the
  harmonics; low-frequency H3/H5 errors reached 22 dB/0.806 dB.
- Linear frequency compensation fixed 100 Hz but over-corrected the held-out
  40 Hz endpoint by 0.090826 dB. The final common cell needs no frequency fit.
- Letting the cell release above the accepted-law join made the -42 dBFS attack
  curve miss by 0.0832 RMS (bound 0.038) and read 2.052x the reference fitted
  tau. Ending it at the join restored the attack gate to 0.0289 RMS worst-case
  without moving the knee result.
- The opposite deep-to-shallow control exposed a second failure in that draft:
  after 33.757294 dB of main-envelope reduction the auxiliary cell cancelled
  14.446922 dB of retained release memory (3.229736 dB still cancelled after
  four seconds). Bounding the cell to its measured domain reduces the observed
  maximum/final correction to 0.849715/0.849714 dB while leaving the static and
  harmonic gates unchanged. Removing the bound reproduces the focused failure.

Neighbour verification passed for static and maximum reduction, all ratios,
All-buttons and measured-curve arms, attack drive/knob, startup peak/lifecycle,
deep-to-shallow release memory, block-size invariance (maximum delta 0), reset
determinism (maximum delta 0), stereo link/phase, and sample rates
44.1/48/88.2/96 kHz (worst flat-gain delta 0.003448 dB). The six colour gates
remain within their established bounds:
low H5 0.117296 dB, low H3 0.646858/0.104789 dB at 100 Hz/1 kHz, LF colour
0.232735 dB, broadband H2 0.103566 dB, H3 0.059604 dB and H5 0.068170 dB.

Final release builds produced JACK, LV2, VST3, CLAP and AU. Native CTest passed
4/4 in 46.96 seconds; no-FMA CTest passed 4/4 in 50.70 seconds. The Linux retry
again reported a successful Podman-machine start and then immediately returned
to `Running: false`; the unverified platform gap recorded in section 10 remains.

## 12. FET Wave 32: complex shallow-knee H3 parity — 2026-08-30

Wave 25's retained 33-point, quarter-dB 100 Hz sweep remained a real hole after
the Wave 31 transfer fix because that post-colour scalar deliberately left the
harmonic ratio unchanged. The new `--fet-low-h3-dense` gate renders the same
12-second stimuli and 9.0-10.5 second coherent window in process. On the
unchanged Wave 31 path it failed at 1.756796 dB worst H3/H1 error and 0.855123
dB worst adjacent error step. The retained UAD WAVs also expose the missing
complex constraint: reference H3 phase moves smoothly from 164.574 to 90.682
degrees, while Wave 31 moved from 167.171 to 54.322 degrees and missed by up to
73.758 degrees. A magnitude-only scalar fit can therefore match the reference
only by jumping between two coefficient roots; it is not a parity mechanism.

The accepted mechanism is confined to vintage 4:1 and the measured -15 to -4
dB driven-input window. It makes pure Chebyshev T3 and T5 bases from the raw
input normalised by Wave 31's held peak, then passes each through one and two
300 Hz low-pass poles. Four real coefficients jointly match UAD H3 real/imaginary
while holding the prior H5 real/imaginary vector. Half-dB rows from -14 through
-6 dB are fit anchors; all intervening quarter-dB rows remain held out, with
cubic interpolation used only across the uniformly spaced fit anchors. The
unequal -15/-14 and -6/-4 endpoint fades stay linear. The four pole states live
in `FETState`, so aggregate prepare/reset clearing covers both channels without
allocation or callback locking.

The accepted dense result is 0.194420 dB worst H3/H1 error, 0.194702 dB worst
adjacent error step and 2.587519 degrees worst phase error, against 0.75/0.20/3.0
bounds. A disabled-cell control across the same 33 rows measures maximum
movement of 0.000000475 dB raw GR, 0.000034092 dB H1, 0.000090070 dB H2 and
0.004332182 dB H5, all below the predeclared 0.02 dB isolation bound.

Controls that shaped and prove the result:

- Re-keying only low-frequency K3 from raw to net knee reduction was rejected:
  it worsened the dense result to 3.908683 dB error and 2.320884 dB adjacent
  step. The Wave 31 scalar is not the hidden harmonic coordinate.
- A raw-input direct-plus-lag draft closed dense H3 and fixed H5, but leaked
  into the established 1 kHz H3 guard (0.846793 dB worst versus about 0.10 dB
  before). Replacing the direct branch with the first 300 Hz pole and using the
  second pole as its independent vector restores the 1 kHz guard to 0.097896
  dB.
- Scaling the accepted direct coefficient to 99 percent leaves magnitude and
  continuity green at 0.225589/0.199254 dB, but phase rises to 3.022585 degrees
  and the combined assertion fails. Restoring it returns 2.587519 degrees.
- Removing only the two T5 hold terms leaves H3 green at 0.194526 dB error,
  0.194710 dB adjacent step and 2.585369 degrees, but moves H5 by 0.032555608
  dB and produces the focused neighbour failure. Restoring them reduces that
  movement to 0.004332182 dB.

Neighbour verification passed for static and maximum reduction, all ratios,
All-buttons and measured-curve arms, the Wave 31 knee/onset and deep-release
cell, attack drive/knob, startup peak/lifecycle, block-size invariance (maximum
delta 0), reset determinism (maximum delta 0), stereo link/phase and flat gain
at 44.1/48/88.2/96 kHz (0.003448 dB worst). The established colour gates read:
low H5 0.117306 dB, low H3 0.619408/0.097896 dB at 100 Hz/1 kHz, LF colour
0.232732 dB, broadband H2 0.103566 dB, H3 0.076874 dB and H5 0.068170 dB.

Final release builds again produced JACK, LV2, VST3, CLAP and AU. Native CTest
passed 4/4 in 58.98 seconds; no-FMA CTest passed 4/4 in 62.87 seconds. The full
Milestone #2 sidechain/Opto gates from section 10 remain green. Linux GCC 12 was
not re-run in this wave; the stopped-Podman platform gap remains unverified.

## 13. FET Wave 33: broadband complex H3 parity — 2026-08-30

Wave 27's scalar-rate experiment had compared different Release controls and
therefore rejected a coordinate the experiment did not hold constant. Its
matched-reduction rows used Release 0.5; the original harmonic campaign used
0.66595459. Once that distinction is retained, the sparse broadband-K3 fit is
shown to match its anchor magnitudes but miss the UAD vector between them: at
Input 0.8 the reference H3 phase rotates by more than 120 degrees while the
reduction-only cubic remains near 135 degrees.

The new `--fet-broadband-h3-dense` gate contains 17 same-stimulus UAD rows at
48 kHz and 13 at 96 kHz. Nine Release-0.5 rows per rate are the long-window
calibration surface; the original Release-0.66595459 campaign rows are the
opposite-control validation surface. Wave 32 failed this gate at 4.441893 dB
worst magnitude error, 85.829597 degrees worst phase error and 4.479455 dB
worst adjacent error step. The accepted Wave 33 result was 0.174096 dB,
0.528112 degrees and 0.172962 dB against 0.25/3.0/0.20 bounds.

The mechanism uses raw Chebyshev T3 and its first three 300 Hz pole states.
Four real coefficients solve the complex 1 kHz H3 residual while forcing the
same cell's complex 100 Hz H3 contribution to zero. Release interpolates
between separately measured 0.5 and 0.66595459 coefficient tables; Input uses
a triangular measured-cell weight centred at 0.8. The 96 kHz normalisation was
fit only from the Release-0.5 rows; four untouched 96 kHz campaign-Release rows
then validated within 0.075 dB / 0.42 degrees. `shallowT3Lowest` is part of
`FETState`, so aggregate prepare/reset clearing covers it without allocation or
callback locking.

The neighbour gate measured 0.000000496 dB GR, 0.000011050 dB H1,
0.000099685 dB H2 and 0.002120043 dB H5 movement at acceptance. Injecting
`0.0001 * shallowT5Lag` left the H3 gate green but moved H5 by 7.060746814 dB
and produced the intended isolation failure; removing the fault restored the
figures above. Native/no-FMA focused gates and both 4/4 full suites passed
(73.82/78.42 seconds). Wave 34's independently intended H5 correction changes
the stored H5 neighbour anchors; with those accepted anchors the current H3
gate reads 0.183746 dB / 0.504795 degrees / 0.160003 dB natively and
0.184155 / 0.503875 / 0.160423 without FMA.

## 14. FET Wave 34: broadband complex H5 parity — 2026-08-30

The remaining broadband H5 problem was larger than Wave 26's 0.372948 dB
96 kHz magnitude failure. Retained same-stimulus Release-0.5 captures expose a
5.131136 dB / 137.482381-degree miss, while the sparse campaign magnitudes hid
the phase error. A new `--fet-broadband-h5-dense` gate now carries 31 scoreable
same-stimulus UAD vectors: Release 0.5 at 48/96 kHz, the original
Release-0.66595459 grid at 48/96 kHz, and three independently retained 48 kHz
campaign midpoints at -30, -18 and -9 dBFS.

The old scalar proposal is conclusively rejected, not tuned around. Zero/full
source controls confirm complex linearity, but the best real scale still leaves
0.739-2.513 dB and 8.735-33.615 degrees residual. The accepted mechanism is a
raw Chebyshev T5 plus its first three 300 Hz pole states. Four real coefficients
solve the 1 kHz H5 vector and force the joint 100 Hz H5 contribution to zero.
Tables retain drive, Input, Release and 48/96 kHz as measured coordinates;
zero guards keep the correction dormant below the -92 dBc scoring onset, and
triangular Input weights leave neighbouring measured positions on their prior
paths. The added `shallowT5Lowest` state is cleared by the existing aggregate
prepare/reset paths.

The three initially unscored 48 kHz campaign midpoint magnitude errors were
0.779003, 1.450880 and 0.493261 dB. They now read +0.000019, -0.000021 and
-0.000019 dB, with phase errors +0.000016, +0.000093 and +0.000089
degrees.
Across all 31 rows the native worst result is 0.001763 dB / 0.007186 degrees;
the no-FMA result is 0.002256 dB / 0.021547 degrees, against 0.25 dB / 3.0
degree bounds. The pre-existing sparse broadband H5 gate improves to 0.055018
dB worst, and the constrained opposite-frequency path remains 0.117324 dB
worst at 100 Hz.

Neighbour verification passed for broadband H2 (0.103559 dB), LF colour
(0.232734 dB), both dense H3 gates, static and maximum reduction, knee onset
(0.011713 dB worst and 0.000446 dB frequency split), deep-release memory,
startup lifecycle (counter 24, blend 0), stereo phase, reset determinism and
block-size invariance (both maximum delta 0), and 44.1/48/88.2/96 kHz flat
gain (0.003448 dB worst). After updating the H3 gate's H5 anchors to this
independently accepted surface, a deliberate `0.00001 * shallowT5` leak left
H3 within its bounds but moved H5 by 11.682719686 dB and produced the expected
neighbour failure; restoring production returns native H5 movement to exactly
0 in that gate (0.002006967 dB without FMA).

Final release builds produced standalone/JACK, LV2, VST3, CLAP and signed AU in
both variants. Native CTest passed 4/4 in 86.78 seconds; no-FMA CTest passed
4/4 in 92.00 seconds. This includes the Milestone #2 AU sidechain and Opto
dense-programme gates from section 10. A final live GitHub audit confirms both
milestone issues, #200 and #210, are CLOSED (closed 2026-08-30 at 14:51 UTC).
Linux GCC 12 was not re-run; the stopped Podman platform gap remains explicitly
unverified.

## 15. FET Wave 35: final stereo/recovery audio parity — 2026-08-30

The two retained Wave 27 audio holdouts are now measured gates and both pass.
This closes the known FET audio-parity surface; it does not manufacture a claim
for the still-unrepeatable visual meter film or the unavailable Linux runner.

The new `--fet-stereo-dense` gate replays the exact 48/96 kHz UAD phase rows:
the exposed 96 kHz equal-level quarter-cycle cell, its two adjacent phases, the
same 48 kHz cell, and the unequal-level opposite arm. The pre-change path failed
at 0.059104905 dB worst. A common oversampled detector was rejected at
0.199462399 dB and native winner selection with an oversampled maximum was
rejected at 0.345179409 dB. The control experiment was decisive: evaluating the
link at 1x reduced the whole falsifier set to 0.006510735 dB. The accepted path
therefore evaluates the internal signed-maximum link control once per host
sample and holds it through the oversampling phases, while leaving audio,
colour, and external-sidechain interpolation oversampled. It reads 0.009922296
dB native and 0.010486871 dB without FMA against the 0.020 dB bound. The older
in-phase/antiphase law, asymmetric stereo reference, block and reset gates all
remain green.

The new `--fet-recovery-dense` gate covers all 16 retained recovery conditions
and five declared windows per condition: 48/96 kHz, 1/4 kHz carriers, -30/-18/-6
dBFS drive, Attack 0/0.5/1, 4:1/8:1, and the 90-degree terminal-phase falsifier.
The current pre-change binary failed at 3.404430389 dB. It also exposed a newer
neighbor regression: after a loud burst, the shallow-knee helper treated a
retained envelope as live knee programme and produced a false +0.85 dB gain
hump, reaching +0.915519714 dB error in the 48 kHz phase-90 4.5 s window. The
helper now requires the existing 2 ms live-programme support, so carrier zero
crossings remain supported while the post-burst false arm cannot engage.

The remaining recovery difference is a finite terminal-charge population, not
a new asymptotic release scalar. Its measured coordinates are maximum
reduction, Attack, Release, ratio, host rate, and terminal level drop; the last
coordinate supplies the phase/time-order degree of freedom that Wave 27 proved
was missing. A compact positive terminal arm plus the independently measured
small bipolar tail reaches 0.140045166 dB worst over all 80 native points and
the same 0.140045166 dB without FMA, against the 0.150 dB bound. Static and
sustained programme never enter this arm. Ratio/Attack/Release automation
clears a pending event instead of reinterpreting it under new controls.

The recovery state is aggregate-initialized in prepare/reset and explicitly
cleared on repeated prepare, reset, FET mode exit, settled bypass, and the
unused channel of a mono render. `--fet-recovery-state` measures an exercised
gain of 1.491761/1.491761 and exactly 1.000000/1.000000 after prepare, reset,
mode exit, and bypass; mono clears channel 1 to exactly 1.000000. Removing only
the settled-bypass clear left 1.482260/1.482260 and produced the expected test
failure; restoring it returns the gate to green.

Focused neighbors passed for knee onset/release, static and maximum reduction,
All-buttons settling, attack drive/knob, startup peak/lifecycle/neighbors,
stereo, reset and block invariance, sample-rate gain, and every broadband/LF
H2/H3/H5 gate. Final production builds again generated standalone/JACK, LV2,
VST3, CLAP, and signed AU in both variants. Native CTest passed 4/4 in 109.23
seconds; no-FMA CTest passed 4/4 in 116.06 seconds. These suites include the
Milestone #2 AU-sidechain and Opto dense-programme gates; the live closeout in
section 14 remains valid with issues #200 and #210 CLOSED. Linux GCC 12 was not
re-run because the Podman platform remains unavailable, and visual meter-film
parity remains measurement-blocked by the reference's failed repeatability
control.

## 16. VCA mode = dbx 160: campaign and model — 2026-09-01

Owner decision: the VCA mode achieves parity with the UAD dbx 160 (installed
`/Library/Audio/Plug-Ins/Components/uaudio_dbx_160.component`, `aufx U373
UADx`, v1.0.9); its front panel has no attack/release, so those controls are
gone from the face. Harness: `dusk-audio-tools/plugins/MultiComp/tests/
reference_comparison_dbx160/` on the shared `harness_core` (README lists every
probe and the model). Everything below was measured on the installed AU and
reproduced on the plugin with one extraction per probe; every report is
capture-audited.

Laws (`core/MultiCompDbxLaw.hpp`): THRESHOLD linear -55..0 dB (knob text is the
sine onset in peak dBFS +0.26 dB); COMPRESSION is a 0..100 knob position whose
applied ratio is the measured table (2.37/4.14/7.51 at 25/50/75 %, Inf at the
stop; the panel read-back rounds lower); OUTPUT GAIN linear -20..+20 (exact);
SC FILTER is an on/off switch engaging a half-order tilt ~sqrt(f/276 Hz) that
keeps rising to +19.4 dB at 20 kHz (`SidechainTilt`, seven sections fitted in
the digital domain, 0.12 dB RMSE); MIX is a linear amplitude crossfade with
gain on the wet path; the stereo detector is a linked maximum.

Model (`processVCA`): one first-order RMS power integrator (35 ms) run once per
host sample on the native oversampling phase, driving the gain directly. Its
dB reading passes through the measured absolute-level detector calibration in
`MultiCompDbxLaw.hpp` before the threshold comparison. No
attack/release envelope: a single constant fitted to nine reference step
responses (three depths x three ratios) gave 31/35 ms at 0.22/0.06 dB RMSE, and
the equal-RMS sine/burst/noise triplet reads identical GR on the reference. No
colour block: the reference's H2/H4/H5 sit at the numerical floor and its H3
(~-67 dBc) is the detector's own 2 kHz ripple, which the model reproduces to
0.2 dB. Threshold compared 3.01 dB below the knob (RMS of a sine) plus the
0.29 dB measured onset offset. `vca_attack`, `vca_release`, `vca_overeasy` and
`vca_detector_mode` remain host-visible for state compatibility and are inert.

Findings that changed the code, each from a falsifier or a direct measurement:
the JUCE-era level-adaptive RMS constant inflated the deep slope from 0.76 to
0.80 dB/dB (fixed-tau control read 0.750); the 2x sidechain's linear
interpolation refilled the RMS reading above 8 kHz (-1.25 dB at 12 kHz, non-
monotonic; native-phase integration reads flat to 20 kHz like the reference);
a per-cycle GR mean over gated bursts diluted GR by the duty cycle when one
side is digitally silent (use energy-weighted GR). The 2026-09-02 dense knee
control at threshold positions 0, 0.25 and 0.509064 falsified a threshold-knee
fix: ratio-normalised residual instead collapsed onto absolute input level.
The 4:1 detector calibration predicted the held-out Inf curves to 0.067 dB and
the independent static ratios/thresholds before it was added to the DSP. The
remaining depth-linear Inf residual falsified the broad-range `-60:1` endpoint:
the reference output's direct -12 to -6 dBFS step is -0.070243 dB at three
thresholds, or `-85.418:1`. Encoding that measured slope reduced the held-out
Inf sweeps to 0.017 dB and the full static worst to 0.051 dB. Because changing
the stop also changes the formerly unmeasured 95--100 % interpolation, a direct
97.5 % capture measured `87.972:1`; adding that point moved its deep anchor
from +0.032 dB (failing) to -0.005 dB (passing).

Scoreboard (VST3 SHA 10ea6ad5ecf3, 2026-09-02): step attack/release within 1 ms
at every depth and ratio; six dense 4:1/Inf knee sweeps have worst residuals
0.012/0.017 dB at threshold 0, 0.008/0.010 at 0.25, and 0.012/0.016 at the
default; the seventh 97.5 % holdout is 0.016 dB worst. The 100-cell static grid
reads mean +0.005 dB, mean absolute 0.0095, 100/100 within 0.5 dB, worst 0.051
dB. Crest
excess sine/burst/noise is +0.00/+0.22/+0.01 dB; detector FR is flat within
0.01 dB above 100 Hz; SC filter RMSE is 0.129 dB from 40 Hz-20 kHz; H3 mean/max
residual is -0.124/0.215 dB; Mix remains linear. Every cited capture passed the
harness audit. Framework refresh: DAF `50ad8c22` (PR #26) and DAF-Widgets
`487b092a`; the latter changes funding metadata only, and the current candidate
builds against both refreshed checkouts. Final native CTest passed 4/4 in
115.24 seconds and the no-FMA build passed 4/4 in 123.07 seconds.

State: version 4 (v3 `vca_ratio` migrates through `compressPosition()`, the
threshold clamps into -55..0; regression test in the layer suite). Remaining:
meter calibration/ballistics (visual), retiring the four inert parameters (state v5,
shifts host indices for every later parameter and therefore the Opto/1176
campaign pins -- do it as its own change). Linux gcc-12 amd64 (podman) ran the
full predecessor core suite to PASS on 2026-09-02. On final SHA 10ea6ad5ecf3,
the dbx tilt and VCA parity gates passed (deep Inf deltas +0.046/+0.021/-0.006
dB; 97.5 % delta -0.006 dB), followed by the complete unchanged Opto block and
the early FET neighbor gates. The redundant emulated sweep was stopped during
the unchanged dense FET harmonics when work was redirected back to VCA; native
and no-FMA still completed the full final suite.

UI note (2026-09-01, later): the VCA face was briefly moved to the 486-tall
canvas to match the dbx 160's ~2.5:1 proportions and reverted the same day --
Logic kept the 380 window and the canvas rendered letterboxed at 78 %, and the
headless probe shows the AU wrapper's GL child at half size after any resize
(evidence on #240). Rule until #240 is fixed: a mode's canvas must equal the
plugin's opening size (380 for the hardware faces). The reference styling
(letter-spaced silkscreen via `spacedText`, domed knobs on serrated skirts, the
hardware's mV/V threshold ring at the reference's own angles, keycap meter
buttons, PULL/SC slide switch, amber/blue meter through the shared
`DuskVuMeter` style options) lives on the 1120x310 face.

VU geometry fix (2026-09-02): `DuskVuMeter` used `panel.P()` on radii that had
already been multiplied by the panel scale. At enlarged host sizes this made
the arc radius proportional to scale squared, clipping the ticks/labels and
drawing an overlong needle across the face. `DuskVuMeterGeometry.hpp` now
converts design radii to screen offsets exactly once. The new 1.5x regression
failed at 225 px before the fix and passes at 150 px after it; the rebuilt
standalone was visually checked at 1.0x and 1.27x with the full -40..+20 arc,
labels and needle contained in the window. This closes meter layout/rendering,
not the separately recorded reference-ballistics measurement gap.

Issue `#240` root cause (2026-09-01, night): pugl's `mac.m` sizes the wrapper view from
`viewScreen(view)` but the GL draw view through its own `convertRectFromBacking:`
-- two scale sources, which disagree in mixed-DPI or not-yet-windowed cases and
put the draw view at half/double size. Fix (draw view takes the wrapper's point
size in `puglSetWindowSize` and `puglRealize`) sits uncommitted on fork branch
`fix/draw-view-follows-wrapper-scale` in `~/projects/DAF/dgl/src/pugl-upstream`;
verified with three probe resize replays. Ship via pugl commit -> DAF submodule
pin -> CI `DAF_REF`. Logic keeping the opening window size on a plugin resize
request is separate host behaviour; hardware faces stay on the 380 canvas.
