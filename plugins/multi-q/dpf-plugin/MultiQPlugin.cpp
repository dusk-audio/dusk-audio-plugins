// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQPlugin.cpp — DPF shell around the framework-free MultiQDSP core.
//
// The shell owns the 190 host-automation parameters (MultiQParams.hpp), caches
// them as relaxed atomics, and snapshots them into a MultiQDSP::Params struct
// once per block before calling MultiQDSP::process(). The DSP core is unchanged.

#include "DistrhoPlugin.hpp"
#include "MultiQAccess.hpp"
#include "MultiQParams.hpp"
#include "MultiQDSP.hpp"

#include <atomic>
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

class MultiQPlugin : public Plugin
{
public:
    MultiQPlugin()
        : Plugin(kParamCount, 1 /* one "Default" program */, 0 /* no state */)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i].store(kMqParams[i].def, std::memory_order_relaxed);
    }

    // same-process meter/analyzer access for the UI bridge (MultiQAccess.hpp)
    float inPeakLForUI()  const noexcept { return dsp.getInputPeakL(); }
    float inPeakRForUI()  const noexcept { return dsp.getInputPeakR(); }
    float outPeakLForUI() const noexcept { return dsp.getOutputPeakL(); }
    float outPeakRForUI() const noexcept { return dsp.getOutputPeakR(); }
    const duskaudio::SpectrumRing* outSpecForUI() const noexcept { return &dsp.outputSpectrum(); }

protected:
    //--- metadata --------------------------------------------------------------
    const char* getLabel() const override { return "MultiQ2"; }
    const char* getDescription() const override
    {
        return "Universal EQ: 8-band digital EQ with per-band shapes, routing, "
               "saturation and dynamics, plus British console and Tube EQ characters.";
    }
    const char* getMaker() const override    { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override  { return "GPL-3.0-or-later"; }
    uint32_t    getVersion() const override  { return d_version(2, 0, 0); }
    int64_t     getUniqueId() const override { return d_cconst('D', 's', 'M', 'q'); } // DsMq

    //--- parameters ------------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index >= kParamCount)
            return;

        // The single global bypass takes the host bypass designation (which sets
        // its own name/symbol/range); everything else is a normal parameter.
        if (index == kParamBypass)
        {
            p.initDesignation(kParameterDesignationBypass);
            return;
        }

        const MqParam& d = kMqParams[index];
        switch (d.kind)
        {
        case 'c': // discrete choice
        case 'b': // boolean toggle (Off/On), exposed as a 2-value integer choice
            p.hints = kParameterIsAutomatable | kParameterIsInteger;
            break;
        default:  // 'f' linear / 'g' skewed float (UI owns the skew feel)
            p.hints = kParameterIsAutomatable;
            break;
        }

        p.name       = d.name;
        p.symbol     = d.id;
        p.unit       = d.unit;
        p.ranges.def = d.def;
        p.ranges.min = d.min;
        p.ranges.max = d.max;

        if ((d.kind == 'c' || d.kind == 'b') && d.choices != nullptr && d.numChoices > 0)
        {
            p.enumValues.count = (uint8_t)d.numChoices;
            p.enumValues.restrictedMode = true;
            auto* const vals = new ParameterEnumerationValue[d.numChoices];
            for (int i = 0; i < d.numChoices; ++i)
            {
                vals[i].value = (float)i;
                vals[i].label = d.choices[i];
            }
            p.enumValues.values = vals; // DPF takes ownership
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParamCount ? values[index].load(std::memory_order_relaxed) : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < kParamCount)
            values[index].store(value, std::memory_order_relaxed);
    }

    //--- programs (factory presets) --------------------------------------------
    // Phase 2 ships a single "Default" program (all params to their layout
    // defaults). Factory presets from MultiQPresets.h land with the Phase-3 UI.
    void initProgramName(uint32_t index, String& programName) override
    {
        if (index == 0)
            programName = "Default";
    }

    void loadProgram(uint32_t index) override
    {
        if (index != 0)
            return;
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i].store(kMqParams[i].def, std::memory_order_relaxed);
    }

    //--- lifecycle -------------------------------------------------------------
    void activate() override
    {
        dsp.prepare(getSampleRate(), (int)getBufferSize());
        updateLatency();
    }

    void deactivate() override { dsp.reset(); }

    void sampleRateChanged(double newSampleRate) override
    {
        dsp.prepare(newSampleRate, (int)getBufferSize());
        updateLatency();
    }

    void bufferSizeChanged(uint32_t newBufferSize) override
    {
        dsp.prepare(getSampleRate(), (int)newBufferSize);
        updateLatency();
    }

    //--- audio -----------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        // Hard bypass (the core has no bypass path in Phase 2): pass input through.
        if (values[kParamBypass].load(std::memory_order_relaxed) > 0.5f)
        {
            for (uint32_t c = 0; c < DISTRHO_PLUGIN_NUM_OUTPUTS; ++c)
                if (outputs[c] != inputs[c])
                    std::memcpy(outputs[c], inputs[c], sizeof(float) * frames);
            return;
        }

        MultiQDSP::Params p;
        fillParams(p);
        dsp.process(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, (int)frames, p);
        updateLatency(); // British oversampling changes reported latency
    }

private:
    using MultiQDSP = duskaudio::MultiQDSP;
    using EQType    = duskaudio::EQType;

    // Snapshot the atomic value cache into the core's per-block Params struct.
    void fillParams(MultiQDSP::Params& p) const
    {
        auto f  = [this](int i) { return values[i].load(std::memory_order_relaxed); };
        auto bl = [&](int i)    { return f(i) > 0.5f; };
        auto ci = [&](int i)    { return (int)std::lround(f(i)); };
        auto lut = [](const float* a, int n, int i)
                   { return a[i < 0 ? 0 : (i >= n ? n - 1 : i)]; };

        for (int band = 0; band < 8; ++band)
        {
            p.bandEnabled[band]     = bl(mqidx::enabled(band));
            p.bandFreq[band]        = f(mqidx::freq(band));
            p.bandQ[band]           = f(mqidx::q(band));
            p.bandRouting[band]     = ci(mqidx::routing(band)); // raw 0=Global..5=Side
            p.bandInvert[band]      = bl(mqidx::invert(band));
            p.bandPhaseInvert[band] = bl(mqidx::phaseInvert(band));
            p.bandPan[band]         = f(mqidx::pan(band));

            if (mqidx::isEdge(band))
            {
                p.bandSlope[band]   = ci(mqidx::slope(band)); // only [0]/[7] meaningful
            }
            else
            {
                p.bandGain[band]     = f(mqidx::gain(band));
                p.bandShape[band]    = ci(mqidx::shape(band));
                p.bandSatType[band]  = ci(mqidx::satType(band));
                p.bandSatDrive[band] = f(mqidx::satDrive(band));
            }

            p.bandDynEnabled[band]   = bl(mqidx::dynEnabled(band));
            p.bandDynThreshold[band] = f(mqidx::dynThreshold(band));
            p.bandDynAttack[band]    = f(mqidx::dynAttack(band));
            p.bandDynRelease[band]   = f(mqidx::dynRelease(band));
            p.bandDynRange[band]     = f(mqidx::dynRange(band));
            p.bandDynRatio[band]     = f(mqidx::dynRatio(band));
        }

        p.masterGain     = f(kParamMasterGain);
        p.processingMode = ci(kParamProcessingMode);
        p.qCoupleMode    = ci(kParamQCoupleMode);
        p.eqType         = ci(kParamEqType);
        p.oversampling   = ci(kParamHqEnabled); // 0=Off,1=2x,2=4x
        p.soloBand       = -1;                  // UI wires solo later
        p.deltaSolo      = false;

        auto& br = p.british;
        br.hpfFreq = f(kParamBritishHpfFreq);  br.hpfEnabled = bl(kParamBritishHpfEnabled);
        br.lpfFreq = f(kParamBritishLpfFreq);  br.lpfEnabled = bl(kParamBritishLpfEnabled);
        br.lfGain  = f(kParamBritishLfGain);   br.lfFreq = f(kParamBritishLfFreq);  br.lfBell = bl(kParamBritishLfBell);
        br.lmGain  = f(kParamBritishLmGain);   br.lmFreq = f(kParamBritishLmFreq);  br.lmQ    = f(kParamBritishLmQ);
        br.hmGain  = f(kParamBritishHmGain);   br.hmFreq = f(kParamBritishHmFreq);  br.hmQ    = f(kParamBritishHmQ);
        br.hfGain  = f(kParamBritishHfGain);   br.hfFreq = f(kParamBritishHfFreq);  br.hfBell = bl(kParamBritishHfBell);
        br.blackMode  = bl(kParamBritishMode); // Brown=0/Black=1
        br.saturation = f(kParamBritishSaturation);
        br.inputGain  = f(kParamBritishInputGain);
        br.outputGain = f(kParamBritishOutputGain);

        // Tube: resolve the 6 stepped choice freqs via the LUTs (MultiQ.cpp:784-842).
        auto& t = p.tube;
        t.lfBoostGain      = f(kParamPultecLfBoostGain);
        t.lfBoostFreq      = lut(mqp::kLfBoostHz, 4, ci(kParamPultecLfBoostFreq));
        t.lfAttenGain      = f(kParamPultecLfAttenGain);
        t.hfBoostGain      = f(kParamPultecHfBoostGain);
        t.hfBoostFreq      = lut(mqp::kHfBoostHz, 7, ci(kParamPultecHfBoostFreq));
        t.hfBoostBandwidth = f(kParamPultecHfBoostBandwidth);
        t.hfAttenGain      = f(kParamPultecHfAttenGain);
        t.hfAttenFreq      = lut(mqp::kHfAttenHz, 3, ci(kParamPultecHfAttenFreq));
        t.midEnabled       = bl(kParamPultecMidEnabled);
        t.midLowFreq       = lut(mqp::kMidLowHz, 5, ci(kParamPultecMidLowFreq));
        t.midLowPeak       = f(kParamPultecMidLowPeak);
        t.midDipFreq       = lut(mqp::kMidDipHz, 7, ci(kParamPultecMidDipFreq));
        t.midDip           = f(kParamPultecMidDip);
        t.midHighFreq      = lut(mqp::kMidHighHz, 5, ci(kParamPultecMidHighFreq));
        t.midHighPeak      = f(kParamPultecMidHighPeak);
        t.inputGain        = f(kParamPultecInputGain);
        t.outputGain       = f(kParamPultecOutputGain);
        t.tubeDrive        = f(kParamPultecTubeDrive);
    }

    void updateLatency()
    {
        const int lat = dsp.getLatencySamples();
        if (lat != lastLatency)
        {
            lastLatency = lat;
            setLatency((uint32_t)(lat < 0 ? 0 : lat));
        }
    }

    MultiQDSP dsp;
    int lastLatency = -1;
    std::atomic<float> values[kParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQPlugin)
};

Plugin* createPlugin()
{
    return new MultiQPlugin();
}

END_NAMESPACE_DISTRHO

// same-process UI accessors (see MultiQAccess.hpp). Read straight off the live
// DSP instance's lock-free atomics / analyzer ring; null-safe for the split UI.
static DISTRHO_NAMESPACE::MultiQPlugin* asMq(void* p) noexcept
{
    return static_cast<DISTRHO_NAMESPACE::MultiQPlugin*>(p);
}
float multiQGetInputPeakL(void* p)  noexcept { return p ? asMq(p)->inPeakLForUI()  : 0.0f; }
float multiQGetInputPeakR(void* p)  noexcept { return p ? asMq(p)->inPeakRForUI()  : 0.0f; }
float multiQGetOutputPeakL(void* p) noexcept { return p ? asMq(p)->outPeakLForUI() : 0.0f; }
float multiQGetOutputPeakR(void* p) noexcept { return p ? asMq(p)->outPeakRForUI() : 0.0f; }
const duskaudio::SpectrumRing* multiQGetOutputSpectrum(void* p) noexcept
{
    return p ? asMq(p)->outSpecForUI() : nullptr;
}
