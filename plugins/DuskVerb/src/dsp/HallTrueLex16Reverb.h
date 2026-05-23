#pragma once

#include "FDNReverb.h"

#include <vector>

// =====================================================================
// HallTrueLex16Reverb — Engine 14
// =====================================================================
//
// 16-channel composition variant of Engine 13. Same parallel topology
// (ER TDL + tank + post-mix Schroeder AP) but with the 8-ch HallReverb
// tank swapped out for the 16-ch FDNReverb (Engine 4's Hadamard FDN).
//
//   in (L,R)
//     │
//     ├──► ER TDL: 4 taps @ {0.0, 4.0, 7.52, 9.79} ms HARDCODED
//     │         ↓ erOut × erLevel (INDEPENDENT, 0..4)
//     │         │
//     ↓         │
//   FDNReverb (Engine 4, 16-channel Hadamard FDN)
//             ↓ tankOut × tankLevel (INDEPENDENT, 0..4)
//             │
//             └─► 2-stage post-mix Schroeder AP (smears modal crest)
//                           ▼
//                       out (L,R)
//
// Why the 16-channel tank should break the Engine 10/13 ceiling:
//
//   • 2× modal density vs 8-ch → spectral_crest_db drops ~3 dB
//     structurally (the sum-of-N-combs crest scales as 1/sqrt(N)).
//   • Treble band modal sparsity fixed → centroid_drift_per_band
//     bin 3 (the 4 kHz drift Engine 10 cannot close) goes from
//     architectural OUT to within JND.
//   • box_ratio_db narrows by ~2-3 dB as modal density flattens the
//     midrange concentration.
//
// Why removing FDN-tank's legacy specular taps matters:
//
//   • FDNReverb doesn't ship the Hall-style Hall Spec X taps that the
//     8-ch HallReverb used as its early-energy carrier. The 16-ch FDN
//     has its own intrinsic early diffusion via the Hadamard mixing
//     matrix; specular taps would just double up.
//   • All early specular energy at the Lex anchor peak positions
//     [0, 4.0, 7.52, 9.79] ms is delivered EXCLUSIVELY by the parallel
//     ER TDL above — guarantees peak_locations_ms 4/4 PASS.

class HallTrueLex16Reverb
{
public:
    HallTrueLex16Reverb() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);

    // Embedded 16-ch FDNReverb tank — public so DuskVerbEngine's broadcast
    // setters (Decay/Size/Multiplies/Crossovers/Mod/Saturation/Diffusion)
    // can target it without duplicating ~30 forwarder one-liners.
    FDNReverb tank;

    // Engine 14-specific axes (mirror Engine 13's TrueLex axes).
    void setERWeight   (int idx, float w);
    void setERLevel    (float level);
    void setTankLevel  (float level);
    void setAPCoeff    (float g);

    void setFreeze (bool frozen) { tank.setFreeze (frozen); }

private:
    static constexpr int kNumERTaps = 5;
    // Taps 0-3: Lex anchor peak positions (0-10ms) — carries
    // peak_locations_ms PASS by construction.
    // Tap 4: 49.0 ms — "C80 fill" tap. With Pre-Delay 14 ms applied
    // upstream this fires at IR t = 63 ms, perfectly INSIDE the C80
    // window (0-80ms) but OUTSIDE the D50 window (0-50ms). Lets the
    // optimizer raise c80 via tap-4 weight WITHOUT bloating d50 head.
    // Replicates the legacy Hall Spec 3 timing (was 48.88 ms in
    // 8-ch HallReverb, masked by ER on Engine 14 v3-v5 spec-mute).
    static constexpr float kERTapTimesMs[kNumERTaps] = {
        0.0f, 4.0f, 7.52f, 9.79f, 49.0f
    };
    static constexpr float kERSignL[kNumERTaps] = { +1.0f, +1.0f, -1.0f, +1.0f, -1.0f };
    static constexpr float kERSignR[kNumERTaps] = { +1.0f, -1.0f, +1.0f, -1.0f, +1.0f };

    struct ERTDL
    {
        std::vector<float> bufL, bufR;
        int writePos = 0, mask = 0;
        int tapSamples[kNumERTaps] = { 0, 0, 0, 0, 0 };
        float weights[kNumERTaps]  = { 1.0f, 0.65f, 0.45f, 0.30f, 0.0f };

        void prepare (double sr);
        void clear();
        void process (float inL, float inR, float& outL, float& outR);
    };
    ERTDL erTDL_;
    float erLevel_   = 1.0f;
    float tankLevel_ = 1.0f;

    struct APStage
    {
        std::vector<float> buf;
        int writePos = 0, mask = 0, delaySamples = 0;
        float process (float input, float g)
        {
            const int r = (writePos - delaySamples) & mask;
            const float vd = buf[static_cast<size_t> (r)];
            const float vn = input + g * vd;
            buf[static_cast<size_t> (writePos)] = vn;
            writePos = (writePos + 1) & mask;
            return vd - g * vn;
        }
        void clear()
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            writePos = 0;
        }
    };
    static constexpr int kAPStages = 2;
    static constexpr int kAPPrimesL[kAPStages] = { 1567, 2393 };
    static constexpr int kAPPrimesR[kAPStages] = { 1583, 2417 };
    APStage apL_[kAPStages] {};
    APStage apR_[kAPStages] {};
    float apG_ = 0.5f;

    std::vector<float> erOutL_, erOutR_;
    std::vector<float> tankOutL_, tankOutR_;

    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 0;
    bool   prepared_     = false;
};
