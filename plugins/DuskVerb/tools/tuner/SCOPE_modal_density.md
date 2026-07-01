# SCOPE — Modal-density fix (sine1k honk + boing + diffusion_flux)

2026-06-28. Origin: fleet audit on merged main. `sine1k_full_rms` (14/18),
`tail_resonance`/boing (8), `diffusion_flux` (6) are ONE defect — modal sparsity.
A reverb mode parks near a probe frequency (Cathedral 499×2≈1k, Small Drum 963 Hz),
so a sustained tone resonates and the tail rings. Broadband bands MATCH the anchor
(mid-1k, T60-1k pass), so it is invisible to level/decay gates and largely masked
on real material. See memory `duskverb_sine1k_honk_is_modal_sparsity`.

## Not one fix — three buckets (different physics)

| bucket | presets | modal fails | tank-density verdict |
|---|---|---|---|
| Long halls | Cathedral, Large Chamber, Blade Runner, Vocal Hall (DenseHall 8-line) + Ambience (AccurateHall 16) | ~8 | tractable — long tail sustains density |
| Short rooms | Small Drum, Medium Drum, Live Room (Dattorro) | ~5 | PROVEN DEAD END (reverted 2026-06-17) |
| Plates | Drum Plate, Vintage Gold (Dattorro) | ~2 | already density 1.0; near floor, skip |

The short-room density-fill (#87: `setDensityRoomFill` + 12-AP cascade +
`setMainLineDetune`) was built, tuned, then REVERTED — post-mortem in
PluginProcessor.cpp kDattorroDensityByName (~line 2794): density-fill kills the
boing (Small 10→0 dB) but the 12 hall APs stretch the loop to ~200 ms → short-room
T60 collapses/runs-away non-monotonically. "Rooms need ENGINE MIGRATION (FDN
16-line / velvet, dense modes w/o a runaway loop) or accept the boing."

## Engine state (important — the dense engines were retired)
- **AccurateHall32** (32-line FDN, algo 12): REMOVED 2026-06-13 — case falls back to
  16-line AccurateHall (DuskVerbEngine.cpp:1445). Re-enabling = re-instantiate
  `FDNReverbT<true,32>` + wire the case.
- **VelvetTail** (`VelvetTail.h`, loopless velvet-noise FIR tail): exists but has NO
  routing case in DuskVerbEngine. Unwired. The source-prescribed short-room target
  needs wiring first.
- Densest LIVE engine = 16-line AccurateHall (algo 10) / its SparseField(11) +
  TiledRoom(13) composites (16-line dense tail + front-loaded sparse ER).

## De-risk renders (force preset onto 16-line FDN, gain-matched full_check)
T60 off (dormant octave table) — read the MODAL gates only.

| probe | sine1k | boing | flux | read |
|---|---|---|---|---|
| Cathedral DenseHall(8) baseline | **+9.53 ✗** | pass | pass | parked 1k mode |
| Cathedral → AccurateHall(16) | **−2.32** (≈pass) | pass | pass | **mode GONE** — density direction validated |
| Small Drum Dattorro baseline | +8.65 ✗ | **+9.8 ✗ @963** | pass | sparse room mode |
| Small Drum → AccurateHall(16) | +4.73 ✗ (halved) | **pass** | 1.65 ✗ | boing gone, but FDN wash → n_fail 25→31 |

16-line FDN dissolves the hall modal triad cleanly; on a short room it kills the
boing but trades it for FDN back-load wash + T60 trouble (the documented loop
problem).

## Workstream A — HALLS (SUPERSEDED / REVERTED — do NOT follow)
> **Outdated.** The Cathedral pilot below disproved this plan: migrating halls to the
> FDN-based SparseField composite (algo 11) only RELOCATES the sparsity (honk → HF
> metal) and was reverted. The authoritative guidance is **"PILOT RESULTS"** and
> **"CORRECTED recommendation — densify DenseHall's LOW-MID"** at the end of this doc.
> The text below is kept only for the historical rationale.

_The retired plan (kept only for rationale — it was NOT carried out):_ it would have
migrated the 4 DenseHall halls to the **SparseField composite (algo 11)** = 16-line
dense AccurateHall tail (kills modes, de-risk-proven) + front-loaded sparse ER
(to keep the front-load DenseHall+BuildupDiffuser provides). Plain algo 10 would have
lost the front-load (energy_t50/onset regressing), so it specified 11.

The per-preset steps would have been (Cathedral, Large Chamber, Blade Runner, Vocal Hall):
1. Switch FactoryPresets algo 14 → 11; set the SparseField ER tap list + tail/ER
   gain from the existing DenseHall ER config.
2. Add an octave-T60 entry to `kAccurateHallT60ByName` and run
   `calibrate_octave_t60.py` (LIVE tooling — Vocal Plate/Ambience/Tiled already use it).
3. Re-check width/level/front-load; sweep residuals.

Projected at the time: ~7 modal fails closed + the AccurateHall octave-GEQ decoupling
T60; ~½–1 day per preset × 4; risk that SparseField ER couldn't reproduce each hall's
early field (cent/bright-early profile differs). **This was disproven by the pilot and
reverted — see "PILOT RESULTS" and "CORRECTED recommendation" below.**

## Workstream B — SHORT ROOMS (defer; low confidence)
Source-prescribed loopless engine (VelvetTail) is unwired; 16-line FDN only
half-fixes and adds wash. Options, both expensive:
- B1: wire VelvetTail into DuskVerbEngine (new routing case + config) and re-tune
  Small/Medium Drum + Live Room from scratch (multi-day each; new engine = new gate
  profile — Reverse Taps on velvet sits at 26).
- B2: accept the room boing (it is partly masked on real broadband material).

Recommend B2 for now; revisit B1 only after A lands and if the rooms still rank.

## Honest ceiling
Realistic recovery ≈ 7–12 fails, NOT all 28. New engines carry new gate profiles;
short rooms may net little. (NOTE: the original "biggest win = 4 hall migrations to
SparseField(11)" estimate is SUPERSEDED — the pilot below showed it trades honk for
HF metal and was reverted; see the CORRECTED recommendation.)

## PILOT RESULTS (2026-06-28, Cathedral) — Workstream A revised

Ran the full Cathedral algo 14→11 migration + octave recal end-to-end. Outcome
**disproves the "migrate halls to FDN" recommendation above:**

| stage | n_fail | note |
|---|---|---|
| DenseHall baseline (ship) | 19 | mid honk +9.5, boing, smooth top |
| algo 11 raw (seeded octave) | 17 | **modal triad GONE** (no sine1k/boing/flux) |
| + octave recal (9/9) | 14 | T60/decay/cent_500 fixed |
| + OutputDiffusion 0.4 (env) | 13 | ss air closed |

Density DOES kill the low-mid honk/boing (−6, the audible mid defect). BUT a NEW
fail appears: **HF-tail texture +10** (metallic 2-12k tail). OutputDiffusion (+0.3)
and modulation (Mod 0.8) are INERT on it. Forcing 32 lines (`FDNReverbT<true,32>`)
only reaches +7.1 — still fails (gate +2). Even 32 lines ≠ VVV smoothness (12.5).

**Conclusion: FDN line-count RELOCATES sparsity, doesn't eliminate it.** 8-line
DenseHall = sparse low-mid (honk). 16/32-line FDN = dense low-mid but sparse HF
(metal — FDN lacks DenseHall's in-loop allpass diffusion, which already gives
kurtosis 7.2). The engines have inverse profiles. Migrating halls to FDN trades
honk for metal — net gate win, net sonic wash. **AccurateHall32 re-enable NOT worth
it** (doesn't reach smoothness). Pilot reverted.

## CORRECTED recommendation — densify DenseHall's LOW-MID
Keep DenseHall (its HF diffusion is the asset). Add low-mid mode density:
- **A1′ — extend DenseHall 8→16 lines**: Hadamard 8×8→16×16 in DenseHallReverb.h +
  rebuild the line-length array (closer-spaced ratios) + per-preset octave re-tune.
  Real DSP surgery. Keeps the HF diffusion → no metal.
- **A2′ — longer in-loop density allpasses**: add ~30-100ms in-loop APs (Dattorro-#87
  style) to densify low-mid modes. The LONG hall tails sustain the loop length (unlike
  the short rooms that floored — `duskverb_boing_sparse_modal`). Cheaper than A1′; test
  on Cathedral first (sine1k/boing the read).

Either is a focused DenseHall engine task. A2′ is the cheaper spike — try it first.
