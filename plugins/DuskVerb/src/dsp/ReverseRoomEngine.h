#pragma once

#include "FDNReverb.h"

#include <vector>

// ReverseRoomEngine — replicates the Lexicon PCM Room "Reverse 1" preset,
// reverse-engineered from the anchor data (lex-reverse-1 impulse/sine renders):
//
//   * It is NOT backwards-convolution. The impulse response peaks at ~70 ms
//     then decays (RT60 ~4.6 s) — i.e. the algorithm is fully CAUSAL, no
//     time-reversal, no latency.
//   * The "reverse" character is a RISING-GAIN early-reflection onset: discrete
//     taps at ~4.4 ms spacing whose gains ramp UP over the first ~70 ms (the
//     "Tap Slope"). That gentle swell is the whole reverse effect and gives the
//     measured env_p2p ~+24.6 dB — NOT the old NonLinear gate's +60 dB
//     silence -> blast cliff.
//   * The swell feeds a dark, modulated, diffuse tail: centroid darkens
//     9.8k -> 3k Hz over 1 s, Spin modulation ~2.7 Hz, wide stereo.
//
// Signal flow:  input -> [rising-ER tap FIR] -> [FDNReverb tail] -> output
// The ER FIR shapes the dry input into a rising-onset early-reflection burst;
// feeding the FDN in SERIES makes the diffuse tail inherit the swell, so the
// whole response rises to a peak then decays — matching the reference envelope.
//
// UI knob mapping (this engine — see PluginEditor::applyEngineAccent):
//   DECAY      -> FDN tail RT60
//   SIZE       -> FDN room size + ER tap span (scales the onset duration)
//   DIFFUSION  -> ER tap density (sparse "Concrete Stairs" <-> dense) + FDN diffusion
//   TREBLE MULT-> tail HF damping (the 9.8k->3k centroid darkening)
//   BASS/MID   -> FDN band decay
//   LOW/HI XOVER-> FDN band split
//   DEPTH/RATE -> FDN modulation (the ~2.7 Hz Spin)
//   SATURATION -> FDN input drive
// The ER ramp duration + slope are the engine's fixed "Reverse" signature
// (rampMs_/slope_), tuned to match the reference; not exposed as knobs.
class ReverseRoomEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);
    void clearBuffers();

    // Universal setter API (matches every other late-tank engine so the
    // DuskVerbEngine wrapper forwards without knowing the engine type).
    void setDecayTime         (float seconds);
    void setSize              (float size);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation        (float amount);
    void setModDepth          (float depth);
    void setModRate           (float hz);
    void setTailSpinDepth     (float depth);   // → fdn_ post-loop output AM
    void setTailSpinRate      (float hz);
    void setTankDiffusion     (float amount);   // -> ER tap density (+ FDN diffusion)
    void setFreeze            (bool frozen);

private:
    void rebuildTaps();

    // Dark modulated diffuse tail — the same FDN as the "Realistic Space"
    // engine, fed by the rising-ER output (so the tail inherits the swell).
    FDNReverb fdn_;

    // Rising-gain early-reflection FIR (the "reverse" onset). Two independent
    // tap sets (L/R) for the wide-stereo decorrelation the reference shows.
    static constexpr int kMaxTaps = 128;
    struct ERChannel
    {
        std::vector<float> ring;        // power-of-2 ring buffer
        int   writePos = 0;
        int   mask     = 0;
        int   pos [kMaxTaps] {};        // tap delays (samples back from write head)
        float gain[kMaxTaps] {};        // rising tap gains
    };
    ERChannel erL_, erR_;
    int   numTaps_ = 0;

    std::vector<float> erBufL_, erBufR_;   // ER output scratch -> FDN input

    // "Reverse" signature (tuned to lex-reverse-1). rampMs_ is the onset rise
    // duration; slope_ is the Tap Slope (gain ~ (t/ramp)^slope_).
    float rampMs_    = 45.0f;   // onset RISE duration (the "reverse" swell to the peak)
    float slope_     = 1.6f;    // Tap Slope (gain ~ (t/ramp)^slope)
    float floorGain_ = 0.45f;   // earliest-tap gain floor: onset rises from ~-7dB
                                // (not digital silence, which gave an infinite
                                // env_p2p cliff). 0.45 is the best-total config.
    float density_   = 0.85f;   // from setTankDiffusion
    float sizeScale_ = 1.0f;    // from setSize, scales ER span

    double sampleRate_ = 48000.0;
    int    maxBlock_   = 0;
    bool   prepared_   = false;
};
