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

// Adapted to main's API (2026-05-05): the old PreampDSP/ToneStack pair was
// replaced by PreampModel (factory + per-amp concrete classes) and
// ToneStackModel (Yeh/Smith bilinear). PowerAmp's AmpType enum now lives in
// PreampModel.h.
//
// The full-chain processing path now mirrors production DuskAmpEngine: the
// preamp / tone stack / power amp run at 4x oversampled rate via the shared
// OversamplingManager, with up/down conversion at block boundaries. Without
// this the waveshapers in the preamp + power amp alias heavily at 44.1kHz
// and the THD/even-odd numbers come out as nonsense.
#include "ToneStackModel.h"
#include "PreampModel.h"
#include "PowerAmp.h"
#include "Oversampling.h"

static constexpr int kOversamplingFactor = 4;  // matches production "high quality" default

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
    ToneStackModel::Topology toneTopology;
    AmpType                  ampType;   // shared by PreampModel + PowerAmp (defined in PreampModel.h)
    float                    preampGain;
};

static const AmpConfig kAmps[] = {
    { "Fender",   ToneStackModel::Topology::Fender,   AmpType::Fender,   0.3f  },
    { "Marshall", ToneStackModel::Topology::Marshall, AmpType::Marshall, 0.4f  },
    { "Vox",      ToneStackModel::Topology::Vox,      AmpType::Vox,      0.35f },
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
    const AmpConfig& amp, const DriveConfig& drive,
    float sagAmount = 0.5f)
{
    constexpr int blockSize  = 512;
    const int     numSamples = static_cast<int> (input.size());

    // Oversampling wrapper — preamp/tone stack/power amp all run at the
    // oversampled rate to match production behaviour and avoid aliasing.
    DuskAudio::OversamplingManager oversampling;
    oversampling.setFactor (kOversamplingFactor);
    oversampling.prepare (sr, blockSize, /*numChannels*/ 1);
    const double osRate = oversampling.getOversampledSampleRate();

    auto           preamp = PreampModel::create (amp.ampType);
    ToneStackModel toneStack;
    PowerAmp       powerAmp;

    preamp->prepare (osRate);
    toneStack.prepare (osRate);
    powerAmp.prepare (osRate);

    preamp->setGain (drive.preampDrive);
    preamp->setBright (false);

    toneStack.setTopology (amp.toneTopology);
    toneStack.setBass (0.5f);
    toneStack.setMid (0.5f);
    toneStack.setTreble (0.5f);

    powerAmp.setAmpType (amp.ampType);
    powerAmp.setDrive (drive.powerDrive);
    powerAmp.setPresence (0.5f);
    powerAmp.setResonance (0.5f);
    powerAmp.setSag (sagAmount);

    // Working buffer for the up/downsample round-trip. JUCE oversampling
    // requires an AudioBlock backed by mutable storage that survives
    // between processSamplesUp and processSamplesDown.
    juce::AudioBuffer<float> osBuf (1, blockSize);

    std::vector<float> output (input);

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        const int thisBlock = std::min (blockSize, numSamples - offset);

        // Copy this block into the oversampling buffer.
        std::copy (output.data() + offset,
                   output.data() + offset + thisBlock,
                   osBuf.getWritePointer (0));

        juce::dsp::AudioBlock<float> baseBlock (osBuf.getArrayOfWritePointers(),
                                                 1, static_cast<size_t> (thisBlock));
        auto upBlock = oversampling.processSamplesUp (baseBlock);

        const int   upN  = static_cast<int> (upBlock.getNumSamples());
        float* const upP = upBlock.getChannelPointer (0);

        preamp->process    (upP, upN);
        toneStack.process  (upP, upN);
        powerAmp.process   (upP, upN);

        oversampling.processSamplesDown (baseBlock);

        std::copy (osBuf.getReadPointer (0),
                   osBuf.getReadPointer (0) + thisBlock,
                   output.data() + offset);
    }

    return output;
}

// Process tone stack only (for tone stack validation)
static std::vector<float> processToneStackOnly (
    const std::vector<float>& input, double sr,
    ToneStackModel::Topology topology,
    float bass = 0.5f, float mid = 0.5f, float treble = 0.5f)
{
    ToneStackModel toneStack;
    toneStack.prepare (sr);
    toneStack.setTopology (topology);
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
        const char* typeNames[] = { "Fender", "Marshall", "Vox" };
        ToneStackModel::Topology types[] = {
            ToneStackModel::Topology::Fender,
            ToneStackModel::Topology::Marshall,
            ToneStackModel::Topology::Vox
        };

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

        // Hotter burst for the cranked-sag stress test below — the plain
        // generateSagBurst peaks at ~1.2 and after preamp+tone stack mid
        // attenuation feeds the power amp at ~0.3 level, which never builds
        // the sag envelope toward 1.0. Boost amplitude per partial to
        // reach near-rail levels at the power amp input.
        auto stressBurst = burst;
        for (auto& s : stressBurst) s = std::clamp (s * 2.5f, -1.0f, 1.0f);

        // Clean drive sag test
        for (const auto& amp : kAmps)
        {
            DriveConfig sagDrive = { "sag", 0.6f, 0.6f };
            auto output = processThroughChain (burst, sr, amp, sagDrive);
            std::string filename = outputDir + "/sag_" + std::string(amp.name) + ".wav";
            writeWav (juce::File (filename), output, sr);
            std::cout << "  " << amp.name << " (clean) -> " << filename << std::endl;
        }

        // Cranked drive sag test — main collapsed Clean/Crunch/Lead channels
        // into "amp + gain"; we just lean on a higher preampGain in the
        // DriveConfig below to push the same circuit harder.
        AmpConfig crankedAmps[] = {
            { "Fender",   ToneStackModel::Topology::Fender,   AmpType::Fender,   0.55f },
            { "Marshall", ToneStackModel::Topology::Marshall, AmpType::Marshall, 0.60f },
            { "Vox",      ToneStackModel::Topology::Vox,      AmpType::Vox,      0.55f },
        };
        for (const auto& amp : crankedAmps)
        {
            DriveConfig crankedDrive = { "cranked", 0.85f, 0.75f };
            // Use the boosted burst for cranked tests — at the default burst
            // level the sag envelope at the power-amp input only reaches
            // ~0.3, hiding per-amp sag-depth differences. With the hotter
            // burst the envelope approaches 1.0 and we actually exercise
            // the per-amp PowerAmpConfig::sagDepth values. Also push sag
            // knob to maximum so the per-amp depth coefficient sees its
            // full range.
            auto output = processThroughChain (stressBurst, sr, amp, crankedDrive, /*sagAmount*/ 1.0f);
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

        struct PAConfig { const char* name; AmpType type; float drive; };
        PAConfig paConfigs[] = {
            { "Fender_PA",   AmpType::Fender,   0.5f },
            { "Marshall_PA", AmpType::Marshall, 0.5f },
            { "Vox_PA",      AmpType::Vox,      0.7f }, // Higher drive to push into nonlinear region
        };

        constexpr int blockSize = 512;

        for (const auto& pa : paConfigs)
        {
            // Match production: power-amp waveshaper runs at the
            // oversampled rate. Without this the even/odd ratio is
            // dominated by aliased clip products, not the actual
            // tube-curve asymmetry.
            DuskAudio::OversamplingManager oversampling;
            oversampling.setFactor (kOversamplingFactor);
            oversampling.prepare (sr, blockSize, 1);

            PowerAmp powerAmp;
            powerAmp.prepare (oversampling.getOversampledSampleRate());
            powerAmp.setAmpType (pa.type);
            powerAmp.setDrive (pa.drive);
            powerAmp.setPresence (0.5f);
            powerAmp.setResonance (0.5f);
            powerAmp.setSag (0.3f);

            juce::AudioBuffer<float> osBuf (1, blockSize);
            std::vector<float> buffer (tone);
            int numSamples = static_cast<int> (buffer.size());

            for (int offset = 0; offset < numSamples; offset += blockSize)
            {
                int thisBlock = std::min (blockSize, numSamples - offset);
                std::copy (buffer.data() + offset,
                           buffer.data() + offset + thisBlock,
                           osBuf.getWritePointer (0));

                juce::dsp::AudioBlock<float> baseBlock (osBuf.getArrayOfWritePointers(),
                                                        1, static_cast<size_t> (thisBlock));
                auto upBlock = oversampling.processSamplesUp (baseBlock);
                powerAmp.process (upBlock.getChannelPointer (0),
                                  static_cast<int> (upBlock.getNumSamples()));
                oversampling.processSamplesDown (baseBlock);

                std::copy (osBuf.getReadPointer (0),
                           osBuf.getReadPointer (0) + thisBlock,
                           buffer.data() + offset);
            }

            std::string filename = outputDir + "/poweramp_only_" + std::string(pa.name) + ".wav";
            writeWav (juce::File (filename), buffer, sr);
            std::cout << "  " << pa.name << " -> " << filename << std::endl;
        }
    }

    // --- 8. Fender stage-by-stage diagnostic ---
    // The push-pull fix made Marshall correct but collapsed Fender's full-chain
    // output to silence. Render the chain at incremental endpoints to pinpoint
    // where the signal disappears.
    std::cout << std::endl << "=== FENDER STAGE-BY-STAGE DIAGNOSTIC ===" << std::endl;
    {
        auto signal = generateTestTone (sr, 440.0, 0.25f);   // matches THD "normal"
        constexpr int blockSize = 512;
        const int numSamples = static_cast<int> (signal.size());

        auto peakOf = [] (const std::vector<float>& v) {
            float p = 0.0f;
            for (float s : v) p = std::max (p, std::abs (s));
            return p;
        };

        auto runPreampOnly = [&] () {
            DuskAudio::OversamplingManager os;
            os.setFactor (kOversamplingFactor);
            os.prepare (sr, blockSize, 1);
            auto preamp = PreampModel::create (AmpType::Fender);
            preamp->prepare (os.getOversampledSampleRate());
            preamp->setGain (0.55f);   // crunch
            preamp->setBright (false);
            juce::AudioBuffer<float> osBuf (1, blockSize);
            std::vector<float> out (signal);
            for (int o = 0; o < numSamples; o += blockSize)
            {
                int n = std::min (blockSize, numSamples - o);
                std::copy (out.data() + o, out.data() + o + n, osBuf.getWritePointer (0));
                juce::dsp::AudioBlock<float> bb (osBuf.getArrayOfWritePointers(),
                                                  1, static_cast<size_t> (n));
                auto up = os.processSamplesUp (bb);
                preamp->process (up.getChannelPointer (0),
                                 static_cast<int> (up.getNumSamples()));
                os.processSamplesDown (bb);
                std::copy (osBuf.getReadPointer (0), osBuf.getReadPointer (0) + n,
                           out.data() + o);
            }
            return out;
        };

        auto runPreampToneStack = [&] () {
            DuskAudio::OversamplingManager os;
            os.setFactor (kOversamplingFactor);
            os.prepare (sr, blockSize, 1);
            auto preamp = PreampModel::create (AmpType::Fender);
            ToneStackModel ts;
            preamp->prepare (os.getOversampledSampleRate());
            ts.prepare (os.getOversampledSampleRate());
            preamp->setGain (0.55f);
            preamp->setBright (false);
            ts.setTopology (ToneStackModel::Topology::Fender);
            ts.setBass (0.5f); ts.setMid (0.5f); ts.setTreble (0.5f);
            juce::AudioBuffer<float> osBuf (1, blockSize);
            std::vector<float> out (signal);
            for (int o = 0; o < numSamples; o += blockSize)
            {
                int n = std::min (blockSize, numSamples - o);
                std::copy (out.data() + o, out.data() + o + n, osBuf.getWritePointer (0));
                juce::dsp::AudioBlock<float> bb (osBuf.getArrayOfWritePointers(),
                                                  1, static_cast<size_t> (n));
                auto up = os.processSamplesUp (bb);
                preamp->process    (up.getChannelPointer (0),
                                    static_cast<int> (up.getNumSamples()));
                ts.process         (up.getChannelPointer (0),
                                    static_cast<int> (up.getNumSamples()));
                os.processSamplesDown (bb);
                std::copy (osBuf.getReadPointer (0), osBuf.getReadPointer (0) + n,
                           out.data() + o);
            }
            return out;
        };

        auto runFullChain = [&] (bool forcePushPull) {
            DuskAudio::OversamplingManager os;
            os.setFactor (kOversamplingFactor);
            os.prepare (sr, blockSize, 1);
            auto preamp = PreampModel::create (AmpType::Fender);
            ToneStackModel ts;
            PowerAmp pa;
            preamp->prepare (os.getOversampledSampleRate());
            ts.prepare (os.getOversampledSampleRate());
            pa.prepare (os.getOversampledSampleRate());
            pa.setAmpType (AmpType::Fender);
            pa.setIsPushPullOverride (forcePushPull);
            pa.setDrive (0.5f);
            pa.setPresence (0.5f);
            pa.setResonance (0.5f);
            pa.setSag (0.5f);
            preamp->setGain (0.55f);
            preamp->setBright (false);
            ts.setTopology (ToneStackModel::Topology::Fender);
            ts.setBass (0.5f); ts.setMid (0.5f); ts.setTreble (0.5f);
            juce::AudioBuffer<float> osBuf (1, blockSize);
            std::vector<float> out (signal);
            for (int o = 0; o < numSamples; o += blockSize)
            {
                int n = std::min (blockSize, numSamples - o);
                std::copy (out.data() + o, out.data() + o + n, osBuf.getWritePointer (0));
                juce::dsp::AudioBlock<float> bb (osBuf.getArrayOfWritePointers(),
                                                  1, static_cast<size_t> (n));
                auto up = os.processSamplesUp (bb);
                preamp->process (up.getChannelPointer (0),
                                 static_cast<int> (up.getNumSamples()));
                ts.process      (up.getChannelPointer (0),
                                 static_cast<int> (up.getNumSamples()));
                pa.process      (up.getChannelPointer (0),
                                 static_cast<int> (up.getNumSamples()));
                os.processSamplesDown (bb);
                std::copy (osBuf.getReadPointer (0), osBuf.getReadPointer (0) + n,
                           out.data() + o);
            }
            return out;
        };

        auto a = runPreampOnly();        writeWav (juce::File (outputDir + "/fender_diag_1_preamp.wav"), a, sr);
        auto b = runPreampToneStack();   writeWav (juce::File (outputDir + "/fender_diag_2_preamp_tonestack.wav"), b, sr);
        auto c = runFullChain (false);   writeWav (juce::File (outputDir + "/fender_diag_3_full_singleended.wav"), c, sr);
        auto d = runFullChain (true);    writeWav (juce::File (outputDir + "/fender_diag_4_full_pushpull.wav"), d, sr);

        std::cout << "  Stage 1: preamp only          peak=" << peakOf (a) << std::endl;
        std::cout << "  Stage 2: preamp + tone stack  peak=" << peakOf (b) << std::endl;
        std::cout << "  Stage 3: full chain (single)  peak=" << peakOf (c) << std::endl;
        std::cout << "  Stage 4: full chain (PP)      peak=" << peakOf (d) << std::endl;
    }

    std::cout << std::endl << "Done! Output in: " << outputDir << std::endl;
    return 0;
}
