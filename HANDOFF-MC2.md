# Multi-Comp 2 / Opto (LA-2A) — handoff, 2026-08-22 snapshot

You are the planner and reviewer; delegate implementation to Codex via the
`/codex` skill. Verify everything before acting — including the claims in this
file. The campaign log memory `mc2-la2a-campaign.md` is the long-form record;
this file is the working state.

**Snapshot authority:** the campaign worktree named in section 2, at committed
HEAD `e0a4f21` plus its listed pre-existing working-tree changes, is the source
for the measurements below. `origin/main` was `c1f9ae4` when this snapshot was
recorded. These committed trees are close but not identical; do not substitute
one for the other or combine files from the unrelated primary checkout.

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

- **Opto only** until finished; then match the reference UI. Bugs found in
  other modes get issues, not fixes (#220 deferred; same
  `external ? sidechain : input` dead-link bug likely in VCA ~line 854 and
  Bus ~line 960 of MultiCompModes.hpp — needs its own issue).
- **The hardware reference is the only oracle.** Old JUCE implementation is
  not a reference; its Opto golden vectors were deleted deliberately.
- **Parameter laws follow the reference** ("stick with parity, the goal is to
  match not deviate"). Controls with no reference equivalent (Stereo Link,
  Mix, Oversampling, Drive) are product decisions.
- Commit/push authorization was granted for THIS session's work and used.
  Treat it as spent: default back to never commit/push without a fresh
  instruction. Adversarial review must run clean before every push.

## 2. Repository state

Worktree (all work happens here):
`/private/tmp/claude-502/-Users-marckorte-projects-dusk-audio-plugins/ea21f9ad-379c-40f2-86e2-dfd1e8d95e07/scratchpad/mc2`

- **Authoritative 2026-08-22 campaign snapshot:** worktree branch
  `multi-comp-2/review-followups` at `e0a4f21`, with its existing uncommitted
  changes preserved. `origin/main` is `c1f9ae4` (merged PR #222). A direct tree
  comparison shows one three-line difference in `MultiCompPlugin.cpp`; the two
  revisions are therefore not content-equal.
- **Historical 2026-08-21 handoff refs:** `main=662d6d5` and review branch
  `bad1c54`. They identify the earlier post-PR-#221 state only and are not the
  source for the current gate table.
- Pre-existing modified files in the authoritative worktree are
  `.github/workflows/daf-build.yml`, `MultiCompModes.hpp`,
  `MultiCompCoreTests.cpp`, `MultiCompPluginLayerTests.cpp`, and
  `MultiCompUI.cpp`. Preserve them; never treat a clean `e0a4f21` checkout as
  the measured tree.
- **Uncommitted, never touch:** `.github/workflows/daf-build.yml` (owner's
  own edit). It also survives in `git stash` as a backup from a rebase.
- Build dir `build-mc2` in the worktree root; configure command if needed:
  ```bash
  cmake -U 'SDL2*' -U 'pkgcfg_lib_SDL2*' -S plugins/multi-comp/daf-plugin \
    -B build-mc2 -DCMAKE_BUILD_TYPE=Release -DDAF_PATH=$HOME/projects/DAF \
    -DDAFWIDGETS_PATH=$HOME/projects/DAF-Widgets -DDUSK_DAF_INSTALL_LOCAL=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
  ```
- DAF fork `~/projects/DAF`: AU render guard already merged to fork main.
  Do not modify. Never repoint CI to upstream DAF.
- Measurements: `~/projects/dusk-audio-tools/plugins/MultiComp/measurements/`
  (17 JSON + probe scripts). Read before measuring anything new.

## 3. Where Opto stands — authoritative 2026-08-22 macOS re-run

| gate | current |
| --- | --- |
| static law (sine), 4 deltas | -0.021 / -0.145 / -0.085 / -0.196 dB |
| broadband static law | fitted RMS 0.073 dB, held-out -24 dBFS +0.016 dB |
| detector weighting | mean -0.237 dB; shape RMS 0.097 dB |
| detector memory, 16 pts | RMS 0.070 dB, worst 0.142 dB |
| make-up taper | worst 0.0029 dB |
| output ceiling | worst 0.230 dB, peak +4.68 dBFS |
| burst-rate sweep, 2/5/10/20/40 Hz | +0.407 / +0.563 / +0.187 / +0.384 / +0.792 dB |
| crest sweep | -0.141 / -0.540 / **+0.615 held out** / -0.709 dB |
| harmonics | fundamental 0.0002 dB; compressed H2-H5 worst 0.071 dB |
| sample-rate invariance | 0.002 dB spread |
| asymmetric stereo | -0.19 / -0.25 dB, symmetric |
| non-Opto golden vectors | unchanged |

The older 2026-08-21 tree was verified to <0.001 dB across
macOS-clang-arm64, Linux-gcc-x86-64, Linux-arm64, and both fp-contract modes.
That result is historical, not verification of the modified 2026-08-22
working tree; rerun the Linux and fp-contract checks before declaring a DSP
change done.

### The remaining emulation gap (open, structural)

The reference's crest response is NON-MONOTONIC (10.19 → 14.07 → 11.33 →
8.52 dB GR at constant -24 dBFS RMS). On the authoritative snapshot our
residuals are -0.141 / -0.540 / +0.615 held out / -0.709 dB; the four-row RMS
is 0.546 dB. The older +0.71/-1.28 dB residuals and 0.96 → 0.75 RMS
comparison describe the historical 2026-08-21 silence-gate trajectory only.
Five mechanisms already tried and rejected WITH numbers (see the crest test's
comment block and campaign log): 0.8 ms integrator, 6000 dB/s cell slew,
0.4 ms reservoir + slew, shorter reservoirs, duration-gated charge cap. Each
fixed one row by breaking burst-rate or a neighbour. A real fix needs
structure the model lacks: two detector paths combining nonlinearly, charge
saturation on very short events, or a non-magnitude rectifier law. Do NOT fit
another correction term on top.

**Dense-programme A/B vs the reference had not been re-run when this snapshot
was recorded.** The +1.66 dB PR-0.7 result and the +0.3 to +0.8 dB interpolated
estimate are both historical 2026-08-21 context, not current measurements.
Measuring this is the top verification task.
Renderer: `~/projects/dusk-audio-plugins/build/tests/duskverb_render/duskverb_render`,
reference AU `/Library/Audio/Plug-Ins/Components/uaudio_teletronix_la-2a_tc.component`,
harness in `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/`.
**Render our build with `--vst3`, never `--au`** (AU resolves through the
global registry and can silently load a stale installed build). GR is always
`output(PR=0) - output(PR=x)` at the same Gain, never input - output.

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

## 5. After the emulation: UI restyle (#212) and geometry (#213)

- Reference UI 950 x 263 (2U, 3.6:1), vector primitives only, NO third-party
  artwork/marks. LA-2A skin on the Opto panel only; other modes and chrome
  keep the Dusk dark theme. Panel readouts: Peak Reduction and Gain 0-100,
  Comp/Limit, meter reads GR.
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
