// TapeEchoDSP.hpp
// Component-modeled vintage three-head tape echo with spring reverb.
//
// Framework-free: standard C++17 only. No JUCE, no host dependencies.
// Designed to be wrapped by DPF, raw CLAP, or any other plugin shell.
//
// Signal flow (stereo input, mono wet paths, stereo dry path):
//
//   L+R ─► record preamp (odd soft clip + DC block) ───────┬──► reverb send
//                                                         │
//        ┌────────────────────────────────────────────────┘
//        ▼
//   [+]──► record EQ (HP + speed-dependent LP) ──► tape saturation ──► TAPE
//    ▲                                                                │
//    │                                                     3 read heads (Hermite)
//    │                                                     T · {1.00, 1.90, 2.75}
//    └── intensity · softClip(head sum)  ◄─────────────────┴──┐
//                                                             ▼
//                                    bass/treble shelves (echo path only)
//                                                             ▼
//   out L/R = dry·input L/R + pan(echoLevel·echoEQ)
//                           + pan(reverbLevel·spring(reverb send))
//
// The record EQ and tape saturation live INSIDE the feedback loop, so each
// repeat progressively darkens and compresses — this is what lets high
// Intensity settings bloom into stable, warm self-oscillation instead of
// exploding numerically.

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Shared DSP primitives (namespace duskaudio) — these replace the building
// blocks that used to be defined locally here. All lifted from this very file
// during the shared-dpf extraction, so consuming them is bit-identical.
#include "DuskSmoothed.hpp"    // SmoothedValue
#include "DuskFilters.hpp"     // OnePoleLP/HP, DCBlocker, Biquad (shelves)
#include "DuskOversampler.hpp" // HalfbandFIR, hbtaps::kA/kB

namespace duskaudio
{

//==============================================================================
// Small framework-free building blocks now live in plugins/shared-dpf/dsp:
//   SmoothedValue            -> DuskSmoothed.hpp
//   OnePoleLP / OnePoleHP    -> DuskFilters.hpp
//   DCBlocker                -> DuskFilters.hpp (default R = 0.9975, ~20 Hz @ 48k;
//                               tape-echo keeps the default, so it is unchanged)
//   ShelfFilter              -> DuskFilters.hpp Biquad + Biquad::shelfSlope1()
//   HalfbandFIR + hb taps    -> DuskOversampler.hpp (HalfbandFIR, hbtaps::kA/kB)
// The shared versions were lifted from this file, so behavior is bit-identical.
//==============================================================================

//==============================================================================
// Spring reverb — simplified four-path dispersive tank.
//
// Each spring path is a feedback delay loop containing a long chain of
// identical first-order allpasses. The chain is dispersive (group delay falls with
// frequency), so every trip around the loop smears transients into the
// characteristic downward "boing" chirp. Damping in the loop keeps it dark;
// slow delay modulation keeps the tail from ringing statically.
//==============================================================================
class SpringReverb
{
public:
    void prepare(double sampleRate, float detune /* per-channel length scale */);
    void reset();
    float process(float in) noexcept;

private:
    struct Allpass1
    {
        float a = 0.63f, z = 0.0f;
        float process(float x) noexcept
        {
            const float y = -a * x + z;
            z = x + a * y;
            return y;
        }
    };

    static constexpr int kNumAllpasses = 36;

    struct Spring
    {
        std::vector<float>                       buf;
        std::vector<float>                       feedbackBuf;
        int                                      len = 0, writeIdx = 0;
        int                                      feedbackWriteIdx = 0;
        float                                    feedback = 0.0f;
        float                                    lfoPhase = 0.0f, lfoInc = 0.0f, lfoDepth = 0.0f;
        std::array<Allpass1, kNumAllpasses>      chain;
        OnePoleLP                                damping;
        Biquad                                   feedbackHighPass;
        Biquad                                   highDamping;
        Biquad                                   airDamping;
        Biquad                                   upperModeDamping;

        void  prepare(double fs, float lengthSeconds, float fbAmount,
                      float lfoHz, float apCoeff);
        void  reset();
        float process(float x) noexcept;
    };

    std::array<Spring, 4> springs;
    std::vector<float> pickupBuf;
    std::vector<float> pickupSpring3Buf;
    int pickupWriteIdx = 0;
    int pickupTap8 = 1, pickupTap18 = 1, pickupTap28 = 1;
    int pickupSpring3Tap85 = 1;
    int pickupSpring3Tap20 = 1;
    int pickupSpring3Tap285 = 1;
    std::vector<float> outputDiffusionBuf;
    int outputDiffusionWriteIdx = 0;
    OnePoleHP inputHP;   // springs don't transmit deep lows
    OnePoleLP inputLP;   // dark transducer voicing
    DCBlocker dcBlock;
    Biquad outputHP;
    Biquad outputVoiceLP;
    std::array<Biquad, 9> outputCeilingLP;
    Biquad outputLowContour;
    Biquad outputBody;
    Biquad outputPresence;
    float pickupImagePhase = 0.0f;
    float pickupImagePhaseInc = 0.0f;
};

//==============================================================================
// TapeEchoDSP — the complete tape echo core.
//==============================================================================
class TapeEchoDSP
{
public:
    static constexpr int kNumModes    = 12;
    static constexpr int kMaxChannels = 2;

    // Hosted head-1 arrival endpoints. These include the record/playback path
    // delay so rendered repeats align with the reference, not just the nominal
    // motor labels.
    static constexpr float kMinDelayMs = 69.83f;
    static constexpr float kMaxDelayMs = 178.50f;

    // Mechanically fixed head spacing ratios.
    static constexpr float kHeadRatio[3] = { 1.0f, 1.90f, 2.75f };

    TapeEchoDSP() = default;

    //--- lifecycle -----------------------------------------------------------
    // Allocates. Call from the main/setup thread, never the audio thread.
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    //--- processing ----------------------------------------------------------
    // inputs/outputs: arrays of channel pointers; in-place (inputs == outputs)
    // is supported. numChannels is clamped to [1, 2]. Real-time safe: no
    // allocation, no locks, no I/O.
    void processBlock(const float* const* inputs, float* const* outputs,
                      int numChannels, int numSamples) noexcept;

    //--- parameters (thread-safe: callable from any thread) -------------------
    void setMode(int mode1to12) noexcept          { pMode.store(clampInt(mode1to12, 1, 12), std::memory_order_relaxed); }
    void setRepeatRate(float v01) noexcept        { pRepeatRate.store(clamp01(v01), std::memory_order_relaxed); }  // 0 = slow motor (177 ms), 1 = fast (69 ms)
    void setIntensity(float v01) noexcept         { pIntensity.store(clamp01(v01), std::memory_order_relaxed); }   // > ~0.75 self-oscillates
    void setEchoLevel(float v01) noexcept         { pEchoLevel.store(clamp01(v01), std::memory_order_relaxed); }
    void setReverbLevel(float v01) noexcept       { pReverbLevel.store(clamp01(v01), std::memory_order_relaxed); }
    void setDryLevel(float v01) noexcept          { pDryLevel.store(clamp01(v01), std::memory_order_relaxed); }
    void setBass(float vMinus1to1) noexcept       { pBass.store(clampF(vMinus1to1, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setTreble(float vMinus1to1) noexcept     { pTreble.store(clampF(vMinus1to1, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setInputGain(float v01) noexcept         { pInputGain.store(clamp01(v01), std::memory_order_relaxed); }   // preamp drive
    void setWowFlutter(float v01) noexcept        { pWowFlutter.store(clamp01(v01), std::memory_order_relaxed); }
    void setBypass(bool bypassed) noexcept
    {
        const float next = bypassed ? 1.0f : 0.0f;
        const float previous =
            pBypass.exchange(next, std::memory_order_relaxed);
        if (next > 0.5f && previous <= 0.5f)
            pClearRequest.fetch_add(1u, std::memory_order_relaxed);
    }
    void setTapeAge(float v01) noexcept           { pTapeAge.store(clamp01(v01), std::memory_order_relaxed); }   // 0 = fresh (bit-identical); worn tape: hiss, extra wow, HF loss, level wobble
    void setOutputVolume(float v01) noexcept      { pOutputVolume.store(clamp01(v01), std::memory_order_relaxed); }
    void setEchoPan(float v01) noexcept           { pEchoPan.store(clamp01(v01), std::memory_order_relaxed); }
    void setReverbPan(float v01) noexcept         { pReverbPan.store(clamp01(v01), std::memory_order_relaxed); }
    void setInputSend(bool enabled) noexcept      { pInputSend.store(enabled ? 1.0f : 0.0f, std::memory_order_relaxed); }
    void setWetSolo(bool enabled) noexcept        { pWetSolo.store(enabled ? 1.0f : 0.0f, std::memory_order_relaxed); }
    void triggerLoopSplice() noexcept             { pSpliceTrigger.fetch_add(1u, std::memory_order_relaxed); }

    // The motor control is intentionally nonlinear. The inverse is used by
    // tempo sync so a requested musical delay still lands at the right time.
    static float delayMsForRepeatRate(float v01) noexcept;
    static float repeatRateForDelayMs(float delayMs) noexcept;

    //--- metering (thread-safe: read from any thread) --------------------------
    // Peak output level with ~300 ms release, linear [0, ~3]. For VU/peak UI.
    // Reads 0 while bypassed (a powered-off meter is a dead meter).
    float getOutputLevel() const noexcept         { return outputPeak.load(std::memory_order_relaxed); }

private:
    //--- helpers -------------------------------------------------------------
    static float clamp01(float v) noexcept  { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float clampF(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static int   clampInt(int v, int lo, int hi) noexcept     { return v < lo ? lo : (v > hi ? hi : v); }

    // Bounded cubic soft clip, tanh-like, branch-light. Monotonic on the
    // clamped range, |out| <= 1. Used for tape saturation and the feedback
    // limiter — this is what stabilizes self-oscillation.
    static float softClip(float x) noexcept
    {
        x = clampF(x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Input-stage limiting is odd-symmetric. Hosted harmonic ladders show the
    // even orders at the noise floor across the full input-control range.
    static float preampShape(float x) noexcept
    {
        x = clampF(x, -2.5f, 2.5f);
        return softClip(x);
    }

    // Measured front-panel input taper. It is shared by the direct and effect
    // paths, reaches unity at the midpoint, provides about +8.6 dB at maximum,
    // and mutes exactly at zero.
    static float inputGainFromControl(float x) noexcept
    {
        return x * (1.29212f + 1.40165f * x);
    }

    // Measured effect-volume taper. The linear term is important at low
    // settings; a pure square law undershoots the lower half substantially.
    static float echoGainFromControl(float x) noexcept
    {
        return x * (0.30355f + 0.69645f * x);
    }

    static float feedbackGainFromControl(float x) noexcept
    {
        x = clamp01(x);
        float normalized;
        if (x <= 0.7f)
        {
            normalized = 0.00063492f * x
                       + 0.95619048f * x * x
                       + 0.96507937f * x * x * x;
            const float knee = clamp01((x - 0.65f) * 20.0f);
            const float kneeSmooth = knee * knee * (3.0f - 2.0f * knee);
            normalized -= 0.02728236f * kneeSmooth;
        }
        else
        {
            float loX, hiX, loGain, hiGain;
            if (x <= 0.8f)
            {
                loX = 0.7f; hiX = 0.8f;
                loGain = 0.77271764f; hiGain = 0.82074074f;
            }
            else if (x <= 0.9f)
            {
                loX = 0.8f; hiX = 0.9f;
                loGain = 0.82074074f; hiGain = 0.85925926f;
            }
            else
            {
                loX = 0.9f; hiX = 1.0f;
                loGain = 0.85925926f; hiGain = 0.88f;
            }
            const float t = (x - loX) / (hiX - loX);
            const float smooth = t * t * (3.0f - 2.0f * t);
            normalized = loGain + (hiGain - loGain) * smooth;
        }
        return 1.30f * normalized;
    }

    // Group delay of the 4x oversampling chain in base-rate samples
    // (2x stage: 46 samples at 2x; 4x stage: 14 at 4x). Compensated in the
    // tape delay so head timing stays exact.
    static constexpr float kPreampLatencySamples = 23.0f + 3.5f;

    float readTape(const std::vector<float>& buf, float delaySamples) const noexcept;
    void  refreshBlockRateControls();

    //--- per-channel state ----------------------------------------------------
    struct Channel
    {
        std::vector<float> tape;
        DCBlocker          preampDC;
        HalfbandFIR<47, 12> upA, downA;   // base <-> 2x (tight, -67 dB)
        HalfbandFIR<15, 4>  upB, downB;   // 2x <-> 4x (loose, -75 dB)
        Biquad             recordHP;   // in-loop, two-pole low-end cleanup
        Biquad             speedLP;    // tape-speed-dependent playback bandwidth
        std::array<Biquad, 4> antiAliasLP; // steep fixed record/repro ceiling
        Biquad             bassShelf;  // echo output path only
        Biquad             trebleShelf;
        Biquad             dryLowShelf;  // direct-path coupling/amp bandwidth
        Biquad             dryHighShelf;
        SpringReverb       spring;
        std::array<float, 32> springCleanDelay {};
        int                springCleanDelayWriteIdx = 0;
        float              recordEnvelope = 0.0f;
        float              magneticEnvelope = 0.0f;
    };

    std::array<Channel, kMaxChannels> channels;

    float preampOversampled(Channel& ch, float x, float drive) noexcept;

    //--- tape transport --------------------------------------------------------
    int    writeIdx   = 0;
    int    mask       = 0;      // tape length is a power of two
    float  maxDelaySamples = 0.0f;
    double fs         = 44100.0;

    //--- metering ---------------------------------------------------------------
    std::atomic<float> outputPeak { 0.0f };
    float meterDecayPerSample = 1.0f;
    float recordEnvelopeAttack = 1.0f;
    float recordEnvelopeRelease = 1.0f;

    //--- modulation (shared across channels: one motor, one tape) --------------
    float     wowPhase = 0.0f, wowInc = 0.0f;
    float     flutterPhase = 0.0f, flutterInc = 0.0f;
    OnePoleLP noiseLP;
    uint32_t  rngState = 0x9E3779B9u;

    float frand() noexcept  // uniform [-1, 1)
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float)(int32_t)rngState * (1.0f / 2147483648.0f);
    }

    //--- atomic parameter inputs -----------------------------------------------
    std::atomic<int>   pMode        { 1 };
    std::atomic<float> pRepeatRate  { 0.0f };
    std::atomic<float> pIntensity   { 0.0f };
    std::atomic<float> pEchoLevel   { 0.5f };
    std::atomic<float> pReverbLevel { 0.0f };
    std::atomic<float> pDryLevel    { 1.0f };
    std::atomic<float> pBass        { 0.0f };
    std::atomic<float> pTreble      { 0.0f };
    std::atomic<float> pInputGain   { 0.5f };
    std::atomic<float> pWowFlutter  { 0.0f };
    std::atomic<float> pBypass      { 0.0f };
    std::atomic<float> pTapeAge     { 0.5f };
    std::atomic<float> pOutputVolume{ 0.5f };
    std::atomic<float> pEchoPan     { 0.5f };
    std::atomic<float> pReverbPan   { 0.5f };
    std::atomic<float> pInputSend   { 1.0f };
    std::atomic<float> pWetSolo     { 0.0f };
    std::atomic<uint32_t> pSpliceTrigger { 0u };
    std::atomic<uint32_t> pClearRequest { 0u };

    //--- smoothed control signals ----------------------------------------------
    SmoothedValue delaySmoother;                 // motor inertia -> pitch glide
    SmoothedValue intensitySmoother;
    SmoothedValue headGain[3];                   // mode switching, click-free
    SmoothedValue reverbSendSmoother;
    SmoothedValue echoLevelSmoother, reverbLevelSmoother, dryLevelSmoother;
    SmoothedValue outputVolumeSmoother;
    SmoothedValue echoPanSmoother, reverbPanSmoother;
    SmoothedValue inputSendSmoother, wetSoloSmoother;
    SmoothedValue driveSmoother, wowFlutterSmoother;
    SmoothedValue powerSmoother;                 // bypass crossfade, click-free
    SmoothedValue ageSmoother;                   // tape age morph

    // tape-age state: separate RNG so age 0 leaves the wow noise stream
    // (and therefore the output) bit-identical to builds without this knob
    uint32_t  ageRngState = 0x1F123BB5u;
    OnePoleLP hissVoice;                           // darken the mono tape hiss
    OnePoleLP wobbleLP;                           // slow playback-level wobble
    float     lastPlaybackCutoff = -1.0f;         // block-rate speed/age guard
    float     lastAntiAliasCutoff = -1.0f;
    uint32_t  lastSpliceTrigger = 0u;
    uint32_t  lastClearRequest = 0u;
    float     spliceSamplesToHead1 = 0.0f;
    bool      spliceClockStarted = false;

    float ageRand() noexcept
    {
        ageRngState = ageRngState * 1664525u + 1013904223u;
        return (float)(int32_t)ageRngState * (1.0f / 2147483648.0f);
    }

    // cached to detect shelf-coefficient changes at block rate
    float lastBass = -999.0f, lastTreble = -999.0f;
};

} // namespace duskaudio
