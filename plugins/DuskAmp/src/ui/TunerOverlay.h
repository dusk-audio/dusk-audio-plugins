// TunerOverlay.h — full-editor modal showing detected note name + cents.
//
// Mirrors the pattern of plugins/shared/SupportersOverlay.h: a Component
// that paints a panel over the editor and dismisses on click. The note +
// cents indicator is driven by setDetected(hz, level) calls from the
// editor's timer — the overlay itself is not aware of audio threads.

#pragma once

#if __has_include (<JuceHeader.h>)
    #include <JuceHeader.h>
#else
    #include <juce_gui_basics/juce_gui_basics.h>
    #include <juce_graphics/juce_graphics.h>
#endif

#include <cmath>

class TunerOverlay : public juce::Component
{
public:
    TunerOverlay()
    {
        setInterceptsMouseClicks (true, false);
    }

    /** Audio-driven update from the editor's timer. hz=0 means "no signal". */
    void setDetected (float hz, float level)
    {
        if (std::abs (hz - currentHz_) > 0.01f || std::abs (level - currentLevel_) > 0.0001f)
        {
            currentHz_    = hz;
            currentLevel_ = level;
            // Smooth the cents reading so the indicator doesn't twitch.
            if (hz > 0.0f)
            {
                const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
                const float nearest = std::round (midi);
                const float newCents = (midi - nearest) * 100.0f;
                smoothedCents_ += 0.25f * (newCents - smoothedCents_);
                currentMidi_   = midi;
                currentNearest_= static_cast<int> (nearest);
            }
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const int w = getWidth();
        const int h = getHeight();

        // Dim the editor underneath
        g.setColour (juce::Colour (0xf0101010));
        g.fillRect (0, 0, w, h);

        // Panel
        const int panelW = juce::jmin (640, w - 80);
        const int panelH = juce::jmin (380, h - 80);
        auto panel = juce::Rectangle<int> ((w - panelW) / 2, (h - panelH) / 2, panelW, panelH);

        juce::ColourGradient bgGrad (
            juce::Colour (0xff2d2d2d), (float) panel.getX(), (float) panel.getY(),
            juce::Colour (0xff1a1a1a), (float) panel.getX(), (float) panel.getBottom(), false);
        g.setGradientFill (bgGrad);
        g.fillRoundedRectangle (panel.toFloat(), 12.0f);

        g.setColour (juce::Colour (0xff505050));
        g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 12.0f, 2.0f);

        // Header
        g.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
        g.setColour (juce::Colour (0xffe8e8e8));
        g.drawText ("TUNER", panel.getX(), panel.getY() + 18,
                    panel.getWidth(), 22, juce::Justification::centred);

        // "MUTED" badge
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (juce::Colour (0xffe06060));
        g.drawText ("MUTED",
                    panel.getX() + panel.getWidth() - 70, panel.getY() + 22,
                    60, 16, juce::Justification::centredRight);

        const bool hasSignal = currentHz_ > 0.0f;

        // Big note name
        const auto& noteName = (hasSignal ? noteNameOf (currentNearest_) : juce::String ("—"));
        const auto octaveStr = (hasSignal ? juce::String (octaveOf (currentNearest_)) : juce::String());
        g.setFont (juce::Font (juce::FontOptions (96.0f).withStyle ("Bold")));
        g.setColour (hasSignal ? juce::Colour (0xfff0f0f0) : juce::Colour (0xff606060));
        g.drawText (noteName,
                    panel.getX(), panel.getY() + 60,
                    panel.getWidth(), 110, juce::Justification::centred);

        if (hasSignal)
        {
            // Octave subscript next to the note
            g.setFont (juce::Font (juce::FontOptions (28.0f)));
            g.setColour (juce::Colour (0xffa0a0a0));
            // Right-align after the centred note text — measure note width
            // approximately by glyph count.
            const int noteApproxW = 60; // glyph width estimate at 96 pt
            g.drawText (octaveStr,
                        panel.getCentreX() + noteApproxW / 2 + 4,
                        panel.getY() + 110, 60, 30,
                        juce::Justification::centredLeft);

            // Detected Hz
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.setColour (juce::Colour (0xff909090));
            g.drawText (juce::String (currentHz_, 1) + " Hz",
                        panel.getX(), panel.getY() + 175,
                        panel.getWidth(), 18, juce::Justification::centred);

            // Cents bar — ±50 cents range, centred at panel midline
            const int barY = panel.getY() + 215;
            const int barH = 26;
            const int barW = panel.getWidth() - 80;
            const int barX = panel.getX() + 40;
            g.setColour (juce::Colour (0xff202020));
            g.fillRoundedRectangle ((float) barX, (float) barY, (float) barW, (float) barH, 4.0f);

            // Tick marks
            g.setColour (juce::Colour (0xff404040));
            for (int t = -50; t <= 50; t += 10)
            {
                const int tx = barX + (t + 50) * barW / 100;
                g.drawVerticalLine (tx, (float) barY, (float) (barY + barH));
            }
            // Centre marker (in tune)
            g.setColour (juce::Colour (0xff606060));
            const int cx = barX + barW / 2;
            g.fillRect (cx - 1, barY - 4, 3, barH + 8);

            // Current cents indicator
            const float cents = juce::jlimit (-50.0f, 50.0f, smoothedCents_);
            const int   ix    = barX + static_cast<int> ((cents + 50.0f) * (float) barW / 100.0f);
            const bool  inTune = std::abs (cents) <= 5.0f;
            g.setColour (inTune ? juce::Colour (0xff60c060) : juce::Colour (0xffd0a040));
            g.fillRoundedRectangle ((float) (ix - 4), (float) (barY - 2),
                                    8.0f, (float) (barH + 4), 3.0f);

            // Cents text
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.setColour (juce::Colour (0xff909090));
            const auto centsTxt = (cents >= 0.0f ? juce::String ("+") : juce::String())
                                + juce::String (cents, 1) + " cents";
            g.drawText (centsTxt,
                        panel.getX(), barY + barH + 6,
                        panel.getWidth(), 18, juce::Justification::centred);
        }
        else
        {
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.setColour (juce::Colour (0xff707070));
            g.drawText ("Play a note",
                        panel.getX(), panel.getY() + 175,
                        panel.getWidth(), 20, juce::Justification::centred);
        }

        // Footer
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.setColour (juce::Colour (0xff707070));
        g.drawText ("Click anywhere to close",
                    panel.getX(), panel.getBottom() - 32,
                    panel.getWidth(), 18, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onDismiss) onDismiss();
    }

    std::function<void()> onDismiss;

private:
    static juce::String noteNameOf (int midi)
    {
        static const char* kNames[12] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        const int idx = ((midi % 12) + 12) % 12;
        return juce::String::fromUTF8 (kNames[idx]);
    }
    static int octaveOf (int midi)
    {
        return midi / 12 - 1;
    }

    float currentHz_      = 0.0f;
    float currentLevel_   = 0.0f;
    float currentMidi_    = 69.0f;
    int   currentNearest_ = 69;
    float smoothedCents_  = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerOverlay)
};
