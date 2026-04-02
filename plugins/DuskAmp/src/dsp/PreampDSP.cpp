#include "PreampDSP.h"
#include <cmath>
#include <algorithm>

// =========================================================================
// Per-model stage configurations from real amp schematics
// =========================================================================

// Round (Fender Deluxe Reverb AB763 Vibrato channel): 3x gain stages
// V2A: 100k plate, 1.5kΩ cathode + 25µF bypass → fc ≈ 4.2 Hz, 47nF coupling, 1MΩ grid leak
// V2B: 100k plate, 820Ω cathode + 25µF bypass → fc ≈ 7.7 Hz, 20nF coupling
// V4B: reverb recovery / extra gain stage, 100k plate, 0.1µF coupling, 220kΩ grid leak
static constexpr PreampDSP::StageConfig kRoundStages[3] = {
    { 0.047e-6f, 1e6f,    4.2f, 6.0f, 0.30f },    // V2A: 47nF coupling, 1.5kΩ + 25µF bypass
    { 0.02e-6f,  1e6f,    7.7f, 3.0f, 0.40f },    // V2B: 20nF coupling, 820Ω + 25µF bypass
    { 0.1e-6f,   220e3f,  4.2f, 4.0f, 0.50f }     // V4B: 100nF coupling, 220kΩ grid leak
};

// Chime (Vox AC30 Top Boost 1964): 3x ECC83 (12AX7)
// V1:  220k plate, 1.5kΩ cathode + 25µF bypass → fc ≈ 4.2 Hz, 47nF coupling, 1MΩ grid leak
// V2A: 220k plate, 1.8kΩ cathode, NO bypass cap (cathode degeneration, ~30dB gain)
// V2B: cathode follower (buffer, ~unity gain, ~600Ω output Z), drives Top Boost tone circuit
static constexpr PreampDSP::StageConfig kChimeStages[3] = {
    { 0.047e-6f, 1e6f,   4.2f, 6.0f, 0.35f },    // V1: 47nF coupling, bypassed cathode
    { 0.047e-6f, 1e6f,   0.0f, 0.0f, 0.55f },    // V2A: 47nF coupling, unbypassed cathode
    { 0.047e-6f, 1e6f,   0.0f, 0.0f, 0.10f }     // V2B: cathode follower (buffer, low drive)
};

// Punch (Marshall 1959 Super Lead Plexi): 3x ECC83 (12AX7)
// V1:  100k plate, 820Ω cathode + 250µF bypass → fc ≈ 0.78 Hz, 22nF coupling, 1MΩ grid leak
// V2A: 100k plate, 2.7kΩ cathode + 0.68µF bypass → fc ≈ 87 Hz (tight low-end character)
// V2B: cathode follower (buffer, ~unity gain), 22nF coupling to tone stack
static constexpr PreampDSP::StageConfig kPunchStages[3] = {
    { 0.022e-6f, 1e6f,   0.78f, 6.0f, 0.35f },    // V1: 22nF coupling, 820Ω + 250µF bypass
    { 0.022e-6f, 1e6f,   87.0f, 6.0f, 0.55f },    // V2A: 22nF coupling, 2.7kΩ + 0.68µF bypass
    { 0.0f,      0.0f,   0.0f,  0.0f, 0.10f }     // V2B: cathode follower (DC coupled, buffer)
};

// =========================================================================
// Lifecycle
// =========================================================================

void PreampDSP::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    for (int i = 0; i < kMaxStages; ++i)
    {
        stages_[i].setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
        stages_[i].prepare (sampleRate, 1);
        interStageDC_[i].prepare (sampleRate, 10.0f);
    }

    // Pre-compute Koren LUTs for all 3 models (expensive, but only at prepare time)
    precomputeAllKorenLUTs();

    setAmpModel (currentModel_);
    reset();
}

void PreampDSP::reset()
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        stages_[i].reset();
        interStageDC_[i].reset();
        couplingCapState_[i] = 0.0f;
        cathodeShelfState_[i] = 0.0f;
    }

    brightFilter_.reset();
    trebleBleedState_ = 0.0f;
}

// =========================================================================
// Parameter setters
// =========================================================================

void PreampDSP::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    updateGainStaging();
    updateTrebleBleed();
}

void PreampDSP::setSagMultiplier (float sag)
{
    float newSag = std::clamp (sag, 0.5f, 1.0f);
    if (std::abs (newSag - sagMultiplier_) < 0.001f)
        return;  // hysteresis — don't rebuild gain staging for tiny changes
    sagMultiplier_ = newSag;
    updateGainStaging();
    updateBiasFromSag();
}

void PreampDSP::setAmpModel (AmpModel model)
{
    currentModel_ = model;

    const StageConfig* configs = nullptr;

    switch (model)
    {
        case AmpModel::Round:
            numActiveStages_ = 3;
            configs = kRoundStages;
            brightActive_ = true;
            brightFreq_ = 1500.0f;
            brightGainDB_ = 3.0f;
            brightQ_ = 1.5f;
            trebleBleedMaxDB_ = 3.0f;
            sagBiasScale_ = 0.10f;     // GZ34 tube rectifier: noticeable bloom
            break;

        case AmpModel::Chime:
            numActiveStages_ = 3;
            configs = kChimeStages;
            brightActive_ = true;
            brightFreq_ = 2000.0f;
            brightGainDB_ = 2.5f;
            brightQ_ = 1.5f;
            trebleBleedMaxDB_ = 2.0f;
            sagBiasScale_ = 0.08f;     // GZ34: moderate bloom
            break;

        case AmpModel::Punch:
            numActiveStages_ = 3;
            configs = kPunchStages;
            brightActive_ = true;
            brightFreq_ = 1500.0f;
            brightGainDB_ = 3.0f;
            brightQ_ = 1.5f;
            trebleBleedMaxDB_ = 1.5f;
            sagBiasScale_ = 0.02f;     // solid-state rectifier: minimal shift
            break;
    }

    for (int i = 0; i < kMaxStages; ++i)
        stageConfigs_[i] = configs[i];

    updateBrightCoeff();
    updateTrebleBleed();
    updateCouplingCapCoeffs();
    updateCathodeBypassCoeffs();
    updateGainStaging();
}

// =========================================================================
// Processing
// =========================================================================

void PreampDSP::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // Bright cap: resonant peak from capacitor across volume pot
        if (brightActive_)
            sample = brightFilter_.process (sample);

        // Treble bleed: compensate for HF loss at higher drive
        if (trebleBleedMix_ > 0.001f)
        {
            float hpOut = sample - trebleBleedState_;
            trebleBleedState_ += hpOut * trebleBleedCoeff_;
            sample += hpOut * trebleBleedMix_;
        }

        // Process through each active tube stage
        for (int stage = 0; stage < numActiveStages_; ++stage)
        {
            // Coupling cap HPF (per-stage, per-model frequency)
            couplingCapState_[stage] += (sample - couplingCapState_[stage]) * (1.0f - couplingCapCoeff_[stage]);
            sample = sample - couplingCapState_[stage];

            // Tube stage
            sample = stages_[stage].processSample (sample, 0);

            // Cathode bypass shelf: attenuate LF relative to HF
            float lf = cathodeShelfState_[stage];
            cathodeShelfState_[stage] += (sample - lf) * cathodeShelfCoeff_[stage];
            float hfContent = sample - lf;
            sample = lf * cathodeShelfAttenuation_[stage] + hfContent;

            // DC block
            sample = interStageDC_[stage].processSample (sample);
        }

        buffer[i] = sample;
    }
}

// =========================================================================
// Internal updates
// =========================================================================

void PreampDSP::precomputeAllKorenLUTs()
{
    // Pre-compute Koren transfer function LUTs for all 3 amp models.
    // This is expensive (~98K float ops per model) but only runs at prepare time.
    // 12AX7 Koren parameters: mu=100, Kp=600, Kvb=300, Ex=1.4, Kg1=1060

    struct ModelCircuit { float Vb; float Rp; };
    static constexpr ModelCircuit circuits[3] = {
        { 280.0f, 100000.0f },  // Round (Fender AB763): B+=280V, Rp=100kΩ
        { 320.0f, 220000.0f },  // Chime (Vox AC30): B+=320V, Rp=220kΩ
        { 320.0f, 100000.0f }   // Punch (Marshall Plexi): B+=320V, Rp=100kΩ
    };

    AnalogEmulation::TubeEmulation tempStage;
    tempStage.setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);

    for (int m = 0; m < 3; ++m)
    {
        tempStage.initializeKorenTransferFunction (100.0f, 600.0f, 300.0f, 1.4f, 1060.0f,
                                                    circuits[m].Vb, circuits[m].Rp, 0.0f);
        std::copy (tempStage.getPlateTransferTable(),
                   tempStage.getPlateTransferTable() + kLUTSize,
                   korenLUTs_[m]);
    }

    lutsReady_ = true;

    // Apply current model's LUT to all active stages
    applyPrecomputedLUT (static_cast<int> (currentModel_));
}

void PreampDSP::applyPrecomputedLUT (int modelIndex)
{
    if (! lutsReady_) return;
    int idx = std::clamp (modelIndex, 0, 2);
    for (int i = 0; i < numActiveStages_; ++i)
        stages_[i].setPlateTransferTable (korenLUTs_[idx]);
}

void PreampDSP::initializeKorenLUTs()
{
    // Now just applies pre-computed LUT (no-op if called before prepare)
    applyPrecomputedLUT (static_cast<int> (currentModel_));
}

void PreampDSP::updateGainStaging()
{
    // Sag reduces available gain (lower B+ = less headroom before clipping)
    float saggedDrive = drive_ * sagMultiplier_;
    for (int i = 0; i < numActiveStages_; ++i)
        stages_[i].setDrive (saggedDrive * stageConfigs_[i].driveScale);
}

void PreampDSP::updateBiasFromSag()
{
    // When B+ drops (sag), the tube's quiescent point shifts, changing clipping
    // asymmetry and adding even harmonics. This creates the "bloomy" feel of
    // tube rectifier amps at high drive.
    float sagAmount = 1.0f - sagMultiplier_;  // 0.0 = no sag, 0.5 = max sag
    float biasShift = sagAmount * sagBiasScale_;
    for (int i = 0; i < numActiveStages_; ++i)
        stages_[i].setBiasPoint (biasShift);
}

void PreampDSP::updateCouplingCapCoeffs()
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        const auto& cfg = stageConfigs_[i];
        if (cfg.couplingCapF <= 0.0f || cfg.gridLeakR <= 0.0f)
        {
            couplingCapCoeff_[i] = 0.995f;
            continue;
        }

        // HPF cutoff: fc = 1 / (2*pi*C*R)
        float fc = 1.0f / (2.0f * kPi * cfg.couplingCapF * cfg.gridLeakR);
        fc = std::max (fc, 1.0f);
        couplingCapCoeff_[i] = std::exp (-2.0f * kPi * fc / static_cast<float> (sampleRate_));
    }
}

void PreampDSP::updateBrightCoeff()
{
    // Peaking EQ biquad (Audio EQ Cookbook) — models the resonant peak
    // created by a bright cap across the volume pot
    float A  = std::pow (10.0f, brightGainDB_ / 40.0f);
    float w0 = 2.0f * kPi * brightFreq_ / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * brightQ_);

    float a0 = 1.0f + alpha / A;
    if (std::abs (a0) < 1e-7f)
    {
        brightFilter_.b0 = 1.0f;
        brightFilter_.b1 = brightFilter_.b2 = 0.0f;
        brightFilter_.a1 = brightFilter_.a2 = 0.0f;
        return;
    }

    brightFilter_.b0 = (1.0f + alpha * A) / a0;
    brightFilter_.b1 = (-2.0f * cosw0) / a0;
    brightFilter_.b2 = (1.0f - alpha * A) / a0;
    brightFilter_.a1 = (-2.0f * cosw0) / a0;
    brightFilter_.a2 = (1.0f - alpha / A) / a0;
}

void PreampDSP::updateTrebleBleed()
{
    // Subtle HF boost proportional to drive (compensates for Miller cap rolloff).
    // Keep the mix very gentle: 0 at drive=0, ~0.12 at drive=1 for Round (3dB max).
    float gainDB = drive_ * trebleBleedMaxDB_;
    trebleBleedMix_ = gainDB * 0.04f;  // ~0.12 max for 3dB, ~0.06 for 1.5dB

    float fc = 2000.0f;
    float w = 2.0f * kPi * fc / static_cast<float> (sampleRate_);
    trebleBleedCoeff_ = w / (w + 1.0f);
}

void PreampDSP::updateCathodeBypassCoeffs()
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        const auto& cfg = stageConfigs_[i];
        if (cfg.cathodeBypassHz <= 0.0f)
        {
            cathodeShelfCoeff_[i] = 0.0f;
            cathodeShelfAttenuation_[i] = 1.0f;
            continue;
        }

        // One-pole lowpass coefficient for the shelf corner frequency
        float w = 2.0f * kPi * cfg.cathodeBypassHz / static_cast<float> (sampleRate_);
        cathodeShelfCoeff_[i] = w / (w + 1.0f);

        // Below the bypass frequency, gain is reduced by cathode degeneration
        // Convert dB boost to LF attenuation factor
        cathodeShelfAttenuation_[i] = std::pow (10.0f, -cfg.cathodeBypassDB / 20.0f);
    }
}
