/*
 * Auto-gain stability gate.
 *
 * Auto-makeup used to invert the compressor's gain reduction through a ~200 ms
 * smoother. That tracks the compression envelope closely enough to partially
 * undo it, so the compressor breathed on any source with real dynamics, and it
 * stepped on mode/preset change because the smoother was primed with
 * setCurrentAndTargetValue. It is now slow input/output level matching
 * (AutoGainMatcher).
 *
 * Two gates:
 *
 * 1. PUMPING. Render a source that alternates loud and quiet every half second,
 *    once with auto-makeup off and once on. The per-block ratio between the two
 *    renders IS the applied makeup gain, with the compressor's own behaviour
 *    divided out. A level-matching auto-gain barely moves across the cycle; a
 *    GR inverter swings with it. Gate: peak-to-peak movement of that gain.
 *
 * 2. STATIC MATCH. With a steady source, auto-makeup on, the output should end
 *    up close to the input level, per mode. That is the whole promise of the
 *    feature, and it is what the GR inverter could not do for the Opto without a
 *    hand-tuned fudge factor.
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{

bool g_paramError = false;

void setParam(UniversalCompressor& plugin, const juce::String& paramID, float value)
{
    auto& params = plugin.getParameters();
    if (auto* param = params.getParameter(paramID))
        param->setValueNotifyingHost(params.getParameterRange(paramID).convertTo0to1(value));
    else
    {
        std::cerr << "ERROR: param '" << paramID << "' not found — test config invalid\n";
        g_paramError = true;
    }
}

/** Unity makeup knobs, so the only level difference between renders is auto-gain. */
void configureMode(UniversalCompressor& plugin, CompressorMode mode)
{
    setParam(plugin, "mode", static_cast<float>(static_cast<int>(mode)));
    setParam(plugin, "mix", 100.0f);

    switch (mode)
    {
        case CompressorMode::Opto:
            setParam(plugin, "opto_peak_reduction", 55.0f);
            setParam(plugin, "opto_gain", 50.0f);            // knob 50 = 0 dB
            break;
        case CompressorMode::FET:
            setParam(plugin, "fet_input", 6.0f);
            setParam(plugin, "fet_output", 0.0f);
            break;
        case CompressorMode::VCA:
            setParam(plugin, "vca_threshold", -24.0f);
            setParam(plugin, "vca_ratio", 4.0f);
            setParam(plugin, "vca_output", 0.0f);
            break;
        case CompressorMode::Bus:
            setParam(plugin, "bus_threshold", -24.0f);
            setParam(plugin, "bus_makeup", 0.0f);
            break;
        case CompressorMode::StudioFET:
            // Studio FET shares the fet_* parameter set (see applyPreset case 4).
            setParam(plugin, "fet_input", 6.0f);
            setParam(plugin, "fet_output", 0.0f);
            break;
        case CompressorMode::StudioVCA:
            setParam(plugin, "studio_vca_threshold", -24.0f);
            setParam(plugin, "studio_vca_output", 0.0f);
            break;
        case CompressorMode::Digital:
            setParam(plugin, "digital_threshold", -24.0f);
            setParam(plugin, "digital_output", 0.0f);
            break;
        case CompressorMode::Multiband:
            for (const juce::String name : { "low", "lowmid", "highmid", "high" })
            {
                setParam(plugin, "mb_" + name + "_enabled", 1.0f);
                setParam(plugin, "mb_" + name + "_ratio", 4.0f);
                setParam(plugin, "mb_" + name + "_threshold", -24.0f);
                setParam(plugin, "mb_" + name + "_makeup", 0.0f);
            }
            break;
    }
}

/** Per-block output RMS for a render. `dynamic` alternates loud/quiet each half second. */
std::vector<float> renderBlockRms(CompressorMode mode, bool autoMakeup, bool dynamic,
                                  double sr, int block, int numBlocks)
{
    UniversalCompressor plugin;
    plugin.setPlayConfigDetails(2, 2, sr, block);
    plugin.prepareToPlay(sr, block);
    configureMode(plugin, mode);
    setParam(plugin, "auto_makeup", autoMakeup ? 1.0f : 0.0f);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);
    juce::Random rng(0xA07A1);

    const int blocksPerHalfSecond = juce::jmax(1, static_cast<int>(sr * 0.5 / block));

    std::vector<float> out;
    out.reserve(static_cast<size_t>(numBlocks));

    for (int b = 0; b < numBlocks; ++b)
    {
        // Loud = -9 dBFS-ish, quiet = -27 dBFS-ish.
        const bool loud = (! dynamic) || (((b / blocksPerHalfSecond) % 2) == 0);
        const float amp = loud ? 0.35f : 0.045f;

        for (int i = 0; i < block; ++i)
        {
            const float s = amp * (rng.nextFloat() * 2.0f - 1.0f);
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample(ch, i, s);
        }

        plugin.processBlock(buf, midi);

        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer(ch);
            for (int i = 0; i < block; ++i)
                sum += static_cast<double>(d[i]) * d[i];
        }
        out.push_back(static_cast<float>(std::sqrt(sum / (2.0 * block))));
    }

    return out;
}

float toDb(float linear) { return linear > 1.0e-9f ? 20.0f * std::log10(linear) : -180.0f; }

struct ModeCase
{
    const char* name;
    CompressorMode mode;
};

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int block = 512;

    const ModeCase modes[] = {
        { "Opto",       CompressorMode::Opto },
        { "FET",        CompressorMode::FET },
        { "VCA",        CompressorMode::VCA },
        { "Bus",        CompressorMode::Bus },
        { "StudioVCA",  CompressorMode::StudioVCA },
        { "Digital",    CompressorMode::Digital },
        { "Multiband",  CompressorMode::Multiband },
    };

    // Makeup movement across a 6:1 loud/quiet cycle. The estimator's window is
    // 2 s, so a full cycle should shift it very little.
    constexpr float kMaxPumpDb = 3.0f;
    // How close a settled output should sit to the input level.
    constexpr float kMaxStaticErrorDb = 2.0f;

    int failures = 0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n--- Auto-gain stability gate ---\n";
    std::cout << "\n  1. Pumping: peak-to-peak makeup movement over a loud/quiet cycle"
              << " (limit " << kMaxPumpDb << " dB)\n";

    {
        constexpr int numBlocks = 940;                 // ~10 s
        const int settleBlock = 940 / 3;               // judge the last two thirds

        for (const auto& m : modes)
        {
            const auto off = renderBlockRms(m.mode, false, true, sr, block, numBlocks);
            const auto on  = renderBlockRms(m.mode, true,  true, sr, block, numBlocks);

            float minDb = 1.0e9f, maxDb = -1.0e9f;
            for (int b = settleBlock; b < numBlocks; ++b)
            {
                if (off[static_cast<size_t>(b)] < 1.0e-6f)
                    continue;
                const float gainDb = toDb(on[static_cast<size_t>(b)]) - toDb(off[static_cast<size_t>(b)]);
                minDb = std::min(minDb, gainDb);
                maxDb = std::max(maxDb, gainDb);
            }

            const float pumpDb = maxDb - minDb;
            const bool ok = pumpDb <= kMaxPumpDb;
            if (! ok)
                ++failures;

            std::cout << (ok ? "  ok   " : "  FAIL ")
                      << std::setw(11) << std::left << m.name << std::right
                      << "  makeup " << std::setw(7) << minDb << " .. " << std::setw(7) << maxDb
                      << " dB   swing " << std::setw(6) << pumpDb << " dB\n";
        }
    }

    std::cout << "\n  2. Static match: settled output vs input, auto-makeup on"
              << " (limit " << kMaxStaticErrorDb << " dB)\n";

    {
        constexpr int numBlocks = 940;
        const int settleBlock = (940 * 2) / 3;
        const float inputRms = 0.35f / std::sqrt(3.0f);   // RMS of uniform noise at +/-0.35
        const float inputDb = toDb(inputRms);

        for (const auto& m : modes)
        {
            const auto on = renderBlockRms(m.mode, true, false, sr, block, numBlocks);

            double sum = 0.0;
            int count = 0;
            for (int b = settleBlock; b < numBlocks; ++b)
            {
                sum += on[static_cast<size_t>(b)];
                ++count;
            }
            const auto meanRms = static_cast<float>(sum / std::max(1, count));
            const float errorDb = toDb(meanRms) - inputDb;
            const bool ok = std::abs(errorDb) <= kMaxStaticErrorDb;
            if (! ok)
                ++failures;

            std::cout << (ok ? "  ok   " : "  FAIL ")
                      << std::setw(11) << std::left << m.name << std::right
                      << "  out " << std::setw(8) << toDb(meanRms)
                      << " dB   error " << std::setw(7) << errorDb << " dB\n";
        }
    }

    if (g_paramError)
    {
        std::cout << "\n  RESULT: FAIL (missing parameter — test config invalid)\n";
        return 2;
    }

    std::cout << "\n  RESULT: " << (failures == 0 ? "PASS" : "FAIL")
              << " (" << failures << " checks outside limits)\n";
    return failures == 0 ? 0 : 1;
}
