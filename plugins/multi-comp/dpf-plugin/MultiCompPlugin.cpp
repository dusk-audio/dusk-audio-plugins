#include "DistrhoPlugin.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVersion.hpp"
#include "util/CrashLog.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

class MultiCompPlugin final : public Plugin
{
    DuskCrashLog::ScopedRegistration crashLog_ { "multi-comp-2", MULTICOMP2_VERSION_STRING };

public:
    MultiCompPlugin() : Plugin(multicompp::kTotalParamCount, multicompp::kFactoryPresets.size(), 1)
    {
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            const float def = i < multicompp::kParamCount
                ? multicompp::hostDefault(multicompp::kParams[static_cast<size_t>(i)])
                : multicompp::hostDefault(multicompp::bandParam(
                    (i - multicompp::kBandBase) % 8, (i - multicompp::kBandBase) / 8));
            values[static_cast<size_t>(i)].store(def, std::memory_order_relaxed);
        }
    }

    float gr() const noexcept { return dsp.getGainReduction(); }
    float bandGr(int b) const noexcept { return dsp.getBandGainReduction(b); }
    float inputLevel() const noexcept { return dsp.getInputLevel(); }
    float outputLevel() const noexcept { return dsp.getOutputLevel(); }

protected:
    const char* getLabel() const override { return "MultiComp2"; }
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
            p.name = d.name; p.symbol = d.id;
            // DPF has no arbitrary value<->text callback. Tapered parameters
            // therefore expose their honest normalized coordinate without a
            // misleading physical unit; the custom UI displays mapped units.
            p.unit = multicompp::hasSkew(d) ? "" : d.unit;
            p.ranges.min = multicompp::hostMin(d); p.ranges.max = multicompp::hostMax(d);
            p.ranges.def = multicompp::hostDefault(d);
            p.hints = kParameterIsAutomatable | (d.integer ? kParameterIsInteger : 0u);
            using Id = multicompp::ParamId;
            switch (static_cast<Id>(index))
            {
                case Id::Bypass: p.initDesignation(kParameterDesignationBypass); return;
                case Id::Mode: setEnum(p, multicompp::kModes, 8); break;
                case Id::TruePeakEnable: case Id::ExternalSidechain: case Id::AutoMakeup:
                case Id::OptoLimit: case Id::VcaOverEasy: case Id::DigitalAdaptive:
                case Id::GlobalSidechainListen: case Id::NoiseEnable:
                    setEnum(p, multicompp::kOnOff, 2); p.hints |= kParameterIsBoolean; break;
                case Id::TruePeakQuality: setEnum(p, multicompp::kTruePeakQuality, 2); break;
                case Id::Distortion: setEnum(p, multicompp::kDistortion, 4); break;
                case Id::Oversampling: setEnum(p, multicompp::kOversampling, 3); break;
                case Id::FetRatio: setEnum(p, multicompp::kRatios, 5); break;
                case Id::FetCurve: setEnum(p, multicompp::kFetCurve, 2); break;
                case Id::VcaClassicDetector: setEnum(p, multicompp::kVcaDetector, 2); break;
                case Id::BusRatio: setEnum(p, multicompp::kBusRatios, 3); break;
                case Id::BusAttack: setEnum(p, multicompp::kBusAttack, 6); break;
                case Id::BusRelease: setEnum(p, multicompp::kBusRelease, 5); break;
                case Id::StereoLinkMode: setEnum(p, multicompp::kLinkMode, 3); break;
                default: break;
            }
            return;
        }
        if (index < multicompp::kMeterMaster)
        {
            const int band = static_cast<int>((index - multicompp::kBandBase) / 8);
            const int field = static_cast<int>((index - multicompp::kBandBase) % 8);
            const auto d = multicompp::bandParam(field, band);
            p.name = String(d.name) + " " + (band == 0 ? "Low" : band == 1 ? "Low-Mid" : band == 2 ? "High-Mid" : "High");
            p.symbol = String(d.id) + "_" + std::to_string(band).c_str();
            p.unit = multicompp::hasSkew(d) ? "" : d.unit;
            p.ranges.min = multicompp::hostMin(d); p.ranges.max = multicompp::hostMax(d);
            p.ranges.def = multicompp::hostDefault(d);
            p.hints = kParameterIsAutomatable | (d.integer ? (kParameterIsInteger | kParameterIsBoolean) : 0u);
            return;
        }
        // Output-only deliberately excludes kParameterIsAutomatable: hosts may
        // read these meters, but must never offer them as writable targets.
        p.hints = kParameterIsOutput;
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
            const float v = duskaudio::clampFinite(
                value, multicompp::hostMin(d), multicompp::hostMax(d), multicompp::hostDefault(d));
            values[index].store(v, std::memory_order_relaxed);
            dsp.setMultibandParameter(band, d.core, multicompp::hostToPlain(d, v));
            return;
        }
        if (index >= multicompp::kParamCount) return;
        const auto& d = multicompp::kParams[index];
        const float v = duskaudio::clampFinite(
            value, multicompp::hostMin(d), multicompp::hostMax(d), multicompp::hostDefault(d));
        values[index].store(v, std::memory_order_relaxed);
        dsp.setParameter(d.core, multicompp::hostToPlain(d, v));
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
            if (parameterIndex >= 0)
            {
                const auto& d = multicompp::kParams[static_cast<size_t>(parameterIndex)];
                setParameterValue(static_cast<uint32_t>(parameterIndex), multicompp::plainToHost(d, value));
            }
        });
    }

    void initState(uint32_t index, State& state) override
    { if (index == 0) { state.key = "parameters"; state.label = "Multi-Comp Parameters"; state.defaultValue = ""; state.hints = kStateIsHostReadable; } }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "parameters") != 0) return String();
        multicompp::StateValues state{};
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
            state[static_cast<size_t>(i)] = values[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        return String(multicompp::encodeState(state).c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "parameters") != 0 || value == nullptr) return;
        multicompp::StateValues state{};
        if (!multicompp::decodeState(value, state)) return;
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
            setParameterValue(static_cast<uint32_t>(i), state[static_cast<size_t>(i)]);
    }

    void activate() override { dsp.prepare(getSampleRate(), static_cast<int>(getBufferSize())); pushParameters(); updateLatency(); }
    void deactivate() override { dsp.reset(); }
    void sampleRateChanged(double sr) override { dsp.prepare(sr, static_cast<int>(getBufferSize())); pushParameters(); updateLatency(); }
    void bufferSizeChanged(uint32_t bs) override { dsp.prepare(getSampleRate(), static_cast<int>(bs)); pushParameters(); updateLatency(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const bool useSc = values[static_cast<size_t>(multicompp::ParamId::ExternalSidechain)].load(std::memory_order_relaxed) > 0.5f && inputs[2] != nullptr && inputs[3] != nullptr;
        if (useSc) { const float* sc[2] = {inputs[2], inputs[3]}; dsp.processBlockExternal(inputs, sc, outputs, 2, static_cast<int>(frames)); }
        else dsp.processBlock(inputs, outputs, 2, static_cast<int>(frames));
        // setLatency() is documented as callable from run(). The CLAP wrapper
        // latches a change here and publishes it at the next activate(), which
        // is the only point the CLAP spec allows the latency to move.
        updateLatency();
    }

private:
    static void setEnum(Parameter& p, const char* const* labels, int count)
    { p.enumValues.count = static_cast<uint8_t>(count); p.enumValues.restrictedMode = true; auto* e = new ParameterEnumerationValue[count]; for (int i = 0; i < count; ++i) e[i] = ParameterEnumerationValue(static_cast<float>(i), labels[i]); p.enumValues.values = e; }
    void pushParameters()
    {
        for (int i = 0; i < multicompp::kParamCount; ++i)
        {
            const auto& d = multicompp::kParams[static_cast<size_t>(i)];
            dsp.setParameter(d.core, multicompp::hostToPlain(
                d, values[static_cast<size_t>(i)].load(std::memory_order_relaxed)));
        }
        for (int i = multicompp::kBandBase; i < multicompp::kMeterMaster; ++i)
        {
            const int band = (i - multicompp::kBandBase) / 8;
            const auto d = multicompp::bandParam((i - multicompp::kBandBase) % 8, band);
            dsp.setMultibandParameter(band, d.core, multicompp::hostToPlain(
                d, values[static_cast<size_t>(i)].load(std::memory_order_relaxed)));
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
float multiCompGetBandGainReduction(void* p, int band) noexcept { return p ? asMultiComp(p)->bandGr(band) : 0.0f; }
float multiCompGetInputLevel(void* p) noexcept { return p ? asMultiComp(p)->inputLevel() : -60.0f; }
float multiCompGetOutputLevel(void* p) noexcept { return p ? asMultiComp(p)->outputLevel() : -60.0f; }
