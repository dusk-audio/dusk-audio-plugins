// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FilterEngine.hpp — stable, separately voiced filter models for the analogue
// Sunset Circuits modes.
//
// The previous implementation shared a forward-Euler four-pole cascade between
// Cosmos, Oracle and Mono.  Its coefficient became unstable in the top octave
// and its only "self oscillation" was a Nyquist flip-flop.  The models below use
// topology-preserving-transform (TPT) integrators and an algebraic four-pole
// feedback solve.  The integrators remain stable at every legal cutoff, and the
// Oracle/Modular feedback paths can ring musically instead of alternating at
// Nyquist.
//
// These are behavioural circuit models: each family has independent feedback,
// drive, stage saturation and bass-compensation calibration.  They deliberately
// share only the numerically stable TPT primitive, not one set of voicing
// constants.

#pragma once

#include "SynthCommon.hpp"

namespace msynth
{

enum class FilterMode
{
    Cosmos = 0,
    Oracle,
    Mono,
    Modular
};

enum class ModularFilterModel
{
    Early = 0, // earlier ladder: open top end, stronger saturation
    Late      // later ladder: characteristic ~12 kHz cutoff ceiling
};

// First-order non-resonant high-pass used by Cosmos.
class SimpleHPF
{
public:
    void prepare(double sampleRate) noexcept { sr = (float)sampleRate; reset(); }

    void setCutoff(float cutoffHz) noexcept
    {
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.45f);
        const float g = std::tan(kPi * fc / sr);
        a = 1.0f / (1.0f + g);
    }

    float process(float input) noexcept
    {
        // Bilinear one-pole HPF.  `lpState` is the matching TPT low-pass state.
        const float hp = (input - lpState) * a;
        const float lp = input - hp;
        lpState = lp + (lp - lpState);
        if (isBad(lpState)) lpState = 0.0f;
        return hp;
    }

    void reset() noexcept { lpState = 0.0f; }

private:
    float sr = 44100.0f;
    float a = 1.0f;
    float lpState = 0.0f;
};

// Four cascaded TPT one-poles with a zero-delay linear feedback solve and
// bounded analogue-stage saturation.  The state contribution to the fourth
// pole is solved before the stages are advanced:
//
//   y4 = G^4*u + sigma, u = (x - k*sigma) / (1 + k*G^4)
//
// Non-linearity is then applied at the input and each integrator.  This is the
// inexpensive "predict then saturate" form commonly used for polyphonic virtual
// analogue filters: it keeps the resonance pitch stable and the loop bounded
// without an iterative per-sample Newton solve.
class TPTFourPole
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        dcCoeff = std::exp(-kTwoPi * 5.0f / sr);
        reset();
    }

    void configure(float maxFb, float inputGain, float inputSat,
                   float stageSat, float lowComp, float outGain) noexcept
    {
        maxFeedback = maxFb;
        preGain = inputGain;
        inputDrive = maxf(0.05f, inputSat);
        stageDrive = maxf(0.05f, stageSat);
        bassComp = lowComp;
        outputGain = outGain;
    }

    void setParameters(float cutoffHz, float resonance) noexcept
    {
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.45f);
        const float gw = std::tan(kPi * fc / sr);
        G = gw / (1.0f + gw);
        const float r = clampf(resonance, 0.0f, 1.0f);
        // A gentle convex law keeps the useful lower half of the control broad,
        // while the final 15% reaches the musical oscillation region.
        feedback = maxFeedback * r * (0.72f + 0.28f * r);
        resonanceAmount = r;
    }

    float process(float input) noexcept
    {
        const float oneMinusG = 1.0f - G;
        const float G2 = G * G;
        const float G3 = G2 * G;
        const float G4 = G2 * G2;
        const float sigma = oneMinusG
            * (G3 * z[0] + G2 * z[1] + G * z[2] + z[3]);

        float u = (input * preGain - feedback * sigma)
                / (1.0f + feedback * G4);
        u = soft(u, inputDrive);

        float x = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = G * (x - z[i]);
            float y = soft(z[i] + v, stageDrive);
            z[i] = y + v;
            if (isBad(z[i]))
            {
                z[i] = 0.0f;
                y = 0.0f;
            }
            x = y;
        }

        // Resonant cascades naturally lose bass.  The compensation is fed from
        // the pre-saturated input and kept deliberately small per model.
        const float raw = (x + input * bassComp * resonanceAmount) * outputGain;
        const float out = raw - dcIn + dcCoeff * dcOut;
        dcIn = raw;
        dcOut = isBad(out) ? 0.0f : out;
        if (isBad(dcIn)) dcIn = 0.0f;
        return dcOut;
    }

    void reset() noexcept
    {
        for (float& state : z) state = 0.0f;
        dcIn = dcOut = 0.0f;
    }

private:
    static float soft(float x, float amount) noexcept
    {
        // Dividing by amount (not tanh(amount)) preserves unity small-signal
        // gain and therefore the calibrated resonance threshold.
        return std::tanh(x * amount) / amount;
    }

    float sr = 44100.0f;
    float G = 0.0f;
    float feedback = 0.0f;
    float resonanceAmount = 0.0f;
    float maxFeedback = 4.2f;
    float preGain = 1.0f;
    float inputDrive = 1.0f;
    float stageDrive = 0.8f;
    float bassComp = 0.0f;
    float outputGain = 1.0f;
    float z[4] = {};
    float dcCoeff = 0.999f;
    float dcIn = 0.0f, dcOut = 0.0f;
};

// Early-80s OTA poly filter. Cleaner stages and a subcritical feedback ceiling
// retain the DCO clarity without crossing into sustained self-oscillation.
class CosmosFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        lpf.prepare(sampleRate);
        hpf.prepare(sampleRate);
        lpf.configure(3.88f, 0.88f, 0.16f, 0.12f, 0.02f, 1.10f);
    }
    void setParameters(float lpCutoff, float lpResonance, float hpCutoff) noexcept
    {
        lpf.setParameters(lpCutoff, lpResonance);
        hpf.setCutoff(hpCutoff);
    }
    float process(float input) noexcept { return lpf.process(hpf.process(input)); }
    void reset() noexcept { lpf.reset(); hpf.reset(); }
private:
    TPTFourPole lpf;
    SimpleHPF hpf;
};

// Late-70s American poly filter.  Hotter input and a slightly supercritical
// feedback range give the strong, pitch-stable self oscillation used by Poly-Mod.
class OracleFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        lpf.prepare(sampleRate);
        lpf.configure(4.46f, 1.08f, 0.90f, 0.72f, 0.10f, 0.96f);
    }
    void setParameters(float cutoffHz, float resonance) noexcept
    {
        lpf.setParameters(cutoffHz, resonance);
    }
    float process(float input) noexcept { return lpf.process(input); }
    void reset() noexcept { lpf.reset(); }
private:
    TPTFourPole lpf;
};

// Driven mono OTA cascade.  The input gain rises with resonance, reproducing the
// compressed/squelchy interaction of a hot mono signal path.
class MonoFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        lpf.prepare(sampleRate);
        configure(0.0f);
    }
    void setParameters(float cutoffHz, float resonance) noexcept
    {
        configure(clampf(resonance, 0.0f, 1.0f));
        lpf.setParameters(cutoffHz, resonance);
    }
    float process(float input) noexcept { return lpf.process(input); }
    void reset() noexcept { lpf.reset(); }
private:
    void configure(float resonance) noexcept
    {
        lpf.configure(4.30f, 1.10f + 1.65f * resonance,
                      1.35f, 1.05f, 0.13f, 0.82f);
    }
    TPTFourPole lpf;
};

// Semi-modular ladder with the two historically important revisions.  Model
// The late revision intentionally stops opening around 12 kHz; the early
// revision remains open to the engine's safe TPT ceiling.
class LadderFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        core.prepare(sampleRate);
        applyModel();
    }

    void setModel(ModularFilterModel newModel) noexcept
    {
        if (model == newModel) return;
        model = newModel;
        applyModel();
    }

    void setParameters(float cutoffHz, float resonance) noexcept
    {
        const float tptCeiling = sr * 0.45f;
        const float ceiling = model == ModularFilterModel::Late
            ? (tptCeiling < 12000.0f ? tptCeiling : 12000.0f)
            : tptCeiling;
        core.setParameters(clampf(cutoffHz, 10.0f, ceiling), resonance);
    }

    float process(float input) noexcept { return core.process(input); }
    void reset() noexcept { core.reset(); }

private:
    void applyModel() noexcept
    {
        if (model == ModularFilterModel::Early)
            core.configure(4.42f, 1.18f, 1.05f, 0.92f, 0.09f, 0.90f);
        else
            core.configure(4.28f, 1.05f, 0.88f, 0.76f, 0.12f, 0.94f);
    }

    float sr = 44100.0f;
    ModularFilterModel model = ModularFilterModel::Early;
    TPTFourPole core;
};

class SynthFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        cosmos.prepare(sampleRate);
        oracle.prepare(sampleRate);
        mono.prepare(sampleRate);
        ladder.prepare(sampleRate);
    }

    void setMode(FilterMode m) noexcept { mode = m; }
    void setModularModel(ModularFilterModel m) noexcept { ladder.setModel(m); }

    // Re-preparing resets only filter memory; oscillator/envelope musical state is
    // retained by SynthVoice during an oversampling-factor change.
    void setSampleRate(double sampleRate) noexcept { prepare(sampleRate); }

    void setParameters(float cutoff, float resonance, float hpCutoff = 20.0f) noexcept
    {
        switch (mode)
        {
            case FilterMode::Cosmos:  cosmos.setParameters(cutoff, resonance, hpCutoff); break;
            case FilterMode::Oracle:  oracle.setParameters(cutoff, resonance); break;
            case FilterMode::Mono:    mono.setParameters(cutoff, resonance); break;
            case FilterMode::Modular: ladder.setParameters(cutoff, resonance); break;
        }
    }

    float process(float input) noexcept
    {
        switch (mode)
        {
            case FilterMode::Cosmos:  return cosmos.process(input);
            case FilterMode::Oracle:  return oracle.process(input);
            case FilterMode::Mono:    return mono.process(input);
            case FilterMode::Modular: return ladder.process(input);
        }
        return input;
    }

    void reset() noexcept
    {
        cosmos.reset();
        oracle.reset();
        mono.reset();
        ladder.reset();
    }

private:
    FilterMode mode = FilterMode::Cosmos;
    CosmosFilter cosmos;
    OracleFilter oracle;
    MonoFilter mono;
    LadderFilter ladder;
};

} // namespace msynth
