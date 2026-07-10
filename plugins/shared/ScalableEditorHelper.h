#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Resizable plugin UI helper. Call initialize() before setSize(),
// updateResizer() in resized(). Window size persists across sessions.
class ScalableEditorHelper
{
public:
    ScalableEditorHelper() = default;
    ~ScalableEditorHelper() = default;

    // uiVersion (default 0 = no version check, backwards-compatible) bumps
    // the persistence "uiVersion" key. If the stored uiVersion is lower
    // than the value passed here, loadStoredSize() resets the persisted
    // size to the new defaults. Bump this in any caller after a layout
    // change that requires existing sessions to pick up the new defaults.
    void initialize(juce::AudioProcessorEditor* editor,
                    juce::AudioProcessor* processor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight,
                    bool fixedAspectRatio = false,
                    int uiVersion = 0)
    {
        parentEditor = editor;
        audioProcessor = processor;
        baseWidth = static_cast<float>(defaultWidth);
        baseHeight = static_cast<float>(defaultHeight);
        defaultW = defaultWidth;
        defaultH = defaultHeight;
        minW = minWidth;
        minH = minHeight;
        maxW = maxWidth;
        maxH = maxHeight;
        currentUiVersion = uiVersion;

        loadStoredSize();

        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
        if (fixedAspectRatio)
            constrainer.setFixedAspectRatio(baseWidth / baseHeight);

        resizer = std::make_unique<juce::ResizableCornerComponent>(editor, &constrainer);
        editor->addAndMakeVisible(resizer.get());
        resizer->setAlwaysOnTop(true);

        editor->setResizable(true, true);
        editor->setResizeLimits(minWidth, minHeight, maxWidth, maxHeight);
    }

    // Overload without processor — no size persistence, fixed aspect ratio.
    void initialize(juce::AudioProcessorEditor* editor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight)
    {
        parentEditor = editor;
        audioProcessor = nullptr;
        persistenceEnabled = false;
        baseWidth = static_cast<float>(defaultWidth);
        baseHeight = static_cast<float>(defaultHeight);
        defaultW = defaultWidth;
        defaultH = defaultHeight;
        minW = minWidth;
        minH = minHeight;
        maxW = maxWidth;
        maxH = maxHeight;
        storedWidth = defaultWidth;
        storedHeight = defaultHeight;

        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
        constrainer.setFixedAspectRatio(baseWidth / baseHeight);

        resizer = std::make_unique<juce::ResizableCornerComponent>(editor, &constrainer);
        editor->addAndMakeVisible(resizer.get());
        resizer->setAlwaysOnTop(true);

        editor->setResizable(true, true);
        editor->setResizeLimits(minWidth, minHeight, maxWidth, maxHeight);
    }

    int getStoredWidth() const { return storedWidth; }
    int getStoredHeight() const { return storedHeight; }

    void updateResizer()
    {
        if (parentEditor == nullptr)
            return;

        if (resizer)
        {
            const int handleSize = 16;
            resizer->setBounds(parentEditor->getWidth() - handleSize,
                               parentEditor->getHeight() - handleSize,
                               handleSize, handleSize);
        }

        // Scale based on width only (height may vary with collapsible sections)
        scaleFactor = static_cast<float>(parentEditor->getWidth()) / baseWidth;
    }

    float getScaleFactor() const { return scaleFactor; }

    int scaled(int value) const
    {
        return static_cast<int>(static_cast<float>(value) * scaleFactor);
    }

    float scaled(float value) const
    {
        return value * scaleFactor;
    }

    juce::ComponentBoundsConstrainer& getConstrainer() { return constrainer; }

private:
    juce::AudioProcessorEditor* parentEditor = nullptr;
    juce::AudioProcessor* audioProcessor = nullptr;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    float baseWidth = 800.0f;
    float baseHeight = 600.0f;
    float scaleFactor = 1.0f;
    bool persistenceEnabled = true;

    int defaultW = 800;
    int defaultH = 600;
    int minW = 640;
    int minH = 480;
    int maxW = 1920;
    int maxH = 1200;
    int storedWidth = 800;
    int storedHeight = 600;
    int currentUiVersion = 0;   // 0 = no version gate (legacy callers)

    static constexpr const char* kWindowWidth = "windowWidth";
    static constexpr const char* kWindowHeight = "windowHeight";
    static constexpr const char* kUiVersion    = "uiVersion";

    void loadStoredSize()
    {
        storedWidth = defaultW;
        storedHeight = defaultH;

        if (!persistenceEnabled)
            return;

        auto* props = getAppProperties();
        if (props == nullptr)
            return;

        auto* userSettings = props->getUserSettings();
        if (userSettings == nullptr)
            return;

        juce::String prefix = getPluginPrefix();

        // UI version gate: if the caller passed a non-zero uiVersion and the
        // persisted version is older, the stored size is from a prior
        // layout and is no longer valid — fall back to defaults so the
        // user sees the new layout without having to manually resize.
        if (currentUiVersion > 0)
        {
            const int storedUiVersion = userSettings->getIntValue(prefix + kUiVersion, 0);
            if (storedUiVersion < currentUiVersion)
                return;   // storedWidth / Height already = defaults
        }

        storedWidth = userSettings->getIntValue(prefix + kWindowWidth, defaultW);
        storedHeight = userSettings->getIntValue(prefix + kWindowHeight, defaultH);

        storedWidth = juce::jlimit(minW, maxW, storedWidth);
        storedHeight = juce::jlimit(minH, maxH, storedHeight);
    }

    void saveCurrentSize()
    {
        if (parentEditor == nullptr)
            return;

        if (!persistenceEnabled)
            return;

        auto* props = getAppProperties();
        if (props == nullptr)
            return;

        auto* userSettings = props->getUserSettings();
        if (userSettings == nullptr)
            return;

        juce::String prefix = getPluginPrefix();
        userSettings->setValue(prefix + kWindowWidth, parentEditor->getWidth());
        userSettings->setValue(prefix + kWindowHeight, parentEditor->getHeight());
        if (currentUiVersion > 0)
            userSettings->setValue(prefix + kUiVersion, currentUiVersion);
        props->saveIfNeeded();
    }

    juce::ApplicationProperties* getAppProperties()
    {
        static juce::ApplicationProperties appProps;
        static bool initialized = false;

        if (!initialized)
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "DuskAudio";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX
            options.folderName = ".config/DuskAudio";

            // Older builds stored settings in ~/DuskAudio; move them so
            // existing users keep their saved window sizes.
            auto oldFile = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                               .getChildFile("DuskAudio")
                               .getChildFile("DuskAudio.settings");
            auto newFile = options.getDefaultFile();
            if (oldFile.existsAsFile() && !newFile.exists())
            {
                newFile.getParentDirectory().createDirectory();
                if (oldFile.moveFileTo(newFile))
                    oldFile.getParentDirectory().deleteFile(); // rmdir: only removes ~/DuskAudio if now empty
            }
           #else
            options.folderName = "DuskAudio";
           #endif
            appProps.setStorageParameters(options);
            initialized = true;
        }

        return &appProps;
    }

    juce::String getPluginPrefix() const
    {
        if (audioProcessor != nullptr)
            return audioProcessor->getName() + "_";
        return "Plugin_";
    }

public:
    void saveSize()
    {
        saveCurrentSize();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableEditorHelper)
};
