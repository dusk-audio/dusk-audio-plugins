#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Header bar with plugin title, FFT/Channel dropdowns, gear button, and branding.
*/
class HeaderBar : public juce::Component
{
public:
    HeaderBar()
    {
        // FFT Resolution
        fftCombo.addItem("2048", 1);
        fftCombo.addItem("4096", 2);
        fftCombo.addItem("8192", 3);
        fftCombo.setSelectedId(2);
        addAndMakeVisible(fftCombo);

        // Channel Mode
        channelCombo.addItem("Stereo", 1);
        channelCombo.addItem("Mono", 2);
        channelCombo.addItem("Mid", 3);
        channelCombo.addItem("Side", 4);
        addAndMakeVisible(channelCombo);

        // Gear button (painted manually in paint(), click handled here)
        gearButton.setButtonText("");
        gearButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        gearButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
        addAndMakeVisible(gearButton);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(15, 6);

        // Title takes left portion (handled by paint)
        bounds.removeFromLeft(200);

        // Right side: Dusk Audio text space
        bounds.removeFromRight(80);

        // Gear button
        auto gearArea = bounds.removeFromRight(38);
        gearButton.setBounds(gearArea);
        bounds.removeFromRight(8);

        // Channel combo
        channelCombo.setBounds(bounds.removeFromRight(85).reduced(0, 3));
        bounds.removeFromRight(8);

        // FFT combo
        fftCombo.setBounds(bounds.removeFromRight(80).reduced(0, 3));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff252525));

        auto bounds = getLocalBounds().reduced(15, 0);

        // Title
        g.setColour(juce::Colour(0xff00aaff));
        g.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
        g.drawText("SPECTRUM ANALYZER", bounds.removeFromLeft(200),
            juce::Justification::centredLeft);

        // Company name
        g.setColour(juce::Colour(0xff888888));
        g.setFont(14.0f);
        g.drawText("Dusk Audio", bounds, juce::Justification::centredRight);

        // Gear icon (drawn over the transparent button)
        g.setColour(juce::Colour(0xff888888));
        g.setFont(22.0f);
        g.drawText(juce::String::charToString(0x2699), gearButton.getBounds(),
            juce::Justification::centred);

        // Bottom border
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    //==========================================================================
    juce::ComboBox& getFFTCombo() { return fftCombo; }
    juce::ComboBox& getChannelCombo() { return channelCombo; }
    juce::TextButton& getGearButton() { return gearButton; }

    juce::Rectangle<int> getTitleArea() const
    {
        return getLocalBounds().reduced(15, 0).withWidth(200);
    }

private:
    juce::ComboBox fftCombo;
    juce::ComboBox channelCombo;
    juce::TextButton gearButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};
