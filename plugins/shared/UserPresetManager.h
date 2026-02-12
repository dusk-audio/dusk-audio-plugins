#pragma once

#include <JuceHeader.h>
#include <vector>

// Saves/loads user presets as XML in the platform-standard app data directory.
class UserPresetManager
{
public:
    struct UserPreset
    {
        juce::String name;
        juce::File file;
        juce::Time lastModified;
    };

    explicit UserPresetManager(const juce::String& pluginName)
        : pluginName(pluginName)
    {
    }

    ~UserPresetManager() = default;

    juce::File getUserPresetDirectory() const
    {
        auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
        return appDataDir.getChildFile("Dusk Audio")
                         .getChildFile(pluginName)
                         .getChildFile("Presets");
    }

    juce::File getLegacyPresetDirectory() const
    {
        auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
        return appDataDir.getChildFile("Luna Co Audio")
                         .getChildFile(pluginName)
                         .getChildFile("Presets");
    }

    bool ensureDirectoryExists()
    {
        auto dir = getUserPresetDirectory();
        if (!dir.exists())
        {
            auto legacyDir = getLegacyPresetDirectory();
            if (legacyDir.isDirectory())
            {
                if (!legacyDir.copyDirectoryTo(dir))
                    return dir.createDirectory();
            }
            else
                return dir.createDirectory();
        }
        return true;
    }

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

        std::sort(presets.begin(), presets.end(),
            [](const UserPreset& a, const UserPreset& b) {
                return a.name.compareIgnoreCase(b.name) < 0;
            });

        return presets;
    }

    bool saveUserPreset(const juce::String& name, const juce::ValueTree& state,
                        const juce::String& pluginVersion = "")
    {
        if (name.isEmpty())
            return false;

        if (!ensureDirectoryExists())
            return false;

        juce::String safeName = sanitizePresetName(name);
        if (safeName.isEmpty())
            return false;

        auto file = getUserPresetDirectory().getChildFile(safeName + ".xml");

        auto xml = state.createXml();
        if (!xml)
            return false;

        xml->setAttribute("presetName", name);
        xml->setAttribute("savedAt", juce::Time::getCurrentTime().toISO8601(true));
        xml->setAttribute("pluginName", pluginName);
        if (pluginVersion.isNotEmpty())
            xml->setAttribute("pluginVersion", pluginVersion);

        return xml->writeTo(file);
    }

    juce::ValueTree loadUserPreset(const juce::String& name)
    {
        auto file = getUserPresetDirectory().getChildFile(sanitizePresetName(name) + ".xml");
        return loadUserPresetFromFile(file);
    }

    juce::ValueTree loadUserPresetFromFile(const juce::File& file)
    {
        if (!file.existsAsFile())
            return {};

        auto xml = juce::XmlDocument::parse(file);
        if (!xml)
            return {};

        return juce::ValueTree::fromXml(*xml);
    }

    bool deleteUserPreset(const juce::String& name)
    {
        auto file = getUserPresetDirectory().getChildFile(sanitizePresetName(name) + ".xml");
        if (!file.existsAsFile())
            return false;

        return file.deleteFile();
    }

    bool renameUserPreset(const juce::String& oldName, const juce::String& newName)
    {
        if (oldName.isEmpty() || newName.isEmpty())
            return false;

        auto oldFile = getUserPresetDirectory().getChildFile(sanitizePresetName(oldName) + ".xml");
        if (!oldFile.existsAsFile())
            return false;

        juce::String safeName = sanitizePresetName(newName);
        if (safeName.isEmpty())
            return false;

        auto newFile = getUserPresetDirectory().getChildFile(safeName + ".xml");
        if (newFile.existsAsFile())
            return false;

        return oldFile.moveFileTo(newFile);
    }

    bool presetExists(const juce::String& name)
    {
        auto file = getUserPresetDirectory().getChildFile(sanitizePresetName(name) + ".xml");
        return file.existsAsFile();
    }

    int getNumUserPresets()
    {
        return static_cast<int>(loadUserPresets().size());
    }

    const juce::String& getPluginName() const { return pluginName; }

private:
    static juce::String sanitizePresetName(const juce::String& name)
    {
        return name.removeCharacters("\\/:*?\"<>|");
    }

    juce::String pluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UserPresetManager)
};
