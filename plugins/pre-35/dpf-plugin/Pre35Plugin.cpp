// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// Pre35Plugin.cpp — DPF shell around the framework-free pre35::Pre35DSP core.
//
// The shell owns exactly four things the core deliberately does not:
//   1. one core instance PER CHANNEL, with decorrelated noise seeds;
//   2. host bypass (the core has no bypass and should not grow one);
//   3. latency reporting, including clearing it while bypassed;
//   4. output metering for the UI.
// Everything else is a straight pass-through to the core's atomic setters, which
// are already smoothed internally at its own control rate — there is no second
// smoother here, and there must not be one: it would fight the core's ramps.

#include "DistrhoPlugin.hpp"

#include "DuskDenormals.hpp"
#include "Pre35Access.hpp"
#include "Pre35DSP.hpp"
#include "Pre35Params.hpp"
#include "Pre35Version.hpp"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr int kNumChannels = DISTRHO_PLUGIN_NUM_INPUTS;
    static_assert(kNumChannels == DISTRHO_PLUGIN_NUM_OUTPUTS,
                  "PRE-35 processes in place, one core per channel: the port counts must match");

    /** Meter release, seconds. Matches the fleet's ~300 ms peak fallback. */
    constexpr float kMeterReleaseSeconds = 0.3f;

    /** Base noise seed ('PRE3'); channel n gets base + n so a stereo pair is
        decorrelated rather than two copies of the same hiss. Only meaningful
        before prepare(), so it is set once in the constructor. */
    constexpr uint64_t kNoiseSeedBase = 0x50524533ull;

    /** setState() landed something it could not parse as a version number. Distinct
        from every version we will ever ship, so a future migration can tell "no
        readable tag" from "tag says 1". */
    constexpr int kUnknownStateVersion = -1;
}

class Pre35Plugin : public Plugin
{
public:
    Pre35Plugin()
        : Plugin(kParamCount, 0, 1)   // params, programs (none), states ("stateVersion")
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i].store(kParamDefaults[i], std::memory_order_relaxed);

        for (int ch = 0; ch < kNumChannels; ++ch)
            dsp[ch].setNoiseSeed(kNoiseSeedBase + (uint64_t)ch);
    }

    //--- same-process accessors for the UI bridge -----------------------------
    float outPeakL() const noexcept { return meterPeak[0].load(std::memory_order_relaxed); }
    float outPeakR() const noexcept
    {
        return meterPeak[kNumChannels > 1 ? 1 : 0].load(std::memory_order_relaxed);
    }

protected:
    //--- metadata -------------------------------------------------------------
    const char* getLabel() const override { return "PRE-35"; }
    const char* getDescription() const override
    {
        return "Measured model of a Tascam M-35 mixer channel's mic preamp: input "
               "transformer saturation, the calibrated trim taper and pad, the amp's "
               "own frequency response and its input-referred noise floor.";
    }
    const char* getMaker() const override    { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override  { return "GPL-3.0-or-later"; }
    uint32_t    getVersion() const override
    {
        return d_version(PRE35_VERSION_MAJOR, PRE35_VERSION_MINOR, PRE35_VERSION_PATCH);
    }
    int64_t getUniqueId() const override { return d_cconst('D', 's', 'P', '3'); } // DsP3

    //--- parameters -----------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        auto rng = [&p](float def, float min, float max)
        { p.ranges.def = def; p.ranges.min = min; p.ranges.max = max; };
        auto boolean = [&p]() { p.hints |= kParameterIsBoolean | kParameterIsInteger; };

        switch (index)
        {
        case kPad:
            // Integer + restricted enumeration, never a raw float bool/choice: a
            // continuous "choice" flakes host round-trip tests and lets a host land
            // between switch positions.
            p.hints |= kParameterIsInteger;
            p.name = "Pad"; p.symbol = "pad"; p.unit = "dB";
            rng(kParamDefaults[kPad], kParamMin[kPad], kParamMax[kPad]);
            p.enumValues.count = (uint8_t)kNumPads;
            p.enumValues.restrictedMode = true;
            {
                auto* e = new ParameterEnumerationValue[kNumPads];
                for (int i = 0; i < kNumPads; ++i)
                    e[i] = ParameterEnumerationValue((float)i, kPadLabels[i]);
                p.enumValues.values = e;   // DPF takes ownership (deleteLater defaults true)
            }
            break;
        case kTrim:
            p.name = "Trim"; p.symbol = "trim"; p.unit = "%";
            rng(kParamDefaults[kTrim], kParamMin[kTrim], kParamMax[kTrim]);
            break;
        case kIron:
            p.name = "Iron"; p.symbol = "iron"; p.unit = "%";
            rng(kParamDefaults[kIron], kParamMin[kIron], kParamMax[kIron]);
            break;
        case kNoise:
            boolean(); p.name = "Noise"; p.symbol = "noise";
            rng(kParamDefaults[kNoise], kParamMin[kNoise], kParamMax[kNoise]);
            break;
        case kAutoGain:
            boolean(); p.name = "Auto Gain"; p.symbol = "auto_gain";
            rng(kParamDefaults[kAutoGain], kParamMin[kAutoGain], kParamMax[kAutoGain]);
            break;
        case kOutput:
            p.name = "Output"; p.symbol = "output"; p.unit = "dB";
            rng(kParamDefaults[kOutput], kParamMin[kOutput], kParamMax[kOutput]);
            break;
        case kBypass:
            p.initDesignation(kParameterDesignationBypass);
            break;
        case kOutPeakL:
            p.hints = kParameterIsAutomatable | kParameterIsOutput;
            p.name = "Out Peak L"; p.symbol = "out_peak_l"; rng(0, 0, 2);
            break;
        case kOutPeakR:
            p.hints = kParameterIsAutomatable | kParameterIsOutput;
            p.name = "Out Peak R"; p.symbol = "out_peak_r"; rng(0, 0, 2);
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kOutPeakL: return outPeakL();
        case kOutPeakR: return outPeakR();
        default:        return index < kParamCount
                             ? values[index].load(std::memory_order_relaxed) : 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kNumInputParams)   // output params are not settable
            return;
        if (! std::isfinite(value))
            return;

        // The mirror is atomic for the same reason the core's own targets are: DPF
        // calls this from whatever thread the host's automation arrives on, while
        // getParameterValue() and pushAllParams() read it from others. Relaxed is
        // enough — each element stands alone, nothing is published through them.
        values[index].store(value, std::memory_order_relaxed);

        // Every setter below is a relaxed atomic store in the core and is safe from
        // any thread; the core snapshots them once per internal control chunk.
        switch (index)
        {
        case kPad:
            for (auto& d : dsp) d.setPadIndex((int)std::lround(value));
            break;
        case kTrim:
            for (auto& d : dsp) d.setTrimPercent((double)value);
            break;
        case kIron:
            for (auto& d : dsp) d.setIronAmount((double)value * 0.01);  // % -> 0..2
            break;
        case kNoise:
            for (auto& d : dsp) d.setNoiseEnabled(value > 0.5f);
            break;
        case kAutoGain:
            for (auto& d : dsp) d.setAutoGain(value > 0.5f);
            break;
        case kOutput:
            for (auto& d : dsp) d.setOutputGainDb((double)value);
            break;
        case kBypass:
            // Consumed in run(): the audio path and the reported latency both have
            // to change on the same block boundary, and setLatency() may only be
            // called from the constructor, activate() or run() (DPF contract).
            // Kept as its own bool rather than re-deriving `> 0.5f` from the mirror
            // on every block: the audio thread wants the decision, not the float.
            bypassFlag.store(value > 0.5f, std::memory_order_relaxed);
            break;
        }
    }

    //--- state ----------------------------------------------------------------
    // The parameters ARE the state; the host saves and restores them itself. The
    // single key below only stamps a format version onto the save so a future
    // change has something to migrate from.
    void initState(uint32_t index, State& state) override
    {
        if (index != 0)
            return;
        state.key          = kStateVersionKey;
        state.label        = "State Version";
        state.defaultValue = kStateVersionValue;
        state.hints        = kStateIsHostReadable;
    }

    String getState(const char* key) const override
    {
        if (key != nullptr && std::strcmp(key, kStateVersionKey) == 0)
            return String(kStateVersionValue);
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (key == nullptr || std::strcmp(key, kStateVersionKey) != 0)
            return;

        // THE MIGRATION HOOK. Version 1 is the only format that exists, so there is
        // nothing to migrate yet and nothing here changes behaviour — but when there
        // is, this is the value it will branch on, so it has to be able to say "I do
        // not know what this save is" out loud.
        //
        // std::atoi cannot: it returns 0 for "", for "banana" and for a genuine "0"
        // alike, so a corrupt or future-format tag would be indistinguishable from an
        // old one and would silently take the oldest migration path. strtol with the
        // end pointer separates them, and anything unparseable lands on
        // kUnknownStateVersion instead of impersonating a version we ship.
        loadedStateVersion = kUnknownStateVersion;
        if (value == nullptr || value[0] == '\0')
            return;

        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(value, &end, 10);
        // Whole string consumed (trailing blanks tolerated), in range, non-negative.
        while (end != nullptr && (*end == ' ' || *end == '\t'))
            ++end;
        if (end == value || end == nullptr || *end != '\0' || errno == ERANGE
            || parsed < 0 || parsed > INT_MAX)
            return;

        loadedStateVersion = (int)parsed;
    }

    //--- lifecycle ------------------------------------------------------------
    void activate() override
    {
        prepareCores(getSampleRate());
        // The host restored parameters before activate(); push them into freshly
        // prepared cores, then reset so the smoothers are snapped to those targets
        // and the first sample out is steady state rather than a 20 ms ramp.
        pushAllParams();
        for (auto& d : dsp) d.reset();
        bypassResetPending = false;
        updateLatency(bypassFlag.load(std::memory_order_relaxed));
    }

    void deactivate() override
    {
        for (auto& d : dsp) d.reset();
        clearMeters();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        // The argument, not getSampleRate(): DPF happens to store the new rate
        // before calling this, but depending on that ordering buys nothing and
        // would break silently if it ever changed.
        prepareCores(newSampleRate);
        pushAllParams();
        for (auto& d : dsp) d.reset();
        // No updateLatency() here: setLatency() is only valid in the constructor,
        // activate() and run() (DPF contract), and this can fire while deactivated.
        // It matters that it is refreshed and not assumed constant — the core's
        // oversampling factor is rate-dependent (8x at 44.1/48 kHz down to 1x above
        // ~344 kHz, where the latency becomes 0) — and activate()/run() do exactly
        // that on the way back in.
    }

    // The core sizes nothing off the block length (every buffer is a fixed-size
    // member and it chunks internally), so a host buffer-size change needs no
    // reconfiguration at all. Deliberately not overriding bufferSizeChanged().

    //--- audio ----------------------------------------------------------------
    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const duskaudio::ScopedFlushDenormals noDenormals;

        const bool bypassed = bypassFlag.load(std::memory_order_relaxed);

        // FIRST, before a single sample is produced. sampleRateChanged() can have
        // re-prepared the cores at a different oversampling factor, and a bypass
        // toggle changes the number too; reporting after the audio would render
        // this block at one latency while the host still compensates for another.
        // Placed ahead of the frames == 0 early-out as well, so a host that opens
        // with empty blocks still learns the value before real audio starts.
        updateLatency(bypassed);

        if (frames == 0)   // some hosts send empty blocks
            return;

        if (bypassed)
        {
            for (int ch = 0; ch < kNumChannels; ++ch)
                if (outputs[ch] != inputs[ch])
                    std::memcpy(outputs[ch], inputs[ch], sizeof(float) * frames);

            // Latency is cleared while bypassed, so the dry path is NOT delayed to
            // match; the host's own compensation follows the reported number. The
            // cores go stale meanwhile — resync them on the way back out.
            bypassResetPending = true;
            clearMeters();
            return;
        }

        if (bypassResetPending)
        {
            // Allocation-free and lock-free (the core calls it on its own recovery
            // path inside process()), so this is safe here: it clears the filter,
            // detector and resampler state left over from before the bypass and
            // snaps the smoothers, instead of ramping out of a stale tail.
            for (auto& d : dsp) d.reset();
            bypassResetPending = false;
        }

        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            float* out = outputs[ch];
            if (out != inputs[ch])
                std::memcpy(out, inputs[ch], sizeof(float) * frames);
            dsp[ch].process(out, (int)frames);   // in place, mono, host rate
            updateMeter(ch, out, frames);
        }
    }

private:
    void prepareCores(double rate)
    {
        sampleRate = rate > 0.0 ? rate : 48000.0;
        for (auto& d : dsp)
            d.prepare(sampleRate);
        // One block of release, precomputed: run() must not call exp().
        const double rel = sampleRate > 0.0 ? sampleRate * (double)kMeterReleaseSeconds : 1.0;
        meterRelease = (float)std::exp(-1.0 / (rel > 1.0 ? rel : 1.0));
    }

    void pushAllParams()
    {
        for (uint32_t i = 0; i < kNumInputParams; ++i)
            setParameterValue(i, values[i].load(std::memory_order_relaxed));
    }

    void clearMeters()
    {
        for (auto& m : meterPeak)
            m.store(0.0f, std::memory_order_relaxed);
    }

    void updateMeter(int ch, const float* buf, uint32_t frames)
    {
        float peak = 0.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float a = std::fabs(buf[i]);
            if (a > peak)
                peak = a;
        }
        if (! std::isfinite(peak))
            peak = 0.0f;

        float held = meterPeak[ch].load(std::memory_order_relaxed);
        // Per-block release: one pow() per channel per block, never a per-sample
        // exp(). Instant attack, so a transient is never under-read.
        held *= std::pow(meterRelease, (float)frames);
        meterPeak[ch].store(peak > held ? peak : held, std::memory_order_relaxed);
    }

    /** Bypassed reports 0 (CLAUDE.md: latency cleared on bypass, restored on
        un-bypass); otherwise whatever the core's resampler stages actually cost —
        20 host samples whenever it oversamples, 0 when the host rate is already
        above the internal target and it does not. Queried, never hardcoded. */
    void updateLatency(bool bypassed)
    {
        const uint32_t lat = bypassed ? 0u : (uint32_t)dsp[0].latencySamples();
        if (lat != lastLatency)
        {
            setLatency(lat);
            lastLatency = lat;
        }
    }

    pre35::Pre35DSP dsp[kNumChannels];
    std::atomic<float> meterPeak[kNumChannels] { };
    std::atomic<bool>  bypassFlag { false };
    // Host-visible parameter mirror. Atomic because DPF gives no thread guarantee
    // for setParameterValue()/getParameterValue() — see setParameterValue().
    std::atomic<float> values[kParamCount] { };
    double   sampleRate  = 48000.0;
    float    meterRelease = 0.0f;
    uint32_t lastLatency = 0xffffffffu;
    int      loadedStateVersion = 1;
    bool     bypassResetPending = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pre35Plugin)
};

Plugin* createPlugin() { return new Pre35Plugin(); }

END_NAMESPACE_DISTRHO

//--- same-process UI accessors (see Pre35Access.hpp) --------------------------
using DISTRHO_NAMESPACE::Pre35Plugin;
static Pre35Plugin* asPlugin(void* p) { return static_cast<Pre35Plugin*>(p); }

float pre35GetOutPeakL(void* p) noexcept { return p ? asPlugin(p)->outPeakL() : 0.0f; }
float pre35GetOutPeakR(void* p) noexcept { return p ? asPlugin(p)->outPeakR() : 0.0f; }
