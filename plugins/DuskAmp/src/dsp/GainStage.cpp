#include "GainStage.h"

void GainStage::configure (const GainStageConfig& config)
{
    config_ = config;
    tube_.setTubeType (config_.tubeType);
    tube_.setBiasPoint (config_.biasPoint);
}

void GainStage::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    tube_.prepare (sampleRate, 1); // mono
    dc_.prepare (sampleRate, 10.0f);
    updateFilterCoefficients();
    reset();
}

void GainStage::reset()
{
    tube_.reset();
    dc_.reset();
    couplingCapState_ = 0.0f;
    cathodeBypassState_ = 0.0f;
    brightCapState_ = 0.0f;
    postLPFState_ = 0.0f;
    postHPFState_ = 0.0f;
    cfEnvelope_ = 0.0f;
    cleanupRMS_ = 0.0f;
}

void GainStage::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    // Map master gain through this stage's drive scale
    tube_.setDrive (gain_ * config_.driveScale);
}

void GainStage::setBright (bool on)
{
    bright_ = on;
}

float GainStage::processSample (float input)
{
    float sample = input;

    // 1. Bright cap boost (highpass-filtered treble added before tube stage)
    if (bright_ && config_.brightCapHz > 0.0f)
    {
        float hpOut = sample - brightCapState_;
        brightCapState_ += hpOut * brightCapCoeff_;
        sample += hpOut * config_.brightCapAmount;
    }

    // 2. Coupling cap HPF (blocks DC, rolls off bass like real amp stages)
    {
        float hpOut = sample - couplingCapState_;
        couplingCapState_ += hpOut * (1.0f - couplingCapCoeff_);
        sample = hpOut;
    }

    // 2.5. Volume cleanup: track input RMS, modulate tube bias/grid-current
    if (config_.biasSensitivity > 0.0f || config_.gridCurrentSensitivity > 0.0f)
    {
        float inputSq = sample * sample;
        if (inputSq > cleanupRMS_)
            cleanupRMS_ = cleanupAttackCoeff_ * cleanupRMS_ + (1.0f - cleanupAttackCoeff_) * inputSq;
        else
            cleanupRMS_ = cleanupReleaseCoeff_ * cleanupRMS_ + (1.0f - cleanupReleaseCoeff_) * inputSq;

        float rms = std::sqrt (cleanupRMS_ + 1e-15f);
        // Map RMS to cleanup amount: high RMS (>0.25) = 0, low RMS = 1 (full cleanup)
        float cleanupAmount = 1.0f - std::clamp (rms * 4.0f, 0.0f, 1.0f);

        tube_.modulateBias (cleanupAmount * config_.biasSensitivity);
        tube_.modulateGridThreshold (cleanupAmount * config_.gridCurrentSensitivity);
    }

    // 3. Tube stage (the main nonlinearity)
    sample = tube_.processSample (sample, 0);

    // 4. DC block
    sample = dc_.processSample (sample);

    // 5. Cathode bypass bass boost
    if (config_.cathodeBypassHz > 0.0f)
    {
        // Lowpass envelope of tube output = bass content
        cathodeBypassState_ += (sample - cathodeBypassState_) * cathodeBypassCoeff_;
        // Add weighted bass boost
        sample += cathodeBypassState_ * config_.cathodeBypassAmount;
    }

    // 6. Cathode follower (unity-gain buffer with envelope-following compression)
    if (config_.hasCathodeFollower)
    {
        float absVal = std::abs (sample);

        if (absVal > cfEnvelope_)
            cfEnvelope_ = cfAttackCoeff_ * cfEnvelope_ + (1.0f - cfAttackCoeff_) * absVal;
        else
            cfEnvelope_ = cfReleaseCoeff_ * cfEnvelope_;

        if (cfEnvelope_ < 1e-15f) cfEnvelope_ = 0.0f;

        // Gain reduction proportional to envelope
        float reduction = 1.0f - config_.cfMaxReduction * std::min (cfEnvelope_, 1.0f);
        sample *= reduction;
    }

    // 7. Inter-stage LPF (tames fizz between cascaded stages)
    if (config_.postStageLPFHz > 0.0f)
    {
        postLPFState_ += postLPFCoeff_ * (sample - postLPFState_);
        sample = postLPFState_;
    }

    // 8. Inter-stage HPF (tightens bass between cascaded stages)
    if (config_.postStageHPFHz > 0.0f)
    {
        float hpOut = sample - postHPFState_;
        postHPFState_ += hpOut * (1.0f - postHPFCoeff_);
        // Blend: mostly highpassed but keep some body
        sample = hpOut * 0.85f + sample * 0.15f;
    }

    // 9. Output attenuation
    sample *= config_.outputAttenuation;

    return sample;
}

void GainStage::updateFilterCoefficients()
{
    float fs = static_cast<float> (sampleRate_);

    // Coupling cap HPF coefficient: exp(-2*pi*fc/fs)
    couplingCapCoeff_ = std::exp (-2.0f * kPi * config_.couplingCapHz / fs);

    // Cathode bypass LPF coefficient: w/(w+1)
    if (config_.cathodeBypassHz > 0.0f)
    {
        float w = 2.0f * kPi * config_.cathodeBypassHz / fs;
        cathodeBypassCoeff_ = w / (w + 1.0f);
    }

    // Bright cap HPF coefficient
    if (config_.brightCapHz > 0.0f)
    {
        float w = 2.0f * kPi * config_.brightCapHz / fs;
        brightCapCoeff_ = w / (w + 1.0f);
    }

    // Inter-stage LPF
    if (config_.postStageLPFHz > 0.0f)
    {
        float w = 2.0f * kPi * config_.postStageLPFHz / fs;
        postLPFCoeff_ = w / (w + 1.0f);
    }

    // Inter-stage HPF: exp(-2*pi*fc/fs)
    if (config_.postStageHPFHz > 0.0f)
    {
        postHPFCoeff_ = std::exp (-2.0f * kPi * config_.postStageHPFHz / fs);
    }

    // Cathode follower envelope coefficients
    if (config_.hasCathodeFollower)
    {
        if (config_.cfAttackMs > 0.0f)
            cfAttackCoeff_ = std::exp (-1000.0f / (config_.cfAttackMs * fs));
        else
            cfAttackCoeff_ = 0.0f;

        if (config_.cfReleaseMs > 0.0f)
            cfReleaseCoeff_ = std::exp (-1000.0f / (config_.cfReleaseMs * fs));
        else
            cfReleaseCoeff_ = 0.0f;
    }

    // Volume cleanup RMS envelope coefficients
    if (config_.biasSensitivity > 0.0f || config_.gridCurrentSensitivity > 0.0f)
    {
        float attackMs  = 20.0f + config_.cleanupSpeed * 100.0f;   // 20-120ms
        float releaseMs = 50.0f + config_.cleanupSpeed * 300.0f;   // 50-350ms
        cleanupAttackCoeff_  = std::exp (-1000.0f / (attackMs * fs));
        cleanupReleaseCoeff_ = std::exp (-1000.0f / (releaseMs * fs));
    }
}
