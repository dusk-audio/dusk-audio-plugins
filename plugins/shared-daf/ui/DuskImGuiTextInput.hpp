// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskImGuiTextInput.hpp — narrow Windows focus bridge for DAF/Dear ImGui text
// fields embedded in plugin hosts.
//
// Some Windows hosts keep keyboard focus on the editor's parent HWND even after
// an ImGui InputText has activated. Mouse input still reaches the child, so the
// field shows a caret, but neither characters nor shortcuts such as Ctrl+V reach
// DAF. Focus only the native editor while ImGui explicitly requests text input,
// then restore the host's prior child (or the editor parent) when editing ends.
// This keeps normal DAW shortcuts with the host at every other time.

#pragma once

#include "DearImGui.hpp"

#if defined(DAF_OS_WINDOWS)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#endif

namespace duskdaf
{

class DuskImGuiTextInputFocus
{
public:
    template <class UIType>
    void update(UIType& ui) noexcept
    {
#if defined(DAF_OS_WINDOWS)
        const bool wantsText = ImGui::GetIO().WantTextInput;
        HWND const editor = reinterpret_cast<HWND>(
            ui.getParentWindow().getNativeWindowHandle());

        // No native handle yet: leave wasActive_ alone so activation is retried
        // on a later frame rather than latched as already done, which would
        // leave WantTextInput stuck with the host still holding the keyboard.
        if (editor == nullptr)
            return;

        if (wantsText)
        {
            if (!wasActive_)
            {
                HWND const parent = GetParent(editor);
                HWND const focused = GetFocus();

                // Only remember focus from inside the same embedded host
                // hierarchy, and never the editor itself: restoring an
                // unrelated application after an Alt-Tab would be a surprising
                // focus steal, and restoring the editor would never hand the
                // keyboard back to the host.
                // Captured once per activation: on a retry frame SetFocus may
                // have landed by now, so re-capturing would replace the host
                // child we remembered with the editor's own parent.
                if (restoreFocus_ == nullptr)
                    restoreFocus_ =
                        focused != nullptr && focused != editor
                        && (focused == parent
                            || (parent != nullptr && IsChild(parent, focused)))
                            ? focused
                            : parent;

                if (focused != editor)
                    SetFocus(editor);

                // Latch only once the native editor really owns focus; a failed
                // SetFocus retries on the next frame.
                wasActive_ = (GetFocus() == editor);
            }
        }
        else
        {
            if (wasActive_)
            {
                // If the user has already focused another window, leave it alone.
                if (GetFocus() == editor)
                {
                    HWND const parent = GetParent(editor);
                    // A dead HWND can be recycled by an unrelated window, so
                    // IsWindow() alone proves nothing: require the stored handle
                    // to still belong to this editor's hierarchy, else use the
                    // parent.
                    HWND const target =
                        restoreFocus_ != nullptr && IsWindow(restoreFocus_)
                        && (restoreFocus_ == parent
                            || (parent != nullptr && IsChild(parent, restoreFocus_)))
                            ? restoreFocus_
                            : parent;
                    if (target != nullptr)
                        SetFocus(target);
                }
                wasActive_ = false;
            }
            // Cleared on every frame without text input, which also covers an
            // ABANDONED activation (captured, but SetFocus never took, so
            // wasActive_ never latched): the next activation has to capture the
            // focus owner as it is then, not a handle left over from that try.
            restoreFocus_ = nullptr;
        }
#else
        (void)ui;
#endif
    }

private:
#if defined(DAF_OS_WINDOWS)
    HWND restoreFocus_ = nullptr;
    bool wasActive_ = false;
#endif
};

} // namespace duskdaf

