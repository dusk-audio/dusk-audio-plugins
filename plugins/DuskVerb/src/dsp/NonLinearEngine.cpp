#include "NonLinearEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// NonLinearEngine v7 — HALL + SIDECHAIN GATE (the engineering technique).
// See header for full architecture.
// ============================================================================

namespace
{
    // Convert a UI dB value to linear gain.
    inline float dbToLin (float dB) { return std::pow (10.0f, dB / 20.0f); }
}

void NonLinearEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;

    // ── Hall (FDNReverb) ──
    // Configured for "lush big hall" baseline — dense, slightly dark,
    // matching the Logic Space Designer "Big Hall" character a vintage
    // engineer would dial up before patching the gate after it. Per-preset
    // overrides via the universal setter API.
    fdn_.prepare (sampleRate, maxBlockSize);

    // Default hall character: lushest config (matches the Lush Dark Hall preset
    // baseline as the user requested in the v7 plan). Per-preset setters then
    // override per-instance.
    fdn_.setDecayTime         (1.5f);   // 1.5 s default; Phil Collins sweet spot
    fdn_.setSize              (0.80f);  // big hall room
    fdn_.setBassMultiply      (1.20f);  // slight low-end body
    fdn_.setMidMultiply       (1.10f);  // mid emphasis (lush voicing)
    fdn_.setTrebleMultiply    (0.70f);  // gentle HF damping (lush, not bright)
    fdn_.setCrossoverFreq     (550.0f); // bass↔mid split
    fdn_.setHighCrossoverFreq (3500.0f);// mid↔treble split
    fdn_.setSaturation        (0.0f);
    fdn_.setTankDiffusion     (0.85f);  // very dense — max-density baseline

    // ── Gate ──
    // Defaults match a typical Phil Collins-style snare gate (will be
    // overridden by preset). 1-sample attack on the envelope follower
    // (built into NoiseGate) ensures the gate opens crisply on the snare
    // transient.
    gate_.prepare (sampleRate);
    gate_.setThreshold      (dbToLin (-32.0f));
    gate_.setReduction      (0.0f);                                                  // full silence
    gate_.setAttackSamples  (std::max (1, static_cast<int> (0.001 * sampleRate)));   // 1 ms
    gate_.setHoldSamples    (std::max (0, static_cast<int> (0.150 * sampleRate)));   // 150 ms
    gate_.setReleaseSamples (std::max (1, static_cast<int> (0.050 * sampleRate)));   // 50 ms

    // ── Scratch buffers ──
    wetL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    wetR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    prepared_ = true;
}

void NonLinearEngine::clearBuffers()
{
    fdn_.clearBuffers();
    gate_.reset();
    std::fill (wetL_.begin(), wetL_.end(), 0.0f);
    std::fill (wetR_.begin(), wetR_.end(), 0.0f);
}

// ============================================================================
// Setters — most pass through to the FDN; a few are re-purposed for gate
// parameters (see header for the full mapping table).
// ============================================================================

void NonLinearEngine::setDecayTime (float seconds)
{
    // Hall RT60. The plan calls for a long decay range (0.5 - 6 s) for
    // "big hall"; we trust the FDN's internal clamping.
    fdn_.setDecayTime (seconds);
}

void NonLinearEngine::setSize         (float size) { fdn_.setSize (size); }
void NonLinearEngine::setBassMultiply (float mult) { fdn_.setBassMultiply (mult); }
void NonLinearEngine::setTrebleMultiply (float mult) { fdn_.setTrebleMultiply (mult); }
void NonLinearEngine::setCrossoverFreq (float hz)    { fdn_.setCrossoverFreq (hz); }
void NonLinearEngine::setHighCrossoverFreq (float hz){ fdn_.setHighCrossoverFreq (hz); }
void NonLinearEngine::setSaturation   (float amount) { fdn_.setSaturation (amount); }

// MID MULT → GATE THRESHOLD. Map 0.1..1.5 (FDN's mid-mult range — see
// PluginProcessor.cpp param layout) to threshold dB -60..0:
//   mult = 0.10 → threshold -60 dB (very sensitive)
//   mult = 0.80 → threshold -28 dB (typical Phil Collins)
//   mult = 1.50 → threshold   0 dB (gate barely triggers)
void NonLinearEngine::setMidMultiply (float mult)
{
    const float clamped = std::clamp (mult, 0.1f, 1.5f);
    const float dB = -60.0f + (clamped - 0.1f) / 1.4f * 60.0f;
    gate_.setThreshold (dbToLin (dB));
}

// DEPTH (mod_depth, 0..1) → GATE ATTACK. Linear 0..1 → 1..50 ms.
//   depth 0.00 →  1 ms (instant gate snap)
//   depth 0.04 →  3 ms (Phil Collins)
//   depth 1.00 → 50 ms (slow soft open)
void NonLinearEngine::setModDepth (float depth)
{
    const float clamped = std::clamp (depth, 0.0f, 1.0f);
    const float ms = 1.0f + clamped * 49.0f;
    gate_.setAttackSamples (std::max (1, static_cast<int> (ms * 0.001 * sampleRate_)));
}

// RATE (mod_rate, 0.1..10 Hz) → GATE RELEASE. Linear map to 5..2000 ms.
//   rate  0.1 →    5 ms (hard cliff)
//   rate  0.4 →   80 ms (typical 80s gated snare)
//   rate 10.0 → 2000 ms (long natural fade — gate barely shaping)
void NonLinearEngine::setModRate (float hz)
{
    const float clamped = std::clamp (hz, 0.1f, 10.0f);
    const float ms = 5.0f + (clamped - 0.1f) / 9.9f * 1995.0f;
    gate_.setReleaseSamples (std::max (1, static_cast<int> (ms * 0.001 * sampleRate_)));
}

// DIFFUSION (0..1) → GATE HOLD. Linear 0..1 → 0..500 ms.
//   diffusion 0.00 →   0 ms (immediate release after threshold drop)
//   diffusion 0.30 → 150 ms (Phil Collins)
//   diffusion 1.00 → 500 ms (long sustained gate before fade)
void NonLinearEngine::setTankDiffusion (float amount)
{
    const float clamped = std::clamp (amount, 0.0f, 1.0f);
    const float ms = clamped * 500.0f;
    gate_.setHoldSamples (std::max (0, static_cast<int> (ms * 0.001 * sampleRate_)));
}

void NonLinearEngine::setFreeze (bool frozen)
{
    fdn_.setFreeze (frozen);
}

void NonLinearEngine::setGateEnabled (bool enabled)
{
    gateEnabled_ = enabled;
}

// ============================================================================
// process — Hall → (optional) Gate. Trigger = DRY input.
// ============================================================================

void NonLinearEngine::process (const float* inL, const float* inR,
                               float* outL, float* outR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    if (numSamples > static_cast<int> (wetL_.size()))
        numSamples = static_cast<int> (wetL_.size());

    // ── 1) Hall stage — FDN produces wet from dry input ──
    fdn_.process (inL, inR, wetL_.data(), wetR_.data(), numSamples);

    // ── 2) Gate stage — trigger from DRY input, applies envelope to wet ──
    if (gateEnabled_)
    {
        gate_.process (inL, inR, wetL_.data(), wetR_.data(), numSamples);
    }
    // (When gate is OFF, wet passes through unmodified — pure hall reverb.)

    // ── 3) Copy wet to output ──
    std::memcpy (outL, wetL_.data(), sizeof (float) * static_cast<size_t> (numSamples));
    std::memcpy (outR, wetR_.data(), sizeof (float) * static_cast<size_t> (numSamples));
}
