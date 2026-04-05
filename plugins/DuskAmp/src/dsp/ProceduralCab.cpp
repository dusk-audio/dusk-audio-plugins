#include "ProceduralCab.h"

static constexpr float kPi = 3.14159265358979323846f;

void ProceduralCab::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    updateSpeakerCoefficients();
    updateMicCoefficients();
    updateBodyCoefficients();
    reset();
}

void ProceduralCab::reset()
{
    speakerResonance_.reset();
    speakerRolloff1_.reset();
    speakerRolloff2_.reset();
    micPresence_.reset();
    micDarkening_.reset();
    bodyResonance_.reset();
    bodyLowShelf_.reset();
}

void ProceduralCab::setAmpType (AmpType type)
{
    switch (type)
    {
        case AmpType::FenderDeluxe:
            speakerType_ = SpeakerType::BrightJensen;
            cabBody_ = CabBodyType::Open1x12;
            break;
        case AmpType::VoxAC30:
            speakerType_ = SpeakerType::ChimeyBlue;
            cabBody_ = CabBodyType::Open2x12;
            break;
        case AmpType::MarshallPlexi:
            speakerType_ = SpeakerType::WarmGreenback;
            cabBody_ = CabBodyType::Closed4x12;
            break;
        default:
            speakerType_ = SpeakerType::WarmGreenback;
            cabBody_ = CabBodyType::Closed4x12;
            break;
    }

    updateSpeakerCoefficients();
    updateBodyCoefficients();
}

void ProceduralCab::setSpeakerType (int type)
{
    speakerType_ = static_cast<SpeakerType> (
        std::clamp (type, 0, static_cast<int> (SpeakerType::kNumSpeakers) - 1));
    updateSpeakerCoefficients();
}

void ProceduralCab::setMicPosition (float position01)
{
    micPosition_ = std::clamp (position01, 0.0f, 1.0f);
    updateMicCoefficients();
}

void ProceduralCab::process (float* buffer, int numSamples)
{
    if (!enabled_) return;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // 1. Speaker resonance (cone resonance peak at ~100Hz)
        sample = speakerResonance_.process (sample);

        // 2. Speaker HF rolloff (4th-order Butterworth via cascaded biquads)
        sample = speakerRolloff1_.process (sample);
        sample = speakerRolloff2_.process (sample);

        // 3. Speaker breakup (soft clipping when cone is pushed hard)
        if (std::abs (sample) > breakupThreshold_)
        {
            float excess = std::abs (sample) - breakupThreshold_;
            float softClip = breakupThreshold_ + std::tanh (excess * 3.0f) * breakupAmount_;
            sample = (sample >= 0.0f) ? softClip : -softClip;
        }

        // 4. Mic model — bypassed (knob hidden, can re-enable later)
        // float onAxis = micPresence_.process (sample);
        // float offAxis = micDarkening_.process (sample);
        // sample = onAxis * micPosition_ + offAxis * (1.0f - micPosition_);

        // 5. Cabinet body resonance
        sample = bodyResonance_.process (sample);
        sample = bodyLowShelf_.process (sample);

        buffer[i] = sample;
    }
}

void ProceduralCab::updateSpeakerCoefficients()
{
    float resonanceHz, resonanceGainDB, resonanceQ;
    float rolloffHz;

    switch (speakerType_)
    {
        case SpeakerType::BrightJensen:
            resonanceHz = 90.0f;
            resonanceGainDB = 3.0f;
            resonanceQ = 1.2f;
            rolloffHz = 5000.0f;
            breakupThreshold_ = 0.8f;
            breakupAmount_ = 0.03f;
            break;

        case SpeakerType::WarmGreenback:
            resonanceHz = 100.0f;
            resonanceGainDB = 4.0f;
            resonanceQ = 1.0f;
            rolloffHz = 4500.0f;
            breakupThreshold_ = 0.7f;
            breakupAmount_ = 0.05f;
            break;

        case SpeakerType::AggressiveV:
            resonanceHz = 110.0f;
            resonanceGainDB = 3.5f;
            resonanceQ = 1.1f;
            rolloffHz = 4000.0f;
            breakupThreshold_ = 0.65f;
            breakupAmount_ = 0.06f;
            break;

        case SpeakerType::ChimeyBlue:
            resonanceHz = 85.0f;
            resonanceGainDB = 5.0f;
            resonanceQ = 1.5f;
            rolloffHz = 5500.0f;
            breakupThreshold_ = 0.75f;
            breakupAmount_ = 0.04f;
            break;

        default:
            resonanceHz = 100.0f;
            resonanceGainDB = 4.0f;
            resonanceQ = 1.0f;
            rolloffHz = 4500.0f;
            breakupThreshold_ = 0.7f;
            breakupAmount_ = 0.05f;
            break;
    }

    computePeaking (speakerResonance_, resonanceHz, resonanceGainDB, resonanceQ);

    // Cascaded 2nd-order LPFs = 4th-order Butterworth rolloff
    // Use Q = 0.707 for each section (Butterworth cascade)
    computeLPF (speakerRolloff1_, rolloffHz, 0.54f); // Butterworth Q stage 1
    computeLPF (speakerRolloff2_, rolloffHz, 1.31f); // Butterworth Q stage 2
}

void ProceduralCab::updateMicCoefficients()
{
    // On-axis: SM57-style presence peak at 5.5kHz
    float presenceGainDB = 4.0f * micPosition_; // More peak when more on-axis
    computePeaking (micPresence_, 5500.0f, presenceGainDB, 2.0f);

    // Off-axis: gentle LPF darkening
    float darkeningFreq = 3500.0f + micPosition_ * 4000.0f; // 3.5kHz off-axis → 7.5kHz on-axis
    computeLPF (micDarkening_, darkeningFreq, 0.707f);
}

void ProceduralCab::updateBodyCoefficients()
{
    float bodyHz, bodyGainDB, bodyQ;
    float lowShelfHz, lowShelfGainDB;

    switch (cabBody_)
    {
        case CabBodyType::Open1x12:
            bodyHz = 180.0f;
            bodyGainDB = 2.0f;
            bodyQ = 0.8f;
            lowShelfHz = 120.0f;
            lowShelfGainDB = -3.0f;  // Open-back bass cut
            break;

        case CabBodyType::Open2x12:
            bodyHz = 160.0f;
            bodyGainDB = 2.5f;
            bodyQ = 0.9f;
            lowShelfHz = 100.0f;
            lowShelfGainDB = -1.5f;  // Less bass cut than 1x12
            break;

        case CabBodyType::Closed4x12:
            bodyHz = 140.0f;
            bodyGainDB = 3.5f;
            bodyQ = 1.0f;
            lowShelfHz = 80.0f;
            lowShelfGainDB = 0.0f;   // No bass cut — sealed cabinet
            break;

        default:
            bodyHz = 140.0f;
            bodyGainDB = 3.0f;
            bodyQ = 1.0f;
            lowShelfHz = 100.0f;
            lowShelfGainDB = 0.0f;
            break;
    }

    computePeaking (bodyResonance_, bodyHz, bodyGainDB, bodyQ);
    computeLowShelf (bodyLowShelf_, lowShelfHz, lowShelfGainDB, 0.707f);
}

// ============================================================================
// Audio EQ Cookbook filter design
// ============================================================================

void ProceduralCab::computePeaking (Biquad& bq, float freq, float gainDB, float Q)
{
    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float A = std::pow (10.0f, gainDB / 40.0f);
    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha / A;
    if (std::abs (a0) < 1e-7f) { bq.b0 = 1.0f; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0f; return; }

    bq.b0 = (1.0f + alpha * A) / a0;
    bq.b1 = (-2.0f * cosw0) / a0;
    bq.b2 = (1.0f - alpha * A) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha / A) / a0;
}

void ProceduralCab::computeLPF (Biquad& bq, float freq, float Q)
{
    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha;
    if (std::abs (a0) < 1e-7f) { bq.b0 = 1.0f; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0f; return; }

    bq.b0 = ((1.0f - cosw0) / 2.0f) / a0;
    bq.b1 = (1.0f - cosw0) / a0;
    bq.b2 = ((1.0f - cosw0) / 2.0f) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
}

void ProceduralCab::computeLowShelf (Biquad& bq, float freq, float gainDB, float Q)
{
    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float A = std::pow (10.0f, gainDB / 40.0f);
    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqrtA = std::sqrt (A);

    float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
    if (std::abs (a0) < 1e-7f) { bq.b0 = 1.0f; bq.b1 = bq.b2 = bq.a1 = bq.a2 = 0.0f; return; }

    bq.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha)) / a0;
    bq.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
    bq.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha)) / a0;
    bq.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
    bq.a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
}
