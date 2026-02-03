#pragma once

#include <JuceHeader.h>
#include <vector>

//==============================================================================
/**
    User Preset Manager for Luna Co. Audio plugins

    Handles saving, loading, and managing user presets.
    Presets are stored as XML files in the user's application data directory.

    Directory locations:
    - macOS: ~/Library/Application Support/Luna Co Audio/{PluginName}/Presets/
    - Windows: %APPDATA%/Luna Co Audio/{PluginName}/Presets/
    - Linux: ~/.config/Luna Co Audio/{PluginName}/Presets/

    Usage:
    @code
    UserPresetManager presetManager("Multi-Q");  // Or "Multi-Comp", "4K-EQ", etc.

    // Save current state
    presetManager.saveUserPreset("My Custom Preset", processor.parameters.copyState());

    // Load presets
    auto presets = presetManager.loadUserPresets();
    for (const auto& preset : presets)
        DBG(preset.name);

    // Load a specific preset
    auto state = presetManager.loadUserPreset("My Custom Preset");
    if (state.isValid())
        processor.parameters.replaceState(state);
    @endcode
*/
class UserPresetManager
{
public:
    //==============================================================================
    struct UserPreset
    {
        juce::String name;
        juce::File file;
        juce::Time lastModified;
    };

    //==============================================================================
    /** Create a UserPresetManager for a specific plugin
        @param pluginName The name of the plugin (e.g., "Multi-Q", "Multi-Comp")
    */
    explicit UserPresetManager(const juce::String& pluginName)
        : pluginName(pluginName)
    {
    }

    ~UserPresetManager() = default;

    //==============================================================================
    /** Get the directory where user presets are stored for this plugin */
    juce::File getUserPresetDirectory() const
    {
        auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);

        #if JUCE_MAC
            // macOS: ~/Library/Application Support/Luna Co Audio/{PluginName}/Presets
            return appDataDir.getChildFile("Luna Co Audio")
                             .getChildFile(pluginName)
                             .getChildFile("Presets");
        #elif JUCE_WINDOWS
            // Windows: %APPDATA%/Luna Co Audio/{PluginName}/Presets
            return appDataDir.getChildFile("Luna Co Audio")
                             .getChildFile(pluginName)
                             .getChildFile("Presets");
        #else
            // Linux: ~/.config/Luna Co Audio/{PluginName}/Presets
            return appDataDir.getChildFile("Luna Co Audio")
                             .getChildFile(pluginName)
                             .getChildFile("Presets");
        #endif
    }

    //==============================================================================
    /** Ensure the user preset directory exists */
    bool ensureDirectoryExists()
    {
        auto dir = getUserPresetDirectory();
        if (!dir.exists())
            return dir.createDirectory();
        return true;
    }

    //==============================================================================
    /** Load all user presets from the preset directory */
    std::vector<UserPreset> loadUserPresets()
    {
        std::vector<UserPreset> presets;

        auto dir = getUserPresetDirectory();
        if (!dir.exists())
            return presets;

        for (const auto& file : dir.findChildFiles(juce::File::findFiles, false, "*.xml"))
        {
            UserPreset preset;
            preset.name = file.getFileNameWithoutExtension();
            preset.file = file;
            preset.lastModified = file.getLastModificationTime();
            presets.push_back(preset);
        }

        // Sort by name (case-insensitive)
        std::sort(presets.begin(), presets.end(),
            [](const UserPreset& a, const UserPreset& b) {
                return a.name.compareIgnoreCase(b.name) < 0;
            });

        return presets;
    }

    //==============================================================================
    /** Save a user preset with the given name
        @param name The name for the preset (will be used as filename)
        @param state The ValueTree containing the plugin state
        @param pluginVersion Optional version string to store in the preset
        @returns true if save was successful
    */
    bool saveUserPreset(const juce::String& name, const juce::ValueTree& state,
                        const juce::String& pluginVersion = "")
    {
        if (name.isEmpty())
            return false;

        if (!ensureDirectoryExists())
            return false;

        // Sanitize filename (remove invalid characters)
        juce::String safeName = sanitizePresetName(name);
        if (safeName.isEmpty())
            return false;

        auto file = getUserPresetDirectory().getChildFile(safeName + ".xml");

        // Create XML from ValueTree
        auto xml = state.createXml();
        if (!xml)
            return false;

        // Add metadata
        xml->setAttribute("presetName", name);
        xml->setAttribute("savedAt", juce::Time::getCurrentTime().toISO8601(true));
        xml->setAttribute("pluginName", pluginName);
        if (pluginVersion.isNotEmpty())
            xml->setAttribute("pluginVersion", pluginVersion);

        // Write to file
        return xml->writeTo(file);
    }

    //==============================================================================
    /** Load a user preset by name
        @param name The name of the preset to load
        @returns The ValueTree containing the preset state, or an invalid tree if not found
    */
    juce::ValueTree loadUserPreset(const juce::String& name)
    {
        auto file = getUserPresetDirectory().getChildFile(sanitizePresetName(name) + ".xml");
        return loadUserPresetFromFile(file);
    }

    /** Load a user preset from a file
        @param file The file to load
        @returns The ValueTree containing the preset state, or an invalid tree if not found
    */
    juce::ValueTree loadUserPresetFromFile(const juce::File& file)
    {
        if (!file.existsAsFile())
            return {};

        auto xml = juce::XmlDocument::parse(file);
        if (!xml)
            return {};

        return juce::ValueTree::fromXml(*xml);
    }

    //==============================================================================
    /** Delete a user preset
        @param name The name of the preset to delete
        @returns true if deletion was successful
    */
    bool deleteUserPreset(const juce::String& name)
    {
        auto file = getUserPresetDirectory().getChildFile(sanitizePresetName(name) + ".xml");
        if (!file.existsAsFile())
            return false;

        return file.deleteFile();
    }

    //==============================================================================
    /** Rename a user preset
        @param oldName The current name of the preset
        @param newName The new name for the preset
        @returns true if rename was successful
    */
    bool renameUserPreset(const juce::String& oldName, const juce::String& newName)
    {
        if (oldName.isEmpty() || newName.isEmpty())
            return false;

        auto oldFile = getUserPresetDirectory().getChildFile(sanitizePresetName(oldName) + ".xml");
        if (!oldFile.existsAsFile())
            return false;

        // Sanitize new name
        juce::String safeName = sanitizePresetName(newName);
        if (safeName.isEmpty())
            return false;

        auto newFile = getUserPresetDirectory().getChildFile(safeName + ".xml");

        // Don't overwrite existing preset
        if (newFile.existsAsFile())
            return false;

        return oldFile.moveFileTo(newFile);
    }

    //==============================================================================
    /** Check if a preset with the given name exists
        @param name The name to check
        @returns true if a preset with this name exists
    */
    bool presetExists(const juce::String& name)
    {
        auto file = getUserPresetDirectory().getChildFile(sanitizePresetName(name) + ".xml");
        return file.existsAsFile();
    }

    //==============================================================================
    /** Get the number of user presets */
    int getNumUserPresets()
    {
        auto presets = loadUserPresets();
        return static_cast<int>(presets.size());
    }

    //==============================================================================
    /** Get the plugin name this manager was created for */
    const juce::String& getPluginName() const { return pluginName; }

private:
    static juce::String sanitizePresetName(const juce::String& name)
    {
        return name.removeCharacters("\\/:*?\"<>|");
    }

    juce::String pluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UserPresetManager)
};
