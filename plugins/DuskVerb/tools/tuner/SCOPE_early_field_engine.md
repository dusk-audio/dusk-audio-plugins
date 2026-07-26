# SCOPE — Early-Field Engine (EFE)

Status: proposed. Closes the last structural cluster after the Phase-1/3 damping +
match-EQ work. n_fail is distance-to-anchor, not quality — but this cluster is also
the audible "distant / washy / wrong-attack" character, so it's worth doing.

## The problem (measured 2026-06-15, fleet)
Three early-field defect classes, all of which preset tuning + the output match-EQ
CANNOT touch (they live in the first ~150 ms, set by engine architecture):

1. **diffusion_flux (kurt L1) — fails on ALL presets** (Δ 3.4 → 78.8). The early
   energy's density/texture-vs-time doesn't match the anchor. Worst on rooms/large
   spaces (Medium Drum 78.8, Large Chamber 58.8). The single most universal fail.
2. **energy_t50 / energy_first50 — front/back-load mismatch, DIRECTION VARIES per
   preset.** Some DV too front-loaded (Cathedral first50 +15 pp, t50 −139 ms; Bright
   +11.5 pp; Large Chamber +21 pp; Medium Drum +47 pp), some too back-loaded (Vocal
   Hall −22 pp, t50 +60 ms; Vocal Plate −24 pp). No single global fix — needs a
   per-preset controllable energy-vs-time envelope.
3. **attack_time / onset_slope** — same per-preset variance (DV attack too fast or
   too slow vs anchor by 8–60 ms).
4. **cent_50 (early brightness)** — DV consistently DARK early on halls/rooms (−20 to
   −53 %): the early reflections lack HF.

## Root cause
- The **late tank's intrinsic early output fights the ER stage.** The ER generator
  (`SparseEarlyField` / `EarlyReflections`) is summed with the tank at a fixed ratio
  (`sparseERGain_`/`tankGain_`), but the tank starts contributing energy immediately
  with its OWN front/back-load and density — which the ER can't override. The
  2026-06-08 front-load campaign (er_boost/er_rise/tank_level levers) hit exactly
  this wall (memory: energy-arrival gates, "preset params CANNOT close them").
- `SparseEarlyField` density is a FIXED model (`density(tMs)=exp(-t/18)` burst) — not
  fit to the anchor's echo-density curve → diffusion_flux can't be matched.
- No **early-HF** path → cent_50 stays dark.
- No **tank-onset control** → can't give the ER ownership of the early window.

## Existing infra to build on (don't rewrite from scratch)
- `SparseEarlyField` (velvet-noise tapped ER): has `setSizeScale/OnsetPeakMs/DecayMs/
  Burst2Ms` + `density()`/`gainEnv()`. Upgrade its density model + add per-tap HF.
- `EarlyReflections`: `setSize/TimeScale/GainExponent/OnsetRiseMs/AirAbsorption*/
  Decorr`. Has HF rolloff (absorption) but no early-HF boost.
- Composite mix already wired (`sparseERGain_`/`sparseTankGain_`, `kCompositeERByName`)
  + engine-agnostic insertion proven by the match-EQ (DuskVerbEngine output stage).

## Design — Early-Field Engine (front-end stage, engine-agnostic)
A parametric ER front-end + a tank-onset control + a clean handoff, the same way the
match-EQ is a parametric back-end. Four primitives:

1. **Tank-onset delay (THE key missing lever).** Per-preset delay on the late-tank
   OUTPUT so the ER owns the first window. Implemented (Layer 3, DenseHall path) as
   `setTankOnsetMs(float)` — a single delay-ms control; there is no separate `fadeMs`
   parameter yet. This is what lets the ER define t50/first50/attack without the tank's
   intrinsic load fighting it. A future energy-preserving fade ramp (the second arg in
   the original sketch) is still TODO — without it, watch for the Cathedral
   "pumping"/hole defect we already fixed once.
2. **Density-ramp ER** (upgrade `SparseEarlyField`): tap COUNT per unit time rises
   sparse→dense over a tunable `mixingTimeMs` (the Abel-Huang echo-density curve) →
   matches diffusion_flux. Replaces the fixed `density()` model.
3. **Energy-envelope ER**: tap gains follow a target attack→peak→decay envelope →
   controls energy_t50/first50/attack, both directions. Extends the existing
   onsetPeak/decay.
4. **Early-HF shelf** per tap (brighter early taps, rolling to dark) → cent_50.

Per-preset **offline fit**: measure the anchor's (a) energy-vs-time curve, (b) echo-
density curve, (c) early-window band centroid; fit the EFE params; bake via a
name-keyed map (`kEarlyFieldByName`) like `kOutputMatchEQByName`. Default = current
behaviour (tankOnset 0, legacy density) → **bit-null** until a preset opts in.

## Phased plan (verify + bit-null each phase)
- **Phase A — tank-onset primitive.** Add `setTankOnset(delay,fade)` + energy-preserving
  ER→tank crossfade, engine-agnostic. Prove on **Cathedral** (too front-loaded) and
  **Vocal Hall** (too back-loaded): does delaying/advancing the tank move
  energy_t50/first50 into JND? This is the make-or-break lever — do it first, measure,
  STOP if the handoff can't hit the targets cleanly.
- **Phase B — density-ramp ER** (diffusion_flux, the universal fail). Tunable
  sparse→dense tap density; fit to anchor echo density.
- **Phase C — early-HF** (cent_50).
- **Phase D — per-preset offline fit + bake** (`kEarlyFieldByName` + a fit script that
  emits the per-preset params, like /tmp/matcheq_strength.py).

## Effort / risk / payoff
- **Effort: LARGE.** New RT-safe DSP component + per-preset fitting harness + crossfade.
  Biggest of the three structural tickets (vs damping/match-EQ which reused existing
  filterbanks).
- **Risk:** the ER↔tank handoff. We've already been bitten by an early-field energy
  gap (the Cathedral "ducking/pumping" — an ER spike → hole → tail swell). The
  crossfade MUST be energy-continuous. Mitigation: Phase A proves the handoff before
  any further build.
- **Payoff: the largest remaining.** Closes diffusion_flux (all 10), energy_t50/first50
  (most), cent_50 (halls/rooms) = the bulk of the residual beyond-JND fails (~3–7
  gates/preset). Opt-in per preset (same discipline as match-EQ — only bake where it
  helps; don't regress the already-good ones like Ambience).
- **Honest ceiling:** still not clones — the late-tank modal character (osc/pitch-
  chorus/intrinsic density) differs from Valhalla/Lexicon. EFE fixes the EARLY field,
  not the late-tank fingerprint.

## Verification (each phase)
- Build + bit-null `cmp` on the non-opted-in fleet (must be byte-exact).
- full_check the early-field gates (attack_time, onset_slope, energy_t50,
  energy_first50, diffusion_flux, cent_50) before/after, per opted-in preset.
- EAR-test the handoff on a drum loop (transient through the ER, no pumping).
- Never-worse on any kept preset; opt-in only where it nets a win.
