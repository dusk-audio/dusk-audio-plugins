# MC-2 Opto E2E: learned end-to-end capture of the UADx LA-2A — plan (2026-10-01)

On approval this file is copied verbatim to
`plugins/multi-comp/docs/opto-e2e-learned-plan-2026-10-01.md`. Plan mode only allows this
path. Nothing is built before approval.

## Context

The R11a physical base, the default build, is honest but weak through the real AUs:

| Check | R11a physical base |
|---|---|
| Settled | 120/122 |
| Knee | 9/9 |
| Recovery | 20/20 |
| Charge | fails |
| Dense PR 40 | fails |
| Harmonic cells | ~30% pass; 1 kHz witness 10–36 dB low |
| Two-tone | 88/280 |
| Pink, full band / octave | 4/26 and 28/96 |
| Link | C2 188/437; C3 and C4 fail |
| Music | 0.825 / 1.185 against a 0.5 bar |

Every earlier learned model (tools repo, G0 → GME, R7–R23) learned **only the gain computer
at 200 Hz** and kept the physical audio path. They improved music: GME scored 0.510/0.716 and
GME+P′ 0.466/0.607. But they broke the link (C2 15/437), the harmonics, two-tone and settled.
An end-to-end audio→audio capture has never been tried.

**Goal:** one causal learned model, audio in → audio out, matching the UAD on any signal.
- Conditioned on PR, Compress/Limit and Gain.
- Linked stereo with shared weights, so L=R is bit-identical to mono.
- Runs through RTNeural inside MC-2 at ≤ 5% of one core per stereo instance.
- Time box: one week. The physical base stays the default and the fallback.

**Owner decisions (2026-10-01):**
- the structured end-to-end network (below);
- R10 `m` (MySong / whiskey) excluded;
- the learned engine runs only at a 48 kHz host, other rates route to the physical engine;
- the pilot spans D1–D2 and ends with a stop for the owner's go.

**Owner additions at approval (2026-10-01, second message):**
1. **Even harmonics.** The 1 kHz witness's largest deficits (H2 about 10 dB low, H4 about
   36 dB low) come from the per-channel input element: a one-sided, input-level-dependent
   nonlinearity ahead of the cell, flat across frequency. The audio path must be able to
   produce them, not just modulate gain. See "Element" below.
2. **The pilot pass bar** is the owner's, fixed before the pilot starts (see "C1 pilot
   criteria"). Miss any item → report and stop; no scaling.
3. **Sample rates.** 48 kHz only is fine for the pilot and this week. **Release blocker:**
   before shipping, the learned engine must work at 44.1, 88.2 and 96 kHz, for example by
   resampling internally to 48 kHz.
4. **RTNeural** is at `external/RTNeural`, committed in `94d9cacb`. The owner's note says
   "pinned to [TAG]", but RTNeural has no tag at this commit. The pin of record is commit
   `95c3c0f987a6` (xsimd submodule 14.3.0, `e88a7283`). Use exactly that.

## Facts verified during planning

**Disk and machine.**
- 293 GiB free, so nothing needs archiving.
- Each MC-2 evaluation tag measures about 24 GB: scorecard 11, fit grid 6.5, interim 3.1,
  music 1.1, the rest smaller.
- `opto-production-harmonics-20260928` is 250 GB. It is an archive candidate only if ever
  needed.
- M1 Pro, 10 cores, 16 GB RAM; `/usr/bin/python3` 3.9.6 with torch 2.8.0. Training runs on
  the CPU.

**Capture host and UAD.**
- The pinned capture host `programme-native-20260911` is **gone**, and every tools-repo
  capture script points at it.
- The replacement is `build-multi-comp-1176/opto-host-20260930/duskverb_render` (SHA
  `f9a0ec1f…`). It was verified 88/88 UAD byte-identical in R10.
- The UAD AU (v1.0.8 b787) is unchanged (`8c97e490…`). It has latency 87, renders
  deterministically, and its idle output is −242 dBFS.
- Throughput is about 0.7 s per 11.5 s stem, roughly 5,000 stems/h, rendered in float32.

**RTNeural** exists only at `/private/tmp/opto-rtneural-20260928` (commit `95c3c0f`). That is
a temp dir. It is not in the plugins repo.

**MC-2 code.**
- MC-2 is a DAF plugin.
- `processRange` runs the generic path channel by channel (`MultiCompDSP.cpp` ~1335). The
  linked Bus and FET paths process both channels per sample (~1253).
- The dry delay adds `kOptoStageLatencyHost` (~598).

**Training data.**
- Every existing whitelisted capture is at **Gain 0.25** (`t4_p2_capture.GAIN`), so only fresh
  renders teach Gain.
- The R9 generator puts tones inside fixture and held-out bands: `f2 = 713 + 263.17·i` gives
  713 Hz and 976 Hz, and the bass roots include 82.4 Hz.

**Reuse (tools repo, `plugins/MultiComp/tests/programme_parity/opto/`).**

| Script | What it provides |
|---|---|
| `t4_p2_capture.render_one` | readback check, rejects, render lock, reverse-order repeat |
| `mus_kernel.py` | `causal_hp20_v1` metric, `guard_clip` |
| `dc_score.guard`, `known_hashes()` | fixture and sealed guards |
| `graybox_bootstrap.py` | paired bootstrap: 100k reps, clip resampling, floors 0.089 / 0.132 |
| `graybox_gme_rtneural.py` + `.cpp` | export and C++ parity |
| `graybox_cpu.cpp`, `graybox_g0_runtime.py` | CPU timing |
| `graybox_corpus_r9.py`, `graybox_corpus_r14.py` | generator patterns |

**AU tools (plugins repo):** `plugins/multi-comp/tests/opto_au_scorecard.py` and
`opto_au_harmonic_map.py`, which covers the fit grid, interim, IMD, sweep, witness and the
music capture, score and bootstrap.

## Design

### Architecture (item 3, and the no-correction rule)

This is one network, trained jointly, with blocks in the measured LA-2A order. Every block is
learned and acts identically on every signal. There is no classifier, no lookup table and no
hand-set constant. The fixed glue is plain signal-independent arithmetic: squaring, log, a
fixed anti-alias decimator, pooling, interpolation and multiplication.

```text
host 48k ─► outer 2x OS (existing, 67 smp; learned mode pins 2x, like Opto ignores Global Lookahead)
  per channel, shared weights (mode rate 96k):
    LinFIR   learned causal 32-tap FIR, init = pass-through (UAD frequency response / fractional delay)
    FrontEnd learned Conv1D bank (8 × 32 taps) → x² → fixed half-band decimator cascade ÷32 → log
    FastAudio a few learned one-pole smoothers on x² (audio-rate, so ripple at 4f/6f stays reachable)
  link: symmetric pool across channels [mean ‖ max]; whole block processed as a paired path
        (front ends for both channels, then control, then per-channel audio) → L=R ≡ mono bit-exact
  linked control (one instance):
    SlowGRU h32 @ 187.5 Hz ── seconds of release memory
    FastGRU h16 @ 3 kHz ── attack, charge, ripple; inputs: pooled feats, PR/mode, slow state, own last gain
    Head → log g (cell gain) + c (8-dim conditioning) + FastAudio contribution to log g at audio rate
  per channel, shared weights:
    Element  learned audio-rate NONLINEARITY [x, own-level(x), PR, c] → y, f(0,·)=0, ahead of the cell
             (per-channel input element; own-level = learned smoothers on this channel's own x²)
    × g                                                       (cell)
    Makeup   learned scalar from the Gain knob (tiny MLP, once per block; Gain enters only after the cell)
    Stage    learned map [y, gainEmbed] → out, constrained f(0)=0, inside the existing
             MultiCompOptoStageOversampler (local 2x, so total latency stays 73: no harness change, no alias regression)
    learned output HP pair (~1 Hz)
  ─► outer OS down ─► host
```

**Element can produce the input element's even harmonics (owner addition 1).** Element is a
per-sample map of the waveform x itself, not a gain on x. Through its biases (tanh MLP) or
even-order terms (polynomial) it represents one-sided curves, so a sine through it gains
H2/H4/H6. It sits ahead of the cell and is conditioned on the channel's **own** input level
(learned smoothers on that channel's own x², not the linked pool, because the physical
element is per-channel) and on PR, plus the linked vector c. It has shared weights and
acts identically on any signal, which keeps L=R ≡ mono intact. The pilot checks this
directly: Element's own H2/H4 contribution on a DEV tone at the witness level
(element-only probe, in torch), plus the witness through the AU.

**Map form, decided in the pilot on DEV only:** a 16-unit tanh MLP, or a degree-7 polynomial
whose coefficients come from the head. The polynomial is cheaper, gives f(0)=0 for free, and
controls the harmonics directly. The f(0)=0 constraint is structural (DC and idle-silence
safety), not a correction.

**Structural checklist** (learned-law lesson 1):

| Measured element | Block that carries it |
|---|---|
| Per-channel instantaneous input element (about −26.5 dBFS; H2/H4/H6; scales with PR) | Element, conditioned on `c` |
| Feedback cell; release at 64 ms, 185 ms and 1.17 s; charge suppression of 0.75 dB per dB | SlowGRU and FastGRU, with own-gain feedback |
| Carrier-dependent attack (~f^0.5, 2.1 kHz corner; pink behaves like LF) | FrontEnd bank up to Nyquist |
| LF ripple (4.19 dB p-p at 50 Hz, PR 1) | FastAudio path plus FastGRU |
| Limit mix | Mode input |
| Make-up outside the detector | Gain only after the cell |
| Output soft ceiling (+6.34 dBFS, 2.44 dB knee, asymmetric) and the ~1 Hz HP pair | Stage and learned HP |
| Link sums lights on music and acts as max on tones | Learned mean/max mix |
| UAD frequency response and fractional delay | LinFIR |

**Memory and its CPU cost.**
- The seconds-long release is held in SlowGRU state. A 1.2 s tau is about 225 steps at
  187.5 Hz, which is trainable on whole clips. The earlier GRU gain computers already learned
  release at 200 Hz.
- An audio-rate GRU or TCN would need about 57k samples of receptive field, which does not fit
  the budget. That is the reason for the hybrid.
- The arithmetic count is about 120 M MAC/s, but the real cost is per-call overhead plus
  tanh: about 6 M tanh/s, which at `std::tanh` speed would be 9–15% of a core.

**CPU plan:**
1. Measure a stubbed learned branch first, to get the processRange floor.
2. Use the RTNeural xsimd backend with accurate (not "fast") tanh.
3. Measure the full model.
4. If it is over 5%, apply this fallback order and report each step: polynomial maps → FIR
   32→16 taps → FastGRU at 1.5 kHz.

### Data (item 1)

**Whitelist** (the R10 `m` family is excluded):

| Set | Contents |
|---|---|
| Music FIT `f_` | 17 Fytakyte clips × 17 settings |
| R8 `r` | 1,224 stems |
| R9 `d` | 80 synthetic clips |
| R14 `y` | 64 synthetic clips |
| R22 `u` | 29 FIT clips; its selection clips go to DEV |

All of these are Gain 0.25 only.

**Exclusion bands** are applied to every training segment, old and new:

| Item | Values | Band |
|---|---|---|
| Held-out frequencies | 150, 300, 700, 1400, 3500, 12000 Hz | ±3% |
| Interim frequencies | 70, 400, 3000 Hz | ±3% |
| 1 kHz witness / fixture carrier | 1 kHz | 970–1030 Hz |
| Settled carrier | 82.41 Hz | ±3% |
| Gain | 0.40, 0.45 | ±0.02 |
| PR (sealed holdout) | 0.75, 0.59375 | ±0.01 |

- Old families are audited from their generator metadata. Offending segments (R9 `broadband`
  i ∈ {0, 1, 4, 9}, `bass` root 82.4) are masked out of the loss, and C2 logs it.
- These are guards, not proof that a point is unseen.

**Guards.**
- `known_hashes()` and `dc_score.guard` stay in place.
- Refused names and directories:
  - `z_`, Untitled, `opto-holdout-20260926`, MySong, whiskey spin;
  - `opto-t4-p2-sysid`, `opto-combo`, `opto-broadband`, `opto-checka`, `opto-element`;
  - the charge, dense, crest and recovery stimuli;
  - every r9/r10/r11 grid capture and every scorecard render.
- The guard test is shown failing on an injected fixture.

**Fresh renders** come from `e2e_corpus.py`, seeded, with every parameter drawn from a
continuous distribution:

| Family | Parameters |
|---|---|
| Tones | log-uniform 10 Hz–20 kHz, −60..0 dBFS |
| Sweeps | log, slow and fast, up and down |
| Bursts from silence and from lit states | length log-uniform 0.2 ms–2 s (covers the 17–144-sample charge range), gaps 1 ms–5 s |
| AM tones | 0.5–300 Hz rate, depth 0–1 |
| Two-tones | f-ratio and level-ratio |
| Noise bands | white, pink, band-pass |
| Dense programme | — |
| Decorrelated stereo | ≥ 15% of the corpus: L/R offset 0–20 dB, partial correlation, one side only |
| Sub-20 Hz and DC steps | for the HP pair |
| Level-step envelopes | — |
| Knob automation | `--nparam-event` |
| Idle silence | — |

**Settings.**
- One random tuple per host pass, 10 clips per pass.
- PR ~ U(0, 1), snapped to 1/32768. Mode ~ Bernoulli(0.5).
- Gain ~ U over the full knob range, with the top oversampled for saturation.
- Because the old data is Gain 0.25 only, fresh renders carry about 60% of the training
  weight.

**Excitation check before training** (learned-law lesson 2). Histograms per checklist row:
- deep and shallow GR;
- gaps after lit states up to 5 s;
- 20–100 Hz ripple;
- 6–20 kHz;
- stereo asymmetry;
- Gain up to maximum;
- two-tone ratios.

**DEV split:** by clip and seed, 15% of fresh clips plus the R22 selection clips. **All model
selection uses DEV only.**

**Volume.**

| Item | Amount |
|---|---|
| Whitelisted audio | about 10.5 h |
| Fresh audio | about 7,200 × 10 s ≈ 20 h |
| Capture time | ≈ 1.5 h plus a 10% reverse-order repeat |

- Data is read through `np.memmap` from the WAVs, with no copies, in float32 throughout.
- **Alignment:** the UAD output is trimmed by 87 and the target is shifted to the model's
  latency of 73. The torch model includes the exact outer-OS and stage-OS FIRs.

### Loss (item 2)

`L = λ_env·L_env + λ_spec·L_spec + λ_harm·L_harm + λ_wave·L_wave`

| Term | Definition | What it sees |
|---|---|---|
| `L_env` | Causal HP20 on both sides (as in `causal_hp20_v1`); frame-energy dB L1 at 1 ms and 5 ms hops over frames within 60 dB of the clip maximum, release tails included | GR trajectory and attack |
| `L_spec` | Multi-resolution log-magnitude STFT (N = 1024 / 4096 / 16384), Kaiser window with sidelobes ≤ −140 dB, L1; floor **relative to each frame**, −120 dB below its peak bin, above float32 FFT rounding | bins 60–90 dB below the fundamental weighted like the fundamental |
| `L_harm` | Training side only: generator frequency metadata drives a float64 sliding lock-in at k·f (k = 1..7) and the IMD products; dB error with a −120 dBFS floor, plus phase weighted by level | exactly what the scorecard measures, including the sign of H2 at 180° |
| `L_wave` | Small ESR on the HP'd waveform | anchors phase and fundamental |

The terms are normalised to about 1 at initialisation. The λ ratio is picked on DEV from at
most three settings.

**Training runs in two phases, on CPU** (the MPS kernel-per-op cost makes the
feedback GRU loop slow):
1. **Control pre-train** at the control rate on whole clips. The target is a GR envelope
   derived from the output/input envelopes, on the training side only. The head reads the slow
   state directly.
2. **End-to-end fine-tune** on 0.5–2 s windows. The state is carried in from a no-grad run of
   the clip prefix and then detached. Batch sizes are set so peak RSS stays ≤ 6 GB per run,
   and two runs go in parallel.

**Loss proof (pilot).**
- **Hypothesis:** a waveform-only loss (A: ESR plus linear MR-STFT) leaves the witness H4..H7
  more than 6 dB off and fails DEV tonal harmonics; the proposed loss (B) brings H2..H7 within
  2 dB.
- **Evidence:** A vs B on DEV tonal harmonics (many tones, in torch), plus B on the witness
  through the AU. The witness band is excluded from training.
- **Falsifier:** B also misses. In that case I report the root cause (capacity, aliasing,
  precision or control aliasing) before any scaling.

### Integration in MC-2 (plugins repo)

**Build.**
- RTNeural is the git submodule at `external/RTNeural`, pinned to commit `95c3c0f`
  (owner commit `94d9cacb`; no tag exists at this commit).
- A CMake option, **`MC2_OPTO_LEARNED`, is OFF by default**, so CI and release builds are
  unchanged.

**New `plugins/multi-comp/core/MultiCompOptoLearned.hpp`.**
- Contents: the RTNeural `ModelT` types, the paired linked processing, and the glue.
- Weights live in the generated `MultiCompOptoLearnedWeights.hpp` and are parsed in the
  constructor. There is no audio-thread I/O or allocation.
- `reset()` clears every state, and `prepare()` is idempotent.

**`MultiCompDSP.cpp`.**
- Add a learned linked branch modelled on the Bus/FET paired path (~1253).
- Latency stays 73, because the stage oversampler is kept. The dry delay and
  `latencySamplesForMode` are unchanged, and each is checked.

**Routing.** This is configuration, not classification, and is documented.
- External side-chain, host rates other than 48 kHz, and link amounts below 100% route to the
  physical engine. They are unsupported in learned mode and excluded from the verdict.
- Dual Mono runs the linked model per channel; that is valid because mono ≡ L=R.
- The GR meter reads −20·log10 g.

**Bit-null check:** with the flag OFF, byte-compare renders against the r11a-phys AU.

**Tools-repo code** goes in `plugins/MultiComp/tests/programme_parity/opto/e2e/`:
- `e2e_corpus.py`, `e2e_capture.py`, `e2e_dataset.py`, `e2e_model.py`, `e2e_loss.py`,
  `e2e_train.py`, `e2e_export_rtneural.py`, `e2e_parity.cpp` and `e2e_cpu.cpp`.
- These wrap the reuse tools above. `evidence.py` is repointed to the new host SHA.

**Evidence** goes to `build-multi-comp-1176/opto-e2e-20261001/` and stays local.

### Evaluation (item 4): through the real AU only

The settings are the standard ones: 48k, 512-sample blocks, 2 s pre-roll, linked, latency 73
(measured by impulse and xcorr against what is reported), MC-2 neutral.

**Frozen-model rule.** Gates are viewed **once, on the frozen final model**, and the model
viewed is the model reported. Nothing is iterated after a gate look. The only earlier looks are
the pre-registered pilot subset in C1, and the pilot model is discarded.

**Order of the verdict report:**
1. **Two-tone (280 combinations, 140 single components) and stereo link C1–C4: first and
   prominent.**
2. The rest of the scorecard: settled 122, knee 9, recovery 20, charge 16, dense 4, pink
   26/96.
3. Fitted harmonic grid. It is a pure test, since nothing is fitted to it.
4. Interim validation set: reported, never used for selection.
5. IMD and the slow sweep, at 2 dB.
6. 1 kHz witness.
7. Music VAL, overall and high PR, with the paired bootstrap against the existing r11a-phys
   renders.
8. CPU: `e2e_cpu.cpp`, the full MC-2 core in learned mode, 60 s of stereo music, 48k,
   512-sample blocks, single thread, median of 5. Bar: 5% of one core.
9. auval and CTest. CTest stays 19/20 with the flag OFF.

**New learned-mode tests**, each shown failing once by breaking what it guards:

| Test | Bar |
|---|---|
| Mono vs L=R | bit-exact, in the core and through the AU (`--channels 1` vs 2) |
| Idle silence | output ≤ −150 dBFS |
| Reset; prepare twice; zero-sample block | — |
| RTNeural vs torch on exported vectors | ≤ 1e-6 FS |
| Latency, measured vs reported | match |

**Untouched:** the harmonic held-out grid, the sealed holdout, Untitled and `z_` are never
rendered or opened. All bars are unchanged.

## Schedule (item 5); D1 is the first day after approval

| Day | Stage | Machine time | Checkpoint (`plugins/multi-comp/docs/opto-e2e-cN-*.md`) |
|---|---|---|---|
| D1–D2 | **Pilot.** Pin host and AU, vendor RTNeural, write the generator. Pilot corpus (~600 renders, ~10 min): tones, bursts, two-tones, LF tones at 20–100 Hz, a long-gap release family, PR 0–0.8, both modes, Gain 0.1–0.6. Loss A vs B; pilot training (~3 h, two parallel runs). Export + C++ parity, CPU micro-benchmarks, stub floor. Paired MC-2 branch, learned AU build | ~5 h | **C1 pilot**, then **STOP for the owner's go** (criteria below) |
| D3 | Full corpus capture, excitation check, band audit of old families, guard fail/pass | ~2 h capture | **C2 data** |
| D3–D5 | Scaled training: two candidates in parallel; DEV-only selection; at most one coverage iteration, driven by DEV error, before freezing | ~30–40 h, background | **C3 training**: DEV metrics, the frozen model and why it was chosen |
| D6 | Export, AU build, single frozen evaluation | ~3–4 h | **C4 verdict** |
| D7 | Final report: recommendation (the owner decides), negative results with root causes, memory update | — | **Final** |

**C1 pilot pass bar** (owner's, fixed 2026-10-01 before the pilot starts). All must hold:

| # | Criterion |
|---|---|
| B1 | **The pipeline works end to end through the AU:** generator → UAD capture → train → export → MC-2 learned AU → render → score. This includes capture integrity (repeats sample-exact; old- vs new-host byte compare on 20 stems), export parity (RTNeural vs torch ≤ 1e-6 FS), and latency measured = reported = 73 |
| B2 | **CPU ≤ 5% of one core per stereo instance**, measured on the pilot model in the full MC-2 core (60 s stereo, 48 kHz, 512-sample blocks, single thread, median of 5). The full-size projection is also reported |
| B3 | **L = R matches mono exactly** (bit-exact, in the core and through the AU) |
| B4 | **On the held-back training slice (DEV), the GR error is no worse than the physical base**, both rendered through the AU and scored with the `causal_hp20_v1` frame metric |
| B5 | **At the 1 kHz witness (kept out of training), H2–H5 are within 6 dB of the UAD with the composite loss, and closer than with the waveform-only loss** (each harmonic, both through the AU) |

Reported but not part of the bar: H6/H7 and the 2 dB map bar at the witness; idle
≤ −150 dBFS; auval; the AU subset (settled, recovery, two-tone subset, link C1/C2); DEV
release (gaps up to 5 s) and LF-ripple error.

To make B5's A-vs-B comparison count, both pilot models are exported and rendered through
the AU at the witness. That means two learned AU builds, installed one after the other.

**Stop conditions:**
- any of B1–B5 fails → report and stop, no scaling;
- any capture repeat is not sample-exact;
- any guard detects contamination;
- peak RAM threatens the 16 GB machine.

**Disk** (measured per tag): fresh captures ~50 GB + inputs ~5 GB + pilot ~6 GB + final
evaluation ~24 GB ≈ **85 GB of 293 GiB free**. Nothing is archived or deleted.

**First step after approval:**
1. Copy this plan to `plugins/multi-comp/docs/opto-e2e-learned-plan-2026-10-01.md`.
2. Update the auto memory: the new direction, a one-week box from approval, the physical
   default, the pinned host gone, `z_` contaminated, existing captures Gain 0.25 only.

## Verification (end-to-end)

The flow: generator → capture (integrity checks) → train (DEV) → export → C++ parity → AU
build and install (SHA-pinned per tag, one render lock) → measure latency → scorecard plus
harmonic map, interim, IMD, sweep, witness, music and bootstrap → CPU → CTest and auval.

Reported numbers come only from AU renders. I make no commits, branches or pushes.
