#include "ReverseRoomEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// ReverseRoomEngine — causal rising-ER onset + dark modulated FDN tail.
// Reverse-engineered from lex-reverse-1 (Lexicon PCM Room "Reverse 1").
// See header for the full architecture and the measured reference spec.
// ============================================================================

void ReverseRoomEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_   = maxBlockSize;

    // Dark, dense, modulated diffuse tail baseline (the "Reverse 1" room).
    // Per-preset setters override these via the universal API below.
    fdn_.prepare (sampleRate, maxBlockSize);
    fdn_.setDecayTime         (2.0f);
    fdn_.setSize              (0.85f);
    fdn_.setBassMultiply      (1.20f);
    fdn_.setMidMultiply       (1.05f);
    fdn_.setTrebleMultiply    (0.55f);   // heavy HF damping (9.8k -> 3k centroid)
    fdn_.setCrossoverFreq     (275.0f);  // Bass XOver from the reference
    fdn_.setHighCrossoverFreq (3000.0f);
    fdn_.setSaturation        (0.0f);
    fdn_.setTankDiffusion     (0.85f);
    fdn_.setModDepth          (0.20f);   // the ~2.7 Hz Spin
    fdn_.setModRate           (2.7f);

    // ER ring buffers: must hold the longest tap delay. Max onset window =
    // rampMs_ at the largest size scale (~2x) -> size generously to pow2.
    const int maxRampSamp = static_cast<int> (rampMs_ * 0.001 * sampleRate * 2.5) + 64;
    const int ringSize    = DspUtils::nextPowerOf2 (std::max (maxRampSamp, 1024));
    erL_.ring.assign (static_cast<size_t> (ringSize), 0.0f);
    erR_.ring.assign (static_cast<size_t> (ringSize), 0.0f);
    erL_.mask = erR_.mask = ringSize - 1;
    erL_.writePos = erR_.writePos = 0;

    erBufL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erBufR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    rebuildTaps();
    prepared_ = true;
}

void ReverseRoomEngine::clearBuffers()
{
    fdn_.clearBuffers();
    std::fill (erL_.ring.begin(), erL_.ring.end(), 0.0f);
    std::fill (erR_.ring.begin(), erR_.ring.end(), 0.0f);
    erL_.writePos = erR_.writePos = 0;
    std::fill (erBufL_.begin(), erBufL_.end(), 0.0f);
    std::fill (erBufR_.begin(), erBufR_.end(), 0.0f);
}

// Build the rising-gain early-reflection tap sets. Deterministic (fixed seed)
// so every render is bit-identical — the optimizer/gates need reproducibility.
void ReverseRoomEngine::rebuildTaps()
{
    const float sr       = static_cast<float> (sampleRate_);
    const int   rampSamp = std::max (8, static_cast<int> (rampMs_ * 0.001f * sr * sizeScale_));
    const int   minTap   = std::max (1, static_cast<int> (0.002f * sr));   // 2 ms floor
    // density 0 -> 16 taps (sparse "Concrete Stairs"), 1 -> kMaxTaps (dense).
    const int   n        = std::clamp (16 + static_cast<int> (density_ * (kMaxTaps - 16)),
                                       16, kMaxTaps);
    numTaps_ = n;

    for (int ch = 0; ch < 2; ++ch)
    {
        ERChannel& e = (ch == 0) ? erL_ : erR_;
        // Independent L/R pseudo-random sequences for wide stereo decorrelation.
        std::uint32_t s = (ch == 0) ? 0x1234567u : 0x89ABCDEu;
        auto rnd = [&s]() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); };

        for (int k = 0; k < n; ++k)
        {
            // Roughly even spacing across the ramp with +-half-slot jitter
            // -> ~4-5 ms average spacing, semi-dense (matches density 0.47).
            const float frac = (static_cast<float> (k) + 0.5f) / static_cast<float> (n);
            const float jit  = (rnd() - 0.5f) / static_cast<float> (n);
            const float pf   = std::clamp (frac + jit, 0.0f, 1.0f);
            int p = minTap + static_cast<int> (pf * static_cast<float> (rampSamp - minTap));
            p = std::clamp (p, 1, e.mask);
            e.pos[k] = p;

            // RISING gain from a floor: g = floor + (1-floor)*(t/ramp)^slope.
            // The floor keeps the onset from starting at digital silence (which
            // produced an infinite env_p2p swell); the reference rises from a
            // finite -86dB-ish floor, giving env_p2p ~+24.6, not a cliff.
            const float rel = std::clamp (static_cast<float> (p) / static_cast<float> (rampSamp), 0.0f, 1.0f);
            e.gain[k] = floorGain_ + (1.0f - floorGain_) * std::pow (rel, slope_);
        }

        // Bound the summed output level (sqrt(n) for incoherent taps).
        const float norm = 1.0f / std::sqrt (static_cast<float> (n));
        for (int k = 0; k < n; ++k)
            e.gain[k] *= norm;
    }
}

void ReverseRoomEngine::process (const float* inL, const float* inR,
                                 float* outL, float* outR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    if (numSamples > maxBlock_)
        numSamples = maxBlock_;

    // ── 1) Rising-ER FIR: shape dry input into a swelling early-reflection
    //        burst (the "reverse" onset). Per channel, allocation-free. ──
    for (int ch = 0; ch < 2; ++ch)
    {
        ERChannel&   e   = (ch == 0) ? erL_ : erR_;
        const float* in  = (ch == 0) ? inL : inR;
        float*       out = (ch == 0) ? erBufL_.data() : erBufR_.data();
        const int    mask = e.mask;
        const int    nt   = numTaps_;
        float*       ring = e.ring.data();
        int          wp   = e.writePos;

        for (int i = 0; i < numSamples; ++i)
        {
            ring[wp] = in[i];
            float acc = 0.0f;
            for (int k = 0; k < nt; ++k)
                acc += e.gain[k] * ring[(wp - e.pos[k]) & mask];
            out[i] = acc;
            wp = (wp + 1) & mask;
        }
        e.writePos = wp;
    }

    // ── 2) Dark modulated diffuse tail. Feeding the FDN in SERIES makes the
    //        tail inherit the swell -> whole response rises then decays. ──
    fdn_.process (erBufL_.data(), erBufR_.data(), outL, outR, numSamples);
}

// ── Universal setters ──────────────────────────────────────────────────────
void ReverseRoomEngine::setDecayTime (float seconds)   { fdn_.setDecayTime (seconds); }
void ReverseRoomEngine::setBassMultiply (float mult)   { fdn_.setBassMultiply (mult); }
void ReverseRoomEngine::setMidMultiply (float mult)    { fdn_.setMidMultiply (mult); }
void ReverseRoomEngine::setTrebleMultiply (float mult) { fdn_.setTrebleMultiply (mult); }
void ReverseRoomEngine::setCrossoverFreq (float hz)    { fdn_.setCrossoverFreq (hz); }
void ReverseRoomEngine::setHighCrossoverFreq (float hz){ fdn_.setHighCrossoverFreq (hz); }
void ReverseRoomEngine::setSaturation (float amount)   { fdn_.setSaturation (amount); }
void ReverseRoomEngine::setModDepth (float depth)      { fdn_.setModDepth (depth); }
void ReverseRoomEngine::setModRate (float hz)          { fdn_.setModRate (hz); }
void ReverseRoomEngine::setFreeze (bool frozen)        { fdn_.setFreeze (frozen); }

void ReverseRoomEngine::setSize (float size)
{
    fdn_.setSize (size);
    // 0..1 size -> 0.6..1.6x ER onset span (longer onset on bigger rooms).
    const float newScale = 0.6f + 1.0f * std::clamp (size, 0.0f, 1.0f);
    if (std::abs (newScale - sizeScale_) > 1.0e-4f)
    {
        sizeScale_ = newScale;
        if (prepared_) rebuildTaps();
    }
}

void ReverseRoomEngine::setTankDiffusion (float amount)
{
    fdn_.setTankDiffusion (amount);
    const float a = std::clamp (amount, 0.0f, 1.0f);
    if (std::abs (a - density_) > 1.0e-4f)
    {
        density_ = a;
        if (prepared_) rebuildTaps();
    }
}
