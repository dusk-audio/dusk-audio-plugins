/*
  ==============================================================================

    AudioAnalyzer.h
    Analyzes sidechain audio to drive drum generation parameters

    Unlike the old GrooveExtractor (which extracted timing from drums),
    this analyzes ANY musical audio (guitar, bass, keys, etc.) to control:
    - Energy/loudness → drum intensity
    - Onset density → pattern complexity
    - Spectral changes → fill triggers (chord changes, sections)
    - Rhythmic accents → where to place emphasis

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <deque>

//==============================================================================
/**
 * Analysis results that can be used to control DrummerEngine parameters
 */
struct AudioAnalysisResult
{
    // Energy envelope (0-1) - controls loudness/intensity
    float energy = 0.0f;

    // Smoothed energy for stable control
    float smoothedEnergy = 0.0f;

    // Onset density (0-1) - how busy the playing is, controls complexity
    float onsetDensity = 0.0f;

    // Spectral flux (0-1) - rate of timbral change, can trigger fills
    float spectralFlux = 0.0f;

    // Low frequency energy ratio (0-1) - bass presence
    float lowEnergyRatio = 0.0f;

    // Mid frequency energy ratio (0-1) - guitar/vocal presence
    float midEnergyRatio = 0.0f;

    // High frequency energy ratio (0-1) - brightness/cymbal space
    float highEnergyRatio = 0.0f;

    // Detected downbeat strength (0-1) - where strong beats fall
    float downbeatStrength = 0.0f;

    // Section change detected (for fill triggers)
    bool sectionChangeDetected = false;

    // Suggested fill trigger
    bool suggestFill = false;

    // Is the input active (above noise floor)?
    bool isActive = false;

    // Confidence in the analysis (0-1)
    float confidence = 0.0f;
};

//==============================================================================
/**
 * Audio analyzer for Follow Mode
 *
 * Analyzes musical audio (not drums) to extract parameters for drum generation:
 * - Follows the energy of the input to match drum intensity
 * - Detects rhythmic density to control pattern complexity
 * - Identifies section changes/chord changes to trigger fills
 * - Extracts accent patterns to influence drum emphasis
 */
class AudioAnalyzer
{
public:
    AudioAnalyzer();
    ~AudioAnalyzer() = default;

    // Prepare for processing
    void prepare(double sampleRate, int maxBlockSize);

    // Reset all state
    void reset();

    // Process a block of audio
    void processBlock(const float* leftChannel, const float* rightChannel,
                      int numSamples, double hostBpm, double hostPositionBeats);

    // Get current analysis results
    const AudioAnalysisResult& getAnalysis() const { return currentAnalysis; }

    // Parameters
    void setSensitivity(float sensitivity);     // Overall sensitivity (0-1)
    void setEnergySmoothing(float timeMs);      // Energy follower smoothing
    void setFillSensitivity(float sensitivity); // How easily fills are triggered

private:
    double sampleRate = 44100.0;
    int blockSize = 512;

    // Current analysis
    AudioAnalysisResult currentAnalysis;

    // Sensitivity settings
    float sensitivity = 0.5f;
    float fillSensitivity = 0.5f;

    //==========================================================================
    // Energy Follower
    //==========================================================================
    float energyEnvelope = 0.0f;
    float energyAttackCoeff = 0.0f;
    float energyReleaseCoeff = 0.0f;
    float peakEnergy = 0.0f;          // For normalization
    float energyHistory[64] = {};      // ~1.3 sec at 48kHz/512
    int energyHistoryIndex = 0;

    void updateEnergyFollower(const float* monoData, int numSamples);

    //==========================================================================
    // Onset Detection
    //==========================================================================
    float prevEnergy = 0.0f;
    float onsetThreshold = 0.1f;
    int onsetCount = 0;
    int samplesSinceReset = 0;
    int onsetWindowSamples = 88200;  // Computed in prepare() for ~2 sec window

    void detectOnsets(const float* monoData, int numSamples);

    //==========================================================================
    // Spectral Analysis (3-band)
    //==========================================================================
    // Filter states
    float lowpassState = 0.0f;
    float bandpassLowState = 0.0f;
    float bandpassHighState = 0.0f;
    float highpassState = 0.0f;

    // Filter coefficients
    float lowCutoff = 0.0f;   // ~200 Hz
    float midCutoff = 0.0f;   // ~2000 Hz
    float highCutoff = 0.0f;  // ~6000 Hz

    // Band energies
    float lowBandEnergy = 0.0f;
    float midBandEnergy = 0.0f;
    float highBandEnergy = 0.0f;

    // Previous band energies for spectral flux
    float prevLowEnergy = 0.0f;
    float prevMidEnergy = 0.0f;
    float prevHighEnergy = 0.0f;

    void analyzeSpectrum(const float* monoData, int numSamples);
    void updateFilterCoefficients();

    // Simple one-pole filters
    float applyLowpass(float input, float& state, float coeff) const
    {
        state += coeff * (input - state);
        return state;
    }

    float applyHighpass(float input, float& state, float coeff) const
    {
        state += coeff * (input - state);
        return input - state;
    }

    //==========================================================================
    // Section/Fill Detection
    //==========================================================================
    float spectralFluxHistory[32] = {};
    int spectralFluxIndex = 0;
    float avgSpectralFlux = 0.0f;
    int samplesSinceLastFill = 0;
    int minFillIntervalSamples = 176400;  // Computed in prepare() for ~4 sec minimum

    void detectSectionChanges();

    //==========================================================================
    // Beat/Accent Tracking
    //==========================================================================
    double lastBeatPosition = 0.0;
    float beatEnergies[4] = {};  // Energy at each beat of the bar
    int currentBeatIndex = 0;

    void trackBeats(double hostPositionBeats);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioAnalyzer)
};
