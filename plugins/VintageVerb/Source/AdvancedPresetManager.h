/*
  ==============================================================================

    AdvancedPresetManager.h - Professional preset management system

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>

class AdvancedPresetManager
{
public:
    AdvancedPresetManager(juce::AudioProcessorValueTreeState& apvts);
    ~AdvancedPresetManager();

    //==============================================================================
    // Preset Structure
    //==============================================================================
    struct Preset
    {
        juce::String name;
        juce::String category;
        juce::String author;
        juce::String description;
        juce::DateTime dateCreated;
        juce::DateTime dateModified;
        std::map<juce::String, float> parameters;
        std::map<juce::String, juce::String> metadata;
        juce::StringArray tags;
        float rating = 0.0f;  // User rating 0-5
        bool isFavorite = false;
        bool isFactory = false;
    };

    //==============================================================================
    // A/B Comparison
    //==============================================================================
    class ABComparison
    {
    public:
        ABComparison(juce::AudioProcessorValueTreeState& params);

        void copyToA();
        void copyToB();
        void switchToA();
        void switchToB();
        void toggleAB();

        bool isOnA() const { return currentSlot == 0; }
        void copyAtoB();
        void copyBtoA();

        // Get current differences
        std::vector<juce::String> getDifferentParameters() const;
        float getSimilarityPercentage() const;

    private:
        juce::AudioProcessorValueTreeState& parameters;
        std::map<juce::String, float> slotA;
        std::map<juce::String, float> slotB;
        int currentSlot = 0;  // 0 = A, 1 = B

        void saveCurrentToSlot(std::map<juce::String, float>& slot);
        void loadSlotToCurrent(const std::map<juce::String, float>& slot);
    };

    //==============================================================================
    // Preset Morphing
    //==============================================================================
    class PresetMorpher
    {
    public:
        PresetMorpher(juce::AudioProcessorValueTreeState& params);

        void setSourcePreset(const Preset& preset);
        void setTargetPreset(const Preset& preset);
        void setMorphPosition(float position);  // 0 = source, 1 = target

        // Morphing modes
        enum class MorphMode
        {
            Linear,
            Exponential,
            Logarithmic,
            SCurve,
            Random  // Randomly interpolate each parameter
        };

        void setMorphMode(MorphMode mode) { morphMode = mode; }
        void setMorphTime(float seconds);
        void startMorphing();
        void stopMorphing();
        bool isMorphing() const { return morphing; }

        // Parameter exclusion
        void excludeParameter(const juce::String& paramID);
        void includeParameter(const juce::String& paramID);

    private:
        juce::AudioProcessorValueTreeState& parameters;
        Preset sourcePreset;
        Preset targetPreset;
        float morphPosition = 0.0f;
        MorphMode morphMode = MorphMode::Linear;
        bool morphing = false;

        juce::StringArray excludedParameters;
        std::map<juce::String, juce::SmoothedValue<float>> morphSmoothers;

        float applyMorphCurve(float value);
        void updateMorphing();
    };

    //==============================================================================
    // Preset Organization
    //==============================================================================
    class PresetOrganizer
    {
    public:
        // Categories
        void addCategory(const juce::String& category);
        void removeCategory(const juce::String& category);
        void renameCategory(const juce::String& oldName, const juce::String& newName);
        juce::StringArray getCategories() const { return categories; }

        // Tags
        void addTag(const juce::String& tag);
        void removeTag(const juce::String& tag);
        juce::StringArray getAllTags() const { return allTags; }

        // Searching and filtering
        std::vector<Preset> searchPresets(const juce::String& query);
        std::vector<Preset> filterByCategory(const juce::String& category);
        std::vector<Preset> filterByTags(const juce::StringArray& tags);
        std::vector<Preset> filterByRating(float minRating);
        std::vector<Preset> getFavorites();
        std::vector<Preset> getRecent(int count);

        // Sorting
        enum class SortBy
        {
            Name,
            Category,
            DateCreated,
            DateModified,
            Rating,
            Author
        };

        void sortPresets(std::vector<Preset>& presets, SortBy sortBy, bool ascending = true);

    private:
        juce::StringArray categories;
        juce::StringArray allTags;
        std::vector<Preset> allPresets;
    };

    //==============================================================================
    // Main Interface
    //==============================================================================

    // Preset management
    void loadPreset(const Preset& preset);
    void savePreset(const juce::String& name, const juce::String& category = "User");
    void deletePreset(const Preset& preset);
    void renamePreset(Preset& preset, const juce::String& newName);
    void updatePreset(Preset& preset);

    // Import/Export
    void exportPreset(const Preset& preset, const juce::File& file);
    void importPreset(const juce::File& file);
    void exportBank(const std::vector<Preset>& presets, const juce::File& file);
    void importBank(const juce::File& file);

    // Factory presets
    void loadFactoryPresets();
    void restoreFactoryPresets();
    std::vector<Preset> getFactoryPresets() const;

    // User presets
    std::vector<Preset> getUserPresets() const;
    juce::File getUserPresetsFolder() const;

    // Recent presets
    void addToRecent(const Preset& preset);
    std::vector<Preset> getRecentPresets(int count = 10) const;

    // Random preset generation
    Preset generateRandomPreset(const juce::String& basedOn = "");
    void randomizeParameters(float amount = 1.0f);  // 0-1 randomization amount

    // Preset interpolation
    Preset interpolatePresets(const Preset& a, const Preset& b, float position);

    // Undo/Redo
    void pushToUndoStack();
    void undo();
    void redo();
    bool canUndo() const { return undoPosition > 0; }
    bool canRedo() const { return undoPosition < undoStack.size() - 1; }

    // Get components
    ABComparison& getABComparison() { return abComparison; }
    PresetMorpher& getMorpher() { return morpher; }
    PresetOrganizer& getOrganizer() { return organizer; }

    // Listeners
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void presetLoaded(const Preset& preset) {}
        virtual void presetSaved(const Preset& preset) {}
        virtual void presetDeleted(const Preset& preset) {}
        virtual void presetListChanged() {}
    };

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    juce::AudioProcessorValueTreeState& parameters;

    ABComparison abComparison;
    PresetMorpher morpher;
    PresetOrganizer organizer;

    std::vector<Preset> presets;
    std::vector<Preset> recentPresets;
    Preset currentPreset;

    // Undo/Redo
    std::vector<std::map<juce::String, float>> undoStack;
    size_t undoPosition = 0;
    static constexpr size_t MAX_UNDO_LEVELS = 50;

    // File handling
    juce::File presetsDirectory;
    void scanPresetsFolder();
    void savePresetToFile(const Preset& preset, const juce::File& file);
    Preset loadPresetFromFile(const juce::File& file);

    // Listeners
    juce::ListenerList<Listener> listeners;

    // Factory preset data
    void createFactoryPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedPresetManager)
};

//==============================================================================
// Preset Browser Component
//==============================================================================
class PresetBrowserComponent : public juce::Component,
                               public AdvancedPresetManager::Listener
{
public:
    PresetBrowserComponent(AdvancedPresetManager& manager);
    ~PresetBrowserComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // AdvancedPresetManager::Listener
    void presetListChanged() override;

private:
    AdvancedPresetManager& presetManager;

    // UI Components
    juce::TextEditor searchBox;
    juce::ComboBox categoryFilter;
    juce::ListBox presetList;
    juce::TextButton loadButton;
    juce::TextButton saveButton;
    juce::TextButton deleteButton;
    juce::TextButton abButton;
    juce::Slider morphSlider;
    juce::Label presetInfo;

    // Tag cloud
    std::unique_ptr<juce::Component> tagCloud;

    // Rating stars
    std::unique_ptr<juce::Component> ratingComponent;

    void updatePresetList();
    void loadSelectedPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowserComponent)
};