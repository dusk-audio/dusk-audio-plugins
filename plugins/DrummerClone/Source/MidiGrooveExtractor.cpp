#include "MidiGrooveExtractor.h"
#include <cmath>
#include <numeric>

MidiGrooveExtractor::MidiGrooveExtractor()
{
    prepare(44100.0);
}

void MidiGrooveExtractor::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void MidiGrooveExtractor::reset()
{
    noteRingBuffer.clear();
    currentTime = 0.0;
}

ExtractedGroove MidiGrooveExtractor::extractFromBuffer(const juce::MidiBuffer& midiBuffer)
{
    ExtractedGroove groove;

    for (const auto metadata : midiBuffer)
    {
        const auto& message = metadata.getMessage();

        // Only process note-on events with sufficient velocity
        if (message.isNoteOn() && message.getVelocity() >= velocityThreshold)
        {
            // Calculate time in seconds
            double timeSeconds = static_cast<double>(metadata.samplePosition) / sampleRate;

            groove.noteOnTimes.push_back(timeSeconds);
            groove.velocities.push_back(message.getVelocity());
            groove.pitches.push_back(message.getNoteNumber());

            // Add to ring buffer
            NoteEvent event;
            event.timeSeconds = currentTime + timeSeconds;
            event.pitch = message.getNoteNumber();
            event.velocity = message.getVelocity();
            noteRingBuffer.push_back(event);
        }
    }

    groove.noteCount = static_cast<int>(groove.noteOnTimes.size());

    // Calculate statistics
    if (groove.noteCount > 0)
    {
        groove.averageVelocity = calculateAverageVelocity();
        groove.velocityVariance = calculateVelocityVariance(groove.averageVelocity);
    }

    // Prune old events from ring buffer
    pruneOldEvents();

    return groove;
}

void MidiGrooveExtractor::addToRingBuffer(const juce::MidiBuffer& midiBuffer, double bufferStartTime)
{
    currentTime = bufferStartTime;

    for (const auto metadata : midiBuffer)
    {
        const auto& message = metadata.getMessage();

        if (message.isNoteOn() && message.getVelocity() >= velocityThreshold)
        {
            double timeSeconds = bufferStartTime + (static_cast<double>(metadata.samplePosition) / sampleRate);

            NoteEvent event;
            event.timeSeconds = timeSeconds;
            event.pitch = message.getNoteNumber();
            event.velocity = message.getVelocity();
            noteRingBuffer.push_back(event);
        }
    }

    pruneOldEvents();
}

ExtractedGroove MidiGrooveExtractor::analyzeRingBuffer(double bpm)
{
    ExtractedGroove groove;

    for (const auto& event : noteRingBuffer)
    {
        groove.noteOnTimes.push_back(event.timeSeconds);
        groove.velocities.push_back(event.velocity);
        groove.pitches.push_back(event.pitch);
    }

    groove.noteCount = static_cast<int>(noteRingBuffer.size());

    if (groove.noteCount > 0)
    {
        groove.averageVelocity = calculateAverageVelocity();
        groove.velocityVariance = calculateVelocityVariance(groove.averageVelocity);
        groove.noteDensity = calculateNoteDensity(bpm);
    }

    return groove;
}

std::vector<double> MidiGrooveExtractor::getRecentNoteOnTimes() const
{
    std::vector<double> times;
    times.reserve(noteRingBuffer.size());

    for (const auto& event : noteRingBuffer)
    {
        times.push_back(event.timeSeconds);
    }

    return times;
}

void MidiGrooveExtractor::pruneOldEvents()
{
    // Remove events older than BUFFER_DURATION
    while (!noteRingBuffer.empty() &&
           (currentTime - noteRingBuffer.front().timeSeconds) > BUFFER_DURATION)
    {
        noteRingBuffer.pop_front();
    }
}

double MidiGrooveExtractor::calculateAverageVelocity() const
{
    if (noteRingBuffer.empty())
        return 100.0;

    double sum = 0.0;
    for (const auto& event : noteRingBuffer)
    {
        sum += event.velocity;
    }

    return sum / static_cast<double>(noteRingBuffer.size());
}

double MidiGrooveExtractor::calculateVelocityVariance(double mean) const
{
    if (noteRingBuffer.size() < 2)
        return 0.0;

    double sumSquaredDiff = 0.0;
    for (const auto& event : noteRingBuffer)
    {
        double diff = event.velocity - mean;
        sumSquaredDiff += diff * diff;
    }

    return sumSquaredDiff / static_cast<double>(noteRingBuffer.size() - 1);
}

double MidiGrooveExtractor::calculateNoteDensity(double bpm) const
{
    if (noteRingBuffer.empty() || bpm <= 0.0)
        return 0.0;

    // Calculate notes per beat
    double beatsInBuffer = (BUFFER_DURATION * bpm) / 60.0;
    return static_cast<double>(noteRingBuffer.size()) / beatsInBuffer;
}