// duskamp_di_render — render a DI WAV through DuskAmpProcessor with a
// configurable amp + cab IR + knob settings, write the result to a WAV.
//
// Built for A/B comparison against commercial amp sims (AmpliTube, Helix,
// Neural DSP) — the sister tool reads the AmpliTube bounce, this one
// generates DuskAmp's render of the same DI for side-by-side analysis.
//
// Usage:
//   duskamp_di_render <input.wav> <output.wav>
//                     [--amp american|british|ac]
//                     [--channel clean|crunch|lead]
//                     [--gain 0..1] [--bass 0..1] [--mid 0..1] [--treble 0..1]
//                     [--drive 0..1] [--presence 0..1] [--resonance 0..1] [--sag 0..1]
//                     [--ir <ir.wav>]
//                     [--mix 0..1]   (cab mix; default 1.0)
//                     [--output-db <dB>]   (default 0)
//                     [--bright 0|1] [--no-cab]
//
// Sample rate of the input WAV drives the render. Output is stereo float WAV
// at the same sample rate. Mono inputs are duplicated to L/R before processing.

#include "PluginProcessor.h"
#include "ParamIDs.h"
#include "FactoryPresets.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    constexpr int kBlockSize = 256;

    bool readWav (const juce::File& file, juce::AudioBuffer<float>& buf, double& sampleRate)
    {
        juce::AudioFormatManager mgr;
        mgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (file));
        if (! reader)
        {
            std::cerr << "Failed to open input WAV: " << file.getFullPathName() << std::endl;
            return false;
        }
        sampleRate = reader->sampleRate;
        const int numCh = static_cast<int> (reader->numChannels);
        const int len   = static_cast<int> (reader->lengthInSamples);
        buf.setSize (numCh, len);
        reader->read (&buf, 0, len, 0, true, true);
        std::cout << "  input: " << numCh << "ch / "
                  << static_cast<int> (sampleRate) << " Hz / "
                  << len << " samples ("
                  << juce::String (len / sampleRate, 2) << " s)" << std::endl;
        return true;
    }

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf,
                   double sampleRate)
    {
        file.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream == nullptr || ! stream->openedOk())
        {
            std::cerr << "Failed to open output WAV: " << file.getFullPathName() << std::endl;
            return false;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer (
            fmt.createWriterFor (stream.get(), sampleRate, buf.getNumChannels(),
                                 32, {}, 0));
        if (writer == nullptr) return false;
        stream.release();
        return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    void setParam (juce::AudioProcessorValueTreeState& params,
                   const juce::String& id, float value)
    {
        if (auto* p = params.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    int parseChannel (const juce::String& s)
    {
        if (s.equalsIgnoreCase ("clean"))  return 0;
        if (s.equalsIgnoreCase ("crunch")) return 1;
        if (s.equalsIgnoreCase ("lead"))   return 2;
        return 0;
    }

    int parseAmp (const juce::String& s)
    {
        if (s.equalsIgnoreCase ("american")) return 0;
        if (s.equalsIgnoreCase ("british"))  return 1;
        if (s.equalsIgnoreCase ("ac"))       return 2;
        return 0;
    }

    // Wait for the cab worker (Phase 1's cabLoadPool_) to flip irLoaded_,
    // since BinaryData-bundled IR autoload now runs on a background thread.
    // 2 s is well above the ~500 ms upper bound for the pink-noise loudness
    // measurement on modest hardware.
    void waitForCabReady (DuskAmpProcessor& processor, double timeoutSec = 2.0)
    {
        const double deadline = juce::Time::getMillisecondCounterHiRes() * 1.0e-3 + timeoutSec;
        while (! processor.getEngine().getCabinetIR().isLoaded()
               && juce::Time::getMillisecondCounterHiRes() * 1.0e-3 < deadline)
            juce::Thread::sleep (10);
    }

    // Loudness sweep helpers. RMS over the full render (in dBFS) is a
    // close-enough proxy for integrated LUFS when the goal is matching
    // 9 amp presets within ±1 dB on the same DI — full BS.1770 K-weighting
    // adds implementation surface without changing the relative ranking
    // for steady-state guitar content.
    struct PresetMeasurement
    {
        juce::String name;
        float currentOutputDb;
        float rmsDb;
        float peakDb;
    };

    PresetMeasurement measurePreset (const DuskAmpPreset& preset,
                                     const juce::AudioBuffer<float>& diInput,
                                     double sampleRate)
    {
        DuskAmpProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, kBlockSize);

        // Set TONE_TYPE before prepareToPlay so the right per-amp bundled IR
        // gets queued for autoload before the audio path goes live.
        setParam (processor.parameters, DuskAmpParams::TONE_TYPE,
                  static_cast<float> (preset.toneType));

        processor.prepareToPlay (sampleRate, kBlockSize);
        waitForCabReady (processor);

        preset.applyTo (processor.parameters);

        // Warm-up — let smoothed values + cab convolution settle.
        {
            juce::AudioBuffer<float> warm (2, kBlockSize);
            warm.clear();
            juce::MidiBuffer midi;
            for (int n = 0; n < 64; ++n)
                processor.processBlock (warm, midi);
        }

        const int totalSamples = diInput.getNumSamples();
        juce::MidiBuffer midi;
        double sumSq = 0.0;
        float  peak  = 0.0f;
        juce::int64 nProcessed = 0;

        for (int sampleStart = 0; sampleStart < totalSamples; sampleStart += kBlockSize)
        {
            const int blockSamples = std::min (kBlockSize, totalSamples - sampleStart);
            juce::AudioBuffer<float> block (2, blockSamples);
            block.copyFrom (0, 0, diInput.getReadPointer (0, sampleStart), blockSamples);
            if (diInput.getNumChannels() >= 2)
                block.copyFrom (1, 0, diInput.getReadPointer (1, sampleStart), blockSamples);
            else
                block.copyFrom (1, 0, diInput.getReadPointer (0, sampleStart), blockSamples);

            processor.processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float* p = block.getReadPointer (ch);
                for (int i = 0; i < blockSamples; ++i)
                {
                    const float s = p[i];
                    sumSq += static_cast<double> (s) * s;
                    peak = std::max (peak, std::abs (s));
                }
            }
            nProcessed += 2 * blockSamples;
        }

        processor.releaseResources();

        const double rms   = nProcessed > 0 ? std::sqrt (sumSq / static_cast<double> (nProcessed)) : 0.0;
        const float  rmsDb = rms  > 1.0e-9 ? juce::Decibels::gainToDecibels (static_cast<float> (rms)) : -120.0f;
        const float  pkDb  = peak > 1.0e-9f ? juce::Decibels::gainToDecibels (peak) : -120.0f;

        return { juce::String (preset.name), preset.outputLevel, rmsDb, pkDb };
    }

    int runLoudnessSweep (const juce::File& diFile)
    {
        std::cout << "Loudness sweep — DI: " << diFile.getFullPathName() << std::endl;
        juce::AudioBuffer<float> di;
        double sampleRate = 0.0;
        if (! readWav (diFile, di, sampleRate)) return 2;

        std::vector<PresetMeasurement> results;
        results.reserve (static_cast<size_t> (kNumFactoryPresets));

        for (int i = 0; i < kNumFactoryPresets; ++i)
        {
            std::cout << "  [" << (i + 1) << "/" << kNumFactoryPresets << "] "
                      << kFactoryPresets[i].name << " ..." << std::flush;
            const auto m = measurePreset (kFactoryPresets[i], di, sampleRate);
            std::cout << " rms=" << juce::String (m.rmsDb, 2)
                      << " dB  peak=" << juce::String (m.peakDb, 2) << " dB" << std::endl;
            results.push_back (m);
        }

        // Median RMS = target reference for the matched set.
        std::vector<float> rmsSorted;
        rmsSorted.reserve (results.size());
        for (const auto& r : results) rmsSorted.push_back (r.rmsDb);
        std::sort (rmsSorted.begin(), rmsSorted.end());
        const float median = rmsSorted[rmsSorted.size() / 2];

        std::cout << "\nReference (median RMS): " << juce::String (median, 2) << " dBFS\n\n"
                  << std::left << std::setw (20) << "Preset"
                  << std::setw (10) << "rms dB"
                  << std::setw (10) << "peak dB"
                  << std::setw (10) << "outLv"
                  << std::setw (12) << "delta"
                  << "suggest outLv\n"
                  << std::string (72, '-') << "\n";

        constexpr float kTruePeakCeilingDb = -1.0f;
        for (const auto& r : results)
        {
            const float delta = median - r.rmsDb;
            // Headroom guard: never push a preset above peak ≤ -1 dBTP. If
            // raising outLv to hit the median would exceed that, cap.
            const float headroom = kTruePeakCeilingDb - r.peakDb;
            const float bounded  = std::min (delta, headroom);
            const float suggest  = r.currentOutputDb + bounded;
            const bool  capped   = bounded < delta - 0.05f;

            std::cout << std::left << std::setw (20) << r.name
                      << std::setw (10) << juce::String (r.rmsDb, 2).toStdString()
                      << std::setw (10) << juce::String (r.peakDb, 2).toStdString()
                      << std::setw (10) << juce::String (r.currentOutputDb, 2).toStdString()
                      << std::setw (12) << juce::String (delta, 2).toStdString()
                      << juce::String (suggest, 2).toStdString()
                      << (capped ? "  (peak-capped)" : "")
                      << "\n";
        }

        std::cout << "\nTarget: each preset within ±1 dB integrated RMS of median, peak ≤ "
                  << kTruePeakCeilingDb << " dBFS.\n"
                  << "Apply suggested outLv values by hand-editing kFactoryPresets[] in src/FactoryPresets.h.\n";
        return 0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;

    if (argc < 2)
    {
        std::cerr << "Usage: duskamp_di_render <input.wav> <output.wav> [options]\n"
                     "       duskamp_di_render <input.wav> --loudness-sweep\n"
                     "  --mode dsp|nam     plugin mode (default dsp)\n"
                     "  --nam <profile>    NAM .nam profile to load (requires --mode nam)\n"
                     "  --amp american|british|ac\n"
                     "  --channel clean|crunch|lead\n"
                     "  --gain --bass --mid --treble --drive --presence --resonance --sag (0..1)\n"
                     "  --ir <ir.wav>      cab IR path (default: no cab)\n"
                     "  --mix <0..1>       cab wet/dry mix (default 1.0)\n"
                     "  --output-db <dB>   master output (default 0)\n"
                     "  --bright 0|1       bright switch (default 0)\n"
                     "  --no-cab           disable cab section\n"
                     "  --loudness-sweep   render all 9 factory presets, report RMS/peak,\n"
                     "                     and suggest outputLevel adjustments to match\n";
        return 1;
    }

    const juce::File inFile = juce::File (juce::String::fromUTF8 (argv[1]));

    // Sweep mode: short-circuit before parsing single-render options.
    for (int i = 2; i < argc; ++i)
    {
        if (juce::String::fromUTF8 (argv[i]) == "--loudness-sweep")
            return runLoudnessSweep (inFile);
    }

    if (argc < 3)
    {
        std::cerr << "Single-render mode requires <output.wav>. "
                     "For loudness sweep, pass --loudness-sweep instead.\n";
        return 1;
    }
    const juce::File outFile = juce::File (juce::String::fromUTF8 (argv[2]));

    // Defaults — chosen to match the user's "American Clean" reference target.
    int   ampIdx     = 0;     // American
    int   channelIdx = 0;     // Clean
    float gain       = 0.5f;
    float bass       = 0.5f;
    float mid        = 0.5f;
    float treble     = 0.5f;
    float drive      = 0.3f;
    float presence   = 0.5f;
    float resonance  = 0.5f;
    float sag        = 0.3f;
    bool  bright     = false;
    bool  cabEnabled = true;
    float cabMix     = 1.0f;
    float cabHiCut   = 12000.0f;
    float cabLoCut   = 60.0f;
    float outputDb   = 0.0f;
    juce::File irFile;
    juce::File namFile;
    int   modeIdx    = 0; // 0 = DSP, 1 = NAM

    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg (juce::String::fromUTF8 (argv[i]));
        auto next = [&] () -> juce::String
        {
            if (i + 1 < argc) return juce::String::fromUTF8 (argv[++i]);
            return {};
        };
        if      (arg == "--amp")        ampIdx = parseAmp (next());
        else if (arg == "--channel")    channelIdx = parseChannel (next());
        else if (arg == "--gain")       gain = next().getFloatValue();
        else if (arg == "--bass")       bass = next().getFloatValue();
        else if (arg == "--mid")        mid = next().getFloatValue();
        else if (arg == "--treble")     treble = next().getFloatValue();
        else if (arg == "--drive")      drive = next().getFloatValue();
        else if (arg == "--presence")   presence = next().getFloatValue();
        else if (arg == "--resonance")  resonance = next().getFloatValue();
        else if (arg == "--sag")        sag = next().getFloatValue();
        else if (arg == "--bright")     bright = (next().getIntValue() != 0);
        else if (arg == "--ir")         irFile = juce::File (next());
        else if (arg == "--nam")        namFile = juce::File (next());
        else if (arg == "--mode")
        {
            const auto m = next().toLowerCase();
            if      (m == "dsp") modeIdx = 0;
            else if (m == "nam") modeIdx = 1;
            else std::cerr << "Unknown --mode: " << m << std::endl;
        }
        else if (arg == "--no-cab")     cabEnabled = false;
        else if (arg == "--mix")        cabMix = next().getFloatValue();
        else if (arg == "--hicut")      cabHiCut = next().getFloatValue();
        else if (arg == "--locut")      cabLoCut = next().getFloatValue();
        else if (arg == "--output-db")  outputDb = next().getFloatValue();
        else
            std::cerr << "Unknown option: " << arg << std::endl;
    }

    std::cout << "Loading: " << inFile.getFullPathName() << std::endl;
    juce::AudioBuffer<float> in;
    double sampleRate = 0.0;
    if (! readWav (inFile, in, sampleRate)) return 2;

    const int totalSamples = in.getNumSamples();

    // Spin up the processor at the input's native sample rate. Set the amp
    // model BEFORE prepareToPlay so the per-amp default IR auto-load (driven
    // off TONE_TYPE in prepareToPlay) picks the right cab — otherwise it
    // would fire for the layout default (British) and the explicit --ir
    // would re-load on top, doing the convolution-normalise work twice.
    DuskAmpProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, kBlockSize);

    // Apply config via APVTS (the same path Logic uses, so all smoothing /
    // mode-aware dispatch behaves exactly like the live plugin).
    auto& params = processor.parameters;
    setParam (params, DuskAmpParams::AMP_MODE,        static_cast<float> (modeIdx));
    setParam (params, DuskAmpParams::TONE_TYPE,       static_cast<float> (ampIdx));
    setParam (params, DuskAmpParams::TONE_TYPE_NAM,   static_cast<float> (ampIdx));

    processor.prepareToPlay (sampleRate, kBlockSize);

    if (modeIdx == 1 && namFile.existsAsFile())
    {
        std::cout << "  NAM: " << namFile.getFileName() << std::endl;
        processor.loadNAMModel (namFile);
    }
    else if (modeIdx == 1)
    {
        std::cerr << "WARNING: --mode nam without a valid --nam <profile> — NAM path "
                     "will run without a model and output may be silent." << std::endl;
    }

    setParam (params, DuskAmpParams::PREAMP_CHANNEL,  static_cast<float> (channelIdx));
    setParam (params, DuskAmpParams::PREAMP_GAIN,     gain);
    setParam (params, DuskAmpParams::PREAMP_BRIGHT,   bright ? 1.0f : 0.0f);
    setParam (params, DuskAmpParams::BASS,            bass);
    setParam (params, DuskAmpParams::MID,             mid);
    setParam (params, DuskAmpParams::TREBLE,          treble);
    setParam (params, DuskAmpParams::BASS_NAM,        bass);
    setParam (params, DuskAmpParams::MID_NAM,         mid);
    setParam (params, DuskAmpParams::TREBLE_NAM,      treble);
    setParam (params, DuskAmpParams::POWER_DRIVE,     drive);
    setParam (params, DuskAmpParams::PRESENCE,        presence);
    setParam (params, DuskAmpParams::RESONANCE,       resonance);
    setParam (params, DuskAmpParams::SAG,             sag);
    setParam (params, DuskAmpParams::CAB_ENABLED,     cabEnabled ? 1.0f : 0.0f);
    setParam (params, DuskAmpParams::CAB_NORMALIZE,   1.0f);
    setParam (params, DuskAmpParams::CAB_MIX,         cabMix);
    setParam (params, DuskAmpParams::CAB_HICUT,       cabHiCut);
    setParam (params, DuskAmpParams::CAB_LOCUT,       cabLoCut);
    setParam (params, DuskAmpParams::OUTPUT_LEVEL,    outputDb);
    setParam (params, DuskAmpParams::OUTPUT_LEVEL_NAM, outputDb);

    if (irFile.existsAsFile())
    {
        std::cout << "  IR: " << irFile.getFileName() << " (explicit)" << std::endl;
        processor.loadCabinetIR (irFile);
    }
    else if (cabEnabled)
    {
        // No --ir given. prepareToPlay auto-loaded the per-amp bundled
        // default; report its display name so the render is reproducible.
        const auto& cab = processor.getEngine().getCabinetIR();
        if (cab.isLoaded())
            std::cout << "  IR: " << cab.getLoadedFileName()
                      << " (auto-default, bundled)" << std::endl;
        else
            std::cout << "  IR: (none — cab will pass dry)\n";
    }

    std::cout << "  amp=" << ampIdx << " channel=" << channelIdx
              << " gain=" << gain << " drive=" << drive
              << " bass=" << bass << " mid=" << mid << " treble=" << treble << std::endl;

    // Warm-up — let smoothed values + IR convolution settle.
    {
        juce::AudioBuffer<float> warm (2, kBlockSize);
        warm.clear();
        juce::MidiBuffer midi;
        for (int n = 0; n < 64; ++n)
            processor.processBlock (warm, midi);
    }

    // Render.
    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();
    juce::MidiBuffer midi;
    int processed = 0;
    for (int sampleStart = 0; sampleStart < totalSamples; sampleStart += kBlockSize)
    {
        const int blockSamples = std::min (kBlockSize, totalSamples - sampleStart);
        juce::AudioBuffer<float> block (2, blockSamples);
        // Mono-or-stereo input → stereo: duplicate ch0 if input is mono.
        block.copyFrom (0, 0, in.getReadPointer (0, sampleStart), blockSamples);
        if (in.getNumChannels() >= 2)
            block.copyFrom (1, 0, in.getReadPointer (1, sampleStart), blockSamples);
        else
            block.copyFrom (1, 0, in.getReadPointer (0, sampleStart), blockSamples);

        processor.processBlock (block, midi);

        output.copyFrom (0, sampleStart, block, 0, 0, blockSamples);
        output.copyFrom (1, sampleStart, block, 1, 0, blockSamples);
        processed += blockSamples;
    }

    std::cout << "Rendered " << processed << " samples." << std::endl;
    if (! writeWav (outFile, output, sampleRate)) return 3;

    std::cout << "Wrote: " << outFile.getFullPathName() << std::endl;
    processor.releaseResources();
    return 0;
}
