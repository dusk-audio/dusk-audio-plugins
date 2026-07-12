// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// MultiSynthDSP.hpp — top-level Multi-Synth engine (framework-free C++17).
// Product name: Sunset Circuits (internal class/namespace names stay stable).
//
// Zero JUCE/DPF includes. One class wrapping the whole instrument: voices,
// mod matrix, arpeggiator, effects. The DPF shell owns MIDI parsing and the
// parameter table; it forwards everything through the thread-safe atomic API
// below.
//
// ============================ THREADING CONTRACT ============================
// prepare()/reset() run on the message/setup thread (they allocate). Everything
// else (setParameter, note/controller/tempo events, processBlock) is called
// from the audio thread. Parameters are plain atomics (relaxed); processBlock
// snapshots them once per block into a VoiceParameters struct — no atomic loads
// or string lookups in the render path.
//
// ======================== SAMPLE-ACCURATE MIDI =============================
// Note/controller/pitch-bend/tempo events carry NO explicit sample offset. The
// shell achieves sample accuracy by SPLITTING the block at each MIDI event
// offset and calling processBlock() for each segment, applying the events that
// fall on a boundary in between. e.g. for events at offsets 0, 130, 400 in a
// 512-frame block:
//     apply events@0;   processBlock(out+0,   130);
//     apply events@130; processBlock(out+130, 270);
//     apply events@400; processBlock(out+400, 112);
// The arpeggiator advances per host sample INSIDE processBlock and triggers its
// own generated notes sample-accurately, so it needs no external event stream.
//
// ============================ OVERSAMPLING (fix #1) =========================
// Voices render at internalRate = hostRate * osFactor (1/2/4, default 2x). Every
// oscillator/envelope/LFO/S&H/filter time constant derives from internalRate,
// so pitch and all envelope/LFO times are CORRECT at every factor (the JUCE
// build was an octave low at anything but 4x). Decimation back to hostRate uses
// cascaded polyphase halfband FIRs (shared DuskOversampler taps), not a box
// average. Effects, the vintage BBD chorus and the arpeggiator run at hostRate.
// Changing the factor re-prepares the voices at block start (allocation-free).

#pragma once

#include "Voice.hpp"
#include "Effects.hpp"
#include "Arpeggiator.hpp"
#include "AcidEngine.hpp"      // Acid mode (mode 5): mono voice + pattern sequencer
#include "DuskOversampler.hpp" // HalfbandFIR + hbtaps
#include "DuskFilters.hpp"     // DCBlocker (output hygiene)

#include <array>
#include <atomic>

namespace msynth
{

//==============================================================================
// Flat parameter index. The shell's param table (Phase 3) maps onto these; the
// test harness resolves them by name (see paramIndexForName).
enum Param : int
{
    // Global
    pMode = 0, pMasterTune, pMasterVol, pMasterPan, pStereoWidth, pOversampling, pAnalogAmt, pVintage,
    // Oscillators
    pOsc1Wave, pOsc1Detune, pOsc1PW, pOsc1Level,
    pOsc2Wave, pOsc2Detune, pOsc2PW, pOsc2Level, pOsc2Semi,
    pOsc3Wave, pOsc3Level, pSubLevel, pSubWave, pNoiseLevel,
    // Filter + envelopes
    pFilterCutoff, pFilterRes, pFilterHP, pFilterEnvAmt,
    pAmpA, pAmpD, pAmpS, pAmpR, pAmpCurve,
    pFiltA, pFiltD, pFiltS, pFiltR, pFiltCurve,
    // Mode-specific
    pCrossMod, pRingMod, pHardSync, pFMAmount,
    pPmFenvOscA, pPmFenvFilt, pPmOscBOscA, pPmOscBPWM, pShRate, pCosmosChorus,
    // LFOs
    pLfo1Rate, pLfo1Shape, pLfo1Fade, pLfo1Sync,
    pLfo2Rate, pLfo2Shape, pLfo2Fade, pLfo2Sync,
    // Unison / porta / velocity
    pUnisonVoices, pUnisonDetune, pUnisonSpread,
    pPortaTime, pLegato, pGlideMode, pVelSens, pVelCurve, pPbRange,
    // Arp
    pArpOn, pArpMode, pArpOctave, pArpRate, pArpGate, pArpSwing, pArpLatch,
    pArpVelMode, pArpFixedVel,
    pArpStep0, // 16 contiguous step mutes
    pArpStep15 = pArpStep0 + 15,
    // FX
    pDriveOn, pDriveType, pDriveAmt, pDriveMix,
    pChorusOn, pChorusRate, pChorusDepth, pChorusMix,
    pDelayOn, pDelaySync, pDelayTime, pDelayDiv, pDelayFB, pDelayMix, pDelayPP, pDelayTape,
    pReverbOn, pReverbSize, pReverbDecay, pReverbDamp, pReverbMix, pReverbPD,
    // Mod matrix (8 slots x 3)
    pModSrc0, pModSrc7 = pModSrc0 + 7,
    pModDst0, pModDst7 = pModDst0 + 7,
    pModAmt0, pModAmt7 = pModAmt0 + 7,
    // ---- Phase 3 engine params (modes 4/5) ----
    // Prism (4-op FM). op params are 4 contiguous blocks of 9 fields each
    // (Ratio,Fine,Level,Vel,KeyScale,A,D,S,R) so op N field F = pOp1Ratio + N*9 + F.
    pPrismAlgo, pPrismFB,
    pOp1Ratio, pOp1Fine, pOp1Level, pOp1Vel, pOp1KeyScale, pOp1A, pOp1D, pOp1S, pOp1R,
    pOp2Ratio, pOp2Fine, pOp2Level, pOp2Vel, pOp2KeyScale, pOp2A, pOp2D, pOp2S, pOp2R,
    pOp3Ratio, pOp3Fine, pOp3Level, pOp3Vel, pOp3KeyScale, pOp3A, pOp3D, pOp3S, pOp3R,
    pOp4Ratio, pOp4Fine, pOp4Level, pOp4Vel, pOp4KeyScale, pOp4A, pOp4D, pOp4S, pOp4R,
    // Acid globals + 16-step pattern rows (seqPitch/Accent/Slide; the on/off
    // row reuses the existing arpStep0-15 mutes).
    pAcidAccentAmt, pAcidSlideTime,
    pSeqPitch0,  pSeqPitch15  = pSeqPitch0  + 15,
    pSeqAccent0, pSeqAccent15 = pSeqAccent0 + 15,
    pSeqSlide0,  pSeqSlide15  = pSeqSlide0  + 15,
    kNumParams
};

// Fields per Prism op block (Ratio,Fine,Level,Vel,KeyScale,A,D,S,R): op N field
// F lives at pOp1Ratio + N*kOpParamStride + F.
static constexpr int kOpParamStride = 9;

class MultiSynthDSP
{
public:
    static constexpr int kScopeSize = 512;

    MultiSynthDSP();

    //--- lifecycle (message thread; allocates) --------------------------------
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    //--- render (audio thread; RT-safe) ---------------------------------------
    void processBlock(float* outL, float* outR, int nSamples) noexcept;

    //--- parameters (any thread) ----------------------------------------------
    void setParameter(int index, float value) noexcept
    {
        if (index >= 0 && index < kNumParams)
            params[(size_t)index].store(value, std::memory_order_relaxed);
    }
    float getParameter(int index) const noexcept
    {
        return (index >= 0 && index < kNumParams) ? params[(size_t)index].load(std::memory_order_relaxed) : 0.0f;
    }
    // Name lookup for the test harness / shell param mapping. Returns -1 if unknown.
    static int paramIndexForName(const char* name) noexcept;

    //--- MIDI / transport (audio thread; see contract above) ------------------
    void noteOn(int note, float velocity01) noexcept;
    void noteOff(int note) noexcept;
    void pitchBend(float norm) noexcept   { pitchBendNorm.store(clampf(norm, -1.0f, 1.0f), std::memory_order_relaxed); }
    void modWheel(float v01) noexcept     { modWheelValue.store(clamp01(v01), std::memory_order_relaxed); }
    void aftertouch(float v01) noexcept   { aftertouchValue.store(clamp01(v01), std::memory_order_relaxed); }
    void allNotesOff() noexcept;
    void setTempo(double bpm, bool playing) noexcept
    { hostBpm.store(bpm, std::memory_order_relaxed); transportPlaying.store(playing, std::memory_order_relaxed); }
    // Host song position at the start of the next processBlock segment, in beats.
    // When valid AND the transport is playing, the arp/acid step clock phase-locks
    // to this grid (see processBlock); otherwise the free-run clock is used.
    void setSongPosition(double beats, bool valid) noexcept
    { songPosBeats.store(beats, std::memory_order_relaxed); songPosValid.store(valid, std::memory_order_relaxed); }

    //--- observables (read from any thread) -----------------------------------
    float getOutputLevelL() const noexcept { return outLevelL.load(std::memory_order_relaxed); }
    float getOutputLevelR() const noexcept { return outLevelR.load(std::memory_order_relaxed); }
    int   getArpStep() const noexcept { return arpStep.load(std::memory_order_relaxed); }
    int   getArpTotalSteps() const noexcept { return arpTotalSteps.load(std::memory_order_relaxed); }
    // Held-note bitmask over MIDI 0..127 (bit n set between noteOn(n) and
    // noteOff(n)/allNotesOff, regardless of arp/acid routing — it tracks the
    // player's KEY state, not the sounding voices). Audio thread updates it with
    // relaxed atomic RMW; the UI reads it each frame to light the on-screen
    // keyboard while a hardware MIDI keyboard is played.
    void getHeldNotes(uint64_t& lo, uint64_t& hi) const noexcept
    {
        lo = heldNotesLo.load(std::memory_order_relaxed);
        hi = heldNotesHi.load(std::memory_order_relaxed);
    }
    // 512-sample mono scope ring (audio thread writes, UI reads). Copies the
    // NEWEST min(maxN, valid) samples oldest->newest into dst using relaxed loads;
    // returns the number of samples written (<= maxN, <= kScopeSize). Before the
    // ring is full only the valid samples are returned (no stale/zero tail).
    // Data-race-free (every slot is a relaxed std::atomic, so per-element loads
    // are well defined), but NOT a coherent snapshot: a concurrent write can
    // update slots mid-copy, so the buffer may tear across the write cursor.
    // That is fine for a visualizer — a torn frame is imperceptible.
    int copyScope(float* dst, int maxN) const noexcept
    {
        const int valid = scopeCount.load(std::memory_order_relaxed);
        int n = maxN < kScopeSize ? maxN : kScopeSize;
        if (n > valid) n = valid;
        if (n <= 0) return 0;
        const int wp = scopeWritePos.load(std::memory_order_relaxed);
        const int start = (wp - n + kScopeSize) % kScopeSize; // wp>=0 so one wrap suffices
        for (int i = 0; i < n; ++i)
            dst[i] = scope[(size_t)((start + i) % kScopeSize)].load(std::memory_order_relaxed);
        return n;
    }

private:
    static float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    float p(Param e) const noexcept { return params[(size_t)e].load(std::memory_order_relaxed); }

    void snapshotParameters() noexcept;   // atomics -> voiceParams + subsystems
    void applyOsFactor(int factor);       // (re)prepare voices at internalRate

    // --- Acid mode (mode 5) helpers ---
    // Note routing for mode 5 lives outside the poly VoiceAllocator: a single
    // AcidVoice + AcidSequencer replace the whole poly/arp path (mono).
    bool  isAcidMode() const noexcept { return (int)p(pMode) == (int)SynthMode::Acid; }
    void  acidNoteOn(int note, float velocity01) noexcept;
    void  acidNoteOff(int note) noexcept;
    // 100/127: MIDI velocity > 100 accents a live-played acid note (design doc).
    static constexpr float kAcidAccentVel = 100.0f / 127.0f;

    // Streaming stereo decimator: generate `factor` internal samples, get one
    // host sample. Halfband stage assignment mirrors DuskOversampler.
    struct Decimator
    {
        int factor = 2;
        duskaudio::HalfbandFIR<47, 12> downA; // 2x -> 1x
        duskaudio::HalfbandFIR<15, 4>  downB; // 4x -> 2x
        void setFactor(int f) noexcept { factor = (f == 4) ? 4 : (f == 2 ? 2 : 1); }
        void reset() noexcept { downA.reset(); downB.reset(); }
        float process(const float* s) noexcept
        {
            if (factor == 1) return s[0];
            if (factor == 2)
            {
                downA.push(s[0]); downA.push(s[1]);
                return downA.out(duskaudio::hbtaps::kA);
            }
            downB.push(s[0]); downB.push(s[1]); const float a = downB.out(duskaudio::hbtaps::kB);
            downB.push(s[2]); downB.push(s[3]); const float b = downB.out(duskaudio::hbtaps::kB);
            downA.push(a); downA.push(b);
            return downA.out(duskaudio::hbtaps::kA);
        }
    };

    double hostRate = 44100.0;
    int    maxBlock = 512;
    int    osFactor = 2;

    std::array<std::atomic<float>, kNumParams> params;

    VoiceAllocator voices;
    ModMatrix modMatrix;
    Arpeggiator arp;
    EffectsChain effects;
    CosmosChorusEffect cosmosChorus;
    Decimator decimL, decimR;

    // Acid mode (mode 5): mono, poly path bypassed. Voice runs at the internal
    // (oversampled) rate; the sequencer clocks at host rate (see processBlock).
    AcidVoice     acidVoice;
    AcidSequencer acidSeq;
    bool acidSeqEnabled = false;  // sequencer active this block (mode 5 + arpOn)
    // Last-note-priority held-note stack for live (non-sequencer) mono acid play.
    // Releasing the sounding note while an older note is still held returns to that
    // older note (a bare counter would leave the wrong pitch playing). RT-safe:
    // fixed array, no allocation.
    struct HeldNote { int note; float vel; };
    HeldNote acidHeld[16] {};
    int      acidHeldCount = 0;

    VoiceParameters voiceParams;

    // block-cached engine-level controls
    float masterGain = 1.0f, masterPan = 0.0f, stereoWidth = 0.5f, vintage = 0.0f;
    float baseDriveMix = 1.0f, baseChorusMix = 0.5f, baseDelayMix = 0.3f, baseReverbMix = 0.2f;
    bool  hasEffectsMixRouting = false;
    bool  arpEnabled = false;

    // Preset/mode transition tracking (stuck-note fix). snapshotParameters()
    // compares these against the current snapshot: a mode change or an arp /
    // acid-sequencer disable while a note is held would otherwise strand the
    // sounding voice (its note-off routes through the new path, or the disabled
    // subsystem stops advancing), so we release it. haveLastSnap suppresses a
    // spurious release on the very first block (before any real transition).
    bool      haveLastSnap = false;
    SynthMode lastSnapMode = SynthMode::Cosmos;
    bool      lastArpEnabled = false;
    bool      lastAcidSeqEnabled = false;

    float prevVintageL = 0.0f, prevVintageR = 0.0f;
    Xorshift vintageRng;
    duskaudio::DCBlocker dcBlockL, dcBlockR; // output hygiene (removes reverb/nonlinearity DC)

    // MIDI / transport state
    std::atomic<float> pitchBendNorm { 0.0f };
    std::atomic<float> modWheelValue { 0.0f };
    std::atomic<float> aftertouchValue { 0.0f };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> transportPlaying { false };
    std::atomic<double> songPosBeats { 0.0 };
    std::atomic<bool> songPosValid { false };

    // observables
    std::atomic<float> outLevelL { -60.0f };
    std::atomic<float> outLevelR { -60.0f };
    std::atomic<int>   arpStep { 0 };
    std::atomic<int>   arpTotalSteps { 0 };
    std::array<std::atomic<float>, kScopeSize> scope {};
    std::atomic<int>   scopeWritePos { 0 };
    std::atomic<int>   scopeCount { 0 };  // saturating count of valid samples (<= kScopeSize)
    std::atomic<uint64_t> heldNotesLo { 0 }, heldNotesHi { 0 };  // key-state mask, see getHeldNotes()
    float meterL = 0.0f, meterR = 0.0f, meterDecay = 0.9999f;
};

} // namespace msynth
