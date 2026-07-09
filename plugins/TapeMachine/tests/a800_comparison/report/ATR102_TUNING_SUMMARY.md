# TapeMachine Classic102 → UAD Ampex ATR-102 tuning — summary

Tuned TapeMachine's **Classic102 mode** (`Classic102`, machine index 1) to match the
**UAD Ampex ATR-102** (factory defaults, Transformer **On**) across the 7 measurement
categories. All DSP changes are gated to `machine == Classic102`; **Swiss800 (A800) is
byte-identical** to before this campaign (verified: 6 A800-gated render paths, md5-equal).
AU passes `auval -v aufx DsTM Dusk`.

Reference config: 456 / NAB / 15 IPS / Repro / +6 cal, both plugins, unless noted.

## Before → after

| Category | Metric | Baseline | Tuned | ATR-102 target |
|---|---|---|---|---|
| **2 Harmonics** | 3rd @−6 dBFS | −25 (harsh) | **−51** | −52 |
| | 2nd @−6 dBFS | −34 | **−55** | −56 |
| | THD @−6 | 6.2% | **0.35%** | 0.55–0.88% |
| | SMPTE IMD | 1.45% | 0.72% | 1.47% |
| **3 Transient** | crest factor | 27.2 | **28.3** | 27.0 |
| **1 FR** | Repro HF @10k (clean) | −1.00 | **+0.40** | +0.88 |
| | Repro LF @30 Hz | −2.35 | **+1.63** | +2.97 |
| | Repro @15k | −3.76 | **−1.00** | +0.13 |
| **4 Path** | Sync HF @10k | −2.76 (dark) | **−1.24** | −0.83 |
| | Input HF @10k | +0.09 | +0.09 | +0.00 |
| **5 Bias** | THD low/nom/high | 6.0/6.2/4.7 (inert) | **14.3/0.35/0.18** | 23.8/0.88/0.48 |
| | HF vs bias (nom/high) | +0.25/+0.50 | **+0.40/−0.89** | +1.66/+0.04 |
| **6 Noise** | idle hiss | none (−240) | **−80.1 dBFS** | −79.0 |
| | hum fraction | none | **−2.1 dB** | −2.9 |
| | crosstalk L→R | −36.5 dB | **−51.4 dB** | −51.4 |
| **7 Digital** | aliasing (hot HF) | −39.6 dB | **−63.3** | −60.3 |

## DSP changes (all `machine == Classic102`, in `plugins/TapeMachine/core/`)

- **Saturation** → replaced the 3-band Jiles-Atherton with a **measured transfer-curve
  waveshaper** (`classicShaper`, order-7 polynomial + value+slope-matched tanh knee)
  fitted to the ATR-102's harmonics-vs-level (`transfer_fit.py`, 0.32% residual), driven
  by `m_classicDrive` (base 1.4). A small `x²` even-order term (`kClassicEven = 0.017`)
  restores the ATR's 2nd harmonic. Both the waveshaper and a smooth-knee soft limit run
  inside their own `Local2xStage` (local 2× halfband) so upper harmonics don't fold —
  identical anti-aliasing structure to the A800.
- **Transformer bypassed** for Classic102 (`m_hasTransformers = false`): the fitted curve
  captures the ATR's whole chain (its transformer is On), so the discrete transformer would
  double-count. This is the Classic102 analogue of the A800 dropping the J-A.
- **FR (all speeds)** → the ATR was A/B'd across the full settings matrix (speed × tape × EQ × cal),
  not just the 15 IPS reference. Per-speed `getHeadBump` Classic102 rows were fitted to the ATR's
  measured bump — **+2.3 dB @30 (7.5 IPS), +3.0 (15 IPS), and a −4.7 dB LF DIP at 30 IPS**
  (the ATR rolls the low end off at 30 IPS instead of bumping it; the head-bump gain clamp now
  allows a negative gain, min −6 dB, for Classic102). HF is kept ~flat to 15 kHz at every speed
  via a Classic102-specific `hfExt` (1.30/1.10/0.90 for 7.5/15/30 — the shared 0.7/1.0/1.3
  extension darkened 7.5 by ~5 dB), `hfLossScale 0.2`, `hfRolloffFreq 21 kHz`; **dc-blocker at
  15 Hz for Classic102** (A800 keeps 25 Hz) so the low bump survives the LF highpass. Result: FR
  now matches the ATR within **~1.4 dB on 456 across all speeds/EQ** (≤2.4 dB on the 250/GP9
  formulations at 7.5/30 IPS — a documented tape×speed residual), versus up to **4.8 dB off** at
  7.5/30 IPS with the old single-config tuning. See `matrix_probe.py` for the 18-config sweep.
- **Bias** → Classic102 gets its own **steeper** bias→drive curve
  (`exp(6.5·(0.5−bias))`, clamp 0.15–9) so under-biasing drives the ATR's dramatic
  distortion rise; bias→HF shelf inverted (under-bias brightens).
- **Idle noise** → the A800's `a800IdleNoise` was refactored into a byte-identical
  `idleNoise(scale…)` helper; Classic102 now emits constant hiss+hum via `classicIdleNoise`
  (matches the ATR "Hiss & Hum"), replacing the signal-modulated `generateNoise`.
- **Crosstalk** → Classic102 bleed reduced to −51.4 dB (`crosstalkAmount 0.0027`), matching
  the ATR "Crosstalk On".

## ATR front-panel toggles

The ATR-102's 5 switches (Crosstalk, Wow & Flutter, Auto Cal, Hiss & Hum, Transformer) are
**not** replicated 1:1. Classic102 is tuned to the ATR's default state (all reference renders
used Transformer **On**), and each toggle maps to an existing TapeMachine control: Crosstalk →
always modelled; Wow & Flutter → the Wow/Flutter knobs; Auto Cal → Auto Calibration; Hiss & Hum
→ the Noise Amount knob; Transformer → folded into `classicShaper` (we emulate Transformer-On).

## Known residuals (root-caused, not fully matched)

- **THD absolute** (0.35% vs 0.55–0.88%) and **IMD** (0.72% vs 1.47%): the ATR's distortion
  is **5th-harmonic-heavy** (its 5th, −47, is *hotter* than its 3rd, −52) and its IMD is
  **dynamic** (record/repro amp memory). A memoryless near-linear waveshaper reproduces the
  3rd/2nd (the acceptance metrics) but under-produces the 5th and the dynamic IMD, so it lands
  *cleaner* than the ATR. Re-enabling the transformer for IMD was measured and **rejected**:
  at the drive giving IMD ≈ 1.25% it pinned the 2nd at −33 dB / THD 2.2% (targets −56 / 0.55%).
- **Bias-HF at deep under-bias** (−4.86 vs +5.01) and **hot-level HF** (−1.08 vs +0.87): the
  memoryless waveshaper **darkens** HF under heavy drive, the opposite of the ATR's
  post-saturation HF EQ. Matched at nominal/over-bias; the deep under-bias HF boost is only
  partly recovered. Two gates (bias-THD wants heavy drive, bias-HF wants brightness) conflict
  on this topology — a shared root cause, documented rather than force-fitted.
- **Sync LF** — the ATR's Sync path has a broad bass boost (+4 dB @80 Hz, record-head
  monitoring) not modelled; Classic102 Sync tracks Repro. Sync HF is "not too dark" (−1.24).
- **Input LF** — slightly bright below 60 Hz (the 15 Hz dc-blocker that helps Repro's bump
  also lifts Input LF); the ATR's Input has more LF rolloff.

## Cleanup (done)

Replacing the Classic102 J-A + transformer + signal-modulated noise orphaned a set of classes
that were then **removed** (TapeMachineDSP.hpp 1805 → 1349 lines, A800 re-verified byte-identical):
`JilesAthertonHysteresis` + `hysteresisBass/Mid/Treble` (+ `getJAParams`/`setFormulation`),
`ThreeBandSplitter`, `getBandDriveRatios`, `ImprovedNoiseGenerator::generateNoise`,
`SoftLimiter preSaturationLimiter`, the `TransformerSaturation` in/out stages + `m_hasTransformers`,
and the already-dead `TapeSaturator`. Verbose multi-line rationale comments were also collapsed to
concise one-liners (the full rationale lives in this report + memory). In the UI, all hover
tooltips (`ImGui::SetTooltip`) and their `tip` plumbing were removed, and the DAW
`getDescription()` string was emptied. Build clean (no warnings); `auval` still passes.

## Reproduce

`DEEP_MACHINE=ATR102 python3 deep_probe.py` (categories 2–7 + hot-FR),
`python3 fr_probe.py` (clean W&F-off Repro/Input/Sync FR), `python3 probe_mine.py`
(fast Classic102-only harmonics/THD/IMD/crest/aliasing). `FIT_INPUT=renders/deep_atr/fit_src.wav
FIT_FUNC=classicShaper FIT_ORDER=7 python3 transfer_fit.py` regenerates the waveshaper coefficients.
