// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// MultiSynthDSP.cpp — top engine implementation (see MultiSynthDSP.hpp).

#include "MultiSynthDSP.hpp"
#include "DuskDenormals.hpp"

#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace msynth
{

//==============================================================================
// Parameter name/default table. The 16 arp-step and 24 mod-matrix entries are
// contiguous in the enum; listed explicitly here for name resolution.
namespace
{
struct ParamDef { int idx; const char* name; float def; };

const ParamDef kParamDefs[] = {
    { pMode, "mode", 0 }, { pMasterTune, "masterTune", 0 }, { pMasterVol, "masterVol", 0 },
    { pMasterPan, "masterPan", 0 }, { pStereoWidth, "stereoWidth", 0.5f },
    { pOversampling, "oversampling", 1 }, { pAnalogAmt, "analogAmt", 0.2f }, { pVintage, "vintage", 0 },

    { pOsc1Wave, "osc1Wave", 0 }, { pOsc1Detune, "osc1Detune", 0 }, { pOsc1PW, "osc1PW", 0.5f }, { pOsc1Level, "osc1Level", 1.0f },
    { pOsc2Wave, "osc2Wave", 0 }, { pOsc2Detune, "osc2Detune", 7.0f }, { pOsc2PW, "osc2PW", 0.5f }, { pOsc2Level, "osc2Level", 0.8f },
    { pOsc2Semi, "osc2Semi", 0 }, { pOsc3Wave, "osc3Wave", 0 }, { pOsc3Level, "osc3Level", 0.5f },
    { pSubLevel, "subLevel", 0.5f }, { pSubWave, "subWave", 0 }, { pNoiseLevel, "noiseLevel", 0 },

    { pFilterCutoff, "filterCutoff", 8000.0f }, { pFilterRes, "filterRes", 0.3f },
    { pFilterHP, "filterHP", 20.0f }, { pFilterEnvAmt, "filterEnvAmt", 0.5f },
    { pAmpA, "ampA", 0.01f }, { pAmpD, "ampD", 0.2f }, { pAmpS, "ampS", 0.8f }, { pAmpR, "ampR", 0.3f }, { pAmpCurve, "ampCurve", 3 },
    { pFiltA, "filtA", 0.01f }, { pFiltD, "filtD", 0.3f }, { pFiltS, "filtS", 0.4f }, { pFiltR, "filtR", 0.5f }, { pFiltCurve, "filtCurve", 3 },

    { pCrossMod, "crossMod", 0 }, { pRingMod, "ringMod", 0 }, { pHardSync, "hardSync", 0 }, { pFMAmount, "fmAmount", 0 },
    { pPmFenvOscA, "pmFenvOscA", 0 }, { pPmFenvFilt, "pmFenvFilt", 0 }, { pPmOscBOscA, "pmOscBOscA", 0 }, { pPmOscBPWM, "pmOscBPWM", 0 },
    { pShRate, "shRate", 5.0f }, { pCosmosChorus, "cosmosChorus", 3 },

    { pLfo1Rate, "lfo1Rate", 1.0f }, { pLfo1Shape, "lfo1Shape", 0 }, { pLfo1Fade, "lfo1Fade", 0 }, { pLfo1Sync, "lfo1Sync", 0 },
    { pLfo2Rate, "lfo2Rate", 0.5f }, { pLfo2Shape, "lfo2Shape", 0 }, { pLfo2Fade, "lfo2Fade", 0 }, { pLfo2Sync, "lfo2Sync", 0 },

    { pUnisonVoices, "unisonVoices", 1 }, { pUnisonDetune, "unisonDetune", 10.0f }, { pUnisonSpread, "unisonSpread", 1.0f },
    { pPortaTime, "portaTime", 0 }, { pLegato, "legato", 0 }, { pGlideMode, "glideMode", 0 },
    { pVelSens, "velSens", 0.7f }, { pVelCurve, "velCurve", 0 }, { pPbRange, "pbRange", 2 },

    { pArpOn, "arpOn", 0 }, { pArpMode, "arpMode", 0 }, { pArpOctave, "arpOctave", 1 }, { pArpRate, "arpRate", 3 },
    { pArpGate, "arpGate", 0.5f }, { pArpSwing, "arpSwing", 0 }, { pArpLatch, "arpLatch", 0 },
    { pArpVelMode, "arpVelMode", 0 }, { pArpFixedVel, "arpFixedVel", 100 },

    { pDriveOn, "driveOn", 0 }, { pDriveType, "driveType", 0 }, { pDriveAmt, "driveAmt", 0.3f }, { pDriveMix, "driveMix", 1.0f },
    { pChorusOn, "chorusOn", 0 }, { pChorusRate, "chorusRate", 0.8f }, { pChorusDepth, "chorusDepth", 0.5f }, { pChorusMix, "chorusMix", 0.5f },
    { pDelayOn, "delayOn", 0 }, { pDelaySync, "delaySync", 1 }, { pDelayTime, "delayTime", 500.0f }, { pDelayDiv, "delayDiv", 3 },
    { pDelayFB, "delayFB", 0.3f }, { pDelayMix, "delayMix", 0.3f }, { pDelayPP, "delayPP", 0 }, { pDelayTape, "delayTape", 0 },
    { pReverbOn, "reverbOn", 0 }, { pReverbSize, "reverbSize", 0.5f }, { pReverbDecay, "reverbDecay", 2.0f },
    { pReverbDamp, "reverbDamp", 0.3f }, { pReverbMix, "reverbMix", 0.2f }, { pReverbPD, "reverbPD", 20.0f },

    // --- Prism (4-op FM). Default = algo 4 (dual stacks): op1/op3 carriers,
    //     op2/op4 modulators. ADSR defaults mirror FMVoiceEngine's ctor. ---
    { pPrismAlgo, "prismAlgo", 4 }, { pPrismFB, "prismFB", 0 },
    { pOp1Ratio, "op1Ratio", 1.0f }, { pOp1Fine, "op1Fine", 0 }, { pOp1Level, "op1Level", 1.0f },
    { pOp1Vel, "op1Vel", 0 }, { pOp1KeyScale, "op1KeyScale", 0 },
    { pOp1A, "op1A", 0.005f }, { pOp1D, "op1D", 0.4f }, { pOp1S, "op1S", 0.7f }, { pOp1R, "op1R", 0.4f },
    { pOp2Ratio, "op2Ratio", 1.0f }, { pOp2Fine, "op2Fine", 0 }, { pOp2Level, "op2Level", 0.5f },
    { pOp2Vel, "op2Vel", 0 }, { pOp2KeyScale, "op2KeyScale", 0 },
    { pOp2A, "op2A", 0.005f }, { pOp2D, "op2D", 0.4f }, { pOp2S, "op2S", 0.7f }, { pOp2R, "op2R", 0.4f },
    { pOp3Ratio, "op3Ratio", 1.0f }, { pOp3Fine, "op3Fine", 0 }, { pOp3Level, "op3Level", 0.8f },
    { pOp3Vel, "op3Vel", 0 }, { pOp3KeyScale, "op3KeyScale", 0 },
    { pOp3A, "op3A", 0.005f }, { pOp3D, "op3D", 0.4f }, { pOp3S, "op3S", 0.7f }, { pOp3R, "op3R", 0.4f },
    { pOp4Ratio, "op4Ratio", 1.0f }, { pOp4Fine, "op4Fine", 0 }, { pOp4Level, "op4Level", 0.5f },
    { pOp4Vel, "op4Vel", 0 }, { pOp4KeyScale, "op4KeyScale", 0 },
    { pOp4A, "op4A", 0.005f }, { pOp4D, "op4D", 0.4f }, { pOp4S, "op4S", 0.7f }, { pOp4R, "op4R", 0.4f },

    // --- Acid globals (per-step seqPitch/Accent/Slide resolved by prefix below) ---
    { pAcidAccentAmt, "acidAccentAmt", 0.7f }, { pAcidSlideTime, "acidSlideTime", 60.0f },
};
} // namespace

int MultiSynthDSP::paramIndexForName(const char* name) noexcept
{
    for (const auto& d : kParamDefs)
        if (std::strcmp(d.name, name) == 0) return d.idx;
    // contiguous groups
    auto match = [name](const char* prefix, int base, int count) -> int {
        const size_t plen = std::strlen(prefix);
        if (std::strncmp(name, prefix, plen) != 0) return -1;
        int n = std::atoi(name + plen);
        return (n >= 0 && n < count) ? base + n : -1;
    };
    int r;
    if ((r = match("arpStep",   pArpStep0,   16)) >= 0) return r;
    if ((r = match("modSrc",    pModSrc0,     8)) >= 0) return r;
    if ((r = match("modDst",    pModDst0,     8)) >= 0) return r;
    if ((r = match("modAmt",    pModAmt0,     8)) >= 0) return r;
    if ((r = match("seqPitch",  pSeqPitch0,  16)) >= 0) return r;
    if ((r = match("seqAccent", pSeqAccent0, 16)) >= 0) return r;
    if ((r = match("seqSlide",  pSeqSlide0,  16)) >= 0) return r;
    return -1;
}

//==============================================================================
MultiSynthDSP::MultiSynthDSP()
{
    for (auto& a : params) a.store(0.0f, std::memory_order_relaxed);
    for (const auto& d : kParamDefs) params[(size_t)d.idx].store(d.def, std::memory_order_relaxed);
    for (int i = 0; i < 16; ++i) params[(size_t)(pArpStep0 + i)].store(1.0f, std::memory_order_relaxed); // steps on
    vintageRng.seed(0xBEEF1234u);
}

void MultiSynthDSP::prepare(double sampleRate, int maxBlockSize)
{
    hostRate = sampleRate;
    maxBlock = maxBlockSize;
    osFactor = 2;
    effects.prepare(hostRate, maxBlockSize);
    cosmosChorus.prepare(hostRate, maxBlockSize);
    arp.prepare(hostRate);
    meterDecay = std::exp(-1.0f / (0.3f * (float)hostRate));
    modeFadeStep = 1.0f / (float)(kModeFadeSeconds * hostRate);
    dcBlockL.setSampleRate(hostRate);
    dcBlockR.setSampleRate(hostRate);
    // Full voice init at the default internal rate; later factor changes use the
    // musical-state-preserving setSampleRate path (see applyOsFactor).
    voices.prepare(hostRate * (double)osFactor);
    // Acid voice runs at the internal (oversampled) rate; the sequencer clocks
    // at host rate (its samplesPerStep must match the per-host-sample advance).
    acidVoice.prepare(hostRate * (double)osFactor);
    acidSeq.prepare(hostRate);
    decimL.setFactor(osFactor); decimR.setFactor(osFactor);
    // Parameter smoothers advance once per HOST sample (see processBlock), so
    // they are prepared at hostRate regardless of the oversampling factor.
    for (auto* s : { &smGain, &smPanAngle, &smWidth,
                     &smCutoff, &smRes, &smHPCutoff, &smFilterEnvAmt,
                     &smOsc1Level, &smOsc2Level, &smOsc3Level, &smSubLevel, &smNoiseLevel })
        s->prepare(hostRate, kParamSmoothTau);
    reset();
}

void MultiSynthDSP::applyOsFactor(int factor)
{
    osFactor = (factor == 4) ? 4 : (factor == 2 ? 2 : 1);
    const double internalRate = hostRate * (double)osFactor;
    // Allocation-free rate change that preserves active notes and their pitch.
    voices.setSampleRate(internalRate);
    acidVoice.setSampleRate(internalRate); // host-rate sequencer unaffected
    decimL.setFactor(osFactor); decimL.reset();
    decimR.setFactor(osFactor); decimR.reset();
}

void MultiSynthDSP::reset()
{
    voices.reset();
    effects.reset();
    cosmosChorus.reset();
    arp.reset();
    acidVoice.reset(); acidSeq.reset(); acidHeldCount = 0;
    decimL.reset(); decimR.reset();
    dcBlockL.reset(); dcBlockR.reset();
    prevVintageL = prevVintageR = 0.0f;
    meterL = meterR = 0.0f;
    haveLastSnap = false;   // first snapshot after (re)prepare must not spuriously release
    haveWitness  = false;   // ...and must SNAP the smoothers, not glide up from stale state
    modeSwitchPending = false;  // ...and must adopt the requested mode, not fade into it
    modeFade = 1.0f;
    for (auto& s : scope) s.store(0.0f, std::memory_order_relaxed);
    scopeWritePos.store(0, std::memory_order_relaxed);
    scopeCount.store(0, std::memory_order_relaxed);
    heldNotesLo.store(0, std::memory_order_relaxed);
    heldNotesHi.store(0, std::memory_order_relaxed);
    sustainDown = false;    // a re-prepare starts with the pedal up and nothing captured
    clearSustained();
}

//==============================================================================
void MultiSynthDSP::noteOn(int note, float velocity01) noexcept
{
    if (note >= 0 && note < 128)
    {
        (note < 64 ? heldNotesLo : heldNotesHi)
            .fetch_or(1ull << (note & 63), std::memory_order_relaxed);
        // Re-pressing a key that the pedal is currently holding hands the note back
        // to the KEY: it retriggers now, and the pending pedal release is dropped so
        // lifting the pedal cannot cut a note whose key is still down. If the key
        // goes up again while the pedal is still down it is simply captured afresh.
        (note < 64 ? sustainedLo : sustainedHi) &= ~(1ull << (note & 63));
    }
    if (isAcidMode()) { acidNoteOn(note, velocity01); return; }
    // Keep voiceParams.mode current even for a frame-0 note that arrives before
    // the block's snapshot runs — the voice needs it to trigger the right osc
    // section (Prism retriggers its 4 operator envelopes at note-on). renderMode()
    // rather than the raw parameter: while a mode-switch crossfade is in flight the
    // engine is still rendering the OLD mode, and a note arriving now belongs to it.
    voiceParams.mode = renderMode();
    if (p(pArpOn) > 0.5f) { arp.setEnabled(true); arp.noteOn(note, clampi((int)(velocity01 * 127.0f), 1, 127)); }
    else voices.noteOn(note, clamp01(velocity01), voiceParams);
}

void MultiSynthDSP::noteOff(int note) noexcept
{
    if (note >= 0 && note < 128)
    {
        // The key-state mask always clears: the key IS up, whatever the pedal is
        // doing. Latch pruning combines it with the pedal mask explicitly (see
        // snapshotParameters), so nothing downstream loses the sustained notes.
        (note < 64 ? heldNotesLo : heldNotesHi)
            .fetch_and(~(1ull << (note & 63)), std::memory_order_relaxed);
        if (sustainDown)
        {
            (note < 64 ? sustainedLo : sustainedHi) |= 1ull << (note & 63);
            return;   // deferred to sustainPedal(false)
        }
    }
    routeNoteOff(note);
}

// Release `note` through whichever path currently owns it. Called by a live key-up
// and by the pedal-up sweep, so a deferred release is identical to a live one.
void MultiSynthDSP::routeNoteOff(int note) noexcept
{
    if (isAcidMode()) { acidNoteOff(note); return; }
    if (p(pArpOn) > 0.5f) arp.noteOff(note);
    else voices.noteOff(note);
}

// ============================ SUSTAIN CONTRACT ===============================
// * Pedal down: note-offs are captured, not released. The voice sustains; the key
//   mask still clears, so "which keys are down" stays truthful for the UI keyboard.
// * Pedal up: every captured note whose key is NOT currently down is released.
//   Notes re-pressed while the pedal was down survive (their key is down).
// * Arp / acid sequencer: the pedal feeds the held-set exactly like a key. A
//   captured note-off never reaches Arpeggiator::noteOff, so the pattern keeps
//   running until the pedal lifts — which is also how LATCH behaves, except the
//   pedal is momentary. Latch-off pruning therefore matches captured notes against
//   (keys | pedal) so a sustained note is not pruned out from under the pedal.
// * Mono (mode 2) needs no special case: it is the poly path with modeVoices == 1,
//   so a pedal-held note simply keeps sounding until the pedal lifts.
// * Acid (mode 5) is last-note-hold: captured notes stay on the held-note stack, so
//   the pedal holds the last note (and the sequencer's root) exactly as a key would.
//   The real TB-303 has no pedal input at all; ignoring CC64 there would be equally
//   defensible, but the uniform behaviour is what a player expects from a mode
//   rocker on one instrument, and it cannot strand a note (see releaseSustained).
// * allNotesOff (CC120/CC123) clears the captured set AND lifts the pedal: a panic
//   that left the pedal latched down would re-strand the very next note played,
//   which is exactly the stuck note the panic exists to clear.
// * A mode switch clears the captured set with the rest of the note state
//   (snapshotParameters); a preset change WITHIN a mode keeps held notes seamless,
//   and pedal-held notes are held notes, so they are kept too.
void MultiSynthDSP::sustainPedal(bool down) noexcept
{
    if (down == sustainDown) return;
    sustainDown = down;
    if (down) { clearSustained(); return; }
    const uint64_t lo = sustainedLo, hi = sustainedHi;
    clearSustained();
    // Keys physically down are NOT released — the pedal was only ever holding the
    // notes whose keys had already come up.
    releaseSustained(lo & ~heldNotesLo.load(std::memory_order_relaxed),
                     hi & ~heldNotesHi.load(std::memory_order_relaxed));
}

void MultiSynthDSP::releaseSustained(uint64_t lo, uint64_t hi) noexcept
{
    if ((lo | hi) == 0) return;
    auto set = [&](int n) noexcept
    { return ((((n < 64) ? lo : hi) >> (n & 63)) & 1ull) != 0; };

    // Live (non-sequencer) Acid is a last-note-priority mono stack whose TOP note is
    // the one sounding. Releasing the top first makes acidNoteOff glide the voice
    // down to the next held note — a note that is about to be released on this very
    // sample, so the slide is an audible blip on the way to silence. Release it
    // last and the stack unwinds silently. Every other path is order-independent.
    int last = -1;
    if (isAcidMode() && p(pArpOn) <= 0.5f && acidHeldCount > 0)
    {
        const int top = acidHeld[acidHeldCount - 1].note;
        if (top >= 0 && top < 128 && set(top)) last = top;
    }
    for (int n = 0; n < 128; ++n)
        if (n != last && set(n)) routeNoteOff(n);
    if (last >= 0) routeNoteOff(last);
}

void MultiSynthDSP::polyAftertouch(int note, float v01) noexcept
{
    // Per-voice: the mod matrix reads max(channel, key) pressure inside the voice
    // (Voice.hpp), so one Aftertouch routing serves both message types. Acid's mono
    // engine has no matrix, so mode 5 is a no-op — and with the arp on, the sounding
    // notes are the arp's transposed copies, which no incoming key number matches;
    // both cases fall through as "no voice plays this note", which is the correct
    // drop rather than a patch-wide jump.
    if (note < 0 || note > 127) return;
    voices.setPolyPressure(note, clamp01(v01));
}

void MultiSynthDSP::allNotesOff() noexcept
{
    heldNotesLo.store(0, std::memory_order_relaxed);
    heldNotesHi.store(0, std::memory_order_relaxed);
    sustainDown = false;   // a panic that leaves the pedal down re-strands the next note
    clearSustained();
    voices.allNotesOff();
    // Empty the arp / sequencer held-sets UNCONDITIONALLY. Both reset() and
    // clearLatch() deliberately keep held notes while latch is engaged, so a panic
    // with LATCH on left the pattern running (measured: arp -21.0 dB, acid seq
    // -16.6 dB, 1.5 s after the panic, against -400 dB unlatched). retainHeld
    // against an empty key mask is the one call that drops every held note whatever
    // latch says, and it must come BEFORE the resets so the reset clears the
    // step/note state of an already-empty sequencer.
    arp.retainHeld(0, 0);
    acidSeq.retainHeld(0, 0);
    arp.reset();
    acidVoice.noteOff();
    acidSeq.reset();
    acidHeldCount = 0;
}

//==============================================================================
// Acid (mode 5) note routing. With the sequencer on (arpOn) the player holds a
// root note and the 16-step pattern transposes from it; with it off, live mono
// play glides (legato) and MIDI velocity > 100 accents.
void MultiSynthDSP::acidNoteOn(int note, float velocity01) noexcept
{
    if (p(pArpOn) > 0.5f) { acidSeq.noteOn(note); return; }
    const float vel = clamp01(velocity01);
    // Remove any existing entry for this note (re-press moves it to the top).
    int w = 0;
    for (int r = 0; r < acidHeldCount; ++r)
        if (acidHeld[r].note != note) acidHeld[w++] = acidHeld[r];
    acidHeldCount = w;
    const bool slide = acidHeldCount > 0;   // a note was already sounding -> legato glide
    // Push on top, dropping the oldest if the stack is full.
    if (acidHeldCount >= 16)
    {
        for (int r = 1; r < 16; ++r) acidHeld[r - 1] = acidHeld[r];
        acidHeldCount = 15;
    }
    acidHeld[acidHeldCount++] = { note, vel };
    const bool accent = vel > kAcidAccentVel;
    acidVoice.noteOn(midiToHz((float)note), accent, slide, vel);
}

void MultiSynthDSP::acidNoteOff(int note) noexcept
{
    if (p(pArpOn) > 0.5f) { acidSeq.noteOff(note); return; }
    if (acidHeldCount <= 0) return;
    const bool wasTop = acidHeld[acidHeldCount - 1].note == note;
    // Remove the entry for this note (ignore if not held).
    int w = 0;
    bool removed = false;
    for (int r = 0; r < acidHeldCount; ++r)
    {
        if (!removed && acidHeld[r].note == note) { removed = true; continue; }
        acidHeld[w++] = acidHeld[r];
    }
    if (!removed) return;
    acidHeldCount = w;
    if (wasTop && acidHeldCount > 0)
    {
        // Return to the now-top held note (legato via slide-tie, no retrigger).
        const HeldNote& t = acidHeld[acidHeldCount - 1];
        acidVoice.noteOn(midiToHz((float)t.note), t.vel > kAcidAccentVel, /*slide=*/true, t.vel);
    }
    else if (acidHeldCount == 0)
    {
        acidVoice.noteOff();
    }
}

//==============================================================================
void MultiSynthDSP::snapshotParameters(int nSamples) noexcept
{
    // Consume the explicit program-change signal first (see notifyProgramChange).
    // Read here, applied at the bottom once every target for this block is known.
    const bool explicitSnap = pendingSnap.exchange(false, std::memory_order_acquire);

    VoiceParameters& vp = voiceParams;

    // --- mode-switch crossfade (rationale in the header) ----------------------
    // The requested mode is held off while the voice path fades out, and commits
    // on the first block that starts with the fade at zero. Everything below this
    // point derives from vp.mode, so holding it steady is all it takes to keep the
    // outgoing engine intact and rendering for the length of the fade.
    const SynthMode requestedMode = (SynthMode)clampi((int)p(pMode), 0, 5);
    if (!haveLastSnap)
    {
        // First snapshot after prepare/reset: adopt outright, there is nothing
        // sounding to fade and the host has already set the mode it wants.
        activeMode = requestedMode;
        modeSwitchPending = false;
        modeFade = 1.0f;
    }
    else if (modeSwitchPending && modeFade <= 0.0f)
    {
        activeMode = requestedMode;     // muted: commit (cleanup runs further down)
        modeSwitchPending = false;
    }
    else
    {
        // Starts a fade on a new request, and cancels one whose request was
        // reverted before it finished.
        modeSwitchPending = (requestedMode != activeMode);
    }
    vp.mode = activeMode;

    int modeVoices = 6;
    switch (vp.mode)
    {
        case SynthMode::Cosmos:  modeVoices = 6; break;
        case SynthMode::Oracle:  modeVoices = 5; break;
        case SynthMode::Mono:    modeVoices = 1; break;
        case SynthMode::Modular: modeVoices = 2; break;
        case SynthMode::Prism:   modeVoices = 8; break;
        case SynthMode::Acid:    modeVoices = 1; break;
    }
    voices.setModeVoices(modeVoices);
    voices.setUnison(clampi((int)p(pUnisonVoices), 1, kMaxUnison));
    // (The headroom trim that goes with this budget is snapped-or-glided at the
    // bottom of this function, with the rest of the smoothed controls.)

    vp.osc1Wave = (Waveform)clampi((int)p(pOsc1Wave), 0, 5);
    vp.osc1Detune = p(pOsc1Detune);
    vp.osc1PulseWidth = p(pOsc1PW);
    vp.osc1Level = p(pOsc1Level);
    vp.osc2Wave = (Waveform)clampi((int)p(pOsc2Wave), 0, 5);
    vp.osc2Detune = p(pOsc2Detune);
    vp.osc2PulseWidth = p(pOsc2PW);
    vp.osc2Level = p(pOsc2Level);
    vp.osc2SemiOffset = (int)p(pOsc2Semi);
    vp.osc3Wave = (Waveform)clampi((int)p(pOsc3Wave), 0, 5);
    vp.osc3Level = p(pOsc3Level);
    vp.subLevel = p(pSubLevel);
    vp.subWave = ((int)p(pSubWave) == 0) ? Waveform::Square : Waveform::Sine;
    vp.noiseLevel = p(pNoiseLevel);
    vp.shRate = p(pShRate);

    vp.filterCutoff = p(pFilterCutoff);
    vp.filterResonance = p(pFilterRes);
    vp.filterHPCutoff = p(pFilterHP);
    vp.filterEnvAmount = p(pFilterEnvAmt);

    vp.ampAttack = p(pAmpA); vp.ampDecay = p(pAmpD); vp.ampSustain = p(pAmpS); vp.ampRelease = p(pAmpR);
    vp.ampCurve = (EnvelopeCurve)clampi((int)p(pAmpCurve), 0, 3);
    vp.filtAttack = p(pFiltA); vp.filtDecay = p(pFiltD); vp.filtSustain = p(pFiltS); vp.filtRelease = p(pFiltR);
    vp.filtCurve = (EnvelopeCurve)clampi((int)p(pFiltCurve), 0, 3);

    vp.crossMod = p(pCrossMod);
    vp.ringMod = p(pRingMod);
    vp.hardSync = p(pHardSync) > 0.5f;
    vp.fmAmount = p(pFMAmount);
    vp.polyModFEnvOscA = p(pPmFenvOscA);
    vp.polyModFEnvFilt = p(pPmFenvFilt);
    vp.polyModOscBOscA = p(pPmOscBOscA);
    vp.polyModOscBPWM = p(pPmOscBPWM);

    // Prism (4-op FM) params — op blocks are 9 contiguous fields each.
    vp.prismAlgo = clampi((int)p(pPrismAlgo), 0, 7);
    vp.prismFB   = p(pPrismFB);
    for (int i = 0; i < 4; ++i)
    {
        const int b = pOp1Ratio + i * kOpParamStride;
        auto& o = vp.op[i];
        o.ratio    = p((Param)(b + 0));
        o.fine     = p((Param)(b + 1));
        o.level    = p((Param)(b + 2));
        o.vel      = p((Param)(b + 3));
        o.keyScale = p((Param)(b + 4));
        o.a        = p((Param)(b + 5));
        o.d        = p((Param)(b + 6));
        o.s        = p((Param)(b + 7));
        o.r        = p((Param)(b + 8));
    }

    vp.portamentoTime = p(pPortaTime);
    vp.legatoMode = p(pLegato) > 0.5f;
    vp.glideMode = (int)p(pGlideMode);
    vp.analogAmount = p(pAnalogAmt);
    vp.velocitySensitivity = p(pVelSens);
    vp.velocityCurve = (int)p(pVelCurve);

    vp.unisonDetune = p(pUnisonDetune);
    vp.unisonSpread = p(pUnisonSpread);

    const float pbRange = p(pPbRange);
    const float pbNorm = pitchBendNorm.load(std::memory_order_relaxed);
    vp.pitchBendSemis = pbNorm * pbRange;      // oscillator base frequency
    vp.pitchBendNorm  = pbNorm;                // normalized ±1 mod source (C2)
    vp.masterTuneSemis = p(pMasterTune) / 100.0f;
    vp.modWheel = modWheelValue.load(std::memory_order_relaxed);
    vp.aftertouch = aftertouchValue.load(std::memory_order_relaxed);

    // --- Arp ---
    arpEnabled = p(pArpOn) > 0.5f;
    arp.setEnabled(arpEnabled);
    arp.setMode((ArpMode)clampi((int)p(pArpMode), 0, 6));
    arp.setOctaveRange((int)p(pArpOctave));
    arp.setRate((ArpRateDivision)clampi((int)p(pArpRate), 0, (int)ArpRateDivision::NumDivisions - 1));
    arp.setGate(p(pArpGate));
    arp.setSwing(p(pArpSwing));
    const bool latchOn = p(pArpLatch) > 0.5f;
    if (lastArpLatch && !latchOn)
    {
        // Latch just turned off: prune latched notes that are no longer held —
        // by a key (heldNotes) OR by the sustain pedal, which feeds the arp
        // held-set exactly like a key. Notes still held keep playing; with none
        // held the arp/acid pattern stops cleanly.
        const uint64_t lo = heldNotesLo.load(std::memory_order_relaxed) | sustainedLo;
        const uint64_t hi = heldNotesHi.load(std::memory_order_relaxed) | sustainedHi;
        arp.retainHeld(lo, hi);
        acidSeq.retainHeld(lo, hi);
    }
    lastArpLatch = latchOn;
    arp.setLatch(latchOn);
    arp.setVelocityMode((ArpVelocityMode)clampi((int)p(pArpVelMode), 0, 2));
    arp.setFixedVelocity((int)p(pArpFixedVel));
    for (int i = 0; i < 16; ++i) arp.setStepActive(i, p((Param)(pArpStep0 + i)) > 0.5f);

    // --- Effects ---
    // The mix / drive / depth / feedback values are read into locals because they
    // double as smoothing witnesses at the bottom of this function; reading each
    // atomic exactly once keeps the witness in step with the target it came from.
    // setMix() only sets the smoother TARGET -- the mod-matrix EffectsMix scale is
    // applied separately per sample via effects.setMixMod().
    effects.setMixMod(1.0f);   // unmodulated unless the render loop says otherwise

    const float driveAmt = p(pDriveAmt), driveMix = p(pDriveMix);
    effects.drive.setEnabled(p(pDriveOn) > 0.5f);
    effects.drive.setType((DriveType)clampi((int)p(pDriveType), 0, 2));
    effects.drive.setDrive(driveAmt);
    effects.drive.setMix(driveMix);

    const float chorusDepth = p(pChorusDepth), chorusMix = p(pChorusMix);
    effects.chorus.setEnabled(p(pChorusOn) > 0.5f);
    effects.chorus.setRate(p(pChorusRate));
    effects.chorus.setDepth(chorusDepth);
    effects.chorus.setMix(chorusMix);

    effects.delay.setEnabled(p(pDelayOn) > 0.5f);
    effects.delay.setTempoSync(p(pDelaySync) > 0.5f);
    effects.delay.setTimeMs(p(pDelayTime));
    effects.delay.setSyncDivision((ArpRateDivision)clampi((int)p(pDelayDiv), 0, (int)ArpRateDivision::NumDivisions - 1));
    const float delayFB = p(pDelayFB), delayMix = p(pDelayMix);
    effects.delay.setFeedback(delayFB);
    effects.delay.setMix(delayMix);
    effects.delay.setPingPong(p(pDelayPP) > 0.5f);
    effects.delay.setTapeCharacter(p(pDelayTape) > 0.5f);

    effects.reverb.setEnabled(p(pReverbOn) > 0.5f);
    effects.reverb.setSize(p(pReverbSize));
    effects.reverb.setDecay(p(pReverbDecay));
    effects.reverb.setDamping(p(pReverbDamp));
    const float reverbMix = p(pReverbMix);
    effects.reverb.setMix(reverbMix);
    effects.reverb.setPreDelay(p(pReverbPD));

    cosmosChorus.setMode(vp.mode == SynthMode::Cosmos
        ? (CosmosChorusMode)clampi((int)p(pCosmosChorus), 0, 3) : CosmosChorusMode::Off);

    const bool isModular = (vp.mode == SynthMode::Modular);
    effects.springReverb.setEnabled(isModular);
    if (isModular) effects.springReverb.setMix(0.15f);

    // --- LFOs (tempo sync: rate scaling + host phase lock) ---
    // Sync has two halves. The free-run RATE is scaled by bpm/120, and the PHASE
    // is locked to the host song position whenever the transport is playing
    // (LFO::setSongBeat) — the rate scaling is what the stopped-transport
    // fallback runs on.
    //
    // The two halves have to agree, and they do by construction. At bpm the
    // synced LFO runs rate*bpm/120 cycles per second against a grid of bpm/60
    // beats per second, so one cycle is 2/rate BEATS at every tempo: the rate
    // knob's musical meaning (2.0 = a quarter note, 1.0 = a half note) is
    // tempo-invariant, and that is exactly what the phase lock reproduces.
    // beatsPerCycle comes from the UNSCALED knob for that reason.
    const double bpm = hostBpm.load(std::memory_order_relaxed);
    const bool  lfo1Sync = p(pLfo1Sync) > 0.5f, lfo2Sync = p(pLfo2Sync) > 0.5f;
    const float lfo1Knob = p(pLfo1Rate), lfo2Knob = p(pLfo2Rate);
    float lfo1Rate = lfo1Knob; float lfo2Rate = lfo2Knob;
    if (lfo1Sync && bpm > 0.0) lfo1Rate *= (float)(bpm / 120.0);
    if (lfo2Sync && bpm > 0.0) lfo2Rate *= (float)(bpm / 120.0);
    const float lfo1Beats = 2.0f / maxf(1.0e-4f, lfo1Knob);
    const float lfo2Beats = 2.0f / maxf(1.0e-4f, lfo2Knob);
    const LFOShape lfo1Shape = (LFOShape)clampi((int)p(pLfo1Shape), 0, 4);
    const LFOShape lfo2Shape = (LFOShape)clampi((int)p(pLfo2Shape), 0, 4);
    const float lfo1Fade = p(pLfo1Fade), lfo2Fade = p(pLfo2Fade);
    const bool prismMode = (vp.mode == SynthMode::Prism);
    for (int v = 0; v < kMaxPolyphony; ++v)
    {
        voices.getVoice(v)->setLFO1Params(lfo1Shape, lfo1Rate, lfo1Fade, lfo1Sync, lfo1Beats);
        voices.getVoice(v)->setLFO2Params(lfo2Shape, lfo2Rate, lfo2Fade, lfo2Sync, lfo2Beats);
        if (prismMode) voices.getVoice(v)->updateFMParams(vp);
    }

    // --- Mod matrix ---
    hasEffectsMixRouting = false;
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto& slot = modMatrix.getSlot(i);
        slot.source = (ModSource)clampi((int)p((Param)(pModSrc0 + i)), 0, kNumModSources - 1);
        slot.destination = (ModDest)clampi((int)p((Param)(pModDst0 + i)), 0, kNumModDests - 1);
        slot.amount = p((Param)(pModAmt0 + i));
        if (slot.destination == ModDest::EffectsMix && slot.source != ModSource::None && slot.amount != 0.0f)
            hasEffectsMixRouting = true;
    }

    // --- Acid (mode 5): mono voice + pattern sequencer ---
    // Shared-knob mapping (documented for the UI/manual):
    //   filterCutoff -> cutoff, filterRes -> resonance, filterEnvAmt -> envMod
    //   (magnitude; acid sweep is always upward), ampD -> decay, ampS -> sustain,
    //   osc1Wave -> waveform, osc1PW -> pulse width, driveAmt -> filter input
    //   drive (mapped 1 + 4*driveAmt so the 0..1 knob spans clean..screaming).
    // Acid globals: acidAccentAmt -> accent depth, acidSlideTime -> glide time.
    if (vp.mode == SynthMode::Acid)
    {
        acidVoice.setWaveform(vp.osc1Wave);
        acidVoice.setPulseWidth(vp.osc1PulseWidth);
        // cutoff / resonance / envMod are pushed per host sample from the shared
        // smoothers in processBlock — the acid voice renders outside the poly
        // path, so taking them from the block snapshot here would leave mode 5 as
        // the one voice path that still steps once per block.
        //
        // The rest stay on the block snapshot deliberately. Decay, sustain and
        // slide time are envelope/glide TIME constants: stepping one bends the
        // envelope's slope, it does not put a discontinuity in the signal. Drive
        // does step the filter input gain, but measured with the zipper probe it
        // sits at -91 dB absolute (41 dB over a -132 dB floor) because the acid
        // filter's own saturation absorbs it — below every threshold in the gate
        // and far below audibility, so it is not worth a smoother.
        acidVoice.setDecay(vp.ampDecay);
        acidVoice.setSustain(vp.ampSustain);
        acidVoice.setDrive(1.0f + 4.0f * clamp01(driveAmt));
        acidVoice.setAccentAmount(clamp01(p(pAcidAccentAmt)));
        acidVoice.setSlideTime(p(pAcidSlideTime));

        // Sequencer clocks off the arp controls; per-step rows are the arpStep
        // mutes (on/off) plus the acid-only seqPitch/Accent/Slide lanes.
        acidSeqEnabled = arpEnabled;
        acidSeq.setEnabled(arpEnabled);
        acidSeq.setRate((ArpRateDivision)clampi((int)p(pArpRate), 0, (int)ArpRateDivision::NumDivisions - 1));
        acidSeq.setGate(p(pArpGate));
        acidSeq.setSwing(p(pArpSwing));
        acidSeq.setLatch(p(pArpLatch) > 0.5f);
        for (int i = 0; i < 16; ++i)
            acidSeq.setStep(i, p((Param)(pArpStep0 + i)) > 0.5f,
                            (int)p((Param)(pSeqPitch0 + i)),
                            p((Param)(pSeqAccent0 + i)) > 0.5f,
                            p((Param)(pSeqSlide0 + i)) > 0.5f);
    }
    else
    {
        acidSeqEnabled = false;
    }

    // --- engine-level cached controls ---
    // +8 dB output makeup: commercial soft synths ship factory presets peaking
    // around -6..0 dBFS (chords ~-3); without makeup this engine's fleet median
    // was -14 dBFS and users heard it as "way quieter than Diva". The makeup is
    // applied with masterGain at the FINAL output (post-FX), so it scales level
    // only — drive/delay/reverb tone is untouched. Hot presets are trimmed via
    // their masterVol rows so the audit ceiling (peak <= -1 dBFS) still holds.
    constexpr float kOutputMakeup = 2.51188643f; // +8 dB
    masterGain = kOutputMakeup * std::pow(10.0f, p(pMasterVol) / 20.0f);
    masterPan = p(pMasterPan);
    stereoWidth = p(pStereoWidth);
    vintage = p(pVintage);

    // --- Preset/mode transition handling (stuck-note fix) ---------------------
    // A factory-preset load can change the mode, or disable the arp / acid
    // sequencer, WHILE notes are held. The note-routing path then changes out
    // from under the sounding voice, so its key-up never reaches it and it drones
    // forever. Detect the transition here and release the stranded voice(s).
    // For an arp / sequencer disable that is a plain release, so envelopes enter
    // Release and tails ring out with no click. Browsing presets WITHIN the same
    // mode is untouched — held notes stay seamless, which is the correct, desired
    // behaviour.
    if (haveLastSnap)
    {
        if (vp.mode != lastSnapMode)
        {
            // Mode changed: every note started under the old mode is unreachable.
            // This branch only runs on the block that COMMITS a mode switch, i.e.
            // with the voice path already faded to zero gain, so the outgoing
            // engines are RESET rather than released — releasing them would ring the
            // old notes back IN behind the fade-in, rendered through the new mode's
            // oscillator section. Their EFFECT tails are unaffected either way: the
            // mode fade sits ahead of the effects chain.
            voices.reset();
            acidVoice.reset();
            acidHeldCount = 0;   // drop the live-acid held-note stack
            // Pedal-captured note-offs belong to voices that no longer exist; the
            // pedal itself stays down (it is a physical control, not note state), so
            // playing into the new mode with the pedal still held sustains normally.
            clearSustained();
            arp.clearLatch();    // drop latched held notes (else they re-trigger
            arp.reset();         // in the new mode and drone; reset() keeps them
            acidSeq.clearLatch();// while latch is on)
            acidSeq.reset();
        }
        else
        {
            // Same mode: only the specific subsystem whose routing moved is stranded.
            // The arp toggle strands voices in BOTH directions, because noteOn and
            // noteOff each pick their path from the CURRENT arpOn: enabling it while
            // notes sound sends their key-ups to Arpeggiator::noteOff, which never
            // reaches the poly voices that are actually sounding (measured -14.9 dB
            // droning forever, with or without the sustain pedal), and disabling it
            // strands the arp-triggered voice the same way. Either edge releases.
            if (lastArpEnabled != arpEnabled && vp.mode != SynthMode::Acid)
            {
                voices.allNotesOff();
                // The captured note-offs belong to the voices just released, and
                // their routing has moved too — drop them like a mode switch does.
                clearSustained();
            }
            if (lastAcidSeqEnabled && !acidSeqEnabled)
                acidVoice.noteOff();           // release the gated acid voice
        }
    }
    haveLastSnap = true;
    lastSnapMode = vp.mode;
    lastArpEnabled = arpEnabled;
    lastAcidSeqEnabled = acidSeqEnabled;

    // --- Parameter smoothing targets -----------------------------------------
    // Everything above wrote this block's TARGETS. The render loop advances the
    // smoothers once per host sample so automation and knob drags glide instead
    // of stepping 94 times a second (see kParamSmoothTau in the header).
    // A program change (or the first block after prepare/reset) must land
    // instantly, so decide first and snap rather than glide. The explicit signal
    // is authoritative; the heuristic only catches hosts that replay a stored
    // patch as raw parameter writes. detectBulkParamChange() is called
    // unconditionally because it also carries the witness history forward.
    //
    // Witnesses come from the values already read above, never from a fresh
    // atomic load: a re-read could pick up a host write that landed after the
    // target was taken, and that jump would then be consumed without ever being
    // acted on. Slots are named so the fill cannot drift out of step with the
    // threshold table.
    float witnessNow[kNumSmoothWitness];
    witnessNow[wMasterGain]  = masterGain;
    witnessNow[wMasterPan]   = masterPan;
    witnessNow[wStereoWidth] = stereoWidth;
    witnessNow[wCutoff]      = vp.filterCutoff;
    witnessNow[wHPCutoff]    = vp.filterHPCutoff;
    witnessNow[wRes]         = vp.filterResonance;
    witnessNow[wOsc1Level]   = vp.osc1Level;
    witnessNow[wOsc2Level]   = vp.osc2Level;
    witnessNow[wOsc3Level]   = vp.osc3Level;
    witnessNow[wSubLevel]    = vp.subLevel;
    witnessNow[wNoiseLevel]  = vp.noiseLevel;
    witnessNow[wDriveAmt]    = driveAmt;
    witnessNow[wDriveMix]    = driveMix;
    witnessNow[wChorusDepth] = chorusDepth;
    witnessNow[wChorusMix]   = chorusMix;
    witnessNow[wDelayFB]     = delayFB;
    witnessNow[wDelayMix]    = delayMix;
    witnessNow[wReverbMix]   = reverbMix;

    const bool bulkSnap = detectBulkParamChange(witnessNow, (float)(nSamples / hostRate));
    smoothSnap = explicitSnap || bulkSnap;

    setSmoothTarget(smGain,     masterGain);
    setSmoothTarget(smPanAngle, (masterPan + 1.0f) * 0.25f * kPi);
    setSmoothTarget(smWidth,    2.0f * stereoWidth);

    // Filter section. The values written into vp above are this block's targets;
    // the render loop overwrites the vp fields with the glided values each host
    // sample, so every voice (and every oversampled sub-sample) sees the same
    // smooth trajectory.
    //
    // The two cutoffs are smoothed in LOG2(Hz), not Hz. A one-pole on linear Hz
    // is violently asymmetric because pitch is logarithmic: 12 kHz -> 200 Hz
    // takes 32.6 ms just to come within an octave of the target, while
    // 200 Hz -> 12 kHz crosses the entire low end in 0.14 ms. In log2 the glide
    // takes the same time per octave in both directions, which is what the ear
    // expects from a filter sweep. Cost is one exp2() per host sample per cutoff
    // -- cheap next to the pow(2,x) the voice already runs per voice per
    // oversampled sample for its envelope modulation.
    setSmoothTarget(smCutoff,       std::log2(maxf(kMinSmoothHz, vp.filterCutoff)));
    setSmoothTarget(smHPCutoff,     std::log2(maxf(kMinSmoothHz, vp.filterHPCutoff)));
    setSmoothTarget(smRes,          vp.filterResonance);
    setSmoothTarget(smFilterEnvAmt, vp.filterEnvAmount);

    // Oscillator mix levels. These scale the summed oscillator output directly,
    // so a stepped level is a plain amplitude discontinuity -- the loudest and
    // most obvious form of zipper in the voice.
    // The per-voice headroom trim 2/sqrt(effectivePoly()) is a smoothed control
    // like any other: it glides so a mid-note unison or voice-budget change cannot
    // step the level of every surviving voice, and it LANDS on a preset load (and
    // on the first snapshot after prepare/reset, where the budget is simply being
    // established). It lives in the allocator rather than in a SmoothedValue here
    // because only the allocator knows the effective polyphony.
    if (smoothSnap) voices.snapVoiceGain();

    setSmoothTarget(smOsc1Level,  vp.osc1Level);
    setSmoothTarget(smOsc2Level,  vp.osc2Level);
    setSmoothTarget(smOsc3Level,  vp.osc3Level);
    setSmoothTarget(smSubLevel,   vp.subLevel);
    setSmoothTarget(smNoiseLevel, vp.noiseLevel);

    // The effect drive / wet mixes smooth themselves (Effects.hpp); they only
    // need to be told when a snapshot is a preset load rather than a knob move.
    if (smoothSnap) effects.snapSmoothing();
}

//==============================================================================
bool MultiSynthDSP::detectBulkParamChange(const float* now, float blockSeconds) noexcept
{
    // The thresholds say how far a control can move in a 10 ms block, so scale
    // them by the ACTUAL block length. Without this the classifier gets steadily
    // more trigger-happy as the host buffer grows — at 4096 frames a five-lane
    // macro sweep moves ~28% of range per block and would read as a preset load,
    // snapping every block and putting the zipper straight back — and
    // hypersensitive on the short sub-blocks the shell creates when it splits at
    // MIDI events. Clamped at both ends so neither extreme runs away; the bias is
    // deliberately toward gliding, because a missed snap is inaudible while a
    // false snap clicks.
    const float scale = clampf(blockSeconds / 0.010f, 0.25f, 4.0f);

    int large = 0;
    const bool first = !haveWitness;
    for (int i = 0; i < kNumSmoothWitness; ++i)
    {
        const SmoothWitness& w = kSmoothWitness[i];
        const float v = now[i];
        const float prev = lastWitness[i];
        lastWitness[i] = v;
        if (first) continue;
        // Frequencies and gains are compared as ratios — 500 -> 2000 Hz is a
        // preset-sized move, 15000 -> 16500 Hz is not, and a linear delta cannot
        // tell them apart.
        const float d = w.logScale
            ? std::abs(std::log2(maxf(1.0e-6f, v) / maxf(1.0e-6f, prev)))
            : std::abs(v - prev);
        if (d > w.jump * scale) ++large;
    }
    haveWitness = true;
    return first || large >= kBulkSnapCount;
}

//==============================================================================
void MultiSynthDSP::processBlock(float* outL, float* outR, int nSamples) noexcept
{
    duskaudio::ScopedFlushDenormals noDenormals;
    if (nSamples <= 0) return;

    // Apply a pending oversampling change at block start (never mid-block).
    const int desiredOs = ((int)p(pOversampling) == 0) ? 1 : ((int)p(pOversampling) == 1) ? 2 : 4;
    if (desiredOs != osFactor) applyOsFactor(desiredOs);

    const double bpm = hostBpm.load(std::memory_order_relaxed);
    const bool playing = transportPlaying.load(std::memory_order_relaxed);

    // Host phase-lock: when the transport is playing AND a valid song position is
    // available, derive the arp/acid step clock statelessly from song position in
    // beats. The block starts at songPosBeats and the cursor advances one host
    // sample at a time (the arp/acid advance per host sample). When not locked,
    // the engines keep their free-run counter clock.
    const bool   hostLocked     = playing && songPosValid.load(std::memory_order_relaxed);
    double       songBeat       = songPosBeats.load(std::memory_order_relaxed);
    const double beatsPerSample = (bpm > 0.0 ? bpm : 120.0) / (60.0 * hostRate);

    auto softLimit = [](float x) noexcept -> float {
        if (isBad(x)) return 0.0f;
        const float ax = std::abs(x);
        if (ax <= 0.9f) return x;
        const float limited = 0.9f + 0.1f * std::tanh((ax - 0.9f) * 10.0f);
        return x >= 0.0f ? limited : -limited;
    };

    // The block is rendered in SEGMENTS. Normally there is exactly one, but a
    // pending mode switch commits on the sample its crossfade reaches zero, and the
    // commit has to be followed by a fresh snapshot — the new mode's voice budget,
    // oscillator section, chorus and acid wiring all come from snapshotParameters.
    // Cutting the segment there is what keeps the transition buffer-size
    // INVARIANT. Waiting for the next block boundary instead left the voice path
    // muted for (blockSize - fade) samples: 0 at 512 frames, but 73 ms at 4096,
    // during which a note the player pressed was routed to the outgoing mode and
    // then wiped by the commit — the key simply did nothing. songBeat is carried
    // across segments so the host-locked arp/acid clock does not restart.
    //
    // At most two segments run per block: after a commit the requested mode and the
    // active mode agree, so no second cut can be scheduled.
    for (int done = 0; done < nSamples; )
    {
        const int remain = nSamples - done;
        snapshotParameters(remain);

        int segment = remain;
        if (modeSwitchPending)
        {
            // snapshotParameters only leaves a switch pending with modeFade > 0, so
            // this is always at least one sample.
            const int toZero = (int)std::ceil((double)modeFade / (double)modeFadeStep);
            if (toZero > 0 && toZero < segment) segment = toZero;
        }

        const bool acidMode = (voiceParams.mode == SynthMode::Acid);
        const int segEnd = done + segment;

        for (int i = done; i < segEnd; ++i)
        {
            // Advance the voice-parameter smoothers once per HOST sample and publish
            // them into the shared snapshot the voices render from. Done for both the
            // poly and the acid path so the smoother state stays valid across a mode
            // switch (the acid voice takes its cutoff/res at snapshot time instead).
            voiceParams.filterCutoff    = std::exp2(smCutoff.next());   // smoothed in log2(Hz)
            voiceParams.filterHPCutoff  = std::exp2(smHPCutoff.next()); // ...so sweeps are symmetric
            voiceParams.filterResonance = smRes.next();
            voiceParams.filterEnvAmount = smFilterEnvAmt.next();
            voiceParams.osc1Level       = smOsc1Level.next();
            voiceParams.osc2Level       = smOsc2Level.next();
            voiceParams.osc3Level       = smOsc3Level.next();
            voiceParams.subLevel        = smSubLevel.next();
            voiceParams.noiseLevel      = smNoiseLevel.next();

            // Render osFactor internal samples, then decimate to host rate (fix #1).
            float iL[4], iR[4];
            float fxAccum = 0.0f;

            if (acidMode)
            {
                // Mono acid path: the sequencer (when enabled) or live legato play
                // drives a single AcidVoice; the poly allocator/arp are bypassed.
                // Feed it the same smoothed filter values the poly voices read.
                acidVoice.setCutoff(voiceParams.filterCutoff);
                acidVoice.setResonance(voiceParams.filterResonance);
                acidVoice.setEnvMod(clampf(std::abs(voiceParams.filterEnvAmount), 0.0f, 1.0f));
                if (acidSeqEnabled)
                {
                    const auto ev = acidSeq.advanceSample(bpm, playing, songBeat, hostLocked);
                    if (ev.noteOff) acidVoice.noteOff();
                    if (ev.noteOn)  acidVoice.noteOn(ev.freq, ev.accent, ev.slide, 1.0f);
                }
                for (int os = 0; os < osFactor; ++os)
                {
                    const float m = acidVoice.processSample();
                    iL[os] = m; iR[os] = m;
                }
            }
            else
            {
                // Host-locked LFO phase: push the song-beat cursor once per HOST
                // sample, before anything consumes it. Only the LFOs with sync on
                // act on it (Envelope.hpp). It comes BEFORE the arp because the
                // arp's note-ons retrigger LFOs, and a retriggered LFO reads the
                // grid phase this call establishes.
                //
                // The acid branch does not push: it renders a mono voice with no
                // LFOs of its own and leaves the poly voices silent. Their lock
                // state simply goes stale for the duration, which the first push
                // after a switch back absorbs — a stale grid phase reads as one
                // large slew step, and a large step is what clears the offset.
                voices.setLFOSongBeat(songBeat, beatsPerSample, hostLocked);

                // Arp advances at host rate; triggers its own generated notes.
                if (arpEnabled)
                {
                    const auto ev = arp.advanceSample(bpm, playing, songBeat, hostLocked);
                    if (ev.noteOffValid) voices.noteOff(ev.offNote);
                    if (ev.noteOnValid)  voices.noteOn(ev.onNote, (float)ev.onVel / 127.0f, voiceParams);
                }
                for (int os = 0; os < osFactor; ++os)
                {
                    float l, r, fx;
                    voices.renderInternalSample(voiceParams, modMatrix, l, r, fx);
                    iL[os] = l; iR[os] = r; fxAccum += fx;
                }
            }

            float sL = decimL.process(iL);
            float sR = decimR.process(iR);
            const float fxMod = fxAccum / (float)osFactor;

            // Mode-switch crossfade (see snapshotParameters). Applied to the VOICE path
            // only, ahead of the effects, so delay and reverb tails ring on through the
            // transition instead of ducking with it. Smoothstep rather than the bare
            // linear ramp so the fade has no slope corner at either end; it is exactly
            // 1.0 — and so a bit-for-bit no-op — whenever no switch is in flight.
            modeFade = clampf(modeFade + (modeSwitchPending ? -modeFadeStep : modeFadeStep),
                              0.0f, 1.0f);
            const float mf = modeFade * modeFade * (3.0f - 2.0f * modeFade);
            sL *= mf; sR *= mf;

            constexpr float kVoiceGain = 0.7f;
            sL *= kVoiceGain; sR *= kVoiceGain;

            cosmosChorus.process(sL, sR);

            // EffectsMix mod (fix #5): scale effect wet mixes by the mean per-voice
            // routing amount. Only pays per-sample setter cost when routed.
            //
            // This is a SCALE on the smoothed mix, not a new smoother target. Pushing
            // the modulation through the 8 ms one-pole would lowpass an LFO or
            // envelope that is meant to arrive intact, and would change the rendered
            // output of every patch that uses the routing -- de-zippering must not do
            // that. snapshotParameters() resets the scale to 1.0 each block, so the
            // unrouted case needs nothing here.
            if (hasEffectsMixRouting)
                effects.setMixMod(clampf(1.0f + fxMod, 0.0f, 2.0f));

            effects.process(sL, sR, bpm);

            // Vintage: HF rolloff + tiny noise floor (member PRNG, no rand()).
            if (vintage > 0.01f)
            {
                const float coeff = 1.0f - vintage * 0.3f;
                sL = sL * coeff + prevVintageL * (1.0f - coeff);
                sR = sR * coeff + prevVintageR * (1.0f - coeff);
                prevVintageL = sL; prevVintageR = sR;
                const float noise = vintageRng.nextBipolar() * vintage * 0.001f;
                sL += noise; sR += noise;
            }

            // Master gain + pan (per-sample smoothed; see kParamSmoothTau). The pan
            // ANGLE is glided and cos/sin taken here, so the pan law stays exactly
            // constant-power throughout the move rather than only at its endpoints.
            const float gain = smGain.next();
            const float ang  = smPanAngle.next();
            sL *= gain * std::cos(ang);
            sR *= gain * std::sin(ang);

            // Stereo width (mid/side). The 0..1 param maps to a 0..2 side factor so
            // the 0.5 DEFAULT is unity (image preserved): 0 = mono, 0.5 = as
            // rendered, 1 = double-width. The old side*width mapping silently
            // halved every preset's stereo image at the default setting.
            const float mid = (sL + sR) * 0.5f;
            const float side = (sL - sR) * 0.5f;
            const float widthFactor = smWidth.next();
            sL = mid + side * widthFactor;
            sR = mid - side * widthFactor;

            // Output DC blocker (reverb combs / filter nonlinearity leave a little DC).
            sL = dcBlockL.process(sL);
            sR = dcBlockR.process(sR);

            sL = softLimit(sL);
            sR = softLimit(sR);

            outL[i] = sL;
            if (outR) outR[i] = sR;

            // Scope ring (mono sum).
            const int wp = scopeWritePos.load(std::memory_order_relaxed);
            scope[(size_t)wp].store((sL + (outR ? sR : sL)) * 0.5f, std::memory_order_relaxed);
            scopeWritePos.store((wp + 1) % kScopeSize, std::memory_order_relaxed);
            const int sc = scopeCount.load(std::memory_order_relaxed);
            if (sc < kScopeSize) scopeCount.store(sc + 1, std::memory_order_relaxed);

            // Peak metering with release.
            const float aL = std::abs(sL), aR = std::abs(sR);
            meterL = aL > meterL ? aL : meterL * meterDecay;
            meterR = aR > meterR ? aR : meterR * meterDecay;

            // Advance the host-locked song-beat cursor one host sample.
            songBeat += beatsPerSample;
        }

        done = segEnd;
    }

    if (voiceParams.mode == SynthMode::Acid && acidSeqEnabled)
    {
        arpStep.store(acidSeq.getCurrentStep(), std::memory_order_relaxed);
        arpTotalSteps.store(16, std::memory_order_relaxed);
    }
    else if (arpEnabled)
    {
        arpStep.store(arp.getCurrentStep(), std::memory_order_relaxed);
        arpTotalSteps.store(arp.getTotalSteps(), std::memory_order_relaxed);
    }
    else
    {
        // Neither the acid sequencer nor the arp is running: publish the idle
        // sentinel so the observable matches the "-1 when idle" bridge contract
        // and the UI step playhead clears (C5). -1 highlights no step.
        arpStep.store(-1, std::memory_order_relaxed);
        arpTotalSteps.store(0, std::memory_order_relaxed);
    }

    auto toDb = [](float lin) noexcept { return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -60.0f; };
    outLevelL.store(toDb(meterL), std::memory_order_relaxed);
    outLevelR.store(toDb(meterR), std::memory_order_relaxed);
}

} // namespace msynth
