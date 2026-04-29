#include "ShimmerEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// ShimmerEngine v8 — classic Eno/Lanois Townhouse rig topology.
// See header for full architecture description.
// ============================================================================

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;
}

// ============================================================================
// GranularPitchShifter (kept verbatim from v6 — this is the part that worked)
// ============================================================================

void ShimmerEngine::GranularPitchShifter::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    // Buffer = 8 × grain size for plenty of lookback headroom across the
    // 1.0–4.0 pitch-ratio range.
    const int bufSize = DspUtils::nextPowerOf2 (8 * kGrainSize);
    buffer_.assign (static_cast<size_t> (bufSize), 0.0f);
    mask_ = bufSize - 1;
    clear();
    updateAntiAliasCutoff();
}

void ShimmerEngine::GranularPitchShifter::BiquadLP::setCoeffs (double sampleRate, float fc, float Q)
{
    const float omega = kTwoPi * fc / static_cast<float> (sampleRate);
    const float cosw  = std::cos (omega);
    const float sinw  = std::sin (omega);
    const float alpha = sinw / (2.0f * Q);

    const float a0 = 1.0f + alpha;
    b0 = (1.0f - cosw) * 0.5f / a0;
    b1 = (1.0f - cosw) / a0;
    b2 = (1.0f - cosw) * 0.5f / a0;
    a1 = -2.0f * cosw / a0;
    a2 = (1.0f - alpha) / a0;
}

void ShimmerEngine::GranularPitchShifter::updateAntiAliasCutoff()
{
    // Cutoff = min(SR/2 × 0.9, SR/2 / pitchRatio_) so we never sample
    // content that would alias when the read pointer advances by ratio
    // samples per output sample. Two cascaded Butterworth biquads = 4-pole
    // / 24 dB-per-octave roll-off, > 30 dB rejection above the cutoff.
    const float nyquist = 0.5f * static_cast<float> (sampleRate_);
    const float fc      = std::min (nyquist * 0.9f,
                                    nyquist / std::max (pitchRatio_, 1.0f));
    aaStage1_.setCoeffs (sampleRate_, fc, kButterworthQ1);
    aaStage2_.setCoeffs (sampleRate_, fc, kButterworthQ2);
}

void ShimmerEngine::GranularPitchShifter::clear()
{
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    phase1_   = 0;
    phase2_   = kGrainSize / 2;
    readPos1_ = 0.0f;
    readPos2_ = static_cast<float> (kGrainSize) * 0.5f;
    aaStage1_.clear();
    aaStage2_.clear();
}

void ShimmerEngine::GranularPitchShifter::setPitchRatio (float ratio)
{
    pitchRatio_ = std::clamp (ratio, 0.5f, 4.0f);
    updateAntiAliasCutoff();
}

void ShimmerEngine::GranularPitchShifter::setModulation (float rateHz, float depth,
                                                          std::uint32_t seed)
{
    ratioMod_.prepare (static_cast<float> (sampleRate_), seed);
    ratioMod_.setRate (rateHz);
    ratioMod_.setDepth (1.0f);
    ratioModDepth_ = depth;
    ratioModEnabled_ = (depth > 0.0f);
}

void ShimmerEngine::GranularPitchShifter::startNewGrain (int& phase, float& readPos)
{
    phase = 0;
    const float lookback = std::ceil ((pitchRatio_ - 1.0f) * static_cast<float> (kGrainSize)) + 64.0f;
    readPos = static_cast<float> (writePos_) - lookback;
}

float ShimmerEngine::GranularPitchShifter::readLinear (float pos) const
{
    const int   intPos  = static_cast<int> (std::floor (pos));
    const float frac    = pos - static_cast<float> (intPos);
    const int   idx0    = intPos & mask_;
    const int   idx1    = (intPos + 1) & mask_;
    return buffer_[static_cast<size_t> (idx0)] * (1.0f - frac)
         + buffer_[static_cast<size_t> (idx1)] *         frac;
}

float ShimmerEngine::GranularPitchShifter::process (float input)
{
    // Bypass when pitchRatio is essentially unity. Saves ~5 % CPU per
    // sample AND avoids a known startup glitch: the dual-grain Hann
    // overlap takes ~kGrainSize/2 samples (~43 ms at 48 k) to ramp up
    // because grain 2 starts mid-window over zero-initialised buffer.
    // For Shimmer's PITCH=0 setting we want the engine to behave as a
    // plain reverb (input passes through to the reverb stage), not have
    // the snare's onset attenuated by 30 dB through Hann ramp-up.
    if (std::abs (pitchRatio_ - 1.0f) < 0.005f)
    {
        // Still keep the buffer + write-head alive so we can resume
        // gracefully if pitchRatio changes mid-block (parameter automation).
        const float filteredPassthrough = aaStage2_.process (aaStage1_.process (input));
        buffer_[static_cast<size_t> (writePos_)] = filteredPassthrough + DspUtils::kDenormalPrevention;
        writePos_ = (writePos_ + 1) & mask_;
        return filteredPassthrough;
    }

    // Anti-alias the input before it hits the grain buffer.
    const float filtered = aaStage2_.process (aaStage1_.process (input));

    // Write filtered input to buffer.
    buffer_[static_cast<size_t> (writePos_)] = filtered + DspUtils::kDenormalPrevention;

    // Read both grains with linear interpolation.
    const float v1 = readLinear (readPos1_);
    const float v2 = readLinear (readPos2_);

    // Hann window per grain.
    const float t1 = static_cast<float> (phase1_) / static_cast<float> (kGrainSize);
    const float t2 = static_cast<float> (phase2_) / static_cast<float> (kGrainSize);
    const float w1 = 0.5f * (1.0f - std::cos (kTwoPi * t1));
    const float w2 = 0.5f * (1.0f - std::cos (kTwoPi * t2));

    const float out = v1 * w1 + v2 * w2;

    // Slow random-walk modulation of the per-sample read advance. At ±0.5%
    // ratio variation (≈ ±8 cents) the pitch sounds steady, but each cascade
    // cycle pitches by a slightly different amount — so the cumulative
    // cascade doesn't stack on exact harmonic frequencies, which is what
    // creates the sharp metallic peaks at e.g. 376/428/758/1314 Hz when a
    // 200 Hz fundamental migrates octave-by-octave through the loop.
    // Modulation smears those peaks across narrow bands so they blend with
    // the rest of the cascade tail (the "subdued" character the user
    // observed in Valhalla Shimmer).
    const float effectiveRatio = ratioModEnabled_
        ? pitchRatio_ * (1.0f + ratioModDepth_ * ratioMod_.next())
        : pitchRatio_;

    // Advance grain phases; reset when a grain reaches kGrainSize.
    readPos1_ += effectiveRatio;
    readPos2_ += effectiveRatio;
    ++phase1_;
    ++phase2_;
    if (phase1_ >= kGrainSize) startNewGrain (phase1_, readPos1_);
    if (phase2_ >= kGrainSize) startNewGrain (phase2_, readPos2_);

    writePos_ = (writePos_ + 1) & mask_;
    return out;
}

// ============================================================================
// ShimmerEngine
// ============================================================================

void ShimmerEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    pitchL_.prepare (sampleRate);
    pitchR_.prepare (sampleRate);

    // Defocus the cascade peaks via random-walk pitch-ratio modulation.
    // For peak smearing to work, the modulation must change appreciably
    // between consecutive cascade cycles (~50 ms apart). At rates of
    // 5-7 Hz the LFO traverses ~25-35% of its excursion in one cascade
    // period, so consecutive cycles see distinctly different ratios.
    // ±1.5% (~26 cents) depth is enough to noticeably smear the sharp
    // resonant peaks of the migrated harmonics; lower depth leaves the
    // peaks visible. Slight chorus-like wobble is the audible side
    // effect — period-correct for analog shimmer rigs which had wow/
    // flutter from tape and tube modulators in the loop.
    pitchL_.setModulation (5.7f, 0.015f, 0xC0FFEEu);
    pitchR_.setModulation (7.3f, 0.015f, 0xBADC0DEu);

    // Hall reverb baseline: long, lush, slightly dark (period-correct
    // for the Lexicon 224 character that the original Eno/Lanois rig used).
    reverb_.prepare (sampleRate, maxBlockSize);
    reverb_.setDecayTime         (4.0f);
    reverb_.setSize              (0.75f);
    reverb_.setBassMultiply      (1.10f);
    reverb_.setMidMultiply       (1.00f);
    reverb_.setTrebleMultiply    (0.85f);
    reverb_.setCrossoverFreq     (550.0f);
    reverb_.setHighCrossoverFreq (3500.0f);
    reverb_.setSaturation        (0.0f);
    reverb_.setTankDiffusion     (0.85f);

    reverbInL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    reverbInR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    wetL_.assign      (static_cast<size_t> (maxBlockSize), 0.0f);
    wetR_.assign      (static_cast<size_t> (maxBlockSize), 0.0f);

    // Feedback delay line — fixed 50 ms acoustic delay, decoupled from
    // block size. Buffer must be ≥ maxBlockSize so that a single block's
    // reads + writes never lap the pointer (which would corrupt the delay
    // by overwriting a slot before the next block has read it).
    const int requestedDelay = static_cast<int> (sampleRate * 0.050);
    fbDelaySamples_   = std::max (requestedDelay, maxBlockSize + 1);
    fbDelayWritePos_  = 0;
    fbDelayLineL_.assign (static_cast<size_t> (fbDelaySamples_), 0.0f);
    fbDelayLineR_.assign (static_cast<size_t> (fbDelaySamples_), 0.0f);

    const float sr = static_cast<float> (sampleRate);
    fbHpfL_.setHPCutoff (kFeedbackHpfHz, sr);
    fbHpfR_.setHPCutoff (kFeedbackHpfHz, sr);
    fbLpfL_.setLPCutoff (kFeedbackLpfHz, sr);
    fbLpfR_.setLPCutoff (kFeedbackLpfHz, sr);
    fbHpfL_.clear(); fbHpfR_.clear();
    fbLpfL_.clear(); fbLpfR_.clear();

    updatePitchRatio();
    prepared_ = true;
}

void ShimmerEngine::clearBuffers()
{
    pitchL_.clear();
    pitchR_.clear();
    reverb_.clearBuffers();
    std::fill (reverbInL_.begin(), reverbInL_.end(), 0.0f);
    std::fill (reverbInR_.begin(), reverbInR_.end(), 0.0f);
    std::fill (wetL_.begin(),      wetL_.end(),      0.0f);
    std::fill (wetR_.begin(),      wetR_.end(),      0.0f);
    std::fill (fbDelayLineL_.begin(), fbDelayLineL_.end(), 0.0f);
    std::fill (fbDelayLineR_.begin(), fbDelayLineR_.end(), 0.0f);
    fbDelayWritePos_ = 0;
    fbHpfL_.clear(); fbHpfR_.clear();
    fbLpfL_.clear(); fbLpfR_.clear();
}

// ============================================================================
// Setters
// ============================================================================

void ShimmerEngine::setDecayTime (float seconds)
{
    // Pass through to the FDN reverb. The FDN has its own internal range
    // clamping. Long decays (3-6 s) are typical for shimmer cascade.
    reverb_.setDecayTime (seconds);
}

void ShimmerEngine::setSize         (float size) { reverb_.setSize (size); }
void ShimmerEngine::setBassMultiply (float mult) { reverb_.setBassMultiply (mult); }
void ShimmerEngine::setMidMultiply  (float mult) { reverb_.setMidMultiply  (mult); }
void ShimmerEngine::setTrebleMultiply (float mult) { reverb_.setTrebleMultiply (mult); }
void ShimmerEngine::setCrossoverFreq  (float hz)   { reverb_.setCrossoverFreq  (hz); }
void ShimmerEngine::setHighCrossoverFreq (float hz){ reverb_.setHighCrossoverFreq (hz); }
void ShimmerEngine::setTankDiffusion (float amount){ reverb_.setTankDiffusion (amount); }

void ShimmerEngine::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

// DEPTH (mod_depth, 0..1) → PITCH semitones (0..24). 0 = unity (no shift),
// 0.5 = +12 (octave up — canonical Eno Choir), 1.0 = +24 (Cascading Heaven).
void ShimmerEngine::setModDepth (float depth)
{
    const float clamped = std::clamp (depth, 0.0f, 1.0f);
    pitchSemitones_ = clamped * 24.0f;
    if (prepared_) updatePitchRatio();
}

// RATE (mod_rate, 0.1..10 Hz) → FEEDBACK gain (0..kFeedbackMax). Maps the
// Hz value linearly so the user gets useful resolution across the cascade-
// strength range. Hard-capped at kFeedbackMax (0.95) for stability.
void ShimmerEngine::setModRate (float hz)
{
    const float clamped = std::clamp (hz, 0.1f, 10.0f);
    const float fb01 = (clamped - 0.1f) / 9.9f;        // 0..1
    feedbackGain_ = std::clamp (fb01 * kFeedbackMax, 0.0f, kFeedbackMax);
}

void ShimmerEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    reverb_.setFreeze (frozen);
}

void ShimmerEngine::updatePitchRatio()
{
    // Convert semitones to ratio. ratio = 2^(semitones / 12).
    const float ratio = std::pow (2.0f, pitchSemitones_ / 12.0f);
    pitchL_.setPitchRatio (ratio);
    pitchR_.setPitchRatio (ratio);
}

// ============================================================================
// process — v9 topology: dry input → reverb (forward path);
//                        wet → delay → pitchShift → softClip×fb → mix node.
// ============================================================================

void ShimmerEngine::process (const float* inL, const float* inR,
                             float* outL, float* outR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    if (numSamples > maxBlockSize_)
        numSamples = maxBlockSize_;

    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;
    const float fb           = feedbackGain_;

    // ── Pass 1: build reverb input = drive(input) + softClipped(pitched fb).
    // The pitch shifter NO LONGER processes the dry input — only the delayed
    // wet feedback. That preserves the natural reverb tail for the dry
    // signal (first-pass through reverb is unpitched) and adds the shimmer
    // cascade purely via the feedback loop.
    //
    // Pass 1 walks a local read cursor through the 50 ms circular delay
    // line; Pass 3 walks the member write cursor through the same line.
    // Both start at fbDelayWritePos_ and advance per sample, so the wet
    // sample written at slot k in this block is the one read back at slot
    // k after the buffer wraps once (= fbDelaySamples_ samples later).
    int readPos = fbDelayWritePos_;
    for (int n = 0; n < numSamples; ++n)
    {
        // Drive softClip on input — the saturation knob still affects the
        // dry path entering the reverb.
        const float driveL = DspUtils::softClip (inL[n], satThreshold, satCeiling);
        const float driveR = DspUtils::softClip (inR[n], satThreshold, satCeiling);

        // Pitch-shift the delayed wet directly. At PITCH=0 the shifter
        // bypasses to a filtered passthrough, collapsing the engine to a
        // feedback-extended reverb.
        const float pitchedFbL = pitchL_.process (fbDelayLineL_[static_cast<size_t> (readPos)]);
        const float pitchedFbR = pitchR_.process (fbDelayLineR_[static_cast<size_t> (readPos)]);

        // Band-pass the pitch-shifted feedback: HPF removes grain-rate
        // sub-harmonics that would otherwise rumble up over time; LPF
        // tames the upper-spectrum metallic ring caused by repeated
        // pitch-up cycles concentrating energy near the AA-filter wall.
        const float bandedL = fbLpfL_.processLP (fbHpfL_.processHP (pitchedFbL));
        const float bandedR = fbLpfR_.processLP (fbHpfR_.processHP (pitchedFbR));

        // Apply user feedback gain × kFeedbackLoopAttn, then softClip
        // as a runaway safety net.
        const float fbL = DspUtils::softClip (bandedL * fb * kFeedbackLoopAttn,
                                              kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);
        const float fbR = DspUtils::softClip (bandedR * fb * kFeedbackLoopAttn,
                                              kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);

        // Mix node: dry input + pitched feedback → reverb input.
        reverbInL_[static_cast<size_t> (n)] = driveL + fbL;
        reverbInR_[static_cast<size_t> (n)] = driveR + fbR;

        if (++readPos >= fbDelaySamples_) readPos = 0;
    }

    // ── Pass 2: reverb on the (dry + pitched-fb) mix → wet. ──
    reverb_.process (reverbInL_.data(), reverbInR_.data(),
                     wetL_.data(),      wetR_.data(),      numSamples);

    // ── Pass 3: emit wet (with output trim) AND write wet into the
    // feedback delay line for next-cycle pitch + recirculation. Engine
    // outputs WET ONLY; dry/wet mix happens in the DuskVerbEngine wrapper
    // via setMix.
    for (int n = 0; n < numSamples; ++n)
    {
        const float wL = wetL_[static_cast<size_t> (n)];
        const float wR = wetR_[static_cast<size_t> (n)];

        fbDelayLineL_[static_cast<size_t> (fbDelayWritePos_)] = wL;
        fbDelayLineR_[static_cast<size_t> (fbDelayWritePos_)] = wR;
        if (++fbDelayWritePos_ >= fbDelaySamples_) fbDelayWritePos_ = 0;

        outL[n] = std::tanh (wL * kWetOutputGain);
        outR[n] = std::tanh (wR * kWetOutputGain);
    }
}
