#pragma once

#include "DattorroTank.h"
#include <vector>

// =====================================================================
// LexFigure8Reverb — Engine 15
// =====================================================================
//
// Classic Lexicon/Dattorro Figure-8 reverberator. The underlying DSP is
// `DattorroTank` (cross-coupled stereo tanks with nested AP / delay / LP
// cascades and Lex-style spin-and-wander RandomWalkLFO on the modulated
// allpasses — see DattorroTank.h header comment for the canonical
// 1997 topology description).
//
// Phase A — Pre-tank ER TDL added in parallel with tank output.
// Lex Med Hall peak_locations_ms anchor = [0.0, 4.0, 7.52, 9.79] ms.
// LexFigure8 tank alone places its earliest density-AP outputs at
// ~1.6 / 3.1 / 4.8 ms (too tight). The ER TDL produces 4 discrete
// impulse taps that match Lex peak positions exactly. Tank carries
// late tail; ER carries peak structure.
//
// Topology:
//
//   in (L,R)
//     ├──► ER TDL (4 taps, stereo decorrelated) ──► +
//     │                                              ↑
//     └──► DattorroTank ───────────────────────────► +
//                                                    ↓
//                                                out (L,R)

class LexFigure8Reverb
{
public:
    LexFigure8Reverb() = default;

    static constexpr int kNumERTaps = 4;

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    // Process signature — rawInL/R carry the unfiltered input (no pre-delay,
    // no input AP cascade, no shell filters) for ER capture. tankInL/R
    // carry the pre-delayed + diffused input for the DattorroTank. outL/R
    // receives ONLY the tank output — ER lives in erOutL_/erOutR_ for the
    // engine layer to add AFTER shell processing (lo/hi cut, M/S width).
    void process (const float* rawInL, const float* rawInR,
                  const float* tankInL, const float* tankInR,
                  float* outL, float* outR, int numSamples);

    const float* getERBufferL() const { return erOutL_.data(); }
    const float* getERBufferR() const { return erOutR_.data(); }

    DattorroTank tank;

    void setFreeze (bool frozen) { tank.setFreeze (frozen); }

    void setStructuralHFDamping (float hz) { tank.setStructuralHFDamping (hz); }

    // ER TDL setters. idx 0..3. delayMs in milliseconds (0..30).
    // gainDb in decibels (-60..+6). stereoOffsetMs added to all R taps.
    void setERTapDelay      (int idx, float ms);
    void setERTapGainDb     (int idx, float db);
    void setERStereoOffset  (float ms);

    // Tank output attenuation (0..1). Scales tank's 7-tap output sum
    // independently of ER. Lets the tuner balance ER vs tank energy
    // to overwhelm tank density-AP early peaks at 0.25/1.27/2.27 ms
    // that otherwise dominate peak_locations_ms detector.
    void setTankAtten (float scale) { tank.setLateGainScale (scale); }

    // Tank input attenuation (0..1). Pre-scales the audio fed into
    // DattorroTank so its density-AP early peaks shrink proportionally
    // while ER taps stay at full amplitude. Lets ER dominate the
    // peak detector's height gate without tank reduction killing
    // the late tail simultaneously (setTankAtten reduces BOTH).
    void setTankInputScale (float scale);

    // Tank pre-delay (ms). Internal delay applied to tank input ONLY
    // (ER taps unaffected). Shifts tank's density-AP early peaks
    // PAST the ER tap times so peak_locations detector finds ER
    // taps as the dominant first 4 peaks.
    void setTankPreDelay (float ms);

    // Density-AP jitter forwarders — let APVTS sweep the
    // RandomWalkLFO depth/rate to smear modal comb teeth at
    // 200-500 Hz (box_ratio + spectral_crest mitigation).
    // Engine 10 + plates never call these → preserved bit-for-bit.
    void setDensityJitterDepth (float frac) { tank.setDensityJitterDepth (frac); }
    void setDensityJitterRate  (float hz)   { tank.setDensityJitterRate  (hz); }
    // Phase C — 4th band damping (sub-bass split below main bass xover).
    // Engine 10 + plates never call these → preserved bit-for-bit.
    void setSubBassMultiply  (float mult)   { tank.setSubBassMultiply  (mult); }
    void setSubBassCrossover (float hz)     { tank.setSubBassCrossover (hz); }
    // Phase D — in-loop tilt high-shelf at 2 kHz pivot.
    void setStructuralTilt (float dbPerOctave) { tank.setStructuralTilt (dbPerOctave); }
    // Phase F — air band (4th damping). multiply=1.0 = neutral.
    void setAirMultiply  (float mult) { tank.setAirMultiply  (mult); }
    void setAirCrossover (float hz)   { tank.setAirCrossover (hz); }
    // Phase G — 8-band damping per octave (rt60_per_band alignment).
    void setBandMultiply (int idx, float mult) { tank.setEightBandMultiply (idx, mult); }
    // Phase J — per-stage density-AP delay override (ms at 44.1 kHz reference).
    // 0 = no override; tuner uses > 0 to reshape peak_locations to Lex anchor.
    // Engine 10 + plates never call this → preserved bit-for-bit.
    void setDensityAPDelayMs (int stageIdx, float ms) { tank.setDensityAPDelayMs (stageIdx, ms); }
    // Phase K — per-tap output position override (v28 output-tap rearch).
    // channel 0=L 1=R, tapIdx 0..6, frac 0..1. Reshapes tank's natural
    // early-energy emission geometry so peak_locations spacing + time_domain_crest
    // can be tuned independent of ER. Engine 10/plates never call → preserved.
    void setOutputTapFraction (int channel, int tapIdx, float frac) { tank.setOutputTapFraction (channel, tapIdx, frac); }
    // Phase L — per-channel delay1/delay2 base ms override (v29 Lexicon-primes
    // rearch). channel 0=L 1=R, delayIdx 0=delay1 1=delay2, ms > 0 = override.
    // Reshapes fundamental modal density + output tap absolute timing so CMA
    // can attack rt60_per_band / c80_per_octave / centroid_drift jointly.
    // Engine 10/plates never call → preserved bit-for-bit.
    void setDelayBaseMs (int channel, int delayIdx, float ms) { tank.setDelayBaseMs (channel, delayIdx, ms); }
    // Phase M — per-channel AP1/AP2 base ms override (v30 diffuser rearch).
    // channel 0=L 1=R, apIdx 0=ap1 (modulated) 1=ap2 (static). Reshapes
    // diffuser smear → time_domain_crest + transient density.
    void setAPBaseMs (int channel, int apIdx, float ms) { tank.setAPBaseMs (channel, apIdx, ms); }
    // Phase N — per-channel cross-feed coefficient (v31 stereo-bleed lever).
    // Default 1.0 = canonical Dattorro figure-8 bleed. Tunes inter-tank
    // coupling → centroid_drift_per_band via phase-cancellation control.
    void setCrossFeedCoeff (int channel, float coeff) { tank.setCrossFeedCoeff (channel, coeff); }

    // Phase H — Transient-gated tank ducker (non-LTI).
    // Envelope follower on raw input drives per-sample ducker gain
    // applied to TANK INPUT ONLY. ER taps bypass the ducker entirely.
    // When impulse hits, env spikes → ducker mutes tank for the
    // transient window → ER fills the void uncontested. When noiseburst
    // settles, env stays high but the steady-state envelope is at
    // expected level → ducker releases → tank receives full energy.
    // Threshold (peak amp 0..1), attack/release (ms), depth (0..1).
    void setDuckerThreshold (float thresh);
    void setDuckerAttackMs  (float ms);
    void setDuckerReleaseMs (float ms);
    void setDuckerDepth     (float depth);

private:
    bool   prepared_     = false;
    double sampleRate_   = 48000.0;

    std::vector<float> erBuf_;
    int   erBufMask_      = 0;
    int   erWriteIdx_     = 0;

    float tapDelayMs_   [kNumERTaps] = { 0.0f, 4.0f, 7.52f, 9.79f };
    float tapGainLin_   [kNumERTaps] = { 0.70794578f, 0.50118723f, 0.35481339f, 0.25118864f };  // -3, -6, -9, -12 dB
    float stereoOffsetMs_            = 0.20f;

    float tapDelaySamp_ [kNumERTaps] = {};
    float stereoOffsetSamp_          = 0.0f;
    float tankInputScale_            = 1.0f;

    std::vector<float> tankInL_;
    std::vector<float> tankInR_;
    std::vector<float> erOutL_;
    std::vector<float> erOutR_;
    std::vector<float> erGate_;   // Per-sample transient gate for ER (Phase H)

    std::vector<float> tankPreDelayL_;
    std::vector<float> tankPreDelayR_;
    int   tankPreDelayMask_  = 0;
    int   tankPreDelayWrite_ = 0;
    float tankPreDelayMs_    = 0.0f;
    int   tankPreDelaySamp_  = 0;

    // Phase H — ducker state (one-pole peak follower)
    float duckerThreshold_   = 0.0f;     // 0 = disabled
    float duckerAttackMs_    = 0.1f;
    float duckerReleaseMs_   = 8.0f;
    float duckerDepth_       = 0.0f;     // 0 = no duck
    float duckerAttackCoeff_ = 0.0f;
    float duckerReleaseCoeff_= 0.0f;
    float duckerEnvState_    = 0.0f;
    float duckerSlowState_   = 0.0f;
    float duckerSlowCoeff_   = 0.0f;
    float duckerEnergyState_ = 0.0f;   // Phase H v23 — leaky integrator
    float duckerGateState_   = 0.0f;   // Held gate w/ release
    // Phase H v24 — true 32-tap rolling sum (matches WAV analysis).
    static constexpr int kEnergyWindowSize = 32;
    float duckerEnergyBuf_[kEnergyWindowSize] = {};
    int   duckerEnergyIdx_   = 0;
    float duckerEnergySum_   = 0.0f;
    bool  duckerActive_      = false;

    void recomputeDuckerCoeffs();

    void recomputeTapSamples();

    // Smoke J — Edge-triggered click synthesizer (non-LTI).
    // Detect rising-edge transients in raw input via first-difference;
    // when delta exceeds clickThresh_, fire a single-sample 1.0 into
    // a ring buffer. Sum 3 delayed reads (113/227/349 ms) into ER output
    // stream. Refractory period prevents re-trigger during sustained
    // bursts. Tank untouched. ER output gets clicks as +clickGain peaks
    // that show in noiseburst envelope post-100ms (lifting tdc).
    static constexpr int   kNumClickDelays = 3;
    static constexpr float kClickDelayMs[kNumClickDelays] = { 113.0f, 227.0f, 349.0f };
    float clickThresh_              = 0.001f;     // very low — trigger easily
    float clickGainLin_             = 0.0f;       // OFF — wire APVTS axis for CMA
    float clickRefractoryMs_        = 300.0f;
    int   clickRefractorySamp_      = 0;
    int   clickRefractoryCountdown_ = 0;
    float clickPrevInput_           = 0.0f;
    int   clickTriggerCount_        = 0;
    int   clickBurstCountdown_      = 0;
    int   clickBurstLenSamp_        = 0;     // 5 ms boxcar
    std::vector<float> clickImpulseBuf_;
    int   clickImpulseMask_         = 0;
    int   clickImpulseWrite_        = 0;
    int   clickDelaySamp_[kNumClickDelays] = {};
};
