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
    if ((r = match("arpStep", pArpStep0, 16)) >= 0) return r;
    if ((r = match("modSrc",  pModSrc0,  8))  >= 0) return r;
    if ((r = match("modDst",  pModDst0,  8))  >= 0) return r;
    if ((r = match("modAmt",  pModAmt0,  8))  >= 0) return r;
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
    junoChorus.prepare(hostRate, maxBlockSize);
    arp.prepare(hostRate);
    meterDecay = std::exp(-1.0f / (0.3f * (float)hostRate));
    applyOsFactor(2);
    reset();
}

void MultiSynthDSP::applyOsFactor(int factor)
{
    osFactor = (factor == 4) ? 4 : (factor == 2 ? 2 : 1);
    const double internalRate = hostRate * (double)osFactor;
    voices.prepare(internalRate);      // allocation-free re-prepare at new rate
    decimL.setFactor(osFactor); decimL.reset();
    decimR.setFactor(osFactor); decimR.reset();
}

void MultiSynthDSP::reset()
{
    voices.reset();
    effects.reset();
    junoChorus.reset();
    arp.reset();
    decimL.reset(); decimR.reset();
    prevVintageL = prevVintageR = 0.0f;
    meterL = meterR = 0.0f;
    scope.fill(0.0f);
    scopeWritePos.store(0, std::memory_order_relaxed);
}

//==============================================================================
void MultiSynthDSP::noteOn(int note, float velocity01) noexcept
{
    if (p(pArpOn) > 0.5f) { arp.setEnabled(true); arp.noteOn(note, clampi((int)(velocity01 * 127.0f), 1, 127)); }
    else voices.noteOn(note, clamp01(velocity01), voiceParams);
}

void MultiSynthDSP::noteOff(int note) noexcept
{
    if (p(pArpOn) > 0.5f) arp.noteOff(note);
    else voices.noteOff(note);
}

void MultiSynthDSP::allNotesOff() noexcept
{
    voices.allNotesOff();
    arp.reset();
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

    vp.portamentoTime = p(pPortaTime);
    vp.legatoMode = p(pLegato) > 0.5f;
    vp.glideMode = (int)p(pGlideMode);
    vp.analogAmount = p(pAnalogAmt);
    vp.velocitySensitivity = p(pVelSens);
    vp.velocityCurve = (int)p(pVelCurve);

    vp.unisonDetune = p(pUnisonDetune);
    vp.unisonSpread = p(pUnisonSpread);

    const float pbRange = p(pPbRange);
    vp.pitchBendSemis = pitchBendNorm.load(std::memory_order_relaxed) * pbRange;
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
    arp.setLatch(p(pArpLatch) > 0.5f);
    arp.setVelocityMode((ArpVelocityMode)clampi((int)p(pArpVelMode), 0, 2));
    arp.setFixedVelocity((int)p(pArpFixedVel));
    for (int i = 0; i < 16; ++i) arp.setStepMute(i, p((Param)(pArpStep0 + i)) > 0.5f);

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

    junoChorus.setMode(vp.mode == SynthMode::Cosmos
        ? (JunoChorusMode)clampi((int)p(pCosmosChorus), 0, 3) : JunoChorusMode::Off);

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
    for (int v = 0; v < kMaxPolyphony; ++v)
    {
        voices.getVoice(v)->setLFO1Params(lfo1Shape, lfo1Rate, lfo1Fade);
        voices.getVoice(v)->setLFO2Params(lfo2Shape, lfo2Rate, lfo2Fade);
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

    // --- engine-level cached controls ---
    masterGain = std::pow(10.0f, p(pMasterVol) / 20.0f);
    masterPan = p(pMasterPan);
    stereoWidth = p(pStereoWidth);
    vintage = p(pVintage);
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
    const float panAngle = (masterPan + 1.0f) * 0.25f * kPi;
    const float panL = std::cos(panAngle), panR = std::sin(panAngle);

    auto softLimit = [](float x) noexcept -> float {
        if (isBad(x)) return 0.0f;
        const float ax = std::abs(x);
        if (ax <= 0.9f) return x;
        const float limited = 0.9f + 0.1f * std::tanh((ax - 0.9f) * 10.0f);
        return x >= 0.0f ? limited : -limited;
    };

    for (int i = 0; i < nSamples; ++i)
    {
        // Arp advances at host rate; triggers its own generated notes.
        if (arpEnabled)
        {
            const auto ev = arp.advanceSample(bpm, playing);
            if (ev.noteOffValid) voices.noteOff(ev.offNote);
            if (ev.noteOnValid)  voices.noteOn(ev.onNote, (float)ev.onVel / 127.0f, voiceParams);
        }

        // Render osFactor internal samples, then decimate to host rate (fix #1).
        float iL[4], iR[4];
        float fxAccum = 0.0f;
        for (int os = 0; os < osFactor; ++os)
        {
            float l, r, fx;
            voices.renderInternalSample(voiceParams, modMatrix, l, r, fx);
            iL[os] = l; iR[os] = r; fxAccum += fx;
        }
        float sL = decimL.process(iL);
        float sR = decimR.process(iR);
        const float fxMod = fxAccum / (float)osFactor;

        constexpr float kVoiceGain = 0.7f;
        sL *= kVoiceGain; sR *= kVoiceGain;

        junoChorus.process(sL, sR);

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

        sL = softLimit(sL);
        sR = softLimit(sR);

        outL[i] = sL;
        if (outR) outR[i] = sR;

        // Scope ring (mono sum).
        const int wp = scopeWritePos.load(std::memory_order_relaxed);
        scope[(size_t)wp] = (sL + (outR ? sR : sL)) * 0.5f;
        scopeWritePos.store((wp + 1) % kScopeSize, std::memory_order_relaxed);

        // Peak metering with release.
        const float aL = std::abs(sL), aR = std::abs(sR);
        meterL = aL > meterL ? aL : meterL * meterDecay;
        meterR = aR > meterR ? aR : meterR * meterDecay;
    }

    if (arpEnabled)
    {
        arpStep.store(arp.getCurrentStep(), std::memory_order_relaxed);
        arpTotalSteps.store(arp.getTotalSteps(), std::memory_order_relaxed);
    }

    auto toDb = [](float lin) noexcept { return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -60.0f; };
    outLevelL.store(toDb(meterL), std::memory_order_relaxed);
    outLevelR.store(toDb(meterR), std::memory_order_relaxed);
}

} // namespace msynth
