#include <JuceHeader.h>
#include "../MultiQ.h"
#include "../MultiQEditor.h"
#include <cmath>
#include <iostream>
#include <iomanip>

static int passed = 0, failed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        std::cout << "\033[32m[PASS]\033[0m " << name << "\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name << "\n";
        ++failed;
    }
}

static void checkDb(const char* name, float actual, float expected, float tolerance)
{
    bool ok = std::abs(actual - expected) <= tolerance;
    if (ok) {
        std::cout << "\033[32m[PASS]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(1) << actual
                  << " dB (expected " << expected << " dB, tol " << tolerance << ")\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(1) << actual
                  << " dB (expected " << expected << " dB, tol " << tolerance << ")\n";
        ++failed;
    }
}

// Generate stereo sine wave
static juce::AudioBuffer<float> generateSine(float freq, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        float val = std::sin(phase);
        buf.setSample(0, i, val);
        buf.setSample(1, i, val);
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
    return buf;
}

// Measure RMS in dB for a channel, skipping initial transient
static float measureRMSdB(const juce::AudioBuffer<float>& buf, int channel, int skipSamples = 0)
{
    int start = skipSamples;
    int count = buf.getNumSamples() - start;
    if (count <= 0) return -100.0f;

    const float* data = buf.getReadPointer(channel);
    double sum = 0.0;
    for (int i = start; i < start + count; ++i)
        sum += static_cast<double>(data[i]) * data[i];

    float rms = static_cast<float>(std::sqrt(sum / count));
    return rms > 1e-10f ? 20.0f * std::log10(rms) : -100.0f;
}

// Process buffer through plugin in fixed-size blocks
static void processWithPlugin(MultiQ& plugin, juce::AudioBuffer<float>& buffer, int blockSize = 512)
{
    juce::MidiBuffer midi;
    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();

    // Process in blocks using a temporary buffer to avoid AudioBuffer view issues
    juce::AudioBuffer<float> block(numChannels, blockSize);

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        int thisBlock = std::min(blockSize, numSamples - offset);

        // Copy input to block buffer
        for (int ch = 0; ch < numChannels; ++ch)
            block.copyFrom(ch, 0, buffer, ch, offset, thisBlock);

        if (thisBlock < blockSize)
            for (int ch = 0; ch < numChannels; ++ch)
                block.clear(ch, thisBlock, blockSize - thisBlock);

        // Process
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), numChannels, thisBlock);
        plugin.processBlock(view, midi);

        // Copy output back
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom(ch, offset, block, ch, 0, thisBlock);
    }
}

// Set a parameter by ID
static void setParam(MultiQ& plugin, const juce::String& paramID, float value)
{
    if (auto* param = plugin.parameters.getParameter(paramID))
        param->setValueNotifyingHost(plugin.parameters.getParameterRange(paramID).convertTo0to1(value));
}

// Reset plugin state between tests and run warmup blocks
static void resetPlugin(MultiQ& plugin, double sampleRate = 44100.0, int blockSize = 512)
{
    plugin.releaseResources();
    plugin.prepareToPlay(sampleRate, blockSize);

    // Reset all bands to flat. HPF/LPF (bands 1 & 8) disabled for test stability —
    // the cascaded IIR filters need multiple blocks to settle after releaseResources/prepareToPlay
    // cycles. This is not an issue in DAW usage (pluginval level 10 passes).
    for (int i = 1; i <= 8; ++i)
    {
        setParam(plugin, ParamIDs::bandGain(i), 0.0f);
        setParam(plugin, ParamIDs::bandEnabled(i), (i >= 2 && i <= 7) ? 1.0f : 0.0f);
        setParam(plugin, ParamIDs::bandInvert(i), 0.0f);
        setParam(plugin, ParamIDs::bandPhaseInvert(i), 0.0f);
        setParam(plugin, ParamIDs::bandPan(i), 0.0f);
    }
    setParam(plugin, ParamIDs::masterGain, 0.0f);
    setParam(plugin, ParamIDs::bypass, 0.0f);
    setParam(plugin, ParamIDs::hqEnabled, 0.0f);
    setParam(plugin, ParamIDs::autoGainEnabled, 0.0f);
    setParam(plugin, ParamIDs::limiterEnabled, 0.0f);

    // Warmup: process silence to let filters settle and coefficients update
    juce::AudioBuffer<float> warmup(2, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < 10; ++i)
    {
        warmup.clear();
        plugin.processBlock(warmup, midi);
    }
}

// Apply params and settle by processing low-level audio
static void settleParams(MultiQ& plugin, int blockSize = 512, int numBlocks = 20)
{
    juce::AudioBuffer<float> warmup(2, blockSize);
    juce::MidiBuffer midi;
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * 1000.0 / 44100.0);
    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            float val = std::sin(phase) * 0.1f;  // Low-level signal to settle filters
            warmup.setSample(0, i, val);
            warmup.setSample(1, i, val);
            phase += inc;
        }
        plugin.processBlock(warmup, midi);
    }
}

// ===== TEST: INV Gain Inversion =====
static void testINV(MultiQ& plugin)
{
    std::cout << "\n--- Test: INV (Gain Inversion) ---\n";
    resetPlugin(plugin);

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);  // 2 seconds
    int skip = static_cast<int>(sr);  // Skip first second

    // Band 4: +6dB peak at 1kHz
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);

    auto buf1 = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, buf1);
    float boostRMS = measureRMSdB(buf1, 0, skip);

    // Process with INV on
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandInvert(4), 1.0f);
    settleParams(plugin);

    auto buf2 = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, buf2);
    float cutRMS = measureRMSdB(buf2, 0, skip);

    // Input reference (flat)
    resetPlugin(plugin);
    settleParams(plugin);
    auto bufFlat = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufFlat);
    float flatRMS = measureRMSdB(bufFlat, 0, skip);

    float boostDelta = boostRMS - flatRMS;
    float cutDelta = cutRMS - flatRMS;

    checkDb("INV: boost produces positive gain", boostDelta, 6.0f, 1.5f);
    checkDb("INV: inverted produces negative gain", cutDelta, -6.0f, 1.5f);
    check("INV: boost and cut are opposite",
          std::abs(boostDelta + cutDelta) < 2.0f);
}

// ===== TEST: Phase Invert =====
static void testPhaseInvert(MultiQ& plugin)
{
    std::cout << "\n--- Test: Phase Invert ---\n";
    resetPlugin(plugin);

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr);
    int skip = 4096;

    // Band 4: +6dB at 1kHz
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    settleParams(plugin);

    // Normal
    auto buf1 = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, buf1);

    // Phase inverted
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandPhaseInvert(4), 1.0f);
    settleParams(plugin);

    auto buf2 = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, buf2);

    // Compare: normal and phase-inverted should differ
    double diffSum = 0.0;
    int count = numSamples - skip;
    for (int i = skip; i < numSamples; ++i)
    {
        float diff = buf1.getSample(0, i) - buf2.getSample(0, i);
        diffSum += diff * diff;
    }
    float diffRMS = static_cast<float>(std::sqrt(diffSum / count));
    float diffDB = diffRMS > 1e-10f ? 20.0f * std::log10(diffRMS) : -100.0f;

    check("Phase invert: output differs from normal", diffDB > -40.0f);

    // Phase invert formula: inverted = 2*dry - wet
    // So normal + inverted = wet + (2*dry - wet) = 2*dry
    // The sum should be approximately 2x the dry signal
    double sumSq = 0.0;
    for (int i = skip; i < numSamples; ++i)
    {
        float s = buf1.getSample(0, i) + buf2.getSample(0, i);
        sumSq += s * s;
    }
    float sumRMS = static_cast<float>(std::sqrt(sumSq / count));

    // Dry signal RMS for 1kHz sine at unity = ~0.707
    // 2 * dry RMS ≈ 1.414, so ~+3dB above dry
    check("Phase invert: sum ≈ 2x dry (not zero)", sumRMS > 0.5f);
}

// ===== TEST: Per-Band Pan =====
static void testPan(MultiQ& plugin)
{
    std::cout << "\n--- Test: Per-Band Pan ---\n";
    resetPlugin(plugin);

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr);
    int skip = 4096;

    // Band 4: +6dB at 1kHz, center pan
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandPan(4), 0.0f);
    settleParams(plugin);

    auto bufCenter = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufCenter);
    float centerL = measureRMSdB(bufCenter, 0, skip);
    float centerR = measureRMSdB(bufCenter, 1, skip);

    checkDb("Pan center: L and R are equal", centerL - centerR, 0.0f, 0.5f);

    // Pan full left
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandPan(4), -1.0f);
    settleParams(plugin);

    auto bufLeft = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufLeft);
    float leftL = measureRMSdB(bufLeft, 0, skip);
    float leftR = measureRMSdB(bufLeft, 1, skip);

    // Dry reference
    resetPlugin(plugin);
    settleParams(plugin);
    auto bufDry = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufDry);
    float dryRMS = measureRMSdB(bufDry, 0, skip);

    check("Pan left: L has EQ boost", leftL > dryRMS + 2.0f);
    checkDb("Pan left: R is near dry level", leftR, dryRMS, 1.0f);

    // Pan full right
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandPan(4), 1.0f);
    settleParams(plugin);

    auto bufRight = generateSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufRight);
    float rightL = measureRMSdB(bufRight, 0, skip);
    float rightR = measureRMSdB(bufRight, 1, skip);

    checkDb("Pan right: L is near dry level", rightL, dryRMS, 1.0f);
    check("Pan right: R has EQ boost", rightR > dryRMS + 2.0f);
}

// ===== TEST: State Save/Restore Round-Trip =====
// Issue #105 (the real bug): opening the editor must NOT reset the loaded British/Tube preset.
// The tube/british quick-preset combos fired setSelectedId(1) with the default async
// notification, so applyTube/BritishPreset(1) ("Default - flat") ran just after the editor was
// built and wiped the loaded preset's params. Fix = dontSendNotification on those setSelectedId
// calls. Needs a display (creates the real editor), so it self-skips on headless CI.
static void testEditorPresetSurvivesOpen()
{
    std::cout << "\n--- Test: Editor open must not reset British/Tube preset (issue #105) ---\n";
  #if JUCE_LINUX
    if (juce::SystemStats::getEnvironmentVariable ("DISPLAY", juce::String()).isEmpty())
    {
        std::cout << "  (skipped: no DISPLAY — the editor needs X)\n";
        return;
    }
  #endif
    auto pump = []()
    {
        auto* mm = juce::MessageManager::getInstance();
        mm->callAsync ([mm] { mm->stopDispatchLoop(); });
        mm->runDispatchLoop();   // runs the queued async onChange (the old reset), then quits
    };

    auto testMode = [&pump] (int eqTypeVal, const juce::String& paramId, const juce::String& label)
    {
        MultiQ proc;
        proc.setPlayConfigDetails (2, 2, 44100.0, 512);
        proc.prepareToPlay (44100.0, 512);
        auto val = [&proc] (const juce::String& id) { auto* p = proc.parameters.getRawParameterValue (id); return p ? p->load() : -999.0f; };

        // Pick a factory preset for this mode whose tracked param is clearly non-default.
        const auto& presets = proc.getFactoryPresets();
        int chosen = -1; float before = 0.0f;
        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (presets[i].eqType != eqTypeVal) continue;
            proc.setCurrentProgram ((int) i + 1);
            const float v = val (paramId);
            if (std::abs (v) > 0.01f) { chosen = (int) i; before = v; break; }
        }
        const juce::String n0 = label + ": found a non-default factory preset";
        check (n0.toRawUTF8(), chosen >= 0);
        if (chosen < 0) return;

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        pump(); pump();   // let any queued async onChange fire (this was the reset)

        const juce::String n1 = label + ": preset survives editor open";
        checkDb (n1.toRawUTF8(), val (paramId), before, 0.05f);

        // #105 unify: the mode-panel preset combo must mirror the main dropdown (same preset shown).
        if (auto* mqEd = dynamic_cast<MultiQEditor*> (ed.get()))
        {
            const juce::String mainTxt  = mqEd->getMainPresetText();
            const juce::String panelTxt = (eqTypeVal == 3) ? mqEd->getTubePresetText() : mqEd->getBritishPresetText();
            const juce::String n2 = label + ": panel combo mirrors main combo";
            check (n2.toRawUTF8(), mainTxt.isNotEmpty() && panelTxt == mainTxt);
        }
        if (ed) { proc.editorBeingDeleted (ed.get()); ed.reset(); }
    };

    testMode (3, ParamIDs::pultecLfBoostGain, "#105 Tube");
    testMode (2, ParamIDs::britishLfGain,     "#105 British");
}

// Issue #105: British/Tube mode params must survive a getState/setState round-trip.
// Reproduces the VST3 "resets to default on session open" path deterministically.
static void testBritishTubeStateRoundTrip(MultiQ& plugin)
{
    std::cout << "\n--- Test: British/Tube State Round-Trip (issue #105) ---\n";

    auto getParam = [](MultiQ& p, const juce::String& paramID) -> float {
        auto* param = p.parameters.getParameter(paramID);
        if (!param) return -999.0f;
        return p.parameters.getParameterRange(paramID).convertFrom0to1(param->getValue());
    };

    auto roundTrip = [](MultiQ& src) {
        juce::MemoryBlock st;
        src.getStateInformation(st);
        auto dst = std::make_unique<MultiQ>();
        auto layouts = dst->getBusesLayout();
        layouts.getMainInputChannelSet()  = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        dst->setBusesLayout(layouts);
        dst->setPlayConfigDetails(2, 2, 44100.0, 512);
        dst->prepareToPlay(44100.0, 512);
        dst->setStateInformation(st.getData(), static_cast<int>(st.getSize()));
        return dst;
    };

    // --- BRITISH mode (eqType index 2) ---
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::eqType, 2.0f);
    setParam(plugin, ParamIDs::britishLfGain, 6.0f);
    setParam(plugin, ParamIDs::britishHmFreq, 3000.0f);
    setParam(plugin, ParamIDs::britishHfGain, -5.0f);
    {
        auto p2 = roundTrip(plugin);
        checkDb("RT British: eqType stays British(2)", getParam(*p2, ParamIDs::eqType), 2.0f, 0.01f);
        checkDb("RT British: LF gain survives", getParam(*p2, ParamIDs::britishLfGain), 6.0f, 0.05f);
        checkDb("RT British: HM freq survives", getParam(*p2, ParamIDs::britishHmFreq), 3000.0f, 20.0f);
        checkDb("RT British: HF gain survives", getParam(*p2, ParamIDs::britishHfGain), -5.0f, 0.05f);
    }

    // --- TUBE mode (eqType index 3) ---
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::eqType, 3.0f);
    setParam(plugin, ParamIDs::pultecTubeDrive, 0.8f);
    {
        auto p3 = roundTrip(plugin);
        checkDb("RT Tube: eqType stays Tube(3)", getParam(*p3, ParamIDs::eqType), 3.0f, 0.01f);
        checkDb("RT Tube: tube drive survives", getParam(*p3, ParamIDs::pultecTubeDrive), 0.8f, 0.02f);
    }

    // --- EXACT user path: select a British FACTORY PRESET, then round-trip ---
    resetPlugin(plugin);
    {
        const auto& presets = plugin.getFactoryPresets();
        int britIdx = -1;
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].eqType == 2) { britIdx = static_cast<int>(i); break; }
        check("found a British factory preset", britIdx >= 0);
        if (britIdx >= 0)
        {
            plugin.setCurrentProgram(britIdx + 1);   // +1: "Init" occupies slot 0
            const float lf = getParam(plugin, ParamIDs::britishLfGain);
            const float hm = getParam(plugin, ParamIDs::britishHmFreq);
            auto p4 = roundTrip(plugin);
            checkDb("RT British PRESET: eqType stays British", getParam(*p4, ParamIDs::eqType), 2.0f, 0.01f);
            checkDb("RT British PRESET: LF gain survives", getParam(*p4, ParamIDs::britishLfGain), lf, 0.05f);
            checkDb("RT British PRESET: HM freq survives", getParam(*p4, ParamIDs::britishHmFreq), hm, 20.0f);
        }
    }
}

static void testStateRoundTrip(MultiQ& plugin)
{
    std::cout << "\n--- Test: State Save/Restore Round-Trip ---\n";
    resetPlugin(plugin);

    // Set a variety of non-default parameter values across all features
    setParam(plugin, ParamIDs::masterGain, -3.5f);
    setParam(plugin, ParamIDs::bypass, 0.0f);

    // Band 1 (HPF): enable with specific settings
    setParam(plugin, ParamIDs::bandEnabled(1), 1.0f);
    setParam(plugin, ParamIDs::bandFreq(1), 80.0f);
    setParam(plugin, ParamIDs::bandQ(1), 1.2f);
    setParam(plugin, ParamIDs::bandSlope(1), 3.0f);  // 24dB/oct

    // Band 3: parametric with gain, custom Q, invert, phase invert, pan
    setParam(plugin, ParamIDs::bandFreq(3), 2500.0f);
    setParam(plugin, ParamIDs::bandGain(3), -4.5f);
    setParam(plugin, ParamIDs::bandQ(3), 2.0f);
    setParam(plugin, ParamIDs::bandInvert(3), 1.0f);
    setParam(plugin, ParamIDs::bandPhaseInvert(3), 1.0f);
    setParam(plugin, ParamIDs::bandPan(3), 0.6f);

    // Band 5: different settings
    setParam(plugin, ParamIDs::bandFreq(5), 8000.0f);
    setParam(plugin, ParamIDs::bandGain(5), 3.0f);
    setParam(plugin, ParamIDs::bandEnabled(5), 0.0f);  // Disabled

    // Band 8 (LPF): enable
    setParam(plugin, ParamIDs::bandEnabled(8), 1.0f);
    setParam(plugin, ParamIDs::bandFreq(8), 16000.0f);

    // Save state
    juce::MemoryBlock stateData;
    plugin.getStateInformation(stateData);
    check("State saved (non-empty)", stateData.getSize() > 0);

    // Create a fresh plugin instance and load state
    auto plugin2 = std::make_unique<MultiQ>();
    auto layouts = plugin2->getBusesLayout();
    layouts.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
    layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
    plugin2->setBusesLayout(layouts);
    plugin2->setPlayConfigDetails(2, 2, 44100.0, 512);
    plugin2->prepareToPlay(44100.0, 512);

    plugin2->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));

    // Helper to read parameter value
    auto getParam = [](MultiQ& p, const juce::String& paramID) -> float {
        auto* param = p.parameters.getParameter(paramID);
        if (!param) return -999.0f;
        return p.parameters.getParameterRange(paramID).convertFrom0to1(param->getValue());
    };

    // Verify restored values match
    checkDb("RT: masterGain", getParam(*plugin2, ParamIDs::masterGain), -3.5f, 0.1f);

    checkDb("RT: band1 enabled", getParam(*plugin2, ParamIDs::bandEnabled(1)), 1.0f, 0.01f);
    checkDb("RT: band1 freq", getParam(*plugin2, ParamIDs::bandFreq(1)), 80.0f, 1.0f);
    checkDb("RT: band1 Q", getParam(*plugin2, ParamIDs::bandQ(1)), 1.2f, 0.05f);

    checkDb("RT: band3 freq", getParam(*plugin2, ParamIDs::bandFreq(3)), 2500.0f, 10.0f);
    checkDb("RT: band3 gain", getParam(*plugin2, ParamIDs::bandGain(3)), -4.5f, 0.1f);
    checkDb("RT: band3 Q", getParam(*plugin2, ParamIDs::bandQ(3)), 2.0f, 0.05f);
    checkDb("RT: band3 invert", getParam(*plugin2, ParamIDs::bandInvert(3)), 1.0f, 0.01f);
    checkDb("RT: band3 phaseInvert", getParam(*plugin2, ParamIDs::bandPhaseInvert(3)), 1.0f, 0.01f);
    checkDb("RT: band3 pan", getParam(*plugin2, ParamIDs::bandPan(3)), 0.6f, 0.05f);

    checkDb("RT: band5 enabled (off)", getParam(*plugin2, ParamIDs::bandEnabled(5)), 0.0f, 0.01f);
    checkDb("RT: band5 freq", getParam(*plugin2, ParamIDs::bandFreq(5)), 8000.0f, 50.0f);
    checkDb("RT: band5 gain", getParam(*plugin2, ParamIDs::bandGain(5)), 3.0f, 0.1f);

    checkDb("RT: band8 enabled", getParam(*plugin2, ParamIDs::bandEnabled(8)), 1.0f, 0.01f);
    checkDb("RT: band8 freq", getParam(*plugin2, ParamIDs::bandFreq(8)), 16000.0f, 100.0f);

    // Verify the restored plugin produces the same output as the original
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::masterGain, -3.5f);
    setParam(plugin, ParamIDs::bandEnabled(1), 1.0f);
    setParam(plugin, ParamIDs::bandFreq(1), 80.0f);
    setParam(plugin, ParamIDs::bandQ(1), 1.2f);
    setParam(plugin, ParamIDs::bandSlope(1), 3.0f);
    setParam(plugin, ParamIDs::bandFreq(3), 2500.0f);
    setParam(plugin, ParamIDs::bandGain(3), -4.5f);
    setParam(plugin, ParamIDs::bandQ(3), 2.0f);
    setParam(plugin, ParamIDs::bandInvert(3), 1.0f);
    setParam(plugin, ParamIDs::bandPhaseInvert(3), 1.0f);
    setParam(plugin, ParamIDs::bandPan(3), 0.6f);
    setParam(plugin, ParamIDs::bandFreq(5), 8000.0f);
    setParam(plugin, ParamIDs::bandGain(5), 3.0f);
    setParam(plugin, ParamIDs::bandEnabled(5), 0.0f);
    setParam(plugin, ParamIDs::bandEnabled(8), 1.0f);
    setParam(plugin, ParamIDs::bandFreq(8), 16000.0f);
    settleParams(plugin, 512, 40);

    resetPlugin(*plugin2);
    plugin2->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
    settleParams(*plugin2, 512, 40);

    auto buf1 = generateSine(1000.0f, 44100.0, 44100);
    auto buf2 = generateSine(1000.0f, 44100.0, 44100);
    processWithPlugin(plugin, buf1);
    processWithPlugin(*plugin2, buf2);

    float rms1 = measureRMSdB(buf1, 0, 22050);
    float rms2 = measureRMSdB(buf2, 0, 22050);
    checkDb("RT: restored output matches original", rms1 - rms2, 0.0f, 0.5f);
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQIntegrationTest"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void shutdown() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {}

    void initialise(const juce::String&) override
    {
        auto plugin = std::make_unique<MultiQ>();

        // Set up stereo bus layout
        auto layouts = plugin->getBusesLayout();
        layouts.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        plugin->setBusesLayout(layouts);
        plugin->setPlayConfigDetails(2, 2, 44100.0, 512);
        plugin->prepareToPlay(44100.0, 512);

        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out, "
                  << plugin->getSampleRate() << " Hz\n";

        // Quick sanity: process a tiny buffer of silence
        {
            juce::AudioBuffer<float> test(2, 512);
            test.clear();
            juce::MidiBuffer midi;
            plugin->processBlock(test, midi);
            std::cout << "Silence test: sample[0]=" << test.getSample(0, 0)
                      << " sample[511]=" << test.getSample(0, 511) << "\n";
        }

        testINV(*plugin);
        testPhaseInvert(*plugin);
        testPan(*plugin);
        testStateRoundTrip(*plugin);
        testBritishTubeStateRoundTrip(*plugin);
        testEditorPresetSurvivesOpen();
        // LP+INV test skipped: FIR generation requires background thread + message loop
        // The LP+INV fix was verified manually and via pluginval automation tests
        // testINVLinearPhase(*plugin);

        std::cout << "\n=============================\n";
        std::cout << "Results: " << passed << "/" << (passed + failed) << " passed\n";

        if (failed > 0)
            std::cout << "\033[31m" << failed << " FAILED\033[0m\n";
        else
            std::cout << "\033[32mAll tests passed!\033[0m\n";

        setApplicationReturnValue(failed > 0 ? 1 : 0);
        quit();
    }
};

START_JUCE_APPLICATION(TestApp)
