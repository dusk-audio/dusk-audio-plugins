/*
    Chord Analyzer host-side tests for the "Detected Chord" parameter (issue
    #267). These link the plugin's shared code, so they run against the real
    processor and the real parameter object rather than a stand-in:

      * every code in the parameter's range survives the float normalisation
        JUCE puts a parameter through, and getText() renders exactly what
        ChordAnalyzer::decodeLabel does;
      * a chord fed through processBlock reaches the parameter as text on the
        normal 20 Hz publish path, with no editor open;
      * Show Inversions changes that text with no note changing;
      * the output parameters stay out of the saved state;
      * "Detected Inversion" carries every ordinal the analyzer can report,
        including the ones added for issue #273.
*/

#include "../Source/PluginProcessor.h"
#include <iostream>

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

static juce::RangedAudioParameter* findParameter(juce::AudioProcessor& processor,
                                                 const juce::String& parameterID)
{
    for (auto* param : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == parameterID)
                return ranged;

    return nullptr;
}

//==============================================================================
// Every code, through the parameter's own normalisation and its own getText.
static void testParameterRoundTrip(juce::RangedAudioParameter& chordParam)
{
    std::cout << "\n--- Parameter round-trip ---\n";

    int normalisationFailures = 0;
    int textFailures = 0;
    int reverseFailures = 0;
    int firstNormalisationFailure = -1;
    juce::String firstTextFailure;

    for (int code = 0; code <= ChordAnalyzer::maxLabelCode; ++code)
    {
        const float normalised = chordParam.convertTo0to1(static_cast<float>(code));
        const int decoded = juce::roundToInt(chordParam.convertFrom0to1(normalised));

        if (decoded != code)
        {
            ++normalisationFailures;
            if (firstNormalisationFailure < 0)
                firstNormalisationFailure = code;
        }

        const juce::String text = chordParam.getText(normalised, 1024);
        const juce::String expected = ChordAnalyzer::decodeLabel(code);

        if (text != expected)
        {
            ++textFailures;
            if (firstTextFailure.isEmpty())
                firstTextFailure = "code " + juce::String(code)
                                 + ": expected \"" + expected + "\" got \"" + text + "\"";
        }

        if (chordParam.getText(chordParam.getValueForText(text), 1024) != text)
            ++reverseFailures;
    }

    std::cout << "       swept " << (ChordAnalyzer::maxLabelCode + 1)
              << " codes through convertTo0to1 / convertFrom0to1 / getText\n";

    if (firstNormalisationFailure >= 0)
        std::cout << "       first normalisation failure at code "
                  << firstNormalisationFailure << "\n";

    if (! firstTextFailure.isEmpty())
        std::cout << "       first text failure: " << firstTextFailure << "\n";

    check("every code survives the float normalisation", normalisationFailures == 0);
    check("getText matches decodeLabel for every code", textFailures == 0);
    std::cout << "       text/value/text conversion failures: " << reverseFailures << "\n";
    check("every displayed label survives text/value/text conversion", reverseFailures == 0);
    check("numeric label-code text remains accepted",
          chordParam.getText(chordParam.getValueForText("50000"), 1024) == ChordAnalyzer::decodeLabel(50000));
}

// The same sweep through the live parameter object, the way the timer writes
// it and the way a host reads it back.
static void testLiveParameterText(juce::RangedAudioParameter& chordParam)
{
    std::cout << "\n--- Parameter set / read back ---\n";

    auto* asFloat = dynamic_cast<juce::AudioParameterFloat*>(&chordParam);
    check("Detected Chord is an AudioParameterFloat", asFloat != nullptr);

    if (asFloat == nullptr)
        return;

    // Not discrete: a discrete parameter of this size makes the AU wrapper
    // build one CFString per step at instantiation and report the parameter as
    // kAudioUnitParameterUnit_Indexed.
    check("Detected Chord is not discrete", ! asFloat->isDiscrete());
    check("Detected Chord is not boolean", ! asFloat->isBoolean());

    int failures = 0;
    juce::String firstFailure;

    for (int code = 0; code <= ChordAnalyzer::maxLabelCode; ++code)
    {
        *asFloat = static_cast<float>(code);

        const juce::String text = asFloat->getCurrentValueAsText();
        const juce::String expected = ChordAnalyzer::decodeLabel(code);

        if (text != expected)
        {
            ++failures;
            if (firstFailure.isEmpty())
                firstFailure = "code " + juce::String(code)
                             + ": expected \"" + expected + "\" got \"" + text + "\"";
        }
    }

    if (! firstFailure.isEmpty())
        std::cout << "       first failure: " << firstFailure << "\n";

    std::cout << "       set and read back " << (ChordAnalyzer::maxLabelCode + 1)
              << " codes through the live parameter\n";

    check("getCurrentValueAsText matches every code written", failures == 0);
}

//==============================================================================
// "Detected Inversion" gained four choices for issue #273: the 4th, 5th and 6th
// inversions the old interval-class numbering could not express, and a "Slash"
// for a bass the matched chord shape does not spell. They are appended, so a
// session saved before this keeps the meaning of indices 0..4.
static void testInversionChoices(juce::AudioProcessor& processor)
{
    std::cout << "\n--- Detected Inversion choices ---\n";

    auto* param = dynamic_cast<juce::AudioParameterChoice*>(
        findParameter(processor, ChordAnalyzerProcessor::PARAM_DETECTED_INVERSION));

    check("Detected Inversion is a choice parameter", param != nullptr);

    if (param == nullptr)
        return;

    const juce::StringArray expected{ "-", "Root", "1st", "2nd", "3rd",
                                      "4th", "5th", "6th", "Slash" };

    std::cout << "       choices: " << param->choices.joinIntoString(" ") << "\n";

    check("Detected Inversion has one choice per ordinal plus the slash bass",
          param->choices.size() == kInversionSlashBass + 2);
    check("Detected Inversion choices read as declared", param->choices == expected);
    check("the choices existing sessions address are unchanged",
          param->choices[0] == "-" && param->choices[1] == "Root"
           && param->choices[2] == "1st" && param->choices[3] == "2nd"
           && param->choices[4] == "3rd");
}

//==============================================================================
// The saved state must not carry detection results: they are outputs, and the
// existing four are added straight to the processor rather than to the APVTS
// for exactly that reason.
static void testStateExcludesOutputs(ChordAnalyzerProcessor& processor)
{
    std::cout << "\n--- Saved state ---\n";

    const std::array<const char*, 5> ids{
        ChordAnalyzerProcessor::PARAM_KEY_ROOT, ChordAnalyzerProcessor::PARAM_KEY_MODE,
        ChordAnalyzerProcessor::PARAM_SUGGESTION_LEVEL, ChordAnalyzerProcessor::PARAM_SHOW_INVERSIONS,
        ChordAnalyzerProcessor::PARAM_RESPECT_SUSTAIN};
    // Seed every transient output so excluding them cannot pass merely because
    // all five happen to be zero. Internal publishing must remain legal.
    bool outputsPopulated = true;
    for (const char* id : {ChordAnalyzerProcessor::PARAM_DETECTED_ROOT, ChordAnalyzerProcessor::PARAM_DETECTED_QUALITY,
                          ChordAnalyzerProcessor::PARAM_DETECTED_BASS, ChordAnalyzerProcessor::PARAM_DETECTED_INVERSION,
                          ChordAnalyzerProcessor::PARAM_DETECTED_CHORD})
    {
        auto* parameter = findParameter(processor, id);
        parameter->setValueNotifyingHost(0.5f);
        outputsPopulated &= parameter->getValue() > 0.0f;
    }
    check("engine can publish nonzero values to all five transient outputs", outputsPopulated);
    const std::array<float, 5> expected{6.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    std::array<float, 5> original{};
    for (size_t i = 0; i < ids.size(); ++i)
    {
        auto* parameter = processor.getAPVTS().getParameter(ids[i]);
        original[i] = parameter->getValue();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(expected[i]));
    }

    juce::MemoryBlock state;
    processor.getStateInformation(state);

    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(),
                                                      static_cast<int>(state.getSize()));

    check("state saves as XML", xml != nullptr);

    if (xml == nullptr)
        return;

    const juce::String text = xml->toString();

    check("state does not carry Detected Chord", ! text.contains("detectedChord"));
    check("state does not carry the other detection outputs",
          ! text.contains("detectedRoot") && ! text.contains("detectedQuality")
          && ! text.contains("detectedBass") && ! text.contains("detectedInversion"));
    check("state does carry the real parameters", text.contains("showInversions"));

    ChordAnalyzerProcessor restored;
    struct LoadProbe : juce::AudioProcessorListener
    {
        LoadProbe(const std::array<const char*, 5>& names, const std::array<float, 5>& values)
            : ids(names), expected(values) {}
        const std::array<const char*, 5>& ids;
        const std::array<float, 5>& expected;
        int notifications = 0;
        bool complete = true;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor* p, const ChangeDetails& details) override
        {
            if (!details.programChanged) return;
            ++notifications;
            auto& loaded = static_cast<ChordAnalyzerProcessor&>(*p);
            for (size_t i = 0; i < ids.size(); ++i)
                complete &= loaded.getAPVTS().getRawParameterValue(ids[i])->load() == expected[i];
            complete &= !details.latencyChanged && !details.parameterInfoChanged;
        }
    } probe(ids, expected);
    restored.addListener(&probe);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    bool inputsMatch = true;
    for (size_t i = 0; i < ids.size(); ++i)
        inputsMatch &= restored.getAPVTS().getRawParameterValue(ids[i])->load() == expected[i];
    check("all five non-default inputs reload into a fresh instance", inputsMatch);
    check("successful state load notifies the host once after all values are available",
          probe.notifications == 1 && probe.complete);
    bool outputsEmpty = true;
    for (const char* id : {ChordAnalyzerProcessor::PARAM_DETECTED_ROOT, ChordAnalyzerProcessor::PARAM_DETECTED_QUALITY,
                          ChordAnalyzerProcessor::PARAM_DETECTED_BASS, ChordAnalyzerProcessor::PARAM_DETECTED_INVERSION,
                          ChordAnalyzerProcessor::PARAM_DETECTED_CHORD})
        outputsEmpty &= findParameter(restored, id)->getValue() == 0.0f;
    check("loading state into a fresh instance leaves every detection output at its own default", outputsEmpty);
    juce::MemoryBlock resaved;
    restored.getStateInformation(resaved);
    check("save/load/save is byte-for-byte reproducible", state == resaved);

    const int validNotifications = probe.notifications;
    restored.setStateInformation(nullptr, 0);
    juce::XmlElement wrongType("OtherPluginState");
    juce::MemoryBlock wrongState;
    juce::AudioProcessor::copyXmlToBinary(wrongType, wrongState);
    restored.setStateInformation(wrongState.getData(), static_cast<int>(wrongState.getSize()));
    restored.getStateInformation(resaved);
    check("invalid or foreign state does not notify the host or change saved inputs",
          probe.notifications == validNotifications && state == resaved);
    restored.removeListener(&probe);

    for (size_t i = 0; i < ids.size(); ++i)
        processor.getAPVTS().getParameter(ids[i])->setValueNotifyingHost(original[i]);
}

//==============================================================================
// Drives the real publish path: MIDI in through processBlock, the processor's
// own 20 Hz timer out to the parameter. Steps run on the message thread so the
// timer gets to fire between them.
class PublishDriver : private juce::Timer
{
public:
    PublishDriver(ChordAnalyzerProcessor& p,
                  juce::RangedAudioParameter& chordParam,
                  juce::RangedAudioParameter& inversionParam)
        : processor(p), param(chordParam), inversion(inversionParam)
    {
        startTimer(150);
    }

private:
    void sendNotes(const std::vector<int>& on, const std::vector<int>& off)
    {
        juce::MidiBuffer midi;
        int sample = 0;

        for (int note : off)
            midi.addEvent(juce::MidiMessage::noteOff(1, note), sample++);

        for (int note : on)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), sample++);

        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        processor.processBlock(buffer, midi);
    }

    void setShowInversions(bool shouldShow)
    {
        auto* p = processor.getAPVTS().getParameter(ChordAnalyzerProcessor::PARAM_SHOW_INVERSIONS);
        p->setValueNotifyingHost(shouldShow ? 1.0f : 0.0f);
    }

    void timerCallback() override
    {
        switch (step++)
        {
            case 0:
                setShowInversions(true);
                sendNotes({ 60, 64, 67, 71 }, {});
                break;

            case 1:
                check("Cmaj7 reaches the host parameter as text",
                      param.getCurrentValueAsText() == "Cmaj7");
                std::cout << "       after C E G B: \"" << param.getCurrentValueAsText() << "\"\n";
                sendNotes({ 52 }, {});   // add E below the C: Cmaj7 over an E bass
                break;

            case 2:
                check("first inversion publishes the slash bass",
                      param.getCurrentValueAsText() == "Cmaj7/E");
                std::cout << "       after adding an E bass: \""
                          << param.getCurrentValueAsText() << "\"\n";
                setShowInversions(false);
                break;

            case 3:
                check("Show Inversions off drops the slash with no note changing",
                      param.getCurrentValueAsText() == "Cmaj7");
                std::cout << "       Show Inversions off: \"" << param.getCurrentValueAsText() << "\"\n";
                setShowInversions(true);
                break;

            case 4:
                check("Show Inversions back on restores the slash",
                      param.getCurrentValueAsText() == "Cmaj7/E");
                sendNotes({ 78 }, { 52, 60, 64, 67, 71 });   // lone F#5
                break;

            case 5:
                check("a single note publishes its octave",
                      param.getCurrentValueAsText() == "F#5");
                std::cout << "       single note: \"" << param.getCurrentValueAsText() << "\"\n";
                sendNotes({}, { 78 });
                break;

            case 6:
                check("no notes publishes \"-\"", param.getCurrentValueAsText() == "-");
                check("no notes publishes no inversion",
                      inversion.getCurrentValueAsText() == "-");
                // The voicing from issue #273: D3 E3 G3 Bb3 C4, a C9 over its 9th.
                sendNotes({ 50, 52, 55, 58, 60 }, {});
                break;

            case 7:
                check("the 9th in the bass publishes as a 4th inversion",
                      inversion.getCurrentValueAsText() == "4th");
                std::cout << "       D3 E3 G3 Bb3 C4: \"" << param.getCurrentValueAsText()
                          << "\", inversion \"" << inversion.getCurrentValueAsText() << "\"\n";
                check("the same voicing still publishes its label",
                      param.getCurrentValueAsText() == "C9/D");
                sendNotes({ 54, 60, 64, 67 }, { 50, 52, 55, 58, 60 });
                break;

            case 8:
                check("a bass the chord does not spell publishes as Slash",
                      inversion.getCurrentValueAsText() == "Slash");
                std::cout << "       F#2 C4 E4 G4: \"" << param.getCurrentValueAsText()
                          << "\", inversion \"" << inversion.getCurrentValueAsText() << "\"\n";
                sendNotes({ 52, 60, 64, 67 }, { 54 });
                break;

            case 9:
                check("the third in the bass still publishes as a 1st inversion",
                      inversion.getCurrentValueAsText() == "1st");
                std::cout << "       E3 C4 E4 G4: \"" << param.getCurrentValueAsText()
                          << "\", inversion \"" << inversion.getCurrentValueAsText() << "\"\n";
                sendNotes({}, { 52, 60, 64, 67 });
                break;

            default:
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
        }
    }

    ChordAnalyzerProcessor& processor;
    juce::RangedAudioParameter& param;
    juce::RangedAudioParameter& inversion;
    int step = 0;
};

//==============================================================================
int main()
{
    std::cout << "=== Chord Analyzer Detected Chord parameter tests ===\n";

    juce::ScopedJuceInitialiser_GUI juceInit;

    ChordAnalyzerProcessor processor;
    processor.prepareToPlay(48000.0, 512);

    auto* chordParam = findParameter(processor, ChordAnalyzerProcessor::PARAM_DETECTED_CHORD);
    check("Detected Chord parameter exists", chordParam != nullptr);

    if (chordParam == nullptr)
    {
        std::cout << "\n=== Results: " << passed << " passed, " << (failed + 1) << " failed ===\n";
        return 1;
    }

    check("Detected Chord is named for the host", chordParam->getName(64) == "Detected Chord");
    check("Detected Chord range tops out at maxLabelCode",
          juce::roundToInt(chordParam->getNormalisableRange().end) == ChordAnalyzer::maxLabelCode);

    // The plugin builds with JUCE_FORCE_USE_LEGACY_PARAM_IDS, so a VST3 host
    // addresses parameters by index. Anything but appending renumbers the
    // existing ones and breaks saved automation, so pin the order.
    std::cout << "       parameter order:";
    for (auto* param : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        std::cout << " " << param->getParameterIndex() << "="
                  << (ranged != nullptr ? ranged->paramID : param->getName(32));
    }
    std::cout << "\n";

    check("Detected Chord was appended after the existing parameters",
          processor.getParameters().getLast() == chordParam);

    auto* inversionParam = findParameter(processor,
                                         ChordAnalyzerProcessor::PARAM_DETECTED_INVERSION);
    check("Detected Inversion parameter exists", inversionParam != nullptr);

    if (inversionParam == nullptr)
    {
        std::cout << "\n=== Results: " << passed << " passed, " << (failed + 1) << " failed ===\n";
        return 1;
    }

    testParameterRoundTrip(*chordParam);
    testLiveParameterText(*chordParam);
    testInversionChoices(processor);
    testStateExcludesOutputs(processor);

    std::cout << "\n--- Publish path ---\n";
    PublishDriver driver(processor, *chordParam, *inversionParam);
    juce::MessageManager::getInstance()->runDispatchLoop();

    processor.releaseResources();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
