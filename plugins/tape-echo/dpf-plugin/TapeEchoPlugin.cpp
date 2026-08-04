// TapeEchoPlugin.cpp — DPF shell around the framework-free TapeEchoDSP core.

#include "DistrhoPlugin.hpp"
#include "TapeEchoAccess.hpp"
#include "TapeEchoDSP.hpp"
#include "TapeEchoParams.hpp"
#include "TapeEchoVersion.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

START_NAMESPACE_DISTRHO

class TapeEchoPlugin : public Plugin
{
public:
    TapeEchoPlugin()
        : Plugin(kParamCount, kNumFactoryPresets, 0)
    {
        values[kParamMode]        = 1.0f;
        values[kParamRepeatRate]  = 0.0f;
        values[kParamIntensity]   = 0.0f;
        values[kParamEchoLevel]   = 0.5f;
        values[kParamReverbLevel] = 0.0f;
        values[kParamBass]        = 0.0f;
        values[kParamTreble]      = 0.0f;
        values[kParamInputGain]   = 0.5f;
        values[kParamWowFlutter]  = 0.0f;
        values[kParamDryLevel]    = 1.0f;
        values[kParamTempoSync]   = 0.0f;
        values[kParamSyncDivision] = 2.0f; // 1/16
        values[kParamTapeAge]     = 0.5f;
        values[kParamOutputVolume] = 0.5f;
        values[kParamEchoPan]     = 0.5f;
        values[kParamReverbPan]   = 0.5f;
        values[kParamInputSend]   = 1.0f;
        values[kParamWetSolo]     = 0.0f;
        values[kParamBypass]      = 0.0f;
        values[kParamPeakLevel]   = 0.0f;
        values[kParamMix]         = 0.5f;
    }

public:
    // Same-process UI bridge access (TapeEchoAccess.hpp).
    float getRecordVuLevelForUI() const noexcept { return dsp.getRecordVuLevel(); }
    float getRecordPeakLevelForUI() const noexcept { return dsp.getRecordPeakLevel(); }
    float getHead1DelayMsForUI() const noexcept
    {
        return effectiveHead1DelayMs.load(std::memory_order_relaxed);
    }

protected:
    //--- metadata --------------------------------------------------------------
    const char* getLabel() const override       { return "TapeEcho"; }
    const char* getDescription() const override
    {
        return "Component-modeled vintage tape echo: "
               "3-head tape delay with spring reverb, tape saturation and wow and flutter.";
    }
    const char* getMaker() const override       { return "Dusk Audio"; }
    const char* getHomePage() const override    { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override     { return "GPL-3.0-or-later"; }
    // Version comes from the CMake project() VERSION via TapeEchoVersion.hpp.
    uint32_t    getVersion() const override
    { return d_version(TE2_VERSION_MAJOR, TE2_VERSION_MINOR, TE2_VERSION_PATCH); }
    int64_t     getUniqueId() const override    { return d_cconst('D', 's', 'T', 'E'); } // must match DISTRHO_PLUGIN_UNIQUE_ID (DsTE)

    //--- parameters ------------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        switch (index)
        {
        case kParamMode:
            p.hints |= kParameterIsInteger;
            p.name = "Mode";        p.symbol = "mode";
            p.ranges.def = 1.0f;    p.ranges.min = 1.0f;  p.ranges.max = 12.0f;
            break;
        case kParamRepeatRate:
            p.name = "Repeat Rate"; p.symbol = "repeat_rate";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamIntensity:
            p.name = "Intensity";   p.symbol = "intensity";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamEchoLevel:
            p.name = "Echo Volume"; p.symbol = "echo_volume";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamReverbLevel:
            p.name = "Reverb Volume"; p.symbol = "reverb_volume";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamBass:
            p.name = "Bass";        p.symbol = "bass";
            p.ranges.def = 0.0f;    p.ranges.min = -1.0f; p.ranges.max = 1.0f;
            break;
        case kParamTreble:
            p.name = "Treble";      p.symbol = "treble";
            p.ranges.def = 0.0f;    p.ranges.min = -1.0f; p.ranges.max = 1.0f;
            break;
        case kParamInputGain:
            p.name = "Input Volume"; p.symbol = "input_volume";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamWowFlutter:
            p.name = "Wow & Flutter"; p.symbol = "wow_flutter";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamDryLevel:
            // Retained at its shipped ID for old projects and automation. The
            // custom UI now exposes the appended Mix control instead.
            p.hints |= kParameterIsHidden;
            p.name = "Dry Level";   p.symbol = "dry_level";
            p.ranges.def = 1.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamTempoSync:
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.name = "Tempo Sync";  p.symbol = "tempo_sync";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamSyncDivision:
            p.hints |= kParameterIsInteger;
            p.name = "Sync Division"; p.symbol = "sync_division";
            p.ranges.def = 2.0f;    p.ranges.min = 0.0f;
            p.ranges.max = (float)(kNumSyncDivisions - 1);
            break;
        case kParamTapeAge:
            p.name = "Tape Age";    p.symbol = "tape_age";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            p.enumValues.count = 3;
            p.enumValues.restrictedMode = true;
            {
                auto* const e = new ParameterEnumerationValue[3];
                e[0] = ParameterEnumerationValue(0.0f, "New");
                e[1] = ParameterEnumerationValue(0.5f, "Used");
                e[2] = ParameterEnumerationValue(1.0f, "Old");
                p.enumValues.values = e;
            }
            break;
        case kParamOutputVolume:
            p.name = "Output Volume"; p.symbol = "output_volume";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamEchoPan:
            p.name = "Echo Pan";    p.symbol = "echo_pan";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamReverbPan:
            p.name = "Reverb Pan";  p.symbol = "reverb_pan";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamInputSend:
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.name = "Input Send";  p.symbol = "input_send";
            p.ranges.def = 1.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamWetSolo:
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            // Legacy one-click dry mute. Keep its exact parameter identity and
            // behavior so saved automation still works, but do not present it
            // as a second wet-routing control beside Mix.
            p.hints |= kParameterIsHidden;
            p.name = "Wet Solo";    p.symbol = "wet_solo";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        case kParamBypass:
            // host-integrated bypass; shown as the POWER switch in our UI
            p.initDesignation(kParameterDesignationBypass);
            break;
        case kParamOutLevel:
            p.hints = kParameterIsAutomatable | kParameterIsOutput;
            p.name = "Record VU";   p.symbol = "out_level";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 3.0f;
            break;
        case kParamPeakLevel:
            p.hints = kParameterIsAutomatable | kParameterIsOutput;
            p.name = "Record Peak"; p.symbol = "peak_level";
            p.ranges.def = 0.0f;    p.ranges.min = 0.0f;  p.ranges.max = 3.0f;
            break;
        case kParamMix:
            p.name = "Mix";         p.symbol = "mix";
            p.ranges.def = 0.5f;    p.ranges.min = 0.0f;  p.ranges.max = 1.0f;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == kParamOutLevel)
            return dsp.getRecordVuLevel();
        if (index == kParamPeakLevel)
            return dsp.getRecordPeakLevel();
        return index < kParamCount ? values[index].load(std::memory_order_relaxed) : 0.0f;
    }

    // DSP setters are atomic stores — safe from whichever thread DPF uses.
    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamCount
            || index == kParamOutLevel
            || index == kParamPeakLevel
            || !std::isfinite(value))
            return;
        value = std::max(kTeParams[index].min,
                         std::min(kTeParams[index].max, value));
        if (index == kParamMode || index == kParamSyncDivision)
            value = std::round(value);
        else if (index == kParamBypass
                 || index == kParamTempoSync
                 || index == kParamInputSend
                 || index == kParamWetSolo)
            value = value >= 0.5f ? 1.0f : 0.0f;
        if (index == kParamTapeAge)
            value = teQuantizeTapeAge(value);
        values[index].store(value, std::memory_order_relaxed);
        switch (index)
        {
        case kParamBypass:      dsp.setBypass(value > 0.5f);      break;
        case kParamTempoSync:
            if (value < 0.5f) // sync released: hand control back to the knob
                dsp.setRepeatRate(values[kParamRepeatRate].load(std::memory_order_relaxed));
            break;
        case kParamSyncDivision: break; // applied per-block in run()
        case kParamTapeAge:     dsp.setTapeAge(value);            break;
        case kParamOutputVolume:dsp.setOutputVolume(value);        break;
        case kParamEchoPan:     dsp.setEchoPan(value);             break;
        case kParamReverbPan:   dsp.setReverbPan(value);           break;
        case kParamInputSend:   dsp.setInputSend(value >= 0.5f);   break;
        case kParamWetSolo:     dsp.setWetSolo(value >= 0.5f);     break;
        case kParamMode:        dsp.setMode((int)(value + 0.5f)); break;
        case kParamRepeatRate:
            if (values[kParamTempoSync].load(std::memory_order_relaxed) < 0.5f)
                dsp.setRepeatRate(value);
            break;
        case kParamIntensity:   dsp.setIntensity(value);          break;
        case kParamEchoLevel:   dsp.setEchoLevel(value);          break;
        case kParamReverbLevel: dsp.setReverbLevel(value);        break;
        case kParamBass:        dsp.setBass(value);               break;
        case kParamTreble:      dsp.setTreble(value);             break;
        case kParamInputGain:   dsp.setInputGain(value);          break;
        case kParamWowFlutter:  dsp.setWowFlutter(value);         break;
        case kParamDryLevel:    dsp.setDryLevel(value);           break;
        case kParamMix:         dsp.setMix(value);                break;
        }
    }

    //--- programs ----------------------------------------------------------------
    void initProgramName(uint32_t index, String& programName) override
    {
        if (index < (uint32_t)kNumFactoryPresets)
            programName = kFactoryPresets[index].name;
    }

    void loadProgram(uint32_t index) override
    {
        if (index >= (uint32_t)kNumFactoryPresets)
            return;
        const TapeEchoPreset& preset = kFactoryPresets[index];
        for (uint32_t i = 0; i <= kParamTapeAge; ++i)
            setParameterValue(i, preset.v[i]);
        setParameterValue(kParamOutputVolume, preset.outputVolume);
        setParameterValue(kParamEchoPan, preset.echoPan);
        setParameterValue(kParamReverbPan, preset.reverbPan);
        setParameterValue(kParamInputSend, preset.inputSend);
        setParameterValue(kParamWetSolo, preset.wetSolo);
        setParameterValue(kParamMix, preset.mix);
        // BYPASS is not a preset parameter: a program change must never override
        // the host-designated bypass the player set (see teIsPresetParam).
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

    //--- audio -------------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        float effectiveMs = duskaudio::TapeEchoDSP::delayMsForRepeatRate(
            values[kParamRepeatRate].load(std::memory_order_relaxed));
        if (values[kParamTempoSync].load(std::memory_order_relaxed) > 0.5f)
        {
            const TimePosition& tp = getTimePosition();
            if (tp.bbt.valid && tp.bbt.beatsPerMinute > 20.0)
                lastBpm = tp.bbt.beatsPerMinute;

            const int mode = (int)(
                values[kParamMode].load(std::memory_order_relaxed) + 0.5f);
            // The selected note belongs to the first active playback head.
            // Convert it back to the equivalent head-1 motor time before
            // clamping to the physical transport range.
            const double leadingHeadRatio =
                duskaudio::TapeEchoDSP::leadingHeadRatioForMode(mode);
            const double requestedMs = syncDelayMs(lastBpm,
                (int)(values[kParamSyncDivision].load(std::memory_order_relaxed) + 0.5f));
            const double requestedHead1Ms = requestedMs / leadingHeadRatio;

            // CLAMP, do not octave-fold. This is measured reference behaviour,
            // not a limitation: the hardware pins a too-long note at the motor
            // maximum instead of transposing it down an octave. Hosted captures
            // show it directly -- "Basic Guitar Delay" reads 487.6 ms at both
            // 120 and 80 BPM (pinned, not proportional), "Vocal Bounce Delay"
            // reads 190.1 ms at 80/120/160 alike, and the pinned head-1 value
            // lands on 178.1 ms against our kMaxDelayMs of 178.50. The longer
            // readings are that same clamp scaled by the head ratios
            // (178.50 x 1.90 = 339, x 2.75 = 491). An octave fold was tried and
            // measured: it breaks 5 tempo-sync gates because it transposes notes
            // the reference simply truncates. Divisions past the range therefore
            // DO collide at the endpoint on purpose -- that is what the hardware
            // does. See regression_gate.py's per-preset tempo deltas.
            const double kMinMs = (double)duskaudio::TapeEchoDSP::kMinDelayMs;
            const double kMaxMs = (double)duskaudio::TapeEchoDSP::kMaxDelayMs;
            const double clampedMs =
                std::max(kMinMs, std::min(requestedHead1Ms, kMaxMs));

            // Convert the clamped delay to the motor-speed knob's 0..1 range;
            // the DSP's inertia smoother turns tempo changes into tape glides.
            dsp.setRepeatRate(
                duskaudio::TapeEchoDSP::repeatRateForDelayMs((float)clampedMs));
            effectiveMs = (float)clampedMs;
        }
        effectiveHead1DelayMs.store(effectiveMs, std::memory_order_relaxed);

        dsp.processBlock(inputs, outputs, DISTRHO_PLUGIN_NUM_INPUTS, (int)frames);
    }

private:
    void pushAllParams()
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (i != kParamOutLevel && i != kParamPeakLevel)
                setParameterValue(i, values[i].load(std::memory_order_relaxed));
    }

    duskaudio::TapeEchoDSP dsp;
    double lastBpm = 120.0;
    std::atomic<float> effectiveHead1DelayMs {
        duskaudio::TapeEchoDSP::kMaxDelayMs
    };
    // Parameter cache shared across threads: run() reads it on the audio thread
    // while setParameterValue()/loadProgram() write it from the host thread.
    // Atomic (relaxed) storage removes the data race — same pattern as the DSP
    // core's parameter atomics. The ctor stores below run before any concurrency.
    std::atomic<float> values[kParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoPlugin)
};

Plugin* createPlugin()
{
    return new TapeEchoPlugin();
}

END_NAMESPACE_DISTRHO

// same-process UI accessor (see TapeEchoAccess.hpp)
float tapeEchoGetRecordVuLevel(void* const pluginInstancePointer) noexcept
{
    auto* const plugin = static_cast<DISTRHO_NAMESPACE::TapeEchoPlugin*>(pluginInstancePointer);
    return plugin != nullptr ? plugin->getRecordVuLevelForUI() : 0.0f;
}

float tapeEchoGetRecordPeakLevel(void* const pluginInstancePointer) noexcept
{
    auto* const plugin = static_cast<DISTRHO_NAMESPACE::TapeEchoPlugin*>(pluginInstancePointer);
    return plugin != nullptr ? plugin->getRecordPeakLevelForUI() : 0.0f;
}

float tapeEchoGetHead1DelayMs(void* const pluginInstancePointer) noexcept
{
    auto* const plugin = static_cast<DISTRHO_NAMESPACE::TapeEchoPlugin*>(pluginInstancePointer);
    return plugin != nullptr ? plugin->getHead1DelayMsForUI() : 0.0f;
}
