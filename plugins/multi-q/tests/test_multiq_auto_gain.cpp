// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: auto-gain must also undo compensation when the EQ returns to unity.
#if defined(MULTIQ_TEST_JUCE)
#include "../MultiQ.h"
#else
#include "../core/MultiQDSP.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

class Processor
{
public:
    Processor(double sr, int channels)
    {
#if defined(MULTIQ_TEST_JUCE)
        auto layout = dsp->getBusesLayout();
        const auto bus = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.set(0, bus); layout.outputBuses.set(0, bus);
        check(dsp->setBusesLayout(layout), "JUCE test really uses the requested mono/stereo layout");
        set(ParamIDs::bandEnabled(1), 0.0f);
        set(ParamIDs::bandEnabled(8), 0.0f);
        dsp->prepareToPlay(sr, 512);
#else
        (void)channels;
        params.bandEnabled[0] = params.bandEnabled[7] = false;
        dsp->prepare(sr, 512);
#endif
    }
    void setGain(float gain, bool enabled)
    {
#if defined(MULTIQ_TEST_JUCE)
        set(ParamIDs::masterGain, gain);
        set(ParamIDs::autoGainEnabled, enabled ? 1.0f : 0.0f);
#else
        params.masterGain = gain;
        params.autoGainEnabled = enabled;
#endif
    }
    void process(float* left, float* right, int channels, int frames)
    {
        float* out[] = {left, right};
#if defined(MULTIQ_TEST_JUCE)
        juce::AudioBuffer<float> buffer(out, channels, frames);
        juce::MidiBuffer midi;
        dsp->processBlock(buffer, midi);
#else
        const float* in[] = {left, right};
        dsp->process(in, out, channels, frames, params);
#endif
    }
private:
#if defined(MULTIQ_TEST_JUCE)
    std::unique_ptr<MultiQ> dsp = std::make_unique<MultiQ>();
    void set(const juce::String& id, float value)
    {
        auto* parameter = dsp->parameters.getParameter(id);
        if (!parameter) { check(false, "requested JUCE parameter exists"); return; }
        parameter->setValueNotifyingHost(dsp->parameters.getParameterRange(id).convertTo0to1(value));
    }
#else
    std::unique_ptr<duskaudio::MultiQDSP> dsp = std::make_unique<duskaudio::MultiQDSP>();
    duskaudio::MultiQDSP::Params params;
#endif
};

void atRate(int sr, int channels)
{
    Processor processor(sr, channels);
    std::mt19937 rng(148);
    std::uniform_real_distribution<float> noise(-0.125f, 0.125f);
    std::array<float, 512> left{}, right{};
    auto measure = [&](float master, bool enabled, double expected, const char* message)
    {
        processor.setGain(master, enabled);
        double inputEnergy = 0.0, outputEnergy = 0.0;
        bool finite = true;
        // Two-second measurement window, then a two-second ramp. Eight seconds
        // also lets a mixed transition window retire before the final reading.
        for (int pos = 0; pos < sr * 8; pos += 512)
        {
            const int frames = std::min(512, sr * 8 - pos);
            for (int i = 0; i < frames; ++i)
            {
                left[i] = right[i] = noise(rng);
                if (pos >= sr * 7) inputEnergy += double(left[i]) * left[i] * channels;
            }
            processor.process(left.data(), right.data(), channels, frames);
            for (int i = 0; i < frames; ++i)
            {
                finite &= std::isfinite(left[i]) && (channels == 1 || std::isfinite(right[i]));
                if (pos >= sr * 7)
                {
                    outputEnergy += double(left[i]) * left[i];
                    if (channels == 2) outputEnergy += double(right[i]) * right[i];
                }
            }
        }
        check(finite && inputEnergy > 1.0 && outputEnergy > 1.0,
              "auto-gain measurement contains finite, excited input and output");
        const double gain = 10.0 * std::log10(outputEnergy / inputEnergy);
        std::printf("Auto-gain: sr=%d channels=%d master=%+.1f enabled=%d settled=%+.6f expected=%+.6f dB\n",
                    sr, channels, master, enabled, gain, expected);
        check(std::isfinite(gain) && std::abs(gain - expected) < 0.03, message);
    };
    for (float master : {6.0f, -6.0f, 12.0f, -12.0f})
    {
        const double expected = master > 6 ? master + 20.0 * std::log10(0.5)
            : master < -6 ? master + 20.0 * std::log10(2.0) : 0.0;
        measure(master, true, expected, "auto-gain compensates an active gain change within its existing cap");
        measure(0.0f, true, 0.0, "returning to neutral releases the preceding gain compensation");
    }
    measure(3.0f, false, 3.0, "disabled auto-gain preserves the requested master gain");
    measure(0.0f, true, 0.0, "re-enabling auto-gain starts from unity after disable");
}
}

int main()
{
#if defined(MULTIQ_TEST_JUCE)
    juce::ScopedJuceInitialiser_GUI init;
#endif
    for (int sr : {44100, 48000, 96000})
        for (int channels : {1, 2}) atRate(sr, channels);
    std::printf("Multi-Q auto-gain: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
