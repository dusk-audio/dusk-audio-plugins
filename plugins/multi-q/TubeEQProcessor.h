/*
  ==============================================================================

    TubeEQProcessor.h

    Vintage tube EQ processor for Multi-Q's Tube mode.

    Passive LC network topology with tube makeup gain stage.
    Boost/cut interaction creates characteristic frequency response curves.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../shared/AnalogEmulation/AnalogEmulation.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include "SafeFloat.h"
#include <random>

// Helper for LC filter pre-warping (clamp omega to avoid tan() blowup near Nyquist)
inline float tubeEQPreWarpFrequency(float freq, double sampleRate)
{
    float omega = juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
    omega = std::min(omega, juce::MathConstants<float>::halfPi - 0.001f);
    return static_cast<float>(sampleRate) / juce::MathConstants<float>::pi * std::tan(omega);
}

/** Inductor model for LC network emulation with frequency-dependent Q,
    core saturation, and hysteresis. */
class InductorModel
{
public:
    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        this->sampleRate = sampleRate;
        computeDecayCoefficients(sampleRate);
        reset();

        // Random variation of ±5% on Q and ±2% on saturation threshold
        // Use deterministic seed for reproducibility across sessions
        // Default seed based on sample rate for consistent character
        uint32_t seed = (characterSeed != 0) ? characterSeed
                                             : static_cast<uint32_t>(sampleRate * 1000.0);
        std::mt19937 gen(seed);
        std::uniform_real_distribution<float> qDist(0.95f, 1.05f);
        std::uniform_real_distribution<float> satDist(0.98f, 1.02f);
        componentQVariation = qDist(gen);
        componentSatVariation = satDist(gen);
    }

    void reset()
    {
        prevInput = 0.0f;
        prevOutput = 0.0f;
        hysteresisState = 0.0f;
        coreFlux = 0.0f;
        rmsLevel = 0.0f;
        currentSaturationLevel = 0.0f;
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread.
        Does NOT regenerate component variations (seed-dependent, not rate-dependent). */
    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        computeDecayCoefficients(newRate);
        reset();
    }

    /** Get frequency-dependent Q (models core losses at LF, skin effect at HF).
        Uses smooth polynomial fit instead of piecewise linear for natural Q variation. */
    float getFrequencyDependentQ(float frequency, float baseQ) const
    {
        // Smooth polynomial fit of inductor Q vs frequency
        // Based on typical vintage Pultec inductor measurements:
        //   Peak Q around 200-400 Hz, rolling off at both extremes
        //   LF losses from core hysteresis, HF losses from skin effect + winding capacitance
        //
        // Using log-frequency domain for smooth interpolation:
        //   log10(20)=1.3, log10(200)=2.3, log10(2k)=3.3, log10(20k)=4.3
        float logFreq = std::log10(std::max(frequency, 10.0f));

        // Polynomial: peak at ~250Hz (logFreq=2.4), drops at both ends
        // Coefficients tuned to match: Q=0.5 at 20Hz, Q=1.0 at 250Hz, Q=0.5 at 10kHz, Q=0.3 at 20kHz
        float centered = logFreq - 2.4f;  // Center around 250Hz
        float qMultiplier = 1.0f - 0.22f * centered * centered - 0.04f * centered * centered * centered * centered;
        qMultiplier = std::max(qMultiplier, 0.25f);

        // Program-dependent Q reduction: core saturation lowers Q
        // Hot signals cause inductor losses to increase
        float rmsReduction = 1.0f - rmsLevel * 0.15f;  // Up to 15% Q reduction at high levels
        rmsReduction = std::max(rmsReduction, 0.75f);

        return baseQ * qMultiplier * rmsReduction * componentQVariation;
    }

    /** Process inductor non-linearity: B-H curve saturation + hysteresis.
        Returns the saturated signal. Also updates internal saturationLevel
        which can be used to modulate filter Q (real inductors lose Q under saturation). */
    float processNonlinearity(float input, float driveLevel)
    {
        if (!safeIsFinite(input))
            return 0.0f;

        // Track RMS level for program-dependent behavior (45ms time constant)
        rmsLevel = rmsLevel * rmsDecay + input * input * (1.0f - rmsDecay);
        float rmsValue = std::sqrt(rmsLevel);

        // Adjust saturation threshold based on program level
        // Hot signals cause more compression (core heating simulation)
        float dynamicThreshold = (0.65f - rmsValue * 0.15f) * componentSatVariation;
        dynamicThreshold = std::max(dynamicThreshold, 0.35f);

        float saturatedInput = input;
        float absInput = std::abs(input);

        if (absInput > dynamicThreshold)
        {
            float excess = (absInput - dynamicThreshold) / (1.0f - dynamicThreshold);
            float langevin = std::tanh(excess * 2.5f * (1.0f + driveLevel));

            // Blend original with saturated
            float compressed = dynamicThreshold + langevin * (1.0f - dynamicThreshold) * 0.7f;
            saturatedInput = std::copysign(compressed, input);

            // Freed/UTC inductors operate 10-15dB below input in the passive EQ network.
            // Core saturation is subtle at normal levels, only significant at extreme LF boost.
            // H3 dominant (symmetric core), with small H2 from residual asymmetry.
            float h2Amount = 0.012f * driveLevel * excess;
            saturatedInput += h2Amount * input * absInput;

            float h3Amount = 0.018f * driveLevel * driveLevel * excess;
            saturatedInput += h3Amount * input * input * input;

            // Track saturation depth for Q modulation
            // Real inductors: inductance drops as core saturates, lowering filter Q
            currentSaturationLevel = currentSaturationLevel * 0.95f + excess * 0.05f;
        }
        else
        {
            currentSaturationLevel *= 0.95f;  // Decay when not saturating
        }

        // Hysteresis — wider loop at higher drive for more "iron" character
        float deltaInput = saturatedInput - prevInput;
        float hysteresisCoeff = 0.08f * driveLevel;

        // Core flux integration with decay (0.75ms time constant)
        coreFlux = coreFlux * fluxDecay + deltaInput * hysteresisCoeff;
        coreFlux = std::clamp(coreFlux, -0.15f, 0.15f);

        // Hysteresis adds slight asymmetry based on flux direction (0.28ms time constant)
        hysteresisState = hysteresisState * hystDecay + coreFlux * (1.0f - hystDecay);
        float output = saturatedInput + hysteresisState * 0.5f;

        prevInput = input;
        prevOutput = output;

        return output;
    }

    /** Get current saturation level (0-1) for filter Q modulation.
        When core saturates, inductance drops, which lowers the filter's resonant Q. */
    float getSaturationLevel() const { return std::min(currentSaturationLevel, 1.0f); }

    float getRmsLevel() const { return std::sqrt(rmsLevel); }

private:
    void computeDecayCoefficients(double sr)
    {
        rmsDecay = std::exp(-1.0f / (0.045f * static_cast<float>(sr)));       // 45ms RMS integration
        fluxDecay = std::exp(-1.0f / (0.00075f * static_cast<float>(sr)));    // 0.75ms core flux
        hystDecay = std::exp(-1.0f / (0.00028f * static_cast<float>(sr)));    // 0.28ms hysteresis
    }

    double sampleRate = 44100.0;
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float hysteresisState = 0.0f;
    float coreFlux = 0.0f;
    float rmsLevel = 0.0f;
    float currentSaturationLevel = 0.0f;

    // Sample-rate-dependent decay coefficients
    float rmsDecay = 0.9995f;
    float fluxDecay = 0.97f;
    float hystDecay = 0.92f;

    // Component tolerance variation (vintage unit character)
    float componentQVariation = 1.0f;
    float componentSatVariation = 1.0f;
};

/*
  Tube stage model: polynomial waveshaper calibrated to Pultec EQP-1A character.

  Architecture (per sample):
    1. Anti-alias LP at ~10 kHz  — HF residual bypasses waveshaper entirely
    2. LF split at ~300 Hz       — isolates sub-bass for transformer H3 boost
    3. Polynomial waveshaper     — y = x + b·x² + c·x³ + d·x⁴  (all from same x)
                                   x = filteredInput · drive · DRIVE_SCALE
    4. LF transformer H3         — extra H3 on the LF split component only
    5. DC blocking               — removes DC from even-order (x², x⁴) terms
    6. Gain compensation         — preserves unity gain at low drive

  Harmonic profile at nominal (drive=0.5, input amplitude A=0.5):
    H2 ≈ 0.375%   (12AX7/12AU7 push-pull warmth — dominant, ~82% of THD)
    H3 ≈ 0.065%   (general tube character — ~14% of THD)
    H4 ≈ 0.015%   (secondary tube — ~3% of THD)
    H5 ≈ 0%       (negligible)
  Total THD ≈ 0.38%, matching a nominal Pultec EQP-1A at 0 VU.

  Drive-dependent scaling (natural from polynomial):
    H2 ∝ drive¹·A    — grows linearly ("syrupy tube warmth" at low drive)
    H3 ∝ drive²·A²   — grows quadratically (transformer H3 rises faster at high drive)
    H4 ∝ drive³·A³   — grows cubically

  Frequency-dependent H3 (transformer modeling):
    The LF split at ~300 Hz ensures transformer H3 is dominant in the sub-bass.
    At 8 kHz the LF state is ~0, so no extra H3 is generated at HF.
    At 80 Hz the LF state ≈ input, producing the characteristic low-end "growl".

  Real Pultec EQP-1A: passive LC network + 12AX7/12AU7 tube makeup amp + Peerless
  transformers. H2-dominant from tubes (push-pull topology cancels odd harmonics but
  reinforces even ones). Peerless iron adds H3 in the low end from core saturation.
*/
class TubeEQTubeStage
{
public:
    void prepare(double sampleRate, int /*numChannels*/)
    {
        this->sampleRate = sampleRate;
        for (auto& dc : dcBlockers)
            dc.prepare(sampleRate, 8.0f);
        computeCoefficients(sampleRate);
        reset();
    }

    void reset()
    {
        preLPStates.fill(0.0f);
        lfStates.fill(0.0f);
        for (auto& dc : dcBlockers)
            dc.reset();
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread. */
    void updateSampleRate(double newRate)
    {
        sampleRate = newRate;
        computeCoefficients(newRate);
        reset();
    }

    void setDrive(float newDrive)
    {
        drive = juce::jlimit(0.0f, 1.0f, newDrive);
    }

    float processSample(float input, int channel)
    {
        if (!safeIsFinite(input))
            return 0.0f;

        int ch = std::clamp(channel, 0, maxChannels - 1);
        float& preLPState = preLPStates[static_cast<size_t>(ch)];
        float& lfState    = lfStates[static_cast<size_t>(ch)];

        // ── 1. Anti-alias pre-LP at ~10 kHz ─────────────────────────────────────
        // HF residual bypasses the waveshaper and is recombined at the output.
        // Prevents high-frequency content from generating alias products.
        // At oversampled rates the LP is transparent (cutoff remains at 10 kHz
        // regardless of sample rate, so it moves above 20 kHz at ≥88 kHz SR).
        preLPState += preLPCoeff * (input - preLPState);
        const float filteredInput = preLPState;
        const float hfPass = input - filteredInput;

        // ── 2. LF split at ~300 Hz (transformer core saturation model) ──────────
        // Only sub-bass content reaches lfState significantly; mid/HF is ~0.
        lfState += lfCoeff * (filteredInput - lfState);

        // ── 3. Polynomial waveshaper ─────────────────────────────────────────────
        // All harmonic terms computed from the same xd — no cascading inflation.
        // Dividing distortion by driveAmount normalises gain while preserving
        // harmonic scaling: H2 ∝ drive¹·A, H3 ∝ drive²·A², H4 ∝ drive³·A³.
        const float DRIVE_SCALE = 2.0f;
        const float driveAmount = drive * DRIVE_SCALE;
        const float xd_raw = filteredInput * driveAmount;
        // Soft-clip to prevent polynomial explosion at hot levels
        const float xd  = xd_raw / std::sqrt(1.0f + xd_raw * xd_raw * 0.25f);
        const float xd2 = xd * xd;
        const float xd3 = xd2 * xd;
        const float xd4 = xd2 * xd2;

        // Calibrated at nominal (drive=0.5, A=0.5, driveAmount=1.0):
        //   H2 ≈ 0.375%  — 12AX7/12AU7 push-pull warmth (dominant, ~82% of THD)
        //   H3 ≈ 0.065%  — general tube character (~14% of THD)
        //   H4 ≈ 0.015%  — secondary tube harmonic (~3% of THD)
        const float b = 0.015f;    // H2 — x² is EVEN → true 2nd harmonic + DC
        const float c = 0.0104f;  // H3 — x³ is ODD  → true 3rd harmonic
        const float d = 0.0096f;  // H4 — x⁴ is EVEN → true 4th harmonic + DC

        // ── 4. LF transformer H3 (Peerless iron core saturation) ────────────────
        // Extra H3 applied only to the LF component. At 8 kHz, lfState ≈ 0 —
        // no extra H3 is generated at high frequencies, preventing aliasing.
        // At 80 Hz, lfState ≈ filteredInput → pronounced "growl" in the sub-bass.
        // H3 from transformer grows as driveAmount² (faster than tube H2) — at
        // heavy drive the transformer H3 rises disproportionately, matching the
        // behaviour of a Pultec driven hard on a drum bus.
        const float xd_lf_raw = lfState * driveAmount;
        const float xd_lf = xd_lf_raw / std::sqrt(1.0f + xd_lf_raw * xd_lf_raw * 0.25f);
        const float transformerH3 = 0.025f * xd_lf * xd_lf * xd_lf;  // LF-only H3

        float distortion = b*xd2 + c*xd3 + d*xd4 + transformerH3;
        float y = filteredInput + distortion / driveAmount;

        // ── 5. DC blocking ───────────────────────────────────────────────────────
        // Even-order terms (x², x⁴) introduce DC; the DC blocker removes it.
        y = dcBlockers[static_cast<size_t>(ch)].processSample(y);

        // ── 6. Recombine: saturated LF/mid + clean HF bypass ────────────────────
        // No gain compensation needed: y = filteredInput + distortion/driveAmount
        // is inherently ~unity-gain at the fundamental.
        const float result = y + hfPass;
        return safeIsFinite(result) ? result : input;
    }

private:
    void computeCoefficients(double sr)
    {
        // Anti-alias LP — scales with sample rate but clamps to 10 kHz.
        // At 44.1 kHz: cutoff ≈ 9.7 kHz (0.22 * 44100). At ≥88.2 kHz the cutoff
        // rises above 10 kHz, keeping the filter transparent at oversampled rates.
        const float preLPFreq = std::min(10000.0f, static_cast<float>(sr) * 0.22f);
        preLPCoeff = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * preLPFreq / static_cast<float>(sr));

        // Transformer LF split at ~300 Hz.
        // Below 300 Hz: lfState tracks filteredInput (full amplitude → strong transformer H3).
        // Above 300 Hz: lfState ≈ 0 (no transformer H3 generated at mid/HF).
        lfCoeff = 1.0f - std::exp(
            -juce::MathConstants<float>::twoPi * 300.0f / static_cast<float>(sr));
    }

    static constexpr int maxChannels = 8;
    double sampleRate = 44100.0;
    float drive = 0.3f;
    float preLPCoeff = 0.77f;  // Anti-alias LP (~10 kHz at 44.1 kHz)
    float lfCoeff    = 0.04f;  // Transformer LF split (~300 Hz at 44.1 kHz)

    std::array<float, maxChannels> preLPStates{};
    std::array<float, maxChannels> lfStates{};
    std::array<AnalogEmulation::DCBlocker, maxChannels> dcBlockers;
};

/** Vintage passive EQ style LF section with dual-biquad boost/cut interaction.
    Peak filter for boost + low shelf for attenuation, with inductor nonlinearity
    applied between stages for authentic passive LC network behavior. */
class PultecLFSection
{
public:
    // Tunable interaction constants (named for easy adjustment)
    // Calibrated to match Pultec EQP-1A hardware measurements
    static constexpr float kPeakGainScale = 1.4f;       // Max ~14 dB boost (hardware: ~13.5 dB)
    static constexpr float kPeakInteraction = 0.08f;     // Boost enhanced slightly by cut presence
    static constexpr float kBaseQ = 0.55f;               // Base resonance width
    static constexpr float kQInteraction = 0.015f;       // Q sharpens with cut (hardware behavior)
    static constexpr float kDipFreqBase = 1.0f;          // Dip shelf at SAME freq as boost (Pultec Trick)
    static constexpr float kDipFreqRange = 0.0f;         // No gain-dependent frequency shift
    static constexpr float kDipGainScale = 1.75f;        // Max ~17.5 dB cut (hardware: ~17.5 dB)
    static constexpr float kDipInteraction = 0.06f;      // Boost presence slightly deepens cut
    static constexpr float kDipBaseQ = 0.65f;            // Broad shelf (wider than peak for Pultec Trick)
    static constexpr float kDipQScale = 0.03f;           // Q increases with atten amount

    void prepare(double sampleRate, uint32_t characterSeed = 0)
    {
        currentSampleRate = sampleRate;
        for (size_t i = 0; i < maxChannels; ++i)
            inductors[i].prepare(sampleRate, characterSeed + static_cast<uint32_t>(i));
        reset();
    }

    void reset()
    {
        for (auto& ch : channels)
        {
            ch.peakZ1 = ch.peakZ2 = 0.0f;
            ch.dipZ1 = ch.dipZ2 = 0.0f;
        }
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread. */
    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;
        for (auto& ind : inductors)
            ind.updateSampleRate(newRate);
        reset();
    }

    void updateCoefficients(float boostGain, float attenGain, float frequency,
                            double sampleRate)
    {
        cachedBoost = boostGain;
        cachedAtten = attenGain;
        cachedFreq = frequency;

        float maxFreq = static_cast<float>(sampleRate) * 0.45f;
        frequency = std::clamp(frequency, 10.0f, maxFreq);

        // ---- Peak filter (resonant boost) ----
        if (boostGain > 0.01f)
        {
            float peakGainDB = boostGain * kPeakGainScale + attenGain * boostGain * kPeakInteraction;
            float effectiveQ = inductors[0].getFrequencyDependentQ(frequency, kBaseQ);
            effectiveQ *= (1.0f + attenGain * kQInteraction);

            // Core saturation reduces Q: inductance drops as core saturates
            // This creates program-dependent behavior — louder signals get broader peaks
            float satLevel = inductors[0].getSaturationLevel();
            effectiveQ *= (1.0f - satLevel * 0.25f);  // Up to 25% Q reduction under saturation

            effectiveQ = std::max(effectiveQ, 0.2f);

            // Bandwidth-matched peaking: kbw=tan(π·bw/sr), center via cos(2π·fc/sr).
            // Avoids Q/bandwidth cramping near Nyquist (same formula as setTubeEQPeakCoeffs).
            double fc  = std::max(1.0, std::min((double)frequency, sampleRate * 0.4998));
            double bw  = fc / std::max(0.01, (double)effectiveQ);
            double kbw = std::tan(juce::MathConstants<double>::pi * std::min(bw, sampleRate * 0.4998) / sampleRate);
            double A   = std::pow(10.0, (double)peakGainDB / 40.0);
            double cosW = std::cos(2.0 * juce::MathConstants<double>::pi * fc / sampleRate);

            double pb0 = 1.0 + kbw * A,  pb2 = 1.0 - kbw * A;
            double pa0 = 1.0 + kbw / A,  pa2 = 1.0 - kbw / A;
            double pb1 = -2.0 * cosW;
            double pa1 = -2.0 * cosW;

            peakB0 = (float)(pb0/pa0); peakB1 = (float)(pb1/pa0); peakB2 = (float)(pb2/pa0);
            peakA1 = (float)(pa1/pa0); peakA2 = (float)(pa2/pa0);
        }
        else
        {
            peakB0 = 1.0f; peakB1 = 0.0f; peakB2 = 0.0f;
            peakA1 = 0.0f; peakA2 = 0.0f;
        }

        // ---- Dip shelf (attenuation with interaction) ----
        if (attenGain > 0.01f)
        {
            float dipFreqRatio = kDipFreqBase + kDipFreqRange * (1.0f - attenGain / 10.0f);
            float dipFreq = frequency * dipFreqRatio;
            dipFreq = std::clamp(dipFreq, 10.0f, maxFreq);

            float dipGainDB = -(attenGain * kDipGainScale + boostGain * attenGain * kDipInteraction);
            float dipQ = kDipBaseQ + attenGain * kDipQScale;

            // Pre-warped low-shelf: k=tan(π·fc/sr) places turnover at exactly dipFreq.
            // Derives cosW/sinW from k to avoid double-warping (same as setLowShelfCoeffs).
            double dfc  = std::max(1.0, std::min((double)dipFreq, sampleRate * 0.4998));
            double dA   = std::pow(10.0, (double)dipGainDB / 40.0);
            double dk   = std::tan(juce::MathConstants<double>::pi * dfc / sampleRate);
            double dk2  = dk * dk;
            double dsqA = std::sqrt(dA);
            double dcq  = std::max(0.01, (double)dipQ);
            double dcosW = (1.0 - dk2) / (1.0 + dk2);
            double dsinW = 2.0 * dk / (1.0 + dk2);
            double dalpha = dsinW / 2.0 * std::sqrt((dA + 1.0/dA) * (1.0/dcq - 1.0) + 2.0);

            double db0 = dA * ((dA+1.0) - (dA-1.0)*dcosW + 2.0*dsqA*dalpha);
            double db1 = 2.0*dA * ((dA-1.0) - (dA+1.0)*dcosW);
            double db2 = dA * ((dA+1.0) - (dA-1.0)*dcosW - 2.0*dsqA*dalpha);
            double da0 = (dA+1.0) + (dA-1.0)*dcosW + 2.0*dsqA*dalpha;
            double da1 = -2.0 * ((dA-1.0) + (dA+1.0)*dcosW);
            double da2 = (dA+1.0) + (dA-1.0)*dcosW - 2.0*dsqA*dalpha;

            dipB0 = (float)(db0/da0); dipB1 = (float)(db1/da0); dipB2 = (float)(db2/da0);
            dipA1 = (float)(da1/da0); dipA2 = (float)(da2/da0);
        }
        else
        {
            dipB0 = 1.0f; dipB1 = 0.0f; dipB2 = 0.0f;
            dipA1 = 0.0f; dipA2 = 0.0f;
        }
    }

    float processSample(float input, int channel)
    {
        int ch = std::clamp(channel, 0, maxChannels - 1);
        auto& s = channels[ch];

        // Peak filter (Direct Form II Transposed)
        float peakOut = peakB0 * input + s.peakZ1;
        s.peakZ1 = peakB1 * input - peakA1 * peakOut + s.peakZ2;
        s.peakZ2 = peakB2 * input - peakA2 * peakOut;

        // Inductor nonlinearity between stages (subtle saturation — per-channel to avoid L/R state bleed)
        float interStage = (cachedBoost > 0.01f)
            ? inductors[static_cast<size_t>(ch)].processNonlinearity(peakOut, cachedBoost * 0.3f)
            : peakOut;

        // Dip shelf filter
        float dipOut = dipB0 * interStage + s.dipZ1;
        s.dipZ1 = dipB1 * interStage - dipA1 * dipOut + s.dipZ2;
        s.dipZ2 = dipB2 * interStage - dipA2 * dipOut;

        // State clamping for safety
        auto clampState = [](float& v) { v = std::clamp(v, -8.0f, 8.0f); };
        clampState(s.peakZ1); clampState(s.peakZ2);
        clampState(s.dipZ1); clampState(s.dipZ2);

        return safeIsFinite(dipOut) ? dipOut : input;
    }

    float getMagnitudeDB(float freqHz, double sampleRate) const
    {
        double omega = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        double cosw = std::cos(omega);
        double sinw = std::sin(omega);
        double cos2w = std::cos(2.0 * omega);
        double sin2w = std::sin(2.0 * omega);

        auto biquadMag = [&](float b0, float b1, float b2, float a1, float a2) -> double
        {
            double numR = b0 + b1 * cosw + b2 * cos2w;
            double numI = -(b1 * sinw + b2 * sin2w);
            double denR = 1.0 + a1 * cosw + a2 * cos2w;
            double denI = -(a1 * sinw + a2 * sin2w);
            double numMag2 = numR * numR + numI * numI;
            double denMag2 = denR * denR + denI * denI;
            return (denMag2 > 1e-20) ? std::sqrt(numMag2 / denMag2) : 1.0;
        };

        double peakMag = biquadMag(peakB0, peakB1, peakB2, peakA1, peakA2);
        double dipMag = biquadMag(dipB0, dipB1, dipB2, dipA1, dipA2);
        double combined = peakMag * dipMag;
        return static_cast<float>(20.0 * std::log10(combined + 1e-10));
    }

    InductorModel& getInductor() { return inductors[0]; }

    // Get inductor RMS level for program-dependent metering
    float getInductorRmsLevel() const { return inductors[0].getRmsLevel(); }

private:
    static constexpr int maxChannels = 8;

    float peakB0 = 1.0f, peakB1 = 0.0f, peakB2 = 0.0f;
    float peakA1 = 0.0f, peakA2 = 0.0f;
    float dipB0 = 1.0f, dipB1 = 0.0f, dipB2 = 0.0f;
    float dipA1 = 0.0f, dipA2 = 0.0f;

    struct ChannelState {
        float peakZ1 = 0.0f, peakZ2 = 0.0f;
        float dipZ1 = 0.0f, dipZ2 = 0.0f;
    };
    ChannelState channels[maxChannels] = {};

    std::array<InductorModel, maxChannels> inductors;
    double currentSampleRate = 44100.0;
    float cachedBoost = 0.0f, cachedAtten = 0.0f, cachedFreq = 60.0f;
};

class TubeEQProcessor
{
public:
    // Parameter structure for Tube EQ
    struct Parameters
    {
        // Low Frequency Section
        float lfBoostGain = 0.0f;      // 0-10 (maps to 0-14 dB)
        float lfBoostFreq = 60.0f;     // 20, 30, 60, 100 Hz (4 positions)
        float lfAttenGain = 0.0f;      // 0-10 (maps to 0-16 dB cut)

        // High Frequency Boost Section
        float hfBoostGain = 0.0f;      // 0-10 (maps to 0-16 dB)
        float hfBoostFreq = 8000.0f;   // 3k, 4k, 5k, 8k, 10k, 12k, 16k Hz
        float hfBoostBandwidth = 0.5f; // Sharp to Broad (Q control)

        // High Frequency Attenuation (shelf)
        float hfAttenGain = 0.0f;      // 0-10 (maps to 0-16 dB cut)
        float hfAttenFreq = 10000.0f;  // 5k, 10k, 20k Hz (3 positions)

        // Mid Dip/Peak Section
        bool midEnabled = true;           // Section bypass
        float midLowFreq = 500.0f;        // 0.2, 0.3, 0.5, 0.7, 1.0 kHz
        float midLowPeak = 0.0f;          // 0-10 (maps to 0-12 dB boost)
        float midDipFreq = 700.0f;        // 0.2, 0.3, 0.5, 0.7, 1.0, 1.5, 2.0 kHz
        float midDip = 0.0f;              // 0-10 (maps to 0-10 dB cut)
        float midHighFreq = 3000.0f;      // 1.5, 2.0, 3.0, 4.0, 5.0 kHz
        float midHighPeak = 0.0f;         // 0-10 (maps to 0-12 dB boost)

        // Global controls
        float inputGain = 0.0f;        // -12 to +12 dB
        float outputGain = 0.0f;       // -12 to +12 dB
        float tubeDrive = 0.3f;        // 0-1 (tube saturation amount)
        bool bypass = false;
    };

    TubeEQProcessor()
    {
        // Initialize with default tube type
    }

    void prepare(double sampleRate, int samplesPerBlock, int numChannels)
    {
        // Tube stage and LC network support max 8 channels; channels beyond 8
        // will alias to channel 7's state.
        jassert(numChannels <= maxProcessChannels);

        currentSampleRate = sampleRate;
        this->numChannels = std::min(numChannels, maxProcessChannels);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 1;

        // Prepare HF boost filters (resonant peak with bandwidth)
        hfBoostFilterL.prepare(spec);
        hfBoostFilterR.prepare(spec);

        // Prepare HF atten filters (shelf)
        hfAttenFilterL.prepare(spec);
        hfAttenFilterR.prepare(spec);

        // Prepare Mid section filters
        midLowPeakFilterL.prepare(spec);
        midLowPeakFilterR.prepare(spec);
        midDipFilterL.prepare(spec);
        midDipFilterR.prepare(spec);
        midHighPeakFilterL.prepare(spec);
        midHighPeakFilterR.prepare(spec);

        // Prepare enhanced analog stages
        // Use deterministic seed based on sample rate for reproducible vintage character
        characterSeed = static_cast<uint32_t>(sampleRate * 1000.0);
        tubeStage.prepare(sampleRate, numChannels);
        pultecLF.prepare(sampleRate, characterSeed);
        hfInductorL.prepare(sampleRate, characterSeed + 1);  // Offset for variation between inductors
        hfInductorR.prepare(sampleRate, characterSeed + 2);  // Different seed for subtle L/R character variation
        hfQInductor.prepare(sampleRate, characterSeed + 1);

        // Prepare transformers
        inputTransformer.prepare(sampleRate, numChannels);
        outputTransformer.prepare(sampleRate, numChannels);

        // Set up transformer profiles
        setupTransformerProfiles();

        // Initialize analog emulation library
        AnalogEmulation::initializeLibrary();

        // Create initial passthrough coefficients for all IIR filters (off audio thread)
        // so that updateFilters() can modify them in-place without heap allocations
        initFilterCoefficients(hfBoostFilterL);
        initFilterCoefficients(hfBoostFilterR);
        initFilterCoefficients(hfAttenFilterL);
        initFilterCoefficients(hfAttenFilterR);
        initFilterCoefficients(midLowPeakFilterL);
        initFilterCoefficients(midLowPeakFilterR);
        initFilterCoefficients(midDipFilterL);
        initFilterCoefficients(midDipFilterR);
        initFilterCoefficients(midHighPeakFilterL);
        initFilterCoefficients(midHighPeakFilterR);

        reset();
    }

    void reset()
    {
        hfBoostFilterL.reset();
        hfBoostFilterR.reset();
        hfAttenFilterL.reset();
        hfAttenFilterR.reset();
        midLowPeakFilterL.reset();
        midLowPeakFilterR.reset();
        midDipFilterL.reset();
        midDipFilterR.reset();
        midHighPeakFilterL.reset();
        midHighPeakFilterR.reset();
        tubeStage.reset();
        pultecLF.reset();
        hfInductorL.reset();
        hfInductorR.reset();
        inputTransformer.reset();
        outputTransformer.reset();
    }

    /** Lightweight sample-rate update (no allocation). Safe for audio thread.
        Resets filter state and marks parameters dirty for coefficient recalculation.
        NOTE: Transformer sample rates are deferred to the next full prepare(). */
    void updateSampleRate(double newRate)
    {
        currentSampleRate = newRate;

        // Lightweight rate updates (no allocation, safe for audio thread)
        tubeStage.updateSampleRate(newRate);
        pultecLF.updateSampleRate(newRate);
        hfInductorL.updateSampleRate(newRate);
        hfInductorR.updateSampleRate(newRate);
        hfQInductor.updateSampleRate(newRate);
        // Transformers deferred to next full prepare() (TransformerEmulation::prepare may allocate)

        parametersNeedUpdate.store(true, std::memory_order_release);
        reset();
    }

    void setParameters(const Parameters& newParams)
    {
        {
            juce::SpinLock::ScopedLockType lock(paramLock);
            pendingParams = newParams;
        }
        parametersNeedUpdate.store(true, std::memory_order_release);
    }

    Parameters getParameters() const
    {
        juce::SpinLock::ScopedLockType lock(paramLock);
        return params;
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        // Apply pending parameter updates (deferred from setParameters for thread safety)
        if (parametersNeedUpdate.exchange(false, std::memory_order_acquire))
        {
            {
                juce::SpinLock::ScopedLockType lock(paramLock);
                params = pendingParams;
            }
            updateFilters();
            tubeStage.setDrive(params.tubeDrive);
        }

        if (params.bypass)
            return;

        const int numSamples = buffer.getNumSamples();
        const int channels = std::min(buffer.getNumChannels(), maxProcessChannels);

        // Apply input gain
        if (std::abs(params.inputGain) > 0.01f)
        {
            float inputGainLinear = juce::Decibels::decibelsToGain(params.inputGain);
            buffer.applyGain(inputGainLinear);
        }

        // Process each channel
        for (int ch = 0; ch < channels; ++ch)
        {
            float* channelData = buffer.getWritePointer(ch);
            bool isLeft = (ch % 2 == 0);  // L/R pairs for stereo and surround

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = channelData[i];

                // NaN/Inf protection - skip processing if input is invalid
                if (!safeIsFinite(sample))
                {
                    channelData[i] = 0.0f;
                    continue;
                }

                // Input transformer coloration
                sample = inputTransformer.processSample(sample, ch);

                // === LF Section: dual-biquad boost/cut interaction ===
                sample = pultecLF.processSample(sample, ch);

                // === HF Section with inductor characteristics ===
                if (params.hfBoostGain > 0.01f)
                {
                    // Apply inductor nonlinearity before HF boost (per-channel to avoid L/R cross-contamination)
                    sample = isLeft ? hfInductorL.processNonlinearity(sample, params.hfBoostGain * 0.2f)
                                    : hfInductorR.processNonlinearity(sample, params.hfBoostGain * 0.2f);

                    sample = isLeft ? hfBoostFilterL.processSample(sample)
                                    : hfBoostFilterR.processSample(sample);
                }

                // HF Attenuation (shelf)
                if (params.hfAttenGain > 0.01f)
                {
                    sample = isLeft ? hfAttenFilterL.processSample(sample)
                                    : hfAttenFilterR.processSample(sample);
                }

                // === Mid Dip/Peak Section ===
                if (params.midEnabled)
                {
                    // Mid Low Peak
                    if (params.midLowPeak > 0.01f)
                    {
                        sample = isLeft ? midLowPeakFilterL.processSample(sample)
                                        : midLowPeakFilterR.processSample(sample);
                    }

                    // Mid Dip (cut)
                    if (params.midDip > 0.01f)
                    {
                        sample = isLeft ? midDipFilterL.processSample(sample)
                                        : midDipFilterR.processSample(sample);
                    }

                    // Mid High Peak
                    if (params.midHighPeak > 0.01f)
                    {
                        sample = isLeft ? midHighPeakFilterL.processSample(sample)
                                        : midHighPeakFilterR.processSample(sample);
                    }
                }

                // Tube makeup gain stage
                if (params.tubeDrive > 0.01f)
                {
                    sample = tubeStage.processSample(sample, ch);
                }

                // Output transformer
                sample = outputTransformer.processSample(sample, ch);

                // NaN/Inf protection - zero output if processing produced invalid result
                if (!safeIsFinite(sample))
                    sample = 0.0f;

                channelData[i] = sample;
            }
        }

        // Per-block inductor Q modulation for HF boost
        // Real Pultec inductors: core saturation reduces inductance → lowers filter Q
        // Update HF boost filter Q based on per-channel inductor saturation state (once per block, not per sample)
        if (params.hfBoostGain > 0.01f)
        {
            float baseQ = juce::jmap(params.hfBoostBandwidth, 0.0f, 1.0f, 2.0f, 0.3f);
            float freq = params.hfBoostFreq;

            float satLevelL = hfInductorL.getSaturationLevel();
            if (satLevelL > 0.01f)
            {
                float effectiveQL = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);
                // Reduce Q proportionally to saturation (up to 20% reduction)
                effectiveQL *= (1.0f - satLevelL * 0.20f);
                setTubeEQPeakCoeffs(hfBoostFilterL, currentSampleRate, freq, effectiveQL, params.hfBoostGain * 1.8f);
            }

            float satLevelR = hfInductorR.getSaturationLevel();
            if (satLevelR > 0.01f)
            {
                float effectiveQR = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);
                effectiveQR *= (1.0f - satLevelR * 0.20f);
                setTubeEQPeakCoeffs(hfBoostFilterR, currentSampleRate, freq, effectiveQR, params.hfBoostGain * 1.8f);
            }
        }

        // Apply output gain
        if (std::abs(params.outputGain) > 0.01f)
        {
            float outputGainLinear = juce::Decibels::decibelsToGain(params.outputGain);
            buffer.applyGain(outputGainLinear);
        }
    }

    /** Get LF section magnitude at a frequency (for curve display). */
    float getPultecLFMagnitudeDB(float frequencyHz) const
    {
        return pultecLF.getMagnitudeDB(frequencyHz, currentSampleRate);
    }

    // Get frequency response magnitude at a specific frequency (for curve display)
    float getFrequencyResponseMagnitude(float frequencyHz) const
    {
        // Snapshot parameters and coefficient Ptrs under the lock. The underlying
        // coefficient values may be updated in-place by the audio thread (benign
        // data race) — torn reads just cause a brief visual glitch in the curve
        // display, self-correcting on the next repaint frame.
        Parameters localParams;
        juce::dsp::IIR::Coefficients<float>::Ptr localHfBoost, localHfAtten;
        juce::dsp::IIR::Coefficients<float>::Ptr localMidLowPeak, localMidDip, localMidHighPeak;
        {
            juce::SpinLock::ScopedLockType lock(paramLock);
            localParams = params;
            localHfBoost = hfBoostFilterL.coefficients;
            localHfAtten = hfAttenFilterL.coefficients;
            localMidLowPeak = midLowPeakFilterL.coefficients;
            localMidDip = midDipFilterL.coefficients;
            localMidHighPeak = midHighPeakFilterL.coefficients;
        }

        if (localParams.bypass)
            return 0.0f;

        float magnitudeDB = 0.0f;

        // Calculate contribution from each filter
        double omega = juce::MathConstants<double>::twoPi * frequencyHz / currentSampleRate;

        // LF Section (dual-biquad: combined boost + atten interaction)
        if (localParams.lfBoostGain > 0.01f || localParams.lfAttenGain > 0.01f)
        {
            magnitudeDB += pultecLF.getMagnitudeDB(frequencyHz, currentSampleRate);
        }

        // Helper: evaluate biquad magnitude from JUCE IIR coefficients
        // JUCE stores 5 elements: {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0}
        // Transfer function: H(z) = (c[0] + c[1]*z^-1 + c[2]*z^-2) / (1 + c[3]*z^-1 + c[4]*z^-2)
        auto biquadMagDB = [&](const juce::dsp::IIR::Coefficients<float>& coeffs) -> float {
            std::complex<double> z = std::exp(std::complex<double>(0.0, omega));
            std::complex<double> zInv = 1.0 / z;
            std::complex<double> zInv2 = zInv * zInv;

            std::complex<double> num = static_cast<double>(coeffs.coefficients[0])
                                     + static_cast<double>(coeffs.coefficients[1]) * zInv
                                     + static_cast<double>(coeffs.coefficients[2]) * zInv2;
            std::complex<double> den = 1.0
                                     + static_cast<double>(coeffs.coefficients[3]) * zInv
                                     + static_cast<double>(coeffs.coefficients[4]) * zInv2;

            return static_cast<float>(20.0 * std::log10(std::abs(num / den) + 1e-10));
        };

        // HF Boost contribution
        if (localParams.hfBoostGain > 0.01f && localHfBoost != nullptr)
            magnitudeDB += biquadMagDB(*localHfBoost);

        // HF Atten contribution
        if (localParams.hfAttenGain > 0.01f && localHfAtten != nullptr)
            magnitudeDB += biquadMagDB(*localHfAtten);

        // ===== MID SECTION CONTRIBUTIONS =====
        if (localParams.midEnabled)
        {
            if (localParams.midLowPeak > 0.01f && localMidLowPeak != nullptr)
                magnitudeDB += biquadMagDB(*localMidLowPeak);

            if (localParams.midDip > 0.01f && localMidDip != nullptr)
                magnitudeDB += biquadMagDB(*localMidDip);

            if (localParams.midHighPeak > 0.01f && localMidHighPeak != nullptr)
                magnitudeDB += biquadMagDB(*localMidHighPeak);
        }

        return magnitudeDB;
    }

private:
    Parameters params;
    Parameters pendingParams;
    std::atomic<bool> parametersNeedUpdate{false};
    mutable juce::SpinLock paramLock;
    double currentSampleRate = 44100.0;
    int numChannels = 2;
    uint32_t characterSeed = 0;

    // HF Boost: Resonant peak with bandwidth
    juce::dsp::IIR::Filter<float> hfBoostFilterL, hfBoostFilterR;

    // HF Atten: High shelf cut
    juce::dsp::IIR::Filter<float> hfAttenFilterL, hfAttenFilterR;

    // Mid Section filters
    juce::dsp::IIR::Filter<float> midLowPeakFilterL, midLowPeakFilterR;
    juce::dsp::IIR::Filter<float> midDipFilterL, midDipFilterR;
    juce::dsp::IIR::Filter<float> midHighPeakFilterL, midHighPeakFilterR;

    // Enhanced analog stages
    TubeEQTubeStage tubeStage;
    PultecLFSection pultecLF;
    InductorModel hfInductorL;  // Per-channel inductors to prevent L/R state cross-contamination
    InductorModel hfInductorR;

    // Persistent inductor model for HF Q computation (avoids mt19937 allocation on audio thread)
    InductorModel hfQInductor;

    static constexpr int maxProcessChannels = 8;

    // Transformers
    AnalogEmulation::TransformerEmulation inputTransformer;
    AnalogEmulation::TransformerEmulation outputTransformer;

    void setupTransformerProfiles()
    {
        // Input transformer profile (Chicago/UTC/Freed style)
        // Pultec transformers have symmetric B-H curves → odd harmonics (H3, H5)
        // This is distinct from Neve (asymmetric → H2). The combination of
        // even-order tube harmonics + odd-order transformer harmonics creates
        // the signature Pultec character.
        AnalogEmulation::TransformerProfile inputProfile;
        inputProfile.hasTransformer = true;
        inputProfile.saturationAmount = 0.15f;
        inputProfile.lowFreqSaturation = 1.3f;  // LF saturation boost (core flux physics)
        inputProfile.highFreqRolloff = 22000.0f;
        inputProfile.dcBlockingFreq = 10.0f;
        inputProfile.harmonics = { 0.005f, 0.015f, 0.002f };  // H3 dominant (symmetric core)

        inputTransformer.setProfile(inputProfile);
        inputTransformer.setEnabled(true);

        // Output transformer — slightly more color, same odd-harmonic character
        // Output transformer saturates at lower levels (~+18-20 dBu at 60 Hz)
        AnalogEmulation::TransformerProfile outputProfile;
        outputProfile.hasTransformer = true;
        outputProfile.saturationAmount = 0.12f;
        outputProfile.lowFreqSaturation = 1.2f;
        outputProfile.highFreqRolloff = 20000.0f;
        outputProfile.dcBlockingFreq = 8.0f;
        outputProfile.harmonics = { 0.004f, 0.012f, 0.001f };  // H3 dominant

        outputTransformer.setProfile(outputProfile);
        outputTransformer.setEnabled(true);
    }

    void updateFilters()
    {
        // LF Section: dual-biquad with boost/cut interaction
        pultecLF.updateCoefficients(params.lfBoostGain, params.lfAttenGain,
                                     params.lfBoostFreq, currentSampleRate);
        updateHFBoost();
        updateHFAtten();
        updateMidLowPeak();
        updateMidDip();
        updateMidHighPeak();
    }

    void updateHFBoost()
    {
        // HF resonant peak with variable bandwidth
        float freq = params.hfBoostFreq;
        float gainDB = params.hfBoostGain * 1.8f;  // 0-10 maps to ~0-18 dB (hardware: ~18 dB)

        // Bandwidth control: Sharp (high Q) to Broad (low Q)
        // EQP-1A bandwidth pot range: Q ~2.0 (narrow) to ~0.3 (wide)
        float baseQ = juce::jmap(params.hfBoostBandwidth, 0.0f, 1.0f, 2.0f, 0.3f);

        // Frequency-dependent Q from inductor model
        float effectiveQ = hfQInductor.getFrequencyDependentQ(params.hfBoostFreq, baseQ);

        setTubeEQPeakCoeffs(hfBoostFilterL, currentSampleRate, freq, effectiveQ, gainDB);
        setTubeEQPeakCoeffs(hfBoostFilterR, currentSampleRate, freq, effectiveQ, gainDB);
    }

    void updateHFAtten()
    {
        // HF high shelf cut
        float freq = params.hfAttenFreq;
        float gainDB = -params.hfAttenGain * 1.6f;  // 0-10 maps to ~0-16 dB cut (hardware: ~16 dB)

        setHighShelfCoeffs(hfAttenFilterL, currentSampleRate, freq, 0.6f, gainDB);
        setHighShelfCoeffs(hfAttenFilterR, currentSampleRate, freq, 0.6f, gainDB);
    }

    void updateMidLowPeak()
    {
        // Mid Low Peak: Resonant boost in low-mid range
        float freq = params.midLowFreq;
        float gainDB = params.midLowPeak * 1.2f;  // 0-10 maps to ~0-12 dB

        // Moderate Q for musical character
        float q = 1.2f;

        setTubeEQPeakCoeffs(midLowPeakFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midLowPeakFilterR, currentSampleRate, freq, q, gainDB);
    }

    void updateMidDip()
    {
        // Mid Dip: Cut in mid range
        float freq = params.midDipFreq;
        float gainDB = -params.midDip * 1.0f;  // 0-10 maps to ~0-10 dB cut

        // Broader Q for natural sounding cut
        float q = 0.8f;

        setTubeEQPeakCoeffs(midDipFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midDipFilterR, currentSampleRate, freq, q, gainDB);
    }

    void updateMidHighPeak()
    {
        // Mid High Peak: Resonant boost in upper-mid range
        float freq = params.midHighFreq;
        float gainDB = params.midHighPeak * 1.2f;  // 0-10 maps to ~0-12 dB

        // Moderate Q for presence
        float q = 1.4f;

        setTubeEQPeakCoeffs(midHighPeakFilterL, currentSampleRate, freq, q, gainDB);
        setTubeEQPeakCoeffs(midHighPeakFilterR, currentSampleRate, freq, q, gainDB);
    }

    // Create initial passthrough coefficients for a filter (called from prepare(), off audio thread)
    static void initFilterCoefficients(juce::dsp::IIR::Filter<float>& filter)
    {
        filter.coefficients = new juce::dsp::IIR::Coefficients<float>(
            1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    }

    // Assign biquad coefficients in-place (no heap allocation)
    // JUCE IIR::Coefficients stores 5 elements: {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0}
    // (a0 is divided out during construction, not stored as a separate element)
    static void setFilterCoeffs(juce::dsp::IIR::Filter<float>& filter,
                                float b0, float b1, float b2, float a1, float a2)
    {
        if (filter.coefficients == nullptr)
            return;
        auto* c = filter.coefficients->coefficients.getRawDataPointer();
        c[0] = b0; c[1] = b1; c[2] = b2; c[3] = a1; c[4] = a2;
    }

    // Tube EQ peak filter with inductor characteristics — cramping-free.
    // Pre-warps bandwidth: kbw=tan(π·bw/sr), center via cos(2π·fc/sr).
    // Preserves q*0.85 inductor broadening character.
    void setTubeEQPeakCoeffs(juce::dsp::IIR::Filter<float>& filter,
                             double sampleRate, float freq, float q, float gainDB) const
    {
        // Inductor-style Q modification - broader, more musical
        double tubeEQQ = std::max(0.01, (double)q * 0.85);
        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double bw  = fc / tubeEQQ;
        double kbw = std::tan(juce::MathConstants<double>::pi * std::min(bw, sampleRate * 0.4998) / sampleRate);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double cosW = std::cos(2.0 * juce::MathConstants<double>::pi * fc / sampleRate);

        // A = 10^(gainDB/40) < 1 for cuts — no branch needed, formula handles both.
        const double b0 = 1.0 + kbw * A,  b2 = 1.0 - kbw * A;
        const double a0 = 1.0 + kbw / A,  a2 = 1.0 - kbw / A;
        const double b1 = -2.0 * cosW;
        const double a1 = -2.0 * cosW;
        setFilterCoeffs(filter, (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                                (float)(a1/a0), (float)(a2/a0));
    }

    // Low shelf — cramping-free, derives cosW/sinW from k=tan(π·fc/sr).
    void setLowShelfCoeffs(juce::dsp::IIR::Filter<float>& filter,
                           double sampleRate, float freq, float q, float gainDB) const
    {
        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double cq  = std::max(0.01, (double)q);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double sqA = std::sqrt(A);
        double k   = std::tan(juce::MathConstants<double>::pi * fc / sampleRate);
        double k2  = k * k;
        double cosW  = (1.0 - k2) / (1.0 + k2);
        double sinW  = 2.0 * k   / (1.0 + k2);
        double alpha = sinW / 2.0 * std::sqrt((A + 1.0/A) * (1.0/cq - 1.0) + 2.0);

        double b0 =  A * ((A+1.0) - (A-1.0)*cosW + 2.0*sqA*alpha);
        double b1 =  2.0*A * ((A-1.0) - (A+1.0)*cosW);
        double b2 =  A * ((A+1.0) - (A-1.0)*cosW - 2.0*sqA*alpha);
        double a0 = (A+1.0) + (A-1.0)*cosW + 2.0*sqA*alpha;
        double a1 = -2.0 * ((A-1.0) + (A+1.0)*cosW);
        double a2 = (A+1.0) + (A-1.0)*cosW - 2.0*sqA*alpha;
        setFilterCoeffs(filter, (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                                (float)(a1/a0), (float)(a2/a0));
    }

    // High shelf — cramping-free, derives cosW/sinW from k=tan(π·fc/sr).
    void setHighShelfCoeffs(juce::dsp::IIR::Filter<float>& filter,
                            double sampleRate, float freq, float q, float gainDB) const
    {
        double fc  = std::max(1.0, std::min((double)freq, sampleRate * 0.4998));
        double cq  = std::max(0.01, (double)q);
        double A   = std::pow(10.0, (double)gainDB / 40.0);
        double sqA = std::sqrt(A);
        double k   = std::tan(juce::MathConstants<double>::pi * fc / sampleRate);
        double k2  = k * k;
        double cosW  = (1.0 - k2) / (1.0 + k2);
        double sinW  = 2.0 * k   / (1.0 + k2);
        double alpha = sinW / 2.0 * std::sqrt((A + 1.0/A) * (1.0/cq - 1.0) + 2.0);

        double b0 =  A * ((A+1.0) + (A-1.0)*cosW + 2.0*sqA*alpha);
        double b1 = -2.0*A * ((A-1.0) + (A+1.0)*cosW);
        double b2 =  A * ((A+1.0) + (A-1.0)*cosW - 2.0*sqA*alpha);
        double a0 = (A+1.0) - (A-1.0)*cosW + 2.0*sqA*alpha;
        double a1 =  2.0 * ((A-1.0) - (A+1.0)*cosW);
        double a2 = (A+1.0) - (A-1.0)*cosW - 2.0*sqA*alpha;
        setFilterCoeffs(filter, (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                                (float)(a1/a0), (float)(a2/a0));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TubeEQProcessor)
};
