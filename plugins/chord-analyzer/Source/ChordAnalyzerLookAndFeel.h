#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Custom Look and Feel for Chord Analyzer
class ChordAnalyzerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    //==========================================================================
    // Color palette
    struct Colors
    {
        // Backgrounds
        static inline const juce::Colour bgMain{0xff1a1a1a};
        static inline const juce::Colour bgSection{0xff252525};
        static inline const juce::Colour bgDark{0xff0f0f0f};
        static inline const juce::Colour bgHighlight{0xff303030};

        // Accent colors
        static inline const juce::Colour accentBlue{0xff4a9eff};
        static inline const juce::Colour accentGold{0xffd4a84b};
        static inline const juce::Colour accentGreen{0xff4aff7a};
        static inline const juce::Colour accentRed{0xffff4a4a};

        // Text colors
        static inline const juce::Colour textBright{0xffffffff};
        static inline const juce::Colour textLight{0xffe0e0e0};
        static inline const juce::Colour textDim{0xffa0a0a0};
        static inline const juce::Colour textMuted{0xff707070};

        // Suggestion category colors
        static inline const juce::Colour suggestionBasic{0xff4a9eff};      // Blue
        static inline const juce::Colour suggestionIntermediate{0xff9a7eff}; // Purple
        static inline const juce::Colour suggestionAdvanced{0xffff7e4a};    // Orange

        // Function colors
        static inline const juce::Colour funcTonic{0xff4aff7a};        // Green
        static inline const juce::Colour funcSubdominant{0xffffd44a};  // Yellow
        static inline const juce::Colour funcDominant{0xffff4a4a};     // Red
        static inline const juce::Colour funcChromatic{0xff9a7eff};    // Purple
    };

    //==========================================================================
    ChordAnalyzerLookAndFeel()
    {
        // Set default colors
        setColour(juce::ResizableWindow::backgroundColourId, Colors::bgMain);
        setColour(juce::TextButton::buttonColourId, Colors::bgSection);
        setColour(juce::TextButton::buttonOnColourId, Colors::accentBlue);
        setColour(juce::TextButton::textColourOffId, Colors::textLight);
        setColour(juce::TextButton::textColourOnId, Colors::textBright);

        setColour(juce::ComboBox::backgroundColourId, Colors::bgSection);
        setColour(juce::ComboBox::textColourId, Colors::textLight);
        setColour(juce::ComboBox::outlineColourId, Colors::bgHighlight);
        setColour(juce::ComboBox::arrowColourId, Colors::textDim);

        setColour(juce::PopupMenu::backgroundColourId, Colors::bgSection);
        setColour(juce::PopupMenu::textColourId, Colors::textLight);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Colors::accentBlue);
        setColour(juce::PopupMenu::highlightedTextColourId, Colors::textBright);

        setColour(juce::Label::textColourId, Colors::textLight);

        setColour(juce::ToggleButton::textColourId, Colors::textLight);
        setColour(juce::ToggleButton::tickColourId, Colors::accentBlue);
        setColour(juce::ToggleButton::tickDisabledColourId, Colors::textMuted);
    }

    //==========================================================================
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        auto cornerSize = 4.0f;

        juce::Colour baseColour = backgroundColour;

        if (shouldDrawButtonAsDown)
            baseColour = baseColour.brighter(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, cornerSize);

        // Subtle border
        g.setColour(Colors::bgHighlight);
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }

    //==========================================================================
    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto tickBounds = bounds.removeFromLeft(bounds.getHeight()).reduced(4.0f);

        // Draw checkbox background
        g.setColour(Colors::bgSection);
        g.fillRoundedRectangle(tickBounds, 3.0f);

        g.setColour(Colors::bgHighlight);
        g.drawRoundedRectangle(tickBounds, 3.0f, 1.0f);

        // Draw tick if toggled
        if (button.getToggleState())
        {
            g.setColour(Colors::accentBlue);
            auto tick = tickBounds.reduced(3.0f);

            juce::Path tickPath;
            tickPath.startNewSubPath(tick.getX(), tick.getCentreY());
            tickPath.lineTo(tick.getCentreX() - 2.0f, tick.getBottom() - 3.0f);
            tickPath.lineTo(tick.getRight(), tick.getY() + 3.0f);

            g.strokePath(tickPath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
        }

        // Draw label
        g.setColour(shouldDrawButtonAsHighlighted ? Colors::textBright : Colors::textLight);
        g.setFont(getFont().withHeight(13.0f));
        g.drawText(button.getButtonText(), bounds.reduced(4.0f, 0.0f),
                   juce::Justification::centredLeft, true);
    }

    //==========================================================================
    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool isButtonDown,
                      int buttonX, int buttonY,
                      int buttonW, int buttonH,
                      juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
        auto cornerSize = 4.0f;

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, cornerSize);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);

        // Draw arrow
        juce::Path arrow;
        auto arrowX = (float)buttonX + (float)buttonW * 0.5f;
        auto arrowY = (float)buttonY + (float)buttonH * 0.5f;
        auto arrowSize = 5.0f;

        arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.5f,
                          arrowX + arrowSize, arrowY - arrowSize * 0.5f,
                          arrowX, arrowY + arrowSize * 0.5f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }

    //==========================================================================
    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));

        if (!label.isBeingEdited())
        {
            auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());

            g.setColour(label.findColour(juce::Label::textColourId));
            g.setFont(label.getFont());
            g.drawText(label.getText(), textArea, label.getJustificationType(), true);
        }
    }

    //==========================================================================
    juce::Font getFont() const
    {
        return juce::Font(juce::FontOptions(14.0f));
    }

    juce::Font getLabelFont(juce::Label& /*label*/) override
    {
        return getFont();
    }

    juce::Font getComboBoxFont(juce::ComboBox& /*box*/) override
    {
        return getFont().withHeight(13.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return getFont().withHeight(13.0f);
    }

    //==========================================================================
    // Helper to get suggestion button color
    static juce::Colour getSuggestionColor(int category)
    {
        switch (category)
        {
            case 0: return Colors::suggestionBasic;
            case 1: return Colors::suggestionIntermediate;
            case 2: return Colors::suggestionAdvanced;
            default: return Colors::suggestionBasic;
        }
    }

    // Helper to get function display color
    static juce::Colour getFunctionColor(int function)
    {
        switch (function)
        {
            case 0: return Colors::funcTonic;         // Tonic
            case 1: return Colors::funcSubdominant;   // Subdominant
            case 2: return Colors::funcDominant;      // Dominant
            case 3: return Colors::funcChromatic;     // Secondary dominant
            case 4: return Colors::funcChromatic;     // Borrowed
            case 5: return Colors::funcChromatic;     // Chromatic
            default: return Colors::textDim;
        }
    }

    //==========================================================================
    // Draw a section panel
    static void drawSectionPanel(juce::Graphics& g, juce::Rectangle<int> bounds,
                                  const juce::String& title = "")
    {
        auto floatBounds = bounds.toFloat();

        // Background
        g.setColour(Colors::bgSection);
        g.fillRoundedRectangle(floatBounds, 6.0f);

        // Border
        g.setColour(Colors::bgHighlight);
        g.drawRoundedRectangle(floatBounds.reduced(0.5f), 6.0f, 1.0f);

        // Title
        if (title.isNotEmpty())
        {
            g.setColour(Colors::textDim);
            g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
            g.drawText(title, bounds.removeFromTop(20).reduced(10, 2),
                       juce::Justification::centredLeft, true);
        }
    }

    //==========================================================================
    // Draw header
    static void drawPluginHeader(juce::Graphics& g, juce::Rectangle<int> bounds,
                                  const juce::String& title,
                                  const juce::String& subtitle)
    {
        // Background gradient
        juce::ColourGradient gradient(Colors::bgSection, 0.0f, 0.0f,
                                       Colors::bgDark, 0.0f, (float)bounds.getHeight(),
                                       false);
        g.setGradientFill(gradient);
        g.fillRect(bounds);

        // Bottom border
        g.setColour(Colors::bgHighlight);
        g.fillRect(bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);

        // Title
        auto titleArea = bounds.reduced(15, 0);
        g.setColour(Colors::textBright);
        g.setFont(juce::Font(juce::FontOptions(18.0f)).boldened());
        g.drawText(title, titleArea, juce::Justification::centredLeft, true);

        // Subtitle (company name)
        g.setColour(Colors::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(subtitle, titleArea, juce::Justification::centredRight, true);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzerLookAndFeel)
};
