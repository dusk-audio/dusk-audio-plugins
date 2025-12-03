#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <atomic>
#include "GrooveTemplateGenerator.h"
#include "TransientDetector.h"

/**
 * Detected genre/style for auto-style selection
 */
enum class DetectedGenre
{
    Unknown = 0,
    Rock,           // 4/4, steady 8ths, backbeat snare
    HipHop,         // Syncopated kick, sparse snare, swung 16ths
    RnB,            // Heavy ghost notes, smooth swing
    Electronic,     // Four on floor, open hats on upbeats
    Trap,           // Half-time snare, rolling hi-hats
    Jazz,           // Ride pattern, brush feel, heavy swing
    Funk,           // 16th note groove, syncopated everything
    Songwriter,     // Simple patterns, brushes possible
    Latin           // Clave-based patterns
};

/**
 * Tempo drift information for tracking tempo changes
 */
struct TempoDriftInfo
{
    float driftPercentage = 0.0f;    // How much tempo is drifting (-100 to +100)
    float stability = 1.0f;          // How stable the tempo is (0 = unstable, 1 = rock solid)
    float avgTempo = 0.0f;           // Measured average tempo
    float tempoVariance = 0.0f;      // Variance in tempo measurements
    bool isRushing = false;          // True if player is rushing
    bool isDragging = false;         // True if player is dragging
};

/**
 * GrooveLearner - Accumulates transients over time to learn a groove pattern
 *
 * Similar to Logic Pro's Drummer "follow" feature, this class:
 * 1. Records transients from sidechain audio over multiple bars
 * 2. Analyzes the accumulated data to build a groove template
 * 3. Locks the groove once sufficient data is collected
 *
 * Phase 3 Improvements:
 * - Multi-source analysis: Combines MIDI and audio transients for better accuracy
 * - Tempo drift detection: Detects when player is rushing/dragging
 * - Genre detection: Auto-suggests style based on pattern analysis
 * - Improved confidence: Multi-factor confidence scoring
 *
 * States:
 * - Idle: Not learning, using default or previously locked groove
 * - Learning: Actively recording transients and updating groove
 * - Locked: Groove is finalized and won't change until reset
 *
 * Thread Safety:
 * - Audio thread calls: processOnsets(), processMidiOnsets(), prepare(), setBPM(), setTimeSignature()
 * - GUI thread calls: getState(), getLearningProgress(), getGrooveTemplate(), etc.
 * - State variables use std::atomic for lock-free access
 * - GrooveTemplate is double-buffered for lock-free GUI reads
 * - Complex operations protected by SpinLock (minimal blocking)
 */
class GrooveLearner
{
public:
    enum class State
    {
        Idle,       // Not learning
        Learning,   // Actively learning from input
        Locked      // Groove locked, no longer updating
    };

    /**
     * Source type for distinguishing transient origins
     */
    enum class TransientSource
    {
        Audio,      // From audio transient detection
        Midi        // From MIDI note analysis
    };

    GrooveLearner();
    ~GrooveLearner() = default;

    /**
     * Prepare the learner for processing
     * @param sampleRate Audio sample rate
     * @param bpm Current tempo
     */
    void prepare(double sampleRate, double bpm);

    /**
     * Update tempo (needed for accurate beat tracking)
     */
    void setBPM(double newBPM);

    /**
     * Set time signature for accurate bar tracking
     */
    void setTimeSignature(int numerator, int denominator);

    /**
     * Start learning from sidechain input
     */
    void startLearning();

    /**
     * Lock the current groove (stops learning)
     */
    void lockGroove();

    /**
     * Reset and clear learned groove
     */
    void reset();

    /**
     * Process incoming audio transients
     * Called each audio block with detected onset times
     * @param onsets Vector of onset times in seconds (relative to buffer start)
     * @param ppqPosition Current DAW playhead position in quarter notes
     * @param numSamples Number of samples in current buffer
     */
    void processOnsets(const std::vector<double>& onsets, double ppqPosition, int numSamples);

    /**
     * Process incoming MIDI transients (Phase 3: Multi-source analysis)
     * Called each audio block with MIDI note onset information
     * @param midiOnsets Vector of MIDI onset info (ppq position, velocity, note)
     * @param ppqPosition Current DAW playhead position in quarter notes
     */
    void processMidiOnsets(const std::vector<std::tuple<double, int, int>>& midiOnsets, double ppqPosition);

    /**
     * Enable/disable multi-source mode (combine MIDI + audio)
     */
    void setMultiSourceEnabled(bool enabled) { multiSourceEnabled.store(enabled, std::memory_order_relaxed); }
    bool isMultiSourceEnabled() const { return multiSourceEnabled.load(std::memory_order_relaxed); }

    /**
     * Get the current learned groove template (thread-safe, lock-free read)
     */
    GrooveTemplate getGrooveTemplate() const;

    /**
     * Get current learning state (thread-safe)
     */
    State getState() const { return currentState.load(std::memory_order_acquire); }

    /**
     * Get learning progress (0.0 - 1.0)
     * Based on number of bars analyzed (thread-safe)
     */
    float getLearningProgress() const;

    /**
     * Get number of bars learned (thread-safe)
     */
    int getBarsLearned() const { return barsAnalyzed.load(std::memory_order_relaxed); }

    /**
     * Get confidence in the learned groove (0.0 - 1.0) (thread-safe)
     * Phase 3: Now uses multi-factor scoring including:
     * - Pattern consistency
     * - Tempo stability
     * - Hit density
     * - Swing consistency
     */
    float getConfidence() const;

    /**
     * Check if groove is ready for use (thread-safe)
     */
    bool isGrooveReady() const;

    /**
     * Get detected genre based on pattern analysis (Phase 3)
     * Analyzes kick/snare patterns, swing amount, note density to suggest style
     */
    DetectedGenre getDetectedGenre() const { return detectedGenre.load(std::memory_order_relaxed); }

    /**
     * Get detected genre as string
     */
    juce::String getDetectedGenreString() const;

    /**
     * Get tempo drift information (Phase 3)
     * Detects if player is rushing/dragging
     */
    TempoDriftInfo getTempoDrift() const;

    /**
     * Auto-lock threshold - locks after this many bars (thread-safe)
     */
    void setAutoLockBars(int bars) { autoLockBars.store(bars, std::memory_order_relaxed); }
    int getAutoLockBars() const { return autoLockBars.load(std::memory_order_relaxed); }

    /**
     * Enable/disable auto-lock (thread-safe)
     */
    void setAutoLockEnabled(bool enabled) { autoLockEnabled.store(enabled, std::memory_order_relaxed); }
    bool isAutoLockEnabled() const { return autoLockEnabled.load(std::memory_order_relaxed); }

private:
    // Thread-safe state (atomic for lock-free access)
    std::atomic<State> currentState{State::Idle};
    std::atomic<int> barsAnalyzed{0};
    std::atomic<int> totalHits{0};
    std::atomic<bool> autoLockEnabled{true};
    std::atomic<int> autoLockBars{4};
    std::atomic<bool> multiSourceEnabled{false};  // Phase 3: Multi-source mode
    std::atomic<DetectedGenre> detectedGenre{DetectedGenre::Unknown};  // Phase 3: Genre detection

    // SpinLock for protecting complex state during audio processing
    // SpinLock is preferred over mutex for real-time audio (no syscalls)
    juce::SpinLock processLock;

    // Timing info (protected by processLock, only modified in audio thread)
    double sampleRate = 44100.0;
    double currentBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    double barLengthInQuarters = 4.0;  // Default 4/4

    // Accumulated transient data (protected by processLock)
    struct TransientEvent
    {
        double ppqPosition;     // Position in quarter notes
        double beatPosition;    // Position within beat (0.0 - 1.0)
        int barNumber;          // Which bar this occurred in
        int sixteenthPosition;  // Position as 16th note (0-15 for 4/4)
        TransientSource source = TransientSource::Audio;  // Phase 3: Track source
        int velocity = 100;     // MIDI velocity (for MIDI onsets)
        int midiNote = -1;      // MIDI note number (-1 for audio)
    };

    std::vector<TransientEvent> allTransients;

    // Phase 3: Separate tracking for audio and MIDI sources
    std::atomic<int> audioHits{0};
    std::atomic<int> midiHits{0};

    // Expected max transients to pre-allocate (avoids audio thread allocations)
    // Formula: 16 sixteenths * autoLockBars * 4 (allowing multiple hits per position)
    static constexpr size_t DEFAULT_TRANSIENT_RESERVE = 256;

    // Bar tracking (protected by processLock)
    int lastBarNumber = -1;

    // Analysis results - double buffered for lock-free GUI reads
    // Index 0 or 1, toggled atomically when updating
    GrooveTemplate grooveBuffers[2];
    std::atomic<int> activeGrooveBuffer{0};

    GrooveTemplateGenerator grooveGenerator;

    // Hit counts per 16th position (protected by processLock)
    std::array<int, 16> hitCounts = {0};
    std::array<float, 16> avgDeviations = {0.0f};  // Average timing deviation from grid

    // Phase 3: Velocity tracking per position for dynamics analysis
    std::array<float, 16> avgVelocities = {0.0f};
    std::array<int, 16> velocityCounts = {0};

    // Phase 3: Tempo drift tracking
    std::vector<double> interOnsetIntervals;  // Store IOIs for tempo analysis
    static constexpr size_t MAX_IOI_HISTORY = 64;
    double lastOnsetPPQ = -1.0;
    mutable TempoDriftInfo cachedTempoDrift;  // Cached for GUI access

    // Phase 3: Genre detection accumulators
    std::array<int, 4> kickBeatHits = {0};    // Hits on beats 1, 2, 3, 4
    std::array<int, 4> snareBeatHits = {0};   // Snare hits on beats 1, 2, 3, 4
    float accumulatedSwing = 0.0f;
    int swingSamples = 0;
    bool hasHalfTimeSnare = false;
    bool hasFourOnFloor = false;
    int sixteenthNoteHits = 0;  // Count of hits on pure 16th positions

    // Minimum data thresholds
    static constexpr int MIN_HITS_FOR_VALID_GROOVE = 8;
    static constexpr int MIN_BARS_FOR_CONFIDENCE = 2;

    // Helper methods (must be called with processLock held)
    void analyzeTransients();
    void updateGrooveTemplate();
    void publishGrooveTemplate();  // Swap double buffer
    double getPPQPositionInBar(double ppq) const;
    int getBarNumber(double ppq) const;
    int getSixteenthPosition(double ppqInBar) const;
    float calculateSwingFromHits() const;
    void calculateMicroTimingFromHits();

    // Phase 3: New analysis methods
    void analyzeGenre();           // Detect genre from accumulated patterns
    void updateTempoDrift();       // Update tempo drift tracking
    float calculatePatternConsistency() const;  // Measure pattern consistency across bars
    void processTransientInternal(double ppqPosition, TransientSource source, int velocity = 100, int midiNote = -1);
};
