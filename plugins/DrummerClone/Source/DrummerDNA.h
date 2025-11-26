#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <map>

/**
 * DrummerDNA - Personality profiles for virtual drummers
 *
 * Each drummer has unique characteristics that influence:
 * - Pattern preferences
 * - Fill tendencies
 * - Dynamic range
 * - Ghost note usage
 * - Cymbal choices
 *
 * This gives each drummer a distinct "feel" like Logic Pro's Drummer characters.
 */
struct DrummerProfile
{
    juce::String name;
    juce::String style;           // Rock, HipHop, etc.
    juce::String bio;             // Description for UI tooltip

    // Personality traits (0.0 - 1.0)
    float aggression = 0.5f;      // Velocity intensity
    float grooveBias = 0.5f;      // Prefers straight (0) vs swung (1)
    float ghostNotes = 0.3f;      // Ghost note frequency
    float fillHunger = 0.3f;      // How often fills occur
    float tomLove = 0.3f;         // Tom usage in fills
    float ridePreference = 0.3f;  // Ride vs hi-hat preference
    float crashHappiness = 0.4f;  // Crash cymbal frequency
    float simplicity = 0.5f;      // Prefers simple (1) vs complex (0) patterns
    float laidBack = 0.0f;        // Timing: behind beat (1) vs on top (-1 to 1)

    // Technical preferences
    int preferredDivision = 16;   // 8 or 16
    float swingDefault = 0.0f;    // Default swing amount
    int velocityFloor = 40;       // Minimum velocity
    int velocityCeiling = 127;    // Maximum velocity
};

/**
 * DrummerDNA - Factory and manager for drummer profiles
 */
class DrummerDNA
{
public:
    DrummerDNA();
    ~DrummerDNA() = default;

    /**
     * Get a drummer profile by index
     * @param index Profile index (0-27)
     * @return Drummer profile
     */
    const DrummerProfile& getProfile(int index) const;

    /**
     * Get a drummer profile by name
     * @param name Drummer name
     * @return Drummer profile (or default if not found)
     */
    const DrummerProfile& getProfileByName(const juce::String& name) const;

    /**
     * Get all drummers for a specific style
     * @param style Style name (Rock, HipHop, etc.)
     * @return Vector of profile indices
     */
    std::vector<int> getDrummersByStyle(const juce::String& style) const;

    /**
     * Get total number of drummers
     */
    int getNumDrummers() const { return static_cast<int>(profiles.size()); }

    /**
     * Get list of all drummer names
     */
    juce::StringArray getDrummerNames() const;

    /**
     * Get list of all style names
     */
    juce::StringArray getStyleNames() const;

    /**
     * Load custom drummer profiles from JSON directory
     * @param directory Path to JSON files
     */
    void loadFromDirectory(const juce::File& directory);

    /**
     * Save a profile to JSON
     * @param profile Profile to save
     * @param file Output file
     */
    static void saveToJSON(const DrummerProfile& profile, const juce::File& file);

    /**
     * Load a profile from JSON
     * @param file Input file
     * @return Loaded profile
     */
    static DrummerProfile loadFromJSON(const juce::File& file);

private:
    std::vector<DrummerProfile> profiles;
    DrummerProfile defaultProfile;

    void createDefaultProfiles();
};