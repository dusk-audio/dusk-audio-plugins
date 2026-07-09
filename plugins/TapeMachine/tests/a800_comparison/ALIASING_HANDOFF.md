# Handoff: close the TapeMachine A800 aliasing gap (−47 → ~−63 dB)

> **RESOLVED 2026-07-09**: −63.25 dB (UAD −63.36). The dominant source was the
> `preSaturationLimiter` hard clip, not the waveshaper; fixed with `a800SoftLimit`
> (tanh knee) + `Local2xStage` (non-nested local 2× halfband) around both A800
> nonlinearities. The "−47 dB" below was measured with W&F defaults on — flutter FM
> sidebands, not aliasing; the true pre-fix floor (Wow=0/Flutter=0) was −56.3 dB.
> Details in `report/A800_TUNING_SUMMARY.md` ("Aliasing — RESOLVED").

Paste everything below as the opening prompt for a fresh session. It is self-contained.

---

## Mission
The TapeMachine **A800 mode** (`Swiss800`) was tuned to match the UAD Studer A800 across 7
measurement categories (done — see `plugins/TapeMachine/tests/a800_comparison/report/A800_TUNING_SUMMARY.md`).
**One residual remains: aliasing.** Mine measures **−47 dB** worst in-band spur on hot HF tones;
the UAD A800 is **−63 dB**. Close that ~16 dB gap.

### Hard constraints (do not violate)
- **No 8× (or higher) global oversampling.** This plugin must load on every DAW track — CPU is
  the binding constraint. The chain is already at 4× (192 kHz). Any fix must be CPU-cheap.
- **Real-time safety** (audio thread): no allocation, no locks, no I/O. `processSample` runs per
  sample at the OS rate × 2 channels.
- **Classic102 must stay byte-identical.** Every A800 change is gated on `machine == Swiss800`.
  Verify Classic102 is unchanged after your work.
- Keep the current harmonic/THD/FR match intact (don't trade tone for aliasing).

### Acceptance criteria
- Aliasing (worst in-band spur on the hot HF-tone test) improves from −47 dB toward **≤ −60 dB**.
- **No HF loss**: the frequency response and the 5 alias-test tone levels (5/8/11/15/19 kHz) must
  NOT drop (my failed ADAA attenuated 19 kHz by ~40 dB — that is the failure mode to avoid).
- Harmonic profile, THD-vs-level, and IMD stay within ~1 dB / a few % of current.
- `auval -v aufx DsTM Dusk` passes. Classic102 renders unchanged.

## Mandatory reading (in this order)
1. `plugins/TapeMachine/tests/a800_comparison/report/A800_TUNING_SUMMARY.md` — what was tuned + the residual.
2. `plugins/TapeMachine/core/TapeMachineDSP.hpp`:
   - `a800Shaper()` — the memoryless waveshaper: order-7 polynomial (constants `kSc1..kSc7`) for
     `|x| ≤ kSx0` (0.7), value+slope-matched **tanh knee** (`kSP0,kSS0,kSknee`) beyond. THIS is the
     aliasing source (order-7 poly makes high harmonics).
   - The A800 saturation branch in `processSample()` (search `machine == Swiss800` inside the
     `drive > 0.001f` block): `float sh = a800Shaper (signal * m_a800Drive) * m_a800DriveInv;`
     then `sh += 0.006f * (sh * sh);` (an even-harmonic term — **it also aliases**, include it in
     whatever antialiasing you apply). `m_a800Drive`/`m_a800DriveInv` are cached in `updateFilters`.
3. `plugins/TapeMachine/core/DuskOversampler.hpp` — **critical**. The chain oversampler is a
   **NESTED cascaded halfband**: `4× = 2× (stage A) inside 2× (stage B)`. `processSample(x, f)`
   calls the functor `f` `factor` times per input sample via `process2x` → `process4xInner`. The
   core `processSample` (and thus `a800Shaper`) runs INSIDE this functor at the OS rate, on
   halfband-interpolated subsamples.

## What was already tried and WHY IT FAILED (do not repeat blindly)
**1st-order ADAA** (antiderivative anti-aliasing): `y = (F(x)−F(xPrev))/(x−xPrev)`, F = analytic
antiderivative of `a800Shaper` (polynomial integral in-range + a numerically-stable
`stableLogCosh` in the knee), central-diff fallback for small `dx`.
- **Validated CORRECT and STABLE in an isolated Python reference** (level-preserving to −0.35 dB,
  no overflow, float32 == float64). The math is right.
- **In-plugin it broke badly**: HF tones attenuated ~40 dB, aliasing got WORSE (−10 dB).
- **Root-cause hypothesis (unconfirmed):** ADAA introduces a **half-sample group delay** at the OS
  rate; the shaper runs inside the **nested cascaded halfband** functor, and that half-sample delay
  interacts destructively with the two cascaded halfband group delays (down-A/down-B) → HF
  cancellation. The ADAA state `xPrev` sees the halfband subsample sequence, which may also not be
  the clean consecutive-time series ADAA assumes.
- The ADAA code was reverted/removed. Reconstruct from git history or the report if you want to retry it.

## Candidate approaches (measure, don't assume — pick by data)
1. **Local single-stage 2× oversampling of ONLY the waveshaper** (recommended first try).
   Wrap just `a800Shaper` + the even term in its own **non-nested** 2× halfband (a clean
   `HalfbandFIR` up/down, like one stage of `DuskOversampler`), so the shaper effectively runs at
   8× while the rest of the DSP stays at 4×. Sidesteps the nested-halfband/ADAA delay problem
   entirely. CPU = 2 short FIRs/sample for the shaper only. Verify the aliasing drop and that HF is
   preserved.
2. **ADAA reconciled with the halfband.** Either (a) compensate ADAA's exact 0.5-sample delay
   inside the halfband path, (b) apply ADAA at a single clean 2× stage instead of inside the nested
   4×, or (c) use a **2nd-order ADAA** (better HF, but also delayed — same reconciliation needed).
   First reproduce the failure and CONFIRM the delay hypothesis with a controlled experiment
   (e.g. measure the group delay the shaper functor actually sees).
3. **Lower the effective harmonic order for HF content** — a mild, cheap pre-shaper HF treatment so
   the polynomial generates fewer aliasing harmonics on the top octave. Risk: dulls HF; measure.
4. **What is UAD actually doing?** They hit −63 dB at low OS — the "magic" is near-certainly ADAA
   or a bandlimited nonlinearity. If you can find/observe their approach, mirror it.

## Measurement harness (already built — use it, don't rebuild)
Directory: `plugins/TapeMachine/tests/a800_comparison/`. Requires `numpy scipy soundfile matplotlib`.
- **Build the AU:** `cd plugins/TapeMachine/dpf-plugin && cmake --build build --target tape_machine_2-au -j8`
  (installs to `~/Library/Audio/Plug-Ins/Components/tape_machine_2.component`).
- **Renderer** (generic JUCE host): `build/tests/duskverb_render/duskverb_render`
  (build once with `-DBUILD_DUSKVERB_RENDER=ON`). Flags: `--au <path> --input-wav <wav>
  --output-dir <dir> --slug s --prerun-seconds 1 --param "Name=Value"`. A800 params:
  `--param "Tape Machine=0" "Tape Speed=1" "Tape Type=0" "EQ Standard=0" "Signal Path=0"
  "Calibration=2" "Oversampling=2"`.
- **Aliasing metric:** `deep_probe.py` function `aliasing(wavpath)` — feed it a render of
  `stimuli/alias.wav` (hot 5/8/11/15/19 kHz tones). Returns worst in-band non-harmonic spur (dB).
  Quick one-off:
  ```python
  import sys; sys.path.insert(0,'.'); from deep_probe import aliasing
  print(aliasing('<render>_stem.wav'))
  ```
- **Full 7-category regression:** `python3 deep_probe.py` (renders both plugins, prints all
  categories incl. aliasing). `python3 render_matrix.py --mine && python3 score_matrix.py` for the
  18-config FR/THD/W&F matrix. Confirm NOTHING else regresses.
- **UAD reference** (`−63 dB` target): `/Library/Audio/Plug-Ins/Components/uaudio_studer_a800.component`.
  **Needs the PACE daemon running** or UADx silently bypasses (flat output). If a UAD render is a
  clean Dirac/flat, run:
  `sudo launchctl bootstrap system /Library/LaunchDaemons/com.paceap.eden.licensed.plist;
   sudo launchctl kickstart -k system/com.paceap.eden.licensed` (verify `/var/tmp/com.paceap.eden.licensed/`).

## DPF param gotchas (will bite you)
- TapeMachine (DPF) **choice** params take the integer index (`"Tape Machine=0"`). **Float** params
  interpret `--param` value as NORMALISED 0–1 (`"Bias=20"` clamps to max!). Use `--nparam "Bias=0.2"`
  for float params, or leave them default.
- UAD params take display labels (`--param "IPS=30 IPS"`). Its `--dump-params` mis-decodes some
  fields — trust the audio, not the dump.

## Verification obligations (before claiming done)
1. `cmake --build build --target tape_machine_2-au -j8` succeeds; `auval -v aufx DsTM Dusk` PASSES.
2. `python3 deep_probe.py` — aliasing improved to ≤ −60 dB; harmonics/THD/IMD/bias/noise unchanged;
   **HF tones NOT attenuated** (compare 5/8/11/15/19 kHz seg levels to the pre-change render).
3. Classic102 renders byte-similar to before (`--param "Tape Machine=1"`), i.e. your change is
   A800-gated.
4. Report: before/after aliasing number, what approach worked, CPU impact (qualitative — how many
   extra ops/sample), and whether any HF/tone was traded. If it did NOT work, report the measured
   failure + root cause (a negative result with evidence is a valid deliverable).

## Scope wall
Touch only the A800 saturation/oversampling path in `plugins/TapeMachine/core/`. Do not retune the
head bump / EQ / bias / noise (already matched). Do not change the global OS factor. Do not modify
Classic102 behaviour.
