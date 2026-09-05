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
      * the output parameters stay out of the saved state.
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
// The saved state must not carry detection results: they are outputs, and the
// existing four are added straight to the processor rather than to the APVTS
// for exactly that reason.
static void testStateExcludesOutputs(ChordAnalyzerProcessor& processor)
{
    std::cout << "\n--- Saved state ---\n";

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

    // Loading it back must leave the detection outputs alone.
    const juce::String before = processor.getParameters()[0]->getCurrentValueAsText();
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check("state reloads without changing an input parameter",
          processor.getParameters()[0]->getCurrentValueAsText() == before);
}

//==============================================================================
// Drives the real publish path: MIDI in through processBlock, the processor's
// own 20 Hz timer out to the parameter. Steps run on the message thread so the
// timer gets to fire between them.
class PublishDriver : private juce::Timer
{
public:
    PublishDriver(ChordAnalyzerProcessor& p, juce::RangedAudioParameter& chordParam)
        : processor(p), param(chordParam)
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
                break;

            default:
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
                break;
        }
    }

    ChordAnalyzerProcessor& processor;
    juce::RangedAudioParameter& param;
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

    testParameterRoundTrip(*chordParam);
    testLiveParameterText(*chordParam);
    testStateExcludesOutputs(processor);

    std::cout << "\n--- Publish path ---\n";
    PublishDriver driver(processor, *chordParam);
    juce::MessageManager::getInstance()->runDispatchLoop();

    processor.releaseResources();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
