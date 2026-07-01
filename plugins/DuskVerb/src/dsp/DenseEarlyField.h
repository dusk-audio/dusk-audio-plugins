#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// Dense early-field — a compact Schroeder reverb (4 parallel combs + 2 series
// allpasses, mono) fed from a predelayed dry-mono input, darkened and mildly
// R-decorrelated, intended to be summed POST-tank. It fills the loud post-onset
// 0.1-0.5 s "shelf" a real plate / room has but a sparse Dattorro tank disperses
// too fast (a thin, peaked tail). The predelay keeps the field AFTER the main
// onset (no pre-echo). gain 0 → inactive → contributes exactly 0 (bit-null sum).
//
// The algorithm mirrors DattorroPlateVintage's inline dense field (which stays
// inline there to preserve its byte-exact VVP render). This header lets the
// DuskVerbEngine reuse the same field for the algo-0 Dattorro rooms without
// touching DattorroTank.cpp — so every algo-0 preset stays bit-null when the
// field is off. See [[duskverb_dpv_dense_early_field]].
class DenseEarlyField
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = static_cast<float> (sampleRate);
        for (int c = 0; c < 4; ++c)
        {
            comb_[c].prepare (static_cast<int> (kCombMs[c] * 0.001f * sampleRate_) + 8);
            combLen_[c] = std::max (1, static_cast<int> (kCombMs[c] * 0.001f * sampleRate_));
        }
        ap1_.prepare (static_cast<int> (kAp1Ms * 0.001f * sampleRate_) + 8);
        ap2_.prepare (static_cast<int> (kAp2Ms * 0.001f * sampleRate_) + 8);
        ap1Len_ = std::max (1, static_cast<int> (kAp1Ms * 0.001f * sampleRate_));
        ap2Len_ = std::max (1, static_cast<int> (kAp2Ms * 0.001f * sampleRate_));
        pre_.prepare (static_cast<int> (0.30f * sampleRate_));   // up to 300 ms predelay
        apR_.prepare (static_cast<int> (kDecorrMs * 0.001f * sampleRate_) + 8);
        apRLen_ = std::max (1, static_cast<int> (kDecorrMs * 0.001f * sampleRate_));
        lpCoeff_ = 1.0f - std::exp (-6.283185307f
                       * std::min (kLpHz, 0.45f * sampleRate_) / sampleRate_);
        prepared_ = true;
        setParams (gain_, predelayMs_, t60Ms_);   // (re)compute feedback + predelay
    }

    void clear()
    {
        for (int c = 0; c < 4; ++c) comb_[c].clear();
        ap1_.clear(); ap2_.clear(); pre_.clear(); apR_.clear();
        lpZ_ = 0.0f;
    }

    void setParams (float gain, float predelayMs, float t60Ms)
    {
        gain_       = std::clamp (gain, 0.0f, 4.0f);
        predelayMs_ = std::max (0.0f, predelayMs);
        t60Ms_      = std::clamp (t60Ms, 50.0f, 2000.0f);
        active_     = gain_ > 1.0e-6f;
        if (prepared_)
        {
            preSamp_ = std::min (std::max (0, static_cast<int> (predelayMs_ * 0.001f * sampleRate_)), pre_.mask);
            const float t60s = t60Ms_ * 0.001f;
            for (int c = 0; c < 4; ++c)
            {
                // Schroeder comb feedback for the target T60: g = 10^(-3·Lsec/T60).
                const float Lsec = combLen_[c] / sampleRate_;
                combG_[c] = std::clamp (std::pow (10.0f, -3.0f * Lsec / t60s), 0.0f, 0.97f);
            }
        }
    }

    bool active() const { return active_; }

    // Process one mono input sample; ADDS the gain-scaled stereo dense-field
    // contribution into addL/addR. Caller guards on active() and supplies the
    // dry-mono input (0 when frozen, so the field decays out with the tank).
    inline void processSample (float monoIn, float& addL, float& addR)
    {
        // Read the predelayed tap BEFORE advancing the ring so preSamp_ maps to an
        // exact preSamp_-sample delay (write-then-read was one sample early and read a
        // stale slot at zero delay). Fast path when preSamp_ is 0 (no predelay).
        const float xp = (preSamp_ == 0) ? monoIn : pre_.readAgo (preSamp_);
        pre_.write (monoIn);
        float s = 0.0f;
        for (int c = 0; c < 4; ++c)
        {
            const float d = comb_[c].readAgo (combLen_[c]);
            comb_[c].write (xp + combG_[c] * d);
            s += d;
        }
        s *= 0.25f;
        const float a1 = ap1_.readAgo (ap1Len_); const float y1 = -kApG * s  + a1; ap1_.write (s  + kApG * y1);
        const float a2 = ap2_.readAgo (ap2Len_); const float y2 = -kApG * y1 + a2; ap2_.write (y1 + kApG * y2);
        // Darken (early reflections are duller) then decorrelate R for stereo.
        lpZ_ += lpCoeff_ * (y2 - lpZ_);
        const float yL = lpZ_;
        const float ar = apR_.readAgo (apRLen_); const float yR = -kDecorrG * yL + ar; apR_.write (yL + kDecorrG * yR);
        addL += gain_ * yL;
        addR += gain_ * yR;
    }

private:
    // Power-of-2 ring delay (single-write, integer taps) — mirrors the type used
    // by DattorroPlateVintage's dense field so the math is bit-for-bit the same.
    struct RingDelay
    {
        std::vector<float> buf; int mask = 0, w = 0;
        void prepare (int maxSamples)
        {
            int n = 1; while (n < std::max (2, maxSamples + 1)) n <<= 1;
            buf.assign (static_cast<size_t> (n), 0.0f); mask = n - 1; w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        inline void  write (float x)        { buf[static_cast<size_t> (w)] = x; w = (w + 1) & mask; }
        inline float readAgo (int d) const  { return buf[static_cast<size_t> ((w - d) & mask)]; }
    };

    float sampleRate_ = 48000.0f;
    bool  prepared_   = false;

    RingDelay comb_[4], ap1_, ap2_, pre_, apR_;
    int   combLen_[4] = { 0, 0, 0, 0 }, ap1Len_ = 0, ap2Len_ = 0, apRLen_ = 0, preSamp_ = 0;
    float combG_[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
    float gain_       = 0.0f;                       // 0 = off (bit-null)
    float t60Ms_      = 500.0f, predelayMs_ = 70.0f;
    bool  active_     = false;
    float lpZ_        = 0.0f, lpCoeff_ = 1.0f;       // 1-pole darken state

    static constexpr float kApG     = 0.7f;
    static constexpr float kLpHz    = 12000.0f;     // band-limit (near-bypass; LP hurts the shelf fill)
    static constexpr float kDecorrMs = 3.7f;        // R decorrelation delay (mild)
    static constexpr float kDecorrG  = 0.14f;       // mild decorrelation (early field near-mono)
    static constexpr float kCombMs[4] = { 23.3f, 29.7f, 37.1f, 43.3f };  // prime-ish, dense buildup
    static constexpr float kAp1Ms = 5.5f, kAp2Ms = 1.7f;
};
