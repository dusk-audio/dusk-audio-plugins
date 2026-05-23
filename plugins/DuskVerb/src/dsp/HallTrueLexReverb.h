#pragma once

#include "HallReverb.h"

#include <vector>

// =====================================================================
// HallTrueLexReverb — Engine 13
// =====================================================================
//
// Composition of three existing DuskVerb hall blocks targeting Lex
// Med Hall 19/19. None of the inner DSP is new; the win comes from
// the topology:
//
//   in (L,R)
//     │
//     ├──► ER TDL: 4 taps @ {0.0, 4.0, 7.52, 9.79} ms HARDCODED
//     │         ↓ erOut × erLevel  (INDEPENDENT, 0..2)
//     │         │
//     ↓         │
//   HallReverb (Engine 10) 3-band parallel 8-ch FDN tank
//             ↓ tankOut × tankLevel (INDEPENDENT, 0..2)
//             │
//             └─────────────┐
//                           ▼
//                  Sum (erOut + tankOut)
//                           ▼
//                2-stage post-mix Schroeder AP cascade
//                (L + R independent prime-coprime delays;
//                 single shared g coefficient — searched)
//                           ▼
//                       out (L,R)
//
// Why this is the topology that should beat the 12/19 ceiling:
//
//   • ER tap times hardcoded at Lex anchor peaks (Engine 12 Hybrid
//     pattern) — guarantees peak_locations_ms 4/4 PASS by construction.
//
//   • Tank is the proven Engine 10 FDN — preserves c80, d50,
//     decay_envelope_db (12/12), bass/treble ratio, a_weighted,
//     late_tail × 3 PASSes the 12/19 micro-squeeze winner already
//     achieved.
//
//   • Post-mix Schroeder AP cascade smears the FDN's sum-of-N-combs
//     spectral crest WITHOUT recirculating through the tank — so
//     decay/c80/d50 stay intact while box_ratio_db + spectral_crest_db
//     pick up phase smear.
//
//   • Independent erLevel + tankLevel (Engine 12 Fix B pattern) — no
//     zero-sum crossfade. Optimizer can boost ER for early energy
//     WITHOUT dragging down the tank tail.
//
// The embedded HallReverb instance is exposed as the public `tank`
// reference so DuskVerbEngine's existing setHall* forwarders can target
// Engine 13's tank without a duplicated wall of pass-throughs. All
// Engine 10 DSP knobs (EQ, shelves, multiplies, damping, taps,
// specular, inline diffusion) work on Engine 13 unchanged.

class HallTrueLexReverb
{
public:
    HallTrueLexReverb() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inL, const float* inR,
                  float* outL, float* outR, int numSamples);

    // Embedded Engine 10 tank — public so DuskVerbEngine can reuse the
    // existing setHall* forwarders without duplicating ~40 one-liners.
    HallReverb tank;

    // ── Engine 13-specific axes ───────────────────────────────────
    void setERWeight (int idx, float w);
    void setERLevel  (float level);
    void setTankLevel (float level);
    // Single shared Schroeder allpass coefficient across both stages,
    // both channels. Clamped to [0, 0.85] inside the setter.
    void setAPCoeff (float g);

    // Convenience forwarder for shared Freeze button (engine-wide).
    void setFreeze (bool frozen) { tank.setFreeze (frozen); }

private:
    static constexpr int kNumERTaps = 4;
    // Lex Med Hall measured peak positions. HARDCODED — the entire
    // engine's reason for existing is to land peak_locations_ms 4/4
    // PASS by construction. Optuna cannot move these.
    static constexpr float kERTapTimesMs[kNumERTaps] = {
        0.0f, 4.0f, 7.52f, 9.79f
    };
    static constexpr float kERSignL[kNumERTaps] = { +1.0f, +1.0f, -1.0f, +1.0f };
    static constexpr float kERSignR[kNumERTaps] = { +1.0f, -1.0f, +1.0f, -1.0f };

    struct ERTDL
    {
        std::vector<float> bufL, bufR;
        int writePos = 0, mask = 0;
        int tapSamples[kNumERTaps] = { 0, 0, 0, 0 };
        float weights[kNumERTaps]  = { 1.0f, 0.65f, 0.45f, 0.30f };

        void prepare (double sr);
        void clear();
        void process (float inL, float inR, float& outL, float& outR);
    };
    ERTDL erTDL_;
    float erLevel_   = 1.0f;
    float tankLevel_ = 1.0f;

    // Post-mix Schroeder AP cascade. 2 stages per channel, distinct
    // prime delays L/R for stereo decorrelation. Single shared g.
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
    // Primes coprime with HybridHallReverb pre-diff primes + HallSubTank
    // inline AP primes. Delays at 48 kHz: 1567/48000 = 32.6 ms,
    // 2393/48000 = 49.9 ms — long enough to smear modal crest without
    // adding audible repetitions.
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
