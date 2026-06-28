# DuskVerb fleet scoreboard — 2026-06-15 (new perceptual gates)

Gain-matched (render scaled to anchor noiseburst RMS) full_check, RAW (no
`--category` ceiling exemptions), against the **new gates**: `tail_resonance`
(boing — dominant 200-2k tail-mode prominence vs anchor, gate +3dB),
`impulse_rms` (hit loudness, gate ±2dB), + the FIXED `diffusion_flux` (floor-
guard + one-directional). 18 anchored presets (Surf Spring + 1981 Gated Snare
have no anchor). Δ shown only where the gate FAILS.

| preset | n_fail | boing | impRMS | df |
|---|---|---|---|---|
| Tiled Room | 15 | pass | pass | 4.79 |
| Vintage Gold Plate | 16 | +4.3 | +2.3 | pass |
| 79 Vocal Chamber | 16 | pass | pass | pass |
| Ambience | 17 | +9.9 | pass | 5.43 |
| Vocal Plate | 20 | pass | pass | pass |
| Drum Plate | 20 | **pass** | +4.0 | pass |
| Bright Hall | 22 | pass | pass | pass |
| Reverse Taps | ~~22~~ **46** | ~~+11.4~~ pass | pass | pass |  ← was vs BROKEN anchor (dup); re-captured
| Vocal Hall | 28 | pass | +? | pass |
| Cathedral Large Hall | 28 | pass | +2.8 | pass |
| Small Drum Room | 29 | +9.7 | +3.2 | pass |
| Black Hole | ~~29~~~~25~~ **24** | +7.2 @238 | pass | 1.57 |  ← anchor re-captured at NATIVE mix=0.5 (wetDry=1.0 was wrong); DV mix 0.5
| Blade Runner 224 | 31 | pass | +3.3 | 6.95 |
| Large Chamber | 35 | pass | pass | pass |
| Live Room | 35 | **+20.0** | +2.2 | pass |
| Vintage Vocal Plate | 36 | pass | +2.6 | pass |
| Deep Blue Day | ~~40~~~~49~~ **37** | +7.6 @203 | pass | pass |  ← anchor re-captured at NATIVE mix=0.5 (wetDry=1.0 inflated to 49); DV mix 0.38→0.50. cent_50 now PASSES
| Medium Drum Room | 45 | +10.9 | +3.8 | pass |

**18 presets, mean n_fail = 27.8.** (500 total fails / 18 — updated after the
Reverse Taps re-capture lifted it 22→46.)

## Findings
- **The new gates were the real deliverable.** Prior scoreboards were partly
  fiction — the boing + transient-loudness were invisible. True fleet state is
  much higher than the old teens.
- **Boing (tail resonance) fails on 7/18** — and ACROSS ENGINES, not just
  Dattorro: Live Room +20 (worst), Medium Drum +10.9, Ambience +9.9, Small Drum
  +9.7, Deep Blue +7.5, Black Hole +7.3, Gold +4.3. (Reverse Taps no longer
  fails — its +11.4 was vs the broken duplicate anchor; passes on the re-capture.)
  A prominent pitched tail mode = modal sparsity / under-diffusion fleet-wide.
- **impulse_rms (hit loudness) fails on ~10** — presets matched on sustained RMS
  but are hot/cold on the transient (±2-4dB). The sustained-only gates missed it.
- **Drum Plate boing now PASSES** (Size-up fix held) — the one fixed so far.
- Caveats: render non-determinism ±1-2; a few anchors warrant the broken-anchor
  re-validation (esp. valhalla-shimmer Black Hole / Deep Blue — flagged earlier
  as possible duplicates) before trusting their numbers.

## Anchor re-validation (2026-06-15, whole fleet)
md5-collision + audio-metric sweep over ALL anchor dirs. Broken anchors found —
all the same bug (fxp/preset never applied at capture → plugin DEFAULT → distinct
presets byte-identical, which INFLATES the score):
- gold/vocal/rich plate (f716f04), lex-reverse-1 (71e09e7, ==random_large_rhall1),
  valhalla-shimmer black-hole==deep-blue-day + 50% dry (ca0969e, re-rendered via
  ValhallaShimmer --nparam + wetDry->1.0).
- Effect on scores (broken anchors MASK the true, worse state): Reverse 22->46,
  Deep Blue 40->49, Black Hole 29->25. Updated above.
- FALSE alarms (wet, just short-decay): Live Room + vvv vocal-plate/ambience/
  tiled/84-room. dryspike=1.0 on the shimmer pads is the instant-onset-pad
  character, not contamination.
- TODO: floor-guard tail_resonance + diffusion_flux on short anchors.

## Shimmer mix + octave correction (2026-06-15)
- **The shimmer anchors were captured at the WRONG mix.** All Valhalla Shimmer
  factory presets ship `wetDry=0.5` (verified from the plugin UI + the .vstpreset
  `parameter0=0.5`). The ca0969e re-capture forced `wetDry=1.0` (mistook the 50%
  default for dry-contamination). Re-rendered both anchors at native 0.5 via
  ValhallaShimmer --nparam (full param replay). DV presets set to match: Deep Blue
  Day mix 0.38→0.50, Black Hole was already 0.50.
- **Effect:** Deep Blue Day 49→**37** (the fully-wet anchor was inflating fails;
  the 50% dry lifts cent_50 into PASS). Black Hole 25→**24** (~same).
- **Octave generation & the boing gate:** the +12 shimmer octave lands at
  **3.7–5.1 kHz** (Black Hole tail peaks 80/100/200 + 3780/5160 Hz). The
  `tail_resonance` (boing) gate measures **200–2000 Hz** — so it does NOT touch the
  octave (neither false-fires on it nor verifies it). The +7 boing on both is a
  REAL low ring (BH 238 Hz, DBD 203 Hz) at a different freq than the anchor's
  modal content — a genuine defect, not the octave. The octave's HF fidelity IS
  gated (cent_500 / T60-4k/8k/16k) but those are the documented 12 kHz shifter-AA
  structural ceiling (centroid sticks ~3.5k vs anchor 6.5k, T60-16k 2.5s vs 9.6s).
  No NEW gate needed for the octave — the boing reading is correct as-is.

## Next (the fleet campaign)
1. Re-validate suspect anchors (valhalla-shimmer pair) like the lex-vintage
   plates were.
2. The boing is the dominant fleet defect — needs the modal-density fix (Size/
   density on tank presets; or the dense-engine path) applied fleet-wide.
3. impulse_rms — per-preset transient-loudness trim.
