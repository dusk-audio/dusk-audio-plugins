#include <JuceHeader.h>
#include "../MultiQ.h"
#include <iostream>
#include <iomanip>
#if JUCE_MAC
 #include <dlfcn.h>
#endif

// Pump the platform message loop so callAsync callbacks actually fire.
// On macOS, JUCE posts callAsync work to the CoreFoundation run loop.
// We call CFRunLoopRunInMode via dlsym to avoid pulling in CF headers
// (which define 'Point' and conflict with juce::Point<T>).
static void pumpMessageLoop(int milliseconds = 100)
{
#if JUCE_MAC
    typedef int      (*CFRunLoopRunInMode_t)(const void*, double, bool);
    static auto runFn  = (CFRunLoopRunInMode_t)dlsym(RTLD_DEFAULT, "CFRunLoopRunInMode");
    static auto modePtr= (const void**)        dlsym(RTLD_DEFAULT, "kCFRunLoopDefaultMode");
    if (runFn && modePtr)
        runFn(*modePtr, static_cast<double>(milliseconds) / 1000.0, false);
#endif
    juce::Thread::sleep(10);
}

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

static void checkDb(const char* name, float actual, float expected, float tol)
{
    bool ok = std::abs(actual - expected) <= tol;
    if (ok) {
        std::cout << "\033[32m[PASS]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(2) << actual
                  << " dB (expected " << expected << " ±" << tol << ")\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(2) << actual
                  << " dB (expected " << expected << " ±" << tol << ")\n";
        ++failed;
    }
}

// Read band gain as dB. Band gain range is -24..+24, normalized 0..1.
static float getBandGainDb(MultiQ& plugin, int bandNum)
{
    auto* param = plugin.parameters.getParameter(ParamIDs::bandGain(bandNum));
    if (!param) { std::cout << "  [WARN] bandGain(" << bandNum << ") param not found\n"; return -999.0f; }
    return param->getValue() * 48.0f - 24.0f;
}

static bool getBandEnabled(MultiQ& plugin, int bandNum)
{
    auto* param = plugin.parameters.getParameter(ParamIDs::bandEnabled(bandNum));
    if (!param) return false;
    return param->getValue() > 0.5f;
}

static float getEqType(MultiQ& plugin)
{
    auto* raw = plugin.parameters.getRawParameterValue(ParamIDs::eqType);
    return raw ? raw->load() : -1.0f;
}

static void setParam(MultiQ& plugin, const juce::String& id, float value)
{
    if (auto* param = plugin.parameters.getParameter(id))
        param->setValueNotifyingHost(
            plugin.parameters.getParameterRange(id).convertTo0to1(value));
}

static void setChoiceParam(MultiQ& plugin, const juce::String& id, int index)
{
    if (auto* param = plugin.parameters.getParameter(id))
    {
        int n = param->getNumSteps();
        if (n > 1)
            param->setValueNotifyingHost(
                static_cast<float>(index) / static_cast<float>(n - 1));
    }
}

static void initPlugin(MultiQ& plugin)
{
    auto layouts = plugin.getBusesLayout();
    layouts.getMainInputChannelSet()  = juce::AudioChannelSet::stereo();
    layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
    plugin.setBusesLayout(layouts);
    plugin.setPlayConfigDetails(2, 2, 44100.0, 512);
    plugin.prepareToPlay(44100.0, 512);
}

// ===== TEST: British → Digital transfer =====
static void testBritishTransfer(MultiQ& plugin)
{
    std::cout << "\n--- Test: British → Digital transfer (Punchy Drums values) ---\n";
    plugin.releaseResources();
    initPlugin(plugin);

    setChoiceParam(plugin, ParamIDs::eqType, static_cast<int>(EQType::British));

    // Set British "Punchy Drums" parameter values
    setParam(plugin, ParamIDs::britishHpfFreq,    60.0f);
    setParam(plugin, ParamIDs::britishHpfEnabled,  1.0f);
    setParam(plugin, ParamIDs::britishLpfFreq,  18000.0f);
    setParam(plugin, ParamIDs::britishLpfEnabled,  1.0f);
    setParam(plugin, ParamIDs::britishLfGain,      3.0f);
    setParam(plugin, ParamIDs::britishLfFreq,     80.0f);
    setParam(plugin, ParamIDs::britishLfBell,      0.0f);
    setParam(plugin, ParamIDs::britishLmGain,     -4.0f);
    setParam(plugin, ParamIDs::britishLmFreq,    350.0f);
    setParam(plugin, ParamIDs::britishLmQ,         1.8f);
    setParam(plugin, ParamIDs::britishHmGain,      4.0f);
    setParam(plugin, ParamIDs::britishHmFreq,   4000.0f);
    setParam(plugin, ParamIDs::britishHmQ,         1.2f);
    setParam(plugin, ParamIDs::britishHfGain,      2.0f);
    setParam(plugin, ParamIDs::britishHfFreq,   8000.0f);
    setParam(plugin, ParamIDs::britishHfBell,      0.0f);

    std::cout << "  Pre-transfer eqType=" << getEqType(plugin)
              << " britishLfGain=" << plugin.parameters.getRawParameterValue(ParamIDs::britishLfGain)->load()
              << "\n";
    check("Pre-transfer mode is British", getEqType(plugin) == 2.0f);

    // Simulate button click — this is the exact path the UI takes
    plugin.transferCurrentEQToDigital();

    // The callAsync to lower transferInProgress hasn't fired yet (no message loop here),
    // but band gains should be set and setCurrentProgram(0) should be blocked.
    // Manually lower the flag to allow checking the final state.
    plugin.transferInProgress.store(false);

    std::cout << "  Post-transfer eqType=" << getEqType(plugin) << "\n";
    std::cout << "  Band2 gain=" << getBandGainDb(plugin, 2)
              << " Band3 gain=" << getBandGainDb(plugin, 3)
              << " Band5 gain=" << getBandGainDb(plugin, 5)
              << " Band7 gain=" << getBandGainDb(plugin, 7) << "\n";

    check("Mode switched to Digital",    getEqType(plugin) == 0.0f);
    check("Band1 (HPF) enabled",         getBandEnabled(plugin, 1));
    check("Band2 enabled",               getBandEnabled(plugin, 2));
    checkDb("Band2 gain (+3 dB LF)",     getBandGainDb(plugin, 2),  3.0f, 0.5f);
    check("Band3 enabled",               getBandEnabled(plugin, 3));
    checkDb("Band3 gain (-4 dB LMF)",    getBandGainDb(plugin, 3), -4.0f, 0.5f);
    check("Band4 disabled",              !getBandEnabled(plugin, 4));
    check("Band5 enabled",               getBandEnabled(plugin, 5));
    checkDb("Band5 gain (+4 dB HMF)",    getBandGainDb(plugin, 5),  4.0f, 0.5f);
    check("Band6 disabled",              !getBandEnabled(plugin, 6));
    check("Band7 enabled",               getBandEnabled(plugin, 7));
    checkDb("Band7 gain (+2 dB HF)",     getBandGainDb(plugin, 7),  2.0f, 0.5f);
    check("Band8 (LPF) enabled",         getBandEnabled(plugin, 8));
}

// ===== TEST: Tube → Digital transfer =====
static void testTubeTransfer(MultiQ& plugin)
{
    std::cout << "\n--- Test: Tube → Digital transfer (Subtle Warmth values) ---\n";
    plugin.releaseResources();
    initPlugin(plugin);

    setChoiceParam(plugin, ParamIDs::eqType, static_cast<int>(EQType::Tube));
    setParam(plugin, ParamIDs::pultecLfBoostGain,      1.5f);
    setParam(plugin, ParamIDs::pultecLfAttenGain,      0.0f);
    setParam(plugin, ParamIDs::pultecLfBoostFreq,      3.0f);   // index 3 = 100 Hz
    setParam(plugin, ParamIDs::pultecHfBoostGain,      1.5f);
    setParam(plugin, ParamIDs::pultecHfBoostFreq,      5.0f);   // index 5 = 12 kHz
    setParam(plugin, ParamIDs::pultecHfBoostBandwidth, 0.5f);
    setParam(plugin, ParamIDs::pultecHfAttenGain,      0.5f);
    setParam(plugin, ParamIDs::pultecHfAttenFreq,      2.0f);   // index 2 = 20 kHz

    check("Pre-transfer mode is Tube", getEqType(plugin) == 3.0f);

    plugin.transferCurrentEQToDigital();
    plugin.transferInProgress.store(false);

    std::cout << "  Post-transfer eqType=" << getEqType(plugin) << "\n";
    std::cout << "  Band2 gain=" << getBandGainDb(plugin, 2)
              << " Band5 gain=" << getBandGainDb(plugin, 5)
              << " Band7 gain=" << getBandGainDb(plugin, 7) << "\n";

    check("Mode switched to Digital",           getEqType(plugin) == 0.0f);
    check("Band2 enabled",                      getBandEnabled(plugin, 2));
    // net LF = 1.5*1.4 - 0*1.75 = 2.1 dB
    checkDb("Band2 gain (~+2.1 dB LF)",         getBandGainDb(plugin, 2), 2.1f, 0.5f);
    check("Band5 enabled",                      getBandEnabled(plugin, 5));
    // HF boost = 1.5 * 1.8 = 2.7 dB
    checkDb("Band5 gain (~+2.7 dB HF boost)",   getBandGainDb(plugin, 5), 2.7f, 0.5f);
    check("Band7 enabled",                      getBandEnabled(plugin, 7));
    // HF atten = -0.5 * 1.6 = -0.8 dB
    checkDb("Band7 gain (~-0.8 dB HF atten)",   getBandGainDb(plugin, 7), -0.8f, 0.3f);
}

// ===== TEST: setCurrentProgram(0) from host does NOT reset params; resetToInit() does =====
static void testGuardBlocksReset(MultiQ& plugin)
{
    std::cout << "\n--- Test: setCurrentProgram(0) is benign; resetToInit() resets ---\n";
    plugin.releaseResources();
    initPlugin(plugin);

    setChoiceParam(plugin, ParamIDs::eqType, static_cast<int>(EQType::British));
    setParam(plugin, ParamIDs::britishLfGain, 6.0f);
    setParam(plugin, ParamIDs::britishHfGain, 3.0f);

    plugin.transferCurrentEQToDigital();
    plugin.transferInProgress.store(false);  // simulate callAsync completing

    std::cout << "  Band2 after transfer: " << getBandGainDb(plugin, 2) << " dB\n";
    check("Band2 set by transfer", getBandGainDb(plugin, 2) > 0.5f);

    // setCurrentProgram(0) is now a no-op for parameters (host bookkeeping call only).
    // It must NOT reset any band gains — that would interfere with transfers in all DAWs.
    plugin.setCurrentProgram(0);
    std::cout << "  Band2 after setCurrentProgram(0): " << getBandGainDb(plugin, 2) << " dB\n";
    check("setCurrentProgram(0) does not wipe band gains", getBandGainDb(plugin, 2) > 0.5f);

    // resetToInit() is the explicit UI-driven reset and DOES clear params
    plugin.resetToInit();
    std::cout << "  Band2 after resetToInit(): " << getBandGainDb(plugin, 2) << " dB\n";
    checkDb("resetToInit() resets Band2 to 0 dB", getBandGainDb(plugin, 2), 0.0f, 0.2f);
}

// ===== TEST: band gains survive the async message loop after transfer =====
// This is the closest we can get to a real DAW test without a running host.
// It pumps the JUCE message loop so callAsync callbacks actually fire,
// then verifies band gains haven't been wiped by any async callback.
static void testAsyncSurvival(MultiQ& plugin)
{
    std::cout << "\n--- Test: band gains survive async message loop after transfer ---\n";
    plugin.releaseResources();
    initPlugin(plugin);

    setChoiceParam(plugin, ParamIDs::eqType, static_cast<int>(EQType::British));
    setParam(plugin, ParamIDs::britishLfGain,   3.0f);
    setParam(plugin, ParamIDs::britishLmGain,  -4.0f);
    setParam(plugin, ParamIDs::britishHmGain,   4.0f);
    setParam(plugin, ParamIDs::britishHfGain,   2.0f);
    setParam(plugin, ParamIDs::britishHpfFreq,  60.0f);
    setParam(plugin, ParamIDs::britishHpfEnabled, 1.0f);

    plugin.transferCurrentEQToDigital();

    // Pump the message loop — this fires callAsync(updatePresetSelector) and
    // callAsync(lowerTransferInProgress), just as a real DAW would process them.
    pumpMessageLoop(100);

    // Simulate host calling setCurrentProgram(0) AFTER the message loop settles.
    // With the new implementation this is a no-op for parameters.
    plugin.setCurrentProgram(0);

    std::cout << "  After message loop + setCurrentProgram(0):\n";
    std::cout << "    Band2=" << getBandGainDb(plugin, 2)
              << " Band3=" << getBandGainDb(plugin, 3)
              << " Band5=" << getBandGainDb(plugin, 5)
              << " Band7=" << getBandGainDb(plugin, 7) << "\n";

    check("Async: mode is Digital",        getEqType(plugin) == 0.0f);
    check("Async: transferInProgress=false", !plugin.transferInProgress.load());
    checkDb("Async: Band2 gain (+3 dB)",   getBandGainDb(plugin, 2),  3.0f, 0.5f);
    checkDb("Async: Band3 gain (-4 dB)",   getBandGainDb(plugin, 3), -4.0f, 0.5f);
    checkDb("Async: Band5 gain (+4 dB)",   getBandGainDb(plugin, 5),  4.0f, 0.5f);
    checkDb("Async: Band7 gain (+2 dB)",   getBandGainDb(plugin, 7),  2.0f, 0.5f);
    check("Async: Band1 (HPF) enabled",    getBandEnabled(plugin, 1));
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQTransferTest"; }
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
        initPlugin(*plugin);

        std::cout << "Multi-Q Transfer-to-Digital Tests\n";
        std::cout << "==================================\n";
        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out\n";

        testBritishTransfer(*plugin);
        testTubeTransfer(*plugin);
        testGuardBlocksReset(*plugin);
        testAsyncSurvival(*plugin);

        std::cout << "\n==================================\n";
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
