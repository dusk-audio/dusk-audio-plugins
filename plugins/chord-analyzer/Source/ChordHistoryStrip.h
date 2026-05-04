#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ChordAnalyzer.h"
#include "ChordAnalyzerLookAndFeel.h"

#include <vector>

// Horizontal strip showing the most recent N detected chords (newest on
// the right). Chips fade in alpha proportional to age so the most recent
// chord pops; older history dims toward the background. Lives below the
// main chord display in the editor.
//
// The component owns no state beyond its current snapshot — call
// setHistory() with a vector copy from the processor whenever the
// editor's timer ticks. Repaint cost is bounded by the number of chips
// the strip can fit (display-width / kMinChipWidth), capped at 32.
class ChordHistoryStrip : public juce::Component
{
public:
    ChordHistoryStrip() = default;

    // Chip click — argument is the index into the snapshot returned by
    // the most recent setHistory() call (0 = oldest, size-1 = newest).
    std::function<void (int)> onChipClicked;

    // Chip drag start — fires once per drag gesture (after the user
    // moves past JUCE's drag threshold). Caller is expected to kick off
    // FileDragAndDropContainer::performExternalDragDropOfFiles. The
    // ChordInfo argument is the chip the drag started over.
    std::function<void (const ChordInfo&)> onChipDragStart;

    void setHistory (const std::vector<ChordInfo>& newHistory)
    {
        if (newHistory == history_)
            return;
        history_ = newHistory;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        if (history_.empty())
        {
            g.setColour (ChordAnalyzerLookAndFeel::Colors::textMuted);
            g.setFont (juce::Font (juce::FontOptions (12.0f)).italicised());
            g.drawText ("Play some chords — recent history will appear here.",
                        bounds, juce::Justification::centred);
            return;
        }

        const int n      = static_cast<int> (history_.size());
        const int chipW  = juce::jmax (kMinChipWidth,
                                       (bounds.getWidth() - kStripPad * 2)
                                         / juce::jmax (1, juce::jmin (n, kMaxVisibleChips)));
        const int chipH  = bounds.getHeight() - kStripPad * 2;

        // Determine the visible window — we always show the most recent
        // chord at the right edge, fitting as many older entries as the
        // strip width allows.
        const int maxFit = juce::jmax (1, (bounds.getWidth() - kStripPad * 2) / chipW);
        const int firstVisible = juce::jmax (0, n - maxFit);

        for (int i = firstVisible; i < n; ++i)
        {
            const int displayIdx = i - firstVisible;
            const int x = bounds.getX() + kStripPad + displayIdx * chipW;
            const int y = bounds.getY() + kStripPad;
            const juce::Rectangle<int> chip (x, y, chipW - kChipGap, chipH);

            // Newer chords get higher alpha; oldest fades to ~0.35.
            const float age   = static_cast<float> (n - 1 - i)
                              / static_cast<float> (juce::jmax (1, n - 1));
            const float alpha = juce::jmap (age, 0.0f, 1.0f, 1.0f, 0.35f);

            paintChip (g, chip, history_[(size_t) i], alpha,
                       i == n - 1 /* isMostRecent */);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        // mouseDown on its own doesn't trigger the click — we wait for
        // mouseUp without a drag so a drag gesture isn't also treated as
        // a click. The drag flag is reset in mouseUp.
        dragInitiated_ = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragInitiated_ || ! onChipDragStart)
            return;

        // Wait until the user has moved past the standard JUCE drag
        // threshold (4 px) so a slightly shaky click doesn't kick off
        // an unintended drag.
        if (e.getDistanceFromDragStart() < 4)
            return;

        const auto* chord = chordAt (e.getMouseDownPosition());
        if (chord != nullptr)
        {
            dragInitiated_ = true;
            onChipDragStart (*chord);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // Treat as a click if the user didn't drag past the threshold.
        if (! dragInitiated_ && onChipClicked
            && e.getDistanceFromDragStart() < 4)
        {
            const int idx = chipIndexAt (e.getMouseDownPosition());
            if (idx >= 0 && idx < static_cast<int> (history_.size()))
                onChipClicked (idx);
        }
        dragInitiated_ = false;
    }

    // For drag-and-drop wiring later (Feature B): expose the chord at a
    // hit-tested point so the parent can implement startDragging.
    const ChordInfo* chordAt (juce::Point<int> pt) const
    {
        const int idx = chipIndexAt (pt);
        if (idx < 0 || idx >= static_cast<int> (history_.size()))
            return nullptr;
        return &history_[(size_t) idx];
    }

private:
    static constexpr int kMinChipWidth     = 56;
    static constexpr int kMaxVisibleChips  = 16;
    static constexpr int kStripPad         = 6;
    static constexpr int kChipGap          = 4;

    std::vector<ChordInfo> history_;
    bool                   dragInitiated_ = false;

    int chipIndexAt (juce::Point<int> pt) const
    {
        const int n = static_cast<int> (history_.size());
        if (n == 0)
            return -1;

        const auto bounds = getLocalBounds();
        const int chipW   = juce::jmax (kMinChipWidth,
                                        (bounds.getWidth() - kStripPad * 2)
                                          / juce::jmax (1, juce::jmin (n, kMaxVisibleChips)));
        const int chipH        = bounds.getHeight() - kStripPad * 2;
        const int maxFit       = juce::jmax (1, (bounds.getWidth() - kStripPad * 2) / chipW);
        const int firstVisible = juce::jmax (0, n - maxFit);

        const int relY = pt.getY() - bounds.getY() - kStripPad;
        if (relY < 0 || relY >= chipH)
            return -1;

        const int relX = pt.getX() - bounds.getX() - kStripPad;
        if (relX < 0)
            return -1;
        const int displayIdx = relX / chipW;
        const int xInCell    = relX - displayIdx * chipW;
        if (xInCell >= chipW - kChipGap)
            return -1;
        const int idx        = firstVisible + displayIdx;
        return (idx >= n) ? -1 : idx;
    }

    static void paintChip (juce::Graphics& g,
                           juce::Rectangle<int> bounds,
                           const ChordInfo& chord,
                           float alpha,
                           bool isMostRecent)
    {
        const auto rect = bounds.toFloat().reduced (1.0f);
        const float corner = 4.0f;

        const auto bg = (isMostRecent
                            ? ChordAnalyzerLookAndFeel::Colors::accentBlue.withAlpha (0.18f)
                            : ChordAnalyzerLookAndFeel::Colors::bgSection)
                       .withMultipliedAlpha (alpha);
        g.setColour (bg);
        g.fillRoundedRectangle (rect, corner);

        if (isMostRecent)
        {
            g.setColour (ChordAnalyzerLookAndFeel::Colors::accentBlue.withAlpha (0.6f));
            g.drawRoundedRectangle (rect, corner, 1.0f);
        }

        // Chord name (large) on top, roman numeral (small) below.
        const auto textBright = ChordAnalyzerLookAndFeel::Colors::textBright
                                  .withMultipliedAlpha (alpha);
        const auto textDim    = ChordAnalyzerLookAndFeel::Colors::textDim
                                  .withMultipliedAlpha (alpha);

        const int nameH = juce::jmax (16, bounds.getHeight() / 2 + 4);
        auto nameRect = bounds.removeFromTop (nameH);
        g.setColour (textBright);
        g.setFont (juce::Font (juce::FontOptions (15.0f)).boldened());
        g.drawText (chord.name, nameRect, juce::Justification::centred, true);

        if (chord.romanNumeral.isNotEmpty())
        {
            g.setColour (textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (chord.romanNumeral, bounds, juce::Justification::centred, true);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordHistoryStrip)
};
