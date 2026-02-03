#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * ScalableEditorHelper - Shared utility for resizable plugin UIs
 *
 * Usage in your PluginEditor:
 *
 * 1. Add as a member:
 *    ScalableEditorHelper resizeHelper;
 *
 * 2. In constructor, BEFORE setSize():
 *    resizeHelper.initialize(this, &processor, defaultWidth, defaultHeight, minWidth, minHeight, maxWidth, maxHeight);
 *    setSize(resizeHelper.getStoredWidth(), resizeHelper.getStoredHeight());
 *
 * 3. In resized():
 *    resizeHelper.updateResizer();
 *    float scale = resizeHelper.getScaleFactor();
 *    // Use scale to size your components proportionally
 *
 * 4. Window size is automatically saved/restored via plugin state.
 *
 * This provides:
 * - Fixed aspect ratio to prevent distortion (optional)
 * - ResizableCornerComponent for drag-to-resize
 * - Automatic scale factor calculation
 * - Window size persistence across sessions
 */
class ScalableEditorHelper
{
public:
    ScalableEditorHelper() = default;
    ~ScalableEditorHelper() = default;

    /**
     * Initialize the resize system for a plugin editor.
     * Call this in your editor's constructor BEFORE setSize().
     *
     * @param editor The AudioProcessorEditor to make resizable
     * @param processor The AudioProcessor (for state persistence)
     * @param defaultWidth Base width for scale calculations (and default size)
     * @param defaultHeight Base height for scale calculations (and default size)
     * @param minWidth Minimum allowed width
     * @param minHeight Minimum allowed height
     * @param maxWidth Maximum allowed width
     * @param maxHeight Maximum allowed height
     * @param fixedAspectRatio Whether to maintain fixed aspect ratio (default: false)
     */
    void initialize(juce::AudioProcessorEditor* editor,
                    juce::AudioProcessor* processor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight,
                    bool fixedAspectRatio = false)
    {
        if (editor == nullptr)
            return;

        if (defaultWidth <= 0 || defaultHeight <= 0)
        {
            jassertfalse; // Invalid dimensions
            return;
        }

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

        // Load stored size from plugin state (if available)
        loadStoredSize();

        // Configure constrainer with min/max
        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
        if (fixedAspectRatio)
            constrainer.setFixedAspectRatio(baseWidth / baseHeight);

        // Create the corner resize handle
        resizer = std::make_unique<juce::ResizableCornerComponent>(editor, &constrainer);
        editor->addAndMakeVisible(resizer.get());
        resizer->setAlwaysOnTop(true);

        // Enable resizing in JUCE - IMPORTANT: setResizeLimits tells the DAW the constraints
        editor->setResizable(true, true);
        editor->setResizeLimits(minWidth, minHeight, maxWidth, maxHeight);
    }

    /**
     * Legacy initialize without processor (no persistence).
     * This overload truly disables persistence by not calling loadStoredSize()/saveCurrentSize().
     */
    void initialize(juce::AudioProcessorEditor* editor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight)
    {
        if (editor == nullptr)
            return;

        if (defaultWidth <= 0 || defaultHeight <= 0)
        {
            jassertfalse; // Invalid dimensions
            return;
        }

        parentEditor = editor;
        audioProcessor = nullptr;  // Explicitly null - no persistence
        persistenceEnabled = false;  // Disable persistence for legacy mode
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

        // Configure constrainer with min/max and fixed aspect ratio for legacy mode
        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
        constrainer.setFixedAspectRatio(baseWidth / baseHeight);

        // Create the corner resize handle
        resizer = std::make_unique<juce::ResizableCornerComponent>(editor, &constrainer);
        editor->addAndMakeVisible(resizer.get());
        resizer->setAlwaysOnTop(true);

        // Enable resizing in JUCE
        editor->setResizable(true, true);
        editor->setResizeLimits(minWidth, minHeight, maxWidth, maxHeight);
    }

    /**
     * Get the stored width (or default if none stored).
     * Call this before setSize() in your constructor.
     */
    int getStoredWidth() const { return storedWidth; }

    /**
     * Get the stored height (or default if none stored).
     * Call this before setSize() in your constructor.
     */
    int getStoredHeight() const { return storedHeight; }

    /**
     * Call this at the start of your resized() method.
     * Positions the resize handle and calculates the new scale factor.
     */
    void updateResizer()
    {
        if (parentEditor == nullptr)
            return;

        // Position the resize handle in bottom-right corner
        if (resizer)
        {
            const int handleSize = 16;
            resizer->setBounds(parentEditor->getWidth() - handleSize,
                               parentEditor->getHeight() - handleSize,
                               handleSize, handleSize);
        }

        // Calculate scale factor based on WIDTH only
        // This allows height to change (e.g., collapsible sections) without affecting scale
        scaleFactor = static_cast<float>(parentEditor->getWidth()) / baseWidth;
    }

    /**
     * Get the current scale factor for sizing components.
     * Multiply your base sizes by this value.
     *
     * Example:
     *   int knobSize = static_cast<int>(75 * getScaleFactor());
     */
    float getScaleFactor() const { return scaleFactor; }

    /**
     * Convenience method to scale an integer value.
     */
    int scaled(int value) const
    {
        return static_cast<int>(static_cast<float>(value) * scaleFactor);
    }

    /**
     * Convenience method to scale a float value.
     */
    float scaled(float value) const
    {
        return value * scaleFactor;
    }

    /**
     * Get the constrainer for custom configuration.
     */
    juce::ComponentBoundsConstrainer& getConstrainer() { return constrainer; }

private:
    juce::AudioProcessorEditor* parentEditor = nullptr;
    juce::AudioProcessor* audioProcessor = nullptr;
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    float baseWidth = 800.0f;
    float baseHeight = 600.0f;
    float scaleFactor = 1.0f;
    bool persistenceEnabled = true;  // False for legacy mode (no processor)

    // Size constraints and defaults
    int defaultW = 800;
    int defaultH = 600;
    int minW = 640;
    int minH = 480;
    int maxW = 1920;
    int maxH = 1200;
    int storedWidth = 800;
    int storedHeight = 600;

    // State persistence keys
    static constexpr const char* kWindowWidth = "windowWidth";
    static constexpr const char* kWindowHeight = "windowHeight";

    /**
     * Load stored window size from application properties.
     */
    void loadStoredSize()
    {
        storedWidth = defaultW;
        storedHeight = defaultH;

        // Skip persistence if disabled (legacy mode with no processor)
        if (!persistenceEnabled)
            return;

        // Use application properties for persistence
        auto* props = getAppProperties();
        if (props == nullptr)
            return;

        auto* userSettings = props->getUserSettings();
        if (userSettings == nullptr)
            return;

        juce::String prefix = getPluginPrefix();
        storedWidth = userSettings->getIntValue(prefix + kWindowWidth, defaultW);
        storedHeight = userSettings->getIntValue(prefix + kWindowHeight, defaultH);

        // Clamp to valid range
        storedWidth = juce::jlimit(minW, maxW, storedWidth);
        storedHeight = juce::jlimit(minH, maxH, storedHeight);
    }

    /**
     * Save current window size to application properties.
     */
    void saveCurrentSize()
    {
        if (parentEditor == nullptr)
            return;

        // Skip persistence if disabled (legacy mode with no processor)
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

    /**
     * Get application properties for persistent storage.
     */
    juce::ApplicationProperties* getAppProperties()
    {
        static juce::ApplicationProperties appProps;
        static bool initialized = false;

        if (!initialized)
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "LunaCoAudio";
            options.folderName = "LunaCoAudio";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            appProps.setStorageParameters(options);
            initialized = true;
        }

        return &appProps;
    }

    /**
     * Get a unique prefix for this plugin's settings.
     */
    juce::String getPluginPrefix() const
    {
        if (audioProcessor != nullptr)
            return audioProcessor->getName() + "_";
        return "Plugin_";
    }

public:
    /**
     * Call this when the editor is being destroyed to save the size.
     * Put this in your editor's destructor.
     */
    void saveSize()
    {
        saveCurrentSize();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableEditorHelper)
};
