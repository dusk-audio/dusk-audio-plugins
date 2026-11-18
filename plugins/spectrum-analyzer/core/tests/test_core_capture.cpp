// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// Core capture test (GH #184, migration plan step 1). Freezes how the existing
// framework-free core behaves through prepare, reset and re-prepare, what its
// meters read and what its spectrum draws at fixed sample positions at 44.1,
// 48 and 96 kHz, before step 2 moves analysis off the audio callback.
//
// LegacyChain is a hand transcription of SpectrumAnalyzerProcessor's DSP
// orchestration (Source/PluginProcessor.cpp): prepareToPlay, releaseResources,
// the analysis feed of processBlock (channel routing feeds only the FFT; every
// meter reads the original input; the block runs under a flush-to-zero guard)
// and parameterChanged's mapping of the eight analysis controls. So this test
// guards the core's numerics and reset semantics under a model faithful to
// today's adapter, not the adapter itself. A tripwire hashes the four adapter
// bodies and fails with "the adapter changed" when any of them is edited. The
// adapter's output peak/RMS meter is adapter code and is not captured here.
// The editor's 30 Hz timer is one processFFT() per tick, tick k due once k/30 s
// of audio has been fed; the editor consumes each frame on the same tick.
//
// Three kinds of assertion:
//  * exact - lifecycle facts (floors after prepare and release, a computed but
//    unconsumed frame dropped by release, FIFO cleared, FFT frame thresholds,
//    the peak-hold default, control-to-component mapping, the scope of the
//    adapter's reset calls) and in-process equivalences (a chain prepared
//    again, from another rate or with other controls, is bit-identical to a
//    fresh one in every meter reading and every spectrum and peak-hold bin at
//    every mark; routing and spectrum settings change no meter). Same binary,
//    same input: these hold in any FP contraction mode.
//  * margin - the gated-history cap, the three loudness gates (runs built
//    0.3 dB either side of each decision) and the display laws (within
//    kDisplayBound or 1e-4).
//  * frozen - meter readings at every mark and a spectrum digest (display and
//    peak-hold dB at 16 bins at 4 marks, for two control sets) against
//    SpectrumCaptureFixtures.inc, each quantity within its own bound below;
//    flags, frame counts, positions, digest bins and the programme hash are
//    exact.
//
// Usage: SpectrumCoreCaptureTest <Source/PluginProcessor.cpp>          run every check
//        SpectrumCoreCaptureTest <Source/PluginProcessor.cpp> --emit   print a fresh
//            SpectrumCaptureFixtures.inc (only after every other check passes)

#include "ChannelRouter.h"
#include "CorrelationMeter.h"
#include "FFTProcessor.h"
#include "KSystemMeter.h"
#include "LUFSMeter.h"
#include "SpectrumAnalyzerParams.hpp"
#include "TruePeakDetector.h"

#include "CaptureStimulus.hpp"
#include "DuskDenormals.hpp"
#include "SourceText.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace duskaudio::spectrum;
using spectrum_capture::StereoProgramme;

namespace
{
//==============================================================================
// Bounds for frozen values, one per quantity. Each is the quantity's real,
// build-dependent resolution: how far a float32 build of the shipping code sits
// from the same computation done in double with the code's own float
// coefficients (K-System and correlation decay factors, K-weighting biquads,
// true-peak taps, Hann window, FFT twiddles, smoothing factors), so the
// coefficient rounding every IEEE build shares is excluded. Measured on this
// programme at 44.1, 48 and 96 kHz in -ffp-contract=off, on and fast builds,
// plus explicit fused and unfused variants of the K-System and correlation
// accumulators, worst over every 10 ms block end or spectrum frame unless
// noted. A frozen and a live build can err in opposite directions, so each
// bound exceeds twice its worst error. A consistent recalibration smaller than
// a quantity's bound passes; a larger one fails. Every bound is about a tenth
// of the readout step (0.1 dB; 0.01 for correlation) or less.
//   quantity            worst error            bound
//   momentary           8.2e-4 dB (1)          0.002 dB
//   short-term          2.6e-3 dB (2)          0.006 dB
//   integrated          2.6e-4 dB              0.0006 dB
//   loudness range      2.6e-4 LU              0.0006 LU
//   K-System level      5.2e-3 dB (96 kHz)     0.011 dB
//   K-System peak hold  5.2e-3 dB (96 kHz)     0.011 dB
//   true peak           3.3e-6 dB              1e-5 dB (3)
//   correlation         5.3e-4                 0.0011 (smoothed and raw)
//   spectrum display    6.5e-4 dB (4)          0.0014 dB
//   spectrum peak hold  5.6e-4 dB (4)          0.0012 dB
// (1) Every block end but one: at 9.40 s the 400 ms window [9.0, 9.4] holds
//     exactly the K-weighting filter's ring-down after the 9 s stop, then
//     silence, which float32 resolves to 0.0069, 0.0079 and 0.021 dB at 44.1,
//     48 and 96 kHz. No mark samples it.
// (2) At the marks. Anywhere it reaches 0.017 dB at 96 kHz, 10.18 s: a 3 s
//     window whose float sum takes in a +2 LUFS onset after quieter content.
// (3) 1.5 x twice the worst error (2 x 3.3e-6 = 6.6e-6).
// (4) At the 16 digest bins, every frame.
// Quiet material right after loud material is resolved far worse: 0.16 dB on a
// -65 LUFS block that directly follows -14 LUFS noise at 96 kHz, which is why
// the programme puts its quiet segments first.
constexpr double kMomentaryBound      = 0.002;
constexpr double kShortTermBound      = 0.006;
constexpr double kIntegratedBound     = 0.0006;
constexpr double kLoudnessRangeBound  = 0.0006;
constexpr double kKLevelBound         = 0.011;
constexpr double kKPeakHoldBound      = 0.011;
constexpr double kTruePeakBound       = 0.00001;
constexpr double kCorrelationBound    = 0.0011;
constexpr double kCorrelationRawBound = 0.0011;
constexpr double kDisplayBound        = 0.0014;
constexpr double kSpectrumPeakBound   = 0.0012;

constexpr int kSampleRates[] = { 44100, 48000, 96000 };
constexpr int kDisplayBins = FFTProcessor::DISPLAY_BINS;
using Trace = std::array<float, FFTProcessor::DISPLAY_BINS>;

//==============================================================================
struct Reading
{
    const char* label;
    long long position;                                         // samples since prepare
    float momentary, shortTerm, integrated, loudnessRange;      // LUFS, LUFS, LUFS, LU
    float kLevelL, kLevelR, kLevelMono, kPeakHoldL, kPeakHoldR; // dB on the K-14 scale
    float truePeakL, truePeakR;                                 // dBTP
    bool clipped;
    float correlation, correlationRaw;                          // displayed (smoothed), raw
    int fftFrames;                                              // spectrum frames since prepare/release
};

enum class Action { Read, AdapterResets, Release };

struct Mark
{
    const char* label;
    int block;          // 10 ms blocks fed before this mark
    Action action;
};

// Fixed positions, each a whole number of 10 ms blocks. The adapter's reset
// calls (integrated loudness, true-peak hold) land mid loudness-block.
constexpr Mark kMarks[] = {
    { "prepared",          0, Action::Read },
    { "t=0.01s",           1, Action::Read },
    { "t=0.40s",          40, Action::Read },
    { "t=1.00s",         100, Action::Read },   // only -65 LUFS blocks stored
    { "t=2.00s",         200, Action::Read },   // -75 LUFS blocks were not stored
    { "t=4.00s",         400, Action::Read },
    { "t=6.50s",         650, Action::Read },
    { "t=9.50s",         950, Action::Read },
    { "t=11.00s",       1100, Action::Read },
    { "t=13.05s",       1305, Action::Read },
    { "adapter resets", 1305, Action::AdapterResets },
    { "t=14.00s",       1400, Action::Read },   // ten noise blocks since the resets
    { "released",       1400, Action::Release },
};
constexpr std::size_t kMarkCount = sizeof(kMarks) / sizeof(kMarks[0]);
constexpr std::size_t kMarkBeforeResets = 9, kMarkResets = 10, kMarkLastRead = 11;
static_assert(kMarks[kMarkResets].action == Action::AdapterResets
              && kMarks[kMarkBeforeResets].block == kMarks[kMarkResets].block);
static_assert(kMarks[kMarkCount - 1].action == Action::Release
              && kMarks[kMarkLastRead].block == kMarks[kMarkCount - 1].block);
static_assert(kMarks[kMarkCount - 1].block == spectrum_capture::kProgrammeSeconds * 100);

// Spectrum digest: a quiet tone rising, a step down, decay into silence, noise.
// Robust to input dither of 2e-6, which moves these values about 100 times
// more than the build differences do: no peak-hold decision flips here.
constexpr std::size_t kDigestMarks[] = { 3, 4, 7, 9 };
constexpr std::size_t kDigestMarkCount = sizeof(kDigestMarks) / sizeof(kDigestMarks[0]);
constexpr int kDigestBins = 16;

// Control sets: A is the descriptor's defaults; B moves every spectrum control
// (peak hold On, Mid routing, 8192 points); C only dirties a chain before it is
// prepared again, and differs from both in every analysis control.
enum ControlSet { kSetA, kSetB, kSetCount };
constexpr const char* kSetNames[] = { "A", "B" };

std::array<float, PARAM_COUNT> defaultControls()
{
    std::array<float, PARAM_COUNT> values {};
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
        values[i] = parameterSpecs[i].defaultValue;
    return values;
}

std::array<float, PARAM_COUNT> controlsFor(ControlSet set)
{
    auto c = defaultControls();
    if (set == kSetB)
    {
        c[PARAM_CHANNEL_MODE] = 2.0f;      // Mid
        c[PARAM_FFT_RESOLUTION] = 2.0f;    // 8192
        c[PARAM_SMOOTHING] = 0.9f;
        c[PARAM_SLOPE] = 3.0f;
        c[PARAM_DECAY_RATE] = 6.0f;
        c[PARAM_PEAK_HOLD] = 1.0f;
        c[PARAM_PEAK_HOLD_TIME] = 0.5f;
    }
    return c;
}

std::array<float, PARAM_COUNT> dirtyingControls()
{
    std::array<float, PARAM_COUNT> c {};
    c[PARAM_CHANNEL_MODE] = 3.0f;      // Side
    c[PARAM_FFT_RESOLUTION] = 0.0f;    // 2048
    c[PARAM_SMOOTHING] = 0.3f;
    c[PARAM_SLOPE] = -2.0f;
    c[PARAM_DECAY_RATE] = 40.0f;
    c[PARAM_PEAK_HOLD] = 1.0f;
    c[PARAM_PEAK_HOLD_TIME] = 3.0f;
    c[PARAM_DISPLAY_MIN] = -90.0f;
    c[PARAM_DISPLAY_MAX] = 12.0f;
    c[PARAM_K_SYSTEM_TYPE] = 2.0f;     // K-20
    return c;
}

int fftSizeFor(const std::array<float, PARAM_COUNT>& controls)
{
    return 2048 << static_cast<int>(controls[PARAM_FFT_RESOLUTION]);
}

//==============================================================================
struct SpectrumDigest
{
    int fftFrames;
    float display[kDigestBins];
    float peakHold[kDigestBins];
};

struct ControlSetDigest
{
    SpectrumDigest at[kDigestMarkCount];
};

struct RateCapture
{
    int sampleRate;
    int blockSize;
    unsigned long long programmeHash;
    int digestBins[kDigestBins];
    Reading readings[kMarkCount];
    ControlSetDigest spectrum[kSetCount];
};

constexpr RateCapture kFrozen[] = {
#include "SpectrumCaptureFixtures.inc"
};

//==============================================================================
// The four adapter bodies LegacyChain transcribes, hashed after comments and
// whitespace are normalised away.
struct AdapterFunction
{
    const char* signature;
    std::uint64_t hash;
};

constexpr AdapterFunction kAdapterFunctions[] = {
    { "SpectrumAnalyzerProcessor::prepareToPlay(",    0xe254e1c02d2009ddull },
    { "SpectrumAnalyzerProcessor::releaseResources(", 0x54dc21bcf7bbe3daull },
    { "SpectrumAnalyzerProcessor::processBlock(",     0x2e94f76a8dd6040full },
    { "SpectrumAnalyzerProcessor::parameterChanged(", 0xc622e054c12e4813ull },
};

//==============================================================================
int failures = 0;
bool emitting = false;   // --emit: progress goes to stderr, stdout is the fixture

void check(bool ok, const std::string& what)
{
    std::fprintf(emitting ? stderr : stdout, "%s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok)
        ++failures;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// Float to double is exact, and so is the difference of two nearby floats.
double distance(float a, float b) { return std::fabs(static_cast<double>(a) - static_cast<double>(b)); }

bool allAt(const Trace& values, float level)
{
    return std::all_of(values.begin(), values.end(), [level](float v) { return sameBits(v, level); });
}

//==============================================================================
class LegacyChain
{
public:
    // prepareToPlay(sampleRate, samplesPerBlock)
    void prepare(int sampleRate, int maxBlock, const std::array<float, PARAM_COUNT>& controls)
    {
        rate = sampleRate;
        fft.prepare(sampleRate, maxBlock);
        lufs.prepare(sampleRate, 2);
        ksys.prepare(sampleRate);
        tp.prepare(sampleRate, 2);
        corr.prepare(sampleRate);
        routedL.resize(static_cast<std::size_t>(maxBlock));
        routedR.resize(static_cast<std::size_t>(maxBlock));
        // The adapter re-applies its listened controls here, in this order.
        // displayMin/displayMax have no listener: they only reach the editor.
        for (unsigned id : { PARAM_CHANNEL_MODE, PARAM_FFT_RESOLUTION, PARAM_SMOOTHING, PARAM_SLOPE,
                             PARAM_DECAY_RATE, PARAM_PEAK_HOLD, PARAM_PEAK_HOLD_TIME, PARAM_K_SYSTEM_TYPE })
            applyControl(id, controls[id]);
        fed = 0;
        ticks = 0;
        fftFrames = 0;
    }

    // releaseResources()
    void release()
    {
        fft.reset();
        lufs.reset();
        ksys.reset();
        tp.reset();
        corr.reset();
        fed = 0;
        ticks = 0;
        fftFrames = 0;
    }

    // parameterChanged(parameterID, newValue)
    void applyControl(unsigned id, float value)
    {
        switch (id)
        {
            case PARAM_CHANNEL_MODE:
                router.setMode(static_cast<ChannelRouter::Mode>(static_cast<int>(value)));
                break;
            case PARAM_FFT_RESOLUTION:
                switch (static_cast<int>(value))
                {
                    case 0:  fft.setResolution(FFTProcessor::Resolution::Low); fftSize = 2048; break;
                    case 1:  fft.setResolution(FFTProcessor::Resolution::Medium); fftSize = 4096; break;
                    case 2:  fft.setResolution(FFTProcessor::Resolution::High); fftSize = 8192; break;
                    default: fft.setResolution(FFTProcessor::Resolution::Medium); fftSize = 4096; break;
                }
                break;
            case PARAM_SMOOTHING:      fft.setSmoothing(value); break;
            case PARAM_SLOPE:          fft.setSlope(value); break;
            case PARAM_DECAY_RATE:     fft.setDecayRate(value); break;
            case PARAM_PEAK_HOLD:      fft.setPeakHoldEnabled(value > 0.5f); break;
            case PARAM_PEAK_HOLD_TIME: fft.setPeakHoldTime(value); break;
            case PARAM_K_SYSTEM_TYPE:
                ksys.setType(static_cast<KSystemMeter::Type>(static_cast<int>(value)));
                break;
            default:
                break;   // displayMin, displayMax: editor only
        }
    }

    // processBlock's analysis feed, then any editor ticks now due.
    void processBlock(const float* left, const float* right, int numSamples)
    {
        {
            const duskaudio::ScopedFlushDenormals noDenormals;
            router.process(left, right, routedL.data(), routedR.data(), numSamples);
            fft.pushSamples(routedL.data(), routedR.data(), numSamples);
            lufs.process(left, right, numSamples);
            ksys.process(left, right, numSamples);
            const float* channels[2] = { left, right };
            tp.process(channels, numSamples);
            corr.process(left, right, numSamples);
        }
        fed += numSamples;
        while ((ticks + 1) * rate <= fed * 30)
        {
            editorTick();
            ++ticks;
        }
    }

    // The editor timer: one processFFT(), and a frame is consumed when ready.
    void editorTick()
    {
        fft.processFFT();
        if (fft.isDataReady())
        {
            ++fftFrames;
            fft.clearDataReady();
        }
    }

    // A frame computed but not yet taken by the editor: the FIFO is topped up
    // to a full frame and processFFT() runs, as a timer tick would, without the
    // consume that follows it.
    void computePendingFrame()
    {
        const std::vector<float> silence(static_cast<std::size_t>(fftSize), 0.0f);
        fft.pushSamples(silence.data(), silence.data(), fftSize);
        fft.processFFT();
    }

    // resetIntegratedLoudness() and resetPeakHold(): the adapter's two reset
    // calls. Nothing in Source/ calls them today; the plan's step-4 reset
    // actions would be new UI.
    void adapterResetCalls()
    {
        lufs.resetIntegrated();
        tp.resetPeakHold();
    }

    Reading read(const char* label, long long position) const
    {
        return { label, position,
                 lufs.getMomentaryLUFS(), lufs.getShortTermLUFS(), lufs.getIntegratedLUFS(),
                 lufs.getLoudnessRange(),
                 ksys.getKLevelL(), ksys.getKLevelR(), ksys.getKLevelMono(),
                 ksys.getPeakHoldL(), ksys.getPeakHoldR(),
                 tp.getTruePeakDB(0), tp.getTruePeakDB(1), tp.hasClipped(),
                 corr.getSmoothedCorrelation(), corr.getCorrelation(),
                 fftFrames };
    }

    int currentFftSize() const { return fftSize; }

    FFTProcessor fft;
    LUFSMeter lufs;
    KSystemMeter ksys;
    TruePeakDetector tp;
    CorrelationMeter corr;
    ChannelRouter router;

private:
    std::vector<float> routedL, routedR;
    long long rate = 44100, fed = 0, ticks = 0;
    int fftFrames = 0;
    int fftSize = 4096;
};

//==============================================================================
struct ScheduleRun
{
    std::array<Reading, kMarkCount> readings {};
    std::vector<Trace> display = std::vector<Trace>(kMarkCount);    // after each mark's action
    std::vector<Trace> peakHold = std::vector<Trace>(kMarkCount);
    bool framePendingAtRelease = false;
    bool peakTraceAboveFloorAtRelease = false;
};

// Feeds the programme in 10 ms blocks from a freshly prepared chain, reading
// (and resetting or releasing) at every mark.
ScheduleRun runSchedule(LegacyChain& chain, const StereoProgramme& programme, int sampleRate)
{
    const int block = sampleRate / 100;
    ScheduleRun run;
    int fedBlocks = 0;
    for (std::size_t m = 0; m < kMarkCount; ++m)
    {
        const Mark& mark = kMarks[m];
        for (; fedBlocks < mark.block; ++fedBlocks)
        {
            const std::size_t at = static_cast<std::size_t>(fedBlocks) * static_cast<std::size_t>(block);
            chain.processBlock(programme.left.data() + at, programme.right.data() + at, block);
        }
        if (mark.action == Action::AdapterResets)
            chain.adapterResetCalls();
        if (mark.action == Action::Release)
        {
            chain.computePendingFrame();
            run.framePendingAtRelease = chain.fft.isDataReady();
            const auto& peak = chain.fft.getPeakHold();
            run.peakTraceAboveFloorAtRelease
                = std::any_of(peak.begin(), peak.end(), [](float v) { return v > -100.0f; });
            chain.release();
        }
        run.readings[m] = chain.read(mark.label, static_cast<long long>(mark.block) * block);
        run.display[m] = chain.fft.getMagnitudes();
        run.peakHold[m] = chain.fft.getPeakHold();
    }
    return run;
}

// Dirties every piece of chain state with `blocks` blocks of `programme`.
void dirty(LegacyChain& chain, const StereoProgramme& programme, int block, int blocks)
{
    for (int b = 0; b < blocks; ++b)
    {
        const std::size_t at = static_cast<std::size_t>(b) * static_cast<std::size_t>(block);
        chain.processBlock(programme.left.data() + at, programme.right.data() + at, block);
    }
}

bool sameMeters(const Reading& a, const Reading& b)
{
    return sameBits(a.momentary, b.momentary) && sameBits(a.shortTerm, b.shortTerm)
        && sameBits(a.integrated, b.integrated) && sameBits(a.loudnessRange, b.loudnessRange)
        && sameBits(a.kLevelL, b.kLevelL) && sameBits(a.kLevelR, b.kLevelR)
        && sameBits(a.kLevelMono, b.kLevelMono) && sameBits(a.kPeakHoldL, b.kPeakHoldL)
        && sameBits(a.kPeakHoldR, b.kPeakHoldR) && sameBits(a.truePeakL, b.truePeakL)
        && sameBits(a.truePeakR, b.truePeakR) && a.clipped == b.clipped
        && sameBits(a.correlation, b.correlation) && sameBits(a.correlationRaw, b.correlationRaw);
}

bool sameReading(const Reading& a, const Reading& b)
{
    return std::strcmp(a.label, b.label) == 0 && a.position == b.position && sameMeters(a, b)
        && a.fftFrames == b.fftFrames;
}

bool sameTrace(const Trace& a, const Trace& b) { return std::memcmp(a.data(), b.data(), sizeof(Trace)) == 0; }

bool sameRun(const ScheduleRun& a, const ScheduleRun& b)
{
    for (std::size_t m = 0; m < kMarkCount; ++m)
        if (!sameReading(a.readings[m], b.readings[m]) || !sameTrace(a.display[m], b.display[m])
            || !sameTrace(a.peakHold[m], b.peakHold[m]))
            return false;
    return a.framePendingAtRelease == b.framePendingAtRelease
        && a.peakTraceAboveFloorAtRelease == b.peakTraceAboveFloorAtRelease;
}

//==============================================================================
// Floors of a prepared or released chain at the default K-14 scale: every
// loudness at -100, LRA 0, K levels at -100 dB RMS + 14 = -86, true peak at
// -100 dBTP and unclipped, correlation 0, no spectrum frame yet.
void checkFloors(const Reading& r, const std::string& tag)
{
    const bool loudness = sameBits(r.momentary, -100.0f) && sameBits(r.shortTerm, -100.0f)
                       && sameBits(r.integrated, -100.0f) && sameBits(r.loudnessRange, 0.0f);
    const bool kSystem = sameBits(r.kLevelL, -86.0f) && sameBits(r.kLevelR, -86.0f)
                      && sameBits(r.kLevelMono, -86.0f) && sameBits(r.kPeakHoldL, -86.0f)
                      && sameBits(r.kPeakHoldR, -86.0f);
    const bool peak = sameBits(r.truePeakL, -100.0f) && sameBits(r.truePeakR, -100.0f) && !r.clipped;
    const bool correlation = sameBits(r.correlation, 0.0f) && sameBits(r.correlationRaw, 0.0f);
    check(loudness && kSystem && peak && correlation && r.fftFrames == 0,
          tag + ": every meter at its floor (LUFS -100, LRA 0, K -86, TP -100 unclipped, corr 0, 0 frames)");
}

// The adapter's reset calls clear integrated loudness, LRA and the true-peak
// hold with its clip latch, and nothing else.
void checkAdapterResetCalls(const ScheduleRun& run, const std::string& tag)
{
    const Reading& before = run.readings[kMarkBeforeResets];
    const Reading& after = run.readings[kMarkResets];
    check(sameBits(after.integrated, -100.0f) && sameBits(after.loudnessRange, 0.0f),
          tag + ": resetIntegrated clears integrated loudness and LRA");
    check(sameBits(after.truePeakL, -100.0f) && sameBits(after.truePeakR, -100.0f) && !after.clipped,
          tag + ": resetPeakHold clears both true peaks and the clip latch");
    check(before.clipped, tag + ": the clip latch was set before the reset (the reset is observable)");
    check(sameBits(after.momentary, before.momentary) && sameBits(after.shortTerm, before.shortTerm)
              && sameBits(after.kLevelL, before.kLevelL) && sameBits(after.kLevelR, before.kLevelR)
              && sameBits(after.kLevelMono, before.kLevelMono)
              && sameBits(after.kPeakHoldL, before.kPeakHoldL)
              && sameBits(after.kPeakHoldR, before.kPeakHoldR)
              && sameBits(after.correlation, before.correlation)
              && sameBits(after.correlationRaw, before.correlationRaw)
              && after.fftFrames == before.fftFrames
              && sameTrace(run.display[kMarkResets], run.display[kMarkBeforeResets])
              && sameTrace(run.peakHold[kMarkResets], run.peakHold[kMarkBeforeResets]),
          tag + ": momentary, short-term, K-System, correlation and the spectrum are untouched");
}

// After prepare or release the FIFO holds nothing: a frame needs N fresh samples.
void checkEmptyFifo(FFTProcessor& fft, int fftSize, const StereoProgramme& programme, const std::string& tag)
{
    fft.pushSamples(programme.left.data(), programme.right.data(), fftSize - 1);
    fft.processFFT();
    const bool shortOfOne = !fft.isDataReady();
    fft.pushSamples(programme.left.data() + fftSize - 1, programme.right.data() + fftSize - 1, 1);
    fft.processFFT();
    check(shortOfOne && fft.isDataReady(),
          tag + ": no frame from " + std::to_string(fftSize - 1) + " samples, a frame from "
              + std::to_string(fftSize));
}

//==============================================================================
// The control-to-component mapping of parameterChanged, through the descriptor's
// index order: choice index i must select the i-th label's behaviour.
void checkControlMapping(const StereoProgramme& programme, int sampleRate)
{
    const std::string tag = "[mapping sr=" + std::to_string(sampleRate) + "]";
    const int block = sampleRate / 100;
    LegacyChain chain;
    chain.prepare(sampleRate, block, defaultControls());

    const float references[] = { -12.0f, -14.0f, -20.0f };   // K-12, K-14, K-20
    for (unsigned i = 0; i < parameterSpecs[PARAM_K_SYSTEM_TYPE].numLabels; ++i)
    {
        chain.applyControl(PARAM_K_SYSTEM_TYPE, static_cast<float>(i));
        check(chain.ksys.getType() == static_cast<KSystemMeter::Type>(i)
                  && sameBits(chain.ksys.getReferenceLevel(), references[i])
                  && std::string(KSystemMeter::getTypeName(chain.ksys.getType()))
                         == parameterSpecs[PARAM_K_SYSTEM_TYPE].labels[i],
              tag + " kSystemType " + std::to_string(i) + " -> "
                  + parameterSpecs[PARAM_K_SYSTEM_TYPE].labels[i] + " (reference "
                  + std::to_string(static_cast<int>(references[i])) + " dBFS)");
    }

    for (unsigned i = 0; i < parameterSpecs[PARAM_CHANNEL_MODE].numLabels; ++i)
    {
        chain.applyControl(PARAM_CHANNEL_MODE, static_cast<float>(i));
        check(chain.router.getMode() == static_cast<ChannelRouter::Mode>(i)
                  && std::string(ChannelRouter::getModeName(chain.router.getMode()))
                         == parameterSpecs[PARAM_CHANNEL_MODE].labels[i],
              tag + " channelMode " + std::to_string(i) + " -> "
                  + parameterSpecs[PARAM_CHANNEL_MODE].labels[i]);
    }

    for (unsigned i = 0; i < parameterSpecs[PARAM_FFT_RESOLUTION].numLabels; ++i)
    {
        auto controls = defaultControls();
        controls[PARAM_FFT_RESOLUTION] = static_cast<float>(i);
        LegacyChain resolved;
        resolved.prepare(sampleRate, block, controls);
        const int size = std::stoi(parameterSpecs[PARAM_FFT_RESOLUTION].labels[i]);
        check(resolved.currentFftSize() == size && fftSizeFor(controls) == size,
              tag + " fftResolution " + std::to_string(i) + " is labelled " + std::to_string(size));
        checkEmptyFifo(resolved.fft, size, programme,
                       tag + " fftResolution " + std::to_string(i) + " -> N=" + std::to_string(size));
    }
}

//==============================================================================
// The plan: "apply the adapter's Off peak-hold default; the standalone FFT class
// initially enables it". A bare FFTProcessor fed the same input differs from the
// default chain in its peak-hold array only.
void checkPeakHoldDefault(const ScheduleRun& defaults, const StereoProgramme& programme, int sampleRate)
{
    const std::string tag = "[peak hold sr=" + std::to_string(sampleRate) + "]";
    bool offEverywhere = true;
    for (std::size_t m = 0; m < kMarkCount; ++m)
        offEverywhere = offEverywhere && allAt(defaults.peakHold[m], -100.0f);
    check(offEverywhere && !defaults.peakTraceAboveFloorAtRelease,
          tag + " descriptor default (Off): the peak-hold trace stays at its floor at every mark");

    FFTProcessor bare;   // constructor settings, no control applied
    const int block = sampleRate / 100;
    bare.prepare(sampleRate, block);
    long long fed = 0, ticks = 0;
    for (int b = 0; b < kMarks[kMarkLastRead].block; ++b)
    {
        const std::size_t at = static_cast<std::size_t>(b) * static_cast<std::size_t>(block);
        bare.pushSamples(programme.left.data() + at, programme.right.data() + at, block);
        fed += block;
        for (; (ticks + 1) * sampleRate <= fed * 30; ++ticks)
        {
            bare.processFFT();
            bare.clearDataReady();
        }
    }
    const auto& peak = bare.getPeakHold();
    check(std::any_of(peak.begin(), peak.end(), [](float v) { return v > -100.0f; }),
          tag + " bare FFTProcessor: peak hold starts enabled (its trace rises above the floor)");
    check(sameTrace(bare.getMagnitudes(), defaults.display[kMarkLastRead]),
          tag + " bare FFTProcessor and the default chain draw the identical spectrum (peak hold is the only difference)");
}

//==============================================================================
// The gated-block history keeps the newest 6000 blocks (10 minutes): a loud
// first block still counts toward integrated loudness when the 6000th block
// completes and has dropped out once the 6001st does. Fed in 1 s calls, so the
// whole check costs one window sum per second of audio.
void checkGatedHistoryCap()
{
    constexpr int sampleRate = 44100;
    constexpr int blockSamples = 4410;   // 100 ms
    constexpr int period = 441;          // 1 kHz repeats every 441 samples at 44.1 kHz
    std::array<float, period> cycleLoud {}, cycleQuiet {};
    for (int i = 0; i < period; ++i)
    {
        const double s = std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / sampleRate);
        cycleLoud[(std::size_t)i] = static_cast<float>(0.999 * s);    // block 1: about -3 LUFS
        cycleQuiet[(std::size_t)i] = static_cast<float>(0.01 * s);    // after: about -43 LUFS
    }
    LUFSMeter meter;
    meter.prepare(sampleRate, 2);
    std::vector<float> chunk(static_cast<std::size_t>(sampleRate));
    long long sample = 0;
    const auto feed = [&](int numSamples) {
        for (int i = 0; i < numSamples; ++i, ++sample)
            chunk[(std::size_t)i] = (sample < blockSamples ? cycleLoud : cycleQuiet)[(std::size_t)(sample % period)];
        meter.process(chunk.data(), chunk.data(), numSamples);
    };
    for (int second = 0; second < 600; ++second)
        feed(sampleRate);                   // 6000 blocks
    const float with = meter.getIntegratedLUFS();
    feed(blockSamples);                     // the 6001st
    const float without = meter.getIntegratedLUFS();
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "[gated history] 6000 blocks keep the first (integrated %.2f LUFS > -40), the 6001st drops it (%.2f < -42)",
                  (double)with, (double)without);
    check(with > -40.0f && without < -42.0f, buf);
}

//==============================================================================
// The three loudness gates, each pinned to within 0.3 dB of its value by a pair
// of runs built 0.3 dB inside and 0.3 dB outside it. Levels come from the meter
// itself: a calibration run gives its reading L of a 1 kHz tone, and every
// other level is L moved by an exact amplitude ratio (the meter is linear in
// power), so a block d dB down reads L - d and the margins below are the
// meter's own:
//   absolute gate, -70 LUFS: blocks at -69.7 are stored, at -70.3 not;
//   integrated relative gate, ungated mean - 10 LU: 10 blocks at L - d and 90
//     at L sit 10 - 0.9 d from it, so d = 9.7 / 0.9 keeps them and
//     d = 10.3 / 0.9 drops them;
//   LRA relative gate, mean - 20 LU: 20 blocks at L - d and 80 at L sit
//     20 - 0.8 d from it, so d = 24.625 keeps them (LRA = d) and d = 25.375
//     drops them (LRA = 0).
// The quiet blocks come first. Right after loud ones, the first quiet block
// would also carry the K-weighting filters' decay from the loud tone and read
// high (+0.011 and +0.013 dB in the integrated runs, +0.27 and +0.31 dB in
// the LRA runs, which would keep one block of the LRA drop run). In this
// order every block reads within 0.0012 dB of its built level (the first
// block of a run reads about 0.0012 dB low, its filters starting from rest),
// so every block sits 0.2988 to 0.3012 dB from its decision in all three
// contraction modes; a gate moved 0.5 LU either way fails one run of its pair.
void checkLoudnessGates()
{
    constexpr int sampleRate = 48000, blockSamples = 4800;   // 100 ms = 100 cycles of 1 kHz
    constexpr double twoPi = 6.283185307179586476925;
    std::array<double, 48> cycle {};
    for (int i = 0; i < 48; ++i)
        cycle[(std::size_t)i] = std::sin(twoPi * i / 48.0);

    struct Readout { float integrated, loudnessRange; };
    const auto run = [&](const std::vector<double>& blockAmplitudes) {
        LUFSMeter meter;
        meter.prepare(sampleRate, 2);
        std::vector<float> chunk(static_cast<std::size_t>(blockSamples));
        for (const double amplitude : blockAmplitudes)
        {
            for (int i = 0; i < blockSamples; ++i)
                chunk[(std::size_t)i] = static_cast<float>(amplitude * cycle[(std::size_t)(i % 48)]);
            meter.process(chunk.data(), chunk.data(), blockSamples);
        }
        return Readout { meter.getIntegratedLUFS(), meter.getLoudnessRange() };
    };
    // Quiet blocks first, then loud ones (see above).
    const auto blocks = [](int quiet, double quietAmplitude, int loud, double loudAmplitude) {
        std::vector<double> v(static_cast<std::size_t>(quiet), quietAmplitude);
        v.insert(v.end(), static_cast<std::size_t>(loud), loudAmplitude);
        return v;
    };
    const auto down = [](double amplitude, double db) { return amplitude * std::pow(10.0, -db / 20.0); };

    constexpr double reference = 0.1;   // -20 dBFS
    constexpr double margin = 0.3;      // dB either side of every decision
    const double level = run(blocks(0, 0.0, 20, reference)).integrated;
    char buf[300];

    // Absolute gate.
    const double atGate = down(reference, level - (-70.0));   // a block reading exactly -70
    const Readout stored = run(blocks(20, down(atGate, -margin), 0, 0.0));
    const Readout notStored = run(blocks(20, down(atGate, margin), 0, 0.0));
    std::snprintf(buf, sizeof(buf),
                  "[loudness gates] absolute -70 LUFS: blocks built at -69.70 read %.2f (stored, want -69.70), "
                  "blocks at -70.30 read %.2f (not stored, want -100); margins +0.30 / -0.30",
                  (double)stored.integrated, (double)notStored.integrated);
    check(std::fabs(stored.integrated - (-69.7f)) < 0.05f && sameBits(notStored.integrated, -100.0f), buf);

    // Integrated loudness, relative gate.
    const double keptDown = (10.0 - margin) / 0.9, droppedDown = (10.0 + margin) / 0.9;
    const Readout kept = run(blocks(10, down(reference, keptDown), 90, reference));
    const Readout dropped = run(blocks(10, down(reference, droppedDown), 90, reference));
    const double keptWant = level + 10.0 * std::log10(0.9 + 0.1 * std::pow(10.0, -keptDown / 10.0));
    std::snprintf(buf, sizeof(buf),
                  "[loudness gates] integrated relative gate: 10 blocks %.2f dB down are kept "
                  "(%.3f, want %.3f), %.2f dB down dropped (%.3f, want %.3f); margins +0.30 / -0.30 LU",
                  keptDown, (double)kept.integrated, keptWant, droppedDown, (double)dropped.integrated, level);
    check(std::fabs(kept.integrated - keptWant) < 0.05 && std::fabs(dropped.integrated - level) < 0.05, buf);

    // Loudness range, relative gate.
    const double wideDown = (20.0 - margin) / 0.8, narrowDown = (20.0 + margin) / 0.8;   // 24.625, 25.375
    const Readout wide = run(blocks(20, down(reference, wideDown), 80, reference));
    const Readout narrow = run(blocks(20, down(reference, narrowDown), 80, reference));
    std::snprintf(buf, sizeof(buf),
                  "[loudness gates] LRA relative gate: 20 blocks %.3f dB down are kept (LRA %.3f, want %.3f), "
                  "%.3f dB down dropped (LRA %.3f, want 0); margins +0.30 / -0.30 LU",
                  wideDown, (double)wide.loudnessRange, wideDown, narrowDown, (double)narrow.loudnessRange);
    check(std::fabs(wide.loudnessRange - wideDown) < 0.05 && narrow.loudnessRange < 0.05f, buf);
}

//==============================================================================
// The spectrum display's laws, from the code's own constants, one frame per
// push at 48 kHz and 8192 points: an on-bin sine reads its level; the slope
// adds slope x log2(f / 1 kHz); smoothing is a one-pole of 0.95 x smoothing per
// frame; peak hold follows a rise, holds for peakHoldTime x 30 frames and then
// falls by decayRate / 30 dB per frame. Levels compare within kDisplayBound,
// the hold length exactly, the smoothing ratio and the decay step to 1e-4
// (float rounding of one operation on values of 10 dB or more is below 1e-6).
void checkSpectrumLaws()
{
    constexpr int sampleRate = 48000, fftSize = 8192, toneBin = 171;   // 171 x 48000 / 8192 = 1001.95 Hz
    constexpr double amplitude = 0.125;                                 // -18.06 dBFS
    constexpr double twoPi = 6.283185307179586476925;
    std::vector<float> tone(static_cast<std::size_t>(fftSize)), silence(static_cast<std::size_t>(fftSize), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        tone[(std::size_t)i] = static_cast<float>(amplitude * std::sin(twoPi * toneBin * i / fftSize));

    // The display bin that reads FFT bin 171.
    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto fftBinOf = [binWidth](int displayBin) {
        return static_cast<int>(FFTProcessor::getFrequencyForBin(displayBin) / binWidth);
    };
    int bin = FFTProcessor::getBinForFrequency(static_cast<float>(toneBin) * binWidth);
    while (fftBinOf(bin) < toneBin) ++bin;
    while (fftBinOf(bin) > toneBin) --bin;

    const auto analyzer = [](float smoothing, float slope, bool hold) {
        auto fft = std::make_unique<FFTProcessor>();
        fft->prepare(sampleRate, fftSize);
        fft->setResolution(FFTProcessor::Resolution::High);
        fft->setSmoothing(smoothing);
        fft->setSlope(slope);
        fft->setDecayRate(6.0f);
        fft->setPeakHoldEnabled(hold);
        fft->setPeakHoldTime(0.5f);
        return fft;
    };
    const auto frame = [&](FFTProcessor& fft, const std::vector<float>& samples) {
        fft.pushSamples(samples.data(), samples.data(), fftSize);
        fft.processFFT();
        fft.clearDataReady();
    };

    auto flat = analyzer(0.0f, 0.0f, false);
    frame(*flat, tone);
    const double level = flat->getMagnitudes()[(std::size_t)bin];
    char buf[200];
    std::snprintf(buf, sizeof(buf), "[display law] an on-bin sine of -18.06 dBFS reads %.5f dB", level);
    check(distance(static_cast<float>(level), static_cast<float>(20.0 * std::log10(amplitude))) <= kDisplayBound, buf);

    auto sloped = analyzer(0.0f, 3.0f, false);
    frame(*sloped, tone);
    const double shift = sloped->getMagnitudes()[(std::size_t)bin] - level;
    const double wantShift = 3.0 * std::log2(static_cast<double>(FFTProcessor::getFrequencyForBin(bin)) / 1000.0);
    std::snprintf(buf, sizeof(buf), "[display law] slope +3 dB/oct adds %.5f dB at %.2f Hz (3 x log2(f / 1 kHz) = %.5f)",
                  shift, (double)FFTProcessor::getFrequencyForBin(bin), wantShift);
    check(std::fabs(shift - wantShift) <= kDisplayBound, buf);

    // Smoothing 0.9 with peak hold: six tone frames, then silence.
    auto held = analyzer(0.9f, 0.0f, true);
    constexpr int toneFrames = 6, silentFrames = 40;
    std::vector<float> s, p;
    for (int f = 0; f < toneFrames + silentFrames; ++f)
    {
        frame(*held, f < toneFrames ? tone : silence);
        s.push_back(held->getMagnitudes()[(std::size_t)bin]);
        p.push_back(held->getPeakHold()[(std::size_t)bin]);
    }
    bool tracksRise = true;
    for (int f = 0; f < toneFrames; ++f)
        tracksRise = tracksRise && sameBits(p[(std::size_t)f], s[(std::size_t)f]) && (f == 0 || s[(std::size_t)f] > s[(std::size_t)f - 1]);
    check(tracksRise, "[display law] peak hold follows a rising trace frame by frame");

    const double coefficient = 0.9f * 0.95f;
    double worstRatio = 0.0;
    for (int f = toneFrames; f + 1 < toneFrames + silentFrames && s[(std::size_t)f] + 100.0 > 10.0; ++f)
        worstRatio = std::max(worstRatio, std::fabs((s[(std::size_t)f + 1] + 100.0) / (s[(std::size_t)f] + 100.0) - coefficient));
    std::snprintf(buf, sizeof(buf), "[display law] smoothing 0.9 decays toward the floor by 0.855 per frame (worst ratio error %.1e)",
                  worstRatio);
    check(worstRatio <= 1e-4, buf);

    const float lastRise = p[toneFrames - 1];
    int holdFrames = 0;
    while (toneFrames + holdFrames < toneFrames + silentFrames && sameBits(p[(std::size_t)(toneFrames + holdFrames)], lastRise))
        ++holdFrames;
    std::snprintf(buf, sizeof(buf), "[display law] peak hold of 0.5 s holds for %d frames (0.5 x 30 = 15)", holdFrames);
    check(holdFrames == 15, buf);

    double worstStep = 0.0;
    const int firstFall = toneFrames + holdFrames;
    for (int f = firstFall; f < firstFall + 10 && f < toneFrames + silentFrames; ++f)
        worstStep = std::max(worstStep, std::fabs((p[(std::size_t)f - 1] - p[(std::size_t)f]) - 6.0 / 30.0));
    std::snprintf(buf, sizeof(buf), "[display law] then falls by decayRate / 30 = 0.2 dB per frame (worst step error %.1e)", worstStep);
    check(worstStep <= 1e-4, buf);
}

//==============================================================================
// The tripwire for LegacyChain: if any adapter body it transcribes changes, the
// model may no longer match and must be re-checked before this test means
// anything.
void checkAdapterUnchanged(const std::string& path)
{
    try
    {
        const std::string code = source_text::stripComments(source_text::readFile(path));
        for (const auto& f : kAdapterFunctions)
        {
            const std::uint64_t h = source_text::fnv1a64(
                source_text::normaliseWhitespace(source_text::functionBody(code, f.signature)));
            char buf[400];
            if (h == f.hash)
                std::snprintf(buf, sizeof(buf), "[adapter] %s...) body matches the one LegacyChain transcribes",
                              f.signature);
            else
                std::snprintf(buf, sizeof(buf),
                              "[adapter] %s...) body changed (hash 0x%016llx): the adapter changed, re-check "
                              "LegacyChain against Source/PluginProcessor.cpp, then freeze the new hash",
                              f.signature, (unsigned long long)h);
            check(h == f.hash, buf);
        }
    }
    catch (const source_text::SourceError& e)
    {
        check(false, "[adapter] cannot read the adapter: " + e.message);
    }
}

//==============================================================================
// Distance of a display bin's FFT-bin coordinate from an integer, computed the
// way processFFT() computes it. A digest bin far from an integer keeps its FFT
// bin in every build.
float mappingMargin(int displayBin, int sampleRate, int fftSize)
{
    const float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const float x = FFTProcessor::getFrequencyForBin(displayBin) / binWidth;
    return std::fabs(x - std::round(x));
}

// Emit mode only: for each target frequency, the nearest display bin whose FFT
// bin is unambiguous at both 4096 and 8192 points. 1 kHz and 1.5 kHz are the
// programme's tones; fs/4 is its intersample tone (capped at the 20 kHz top).
std::array<int, kDigestBins> selectDigestBins(int sampleRate)
{
    const float targets[kDigestBins] = { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 750.0f, 1000.0f, 1500.0f,
                                         2000.0f, 3000.0f, 4000.0f, 6000.0f, 8000.0f,
                                         std::min(sampleRate / 4.0f, 20000.0f), 15000.0f, 18000.0f };
    std::array<int, kDigestBins> bins {};
    for (int t = 0; t < kDigestBins; ++t)
    {
        const int start = FFTProcessor::getBinForFrequency(targets[t]);
        int chosen = -1;
        for (int offset = 0; offset < 64 && chosen < 0; ++offset)
            for (const int candidate : { start - offset, start + offset })
            {
                if (chosen >= 0 || candidate < 0 || candidate >= kDisplayBins)
                    continue;
                const bool unused = std::find(bins.begin(), bins.begin() + t, candidate) == bins.begin() + t;
                if (unused && mappingMargin(candidate, sampleRate, 4096) >= 0.05f
                    && mappingMargin(candidate, sampleRate, 8192) >= 0.05f)
                    chosen = candidate;
            }
        bins[(std::size_t)t] = chosen;
    }
    return bins;
}

ControlSetDigest digestOf(const ScheduleRun& run, const int* bins)
{
    ControlSetDigest d {};
    for (std::size_t k = 0; k < kDigestMarkCount; ++k)
    {
        const std::size_t m = kDigestMarks[k];
        d.at[k].fftFrames = run.readings[m].fftFrames;
        for (int b = 0; b < kDigestBins; ++b)
        {
            d.at[k].display[b] = run.display[m][(std::size_t)bins[b]];
            d.at[k].peakHold[b] = run.peakHold[m][(std::size_t)bins[b]];
        }
    }
    return d;
}

//==============================================================================
RateCapture captureRate(int sampleRate)
{
    const std::string rateTag = "sr=" + std::to_string(sampleRate);
    const auto programme = spectrum_capture::makeProgramme(sampleRate);
    const int block = sampleRate / 100;

    RateCapture live {};
    live.sampleRate = sampleRate;
    live.blockSize = block;
    live.programmeHash = spectrum_capture::hashProgramme(programme);

    const RateCapture* frozen = nullptr;
    for (const auto& f : kFrozen)
        if (f.sampleRate == sampleRate)
            frozen = &f;
    const auto bins = selectDigestBins(sampleRate);
    for (int b = 0; b < kDigestBins; ++b)
        live.digestBins[b] = (!emitting && frozen != nullptr) ? frozen->digestBins[b] : bins[(std::size_t)b];

    std::vector<ScheduleRun> runs;
    runs.reserve(kSetCount);   // `run` below refers into it across iterations
    for (int set = 0; set < kSetCount; ++set)
    {
        const auto controls = controlsFor(static_cast<ControlSet>(set));
        const int fftSize = fftSizeFor(controls);
        const std::string tag = "[lifecycle " + rateTag + " set " + kSetNames[set] + "]";

        LegacyChain fresh;
        fresh.prepare(sampleRate, block, controls);
        check(!fresh.fft.isDataReady() && allAt(fresh.fft.getMagnitudes(), -100.0f)
                  && allAt(fresh.fft.getPeakHold(), -100.0f),
              tag + " prepared: spectrum and peak-hold traces at -100 dB, no frame pending");
        runs.push_back(runSchedule(fresh, programme, sampleRate));
        const ScheduleRun& run = runs.back();

        checkFloors(run.readings[0], tag + " prepared");
        checkFloors(run.readings[kMarkCount - 1], tag + " released");
        checkAdapterResetCalls(run, tag + " adapter reset calls");
        check(run.framePendingAtRelease && (set == kSetA || run.peakTraceAboveFloorAtRelease),
              tag + " before release: a computed frame is pending"
                  + std::string(set == kSetB ? " and the peak-hold trace is above its floor" : ""));
        check(!fresh.fft.isDataReady() && allAt(run.display[kMarkCount - 1], -100.0f)
                  && allAt(run.peakHold[kMarkCount - 1], -100.0f),
              tag + " released: the pending frame is dropped, spectrum and peak-hold traces back at -100 dB");
        checkEmptyFifo(fresh.fft, fftSize, programme, tag + " released FIFO");

        // Re-prepare from another rate after a release (a host rate change).
        {
            const int other = sampleRate == 44100 ? 96000 : 44100;
            const auto otherProgramme = spectrum_capture::makeProgramme(other);
            LegacyChain chain;
            chain.prepare(other, other / 100, controls);
            dirty(chain, otherProgramme, other / 100, 1050);
            chain.release();
            chain.prepare(sampleRate, block, controls);
            check(sameRun(runSchedule(chain, programme, sampleRate), run),
                  tag + " re-prepare from " + std::to_string(other)
                      + " Hz after release: bit-identical to a fresh chain (meters, spectrum, peak hold, every mark)");
        }

        // Prepare again without a release, from other controls and 512-sample blocks.
        {
            LegacyChain chain;
            chain.prepare(sampleRate, 512, dirtyingControls());
            dirty(chain, programme, 512, (12 * sampleRate) / 512);
            chain.adapterResetCalls();
            chain.prepare(sampleRate, block, controls);
            check(sameRun(runSchedule(chain, programme, sampleRate), run),
                  tag + " prepare again (no release) from other controls and 512-sample blocks: bit-identical to a fresh chain");
        }

        live.spectrum[set] = digestOf(run, live.digestBins);
    }

    // Routing and spectrum settings reach only the spectrum; every meter reads the input.
    {
        bool metersMatch = true;
        for (std::size_t m = 0; m < kMarkCount; ++m)
            metersMatch = metersMatch && sameMeters(runs[kSetB].readings[m], runs[kSetA].readings[m]);
        const bool spectrumDiffers = !sameTrace(runs[kSetB].display[kMarkLastRead], runs[kSetA].display[kMarkLastRead]);
        check(metersMatch && spectrumDiffers,
              "[lifecycle " + rateTag + "] set B (Mid, 8192, peak hold On...) vs A: every meter reading bit-identical, the spectrum differs");
    }

    checkPeakHoldDefault(runs[kSetA], programme, sampleRate);

    for (std::size_t m = 0; m < kMarkCount; ++m)
        live.readings[m] = runs[kSetA].readings[m];
    return live;
}

//==============================================================================
struct FloatField
{
    const char* name;
    float Reading::*member;
    double bound;
};

constexpr FloatField kFloatFields[] = {
    { "momentary",      &Reading::momentary,      kMomentaryBound },
    { "shortTerm",      &Reading::shortTerm,      kShortTermBound },
    { "integrated",     &Reading::integrated,     kIntegratedBound },
    { "loudnessRange",  &Reading::loudnessRange,  kLoudnessRangeBound },
    { "kLevelL",        &Reading::kLevelL,        kKLevelBound },
    { "kLevelR",        &Reading::kLevelR,        kKLevelBound },
    { "kLevelMono",     &Reading::kLevelMono,     kKLevelBound },
    { "kPeakHoldL",     &Reading::kPeakHoldL,     kKPeakHoldBound },
    { "kPeakHoldR",     &Reading::kPeakHoldR,     kKPeakHoldBound },
    { "truePeakL",      &Reading::truePeakL,      kTruePeakBound },
    { "truePeakR",      &Reading::truePeakR,      kTruePeakBound },
    { "correlation",    &Reading::correlation,    kCorrelationBound },
    { "correlationRaw", &Reading::correlationRaw, kCorrelationRawBound },
};

void compareFrozen(const RateCapture& live)
{
    const std::string rate = std::to_string(live.sampleRate);
    const RateCapture* frozen = nullptr;
    for (const auto& f : kFrozen)
        if (f.sampleRate == live.sampleRate)
            frozen = &f;
    if (frozen == nullptr)
    {
        check(false, "[frozen sr=" + rate + "] no frozen capture for this rate");
        return;
    }

    char hash[64];
    std::snprintf(hash, sizeof(hash), "0x%016llx", live.programmeHash);
    check(frozen->programmeHash == live.programmeHash && frozen->blockSize == live.blockSize,
          "[frozen sr=" + rate + "] programme hash " + hash + " and block " + std::to_string(live.blockSize)
              + " match: the input is bit-identical to the frozen run");

    bool binsUnambiguous = true;
    for (int b = 0; b < kDigestBins; ++b)
        binsUnambiguous = binsUnambiguous && frozen->digestBins[b] >= 0
                       && mappingMargin(frozen->digestBins[b], live.sampleRate, 4096) >= 0.02f
                       && mappingMargin(frozen->digestBins[b], live.sampleRate, 8192) >= 0.02f;
    check(binsUnambiguous, "[frozen sr=" + rate + "] every digest bin maps to one FFT bin, at least 0.02 bins from an edge");

    for (std::size_t m = 0; m < kMarkCount; ++m)
    {
        const Reading& got = live.readings[m];
        const Reading& want = frozen->readings[m];
        const std::string tag = "[frozen sr=" + rate + " " + got.label + " @" + std::to_string(got.position) + "]";
        std::string problems;
        if (std::strcmp(got.label, want.label) != 0 || got.position != want.position)
            problems += " schedule differs (frozen '" + std::string(want.label) + "' @"
                      + std::to_string(want.position) + ");";
        double worst = 0.0;   // as a fraction of each field's bound
        for (const auto& field : kFloatFields)
        {
            const double delta = distance(got.*field.member, want.*field.member);
            worst = std::max(worst, delta / field.bound);
            if (!(delta <= field.bound))
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf), " %s %.9g vs frozen %.9g (|d| %.3g > %.3g);", field.name,
                              (double)(got.*field.member), (double)(want.*field.member), delta, field.bound);
                problems += buf;
            }
        }
        if (got.clipped != want.clipped)
            problems += std::string(" clipped ") + (got.clipped ? "true" : "false") + " vs frozen "
                      + (want.clipped ? "true" : "false") + ";";
        if (got.fftFrames != want.fftFrames)
            problems += " fftFrames " + std::to_string(got.fftFrames) + " vs frozen "
                      + std::to_string(want.fftFrames) + ";";
        char summary[120];
        std::snprintf(summary, sizeof(summary), " 13 readings within their bounds (worst %.0f%% of a bound), flag and frames exact",
                      100.0 * worst);
        check(problems.empty(), tag + (problems.empty() ? std::string(summary) : problems));
    }

    for (int set = 0; set < kSetCount; ++set)
        for (std::size_t k = 0; k < kDigestMarkCount; ++k)
        {
            const SpectrumDigest& got = live.spectrum[set].at[k];
            const SpectrumDigest& want = frozen->spectrum[set].at[k];
            const std::string tag = "[frozen sr=" + rate + " set " + kSetNames[set] + " spectrum "
                                  + kMarks[kDigestMarks[k]].label + "]";
            std::string problems;
            double worst = 0.0;
            for (int b = 0; b < kDigestBins; ++b)
            {
                const double dd = distance(got.display[b], want.display[b]);
                const double dp = distance(got.peakHold[b], want.peakHold[b]);
                worst = std::max({ worst, dd / kDisplayBound, dp / kSpectrumPeakBound });
                char buf[200];
                if (!(dd <= kDisplayBound))
                {
                    std::snprintf(buf, sizeof(buf), " bin %d display %.9g vs frozen %.9g (|d| %.3g);",
                                  live.digestBins[b], (double)got.display[b], (double)want.display[b], dd);
                    problems += buf;
                }
                if (!(dp <= kSpectrumPeakBound))
                {
                    std::snprintf(buf, sizeof(buf), " bin %d peak hold %.9g vs frozen %.9g (|d| %.3g);",
                                  live.digestBins[b], (double)got.peakHold[b], (double)want.peakHold[b], dp);
                    problems += buf;
                }
            }
            if (got.fftFrames != want.fftFrames)
                problems += " fftFrames " + std::to_string(got.fftFrames) + " vs frozen " + std::to_string(want.fftFrames) + ";";
            char summary[120];
            std::snprintf(summary, sizeof(summary), " 16 bins of display and peak hold within bounds (worst %.0f%%), frames exact",
                          100.0 * worst);
            check(problems.empty(), tag + (problems.empty() ? std::string(summary) : problems));
        }
}

//==============================================================================
std::string literal(float v)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%#.9gf", static_cast<double>(v));
    return buf;
}

std::string floatList(const float* values, int count)
{
    std::string s;
    for (int i = 0; i < count; ++i)
        s += (i == 0 ? "" : (i % 4 == 0 ? ",\n            " : ", ")) + literal(values[i]);
    return s;
}

void emitFixtures(const std::vector<RateCapture>& captures)
{
#if defined(__VERSION__)
    const char* compiler = __VERSION__;
#else
    const char* compiler = "unknown compiler";
#endif
    std::printf("// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).\n"
                "//\n"
                "// GENERATED by `SpectrumCoreCaptureTest <PluginProcessor.cpp> --emit`\n"
                "// (tests/test_core_capture.cpp): one RateCapture per sample rate, fields in\n"
                "// declaration order. Regenerate only for a deliberate, reviewed calibration\n"
                "// change, never to make a failing run pass, and from a -ffp-contract=off build.\n"
                "// Values are float literals that round-trip exactly. Emitted by: %s\n", compiler);
    for (const auto& c : captures)
    {
        std::printf("{ %d, %d, 0x%016llxull,\n  {", c.sampleRate, c.blockSize, c.programmeHash);
        for (int b = 0; b < kDigestBins; ++b)
            std::printf("%s%d", b == 0 ? " " : ", ", c.digestBins[b]);
        std::printf(" },\n  {\n");
        for (std::size_t m = 0; m < kMarkCount; ++m)
        {
            const Reading& r = c.readings[m];
            std::printf("    { \"%s\", %lld,\n"
                        "      %s, %s, %s, %s,\n"
                        "      %s, %s, %s, %s, %s,\n"
                        "      %s, %s, %s,\n"
                        "      %s, %s, %d },\n",
                        r.label, r.position,
                        literal(r.momentary).c_str(), literal(r.shortTerm).c_str(),
                        literal(r.integrated).c_str(), literal(r.loudnessRange).c_str(),
                        literal(r.kLevelL).c_str(), literal(r.kLevelR).c_str(), literal(r.kLevelMono).c_str(),
                        literal(r.kPeakHoldL).c_str(), literal(r.kPeakHoldR).c_str(),
                        literal(r.truePeakL).c_str(), literal(r.truePeakR).c_str(),
                        r.clipped ? "true" : "false",
                        literal(r.correlation).c_str(), literal(r.correlationRaw).c_str(), r.fftFrames);
        }
        std::printf("  },\n  {\n");
        for (int set = 0; set < kSetCount; ++set)
        {
            std::printf("    { { // control set %s\n", kSetNames[set]);
            for (std::size_t k = 0; k < kDigestMarkCount; ++k)
            {
                const SpectrumDigest& d = c.spectrum[set].at[k];
                std::printf("      { %d, // %s\n          { %s },\n          { %s } },\n", d.fftFrames,
                            kMarks[kDigestMarks[k]].label, floatList(d.display, kDigestBins).c_str(),
                            floatList(d.peakHold, kDigestBins).c_str());
            }
            std::printf("    } },\n");
        }
        std::printf("  } },\n");
    }
}
} // namespace

int main(int argc, char** argv)
{
    emitting = argc == 3 && std::strcmp(argv[2], "--emit") == 0;
    if (argc < 2 || argc > 3 || (argc == 3 && !emitting))
    {
        std::printf("usage: %s <Source/PluginProcessor.cpp> [--emit]\n", argv[0]);
        return 2;
    }

    checkAdapterUnchanged(argv[1]);
    checkGatedHistoryCap();
    checkLoudnessGates();
    checkSpectrumLaws();

    std::vector<RateCapture> captures;
    for (const int sampleRate : kSampleRates)
    {
        checkControlMapping(spectrum_capture::makeProgramme(sampleRate), sampleRate);
        captures.push_back(captureRate(sampleRate));
    }

    if (emitting)
    {
        if (failures != 0)
        {
            std::fprintf(stderr, "\nnot emitting: %d check(s) failed\n", failures);
            return 1;
        }
        emitFixtures(captures);
        return 0;
    }

    check(sizeof(kFrozen) / sizeof(kFrozen[0]) == sizeof(kSampleRates) / sizeof(kSampleRates[0]),
          "frozen fixture holds one capture per sample rate");
    for (const auto& c : captures)
        compareFrozen(c);

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "CORE CAPTURE HOLDS" : "CORE CAPTURE BROKEN",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
