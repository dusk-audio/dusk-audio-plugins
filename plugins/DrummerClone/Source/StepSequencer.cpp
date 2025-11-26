#include "StepSequencer.h"

StepSequencer::StepSequencer()
{
    // Initialize pattern to empty
    clearAllSteps();

    // Start timer for playhead animation (60 fps)
    startTimerHz(60);
}

StepSequencer::~StepSequencer()
{
    stopTimer();
}

void StepSequencer::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Background
    g.setColour(juce::Colour(35, 35, 40));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Draw header with step numbers
    auto headerArea = bounds.removeFromTop(headerHeight);
    headerArea.removeFromLeft(labelWidth);

    g.setColour(juce::Colours::grey);
    g.setFont(10.0f);

    for (int step = 0; step < 16; ++step)
    {
        auto stepArea = headerArea.removeFromLeft(stepWidth);
        // Mark beat boundaries (1, 5, 9, 13)
        if (step % 4 == 0)
        {
            g.setColour(juce::Colours::white);
        }
        else
        {
            g.setColour(juce::Colours::grey);
        }
        g.drawText(juce::String(step + 1), stepArea, juce::Justification::centred);
    }

    // Draw lanes
    for (int lane = 0; lane < NumLanes; ++lane)
    {
        auto laneArea = bounds.removeFromTop(laneHeight);
        auto labelArea = laneArea.removeFromLeft(labelWidth);

        // Lane label
        g.setColour(getLaneColor(lane));
        g.setFont(11.0f);
        g.drawText(laneNames[lane], labelArea.reduced(4, 0), juce::Justification::centredRight);

        // Draw steps
        for (int step = 0; step < 16; ++step)
        {
            auto stepArea = laneArea.removeFromLeft(stepWidth);
            auto cellBounds = stepArea.reduced(2);

            // Background for beat boundaries
            if (step % 4 == 0)
            {
                g.setColour(juce::Colour(50, 50, 55));
            }
            else
            {
                g.setColour(juce::Colour(40, 40, 45));
            }
            g.fillRect(cellBounds);

            // Cell border
            g.setColour(juce::Colour(55, 55, 60));
            g.drawRect(cellBounds);

            // Active step
            if (pattern[lane][step].active)
            {
                auto laneColor = getLaneColor(lane);
                float vel = pattern[lane][step].velocity;

                // Velocity affects brightness
                auto activeColor = laneColor.withAlpha(0.4f + vel * 0.6f);
                g.setColour(activeColor);

                auto activeArea = cellBounds.reduced(2);
                g.fillRoundedRectangle(activeArea.toFloat(), 2.0f);

                // Velocity bar at bottom
                g.setColour(laneColor);
                int barHeight = static_cast<int>(vel * (activeArea.getHeight() - 2));
                g.fillRect(activeArea.getX(), activeArea.getBottom() - barHeight,
                           activeArea.getWidth(), barHeight);
            }

            // Playhead indicator
            if (step == currentStep)
            {
                g.setColour(juce::Colours::white.withAlpha(0.3f));
                g.fillRect(cellBounds);
            }
        }
    }

    // Draw grid lines for beat divisions
    int gridX = labelWidth;
    g.setColour(juce::Colour(60, 60, 70));
    for (int beat = 1; beat < 4; ++beat)
    {
        int x = gridX + (beat * 4 * stepWidth);
        g.drawLine(static_cast<float>(x), static_cast<float>(headerHeight),
                   static_cast<float>(x), static_cast<float>(getHeight()), 1.0f);
    }
}

void StepSequencer::resized()
{
    // Nothing special needed
}

void StepSequencer::mouseDown(const juce::MouseEvent& e)
{
    auto [lane, step] = getStepLaneFromPosition(e.getPosition());

    if (lane >= 0 && lane < NumLanes && step >= 0 && step < 16)
    {
        // Toggle step on click
        pattern[lane][step].active = !pattern[lane][step].active;

        if (pattern[lane][step].active)
        {
            pattern[lane][step].velocity = 0.8f;  // Default velocity
        }

        // Set up for velocity drag
        isDragging = true;
        dragLane = lane;
        dragStep = step;
        dragStartY = e.getPosition().getY();
        dragStartVelocity = pattern[lane][step].velocity;

        repaint();

        if (onPatternChanged)
            onPatternChanged();
    }
}

void StepSequencer::mouseDrag(const juce::MouseEvent& e)
{
    if (isDragging && dragLane >= 0 && dragStep >= 0)
    {
        if (pattern[dragLane][dragStep].active)
        {
            // Adjust velocity based on vertical drag
            float deltaY = dragStartY - e.getPosition().getY();
            float velocityDelta = deltaY / 50.0f;  // 50 pixels = full velocity range

            float newVelocity = juce::jlimit(0.1f, 1.0f, dragStartVelocity + velocityDelta);
            pattern[dragLane][dragStep].velocity = newVelocity;

            repaint();

            if (onPatternChanged)
                onPatternChanged();
        }
    }
}

void StepSequencer::mouseUp(const juce::MouseEvent& /*e*/)
{
    isDragging = false;
    dragLane = -1;
    dragStep = -1;
}

void StepSequencer::timerCallback()
{
    // Repaint for playhead animation
    // In a real implementation, this would sync with DAW transport
    repaint();
}

void StepSequencer::setPlayheadPosition(int step)
{
    if (step != currentStep)
    {
        currentStep = step;
        repaint();
    }
}

bool StepSequencer::isStepActive(int lane, int step) const
{
    if (lane >= 0 && lane < NumLanes && step >= 0 && step < 16)
    {
        return pattern[lane][step].active;
    }
    return false;
}

float StepSequencer::getStepVelocity(int lane, int step) const
{
    if (lane >= 0 && lane < NumLanes && step >= 0 && step < 16)
    {
        return pattern[lane][step].velocity;
    }
    return 0.0f;
}

void StepSequencer::setStep(int lane, int step, bool active, float velocity)
{
    if (lane >= 0 && lane < NumLanes && step >= 0 && step < 16)
    {
        pattern[lane][step].active = active;
        pattern[lane][step].velocity = juce::jlimit(0.0f, 1.0f, velocity);
        repaint();
    }
}

void StepSequencer::clearAllSteps()
{
    for (auto& lane : pattern)
    {
        for (auto& step : lane)
        {
            step.active = false;
            step.velocity = 0.8f;
        }
    }
    repaint();
}

juce::Colour StepSequencer::getLaneColor(int lane) const
{
    switch (lane)
    {
        case Kick:        return juce::Colour(255, 100, 100);  // Red
        case Snare:       return juce::Colour(100, 180, 255);  // Blue
        case ClosedHiHat: return juce::Colour(255, 220, 100);  // Yellow
        case OpenHiHat:   return juce::Colour(255, 180, 80);   // Orange
        case Clap:        return juce::Colour(200, 100, 255);  // Purple
        case Tom1:        return juce::Colour(100, 255, 150);  // Green
        case Tom2:        return juce::Colour(80, 200, 120);   // Dark green
        case Crash:       return juce::Colour(255, 150, 200);  // Pink
        default:          return juce::Colours::white;
    }
}

std::pair<int, int> StepSequencer::getStepLaneFromPosition(juce::Point<int> pos) const
{
    int x = pos.getX();
    int y = pos.getY();

    // Check if in step area
    if (x < labelWidth || y < headerHeight)
    {
        return {-1, -1};
    }

    int step = (x - labelWidth) / stepWidth;
    int lane = (y - headerHeight) / laneHeight;

    if (step >= 0 && step < 16 && lane >= 0 && lane < NumLanes)
    {
        return {lane, step};
    }

    return {-1, -1};
}
