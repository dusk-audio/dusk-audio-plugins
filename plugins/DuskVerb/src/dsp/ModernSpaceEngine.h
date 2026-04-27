#pragma once

#include "DspUtils.h"
#include "ParallelDiffuser.h"
#include "TwoBandDamping.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// "Modern Space" 6-AP cross-coupled tank reverb.
//
// Topology: two cross-coupled feedback loops (figure-8). Each loop runs
// modulated allpass → delay → 6-allpass density cascade → two-band damping
// → static allpass → delay. The 6-AP density cascade (twice the depth of
// classic Dattorro's 3-AP cascade) produces a denser, smoother modal
// distribution suited to lush halls, ambient swells and dark rooms.
//
// Mutually-prime delay lengths across both tanks + density cascades break
// modal phase-locking, so the late tail rings as continuous wash rather
// than discrete echoes.
class ModernSpaceEngine
{
public:
    ModernSpaceEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setMidMultiply (float mult);          // 3-band damping mid multiplier
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);          // bass↔mid split (legacy name)
    void setHighCrossoverFreq (float hz);      // 3-band damping mid↔high split
    void setSaturation (float amount);         // 0..1 drive softClip
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setFreeze (bool frozen);
    // User-facing tank density. amount is the DIFFUSION knob value [0, 1].
    // Scales the 6-AP density-cascade coefficient around its baseline.
    void setTankDiffusion (float amount);
    void clearBuffers();

private:
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kSafetyClip = 4.0f;
    static constexpr int kNumDensityAPs = 6;

    // Phi-spaced (13/8) main delays so mode families align only every 8-13
    // cycles (closest Fibonacci convergent of φ).
    static constexpr int kLeftAP1Base  = 401;
    static constexpr int kLeftDel1Base = 2999;
    static constexpr int kLeftAP2Base  = 953;
    static constexpr int kLeftDel2Base = 1847;

    static constexpr int kRightAP1Base  = 509;
    static constexpr int kRightDel1Base = 2711;
    static constexpr int kRightAP2Base  = 1289;
    static constexpr int kRightDel2Base = 1669;

    // 6-AP density cascade primes — coprime to all main delays.
    //
    // SUPER-SIZED on 2026-04-26 (~3.75× the original lengths):
    //   Old left  sum = 1928 samples = 44 ms — far shorter than the
    //   ~170 ms figure-8 loop period at hall sizes, leaving 130 ms gaps
    //   between recirculations that the listener hears as discrete delay
    //   pulses (verified: short-lag autocorrelation strength = 0.90 at
    //   ~8 ms lag).
    //   New left  sum = 7274 samples = 165 ms at 44.1 k base — wider than
    //   the loop period at every size setting, so the cascade smear OVERLAPS
    //   ITSELF on each circulation. Eliminates the rhythmic-pulse artefact
    //   without touching the loop topology.
    //   Right channel sum = 7592 samples = 172 ms (same architecture, primes
    //   distinct from left to keep L/R decorrelated).
    //
    // All 12 values are prime, all 12 are mutually distinct, none coincide
    // with any main-delay base value.
    static constexpr int kLeftDensityAPBase[kNumDensityAPs]  = { 523,  751, 1019, 1381, 1627, 1973 };
    static constexpr int kRightDensityAPBase[kNumDensityAPs] = { 563,  797, 1063, 1423, 1693, 2053 };

    // Worst-case base delay (hall-scale × max sizeRange) — sized for 88.2k
    // and large size knob, see prepare().
    static constexpr int kMaxBaseDelay = 12000;

    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;

        void allocate (int maxSamples);
        void clear();

        void write (float sample)
        {
            buffer[static_cast<size_t> (writePos)] = sample;
            writePos = (writePos + 1) & mask;
        }

        float read (int delaySamples) const
        {
            return buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
        }

        float readInterpolated (float delaySamples) const;
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
        int delaySamples = 0;
        // Lexicon-style "spin and wander". The jitter on the read position
        // breaks the perfect periodicity of the cascaded allpasses that
        // otherwise leak each AP's delay period into the tail as an audible
        // discrete echo (verified by render-tool autocorrelation).
        //
        // CRITICAL: per-sample uniform xorshift jitter would inject hash
        // noise into the audio. We use RandomWalkLFO which picks a new
        // random target every period and smoothstep-interpolates to it —
        // gives smooth wander with no high-frequency content.
        //
        // Depth scales with delay (jitterDepthFraction × delaySamples) so
        // the *relative* modulation stays constant across the size knob's
        // range. Without this, ±1 sample on a 100-sample AP works fine
        // but is invisible on an 800-sample AP at large size — letting
        // the long delays ring audibly at hall sizes.
        //
        // Default fraction = 0 = static AP (back-compat for ap2 etc.).
        DspUtils::RandomWalkLFO jitterLFO;
        float                   jitterDepthFraction = 0.0f;

        void allocate (int maxSamples);
        void clear();

        // Push the latest delay length into the LFO. Call after delaySamples
        // changes (e.g., from updateDelayLengths via setSize).
        //
        // Both depth AND rate scale with delay:
        //   • Depth = 1.5 % of delaySamples — the relative jitter stays
        //     constant across all sizes.
        //   • Rate is set so the LFO period equals ~2 × delaySamples.
        //     This guarantees the jitter transitions a meaningful fraction
        //     of its range within one AP ring period, breaking modal
        //     phase-locking even on the shortest APs (otherwise a 7 Hz
        //     LFO completes only a tiny fraction of a transition within
        //     a 7 ms ring → ring stays coherent → audible echo).
        void updateJitterDepth (float sampleRate)
        {
            if (jitterDepthFraction <= 0.0f || delaySamples <= 0)
                return;
            jitterLFO.setDepth (static_cast<float> (delaySamples) * jitterDepthFraction);
            const float period = 2.0f * static_cast<float> (delaySamples);
            const float lfoRateHz = sampleRate / period;
            // Clamp to a musically sensible range. Too slow (< 5 Hz) doesn't
            // break short rings; too fast (> 200 Hz) becomes audible flutter.
            const float clamped = std::min (std::max (lfoRateHz, 5.0f), 200.0f);
            jitterLFO.setRate (clamped);
        }

        float process (float input, float g)
        {
            float vd;
            if (jitterDepthFraction > 0.0f)
            {
                const float jitter  = jitterLFO.next();
                const float readPos = static_cast<float> (writePos)
                                    - static_cast<float> (delaySamples)
                                    - jitter;
                int   intIdx = static_cast<int> (std::floor (readPos));
                const float frac = readPos - static_cast<float> (intIdx);
                intIdx = static_cast<int> (static_cast<unsigned int> (intIdx)
                                            & static_cast<unsigned int> (mask));
                vd = DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
            }
            else
            {
                vd = buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
            }
            const float vn = input + g * vd;
            buffer[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
    };

    struct Tank
    {
        DelayLine ap1Buffer;
        int ap1BaseDelay = 0;
        float ap1DelaySamples = 0.0f;

        DelayLine delay1;
        int delay1BaseDelay = 0;
        float delay1Samples = 0;

        Allpass densityAP[kNumDensityAPs];
        int densityAPBase[kNumDensityAPs] = {};

        ThreeBandDamping damping;

        Allpass ap2;
        int ap2BaseDelay = 0;

        DelayLine delay2;
        int delay2BaseDelay = 0;
        float delay2Samples = 0;

        float crossFeedState = 0.0f;

        // Random-walk LFO replaces the old sine modulator. Sine LFOs at low
        // rates create a perceptible periodic warble when the period is close
        // to the tank's natural modal beat frequency; smoothed-noise wander
        // breaks that beat and produces the "expensive" shimmer of high-end
        // hardware random reverbs. This LFO carries the USER's mod_depth knob
        // for chorus character.
        DspUtils::RandomWalkLFO lfo;
    };

    Tank leftTank_;
    Tank rightTank_;

    // Output is extracted as a simple per-sample read of `damped` (the
    // signal immediately AFTER the 6-AP density cascade and damping
    // biquad, BEFORE AP2/delay2/cross-feedback). All earlier multi-tap
    // strategies were abandoned because reading any buffer at sparse
    // discrete intervals produced an audible macro-flutter on transients.
    //
    // The early-period density problem (no signal at output until the
    // first cascade traversal completes ~98 ms after onset) is now
    // handled at the INPUT side by the ParallelDiffuser member, which
    // shatters transients into ~40 dense reflections in the first 60 ms
    // BEFORE they enter the tank. By the time the tank's density cascade
    // smears that already-dense input, `damped` is continuous from the
    // first sample after the cascade.
    //
    // (No tap arrays, no readOutputTap, no kNorm. The output is just
    //  outputL[n] = damped, outputR[n] = dampedR. Period.)

    static float nextDrift (uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
    }

    double sampleRate_ = 44100.0;
    float decayTime_ = 2.0f;
    float bassMultiply_ = 1.0f;
    float midMultiply_ = 1.0f;            // 3-band mid multiplier (1.0 = no mid colour)
    float trebleMultiply_ = 0.5f;
    float crossoverFreq_ = 1000.0f;       // bass↔mid (legacy "crossover")
    float highCrossoverFreq_ = 4000.0f;   // mid↔high (new for 3-band)
    float saturationAmount_ = 0.0f;       // 0..1 drive softClip (NEW)
    float modDepthSamples_ = 8.0f;
    float lastModDepthRaw_ = 0.5f;
    float modRateHz_ = 1.0f;
    float sizeParam_ = 0.5f;

    // Decay diffusion (pre/post density cascade) and density-cascade feedback.
    // densityDiffCoeff baseline was 0.18 — far too low to act as proper
    // diffusion; each AP just rang at its delay period (3.6-13.9 ms across
    // the 6-AP cascade at hall sizes), producing audible discrete repetitions
    // in the tail. 0.55 matches Lexicon hall density-AP convention and
    // smears the signal across the cascade rather than ringing per-stage.
    // Loop stability is guaranteed by the 0.98 band-gain ceiling in
    // updateDecayCoefficients(). setTankDiffusion() scales around this.
    float decayDiff1_ = 0.70f;
    float decayDiff2_ = 0.50f;
    // Bumped 0.55 → 0.62 on 2026-04-26 alongside the super-sized density
    // AP arrays. Slightly longer ring-time per stage further fills the
    // micro-gaps between cascade outputs. Per-stage AP coeff is still
    // hard-clamped to kBloomCoeffCeiling (0.85) in process(), so even at
    // max diffusion (1.2× scale) × max bloom multiplier (1.2×) the worst-
    // case stage coeff = 0.62 × 1.2 × 1.2 = 0.893 → clamped safe at 0.85.
    static constexpr float kDensityDiffBaseline_ = 0.62f;
    float densityDiffCoeff_ = kDensityDiffBaseline_;

    bool frozen_ = false;
    bool prepared_ = false;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();

    // Parallel-AP input shatterer applied before the tank entry. Replaces
    // the global series DiffusionStage (which is bypassed for 6-AP — see
    // DuskVerbEngine::process). 6-stage parallel sum with alternating ±,
    // mutually-prime delays, energy-preserving normalisation. Eliminates
    // the discrete-event perception by ensuring transients arrive at the
    // tank already broken into ≈40 dense reflections.
    ParallelDiffuser parallelDiffuser_;

    // Scratch buffers for the diffused input — sized in prepare() so the
    // audio thread never allocates. The caller's input pointers are const,
    // so we copy → diffuse-in-place → feed the tank from these.
    std::vector<float> diffusedL_;
    std::vector<float> diffusedR_;
};
