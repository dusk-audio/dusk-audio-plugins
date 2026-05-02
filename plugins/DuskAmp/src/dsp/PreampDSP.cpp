#include "PreampDSP.h"
#include <cmath>
#include <algorithm>

void PreampDSP::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    for (int i = 0; i < kMaxStages; ++i)
    {
        stages_[i].setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
        stages_[i].prepare (sampleRate, 1); // mono
        interStageDC_[i].prepare (sampleRate, 10.0f);
    }

    updateCouplingCapCoeff();
    updateBrightCoeff();
    updateMarshallVoicingCoeffs();
    updateCathodeEnvCoeffs();
    updateGainStaging();
    reset();
}

void PreampDSP::reset()
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        stages_[i].reset();
        interStageDC_[i].reset();
        couplingCapState_[i] = 0.0f;
    }

    brightBoostState_   = 0.0f;
    cathodeShelfState_  = 0.0f;
    jumperLpfState_     = 0.0f;
    cathodeEnv_         = 0.0f;
}

void PreampDSP::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    updateGainStaging();
}

void PreampDSP::setChannel (Channel ch)
{
    currentChannel_ = ch;

    switch (ch)
    {
        case Channel::Clean:  numActiveStages_ = 1; break;
        case Channel::Crunch: numActiveStages_ = 2; break;
        case Channel::Lead:   numActiveStages_ = 3; break;
    }

    updateGainStaging();
}

void PreampDSP::setBright (bool on)
{
    bright_ = on;
}

void PreampDSP::setMarshallVoicing (bool on)
{
    marshallVoicing_ = on;
}

void PreampDSP::process (float* buffer, int numSamples)
{
    // Bright-cap mix amount scales with gain knob: at low gain (volume pot
    // far from the cap end) the cap bypasses MORE of the pot's resistance
    // → the bright effect is strongest. At high gain (wiper near the cap
    // end) the cap has less effect → minimal bright boost. This is the
    // hallmark Marshall (and Fender to a lesser extent) "you only hear the
    // bright cap at low volumes" interaction.
    const float brightMix = bright_ ? (0.45f * (1.0f - 0.7f * gain_)) : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // Bright cap (gain-modulated): mix HPF of input before the first stage
        if (bright_)
        {
            float hpOut = sample - brightBoostState_;
            brightBoostState_ += hpOut * brightBoostCoeff_;
            sample += hpOut * brightMix;
        }

        // Marshall-only voicing: V1A cathode-bypass cap (LF cut at ~285 Hz)
        // and channel-jumper LM mix (Channel-II low-mid content blended in).
        // Applied here before the tube stages so the saturation sees the
        // full Marshall-flavoured signal.
        if (marshallVoicing_)
        {
            // Envelope follower for the dynamic-cap behaviour. Real V1A
            // bypass caps slowly charge with sustained signal — once
            // charged the cap is closer to a short and the LF shelf
            // flattens, giving the "compression on transients, bloom on
            // sustain" feel. Track |sample| with slow attack/release
            // matching cap RC.
            const float instLevel = std::abs (sample);
            const float coeff = (instLevel > cathodeEnv_)
                ? cathodeEnvAttackCoeff_ : cathodeEnvReleaseCoeff_;
            cathodeEnv_ += (instLevel - cathodeEnv_) * coeff;
            if (cathodeEnv_ < 1.0e-15f) cathodeEnv_ = 0.0f;

            // Map env (0..~0.4 typical playing) to a shelf-cut multiplier
            // around the static 0.55 baseline. envScaled clamped to [0,1].
            // Sustained loud (envScaled=1) → cut = 0.41 (≈ -4.7 dB LF).
            // Silent / transient (envScaled=0) → cut = 0.55 (≈ -7.0 dB LF).
            // ~2 dB LF dynamic — subtle but audible "chord-bloom" character.
            const float envScaled = std::min (cathodeEnv_ * 2.5f, 1.0f);
            const float dynamicCut = 0.55f * (1.0f - 0.25f * envScaled);

            // Static 285 Hz LPF state (the cap's voltage) — subtract from
            // signal to produce the low-shelf cut.
            cathodeShelfState_ += cathodeShelfCoeff_ * (sample - cathodeShelfState_);
            if (std::abs (cathodeShelfState_) < 1e-15f) cathodeShelfState_ = 0.0f;
            sample -= cathodeShelfState_ * dynamicCut;

            // Channel-jumper: add 25% of a low-passed (~600 Hz) input as
            // Channel-II low-mid content. Both bright (HF emphasis) and
            // jumper (LM emphasis) signals end up in the same tube — the
            // sum-then-saturate behaviour of the real Hendrix-trick patch
            // bridge.
            jumperLpfState_ += jumperLpfCoeff_ * (buffer[i] - jumperLpfState_);
            if (std::abs (jumperLpfState_) < 1e-15f) jumperLpfState_ = 0.0f;
            sample += jumperLpfState_ * 0.25f;
        }

        // Process through each active tube stage
        for (int stage = 0; stage < numActiveStages_; ++stage)
        {
            // Coupling cap highpass (removes DC, rolls off bass like a real amp)
            float hpOut = sample - couplingCapState_[stage];
            couplingCapState_[stage] += hpOut * (1.0f - couplingCapCoeff_);
            sample = hpOut;

            // Tube stage (mono, channel 0)
            sample = stages_[stage].processSample (sample, 0);

            // DC block between stages
            sample = interStageDC_[stage].processSample (sample);
        }

        buffer[i] = sample * outputMakeup_;
    }
}

void PreampDSP::updateGainStaging()
{
    // Distribute gain across active stages.
    // Real preamp tubes have ~35-70x voltage gain per stage, but we're
    // working with normalized ±1 signals. The drive parameter controls
    // how hard each stage is hit, but we need to keep the overall output
    // reasonable so the power amp (and eventually the DAC) isn't clipped.
    //
    // At gain=0 (clean), the preamp should add minimal distortion (<5% THD).
    // At gain=1 (full), significant harmonic content is expected.
    //
    // Key: TubeEmulation::setDrive maps to inputGain = 1 + drive*2,
    // and outputScaling = 0.8 for 12AX7. So at drive=0, throughput ≈ 0.8x.
    // Multiple stages cascade this gain, so we need lower per-stage drive
    // values to keep the total chain under control.

    // Makeup gains chosen so post-preamp peak at moderate drive sits roughly at
    // Clean=0.35, Crunch=0.65, Lead=1.0 — giving Lead more headroom loss into
    // the waveshaper downstream, matching user expectation that higher-gain
    // channels sound both louder and more saturated.
    //
    // The gain knob also scales the channel's final makeup so it acts like a
    // real amp's volume control (not just a "saturation amount" knob). Range
    // chosen for ~20 dB of useful knob travel: gain=0 → 10% of max (quiet but
    // not silent), gain=1 → full channel makeup (matches the previous fixed
    // value so THD calibration stays valid at the top of the knob).
    const float gainMakeup = 0.1f + gain_ * 0.9f;

    switch (currentChannel_)
    {
        case Channel::Clean:
            stages_[0].setDrive (gain_ * 0.25f);
            outputMakeup_ = 2.0f * gainMakeup;
            break;

        case Channel::Crunch:
            stages_[0].setDrive (gain_ * 0.15f);
            stages_[1].setDrive (gain_ * 0.35f);
            outputMakeup_ = 4.0f * gainMakeup;
            break;

        case Channel::Lead:
            stages_[0].setDrive (gain_ * 0.12f);
            stages_[1].setDrive (gain_ * 0.25f);
            stages_[2].setDrive (gain_ * 0.5f);
            outputMakeup_ = 8.0f * gainMakeup;
            break;
    }
}

void PreampDSP::updateCouplingCapCoeff()
{
    // Coupling cap HPF: ~30Hz cutoff
    // coeff = exp(-2*pi*fc/fs)
    float fc = 30.0f;
    couplingCapCoeff_ = std::exp (-2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_));
}

void PreampDSP::updateBrightCoeff()
{
    // Bright cap HPF: ~1.5kHz cutoff for treble boost
    float fc = 1500.0f;
    float w = 2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_);
    brightBoostCoeff_ = w / (w + 1.0f);
}

void PreampDSP::updateMarshallVoicingCoeffs()
{
    // Cathode-bypass shelf corner: 285 Hz (set by V1A's 0.68 µF on 820 Ω).
    {
        const float fc = 285.0f;
        const float w = 2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_);
        cathodeShelfCoeff_ = w / (w + 1.0f);
    }
    // Jumper LM corner: ~600 Hz — matches Channel-II's perceived low-mid
    // emphasis (warmth without muddy bass).
    {
        const float fc = 600.0f;
        const float w = 2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_);
        jumperLpfCoeff_ = w / (w + 1.0f);
    }
}

void PreampDSP::updateCathodeEnvCoeffs()
{
    // 50 ms attack — the cap charges at ~5τ for full charge, but audibly
    // most of the bloom kicks in within the first τ.
    // 200 ms release — roughly the discharge through the next-stage's
    // grid-leak resistor (typically ~1 MΩ × 0.68 μF ≈ 680 ms full RC,
    // but we hear about ~1τ before perceived "back to clean").
    const float attackMs  = 50.0f;
    const float releaseMs = 200.0f;
    const float sr = static_cast<float> (sampleRate_);
    cathodeEnvAttackCoeff_  = 1.0f - std::exp (-1000.0f / (attackMs  * sr));
    cathodeEnvReleaseCoeff_ = 1.0f - std::exp (-1000.0f / (releaseMs * sr));
}
