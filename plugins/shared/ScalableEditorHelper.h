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

    void initialize(juce::AudioProcessorEditor* editor,
                    juce::AudioProcessor* processor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight,
                    bool fixedAspectRatio = false)
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

    static constexpr const char* kWindowWidth = "windowWidth";
    static constexpr const char* kWindowHeight = "windowHeight";

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
            options.folderName = "DuskAudio";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
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
