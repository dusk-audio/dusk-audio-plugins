/*
  ==============================================================================

    XYPad.h
    Logic Pro Drummer-style XY pad for complexity/loudness control

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class XYPad : public juce::Component
{
public:
    XYPad(juce::AudioProcessorValueTreeState& apvts,
          const juce::String& xParamId,
          const juce::String& yParamId)
        : xParam(apvts.getParameter(xParamId)),
          yParam(apvts.getParameter(yParamId))
    {
        jassert(xParam != nullptr);
        jassert(yParam != nullptr);

        // Attach to parameters
        xAttachment = std::make_unique<juce::ParameterAttachment>(
            *xParam, [this](float v) { xValue = v; repaint(); }, nullptr);
        yAttachment = std::make_unique<juce::ParameterAttachment>(
            *yParam, [this](float v) { yValue = v; repaint(); }, nullptr);

        xValue = xParam->getValue();
        yValue = yParam->getValue();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        // Background
        g.setColour(juce::Colour(0xff2a2a32));
        g.fillRoundedRectangle(bounds, 8.0f);

        // Border
        g.setColour(juce::Colour(0xff4a4a54));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);

        // Grid lines
        g.setColour(juce::Colour(0xff3a3a44));
        float centerX = bounds.getCentreX();
        float centerY = bounds.getCentreY();
        g.drawLine(centerX, bounds.getY(), centerX, bounds.getBottom(), 0.5f);
        g.drawLine(bounds.getX(), centerY, bounds.getRight(), centerY, 0.5f);

        // Axis labels
        g.setColour(juce::Colour(0xff666677));
        g.setFont(10.0f);
        g.drawText("Simple", bounds.getX() + 5, bounds.getBottom() - 18, 50, 15, juce::Justification::left);
        g.drawText("Complex", bounds.getRight() - 55, bounds.getBottom() - 18, 50, 15, juce::Justification::right);
        g.drawText("Soft", bounds.getX() + 5, bounds.getY() + 3, 40, 15, juce::Justification::left);
        g.drawText("Loud", bounds.getX() + 5, bounds.getBottom() - 35, 40, 15, juce::Justification::left);

        // Calculate handle position
        float handleX = bounds.getX() + xValue * bounds.getWidth();
        float handleY = bounds.getBottom() - yValue * bounds.getHeight();

        // Handle glow
        juce::ColourGradient glow(juce::Colour(0x4488aaff), handleX, handleY,
                                   juce::Colours::transparentBlack, handleX + 30, handleY + 30, true);
        g.setGradientFill(glow);
        g.fillEllipse(handleX - 25, handleY - 25, 50, 50);

        // Handle
        g.setColour(juce::Colour(0xff5588cc));
        g.fillEllipse(handleX - 12, handleY - 12, 24, 24);

        // Handle highlight
        g.setColour(juce::Colour(0xff88aadd));
        g.drawEllipse(handleX - 12, handleY - 12, 24, 24, 2.0f);

        // Inner dot
        g.setColour(juce::Colours::white);
        g.fillEllipse(handleX - 4, handleY - 4, 8, 8);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        updateFromMouse(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        updateFromMouse(e);
    }

private:
    void updateFromMouse(const juce::MouseEvent& e)
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        float newX = juce::jlimit(0.0f, 1.0f, (e.position.x - bounds.getX()) / bounds.getWidth());
        float newY = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - bounds.getY()) / bounds.getHeight());

        xParam->setValueNotifyingHost(newX);
        yParam->setValueNotifyingHost(newY);

        xValue = newX;
        yValue = newY;
        repaint();
    }

    juce::RangedAudioParameter* xParam;
    juce::RangedAudioParameter* yParam;

    std::unique_ptr<juce::ParameterAttachment> xAttachment;
    std::unique_ptr<juce::ParameterAttachment> yAttachment;

    float xValue = 0.5f;
    float yValue = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};
