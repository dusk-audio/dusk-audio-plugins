// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiSynthPlugin.cpp — thin DPF shell around the framework-free MultiSynthDSP
// core. Product name: Sunset Circuits (internal class/namespace names stay).
// Owns the parameter table (generated from MultiSynthParams.hpp), MIDI
// event routing (split-block rendering at each event offset), transport tempo
// forwarding, and the factory programs (kNumFactoryPresets). All DSP in the core.

#include "DistrhoPlugin.hpp"
#include "MultiSynthAccess.hpp"
#include "MultiSynthDSP.hpp"
#include "MultiSynthParams.hpp"

#include <atomic>

// Version is injected by CMake (SC_VERSION_* compile defs from project(VERSION),
// which is single-sourced through get_plugin_version). These fallbacks only apply
// to an ad-hoc compile with no build definitions and keep getVersion() valid.
#ifndef SC_VERSION_MAJOR
 #define SC_VERSION_MAJOR 1
#endif
#ifndef SC_VERSION_MINOR
 #define SC_VERSION_MINOR 0
#endif
#ifndef SC_VERSION_PATCH
 #define SC_VERSION_PATCH 0
#endif

START_NAMESPACE_DISTRHO

// The DPF parameter index must equal the core msynth::Param index 1:1, so
// forwarding is a single dsp.setParameter(index, value). Spot-check the anchors
// of every contiguous block; a drift here fails the build, not silently at run.
static_assert((int)kParamMode        == (int)msynth::pMode,        "param order drift");
static_assert((int)kParamArpStep0    == (int)msynth::pArpStep0,    "arp step drift");
static_assert((int)kParamModSrc0     == (int)msynth::pModSrc0,     "mod matrix drift");
static_assert((int)kParamPrismAlgo   == (int)msynth::pPrismAlgo,   "prism block drift");
static_assert((int)kParamOp1Ratio    == (int)msynth::pOp1Ratio,    "op block drift");
static_assert((int)kParamOp4R        == (int)msynth::pOp4R,        "op block end drift");
static_assert((int)kParamSeqPitch0   == (int)msynth::pSeqPitch0,   "seq pitch drift");
static_assert((int)kParamSeqSlide0   == (int)msynth::pSeqSlide0,   "seq slide drift");
static_assert((int)kNumCoreParams    == (int)msynth::kNumParams,   "core param count drift");

class MultiSynthPlugin : public Plugin
{
public:
    MultiSynthPlugin()
        : Plugin(kParamCount, kNumFactoryPresets, 0)
    {
        for (uint32_t i = 0; i < kNumCoreParams; ++i)
        {
            const float d = kParamDefs[i].def;
            values[i].store(d, std::memory_order_relaxed);
            dsp.setParameter((int)i, d);
        }
        // Report the default-state latency from construction so a host that
        // queries getLatency() before activate() (e.g. at plugin scan/load)
        // gets the correct 2x value (12 samples) rather than 0 — otherwise PDC
        // is misaligned until the first activate(). (E1)
        updateLatency();
    }

    // Same-process accessors for the Phase-4 UI bridge (MultiSynthAccess.hpp).
    float getOutLevelLForUI() const noexcept { return dsp.getOutputLevelL(); }
    float getOutLevelRForUI() const noexcept { return dsp.getOutputLevelR(); }
    int   getArpStepForUI()   const noexcept { return dsp.getArpStep(); }
    msynth::MultiSynthDSP* getDSPForUI() noexcept { return &dsp; }
    // Packed (sequence << 8 | program) for a MIDI program change the plugin applied
    // to itself; 0 = none yet. See loadProgramFromMidi.
    uint32_t getMidiProgramSignalForUI() const noexcept
    { return midiProgramSignal.load(std::memory_order_acquire); }

protected:
    //--- metadata --------------------------------------------------------------
    const char* getLabel() const override    { return "SunsetCircuits"; }
    const char* getDescription() const override
    {
        return "Sunset Circuits: six vintage synthesizers in one instrument. "
               "DCO poly, American poly with poly-mod, aggressive mono, "
               "semi-modular, 4-op FM, and an acid bass box with a 16-step "
               "pattern sequencer.";
    }
    const char* getMaker() const override    { return "Dusk Audio"; }
    const char* getHomePage() const override { return "https://dusk-audio.github.io/"; }
    const char* getLicense() const override  { return "GPL-3.0-or-later"; }
    uint32_t    getVersion() const override  { return d_version(SC_VERSION_MAJOR, SC_VERSION_MINOR, SC_VERSION_PATCH); }
    int64_t     getUniqueId() const override { return d_cconst('D', 's', 'S', 'C'); } // DsSC — matches DISTRHO_PLUGIN_UNIQUE_ID

    //--- parameters ------------------------------------------------------------
    void initParameter(uint32_t index, Parameter& p) override
    {
        if (index < kNumCoreParams)
        {
            const ParamDef& d = kParamDefs[index];
            p.hints = kParameterIsAutomatable;
            switch (d.kind)
            {
            case PK_LOG:  p.hints |= kParameterIsLogarithmic; break;
            case PK_INT:  p.hints |= kParameterIsInteger; break;
            case PK_BOOL: p.hints |= kParameterIsBoolean | kParameterIsInteger; break;
            default: break;
            }
            p.name   = d.name;
            p.symbol = d.symbol;
            p.ranges.def = d.def;
            p.ranges.min = d.min;
            p.ranges.max = d.max;
            return;
        }
        // Output params: peak meters (fallback path; real path = access bridge).
        // Output-only: not automatable (host writes to output params are discarded).
        p.hints  = kParameterIsOutput;
        p.ranges.min = -60.0f;
        p.ranges.max = 6.0f;
        p.ranges.def = -60.0f;
        if (index == kParamOutLevelL) { p.name = "Out Level L"; p.symbol = "outLevelL"; }
        else                          { p.name = "Out Level R"; p.symbol = "outLevelR"; }
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index == kParamOutLevelL) return dsp.getOutputLevelL();
        if (index == kParamOutLevelR) return dsp.getOutputLevelR();
        return index < kNumCoreParams ? values[index].load(std::memory_order_relaxed) : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kNumCoreParams) return; // output params are not settable
        values[index].store(value, std::memory_order_relaxed);
        dsp.setParameter((int)index, value);
        if (index == kParamOversampling) updateLatency(); // E1
    }

    //--- programs (factory presets; count = kNumFactoryPresets) -----------------
    void initProgramName(uint32_t index, String& programName) override
    {
        if (index < (uint32_t)kNumFactoryPresets)
            programName = kFactoryPresets[index].name;
    }

    void loadProgram(uint32_t index) override
    {
        if (index >= (uint32_t)kNumFactoryPresets) return;
        // Reset every param to its default, apply the shared baseline, then the
        // preset's overrides (mirrors JUCE applyFactoryPreset but deterministic:
        // no dependence on the previously-loaded program's state).
        for (uint32_t i = 0; i < kNumCoreParams; ++i)
            setParameterValue(i, kParamDefs[i].def);
        for (int r = 0; r < kBaselineRows; ++r)
            setParameterValue((uint32_t)kPresetBaseline[r].index, kPresetBaseline[r].value);
        const FactoryPreset& pr = kFactoryPresets[index];
        for (int r = 0; r < pr.nRows; ++r)
            setParameterValue((uint32_t)pr.rows[r].index, pr.rows[r].value);
        // Land the smoothed controls on the new patch instead of gliding to it.
        // Must come after the writes above so the snapshot that consumes the flag
        // already sees the whole preset.
        dsp.notifyProgramChange();
        updateLatency(); // a preset may change oversampling (E1)
    }

    //--- lifecycle -------------------------------------------------------------
    void activate() override
    {
        dsp.prepare(getSampleRate(), (int)getBufferSize());
        pushAllParams();
        updateLatency(); // E1
    }

    void deactivate() override { dsp.reset(); }

    void sampleRateChanged(double newSampleRate) override
    {
        dsp.prepare(newSampleRate, (int)getBufferSize());
        pushAllParams();
    }

    //--- audio -----------------------------------------------------------------
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 20.0)
            lastBpm = tp.bbt.beatsPerMinute;
        dsp.setTempo(lastBpm, tp.playing);

        // Host phase-lock: song position in beats at frame 0 of this buffer.
        // Prefer BBT (bar/beat/tick); fall back to the transport frame counter.
        // The DSP only locks when the transport is playing (see processBlock).
        double songBeat = 0.0;
        bool   songValid = false;
        if (tp.bbt.valid && tp.bbt.ticksPerBeat > 0.0)
        {
            // BBT bar/beat/tick are in meter-denominator units (bbt.beatType is
            // the time-signature denominator; beat runs 1..beatsPerBar), while the
            // core's getBeatsPerStep speaks quarter-notes. Convert denominator
            // beats to quarter-note beats so 6/8, 3/4 etc. sync correctly (S1).
            songBeat = (double)(tp.bbt.bar - 1) * (double)tp.bbt.beatsPerBar
                     + (double)(tp.bbt.beat - 1)
                     + tp.bbt.tick / tp.bbt.ticksPerBeat;
            if (tp.bbt.beatType > 0.0f) songBeat *= 4.0 / (double)tp.bbt.beatType;
            songValid = true;
        }
        else
        {
            // No BBT, OR BBT with no sub-beat tick resolution (ticksPerBeat <= 0).
            // In the latter case BBT is block-constant within a beat, so songBeat
            // would jump backward at each buffer start and machine-gun the arp/
            // acid backward-jump (loop-wrap) detector. The transport frame counter
            // is monotonic, so derive songBeat from it instead (S2).
            songBeat = (double)tp.frame / getSampleRate() * lastBpm / 60.0;
            songValid = true;
        }
        // lastBpm comes from bbt.beatsPerMinute, which the DPF header states only
        // as "number of beats per minute" with no meter qualifier; it is the
        // musical (quarter-note) BPM by universal DAW convention, matching the
        // quarter-note songBeat above, so it is used as-is (no beatType scaling).
        const double beatsPerFrame = lastBpm / 60.0 / getSampleRate();

        float* const outL = outputs[0];
        float* const outR = outputs[1];

        uint32_t frameOffset = 0;
        for (uint32_t e = 0; e < midiEventCount; ++e)
        {
            const MidiEvent& ev = midiEvents[e];
            const uint32_t evFrame = ev.frame < frames ? ev.frame : frames;
            if (evFrame > frameOffset)
            {
                // Set the song position for THIS segment's start frame so the grid
                // stays continuous across MIDI-split sub-blocks (no false wrap).
                dsp.setSongPosition(songBeat + (double)frameOffset * beatsPerFrame, songValid);
                dsp.processBlock(outL + frameOffset, outR + frameOffset,
                                 (int)(evFrame - frameOffset));
                frameOffset = evFrame;
            }
            handleMidi(ev);
        }
        if (frameOffset < frames)
        {
            dsp.setSongPosition(songBeat + (double)frameOffset * beatsPerFrame, songValid);
            dsp.processBlock(outL + frameOffset, outR + frameOffset,
                             (int)(frames - frameOffset));
        }
    }

private:
    void pushAllParams()
    {
        for (uint32_t i = 0; i < kNumCoreParams; ++i)
            dsp.setParameter((int)i, values[i].load(std::memory_order_relaxed));
    }

    // E1: report the oversampling group delay to the host so it can compensate.
    // The oversampling param is 0=1x, 1=2x, 2=4x. The added latency (in host
    // samples) is the halfband decimator group delay, measured by cross-
    // correlating the same note-onset rendered at each factor (see the core
    // Decimator: downA = HalfbandFIR<47,12> for 2x<->1x, downB = <15,4> for
    // 4x<->2x). Those group delays are rate-independent in samples:
    //   1x -> 0        (oversampling bypassed, no filter)
    //   2x -> 12       (downA only: 12 host samples)
    //   4x -> 14       (downA 12 + downB 4-at-2x = 2 host samples)
    // Values verified by measurement; changing the halfband taps requires
    // re-measuring these three ints.
    static uint32_t latencyForOsParam(float osParamValue) noexcept
    {
        const int os = (int)(osParamValue + 0.5f);
        if (os >= 2) return 14u; // 4x
        if (os == 1) return 12u; // 2x
        return 0u;               // 1x
    }

    void updateLatency()
    {
        setLatency(latencyForOsParam(values[kParamOversampling].load(std::memory_order_relaxed)));
    }

    // MIDI program change (0xC0) -> factory preset, through the SAME loadProgram()
    // the host's own program change uses, so the preset rows, the smoothing snap
    // (notifyProgramChange) and the reported latency all stay consistent with a
    // host-driven change. Out-of-range programs are ignored (54 factory presets;
    // the MIDI data byte reaches 127).
    //
    // RT AUDIT — this is called from handleMidi on the audio thread. loadProgram()
    // does: kNumCoreParams + kBaselineRows + preset rows calls to setParameterValue,
    // each a pair of relaxed atomic stores (values[] and the core's params[]); one
    // dsp.notifyProgramChange() (release store); and updateLatency(), which is a
    // plain field store in DPF (Plugin::setLatency -> pData->latency = frames). No
    // allocation, no lock, no I/O, no host callback — RT-safe, so it runs inline
    // rather than through a pending-program atomic. Inline is also strictly better
    // for timing: run() splits the buffer at every MIDI event, so applying it here
    // lands the preset on the event's own frame instead of the next block boundary.
    //
    // The JACK standalone wrapper ALSO handles 0xC0 itself and then still forwards
    // the event, so there it applies twice. loadProgram() is deterministic (it
    // rewrites every parameter from defaults), so the second pass is a no-op that
    // writes the same values and snaps an already-snapped smoother.
    void loadProgramFromMidi(uint8_t program) noexcept
    {
        if (program >= (uint8_t)kNumFactoryPresets) return;
        loadProgram(program);
        // Tell a same-process UI (which the host never notifies about a change the
        // plugin made to itself) that the program moved. Sequence counter in the
        // high 24 bits so repeats of the same program are still seen as edges; it
        // skips 0 on wrap because a packed value of 0 means "no MIDI program change
        // yet" (a wrap landing on program 0 would otherwise be swallowed).
        uint32_t seq = ((midiProgramSignal.load(std::memory_order_relaxed) >> 8) + 1u) & 0xFFFFFFu;
        if (seq == 0u) seq = 1u;
        midiProgramSignal.store((seq << 8) | (uint32_t)program, std::memory_order_release);
    }

    // MIDI routing. CHANNEL POLICY: OMNI — every channel is played, the channel
    // nibble is ignored throughout. That is the synth convention (an instrument
    // plugin is already addressed per-track by the host, so a channel filter only
    // creates silent-plugin support tickets), and it is stated in the manual.
    //
    // Runs on the AUDIO THREAD, between the run() render segments, at the exact
    // frame of the event — so everything called here must be RT-safe.
    void handleMidi(const MidiEvent& ev) noexcept
    {
        if (ev.size < 2 || ev.size > MidiEvent::kDataSize) return; // ignore sysex/ext
        const uint8_t status = (uint8_t)(ev.data[0] & 0xF0u);
        switch (status)
        {
        case 0x90: // note on (velocity 0 == note off)
            if (ev.size < 3) return; // need note + velocity
            if (ev.data[2] == 0) dsp.noteOff(ev.data[1]);
            else                 dsp.noteOn(ev.data[1], (float)ev.data[2] / 127.0f);
            break;
        case 0x80: // note off (size >= 2 already guaranteed)
            dsp.noteOff(ev.data[1]);
            break;
        case 0xA0: // polyphonic key pressure -> per-voice, same mod source as 0xD0
            if (ev.size < 3) return; // need note + pressure
            dsp.polyAftertouch(ev.data[1], (float)ev.data[2] / 127.0f);
            break;
        case 0xB0: // control change
            if (ev.size < 3) return; // need controller + value
            if (ev.data[1] == 1)                         dsp.modWheel((float)ev.data[2] / 127.0f);
            else if (ev.data[1] == 64)                   dsp.sustainPedal(ev.data[2] >= 64); // damper
            else if (ev.data[1] == 120 || ev.data[1] == 123) dsp.allNotesOff(); // all sound / all notes off
            break;
        case 0xC0: // program change -> factory preset (data byte is 0..127)
            loadProgramFromMidi(ev.data[1]);
            break;
        case 0xD0: // channel pressure (aftertouch, size >= 2 already guaranteed)
            dsp.aftertouch((float)ev.data[1] / 127.0f);
            break;
        case 0xE0: // pitch bend (14-bit, centred at 8192)
        {
            if (ev.size < 3) return; // need both data bytes
            const int v = (int)ev.data[1] | ((int)ev.data[2] << 7);
            dsp.pitchBend((float)(v - 8192) / 8192.0f);
            break;
        }
        default: break;
        }
    }

    msynth::MultiSynthDSP dsp;
    double lastBpm = 120.0;
    // Audio thread writes on a MIDI program change, UI thread polls (see
    // loadProgramFromMidi / MultiSynthAccess.hpp).
    std::atomic<uint32_t> midiProgramSignal { 0 };
    // Host thread writes (setParameterValue/loadProgram); run() reads on the
    // audio thread. Atomic (relaxed) removes the data race, same as the core.
    std::atomic<float> values[kNumCoreParams] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiSynthPlugin)
};

Plugin* createPlugin() { return new MultiSynthPlugin(); }

END_NAMESPACE_DISTRHO

// Same-process UI accessors (see MultiSynthAccess.hpp). Strong definitions; the
// weak UI-side references resolve here in every single-binary format.
float multiSynthGetOutLevelL(void* const p) noexcept
{ return p ? static_cast<DISTRHO_NAMESPACE::MultiSynthPlugin*>(p)->getOutLevelLForUI() : 0.0f; }
float multiSynthGetOutLevelR(void* const p) noexcept
{ return p ? static_cast<DISTRHO_NAMESPACE::MultiSynthPlugin*>(p)->getOutLevelRForUI() : 0.0f; }
int multiSynthGetArpStep(void* const p) noexcept
{ return p ? static_cast<DISTRHO_NAMESPACE::MultiSynthPlugin*>(p)->getArpStepForUI() : -1; }
uint32_t multiSynthGetMidiProgramSignal(void* const p) noexcept
{ return p ? static_cast<DISTRHO_NAMESPACE::MultiSynthPlugin*>(p)->getMidiProgramSignalForUI() : 0u; }
msynth::MultiSynthDSP* multiSynthGetDSP(void* const p) noexcept
{ return p ? static_cast<DISTRHO_NAMESPACE::MultiSynthPlugin*>(p)->getDSPForUI() : nullptr; }
