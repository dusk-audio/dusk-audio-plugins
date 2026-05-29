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

    input_.prepare    (sampleRate);
    inputNam_.prepare (sampleRate);
    preamp_.prepare (oversampledRate);
    phaseInverter_.prepare (oversampledRate);

    // Three independent power amps — pin each one to its amp type at prepare,
    // then leave the type fixed. Crossfade between them on amp-model switch.
    powerAmpFender_.prepare   (oversampledRate);
    powerAmpVox_.prepare      (oversampledRate);
    powerAmpMarshall_.prepare (oversampledRate);
    powerAmpFender_.setAmpType   (PowerAmp::AmpType::Fender);
    powerAmpVox_.setAmpType      (PowerAmp::AmpType::Vox);
    powerAmpMarshall_.setAmpType (PowerAmp::AmpType::Marshall);

    // Scratch buffer for the target amp's output during a crossfade. Sized
    // for the maximum oversampled block (8× factor cap). Pre-allocated to
    // avoid audio-thread allocation when a fade kicks off.
    ampScratchBuffer_.setSize (1, maxBlockSize * 8, false, true, true);

    cabinet_.prepare (sampleRate, maxBlockSize);
    postFx_.prepare (sampleRate, maxBlockSize);

    // DSP tonestack runs oversampled (sits between preamp and phase inverter
    // at the oversampled rate). NAM tonestack runs at base SR (post-NAM,
    // before cab — NAM itself is non-oversampled).
    toneStack_.prepare    (oversampledRate);
    toneStackNam_.prepare (sampleRate);

#if DUSKAMP_NAM_SUPPORT
    nam_.prepare (sampleRate, maxBlockSize);
#endif
}

void DuskAmpEngine::process (float* left, float* right, int numSamples)
{
    // 1. Sum stereo input to mono (guitar is mono anyway)
    for (int i = 0; i < numSamples; ++i)
        monoBuffer_[static_cast<size_t> (i)] = (left[i] + right[i]) * 0.5f;

    // 2. Input section: gain + noise gate (runs at base rate). Two
    // separate InputSection instances so DSP and NAM modes have
    // independent gain/gate settings — switching modes never carries the
    // wrong gain over and can't blast the user. The crossfade in step 4
    // muting the audio across the actual mode swap, so the change of
    // which input runs is silent.
    if (currentMode_ == AmpMode::NAM)
        inputNam_.process (monoBuffer_.data(), numSamples);
    else
        input_.process    (monoBuffer_.data(), numSamples);

    // Deferred mode switch from crossfade completion
    if (modeSwitchPending_)
    {
        currentMode_ = targetMode_;
        toneStack_.reset();
        toneStackNam_.reset();
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

        // Power-amp dispatch with optional crossfade between OLD and NEW amp
        // models. During a fade, both amps process the same input; outputs
        // are linearly blended over the fade window. Once the fade completes,
        // currentAmpType_ snaps to targetAmpType_ and only that amp runs.
        if (ampCrossfadeSamples_ > 0)
        {
            // Capture the post-PI input for the target amp BEFORE current amp
            // overwrites the buffer in place.
            float* scratch = ampScratchBuffer_.getWritePointer (0);
            std::copy (oversampledData, oversampledData + oversampledNumSamples, scratch);

            ampForType (currentAmpType_).process (oversampledData, oversampledNumSamples);
            ampForType (targetAmpType_).process  (scratch,         oversampledNumSamples);

            // Blend: t goes 0 → 1 as crossfade progresses.
            const float invTotal = 1.0f / static_cast<float> (ampCrossfadeTotalSamples_);
            for (int i = 0; i < oversampledNumSamples; ++i)
            {
                if (ampCrossfadeSamples_ > 0)
                {
                    const float t = 1.0f - static_cast<float> (ampCrossfadeSamples_) * invTotal;
                    oversampledData[i] = oversampledData[i] * (1.0f - t) + scratch[i] * t;
                    --ampCrossfadeSamples_;
                }
                else
                {
                    oversampledData[i] = scratch[i];
                }
            }

            if (ampCrossfadeSamples_ <= 0)
                currentAmpType_ = targetAmpType_;
        }
        else
        {
            ampForType (currentAmpType_).process (oversampledData, oversampledNumSamples);
        }

        oversampling_.processSamplesDown (inputBlock);

        auto* downsampled = oversamplingBuffer_.getReadPointer (0);
        std::copy (downsampled, downsampled + numSamples, monoBuffer_.data());
    }
#if DUSKAMP_NAM_SUPPORT
    else if (currentMode_ == AmpMode::NAM)
    {
        // NAM mode: neural capture already includes the full signal chain
        // (preamp + power amp + often the OT). Running it through our
        // DSP PowerAmp would double-saturate. Use the dedicated NAM
        // tonestack as a post-NAM EQ voicing so the user's bass/mid/treble
        // settings persist independently of the DSP-mode tonestack. Cab
        // IR comes next (step 5).
        nam_.process (monoBuffer_.data(), numSamples);
        toneStackNam_.process (monoBuffer_.data(), numSamples);
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

    // 8. Output gain THEN chain-end safety limiter. Order matters: the
    // output trim is applied first so the limiter is the true final
    // ceiling — a positive trim can never push peaks past it.
    //
    // The limiter soft-caps at `kCeiling` (≈ 0 dBFS). `kCeiling*tanh(x/kCeiling)`
    // has unity slope at 0, so normal playing levels (below ~-6 dBFS) pass
    // essentially unchanged; only transients that would exceed 0 dBFS get
    // softly compressed. This replaces the prior `tanh(x/1.8)*1.8` whose
    // asymptote was +5.1 dBFS — that let cab-convolution / FX transients
    // (and any positive output trim) clip the host. Power amp still does
    // its own per-amp tanh voicing upstream, so this stage only catches
    // true downstream overloads without re-coloring the amp character.
    constexpr float kCeiling = 0.99f;   // -0.1 dBFS
    const float outGain = (currentMode_ == AmpMode::NAM) ? outputGainNam_ : outputGain_;
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = kCeiling * std::tanh (left[i]  * outGain / kCeiling);
        right[i] = kCeiling * std::tanh (right[i] * outGain / kCeiling);
    }
}

void DuskAmpEngine::reset()
{
    input_.reset();
    inputNam_.reset();
    preamp_.reset();
    toneStack_.reset();
    toneStackNam_.reset();
    powerAmpFender_.reset();
    powerAmpVox_.reset();
    powerAmpMarshall_.reset();
    cabinet_.reset();
    postFx_.reset();
    oversampling_.reset();
    ampCrossfadeSamples_ = 0;
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
        // Both tonestacks were prepared at their fixed rates in prepare();
        // mode switch only changes which one runs in process(). No re-prepare
        // needed.
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

void DuskAmpEngine::setInputGainNam (float dB)
{
    inputNam_.setInputGain (dB);
}

void DuskAmpEngine::setGateThresholdNam (float dB)
{
    inputNam_.setGateThreshold (dB);
}

void DuskAmpEngine::setGateReleaseNam (float ms)
{
    inputNam_.setGateRelease (ms);
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

    // Trigger a crossfade between the OLD power-amp instance and the NEW one
    // if the user actually changed amp models. Each amp keeps its own filter
    // state across switches; the fade window blends their outputs so there's
    // no click. Crossfade runs at the oversampled rate where the power amps
    // live (~256 oversampled samples ≈ 1.5 ms at 4× / 0.7 ms at 8×).
    if (paType != currentAmpType_ && ampCrossfadeSamples_ <= 0)
    {
        targetAmpType_ = paType;
        // Reset target's filters so the crossfade starts from a clean state
        // (avoids any DC step from stale state in the long-idle amp).
        ampForType (paType).reset();
        constexpr int kAmpCrossfadeOversampledSamples = 256;
        ampCrossfadeSamples_      = kAmpCrossfadeOversampledSamples;
        ampCrossfadeTotalSamples_ = kAmpCrossfadeOversampledSamples;
    }

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

// --- Tone Stack (NAM) — purely an EQ voicing post-NAM, no amp model coupling ---

void DuskAmpEngine::setToneStackTypeNam (int type)
{
    const int t = std::clamp (type, 0, 2);
    toneStackNam_.setType (static_cast<ToneStack::Type> (t));
}

void DuskAmpEngine::setBassNam (float value01)
{
    toneStackNam_.setBass (value01);
}

void DuskAmpEngine::setMidNam (float value01)
{
    toneStackNam_.setMid (value01);
}

void DuskAmpEngine::setTrebleNam (float value01)
{
    toneStackNam_.setTreble (value01);
}

// --- Power Amp ---
//
// Knob setters dispatch to ALL three power-amp instances so each amp is
// always primed with the user's current settings. When the user switches
// amp models, the new amp's drive/presence/resonance/sag are already up
// to date — no need to re-push the knobs at switch time.

PowerAmp& DuskAmpEngine::ampForType (PowerAmp::AmpType t)
{
    switch (t)
    {
        case PowerAmp::AmpType::Fender:   return powerAmpFender_;
        case PowerAmp::AmpType::Vox:      return powerAmpVox_;
        case PowerAmp::AmpType::Marshall:
        default:                          return powerAmpMarshall_;
    }
}

void DuskAmpEngine::setPowerDrive (float drive01)
{
    powerAmpFender_.setDrive   (drive01);
    powerAmpVox_.setDrive      (drive01);
    powerAmpMarshall_.setDrive (drive01);
}

void DuskAmpEngine::setPresence (float value01)
{
    powerAmpFender_.setPresence   (value01);
    powerAmpVox_.setPresence      (value01);
    powerAmpMarshall_.setPresence (value01);
}

void DuskAmpEngine::setResonance (float value01)
{
    powerAmpFender_.setResonance   (value01);
    powerAmpVox_.setResonance      (value01);
    powerAmpMarshall_.setResonance (value01);
}

void DuskAmpEngine::setSag (float sag01)
{
    powerAmpFender_.setSag   (sag01);
    powerAmpVox_.setSag      (sag01);
    powerAmpMarshall_.setSag (sag01);
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

void DuskAmpEngine::setOutputLevelNam (float dB)
{
    if (dB == prevOutputDBNam_) return;
    prevOutputDBNam_ = dB;
    outputGainNam_ = std::pow (10.0f, dB / 20.0f);
}

// --- Oversampling ---

void DuskAmpEngine::setOversamplingFactor (int factor)
{
    oversampling_.setFactor (factor);

    double oversampledRate = oversampling_.getOversampledSampleRate();
    preamp_.prepare (oversampledRate);
    phaseInverter_.prepare (oversampledRate);
    powerAmpFender_.prepare   (oversampledRate);
    powerAmpVox_.prepare      (oversampledRate);
    powerAmpMarshall_.prepare (oversampledRate);

    // DSP tonestack follows the oversampled rate; NAM tonestack runs at base
    // (NAM is non-oversampled) and isn't affected by the oversampling factor.
    toneStack_.prepare (oversampledRate);
}

int DuskAmpEngine::getLatencyInSamples() const
{
    // Oversampling FIRs add real latency; NAM is reported here so any
    // future resampling wrapper around NAM gets accounted for upstream
    // (today it returns 0). Cab IR uses NonUniform partitioned conv with
    // zero head latency, so nothing to add there.
    return oversampling_.getLatencyInSamples()
         + nam_.getLatencyInSamples();
}
