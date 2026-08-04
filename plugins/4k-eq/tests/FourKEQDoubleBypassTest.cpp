#include "FourKEQ.h"

#include <array>
#include <cmath>
#include <iostream>

int main()
{
    FourKEQ processor;
    processor.prepareToPlay(48000.0, 8);

    auto* bypass = processor.parameters.getParameter("bypass");
    if (bypass == nullptr)
    {
        std::cerr << "bypass parameter is unavailable\n";
        return 1;
    }
    bypass->setValueNotifyingHost(1.0f);

    constexpr int numSamples = 8;
    const std::array<double, numSamples> source{
        std::nextafter(0.1, 1.0),
        std::nextafter(-0.1, -1.0),
        std::nextafter(1.0, 2.0),
        std::nextafter(-1.0, -2.0),
        1.0 / 3.0,
        -1.0 / 7.0,
        0x1.0000000000001p-20,
        -0x1.0000000000001p-20 };

    juce::AudioBuffer<double> buffer(2, numSamples);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            buffer.setSample(channel, sample,
                             source[static_cast<size_t>(sample)]);

    processor.inputLevelL.store(-12.0f);
    processor.outputLevelL.store(-18.0f);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            if (buffer.getSample(channel, sample)
                != source[static_cast<size_t>(sample)])
            {
                std::cerr << "double-precision bypass altered sample "
                          << sample << " on channel " << channel << '\n';
                return 1;
            }

    if (processor.inputLevelL.load() != -12.0f
        || processor.outputLevelL.load() != -18.0f)
    {
        std::cerr << "double-precision bypass published meter values\n";
        return 1;
    }
    if (processor.getLatencySamples() != 0)
    {
        std::cerr << "double-precision bypass did not clear latency\n";
        return 1;
    }

    std::cout << "4K EQ double-precision bypass test passed\n";
    return 0;
}
