// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0.1 — DuskAmp golden render generator.
//
// Generates a deterministic 30 s synthetic DI through DuskAmpProcessor for
// every entry in kFactoryPresets[] and writes the result to
// tests/golden_renders/<index>_<slug>.wav. The DI is synthesised in-process
// (no external file dependency); both the DI and the renders are reproducible
// across runs as long as no DSP changes alter audio output.
//
// Workflow:
//   1. cmake .. -DBUILD_DUSKAMP_TESTS=ON
//   2. cmake --build build --target duskamp_golden_render -j8
//   3. ./build/tests/duskamp_golden_render/duskamp_golden_render
//
// Golden renders are committed to the repo. After any DSP change, re-run the
// renderer and compare against the golden set. Identical = no regression.
// Different = either a known-good change (regenerate goldens) or a bug.

#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include "SyntheticDI.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <iostream>

namespace
{
    constexpr double kSampleRate   = 48000.0;
    constexpr int    kBlockSize    = 256;
    constexpr int    kDurationSec  = 30;
    constexpr int    kTotalSamples = static_cast<int> (kSampleRate * kDurationSec);

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf)
    {
        file.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! dynamic_cast<juce::FileOutputStream&> (*stream).openedOk())
        {
            std::cerr << "Failed to open " << file.getFullPathName() << std::endl;
            return false;
        }

        const auto options = juce::AudioFormatWriterOptions{}
            .withSampleRate (kSampleRate)
            .withNumChannels (buf.getNumChannels())
            .withBitsPerSample (32)
            .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        // createWriterFor takes the unique_ptr by reference and consumes it on success.
        auto writer = fmt.createWriterFor (stream, options);
        if (writer == nullptr)
        {
            std::cerr << "Failed to create WAV writer for " << file.getFullPathName() << std::endl;
            return false;
        }

        return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    // Filename-safe version of the preset name.
    juce::String slugify (const juce::String& name)
    {
        return name.replace (" ", "_")
                   .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "abcdefghijklmnopqrstuvwxyz0123456789_-");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;

    juce::File outDir;
    if (argc > 1)
        outDir = juce::File (juce::String::fromUTF8 (argv[1]));
    else
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("tests/golden_renders");

    if (! outDir.createDirectory())
    {
        std::cerr << "Failed to create output dir: " << outDir.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "Output: " << outDir.getFullPathName() << std::endl;
    std::cout << "Synthesising DI (" << kDurationSec << " s @ "
              << static_cast<int> (kSampleRate) << " Hz)..." << std::endl;

    const auto di = DuskAmpTest::makeSyntheticDI (kSampleRate, kDurationSec);

    DuskAmpProcessor processor;
    processor.setPlayConfigDetails (2, 2,
                                    kSampleRate,
                                    kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    int rendered = 0;
    for (int p = 0; p < kNumFactoryPresets; ++p)
    {
        const auto& preset = kFactoryPresets[p];

        processor.reset();
        preset.applyTo (processor.parameters);

        // After applying preset, advance a "warm-up" block of zeros so smoothed-value
        // ramps and DSP state settle into the new parameter regime before we
        // capture audio. Otherwise the first ~32 ms would be a parameter ramp.
        {
            juce::AudioBuffer<float> warm (2, kBlockSize);
            warm.clear();
            juce::MidiBuffer midi;
            for (int n = 0; n < 16; ++n) // ~85 ms of warm-up
                processor.processBlock (warm, midi);
        }

        juce::AudioBuffer<float> output (2, kTotalSamples);
        output.clear();

        juce::MidiBuffer midi;
        for (int sampleStart = 0; sampleStart < kTotalSamples; sampleStart += kBlockSize)
        {
            const int blockSamples = std::min (kBlockSize, kTotalSamples - sampleStart);

            juce::AudioBuffer<float> block (2, blockSamples);
            // Mono DI duplicated to both channels (matches what hosts feed)
            block.copyFrom (0, 0, di.getReadPointer (0, sampleStart), blockSamples);
            block.copyFrom (1, 0, di.getReadPointer (0, sampleStart), blockSamples);

            processor.processBlock (block, midi);

            output.copyFrom (0, sampleStart, block, 0, 0, blockSamples);
            output.copyFrom (1, sampleStart, block, 1, 0, blockSamples);
        }

        const auto outFile = outDir.getChildFile (
            juce::String::formatted ("%02d_%s.wav", p, slugify (preset.name).toRawUTF8()));

        if (! writeWav (outFile, output))
            return 2;

        std::cout << "[" << (p + 1) << "/" << kNumFactoryPresets << "] "
                  << preset.name << " -> " << outFile.getFileName() << std::endl;
        ++rendered;
    }

    processor.releaseResources();

    std::cout << std::endl
              << "Rendered " << rendered << "/" << kNumFactoryPresets
              << " presets to " << outDir.getFullPathName() << std::endl;
    return 0;
}
