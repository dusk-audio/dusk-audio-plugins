// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskImGuiTextInput.hpp — Windows focus bridge for embedded DPF ImGui editors.
//
// Some Windows hosts keep keyboard focus on their own parent window even after
// ImGui activates an InputText item in the embedded plugin HWND. The editor then
// shows a caret but receives neither typed characters nor paste shortcuts.
//
// Call update() once at the end of every UI frame. While ImGui requests text
// input, the bridge gives focus to the editor HWND; when editing ends it returns
// focus to the window that held it previously (or the editor's host parent).

#pragma once

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace duskdpf
{

// Requires DistrhoUI.hpp (and therefore ImGui) to have been included first.
class DuskImGuiTextInputFocus
{
public:
    template <class UIType>
    void update(UIType& ui) noexcept
    {
#if defined(_WIN32)
        HWND const editor =
            reinterpret_cast<HWND>(ui.getWindow().getNativeWindowHandle());
        if (editor == nullptr || !IsWindow(editor))
            return;

        const bool wantsText = ImGui::GetIO().WantTextInput;
        if (wantsText)
        {
            if (!active_)
            {
                restore_ = GetFocus();
                if (restore_ == editor || (restore_ != nullptr && !IsWindow(restore_)))
                    restore_ = nullptr;
                active_ = true;
            }

            if (GetFocus() != editor)
                SetFocus(editor);
            return;
        }

        if (!active_)
            return;

        // Do not steal focus back from another window if the user or host moved
        // it deliberately while the text item was active.
        if (GetFocus() == editor)
        {
            HWND target = restore_;
            if (target == nullptr || !IsWindow(target))
                target = GetParent(editor);
            if (target != nullptr && target != editor && IsWindow(target))
                SetFocus(target);
        }

        restore_ = nullptr;
        active_ = false;
#else
        (void)ui;
#endif
    }

private:
#if defined(_WIN32)
    HWND restore_ = nullptr;
    bool active_ = false;
#endif
};

} // namespace duskdpf
