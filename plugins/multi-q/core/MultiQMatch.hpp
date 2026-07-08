// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQMatch.hpp — framework-free spectrum-match EQ core (zero JUCE/DPF).
//
// Line-for-line port of plugins/multi-q/EQMatchProcessor.h (Logic-Pro-style Match
// EQ): Welch-method spectrum capture (learn current + reference), correction-curve
// computation (reference/current, semitone smoothing, boost/cut limiting) and FIR
// generation (minimum-phase cepstral or linear-phase). juce::dsp::FFT is replaced
// by a self-contained radix-2 complex FFT (FFTr2); juce types by std; the
// juce::SpinLock cross-thread transfer by the DoubleBuffer / atomic-published
// pattern already used in the core.
//
// This class ALSO owns the FIR-correction convolution (JUCE used juce::dsp::
// Convolution): an RT-safe uniformly-partitioned overlap-save FFT convolver with a
// lock-free double-buffered impulse response and an aligned dry/wet crossfade.
//
// Thread model (mirrors MultiQ.cpp):
//   * Message thread (UI bridge): requestStartLearn*/requestClear set atomics;
//     computeCorrection() runs the FFT-heavy correction+FIR build directly (learn
//     is stopped by then) and lock-free-publishes the partition spectra.
//   * Audio thread: audioThreadSync() consumes the pending atomics (so the
//     learned-spectrum accumulators are only ever mutated on the audio thread,
//     exactly like MultiQ.cpp:511-535); feedLearningBlock() accumulates frames;
//     process() runs the convolver reading the published IR.

#pragma once

#include "MultiQFilters.hpp"  // kMultiQPi, safeIsFinite
#include "MultiQParams.hpp"   // LinearSmoothedValue

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace duskaudio
{

// Lock-free single-writer / single-reader publish buffer (same contract as the
// DoubleBuffer in EQMatchProcessor.h): the writer fills writeBuffer() then
// publish()es; a reader always sees a fully-written snapshot via readBuffer().
template<typename T>
struct MatchDoubleBuffer
{
    std::array<T, 2> buffers{};
    std::atomic<int> readIndex{0};

    T& writeBuffer() { return buffers[(size_t)(1 - readIndex.load(std::memory_order_relaxed))]; }
    void publish() { readIndex.fetch_xor(1, std::memory_order_release); }
    const T& readBuffer() const { return buffers[(size_t)readIndex.load(std::memory_order_acquire)]; }
};

// Minimal spin lock — used ONLY between non-realtime threads (message-thread
// correction writer vs. UI/state readers of correctionCurveDB/correctionFIR). The
// audio thread never touches it (it reads the lock-free partition spectra instead).
struct MatchSpinLock
{
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    void lock()   { while (flag.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { flag.clear(std::memory_order_release); }
};

//==============================================================================
// Self-contained iterative radix-2 complex FFT with precomputed twiddles +
// bit-reversal. Operates in-place on separate real/imag arrays. inverse()
// applies the 1/N normalisation (matching juce::dsp::FFT::performInverse*).
//==============================================================================
class FFTr2
{
public:
    void prepare(int size);
    void forward(float* re, float* im) const { transform(re, im, false); }
    void inverse(float* re, float* im) const { transform(re, im, true); }
    int size() const noexcept { return n; }

private:
    void transform(float* re, float* im, bool inv) const;
    int n = 0;
    std::vector<int>   rev;   // bit-reversal permutation
    std::vector<float> twR, twI; // twiddles W[j] = exp(-2πi j / n), j in 0..n/2-1
};

//==============================================================================
// MultiQMatch — the spectrum-match EQ processor + correction convolver.
//==============================================================================
class MultiQMatch
{
public:
    static constexpr int FFT_ORDER = 12;               // 4096-point analysis FFT
    static constexpr int FFT_SIZE  = 1 << FFT_ORDER;   // 4096
    static constexpr int NUM_BINS  = FFT_SIZE / 2 + 1;  // 2049
    static constexpr int FIR_LENGTH = 4096;             // correction FIR length
    static constexpr int HOP_SIZE  = FFT_SIZE / 2;      // 50% overlap (Welch)

    // Convolver (uniformly-partitioned overlap-save). Partition/hop = 128 gives a
    // 128-sample reported latency (min-phase FIR carries ~0 inherent delay).
    static constexpr int CONV_HOP  = 128;
    static constexpr int CONV_FFT  = 256;               // 2 * CONV_HOP
    static constexpr int NUM_PART  = FIR_LENGTH / CONV_HOP; // 32
    static constexpr int OUT_RING  = 4 * CONV_HOP;      // 512, power of two

    MultiQMatch() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    double getSampleRate() const noexcept { return sampleRate; }

    // --- message-thread control (UI bridge). These only set atomics; the audio
    //     thread applies them in audioThreadSync() (MultiQ.cpp:511-535). --------
    void requestStartLearnCurrent()   { pendingStartCurrent.store(true, std::memory_order_release); }
    void requestStartLearnReference() { pendingStartReference.store(true, std::memory_order_release); }
    void requestStopLearning()        { pendingStop.store(true, std::memory_order_release); }
    void requestClear()               { pendingClear.store(true, std::memory_order_release); }

    // Build the correction curve + FIR from the two learned spectra and lock-free
    // publish the partition spectra to the audio thread. Runs on the message
    // thread (FFT-heavy). Returns false if either spectrum is missing.
    bool computeCorrection(float smoothingSemitones, float applyAmount,
                           float limitBoostDB, float limitCutDB, bool minimumPhase);

    // --- learning-state queries (any thread) ----------------------------------
    bool isLearning() const          { return learningTarget.load(std::memory_order_acquire) != Target::None; }
    bool isLearningCurrent() const   { return learningTarget.load(std::memory_order_acquire) == Target::Current; }
    bool isLearningReference() const { return learningTarget.load(std::memory_order_acquire) == Target::Reference; }
    int  getLearningFrameCount() const;
    bool hasCurrentSpectrum() const   { return currentSpectrumBuf.readBuffer().valid; }
    bool hasReferenceSpectrum() const { return referenceSpectrumBuf.readBuffer().valid; }
    bool hasCorrectionCurve() const   { return correctionValid.load(std::memory_order_acquire); }

    // dB spectra for UI display (write up to n = NUM_BINS values). Lock-free reads.
    void getCurrentSpectrumDB(float* out, int n) const;
    void getReferenceSpectrumDB(float* out, int n) const;
    void getCorrectionCurveDB(float* out, int n) const;

    // --- audio-thread entry points --------------------------------------------
    // Consume pending message-thread requests. Call once at the top of process().
    void audioThreadSync();
    // Accumulate mono learning frames (Welch). Call when isLearning() is true.
    void feedLearningBlock(const float* monoSamples, int numSamples);
    // Convenience: downmix a stereo (or mono, R==nullptr) input and feed it —
    // used by the core to feed raw input (MultiQ.cpp:539-548).
    void feedLearningStereo(const float* left, const float* right, int numSamples);
    // Apply the correction FIR to a stereo (or mono) block in place. No-op (leaves
    // the buffer untouched, zero latency) when no correction is active.
    void process(float* left, float* right, bool isStereo, int numSamples);
    // Reported extra latency in samples (0 when inactive).
    int getLatencySamples() const noexcept;

    // --- state persistence (message thread). Serialises the learned spectra +
    //     correction curve + FIR as a base64 blob, restored by deserialize(). ---
    std::string serialize() const;
    bool deserialize(const std::string& base64);

private:
    // Welch-method averaged periodogram (identical semantics to EQMatchProcessor).
    struct LearnedSpectrum
    {
        std::array<double, NUM_BINS> powerSum{};
        int  frameCount = 0;
        bool valid = false;

        void reset() { powerSum.fill(0.0); frameCount = 0; valid = false; }
        void addFrame(const float* re, const float* im)
        {
            for (int k = 0; k < NUM_BINS; ++k)
            {
                const double r = re[k], im2 = im[k];
                powerSum[(size_t)k] += r * r + im2 * im2;
            }
            ++frameCount;
            if (frameCount >= 3) valid = true;
        }
        void getAverageMagnitudeDB(float* outDB) const
        {
            if (frameCount <= 0) { for (int k = 0; k < NUM_BINS; ++k) outDB[k] = -100.0f; return; }
            const double invCount = 1.0 / (double)frameCount;
            for (int k = 0; k < NUM_BINS; ++k)
            {
                const double mag = std::sqrt(powerSum[(size_t)k] * invCount + 1e-30);
                outDB[k] = (float)(20.0 * std::log10(mag + 1e-30));
            }
        }
    };

    enum class Target { None, Current, Reference };

    // Partition spectra of the correction FIR (audio-thread read, message-thread
    // written into the inactive slot then atomically published via irActive).
    struct IRSpectra
    {
        std::array<std::array<float, CONV_FFT>, NUM_PART> re{};
        std::array<std::array<float, CONV_FFT>, NUM_PART> im{};
    };

    // Per-channel overlap-save convolver state (FDL + sliding window + FIFOs).
    struct ChannelConv
    {
        std::array<float, CONV_FFT> window{};           // sliding input window (last 256 in)
        std::array<std::array<float, CONV_FFT>, NUM_PART> fdlRe{}, fdlIm{}; // freq-domain delay line
        std::array<float, OUT_RING> outRing{};          // convolved-output FIFO
        std::array<float, CONV_HOP> dryDelay{};         // aligned dry path (128-sample delay)
        int fdlHead = 0;
        int outRead = 0, outWrite = 0, outCount = 0;
        int dryPos = 0;
        void reset();
    };

    void feedSample(float s);
    void processLearningFrame(Target target);
    void applyFractionalOctaveSmoothing(std::array<float, NUM_BINS>& curve, float semitones) const;
    void generateLinearPhaseFIR(const std::array<float, NUM_BINS>& curveDB);
    void generateMinimumPhaseFIR(const std::array<float, NUM_BINS>& curveDB);
    void installFIR();   // build partition spectra from correctionFIR + publish
    void processHop(ChannelConv& c, const IRSpectra* H); // one 128-sample conv hop
    void doClearNow();   // audio-thread immediate clear of spectra/state

    double sampleRate = 48000.0;

    // learning
    std::atomic<Target> learningTarget{Target::None};
    LearnedSpectrum currentSpectrum, referenceSpectrum; // audio-thread owned
    MatchDoubleBuffer<LearnedSpectrum> currentSpectrumBuf, referenceSpectrumBuf;
    std::array<float, FFT_SIZE> hannWindow{};
    std::vector<float> learningInput;   // circular input buffer (FFT_SIZE)
    int learningInputPos = 0;
    int samplesSinceLastFrame = 0;
    // FFT scratch (audio thread learning + message-thread gen use separate ones)
    std::array<float, FFT_SIZE> learnRe{}, learnIm{};
    std::vector<float> genRe, genIm;   // message-thread FIR-generation scratch (8192)

    // correction (message-thread written, UI/state read under spinlock)
    std::array<float, NUM_BINS> correctionCurveDB{};
    std::array<float, FIR_LENGTH> correctionFIR{};
    std::atomic<bool> correctionValid{false};
    bool lastMinimumPhase = true;
    mutable MatchSpinLock correctionLock;

    // FFTs
    FFTr2 fftAnalysis;   // 4096 (learning + linear FIR gen)
    FFTr2 fftGenMin;     // 8192 (min-phase cepstral gen)
    FFTr2 fftConv;       // 256  (convolver)

    // pending cross-thread requests (set on message thread, consumed on audio)
    std::atomic<bool> pendingStartCurrent{false}, pendingStartReference{false};
    std::atomic<bool> pendingStop{false}, pendingClear{false};
    std::atomic<bool> pendingActivate{false};   // set by computeCorrection/deserialize

    // convolver
    IRSpectra irSpectra[2];
    std::atomic<int> irActive{-1};      // published slot (>=0) or -1 = passthrough
    int irWriteToggle = 0;              // message-thread next-slot picker
    ChannelConv convL, convR;
    int hopFill = 0;                    // shared 0..CONV_HOP sample counter
    std::array<float, CONV_FFT> convScratchRe{}, convScratchIm{}; // hop FFT scratch
    std::array<float, CONV_FFT> convYRe{}, convYIm{};             // hop accum scratch
    enum class ConvState { Inactive, Active, Clearing };
    std::atomic<ConvState> convState{ConvState::Inactive};
    LinearSmoothedValue wetGain;        // dry↔wet crossfade (10 ms)
};

} // namespace duskaudio
