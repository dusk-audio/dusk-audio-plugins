// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// 4K EQ 2's own knobs (the stepped filter knobs and the console detent knobs)
// and their inline value fields, on the production editor: a click inside an
// open field stays in the field (dusk-audio/plugins#337), and everything else
// the knobs do is unchanged. See DuskValueFieldTest.hpp for the cases.
//
// The same knobs edit only when a gesture moves them, the rule DuskPanel::knob
// keeps (DuskImGuiWidgets.hpp). A press and release without movement, or with
// mouse movement only before the press, writes nothing; a knob at an end of
// its travel pushed further out by a drag or a wheel step writes nothing; a
// band or filter that a legacy dial plays outside its knob's printed range
// keeps playing there until the knob is moved into the range (or a filter's to
// OUT, which switches it out and keeps its frequency); and a knob held still
// while the host moves its parameter shows the new value with its pointer.
// Those checks are tagged [click]: each fails on the editor before its knobs
// took the rule, where every frame of a held press wrote the knob's value (and
// so pulled such a band to the end of the range, and held the pointer where
// the press began). The [guard] checks pass on both: the controls (the same
// press point drags, the opposite direction edits, a moved drag keeps the
// pointer), and what the editor sent before, event for event (see sentAsBefore
// for how values are compared): reset, wheel steps, drags of 1 px per frame
// with and without Shift, the INPUT and OUTPUT knobs' press, and the value a
// typed entry commits.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "FourKEQUI.cpp"
#include "DuskValueFieldTest.hpp"

// The DSP bridge (FourKEQAccess.hpp) as an out-of-process UI sees it. Defined
// because a weak reference must still resolve when an Apple binary links.
float fourKEQGetInputPeakL(void*) noexcept { return 0; }
float fourKEQGetInputPeakR(void*) noexcept { return 0; }
float fourKEQGetOutputPeakL(void*) noexcept { return 0; }
float fourKEQGetOutputPeakR(void*) noexcept { return 0; }
const duskaudio::SpectrumRing* fourKEQGetPreSpectrum(void*) noexcept { return nullptr; }
const duskaudio::SpectrumRing* fourKEQGetPostSpectrum(void*) noexcept { return nullptr; }
uint32_t fourKEQGetLegacyDialBands(void*) noexcept { return 0; }
uint32_t fourKEQGetLegacyDialFilters(void*) noexcept { return 0; }

namespace
{
using Host = dusktest::EditorHost<DAF::FourKEQUI>;
using dusktest::HostEvent;
using dusktest::Values;

int clickChecks = 0, clickFailures = 0;

// A check that fails on the editor before its knobs edited only on movement
// (see the top of this file).
void detectsClick(bool ok, const std::string& what)
{
    std::printf("%s [click] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    dusktest::failures += ok ? 0 : 1;
    clickFailures += ok ? 0 : 1;
    ++clickChecks;
}

// dusktest::summary() with the [click] checks counted too.
int summary()
{
    std::printf("%s: %d failure(s) in %d [#337], %d [guard] and %d [click] checks, %d of them"
                " [click]\n", dusktest::failures ? "FAILED" : "PASSED", dusktest::failures,
                dusktest::issueChecks, dusktest::guardChecks, clickChecks, clickFailures);
    return dusktest::failures ? 1 : 0;
}

// Window pixels at the design size (FourKEQUI::drawColumns, graph shown): the
// column centres between COL[], rows cY(314) = 274, cY(452) = 412 and
// cY(590) = 550. Every knob below has radius 28.
constexpr float kFilters = 113.0f, kLf = 258.5f, kLmf = 404.0f, kHmf = 549.5f, kHf = 695.0f;
constexpr float kMaster = 844.0f, kTop = 274.0f, kMid = 412.0f, kLow = 550.0f;

// One of the editor's own knobs at a start state.
struct OwnKnob
{
    std::string name;
    float x = 0, y = 0;
    std::vector<uint32_t> gesture;   // the parameters a press opens a gesture on
    uint32_t watched = 0;            // what the checks read: a band's or filter's Hz, or the gain or Q
    Values start;
    float lo = 0, hi = 0;            // the knob's printed range of `watched`
};

OwnKnob consoleKnob(std::string name, float x, float y, uint32_t parameter, Values start,
                    float lo, float hi)
{
    OwnKnob k;
    k.name = std::move(name);
    k.x = x;
    k.y = y;
    k.gesture = { parameter };
    k.watched = parameter;
    k.start = std::move(start);
    k.lo = lo;
    k.hi = hi;
    return k;
}

OwnKnob filterKnob(std::string name, bool lowPass, Values start)
{
    OwnKnob k;
    k.name = std::move(name);
    k.x = kFilters;
    k.y = lowPass ? kMid : kTop;
    k.gesture = { lowPass ? kLpfEnabled : kHpfEnabled, lowPass ? kLpfHz : kHpfHz };
    k.watched = lowPass ? kLpfHz : kHpfHz;
    k.start = std::move(start);
    k.lo = lowPass ? 3000.0f : 16.0f;
    k.hi = lowPass ? 15201.0f : 350.0f;
    return k;
}

// What the plugin plays for `watched` once the host has forwarded `start` and
// every write the editor sent. The plugin moves its legacy dial selectors by
// the rule fkStoreParam applies (fkSelectorWrite).
struct Played { float value; bool legacyDial; };
Played played(const Host& h, const Values& start, uint32_t watched)
{
    float v[kParamCount];
    for (uint32_t i = 0; i < kParamCount; ++i)
        v[i] = kFourKParams[i].def;
    for (const auto& [parameter, value] : start)
        fkStoreParam(v, parameter, value);
    for (const HostEvent& e : h.ui.hostEvents)
        if (e.kind == HostEvent::Write)
            fkStoreParam(v, e.parameter, e.value);
    if (const int b = fkBandOfHzParam(watched); b >= 0)
        return { fkBandHz(v, b), fkBandFollowsLegacyDial(v, b) };
    if (const int f = fkFilterOfHzParam(watched); f >= 0)
        return { fkFilterHz(v, f), fkFilterFollowsLegacyDial(v, f) };
    return { v[watched], false };
}

int writesSince(const Host& h, size_t from)
{
    int n = 0;
    for (size_t i = from; i < h.ui.hostEvents.size(); ++i)
        n += h.ui.hostEvents[i].kind == HostEvent::Write;
    return n;
}

// Exactly one gesture per parameter of the knob, and the protocol kept.
bool oneGestureEach(const Host& h, size_t from, const OwnKnob& k)
{
    bool ok = h.protocolOk(from);
    for (uint32_t p : k.gesture)
        ok = ok && h.count(from, HostEvent::Begin, p) == 1 && h.count(from, HostEvent::End, p) == 1;
    return ok;
}

std::string hz(float v)
{
    char s[32];
    std::snprintf(s, sizeof(s), "%.6g", (double) v);
    return s;
}

// The events a gesture sent: each event's kind and parameter exactly, and each
// written value either exactly, where nothing rounds it (a default, a clamp end,
// a switch or selector value), or, where float arithmetic computes it (a knob
// position moved by a drag or a wheel step, then interpolated between detents),
// within kComputed of it. Compilers fuse those multiply-adds or not (clang on
// arm64 does by default, GCC for baseline x86-64 cannot), which moves the last
// bits: 123.400009 Hz against 123.400017. kComputed, 1e-4 of the value, lies 30
// times below the smallest step a computed value here takes (a 1 px Shift drag
// from 77 Hz moves 0.24 Hz, 3.1e-3 of the value) and some 100 times above what
// that rounding can reach (about 1e-6 of the value by the arithmetic of a gain
// wheel step; the largest difference observed is under 1e-7).
constexpr float kComputed = 1.0e-4f;
struct Sent
{
    HostEvent::Kind kind;
    uint32_t parameter;
    float value;
    bool computed = false;
};
Sent computedWrite(uint32_t parameter, float value)
{
    return { HostEvent::Write, parameter, value, true };
}
bool sentAsBefore(const Host& h, size_t from, const std::vector<Sent>& expected)
{
    if (h.ui.hostEvents.size() - from != expected.size())
        return false;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        const HostEvent& e = h.ui.hostEvents[from + i];
        const Sent& x = expected[i];
        if (e.kind != x.kind || e.parameter != x.parameter)
            return false;
        if (e.kind == HostEvent::Write
            && (x.computed ? !(std::fabs(e.value - x.value) <= kComputed * std::fabs(x.value))
                           : e.value != x.value))
            return false;
    }
    return true;
}
// The events since `from`, with values to 9 digits (enough to tell floats apart).
void printSent(const Host& h, size_t from)
{
    for (size_t i = from; i < h.ui.hostEvents.size(); ++i)
    {
        const HostEvent& e = h.ui.hostEvents[i];
        const char* kind = e.kind == HostEvent::Begin ? "begin"
                         : e.kind == HostEvent::End ? "end" : "write";
        std::printf("    %s %s", kind, kFourKParams[e.parameter].key);
        if (e.kind == HostEvent::Write)
            std::printf(" = %.9g", (double) e.value);
        std::printf("\n");
    }
}

// A drag of dy px from the knob's centre writes, in one gesture per parameter.
// With the press point shown live this way, a case that sends nothing from the
// same point cannot pass by missing the knob.
void dragControl(const OwnKnob& k, float dy, const std::string& what)
{
    Host h(960, 640, k.start);
    const size_t m = h.mark();
    h.drag(k.x, k.y, 0.0f, dy);
    h.idle(2);
    const bool ok = oneGestureEach(h, m, k) && writesSince(h, m) > 0;
    dusktest::guards(ok, k.name + ": control, " + what);
    if (!ok) h.printEvents(m);
}

// A press and release without movement writes nothing: it sends only the begin
// and end of one gesture per parameter, as DuskPanel::knob does (the press may
// still become a drag). controlDy: a drag from the same point that moves the
// knob (down for one at the top of its travel).
void motionlessPress(const OwnKnob& k, float controlDy = -10.0f)
{
    Host h(960, 640, k.start);
    h.move(k.x, k.y);
    h.idle(2);
    const Played before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.press();
    h.idle(5);
    h.release();
    h.idle(2);
    const bool silent = writesSince(h, m) == 0 && h.eventsSince(m) == 2 * (int) k.gesture.size();
    detectsClick(silent, k.name + ": a press held 5 frames and released without movement writes"
                 " nothing, only the begin and end of its gesture");
    if (!silent) h.printEvents(m);
    dusktest::guards(oneGestureEach(h, m, k),
                     k.name + ": that press opens and closes one gesture per parameter");
    const Played after = played(h, k.start, k.watched);
    if (before.value < k.lo || before.value > k.hi)
        detectsClick(after.value == before.value && after.legacyDial && before.legacyDial,
                     k.name + ": keeps playing " + hz(before.value) + " Hz, outside the knob's "
                     + hz(k.lo) + ".." + hz(k.hi) + ", on its legacy dial (plays " + hz(after.value)
                     + ")");
    dragControl(k, controlDy, "a " + hz(std::fabs(controlDy)) + " px drag "
                + (controlDy < 0 ? "up" : "down")
                + " from the same point writes in one gesture per parameter");
}

// Mouse movement before the press, in the press frame (a host may deliver both
// in one frame), is not a drag: the knob writes nothing until the mouse moves
// with the button held.
void movedIntoThePressFrame(const OwnKnob& k)
{
    Host h(960, 640, k.start);
    h.move(k.x, k.y + 10.0f);
    h.idle(2);
    const size_t m = h.mark();
    h.io().AddMousePosEvent(k.x, k.y);
    h.io().AddMouseButtonEvent(0, true);
    h.frame();
    h.idle(5);
    h.release();
    h.idle(2);
    detectsClick(writesSince(h, m) == 0,
                 k.name + ": mouse movement in the press frame, before the press, writes nothing");
    if (writesSince(h, m) != 0) h.printEvents(m);

    Host c(960, 640, k.start);
    c.move(k.x, k.y);
    c.idle(2);
    const size_t n = c.mark();
    c.press();
    c.move(k.x, k.y - 10.0f);
    c.release();
    c.idle(2);
    const bool ok = oneGestureEach(c, n, k) && writesSince(c, n) > 0;
    dusktest::guards(ok, k.name + ": control, the same 10 px moved one frame after the press writes in"
                     " one gesture per parameter");
    if (!ok) c.printEvents(n);
}

// A drag of 1 px per frame, the smallest step (Dear ImGui floors mouse
// positions), with or without Shift: what the editor wrote before (see
// sentAsBefore), one write per moved frame. Under Shift 1 px is 0.0008 of the
// travel, so a gate that let small moves through only above some size would
// drop them.
void slowDrag(const OwnKnob& k, bool shift, int frames, const std::vector<Sent>& before)
{
    Host h(960, 640, k.start);
    if (shift) h.modifier(ImGuiMod_Shift, true);
    h.move(k.x, k.y);
    h.press();
    const size_t m = h.mark();
    for (int i = 1; i <= frames; ++i)
        h.move(k.x, k.y - (float) i);
    h.release();
    if (shift) h.modifier(ImGuiMod_Shift, false);
    h.idle(2);
    const bool ok = sentAsBefore(h, m, before);
    dusktest::guards(ok, k.name + ": a " + (shift ? "Shift " : "") + "drag up of 1 px per frame for "
                     + std::to_string(frames) + " frame(s) sends what it sent before");
    if (!ok) printSent(h, m);
}

// The knob sits at an end of its travel (`outward` -1: the top, so up pushes
// further out; +1: the bottom). A drag of `push` px and a wheel step further
// out write nothing, and a band or filter beyond the range keeps playing what
// it plays. A 20 px drag and one wheel event of `inwardNotches` notches the
// other way edit (and bring it into range).
void pinnedAndPushedOut(const OwnKnob& k, float outward, float push = 20.0f, float inwardNotches = 1.0f)
{
    const bool beyond = [&] {
        Host h(960, 640, k.start);
        const float v = played(h, k.start, k.watched).value;
        return v < k.lo || v > k.hi;
    }();
    {
        Host h(960, 640, k.start);
        const Played before = played(h, k.start, k.watched);
        const size_t m = h.mark();
        h.drag(k.x, k.y, 0.0f, outward * push);
        h.idle(2);
        detectsClick(writesSince(h, m) == 0, k.name + ": a " + hz(push) + " px drag "
                     + (outward < 0 ? "up" : "down")
                     + ", further out from the end it sits at, writes nothing");
        if (writesSince(h, m) != 0) h.printEvents(m);
        if (beyond)
        {
            const Played after = played(h, k.start, k.watched);
            detectsClick(after.value == before.value && after.legacyDial,
                         k.name + ": keeps playing " + hz(before.value)
                         + " Hz on its legacy dial (plays " + hz(after.value) + ")");
        }
    }
    {
        Host h(960, 640, k.start);
        h.move(k.x, k.y);
        h.idle(2);
        const size_t m = h.mark();
        h.wheel(-outward);
        h.idle(2);
        detectsClick(h.eventsSince(m) == 0, k.name + ": a wheel step " + (outward < 0 ? "up" : "down")
                     + ", further out, sends nothing");
        if (h.eventsSince(m) != 0) h.printEvents(m);
    }
    {
        Host h(960, 640, k.start);
        const size_t m = h.mark();
        h.drag(k.x, k.y, 0.0f, -outward * 20.0f);
        h.idle(2);
        const Played after = played(h, k.start, k.watched);
        bool ok = oneGestureEach(h, m, k) && writesSince(h, m) > 0;
        if (beyond)
            ok = ok && !after.legacyDial && after.value >= k.lo && after.value <= k.hi;
        dusktest::guards(ok, k.name + ": control, a 20 px drag the other way writes in one gesture per"
                         " parameter" + (beyond ? " and brings it into the knob's range (plays "
                         + hz(after.value) + ")" : std::string()));
        if (!ok) h.printEvents(m);
    }
    {
        Host h(960, 640, k.start);
        h.move(k.x, k.y);
        h.idle(2);
        const size_t m = h.mark();
        h.wheel(outward * inwardNotches);
        h.idle(2);
        const bool ok = oneGestureEach(h, m, k) && writesSince(h, m) > 0;
        dusktest::guards(ok, k.name + ": control, one wheel event of " + hz(inwardNotches)
                         + " notch(es) the other way writes in one gesture");
        if (!ok) h.printEvents(m);
    }
}

// A filter that plays beyond its first detent (an HPF below 16 Hz, an LPF above
// 15.2 kHz) points at that detent, and the stretch down to OUT plays the detent.
// Dragged through it to OUT, the filter is switched OUT and nothing else is
// written: its frequency and legacy dial stay as they were.
void flatStretchToOut(const OwnKnob& k, uint32_t enabled)
{
    Host h(960, 640, k.start);
    const Played before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.drag(k.x, k.y, 0.0f, 30.0f);
    h.idle(2);
    float en = -1.0f;
    h.lastWrite(m, enabled, en);
    const Played after = played(h, k.start, k.watched);
    const bool ok = en == 0.0f && h.count(m, HostEvent::Write, k.watched) == 0
        && h.count(m, HostEvent::Write, kLegacyDialFilters) == 0 && after.legacyDial
        && after.value == before.value;
    detectsClick(ok, k.name + ": a drag down through the stretch below its first detent to OUT writes"
                 " only the switch to OUT");
    if (!ok) h.printEvents(m);
    dusktest::guards(oneGestureEach(h, m, k) && en == 0.0f,
                     k.name + ": control, that drag switches the filter OUT in one gesture");
}

// The angle of the knob's pointer in the last frame, read from the draw data,
// as DuskPanel::knobAngle measures it (clockwise from 12 o'clock).
// drawMetalKnobBody draws the pointer as one line on the cap (0.74 of the
// knob's radius of 28 px) from 0.12 to 0.92 of the cap's radius, in its own
// colour; the line's core vertices sit in pairs at its two ends. The angle is
// that of the line from the mean of its inner end to the mean of its outer
// end, so it does not depend on where the knob's centre is taken to be. The
// value bubble is on the foreground list and is left out. NaN when the line
// is not found.
float pointerAngle(Host& h, const OwnKnob& k, ImU32 colour)
{
    h.io();   // makes the host's context current
    const ImDrawList* foreground = ImGui::GetForegroundDrawList();
    const ImDrawData* data = ImGui::GetDrawData();
    std::vector<ImVec2> line;
    for (int l = 0; data != nullptr && l < data->CmdListsCount; ++l)
    {
        if (data->CmdLists[l] == foreground)
            continue;
        for (const ImDrawVert& v : data->CmdLists[l]->VtxBuffer)
            if (v.col == colour && std::hypot(v.pos.x - k.x, v.pos.y - k.y) < 0.74f * 28.0f)
                line.push_back(v.pos);
    }
    const float midway = 0.52f * 0.74f * 28.0f;   // between the two ends
    double ix = 0, iy = 0, ox = 0, oy = 0;
    int inner = 0, outer = 0;
    for (const ImVec2& p : line)
    {
        const bool out = std::hypot(p.x - k.x, p.y - k.y) > midway;
        (out ? ox : ix) += p.x;
        (out ? oy : iy) += p.y;
        ++(out ? outer : inner);
    }
    if (inner == 0 || outer == 0)
        return std::nanf("");
    return (float) std::atan2(ox / outer - ix / inner, iy / inner - oy / outer);
}

std::string degrees(float radians)
{
    return hz(radians * 180.0f / 3.14159265f);
}

// A knob held still while the host moves its parameter (automation playing,
// say): its pointer shows the parameter, as DuskPanel::knob's does, also when
// an earlier gesture on the same knob moved it. Once the drag has moved the
// knob, its pointer follows the drag and the host's value does not move it
// while the knob is held. `colour`: the knob's pointer colour; `to` sits on a
// detent at 0.75 of the travel, where the pointer is at 67.5 deg.
void pointerWhileHeld(const OwnKnob& k, ImU32 colour, float to)
{
    constexpr float same = 1.0e-4f;   // radians
    const float detent = duskdaf::DuskPanel::knobAngle(0.75f);
    Values atTo = k.start;
    atTo.push_back({ k.watched, to });
    Host resting(960, 640, k.start), restingAtTo(960, 640, atTo);
    resting.idle(2);
    restingAtTo.idle(2);
    const float a0 = pointerAngle(resting, k, colour), a1 = pointerAngle(restingAtTo, k, colour);
    dusktest::guards(std::isfinite(a0) && std::fabs(a1 - detent) < 0.005f && std::fabs(a1 - a0) > 0.1f,
                     k.name + ": its pointer is read from the draw data: at rest " + degrees(a0)
                     + " deg, and " + degrees(a1) + " deg at " + hz(to) + " (the detent at 67.5 deg)");

    Host h(960, 640, k.start);
    h.drag(k.x, k.y, 0.0f, -10.0f);   // a gesture that moved the knob, then let go
    h.idle(2);
    const float rest = pointerAngle(h, k, colour);
    h.press();                        // at the end of that drag, still on the knob
    h.idle(3);
    const float pressed = pointerAngle(h, k, colour);
    h.ui.parameter(k.watched, to);
    h.idle(2);
    const float held = pointerAngle(h, k, colour);
    h.release();
    detectsClick(std::fabs(rest - a0) > 0.01f && std::fabs(pressed - rest) < same
                 && std::fabs(held - a1) < same,
                 k.name + ": after a drag, pressed again and held still while the host moves it to "
                 + hz(to) + ", its pointer follows (" + degrees(pressed) + " deg pressed, "
                 + degrees(held) + " deg after)");

    Host g(960, 640, k.start);
    g.move(k.x, k.y);
    g.idle(2);
    g.press();
    g.move(k.x, k.y - 10.0f);
    g.idle(2);
    const float dragged = pointerAngle(g, k, colour);
    g.ui.parameter(k.watched, to);
    g.idle(2);
    const float after = pointerAngle(g, k, colour);
    g.release();
    dusktest::guards(std::fabs(dragged - a0) > 0.01f && std::fabs(after - dragged) < same,
                     k.name + ": control, once a 10 px drag has moved it, its pointer stays with the"
                     " drag (" + degrees(dragged) + " deg) when the host moves it (" + degrees(after)
                     + " deg)");
}

// A filter that plays beyond its first detent points at it (0.1 of the travel),
// and a drag down into the stretch above OUT writes nothing (stepSettingMoves).
// The knob still turns with the drag: its pointer follows a gesture once the
// gesture has moved, whether or not it wrote, and goes back to the parameter
// when the knob is let go.
void pointerInTheStretchAboveOut(const OwnKnob& k, ImU32 colour)
{
    const float atDetent = duskdaf::DuskPanel::knobAngle(0.1f);
    const float dragged = duskdaf::DuskPanel::knobAngle(0.07f);   // 6 px down at 0.005 per px
    Host h(960, 640, k.start);
    h.move(k.x, k.y);
    h.idle(2);
    const float rest = pointerAngle(h, k, colour);
    const Played before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.press();
    h.move(k.x, k.y + 3.0f);
    h.move(k.x, k.y + 6.0f);
    h.idle(2);
    const float held = pointerAngle(h, k, colour);
    const Played during = played(h, k.start, k.watched);
    detectsClick(std::fabs(rest - atDetent) < 0.005f && std::fabs(held - dragged) < 0.005f
                 && writesSince(h, m) == 0 && during.value == before.value && during.legacyDial,
                 k.name + ": dragged 6 px down into the stretch above OUT and held, its pointer"
                 " turns with the drag (" + degrees(rest) + " to " + degrees(held)
                 + " deg) while nothing is written");
    if (writesSince(h, m) != 0) printSent(h, m);
    h.release();
    h.idle(2);
    const float after = pointerAngle(h, k, colour);
    dusktest::guards(std::fabs(after - atDetent) < 0.005f,
                     k.name + ": control, let go, its pointer is back at the first detent ("
                     + degrees(after) + " deg)");
}

// Gestures that the rule leaves alone send what they sent before it, value
// for value.
enum class Unchanged { Wheel, Reset, Press };
void sendsAsBefore(const OwnKnob& k, Unchanged gesture, float notches, const std::vector<Sent>& before,
                   const std::string& what)
{
    Host h(960, 640, k.start);
    h.move(k.x, k.y);
    h.idle(2);
    const size_t m = h.mark();
    if (gesture == Unchanged::Wheel)
        h.wheel(notches);
    else if (gesture == Unchanged::Reset)
    {
        h.modifier(ImGuiMod_Super, true);
        h.click(k.x, k.y);
        h.modifier(ImGuiMod_Super, false);
    }
    else
    {
        h.press();
        h.idle(5);
        h.release();
    }
    h.idle(2);
    const bool ok = sentAsBefore(h, m, before);
    dusktest::guards(ok, k.name + ": " + what + " sends what it sent before");
    if (!ok) printSent(h, m);
}

// Double-click to type on a band or filter that a legacy dial plays outside
// the knob's range: the field opens and nothing moves it until a value is
// committed; the committed value is written as before, in one gesture.
void typeOverLegacy(const OwnKnob& k, const char* text, float committed)
{
    Host h(960, 640, k.start);
    const Played before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.doubleClick(k.x, k.y);
    h.idle();
    const Played open = played(h, k.start, k.watched);
    detectsClick(h.fieldActive() && writesSince(h, m) == 0 && open.value == before.value
                 && open.legacyDial,
                 k.name + ": the double-click that opens the value field writes nothing; it keeps"
                 " playing " + hz(before.value) + " Hz (plays " + hz(open.value) + ")");
    if (writesSince(h, m) != 0) h.printEvents(m);
    const size_t t = h.mark();
    h.type(text);
    h.key(ImGuiKey_Enter);
    h.idle(2);
    float value = 0.0f;
    const Played after = played(h, k.start, k.watched);
    const bool ok = h.lastWrite(t, k.watched, value) && value == committed
        && h.count(t, HostEvent::Begin, k.watched) == 1 && h.protocolOk(t) && after.value == committed
        && !after.legacyDial;
    dusktest::guards(ok, k.name + ": typing " + text + " and Enter write " + hz(committed)
                     + " in one gesture, and it plays that from its Hz parameter");
    if (!ok) h.printEvents(t);
}

// The editor's own knobs, each edited only by movement.
void ownKnobsEditOnlyOnMovement()
{
    constexpr HostEvent::Kind B = HostEvent::Begin, W = HostEvent::Write, E = HostEvent::End;
    const ImU32 consolePointer = IM_COL32(245, 245, 245, 255);
    const ImU32 filterPointer = IM_COL32(30, 30, 33, 255);

    // Between detents, where a value written back through the knob can come
    // back a rounding step off.
    const OwnKnob hpf77 = filterKnob("HPF knob at 77 Hz", false,
                                     { { kHpfEnabled, 1.0f }, { kHpfHz, 77.0f } });
    const OwnKnob lf123 = consoleKnob("LF FREQ knob at 123 Hz", kLf, kMid, kLfHz,
                                      { { kLfHz, 123.0f } }, 30, 450);
    for (const OwnKnob& k : {
             hpf77,
             filterKnob("LPF knob at 6543 Hz", true, { { kLpfEnabled, 1.0f }, { kLpfHz, 6543.0f } }),
             consoleKnob("LF GAIN knob at +2.7 dB", kLf, kTop, kLfGain, { { kLfGain, 2.7f } }, -15, 15),
             lf123,
             consoleKnob("LMF GAIN knob at +2.7 dB", kLmf, kTop, kLmGain, { { kLmGain, 2.7f } },
                         -15, 15),
             consoleKnob("LMF FREQ knob at 1234 Hz", kLmf, kMid, kLmHz, { { kLmHz, 1234.0f } },
                         200, 2500),
             consoleKnob("LMF Q knob at 1.234", kLmf, kLow, kLmQ, { { kLmQ, 1.234f } }, 0.5f, 3),
             consoleKnob("HMF GAIN knob at +2.7 dB", kHmf, kTop, kHmGain, { { kHmGain, 2.7f } },
                         -15, 15),
             consoleKnob("HMF FREQ knob at 4321 Hz", kHmf, kMid, kHmHz, { { kHmHz, 4321.0f } },
                         600, 7000),
             consoleKnob("HMF Q knob at 2.6", kHmf, kLow, kHmQ, { { kHmQ, 2.6f } }, 0.5f, 3),
             consoleKnob("HF GAIN knob at +2.7 dB", kHf, kTop, kHfGain, { { kHfGain, 2.7f } }, -15, 15),
             consoleKnob("HF FREQ knob at 9876 Hz", kHf, kMid, kHfHz, { { kHfHz, 9876.0f } },
                         1500, 16000) })
        motionlessPress(k);
    const OwnKnob hpfOut = filterKnob("HPF knob at OUT", false, { { kHpfEnabled, 0.0f } });
    motionlessPress(hpfOut, -20.0f);

    movedIntoThePressFrame(lf123);
    movedIntoThePressFrame(hpf77);

    // Slow drags write on every frame, also under Shift and when the end of
    // the travel cuts the last step short (0.0002 of the travel below the end).
    // The values a moved position interpolates are computed; the end of the
    // travel writes the last detent's value itself.
    const auto lfWrites = [](std::vector<float> hzs) {
        std::vector<Sent> sent;
        for (float v : hzs)
            sent.push_back(computedWrite(kLfHz, v));
        sent.push_back({ E, kLfHz, 0 });
        return sent;
    };
    slowDrag(lf123, true, 6,
             lfWrites({ 123.400017f, 123.800018f, 124.200027f, 124.600037f, 125.000038f,
                        125.400047f }));
    slowDrag(lf123, false, 6, lfWrites({ 125.500008f, 128.0f, 130.5f, 133.0f, 135.5f, 138.0f }));
    const auto hpfWrites = [](std::vector<float> hzs, bool computed) {
        std::vector<Sent> sent;
        for (float v : hzs)
        {
            sent.push_back({ W, kHpfEnabled, 1.0f });
            sent.push_back(computed ? computedWrite(kHpfHz, v) : Sent { W, kHpfHz, v });
        }
        sent.push_back({ E, kHpfEnabled, 0 });
        sent.push_back({ E, kHpfHz, 0 });
        return sent;
    };
    slowDrag(hpf77, true, 6,
             hpfWrites({ 77.2400055f, 77.480011f, 77.7200165f, 77.960022f, 78.2000275f, 78.440033f },
                       true));
    slowDrag(hpf77, false, 6, hpfWrites({ 78.5f, 80.0f, 81.5f, 83.0f, 84.5f, 86.0f }, true));
    slowDrag(consoleKnob("LF FREQ knob at 449.9 Hz", kLf, kMid, kLfHz, { { kLfHz, 449.9f } }, 30, 450),
             true, 1, { { W, kLfHz, 450.0f }, { E, kLfHz, 0 } });
    slowDrag(filterKnob("HPF knob at 349.9 Hz", false, { { kHpfEnabled, 1.0f }, { kHpfHz, 349.9f } }),
             true, 1, hpfWrites({ 350.0f }, false));

    // Held still while the host moves the parameter, the pointer follows it.
    pointerWhileHeld(lf123, consolePointer, 338.0f);
    pointerWhileHeld(hpf77, filterPointer, 250.0f);

    // Bands and filters a legacy dial plays outside the knob's range. With the
    // default switches (Brown, LF and HF shelves) LF 450 plays 511.7 Hz, HF 1500
    // 693.8 Hz, HPF 16 9.44 Hz and LPF 15201 29.9 kHz; in Black LMF 200 plays
    // 138.9 Hz, HMF 7000 8792 Hz and HPF 350 434 Hz. The knob sits at the end
    // the value lies beyond, except that the HPF below 16 Hz and the LPF above
    // 15.2 kHz point at their first detent, and the stretch from there down to
    // OUT plays that detent.
    const OwnKnob lfLegacy = consoleKnob("LF FREQ knob, legacy dial 450 (plays 511.7 Hz)",
                                         kLf, kMid, kLfHz, { { kLfFreq, 450.0f } }, 30, 450);
    const OwnKnob hfLegacy = consoleKnob("HF FREQ knob, legacy dial 1500 (plays 693.8 Hz)",
                                         kHf, kMid, kHfHz, { { kHfFreq, 1500.0f } }, 1500, 16000);
    const OwnKnob lmfLegacy = consoleKnob("LMF FREQ knob, Black, legacy dial 200 (plays 138.9 Hz)",
                                          kLmf, kMid, kLmHz, { { kEqType, 1.0f }, { kLmFreq, 200.0f } },
                                          200, 2500);
    const OwnKnob hmfLegacy = consoleKnob("HMF FREQ knob, Black, legacy dial 7000 (plays 8792 Hz)",
                                          kHmf, kMid, kHmHz,
                                          { { kEqType, 1.0f }, { kHmFreq, 7000.0f } }, 600, 7000);
    const OwnKnob hpfLegacy = filterKnob("HPF knob IN, legacy dial 16 (plays 9.44 Hz)", false,
                                         { { kHpfEnabled, 1.0f }, { kHpfFreq, 16.0f } });
    const OwnKnob lpfLegacy = filterKnob("LPF knob IN, legacy dial 15201 (plays 29.9 kHz)", true,
                                         { { kLpfEnabled, 1.0f }, { kLpfFreq, 15201.0f } });
    const OwnKnob hpfBlackLegacy = filterKnob(
        "HPF knob IN, Black, legacy dial 350 (plays 434 Hz)", false,
        { { kEqType, 1.0f }, { kHpfEnabled, 1.0f }, { kHpfFreq, 350.0f } });
    motionlessPress(lfLegacy, 10.0f);
    motionlessPress(hfLegacy);
    motionlessPress(lmfLegacy);
    motionlessPress(hmfLegacy, 10.0f);
    motionlessPress(hpfLegacy);
    motionlessPress(lpfLegacy);
    motionlessPress(hpfBlackLegacy, 10.0f);
    pinnedAndPushedOut(lfLegacy, -1.0f);
    pinnedAndPushedOut(hfLegacy, 1.0f);
    pinnedAndPushedOut(lmfLegacy, 1.0f);
    pinnedAndPushedOut(hmfLegacy, -1.0f);
    pinnedAndPushedOut(hpfLegacy, 1.0f, 6.0f);   // 6 px stays above OUT
    pinnedAndPushedOut(lpfLegacy, 1.0f, 6.0f);
    pinnedAndPushedOut(hpfBlackLegacy, -1.0f);
    flatStretchToOut(hpfLegacy, kHpfEnabled);
    flatStretchToOut(lpfLegacy, kLpfEnabled);
    pointerInTheStretchAboveOut(hpfLegacy, filterPointer);

    // Values at an end of the knob's travel, inside its range (and an HPF at OUT).
    pinnedAndPushedOut(consoleKnob("LF GAIN knob at +15 dB", kLf, kTop, kLfGain, { { kLfGain, 15.0f } },
                                   -15, 15), -1.0f);
    pinnedAndPushedOut(consoleKnob("HF GAIN knob at -15 dB", kHf, kTop, kHfGain,
                                   { { kHfGain, -15.0f } }, -15, 15), 1.0f);
    pinnedAndPushedOut(consoleKnob("LMF Q knob at 0.5", kLmf, kLow, kLmQ, { { kLmQ, 0.5f } }, 0.5f, 3),
                       -1.0f);
    pinnedAndPushedOut(consoleKnob("HMF Q knob at 3", kHmf, kLow, kHmQ, { { kHmQ, 3.0f } }, 0.5f, 3),
                       1.0f);
    pinnedAndPushedOut(consoleKnob("LF FREQ knob at 30 Hz", kLf, kMid, kLfHz, { { kLfHz, 30.0f } },
                                   30, 450), 1.0f);
    pinnedAndPushedOut(consoleKnob("HF FREQ knob at 16000 Hz", kHf, kMid, kHfHz,
                                   { { kHfHz, 16000.0f } }, 1500, 16000), -1.0f);
    pinnedAndPushedOut(filterKnob("LPF knob IN at 3000 Hz", true,
                                  { { kLpfEnabled, 1.0f }, { kLpfHz, 3000.0f } }), -1.0f);
    // OUT ends 0.06 of the travel up, and each wheel event moves the knob from
    // where its parameter puts it: an event of one notch up (0.02) stays in OUT,
    // and so does every further one-notch event. One event of four notches
    // (0.08) leaves OUT.
    pinnedAndPushedOut(hpfOut, 1.0f, 20.0f, 4.0f);

    // Typing a value over a legacy band or filter, and gestures that send what
    // they sent before (the values are the ones the editor sent before its
    // knobs edited only on movement). A wheel step's value is computed; a
    // reset writes the default itself.
    typeOverLegacy(hfLegacy, "2000", 2000.0f);
    typeOverLegacy(lpfLegacy, "8000", 8000.0f);
    const OwnKnob lf338 = consoleKnob("LF FREQ knob at 338 Hz", kLf, kMid, kLfHz,
                                      { { kLfHz, 338.0f } }, 30, 450);
    const OwnKnob hpf120 = filterKnob("HPF knob at 120 Hz", false,
                                      { { kHpfEnabled, 1.0f }, { kHpfHz, 120.0f } });
    const OwnKnob lpf5000 = filterKnob("LPF knob at 5000 Hz", true,
                                       { { kLpfEnabled, 1.0f }, { kLpfHz, 5000.0f } });
    const OwnKnob lfGain27 = consoleKnob("LF GAIN knob at +2.7 dB", kLf, kTop, kLfGain,
                                         { { kLfGain, 2.7f } }, -15, 15);
    const OwnKnob lmQ1234 = consoleKnob("LMF Q knob at 1.234", kLmf, kLow, kLmQ,
                                        { { kLmQ, 1.234f } }, 0.5f, 3);
    sendsAsBefore(lf338, Unchanged::Wheel, 1.0f,
                  { { B, kLfHz, 0 }, computedWrite(kLfHz, 346.933319f), { E, kLfHz, 0 } },
                  "a wheel step up");
    sendsAsBefore(lf338, Unchanged::Wheel, -1.0f,
                  { { B, kLfHz, 0 }, computedWrite(kLfHz, 326.960022f), { E, kLfHz, 0 } },
                  "a wheel step down");
    sendsAsBefore(hpf120, Unchanged::Wheel, 1.0f,
                  { { B, kHpfEnabled, 0 }, { B, kHpfHz, 0 }, { W, kHpfEnabled, 1.0f },
                    computedWrite(kHpfHz, 130.399994f), { E, kHpfHz, 0 }, { E, kHpfEnabled, 0 } },
                  "a wheel step up");
    sendsAsBefore(lpf5000, Unchanged::Wheel, 1.0f,
                  { { B, kLpfEnabled, 0 }, { B, kLpfHz, 0 }, { W, kLpfEnabled, 1.0f },
                    computedWrite(kLpfHz, 4900.0f), { E, kLpfHz, 0 }, { E, kLpfEnabled, 0 } },
                  "a wheel step up");
    sendsAsBefore(lfGain27, Unchanged::Wheel, 1.0f,
                  { { B, kLfGain, 0 }, computedWrite(kLfGain, 3.29999971f), { E, kLfGain, 0 } },
                  "a wheel step up");
    sendsAsBefore(lmQ1234, Unchanged::Wheel, 1.0f,
                  { { B, kLmQ, 0 }, computedWrite(kLmQ, 1.18439996f), { E, kLmQ, 0 } },
                  "a wheel step up");
    sendsAsBefore(lfLegacy, Unchanged::Wheel, -1.0f,
                  { { B, kLfHz, 0 }, computedWrite(kLfHz, 441.0f), { B, kLegacyDialBands, 0 },
                    { W, kLegacyDialBands, 16.0f }, { E, kLegacyDialBands, 0 }, { E, kLfHz, 0 } },
                  "a wheel step down, into its range,");
    sendsAsBefore(lfLegacy, Unchanged::Reset, 0.0f,
                  { { B, kLfHz, 0 }, { W, kLfHz, 200.0f }, { B, kLegacyDialBands, 0 },
                    { W, kLegacyDialBands, 16.0f }, { E, kLegacyDialBands, 0 }, { E, kLfHz, 0 } },
                  "a Cmd-click reset");
    sendsAsBefore(hfLegacy, Unchanged::Reset, 0.0f,
                  { { B, kHfHz, 0 }, { W, kHfHz, 8000.0f }, { B, kLegacyDialBands, 0 },
                    { W, kLegacyDialBands, 16.0f }, { E, kLegacyDialBands, 0 }, { E, kHfHz, 0 } },
                  "a Cmd-click reset");
    sendsAsBefore(hpfLegacy, Unchanged::Reset, 0.0f,
                  { { B, kHpfEnabled, 0 }, { W, kHpfEnabled, 0.0f }, { E, kHpfEnabled, 0 } },
                  "a Cmd-click reset to OUT");
    sendsAsBefore(lpfLegacy, Unchanged::Reset, 0.0f,
                  { { B, kLpfEnabled, 0 }, { W, kLpfEnabled, 0.0f }, { E, kLpfEnabled, 0 } },
                  "a Cmd-click reset to OUT");
    sendsAsBefore(lmQ1234, Unchanged::Reset, 0.0f,
                  { { B, kLmQ, 0 }, { W, kLmQ, 1.5f }, { E, kLmQ, 0 } }, "a Cmd-click reset");
    // The INPUT and OUTPUT knobs are DuskPanel::knob's own and already kept the rule.
    sendsAsBefore(consoleKnob("INPUT knob at +3.3 dB", kMaster, kTop, kInputGain,
                              { { kInputGain, 3.3f } }, -12, 12),
                  Unchanged::Press, 0.0f, { { B, kInputGain, 0 }, { E, kInputGain, 0 } },
                  "a press without movement");
    sendsAsBefore(consoleKnob("OUTPUT knob at -12 dB", kMaster, kMid, kOutputGain,
                              { { kOutputGain, -12.0f } }, -12, 12),
                  Unchanged::Press, 0.0f, { { B, kOutputGain, 0 }, { E, kOutputGain, 0 } },
                  "a press without movement");
}
} // namespace

int main()
{
    dusktest::isolateHome(FOURKEQ_UI_TEST_HOME);
    using dusktest::ValueFieldKnob;

    // Layout (FourKEQUI::drawColumns, graph shown): cY(y) = y - 40, column
    // centres between COL[] = { 40, 186, 331, ... }, knob radius 28.
    ValueFieldKnob hpf;
    hpf.name = "HPF knob";
    hpf.x = 113.0f; hpf.y = 274.0f; hpf.radius = 28.0f; hpf.aboveField = -18.0f;
    hpf.outside = ImVec2(113.0f, 343.0f);   // between the HPF and LPF knobs
    hpf.parameters = { kHpfEnabled, kHpfHz };
    hpf.valueParameter = kHpfHz;
    hpf.start = { { kHpfEnabled, 1.0f }, { kHpfHz, 120.0f } };   // the 120 detent, mid-travel
    hpf.typed = "250"; hpf.committed = 250.0f;
    hpf.resetModifier = ImGuiMod_Super;   // Cmd (macOS) / Ctrl+click: back to OUT
    hpf.resetParameter = kHpfEnabled; hpf.resetValue = 0.0f;

    ValueFieldKnob lfGain;
    lfGain.name = "LF GAIN knob";
    lfGain.x = 258.5f; lfGain.y = 274.0f; lfGain.radius = 28.0f; lfGain.aboveField = -18.0f;
    lfGain.outside = ImVec2(258.5f, 343.0f);   // between LF GAIN and LF FREQ
    lfGain.parameters = { kLfGain };
    lfGain.valueParameter = kLfGain;
    lfGain.start = { { kLfGain, 3.0f } };
    lfGain.typed = "6"; lfGain.committed = 6.0f;
    lfGain.resetModifier = ImGuiMod_Super;
    lfGain.resetParameter = kLfGain; lfGain.resetValue = kFourKParams[kLfGain].def;

    // LF FREQ: a console knob on a frequency parameter, which types and reads Hz.
    ValueFieldKnob lfFreq = lfGain;
    lfFreq.name = "LF FREQ knob";
    lfFreq.y = 412.0f;
    lfFreq.outside = ImVec2(258.5f, 343.0f);
    lfFreq.parameters = { kLfHz };
    lfFreq.valueParameter = kLfHz;
    lfFreq.start = { { kLfHz, 338.0f } };   // a detent away from the 200 Hz default
    lfFreq.typed = "100"; lfFreq.committed = 100.0f;
    lfFreq.resetParameter = kLfHz; lfFreq.resetValue = kFourKParams[kLfHz].def;

    dusktest::ValueFieldSuite<DAF::FourKEQUI> suite(960, 640);
    suite.run(hpf, lfGain);
    suite.run(lfGain, hpf);
    suite.run(lfFreq, hpf);

    ownKnobsEditOnlyOnMovement();

    return summary();
}
