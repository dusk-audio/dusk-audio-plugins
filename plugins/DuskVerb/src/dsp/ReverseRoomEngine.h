#pragma once

#include "VelvetTail.h"

#include <vector>

// ReverseRoomEngine — the gated-reverse engine (algo 9, the single preset
// "Reverse Taps"), reverse-engineered from the GATED lex-reverse-1 anchor:
//
//   * NOT backwards-convolution — fully CAUSAL, no time-reversal, no latency.
//   * The "reverse" character is a RISING-GAIN early-reflection onset: discrete
//     taps whose gains ramp UP over the first ~rampMs_ to a peak (the "Tap
//     Slope", gain ~ (t/ramp)^slope_; slope_<1 = concave) — the swell.
//   * An INPUT-KEYED hard gate (keyed off |dry input|, NOT the tail — the tail
//     never falls below threshold) supplies the pre-onset silence + post-peak
//     cliff: it opens on input, HOLDS while input is present (duration-dependent
//     hold), then HARD-releases to near-silence → the anchor's gated envelope
//     (env_p2p ~+72 dB), cutting the tail to its short per-band T60.
//
// Signal flow:  input -> [rising-ER swell FIR] -> [VelvetTail tail] -> [gate] -> out
// The swell-ER shapes the dry input into a rising-onset burst that feeds the
// feed-FORWARD VelvetTail (sparse velvet-noise FIR: per-band tap-gain decay, NO
// recirculation → no mid-decay floor, per-band decay/level/brightness/stereo all
// decouple). The input-keyed gate then cuts the tail to the anchor's short T60 +
// hard cliff. (Replaces the original FDNReverb tail, which floored the mids ~0.3 s
// and ran too dark/correlated for this short, bright, wide gated reverse.)
//
// UI knob mapping (this engine — see PluginEditor::applyEngineAccent):
//   DECAY      -> VelvetTail global decay scale (setGlobalDecayScale)
//   SIZE       -> VelvetTail size scale + ER tap span (scales the onset duration)
//   DIFFUSION  -> ER tap density (setTankDiffusion → rebuildTaps)
//   All other universal setters (Bass/Mid/Treble Multiply, Lo/Hi Crossover,
//   Saturation, Mod Depth/Rate, Tail Spin, Freeze) are NO-OPS on this engine —
//   the VelvetTail per-band T60/level/brightness + the gate timing are baked
//   engine constants (the fixed "Reverse" signature: rampMs_/slope_/holdMs_/
//   closeTauMs_/bandT60_), tuned to the reference, not exposed as knobs.
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

    // Dark modulated diffuse tail, fed by the rising-ER output (so the tail
    // inherits the swell). FDNReverbT<true> = AccurateHall GEQ variant: a
    // per-OCTAVE loop-attenuation GEQ (setOctaveT60) shapes each band's decay +
    // level. The anchor's per-band T60 spans 0.17-0.47s on SUSTAINED but its
    // IMPULSE tail is 0.094s — reconciled by the DURATION-DEPENDENT hold below
    // (long input → long hold → bands decay naturally → GEQ sets per-band T60;
    // short input → short hold → the gate sets the fast impulse tail).
    VelvetTail velvet_;

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
    // GATED anchor (lex-reverse-1 re-measured 2026-06-17): ~102ms predelay →
    // concave swell to peak ~320ms → HARD CLIFF to silence. slope_<1 = concave
    // (gain ~ (t/ramp)^0.7, fast initial rise into the peak). The input-keyed
    // GATE (below) supplies the pre-onset silence + post-peak cliff, so floorGain_
    // drops to near-zero (the old 0.45 dodged an env_p2p cliff vs the OLD UNGATED
    // anchor — that anchor/verdict is stale; the current anchor IS +72dB gated).
    float rampMs_    = 470.0f;  // onset RISE to the peak (swept 2026-06-17: attack≈345ms match)
    float slope_     = 0.7f;    // Tap Slope; <1 = concave (was 1.6 convex)
    float floorGain_ = 0.05f;   // near-zero: gate owns silence/cliff (was 0.45)
    float density_   = 0.85f;   // from setTankDiffusion
    float sizeScale_ = 1.0f;    // from setSize, scales ER span

    // ── Input-keyed hard gate (the GATED-reverse signature) ──────────────────
    // Keyed off the DRY input |inL|+|inR|, NOT the FDN output (the tail never
    // falls below threshold → the gate would never close). One mono smoothed
    // scalar multiplies BOTH output channels (broadband — no per-tap gating →
    // no comb re-modulation, no click). All POD → deterministic, alloc-free.
    enum class GateState { Idle, Open, Hold, Closing };
    GateState gateState_   = GateState::Idle;

    float envFollow_   = 0.0f;     // dry-input peak-envelope follower (linear)
    float envAtkCoeff_ = 0.0f;     // one-pole attack coeff  (~1.5 ms)
    float envRelCoeff_ = 0.0f;     // one-pole release coeff (~80 ms)
    float threshLin_   = 0.0178f;  // gate open threshold (~ -35 dBFS; swept: starts Hold before the snare body fully decays)

    float gateGain_     = 0.0f;    // current smoothed output gain (starts closed)
    float gateFloorLin_ = 3.16e-5f;// closed floor ~ -90 dBFS (not literal 0)
    float gateOpenCoeff_  = 0.0f;  // one-pole open slew  (~6 ms)
    float gateCloseCoeff_ = 0.0f;  // one-pole close slew (tau ~6 ms → cliff)

    int   holdCounter_ = 0;        // samples remaining in Hold before Closing
    int   holdSamples_ = 0;        // = holdMs_ * sr (base hold, retriggerable)
    float holdMs_      = 340.0f;   // BASE hold ms (≈ impulse; short input) (swept)
    float closeTauMs_  = 3.0f;     // close slew tau → hard cliff (swept)

    // DURATION-DEPENDENT hold: hold grows with how long input has been present in
    // this burst, so an impulse cuts fast (impulse tail_t60 0.094s) while
    // sustained holds long enough that the per-octave GEQ decay (≤0.47s) shows as
    // the measured per-band T60 instead of being truncated by the gate.
    int   inputActiveSamps_ = 0;   // samples input has been present this burst
    int   holdMaxSamps_     = 0;   // = holdMaxMs_ * sr (ceiling)
    float holdPerSec_       = 60.0f;  // +ms hold per second of input presence
    float holdMaxMs_        = 450.0f; // hold ceiling (sustained). 2026-07-04 600->450: on the 22.6 s piano stem the gate held ~0.5 s of loud low-mid past where the Lex reverse had closed (piano-tail gate +21 dB); 450 tracks the anchor's release without touching the noiseburst T60s (hold there is duration-scaled ~346 ms, under both caps).

    double sampleRate_ = 48000.0;
    int    maxBlock_   = 0;
    bool   prepared_   = false;
    bool   baselineApplied_ = false;   // apply the dark "Reverse 1" baseline to
                                       // fdn_ only ONCE; later prepares (sample-
                                       // rate / block changes) must NOT clobber
                                       // preset values the setters pushed into fdn_.
};
