# Opto E2E learned capture: C1 pilot report (2026-10-01)

Plan: `opto-e2e-learned-plan-2026-10-01.md`, with the owner's additions written into it. The
measurement boundary is the real AUs (UADx LA-2A v1.0.8 b787 and MC-2), run through the pinned
`duskverb_render` (`opto-host-20260930`, `f9a0ec1f`) at 48 kHz, 512-sample blocks, 2 s pre-roll,
stereo linked, latency compensated (UAD 87 samples, MC-2 73), MC-2 neutral.

## Verdict: the pilot FAILS its pass bar, so I stopped and did not scale

B4 and B5 fail, and so does B1's export-parity sub-check as pre-registered. B2 and B3 pass.
Each failure has a root cause, given below.

| # | Owner's bar | Result | Verdict |
|---|---|---|---|
| B1 | Pipeline works end to end through the AU | Every stage ran: generator → UAD capture → train → export → learned MC-2 AU → render → score. Capture integrity, latency and auval pass. **Export parity is 3.29e-6 FS against torch float32, over the 1e-6 bar** | **FAIL** (parity sub-check only) |
| B2 | CPU ≤ 5% of one core per stereo instance | Full MC-2 core in learned mode: **3.99%** (60 s stereo music, 48 kHz, 512-sample blocks, one thread, median of 5). The physical engine is 8.29% on the same harness | PASS |
| B3 | L = R matches mono exactly | Bit-exact in the C++ harness, and through the AU (a 1-channel instance against a 2-channel instance fed L = R, both channels) | PASS |
| B4 | DEV GR error no worse than the physical base | Row-balanced frame-GR RMS on 90 DEV items: learned **1.139 dB**, physical 0.398 dB. Paired bootstrap, learned minus physical: +0.742 dB, 95% CI [+0.522, +0.981] | **FAIL** |
| B5 | Witness H2–H5 within 6 dB with the composite loss, and closer than the waveform-only loss | Composite: H2 −4.4, H3 −11.1, H4 −22.0, H5 −51.2 dB. Waveform-only: H2 −0.8, H3 −6.3, H4 −27.2, H5 −35.5 dB | **FAIL** (both conditions) |

## B5: the witness, and why the loss comparison could not work

These are actual AU renders. The 1 kHz witness band (970–1030 Hz) was excluded from training.

| Harmonic | UAD dBFS | Composite (B) − UAD | Waveform-only (A) − UAD | Physical base − UAD |
|---|---:|---:|---:|---:|
| H2 | −67.24 | −4.45 | −0.75 | −10.18 |
| H3 | −71.69 | −11.08 | −6.34 | −20.63 |
| H4 | −91.62 | −21.97 | −27.17 | −35.56 |
| H5 | −87.53 | −51.16 | −35.47 | −25.82 |
| H6 | −101.86 | −53.33 | −53.21 | −29.53 |
| H7 | −95.98 | −64.75 | −80.68 | −27.78 |

**Root cause: the input element I designed cannot reach the UAD's harmonic levels at this
signal level.**
- The element is `x + Σ a_k clamp(x)^k` with `|a_k| ≤ 0.1`. I bounded the coefficients after the
  first run diverged.
- At the witness amplitude (−16 dBFS peak, A = 0.158), each harmonic's largest possible value comes
  from its own term alone, as `0.1·A^k / 2^(k−1)`:

| Harmonic | Element ceiling | UAD | Gap |
|---|---|---|---|
| H3 | −80.0 dBFS | −71.7 | 8.4 dB short |
| H4 | −102.1 dBFS | −91.6 | 10.4 dB short |
| H5 | −124.1 dBFS | −87.5 | 36.6 dB short |

- The learned coefficients on a DEV-like tone (1.2 kHz, same level) sit at only 9–45% of the
  bound. Raising the bound alone would not fix it.
- A polynomial in absolute amplitude ties every harmonic ratio to level as `A^(k−1)`. So it cannot
  hold the UAD's profile, which is near flat across level once the element's knee is passed. It
  cannot do this at any bound.
- Neither loss can therefore reach the target. The A-versus-B comparison only measures which loss
  the parametrisation lets win on H2, and the B5 hypothesis "the loss is the lever" was not
  testable on this architecture.
- The physical base's H5–H7 sit closer only because its own (non-learned) curve produces them.

**Proposed fix, not built:** make the element act on level-normalised amplitude:
`y = x + e·Σ a_k(·)·(x/e)^k`, where `e` is a learned per-channel envelope with a floor.
- The bounded coefficients then set harmonic ratios directly at every level.
- The element still acts identically on any signal, and L = R ≡ mono is kept.
- Before any training, a reachability check (the analytic ceiling above, at −40, −16 and 0 dBFS)
  must show every UAD harmonic is reachable.

## B4: DEV gain reduction

| DEV family | n | Learned RMS dB | Physical RMS dB |
|---|---:|---:|---:|
| stereo | 14 | 1.37 | 0.30 |
| release | 12 | 1.62 | 0.24 |
| tone | 16 | 0.79 | 0.33 |
| two-tone | 16 | 1.16 | 0.39 |
| noise | 8 | 0.97 | 0.74 |
| burst | 16 | 1.05 | 0.44 |
| lf | 8 | 0.56 | 0.27 |

**The error is dominated by static offsets in the PR → GR law, not by dynamics:**
- `release_002` at 16.7 dB of UAD GR: mean −3.43 dB.
- `stereo_001` at 12.0 dB: mean +2.91 dB.
- `stereo_010`: the UAD applies 0 dB of GR, while the learned model compresses about 1.5 dB.

**Root cause: the pilot corpus had only 52 distinct (PR, mode, Gain) settings in FIT.**
- Each host pass renders 10 clips at one setting, so the 508 FIT renders cover 52 setting points.
- The physical base was fitted against dense UAD static measurements; the learned law saw 52
  points.
- The low-Gain end of the make-up law also underfits. Gain 0.13 items had a 6–7.5 dB frame error
  after phase 1, because there are few draws near the bottom of the steep taper.
- Phase 1 plateaued at a 1.52 dB train / 1.76 dB DEV frame error (underfitting, not
  overfitting).

**Proposed fix for the next pilot:** 1–2 clips per pass, with many more passes. The capture cost
is about 1 s per pass, so even 5,000 distinct settings is about 1.5 h.

## B1: export parity

| Comparison (trained pilot B, stereo test signal) | Max abs | RMS |
|---|---:|---:|
| C++ (float32 control) vs torch float32: **the pre-registered measure** | **3.29e-6** | 7.3e-7 |
| torch float32 vs torch float64 (the same model computed exactly) | 3.27e-6 | 7.1e-7 |
| C++ with a **double-precision control path** vs torch float64 | **8.25e-7** | 1.75e-7 |

**Root cause: float32 rounding in the recurrent control path, compounded over thousands of GRU
steps.** Both implementations carry it equally, so the export itself is correct.
- The plugin's outer oversampler's float32 round trip is only 1.7e-7, so it is not the source.
- Running the C++ GRUs and heads in double brings C++ to within 8.3e-7 of the exact model, under
  the bar.
- That adds 0.13% CPU (engine only: 3.83% → 3.97%).
- No float32 torch reference can pass a 1e-6 bar, because torch32 itself is 3.3e-6 from the exact
  model.

**Owner decision needed:**
- whether the parity reference becomes torch float64, i.e. the exact model, with the bar
  unchanged;
- whether the double-precision control path is adopted.

The evaluated build is float32. The double variant is kept at
`build-multi-comp-1176/opto-e2e-20261001/pilot/parity-double-control/`.

## Reported, not part of the bar: the pilot B AU subset

| Check | Pilot B (learned) | Physical base (R11a) |
|---|---|---|
| **Two-tone combinations (280)** | **38 (13.6%)**, RMS 1.45 dB | 88 (31%) |
| Two-tone single components (140) | 32 | 133 |
| **Link C1** | pass (L = R output ratio = 0 dB) | pass |
| **Link C2 (437)** | **1**, worst 2.44 dB | 188 |
| **Link C3 / C4** | fail / fail | fail / fail |
| Settled (122) | 23, worst 11.49 dB, RMS 2.88 dB | 120 |
| Recovery (20) | 4 | 20 |

These follow from the B4 static-law errors. They are the pre-registered pilot look; the pilot model
is discarded.

## What else was verified

**Capture integrity:**
- 76 UAD passes and 24 physical-MC-2 passes, 0 rejects.
- Reverse-order repeat: 68 of 68 stems sample-exact.
- Old host vs new host: 20 of 20 R14 stems sample-exact.
- Exclusion-band audit: 0 violations.
- Hash guard: 0 collisions against 328 same-shape stimuli.

**The physical default is bit-null:** with `MC2_OPTO_LEARNED=OFF`, the built AU binary is
byte-identical to the installed r11a-phys binary (SHA-256 `4aea742e…`). The r11a-phys AU is
installed again.

**CTest (flag OFF):** see the addendum at the end.

**The learned AU (both pilots):**
- auval passes.
- Latency by cross-correlation is 73, equal to the reported value.
- Mono ≡ L=R is bit-exact.
- Installed SHAs: A `96f47ba1…`, B `cdc1a49c…`.

## What went wrong today, and the fixes (all before the final runs)

1. **Host cwd.** The first DEV capture was rejected on `! session.wav stimulus not found`: the host
   looks for its built-in battery relative to the cwd. Every render now runs from the plugins
   repo root, as earlier tools did.
2. **Training run 1 diverged.** The ESR was taken per clip, so idle and near-silent clips divided
   by about 1e-12. It is now pooled over the batch with a −60 dBFS floor. I also bounded the
   element coefficients at this point, and that bound is what B5 exposes.
3. **LinFIR placement.** LinFIR fed only the detector, so it never reached the audio path. It now
   feeds both, as the plan intended.
4. **Training run 2 did not learn.** The cell-gain head's initial bias of −6 put softplus in its
   flat region (gradient about 0.0025), and the make-up moved about 0.01 dB per step. Both log-gain
   heads were rescaled (×3, bias −1/3), and a 4-clip overfit test confirmed learning before relaunch.
5. **Single-phase training was too slow** (about 4,000 item-updates in 4 h). I adopted the plan's
   two-phase schedule:
   - phase 1: control path only at frame rate, 40 epochs, DEV frame error 1.76 dB;
   - phase 2: end to end, 5 epochs, selection on DEV. A and B were both initialised from phase 1,
     so the loss comparison stays paired.
6. **A C++ namespace slip** (`GAIN_SCALE`) broke the first evaluation build. It was fixed and
   rebuilt.

**Deleted:** the only thing I deleted was my own 3-step smoke-test output (`runs/smoke`).
Superseded and diverged runs were moved aside, not deleted.

## Disk

The owner-approved cleanup removed 212 superseded paths (209.5 GiB). The inventory and the
resolved list are at `opto-e2e-20261001/cleanup-2026-10-01-resolved.txt`. Free space is 493 GiB.
The pilot uses about 9 GB.

## Recommendation (owner decides)

**Do not scale this architecture.** Instead, run a second pilot (about 1 day) with:
1. the level-normalised element, with the reachability check pre-registered;
2. 1–2 clips per pass and about 5,000 distinct settings, plus the whitelisted existing corpora in
   phase 1;
3. the double-precision control path;
4. your ruling on the parity reference.

The pass bar stays exactly as fixed.

## Where things are

**Code, uncommitted:**
- `plugins/multi-comp/core/MultiCompOptoLearned.hpp` (engine);
- `MultiCompOptoLearnedWeights.hpp` (generated, pilot B);
- `MultiCompDSP.{hpp,cpp}` (routing and branch, `#if MC2_OPTO_LEARNED`);
- `daf-plugin/CMakeLists.txt` (option, OFF by default).

**Tools:** `dusk-audio-tools/plugins/MultiComp/tests/programme_parity/opto/e2e/`.

**Evidence:** `build-multi-comp-1176/opto-e2e-20261001/pilot/`:
- `runs/{phase1,pilotA,pilotB}`;
- `eval/{pilotA,pilotB}`;
- `dev-gr-pilotB-vs-r11a-phys.json`;
- witness tables under `opto-production-harmonics-20260928/checkpoints/e2e-pilot{A,B}/`.

## Addendum: CTest (flag OFF)

`build-mc2-e2e-off`, full build and `ctest`: **19 of 20 pass** (286.8 s). Only MultiCompCore fails,
on the same pre-existing Opto gate as the r11a-phys baseline: "FAIL: Opto H2-H5 and even-to-odd
balance match all six measured points". That is R10's known 1 kHz harmonic-rows failure, and the
baseline `ctest-r11a-phys.log` is also 19 of 20. The learned engine is never compiled into any
CTest target. Log: `build-multi-comp-1176/opto-e2e-20261001/ctest-logs/c1-flag-off-ctest-2026-10-01.log`
(copied 2026-10-02 from `build-mc2-e2e-off/ctest.log`, which is a deletion candidate).
