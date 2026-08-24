# Task prompt: close the remaining Multi-Comp 2 Opto vs LA-2A gaps

Paste this whole file as the opening message of a fresh session.
Rewritten 2026-08-24; supersedes every earlier copy of this prompt. Nothing in
`~/projects/dusk-audio-tools/plugins/MultiComp/handoff/` is authoritative unless
listed in the mandatory reading below — several files there still assert claims
from the refuted list in this document.

---

You are picking up the Multi-Comp 2 (DAF) **Opto mode** emulation campaign. The
goal is unchanged and binding: **Opto must match the UAD Teletronix LA-2A. The
old JUCE Opto implementation is not a reference and never will be.** Anything
that improves a synthetic gate while making the reference match worse is a
failure, not a trade.

## Mandatory reading, before touching anything

First `cd` into the campaign worktree and verify `git rev-parse HEAD` and
`git status --short --branch` against the snapshot below. Read that worktree's
`AGENTS.md` and `CLAUDE.md` completely before modifying code — where the two
disagree, `CLAUDE.md` wins (AGENTS.md says so itself). Do not mix instructions, handoffs,
or source files from the unrelated primary checkout into the worktree.

Then read:

1. `~/.claude/projects/-Users-marckorte-projects-dusk-audio-plugins/memory/mc2-la2a-campaign.md`
   — long-form campaign log: decisions, reference facts, refuted claims.
2. `HANDOFF-MC2.md` from the worktree — the working-state record, including the
   2026-08-22 follow-up audit and its excluded mechanisms.
3. `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/README.md`
   — harness contract, probe list, the single GR definition.

Treat every root-cause claim in those documents as a **hypothesis**. Six have
already been refuted by re-measurement (fixed-ratio law, 3.34x release split,
~6x exposure stretch, +3.64 dB rebound, even-dominant harmonics, a defective
memory-curve window). Read per-point data, never a verdict sentence.

## Repository state (2026-08-24)

- **The Opto campaign snapshot is MERGED.** `origin/main` = `582ab99` (PR #228;
  #225-#227 landed earlier the same way). The DSP measured below is what main
  ships.
- Campaign branch `multi-comp-2/opto-campaign-snapshot` merged to main through
  PR #228 (`582ab99`): `ff57a16` dense-programme parity gate, `0110ff9` AU
  sidechain-bus tests, `8d602d7` 2x pin. The branch now carries the PR
  0.85/1.00 gate rows and this document refresh (2026-08-24).
  The OPTO code paths (processOpto, opto coefficients, linked detector) are
  unchanged since the authoritative `d090a7b` measurement state, so every Opto
  number below is current; Bus/VCA detector fixes landed after it via PR #224.
  Re-measure on the current build before using any row as the baseline for a
  DSP decision — deduction is not measurement.
- **Worktree (all work happens here):** `/private/tmp/dusk-audio-plugins-opto-campaign`
  (branch `multi-comp-2/opto-campaign-snapshot`). The worktree path
  named in older copies of this prompt
  (`.../d3a95f36-.../scratchpad/mc2-port`) is GONE — do not recreate it.
- The primary checkout `~/projects/dusk-audio-plugins` has a stale local `main`
  (`f7edaea`) and sits on an already-merged fix branch. Leave it alone.
- Opto DSP: `plugins/multi-comp/core/MultiCompModes.hpp` (`processOpto` at
  ~:646-1105, `OptoState`, the `opto*` coefficient members) and
  `plugins/multi-comp/core/MultiCompDSP.cpp` (linked detector ~:728).
- Gates: `plugins/multi-comp/core/tests/MultiCompCoreTests.cpp`; plugin-layer
  and AU gates in `plugins/multi-comp/daf-plugin/`.
- Reference measurements (17 JSON + probe scripts):
  `~/projects/dusk-audio-tools/plugins/MultiComp/measurements/`. **Read these
  before generating any new reference render** — most questions are already
  answered on disk.
- DAF fork pins live in `.github/workflows/daf-build.yml` (`DAF_REF` /
  `DAFWIDGETS_REF`), mirrored in daf-release.yml and daf-au-test.yml — read
  them there, do not trust a doc's copy. Hard forks; never read or build
  against upstream DISTRHO/DPF, never modify `~/projects/DAF` or
  `~/projects/DAF-Widgets`. Advancing either pin is not part of any Opto task;
  if a build breaks, fix the plugin code.

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
amplifier — find the branch, don't retune around it. Known baseline exception:
default macOS differs from Linux/no-contract only in the low-frequency
weighting rows by up to 0.004563 dB; not permission to widen gates.

## Current measured state (2026-08-22 macOS clang re-run; Opto DSP verified unchanged since `d090a7b` by diff)

Suite: **PASS** (macOS default, macOS no-contract, Linux GCC 12).

Steady state, effectively solved — do not spend time here:

| gate | error vs reference |
|---|---|
| make-up gain taper, 16 points | worst 0.0029 dB |
| static law, sine, -40…-4 dBFS | -0.021 / -0.145 / -0.085 / -0.196 dB |
| broadband static law | fitted RMS 0.073 dB, held-out -24 dBFS +0.016 dB |
| output ceiling | worst 0.230 dB, max-drive peak +4.68 dBFS |
| harmonics | fundamental 0.0002 dB; compressed H2-H5 worst 0.071 dB |
| detector weighting | shape RMS 0.097 dB; mean -0.237 and 1 kHz -0.174 dB anchors gated |
| detector memory, 16 gaps 1 ms-2 s | RMS 0.070 dB, worst 0.142 dB |
| sample-rate invariance | 0.002 dB spread |
| stereo link, asymmetric | -0.19 / -0.25 dB, symmetric |

The Opto UI restyle (#212) is complete and merged: 1120x310 faceplate
(3.613:1), needle meter with 300 ms display ballistics, COMP/LIMIT lever,
ridged 0-100 knobs, set-screw Mix and SC-HP trims, preset integration,
conforming header. `design-qa.md` records the passed visual QA. UI is not part
of this task except the flagged owner-decision items in the work order.

Open gaps, in attack order:

| # | gap | current numbers |
|---|---|---|
| 1 | dense-programme A/B, **monotonic in PR on THIS stimulus — THE gap** (the Step 1 localisation probe, a different stimulus, is non-monotonic in PR — see Step 1) | renderer A/B, unwindowed total-RMS GR residual: +0.224 / +0.033 / **+1.000 / +1.488 / +2.012 dB** at PR 0.4/0.55/0.7/0.85/1.0, crest 17.795 dB. (The in-test 100 ms-frame means read higher — see the test's comment block.) |
| 2 | crest sweep residuals (structural, same mechanism suspected) | -0.141 / -0.540 / **+0.615 held out** / -0.709 dB |
| 3 | short-event release decay | RMS 0.701, worst 1.386 dB |
| 4 | short-event charge (owns the Limit t90 extreme: PR 1.0 / -3 dBFS / 1 s is -25.104 ms, charge not release) | fitted RMS 0.414 worst 0.716; held-out RMS 0.301 worst 0.401 dB |
| 5 | burst-rate one-sided bias, 2-40 Hz | +0.407 / +0.563 / +0.187 / +0.384 / +0.792 dB (all positive; arithmetic audit clean; do not retune speculatively from the one-sided sign) |

Authoritative dense-programme sweep (renderer A/B vs live reference AU):

| PR | reference GR | our GR | total-RMS residual | frame p50 | frame p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0.40 | 1.664 | 1.887 | +0.224 | +0.264 | +0.805 |
| 0.55 | 5.956 | 5.989 | +0.033 | +0.152 | +0.881 |
| 0.70 | 12.928 | 13.927 | +1.000 | +1.196 | +2.239 |
| 0.85 | 19.286 | 20.774 | +1.488 | +1.656 | +2.843 |
| 1.00 | 21.192 | 23.204 | +2.012 | +2.206 | +3.403 |

Near-parity at PR ≤ 0.55 becoming monotonic over-compression above it makes
**PR a missing dynamic axis**. The single-PR crest gate structurally cannot
predict this, and the static-law gates exclude a steady-state curve error.

## Work order

### Step 0 — verify state

In the worktree: branch is `multi-comp-2/opto-campaign-snapshot`;
`git log --oneline -2` shows "gate Opto dense-programme parity at PR 0.85 and
1.00" and "refresh the Opto campaign handoff documents" (or descendants); tree
clean apart from work you were asked to do; and the macOS core suite passes
before any edit. If any of that disagrees, stop and report.

### Step 1 — PR-axis localisation: COMPLETE (2026-08-24)

Measured with `probe_pr_axis_localisation.py` (tools repo harness), canonical
manifests, both devices re-rendered: dynamic residual -0.408/-0.638/+1.041/
+1.867/+1.002 dB at PR 0.4/0.55/0.7/0.85/1.0, stationary -0.038..-0.555,
attack 0-2 ms up to +2.276, gap 2-10 ms up to +4.946 and 10-30 ms up to
+3.751 dB (PR 1.0); the 30-60 ms band falls to +0.36..+0.69 dB at PR <= 0.85
but is still +1.467 dB at PR 1.0. **Within the probe's 60 ms inter-event
window the error is acquired at the event boundary and dissipates — retention
BEYOND 60 ms is a declared blind spot of this probe, not excluded.** Note the
probe's dynamic residual is NON-monotonic in PR (+1.867 at 0.85, +1.002 at
1.0) while the dense-programme gap is monotonic — a stimulus dependence the
mechanism must explain, not ignore. Full report: harness `report/pr_axis_localisation.{json,md}`,
including the probe's structural blind spots. Two traps found and handled:
float-WAV PEAK chunks carry a write timestamp (manifests now hash sample
bytes), and `MC2_MINE_AU` must be exported explicitly — its default silently
resolves to the primary checkout's stale VST3 (the 102-parameter readback
contract catches this).

The original experiment definition, for re-runs at other operating points:

For each PR in {0.4, 0.55, 0.7, 0.85, 1.0}, run the same level- and
crest-controlled dynamic stimulus and a stationary control, always retaining
the PR=0 render at the same Gain. Report total-RMS GR, frame p50/p95, and
**event-aligned attack and inter-event residuals**. The discriminator: does the
divergence above PR 0.55 appear **while charging** on events, or does it
**persist through the gaps** (history-dependent retention)?

Division of labour is fixed: **you cannot host the reference AU** (the sandbox
blocks AudioComponent registration). You write the instruments — stimulus
generation, event-aligned extraction, residual reporting — as scripts in the
harness at `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/`
following its existing conventions (`MC2_*` env, Python 3.9 — no `match`, no
`X | Y` types, no `zip(strict=)`). Claude runs every reference render and
returns the outputs. A render failure on your side is not a finding.

Render rules, non-negotiable:
- our build renders with `--vst3 <path>`, never `--au` (the AU registry
  silently substitutes installed builds; this once corrupted a whole A/B)
- our build's VST3 is the worktree's `build-validation/bin/multi_comp_2.vst3`
  (configure with the cmake recipe in HANDOFF-MC2.md §2, `-B build-validation`);
  export `MC2_MINE_AU` to that path explicitly — the harness default silently
  resolves to the primary checkout's stale VST3, and only the 102-parameter
  readback contract stands between that and a corrupted A/B
- GR is always `output(PR=0) - output(PR=x)` at the same Gain, never
  input - output
- renderer `~/projects/dusk-audio-plugins/build/tests/duskverb_render/duskverb_render`;
  reference `/Library/Audio/Plug-Ins/Components/uaudio_teletronix_la-2a_tc.component`
- identical extraction on both sides; any change re-derives the reference side

### Step 2 — the structural mechanism (the first real task; Step 1 is complete)

The reference's crest response is **non-monotonic**: 10.19 → 14.07 → 11.33 →
8.52 dB GR at constant -24 dBFS RMS. Our model reproduces the shape but not the
residuals, and the programme error is PR-dependent on top.

**Already excluded by measurement — do not re-investigate or re-try:**
stereo linking, detector weighting shape, duty cycle at fixed rate, level
variation between events, metric windowing, the broadband static law,
accumulation across repeated events, a 0.8 ms integrator, 6000 dB/s cell slew,
a 0.4 ms reservoir plus slew, shorter reservoirs, a duration-gated charge cap,
scaling the repetition blend (crest fitted RMS 0.521 → 1.338 dB), a calibrated
power/RMS rectifier (short-event recovery RMS 0.701 → 1.033, worst 1.386 →
3.065 dB), parameter-free below-minimum path competition `min²/max` (crest
fitted RMS 0.521 → 1.131 dB), and a one-knob high-drive fast-rate pivot change
18 → 15 dB (short-event charge 0.414/0.716 → 0.470/0.995 dB). All were
reverted with numbers; the details are in `HANDOFF-MC2.md`.

Also measured and on file: the integrated path already controls
`min(peak, integrated)` in 92.07% of dense-stimulus blocks; cell state ends
above target in 84-88% of blocks with the excess spread across all three
populations (no single owner); `repetitionBlend` was zero throughout. The
model already implements capacity-limited charge (`followTarget`, exponents
5.1/1.5, fast minimum rate 0.150) — **adding another reservoir or fitting
another correction term on top is explicitly prohibited** until a
measured mechanism — not merely the event-boundary location — is identified.

State the hypothesis and its falsifying experiment **before** editing anything
structural. One knob per iteration, fresh measurement between. If a change
improves its own gate and worsens another, it is a symptom fix — revert it and
say so. A negative result with a root cause is a deliverable.

### Step 3 — coverage hardening (can interleave with Step 2)

- DONE 2026-08-24: PR 0.85/1.00 envelopes captured (grid validated to
  0.0005 dB against the 0.40/0.70 constants) and landed in
  `testOptoDenseProgrammeParity`; verified on macOS clang, macOS
  -ffp-contract=off, and Linux gcc 12.
- After the structural fix lands, tighten `testOptoDenseProgrammeParity`'s
  ceilings — the current values live in that test's comment block. The PR
  0.40/0.70 rows are parity gates; the PR 0.85/1.00 rows are gap tripwires to
  be tightened toward parity.
- Every new gate: state what it structurally cannot see, and break it first —
  an assertion never seen to fail is not a test.

### Step 4 — owner-decision items (flag, do not build)

Report these for the owner to decide; none is authorised yet:

1. **Meter-mode switch (GR / +4 / +10):** deliberately not implemented — the
   output bridge publishes block peak and no RMS integration or +4/+10
   calibration has been specified or measured from the reference.
2. **Faceplate power switch:** absent; the shell BYPASS covers the function.
   Cosmetic decision.
3. **Host-side parameter unit** for Opto Peak Reduction/Gain is still `"%"`
   (`plugins/multi-comp/daf-plugin/MultiCompParams.hpp:56` — not the core
   file of the same name) while the faceplate is unitless 0-100 — DAW
   parameter displays disagree with the panel. Host-visible change; needs a nod.
4. **#213 geometry fix** is merged but unverified in Logic (AU cache needs a
   restart/rescan — owner action, not yours).

## Hard scope walls

- **DSP and per-mode panels: Opto only.** Bugs in the other seven modes get
  issues, not fixes (#220 was closed 2026-08-23 by the same fix). The `external ? sidechain : input`
  dead-link bug was FIXED for VCA and Bus by 452b13b (PR #224); the one
  remaining instance is processStudioVCA (`MultiCompModes.hpp:1443`) — audit
  it via its own issue, not a patch here.
- Keep UI and DSP iterations isolated; do not mix visual edits into a DSP
  experiment.
- Idle-state harmonics are out of scope (worst 1.86 dB on H5 at ~-110 dBc).
- Do not touch `.github/workflows/daf-build.yml` (owner's dispatch edit).
- Do not modify `~/projects/DAF` / `~/projects/DAF-Widgets` or point builds
  anywhere else; upstream DISTRHO is not a reference for anything.
- Do not re-apply review findings declined with reasons (`HANDOFF-MC2.md` §4:
  UI minimum size, test thresholds from production constants, split test
  targets, PR=0 baseline caching).
- Opto parameters stay as they are. Parameter laws follow the reference
  ("stick with parity, the goal is to match not deviate"); controls with no
  reference equivalent (Stereo Link, Mix, Oversampling, Drive) are product
  decisions.
- **Never `git commit`, `git push`, `git add`.** The owner makes every commit.
  Leave work in the working tree and report it. Adversarial review must run
  clean before any owner-authorised push — every time.

## Verification obligations

A change is not done until all of these are true and their **real output** is
in your report:

1. Full core suite passes on macOS, with before/after numbers for every gate
   the change could touch.
2. Full core suite passes on Linux via the podman recipe, with per-gate
   numbers diffed against macOS: agreement within the documented contraction
   baseline (<=0.005 dB on the weighting rows). A gate that moves more is a
   rounding-difference amplifier to find, not a tolerance to widen.
3. The `-ffp-contract=off` macOS build agrees with the normal build.
4. The dense-programme A/B is re-measured against the reference AU after the
   change (rendered by Claude), not predicted from the gates — the FULL
   five-point PR sweep (0.4/0.55/0.7/0.85/1.0) with frame p50/p95 and material
   crest. A single-PR re-measure structurally cannot see the monotonic-in-PR
   gap.
5. The seven non-Opto golden vectors are unchanged.

"It builds" and "should be unaffected" are not verification. Bit-null means
byte-compared renders. Never-worse means a re-measured baseline on the current
build, not a remembered number.

## Report format

Terse. Numbers, not adjectives. For each gap: what you measured, the per-point
data, what you changed, the before/after on every affected gate, and what is
still open. Report your own errors plainly and move on. When two gates
conflict, stop optimising and ask what single mechanism produces both readings.
