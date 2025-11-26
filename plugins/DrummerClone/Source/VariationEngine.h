#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cstdint>

/**
 * VariationEngine - Generates infinite, non-repeating variations
 *
 * Uses combination of:
 * - Perlin noise for smooth energy drift over time
 * - LFSR (Linear Feedback Shift Register) for pseudo-random patterns
 * - Pattern hashing to detect and avoid repetition
 *
 * This ensures the drummer feels "alive" and doesn't loop obviously.
 */
class VariationEngine
{
public:
    VariationEngine();
    ~VariationEngine() = default;

    /**
     * Prepare the engine
     * @param seed Random seed (use different seeds for different drummers)
     */
    void prepare(uint32_t seed = 0);

    /**
     * Reset the engine state
     */
    void reset();

    /**
     * Get energy variation for a given bar position
     * Uses Perlin noise for smooth transitions
     * @param barPosition Current bar number
     * @return Energy multiplier (0.8 - 1.2 range typically)
     */
    float getEnergyVariation(double barPosition);

    /**
     * Get a pseudo-random value for pattern decisions
     * Uses LFSR for deterministic but non-repeating sequence
     * @return Value between 0.0 and 1.0
     */
    float nextRandom();

    /**
     * Check if a pattern (represented as hash) was recently used
     * @param patternHash CRC32 or similar hash of pattern
     * @return true if pattern was used in last N bars
     */
    bool wasRecentlyUsed(uint32_t patternHash);

    /**
     * Register a pattern as used
     * @param patternHash Hash of the pattern
     */
    void registerPattern(uint32_t patternHash);

    /**
     * Calculate a simple pattern hash from MIDI events
     * @param notes Array of note pitches
     * @param velocities Array of velocities
     * @param count Number of notes
     * @return Hash value
     */
    static uint32_t hashPattern(const int* notes, const int* velocities, int count);

    /**
     * Get variation probability based on bar position
     * Higher at phrase boundaries (every 4 or 8 bars)
     * @param barPosition Current bar number
     * @return Probability 0.0 - 1.0
     */
    float getVariationProbability(int barPosition);

    /**
     * Get fill trigger probability
     * Increases over time since last fill, with random variation
     * @param barsSinceLastFill Bars since the last fill
     * @param fillHunger Drummer's fill tendency (0-1)
     * @return Probability 0.0 - 1.0
     */
    float getFillProbability(int barsSinceLastFill, float fillHunger);

private:
    // LFSR state (16-bit)
    uint16_t lfsrState = 0xACE1;

    // Perlin noise state
    std::array<float, 256> perlinGradients;
    std::array<uint8_t, 256> perlinPermutation;

    // Pattern history (circular buffer of recent pattern hashes)
    static constexpr int HISTORY_SIZE = 16;
    std::array<uint32_t, HISTORY_SIZE> patternHistory;
    int historyIndex = 0;

    // Random engine for initialization
    juce::Random random;

    // Perlin noise helpers
    void initPerlin(uint32_t seed);
    float perlinNoise(float x);
    float fade(float t);
    float lerp(float a, float b, float t);

    // LFSR step
    uint16_t lfsrStep();
};