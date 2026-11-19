# Opto grey-box round 1: steps 1 and 2 (2026-10-02, in progress)

Brief: the owner's paste "MC-2 OPTO parity, new method" (2026-10-02). Method memory: grey-box, gain-trajectory
loss, joint fit of the existing cell's constants. This file is the running report; the verdict section is
rewritten when step 5 closes. Harness code: tools repo `plugins/MultiComp/tests/programme_parity/opto/greybox/`.
Evidence: `build-multi-comp-1176/opto-greybox-20261002/` (local only).

## Status

| Step | State |
|---|---|
| 1 extraction, verified | **PASS** (A: 522/522 static cells within 0.015 dB; B: 42/42 stems, pooled 0.0069 dB rms) |
| 1 R11a baseline table | done: 0.455 dB rms pooled, p95 stem rms 0.99 dB (7,255 stems; the brief's "11,248 stems" counts 7,506 renders) |
| 2 twin parity | **FAIL as stated** (max 0.0044 dB vs 1e-4 bar) but inside the C++ core's own build-to-build spread (0.011 dB); flagged below |
| 2 constants enumeration | done (36 fitted scalars, 2 measured tables held) |
| 3 joint fit | pre-registered and running (`fit/preregistration.json`, `fit/log.txt`) |

## Flags for the owner (standing rule: flag and ask)

1. **Twin parity bar.** The float64 twin reproduces the C++ cell to max 0.0044 dB / rms 0.0007 dB over the 20
   stems. The C++ core itself is not reproducible below about 1e-3 dB: the same source built with and without
   FMA contraction differs by max 0.011 dB (rms 0.0008), and the cell compiled in a different translation unit
   from the plugin's differs by max 0.012 dB. The feedback loop (light -> gain -> side chain) amplifies ulp-level
   rounding. A 1e-4 dB bar is below the reference's own noise. The fit does not use the twin: its forward is the
   exact C++ core with mutable constants, so there is no twin-to-core gap in the fitted result. I propose to read
   the parity obligation as "within the C++ self-noise", which holds; I did not stop step 3's preparation.
2. **Autograd replaced by forward differences on the C++ core.** A torch autograd twin of a 576k-sample nonlinear
   recursion runs at about 35M kernel launches per stem on CPU; a Newton iteration over 1,500 stems would take
   days. The forward-difference Jacobian in the transformed constant space (one C++ forward per constant, 5.2 s
   per stem for 36 constants) gives Gauss-Newton with LM damping in about 18 minutes per iteration. The twin
   remains as the readable reference of every constant.
3. **DEV set.** The C1 DEV set (with PR-0 companions) was deleted in the 10-02 cleanup; the C2 corpus is all
   FIT. DEV is a clip-level 10 % split of the C2 corpus (seed 20261002, every pass of a DEV clip is DEV).
4. **The machine is on battery.** Long jobs run under caffeinate; AC and an open lid are needed for the fit.

## Step 1: gain-trajectory extraction

Definition (pre-registered form, instrument details added where the verification demanded them):

- `G[k] = 10 log10( E_y[k] / E_xlin[k] ) - makeup_dB` per 240-sample frame (200 Hz), energies summed over both
  channels, causal HP20 (4th-order Butterworth) on both; `y` latency-compensated (UAD 87, MC-2 73).
- `x_lin` = the stimulus through the device's complete PR-0 linear path: the two native sub-audio high-passes
  (1.0108 Hz Q .8388 before the stage, 0.9891 Hz Q .8208 after, the T4-P5 fit of the UAD impulse tails) as exact
  IIR, then a short measured impulse response (cross-spectrum of 18 linear C2 white-noise stems at PR < 0.12 and
  peak < -12 dBFS for the UAD; a PR-0 lab render for MC-2; the HPs divided out, crossfaded to the shelf below
  300 Hz). Without the analytic HPs, 20-40 Hz tones jitter by +-0.6 dB per frame: a few degrees of phase shift
  moves the within-period energy pattern across the 5 ms frames.
- Make-up removed with the native Gain taper (`kOptoGainTaper`); it matches the UAD PR-0 tones within 0.002 dB
  up to -8 dBFS.
- Kept frames: raw stimulus frame above -60 dBFS (the brief's floor) and **output peak below 0.8**. Above
  that the UAD output stage limits (plateau about 1.45): at -4 / 0 dBFS the plain ratio reads 0.25 / 1.5 dB of
  false reduction, identically at PR 0. 72 of 594 static cells and the loud frames of a few corpus stems are
  excluded this way; the stage is not part of the gain computer.
- Model side (logged or simulated g): the plugin applies the gain at 2x to the wide-FIR-upsampled shelved audio
  and the gain has sample-rate structure (adjacent phases differ by up to 0.005), so the frame functional is
  `10 log10( sum (sub(down2(g_os * up2(shelf x))))^2 / sum (sub(down2(up2(shelf x))))^2 )` with the odd
  decimation phase, HP20 on both. A host-rate `sum (g x)^2 / sum x^2` misses by 0.05 dB on 50-100 Hz tones.

### Verification A: static tone library (pre-registered bar 0.05 dB)

UAD captures `opto-production-harmonics-20260928/captures/base/uad`: 3 Gains x (PR .35/.70/1.0 x Compress/Limit)
x 33 tones (100 / 1000 / 5000 Hz, -40..0 dBFS). Known law = the fixture recipe, `10 log10(P_PR0 / P_PRx)` over
6.0-7.5 s against the PR-0 companion capture of the same set.

| | Cells | rms | max | within 0.05 dB |
|---|---:|---:|---:|---:|
| Kept (output peak < 0.8) | 522 | 0.0069 dB | 0.0157 dB | **522** |
| Dropped (stage-limited: -4 / 0 dBFS at Gain .25 / .35) | 72 | | | |

Linear corpus stems (384 stems, PR < 0.12, peak < -12 dBFS; G must be 0): rms 0.0072 dB over stems, every
family between 0.006 and 0.009, worst stem 0.028 (tones from 20 Hz to 15.6 kHz within +-0.02 dB); full table
in `verify-static.json` (`linear_stems`).

### Verification B: synthetic (pre-registered bar 0.05 dB rms)

R11a lab render (full MultiCompDSP from a pristine git-archive of 08b95e83, neutral host state, 2x) on 42 C2
stems (6 per family, seed 20261002); extraction from the rendered output against the logged g through the frame
functional.

| Family | n | rms (dB) | worst stem |
|---|---:|---:|---:|
| burst | 6 | 0.0125 | 0.023 |
| lf | 6 | 0.0076 | 0.015 |
| noise | 6 | 0.0043 | 0.007 |
| release | 6 | 0.0029 | 0.004 |
| stereo | 6 | 0.0032 | 0.005 |
| tone | 6 | 0.0017 | 0.003 |
| twotone | 6 | 0.0043 | 0.008 |
| **pooled** | 42 | **0.0069** | 42/42 within the bar |

### R11a baseline error table

`gb_step1.py baseline` rendered every non-idle C2 stem (7,389) through the R11a lab core; G_uad extracted from
the UAD stem, G_r11a from the logged g through the frame functional (`targets/<pass>/<clip>.npz`,
`baseline-r11a.json`). 7,255 stems have at least 10 kept frames. Error = G_r11a - G_uad per kept frame.

| Slice | stems | rms pooled (dB) | median stem rms | p95 stem rms | mean signed (MC-2 minus UAD) |
|---|---:|---:|---:|---:|---:|
| all | 7255 | 0.455 | 0.189 | 0.986 | +0.007 |
| family burst | 1228 | 0.516 | 0.360 | 1.146 | +0.032 |
| family lf | 727 | 0.447 | 0.192 | 0.868 | -0.012 |
| family noise | 744 | 0.613 | 0.125 | 1.512 | +0.169 |
| family release | 1004 | 0.478 | 0.230 | 0.878 | +0.021 |
| family stereo | 1116 | 0.437 | 0.188 | 0.884 | +0.051 |
| family tone | 1228 | 0.403 | 0.126 | 0.811 | -0.100 |
| family twotone | 1208 | 0.340 | 0.080 | 0.752 | -0.052 |
| mode comp | 3639 | 0.402 | 0.181 | 0.853 | -0.013 |
| mode limit | 3616 | 0.503 | 0.199 | 1.110 | +0.026 |
| PR 0.00-0.17 | 1500 | 0.147 | 0.007 | 0.181 | -0.011 |
| PR 0.17-0.34 | 1476 | 0.250 | 0.055 | 0.578 | -0.015 |
| PR 0.34-0.51 | 1565 | 0.376 | 0.230 | 0.784 | +0.024 |
| PR 0.51-0.68 | 1354 | 0.503 | 0.342 | 1.041 | +0.003 |
| PR 0.68-0.85 | 1360 | 0.757 | 0.506 | 1.575 | +0.032 |
| Gain 0.10-0.21 | 3355 | 0.456 | 0.206 | 0.969 | +0.000 |
| Gain 0.21-0.32 | 1856 | 0.467 | 0.197 | 0.998 | +0.011 |
| Gain 0.32-0.43 | 802 | 0.456 | 0.186 | 1.026 | +0.004 |
| Gain 0.43-0.54 | 580 | 0.417 | 0.133 | 0.990 | +0.019 |
| Gain 0.54-0.65 | 662 | 0.447 | 0.147 | 0.872 | +0.021 |

The extraction from the rendered R11a audio instead of the logged g gives the same pooled figure (0.466 dB).
The error grows with PR (0.15 dB below PR .17 to 0.76 dB above .68), Limit is worse than Compress, and noise is
compressed 0.17 dB more than the UAD on average while tones are compressed 0.10 dB less. Worst stems: c2release_0128 PR 0.83 Limit 3.67 (mean +1.52, UAD GR 7.1); c2release_0128 PR 0.84 Comp 3.61 (mean +1.50, UAD GR 7.0); c2noise_0137 PR 0.76 Comp 3.42 (mean +0.07, UAD GR 15.1); c2tone_0442 PR 0.84 Limit 3.06 (mean +3.06, UAD GR 44.1); c2twotone_0031 PR 0.81 Limit 2.77 (mean +2.75, UAD GR 28.6); c2release_0366 PR 0.77 Limit 2.76 (mean +0.80, UAD GR 9.1).

## Step 2: the twin and the constants

### Parity (20 stems, 3 per family, seed 20261002)

| Comparison | max (dB) | rms (dB) |
|---|---:|---:|
| twin (float64) vs C++ cell, same feed | 0.0044 | 0.0007 |
| twin vs full-DSP probe | 0.0113 | 0.0007 |
| C++ with vs without FMA contraction (same source) | 0.0114 | 0.0008 |
| C++ cell TU vs plugin TU (same source, same flags) | 0.0125 | 0.0001 |

Bar 1e-4 dB: **not met**; see flag 1. Stems with g = 1 throughout agree exactly. Coefficient designs (20
biquads, 21 scalars) agree with the C++ dump within 3.2e-5 relative (float32 design arithmetic).

### Constants (`constants.json`)

62 header constants enumerated with value, physically bounded range and a sensitivity probe (frame functional on
3 stems, +1 % or the smallest switch-on step). Fitted in pass 1 (36): elTauSeconds, fastRelease,
fastBimolecular, slowCharge, slowRelease, slowBimolecular, memoryCharge, memoryRelease, limitOutputMix,
limitInputMix, compressInputMix, timeScaleLight, timeScaleExponent, lfTermGainDb, lfTermFrequency, lfTermGrSpan,
flashCharge, flashRelease, flashBimolecular, lfShelfDb, lfLitTauAttack, lfLitTauRelease, lfLitLevelDb,
lfLitSpanDb, lfLitFloor, elFreqExponent, elFreqTau, hfPeakDb, lfPeakDb, midPeakDb, hf10kPeakDb, elFreqCorner,
trapCapture, trapEmit, trapCapacity, prSlopeBelow. Held: structural switches and disabled blocks (lightCeiling,
depthWeighting, lowFrequencyFloor, conductanceTable*, hFloor, lfLitLog, hChargeOnly, attackRate*, elRev*, the
(P) element, table geometry, prGate) and the two measured static tables (light table 37, PR gain table 45; a
pass 2 over them is pre-registered only if the pass-1 residual shows a settled pattern by PR).

## Step 3: pre-registration (summary; full text in `fit/preregistration.json`)

Loss = mean square of `G_model - G_uad` (dB) over kept frames, pooled over a fixed 1,500-stem stratified subset
of FIT (family x PR tercile x mode). Optimiser = Gauss-Newton + LM damping, forward-difference Jacobian in the
transformed space (log for rate-like constants), bounds by clipping, backtracking on lambda; stop at 25
iterations or two relative improvements below 1e-4. Selection = lowest DEV rms. Held-out music, interim
settings, LEWITT VAL and the 1 kHz witness are not evaluated during the fit.

## Step 3: fit progress and residual probes (2026-10-02 afternoon)

| Iter | FIT rms (dB) | DEV rms (dB) |
|---|---:|---:|
| 0 (R11a) | 0.455 | 0.462 |
| 1 | 0.423 | 0.437 |
| 2 | 0.414 | 0.422 |
| 3 | 0.398 | 0.408 |
| 4 | 0.397 | 0.407 |
| 5 | 0.396 | 0.409 |
| 6 | 0.396 | 0.406 (selected) |

Process notes: the first run stopped itself after iteration 2 through a bug in my stop rule (step norm read
after the update); fixed, resumed. A second resume launched while the first run was only swapped out (the
machine sat at 15 GB used / 9 GB swap with my probes alongside) and was killed; the original run continues.

### Residual on DEV, R11a vs iteration 4 (`fit/residual-*.json`)

| Slice | R11a rms | iter-4 rms | iter-4 mean |
|---|---:|---:|---:|
| all | 0.462 | 0.407 | -0.013 |
| static part (per-stem mean) / dynamic part | 0.291 / 0.355 | 0.238 / 0.325 | |
| settled frames | 0.352 | 0.300 | -0.012 |
| attack frames (UAD GR rising > 1 dB / 50 ms) | 1.100 | 0.986 | +0.011 |
| release frames | 1.020 | 0.951 | -0.033 |
| GR 0-1 / 1-3 / 3-6 dB | 0.13 / 0.35 / 0.55 | 0.11 / 0.29 / 0.41 | |
| GR 6-10 / 10-15 / 15-25 / 25-60 dB | 0.68 / 0.72 / 0.86 / 0.81 | 0.53 / 0.62 / 0.83 / 0.74 | +0.10 / -0.12 / -0.15 / +0.06 |
| noise family (dynamic part) | 0.723 (0.611) | 0.638 (0.590) | +0.109 |

The fit lowers the settled and shallow-reduction error; the frames where the gain is moving (9 % of frames,
about 1 dB rms) and the deep-reduction frames move little.

### Isolating analyses on existing data (no new capture)

1. **Gain timing** (`fit/lag-iter-04.json`, 242 burst + release DEV stems): shifting the model gain by -3..+3 ms
   before the functional has its minimum at 0 for attack (0.980), release (0.829) and all frames. Not a latency.
2. **History** (`fit/history-iter-04.json`, 1,572 onsets over every burst stem): onset error at 5 / 20 / 100 ms
   is 0.94 / 0.81 / 0.55 dB rms with means +0.14 / +0.13 / -0.05; correlation with quiet time before the onset,
   previous burst level and length is at most 0.16; with PR 0.21 and with the pre-onset reduction 0.16. The model
   over-attacks by +0.42 / +0.46 dB at 5 / 20 ms when the cell already sits at 10-20 dB, and by +0.32 dB after
   gaps shorter than 50 ms. Release after offsets: +0.20 / +0.10 / +0.10 dB at 20 / 100 / 400 ms (model releases
   less), rms 0.57 / 0.48 / 0.35.
3. **Motion amplitude** (`fit/motion-iter-04.json`): std of the 5 ms GR increments, model / UAD. R11a was 0.73-0.88
   at 1-10 dB on release / stereo / noise (too shallow); iteration 4 is 0.85-1.22 everywhere. The increment
   correlation is 0.86-0.98 on bursts, release and stereo but **0.63-0.74 on noise**: the right amount of motion,
   wrongly shaped on broadband input.

Reading: the remaining error is not a timing or a history (memory) term and the fast motion is now of the right
size; it is the shape of the response to broadband/noise-like input and the depth law above 10 dB. Candidate
block: the detector statistic feeding the light.

4. **Crest-factor probe** (`fit/crest-r11a-iter-05.json`; the existing O1 capture
   `opto-programme-20260916/crest`: sine / white / pink at -12 / -18 / -24 dBFS, PR .4 / .625 / .85 / 1.0,
   Compress, settled over the last 2 s against the PR-0 companion). Crest term = (noise error) - (sine error)
   at the same level and PR, model minus native:

   | | white mean / rms / max | pink mean / rms / max | sine error range |
   |---|---|---|---|
   | R11a | -0.55 / 0.58 / 0.77 | -0.72 / 0.73 / 1.01 | -0.20 .. +0.20 |
   | pass 1 iter 5 | -0.24 / 0.32 / 0.61 | -0.42 / 0.49 / 1.10 | -0.16 .. +0.15 |

   34 of 36 noise cells negative: the model reads broadband input as less drive than the UAD does, pink (LF-heavy)
   more so than white, at every PR and level, while the sine statics are within 0.2 dB. The light table (el ->
   light, convex in dB) sets how noise peaks drive the cell relative to a sine of the same rms; it was held in
   pass 1 along with the PR gain table.

**Pass 1 stopped by hand after iteration 6** (DEV 0.406, selected): the relative improvements were 6.0e-3, 3.6e-3,
1.6e-3 and falling geometrically, so the remaining pass-1 gain is below 0.005 dB while steps 4 and 5 are still
ahead in the box. This deviates from the pre-registered 25-iteration rule; recorded in `fit/selection.json`.

**Pass 2 launched** (`fit/pass2/preregistration.json`, written before any pass-2 forward): the 36 pass-1
constants plus the 37 light-table entries as log parameters (bounds 1e-6..1e5, 2 % steps), starting from
pass-1 iteration 6, PR gain table held, 12 iterations maximum, stall rule 1e-3, selection on DEV. The pass-1
residual named the block; this is the single refit the brief allows, and the crest probe is re-run on the
result as the measured check.

## Pass 2 result (2026-10-02 18:05)

| Pass 2 iter | FIT rms | DEV rms |
|---|---:|---:|
| 0 (= pass 1 iter 6) | 0.396 | 0.406 |
| 1 | 0.389 | 0.403 |
| 2 | 0.385 | 0.396 |
| 3 | 0.382 | 0.394 |
| 4 | 0.380 | **0.3935** (selected) |
| 5 | 0.380 | 0.394 |

Stopped by hand after iteration 5 (DEV flat over two iterations while FIT still fell: the table freedom had
started to fit the subset). Light-table entries that moved by more than 10 %: 1:1.61 2:2.12 3:0.51 4:2.58 5:5.46 6:4.03 7:2.27 8:0.24 9:1.51 10:2.54 11:1.38 13:1.10 14:1.14 18:0.85 19:0.82 20:1.25 21:1.25 22:1.19 23:0.80 24:0.69 25:0.35 26:19.32 27:5.31 28:17.25 29:1.12 30:0.58 31:0.07 32:0.77 33:0.90 34:37.50 35:0.35 36:6.73
(index = 1 dB steps from -59 dBFS el). The top of the table (indices 25-36, the rarely reached high-light
end) went zig-zag (0.07x, 19x, 17x, 37x on neighbouring entries): an ill-conditioned, unphysical table, so the
pass-2 constants are **not a ship candidate as they stand**; a smoothness constraint or a coarser light
parametrisation would be needed before export. Scalars that moved more than 5 %: 17.

**The named block did not move.** On DEV with the pass-2 constants (`fit/residual-pass2-best.json`,
`fit/crest-final.json`, `fit/motion-pass2-best.json`):

| Quantity | pass 1 (iter 6) | pass 2 (iter 4) |
|---|---:|---:|
| DEV rms, all | 0.406 | 0.393 |
| settled frames | 0.300 | 0.288 |
| release frames | 0.951 | 0.935 |
| noise family | 0.638 (mean +0.109) | 0.641 (mean +0.116) |
| GR 15-25 dB | 0.829 | 0.787 |
| crest term white / pink (mean) | -0.21 / -0.39 | -0.25 / -0.47 |
| noise 5 ms increment correlation | 0.63-0.74 | 0.64-0.74 |

The pooled loss was lowered through the settled and shallow frames; the broadband drive deficit, the noise
motion shape and the moving-gain frames are where they were. The light table's convexity is therefore not the
mechanism behind the crest term.

**What the crest term measures, stated as a number for the next step:** at equal rms the UAD's detector drives
the cell as if a white-noise input were 3.2 dB above a sine (native sine-equivalent shift, O1 crest capture);
the model's reads 2.3 dB, and for pink noise the gap is larger. A detector that reads the rectified mean sees
Gaussian noise 2.0 dB above a sine; a peak follower sees it 8-10 dB above. The UAD sits between, nearer the
mean; the model sits at the mean. The block is the statistic the EL panel integrates (el <- |s| with a 2 us
time constant, then the light table), and its dependence on spectrum (pink > white) points at the loop
weighting ahead of it. This is a measured deficit of 0.25-0.5 dB of drive on broadband input at every PR and
level; it was not fitted away by any of the 73 constants.

### Full-corpus evaluation of the selected constants (`fit/eval-*.json`)

| | DEV rms (732 stems) | DEV p95 stem rms | FIT rms (6,657) | FIT p95 stem rms | PR < .17 / .17-.34 / .34-.51 / .51-.68 / > .68 (FIT) |
|---|---:|---:|---:|---:|---|
| R11a | 0.462 | 0.967 | 0.455 | 0.987 | 0.15 / 0.25 / 0.38 / 0.50 / 0.76 |
| pass 2 iter 4 | 0.393 | 0.797 | 0.388 | 0.832 | 0.11 / 0.18 / 0.30 / 0.42 / 0.68 |

FIT and DEV agree to 0.005 dB: no overfitting at the stem level; the gain is spread over every family
(noise 0.60 -> 0.53, tone 0.40 -> 0.30, release 0.49 -> 0.42 on FIT) and every PR bin, with the high-PR bin
still at 0.68 dB.

## Day-1 verdict and plan

- Gain computer on the C2 corpus: R11a 0.455 -> 0.393 dB rms (DEV), settled frames 0.29 dB, moving-gain frames
  0.9-1.0 dB, broadband 0.64 dB. The music bar (0.5 dB rms, 1.5 dB p95) is not measured yet; by the brief it is
  measured once at step 5.
- Flags for the owner stand (twin parity bar, forward differences, DEV from C2, both passes stopped by hand).
- Day 2: (a) one isolating probe through the host for the detector statistic: band-limited stimuli at fixed rms
  and fixed spectrum with crest factors 3, 6, 9, 12 dB (clipped sine, sine, two-tone, Gaussian, bursts), PR .5
  and .85, settled GR, minutes of capture, lock held; (b) the measured statistic goes into the cell's EL panel
  as a constant (no classifier, no table of signals), one refit; (c) step 4 check C from the R13 tensor;
  (d) step 5 battery and the music bar once.

## Step 4: check C (2026-10-02 evening): P2 does not hold, step 4 stops here

From the R13 element tensor (`opto-r13-20261002/element/element-tensor.json`, 1.2 kHz, Gain .25), H2 of the UAD
output in dBFS at PR .5 and .4, both modes, against two predictions anchored at the -12 dBFS cell. A2(x) is the
hard kink's own 2f amplitude (T -26.1 dBFS) at that input level, computed numerically; GR is the measured
reduction of the cell (input + 1.0 dB make-up - H1).

- P1: H2 ∝ g · A2(x) (element ahead of the cell, no depth modulation)
- P2: H2 ∝ g² · A2(x) (depth scaled by the cell gain, the R13 "-1.00 dB/dB" reading)

| Mode, PR | level | H2 measured | P1 error | P2 error | best exponent k in H2 ∝ g^k A2 |
|---|---:|---:|---:|---:|---:|
| Comp .5 | -18 | -71.43 | -3.51 | +0.97 | 1.78 |
| | -6 | -73.23 | +3.04 | -1.80 | 1.63 |
| | 0 | -74.12 | +5.13 | -4.61 | 1.53 |
| Comp .4 | -18 | -69.25 | -2.27 | +1.24 | 1.65 |
| | -6 | -67.89 | +2.31 | -2.30 | 1.50 |
| | 0 | -68.17 | +3.81 | -5.68 | 1.40 |
| Limit .5 | -18 | -71.93 | -3.79 | +0.97 | 1.80 |
| | -6 | -75.32 | +3.54 | -1.81 | 1.66 |
| | 0 | -77.98 | +6.51 | -4.63 | 1.58 |
| Limit .4 | -18 | -69.42 | -2.49 | +1.24 | 1.67 |
| | -6 | -69.17 | +2.64 | -2.30 | 1.53 |
| | 0 | -70.62 | +4.72 | -5.69 | 1.45 |

The measured H2 sits between the two laws with errors of opposite sign, and the exponent is neither 1 nor 2
and drifts with level. "Depth × cell gain" is not the law in absolute terms; the R13 slope of -1.00 dB/dB was a
ratio-to-output-fundamental reading normalised at the onset cells, which this absolute check does not
reproduce at -6 / 0 dBFS. Per the brief ("if P2 holds ... then implement"), no dense d(PR) probe and no
element implementation follow. The output stage's own H2 at these output levels (-16 .. -20 dBFS) is far
below the measured -68 .. -78 dBFS and does not explain the gap.

## Step 3, the isolating probe through the host (2026-10-02 evening)

New capture `opto-greybox-20261002/crest-probe` (pinned host, UAD b787, render lock): sine, clipped sine,
two-tone, 8-tone, and 100 Hz-band Gaussian noise, all around 1.2 kHz, at rms -26 and -18 dBFS, PR 0 companion,
PR .5 Compress, PR .85 Compress, PR .5 Limit, Gain .25; settled over the last 2 s. Crest term = (model - native)
minus the same for the sine at that rms and pass.

| Kind | crest | native minus sine (GR dB, 6 cells) | crest term R11a | crest term pass 2 |
|---|---:|---|---|---|
| clipped sine | 1.1 dB | -0.15 .. -0.46 | -0.05 .. +0.16 | -0.12 .. +0.01 |
| two-tone | 6.0 dB | +0.38 .. +1.09 | -0.43 .. -0.07 | -0.05 .. +0.37 |
| 8-tone | 6.1 dB | +0.17 .. +0.64 | -0.24 .. -0.05 | -0.05 .. +0.21 |
| Gaussian band noise | 11.6 dB | +0.43 .. +1.42 | -1.09 .. -0.27 | **-0.68 .. -0.17** |

At fixed rms and spectrum the deterministic high-crest signals are within +-0.1 dB after pass 2 (R11a was 0.2-0.4
short); only Gaussian noise is read short, by 0.2-0.7 dB, most at PR .85. The deficit is specific to random
signals with rare tall peaks, not to crest factor as such.

Two mechanisms tested on this probe, both falsified (`crest-probe/elrel-grid.json`, scratch slope grid):

1. **EL panel peak hold** (candidate header with a separate release time; bit-identical to the pristine cell at
   equal taus, verified on two stems): over 0.25 us .. 3 ms the noise crest term moves from -0.444 to -0.393 dB
   while the sine error rises from 0.17 to 0.94 dB rms. Not the mechanism.
2. **Light law above the table top** (`lightSlopeAbove` 0.08 .. 0.40): no change to any cell at any value; the EL
   level never reaches the table top on these stimuli. Not the mechanism.

The block therefore sits inside the table range: how the cell integrates a fluctuating light (populations with
bimolecular recombination and the CdS time-scale modulation) rather than the static light law. No constant in the
header moves it; a mechanism would be new structure, which this round does not add. The decision gate the brief
pre-registers (held-out music residual after one structural refit) is being measured.

## The gate: held-out music residual (viewed once, 2026-10-02 19:30; `music-r11a-iter-06-pass2-best.json`)

Real-music corpus `opto-music-20260923` (UAD natives, Gain .25, PR .3125 .. 1.0 x Compress/Limit = 16 settings,
latency 87), the step-1 instrument on both sides, lab core for the model. VAL = the 9 v_ clips, never used
before this view.

| Split | Constants | rms (dB) | p95 of abs error | p99 | mean | Comp / Limit | bar |
|---|---|---:|---:|---:|---:|---|---|
| FIT (17 clips) | R11a | 0.539 | 1.02 | 2.06 | +0.03 | 0.47 / 0.60 | rms FAIL |
| | pass 1 (it 6) | 0.658 | 1.14 | 2.38 | -0.31 | 0.65 / 0.67 | rms FAIL |
| | pass 2 (it 4) | 0.633 | 1.12 | 2.30 | -0.28 | 0.61 / 0.66 | rms FAIL |
| **VAL (9 clips)** | R11a | 0.770 | 1.65 | 3.22 | +0.10 | 0.63 / 0.89 | FAIL / FAIL |
| | pass 1 (it 6) | 0.801 | 1.58 | 3.35 | -0.13 | 0.68 / 0.90 | FAIL / FAIL |
| | **pass 2 (it 4)** | **0.772** | **1.56** | 3.30 | -0.05 | 0.63 / 0.89 | **FAIL / FAIL** |

By PR on VAL (rms, pass 2 vs R11a): .3125 0.23 vs 0.44, .406 0.29 vs 0.48, .5 0.33 vs 0.54, .625 0.51 vs 0.52,
.719 0.74 vs 0.63, .8125 0.95 vs 0.85, .906 1.12 vs 1.07, 1.0 1.25 vs 1.23. Worst clips on every set: kick,
bass synth, bass amp / DI, song.

**Verdict of step 3:** the corpus fit transfers to music below PR .6 (halved error) and not above it; the C2
corpus has no PR above .85, and the high-PR bin is the corpus's worst bin too (0.68 dB). The music mean went
from +0.10 to -0.05 (R11a under-compressed, pass 2 slightly over). The bar is not met by the physical cell with
any constants found; the pre-registered fallback (micro-TCN gain computer trained on g[n]) is engaged.

**Before the fallback, one capture both routes need:** a PR .85-1.0 supplement of the C2 corpus (existing
stimuli, new settings, `c2hi` passes), so neither the cell nor a learned gain computer extrapolates where the
music fails hardest.

## Fallback engaged: micro-TCN gain computer on g[n] (`opto-greybox-20261002/tcn/`, pre-registered before training)

- **Input**: the plugin's own linked side chain (the louder of L/R per host sample), reduced to three 6 kHz
  features per 8-sample block (max|s|, mean|s|, rms, in dB/40, -100 dB floor); PR and mode by FiLM. No audio
  is reconstructed; the output is the gain trajectory only.
- **Net**: 1x1 conv to 16 channels, 7 causal residual blocks of dilated conv (k 5, dilations 1..4096, receptive
  field 3.6 s), PReLU, FiLM; 1x1 head; mean over 30 samples to 200 Hz; G = -softplus (dB). About 13k weights;
  about 54 MMAC/s at 6 kHz, under 1 % of a core (the previous learned attempts ran at audio rate or
  reconstructed audio; this one does neither).
- **Data**: C2 FIT stems plus the PR .85-1.0 supplement (`c2/corpus-hi`, 600 passes / 898 stems on the same
  stimuli, seed 20261003, captured tonight); targets from the step-1 instrument; DEV = the same clip-level DEV
  split as the physical fit; music never in the loop.
- **Training**: Adam 2e-3, batch 16 stems, 40 epochs, cosine decay, seed 20261003; selection = lowest DEV rms.
- **Gate**: music VAL once, same bar (0.5 dB rms, 1.5 dB p95). Export would be weights in C++ (constants
  cannot express it); the physical cell stays the default until the gate passes and the owner rules.

### Fallback runs (2026-10-02 night; `tcn/`, `tcn-c/`; every change recorded in the preregistration files before selection)

| Run | Front-end | Net | Epochs | DEV rms (dB) | Note |
|---|---|---|---:|---:|---|
| 6k (abandoned) | 6 kHz, 3 feats | 16 ch, 7 blocks, linear FiLM | 1 of 40 | 2.51 | 25 min per epoch on CPU; stopped |
| a (abandoned) | 1.2 kHz | 16 ch, 6 blocks, linear FiLM | 4 of 40 | 1.83 | static law not learned (settled tones 2-5 dB off); stopped |
| **b** | 1.2 kHz, dB/20 | 32 ch, 6 blocks, MLP FiLM on PR polynomial | 40 | **0.519** (train 0.474) | GPU (MPS), 40 s per epoch; the drop came in the cosine decay (0.89 at 20, 0.55 at 30); still falling at 40 |
| **c** | 2.4 kHz | 64 ch, 7 blocks, k 7 | 80 | **0.365** (train 0.282) | GPU, 135 s per epoch; below the physical cell on DEV (0.393) from epoch 60 |

Physical cell on the same DEV: 0.393 (pass 2), 0.406 (pass 1), 0.462 (R11a). Run b is not better than the cell on
DEV and therefore gets no music view.

### Run c on music (viewed once, `tcn-c/music-best.json`)

| Split | rms | p95 | p99 | by PR (.31 .. 1.0) |
|---|---:|---:|---:|---|
| FIT | 0.714 | 1.41 | 2.52 | 0.38 0.48 0.54 0.60 0.75 0.85 0.91 0.97 |
| **VAL** | **1.039** | **2.02** | 4.04 | 0.57 0.68 0.75 0.82 0.92 1.19 1.44 1.50 |

Against the physical cell (VAL 0.772 / 1.56) and R11a (0.770 / 1.65) the learned gain computer is worse on music
by 0.27 dB rms although it is better on the synthetic corpus by 0.03. The synthetic corpus (tones, two-tones,
band noise, bursts, release pairs) does not span real programme; the net learns its families and not the
device. Bar FAILED on both routes.

## Day-1 closure (2026-10-03 02:00)

- **Bar not met** by either route: physical cell with fitted constants VAL 0.772 / 1.56; learned gain computer
  on the synthetic corpus VAL 1.039 / 2.02. The bar is 0.5 / 1.5.
- **Named gap**: the UAD gain computer above PR .7 on transient, LF-heavy programme (kick, bass synth, bass DI)
  and on noise-like input. The cell has no constant for it (every constant and both tables tried); the learned
  route has no data for it (the corpus is synthetic; music FIT was never in training).
- **What is reusable**: the verified instrument (`gb_common.py`, 0.007 dB), 8,287 stem targets, the PR .85-1.0
  supplement, the falsifications (timing, history, EL peak hold, light-law tail, "depth x gain" element law),
  the crest capture, the GPU training pipeline (135 s per epoch for a 64-channel net).
- **The one experiment left on the learned route**, not run (owner interrupted it): train with the 17 music FIT
  clips x 16 settings in the training set, VAL still untouched. It is the direct test of whether the gap is data
  (domain) or capacity. About 3 h on the GPU.
- **The one experiment left on the physical route**: a probe of the T4B light integration under fluctuating
  drive (AM tones with modulation depth and rate swept at fixed rms), to measure the mechanism behind the
  Gaussian-noise deficit before adding structure.
- No commits; harness in the tools repo, evidence local, native WAVs in neither.
