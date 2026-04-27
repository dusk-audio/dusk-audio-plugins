#pragma once

#include <array>
#include <vector>

// NonLinearEngine — RMX16 NonLin 2 architecture, v3 (the third try):
// **dense feed-forward Tapped Delay Line** + **triggered envelope gate**.
//
// History of attempts (so future-me doesn't loop again):
//   v1: 64-tap TDL, sparse — sounded thin and clicky, no continuous body
//   v2: 4-channel small FDN with 0.97 feedback — sounded like an 8-bit
//       keyboard because short delays + high feedback produced strong
//       pitched modal ringing at 59-143 Hz + harmonics
//   v3: 256-tap TDL with NO feedback (this file). The high tap density
//       guarantees a dense plateau even on transient input; the absence
//       of feedback rules out modal ringing entirely. The "non-decaying
//       reverb pattern" per the AMS RMX16 V3.0 software notes is achieved
//       by the per-tap GAIN being constant across the length window —
//       NOT by recirculating feedback (which is what introduces tonal
//       artefacts in this sub-100ms delay range).
//
// Architecture:
//   1. Input → 1.5-second circular buffer (per channel)
//   2. 256 quasi-random tap positions distributed across `lengthSamples_`
//   3. Per-tap gain shaped by SHAPE selector (Gated/Reverse/Decaying)
//   4. Sum of all 256 taps × triggered envelope gate × anti-click ramp = out
//   5. Triggered envelope: peak follower (1-sample attack, 10ms release)
//      detects input transients, resets gate phase to 0; gate runs for
//      `lengthSamples_` samples then closes
//   6. 2ms linear anti-click ramp at gate close prevents the digital pop
//
// Knob hijacking (unchanged):
//   setDecayTime      → "LENGTH" knob: gate window in seconds (0.05 – 1.5)
//   setTankDiffusion  → "SHAPE" knob: envelope selector
//                          0.00-0.33 → Gated   (plateau then ramp-cliff)
//                          0.33-0.66 → Reverse (ramp up then ramp-cliff)
//                          0.66-1.00 → Decaying (exponential decay then cliff)
//   setSaturation     → input drive softClip
//   setBassMultiply   → output post-shelf gain
//   setSize, setMod*, setMid/Crossover/HighX/Treble → no-ops (TDL has no
//                          recirculation to modulate / damp)
class NonLinearEngine
{
public:
    enum class EnvelopeShape : int
    {
        Gated    = 0,   // RMX16 NonLin 2 plateau-then-cliff (Phil Collins snare)
        Reverse  = 1,   // ramp-up-then-cliff (pre-snare swell)
        Decaying = 2,   // exponential-decay (plate-like punchy)
    };

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);
    void clearBuffers();

    // Universal setters — same surface as every other late-tank engine.
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
    void setTankDiffusion     (float amount);
    void setFreeze            (bool frozen);

private:
    // 256 taps across the gate window. At length 350 ms this gives 1.4 ms
    // average spacing — dense enough that on a snare hit the simultaneous-
    // firing tap count (~22 within the snare's 30 ms transient) produces
    // a continuous-perception plateau, not 64-tap clickiness.
    static constexpr int   kNumTaps      = 256;
    static constexpr float kMaxLengthMs  = 1500.0f;
    static constexpr float kMinLengthMs  = 50.0f;

    // SHAPE region thresholds on the diffusion knob.
    static constexpr float kShapeGatedMax   = 1.0f / 3.0f;
    static constexpr float kShapeReverseMax = 2.0f / 3.0f;

    // Trigger threshold = −24 dBFS (0.063 linear). Tuned for typical
    // drum/percussive input levels.
    static constexpr float kTriggerThreshold = 0.0631f;

    // Anti-click ramp at gate close: 2 ms linear fade.
    static constexpr float kAntiClickRampMs = 2.0f;

    struct Tap
    {
        int   delaySamples = 0;
        float gainL        = 0.0f;
        float gainR        = 0.0f;
    };

    std::array<Tap, kNumTaps> taps_;

    // Per-channel circular buffers, sized for kMaxLengthMs at the current
    // sample rate (rounded up to the next power-of-2 for AND-mask wrap).
    std::vector<float> bufL_, bufR_;
    int writePos_ = 0;
    int mask_     = 0;

    // ── Parallel small-room FDN (the always-on "ambient bed") ─────────
    // Runs continuously at low gain alongside the gated TDL. After the
    // gate envelope closes, this small reverb is what the listener hears
    // as the "room continues to exist" tail. Matches Valhalla / classic
    // '80s outboard-rig topology where the gate attenuates a continuous
    // reverb rather than mathematically zeroing it.
    //
    // Feedback held at 0.87 (NOT 0.97 like the v2 attempt) so the modal
    // ringing that gave that version its 8-bit keyboard timbre never
    // builds up. RT60 ≈ 600 ms at this feedback × 12 ms mean delay.
    static constexpr int   kNumFDNChannels       = 4;
    // Feedback 0.90 — final-tuned via VVV comparison. RT60 ≈ 800 ms gives
    // an audible body across the +325..+475 ms post-gate transition zone
    // but lets the tail decay through −80 dB by +500 ms (the 0.93 attempt
    // sustained too long, plateauing at −68 dB indefinitely instead of
    // dying away naturally).
    static constexpr float kFDNFeedback          = 0.90f;
    // Tail mix 0.06 (≈ −24 dB) — middle ground between barely-perceptible
    // during plateau and beautifully-exposed once gate closes. With TDL
    // dominating at ~−25 dB during plateau, the body sits ~10 dB below
    // (masked but contributing texture); after gate close the body is
    // the only thing playing and reads as a natural fast-decay room.
    static constexpr float kFDNTailLevel         = 0.06f;
    static constexpr int   kFDNBaseDelays[4]     = { 311, 487, 571, 757 };  // primes, 7/11/13/17 ms @ 44.1k

    struct FDNDelay
    {
        std::vector<float> buffer;
        int writePos     = 0;
        int mask         = 0;
        int delaySamples = 0;

        void allocate (int maxSamples);
        void clear();
        inline float read() const noexcept
        {
            return buffer[static_cast<size_t> ((writePos - delaySamples) & mask)];
        }
        inline void write (float x) noexcept
        {
            buffer[static_cast<size_t> (writePos)] = x;
            writePos = (writePos + 1) & mask;
        }
    };

    std::array<FDNDelay, kNumFDNChannels> fdnDelays_;
    std::array<float,    kNumFDNChannels> fdnDampStates_ {};
    float fdnDampCoeff_ = 0.5f;     // 1-pole LP per channel (HF damping)

    double sampleRate_    = 44100.0;
    float  lengthSamples_ = 16800.0f;
    EnvelopeShape currentShape_ = EnvelopeShape::Gated;

    // ── Gate state ────────────────────────────────────────────────────
    int rampSamples_ = 96;       // 2 ms @ 48k anti-click ramp
    int gatePhase_   = -1;       // -1 = gate inactive

    // ── Envelope follower for trigger detection ───────────────────────
    float envFollower_       = 0.0f;
    float releaseCoeff_      = 0.99f;
    bool  wasAboveThreshold_ = false;

    // ── Cached params ─────────────────────────────────────────────────
    float bassMult_         = 1.0f;
    float saturationAmount_ = 0.0f;
    bool  frozen_           = false;
    bool  prepared_         = false;

    void rebuildTaps();
    static EnvelopeShape shapeFromAmount (float amount) noexcept;
    inline float computeGate (int phase) const noexcept;
};
