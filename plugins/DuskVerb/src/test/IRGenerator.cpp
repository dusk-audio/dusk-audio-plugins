#include "dsp/DuskVerbEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#if defined(__SSE__) || defined(__SSE2__)
 #include <xmmintrin.h>
#endif

#include <cstring>
#include <iostream>
#include <vector>

int main()
{
    // Flush denormals to zero (no ScopedNoDenormals outside of JUCE plugin context)
#if defined(__SSE__) || defined(__SSE2__)
    _mm_setcsr (_mm_getcsr() | 0x8040); // FTZ + DAZ
#endif

    constexpr double sampleRate  = 48000.0;
    constexpr int    numSeconds  = 3;
    constexpr int    totalFrames = static_cast<int> (sampleRate * numSeconds);
    constexpr int    blockSize   = 512;

    // Stereo buffer with unit impulse at sample 0
    std::vector<float> bufferL (static_cast<size_t> (totalFrames), 0.0f);
    std::vector<float> bufferR (static_cast<size_t> (totalFrames), 0.0f);
    bufferL[0] = 1.0f;
    bufferR[0] = 1.0f;

    // Configure full reverb engine
    DuskVerbEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setAlgorithm (1); // Explicitly select Hall
    engine.setDecayTime (2.5f);
    engine.setBassMultiply (1.2f);
    engine.setTrebleMultiply (0.6f);
    engine.setCrossoverFreq (1000.0f);
    engine.setModDepth (0.3f);
    engine.setModRate (1.0f);
    engine.setSize (0.85f);
    engine.setPreDelay (20.0f);
    engine.setDiffusion (0.7f);
    engine.setOutputDiffusion (0.8f);
    engine.setERLevel (0.5f);
    engine.setERSize (0.5f);
    engine.setMix (1.0f);

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
    juce::File outputFile = juce::File::getCurrentWorkingDirectory().getChildFile ("ir_test.wav");

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
