// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQPlugin.cpp — DPF shell around the framework-free FourKEQDSP core.

#include "DistrhoPlugin.hpp"
#include "FourKEQAccess.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"

START_NAMESPACE_DISTRHO

class FourKEQPlugin : public Plugin
{
public:
    FourKEQPlugin()
        : Plugin(kParamCount, kNumFactoryPresets, 0)
    {
        // Seed the value mirror with the parameter defaults.
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            Parameter p;
            initParameter(i, p);
            values[i] = p.ranges.def;
        }
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
               "high/low filters, Brown/Black voicings and console saturation, "
               "oversampled for cramp-free high-frequency response.";
    }
    const char* getMaker() const override    { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override  { return "GPL-3.0-or-later"; }
    uint32_t    getVersion() const override  { return d_version(2, 0, 0); }
    int64_t     getUniqueId() const override { return d_cconst('D', 's', 'F', 'q'); } // DsFq

    //--- parameters -----------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        auto rng = [&p](float def, float min, float max) { p.ranges.def = def; p.ranges.min = min; p.ranges.max = max; };
        auto boolean = [&p]() { p.hints |= kParameterIsBoolean | kParameterIsInteger; };

        switch (index)
        {
        case kHpfFreq:   p.name = "HPF Frequency"; p.symbol = "hpf_freq"; p.unit = "Hz"; rng(20, 20, 500); break;
        case kHpfEnabled:boolean(); p.name = "HPF Enabled"; p.symbol = "hpf_enabled"; rng(0, 0, 1); break;
        case kLpfFreq:   p.name = "LPF Frequency"; p.symbol = "lpf_freq"; p.unit = "Hz"; rng(20000, 3000, 20000); break;
        case kLpfEnabled:boolean(); p.name = "LPF Enabled"; p.symbol = "lpf_enabled"; rng(0, 0, 1); break;
        case kLfGain:    p.name = "LF Gain"; p.symbol = "lf_gain"; p.unit = "dB"; rng(0, -20, 20); break;
        case kLfFreq:    p.name = "LF Frequency"; p.symbol = "lf_freq"; p.unit = "Hz"; rng(100, 30, 480); break;
        case kLfBell:    boolean(); p.name = "LF Bell Mode"; p.symbol = "lf_bell"; rng(0, 0, 1); break;
        case kLmGain:    p.name = "LM Gain"; p.symbol = "lm_gain"; p.unit = "dB"; rng(0, -20, 20); break;
        case kLmFreq:    p.name = "LM Frequency"; p.symbol = "lm_freq"; p.unit = "Hz"; rng(600, 200, 2500); break;
        case kLmQ:       p.name = "LM Q"; p.symbol = "lm_q"; rng(0.7f, 0.4f, 4.0f); break;
        case kHmGain:    p.name = "HM Gain"; p.symbol = "hm_gain"; p.unit = "dB"; rng(0, -20, 20); break;
        case kHmFreq:    p.name = "HM Frequency"; p.symbol = "hm_freq"; p.unit = "Hz"; rng(2000, 600, 7000); break;
        case kHmQ:       p.name = "HM Q"; p.symbol = "hm_q"; rng(0.7f, 0.4f, 4.0f); break;
        case kHfGain:    p.name = "HF Gain"; p.symbol = "hf_gain"; p.unit = "dB"; rng(0, -20, 20); break;
        case kHfFreq:    p.name = "HF Frequency"; p.symbol = "hf_freq"; p.unit = "Hz"; rng(8000, 1500, 16000); break;
        case kHfBell:    boolean(); p.name = "HF Bell Mode"; p.symbol = "hf_bell"; rng(0, 0, 1); break;
        case kEqType:    p.hints |= kParameterIsInteger; p.name = "EQ Type"; p.symbol = "eq_type"; rng(0, 0, 1);
                         p.enumValues.count = 2; p.enumValues.restrictedMode = true;
                         { auto* e = new ParameterEnumerationValue[2];
                           e[0] = ParameterEnumerationValue(0.f, kEqTypeLabels[0]);
                           e[1] = ParameterEnumerationValue(1.f, kEqTypeLabels[1]);
                           p.enumValues.values = e; } break;
        case kBypass:    p.initDesignation(kParameterDesignationBypass); break;
        case kInputGain: p.name = "Input Gain"; p.symbol = "input_gain"; p.unit = "dB"; rng(0, -12, 12); break;
        case kOutputGain:p.name = "Output Gain"; p.symbol = "output_gain"; p.unit = "dB"; rng(0, -12, 12); break;
        case kSaturation:p.name = "Saturation"; p.symbol = "saturation"; p.unit = "%"; rng(0, 0, 100); break;
        case kOversampling: p.hints |= kParameterIsInteger; p.name = "Oversampling"; p.symbol = "oversampling"; rng(0, 0, 2);
                         p.enumValues.count = 3; p.enumValues.restrictedMode = true;
                         { auto* e = new ParameterEnumerationValue[3];
                           e[0] = ParameterEnumerationValue(0.f, kOversampleLabels[0]);
                           e[1] = ParameterEnumerationValue(1.f, kOversampleLabels[1]);
                           e[2] = ParameterEnumerationValue(2.f, kOversampleLabels[2]);
                           p.enumValues.values = e; } break;
        case kMsMode:    boolean(); p.name = "M/S Mode"; p.symbol = "ms_mode"; rng(0, 0, 1); break;
        case kSpectrumPrePost: boolean(); p.name = "Spectrum Pre/Post"; p.symbol = "spectrum_prepost"; rng(0, 0, 1); break;
        case kAutoGain:  boolean(); p.name = "Auto Gain Compensation"; p.symbol = "auto_gain"; rng(1, 0, 1); break;
        case kShowGraph: p.hints = kParameterIsBoolean | kParameterIsInteger; // UI-only, not automatable
                         p.name = "Show Graph"; p.symbol = "show_graph"; rng(1, 0, 1); break;
        case kOutPeakL:  p.hints = kParameterIsAutomatable | kParameterIsOutput; p.name = "Out Peak L"; p.symbol = "out_peak_l"; rng(0, 0, 2); break;
        case kOutPeakR:  p.hints = kParameterIsAutomatable | kParameterIsOutput; p.name = "Out Peak R"; p.symbol = "out_peak_r"; rng(0, 0, 2); break;
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
        case kSaturation: dsp.setSaturation(value); break;
        case kOversampling: dsp.setOversampling((int)(value + 0.5f)); break;
        case kMsMode:     dsp.setMsMode(value > 0.5f); break;
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
        const FourKEQPreset& p = kFactoryPresets[index];
        setParameterValue(kLfGain, p.lfGain); setParameterValue(kLfFreq, p.lfFreq); setParameterValue(kLfBell, p.lfBell);
        setParameterValue(kLmGain, p.lmGain); setParameterValue(kLmFreq, p.lmFreq); setParameterValue(kLmQ, p.lmQ);
        setParameterValue(kHmGain, p.hmGain); setParameterValue(kHmFreq, p.hmFreq); setParameterValue(kHmQ, p.hmQ);
        setParameterValue(kHfGain, p.hfGain); setParameterValue(kHfFreq, p.hfFreq); setParameterValue(kHfBell, p.hfBell);
        setParameterValue(kHpfFreq, p.hpfFreq); setParameterValue(kLpfFreq, p.lpfFreq);
        setParameterValue(kSaturation, p.saturation); setParameterValue(kOutputGain, p.outputGain);
        setParameterValue(kInputGain, p.inputGain); setParameterValue(kEqType, p.eqType);
        // v2: engage the filters the preset implies (JUCE left them off -> inert).
        setParameterValue(kHpfEnabled, p.hpfFreq > 20.5f ? 1.0f : 0.0f);
        setParameterValue(kLpfEnabled, p.lpfFreq < 19999.0f ? 1.0f : 0.0f);
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
        // activate() and run() (DPF contract), and this fires while deactivated.
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

    //--- audio ----------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp.processBlock(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, (int)frames);
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
