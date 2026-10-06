// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutDSP.cpp — see RingOutDSP.hpp.

#include "RingOutDSP.hpp"

#include "DuskDenormals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace duskaudio
{

namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kLn2 = 0.69314718056f;
    constexpr float kDbFloorLin = 1.0e-6f;   // = RingOutDSP::kDbFloor (-120 dB)

    inline float linToDb(float lin) noexcept
    {
        return 20.0f * std::log10(lin > kDbFloorLin ? lin : kDbFloorLin);
    }

    // Half the -3 dB bandwidth of an RBJ peaking section, in octaves.
    inline float halfBandwidthOctaves(float q) noexcept
    {
        q = q > 1.0e-3f ? q : 1.0e-3f;
        return (1.0f / kLn2) * std::asinh(0.5f / q);
    }

    // Decaying peak: a 300 ms release, measured per block.
    inline float decayPeak(float current, float blockPeak, int numSamples, double sampleRate) noexcept
    {
        const float decay = std::exp(-(float)numSamples / (0.3f * (float)sampleRate));
        const float faded = current * decay;
        return blockPeak > faded ? blockPeak : faded;
    }

    // Deepen a filter that a tone still rings through: 3 dB at a time to the
    // floor, then wider, a fifth at a time, to the widest the engine will go.
    inline void deepen(ringout::Filter& x) noexcept
    {
        if (x.cutDb > ringout::kAutoCutFloor + 0.05f)
            x.cutDb = ringout::snapCut(std::max(ringout::kAutoCutFloor, x.cutDb - 3.0f));
        else
            x.q = ringout::snapQ(std::max(ringout::kAutoQFloor, x.q * 0.8f));
    }

    inline int rescaleSamples(int samples, double oldRate, double newRate) noexcept
    {
        if (samples <= 0 || oldRate <= 0.0) return samples;
        return (int)((double)samples * newRate / oldRate + 0.5);
    }
}

//==============================================================================
// thresholds
//
// Low is the setting the reference's users prefer for ringing out: it needs the
// tone to stand clearly above everything, have no harmonics, and persist for
// about 130 ms. High trades false positives for speed: lower margins and a
// shorter persistence, for a system that rings only briefly before someone pulls
// the fader.
const RingOutDSP::Thresholds& RingOutDSP::thresholdsFor(int sense) noexcept
{
    //                                  papr   pnpr   phpr   sub   minDb  drop  frames
    static const Thresholds kLow  = { 24.0f, 16.0f, 16.0f, 8.0f, -62.0f, 2.0f, 6 };
    static const Thresholds kHigh = { 16.0f, 10.0f, 10.0f, 4.0f, -72.0f, 3.0f, 4 };
    return sense == kSenseHigh ? kHigh : kLow;
}

int RingOutDSP::fftSizeForRate(double sampleRate) noexcept
{
    if (sampleRate <= 50000.0)  return 4096;
    if (sampleRate <= 100000.0) return 8192;
    return kMaxFftSize;
}

RingOutDSP::RingOutDSP()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        inPeakDb_[ch].store(kDbFloor, std::memory_order_relaxed);
        outPeakDb_[ch].store(kDbFloor, std::memory_order_relaxed);
    }
    prepare(48000.0, 512);
}

void RingOutDSP::prepare(double sampleRate, int /*maxBlockSize*/)
{
    const double oldRate = sampleRate_;
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    fftSize_ = fftSizeForRate(sampleRate_);
    hop_ = fftSize_ / 4;
    binHz_ = (float)(sampleRate_ / (double)fftSize_);

    // A countdown armed before activation was measured at the old rate (the
    // constructor's 48 kHz when a session is restored before the first
    // activate()); keep its wall-clock length.
    setupSamplesLeft_.store(rescaleSamples(setupSamplesLeft_.load(std::memory_order_relaxed), oldRate, sampleRate_),
                            std::memory_order_relaxed);
    addSamplesLeft_.store(rescaleSamples(addSamplesLeft_.load(std::memory_order_relaxed), oldRate, sampleRate_),
                          std::memory_order_relaxed);

    fft_.prepare(fftSize_);
    ring_.assign((size_t)fftSize_, 0.0f);
    re_.assign((size_t)fftSize_, 0.0f);
    im_.assign((size_t)fftSize_, 0.0f);
    window_.resize((size_t)fftSize_);
    for (int i = 0; i < fftSize_; ++i)
        window_[(size_t)i] = 0.5f - 0.5f * std::cos(2.0f * kPi * (float)i / (float)fftSize_);

    // 24 Hz is bin 2 at every supported rate (the bin width is held near
    // 11.7 Hz), and the detector's local-maximum test needs two bins below the
    // scan start, so the band begins exactly there. Never above 0.45 fs so the
    // harmonic checks have room.
    const float topHz = std::min(ringout::kFreqMax, 0.45f * (float)sampleRate_);
    rangeLo_ = std::max(2, (int)std::floor(ringout::kFreqMin / binHz_));
    rangeHi_ = std::min(fftSize_ / 2 - 1, (int)std::floor(topHz / binHz_));

    const double subRate = sampleRate_ / (double)kSubBlock;
    for (SlotState& st : slots_)
    {
        st.cutDb.prepare(subRate, 0.015f);
        st.logFreq.prepare(subRate, 0.010f);
        st.q.prepare(subRate, 0.010f);
    }
    gain_.prepare(subRate, 0.010f);
    bypassMix_.prepare(subRate, 0.010f);

    reset();
}

void RingOutDSP::reset() noexcept
{
    for (SlotState& st : slots_)
    {
        for (Biquad& b : st.bq) { b.reset(); b.setCoeffs(BiquadCoeffs()); }
        st.cutDb.snap(0.0f);
        st.logFreq.snap(std::log(ringout::kFreqDefault));
        st.q.snap(ringout::kQDefault);
        st.engaged = st.active = false;
        st.rowId = 0;
        st.entry = -1;
        st.lastCut = 0.0f; st.lastLogF = 0.0f; st.lastQ = 0.0f;
        st.cooldownFrames = 0;
    }
    gainNow_ = std::pow(10.0f, gainOutDb_.load(std::memory_order_relaxed) * 0.05f);
    gain_.snap(gainNow_);
    mixNow_ = bypass_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    bypassMix_.snap(mixNow_);
    subPos_ = 0;

    std::fill(ring_.begin(), ring_.end(), 0.0f);
    ringPos_ = 0;
    hopCount_ = 0;
    framesSinceReset_ = 0;
    clearTracks();
    numHoldoffs_ = 0;

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        inPeakLin_[ch] = outPeakLin_[ch] = 0.0f;
        inPeakDb_[ch].store(kDbFloor, std::memory_order_relaxed);
        outPeakDb_[ch].store(kDbFloor, std::memory_order_relaxed);
    }

    // Force a fresh copy of the table on the next block.
    liveVersion_ = 0;
}

//==============================================================================
// engine controls

void RingOutDSP::setSetup(bool on) noexcept
{
    const bool was = setupRequested_.exchange(on, std::memory_order_acq_rel);
    if (on && !was)
    {
        setupSamplesLeft_.store((int)(ringout::kSetupSeconds * sampleRate_ + 0.5), std::memory_order_relaxed);
        setupExpired_.store(false, std::memory_order_relaxed);
        setupActive_.store(true, std::memory_order_release);
    }
    else if (!on)
    {
        // Also after an expiry already cleared the request: lowering SETUP
        // always leaves the engine idle with no expiry pending.
        setupActive_.store(false, std::memory_order_release);
        setupExpired_.store(false, std::memory_order_relaxed);
    }
}

void RingOutDSP::setAdd(bool held) noexcept
{
    const bool was = addHeld_.exchange(held, std::memory_order_acq_rel);
    if (held && !was)
    {
        addSamplesLeft_.store((int)(ringout::kSetupSeconds * sampleRate_ + 0.5), std::memory_order_relaxed);
        addSatisfied_.store(false, std::memory_order_relaxed);
        addSearching_.store(true, std::memory_order_release);
    }
    else if (!held && was)
    {
        addSearching_.store(false, std::memory_order_release);
        addSatisfied_.store(false, std::memory_order_relaxed);
    }
}

bool RingOutDSP::engineActive() const noexcept
{
    if (bypass_.load(std::memory_order_relaxed))
        return false;
    return setupActive_.load(std::memory_order_acquire)
        || addSearching_.load(std::memory_order_acquire);
}

RingOutDSP::Status RingOutDSP::status() const noexcept
{
    Status s;
    s.setupActive = setupActive_.load(std::memory_order_acquire);
    s.setupRemainingSeconds = s.setupActive
        ? (float)setupSamplesLeft_.load(std::memory_order_relaxed) / (float)sampleRate_ : 0.0f;
    s.setupExpired = setupExpired_.load(std::memory_order_relaxed);
    s.addSearching = addSearching_.load(std::memory_order_acquire);
    s.addSatisfied = addSatisfied_.load(std::memory_order_relaxed);
    s.tableFull = tableFull_.load(std::memory_order_relaxed);
    s.lastEngagedSlot = lastEngagedSlot_.load(std::memory_order_relaxed);
    s.engagementCount = engagementCount_.load(std::memory_order_relaxed);
    s.tableVersion = tableVersion();
    return s;
}

//==============================================================================
// table mutators (tableLock_ held)

void RingOutDSP::clearLocked() noexcept
{
    shared_.clear();
    for (uint32_t& id : sharedIds_) id = 0;
}

int RingOutDSP::addLocked(const ringout::Filter& x) noexcept
{
    const int row = shared_.add(x);
    if (row >= 0)
        sharedIds_[row] = nextRowId_++;
    return row;
}

int RingOutDSP::applyEditLocked(const ringout::EditCommand& c) noexcept
{
    using ringout::EditCommand;
    switch (c.kind)
    {
    case EditCommand::kClear:
        if (shared_.count == 0) return -1;
        clearLocked();
        return 0;
    case EditCommand::kSet:
        if (c.slot < 0 || c.slot >= shared_.count) return -1;
        shared_.f[c.slot] = c.filter;         // same filter, edited: keeps its id
        return c.slot;
    case EditCommand::kAdd:
        return addLocked(c.filter);
    case EditCommand::kDelete:
        if (c.slot < 0 || c.slot >= shared_.count) return -1;
        for (int k = c.slot; k + 1 < shared_.count; ++k)
            sharedIds_[k] = sharedIds_[k + 1];
        sharedIds_[shared_.count - 1] = 0;
        shared_.remove(c.slot);
        return c.slot;
    default:
        return -1;
    }
}

// Rows that are the same filter as an existing one (same frequency within the
// identity tolerance) keep that filter's id, so only what changed crossfades.
void RingOutDSP::replaceLocked(const ringout::FilterTable& table) noexcept
{
    uint32_t newIds[ringout::kMaxFilters] = {};
    bool oldUsed[ringout::kMaxFilters] = {};
    for (int row = 0; row < table.count; ++row)
    {
        const float lf = std::log(table.f[row].freqHz);
        int best = -1; float bestDist = kSameFilterLogTol;
        for (int old = 0; old < shared_.count; ++old)
        {
            if (oldUsed[old]) continue;
            const float dist = std::fabs(std::log(shared_.f[old].freqHz) - lf);
            if (dist <= bestDist) { best = old; bestDist = dist; }
        }
        if (best >= 0) { oldUsed[best] = true; newIds[row] = sharedIds_[best]; }
        else newIds[row] = nextRowId_++;
    }
    shared_ = table;
    for (int row = 0; row < ringout::kMaxFilters; ++row)
        sharedIds_[row] = row < table.count ? newIds[row] : 0;
}

//==============================================================================
// table access (host / UI side)

void RingOutDSP::getTable(ringout::FilterTable& out) const noexcept
{
    const SpinLock::ScopedLock guard(tableLock_);
    out = shared_;
}

void RingOutDSP::setTable(const ringout::FilterTable& table) noexcept
{
    if (!ringout::tableValid(table))
        return;
    const SpinLock::ScopedLock guard(tableLock_);
    if (ringout::tablesEqual(shared_, table))
        return;
    replaceLocked(table);
    tableVersion_.fetch_add(1, std::memory_order_acq_rel);
}

int RingOutDSP::applyEdit(const ringout::EditCommand& command) noexcept
{
    const SpinLock::ScopedLock guard(tableLock_);
    const int slot = applyEditLocked(command);
    if (slot >= 0)
        tableVersion_.fetch_add(1, std::memory_order_acq_rel);
    return slot;
}

// Hosts deliver a RESET automation event from their process callback, so this
// must not wait: clear now if the lock is free, otherwise leave a request the
// audio thread honours at its next block.
void RingOutDSP::resetFilters() noexcept
{
    {
        const SpinLock::ScopedTryLock guard(tableLock_);
        if (guard.isLocked())
        {
            if (shared_.count != 0)
            {
                clearLocked();
                tableVersion_.fetch_add(1, std::memory_order_acq_rel);
            }
            tableFull_.store(false, std::memory_order_relaxed);
            return;
        }
    }
    resetRequested_.store(true, std::memory_order_release);
}

// Audio thread. A failed try-lock just means another block on the old copy.
void RingOutDSP::syncTableFromShared() noexcept
{
    const bool resetPending = resetRequested_.load(std::memory_order_acquire);
    const unsigned v = tableVersion_.load(std::memory_order_acquire);
    if (v == liveVersion_ && !resetPending)
        return;
    const SpinLock::ScopedTryLock guard(tableLock_);
    if (!guard.isLocked())
        return;
    if (resetPending)
    {
        resetRequested_.store(false, std::memory_order_relaxed);
        if (shared_.count != 0)
        {
            clearLocked();
            tableVersion_.fetch_add(1, std::memory_order_acq_rel);
        }
        tableFull_.store(false, std::memory_order_relaxed);
    }
    adoptLiveTable();
}

// Caller holds tableLock_.
void RingOutDSP::adoptLiveTable() noexcept
{
    live_ = shared_;
    std::memcpy(liveIds_, sharedIds_, sizeof(liveIds_));
    liveVersion_ = tableVersion_.load(std::memory_order_relaxed);
    assignSlots();
}

int RingOutDSP::slotForRow(int row) const noexcept
{
    for (int s = 0; s < kNumSlots; ++s)
        if (slots_[s].engaged && slots_[s].entry == row)
            return s;
    return -1;
}

// Slots follow row ids: a slot keeps the filter it serves wherever the row
// moved, a new id takes a free slot and ramps in, a slot whose id left the
// table fades out.
void RingOutDSP::assignSlots() noexcept
{
    for (SlotState& st : slots_)
        if (st.engaged) st.entry = -1;

    for (int row = 0; row < live_.count; ++row)
    {
        const uint32_t id = liveIds_[row];
        bool found = false;
        for (int s = 0; s < kNumSlots && !found; ++s)
        {
            SlotState& st = slots_[s];
            if (st.engaged && st.rowId == id)
            {
                st.entry = row;
                found = true;
            }
        }
        if (found)
            continue;

        int chosen = -1;
        for (int s = 0; s < kNumSlots && chosen < 0; ++s)
            if (!slots_[s].engaged) chosen = s;
        if (chosen < 0)
        {
            // Every slot busy: twenty filters plus twenty still fading. Cut the
            // fade nearest to silence short.
            float quietest = 1.0e9f;
            for (int s = 0; s < kNumSlots; ++s)
            {
                const SlotState& st = slots_[s];
                if (st.entry >= 0) continue;
                const float depth = std::fabs(st.cutDb.value());
                if (depth < quietest) { quietest = depth; chosen = s; }
            }
        }
        if (chosen < 0)
            break;   // cannot happen: kNumSlots > kMaxFilters
        SlotState& st = slots_[chosen];
        st.engaged = true;
        st.rowId = id;
        st.entry = row;
        st.logFreq.snap(std::log(live_.f[row].freqHz));
        st.q.snap(effectiveQ(live_.f[row].q, globalQ_.load(std::memory_order_relaxed)));
        st.cutDb.snap(0.0f);
        st.cooldownFrames = 0;
        for (Biquad& b : st.bq) b.reset();
    }
}

//==============================================================================
// audio

void RingOutDSP::processBlock(const float* const* inputs, float* const* outputs,
                              int numChannels, int numSamples) noexcept
{
    const ScopedFlushDenormals noDenormals;
    if (numSamples <= 0 || inputs == nullptr || outputs == nullptr)
        return;
    numChannels = numChannels < 1 ? 1 : (numChannels > kMaxChannels ? kMaxChannels : numChannels);

    syncTableFromShared();

    // SETUP and ADD timers. An expired SETUP disarms the request too, so the
    // next 1 the host writes is a rising edge again.
    if (setupActive_.load(std::memory_order_acquire))
    {
        const int left = setupSamplesLeft_.fetch_sub(numSamples, std::memory_order_relaxed) - numSamples;
        if (left <= 0)
        {
            setupActive_.store(false, std::memory_order_release);
            setupRequested_.store(false, std::memory_order_release);
            setupExpired_.store(true, std::memory_order_relaxed);
            clearTracks();
        }
    }
    if (addSearching_.load(std::memory_order_acquire))
    {
        const int left = addSamplesLeft_.fetch_sub(numSamples, std::memory_order_relaxed) - numSamples;
        if (left <= 0)
        {
            addSearching_.store(false, std::memory_order_release);
            clearTracks();
        }
    }

    // Input meter, on the raw input.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float peak = 0.0f;
        const float* x = inputs[ch];
        for (int i = 0; i < numSamples; ++i) { const float a = std::fabs(x[i]); if (a > peak) peak = a; }
        inPeakLin_[ch] = decayPeak(inPeakLin_[ch], peak, numSamples, sampleRate_);
        inPeakDb_[ch].store(linToDb(inPeakLin_[ch]), std::memory_order_relaxed);
    }
    if (numChannels == 1)
        inPeakDb_[1].store(inPeakDb_[0].load(std::memory_order_relaxed), std::memory_order_relaxed);

    int offset = 0;
    while (offset < numSamples)
    {
        int chunk = std::min(numSamples - offset, kSubBlock - subPos_);
        chunk = std::min(chunk, hop_ - hopCount_);

        // Everything time-varying advances on the 32-sample grid, which is
        // aligned to the stream, not to the host's blocks: the output is the
        // same whatever block size the host chooses.
        if (subPos_ == 0)
        {
            updateSmoothersAndCoefficients();
            gain_.setTarget(std::pow(10.0f, gainOutDb_.load(std::memory_order_relaxed) * 0.05f));
            gainNow_ = gain_.next();
            bypassMix_.setTarget(bypass_.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
            mixNow_ = bypassMix_.next();
            if (mixNow_ < 1.0e-4f) mixNow_ = 0.0f;
            if (mixNow_ > 1.0f - 1.0e-4f) mixNow_ = 1.0f;
        }

        // The analyser sees the input whatever the bypass state, so the display
        // keeps working while the processing is out of circuit.
        pushAnalysis(inputs, numChannels, offset, chunk);

        // The wet path is always computed, bypassed or not, so the filter
        // states stay warm and un-bypass has no stale tail to replay. It runs in
        // a scratch buffer because `inputs` may alias `outputs`.
        for (int ch = 0; ch < numChannels; ++ch)
            std::memcpy(wet_[ch], inputs[ch] + offset, (size_t)chunk * sizeof(float));
        for (SlotState& st : slots_)
        {
            if (!st.active)
                continue;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* y = wet_[ch];
                Biquad& b = st.bq[ch];
                for (int i = 0; i < chunk; ++i)
                    y[i] = b.process(y[i]);
            }
        }
        if (std::fabs(gainNow_ - 1.0f) > 1.0e-6f)
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < chunk; ++i)
                    wet_[ch][i] *= gainNow_;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* x = inputs[ch] + offset;
            float* y = outputs[ch] + offset;
            const float* w = wet_[ch];
            if (mixNow_ >= 1.0f)
            {
                std::memcpy(y, w, (size_t)chunk * sizeof(float));
            }
            else if (mixNow_ <= 0.0f)
            {
                // Bit-exact passthrough while bypassed.
                if (y != x)
                    std::memcpy(y, x, (size_t)chunk * sizeof(float));
            }
            else
            {
                for (int i = 0; i < chunk; ++i)
                {
                    const float d = x[i];        // read before the aliased write
                    y[i] = d + mixNow_ * (w[i] - d);
                }
            }
        }

        offset += chunk;
        subPos_ += chunk;
        if (subPos_ >= kSubBlock)
            subPos_ = 0;
        hopCount_ += chunk;
        if (hopCount_ >= hop_)
        {
            hopCount_ = 0;
            analyzeFrame();
        }
    }

    // Output meter.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float peak = 0.0f;
        const float* y = outputs[ch];
        for (int i = 0; i < numSamples; ++i) { const float a = std::fabs(y[i]); if (a > peak) peak = a; }
        outPeakLin_[ch] = decayPeak(outPeakLin_[ch], peak, numSamples, sampleRate_);
        outPeakDb_[ch].store(linToDb(outPeakLin_[ch]), std::memory_order_relaxed);
    }
    if (numChannels == 1)
        outPeakDb_[1].store(outPeakDb_[0].load(std::memory_order_relaxed), std::memory_order_relaxed);
}

// Once per sub-block: move every slot's smoothed cut / frequency / Q toward the
// filter it serves (or toward flat, for a slot fading out), and redesign the
// coefficients of the slots that moved.
void RingOutDSP::updateSmoothersAndCoefficients() noexcept
{
    const float gq  = globalQ_.load(std::memory_order_relaxed);
    const float amp = globalAmp_.load(std::memory_order_relaxed);

    for (SlotState& st : slots_)
    {
        if (!st.engaged)
        {
            st.active = false;
            continue;
        }

        const bool serving = st.entry >= 0 && st.entry < live_.count;
        if (serving)
        {
            const ringout::Filter& f = live_.f[st.entry];
            st.cutDb.setTarget(f.on ? effectiveCutDb(f.cutDb, amp) : 0.0f);
            st.logFreq.setTarget(std::log(f.freqHz));
            st.q.setTarget(effectiveQ(f.q, gq));
        }
        else
            st.cutDb.setTarget(0.0f);       // fade out, then release the slot

        const float cut  = st.cutDb.next();
        const float logF = st.logFreq.next();
        const float q    = st.q.next();

        if (!serving && std::fabs(cut) < 0.01f)
        {
            st.engaged = false;
            st.active = false;
            st.rowId = 0;
            st.entry = -1;
            for (Biquad& b : st.bq) { b.reset(); b.setCoeffs(BiquadCoeffs()); }
            continue;
        }

        const bool moved = std::fabs(cut - st.lastCut) > 1.0e-4f
                        || std::fabs(logF - st.lastLogF) > 1.0e-5f
                        || std::fabs(q - st.lastQ) > 1.0e-4f;
        if (moved)
        {
            const BiquadCoeffs k = Biquad::peak(sampleRate_, std::exp(logF), cut, q);
            for (Biquad& b : st.bq) b.setCoeffs(k);
            st.lastCut = cut; st.lastLogF = logF; st.lastQ = q;
        }
        st.active = std::fabs(cut) > 0.005f;
    }
}

//==============================================================================
// analysis

void RingOutDSP::pushAnalysis(const float* const* inputs, int numChannels, int offset, int count) noexcept
{
    const int mask = fftSize_ - 1;
    if (numChannels == 1)
    {
        const float* x = inputs[0] + offset;
        for (int i = 0; i < count; ++i)
        {
            ring_[(size_t)ringPos_] = x[i];
            ringPos_ = (ringPos_ + 1) & mask;
        }
    }
    else
    {
        const float* l = inputs[0] + offset;
        const float* r = inputs[1] + offset;
        for (int i = 0; i < count; ++i)
        {
            ring_[(size_t)ringPos_] = 0.5f * (l[i] + r[i]);
            ringPos_ = (ringPos_ + 1) & mask;
        }
    }
}

void RingOutDSP::analyzeFrame() noexcept
{
    const int n = fftSize_;
    const int mask = n - 1;
    // Oldest sample first: ringPos_ points at the oldest slot.
    for (int i = 0; i < n; ++i)
    {
        re_[(size_t)i] = ring_[(size_t)((ringPos_ + i) & mask)] * window_[(size_t)i];
        im_[(size_t)i] = 0.0f;
    }
    fft_.forward(re_.data(), im_.data());

    // Hann sums to N/2, so a full-scale sine lands at 0 dB with 4/N. The dB
    // spectrum is written once, straight into the frame that is published and
    // then read by the detector.
    const float norm = 4.0f / (float)n;
    const int half = n / 2;
    float* const db = frame_.db;
    for (int k = 0; k <= half; ++k)
    {
        const float mag = std::sqrt(re_[(size_t)k] * re_[(size_t)k] + im_[(size_t)k] * im_[(size_t)k]) * norm;
        db[k] = linToDb(mag);
    }
    frame_.bins = half + 1;
    frame_.binHz = binHz_;
    spectrum_.store(frame_);

    ++framesSinceReset_;

    for (int i = 0; i < numHoldoffs_; )
    {
        if (--holdoffs_[i].frames <= 0)
            holdoffs_[i] = holdoffs_[--numHoldoffs_];
        else
            ++i;
    }
    for (SlotState& st : slots_)
        if (st.cooldownFrames > 0) --st.cooldownFrames;

    // The ring must hold a full frame of real signal before a peak means anything.
    if (!engineActive() || framesSinceReset_ < (unsigned)(n / hop_))
    {
        clearTracks();
        return;
    }
    detect(db, thresholdsFor(sense_.load(std::memory_order_relaxed)));
}

void RingOutDSP::clearTracks() noexcept
{
    numTracks_ = 0;
}

void RingOutDSP::addHoldoff(float freq) noexcept
{
    if (numHoldoffs_ < kMaxHoldoffs)
        holdoffs_[numHoldoffs_++] = Holdoff{ freq, 8 };
}

void RingOutDSP::detect(const float* db, const Thresholds& t) noexcept
{
    // Frame average over the analysed range, in dB (the PAPR reference).
    double sum = 0.0;
    for (int k = rangeLo_; k <= rangeHi_; ++k) sum += db[k];
    const float meanDb = (float)(sum / (double)(rangeHi_ - rangeLo_ + 1));

    for (int i = 0; i < numTracks_; ++i) tracks_[i].seen = false;

    const int half = fftSize_ / 2;
    // The local-maximum test looks two bins each side; rangeLo_ is at least 2.
    const int lo = rangeLo_;
    const int hi = std::min(rangeHi_, half - 2);
    for (int k = lo; k <= hi; ++k)
    {
        const float p = db[k];
        if (p < t.minDb) continue;
        if (!(p > db[k - 1] && p >= db[k + 1] && p >= db[k - 2] && p >= db[k + 2])) continue;

        // Parabolic interpolation on the log-magnitude: sub-bin frequency and
        // the true peak height.
        const float a = db[k - 1], b = p, c = db[k + 1];
        const float denom = a - 2.0f * b + c;
        const float delta = std::fabs(denom) > 1.0e-6f ? 0.5f * (a - c) / denom : 0.0f;
        const float peakDb = b - 0.25f * (a - c) * delta;
        const float freq = ((float)k + delta) * binHz_;

        // Already handled: let the new notch act before judging again.
        bool held = false;
        for (int h = 0; h < numHoldoffs_ && !held; ++h)
            if (std::fabs(holdoffs_[h].freq - freq) <= std::max(2.0f * binHz_, 0.01f * freq))
                held = true;
        if (held) continue;

        // PAPR: above the frame average.
        if (peakDb - meanDb < t.papr) continue;

        // PNPR: above its own neighbourhood, outside the Hann main lobe (+-2
        // bins). Near the band edges only the bins that exist are consulted.
        float neighbour = -200.0f;
        for (int d = 3; d <= 8; ++d)
        {
            if (k - d >= 1)    neighbour = std::max(neighbour, db[k - d]);
            if (k + d <= half) neighbour = std::max(neighbour, db[k + d]);
        }
        if (peakDb - neighbour < t.pnpr) continue;

        // Level near a frequency (the bin and its two neighbours), ignoring
        // bins inside the candidate's own Hann main lobe: at the bottom of the
        // band a 30 Hz tone's 2f lands two bins away, where the window is still
        // showing the fundamental's leakage, not a harmonic. Past the band
        // counts as absent.
        const float centreBin = (float)k + delta;
        const auto levelNear = [&](float hz) -> float
        {
            const int kb = (int)std::lround(hz / binHz_);
            if (kb - 1 < rangeLo_ || kb + 1 > rangeHi_) return -200.0f;
            float m = -200.0f;
            for (int d = -1; d <= 1; ++d)
                if (std::fabs((float)(kb + d) - centreBin) > 2.5f)
                    m = std::max(m, db[kb + d]);
            return m;
        };

        // PHPR: no second or third harmonic.
        bool harmonicRich = false;
        for (int h = 2; h <= 3 && !harmonicRich; ++h)
            if (peakDb - levelNear(freq * (float)h) < t.phpr) harmonicRich = true;
        if (harmonicRich) continue;

        // Not itself the harmonic of something louder below it.
        if (peakDb - levelNear(0.5f * freq) < t.sub) continue;

        // Not a partial of a harmonic series. A note's high partials can sit
        // where their own 2f and 3f fall above the band, so the two checks
        // above alone pass them; a partial's fundamental is below it, though,
        // and so are its neighbours in the series. Look for a fundamental f/k
        // that is present together with one other member of the same series,
        // all within 15 dB of the candidate.
        const auto binMax = levelNear;
        bool partialOfSeries = false;
        for (int kk = 2; kk <= 100 && !partialOfSeries; ++kk)
        {
            const float f0 = freq / (float)kk;
            if (f0 < (float)rangeLo_ * binHz_) break;
            if (binMax(f0) < peakDb - 15.0f) continue;
            const int others[4] = { 2, 3, kk - 1, kk + 1 };
            for (const int m : others)
            {
                if (m == kk || m < 2) continue;
                if (binMax((float)m * f0) >= peakDb - 15.0f) { partialOfSeries = true; break; }
            }
        }
        if (partialOfSeries) continue;

        // Candidate. Persistence: match a track at (nearly) the same frequency.
        const float tol = std::max(binHz_, 0.005f * freq);
        int best = -1; float bestDist = 1.0e9f;
        for (int i = 0; i < numTracks_; ++i)
        {
            if (tracks_[i].seen) continue;
            const float dist = std::fabs(tracks_[i].freq - freq);
            if (dist <= tol && dist < bestDist) { best = i; bestDist = dist; }
        }
        if (best >= 0)
        {
            Track& tr = tracks_[best];
            // Feedback grows or holds; a note decays. Judge against the loudest
            // the track has been, not the previous frame, or a slow decay of a
            // decibel per frame counts as "holding" for ever.
            if (peakDb >= tr.maxDb - t.dropTolDb)
                ++tr.frames;                 // still ringing, or louder
            else
            {
                tr.frames = 1;               // it decayed: a note, not a loop
                tr.firstDb = peakDb;
                tr.maxDb = peakDb;
            }
            tr.freq = 0.7f * tr.freq + 0.3f * freq;
            tr.lastDb = peakDb;
            tr.maxDb = std::max(tr.maxDb, peakDb);
            tr.seen = true;
        }
        else
        {
            int slot = numTracks_;
            if (slot >= kMaxTracks)
            {
                // Replace the youngest track that has not been matched this
                // frame; a live match must not be overwritten by a later
                // candidate in the same scan.
                slot = -1;
                for (int i = 0; i < kMaxTracks; ++i)
                    if (!tracks_[i].seen && (slot < 0 || tracks_[i].frames < tracks_[slot].frames))
                        slot = i;
                if (slot < 0) continue;      // every track is live: skip this candidate
            }
            else
                ++numTracks_;
            Track& tr = tracks_[slot];
            tr.freq = freq; tr.lastDb = tr.firstDb = tr.maxDb = peakDb;
            tr.frames = 1; tr.seen = true;
        }
    }

    // Drop tracks that vanished this frame; engage the ones that persisted.
    for (int i = 0; i < numTracks_; )
    {
        Track& tr = tracks_[i];
        if (!tr.seen)
        {
            tracks_[i] = tracks_[--numTracks_];
            continue;
        }
        if (tr.frames >= t.frames)
        {
            if (!engage(tr))
            {
                // The table lock was busy: keep the persistence already built
                // up and try again next frame.
                tr.frames = t.frames;
                ++i;
                continue;
            }
            if (!engineActive())           // ADD satisfied: stop here
            {
                clearTracks();
                return;
            }
            tracks_[i] = tracks_[--numTracks_];
            continue;
        }
        ++i;
    }
}

// A persistent tone at track.freq: notch it, or deepen the notch that should
// already be catching it.
bool RingOutDSP::engage(const Track& track) noexcept
{
    const SpinLock::ScopedTryLock guard(tableLock_);
    if (!guard.isLocked())
        return false;        // someone is editing the table this instant

    const float f = track.freq;
    const float gq = globalQ_.load(std::memory_order_relaxed);
    ringout::FilterTable& table = shared_;

    // An existing filter whose bandwidth covers the tone, nearest first.
    int covering = -1; float coveringDist = 1.0e9f;
    int nearest = -1;  float nearestDist = 1.0e9f, nearestHalfBw = 0.0f;
    for (int i = 0; i < table.count; ++i)
    {
        const ringout::Filter& x = table.f[i];
        const float dist = std::fabs(std::log2(f / x.freqHz));
        const float halfBw = halfBandwidthOctaves(effectiveQ(x.q, gq)) + (binHz_ / x.freqHz) / kLn2;
        if (dist < nearestDist) { nearest = i; nearestDist = dist; nearestHalfBw = halfBw; }
        if (dist <= halfBw && dist < coveringDist) { covering = i; coveringDist = dist; }
    }

    int row = -1;
    bool changed = false;
    if (covering >= 0)
    {
        const int slot = slotForRow(covering);
        if (slot >= 0 && slots_[slot].cooldownFrames > 0)
            return true;                   // its last change has not had time to act
        ringout::Filter& x = table.f[covering];
        const ringout::Filter before = x;
        if (!x.on)
            x.on = true;                   // it was switched off: bring it back first
        else
            deepen(x);
        // Pull the centre a little toward where the tone actually is, once the
        // tone sits a quarter of a bin or more off it; closer than that the
        // estimate's own jitter would just churn the table.
        if (std::fabs(f - x.freqHz) > 0.25f * binHz_)
            x.freqHz = ringout::snapFreq(x.freqHz * std::pow(f / x.freqHz, 0.3f));
        changed = !ringout::filtersEqual(before, x);
        row = covering;
    }
    else if (table.count < ringout::kMaxFilters)
    {
        ringout::Filter x;
        x.on = true;
        x.freqHz = ringout::snapFreq(f);
        // Narrow enough to spare the programme, wide enough to cover the
        // estimate: at least three bins across, between Q 3 and Q 8.
        x.q = ringout::snapQ(ringout::clampf(f / (3.0f * binHz_), 3.0f, 8.0f));
        // Start at 6 dB, more for a tone that grew a lot while being watched,
        // and let persistence deepen it from there.
        const float growth = ringout::clampf(0.5f * (track.lastDb - track.firstDb), 0.0f, 6.0f);
        x.cutDb = ringout::snapCut(-(6.0f + growth));
        row = addLocked(x);
        changed = row >= 0;
    }
    else if (nearest >= 0 && nearestDist <= 2.0f * nearestHalfBw)
    {
        // Full table, tone just outside the nearest notch: widening that one is
        // the only move left.
        const int slot = slotForRow(nearest);
        if (slot >= 0 && slots_[slot].cooldownFrames > 0)
            return true;
        ringout::Filter& x = table.f[nearest];
        const ringout::Filter before = x;
        x.on = true;
        deepen(x);
        changed = !ringout::filtersEqual(before, x);
        row = nearest;
    }
    else
    {
        // Full table and nothing near: carving an unrelated notch deeper would
        // not stop this tone. Report it and leave the tone alone for a while.
        tableFull_.store(true, std::memory_order_relaxed);
        addHoldoff(f);
        return true;
    }

    // A filter already at the floor, both in depth and width, has nothing left
    // to give: no version bump for an unchanged table, just a pause before the
    // tone is judged again.
    addHoldoff(f);
    if (row < 0 || !changed)
        return true;

    tableVersion_.fetch_add(1, std::memory_order_acq_rel);
    adoptLiveTable();

    const int slot = slotForRow(row);
    if (slot >= 0)
        slots_[slot].cooldownFrames = 8;
    lastEngagedSlot_.store(row, std::memory_order_relaxed);
    engagementCount_.fetch_add(1, std::memory_order_relaxed);

    if (addSearching_.load(std::memory_order_acquire))
    {
        addSearching_.store(false, std::memory_order_release);
        addSatisfied_.store(true, std::memory_order_relaxed);
    }
    return true;
}

//==============================================================================

double RingOutDSP::responseDb(const ringout::FilterTable& table, float globalQ, float globalAmpDb,
                              double sampleRate, double freqHz) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    double mag = 1.0;
    for (int i = 0; i < table.count; ++i)
    {
        const ringout::Filter& x = table.f[i];
        if (!x.on) continue;
        Biquad b;
        b.setCoeffs(Biquad::peak(sampleRate, x.freqHz, effectiveCutDb(x.cutDb, globalAmpDb),
                                 effectiveQ(x.q, globalQ)));
        mag *= b.magnitude(w);
    }
    return 20.0 * std::log10(mag > 1.0e-9 ? mag : 1.0e-9);
}

} // namespace duskaudio
