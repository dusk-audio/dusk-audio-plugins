// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachinePlugin.cpp — DPF shell around the framework-free TapeMachineDSP core.

#include "DistrhoPlugin.hpp"
#include "TapeMachineAccess.hpp"
#include "TapeMachineParams.hpp"
#include "TapeMachineDSP.hpp"

#include <atomic>
#include <cmath>

START_NAMESPACE_DISTRHO

class TapeMachinePlugin : public Plugin
{
public:
    TapeMachinePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i].store(kTmParams[i].def, std::memory_order_relaxed);
    }

    // same-process meter access for the UI bridge (TapeMachineAccess.hpp)
    float getVuLForUI() const noexcept { return dsp.getVuL(); }
    float getVuRForUI() const noexcept { return dsp.getVuR(); }

protected:
    //--- metadata --------------------------------------------------------------
    const char* getLabel() const override    { return "TapeMachine2"; }
    const char* getDescription() const override
    {
        return "Reel-to-reel tape machine emulation: Jiles-Atherton tape "
               "saturation, head/gap EQ, wow & flutter, hiss, NAB/CCIR/AES.";
    }
    const char* getMaker() const override    { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override  { return "GPL-3.0-or-later"; }
    uint32_t    getVersion() const override  { return d_version(2, 0, 0); }
    int64_t     getUniqueId() const override { return d_cconst('D', 's', 'T', 'M'); } // matches DISTRHO_PLUGIN_UNIQUE_ID (DsTM)

    //--- parameters ------------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index >= kParamCount)
            return;
        const TmParam& d = kTmParams[index];

        switch (d.kind)
        {
        case 'b': // host-integrated bypass (shown as the UI POWER/tape-stop)
            p.initDesignation(kParameterDesignationBypass);
            return;
        case 'o': // output-only meter
            p.hints = kParameterIsAutomatable | kParameterIsOutput;
            break;
        case 'c': // discrete choice
            p.hints = kParameterIsAutomatable | kParameterIsInteger;
            break;
        default:  // 'f' linear / 'g' skewed float (UI owns the skew feel)
            p.hints = kParameterIsAutomatable;
            break;
        }

        p.name   = d.name;
        p.symbol = d.id;
        p.unit   = d.unit;
        p.ranges.def = d.def;
        p.ranges.min = d.min;
        p.ranges.max = d.max;

        // Attach human-readable labels for choice parameters (generic hosts).
        if (d.kind == 'c' && d.choices != nullptr && d.numChoices > 0)
        {
            p.enumValues.count = (uint8_t)d.numChoices;
            p.enumValues.restrictedMode = true;
            auto* const vals = new ParameterEnumerationValue[d.numChoices];
            for (int i = 0; i < d.numChoices; ++i)
            {
                vals[i].value = (float)i;
                vals[i].label = d.choices[i];
            }
            p.enumValues.values = vals; // DPF takes ownership (deletes[] on destroy)
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == kParamVuL) return dsp.getVuL();
        if (index == kParamVuR) return dsp.getVuR();
        return index < kParamCount ? values[index].load(std::memory_order_relaxed) : 0.0f;
    }

    // DSP setters are atomic stores — safe from whichever thread DPF uses.
    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamVuL) // output/meter params are not settable
            return;
        values[index].store(value, std::memory_order_relaxed);
        applyToDsp(index, value);
    }

    //--- lifecycle -------------------------------------------------------------
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
        updateLatency();
    }

    //--- audio -----------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp.processBlock(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, (int)frames);
        updateLatency(); // oversampling factor changes reported latency
    }

private:
    void applyToDsp(uint32_t index, float v)
    {
        const int iv = (int)(v + 0.5f);
        switch (index)
        {
        case kParamTapeMachine: dsp.setTapeMachine(iv);           break;
        case kParamTapeSpeed:   dsp.setTapeSpeed(iv);             break;
        case kParamTapeType:    dsp.setTapeType(iv);              break;
        case kParamSignalPath:  dsp.setSignalPath(iv);           break;
        case kParamEqStandard:  dsp.setEqStandard(iv);           break;
        case kParamInputGain:   dsp.setInputGainDb(v);           break;
        case kParamSaturation:  dsp.setSaturation(v);            break;
        case kParamBias:        dsp.setBias(v);                  break;
        case kParamCalibration: dsp.setCalibration(iv);          break;
        case kParamAutoCal:     dsp.setAutoCal(v > 0.5f);        break;
        case kParamHighpassFreq:dsp.setHighpassHz(v);            break;
        case kParamLowpassFreq: dsp.setLowpassHz(v);             break;
        case kParamNoiseAmount: dsp.setNoiseAmount(v);           break;
        case kParamNoiseEnabled:dsp.setNoiseEnabled(v > 0.5f);   break;
        case kParamWow:         dsp.setWow(v);                   break;
        case kParamFlutter:     dsp.setFlutter(v);               break;
        case kParamOutputGain:  dsp.setOutputGainDb(v);          break;
        case kParamAutoComp:    dsp.setAutoComp(v > 0.5f);       break;
        case kParamOversampling:dsp.setOversampling(iv);         break;
        case kParamBypass:      dsp.setBypass(v > 0.5f);         break;
        }
    }

    void pushAllParams()
    {
        for (uint32_t i = 0; i < kParamVuL; ++i)
            applyToDsp(i, values[i].load(std::memory_order_relaxed));
    }

    void updateLatency()
    {
        const int lat = dsp.latencySamples();
        if (lat != lastLatency)
        {
            lastLatency = lat;
            setLatency((uint32_t)(lat < 0 ? 0 : lat));
        }
    }

    duskaudio::TapeMachineDSP dsp;
    int lastLatency = -1;
    // Parameter cache shared across threads (relaxed atomics), same pattern as
    // the DSP core's own parameter atomics.
    std::atomic<float> values[kParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachinePlugin)
};

Plugin* createPlugin()
{
    return new TapeMachinePlugin();
}

END_NAMESPACE_DISTRHO

// same-process UI accessors (see TapeMachineAccess.hpp)
float tapeMachineGetVuL(void* const pluginInstancePointer) noexcept
{
    auto* const p = static_cast<DISTRHO_NAMESPACE::TapeMachinePlugin*>(pluginInstancePointer);
    return p != nullptr ? p->getVuLForUI() : 0.0f;
}
float tapeMachineGetVuR(void* const pluginInstancePointer) noexcept
{
    auto* const p = static_cast<DISTRHO_NAMESPACE::TapeMachinePlugin*>(pluginInstancePointer);
    return p != nullptr ? p->getVuRForUI() : 0.0f;
}
