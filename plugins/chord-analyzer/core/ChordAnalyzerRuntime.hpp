// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "ChordAnalyzerState.hpp"
#include "ChordHistory.hpp"
#include "ChordRecorder.hpp"
#include "MidiNoteState.hpp"
#include "../../shared-daf/dsp/DuskSeqLock.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace duskaudio::chord
{
struct AnalyzerView
{
    ChordDescription chord;
    std::vector<int> notes;
    std::vector<ChordSuggestionText> suggestions;
    std::vector<ChordDescription> history;
    bool recording = false;
    int recordedEvents = 0;
    double tempoBPM = 120.0;
};

// Audio owns MIDI and the queue producer. One mutex serializes all non-audio
// consumers (worker, editor and recording controls). run() never takes it.
// A 20 Hz worker preserves history/recording even with no editor instantiated.
class ChordAnalyzerRuntime
{
public:
    ChordAnalyzerRuntime();
    ~ChordAnalyzerRuntime();
    ChordAnalyzerRuntime(const ChordAnalyzerRuntime&) = delete;
    ChordAnalyzerRuntime& operator=(const ChordAnalyzerRuntime&) = delete;

    float parameter(unsigned index) const noexcept;
    void setParameter(unsigned index, float value) noexcept;
    InputValues inputValues() const noexcept { return inputs.values(); }
    bool restoreInputs(const InputValues& values) noexcept;
    // Host lifecycle only, while audio is stopped. Ends an active recording
    // at the old clock before resetting its timeline; retains session/history.
    void prepare(double sampleRate);
    void release();
    // Audio-only, bounded and allocation-free. Call beginBlock before MIDI and
    // endBlock after it; zero-frame blocks must not call either.
    void beginBlock() noexcept;
    void midi(const std::uint8_t* data, std::uint32_t size) noexcept;
    void endBlock(std::uint32_t frames, double tempoBPM) noexcept;

    AnalyzerView view() const;
    RecordingSession session() const;
    void startRecording(std::int64_t unixMilliseconds);
    void stopRecording();
    void clearRecording();
    void clearHistory();
    // Non-audio drain, also used before a control action needs current data.
    void flush();

private:
    friend struct ChordAnalyzerRuntimeTestAccess;
    struct Frame
    {
        MidiNoteState::Words words{};
        double timeSec = 0.0;
        std::uint64_t serial = 0;
    };
    void publish() noexcept;
    void serviceLocked();
    void analyzeLocked(const Frame& frame);
    void resetNotesLocked();

    InputState inputs;
    std::array<std::atomic<float>, PARAM_COUNT - inputParameterCount> outputs{};
    MidiNoteState noteState;
    MidiNoteState::Words publishedWords{};
    double sampleRate = 44100.0;
    std::uint64_t serial = 0;
    std::atomic<double> elapsed{0.0}, hostTempo{120.0};
    static_assert(std::atomic<double>::is_always_lock_free, "audio clock must be lock-free");
    static constexpr std::size_t queueCapacity = 64;
    std::array<Frame, queueCapacity> queue{};
    std::atomic<std::size_t> writeHead{0}, readHead{0};
    duskaudio::SeqLock<Frame> latest;
    std::atomic<bool> settingsDirty{false};

    mutable std::mutex modelMutex;
    std::condition_variable wake;
    bool closing = false;
    ChordAnalyzerText analyzer;
    ChordHistory history;
    ChordRecorder recorder;
    AnalyzerView model;
    Frame consumed;
    // Must be last: every object the thread touches has been constructed.
    std::thread worker;
};
} // namespace duskaudio::chord
