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
#include "DistrhoPluginUtils.hpp"   // getPluginFormatName()
#include "MultiSynthAccess.hpp"
#include "MultiSynthDSP.hpp"
#include "MultiSynthParams.hpp"

#include <atomic>
#include <cstring>

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

//---------------------------------------------------------------------------
// Enum-backed INT parameters: the exposed [min..max] range must cover exactly
// the core enum's value set. Without this, adding an enumerator to a core enum
// silently leaves the new value HOST-UNREACHABLE (no automation, no preset, no
// UI) — the parameter range stays at the old maximum and nothing complains.
// The generated table (tools/gen_params.py) is the thing that must be edited
// when one of these fires.
//
// Two tiers, because the core enums differ:
//   (a) enums with a cardinality sentinel/count -> asserted against the count,
//       so BOTH inserting and appending an enumerator trips the assert.
//   (b) enums with no sentinel -> asserted against the LAST enumerator, which
//       catches reordering/removal/insertion. Appending past the last one can
//       only be caught by a sentinel, which lives in core/ (out of scope for
//       this shell); adding one there would upgrade these to tier (a).
static constexpr int paramMaxI(int i) { return (int)kParamDefs[i].max; }
static constexpr int paramMinI(int i) { return (int)kParamDefs[i].min; }

// (a) sentinel/count-backed — append-proof.
static_assert(paramMaxI(kParamArpRate)  == (int)msynth::ArpRateDivision::NumDivisions - 1,
              "ArpRate range vs ArpRateDivision cardinality");
static_assert(paramMaxI(kParamDelayDiv) == (int)msynth::ArpRateDivision::NumDivisions - 1,
              "DelayDiv range vs ArpRateDivision cardinality (same division enum)");
static_assert(paramMaxI(kParamModSrc0)  == msynth::kNumModSources - 1,
              "Mod source range vs ModSource cardinality");
static_assert(paramMaxI(kParamModDst0)  == msynth::kNumModDests - 1,
              "Mod dest range vs ModDest cardinality");
static_assert(paramMaxI(kParamPrismAlgo)
                  == (int)(sizeof(msynth::kPrismAlgos) / sizeof(msynth::kPrismAlgos[0])) - 1,
              "Prism algorithm range vs kPrismAlgos table size");
static_assert(paramMaxI(kParamUnisonVoices) == msynth::kMaxUnison,
              "Unison voices range vs kMaxUnison");
// The mod matrix is kNumModSlots contiguous triples; the last slot's params must
// land exactly where the count says, and every slot must share slot 0's range.
static_assert((int)kParamModSrc7 - (int)kParamModSrc0 == msynth::kNumModSlots - 1,
              "mod source block width vs kNumModSlots");
static_assert((int)kParamModDst7 - (int)kParamModDst0 == msynth::kNumModSlots - 1,
              "mod dest block width vs kNumModSlots");
static_assert((int)kParamModAmt7 - (int)kParamModAmt0 == msynth::kNumModSlots - 1,
              "mod amount block width vs kNumModSlots");
static_assert(paramMaxI(kParamModSrc7) == paramMaxI(kParamModSrc0)
                  && paramMaxI(kParamModDst7) == paramMaxI(kParamModDst0),
              "mod matrix slots must all expose the same source/dest range");

// (b) last-enumerator-backed — catches insertion/reorder/removal.
static_assert(paramMaxI(kParamMode)      == (int)msynth::SynthMode::Acid,
              "Mode range vs SynthMode");
static_assert(paramMaxI(kParamOsc1Wave)  == (int)msynth::Waveform::Pulse
                  && paramMaxI(kParamOsc2Wave) == (int)msynth::Waveform::Pulse,
              "Osc 1/2 wave range vs Waveform (Saw..Pulse; Noise is a separate source)");
static_assert(paramMaxI(kParamAmpCurve)  == (int)msynth::EnvelopeCurve::AnalogRC
                  && paramMaxI(kParamFiltCurve) == (int)msynth::EnvelopeCurve::AnalogRC,
              "Amp/Filter curve range vs EnvelopeCurve");
static_assert(paramMaxI(kParamLfo1Shape) == (int)msynth::LFOShape::RandomSmooth
                  && paramMaxI(kParamLfo2Shape) == (int)msynth::LFOShape::RandomSmooth,
              "LFO shape range vs LFOShape");
static_assert(paramMaxI(kParamArpMode)   == (int)msynth::ArpMode::Chord,
              "ArpMode range vs ArpMode");
static_assert(paramMaxI(kParamArpVelMode) == (int)msynth::ArpVelocityMode::AccentPattern,
              "Arp velocity mode range vs ArpVelocityMode");
static_assert(paramMaxI(kParamDriveType) == (int)msynth::DriveType::Tube,
              "Drive type range vs DriveType");
static_assert(paramMaxI(kParamCosmosChorus) == (int)msynth::CosmosChorusMode::Both,
              "Cosmos chorus range vs CosmosChorusMode");
// Every enum-backed choice starts at the enum's zero value.
static_assert(paramMinI(kParamMode) == 0 && paramMinI(kParamArpRate) == 0
                  && paramMinI(kParamDelayDiv) == 0 && paramMinI(kParamModSrc0) == 0
                  && paramMinI(kParamModDst0) == 0 && paramMinI(kParamPrismAlgo) == 0
                  && paramMinI(kParamArpMode) == 0 && paramMinI(kParamDriveType) == 0,
              "enum-backed choice params must start at 0");

// Which BBT beat convention the wrapper we were loaded as delivers (see the long
// note in run()). CLAP and AudioUnit hand us quarter notes; VST2/VST3/LV2/JACK
// hand us time-signature-denominator beats.
//
// This has to be a RUNTIME query. Compile-time is not an option: DPF's CMake
// applies DISTRHO_PLUGIN_TARGET_<FMT> only to the per-format module target
// (DPF cmake/DPF-plugin.cmake:1688), while this file is compiled exactly once
// into the shared <name>-dsp static library that every format binary links —
// verified in our own build.ninja, where MultiSynthPlugin.cpp.o has no
// DISTRHO_PLUGIN_TARGET_* define at all. One object, six wrappers.
//
// DistrhoPluginUtils.hpp:45 says "DO NOT CHANGE PLUGIN BEHAVIOUR BASED ON PLUGIN
// FORMAT". That rule exists so plugins do not offer different features per
// format. This is the opposite: it normalises a documented disagreement between
// the wrappers so the musical behaviour is IDENTICAL in every format. Without
// it, the same project plays a different arp rate in CLAP than in VST3.
//
// Unknown/renamed format names fall through to the denominator convention, which
// is what four of the six wrappers do and what this plugin shipped with.
static bool detectBbtBeatIsQuarterNote() noexcept
{
    const char* const fmt = getPluginFormatName();
    if (fmt == nullptr) return false;
    return std::strcmp(fmt, "CLAP") == 0 || std::strcmp(fmt, "AudioUnit") == 0;
}

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
            // Display-only unit ("Hz"/"dB"/"ms"/"s"/"st"/"ct"); blank for
            // dimensionless params. Skipping the empty case avoids 200-odd
            // pointless String assignments at scan time.
            if (d.unit[0] != '\0') p.unit = d.unit;
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

    // Two threads can ask for a program: the HOST (main thread, this override)
    // and the plugin itself from a MIDI 0xC0 on the AUDIO thread
    // (loadProgramFromMidi). Both walk the same ~250 setParameterValue() writes.
    // Interleaved, they produce a HYBRID patch — preset A's oscillator section
    // with preset B's filter section — that matches neither program and that the
    // user cannot reproduce or undo.
    //
    // programBusy serialises them without ever blocking the audio thread:
    // whoever gets the flag applies the program; the loser does not spin and does
    // not drop the request, it parks the program index in pendingProgram and
    // run() retries it at the top of the next buffer.
    void loadProgram(uint32_t index) override
    {
        if (index >= (uint32_t)kNumFactoryPresets) return;
        if (programBusy.test_and_set(std::memory_order_acquire))
        {
            // Contended. Defer instead of interleaving or spinning. A newer
            // request simply overwrites an older pending one — the user's last
            // choice is the one that should win.
            pendingProgram.store((int32_t)index, std::memory_order_release);
            return;
        }
        applyProgram(index);
        programBusy.clear(std::memory_order_release);
    }

    //--- lifecycle -------------------------------------------------------------
    void activate() override
    {
        dsp.prepare(getSampleRate(), (int)getBufferSize());
        pushAllParams();
        updateLatency(); // E1
        haveLastTransportFrame = false; // S3: no stale frame across a re-activate
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
        // Retry a program change that lost the programBusy race (see loadProgram).
        drainPendingProgram();

        const TimePosition& tp = getTimePosition();
        // Any positive tempo is a real tempo. The old ">20" floor silently
        // rejected legitimate slow tempi (a 15 BPM ambient/film-score session is
        // valid) and left the arp/delay running at the stale previous BPM; only
        // zero/negative is actually meaningless.
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 0.0)
            lastBpm = tp.bbt.beatsPerMinute;
        dsp.setTempo(lastBpm, tp.playing);

        // Host phase-lock: song position in beats at frame 0 of this buffer.
        // Prefer BBT (bar/beat/tick); fall back to the transport frame counter.
        // The DSP only locks when the transport is playing (see processBlock).
        double songBeat = 0.0;
        bool   songValid = false;
        if (tp.bbt.valid && tp.bbt.ticksPerBeat > 0.0)
        {
            // ---- PER-WRAPPER BBT BEAT SEMANTICS (S1) -------------------------
            // DPF's BarBeatTick struct is a copy of jack_position_t and its doc
            // (DistrhoDetails.hpp:987-1000) only constrains beat <= beatsPerBar;
            // it never states the unit of a "beat". The wrappers disagree:
            //
            //   VST3, VST2, LV2, JACK  ->  beat/tick in TIME-SIGNATURE
            //                              DENOMINATOR units (in 6/8 a beat is an
            //                              eighth note, beat runs 1..6).
            //     DistrhoPluginVST3.cpp:1422-1429  ppqPerBar = num*4/denom, then
            //       barBeats = fmod(ppq, ppqPerBar)/ppqPerBar * num  -> denominator
            //     DistrhoPluginVST2.cpp:1036-1043  identical arithmetic
            //     DistrhoPluginLV2.cpp:460-464     pass-through of time:barBeat,
            //       which LV2 defines in time:beatUnit (denominator) units
            //     DistrhoPluginJACK.cpp:394-396    verbatim jack_position_t copy
            //
            //   CLAP, AudioUnit        ->  beat/tick in QUARTER NOTES, merely
            //                              wrapped modulo the NUMERATOR.
            //     DistrhoPluginCLAP.cpp:930-936    clapBeats = song_pos_beats >> 31,
            //       and CLAP defines song_pos_beats in quarter notes
            //       (clap/events.h:219, fixedpoint.h:12 CLAP_BEATTIME_FACTOR).
            //       beat = clapBeats % tsig_num + 1; tick = frac(quarter)*1920.
            //     DistrhoPluginAU.cpp:2334-2339    same shape, on the AU quarter-
            //       note musical timeline divided by the numerator.
            //
            // So the raw sum below is denominator-beats on the first group and
            // ALREADY quarter notes on the second, while the core's
            // getBeatsPerStep() speaks quarter notes. Scaling by 4/beatType is
            // therefore correct for the first group and a DOUBLE conversion for
            // the second: in 6/8 it multiplied CLAP's quarter notes by 0.5 and ran
            // the arp/acid grid at half rate under CLAP (and AU) only.
            //
            // NOTE the scale factor is exactly 1.0 whenever beatType == 4, so for
            // every x/4 signature both branches are bit-identical and this
            // distinction can only ever change behaviour in 6/8, 12/8, 2/2, ...
            songBeat = (double)(tp.bbt.bar - 1) * (double)tp.bbt.beatsPerBar
                     + (double)(tp.bbt.beat - 1)
                     + tp.bbt.tick / tp.bbt.ticksPerBeat;
            if (!bbtBeatIsQuarterNote && tp.bbt.beatType > 0.0f)
                songBeat *= 4.0 / (double)tp.bbt.beatType;
            songValid = true;
        }
        else
        {
            // No BBT, OR BBT with no sub-beat tick resolution (ticksPerBeat <= 0
            // — only JACK can report that, DistrhoPluginJACK.cpp:392 sets valid
            // from the BBT flag alone while :408 copies ticks_per_beat unguarded).
            // In the latter case BBT is block-constant within a beat, so songBeat
            // would jump backward at each buffer start and machine-gun the arp/
            // acid backward-jump (loop-wrap) detector. Derive songBeat from the
            // transport frame counter instead (S2).
            //
            // tp.frame is NOT a dependable song-position counter on every wrapper
            // (S3):
            //   VST3 (DistrhoPluginVST3.cpp:1409-1412) has no else branch — when
            //     the host sets neither PROJECT_TIME_VALID nor CONT_TIME_VALID,
            //     fTimePosition.frame keeps its PREVIOUS value forever (0 until
            //     the first valid block).
            //   CLAP (DistrhoPluginCLAP.cpp:916) uses process->steady_time, which
            //     is pinned to 0 on every block when the host reports -1
            //     ("not available", clap/process.h:36).
            // Either way the counter FREEZES, so songBeat is a constant. Reporting
            // that as a valid host position pins the arp/acid grid to step 0 for as
            // long as the transport rolls. Detect the freeze and report invalid
            // instead, which makes the core free-run on its own clock — a stable
            // wrong phase beats a stuck one.
            //
            // (CLAP's steady_time also keeps counting while the transport is
            // STOPPED, which is harmless here: the core only phase-locks while
            // tp.playing, see dsp.setTempo above and processBlock.)
            const int64_t frame = tp.frame;
            const bool frozen = tp.playing && frames > 0
                             && haveLastTransportFrame && frame == lastTransportFrame;
            songBeat  = (double)frame / getSampleRate() * lastBpm / 60.0;
            songValid = !frozen;
        }
        // Track the raw counter in BOTH branches so a wrapper that drops in and
        // out of BBT validity never compares against a stale sample.
        if (frames > 0)
        {
            lastTransportFrame = tp.frame;
            haveLastTransportFrame = true;
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
    // The actual program application. ONLY ever called with programBusy held, so
    // its ~250 parameter writes can never interleave with another caller's.
    void applyProgram(uint32_t index)
    {
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

    // Audio-thread retry for a program request that lost the programBusy race.
    // Never spins: if the other thread still holds the flag the request stays
    // pending and the next buffer tries again.
    void drainPendingProgram()
    {
        if (pendingProgram.load(std::memory_order_acquire) < 0) return;
        if (programBusy.test_and_set(std::memory_order_acquire)) return;
        // Claim the request only after winning the flag, so a request that lands
        // between the load and here is not swallowed.
        const int32_t p = pendingProgram.exchange(-1, std::memory_order_acq_rel);
        if (p >= 0 && p < kNumFactoryPresets)
            applyProgram((uint32_t)p);
        programBusy.clear(std::memory_order_release);
    }

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
        // Compare in int, NOT by casting the bound down to uint8_t: kNumFactoryPresets
        // is an int, and (uint8_t)kNumFactoryPresets silently wraps once the preset
        // count passes 255 (or, worse, at 256 becomes 0 and rejects everything).
        // Widening the program byte is always correct.
        if ((int)program >= kNumFactoryPresets) return;
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

        // MIDI DATA BYTES ARE 7-BIT. Mask both of them once, here, at the single
        // point where host bytes enter the engine, rather than relying on every
        // downstream callee to re-validate. DPF hands the host's bytes through
        // verbatim, so a malformed or corrupted stream can present a data byte
        // with bit 7 set. Unmasked, note 0xC8 becomes note 200 and:
        //   * MultiSynthDSP::noteOn (core MultiSynthDSP.cpp:178) guards its
        //     key-state bitmask with `note < 128` and so silently ignores it,
        //     but then still forwards the note to voices.noteOn / arp.noteOn
        //     (:195-196) — the note SOUNDS, at 2^(131/12) x A440, i.e. an
        //     ultrasonic full-level oscillator that is pure aliasing;
        //   * that same guard desyncs the arp: Arpeggiator::noteOn
        //     (Arpeggiator.hpp:100) stores note 200 in held[] unguarded, while
        //     the latch-release prune Arpeggiator::retainHeld (:75) rejects
        //     n >= 128, so releasing LATCH silently drops a note that is playing;
        //   * data byte 2 > 127 makes v/127.0f exceed 1.0 for velocity, pressure
        //     and CC. The core happens to clamp velocity (clamp01/clampi at
        //     :195-196), but the shell must not depend on that.
        // Masking rather than rejecting keeps a slightly-corrupt stream musical,
        // which is what hardware does with the same input.
        const uint8_t d1 = (uint8_t)(ev.data[1] & 0x7Fu);
        const uint8_t d2 = (uint8_t)(ev.size >= 3 ? (ev.data[2] & 0x7Fu) : 0u);

        switch (status)
        {
        case 0x90: // note on (velocity 0 == note off)
            if (ev.size < 3) return; // need note + velocity
            if (d2 == 0) dsp.noteOff(d1);
            else         dsp.noteOn(d1, (float)d2 / 127.0f);
            break;
        case 0x80: // note off (size >= 2 already guaranteed)
            dsp.noteOff(d1);
            break;
        case 0xA0: // polyphonic key pressure -> per-voice, same mod source as 0xD0
            if (ev.size < 3) return; // need note + pressure
            dsp.polyAftertouch(d1, (float)d2 / 127.0f);
            break;
        case 0xB0: // control change
            if (ev.size < 3) return; // need controller + value
            if (d1 == 1)                                 dsp.modWheel((float)d2 / 127.0f);
            else if (d1 == 64)                           dsp.sustainPedal(d2 >= 64); // damper
            else if (d1 == 121)
            {
                // Reset All Controllers. Only the controllers this instrument
                // actually implements are reset — the damper (a pedal left latched
                // down by a stopped transport is a stuck-note source in its own
                // right) and the two continuous mod sources. Synth PARAMETERS are
                // never touched here: a host sending CC121 on transport stop must
                // not silently rewrite the patch the user is editing.
                dsp.sustainPedal(false);
                dsp.modWheel(0.0f);
                dsp.aftertouch(0.0f);
                dsp.pitchBend(0.0f); // spec: RAC also centres pitch bend
            }
            // Channel-mode panic messages. The MIDI 1.0 spec defines CC120..127
            // as the channel-mode block, and 124..127 (Omni Off / Omni On /
            // Mono On / Poly On) each carry an IMPLICIT All Notes Off — hosts and
            // controllers do send them on stop/reset, and ignoring them left
            // notes hanging. 122 (Local Control) is the one member of the block
            // that is NOT a panic and is deliberately not listed.
            //
            // CC120 (All Sound Off) is spec'd as an IMMEDIATE mute that ignores
            // release envelopes, unlike CC123 (All Notes Off) which releases
            // normally. The core exposes only allNotesOff() — there is no
            // hard-kill entry point — so 120 is handled as a release here. A true
            // CC120 needs a core-side immediate-silence API (out of scope for
            // this shell; see the report).
            else if (d1 == 120 || d1 == 123 || (d1 >= 124 && d1 <= 127))
                dsp.allNotesOff();
            break;
        case 0xC0: // program change -> factory preset (data byte is 0..127)
            loadProgramFromMidi(d1);
            break;
        case 0xD0: // channel pressure (aftertouch, size >= 2 already guaranteed)
            dsp.aftertouch((float)d1 / 127.0f);
            break;
        case 0xE0: // pitch bend (14-bit, centred at 8192)
        {
            if (ev.size < 3) return; // need both data bytes
            const int v = (int)d1 | ((int)d2 << 7);
            dsp.pitchBend((float)(v - 8192) / 8192.0f);
            break;
        }
        default: break;
        }
    }

    msynth::MultiSynthDSP dsp;
    double lastBpm = 120.0;
    // Resolved once at construction (the wrapper identity cannot change for the
    // life of the instance); read on the audio thread, never written again.
    const bool bbtBeatIsQuarterNote = detectBbtBeatIsQuarterNote();
    // Freeze detection for the no-BBT transport-frame fallback (S3). Audio
    // thread only, reset in activate().
    int64_t lastTransportFrame = 0;
    bool    haveLastTransportFrame = false;
    // Audio thread writes on a MIDI program change, UI thread polls (see
    // loadProgramFromMidi / MultiSynthAccess.hpp).
    std::atomic<uint32_t> midiProgramSignal { 0 };
    // Mutual exclusion between the host's loadProgram() (main thread) and the
    // plugin's own MIDI 0xC0 (audio thread). atomic_flag is lock-free on every
    // supported target and is only ever test_and_set / clear — never waited on,
    // so the audio thread cannot block. -1 = nothing pending.
    std::atomic_flag     programBusy = ATOMIC_FLAG_INIT;
    std::atomic<int32_t> pendingProgram { -1 };
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
