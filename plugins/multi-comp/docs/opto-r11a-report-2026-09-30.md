# Opto R11a report — 2026-09-30

Everything below was measured through the actual Audio Units (UADx LA-2A and
MC-2, `duskverb_render`, 48 kHz, 512-sample blocks, 2 s pre-roll, linked
stereo, latency compensated, MC-2 neutral). Tone-only synthesis is off in both
builds (`kOptoToneSynthesisEnabled = false`). No fitting was done this round.

- **Current build** (`r11a-base`, AU `59d3de4d...`): the classifier-gated
  corrections T1–T15 are live.
- **Physical base** (`r11a-phys`, AU `4aea742e...`, now the default build):
  `kOptoClassifierCorrectionsEnabled = false`, so T1–T15 are zero. The
  classifiers C1–C7 still execute but drive nothing. C8 and C9 (the cell's EL
  frequency law and LF floor) are kept; see the open questions.

## Scorecard, side by side

| Check | Bar | Current (classifiers on) | Physical base (off) |
|---|---|---:|---:|
| Fitted harmonic grid (14,056 eligible) | 2 dB | 4,478 | 4,477 |
| Interim validation, 70 / 400 / 3k Hz (6,190) | 2 dB | 1,904 | 1,913 |
| Interim validation, Gain 0.40 (2,419) | 2 dB | 500 | 501 |
| Intermodulation (1,205) | 2 dB | 357 (319 misses above -80 dBFS) | 377 (292) |
| Slow sweep (2,391) | 2 dB | 1,383 (609 misses above -80 dBFS) | 1,397 (595) |
| 1 kHz witness, H2..H7 (dB vs UAD) | 2 dB | -10.2 / -19.6 / -35.6 / -23.0 / -29.6 / -25.1 | -10.2 / -20.6 / -35.6 / -25.8 / -29.5 / -27.8 |
| Settled, vs the UAD fixture (122) | 0.5 dB | 121, worst 0.501 | 120, worst 0.653 |
| Settled, AU vs core (122) | 0.01 dB | 122, worst 0.0022 | 122, worst 0.00003 |
| Knee (9) | 0.1 dB | 9 | 9 |
| Recovery (20) | test bars | 20 | 20 |
| Charge (16) | fitted RMS < 0.42 / worst < 0.75; held-out RMS < 0.50 / worst < 0.75 | pass (0.074 / 0.207; 0.311 / 0.510) | **fail** (0.930 / 2.017; 0.507 / 0.689) |
| Dense programme (4) | mean / RMS / corr | 4 | 3 (PR 40 mean -0.447 against a 0.35 bar) |
| Two-tone combinations (280) | ≥ 90% within 0.3 dB | 110 (39%) | 88 (31%) |
| Two-tone single components (140) | 0.3 dB | 112 | 133 |
| Pink, full band (26) / octave bands (96) | 0.3 / 0.15 dB | 5 / 29 | 4 / 28 |
| Stereo link: C1 / C2 (437) / C3 / C4 | — | pass / 188 / fail / fail | pass / 188 / fail / fail |
| Music, VAL overall / high PR | — | 0.8613 / 1.2177 | **0.8248 / 1.1847** |
| Music bootstrap vs R9 | 95% CI | +0.0007 [-0.0003, +0.0016] | -0.0358 [-0.0703, -0.0056] |
| Music bootstrap, physical minus current | 95% CI | — | -0.0365 [-0.0718, -0.0055]; high PR -0.0330 [-0.0802, -0.0041] |
| CTest | — | 19 / 20 | 19 / 20 |

Notes on the table:
- The physical base is better on music by about 0.036 dB with the CI excluding
  zero, but that is below the campaign's clip-noise floor (0.089 / 0.132 dB).
- C1 is measured on the output, because the AU exposes no cell-gain probe.
- The physical build's charge fail and dense PR 40 fail come from removing
  T4 (short-event charge) and T7 (programme): the gates those tables were fitted to.
- `MultiCompCore` fails on the known Opto gates in both builds.

The harmonic map, the interim set, the witness, intermodulation and the sweep
barely change between the builds. With the tone synthesis off, the Opto
harmonics are the physical model's in both. Neither the classifiers nor their
removal changes harmonic parity: the missing input element and cell fast
dynamics do.

## Side effects of the classifier corrections

**Sustained Limit offset (T8):** 1 kHz, Limit, PR 0.60, Gain 0.25.

| Input | UAD GR | Current | Physical | Current minus physical |
|---:|---:|---:|---:|---:|
| -24 dBFS | 2.138 | 2.285 | **2.209** | +0.076 |
| -12 dBFS | 11.837 | 11.944 | **11.845** | +0.099 |
| -3 dBFS | 20.411 | 20.557 | **20.508** | +0.049 |

There is a permanent offset of 0.05–0.10 dB in Limit, and the physical base is
closer to the UAD at every level.

**Low-frequency Limit (T4/T8 restarts), PR 0.35–0.85:**
- Settled GR: the corrections add up to +0.68 dB. For example, at 50 Hz /
  -12 dBFS / PR 0.50 the UAD measures 11.66, the current build 12.57 and the
  physical base 11.92 dB; at 100 Hz / -12 / PR 0.50 the figures are 12.10,
  12.84 and 12.16 dB. Over the 24 cells the physical base is closer to the
  UAD in 9, the current build in 7, and 8 are ties within 0.005 dB.
- The within-cycle notch is confirmed where predicted. The near-zero-crossing
  gain exceeds the gain at the peaks by +0.28 to +0.30 dB in the current build
  at 50 Hz / -18 dBFS / PR 0.50–0.70, against +0.02 to +0.04 in the physical
  base.
- The UAD itself shows +0.13 to +0.54 dB of within-cycle variation at 50 Hz
  (its ripple). The physical MC-2 shows +0.01 to +0.16 dB. This independently
  confirms the missing fast cell dynamics, which R12 addresses; the classifier
  notch was partly, and accidentally, imitating them.
- Per-cell data: `r11a-scorecard/sidefx-{uad-base,mc2-r11a-base,mc2-r11a-phys}.json`.

## Other R11a results

- **Latency:** 73 samples (impulse peak 73, cross-correlation 72.975). MC-2
  reports 73.000 to the host at every oversampling setting. The UAD measures 87.
- **Output stage** (the pass bar was set before fitting, and nothing was
  fitted): the DC tail is within 0.3% of the UAD at every level and Gain (8/8),
  and the probe gain within 0.0023 dB. The stage is unchanged. R10's DC-tail
  explanation of the memory gate is falsified as a stage error; the memory law
  was tuned to R9's wrong-sign tail.
- **Harness sign convention:** verified with a synthetic known-sign curve.
  The UAD's H2 at 180 degrees is a negative even part.
- **Classifier audit:** `opto-r11a-classifier-audit-2026-09-30.md`.

## Open questions

- C8 (the cell's EL slew-ratio frequency estimate) and C9 (the LF-floor
  energy ratio) act continuously on every signal, but they are still
  estimators of the input's frequency content. Are they physical (EL
  brightness rising with frequency; the cell unable to follow LF ripple), or
  stand-ins to be replaced by the fast-dynamics model in R12?
- Removing the classifiers fails charge and dense PR 40. Those are dynamics
  laws that R11b and R12 must rebuild physically, from separate data, not
  from the fixtures.

## Process notes

- The physical-build pipeline lost about two hours because its script was
  not executable. Its fitted-grid capture had to be launched separately.
- One validation worker crashed on a shared temporary file (now
  per-process) and was rerun.
- The `.metadata_never_index` marker was added to `build-multi-comp-1176` to
  stop Spotlight indexing the evidence.
- New AU tools: `tests/opto_au_scorecard.py` (the done-criteria scorecard
  through the AUs; it reproduces the core recovery instrument to 0.001 dB) and
  the R11a commands in `tests/opto_au_harmonic_map.py` (DC tail, interim
  validation, intermodulation and sweep).
