#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>
#include "GrooveTemplateGenerator.h"

/**
 * GrooveFollower - Real-time groove smoothing and interpolation
 *
 * Maintains a buffer of recent GrooveTemplates and provides smooth
 * interpolation for real-time use. Prevents jarring changes in feel
 * when the input groove changes.
 */
class GrooveFollower
{
public:
    GrooveFollower();
    ~GrooveFollower() = default;

    /**
     * Update with a new groove template
     * @param newGroove The newly extracted groove template
     */
    void update(const GrooveTemplate& newGroove);

    /**
     * Get the current smoothed groove template
     * @param playheadBars Current playhead position in bars (for lookahead)
     * @return Smoothed groove template
     */
    GrooveTemplate getCurrent(double playheadBars = 0.0) const;

    /**
     * Get the "groove lock" percentage (confidence in the current groove)
     * Based on note count and consistency
     * @return Lock percentage (0.0 - 100.0)
     */
    float getLockPercentage() const;

    /**
     * Check if the groove is considered "locked" (stable enough to use)
     * @return true if groove is stable
     */
    bool isLocked() const;

    /**
     * Set the smoothing factor
     * @param alpha Smoothing factor (0.0 = no smoothing, 1.0 = instant changes)
     */
    void setSmoothingFactor(float alpha);

    /**
     * Set the minimum note count for groove lock
     * @param count Minimum notes required
     */
    void setMinNotesForLock(int count);

    /**
     * Reset the follower state
     */
    void reset();

    /**
     * Get the raw (unsmoothed) current template
     */
    const GrooveTemplate& getRawTemplate() const { return currentTemplate; }

private:
    // Buffer of recent templates (for lookahead and analysis)
    static constexpr int TEMPLATE_BUFFER_SIZE = 4;
    std::deque<GrooveTemplate> templateBuffer;

    // Current smoothed template
    GrooveTemplate currentTemplate;

    // Smoothing parameters
    float smoothingAlpha = 0.3f;        // Exponential smoothing factor
    int minNotesForLock = 8;            // Minimum notes to consider "locked"

    // Lock state
    float lockPercentage = 0.0f;
    bool locked = false;

    // Consistency tracking
    float lastSwing8 = 0.0f;
    float lastSwing16 = 0.0f;
    float swingConsistency = 0.0f;

    // Helper methods
    void smoothTemplate(const GrooveTemplate& newTemplate);
    void updateLockState();
    float calculateConsistency(const GrooveTemplate& a, const GrooveTemplate& b) const;
};