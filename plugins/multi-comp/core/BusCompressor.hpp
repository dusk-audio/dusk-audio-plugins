// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// BusCompressor.hpp — no-framework port of UniversalCompressor::BusCompressor
// (multicomp.cpp L2529–L2957). Every constant, coefficient and per-sample op
// order preserved from the original. Substitutions recorded in core/PORT_NOTES.md.

#pragma once

#include "UniversalCompressorServices.hpp"
#include "../HardwareEmulation/TransformerEmulation.h"
#include "../HardwareEmulation/ConvolutionEngine.h"

namespace duskaudio
{

class BusCompressor
{
public:
    // NOTE: prepare() takes a blockSize (default 512) — the source sizes an
    // internal RMS/scratch on it. The top level passes the oversampled block size.
    void prepare (double sampleRate, int numChannels, int blockSize = 512);
    void updateSampleRate (double newSampleRate);

    // process(): RT / per-sample. Used for the unlinked / mono path.
    //   threshold    : bus_threshold (dB)
    //   ratio        : resolved ratio value (2.0 / 4.0 / 10.0) from bus_ratio choice
    //   attackIndex  : bus_attack  choice index (0..5)
    //   releaseIndex : bus_release choice index (0..4)
    //   makeupGain   : bus_makeup (dB, 0 == unity; caller forces 0 under auto-makeup)
    //   mixAmount    : bus_mix (0..1) — the mode's own parallel mix
    float process (float input, int channel, float threshold, float ratio,
                   int attackIndex, int releaseIndex, float makeupGain, float mixAmount = 1.0f,
                   bool oversample = false, float sidechainSignal = 0.0f, bool useExternalSidechain = false);

    // processStereoLinked(): RT / per-block. True stereo-link path — one shared
    // sidechain drives both VCAs. postGain is the top level's compensationGain
    // (1.0 on the non-oversampled strip path). extScL/extScR carry the pre-built
    // linked sidechain (read only when useExternalSidechain is true).
    void processStereoLinked (float* dataL, float* dataR, int numSamples,
                              float threshold, float ratio,
                              int attackIndex, int releaseIndex,
                              float makeupGain, float postGain, float stereoLinkAmount,
                              const float* extScL, const float* extScR,
                              bool useExternalSidechain);

    float getGainReduction (int channel) const;

    void reset();

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rms = 0.0f;
        float previousLevel = 0.0f; // For auto-release tracking
        float hpState = 0.0f;       // Simple highpass filter state (1st pole)
        float prevInput = 0.0f;     // Previous input for filter (1st pole)
        float hpState2 = 0.0f;      // Second pole state (12dB/oct total)
        float prevInput2 = 0.0f;    // Previous input for second pole
        float prevCompressed = 0.0f; // Feedback topology: previous compressed output
    };

    // Feedback sidechain: 60Hz 2-pole HPF on the previous compressed output, rectified.
    float busHpRectify (Detector& d)
    {
        const float fb = d.prevCompressed;
        const float sr = static_cast<float> (sampleRate);
        const float alpha = 1.0f / (1.0f + 6.2831853f * 60.0f / sr);
        d.hpState   = alpha * (d.hpState + fb - d.prevInput);
        d.prevInput = fb;
        const float firstPole = d.hpState;
        d.hpState2   = alpha * (d.hpState2 + firstPole - d.prevInput2);
        d.prevInput2 = firstPole;
        return std::abs (d.hpState2);
    }

    // RMS averaging of the rectified sidechain — the bus detector averages
    // (RMS), it is not a peak follower; this is what gives the smooth "glue".
    float busRms (Detector& d, float rectified)
    {
        const float sr = static_cast<float> (sampleRate);
        const float rmsCoeff = std::exp (-1.0f / jmax (1.0f, Constants::BUS_RMS_TIME * sr));
        d.rms = rmsCoeff * d.rms + (1.0f - rmsCoeff) * rectified * rectified;
        return std::sqrt (jmax (0.0f, d.rms));
    }

    // Over-easy (soft-knee) gain computer over BUS_OVEREASY_KNEE_WIDTH dB — the
    // bus knee, replacing the old hard knee.
    float busReduction (float detectionLevel, float thresholdLin, float ratio)
    {
        const float overThreshDb = gainToDb (
            jmax (detectionLevel, 1.0e-9f) / thresholdLin);
        const float reductionRatio = 1.0f - 1.0f / ratio;
        const float knee = Constants::BUS_OVEREASY_KNEE_WIDTH;
        float reduction;
        if (overThreshDb <= -knee * 0.5f)
            reduction = 0.0f;
        else if (overThreshDb >= knee * 0.5f)
            reduction = overThreshDb * reductionRatio;
        else
        {
            const float x = overThreshDb + knee * 0.5f;   // 0..knee inside the knee
            reduction = reductionRatio * (x * x) / (2.0f * knee);
        }
        return jmin (reduction, Constants::BUS_MAX_REDUCTION_DB);
    }

    void calibrateHardwareGain();

    std::vector<Detector> detectors;
    double sampleRate = 0.0;  // Set by prepare() from DAW

    // Hardware emulation components (VCA bus compressor style)
    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::StereoConvolution convolution;
    float hardwareGainCompensation = 1.0f;
};

//==============================================================================
inline void BusCompressor::prepare (double sampleRate, int numChannels, int blockSize)
{
    if (sampleRate <= 0.0 || numChannels <= 0 || blockSize <= 0)
        return;

    this->sampleRate = sampleRate;
    detectors.clear();
    detectors.resize ((size_t) numChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& detector = detectors[(size_t) ch];
        detector.envelope = 1.0f;
        detector.rms = 0.0f;
        detector.previousLevel = 0.0f;
        detector.hpState = 0.0f;
        detector.prevInput = 0.0f;
        detector.hpState2 = 0.0f;
        detector.prevInput2 = 0.0f;
        detector.prevCompressed = 0.0f;
    }

    // Hardware emulation components (VCA bus compressor style)
    // Input transformer (console-style)
    inputTransformer.prepare (sampleRate, numChannels);
    inputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getConsoleBus().inputTransformer);
    inputTransformer.setEnabled (true);

    // Output transformer
    outputTransformer.prepare (sampleRate, numChannels);
    outputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getConsoleBus().outputTransformer);
    outputTransformer.setEnabled (true);

    // Short convolution for console coloration
    convolution.prepare (sampleRate);
    convolution.loadTransformerIR (HardwareEmulation::ShortConvolution::TransformerType::Console_Bus);

    // Calibrate hardware gain compensation for the full analog chain
    calibrateHardwareGain();
}

// Control-thread only: recalibrates the emulation chain (thousands of samples
// of warmup + measurement sine through the transformers) — far too heavy for
// the audio callback. The top level calls it from prepare() only.
inline void BusCompressor::updateSampleRate (double newSampleRate)
{
    if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
        return;
    sampleRate = newSampleRate;

    // Update transformer and convolution filter coefficients
    inputTransformer.prepare (sampleRate, static_cast<int> (detectors.size()));
    inputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getConsoleBus().inputTransformer);
    outputTransformer.prepare (sampleRate, static_cast<int> (detectors.size()));
    outputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getConsoleBus().outputTransformer);
    convolution.prepare (sampleRate);
    convolution.loadTransformerIR (HardwareEmulation::ShortConvolution::TransformerType::Console_Bus);
    calibrateHardwareGain();
}

inline float BusCompressor::process (float input, int channel, float threshold, float ratio,
                                     int attackIndex, int releaseIndex, float makeupGain, float mixAmount,
                                     bool oversample, float sidechainSignal, bool useExternalSidechain)
{
    if (channel >= static_cast<int> (detectors.size()))
        return input;

    // Safety check for sample rate
    if (sampleRate <= 0.0)
        return input;

    auto& detector = detectors[(size_t) channel];

    // Hardware emulation: Input transformer (console-style)
    // Adds subtle saturation and frequency-dependent coloration
    float transformedInput = inputTransformer.processSample (input, channel);

    // Bus Compressor quad VCA topology
    // Uses parallel detection path with feedback design (sidechain taps compressed output)

    // Sidechain detection: external or feedback (HP-filtered previous output),
    // RMS-averaged for the smooth/averaging response.
    const float rectified = useExternalSidechain ? std::abs (sidechainSignal)
                                                  : busHpRectify (detector);
    float detectionLevel = busRms (detector, rectified);

    // Bus Compressor specific ratios: 2:1, 4:1, 10:1
    // ratio parameter already contains the actual ratio value (2.0, 4.0, or 10.0)
    float actualRatio = ratio;

    float thresholdLin = dbToGain (threshold);

    // Over-easy (soft-knee) gain computer — the "glue" knee.
    float reduction = busReduction (detectionLevel, thresholdLin, actualRatio);

    // Bus Compressor attack and release times
    std::array<float, 6> attackTimes = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f}; // ms
    std::array<float, 5> releaseTimes = {100.0f, 300.0f, 600.0f, 1200.0f, -1.0f}; // ms, -1 = auto

    float attackTime = attackTimes[(size_t) jlimit (0, 5, attackIndex)] * 0.001f;
    float releaseTime = releaseTimes[(size_t) jlimit (0, 4, releaseIndex)] * 0.001f;

    // Bus Auto-release mode - program-dependent (150-450ms range)
    if (releaseTime < 0.0f)
    {
        // Hardware-accurate Bus auto-release: 150ms to 450ms based on program
        // Faster for transient-dense material, slower for sustained compression

        // Track signal dynamics
        float signalDelta = std::abs (detectionLevel - detector.previousLevel);
        detector.previousLevel = detector.previousLevel * 0.95f + detectionLevel * 0.05f;

        // Transient density: high delta = drums/percussion, low delta = sustained
        float transientDensity = jlimit (0.0f, 1.0f, signalDelta * 20.0f);

        // Compression amount factor: more compression = slower release
        float compressionFactor = jlimit (0.0f, 1.0f, reduction / 12.0f); // 0dB to 12dB

        // Bus auto-release formula (150ms to 450ms)
        // Transient material: faster release (150-250ms)
        // Sustained material with heavy compression: slower release (300-450ms)
        float minRelease = 0.15f;  // 150ms
        float maxRelease = 0.45f;  // 450ms

        // Calculate release time based on material and compression
        float sustainedFactor = (1.0f - transientDensity) * compressionFactor;
        releaseTime = minRelease + (sustainedFactor * (maxRelease - minRelease));
    }

    // Bus Compressor envelope following with smooth response
    float targetGain = dbToGain (-reduction);

    if (targetGain < detector.envelope)
    {
        // Attack phase — exponential envelope for authentic snap
        float attackCoeff = std::exp (-1.0f / jmax (1.0f, attackTime * static_cast<float> (sampleRate)));
        detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;
    }
    else
    {
        // Release phase — exponential envelope for smooth recovery
        float releaseCoeff = std::exp (-1.0f / jmax (1.0f, releaseTime * static_cast<float> (sampleRate)));
        detector.envelope = targetGain + (detector.envelope - targetGain) * releaseCoeff;
    }

    // NaN/Inf safety check
    if (std::isnan (detector.envelope) || std::isinf (detector.envelope))
        detector.envelope = 1.0f;

    // Apply the gain reduction envelope to the input signal
    float compressed = transformedInput * detector.envelope;
    detector.prevCompressed = compressed;  // Store for feedback sidechain

    // Bus Compressor Output Stage - Subtle console saturation
    // Spec from compressor_specs.json: < 0.3% THD
    // The VCA bus is a clean VCA design, but the console path adds character.
    // Console transformers add subtle 2nd harmonic warmth.
    //
    // Harmonic math: For input x = A*sin(wt)
    // - k2*x^2 produces 2nd harmonic at (k2*A^2/2) amplitude
    // - k3*x^3 produces 3rd harmonic at (3*k3*A^3)/4 amplitude
    // Target: ~0.15-0.2% THD at typical levels

    float processed = compressed;

    // Console saturation - 2nd harmonic dominant from transformers
    // k2 = 0.004 gives ~0.2% 2nd harmonic at moderate levels
    // k3 = 0.003 gives subtle 3rd harmonic for "glue"
    constexpr float k2 = 0.004f;   // 2nd harmonic coefficient (asymmetric warmth)
    constexpr float k3 = 0.003f;   // 3rd harmonic coefficient (symmetric glue)

    // Apply waveshaping: y = x + k2*x^2 + k3*x^3
    float x2 = processed * processed;
    float x3 = x2 * processed;
    processed = processed + k2 * x2 + k3 * x3;

    // Bus output transformer — console iron coloration
    processed = outputTransformer.processSample (processed, channel);

    // Console transformer frequency response (short convolution — 2.5kHz punch, HF extension)
    processed = convolution.processSample (processed, channel);

    // Hardware gain compensation (measured at prepare time)
    processed *= hardwareGainCompensation;

    // Apply makeup gain
    float output = processed * dbToGain (makeupGain);

    // Note: Mix/parallel compression is now handled globally at the end of processBlock
    // for consistency across all compressor modes (mixAmount parameter kept for API compatibility)
    (void) mixAmount;   // Suppress unused warning
    (void) oversample;

    // Final output limiting
    return jlimit (-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
}

// Stereo-linked bus processing — true stereo-link behaviour: the sidechain is
// shared across the L/R pair so an asymmetric stereo signal never pulls the
// image. The per-channel process() above runs in a channel-outer loop and so
// can't share feedback state; this processes the L/R pair in lockstep.
// Each channel's detection is blended toward the pair's max by
// stereoLinkAmount: 100% = both see the max → identical gain (full link),
// less = the channels partially diverge (matching the continuous link knob
// the other modes honour). Feedback topology is preserved.
inline void BusCompressor::processStereoLinked (float* dataL, float* dataR, int numSamples,
                                                float threshold, float ratio,
                                                int attackIndex, int releaseIndex,
                                                float makeupGain, float postGain, float stereoLinkAmount,
                                                const float* extScL, const float* extScR,
                                                bool useExternalSidechain)
{
    if (detectors.size() < 2 || sampleRate <= 0.0)
        return;

    auto& dL = detectors[(size_t) 0];
    auto& dR = detectors[(size_t) 1];
    const float sr = static_cast<float> (sampleRate);
    const float thresholdLin = dbToGain (threshold);
    const float linkAmt = jlimit (0.0f, 1.0f, stereoLinkAmount);
    const std::array<float, 6> attackTimes  = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
    const std::array<float, 5> releaseTimes = { 100.0f, 300.0f, 600.0f, 1200.0f, -1.0f };
    const float attackTime  = attackTimes[(size_t) jlimit (0, 5, attackIndex)] * 0.001f;
    const float baseRelease = releaseTimes[(size_t) jlimit (0, 4, releaseIndex)] * 0.001f;
    constexpr float k2 = 0.004f, k3 = 0.003f;

    // Output stage (console harmonics + transformer + IR + makeup), per channel.
    auto outStage = [this, k2, k3] (float compressed, int ch, float makeup)
    {
        float p = compressed;
        const float x2 = p * p, x3 = x2 * p;
        p = p + k2 * x2 + k3 * x3;
        p = outputTransformer.processSample (p, ch);
        p = convolution.processSample (p, ch);
        p *= hardwareGainCompensation;
        const float out = p * dbToGain (makeup);
        return jlimit (-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, out);
    };

    // Advance one channel's envelope from its (link-blended) detection and
    // return the gain. At link=100% both channels get the same detection and
    // converge to identical gain (true link); below they partially diverge.
    auto advance = [&] (Detector& d, float detUsed) -> float
    {
        const float reduction = busReduction (detUsed, thresholdLin, ratio);
        float releaseTime = baseRelease;
        if (releaseTime < 0.0f)   // Auto: program-dependent 150-450ms, per channel
        {
            const float signalDelta = std::abs (detUsed - d.previousLevel);
            d.previousLevel = d.previousLevel * 0.95f + detUsed * 0.05f;
            const float transientDensity  = jlimit (0.0f, 1.0f, signalDelta * 20.0f);
            const float compressionFactor = jlimit (0.0f, 1.0f, reduction / 12.0f);
            releaseTime = 0.15f + (1.0f - transientDensity) * compressionFactor * (0.45f - 0.15f);
        }
        const float targetGain = dbToGain (-reduction);
        float& env = d.envelope;
        if (targetGain < env)
            env = targetGain + (env - targetGain) * std::exp (-1.0f / jmax (1.0f, attackTime  * sr));
        else
            env = targetGain + (env - targetGain) * std::exp (-1.0f / jmax (1.0f, releaseTime * sr));
        if (std::isnan (env) || std::isinf (env))
            env = 1.0f;
        return env;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        const float tL = inputTransformer.processSample (dataL[i], 0);
        const float tR = inputTransformer.processSample (dataR[i], 1);

        // Per-channel RMS detection (external or HP-filtered feedback), each
        // blended toward the pair's max by the link amount.
        const float detL = busRms (dL, useExternalSidechain ? std::abs (extScL[i]) : busHpRectify (dL));
        const float detR = busRms (dR, useExternalSidechain ? std::abs (extScR[i]) : busHpRectify (dR));
        const float linked = jmax (detL, detR);
        const float useL = detL * (1.0f - linkAmt) + linked * linkAmt;
        const float useR = detR * (1.0f - linkAmt) + linked * linkAmt;

        const float envL = advance (dL, useL);
        const float envR = advance (dR, useR);

        const float cL = tL * envL;
        const float cR = tR * envR;
        dL.prevCompressed = cL;   // feedback taps
        dR.prevCompressed = cR;

        dataL[i] = outStage (cL, 0, makeupGain) * postGain;
        dataR[i] = outStage (cR, 1, makeupGain) * postGain;
    }
}

inline float BusCompressor::getGainReduction (int channel) const
{
    if (channel >= static_cast<int> (detectors.size()))
        return 0.0f;
    return gainToDb (detectors[(size_t) channel].envelope);
}

inline void BusCompressor::reset()
{
    for (auto& detector : detectors)
    {
        detector.envelope = 1.0f;
        detector.rms = 0.0f;
        detector.previousLevel = 0.0f;
        detector.hpState = 0.0f;
        detector.prevInput = 0.0f;
        detector.hpState2 = 0.0f;
        detector.prevInput2 = 0.0f;
        detector.prevCompressed = 0.0f;
    }
    inputTransformer.reset();
    outputTransformer.reset();
    convolution.reset();
}

inline void BusCompressor::calibrateHardwareGain()
{
    constexpr int calibrationSamples = 4800;
    constexpr float refAmplitude = 0.126f;
    constexpr float refFreq = 1000.0f;

    float sr = static_cast<float> (sampleRate);
    float angularStep = 2.0f * kPiF * refFreq / sr;

    inputTransformer.reset();
    outputTransformer.reset();

    // Calibrate using channel 0 only — both channels have identical IR
    int warmup = static_cast<int> (sr * 0.05f);
    for (int i = 0; i < warmup; ++i)
    {
        float x = refAmplitude * std::sin (angularStep * static_cast<float> (i));
        x = inputTransformer.processSample (x, 0);
        x = outputTransformer.processSample (x, 0);
        convolution.processSample (x, 0);
    }

    double inputRmsSquared = 0.0;
    double outputRmsSquared = 0.0;
    for (int i = 0; i < calibrationSamples; ++i)
    {
        float phase = angularStep * static_cast<float> (warmup + i);
        float input = refAmplitude * std::sin (phase);

        float x = inputTransformer.processSample (input, 0);
        x = outputTransformer.processSample (x, 0);
        x = convolution.processSample (x, 0);

        inputRmsSquared += static_cast<double> (input * input);
        outputRmsSquared += static_cast<double> (x * x);
    }

    inputRmsSquared /= calibrationSamples;
    outputRmsSquared /= calibrationSamples;

    if (outputRmsSquared > 1e-12 && inputRmsSquared > 1e-12)
    {
        float chainGain = static_cast<float> (std::sqrt (outputRmsSquared / inputRmsSquared));
        hardwareGainCompensation = 1.0f / chainGain;
    }
    else
    {
        hardwareGainCompensation = 1.0f;
    }

    inputTransformer.reset();
    outputTransformer.reset();
    convolution.reset();
}

} // namespace duskaudio
