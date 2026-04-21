#pragma once

#include "AlgorithmConfig.h"
#include "DattorroTank.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "OutputDiffusion.h"
#include "QuadTank.h"
#include "OnsetBurst.h"
#include "StereoDecorrelator.h"
#include "TailChorus.h"
#include "TiledRoomReverb.h"
#include "presets/PresetEngineBase.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// One-pole exponential smoother for per-sample parameter interpolation.
// Prevents zipper noise when parameters change between processing sub-blocks.
struct OnePoleSmoother
{
    float current = 0.0f;
    float target = 0.0f;
    float coeff = 0.0f;

    void reset (float value) { current = target = value; }

    void setSmoothingTime (double sampleRate, float timeMs)
    {
        coeff = std::exp (-1000.0f / (std::max (timeMs, 0.1f)
                                      * static_cast<float> (sampleRate)));
    }

    void setTarget (float t) { target = t; }

    float next()
    {
        current = target + coeff * (current - target);
        return current;
    }
};

class DuskVerbEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);

    void setAlgorithm (int index);
    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setPreDelay (float milliseconds);
    void setDiffusion (float amount);
    void setOutputDiffusion (float amount);
    void setERLevel (float level);
    void setERSize (float size);
    void setMix (float dryWet);
    void setLoCut (float hz);
    void setHiCut (float hz);
    void setWidth (float width);
    void setFreeze (bool frozen);
    void setGateParams (float holdMs, float releaseMs);
    void setGainTrim (float dB);
    void setInputOnset (float ms);
    void loadCorrectionFilter (int presetIndex);
    void loadPresetTapPositions (int presetIndex);
    void setDattorroDelayScale (float scale);
    void setDattorroSoftOnsetMs (float ms);
    void setLateFeedForwardLevel (float level);
    void resetLateFeedForwardLevel();  // Restore algorithm config default
    void setDattorroLimiter (float thresholdDb, float releaseMs);
    void updateTapPositions (float l0, float l1, float l2, float l3, float l4, float l5, float l6,
                             float r0, float r1, float r2, float r3, float r4, float r5, float r6);
    void updateTapPositionsAndGains (float l0, float l1, float l2, float l3, float l4, float l5, float l6,
                                     float r0, float r1, float r2, float r3, float r4, float r5, float r6,
                                     float gl0, float gl1, float gl2, float gl3, float gl4, float gl5, float gl6,
                                     float gr0, float gr1, float gr2, float gr3, float gr4, float gr5, float gr6);
    // Apply gains on top of existing tap positions (does not change positions)
    void applyTapGains (float gl0, float gl1, float gl2, float gl3, float gl4, float gl5, float gl6,
                        float gr0, float gr1, float gr2, float gr3, float gr4, float gr5, float gr6);
    void setCustomERTaps (const CustomERTap* taps, int numTaps);
    void loadPresetERTaps (const char* presetName);  // Looks up VV-extracted taps by name

    // --- Optimizer-tunable overrides (runtime parameter → sub-component forwarding) ---
    void setAirDampingOverride (float scale);
    void setHighCrossoverOverride (float hz);
    void setNoiseModOverride (float samples);
    void setInlineDiffusionOverride (float coeff);
    void setStereoCouplingOverride (float amount);
    void setChorusDepthOverride (float depth);
    void setChorusRateOverride (float hz);
    void setOutputGainOverride (float gain);
    void setERCrossfeedOverride (float amount);
    void setDecayTimeScaleOverride (float scale);
    void setDecayBoostOverride (float dB);
    void setStructuralHFDampingOverride (float hz);
    void setOutputLowShelfOverride (float dB);
    void setOutputHighShelfOverride (float dB, float hz);
    void setOutputMidEQOverride (float dB, float hz);
    void setTerminalDecayOverride (float thresholdDb, float factor);
    void setERAirCeilingOverride (float hz);
    void setERAirFloorOverride (float hz);

private:
    DiffusionStage diffuser_;
    FDNReverb fdn_;
    DattorroTank dattorroTank_;
    OutputDiffusion outputDiffuser_;
    EarlyReflections er_;
    bool useDattorroTank_ = false;
    bool useQuadTank_ = false;
    QuadTank quadTank_;
    QuadTank hybridQuadTank_;  // Dedicated secondary engine for hybrid dual-engine mode
    TiledRoomReverb tiledRoomReverb_;  // Legacy engine — process path removed, setters kept for parameter forwarding

    // Per-preset engines: every registered preset engine is constructed
    // and prepared once during DuskVerbEngine::prepare() (off the audio
    // thread) and stored in this map. setAlgorithm() pre-resolves the
    // target engine pointer so applyAlgorithm() can swap it without
    // locking prebuiltPresetEnginesMutex_ on the audio thread.
    std::unordered_map<std::string, std::unique_ptr<PresetEngineBase>> prebuiltPresetEngines_;
    std::mutex prebuiltPresetEnginesMutex_;  // Protects map mutations; never held on audio thread
    std::atomic<PresetEngineBase*> presetEngine_ { nullptr };  // non-owning; written in applyAlgorithm (audio thread)

    // Pending algorithm handoff: atomic struct ensures the audio thread reads a
    // consistent (algorithmIndex, engine*, presetClearDone) triple even when
    // setAlgorithm() is called repeatedly from the message thread.
    //
    // Fields are ordered so the struct packs into 16 bytes — the largest size
    // x86-64 (CMPXCHG16B) and ARM64 (LDP/STP) handle lock-free in hardware. A
    // 24-byte layout would fall back to libc++'s internal lock pool, which we
    // must not touch on the audio thread.
    //
    // `is_always_lock_free` is a stricter compile-time guarantee than we need
    // (libc++ returns false for any struct >8 bytes regardless of platform), so
    // we verify with a runtime check in prepare() on each build instead.
    struct PendingSwap
    {
        PresetEngineBase* engine = nullptr;  // 8 B
        int algorithmIndex = -1;             // 4 B
        bool presetClearDone = false;        // 1 B + 3 B pad → total 16 B
    };
    static_assert (sizeof (PendingSwap) == 16,
                   "PendingSwap must pack to 16 bytes so hardware atomics stay lock-free");
    static_assert (std::is_trivially_copyable<PendingSwap>::value,
                   "PendingSwap must be trivially copyable for std::atomic");
    std::atomic<PendingSwap> pendingSwap_ { PendingSwap { nullptr, -1, false } };
    std::atomic<unsigned> pendingSwapSeq_ { 0 };            // Kept for ABI compat; no longer load-bearing
    std::atomic<bool> pendingPresetClear_ { false }; // Deferred clearBuffers() fallback (same-engine case only)

    const AlgorithmConfig* config_ = &kHall;

    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;

    // Hybrid dual-engine: secondary engine scratch buffers
    std::vector<float> hybridL_;
    std::vector<float> hybridR_;
    float hybridBlend_ = 0.0f;
    const AlgorithmConfig* hybridConfig_ = nullptr;
    std::vector<float> preDiffL_;
    std::vector<float> preDiffR_;

    std::vector<float> preDelayBufL_;
    std::vector<float> preDelayBufR_;
    int preDelayWritePos_ = 0;
    int preDelayMask_ = 0;
    int preDelaySamples_ = 0;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 0;

    // Per-sample smoothed output parameters (prevents zipper noise on fast automation)
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother erLevelSmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother loCutSmoother_;
    OnePoleSmoother hiCutSmoother_;

    float erLevelScale_ = 1.0f;
    float lateGainScale_ = 1.0f;
    float decayGainComp_ = 1.0f; // Decay-dependent output compensation (short-decay boost)
    float erCrossfeed_ = 0.0f;
    OnePoleSmoother outputGainSmoother_; // Per-algorithm output gain (smoothed to avoid clicks)

    float decayTime_ = 2.5f; // Cached for decay-linked output diffusion
    float gainTrimLinear_ = 1.0f; // Per-preset level correction (linear multiplier from dB)

    // Per-preset output peak limiter (applied after late gain, before output diffuser)
    float outputLimiterThreshold_ = 0.0f; // 0 = disabled. Linear amplitude.
    float outputLimiterRelease_ = 0.999f; // One-pole release (~30ms at 48kHz)
    float outputLimiterEnv_ = 0.0f;       // Current peak envelope

    // Cached raw param values for re-application after algorithm switch
    float lastDiffusion_ = 0.75f;
    float lastOutputDiffusion_ = 0.5f;
    float lastModDepth_ = 0.4f;
    float lastModRate_ = 0.8f;
    float lastTrebleMult_ = 0.5f;
    float lastBassMult_ = 1.2f;
    float lastERLevel_ = 0.5f;
    float lastSize_ = 0.5f;
    float lastCrossoverHz_ = 1000.0f;
    float lastDecayBoost_ = 1.0f;

    // Input bandwidth filter (Dattorro-style one-pole LP, ~10kHz)
    float inputBwCoeff_ = 0.0f;
    float inputBwStateL_ = 0.0f;
    float inputBwStateR_ = 0.0f;

    // DC blocker (first-order highpass, ~5Hz cutoff)
    float dcCoeff_ = 0.9993f;
    float dcX1L_ = 0.0f, dcY1L_ = 0.0f;
    float dcX1R_ = 0.0f, dcY1R_ = 0.0f;

    // Output EQ: second-order Butterworth biquads (wet signal only)
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f;
        float z1R = 0.0f, z2R = 0.0f;

        float processL (float x)
        {
            float y = b0 * x + z1L;
            z1L = b1 * x - a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        float processR (float x)
        {
            float y = b0 * x + z1R;
            z1R = b1 * x - a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1L = z2L = z1R = z2R = 0.0f; }
    };

    Biquad loCutFilter_;
    Biquad hiCutFilter_;
    Biquad hiCutFilter2_;  // Second cascade stage: 4-pole (24 dB/oct) total for vintage anti-aliasing
    float loCutHz_ = 20.0f;
    float hiCutHz_ = 20000.0f;
    void updateLoCutCoeffs();
    void updateHiCutCoeffs();

    // Per-algorithm output EQ: low shelf at 250Hz + mid parametric + high shelf.
    Biquad lowShelfFilter_;
    Biquad highShelfFilter_;
    Biquad midEQFilter_;
    bool lowShelfEnabled_ = false;
    bool highShelfEnabled_ = false;
    bool midEQEnabled_ = false;
    void updateOutputEQCoeffs();

    // Anti-alias filtering is handled inside the FDN feedback loop (first-order LP at ~17kHz).
    // Accumulates naturally across iterations — no output-stage filter needed.

    // Freeze mode
    bool frozen_ = false;

    // Saturation: when false, bypass fastTanh for clean linear output
    bool enableSaturation_ = false;

    // RMS-tracking peak limiter: reduces crest factor by limiting instantaneous
    // peaks relative to short-term RMS. Targets FDN's Hadamard peak correlation.
    float crestLimitRatio_ = 0.0f;   // 0 = disabled, 1.5 = limit peaks to 1.5× RMS
    static constexpr float kRmsWindowMs = 5.0f;
    int rmsWindowSamples_ = 0;
    std::vector<float> rmsBufferL_;
    std::vector<float> rmsBufferR_;
    float rmsSumL_ = 0.0f;
    float rmsSumR_ = 0.0f;
    int rmsIndex_ = 0;

    // Tail chorus: stereo amplitude modulation on reverb tail
    TailChorus tailChorus_;
    OnsetBurst onsetBurst_;
    StereoDecorrelator stereoDecorr_;

    // Terminal decay: when reverb tail drops below threshold, accelerate decay
    float terminalThresholdDb_ = 0.0f; // 0 = disabled
    float terminalFactor_ = 0.992f;

    // Structural HF damping override: -1.0f means "no override, use config default".
    // Cached so applyAlgorithm() can replay it after the config-baseline write.
    float structuralHFOverrideHz_ = -1.0f;

    // Cached runtime overrides: -1.0f sentinel means "no override, use config default".
    // Stored by the override setters and replayed after applyAlgorithm() writes
    // config-baseline values, so optimizer/host overrides persist across algorithm switches.
    float cachedOutputGainOverride_ = -1.0f;
    float cachedErCrossfeedOverride_ = -1.0f;
    float cachedAirDampingOverride_ = -1.0f;
    float cachedHighCrossoverOverride_ = -1.0f;
    float cachedNoiseModOverride_ = -1.0f;
    float cachedLateGainScaleOverride_ = -1.0f;
    float cachedErLevelScaleOverride_ = -1.0f;

    // Decay time scale override: runtime multiplier (0 = use config default)
    float decayTimeScaleOverride_ = 0.0f;

    // Cached overrides for setters that mutate live DSP state without caching.
    // Sentinel values chosen to avoid colliding with valid parameter ranges.
    float cachedInlineDiffusionOverride_ = -1.0f;   // valid range >= 0
    float cachedStereoCouplingOverride_ = -2.0f;     // valid range -1..+1, sentinel <= -1.5
    float cachedERAirCeilingOverride_ = -1.0f;       // valid range > 0
    float cachedERAirFloorOverride_ = -1.0f;         // valid range > 0
    float cachedOutputLowShelfDb_ = 0.0f;            // 0 dB = no override (bypass)
    float cachedOutputHighShelfDb_ = 0.0f;           // 0 dB = no override (bypass)
    float cachedOutputHighShelfHz_ = 8000.0f;
    float cachedOutputMidEQDb_ = 0.0f;               // 0 dB = no override (bypass)
    float cachedOutputMidEQHz_ = 1000.0f;

    // Late feed-forward: pre-diffusion late reverb blended into output
    float lateFeedForwardLevel_ = 0.0f;

    // Late reverb onset envelope: ramps FDN output from 0→1 to match VV's
    // slower density buildup (parallel FDN inherently produces too much early energy)
    float lateOnsetRamp_ = 1.0f;      // Current envelope value (0→1)
    float lateOnsetIncrement_ = 0.0f; // Per-sample increment (1.0 / rampSamples)
    int   lateOnsetSamples_ = 0;      // Total ramp duration in samples

    // Late-bloom envelope: transient-triggered unity→peak→unity curve applied to the wet path.
    // Compensates for DV's architectural inability to reproduce VV's late-bloom/swell signature
    // (where tail RMS equals or exceeds body RMS).
    // Per-band variant: when any lateBloomBandOffset_[i] is non-zero, the wet signal is split
    // into 4 bands (80-315, 315-1250, 1250-5000, 5000-12000 Hz) via cascaded 1-pole LPFs,
    // each band receives its own envelope gain, then bands are summed.
    float lateBloomLevel_ = 0.0f;             // Peak gain above unity (0 = disabled)
    int   lateBloomDelaySamples_ = 0;
    int   lateBloomAttackSamples_ = 0;
    int   lateBloomHoldSamples_ = 0;
    int   lateBloomReleaseSamples_ = 0;
    int   lateBloomPhaseSamples_ = -1;        // -1 = idle, >=0 = samples since trigger
    bool  lateBloomInputWasQuiet_ = true;
    bool  lateBloomPerBand_ = false;          // set when any band offset is non-zero
    float lateBloomBandLevel_[4] = { 0, 0, 0, 0 };  // effective per-band level
    float bloomSplitA_[3] = { 0, 0, 0 };      // LPF coefficients at 315, 1250, 5000 Hz
    // LR2 band-split: each crossover is two 1-pole LPFs in series (12 dB/oct),
    // so 6 state vars per channel — stages [0..2] are the first pass, [3..5] the second.
    float bloomSplitStateL_[6] = { 0, 0, 0, 0, 0, 0 };
    float bloomSplitStateR_[6] = { 0, 0, 0, 0, 0, 0 };

    // Body-bloom envelope: body-window counterpart to lateBloom. Shapes the 50–200 ms
    // body region to fix body-specific buildup (e.g. hi_body_vs_onset too loud,
    // mid_body_vs_onset too thin) that lateBloom's 150 ms delay can't reach.
    float bodyBloomLevel_ = 0.0f;
    int   bodyBloomDelaySamples_ = 0;
    int   bodyBloomAttackSamples_ = 0;
    int   bodyBloomHoldSamples_ = 0;
    int   bodyBloomReleaseSamples_ = 0;
    int   bodyBloomPhaseSamples_ = -1;
    bool  bodyBloomPerBand_ = false;
    float bodyBloomBandLevel_[4] = { 0, 0, 0, 0 };
    // Body-bloom and tail-bloom share the same band-split filters (bloomSplitA_/State)
    // when both are active in the same block — they don't overlap in time so states
    // remain consistent across the transition.

    // Tail-notch filter: envelope-modulated peaking biquad applied to wet path.
    // Coefficients computed once at applyAlgorithm from (freq, Q, gainDb). Envelope
    // blends between bypass (env=0, original wet) and full filtered (env=1).
    bool  tailNotchEnabled_ = false;
    Biquad tailNotchFilter_;
    int   tailNotchDelaySamples_ = 0;
    int   tailNotchAttackSamples_ = 0;
    int   tailNotchHoldSamples_ = 0;
    int   tailNotchReleaseSamples_ = 0;
    int   tailNotchPhaseSamples_ = -1;
    bool  tailNotchInputWasQuiet_ = true;

    // Per-preset spectral correction filter: 5-stage biquad cascade that shapes
    // DattorroTank output to match VV's exact frequency response.
    // Coefficients generated offline by generate_correction_filters.py.
    static constexpr int kNumCorrectionStages = 8;
    Biquad correctionFilter_[kNumCorrectionStages] {};
    bool correctionFilterActive_ = false;

    // Fix 4 (spectral): ER corrective EQ — applies the same per-preset 12-band
    // correction to the ER path so the combined ER+late signal is spectrally matched.
    // Coefficients are queried from the active PresetEngineBase at algorithm switch.
    static constexpr int kMaxERCorrBands = 16;
    Biquad erCorrEQ_[kMaxERCorrBands] {};
    int erCorrEQBands_ = 0;
    bool erCorrEQActive_ = false;

    // Fix 1 (decay_ratio): per-preset onset envelope table — replaces the simple
    // squared linear ramp with a VV-derived energy buildup curve.
    const float* onsetEnvelopeTable_ = nullptr;
    int onsetEnvelopeTableSize_ = 0;
    float onsetEnvelopePhase_ = 0.0f;   // 0→1 progress through table
    float onsetEnvelopeInc_ = 0.0f;     // per-sample increment
    bool useOnsetTable_ = false;
    bool onsetInputWasQuiet_ = true;  // Tracks silence→signal transition for re-triggering

    // Gate envelope: truncates reverb tail (gated reverb effect)
    bool gateEnabled_ = false;
    int gateHoldSamples_ = 0;
    float gateReleaseCoeff_ = 0.0f;
    float gateEnvelope_ = 0.0f;
    int gateHoldCounter_ = 0;
    bool gateTriggered_ = false;

    // Tail floor gate: envelope-follower hard-zero. Zero output when the tracked
    // envelope falls below tailFloorGateThreshold_. Fast attack keeps active signal
    // through, exponential release matches natural decay so no click.
    bool tailFloorGateEnabled_ = false;
    float tailFloorGateThreshold_ = 0.0f;
    float tailFloorGateReleaseCoeff_ = 0.999f;
    float tailFloorGateEnvL_ = 0.0f;
    float tailFloorGateEnvR_ = 0.0f;

    // Algorithm crossfade: mute-and-morph to prevent clicks on algorithm switch.
    // These fields are accessed from setAlgorithm() (message thread) and
    // process() (audio thread), so they use atomics to avoid data races.
    static constexpr int kFadeSamples = 64;
    // pendingAlgorithm_ removed — now part of pendingSwap_ seqlock triple
    std::atomic<int> fadeCounter_ { 0 };
    std::atomic<bool> fadingOut_ { false };
    std::atomic<bool> firstAlgorithmSet_ { true };
    bool fadeInArmed_ = false;  // One-shot: delays fade-in to next process() block after algorithm swap

    void applyAlgorithm (int index, PresetEngineBase* resolvedEngine, bool alreadyCleared);
};
