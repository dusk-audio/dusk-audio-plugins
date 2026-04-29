#pragma once

#include "AlgorithmConfig.h"
#include "DattorroTank.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "SixAPTankEngine.h"
#include "NonLinearEngine.h"
#include "QuadTank.h"
#include "ShimmerEngine.h"
#include "SpringEngine.h"

#include <algorithm>
#include <cmath>
#include <vector>

// One-pole exponential smoother for per-sample parameter interpolation.
struct OnePoleSmoother
{
    float current = 0.0f;
    float target = 0.0f;
    float coeff = 0.0f;

    void reset (float value) { current = target = value; }

    void setSmoothingTime (double sampleRate, float timeMs)
    {
        coeff = std::exp (-1000.0f / (std::max (timeMs, 0.1f)
                                      * static_cast<float> (sampleRate)));
    }

    void setTarget (float t) { target = t; }

    float next()
    {
        current = target + coeff * (current - target);
        return current;
    }

    // Advance the smoother by `n` samples in O(1) using coeff^n. Used at the
    // top of each process block for parameters whose downstream consumers
    // (filter coefficients, tank delay lengths) are too expensive to
    // recompute per-sample.
    float skip (int n)
    {
        if (n <= 0) return current;
        float multiplier = std::pow (coeff, static_cast<float> (n));
        current = target + multiplier * (current - target);
        return current;
    }
};

// DuskVerb Shell engine.
//
// Routing (every algorithm):
//
//   in (L,R)
//     ↓ pre-delay
//     ├──► EarlyReflections ── (er_size, er_level)
//     │
//     └──► [DiffusionStage] (engines: Dattorro / QuadTank / FDN)
//           ↓
//          selected late tank (Dattorro / 6-AP / QuadTank / FDN / Spring / NonLinear / Shimmer)
//           ↓
//          erL+lateL, erR+lateR
//           ↓
//          Lo Cut → Hi Cut → Width → Mix (equal-power) → Gain Trim
//           ↓
//          out (L, R)
//
// All four sub-engines are owned (never allocated on the audio thread). The
// `algorithm` parameter selects which one consumes the post-predelay signal;
// the others sleep but stay prepared so a switch only takes a buffer-clear
// + setAlgorithm pointer flip.
class DuskVerbEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);

    // Discrete (non-smoothed) controls.
    void setAlgorithm (int index);
    void setFreeze (bool frozen);
    // Engine-specific: only the NonLinear engine has a gate. For other
    // engines this is a no-op (the call is forwarded but ignored).
    void setNonLinearGateEnabled (bool enabled);

    // Tank parameters — propagated to whichever engine is currently active.
    void setDecayTime     (float seconds);
    void setSize          (float size);
    void setBassMultiply  (float mult);
    void setMidMultiply   (float mult);            // 3-band mid multiplier
    void setTrebleMultiply(float mult);
    void setCrossoverFreq (float hz);              // bass↔mid (legacy "crossover")
    void setHighCrossoverFreq (float hz);          // mid↔high (3-band)
    void setSaturation    (float amount);          // 0..1 drive-style softClip
    void setModDepth      (float depth);
    void setModRate       (float hz);
    void setDiffusion     (float amount);

    // Early reflections.
    void setERLevel (float level);
    void setERSize  (float size);

    // Shell parameters (smoothed in process()).
    void setPreDelay  (float milliseconds);
    void setMix       (float dryWet);
    void setLoCut     (float hz);
    void setHiCut     (float hz);
    void setWidth     (float width);
    void setGainTrim  (float dB);
    void setMonoBelow (float hz);             // 20 = bypass; up = sums lows to mono

    // Per-preset SixAPTank brightness/density tunables. Forwarded directly to
    // sixAPTank_ regardless of currentEngine_ — they're only audible when the
    // SixAPTank is the active engine, but pre-applying them at preset-load
    // time is harmless (and necessary so the values are in place before the
    // engine starts processing).
    void setSixAPDensityBaseline (float v);
    void setSixAPBloomCeiling    (float v);
    void setSixAPBloomStagger    (const float values[6]);
    void setSixAPEarlyMix        (float v);
    void setSixAPOutputTrim      (float v);

private:
    // Engines (all owned; only one runs at a time).
    DattorroTank       dattorro_;
    SixAPTankEngine  sixAPTank_;
    QuadTank           quad_;
    FDNReverb          fdn_;
    SpringEngine       spring_;
    NonLinearEngine    nonLinear_;
    ShimmerEngine      shimmer_;

    // Pre-tank input diffuser, applied to every engine. Smears transients
    // before they hit the tank so onsets bloom into the tail rather than
    // arriving as discrete clicks.
    DiffusionStage diffuser_;
    EarlyReflections er_;

    EngineType currentEngine_ = EngineType::Dattorro;
    int currentAlgorithm_ = 0;

    // Pre-delay ring buffer.
    std::vector<float> preDelayBufL_;
    std::vector<float> preDelayBufR_;
    int preDelayWritePos_ = 0;
    int preDelayMask_ = 0;
    int preDelaySamples_ = 0;

    // Scratch buffers (sized by prepare()).
    std::vector<float> tankInL_, tankInR_;
    std::vector<float> tankOutL_, tankOutR_;
    std::vector<float> erOutL_, erOutR_;

    // Per-sample smoothed shell parameters (consumed inside the per-sample loop).
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother erLevelSmoother_;
    OnePoleSmoother gainTrimSmoother_;

    // Per-BLOCK smoothed parameters (advanced once per process() call). These
    // drive expensive recomputes (filter biquad coeffs / tank delay lengths)
    // that would cost too much per-sample, so we cap their evolution at the
    // block boundary instead.
    OnePoleSmoother sizeSmoother_;
    OnePoleSmoother loCutSmoother_;
    OnePoleSmoother hiCutSmoother_;
    OnePoleSmoother monoBelowSmoother_;
    float lastAppliedSize_      = -1.0f;
    float lastAppliedLoCut_     = -1.0f;
    float lastAppliedHiCut_     = -1.0f;
    float lastAppliedMonoHz_    = -1.0f;

    // Mono Maker — 1st-order matched-phase complementary split. Below the
    // cutoff, L+R are summed to mono; above stays stereo. Magnitude-flat
    // because we use input − lowpass for the high band (only 1st-order
    // satisfies perfect reconstruction).
    bool  monoMakerEnabled_ = false;
    float monoLPCoeff_      = 0.0f;
    float monoLPStateL_     = 0.0f;
    float monoLPStateR_     = 0.0f;

    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 0;

    bool   frozen_ = false;

    void pushSizeToTanks (float size);  // helper — internal use only

    // Output IIR filters (Butterworth biquad cookbook).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;
        float z1L = 0, z2L = 0;
        float z1R = 0, z2R = 0;

        float processL (float x)
        {
            float y = b0 * x + z1L;
            z1L = b1 * x - a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        float processR (float x)
        {
            float y = b0 * x + z1R;
            z1R = b1 * x - a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1L = z2L = z1R = z2R = 0.0f; }
    };

    Biquad loCutFilter_;
    Biquad hiCutFilter_;

    void updateLoCutCoeffs (float hz);
    void updateHiCutCoeffs (float hz);
};
