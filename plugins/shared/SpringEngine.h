#pragma once

#include "DspUtils.h"

#include <vector>

// SpringEngine — physical-spring-tank emulation modelled on the Fender 6G15
// reverb unit (1961-65) and its descendants. Three parallel "spring" lines
// per channel; each spring is a delay line with a 24-stage cascaded
// 1st-order all-pass dispersion network at its input and a 1-pole HF
// damping LP at its output, plus a small read-position LFO for the
// characteristic ambient micro-vibration ("drip").
//
// Physical model:
//   • Dispersion: high frequencies travel slower than low frequencies along
//     a transverse-wave spring. The classic "boing" of a tapped spring is
//     the impulse dispersing into a chirp. We synthesize this with cascaded
//     1st-order APs (Välimäki/Parker 2010) — coefficient `a` near −1 gives
//     a quadratic-ish group-delay curve over the audio band.
//   • Multiple springs: the 6G15 used three mechanical springs of different
//     lengths in parallel; their summed output produces the characteristic
//     dense-but-uneven shimmer. We use mutually-prime delay lengths per
//     spring for natural decorrelation.
//   • HF damping: real springs roll off above ~4-5 kHz; modelled with a
//     1-pole LP per spring controlled by the trebleMultiply parameter.
//
// Knob hijacking (per the v3.0 plan — engine sees the standard setters but
// reinterprets them under engine-specific UI labels):
//   setSize           → spring delay multiplier (0.5–1.5×)
//   setDecayTime      → spring feedback gain (per-spring computed from RT60)
//   setTrebleMultiply → spring HF damping LP cutoff
//   setBassMultiply   → output low-shelf gain (post-spring-sum)
//   setModDepth       → "SPRING LEN" knob: read-position LFO depth (samples)
//   setModRate        → "DRIP" knob: LFO rate in Hz (0.1–10)
//   setTankDiffusion  → "CHIRP" knob: dispersion coefficient |a| magnitude
class SpringEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);
    void clearBuffers();

    // Universal setters — every late-tank engine in DuskVerb implements this
    // surface so DuskVerbEngine's setter fan-out can dispatch unconditionally.
    void setDecayTime         (float seconds);
    void setSize              (float size);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);   // not used by SpringEngine; stored for API parity
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);     // not used by SpringEngine; stored for API parity
    void setHighCrossoverFreq (float hz);     // not used by SpringEngine; stored for API parity
    void setSaturation        (float amount);
    void setModDepth          (float depth);
    void setModRate           (float hz);
    void setTankDiffusion     (float amount); // hijacked: chirp depth
    void setFreeze            (bool frozen);

private:
    static constexpr int kNumSprings          = 3;
    static constexpr int kNumDispersionStages = 24;

    // 1st-order all-pass: y[n] = -a·x[n] + x[n-1] + a·y[n-1].
    // |a| < 1 → magnitude = 1 (true all-pass), phase varies with frequency.
    // Cascading many of these builds a frequency-dependent group delay
    // curve — the chirp that gives a tapped spring its boing character.
    struct AllPass1
    {
        float prevX = 0.0f;
        float prevY = 0.0f;

        inline float process (float x, float a) noexcept
        {
            const float y = -a * x + prevX + a * prevY;
            prevX = x;
            prevY = y;
            return y;
        }

        void clear() { prevX = prevY = 0.0f; }
    };

    struct Spring
    {
        std::vector<float> delayBuf;
        int   writePos     = 0;
        int   mask         = 0;
        int   delaySamples = 0;
        float feedback     = 0.5f;

        AllPass1 dispersionAPs[kNumDispersionStages];
        float    dispersionA = -0.7f;     // base dispersion coefficient

        float dampState = 0.0f;           // 1-pole LP for HF roll-off
        float dampCoeff = 0.5f;           // = exp(-2π·fc/sr)

        void allocate (int maxSamples);
        void clear();
        // process() is per-sample. lfoOffset is in samples (signed) and
        // shifts the read head by ±lfoOffset for the "drip" wobble.
        float process (float input, float lfoOffset) noexcept;
    };

    // Per-channel spring banks. L vs R use slightly different prime delay
    // sets so the sum decorrelates naturally without explicit cross-feed.
    Spring leftSprings_[kNumSprings];
    Spring rightSprings_[kNumSprings];

    // Mutually-prime base delays at 44.1k: 50, 110, 170 ms (left)
    //                                       53, 113, 179 ms (right)
    static constexpr int kLeftBaseDelays [kNumSprings] = { 2207, 4853, 7499 };
    static constexpr int kRightBaseDelays[kNumSprings] = { 2339, 4987, 7901 };

    // Read-position LFO per channel — random-walk for non-periodic wobble
    // (matches the "spin and wander" treatment we use elsewhere).
    DspUtils::RandomWalkLFO lfoL_, lfoR_;

    double sampleRate_ = 44100.0;

    // User-facing parameter cache.
    float decayTime_       = 1.5f;
    float sizeParam_       = 0.5f;
    float trebleMult_      = 1.0f;
    float bassMult_        = 1.0f;
    float midMult_         = 1.0f;
    float crossoverHz_     = 1000.0f;
    float highCrossoverHz_ = 4000.0f;
    float saturationAmount_= 0.0f;
    float modDepthRaw_     = 0.25f;   // → LFO depth in samples (0–6)
    float modRateRaw_      = 1.0f;    // → LFO rate in Hz
    float chirpAmount_     = 0.5f;    // → dispersion coefficient magnitude
    bool  frozen_          = false;
    bool  prepared_        = false;

    void updateSpringLengths();   // size knob → per-spring delaySamples
    void updateFeedback();        // decayTime → per-spring feedback gain
    void updateDamping();         // trebleMult → per-spring dampCoeff
    void updateDispersion();      // chirpAmount → per-spring dispersionA
    void updateLFO();             // modRate → LFO Hz, depth handled per-sample
};
