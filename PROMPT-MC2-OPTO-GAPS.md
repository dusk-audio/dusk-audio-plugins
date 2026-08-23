# Task prompt: close the remaining Multi-Comp 2 Opto vs LA-2A gaps

Paste this whole file as the opening message of a fresh session.

---

You are picking up the Multi-Comp 2 (DAF) **Opto mode** emulation campaign. The
goal is unchanged and binding: **Opto must match the UAD Teletronix LA-2A. The
old JUCE Opto implementation is not a reference and never will be.** Anything
that improves a synthetic gate while making the reference match worse is a
failure, not a trade.

## Mandatory reading, before touching anything

First select the campaign worktree, `cd` into it, and verify `git rev-parse HEAD`
and `git status --short --branch` against the dated snapshot below. Read that
worktree's `AGENTS.md` and `CLAUDE.md` completely before modifying code. Read
`HANDOFF-MC2.md` from the same worktree; if it is not present there, use only a
copy whose snapshot-authority block names the same worktree revision. Do not
mix repository instructions, handoffs, or source files from the unrelated
primary checkout into the selected worktree.

Then read:

1. `~/.claude/projects/-Users-marckorte-projects-dusk-audio-plugins/memory/mc2-la2a-campaign.md`
   — long-form campaign log: decisions, reference facts, refuted claims.
2. `HANDOFF-MC2.md` from the selected worktree — authoritative working state
   for the renamed 2026-08-22 snapshot. Its gate table matches "Current measured
   state" below; pre-rename revision references inside it are explicitly
   historical.
3. `~/projects/dusk-audio-tools/plugins/MultiComp/handoff/HANDOFF-2026-08-21.md`
   — the measurement-side handoff, including the seven excluded hypotheses.
4. `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/README.md`
   — harness contract, probe list, the single GR definition.

Treat every root-cause claim in those documents as a **hypothesis**. Six have
already been refuted by re-measurement (fixed-ratio law, 3.34x release split,
~6x exposure stretch, +3.64 dB rebound, even-dominant harmonics, a defective
memory-curve window). Read per-point data, never a verdict sentence.

## Repository state

- **Authoritative renamed snapshot: 2026-08-22.** `origin/main` = `f7edaea`
  (DAF rename, #223); campaign branch
  `multi-comp-2/opto-campaign-snapshot` = `d090a7b`. The branch contains the
  ported Opto gate set; `origin/main` does not.
- Existing worktree with the build dir already configured:
  `/private/tmp/claude-502/-Users-marckorte-projects-dusk-audio-plugins/d3a95f36-6e62-4446-9f5b-32717dcb08a6/scratchpad/mc2-port`
  (branch `multi-comp-2/opto-campaign-snapshot` @ `d090a7b`). Its tracked tree
  is the authoritative port; `b/` is its configured, pre-existing untracked
  build directory and must be preserved. Do not use the unrelated primary
  checkout or the stale pre-rename worktree.
- **Historical pre-rename provenance:** `origin/main=c1f9ae4`; campaign branch
  `multi-comp-2/review-followups=e0a4f21` plus its then-uncommitted changes.
  These revisions explain where the ported measurements came from but are not
  valid worktrees for current changes.
- Opto DSP: `plugins/multi-comp/core/MultiCompModes.hpp` (`processOpto`,
  `OptoState`, the `opto*` coefficient members) and
  `plugins/multi-comp/core/MultiCompDSP.cpp`.
- Gates: `plugins/multi-comp/core/tests/MultiCompCoreTests.cpp` (4110 lines).
- Reference measurements (17 JSON + probe scripts):
  `~/projects/dusk-audio-tools/plugins/MultiComp/measurements/`. **Read these
  before generating any new reference render** — most questions are already
  answered on disk.

Build and run the core suite (verified working, ~4 s compile, macOS):

```bash
c++ -O2 -std=c++17 -Iplugins/multi-comp/core -Iplugins/shared-daf/dsp \
  plugins/multi-comp/core/tests/MultiCompCoreTests.cpp \
  plugins/multi-comp/core/MultiCompDSP.cpp -o /tmp/mc2tests && /tmp/mc2tests
```

Linux parity check (mandatory before declaring any DSP change done — a
macOS-only run once hid a 1.5 dB platform split):

```bash
podman machine start   # if needed
podman run --rm --arch amd64 -v "$PWD/plugins:/src/plugins:ro" \
  docker.io/library/gcc:12 bash -c \
  'g++ -O3 -std=c++17 -I/src/plugins/multi-comp/core \
   -I/src/plugins/shared-daf/dsp \
   /src/plugins/multi-comp/core/tests/MultiCompCoreTests.cpp \
   /src/plugins/multi-comp/core/MultiCompDSP.cpp -o /tmp/t && /tmp/t'
```

Second tripwire on macOS: rebuild with `-ffp-contract=off` and diff the gate
outputs against the normal build. Any gate that moves is a rounding-difference
amplifier — find the branch, don't retune around it.

Known baseline exception measured on the 2026-08-22 follow-up: all three core
runs pass, and Linux GCC 12 matches no-contract macOS detector-weighting rows
within 0.000229 dB. Default macOS differs only in the low-frequency weighting
rows by up to 0.004563 dB (shape RMS 0.096646 default, 0.098245 no-contract,
0.098219 Linux). This is not permission to widen gates, and future DSP work
must still run the full matrix.

## Current measured state (re-run 2026-08-22, macOS clang, `d090a7b` renamed snapshot)

Suite: **PASS**. These are the same authoritative snapshot recorded in
`HANDOFF-MC2.md`.

Steady state, effectively solved — do not spend time here:

| gate | error vs reference |
|---|---|
| make-up gain taper, 16 points | worst 0.0029 dB |
| static law, sine, -40…-4 dBFS | -0.021 / -0.145 / -0.085 / -0.196 dB |
| broadband static law | fitted RMS 0.073 dB, held-out -24 dBFS +0.016 dB |
| output ceiling | worst 0.230 dB, max-drive peak +4.68 dBFS |
| harmonics | fundamental 0.0002 dB; compressed H2-H5 worst 0.071 dB |
| detector memory, 16 gaps 1 ms-2 s | RMS 0.070 dB, worst 0.142 dB |
| sample-rate invariance | 0.002 dB spread |
| stereo link, asymmetric | -0.19 / -0.25 dB, symmetric |

Open gaps, in the order they should be attacked:

| # | gap | current numbers |
|---|---|---|
| 1 | dense-programme A/B | +0.224 / +0.033 / +1.000 / +1.488 / +2.012 dB at PR 0.4/0.55/0.7/0.85/1.0; material crest 17.795 dB |
| 2 | crest sweep residuals (structural) | -0.141 / -0.540 / **+0.615 held out** / -0.709 dB |
| 3 | short-event release decay | RMS 0.701, worst **1.386 dB** |
| 4 | short-event charge | fitted RMS 0.414 worst 0.716; held-out RMS 0.301 worst 0.401 dB |
| 5 | burst-rate **one-sided** bias, 2-40 Hz | +0.407 / +0.563 / +0.187 / +0.384 / +0.792 dB (all positive, mean +0.47); arithmetic audit clean, structural gap remains |
| 6 | detector weighting absolute anchor (resolved) | mean -0.237 dB, 1 kHz -0.174 dB, shape RMS 0.097 dB; all three are now guarded |
| 7 | Limit-mode charge t90 at the extreme | PR 1.0 @ -3 dBFS / 1 s: ref 36.0 ms vs ours 10.9 ms (-25.1 ms, held out); this is charge, not release |
| 8 | crest comment block (resolved) | now records -0.141/-0.540/+0.615/-0.709 dB and labels the older trajectory historical |

## Work order

### Step 1 — measured baseline (complete; repeat after every DSP change)

The renamed `d090a7b` VST3 was re-run against the live reference AU on material
with 17.794665 dB crest. The authoritative sweep is:

| PR | reference GR | our GR | total-RMS residual | frame p50 | frame p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0.40 | 1.664 | 1.887 | +0.224 | +0.264 | +0.805 |
| 0.55 | 5.956 | 5.989 | +0.033 | +0.152 | +0.881 |
| 0.70 | 12.928 | 13.927 | +1.000 | +1.196 | +2.239 |
| 0.85 | 19.286 | 20.774 | +1.488 | +1.656 | +2.843 |
| 1.00 | 21.192 | 23.204 | +2.012 | +2.206 | +3.403 |

Total-RMS residual mean/worst/RMS is +0.951/+2.012/1.209 dB. Mine-side
re-renders independently reproduce our PR-0.55 and PR-0.85 GR as 5.989099 and
20.774064 dB. The campaign table reports PR 1.0 as +2.012 dB; the earlier
artifact-level calculation was +2.012752 dB, a 0.000752 dB reporting difference
with no gate impact. Input-duration-only scoring on the earlier three-row sweep
differs by at most 0.001445 dB, excluding render tail length as the cause. The
historical +0.3 to +0.8 dB crest-interpolated prediction is falsified: the near
match at PR 0.4-0.55 becomes monotonic over-compression above PR 0.55. Repeat
the five-point measurement after any DSP change.

- renderer: `~/projects/dusk-audio-plugins/build/tests/duskverb_render/duskverb_render`
- reference: `/Library/Audio/Plug-Ins/Components/uaudio_teletronix_la-2a_tc.component`
  (PACE-wrapped; instantiates fine under the renderer)
- harness: `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/`
  — `gen_stimuli.py` makes `dense_program.wav`; `probe_factory_presets.py`
  (C1.9) and `compare.py` are the programme paths; `mine_contract.py` runs first
  for the A/B phase; env prefix `MC2_*`.
- **Render our build with `--vst3`, never `--au`.** AU resolves through the
  global registry and will silently measure a stale installed build — this
  already cost one whole A/B that nearly got recorded as a DSP defect.
- **GR is always `output(PR=0) - output(PR=x)` at the same Gain**, never
  input minus output.
- Report all five programme deltas at PR 0.4/0.55/0.7/0.85/1.0, their frame
  p50/p95 residuals, and the crest of the material.

### Step 2 — the cheap, provably-bounded items

Do these next; they are small and independent of the structural work.

- **Gap 8 — complete**: the crest comment now records the current residuals and
  explicitly labels the older trajectory historical. Thresholds were not
  loosened.
- **Gap 5 — arithmetic audit complete, gap remains**: all five periods divide
  48 kHz exactly, each burst is exactly 480 samples, and reference/control use
  the same stimulus and 6.0-7.5 s scoring window. No divisor, count, or rate
  mismatch was found; do not retune speculatively from the one-sided sign.
- **Gap 6 — complete**: the shape score correctly removes its mean, while new
  absolute assertions pin both the -0.237425 dB mean and -0.174332 dB 1 kHz
  offsets. The static sine and broadband gates independently own level
  accuracy. A deliberately broken integration calibration failed the new
  assertion before the original constant was restored and the suite passed.
- **Gap 7 — classified**: this gate's t90 is charge/attack, not release. At PR
  1.0 / -12 dBFS its residual changes from +0.604 ms at 10 ms exposure to
  -10.437 ms at 100 ms and -12.333 ms at 1 s; the held-out PR 1.0 / -3 dBFS /
  1 s row is -25.104 ms. Treat it with gap 4, not gap 3.

### Step 3 — the structural gap (gaps 2, 3, 4)

The reference's crest response is **non-monotonic**: 10.19 → 14.07 → 11.33 →
8.52 dB GR at constant -24 dBFS RMS. A one-pole integrator into a static law
cannot produce that shape. Our model reproduces the shape but not the residuals.

**Already excluded by measurement — do not re-investigate:** stereo linking,
detector weighting shape, duty cycle at fixed rate, level variation between
events, metric windowing, the broadband static law, accumulation across repeated
events, a 0.8 ms integrator, 6000 dB/s cell slew, a 0.4 ms reservoir plus slew,
shorter reservoirs, a duration-gated charge cap, scaling the repetition blend,
and a power/RMS rectifier. Scaling repetition blend to 75% raised fitted crest
RMS from 0.521 to 1.338 dB; the calibrated power rectifier raised short-event
recovery RMS from 0.701056 to 1.032525 dB and worst error from 1.385892 to
3.064520 dB. Both were reverted. A dense-stimulus diagnostic then found the
integrated path already controls `min(peak, integrated)` in 92.0733% of blocks,
with mean path separation increasing from +1.219 dB in the loudest input-RMS
quintile to +2.784 dB in the quietest. Parameter-free below-minimum competition
(`min^2/max`) worsened crest fitted RMS 0.521 -> 1.131 dB and worst error
0.709 -> 1.903 dB, so nonlinear two-path competition was also reverted.

A temporary five-PR dense diagnostic also found cell state above the current
target in 84.181-87.921% of 16-sample blocks. Mean retained excess was
0.960528/1.791087/2.097717/2.108555/2.006334 dB at PR
0.4/0.55/0.7/0.85/1.0. At PR 1.0 the fast/mid/slow contributions were
0.663660/0.860702/0.534533 dB, and `repetitionBlend` was zero throughout. The
error is not owned by one charge population or the repetition path; the
temporary diagnostic scaffolding was reverted.

The Compress-mode PR-1.0/-24 dBFS/1 s attack report reaches t90 in 9.493 ms
versus 15.917 ms on the reference. Lowering the existing high-drive fast-rate
pivot from 18 to 15 dB was tested as a one-knob slowdown, but short-event charge
fitted RMS/worst worsened from 0.414244/0.716022 to 0.469745/0.994778 dB and
failed before programme scoring. The pivot was restored; do not repeat this
simple rate change.

**Do not fit another correction term on top of the existing model.** A real fix
now needs a new reference discriminator. The source already implements
capacity-limited charge in `followTarget`: attack rate depends on remaining
capacity with fast/slow exponents 5.1/1.5 and a fast minimum rate of 0.150.
Adding another charge reservoir from the existing single-event and periodic
burst rows would be an unidentifiable correction, not a measured mechanism.

Before another DSP edit, localise the newly measured PR axis. At PR
0.4/0.55/0.7/0.85/1.0, run the same level- and crest-controlled dynamic
stimulus and a stationary control, always with a PR=0 render at the same Gain.
Record total-RMS GR, frame p50/p95, and event-aligned attack/inter-event
residuals. The discriminator is whether the divergence acquired above PR 0.55
appears during charge or persists through the gaps. The paired-event
charge-capacity matrix is deferred unless this PR sweep specifically shows
history-dependent retention.

State the hypothesis and the experiment that would falsify it **before** editing
anything structural. One knob per iteration, fresh measurement between. If a
change improves its own gate and worsens another, it is a symptom fix — revert
it and say so.

**A negative result with a root cause is a deliverable.** "Mechanism X is
excluded, here is the per-point evidence" is a better outcome than a forced
marginal win, and it belongs in the campaign log where the next session looks.

## Hard scope walls

- **DSP and per-mode panels: Opto mode only.** Bugs found in the other seven
  modes get GitHub issues, not fixes. Known and deferred: #220, plus the same
  `external ? sidechain : input` dead-link bug that likely exists in VCA
  (~line 854) and Bus (~line 960) of `MultiCompModes.hpp` — that needs its own
  issue, not a patch in this work. Owner-directed shell conformity is the sole
  cross-mode UI exception: the visible Global/Sidechain bands are removed and
  the shared header follows TapeMachine 2 / Tape Echo 2, while their parameters
  remain available to host state and automation.
- **Keep UI and DSP iterations isolated.** On 2026-08-22 the owner authorised
  the Opto panel restyle (#212) to proceed in parallel while reference DSP
  measurements are pending. The later 2026-08-22 shell-conformity direction is
  implemented in the working tree as a 1120 x 486 header/layout change; do not
  treat it as authority to alter the seven other mode panels. Do not mix visual
  edits into a DSP experiment. Geometry-contract verification (#213) remains a
  separate host-validation task.
- **Idle-state harmonics are out of scope.** Worst idle error is 1.86 dB on H5
  at roughly -110 dBc. Inaudible. Leave it.
- **Do not touch** `.github/workflows/daf-build.yml` — it carries the owner's
  selective dispatch edit, committed in the campaign snapshot but outside this
  task.
- **DAF and DAF-Widgets are HARD FORKS. `DISTRHO/DPF` and
  `DISTRHO/DPF-Widgets` are not references — never read, diff, cherry-pick,
  merge from, or cite upstream, and never point any build or CI at it.** Ours
  are `dusk-audio/DAF` and `dusk-audio/DAF-Widgets`; CI pins them at
  `788eb019` and `91e0004e`. There is no "upstream fixed it" path.
- **Do not modify the fork checkout at `~/projects/DAF`** either. The build
  consumes it via `-DDAF_PATH=$HOME/projects/DAF`; a local edit makes your build
  disagree with CI, which clones the pinned ref — you would be measuring a
  framework nobody else has. If a build breaks, fix the plugin code. The local
  framework checkouts and CI are currently aligned at `788eb019` and `91e0004e`;
  advancing either pin is not part of this task.
- Do not re-apply the review findings that were declined with reasons (UI
  minimum size, deriving test thresholds from production constants, splitting
  fast/slow test targets, PR=0 baseline caching). They are listed with rationale
  in `HANDOFF-MC2.md` §4.

## Verification obligations

A change is not done until all of these are true and their **real output** is in
your report:

1. Full core suite passes on macOS, with the before/after numbers for every gate
   the change could touch.
2. Full core suite passes on Linux via the podman recipe, matching macOS to
   <0.001 dB.
3. The `-ffp-contract=off` macOS build agrees with the normal build.
4. The dense-programme A/B is re-measured against the reference AU after the
   change, not predicted from the gates.
5. The seven non-Opto golden vectors are unchanged.

"It builds" and "should be unaffected" are not verification. Bit-null means
byte-compared renders. Never-worse means a re-measured baseline on the current
build, not a remembered number.

## Process rules

- **Never commit or push.** The owner makes every commit. The commit
  authorization granted in the 2026-08-21 session was for that session and is
  spent.
- If the owner does authorize a push, an adversarial review must run clean
  first — every time, not only the first time.
- Ask what each new gate structurally **cannot** see. That question has already
  caught four real defects: a dual-mono corpus hid a dead stereo link, sine-only
  hid a 0.9 dB broadband error, a 13 dB crest ceiling hid the 20 dB crest gap,
  and macOS-only CI hid a 1.5 dB platform split.
- Never gate DSP on instantaneous `|sample|` against an absolute epsilon. Use an
  envelope-relative floor plus a short hold. Any binary branch whose consequence
  is margin-independent (hold vs discharge) amplifies rounding differences.

## Report format

Terse. Numbers, not adjectives. For each gap: what you measured, the per-point
data, what you changed, the before/after on every affected gate, and what is
still open. Report your own errors plainly and move on. When two gates conflict,
stop optimising and ask what single mechanism produces both readings.
