# DuskVerb: ground-up new engine — scoping

## Why (what the current FDN cannot do, proven this campaign)
The Hadamard-FDN + tapped-ER architecture is walled, three independent ways tested
(Optuna ×6, manual, sparse-modal engine R&D), against the VVV anchors:
1. **diffusion_flux** — FDN early field collapses to dense Gaussian (kurtosis ~3) by ~10 ms;
   the anchor is a MODERATE two-burst sparse-modal trajectory. Unreachable: dense ER = 6.0,
   pure sparse taps = 12.8, anchor sits between with a specific SHAPE neither reproduces.
2. **energy-arrival + front-load↔body** — FDN back-loads; the late tail carries BOTH the wash
   AND the correlated-low body, inseparable. Cutting the tail front-loads but kills the body.
   VVV's early field IS the body (early-field-dominant topology).
3. **per-octave T60 (9-vs-5)** — 9 octave gates, 5 damping bands; adjacent octaves couple,
   can't be independently set (Vocal Plate floored at 6/9).

These are STRUCTURAL: the FDN is a dense, late-blooming, Gaussian generator with coarse
per-band decay. A new engine is the only way past them.

## Target spec (measured from VVV anchors this campaign)
- Front-loaded energy: ~56% in first 50 ms, 50%-energy by ~41 ms (per preset).
- Two-burst moderate-sparse early field (controllable echo-density trajectory).
- Independent per-octave T60 (match ±5% across 63 Hz–16 kHz).
- Convex early decay (edt distinct from T60).
- Uniform-neutral WIDE stereo image (L/R corr ≈ 0 across bands, no anti-phase).
- Smooth slow modulation (~0.8 Hz chorus), spectrally full/natural.

## Candidate topologies

### A. Filtered/modulated VELVET-NOISE reverb  ← RECOMMENDED
Late field = sparse pseudo-random ±1 impulse sequence (velvet noise, ~1-2k imp/sec) per band,
each band gain/density-enveloped for its own decay; summed. Early field = the front-weighted
density region of the same sequence (no separate stage). Stereo = independent L/R sequences.
Modulation = slow time-variation of tap positions/filter (chorus).
- Closes diffusion_flux: density trajectory is DIRECTLY tunable → match the two-burst shape.
- Closes energy-arrival + body: density envelope IS the energy envelope; front-load = front-
  weight density; the early field carries the body (no late-tail dependency).
- Closes T60 9-vs-5: parallel per-band velvet branches → arbitrary independent per-band decay.
- Wide neutral image: independent L/R velvet → corr ≈ 0 by construction.
- CPU: sparse → only nonzero taps cost; the "interleaved velvet" / fast-convolution-free
  recursive forms are real-time cheap.
- CAVEAT (decision point): velvet noise is procedurally GENERATED + time-varying (algorithmic
  synthesis, NOT loading external IRs) — but it is "render a sparse response" flavored. Must
  confirm this counts as "algorithmic" per the product's no-convolution positioning. If the
  modulated/recursive velvet form is used (time-varying, parameter-driven), it is defensibly
  algorithmic, not a sampled IR.

### B. Scattering Delay Network (SDN)
Delay nodes at virtual wall positions + a scattering matrix; first-order reflections exact,
higher orders build to diffuse. Per-surface absorption filters = per-band decay. Physically
grounded → natural early field + body by construction; modulatable.
- Closes the same walls (early-field-dominant + per-band absorption).
- More complex to implement + tune (geometry → reflection times); heavier CPU than velvet.
- Strong "real room" character; less of a free knob-space than velvet.

### C. Jot FDN + in-loop GEQ + designed early-reflection front-end (EVOLUTION, not ground-up)
Upgrade the existing FDN: replace the 5-band damping with a proper per-band graphic-EQ in the
feedback loop (Jot/Schlecht accurate-RT control → closes T60 9-vs-5 cleanly) + a real
front-loaded early-reflection generator feeding it.
- Cheapest path; closes T60 + improves front-load. Reuses FDN strengths.
- Does NOT fully close diffusion_flux (still FDN-dense late) — partial.
- Good "fast win" if T60 is the priority; not a full VVV match.

## Recommendation
**Primary: A (velvet-noise multiband), pending the algorithmic/convolution decision.** It is the
only candidate that natively addresses ALL measured failures, has the freest tuning space (match
each anchor's density+decay trajectory directly), and is CPU-cheap. **Fallback if velvet is
ruled "too convolution-like": C now (FDN+GEQ, closes T60 fleet-wide cheaply) + B later (SDN) for
the early-field/diffusion character.**

## Integration (low-risk — the slot system already supports it)
DuskVerb has a 10-algorithm `EngineType` enum + slot dispatch (DuskVerbEngine::setAlgorithm /
the process() switch). A new engine is a NEW EngineType + a new DSP class, dispatched in the
switch — it does NOT touch the existing 10 (fleet bit-null trivially preserved; new presets opt
into the new algo index). Same prepare/clear/process contract. Per-preset config via the
existing applyEngineConfig map pattern. The full_check gate harness + anchors already exist to
measure it.

## Phased build + effort/risk
- **P1 — core velvet late reverb (mono, single-band):** generate velvet sequence, decay
  envelope, real-time sparse render. Gate: stable, decays, RT60 controllable. ~few days.
- **P2 — multiband per-octave decay:** parallel band branches → independent T60. Gate: T60 9/9
  on one anchor. ~few days.
- **P3 — early-field density shaping:** front-weight + two-burst density → diffusion_flux +
  energy-arrival. Gate: diffusion ≤1.5 + energy gates on one anchor. THE uncertain core.
- **P4 — stereo + modulation:** independent L/R + slow chorus. Gate: width + tail-mod.
- **P5 — per-preset tune the fleet + ship.** Gate: beat each FDN preset's n_fail, ear-confirm.
- **Effort:** multi-week (5 phases, real DSP). **Risk:** P3 (density→diffusion trajectory match)
  is the same gate that walled the FDN — velvet makes it TUNABLE (vs the FDN's fixed dense
  output), but landing ≤1.5 on a 28-window trajectory is still real work; medium-high confidence
  it gets MUCH closer than the FDN's 5.4, lower confidence it nails ≤1.5 on every preset.
- **Reward:** a true VVV-class hall/plate engine — front-load + sparse + per-band T60 + image,
  the things the FDN structurally cannot do. New algo slot; entire existing fleet untouched.

## Open decisions for the user
1. Is procedurally-generated, modulated VELVET NOISE acceptable as "algorithmic" (vs the
   no-convolution positioning)? → picks A vs (C+B).
2. Scope: full ground-up (A, multi-week) vs the cheaper T60-focused FDN+GEQ evolution (C) first?
3. Target: match VVV specifically, or a general VVV-class hall? (affects how hard P3 is pushed.)
