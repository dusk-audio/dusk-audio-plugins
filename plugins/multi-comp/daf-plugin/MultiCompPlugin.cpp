#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace multicompp::plugin_detail
{
// The aux ports exist only on the full DAF_PLUGIN_NUM_INPUTS layout. The
// output count cannot stand in for that: the AU { 2, 2 } layout is stereo out
// with a two-channel input element, so a test against the channel count would
// index inputs[2]/inputs[3] past the end of a two-element array.
inline bool hasStereoExternalSidechainPorts(int activeInputs,
                                            const float* const* inputs) noexcept
{
    return activeInputs >= 4 && inputs != nullptr
        && inputs[2] != nullptr && inputs[3] != nullptr;
}

// The crossover frequencies the DSP will actually use, given the three the host
// has set. Mirrors MultiCompDSP::crossoverTargets(), which re-derives this every
// block from the raw parameters and never writes the result back.
//
// This must stay a pure read. Storing the ordered values into the parameter
// array ratchets: raising Crossover 1 pushes Crossover 2 up, and lowering
// Crossover 1 again leaves it there, so an automation pass permanently relocates
// a neighbour the host never wrote and the parameter no longer matches its lane.
inline std::array<float, 3> orderedCrossovers(float f1, float f2, float f3) noexcept
{
    const float o1 = f1 < 20.0f ? 20.0f : (f1 > 500.0f ? 500.0f : f1);
    const float o2 = f2 < o1 * 1.5f ? o1 * 1.5f : (f2 > 5000.0f ? 5000.0f : f2);
    const float o3 = f3 < o2 * 1.5f ? o2 * 1.5f : (f3 > 16000.0f ? 16000.0f : f3);
    return {{o1, o2, o3}};
}
} // namespace multicompp::plugin_detail

#ifndef MULTICOMP_PLUGIN_LOGIC_TEST

#include "DafPlugin.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"
#include "MultiCompVersion.hpp"
#include "util/CrashLog.hpp"

START_NAMESPACE_DAF

class MultiCompPlugin final : public Plugin
{
    DuskCrashLog::ScopedRegistration crashLog_ { "multi-comp-2", MULTICOMP2_VERSION_STRING };

public:
    MultiCompPlugin() : Plugin(multicompp::kTotalParamCount, multicompp::kFactoryPresets.size(), 1)
    {
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            const float def = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
            values[static_cast<size_t>(i)].store(def, std::memory_order_relaxed);
        }
    }

    float gr() const noexcept { return dsp.getGainReduction(); }
    float bandGr(int b) const noexcept { return dsp.getBandGainReduction(b); }
    float inputLevel() const noexcept { return dsp.getInputLevel(); }
    float outputLevel() const noexcept { return dsp.getOutputLevel(); }
    // The UI's view of a parameter. Crossovers report the frequency the DSP
    // will use rather than the raw setting, so raising one handle visibly pushes
    // the handles above it. The host-facing getParameterValue() deliberately
    // does not do this: an automation lane must read back what it wrote.
    float parameterValue(uint32_t index) const noexcept
    {
        if (index >= multicompp::kMeterMaster) return 0.0f;
        constexpr auto x1 = static_cast<uint32_t>(multicompp::ParamId::Crossover1);
        constexpr auto x3 = static_cast<uint32_t>(multicompp::ParamId::Crossover3);
        // plain(x1 + 1) and ordered[index - x1] below assume the three
        // crossover ids are contiguous.
        static_assert(x3 == x1 + 2,
                      "Crossover1..Crossover3 must be contiguous parameter ids");
        if (index < x1 || index > x3) return values[index].load(std::memory_order_relaxed);
        const auto plain = [this](uint32_t i) {
            return multicompp::hostToPlain(multicompp::kParams[i],
                                           values[i].load(std::memory_order_relaxed));
        };
        const auto ordered = multicompp::plugin_detail::orderedCrossovers(
            plain(x1), plain(x1 + 1), plain(x3));
        return multicompp::plainToHost(multicompp::kParams[index],
                                       ordered[index - x1]);
    }

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
        if (index < multicompp::kMeterMaster)
        {
            multicompp::resolveParameter(static_cast<int>(index),
                [&](const multicompp::Param& d) {
                    p.name = d.name; p.symbol = d.id;
                    // DAF has no arbitrary value<->text callback. Tapered parameters
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
                },
                [&](const multicompp::BandParam& d, int band) {
                    p.name = String(d.name) + " " + (band == 0 ? "Low" : band == 1 ? "Low-Mid" : band == 2 ? "High-Mid" : "High");
                    p.symbol = String(d.id) + "_" + std::to_string(band).c_str();
                    p.unit = multicompp::hasSkew(d) ? "" : d.unit;
                    p.ranges.min = multicompp::hostMin(d); p.ranges.max = multicompp::hostMax(d);
                    p.ranges.def = multicompp::hostDefault(d);
                    p.hints = kParameterIsAutomatable | (d.integer ? (kParameterIsInteger | kParameterIsBoolean) : 0u);
                });
            return;
        }
        // Output-only deliberately excludes kParameterIsAutomatable: hosts may
        // read these meters, but must never offer them as writable targets.
        p.hints = kParameterIsOutput;
        p.ranges.min = duskaudio::MultiCompDSP::kMinPublishedGainReductionDb;
        p.ranges.max = duskaudio::MultiCompDSP::kMaxPublishedGainReductionDb;
        p.ranges.def = 0.0f; p.unit = "dB";
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
        if (index >= multicompp::kMeterMaster) return;
        // Crossovers take the ordinary path: each one stores exactly what the
        // host set. The DSP re-derives the ordering from all three every block,
        // so nothing here needs to -- and must not -- rewrite its neighbours.
        multicompp::resolveParameter(static_cast<int>(index),
            [&](const multicompp::Param& d) {
                const float v = multicompp::snapHostValue(d, value);
                values[index].store(v, std::memory_order_relaxed);
                dsp.setParameter(d.core, multicompp::hostToPlain(d, v));
            },
            [&](const multicompp::BandParam& d, int band) {
                const float v = multicompp::snapHostValue(d, value);
                values[index].store(v, std::memory_order_relaxed);
                dsp.setMultibandParameter(band, d.core, multicompp::hostToPlain(d, v));
            });
    }

    void initProgramName(uint32_t index, String& name) override
    { if (index < multicompp::kFactoryPresets.size()) name = multicompp::kFactoryPresets[index].name; }

    void loadProgram(uint32_t index) override
    {
        if (index >= multicompp::kFactoryPresets.size()) return;
        const auto& q = multicompp::kFactoryPresets[index];
        multicompp::applyPresetToHostParameters(q,
        [this](int parameterIndex, float hostValue)
        {
            setParameterValue(static_cast<uint32_t>(parameterIndex), hostValue);
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

    void ioChanged(uint16_t in, uint16_t out) override
    {
        // DAF_PLUGIN_EXTRA_IO permits { 4, 2 }, { 2, 2 } and { 1, 1 }. The
        // input count is kept separately from the processing width because the
        // two disagree on { 4, 2 }, where the extra pair is the sidechain and
        // not audio to compress. DAF calls this while deactivated, so run()
        // observes a stable pair.
        activeInputs = static_cast<int>(in);
        activeChannels = (in == 1 && out == 1) ? 1 : 2;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        if (frames == 0)
        {
            // Still publish a latched latency change: some hosts probe with
            // empty blocks and the CLAP wrapper picks the value up at the
            // next activate() regardless of block size.
            updateLatency();
            return;
        }
        const bool useSc = values[static_cast<size_t>(multicompp::ParamId::ExternalSidechain)].load(std::memory_order_relaxed) > 0.5f
            && multicompp::plugin_detail::hasStereoExternalSidechainPorts(activeInputs, inputs);
        if (useSc) { const float* sc[2] = {inputs[2], inputs[3]}; dsp.processBlockExternal(inputs, sc, outputs, activeChannels, static_cast<int>(frames)); }
        else dsp.processBlock(inputs, outputs, activeChannels, static_cast<int>(frames));
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
        for (int i = 0; i < multicompp::kMeterMaster; ++i)
        {
            const float value = values[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            multicompp::resolveParameter(i,
                [&](const multicompp::Param& d) {
                    dsp.setParameter(d.core, multicompp::hostToPlain(d, value));
                },
                [&](const multicompp::BandParam& d, int band) {
                    dsp.setMultibandParameter(band, d.core, multicompp::hostToPlain(d, value));
                });
        }
    }
    void updateLatency() { const int l = dsp.getLatencySamples(); if (l != lastLatency) { lastLatency = l; setLatency(static_cast<uint32_t>(l < 0 ? 0 : l)); } }

    duskaudio::MultiCompDSP dsp;
    // Formats other than AU always present the full input set, and ioChanged()
    // is only called where a host narrows it, so the declared count is the
    // correct default rather than a placeholder.
    int activeInputs = DAF_PLUGIN_NUM_INPUTS;
    int activeChannels = DAF_PLUGIN_NUM_OUTPUTS;
    std::array<std::atomic<float>, multicompp::kMeterMaster> values{};
    int lastLatency = -1;
    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiCompPlugin)
};

Plugin* createPlugin() { return new MultiCompPlugin(); }
END_NAMESPACE_DAF

static DAF_NAMESPACE::MultiCompPlugin* asMultiComp(void* p) noexcept { return static_cast<DAF_NAMESPACE::MultiCompPlugin*>(p); }
float multiCompGetGainReduction(void* p) noexcept { return p ? asMultiComp(p)->gr() : 0.0f; }
float multiCompGetBandGainReduction(void* p, int band) noexcept { return p ? asMultiComp(p)->bandGr(band) : 0.0f; }
float multiCompGetInputLevel(void* p) noexcept { return p ? asMultiComp(p)->inputLevel() : -60.0f; }
float multiCompGetOutputLevel(void* p) noexcept { return p ? asMultiComp(p)->outputLevel() : -60.0f; }
float multiCompGetParameterValue(void* p, uint32_t index) noexcept
{ return p ? asMultiComp(p)->parameterValue(index) : 0.0f; }

#endif // MULTICOMP_PLUGIN_LOGIC_TEST
