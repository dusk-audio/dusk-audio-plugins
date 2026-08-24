# Multi-Comp 2 / Opto (LA-2A) — handoff, 2026-08-22 snapshot

You are the planner and reviewer; delegate implementation to Codex via the
`/codex` skill. Verify everything before acting — including the claims in this
file. The campaign log memory `mc2-la2a-campaign.md` is the long-form record;
this file is the working state.

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
