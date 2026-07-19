// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// TapeMachineDSP.cpp — orchestration layer (port of PluginProcessor.cpp's
// prepareToPlay / processBlock / updateFilters). See TapeMachineDSP.hpp and
// core/PORT_NOTES.md.

#include "TapeMachineDSP.hpp"

namespace duskaudio
{

//==============================================================================
// Tunable smoothing time-constants (exponential one-pole SmoothedValue replaces
// juce::SmoothedValue<Linear>). Chosen to settle in a similar time to the JUCE
// linear ramps (~3*tau ≈ ramp length). See PORT_NOTES for the shape change.
//==============================================================================
static constexpr float kGainTau  = 0.0067f; // ~20 ms settle (juce::dsp::Gain 20 ms ramp)
static constexpr float kSatTau   = 0.05f;   // ~150 ms settle (smoothedSaturation 150 ms)
static constexpr float kParamTau = 0.0067f; // ~20 ms settle (wow / flutter / noise 20 ms)

//==============================================================================
// Signal-level FR compensation (Phase B). The tape core's FR drifts with record
// flux (level+inGain+cal+bias): deep LF thickens above the -12 dBFS reference (the
// LF-cut terms below), and the shaper compresses HF. kRampPow shapes the -12..0 dBFS
// window (gamma so -6 dBFS lands near 40% of full). Zero at/below -12 dBFS flux, so
// the shelves are neutral at the -12 preset-validation anchor by construction.
//
// BRIGHTNESS FIX (2026-07-12): the HF-restore gains were originally fit to the
// private calibration analysis SINGLE-TONE delta surface (HF ~+5 dB @8-15k @0 dBFS). That target was an
// artifact: a lone loud HF *tone* self-compresses in the shaper (~5 dB @0 dBFS) and the
// shelf restored it -> good single-tone match. But on BROADBAND program the HF partials
// ride LOW on dominant LF/mid energy, are NOT self-compressed, yet this shelf (keyed on
// the loud broadband peak) still boosted them -> the audible +2.8..+4.3 dB rising
// 10-16k tilt on loud sustained material (falsified by the control: forcing these to 0
// collapsed the tilt to <=0.6 dB, and the "dull drums" the shelf supposedly fixed were
// not dull without it). private calibration analysis measured the TRUE broadband HF loss vs reference:
// FLAT and tiny across all levels -- Swiss ~+0.4 dB (already slightly bright -> no
// restore), American ~-0.5..-0.9 dB (slightly dark -> a small level-keyed lift).
// Swiss set to 0; Classic to a small value (a broad 6k high-shelf can't flatten the
// residual S-tilt fully -- 2.50 minimises the worst band while keeping the top octave
// safe). See private comparison harness memory (brightness campaign) for the full tables.
static constexpr float kRampPow        = 1.30f;  // flux->factor gamma over the -12..0 window
static constexpr float kLevelHfSwiss   = 0.00f;  // Swiss  level-keyed HF restore (broadband HF loss ~0)
static constexpr float kLevelHfAmerican = 2.50f;  // American level-keyed HF restore (small; Classic runs ~0.5 dB dark)
static constexpr float kLevelLfSwiss   = 2.90f;  // Swiss  LF-cut peak dB at full flux (@32 Hz Q1.4)
static constexpr float kLevelLfAmerican = 5.60f;  // American LF-cut peak dB at full flux (@30 Hz Q1.6)

// Below-anchor decay of the KNOB-keyed driveHfComp (the "crest sizzle" fix). The static
// driveHfComp restores the HF the shaper compresses AT the -12 dBFS operating flux, but it
// is CONSTANT with signal level: below the anchor the shaper stops compressing HF (linear
// regime) yet the static restore stays fully bright, so on quiet passages / the tail of a
// drum hit mine reads +3-4 dB brighter @8-15k than the reference (whose record brightness FADES,
// and actually DARKENS the HF, as flux drops below -12). This multiplies driveHfCompDb by a
// signal-flux-keyed factor that is EXACTLY 1.0 at/above the anchor (neutrality: every preset
// FR fit was validated at the -12 dBFS gate, and driveHfComp is ~0 for all reference/Classic/
// low-drive presets, so those are untouched either way) and decays below it, crossing 0 into
// a small negative floor so the shelf actively CUTS HF at low flux to follow the reference's
// measured low-level darkening. Fit to the private calibration analysis below-anchor drift on Drum Bus + Old
// Tape (both Swiss, drift ~+2.8/-6, +3.6/-12, +3.9/-18 dB below anchor @10k). tau in dB.
static constexpr float kDriveDecayDepth = 1.75f; // floor depth: factor -> (1-depth) as flux drops far below anchor
static constexpr float kDriveDecayTau   = 5.0f;  // dB below anchor for the decay's e-fold

// Calibration trim in dB, indexed by the Calibration choice (matches the reference
// Cal Level labels +3/+6/+7.5/+9). Was a uniform pCalibration*3; the +7.5 rung
// breaks the arithmetic progression, so it is a table now.
static inline float calibrationDbFromIndex (int idx) noexcept
{
    static constexpr float kCalDb[4] = { 3.0f, 6.0f, 7.5f, 9.0f };
    return kCalDb[idx < 0 ? 0 : (idx > 3 ? 3 : idx)];
}

// Auto-Cal optimal bias from tape type + speed (the calibrated operating point). Shared by
// the prepare() snap and the per-block processBlock target so both start from the same value
// (snapping to a fixed 0.5 mismatched the real target and ramped on the first block).
static inline float autoCalBiasFromTypeSpeed (TapeCore::TapeType type, TapeCore::TapeSpeed speed) noexcept
{
    float optimalBias = 0.5f;
    switch (type)
    {
        case TapeCore::FormulaClassic: optimalBias = 0.50f; break;
        case TapeCore::FormulaHighOutput: optimalBias = 0.55f; break;
        case TapeCore::FormulaModern: optimalBias = 0.54f; break;
        case TapeCore::FormulaVintage: optimalBias = 0.45f; break;
    }
    switch (speed)
    {
        case TapeCore::Speed_7_5_IPS:  optimalBias *= 1.05f; break;
        case TapeCore::Speed_15_IPS: break;
        case TapeCore::Speed_30_IPS:   optimalBias *= 0.95f; break;
        case TapeCore::Speed_3_75_IPS: optimalBias *= 1.08f; break;
    }
    return std::clamp (optimalBias, 0.0f, 1.0f);
}


//==============================================================================
void TapeMachineDSP::prepare (double sampleRate, int maxBlockSize)
{
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    if (maxBlockSize <= 0) maxBlockSize = 512;

    baseSampleRate = sampleRate;
    maxBlock       = maxBlockSize;
    maxOsRate      = baseSampleRate * 4.0;

    currentFactor = factorFromChoice (pOversampling.load (std::memory_order_relaxed));
    currentOsRate = baseSampleRate * static_cast<double> (currentFactor);
    lastFactor    = currentFactor;

    // Deterministic RNG streams: distinct constant seeds per channel (PORT_NOTES).
    coreL.setSeeds (1000u);
    coreR.setSeeds (2000u);
    sharedWowFlutter.setSeed (1u);

    osL.setFactor (currentFactor);
    osR.setFactor (currentFactor);
    osL.reset();
    osR.reset();

    coreL.prepare (currentOsRate, currentFactor, maxOsRate);
    coreR.prepare (currentOsRate, currentFactor, maxOsRate);
    sharedWowFlutter.prepare (currentOsRate, currentFactor, maxOsRate);

    const float hpFreq = pHighpassHz.load (std::memory_order_relaxed);
    const float lpFreq = pLowpassHz.load (std::memory_order_relaxed);
    const float lpQ    = pLpQ.load (std::memory_order_relaxed);   // preset LP resonance (0.707 = historic fixed Q)
    hpL.prepare (currentOsRate, hpFreq, 0.707f); hpL.setType (DuskSVF::Type::highpass); hpL.reset();
    hpR.prepare (currentOsRate, hpFreq, 0.707f); hpR.setType (DuskSVF::Type::highpass); hpR.reset();
    lpL.prepare (currentOsRate, lpFreq, lpQ);   lpL.setType (DuskSVF::Type::lowpass);  lpL.reset();
    lpR.prepare (currentOsRate, lpFreq, lpQ);   lpR.setType (DuskSVF::Type::lowpass);  lpR.reset();
    bypassLowpass = (lpFreq >= 19000.0f);
    lastHpFreq = hpFreq;
    lastLpFreq = lpFreq;
    lastLpQ    = lpQ;

    // Smoothers: param smoothers configured at BASE rate but advanced at the
    // oversampled rate (matches the JUCE structure — see PORT_NOTES). Output gain
    // is advanced at the oversampled rate so it is configured there.
    inGain.prepare  (baseSampleRate, kGainTau);
    outGain.prepare (currentOsRate,  kGainTau);
    smSat.prepare     (baseSampleRate, kSatTau);
    smBias.prepare    (baseSampleRate, kSatTau);
    smWow.prepare     (baseSampleRate, kParamTau);
    smFlutter.prepare (baseSampleRate, kParamTau);
    smNoise.prepare   (baseSampleRate, kParamTau);

    // Initial (snap) values matching PluginProcessor::prepareToPlay.
    const float inGainDb = pInputGainDb.load (std::memory_order_relaxed);
    inGain.snap  (dbToGain (inGainDb));
    {
        outGain.snap (pAutoComp.load (std::memory_order_relaxed)
                          ? dbToGain (-inGainDb + pOutputGainDb.load (std::memory_order_relaxed))  // gain link = inverse + Output trim
                          : dbToGain (pOutputGainDb.load (std::memory_order_relaxed)));
    }
    const float initSat = std::clamp (((inGainDb + 12.0f) / 24.0f) * 100.0f, 0.0f, 100.0f);
    smSat.snap     (initSat);
    {
        const auto initMachine = static_cast<TapeCore::TapeMachine> (clampI (pMachine.load (std::memory_order_relaxed), 0, 1));
        const auto initType    = static_cast<TapeCore::TapeType>    (clampI (pType.load  (std::memory_order_relaxed), 0, 3));
        // Normalize speed the same way processBlock does before autoCalBiasFromTypeSpeed:
        // the Swiss has no 3.75 IPS, so coerce it to 15 IPS. Without this the snap uses
        // the un-coerced 3.75 target and smBias ramps on the first block for a Swiss+3.75
        // preset/automation recall.
        int initSpeedIdx = clampI (pSpeed.load (std::memory_order_relaxed), 0, 3);
        if (initMachine == TapeCore::Swiss && initSpeedIdx == TapeCore::Speed_3_75_IPS)
            initSpeedIdx = TapeCore::Speed_15_IPS;
        const auto initSpeed = static_cast<TapeCore::TapeSpeed> (initSpeedIdx);
        smBias.snap    (pAutoCal.load (std::memory_order_relaxed)
                            ? autoCalBiasFromTypeSpeed (initType, initSpeed)
                            : std::clamp (pBias.load (std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    }
    smWow.snap     (pWow.load (std::memory_order_relaxed));
    smFlutter.snap (pFlutter.load (std::memory_order_relaxed));
    smNoise.snap   (pNoiseAmount.load (std::memory_order_relaxed) * 0.01f);

    // Scratch buffers (allocated here; never on the audio thread).
    inGainArr.assign  (static_cast<size_t> (maxBlock), 0.0f);
    const size_t osCap = static_cast<size_t> (maxBlock) * 4u;
    satArr.assign       (osCap, 0.0f);
    biasArr.assign      (osCap, 0.0f);
    wowFlutArr.assign   (osCap, 0.0f);
    noiseArr.assign     (osCap, 0.0f);
    sharedModArr.assign (osCap, 0.0f);
    outGainArr.assign   (osCap, 0.0f);

    // VU: standard ANSI C16.5 ballistics — a mean-abs (rectified) one-pole integrator.
    // tau = 300ms / ln(100) = 65.14 ms gives 99% of final deflection in 300 ms on tone
    // onset (and the same fall time), the accepted digital approximation of a VU meter.
    // Symmetric attack/release: state += (|x| - state) * alpha per sample at the base rate.
    // (Old ballistic was a peak-hold with 300 ms release, which read/responded too slowly.)
    {
        const float vuTau = 0.3f / std::log (100.0f);   // 65.14 ms
        vuBallisticAlpha = 1.0f - std::exp (-1.0f / (vuTau * static_cast<float> (baseSampleRate)));
    }
    // Clip-lamp peak hold: instant attack, 300 ms release (unchanged ballistic).
    peakDecayCoeff = std::exp (-1.0f / (0.3f * static_cast<float> (baseSampleRate)));

    // Signal-level envelope detector: instant attack, ~30 ms release (base rate).
    // Fast enough to track drum transients (the level-keyed EQ must shift tone on the
    // hit, not a block later) while smoothing the |x| ripple of low tones.
    m_levelRelCoeff = std::exp (-1.0f / (0.030f * static_cast<float> (baseSampleRate)));
    m_levelEnv = 0.0f;
    // Program-band envelope LP (Phase C): 500 Hz 2-pole Butterworth per channel, base rate.
    // Corner chosen so the hottest THD step (-3 dBFS 1 kHz) reads ~-15 dBFS after the LP,
    // a >3 dB margin below the -12 anchor => progFactor floored to 0 => byte-identical THD.
    m_progLpL.setCoeffs (DBiquad::lowPass (baseSampleRate, 500.0, 0.70710678));
    m_progLpR.setCoeffs (DBiquad::lowPass (baseSampleRate, 500.0, 0.70710678));
    m_progLpL.reset(); m_progLpR.reset();
    m_progEnv = 0.0f;
    // Level-comp factor smoother: fast attack (~4 ms) so tone shifts ON a transient, slower
    // release (~30 ms) tracking the detector. Per-sample coeffs (1-exp form); the block-rate
    // update raises (1-coeff) to nSamples so the effective TC is block-size-independent.
    m_levelFactAtkCoeff = 1.0f - std::exp (-1.0f / (0.004f * static_cast<float> (baseSampleRate)));
    m_levelFactRelCoeff = 1.0f - std::exp (-1.0f / (0.030f * static_cast<float> (baseSampleRate)));
    m_levelFactorSm = 0.0f;
    m_progFactorSm = 0.0f;    // program-band factor starts neutral (bypassed until sub-500 Hz program engages)
    m_driveDecaySm  = 1.0f;   // neutral (no below-anchor decay until the signal drops below -12)
    vuStateL = vuStateR = inVuStateL = inVuStateR = 0.0f;
    inPeakStateL = inPeakStateR = 0.0f;
    outPeakStateL = outPeakStateR = 0.0f;
    vuL.store (0.0f, std::memory_order_relaxed);
    vuR.store (0.0f, std::memory_order_relaxed);
    inVuL.store (0.0f, std::memory_order_relaxed);
    inVuR.store (0.0f, std::memory_order_relaxed);
    inPeakL.store (0.0f, std::memory_order_relaxed);
    inPeakR.store (0.0f, std::memory_order_relaxed);
    outPeakL.store (0.0f, std::memory_order_relaxed);
    outPeakR.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
void TapeMachineDSP::reset()
{
    osL.reset(); osR.reset();
    coreL.reset(); coreR.reset();
    // sharedWowFlutter has no dedicated reset(); re-zero its consumed state.
    if (! sharedWowFlutter.delayBuffer.empty())
        std::fill (sharedWowFlutter.delayBuffer.begin(), sharedWowFlutter.delayBuffer.end(), 0.0f);
    sharedWowFlutter.writeIndex = 0;
    sharedWowFlutter.wowRandCounter = 0;
    sharedWowFlutter.wowRandCurrent = sharedWowFlutter.wowRandTarget = 0.0f;
    sharedWowFlutter.wowPhase = 0.0;
    sharedWowFlutter.flutterPhase = 0.0;
    sharedWowFlutter.randomCurrent = 0.0f;
    sharedWowFlutter.randomCurrent2 = 0.0f;
    sharedWowFlutter.randomTarget = 0.0f;
    sharedWowFlutter.randomUpdateCounter = 0;

    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    m_levelEnv = 0.0f;
    m_levelFactorSm = 0.0f;
    m_progLpL.reset(); m_progLpR.reset();
    m_progEnv = 0.0f;
    m_progFactorSm = 0.0f;
    m_driveDecaySm  = 1.0f;
    vuStateL = vuStateR = inVuStateL = inVuStateR = 0.0f;
    inPeakStateL = inPeakStateR = 0.0f;
    outPeakStateL = outPeakStateR = 0.0f;
    vuL.store (0.0f, std::memory_order_relaxed);
    vuR.store (0.0f, std::memory_order_relaxed);
    inVuL.store (0.0f, std::memory_order_relaxed);
    inVuR.store (0.0f, std::memory_order_relaxed);
    inPeakL.store (0.0f, std::memory_order_relaxed);
    inPeakR.store (0.0f, std::memory_order_relaxed);
    outPeakL.store (0.0f, std::memory_order_relaxed);
    outPeakR.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
int TapeMachineDSP::latencySamples() const noexcept
{
    // Bypass is a true zero-delay passthrough (processBlock copies input->output with no
    // oversampling round trip), so it must report ZERO latency. Reporting the active-path
    // latency while bypassed would make the host's PDC shift the (undelayed) bypassed track
    // by ~32+ samples relative to the rest of the mix. The DPF shell re-queries this on every
    // block and only calls setLatency() when the value actually changes, so the host re-runs
    // PDC exactly once on each bypass toggle. CLAUDE.md: latency cleared on bypass, restored
    // on un-bypass.
    if (pBypass.load (std::memory_order_relaxed))
        return 0;

    // osL::latency() = global up/down FIR round trip (base-rate samples). Add the two
    // LocalAAStage nonlinearity oversamplers (soft-limit + shaper) that run INSIDE the
    // core at the OS rate: each contributes LocalAAStage::latency() surrounding-rate
    // samples, so 2*that / factor in base-rate samples. Both always run (shaper no longer
    // gated), so the reported latency is constant — no drive-dependent PDC drift.
    const float localAaBase = 2.0f * duskaudio::LocalAAStage::latency()
                            / static_cast<float> (currentFactor > 0 ? currentFactor : 1);
    return static_cast<int> (std::lround (osL.latency() + localAaBase));
}

//==============================================================================
void TapeMachineDSP::applyFactor (int newFactor)
{
    currentFactor = newFactor;
    currentOsRate = baseSampleRate * static_cast<double> (newFactor);

    osL.setFactor (newFactor); osR.setFactor (newFactor);
    osL.reset();               osR.reset();

    // No reallocation: wow/flutter buffers are pre-sized for the max factor.
    coreL.prepare (currentOsRate, newFactor, maxOsRate);
    coreR.prepare (currentOsRate, newFactor, maxOsRate);
    sharedWowFlutter.prepare (currentOsRate, newFactor, maxOsRate);

    const float hpFreq = pHighpassHz.load (std::memory_order_relaxed);
    const float lpFreq = pLowpassHz.load (std::memory_order_relaxed);
    const float lpQ    = pLpQ.load (std::memory_order_relaxed);
    hpL.prepare (currentOsRate, hpFreq, 0.707f); hpL.setType (DuskSVF::Type::highpass);
    hpR.prepare (currentOsRate, hpFreq, 0.707f); hpR.setType (DuskSVF::Type::highpass);
    lpL.prepare (currentOsRate, lpFreq, lpQ);   lpL.setType (DuskSVF::Type::lowpass);
    lpR.prepare (currentOsRate, lpFreq, lpQ);   lpR.setType (DuskSVF::Type::lowpass);

    outGain.prepare (currentOsRate, kGainTau);   // re-config OS-rate ramp coeff

    lastHpFreq = -1.0f;   // force SVF cutoff refresh below
    lastLpFreq = -1.0f;
    lastLpQ    = -1.0f;
    lastFactor = newFactor;
}

//==============================================================================
void TapeMachineDSP::processBlock (const float* const* inputs, float* const* outputs,
                                   int nCh, int nSamples) noexcept
{
    ScopedFlushDenormals denormalGuard;

    if (nSamples <= 0) return;
    nCh = clampI (nCh, 1, 2);

    // --- oversampling factor change (RT-safe reconfigure, no allocation) ------
    const int reqFactor = factorFromChoice (pOversampling.load (std::memory_order_relaxed));
    if (reqFactor != currentFactor)
        applyFactor (reqFactor);

    // --- bypass: pure passthrough (matches JUCE early return, no meter update)-
    if (pBypass.load (std::memory_order_relaxed))
    {
        for (int ch = 0; ch < nCh; ++ch)
            if (inputs[ch] != outputs[ch])
                for (int n = 0; n < nSamples; ++n) outputs[ch][n] = inputs[ch][n];

        // Keep the output PEAK lamp honest even while bypassed: capture any real
        // output clip and let a stale peak decay instead of latching the UI lamp.
        float pkOL = outPeakStateL, pkOR = outPeakStateR;
        for (int n = 0; n < nSamples; ++n)
        {
            const float aL = std::abs (outputs[0][n]);
            pkOL = aL > pkOL ? aL : pkOL * peakDecayCoeff;
            if (nCh >= 2) { const float aR = std::abs (outputs[1][n]); pkOR = aR > pkOR ? aR : pkOR * peakDecayCoeff; }
        }
        outPeakStateL = pkOL; outPeakStateR = (nCh >= 2) ? pkOR : pkOL;
        outPeakL.store (outPeakStateL, std::memory_order_relaxed);
        outPeakR.store (outPeakStateR, std::memory_order_relaxed);
        return;
    }

    // Input VU is metered POST-input-trim / PRE-saturation, inside the processing
    // loops below (see item B): it reflects record/tape-drive level, not the raw
    // incoming signal. Same 0 VU reference + ANSI mean-abs ballistics as the output VU.

    const auto signalPath = static_cast<TapeCore::SignalPath> (clampI (pSignalPath.load (std::memory_order_relaxed), 0, 3));

    // --- Thru: passthrough + VU (input == output) ----------------------------
    if (signalPath == TapeCore::Thru)
    {
        for (int ch = 0; ch < nCh; ++ch)
            if (inputs[ch] != outputs[ch])
                for (int n = 0; n < nSamples; ++n) outputs[ch][n] = inputs[ch][n];

        float sL = vuStateL, sR = vuStateR;
        float pL = inPeakStateL, pR = inPeakStateR;
        float pkOL = outPeakStateL, pkOR = outPeakStateR;   // Thru: output == input
        for (int n = 0; n < nSamples; ++n)
        {
            const float aL = std::abs (inputs[0][n]);
            sL += (aL - sL) * vuBallisticAlpha;
            pL = aL > pL ? aL : pL * peakDecayCoeff;
            pkOL = aL > pkOL ? aL : pkOL * peakDecayCoeff;
            if (nCh >= 2) { const float aR = std::abs (inputs[1][n]); sR += (aR - sR) * vuBallisticAlpha; pR = aR > pR ? aR : pR * peakDecayCoeff; pkOR = aR > pkOR ? aR : pkOR * peakDecayCoeff; }
        }
        vuStateL = sL; vuStateR = (nCh >= 2) ? sR : sL;
        inPeakStateL = pL; inPeakStateR = (nCh >= 2) ? pR : pL;
        inPeakL.store (inPeakStateL, std::memory_order_relaxed);
        inPeakR.store (inPeakStateR, std::memory_order_relaxed);
        outPeakStateL = pkOL; outPeakStateR = (nCh >= 2) ? pkOR : pkOL;
        outPeakL.store (outPeakStateL, std::memory_order_relaxed);
        outPeakR.store (outPeakStateR, std::memory_order_relaxed);
        vuL.store (vuStateL, std::memory_order_relaxed);
        vuR.store (vuStateR, std::memory_order_relaxed);
        inVuStateL = vuStateL; inVuStateR = vuStateR;   // Thru: input == output
        inVuL.store (inVuStateL, std::memory_order_relaxed);
        inVuR.store (inVuStateR, std::memory_order_relaxed);
        return;
    }

    // --- tone SVF coefficient refresh (dirty) --------------------------------
    const float hpFreq = pHighpassHz.load (std::memory_order_relaxed);
    const float lpFreq = pLowpassHz.load (std::memory_order_relaxed);
    const float lpQ    = pLpQ.load (std::memory_order_relaxed);   // preset LP resonance (HP stays fixed 0.707)
    bypassLowpass = (lpFreq >= 19000.0f);
    if (std::abs (hpFreq - lastHpFreq) > 0.01f || std::abs (lpFreq - lastLpFreq) > 0.01f
        || std::abs (lpQ - lastLpQ) > 0.0001f)
    {
        hpL.setType (DuskSVF::Type::highpass); hpL.setCutoff (hpFreq); hpL.setResonance (0.707f);
        hpR.setType (DuskSVF::Type::highpass); hpR.setCutoff (hpFreq); hpR.setResonance (0.707f);
        lpL.setType (DuskSVF::Type::lowpass);  lpL.setCutoff (lpFreq); lpL.setResonance (lpQ);
        lpR.setType (DuskSVF::Type::lowpass);  lpR.setCutoff (lpFreq); lpR.setResonance (lpQ);
        lastHpFreq = hpFreq;
        lastLpFreq = lpFreq;
        lastLpQ    = lpQ;
    }

    // --- block-constant parameter reads --------------------------------------
    const auto machine = static_cast<TapeCore::TapeMachine> (clampI (pMachine.load (std::memory_order_relaxed), 0, 1));
    int speedIdx = clampI (pSpeed.load (std::memory_order_relaxed), 0, 3);
    // 3.75 IPS is an American-only speed; the Swiss model has no 3.75. Coerce it
    // to 15 IPS on the Swiss so preset/automation recall of Swiss+3.75 stays valid
    // (the UI hard-hides 3.75 from the Swiss's dropdown).
    if (machine == TapeCore::Swiss && speedIdx == TapeCore::Speed_3_75_IPS)
        speedIdx = TapeCore::Speed_15_IPS;
    const auto speed   = static_cast<TapeCore::TapeSpeed> (speedIdx);
    const auto type    = static_cast<TapeCore::TapeType>    (clampI (pType.load (std::memory_order_relaxed), 0, 3));
    // AES removed (neither reference deck has it); a stale AES(2) from an old preset -> NAB.
    int eqIdx = pEqStandard.load (std::memory_order_relaxed);
    if (eqIdx < 0 || eqIdx > 1) eqIdx = 0;
    const auto eq      = static_cast<TapeCore::EQStandard>  (eqIdx);

    // American front-panel toggles (American only; ignored on the Swiss so its whole
    // path stays byte-identical regardless of the stored values). All default On = current.
    const bool isClassic     = (machine == TapeCore::American);
    const bool crosstalkOn   = pCrosstalk.load (std::memory_order_relaxed);
    const bool wfEnabled     = pWowFlutterOn.load (std::memory_order_relaxed);
    const bool transformerOn = pTransformer.load (std::memory_order_relaxed);

    const float inputGainDb = pInputGainDb.load (std::memory_order_relaxed);
    const float targetInputGain = dbToGain (inputGainDb);

    float targetOutputGain;
    if (pAutoComp.load (std::memory_order_relaxed))
    {
        // Gain link ("LINK"): output = -input + Output-knob trim. The inverse of the
        // input drive locks the two knobs (drive the tape harder without the level
        // rising); unity/cal-neutrality are handled inside the tape core
        // (m_machineMakeupGain + cal restore). The Output knob remains a small ADDITIVE
        // makeup trim on top of the inverse (default 0 -> pure inverse -> byte-identical
        // unity). Factory presets use it to carry the reference preset's own non-unity output
        // level (a post-tape LINEAR gain: shifts loudness but not THD/FR/aliasing).
        targetOutputGain = dbToGain (-inputGainDb + pOutputGainDb.load (std::memory_order_relaxed));
    }
    else
    {
        targetOutputGain = dbToGain (pOutputGainDb.load (std::memory_order_relaxed));
    }

    inGain.setTarget (targetInputGain);
    outGain.setTarget (targetOutputGain);

    const float saturationAmount = std::clamp (((inputGainDb + 12.0f) / 24.0f) * 100.0f, 0.0f, 100.0f);
    smSat.setTarget     (saturationAmount);
    // American Wow & Flutter master enable: when Off, zero the W&F depth (the
    // knobs are our superset control; the discrete reference toggle simply gates them). On (default)
    // or the Swiss => the knob values pass through unchanged (byte-identical).
    float wowPct = pWow.load (std::memory_order_relaxed);
    float flutPct = pFlutter.load (std::memory_order_relaxed);
    if (isClassic && ! wfEnabled) { wowPct = 0.0f; flutPct = 0.0f; }
    smWow.setTarget     (wowPct);
    smFlutter.setTarget (flutPct);
    smNoise.setTarget   (pNoiseAmount.load (std::memory_order_relaxed) * 0.01f);

    // JUCE derives the noise gate from the amount knob (>0.05 %); the boolean
    // noiseEnabled param is dead in the source, so we mirror that (PORT_NOTES).
    const bool noiseEnabled = pNoiseAmount.load (std::memory_order_relaxed) > 0.05f;

    const float calibrationDb = calibrationDbFromIndex (pCalibration.load (std::memory_order_relaxed));
    // Head Width (American only; the DSP ignores it on the Swiss model). 1 = 1/2" reference.
    const int headWidth = clampI (pHeadWidth.load (std::memory_order_relaxed), 0, 2);

    // Bias: auto-cal (from type/speed) or manual — block target, matches JUCE. Auto-cal uses
    // the same shared helper as the prepare() snap so both agree on the operating point.
    float biasAmount;
    if (pAutoCal.load (std::memory_order_relaxed))
        biasAmount = autoCalBiasFromTypeSpeed (type, speed);
    else
        biasAmount = pBias.load (std::memory_order_relaxed) * 0.01f;
    // Smooth the bias: the per-sample smBias.next() ramp (written into biasArr in the OS loop
    // below) feeds processSample, so a bias-knob step ramps the tape core over ~kSatTau instead
    // of stepping per block; smBias.value() is the block-start value used for the HF-restore
    // shelf. Steady state is identical (the ramp converges to biasAmount).
    smBias.setTarget (biasAmount);
    const float smBiasAmount = smBias.value();

    // --- signal-level envelope (shared max(L,R), PRE input gain) ----------------
    // Peak detector (instant attack, ~30 ms release) over the block's RAW input |x| — BEFORE
    // the input-gain multiply. The gain is NOT scaled onto |x| here; it is folded back in
    // analytically as smInGainDb (the same smoothed term knobFluxDb uses) so signal- and knob-
    // flux carry an identical gain term and cancel EXACTLY at the anchor, even mid-automation.
    // (The old detector scaled |x| by the raw per-block targetInputGain while knobFluxDb used
    // the smoothed gain, so a gain move broke the anchor and chirped the HF.) SHARED across
    // L/R (max of |L|,|R|): a per-channel detector would tilt the stereo image whenever one
    // channel is louder (mid/side spectral shift); the reference record path is mono-linked, so a
    // shared level for the tonal EQ keeps L/R spectrally matched. Representative = the peak the
    // detector reaches this block (persistent state carries the release tail across blocks),
    // so a drum hit shifts the tone ON the hit, not a block later.
    float envDbPre;
    {
        float e = m_levelEnv, blockPk = m_levelEnv;
        for (int n = 0; n < nSamples; ++n)
        {
            float a = std::abs (inputs[0][n]);
            if (nCh >= 2) { const float aR = std::abs (inputs[1][n]); if (aR > a) a = aR; }
            if (a > e) e = a; else e *= m_levelRelCoeff;
            if (e > blockPk) blockPk = e;
        }
        m_levelEnv = e;
        envDbPre = 20.0f * std::log10 (std::max (blockPk, 1e-9f));   // PRE-gain level in dBFS
    }

    // --- program-band envelope (Phase C: SAME pre-gain node, 500 Hz-LP'd per channel) --------
    // Each raw sample is 2-pole-LP'd at 500 Hz BEFORE rectification so the follower tracks
    // sub-500 Hz PROGRAM energy, not a lone HF/mid tone (filtering the already-rectified peak
    // would leak the tone's DC term and defeat the anchor). Keyed by the identical anchor law
    // below (progFluxDb), so a lone 1 kHz THD/alias tone reads below the -12 anchor and the
    // prog trims stay bypassed; sustained low-corner program pushes progFactor positive.
    float progEnvDbPre;
    {
        float pe = m_progEnv, progBlockPk = m_progEnv;
        for (int n = 0; n < nSamples; ++n)
        {
            float a = std::abs (static_cast<float> (m_progLpL.process (static_cast<double> (inputs[0][n]))));
            if (nCh >= 2)
            {
                const float aR = std::abs (static_cast<float> (m_progLpR.process (static_cast<double> (inputs[1][n]))));
                if (aR > a) a = aR;
            }
            if (a > pe) pe = a; else pe *= m_levelRelCoeff;
            if (pe > progBlockPk) progBlockPk = pe;
        }
        m_progEnv = pe;
        progEnvDbPre = 20.0f * std::log10 (std::max (progBlockPk, 1e-9f));   // PRE-gain program-band level
    }

    // Drive-linked HF restore + signal-level FR compensation. The memoryless tape core's
    // FR drifts with RECORD FLUX (signal level + input-gain + cal + bias): above the
    // -12 dBFS reference operating level the HF droops (waveshaper HF compression) and the
    // deep lows thicken (rel-1k) MORE than the reference does; at/below -12 dBFS the deltas
    // vanish. `driveHfCompDb` (unchanged) is the KNOB-keyed static HF restore that keeps
    // every factory preset's FR valid at the -12 dBFS gate level. On top of that a
    // SIGNAL-level term (levelFactor) adds the level-dependent HF restore / LF cut the
    // static shelf can't, so the FR-vs-level surface matches the reference on real program.
    //
    // Track the SMOOTHED saturation (not the raw knob): the compression it cancels ramps
    // over ~kSatTau via smSat, so a shelf snapped from the instant inputGainDb would jump
    // ahead of the compression and chirp the HF on an input-gain step. Steady-state value
    // is identical (smSat converges to saturationAmount) so preset FR is unchanged.
    //
    // THREE gain time constants coexist by design, and this compensation keys off the SAME one
    // (smSat, ~150 ms) on both the detector side (envDbPre + smInGainDb) and the knob side
    // (knobFluxDb) so they cancel at the anchor. The audio path's actual gain ramps FASTER
    // (per-sample inGain.next(), ~20 ms via kGainTau) — a DELIBERATE difference: the shelf must
    // track the HF compression it cancels, and that compression follows the shaper's saturation
    // smoother (smSat), not the raw audio-level gain. Matching the 20 ms audio gain instead
    // would slide the shelf ahead of the compression during a gain move and chirp the HF.
    const float smInGainDb   = smSat.value() * 0.01f * 24.0f - 12.0f;   // undo saturationAmount mapping
    const float kCalFluxDb   = (machine == TapeCore::Swiss ? 0.58f : 1.30f) * (calibrationDb - 6.0f);
    // Under-bias flux: a low Bias knob (Auto Cal off) drives the shaper HOT (biasDrive>1),
    // which the memoryless curve compresses in HF — but the real decks BRIGHTEN when
    // under-biased. Fold the bias-drive dB into driveAboveRef so the HF-restore shelf tracks
    // it and flips the direction. Mirrors the core biasDrive (exp curve); 0 dB at bias 0.5 so
    // the reference/auto-cal path is unchanged. Over-bias (biasDrive<1) clamps to 0 below.
    const float biasDriveExp = (machine == TapeCore::Swiss)
                                   ? std::clamp (std::exp (4.0f * (0.5f - smBiasAmount)), 0.2f, 5.0f)
                                   : std::clamp (std::exp (6.5f * (0.5f - smBiasAmount)), 0.15f, 9.0f);
    const float biasFluxDb   = 20.0f * std::log10 (biasDriveExp);
    // Knob flux (the OLD static-restore key): inGain + cal + bias, no signal level. It is
    // ALSO the flux the signal reaches at exactly -12 dBFS input (see the anchor below).
    const float knobFluxDb    = smInGainDb + kCalFluxDb + biasFluxDb;
    const float driveAboveRef = std::max (0.0f, knobFluxDb);
    const float kDriveHfCoeff = (machine == TapeCore::Swiss) ? 0.040f : 0.020f;
    // Under-bias BRIGHTENING (measured on both reference decks: low bias => HF RISES +4..5 dB
    // @10k — less HF self-erasure): the quadratic drive term alone only cancels the
    // shaper's HF compression (nets ~flat at low bias), so a linear bias-flux term pushes
    // the response positive like the hardware. 0 at/above optimal bias => reference and
    // auto-cal paths unchanged.
    const float kBiasHf       = (machine == TapeCore::Swiss) ? 0.40f : 0.35f;
    const float driveHfCompDb = std::min (12.0f, kDriveHfCoeff * driveAboveRef * driveAboveRef
                                                 + kBiasHf * std::max (0.0f, biasFluxDb));

    // Signal-level term. ANCHOR / RECONCILIATION (keeps every preset FR fit valid by
    // construction): the reference spec operating level is -12 dBFS (= 0 VU). The record flux above
    // the -12 ref = the PRE-gain signal level (envDbPre) + the SMOOTHED input gain (smInGainDb)
    // + 12. Both signalFluxDb and knobFluxDb carry the SAME smInGainDb term, so the input gain
    // cancels by construction: a mid-automation gain move can NOT shift the anchor (the old
    // raw-gain detector could). At a -12 dBFS input sweep (preset_validate / joint4) envDbPre
    // == -12, so signalFluxDb == smInGainDb + kCal + bias == knobFluxDb and the increment below
    // is EXACTLY 0 -> the static-only compensation the fits were done against is reproduced
    // bit-for-bit. Above -12 dBFS the increment grows (and reaches "full" sooner for hot presets,
    // i.e. it slides along the flux axis with inGain, exactly as the private calibration analysis surface does).
    // ramp() = flux mapped 0..1 over the -12..0 dBFS window, gamma kRampPow so -6 dBFS lands at
    // ~40% of full (matches the measured surface).
    const float signalFluxDb = envDbPre + smInGainDb + 12.0f + kCalFluxDb + biasFluxDb;
    auto ramp = [] (float fluxAboveRef) noexcept
    {
        const float t = std::clamp (fluxAboveRef / 12.0f, 0.0f, 1.0f);
        return std::pow (t, kRampPow);
    };
    const float levelFactorTarget = std::max (0.0f, ramp (signalFluxDb) - ramp (knobFluxDb));
    // Program-band factor (Phase C): identical anchor construction as levelFactor, but keyed off
    // the 500 Hz-LP'd program envelope (progEnvDbPre). At/below the -12 anchor the increment is 0
    // (a lone 1 kHz THD tone lands there) so the prog trims stay bypassed; sustained sub-500 Hz
    // program pushes it positive and engages them. Reuses the SAME ramp/knobFluxDb, so the
    // input-gain/cal/bias terms cancel at the anchor exactly like the level-comp factor.
    const float progFluxDb = progEnvDbPre + smInGainDb + 12.0f + kCalFluxDb + biasFluxDb;
    const float progFactorTarget = std::max (0.0f, ramp (progFluxDb) - ramp (knobFluxDb));
    // Smooth the factor before it drives the shelf coeffs. A raw per-block factor jumps several
    // dB on a drum hit -> a biquad coeff discontinuity (zipper / click). One-pole with a FAST
    // (~4 ms) attack so the tone still shifts ON the hit (Phase A showed the transient window
    // dominates the tonal match — that responsiveness is the whole point) and a slower (~30 ms)
    // release tracking the detector's decay. Applied once per block: raise the per-sample coeff
    // to the block length so the effective time constant is block-size-independent, and at the
    // -12 dBFS gate the target is a steady 0 so the smoother sits at 0 -> presets untouched.
    {
        const float perSampleCoeff = (levelFactorTarget > m_levelFactorSm)
                                         ? m_levelFactAtkCoeff : m_levelFactRelCoeff;
        const float blockCoeff = 1.0f - std::pow (1.0f - perSampleCoeff, static_cast<float> (nSamples));
        m_levelFactorSm += blockCoeff * (levelFactorTarget - m_levelFactorSm);
    }
    const float levelFactor  = m_levelFactorSm;
    // Smooth the program-band factor with the SAME fast-attack/slow-release coeffs (block-size-
    // independent). At/below the -12 anchor the target is a steady 0, so it sits at 0 and the
    // prog crossfade stays exactly dry (byte-identical THD/sweep/alias).
    {
        const float perSampleCoeff = (progFactorTarget > m_progFactorSm)
                                         ? m_levelFactAtkCoeff : m_levelFactRelCoeff;
        const float blockCoeff = 1.0f - std::pow (1.0f - perSampleCoeff, static_cast<float> (nSamples));
        m_progFactorSm += blockCoeff * (progFactorTarget - m_progFactorSm);
    }
    const float progFactor = m_progFactorSm;
    const float kLevelHfGain = (machine == TapeCore::Swiss) ? kLevelHfSwiss   : kLevelHfAmerican;
    const float kLevelLfGain = (machine == TapeCore::Swiss) ? kLevelLfSwiss   : kLevelLfAmerican;
    const float levelHfDb    = std::min (9.0f, kLevelHfGain * levelFactor);   // HF restore (>=0)
    const float levelLfDb    = -kLevelLfGain * levelFactor;                   // LF cut (<=0)

    // Below-anchor decay of the knob-static driveHfComp (crest-sizzle fix). belowAnchorDb is
    // how far the SIGNAL flux sits below the -12 dBFS operating flux the static restore assumes.
    // It is signalFluxDb - knobFluxDb, which telescopes to envDbPre + 12: both flux terms carry
    // the SAME smInGainDb + kCalFluxDb + biasFluxDb, so the input-gain/cal/bias cancel and the
    // anchor is envDbPre = -12 dBFS EXACTLY, even mid-automation (identical construction to the
    // level-comp anchor above). NEUTRALITY PROOF: at/above the anchor belowAnchorDb >= 0 so
    // decayTarget == 1.0 and driveHfCompDb is unmultiplied -> every preset-FR fit (validated at
    // the -12 dBFS sweep) and the -6 dBFS THD step are reproduced bit-for-bit; and for every
    // reference / Classic / low-drive preset driveHfCompDb is ~0 (driveAboveRef == 0 + no under-bias),
    // so the factor multiplies zero and changes nothing regardless of level. The decay ONLY bites
    // BELOW -12 dBFS AND only on hot presets (Old Tape, Drum Bus, Thick Sat...) whose static
    // restore would otherwise stay fully bright while the reference's record brightness has faded. The
    // floor is NEGATIVE (factor -> 1 - kDriveDecayDepth ~= -0.75) so the shelf actively cuts HF at
    // low flux, matching the reference's measured low-level HF darkening (the shaper compression that
    // the restore cancels has itself vanished, so the residual is a real record-HF-loss deficit).
    const float belowAnchorDb  = signalFluxDb - knobFluxDb;   // = envDbPre + 12; <0 below the -12 anchor
    const float decayTarget    = (belowAnchorDb >= 0.0f)
                                     ? 1.0f
                                     : 1.0f - kDriveDecayDepth * (1.0f - std::exp (belowAnchorDb / kDriveDecayTau));
    // Smooth like the level factor (shared fast-attack/slow-release coeffs, block-size-independent):
    // a raw per-block jump on a drum hit would step the shelf coeff and click. Attack = factor
    // RISING toward 1.0 (brightness restored ON the transient); release = factor falling to the
    // floor as the tail decays. At a steady -12 sweep the target is a constant 1.0 so the smoother
    // sits at 1.0 -> byte-identical to the un-decayed path.
    {
        const float perSampleCoeff = (decayTarget > m_driveDecaySm)
                                         ? m_levelFactAtkCoeff : m_levelFactRelCoeff;
        const float blockCoeff = 1.0f - std::pow (1.0f - perSampleCoeff, static_cast<float> (nSamples));
        m_driveDecaySm += blockCoeff * (decayTarget - m_driveDecaySm);
    }
    const float driveHfCompFinal = driveHfCompDb * m_driveDecaySm;

    // driveHfShelf keeps the knob-static restore, now with the below-anchor decay applied
    // (neutral at/above the -12 anchor -> presets byte-identical there); the signal-level HF/LF
    // ride separate shelves so the preset FR fits are untouched.
    coreL.setDriveHfComp (driveHfCompFinal);
    coreR.setDriveHfComp (driveHfCompFinal);
    coreL.setLevelComp (machine, levelHfDb, levelLfDb);
    coreR.setLevelComp (machine, levelHfDb, levelLfDb);
    const float presetLevelHmfDb = pLevelHmfTrim.load (std::memory_order_relaxed);
    const float presetLevelHfDb  = pLevelHfTrim.load  (std::memory_order_relaxed);
    const float presetLevelHfFactor = std::pow (levelFactor, 0.25f);
    coreL.setPresetLevelEq (presetLevelHmfDb, presetLevelHfDb, levelFactor, presetLevelHfFactor);
    coreR.setPresetLevelEq (presetLevelHmfDb, presetLevelHfDb, levelFactor, presetLevelHfFactor);
    // Program-band above-anchor trims (Phase C wall-breaker; re-based on PROGRAM in the EAR-GREEN
    // pass): the SAME 6.3k/11k pair as the preset-level EQ, but crossfaded by the PROGRAM-BAND factor
    // so a lone 1 kHz THD tone stays exactly dry (progFactor 0). Mix = pow(progFactor, 0.25) (the
    // shipped expansion idiom), shared by both bands. Most factory presets now carry a nonzero HF
    // trim (cut on bright presets, positive boost on the dark ones); a 0/0 preset gets setProgEq
    // bypassed exactly (byte-identical). Same idiom drives the deep-sub bloom below.
    const float progHmfDb = pProgHmfTrim.load (std::memory_order_relaxed);
    const float progHfDb  = pProgHfTrim.load  (std::memory_order_relaxed);
    const float progMix   = std::pow (progFactor, 0.25f);
    coreL.setProgEq (progHmfDb, progHfDb, progMix, progMix);
    coreR.setProgEq (progHmfDb, progHfDb, progMix, progMix);
    // Program-level deep-sub bloom restore (EAR-GREEN): the reference decks thicken the deep sub
    // (25-50 Hz) on program above the -12 anchor (a tape LF enhancement mine lacked). Same progMix
    // crossfade => byte-null on the -12 sweep / 1 kHz THD tone. Per-preset gain (progLfTrim); 0 on
    // clean / 30-IPS presets => exact bypass.
    const float progLfDb = pProgLfTrim.load (std::memory_order_relaxed);
    coreL.setProgLf (progLfDb, progMix);
    coreR.setProgLf (progLfDb, progMix);

    // Advanced repro-head 4-band EQ (block-constant; 0 dB = neutral).
    const float rLf  = pReproLf.load  (std::memory_order_relaxed);
    const float rLmf = pReproLmf.load (std::memory_order_relaxed);
    const float rHmf = pReproHmf.load (std::memory_order_relaxed);
    const float rHf  = pReproHf.load  (std::memory_order_relaxed);
    const float rSub = pReproSubBell.load (std::memory_order_relaxed); // per-preset 31 Hz Q2.5 sub-bell (0 = exact bypass)
    coreL.setReproEq (rLf, rLmf, rHmf, rHf, rSub);
    coreR.setReproEq (rLf, rLmf, rHmf, rHf, rSub);

    // Classic Transformer switch (American only): Off bypasses the output transformer, which
    // EXTENDS the deep bass (measured Classic On->Off = +3.4/+1.0/+0.4 dB @30/60/100 Hz, flat
    // above ~200 Hz) and THINS the added 2nd harmonic (measured -8 dB). Modelled as an LF
    // low-shelf restore + an even-order scale (setTransformerOff). On (default) or the Swiss
    // 800 => both neutral (0 dB / scale 1) => byte-identical. IMD (the transformer's dynamic
    // LF-intermod, On 1.47% vs Off 0.20%) is NOT separately modelled — our memoryless shaper
    // under-produces it in both states (a documented residual; a faithful IMD match needs a
    // shaper re-fit, out of scope per the campaign rules).
    const bool transformerOff = isClassic && ! transformerOn;
    const float kTransformerLfDb = 4.0f;   // low-shelf restore gain when bypassed (tuned: OFF @30 Hz mine +6.4 vs reference +6.35)
    const float kTransformerEven = 0.08f;  // even (2nd) scale when bypassed (tuned: OFF 2f mine ~-64 vs reference -64.2)
    coreL.setTransformerOff (transformerOff ? kTransformerLfDb : 0.0f, transformerOff ? kTransformerEven : 1.0f);
    coreR.setTransformerOff (transformerOff ? kTransformerLfDb : 0.0f, transformerOff ? kTransformerEven : 1.0f);

    // Shared wow/flutter rates + per-speed DEPTH scale (block-constant, from speed).
    // The reference decks' W&F FM deviation at a fixed 1 kHz pitch scales strongly with tape
    // speed: measured American (W&F on, classic formulation/NAB/+6) FMdev = 0.117/0.065/0.036/0.026 Hz at
    // 3.75/7.5/15/30 IPS — slower tape means a given capstan wobble is a larger fraction of
    // the transport speed, so a larger pitch swing. wfDepthScale is that curve normalised to
    // 15 IPS = 1.0. The Swiss has NO W&F param (measured flat ~0.014 Hz = the demod floor),
    // so this universal tape-physics curve is applied to BOTH machines. kWowDepth/kFlutterDepth
    // (TapeMachineDSP.hpp) were re-anchored (÷3.25) so the Sunbaked preset (3.75 IPS, the depth
    // calibration anchor) still matches the reference (FMdev ~0.20 Hz) after the 3.25x scale here.
    float wowRate = 0.5f, flutterRate = 5.0f, wfDepthScale = 1.0f;
    switch (speed)
    {
        case TapeCore::Speed_7_5_IPS:  wowRate = 0.33f; flutterRate = 3.5f; wfDepthScale = 1.76f; break;
        case TapeCore::Speed_15_IPS:   wowRate = 0.5f;  flutterRate = 5.0f; wfDepthScale = 1.00f; break;
        case TapeCore::Speed_30_IPS:   wowRate = 0.8f;  flutterRate = 7.0f; wfDepthScale = 0.68f; break;
        case TapeCore::Speed_3_75_IPS: wowRate = 0.22f; flutterRate = 2.5f; wfDepthScale = 3.25f; break;
    }

    // --- precompute per-sample shared values ---------------------------------
    const int factor = currentFactor;
    const int osN = nSamples * factor;

    for (int n = 0; n < nSamples; ++n)
        inGainArr[static_cast<size_t> (n)] = inGain.next();

    for (int i = 0; i < osN; ++i)
    {
        const float sat = smSat.next();
        const float bias = smBias.next();
        const float wv  = smWow.next();
        const float fv  = smFlutter.next();
        const float nv  = smNoise.next();
        const float combined = wv + fv;
        float sm = 0.0f;
        if (combined > 0.0f)
            sm = sharedWowFlutter.calculateModulation (wv * 0.01f * wfDepthScale, fv * 0.01f * wfDepthScale, wowRate, flutterRate, currentOsRate);

        const size_t si = static_cast<size_t> (i);
        satArr[si]       = sat * 0.01f;
        biasArr[si]      = bias;
        wowFlutArr[si]   = combined * 0.01f;
        noiseArr[si]     = nv * 100.0f;
        sharedModArr[si] = sm;
        outGainArr[si]   = outGain.next();
    }

    // --- per-channel oversampled processing ----------------------------------
    // Functor chain (at the oversampled rate): HP SVF -> tape core -> LP SVF ->
    // output gain. Input gain is applied at base rate before upsampling.
    float inSL = inVuStateL, inSR = inVuStateR;   // input VU: mean-abs of the post-trim record level
    // Input sample peak hold (instant attack, ~300 ms release) at the SAME record node the
    // input VU meters — post-input-gain, PRE-tape. Feeds only the UI PEAK lamp (a record-
    // level over indicator), kept separate from the mean-abs VU integrator.
    float pkL = inPeakStateL, pkR = inPeakStateR;
    {
        int osIdx = 0;
        for (int n = 0; n < nSamples; ++n)
        {
            const float x = inputs[0][n] * inGainArr[static_cast<size_t> (n)];
            const float axL = std::abs (x); inSL += (axL - inSL) * vuBallisticAlpha;
            pkL = axL > pkL ? axL : pkL * peakDecayCoeff;   // input sample peak for the PEAK lamp
            outputs[0][n] = osL.processSample (x, [&] (float s) noexcept
            {
                const size_t si = static_cast<size_t> (osIdx);
                s = hpL.process (s);
                s = coreL.processSample (s, machine, speed, type, biasArr[si],
                                         satArr[si], wowFlutArr[si], noiseEnabled, noiseArr[si],
                                         &sharedModArr[si], calibrationDb, eq, signalPath, headWidth);
                if (! bypassLowpass) s = lpL.process (s);
                s *= outGainArr[si];
                ++osIdx;
                return s;
            });
        }
    }

    if (nCh >= 2)
    {
        int osIdx = 0;
        for (int n = 0; n < nSamples; ++n)
        {
            const float x = inputs[1][n] * inGainArr[static_cast<size_t> (n)];
            const float axR = std::abs (x); inSR += (axR - inSR) * vuBallisticAlpha;
            pkR = axR > pkR ? axR : pkR * peakDecayCoeff;   // input sample peak for the PEAK lamp
            outputs[1][n] = osR.processSample (x, [&] (float s) noexcept
            {
                const size_t si = static_cast<size_t> (osIdx);
                s = hpR.process (s);
                s = coreR.processSample (s, machine, speed, type, biasArr[si],
                                         satArr[si], wowFlutArr[si], noiseEnabled, noiseArr[si],
                                         &sharedModArr[si], calibrationDb, eq, signalPath, headWidth);
                if (! bypassLowpass) s = lpR.process (s);
                s *= outGainArr[si];
                ++osIdx;
                return s;
            });
        }
    }

    // input VU + input true-peak store (post-trim / pre-sat record level)
    inVuStateL = inSL; inVuStateR = (nCh >= 2) ? inSR : inSL;
    inVuL.store (inVuStateL, std::memory_order_relaxed);
    inVuR.store (inVuStateR, std::memory_order_relaxed);
    inPeakStateL = pkL; inPeakStateR = (nCh >= 2) ? pkR : pkL;
    inPeakL.store (inPeakStateL, std::memory_order_relaxed);
    inPeakR.store (inPeakStateR, std::memory_order_relaxed);

    // --- crosstalk (base rate — deviation from JUCE's OS-rate placement) ------
    if (nCh >= 2)
    {
        // Swiss: the reference stereo instance models NO L/R adjacent-track bleed (measured:
        // an L-only tone leaves the R channel at digital silence), so Swiss crosstalk is 0.
        // American: the reference "Crosstalk On" bleed measures -51..-55 dB L->R across presets;
        // 0.00224 (~-53 dB) is the single-scalar midpoint that lands every On preset within tol.
        // Crosstalk switch (American only): Off removes the bleed (the reference's On/Off per
        // preset is honoured by the preset table); On => the modelled bleed.
        float crosstalkAmount = (machine == TapeCore::Swiss) ? 0.0f : 0.00224f;
        if (isClassic && ! crosstalkOn) crosstalkAmount = 0.0f;
        for (int n = 0; n < nSamples; ++n)
        {
            const float tempL = outputs[0][n];
            const float tempR = outputs[1][n];
            outputs[0][n] += tempR * crosstalkAmount;
            outputs[1][n] += tempL * crosstalkAmount;
        }
    }

    // --- VU meter (output; ANSI mean-abs one-pole, ~300 ms to 99%) + output true-peak ----
    // The PEAK lamp is a genuine digital-clip (output over 0 dBFS) indicator: it reads the
    // final-output sample-peak taken HERE (post-tape / post-output-gain / post-crosstalk — the
    // exact buffer the host receives), instant attack with the ~300 ms peakDecayCoeff release.
    // Tape soft-saturates, so moderate record drive never trips it — only a true output over does.
    float sL = vuStateL, sR = vuStateR;
    float pkOL = outPeakStateL, pkOR = outPeakStateR;
    for (int n = 0; n < nSamples; ++n)
    {
        const float aL = std::abs (outputs[0][n]);
        sL += (aL - sL) * vuBallisticAlpha;
        pkOL = aL > pkOL ? aL : pkOL * peakDecayCoeff;
        if (nCh >= 2)
        {
            const float aR = std::abs (outputs[1][n]);
            sR += (aR - sR) * vuBallisticAlpha;
            pkOR = aR > pkOR ? aR : pkOR * peakDecayCoeff;
        }
    }
    vuStateL = sL;
    vuStateR = (nCh >= 2) ? sR : sL;
    vuL.store (vuStateL, std::memory_order_relaxed);
    vuR.store (vuStateR, std::memory_order_relaxed);
    outPeakStateL = pkOL; outPeakStateR = (nCh >= 2) ? pkOR : pkOL;
    outPeakL.store (outPeakStateL, std::memory_order_relaxed);
    outPeakR.store (outPeakStateR, std::memory_order_relaxed);
}

} // namespace duskaudio
