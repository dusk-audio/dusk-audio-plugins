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
#include "MultiQMatch.hpp"                         // Match character (spectrum-match EQ + FIR conv)
#include "MultiQTube.hpp"                         // Tube character (framework-free TubeEQ port)
#include "MultiQLimiter.hpp"                      // output brickwall limiter (framework-free)
#include "../../shared-dpf/dsp/FourKEQDSP.hpp"   // British character = upgraded 4K EQ core
#include "../../shared-dpf/dsp/DuskOversampler.hpp" // 2x/4x OS for Digital per-band saturation
#include "../../shared/AnalogEmulation/WaveshaperCurves.h"

#include <array>
#include <atomic>

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

        // Master-bus utilities (applied at the very end of process(), all
        // characters). All default OFF so a default Params is a no-op vs the
        // pre-feature build — preserving the validated Digital bit-exact A/B.
        bool  autoGainEnabled  = false;   // 2 s RMS loudness match, +/-6 dB
        bool  limiterEnabled   = false;   // ~1 ms-lookahead brickwall
        float limiterCeiling   = 0.0f;    // dBFS

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

        // Tube character (routed through the framework-free MultiQTube port).
        // The 6 stepped frequencies are stored as RESOLVED Hz — the shell/harness
        // applies the choice-index → Hz LUTs (MultiQ.cpp:784-842) before filling
        // this, matching what the JUCE MultiQ shell hands to TubeEQProcessor.
        struct Tube {
            float lfBoostGain = 0.f;   float lfBoostFreq = 60.f;    // 20/30/60/100 Hz
            float lfAttenGain = 0.f;
            float hfBoostGain = 0.f;   float hfBoostFreq = 8000.f;  // 3k..16k Hz
            float hfBoostBandwidth = 0.5f;
            float hfAttenGain = 0.f;   float hfAttenFreq = 10000.f; // 5k/10k/20k Hz
            bool  midEnabled = true;
            float midLowFreq = 500.f;  float midLowPeak = 0.f;      // 0.2..1.0 kHz
            float midDipFreq = 700.f;  float midDip = 0.f;          // 0.2..2.0 kHz
            float midHighFreq = 3000.f; float midHighPeak = 0.f;    // 1.5..5.0 kHz
            float inputGain = 0.f, outputGain = 0.f; // dB
            float tubeDrive = 0.3f;    // 0-1
        } tube;
    };

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void process(const float* const* inputs, float* const* outputs,
                 int numChannels, int numSamples, const Params& p);

    // Reported latency in base-rate samples. Digital/Tube run at base rate (0);
    // British routes through FourKEQDSP, which reports oversampler latency when
    // oversampling > 0. Reads the last-processed character (cached in process()),
    // so call it after process() when the host asks for an updated latency.
    int getLatencySamples() const noexcept
    {
        int lat = limiter.getLatencySamples();   // lookahead, all characters (0 if off)
        if (lastEqType == (int)EQType::British)   lat += britishEQ.getLatencySamples();
        else if (lastEqType == (int)EQType::Tube) lat += tubeEQ.getLatencySamples();
        else if (lastEqType == (int)EQType::Match) lat += matchProc.getLatencySamples();
        else                                      lat += lastDigitalSatLatency;
        return lat;
    }

    // Current limiter gain reduction (dB, >=0) for the UI meter. Read-only tap.
    float getLimiterGainReduction() const noexcept { return limiter.getGainReduction(); }

    //--- metering + analyzer taps (read-only observers; any thread) -----------
    // Read-only observers written in process() after the character branch; they
    // never touch the processed audio (bit-identity preserved). Linear peak with
    // ~300 ms release, relaxed atomics; pre-processing input and post-processing
    // output, per channel (L/R).
    float getInputPeakL()  const noexcept { return inPeakL.load(std::memory_order_relaxed); }
    float getInputPeakR()  const noexcept { return inPeakR.load(std::memory_order_relaxed); }
    float getOutputPeakL() const noexcept { return outPeakL.load(std::memory_order_relaxed); }
    float getOutputPeakR() const noexcept { return outPeakR.load(std::memory_order_relaxed); }
    // Lock-free ring of recent post-processing output (mono downmix) for the UI
    // spectrum FFT. The audio thread push()es; the UI snapshot()s (see the
    // SpectrumRing snapshot protocol / FourKEQ analyzer).
    const SpectrumRing& outputSpectrum() const noexcept { return analyzerRing; }
    // Companion PRE-EQ ring: recent RAW input (mono downmix) captured at the top of
    // process(), before any character branch. Read-only/additive — filled from the
    // input alongside the input-peak tap, never touching the processed audio, so
    // Digital bit-identity is preserved. The UI picks pre vs post per its param.
    const SpectrumRing& inputSpectrum() const noexcept { return inputAnalyzerRing; }

    // Live per-band dynamic-EQ gain (dB) for the UI's animated response overlay.
    // Read-only tap of the dynamics meter the DSP already maintains; never touches
    // processed audio (bit-identity preserved).
    float getBandDynamicGain(int band) const noexcept { return dynamicEQ.getCurrentDynamicGain(band); }

    // Match spectrum-EQ processor: the shell's UI write-bridge (learn/compute/
    // clear) and the read-bridge (spectra/curves/state) go through this. The audio
    // thread's learn-feed + FIR convolution happen inside MultiQDSP::process().
    MultiQMatch&       matchProcessor()       noexcept { return matchProc; }
    const MultiQMatch& matchProcessor() const noexcept { return matchProc; }

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

    // read-only observer taps (RT-safe, no alloc/lock): capture the pre-process
    // input peak, and publish the post-process output peak + push the analyzer
    // ring. Called once per block (input at the top, output before each return).
    void captureInputPeak(const float* const* inputs, int numChannels, int numSamples) noexcept;
    void publishOutputTaps(const float* L, const float* R, bool isStereo, int numSamples) noexcept;

    // Auto-gain: accumulate the raw-input loudness window (top of block) and apply
    // the smoothed make-up gain to the post-master output (MultiQ.cpp:1523-1580).
    void accumulateInputRms(const float* const* inputs, int numChannels, int numSamples) noexcept;
    void applyAutoGain(float* L, float* R, bool isStereo, int numSamples) noexcept;

    double currentSampleRate = 48000.0;
    float  biquadSmoothCoeff = 1.0f; // 1 - exp(-1/(0.001*sr)); per-sample coeff ramp
    bool   firstBlock = true;
    int    prevHpfStages = -1, prevLpfStages = -1;
    int    lastEqType = (int)EQType::Digital; // cached in process() for getLatencySamples()

    CascadedFilter hpfFilter, lpfFilter;
    std::array<StereoBiquad, 6> svfFilters;       // bands 2-7 (DF2T, AnalogMatchedBiquad coeffs)
    std::array<StereoSVF, 6>    svfDynGainFilters; // bands 2-7 dynamic-gain (Cytomic SVF)
    MultiQDynamics dynamicEQ;
    FourKEQDSP britishEQ;              // British character (upgraded 4K parallel-summing core)
    MultiQMatch matchProc;            // Match character (learn + correction-FIR convolution)
    MultiQTube tubeEQ;                 // Tube character (framework-free TubeEQ port)
    MultiQLimiter limiter;            // master-bus brickwall (all characters)

    // Digital per-band saturation oversampling: one L/R polyphase oversampler per
    // middle band (2-7). Engaged only when hq_enabled>0 AND the band saturates; at
    // 1x each is a transparent passthrough (Digital A/B stays bit-identical).
    std::array<Oversampler, 6> satOsL, satOsR;
    int prevSatOsFactor = 1;          // reset OS state when the hq factor changes
    int lastDigitalSatLatency = 0;    // summed active-sat-band OS latency (base samples)

    //--- auto-gain (2 s RMS loudness match, +/-6 dB — MultiQ.cpp:1523-1580) ------
    LinearSmoothedValue autoGainComp; // linear make-up gain, 2 s ramp
    double inputRmsSum = 0.0, outputRmsSum = 0.0;
    float  inputPeakMax = 0.0f, outputPeakMax = 0.0f;
    int    rmsSampleCount = 0;
    int    rmsWindowSamples = 96000;  // 2 s (set from sample rate in prepare)
    std::array<LinearSmoothedValue, NUM_BANDS> bandEnableSmoothed;
    std::array<float, NUM_BANDS> prevBandPhaseInvertGain { {1,1,1,1,1,1,1,1} };
    std::array<float, NUM_BANDS> prevBandPanVal {};

    //--- metering + analyzer taps (written in process(); read from any thread) -
    SpectrumRing analyzerRing;                            // recent output (mono), UI FFT
    SpectrumRing inputAnalyzerRing;                       // recent raw input (mono), UI pre-EQ FFT
    std::atomic<float> inPeakL{0.f}, inPeakR{0.f}, outPeakL{0.f}, outPeakR{0.f};
    float meterDecay = 1.0f;                              // per-sample peak-hold release
};

} // namespace duskaudio
