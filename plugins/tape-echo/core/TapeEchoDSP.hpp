// TapeEchoDSP.hpp
// Component-modeled vintage three-head tape echo with spring reverb.
//
// Framework-free: standard C++17 only. No JUCE, no host dependencies.
// Designed to be wrapped by DPF, raw CLAP, or any other plugin shell.
//
// Signal flow (per channel, mirroring the hardware):
//
//   in ──► FET preamp (asymmetric soft clip + DC block) ──┬──► reverb send
//                                                         │
//        ┌────────────────────────────────────────────────┘
//        ▼
//   [+]──► record EQ (HP 55 Hz / LP 6.2 kHz) ──► tape saturation ──► TAPE
//    ▲                                                                │
//    │                                                     3 read heads (Hermite)
//    │                                                     T · {1.00, 1.90, 2.75}
//    └── intensity · softClip(head sum)  ◄─────────────────┴──┐
//                                                             ▼
//                                    bass/treble shelves (echo path only)
//                                                             ▼
//   out = dry·in + echoLevel·echoEQ + reverbLevel·spring(reverb send)
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

namespace duskaudio
{

//==============================================================================
// Small framework-free building blocks
//==============================================================================

// One-pole exponential parameter smoother (replaces juce::LinearSmoothedValue).
class SmoothedValue
{
public:
    void prepare(double sampleRate, float tauSeconds) noexcept
    {
        coeff = 1.0f - std::exp(-1.0f / (float)(tauSeconds * sampleRate));
    }
    void snap(float v) noexcept   { current = target = v; }
    void setTarget(float v) noexcept { target = v; }
    float next() noexcept         { current += coeff * (target - current); return current; }
    float value() const noexcept  { return current; }

private:
    float current = 0.0f, target = 0.0f, coeff = 1.0f;
};

class OnePoleLP
{
public:
    void setCutoff(float hz, double fs) noexcept
    {
        c = 1.0f - std::exp(-6.2831853f * hz / (float)fs);
    }
    void reset() noexcept { z = 0.0f; }
    float process(float x) noexcept { z += c * (x - z); return z; }

private:
    float c = 1.0f, z = 0.0f;
};

class OnePoleHP
{
public:
    void setCutoff(float hz, double fs) noexcept { lp.setCutoff(hz, fs); }
    void reset() noexcept { lp.reset(); }
    float process(float x) noexcept { return x - lp.process(x); }

private:
    OnePoleLP lp;
};

class DCBlocker
{
public:
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process(float x) noexcept
    {
        const float y = x - x1 + 0.9975f * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float x1 = 0.0f, y1 = 0.0f;
};

// Transposed direct-form II biquad, RBJ shelving coefficients.
class ShelfFilter
{
public:
    enum class Type { lowShelf, highShelf };

    void configure(Type type, float freqHz, float gainDb, double fs) noexcept;
    void reset() noexcept { z1 = z2 = 0.0f; }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

//==============================================================================
// Polyphase-friendly FIR halfband for 2x up/down sampling (ring convolution;
// taps designed with scipy remez, halfband-exact: even offsets zero,
// center 0.5). Used to run the preamp nonlinearity at 4x — saturating
// full-band input at base rate folds harmonics of bright content straight
// into the echo passband (measured up to -1.5 dBc before this).
//==============================================================================
template <int L, int NSide>
class HalfbandFIR
{
public:
    void reset() noexcept
    {
        for (float& v : buf) v = 0.0f;
        pos = 0;
    }
    void push(float x) noexcept { pos = (pos + 1) & 63; buf[pos] = x; }
    float out(const float* taps) const noexcept
    {
        constexpr int C = L / 2;
        float acc = 0.5f * buf[(pos - C) & 63];
        for (int i = 0; i < NSide; ++i)
        {
            const int k = 2 * i + 1;
            acc += taps[i] * (buf[(pos - (C - k)) & 63] + buf[(pos - (C + k)) & 63]);
        }
        return acc;
    }

private:
    float buf[64] = {};
    int   pos = 0;
};

//==============================================================================
// Spring reverb — simplified two-spring tank in the classic style.
//
// Each of the two springs is a feedback delay loop containing a long chain of
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
        int                                      len = 0, writeIdx = 0;
        float                                    feedback = 0.0f;
        float                                    lfoPhase = 0.0f, lfoInc = 0.0f, lfoDepth = 0.0f;
        std::array<Allpass1, kNumAllpasses>      chain;
        OnePoleLP                                damping;

        void  prepare(double fs, float lengthSeconds, float fbAmount,
                      float lfoHz, float apCoeff);
        void  reset();
        float process(float x) noexcept;
    };

    std::array<Spring, 2> springs;
    OnePoleHP inputHP;   // springs don't transmit deep lows
    OnePoleLP inputLP;   // dark transducer voicing
    DCBlocker dcBlock;
};

//==============================================================================
// TapeEchoDSP — the complete tape echo core.
//==============================================================================
class TapeEchoDSP
{
public:
    static constexpr int kNumModes    = 12;
    static constexpr int kMaxChannels = 2;

    // Head-1 delay range from the hardware motor-speed span.
    static constexpr float kMinDelayMs = 69.0f;
    static constexpr float kMaxDelayMs = 177.0f;

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
    void setBypass(bool bypassed) noexcept        { pBypass.store(bypassed ? 1.0f : 0.0f, std::memory_order_relaxed); } // POWER off: clean passthrough, tape keeps spinning
    void setTapeAge(float v01) noexcept           { pTapeAge.store(clamp01(v01), std::memory_order_relaxed); }   // 0 = fresh (bit-identical); worn tape: hiss, extra wow, HF loss, level wobble

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

    // FET preamp: mild asymmetry (even harmonics) + soft limiting. The x²
    // term introduces DC, removed by a per-channel DC blocker downstream.
    static float preampShape(float x) noexcept
    {
        x = clampF(x, -2.5f, 2.5f);
        return softClip(x + 0.08f * x * x);
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
        OnePoleHP          recordHP;   // in-loop
        OnePoleLP          recordLP;   // in-loop, darkens every repeat
        OnePoleLP          recordLP2;  // second pole: the record/repro chain of the
                                       // hardware rolls off steeper than 6 dB/oct
        ShelfFilter        bassShelf;  // echo output path only
        ShelfFilter        trebleShelf;
        SpringReverb       spring;
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
    std::atomic<float> pRepeatRate  { 0.5f };
    std::atomic<float> pIntensity   { 0.4f };
    std::atomic<float> pEchoLevel   { 0.8f };
    std::atomic<float> pReverbLevel { 0.0f };
    std::atomic<float> pDryLevel    { 1.0f };
    std::atomic<float> pBass        { 0.0f };
    std::atomic<float> pTreble      { 0.0f };
    std::atomic<float> pInputGain   { 0.5f };
    std::atomic<float> pWowFlutter  { 0.5f };
    std::atomic<float> pBypass      { 0.0f };
    std::atomic<float> pTapeAge     { 0.0f };

    //--- smoothed control signals ----------------------------------------------
    SmoothedValue delaySmoother;                 // motor inertia -> pitch glide
    SmoothedValue intensitySmoother;
    SmoothedValue headGain[3];                   // mode switching, click-free
    SmoothedValue reverbSendSmoother;
    SmoothedValue echoLevelSmoother, reverbLevelSmoother, dryLevelSmoother;
    SmoothedValue driveSmoother, wowFlutterSmoother;
    SmoothedValue powerSmoother;                 // bypass crossfade, click-free
    SmoothedValue ageSmoother;                   // tape age morph

    // tape-age state: separate RNG so age 0 leaves the wow noise stream
    // (and therefore the output) bit-identical to builds without this knob
    uint32_t  ageRngState = 0x1F123BB5u;
    OnePoleLP hissVoice, hissVoiceR;              // darken the injected hiss (per channel)
    OnePoleLP wobbleLP;                           // slow playback-level wobble
    float     lastAge = -1.0f;                    // block-rate LP2 retune guard

    float ageRand() noexcept
    {
        ageRngState = ageRngState * 1664525u + 1013904223u;
        return (float)(int32_t)ageRngState * (1.0f / 2147483648.0f);
    }

    // cached to detect shelf-coefficient changes at block rate
    float lastBass = -999.0f, lastTreble = -999.0f;
};

} // namespace duskaudio
