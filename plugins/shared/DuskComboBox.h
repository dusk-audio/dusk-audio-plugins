#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/**
    DuskComboBox — a juce::ComboBox whose drop-down renders INSIDE the plugin
    editor instead of as a separate top-level OS window.

    Why this exists
    ---------------
    juce::ComboBox::showPopup() shows its PopupMenu in a heavyweight native
    window (its own X11/Wayland surface). On Linux under Wayland and XWayland
    those popup surfaces flicker, mis-position relative to the host window, grab
    focus oddly, or fail to dismiss — the dropdowns are visibly glitchy in DAWs.

    PopupMenu::Options::withParentComponent() makes the menu a LIGHTWEIGHT child
    of a component we already own (the editor), so it is painted in the existing
    plugin window with no new OS surface. That is robust across Wayland,
    XWayland and X11, and is the single behavioural difference from the stock
    ComboBox — selection, attachments, keyboard step, and Look&Feel are
    unchanged.

    Usage
    -----
    Drop-in replacement for juce::ComboBox. For a categorized / submenu popup,
    set `menuBuilder`; otherwise the popup mirrors the flat addItem() list. The
    menu inherits the component's current LookAndFeel, so existing
    drawPopupMenu* overrides keep styling it.
*/
class DuskComboBox : public juce::ComboBox
{
public:
    DuskComboBox() = default;
    explicit DuskComboBox (const juce::String& componentName)
        : juce::ComboBox (componentName) {}

    /** Optional. Returns the PopupMenu to display (e.g. categories as
        submenus). When null, the popup mirrors the flat item list. The chosen
        item id is routed through setSelectedId() so onChange fires exactly as a
        normal pick would — keep your menu item ids equal to the ComboBox item
        ids. */
    std::function<juce::PopupMenu()> menuBuilder;

    void showPopup() override
    {
        juce::PopupMenu menu = menuBuilder ? menuBuilder() : buildFlatMenu();
        menu.setLookAndFeel (&getLookAndFeel());

        // hidePopup() in the callback is ESSENTIAL: ComboBox sets an internal
        // "menu is showing" flag in showPopup() and only clears it via
        // hidePopup(). Without it the box responds once, then silently no-ops
        // on the next click until the editor is recreated.
        juce::Component::SafePointer<DuskComboBox> safe (this);
        auto options = juce::PopupMenu::Options()
                           .withTargetComponent (this)
                           .withMinimumWidth (getWidth())
                           // Force a SINGLE scrolling column. A tall categorized list
                           // (section headers + items) otherwise lets JUCE auto-split
                           // into multiple columns when it exceeds the parent height —
                           // which detaches a category header from its items (the
                           // "Chambers" header landed bottom-left while its presets
                           // wrapped to the top-right column). One column + scroll
                           // arrows keeps every header glued to its items.
                           .withMaximumNumColumns (1);

        // Render in-window. Parent to the top-level editor so the menu lives in
        // the existing plugin surface (no native popup window). If we are not
        // yet parented (shouldn't happen once visible), fall back to the stock
        // native popup rather than crash.
        if (auto* host = getTopLevelComponent())
            options = options.withParentComponent (host);

        menu.showMenuAsync (options, [safe] (int result)
        {
            if (safe == nullptr)
                return;
            safe->hidePopup();
            if (result > 0)
                safe->setSelectedId (result, juce::sendNotification);
        });
    }

private:
    /** Mirror the flat addItem()/addItemList() contents, ticking the selected
        item. Section headings (id 0) become separators. */
    juce::PopupMenu buildFlatMenu()
    {
        juce::PopupMenu m;
        const int selected = getSelectedId();
        for (int i = 0; i < getNumItems(); ++i)
        {
            const int id = getItemId (i);
            if (id == 0)
            {
                m.addSeparator();
                continue;
            }
            m.addItem (id, getItemText (i), true, id == selected);
        }
        return m;
    }
};
