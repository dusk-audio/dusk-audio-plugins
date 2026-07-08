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
#include "MultiQProgramPresets.hpp"  // Digital factory presets (host programs)
#include "MultiQDSP.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

class MultiQPlugin : public Plugin
{
public:
    MultiQPlugin()
        : Plugin(kParamCount, 1 + mqprog::kNumDigitalPrograms /* Default + Digital presets */,
                 1 /* one state: the Match learned/correction blob */)
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
    float bandDynGainForUI(int band) const noexcept { return dsp.getBandDynamicGain(band); }
    float limiterGrForUI() const noexcept { return dsp.getLimiterGainReduction(); }

    // Solo write-bridge (transient editor state — the UI drives it, it is not a
    // host-automatable parameter, mirroring the JUCE build's soloedBand/deltaSolo).
    void setSoloForUI(int band, bool delta) noexcept
    {
        soloBand.store(band, std::memory_order_relaxed);
        deltaSolo.store(delta, std::memory_order_relaxed);
    }
    int  soloBandForUI()  const noexcept { return soloBand.load(std::memory_order_relaxed); }
    bool soloDeltaForUI() const noexcept { return deltaSolo.load(std::memory_order_relaxed); }

    //--- Match spectrum-EQ UI bridge (message thread) --------------------------
    void matchStartLearnCurrentForUI(bool on) noexcept
    {
        if (on) dsp.matchProcessor().requestStartLearnCurrent();
        else    dsp.matchProcessor().requestStopLearning();
    }
    void matchStartLearnReferenceForUI(bool on) noexcept
    {
        if (on) dsp.matchProcessor().requestStartLearnReference();
        else    dsp.matchProcessor().requestStopLearning();
    }
    bool matchComputeForUI() noexcept
    {
        // Read the live Match params (mirrors MultiQ::computeMatchCorrection).
        auto f = [this](int i) { return values[i].load(std::memory_order_relaxed); };
        const float applyAmount = f(kParamMatchApply) / 100.0f;      // -1..+1
        const float smoothing   = f(kParamMatchSmoothing);          // semitones
        const bool  limitBoost  = f(kParamMatchLimitBoost) > 0.5f;
        const bool  limitCut    = f(kParamMatchLimitCut) > 0.5f;
        const float maxBoostDB  = limitBoost ? 6.0f : 0.0f;         // 0 = hard ±15 only
        const float maxCutDB    = limitCut ? -6.0f : 0.0f;
        const bool ok = dsp.matchProcessor().computeCorrection(
            smoothing, applyAmount, maxBoostDB, maxCutDB, /*minimumPhase*/ true);
        updateLatency();
        return ok;
    }
    void matchClearForUI() noexcept { dsp.matchProcessor().requestClear(); }

    bool matchIsLearningForUI()          const noexcept { return dsp.matchProcessor().isLearning(); }
    bool matchIsLearningCurrentForUI()   const noexcept { return dsp.matchProcessor().isLearningCurrent(); }
    bool matchIsLearningReferenceForUI() const noexcept { return dsp.matchProcessor().isLearningReference(); }
    int  matchFrameCountForUI()          const noexcept { return dsp.matchProcessor().getLearningFrameCount(); }
    bool matchHasCurrentForUI()          const noexcept { return dsp.matchProcessor().hasCurrentSpectrum(); }
    bool matchHasReferenceForUI()        const noexcept { return dsp.matchProcessor().hasReferenceSpectrum(); }
    bool matchHasCorrectionForUI()       const noexcept { return dsp.matchProcessor().hasCorrectionCurve(); }
    void matchGetCurrentDbForUI(float* out, int n)    const noexcept { dsp.matchProcessor().getCurrentSpectrumDB(out, n); }
    void matchGetReferenceDbForUI(float* out, int n)  const noexcept { dsp.matchProcessor().getReferenceSpectrumDB(out, n); }
    void matchGetCorrectionDbForUI(float* out, int n) const noexcept { dsp.matchProcessor().getCorrectionCurveDB(out, n); }

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
    // Program 0 = "Default" (all params to layout defaults); programs 1..N = the
    // Digital factory presets ported from MultiQPresets.h (see MultiQProgramPresets.hpp).
    void initProgramName(uint32_t index, String& programName) override
    {
        if (index == 0) { programName = "Default"; return; }
        const uint32_t pi = index - 1;
        if (pi < (uint32_t)mqprog::kNumDigitalPrograms)
            programName = mqprog::kDigitalPrograms[pi].name;
    }

    void loadProgram(uint32_t index) override
    {
        // Every program starts from layout defaults, then a Digital preset applies
        // its sparse (paramIndex,value) overrides on top.
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i].store(kMqParams[i].def, std::memory_order_relaxed);
        if (index == 0)
            return;
        const uint32_t pi = index - 1;
        if (pi >= (uint32_t)mqprog::kNumDigitalPrograms)
            return;
        values[kParamEqType].store(0.0f, std::memory_order_relaxed); // Digital character
        const mqprog::Program& prog = mqprog::kDigitalPrograms[pi];
        for (int i = 0; i < prog.count; ++i)
            values[prog.pairs[i].idx].store(prog.pairs[i].val, std::memory_order_relaxed);
    }

    //--- state (Match learned spectra + correction FIR) ------------------------
    // One host-persisted state key: "matchData" (a base64 blob of the learned
    // current/reference spectra + correction curve + FIR). WANT_FULL_STATE lets
    // the host pull it via getState() on save. The blob is applied after the DSP
    // is prepared (activate()), so a restore that arrives before activate() is
    // cached and re-applied — MultiQMatch::deserialize rebuilds the FIR and
    // re-activates the convolver on the next processed block.
    void initState(uint32_t index, State& state) override
    {
        if (index != 0) return;
        state.key          = "matchData";
        state.label        = "Match EQ Data";
        state.defaultValue = "";
        state.hints        = kStateIsHostReadable | kStateIsBase64Blob;
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "matchData") == 0)
            return String(dsp.matchProcessor().serialize().c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "matchData") != 0)
            return;
        matchStateStr = value != nullptr ? value : "";
        if (dspPrepared && !matchStateStr.empty())
        {
            dsp.matchProcessor().deserialize(matchStateStr);
            updateLatency();
        }
    }

    //--- lifecycle -------------------------------------------------------------
    void activate() override
    {
        dsp.prepare(getSampleRate(), (int)getBufferSize());
        dspPrepared = true;
        if (!matchStateStr.empty())
            dsp.matchProcessor().deserialize(matchStateStr);
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

        // Master-bus utilities (auto_gain_enabled / limiter_enabled / ceiling).
        p.autoGainEnabled = bl(kParamAutoGainEnabled);
        p.limiterEnabled  = bl(kParamLimiterEnabled);
        p.limiterCeiling  = f(kParamLimiterCeiling);

        // Solo / delta-solo come from the UI write-bridge (transient editor state,
        // not host-automatable params) — see multiQSetSolo() in MultiQAccess.hpp.
        p.soloBand  = soloBand.load(std::memory_order_relaxed);
        p.deltaSolo = deltaSolo.load(std::memory_order_relaxed);

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
    std::atomic<int>  soloBand{-1};    // -1 = no solo (UI-driven, see setSoloForUI)
    std::atomic<bool> deltaSolo{false};
    std::string matchStateStr;         // cached Match blob (applied once DSP prepared)
    bool dspPrepared = false;

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
float multiQGetBandDynGain(void* p, int band) noexcept
{
    return p ? asMq(p)->bandDynGainForUI(band) : 0.0f;
}
float multiQGetLimiterGR(void* p) noexcept
{
    return p ? asMq(p)->limiterGrForUI() : 0.0f;
}

// Solo write-bridge (UI → DSP). band = -1 clears solo; delta engages delta-solo.
void multiQSetSolo(void* p, int band, bool delta) noexcept
{
    if (p) asMq(p)->setSoloForUI(band, delta);
}
int  multiQGetSoloBand(void* p)  noexcept { return p ? asMq(p)->soloBandForUI()  : -1; }
bool multiQGetSoloDelta(void* p) noexcept { return p ? asMq(p)->soloDeltaForUI() : false; }

// Match spectrum-EQ write/read bridge (UI → DSP). See MultiQAccess.hpp.
void multiQMatchStartLearnCurrent(void* p, bool on) noexcept   { if (p) asMq(p)->matchStartLearnCurrentForUI(on); }
void multiQMatchStartLearnReference(void* p, bool on) noexcept { if (p) asMq(p)->matchStartLearnReferenceForUI(on); }
bool multiQMatchCompute(void* p) noexcept                       { return p ? asMq(p)->matchComputeForUI() : false; }
void multiQMatchClear(void* p) noexcept                         { if (p) asMq(p)->matchClearForUI(); }
bool multiQMatchIsLearning(void* p) noexcept                    { return p ? asMq(p)->matchIsLearningForUI() : false; }
bool multiQMatchIsLearningCurrent(void* p) noexcept            { return p ? asMq(p)->matchIsLearningCurrentForUI() : false; }
bool multiQMatchIsLearningReference(void* p) noexcept          { return p ? asMq(p)->matchIsLearningReferenceForUI() : false; }
int  multiQMatchFrameCount(void* p) noexcept                    { return p ? asMq(p)->matchFrameCountForUI() : 0; }
bool multiQMatchHasCurrent(void* p) noexcept                    { return p ? asMq(p)->matchHasCurrentForUI() : false; }
bool multiQMatchHasReference(void* p) noexcept                  { return p ? asMq(p)->matchHasReferenceForUI() : false; }
bool multiQMatchHasCorrection(void* p) noexcept                 { return p ? asMq(p)->matchHasCorrectionForUI() : false; }
void multiQMatchGetCurrentDb(void* p, float* out, int n) noexcept    { if (p) asMq(p)->matchGetCurrentDbForUI(out, n); }
void multiQMatchGetReferenceDb(void* p, float* out, int n) noexcept  { if (p) asMq(p)->matchGetReferenceDbForUI(out, n); }
void multiQMatchGetCorrectionDb(void* p, float* out, int n) noexcept { if (p) asMq(p)->matchGetCorrectionDbForUI(out, n); }
