#pragma once

#if __has_include(<JuceHeader.h>)
    #include <JuceHeader.h>
#else
    #include <juce_gui_basics/juce_gui_basics.h>
    #include <juce_graphics/juce_graphics.h>
#endif

#include "PatreonBackers.h"

// Patreon supporter credits overlay. Click title to show, click anywhere to dismiss.
class SupportersOverlay : public juce::Component
{
public:
    SupportersOverlay(const juce::String& pluginName = "", const juce::String& version = "")
        : pluginDisplayName(pluginName), pluginVersion(version)
    {
        setInterceptsMouseClicks(true, false);
    }

    void setPluginName(const juce::String& name) { pluginDisplayName = name; }
    void setVersion(const juce::String& version) { pluginVersion = version; }

    void paint(juce::Graphics& g) override
    {
        int w = getWidth();
        int h = getHeight();

        // Dark overlay covering everything
        g.setColour(juce::Colour(0xf0101010));
        g.fillRect(0, 0, w, h);

        // Panel area - centered
        int panelWidth = juce::jmin(600, w - 80);
        int panelHeight = juce::jmin(450, h - 80);
        auto panelBounds = juce::Rectangle<int>(
            (w - panelWidth) / 2,
            (h - panelHeight) / 2,
            panelWidth, panelHeight);

        // Panel background with gradient
        juce::ColourGradient panelGradient(
            juce::Colour(0xff2d2d2d), static_cast<float>(panelBounds.getX()), static_cast<float>(panelBounds.getY()),
            juce::Colour(0xff1a1a1a), static_cast<float>(panelBounds.getX()), static_cast<float>(panelBounds.getBottom()), false);
        g.setGradientFill(panelGradient);
        g.fillRoundedRectangle(panelBounds.toFloat(), 12.0f);

        // Panel border
        g.setColour(juce::Colour(0xff505050));
        g.drawRoundedRectangle(panelBounds.toFloat().reduced(0.5f), 12.0f, 2.0f);

        // Header
        g.setFont(juce::Font(juce::FontOptions(24.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffe8e8e8));
        g.drawText("Special Thanks", panelBounds.getX(), panelBounds.getY() + 25,
                   panelBounds.getWidth(), 32, juce::Justification::centred);

        // Subheading
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.setColour(juce::Colour(0xff909090));
        g.drawText("To our amazing supporters who make this plugin possible",
                   panelBounds.getX(), panelBounds.getY() + 60,
                   panelBounds.getWidth(), 20, juce::Justification::centred);

        // Divider line
        g.setColour(juce::Colour(0xff404040));
        g.fillRect(panelBounds.getX() + 40, panelBounds.getY() + 90, panelBounds.getWidth() - 80, 1);

        // Supporters list
        auto supportersText = PatreonCredits::getAllBackersFormatted();

        // Text area for supporters
        auto textArea = panelBounds.reduced(40, 0);
        textArea.setY(panelBounds.getY() + 105);
        textArea.setHeight(panelBounds.getHeight() - 170);

        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.setColour(juce::Colour(0xffd0d0d0));
        g.drawFittedText(supportersText, textArea,
                         juce::Justification::centred, 30);

        // Footer divider
        g.setColour(juce::Colour(0xff404040));
        g.fillRect(panelBounds.getX() + 40, panelBounds.getBottom() - 55, panelBounds.getWidth() - 80, 1);

        // Footer with click-to-close hint
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour(0xff808080));
        g.drawText("Click anywhere to close",
                   panelBounds.getX(), panelBounds.getBottom() - 45,
                   panelBounds.getWidth(), 20, juce::Justification::centred);

        // Dusk Audio credit with plugin name and version
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xff606060));
        juce::String creditText;
        if (pluginDisplayName.isEmpty())
            creditText = "by Dusk Audio";
        else if (pluginVersion.isEmpty())
            creditText = pluginDisplayName + " by Dusk Audio";
        else
            creditText = pluginDisplayName + " v" + pluginVersion + " by Dusk Audio";
        g.drawText(creditText,
                   panelBounds.getX(), panelBounds.getBottom() - 25,
                   panelBounds.getWidth(), 18, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;

private:
    juce::String pluginDisplayName;
    juce::String pluginVersion;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SupportersOverlay)
};
