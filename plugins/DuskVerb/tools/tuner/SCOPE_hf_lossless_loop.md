# SCOPE — HF-lossless loop interpolation (unwall T60-16k on DenseHall + Shimmer)

**Status:** scoped 2026-06-16 (inline; the ultracode workflow 529'd, re-derived by hand).
**Goal:** make a 16 kHz tail SUSTAIN in the recirculating loop so DenseHall (Vocal
Hall / Cathedral / Bright Hall / Blade Runner / Large Chamber) and the Shimmer
(Black Hole / Deep Blue Day) stop dying dark at the top octave. Once 16 kHz can
ring, the already-built (dormant) DenseHall per-octave GEQ + the staged Shimmer
brightening become the shaping tools.

## 1. Root cause (quantified)

The fractional-delay read in a modulated delay line is `buf[i0]*(1−fr) +
buf[i0+1]*fr` — **linear interpolation**, which is a *position-dependent lowpass*:

| frac | |H| at 16 kHz (48 k) | |H| at nyquist |
|------|--------------------|----------------|
| 0.0  | 1.0 (lossless)     | 1.0            |
| 0.5  | **≈ 0.50 (−6 dB)** | **0 (−∞, null)** |

The loop is LFO-modulated, so `fr` sweeps 0..1 continuously → a wandering ~0..6 dB
notch at 16 kHz **every pass**. Over a long tail (tens of passes) it compounds →
16 kHz cannot sustain. Measured: commanding the DenseHall octave GEQ to T60-16k =
20 s realizes only ~0.7 s; the loss dominates the commanded gain. The per-octave
GEQ is **attenuation-only** (gain ≤ 0.9999) so it can never *add back* the loop
gain HF needs — confirming the fix must be in the interpolation, not the damping.

**Where the loss lives:**

- **DenseHall — LINEAR everywhere (the odd engine out).** `DenseHallReverb.h`:
  - `Line::getlast` (215), `ModAP::process` (199), `SpinComb::process` (226) — all raw linear.
  - Per pass per line: 1 Line read + 3 nested `loopAP` (ModAP) + spin ≈ **5 linear interps**, ×8 lines. Heavy.
- **Shimmer — mixed.** The FDN hall (`reverb_` = FDNReverb) ALREADY uses `cubicHermite`
  (FDNReverb.cpp:582/859/870). Only `GranularPitchShifter::readLinear`
  (ShimmerEngine.cpp) is linear — and it reads the grain that GENERATES the up-pitched
  octave, so it loses HF on the very voice meant to sustain 12–24 kHz.
- **Precedent (de-risks this):** FDN, QuadTank, Dattorro, SixAP, DiffusionStage ALL
  read via `DspUtils::cubicHermite` (4-point Catmull-Rom) already — proven bit-null-safe
  in the FDN recursive hot loop. DenseHall + the shimmer grain simply never got it.

## 2. The fix (phased — cheapest precedented step first)

`cubicHermite` is far flatter than linear near nyquist (≈ −2…−3 dB at 16 kHz vs
linear's −6…−∞) but NOT perfectly flat. So:

- **Phase 0 — linear → `cubicHermite`** on the DenseHall reads (Line/ModAP/SpinComb)
  + ShimmerEngine `readLinear`. Uses the existing, proven helper. Low risk
  (precedented), FIR (modulation-safe, no allpass transient). Likely recovers most
  of the gap (linear→Hermite is the big jump). **Do this first; it may suffice.**
- **Phase 1 (only if Hermite's residual rolloff still caps 16 k)** — **Thiran 1st-order
  allpass fractional delay** for the loop reads: `H(z)=(a+z⁻¹)/(1+a·z⁻¹)`,
  `a=(1−fr)/(1+fr)`. Allpass = **flat magnitude everywhere** = truly HF-lossless.
  Cost: 1 mul + 1 state/read. Risk: the coeff moves with the modulated `fr` → a
  transient on fast `fr` change, and it adds phase/transient energy in a recursive
  loop. Reverb mod is SLOW (5–7 Hz, small excursion) so the coeff drifts gently —
  manageable, but this is the ear-axis (metallic/ring) that needs the user's listen.
  Fallback if the allpass misbehaves: **5th-order Lagrange** (6-tap FIR, flat to
  ~0.8·nyquist, bulletproof under modulation, ~3× linear CPU).

New shared helper (Phase 1): `DspUtils::thiranAllpassRead(state&, buf, mask, i0, fr)`
or `lagrange5(buf, mask, idx, fr)` — alongside `cubicHermite`.

## 3. Bit-null / codegen strategy

- **DenseHall:** all 5 algo-14 presets are being changed → no within-engine bit-null
  to preserve. The only risk is codegen drift of OTHER engines if DenseHallReverb.h's
  changes perturb the shared `DuskVerbEngine.cpp` TU (memory:
  duskverb_bitnull_codegen_limit). Mitigant: `cubicHermite` is ALREADY called in that
  TU by the FDN — adding more calls is the same instruction class, low drift risk.
  **Prove it:** render a Dattorro preset (Drum Plate) + an FDN preset before/after the
  DenseHall edit; require byte-identical (md5). If it drifts, isolate DenseHall's read
  in its own TU (the OctaveGEQDesign.cpp precedent).
- **Shimmer:** `readLinear` is shimmer-only (algo 7); changing it affects only Black
  Hole / Deep Blue. Verify other engines byte-identical.
- **Phase 1 allpass** has per-read STATE → must reset in every clear path (mirror the
  octDamp_ reset I added). Gate behind a compile/preset switch only if a no-change
  comparison is needed; otherwise migrate the presets outright.

## 4. Make-or-break (FIRST, before any rollout)

On **Cathedral** (DenseHall) + **Black Hole** (Shimmer):

| metric | baseline | Phase-0 pass bar | Phase-1 target |
|--------|----------|------------------|----------------|
| Cathedral T60-16k | 0.42 s | **> 0.9 s** (toward anchor 1.63) | ≥ 1.5 s |
| Black Hole T60-16k | 4.3 s | **> 6 s** (toward anchor 9.6) | ≥ 8 s |
| metallic ring (diffusion_flux / tail kurtosis) | clean ~0.16 | **stays < 1.5** | stays clean |
| CPU (per-block, profiled) | — | **< +15 %** vs current | < +35 % |

PASS Phase 0 → roll out + recalibrate. FAIL (16 k still walled) → Phase 1 allpass.
FAIL both / metallic returns / CPU blows → report the wall honestly (the loop
topology, not just interpolation, limits it) — no premature ceiling, but no
gate-chasing a regression either.

## 5. Risks

- **Metallic ring** — the reason DenseHall exists (the migration killed it via dense
  diffusion). A flatter HF interpolator could let sparse HF modes ring again. Gate
  proxy = kurtosis; **final call = the user's ear** (this is the one true ear-axis).
- **CPU** — DenseHall does ~5 reads/line ×8 lines/sample. Hermite = 2× the interp
  ops of linear; allpass ≈ 1.5× + state. Profile before/after; halls run at this
  cost already on the FDN, so likely fine, but measure.
- **Pre-ring / transient smear** — Hermite (Catmull-Rom) slightly overshoots; allpass
  has phase ripple. Watch attack/onset gates + ear.
- **Shimmer voice-1 AA still caps 12 kHz** independently (nyquist/ratio, ShimmerEngine.cpp:56).
  This fork lifts the LOOP sustain + the grain HF, but the +12 voice's top octave is
  still AA-limited — the +24 voice (to 24 k) + the loop sustain carry 12–24 k. Don't
  expect voice-1 to reach 20 k; oversampling the shifter is a separate, later lever.

## 6. Phased plan

0. **Make-or-break** — swap DenseHall + shimmer-grain reads to `cubicHermite`, measure
   Cathedral + Black Hole T60-16k + kurtosis + CPU. Bit-null-verify other engines.
1. If Phase 0 short of bar → **Thiran allpass** loop reads (DenseHall Line + loopAP;
   shimmer feedback delay). Re-measure + **ear-check the metallic axis** (user).
2. **DenseHall rollout** — migrate all 5 presets; re-run `dh_calib.py` to recalibrate
   the now-USEFUL per-octave GEQ (16 k reachable → bake `kDenseHallOctaveT60ByName`).
3. **Shimmer rollout** — confirm the brightening (LPF 14 k + voice2 0.60) now sustains;
   re-tune Black Hole / Deep Blue vs the anchors; ear A/B.
4. **Fleet re-score** + commit per engine.

## 7. What this unlocks (already-built, waiting on this)

- **DenseHall per-octave GEQ** (fork #2) — built, dormant, bit-null; calibrated curves
  saved (Cathedral/Vocal Hall/Bright Hall) in memory. The moment 16 k unwalls, bake them.
- **Shimmer brightening** (LPF 14 k + voice2 0.60) — staged in the ABX, uncommitted.
  Pays off once the loop sustains the brightened HF instead of re-culling it.

**Effort:** Phase 0 = small + precedented (≈ a few edits + one build + the make-or-break).
Phase 1 = real DSP (new allpass-read helper + state plumbing + ear validation). Most
of the value may land in Phase 0; Phase 1 is the insurance for true 16 k sustain.

---

## PHASE 0 RESULT (2026-06-16) — PROVEN on DenseHall, then REVERTED pending ear-OK

Swapped DenseHall (Line/ModAP/SpinComb) + shimmer-grain reads linear → `cubicHermite`.

- **DenseHall = breakthrough.** Cathedral T60-16k **0.42 → 1.39 s** raw (octave-off),
  **→ 1.53 s** after recalibrating the octave GEQ (anchor 1.63, −5.6 %). cent_50/500
  went −27 %/−27 % (dark) → **+13 %/+3 % (matched)**; T60 4k/8k **perfect**. Kurtosis
  stayed clean (0.16 — no metallic). The linear→Hermite swap UNWALLS 16 k.
- **n_fail on Cathedral: 16 → 17** after full recalibration — NOT an HF regression: the
  residual 17 = the pre-existing EARLY-FIELD cluster (attack/onset/energy_t50 — fork #3)
  + structural sine1k + a NEW HF-LEVEL-hot (ss/bloom +2-4 dB from over-brightening,
  TUNABLE via the output Hi-shelf, projected 16→~12). The core dark+dead-HF defects are
  fixed; count is flat only because other small gates shift.
- **Shimmer barely moved** (Black Hole T60-16k 4.3 → 4.37). Its FDN hall ALREADY uses
  cubicHermite; the grain read wasn't the bottleneck. The shimmer's 16 k cap is the
  feedback LPF (14 k) + shifter AA (12 k) — a DIFFERENT lever, not this interpolation
  fix. So this fork is a DenseHall win; the shimmer needs its own HF-path work.
- **Bit-null:** Drum Plate (Dattorro, same TU) byte-stable (n_fail 21 unchanged).
- **REVERTED** because Hermite is global to ALL 5 DenseHall presets (all calibrated for
  the old lossy linear → leaving it on mis-tunes them too-bright until each is
  recalibrated), and the brightness jump is a CHARACTER change the user must ear-judge.
  Preview staged: `dusk-audio-tools/tuner_runs/listen/densehall_hffix/cathedral_NEW-hffix_*`
  vs `cathedral_anchor_*`.

**ROLLOUT recipe (on ear-OK), per DenseHall preset:** Hermite ON → `dh_calib.py`
recalibrate the octave GEQ (16 k now reachable) → bake `kDenseHallOctaveT60ByName` →
output Hi-shelf trim the HF-level-hot → re-measure. Cathedral converged curve (post-
Hermite): `4.8741,4.5315,3.8561,3.3995,3.1289,2.6094,2.1758,1.9878,5.6285`.
