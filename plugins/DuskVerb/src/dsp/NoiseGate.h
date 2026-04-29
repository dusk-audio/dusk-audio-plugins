#pragma once

#include <algorithm>
#include <cmath>

// NoiseGate — classic outboard-rig noise gate with sidechain-style trigger.
//
// Modeled after the SSL G-bus / drawmer-style hardware gates used on the
// 1980s big-snare records. The trigger envelope follower watches the DRY
// input (which IS the sidechain in our single-plugin context). When the
// dry signal crosses the threshold, the gate opens fast, holds for HOLD
// time, then releases over RELEASE time. The gate is applied to the wet
// reverb output — so the listener hears a long lush hall *shaped* by the
// dry-snare envelope.
//
// State machine:
//   Closed  : gain = reduction. trigger > threshold → enter Attack.
//   Attack  : ramp gain reduction → 1.0 over attackSamples_. → Open.
//   Open    : gain = 1.0. trigger drops below threshold → Hold (phase=0).
//   Hold    : gain = 1.0. trigger > threshold → back to Open. phase done → Release.
//   Release : ramp gain 1.0 → reduction over releaseSamples_.
//             trigger > threshold → re-trigger from current gain (smooth).
//             phase done → Closed.
//
// Critical DSP guardrail: the envelope follower MUST have a 1-sample
// (instantaneous) attack on the trigger input. Anything sluggish causes
// the gate to open AFTER the snare transient has passed, choking the
// reverb attack. Release time on the follower is short (~10 ms) so the
// gate closes quickly after the snare body decays.
class NoiseGate
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        // Envelope-follower release: 10 ms. coeff = exp(-1 / (tau * sr))
        envReleaseCoeff_ = std::exp (-1.0f / (0.010f * static_cast<float> (sampleRate)));
        reset();
    }

    void reset()
    {
        state_     = State::Closed;
        envelope_  = 0.0f;
        currentGain_ = reductionLin_;
        phase_     = 0;
    }

    // Setters (linear values for threshold/reduction; sample counts for times).
    void setThreshold       (float linear) { thresholdLin_ = std::max (0.0f, linear); }
    void setReduction       (float linear) { reductionLin_ = std::clamp (linear, 0.0f, 1.0f); }
    void setAttackSamples   (int n) { attackSamples_  = std::max (1, n); }
    void setHoldSamples     (int n) { holdSamples_    = std::max (0, n); }
    void setReleaseSamples  (int n) { releaseSamples_ = std::max (1, n); }

    // Process a block. trigger[] is the DRY input (used for envelope-following
    // and threshold detection). wetL/wetR are the FDN wet output, modified
    // in place: each sample multiplied by the current gate gain.
    void process (const float* triggerL, const float* triggerR,
                  float* wetL, float* wetR, int numSamples)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            // ── 1) Envelope follower ── 1-sample (instantaneous) attack.
            // Take the larger of |L|, |R| as the trigger level.
            const float level = std::max (std::abs (triggerL[n]), std::abs (triggerR[n]));
            if (level > envelope_)
                envelope_ = level;                         // instantaneous attack
            else
                envelope_ = level + envReleaseCoeff_ * (envelope_ - level);

            const bool aboveThreshold = (envelope_ > thresholdLin_);

            // ── 2) State machine ──
            switch (state_)
            {
                case State::Closed:
                    if (aboveThreshold)
                    {
                        // Capture currentGain_ as the attack start so the ramp
                        // is continuous from the reduction floor up to 1.0.
                        // Without this, attackStart_ stays at its default 0.0f
                        // and the first attack after reset() jumps from 0
                        // (not reductionLin_) — audible click if reduction > 0.
                        attackStart_ = currentGain_;
                        state_ = State::Attack;
                        phase_ = 0;
                    }
                    break;

                case State::Attack:
                {
                    // Ramp from currentGain_ (= reduction at start of attack)
                    // up to 1.0 over attackSamples_. Using actual currentGain_
                    // as the ramp start gives smooth re-attacks from Release.
                    const float t = static_cast<float> (phase_) / static_cast<float> (attackSamples_);
                    currentGain_ = attackStart_ + t * (1.0f - attackStart_);
                    ++phase_;
                    if (phase_ >= attackSamples_)
                    {
                        currentGain_ = 1.0f;
                        state_ = State::Open;
                    }
                    break;
                }

                case State::Open:
                    currentGain_ = 1.0f;
                    if (! aboveThreshold)
                    {
                        state_ = State::Hold;
                        phase_ = 0;
                    }
                    break;

                case State::Hold:
                    currentGain_ = 1.0f;
                    if (aboveThreshold)
                    {
                        state_ = State::Open;          // re-triggered, stay open
                    }
                    else
                    {
                        ++phase_;
                        if (phase_ >= holdSamples_)
                        {
                            state_ = State::Release;
                            phase_ = 0;
                            releaseStart_ = currentGain_;
                        }
                    }
                    break;

                case State::Release:
                {
                    if (aboveThreshold)
                    {
                        // Smooth re-trigger from current gain
                        attackStart_ = currentGain_;
                        state_ = State::Attack;
                        phase_ = 0;
                    }
                    else
                    {
                        const float t = static_cast<float> (phase_) / static_cast<float> (releaseSamples_);
                        currentGain_ = releaseStart_ + t * (reductionLin_ - releaseStart_);
                        ++phase_;
                        if (phase_ >= releaseSamples_)
                        {
                            currentGain_ = reductionLin_;
                            state_ = State::Closed;
                            attackStart_ = reductionLin_;
                        }
                    }
                    break;
                }
            }

            // ── 3) Apply gain to wet ──
            wetL[n] *= currentGain_;
            wetR[n] *= currentGain_;
        }
    }

    // Bypass — pass wet through unchanged but still keep envelope follower
    // running (so toggling back ON doesn't have a stale envelope state).
    void processBypass (const float* triggerL, const float* triggerR,
                        const float* /*wetL*/, const float* /*wetR*/,
                        int numSamples)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float level = std::max (std::abs (triggerL[n]), std::abs (triggerR[n]));
            if (level > envelope_) envelope_ = level;
            else envelope_ = level + envReleaseCoeff_ * (envelope_ - level);
        }
        // wet unchanged; do nothing else. State stays Closed.
        state_ = State::Closed;
        currentGain_ = 1.0f;     // visual: when bypassed, gate isn't attenuating
    }

private:
    enum class State { Closed, Attack, Open, Hold, Release };

    double sampleRate_ = 48000.0;

    // Parameters (defaults sensible for "RMX16 Phil Collins")
    float thresholdLin_ = 0.0631f;     // -24 dBFS
    float reductionLin_ = 0.0f;        // full silence
    int   attackSamples_  = 48;        // 1 ms @ 48k
    int   holdSamples_    = 7200;      // 150 ms @ 48k
    int   releaseSamples_ = 2400;      // 50 ms @ 48k

    // State
    State state_ = State::Closed;
    float envelope_     = 0.0f;
    float envReleaseCoeff_ = 0.99f;
    float currentGain_  = 0.0f;
    int   phase_        = 0;
    float attackStart_  = 0.0f;        // gain at the moment of entering Attack
    float releaseStart_ = 1.0f;        // gain at the moment of entering Release
};
