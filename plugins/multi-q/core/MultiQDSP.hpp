// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQDSP.hpp — framework-free Multi-Q DSP core (zero JUCE/DPF).
//
// Phase 2b scope: the DIGITAL character path — 8-band EQ (variable-slope HPF,
// low/high shelves, 4 parametric bands, variable-slope LPF), per-band shape,
// Q-coupling, invert, phase-invert, pan, per-band channel routing (Stereo/L/R/
// Mid/Side), per-band saturation, enable crossfades, delta-solo, and Match-mode
// band-bypass. Ported line-for-line from MultiQ::processBlock's Digital branch.
//
// NOT yet ported (later sub-phases): per-band dynamic EQ (DynamicEQProcessor),
// British/Tube characters, output limiter, auto-gain, oversampling, analyzer,
// Match FIR correction. Those are stubbed / passthrough here.

#pragma once

#include "MultiQFilters.hpp"
#include "MultiQParams.hpp"
#include "MultiQDynamics.hpp"
#include "../../shared-dpf/dsp/FourKEQDSP.hpp"   // British character = upgraded 4K EQ core
#include "../../shared/AnalogEmulation/WaveshaperCurves.h"

#include <array>

namespace duskaudio
{

class MultiQDSP
{
public:
    static constexpr int NUM_BANDS = kMultiQNumBands;

    // Per-block parameter snapshot. The host shell fills this from its atomic
    // value cache once per block (mirrors MultiQ's safeGetParam reads).
    struct Params
    {
        // per band [8] (index 0=HPF .. 7=LPF)
        std::array<bool,  NUM_BANDS> bandEnabled { {true,true,true,true,true,true,true,true} };
        std::array<float, NUM_BANDS> bandFreq    { {20,100,200,500,1000,2000,4000,20000} };
        std::array<float, NUM_BANDS> bandGain     {}; // bands 2-7 meaningful
        std::array<float, NUM_BANDS> bandQ        { {0.71f,0.71f,0.71f,0.71f,0.71f,0.71f,0.71f,0.71f} };
        std::array<int,   NUM_BANDS> bandShape    {}; // bands 2-7
        std::array<int,   NUM_BANDS> bandRouting  {}; // raw choice 0=Global,1=Stereo,2=Left,3=Right,4=Mid,5=Side
        std::array<bool,  NUM_BANDS> bandInvert   {};
        std::array<bool,  NUM_BANDS> bandPhaseInvert {};
        std::array<float, NUM_BANDS> bandPan      {};
        std::array<int,   NUM_BANDS> bandSatType  {}; // 0=Off,1=Tape,2=Tube,3=Console,4=FET
        std::array<float, NUM_BANDS> bandSatDrive { {0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f,0.3f} };
        std::array<int,   NUM_BANDS> bandSlope    { {1,0,0,0,0,0,0,1} }; // only [0]/[7] used

        // dynamics per band [8] — accepted but not yet processed (Phase 2b-2)
        std::array<bool,  NUM_BANDS> bandDynEnabled {};
        std::array<float, NUM_BANDS> bandDynThreshold { {-20,-20,-20,-20,-20,-20,-20,-20} };
        std::array<float, NUM_BANDS> bandDynAttack  { {10,10,10,10,10,10,10,10} };
        std::array<float, NUM_BANDS> bandDynRelease { {100,100,100,100,100,100,100,100} };
        std::array<float, NUM_BANDS> bandDynRange   { {12,12,12,12,12,12,12,12} };
        std::array<float, NUM_BANDS> bandDynRatio   { {4,4,4,4,4,4,4,4} };

        float masterGain    = 0.0f;  // dB
        int   processingMode = 0;    // 0=Stereo,1=L,2=R,3=Mid,4=Side
        int   qCoupleMode   = 0;     // 0..8
        int   eqType        = 0;     // 0=Digital,1=Match,2=British,3=Tube
        int   oversampling  = 0;     // 0=1x,1=2x,2=4x (British/Tube nonlinearity only)
        int   soloBand      = -1;    // -1 = none
        bool  deltaSolo     = false;

        // British character (routed through the upgraded FourKEQDSP core).
        struct British {
            float hpfFreq = 20.f;   bool hpfEnabled = false;
            float lpfFreq = 20000.f; bool lpfEnabled = false;
            float lfGain = 0.f; float lfFreq = 100.f;  bool lfBell = false;
            float lmGain = 0.f; float lmFreq = 600.f;  float lmQ = 0.7f;
            float hmGain = 0.f; float hmFreq = 2000.f; float hmQ = 0.7f;
            float hfGain = 0.f; float hfFreq = 8000.f; bool hfBell = false;
            bool  blackMode = false;   // false=Brown(E), true=Black(G)
            float saturation = 0.f;    // 0-100 %
            float inputGain = 0.f, outputGain = 0.f; // dB
        } british;
    };

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void process(const float* const* inputs, float* const* outputs,
                 int numChannels, int numSamples, const Params& p);

private:
    // Framework-free cascaded biquad (variable-slope HPF/LPF). Direct-form II
    // transposed, coefficients set directly (no per-sample smoothing), NaN-guard
    // resets the offending stage. Mirrors MultiQ::CascadedFilter.
    struct CascadedFilter
    {
        static constexpr int MAX_STAGES = 8;
        std::array<MqBiquadCoeffs, MAX_STAGES> co{};
        std::array<float, MAX_STAGES> s1L{}, s2L{}, s1R{}, s2R{};
        int activeStages = 1;

        void reset()
        {
            s1L.fill(0.f); s2L.fill(0.f); s1R.fill(0.f); s2R.fill(0.f);
        }
        void stepSmoothing() {}
        float processSampleL(float x)
        {
            for (int i = 0; i < activeStages; ++i)
            {
                float prev = x;
                const float* c = co[(size_t)i].coeffs;
                float y = c[0] * x + s1L[(size_t)i];
                s1L[(size_t)i] = c[1] * x - c[4] * y + s2L[(size_t)i];
                s2L[(size_t)i] = c[2] * x - c[5] * y;
                x = y;
                if (!safeIsFinite(x)) { s1L[(size_t)i] = s2L[(size_t)i] = 0.f; x = prev; }
            }
            return x;
        }
        float processSampleR(float x)
        {
            for (int i = 0; i < activeStages; ++i)
            {
                float prev = x;
                const float* c = co[(size_t)i].coeffs;
                float y = c[0] * x + s1R[(size_t)i];
                s1R[(size_t)i] = c[1] * x - c[4] * y + s2R[(size_t)i];
                s2R[(size_t)i] = c[2] * x - c[5] * y;
                x = y;
                if (!safeIsFinite(x)) { s1R[(size_t)i] = s2R[(size_t)i] = 0.f; x = prev; }
            }
            return x;
        }
    };

    // coefficient builders (ported from MultiQ.cpp)
    void computeBandCoeffs(int band, const Params& p, MqBiquadCoeffs& c) const;
    void updateHPF(const Params& p);
    void updateLPF(const Params& p);
    static void computeTiltShelf(MqBiquadCoeffs& c, double sr, double freq, float gainDB);
    // set the dyn-gain SVF target for a band from the given dynamic gain (dB)
    void updateDynGainFilter(int band, float dynGainDb, const Params& p);

    double currentSampleRate = 48000.0;
    float  biquadSmoothCoeff = 1.0f; // 1 - exp(-1/(0.001*sr)); per-sample coeff ramp
    bool   firstBlock = true;
    int    prevHpfStages = -1, prevLpfStages = -1;

    CascadedFilter hpfFilter, lpfFilter;
    std::array<StereoBiquad, 6> svfFilters;       // bands 2-7 (DF2T, AnalogMatchedBiquad coeffs)
    std::array<StereoSVF, 6>    svfDynGainFilters; // bands 2-7 dynamic-gain (Cytomic SVF)
    MultiQDynamics dynamicEQ;
    FourKEQDSP britishEQ;              // British character (upgraded 4K parallel-summing core)
    std::array<LinearSmoothedValue, NUM_BANDS> bandEnableSmoothed;
    std::array<float, NUM_BANDS> prevBandPhaseInvertGain { {1,1,1,1,1,1,1,1} };
    std::array<float, NUM_BANDS> prevBandPanVal {};
};

} // namespace duskaudio
