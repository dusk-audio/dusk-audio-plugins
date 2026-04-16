// dsp_test_harness.cpp — Standalone test harness for DuskAmp DSP validation
//
// Processes test signals through the DuskAmp DSP chain and writes output WAVs.
// Links directly against the DSP classes, bypassing JUCE plugin infrastructure.
//
// Build: cmake adds this as a target (DuskAmp_DSPTest)
// Run:   ./DuskAmp_DSPTest <test_signals_dir> <output_dir>
//
// Generates WAV files for each amp type × drive level × test signal combination.
// The Python validation script reads these WAV files for analysis.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "ToneStack.h"
#include "PreampDSP.h"
#include "PowerAmp.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

static constexpr double kSampleRate = 44100.0;

// ============================================================================
// WAV I/O helpers (using JUCE)
// ============================================================================

static std::vector<float> readWav (const juce::File& file, double& sampleRate)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (
        formatManager.createReaderFor (file));

    if (! reader)
    {
        std::cerr << "  Cannot read: " << file.getFullPathName() << std::endl;
        return {};
    }

    sampleRate = reader->sampleRate;
    int numSamples = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (1, numSamples);
    reader->read (&buffer, 0, numSamples, 0, true, false); // Read left channel only

    std::vector<float> data (static_cast<size_t> (numSamples));
    std::copy (buffer.getReadPointer (0),
               buffer.getReadPointer (0) + numSamples,
               data.begin());
    return data;
}

static void writeWav (const juce::File& file, const std::vector<float>& data, double sampleRate)
{
    // Delete existing file first (JUCE FileOutputStream appends otherwise)
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (new juce::FileOutputStream (file),
                             sampleRate, 1, 32, {}, 0));

    if (! writer)
    {
        std::cerr << "  Cannot write: " << file.getFullPathName() << std::endl;
        return;
    }

    juce::AudioBuffer<float> buffer (1, static_cast<int> (data.size()));
    std::copy (data.begin(), data.end(), buffer.getWritePointer (0));
    writer->writeFromAudioSampleBuffer (buffer, 0, static_cast<int> (data.size()));
}

// ============================================================================
// Generate test signals (same as Python version)
// ============================================================================

static std::vector<float> generateSineSweep (double sr, double duration = 4.0,
                                              double fStart = 80.0, double fEnd = 4000.0,
                                              float amplitude = 0.3f)
{
    int n = static_cast<int> (sr * duration);
    std::vector<float> signal (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        double t = static_cast<double> (i) / sr;
        double phase = 2.0 * juce::MathConstants<double>::pi * fStart * duration
                       / std::log (fEnd / fStart)
                       * (std::exp (t / duration * std::log (fEnd / fStart)) - 1.0);
        signal[static_cast<size_t> (i)] = amplitude * static_cast<float> (std::sin (phase));
    }
    // Fade in/out
    int fade = static_cast<int> (0.01 * sr);
    for (int i = 0; i < fade; ++i)
    {
        float g = static_cast<float> (i) / static_cast<float> (fade);
        signal[static_cast<size_t> (i)] *= g;
        signal[static_cast<size_t> (n - 1 - i)] *= g;
    }
    return signal;
}

static std::vector<float> generateTestTone (double sr, double fundamental,
                                             float amplitude, double duration = 2.0)
{
    int n = static_cast<int> (sr * duration);
    std::vector<float> signal (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        double t = static_cast<double> (i) / sr;
        signal[static_cast<size_t> (i)] = amplitude * static_cast<float> (
            std::sin (2.0 * juce::MathConstants<double>::pi * fundamental * t));
    }
    // Fade in
    int fade = static_cast<int> (0.1 * sr);
    for (int i = 0; i < fade; ++i)
        signal[static_cast<size_t> (i)] *= static_cast<float> (i) / static_cast<float> (fade);
    return signal;
}

static std::vector<float> generateImpulse (double sr, double duration = 2.0)
{
    int n = static_cast<int> (sr * duration);
    std::vector<float> signal (static_cast<size_t> (n), 0.0f);
    signal[100] = 1.0f; // Impulse at sample 100
    return signal;
}

static std::vector<float> generateDynamicSweep (double sr, double fundamental = 110.0,
                                                  double duration = 6.0)
{
    int n = static_cast<int> (sr * duration);
    std::vector<float> signal (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        double t = static_cast<double> (i) / sr;
        double ampDb = -40.0 + 40.0 * t / duration;
        double amp = std::pow (10.0, ampDb / 20.0) * 0.5;
        signal[static_cast<size_t> (i)] = static_cast<float> (
            amp * std::sin (2.0 * juce::MathConstants<double>::pi * fundamental * t));
    }
    int fade = static_cast<int> (0.02 * sr);
    for (int i = 0; i < fade; ++i)
    {
        float g = static_cast<float> (i) / static_cast<float> (fade);
        signal[static_cast<size_t> (i)] *= g;
        signal[static_cast<size_t> (n - 1 - i)] *= g;
    }
    return signal;
}

static std::vector<float> generateSagBurst (double sr)
{
    double burstDur = 0.5, quietDur = 1.5;
    int n = static_cast<int> (sr * (burstDur + quietDur));
    int burstEnd = static_cast<int> (sr * burstDur);
    int refStart = burstEnd + static_cast<int> (0.01 * sr);
    std::vector<float> signal (static_cast<size_t> (n), 0.0f);

    // Loud chord burst
    double freqs[] = { 82.4, 110.0, 164.8, 220.0 };
    for (int i = 0; i < burstEnd; ++i)
    {
        double t = static_cast<double> (i) / sr;
        for (auto f : freqs)
            signal[static_cast<size_t> (i)] += 0.3f * static_cast<float> (
                std::sin (2.0 * juce::MathConstants<double>::pi * f * t));
    }
    // Quiet reference tone after burst
    for (int i = refStart; i < n; ++i)
    {
        double t = static_cast<double> (i) / sr;
        signal[static_cast<size_t> (i)] += 0.05f * static_cast<float> (
            std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t));
    }
    // Fade in burst
    int fade = static_cast<int> (0.005 * sr);
    for (int i = 0; i < fade; ++i)
        signal[static_cast<size_t> (i)] *= static_cast<float> (i) / static_cast<float> (fade);

    return signal;
}

// ============================================================================
// Process through DSP chain
// ============================================================================

struct AmpConfig
{
    const char* name;
    ToneStack::Type toneType;
    PreampDSP::Channel preampChannel;
    PowerAmp::AmpType powerAmpType;
    float preampGain;
};

static const AmpConfig kAmps[] = {
    { "Fender",   ToneStack::Type::American, PreampDSP::Channel::Clean,  PowerAmp::AmpType::Fender,   0.3f },
    { "Marshall",  ToneStack::Type::British,  PreampDSP::Channel::Crunch, PowerAmp::AmpType::Marshall, 0.4f },
    { "Vox",      ToneStack::Type::AC,       PreampDSP::Channel::Clean,  PowerAmp::AmpType::Vox,      0.35f },
};

struct DriveConfig
{
    const char* name;
    float preampDrive;
    float powerDrive;
};

static const DriveConfig kDrives[] = {
    { "clean",   0.20f, 0.20f },
    { "crunch",  0.55f, 0.50f },
    { "cranked", 0.85f, 0.75f },
};

static std::vector<float> processThroughChain (
    const std::vector<float>& input, double sr,
    const AmpConfig& amp, const DriveConfig& drive)
{
    PreampDSP preamp;
    ToneStack toneStack;
    PowerAmp powerAmp;

    preamp.prepare (sr);
    toneStack.prepare (sr);
    powerAmp.prepare (sr);

    preamp.setChannel (amp.preampChannel);
    preamp.setGain (drive.preampDrive);
    preamp.setBright (false);

    toneStack.setType (amp.toneType);
    toneStack.setBass (0.5f);
    toneStack.setMid (0.5f);
    toneStack.setTreble (0.5f);

    powerAmp.setAmpType (amp.powerAmpType);
    powerAmp.setDrive (drive.powerDrive);
    powerAmp.setPresence (0.5f);
    powerAmp.setResonance (0.5f);
    powerAmp.setSag (0.5f);

    // Copy input to working buffer
    std::vector<float> buffer (input);

    // Process in blocks of 512
    constexpr int blockSize = 512;
    int numSamples = static_cast<int> (buffer.size());

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        int thisBlock = std::min (blockSize, numSamples - offset);
        float* ptr = buffer.data() + offset;

        preamp.process (ptr, thisBlock);
        toneStack.process (ptr, thisBlock);
        powerAmp.process (ptr, thisBlock);
    }

    return buffer;
}

// Process tone stack only (for tone stack validation)
static std::vector<float> processToneStackOnly (
    const std::vector<float>& input, double sr,
    ToneStack::Type type, float bass = 0.5f, float mid = 0.5f, float treble = 0.5f)
{
    ToneStack toneStack;
    toneStack.prepare (sr);
    toneStack.setType (type);
    toneStack.setBass (bass);
    toneStack.setMid (mid);
    toneStack.setTreble (treble);

    std::vector<float> buffer (input);
    constexpr int blockSize = 512;
    int numSamples = static_cast<int> (buffer.size());

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        int thisBlock = std::min (blockSize, numSamples - offset);
        toneStack.process (buffer.data() + offset, thisBlock);
    }
    return buffer;
}

// ============================================================================
// Main
// ============================================================================

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    std::string outputDir = "dsp_test_output";
    if (argc > 1) outputDir = argv[1];

    fs::create_directories (outputDir);

    double sr = kSampleRate;

    std::cout << "DuskAmp DSP Test Harness" << std::endl;
    std::cout << "Sample rate: " << sr << " Hz" << std::endl;
    std::cout << "Output dir: " << outputDir << std::endl << std::endl;

    // --- 1. Tone stack impulse responses ---
    std::cout << "=== TONE STACK IMPULSE RESPONSES ===" << std::endl;
    {
        auto impulse = generateImpulse (sr);
        const char* typeNames[] = { "American", "British", "AC" };
        ToneStack::Type types[] = { ToneStack::Type::American, ToneStack::Type::British, ToneStack::Type::AC };

        for (int i = 0; i < 3; ++i)
        {
            auto ir = processToneStackOnly (impulse, sr, types[i]);
            std::string filename = outputDir + "/tonestack_ir_" + typeNames[i] + ".wav";
            writeWav (juce::File (filename), ir, sr);
            std::cout << "  " << typeNames[i] << " -> " << filename << std::endl;
        }
    }

    // --- 2. THD test tones at various drive levels ---
    std::cout << std::endl << "=== THD TEST TONES ===" << std::endl;
    {
        // Guitar DI levels: whisper ~-30dBFS, normal ~-12dBFS, loud ~-3dBFS
        float inputLevels[] = { 0.03f, 0.25f, 0.7f };
        const char* levelNames[] = { "whisper", "normal", "loud" };

        for (const auto& amp : kAmps)
        {
            for (const auto& drive : kDrives)
            {
                for (int li = 0; li < 3; ++li)
                {
                    auto tone = generateTestTone (sr, 440.0, inputLevels[li]);
                    auto output = processThroughChain (tone, sr, amp, drive);

                    std::string filename = outputDir + "/thd_" + amp.name + "_"
                                         + drive.name + "_" + levelNames[li] + ".wav";
                    writeWav (juce::File (filename), output, sr);
                }
                std::cout << "  " << amp.name << " " << drive.name << std::endl;
            }
        }
    }

    // --- 3. Compression curves ---
    std::cout << std::endl << "=== COMPRESSION CURVES ===" << std::endl;
    {
        for (const auto& amp : kAmps)
        {
            DriveConfig midDrive = { "mid", 0.5f, 0.5f };
            std::string filename = outputDir + "/compression_" + std::string(amp.name) + ".csv";

            std::ofstream csv (filename);
            csv << "input_db,output_db" << std::endl;

            for (int db = -40; db <= 0; db += 2)
            {
                float level = std::pow (10.0f, static_cast<float> (db) / 20.0f);
                auto tone = generateTestTone (sr, 220.0, level, 0.5);
                auto output = processThroughChain (tone, sr, amp, midDrive);

                // Measure RMS of steady-state portion
                int startSample = static_cast<int> (0.2 * sr);
                float rms = 0.0f;
                int count = 0;
                for (size_t j = static_cast<size_t> (startSample); j < output.size(); ++j)
                {
                    rms += output[j] * output[j];
                    ++count;
                }
                rms = std::sqrt (rms / static_cast<float> (count) + 1e-20f);
                float outDb = 20.0f * std::log10 (rms + 1e-20f);

                csv << db << "," << outDb << std::endl;
            }
            std::cout << "  " << amp.name << " -> " << filename << std::endl;
        }
    }

    // --- 4. Sag recovery (clean + cranked) ---
    std::cout << std::endl << "=== SAG RECOVERY ===" << std::endl;
    {
        auto burst = generateSagBurst (sr);

        // Clean drive sag test
        for (const auto& amp : kAmps)
        {
            DriveConfig sagDrive = { "sag", 0.6f, 0.6f };
            auto output = processThroughChain (burst, sr, amp, sagDrive);
            std::string filename = outputDir + "/sag_" + std::string(amp.name) + ".wav";
            writeWav (juce::File (filename), output, sr);
            std::cout << "  " << amp.name << " (clean) -> " << filename << std::endl;
        }

        // Cranked drive sag test — use Crunch channel for more signal through preamp
        AmpConfig crankedAmps[] = {
            { "Fender",   ToneStack::Type::American, PreampDSP::Channel::Crunch, PowerAmp::AmpType::Fender,   0.4f },
            { "Marshall",  ToneStack::Type::British,  PreampDSP::Channel::Crunch, PowerAmp::AmpType::Marshall, 0.4f },
            { "Vox",      ToneStack::Type::AC,       PreampDSP::Channel::Crunch, PowerAmp::AmpType::Vox,      0.35f },
        };
        for (const auto& amp : crankedAmps)
        {
            DriveConfig crankedDrive = { "cranked", 0.85f, 0.75f };
            auto output = processThroughChain (burst, sr, amp, crankedDrive);
            std::string filename = outputDir + "/sag_" + std::string(amp.name) + "_cranked.wav";
            writeWav (juce::File (filename), output, sr);
            std::cout << "  " << amp.name << " (cranked) -> " << filename << std::endl;
        }
    }

    // --- 5. Full chain with sweep (for spectral analysis) ---
    std::cout << std::endl << "=== FULL CHAIN SWEEPS ===" << std::endl;
    {
        auto sweep = generateSineSweep (sr);
        for (const auto& amp : kAmps)
        {
            for (const auto& drive : kDrives)
            {
                auto output = processThroughChain (sweep, sr, amp, drive);
                std::string filename = outputDir + "/sweep_" + amp.name + "_" + drive.name + ".wav";
                writeWav (juce::File (filename), output, sr);
            }
            std::cout << "  " << amp.name << std::endl;
        }
    }

    // --- 6. Dynamic sweep (for compression analysis) ---
    std::cout << std::endl << "=== DYNAMIC SWEEPS ===" << std::endl;
    {
        auto dynSweep = generateDynamicSweep (sr);
        for (const auto& amp : kAmps)
        {
            DriveConfig midDrive = { "mid", 0.5f, 0.5f };
            auto output = processThroughChain (dynSweep, sr, amp, midDrive);
            std::string filename = outputDir + "/dynamic_" + std::string(amp.name) + ".wav";
            writeWav (juce::File (filename), output, sr);
            std::cout << "  " << amp.name << std::endl;
        }
    }

    // --- 7. Power amp only (for even/odd harmonic isolation) ---
    std::cout << std::endl << "=== POWER AMP ONLY (even/odd test) ===" << std::endl;
    {
        // Send a pure 440Hz tone directly into the power amp.
        // Level calibrated so the signal reaches the waveshaper's transition region
        // (where nonlinear behavior and even/odd harmonic character are most apparent).
        // Too high → fully saturated → both halves clip identically → no asymmetry.
        // Too low → linear → no harmonics at all.
        float testLevel = 0.02f;  // Moderate level for the high-gain stageGain_ in the PA
        auto tone = generateTestTone (sr, 440.0, testLevel);

        struct PAConfig { const char* name; PowerAmp::AmpType type; float drive; };
        PAConfig paConfigs[] = {
            { "Fender_PA",  PowerAmp::AmpType::Fender,  0.5f },
            { "Marshall_PA", PowerAmp::AmpType::Marshall, 0.5f },
            { "Vox_PA",     PowerAmp::AmpType::Vox,     0.7f }, // Higher drive to push into nonlinear region
        };

        for (const auto& pa : paConfigs)
        {
            PowerAmp powerAmp;
            powerAmp.prepare (sr);
            powerAmp.setAmpType (pa.type);
            powerAmp.setDrive (pa.drive);
            powerAmp.setPresence (0.5f);
            powerAmp.setResonance (0.5f);
            powerAmp.setSag (0.3f);

            std::vector<float> buffer (tone);
            constexpr int blockSize = 512;
            int numSamples = static_cast<int> (buffer.size());
            for (int offset = 0; offset < numSamples; offset += blockSize)
            {
                int thisBlock = std::min (blockSize, numSamples - offset);
                powerAmp.process (buffer.data() + offset, thisBlock);
            }

            std::string filename = outputDir + "/poweramp_only_" + std::string(pa.name) + ".wav";
            writeWav (juce::File (filename), buffer, sr);
            std::cout << "  " << pa.name << " -> " << filename << std::endl;
        }
    }

    std::cout << std::endl << "Done! Output in: " << outputDir << std::endl;
    return 0;
}
