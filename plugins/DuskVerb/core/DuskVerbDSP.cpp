// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// See DuskVerbDSP.hpp. Every function below is a transcription of the matching
// DuskVerbProcessor member in plugins/DuskVerb/src/PluginProcessor.cpp; the
// comments explaining WHY each step is ordered the way it is were written
// against real bugs and are kept with the code they describe.

#include "DuskVerbDSP.hpp"

#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbPresetEngineConfig.h"
#include "DuskVerbPresets.hpp"
#include "DuskVerbToneMacro.hpp"
#include "../src/FactoryPresets.h"

#include "DuskDenormals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace duskverb
{
namespace
{
// juce::Decibels::gainToDecibels with the default -100 dB floor.
inline float gainToDecibels (float gain) noexcept
{
    return gain > 0.0f ? std::max (-100.0f, std::log10 (gain) * 20.0f) : -100.0f;
}

constexpr float kHalfPi = 1.5707963267948966f;

// Bypass crossfade length. Long enough that the switch is inaudible, short
// enough that a host toggling bypass feels instant.
constexpr float kBypassFadeSeconds = 0.030f;
} // namespace

DuskVerbDSP::DuskVerbDSP (const EarlyFieldConfig* earlyFieldOverride, const OctaveTrimGains* octaveTrimOverride,
                          const BandLimitSetting* bandLimitOverride)
{
    // Message thread: cache the DUSKVERB_* sweep environment before any audio.
    primeTuningEnvCache();

    if (earlyFieldOverride != nullptr)
    {
        earlyFieldOverride_ = *earlyFieldOverride;
        hasEarlyFieldOverride_ = true;
    }
#if defined (DUSKVERB2_TUNING_HOOKS) && DUSKVERB2_TUNING_HOOKS
    // The early-field tuning hook, read once here and never on the audio thread. A config that
    // does not parse is refused whole, and said so: a tuning run that silently fell back to the
    // table would measure the wrong thing.
    else if (const char* env = std::getenv ("DUSKVERB2_EARLYFIELD"); env != nullptr && env[0] != '\0')
    {
        char text[512];
        if (parseEarlyFieldConfig (env, earlyFieldOverride_, text, sizeof (text)))
        {
            hasEarlyFieldOverride_ = true;
            formatEarlyFieldConfig (earlyFieldOverride_, text, sizeof (text));
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_EARLYFIELD for every preset: %s\n", text);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_EARLYFIELD ignored, %s\n", text);
    }
#endif

    if (octaveTrimOverride != nullptr)
    {
        octaveTrimOverride_ = *octaveTrimOverride;
        hasOctaveTrimOverride_ = true;
    }
#if defined (DUSKVERB2_TUNING_HOOKS) && DUSKVERB2_TUNING_HOOKS
    // The octave-trim tuning hook, read and refused the same way.
    else if (const char* env = std::getenv ("DUSKVERB2_OCTAVETRIM"); env != nullptr && env[0] != '\0')
    {
        char text[512];
        if (parseOctaveTrim (env, octaveTrimOverride_, text, sizeof (text)))
        {
            hasOctaveTrimOverride_ = true;
            formatOctaveTrim (octaveTrimOverride_, text, sizeof (text));
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_OCTAVETRIM for every preset: %s\n", text);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_OCTAVETRIM ignored, %s\n", text);
    }
#endif

    if (bandLimitOverride != nullptr)
    {
        bandLimitOverride_ = *bandLimitOverride;
        hasBandLimitOverride_ = true;
    }
#if defined (DUSKVERB2_TUNING_HOOKS) && DUSKVERB2_TUNING_HOOKS
    // The band-limit tuning hook, read and refused the same way.
    else if (const char* env = std::getenv ("DUSKVERB2_BANDLIMIT"); env != nullptr && env[0] != '\0')
    {
        char text[512];
        if (parseBandLimit (env, bandLimitOverride_, text, sizeof (text)))
        {
            hasBandLimitOverride_ = true;
            formatBandLimit (bandLimitOverride_, text, sizeof (text));
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_BANDLIMIT for every preset: %s\n", text);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_BANDLIMIT ignored, %s\n", text);
    }
    // The engine patch's decorrelated side, forced on (1) or off (0) for every preset that loads.
    if (const char* env = std::getenv ("DUSKVERB2_PMBSIDE"); env != nullptr && env[0] != '\0')
    {
        if ((env[0] == '0' || env[0] == '1') && env[1] == '\0')
        {
            pmbSideOverride_ = env[0] - '0';
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBSIDE=%d for every preset\n", pmbSideOverride_);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBSIDE ignored, not 0 or 1: '%s'\n", env);
    }
    // The engine patch's sparse-ER low-pass corner, for every preset that loads (0: off).
    if (const char* env = std::getenv ("DUSKVERB2_SPARSEERLP"); env != nullptr && env[0] != '\0')
    {
        double hz = -1.0;
        if (detail::parseNumber (detail::trimmed (env, env + std::strlen (env)), hz) && (hz == 0.0 || (hz >= 1000.0 && hz <= 20000.0)))
        {
            sparseERLpOverride_ = static_cast<float> (hz);
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_SPARSEERLP=%g Hz for every preset\n", hz);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_SPARSEERLP ignored, not 0 or 1000-20000 Hz: '%s'\n", env);
    }
    // The engine patch's dual slope, for every preset that loads: "ratio,gain".
    if (const char* env = std::getenv ("DUSKVERB2_DUALSLOPE"); env != nullptr && env[0] != '\0')
    {
        std::array<float, 2> v {};
        if (detail::parseNumberList (env, v, 0.0, 4.0, true) && (v[0] == 0.0f || (v[0] >= 0.05f && v[0] <= 1.0f)))
        {
            dualSlopeOverride_ = v;
            hasDualSlopeOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_DUALSLOPE=%g,%g for every preset\n", static_cast<double> (v[0]), static_cast<double> (v[1]));
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_DUALSLOPE ignored, not ratio 0 or 0.05-1 and gain 0-4: '%s'\n", env);
    }
    // The engine patch's octave decay multipliers, for every preset that loads.
    if (const char* env = std::getenv ("DUSKVERB2_OCTDECAY"); env != nullptr && env[0] != '\0')
    {
        if (parseOctaveDecay (env, octaveDecayOverride_))
        {
            hasOctaveDecayOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_OCTDECAY for every preset: %s\n", env);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_OCTDECAY ignored, not nine multipliers in 0.05..20: '%s'\n", env);
    }
    // The shared mode-smear sweep hook: a sweep measures the smear it sets, not the patch's.
    if (const char* env = std::getenv ("DUSKVERB_MODESMEAR"); env != nullptr && env[0] != '\0')
        sharedModeSmearHook_ = true;
    // The shared density-jitter sweep hook, likewise.
    if (const char* env = std::getenv ("DUSKVERB_DENSJIT"); env != nullptr && env[0] != '\0')
        sharedDensityJitterHook_ = true;
    // The shared FDN delay sweep hook, likewise.
    if (const char* env = std::getenv ("DUSKVERB_FDN_DELAYS"); env != nullptr && env[0] != '\0')
        sharedFdnDelaysHook_ = true;
    // The engine patch's feedback rotation, for every preset that loads: "peak radians,rate Hz".
    if (const char* env = std::getenv ("DUSKVERB2_FBROT"); env != nullptr && env[0] != '\0')
    {
        std::array<float, 2> v {};
        if (detail::parseNumberList (env, v, 0.0, 10.0, true) && fbRotationValid (v))
        {
            fbRotationOverride_ = v;
            hasFbRotationOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_FBROT=%g,%g for every preset\n", static_cast<double> (v[0]), static_cast<double> (v[1]));
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_FBROT ignored, not a peak angle 0-1.5708 rad and a rate 0.01-10 Hz: '%s'\n", env);
    }
    // The engine patch's ParallelMultiband smear shares, for every preset that loads.
    if (const char* env = std::getenv ("DUSKVERB2_PMBSMEARSHARE"); env != nullptr && env[0] != '\0')
    {
        if (parsePmbSmearShare (env, pmbSmearShareOverride_))
        {
            hasPmbSmearShareOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBSMEARSHARE for every preset: %s\n", env);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBSMEARSHARE ignored, not six shares in 0..1: '%s'\n", env);
    }
    // The engine patch's ParallelMultiband densifier coefficients and band levels, likewise.
    if (const char* env = std::getenv ("DUSKVERB2_PMBAPK"); env != nullptr && env[0] != '\0')
    {
        if (parsePmbDensifier (env, pmbDensifierOverride_))
        {
            hasPmbDensifierOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBAPK for every preset: %s\n", env);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBAPK ignored, not six coefficients in -1..0.75: '%s'\n", env);
    }
    if (const char* env = std::getenv ("DUSKVERB2_PMBLEVEL"); env != nullptr && env[0] != '\0')
    {
        if (parsePmbLevel (env, pmbLevelOverride_))
        {
            hasPmbLevelOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBLEVEL for every preset: %s\n", env);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_PMBLEVEL ignored, not six multipliers in 0.1..4: '%s'\n", env);
    }
    // The engine patch's band width stage, for every preset that loads.
    if (const char* env = std::getenv ("DUSKVERB2_STEREOBANDS"); env != nullptr && env[0] != '\0')
    {
        if (parseStereoBands (env, stereoBandsOverride_))
        {
            hasStereoBandsOverride_ = true;
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_STEREOBANDS for every preset: %s\n", env);
        }
        else
            std::fprintf (stderr, "DuskVerb 2: DUSKVERB2_STEREOBANDS ignored, not three gains in 0..%g: '%s'\n",
                          static_cast<double> (StereoBands::kMaxGain), env);
    }
#endif

    const auto& table = paramTable();
    for (int i = 0; i < kNumParams; ++i)
        params_[static_cast<size_t> (i)].store (dspFromHost (table[static_cast<size_t> (i)],
                                                             hostDefault (table[static_cast<size_t> (i)])),
                                                std::memory_order_relaxed);

    // No config is installed yet: the engines' default tables, voiced at the default knobs.
    const DampingKnobs knobs = dampingKnobs (toneTrebleFactor (p (Tone)));
    scalerA_.capture (engineA_, knobs, true);
    scalerB_.capture (engineB_, knobs, true);
}

void DuskVerbDSP::prepare (double sampleRate, int samplesPerBlock)
{
    constexpr int kMinPreparedBlockSize = 4096;
    const int safeBlockSize = std::max (samplesPerBlock, kMinPreparedBlockSize);
    const bool needsReprepare =
        (preparedSampleRate_ != sampleRate || safeBlockSize > preparedBlockSize_);

    if (needsReprepare)
    {
        preparedSampleRate_ = sampleRate;
        preparedBlockSize_  = safeBlockSize;
        // Both engines stay prepared for the lifetime of the session — only
        // one runs at any moment except during the brief preset-fade window.
        engineA_.prepare (sampleRate, safeBlockSize);
        engineB_.prepare (sampleRate, safeBlockSize);

        // Preset swaps rebuild the buildup cascade on the audio thread. Grow
        // its vectors to the supported maximum here; subsequent assign() calls
        // retain that capacity while restoring each preset's exact delay sizes,
        // masks and modulation state. No engine coefficients are changed.
        for (auto* engine : { &engineA_, &engineB_ })
        {
            engine->setBuildupTimeScale (3.0f);
            engine->setBuildupTimeScale (1.0f);
        }
        for (OwnVoice* own : { &ownA_, &ownB_ })
        {
            own->nonLin.prepare (sampleRate, safeBlockSize);
            own->shell.prepare (sampleRate);
        }
        scalerA_.prepare (sampleRate);
        scalerB_.prepare (sampleRate);
        earlyInput_.prepare (sampleRate, safeBlockSize);
        for (FieldVoice* voice : { &fieldA_, &fieldB_ })
        {
            voice->field.prepare (earlyInput_);
            voice->shell.prepare (sampleRate);
        }
        octaveTrim_.prepare (sampleRate);
        bandLimit_.prepare (sampleRate);
        stereoBands_.prepare (sampleRate);

        fadeBufL_.assign (static_cast<size_t> (safeBlockSize), 0.0f);
        fadeBufR_.assign (static_cast<size_t> (safeBlockSize), 0.0f);
        dryBufL_ .assign (static_cast<size_t> (safeBlockSize), 0.0f);
        dryBufR_ .assign (static_cast<size_t> (safeBlockSize), 0.0f);
        workL_   .assign (static_cast<size_t> (safeBlockSize), 0.0f);
        workR_   .assign (static_cast<size_t> (safeBlockSize), 0.0f);
        for (auto* buffer : { &earlyInL_, &earlyInR_, &earlyOutL_, &earlyOutR_ })
            buffer->assign (static_cast<size_t> (safeBlockSize), 0.0f);

        // 2 ms matches the engine's old internal mix-smoothing time so user
        // knob movements stay responsive but any preset-driven mix jump ramps
        // instead of stepping.
        mixSmoother_.setSmoothingTime (sampleRate, 2.0f);
        const bool busMode = p (BusMode) >= 0.5f;
        mixSmoother_.reset (busMode ? 1.0f : p (Mix));

        ducker_.prepare (sampleRate);

        bypassFadeStep_ = 1.0f / std::max (1.0f, static_cast<float> (sampleRate) * kBypassFadeSeconds);
        bypassFade_     = p (Bypass) >= 0.5f ? 1.0f : 0.0f;

        // Cancel any in-flight fade — both engines are reset, no tail to continue.
        previousEngine_ = nullptr;
        presetFadeRemaining_ = 0;
        presetFadeTotal_     = 0;

        // Force re-push of every cached value next process() call.
        cachedAlgorithm_ = -1;
        lastDecaySec_ = lastSize_ = lastDamping_ = lastBassMult_ = lastMidMult_ =
        lastCrossover_ = lastHighCrossover_ = lastSaturation_ =
        lastDiffusion_ = lastModDepth_ = lastModRate_ = lastERSize_ = lastPreDelayMs_ =
        lastMix_ = lastLoCut_ = lastHiCut_ = lastWidth_ = lastMonoBelow_ = lastMonoBelowDepth_ =
        lastTonalCorr_ = -1.0f;
        lastERLevel_ = -2.0f;
        lastERBoost_ = -1.0f;
        lastERRise_  = -1.0f;
        lastERBusLow_ = lastERBusHigh_ = -99.0f;
        lastTankLevel_ = -1.0f;
        lastTankSplitHz_ = -1.0f;
        lastERStereoNeutral_ = -1.0f;
        lastERDecorr_ = -1.0f;
        lastXTalk_   = -1.0f;
        lastMbEnable_ = false;
        lastMbLow_ = lastMbMid_ = lastMbHigh_ = -1.0f;
        lastGainTrim_ = -999.0f;
        lastHiCutShelfDb_ = 999.0f;   // out-of-range sentinel forces push
        haveLastFreeze_ = false;
        haveLastGateEnabled_ = false;
    }

    // Re-install engine config on BOTH engines. The host may issue multiple
    // deactivate+activate cycles during init. Each prepare resets ParametricBand
    // coefficients to designUnity, so without this re-install the PostTankEQ /
    // modulation topology / sixAP overrides installed by an earlier swap would
    // be gone by render time.
    if (auto* preset = lastAppliedPreset_.load (std::memory_order_acquire))
    {
        preset->applyEngineConfig (engineA_);
        preset->applyEngineConfig (engineB_);
        // The config re-install resets what the patch sets over it (the PMB band table), so
        // the patch goes back on too, in the swap's order: before the scalers capture.
        applyEnginePatch (engineA_, preset);
        applyEnginePatch (engineB_, preset);
        // The re-install put the unscaled tables back; the next block rescales them.
        const DampingKnobs stored = storedDampingKnobs (*preset);
        scalerA_.capture (engineA_, stored, true);
        scalerB_.capture (engineB_, stored, true);
        installEarlyField (&engineA_, preset, nullptr);
        installEarlyField (&engineB_, preset, nullptr);
    }
}

void DuskVerbDSP::reset()
{
    clearState();
    releaseResources();
}

void DuskVerbDSP::clearState() noexcept
{
    engineA_.clearAllBuffers();
    engineB_.clearAllBuffers();
    // Only a voice that is sounding needs clearing: armOwnVoice() clears a voice whenever it
    // becomes active, so an idle one is cleared before it is heard again. That keeps a host
    // reset on a shared engine from also zeroing both own voices' rings (several MB at high
    // sample rates) on the audio thread.
    for (OwnVoice* own : { &ownA_, &ownB_ })
    {
        if (! own->active)
            continue;
        own->nonLin.clear();
        own->shell.clear();
    }
    // The fields' input is what every field installed later starts from, so it is cleared
    // whichever field is installed; a voice's own state is a few floats.
    earlyInput_.clear();
    for (FieldVoice* voice : { &fieldA_, &fieldB_ })
    {
        voice->field.clearState();
        voice->shell.clear();
        voice->needsSnap = true;
    }
    // The trim and the band limit keep the preset's settings; a fade they were in ends with the
    // engines' below.
    octaveTrim_.clear();
    bandLimit_.clear();
    stereoBands_.clear();
    // activeEngine_ is deliberately NOT reassigned. The JUCE processor never
    // moves it outside performPresetSwap(), and moving it here breaks presets
    // whose voicing rests on a parameter that prepare() does not re-arm for the
    // edge-detect (the QuadTank multipliers, the FiveBand set, the DPV EQ, the
    // post-band trims, ...). Those reach an engine only through
    // forcePushAllParametersTo() at swap time, i.e. only the engine that was
    // active when the preset was applied; pointing at the other one silently
    // reverts them to defaults. Measured: forcing engineA_ here cost 79 Vocal
    // Chamber -26 dB and Tiled Room -30 dB in the JUCE null test.
    previousEngine_      = nullptr;
    presetFadeRemaining_ = 0;
    presetFadeTotal_     = 0;
    ducker_.reset();

    std::fill (fadeBufL_.begin(), fadeBufL_.end(), 0.0f);
    std::fill (fadeBufR_.begin(), fadeBufR_.end(), 0.0f);
    std::fill (dryBufL_.begin(),  dryBufL_.end(),  0.0f);
    std::fill (dryBufR_.begin(),  dryBufR_.end(),  0.0f);
    std::fill (workL_.begin(),    workL_.end(),    0.0f);
    std::fill (workR_.begin(),    workR_.end(),    0.0f);
    for (auto* buffer : { &earlyInL_, &earlyInR_, &earlyOutL_, &earlyOutR_ })
        std::fill (buffer->begin(), buffer->end(), 0.0f);

    if (preparedSampleRate_ > 0.0)
        mixSmoother_.reset (p (BusMode) >= 0.5f ? 1.0f : p (Mix));
    bypassFade_ = p (Bypass) >= 0.5f ? 1.0f : 0.0f;

    inputLevelL_.store  (-100.0f, std::memory_order_relaxed);
    inputLevelR_.store  (-100.0f, std::memory_order_relaxed);
    outputLevelL_.store (-100.0f, std::memory_order_relaxed);
    outputLevelR_.store (-100.0f, std::memory_order_relaxed);

    // Re-push the algorithm, freeze and gate edges on the next block. The other cached values
    // are not pushed again, so each engine's clearBuffers() must keep the settings it was given.
    cachedAlgorithm_ = -1;
    haveLastFreeze_ = false;
    haveLastGateEnabled_ = false;
}

void DuskVerbDSP::releaseResources() noexcept
{
    // Dropping the prepared configuration makes the next prepare() re-run
    // DuskVerbEngine::prepare() on both engines instead of taking its "nothing
    // changed" early-out. That re-prepare is what resets modulator phase, filter
    // state and the smoothers — clearAllBuffers() does not — and without it the
    // DAF build carried LFO phase from one render stimulus into the next while
    // the JUCE build started each from zero: every stem after the first missed
    // the null test by ~6 dB while the first was bit-identical.
    //
    // See the header for why this must NOT also clear the engines.
    preparedSampleRate_ = 0.0;
    preparedBlockSize_  = 0;
}

void DuskVerbDSP::pushSixAPBrightnessTo (DuskVerbEngine& target)
{
    target.setSixAPDensityBaseline (sixAPBrightness_.densityBaseline);
    target.setSixAPBloomCeiling    (sixAPBrightness_.bloomCeiling);
    target.setSixAPBloomStagger    (sixAPBrightness_.bloomStagger);
    target.setSixAPEarlyMix        (sixAPBrightness_.earlyMix);
    target.setSixAPOutputTrim      (sixAPBrightness_.outputTrim);
}

void DuskVerbDSP::setSixAPBrightness (const SixAPBrightnessState& s)
{
    sixAPBrightness_ = s;
    pushSixAPBrightnessTo (engineA_);
    pushSixAPBrightnessTo (engineB_);
}

void DuskVerbDSP::stageSound(const std::array<float, kNumParams>& plain,
                            const FactoryPreset* preset, const SixAPBrightnessState& sixAP) noexcept
{
    for (int i = 0; i < kNumParams; ++i) setParameter(i, plain[i]);
    sixAPBrightness_ = sixAP;
    restorePresetIdentity(preset);
}

void DuskVerbDSP::applyFactoryPresetConfig (const FactoryPreset& preset)
{
    // Message thread. The caller has already written the preset's parameter
    // values. Cache the engine-config state, then arm the swap flag: the audio
    // thread picks it up at the top of the next processBlock(), reconfigures the
    // idle engine and starts the crossfade. Touching the engine directly here
    // would race with processBlock.
    sixAPBrightness_.densityBaseline = preset.sixAPDensityBaseline;
    sixAPBrightness_.bloomCeiling    = preset.sixAPBloomCeiling;
    sixAPBrightness_.earlyMix        = preset.sixAPEarlyMix;
    sixAPBrightness_.outputTrim      = preset.sixAPOutputTrim;
    for (int i = 0; i < 6; ++i)
        sixAPBrightness_.bloomStagger[i] = preset.sixAPBloomStagger[i];

    // Release ordering pairs with the acquire load in processBlock to publish
    // the brightness writes above.
    lastAppliedPreset_.store (&preset, std::memory_order_release);
    pendingPresetSwap_.store (true, std::memory_order_release);
}

void DuskVerbDSP::restorePresetIdentity (const FactoryPreset* preset)
{
    // Deliberately does NOT write sixAPBrightness_ or any parameter: the
    // restored session's own values are already in place and must win.
    //
    // Clearing the pointer alone would not be enough for the nullptr case: a
    // reused instance's engine still holds the PREVIOUS preset's name-keyed
    // config (PostTankEQ bands, mod topology, FDN base delays — none of which
    // forcePushAllParametersTo touches). The armed swap runs
    // performPresetSwap()'s null branch, which resets those on the audio thread.
    lastAppliedPreset_.store (preset, std::memory_order_release);
    pendingPresetSwap_.store (true, std::memory_order_release);
}

void DuskVerbDSP::forcePushAllParametersTo (DuskVerbEngine* target)
{
    // Audio thread. Reads the current parameter values + cached SixAPBrightness
    // state and pushes everything unconditionally, so the freshly-cleared idle
    // engine starts the fade already configured for the new preset.
    // An own algorithm never reaches the shared engine, whose table ends at
    // kNumSharedAlgorithms: the engine keeps its last shared algorithm and sits
    // idle while its own voice renders (see armOwnVoice).
    const int engine = algorithm();
    if (! isOwnAlgorithm (engine))
        target->setAlgorithm (engine);

    // Pre-delay: push the raw value (without tempo-sync resolution). Any
    // sync-driven delta is picked up by the next block's edge detection.
    target->setPreDelay (p (Predelay));

    target->setDecayTime         (engineDecay());
    target->setSize              (p (Size));
    target->setTrebleMultiply    (p (Damping));
    target->setAirTrebleMultiply (p (Damping));
    target->setBassMultiply      (p (BassMult));
    target->setMidMultiply       (p (MidMult));
    target->setSubMultiply       (p (SubMult));
    target->setHiMidMultiply     (p (HiMidMult));
    target->setSubCrossoverFreq  (p (CrossoverSub));
    target->setAirCrossoverFreq  (p (CrossoverAir));
    target->setShaperDepth       (p (TransientShaper));
    target->setShaperTimeMs      (p (ShaperTime));
    target->setShaperXoverHz     (p (ShaperXover));
    target->setShaperSens        (p (ShaperSens));
    target->setInputSubGainDb    (p (InputSubGain));
    target->setInputMidGainDb    (p (InputMidGain));
    target->setInputHighGainDb   (p (InputHighGain));
    target->setCrossoverFreq     (p (Crossover));
    target->setHighCrossoverFreq (p (HighCrossover));
    target->setBassChokeHz       (p (BassChoke));
    target->setSaturation        (p (Saturation));
    target->setDiffusion         (p (Diffusion));
    target->setModDepth          (p (ModDepth));
    target->setModRate           (p (ModRate));
    target->setTailSpinDepth     (p (TailSpinDepth));
    target->setTailSpinRate      (p (TailSpinRate));
    target->setERSize            (p (ErSize));
    target->setERLevel           (p (ErLevel));
    target->setEREarlyBoost      (p (ErBoost));
    target->setQuadHiMidMultiply (p (QtHimidMult));
    target->setQuadAirMultiply   (p (QtAirMult));
    target->setEROnsetRiseMs     (p (ErRise));
    target->setERBusShelves      (p (ErBusLowGain), p (ErBusHighGain));
    target->setTankOutputLevel   (p (TankLevel));
    target->setTankSplitHz       (p (TankSplitHz));
    target->setERStereoNeutral   (p (ErStereoNeutral) >= 0.5f);
    target->setERDecorr          (p (ErDecorr));
    target->setOutputCrossTalk   (p (Xtalk));
    target->setMultibandEnabled  (p (MbEnable) >= 0.5f);
    target->setMultibandDecays   (p (MbLowDecay), p (MbMidDecay), p (MbHighDecay));
    target->setLoCut             (p (LoCut));
    target->setHiCut             (p (HiCut));
    target->setHiCutShelfGainDb  (p (HiCutShelfDb));
    // Force-push at swap so the idle engine can't retain a stale on/off state.
    target->setTonalCorrection   (p (TonalCorrection) >= 0.5f);

    target->setPostTankBandTrimCrossovers (200.0f, 800.0f, 3000.0f);
    target->setPostTankBandTrimGainDb (0, p (PostBandSubDb));
    target->setPostTankBandTrimGainDb (1, p (PostBandLowmidDb));
    target->setPostTankBandTrimGainDb (2, p (PostBandMidhiDb));
    target->setPostTankBandTrimGainDb (3, p (PostBandAirDb));

    target->setPerBandEDTCrossovers (200.0f, 800.0f, 3000.0f);
    target->setPerBandEDTShape (0, p (EdtSubAttackDb),    p (EdtSubTauMs));
    target->setPerBandEDTShape (1, p (EdtLowmidAttackDb), p (EdtLowmidTauMs));
    target->setPerBandEDTShape (2, p (EdtMidhiAttackDb),  p (EdtMidhiTauMs));
    target->setPerBandEDTShape (3, p (EdtAirAttackDb),    p (EdtAirTauMs));

    target->setFDNInLoopPeaking (p (InLoopPeakHz), p (InLoopPeakQ), p (InLoopPeakDb));

    target->setFDNDualBassShelf (p (BassShelfFastFc), p (BassShelfSlowFc),
                                 p (BassShelfFastDb), p (BassShelfSlowDb),
                                 p (BassShelfTransitionMs));

    target->setWidth          (p (Width));
    target->setGainTrim       (p (GainTrim));
    target->setMonoBelow      (p (MonoBelow));
    target->setMonoBelowDepth (p (MonoBelowDepth));

    // Mix lives on the shell — not pushed to the engine.

    target->setFreeze               (p (Freeze) >= 0.5f);
    target->setNonLinearGateEnabled (p (GateEnabled) >= 0.5f);

    pushSixAPBrightnessTo (*target);

    target->setDpvHfShelfGainDb   (p (DpvHfShelfDb));
    target->setDpvHfShelfFreqHz   (p (DpvHfShelfHz));
    target->setDpvStructHfDampHz  (p (DpvStructHfDampHz));
    target->setDpvBoxCutGainDb    (p (DpvBoxCutDb));
    target->setDpvBoxCutFreqHz    (p (DpvBoxCutHz));
    target->setDpvBassShelfGainDb (p (DpvBassShelfDb));
    target->setDpvBassShelfFreqHz (p (DpvBassShelfHz));
}

void DuskVerbDSP::syncParameterCacheToCurrent()
{
    // Update the edge-detection cache so the next block doesn't re-push values
    // we just force-pushed to the new active engine.
    //
    // NOTE (carried over verbatim from the JUCE processor): lastTonalCorr_ is
    // deliberately NOT written here, so the next block re-issues
    // setTonalCorrection once. Harmless, and changing it would move the fleet.
    cachedAlgorithm_   = algorithm();
    lastPreDelayMs_    = p (Predelay);
    lastDecaySec_      = engineDecay();
    lastSize_          = p (Size);
    lastDamping_       = p (Damping);
    lastBassMult_      = p (BassMult);
    lastMidMult_       = p (MidMult);
    lastSubMult_       = p (SubMult);
    lastHiMidMult_     = p (HiMidMult);
    lastCrossoverSub_  = p (CrossoverSub);
    lastCrossoverAir_  = p (CrossoverAir);
    lastShaperDepth_   = p (TransientShaper);
    lastShaperTime_    = p (ShaperTime);
    lastShaperXover_   = p (ShaperXover);
    lastShaperSens_    = p (ShaperSens);
    lastInputSubGain_  = p (InputSubGain);
    lastInputMidGain_  = p (InputMidGain);
    lastInputHighGain_ = p (InputHighGain);
    lastCrossover_     = p (Crossover);
    lastHighCrossover_ = p (HighCrossover);
    lastBassChoke_     = p (BassChoke);
    lastSaturation_    = p (Saturation);
    lastDiffusion_     = p (Diffusion);
    lastModDepth_      = p (ModDepth);
    lastModRate_       = p (ModRate);
    lastTailSpinDepth_ = p (TailSpinDepth);
    lastTailSpinRate_  = p (TailSpinRate);
    lastERSize_        = p (ErSize);
    lastERLevel_       = p (ErLevel);
    lastERBoost_       = p (ErBoost);
    lastQtHiMidMult_   = p (QtHimidMult);
    lastQtAirMult_     = p (QtAirMult);
    lastERRise_        = p (ErRise);
    lastERBusLow_      = p (ErBusLowGain);
    lastERBusHigh_     = p (ErBusHighGain);
    lastTankLevel_     = p (TankLevel);
    lastTankSplitHz_   = p (TankSplitHz);
    lastERStereoNeutral_ = p (ErStereoNeutral);
    lastERDecorr_      = p (ErDecorr);
    lastXTalk_         = p (Xtalk);
    lastMbEnable_      = p (MbEnable) >= 0.5f;
    lastMbLow_         = p (MbLowDecay);
    lastMbMid_         = p (MbMidDecay);
    lastMbHigh_        = p (MbHighDecay);
    lastLoCut_         = p (LoCut);
    lastHiCut_         = p (HiCut);
    lastHiCutShelfDb_  = p (HiCutShelfDb);
    lastWidth_         = p (Width);
    lastGainTrim_      = p (GainTrim);
    lastMonoBelow_     = p (MonoBelow);
    lastMonoBelowDepth_= p (MonoBelowDepth);
    lastDpvHfShelfDb_    = p (DpvHfShelfDb);
    lastDpvHfShelfHz_    = p (DpvHfShelfHz);
    lastDpvStructHfDamp_ = p (DpvStructHfDampHz);
    lastDpvBoxCutDb_     = p (DpvBoxCutDb);
    lastDpvBoxCutHz_     = p (DpvBoxCutHz);
    lastDpvBassShelfDb_  = p (DpvBassShelfDb);
    lastDpvBassShelfHz_  = p (DpvBassShelfHz);

    lastPostBandSub_    = p (PostBandSubDb);
    lastPostBandLowMid_ = p (PostBandLowmidDb);
    lastPostBandMidHi_  = p (PostBandMidhiDb);
    lastPostBandAir_    = p (PostBandAirDb);

    lastEDTSubAtk_    = p (EdtSubAttackDb);
    lastEDTSubTau_    = p (EdtSubTauMs);
    lastEDTLowMidAtk_ = p (EdtLowmidAttackDb);
    lastEDTLowMidTau_ = p (EdtLowmidTauMs);
    lastEDTMidHiAtk_  = p (EdtMidhiAttackDb);
    lastEDTMidHiTau_  = p (EdtMidhiTauMs);
    lastEDTAirAtk_    = p (EdtAirAttackDb);
    lastEDTAirTau_    = p (EdtAirTauMs);

    lastInLoopPeakHz_ = p (InLoopPeakHz);
    lastInLoopPeakQ_  = p (InLoopPeakQ);
    lastInLoopPeakDb_ = p (InLoopPeakDb);

    lastBassShelfFastFc_     = p (BassShelfFastFc);
    lastBassShelfSlowFc_     = p (BassShelfSlowFc);
    lastBassShelfFastDb_     = p (BassShelfFastDb);
    lastBassShelfSlowDb_     = p (BassShelfSlowDb);
    lastBassShelfTransition_ = p (BassShelfTransitionMs);

    lastMix_ = p (BusMode) >= 0.5f ? 1.0f : p (Mix);

    lastFreeze_          = p (Freeze) >= 0.5f;
    haveLastFreeze_      = true;
    lastGateEnabled_     = p (GateEnabled) >= 0.5f;
    haveLastGateEnabled_ = true;
}

void DuskVerbDSP::armOwnVoice (const DuskVerbEngine* engine, bool own) noexcept
{
    OwnVoice& voice = ownFor (engine);
    voice.active = own;
    if (own)
    {
        voice.nonLin.clear();
        voice.shell.clear();
        voice.needsSnap = true;
    }
}

// Lo Cut, Hi Cut (the Tone macro folded in, as for the shared engines) and its shelf depth, Width
// and the output trim, with the values the shared engines are given.
void DuskVerbDSP::pushShellParameters (Dv2OutputShell& shell, float toneHiCut) noexcept
{
    shell.setLoCut (p (LoCut));
    shell.setHiCut (effectiveHiCut (p (HiCut), toneHiCut));
    shell.setHiCutShelfDb (p (HiCutShelfDb));
    shell.setWidth (p (Width));
    shell.setGainTrim (p (GainTrim));
}

void DuskVerbDSP::pushOwnParameters (OwnVoice& voice, float preDelayMs, float toneTreble, float toneHiCut) noexcept
{
    // The shared controls keep the meaning they have on every engine: Decay is the
    // time to -60 dB, Treble Multiply under 1 darkens (the Tone macro folded in as
    // for the shared engines), then Pre-Delay, Lo Cut, Hi Cut and its shelf depth,
    // Width and the output trim. The rest have no counterpart in this engine.
    voice.nonLin.setKnee (NonLinBurstEngine::kneeForDecaySeconds (engineDecay()));
    voice.nonLin.setHfDamp (NonLinBurstEngine::hfDampHzForTreble (p (Damping) * toneTreble));
    voice.nonLin.setPreDelay (preDelayMs);
    pushShellParameters (voice.shell, toneHiCut);
    if (voice.needsSnap)
    {
        voice.shell.snap();
        voice.needsSnap = false;
    }
}

void DuskVerbDSP::pushFieldParameters (FieldVoice& voice, float toneHiCut) noexcept
{
    // Every control of the engine's output stage, Mono Below included: the field's post path
    // then moves with the reverb's.
    pushShellParameters (voice.shell, toneHiCut);
    voice.shell.setMonoBelow (p (MonoBelow));
    voice.shell.setMonoBelowDepth (p (MonoBelowDepth));
    if (voice.needsSnap)
    {
        voice.shell.snap();
        voice.needsSnap = false;
    }
}

const EarlyFieldConfig& DuskVerbDSP::earlyFieldConfigFor (const FactoryPreset* preset) const noexcept
{
    if (preset == nullptr)
        return kNoEarlyField;          // no preset identity: neutral, like the engine config
    return hasEarlyFieldOverride_ ? earlyFieldOverride_ : earlyFieldFor (preset->name);
}

const OctaveTrimGains& DuskVerbDSP::octaveTrimGainsFor (const FactoryPreset* preset) const noexcept
{
    if (preset == nullptr)
        return kFlatOctaveTrim;        // no preset identity: flat, as the early field is none
    return hasOctaveTrimOverride_ ? octaveTrimOverride_ : octaveTrimFor (preset->name);
}

const BandLimitSetting& DuskVerbDSP::bandLimitSettingFor (const FactoryPreset* preset) const noexcept
{
    if (preset == nullptr)
        return kNoBandLimit;           // no preset identity: none, as the trim is flat
    return hasBandLimitOverride_ ? bandLimitOverride_ : bandLimitFor (preset->name);
}

const StereoBands::Gains& DuskVerbDSP::stereoBandsFor (const FactoryPreset* preset) const noexcept
{
    if (preset == nullptr)
        return StereoBands::kOff;      // no preset identity: no stage, like the field and the trim
    return hasStereoBandsOverride_ ? stereoBandsOverride_ : enginePatchFor (preset->name).stereoBands;
}

void DuskVerbDSP::applyEnginePatch (DuskVerbEngine& engine, const FactoryPreset* preset) noexcept
{
    // Every feature is set, on or off, so a reused engine never keeps the last preset's; the mode
    // smear and the octave decay change what the shared config has just installed.
    const EnginePatch& patch = preset != nullptr ? enginePatchFor (preset->name) : kNoEnginePatch;
    const bool side = preset != nullptr && pmbSideOverride_ >= 0 ? pmbSideOverride_ == 1 : patch.pmbDecorrelatedSide;
    engine.setPmbDecorrelatedSide (side);
    engine.setSparseERTone (preset != nullptr && sparseERLpOverride_ >= 0.0f ? sparseERLpOverride_ : patch.sparseERLowpassHz);
    const std::array<float, 2>& dual = preset != nullptr && hasDualSlopeOverride_ ? dualSlopeOverride_ : patch.dualSlope;
    engine.setFDNDualSlope (dual[0], dual[1]);
    const std::array<float, 2>& rot = preset != nullptr && hasFbRotationOverride_ ? fbRotationOverride_ : patch.fbRotation;
    engine.setFeedbackRotation (rot[0], rot[1]);
    for (int b = 0; b < static_cast<int> (patch.pmbWidth.size()); ++b)
        if (const float width = patch.pmbWidth[static_cast<size_t> (b)]; width >= 0.0f)
            engine.setPmbBand (b, engine.getPmbBandT60 (b), engine.getPmbBandLevel (b), engine.getPmbBandDirect (b), width);

    // The 16-line tanks' delays over the set the shared config has just installed; the 32-entry
    // array the engine forwards gets the default tail, which no 16-line tank reads.
    if (patch.fdnDelays[0] != 0 && ! sharedFdnDelaysHook_)
    {
        static constexpr int kDefaultTail[16] = { 1213, 1361, 1531, 1721, 1933, 2153, 2417, 2713,
                                                  3041, 3413, 3833, 4297, 4817, 5417, 6079, 6469 };
        int delays[32];
        std::copy (patch.fdnDelays.begin(), patch.fdnDelays.end(), delays);
        std::copy (std::begin (kDefaultTail), std::end (kDefaultTail), delays + 16);
        engine.setFDNBaseDelays (delays);
    }

    if (patch.modeSmear[0] >= 0.0f && ! sharedModeSmearHook_)
        engine.setDattorroModeSmear (patch.modeSmear[0], patch.modeSmear[1]);
    const std::array<float, 6>& share = preset != nullptr && hasPmbSmearShareOverride_ ? pmbSmearShareOverride_ : patch.pmbSmearShare;
    for (int b = 0; b < static_cast<int> (share.size()); ++b)
        engine.setPmbModeSmearBandShare (b, share[static_cast<size_t> (b)]);

    // The Dattorro density allpasses' jitter over the shared config's, which applyEngineConfig sets
    // for every preset just before this (so a row-less preset has its own, never the last row's).
    if (patch.densityJitter >= 0.0f && ! sharedDensityJitterHook_)
        engine.setDattorroDensityJitter (patch.densityJitter);

    // DuskVerb 2's band width stage replaces the engine's one-pole width bands: theirs go to unity
    // (the legacy single multiply by Width) whenever the stage plays.
    if (StereoBands::playing (stereoBandsFor (preset)))
        engine.setWidthBands (1.0f, 1.0f, 1.0f);

    // The ParallelMultiband bands' in-loop allpass coefficients (every band, every preset: below 0
    // is the tank's own) and their levels over the shared row's.
    const bool hooked = preset != nullptr;
    const std::array<float, 6>& densifier = hooked && hasPmbDensifierOverride_ ? pmbDensifierOverride_ : patch.pmbDensifier;
    const std::array<float, 6>& level = hooked && hasPmbLevelOverride_ ? pmbLevelOverride_ : patch.pmbLevel;
    for (int b = 0; b < 6; ++b)
    {
        engine.setPmbDensifierCoeff (b, densifier[static_cast<size_t> (b)]);
        if (const float m = level[static_cast<size_t> (b)]; m != 1.0f)
            engine.setPmbBand (b, engine.getPmbBandT60 (b), engine.getPmbBandLevel (b) * m, engine.getPmbBandDirect (b),
                               engine.getPmbBandWidth (b));
    }

    // Octave decay: each octave of whichever per-octave table the engine plays, times its
    // multiplier; the ParallelMultiband bands take the geometric mean of the octaves they span.
    const std::array<float, 9>& octave = preset != nullptr && hasOctaveDecayOverride_ ? octaveDecayOverride_ : patch.octaveDecay;
    if (std::all_of (octave.begin(), octave.end(), [] (float m) { return m == 1.0f; }))
        return;
    for (int k = 0; k < static_cast<int> (octave.size()); ++k)
    {
        const float m = octave[static_cast<size_t> (k)];
        if (m == 1.0f)
            continue;
        if (engine.isDattorroOctaveT60Active())
            engine.setDattorroOctaveT60 (k, engine.getDattorroOctaveT60 (k) * m);
        if (engine.isAccurateHallOctaveT60Active())
            engine.setAccurateHallOctaveT60 (k, engine.getAccurateHallOctaveT60 (k) * m);
        if (engine.isDenseHallOctaveT60Active())
            engine.setDenseHallOctaveT60 (k, engine.getDenseHallOctaveT60 (k) * m);
    }
    // Bands <120 / 120-350 / 350-1k / 1k-3k / 3k-8k / >8k against octaves 63 ... 16k.
    static constexpr int kBandOctaves[6][2] = { { 0, 0 }, { 1, 2 }, { 3, 3 }, { 4, 5 }, { 6, 6 }, { 7, 8 } };
    for (int b = 0; b < 6; ++b)
    {
        const float m = std::sqrt (octave[static_cast<size_t> (kBandOctaves[b][0])] * octave[static_cast<size_t> (kBandOctaves[b][1])]);
        if (m != 1.0f)
            engine.setPmbBand (b, engine.getPmbBandT60 (b) * m, engine.getPmbBandLevel (b), engine.getPmbBandDirect (b),
                               engine.getPmbBandWidth (b));
    }
}

void DuskVerbDSP::installDecayCalibration (const FactoryPreset* preset) noexcept
{
    const float* shown = preset != nullptr ? decayShownFor (preset->name) : nullptr;
    decayAlgorithm_ = shown != nullptr ? resolveAlgorithm (preset->algorithm) : -1;
    if (shown == nullptr)
        return;
    const ParamDesc& d = paramDesc (Decay);
    decayShown_  = dspFromPlain (d, *shown);
    decayEngine_ = dspFromPlain (d, preset->decay);
}

float DuskVerbDSP::engineDecay() const noexcept
{
    const float decay = p (Decay);
    if (decayAlgorithm_ != algorithm())
        return decay;
    // Exact at the reading itself whatever the engine flags make of the division, as the
    // damping scaler's C == S is (DuskVerbDampingScaler.hpp): the preset sounds as it did.
    return decay == decayShown_ ? decayEngine_ : decayEngine_ * (decay / decayShown_);
}

void DuskVerbDSP::installEarlyField (const DuskVerbEngine* engine, const FactoryPreset* preset,
                                     const DuskVerbEngine* outgoing) noexcept
{
    // The input is shared, so the field already has the whole history. At a swap (`outgoing`,
    // the engine fading out) it carries on from the outgoing field's post-path state, or starts
    // that state over when the outgoing one played none: a reload of the same preset then
    // renders the very samples the outgoing field does, and their crossfade changes nothing.
    FieldVoice& voice = fieldFor (engine);
    voice.field.configure (earlyFieldConfigFor (preset));
    voice.needsSnap = true;
    if (outgoing == nullptr)
        return;
    if (fieldPostPlays (outgoing))
    {
        voice.field.copyStateFrom (fieldFor (outgoing).field);
        voice.shell.copyStateFrom (fieldFor (outgoing).shell);
    }
    else
    {
        voice.field.clearState();
        voice.shell.clear();
    }
}

void DuskVerbDSP::renderWet (DuskVerbEngine* engine, float* left, float* right, int numSamples) noexcept
{
    OwnVoice& own = ownFor (engine);
    if (! own.active)
    {
        // The field's pre path feeds the engine, whose output stage then covers it; its post
        // path is added after the preset crossfade (addEarlyFields).
        if (const EarlyField& field = fieldFor (engine).field; field.enabled())
            field.preEngine (earlyInput_, left, right, numSamples);
        engine->process (left, right, numSamples);
        return;
    }
    own.nonLin.process (left, right, left, right, numSamples);
    own.shell.process (left, right, numSamples);
}

void DuskVerbDSP::addEarlyFields (float* left, float* right, int numSamples,
                                  const DuskVerbEngine* fadingOut, int fadeIndex) noexcept
{
    const bool in  = fieldPostPlays (activeEngine_);
    const bool out = fadingOut != nullptr && fieldPostPlays (fadingOut);
    if (! in && ! out)
        return;                                    // no field: the wet is the engines' alone
    const auto render = [this, numSamples] (FieldVoice& voice, std::vector<float>& l, std::vector<float>& r) {
        voice.field.renderPost (earlyInput_, l.data(), r.data(), numSamples);
        voice.shell.process (l.data(), r.data(), numSamples);
    };
    if (in)
        render (fieldFor (activeEngine_), earlyInL_, earlyInR_);
    if (out)
        render (fieldFor (fadingOut), earlyOutL_, earlyOutR_);

    // Linear over the engines' fade (the same t), so the two gains sum to one: the same field on
    // both sides passes unchanged, where the equal-power engine fade would lift two correlated
    // fields by 3 dB mid-fade.
    const int fade = fadingOut != nullptr ? std::min (numSamples, presetFadeTotal_ - fadeIndex) : 0;
    const float invSpan = 1.0f / static_cast<float> (std::max (1, presetFadeTotal_ - 1));
    const int end = in ? numSamples : fade;
    for (int i = 0; i < end; ++i)
    {
        const size_t k = static_cast<size_t> (i);
        float l = in ? earlyInL_[k] : 0.0f, r = in ? earlyInR_[k] : 0.0f;
        if (i < fade)
        {
            const float t = std::min (1.0f, static_cast<float> (fadeIndex + i) * invSpan);
            const float ol = out ? earlyOutL_[k] : 0.0f, orr = out ? earlyOutR_[k] : 0.0f;
            l = ol + t * (l - ol);
            r = orr + t * (r - orr);
        }
        left[i]  += l;
        right[i] += r;
    }
}

void DuskVerbDSP::performPresetSwap()
{
    // Audio thread. Pick the idle engine, reset it to silence, force-push the
    // just-applied parameters and brightness state, snap its shell smoothers to
    // the new targets, then swap pointers and arm the equal-power crossfade.
    DuskVerbEngine* newActive = (activeEngine_ == &engineA_) ? &engineB_ : &engineA_;

    // One acquire load for the Decay calibration, the engine config and the early field alike.
    // The calibration goes in first: the force-push below gives the new engine its decay.
    const FactoryPreset* preset = lastAppliedPreset_.load (std::memory_order_acquire);
    installDecayCalibration (preset);

    newActive->clearAllBuffers();
    forcePushAllParametersTo (newActive);
    armOwnVoice (newActive, isOwnAlgorithm (algorithm()));

    // The outgoing engine keeps its own field (config and controls) for the fade.
    installEarlyField (newActive, preset, activeEngine_);
    if (preset != nullptr)
    {
        preset->applyEngineConfig (*newActive);
        applyEnginePatch (*newActive, preset);
        scalerFor (newActive).capture (*newActive, storedDampingKnobs (*preset), true);
        // Cache freq + Q so subsequent gain edge-detects can re-issue
        // setPostTankEQBand without re-consulting the name-keyed map.
        resolvePteqFreqQ (preset->name, pteqBandFreq_, pteqBandQ_);
    }
    else
    {
        // No known preset. Reset the name-keyed engine config to defaults so a
        // preset previously applied to this reused instance can't leak its
        // PostTankEQ / topology / base delays onto the restored session.
        newActive->reapplyNeutralEngineConfig();
        duskverb::applyNeutralPmbAndModeSmear (*newActive);   // the shared neutral config leaves them
        applyEnginePatch (*newActive, nullptr);
        // No stored knob values without a preset: the restored ones are the reference, so the
        // session sounds as it did before the damping card was rescaled, and moves from there.
        scalerFor (newActive).capture (*newActive, dampingKnobs (toneTrebleFactor (p (Tone))), false);
        resolvePteqFreqQ ("", pteqBandFreq_, pteqBandQ_);
        // reapplyNeutralEngineConfig() flattened the PostTankEQ, but the
        // restored session still carries its own pteq gains. Re-install them now
        // (at the default freq/Q) — otherwise syncParameterCacheToCurrent()
        // below caches them as already-applied and the edge-detect never fires.
        newActive->setPostTankEQBand (0, pteqBandFreq_[0], pteqBandQ_[0], p (PteqBand0GainDb));
        newActive->setPostTankEQBand (1, pteqBandFreq_[1], pteqBandQ_[1], p (PteqBand1GainDb));
        newActive->setPostTankEQBand (2, pteqBandFreq_[2], pteqBandQ_[2], p (PteqBand2GainDb));
        newActive->setPostTankEQBand (3, pteqBandFreq_[3], pteqBandQ_[3], p (PteqBand3GainDb));
    }

    // Session/user/A-B snapshots carry custom SixAP values independently of
    // the name-keyed engine topology. A factory snapshot contains that
    // factory's values; a restored custom snapshot must retain its own.
    pushSixAPBrightnessTo(*newActive);
    newActive->snapSmoothersToTargets();

    // Inherit pre-tank input history (pre-delay buffer + ER signal state) from
    // the currently-active engine. Without this the new engine's ER taps fire
    // from silence over their 8-80 ms delay range, producing audible discrete
    // onsets the crossfade can't mask. Tank state stays cleared.
    // Only from a shared engine that has been rendering, into one that will:
    // while its own voice renders, a shared engine hears no input, so its history
    // is stale (from before the own voice took over, or from before a re-prepare),
    // and an engine that will sit idle has no use for one.
    if (! ownFor (activeEngine_).active && ! ownFor (newActive).active)
        newActive->copyInputHistoryFrom (*activeEngine_);

    // Retarget the shell's mix smoother to the new preset's value.
    mixSmoother_.setTarget (p (BusMode) >= 0.5f ? 1.0f : p (Mix));

    previousEngine_ = activeEngine_;
    activeEngine_   = newActive;

    constexpr float kFadeSeconds = 0.050f;
    presetFadeTotal_     = std::max (1, static_cast<int> (preparedSampleRate_ * kFadeSeconds));
    presetFadeRemaining_ = presetFadeTotal_;
    // The octave trim and the band limit crossfade over the same span (a swap to the setting
    // playing is none).
    octaveTrim_.swapTo (octaveTrimGainsFor (preset), presetFadeTotal_);
    bandLimit_.swapTo (bandLimitSettingFor (preset), presetFadeTotal_);
    stereoBands_.swapTo (stereoBandsFor (preset), presetFadeTotal_);

    syncParameterCacheToCurrent();
}

void DuskVerbDSP::processBlock (const float* const* in, float* const* out,
                                int numChannels, int numSamples) noexcept
{
    processBlock(in, out, numChannels, numChannels, numSamples);
}

void DuskVerbDSP::processBlock (const float* const* in, float* const* out,
                                int numInputs, int numChannels, int numSamples) noexcept
{
    duskaudio::ScopedFlushDenormals noDenormals;

    if (numSamples <= 0 || numInputs <= 0 || numChannels <= 0 || out == nullptr || in == nullptr)
        return;

    if (preparedBlockSize_ <= 0)
    {
        // Not prepared. The host contract says this cannot happen, but leaving
        // the output untouched would hand the host whatever was in the buffer;
        // pass the input through instead, which is the least surprising thing a
        // reverb can do with no state.
        const size_t passBytes = static_cast<size_t> (numSamples) * sizeof (float);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* source = in[std::min(ch, numInputs - 1)];
            if (out[ch] != nullptr && source != nullptr && out[ch] != source)
                std::memcpy (out[ch], source, passBytes);
        }
        return;
    }
    // Defensive clamp; the shell sizes the scratch to the host's declared maximum
    // block, but some hosts overshoot on tempo-sync edge cases. Cap at
    // preparedBlockSize_ so the worst case is a partially-processed block rather
    // than an overrun; the unprocessed tail is zeroed so the host never sees
    // stale data past the boundary. (Carried over from the JUCE processor.)
    if (numSamples > preparedBlockSize_)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            if (out[ch] != nullptr)
                std::memset (out[ch] + preparedBlockSize_, 0,
                             static_cast<size_t> (numSamples - preparedBlockSize_) * sizeof (float));
        numSamples = preparedBlockSize_;
    }

    float* const left  = workL_.data();
    float* const right = workR_.data();
    const size_t bytes = static_cast<size_t> (numSamples) * sizeof (float);

    // Promote mono input to stereo before any other processing (JUCE:
    // buffer.copyFrom (1, 0, buffer, 0, 0, numSamples)).
    std::memcpy (left,  in[0], bytes);
    std::memcpy (right, numInputs > 1 && in[1] != nullptr ? in[1] : in[0], bytes);

    // ---- Bypass ----
    const bool bypassOn = p (Bypass) >= 0.5f;
    if (bypassOn && bypassFade_ >= 1.0f)
    {
        // Fully bypassed and settled: bit-exact passthrough, meters still live.
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        const float dbL = gainToDecibels (peakL);
        const float dbR = gainToDecibels (peakR);
        inputLevelL_.store  (dbL, std::memory_order_relaxed);
        inputLevelR_.store  (dbR, std::memory_order_relaxed);
        outputLevelL_.store (dbL, std::memory_order_relaxed);
        outputLevelR_.store (dbR, std::memory_order_relaxed);
        writeOut (out, numChannels, left, right, numSamples);
        return;
    }

    // Input metering.
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        inputLevelL_.store (gainToDecibels (peakL), std::memory_order_relaxed);
        inputLevelR_.store (gainToDecibels (peakR), std::memory_order_relaxed);
    }

    // ---- Detect preset-apply request from the message thread ----
    // Acquire pairs with the release store in applyFactoryPresetConfig(). Gated
    // on presetFadeRemaining_ == 0: starting a new swap mid-fade would clear the
    // engine that is currently fading out, dropping its tail — audible as a
    // click on rapid preset cycling.
    if (presetFadeRemaining_ == 0
        && pendingPresetSwap_.exchange (false, std::memory_order_acquire))
        performPresetSwap();

    // ---- Push parameter changes to the engine on edges only ----
    const int algoIdx = algorithm();
    if (algoIdx != cachedAlgorithm_)
    {
        const bool own = isOwnAlgorithm (algoIdx);
        if (cachedAlgorithm_ >= 0 && own != ownFor (activeEngine_).active)
        {
            // A change of engine family crossfades like a preset swap: the outgoing
            // engine keeps its tail while the other member of the pair starts in
            // the new family. Mid-fade, cachedAlgorithm_ stays put and the next
            // block tries again, as a preset request would.
            if (presetFadeRemaining_ == 0)
                performPresetSwap();
        }
        else
        {
            cachedAlgorithm_ = algoIdx;
            if (own)
            {
                if (! ownFor (activeEngine_).active)
                    armOwnVoice (activeEngine_, true);     // a fresh start: nothing to fade
            }
            else
            {
                if (OwnVoice& voice = ownFor (activeEngine_); voice.active)
                {
                    // A fresh start back on the shared engine (after a re-prepare or a
                    // host reset): it sat idle while its own voice rendered, and a
                    // re-prepare does not clear it, so what it holds is stale.
                    voice.active = false;
                    activeEngine_->clearAllBuffers();
                }
                activeEngine_->setAlgorithm (algoIdx);
            }
        }
    }

    // Pre-delay (with optional tempo sync)
    float preDelayMs = p (Predelay);
    const int syncIndex = static_cast<int> (p (PredelaySync));
    if (syncIndex > 0)
    {
        static constexpr float kNoteBeats[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        const float beats = kNoteBeats[syncIndex - 1];
        if (hostBpmValid_ && hostBpm_ > 0.0)
            preDelayMs = std::clamp (60000.0f / static_cast<float> (hostBpm_) * beats, 0.0f, 250.0f);
    }
    if (preDelayMs != lastPreDelayMs_)
    {
        lastPreDelayMs_ = preDelayMs;
        activeEngine_->setPreDelay (preDelayMs);
    }

    auto pushIfChanged = [] (float& last, float current, auto setter) {
        if (current != last) { last = current; setter (current); }
    };

    // ── Macro / morph layer ── folded into the base values below so they layer
    // on every space. Tone = spectral tilt; Character = movement/grit. Defaults
    // (0, 0) -> factor 1.0 / +0.0 -> effective == base -> bit-null.
    const float tone = p (Tone);
    const float chr  = p (Character);
    const float toneTreble = toneTrebleFactor (tone);
    const float toneHiCut  = toneHiCutFactor (tone);
    const DampingKnobs knobs = dampingKnobs (toneTreble);

    pushIfChanged (lastDecaySec_,  engineDecay(), [this] (float v) { activeEngine_->setDecayTime (v); });
    pushIfChanged (lastSize_,      p (Size),   [this] (float v) { activeEngine_->setSize (v); });
    pushIfChanged (lastDamping_,   p (Damping) * toneTreble, [this] (float v) {
        activeEngine_->setTrebleMultiply (v);
        // setTrebleMultiply writes FDNReverb::trebleMultiply_, which the loop
        // never reads; setAirTrebleMultiply is what drives the per-line gHigh.
        activeEngine_->setAirTrebleMultiply (v);
    });
    pushIfChanged (lastBassMult_,  p (BassMult),  [this] (float v) { activeEngine_->setBassMultiply (v); });
    pushIfChanged (lastMidMult_,   p (MidMult),   [this] (float v) { activeEngine_->setMidMultiply (v); });
    pushIfChanged (lastSubMult_,   p (SubMult),   [this] (float v) { activeEngine_->setSubMultiply (v); });
    pushIfChanged (lastHiMidMult_, p (HiMidMult), [this] (float v) { activeEngine_->setHiMidMultiply (v); });
    pushIfChanged (lastCrossoverSub_, p (CrossoverSub), [this] (float v) { activeEngine_->setSubCrossoverFreq (v); });
    pushIfChanged (lastCrossoverAir_, p (CrossoverAir), [this] (float v) { activeEngine_->setAirCrossoverFreq (v); });
    pushIfChanged (lastShaperDepth_, p (TransientShaper), [this] (float v) { activeEngine_->setShaperDepth (v); });
    pushIfChanged (lastShaperTime_,  p (ShaperTime),  [this] (float v) { activeEngine_->setShaperTimeMs (v); });
    pushIfChanged (lastShaperXover_, p (ShaperXover), [this] (float v) { activeEngine_->setShaperXoverHz (v); });
    pushIfChanged (lastShaperSens_,  p (ShaperSens),  [this] (float v) { activeEngine_->setShaperSens (v); });
    pushIfChanged (lastInputSubGain_,  p (InputSubGain),  [this] (float v) { activeEngine_->setInputSubGainDb (v); });
    pushIfChanged (lastInputMidGain_,  p (InputMidGain),  [this] (float v) { activeEngine_->setInputMidGainDb (v); });
    pushIfChanged (lastInputHighGain_, p (InputHighGain), [this] (float v) { activeEngine_->setInputHighGainDb (v); });
    pushIfChanged (lastCrossover_,     p (Crossover),     [this] (float v) { activeEngine_->setCrossoverFreq (v); });
    pushIfChanged (lastHighCrossover_, p (HighCrossover), [this] (float v) { activeEngine_->setHighCrossoverFreq (v); });
    pushIfChanged (lastBassChoke_,     p (BassChoke),     [this] (float v) { activeEngine_->setBassChokeHz (v); });
    pushIfChanged (lastSaturation_,    std::clamp (p (Saturation) + chr * 0.35f, 0.0f, 1.0f), [this] (float v) { activeEngine_->setSaturation (v); });
    pushIfChanged (lastDiffusion_,     p (Diffusion),     [this] (float v) { activeEngine_->setDiffusion (v); });
    pushIfChanged (lastTonalCorr_,     p (TonalCorrection), [this] (float v) { activeEngine_->setTonalCorrection (v >= 0.5f); });
    pushIfChanged (lastModDepth_,      std::clamp (p (ModDepth) + chr * 0.5f, 0.0f, 1.0f), [this] (float v) { activeEngine_->setModDepth (v); });
    pushIfChanged (lastModRate_,       p (ModRate),       [this] (float v) { activeEngine_->setModRate (v); });
    pushIfChanged (lastTailSpinDepth_, p (TailSpinDepth), [this] (float v) { activeEngine_->setTailSpinDepth (v); });
    pushIfChanged (lastTailSpinRate_,  p (TailSpinRate),  [this] (float v) { activeEngine_->setTailSpinRate (v); });
    pushIfChanged (lastERSize_,        p (ErSize),        [this] (float v) { activeEngine_->setERSize (v); });
    pushIfChanged (lastERLevel_,       p (ErLevel),       [this] (float v) { activeEngine_->setERLevel (v); });
    pushIfChanged (lastERBoost_,       p (ErBoost),       [this] (float v) { activeEngine_->setEREarlyBoost (v); });
    // The QuadTank hi-mid and air multipliers override Treble above the high crossover; the
    // scaler carries the damping card into them (the raw values at the preset's own knobs).
    const DampingScaler& scaler = scalerFor (activeEngine_);
    pushIfChanged (lastQtHiMidMult_, scaler.quadMultiplier (p (QtHimidMult), DampingScaler::kQuadHiMidHz, knobs),
                   [this] (float v) { activeEngine_->setQuadHiMidMultiply (v); });
    pushIfChanged (lastQtAirMult_,   scaler.quadMultiplier (p (QtAirMult), DampingScaler::kQuadAirHz, knobs),
                   [this] (float v) { activeEngine_->setQuadAirMultiply (v); });
    pushIfChanged (lastERRise_,        p (ErRise),        [this] (float v) { activeEngine_->setEROnsetRiseMs (v); });
    // ER-bus shelves take both gains at once; push if EITHER changed.
    {
        const float lo = p (ErBusLowGain), hi = p (ErBusHighGain);
        if (lo != lastERBusLow_ || hi != lastERBusHigh_)
        {
            lastERBusLow_ = lo; lastERBusHigh_ = hi;
            activeEngine_->setERBusShelves (lo, hi);
        }
    }
    pushIfChanged (lastTankLevel_,   p (TankLevel),   [this] (float v) { activeEngine_->setTankOutputLevel (v); });
    pushIfChanged (lastTankSplitHz_, p (TankSplitHz), [this] (float v) { activeEngine_->setTankSplitHz (v); });
    pushIfChanged (lastERStereoNeutral_, p (ErStereoNeutral), [this] (float v) { activeEngine_->setERStereoNeutral (v >= 0.5f); });
    pushIfChanged (lastERDecorr_,    p (ErDecorr),    [this] (float v) { activeEngine_->setERDecorr (v); });
    pushIfChanged (lastXTalk_,       p (Xtalk),       [this] (float v) { activeEngine_->setOutputCrossTalk (v); });
    {
        const bool  mbEn = p (MbEnable) >= 0.5f;
        const float mbLo = p (MbLowDecay);
        const float mbMi = p (MbMidDecay);
        const float mbHi = p (MbHighDecay);
        if (mbEn != lastMbEnable_) { lastMbEnable_ = mbEn; activeEngine_->setMultibandEnabled (mbEn); }
        if (mbLo != lastMbLow_ || mbMi != lastMbMid_ || mbHi != lastMbHigh_)
        {
            lastMbLow_ = mbLo; lastMbMid_ = mbMi; lastMbHigh_ = mbHi;
            activeEngine_->setMultibandDecays (mbLo, mbMi, mbHi);
        }
    }
    pushIfChanged (lastLoCut_, p (LoCut), [this] (float v) { activeEngine_->setLoCut (v); });
    pushIfChanged (lastHiCut_, effectiveHiCut (p (HiCut), toneHiCut), [this] (float v) { activeEngine_->setHiCut (v); });
    pushIfChanged (lastHiCutShelfDb_, p (HiCutShelfDb), [this] (float v) { activeEngine_->setHiCutShelfGainDb (v); });

    pushIfChanged (lastPteqBand0Gain_, p (PteqBand0GainDb),
                   [this] (float v) { activeEngine_->setPostTankEQBand (0, pteqBandFreq_[0], pteqBandQ_[0], v); });
    pushIfChanged (lastPteqBand1Gain_, p (PteqBand1GainDb),
                   [this] (float v) { activeEngine_->setPostTankEQBand (1, pteqBandFreq_[1], pteqBandQ_[1], v); });
    pushIfChanged (lastPteqBand2Gain_, p (PteqBand2GainDb),
                   [this] (float v) { activeEngine_->setPostTankEQBand (2, pteqBandFreq_[2], pteqBandQ_[2], v); });
    pushIfChanged (lastPteqBand3Gain_, p (PteqBand3GainDb),
                   [this] (float v) { activeEngine_->setPostTankEQBand (3, pteqBandFreq_[3], pteqBandQ_[3], v); });

    pushIfChanged (lastPostBandSub_,    p (PostBandSubDb),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (0, v); });
    pushIfChanged (lastPostBandLowMid_, p (PostBandLowmidDb),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (1, v); });
    pushIfChanged (lastPostBandMidHi_,  p (PostBandMidhiDb),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (2, v); });
    pushIfChanged (lastPostBandAir_,    p (PostBandAirDb),
                   [this] (float v) { activeEngine_->setPostTankBandTrimGainDb (3, v); });

    auto pushEDT = [this] (int region, float& lastAtk, float& lastTau, int atkId, int tauId) {
        const float atk = p (atkId);
        const float tau = p (tauId);
        if (atk != lastAtk || tau != lastTau) {
            activeEngine_->setPerBandEDTShape (region, atk, tau);
            lastAtk = atk;
            lastTau = tau;
        }
    };
    pushEDT (0, lastEDTSubAtk_,    lastEDTSubTau_,    EdtSubAttackDb,    EdtSubTauMs);
    pushEDT (1, lastEDTLowMidAtk_, lastEDTLowMidTau_, EdtLowmidAttackDb, EdtLowmidTauMs);
    pushEDT (2, lastEDTMidHiAtk_,  lastEDTMidHiTau_,  EdtMidhiAttackDb,  EdtMidhiTauMs);
    pushEDT (3, lastEDTAirAtk_,    lastEDTAirTau_,    EdtAirAttackDb,    EdtAirTauMs);

    {
        const float pHz = p (InLoopPeakHz);
        const float pQ  = p (InLoopPeakQ);
        const float pDb = p (InLoopPeakDb);
        if (pHz != lastInLoopPeakHz_ || pQ != lastInLoopPeakQ_ || pDb != lastInLoopPeakDb_)
        {
            activeEngine_->setFDNInLoopPeaking (pHz, pQ, pDb);
            lastInLoopPeakHz_ = pHz;
            lastInLoopPeakQ_  = pQ;
            lastInLoopPeakDb_ = pDb;
        }
    }

    {
        const float fastFc = p (BassShelfFastFc);
        const float slowFc = p (BassShelfSlowFc);
        const float fastDb = p (BassShelfFastDb);
        const float slowDb = p (BassShelfSlowDb);
        const float trans  = p (BassShelfTransitionMs);
        if (fastFc != lastBassShelfFastFc_ || slowFc != lastBassShelfSlowFc_
            || fastDb != lastBassShelfFastDb_ || slowDb != lastBassShelfSlowDb_
            || trans  != lastBassShelfTransition_)
        {
            activeEngine_->setFDNDualBassShelf (fastFc, slowFc, fastDb, slowDb, trans);
            lastBassShelfFastFc_     = fastFc;
            lastBassShelfSlowFc_     = slowFc;
            lastBassShelfFastDb_     = fastDb;
            lastBassShelfSlowDb_     = slowDb;
            lastBassShelfTransition_ = trans;
        }
    }

    pushIfChanged (lastWidth_,     p (Width),     [this] (float v) { activeEngine_->setWidth (v); });
    pushIfChanged (lastGainTrim_,  p (GainTrim),  [this] (float v) { activeEngine_->setGainTrim (v); });
    if (OwnVoice& own = ownFor (activeEngine_); own.active)
        pushOwnParameters (own, preDelayMs, toneTreble, toneHiCut);
    if (fieldPostPlays (activeEngine_))
        pushFieldParameters (fieldFor (activeEngine_), toneHiCut);
    pushIfChanged (lastMonoBelow_, p (MonoBelow), [this] (float v) { activeEngine_->setMonoBelow (v); });
    pushIfChanged (lastMonoBelowDepth_, p (MonoBelowDepth), [this] (float v) { activeEngine_->setMonoBelowDepth (v); });

    pushIfChanged (lastDpvHfShelfDb_,    p (DpvHfShelfDb),      [this] (float v) { activeEngine_->setDpvHfShelfGainDb   (v); });
    pushIfChanged (lastDpvHfShelfHz_,    p (DpvHfShelfHz),      [this] (float v) { activeEngine_->setDpvHfShelfFreqHz   (v); });
    pushIfChanged (lastDpvStructHfDamp_, p (DpvStructHfDampHz), [this] (float v) { activeEngine_->setDpvStructHfDampHz  (v); });
    pushIfChanged (lastDpvBoxCutDb_,     p (DpvBoxCutDb),       [this] (float v) { activeEngine_->setDpvBoxCutGainDb    (v); });
    pushIfChanged (lastDpvBoxCutHz_,     p (DpvBoxCutHz),       [this] (float v) { activeEngine_->setDpvBoxCutFreqHz    (v); });
    pushIfChanged (lastDpvBassShelfDb_,  p (DpvBassShelfDb),    [this] (float v) { activeEngine_->setDpvBassShelfGainDb (v); });
    pushIfChanged (lastDpvBassShelfHz_,  p (DpvBassShelfHz),    [this] (float v) { activeEngine_->setDpvBassShelfFreqHz (v); });

    // The per-band decay tables the damping card does not reach on its own.
    if (! ownFor (activeEngine_).active)
        scalerFor (activeEngine_).update (*activeEngine_, algoIdx, knobs, numSamples);

    // Mix: bus_mode forces 100 % wet. The mix smoother lives on the shell so the
    // dry is added AFTER any preset crossfade and stays correlated across it.
    const bool busMode = p (BusMode) >= 0.5f;
    const float mixVal = busMode ? 1.0f : p (Mix);
    pushIfChanged (lastMix_, mixVal, [this] (float v) { mixSmoother_.setTarget (v); });

    const bool freezeNow = p (Freeze) >= 0.5f;
    if (! haveLastFreeze_ || freezeNow != lastFreeze_)
    {
        activeEngine_->setFreeze (freezeNow);
        lastFreeze_     = freezeNow;
        haveLastFreeze_ = true;
    }

    const bool gateEnabledNow = p (GateEnabled) >= 0.5f;
    if (! haveLastGateEnabled_ || gateEnabledNow != lastGateEnabled_)
    {
        activeEngine_->setNonLinearGateEnabled (gateEnabledNow);
        lastGateEnabled_     = gateEnabledNow;
        haveLastGateEnabled_ = true;
    }

    // Save the dry input BEFORE either engine consumes it. The engines output
    // WET-ONLY, so the dry/wet mix is applied here after the crossfade — this
    // keeps the dry correlated across a swap and eliminates the +3 dB midpoint
    // swell that equal-power blending produced when each engine had its own dry.
    std::memcpy (dryBufL_.data(), left,  bytes);
    std::memcpy (dryBufR_.data(), right, bytes);

    // The early fields' input takes the block before either engine reads it, whichever field
    // plays. Frozen, it fades to silence (and back after): the fields hear nothing new while the
    // engine holds its tail, and drain over their windows. An engine that cannot hold its tail
    // (algorithmFreezes) keeps its field listening too, so Freeze acts on all of it or nothing.
    earlyInput_.write (left, right, numSamples, freezeNow && algorithmFreezes (algorithm()));

    const bool fading = (presetFadeRemaining_ > 0 && previousEngine_ != nullptr);
    if (fading)
    {
        std::memcpy (fadeBufL_.data(), left,  bytes);
        std::memcpy (fadeBufR_.data(), right, bytes);
    }

    renderWet (activeEngine_, left, right, numSamples);

    // Where this block sits in the preset fade, for the early fields' own crossfade below
    // (the engine fade that follows moves it on).
    const DuskVerbEngine* const fadingOut = fading ? previousEngine_ : nullptr;
    const int fadeIndex = presetFadeTotal_ - presetFadeRemaining_;

    if (fading)
    {
        // Run the saved input through the previous engine so its tail keeps
        // evolving instead of being a static snapshot, then equal-power
        // crossfade the two WET outputs.
        renderWet (previousEngine_, fadeBufL_.data(), fadeBufR_.data(), numSamples);

        const int samplesToCrossfade = std::min (numSamples, presetFadeRemaining_);
        // Denominator is (total - 1) so the LAST fade sample lands at t=1 exactly
        // (gOld=0); using `total` leaves a residual that drops to 0 in one sample
        // post-fade — audible as a tick on long sustained tails.
        const int spanDen = std::max (1, presetFadeTotal_ - 1);
        const float invSpan = 1.0f / static_cast<float> (spanDen);
        const int idxBase = presetFadeTotal_ - presetFadeRemaining_;

        for (int i = 0; i < samplesToCrossfade; ++i)
        {
            const float t    = std::min (1.0f, static_cast<float> (idxBase + i) * invSpan);
            const float gNew = std::sin (t * kHalfPi);
            const float gOld = std::cos (t * kHalfPi);
            left[i]  = left[i]  * gNew + fadeBufL_[static_cast<size_t> (i)] * gOld;
            right[i] = right[i] * gNew + fadeBufR_[static_cast<size_t> (i)] * gOld;
        }

        presetFadeRemaining_ -= samplesToCrossfade;
        if (presetFadeRemaining_ <= 0)
        {
            previousEngine_      = nullptr;
            presetFadeRemaining_ = 0;
        }
        // Samples past samplesToCrossfade are pure activeEngine_ wet — leave them.
    }

    // The early fields' post paths, through their output stages and their own (linear) fade.
    addEarlyFields (left, right, numSamples, fadingOut, fadeIndex);

    // The preset's band width stage on the summed wet. No row leaves it untouched.
    stereoBands_.process (left, right, numSamples);

    // The preset's octave trim on the summed wet. A flat one leaves it untouched.
    octaveTrim_.process (left, right, numSamples);

    // The preset's band limit after it. No row leaves the wet untouched.
    bandLimit_.process (left, right, numSamples);

    // ---- Wet ducking ----
    // Sidechained off the dry snapshot. Depth 0 -> ReverbDucker early-outs and
    // the wet is untouched, so non-ducked presets stay bit-identical.
    {
        ducker_.setDepth (p (Duck));
        ducker_.process (dryBufL_.data(), dryBufR_.data(), left, right, numSamples);
    }

    // ---- Dry/wet mix ----
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float mix = mixSmoother_.next();
            const float wetGain = std::sin (mix * kHalfPi);
            const float dryGain = std::cos (mix * kHalfPi);
            const size_t idx = static_cast<size_t> (i);
            left[i]  = dryBufL_[idx] * dryGain + left[i]  * wetGain;
            right[i] = dryBufR_[idx] * dryGain + right[i] * wetGain;
        }
    }

    // ---- Bypass crossfade ----
    // Untouched when the plugin has never been bypassed: bypassFade_ stays 0 and
    // the target is 0, so the loop is skipped entirely and the samples above are
    // the final output, bit for bit. dryBuf* still holds the unprocessed input.
    if (bypassOn || bypassFade_ > 0.0f)
    {
        const float target = bypassOn ? 1.0f : 0.0f;
        float f = bypassFade_;
        for (int i = 0; i < numSamples; ++i)
        {
            f = target > f ? std::min (target, f + bypassFadeStep_)
                           : std::max (target, f - bypassFadeStep_);
            const size_t idx = static_cast<size_t> (i);
            left[i]  = left[i]  + (dryBufL_[idx] - left[i])  * f;
            right[i] = right[i] + (dryBufR_[idx] - right[i]) * f;
        }
        bypassFade_ = f;
    }

    // Output metering.
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max (peakL, std::abs (left[i]));
            peakR = std::max (peakR, std::abs (right[i]));
        }
        const float dbL = gainToDecibels (peakL);
        const float dbR = gainToDecibels (peakR);
        outputLevelL_.store (dbL, std::memory_order_relaxed);
        outputLevelR_.store (dbR, std::memory_order_relaxed);
    }

    writeOut (out, numChannels, left, right, numSamples);
}

void DuskVerbDSP::writeOut (float* const* out, int numChannels,
                            const float* left, const float* right, int numSamples) noexcept
{
    const size_t bytes = static_cast<size_t> (numSamples) * sizeof (float);
    if (out[0] != nullptr) std::memcpy (out[0], left, bytes);
    if (numChannels > 1 && out[1] != nullptr) std::memcpy (out[1], right, bytes);
    // Anything above the second output channel is silence, as the JUCE build's
    // buffer.clear() loop did.
    for (int ch = 2; ch < numChannels; ++ch)
        if (out[ch] != nullptr) std::memset (out[ch], 0, bytes);
}

} // namespace duskverb
