#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace multicompp::plugin_detail
{
// The external sidechain's width, 0 (none), 1 or 2: the inputs the host reports
// on top of the main ones. Its ports keep their positions, 2-3, a mono sidechain
// on port 2. The output count cannot stand in for it: the AU { 2, 2 } layout is
// stereo without a sidechain, so a test against the channel count would read
// inputs[2] / inputs[3] as a key the host never feeds.
inline int externalSidechainWidth(int activeInputs, int mainChannels,
                                  const float* const* inputs) noexcept
{
    const int width = std::clamp(activeInputs - mainChannels, 0, 2);
    if (width == 0 || inputs == nullptr || inputs[2] == nullptr || (width == 2 && inputs[3] == nullptr))
        return 0;
    return width;
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
// DafPluginInfo.h opts into DAF_PLUGIN_SIDECHAIN_ON_EXTRA_IO, which an older DAF ignores without a word: the AU would
// refuse Logic's mono tracks again (no editor, no audio there).
#if DAF_PLUGIN_SIDECHAIN_ON_EXTRA_IO && !defined(DAF_SUPPORTS_SIDECHAIN_ON_EXTRA_IO)
#error "Multi-Comp 2 needs a DAF with DAF_PLUGIN_SIDECHAIN_ON_EXTRA_IO (the AU on Logic mono tracks)"
#endif
#include "MultiCompAccess.hpp"
#include "MultiCompOutputVu.hpp"
#include "MultiCompGainReductionNeedle.hpp"
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
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        {
            const float def = multicompp::resolveParameter(i,
                [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
                [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
            values[static_cast<size_t>(i)].store(def, std::memory_order_relaxed);
        }
        // One host latency in every mode (MultiCompDSP::getHostLatencySamples).
        dsp.setHostLatencyPadding(true);
    }

    float gr() const noexcept { return dsp.getGainReduction(); }
    float bandGr(int b) const noexcept { return dsp.getBandGainReduction(b); }
    float inputLevel() const noexcept { return dsp.getInputLevel(); }
    float outputLevel() const noexcept { return dsp.getOutputLevel(); }
    float outputVuLevel() const noexcept { return outputVu.levelDb(); }
    float gainReductionNeedle() const noexcept { return grNeedle.levelDb(); }
    float vcaGainReductionNeedle() const noexcept { return vcaGrNeedle.levelDb(); }
    float busMeterReading() const noexcept { return dsp.getBusMeterReading(); }
    float busFadePosition() const noexcept { return dsp.getBusFadePosition(); }
    // The UI's view of a parameter. Crossovers report the frequency the DSP
    // will use rather than the raw setting, so raising one handle visibly pushes
    // the handles above it. The host-facing getParameterValue() deliberately
    // does not do this: an automation lane must read back what it wrote.
    float parameterValue(uint32_t index) const noexcept
    {
        if (!multicompp::isControlParameter(static_cast<int>(index))) return 0.0f;
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
        if (multicompp::isControlParameter(static_cast<int>(index)))
        {
            multicompp::resolveParameter(static_cast<int>(index),
                [&](const multicompp::Param& d) {
                    p.name = d.name; p.symbol = d.id;
                    // Tapered parameters expose their honest normalized coordinate;
                    // the Opto controls add display-only callbacks below without
                    // changing their host/state/automation domain.
                    p.unit = multicompp::hasSkew(d) ? "" : d.unit;
                    p.ranges.min = multicompp::hostMin(d); p.ranges.max = multicompp::hostMax(d);
                    p.ranges.def = multicompp::hostDefault(d);
                    p.hints = kParameterIsAutomatable | (d.integer ? kParameterIsInteger : 0u);
                    using Id = multicompp::ParamId;
                    // Kept for sessions that saved them, but no mode reads them:
                    // the VCA face's attack, release, knee and detector are fixed.
                    if (multicompp::isInertParameter(index)) p.hints |= kParameterIsHidden;
                    switch (static_cast<Id>(index))
                    {
                        case Id::Bypass: p.initDesignation(kParameterDesignationBypass); return;
                        case Id::Mode: setEnum(p, multicompp::kModes, 8); break;
                        case Id::TruePeakEnable: case Id::ExternalSidechain: case Id::AutoMakeup:
                        case Id::OptoLimit: case Id::VcaOverEasy: case Id::DigitalAdaptive:
                        case Id::GlobalSidechainListen: case Id::NoiseEnable: case Id::BusFade:
                            setEnum(p, multicompp::kOnOff, 2); p.hints |= kParameterIsBoolean; break;
                        case Id::TruePeakQuality: setEnum(p, multicompp::kTruePeakQuality, 2); break;
                        case Id::Distortion: setEnum(p, multicompp::kDistortion, 4); break;
                        case Id::Oversampling: setEnum(p, multicompp::kOversampling, 3); break;
                        case Id::FetRatio: setEnum(p, multicompp::kRatios, 5); break;
                        case Id::OptoMeter: setEnum(p, multicompp::kOptoMeterModes, 3); break;
                        case Id::FetMeter: setEnum(p, multicompp::kFetMeterModes, 3); break;
                        case Id::FetCurve: setEnum(p, multicompp::kFetCurve, 2); break;
                        case Id::VcaClassicDetector: setEnum(p, multicompp::kVcaDetector, 2); break;
                        case Id::BusRatio: setEnum(p, multicompp::kBusRatios, 3); break;
                        case Id::BusAttack: setEnum(p, multicompp::kBusAttack, 6); break;
                        case Id::BusRelease: setEnum(p, multicompp::kBusRelease, 5); break;
                        case Id::BusHeadroom: setEnum(p, multicompp::kBusHeadroom, 7); break;
                        case Id::StereoLinkMode: setEnum(p, multicompp::kLinkMode, 3); break;
                        default: break;
                    }
                },
                [&](const multicompp::BandParam& d, int band) {
                    p.name = multicompp::bandHostName(d, band).c_str();
                    p.symbol = String(d.id) + "_" + std::to_string(band).c_str();
                    p.unit = multicompp::hasSkew(d) ? "" : d.unit;
                    p.ranges.min = multicompp::hostMin(d); p.ranges.max = multicompp::hostMax(d);
                    p.ranges.def = multicompp::hostDefault(d);
                    p.hints = kParameterIsAutomatable | (d.integer ? (kParameterIsInteger | kParameterIsBoolean) : 0u);
                    if (d.integer) setEnum(p, multicompp::kOnOff, 2);
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
        if (multicompp::isControlParameter(static_cast<int>(index))) return values[index].load(std::memory_order_relaxed);
        if (index == multicompp::kMeterMaster) return dsp.getGainReduction();
        if (index >= multicompp::kMeterBand0 && index <= multicompp::kMeterBand3)
            return dsp.getBandGainReduction(static_cast<int>(index - multicompp::kMeterBand0));
        return 0.0f;
    }

    bool hasCustomParameterText(uint32_t index) const override
    {
        return multicompp::usesOptoReferenceDisplay(index) || isTapered(index) || isVcaCompression(index);
    }

    bool getParameterValueText(uint32_t index, float value,
                               char* text, uint32_t size) const override
    {
        if (!hasCustomParameterText(index) || !text || size == 0
            || !std::isfinite(value)) return false;
        if (isVcaCompression(index))
            return multicompp::formatVcaCompressionText(value, text, size);
        if (isTapered(index))
            return multicompp::resolveParameter(static_cast<int>(index),
                [&](const multicompp::Param& d) { return multicompp::formatTaperedText(d, value, text, size); },
                [&](const multicompp::BandParam& d, int) { return multicompp::formatTaperedText(d, value, text, size); });
        const float display = std::nearbyint(
            multicompp::optoReferenceDisplayValue(value));
        const int written = std::snprintf(text, size, "%.0f", display);
        return written >= 0 && static_cast<uint32_t>(written) < size;
    }

    bool getParameterValueFromText(uint32_t index, const char* text,
                                   float& value) const override
    {
        if (isVcaCompression(index))
            return multicompp::parseVcaCompressionText(text, value);
        if (isTapered(index))
            return multicompp::resolveParameter(static_cast<int>(index),
                [&](const multicompp::Param& d) { return multicompp::parseTaperedText(d, text, value); },
                [&](const multicompp::BandParam& d, int) { return multicompp::parseTaperedText(d, text, value); });
        float display = 0.0f;
        if (!hasCustomParameterText(index) || !multicompp::parseOptoDisplayText(text, display))
            return false;
        value = multicompp::optoReferenceHostValue(display);
        return true;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (!multicompp::isControlParameter(static_cast<int>(index))) return;
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
        multicompp::applyFactoryPresetToHostParameters(index,
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
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            state[static_cast<size_t>(i)] = values[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        return String(multicompp::encodeState(state).c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "parameters") != 0 || value == nullptr) return;
        multicompp::StateValues state{};
        if (!multicompp::decodeState(value, state)) return;
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            setParameterValue(static_cast<uint32_t>(i), state[static_cast<size_t>(i)]);
    }

    void activate() override { prepare(getSampleRate(), getBufferSize()); }
    void deactivate() override { dsp.reset(); outputVu.reset(); grNeedle.reset(); vcaGrNeedle.reset(); }
    void sampleRateChanged(double sr) override { prepare(sr, getBufferSize()); }
    void bufferSizeChanged(uint32_t bs) override { prepare(getSampleRate(), bs); }

    void ioChanged(uint16_t in, uint16_t out) override
    {
        // DAF_PLUGIN_EXTRA_IO permits { 4, 2 }, { 2, 2 } and { 1, 1 }, and
        // DAF_PLUGIN_SIDECHAIN_ON_EXTRA_IO any of the main layouts plus a mono
        // or stereo sidechain ({ 2, 1 }, { 3, 1 }, { 3, 2 }). The processing
        // width is the output count; the input count is kept separately because
        // the sidechain is not audio to compress. DAF calls this while
        // deactivated, so run() observes a stable pair.
        activeInputs = static_cast<int>(in);
        activeChannels = out == 1 ? 1 : 2;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        duskaudio::ScopedFlushDenormals guard;
        if (frames == 0)
        {
            // Still publish a latched latency change: some hosts probe with
            // empty blocks and the CLAP wrapper picks the value up at the
            // next activate() regardless of block size.
            updateLatency();
            return;
        }
        const int sidechainWidth = values[static_cast<size_t>(multicompp::ParamId::ExternalSidechain)].load(std::memory_order_relaxed) > 0.5f
            ? multicompp::plugin_detail::externalSidechainWidth(activeInputs, activeChannels, inputs) : 0;
        if (sidechainWidth > 0)
        {
            // A mono sidechain keys every channel; a stereo one keys a mono instance by its mid, the usual mono
            // downmix of a key. (Hosts never pass more frames than the prepared block; one that did would key
            // the mono instance by the left channel.)
            const float* sc[2] = {inputs[2], sidechainWidth == 2 ? inputs[3] : inputs[2]};
            if (activeChannels == 1 && sidechainWidth == 2 && frames <= monoSidechain.size())
            {
                for (uint32_t i = 0; i < frames; ++i)
                    monoSidechain[i] = 0.5f * (inputs[2][i] + inputs[3][i]);
                sc[0] = sc[1] = monoSidechain.data();
            }
            dsp.processBlockExternal(inputs, sc, outputs, activeChannels, static_cast<int>(frames));
        }
        else dsp.processBlock(inputs, outputs, activeChannels, static_cast<int>(frames));
        outputVu.process(outputs, activeChannels, static_cast<int>(frames));
        const float gainReduction = dsp.getGainReduction();
        const long mode = currentMode();
        if (mode != needleMode)
        {   // a mode switch brings another face's meter into view: it already reads the gain reduction
            needleMode = mode;
            grNeedle.snap(gainReduction);
            vcaGrNeedle.snap(gainReduction);
        }
        else
        {
            grNeedle.process(gainReduction, static_cast<int>(frames));
            vcaGrNeedle.process(gainReduction, static_cast<int>(frames));
        }
        // setLatency() is documented as callable from run(). The CLAP wrapper
        // latches a change here and publishes it at the next activate(), which
        // is the only point the CLAP spec allows the latency to move.
        updateLatency();
    }

private:
    static bool isVcaCompression(uint32_t index) noexcept
    {
        return index == static_cast<uint32_t>(multicompp::ParamId::VcaRatio);
    }
    static bool isTapered(uint32_t index) noexcept
    {
        return multicompp::isControlParameter(static_cast<int>(index))
            && multicompp::resolveParameter(static_cast<int>(index),
                [](const multicompp::Param& d) { return multicompp::hasSkew(d); },
                [](const multicompp::BandParam& d, int) { return multicompp::hasSkew(d); });
    }
    static void setEnum(Parameter& p, const char* const* labels, int count)
    { p.enumValues.count = static_cast<uint8_t>(count); p.enumValues.restrictedMode = true; auto* e = new ParameterEnumerationValue[count]; for (int i = 0; i < count; ++i) e[i] = ParameterEnumerationValue(static_cast<float>(i), labels[i]); p.enumValues.values = e; }
    void pushParameters()
    {
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
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
    void prepare(double sampleRate, uint32_t bufferSize)
    {
        dsp.prepare(sampleRate, static_cast<int>(bufferSize));
        monoSidechain.assign(bufferSize, 0.0f);
        outputVu.prepare(sampleRate);
        grNeedle.prepare(sampleRate);
        vcaGrNeedle.prepare(sampleRate);
        needleMode = currentMode();   // needles start at rest and move; only a later switch snaps them
        pushParameters();
        updateLatency();
    }
    // Constant across modes (see MultiCompDSP::getHostLatencySamples); only
    // the lookahead controls move it.
    void updateLatency() { const int l = dsp.getHostLatencySamples(); if (l != lastLatency) { lastLatency = l; setLatency(static_cast<uint32_t>(l < 0 ? 0 : l)); } }

    duskaudio::MultiCompDSP dsp;
    multicompp::OutputVu outputVu;
    multicompp::GainReductionNeedle grNeedle{multicompp::kVuNeedle};        // Opto and FET faces
    multicompp::GainReductionNeedle vcaGrNeedle{multicompp::kVcaNeedle};    // VCA face
    std::vector<float> monoSidechain;   // run(): a mono instance's key from a stereo sidechain, its mid
    // Formats other than AU always present the full input set, and ioChanged()
    // is only called where a host narrows it, so the declared count is the
    // correct default rather than a placeholder.
    int activeInputs = DAF_PLUGIN_NUM_INPUTS;
    int activeChannels = DAF_PLUGIN_NUM_OUTPUTS;
    std::array<std::atomic<float>, multicompp::kTotalParamCount> values{};
    int lastLatency = -1;
    long needleMode = -1;   // the mode the needles last moved in (run() snaps them on a switch)
    long currentMode() const noexcept
    {
        return std::lround(values[static_cast<size_t>(multicompp::ParamId::Mode)].load(std::memory_order_relaxed));
    }
    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiCompPlugin)
};

Plugin* createPlugin() { return new MultiCompPlugin(); }
END_NAMESPACE_DAF

static DAF_NAMESPACE::MultiCompPlugin* asMultiComp(void* p) noexcept { return static_cast<DAF_NAMESPACE::MultiCompPlugin*>(p); }
float multiCompGetGainReduction(void* p) noexcept { return p ? asMultiComp(p)->gr() : 0.0f; }
float multiCompGetBandGainReduction(void* p, int band) noexcept { return p ? asMultiComp(p)->bandGr(band) : 0.0f; }
float multiCompGetInputLevel(void* p) noexcept { return p ? asMultiComp(p)->inputLevel() : -60.0f; }
float multiCompGetOutputLevel(void* p) noexcept { return p ? asMultiComp(p)->outputLevel() : -60.0f; }
float multiCompGetOutputVuLevel(void* p) noexcept { return p ? asMultiComp(p)->outputVuLevel() : -120.0f; }
float multiCompGetGainReductionNeedle(void* p) noexcept { return p ? asMultiComp(p)->gainReductionNeedle() : 0.0f; }
float multiCompGetVcaGainReductionNeedle(void* p) noexcept { return p ? asMultiComp(p)->vcaGainReductionNeedle() : 0.0f; }
float multiCompGetBusMeterReading(void* p) noexcept { return p ? asMultiComp(p)->busMeterReading() : 0.0f; }
float multiCompGetBusFadePosition(void* p) noexcept { return p ? asMultiComp(p)->busFadePosition() : 0.0f; }
float multiCompGetParameterValue(void* p, uint32_t index) noexcept
{ return p ? asMultiComp(p)->parameterValue(index) : 0.0f; }

#endif // MULTICOMP_PLUGIN_LOGIC_TEST
