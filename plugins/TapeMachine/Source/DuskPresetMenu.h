#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

// In-window preset menu — replaces juce::ComboBox's native popup window so
// the TapeMachine plugin's preset dropdown matches the Dusk Studio host's
// modal grammar (dim backdrop, rounded panel, click-outside / Esc dismiss,
// no separate top-level OS window).
//
// Pair: DuskPresetMenu (the popup body) + DuskPresetSelector (a juce::ComboBox
// subclass whose showPopup() override mounts a DuskPresetMenu inside the
// plugin editor instead of opening JUCE's native popup).
class DuskPresetMenu final : public juce::Component
{
public:
    struct Entry
    {
        juce::String text;
        int  itemId       = 0;
        bool isHeader     = false;
        bool isSeparator  = false;
        bool isChecked    = false;
    };

    DuskPresetMenu (std::vector<Entry> entriesIn,
                    juce::Rectangle<int> anchorInParent,
                    std::function<void (int)> onSelectedIn,
                    std::function<void()>     onDismissedIn)
        : items (std::move (entriesIn)),
          anchorRect (anchorInParent),
          onSelected (std::move (onSelectedIn)),
          onDismissed (std::move (onDismissedIn))
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        setInterceptsMouseClicks (true, true);
    }

    void paint (juce::Graphics& g) override
    {
        // Translucent dim backdrop across the full parent so anything
        // behind reads as muted.
        g.fillAll (juce::Colour (0x80000000));

        const auto rect = computeMenuRect();
        // Soft shadow + rounded panel.
        g.setColour (juce::Colour (0x80000000));
        g.fillRoundedRectangle (rect.toFloat().translated (0.0f, 3.0f), 8.0f);
        g.setColour (juce::Colour (0xff1c1c20));
        g.fillRoundedRectangle (rect.toFloat(), 8.0f);
        g.setColour (juce::Colour (0xff3a3a44));
        g.drawRoundedRectangle (rect.toFloat().reduced (0.5f), 8.0f, 1.0f);

        int y = rect.getY() + kPanelPadTop;
        const int xText = rect.getX() + kRowPadX;
        const int rowW  = rect.getWidth() - 2 * kRowPadX;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            if (it.isSeparator)
            {
                g.setColour (juce::Colour (0xff32323a));
                g.drawHorizontalLine (y + kSeparatorRowH / 2,
                                       (float) (rect.getX() + 10),
                                       (float) (rect.getRight() - 10));
                y += kSeparatorRowH;
                continue;
            }
            if (it.isHeader)
            {
                g.setColour (juce::Colour (0xffd0d0d8));
                g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
                g.drawText (it.text, xText, y, rowW, kRowH,
                              juce::Justification::centredLeft, false);
                y += kRowH;
                continue;
            }

            const auto rowRect = juce::Rectangle<int> (rect.getX() + 4, y,
                                                          rect.getWidth() - 8, kRowH);
            if (rowRect.contains (hoverPoint))
            {
                g.setColour (juce::Colour (0xff34343e));
                g.fillRoundedRectangle (rowRect.toFloat(), 4.0f);
            }

            // Check mark for the active selection.
            if (it.isChecked)
            {
                g.setColour (juce::Colour (0xffe0c050));
                g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")),
                              xText - 2, y, 14, kRowH,
                              juce::Justification::centred, false);
            }

            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawText (it.text, xText + 16, y, rowW - 16, kRowH,
                          juce::Justification::centredLeft, false);
            y += kRowH;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto rect = computeMenuRect();
        if (! rect.contains (e.x, e.y))
        {
            if (onDismissed) onDismissed();
            return;
        }
        const int idx = itemAtY (e.y);
        if (idx >= 0 && idx < (int) items.size())
        {
            const auto& it = items[(size_t) idx];
            if (it.isHeader || it.isSeparator || it.itemId <= 0)
                return;   // ignore non-item clicks
            if (onSelected) onSelected (it.itemId);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverPoint = e.getPosition();
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverPoint = { -1, -1 };
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            if (onDismissed) onDismissed();
            return true;
        }
        return false;
    }

private:
    static constexpr int kRowH            = 26;
    static constexpr int kSeparatorRowH   = 8;
    static constexpr int kPanelPadTop     = 6;
    static constexpr int kPanelPadBottom  = 6;
    static constexpr int kRowPadX         = 12;
    static constexpr int kMinPanelW       = 220;
    static constexpr int kAnchorGap       = 4;

    int rowHeightFor (const Entry& it) const noexcept
    {
        return it.isSeparator ? kSeparatorRowH : kRowH;
    }

    int totalContentHeight() const noexcept
    {
        int h = kPanelPadTop + kPanelPadBottom;
        for (const auto& it : items) h += rowHeightFor (it);
        return h;
    }

    juce::Rectangle<int> computeMenuRect() const
    {
        const int h = totalContentHeight();
        const int w = juce::jmax (kMinPanelW, anchorRect.getWidth());
        int x = anchorRect.getX();
        int y = anchorRect.getBottom() + kAnchorGap;

        const auto parentBounds = getParentComponent() != nullptr
                                    ? getParentComponent()->getLocalBounds()
                                    : juce::Rectangle<int> (0, 0, getWidth(), getHeight());

        // Drop above anchor if it would overflow the parent's bottom.
        if (y + h > parentBounds.getBottom() - 4)
        {
            const int yAbove = anchorRect.getY() - kAnchorGap - h;
            y = (yAbove >= parentBounds.getY() + 4)
                  ? yAbove
                  : juce::jmax (parentBounds.getY() + 4, parentBounds.getBottom() - h - 4);
        }
        if (x + w > parentBounds.getRight() - 4)
            x = parentBounds.getRight() - w - 4;
        x = juce::jmax (parentBounds.getX() + 4, x);

        return juce::Rectangle<int> (x, y, w, h);
    }

    int itemAtY (int y) const noexcept
    {
        const auto rect = computeMenuRect();
        int yy = rect.getY() + kPanelPadTop;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const int rh = rowHeightFor (items[i]);
            if (y >= yy && y < yy + rh) return (int) i;
            yy += rh;
        }
        return -1;
    }

    std::vector<Entry> items;
    juce::Rectangle<int> anchorRect;
    std::function<void (int)> onSelected;
    std::function<void()>     onDismissed;
    juce::Point<int> hoverPoint { -1, -1 };
};

// ComboBox subclass that intercepts showPopup() and renders DuskPresetMenu
// as a child of the plugin editor instead of opening JUCE's native popup
// in a separate OS window.
class DuskPresetSelector final : public juce::ComboBox
{
public:
    DuskPresetSelector() = default;
    ~DuskPresetSelector() override { hideMenu(); }

    void showPopup() override
    {
        // Toggle: clicking the combo while the menu is open dismisses it.
        if (activeMenu_ != nullptr) { hideMenu(); return; }

        // Snapshot the combo's items into a flat list. JUCE's ComboBox
        // stores everything (items + section headings + separators) in
        // an internal PopupMenu exposed via getRootMenu(); iterate that
        // to preserve the section structure (ComboBox::getNumItems
        // skips headings + separators).
        auto* pm = getRootMenu();
        if (pm == nullptr) return;

        std::vector<DuskPresetMenu::Entry> entries;
        const int selected = getSelectedId();
        for (juce::PopupMenu::MenuItemIterator it (*pm); it.next();)
        {
            const auto& mi = it.getItem();
            DuskPresetMenu::Entry e;
            e.text        = mi.text;
            e.itemId      = mi.itemID;
            e.isHeader    = mi.isSectionHeader;
            e.isSeparator = mi.isSeparator;
            e.isChecked   = (mi.itemID != 0 && mi.itemID == selected);
            entries.push_back (std::move (e));
        }

        auto* host = findHostForMenu();
        if (host == nullptr) return;

        const auto anchorInHost = host->getLocalArea (this, getLocalBounds());

        juce::Component::SafePointer<DuskPresetSelector> safeSelf (this);
        auto menu = std::make_unique<DuskPresetMenu> (
            std::move (entries),
            anchorInHost,
            [safeSelf] (int chosenId)
            {
                if (safeSelf == nullptr) return;
                if (chosenId > 0)
                    safeSelf->setSelectedId (chosenId, juce::sendNotificationSync);
                safeSelf->hideMenu();
            },
            [safeSelf]
            {
                if (safeSelf == nullptr) return;
                safeSelf->hideMenu();
            });
        menu->setBounds (host->getLocalBounds());
        activeMenuHost_ = host;
        activeMenu_ = menu.release();
        host->addAndMakeVisible (activeMenu_);
        activeMenu_->toFront (true);
        activeMenu_->grabKeyboardFocus();
    }

    void hideMenu()
    {
        // Clear the base ComboBox's private `menuActive` flag. mouseDown() only opens the popup via
        // showPopupIfNotActive(), which no-ops while `menuActive` is true; the base clears the flag
        // in hidePopup(), but that is only invoked from the native popup's finished-callback, which
        // we never trigger (we replaced the native popup with DuskPresetMenu). Without this reset
        // the flag stays true after the first open, so the dropdown refuses to reopen once a preset
        // has been selected or the menu dismissed (issue: dropdown dead after first use). hidePopup()
        // self-guards on `menuActive`, so this is a cheap no-op when nothing is open.
        hidePopup();

        if (activeMenu_ == nullptr) return;
        if (activeMenuHost_ != nullptr)
            activeMenuHost_->removeChildComponent (activeMenu_);
        // Defer destruction so we can be safely called from inside the
        // menu's own click handler (the lambda runs while activeMenu_
        // is still on the stack); a synchronous delete here would
        // destroy the lambda's std::function captures mid-call.
        std::unique_ptr<juce::Component> trash (activeMenu_);
        std::shared_ptr<juce::Component> trashShared (trash.release());
        juce::MessageManager::callAsync (
            [trashShared]() mutable { (void) trashShared; });
        activeMenu_     = nullptr;
        activeMenuHost_ = nullptr;
    }

private:
    // Find the highest in-plugin parent — for a JUCE plugin that's the
    // AudioProcessorEditor itself. The menu mounts there so it stays
    // inside the plugin's own window/HWND rather than escaping into the
    // host's top-level chrome.
    juce::Component* findHostForMenu()
    {
        if (auto* ed = findParentComponentOfClass<juce::AudioProcessorEditor>())
            return ed;
        return getTopLevelComponent();
    }

    juce::Component* activeMenu_     = nullptr;
    juce::Component* activeMenuHost_ = nullptr;
};
