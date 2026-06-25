#pragma once

#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// DiffusedEarlyReflections — a DISCRETE-but-SMOOTH early-reflection generator.
//
// Why this exists (clarity / "snare not masked" / two-taps, 2026-06-25): the VVV
// halls front a transient with a handful of DISCRETE early reflections (a comb at
// ~16/31/40/56/70/... ms) that stay DEFINED — the source cuts through, the ear
// hears distinct taps. DuskVerb's DenseHall tank is diffuse-from-zero (smears the
// transient → muddy, masks the source), and the velvet SparseEarlyField, when
// spread far enough to read as discrete, turns its broadband Dirac taps into a
// metallic magnitude comb (HF-tail kurtosis blows up — proven on Cathedral).
//
// The fix is the textbook ER topology: DIFFUSE, THEN TAP. The input transient is
// first smeared by a short allpass cascade (~3-6 ms) into a dense burst, and THEN
// read out at the discrete reflection times. Each tap therefore emits a smooth
// diffuse burst (no comb → not metallic) AT a discrete arrival time (defined →
// clarity). Independent L/R allpass chains decorrelate the field (wide, stereo).
// The reflection list is per-preset (matched to the anchor's measured comb).
//
// Allpasses are unity-gain (flat magnitude) → the field's spectrum is the input's;
// no spectral tilt, only temporal structure. Empty reflection list / not prepared
// → process() writes silence and active()==false → caller skips → bit-null.
class DiffusedEarlyReflections
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;
        const int bufLen = DspUtils::nextPowerOf2 (
            static_cast<int> (std::ceil (kMaxTimeMs * 0.001 * sampleRate)) + 8);
        bufferL_.assign (static_cast<size_t> (bufLen), 0.0f);
        bufferR_.assign (static_cast<size_t> (bufLen), 0.0f);
        mask_ = bufLen - 1;
        writePos_ = 0;
        buildDiffusers();
        prepared_ = true;
        rebuildTaps();
    }

    // Per-preset reflection comb: times (ms after onset) + linear gains. n == 0 →
    // inactive (bit-null). Times beyond kMaxTimeMs are dropped. Message-thread only.
    void setReflections (const float* timesMs, const float* gains, int n)
    {
        n_ = std::clamp (n, 0, kMaxRefl);
        for (int i = 0; i < n_; ++i) { timesMs_[i] = timesMs[i]; gains_[i] = gains[i]; }
        if (prepared_) rebuildTaps();
    }

    // Per-reflection diffusion smear (0 = none/sharp, 1 = full ~6ms burst). Scales
    // the allpass feedback so a preset can dial how "diffuse vs slappy" the taps read.
    void setDiffusion (float amount)
    {
        const float a = std::clamp (amount, 0.0f, 1.0f);
        if (std::abs (a - diffusion_) <= 1.0e-4f) return;
        diffusion_ = a;
        if (prepared_) buildDiffusers();
    }

    bool active() const { return prepared_ && n_ > 0; }

    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples)
    {
        if (! active())
        {
            std::fill (outL, outL + numSamples, 0.0f);
            std::fill (outR, outR + numSamples, 0.0f);
            return;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = 0.5f * (inL[i] + inR[i]);   // mono feed; width comes from the L/R chains
            float dl = x, dr = x;
            for (int s = 0; s < kNumAP; ++s) { dl = apL_[s].process (dl); dr = apR_[s].process (dr); }
            bufferL_[static_cast<size_t> (writePos_)] = dl;
            bufferR_[static_cast<size_t> (writePos_)] = dr;

            float l = 0.0f, r = 0.0f;
            for (int t = 0; t < n_; ++t)
            {
                l += gains_[t] * bufferL_[static_cast<size_t> ((writePos_ - tapDelay_[t]) & mask_)];
                r += gains_[t] * bufferR_[static_cast<size_t> ((writePos_ - tapDelay_[t]) & mask_)];
            }
            outL[i] = l;
            outR[i] = r;
            writePos_ = (writePos_ + 1) & mask_;
        }
    }

    void clear()
    {
        std::fill (bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill (bufferR_.begin(), bufferR_.end(), 0.0f);
        writePos_ = 0;
        for (int s = 0; s < kNumAP; ++s) { apL_[s].clear(); apR_[s].clear(); }
    }

private:
    static constexpr int   kMaxRefl   = 24;
    static constexpr float kMaxTimeMs = 260.0f;
    static constexpr int   kNumAP     = 2;     // SHORT cascade (~2-3ms smear): must be < the ~9ms tightest reflection spacing, else adjacent reflections merge into one blob (lose discreteness). Smooths each tap enough to avoid the metallic Dirac comb without merging the comb.

    // Schroeder allpass — unity-gain, flat magnitude. Smears the transient in time.
    struct Allpass
    {
        std::vector<float> buf; int idx = 0; float g = 0.0f;
        void prepare (int len, float gain) { buf.assign (static_cast<size_t> (std::max (len, 1)), 0.0f); idx = 0; g = gain; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process (float x)
        {
            const float d = buf[static_cast<size_t> (idx)];
            const float y = d - g * x;
            buf[static_cast<size_t> (idx)] = x + g * y;
            idx = (idx + 1) % static_cast<int> (buf.size());
            return y;
        }
    };

    void buildDiffusers()
    {
        // Short coprime allpass delays (samples @44.1k, ms ≈ {1.0,1.9,3.0,4.3}), L/R
        // distinct → ~6 ms smear, decorrelated. Feedback scales with diffusion_.
        const float r = static_cast<float> (sampleRate_ / 44100.0);
        const float g = 0.5f + 0.2f * diffusion_;   // 0.5..0.7 (below the 0.7 ring threshold)
        for (int s = 0; s < kNumAP; ++s)
        {
            apL_[s].prepare (static_cast<int> (kApL[s] * r) + 1, g);
            apR_[s].prepare (static_cast<int> (kApR[s] * r) + 1, g);
        }
    }

    void rebuildTaps()
    {
        const float sr = static_cast<float> (sampleRate_);
        int m = 0;
        for (int i = 0; i < n_; ++i)
        {
            if (timesMs_[i] <= 0.0f || timesMs_[i] > kMaxTimeMs) continue;
            const int d = static_cast<int> (timesMs_[i] * 0.001f * sr);
            if (d <= 0 || d > mask_) continue;
            tapDelay_[m] = d; gains_[m] = gains_[i]; ++m;
        }
        n_ = m;
    }

    static constexpr int kApL[kNumAP] = { 37, 71 };   // ~0.8,1.6 ms → ~2.4ms smear (< tightest spacing)
    static constexpr int kApR[kNumAP] = { 41, 79 };   // coprime, R offset → decorrelated

    double sampleRate_ = 44100.0;
    bool   prepared_   = false;
    std::vector<float> bufferL_, bufferR_;
    int    writePos_ = 0, mask_ = 0;

    Allpass apL_[kNumAP], apR_[kNumAP];
    float   diffusion_ = 0.6f;

    int   n_ = 0;
    float timesMs_[kMaxRefl] {};
    float gains_[kMaxRefl] {};
    int   tapDelay_[kMaxRefl] {};
};
