/*
  ==============================================================================

    TempoTracker.h
    Estimates and tracks tempo from detected transients

    Uses inter-onset interval (IOI) analysis to estimate BPM.
    Supports tempo changes and provides confidence metrics.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <deque>

//==============================================================================
/**
 * Tempo estimate with confidence
 */
struct TempoEstimate
{
    double bpm = 120.0;
    float confidence = 0.0f;  // 0-1, how confident in the estimate
    bool isStable = false;    // True if tempo has been stable for a while
};

//==============================================================================
/**
 * Tempo tracker using inter-onset intervals
 *
 * Analyzes timing between detected transients to estimate tempo.
 * Uses histogram-based tempo detection with octave handling.
 */
class TempoTracker
{
public:
    TempoTracker();
    ~TempoTracker() = default;

    // Prepare for processing
    void prepare(double sampleRate);

    // Reset state
    void reset();

    // Add a detected transient
    void addTransient(double timeInSamples, float strength, int instrumentCategory);

    // Get current tempo estimate
    TempoEstimate getTempoEstimate() const;

    // Get estimated beat phase (0-1, where in the beat cycle we are)
    float getBeatPhase(double currentTimeInSamples) const;

    // Parameters
    void setTempoRange(double minBpm, double maxBpm);
    void setAdaptationRate(float rate);  // How fast to adapt to tempo changes (0-1)

    // Manual tempo hint (from host or user)
    void setTempoHint(double bpm);
    void clearTempoHint();

private:
    double sampleRate = 44100.0;

    // Tempo range
    double minBpm = 60.0;
    double maxBpm = 200.0;

    // Tempo hint from host
    double tempoHint = 0.0;
    bool hasTempoHint = false;

    // Recent transient times (in samples)
    static constexpr int MAX_TRANSIENTS = 64;
    std::deque<double> transientTimes;
    std::deque<float> transientStrengths;

    // Inter-onset intervals
    std::vector<double> recentIOIs;
    static constexpr int MAX_IOIS = 128;

    // Tempo histogram (for beat period detection)
    static constexpr int HISTOGRAM_SIZE = 256;
    static constexpr double HISTOGRAM_MIN_PERIOD_MS = 200.0;   // 300 BPM
    static constexpr double HISTOGRAM_MAX_PERIOD_MS = 1500.0;  // 40 BPM
    std::array<float, HISTOGRAM_SIZE> tempoHistogram = {};

    // Current tempo estimate
    double estimatedBpm = 120.0;
    float confidence = 0.0f;
    double lastBeatTime = 0.0;
    double beatPeriodSamples = 0.0;

    // Stability tracking
    int stableFrameCount = 0;
    static constexpr int STABLE_THRESHOLD = 8;  // Frames needed for "stable"
    double previousBpm = 120.0;

    // Adaptation
    float adaptationRate = 0.3f;

    // Convert between BPM and period
    double bpmToPeriodMs(double bpm) const
    {
        return 60000.0 / bpm;
    }

    double periodMsToBpm(double periodMs) const
    {
        return 60000.0 / periodMs;
    }

    // Convert IOI (in samples) to histogram bin
    int ioiToHistogramBin(double ioiSamples) const;

    // Find dominant period from histogram
    double findDominantPeriod() const;

    // Update tempo estimate from recent IOIs
    void updateTempoEstimate();

    // Handle octave errors (double/half tempo)
    double correctOctaveError(double rawBpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoTracker)
};
