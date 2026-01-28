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
 * 2. In constructor, after setSize():
 *    resizeHelper.initialize(this, defaultWidth, defaultHeight, minWidth, minHeight, maxWidth, maxHeight);
 *
 * 3. In resized():
 *    resizeHelper.updateResizer();
 *    float scale = resizeHelper.getScaleFactor();
 *    // Use scale to size your components proportionally
 *
 * This provides:
 * - Fixed aspect ratio to prevent distortion
 * - ResizableCornerComponent for drag-to-resize
 * - Automatic scale factor calculation
 */
class ScalableEditorHelper
{
public:
    ScalableEditorHelper() = default;
    ~ScalableEditorHelper() = default;

    /**
     * Initialize the resize system for a plugin editor.
     * Call this in your editor's constructor AFTER setSize().
     *
     * @param editor The AudioProcessorEditor to make resizable
     * @param defaultWidth Base width for scale calculations
     * @param defaultHeight Base height for scale calculations
     * @param minWidth Minimum allowed width
     * @param minHeight Minimum allowed height
     * @param maxWidth Maximum allowed width
     * @param maxHeight Maximum allowed height
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
        baseWidth = static_cast<float>(defaultWidth);
        baseHeight = static_cast<float>(defaultHeight);

        // Configure constrainer with min/max and fixed aspect ratio
        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
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

        // Calculate scale factor based on current size vs base size
        float widthScale = static_cast<float>(parentEditor->getWidth()) / baseWidth;
        float heightScale = static_cast<float>(parentEditor->getHeight()) / baseHeight;
        scaleFactor = juce::jmin(widthScale, heightScale);
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
    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    float baseWidth = 800.0f;
    float baseHeight = 600.0f;
    float scaleFactor = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableEditorHelper)
};
