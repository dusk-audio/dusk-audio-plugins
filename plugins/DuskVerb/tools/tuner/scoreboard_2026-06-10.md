# DuskVerb anchored-preset scoreboard — 2026-06-10

Gain-matched full_check (parallel_check.py), 100%-wet renders, current harness.
Composite-exact octave GEQ era (commit 9d00545): every old leaky-cascade
"T60-ceiling / kept-on-FDN" verdict re-evaluated this round.

| Preset | Engine | n_fail | prev | Δ | Notes |
|---|---|---:|---:|---:|---|
| Bright Hall | AccurateHall | 9 | 18 | -9 | migrated off VintageTank 2026-06-10 |
| 79 Vocal Chamber | QuadTank | 10 | 10 | 0 | AccurateHall trial REJECTED (23 — late-heavy field wrong for tight chamber) |
| Blade Runner 224 | AccurateHall | 11 | 15 | -4 | recal + 250 Hz decouple (350 Hz pteq) |
| Vocal Plate | AccurateHall | 13 | 14 | -1 | 5 kHz post-tank tilt; 1 kHz pteq is POISON on VP |
| Cathedral Large Hall | AccurateHall | 13 | 26 | -13 | migrated off FDN 2026-06-09 |
| Drum Plate | AccurateHall | 16 | 31 | -15 | migrated off FDN 2026-06-10; pteq 1k+8/150+3.5 |
| Vocal Hall | AccurateHall | 16 | 20 | -4 | tank 0.42→0.50 + pteq 70 Hz softened |
| Tiled Room | AccurateHall | 17 | 18 | -1 | migrated; residual = energy-arrival wall (anchor first50 69%) |
| Medium Drum Room | AccurateHall | 17 | new | — | NEW preset, VVV "Fat Snare Room" anchor (re-captured via --nparam) |
| Ambience | AccurateHall | 20 | 21 | -1 | Width 1.42→1.05 |

## Structural walls (unchanged, fleet-wide)
- diffusion_flux: all presets (dense-FDN vs sparse-modal) — early-field engine ticket.
- attack/onset/t50/first50: early-field wall; worst on front-loaded rooms
  (Tiled anchor first50=69%, Ambience 54%). MDR partially closed via
  er_boost 7 + er_rise 33 ms (the bimodal-attack landing trick).
- boom/body late-window curvature (VH/DP): anchor holds early level, DV decays
  exponentially; EDT shaper stays disabled (documented IMD).
- width-vs-corr 1:1 trade (global Width vs per-band targets).
- env_p2p / env_shape / tail pitch-chorus: modulation-topology character.

## Method notes this round
- Optuna remains cold-start-only and its proxy loss DIVERGES from full_check
  (MDR capped sweep "won" at 23 internal, scored 27 on full_check; hand row 24).
- pteq tuning: ONE band at a time; gain-match renormalization turns multi-band
  edits into whack-a-mole (every preset this round confirmed it).
- In-loop peaking (fbInLoopDb) must be 0 on AccurateHall presets.
