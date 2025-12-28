# Chow Tape Model Technical Analysis

## Executive Summary

This document analyzes the Chow Tape Model (AnalogTapeModel) by Jatin Chowdhury to understand his approach to tape hysteresis modeling and anti-aliasing. The goal is to inform improvements to our TapeMachine plugin.

**Sources:**
- [DAFx 2019 Paper](https://ccrma.stanford.edu/~jatin/420/tape/TapeModel_DAFx.pdf)
- [GitHub Repository](https://github.com/jatinchowdhury18/AnalogTapeModel)
- [Jatin's Research Page](https://ccrma.stanford.edu/~jatin/research/)

---

## 1. HYSTERESIS MODEL

### Which Model Does Chow Use?

Chow implements **two hysteresis models**:

#### A. Jiles-Atherton Model (Original, Numerical Solvers)
The original implementation uses the Jiles-Atherton differential equation model with multiple numerical solvers:

| Solver | Description | Accuracy | CPU Cost |
|--------|-------------|----------|----------|
| RK2 | 2nd-order Runge-Kutta | Low | Low |
| RK4 | 4th-order Runge-Kutta | Medium | Medium |
| NR4 | Newton-Raphson (4 iterations) | High | High |
| NR8 | Newton-Raphson (8 iterations) | Highest | Highest |

**Key Parameters:**
```cpp
struct HysteresisState {
    float M_s = 1.0f;        // Saturation magnetization
    float a;                  // M_s / 4.0 - field scaling
    float alpha = 1.6e-3f;    // Magnetic damping
    float k = 0.47875f;       // Material coefficient
    float c = 1.7e-1f;        // Reversibility
};
```

The model implements:
- **Langevin function** for magnetization curves
- **hysteresisFunc**: Computes dM/dt from magnetic field H
- **hysteresisFuncPrime**: Derivative for Newton-Raphson iteration

#### B. STN Model (State Transition Network)
A newer approach using neural networks to approximate the hysteresis:

**Architecture:**
```
Input: 5 dimensions
Dense(5 → 4) → Tanh → Dense(4 → 4) → Tanh → Dense(4 → 1)
Output: 1 dimension
```

**Key Innovation:**
- ~230 pre-trained neural networks (21 saturation levels × 11 width levels)
- Training data generated using NR8 solver at 96kHz
- Provides significant speedup over numerical solvers
- Uses RTNeural library for fast inference

**Why 230 Networks?**
The hysteresis has 3 parameters (Drive, Saturation, Width), but Drive can be handled as simple gain scaling. Saturation and Width fundamentally change the hysteresis curve shape, so separate networks are trained for each combination.

### How Does He Handle Aliasing?

**Multi-stage approach:**

1. **Oversampling BEFORE hysteresis** - The entire hysteresis process runs at elevated sample rate
2. **Input clipping** - Prevents extreme values that would cause numerical instability
3. **DC blockers AFTER downsampling** - Removes DC offset accumulated during processing

---

## 2. ANTI-ALIASING APPROACH

### Oversampling Configuration

Uses `chowdsp::VariableOversampling` (based on JUCE's `juce::dsp::Oversampling`):

| Factor | Multiplier | Typical Use |
|--------|------------|-------------|
| OneX | 1x | Bypass |
| TwoX | 2x | **Default** |
| FourX | 4x | High quality |
| EightX | 8x | Maximum quality |
| SixteenX | 16x | Offline rendering |

### Filter Types

| Mode | Characteristics |
|------|-----------------|
| MinPhase | Lower latency, some phase distortion |
| LinPhase | Zero phase distortion, higher latency |

### Critical Design Decisions

1. **Oversampling is ONLY around hysteresis** - Not the entire plugin
2. **Default is 2x oversampling with minimum-phase filters**
3. **Uses JUCE's built-in polyphase IIR design**

### Filter Placement

```
Input → [Upsample 2x-16x] → Hysteresis → [Downsample] → DC Blocker → Output
          ↑                                    ↑
     MinPhase/LinPhase              Anti-aliasing built into downsampler
```

---

## 3. SIGNAL FLOW

### Complete Processing Chain

```
1. INPUT STAGE
   └── Input Gain
   └── Input Filters (bandpass 20Hz-22kHz, user configurable)

2. CORE PROCESSING (with M/S support)
   └── Tone Control IN (pre-emphasis)
   └── Compression (with 1x polyphase IIR oversampling)
   └── HYSTERESIS (with 2x-16x oversampling) ← Main saturation
   └── Tone Control OUT (de-emphasis)
   └── Chew (tape degradation/dropout artifacts)
   └── Degrade (noise/bit reduction)
   └── Flutter (timing variations)
   └── Loss Filter (head gap loss, azimuth)

3. OUTPUT STAGE
   └── Input Filter Makeup (restored frequencies)
   └── Output Gain
   └── Dry/Wet Mix
```

### Which Processors Need Oversampling?

| Processor | Oversampling | Reason |
|-----------|--------------|--------|
| Hysteresis | **Yes (2x-16x)** | Nonlinear, creates harmonics |
| Compression | Yes (1x polyphase) | Subtle nonlinearity |
| Chew | No | Already bandlimited degradation |
| Flutter | No | Time-domain modulation only |
| Loss Filter | No | Linear filtering only |
| Tone Control | No | Linear filtering |

### Key Insight: Emphasis/De-emphasis Around Hysteresis

```cpp
// Pre-emphasis (before hysteresis):
toneIn.setLowGain(dbScale * bassParam);
toneIn.setHighGain(dbScale * trebleParam);

// De-emphasis (after hysteresis):
toneOut.setLowGain(-1.0f * dbScale * bassParam);  // Inverse!
toneOut.setHighGain(-1.0f * dbScale * trebleParam);
```

This allows the hysteresis coloration to be isolated and analyzed separately from the tone controls.

---

## 4. MACHINE CALIBRATION

### Tape Type Parameters

The model uses these core parameters for different "tape types":

| Parameter | Range | Effect |
|-----------|-------|--------|
| **Drive** | 0-1 | Input gain to hysteresis |
| **Saturation** | 0-1 | Controls M_s (saturation magnetization) |
| **Width** | 0-1 | Controls field scaling |
| **Bias** | 0-1 | AC bias amount |

### Saturation vs Character

**Saturation Amount** (Drive + Saturation parameter):
- Higher Drive = more signal into nonlinearity
- Higher Saturation = lower M_s = earlier saturation point

**Saturation Character** (Width parameter):
- Controls the "shape" of the hysteresis curve
- Affects the ratio of even to odd harmonics
- Narrower width = more aggressive, transistor-like
- Wider width = softer, tube-like

### Physical Constants (from measurements)

```cpp
// Ferric Oxide (γF2O3) tape properties:
Hc ≈ 27 kA/m  // Coercivity
alpha = 1.6e-3  // Magnetic damping
k = 0.47875     // Material coefficient (measured)
c = 0.17        // Reversibility (17%)
```

### Loss Effects Modeling

Frequency-domain model with three components:

```cpp
// 1. Spacing Loss (head-to-tape gap)
H[k] = exp(-waveNumber * spacing * 1e-6);

// 2. Thickness Loss (tape coating)
H[k] *= (1 - exp(-thickTimesK)) / thickTimesK;

// 3. Gap Loss (playhead gap width) - sinc response
H[k] *= sin(kGapOverTwo) / kGapOverTwo;
```

Plus a "head bump" resonant filter for midrange emphasis.

---

## 5. COMPARISON TO OUR TAPEMACHINE

### What We Do Similarly

| Feature | Chow | Our TapeMachine |
|---------|------|-----------------|
| Oversampling | 2x-16x (hysteresis only) | 4x (entire chain via JUCE) |
| Pre/De-emphasis | Yes (Tone Control) | Yes (NAB/CCIR curves) |
| Head bump | Yes (resonant filter) | Yes |
| Wow/Flutter | Yes (separate processor) | Yes (shared processor) |
| Loss filtering | Yes (complex FIR) | Yes (simpler IIR) |

### What Chow Does Differently (and Better)

1. **Targeted Oversampling**
   - Only oversamples the hysteresis processor
   - Other linear processors run at base sample rate
   - Lower CPU usage for same quality

2. **True Hysteresis Model**
   - Uses Jiles-Atherton differential equation
   - Or neural network approximation (STN)
   - Our approach: polynomial waveshaping + filtering

3. **STN (Neural Network) Option**
   - 230 pre-trained networks for different parameter combinations
   - Faster than numerical solvers
   - Trained on high-accuracy NR8 solver output

4. **Variable Oversampling with Mode Selection**
   - User can choose 2x, 4x, 8x, 16x
   - User can choose minimum-phase vs linear-phase
   - Our approach: fixed 4x with JUCE defaults

5. **Compression Before Hysteresis**
   - Mimics the natural compression behavior of tape
   - We do compression-like behavior in the saturator

### What We Do Differently

1. **Frequency-Selective Saturation (Anti-Aliasing)**
   - We split signal and only saturate LF content
   - HF passes through linearly
   - Chow relies purely on oversampling

2. **Record Head Filter Before Saturation**
   - 16th-order Butterworth at 15-18kHz
   - Mimics real tape head frequency response
   - Removes content that would generate aliasing harmonics

3. **Soft Limiter After Pre-Emphasis**
   - Catches extreme HF peaks before saturation
   - Prevents harmonic explosion at high input levels

---

## 6. RECOMMENDATIONS FOR OUR PLUGIN

### High Priority

1. **Consider Targeted Oversampling**
   - Only oversample the saturation stages
   - Run linear filters at base sample rate
   - Could reduce CPU by 50%+

2. **Implement Jiles-Atherton Hysteresis (Optional Mode)**
   - RK4 solver would be good balance of quality/CPU
   - Could offer as "Analog" mode alongside current approach

3. **Add Variable Oversampling Controls**
   - Let users choose 2x/4x/8x based on CPU budget
   - Default to 2x for most use cases

### Medium Priority

4. **Consider STN Neural Network Approach**
   - Would require training infrastructure
   - Could use RTNeural library (same as Chow)
   - Significant development effort

5. **Improve Loss Filter**
   - Add thickness loss and spacing loss
   - Use FIR for more accurate frequency response

### Low Priority (Nice to Have)

6. **Add Emphasis/De-emphasis Controls**
   - Let users adjust pre-emphasis curve
   - Useful for matching different tape standards

7. **Add Compression Before Saturation**
   - Natural tape compression behavior
   - Would need subtle program-dependent response

---

## 7. TECHNICAL REFERENCES

### Papers
- [Real-time Physical Modelling for Analog Tape Machines (DAFx 2019)](https://ccrma.stanford.edu/~jatin/420/tape/TapeModel_DAFx.pdf)
- Jiles, D. C., and Atherton, D. L. "Theory of ferromagnetic hysteresis"
- Native Instruments MS-20 filter paper (STN inspiration)

### Code
- [AnalogTapeModel GitHub](https://github.com/jatinchowdhury18/AnalogTapeModel)
- [chowdsp_utils Library](https://github.com/Chowdhury-DSP/chowdsp_utils)
- [RTNeural Library](https://github.com/jatinchowdhury18/RTNeural)

### Key Files to Study
- `HysteresisProcessor.cpp` - Main hysteresis with oversampling
- `HysteresisProcessing.cpp` - Core hysteresis algorithm
- `HysteresisSTN.cpp` - Neural network approach
- `HysteresisOps.h` - Mathematical operations
- `ToneControl.cpp` - Pre/de-emphasis
- `LossFilter.cpp` - Head gap loss modeling

---

*Document created: December 2025*
*For TapeMachine plugin development*
