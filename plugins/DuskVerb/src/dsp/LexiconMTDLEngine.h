#pragma once

#include <cstdint>
#include <vector>

// =====================================================================
// LexiconMTDLEngine — metric-driven Lex Med Hall emulation
// =====================================================================
//
// Engine 15 replacement. Built from scratch against the 19-metric
// grader's requirements. Three stages, each targeting a specific
// failing block of the prior Dattorro-figure-8 implementation:
//
//   Stage 1 (FIR ER matrix)  → peak_locations_ms + c80_per_octave (early)
//   Stage 2 (FDN late tail)  → time_domain_crest (recirculating chatter)
//   Stage 3 (in-loop LP)     → rt60_per_band + centroid_drift_per_band
//
// Stages 1 and 2 are LTI-isolated: ER writes only to its own delay
// line and sums DIRECTLY to wet output. It never feeds the FDN. This
// guarantees ER-tap energy lifts the early metrics without bleeding
// into the late tail and breaking the C80 energy budget.
//
// The FDN's Hadamard mix is unitary (energy-preserving), giving the
// stable foundation needed to crank per-line feedback into the
// "discrete recirculating slap" regime that the smooth Dattorro loop
// cannot reach.

class LexiconMTDLEngine
{
public:
    LexiconMTDLEngine() = default;

    static constexpr int kNumERTaps   = 4;
    static constexpr int kNumFDNLines = 8;

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);

    // Coarse tuning surface. Will be wired to APVTS later.
    void setDecayTime          (float seconds);
    void setBandDampingHz      (float hz);                 // broadcast: set all 8 lines
    void setBandDampingHzAt    (int lineIdx, float hz);    // per-line override (v35)
    void setFDNFeedbackScale   (float scale);     // 0..1 stability margin
    void setFDNFeedbackAt      (int lineIdx, float scale); // per-line feedback override (v36)
    void setERLevel            (float lin);
    void setERTapGainDbAt      (int tapIdx, float db);     // per-tap ER gain (v36)
    void setLateLevel          (float lin);
    void setERStereoOffsetMs   (float ms);
    // Per-line random-walk LFO mod depth in MILLISECONDS on the FDN read
    // position. v36 lever — APVTS-disabled in v37 (proved toxic), kept
    // accessible via this setter for future research.
    void setLineModDepthMsAt   (int lineIdx, float ms);     // (v36, inert in v37)

    // v37 — Pre-tank Schroeder cascade diffusion coefficient (0..0.95).
    // 0 = bypass. Higher = more transient smear → less spectral crest.
    void setSchroederCoeff     (float coeff);
    // v37 — Post-tank tilt EQ. Range -6..+6 dB centered at 1 kHz pivot.
    // Negative = bass-heavy (low-shelf boost + high-shelf cut), positive
    // = treble-heavy. Targets box_ratio_db directly.
    void setTiltDb             (float db);

private:
    // ===== Stage 1: Feed-Forward ER Matrix =====
    // Lex Med Hall measured specular taps at 0.0 / 4.0 / 7.52 / 9.79 ms.
    // FIR-only: dry input writes to erBuf, 4 fractional reads sum to
    // erOut. Never touches the FDN.
    static constexpr float kERDelayMs[kNumERTaps]  = { 0.0f, 4.0f, 7.52f, 9.79f };
    float              erTapGainLin_[kNumERTaps]   = { 0.1778f, 0.1259f, 0.0891f, 0.0631f };  // -15/-18/-21/-24 dB
    int                erDelaySamp_[kNumERTaps]    = {};
    std::vector<float> erBufL_;
    std::vector<float> erBufR_;
    int                erBufMask_                  = 0;
    int                erWriteIdx_                 = 0;
    float              erStereoOffsetMs_           = 0.50f;
    int                erStereoOffsetSamp_         = 0;
    float              erLevel_                    = 1.0f;

    // ===== Stage 2: FDN Late Tail =====
    // 8 parallel delay lines with mutually-coprime prime lengths so
    // modal eigenfrequencies don't pile up. Outputs go through a
    // unitary 8x8 Hadamard mix, then write back into delay inputs
    // along with the dry mono input.
    static constexpr int kFDNDelayPrimes[kNumFDNLines] = {
        919, 1097, 1289, 1471, 1693, 1879, 2113, 2341
    };
    std::vector<float> fdnBuf_[kNumFDNLines];
    int                fdnBufMask_[kNumFDNLines]   = {};
    int                fdnWriteIdx_[kNumFDNLines]  = {};
    int                fdnDelaySamp_[kNumFDNLines] = {};
    float              fbGainPerLine_[kNumFDNLines] = {};   // pre-computed for T60
    // v36 — per-line multiplicative feedback override (default 1.0 = no
    // change). Decouples decay length from damping so CMA can pair a
    // sharply-damped line with long sustain (or bright with short).
    float              fbOverridePerLine_[kNumFDNLines] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float              feedbackScale_              = 0.98f;
    float              decayTime_                  = 1.5f;
    float              lateLevel_                  = 1.0f;

    // v36 LFO infrastructure retained but APVTS-disabled in v37 — the
    // LFO read modulation proved toxic to spectral metrics (lost 2 PC).
    // depthMs defaults to 0; not exposed externally. Keeping the code
    // path avoids cascading edits but the line is effectively LFO-free.
    static constexpr float kLFORatesHz[kNumFDNLines] = {
        0.31f, 0.43f, 0.59f, 0.71f, 0.89f, 1.03f, 1.27f, 1.51f
    };
    float              lfoState_[kNumFDNLines]         = {};
    float              lfoVelocity_[kNumFDNLines]      = {};
    uint32_t           lfoRng_[kNumFDNLines]           = { 0x12345001u, 0x12345002u, 0x12345003u, 0x12345004u,
                                                            0x12345005u, 0x12345006u, 0x12345007u, 0x12345008u };
    float              lineModDepthMs_[kNumFDNLines]   = {};
    float              lineModDepthSamp_[kNumFDNLines] = {};
    float              lfoWalkStep_[kNumFDNLines]      = {};

    // ===== v37 — Pre-tank Schroeder Allpass Cascade =====
    // 4-stage cascade smears input transients BEFORE the FDN.
    // Multiplies modal density exponentially → directly attacks
    // spectral_crest_db without bloating FDN line count. Uniform
    // tunable coefficient.
    static constexpr int   kNumSchroeder = 4;
    static constexpr float kSchroederMs[kNumSchroeder] = { 4.2f, 7.3f, 12.7f, 19.1f };
    std::vector<float> schroederBuf_[kNumSchroeder];
    int                schroederMask_[kNumSchroeder]  = {};
    int                schroederWrite_[kNumSchroeder] = {};
    int                schroederDelaySamp_[kNumSchroeder] = {};
    float              schroederCoeff_                = 0.0f;   // 0 = bypass (v37 default)

    // ===== v37 — Post-tank Tilt EQ (one axis) =====
    // Low-shelf @ 1 kHz gain = -tilt_db/2, High-shelf @ 1 kHz gain =
    // +tilt_db/2. Center pivot keeps integrated energy stable.
    // Targets box_ratio_db directly without disturbing decay.
    float              tiltDb_         = 0.0f;
    // RBJ biquad coefficients (low-shelf then high-shelf, per channel)
    float              tiltLowB_[3]    = { 1.0f, 0.0f, 0.0f };
    float              tiltLowA_[2]    = { 0.0f, 0.0f };
    float              tiltHighB_[3]   = { 1.0f, 0.0f, 0.0f };
    float              tiltHighA_[2]   = { 0.0f, 0.0f };
    float              tiltLowState_[2][2]  = {};    // [channel][x1,x2,y1,y2 packed]
    float              tiltHighState_[2][2] = {};
    // Use TDF-II Direct Form II Transposed: 2 state vars per filter per channel
    float              tiltLowS1_[2]   = {};
    float              tiltLowS2_[2]   = {};
    float              tiltHighS1_[2]  = {};
    float              tiltHighS2_[2]  = {};

    // ===== Stage 3: In-Loop LP Damping (per-line, v35) =====
    // One-pole low-pass on each line's read before it re-enters the
    // mix. PER-LINE cutoff so CMA can mix sharp + sustained lines —
    // sharp lines (low fc) generate envelope chatter for tdc;
    // sustained lines (high fc) hold rt60. Breaks the v34 tdc/rt60
    // Pareto wall that single-fc damping created.
    float              fdnLineState_[kNumFDNLines] = {};
    float              dampingHz_   [kNumFDNLines] = { 10000.0f, 10000.0f, 10000.0f, 10000.0f,
                                                       10000.0f, 10000.0f, 10000.0f, 10000.0f };
    float              dampingCoeff_[kNumFDNLines] = {};

    // ===== Common =====
    double             sampleRate_                 = 48000.0;
    bool               prepared_                   = false;

    void recomputeDamping();
    void recomputeFDNGains();
    static void hadamardMix8 (float* x);
};
