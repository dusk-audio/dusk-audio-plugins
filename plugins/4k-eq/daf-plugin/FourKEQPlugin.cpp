// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// FourKEQPlugin.cpp — DAF shell around the framework-free FourKEQDSP core.

#include "DistrhoPlugin.hpp"
#include "FourKEQAccess.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQPresetRuntime.hpp"
#include "FourKEQVersion.hpp"
#include "util/CrashLog.hpp"

START_NAMESPACE_DISTRHO

class FourKEQPlugin : public Plugin
{
    DuskCrashLog::ScopedRegistration crashLog_ { "4K EQ 2", FOURKEQ2_VERSION_STRING };

public:
    FourKEQPlugin()
        : Plugin(kParamCount, kNumFactoryPresets, 0)
    {
        // Seed the value mirror from the shared parameter table. (Calling
        // initParameter() here would repeat the full parameter setup and
        // re-allocate the kEqType / kOversampling enumeration arrays each pass
        // just to read ranges.def — the table is the single source of truth.)
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kFourKParams[i].def;
    }

    //--- same-process accessors for the UI bridge -----------------------------
    float inPeakL()  const noexcept { return dsp.getInputPeakL(); }
    float inPeakR()  const noexcept { return dsp.getInputPeakR(); }
    float outPeakLv() const noexcept { return dsp.getOutputPeakL(); }
    float outPeakRv() const noexcept { return dsp.getOutputPeakR(); }
    const duskaudio::SpectrumRing* preSpec()  const noexcept { return &dsp.preSpectrum(); }
    const duskaudio::SpectrumRing* postSpec() const noexcept { return &dsp.postSpectrum(); }

protected:
    //--- metadata -------------------------------------------------------------
    const char* getLabel() const override    { return "4K EQ 2"; }
    const char* getDescription() const override
    {
        return "British console EQ emulation: 4-band parametric EQ with "
               "high/low filters, Brown/Black voicings and native console nonlinearity, "
               "oversampled for cramp-free high-frequency response.";
    }
    const char* getMaker() const override    { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override  { return "GPL-3.0-or-later"; }
    // Version comes from the CMake project() VERSION via FourKEQVersion.hpp.
    uint32_t    getVersion() const override
    { return d_version(FOURKEQ2_VERSION_MAJOR, FOURKEQ2_VERSION_MINOR, FOURKEQ2_VERSION_PATCH); }
    int64_t     getUniqueId() const override { return d_cconst('D', 's', 'F', 'q'); } // DsFq

    //--- parameters -----------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index >= kParamCount)
            return;
        p.hints = kParameterIsAutomatable;
        // Symbol and ranges come from the shared table (FourKEQParams.hpp), the
        // same one the UI mirror and the user-preset file keys read — so the
        // host-visible symbols can never drift from the preset format. The
        // switch below only adds names, units, hints and enumerations.
        p.symbol     = kFourKParams[index].key;
        p.ranges.def = kFourKParams[index].def;
        p.ranges.min = kFourKParams[index].min;
        p.ranges.max = kFourKParams[index].max;
        auto boolean = [&p]() { p.hints |= kParameterIsBoolean | kParameterIsInteger; };

        switch (index)
        {
        case kHpfFreq:   p.name = "HPF Frequency"; p.unit = "Hz"; break;
        case kHpfEnabled:boolean(); p.name = "HPF Enabled"; break;
        case kLpfFreq:   p.name = "LPF Frequency"; p.unit = "Hz"; break;
        case kLpfEnabled:boolean(); p.name = "LPF Enabled"; break;
        case kLfGain:    p.name = "LF Gain"; p.unit = "dB"; break;
        case kLfFreq:    p.name = "LF Frequency"; p.unit = "Hz"; break;
        case kLfBell:    boolean(); p.name = "LF Bell Mode"; break;
        case kLmGain:    p.name = "LM Gain"; p.unit = "dB"; break;
        case kLmFreq:    p.name = "LM Frequency"; p.unit = "Hz"; break;
        case kLmQ:       p.name = "LM Q"; break;
        case kHmGain:    p.name = "HM Gain"; p.unit = "dB"; break;
        case kHmFreq:    p.name = "HM Frequency"; p.unit = "Hz"; break;
        case kHmQ:       p.name = "HM Q"; break;
        case kHfGain:    p.name = "HF Gain"; p.unit = "dB"; break;
        case kHfFreq:    p.name = "HF Frequency"; p.unit = "Hz"; break;
        case kHfBell:    boolean(); p.name = "HF Bell Mode"; break;
        case kEqType:    p.hints |= kParameterIsInteger; p.name = "EQ Type";
                         p.enumValues.count = 2; p.enumValues.restrictedMode = true;
                         { auto* e = new ParameterEnumerationValue[2];
                           e[0] = ParameterEnumerationValue(0.f, kEqTypeLabels[0]);
                           e[1] = ParameterEnumerationValue(1.f, kEqTypeLabels[1]);
                           p.enumValues.values = e; } break;
        case kBypass:    p.initDesignation(kParameterDesignationBypass); break;
        case kInputGain: p.name = "Input Gain"; p.unit = "dB"; break;
        case kOutputGain:p.name = "Output Gain"; p.unit = "dB"; break;
        case kSaturation:
            // ABI/state compatibility only. Removing this shipped index would
            // remap every parameter after it in existing sessions. DAF only
            // honours the hidden hint in some formats. Keep the shipped host
            // name as well as the index so existing automation and the parity
            // harness can still resolve it, even though it no longer acts on
            // the standalone 4K DSP.
            p.hints = kParameterIsHidden;
            p.name = "Saturation";
            p.unit = "%";
            break;
        case kOversampling: p.hints |= kParameterIsInteger; p.name = "Oversampling";
                         p.enumValues.count = 3; p.enumValues.restrictedMode = true;
                         { auto* e = new ParameterEnumerationValue[3];
                           e[0] = ParameterEnumerationValue(0.f, kOversampleLabels[0]);
                           e[1] = ParameterEnumerationValue(1.f, kOversampleLabels[1]);
                           e[2] = ParameterEnumerationValue(2.f, kOversampleLabels[2]);
                           p.enumValues.values = e; } break;
        case kMsMode:
            // ABI/state compatibility only. The modeled SSL EQ has no M/S mode,
            // so retain the shipped index without exposing or processing it.
            p.hints = kParameterIsHidden;
            p.name = "M/S Mode";
            break;
        case kSpectrumPrePost: boolean(); p.name = "Spectrum Pre/Post"; break;
        case kAutoGain:  boolean(); p.name = "Auto Gain Compensation"; break;
        case kShowGraph: p.hints = kParameterIsBoolean | kParameterIsInteger; // UI-only, not automatable
                         p.name = "Show Graph"; break;
        case kOutPeakL:  p.hints = kParameterIsAutomatable | kParameterIsOutput; p.name = "Out Peak L"; break;
        case kOutPeakR:  p.hints = kParameterIsAutomatable | kParameterIsOutput; p.name = "Out Peak R"; break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kOutPeakL: return dsp.getOutputPeakL();
        case kOutPeakR: return dsp.getOutputPeakR();
        default:        return index < kParamCount ? values[index] : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kNumInputParams) // output params are not settable
            return;
        values[index] = value;
        switch (index)
        {
        case kHpfFreq:    dsp.setHpfFreq(value); break;
        case kHpfEnabled: dsp.setHpfEnabled(value > 0.5f); break;
        case kLpfFreq:    dsp.setLpfFreq(value); break;
        case kLpfEnabled: dsp.setLpfEnabled(value > 0.5f); break;
        case kLfGain:     dsp.setLfGain(value); break;
        case kLfFreq:     dsp.setLfFreq(value); break;
        case kLfBell:     dsp.setLfBell(value > 0.5f); break;
        case kLmGain:     dsp.setLmGain(value); break;
        case kLmFreq:     dsp.setLmFreq(value); break;
        case kLmQ:        dsp.setLmQ(value); break;
        case kHmGain:     dsp.setHmGain(value); break;
        case kHmFreq:     dsp.setHmFreq(value); break;
        case kHmQ:        dsp.setHmQ(value); break;
        case kHfGain:     dsp.setHfGain(value); break;
        case kHfFreq:     dsp.setHfFreq(value); break;
        case kHfBell:     dsp.setHfBell(value > 0.5f); break;
        case kEqType:     dsp.setEqType((int)(value + 0.5f)); break;
        case kBypass:     dsp.setBypass(value > 0.5f); break;
        case kInputGain:  dsp.setInputGainDb(value); break;
        case kOutputGain: dsp.setOutputGainDb(value); break;
        case kSaturation:
            // The SSL EQ has no independent drive/mix control. Its calibrated
            // native nonlinearity is the core's 0% reference state; Input Gain
            // controls the level presented to that nonlinear path.
            dsp.setSaturation(0.0f);
            break;
        case kOversampling: dsp.setOversampling((int)(value + 0.5f)); break;
        case kMsMode:     dsp.setMsMode(false); break;
        case kSpectrumPrePost: break; // UI-only (analyzer source select)
        case kShowGraph:  break;      // UI-only (graph collapse), persisted in state
        case kAutoGain:   dsp.setAutoGain(value > 0.5f); break;
        }
    }

    //--- programs -------------------------------------------------------------
    void initProgramName(uint32_t index, String& programName) override
    {
        if (index < (uint32_t)kNumFactoryPresets)
            programName = kFactoryPresets[index].name;
    }

    void loadProgram(uint32_t index) override
    {
        if (index >= (uint32_t)kNumFactoryPresets)
            return;
        forEachFourKEQFactoryPresetParam((int)index,
            [this](uint32_t param, float value) { setParameterValue(param, value); });
    }

    //--- lifecycle ------------------------------------------------------------
    void activate() override
    {
        dsp.prepare(getSampleRate(), (int)getBufferSize());
        pushAllParams();
        updateLatency();
    }
    void deactivate() override { dsp.reset(); }
    void sampleRateChanged(double newSampleRate) override
    {
        dsp.prepare(newSampleRate, (int)getBufferSize());
        pushAllParams();
        // No updateLatency() here: setLatency() is only valid in the ctor,
        // activate() and run() (DAF contract), and this fires while deactivated.
        // The host reactivates after a rate change -> activate()/run() refresh it.
    }
    // Reconfigure the DSP (scratch/oversampler sizing) when the host changes its
    // buffer size without a full restart, so the prepared max block never goes stale.
    void bufferSizeChanged(uint32_t newBufferSize) override
    {
        dsp.prepare(getSampleRate(), (int)newBufferSize);
        pushAllParams();
        // (latency refreshed by activate()/run(), never here — see sampleRateChanged)
    }
    void ioChanged(uint16_t numInputs, uint16_t numOutputs) override
    {
        // DISTRHO_PLUGIN_EXTRA_IO permits only matched mono or stereo layouts.
        // DAF calls this while deactivated, so the audio thread sees a stable
        // channel count when processing resumes.
        activeChannels = (numInputs == 1 && numOutputs == 1) ? 1 : 2;
    }

    //--- audio ----------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp.processBlock(inputs, outputs, activeChannels, (int)frames);
        updateLatency();
    }

private:
    void pushAllParams()
    {
        for (uint32_t i = 0; i < kNumInputParams; ++i)
            setParameterValue(i, values[i]);
    }
    void updateLatency()
    {
        const uint32_t lat = (uint32_t)dsp.getLatencySamples();
        if (lat != lastLatency) { setLatency(lat); lastLatency = lat; }
    }

    duskaudio::FourKEQDSP dsp;
    float values[kParamCount] = {};
    uint32_t lastLatency = 0xffffffffu;
    uint16_t activeChannels = DISTRHO_PLUGIN_NUM_INPUTS;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQPlugin)
};

Plugin* createPlugin() { return new FourKEQPlugin(); }

END_NAMESPACE_DISTRHO

//--- same-process UI accessors (see FourKEQAccess.hpp) -----------------------
using DISTRHO_NAMESPACE::FourKEQPlugin;
static FourKEQPlugin* asPlugin(void* p) { return static_cast<FourKEQPlugin*>(p); }

float fourKEQGetInputPeakL(void* p) noexcept  { return p ? asPlugin(p)->inPeakL() : 0.0f; }
float fourKEQGetInputPeakR(void* p) noexcept  { return p ? asPlugin(p)->inPeakR() : 0.0f; }
float fourKEQGetOutputPeakL(void* p) noexcept { return p ? asPlugin(p)->outPeakLv() : 0.0f; }
float fourKEQGetOutputPeakR(void* p) noexcept { return p ? asPlugin(p)->outPeakRv() : 0.0f; }
const duskaudio::SpectrumRing* fourKEQGetPreSpectrum(void* p) noexcept  { return p ? asPlugin(p)->preSpec() : nullptr; }
const duskaudio::SpectrumRing* fourKEQGetPostSpectrum(void* p) noexcept { return p ? asPlugin(p)->postSpec() : nullptr; }
