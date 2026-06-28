#pragma once

#include "DspUtils.h"
#include "TwoBandDamping.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Dattorro-inspired cross-coupled tank reverb for Room algorithm.
//
// Enhanced topology: two asymmetric feedback loops cross-coupled in a figure-8.
// Each loop contains: modulated allpass → delay → density cascade (3 allpasses)
// → two-band damping → static allpass → delay.
//
// The density cascade multiplies echo density by ~8× per loop pass compared
// to the basic Dattorro topology (which only has 2 allpasses per loop).
// This is critical for room-scale delays where mode spacing is wider.
//
// Output is formed by summing 7 signed taps from various internal
// points in both tanks, creating naturally decorrelated stereo.
//
// Reference: Dattorro, "Effect Design Part 1" (JAES 1997), adapted
// for room-scale delays, extended density cascades, and per-sample
// noise modulation for aggressive mode smearing.
class DattorroTank
{
public:
    DattorroTank();

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setMidMultiply (float mult);                  // 3-band mid mult (default 1.0)
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation (float amount);                 // 0..1 drive-style softClip
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setFreeze (bool frozen);

    // User-facing tank density. amount is the DIFFUSION knob value [0, 1].
    // Scales the in-loop density-AP coefficient around the engine baseline so
    // the user can dial the late tail between sparse-and-echoey (knob low)
    // and lush-and-dense (knob high). Existing presets at amount≈0.85 get a
    // modest density bump (~10 %) over the previous fixed value.
    void setTankDiffusion (float amount);
    void setLateGainScale (float scale);
    void setSizeRange (float min, float max);
    void setHallScale (bool enable);   // Switch to hall-scale delay lengths (2x room)
    // Output tap specification (moved to public for per-algorithm configuration)
    struct OutputTap
    {
        int bufferIndex;     // Which delay buffer (0-5: L/R delay1/delay2/AP2)
        float positionFrac;  // Fractional position (0.0-1.0) within the delay
        float sign;          // ±1.0 for decorrelation
        float gain;          // Per-tap amplitude (0.0-2.0). Shapes onset envelope.
                             // < 1.0 attenuates (slows onset), > 1.0 boosts (faster onset).
                             // Default 1.0 = equal weight (original behavior).
    };

    void setOutputTaps (const OutputTap* left, const OutputTap* right);
    void applyTapGains (const float* leftGains, const float* rightGains);  // Update gain field only
    void setDelayScale (float scale);  // Multiplies ALL base delays (controls loop length)
    void setSoftOnsetMs (float ms);    // Output onset smoothing time (0 = off)
    void setLimiter (float thresholdDb, float releaseMs);  // Peak limiter (0 thresholdDb = off)
    void setDecayBoost (float boost);
    void setStructuralHFDamping (float hz);
    // Density-AP random-walk jitter depth (fraction of each AP's delay).
    // Engine default 0.02 (the #87 anti-ring wander). The in-loop wander is
    // a time-VARYING element: every pass FM-scatters tail energy broadband,
    // which builds a flat late-window HF plateau (~35 dB above a dark
    // anchor's floor) that NO static filter — feed, in-loop, or post — can
    // remove, and drives the tail pitch-chorus metric. Dark/short room
    // presets can trade the (mild, short-decay) comb risk for a clean dark
    // tail. Clamped to [0, 0.02]: the AP buffers allocate headroom for
    // exactly 2 % wander. 0.02 = bit-identical legacy.
    void setDensityJitter (float fraction);

    // Plate density rework (default 0 / 1.0 = legacy, byte-identical):
    //   setDensityDepth: 0 = legacy 3 APs; >0 engages all 6 density APs +
    //     a small coeff boost (denser, smoother tail — no added modulation).
    //   setModReduction: 1.0 = legacy mod; <1.0 pulls AP1 + delay modulation
    //     toward still (toward the Lex/VVV near-static tail).
    void setDensityDepth (float depth01);
    void setModReduction (float reduction01);
    // #87 boing fix (short rooms): when true, the 12-AP density cascade loads the
    // MID-PRIME hall-scale density bases (kLeft/RightDensityAPBaseHall, 307-1303
    // smp) instead of the short room bases, adding close-spaced coprime modes that
    // fill the sparse low-mid gap → kills the comb-coincidence resonance. The 4
    // MAIN delay lines stay at room scale (does NOT call setHallScale → room is not
    // lengthened). Only takes effect when setDensityDepth(>0) engages the cascade.
    // false = legacy room density bases = byte-identical.
    void setDensityRoomFill (bool enable);
    // Per-line incommensurate detune of the 4 main delay lines (fractional mult on
    // top of totalScale). Breaks the L<->R harmonic ratios so coincident comb teeth
    // interleave. {1,1,1,1} identity = bit-null. Factors = [Ldel1,Ldel2,Rdel1,Rdel2].
    void setMainLineDetune (float lDel1, float lDel2, float rDel1, float rDel2);
    // Input-diffuser coefficient scale (sweep handle). 1.0 = canonical
    // 0.75/0.625; only active when setDensityDepth>0 engages the cascade.
    void setInputDiffusionScale (float scale01);

    // Per-octave T60 control (the AccurateHall GEQ, ported). band 0..8 =
    // ISO octave centres 63|125|250|500|1k|2k|4k|8k|16k Hz; seconds = target
    // T60 at that octave (>0 engages the octave GEQ in place of the 3-band
    // damping). setOctaveDecayRef ties the Decay knob to the curve (scale =
    // decayTime/ref) so the knob stays live. All-zero T60 = inactive = legacy.
    void setOctaveT60 (int band, float seconds);
    void setOctaveDecayRef (float seconds);

    // Tonal-correction GEQ: a STATIC per-octave EQ on the wet OUTPUT (not in the
    // recirculating loop), so it sets the steady-state spectral balance
    // INDEPENDENTLY of the in-loop decay — the decoupling the Dattorro
    // gain==decay==level coupling otherwise can't give (you can't cut a band's
    // level without shortening its decay). band 0..8 = the ISO octaves; dB = the
    // per-octave level trim (calibrated to match the anchor's steady-state
    // spectrum). All-zero dB = identity = bit-identical.
    void setTonalCorrDb (int band, float dB);

    // Slow-attack BLOOM (input-onset-driven swell). The real Lex vintage vocal
    // plate's impulse peaks ~90ms in (a gentle swell), not instantly. An input
    // activity follower (slow release, so it stays latched through the tail)
    // drives a one-pole swell gain that opens over attackMs after onset, then
    // holds open — so the IR rises to a late peak (slow attack) WITHOUT killing
    // the tail. 0 = off (gain pinned 1.0) = bit-identical. ms ≈ desired
    // attack-to-peak time (tuned vs the anchor's attack_time gate).
    void setBloomAttackMs (float ms);
    void setBloomExp (float e);   // reverse-buildup curve power (>1 suppresses early)

    void clearBuffers();

private:
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr double kBaseSampleRate = 44100.0;

    // Safety clamp (~+12dBFS), matches FDNReverb
    static constexpr float kSafetyClip = 4.0f;

    // Output tap count per channel (Dattorro uses 7)
    static constexpr int kNumOutputTaps = 7;

    // -----------------------------------------------------------------------
    // Base delay lengths at 44100 Hz (all prime, room-scaled from Dattorro plate).
    // Asymmetric L/R prevents correlated modal peaks.

    // Left tank
    static constexpr int kLeftAP1Base  = 331;   // ~7.5ms  modulated allpass
    static constexpr int kLeftDel1Base = 2203;   // ~50ms   delay
    static constexpr int kLeftAP2Base  = 887;    // ~20ms   static allpass
    static constexpr int kLeftDel2Base = 1831;   // ~41.5ms delay

    // Right tank
    static constexpr int kRightAP1Base  = 443;   // ~10ms   modulated allpass
    static constexpr int kRightDel1Base = 2081;   // ~47ms   delay
    static constexpr int kRightAP2Base  = 1307;   // ~29.6ms static allpass
    static constexpr int kRightDel2Base = 1559;   // ~35.3ms delay

    // Worst-case base delay for buffer allocation (hall-scale is larger)
    static constexpr int kMaxBaseDelay = 18028;  // 4507 * 4 (max delayScale=4.0)

    // -----------------------------------------------------------------------
    // Circular delay line with power-of-2 masking.
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;

        void allocate (int maxSamples);
        void clear();

        void write (float sample)
        {
            buffer[static_cast<size_t> (writePos)] = sample;
            writePos = (writePos + 1) & mask;
        }

        // Read at a fixed integer offset behind the write head.
        float read (int delaySamples) const
        {
            return buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
        }

        // Read at a fractional offset (cubic Hermite interpolation).
        float readInterpolated (float delaySamples) const;
    };

    // Non-modulated Schroeder allpass with optional vintage-hardware-style "spin and
    // wander" jitter (see SixAPTankEngine::Allpass for the full rationale).
    // Default jitterDepthFraction = 0 = static AP (back-compat).
    struct Allpass
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
        int delaySamples = 0;

        DspUtils::RandomWalkLFO jitterLFO;
        float                   jitterDepthFraction = 0.0f;

        void allocate (int maxSamples);
        void clear();

        // Depth tracks delaySamples (fractional). Rate is FIXED at sub-
        // audio 1.5 Hz: the original delay-period-scaled rate (5-200 Hz)
        // generated broadband FM sidebands on the recursive AP feedback
        // path — perceived as vibrato/bell (issue #87). Slow random-walk
        // wander breaks comb-tooth phase-lock without sidebands.
        void updateJitterDepth (float /*sampleRate*/)
        {
            if (jitterDepthFraction <= 0.0f || delaySamples <= 0)
                return;
            jitterLFO.setDepth (static_cast<float> (delaySamples) * jitterDepthFraction);
            jitterLFO.setRate (1.5f);
        }

        float process (float input, float g)
        {
            float vd;
            if (jitterDepthFraction > 0.0f)
            {
                const float jitter  = jitterLFO.next();
                const float readPos = static_cast<float> (writePos)
                                    - static_cast<float> (delaySamples)
                                    - jitter;
                int   intIdx = static_cast<int> (std::floor (readPos));
                const float frac = readPos - static_cast<float> (intIdx);
                intIdx = static_cast<int> (static_cast<unsigned int> (intIdx)
                                            & static_cast<unsigned int> (mask));
                vd = DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
            }
            else
            {
                vd = buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
            }
            const float vn = input + g * vd;
            buffer[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
    };

    // -----------------------------------------------------------------------
    // Density cascade: 3 additional allpasses between delay1 and damping.
    // Multiplies echo density ~8× per loop pass (each AP doubles mode count).
    // Delays are prime and coprime to all other elements.
    // 12 density allpasses: the first 3 are the legacy cascade (always active);
    // stages 3..11 are the dense-path extension, engaged when a preset opts in
    // (numActiveDensityAPs_ 3->12). More Schroeder stages = more modes/echoes =
    // shallower spectral comb (lower 'ripple' gates) AND smoother tail — the
    // Lexicon/Valhalla density mechanism, no added modulation. Allpasses are
    // lossless (|H|=1) so extra stages do NOT raise loop gain / threaten
    // stability; they only fill the mode density. Default processes only 3 →
    // byte-identical to the legacy plate.
    static constexpr int kNumDensityAPs = 12;

    // Density-AP wander cap. The density-AP buffers allocate EXACTLY this much
    // read-offset headroom (see prepare()), and setDensityJitter() clamps to it
    // — one invariant, one constant, so the two can never drift out of sync and
    // expose an out-of-bounds interpolated read.
    static constexpr float kMaxDensityJitterFraction = 0.02f;

    // Left tank density AP delays (at 44100 Hz). First 3 = legacy; last 3 = dense-path
    // extension (prime + coprime to the legacy + delay lines → no resonant build-up).
    // Stages 6..11 are SHORT (1-4 ms) like 3..5: density-FILL primes, not the
    // long delays that add discrete mid comb teeth (raises 'ripple', the same
    // trap as the input diffuser). All prime, coprime to stages 0..5 + the lines.
    static constexpr int kLeftDensityAPBase[kNumDensityAPs] =
        { 137, 199, 281, 53, 79, 113, 43, 67, 97, 131, 163, 191 };
    // Right tank density AP delays (at 44100 Hz)
    static constexpr int kRightDensityAPBase[kNumDensityAPs] =
        { 149, 211, 263, 61, 89, 127, 47, 71, 101, 139, 167, 193 };

    // Hall-scale delays: ~2x room for 1-12s RT60 (all prime, coprime to room delays)
    // Total loop: left=12161 (275.8ms), right=12559 (284.8ms)
    // Per-algorithm temporal scaling is done via sizeRange in AlgorithmConfig,
    // NOT by changing these base constants.
    static constexpr int kLeftAP1BaseHall  = 709;    // ~16.1ms
    static constexpr int kLeftDel1BaseHall = 4507;   // ~102.2ms
    static constexpr int kLeftAP2BaseHall  = 1871;   // ~42.4ms
    static constexpr int kLeftDel2BaseHall = 3769;   // ~85.4ms
    static constexpr int kRightAP1BaseHall  = 953;   // ~21.6ms
    static constexpr int kRightDel1BaseHall = 4219;  // ~95.7ms
    static constexpr int kRightAP2BaseHall  = 2749;  // ~62.3ms
    static constexpr int kRightDel2BaseHall = 3299;  // ~74.8ms
    static constexpr int kLeftDensityAPBaseHall[kNumDensityAPs]  =
        { 307, 421, 577, 661, 743, 827, 907, 991, 1063, 1151, 1229, 1303 };
    static constexpr int kRightDensityAPBaseHall[kNumDensityAPs] =
        { 337, 461, 541, 691, 769, 883, 937, 1009, 1091, 1171, 1249, 1321 };

    // -----------------------------------------------------------------------
    // Input diffusion cascade (Dattorro's pre-tank diffusers). A single mono
    // chain of STATIC allpasses that smears the input impulse into a dense
    // burst BEFORE it enters the tank. Without it, a single impulse reaches the
    // 7 output taps as a sparse spray of discrete arrivals — the measured
    // first-60 ms kurtosis spike (DV 7-27 vs a dense anchor's 3-6). With it, the
    // early field is already a dense diffuse burst (low kurtosis), matching the
    // Lexicon/Valhalla "smooth from sample one" character.
    //
    //   • Feed-FORWARD (NOT in the recirculating loop) → does NOT change RT60.
    //   • Energy-preserving allpass (flat magnitude) → no level/spectrum shift;
    //     redistributes early energy in TIME only.
    //   • Scaled by sample rate only (NOT size/delayScale) so the ~20 ms smear
    //     time is constant across room sizes — initial diffusion is a property
    //     of the medium, not the room.
    //   • Default bypassed (inputDiffusionActive_ = false) → byte-identical
    //     legacy; engaged only on the dense-path opt-in (setDensityDepth > 0).
    static constexpr int kNumInputDiffusers = 6;
    // Input-diffuser delays (44100 Hz), all prime and coprime to every tank
    // delay line / density AP (no shared period → no resonant build-up).
    // SHORT + dense (~1–7 ms, vs Dattorro's classic 2–9 ms) because the measured
    // kurtosis error lives in the FIRST 10 ms — short stages flood that window
    // with echoes (the cascade convolution places arrivals at every integer
    // combination of the 6 delays). Coeffs ~0.6 (not 0.75): the lower per-stage
    // feedback rings less and reads smoother in the kurtosis trajectory.
    static constexpr int   kInputDiffuserBase[kNumInputDiffusers]  = { 43, 71, 103, 167, 239, 313 };
    static constexpr float kInputDiffuserCoeff[kNumInputDiffusers] = { 0.65f, 0.65f, 0.62f, 0.60f, 0.60f, 0.58f };

    // -----------------------------------------------------------------------
    // Each cross-coupled feedback loop.
    struct Tank
    {
        // Modulated allpass (decay diffusion 1)
        DelayLine ap1Buffer;       // Buffer for modulated allpass
        int ap1BaseDelay = 0;      // Base delay at 44100 Hz
        float ap1DelaySamples = 0; // Current delay (scaled by rate + size)

        // First delay line
        DelayLine delay1;
        int delay1BaseDelay = 0;
        float delay1Samples = 0;

        // Density cascade: 3 allpasses for echo density multiplication
        Allpass densityAP[kNumDensityAPs];
        int densityAPBase[kNumDensityAPs] = {};

        // Three-band damping (bass / mid / air with independent per-band decay)
        ThreeBandDamping damping;

        // Per-octave GEQ damping (9 ISO-octave T60 plateaus) — the AccurateHall
        // mechanism, ported in to break the 3-band-vs-9-octave T60 wall. Only
        // used when octaveActive_; coeffs designed message-thread per tank (the
        // L/R loop lengths differ slightly), state stays RT-side. Default unused
        // → byte-identical to the legacy 3-band plate.
        OctaveBandDamping          octaveDamping;
        OctaveBandDamping::Coeffs  octaveCoeffs {};

        // Static allpass (decay diffusion 2)
        Allpass ap2;
        int ap2BaseDelay = 0;

        // Second delay line
        DelayLine delay2;
        int delay2BaseDelay = 0;
        float delay2Samples = 0;

        // Cross-feed state (output of this tank feeds other tank's input)
        float crossFeedState = 0.0f;

        // Random-walk LFO for modulated allpass — replaces the previous
        // sine + drift LFO. Smoothed-noise wander never beats periodically
        // against the tank's modal frequencies, eliminating the audible
        // "warble" on long decays.
        DspUtils::RandomWalkLFO lfo;

        // Independent random-walk LFOs for delay1 and delay2 read taps.
        // Replaces the per-sample white-noise jitter that used to modulate
        // these reads — white noise on a delay-read is audio-rate phase
        // modulation, which generates broadband FM sidebands (heard as
        // "tape hiss"). Smoothstep-interpolated wander gives the same
        // mode-breaking benefit without the HF artifacts. Distinct seeds
        // and slightly detuned rates ensure all three modulators in the
        // tank trace independent paths ("spin and wander").
        DspUtils::RandomWalkLFO delay1Lfo;
        DspUtils::RandomWalkLFO delay2Lfo;

        // Saved modulation offset: held constant when frozen to prevent read-head snap
        float savedAP1Mod = 0.0f;
    };

    Tank leftTank_;
    Tank rightTank_;

    // -----------------------------------------------------------------------
    // Default output tap positions (early Dattorro-style, read from both tanks).
    // Tap indices: 0=leftDelay1, 1=leftDelay2, 2=leftAP2,
    //              3=rightDelay1, 4=rightDelay2, 5=rightAP2
    // 7 taps per channel, tapping from BOTH tanks for stereo decorrelation.
    // Positions are fractions of each delay's current length, so they scale with size.
    static constexpr OutputTap kLeftOutputTaps[kNumOutputTaps] = {
        { 3, 0.120f,  1.0f, 1.0f },  // right delay1, early
        { 3, 0.675f,  1.0f, 1.0f },  // right delay1, late
        { 5, 0.480f, -1.0f, 1.0f },  // right AP2
        { 0, 0.450f,  1.0f, 1.0f },  // left delay1, mid
        { 1, 0.540f, -1.0f, 1.0f },  // left delay2, mid
        { 2, 0.210f, -1.0f, 1.0f },  // left AP2
        { 4, 0.310f, -1.0f, 1.0f },  // right delay2, early
    };

    static constexpr OutputTap kRightOutputTaps[kNumOutputTaps] = {
        { 0, 0.140f,  1.0f, 1.0f },  // left delay1, early
        { 0, 0.710f,  1.0f, 1.0f },  // left delay1, late
        { 2, 0.520f, -1.0f, 1.0f },  // left AP2
        { 3, 0.410f,  1.0f, 1.0f },  // right delay1, mid
        { 4, 0.580f, -1.0f, 1.0f },  // right delay2, mid
        { 5, 0.240f, -1.0f, 1.0f },  // right AP2
        { 1, 0.350f, -1.0f, 1.0f },  // left delay2, early
    };

    // -----------------------------------------------------------------------
    // Parameters
    double sampleRate_ = 44100.0;
    float decayTime_ = 1.0f;
    float bassMultiply_ = 1.0f;
    float midMultiply_ = 1.0f;            // 3-band mid (NEW; default 1.0 = no mid colour)
    float trebleMultiply_ = 0.5f;
    float crossoverFreq_ = 1000.0f;
    float highCrossoverFreq_ = 20000.0f;  // Second crossover for 3-band damping (Hz)
    float saturationAmount_ = 0.0f;       // 0..1 drive — sets softClip threshold/ceiling
    float modDepthSamples_ = 8.0f;  // Peak excursion in samples
    float lastModDepthRaw_ = 0.5f;  // Raw 0-1 depth before sample rate scaling
    float modRateHz_ = 1.0f;
    float sizeParam_ = 0.5f;
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;
    float sizeRangeAllocatedMax_ = 4.0f;
    float lateGainScale_ = 1.0f;
    float delayScale_ = 1.0f;  // Global delay multiplier (set before prepare)
    float softOnsetMs_ = 0.0f;    // Output onset ramp time (ms). Smooths early transient spike.
    float softOnsetCoeff_ = 1.0f; // Per-sample increment for onset ramp
    float softOnsetEnvL_ = 0.0f;  // Current ramp value

    // Peak limiter: reduces transient peaks while preserving RMS (lowers crest factor).
    float lastStructHFHz_ = 0.0f;        // Cached for replay after sample rate change
    float limiterThreshold_ = 0.0f;      // 0 = disabled. Linear amplitude threshold.
    float limiterReleaseCoeff_ = 0.999f;  // One-pole release coefficient (~50ms at 48kHz)
    float limiterReleaseMs_ = 50.0f;      // Cached for recompute on sample rate change

    float limiterEnv_ = 0.0f;             // Current peak envelope

    bool frozen_ = false;
    bool prepared_ = false;

    float decayBoost_ = 1.0f;
    float baseLowCrossoverCoeff_ = 0.85f;
    float structHFCoeff_ = 0.0f;
    float structHFStateL_ = 0.0f;
    float structHFStateR_ = 0.0f;
    // Dattorro coefficients
    float decayDiff1_ = 0.70f;   // Modulated allpass feedback
    float decayDiff2_ = 0.50f;   // Static allpass feedback
    // Density cascade feedback baseline. setTankDiffusion() scales this around
    // its baseline; 0.55 matches the reference hardware hall-density convention (Dattorro
    // 1997 used 0.5/0.625) and replaces the old 0.18 which was too low to
    // actually diffuse — at 0.18 each AP rang at its delay period instead of
    // smearing, producing audible discrete tap echoes in the tail.
    static constexpr float kDensityDiffBaseline_ = 0.55f;
    float densityDiffCoeff_ = kDensityDiffBaseline_;

    // Dense-tail path (plate density rework). Default = legacy (3 APs, no boost,
    // full modulation) so existing presets are byte-identical until they opt in.
    //   numActiveDensityAPs_ : 3 (legacy) or 6 (dense) — more Schroeder stages =
    //     more echoes/sec = smoother tail WITHOUT modulation (the Lex/VVV way).
    //   densityCoeffBoost_   : multiplies densityDiffCoeff_ (clamped <0.85 in loop).
    //   modReduction_        : scales AP1 + delay1/2 modulation toward 0 to pull the
    //     tail near-still (the user hears ours wobble more than Lex/VVV). 1.0 = legacy.
    int   numActiveDensityAPs_ = 3;
    float densityCoeffBoost_   = 1.0f;
    float modReduction_        = 1.0f;
    bool  densityRoomFill_     = false;                       // #87: false = room density bases (bit-null)
    float mainDetune_[4]       = { 1.0f, 1.0f, 1.0f, 1.0f };  // #87: {Ldel1,Ldel2,Rdel1,Rdel2}; identity = bit-null

    // Per-octave GEQ state. octaveActive_ false (all T60 == 0) → legacy 3-band,
    // bit-identical. Inter-octave crossovers = geometric means of the ISO
    // centres (the full_check T60-gate band edges), shared with AccurateHall.
    bool  octaveActive_   = false;
    float octaveT60_[9]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    float octaveDecayRef_ = 0.0f;   // <0.05 → knob scale pinned at 1.0
    static constexpr float kOctaveXoverHz[8] =
        { 88.4f, 176.8f, 353.6f, 707.1f, 1414.2f, 2828.4f, 5656.9f, 11313.7f };

    // Static output tonal-correction GEQ (stereo). Applied ONCE to the wet
    // output (not recirculated) → steady-state spectral shaping decoupled from
    // decay. Identity (all gains 1.0) → bit-null. Coeffs designed message-thread.
    OctaveBandDamping          tonalCorrL_, tonalCorrR_;
    OctaveBandDamping::Coeffs  tonalCorrCoeffs_ {};
    bool  tonalCorrActive_     = false;
    float tonalCorrGain_[9]    = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };   // linear per-octave
    void  updateTonalCorr();

    // Reverse-buildup (slow-attack) state. A TRIGGERED one-shot power-curve ramp:
    // on an onset-after-silence the output gain rises env = rampPos^bloomExp_
    // over bloomAttackMs_, so it stays LOW early (suppressing the tank's intrinsic
    // ~60 ms peak — what a gentle one-pole follower could NOT do) then peaks late
    // and HOLDS at 1 (tail rings undamped). Re-arms after bloomRearmSamples_ of
    // input silence so each phrase-onset swells. bloomEnv_ pinned 1.0 inactive →
    // bit-null.
    bool  bloomActive_      = false;
    float bloomAttackMs_    = 0.0f;
    float bloomEnv_         = 1.0f;   // output swell gain (1 = fully open)
    float bloomRampPos_     = 1.0f;   // 0..1 ramp progress (1 = done/held)
    float bloomRampInc_     = 1.0f;   // per-sample ramp increment (1/peakSamples)
    float bloomExp_         = 2.5f;   // power-curve exponent (>1 → suppress early)
    int   bloomQuietSamples_   = 0;   // consecutive near-silent input samples
    int   bloomRearmSamples_   = 6615; // input-silence length that re-arms a swell

    // Input diffusion cascade state. Mono (single chain) — the tank input is a
    // mono sum and the figure-8 + 7-tap output decorrelates to stereo, exactly
    // as in Dattorro. Static (no jitter): initial diffusion must be still, not
    // modulated. Engaged by setDensityDepth (>0); inactive = bit-identical.
    Allpass inputDiffuser_[kNumInputDiffusers];
    bool    inputDiffusionActive_ = false;
    float   inputDiffCoeffScale_  = 1.0f;   // sweep handle; 1.0 = canonical coeffs

    // Delay-read modulation depth (peak excursion in samples). Applied to
    // delay1 and delay2 read taps via per-tank RandomWalkLFOs. Replaces the
    // earlier per-sample white-noise jitter; the smooth wander breaks modal
    // resonances without producing audible FM sidebands.
    float delayModDepthSamples_ = 4.0f;

    // Density-AP wander depth fraction (see setDensityJitter). 0.02 = legacy.
    float densityJitterFraction_ = 0.02f;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();

    // Resolves an output tap to an actual sample offset for the current delay lengths.
    float readOutputTap (const OutputTap& tap) const;

    // Per-algorithm configurable output taps (override static defaults)
    OutputTap customLeftTaps_[kNumOutputTaps] {};
    OutputTap customRightTaps_[kNumOutputTaps] {};
    bool useCustomTaps_ = false;
};
