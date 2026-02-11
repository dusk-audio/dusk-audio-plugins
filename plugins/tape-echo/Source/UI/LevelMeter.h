/*
  ==============================================================================

    LevelMeter.h
    Tape Echo - Level Meter Component

    Simple vertical level meter with peak hold and color-coded levels.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TapeEchoLookAndFeel.h"
#include <atomic>

class LevelMeter : public juce::Component, public juce::Timer
{
public:
    LevelMeter()
    {
        startTimerHz(60);
    }

    ~LevelMeter() override
    {
        stopTimer();
    }

    // Thread-safe: can be called from the audio thread
    void setLevel(float newLevel)
    {
        atomicLevel.store(juce::jlimit(0.0f, 1.0f, newLevel), std::memory_order_relaxed);
    }

    void timerCallback() override
    {
        float newLevel = atomicLevel.load(std::memory_order_relaxed);

        if (newLevel != level)
        {
            level = newLevel;

            // Peak hold with decay
            if (level > peakLevel)
            {
                peakLevel = level;
                peakHoldCounter = 30;  // Hold for ~0.5s at 60fps
            }
            else if (peakHoldCounter > 0)
            {
                peakHoldCounter--;
            }
            else
            {
                peakLevel = peakLevel * 0.95f;  // Decay
            }

            repaint();
        }
        else if (peakHoldCounter > 0 || peakLevel > 0.01f)
        {
            // Continue decay even when level is stable
            if (peakHoldCounter > 0)
            {
                peakHoldCounter--;
            }
            else
            {
                peakLevel = peakLevel * 0.95f;
            }
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1);

        // Background
        g.setColour(TapeEchoLookAndFeel::darkBgColor);
        g.fillRoundedRectangle(bounds, 2.0f);

        // Level bar
        float meterHeight = bounds.getHeight() * level;
        auto meterBounds = bounds.withTrimmedTop(bounds.getHeight() - meterHeight);

        // Green to amber to red gradient based on level
        juce::Colour meterColour = level < 0.7f ? TapeEchoLookAndFeel::accentColor
                                  : level < 0.9f ? TapeEchoLookAndFeel::highlightColor
                                  : juce::Colours::red;
        g.setColour(meterColour);
        g.fillRoundedRectangle(meterBounds, 2.0f);

        // Peak indicator
        if (peakLevel > 0.01f)
        {
            float peakY = bounds.getY() + bounds.getHeight() * (1.0f - peakLevel);
            g.setColour(TapeEchoLookAndFeel::textColor);
            g.fillRect(bounds.getX(), peakY, bounds.getWidth(), 2.0f);
        }
    }

private:
    std::atomic<float> atomicLevel{0.0f};  // Thread-safe level from audio thread
    float level = 0.0f;                     // Display level (message thread only)
    float peakLevel = 0.0f;
    int peakHoldCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};
