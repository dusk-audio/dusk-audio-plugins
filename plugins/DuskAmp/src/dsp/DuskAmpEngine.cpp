#include "DuskAmpEngine.h"
#include <cmath>
#include <algorithm>

void DuskAmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Resize mono scratch buffer
    // Account for oversampling: the upsampled block can be up to 8x larger
    monoBuffer_.resize (static_cast<size_t> (maxBlockSize * 8), 0.0f);

    // Pre-allocate AudioBuffers used in process() to avoid heap allocation on the audio thread
    oversamplingBuffer_.setSize (1, maxBlockSize, false, true, true);
    cabBuffer_.setSize (1, maxBlockSize, false, true, true);

    // Prepare oversampling (mono, 1 channel)
    oversampling_.prepare (sampleRate, maxBlockSize, 1);

    // Prepare all sub-components at the oversampled rate for the nonlinear stages
    double oversampledRate = oversampling_.getOversampledSampleRate();

    input_.prepare (sampleRate);
    preamp_.prepare (oversampledRate);
    phaseInverter_.prepare (oversampledRate);
    powerAmp_.prepare (oversampledRate);
    cabinet_.prepare (sampleRate, maxBlockSize);
    postFx_.prepare (sampleRate, maxBlockSize);

    // Tone stack rate depends on amp mode: oversampled in DSP, base rate in NAM
    if (currentMode_ == AmpMode::NAM)
        toneStack_.prepare (sampleRate);
    else
        toneStack_.prepare (oversampledRate);

#if DUSKAMP_NAM_SUPPORT
    nam_.prepare (sampleRate, maxBlockSize);
#endif
}

void DuskAmpEngine::process (float* left, float* right, int numSamples)
{
    // 1. Sum stereo input to mono (guitar is mono anyway)
    for (int i = 0; i < numSamples; ++i)
        monoBuffer_[static_cast<size_t> (i)] = (left[i] + right[i]) * 0.5f;

    // 2. Input section: gain + noise gate (runs at base rate)
    input_.process (monoBuffer_.data(), numSamples);

    // Deferred mode switch from crossfade completion
    if (modeSwitchPending_)
    {
        currentMode_ = targetMode_;
        toneStack_.reset();
        modeSwitchPending_ = false;
    }

    // 3. Mode-dependent amp processing
    if (currentMode_ == AmpMode::DSP)
    {
        // DSP mode: upsample -> preamp -> tone stack -> power amp -> downsample
        oversamplingBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);

        // AudioBlock must wrap only the active numSamples, not the full pre-allocated buffer
        juce::dsp::AudioBlock<float> inputBlock (oversamplingBuffer_.getArrayOfWritePointers(),
                                                  1, static_cast<size_t> (numSamples));
        auto oversampledBlock = oversampling_.processSamplesUp (inputBlock);

        int oversampledNumSamples = static_cast<int> (oversampledBlock.getNumSamples());
        float* oversampledData = oversampledBlock.getChannelPointer (0);

        preamp_.process (oversampledData, oversampledNumSamples);
        toneStack_.process (oversampledData, oversampledNumSamples);
        phaseInverter_.process (oversampledData, oversampledNumSamples);
        powerAmp_.process (oversampledData, oversampledNumSamples);

        oversampling_.processSamplesDown (inputBlock);

        auto* downsampled = oversamplingBuffer_.getReadPointer (0);
        std::copy (downsampled, downsampled + numSamples, monoBuffer_.data());
    }
#if DUSKAMP_NAM_SUPPORT
    else if (currentMode_ == AmpMode::NAM)
    {
        // NAM mode: neural capture already includes the full signal chain
        // (preamp + power amp + often the OT). Running it through our
        // DSP PowerAmp would double-saturate. Keep the tone stack as an
        // optional post-NAM EQ voicing so the user's bass/mid/treble
        // knobs still do something. Cab IR comes next (step 5).
        nam_.process (monoBuffer_.data(), numSamples);
        toneStack_.process (monoBuffer_.data(), numSamples);
    }
#endif

    // 4. Apply crossfade gain if we're transitioning between modes
    if (crossfadeSamplesRemaining_ > 0)
    {
        for (int i = 0; i < numSamples && crossfadeSamplesRemaining_ > 0; ++i)
        {
            float fadeStep = 2.0f / static_cast<float> (kCrossfadeSamples);

            monoBuffer_[static_cast<size_t> (i)] *= crossfadeGain_;

            crossfadeGain_ += static_cast<float> (crossfadeDirection_) * fadeStep;
            crossfadeGain_ = std::clamp (crossfadeGain_, 0.0f, 1.0f);
            --crossfadeSamplesRemaining_;

            // At the midpoint (gain reached 0), defer mode switch to next process() call
            if (crossfadeDirection_ == -1 && crossfadeGain_ <= 0.0f)
            {
                modeSwitchPending_ = true;  // defer to next process() call
                crossfadeDirection_ = 1; // start fading in
                crossfadeSamplesRemaining_ = kCrossfadeSamples / 2;
            }
        }
    }

    // 5. Cabinet IR (runs at base rate, it's convolution)
    //    Create a sub-buffer view of only the active numSamples to avoid
    //    processing stale data from the pre-allocated cabBuffer_
    cabBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);
    juce::AudioBuffer<float> cabView (cabBuffer_.getArrayOfWritePointers(), 1, numSamples);
    cabinet_.process (cabView);
    std::copy (cabBuffer_.getReadPointer (0),
               cabBuffer_.getReadPointer (0) + numSamples,
               monoBuffer_.data());

    // 6. Copy mono to stereo for post-FX
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = monoBuffer_[static_cast<size_t> (i)];
        right[i] = monoBuffer_[static_cast<size_t> (i)];
    }

    // 7. Post FX: delay + reverb (stereo processing)
    postFx_.process (left, right, numSamples);

    // 8. Final soft-limit + output gain. Power amp already does per-amp
    // tanh limiting at its output, so this is a chain-end SAFETY NET for
    // peak amplification introduced downstream — cab convolution can
    // amplify transients (constructive impulse-response interference),
    // delay feedback can push tail level near 1.0, and reverb wet adds
    // on top of that. Loose K=1.6 so signals up to ~0.8 RMS pass through
    // essentially un-touched and only true overloads (>1.0) get caught.
    constexpr float kLimitK = 1.6f;
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = std::tanh (left[i]  / kLimitK) * kLimitK * outputGain_;
        right[i] = std::tanh (right[i] / kLimitK) * kLimitK * outputGain_;
    }
}

void DuskAmpEngine::reset()
{
    input_.reset();
    preamp_.reset();
    toneStack_.reset();
    powerAmp_.reset();
    cabinet_.reset();
    postFx_.reset();
    oversampling_.reset();
#if DUSKAMP_NAM_SUPPORT
    nam_.reset();
#endif
    std::fill (monoBuffer_.begin(), monoBuffer_.end(), 0.0f);
    crossfadeGain_ = 1.0f;
    crossfadeSamplesRemaining_ = 0;
    crossfadeDirection_ = 0;
    modeSwitchPending_ = false;
}

// --- Mode ---

bool DuskAmpEngine::isNAMModelLoaded() const
{
#if DUSKAMP_NAM_SUPPORT
    return nam_.hasModel();
#else
    return false;
#endif
}

void DuskAmpEngine::setAmpMode (AmpMode mode)
{
    if (mode != currentMode_ && crossfadeSamplesRemaining_ == 0)
    {
        targetMode_ = mode;
        crossfadeSamplesRemaining_ = kCrossfadeSamples;
        crossfadeDirection_ = -1;
        crossfadeGain_ = 1.0f;

        // Pre-prepare tone stack for the target mode's sample rate
        // (safe here — called from processBlock's discrete param section, not per-sample)
        if (mode == AmpMode::NAM)
            toneStack_.prepare (sampleRate_);
        else
            toneStack_.prepare (oversampling_.getOversampledSampleRate());
    }
}

// --- Input ---

void DuskAmpEngine::setInputGain (float dB)
{
    input_.setInputGain (dB);
}

void DuskAmpEngine::setGateThreshold (float dB)
{
    input_.setGateThreshold (dB);
}

void DuskAmpEngine::setGateRelease (float ms)
{
    input_.setGateRelease (ms);
}

// --- Preamp ---

void DuskAmpEngine::setPreampGain (float gain01)
{
    preamp_.setGain (gain01);
}

void DuskAmpEngine::setPreampChannel (int channel)
{
    preamp_.setChannel (static_cast<PreampDSP::Channel> (std::clamp (channel, 0, 2)));
}

void DuskAmpEngine::setPreampBright (bool on)
{
    preamp_.setBright (on);
}

// --- Tone Stack ---

void DuskAmpEngine::setToneStackType (int type)
{
    const int t = std::clamp (type, 0, 2);
    toneStack_.setType (static_cast<ToneStack::Type> (t));

    // Couple the power-amp character to the tone stack family so the user's
    // single AMP/TONE selector picks an actually-different amp model rather
    // than just a tone-stack tweak with a hardcoded Marshall power section.
    //   American → Fender 6V6 push-pull + LTP phase inverter
    //   British  → Marshall EL34 push-pull + LTP phase inverter
    //   AC       → Vox EL84 Class A + cathodyne phase splitter
    PowerAmp::AmpType paType = PowerAmp::AmpType::Fender;
    switch (t)
    {
        case 0: paType = PowerAmp::AmpType::Fender;   break;
        case 1: paType = PowerAmp::AmpType::Marshall; break;
        case 2: paType = PowerAmp::AmpType::Vox;      break;
    }
    powerAmp_.setAmpType (paType);

    // Marshall-only preamp voicing (V1A cathode-bypass shelf + Channel-II
    // jumper LM mix). Other amps run the generic preamp path.
    preamp_.setMarshallVoicing (paType == PowerAmp::AmpType::Marshall);

    // Configure the phase inverter to match the amp's topology. The PI
    // sits between the tonestack and the power-tube waveshaper (where it
    // physically belongs), providing the +30 dB driver gain that used to
    // live as a static postMakeup multiplier inside PowerAmp. Saturating
    // here at heavy drive contributes the harmonics + dynamic compression
    // that real PI clipping adds to a cranked amp.
    switch (paType)
    {
        case PowerAmp::AmpType::Fender:
            phaseInverter_.setTopology (PhaseInverter::Topology::LongTailPair);
            phaseInverter_.setGain (2.5f);     // ≈ +8 dB, replaces postMakeup
            phaseInverter_.setHeadroom (2.2f); // higher headroom: PI stays linear at clean drive,
                                                // 6V6 waveshaper gets the harder signal at cranked
            break;
        case PowerAmp::AmpType::Marshall:
            phaseInverter_.setTopology (PhaseInverter::Topology::LongTailPair);
            phaseInverter_.setGain (4.0f);     // compensates Marshall's 0.25 inputScale
            phaseInverter_.setHeadroom (1.6f); // a touch more headroom — 1959 PI runs higher Vp
            break;
        case PowerAmp::AmpType::Vox:
        default:
            // Cathodyne PI is a single-triode split-load: ~unity differential
            // gain. We use moderate gain + generous headroom so PI stays
            // mostly transparent; the AC30 character comes from the cathode
            // follower (in Top Boost) + the asymmetric EL84 curve seen by
            // Class A push-pull, not from PI clipping. Pushing the PI too
            // hard makes clean settings buzzy.
            phaseInverter_.setTopology (PhaseInverter::Topology::Cathodyne);
            phaseInverter_.setGain (1.6f);
            phaseInverter_.setHeadroom (2.4f);
            break;
    }
}

void DuskAmpEngine::setBass (float value01)
{
    toneStack_.setBass (value01);
}

void DuskAmpEngine::setMid (float value01)
{
    toneStack_.setMid (value01);
}

void DuskAmpEngine::setTreble (float value01)
{
    toneStack_.setTreble (value01);
}

// --- Power Amp ---

void DuskAmpEngine::setPowerDrive (float drive01)
{
    powerAmp_.setDrive (drive01);
}

void DuskAmpEngine::setPresence (float value01)
{
    powerAmp_.setPresence (value01);
}

void DuskAmpEngine::setResonance (float value01)
{
    powerAmp_.setResonance (value01);
}

void DuskAmpEngine::setSag (float sag01)
{
    powerAmp_.setSag (sag01);
}

// --- Cabinet ---

void DuskAmpEngine::setCabinetEnabled (bool on)
{
    cabinet_.setEnabled (on);
}

void DuskAmpEngine::setCabinetMix (float mix01)
{
    cabinet_.setMix (mix01);
}

void DuskAmpEngine::setCabinetHiCut (float hz)
{
    cabinet_.setHiCut (hz);
}

void DuskAmpEngine::setCabinetLoCut (float hz)
{
    cabinet_.setLoCut (hz);
}

void DuskAmpEngine::setCabinetNormalize (bool on)
{
    cabinet_.setNormalize (on);
}

// --- Post FX ---

void DuskAmpEngine::setDelayEnabled (bool on)
{
    postFx_.setDelayEnabled (on);
}

void DuskAmpEngine::setDelayTime (float ms)
{
    postFx_.setDelayTime (ms);
}

void DuskAmpEngine::setDelayFeedback (float fb01)
{
    postFx_.setDelayFeedback (fb01);
}

void DuskAmpEngine::setDelayMix (float mix01)
{
    postFx_.setDelayMix (mix01);
}

void DuskAmpEngine::setReverbEnabled (bool on)
{
    postFx_.setReverbEnabled (on);
}

void DuskAmpEngine::setReverbMix (float mix01)
{
    postFx_.setReverbMix (mix01);
}

void DuskAmpEngine::setReverbDecay (float decay01)
{
    postFx_.setReverbDecay (decay01);
}

// --- NAM ---

#if DUSKAMP_NAM_SUPPORT
void DuskAmpEngine::setNAMInputLevel (float dB)
{
    nam_.setInputLevel (dB);
}

void DuskAmpEngine::setNAMOutputLevel (float dB)
{
    nam_.setOutputLevel (dB);
}
#endif

// --- Output ---

void DuskAmpEngine::setOutputLevel (float dB)
{
    if (dB == prevOutputDB_) return;
    prevOutputDB_ = dB;
    outputGain_ = std::pow (10.0f, dB / 20.0f);
}

// --- Oversampling ---

void DuskAmpEngine::setOversamplingFactor (int factor)
{
    oversampling_.setFactor (factor);

    double oversampledRate = oversampling_.getOversampledSampleRate();
    preamp_.prepare (oversampledRate);
    powerAmp_.prepare (oversampledRate);

    // Tone stack rate depends on current amp mode
    if (currentMode_ == AmpMode::NAM)
        toneStack_.prepare (sampleRate_);
    else
        toneStack_.prepare (oversampledRate);
}

int DuskAmpEngine::getLatencyInSamples() const
{
    return oversampling_.getLatencyInSamples();
}
