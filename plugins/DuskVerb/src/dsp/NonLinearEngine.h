#pragma once

#include "FDNReverb.h"
#include "NoiseGate.h"

#include <vector>

// NonLinearEngine — v7 architecture: HALL + SIDECHAIN GATE
//
// History (so future-me doesn't loop again):
//   v1: 64-tap TDL, sparse — thin/clicky
//   v2: 4-channel FDN at 0.97 feedback — modal ringing
//   v3: 256-tap TDL + dynamic threshold gate — wrong model
//   v4: + 4-stage allpass pre-diffusion — destroyed snare transients
//   v5: classic noise-gate envelope — still wrong (missing real reverb)
//   v6: TRUE static FIR — mathematically the classic 1980s digital nonlinear algorithm,
//       but USERS DON'T WANT that algorithm. They want the
//       engineering TECHNIQUE: a long lush hall + sidechain noise gate.
//   v7 (this file): the engineering technique exactly:
//      ┌─────────┐   ┌────────────────┐   ┌─────────────┐
//      │ input ──┼─→ │ FDNReverb hall │ → │ NoiseGate   │ → output
//      │         │   │  (1-4 s decay) │   │  (gain VCA) │
//      │         │   └────────────────┘   └──────┬──────┘
//      │         │                               │
//      │         └─── trigger (sidechain) ───────┘
//      └─────────┘
//
//   The NoiseGate's trigger envelope follower listens to the DRY input
//   (inL/inR), so it opens IMMEDIATELY on snare onset. The hall reverb
//   plays in full underneath but its WET output is shaped by the gate
//   envelope (attack → hold → release). Result: thick snare bloom +
//   clean cutoff = the classic 1981 gated-drum sound.
//
// UI knob mapping (NonLinear engine only — see PluginEditor::applyEngineAccent):
//   DECAY      → Hall RT60          (FDN setDecayTime)
//   SIZE       → Hall room size     (FDN setSize)
//   BASS MULT  → Hall bass mult     (FDN setBassMultiply)
//   MID MULT   → GATE THRESHOLD     (re-purposed; -60 dB → 0 dB)
//   TREBLE MULT→ Hall treble mult   (FDN setTrebleMultiply)
//   LOW XOVER  → Hall low xover     (FDN setCrossoverFreq)
//   HIGH XOVER → Hall high xover    (FDN setHighCrossoverFreq)
//   DEPTH      → GATE ATTACK        (re-purposed; 1 - 50 ms)
//   RATE       → GATE RELEASE       (re-purposed; 5 - 2000 ms)
//   DIFFUSION  → GATE HOLD          (re-purposed; 0 - 500 ms)
//   SATURATION → Input drive (FDN setSaturation)
//   GATE button → bypass the gate stage (hear pure hall when off)
class NonLinearEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);
    void clearBuffers();

    // Universal setter API (matches every other late-tank engine so the
    // DuskVerbEngine wrapper can forward without knowing the engine type).
    // The mappings to hall/gate parameters happen inside the setters.
    void setDecayTime         (float seconds);
    void setSize              (float size);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);    // → gate THRESHOLD
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);
    void setHighCrossoverFreq (float hz);
    void setSaturation        (float amount);
    void setModDepth          (float depth);   // → gate ATTACK
    void setModRate           (float hz);      // → gate RELEASE
    void setTankDiffusion     (float amount);  // → gate HOLD
    void setFreeze            (bool frozen);
    void setGateEnabled       (bool enabled);

private:
    // Hall stage — proper 16-channel Hadamard FDN with 3-band damping,
    // diffusion, structural HF/LF filtering. Same engine as the "FDN" /
    // "Realistic Space" algorithm in DuskVerb. Configured for max-density
    // hall character (the "lushest hall preset" baseline).
    FDNReverb fdn_;

    // Gate stage — classic state-machine noise gate with 1-sample-attack
    // envelope follower. Triggered by DRY input.
    NoiseGate gate_;

    // Per-channel scratch buffers for the hall wet output. The FDN writes
    // here; if the gate is enabled it then mutates these in place; finally
    // we copy out to outL/outR. Sized to maxBlockSize in prepare().
    std::vector<float> wetL_, wetR_;

    double sampleRate_ = 48000.0;
    bool   gateEnabled_ = true;
    bool   prepared_    = false;
};
