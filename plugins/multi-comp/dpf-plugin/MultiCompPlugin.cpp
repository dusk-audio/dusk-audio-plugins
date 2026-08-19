#include "DistrhoPlugin.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVersion.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

class MultiCompPlugin final : public Plugin
{
public:
    MultiCompPlugin() : Plugin(multicompp::kTotalParamCount, multicompp::kFactoryPresets.size(), 1)
    {
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            const float def = i < multicompp::kParamCount ? multicompp::kParams[static_cast<size_t>(i)].def : multicompp::bandParam((i - multicompp::kBandBase) % 8, (i - multicompp::kBandBase) / 8).def;
            values[static_cast<size_t>(i)].store(def, std::memory_order_relaxed);
        }
    }

    float gr() const noexcept { return dsp.getGainReduction(); }
    float bandGr(int b) const noexcept { return dsp.getBandGainReduction(b); }
    float inputLevel() const noexcept { return dsp.getInputLevel(); }
    float outputLevel() const noexcept { return dsp.getOutputLevel(); }

protected:
    const char* getLabel() const override { return "Multi-Comp 2"; }
    const char* getDescription() const override { return "Eight-mode dynamics processor"; }
    const char* getMaker() const override { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(MULTICOMP2_VERSION_MAJOR, MULTICOMP2_VERSION_MINOR, MULTICOMP2_VERSION_PATCH); }
    int64_t getUniqueId() const override { return d_cconst('D','s','M','c'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (input && index >= 2) { port.hints |= kAudioPortIsSidechain; port.groupId = 1; port.name = index == 2 ? "Sidechain Left" : "Sidechain Right"; port.symbol = index == 2 ? "sidechain_l" : "sidechain_r"; }
        else if (input) { port.groupId = 0; port.name = index == 0 ? "Input Left" : "Input Right"; port.symbol = index == 0 ? "input_l" : "input_r"; }
        else { port.groupId = 0; port.name = index == 0 ? "Output Left" : "Output Right"; port.symbol = index == 0 ? "output_l" : "output_r"; }
    }

    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        if (groupId == 0) { group.name = "Main"; group.symbol = "main"; }
        else if (groupId == 1) { group.name = "Sidechain"; group.symbol = "sidechain"; }
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index < multicompp::kParamCount)
        {
            const auto& d = multicompp::kParams[static_cast<size_t>(index)];
            p.name = d.name; p.symbol = d.id; p.unit = d.unit;
            p.ranges.min = d.min; p.ranges.max = d.max; p.ranges.def = d.def;
            p.hints = kParameterIsAutomatable | (d.integer ? kParameterIsInteger : 0u);
            if (index == 1) { p.initDesignation(kParameterDesignationBypass); return; }
            if (index == 0) setEnum(p, multicompp::kModes, 8);
            else if (index == 5 || index == 7 || index == 8 || index == 15 || index == 29 || index == 30 || index == 50 || index == 55) setEnum(p, multicompp::kOnOff, 2);
            else if (index == 6) setEnum(p, multicompp::kTruePeakQuality, 2);
            else if (index == 9) setEnum(p, multicompp::kDistortion, 4);
            else if (index == 11) setEnum(p, multicompp::kOversampling, 3);
            else if (index == 20) setEnum(p, multicompp::kRatios, 5);
            else if (index == 54) setEnum(p, multicompp::kEnvelopeCurve, 2);
            else if (index == 59) setEnum(p, multicompp::kSaturationMode, 3);
            else if (index == 64) setEnum(p, multicompp::kLinkMode, 3);
            return;
        }
        if (index < multicompp::kMeterMaster)
        {
            const int band = static_cast<int>((index - multicompp::kBandBase) / 8);
            const int field = static_cast<int>((index - multicompp::kBandBase) % 8);
            const auto d = multicompp::bandParam(field, band);
            p.name = String(d.name) + " " + (band == 0 ? "Low" : band == 1 ? "Low-Mid" : band == 2 ? "High-Mid" : "High");
            p.symbol = String(d.id) + "_" + std::to_string(band).c_str(); p.unit = d.unit;
            p.ranges.min = d.min; p.ranges.max = d.max; p.ranges.def = d.def;
            p.hints = kParameterIsAutomatable | (d.integer ? (kParameterIsInteger | kParameterIsBoolean) : 0u);
            return;
        }
        p.hints = kParameterIsAutomatable | kParameterIsOutput;
        p.ranges.min = -60.0f; p.ranges.max = 0.0f; p.ranges.def = 0.0f; p.unit = "dB";
        if (index == multicompp::kMeterMaster) p.name = "GR";
        else { p.name = index == multicompp::kMeterBand0 ? "Low GR" : index == multicompp::kMeterBand1 ? "Low-Mid GR" : index == multicompp::kMeterBand2 ? "High-Mid GR" : "High GR"; }
        p.symbol = index == multicompp::kMeterMaster ? "gr_meter" : index == multicompp::kMeterBand0 ? "gr_low" : index == multicompp::kMeterBand1 ? "gr_lowmid" : index == multicompp::kMeterBand2 ? "gr_highmid" : "gr_high";
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index < multicompp::kParamCount) return values[index].load(std::memory_order_relaxed);
        if (index < multicompp::kMeterMaster) return values[index].load(std::memory_order_relaxed);
        if (index == multicompp::kMeterMaster) return dsp.getGainReduction();
        return dsp.getBandGainReduction(static_cast<int>(index - multicompp::kMeterBand0));
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= multicompp::kParamCount && index < multicompp::kMeterMaster)
        {
            const int band = static_cast<int>((index - multicompp::kBandBase) / 8);
            const int field = static_cast<int>((index - multicompp::kBandBase) % 8);
            const auto d = multicompp::bandParam(field, band);
            const float v = value < d.min ? d.min : value > d.max ? d.max : value;
            values[index].store(v, std::memory_order_relaxed);
            dsp.setMultibandParameter(band, d.core, v);
            return;
        }
        if (index >= multicompp::kParamCount) return;
        const auto& d = multicompp::kParams[index];
        const float v = value < d.min ? d.min : value > d.max ? d.max : value;
        values[index].store(v, std::memory_order_relaxed);
        dsp.setParameter(d.core, v);
    }

    void initProgramName(uint32_t index, String& name) override
    { if (index < multicompp::kFactoryPresets.size()) name = multicompp::kFactoryPresets[index].name; }

    void loadProgram(uint32_t index) override
    {
        if (index >= multicompp::kFactoryPresets.size()) return;
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            const float def = i < multicompp::kParamCount ? multicompp::kParams[static_cast<size_t>(i)].def : multicompp::bandParam((i - multicompp::kBandBase) % 8, (i - multicompp::kBandBase) / 8).def;
            setParameterValue(static_cast<uint32_t>(i), def);
        }
        const auto& q = multicompp::kFactoryPresets[index];
        using P = duskaudio::MultiCompDSP::Parameter;
        const auto set = [this](P parameter, float value)
        {
            setParameterValue(static_cast<uint32_t>(parameter), value);
        };

        // Keep this mapping explicit: the JUCE preset application writes the
        // mode-specific APVTS parameters by identity, not by table offsets.
        set(P::Mode, static_cast<float>(q.mode));
        set(P::Mix, q.mix);
        set(P::SidechainHP, q.sidechainHP);
        set(P::AutoMakeup, q.autoMakeup ? 1.0f : 0.0f);
        set(P::SaturationMode, static_cast<float>(q.saturationMode));

        switch (q.mode)
        {
            case 0: // Opto
                set(P::OptoPeakReduction, q.peakReduction);
                set(P::OptoGain, duskaudio::optoGainDbToKnob(q.makeup));
                set(P::OptoLimit, q.limitMode ? 1.0f : 0.0f);
                break;
            case 1: // FET
            case 4: // Studio FET
                set(P::FetInput, -q.threshold);
                set(P::FetOutput, q.makeup);
                set(P::FetAttack, q.attack);
                set(P::FetRelease, q.release);
                set(P::FetRatio, static_cast<float>(q.fetRatio));
                break;
            case 2: // VCA
                set(P::VcaThreshold, q.threshold);
                set(P::VcaRatio, q.ratio);
                set(P::VcaAttack, q.attack);
                set(P::VcaRelease, q.release);
                set(P::VcaOutput, q.makeup);
                set(P::VcaOverEasy, q.vcaOverEasy);
                break;
            case 3: // Bus
            {
                // JUCE's choice range is 0..2; its preset values are the
                // displayed ratio figures, so values >= 4 land on choice 2.
                const int ratioChoice = q.ratio <= 2.0f ? 0 : q.ratio <= 3.0f ? 1 : 2;
                set(P::BusThreshold, q.threshold);
                set(P::BusRatio, static_cast<float>(ratioChoice));
                set(P::BusAttack, static_cast<float>(q.busAttack));
                set(P::BusRelease, static_cast<float>(q.busRelease));
                set(P::BusMakeup, q.makeup);
                set(P::BusMix, q.mix);
                break;
            }
            case 5: // Studio VCA
                set(P::StudioVcaThreshold, q.threshold);
                set(P::StudioVcaRatio, q.ratio);
                set(P::StudioVcaAttack, q.attack);
                set(P::StudioVcaRelease, q.release);
                set(P::StudioVcaOutput, q.makeup);
                break;
            case 6: // Digital
                set(P::DigitalThreshold, q.threshold);
                set(P::DigitalRatio, q.ratio);
                set(P::DigitalAttack, q.attack);
                set(P::DigitalRelease, q.release);
                set(P::DigitalOutput, q.makeup);
                break;
            case 7: // Multiband presets currently carry no mode-specific data.
                break;
            default:
                break;
        }
    }

    void initState(uint32_t index, State& state) override
    { if (index == 0) { state.key = "parameters"; state.label = "Multi-Comp Parameters"; state.defaultValue = ""; state.hints = kStateIsHostReadable; } }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "parameters") != 0) return String();
        std::string s;
        for (int i = 0; i < multicompp::kMeterMaster; ++i) { if (i) s.push_back(','); s += std::to_string(values[static_cast<size_t>(i)].load(std::memory_order_relaxed)); }
        return String(s.c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "parameters") != 0 || value == nullptr) return;
        const char* p = value;
        for (int i = 0; i < multicompp::kMeterMaster && *p; ++i) { char* end = nullptr; const float v = std::strtof(p, &end); if (end == p) break; setParameterValue(static_cast<uint32_t>(i), v); p = *end == ',' ? end + 1 : end; }
    }

    void activate() override { dsp.prepare(getSampleRate(), static_cast<int>(getBufferSize())); pushParameters(); updateLatency(); }
    void deactivate() override { dsp.reset(); }
    void sampleRateChanged(double sr) override { dsp.prepare(sr, static_cast<int>(getBufferSize())); pushParameters(); }
    void bufferSizeChanged(uint32_t bs) override { dsp.prepare(getSampleRate(), static_cast<int>(bs)); pushParameters(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const bool useSc = values[7].load(std::memory_order_relaxed) > 0.5f && inputs[2] != nullptr && inputs[3] != nullptr;
        if (useSc) { const float* sc[2] = {inputs[2], inputs[3]}; dsp.processBlockExternal(inputs, sc, outputs, 2, static_cast<int>(frames)); }
        else dsp.processBlock(inputs, outputs, 2, static_cast<int>(frames));
        updateLatency();
    }

private:
    static void setEnum(Parameter& p, const char* const* labels, int count)
    { p.enumValues.count = static_cast<uint8_t>(count); p.enumValues.restrictedMode = true; auto* e = new ParameterEnumerationValue[count]; for (int i = 0; i < count; ++i) e[i] = ParameterEnumerationValue(static_cast<float>(i), labels[i]); p.enumValues.values = e; }
    void pushParameters()
    {
        for (int i = 0; i < multicompp::kParamCount; ++i)
            dsp.setParameter(multicompp::kParams[static_cast<size_t>(i)].core, values[static_cast<size_t>(i)].load(std::memory_order_relaxed));
        for (int i = multicompp::kBandBase; i < multicompp::kMeterMaster; ++i)
        {
            const int band = (i - multicompp::kBandBase) / 8;
            const auto d = multicompp::bandParam((i - multicompp::kBandBase) % 8, band);
            dsp.setMultibandParameter(band, d.core, values[static_cast<size_t>(i)].load(std::memory_order_relaxed));
        }
    }
    void updateLatency() { const int l = dsp.getLatencySamples(); if (l != lastLatency) { lastLatency = l; setLatency(static_cast<uint32_t>(l < 0 ? 0 : l)); } }

    duskaudio::MultiCompDSP dsp;
    std::array<std::atomic<float>, multicompp::kMeterMaster> values{};
    int lastLatency = -1;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiCompPlugin)
};

Plugin* createPlugin() { return new MultiCompPlugin(); }
END_NAMESPACE_DISTRHO

static DISTRHO_NAMESPACE::MultiCompPlugin* asMultiComp(void* p) noexcept { return static_cast<DISTRHO_NAMESPACE::MultiCompPlugin*>(p); }
float multiCompGetGainReduction(void* p) noexcept { return p ? asMultiComp(p)->gr() : 0.0f; }
float multiCompGetBandGainReduction0(void* p) noexcept { return p ? asMultiComp(p)->bandGr(0) : 0.0f; }
float multiCompGetBandGainReduction1(void* p) noexcept { return p ? asMultiComp(p)->bandGr(1) : 0.0f; }
float multiCompGetBandGainReduction2(void* p) noexcept { return p ? asMultiComp(p)->bandGr(2) : 0.0f; }
float multiCompGetBandGainReduction3(void* p) noexcept { return p ? asMultiComp(p)->bandGr(3) : 0.0f; }
float multiCompGetInputLevel(void* p) noexcept { return p ? asMultiComp(p)->inputLevel() : -60.0f; }
float multiCompGetOutputLevel(void* p) noexcept { return p ? asMultiComp(p)->outputLevel() : -60.0f; }
