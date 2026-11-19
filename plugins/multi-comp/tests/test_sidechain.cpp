/*
 * Sidechain functional test — proves the plugin keys compression off the
 * EXTERNAL sidechain input bus (bus 1), not the main input. clap-validator
 * 0.3.2 can't exercise this (it hangs on any secondary input port — its own
 * bug), so this test validates the DSP + JUCE bus routing that the CLAP wrapper
 * feeds via getBusBuffer(buffer, true, 1).
 *
 * Setup: main = steady tone that alone causes little/no GR; sidechain enabled.
 *   A) sidechain SILENT  -> output ~= main level (no external trigger)
 *   B) sidechain LOUD    -> heavy GR -> output well below A
 * If B << A, the compressor is keying off the sidechain bus (working). If they
 * match, the sidechain is being ignored (broken).
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <cmath>
#include <iostream>
#include <iomanip>

static void setParam(UniversalCompressor& plugin, const juce::String& id, float v, bool& err)
{
    auto& p = plugin.getParameters();
    if (auto* param = p.getParameter(id))
        param->setValueNotifyingHost(p.getParameterRange(id).convertTo0to1(v));
    else { std::cerr << "ERROR: missing param '" << id << "'\n"; err = true; }
}

static float rmsDb(const juce::AudioBuffer<float>& b, int ch, int start, int n)
{
    double s = 0.0; const float* d = b.getReadPointer(ch);
    for (int i = start; i < start + n; ++i) s += (double)d[i] * d[i];
    const double r = std::sqrt(s / n);
    return r > 1e-12 ? 20.0f * (float)std::log10(r) : -240.0f;
}

// Render `main` (ch0/1) + `sc` (ch2/3) through the plugin; return output RMS dB (ch0).
static float render(UniversalCompressor& plugin, double sr, int block,
                    float mainAmp, float scAmp, int total)
{
    const int inCh = plugin.getTotalNumInputChannels();   // 4 when sidechain enabled
    juce::AudioBuffer<float> buf(juce::jmax(inCh, 2), total);
    buf.clear();
    const float w = 2.0f * juce::MathConstants<float>::pi * 220.0f / (float)sr;
    for (int i = 0; i < total; ++i)
    {
        const float m = mainAmp * std::sin(w * i);
        const float s = scAmp   * std::sin(w * i);
        buf.setSample(0, i, m); if (inCh > 1) buf.setSample(1, i, m);
        if (inCh > 2) buf.setSample(2, i, s);
        if (inCh > 3) buf.setSample(3, i, s);
    }

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> blk(buf.getNumChannels(), block);
    for (int off = 0; off < total; off += block)
    {
        const int nb = std::min(block, total - off);
        for (int c = 0; c < buf.getNumChannels(); ++c) blk.copyFrom(c, 0, buf, c, off, nb);
        if (nb < block) for (int c = 0; c < buf.getNumChannels(); ++c) blk.clear(c, nb, block - nb);
        juce::AudioBuffer<float> view(blk.getArrayOfWritePointers(), buf.getNumChannels(), nb);
        plugin.processBlock(view, midi);
        for (int c = 0; c < buf.getNumChannels(); ++c) buf.copyFrom(c, off, blk, c, 0, nb);
    }
    return rmsDb(buf, 0, total / 2, total / 2);   // settled second half
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    const int total = 24000;

    UniversalCompressor plugin;

    // Enable the sidechain input bus (main + sidechain in, main out).
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());   // main
    layout.inputBuses.add(juce::AudioChannelSet::stereo());   // sidechain
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    if (!plugin.setBusesLayout(layout))
    {
        std::cout << "  RESULT: FAIL (host/plugin rejected the sidechain bus layout)\n";
        return 1;
    }
    plugin.setRateAndBufferSizeDetails(sr, block);
    plugin.prepareToPlay(sr, block);

    bool err = false;
    // VCA mode, external sidechain ON, threshold/ratio set so a loud SC ducks hard.
    setParam(plugin, "mode", static_cast<float>(static_cast<int>(CompressorMode::VCA)), err);
    setParam(plugin, "sidechain_enable", 1.0f, err);
    setParam(plugin, "vca_threshold", -35.0f, err);
    setParam(plugin, "vca_ratio", 8.0f, err);
    setParam(plugin, "vca_attack", 1.0f, err);
    setParam(plugin, "vca_release", 50.0f, err);
    if (err) { std::cout << "  RESULT: FAIL (missing parameters)\n"; return 2; }

    const float mainAmp = 0.05f;   // ~-26 dB main tone
    const float scLoud   = 0.9f;    // hot sidechain

    const float outScSilent = render(plugin, sr, block, mainAmp, 0.0f, total);
    plugin.prepareToPlay(sr, block);   // reset envelope state between renders
    const float outScLoud   = render(plugin, sr, block, mainAmp, scLoud, total);

    const float duck = outScSilent - outScLoud;   // dB of extra GR the loud SC caused

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n--- Sidechain functional test (VCA, external SC on) ---\n";
    std::cout << "  output, SC silent : " << outScSilent << " dB\n";
    std::cout << "  output, SC loud   : " << outScLoud   << " dB\n";
    std::cout << "  sidechain ducking : " << duck << " dB\n";

    // A working external sidechain ducks the main by many dB when the SC is hot.
    // >=6 dB is unambiguous; a broken/ignored SC gives ~0.
    const float kMinDuckDb = 6.0f;
    const bool pass = duck >= kMinDuckDb;
    std::cout << "  RESULT: " << (pass ? "PASS" : "FAIL")
              << " (need >= " << kMinDuckDb << " dB ducking from the sidechain)\n";
    return pass ? 0 : 1;
}
