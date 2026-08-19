#include "DistrhoPlugin.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVersion.hpp"
#include "util/CrashLog.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

START_NAMESPACE_DISTRHO

class MultiCompPlugin final : public Plugin
{
    DuskCrashLog::ScopedRegistration crashLog_ { "multi-comp-2", MULTICOMP2_VERSION_STRING };

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
            else if (index == 5 || index == 7 || index == 8 || index == 15 || index == 29 || index == 50 || index == 55 || index == 58)
            {
                setEnum(p, multicompp::kOnOff, 2);
                if (index == 58) p.hints |= kParameterIsBoolean;
            }
            else if (index == 6) setEnum(p, multicompp::kTruePeakQuality, 2);
            else if (index == 9) setEnum(p, multicompp::kDistortion, 4);
            else if (index == 11) setEnum(p, multicompp::kOversampling, 3);
            else if (index == 20) setEnum(p, multicompp::kRatios, 5);
            else if (index == 21) setEnum(p, multicompp::kFetCurve, 2);
            else if (index == 30) setEnum(p, multicompp::kVcaDetector, 2);
            else if (index == 32) setEnum(p, multicompp::kBusRatios, 3);
            else if (index == 33) setEnum(p, multicompp::kBusAttack, 6);
            else if (index == 34) setEnum(p, multicompp::kBusRelease, 5);
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
        if (index >= multicompp::kMeterBand0 && index <= multicompp::kMeterBand3)
            return dsp.getBandGainReduction(static_cast<int>(index - multicompp::kMeterBand0));
        return 0.0f;
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
        const auto& q = multicompp::kFactoryPresets[index];
        multicompp::forEachPresetParam(q,
        [this](multicompp::CoreParameter parameter, float value)
        {
            const int parameterIndex = multicompp::coreParamIndex(parameter);
            if (parameterIndex >= 0) setParameterValue(static_cast<uint32_t>(parameterIndex), value);
        },
        [this](int band, int field, float value)
        {
            setParameterValue(static_cast<uint32_t>(multicompp::kBandBase + band * 8 + field), value);
        });
    }

    void initState(uint32_t index, State& state) override
    { if (index == 0) { state.key = "parameters"; state.label = "Multi-Comp Parameters"; state.defaultValue = ""; state.hints = kStateIsHostReadable; } }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "parameters") != 0) return String();
        std::string s = "v=1";
        char number[64];
        for (int i = 0; i < multicompp::kParamCount; ++i)
        {
            s.push_back(';'); s += multicompp::kParams[static_cast<size_t>(i)].id; s.push_back('=');
            const auto result = std::to_chars(number, number + sizeof(number), values[static_cast<size_t>(i)].load(std::memory_order_relaxed), std::chars_format::general, 9);
            if (result.ec == std::errc()) s.append(number, result.ptr);
        }
        for (int i = multicompp::kBandBase; i < multicompp::kMeterMaster; ++i)
        {
            const int band = (i - multicompp::kBandBase) / 8;
            const int field = (i - multicompp::kBandBase) % 8;
            s.push_back(';'); s += stateBandId(field, band); s.push_back('=');
            const auto result = std::to_chars(number, number + sizeof(number), values[static_cast<size_t>(i)].load(std::memory_order_relaxed), std::chars_format::general, 9);
            if (result.ec == std::errc()) s.append(number, result.ptr);
        }
        return String(s.c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "parameters") != 0 || value == nullptr) return;
        const std::string_view state(value);
        size_t begin = 0;
        while (begin < state.size())
        {
            const size_t end = state.find(';', begin);
            const std::string_view token = state.substr(begin, end == std::string_view::npos ? state.size() - begin : end - begin);
            const size_t equal = token.find('=');
            if (equal != std::string_view::npos && token.substr(0, equal) != "v")
            {
                float parsed = 0.0f;
                const auto number = token.substr(equal + 1);
                const auto result = std::from_chars(number.data(), number.data() + number.size(), parsed, std::chars_format::general);
                if (result.ec == std::errc() && result.ptr == number.data() + number.size())
                {
                    const std::string_view id = token.substr(0, equal);
                    for (int i = 0; i < multicompp::kParamCount; ++i)
                        if (id == multicompp::kParams[static_cast<size_t>(i)].id) { setParameterValue(static_cast<uint32_t>(i), parsed); break; }
                    for (int i = multicompp::kBandBase; i < multicompp::kMeterMaster; ++i)
                    {
                        const int band = (i - multicompp::kBandBase) / 8;
                        const int field = (i - multicompp::kBandBase) % 8;
                        if (id == stateBandId(field, band)) { setParameterValue(static_cast<uint32_t>(i), parsed); break; }
                    }
                }
            }
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
    }

    void activate() override { dsp.prepare(getSampleRate(), static_cast<int>(getBufferSize())); pushParameters(); updateLatency(); }
    void deactivate() override { dsp.reset(); }
    void sampleRateChanged(double sr) override { dsp.prepare(sr, static_cast<int>(getBufferSize())); pushParameters(); updateLatency(); }
    void bufferSizeChanged(uint32_t bs) override { dsp.prepare(getSampleRate(), static_cast<int>(bs)); pushParameters(); updateLatency(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const bool useSc = values[7].load(std::memory_order_relaxed) > 0.5f && inputs[2] != nullptr && inputs[3] != nullptr;
        if (useSc) { const float* sc[2] = {inputs[2], inputs[3]}; dsp.processBlockExternal(inputs, sc, outputs, 2, static_cast<int>(frames)); }
        else dsp.processBlock(inputs, outputs, 2, static_cast<int>(frames));
        updateLatency();
    }

private:
    static std::string stateBandId(int field, int band)
    {
        char number[16];
        const auto result = std::to_chars(number, number + sizeof(number), band);
        std::string id = multicompp::bandParam(field, band).id;
        id.push_back('_');
        if (result.ec == std::errc()) id.append(number, result.ptr);
        return id;
    }

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
