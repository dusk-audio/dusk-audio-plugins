/*
  ==============================================================================

    TapeVisualization.h
    Tape Echo - Animated Tape Loop Visualization

    Shows tape movement synced to repeat rate, with glowing dots for active heads.
    Feedback indicator glows warmer as intensity increases.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "TapeEchoLookAndFeel.h"
#include <array>

class TapeVisualization : public juce::Component, private juce::Timer
{
public:
    TapeVisualization()
    {
        startTimerHz(60);  // 60 FPS animation
    }

    ~TapeVisualization() override
    {
        stopTimer();
    }

    void setTapeSpeed(float speed)
    {
        tapeSpeed = speed;
    }

    void setHeadActive(int headIndex, bool active)
    {
        if (headIndex >= 0 && headIndex < 3)
            headActive[headIndex] = active;
    }

    void setFeedbackIntensity(float intensity)
    {
        feedbackIntensity = juce::jlimit(0.0f, 1.1f, intensity);
    }

    void setCurrentMode(int mode)
    {
        currentMode = mode;
    }

    void setIsPlaying(bool playing)
    {
        isPlaying = playing;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(10.0f);

        // Background panel
        g.setColour(TapeEchoLookAndFeel::darkBgColor);
        g.fillRoundedRectangle(bounds, 8.0f);

        // Border
        g.setColour(TapeEchoLookAndFeel::secondaryColor.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);

        const float centreX = bounds.getCentreX();
        const float centreY = bounds.getCentreY();

        // Draw tape reels
        const float reelRadius = bounds.getHeight() * 0.35f;
        const float reelSpacing = bounds.getWidth() * 0.35f;

        drawReel(g, centreX - reelSpacing, centreY, reelRadius, true);
        drawReel(g, centreX + reelSpacing, centreY, reelRadius, false);

        // Draw tape path connecting reels
        drawTapePath(g, centreX, centreY, reelRadius, reelSpacing);

        // Draw playback heads
        drawHeads(g, centreX, centreY, bounds);

        // Draw mode indicator
        drawModeIndicator(g, bounds);
    }

private:
    float tapeSpeed = 1.0f;
    std::array<bool, 3> headActive = { true, false, false };
    float feedbackIntensity = 0.3f;
    int currentMode = 1;
    float tapePhase = 0.0f;
    float headPulsePhase = 0.0f;
    bool isPlaying = false;
    float reelRotation = 0.0f;

    void timerCallback() override
    {
        // Only animate reels when DAW transport is playing
        if (isPlaying)
        {
            // Update reel rotation based on tape speed
            reelRotation += 0.1f * tapeSpeed;
            if (reelRotation >= juce::MathConstants<float>::twoPi)
                reelRotation -= juce::MathConstants<float>::twoPi;

            // Update tape animation phase for motion lines
            tapePhase += 0.02f * tapeSpeed;
            if (tapePhase >= 1.0f)
                tapePhase -= 1.0f;
        }

        // Update head pulse animation (always active when heads are on)
        headPulsePhase += 0.05f;
        if (headPulsePhase >= juce::MathConstants<float>::twoPi)
            headPulsePhase -= juce::MathConstants<float>::twoPi;

        repaint();
    }

    void drawReel(juce::Graphics& g, float x, float y, float radius, bool isSupply)
    {
        // Fixed tape amount (no animation) - matches TapeMachine style
        const float tapeAmount = 0.5f;  // Medium tape amount, shows spokes
        float tapeRadius = radius * (0.25f + tapeAmount * 0.6f);

        // Outer reel housing shadow
        g.setColour(juce::Colour(0x90000000));
        g.fillEllipse(x - radius + 3, y - radius + 3, radius * 2, radius * 2);

        // Metal reel flange with gradient (the outer silver ring)
        juce::ColourGradient flangeGradient(
            juce::Colour(0xff8a8078), x - radius, y - radius,
            juce::Colour(0xff4a4540), x + radius, y + radius, true);
        g.setGradientFill(flangeGradient);
        g.fillEllipse(x - radius, y - radius, radius * 2, radius * 2);

        // Inner flange ring
        g.setColour(juce::Colour(0xff3a3530));
        g.drawEllipse(x - radius, y - radius, radius * 2, radius * 2, 2.0f);

        // Tape pack - dark brown/black with subtle gradient to show depth
        // Tape shadow (depth effect)
        g.setColour(juce::Colour(0xff0a0808));
        g.fillEllipse(x - tapeRadius - 1, y - tapeRadius + 1,
                      tapeRadius * 2 + 2, tapeRadius * 2);

        // Main tape pack with subtle radial gradient
        juce::ColourGradient tapeGradient(
            juce::Colour(0xff2a2420), x, y,
            juce::Colour(0xff1a1510), x, y - tapeRadius, true);
        g.setGradientFill(tapeGradient);
        g.fillEllipse(x - tapeRadius, y - tapeRadius,
                      tapeRadius * 2, tapeRadius * 2);

        // Tape edge highlight (shiny tape surface)
        g.setColour(juce::Colour(0x30ffffff));
        g.drawEllipse(x - tapeRadius + 2, y - tapeRadius + 2,
                      tapeRadius * 2 - 4, tapeRadius * 2 - 4, 1.0f);

        // Reel spokes (visible through the tape hub area, animated with reelRotation)
        // Both reels spin clockwise (tape moves left to right)
        float hubRadius = radius * 0.22f;
        float spokeAngle = reelRotation;
        juce::ignoreUnused(isSupply);
        g.setColour(juce::Colour(0xff5a4a3a));

        for (int i = 0; i < 3; ++i)
        {
            float angle = spokeAngle + (i * 2.0f * juce::MathConstants<float>::pi / 3.0f);

            juce::Path spoke;
            float spokeLength = radius * 0.72f;
            float spokeWidth = 6.0f;
            spoke.addRoundedRectangle(-spokeLength, -spokeWidth / 2, spokeLength * 2, spokeWidth, 2.0f);
            spoke.applyTransform(juce::AffineTransform::rotation(angle).translated(x, y));

            // Only draw spoke portions visible outside the tape
            g.saveState();
            juce::Path clipPath;
            clipPath.addEllipse(x - radius, y - radius, radius * 2, radius * 2);
            clipPath.setUsingNonZeroWinding(false);
            clipPath.addEllipse(x - tapeRadius, y - tapeRadius,
                               tapeRadius * 2, tapeRadius * 2);
            g.reduceClipRegion(clipPath);
            g.fillPath(spoke);
            g.restoreState();
        }

        // Center hub with metallic finish
        juce::ColourGradient hubGradient(
            juce::Colour(0xffa09080), x - hubRadius, y - hubRadius,
            juce::Colour(0xff4a4038), x + hubRadius, y + hubRadius, false);
        g.setGradientFill(hubGradient);
        g.fillEllipse(x - hubRadius, y - hubRadius, hubRadius * 2, hubRadius * 2);

        // Hub ring detail
        g.setColour(juce::Colour(0xff3a3028));
        g.drawEllipse(x - hubRadius, y - hubRadius, hubRadius * 2, hubRadius * 2, 1.5f);

        // Center spindle hole
        float holeRadius = 4.0f;
        g.setColour(juce::Colour(0xff0a0a08));
        g.fillEllipse(x - holeRadius, y - holeRadius, holeRadius * 2, holeRadius * 2);

        // Spindle highlight
        g.setColour(juce::Colour(0x20ffffff));
        g.fillEllipse(x - holeRadius + 1, y - holeRadius + 1, holeRadius, holeRadius);
    }

    void drawTapePath(juce::Graphics& g, float centreX, float centreY, float reelRadius, float reelSpacing)
    {
        // Draw tape connecting the reels
        g.setColour(juce::Colour(0xff2a2a2a));  // Tape color

        // Top path (with slight curve for tension)
        juce::Path tapePath;

        float leftReelX = centreX - reelSpacing;
        float rightReelX = centreX + reelSpacing;
        float topY = centreY - reelRadius * 0.5f;

        // From left reel to right reel (top)
        tapePath.startNewSubPath(leftReelX + reelRadius * 0.8f, topY);

        // Curve through head area
        tapePath.quadraticTo(centreX, topY - 15.0f, rightReelX - reelRadius * 0.8f, topY);

        g.strokePath(tapePath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        // Bottom path
        juce::Path bottomPath;
        float bottomY = centreY + reelRadius * 0.5f;
        bottomPath.startNewSubPath(leftReelX + reelRadius * 0.8f, bottomY);
        bottomPath.lineTo(rightReelX - reelRadius * 0.8f, bottomY);
        g.strokePath(bottomPath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

        // Draw animated tape motion lines
        float motionOffset = tapePhase * 20.0f;
        g.setColour(juce::Colours::white.withAlpha(0.1f));

        for (int i = 0; i < 5; ++i)
        {
            float xPos = leftReelX + reelRadius + motionOffset + i * 30.0f;
            if (xPos < rightReelX - reelRadius)
            {
                g.drawLine(xPos, topY - 12.0f, xPos + 5.0f, topY - 12.0f, 1.0f);
            }
        }
    }

    void drawHeads(juce::Graphics& g, float centreX, float centreY, juce::Rectangle<float>& bounds)
    {
        // Head positions (spaced evenly across the top tape path)
        const float headY = centreY - bounds.getHeight() * 0.25f;
        const float headSpacing = bounds.getWidth() * 0.15f;

        std::array<float, 3> headX = {
            centreX - headSpacing,
            centreX,
            centreX + headSpacing
        };

        for (int i = 0; i < 3; ++i)
        {
            drawHead(g, headX[i], headY, headActive[i], i + 1);
        }
    }

    void drawHead(juce::Graphics& g, float x, float y, bool active, int headNumber)
    {
        const float headSize = 14.0f;  // Slightly larger for better visibility

        // Head housing - always visible with better contrast
        g.setColour(TapeEchoLookAndFeel::secondaryColor);
        g.fillRoundedRectangle(x - headSize, y - headSize * 0.8f, headSize * 2, headSize * 1.6f, 4.0f);

        // Border for definition
        g.setColour(TapeEchoLookAndFeel::primaryColor.brighter(0.2f));
        g.drawRoundedRectangle(x - headSize, y - headSize * 0.8f, headSize * 2, headSize * 1.6f, 4.0f, 1.5f);

        if (active)
        {
            // Animated glow when active
            float pulseAmount = (std::sin(headPulsePhase) + 1.0f) * 0.5f;
            juce::Colour glowColour = TapeEchoLookAndFeel::accentColor.interpolatedWith(
                TapeEchoLookAndFeel::highlightColor, feedbackIntensity);

            // Outer glow
            g.setColour(glowColour.withAlpha(0.4f + pulseAmount * 0.3f));
            g.fillEllipse(x - headSize * 0.7f, y - headSize * 0.7f, headSize * 1.4f, headSize * 1.4f);

            // Inner bright dot
            g.setColour(glowColour);
            g.fillEllipse(x - 4, y - 4, 8, 8);
        }
        else
        {
            // Inactive - dim but visible indicator
            g.setColour(TapeEchoLookAndFeel::primaryColor.withAlpha(0.6f));
            g.fillEllipse(x - 3, y - 3, 6, 6);
        }

        // Head number label - below the head housing
        g.setColour(active ? TapeEchoLookAndFeel::accentColor : TapeEchoLookAndFeel::textColor.withAlpha(0.5f));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText(juce::String(headNumber), static_cast<int>(x - 8), static_cast<int>(y + headSize + 2),
                   16, 14, juce::Justification::centred);
    }

    void drawModeIndicator(juce::Graphics& g, juce::Rectangle<float>& bounds)
    {
        // Mode text centered between the reels (bottom center)
        g.setColour(TapeEchoLookAndFeel::textColor.withAlpha(0.8f));
        g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));

        juce::String modeText = getModeDescription(currentMode);

        // Center the text horizontally in the bottom portion
        auto labelArea = bounds.removeFromBottom(22.0f);
        g.drawText(modeText, labelArea, juce::Justification::centred, true);
    }

    juce::String getModeDescription(int mode) const
    {
        // Descriptive mode labels that explain what each mode actually does
        // Modes 1-6: Single and dual head combinations
        // Modes 7-11: All heads with different feedback routing
        // Mode 12: Reverb only
        switch (mode)
        {
            case 1:  return "HEAD 1 - SHORT";      // Short delay only
            case 2:  return "HEAD 2 - MEDIUM";     // Medium delay only
            case 3:  return "HEAD 3 - LONG";       // Long delay only
            case 4:  return "HEADS 1+2";           // Short + Medium
            case 5:  return "HEADS 1+3";           // Short + Long
            case 6:  return "HEADS 2+3";           // Medium + Long
            case 7:  return "ALL - STANDARD";      // All heads, feedback from head 3
            case 8:  return "ALL - TIGHT";         // All heads, feedback from head 1
            case 9:  return "ALL - BALANCED";      // All heads, feedback from head 2
            case 10: return "ALL - COMPLEX";       // All heads, mixed feedback (1+3)
            case 11: return "ALL - DENSE";         // All heads, cascade feedback
            case 12: return "REVERB ONLY";         // Reverb, no echo
            default: return "MODE " + juce::String(mode);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeVisualization)
};
