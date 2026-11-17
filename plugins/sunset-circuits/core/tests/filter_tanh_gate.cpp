// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// filter_tanh_gate: the TPT four-pole filter sounds the same with its rational tanh (#303).
//
// TPTFourPole's soft() runs five times per sample per voice at the internal rate
// in every mode that uses it (Cosmos, Oracle, Prism, Mono, Modular). It used
// std::tanh; it now uses a rational tanh, which made whole voices 7 to 8 %
// cheaper (measured at engine level, build against build; this gate does not
// time anything, because the filter timed alone is bound by its own feedback
// latency and does not show the saving). LegacyTPT below is a frozen copy of the
// filter as it was with std::tanh.
//
// For each mode's voicing, the same input (a saw under a cutoff sweep, at low
// and near-self-oscillating resonance, 96 kHz) goes through the real filter and
// the frozen copy, and the real filter's output must equal the copy's to
// -100 dB (RMS of the difference re the output). Measured -110 to -132 dB here
// and -125 to -131 dB on factory presets through the whole engine. Control: a
// cruder rational, x (15 + x^2) / (15 + 6 x^2), fails 7 of the 10 rows (-40 to
// -77 dB); only the light Cosmos and Prism input drives cannot tell them apart.

#include "FilterEngine.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using namespace msynth;

// Frozen copy of TPTFourPole with std::tanh in soft(). Do not edit.
class LegacyTPT
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = (float)sampleRate;
        dcCoeff = std::exp(-kTwoPi * 5.0f / sr);
        for (float& s : z) s = 0.0f;
        dcIn = dcOut = 0.0f;
    }
    void configure(float maxFb, float inputGain, float inputSat, float stageSat, float lowComp,
                   float outGain) noexcept
    {
        maxFeedback = maxFb; preGain = inputGain; inputDrive = maxf(0.05f, inputSat);
        stageDrive = maxf(0.0f, stageSat); bassComp = lowComp; outputGain = outGain;
    }
    void setParameters(float cutoffHz, float resonance) noexcept
    {
        const float fc = clampf(cutoffHz, 10.0f, sr * 0.45f);
        const float gw = std::tan(kPi * fc / sr);
        G = gw / (1.0f + gw);
        const float r = clampf(resonance, 0.0f, 1.0f);
        feedback = maxFeedback * r * (0.72f + 0.28f * r);
        resonanceAmount = r;
    }
    float process(float input) noexcept
    {
        const float oneMinusG = 1.0f - G;
        const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const float sigma = oneMinusG * (G3 * z[0] + G2 * z[1] + G * z[2] + z[3]);
        float u = (input * preGain - feedback * sigma) / (1.0f + feedback * G4);
        u = soft(u, inputDrive);
        float x = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = G * (x - z[i]);
            float y = soft(z[i] + v, stageDrive);
            z[i] = y + v;
            if (isBad(z[i])) { z[i] = 0.0f; y = 0.0f; }
            x = y;
        }
        const float raw = (x + input * bassComp * resonanceAmount) * outputGain;
        const float out = raw - dcIn + dcCoeff * dcOut;
        dcIn = raw;
        dcOut = isBad(out) ? 0.0f : out;
        if (isBad(dcIn)) dcIn = 0.0f;
        return dcOut;
    }
private:
    static float soft(float x, float amount) noexcept
    {
        return amount > 0.0f ? std::tanh(x * amount) / amount : x;
    }
    float sr = 44100.0f, G = 0.0f, feedback = 0.0f, resonanceAmount = 0.0f, maxFeedback = 4.2f;
    float preGain = 1.0f, inputDrive = 1.0f, stageDrive = 0.8f, bassComp = 0.0f, outputGain = 1.0f;
    float z[4] = {};
    float dcCoeff = 0.999f, dcIn = 0.0f, dcOut = 0.0f;
};

struct Voicing { const char* name; float fb, in, inSat, stSat, low, out; };
// The constants each mode's filter passes to configure() (FilterEngine.hpp).
const Voicing kVoicings[] = {
    {"Cosmos",  4.40f, 0.88f, 0.16f, 0.00f, 0.02f, 1.10f},
    {"Prism",   3.88f, 0.88f, 0.16f, 0.12f, 0.02f, 1.10f},
    {"Oracle",  4.46f, 1.08f, 0.90f, 0.72f, 0.10f, 0.96f},
    {"Mono",    4.30f, 2.09f, 1.35f, 1.05f, 0.13f, 0.82f},   // input gain at resonance 0.6
    {"Modular", 4.42f, 1.18f, 1.05f, 0.92f, 0.09f, 0.90f},
};

constexpr double kRate = 96000.0;
constexpr int kSamples = 4 * 96000;

std::vector<float> makeInput()
{
    std::vector<float> x((size_t)kSamples);
    double ph = 0.0;
    for (int i = 0; i < kSamples; ++i)
    {
        x[(size_t)i] = (float)(2.0 * ph - 1.0);
        ph += 220.0 / kRate;
        if (ph >= 1.0) ph -= 1.0;
    }
    return x;
}

template <class F>
void render(F& f, const Voicing& v, float res, const std::vector<float>& in, std::vector<float>& out)
{
    f.prepare(kRate);
    f.configure(v.fb, v.in, v.inSat, v.stSat, v.low, v.out);
    for (int i = 0; i < kSamples; ++i)
    {
        if ((i & 31) == 0)   // cutoff sweep 200 Hz .. 12 kHz and back, re-set every 32 samples
        {
            const double t = (double)i / kSamples;
            const double oct = 5.9 * (t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t);
            f.setParameters((float)(200.0 * std::pow(2.0, oct)), res);
        }
        out[(size_t)i] = f.process(in[(size_t)i]);
    }
}

double rms(const std::vector<float>& a)
{
    double s = 0; for (float v : a) s += (double)v * v; return std::sqrt(s / (double)a.size());
}
}

int main()
{
    const std::vector<float> in = makeInput();
    std::vector<float> a((size_t)kSamples), b((size_t)kSamples);
    int fails = 0;

    for (const Voicing& v : kVoicings)
        for (float res : {0.3f, 0.95f})
        {
            TPTFourPole real; LegacyTPT copy;
            render(real, v, res, in, a);
            render(copy, v, res, in, b);
            std::vector<float> d((size_t)kSamples);
            for (size_t i = 0; i < d.size(); ++i) d[i] = a[i] - b[i];
            const double ref = rms(b), diff = rms(d);
            const double db = diff > 0 ? 20.0 * std::log10(diff / ref) : -400.0;
            const bool ok = db <= -100.0;
            fails += !ok;
            std::printf("  sound %-8s res %.2f  difference %8.1f dB re output   %s\n", v.name, res, db,
                        ok ? "PASS" : "FAIL");
        }

    std::printf("filter_tanh_gate: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
