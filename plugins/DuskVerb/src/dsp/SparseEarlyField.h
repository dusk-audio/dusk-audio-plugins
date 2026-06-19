#pragma once

#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// SparseEarlyField — front-loaded, sparse, broadband, WIDE early-reflection
// field extending to ~200 ms. The early-dominant topology that the standard
// parallel EarlyReflections (24 taps, 8-80 ms, inverse-distance, LP-rolled,
// anti-phase R) cannot be: VVV front-loads 56% of energy in the first 50 ms and
// stays SPARSE/discrete out to ~200 ms (two-burst kurtosis trajectory), where
// every DuskVerb tank/FDN back-loads into a Gaussian wash by 60 ms.
//
// Velvet-noise-style sparse signed taps (one per "grain" cell, pseudo-random
// sub-cell position + sign), with:
//   - density + gain FRONT-LOADED (decreasing with time) -> energy_t50/first50,
//   - a TWO-BURST density envelope (dense 0-40 ms, second cluster ~95-140 ms)
//     -> the diffusion_flux kurtosis shape,
//   - a gently-RISING onset gain ramp (peak ~onsetPeakMs) -> attack_time/onset_slope,
//   - INDEPENDENT L/R tap times+signs (decorrelated, not anti-phase) -> wide,
//     uniform stereo image (stereo_corr ~ 0),
//   - broadband taps (no per-tap LP) -> spectrally full like VVV.
//
// Deterministic (seeded) tap geometry => reproducible renders. Allocation-free
// process; taps rebuilt off-thread on param change.
class SparseEarlyField
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
        prepared_ = true;
        buildTaps();
    }

    void setSizeScale (float s)   { sizeScale_  = std::max (0.05f, s); if (prepared_) buildTaps(); }
    void setOnsetPeakMs (float m) { onsetPeakMs_ = std::max (0.0f, m); if (prepared_) buildTaps(); }
    void setDecayMs (float m)     { decayMs_    = std::max (10.0f, m); if (prepared_) buildTaps(); }
    void setBurst2Ms (float m)    { burst2Ms_   = std::max (0.0f, m); if (prepared_) buildTaps(); }
    void setBurst2Gain (float g)  { burst2Gain_ = std::max (0.0f, g); if (prepared_) buildTaps(); }

    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples)
    {
        if (! prepared_) { std::fill (outL, outL+numSamples, 0.0f); std::fill (outR, outR+numSamples, 0.0f); return; }
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = 0.5f * (inL[i] + inR[i]);
            bufferL_[static_cast<size_t> (writePos_)] = x;   // mono feed; L/R differ only by tap geometry
            bufferR_[static_cast<size_t> (writePos_)] = x;

            float l = 0.0f, r = 0.0f;
            for (int t = 0; t < numTapsL_; ++t)
                l += tapGainL_[t] * bufferL_[static_cast<size_t> ((writePos_ - tapDelayL_[t]) & mask_)];
            for (int t = 0; t < numTapsR_; ++t)
                r += tapGainR_[t] * bufferR_[static_cast<size_t> ((writePos_ - tapDelayR_[t]) & mask_)];

            outL[i] = l * kOutputScale;
            outR[i] = r * kOutputScale;
            writePos_ = (writePos_ + 1) & mask_;
        }
    }

    void clear()
    {
        std::fill (bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill (bufferR_.begin(), bufferR_.end(), 0.0f);
        writePos_ = 0;
    }

private:
    static constexpr int   kMaxTaps     = 160;
    static constexpr float kMaxTimeMs   = 220.0f;
    static constexpr float kOutputScale = 1.0f;
    // ~32 grain cells span 0..200ms; density envelope decides how many fire.
    static constexpr int   kCells       = 200;   // 1 ms cells

    // 32-bit xorshift — deterministic, no Math.random (reproducible renders).
    struct Rng { uint32_t s; float next() { s^=s<<13; s^=s>>17; s^=s<<5; return (s & 0xFFFFFF) / float(0x1000000); } };

    void buildTaps()
    {
        const float sr = static_cast<float> (sampleRate_);
        numTapsL_ = numTapsR_ = 0;
        buildChannel (0xC0FFEEu, tapDelayL_, tapGainL_, numTapsL_, sr);
        buildChannel (0x1234567u, tapDelayR_, tapGainR_, numTapsR_, sr);
        normalize (tapGainL_, numTapsL_);
        normalize (tapGainR_, numTapsR_);
    }

    // Density(t): front-loaded base (high near 0, decaying) + a second bump
    // around burst2Ms_. Probability a 1 ms cell emits a tap.
    float density (float tMs) const
    {
        const float b1 = std::exp (-tMs / 18.0f);                       // burst 1: dense 0-40ms
        const float b2 = (burst2Ms_ > 0.0f)
            ? 0.55f * std::exp (-((tMs - burst2Ms_) * (tMs - burst2Ms_)) / (2.0f * 22.0f * 22.0f))
            : 0.0f;                                                      // burst 2: cluster ~burst2Ms_
        return std::clamp (b1 + b2 + 0.06f, 0.0f, 1.0f);                 // 0.06 floor → sparse tail to 200ms
    }

    // Tap GAIN envelope: gently rising to onsetPeakMs_ then exponential decay.
    float gainEnv (float tMs) const
    {
        const float rise = (onsetPeakMs_ > 0.0f)
            ? std::min (1.0f, tMs / onsetPeakMs_)                        // linear ramp to peak
            : 1.0f;
        const float dec  = std::exp (-tMs / decayMs_);
        float g = rise * dec;
        // Discrete GAIN bump at burst2Ms_ — the late "duh-DUH" reflection. The
        // density envelope alone CLUSTERS taps there, but the monotonic decay leaves
        // their gain near-silent (gainEnv(139ms) ≈ 0.03 at decayMs 40), so a strong
        // EARLY onset cannot coexist with a loud LATE tap. This lifts the burst2 taps'
        // gain independently of the onset decay → onset stays the attack, burst2 is the
        // prominent secondary reflection (the VVV hall/chamber tap). 0 → bit-null (every
        // existing composite preset: Tiled/Cathedral/Vocal Hall/Blade/Large Chamber/79VC).
        if (burst2Gain_ > 0.0f && burst2Ms_ > 0.0f)
        {
            const float d = tMs - burst2Ms_;
            g += burst2Gain_ * std::exp (-(d * d) / (2.0f * 22.0f * 22.0f));   // 22 ms = the density-cluster width
        }
        return g;
    }

    void buildChannel (uint32_t seed, int* delays, float* gains, int& n, float sr)
    {
        Rng rng { seed };
        for (int c = 0; c < kCells && n < kMaxTaps; ++c)
        {
            const float baseMs = static_cast<float> (c) * sizeScale_;
            if (baseMs > kMaxTimeMs) break;
            if (rng.next() < density (baseMs))
            {
                const float subMs = rng.next() * sizeScale_;            // jitter within cell (L/R independent)
                const float tMs   = baseMs + subMs;
                const int   d      = static_cast<int> (tMs * 0.001f * sr);
                if (d <= 0 || d > mask_) continue;
                const float sign  = (rng.next() < 0.5f) ? -1.0f : 1.0f;
                delays[n] = d;
                gains[n]  = sign * gainEnv (tMs);
                ++n;
            }
        }
    }

    void normalize (float* gains, int n)
    {
        float e = 0.0f;
        for (int i = 0; i < n; ++i) e += gains[i] * gains[i];
        if (e <= 0.0f) return;
        const float g = 1.0f / std::sqrt (e);                            // unit-energy field
        for (int i = 0; i < n; ++i) gains[i] *= g;
    }

    double sampleRate_ = 44100.0;
    bool   prepared_   = false;
    std::vector<float> bufferL_, bufferR_;
    int    writePos_ = 0, mask_ = 0;

    int   tapDelayL_[kMaxTaps] {}, tapDelayR_[kMaxTaps] {};
    float tapGainL_ [kMaxTaps] {}, tapGainR_ [kMaxTaps] {};
    int   numTapsL_ = 0, numTapsR_ = 0;

    float sizeScale_  = 1.0f;     // ms per cell (scales the whole field in time)
    float onsetPeakMs_ = 14.0f;   // rising-onset peak
    float decayMs_    = 55.0f;    // overall gain decay constant
    float burst2Ms_   = 115.0f;   // second density cluster centre (0 = off)
    float burst2Gain_ = 0.0f;     // GAIN bump at burst2Ms_ (the discrete late tap; 0 = off, bit-null)
};
