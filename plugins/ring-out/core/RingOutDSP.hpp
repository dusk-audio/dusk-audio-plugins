// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutDSP.hpp — the framework-free Ring Out engine: a bank of up to twenty
// notch filters, a feedback detector that places and deepens them, the input
// analyser the display draws, and the I/O meters.
//
// Threads
// -------
// processBlock() runs on the audio thread and never blocks: it TRY-locks the
// shared filter table (DuskSpinLock) and otherwise keeps using its last copy,
// publishes the spectrum through a seqlock and the meters through atomics. The
// host/UI side (setTable, applyEdit, getTable) takes the same spinlock for
// real, which only ever waits on a few hundred bytes of copying. The setters a
// host may call from its process callback (parameters, RESET, SETUP, ADD) are
// atomics or try-locks with a deferred fallback, never a wait.
//
// The filter table is owned HERE, not by the host: the detection engine adds
// filters from the audio thread, so the plugin wrapper reads the table back
// (getTable) when the host saves state rather than being told about each change.
//
// Signal path
// -----------
//   in -> [input meter] -> notch bank (shared coefficients, per-channel state)
//      -> output gain -> [bypass crossfade] -> [output meter] -> out
//   mono sum of the input -> analysis ring -> FFT (Hann) -> spectrum + detector
//
// Every table row carries a stable id. Filter SLOTS follow ids, not row
// numbers: a slot keeps serving its filter wherever the row moves (a DEL
// closes the gap), a filter that leaves the table fades out in its slot, and a
// new row ramps in from flat in a free one. There are twice as many slots as
// rows so even a whole-table replacement crossfades cleanly. Bypass runs the
// filters warm and crossfades to a bit-exact dry path over about 30 ms.
//
// Detection (see RingOutDSP.cpp, Detector) follows the standard acoustic
// feedback criteria: a spectral peak that stands far above the frame average
// (PAPR) and its own neighbourhood (PNPR), has no harmonics (PHPR) and is
// neither a harmonic nor a partial of a series, persists at the same frequency
// for several frames, and does not decay. SENSE Low/High selects two threshold
// sets. A qualifying tone gets a notch at the interpolated peak frequency; a
// tone that keeps ringing through an existing notch deepens it in 3 dB steps to
// -20 dB, then widens it to Q 0.7.
#pragma once

#include "RingOutFilterTable.hpp"

#include "DuskFft.hpp"
#include "DuskFilters.hpp"
#include "DuskSeqLock.hpp"
#include "DuskSmoothed.hpp"
#include "DuskSpinLock.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace duskaudio
{

// The most recent analysis frame, in dBFS (a full-scale sine reads 0 dB at its
// bin). Trivially copyable on purpose: it travels through a SeqLock.
struct RingOutSpectrumFrame
{
    static constexpr int kMaxBins = 16384 / 2 + 1;
    int   bins  = 0;          // valid entries in db[] (fftSize/2 + 1)
    float binHz = 0.0f;
    float db[kMaxBins];
};

class RingOutDSP
{
public:
    enum Sense { kSenseLow = 0, kSenseHigh = 1 };

    struct Status
    {
        bool     setupActive = false;
        float    setupRemainingSeconds = 0.0f;
        bool     setupExpired = false;    // the minute ran out since SETUP was last raised
        bool     addSearching = false;
        bool     addSatisfied = false;    // ADD found its filter; release and press again
        bool     tableFull = false;       // the engine wanted a filter and had no row for it
        int      lastEngagedRow = -1;     // table row the engine last placed or deepened
        unsigned engagementCount = 0;
        unsigned tableVersion = 0;
    };

    static constexpr int kMaxFftSize = 16384;
    static constexpr int kMaxChannels = 2;
    // Meters and spectrum never read below this; it is also the floor the
    // output parameters declare.
    static constexpr float kDbFloor = -120.0f;

    // 4096 points up to 50 kHz, 8192 to 100 kHz, 16384 above: the bin width stays
    // near 11.7 Hz and the frame near 85 ms at every supported rate, so the
    // detector's thresholds mean the same thing everywhere.
    static int fftSizeForRate(double sampleRate) noexcept;

    RingOutDSP();

    // Not real-time safe: allocates the analysis buffers for the rate. A SETUP
    // or ADD countdown already running is rescaled to the new rate.
    void prepare(double sampleRate, int maxBlockSize);

    // Clears filter state, meters, analysis history and the detector; keeps the
    // table and the parameters. Real-time safe.
    void reset() noexcept;

    // numChannels is 1 or 2. inputs may alias outputs.
    void processBlock(const float* const* inputs, float* const* outputs,
                      int numChannels, int numSamples) noexcept;

    //--- parameters (any thread, including a host's process callback) ----------
    void setSense(int sense) noexcept        { sense_.store(sense != 0 ? 1 : 0, std::memory_order_relaxed); }
    void setGlobalQ(float q) noexcept        { globalQ_.store(ringout::clampf(q, ringout::kGlobalQMin, ringout::kGlobalQMax), std::memory_order_relaxed); }
    void setGlobalAmpDb(float db) noexcept   { globalAmp_.store(ringout::clampf(db, ringout::kGlobalAmpMin, ringout::kGlobalAmpMax), std::memory_order_relaxed); }
    void setGainOutDb(float db) noexcept     { gainOutDb_.store(ringout::clampf(db, ringout::kGainOutMin, ringout::kGainOutMax), std::memory_order_relaxed); }
    void setBypass(bool b) noexcept          { bypass_.store(b, std::memory_order_relaxed); }

    // Edge-driven: false -> true arms the engine for kSetupSeconds; false stops
    // it. When the minute runs out the engine disarms itself, and the next true
    // is a fresh edge again.
    void setSetup(bool on) noexcept;

    // true starts a search that ends with the first filter engaged, with false
    // (the editor releasing ADD), or after kAddSeconds for a search nobody
    // ends. Any of those leaves the next true a fresh start.
    void setAdd(bool held) noexcept;

    // RESET: remove every filter. Any thread; never blocks (a contended lock
    // defers the clear to the next audio block).
    void resetFilters() noexcept;

    int   sense() const noexcept        { return sense_.load(std::memory_order_relaxed); }
    float globalQ() const noexcept      { return globalQ_.load(std::memory_order_relaxed); }
    float globalAmpDb() const noexcept  { return globalAmp_.load(std::memory_order_relaxed); }
    float gainOutDb() const noexcept    { return gainOutDb_.load(std::memory_order_relaxed); }
    bool  bypassed() const noexcept     { return bypass_.load(std::memory_order_relaxed); }
    bool  setupActive() const noexcept  { return setupActive_.load(std::memory_order_acquire); }
    bool  addSearching() const noexcept { return addSearching_.load(std::memory_order_acquire); }

    //--- the filter table (host / UI threads; spin-locks) ----------------------
    void getTable(ringout::FilterTable& out) const noexcept;
    // Whole-table replacement. Rows that are the same filter as before (same
    // frequency within the slot-identity tolerance) keep their identity, so a
    // preset load crossfades only what changed.
    void setTable(const ringout::FilterTable& table) noexcept;
    // Returns the slot touched, or -1 when the command changed nothing.
    int  applyEdit(const ringout::EditCommand& command) noexcept;
    unsigned tableVersion() const noexcept { return tableVersion_.load(std::memory_order_acquire); }

    //--- read-outs for the UI (any thread) -------------------------------------
    Status status() const noexcept;
    float  inputPeakDb(int channel) const noexcept  { return inPeakDb_[channel & 1].load(std::memory_order_relaxed); }
    float  outputPeakDb(int channel) const noexcept { return outPeakDb_[channel & 1].load(std::memory_order_relaxed); }

    // Copies only the bins in use for the current FFT size, not the frame's
    // worst-case payload.
    bool     copySpectrum(RingOutSpectrumFrame& out) const noexcept { return spectrum_.load(out, spectrumBytes()); }
    unsigned spectrumSequence() const noexcept                       { return spectrum_.sequence(); }

    double sampleRate() const noexcept { return sampleRate_; }
    int    fftSize() const noexcept    { return fftSize_; }
    float  binHz() const noexcept      { return binHz_; }

    //--- pure helpers shared with the UI --------------------------------------
    static float effectiveCutDb(float cutDb, float globalAmpDb) noexcept
    {
        return ringout::clampf(cutDb + globalAmpDb, ringout::kEffectiveCutMin, ringout::kEffectiveCutMax);
    }
    static float effectiveQ(float q, float globalQ) noexcept
    {
        return ringout::clampf(q * globalQ, 0.1f, 200.0f);
    }
    // Composite magnitude response of the on filters, in dB: what the UI draws
    // and what the tests compare the processed signal against.
    static double responseDb(const ringout::FilterTable& table, float globalQ, float globalAmpDb,
                             double sampleRate, double freqHz) noexcept;

private:
    static constexpr int kSubBlock = 32;          // coefficient / smoother update grid
    static constexpr int kNumSlots = 2 * ringout::kMaxFilters;   // a full replacement crossfades
    static constexpr int kMaxTracks = 24;
    static constexpr int kMaxHoldoffs = 32;
    static constexpr float kSameFilterLogTol = 0.05f;  // ~5 %: setTable() keeps the row's identity

    struct SlotState
    {
        Biquad        bq[kMaxChannels];
        SmoothedValue cutDb, logFreq, q;
        bool          engaged = false;    // serving a filter, or fading one out
        uint32_t      rowId = 0;          // identity of the filter served (0 = none)
        int           entry = -1;         // its row in live_ this block; -1 while fading out
        bool          active = false;     // coefficients are not identity this sub-block
        float         lastCut = 0.0f, lastLogF = 0.0f, lastQ = 0.0f;
        int           cooldownFrames = 0; // frames before the engine may touch it again
    };

    struct Track
    {
        float freq = 0.0f, lastDb = -200.0f, firstDb = -200.0f, maxDb = -200.0f;
        int   frames = 0;
        bool  seen = false;
    };

    struct Holdoff { float freq = 0.0f; int frames = 0; };

    struct Thresholds
    {
        float papr, pnpr, phpr, sub, minDb, dropTolDb;
        int   frames;
    };
    static const Thresholds& thresholdsFor(int sense) noexcept;

    // Table mutators; the caller holds tableLock_.
    void clearLocked() noexcept;
    int  addLocked(const ringout::Filter& x) noexcept;
    int  applyEditLocked(const ringout::EditCommand& command) noexcept;
    void replaceLocked(const ringout::FilterTable& table) noexcept;

    void syncTableFromShared() noexcept;
    void adoptLiveTable() noexcept;
    void assignSlots() noexcept;
    int  slotForRow(int row) const noexcept;
    // Bytes of RingOutSpectrumFrame in use at the current FFT size.
    size_t spectrumBytes() const noexcept
    {
        return offsetof(RingOutSpectrumFrame, db) + (size_t)(fftSize_ / 2 + 1) * sizeof(float);
    }
    void updateSmoothersAndCoefficients() noexcept;
    void pushAnalysis(const float* const* inputs, int numChannels, int offset, int count) noexcept;
    void analyzeFrame() noexcept;
    void detect(const float* db, const Thresholds& t) noexcept;
    // Returns false only when the table lock was busy and the track should be
    // kept for the next frame; true means the tone was dealt with (or
    // deliberately left alone) and the track can go.
    bool engage(const Track& track) noexcept;
    bool engineActive() const noexcept;
    void clearTracks() noexcept;
    void addHoldoff(float freq) noexcept;

    double sampleRate_ = 48000.0;
    int    fftSize_ = 4096;
    int    hop_ = 1024;
    float  binHz_ = 48000.0f / 4096.0f;
    int    rangeLo_ = 2, rangeHi_ = 2047;   // bins analysed: 24 Hz .. 20 kHz (or 0.45 fs)

    //--- parameters
    std::atomic<int>   sense_ { kSenseLow };
    std::atomic<float> globalQ_ { ringout::kGlobalQDefault };
    std::atomic<float> globalAmp_ { ringout::kGlobalAmpDefault };
    std::atomic<float> gainOutDb_ { ringout::kGainOutDefault };
    std::atomic<bool>  bypass_ { false };

    //--- engine state
    std::atomic<bool>  setupRequested_ { false };
    std::atomic<bool>  setupActive_ { false };
    std::atomic<bool>  setupExpired_ { false };
    std::atomic<int>   setupSamplesLeft_ { 0 };
    std::atomic<bool>  addHeld_ { false };
    std::atomic<bool>  addSearching_ { false };
    std::atomic<bool>  addSatisfied_ { false };
    std::atomic<int>   addSamplesLeft_ { 0 };
    std::atomic<bool>  tableFull_ { false };
    std::atomic<int>   lastEngagedRow_ { -1 };
    std::atomic<unsigned> engagementCount_ { 0 };

    //--- table: shared (locked) + the audio thread's copy, each with row ids
    mutable SpinLock      tableLock_;
    ringout::FilterTable  shared_;
    uint32_t              sharedIds_[ringout::kMaxFilters] = {};
    uint32_t              nextRowId_ = 1;
    std::atomic<unsigned> tableVersion_ { 1 };
    std::atomic<bool>     resetRequested_ { false };
    ringout::FilterTable  live_;
    uint32_t              liveIds_[ringout::kMaxFilters] = {};
    unsigned              liveVersion_ = 0;
    bool                  snapOnAdopt_ = true;   // first adoption after reset(): no ramp-in

    //--- audio-thread filter state
    SlotState     slots_[kNumSlots];
    SmoothedValue gain_;
    float         gainNow_ = 1.0f;
    SmoothedValue bypassMix_;                 // 1 = processed, 0 = dry
    float         mixNow_ = 1.0f;
    float         wet_[kMaxChannels][kSubBlock] = {};
    int           subPos_ = 0;

    //--- analysis
    FFTr2              fft_;
    std::vector<float> ring_, window_, re_, im_;
    int                ringPos_ = 0;
    int                hopCount_ = 0;
    unsigned           framesSinceReset_ = 0;
    SeqLock<RingOutSpectrumFrame> spectrum_;
    RingOutSpectrumFrame          frame_;     // the frame being built, then published

    //--- detector
    Track   tracks_[kMaxTracks];
    int     numTracks_ = 0;
    Holdoff holdoffs_[kMaxHoldoffs];
    int     numHoldoffs_ = 0;

    //--- meters (dB, decaying peak)
    float              inPeakLin_[kMaxChannels] = { 0.0f, 0.0f };
    float              outPeakLin_[kMaxChannels] = { 0.0f, 0.0f };
    std::atomic<float> inPeakDb_[kMaxChannels];
    std::atomic<float> outPeakDb_[kMaxChannels];
};

} // namespace duskaudio
