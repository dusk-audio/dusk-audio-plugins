# TapeMachine A800 → UAD Studer A800 tuning — summary

Tuned TapeMachine's **A800 mode** (`Swiss800`) to match the **UAD Studer A800** (factory
defaults) across all 7 measurement categories. All DSP changes are gated to
`machine == Swiss800`; **Classic102 is untouched** and still processes/validates.
AU passes `auval`.

## Before → after (456 / NAB / 15 IPS / Repro unless noted)

| Category | Metric | Baseline | Tuned | UAD target |
|---|---|---|---|---|
| **1 FR** | mean matrix error (18 configs) | 1.47 | **0.90** | 0 |
| | head bump @30 IPS | collapsed +1.1 | **+4.2** | +4.1 |
| | HF −3 dB @15 IPS | 15.8 k | **~20 k** | 20 k |
| **2 Harmonics** | 3rd @−6 dBFS | −27 (harsh) | **−38** | −36 |
| | 2nd (even, amp colour) | −148 (none) | **−60** | −61 |
| | 5th / 7th | −80 / absent | **−56 / −62** | −50 / −57 |
| | THD @−6 | 4.68% | **1.3%** | 1.64% |
| | SMPTE IMD | 0.59% | **2.88%** | 3.79% |
| **3 Transient** | crest factor | 29.0 (under) | 25.6 | 27.2 |
| **4 Path** | Repro HF@10k | −0.5 | **−0.8** | −0.31 |
| | Sync HF@10k | −2.1 | −2.4 | −0.31 |
| **5 Bias** | THD under/over-bias | inert 2.25/2.25 | **13.8 / 0.28%** | 14.3 / 0.87 |
| | HF vs bias | inert | **+? → −2 dB** tracks | +3.8 → −1.7 |
| **6 Noise** | idle hiss | none | **−81 dBFS** | −82.5 |
| | mains hum fraction | none | **−4.0 dB** | −3.3 |
| | crosstalk L→R | −46 dB | **−64 dB** | none |
| **7 Digital** | aliasing (hot HF) | −38 dB | **−63.2** | −63.4 |

## DSP changes (all in `plugins/TapeMachine/core/`)
- **Head bump** → machine+speed-specific lookup `getHeadBump()` (A800 tuned; Classic102 reproduces original), wider clamps for A800.
- **HF extension** → `hfLossScale` 0.3 on the two HF-loss shelves for A800.
- **Saturation** → replaced the 3-band Jiles-Atherton with a **measured transfer-curve
  waveshaper** (`a800Shaper`, order-7 polynomial + tanh knee) fitted to the UAD A800's
  harmonics-vs-level (`transfer_fit.py`, 0.27% residual), + a small even-order term.
- **Bias** → A800 bias now drives the waveshaper (under-bias → more THD) and an
  HF shelf fitted to UAD's bias-vs-HF curve.
- **Idle noise** → new `a800IdleNoise()` (pink hiss + 60 Hz hum), Noise knob 100% = UAD.
  Fixed a real bug: `processSample` early-returned 0 on silence, so idle noise never played.
- **Crosstalk** → A800 bleed reduced to −64 dB; **Sync** gap no longer doubled for A800.

## Code-review fixes applied (post-tuning)
- Hoisted per-sample transcendentals out of the audio loop: `biasDrive = std::exp(...)`,
  `a800Drive`, and the `1/drive` makeup are now cached in `updateFilters` (`m_a800Drive`,
  `m_a800DriveInv`); the 60 Hz hum step is cached in `prepare` (`m_humPhaseInc`). Verified
  behaviour-identical (harmonics/THD/bias unchanged).
- `a800Shaper` knee divide replaced by a `* kSkneeInv` multiply.
- `getHeadBump` made `const noexcept`.

## Aliasing — RESOLVED (−63.2 dB, UAD parity)
The dominant alias source was **NOT the waveshaper** — it was `preSaturationLimiter`, a hard
clip at ±0.95 that hot pre-emphasised HF drives ~6 dB past its ceiling (control experiment:
shaper+limiter bypassed → floor −119 dB; shaper alone bypassed → −44, i.e. shaper was
*masking* limiter distortion). The earlier −47 dB reading was also partly a measurement trap:
rendering with shipped W&F defaults (Wow 7/Flutter 3) produces ±65 Hz flutter FM sidebands the
`aliasing()` metric counts as spurs; with `Wow=0 Flutter=0` (deep_probe's config) the true
pre-fix floor was −56.3 dB (15 kHz tone: 13th-order product at 195 kHz folds to 3 kHz at the
192 kHz OS rate — immune to every downstream decimation filter).

Fix (A800-gated, cheap): both A800 nonlinearities run inside their own **single non-nested
local 2× halfband stage** (`Local2xStage`, stage-B 15-tap taps, −75 dB stopband) so fold
products land above audio and get filtered; and the A800's input limit is a **value+slope-
matched tanh knee** (linear to 0.8, ceiling 0.95 — `a800SoftLimit`) instead of the hard clip.
Either alone is insufficient (soft knee alone −54; shaper-2× alone −56); together: **−63.25 dB
vs UAD −63.36**, with alias-tone HF levels preserved to 0.01 dB, harmonics/THD/IMD/bias/noise
unchanged, and transient crest *improved* 25.6 → 26.3 dB (UAD 27.2). Adds ~14 OS-rate samples
(≈3.5 base samples) of A800-only group delay, not reported in PDC (negligible for a tape emu).
ADAA was not needed; the nested-halfband/ADAA interaction hypothesis was never confirmed.

## Known residuals (not fully matched)
- **Sync** ~2 dB darker than UAD (a record-head filter still applies in Sync).
- **Transient** ~1.6 dB more peak compression than UAD (waveshaper is memoryless).
- **CCIR** slightly less accurate than NAB; **hot-level HF** dips more than UAD.

## Reproduce
`gen_stimuli.py` → `render_matrix.py` → `score_matrix.py` (FR/THD/W&F matrix) and
`deep_probe.py` (categories 2–7). `transfer_fit.py` regenerates the waveshaper coefficients.
