// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbPlugin.cpp — the DAF shell for DuskVerb 2.
//
// Everything audible lives in ../core/DuskVerbDSP; this file is the parameter,
// program and state plumbing, plus the strong definitions of the UI accessor
// bridge declared in DuskVerbAccess.hpp.

#include "DafPlugin.hpp"
#include "DuskVerbAccess.hpp"
#include "DuskVerbParams.hpp"
#include "DuskVerbControlState.hpp"
#include "DuskVerbFormat.hpp"
#include "DuskDenormals.hpp"
#include "DuskVerbVersion.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

START_NAMESPACE_DAF

class DuskVerbPlugin final : public Plugin
{
public:
    DuskVerbPlugin()
        : Plugin(duskverb::kTotalParamCount,
                 static_cast<uint32_t>(duskverb::factoryPresetCount()), 1)
    {
        for (int i = 0; i < duskverb::kNumParams; ++i)
        {
            lastHostParams_[i] = duskverb::hostDefault(duskverb::paramDesc(i));
            plainParams_[i] = duskverb::dspFromHost(duskverb::paramDesc(i), lastHostParams_[i]);
        }
    }

    // ── UI bridge ───────────────────────────────────────────────────────────
    float inputLevelL()  const noexcept { return dsp_.getInputLevelL(); }
    float inputLevelR()  const noexcept { return dsp_.getInputLevelR(); }
    float outputLevelL() const noexcept { return dsp_.getOutputLevelL(); }
    float outputLevelR() const noexcept { return dsp_.getOutputLevelR(); }
    int   currentProgram() const noexcept { return controls_.sound().program; }
    int   tailHistory(float* dest, int maxCount) const noexcept
    { return dsp_.getTailHistory(dest, maxCount); }
    float cpuLoad() const noexcept { return cpuLoad_.load(std::memory_order_relaxed); }
    duskverb::StateValues snapshot() const { return controls_.snapshot(); }
    duskverb::EditorState* editorState() noexcept { return &editorState_; }
    uint64_t stateRevision() const noexcept
    {
        const auto sound = controls_.sound();
        return (uint64_t(sound.epoch) << 1) | uint64_t(sound.edited);
    }
    void  applyPresetConfig(int index) noexcept
    {
        // Compatibility bridge while editors migrate to complete state recall.
        // It now publishes an immutable snapshot; it never touches live engines.
        auto state = controls_.snapshot();
        const auto& presets = getFactoryPresets();
        if (index >= static_cast<int>(presets.size())) return;
        state.presetName = index < 0 ? "" : presets[index].name;
        state.sixAP = {};
        if (index >= 0)
        {
            const auto& preset = presets[index];
            state.sixAP.densityBaseline = preset.sixAPDensityBaseline;
            state.sixAP.bloomCeiling = preset.sixAPBloomCeiling;
            state.sixAP.earlyMix = preset.sixAPEarlyMix;
            state.sixAP.outputTrim = preset.sixAPOutputTrim;
            std::copy_n(preset.sixAPBloomStagger, 6, state.sixAP.bloomStagger);
        }
        controls_.publish(state);
    }

protected:
    const char* getLabel() const override       { return "DuskVerb2"; }
    const char* getDescription() const override { return "Algorithmic reverb"; }
    const char* getMaker() const override       { return "Dusk Audio"; }
    const char* getHomePage() const override    { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override     { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override
    { return d_version(DUSKVERB2_VERSION_MAJOR, DUSKVERB2_VERSION_MINOR, DUSKVERB2_VERSION_PATCH); }
    int64_t getUniqueId() const override { return d_cconst('D','s','D','v'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = 0;
        if (input)
        {
            port.name   = index == 0 ? "Input Left" : "Input Right";
            port.symbol = index == 0 ? "input_l" : "input_r";
        }
        else
        {
            port.name   = index == 0 ? "Output Left" : "Output Right";
            port.symbol = index == 0 ? "output_l" : "output_r";
        }
    }

    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        if (groupId == 0) { group.name = "Main"; group.symbol = "main"; }
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index >= static_cast<uint32_t>(duskverb::kNumParams)) return;

        const duskverb::ParamDesc& d = duskverb::paramDesc(static_cast<int>(index));
        p.name   = d.name;
        p.symbol = d.id;
        // Nonlinear parameters use JUCE's normalized automation coordinate;
        // optional framework text callbacks supply their musical units.
        p.unit = d.unit;
        p.ranges.min = duskverb::hostMin(d);
        p.ranges.max = duskverb::hostMax(d);
        p.ranges.def = duskverb::hostDefault(d);
        p.hints = kParameterIsAutomatable | (d.integer ? kParameterIsInteger : 0u);

        if (d.enumLabels != nullptr)
        {
            setEnum(p, d.enumLabels, d.enumCount);
            if (d.enumCount == 2) p.hints |= kParameterIsBoolean;
        }
        // After the labels: the designation fixes the range and hints a host
        // expects of a bypass, and the Off/On labels are what the JUCE build's
        // host text shows for it.
        if (index == static_cast<uint32_t>(duskverb::Bypass))
            p.initDesignation(kParameterDesignationBypass);
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= static_cast<uint32_t>(duskverb::kNumParams)) return 0.0f;
        return controls_.parameter(static_cast<int>(index));
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= static_cast<uint32_t>(duskverb::kNumParams)) return;
        controls_.setParameter(static_cast<int>(index), value);
    }

    bool hasCustomParameterText(uint32_t index) const override
    { return index < duskverb::kNumParams && duskverb::paramDesc(index).enumLabels == nullptr; }

    bool getParameterValueText(uint32_t index, float value, char* text, uint32_t size) const override
    {
        if (!hasCustomParameterText(index) || !text || size == 0 || !duskverb::finiteFloat(value)) return false;
        const auto& desc = duskverb::paramDesc(index);
        const int algorithm = static_cast<int>(controls_.parameter(duskverb::Algorithm));
        char formatted[96];
        duskverb::ui::formatPlainValue(getAlgorithmConfig(algorithm).engine, index,
                                      duskverb::dspFromHost(desc, value), formatted, sizeof(formatted));
        if (std::strlen(formatted) >= size) { text[0] = '\0'; return false; }
        std::memcpy(text, formatted, std::strlen(formatted) + 1);
        return true;
    }

    bool getParameterValueFromText(uint32_t index, const char* text, float& value) const override
    {
        if (!hasCustomParameterText(index) || !text) return false;
        const auto& desc = duskverb::paramDesc(index);
        const int algorithm = static_cast<int>(controls_.parameter(duskverb::Algorithm));
        float plain;
        if (!duskverb::ui::parsePlainValue(getAlgorithmConfig(algorithm).engine, index, text,
              duskverb::dspFromHost(desc, controls_.parameter(index)), plain)) return false;
        value = duskverb::plainToHost(desc, plain);
        return true;
    }

    // ── Programs ────────────────────────────────────────────────────────────
    void initProgramName(uint32_t index, String& name) override
    {
        const auto& presets = getFactoryPresets();
        if (index < presets.size()) name = presets[index].name;
    }

    void loadProgram(uint32_t index) override
    {
        controls_.loadProgram(index);
    }

    int32_t getCurrentProgram() const override { return currentProgram(); }

    // ── State ───────────────────────────────────────────────────────────────
    void initState(uint32_t index, State& state) override
    {
        if (index != 0) return;
        state.key = "parameters";
        state.label = "DuskVerb Parameters";
        state.defaultValue = "";
        state.hints = kStateIsHostReadable | kStateIsParameterSnapshot;
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "parameters") != 0) return String();
        return String(duskverb::encodeState(controls_.snapshot()).c_str());
    }

    bool validateStateValue(const char* key, const char* value) const override
    {
        duskverb::StateValues state;
        return key != nullptr && value != nullptr && std::strcmp(key, "parameters") == 0
            && duskverb::decodeState(value, state);
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "parameters") != 0 || value == nullptr) return;
        duskverb::StateValues state{};
        if (!duskverb::decodeState(value, state)) return;   // all-or-nothing

        controls_.publish(state);
    }

    // ── Lifecycle ───────────────────────────────────────────────────────────
    // 30 s, the value the JUCE build returns from getTailLengthSeconds(). Frames at
    // the current sample rate, so it is re-sent whenever the rate changes.
    static constexpr double kTailSeconds = 30.0;
    void reportTail(double sampleRate) noexcept
    {
        setTail(static_cast<uint32_t>(kTailSeconds * sampleRate + 0.5));
    }

    void activate() override
    {
        synchronizeControls(true);
        dsp_.prepare(getSampleRate(), static_cast<int>(getBufferSize()));
        reportTail(getSampleRate());
    }
    // releaseResources(), not reset(): see DuskVerbDSP::releaseResources(). The
    // JUCE build clears nothing on deactivate, and matching it is what keeps the
    // per-preset null test bit-identical across a host deactivate/reactivate.
    void deactivate() override { dsp_.releaseResources(); }
    void sampleRateChanged(double sr) override
    {
        synchronizeControls(true);
        dsp_.prepare(sr, static_cast<int>(getBufferSize()));
        reportTail(sr);
    }
    void bufferSizeChanged(uint32_t bs) override { dsp_.prepare(getSampleRate(), static_cast<int>(bs)); }

    void ioChanged(uint16_t in, uint16_t out) override
    {
        activeInputs_ = in;
        activeOutputs_ = out;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        duskaudio::ScopedFlushDenormals noDenormals;
        if (frames == 0) return;
        // steady_clock::now() is a vDSO read here: no allocation, no lock, no
        // syscall, which is the whole bar this has to clear to sit in run().
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

        const TimePosition& t = getTimePosition();
        synchronizeControls(false);
        const bool tempoValid = t.bpmValid || t.bbt.valid;
        dsp_.setHostBpm(tempoValid ? t.bbt.beatsPerMinute : 0.0, tempoValid);
        dsp_.processBlock(inputs, outputs, activeInputs_, activeOutputs_, static_cast<int>(frames));

        // Load = time spent / time produced, smoothed. The coefficient is per
        // BLOCK rather than per second on purpose: it costs no extra clock read,
        // and at any sane block size it settles in well under a second, which is
        // all a read-out refreshed at frame rate needs.
        const double sampleRate = getSampleRate();
        if (sampleRate > 0.0)
        {
            const double blockSeconds = static_cast<double>(frames) / sampleRate;
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            const float instant = static_cast<float>(elapsed / blockSeconds);
            const float previous = cpuLoad_.load(std::memory_order_relaxed);
            cpuLoad_.store(previous + 0.02f * (instant - previous), std::memory_order_relaxed);
        }
    }

private:
    void synchronizeControls(bool stopped) noexcept
    {
        const auto sound = controls_.sound();
        // Recall waits for the existing 50 ms fade to finish as one unit.
        // Ordinary automation reaches the active engine on every other block.
        if (sound.recallEpoch != appliedEpoch_ && !stopped && !dsp_.canRecall())
        {
            dsp_.setParameter(duskverb::Bypass, sound.params[duskverb::Bypass]);
            return;
        }
        for (int i = 0; i < duskverb::kNumParams; ++i)
            if (lastHostParams_[i] != sound.params[i])
            {
                lastHostParams_[i] = sound.params[i];
                plainParams_[i] = duskverb::dspFromHost(duskverb::paramDesc(i), sound.params[i]);
            }
        if (sound.recallEpoch != appliedEpoch_)
        {
            dsp_.stageSound(plainParams_, sound.preset, sound.sixAP);
            appliedEpoch_ = sound.recallEpoch;
        }
        else
            for (int i = 0; i < duskverb::kNumParams; ++i) dsp_.setParameter(i, plainParams_[i]);
    }

    static void setEnum(Parameter& p, const char* const* labels, int count)
    {
        p.enumValues.count = static_cast<uint8_t>(count);
        p.enumValues.restrictedMode = true;
        auto* e = new ParameterEnumerationValue[count];
        for (int i = 0; i < count; ++i)
            e[i] = ParameterEnumerationValue(static_cast<float>(i), labels[i]);
        p.enumValues.values = e;
    }

    duskverb::DuskVerbDSP dsp_;
    duskverb::ControlState controls_;
    duskverb::EditorState editorState_;
    std::array<float, duskverb::kNumParams> lastHostParams_{};
    std::array<float, duskverb::kNumParams> plainParams_{};
    uint32_t appliedEpoch_ = 1;
    // Written by run(), read by the editor. Relaxed: it is a read-out, not a
    // state flag, and no other value is ordered against it.
    std::atomic<float> cpuLoad_ { 0.0f };
    int activeInputs_ = DAF_PLUGIN_NUM_INPUTS;
    int activeOutputs_ = DAF_PLUGIN_NUM_OUTPUTS;

    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuskVerbPlugin)
};

Plugin* createPlugin() { return new DuskVerbPlugin(); }

END_NAMESPACE_DAF

static DAF_NAMESPACE::DuskVerbPlugin* asDuskVerb(void* p) noexcept
{ return static_cast<DAF_NAMESPACE::DuskVerbPlugin*>(p); }

float duskVerbGetInputLevelL (void* p) noexcept { return p ? asDuskVerb(p)->inputLevelL()  : -100.0f; }
float duskVerbGetInputLevelR (void* p) noexcept { return p ? asDuskVerb(p)->inputLevelR()  : -100.0f; }
float duskVerbGetOutputLevelL(void* p) noexcept { return p ? asDuskVerb(p)->outputLevelL() : -100.0f; }
float duskVerbGetOutputLevelR(void* p) noexcept { return p ? asDuskVerb(p)->outputLevelR() : -100.0f; }
int   duskVerbGetCurrentProgram(void* p) noexcept { return p ? asDuskVerb(p)->currentProgram() : -1; }
int   duskVerbGetTailHistory(void* p, float* dest, int maxCount) noexcept
{ return p ? asDuskVerb(p)->tailHistory(dest, maxCount) : 0; }
float duskVerbGetCpuLoad(void* p) noexcept { return p ? asDuskVerb(p)->cpuLoad() : 0.0f; }
void  duskVerbApplyPresetConfig(void* p, int index) noexcept
{ if (p) asDuskVerb(p)->applyPresetConfig(index); }
bool duskVerbReadSnapshot(void* p, duskverb::StateValues& state)
{ if (!p) return false; state = asDuskVerb(p)->snapshot(); return true; }
duskverb::EditorState* duskVerbEditorState(void* p) noexcept
{ return p ? asDuskVerb(p)->editorState() : nullptr; }
uint64_t duskVerbStateRevision(void* p) noexcept
{ return p ? asDuskVerb(p)->stateRevision() : 0; }
