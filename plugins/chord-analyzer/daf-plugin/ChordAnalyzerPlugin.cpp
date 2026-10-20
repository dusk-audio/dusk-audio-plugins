// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// DAF shell shared by the instrument and MIDI-only successor. The native
// shipping headless LV2 remains separate, with its original URI and 19 ports.
#include "DafPlugin.hpp"
#include "ChordAnalyzerRuntime.hpp"
#include "DuskDenormals.hpp"
#include <algorithm>
#include <cmath>

START_NAMESPACE_DAF
using namespace duskaudio::chord;

class ChordAnalyzerPlugin : public Plugin
{
public:
    ChordAnalyzerPlugin() : Plugin(PARAM_COUNT, 0, 0) {}
    ChordAnalyzerRuntime& runtimeForUI() noexcept { return runtime; }

protected:
    const char* getLabel() const override
    {
       #if CHORD_ANALYZER_DAF_MIDI_MODE
        return "ChordAnalyzerMIDI2";
       #else
        return "ChordAnalyzer2";
       #endif
    }
    const char* getDescription() const override
    { return "MIDI chord detection, harmonic suggestions and chord recording."; }
    const char* getMaker() const override { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override
    { return d_version(CA2_VERSION_MAJOR, CA2_VERSION_MINOR, CA2_VERSION_PATCH); }
    int64_t getUniqueId() const override
    {
       #if CHORD_ANALYZER_DAF_MIDI_MODE
        return d_cconst('D', 's', 'C', 'M');
       #else
        return d_cconst('D', 's', 'C', 'A');
       #endif
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index >= PARAM_COUNT) return;
        const auto& spec = parameterSpecs[index];
        p.name = spec.name;
        p.symbol = spec.symbol;
        p.ranges.min = 0.0f;
        p.ranges.max = spec.maximum;
        p.ranges.def = spec.initial;
        p.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        // A 295k-step indexed AU parameter is rejected by some hosts. Retain
        // the encoded label as a continuous parameter carrying integer codes.
        if (index != PARAM_DETECTED_CHORD) p.hints |= kParameterIsInteger;
        if (spec.boolean) p.hints |= kParameterIsBoolean;
        const auto labels = parameterChoices(index);
        if (labels.count != 0)
        {
            p.enumValues.count = static_cast<uint8_t>(labels.count);
            p.enumValues.restrictedMode = true;
            auto* values = new ParameterEnumerationValue[labels.count];
            for (unsigned i = 0; i < labels.count; ++i)
                values[i] = ParameterEnumerationValue(static_cast<float>(i), labels.labels[i]);
            p.enumValues.values = values;
        }
    }

    float getParameterValue(uint32_t index) const override { return runtime.parameter(index); }
    void setParameterValue(uint32_t index, float value) override { runtime.setParameter(index, value); }
    void activate() override { runtime.prepare(getSampleRate()); }
    void deactivate() override { runtime.release(); }

   #if !CHORD_ANALYZER_DAF_MIDI_MODE
    void ioChanged(uint16_t, uint16_t outputs) override
    { outputChannels = std::min<unsigned>(outputs, DAF_PLUGIN_NUM_OUTPUTS); }
   #endif

    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* events, uint32_t eventCount) override
    {
        const duskaudio::ScopedFlushDenormals noDenormals;
        if (frames == 0) return;
       #if !CHORD_ANALYZER_DAF_MIDI_MODE
        for (unsigned ch = 0; ch < outputChannels; ++ch)
            if (outputs && outputs[ch]) std::fill_n(outputs[ch], frames, 0.0f);
       #else
        (void)outputs;
       #endif
        runtime.beginBlock();
        bool outputFull = false;
        for (uint32_t i = 0; events && i < eventCount; ++i)
        {
            const auto& event = events[i];
            const auto* data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
            // Do not dereference malformed extended payloads or forward
            // out-of-block timestamps. Unknown/long messages pass unchanged.
            if (event.frame >= frames || event.size == 0 || !data) continue;
            runtime.midi(data, event.size);
            if (!outputFull && !writeMidiEvent(event)) outputFull = true;
        }
        const auto& position = getTimePosition();
        const double bpm = position.bpmValid || position.bbt.valid ? position.bbt.beatsPerMinute : 120.0;
        runtime.endBlock(frames, bpm);
    }

private:
    ChordAnalyzerRuntime runtime;
   #if !CHORD_ANALYZER_DAF_MIDI_MODE
    unsigned outputChannels = DAF_PLUGIN_NUM_OUTPUTS;
   #endif
};

Plugin* createPlugin() { return new ChordAnalyzerPlugin; }
END_NAMESPACE_DAF
