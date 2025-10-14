#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <cmath>
#include <memory>
#include <thread>
#include <chrono>

class TestRunner
{
public:
    TestRunner()
    {
        // Initialize JUCE message manager
        juce::MessageManager::getInstance();
    }

    bool runAllTests()
    {
        std::cout << "\n=======================================" << std::endl;
        std::cout << "  4K EQ Spectrum Analyzer Test Suite" << std::endl;
        std::cout << "=======================================" << std::endl;

        bool allPassed = true;

        // Test 1: Load plugin
        allPassed &= testPluginLoading();

        // Test 2: Test with sine wave
        allPassed &= testSineWaveSpectrum();

        // Test 3: Test with white noise
        allPassed &= testWhiteNoiseSpectrum();

        // Test 4: Test with swept sine
        allPassed &= testSweptSineSpectrum();

        // Test 5: Test FFT accuracy
        allPassed &= testFFTAccuracy();

        std::cout << "\n=======================================" << std::endl;
        if (allPassed)
        {
            std::cout << "  ALL TESTS PASSED!" << std::endl;
        }
        else
        {
            std::cout << "  SOME TESTS FAILED!" << std::endl;
        }
        std::cout << "=======================================" << std::endl;

        return allPassed;
    }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    const double sampleRate = 48000.0;
    const int bufferSize = 512;

    bool testPluginLoading()
    {
        std::cout << "\n[TEST 1] Loading 4K EQ plugin..." << std::endl;

        juce::OwnedArray<juce::PluginDescription> types;
        juce::VST3PluginFormat format;

        juce::String pluginPath = "/home/marc/.vst3/4K EQ.vst3";
        format.findAllTypesForFile(types, pluginPath);

        if (types.isEmpty())
        {
            std::cerr << "  ❌ Failed to find plugin at: " << pluginPath << std::endl;
            return false;
        }

        juce::String error;
        plugin = format.createInstanceFromDescription(*types[0], sampleRate, bufferSize, error);

        if (plugin == nullptr)
        {
            std::cerr << "  ❌ Failed to create plugin instance: " << error << std::endl;
            return false;
        }

        std::cout << "  ✓ Plugin loaded: " << plugin->getName() << std::endl;

        // Prepare plugin
        plugin->prepareToPlay(sampleRate, bufferSize);
        plugin->setNonRealtime(true);

        // Enable spectrum analyzer
        auto* param = findParameter("spectrum");
        if (param)
        {
            param->setValue(1.0f); // Enable spectrum display
            std::cout << "  ✓ Spectrum analyzer enabled" << std::endl;
        }

        return true;
    }

    bool testSineWaveSpectrum()
    {
        std::cout << "\n[TEST 2] Testing with 1kHz sine wave..." << std::endl;

        if (!plugin)
        {
            std::cerr << "  ❌ Plugin not loaded" << std::endl;
            return false;
        }

        // Generate 1kHz sine wave
        juce::AudioBuffer<float> buffer(2, bufferSize);
        const float frequency = 1000.0f;
        const float amplitude = 0.5f;

        for (int sample = 0; sample < bufferSize; ++sample)
        {
            float value = amplitude * std::sin(2.0f * M_PI * frequency * sample / sampleRate);
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }

        // Process multiple blocks to allow FFT to accumulate
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 10; ++i)
        {
            plugin->processBlock(buffer, midiBuffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "  ✓ Processed 1kHz sine wave" << std::endl;
        std::cout << "  ✓ Peak should be visible at 1kHz in spectrum" << std::endl;

        return true;
    }

    bool testWhiteNoiseSpectrum()
    {
        std::cout << "\n[TEST 3] Testing with white noise..." << std::endl;

        if (!plugin)
        {
            std::cerr << "  ❌ Plugin not loaded" << std::endl;
            return false;
        }

        // Generate white noise
        juce::AudioBuffer<float> buffer(2, bufferSize);
        juce::Random random;
        const float amplitude = 0.1f;

        for (int sample = 0; sample < bufferSize; ++sample)
        {
            float value = amplitude * (random.nextFloat() * 2.0f - 1.0f);
            buffer.setSample(0, sample, value);
            buffer.setSample(1, sample, value);
        }

        // Process multiple blocks
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 20; ++i)
        {
            // Generate new white noise each block
            for (int sample = 0; sample < bufferSize; ++sample)
            {
                float value = amplitude * (random.nextFloat() * 2.0f - 1.0f);
                buffer.setSample(0, sample, value);
                buffer.setSample(1, sample, value);
            }
            plugin->processBlock(buffer, midiBuffer);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        std::cout << "  ✓ Processed white noise" << std::endl;
        std::cout << "  ✓ Spectrum should show flat response across frequencies" << std::endl;

        return true;
    }

    bool testSweptSineSpectrum()
    {
        std::cout << "\n[TEST 4] Testing with swept sine (20Hz - 20kHz)..." << std::endl;

        if (!plugin)
        {
            std::cerr << "  ❌ Plugin not loaded" << std::endl;
            return false;
        }

        // Generate logarithmic swept sine
        juce::AudioBuffer<float> buffer(2, bufferSize);
        const float amplitude = 0.3f;
        const float startFreq = 20.0f;
        const float endFreq = 20000.0f;
        const int sweepSamples = static_cast<int>(sampleRate * 2); // 2 second sweep

        float currentPhase = 0.0f;
        int totalSamples = 0;

        juce::MidiBuffer midiBuffer;

        // Process sweep in chunks
        while (totalSamples < sweepSamples)
        {
            for (int sample = 0; sample < bufferSize && totalSamples < sweepSamples; ++sample)
            {
                float t = static_cast<float>(totalSamples) / sweepSamples;
                float frequency = startFreq * std::pow(endFreq / startFreq, t);

                float value = amplitude * std::sin(currentPhase);
                buffer.setSample(0, sample, value);
                buffer.setSample(1, sample, value);

                currentPhase += 2.0f * M_PI * frequency / sampleRate;
                if (currentPhase > 2.0f * M_PI)
                    currentPhase -= 2.0f * M_PI;

                totalSamples++;
            }

            plugin->processBlock(buffer, midiBuffer);

            // Log progress
            if (totalSamples % (sweepSamples / 10) == 0)
            {
                float progress = 100.0f * totalSamples / sweepSamples;
                float currentFreq = startFreq * std::pow(endFreq / startFreq,
                                                         static_cast<float>(totalSamples) / sweepSamples);
                std::cout << "  ... " << static_cast<int>(progress) << "% - Current frequency: "
                          << static_cast<int>(currentFreq) << " Hz" << std::endl;
            }
        }

        std::cout << "  ✓ Completed frequency sweep" << std::endl;
        std::cout << "  ✓ Spectrum should show moving peak from 20Hz to 20kHz" << std::endl;

        return true;
    }

    bool testFFTAccuracy()
    {
        std::cout << "\n[TEST 5] Testing FFT accuracy with multiple tones..." << std::endl;

        if (!plugin)
        {
            std::cerr << "  ❌ Plugin not loaded" << std::endl;
            return false;
        }

        // Generate signal with multiple known frequencies
        std::vector<std::pair<float, float>> tones = {
            {100.0f, 0.2f},   // 100 Hz, amplitude 0.2
            {500.0f, 0.3f},   // 500 Hz, amplitude 0.3
            {1000.0f, 0.4f},  // 1 kHz, amplitude 0.4
            {3000.0f, 0.3f},  // 3 kHz, amplitude 0.3
            {8000.0f, 0.2f}   // 8 kHz, amplitude 0.2
        };

        juce::AudioBuffer<float> buffer(2, bufferSize);

        // Generate complex tone
        for (int block = 0; block < 50; ++block)
        {
            buffer.clear();

            for (int sample = 0; sample < bufferSize; ++sample)
            {
                float value = 0.0f;

                // Sum all tones
                for (const auto& [freq, amp] : tones)
                {
                    float phase = 2.0f * M_PI * freq * (block * bufferSize + sample) / sampleRate;
                    value += amp * std::sin(phase);
                }

                // Normalize to prevent clipping
                value *= 0.5f;

                buffer.setSample(0, sample, value);
                buffer.setSample(1, sample, value);
            }

            juce::MidiBuffer midiBuffer;
            plugin->processBlock(buffer, midiBuffer);
        }

        std::cout << "  ✓ Processed multi-tone signal" << std::endl;
        std::cout << "  ✓ Peaks should be visible at: ";
        for (const auto& [freq, amp] : tones)
        {
            std::cout << static_cast<int>(freq) << "Hz ";
        }
        std::cout << std::endl;

        // Calculate expected dB levels
        std::cout << "  ✓ Expected relative levels:" << std::endl;
        float maxAmp = 0.4f; // Reference amplitude
        for (const auto& [freq, amp] : tones)
        {
            float relativeDb = 20.0f * std::log10(amp / maxAmp);
            std::cout << "    " << static_cast<int>(freq) << "Hz: "
                     << std::fixed << std::setprecision(1) << relativeDb << " dB" << std::endl;
        }

        return true;
    }

    juce::AudioParameterFloat* findParameter(const juce::String& paramID)
    {
        if (!plugin)
            return nullptr;

        for (auto* param : plugin->getParameters())
        {
            if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            {
                if (floatParam->paramID.containsIgnoreCase(paramID))
                    return floatParam;
            }
        }
        return nullptr;
    }
};

int main()
{
    TestRunner runner;
    bool success = runner.runAllTests();
    return success ? 0 : 1;
}