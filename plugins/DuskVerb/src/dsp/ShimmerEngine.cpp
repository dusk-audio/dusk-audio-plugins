#include "ShimmerEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// ============================================================================
// Shimmer reverb and its granular pitch voices.
// ============================================================================

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;
}

// ============================================================================
// GranularPitchShifter
// ============================================================================

void ShimmerEngine::GranularPitchShifter::prepare (double sampleRate, float historyMs,
                                                  const std::vector<float>* timedWindow)
{
#if defined(DUSKVERB2_OPTIMIZED_DSP)
    static const auto window = [] {
        std::array<float, kGrainSize> weights {};
        for (int i = 0; i < kGrainSize; ++i)
            weights[i] = 0.5f * (1.0f - std::cos (kTwoPi * (static_cast<float> (i) / kGrainSize)));
        return weights;
    }();
    legacyWindow_ = window.data();
#endif
    sampleRate_ = sampleRate;
    historySamples_ = static_cast<int> (std::lround (sampleRate * 0.001 * historyMs));
    // Room for the requested delay and the full modulated read at any pitch.
    timedWeights_ = timedWindow ? timedWindow->data() : nullptr;
    timedGrainSize_ = timedWindow ? static_cast<int> (timedWindow->size()) : kGrainSize;
    selectGrainWindow();
    const int maxGrain = std::max (kGrainSize, timedGrainSize_);
    const int capacity = DspUtils::nextPowerOf2 (std::max (kLegacyCapacity, historySamples_ + 4 * maxGrain));
    buffer_.assign (static_cast<size_t> (capacity), 0.0f);
    mask_ = (timedHistory_ ? capacity : kLegacyCapacity) - 1;
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
    resetGrains();
    aaStage1_.clear();
    aaStage2_.clear();
}

void ShimmerEngine::GranularPitchShifter::selectGrainWindow()
{
    const bool useTimedWindow = timedHistory_ && timedWeights_ != nullptr;
    grainSize_ = useTimedWindow ? timedGrainSize_ : kGrainSize;
#if defined(DUSKVERB2_OPTIMIZED_DSP)
    grainWindow_ = useTimedWindow ? timedWeights_ : legacyWindow_;
#endif
}

void ShimmerEngine::GranularPitchShifter::setTimedHistory (bool enabled)
{
    if (timedHistory_ == enabled) return;
    timedHistory_ = enabled;
    selectGrainWindow();
    if (buffer_.empty()) return;
    mask_ = (enabled ? static_cast<int> (buffer_.size()) : kLegacyCapacity) - 1;
    writePos_ &= mask_;
    resetGrains();
}

void ShimmerEngine::GranularPitchShifter::resetGrains()
{
    // A preset with no descending voices leaves these oscillators idle. Start
    // a new descent from its seed on recall, regardless of the preceding preset.
    if (timedHistory_ && pitchRatio_ < 1.0f) ratioMod_ = initialRatioMod_;
    phase1_ = 0;
    phase2_ = grainSize_ / 2;
    readPos1_ = timedHistory_ ? static_cast<float> (writePos_) - grainLookback() : 0.0f;
    readPos2_ = timedHistory_ ? readPos1_ + static_cast<float> (phase2_) * (pitchRatio_ - 1.0f)
                              : static_cast<float> (phase2_);
}

void ShimmerEngine::GranularPitchShifter::setPitchRatio (float ratio)
{
    const float previous = pitchRatio_;
    pitchRatio_ = std::clamp (ratio, 0.5f, 4.0f);
    if (timedHistory_ && pitchRatio_ != previous && writePos_ == 0 && phase1_ == 0)
        resetGrains();
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
    initialRatioMod_ = ratioMod_;
}

float ShimmerEngine::GranularPitchShifter::grainLookback() const
{
    if (!timedHistory_) return std::ceil ((pitchRatio_ - 1.0f) * kGrainSize) + 64.0f;
    // A downward grain falls behind the write head as it plays. Starting it
    // ahead of that head would read the previous ring cycle until it wraps.
    const float fastest = pitchRatio_ * (1.0f + ratioModDepth_);
    return static_cast<float> (historySamples_) + 64.0f * grainSize_ / kGrainSize
         + std::ceil (std::max (0.0f, fastest - 1.0f) * grainSize_);
}

void ShimmerEngine::GranularPitchShifter::startNewGrain (int& phase, float& readPos)
{
    phase = 0;
    readPos = static_cast<float> (writePos_) - grainLookback();
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

#if defined(DUSKVERB2_OPTIMIZED_DSP)
    const float out = v1 * grainWindow_[phase1_] + v2 * grainWindow_[phase2_];
#else
    const float t1 = static_cast<float> (phase1_) / static_cast<float> (grainSize_);
    const float t2 = static_cast<float> (phase2_) / static_cast<float> (grainSize_);
    const float w1 = 0.5f * (1.0f - std::cos (kTwoPi * t1));
    const float w2 = 0.5f * (1.0f - std::cos (kTwoPi * t2));

    const float out = v1 * w1 + v2 * w2;
#endif

    // Slow random-walk modulation of the per-sample read advance. At ±0.5%
    // ratio variation (≈ ±8 cents) the pitch sounds steady, but each cascade
    // cycle pitches by a slightly different amount — so the cumulative
    // cascade doesn't stack on exact harmonic frequencies, which is what
    // creates the sharp metallic peaks at e.g. 376/428/758/1314 Hz when a
    // 200 Hz fundamental migrates octave-by-octave through the loop.
    // Modulation smears those peaks across narrow bands so they blend with
    // the rest of the cascade tail (the intended "subdued"
    // character).
    const float effectiveRatio = ratioModEnabled_
        ? pitchRatio_ * (1.0f + ratioModDepth_ * ratioMod_.next())
        : pitchRatio_;

    // Advance grain phases; reset when a grain reaches kGrainSize.
    readPos1_ += effectiveRatio;
    readPos2_ += effectiveRatio;
    ++phase1_;
    ++phase2_;
    if (phase1_ >= grainSize_) startNewGrain (phase1_, readPos1_);
    if (phase2_ >= grainSize_) startNewGrain (phase2_, readPos2_);

    writePos_ = (writePos_ + 1) & mask_;
    return out;
}

// ============================================================================
// ShimmerEngine
// ============================================================================

void ShimmerEngine::OctaveSpread::prepare (double sampleRate, float lengthMs)
{
    const int length = std::max (1, static_cast<int> (std::lround (sampleRate * 0.001 * lengthMs)));
    delay.assign (static_cast<size_t> (length), 0.0f);
    middleOffset = length / 2;
    write = 0;
}

void ShimmerEngine::OctaveSpread::clear()
{
    std::fill (delay.begin(), delay.end(), 0.0f);
    write = 0;
}

float ShimmerEngine::OctaveSpread::process (float sample)
{
    const int length = static_cast<int> (delay.size());
    int middle = write + middleOffset;
    if (middle >= length) middle -= length;
    const float output = 0.39f * sample + 0.45f * delay[middle] + 0.8f * delay[write];
    delay[write] = sample;
    if (++write == length) write = 0;
    return output;
}

void ShimmerEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // The interval between overlapping grains is part of the octave voicing.
    // Hold it in seconds across host rates and share the Hann table across voices.
    constexpr double overlapSeconds = (GranularPitchShifter::kGrainSize / 2) / 48000.0;
    const int halfGrain = std::max (1, static_cast<int> (std::lround (sampleRate * overlapSeconds)));
    const int length = 2 * halfGrain;
    timedGrainWindow_.resize (static_cast<size_t> (length));
    for (int i = 0; i < length; ++i)
        timedGrainWindow_[static_cast<size_t> (i)] = 0.5f * (1.0f - std::cos (kTwoPi * (static_cast<float> (i) / length)));

    // At high rates each upper octave takes longer to emerge. Feed enough of
    // the delayed wet back into the tank for the later octaves to reach level.
    const float rateStep = std::clamp (static_cast<float> (sampleRate / 48000.0 - 1.0), 0.0f, 3.0f);
    const float above96k = std::max (0.0f, rateStep - 1.0f);
    const float upperReturnMs = 232.0f + 95.0f * rateStep;
    firstUpperInputGain_ = 0.48f + 0.12f * above96k;
    secondUpperInputGain_ = 1.0f + 0.12f * rateStep;
    firstUpperReturnGain_ = 1.30f + 0.16f * rateStep + 0.14f * above96k;
    secondUpperReturnGain_ = 0.50f + 0.17f * rateStep;
    // At 44.1 kHz both upper voices need more initial excitation to reach
    // their buildup levels. Keep the second voice's recirculating gain lower.
    const float lowRate = std::clamp (static_cast<float> ((48000.0 - sampleRate) / 3900.0), 0.0f, 1.0f);
    lowerOctaveScale_ = 1.0f + 0.16f * lowRate;
    firstUpperInputGain_ *= 1.0f + 0.22f * lowRate;
    firstUpperReturnGain_ *= 1.0f + 0.10f * lowRate;
    secondUpperInputGain_ *= 1.0f + 0.07f * lowRate;
    secondUpperReturnGain_ *= 1.0f - 0.05f * lowRate;
    const float firstOctaveDelayMs = 5.0f * std::min (rateStep, 1.0f);
    pitchL_.prepare (sampleRate, firstOctaveDelayMs, &timedGrainWindow_);
    pitchR_.prepare (sampleRate, firstOctaveDelayMs, &timedGrainWindow_);

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
    const float upperOctaveDelayMs = 263.0f + 30.0f * std::min (rateStep, 1.0f);
    pitch2L_.prepare (sampleRate, upperOctaveDelayMs, &timedGrainWindow_);
    pitch2R_.prepare (sampleRate, upperOctaveDelayMs, &timedGrainWindow_);
    pitch2L_.setModulation (4.9f, 0.020f, 0x5EED01u);
    pitch2R_.setModulation (6.7f, 0.020f, 0x5EED02u);
    pitchDownL_.prepare (sampleRate, 0.0f, &timedGrainWindow_);
    pitchDownR_.prepare (sampleRate, 0.0f, &timedGrainWindow_);
    pitchDownL_.setModulation (5.3f, 0.018f, 0xD0117Au);
    pitchDownR_.setModulation (6.1f, 0.018f, 0xD0117Bu);
    pitchDownL_.setPitchRatio (kVoiceDownRatio);   // −1 oct (×0.5 → 500 Hz)
    pitchDownR_.setPitchRatio (kVoiceDownRatio);
    pitchSubL_.prepare (sampleRate, 0.0f, &timedGrainWindow_);
    pitchSubR_.prepare (sampleRate, 0.0f, &timedGrainWindow_);
    pitchSubL_.setModulation (4.7f, 0.018f, 0x5B0117Au);
    pitchSubR_.setModulation (5.9f, 0.018f, 0x5B0117Bu);
    pitchSubL_.setPitchRatio (kVoiceSubRatio);     // −2 oct (×0.25 → 250 Hz)
    pitchSubR_.setPitchRatio (kVoiceSubRatio);
    // Dry-fed octave cascade: 4 mono −12 st stages, each modulated at its own rate/seed so the
    // octave reads as a smooth band (not a discrete mode). Feed-forward from the dry input.
    const std::array<float, kNumOctaves> octaveDelayMs { 25.0f, 156.0f, 175.0f + 52.5f * above96k, 0.0f };
    constexpr std::array<float, kTimedStages> spreadMs { 500.0f, 690.0f, 535.0f };
    for (int i = 0; i < kNumOctaves; ++i)
    {
        if (i < kTimedStages) octaveSpreads_[i].prepare (sampleRate, spreadMs[i]);
        octGenL_[i].prepare (sampleRate, octaveDelayMs[i], &timedGrainWindow_);
        octGenL_[i].setPitchRatio (0.5f);          // −1 oct per stage (cascade: 500/250/125/62)
        octGenL_[i].setModulation (4.3f + 0.6f * static_cast<float> (i), 0.020f,
                                   0x0C7A5E01u + static_cast<std::uint32_t> (i));
    }
    stereoMod_.prepare (sampleRate);
    air_.prepare (sampleRate, upperReturnMs, timedGrainWindow_);
    air_.setMix (airMix_);
    stereoMod_.setParams (stereoModRate_, stereoModDepth_, sampleRate);
    tailSpin_.prepare (sampleRate);
    tailNoise_.prepare (sampleRate);
    noiseDuckKey_.prepare (sampleRate);
    noiseDuckSlew_ = 1.0f - std::exp (-1.0f / (0.050f * static_cast<float> (sampleRate)));
    noiseDuckG_ = 1.0f;
    tailNoise_.setGain (noiseGain_);

    // Hall reverb baseline: long, lush, slightly dark.
    reverb_.prepare (sampleRate, maxBlockSize);
    // Every line at one delay-mod rate. Mod Rate drives the cascade feedback here (setModRate), so
    // the tank never gets it, and the presets on this engine were voiced that way: until 2026-10-28
    // every preset load left the tank's lines at one rate (FDNReverb::clearBuffers dropped the spread).
    reverb_.setModRateSpread (false);
    reverb_.setDecayTime         (4.0f);
    reverb_.setSize              (0.75f);
    reverb_.setBassMultiply      (1.10f);
    reverb_.setMidMultiply       (1.00f);
    reverb_.setTrebleMultiply    (0.85f);
    reverb_.setCrossoverFreq     (550.0f);
    reverb_.setHighCrossoverFreq (3500.0f);
    reverb_.setSaturation        (0.0f);
    reverb_.setTankDiffusion     (0.85f);

    // Dense-diffusion tank, same voicing baseline as the FDN above. DenseHall
    // has no tank-diffusion or saturation knob (diffusion is structural, and
    // the shimmer's softClip saturation lives in the feedback loop, not here).
    denseReverb_.prepare (sampleRate, maxBlockSize);
    denseReverb_.setDecayTime         (4.0f);
    denseReverb_.setSize              (0.75f);
    denseReverb_.setBassMultiply      (1.10f);
    denseReverb_.setMidMultiply       (1.00f);
    denseReverb_.setTrebleMultiply    (0.85f);
    denseReverb_.setCrossoverFreq     (550.0f);
    denseReverb_.setHighCrossoverFreq (3500.0f);

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
    const int upperDelay = std::max (fbDelaySamples_, static_cast<int> (std::lround (sampleRate * upperReturnMs * 0.001)));
    fbDelayLineL_.assign (static_cast<size_t> (upperDelay), 0.0f);
    fbDelayLineR_.assign (static_cast<size_t> (upperDelay), 0.0f);

    const float sr = static_cast<float> (sampleRate);
    fbHpfL_.setHPCutoff (feedbackHpfHz_, sr);
    fbHpfR_.setHPCutoff (feedbackHpfHz_, sr);
    fbLpfL_.setLPCutoff (kFeedbackLpfHz, sr);
    fbLpfR_.setLPCutoff (kFeedbackLpfHz, sr);
    fbHpfL_.clear(); fbHpfR_.clear();
    fbLpfL_.clear(); fbLpfR_.clear();
    hfShelfL_.setHPCutoff (hfSustainHz_, sr);
    hfShelfR_.setHPCutoff (hfSustainHz_, sr);
    hfShelfL_.clear(); hfShelfR_.clear();

    // Loop-hi limiter: re-derive every SR-dependent coeff from the stored config
    // so a sample-rate change keeps the ratio-keyed air duck in sync (same
    // re-apply-in-prepare pattern as the hfShelf + noiseDuck paths above).
    // loopHiCfg_ retains the user params; only the SR-derived coeffs go stale.
    // On the normal render path setLoopHiLimiter runs after prepare and overwrites
    // these, so this is inert (bit-null) there; it only bites on a bare SR change.
    loopHiCfg_.updateCoeffs (sampleRate_);
    loopHiCutL_.design (loopHiCfg_.loHz, loopHiCfg_.hiHz, loopHiCfg_.maxCutDb, sampleRate_);
    loopHiCutR_.design (loopHiCfg_.loHz, loopHiCfg_.hiHz, loopHiCfg_.maxCutDb, sampleRate_);
    const float srLoopHi = static_cast<float> (sampleRate_ > 1000.0 ? sampleRate_ : 48000.0);
    loopHiHpCoeff_ = 1.0f - std::exp (-kTwoPi * loopHiCfg_.loHz / srLoopHi);
    loopHiEnvAtk_  = 1.0f - std::exp (-1.0f / (0.030f * srLoopHi));   // 30 ms envelope attack
    loopHiEnvRel_  = 1.0f - std::exp (-1.0f / (0.300f * srLoopHi));   // 300 ms release
    loopHiDet_.clear();
    loopHiWetEnv_ = loopHiInEnv_ = 0.0f;
    loopHiHpWetL_ = loopHiHpWetR_ = loopHiHpInL_ = loopHiHpInR_ = 0.0f;

    updatePitchRatio();
    prepared_ = true;
}

void ShimmerEngine::clearBuffers()
{
    for (auto& spread : octaveSpreads_) spread.clear();
    pitchL_.clear();
    pitchR_.clear();
    pitch2L_.clear();
    pitch2R_.clear();
    pitchDownL_.clear();
    pitchDownR_.clear();
    pitchSubL_.clear();
    pitchSubR_.clear();
    for (int i = 0; i < kNumOctaves; ++i) octGenL_[i].clear();
    tailNoise_.clear();
    stereoMod_.clear();
    air_.clear();
    tailSpin_.clear();
    reverb_.clearBuffers();
    denseReverb_.clear();
    std::fill (reverbInL_.begin(), reverbInL_.end(), 0.0f);
    std::fill (reverbInR_.begin(), reverbInR_.end(), 0.0f);
    std::fill (wetL_.begin(),      wetL_.end(),      0.0f);
    std::fill (wetR_.begin(),      wetR_.end(),      0.0f);
    std::fill (fbDelayLineL_.begin(), fbDelayLineL_.end(), 0.0f);
    std::fill (fbDelayLineR_.begin(), fbDelayLineR_.end(), 0.0f);
    fbDelayWritePos_ = 0;
    fbHpfL_.clear(); fbHpfR_.clear();
    fbLpfL_.clear(); fbLpfR_.clear();
    hfShelfL_.clear(); hfShelfR_.clear();
    // Noise-duck + ratio-keyed loop-hi limiter state — reset so a stale reduction
    // (or a held-open duck) can't carry across a reset / preset swap.
    noiseDuckKey_.clear();
    noiseDuckG_ = 1.0f;
    loopHiDet_.clear();
    loopHiWetEnv_ = loopHiInEnv_ = 0.0f;
    loopHiHpWetL_ = loopHiHpWetR_ = loopHiHpInL_ = loopHiHpInR_ = 0.0f;
    loopHiCutL_.clear(); loopHiCutR_.clear();
}

// ============================================================================
// Setters
// ============================================================================

void ShimmerEngine::setDecayTime (float seconds)
{
    // Pass through to BOTH tanks. Each has its own internal range clamping.
    // Long decays (3-6 s) are typical for shimmer cascade.
    reverb_.setDecayTime (seconds);
    denseReverb_.setDecayTime (seconds);
}

void ShimmerEngine::setSize         (float size) { reverb_.setSize (size); denseReverb_.setSize (size); }
void ShimmerEngine::setBassMultiply (float mult) { reverb_.setBassMultiply (mult); denseReverb_.setBassMultiply (mult); }
void ShimmerEngine::setMidMultiply  (float mult) { reverb_.setMidMultiply  (mult); denseReverb_.setMidMultiply  (mult); }
void ShimmerEngine::setTrebleMultiply (float mult) { reverb_.setTrebleMultiply (mult); denseReverb_.setTrebleMultiply (mult); }
void ShimmerEngine::setCrossoverFreq  (float hz)   { reverb_.setCrossoverFreq  (hz); denseReverb_.setCrossoverFreq (hz); }
void ShimmerEngine::setHighCrossoverFreq (float hz){ reverb_.setHighCrossoverFreq (hz); denseReverb_.setHighCrossoverFreq (hz); }
void ShimmerEngine::setTankDiffusion (float amount){ reverb_.setTankDiffusion (amount); }  // FDN only; DenseHall diffusion is structural
void ShimmerEngine::setDownOctaveMix (float mix)   { downMix_ = std::clamp (mix, 0.0f, 4.0f); }
void ShimmerEngine::setSubOctaveMix  (float mix)   { subMix_  = std::clamp (mix, 0.0f, 4.0f); }
void ShimmerEngine::setHFAir (float mix) { airMix_ = mix; air_.setMix (mix); }
void ShimmerEngine::setStereoMod (float rateHz, float depth)
{
    stereoModRate_  = rateHz;
    stereoModDepth_ = depth;
    stereoMod_.setParams (rateHz, depth, sampleRate_);
}
void ShimmerEngine::setFeedbackHpfHz (float hz)
{
    feedbackHpfHz_ = std::clamp (hz, 15.0f, 200.0f);
    const float sr = static_cast<float> (sampleRate_);
    fbHpfL_.setHPCutoff (feedbackHpfHz_, sr);
    fbHpfR_.setHPCutoff (feedbackHpfHz_, sr);
}

void ShimmerEngine::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

void ShimmerEngine::setOutputHeadroom (float h)
{
    // Clamp to [1, 8]: below 1 would tighten the clip (pointless / more grit),
    // above 8 makes the output effectively linear (no safety limit at all).
    outputHeadroom_ = std::clamp (h, 1.0f, 8.0f);
}

// DEPTH (mod_depth, 0..1) → PITCH semitones (0..24). 0 = unity (no shift),
// 0.5 = +12 (octave up), 1.0 = +24 (two octaves up).
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
    denseReverb_.setFreeze (frozen);
}

void ShimmerEngine::setModeSmear (float depthSamples, float rateHz)
{
    reverb_.setModeSmear (depthSamples, rateHz);   // Black Hole + Deep Blue Day use the FDN reverb_
}
void ShimmerEngine::setSustainLimiterMid (float lo, float hi, float thr, float cut,
                                          float atk, float relF, float relS)
{
    // In-loop sustained-energy limiter in the internal reverb tank(s); the shimmer's
    // OWN pitch feedback loop is untouched (its air-band runaway needs a ratio-keyed
    // detector — a separate, later feature). maxCutDb 0 = bit-null.
    reverb_.setSustainLimiterMid      (lo, hi, thr, cut, atk, relF, relS);
    denseReverb_.setSustainLimiterMid (lo, hi, thr, cut, atk, relF, relS);
}
void ShimmerEngine::setSustainLimiterLow (float lo, float hi, float thr, float cut,
                                          float atk, float relF, float relS)
{
    reverb_.setSustainLimiterLow      (lo, hi, thr, cut, atk, relF, relS);
    denseReverb_.setSustainLimiterLow (lo, hi, thr, cut, atk, relF, relS);
}
void ShimmerEngine::setLoopHiLimiter (float loHz, float hiHz, float ratioThrDb, float maxCutDb,
                                      float atkMs, float relFastMs, float relSlowMs)
{
    loopHiCfg_.set (loHz, hiHz, ratioThrDb, maxCutDb, atkMs, relFastMs, relSlowMs, sampleRate_);
    loopHiCutL_.design (loopHiCfg_.loHz, loopHiCfg_.hiHz, loopHiCfg_.maxCutDb, sampleRate_);
    loopHiCutR_.design (loopHiCfg_.loHz, loopHiCfg_.hiHz, loopHiCfg_.maxCutDb, sampleRate_);
    const float sr = static_cast<float> (sampleRate_ > 1000.0 ? sampleRate_ : 48000.0);
    loopHiHpCoeff_ = 1.0f - std::exp (-kTwoPi * loopHiCfg_.loHz / sr);
    loopHiEnvAtk_  = 1.0f - std::exp (-1.0f / (0.030f * sr));   // 30 ms envelope attack
    loopHiEnvRel_  = 1.0f - std::exp (-1.0f / (0.300f * sr));   // 300 ms release
    loopHiDet_.clear();
    loopHiWetEnv_ = loopHiInEnv_ = 0.0f;
    loopHiHpWetL_ = loopHiHpWetR_ = loopHiHpInL_ = loopHiHpInR_ = 0.0f;
}

void ShimmerEngine::setUseDenseReverb (bool on) { useDenseReverb_ = on; }
void ShimmerEngine::setUseTailSpin    (bool on) { useTailSpin_ = on; }
void ShimmerEngine::setReverbBaseDelays (const int* delays) { reverb_.setBaseDelays (delays); }
void ShimmerEngine::resetReverbBaseDelays() { reverb_.resetBaseDelays(); }
void ShimmerEngine::setReverbFeedbackRotation (float peakRad, float rateHz) { reverb_.setFeedbackRotation (peakRad, rateHz); }
void ShimmerEngine::setTailNoise      (float gain, float hpHz, float lpHz) { noiseGain_ = gain; tailNoise_.setGain (gain); tailNoise_.setBand (hpHz, lpHz); }
void ShimmerEngine::setNoiseSustainDuck (float amt)
{
    noiseDuckAmt_ = std::clamp (amt, 0.0f, 1.0f);
    noiseDuckG_ = 1.0f;
}
void ShimmerEngine::setUpVoiceScale (float v1, float v2)
{
    voice1Scale_ = std::clamp (v1, 0.0f, 4.0f);
    voice2Scale_ = std::clamp (v2, 0.0f, 4.0f);
}
void ShimmerEngine::setOctaveTiming (bool enabled)
{
    timedOctaves_ = enabled;
    if (!enabled && fbDelaySamples_ > 0) fbDelayWritePos_ %= fbDelaySamples_;
    for (auto& octave : octGenL_) octave.setTimedHistory (enabled);
    for (auto* voice : { &pitchL_, &pitchR_, &pitch2L_, &pitch2R_,
                         &pitchDownL_, &pitchDownR_, &pitchSubL_, &pitchSubR_, &air_.shifterL, &air_.shifterR })
        voice->setTimedHistory (enabled);
}
void ShimmerEngine::setOctaveCascade (const float gains[4])
{
    bool any = false;
    for (int i = 0; i < kNumOctaves; ++i)
    {
        octGain_[i] = std::clamp (gains[i], 0.0f, 8.0f);
        any = any || (octGain_[i] > 1.0e-6f);
    }
    octActive_ = any;
}

void ShimmerEngine::setHFSustainDb (float db, float cornerHz)
{
    // Clamp to +12 dB: the shelf compounds per loop pass, and the loop's
    // softClip + kFeedbackLoopAttn bound it, but past ~12 dB the boosted
    // band pins the clip every pass (distorted ring, not longer T60).
    db = std::clamp (db, 0.0f, 12.0f);
    hfSustainGain_ = (db > 0.01f) ? std::pow (10.0f, db / 20.0f) - 1.0f : 0.0f;
    hfSustainHz_   = std::clamp (cornerHz, 1000.0f, 12000.0f);
    if (prepared_)
    {
        const float sr = static_cast<float> (sampleRate_);
        hfShelfL_.setHPCutoff (hfSustainHz_, sr);
        hfShelfR_.setHPCutoff (hfSustainHz_, sr);
    }
}

void ShimmerEngine::updatePitchRatio()
{
    // Convert semitones to ratio. ratio = 2^(semitones / 12).
    const float ratio = std::pow (2.0f, pitchSemitones_ / 12.0f);
    pitchL_.setPitchRatio (ratio);
    pitchR_.setPitchRatio (ratio);
    pitch2L_.setPitchRatio (ratio * kVoice2OctaveMul);   // octave above -> fills the top band
    pitch2R_.setPitchRatio (ratio * kVoice2OctaveMul);
}

// ============================================================================
// process — v9 topology: dry input → reverb (forward path);
//                        wet → delay → pitchShift → softClip×fb → mix node.
// ============================================================================

void ShimmerEngine::process (const float* inL, const float* inR,
                             float* outL, float* outR, int numSamples)
{
    if (numSamples <= 0) return;
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

    // Read the feedback block before the tank writes its new samples in pass 3.
    // The lower voices use the short return. The longer upper return gives
    // successive upward shifts time to build after the initial excitation.
    const float excitation = frozen_ ? 0.0f : std::min (1.0f, pitchSemitones_ / 12.0f);
    const float octaveScale = timedOctaves_ ? lowerOctaveScale_ : 1.0f;
    const float subGain = subMix_ * octaveScale;
    const float firstExcitation = firstUpperInputGain_ * excitation;
    const float secondExcitation = secondUpperInputGain_ * excitation;
    const int octaveCount = timedOctaves_ && octGain_[3] == 0.0f ? kTimedStages : kNumOctaves;
    const int delayLength = timedOctaves_ ? static_cast<int> (fbDelayLineL_.size()) : fbDelaySamples_;
    int upperRead = fbDelayWritePos_;
    int readPos = upperRead + delayLength - fbDelaySamples_;
    if (readPos >= delayLength) readPos -= delayLength;
    for (int n = 0; n < numSamples; ++n)
    {
        // Drive softClip on input — the saturation knob still affects the
        // dry path entering the reverb.
        const float driveL = DspUtils::softClip (inL[n], satThreshold, satCeiling);
        const float driveR = DspUtils::softClip (inR[n], satThreshold, satCeiling);

        // Pitch-shift the delayed wet directly. At PITCH=0 the shifter
        // bypasses to a filtered passthrough, collapsing the engine to a
        // feedback-extended reverb.
        const float fbSrcL = fbDelayLineL_[static_cast<size_t> (readPos)];
        const float fbSrcR = fbDelayLineR_[static_cast<size_t> (readPos)];
        // Per-preset scale on the upward voices (voice1 +12 st fills 250-500, voice2 +24 st fills
        // 500-1k). Deep Blue Day boosts these to regenerate the mid tail harder on transients
        // (the snare body ×2/×4 → the target 250 Hz-1 kHz body). Default 1.0 → ×1.0f bypass
        // → bit-identical to legacy for Black Hole + every other preset.
        float firstL = fbSrcL, firstR = fbSrcR, secondL = fbSrcL, secondR = fbSrcR;
        if (timedOctaves_)
        {
            // The first reflections excite the upper voices directly. The
            // delayed return then carries the higher octaves through the tank.
            const float upperL = fbDelayLineL_[static_cast<size_t> (upperRead)];
            const float upperR = fbDelayLineR_[static_cast<size_t> (upperRead)];
            firstL = firstUpperReturnGain_ * upperL + firstExcitation * driveL;
            firstR = firstUpperReturnGain_ * upperR + firstExcitation * driveR;
            secondL = secondUpperReturnGain_ * upperL + secondExcitation * driveL;
            secondR = secondUpperReturnGain_ * upperR + secondExcitation * driveR;
            if (++upperRead == delayLength) upperRead = 0;
        }
        float pitchedFbL = kVoice1Mix * voice1Scale_ * pitchL_.process (firstL)
                         + kVoice2Mix * voice2Scale_ * pitch2L_.process (secondL);
        float pitchedFbR = kVoice1Mix * voice1Scale_ * pitchR_.process (firstR)
                         + kVoice2Mix * voice2Scale_ * pitch2R_.process (secondR);
        // Octave-DOWN voice IN THE FEEDBACK: pitch the feedback down −1 oct + add back.
        // The loop regenerates the descending ladder (500→250→125 Hz) GRADUALLY over
        // its 50 ms passes → loud deep low that builds SMOOTHLY (no abrupt "kick-in"
        // like a self-feeding output ladder). softClip bounds the per-grain peaks; the
        // 60 Hz HPF below caps the sub; a MODERATE downMix_ keeps the loop gain < 1 so
        // it stays bounded (the clip was over-cranking to 3.5). 0 → branch skipped (byte-
        // identical on non-shimmer presets; this loop is the recursive-feedback TU, see the .h).
        if (downMix_ > 0.0f)
        {
            pitchedFbL += downMix_ * DspUtils::softClip (pitchDownL_.process (fbSrcL),
                                                         kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);
            pitchedFbR += downMix_ * DspUtils::softClip (pitchDownR_.process (fbSrcR),
                                                         kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);
        }
        // Octave-DOWN-2 (sub) voice: reaches 250 Hz in ONE step where the −1 oct cascade
        // dies out, then the loop regenerates 125→62 Hz from it → the intended deep low
        // wash. Same softClip + loop band-pass keep it bounded. 0 → branch skipped
        // (byte-identical on non-shimmer presets; recursive-feedback TU, see the .h note).
        if (subMix_ > 0.0f)
        {
            pitchedFbL += subGain * DspUtils::softClip (pitchSubL_.process (fbSrcL),
                                                        kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);
            pitchedFbR += subGain * DspUtils::softClip (pitchSubR_.process (fbSrcR),
                                                        kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);
        }

        // Band-pass the pitch-shifted feedback: HPF removes grain-rate
        // sub-harmonics that would otherwise rumble up over time; LPF
        // tames the upper-spectrum metallic ring caused by repeated
        // pitch-up cycles concentrating energy near the AA-filter wall.
        float bandedL = fbLpfL_.processLP (fbHpfL_.processHP (pitchedFbL));
        float bandedR = fbLpfR_.processLP (fbHpfR_.processHP (pitchedFbR));

        // HF-sustain compensation shelf: the tank is HF-lossy per pass (that
        // loss, not the LPF above, caps HF T60). Re-enter the loop with the
        // >4 kHz band lifted so the HF ring survives more passes. First-order
        // high-shelf: x + g * HP(x). gain <= 0 → skipped (bit-null pattern,
        // same as the downMix_/subMix_ branches above; recursive-feedback TU).
        if (hfSustainGain_ > 0.0f)
        {
            bandedL += hfSustainGain_ * hfShelfL_.processHP (bandedL);
            bandedR += hfSustainGain_ * hfShelfR_.processHP (bandedR);
        }

        // RATIO-KEYED loop hi limiter (setLoopHiLimiter): duck the regenerating air
        // band when wet-hi grossly exceeds input-hi (piano: input has no air, the
        // pitch loop makes it; pink: input-hi large -> ratio small -> off). Inactive
        // -> skipped -> bit-null.
        if (loopHiCfg_.active)
        {
            // one-pole HP envelopes (wet = the banded fb; input = this sample's dry)
            loopHiHpWetL_ += loopHiHpCoeff_ * (bandedL - loopHiHpWetL_);
            loopHiHpWetR_ += loopHiHpCoeff_ * (bandedR - loopHiHpWetR_);
            const float wetHi = 0.5f * (std::abs (bandedL - loopHiHpWetL_)
                                      + std::abs (bandedR - loopHiHpWetR_));
            loopHiHpInL_ += loopHiHpCoeff_ * (inL[n] - loopHiHpInL_);
            loopHiHpInR_ += loopHiHpCoeff_ * (inR[n] - loopHiHpInR_);
            const float inHi = 0.5f * (std::abs (inL[n] - loopHiHpInL_)
                                     + std::abs (inR[n] - loopHiHpInR_));
            loopHiWetEnv_ += (wetHi > loopHiWetEnv_ ? loopHiEnvAtk_ : loopHiEnvRel_) * (wetHi - loopHiWetEnv_);
            loopHiInEnv_  += (inHi  > loopHiInEnv_  ? loopHiEnvAtk_ : loopHiEnvRel_) * (inHi  - loopHiInEnv_);
            const float ratio = loopHiWetEnv_ / std::max (loopHiInEnv_, 1.0e-4f);
            const bool present = loopHiInEnv_ > 1.0e-4f || loopHiWetEnv_ > 1.0e-4f;
            const float red = loopHiDet_.advanceUnit (ratio, loopHiCfg_, present);
            bandedL -= red * loopHiCutL_.band (bandedL);
            bandedR -= red * loopHiCutR_.band (bandedR);
        }

        // Apply user feedback gain × kFeedbackLoopAttn, then softClip
        // as a runaway safety net.
        const float fbL = DspUtils::softClip (bandedL * fb * kFeedbackLoopAttn,
                                              kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);
        const float fbR = DspUtils::softClip (bandedR * fb * kFeedbackLoopAttn,
                                              kFeedbackSoftClipKnee, kFeedbackSoftClipCeil);

        // Dry-fed octave cascade (mono, feed-forward): each −12 st stage pitches the previous
        // stage's output down another octave, summed at its own gain → an independent, EVEN
        // 500/250/125/62 Hz descent (the intended structure) that does NOT depend on the feedback
        // loop. Fed from the raw dry (inL/inR), so a transient generates the full cascade in one
        // pass instead of starving the feedback voices. octActive_ false → skipped → bit-null.
        float octMono = 0.0f;
        if (octActive_)
        {
            // Feed SILENCE while frozen: the octave cascade reads the LIVE dry input (unlike the
            // pitch voices, which read the frozen feedback), so tracking live input during a freeze
            // would leak that freeze-time material into the octaves on un-freeze. Silence drains
            // the grain buffers so the held tank stays clean and the cascade resumes from the live
            // input on release. (When frozen the reverb discards its input anyway; this keeps the
            // generator state aligned with the freeze so there's no unfreeze transient.)
            float src = frozen_ ? 0.0f : 0.5f * (inL[n] + inR[n]);
            for (int i = 0; i < octaveCount; ++i)
            {
                src = octGenL_[i].process (src);        // cascade: 1k→500→250→125→62
                if (timedOctaves_ && i < kTimedStages)
                {
                    // Give the first descending octave a little more attack
                    // while later stages receive the entire spread.
                    if (i == 0) octMono += 0.12f * octGain_[0] * src;
                    src = octaveSpreads_[i].process (src);
                }
                octMono += octGain_[i] * src;
            }
            octMono *= octaveScale;
        }

        // Mix node: dry input + pitched feedback + dry-fed octaves → reverb input.
        reverbInL_[static_cast<size_t> (n)] = driveL + fbL + octMono;
        reverbInR_[static_cast<size_t> (n)] = driveR + fbR + octMono;

        if (++readPos >= delayLength) readPos = 0;
    }

    // ── Pass 2: reverb on the (dry + pitched-fb) mix → wet. ──
    if (useDenseReverb_)
        denseReverb_.process (reverbInL_.data(), reverbInR_.data(),
                              wetL_.data(),      wetR_.data(),      numSamples);
    else
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

        // wet (with up + down pitched feedback already folded in by the loop) →
        // delay line. Each voice reads its own return time on the next pass.
        fbDelayLineL_[static_cast<size_t> (fbDelayWritePos_)] = wL;
        fbDelayLineR_[static_cast<size_t> (fbDelayWritePos_)] = wR;
        if (++fbDelayWritePos_ >= delayLength) fbDelayWritePos_ = 0;

        float oL = wL, oR = wR;

        // Tail spin-comb on the OUTPUT ONLY (post-feedback-write → the recirculating
        // cascade stays un-spun). Smears the FDN's metallic HF; off → oL/oR untouched.
        if (useTailSpin_) tailSpin_.process (oL, oR);

        // Stereo modulation on the OUTPUT ONLY (post-feedback-write so the loop is
        // unaffected). depth 0 → struct inactive → oL/oR unchanged → bit-null.
        // Post-loop HF-air voice (genuine >12 kHz air; bypasses the reverb HF-damp + loop LPF).
        // Taps the raw wet (wL/wR), pitches its 6-12 kHz up to 12-24 k. mix 0 → bit-null.
        float airL = wL, airR = wR;
        // Both the octave voice and the noise envelope follow the filtered wet signal.
        air_.filterInput (airL, airR);
        if (air_.active) air_.process (airL, airR, oL, oR);
        if (stereoMod_.active) stereoMod_.process (oL, oR);
        // Tail noise floor — envelope-tracked to the wet, fades with the decay (a
        // dense noise-like fade; masks the sparse-mode ring). gain 0 → skipped → bit-null.
        // NOISE SUSTAIN-DUCK (2026-07-08): the noise's gentle LP leaks 8-16 kHz; on a
        // 22 s sustained stem the envelope-tracked noise runs continuously → measured
        // +24 dB of Black Hole's piano-air +36.9. Duck the noise while INPUT sustains
        // (the piano-air window); it returns within ~250 ms of input-off, so the
        // release-measured benefits (T60-16k lift, cent_500, spec_L1) survive.
        // duckAmt 0 → the legacy call → bit-null.
        if (tailNoise_.active())
        {
            if (noiseDuckAmt_ > 0.0f)
            {
                const bool present = noiseDuckKey_.advance (std::abs (inL[n]) + std::abs (inR[n]));
                const float tgt = present ? (1.0f - noiseDuckAmt_) : 1.0f;
                noiseDuckG_ += noiseDuckSlew_ * (tgt - noiseDuckG_);
                float nl = 0.0f, nr = 0.0f;
                tailNoise_.process (airL, airR, nl, nr);
                oL += noiseDuckG_ * nl;
                oR += noiseDuckG_ * nr;
            }
            else
                tailNoise_.process (airL, airR, oL, oR);
        }
        // Output limiter. h==1 → plain tanh (bit-null). h>1 → h*tanh(x/h): the
        // knee moves to ±h so long-decay buildup (Deep Blue Day) stays linear
        // and doesn't emit odd-harmonic (3k/5k) grit on sustained tones.
        const float gL = oL * kWetOutputGain, gR = oR * kWetOutputGain;
        const float h = outputHeadroom_;
        outL[n] = (h == 1.0f) ? std::tanh (gL) : h * std::tanh (gL / h);
        outR[n] = (h == 1.0f) ? std::tanh (gR) : h * std::tanh (gR / h);
    }
}
