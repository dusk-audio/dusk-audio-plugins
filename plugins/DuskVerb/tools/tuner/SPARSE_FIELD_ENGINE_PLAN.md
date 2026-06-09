# DuskVerb: Sparse-Field (early-dominant) engine — build plan

## Why
The one wall left on EVERY preset (incl. the AccurateHall-migrated ones): the
early field. VVV FRONT-LOADS — 56% of energy in the first 50 ms, 50%-energy by
41 ms, bloom at 14 ms, and SPARSE discrete echoes that stay sparse out to ~200 ms
(kurtosis two-burst: 8-22 @ 0-40 ms, then ~28 @ 100-130 ms). Every DuskVerb
tank/FDN BACK-LOADS — ~14% in first 50 ms, 50%-energy at ~160 ms, smooths to a
Gaussian wash by 60 ms. Failing gates fleet-wide: `energy_t50_ms_abs`,
`energy_first50_pct`, `attack_time`, `onset_slope`, `diffusion_flux`.

Proven NOT closable by tuning (~700 Optuna trials + manual) or output-stage
levers, and the existing parallel `EarlyReflections` (24 taps, 8-80 ms,
inverse-distance) has the WRONG character + tops out at 80 ms — boosting it nets
18-25 fails (mid/HF-heavy, narrow, smoothed). See memory
duskverb_energy_arrival_gate_and_wall. The fix is a NEW early-field-dominant
topology, not tuning. User-chosen direction (2026-06-09): sparse-modal early
field + FDN tail.

## Architecture
New engine `EngineType::SparseField` (algo 11). Signal:
```
in → predelay → SparseEarlyField (0..~200 ms, FRONT-LOADED, two-burst, wide,
                                   spectrally-full, gently-rising onset)
              → [level-balanced sum] →
              → FDN/GEQ tail (REDUCED level; supplies only the very-late body)
```
The sparse field OWNS the first ~150 ms (the perceptual early field); the FDN
tail sits UNDER it and provides late decay only. This is the inseparability fix:
VVV's early field IS the body early on (not a decaying tank tail), so a dominant
sparse field front-loads WITHOUT the tank's wash.

### SparseEarlyField generator (the new DSP)
- **Velvet-noise / modal tap bank** to ~200 ms (vs ER's 80 ms). Sparse signed
  impulses at pseudo-random times, density DECREASING with time (front-loaded).
- **Two-burst density envelope**: dense cluster 0-40 ms, gap, second cluster
  ~100-130 ms → matches the anchor kurtosis trajectory (the diffusion_flux gate).
- **Spectrally full**: per-tap all-pass diffusion (NOT the LP air-rolloff that
  makes ER mid/HF-heavy) so the early field is broadband like VVV.
- **Wide / uniform image**: independent L/R tap times+signs (decorrelated, NOT
  anti-phase) → stereo_corr ≈ 0, width bands flat. Reuse the stereo-neutral idea.
- **Gently-rising onset**: tap-gain envelope ramps to a peak ~14 ms then decays
  → attack_time / onset_slope.
- **Crest ~27 dB**: sparse (not dense) keeps the peak-to-RMS high like VVV.

## Phases (verify between; ≤5 files/phase; CLAUDE.md)
### P0 — DE-RISK (measure-first, the gate that decides the whole project)
Minimal SparseEarlyField + new slot, wired as a standalone engine (NO FDN tail
yet, or tail at ~0). Render impulse, measure vs a VVV anchor:
  energy_t50_ms_abs ≤ 30 ms, energy_first50_pct within 10 pp, crest within ~3 dB,
  diffusion_flux trajectory in the right ballpark.
GATE: can the sparse field ALONE front-load + stay sparse to 200 ms? If NO →
the topology also fails; STOP and report honestly (don't sink more time).
If YES → proceed. This is the cheap kill-test before the full build.

### P1 — slot infrastructure
enum SparseField=11, getNumAlgorithms 11→12, bounds, kEngines name, render.cpp
kNumAlgorithms lockstep, member + prepare/clear/process-case wiring. Fleet
byte-identical to HEAD (new slot, others untouched).

### P2 — generator polish
Tune the tap geometry to the anchor: two-burst envelope, decorr, onset rise,
spectral fullness. Per-preset params (density, burst times, width, onset).

### P3 — FDN tail integration + balance
Feed sparse field → reduced FDN/GEQ tail. Balance early↔late so energy gates
pass AND the late T60/body still matches (reuse tank_level + the octave GEQ).
Calibrate on Vocal Hall (the original defect case).

### P4 — A/B + migrate
Gain-matched full_check vs the current engine per candidate preset; ear-A/B on
session.wav; migrate only where it beats n_fail AND sounds right. Never ship worse.

## Risks / honest scope
- Multi-session. P0 is the kill-test: if the sparse field can't front-load cleanly
  in isolation, the topology is wrong and we stop there.
- Bit-null: new slot → existing engines untouched (the AccurateHall pattern). The
  generator is a separate class, no template/recursive-loop drift risk.
- Even if energy gates close, the late-tail BALANCE (P3) may reopen T60/body — the
  same early↔late tradeoff, now with a dominant early field instead of a boosted ER.
- Realistic best case: closes energy_t50/first50/attack/onset on the migrated halls
  (the audible "distant/washy" defect) + possibly diffusion_flux. Does NOT promise
  every structural gate.
