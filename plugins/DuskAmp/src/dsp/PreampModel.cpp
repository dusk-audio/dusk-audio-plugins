// SPDX-License-Identifier: GPL-3.0-or-later

// PreampModel.cpp — Amp-specific preamp implementations

#include "PreampModel.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265359f;

// Helper: compute one-pole HPF coefficient from cutoff frequency
static float hpfCoeff (float fc, double sr)
{
    return std::exp (-2.0f * kPi * fc / static_cast<float> (sr));
}

// Helper: compute one-pole LPF coefficient from cutoff frequency
static float lpfCoeff (float fc, double sr)
{
    float w = 2.0f * kPi * fc / static_cast<float> (sr);
    return w / (w + 1.0f);
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<PreampModel> PreampModel::create (AmpType type)
{
    switch (type)
    {
        case AmpType::Fender:  return std::make_unique<FenderPreamp>();
        case AmpType::Marshall: return std::make_unique<MarshallPreamp>();
        case AmpType::Vox:     return std::make_unique<VoxPreamp>();
    }
    return std::make_unique<MarshallPreamp>(); // fallback
}

// ============================================================================
// Fender Twin Reverb
// ============================================================================

void FenderPreamp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    // V1a: 12AX7, moderate bias for clean headroom
    v1a_.setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
    v1a_.prepare (sampleRate, 1);
    v1a_.setBiasPoint (-0.1f); // slightly cold bias = more headroom

    dc1_.prepare (sampleRate, 10.0f);

    updateCoeffs();
    reset();
}

void FenderPreamp::reset()
{
    v1a_.reset();
    dc1_.reset();
    cfEnvelope_ = 0.0f;
    couplingCapState_ = 0.0f;
    cathodeBypassState_ = 0.0f;
    brightCapState_ = 0.0f;
}

void FenderPreamp::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    // Fender: map 0-1 to gentle drive range (lots of clean headroom)
    v1a_.setDrive (gain_ * 0.5f);
}

void FenderPreamp::setBright (bool on)
{
    bright_ = on;
}

void FenderPreamp::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // Bright cap: treble boost before gain stage (120pF across volume pot)
        if (bright_)
        {
            float hp = sample - brightCapState_;
            brightCapState_ += hp * (1.0f - brightCapCoeff_);
            sample += hp * 0.25f; // subtle treble lift
        }

        // Coupling cap HPF (22nF → ~30Hz rolloff)
        float hpOut = sample - couplingCapState_;
        couplingCapState_ += hpOut * (1.0f - couplingCapCoeff_);
        sample = hpOut;

        // V1a gain stage
        sample = v1a_.processSample (sample, 0);
        sample = dc1_.processSample (sample);

        // Cathode bypass: boost low frequencies (25uF → ~80Hz)
        // This adds warmth to the Fender clean tone
        cathodeBypassState_ += (sample - cathodeBypassState_) * cathodeBypassCoeff_;
        sample += cathodeBypassState_ * 0.15f * gain_;

        // Cathode follower (V1b): unity-gain buffer with soft compression
        // The CF compresses by drawing grid current when the signal is large
        float absSample = std::abs (sample);
        if (absSample > cfEnvelope_)
            cfEnvelope_ = cfAttackCoeff_ * cfEnvelope_ + (1.0f - cfAttackCoeff_) * absSample;
        else
            cfEnvelope_ = cfReleaseCoeff_ * cfEnvelope_;

        // Compress proportionally — up to 30% gain reduction at full signal
        float compression = 1.0f - cfEnvelope_ * 0.3f;
        compression = std::max (compression, 0.5f);
        sample *= compression;

        buffer[i] = sample;
    }
}

void FenderPreamp::updateCoeffs()
{
    couplingCapCoeff_ = hpfCoeff (30.0f, sampleRate_);   // 22nF → ~30Hz
    cathodeBypassCoeff_ = lpfCoeff (80.0f, sampleRate_); // 25uF → ~80Hz
    brightCapCoeff_ = hpfCoeff (1200.0f, sampleRate_);   // 120pF → ~1.2kHz

    // Cathode follower envelope: attack ~2ms, release ~50ms
    cfAttackCoeff_ = std::exp (-1000.0f / (2.0f * static_cast<float> (sampleRate_)));
    cfReleaseCoeff_ = std::exp (-1000.0f / (50.0f * static_cast<float> (sampleRate_)));
}

// ============================================================================
// Marshall Plexi 1959
// ============================================================================

void MarshallPreamp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    // Both stages: 12AX7
    v1a_.setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
    v1b_.setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
    v1a_.prepare (sampleRate, 1);
    v1b_.prepare (sampleRate, 1);

    // Marshall runs hotter bias than Fender
    v1a_.setBiasPoint (0.0f);
    v1b_.setBiasPoint (0.05f); // slightly hot = earlier breakup

    dc1_.prepare (sampleRate, 10.0f);
    dc2_.prepare (sampleRate, 10.0f);

    updateCoeffs();
    reset();
}

void MarshallPreamp::reset()
{
    v1a_.reset();
    v1b_.reset();
    dc1_.reset();
    dc2_.reset();
    couplingCapState_[0] = couplingCapState_[1] = 0.0f;
    cathodeBypassState_[0] = cathodeBypassState_[1] = 0.0f;
    brightCapState_ = 0.0f;
}

void MarshallPreamp::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    // Marshall: progressive cascaded gain
    // V1a gets moderate drive, V1b gets more (driven by V1a's output)
    v1a_.setDrive (gain_ * 0.4f);
    v1b_.setDrive (gain_ * 0.7f);
}

void MarshallPreamp::setBright (bool on)
{
    bright_ = on;
}

void MarshallPreamp::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // Bright cap: more aggressive treble boost (5nF → ~700Hz)
        if (bright_)
        {
            float hp = sample - brightCapState_;
            brightCapState_ += hp * (1.0f - brightCapCoeff_);
            sample += hp * 0.35f; // more aggressive than Fender
        }

        // --- Stage 1 (V1a) ---

        // Coupling cap HPF (~30Hz)
        float hp1 = sample - couplingCapState_[0];
        couplingCapState_[0] += hp1 * (1.0f - couplingCapCoeff_);
        sample = hp1;

        // V1a tube stage
        sample = v1a_.processSample (sample, 0);
        sample = dc1_.processSample (sample);

        // Cathode bypass V1a: 0.68uF → ~340Hz (less bass boost than Fender)
        cathodeBypassState_[0] += (sample - cathodeBypassState_[0]) * cathodeBypassCoeff_;
        sample += cathodeBypassState_[0] * 0.1f * gain_;

        // --- Stage 2 (V1b) ---

        // Coupling cap HPF (~30Hz)
        float hp2 = sample - couplingCapState_[1];
        couplingCapState_[1] += hp2 * (1.0f - couplingCapCoeff_);
        sample = hp2;

        // V1b tube stage (driven by V1a output → cascaded distortion)
        sample = v1b_.processSample (sample, 0);
        sample = dc2_.processSample (sample);

        // Cathode bypass V1b
        cathodeBypassState_[1] += (sample - cathodeBypassState_[1]) * cathodeBypassCoeff_;
        sample += cathodeBypassState_[1] * 0.1f * gain_;

        buffer[i] = sample;
    }
}

void MarshallPreamp::updateCoeffs()
{
    couplingCapCoeff_ = hpfCoeff (30.0f, sampleRate_);     // 22nF → ~30Hz
    cathodeBypassCoeff_ = lpfCoeff (340.0f, sampleRate_);  // 0.68uF → ~340Hz
    brightCapCoeff_ = hpfCoeff (700.0f, sampleRate_);      // 5nF → ~700Hz
}

// ============================================================================
// Vox AC30 Top Boost
// ============================================================================

void VoxPreamp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    // V1a: first gain stage, V2a: second gain stage (after tone circuit)
    v1a_.setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
    v2a_.setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
    v1a_.prepare (sampleRate, 1);
    v2a_.prepare (sampleRate, 1);

    // AC30: hot bias for early breakup (Class A character)
    v1a_.setBiasPoint (0.1f);
    v2a_.setBiasPoint (0.1f);

    dc1_.prepare (sampleRate, 10.0f);
    dc2_.prepare (sampleRate, 10.0f);

    updateCoeffs();
    reset();
}

void VoxPreamp::reset()
{
    v1a_.reset();
    v2a_.reset();
    dc1_.reset();
    dc2_.reset();
    cfEnvelope_ = 0.0f;
    couplingCapState_[0] = couplingCapState_[1] = 0.0f;
    cathodeBypassState_ = 0.0f;
}

void VoxPreamp::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    // Vox: V1a moderate, V2a adds the "Top Boost" gain
    v1a_.setDrive (gain_ * 0.35f);
    v2a_.setDrive (gain_ * 0.55f);
}

void VoxPreamp::setBright (bool /*on*/)
{
    // AC30 has no bright cap — the Cut control on the tone stack handles HF
}

void VoxPreamp::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // --- Stage 1 (V1a) ---

        // Coupling cap HPF (~50Hz — 10nF into 470k)
        float hp1 = sample - couplingCapState_[0];
        couplingCapState_[0] += hp1 * (1.0f - couplingCapCoeff_);
        sample = hp1;

        // V1a tube stage
        sample = v1a_.processSample (sample, 0);
        sample = dc1_.processSample (sample);

        // Cathode bypass V1a: 25uF → ~80Hz (full bass boost, like Fender)
        cathodeBypassState_ += (sample - cathodeBypassState_) * cathodeBypassCoeff_;
        sample += cathodeBypassState_ * 0.12f * gain_;

        // --- Cathode follower (V1b) ---
        // The CF between stages is what gives the AC30 its spongy compression
        // and chime. It's a more pronounced effect than the Fender CF because
        // the AC30 runs in Class A with hotter bias.
        float absSample = std::abs (sample);
        if (absSample > cfEnvelope_)
            cfEnvelope_ = cfAttackCoeff_ * cfEnvelope_ + (1.0f - cfAttackCoeff_) * absSample;
        else
            cfEnvelope_ = cfReleaseCoeff_ * cfEnvelope_;

        // Stronger compression than Fender — up to 40% reduction
        float compression = 1.0f - cfEnvelope_ * 0.4f;
        compression = std::max (compression, 0.4f);
        sample *= compression;

        // --- Stage 2 (V2a) — after tone circuit in the real amp ---
        // In the real AC30, the tone circuit sits between V1b and V2a.
        // Here, the tone stack runs separately in the engine after this preamp.
        // V2a re-amplifies the tone-shaped signal.

        // Coupling cap HPF (~50Hz)
        float hp2 = sample - couplingCapState_[1];
        couplingCapState_[1] += hp2 * (1.0f - couplingCapCoeff_);
        sample = hp2;

        // V2a tube stage
        sample = v2a_.processSample (sample, 0);
        sample = dc2_.processSample (sample);

        buffer[i] = sample;
    }
}

void VoxPreamp::updateCoeffs()
{
    couplingCapCoeff_ = hpfCoeff (50.0f, sampleRate_);    // 10nF → ~50Hz
    cathodeBypassCoeff_ = lpfCoeff (80.0f, sampleRate_);  // 25uF → ~80Hz

    // Cathode follower envelope: faster attack than Fender (AC30 Class A)
    cfAttackCoeff_ = std::exp (-1000.0f / (1.5f * static_cast<float> (sampleRate_)));
    cfReleaseCoeff_ = std::exp (-1000.0f / (40.0f * static_cast<float> (sampleRate_)));
}
