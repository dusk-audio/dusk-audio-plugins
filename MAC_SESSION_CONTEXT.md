# DuskVerb Hall Calibration — Mac Session Context

> Paste this file into a fresh Claude session on the Mac to inherit the
> full state of the DuskVerb Hall reverb calibration project. The
> companion `MAC_HANDOFF_PROMPT.md` is the kickoff message.

## Mission

Match the Lexicon PCM Native LexHall "02.Medium Halls/000.Med Hall"
preset within 19 measured metrics (JND-bounded). User mandate: **19/19
PASS, no compromises**.

## Status (handoff snapshot)

- **Champion config:** P11 8-channel Hadamard FDN (`HallReverb`,
  algo 10 "Hall (Lex)"). **10/19** PASS via DE v4 + manual nudges.
  Baked into `plugins/DuskVerb/tools/tuner_configs/hall_med_hall_vs_lex.yaml`.
- **Frontier topology:** P16 `HybridHallReverb` (algo 12 "Hall (Hybrid)") —
  parallel ER tapped delay line + P15 Ring tail + post-mix shelves.
  Currently 5–7/19 best on first Optuna pass; topology has
  architectural strengths (spectral_crest PASS) that P11 cannot achieve.
- **All scoring decoupled from VST2** as of this commit — graders
  consume `targets/lex_*.json` snapshots, no yabridge needed on Mac.

## Architecture iteration log (chronological)

| Tag | File | Topology | Best PASS | Architectural takeaway |
|-----|------|----------|-----------|------------------------|
| P11 | `HallReverb.{h,cpp}` | 8-ch Hadamard FDN per band, 3 bands LR4-split, in-loop damping | **10/19** | Champion. c80/d50/bass-treble_ratio/a_weighted/time_crest/stereo_corr/late_tail × 3 clean |
| P12 | same | 16-ch Hadamard | 8/19 | Doubled modal density hurt → tap energy bloat |
| P13 | HallSubTank P9 + P13 patches | 16-ch + Heavy Spin & Wander mod | 5/19 | Time-varying delays don't smear spectral_crest |
| P14 | HallReverb P14 InputDiffuser | 16-ch + 4-stage Schroeder + 3-stage chain APs | 4/19 | Over-diffused; ER taps masked |
| P15 | `RingReverb.{h,cpp}` | Griesinger/Carnes sequential ring | 6/19 | **spectral_crest_db PASS** (P11 wall broken!) + late_tail × 3 PASS |
| P16 | `HybridHallReverb.{h,cpp}` | Parallel ER TDL + P15 Ring + macro mix + shelves | 5–7/19 | peak_locations PASS by hardcoded tap construction; needs more calibration |

All committed as architectural infrastructure. P11 ships as Med Hall v1;
P15 + P16 remain available for ConcertHall / RandomHall / future presets.

## HybridHallReverb v1.1 (Fix B)

`plugins/DuskVerb/src/dsp/HybridHallReverb.{h,cpp}`:

```
in (L,R)
  ├─→ ER TDL: 4 taps @ [0.0, 4.0, 7.52, 9.79] ms  HARDCODED
  │     ↓ erOut × er_level (0..2, INDEPENDENT)
  │     ┐
  ↓     │
  Ring (P15) ───→ ringOut × ring_level (0..2, INDEPENDENT)
                 ┘
                 ↓
  Post-mix shelves (low + high) — c80_per_octave / band ratio
                 ↓
                out (L,R)
```

**Fix B:** earlier `macroMix` was a zero-sum equal-power crossfade. Optimizer
couldn't boost ER for c80 without suppressing Ring for late_tail.
Replaced with independent `er_level` + `ring_level` (each 0..2). No
trade-off.

**ER tap times are HARDCODED** at Lex Med Hall measured peak positions.
Mathematically guarantees `peak_locations_ms 4/4 PASS` by construction
— Optuna can't touch them.

## Optuna metric → axis mapping

For HybridHallReverb (16 axes):

| OUT metric | Axis solving it |
|---|---|
| `peak_locations_ms` | ER tap times (hardcoded) |
| `c80`, `d50` | `er_level`, `ring_level` |
| `c80_per_octave` | `low_shelf_gain/fc`, `high_shelf_gain/fc` |
| `bass_ratio`, `treble_ratio` | shelves + ring damping |
| `centroid_drift_per_band[3]` | `ring_damping_fc` (low → HF dies fast) |
| `late_tail_*` × 3 | `decay`, `ring_damping` (P15 inheritance) |
| `spectral_crest_db` | P15 Ring topology (already PASSes) |
| `box_ratio_db` | Ring + shelves |
| `a_weighted_rms_db` | `gain_trim` (hard penalty) |
| `time_domain_crest` | `er_level` + tap weights |
| `stereo_correlation` | `ring_stereo` + ER per-tap signs |
| `rt60_per_band` | `decay` |
| `edt`, `edt_500` | `er_level/ring_level` ratio |
| `decay_envelope_db` | `ring_damping_fc` + `decay` |

## Audit findings (do not repeat these mistakes)

1. **Phantom 14 ms latency wasn't pipeline.** It was `Diffusion=0.97`
   leaking from P11's iter.py LOCKED into non-Hall-(Lex) engine tests.
   Engine-wide DiffusionStage adds group delay. Fix A neutralizes
   non-Hall algorithms automatically — see `hall_iter.py` line ~140.
2. **Optuna vs iter PASS-count mismatch wasn't scoring bug.** Both
   `count_pass` implementations are identical. Discrepancy was
   different `LOCKED` dict contents. Standardized.
3. **Engine-wide diffuser_ + ER need to be OFF for Hybrid.** Hybrid
   has its own ER TDL. LOCKED sets `Diffusion=0` and `Early Ref Level=0`
   for algo 12.

## Filesystem map (post-handoff)

```
plugins/DuskVerb/
  src/dsp/
    HallReverb.{h,cpp}            ← P11 champion (algo 10)
    HallSubTank.{h,cpp}           ← P11 8-ch sub-tank
    RingReverb.{h,cpp}            ← P15 Griesinger ring (algo 11)
    HybridHallReverb.{h,cpp}      ← P16 frontier (algo 12)
    DuskVerbEngine.{h,cpp}        ← routes algorithm → engine instance
    AlgorithmConfig.h             ← EngineType enum + getAlgorithmConfig()
  tools/
    targets/
      lex_med_hall.json           ← 19 metrics for LexHall Med Hall
      lex_concert_hall.json       ← LexConcertHall
      lex_rich_plate.json         ← LexVintagePlate Rich Plate
      lex_vocal_plate.json        ← LexVintagePlate Vintage Vocal Plate
    tuner/
      target.py                   ← JSON loader, Anchor dataclass
      export_targets.py           ← one-shot batch exporter (Linux+yabridge only)
      hall_iter.py                ← strict per-metric grader, --target-file arg
      hybrid_optuna.py            ← 16-axis TPE search, --target-file arg
      metrics.py                  ← measure_pair() — unchanged
      config.py                   ← MetricWeight dataclass — unchanged
      compute_loss                ← in metrics.py — unchanged
    tuner_configs/
      hall_med_hall_vs_lex.yaml   ← P11 10/19 calibration (locked_params)
```

## Mac workflow (no VST2 needed)

```bash
# Build VST3 (no VST2 SDK required — only used for export_targets.py
# which lives on the Linux box).
cd build && cmake .. -DBUILD_DUSKVERB_RENDER=ON
cmake --build . --target DuskVerb_VST3 duskverb_render -j8

# Verify P11 baseline against JSON anchor:
python3 plugins/DuskVerb/tools/tuner/hall_iter.py \
    --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json

# Continue HybridHallReverb tuning on Mac:
python3 plugins/DuskVerb/tools/tuner/hybrid_optuna.py \
    --target-file plugins/DuskVerb/tools/targets/lex_med_hall.json \
    --trials 300 --workers 6
```

## Project conventions (durable)

- **Volume-first rule:** every parameter sweep must re-trim Gain Trim
  to bring `a_weighted_rms_db` within JND before evaluating other
  metrics. Energy-coupled metrics are invalid until volume matches.
- **100% wet renders only:** both DV (`Dry/Wet=1.0, Bus Mode=on`) and
  Lex (`Mix=100%`) must render 100% wet. Mixed renders contaminate
  every energy metric.
- **No Co-Authored-By Claude trailer in commits.**
- **PR bodies: no Test Plan checklist, no Claude attribution.**
- **No em dashes in plugin manuals.**

## Open questions for the Mac session

1. Will Optuna's 16-axis TPE on Hybrid (with Fix B independent levels)
   exceed P11's 10/19? Untested — last run was on P16 pre-Fix-B.
2. Can hardcoded ER tap weights + macro_mix wins from P16-Optuna v1
   close c80/d50 simultaneously now that ring_level can scale
   independently?
3. Is there a hybrid config that achieves 12+/19 by combining
   P11's c80/d50 strengths with P15's spectral_crest + late_tail wins?

## Active todo (resume here)

- [ ] Tune HybridHallReverb on Mac via Optuna against `targets/lex_med_hall.json`
- [ ] If Hybrid exceeds P11 → bake winner into a `hall_hybrid_v1.yaml`
      tuner config + commit as Hall (Hybrid) Med Hall v1
- [ ] If Hybrid plateaus < P11 → ship P11 as Med Hall v1, move to
      ConcertHall anchor (`targets/lex_concert_hall.json`) with Ring
      or Hybrid topology
