# DuskVerb anchored-preset scoreboard — 2026-06-11

Gain-matched full_check via `parallel_check.py` (6-worker), 100%-wet renders,
**sustained-correct harness** (`--sustained-pink-seconds 4.0`, so the ripple /
mod-freq / ss-energy / per-band edt+decay families actually run on the sustained
render instead of falling back to the noiseburst floor).

> ⚠️ These numbers are HIGHER than `scoreboard_2026-06-10.md`. That board predates
> the sustained-gate fix — its sustained family was silently skipped, so every
> n_fail there was optimistic (it's what made Bright Hall read "9" when the honest
> number was 16). This board is the truth. The increases below are measurement
> honesty, NOT regressions — the only real engine change since is Bright Hall
> (32-line migration, a genuine 16→13 improvement + the metallic-tail fix).

| Rank | Preset | Engine | n_fail | prev board (stale) | Notes |
|---:|---|---|---:|---:|---|
| 1 | Tiled Room | Hall | 26 | 17 | early-field wall (anchor first50 ≈ 69%) dominates |
| 2 | Medium Drum Room | Hall | 25 | 17 | early-field + boom; front-loaded room |
| 3 | Ambience | Hall | 23 | 20 | early-field (first50 ≈ 54%) + width |
| 4 | Blade Runner 224 | Hall | 20 | 11 | recal'd; long-tail hall |
| 5 | Cathedral Large Hall | Hall | 19 | 13 | large hall, late-field + ss |
| 6 | 79 Vocal Chamber | Room | 18 | 10 | QuadTank gain==decay==level coupling wall |
| 7 | Vocal Hall | Hall | 16 | 16 | early-field + boom/body |
| 7 | Drum Plate | Hall | 16 | 16 | de-honked; boom/env_shape |
| 9 | Vocal Plate | Hall | 13 | 13 | 1 kHz pteq is poison; clean floor |
| 9 | Bright Hall | Concert Hall | 13 | 16 | **32-line dense FDN: 16→13 + tail kurtosis ~18→14.9 (metal)** |

Engine names are the standardized 2026-06-11 labels (Hall = AccurateHall algo 10;
Concert Hall = AccurateHall32 algo 12; Room = QuadTank algo 3).

## Structural walls (fleet-wide, dominate the leaders)
- **Early-field wall** (attack/onset/energy_t50/first50 + diffusion_flux): worst
  on the front-loaded rooms — Tiled (first50 ≈ 69%), Ambience (54%), MDR. No
  FDN/tank preset tuning closes it; needs the early-field engine (SparseField was
  the prototype, falsified under sustained-correct gates — see memory).
- **FDN gain==decay==level coupling** (Vocal Plate, 79 Vocal Chamber, Cathedral):
  feedback gain sets decay AND level per band; per-band level trims drag T60.
- **boom / body late-window curvature** (VH/DP/MDR): anchor holds early level,
  DV decays exponentially; EDT shaper stays disabled (documented IMD).
- **Modal residuals** (Bright Hall): 1 kHz steady overshoot + 12.9 kHz needle are
  delay-placement artifacts of the 32-line set — not closable by the inert
  Mid/Treble Multiply (octave GEQ flattens 3-band damping) or broadband Hi Cut.

## Method notes (carried forward)
- Re-measure the baseline on the CURRENT harness before any never-worse call —
  the stale-board "9" nearly triggered a wrong Bright Hall revert.
- Optuna cold-start only; manual off per-gate deltas for already-tuned presets.
- The 32-line AccurateHall32 is a built/bit-null engine slot; only Bright Hall
  ships on it. FDN / Vintage Hall / Sparse engines are unused but selectable.
