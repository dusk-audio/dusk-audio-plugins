# DuskVerb anchored-preset scoreboard — 2026-06-11

Gain-matched full_check via `parallel_check.py` (6-worker), 100%-wet renders,
**sustained-correct harness** (`--sustained-pink-seconds 4.0`, so the ripple /
mod-freq / ss-energy / per-band edt+decay families actually run on the sustained
render instead of falling back to the noiseburst floor).

> ⚠️ 2026-06-11 (late): HARNESS REQUANTIZATION BUG fixed — the gain-match step
> (`sf.write` without subtype) silently converted float renders to 16-bit PCM,
> injecting a dither floor that dominated every quiet late window (MDR cent_500
> read +141% vs a TRUE +2%; osc P2P artificially passed). Anchors were only
> copied → stayed clean → DV alone was penalized. All boards before this note
> scored against that floor. parallel_check.py now writes subtype='FLOAT'.

> ⚠️ These numbers are HIGHER than `scoreboard_2026-06-10.md`. That board predates
> the sustained-gate fix — its sustained family was silently skipped, so every
> n_fail there was optimistic (it's what made Bright Hall read "9" when the honest
> number was 16). This board is the truth. The increases below are measurement
> honesty, NOT regressions — the only real engine change since is Bright Hall
> (32-line migration, a genuine 16→13 improvement + the metallic-tail fix).

| Rank | Preset | Engine | n_fail | prev board (stale) | Notes |
|---:|---|---|---:|---:|---|
| 4 | Ambience | Hall | 16 | 20 | **tunable-cluster sweep 23→16**: bass-damp+bandtrim (boom/640), partial front-load (er_boost 3.1+tank 0.63 CLOSED attack/onset/t50/first50), edt. Residual: boom-1-2s long tail (octave-T60), sharp 640 notch, anti-phase stereo |
| 3 | Blade Runner 224 | Hall | 17 | 11 | **tunable-cluster sweep 20→17**: ER ON (was OFF → fixed 107ms attack) + decay 13.6→11.1 (tail_t60) + damping/diffusion/spectral. Residual edt+254% is octave-T60-locked (GEQ recal = next pass) |
| 5 | Medium Drum Room | Plate | 18 | 17 | **fake 25 → honest 18**: Dattorro re-engine (ring) + r3/r4 tank+early-field + anti-beating sweep (pitch-chorus 8.2x→3.65x via modal geometry). Residual = diffusion_flux (spiky-anchor mismatch, → sparse-ER composite, scoped) + first50 front-load + ss-band splits |
| 7 | Cathedral Large Hall | Hall | 15 | 13 | **early-field+spectral sweep 19→15**: Pre-Delay 20.88→2.48ms + strong-fast ER (boost 4.96/rise 1.53) cracked the 147→2.3ms attack; bandtrim ss-mids cut. Residual edt+498% octave-T60-locked (GEQ recal next) |
| 5 | 79 Vocal Chamber | Room | 18 | 10 | QuadTank gain==decay==level coupling wall |
| 6 | Vocal Hall | Hall | 16 | 16 | early-field + boom/body |
| 6 | Drum Plate | Hall | 16 | 16 | de-honked; boom/env_shape |
| 6 | Tiled Room | Tiled Room | 16 | 17 | **composite ER+dark-tail engine (algo 13): 26→16** |
| 9 | Vocal Plate | Hall | 13 | 13 | 1 kHz pteq is poison; clean floor |
| 9 | Bright Hall | Concert Hall | 13 | 16 | **32-line dense FDN: 16→13 + tail kurtosis ~18→14.9 (metal)** |

Engine names are the standardized 2026-06-11 labels (Hall = AccurateHall algo 10;
Concert Hall = AccurateHall32 algo 12; Room = QuadTank algo 3; Plate = Dattorro
algo 0; Tiled Room = composite algo 13).

### MDR 2026-06-11 campaign (25 → 20, then the floor)
Falsified IN ORDER, each measured in the real harness: 3-band MultibandFDN (29),
AccurateHall32 32-line (32 — the 12.9k ring is input-HF recirculation, line count
can't fix a dark room), Dattorro re-engine (**20, BAKED** — allpass tank damps HF
out of the loop: ring + cent_50 + decay-band + osc P2P all closed), multi-stage
secondary in-loop damping (22/23 — any in-loop filter moves the system poles:
level==decay, optimizer drove it transparent, engine reverted), post-loop level
shaping via BandTrim/pteq env hooks (LIVE + T60-orthogonal as predicted, but
inert on cent_500: a −9 dB notch AT 3253 Hz moved it 16 Hz — cent_500 is the
balance point of a broad static-spectrum tail, not a mode). Remaining 20 =
time-varying spectral decay the anchor has and a static-spectrum tank can't
express (anchor cent 2214→1346 over 50→500 ms; DV 2809→3253) + intrinsic
pitch-chorus/diffusion_flux. Preset-tuning floor reached on every axis.

## Structural walls (fleet-wide, dominate the leaders)
- **Early-field wall** (attack/onset/energy_t50/first50 + diffusion_flux): worst
  on the front-loaded rooms — Tiled (first50 ≈ 69%), Ambience (54%), MDR. No
  FDN/tank preset tuning closes it; needs the early-field engine (SparseField was
  the prototype, falsified under sustained-correct gates — see memory).
- **FDN gain==decay==level coupling** (Vocal Plate, 79 Vocal Chamber, Cathedral):
  feedback gain sets decay AND level per band; per-band level trims drag T60.
- **boom / body late-window curvature** (VH/DP/MDR): anchor holds early level,
  DV decays exponentially; EDT shaper stays disabled (documented IMD).
- **Modal residuals** (Bright Hall): 1 kHz steady overshoot + 12.9 kHz needle are
  delay-placement artifacts of the 32-line set — not closable by the inert
  Mid/Treble Multiply (octave GEQ flattens 3-band damping) or broadband Hi Cut.

## Method notes (carried forward)
- Re-measure the baseline on the CURRENT harness before any never-worse call —
  the stale-board "9" nearly triggered a wrong Bright Hall revert.
- Optuna cold-start only; manual off per-gate deltas for already-tuned presets.
- The 32-line AccurateHall32 is a built/bit-null engine slot; only Bright Hall
  ships on it. FDN / Vintage Hall / Sparse engines are unused but selectable.
