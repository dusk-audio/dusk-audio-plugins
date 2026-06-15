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
| Black Hole | ~~29~~ **25** | +7.3 | pass | 1.58 |  ← was vs BROKEN anchor; re-captured (wetDry->1.0)
| Blade Runner 224 | 31 | pass | +3.3 | 6.95 |
| Large Chamber | 35 | pass | pass | pass |
| Live Room | 35 | **+20.0** | +2.2 | pass |
| Vintage Vocal Plate | 36 | pass | +2.6 | pass |
| Deep Blue Day | ~~40~~ **49** | +7.7 | pass | pass |  ← was vs BROKEN anchor; re-captured (feedback 0.81)
| Medium Drum Room | 45 | +10.9 | +3.8 | pass |

**18 presets, mean n_fail = 26.9.**

## Findings
- **The new gates were the real deliverable.** Prior scoreboards were partly
  fiction — the boing + transient-loudness were invisible. True fleet state is
  much higher than the old teens.
- **Boing (tail resonance) fails on 8/18** — and ACROSS ENGINES, not just
  Dattorro: Live Room +20 (worst), Medium Drum +10.9, Ambience +9.9, Small Drum
  +9.7, Deep Blue +7.5, Black Hole +7.3, Reverse Taps +11.4, Gold +4.3. A
  prominent pitched tail mode = modal sparsity / under-diffusion fleet-wide.
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

## Next (the fleet campaign)
1. Re-validate suspect anchors (valhalla-shimmer pair) like the lex-vintage
   plates were.
2. The boing is the dominant fleet defect — needs the modal-density fix (Size/
   density on tank presets; or the dense-engine path) applied fleet-wide.
3. impulse_rms — per-preset transient-loudness trim.
