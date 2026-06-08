# DuskVerb anchored-preset scoreboard — 2026-06-07

Source: current installed `DuskVerb.so` (branch 87-fix-fdn-quadtank), 100%-wet renders,
`full_check.py --json`. Gate families classified to separate CLEAN-tunable from coupling-wall
from STRUCTURAL.

## n_fail summary

| Preset | Engine | n_fail | Level | Decay(T60/decay-band) | EDT | Attack | Width | Structural |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Vocal Plate | FDN | 26 | 7 | 10 | 3 | 2 | 0 | 4 |
| Drum Plate | FDN | 23 | 8 | 5 | 4 | 2 | 0 | 4 |
| Bright Hall | VintageTank | 20 | 3 | 9 | 1 | 2 | 0 | 5 |
| Vocal Hall | FDN | 17 | 5 | 4 | 4 | 0 | 1 | 3 |
| Cathedral Large Hall | FDN | 26 | 6 | 9 | 3 | 2 | 1 | 5 |
| Blade Runner 224 | FDN | 23 | 6 | 7 | 4 | 2 | 1 | 3 |
| 79 Vocal Chamber | QuadTank | 21 | 7 | 5 | 5 | 0 | 1 | 3 |
| Small Drum Room | Dattorro | 26 | 9 | 6 | 5 | 2 | 1 | 3 |
| Tiled Room | FDN | 21 | 5 | 4 | 5 | 2 | 2 | 3 |
| Ambience | QuadTank | 25 | 4 | 10 | 3 | 2 | 3 | 3 |

(Classification approximate — some gates double-count a family; see per-preset lists below.)

## Fleet-wide patterns (the headline)

1. **`diffusion_flux` fails ALL 10** (Δ 1.57–20.66, gate ≤1.5). STRUCTURAL — FDN dense-Gaussian
   vs VVV sparse-modal echo density. Not tunable. ≈10 locked gates fleet-wide. [[duskverb_fdn_t60_coupling_wall]]

2. **`attack_time` + `onset_slope` fail 8/10** — FDN/QuadTank slow washy onset (DV 37–148 ms vs
   Lex 2–25 ms). ~26 ms shortest-line floor [[duskverb_fdn_attack_floor]] PLUS missing rising-ER.
   Cathedral worst (148 ms vs 2.3 ms). Small Drum Room is the OPPOSITE (3 ms, too sharp, +611%
   slope) — Dattorro fires too hard.

3. **`sine1k full RMS` (counts as 2 gates) fails 5 presets** — gross WET-LEVEL mismatch:
   VP −7.1 dB, DP −5.9, VH −3.0 (too quiet); BH +2.3, SDR +5.4 (too loud). **Cheapest fleet win:
   volume-match each preset's Gain Trim first** (memory: volume-match BEFORE spectral). ~10 gates.

4. **Per-octave `T60 NNN Hz` is the hard core**: VP 7, BH 7, Cathedral 8, Ambience 8, BR 4, DP 4,
   VH 4. This is the FDN/QuadTank decay-coupling wall — 9 octave gates vs 5 damping bands, and
   each band's g sets decay AND level together. Phase B output-makeup frees the level constraint
   so g can chase T60, but 9>5 means some stay structural.

5. **`ss_*` / band-energy fails are mostly "DV too quiet"** (negative Δ) → level, the Phase-B
   output-makeup target. Mixed with a few "too loud" bands → needs tilt, not flat gain.

6. **Width is live on 7/10**: stereo_corr or width-band off. VH +0.11, BR +0.19 corr too high;
   Tiled corr/low-width inverted; **Ambience grossly OVER-decorrelated** (width all 3 bands ≈
   −0.33 vs anchor ≈ −0.02/−0.11) — i.e. DV Width baked TOO WIDE; LOWER Width (opposite of the
   old note that said anchor is the decorrelated one — measured data says DV is). Clean lever.

## Per-preset failing gates (raw)

### Vocal Plate — FDN — 26
LEVEL: noiseburst RMS −2.44 · sub-bass<100 −8.20 · ss deep-sub −11.16 · ss sub −5.42 · ss low-mid +2.22 · sine1k −7.08 (×2)
DECAY(wall): decay sub +94% · decay low +71% · decay low-mid +23% · T60 125/250/500/2k/4k/8k/16k (+8..+48%)
EDT: edt sub −72% · edt low −42% · edt hi +34%
ATTACK: attack +28.4 ms · onset −83%
STRUCTURAL: diffusion 9.77 · env_p2p −55.5 · spec_L1 max 5.41@254 · pitch-chorus 0.28x

### Drum Plate — FDN — 23
LEVEL: snare RMS −3.46 · sub-bass +3.13 · boom sub −3.53 · boom low −3.08 · bloom 4-8k +2.25 · full RMS snare −3.76 · sine1k −5.85 (×2)
DECAY(wall): decay low +36% · T60 63/500/1k/16k (−9..−19%)
EDT: edt sub +139% · edt low +313% · edt low-mid −66% · edt hi +39%
ATTACK: attack +118 ms · onset −89%
STRUCTURAL: diffusion 7.30 · env_p2p −16.0 · spec_L1 max 6.15@12.9k · env_shape 4.72

### Bright Hall — VintageTank — 20
LEVEL: full RMS sine1k +2.31 (×2) · spec_L1 mean 3.15 · ripple high +1.70
DECAY(wall): decay sub −63% · decay low-mid −41% · T60 63/250/500/1k/2k/8k/16k (mixed ±)
EDT: edt low-mid −95%
ATTACK: attack +116 ms · onset −82%
STRUCTURAL: cent_50 −17.3% · diffusion 7.56 · spec_L1 max 8.06@12.9k · pitch-chorus 7.53x

### Vocal Hall — FDN — 17
LEVEL: ss deep-sub +2.09 · ss hi +(-2.69) · boom sub +3.75 · sine1k −2.95 (×2)
DECAY(wall): decay hi +20% · T60 63/4k/8k/16k (mixed)
EDT: edt sub −57% · edt low −53% · edt low-mid −32% · edt hi +69%
WIDTH: stereo_corr +0.11
STRUCTURAL: diffusion 6.18 · env_shape 5.02

### Cathedral Large Hall — FDN — 26
LEVEL: ss deep-sub −2.13 · boom low(500-1s) −3.05 · boom sub(1-2s) +4.17 · boom low(1-2s) −4.05 · body 250-500 −5.68 · body 500-1k −2.02
DECAY(wall): decay low-mid −25% · T60 63/125/250/500/2k/4k/8k/16k (−38..+20%)
EDT: edt sub −80% · edt low +338% · edt low-mid +569%
ATTACK: attack +146 ms · onset −98.6%
WIDTH: width hi >5k −0.07
STRUCTURAL: diffusion 20.66 · spec_L1 mean 4.16 · spec_L1 max 10.21@320 · pitch-chorus 6.96x

### Blade Runner 224 — FDN — 23
LEVEL: sub-bass −2.08 · hi 4-12k −4.28 · ss deep-sub −6.91 · ss upper-mid −2.24 · ss air +6.30
DECAY(wall): decay sub −25% · decay low −75% · decay low-mid +56% · T60 250/1k/2k/16k
EDT: edt sub −30% · edt low +136% · edt low-mid +171% · edt mid +267%
ATTACK: attack +82.6 ms · onset −89%
WIDTH: stereo_corr +0.19
STRUCTURAL: diffusion 12.81 · spec_L1 mean 2.88 · spec_L1 max 6.48@5.1k

### 79 Vocal Chamber — QuadTank — 21
LEVEL: sub-bass −2.25 · mid 1-4k −3.15 · hi 4-12k −8.98 · ss deep-sub −4.10 · ss upper-mid −2.34 · ss hi −9.02 · ss air +11.69
DECAY(wall): decay sub −58% · decay low-mid +31% · T60 4k/8k/16k (+23..+80%)
EDT: edt sub −42% · edt low +378% · edt low-mid +115% · edt mid +29% · edt hi +50%
WIDTH: width mid +0.06
STRUCTURAL: diffusion 8.22 · env_shape 4.95 · body 1-2k −3.19

### Small Drum Room — Dattorro — 26
LEVEL: snare RMS +3.36 · low 100-250 −5.48 · mid 1-4k −2.43 · ss deep-sub −5.16 · ss air +13.29 · bloom 8-12k +2.57 · full RMS snare +2.61 · sine1k +5.40 (×2)
DECAY(wall): decay low +35% · T60 63/125/250/1k/16k (mixed)
EDT: edt sub −51% · edt low +138% · edt low-mid −62% · edt mid +90% · edt hi +104%
ATTACK: attack −7.8 ms (too FAST) · onset +611% (too sharp)
WIDTH: width hi +0.12
STRUCTURAL: diffusion 1.97 · spec_L1 mean 3.70 · spec_L1 max 11.63@101

### Tiled Room — FDN — 21
LEVEL: snare RMS −1.92 · sub-bass +3.81 · ss air −4.28 · boom sub +8.21 · bloom 2-4k +3.87 · bloom 4-8k +3.64
DECAY(wall): decay low +38% · T60 63/8k/16k
EDT: edt sub +276% · edt low +297% · edt low-mid +56% · edt mid +69% · edt hi +66%
ATTACK: attack +40.1 ms · onset −94%
WIDTH: stereo_corr −0.08 · width low −0.13
STRUCTURAL: diffusion 7.91 · env_shape 3.91

### Ambience — QuadTank — 25
LEVEL: sub-bass +3.08 · ss deep-sub +5.87 · boom sub(500-1s) −3.70 · boom sub(1-2s) −8.05
DECAY(wall): decay sub −41% · decay low +77% · T60 63/125/500/1k/2k/4k/8k/16k (−27..+23%)
EDT: edt sub +56% · edt low +382% · edt mid −27%
ATTACK: attack +58.8 ms · onset −93%
WIDTH: width low −0.22 · width mid −0.31 · width hi −0.33 (DV OVER-decorrelated)
STRUCTURAL: cent_500 +137% · diffusion 1.57 · osc P2P −7.09

## Phase-A lever probes — RESULTS (2026-06-07)

**Both cheap hand-levers are DEAD on the FDN/QuadTank fleet. Verified empirically:**

1. **Flat Gain Trim — FALSIFIED.** VP sine1k is −7 dB (too quiet), but raising Gain Trim
   worsens n_fail monotonically: baked=26, Gain Trim=0→33, +5→40, +7→41. The fleet is
   dominated by ABSOLUTE-dB band gates (ss_*, sub-bass, boom, body — all ±2 dB); a broadband
   shift pushes more of them out than the 2 sine1k gates it fixes. Volume-match must be
   PER-BAND (Phase-B output-makeup), not flat. (Overrides [[feedback_volume_match_first]] for
   this gate set — flat match is counterproductive here.)
2. **Global Width — DEAD.** Every move trades 1:1 or worsens: Ambience baked=25, Width=1.0→24
   (−1 only), <1.0→26. VH baked=17, wider(1.2)→19, narrower→20. BR baked=23, any move→25/26.
   Tiled any move→23. stereo_corr doesn't respond cleanly because the dense diffuse field
   dominates broadband corr; needs per-band width TILT, which the engine lacks.
   [[duskverb_quadtank_coupling_wall]]

**Conclusion: the shipped FDN presets sit at a local optimum for the available COUPLED params.**
Cold Optuna (24 dims, no warm-start) reached only n_fail 46 on VP after 60 trials — far worse
than baked 26 — confirming a cold re-sweep is useless and warm-start is mandatory. The VP
FactoryPresets comment itself records prior warm sweeps "can't beat" its floor. Single-param
hand-tuning cannot move these; movement needs JOINT warm-start Optuna (marginal, ~1-3 gates
where new stricter gates left margin) or the Phase-B engine decouple (the real lever).

## Honest Phase-A ceiling
Structural floor per preset ≈ diffusion(1, ALL presets) + attack-remainder(1-2) + edt-sub-deaf
(1) + env_p2p/env_shape(1) + pitch-chorus(0-1) ≈ **4-6 locked gates**. The T60-band core
(4-8 gates on VP/BH/Cath/Ambience) is the coupling wall: 9 octave gates (±5%) vs 5 damping
bands — Phase-B output-makeup frees g to chase T60 but cannot close all 9. Realistic best-case
landing after Phase A+B ≈ low-to-mid teens for the worst presets, NOT 0/19, absent a deeper
decay-decouple (parallel multiband FDN).

## Blocker for warm-start Optuna
No reliable per-preset seed extractor exists. FactoryPresets rows are positional C++ with
scattered fields (Mid/highX/sat in a separate tail line); the render harness only echoes
params passed via --param. Prior seed JSONs in tuner_runs/ are stale (pre-rebake, older gate
set). Reliable warm-start needs either a positional-struct→JSON mapper or a harness
`--dump-params` feature first.

## Honest ceiling estimate (per preset, after Phase A+B levers, before engine decay-decouple)
- Locked floor ≈ diffusion(1) + attack-remainder(1-2) + edt-sub-deaf(1) + pitch-chorus(0-1) +
  env_p2p/env_shape(1) ≈ **4-6 structural per preset**.
- The T60-band core (4-8 gates on VP/BH/Cath/Ambience) is the coupling wall — Phase-B
  output-makeup frees g to chase T60 but 9 octave gates > 5 damping bands caps how many close.

## Phase B result (2026-06-07) — output-makeup decouple FALSIFIED on FDN

Discovery: the engine ALREADY ships the post-loop level/decay decouple as
`PostTankBandTrim` (DspUtils.h:595, DuskVerbEngine.cpp:919) — 4 regions @ 200/800/3000 Hz,
bit-null at 0 dB, engine-agnostic. No new DSP written. Exposed its 4 "Post Band * Gain"
params ([-8,+8] dB) to Optuna FREE_PARAMS + built `duskverb_render --dump-params` seed
extractor for warm-start.

Warm-start Optuna (full_check --json objective), VP + Cathedral:
- 120 trials each: both best = trial 0 (baked, n_fail 26).
- 400 trials each (+ targeted multi-seeds): **0 of 785 trials beat n_fail 26.**

Conclusion: level-decouple is NOT the bottleneck. VP/Cathedral fails are decay-coupling
(7-8 per-octave T60 gates; 9 octaves > 5 damping bands) + structural (diffusion_flux ALL
presets, env_p2p/env_shape, pitch-chorus, ~26 ms attack floor). A LEVEL trim cannot move
decay or modal-density gates. n_fail 26 is the honest floor under all available levers.
Lower requires parallel-multiband decay decouple + ER/onset fix (engine re-architecture,
declined per Option-1 honest-floor stance). No FactoryPresets edited.

### Engine-ceiling exemptions (Option-1 honest floor)
- `diffusion_flux` — FDN dense-Gaussian vs VVV sparse-modal echo density. Fails all 10.
- per-octave `T60` (9 bands) — feedback-gain coupling, 9 gates > 5 damping bands.
- `attack_time`/`onset_slope` residual — ~26 ms shortest-line floor + no rising-ER on FDN.
- `env_p2p`/`env_shape_L1`, `tail pitch-chorus` — modulation-topology / modal character.

## Vocal Hall co-tune (Option A) — 2026-06-07

Harness patched (2 blindspots fixed): sine1k double-count removed; per-band EDT
window now band-scaled + skipped <250 Hz (edt low swung 90x across 5-40ms windows =
pure window-noise). VH raw 17 -> patched-baseline 15 (3 ghosts + 1 dup removed; the
band-scaled window surfaced a real decay low_mid).

Decouple co-tune (multipliers chase decay via --param; baked PostTankEQ holds level):
  Row:   Treble 0.78->1.091, Bass 1.42->1.3042, Mid 0.82->0.76, Width 1.050->0.995
  Map:   kFiveBandByName "Vocal Hall" Sub 1.615 / Hi-Mid 0.577 (xSub 120, xAir 8000)
  pteq:  "Vocal Hall" 70Hz -2.5->-4.5, 1k -0.3->+1.5, 8k -2.5->+1.0 (level comp)
Result: n_fail 15 -> 10 (raw 17 -> 10). Closed T60 63/4k/8k/16k + ss-deep-sub/ss-hi/sine1k.
Multiplier-only (no pteq comp) floored at 16 — proves the level/decay decouple is load-bearing.

Residual 10, classified:
- STRUCTURAL (3): diffusion_flux (FDN dense-Gaussian echo density), env_shape_L1
  (envelope contour, perturbed by retune), edt hi +78% (coupled to the Treble lift that
  fixes T60 16k — can't have long-16k decay without long hi early-decay in single-tank FDN).
- COUPLED TRADES (7): stereo_corr vs width-bands (mutually exclusive on Width);
  T60 500/1k -9/-11% vs T60 4k (one Mid band spans them); decay/edt low_mid 250-500;
  boom-sub +4.2 vs T60 63 (sub decay length vs late level); low 100-250 -2.1.
Honest floor this lever set ≈ 10. Further needs finer levers (EDT AttackRamp per-band,
in-loop peak, more pteq bands) — diminishing returns.

## Vocal Plate co-tune (Option A) — 2026-06-07 — PARTIAL (clean win only)

raw 26 -> patched-harness 22 -> **Lo Cut 81.5->30 = 20** (CLEAN, baked). The 81.5 Hz HPF
starved 20-100 Hz (ss-deep-sub -11, sub-bass -8); opening it restores the anchor low-end.

Deeper co-tune to n_fail 17 was REJECTED as GAMED: lowering HF mults (Treble 0.554) to fix
the long HF T60 (16k +48%) darkened the plate badly — cent_500 -46.8% (anchor 3569 Hz, DV
1898 Hz), audibly too dark. The pteq brightness/level comp that worked for Vocal Hall did
NOT cleanly compensate for VP (8k boost barely moved cent; 1k +7 didn't lift sine1k -7.6 —
the post-tank comp behaves inconsistently for this preset). Not baked.

Why VP != VH: VP is structural-bound, not coupling-bound. Dominant residue is irreducible:
- env_p2p -55 dB (DV flat envelope +5 vs Lex modal +61 — plate sparse-modal character)
- diffusion_flux 9.6 (FDN dense-Gaussian)
- tail pitch-chorus 0.26x (DV tail too pitch-STABLE; Lex plate wanders 35 Hz)
- attack +28 ms / onset (FDN ~26 ms shortest-line floor)
Plus a real HF-brightness vs HF-decay-time coupling the pteq decouple couldn't break without
darkening. Honest clean floor this session = 20. Single-tank FDN matches VH well but is a
poor fit for a sparse-modal bright PLATE (the prior baked comment flagged this too).
