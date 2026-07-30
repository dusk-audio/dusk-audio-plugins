// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskImGuiTextInput.hpp — narrow Windows focus bridge for DPF/Dear ImGui text
// fields embedded in plugin hosts.
//
// Some Windows hosts keep keyboard focus on the editor's parent HWND even after
// an ImGui InputText has activated. Mouse input still reaches the child, so the
// field shows a caret, but neither characters nor shortcuts such as Ctrl+V reach
// DPF. Focus only the native editor while ImGui explicitly requests text input,
// then restore the host's prior child (or the editor parent) when editing ends.
// This keeps normal DAW shortcuts with the host at every other time.

#pragma once

#include "DearImGui.hpp"

#if defined(DISTRHO_OS_WINDOWS)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#endif

namespace duskdpf
{

class DuskImGuiTextInputFocus
{
public:
    template <class UIType>
    void update(UIType& ui) noexcept
    {
#if defined(DISTRHO_OS_WINDOWS)
        const bool wantsText = ImGui::GetIO().WantTextInput;
        HWND const editor = reinterpret_cast<HWND>(
            ui.getParentWindow().getNativeWindowHandle());

        if (editor == nullptr)
        {
            wasActive_ = wantsText;
            return;
        }

        if (wantsText && !wasActive_)
        {
            HWND const parent = GetParent(editor);
            HWND const focused = GetFocus();

            // Only remember focus from inside the same embedded host hierarchy.
            // Restoring an unrelated application after an Alt-Tab would be a
            // surprising focus steal.
            restoreFocus_ =
                focused != nullptr
                && (focused == parent
                    || (parent != nullptr && IsChild(parent, focused)))
                    ? focused
                    : parent;

            if (focused != editor)
                SetFocus(editor);
        }
        else if (!wantsText && wasActive_)
        {
            // If the user has already focused another window, leave it alone.
            if (GetFocus() == editor)
            {
                HWND const target =
                    restoreFocus_ != nullptr && IsWindow(restoreFocus_)
                        ? restoreFocus_
                        : GetParent(editor);
                if (target != nullptr)
                    SetFocus(target);
            }
            restoreFocus_ = nullptr;
        }

        wasActive_ = wantsText;
#else
        (void)ui;
#endif
    }

private:
#if defined(DISTRHO_OS_WINDOWS)
    HWND restoreFocus_ = nullptr;
    bool wasActive_ = false;
#endif
};

} // namespace duskdpf

