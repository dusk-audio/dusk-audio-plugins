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
    void setMidMultiply (float mult);                  // NEW: 3-band mid mult (default 1.0)
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation (float amount);                 // NEW: 0..1 drive-style softClip
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
    // Density cascade active stage count (1..kNumDensityAPs). Default 3 to
    // preserve Engine 10 + plate engine voicing. Engine 15 (LexFigure8)
    // calls setDensityStages(4) at prepare time to engage the 4th AP for
    // modal smearing (box_ratio + spectral_crest reduction).
    void setDensityStages (int stages);
    // Density-AP per-stage jitter controls. Default values match the
    // historical hardcoded constants (0.02 depth / 1.5 Hz rate) so any
    // engine that does NOT call these setters (Engine 10, plate engines)
    // sees bit-for-bit identical behavior. LexFigure8 (Engine 15)
    // exposes these via APVTS for spectral-comb smearing of box_ratio
    // and spectral_crest residuals.
    void setDensityJitterDepth (float frac);
    void setDensityJitterRate  (float hz);
    // Sub-bass band damping. At multiply=1.0 (default) the shelf is
    // bypassed; Engine 10 + plates stay bit-for-bit identical. LexFigure8
    // engages this to split bass below crossover into a 4th sub-bass band
    // for finer rt60_per_band shaping.
    void setSubBassMultiply  (float mult);
    void setSubBassCrossover (float hz);
    // Phase D — in-loop tilt high-shelf. dbPerOctave clamped ±12 dB.
    // Default 0 = neutral / shelf bypassed. Engine 10 + plates never
    // call this → preserved bit-for-bit.
    void setStructuralTilt (float dbPerOctave);
    // Phase F — 4th damping band (air, above main treble crossover).
    // Multiplier 1.0 = neutral / shelf bypassed (Engine 10 + plates
    // preserved bit-for-bit). Crossover 4–12 kHz, typical 8 kHz pivot.
    void setAirMultiply  (float mult);
    void setAirCrossover (float hz);
    // Phase G — 8-band damping per octave (rt60_per_band bin alignment).
    // idx 0..7 = bins 125/250/500/1k/2k/4k/8k/16k Hz. Multiplier 1.0
    // (default for all bands) keeps eightBandActive_ false → uses
    // ThreeBandDamping (Engine 10 + plates preserved bit-for-bit).
    // Auto-enables when ANY band multiplier moves off 1.0.
    void setEightBandMultiply (int bandIdx, float mult);
    // Density-AP per-stage delay override (LexFigure8 only). ms > 0 replaces
    // the hardcoded densityAPBase[stageIdx] (set by setHallScale) with a
    // runtime ms value rounded to integer samples at 44.1 kHz reference.
    // ms <= 0 clears the override (default 0 → no effect → Engine 10 +
    // plates preserved bit-for-bit since they never call this).
    void setDensityAPDelayMs (int stageIdx, float ms);
    // Per-output-tap positionFrac override. channel 0 = left, 1 = right.
    // tapIdx 0..kNumOutputTaps-1. frac is clamped [0,1] and writes the
    // positionFrac field of customLeftTaps_/customRightTaps_; the first
    // call switches useCustomTaps_ on (which is per-instance, so other
    // DattorroTank consumers — Engine 10, Dattorro engine, plates —
    // remain bit-for-bit identical because each engine owns its own
    // tank instance and they never call this setter).
    void setOutputTapFraction (int channel, int tapIdx, float frac);
    // Per-channel delay1/delay2 base override in ms at 44.1 kHz reference.
    // channel 0 = left, 1 = right. delayIdx 0 = delay1, 1 = delay2.
    // ms > 0 replaces the hardcoded Tank::delay1BaseDelay / delay2BaseDelay
    // (set by setHallScale) with a runtime ms value. ms <= 0 clears the
    // override. Defaults at 0 → Engine 10 + plates preserved bit-for-bit.
    // Range clamped to kDelayBaseOverrideMaxMs (150 ms) which matches the
    // allocation worst case in prepare().
    void setDelayBaseMs (int channel, int delayIdx, float ms);
    // Per-channel ap1/ap2 base override in ms at 44.1 kHz reference (v30
    // diffuser rearch). channel 0 = left, 1 = right. apIdx 0 = ap1
    // (modulated allpass), 1 = ap2 (static allpass). ms > 0 replaces the
    // hardcoded Tank::ap1BaseDelay / ap2BaseDelay (set by setHallScale).
    // Range clamped to kAPBaseOverrideMaxMs (100 ms). Engine 10 + plates
    // leave overrides at 0 → preserved bit-for-bit.
    void setAPBaseMs (int channel, int apIdx, float ms);
    // Per-channel cross-feed coefficient (v31 front-door rearch). Multiplier
    // applied to the other tank's cross-feed signal at this tank's input.
    // Default 1.0 = Dattorro canonical (full bleed). Range [0, 2]. Engine
    // 10 + plates leave at 1.0 → bit-for-bit identical. LexFigure8 tunes
    // to reshape centroid_drift_per_band via inter-tank coupling control.
    void setCrossFeedCoeff (int channel, float coeff);
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

    // Non-modulated Schroeder allpass with optional Lexicon-style "spin and
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
        // jitterRateHz overridable per-AP. Default 1.5 Hz matches the
        // hardcoded sub-audio rate from issue #87 (audio-band rates
        // generated FM sidebands → vibrato artifact).
        float                   jitterRateHz        = 1.5f;

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
            jitterLFO.setRate (jitterRateHz);
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
    // Density cascade: up to 4 allpasses between delay1 and damping.
    // Multiplies echo density ~8-16× per loop pass (each AP doubles mode
    // count). Delays are prime and coprime to all other elements. Active
    // stage count gated by densityStages_ runtime setter (default 3 to
    // preserve Engine 10 + plate engines that share this tank class).
    // Engine 15 (LexFigure8) calls setDensityStages(4) to enable the
    // 4th stage for box_ratio + spectral_crest mitigation.
    static constexpr int kNumDensityAPs = 4;

    // Left tank density AP delays (at 44100 Hz). Stages 0-2 = room mode.
    // Stage 3 added for LexFigure8 (kept coprime to other delays).
    static constexpr int kLeftDensityAPBase[kNumDensityAPs] = { 137, 199, 281, 359 };
    // Right tank density AP delays (at 44100 Hz)
    static constexpr int kRightDensityAPBase[kNumDensityAPs] = { 149, 211, 263, 353 };

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
    static constexpr int kLeftDensityAPBaseHall[kNumDensityAPs]  = { 307, 421, 577, 83 };
    static constexpr int kRightDensityAPBaseHall[kNumDensityAPs] = { 337, 461, 541, 89 };

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

        // Sub-bass shelf — optional 4th band below the main bass crossover.
        // Only engaged when subBassActive_ == true (LexFigure8 only). At
        // multiply=1.0 (default) the shelf is bypassed entirely so Engine 10
        // and plate engines see ZERO additional processing.
        ShelfBiquad subBassShelf;

        // Phase D — in-loop tilt high-shelf at 2 kHz pivot. Negative dB
        // darkens the late tail (centroid_drift bins shift DOWN more);
        // positive brightens. Bypassed unless tiltActive_ == true.
        ShelfBiquad tiltShelf;

        // Phase F — in-loop air-band high-shelf (4th damping band) at
        // 8 kHz pivot. Independent decay multiplier above 8 kHz creates
        // the HF-fade-faster-than-mid gradient that Lex hardware exhibits.
        // Bypassed unless airActive_ == true.
        ShelfBiquad airShelf;

        // Phase G — 8-band damping (per-octave RT60 control). When
        // eightBandActive_ is true, this replaces ThreeBandDamping in
        // the process loop. Engine 10 + plates leave it inactive.
        EightBandDamping eightBandDamping;

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
    // its baseline; 0.55 matches the Lexicon hall-density convention (Dattorro
    // 1997 used 0.5/0.625) and replaces the old 0.18 which was too low to
    // actually diffuse — at 0.18 each AP rang at its delay period instead of
    // smearing, producing audible discrete tap echoes in the tail.
    static constexpr float kDensityDiffBaseline_ = 0.55f;
    float densityDiffCoeff_ = kDensityDiffBaseline_;
    int   densityStages_     = 3;  // Engine 10 default; LexFig8 sets to 4.
    // Density-AP jitter tunables. Defaults preserve Engine 10 + plate
    // behavior. LexFigure8 sets these via APVTS to smear box_ratio /
    // spectral_crest comb teeth at 200-500 Hz.
    float densityJitterDepth_ = 0.02f;
    float densityJitterRate_  = 1.5f;
    // Sub-bass band — disabled when multiply == 1.0 (default).
    // Engine 10 + plates leave this at default → bit-for-bit identical.
    float subBassMultiply_  = 1.0f;
    float subBassCrossover_ = 300.0f;
    bool  subBassActive_    = false;
    // Phase D — tilt EQ state. Pivot fixed at 2 kHz.
    float tiltDbPerOctave_  = 0.0f;
    bool  tiltActive_       = false;
    // Phase F — air band state. Disabled when multiply == 1.0 (default).
    float airMultiply_  = 1.0f;
    float airCrossover_ = 8000.0f;
    bool  airActive_    = false;
    // Phase G — 8-band damping per-octave multipliers. Default 1.0 = neutral
    // (eightBandActive_ stays false). Engine 10 + plates leave at default.
    float eightBandMultiply_[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    bool  eightBandActive_       = false;
    // Per-stage density-AP delay override in ms. 0 = no override → tank
    // uses densityAPBase set by setHallScale. > 0 = override applied at
    // updateDelayLengths. Engine 10 + plates leave at 0 → bit-for-bit
    // identical. LexFigure8 reshapes peak_locations via APVTS.
    float densityAPDelayMsOverride_[kNumDensityAPs] = {};
    // Per-channel delay1/delay2 base ms override. Index 0 = left, 1 = right.
    // 0 = no override (use Tank::delay1BaseDelay / delay2BaseDelay set by
    // setHallScale). > 0 = override applied. Engine 10 + plates leave 0 →
    // bit-for-bit identical. LexFigure8 lets CMA reshape the tank's room
    // size + modal-frequency structure for the last 4 metric blocks.
    float delay1BaseMsOverride_[2] = {};
    float delay2BaseMsOverride_[2] = {};
    static constexpr float kDelayBaseOverrideMaxMs = 150.0f;
    // v30 — per-channel AP1/AP2 base ms override. Engine 10 + plates leave
    // at 0 → bit-for-bit identical. LexFigure8 reshapes diffuser geometry.
    float ap1BaseMsOverride_[2] = {};
    float ap2BaseMsOverride_[2] = {};
    static constexpr float kAPBaseOverrideMaxMs = 100.0f;
    // v31 — per-channel cross-feed coefficient. Default 1.0 = Dattorro
    // canonical bleed. Engine 10 + plates leave at 1.0 → preserved.
    float crossFeedCoeff_[2] = { 1.0f, 1.0f };

    // Delay-read modulation depth (peak excursion in samples). Applied to
    // delay1 and delay2 read taps via per-tank RandomWalkLFOs. Replaces the
    // earlier per-sample white-noise jitter; the smooth wander breaks modal
    // resonances without producing audible FM sidebands.
    float delayModDepthSamples_ = 4.0f;

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
