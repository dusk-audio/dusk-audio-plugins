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
   for the 2026-08-22 snapshot. Its gate table matches "Current measured state"
   below; older revision references inside it are explicitly historical.
3. `~/projects/dusk-audio-tools/plugins/MultiComp/handoff/HANDOFF-2026-08-21.md`
   — the measurement-side handoff, including the seven excluded hypotheses.
4. `~/projects/dusk-audio-tools/plugins/MultiComp/tests/reference_comparison/README.md`
   — harness contract, probe list, the single GR definition.

Treat every root-cause claim in those documents as a **hypothesis**. Six have
already been refuted by re-measurement (fixed-ratio law, 3.34x release split,
~6x exposure stretch, +3.64 dB rebound, even-dominant harmonics, a defective
memory-curve window). Read per-point data, never a verdict sentence.

## Repository state

- **Authoritative snapshot: 2026-08-22.** `origin/main` = `c1f9ae4`
  (Multi-Comp 2 post-merge review follow-ups, #222).
  The local `main` branch in `~/projects/dusk-audio-plugins` is **stale** at
  `40f10f8` — fetch before branching off it.
- Existing worktree with the build dir already configured:
  `/private/tmp/claude-502/-Users-marckorte-projects-dusk-audio-plugins/ea21f9ad-379c-40f2-86e2-dfd1e8d95e07/scratchpad/mc2`
  (branch `multi-comp-2/review-followups` @ `e0a4f21`, plus its pre-existing
  working-tree changes). Its committed tree is not content-equal to
  `origin/main`: a direct comparison shows a three-line difference in
  `MultiCompPlugin.cpp`. Reuse this exact worktree and preserve its dirty state,
  or reproduce and document the snapshot deliberately; do not work in the
  primary checkout, which is on an unrelated branch.
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

## Current measured state (re-run 2026-08-22, macOS clang, `e0a4f21` worktree snapshot)

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
| 1 | dense-programme A/B **not re-measured** since the silence-gate fix | stale: +1.66 dB over-compression, PR 0.7 |
| 2 | crest sweep residuals (structural) | -0.141 / -0.540 / **+0.615 held out** / -0.709 dB |
| 3 | short-event release decay | RMS 0.701, worst **1.386 dB** |
| 4 | short-event charge | fitted RMS 0.414 worst 0.716; held-out RMS 0.301 worst 0.401 dB |
| 5 | burst-rate **one-sided** bias, 2-40 Hz | +0.407 / +0.563 / +0.187 / +0.384 / +0.792 dB (all positive, mean +0.47) |
| 6 | detector weighting **mean offset** | -0.237 dB mean, removed by the gate; shape RMS 0.097 dB; raw worst -0.524 dB @ 20 Hz |
| 7 | Limit-mode t90 at the extreme | PR 1.0 @ -3 dBFS: ref 36.0 ms vs ours 10.9 ms (-25.1 ms, held out) |
| 8 | stale comment block in `testOptoCrestSweep` | claims -0.20/-0.26/+0.71/-1.28 and four-row RMS 0.75; actual is now -0.141/-0.540/+0.615/-0.709, four-row RMS ~0.55 |

## Work order

### Step 1 — measure before editing (blocking, no DSP changes until done)

Re-run the dense-programme A/B against the live reference AU. Nothing else
starts until this number exists, because every remaining decision depends on
whether the programme error is still +1.66 dB or now +0.3 to +0.8 dB as the
current crest rows predict.

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
- Report the programme delta at PR 0.7 and at least two other PR settings, plus
  the crest of the material each was measured on.

### Step 2 — the cheap, provably-bounded items

Do these next; they are small and independent of the structural work.

- **Gap 8**: re-measure, then correct the stale comment block in
  `testOptoCrestSweep`. Do not loosen the `require()` thresholds to match — if
  the residuals improved, the thresholds may tighten, never widen.
- **Gap 5**: five of five burst-rate rows err positive. A one-sided residual is
  a constant, not noise. Before sweeping anything, grep the burst-rate path for
  a hardcoded divisor, count, or rate constant — a suspiciously clean bias is
  an arithmetic bug far more often than a tuning problem.
- **Gap 6**: decide whether the -0.237 dB detector-weighting mean offset is a
  real level error in the detector path or an artifact of that gate's operating
  point. The gate currently subtracts it, which means a real 0.24 dB error would
  be invisible. Answer with a measurement, then either fix the DSP or document
  in the test why removing the mean is correct.
- **Gap 7**: the Limit t90 is 3.3x fast at PR 1.0 / -3 dBFS. Establish whether
  this is one extreme corner or the visible end of a release-law error that also
  shows up in gaps 3 and 4 — a shared mechanism would change the priority of the
  structural work below.

### Step 3 — the structural gap (gaps 2, 3, 4)

The reference's crest response is **non-monotonic**: 10.19 → 14.07 → 11.33 →
8.52 dB GR at constant -24 dBFS RMS. A one-pole integrator into a static law
cannot produce that shape. Our model reproduces the shape but not the residuals.

**Already excluded by measurement — do not re-investigate:** stereo linking,
detector weighting shape, duty cycle at fixed rate, level variation between
events, metric windowing, the broadband static law, accumulation across repeated
events, a 0.8 ms integrator, 6000 dB/s cell slew, a 0.4 ms reservoir plus slew,
shorter reservoirs, and a duration-gated charge cap. Each of the last five fixed
one row and broke burst-rate or a neighbour.

**Do not fit another correction term on top of the existing model.** A real fix
needs structure the model lacks. The candidates that have not been tested:

- two detector paths combining **nonlinearly** rather than by max or sum,
- charge saturation on very short events,
- a rectifier law that is not plain magnitude.

State the hypothesis and the experiment that would falsify it **before** editing
anything structural. One knob per iteration, fresh measurement between. If a
change improves its own gate and worsens another, it is a symptom fix — revert
it and say so.

**A negative result with a root cause is a deliverable.** "Mechanism X is
excluded, here is the per-point evidence" is a better outcome than a forced
marginal win, and it belongs in the campaign log where the next session looks.

## Hard scope walls

- **Opto mode only.** Bugs found in the other seven modes get GitHub issues, not
  fixes. Known and deferred: #220, plus the same `external ? sidechain : input`
  dead-link bug that likely exists in VCA (~line 854) and Bus (~line 960) of
  `MultiCompModes.hpp` — that needs its own issue, not a patch in this work.
- **No UI work.** The LA-2A panel restyle (#212) and the geometry contract
  (#213) are a separate task, after the emulation is finished.
- **Idle-state harmonics are out of scope.** Worst idle error is 1.86 dB on H5
  at roughly -110 dBc. Inaudible. Leave it.
- **Do not touch** `.github/workflows/daf-build.yml` — it carries the owner's own
  uncommitted edit.
- **DAF and DAF-Widgets are HARD FORKS. `DISTRHO/DPF` and
  `DISTRHO/DPF-Widgets` are not references — never read, diff, cherry-pick,
  merge from, or cite upstream, and never point any build or CI at it.** Ours
  are `dusk-audio/DAF` and `dusk-audio/DAF-Widgets`; CI pins them at
  `eb54f2f4` and `668de17f`. There is no "upstream fixed it" path.
- **Do not modify the fork checkout at `~/projects/DAF`** either. The build
  consumes it via `-DDAF_PATH=$HOME/projects/DAF`; a local edit makes your build
  disagree with CI, which clones the pinned ref — you would be measuring a
  framework nobody else has. If a build breaks, fix the plugin code. Note the
  checkout is on `main` at `74451d58`. The AU render-guard commit is merged to
  fork `main` (as `49aa4d41`, PR #14), so `HANDOFF-MC2.md` §2 is correct on that
  point. CI still pins the older `eb54f2f4`, which predates it — bumping that pin
  is a deliberate framework advance, not part of this task.
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
