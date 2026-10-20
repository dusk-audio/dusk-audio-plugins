// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// MultiQMatch.hpp — framework-free spectrum-match EQ core (zero JUCE/DAF).
//
// Line-for-line port of plugins/multi-q/EQMatchProcessor.h (Logic-Pro-style Match
// EQ): Welch-method spectrum capture (learn current + reference), correction-curve
// computation (reference/current, semitone smoothing, boost/cut limiting) and FIR
// generation (minimum-phase cepstral or linear-phase). juce::dsp::FFT is replaced
// by a self-contained radix-2 complex FFT (FFTr2); juce types by std; the
// juce::SpinLock cross-thread transfer by owned snapshots and a bounded mailbox.
//
// This class ALSO owns the FIR-correction convolution (JUCE used juce::dsp::
// Convolution): an RT-safe uniformly-partitioned overlap-save FFT convolver with a
// lock-free impulse-response handoff and an aligned dry/wet crossfade.
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
#include "../../shared-daf/dsp/DuskFft.hpp"  // duskaudio::FFTr2 (promoted from here)

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace duskaudio
{

// Single writer, any number of readers. Readers receive an owned snapshot: an
// audio writer must never reuse storage while a UI/state reader copies it.
// Writer reservations and reader pins share one atomic word per slot, so a
// reader that chose an old publication cannot overlap that slot's reuse.
// The writer never waits. If all spare slots are pinned, skip that display
// publication and retain the previous complete frame. Once readers quiesce,
// the next publication includes all accumulated learning, not just its delta.
template<typename T>
class MatchSnapshot
{
    friend struct MatchSnapshotTestAccess;
    static_assert(std::is_trivially_copyable<T>::value, "Snapshots require nonthrowing plain data copies");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Snapshot publication cannot lock");
    static constexpr uint32_t kWriting = uint32_t{1} << 31;
    struct Slot
    {
        T value{};
        mutable std::atomic<uint32_t> users{0};
    };
    T pending_{}; // exclusively writer-owned
    std::array<Slot, 3> slots_{};
    std::atomic<uint32_t> published_{0};
    std::atomic<bool> cleared_{true};

public:
    T& writeBuffer() noexcept { return pending_; }
    // Clear is not a droppable display frame: stopped readers must not retain
    // valid learned data after reset/start-learning/clear on the audio thread.
    void clear() noexcept
    {
        pending_ = T{};
        cleared_.store(true, std::memory_order_release);
    }

    void publish() noexcept
    {
        const uint32_t current = published_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < slots_.size(); ++i)
        {
            if (i == current) continue;
            uint32_t unused = 0;
            if (!slots_[i].users.compare_exchange_strong(
                    unused, kWriting, std::memory_order_acquire, std::memory_order_relaxed))
                continue;
            slots_[i].value = pending_;
            // Unlock before publishing: a reader never waits for a parked
            // writer that has made its still-locked slot the current one.
            slots_[i].users.store(0, std::memory_order_release);
            published_.store(i, std::memory_order_release);
            cleared_.store(false, std::memory_order_release);
            return;
        }
    }

    // Non-realtime readers only. A retry follows a moving publication; a
    // parked writer cannot hold the published slot's reservation.
    T readBuffer() const noexcept
    {
        for (;;)
        {
            if (cleared_.load(std::memory_order_acquire)) return T{};
            const uint32_t index = published_.load(std::memory_order_acquire);
            const auto& slot = slots_[index];
            uint32_t users = slot.users.load(std::memory_order_relaxed);
            if (users >= kWriting - 1) continue;
            if (!slot.users.compare_exchange_weak(
                    users, users + 1, std::memory_order_acquire, std::memory_order_relaxed))
                continue;
            const T result = slot.value;
            slot.users.fetch_sub(1, std::memory_order_release);
            return result;
        }
    }
};

// Single producer / single consumer, latest complete value wins. Each side
// owns one slot; an atomic exchange transfers the spare slot and its ownership.
// Neither side can reuse the other's slot, even if that thread is parked.
// The consumer makes one bounded exchange at a block boundary and retains its
// returned value until the next consume(). reset() requires both sides stopped.
template<typename T>
class MatchMailbox
{
    static_assert(std::is_trivially_copyable<T>::value, "Mailbox values require plain data copies");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "Mailbox publication cannot lock");
    static constexpr uint32_t kDirty = 4;
    std::array<T, 3> slots_{};
    uint32_t writing_ = 2; // producer only
    uint32_t reading_ = 0; // consumer only
    std::atomic<uint32_t> middle_{1};
public:
    T& writeBuffer() noexcept { return slots_[writing_]; }
    void publish() noexcept
    {
        writing_ = middle_.exchange(writing_ | kDirty, std::memory_order_acq_rel) & ~kDirty;
    }
    const T* consume() noexcept
    {
        if (!(middle_.load(std::memory_order_acquire) & kDirty)) return nullptr;
        reading_ = middle_.exchange(reading_, std::memory_order_acq_rel) & ~kDirty;
        return &slots_[reading_];
    }
    void reset() noexcept
    {
        for (auto& slot : slots_) slot = T{};
        writing_ = 2; reading_ = 0;
        middle_.store(1, std::memory_order_relaxed);
    }
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

// FFTr2 lived here until it was promoted VERBATIM to the shared tree
// (plugins/shared-daf/dsp/DuskFft.hpp) so the Spectrum Analyzer 2 port can
// build on the same DSP-grade FFT. Same namespace, same name, same arithmetic;
// the A/B harness gates that nothing moved.

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

    // Immutable partition spectra handed from the message thread to audio.
    struct IRSpectra
    {
        std::array<std::array<float, CONV_FFT>, NUM_PART> re{};
        std::array<std::array<float, CONV_FFT>, NUM_PART> im{};
    };

    struct IRUpdate
    {
        IRSpectra spectra;
        bool active = false;
        bool minimumPhase = true;
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
    MatchSnapshot<LearnedSpectrum> currentSpectrumBuf, referenceSpectrumBuf;
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

    // convolver
    MatchMailbox<IRUpdate> irUpdates;
    IRSpectra activeIR;                 // exclusively audio-owned, including fade-out
    std::atomic<int> activeLatency{0};  // metadata adopted with the audio-owned IR
    ChannelConv convL, convR;
    int hopFill = 0;                    // shared 0..CONV_HOP sample counter
    std::array<float, CONV_FFT> convScratchRe{}, convScratchIm{}; // hop FFT scratch
    std::array<float, CONV_FFT> convYRe{}, convYIm{};             // hop accum scratch
    enum class ConvState { Inactive, Active, Clearing };
    std::atomic<ConvState> convState{ConvState::Inactive};
    LinearSmoothedValue wetGain;        // dry↔wet crossfade (10 ms)
};

} // namespace duskaudio
