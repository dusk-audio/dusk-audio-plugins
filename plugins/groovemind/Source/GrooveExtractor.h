/*
  ==============================================================================

    GrooveExtractor.h
    Extracts groove (micro-timing and velocity patterns) from audio

    Analyzes detected transients to extract the "feel" of the input audio,
    which can then be applied to generated patterns.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "TransientDetector.h"
#include "TempoTracker.h"
#include <array>
#include <deque>

//==============================================================================
/**
 * Extracted groove pattern representing timing and velocity deviations
 * over a one-bar period (typically 16 slots for 16th notes)
 */
struct ExtractedGroove
{
    static constexpr int SLOTS_PER_BAR = 16;  // 16th note resolution

    // Timing offsets in milliseconds for each slot (-50 to +50 typical)
    std::array<float, SLOTS_PER_BAR> timingOffsets = {};

    // Velocity multipliers for each slot (0.5 to 1.5 typical)
    std::array<float, SLOTS_PER_BAR> velocityMultipliers = {};

    // Confidence for each slot (how many hits contributed)
    std::array<float, SLOTS_PER_BAR> confidence = {};

    // Overall groove confidence
    float overallConfidence = 0.0f;

    // Is groove valid (enough data collected)?
    bool isValid = false;

    // Reset to neutral
    void reset()
    {
        timingOffsets.fill(0.0f);
        velocityMultipliers.fill(1.0f);
        confidence.fill(0.0f);
        overallConfidence = 0.0f;
        isValid = false;
    }
};

//==============================================================================
/**
 * Groove extractor - analyzes transients to extract timing/velocity patterns
 *
 * Works by:
 * 1. Quantizing transients to nearest 16th note grid position
 * 2. Measuring deviation from grid (timing offset)
 * 3. Measuring relative velocity (strength)
 * 4. Averaging over multiple bars to get consistent groove
 */
class GrooveExtractor
{
public:
    GrooveExtractor();
    ~GrooveExtractor() = default;

    // Prepare for processing
    void prepare(double sampleRate);

    // Reset state
    void reset();

    // Process a detected transient
    void addTransient(const TransientEvent& event, double currentBpm,
                      double barStartTimeInSamples);

    // Notify of new bar start (helps with alignment)
    void notifyBarStart(double timeInSamples);

    // Get the current extracted groove
    const ExtractedGroove& getExtractedGroove() const { return extractedGroove; }

    // Parameters
    void setAnalysisWindowBars(int bars);  // How many bars to analyze (default: 4)
    void setAdaptationRate(float rate);    // How fast groove updates (0-1)

    // Get analysis progress (0-1)
    float getAnalysisProgress() const;

private:
    double sampleRate = 44100.0;

    // Analysis parameters
    int analysisWindowBars = 4;
    float adaptationRate = 0.3f;

    // Bar tracking
    double lastBarStartTime = 0.0;
    double barLengthSamples = 0.0;
    int barsAnalyzed = 0;

    // Transient history for analysis
    struct GrooveHit
    {
        int slotIndex;           // Which 16th note slot (0-15)
        float timingOffsetMs;    // Deviation from grid in ms
        float normalizedStrength;// Relative strength (0-1)
        int instrumentCategory;  // What instrument
    };

    std::deque<GrooveHit> recentHits;
    static constexpr int MAX_HITS = 256;

    // Accumulated groove data (for averaging)
    std::array<float, ExtractedGroove::SLOTS_PER_BAR> accumulatedTimingOffsets = {};
    std::array<float, ExtractedGroove::SLOTS_PER_BAR> accumulatedVelocities = {};
    std::array<int, ExtractedGroove::SLOTS_PER_BAR> hitCounts = {};

    // Current extracted groove
    ExtractedGroove extractedGroove;

    // Quantize transient time to nearest slot
    int quantizeToSlot(double timeInSamples, double barStartTime, double bpm) const;

    // Calculate timing offset from quantized position
    float calculateTimingOffset(double timeInSamples, int slot,
                                 double barStartTime, double bpm) const;

    // Update extracted groove from accumulated data
    void updateExtractedGroove();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveExtractor)
};

//==============================================================================
/**
 * Follow Mode controller - combines all follow mode components
 */
class FollowModeController
{
public:
    FollowModeController();
    ~FollowModeController() = default;

    // Prepare for processing
    void prepare(double sampleRate, int maxBlockSize);

    // Reset all state
    void reset();

    // Process audio input
    void processAudio(const float* leftChannel, const float* rightChannel,
                      int numSamples, double hostBpm, double hostPositionBeats);

    // Enable/disable follow mode
    void setEnabled(bool enabled) { followEnabled = enabled; }
    bool isEnabled() const { return followEnabled; }

    // Get current state
    const ExtractedGroove& getExtractedGroove() const { return grooveExtractor.getExtractedGroove(); }
    TempoEstimate getTempoEstimate() const { return tempoTracker.getTempoEstimate(); }

    // Get detected transients for the current block (for visualization)
    const std::vector<TransientEvent>& getLastDetectedTransients() const { return lastTransients; }

    // Parameters
    void setSensitivity(float sensitivity);  // Detection sensitivity (0-1)
    void setTempoSource(int source);         // 0=auto, 1=host
    void setGrooveAmount(float amount);      // How much to apply extracted groove (0-1)

    // Apply extracted groove to a timing offset
    float applyGroove(double beatPosition, float originalOffsetMs) const;

    // Apply extracted groove to velocity
    float applyGrooveVelocity(double beatPosition, float originalVelocity) const;

private:
    TransientDetector transientDetector;
    TempoTracker tempoTracker;
    GrooveExtractor grooveExtractor;

    bool followEnabled = false;
    int tempoSource = 0;  // 0=auto, 1=host
    float grooveAmount = 0.7f;

    double sampleRate = 44100.0;
    double currentBpm = 120.0;
    double currentBarStart = 0.0;
    double lastPositionBeats = 0.0;

    std::vector<TransientEvent> lastTransients;

    // Track bar boundaries
    void updateBarTracking(double hostPositionBeats, double hostBpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FollowModeController)
};
