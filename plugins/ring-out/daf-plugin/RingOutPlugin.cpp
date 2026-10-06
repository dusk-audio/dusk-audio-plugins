// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutPlugin.cpp — DAF shell around the framework-free RingOutDSP engine.
//
// Two kinds of plugin data cross this boundary:
//
//   * host PARAMETERS (RingOutParams.hpp): SENSE, SETUP, ADD, RESET, the global
//     Q / AMP / LINK, GAIN OUT, bypass, and the two meter outputs. Cached in
//     relaxed atomics for run() exactly like the other DAF plugins.
//
//   * the FILTER TABLE, as plugin STATE. The detection engine adds and deepens
//     filters on the audio thread, which no host parameter path can express, so
//     the DSP object owns the table and the host reads it back through
//     getState("filters") when it saves (DAF_PLUGIN_WANT_FULL_STATE). The UI
//     edits it through a second, never-persisted key, "edit", carrying one
//     command at a time (RingOutFilterTable.hpp, EditCommand); getState() for
//     that key is always empty so a saved session replays nothing.

#include "DafPlugin.hpp"
#include "RingOutAccess.hpp"
#include "RingOutDSP.hpp"
#include "RingOutParams.hpp"
#include "RingOutVersion.hpp"
#include "util/CrashLog.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

START_NAMESPACE_DAF

class RingOutPlugin : public Plugin
{
    DuskCrashLog::ScopedRegistration crashLog_ { "Ring Out", RO_VERSION_STRING };

public:
    RingOutPlugin()
        : Plugin(kParamCount, kRoNumFactoryPresets, 2 /* states: filters, edit */)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i].store(kRoParams[i].def, std::memory_order_relaxed);
    }

    // Same-process UI bridge (RingOutAccess.hpp).
    const duskaudio::RingOutDSP* dspForUI() const noexcept { return &dsp; }

protected:
    //--- metadata --------------------------------------------------------------
    const char* getLabel() const override       { return "RingOut"; }
    const char* getDescription() const override
    {
        // No raw '&', '<' or '>' here: DAF writes this verbatim into the AU plist.
        return "Feedback eliminator for ringing out monitors and PA: finds the "
               "frequencies that ring and notches them, up to twenty filters.";
    }
    const char* getMaker() const override       { return "Dusk Audio"; }
    const char* getHomePage() const override    { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override     { return "GPL-3.0-or-later"; }
    uint32_t    getVersion() const override
    { return d_version(RO_VERSION_MAJOR, RO_VERSION_MINOR, RO_VERSION_PATCH); }
    int64_t     getUniqueId() const override    { return d_cconst('D', 's', 'R', 'O'); } // must match DAF_PLUGIN_UNIQUE_ID

    //--- parameters ------------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index >= kParamCount)
            return;
        const RoParam& d = kRoParams[index];
        p.name = d.name;
        p.symbol = d.id;
        p.ranges.def = d.def;
        p.ranges.min = d.min;
        p.ranges.max = d.max;
        switch (d.kind)
        {
        case RoParam::kFloat:
            p.hints = kParameterIsAutomatable;
            break;
        case RoParam::kBool:
            p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
            if (index == kParamSense)
            {
                p.enumValues.count = 2;
                p.enumValues.restrictedMode = true;
                auto* const e = new ParameterEnumerationValue[2];
                e[0] = ParameterEnumerationValue(0.0f, "Low");
                e[1] = ParameterEnumerationValue(1.0f, "High");
                p.enumValues.values = e;
            }
            break;
        case RoParam::kTrigger:
            p.hints = kParameterIsAutomatable | kParameterIsTrigger | kParameterIsInteger;
            break;
        case RoParam::kBypass:
            p.initDesignation(kParameterDesignationBypass);
            break;
        case RoParam::kOutput:
            p.hints = kParameterIsAutomatable | kParameterIsOutput;
            p.unit = "dB";
            break;
        }
        if (index == kParamGlobalAmp || index == kParamGainOut)
            p.unit = "dB";
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == kParamInLevel)
            return std::max(dsp.inputPeakDb(0), dsp.inputPeakDb(1));
        if (index == kParamOutLevel)
            return std::max(dsp.outputPeakDb(0), dsp.outputPeakDb(1));
        return index < kParamCount ? values[index].load(std::memory_order_relaxed) : 0.0f;
    }

    // Every DSP setter is an atomic store or a spin-lock whose holder only
    // copies a few hundred bytes, so this is safe from whichever thread the
    // host uses.
    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamCount || index == kParamInLevel || index == kParamOutLevel
            || !std::isfinite(value))
            return;
        value = roNormalizeParamValue(index, value);
        values[index].store(value, std::memory_order_relaxed);
        switch (index)
        {
        case kParamSense:     dsp.setSense(value >= 0.5f ? 1 : 0); break;
        case kParamSetup:     dsp.setSetup(value >= 0.5f);         break;
        case kParamAdd:       dsp.setAdd(value >= 0.5f);           break;
        case kParamReset:     if (value >= 0.5f) dsp.resetFilters(); break;
        case kParamGlobalQ:   dsp.setGlobalQ(value);               break;
        case kParamGlobalAmp: dsp.setGlobalAmpDb(value);           break;
        case kParamLink:      break;   // UI-side coupling of AMP and GAIN OUT
        case kParamGainOut:   dsp.setGainOutDb(value);             break;
        case kParamBypass:    dsp.setBypass(value >= 0.5f);        break;
        default: break;
        }
    }

    //--- programs --------------------------------------------------------------
    void initProgramName(uint32_t index, String& programName) override
    {
        if (index < (uint32_t)kRoNumFactoryPresets)
            programName = kRoFactoryPresetNames[index];
    }

    // "Default": every setting at its default and no filters. The engine
    // switches and the host bypass are not preset content and are left alone.
    void loadProgram(uint32_t index) override
    {
        if (index >= (uint32_t)kRoNumFactoryPresets)
            return;
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (roIsPresetParam(i))
                setParameterValue(i, kRoParams[i].def);
        dsp.resetFilters();
    }

    //--- state -----------------------------------------------------------------
    void initState(uint32_t index, State& state) override
    {
        switch (index)
        {
        case 0:
            state.key = "filters";
            state.label = "Filters";
            state.defaultValue = "";
            state.description = "The notch filters: on,frequency,cut,Q per entry, ';' separated.";
            state.hints = kStateIsHostReadable;
            break;
        case 1:
            // UI -> DSP command channel. Never carries a value when the host
            // asks (getState), so nothing is replayed on a session load.
            state.key = "edit";
            state.label = "Filter edit command";
            state.defaultValue = "";
            state.hints = kStateIsOnlyForDSP;
            break;
        default:
            break;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "filters") == 0)
        {
            duskaudio::ringout::FilterTable table;
            dsp.getTable(table);
            char buf[duskaudio::ringout::kTableTextCapacity];
            if (duskaudio::ringout::serializeTable(table, buf, (int)sizeof(buf)))
                return String(buf);
        }
        return String();
    }

    bool validateStateValue(const char* key, const char* value) const override
    {
        if (key == nullptr || value == nullptr)
            return false;
        if (std::strcmp(key, "filters") == 0)
        {
            duskaudio::ringout::FilterTable table;
            return duskaudio::ringout::parseTable(value, table);
        }
        if (std::strcmp(key, "edit") == 0)
        {
            if (value[0] == '\0')
                return true;
            duskaudio::ringout::EditCommand command;
            return duskaudio::ringout::parseEditCommand(value, command);
        }
        return false;
    }

    void setState(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr)
            return;
        if (std::strcmp(key, "filters") == 0)
        {
            duskaudio::ringout::FilterTable table;
            if (duskaudio::ringout::parseTable(value, table))   // all-or-nothing
                dsp.setTable(table);
        }
        else if (std::strcmp(key, "edit") == 0 && value[0] != '\0')
        {
            duskaudio::ringout::EditCommand command;
            if (duskaudio::ringout::parseEditCommand(value, command))
                dsp.applyEdit(command);
        }
    }

    //--- lifecycle ---------------------------------------------------------------
    void activate() override
    {
        dsp.prepare(getSampleRate(), (int)getBufferSize());
        pushAllParams();
    }

    void deactivate() override { dsp.reset(); }

    void sampleRateChanged(double newSampleRate) override
    {
        dsp.prepare(newSampleRate, (int)getBufferSize());
        pushAllParams();
    }

    void ioChanged(uint16_t numInputs, uint16_t numOutputs) override
    {
        // DAF_PLUGIN_EXTRA_IO permits only matched mono or stereo layouts, and
        // DAF calls this while deactivated.
        activeChannels = (numInputs == 1 && numOutputs == 1) ? 1 : 2;
    }

    //--- audio -------------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        dsp.processBlock(inputs, outputs, activeChannels, (int)frames);
    }

private:
    void pushAllParams()
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (i == kParamInLevel || i == kParamOutLevel || i == kParamReset)
                continue;
            setParameterValue(i, values[i].load(std::memory_order_relaxed));
        }
    }

    duskaudio::RingOutDSP dsp;
    int activeChannels = DAF_PLUGIN_NUM_INPUTS;
    std::atomic<float> values[kParamCount] = {};

    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingOutPlugin)
};

Plugin* createPlugin()
{
    return new RingOutPlugin();
}

END_NAMESPACE_DAF

// Same-process UI accessor (see RingOutAccess.hpp).
const duskaudio::RingOutDSP* ringOutGetDSP(void* const pluginInstancePointer) noexcept
{
    auto* const plugin = static_cast<DAF_NAMESPACE::RingOutPlugin*>(pluginInstancePointer);
    return plugin != nullptr ? plugin->dspForUI() : nullptr;
}
