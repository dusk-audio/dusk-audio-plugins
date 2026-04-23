#include "dsp/DuskVerbEngine.h"
#include "FactoryPresets.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#if defined(__SSE__) || defined(__SSE2__)
 #include <xmmintrin.h>
#endif

#include <cstring>
#include <iostream>
#include <vector>

int main (int argc, char** argv)
{
    // Flush denormals to zero (no ScopedNoDenormals outside of JUCE plugin context)
#if defined(__SSE__) || defined(__SSE2__)
    _mm_setcsr (_mm_getcsr() | 0x8040); // FTZ + DAZ
#endif

    // Args: [algorithm-index] [decay-seconds-override] [output-path]
    // The runtime knob values come from FactoryPresets.h (per-preset),
    // so each preset is rendered with the same settings the plugin user
    // would hear when selecting it. Decay can be overridden via argv[2]
    // for sweeping; pass 0 (or omit) to use the preset's factory decay.
    int   algoIndex     = (argc > 1) ? std::atoi (argv[1]) : 0;
    float decayOverride = (argc > 2) ? static_cast<float> (std::atof (argv[2])) : 0.0f;
    const char* outPath = (argc > 3) ? argv[3] : "ir_test.wav";

    const auto& presets = getFactoryPresets();
    if (algoIndex < 0 || algoIndex >= static_cast<int> (presets.size()))
    {
        std::cerr << "Invalid algorithm index " << algoIndex << std::endl;
        return 1;
    }
    const auto& preset = presets[static_cast<size_t> (algoIndex)];
    float decayTime = (decayOverride > 0.0f) ? decayOverride : preset.decay;

    constexpr double sampleRate  = 48000.0;
    int    numSeconds  = static_cast<int> (decayTime * 1.5f) + 2;
    int    totalFrames = static_cast<int> (sampleRate * numSeconds);
    constexpr int    blockSize   = 512;

    // Stereo buffer with unit impulse at sample 0
    std::vector<float> bufferL (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<float> bufferR (static_cast<size_t> (totalFrames), 0.0f);
    bufferL[0] = 1.0f;
    bufferR[0] = 1.0f;

    // Configure engine using the preset's factory values so the rendered IR
    // mirrors what the user hears when they pick this preset in the UI.
    DuskVerbEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setAlgorithm (algoIndex);
    engine.setDecayTime     (decayTime);
    engine.setBassMultiply  (preset.bassMult);
    engine.setTrebleMultiply(preset.damping);
    engine.setCrossoverFreq (preset.crossover);
    engine.setModDepth      (preset.modDepth);
    engine.setModRate       (preset.modRate);
    engine.setSize          (preset.size);
    engine.setPreDelay      (preset.predelay);
    engine.setDiffusion     (preset.diffusion);
    engine.setOutputDiffusion (0.8f);
    engine.setERLevel       (preset.erLevel);
    engine.setERSize        (preset.erSize);
    engine.setMix           (1.0f);

    // Process in blocks (in-place)
    for (int pos = 0; pos < totalFrames; pos += blockSize)
    {
        int frames = std::min (blockSize, totalFrames - pos);
        engine.process (bufferL.data() + pos, bufferR.data() + pos, frames);
    }

    // Copy to JUCE buffer for WAV writing
    juce::AudioBuffer<float> irBuffer (2, totalFrames);
    std::memcpy (irBuffer.getWritePointer (0), bufferL.data(),
                 static_cast<size_t> (totalFrames) * sizeof (float));
    std::memcpy (irBuffer.getWritePointer (1), bufferR.data(),
                 static_cast<size_t> (totalFrames) * sizeof (float));

    // Write WAV
    juce::File outputFile = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);

    if (outputFile.existsAsFile())
        outputFile.deleteFile();

    juce::WavAudioFormat wavFormat;
    auto fileStream = std::make_unique<juce::FileOutputStream> (outputFile);
    if (fileStream->failedToOpen())
    {
        std::cerr << "Failed to open output file: "
                  << outputFile.getFullPathName() << std::endl;
        return 1;
    }

    std::unique_ptr<juce::OutputStream> outputStream (fileStream.release());
    auto writer = wavFormat.createWriterFor (outputStream,
        juce::AudioFormatWriterOptions{}
            .withSampleRate (sampleRate)
            .withNumChannels (2)
            .withBitsPerSample (24));

    if (writer != nullptr)
    {
        writer->writeFromAudioSampleBuffer (irBuffer, 0, totalFrames);
        writer.reset();

        std::cout << "Wrote " << numSeconds << "s IR to: "
                  << outputFile.getFullPathName() << std::endl;
        return 0;
    }

    std::cerr << "Failed to create output file." << std::endl;
    return 1;
}
