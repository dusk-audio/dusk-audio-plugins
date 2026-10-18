# Opto E2E learned capture: C2 pilot report (2026-10-02)

Brief: the owner's C2 go of 2026-10-01, with rulings. Plan: `opto-e2e-learned-plan-2026-10-01.md`.
C1: `opto-e2e-c1-pilot-2026-10-01.md`.

Measurement boundary, as before: the real AUs (UADx LA-2A v1.0.8 b787 and MC-2) through the pinned
host `opto-host-20260930`, at 48 kHz, 512-sample blocks, 2 s pre-roll, linked stereo, latency
compensated (UAD 87, MC-2 73), MC-2 neutral.

## Verdict: C2 FAILS B4 and B5, and the kill criterion applies

| # | Bar (unchanged) | C2 result | Verdict |
|---|---|---|---|
| B1 | Pipeline works end to end through the AU | All stages ran through the AU; auval passes; latency 73 measured = reported. **Export parity vs the exact model (torch float64): A 1.38e-6, B 1.47e-6, over the 1e-6 bar** | **FAIL** (parity sub-check) |
| B2 | CPU ≤ 5% of one core per stereo instance | Full MC-2 core in learned mode: **4.31%** (same 60 s stereo harness, 48 kHz, 512-sample blocks, one thread, median of 5, double control path) | PASS |
| B3 | L = R matches mono exactly | Bit-exact in C++ and through the AU (1-channel vs 2-channel instance, both channels), A and B | PASS |
| B4 | DEV GR error no worse than the physical base | Same 90 DEV items. Learned (B) **1.035 dB**, physical 0.398 dB. Paired bootstrap, learned − physical: +0.637 dB, 95% CI [+0.422, +0.864] | **FAIL** |
| B5 | Witness H2–H5 within 6 dB with the composite loss, and closer than waveform-only | Composite: H2 −6.7, H3 −14.7, H4 −6.6, H5 −10.3 dB. Waveform-only: H2 +2.2, H3 +10.9, H4 −11.1, H5 +6.2 dB | **FAIL** (both conditions) |

The pre-registered kill criterion is "C2 misses B4 or B5 → the E2E direction closes". C2 misses
both, so **the E2E direction closes here.** The physical R11a stays the default; it was never
changed. The learned code stays behind `MC2_OPTO_LEARNED=OFF`, and the items you may want removed
are listed at the end. No C3 without new mechanism evidence.

## B5: the witness, and the root cause

These are actual AU renders. The witness band (970–1030 Hz) was excluded from training.

| Harmonic | UAD dBFS | Composite (c2B) − UAD | Waveform (c2A) − UAD | C1 composite − UAD |
|---|---:|---:|---:|---:|
| H2 | −67.24 | −6.71 | +2.20 | −4.45 |
| H3 | −71.69 | −14.68 | +10.92 | −11.08 |
| H4 | −91.62 | −6.62 | −11.06 | −21.97 |
| H5 | −87.53 | −10.33 | +6.20 | −51.16 |
| H6 | −101.86 | −6.75 | +3.83 | −53.33 |
| H7 | −95.98 | −13.83 | −6.40 | −64.75 |

The element can now reach the targets: reachability passes with 21 dB or more of headroom (section
3). H4–H7 moved 15–51 dB closer than in C1. The parametrisation fix worked; **what failed is
learning the gate.**

**Root cause: the trained element has no switch-on.** Measured on a DEV-band 1.2 kHz tone (not the
witness) at PR 0.5, Compress, Gain 0.25, the learned coefficient ratio `a_k` (dBc) against the
UAD's measured ratio:

| Input dBFS | c2B H2 / H3 / H4 / H5 | UAD H2 / H3 / H4 / H5 |
|---:|---|---|
| −40 | −83.0 / −84.9 / −116.1 / −99.6 | −84.9 / −108.8 / −99.8 / −104.3 |
| −30 | −79.0 / −76.5 / −89.1 / −88.3 | −75.5 / −103.2 / −131.1 / −121.1 |
| −26 | −74.7 / −74.6 / −86.3 / −86.0 | −71.4 / −94.6 / −110.2 / −108.8 |
| −24 | −73.1 / −73.7 / −85.2 / −85.1 | −58.2 / −61.8 / −66.1 / −71.0 |
| −16 | −68.7 / −71.1 / −81.9 / −82.2 | −51.8 / −58.2 / −75.0 / −68.7 |
| −10 | −66.5 / −69.6 / −80.1 / −80.6 | −54.2 / −58.2 / −72.2 / −69.4 |

- The UAD steps up by 13–45 dB between −26 and −24 dBFS and is flat above.
- The model rises smoothly at about 0.55 dB/dB over the whole range. That is a compromise: too high
  below the threshold, 12–17 dB too low above it. c2A shows the same smooth shape, only higher,
  which is why it overshoots H3/H5 and passes H2.

Why the gate was not learned is a hypothesis, not yet measured:
- The gate is a step of about 2.6 dB in input level.
- The coefficient head is a 16-unit tanh MLP on log-level features. A step that sharp needs large
  input weights, which gradient descent reaches slowly.
- The element is trained only in phase 2: 3 epochs × 1,000 items, about 3,000 item-updates. Phase
  1 does not touch it.
- Outside the narrow transition the gradient towards the step is weak, and L1-in-dB favours the
  median compromise that the table shows.

**The DEV harmonic term barely moved:** C1 5.32 → C2 5.24 dB.

## B4: DEV gain reduction

| DEV family | n | C2 (c2B) | C1 (pilotB) | Physical |
|---|---:|---:|---:|---:|
| stereo | 14 | 1.37 | 1.37 | 0.30 |
| release | 12 | 1.18 | 1.62 | 0.24 |
| tone | 16 | 0.82 | 0.79 | 0.33 |
| two-tone | 16 | 1.07 | 1.16 | 0.39 |
| noise | 8 | 0.85 | 0.97 | 0.74 |
| burst | 16 | 1.02 | 1.05 | 0.44 |
| lf | 8 | 0.45 | 0.56 | 0.27 |

**Dense settings halved phase 1's DEV frame error (C1 1.76 → C2 0.884 dB) but barely moved the
GR metric (1.139 → 1.035 dB).**
- The same items fail as in C1:
  - `stereo_001`, PR .61, Gain .53, UAD GR 12.0 dB: mean +3.70;
  - `twotone_046`: +2.49;
  - `release_002`: −2.80.
- Static (mean) offsets are 58% of the MSE.

**Mixed evidence on mechanism.**
- By construction, GR error = PR 0 output-level error − compressed output-level error.
- Over the 90 items:
  - PR 0 (uncompressed) level error has RMS 0.90 dB, correlation 0.23 with the GR error.
  - Compressed level error has RMS 1.00 dB, correlation 0.55, inverted.
- Both the make-up and output-stage law at PR 0, and the compression law, are about 1 dB off. They
  partly cancel, and neither alone explains B4.
- The frame-envelope training target scores absolute output level. It does not isolate the
  PR → GR law the way the B4 metric does.

## B1: export parity (ruling: reference = exact model, torch float64)

| Comparison, stereo test signal | c2A max / RMS | c2B max / RMS |
|---|---|---|
| **C++ (double control path) vs exact: the bar** | **1.38e-6** / 3.3e-7 | **1.47e-6** / 3.1e-7 |
| Sanity: C++ float32 control vs exact | 1.95e-6 / 3.8e-7 | 3.94e-6 / 2.9e-7 |
| Sanity: torch float32 vs exact | 5.07e-6 / 1.15e-6 | 4.73e-6 / 9.6e-7 |

- **Fails by about 1.4×.**
- The C1 element passed this exact check at 8.2e-7. The change since then is the C2 element, which
  adds a per-sample float32 envelope sqrt, a divide and a Chebyshev recurrence to the audio path.
- **Hypothesis, not tested:** float32 rounding in that new audio-rate element. The falsifier is
  running the element arithmetic in double in the C++ (the control path already is) and
  re-checking.
- The sanity lines are unequal: C++ float32 sits 2–4e-6 from exact, torch float32 5e-6. Both are
  float32-rounding sized, and C++ is closer to exact than torch is.
- Not run, because the kill criterion closes the direction first.

## Section 2: element placement, falsified before any edit

**C1-style probe:** 1.2 kHz, 20 levels from −50 to 0 dBFS, UAD at PR 0.5 Compress Gain 0.25, with
a PR 0 control (`c2/element-probe`).

| Region (input) | Observation | H-a (flat everywhere) | H-b (post-cell) |
|---|---|---|---|
| < −26.3 dBFS | Equal to the PR 0 control (H2 within 0.01 dB); the element is off | ✗ | ✗ |
| −26.3 → −23.7 dBFS (GR 0.0 → 0.2 dB) | Ratios jump H2 +14.7, H3 +33.1, H4 +77.3, H5 +58.1, H6 +49.0, H7 +45.3 dB | ✗ | ✗ (a post-cell polynomial allows +2.6 to +15.6 dB) |
| Above, up to 0 dBFS | Ratio slopes H2–H7: −0.17 / +0.07 / −0.15 / +0.19 / −0.32 / −0.39 dB/dB. H-b predicts +0.26 … +1.55, since post-cell level rises 0.26 dB/dB | ✓ | ✗ |

**E1 (owner follow-up)** compares where the element switches on with where the cell engages. PR
0.75 was replaced by 0.80, because 0.75 is the sealed holdout's reserved PR (flagged).

| PR | Switch-on (H2 > PR 0 + 3 dB) | GR onset (≥ 0.2 dB) |
|---|---|---|
| 0.25 | −24 dBFS | −10 dBFS |
| 0.50 | −25 dBFS | −23 dBFS |
| 0.80 | −25 dBFS | ≤ −40 dBFS |

**E2:** L at −35 dBFS with R at −10 dBFS, linked.
- L's H1 falls 9 dB through the link, but its H2 ratio stays at −82.8 dBc (mono instance −80.8). An
  active element would put it near −57.
- L alone in a stereo instance equals the mono instance exactly.
- With R at 1.2 kHz, L's odd harmonics rise (H3 +48, H5 +33 dB). The R-at-1.7 kHz control shows
  these are R-driven gain-ripple sidebands, not the element.

**Conclusion:** the element is gated by the channel's **own input level** and is not gated by cell
engagement. Per your ruling, the element was built ahead of the cell and conditioned on own
envelope, PR and the Compress/Limit knob, with no linked vector.

## Section 3: reachability, pre-registered

**Parametrisation v1, the power basis** `x + e·Σa_k (x/e)^k`, bound 0.1.
- It passed reachability with 28.8 dB or more of headroom.
- It **failed the overfit test**: envelope error 6.8 → 9.7 dB.
- Odd powers leak into H1 (`u⁷` at a steady √2 carries most of its energy in the fundamental), so
  the harmonic and level objectives fought.

**Final: the Chebyshev basis** `x + √2·e·Σ a_k (T_k(v) − T_k(0))`, `v = x/(√2·e)`, bound 0.03.
- On a steady sine, each `a_k` adds a pure k-th harmonic.
- Verified numerically: H_k at −30.47 … −30.57 dBc against a target of −30.46. H1 moves by at most
  0.005 dB. The largest other harmonic is at or below −52.5 dBc.

| Level | H2 UAD / headroom | H3 | H4 | H5 | H6 | H7 | B5 (H2–H5 ≥ 6 dB) |
|---|---|---|---|---|---|---|---|
| −40 dBFS | −84.9 / +54.5 | −108.8 / +78.4 | −99.8 / +69.4 | −104.3 / +73.8 | −104.8 / +74.4 | −116.2 / +85.8 | OK |
| −24 dBFS (just above switch-on) | −58.2 / +27.8 | −61.8 / +31.4 | −66.1 / +35.7 | −71.0 / +40.5 | −79.2 / +48.7 | −95.9 / +65.4 | OK |
| −16 dBFS | −51.8 / +21.3 | −58.2 / +27.7 | −75.0 / +44.5 | −68.7 / +38.2 | −84.8 / +54.3 | −79.4 / +48.9 | OK |
| 0 dBFS | −57.9 / +27.5 | −57.3 / +26.9 | −78.6 / +48.1 | −69.0 / +38.5 | −86.6 / +56.1 | −91.6 / +61.1 | OK |
| 1 kHz witness | −51.9 / +21.5 | −56.4 / +25.9 | −76.3 / +45.8 | −72.2 / +41.7 | −86.5 / +56.1 | −80.7 / +50.2 | OK |

The ceiling is −30.46 dBc for every order, including the odd orders. Overfit test (Chebyshev):
- envelope error 6.8 → 3.8 dB and 5.8 → 3.3 dB;
- harmonic error 15.0 → 4.4 dB.

## Reported only: the c2B AU subset

| Check | c2B | C1 pilotB | Physical (R11a) |
|---|---|---|---|
| **Two-tone combinations (280)** | **58 (20.7%)**, RMS 1.18 dB | 38 (13.6%) | 88 (31%) |
| Two-tone single components (140) | 23 | 32 | 133 |
| **Link C1** | pass | pass | pass |
| **Link C2 (437)** | **0**, worst 3.40 dB | 1 | 188 |
| **Link C3 / C4** | fail / fail (C3 rel 3.9–10.5; C4 rel 0.16–4.77) | fail / fail | fail / fail |
| Settled (122) | 35, worst 8.61, RMS 4.04 dB | 23 | 120 |
| Recovery (20) | 4 | 4 | 20 |

## Data and integrity (section 4)

**C2 corpus:**
- 3,000 clips at 6 s, 5,000 distinct (PR, mode, Gain) settings, 7,506 UAD renders (32 GB).
- Capture: 10,308 s. **0 rejects.**
- Reverse-order repeat: 500 passes / 742 stems, all sample-exact.
- Band audit: 0 violations. Hash guard: no collisions.
- Settings: PR U(0, 0.85); Gain log-uniform over [0.10, 0.65] (2,112 passes below 0.2); mode 50/50.

**Training data:** C2 corpus plus the C1 FIT renders. DEV is the C1 DEV/DEV0 set, used for
selection only.

**Dense static grids NOT whitelisted (flagged):**
- `opto-programme-20260916/static-grid` and `opto-o1-settled-20260921/static-grid-limit` are
  999 Hz tones, inside the witness exclusion band.
- Only settled values remain; there is no audio.
- The standing rule wins.

**Gain floor 0.10, not 0.08:** below knob about 0.099 the UAD's Gain taper falls from −22 dB to
mute. Measured: −214 dBFS output at 0.0845. The two rejected passes in the first 50-pass check were:
- one real silent UAD output at Gain 0.0845;
- one capture-script false reject of a C2 idle clip, caused by name matching.

Both were fixed, and the 50-pass re-check was clean (0 rejects).

**Training:**

| Stage | Setup | Time | Result (DEV) |
|---|---|---|---|
| Phase 1 | 10 epochs over 8,014 FIT items | 2 h 54 min | frame error 0.884 dB |
| Phase 2 | A and B from the same phase 1, 3 × 1,000 items, epoch 2 selected for both | 1 h 44 min | A ESR 0.048; B envelope 0.87, spectral 2.56, harmonic 5.24 dB, ESR 0.058 |

## Verification (section 6)

**Flag-OFF bit-null: holds, but the reference moved.**
- The flag-OFF AU now hashes `f7723433…`, not the installed r11a-phys `4aea742e…`.
- **Control:** a pristine `git archive` of the r11a-phys commit `c9a5d3fb`, built against the same
  current DAF, gives the **identical** `f7723433…`.
- The difference is entirely the DAF checkout. It changed on 2026-10-01 between 21:11 and 21:18,
  by merging main into `dusk/inherited-bug-fixes` (vst3, filebrowser, dgl and `DAF-plugin.cmake`).
- My changes are bit-null. Note that the installed r11a-phys binary no longer matches what the
  current DAF builds from the same source.

**Other checks:**

| Check | Result |
|---|---|
| CTest, flag OFF | **19 of 20**, the same known MultiCompCore failure ("Opto H2-H5 and even-to-odd balance…") |
| Installed AU restored | r11a-phys (`4aea742e…`) |
| auval, both learned AUs (c2A `86dbb041…`, c2B `867c8ed2…`) | pass |
| Latency by cross-correlation, both | 73 |
| Mono ≡ L=R through the AU, both | bit-exact |

The pipeline's full flag-OFF build failed at the JACK standalone link: the installed SDL2 is
x86_64-only, the pre-existing environment issue AGENTS.md notes. The AU and all CTest targets
built and were verified separately.

## Fixed along the way

1. **Mac sleep.** The capture stalled for 67 minutes because the Mac slept on battery with the lid
   closed. Long jobs now run under `caffeinate`, and you were asked for AC power with the lid open.
2. **Capture script.** Silent-output rejects are now judged from the stimulus's own peak, not from
   its name. The Gain floor is set from the measured UAD taper.
3. **Element.** The power basis was replaced by the Chebyshev basis after the overfit test failed.
4. **Phase 2 crash.** It crashed on its very last step because the scheduler was one step short:
   per-corpus batching adds a step or two per epoch. It now precomputes every epoch's batches, and
   phase 2 was rerun in full (lost: about 1.5 h).
5. **Timeouts.** An earlier detached capture launch had a 2 h shell time limit; it was moved to
   `nohup`.

## Removal candidates (owner decides; nothing deleted)

**Update, 2026-10-02 (owner ruling).** The learned engine is now removed from the working tree,
uncommitted. Its source is preserved in `opto-e2e-20261001/parked-c1c2-state/`; see
`opto-e2e-closure-2026-10-02.md`. The evidence deletion-candidate list is in
`opto-e2e-20261001/deletion-candidates-*.txt`.

- Plugins repo, uncommitted:
  - `plugins/multi-comp/core/MultiCompOptoLearned.hpp` and `MultiCompOptoLearnedWeights.hpp`;
  - the `#if MC2_OPTO_LEARNED` blocks in `MultiCompDSP.{hpp,cpp}`;
  - the `MC2_OPTO_LEARNED` option in `daf-plugin/CMakeLists.txt`.
- The committed `external/RTNeural` submodule (`94d9cacb`).
- Build dirs: `build-mc2-e2e/`, `build-mc2-e2e-off/`.
- Evidence: `build-multi-comp-1176/opto-e2e-20261001/` (C1 + C2 captures and renders, about
  45 GB).
- The scratchpad pristine export.
- Tools-repo scripts under `opto/e2e/`. Programs stay in the tools repo by rule, so I'd keep these
  as the record.

## Where things are

**Reports:** this file and the C1 report.

**Evidence:** `build-multi-comp-1176/opto-e2e-20261001/`:
- `c2/corpus`, `c2/element-probe`, `c2/gate-probe`;
- `c2/reachability.json` (and `-v1-power-basis.json`), `c2/overfit/`;
- `c2/runs/{phase1,c2A,c2B}`, `c2/pipeline.log`;
- `pilot/eval/{c2A,c2B}`;
- `pilot/dev-gr-c2B-vs-r11a-phys.json`.

**Witness tables:** `opto-production-harmonics-20260928/checkpoints/e2e-c2{A,B}/`.
