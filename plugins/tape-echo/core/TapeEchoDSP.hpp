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
#include "../dpf-plugin/TapeEchoParams.hpp" // shared discrete-value helpers

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
    void setTapeAge(float v01) noexcept
    {
        // The three cartridge states use normalized 0/.5/1 host values so
        // existing sessions and decoded reference states retain their meaning.
        pTapeAge.store(teQuantizeTapeAge(v01), std::memory_order_relaxed);
    }
    void setOutputVolume(float v01) noexcept      { pOutputVolume.store(clamp01(v01), std::memory_order_relaxed); }
    void setEchoPan(float v01) noexcept           { pEchoPan.store(clamp01(v01), std::memory_order_relaxed); }
    void setReverbPan(float v01) noexcept         { pReverbPan.store(clamp01(v01), std::memory_order_relaxed); }
    void setInputSend(bool enabled) noexcept      { pInputSend.store(enabled ? 1.0f : 0.0f, std::memory_order_relaxed); }
    void setWetSolo(bool enabled) noexcept        { pWetSolo.store(enabled ? 1.0f : 0.0f, std::memory_order_relaxed); }
    void setMix(float v01) noexcept               { pMix.store(clamp01(v01), std::memory_order_relaxed); }
    float getTapeAge() const noexcept             { return pTapeAge.load(std::memory_order_relaxed); }

    // The motor control is intentionally nonlinear. The inverse is used by
    // tempo sync so a requested musical delay still lands at the right time.
    static float delayMsForRepeatRate(float v01) noexcept;
    static float repeatRateForDelayMs(float delayMs) noexcept;
    static float leadingHeadRatioForMode(int mode1to12) noexcept;

    //--- metering (thread-safe: read from any thread) --------------------------
    // The meter is in the record path after Input Volume. It remains live when
    // Input Send is muted and also sees feedback immediately before the tape
    // record chain. Both readings are dead while POWER is off.
    float getRecordVuLevel() const noexcept       { return recordVu.load(std::memory_order_relaxed); }
    float getRecordPeakLevel() const noexcept     { return recordPeak.load(std::memory_order_relaxed); }

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

    // Hard ceiling with a quadratic knee: identity below `knee`, exactly
    // `ceiling` at and above knee + 2(ceiling - knee). Monotonic, and C1 at
    // both joins (unit slope entering the knee, zero slope leaving it).
    static float hardKnee(float x, float knee, float ceiling) noexcept
    {
        const float magnitude = x < 0.0f ? -x : x;
        if (magnitude <= knee)
            return x;
        const float width = 2.0f * (ceiling - knee);
        const float sign  = x < 0.0f ? -1.0f : 1.0f;
        if (magnitude >= knee + width)
            return sign * ceiling;
        const float under = 1.0f - (magnitude - knee) / width;
        return sign * (knee + (ceiling - knee) * (1.0f - under * under));
    }

    // Record-amplifier overload, seen only by the regeneration path. The
    // intensity control feeds the record amp, and that amplifier runs out of
    // rail long before anything else in the loop does; the program arrives at
    // the same summing node having already been through its own preamp shaper,
    // so this ceiling is applied to the regenerated signal alone. That
    // placement is also what makes it safe: it is identically zero with the
    // intensity control at minimum, so every harmonic and level calibration
    // measured there is bit-identical.
    //
    // The knee is 22 dB above the loop level anywhere in the calibrated repeat
    // ladder and 15 dB above the marginal-oscillation level at intensity 0.7,
    // so nothing at or below the matched settings can reach it.
    //
    // A ceiling at the tape write instead - which is demonstrably where the
    // reference's is, since its loudest program peak (-22.15 dBFS wet) and its
    // runaway peak (-22.11) agree to 0.05 dB - was built and measured and then
    // rejected. This record chain reaches the reference's program fundamental
    // and THD with a waveform 0.76 dB peakier (its loudest program peak is
    // 0.2614 where the reference's is 0.2396), so any tape-write ceiling low
    // enough to shape a runaway also clips program material. Measured: at
    // 0.2396 it cost 0.62 dB of fundamental and 6.4% of THD at -3 dBFS and
    // broke both transfer gates (0.64 / 0.87 dB against 0.50 / 0.75 limits);
    // raised to 0.2620 and then 0.3000 to clear those, it still broke the
    // factory octave-band gate (2.81 and 2.68 dB against a 2.50 limit, from a
    // 2.13 baseline) because sustained tones in the factory presets reach that
    // flux too. The cost of this placement instead is crest: the loop clip
    // sits ahead of the playback roll-off, so a slammed loop reads back at
    // 2.3-2.6 dB crest against the reference's 0.35-0.77. Closing that means
    // reshaping the record stage's peak factor, which re-opens the
    // already-matched harmonic calibration and is its own campaign.
    static constexpr float kLoopCeilingKnee = 0.2000f;
    static constexpr float kLoopCeiling     = 0.2940f;

    // Runaway shaping above the onset knee. The reference crosses unity loop
    // gain near 0.7 and is fully slammed into its tape ceiling by 0.8, holding
    // that ceiling to maximum. The measured taper below only grazes unity, so
    // the loop equilibrated shallow and clean instead of dense and saturated.
    // This multiplier is exactly 1.0 with zero slope at and below 0.62, so
    // every calibration measured at or below feedback 0.5 - the whole repeat
    // ladder and all harmonic gates - is bit-identical. Piecewise smoothstep
    // between anchors, hence C1 continuous with no zipper for the 30 ms
    // intensity smoother to chase.
    static float feedbackRunawayLift(float x) noexcept
    {
        constexpr float kX[5] = { 0.62f, 0.70f, 0.80f, 0.90f, 1.00f };
        constexpr float kL[5] = { 1.0f, 1.0225f, 1.4500f, 1.9000f, 2.4500f };
        if (x <= kX[0])
            return kL[0];
        int i = 3;
        if (x <= kX[1])      i = 0;
        else if (x <= kX[2]) i = 1;
        else if (x <= kX[3]) i = 2;
        const float t = (x - kX[i]) / (kX[i + 1] - kX[i]);
        const float smooth = t * t * (3.0f - 2.0f * t);
        return kL[i] + (kL[i + 1] - kL[i]) * smooth;
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
        return 1.30f * normalized * feedbackRunawayLift(x);
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
        std::array<float, 32> springCleanDelay {};
        int                springCleanDelayWriteIdx = 0;
        float              recordEnvelope = 0.0f;   // program drive only
        float              loopEnvelope = 0.0f;     // regeneration drive only
        float              magneticEnvelope = 0.0f;
    };

    std::array<Channel, kMaxChannels> channels;
    SpringReverb spring; // shared mono wet path

    float preampOversampled(Channel& ch, float x, float drive) noexcept;

    //--- tape transport --------------------------------------------------------
    int    writeIdx   = 0;
    int    mask       = 0;      // tape length is a power of two
    float  maxDelaySamples = 0.0f;
    double fs         = 44100.0;

    //--- metering ---------------------------------------------------------------
    std::atomic<float> recordVu { 0.0f };
    std::atomic<float> recordPeak { 0.0f };
    float meterVu = 0.0f;
    float meterPeak = 0.0f;
    float meterVuAttackCoeff = 1.0f;
    float meterVuReleaseCoeff = 1.0f;
    float meterPeakDecayPerSample = 1.0f;
    float recordEnvelopeAttack = 1.0f;
    float recordEnvelopeRelease = 1.0f;

    //--- modulation (shared across channels: one motor, one tape) --------------
    // Two-pole state-variable band-pass, TPT (Zavalishin) topology. A direct-form
    // biquad is unusable here: at a 6 Hz corner and 48 kHz the pole pair sits
    // 7.9e-4 rad off the real axis, so a1 ~ -1.9996 and a2 ~ 0.9996 lose the
    // centre frequency to float cancellation (~10% drift). The TPT form stores
    // g = tan(pi*fc/fs) directly and stays well conditioned at any fc/fs.
    struct BandPassSVF
    {
        void set(float hz, float Q, double sampleRate) noexcept
        {
            const float nyquistGuard = 0.2f * (float)sampleRate;
            const float f = hz < nyquistGuard ? hz : nyquistGuard;
            const float g = std::tan(kDuskPi * f / (float)sampleRate);
            const float k = 1.0f / Q;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
        // Band-pass output; peak gain is Q at fc, -> 0 at DC and Nyquist.
        float process(float x) noexcept
        {
            const float v3 = x - s2;
            const float v1 = a1 * s1 + a2 * v3;
            const float v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            return v1;
        }
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    };

    float     wowPhase = 0.0f, wowInc = 0.0f;
    float     flutterPhase = 0.0f, flutterInc = 0.0f;
    OnePoleLP noiseLP;
    uint32_t  rngState = 0x9E3779B9u;

    // Stochastic scrape flutter: band noise around 6 Hz. Its own RNG stream so
    // the wow noise realization (and therefore the measured wow depth) is
    // untouched by this addition.
    BandPassSVF flutterBand;
    OnePoleLP   flutterBandLP;                    // steepens the upper skirt
    uint32_t    flutterRngState = 0x2545F491u;

    // Every modulator here is white noise through a fixed-Hz filter, whose
    // output RMS scales as sqrt(fc / fs) -- i.e. the SAME filter fed the SAME
    // noise gets quieter as the sample rate rises (measured: 0.707x at 96 kHz,
    // 0.500x at 192 kHz). All four depths were voiced against the reference at
    // 48 kHz, so without this the wow/flutter/hiss specs would only hold there.
    // Pre-scaling the noise by sqrt(fs / 48k) restores the calibrated RMS at
    // any rate. Exactly 1.0f at 48 kHz, so the calibrated path is bit-identical.
    static constexpr double kNoiseCalibrationFs = 48000.0;
    float noiseRateComp = 1.0f;

    float frand() noexcept  // uniform [-1, 1)
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float)(int32_t)rngState * (1.0f / 2147483648.0f);
    }

    float flutterRand() noexcept  // uniform [-1, 1)
    {
        flutterRngState = flutterRngState * 1664525u + 1013904223u;
        return (float)(int32_t)flutterRngState * (1.0f / 2147483648.0f);
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
    std::atomic<float> pMix         { 0.5f };
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
    SmoothedValue mixSmoother;
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
