// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// MultiSynthDSP.cpp — top engine implementation (see MultiSynthDSP.hpp).

#include "MultiSynthDSP.hpp"
#include "DuskDenormals.hpp"

#include <cstdlib>
#include <cstring>

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

char stepScratch[8]; // not thread-shared: only used at construction / lookup
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
    (void)stepScratch;
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
    for (auto& s : scope) s.store(0.0f, std::memory_order_relaxed);
    scopeWritePos.store(0, std::memory_order_relaxed);
    scopeCount.store(0, std::memory_order_relaxed);
    heldNotesLo.store(0, std::memory_order_relaxed);
    heldNotesHi.store(0, std::memory_order_relaxed);
}

//==============================================================================
void MultiSynthDSP::noteOn(int note, float velocity01) noexcept
{
    if (note >= 0 && note < 128)
        (note < 64 ? heldNotesLo : heldNotesHi)
            .fetch_or(1ull << (note & 63), std::memory_order_relaxed);
    if (isAcidMode()) { acidNoteOn(note, velocity01); return; }
    // Keep voiceParams.mode current even for a frame-0 note that arrives before
    // the block's snapshot runs — the voice needs it to trigger the right osc
    // section (Prism retriggers its 4 operator envelopes at note-on).
    voiceParams.mode = (SynthMode)clampi((int)p(pMode), 0, 5);
    if (p(pArpOn) > 0.5f) { arp.setEnabled(true); arp.noteOn(note, clampi((int)(velocity01 * 127.0f), 1, 127)); }
    else voices.noteOn(note, clamp01(velocity01), voiceParams);
}

void MultiSynthDSP::noteOff(int note) noexcept
{
    if (note >= 0 && note < 128)
        (note < 64 ? heldNotesLo : heldNotesHi)
            .fetch_and(~(1ull << (note & 63)), std::memory_order_relaxed);
    if (isAcidMode()) { acidNoteOff(note); return; }
    if (p(pArpOn) > 0.5f) arp.noteOff(note);
    else voices.noteOff(note);
}

void MultiSynthDSP::allNotesOff() noexcept
{
    heldNotesLo.store(0, std::memory_order_relaxed);
    heldNotesHi.store(0, std::memory_order_relaxed);
    voices.allNotesOff();
    arp.reset();
    acidVoice.noteOff();
    acidSeq.noteOff(0); acidSeq.clearLatch(); acidSeq.reset();
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
void MultiSynthDSP::snapshotParameters() noexcept
{
    VoiceParameters& vp = voiceParams;
    vp.mode = (SynthMode)clampi((int)p(pMode), 0, 5);

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
        // Latch just turned off: prune latched notes whose keys are no longer
        // physically down (heldNotes tracks key state). Keys still held keep
        // playing; with none held the arp/acid pattern stops cleanly.
        const uint64_t lo = heldNotesLo.load(std::memory_order_relaxed);
        const uint64_t hi = heldNotesHi.load(std::memory_order_relaxed);
        arp.retainHeld(lo, hi);
        acidSeq.retainHeld(lo, hi);
    }
    lastArpLatch = latchOn;
    arp.setLatch(latchOn);
    arp.setVelocityMode((ArpVelocityMode)clampi((int)p(pArpVelMode), 0, 2));
    arp.setFixedVelocity((int)p(pArpFixedVel));
    for (int i = 0; i < 16; ++i) arp.setStepActive(i, p((Param)(pArpStep0 + i)) > 0.5f);

    // --- Effects ---
    effects.drive.setEnabled(p(pDriveOn) > 0.5f);
    effects.drive.setType((DriveType)clampi((int)p(pDriveType), 0, 2));
    effects.drive.setDrive(p(pDriveAmt));
    baseDriveMix = p(pDriveMix); effects.drive.setMix(baseDriveMix);

    effects.chorus.setEnabled(p(pChorusOn) > 0.5f);
    effects.chorus.setRate(p(pChorusRate));
    effects.chorus.setDepth(p(pChorusDepth));
    baseChorusMix = p(pChorusMix); effects.chorus.setMix(baseChorusMix);

    effects.delay.setEnabled(p(pDelayOn) > 0.5f);
    effects.delay.setTempoSync(p(pDelaySync) > 0.5f);
    effects.delay.setTimeMs(p(pDelayTime));
    effects.delay.setSyncDivision((ArpRateDivision)clampi((int)p(pDelayDiv), 0, (int)ArpRateDivision::NumDivisions - 1));
    effects.delay.setFeedback(p(pDelayFB));
    baseDelayMix = p(pDelayMix); effects.delay.setMix(baseDelayMix);
    effects.delay.setPingPong(p(pDelayPP) > 0.5f);
    effects.delay.setTapeCharacter(p(pDelayTape) > 0.5f);

    effects.reverb.setEnabled(p(pReverbOn) > 0.5f);
    effects.reverb.setSize(p(pReverbSize));
    effects.reverb.setDecay(p(pReverbDecay));
    effects.reverb.setDamping(p(pReverbDamp));
    baseReverbMix = p(pReverbMix); effects.reverb.setMix(baseReverbMix);
    effects.reverb.setPreDelay(p(pReverbPD));

    cosmosChorus.setMode(vp.mode == SynthMode::Cosmos
        ? (CosmosChorusMode)clampi((int)p(pCosmosChorus), 0, 3) : CosmosChorusMode::Off);

    const bool isModular = (vp.mode == SynthMode::Modular);
    effects.springReverb.setEnabled(isModular);
    if (isModular) effects.springReverb.setMix(0.15f);

    // --- LFOs (with tempo-sync rate scaling) ---
    const double bpm = hostBpm.load(std::memory_order_relaxed);
    float lfo1Rate = p(pLfo1Rate); float lfo2Rate = p(pLfo2Rate);
    if (p(pLfo1Sync) > 0.5f && bpm > 0.0) lfo1Rate *= (float)(bpm / 120.0);
    if (p(pLfo2Sync) > 0.5f && bpm > 0.0) lfo2Rate *= (float)(bpm / 120.0);
    const LFOShape lfo1Shape = (LFOShape)clampi((int)p(pLfo1Shape), 0, 4);
    const LFOShape lfo2Shape = (LFOShape)clampi((int)p(pLfo2Shape), 0, 4);
    const float lfo1Fade = p(pLfo1Fade), lfo2Fade = p(pLfo2Fade);
    const bool prismMode = (vp.mode == SynthMode::Prism);
    for (int v = 0; v < kMaxPolyphony; ++v)
    {
        voices.getVoice(v)->setLFO1Params(lfo1Shape, lfo1Rate, lfo1Fade);
        voices.getVoice(v)->setLFO2Params(lfo2Shape, lfo2Rate, lfo2Fade);
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
        acidVoice.setCutoff(vp.filterCutoff);
        acidVoice.setResonance(vp.filterResonance);
        acidVoice.setEnvMod(clampf(std::abs(vp.filterEnvAmount), 0.0f, 1.0f));
        acidVoice.setDecay(vp.ampDecay);
        acidVoice.setSustain(vp.ampSustain);
        acidVoice.setDrive(1.0f + 4.0f * clamp01(p(pDriveAmt)));
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
    masterGain = std::pow(10.0f, p(pMasterVol) / 20.0f);
    masterPan = p(pMasterPan);
    stereoWidth = p(pStereoWidth);
    vintage = p(pVintage);

    // --- Preset/mode transition handling (stuck-note fix) ---------------------
    // A factory-preset load can change the mode, or disable the arp / acid
    // sequencer, WHILE notes are held. The note-routing path then changes out
    // from under the sounding voice, so its key-up never reaches it and it drones
    // forever. Detect the transition here and release the stranded voice(s).
    // Release (not hard-mute) so envelopes enter Release and tails ring out with
    // no click. Browsing presets WITHIN the same mode is untouched — held notes
    // stay seamless, which is the correct, desired behaviour.
    if (haveLastSnap)
    {
        if (vp.mode != lastSnapMode)
        {
            // Mode changed: every note started under the old mode is unreachable.
            voices.allNotesOff();
            acidVoice.noteOff();
            acidHeldCount = 0;   // drop the live-acid held-note stack
            arp.clearLatch();    // drop latched held notes (else they re-trigger
            arp.reset();         // in the new mode and drone; reset() keeps them
            acidSeq.clearLatch();// while latch is on)
            acidSeq.reset();
        }
        else
        {
            // Same mode: only the specific disabled subsystem's voice is stranded.
            if (lastArpEnabled && !arpEnabled && vp.mode != SynthMode::Acid)
                voices.allNotesOff();          // release the arp-triggered voice
            if (lastAcidSeqEnabled && !acidSeqEnabled)
                acidVoice.noteOff();           // release the gated acid voice
        }
    }
    haveLastSnap = true;
    lastSnapMode = vp.mode;
    lastArpEnabled = arpEnabled;
    lastAcidSeqEnabled = acidSeqEnabled;
}

//==============================================================================
void MultiSynthDSP::processBlock(float* outL, float* outR, int nSamples) noexcept
{
    duskaudio::ScopedFlushDenormals noDenormals;
    if (nSamples <= 0) return;

    // Apply a pending oversampling change at block start (never mid-block).
    const int desiredOs = ((int)p(pOversampling) == 0) ? 1 : ((int)p(pOversampling) == 1) ? 2 : 4;
    if (desiredOs != osFactor) applyOsFactor(desiredOs);

    snapshotParameters();

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

    const float panAngle = (masterPan + 1.0f) * 0.25f * kPi;
    const float panL = std::cos(panAngle), panR = std::sin(panAngle);

    auto softLimit = [](float x) noexcept -> float {
        if (isBad(x)) return 0.0f;
        const float ax = std::abs(x);
        if (ax <= 0.9f) return x;
        const float limited = 0.9f + 0.1f * std::tanh((ax - 0.9f) * 10.0f);
        return x >= 0.0f ? limited : -limited;
    };

    const bool acidMode = (voiceParams.mode == SynthMode::Acid);

    for (int i = 0; i < nSamples; ++i)
    {
        // Render osFactor internal samples, then decimate to host rate (fix #1).
        float iL[4], iR[4];
        float fxAccum = 0.0f;

        if (acidMode)
        {
            // Mono acid path: the sequencer (when enabled) or live legato play
            // drives a single AcidVoice; the poly allocator/arp are bypassed.
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

        constexpr float kVoiceGain = 0.7f;
        sL *= kVoiceGain; sR *= kVoiceGain;

        cosmosChorus.process(sL, sR);

        // EffectsMix mod (fix #5): scale effect wet mixes by the mean per-voice
        // routing amount. Only pays per-sample setter cost when routed.
        if (hasEffectsMixRouting)
        {
            const float m = clampf(1.0f + fxMod, 0.0f, 2.0f);
            effects.drive.setMix(clamp01(baseDriveMix * m));
            effects.chorus.setMix(clamp01(baseChorusMix * m));
            effects.delay.setMix(clamp01(baseDelayMix * m));
            effects.reverb.setMix(clamp01(baseReverbMix * m));
        }

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

        // Master gain + pan.
        sL *= masterGain * panL;
        sR *= masterGain * panR;

        // Stereo width (mid/side).
        const float mid = (sL + sR) * 0.5f;
        const float side = (sL - sR) * 0.5f;
        sL = mid + side * stereoWidth;
        sR = mid - side * stereoWidth;

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

    if (acidMode && acidSeqEnabled)
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
