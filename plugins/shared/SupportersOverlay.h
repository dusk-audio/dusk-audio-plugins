#pragma once

#if __has_include(<JuceHeader.h>)
    #include <JuceHeader.h>
#else
    #include <juce_gui_basics/juce_gui_basics.h>
    #include <juce_graphics/juce_graphics.h>
#endif

#include "PatreonBackers.h"

// Patreon supporter credits overlay. Click title to show, click anywhere to dismiss.
// Renders each tier with its own accent colour and dynamically sizes the panel
// to fit the content. Supports an optional action link (e.g. "Open log folder").
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

    // Optional action link shown above the "Click anywhere to close" hint.
    // When set, clicks inside its hit region fire onActionClick instead of
    // dismissing the overlay. Plugins use this for things like
    // "Open log folder" without each having its own About panel.
    void setActionLink(const juce::String& label, std::function<void()> onClick)
    {
        actionLabel = label;
        onActionClick = std::move(onClick);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        int w = getWidth();
        int h = getHeight();

        // Dark overlay covering everything
        g.setColour(juce::Colour(0xf0101010));
        g.fillRect(0, 0, w, h);

        // Measure content height to size panel dynamically
        const int contentHeight = measureContentHeight();
        const int headerHeight  = 95;   // title + subheading + divider
        const int actionHeight  = actionLabel.isNotEmpty() ? 20 : 0;
        const int footerHeight  = 55 + actionHeight;
        const int panelWidth    = juce::jmin(440, w - 80);
        const int panelHeight   = juce::jmin(headerHeight + contentHeight + footerHeight, h - 60);

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
        g.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
        g.setColour(juce::Colour(0xffe8e8e8));
        g.drawText("Special Thanks", panelBounds.getX(), panelBounds.getY() + 22,
                   panelBounds.getWidth(), 28, juce::Justification::centred);

        // Subheading
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(juce::Colour(0xff787878));
        g.drawText("To our amazing supporters who make this plugin possible",
                   panelBounds.getX(), panelBounds.getY() + 52,
                   panelBounds.getWidth(), 18, juce::Justification::centred);

        // Divider line
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRect(panelBounds.getX() + 30, panelBounds.getY() + 80, panelBounds.getWidth() - 60, 1);

        // Render each tier with its own accent colour
        int y = panelBounds.getY() + headerHeight;
        const int cx = panelBounds.getCentreX();
        const int textW = panelBounds.getWidth() - 60;
        const int maxY = panelBounds.getBottom() - footerHeight;

        auto drawTier = [&](const juce::String& tierName,
                            const std::vector<juce::String>& names,
                            const juce::Colour& headerColour,
                            bool isPast = false)
        {
            if (names.empty() || y >= maxY) return;

            // Tier heading
            g.setFont(juce::Font(juce::FontOptions(10.0f).withStyle("Bold")));
            g.setColour(headerColour);
            g.drawText(tierName, cx - textW / 2, y, textW, 16, juce::Justification::centred);
            y += 20;

            // Short accent line under heading
            const int lineW = 30;
            g.setColour(headerColour.withAlpha(0.3f));
            g.fillRect(cx - lineW / 2, y, lineW, 1);
            y += 8;

            // Names
            g.setFont(juce::Font(juce::FontOptions(isPast ? 12.0f : 13.0f)));
            g.setColour(isPast ? juce::Colour(0xff707070) : juce::Colour(0xffcccccc));
            for (const auto& name : names)
            {
                g.drawText(name, cx - textW / 2, y, textW, 18, juce::Justification::centred);
                y += 18;
            }

            y += 14; // gap between tiers
        };

        drawTier("CHAMPIONS",       PatreonCredits::champions,      juce::Colour(0xffffd700));
        drawTier("PATRONS",         PatreonCredits::patrons,        juce::Colour(0xff00aaff));
        drawTier("SUPPORTERS",      PatreonCredits::supporters,     juce::Colour(0xff6ac47e));
        drawTier("PAST SUPPORTERS", PatreonCredits::pastSupporters, juce::Colour(0xff606060), true);

        // Footer divider
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRect(panelBounds.getX() + 30, panelBounds.getBottom() - (footerHeight - 3),
                   panelBounds.getWidth() - 60, 1);

        // Optional action link (e.g. "Open log folder"); cached for hit-test in mouseDown.
        actionHitRegion = juce::Rectangle<int>{};
        if (actionLabel.isNotEmpty())
        {
            juce::Font linkFont(juce::FontOptions(12.0f));
            linkFont.setUnderline(true);
            g.setFont(linkFont);
            g.setColour(juce::Colour(0xffb89060));
            const juce::Rectangle<int> linkBounds(
                panelBounds.getX(), panelBounds.getBottom() - 64,
                panelBounds.getWidth(), 16);
            g.drawText(actionLabel, linkBounds, juce::Justification::centred);
            actionHitRegion = linkBounds.expanded(0, 4);
        }

        // Footer with click-to-close hint
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xff606060));
        g.drawText(actionLabel.isNotEmpty() ? "Click anywhere else to close"
                                            : "Click anywhere to close",
                   panelBounds.getX(), panelBounds.getBottom() - 44,
                   panelBounds.getWidth(), 18, juce::Justification::centred);

        // Dusk Audio credit with plugin name and version
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colour(0xff505050));
        juce::String creditText;
        if (pluginDisplayName.isEmpty())
            creditText = "by Dusk Audio";
        else if (pluginVersion.isEmpty())
            creditText = pluginDisplayName + " by Dusk Audio";
        else
            creditText = pluginDisplayName + " v" + pluginVersion + " by Dusk Audio";
        g.drawText(creditText,
                   panelBounds.getX(), panelBounds.getBottom() - 26,
                   panelBounds.getWidth(), 18, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onActionClick && actionHitRegion.contains(e.getPosition()))
        {
            onActionClick();
            return;
        }
        if (onDismiss)
            onDismiss();
    }

    std::function<void()> onDismiss;

private:
    int measureContentHeight() const
    {
        int h = 0;
        auto addTier = [&](const std::vector<juce::String>& names) {
            if (names.empty()) return;
            h += 20 + 8;                              // heading + accent line
            h += static_cast<int>(names.size()) * 18; // names
            h += 14;                                  // gap between tiers
        };
        addTier(PatreonCredits::champions);
        addTier(PatreonCredits::patrons);
        addTier(PatreonCredits::supporters);
        addTier(PatreonCredits::pastSupporters);
        return juce::jmax(h, 60);
    }

    juce::String pluginDisplayName;
    juce::String pluginVersion;
    juce::String actionLabel;
    std::function<void()> onActionClick;
    juce::Rectangle<int> actionHitRegion; // recomputed in paint()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SupportersOverlay)
};
