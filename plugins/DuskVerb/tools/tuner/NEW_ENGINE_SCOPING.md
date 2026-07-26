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

---

# 2026-06-11 — GATE-DRIVEN REVERSE-ENGINEERING SCOPE (room category)

## Reframe (per user): don't R&D blind — reverse-engineer to KNOWN target values
We have the anchor's exact measured gate values. The engine is not an open search; it is a
spec: build each stage to PRODUCE the known number, verify against it, move on. The full_check
harness already prints DV-vs-anchor per gate, so every stage has a closed-loop target.

## What already ships (reuse, don't rebuild)
- T60 9-vs-5 wall → **AccurateHall** (algo 10/12): FDN + per-octave in-loop GEQ. Reuse the GEQ.
- Early-field/front-load wall → **composite ER** (algo 13): `SparseEarlyField` + tail. Reuse it.
- Remaining blocker = the **smooth dense late field**: `diffusion_flux`, `osc P2P`,
  `tail pitch-chorus` — the Hadamard FDN's fixed beating. This is the ONLY novel piece to build.

## The reverse-engineering target table (Medium Drum Room anchor = vvv-fat-snare-room)
Every value below is MEASURED from the anchor (the full_check `Lex=`/`VVV=` column). The new
engine must reproduce each; the right column is the stage + design that produces it by construction.

| Gate (anchor target) | Engine stage → design that hits it |
|---|---|
| `diffusion_flux` (anchor's kurtosis-vs-time shape; gate ≤1.5 L1) | **late-field density trajectory** — velvet tap-density envelope (A) OR allpass-mesh depth (B), tuned to the anchor's two-burst shape. THE novel core. |
| `osc P2P` +20.5 dB | late-field **modal smoothness** — independent dense taps (velvet, corr≈0) or deep allpass → low envelope ripple. Set density high enough to hit ~20.5. |
| `tail pitch-chorus` 3.67 Hz | **no intrinsic delay modulation** (velvet taps are static positions; allpass mesh unmodulated) + a slow explicit chorus tuned to 3.67. |
| per-octave `T60` (≈0.73-1.03 s, the MDR calibrated map) | **per-band decay** — parallel band branches with independent density/gain envelopes = arbitrary per-band T60 (reuse the octave-GEQ target table). |
| `decay low_mid` 0.262 s, `decay mid` 0.345 s, `edt` 0.068/0.097/0.100 | per-band envelope SHAPE (edt≠T60): convex early-decay knee per band. |
| `ss` per band (low-mid −31.98, air −50.88, sub/low) | per-band **steady-state level** = per-band tap-gain (independent of decay, since density sets energy and envelope sets decay). |
| `energy_first50` 44.8%, `t50` 60 ms, `attack` 21.9 ms, `onset` 1.148 | **composite ER front-end** (already built) — tune its density/gain to these. |
| `cent_50` 2214 Hz, `cent_500` 1346 Hz | per-band tap-gain spectral tilt (early vs late windows). |
| `stereo_corr` +0.151, `width` | **independent L/R** tap sequences scaled to the target corr (not 0, not anti-phase). |
| `boom` (−73…−76), `bloom 8-12k` −61.5, `env_shape` | late-low-band gain + HF-band envelope; falls out of the per-band level/decay handles. |

## De-risked plan — the hard target FIRST (go/no-go in days)
- **P0 — SMOOTHNESS KILL-TEST.** Prototype ONLY the bare late field (velvet or allpass-mesh),
  mono, single-band, isolated. Measure `diffusion_flux`, `osc P2P`, `tail pitch-chorus` vs the
  anchor. **GO only if it reaches (or clearly trends to) the anchor's values** — diffusion ≤~1.5,
  osc P2P ≈20.5, pitch-chorus tunable to 3.67. If a velvet/allpass tail can't beat the FDN's
  fixed beating here, the engine is falsified cheaply — STOP. This is the entire gamble.
- **P1** per-band decay branches → hit the per-octave T60 + decay/edt targets.
- **P2** weld the built composite ER → hit the early-field targets.
- **P3** independent L/R + slow chorus → hit stereo_corr + pitch-chorus.
- **P4** per-band level tilt → hit ss + cent + boom + bloom; migrate the room category
  (MDR/Ambience/79VC/Cathedral), each beats its FDN n_fail, ear-confirmed, fleet bit-null.

## Effort / risk / reward
- **P0 = days, decides everything.** P1-P4 = multi-week if P0 passes.
- **Risk concentrated in P0** (smooth dense tail to gate level is the research bit). Reverse-
  engineering helps: we tune to a KNOWN number, not search blind. Medium confidence velvet/allpass
  beats Hadamard on the 3 smoothness gates; the per-band stages (P1-P4) are well-understood once
  P0 lands.
- **Reward:** cracks the whole room/hall category, not just MDR. New `EngineType` slot; existing
  fleet bit-null by construction.
- **Decision still open:** velvet (A) needs the "is procedurally-generated noise algorithmic?"
  call; if no, the allpass-scattering mesh (B) is the unambiguously-algorithmic fallback for P0.

## Recommendation
Greenlight **P0 only** (the smoothness kill-test, days). It reverse-engineers the one uncertain
piece against its known anchor value and tells us — cheaply — whether the multi-week engine is
worth building. Everything else (ER, GEQ damping, per-band, slot integration) is already proven.

---

## RESOLUTION (2026-06-11, post-falsification)

**P0 velvet kill-test: RUN TWICE, FALSIFIED TWICE** in the real full_check harness
(diffusion_flux ~16 vs FDN 14 — worse). Root cause of the bad GO above: the prototype
metrics did not match full_check's diffusion_flux_curve (window/onset mismatch read
anchor peak kurt 32 vs the true 93), and the DuskVerb shell (Hi-Cut shelf chain)
smears discrete velvet spikes to Gaussian kurt ~3 before measurement. The scoping
table above is RETAINED for its gate→stage mapping but its GO is void.

**What actually happened instead:** the cent_500/ss/decay walls this doc targeted
turned out to be (a) the FDN-vs-dark-room mismatch — solved by migrating MDR to the
Dattorro allpass tank (algo 0), and (b) a HARNESS BUG — the gain-match step
requantized float renders to 16-bit PCM, faking cent_500 +141% (true value passed).
See scoreboard_2026-06-11.md and memory duskverb-harness-pcm16-requantization.
MDR honest n_fail 21 (Dattorro r3) + early-field r4 sweep in progress. The remaining
early-field cluster (attack/onset/first50/osc-P2P/diffusion_flux) = discrete-
reflection texture — IF a future engine attacks it, the shell-smear constraint above
still applies to any spike-based design (measure THROUGH the shell, in full_check,
from day one).

---

## MDR SPARSE-ER COMPOSITE WELD — pipe laid 2026-06-11 (execute next session)

**Why (data-grounded, not theory):** fleet scan proved diffusion_flux + pitch-chorus
are NOT universal walls — Ambience clears diffusion_flux at 1.51 and 4/5 presets pass
pitch-chorus, all through the same shell. MDR's diffusion_flux=16 is a LOCAL mismatch:
its anchor is a spiky discrete-reflection room (kurt peak ~93) vs DV's smooth Dattorro
wash. The fix is a discrete early field matched to that profile — exactly what Tiled
Room's sparse-ER front does (cut ITS diffusion_flux to 6.28). Bonus: the same discrete
early structure attacks the early-field cluster (attack/onset/first50) — same root.

**Design = mirror the Tiled Room composite (DuskVerbEngine.cpp:1039), Dattorro tail
instead of AccurateHall:**

  new EngineType::RoomComposite = 14   (slot 14 is free post multi-stage revert)
  dispatch:
    dattorro_.process   (tankIn -> tankOut, n);       // mature dense tail (no flutter)
    sparseField_.process(tankIn -> sparseOut, n);      // discrete spiky ER front
    for i: tankOut[i] = tankOut[i]*sparseTailGain_ + sparseOut[i]*sparseERGain_;

**Exact code points (all additive, fleet bit-null by new-slot construction):**
- AlgorithmConfig.h: enum RoomComposite=14; kEngines += { "Drum Room", RoomComposite };
  getNumAlgorithms 14->15; bounds index>=15.
- render.cpp:98 kNumAlgorithms 14->15 (+ comment).
- DuskVerbEngine.cpp: dispatch case (above) + add RoomComposite to the useSmoothER
  EXCLUSION (~line 1089, like TiledRoom — it makes its own early field) + clearAllBuffers
  already clears sparseField_/dattorro_. prepare already prepares both.
- PluginEditor.h getEngineAccent: add a case (else -Wswitch).
- reuse: sparseTailGain_/sparseERGain_, setSparseField{Size,OnsetMs,DecayMs,Burst2Ms},
  setSparseERGain/TailGain — all already wired for TiledRoom.

**Migration + tuning (the real work — needs the anti-beating sweep result for the tail
voicing first):**
- FactoryPresets MDR row algo 0->14; carry the baked Dattorro tail voicing (the
  anti-beating sweep winner) as the tail; add a kRoomCompositeByName voicing map
  (sparseField size/onset/decay/burst2 + er/tail gains) tuned to MDR's anchor.
- TUNE sparseField tap density/onset to MDR's kurt-93 early profile (drive diffusion_flux
  16->toward gate) + er/tail balance for attack/onset/first50. Joint sweep, honest harness.

**Honest risk (why this is scoped, not blind-built tonight):** an ER+FDN composite on MDR
was falsified at 24 EARLIER THIS SESSION — but (a) that was under the contaminated PCM16
harness (cent_500 was fake), (b) FDN tail not Dattorro (Dattorro already killed the ring +
got honest 20), (c) it didn't target diffusion_flux/kurtosis specifically. Re-justified,
but bring eyes: bake ONLY if it beats the anti-beating baseline (never-worse), ear-confirm.
