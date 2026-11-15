// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The inline value field over a custom knob (dusk-audio/plugins#337), checked
// on a plugin's production editor through the window-free host in DafUI.hpp.
// Include the editor's *UI.cpp first, then this.
//
// A knob widget that keeps submitting its own InvisibleButton while its field
// is open takes every click inside the field: Dear ImGui gives a click to the
// item submitted first. The field then closes, and the knob goes active
// without IsItemActivated(), so it drags with no host gesture opened and ends
// one that never began. DuskPanel::knob submits no button while its field is
// open (DuskImGuiWidgets.hpp); these cases hold any other widget to the same.
//
// Every case builds a fresh editor and checks the host protocol on everything
// the editor sent: each write inside an open gesture on its parameter, no end
// without a begin, nothing left open.
//
// What a green run proves. Every check is tagged in the output:
//   [#337]  fails on the editors as they were before the fix and passes after
//           it: a click and a click-drag inside the open field over the knob,
//           typing a value after them, and a press on the knob outside its open
//           field. The click on the start of the text is [#337] only where that
//           point lies over the knob, which it does not on Sunset Circuits'
//           radius-13 RATIO knob; there it is a [guard].
//   [guard] passes on both, and covers the opposite failure modes: the field
//           still opens (also under a held, dragged double-click, which moves
//           nothing), closes by Enter, Escape and a click elsewhere, and the
//           knob then drags in one gesture; another knob still drags while the
//           field is open; Shift, the wheel, the modifier-click reset and the
//           resting hover still work; the points the cases press on still lie
//           on the knob; and the item-query rule below holds.
//
// The item-query rule. With the knob's button skipped, an ImGui::IsItem* read
// after it answers for the item submitted before the knob. Sunset Circuits'
// knobs read the hover for their tooltip there, so previousItemTooltip() hovers
// that item with the field open. In 4K EQ 2's and Multi-Q 2's knobs every
// IsItem* read either sits inside the skip or only runs on a frame that
// submitted the button, so they get no such case.
#pragma once

#include "DafUI.hpp"
#include "DearImGui/imgui_internal.h"
#include "dgl/src/Resources.hpp"   // DAF's own DejaVu Sans, by its path in the DAF tree

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dusktest
{
inline int failures = 0, issueChecks = 0, guardChecks = 0;

// A check that fails on the unfixed editors (see the top of this file).
inline void detects(bool ok, const std::string& what)
{
    std::printf("%s [#337] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
    ++issueChecks;
}
// A check that passes on both.
inline void guards(bool ok, const std::string& what)
{
    std::printf("%s [guard] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
    ++guardChecks;
}
inline int summary()
{
    std::printf("%s: %d failure(s) in %d [#337] and %d [guard] checks\n", failures ? "FAILED" : "PASSED",
                failures, issueChecks, guardChecks);
    return failures ? 1 : 0;
}

// An editor scans the user preset library when it is built. Point that scan at
// an empty directory of the test's own, so no test reads (or could write) the
// user's real library.
inline void isolateHome(const char* directory)
{
   #if defined(_WIN32)
    _putenv_s("APPDATA", directory);
    _putenv_s("LOCALAPPDATA", directory);
    _putenv_s("SUNSET_CIRCUITS_CONFIG_HOME", directory);
   #else
    setenv("HOME", directory, 1);
    setenv("XDG_CONFIG_HOME", directory, 1);
    setenv("SUNSET_CIRCUITS_CONFIG_HOME", directory, 1);
   #endif
}

using HostEvent = DAF::UI::HostEvent;
using Values = std::vector<std::pair<uint32_t, float>>;

template <class Editor>
class EditorHost
{
    struct Context
    {
        ImGuiContext* context = ImGui::CreateContext();
        Context(float width, float height)
        {
            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(width, height);
            io.DeltaTime = 1.0f / 60.0f;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // as DAF runs ImGui (DearImGui.cpp)
            // DAF's ImGui widget puts its DejaVu Sans at 13 px first in the
            // atlas before the editor adds its own faces (DearImGui.cpp,
            // buildFonts), so it is the face windows default to, and the one
            // the value field's text is drawn in.
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            config.OversampleH = 1;
            config.OversampleV = 1;
            config.PixelSnapH = true;
            io.Fonts->AddFontFromMemoryTTF((void*) daf_resources::dejavusans_ttf,
                                           (int) daf_resources::dejavusans_ttf_size, 13.0f, &config);
            io.Fonts->Build();
        }
        ~Context() { ImGui::DestroyContext(context); }
    } context_;

public:
    Editor ui;

    // At the design size the editor draws at scale 1 from the window origin, so
    // a design coordinate is a window pixel.
    EditorHost(uint32_t width, uint32_t height, const Values& start)
        : context_((float) width, (float) height)
    {
        ui.width = width;
        ui.height = height;
        // The editor adds its own faces and builds the atlas; when it finds
        // none it leaves the atlas as the host built it.
        ImFontAtlas& fonts = *ImGui::GetIO().Fonts;
        if (!fonts.IsBuilt())
            fonts.Build();
        for (const auto& [parameter, value] : start)
            ui.parameter(parameter, value);
        idle();
    }

    void frame()
    {
        ImGui::SetCurrentContext(context_.context);
        ImGui::NewFrame();
        ui.draw();
        ImGui::Render();
    }
    // Long enough to leave ImGui's 0.3 s double-click window, and for a field
    // that was asked for to take keyboard focus.
    void idle(int frames = 30) { for (int i = 0; i < frames; ++i) frame(); }
    ImGuiIO& io() { ImGui::SetCurrentContext(context_.context); return ImGui::GetIO(); }

    // One input event per frame, as a host delivers them at worst.
    void move(float x, float y) { io().AddMousePosEvent(x, y); frame(); }
    void press() { io().AddMouseButtonEvent(0, true); frame(); }
    void release() { io().AddMouseButtonEvent(0, false); frame(); }
    void click(float x, float y) { move(x, y); press(); release(); }
    void doubleClick(float x, float y) { move(x, y); press(); release(); press(); release(); }
    void drag(float x, float y, float dx, float dy)
    {
        move(x, y); press(); move(x + 0.5f * dx, y + 0.5f * dy); move(x + dx, y + dy); release();
    }
    void key(ImGuiKey k) { io().AddKeyEvent(k, true); frame(); io().AddKeyEvent(k, false); frame(); }
    void modifier(ImGuiKey mod, bool down) { io().AddKeyEvent(mod, down); frame(); }
    void type(const char* text) { io().AddInputCharactersUTF8(text); frame(); }
    void wheel(float steps) { io().AddMouseWheelEvent(0.0f, steps); frame(); }

    // The inline field is the only text field these editors open over a knob:
    // one that holds the keyboard is the open field.
    bool fieldActive() const
    {
        const ImGuiContext& g = *context_.context;
        return g.ActiveId != 0 && g.ActiveId == g.InputTextState.ID;
    }
    int fieldTextLength() const { return context_.context->InputTextState.TextLen; }
    // The width of the field's text as drawn: in the face windows default to,
    // since the field is drawn in the editor's window with no font pushed.
    float fieldTextWidth()
    {
        ImGuiIO& o = io();
        ImFont* font = o.FontDefault != nullptr ? o.FontDefault : o.Fonts->Fonts[0];
        const ImGuiInputTextState& state = context_.context->InputTextState;
        return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, state.TextA.Data,
                                   state.TextA.Data + state.TextLen).x;
    }
    int fieldSelectionLength() const
    {
        const ImGuiInputTextState& state = context_.context->InputTextState;
        return state.HasSelection() ? std::abs(state.GetSelectionEnd() - state.GetSelectionStart()) : 0;
    }
    bool tooltipShown()
    {
        ImGui::SetCurrentContext(context_.context);
        const ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
        return tooltip != nullptr && tooltip->Active;
    }
    // How often the last frame replaced one tooltip with another. In this Dear
    // ImGui (1.91.9) only SetTooltip and SetItemTooltip replace: the second opens
    // "##Tooltip_01" over the first and counts here. BeginTooltip and
    // BeginItemTooltip append to the tooltip already open and count nothing, so
    // tooltipContent() covers them.
    int tooltipOverrides() const { return context_.context->TooltipOverrideCount; }
    // The extent of what the tooltip on screen holds at the end of the last frame
    // (zero with none): a second text appended to it makes it taller.
    ImVec2 tooltipContent()
    {
        ImGui::SetCurrentContext(context_.context);
        char name[32];
        std::snprintf(name, sizeof(name), "##Tooltip_%02d", context_.context->TooltipOverrideCount);
        const ImGuiWindow* tooltip = ImGui::FindWindowByName(name);
        if (tooltip == nullptr || !tooltip->Active)
            return ImVec2(0.0f, 0.0f);
        return ImVec2(tooltip->DC.CursorMaxPos.x - tooltip->DC.CursorStartPos.x,
                      tooltip->DC.CursorMaxPos.y - tooltip->DC.CursorStartPos.y);
    }
    // Value bubbles draw on the foreground list, which nothing else in these
    // editors draws on.
    int foregroundVertices()
    {
        ImGui::SetCurrentContext(context_.context);
        return ImGui::GetForegroundDrawList()->VtxBuffer.Size;
    }

    size_t mark() const { return ui.hostEvents.size(); }
    int count(size_t from, HostEvent::Kind kind, uint32_t parameter) const
    {
        int n = 0;
        for (size_t i = from; i < ui.hostEvents.size(); ++i)
            n += ui.hostEvents[i].kind == kind && ui.hostEvents[i].parameter == parameter;
        return n;
    }
    int eventsSince(size_t from) const { return (int) (ui.hostEvents.size() - from); }
    bool lastWrite(size_t from, uint32_t parameter, float& value) const
    {
        bool found = false;
        for (size_t i = from; i < ui.hostEvents.size(); ++i)
            if (ui.hostEvents[i].kind == HostEvent::Write && ui.hostEvents[i].parameter == parameter)
            {
                value = ui.hostEvents[i].value;
                found = true;
            }
        return found;
    }
    // Each write inside an open gesture on its parameter, no gesture begun twice
    // or ended without a begin, none left open.
    bool protocolOk(size_t from) const
    {
        std::map<uint32_t, bool> open;
        for (size_t i = from; i < ui.hostEvents.size(); ++i)
        {
            const HostEvent& e = ui.hostEvents[i];
            bool& isOpen = open[e.parameter];
            if (e.kind == HostEvent::Begin) { if (isOpen) return false; isOpen = true; }
            else if (e.kind == HostEvent::End) { if (!isOpen) return false; isOpen = false; }
            else if (!isOpen) return false;
        }
        for (const auto& [parameter, isOpen] : open)
            if (isOpen) return false;
        return true;
    }
    void printEvents(size_t from) const
    {
        for (size_t i = from; i < ui.hostEvents.size(); ++i)
        {
            const HostEvent& e = ui.hostEvents[i];
            std::printf("    %s %u%s", e.kind == HostEvent::Begin ? "begin" : (e.kind == HostEvent::End ? "end" : "write"),
                        e.parameter, e.kind == HostEvent::Write ? "" : "\n");
            if (e.kind == HostEvent::Write) std::printf(" = %g\n", (double) e.value);
        }
    }
};

// One knob under test, in window pixels at the design size.
struct ValueFieldKnob
{
    std::string name;
    float x = 0, y = 0;                // knob centre; the field opens centred on it
    float radius = 0;                  // half the side of the knob's square button
    float aboveField = 0;              // a point on the knob above its field, as an offset from the centre (< 0)
    ImVec2 outside;                    // panel with no control under it
    std::vector<uint32_t> parameters;  // every parameter one drag of the knob opens a gesture on
    uint32_t valueParameter = 0;       // the parameter typed entry writes, and the checks read
    Values start;                      // host values set first: character/mode, a mid-travel position
    const char* typed = "";            // valid entry, different from the start value
    float committed = 0;               // valueParameter after `typed` commits
    ImGuiKey resetModifier = ImGuiMod_Super;   // held during a click, resets the knob
    uint32_t resetParameter = 0;
    float resetValue = 0;
    enum class Hover { Bubble, Tooltip, Nothing } hover = Hover::Bubble;   // what a resting hover shows
};

// The inline field is 58 x 22 design px centred on the knob (DuskPanel::valueEdit),
// its text left-aligned after a 4 px frame padding.
constexpr float kFieldHalfWidth = 29.0f;
constexpr float kFieldPadding = 4.0f;
// The click on the start of the text, as an offset from the knob centre.
constexpr float kTextStart = -kFieldHalfWidth + kFieldPadding + 3.0f;

template <class Editor>
class ValueFieldSuite
{
public:
    using Host = EditorHost<Editor>;
    ValueFieldSuite(uint32_t width, uint32_t height) : width_(width), height_(height) {}

    void run(const ValueFieldKnob& k, const ValueFieldKnob& neighbour) const
    {
        insideTheField(k);
        doubleClickHeldAndDragged(k);
        for (int how = 0; how < 3; ++how)
            closeThenDrag(k, how);
        pressOnTheKnobAboveTheField(k);
        neighbourWhileOpen(k, neighbour);
        fieldNeverOpened(k);
    }

    // The item-query rule (top of this file): with k's field open, hovering
    // `previous`, the item submitted just before k, shows what it shows with the
    // field closed. A tooltip read left outside k's skipped button answers for
    // `previous` there and adds k's tooltip: replacing the one shown (counted by
    // tooltipOverrides) or appended to it (a taller tooltipContent).
    void previousItemTooltip(const ValueFieldKnob& k, const ValueFieldKnob& previous) const
    {
        Host closed(width_, height_, k.start);
        closed.move(previous.x, previous.y);
        closed.idle(60);
        const bool shownClosed = closed.tooltipShown();
        const int overridesClosed = closed.tooltipOverrides();
        const ImVec2 contentClosed = closed.tooltipContent();

        Host h(width_, height_, k.start);
        if (!open(h, k))
            return;
        h.move(previous.x, previous.y);
        h.idle(60);
        const ImVec2 contentOpen = h.tooltipContent();
        const bool ok = h.fieldActive() && shownClosed && h.tooltipShown() && overridesClosed == 0
            && h.tooltipOverrides() == 0 && contentClosed.y > 0.0f
            && contentOpen.x == contentClosed.x && contentOpen.y == contentClosed.y;
        guards(ok, k.name + ": with its field open, hovering " + previous.name
               + " (the item before it) shows only " + previous.name + "'s tooltip");
        if (!ok)
            std::printf("    field closed: shown %d, replaced %d, %.1f x %.1f; field open: shown %d, replaced %d, %.1f x %.1f\n",
                        shownClosed, overridesClosed, (double) contentClosed.x, (double) contentClosed.y,
                        h.tooltipShown(), h.tooltipOverrides(), (double) contentOpen.x, (double) contentOpen.y);
    }

private:
    uint32_t width_, height_;

    // A press at (px, py), then a 20 px drag up: one gesture per parameter,
    // writes, protocol.
    static bool dragsWithOneGesture(Host& h, const ValueFieldKnob& k, const std::string& what, float px, float py)
    {
        const size_t m = h.mark();
        h.drag(px, py, 0.0f, -20.0f);
        h.idle(2);
        bool ok = h.protocolOk(m);
        int writes = 0;
        for (uint32_t p : k.parameters)
        {
            ok = ok && h.count(m, HostEvent::Begin, p) == 1 && h.count(m, HostEvent::End, p) == 1;
            writes += h.count(m, HostEvent::Write, p);
        }
        ok = ok && writes > 0;
        guards(ok, k.name + ": " + what);
        if (!ok) h.printEvents(m);
        return ok;
    }
    static bool dragsWithOneGesture(Host& h, const ValueFieldKnob& k, const std::string& when)
    {
        return dragsWithOneGesture(h, k, when + ", a drag on the knob writes inside exactly one gesture per parameter",
                                   k.x, k.y);
    }

    bool open(Host& h, const ValueFieldKnob& k) const
    {
        const size_t m = h.mark();
        h.doubleClick(k.x, k.y);
        h.idle();
        const bool ok = h.fieldActive() && h.protocolOk(m);
        guards(ok, k.name + ": a double-click opens the value field and leaves no gesture open");
        if (!ok) h.printEvents(m);
        return ok;
    }

    // #337 itself: a click, and a click-drag, inside the open field stay in it.
    void insideTheField(const ValueFieldKnob& k) const
    {
        Host h(width_, height_, k.start);
        if (!open(h, k))
            return;
        // The drag below starts right of the start value's text, so it crosses
        // all of it, and over the knob: at the middle of the field, or just past
        // the text where the text runs past the middle. Measured in the face the
        // field is drawn in, while it still shows the start value.
        const float textEnd = k.x - kFieldHalfWidth + kFieldPadding + h.fieldTextWidth();
        const float pressX = std::max(k.x, textEnd + 3.0f);
        char where[96];
        std::snprintf(where, sizeof(where), " (text ends %+.1f px from the centre, drag starts %+.1f px)",
                      (double) (textEnd - k.x), (double) (pressX - k.x));
        guards(pressX < k.x + k.radius && pressX < k.x + kFieldHalfWidth - 1.0f,
               k.name + ": the field has a point right of its text over the knob" + where);

        // On a small knob the start of the text is not over the knob, so the
        // click there fails on the unfixed editors only where it is.
        const bool textOverKnob = -kTextStart < k.radius;
        const auto onText = textOverKnob ? detects : guards;
        const size_t m = h.mark();
        h.click(k.x + kTextStart, k.y);
        h.idle();
        onText(h.fieldActive(), k.name + ": a click on the text in the open field keeps it open");
        onText(h.fieldActive() && h.fieldSelectionLength() == 0,
               k.name + ": that click places the caret (ends the select-all the field opened with)");
        onText(h.eventsSince(m) == 0, k.name + ": that click sends no gesture and no write to the host");
        if (h.eventsSince(m) != 0) h.printEvents(m);

        h.click(k.x, k.y);
        h.idle();
        detects(h.fieldActive(), k.name + ": a click in the middle of the open field, over the knob, keeps it open");
        detects(h.eventsSince(m) == 0, k.name + ": that click sends no gesture and no write to the host");
        if (h.eventsSince(m) != 0) h.printEvents(m);

        h.drag(pressX, k.y, k.x - kFieldHalfWidth + 2.0f - pressX, 0.0f);   // to the field's left end
        h.idle();
        detects(h.fieldActive(), k.name + ": a click-drag inside the open field keeps it open");
        detects(h.fieldActive() && h.fieldTextLength() > 0 && h.fieldSelectionLength() == h.fieldTextLength(),
                k.name + ": that click-drag selects the text it crosses");
        detects(h.eventsSince(m) == 0, k.name + ": that click-drag sends no gesture and no write to the host");
        if (h.eventsSince(m) != 0) h.printEvents(m);

        h.type(k.typed);
        h.key(ImGuiKey_Enter);
        h.idle(2);
        float value = 0;
        const bool written = h.lastWrite(m, k.valueParameter, value);
        const bool ok = !h.fieldActive() && written && std::fabs(value - k.committed) <= 1.0e-3f * (1.0f + std::fabs(k.committed))
            && h.count(m, HostEvent::Begin, k.valueParameter) == 1 && h.protocolOk(m);
        detects(ok, k.name + ": typing over that selection and Enter commit the value in one gesture");
        if (!ok) { std::printf("    committed %g, expected %g\n", (double) value, (double) k.committed); h.printEvents(m); }
    }

    // The second press of the double-click that opens the field, held and
    // dragged, moves nothing. Before the fix the knob kept its button active
    // through the hold, and its gestures were already skipped while the field
    // was open; now the button goes away under the held press. This guards that
    // hand-off: a fix that let the knob's gestures run again would drag here.
    void doubleClickHeldAndDragged(const ValueFieldKnob& k) const
    {
        Host h(width_, height_, k.start);
        const size_t m = h.mark();
        h.move(k.x, k.y); h.press(); h.release(); h.press();
        const size_t held = h.mark();
        h.move(k.x, k.y - 10.0f); h.move(k.x, k.y - 20.0f); h.release();
        h.idle();
        int writes = 0;
        for (uint32_t p : k.parameters) writes += h.count(held, HostEvent::Write, p);
        const bool ok = h.fieldActive() && writes == 0 && h.protocolOk(m);
        guards(ok, k.name + ": a double-click held and dragged opens the field and moves nothing");
        if (!ok) h.printEvents(m);
    }

    // After the field closes by Enter (0), Escape (1) or a click elsewhere (2),
    // the knob drags as before.
    void closeThenDrag(const ValueFieldKnob& k, int how) const
    {
        static const char* const kHow[3] = { "after Enter closes the field", "after Escape closes the field",
                                             "after a click elsewhere closes the field" };
        Host h(width_, height_, k.start);
        if (!open(h, k))
            return;
        const size_t m = h.mark();
        if (how == 0) h.key(ImGuiKey_Enter);
        else if (how == 1) h.key(ImGuiKey_Escape);
        else h.click(k.outside.x, k.outside.y);
        h.idle();
        const bool closed = !h.fieldActive() && h.eventsSince(m) == 0;
        guards(closed, k.name + ": " + kHow[how] + ", nothing reaches the host");
        if (!closed) h.printEvents(m);
        dragsWithOneGesture(h, k, kHow[how]);
    }

    // A press on the knob outside its open field. It closed the field before
    // the fix as well (the guard half); before the fix it then also dragged the
    // knob with no gesture open (the #337 half). The point is first shown to be
    // on the knob, so a layout change cannot turn the case into a press beside it.
    void pressOnTheKnobAboveTheField(const ValueFieldKnob& k) const
    {
        const float py = k.y + k.aboveField;
        {
            Host probe(width_, height_, k.start);
            dragsWithOneGesture(probe, k, "with the field closed, a drag from the point above the field "
                                "writes inside exactly one gesture per parameter (the point is on the knob)",
                                k.x, py);
        }
        Host h(width_, height_, k.start);
        if (!open(h, k))
            return;
        const size_t m = h.mark();
        h.drag(k.x, py, 0.0f, -20.0f);
        h.idle();
        guards(!h.fieldActive(), k.name + ": a press on the knob outside its open field closes the field");
        const bool ok = h.protocolOk(m);
        detects(ok, k.name + ": that press leaves no write outside a gesture and no unmatched gesture");
        // The text was not edited, so closing the field commits nothing, and the
        // press began while the knob had no button, so it starts no drag: a
        // balanced begin, write, end here would still be a drag nobody asked for.
        const bool silent = h.eventsSince(m) == 0;
        detects(silent, k.name + ": that press sends nothing to the host");
        if (!ok || !silent) h.printEvents(m);
    }

    // With this knob's field open, a drag on another knob behaves as it always
    // did: this field closes, the other knob drags in one gesture.
    void neighbourWhileOpen(const ValueFieldKnob& k, const ValueFieldKnob& other) const
    {
        Host h(width_, height_, k.start);
        for (const auto& [parameter, value] : other.start)
            h.ui.parameter(parameter, value);
        if (!open(h, k))
            return;
        const size_t m = h.mark();
        dragsWithOneGesture(h, other, "with " + k.name + "'s field open");
        int own = 0;
        for (uint32_t p : k.parameters)
            if (std::find(other.parameters.begin(), other.parameters.end(), p) == other.parameters.end())
                own += h.count(m, HostEvent::Begin, p) + h.count(m, HostEvent::Write, p);
        guards(!h.fieldActive() && own == 0,
               k.name + ": that drag closes " + k.name + "'s field without editing " + k.name);
    }

    // The field never opened: drag, Shift fine drag, wheel, modifier-click reset
    // and the resting hover read-out work as before.
    void fieldNeverOpened(const ValueFieldKnob& k) const
    {
        {
            Host h(width_, height_, k.start);
            dragsWithOneGesture(h, k, "with the field never opened");
        }
        float coarse = 0, fine = 0, from = 0;
        for (const auto& [parameter, value] : k.start)
            if (parameter == k.valueParameter) from = value;
        for (bool shift : { false, true })
        {
            Host h(width_, height_, k.start);
            if (shift) h.modifier(ImGuiMod_Shift, true);
            const size_t m = h.mark();
            h.drag(k.x, k.y, 0.0f, -10.0f);
            if (shift) h.modifier(ImGuiMod_Shift, false);
            h.idle(2);
            float value = from;
            h.lastWrite(m, k.valueParameter, value);
            (shift ? fine : coarse) = std::fabs(value - from);
            guards(h.protocolOk(m), k.name + (shift ? ": a Shift drag" : ": a plain 10 px drag") + " keeps the host protocol");
        }
        guards(fine > 0.0f && fine < coarse, k.name + ": Shift drags finer than a plain drag ("
               + std::to_string(fine) + " < " + std::to_string(coarse) + ")");
        {
            Host h(width_, height_, k.start);
            h.move(k.x, k.y);
            h.idle(2);
            const size_t m = h.mark();
            h.wheel(1.0f);
            h.idle(2);
            const bool ok = h.protocolOk(m) && h.count(m, HostEvent::Begin, k.valueParameter) == 1
                && h.count(m, HostEvent::Write, k.valueParameter) > 0;
            guards(ok, k.name + ": a wheel step over the knob writes in one gesture");
            if (!ok) h.printEvents(m);
        }
        {
            Host h(width_, height_, k.start);
            h.modifier(k.resetModifier, true);
            const size_t m = h.mark();
            h.click(k.x, k.y);
            h.modifier(k.resetModifier, false);
            h.idle(2);
            float value = -12345.0f;
            h.lastWrite(m, k.resetParameter, value);
            const bool ok = h.protocolOk(m) && h.count(m, HostEvent::Begin, k.resetParameter) == 1
                && value == k.resetValue && !h.fieldActive();
            guards(ok, k.name + ": a modifier-click resets the knob in one gesture");
            if (!ok) h.printEvents(m);
        }
        if (k.hover != ValueFieldKnob::Hover::Nothing)
        {
            Host h(width_, height_, k.start);
            h.move(-100.0f, -100.0f);
            h.idle(2);
            const bool restingBefore = k.hover == ValueFieldKnob::Hover::Tooltip ? h.tooltipShown() : h.foregroundVertices() > 0;
            h.move(k.x, k.y);
            h.idle(60);
            const bool shown = k.hover == ValueFieldKnob::Hover::Tooltip ? h.tooltipShown() : h.foregroundVertices() > 0;
            guards(!restingBefore && shown, k.name + (k.hover == ValueFieldKnob::Hover::Tooltip
                   ? ": a resting hover shows the tooltip" : ": a resting hover shows the value bubble"));
        }
    }
};

} // namespace dusktest
