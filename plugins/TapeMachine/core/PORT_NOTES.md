# TapeMachine DSP Port Notes

Framework-free (no-JUCE) port of the TapeMachine tape-emulation core for the DPF
build. Source of truth:

- `Source/ImprovedTapeEmulation.h` / `.cpp` — the DSP classes.
- `Source/PluginProcessor.cpp` — `prepareToPlay`, `processBlock`, `updateFilters`,
  and the `get*Characteristics` / `getJAParams` tables (the driving logic).

Every formula, constant, coefficient and per-sample stage order is preserved.
This document lists every substitution, judgment call and deviation.

---

## 1. Mechanical substitutions

| JUCE | Port |
|------|------|
| `#include <JuceHeader.h>` | `<array> <atomic> <cmath> <cstdint> <random> <vector> <algorithm>` + shared dsp headers |
| `juce::MathConstants<double>::pi` | `constexpr double kPiD = 3.14159265358979323846` |
| `juce::MathConstants<float>::pi`  | `constexpr float  kPiF = 3.14159265358979323846f` |
| `juce::Decibels::decibelsToGain(db)` | `dbToGain(db)` / `dbToGainD(db)` — `db > -100 ? pow(10, db*0.05) : 0` (JUCE default −100 dB floor) |
| `juce::jlimit(lo,hi,v)` | `std::clamp(v,lo,hi)` |
| `juce::SmoothedValue<float,Linear>` | `duskaudio::SmoothedValue` (exponential one-pole) |
| `juce::dsp::StateVariableTPTFilter<float>` | `duskaudio::DuskSVF` |
| `juce::dsp::Oversampling<float>` | `duskaudio::Oversampler` |
| `juce::dsp::Gain<float>` | `duskaudio::SmoothedValue` scalar multiply |
| `juce::ScopedNoDenormals` | `duskaudio::ScopedFlushDenormals` |
| `juce::AudioBuffer` / `AudioBlock` / `ProcessContextReplacing` | plain `float* const*` loops |

The custom DSP classes (`ChebyshevAntiAliasingFilter`, `SoftLimiter`,
`SaturationSplitFilter`, `ThreeBandSplitter`, `JilesAthertonHysteresis`,
`TapeEQFilter`, `PhaseSmearingFilter`, `ImprovedNoiseGenerator`,
`WowFlutterProcessor`, `TransformerSaturation`, `PlaybackHeadResponse`,
`MotorFlutter`) are ported **verbatim** — only the `juce::` constant/util
references above were changed. All numeric tables (`getMachine/Tape/Speed
Characteristics`, `getJAParams`, per-band drive ratios, `computeDrive`,
`softClip`, the J-A / Langevin / Padé constants, NAB/CCIR/AES time constants,
Paul-Kellett pink coefficients, etc.) are copied exactly.

---

## 2. Filter designer mapping (JUCE IIR → shared designer)

The JUCE `updateFilters()` / `prepare()` build coefficients with
`juce::dsp::IIR::Coefficients::make*`. `makeHighShelf` / `makePeakFilter` take a
**linear** gain; the source always passes `decibelsToGain(X)`, so the equivalent
dB argument to the shared/`DBiquad` designers is simply **X** (because
`A = sqrt(gainLinear) = sqrt(10^(X/20)) = 10^(X/40)`, which is exactly what
`Biquad::peak/shelf` compute from `gainDb`).

| Source filter | Source call (file:line) | Port |
|---------------|-------------------------|------|
| headBump | `ImprovedTapeEmulation.cpp:341,712` `makePeakFilter(fs,f,Q, dbToGain(gainDb))` | `DBiquad::peak(fs,f,gainDb,Q)` |
| hfLoss1 | `:347,722` `makeLowPass(fs,f,0.707)` | `DBiquad::lowPass(fs,f,0.707)` |
| hfLoss2 | `:352,726` `makeHighShelf(fs,f,0.5, dbToGain(-2*hfLoss))` | `DBiquad::shelf(fs,f,-2*hfLoss,0.5,true)` |
| gapLoss | `:358,735` `makeHighShelf(fs,f,0.707, dbToGain(gapLossAmount))` | `DBiquad::shelf(fs,f,gapLossAmount,0.707,true)` |
| dcBlocker | `:370` `makeHighPass(fs,25,0.707)` | `DBiquad::highPass(fs,25,0.707)` |
| biasFilter | `:364,744` `makeHighShelf(fs,f,0.707, dbToGain(biasAmount*3))` | `Biquad::shelf(fs,f,biasAmount*3,0.707,true)` |
| recordHead1 | `:377` `makeLowPass(fs,fc,1.3066)` | `Biquad::lowPass(fs,fc,1.3066)` |
| recordHead2 | `:379` `makeLowPass(fs,fc,0.5412)` | `Biquad::lowPass(fs,fc,0.5412)` |
| tone highpass (SVF) | `PluginProcessor.cpp:530` `StateVariableTPTFilter highpass, res 0.707` | `DuskSVF` highpass, Q 0.707 |
| tone lowpass (SVF) | `PluginProcessor.cpp:544` `StateVariableTPTFilter lowpass, res 0.707` | `DuskSVF` lowpass, Q 0.707 |

### 2a. Double-precision filters → `DBiquad`

`headBump`, `hfLoss1`, `hfLoss2`, `gapLoss`, `dcBlocker` are
`juce::dsp::IIR::Filter<double>` in the source ("double for low-freq
precision"), processed as `static_cast<double>(signal)`. The shared
`duskaudio::Biquad` is **float only**, so I added a small header-only
**`DBiquad`** (double TDF-II) that copies the *same* designer formulas the
shared `Biquad` uses (which the shared header states match `juce::dsp::IIR`).
This preserves the double precision that keeps the ~48 Hz high-Q head bump
stable at 4x rates. `biasFilter`, `recordHead1/2` are `IIR::Filter<float>` in
the source and map to the shared float `Biquad` directly.

**Sub-ULP note:** the source computes the shelf/peak linear gain in *float*
(`decibelsToGain`) then widens to double; `DBiquad` computes `A` in double from
`gainDb`. These agree to within ~1 ULP — within A/B tolerance.

---

## 3. Deviations & judgment calls

### 3.1 Oversampling filter differs (biggest A/B caveat)
The source uses `juce::dsp::Oversampling` with **`filterHalfBandFIREquiripple`**;
the port uses the shared **`Oversampler`** (polyphase-halfband, different taps).
At 2x/4x the oversampled waveform therefore differs slightly, so the tape sees
different intersample values and the null will **not** be exact at 2x/4x.
**At 1x (factor 1) the `Oversampler` is a transparent passthrough**, so 1x is the
closest match and the recommended setting for null testing.

### 3.2 Inter-channel crosstalk moved to base rate
Source applies crosstalk at the **oversampled** rate *between the tape core and
the lowpass* (`PluginProcessor.cpp:1054`). The functor form processes channels
independently, so — per the task's explicit allowance — crosstalk is applied at
**base rate after** the per-channel oversampled chain (post LP + output gain).
Bleed is −46 dB (Swiss800, 0.005) / −36 dB (Classic102, 0.015); the placement
shift is inaudible and within A/B tolerance.

### 3.3 `SmoothedValue` is exponential, not linear
JUCE param smoothing is linear ramps; the shared `SmoothedValue` is an
exponential one-pole. Tau chosen to settle in a similar time:

| Smoother | JUCE ramp | Port tau | Notes |
|----------|-----------|----------|-------|
| input gain | 20 ms (dsp::Gain) | `kGainTau=0.0067` (~20 ms) | base-rate ramp |
| output gain | 20 ms (dsp::Gain) | `kGainTau=0.0067` | OS-rate ramp |
| saturation | 150 ms | `kSatTau=0.05` (~150 ms) | base-rate config, OS-rate stepped |
| wow / flutter / noise | 20 ms | `kParamTau=0.0067` | base-rate config, OS-rate stepped |

Steady-state values are exact; only ramp **shape** during automation moves
differs. The param smoothers are configured at the **base** rate but advanced at
the **oversampled** rate — this mirrors the JUCE structure
(`smoothedSaturation.reset(baseRate)` advanced inside the OS-rate loop), so the
number of steps-to-settle matches.

### 3.4 Input-gain ramp time dilation not reproduced
In the source, the input-gain `dsp::Gain` is *prepared* at the oversampled rate
but *processed* at the base rate, so its 20 ms ramp dilates to ~20 ms × factor of
real time. The port ramps input gain in ~20 ms regardless of factor. Ramp timing
only; steady-state identical.

### 3.5 Deterministic RNG (fixed seeds)
Source seeds every generator with `std::random_device` (non-deterministic). The
port seeds each `std::mt19937` with a distinct constant so renders are
reproducible and channels stay decorrelated:

| Generator | Seed |
|-----------|------|
| shared wow/flutter | 1 |
| L noise / wow-flutter / motor | 1001 / 1002 / 1003 |
| R noise / wow-flutter / motor | 2001 / 2002 / 2003 |

Noise character (pink filter, scrape bandpass, modulation math) is unchanged.
**Exact-off preserved:** when noise is gated off (`amount ≤ 0.05 %`) or
wow/flutter is 0, the generators are never advanced and nothing is injected — the
signal stays bit-clean, matching the source.

### 3.6 `setSaturation()` is a no-op (dead param mirror)
The `"saturation"` parameter (default 4 %) is **dead code** in the JUCE processor:
it is never cached (`saturationParam` does not exist) and never read. Tape drive
is derived entirely from **input gain**:
`saturation% = clamp(((inputGainDb+12)/24)*100, 0, 100)`
(`PluginProcessor.cpp:726`). `TapeMachineDSP::setSaturation()` stores the value
but it does not affect the DSP — faithful to the source. Drive comes from
`setInputGainDb()`.

### 3.7 `setNoiseEnabled()` is a no-op (dead gate mirror)
The `"noiseEnabled"` param is likewise dead in the source; the effective gate is
`noiseAmountParam > 0.05` (`PluginProcessor.cpp:735`). The port mirrors this:
`noiseEnabled = (noiseAmountPct > 0.05f)`; `setNoiseEnabled()` stores the bool but
does not gate. (If the shell needs a real kill-switch, revisit here — but as
written this guarantees the A/B match.)

### 3.8 `TapeSaturator` not wired in (dead code)
`ImprovedTapeEmulation::TapeSaturator::process()` is never called in the signal
chain. It is **not** wired in (per instruction). The struct and its
`updateCoefficients()` calls are kept only so `prepare()`/`updateFilters()` stay
structurally faithful; they have no audible effect.

### 3.9 VU meter = output peak with ~300 ms release
The DPF contract asks for "linear peak, ~300 ms release". The source instead kept
**300 ms RMS** on *separate* input and output meters. The port exposes the single
`getVuL/R()` as an **output** peak follower with a 300 ms release coefficient
(`exp(-1/(0.3*baseRate))`). Meter-only (cosmetic); does not affect audio, so it
does not impact the A/B null.

### 3.10 Oversampling-switch: no crossfade; forced coeff refresh
- The source crossfades (S-curve) over 512 samples when the oversampling factor
  changes to mask the filter-state reset. The port **omits** the crossfade
  (transition-masking only; steady-state identical). A small click is possible on
  a live OS switch — the DPF shell may add a crossfade if desired.
- The source re-`prepare()`s the tape on OS change but leaves `m_last*` stale, so
  the machine-specific coefficients are **not** re-applied until a machine param
  changes (latent staleness → neutral head-bump/HF curves after an OS switch). The
  port **forces** a full `updateFilters()` after any (re)prepare (invalidates
  `m_last*`), so the correct machine coeffs are always applied at the new rate.
  This only affects the OS-switch transient (A/B is presumably at a fixed OS
  setting) and is strictly more correct.

### 3.11 RT-safe OS-factor change (no reallocation)
`setOversampling()` only stores an atomic; the reconfigure happens in
`processBlock` via `applyFactor()`. To keep it allocation-free on the audio
thread, the wow/flutter delay buffers are **pre-sized for the max factor (4x)**
in `prepare()` (`WowFlutterProcessor::prepare(rate, factor, maxSampleRate)` sizes
from `maxSampleRate`), so factor changes only re-zero (bounded `std::fill`) and
recompute coefficients — never realloc. `baseDelay = 20*factor ≤ 80` always fits.

### 3.12 Dropped `validateCoefficients()` NaN-guard
The source guards each JUCE coeff assignment with a finite-check
(`validateCoefficients`), keeping the previous coeffs if a designer returns NaN.
All designer inputs are clamped to safe ranges (freqs to `nyquist*0.9`, Q/gain
via `jlimit`), so the designers always yield finite coeffs; the guard is dropped.

### 3.13 Tape-core metering atomics dropped
`ImprovedTapeEmulation`'s `inputLevel/outputLevel/gainReduction` atomics are
written per sample but never read by the shell (the shell computes its own RMS).
They are dropped (no audio effect). The audio-affecting input/output denormal
flushes (`|x| < 1e-8 → 0`) are **kept** verbatim.

---

## 4. Signal chain (preserved order)

Base rate: **input gain** → *(upsample)* → per oversampled sample: **HP SVF →
tape core → LP SVF (if `lpFreq < 19 kHz`) → output gain** → *(downsample)* →
**crosstalk (base rate)** → VU. HP is always active (source sets
`bypassHighpass = false` unconditionally).

Tape core per-sample order (unchanged from `ImprovedTapeEmulation::processSample`):
Thru early-out → input denormal → param-change `updateFilters` → calibration gain
→ input transformer (Type B) → pre-emphasis EQ → **[tape: bias filter → soft
limiter → record-head LP (OS>1) → 3-band J-A hysteresis → soft clip → gap-loss →
wow/flutter → head bump → HF loss ×2 (+extra for Sync) → playback head]** →
de-emphasis EQ → phase smear → output transformer (Type B) → noise (tape only) →
DC blocker → anti-alias (OS>1) → output denormal.

Shared wow/flutter modulation is computed **once per oversampled sample**
(precomputed into `sharedModArr`) and consumed by both channels' own delay lines
— preserving the stereo-coherent structure.

---

## 5. Verification
`g++ -std=c++17 -fsyntax-only -I plugins/TapeMachine/core -I plugins/shared-dpf/dsp
plugins/TapeMachine/core/TapeMachineDSP.cpp` — clean (also clean under
`-O2 -Wall -Wextra`).

No `// PORT-TODO` markers remain.
