// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// A window-free stand-in for DAF's UI base class, so a test can run a plugin's
// production editor (its *UI.cpp, #included whole) against Dear ImGui's real
// pointer and keyboard handling. Multi-Comp 2's MultiCompModernUiTests.cpp does
// the same inline, behind a MULTICOMP_UI_TEST_HOST guard in its editor; this
// one needs no change to the editor it hosts.
//
// Put this directory FIRST on the test's include path. The editor's
// #include "DafUI.hpp" and DuskImGuiTextInput.hpp's #include "DearImGui.hpp"
// then resolve here instead of to DAF's window, OpenGL and Pugl stack. Nothing
// of the editor is copied or replaced: only the host side of the UI (parameter
// writes, edit gestures, window size) is stubbed, and recorded for the test.
// DAF_OS_* stay undefined, so DuskImGuiTextInput's Windows focus bridge, which
// needs a native window, compiles out.
#pragma once

#include "DafPluginInfo.h"   // the plugin's own: default size, DIRECT_ACCESS, ...

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS   // as DAF's DearImGui.hpp defines it before imgui.h
#endif
#include "DearImGui/imgui.h"

// What DafUI.hpp brings in through DafUtils.hpp, so an editor that leans on it
// builds against any standard library.
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

typedef unsigned int uint;   // DafDefines.h

#define START_NAMESPACE_DAF namespace DAF {
#define END_NAMESPACE_DAF }
#define DGL_NAMESPACE DAF
#define DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassName) \
    ClassName(const ClassName&) = delete;                      \
    ClassName& operator=(const ClassName&) = delete;

namespace DAF
{
enum MouseCursor { kMouseCursorArrow, kMouseCursorUpLeftDownRight };

class UI
{
public:
    // Every edit gesture and parameter write the editor sends, in order.
    struct HostEvent { enum Kind { Begin, End, Write } kind; uint32_t parameter; float value; };
    std::vector<HostEvent> hostEvents;
    uint32_t width = 0, height = 0;   // the editor window; the test sets it

    UI(uint w = 0, uint h = 0) : width(w), height(h) {}
    virtual ~UI() = default;

    // Test side: one editor frame (between ImGui::NewFrame and ImGui::Render),
    // a host parameter change, and a host program load.
    void draw() { onImGuiDisplay(); }
    void parameter(uint32_t index, float value) { parameterChanged(index, value); }
    void program(uint32_t index) { programLoaded(index); }

    // Host side, as the editor calls it.
    uint getWidth() const noexcept { return width; }
    uint getHeight() const noexcept { return height; }
    double getScaleFactor() const noexcept { return 1.0; }
    double getSampleRate() const noexcept { return 48000.0; }
    void* getPluginInstancePointer() const noexcept { return nullptr; }   // a UI out of the plugin's process
    void setSize(uint, uint) {}
    void setGeometryConstraints(uint, uint, bool = false, bool = false, bool = true) {}
    bool setCursor(MouseCursor) { return true; }
    void repaint() {}
    void editParameter(uint32_t index, bool started)
    {
        hostEvents.push_back({started ? HostEvent::Begin : HostEvent::End, index, 0.0f});
    }
    void setParameterValue(uint32_t index, float value) { hostEvents.push_back({HostEvent::Write, index, value}); }
    void setState(const char*, const char*) {}
    void sendNote(uint8_t, uint8_t, uint8_t) {}

protected:
    virtual void parameterChanged(uint32_t, float) {}
    virtual void programLoaded(uint32_t) {}
    virtual void stateChanged(const char*, const char*) {}
    virtual void uiIdle() {}
    virtual void onImGuiDisplay() {}
};
} // namespace DAF
