# TapeMachine A800 vs UAD Studer A800 — 2026-07-08

Matched settings: Studer A800 / tape 456 / NAB / Repro / +6 dB cal / unity I-O, 48 kHz, TapeMachine at 4× oversampling. Frequency-response curves are level-matched to 0 dB across 300 Hz–3 kHz. THD from a 1 kHz stepped-level tone. Wow & flutter by Hilbert FM-demodulation of a 3150 Hz tone.


## 15 IPS

### Frequency response

| Metric | TapeMachine | UAD A800 | Δ |
|---|---|---|---|
| head_bump_db | 2.06 dB | 2.95 dB | -0.89 |
| head_bump_hz | 51.4 Hz | 31.6 Hz | 19.8 |
| hf_minus3db_hz | 15838.0 Hz | 20000.0 Hz | -4162.0 |

![fr](fr_15ips.png)

### THD vs input level

| input dBFS | TapeMachine % | UAD % | Δ |
|---|---|---|---|
| -30 | 0.048 | 0.178 | -0.130 |
| -24 | 0.088 | 0.337 | -0.250 |
| -18 | 0.224 | 0.606 | -0.382 |
| -12 | 1.011 | 1.006 | +0.005 |
| -6 | 4.648 | 1.642 | +3.006 |
| -3 | 5.563 | 2.460 | +3.102 |

![thd](thd_15ips.png)

### Wow & flutter

_TapeMachine at shipped Wow=7 Flutter=3; the UAD A800's transport flutter is intrinsic (no user control). Headline = robust (MAD-based) pitch deviation, which was validated to track the Wow knob monotonically. The band-split and spectrum are INDICATIVE only — the FM-demod of the oversampled/saturated tone throws frequent glitches (see the p1–99 span), so treat sub-metrics as directional, not exact._

| Metric | TapeMachine | UAD A800 | Δ |
|---|---|---|---|
| **pitch dev (robust, %)** | **0.338** | **0.000** | +0.338 |
| wow 0.5–6 Hz (indic.) | 0.107 | 0.071 | +0.036 |
| flutter 6–100 Hz (indic.) | 0.066 | 0.041 | +0.025 |
| p1–99 span (glitch gauge) | 100.526 | 1523.810 | -1423.284 |
| glitch fraction (%) | 26.780 | 26.780 | +0.000 |

![wf](wf_15ips.png)

### Noise floor (silence in, tape noise enabled)

_Not level-matched: TapeMachine Noise Amount=50, UAD Noise=On — nominal settings, compared for spectral character._

| Metric | TapeMachine | UAD A800 | Δ |
|---|---|---|---|
| broadband RMS | -240.0 dBFS | -82.5 dBFS | -157.5 |

![noise](noise_15ips.png)

### Overall difference

Level+latency-aligned residual (sweep, mine vs UAD): **-0.4 dB** (0 dB = totally different, −∞ = identical).


## 30 IPS

### Frequency response

| Metric | TapeMachine | UAD A800 | Δ |
|---|---|---|---|
| head_bump_db | 1.08 dB | 4.15 dB | -3.07 |
| head_bump_hz | 75.9 Hz | 54.9 Hz | 21.0 |
| hf_minus3db_hz | 18759.0 Hz | 20000.0 Hz | -1241.0 |

![fr](fr_30ips.png)

### THD vs input level

| input dBFS | TapeMachine % | UAD % | Δ |
|---|---|---|---|
| -30 | 0.059 | 0.010 | +0.048 |
| -24 | 0.105 | 0.016 | +0.090 |
| -18 | 0.230 | 0.026 | +0.203 |
| -12 | 0.928 | 0.150 | +0.778 |
| -6 | 4.417 | 0.832 | +3.585 |
| -3 | 7.834 | 1.871 | +5.963 |

![thd](thd_30ips.png)

### Wow & flutter

_TapeMachine at shipped Wow=7 Flutter=3; the UAD A800's transport flutter is intrinsic (no user control). Headline = robust (MAD-based) pitch deviation, which was validated to track the Wow knob monotonically. The band-split and spectrum are INDICATIVE only — the FM-demod of the oversampled/saturated tone throws frequent glitches (see the p1–99 span), so treat sub-metrics as directional, not exact._

| Metric | TapeMachine | UAD A800 | Δ |
|---|---|---|---|
| **pitch dev (robust, %)** | **0.383** | **0.000** | +0.383 |
| wow 0.5–6 Hz (indic.) | 0.070 | 0.025 | +0.044 |
| flutter 6–100 Hz (indic.) | 0.094 | 0.017 | +0.077 |
| p1–99 span (glitch gauge) | 100.596 | 100.000 | +0.596 |
| glitch fraction (%) | 26.780 | 26.780 | +0.000 |

![wf](wf_30ips.png)

### Noise floor (silence in, tape noise enabled)

_Not level-matched: TapeMachine Noise Amount=50, UAD Noise=On — nominal settings, compared for spectral character._

| Metric | TapeMachine | UAD A800 | Δ |
|---|---|---|---|
| broadband RMS | -240.0 dBFS | -83.2 dBFS | -156.8 |

![noise](noise_30ips.png)

### Overall difference

Level+latency-aligned residual (sweep, mine vs UAD): **-0.4 dB** (0 dB = totally different, −∞ = identical).

