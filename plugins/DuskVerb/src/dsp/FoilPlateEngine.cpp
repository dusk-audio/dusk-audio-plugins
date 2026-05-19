#include "FoilPlateEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// =====================================================================
// Primitive bodies — see FoilPlateEngine.h for design rationale.
// =====================================================================

namespace foil_plate
{

namespace
{
    constexpr float kTwoPi      = 6.283185307179586f;
    constexpr float kSafetyClip = 4.0f;
}

// ----- DelayLine ----------------------------------------------------

void DelayLine::allocate (int maxSamples)
{
    const int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask     = size - 1;
    writePos = 0;
}

void DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void DelayLine::write (float sample)
{
    buffer[static_cast<size_t> (writePos)] = sample;
    writePos = (writePos + 1) & mask;
}

float DelayLine::read (int delaySamples) const
{
    return buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
}

float DelayLine::readInterpolated (float delaySamples) const
{
    int   intPart  = static_cast<int> (delaySamples);
    float fracPart = delaySamples - static_cast<float> (intPart);
    int   readPos  = (writePos - intPart - 1) & mask;
    return DspUtils::lagrange6 (buffer.data(), mask, readPos, 1.0f - fracPart);
}

// ----- Allpass ------------------------------------------------------

void Allpass::allocate (int maxSamples)
{
    const int size = DspUtils::nextPowerOf2 (std::max (maxSamples + 4, 16));
    buffer.assign (static_cast<size_t> (size), 0.0f);
    mask     = size - 1;
    writePos = 0;
}

void Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float Allpass::process (float input, float g, float modValue)
{
    float vd;
    if (modDepthSamples > 0.0f)
    {
        const float jitter  = modValue * modDepthSamples;
        const float readPos = static_cast<float> (writePos)
                            - static_cast<float> (delaySamples)
                            - jitter;
        int   intIdx = static_cast<int> (std::floor (readPos));
        const float frac = readPos - static_cast<float> (intIdx);
        intIdx = static_cast<int> (static_cast<unsigned int> (intIdx)
                                    & static_cast<unsigned int> (mask));
        vd = DspUtils::lagrange6 (buffer.data(), mask, intIdx, frac);
    }
    else
    {
        vd = buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
    }
    const float vn = input + g * vd;
    buffer[static_cast<size_t> (writePos)] = vn;
    writePos = (writePos + 1) & mask;
    return vd - g * vn;
}

// ----- SineLFO ------------------------------------------------------

void SineLFO::prepare (float sampleRate, float rateHz, float initialPhaseRad)
{
    phase    = initialPhaseRad;
    phaseInc = kTwoPi * rateHz / sampleRate;
    // depth left untouched — set externally per use case.
}

float SineLFO::next()
{
    const float s = std::sin (phase);
    phase += phaseInc;
    if (phase >= kTwoPi) phase -= kTwoPi;
    return s * depth;
}

// LR4BandSplit method bodies now live in the shared header
// plugins/DuskVerb/src/dsp/LR4BandSplit.h (header-only struct under
// duskverb::dsp::LR4BandSplit). FoilPlateEngine.h re-exports the type
// under foil_plate::LR4BandSplit via a using-declaration, so the rest of
// this file and all dependents (BandReverberator::fbBiquadA/B etc.) keep
// compiling unchanged.

// ----- BandReverberator --------------------------------------------

void BandReverberator::prepare (double sr, int baseDelay, float modRateHz,
                                float modPhaseRad)
{
    baseDelaySamples = baseDelay;
    // Buffer sized for max-size scale 1.5× to allow size-knob growth
    // without audio-thread realloc.
    constexpr float kMaxSizeScale = 1.5f;
    const float scaledForRate = static_cast<float> (baseDelay)
                              * static_cast<float> (sr / 48000.0)
                              * kMaxSizeScale + 32.0f;
    delay.allocate (static_cast<int> (scaledForRate));
    delay.clear();
    modLFO.prepare (static_cast<float> (sr), modRateHz, modPhaseRad);
    modLFO.setDepth (0.0f);  // overwritten by setModDepth
}

void BandReverberator::clear()
{
    delay.clear();
    fbBiquadA.reset();
    fbBiquadB.reset();
    // LFO phase intentionally NOT reset on clearBuffers — keeps the
    // deterministic phase relationship after a host transport jump.
}

void BandReverberator::setFeedbackFilter (FbFilterType type, float fcHz, float sr)
{
    if (type == FbFilterType::Bypass)
    {
        fbBiquadEnabled = false;
        return;
    }
    if (type == FbFilterType::LowPass)
    {
        fbBiquadA.designLP (fcHz, sr);
        fbBiquadB.designLP (fcHz, sr);
    }
    else
    {
        fbBiquadA.designHP (fcHz, sr);
        fbBiquadB.designHP (fcHz, sr);
    }
    fbBiquadEnabled = true;
}

float BandReverberator::process (float input)
{
    const float modSample = modLFO.next();
    const float effectiveDelay = static_cast<float> (baseDelaySamples) + modSample;
    float fb = delay.readInterpolated (effectiveDelay);
    // Run feedback through optional 4th-order LR4 filter — keeps each
    // band's loop locked to its measurement octave (no harmonics
    // bleeding upstream into neighbouring bands).
    if (fbBiquadEnabled)
        fb = fbBiquadB.process (fbBiquadA.process (fb));
    const float toWrite = input + fb * feedbackGain;
    delay.write (toWrite);
    // Normalise output to unity DC gain. Each comb's geometric series
    // accumulates to 1/(1-g) without this; combined with cross-feed at
    // 0.62 the loop blows up. Scaling the read by (1-g) flattens the
    // band's DC gain to 1.0 so the cross-coupled topology stays stable.
    return fb * (1.0f - feedbackGain);
}

// ----- OnsetEnvelope -----------------------------------------------

void OnsetEnvelope::setShape (float holdMs, float tauSec, float minGainArg)
{
    // Caller must invoke setShape before prepare so the ramp coefficient
    // and hold-sample count are computed from the correct sample rate.
    minGain         = minGainArg;
    pendingTauSec_  = tauSec;
    pendingHoldMs_  = holdMs;
}

void OnsetEnvelope::prepare (float sr)
{
    // Peak detector decay: τ ≈ 100 ms (slow enough that sustained
    // material keeps peakHold up, so the "fresh transient" trigger only
    // fires on attacks above peakHold × triggerRatio).
    peakDecayCoeff = std::exp (-1.0f / (0.1f * sr));
    // Envelope ramp τ from setShape (default 0.050 s if not set).
    const float tauSec = pendingTauSec_ > 0.0f ? pendingTauSec_ : 0.050f;
    envRampCoeff   = std::exp (-1.0f / (tauSec * sr));
    // Hold duration (default 0 = no hold, same as original single-pole).
    holdSamples    = static_cast<int> (pendingHoldMs_ * 0.001f * sr);
    clear();
}

void OnsetEnvelope::clear()
{
    peakHold     = 0.0f;
    envelope     = 1.0f;
    prevAbsInput = 0.0f;
}

float OnsetEnvelope::process (float input)
{
    const float absIn = std::fabs (input);

    // Rising-edge trigger: input level jumps above peakHold × ratio AND
    // above an absolute threshold. The latter prevents firing on noise
    // floor during silence.
    const bool fresh = (absIn > peakHold * triggerRatio)
                    && (absIn > triggerThresh)
                    && (absIn > prevAbsInput);
    if (fresh)
    {
        envelope     = minGain;
        holdRemaining = holdSamples;
    }

    // Hold env at minGain for holdSamples; then ramp toward 1.0.
    if (holdRemaining > 0)
        --holdRemaining;
    else
        envelope = 1.0f + envRampCoeff * (envelope - 1.0f);

    // Peak detector: fast attack, slow decay.
    peakHold = std::max (absIn, peakHold * peakDecayCoeff);
    prevAbsInput = absIn;

    return envelope;
}

} // namespace foil_plate


// =====================================================================
// FoilPlateEngine — top-level
// =====================================================================

FoilPlateEngine::FoilPlateEngine() = default;

void FoilPlateEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    const float sr  = static_cast<float> (sampleRate);
    const float rateRatio = sr / 48000.0f;

    auto setupBranch = [&] (Branch& b, int in1Base, int in2Base,
                            int bassBase, int midBase, int trebleBase,
                            float lfoPhaseOffset)
    {
        const int predelayMaxSamples = static_cast<int> (
            static_cast<float> (kPredelayMaxSamplesAt48k) * rateRatio) + 1;
        b.predelay.allocate (predelayMaxSamples + 64);
        b.predelay.clear();

        b.in1.allocate (static_cast<int> (in1Base * rateRatio + 16.0f));
        b.in1.delaySamples    = static_cast<int> (in1Base * rateRatio);
        b.in1.modDepthSamples = 0.0f;
        b.in1.clear();

        b.in2.allocate (static_cast<int> (in2Base * rateRatio + 16.0f));
        b.in2.delaySamples    = static_cast<int> (in2Base * rateRatio);
        b.in2.modDepthSamples = 0.0f;
        b.in2.clear();

        b.split.prepare (sr);
        b.split.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);

        b.bassRev  .prepare (sampleRate,
                             static_cast<int> (bassBase   * rateRatio),
                             kBassLFORateHz,
                             lfoPhaseOffset);
        b.midRev   .prepare (sampleRate,
                             static_cast<int> (midBase    * rateRatio),
                             kMidLFORateHz,
                             lfoPhaseOffset);
        b.trebleRev.prepare (sampleRate,
                             static_cast<int> (trebleBase * rateRatio),
                             kTrebleLFORateHz,
                             lfoPhaseOffset);
        // Lock each band's loop to its measurement octave via a 4th-order
        // LR4 in the feedback path. Mid stays broadband (its passband
        // already sits comfortably between the two xovers).
        b.bassRev  .setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::LowPass,
            crossoverFreq_, sr);
        b.midRev   .setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::Bypass,
            0.0f, sr);
        b.trebleRev.setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::HighPass,
            highCrossoverFreq_, sr);

        b.onsetEnv.prepare (sr);   // retained for API parity; output not applied
        b.crossFeedState = 0.0f;
        b.prevBassOut    = 0.0f;
        b.prevMidOut     = 0.0f;
        b.prevTrebleOut  = 0.0f;
    };

    setupBranch (leftBranch_,
                 kLeftIn1Base, kLeftIn2Base,
                 kLeftBassBase, kLeftMidBase, kLeftTrebleBase,
                 0.0f);
    setupBranch (rightBranch_,
                 kRightIn1Base, kRightIn2Base,
                 kRightBassBase, kRightMidBase, kRightTrebleBase,
                 kRightLFOPhaseRad);

    updateBandFeedback();

    // Initial LFO depth comes from current modDepth setting.
    setModDepth (modDepth_);

    prepared_ = true;
}

void FoilPlateEngine::clearBuffers()
{
    auto clearBranch = [] (Branch& b)
    {
        b.predelay.clear();
        b.in1.clear();
        b.in2.clear();
        b.split.reset();
        b.bassRev.clear();
        b.midRev.clear();
        b.trebleRev.clear();
        b.onsetEnv.clear();
        b.crossFeedState = 0.0f;
        b.prevBassOut    = 0.0f;
        b.prevMidOut     = 0.0f;
        b.prevTrebleOut  = 0.0f;
    };
    clearBranch (leftBranch_);
    clearBranch (rightBranch_);
}

void FoilPlateEngine::process (const float* inputL, const float* inputR,
                               float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
    {
        std::memset (outputL, 0, sizeof (float) * static_cast<size_t> (numSamples));
        std::memset (outputR, 0, sizeof (float) * static_cast<size_t> (numSamples));
        return;
    }

    // Multi-tap predelay positions (sample-rate scaled).
    const float rateRatio = static_cast<float> (sampleRate_) / 48000.0f;
    const int tap0Samples = static_cast<int> (kTap0SamplesAt48k * rateRatio) + 1;
    const int tap1Samples = static_cast<int> (kTap1SamplesAt48k * rateRatio) + 1;
    const int tap2Samples = static_cast<int> (kTap2SamplesAt48k * rateRatio) + 1;
    const int tap3Samples = static_cast<int> (kTap3SamplesAt48k * rateRatio) + 1;
    const int tap4Samples = static_cast<int> (kTap4SamplesAt48k * rateRatio) + 1;
    const int tap5Samples = static_cast<int> (kTap5SamplesAt48k * rateRatio) + 1;
    const int tap6Samples = static_cast<int> (kTap6SamplesAt48k * rateRatio) + 1;

    // Front-end per-branch: predelay multi-tap → AP diffuser →
    // LR4 split. Produces the three band-input signals; tap3 is summed
    // into each band's input so it still recirculates through the tank
    // (acts like a delayed re-injection, not an output-side specular
    // bypass — bypass produced an audible early spike that wrecked C80
    // and D50 in the first build of this topology).
    auto frontEnd = [&] (Branch& b, float dryIn,
                         float& outBass, float& outMid, float& outTreble)
    {
        b.predelay.write (dryIn);
        const float t0 = b.predelay.read (tap0Samples);
        const float t1 = b.predelay.read (tap1Samples);
        const float t2 = b.predelay.read (tap2Samples);
        const float t3 = b.predelay.read (tap3Samples);
        const float t4 = b.predelay.read (tap4Samples);
        const float t5 = b.predelay.read (tap5Samples);
        const float t6 = b.predelay.read (tap6Samples);

        // ─── 2-AP diffuser with staggered tap injections ───
        // AP1 input is the SUM of all primary-path taps: tap0 fires
        // the impulse, tap6 fills the 60–80 ms EDT-suppressing valley,
        // tap4 fires the 110 ms secondary peak, tap5 sustains the
        // 150-250 ms region.
        const float ap1In  = kTap0Weight * t0
                           + kTap6Weight * t6
                           + kTap4Weight * t4
                           + kTap5Weight * t5;
        const float ap1Out = b.in1.process (ap1In, kInputAPGain, 0.0f);
        // tap1 → AP2 input (sum).
        const float ap2In  = ap1Out + kTap1Weight * t1;
        const float ap2Out = b.in2.process (ap2In, kInputAPGain, 0.0f);
        // tap2 → split input (sum).
        const float splitIn = ap2Out + kTap2Weight * t2;

        // ─── LR4 3-band split ───
        b.split.split (splitIn, outBass, outMid, outTreble);

        // tap3 → each band input (sum). Per-band weighting splits tap3
        // evenly across the three loops so its total injection energy
        // matches w3. Each band loop independently band-filters the
        // late re-injection.
        const float t3WeightPerBand = kTap3Weight * (1.0f / 3.0f);
        outBass   += t3WeightPerBand * t3;
        outMid    += t3WeightPerBand * t3;
        outTreble += t3WeightPerBand * t3;
    };

    for (int n = 0; n < numSamples; ++n)
    {
        float L_bass, L_mid, L_treble;
        float R_bass, R_mid, R_treble;

        frontEnd (leftBranch_,  inputL[n],  L_bass, L_mid, L_treble);
        frontEnd (rightBranch_, inputR[n],  R_bass, R_mid, R_treble);

        // ─── Figure-8 per-band cross-coupling ───
        // Each band's L input mixes the previous-sample R-loop output
        // (scaled by its band-specific α). Bass uses +α (drives corr
        // toward +1 in low band), treble uses −α (drives corr toward
        // −1 in high band). Mid is neutral. The opposite-sign bands
        // cancel each other in the broadband cross-correlation while
        // EACH band's modal evolution is individually locked.
        const float L_bass_in   = L_bass   + kFigure8AlphaBass   * rightBranch_.prevBassOut;
        const float L_mid_in    = L_mid    + kFigure8AlphaMid    * rightBranch_.prevMidOut;
        const float L_treble_in = L_treble + kFigure8AlphaTreble * rightBranch_.prevTrebleOut;

        const float R_bass_in   = R_bass   + kFigure8AlphaBass   * leftBranch_.prevBassOut;
        const float R_mid_in    = R_mid    + kFigure8AlphaMid    * leftBranch_.prevMidOut;
        const float R_treble_in = R_treble + kFigure8AlphaTreble * leftBranch_.prevTrebleOut;

        // ─── Run each band-reverberator ───
        const float L_bassOut   = leftBranch_ .bassRev  .process (L_bass_in);
        const float L_midOut    = leftBranch_ .midRev   .process (L_mid_in);
        const float L_trebleOut = leftBranch_ .trebleRev.process (L_treble_in);
        const float R_bassOut   = rightBranch_.bassRev  .process (R_bass_in);
        const float R_midOut    = rightBranch_.midRev   .process (R_mid_in);
        const float R_trebleOut = rightBranch_.trebleRev.process (R_treble_in);

        // ─── Update previous-sample outputs AFTER both branches done ───
        leftBranch_ .prevBassOut   = L_bassOut;
        leftBranch_ .prevMidOut    = L_midOut;
        leftBranch_ .prevTrebleOut = L_trebleOut;
        rightBranch_.prevBassOut   = R_bassOut;
        rightBranch_.prevMidOut    = R_midOut;
        rightBranch_.prevTrebleOut = R_trebleOut;

        // ─── Sum bands ───
        const float lWet = L_bassOut + L_midOut + L_trebleOut;
        const float rWet = R_bassOut + R_midOut + R_trebleOut;

        // ─── Linear output decorrelation mixer ───
        // Reduces residual broadband correlation introduced by the
        // strong figure-8 coupling. Time-invariant → stab preserved.
        const float lOut = kOutMixA * lWet - kOutMixB * rWet;
        const float rOut = kOutMixA * rWet - kOutMixB * lWet;

        outputL[n] = lOut * kEngineOutputGain;
        outputR[n] = rOut * kEngineOutputGain;
    }
}

// =====================================================================
// Setters
// =====================================================================

void FoilPlateEngine::setDecayTime (float seconds)
{
    decayTime_ = std::max (0.05f, seconds);
    if (prepared_) updateBandFeedback();
}

void FoilPlateEngine::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (0.05f, mult);
    if (prepared_) updateBandFeedback();
}

void FoilPlateEngine::setMidMultiply (float mult)
{
    midMultiply_ = std::max (0.05f, mult);
    if (prepared_) updateBandFeedback();
}

void FoilPlateEngine::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (0.05f, mult);
    if (prepared_) updateBandFeedback();
}

void FoilPlateEngine::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::clamp (hz, 20.0f, 20000.0f);
    if (prepared_)
    {
        const float sr = static_cast<float> (sampleRate_);
        leftBranch_ .split.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
        rightBranch_.split.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
        // Re-tune the bass-loop feedback LP at the new xover so the loop
        // stays locked to its band.
        leftBranch_ .bassRev.setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::LowPass,
            crossoverFreq_, sr);
        rightBranch_.bassRev.setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::LowPass,
            crossoverFreq_, sr);
    }
}

void FoilPlateEngine::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::clamp (hz, 20.0f, 20000.0f);
    if (prepared_)
    {
        const float sr = static_cast<float> (sampleRate_);
        leftBranch_ .split.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
        rightBranch_.split.setCrossovers (crossoverFreq_, highCrossoverFreq_, sr);
        // Re-tune the treble-loop feedback HP at the new xover.
        leftBranch_ .trebleRev.setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::HighPass,
            highCrossoverFreq_, sr);
        rightBranch_.trebleRev.setFeedbackFilter (
            foil_plate::BandReverberator::FbFilterType::HighPass,
            highCrossoverFreq_, sr);
    }
}

void FoilPlateEngine::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
}

void FoilPlateEngine::setModDepth (float depth)
{
    modDepth_ = std::clamp (depth, 0.0f, 1.0f);
    // Map 0..1 to ±0..8 samples of delay-line wander per band. Bigger
    // depth = wider modulation = more apparent chorus on the tail.
    const float depthSamples = 8.0f * modDepth_;
    leftBranch_ .bassRev  .modLFO.setDepth (depthSamples);
    leftBranch_ .midRev   .modLFO.setDepth (depthSamples);
    leftBranch_ .trebleRev.modLFO.setDepth (depthSamples);
    rightBranch_.bassRev  .modLFO.setDepth (depthSamples);
    rightBranch_.midRev   .modLFO.setDepth (depthSamples);
    rightBranch_.trebleRev.modLFO.setDepth (depthSamples);
}

void FoilPlateEngine::setModRate (float /*hz*/)
{
    // Per-band LFO rates are fixed constants (Pillar 2) — the user-facing
    // Mod Rate knob is intentionally a no-op on this engine. Deterministic
    // anti-correlated modulation is the point; making the user retune it
    // would break stereo_corr_stability guarantees.
}

void FoilPlateEngine::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    // Size affects baseDelaySamples via 0.5..1.5 scale. Recomputing on the
    // audio thread is OK because the underlying ring buffers were sized
    // at prepare() with the 1.5× headroom factor — no realloc.
    const float scale = 0.5f + sizeParam_;
    auto scaleBand = [scale] (foil_plate::BandReverberator& r, int base)
    {
        r.baseDelaySamples = static_cast<int> (static_cast<float> (base) * scale);
    };
    const float rateRatio = static_cast<float> (sampleRate_ / 48000.0);
    scaleBand (leftBranch_ .bassRev,
               static_cast<int> (kLeftBassBase   * rateRatio));
    scaleBand (leftBranch_ .midRev,
               static_cast<int> (kLeftMidBase    * rateRatio));
    scaleBand (leftBranch_ .trebleRev,
               static_cast<int> (kLeftTrebleBase * rateRatio));
    scaleBand (rightBranch_.bassRev,
               static_cast<int> (kRightBassBase   * rateRatio));
    scaleBand (rightBranch_.midRev,
               static_cast<int> (kRightMidBase    * rateRatio));
    scaleBand (rightBranch_.trebleRev,
               static_cast<int> (kRightTrebleBase * rateRatio));
    if (prepared_) updateBandFeedback();
}

void FoilPlateEngine::setFreeze (bool frozen)
{
    frozen_ = frozen;
    // Freeze drives feedback gain to ~1.0 per band; updateBandFeedback
    // routes the override.
    if (prepared_) updateBandFeedback();
}

void FoilPlateEngine::setTankDiffusion (float /*amount*/)
{
    // No tank-diffusion knob on this engine: density is fixed by the
    // 2-AP front + per-band loop topology (Pillar 3). Setter accepted
    // for API parity but currently a no-op.
}

void FoilPlateEngine::setInputHighShelf (float /*gainDb*/, float /*fcHz*/)
{
    // Input shelf is not wired on this engine — the LR4 split + per-band
    // gains cover all HF shaping needs. Accept the setter for API parity.
}

// =====================================================================
// updateBandFeedback — convert RT60 / multipliers → per-band feedback
// =====================================================================
//
// Standard reverberation formula:
//   RT60 = 60dB / (−20·log10(g)) × (loopTime)
// Solved for g given target effective RT60:
//   g = 10^(−3 · loopTime / effRt60)
//
// loopTime here is per-band loop delay / sampleRate. Each band has its
// own loop, so per-band g maps directly to per-band RT60.

void FoilPlateEngine::updateBandFeedback()
{
    const float sr = static_cast<float> (sampleRate_);
    constexpr float kMaxFeedback = 0.998f;

    auto solveG = [sr, kMaxFeedback] (int loopSamples, float effRt60) {
        const float loopTime = static_cast<float> (loopSamples) / sr;
        if (effRt60 <= 0.0001f || loopSamples <= 0) return 0.0f;
        const float exponent = -3.0f * loopTime / effRt60;
        return std::clamp (std::pow (10.0f, exponent), 0.0f, kMaxFeedback);
    };

    const float bassRt60   = frozen_ ? 1.0e6f : decayTime_ * bassMultiply_;
    const float midRt60    = frozen_ ? 1.0e6f : decayTime_ * midMultiply_;
    const float trebleRt60 = frozen_ ? 1.0e6f : decayTime_ * trebleMultiply_;

    leftBranch_ .bassRev  .feedbackGain = solveG (leftBranch_ .bassRev  .baseDelaySamples, bassRt60);
    leftBranch_ .midRev   .feedbackGain = solveG (leftBranch_ .midRev   .baseDelaySamples, midRt60);
    leftBranch_ .trebleRev.feedbackGain = solveG (leftBranch_ .trebleRev.baseDelaySamples, trebleRt60);
    rightBranch_.bassRev  .feedbackGain = solveG (rightBranch_.bassRev  .baseDelaySamples, bassRt60);
    rightBranch_.midRev   .feedbackGain = solveG (rightBranch_.midRev   .baseDelaySamples, midRt60);
    rightBranch_.trebleRev.feedbackGain = solveG (rightBranch_.trebleRev.baseDelaySamples, trebleRt60);
}
