// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "ChordAnalyzerRuntime.hpp"
#include <chrono>

namespace duskaudio::chord
{
ChordAnalyzerRuntime::ChordAnalyzerRuntime()
{
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
        parameters[i].store(parameterSpecs[i].initial, std::memory_order_relaxed);
    latest.store({});
    worker = std::thread([this] {
        std::unique_lock<std::mutex> lock(modelMutex);
        while (!closing)
        {
            wake.wait_for(lock, std::chrono::milliseconds(50), [this] { return closing; });
            if (!closing) serviceLocked();
        }
    });
}

ChordAnalyzerRuntime::~ChordAnalyzerRuntime()
{
    {
        const std::lock_guard<std::mutex> lock(modelMutex);
        closing = true;
    }
    wake.notify_one();
    worker.join();
}

float ChordAnalyzerRuntime::parameter(unsigned index) const noexcept
{
    return index < PARAM_COUNT ? parameters[index].load(std::memory_order_relaxed) : 0.0f;
}

void ChordAnalyzerRuntime::setParameter(unsigned index, float value) noexcept
{
    if (index >= inputParameterCount || !std::isfinite(value)) return;
    parameters[index].store(snapParameter(index, value), std::memory_order_relaxed);
    settingsDirty.store(true, std::memory_order_release);
}

void ChordAnalyzerRuntime::prepare(double rate)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
    recorder.stopRecording(elapsed.load(std::memory_order_relaxed));
    sampleRate = std::isfinite(rate) && rate > 0.0 ? rate : 44100.0;
    elapsed.store(0.0, std::memory_order_relaxed);
    hostTempo.store(120.0, std::memory_order_relaxed);
    resetNotesLocked();
}

void ChordAnalyzerRuntime::release()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
    recorder.stopRecording(elapsed.load(std::memory_order_relaxed));
    resetNotesLocked();
}

void ChordAnalyzerRuntime::resetNotesLocked()
{
    noteState.reset();
    publishedWords = {};
    // Producer is stopped; consumer is held by modelMutex. Never race two
    // writers to readHead, or reuse a queue cell while a reader copies it.
    readHead.store(writeHead.load(std::memory_order_relaxed), std::memory_order_release);
    consumed = {{}, elapsed.load(std::memory_order_relaxed), ++serial};
    latest.store(consumed);
    settingsDirty.store(false, std::memory_order_release);
    analyzeLocked(consumed);
}

void ChordAnalyzerRuntime::beginBlock() noexcept
{
    noteState.setRespectSustain(parameter(PARAM_RESPECT_SUSTAIN) >= 0.5f);
}

void ChordAnalyzerRuntime::midi(const std::uint8_t* data, std::uint32_t size) noexcept
{
    noteState.process(data, size);
}

void ChordAnalyzerRuntime::endBlock(std::uint32_t frames, double bpm) noexcept
{
    if (frames == 0) return;
    hostTempo.store(std::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0, std::memory_order_relaxed);
    if (noteState.snapshot() != publishedWords) publish();
    elapsed.store(elapsed.load(std::memory_order_relaxed) + frames / sampleRate,
                  std::memory_order_relaxed);
}

void ChordAnalyzerRuntime::publish() noexcept
{
    publishedWords = noteState.snapshot();
    const Frame frame{publishedWords, elapsed.load(std::memory_order_relaxed), ++serial};
    latest.store(frame);
    const auto write = writeHead.load(std::memory_order_relaxed);
    const auto next = (write + 1) % queueCapacity;
    if (next == readHead.load(std::memory_order_acquire)) return;
    queue[write] = frame;
    writeHead.store(next, std::memory_order_release);
}

void ChordAnalyzerRuntime::serviceLocked()
{
    // Capture fallback BEFORE draining. If the producer enqueues more frames
    // while we drain, never jump ahead of those cells and replay them later.
    Frame fallback;
    const bool haveFallback = latest.load(fallback);
    const auto stop = writeHead.load(std::memory_order_acquire);
    for (;;)
    {
        const auto read = readHead.load(std::memory_order_relaxed);
        if (read == stop) break;
        const Frame frame = queue[read];
        readHead.store((read + 1) % queueCapacity, std::memory_order_release);
        if (frame.serial > consumed.serial) analyzeLocked(frame);
    }
    if (haveFallback && fallback.serial > consumed.serial) analyzeLocked(fallback);
    if (settingsDirty.exchange(false, std::memory_order_acq_rel)) analyzeLocked(consumed);
}

void ChordAnalyzerRuntime::analyzeLocked(const Frame& frame)
{
    consumed = frame;
    model.notes.clear();
    for (int n = 0; n < 128; ++n)
        if (frame.words[static_cast<std::size_t>(n / 64)] & (std::uint64_t{1} << (n % 64)))
            model.notes.push_back(n);
    analyzer.setKey(static_cast<int>(parameter(PARAM_KEY_ROOT)), parameter(PARAM_KEY_MODE) != 0.0f);
    auto chord = analyzer.analyze(model.notes);
    if (chord != model.chord)
    {
        history.append(chord);
        recorder.recordChord(chord, frame.timeSec);
    }
    model.chord = std::move(chord);
    model.suggestions = analyzer.getSuggestions(model.chord,
        static_cast<SuggestionCategory>(static_cast<int>(parameter(PARAM_SUGGESTION_LEVEL))));
    const auto& c = model.chord;
    const bool valid = c.isValid;
    const std::array<float, 5> outputs {{
        valid && c.rootNote >= 0 ? static_cast<float>(c.rootNote + 1) : 0.0f,
        valid && c.quality != ChordQuality::Unknown ? static_cast<float>(static_cast<int>(c.quality) + 1) : 0.0f,
        valid && c.bassNote >= 0 ? static_cast<float>(c.bassNote + 1) : 0.0f,
        valid ? static_cast<float>(c.inversion + 1) : 0.0f,
        static_cast<float>(ChordAnalyzerText::encodeLabel(c, model.notes.data(),
            static_cast<int>(model.notes.size()), parameter(PARAM_SHOW_INVERSIONS) != 0.0f))
    }};
    for (unsigned i = 0; i < outputs.size(); ++i)
        parameters[inputParameterCount + i].store(outputs[i], std::memory_order_relaxed);
}

void ChordAnalyzerRuntime::flush()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
}

AnalyzerView ChordAnalyzerRuntime::view() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    auto out = model;
    out.history = history.snapshot();
    out.recording = recorder.isRecording();
    out.recordedEvents = recorder.getEventCount();
    out.tempoBPM = hostTempo.load(std::memory_order_relaxed);
    return out;
}

RecordingSession ChordAnalyzerRuntime::session() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return recorder.getSession();
}

void ChordAnalyzerRuntime::startRecording(std::int64_t unixMilliseconds)
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
    if (recorder.isRecording()) return;
    recorder.startRecording(unixMilliseconds);
    recorder.setKey(static_cast<int>(parameter(PARAM_KEY_ROOT)), parameter(PARAM_KEY_MODE) != 0.0f);
    recorder.setTempo(hostTempo.load(std::memory_order_relaxed));
}

void ChordAnalyzerRuntime::stopRecording()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
    recorder.stopRecording(elapsed.load(std::memory_order_relaxed));
}

void ChordAnalyzerRuntime::clearRecording()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
    recorder.clearSession();
}

void ChordAnalyzerRuntime::clearHistory()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    serviceLocked();
    history.clear();
}
} // namespace duskaudio::chord
