#pragma once

#include "DspUtils.h"

#include <array>
#include <cmath>
#include <vector>

// ShimmerEngine — 8-channel Hadamard FDN with an in-loop granular pitch
// shifter. Modelled on the Eno/Lanois "Shimmer" technique (Brian Eno +
// Daniel Lanois on U2's "The Unforgettable Fire", 1984; later pioneered as
// a built-in plug-in by Eventide ShimmerVerb).
//
// Signal flow per sample:
//   1. 8 delay lines read their stored values → x[0..7]
//   2. Hadamard 8×8 orthogonal mixing → y[0..7]
//   3. Output (stereo): even-indexed delays sum to L, odd to R
//   4. Two granular pitch shifters (one per side) shift the post-mix
//      aggregate signal up by `pitchSemitones` (0–24 = 0 to +2 octaves)
//   5. Write back to each delay: input + decay × ((1−mix)·y[i] + mix·pitched)
//      Each pass through the loop pitches the feedback up another step,
//      so over time the signal "cascades" upward — the canonical Eno/
//      Lanois shimmer character.
//
// Knob hijacking (engine sees the universal setters but reinterprets
// them under engine-specific UI labels):
//   setDecayTime      → FDN feedback gain (RT60-based, standard)
//   setSize           → delay-line scale (standard)
//   setTrebleMultiply → in-loop HF damping (standard)
//   setBassMultiply   → output low-shelf
//   setModDepth       → "PITCH" knob: pitch interval
//                          0.0 = 0 semitones, 0.5 = +12, 1.0 = +24
//   setModRate        → "MIX" knob: shimmer-mix blend
//                          0.1 Hz = ~0% pitched, 10 Hz = 100% pitched
//   setTankDiffusion  → no-op (FDN's mixing IS the diffusion)
//   setSaturation     → input drive softClip
class ShimmerEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);
    void clearBuffers();

    // Universal setters — same surface as every other late-tank engine.
    void setDecayTime         (float seconds);
    void setSize              (float size);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation        (float amount);
    void setModDepth          (float depth);   // hijacked: PITCH (0..1 → 0..24 semitones)
    void setModRate           (float hz);      // hijacked: MIX  (0.1..10 → 0..1 shimmer mix)
    void setTankDiffusion     (float amount);  // no-op
    void setFreeze            (bool frozen);

private:
    static constexpr int kNumChannels = 8;

    // Mutually-prime delays at 44.1k base: 23, 31, 43, 53, 67, 79, 89, 101 ms.
    // Selected for FDN balance — long enough that pitched feedback gets
    // multiple cycles to develop the shimmer cascade, short enough that the
    // initial pitch-up sound arrives within ~80 ms of the dry transient.
    static constexpr int kBaseDelays[kNumChannels] = {
        1015, 1367, 1897, 2337, 2955, 3485, 3925, 4455
    };

    // 8×8 Hadamard mixing matrix (sequency-ordered, ±1 entries scaled by
    // 1/√8 for energy preservation). Each row is orthogonal to every other
    // row, so the matrix decorrelates the 8 channels per sample without
    // changing total RMS.
    static constexpr float kHadamardScale = 0.35355339059327373f;   // 1/√8

    // ──────────────────────────────────────────────────────────────────
    // GranularPitchShifter — overlapping-grain pitch shift via crossfaded
    // dual-grain reads. 4096-sample grains, 50% overlap, Hann window.
    //
    // Per-sample algorithm:
    //   • Write input to circular buffer
    //   • Read TWO grain voices at offset positions (50% phase apart)
    //   • Each voice's read position advances by `pitchRatio` per sample
    //     (1.0 = unity pitch, 2.0 = +1 octave, 4.0 = +2 octaves)
    //   • Apply Hann window per voice based on grain phase
    //   • Sum windowed reads = continuously pitch-shifted output
    //   • When a voice's grain phase reaches kGrainSize, reset to a fresh
    //     read position computed to keep the grain inside the buffered
    //     history (lookback = (pitchRatio-1) × kGrainSize + headroom)
    // ──────────────────────────────────────────────────────────────────
    class GranularPitchShifter
    {
    public:
        static constexpr int kGrainSize = 4096;

        void prepare();
        void clear();
        void setPitchRatio (float ratio);
        float process (float input);

    private:
        std::vector<float> buffer_;
        int   mask_     = 0;
        int   writePos_ = 0;
        int   phase1_   = 0;
        int   phase2_   = kGrainSize / 2;          // 50 % offset → continuous coverage
        float readPos1_ = 0.0f;
        float readPos2_ = static_cast<float> (kGrainSize) * 0.5f;
        float pitchRatio_ = 1.0f;

        void startNewGrain (int& phase, float& readPos);
        float readLinear (float pos) const;
    };

    // ──────────────────────────────────────────────────────────────────
    // FDN delay lines
    // ──────────────────────────────────────────────────────────────────
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos     = 0;
        int mask         = 0;
        int delaySamples = 0;

        void allocate (int maxSamples);
        void clear();
        // Read with sub-sample LFO offset for the in-loop modulation that
        // gives shimmer its characteristic decorrelated wash. lfoOffset is
        // a signed sample count; we linear-interpolate between the two
        // adjacent integer positions for sub-sample precision.
        inline float read (float lfoOffset) const noexcept
        {
            const float readPos = static_cast<float> (writePos)
                                - static_cast<float> (delaySamples)
                                - lfoOffset;
            const int   intIdx  = static_cast<int> (std::floor (readPos));
            const float frac    = readPos - static_cast<float> (intIdx);
            const int   idx0    = intIdx & mask;
            const int   idx1    = (intIdx + 1) & mask;
            return buffer[static_cast<size_t> (idx0)] * (1.0f - frac)
                 + buffer[static_cast<size_t> (idx1)] *         frac;
        }
        inline void write (float x) noexcept
        {
            buffer[static_cast<size_t> (writePos)] = x;
            writePos = (writePos + 1) & mask;
        }
    };

    std::array<DelayLine, kNumChannels> delays_;

    // Two pitch shifters — one for the L-side mixed feedback bus, one for
    // R-side. Cheaper than 8 (one per delay) and gives natural stereo split.
    GranularPitchShifter pitchL_, pitchR_;

    double sampleRate_      = 44100.0;
    float  decayTime_       = 4.0f;
    float  sizeParam_       = 0.5f;
    float  feedbackGain_    = 0.94f;     // bumped 0.85 → 0.94 to give the cascade room to develop (RT60 ~6.5 s)
    float  trebleMult_      = 1.0f;
    float  bassMult_        = 1.0f;
    float  midMult_         = 1.0f;
    float  crossoverHz_     = 1000.0f;
    float  highCrossoverHz_ = 4000.0f;
    float  saturationAmount_= 0.0f;
    float  pitchSemitones_  = 12.0f;     // hijacked from mod_depth (0..24)
    float  shimmerMix_      = 0.5f;      // hijacked from mod_rate (0..1)
    bool   frozen_          = false;
    bool   prepared_        = false;

    // ── In-loop random-walk LFOs (one per FDN delay channel) ──────────
    // CRITICAL for shimmer character (per Valhalla DSP's writeup): without
    // delay-line modulation, the granular pitch shifter finds clean splice
    // points and the cascade stays too coherent (or, with damping, dies
    // entirely). Modulation forces sub-optimal grain splicing → spreads
    // sidebands → produces the characteristic Eno wash. Each LFO rate is
    // distinct (0.20–0.70 Hz) so they never beat against each other.
    std::array<DspUtils::RandomWalkLFO, kNumChannels> lfos_;

    // Output-side HF damping (1-pole LP per L/R output). Moved OUT of the
    // feedback path on 2026-04-26 — applying damping inside the loop was
    // killing the pitched feedback above the cutoff before it could stack
    // (each octave-up shift moved content into the damped band, the LP
    // ate it on the next iteration → cascade never developed). Output
    // damping shapes the wet sound the user hears without touching the
    // feedback's full bandwidth.
    float dampCoeff_     = 0.5f;
    float dampStateOutL_ = 0.0f;
    float dampStateOutR_ = 0.0f;

    void updateFeedback();
    void updateDelays();
    void updateDamping();
    void updatePitchRatio();
};
