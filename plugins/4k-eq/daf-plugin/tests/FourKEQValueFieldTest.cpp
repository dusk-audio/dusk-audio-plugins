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
// filter at the mark next to OUT plays that mark down to OUT, so a drag
// through that stretch writes nothing until the knob reaches OUT, which
// switches the filter out and keeps its frequency; and a knob held still
// while the host moves its parameter shows the new value with its pointer.
// Those checks are tagged [click]: each fails on the editor before its knobs
// took the rule, where every frame of a held press wrote the knob's value (and
// held the pointer where the press began). The [guard] checks pass on both:
// the controls (the same press point drags, the opposite direction edits, a
// moved drag keeps the pointer), and the events a gesture sends, event for
// event (see sentExactly for how values are compared): reset, wheel steps,
// drags of 1 px per frame with and without Shift, the INPUT and OUTPUT knobs'
// press, and the value a typed entry commits. A knob moved to a new position
// writes the value its travel gives there (the frequency knobs' travel: see
// below).
//
// The band and filter frequency knobs are positions on the console's printed
// dial, as their parameters are. The [dial] checks: their read-out is the dial
// value in the reference's own format, at every printed mark, rounded before
// its unit is picked ("1.00 k" at 999.7); the value field opens on that text,
// and a value typed in it ("1.20 k", "30.0 Hz", "Out" for a filter) commits as
// itself; neither the gain nor the Brown/Black mode moves the value bubble or
// the pointer, which sits on the printed mark of its value. Each knob travels
// through the reference's 21 dial positions, k/20 of the travel (a filter's
// from 0.1, its first mark), its printed marks among them: a knob set to the
// dial value of any position points at it, and a wheel event onto a position
// writes its dial value. The read-out and typing checks fail on the editor
// before this change, whose frequency knobs read and typed Hz, and on 1.0.5's,
// whose read-out moved with gain and mode; the travel checks on an editor that
// runs linearly between the printed marks or has a wrong value anywhere in its
// travel; the rounding checks on one that truncates or picks the unit before
// it rounds.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "FourKEQUI.cpp"
#include "DuskValueFieldTest.hpp"

#include <cstring>
#include <string>

// The DSP bridge (FourKEQAccess.hpp) as an out-of-process UI sees it. Defined
// because a weak reference must still resolve when an Apple binary links.
float fourKEQGetInputPeakL(void*) noexcept { return 0; }
float fourKEQGetInputPeakR(void*) noexcept { return 0; }
float fourKEQGetOutputPeakL(void*) noexcept { return 0; }
float fourKEQGetOutputPeakR(void*) noexcept { return 0; }
const duskaudio::SpectrumRing* fourKEQGetPreSpectrum(void*) noexcept { return nullptr; }
const duskaudio::SpectrumRing* fourKEQGetPostSpectrum(void*) noexcept { return nullptr; }

namespace
{
using Host = dusktest::EditorHost<DAF::FourKEQUI>;
using dusktest::HostEvent;
using dusktest::Values;

int clickChecks = 0, clickFailures = 0, dialChecks = 0, dialFailures = 0;

// A check that fails on the editor before its knobs edited only on movement
// (see the top of this file).
void detectsClick(bool ok, const std::string& what)
{
    std::printf("%s [click] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    dusktest::failures += ok ? 0 : 1;
    clickFailures += ok ? 0 : 1;
    ++clickChecks;
}

// A check that fails on an editor whose frequency knobs do not read and type
// their dial value in the reference's format (see the top of this file).
void detectsDial(bool ok, const std::string& what)
{
    std::printf("%s [dial] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    dusktest::failures += ok ? 0 : 1;
    dialFailures += ok ? 0 : 1;
    ++dialChecks;
}

// dusktest::summary() with the [click] and [dial] checks counted too.
int summary()
{
    std::printf("%s: %d failure(s) in %d [#337], %d [guard], %d [click] and %d [dial] checks; %d [click] and %d"
                " [dial] failures\n", dusktest::failures ? "FAILED" : "PASSED", dusktest::failures,
                dusktest::issueChecks, dusktest::guardChecks, clickChecks, dialChecks, clickFailures, dialFailures);
    return dusktest::failures ? 1 : 0;
}

// Window pixels at the design size (FourKEQUI::drawColumns, graph shown): the
// column centres between COL[], rows cY(314) = 274, cY(452) = 412 and
// cY(590) = 550. Every knob below has radius 28.
constexpr float kFilters = 113.0f, kLf = 258.5f, kLmf = 404.0f, kHmf = 549.5f, kHf = 695.0f;
constexpr float kMaster = 844.0f, kTop = 274.0f, kMid = 412.0f, kLow = 550.0f;
// A frequency knob's value field parses its own text, so DuskPanel::valueEdit
// draws it 96 px wide (58 px for the gain and Q knobs).
constexpr float kFrequencyFieldHalfWidth = 48.0f;

// One of the editor's own knobs at a start state.
struct OwnKnob
{
    std::string name;
    float x = 0, y = 0;
    std::vector<uint32_t> gesture;   // the parameters a press opens a gesture on
    uint32_t watched = 0;            // what the checks read: a frequency dial, or the gain or Q
    Values start;
};

OwnKnob consoleKnob(std::string name, float x, float y, uint32_t parameter, Values start)
{
    OwnKnob k;
    k.name = std::move(name);
    k.x = x;
    k.y = y;
    k.gesture = { parameter };
    k.watched = parameter;
    k.start = std::move(start);
    return k;
}

OwnKnob filterKnob(std::string name, bool lowPass, Values start)
{
    OwnKnob k;
    k.name = std::move(name);
    k.x = kFilters;
    k.y = lowPass ? kMid : kTop;
    k.gesture = { lowPass ? kLpfEnabled : kHpfEnabled, lowPass ? kLpfFreq : kHpfFreq };
    k.watched = lowPass ? kLpfFreq : kHpfFreq;
    k.start = std::move(start);
    return k;
}

// What the plugin plays for `watched` once the host has forwarded `start` and
// every write the editor sent: the last value written, else the start value,
// else the default.
float played(const Host& h, const Values& start, uint32_t watched)
{
    float v = kFourKParams[watched].def;
    for (const auto& [parameter, value] : start)
        if (parameter == watched)
            v = value;
    for (const HostEvent& e : h.ui.hostEvents)
        if (e.kind == HostEvent::Write && e.parameter == watched)
            v = e.value;
    return v;
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
// a switch value), or, where float arithmetic computes it (a knob position
// moved by a drag or a wheel step, then interpolated between detents), within
// kComputed of it. Compilers fuse those multiply-adds or not (clang on arm64
// does by default, GCC for baseline x86-64 cannot), which moves the last bits:
// 123.400009 Hz against 123.400017. kComputed, 1e-4 of the value, lies 30
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
bool sentExactly(const Host& h, size_t from, const std::vector<Sent>& expected)
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
// For a check's message, the values `expected` writes to `parameter`
// ("w1, w2, ..."), else its other writes by key ("hpf_enabled 0"). The check
// itself compares every event.
std::string writesIn(const std::vector<Sent>& expected, uint32_t parameter)
{
    std::string own, other;
    for (const Sent& x : expected)
        if (x.kind == HostEvent::Write)
        {
            char v[48];
            if (x.parameter == parameter)
            {
                std::snprintf(v, sizeof(v), "%.6g", (double) x.value);
                own += (own.empty() ? "" : ", ") + std::string(v);
            }
            else
            {
                std::snprintf(v, sizeof(v), "%s %.6g", kFourKParams[x.parameter].key, (double) x.value);
                other += (other.empty() ? "" : ", ") + std::string(v);
            }
        }
    return !own.empty() ? own : !other.empty() ? other : "nothing";
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
// positions), with or without Shift: exactly `expected` (see sentExactly), one
// write per moved frame. Under Shift 1 px is 0.0008 of the travel, so a gate
// that let small moves through only above some size would drop them.
void slowDrag(const OwnKnob& k, bool shift, int frames, const std::vector<Sent>& expected)
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
    const bool ok = sentExactly(h, m, expected);
    dusktest::guards(ok, k.name + ": a " + (shift ? "Shift " : "") + "drag up of 1 px per frame for "
                     + std::to_string(frames) + " frame(s) writes " + writesIn(expected, k.watched));
    if (!ok) printSent(h, m);
}

// The knob sits at an end of its travel (`outward` -1: the top, so up pushes
// further out; +1: the bottom). A drag of `push` px and a wheel step further
// out write nothing. A 20 px drag and one wheel event of `inwardNotches`
// notches the other way edit.
void pinnedAndPushedOut(const OwnKnob& k, float outward, float push = 20.0f, float inwardNotches = 1.0f)
{
    {
        Host h(960, 640, k.start);
        const size_t m = h.mark();
        h.drag(k.x, k.y, 0.0f, outward * push);
        h.idle(2);
        detectsClick(writesSince(h, m) == 0, k.name + ": a " + hz(push) + " px drag "
                     + (outward < 0 ? "up" : "down")
                     + ", further out from the end it sits at, writes nothing");
        if (writesSince(h, m) != 0) h.printEvents(m);
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
        const bool ok = oneGestureEach(h, m, k) && writesSince(h, m) > 0;
        dusktest::guards(ok, k.name + ": control, a 20 px drag the other way writes in one gesture per"
                         " parameter");
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

// A filter at its first detent (an HPF at 16 Hz, an LPF at 15201 Hz) points at
// it, and the stretch from there down to OUT plays the detent. Dragged through
// it to OUT, the filter is switched OUT and nothing else is written: its
// frequency stays as it was.
void flatStretchToOut(const OwnKnob& k, uint32_t enabled)
{
    Host h(960, 640, k.start);
    const float before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.drag(k.x, k.y, 0.0f, 30.0f);
    h.idle(2);
    float en = -1.0f;
    h.lastWrite(m, enabled, en);
    const bool ok = en == 0.0f && h.count(m, HostEvent::Write, k.watched) == 0
        && played(h, k.start, k.watched) == before;
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

// A filter at its first detent points at it (0.1 of the travel), and a drag
// down into the stretch above OUT writes nothing (stepSettingMoves). The knob
// still turns with the drag: its pointer follows a gesture once the gesture
// has moved, whether or not it wrote, and goes back to the parameter when the
// knob is let go.
void pointerInTheStretchAboveOut(const OwnKnob& k, ImU32 colour)
{
    const float atDetent = duskdaf::DuskPanel::knobAngle(0.1f);
    const float dragged = duskdaf::DuskPanel::knobAngle(0.07f);   // 6 px down at 0.005 per px
    Host h(960, 640, k.start);
    h.move(k.x, k.y);
    h.idle(2);
    const float rest = pointerAngle(h, k, colour);
    const float before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.press();
    h.move(k.x, k.y + 3.0f);
    h.move(k.x, k.y + 6.0f);
    h.idle(2);
    const float held = pointerAngle(h, k, colour);
    detectsClick(std::fabs(rest - atDetent) < 0.005f && std::fabs(held - dragged) < 0.005f
                 && writesSince(h, m) == 0 && played(h, k.start, k.watched) == before,
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

// Gestures that the rule leaves alone send exactly `expected`, value for value.
enum class Unchanged { Wheel, Reset, Press };
void sendsExactly(const OwnKnob& k, Unchanged gesture, float notches, const std::vector<Sent>& expected,
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
    const bool ok = sentExactly(h, m, expected);
    dusktest::guards(ok, k.name + ": " + what + " writes " + writesIn(expected, k.watched) + " in one gesture");
    if (!ok) printSent(h, m);
}

// Double-click to type on a knob at an end of its travel: the field opens and
// nothing moves the knob until a value is committed; the committed value is
// written as before, in one gesture.
void typeAtAnEnd(const OwnKnob& k, const char* text, float committed)
{
    Host h(960, 640, k.start);
    const float before = played(h, k.start, k.watched);
    const size_t m = h.mark();
    h.doubleClick(k.x, k.y);
    h.idle();
    const float open = played(h, k.start, k.watched);
    detectsClick(h.fieldActive() && writesSince(h, m) == 0 && open == before,
                 k.name + ": the double-click that opens the value field writes nothing; it keeps"
                 " playing " + hz(before) + " (plays " + hz(open) + ")");
    if (writesSince(h, m) != 0) h.printEvents(m);
    const size_t t = h.mark();
    h.type(text);
    h.key(ImGuiKey_Enter);
    h.idle(2);
    float value = 0.0f;
    const bool ok = h.lastWrite(t, k.watched, value) && value == committed
        && h.count(t, HostEvent::Begin, k.watched) == 1 && h.protocolOk(t)
        && played(h, k.start, k.watched) == committed;
    dusktest::guards(ok, k.name + ": typing " + text + " and Enter write " + hz(committed)
                     + " in one gesture");
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
                                     { { kHpfEnabled, 1.0f }, { kHpfFreq, 77.0f } });
    const OwnKnob lf123 = consoleKnob("LF FREQ knob at 123 Hz", kLf, kMid, kLfFreq,
                                      { { kLfFreq, 123.0f } });
    for (const OwnKnob& k : {
             hpf77,
             filterKnob("LPF knob at 6543 Hz", true, { { kLpfEnabled, 1.0f }, { kLpfFreq, 6543.0f } }),
             consoleKnob("LF GAIN knob at +2.7 dB", kLf, kTop, kLfGain, { { kLfGain, 2.7f } }),
             lf123,
             consoleKnob("LMF GAIN knob at +2.7 dB", kLmf, kTop, kLmGain, { { kLmGain, 2.7f } }),
             consoleKnob("LMF FREQ knob at 1234 Hz", kLmf, kMid, kLmFreq, { { kLmFreq, 1234.0f } }),
             consoleKnob("LMF Q knob at 1.234", kLmf, kLow, kLmQ, { { kLmQ, 1.234f } }),
             consoleKnob("HMF GAIN knob at +2.7 dB", kHmf, kTop, kHmGain, { { kHmGain, 2.7f } }),
             consoleKnob("HMF FREQ knob at 4321 Hz", kHmf, kMid, kHmFreq, { { kHmFreq, 4321.0f } }),
             consoleKnob("HMF Q knob at 2.6", kHmf, kLow, kHmQ, { { kHmQ, 2.6f } }),
             consoleKnob("HF GAIN knob at +2.7 dB", kHf, kTop, kHfGain, { { kHfGain, 2.7f } }),
             consoleKnob("HF FREQ knob at 9876 Hz", kHf, kMid, kHfFreq, { { kHfFreq, 9876.0f } }) })
        motionlessPress(k);
    const OwnKnob hpfOut = filterKnob("HPF knob at OUT", false, { { kHpfEnabled, 0.0f } });
    motionlessPress(hpfOut, -20.0f);

    movedIntoThePressFrame(lf123);
    movedIntoThePressFrame(hpf77);

    // Slow drags write on every frame, also under Shift and when the end of
    // the travel cuts the last step short (0.0002 of the travel below the end).
    // The values a moved position interpolates are computed, by the knob's
    // travel: LF 123 Hz sits at 0.37167 between the reference's positions 0.35
    // (110 Hz) and 0.4 (140 Hz), 600 Hz per unit of travel, so a Shift pixel
    // (0.0008) adds 0.48 Hz and a plain one (0.005) 3 Hz, the sixth crossing 0.4
    // into the next stretch (also 600 Hz per unit). The HPF's 77 Hz lies between
    // 0.35 (75 Hz) and 0.4 (90 Hz), 300 Hz per unit. The end of the travel
    // writes the last value itself.
    const auto lfWrites = [](std::vector<float> values) {
        std::vector<Sent> sent;
        for (float v : values)
            sent.push_back(computedWrite(kLfFreq, v));
        sent.push_back({ E, kLfFreq, 0 });
        return sent;
    };
    slowDrag(lf123, true, 6, lfWrites({ 123.48f, 123.96f, 124.44f, 124.92f, 125.4f, 125.88f }));
    slowDrag(lf123, false, 6, lfWrites({ 126.0f, 129.0f, 132.0f, 135.0f, 138.0f, 141.0f }));
    const auto hpfWrites = [](std::vector<float> values, bool computed) {
        std::vector<Sent> sent;
        for (float v : values)
        {
            sent.push_back({ W, kHpfEnabled, 1.0f });
            sent.push_back(computed ? computedWrite(kHpfFreq, v) : Sent { W, kHpfFreq, v });
        }
        sent.push_back({ E, kHpfEnabled, 0 });
        sent.push_back({ E, kHpfFreq, 0 });
        return sent;
    };
    slowDrag(hpf77, true, 6, hpfWrites({ 77.24f, 77.48f, 77.72f, 77.96f, 78.2f, 78.44f }, true));
    slowDrag(hpf77, false, 6, hpfWrites({ 78.5f, 80.0f, 81.5f, 83.0f, 84.5f, 86.0f }, true));
    slowDrag(consoleKnob("LF FREQ knob at 449.9 Hz", kLf, kMid, kLfFreq, { { kLfFreq, 449.9f } }),
             true, 1, { { W, kLfFreq, 450.0f }, { E, kLfFreq, 0 } });
    slowDrag(filterKnob("HPF knob at 349.9 Hz", false, { { kHpfEnabled, 1.0f }, { kHpfFreq, 349.9f } }),
             true, 1, hpfWrites({ 350.0f }, false));

    // Held still while the host moves the parameter, the pointer follows it.
    pointerWhileHeld(lf123, consolePointer, 338.0f);
    pointerWhileHeld(hpf77, filterPointer, 250.0f);

    // Frequency knobs at the ends of their travel, in both modes: the top of
    // LF, HMF (Black) and the HPF (Black), the bottom of LMF (Black) and HF,
    // and each filter at the mark next to OUT, where the stretch down to OUT
    // plays that mark.
    const OwnKnob lfTop = consoleKnob("LF FREQ knob at 450 Hz, its top", kLf, kMid, kLfFreq,
                                      { { kLfFreq, 450.0f } });
    const OwnKnob hfBottom = consoleKnob("HF FREQ knob at 1500 Hz, its bottom", kHf, kMid, kHfFreq,
                                         { { kHfFreq, 1500.0f } });
    const OwnKnob lmfBottom = consoleKnob("LMF FREQ knob, Black, at 200 Hz, its bottom", kLmf, kMid, kLmFreq,
                                          { { kEqType, 1.0f }, { kLmFreq, 200.0f } });
    const OwnKnob hmfTop = consoleKnob("HMF FREQ knob, Black, at 7000 Hz, its top", kHmf, kMid, kHmFreq,
                                       { { kEqType, 1.0f }, { kHmFreq, 7000.0f } });
    const OwnKnob hpfFirst = filterKnob("HPF knob IN at 16 Hz, the mark next to OUT", false,
                                        { { kHpfEnabled, 1.0f }, { kHpfFreq, 16.0f } });
    const OwnKnob lpfFirst = filterKnob("LPF knob IN at 15201 Hz, the mark next to OUT", true,
                                        { { kLpfEnabled, 1.0f }, { kLpfFreq, 15201.0f } });
    const OwnKnob hpfBlackTop = filterKnob("HPF knob IN, Black, at 350 Hz, its top", false,
                                           { { kEqType, 1.0f }, { kHpfEnabled, 1.0f }, { kHpfFreq, 350.0f } });
    motionlessPress(lfTop, 10.0f);
    motionlessPress(hfBottom);
    motionlessPress(lmfBottom);
    motionlessPress(hmfTop, 10.0f);
    motionlessPress(hpfFirst);
    motionlessPress(lpfFirst);
    motionlessPress(hpfBlackTop, 10.0f);
    pinnedAndPushedOut(lfTop, -1.0f);
    pinnedAndPushedOut(hfBottom, 1.0f);
    pinnedAndPushedOut(lmfBottom, 1.0f);
    pinnedAndPushedOut(hmfTop, -1.0f);
    pinnedAndPushedOut(hpfFirst, 1.0f, 6.0f);   // 6 px stays above OUT
    pinnedAndPushedOut(lpfFirst, 1.0f, 6.0f);
    pinnedAndPushedOut(hpfBlackTop, -1.0f);
    flatStretchToOut(hpfFirst, kHpfEnabled);
    flatStretchToOut(lpfFirst, kLpfEnabled);
    pointerInTheStretchAboveOut(hpfFirst, filterPointer);

    // Values at an end of the knob's travel (and an HPF at OUT).
    pinnedAndPushedOut(consoleKnob("LF GAIN knob at +15 dB", kLf, kTop, kLfGain, { { kLfGain, 15.0f } }),
                       -1.0f);
    pinnedAndPushedOut(consoleKnob("HF GAIN knob at -15 dB", kHf, kTop, kHfGain, { { kHfGain, -15.0f } }),
                       1.0f);
    pinnedAndPushedOut(consoleKnob("LMF Q knob at 0.5", kLmf, kLow, kLmQ, { { kLmQ, 0.5f } }), -1.0f);
    pinnedAndPushedOut(consoleKnob("HMF Q knob at 3", kHmf, kLow, kHmQ, { { kHmQ, 3.0f } }), 1.0f);
    pinnedAndPushedOut(consoleKnob("LF FREQ knob at 30 Hz", kLf, kMid, kLfFreq, { { kLfFreq, 30.0f } }), 1.0f);
    pinnedAndPushedOut(consoleKnob("HF FREQ knob at 16000 Hz", kHf, kMid, kHfFreq, { { kHfFreq, 16000.0f } }),
                       -1.0f);
    pinnedAndPushedOut(filterKnob("LPF knob IN at 3000 Hz", true,
                                  { { kLpfEnabled, 1.0f }, { kLpfFreq, 3000.0f } }), -1.0f);
    // OUT ends 0.06 of the travel up, and each wheel event moves the knob from
    // where its parameter puts it: an event of one notch up (0.02) stays in OUT,
    // and so does every further one-notch event. One event of four notches
    // (0.08) leaves OUT.
    pinnedAndPushedOut(hpfOut, 1.0f, 20.0f, 4.0f);

    // Typing a value on a knob at an end, and gestures whose events are pinned
    // exactly. A wheel event moves the knob 0.02 of its travel per notch from
    // where its value puts it, and writes what the travel gives there
    // (computed): LF 338 Hz is the 0.75 mark, with the reference's 360 Hz at
    // 0.8 and 315 Hz at 0.7; the HPF's 120 Hz and the LPF's 5000 Hz the 0.5
    // marks, with 144 Hz and 4700 Hz at 0.55; LF 450 Hz the top, with 427 Hz at
    // 0.95. The gain and Q knobs travel through their marks. A reset writes the
    // default itself.
    typeAtAnEnd(hfBottom, "2000", 2000.0f);
    typeAtAnEnd(lpfFirst, "8000", 8000.0f);
    const OwnKnob lf338 = consoleKnob("LF FREQ knob at 338 Hz", kLf, kMid, kLfFreq, { { kLfFreq, 338.0f } });
    const OwnKnob hpf120 = filterKnob("HPF knob at 120 Hz", false, { { kHpfEnabled, 1.0f }, { kHpfFreq, 120.0f } });
    const OwnKnob lpf5000 = filterKnob("LPF knob at 5000 Hz", true, { { kLpfEnabled, 1.0f }, { kLpfFreq, 5000.0f } });
    const OwnKnob lfGain27 = consoleKnob("LF GAIN knob at +2.7 dB", kLf, kTop, kLfGain, { { kLfGain, 2.7f } });
    const OwnKnob lmQ1234 = consoleKnob("LMF Q knob at 1.234", kLmf, kLow, kLmQ, { { kLmQ, 1.234f } });
    sendsExactly(lf338, Unchanged::Wheel, 1.0f,
                 { { B, kLfFreq, 0 }, computedWrite(kLfFreq, 346.8f), { E, kLfFreq, 0 } },
                 "a wheel step up");
    sendsExactly(lf338, Unchanged::Wheel, -1.0f,
                 { { B, kLfFreq, 0 }, computedWrite(kLfFreq, 328.8f), { E, kLfFreq, 0 } },
                 "a wheel step down");
    sendsExactly(hpf120, Unchanged::Wheel, 1.0f,
                 { { B, kHpfEnabled, 0 }, { B, kHpfFreq, 0 }, { W, kHpfEnabled, 1.0f },
                   computedWrite(kHpfFreq, 129.6f), { E, kHpfFreq, 0 }, { E, kHpfEnabled, 0 } },
                 "a wheel step up");
    sendsExactly(lpf5000, Unchanged::Wheel, 1.0f,
                 { { B, kLpfEnabled, 0 }, { B, kLpfFreq, 0 }, { W, kLpfEnabled, 1.0f },
                   computedWrite(kLpfFreq, 4880.0f), { E, kLpfFreq, 0 }, { E, kLpfEnabled, 0 } },
                 "a wheel step up");
    sendsExactly(lfGain27, Unchanged::Wheel, 1.0f,
                 { { B, kLfGain, 0 }, computedWrite(kLfGain, 3.29999971f), { E, kLfGain, 0 } },
                 "a wheel step up");
    sendsExactly(lmQ1234, Unchanged::Wheel, 1.0f,
                 { { B, kLmQ, 0 }, computedWrite(kLmQ, 1.18439996f), { E, kLmQ, 0 } },
                 "a wheel step up");
    sendsExactly(lfTop, Unchanged::Wheel, -1.0f,
                 { { B, kLfFreq, 0 }, computedWrite(kLfFreq, 440.8f), { E, kLfFreq, 0 } },
                 "a wheel step down, off its top,");
    sendsExactly(lfTop, Unchanged::Reset, 0.0f,
                 { { B, kLfFreq, 0 }, { W, kLfFreq, 200.0f }, { E, kLfFreq, 0 } }, "a Cmd-click reset");
    sendsExactly(hfBottom, Unchanged::Reset, 0.0f,
                 { { B, kHfFreq, 0 }, { W, kHfFreq, 8000.0f }, { E, kHfFreq, 0 } }, "a Cmd-click reset");
    sendsExactly(hpfFirst, Unchanged::Reset, 0.0f,
                 { { B, kHpfEnabled, 0 }, { W, kHpfEnabled, 0.0f }, { E, kHpfEnabled, 0 } },
                 "a Cmd-click reset to OUT");
    sendsExactly(lpfFirst, Unchanged::Reset, 0.0f,
                 { { B, kLpfEnabled, 0 }, { W, kLpfEnabled, 0.0f }, { E, kLpfEnabled, 0 } },
                 "a Cmd-click reset to OUT");
    sendsExactly(lmQ1234, Unchanged::Reset, 0.0f,
                 { { B, kLmQ, 0 }, { W, kLmQ, 1.5f }, { E, kLmQ, 0 } }, "a Cmd-click reset");
    // The INPUT and OUTPUT knobs are DuskPanel::knob's own and already kept the rule.
    sendsExactly(consoleKnob("INPUT knob at +3.3 dB", kMaster, kTop, kInputGain, { { kInputGain, 3.3f } }),
                 Unchanged::Press, 0.0f, { { B, kInputGain, 0 }, { E, kInputGain, 0 } },
                 "a press without movement");
    sendsExactly(consoleKnob("OUTPUT knob at -12 dB", kMaster, kMid, kOutputGain, { { kOutputGain, -12.0f } }),
                 Unchanged::Press, 0.0f, { { B, kOutputGain, 0 }, { E, kOutputGain, 0 } },
                 "a press without movement");
}

//==============================================================================
// The frequency knobs read and type their dial value as the reference does.
//==============================================================================

// The reference's read-out at the printed marks, Brown and Black alike (its
// hosted readback at positions 0, 0.1, 0.25, 0.5, 0.75, 0.9 and 1; the filters
// read "Out" at 0, where 4K EQ 2 switches the filter out).
struct Readout { float value; const char* text; };
struct ReadoutTable { uint32_t param; float x, y; const char* name; Readout marks[7]; int count; };
const ReadoutTable kReadouts[6] = {
    { kHpfFreq, kFilters, kTop, "HPF", { { 16.f, "16.0 Hz" }, { 45.f, "45.0 Hz" }, { 120.f, "120 Hz" },
      { 250.f, "250 Hz" }, { 320.f, "320 Hz" }, { 350.f, "350 Hz" } }, 6 },
    { kLpfFreq, kFilters, kMid, "LPF", { { 15201.f, "15201 Hz" }, { 10000.f, "10000 Hz" }, { 5000.f, "5000 Hz" },
      { 3750.f, "3750 Hz" }, { 3300.f, "3300 Hz" }, { 3000.f, "3000 Hz" } }, 6 },
    { kLfFreq, kLf, kMid, "LF FREQ", { { 30.f, "30.0 Hz" }, { 42.f, "42.0 Hz" }, { 75.f, "75.0 Hz" },
      { 200.f, "200 Hz" }, { 338.f, "338 Hz" }, { 405.f, "405 Hz" }, { 450.f, "450 Hz" } }, 7 },
    { kLmFreq, kLmf, kMid, "LMF FREQ", { { 200.f, "200 Hz" }, { 260.f, "260 Hz" }, { 550.f, "550 Hz" },
      { 1000.f, "1.00 k" }, { 1750.f, "1.75 k" }, { 2200.f, "2.20 k" }, { 2500.f, "2.50 k" } }, 7 },
    { kHmFreq, kHmf, kMid, "HMF FREQ", { { 600.f, "600 Hz" }, { 720.f, "720 Hz" }, { 1150.f, "1.15 k" },
      { 3000.f, "3.00 k" }, { 5250.f, "5.25 k" }, { 6400.f, "6.40 k" }, { 7000.f, "7.00 k" } }, 7 },
    { kHfFreq, kHf, kMid, "HF FREQ", { { 1500.f, "1.50 k" }, { 1800.f, "1.80 k" }, { 3500.f, "3.50 k" },
      { 8000.f, "8.00 k" }, { 12000.f, "12.0 k" }, { 14800.f, "14.8 k" }, { 16000.f, "16.0 k" } }, 7 },
};

uint32_t enableOf(uint32_t param)
{
    return param == kHpfFreq ? kHpfEnabled : kLpfEnabled;
}

Values startAt(const ReadoutTable& r, float value, bool black)
{
    Values v = { { kEqType, black ? 1.0f : 0.0f }, { r.param, value } };
    if (r.param == kHpfFreq || r.param == kLpfFreq)
        v.push_back({ enableOf(r.param), 1.0f });
    return v;
}

// The open value field's text.
std::string fieldText(Host& h)
{
    h.io();   // makes the host's context current
    const ImGuiInputTextState& state = ImGui::GetCurrentContext()->InputTextState;
    return std::string(state.TextA.Data, (size_t) state.TextLen);
}

// The value bubble a resting hover over the knob at (x, y) draws: the
// foreground list's vertices, which nothing else in this editor draws on.
std::vector<ImDrawVert> bubble(Host& h, float x, float y)
{
    h.move(x, y);
    h.idle(3);
    h.io();
    const ImDrawList* list = ImGui::GetForegroundDrawList();
    return std::vector<ImDrawVert>(list->VtxBuffer.begin(), list->VtxBuffer.end());
}

bool sameVertices(const std::vector<ImDrawVert>& a, const std::vector<ImDrawVert>& b)
{
    return !a.empty() && a.size() == b.size()
        && std::memcmp(a.data(), b.data(), a.size() * sizeof(ImDrawVert)) == 0;
}

void readoutsAreTheDial()
{
    // The format, at every printed mark.
    for (const ReadoutTable& r : kReadouts)
        for (int i = 0; i < r.count; ++i)
        {
            char text[32];
            DAF::frequencyText(text, sizeof(text), r.param, r.marks[i].value);
            detectsDial(std::strcmp(text, r.marks[i].text) == 0, std::string(r.name) + " at " + hz(r.marks[i].value)
                        + " reads \"" + text + "\", the reference \"" + r.marks[i].text + "\"");
        }

    // Between the marks the read-out rounds to its digits before it picks its
    // unit (999.7 is "1.00 k", 99.96 "100 Hz", 9999 "10.0 k"), and rounds rather
    // than truncates (1006 is "1.01 k"); so does the value field the editor
    // opens there.
    struct Rounded { int table; float value; const char* text; };
    for (const Rounded& c : { Rounded { 3, 999.7f, "1.00 k" }, Rounded { 3, 1006.0f, "1.01 k" },
                              Rounded { 2, 99.96f, "100 Hz" }, Rounded { 5, 9999.0f, "10.0 k" } })
    {
        const ReadoutTable& r = kReadouts[c.table];
        char text[32];
        DAF::frequencyText(text, sizeof(text), r.param, c.value);
        detectsDial(std::strcmp(text, c.text) == 0, std::string(r.name) + " at " + hz(c.value) + " reads \"" + text
                    + "\", rounded before the unit is picked: \"" + c.text + "\"");
        Host h(960, 640, startAt(r, c.value, false));
        h.doubleClick(r.x, r.y);
        h.idle();
        const std::string seed = fieldText(h);
        detectsDial(h.fieldActive() && seed == c.text, std::string(r.name) + " at " + hz(c.value)
                    + ": the value field opens on \"" + seed + "\", rounded before the unit is picked: \""
                    + c.text + "\"");
    }

    // The editor: the value field opens on that text, Brown and Black alike,
    // and committing it untouched writes nothing.
    for (const ReadoutTable& r : kReadouts)
        for (int black = 0; black < 2; ++black)
        {
            const Readout& mark = r.marks[r.count - 3];
            Host h(960, 640, startAt(r, mark.value, black));
            h.doubleClick(r.x, r.y);
            h.idle();
            const std::string seed = fieldText(h);
            detectsDial(h.fieldActive() && seed == mark.text, std::string(r.name) + (black ? ", Black," : ", Brown,")
                        + " at " + hz(mark.value) + ": the value field opens on \"" + seed + "\", the reference's \""
                        + mark.text + "\"");
            const size_t m = h.mark();
            h.key(ImGuiKey_Enter);
            h.idle(2);
            dusktest::guards(!h.fieldActive() && h.eventsSince(m) == 0,
                             std::string(r.name) + ": Enter on the untouched text closes the field and writes nothing");
            if (h.eventsSince(m) != 0) h.printEvents(m);
        }
    for (const ReadoutTable& r : { kReadouts[0], kReadouts[1] })
    {
        Host h(960, 640, { { enableOf(r.param), 0.0f } });
        h.doubleClick(r.x, r.y);
        h.idle();
        detectsDial(h.fieldActive() && fieldText(h) == "Out", std::string(r.name) + " switched out: the value field"
                    " opens on \"" + fieldText(h) + "\", the reference's \"Out\"");
    }

    // The value bubble: the same text draws the same bubble and another text
    // another one; neither the band's gain nor the mode moves it.
    {
        const ReadoutTable& lmf = kReadouts[3];
        Host at1000(960, 640, startAt(lmf, 1000.0f, false)), at1004(960, 640, startAt(lmf, 1004.0f, false)),
            at1010(960, 640, startAt(lmf, 1010.0f, false));
        const std::vector<ImDrawVert> b1000 = bubble(at1000, lmf.x, lmf.y);
        detectsDial(sameVertices(b1000, bubble(at1004, lmf.x, lmf.y)),
                    "LMF FREQ at 1000 and at 1004 Hz, both \"1.00 k\": the same value bubble");
        dusktest::guards(!b1000.empty() && !sameVertices(b1000, bubble(at1010, lmf.x, lmf.y)),
                         "LMF FREQ: control, at 1010 Hz (\"1.01 k\") another value bubble");
        const std::pair<float, int> gainsAndModes[] = { { -15.0f, 0 }, { 15.0f, 0 }, { 0.0f, 1 },
                                                        { -15.0f, 1 }, { 15.0f, 1 } };
        for (const auto& [gain, black] : gainsAndModes)
        {
            Values v = startAt(lmf, 1000.0f, black);
            v.push_back({ kLmGain, gain });
            Host h(960, 640, v);
            detectsDial(sameVertices(b1000, bubble(h, lmf.x, lmf.y)),
                        std::string("LMF FREQ at 1000 Hz, ") + (black ? "Black" : "Brown") + ", LMF gain "
                        + hz(gain) + " dB: the value bubble is the one at 0 dB in Brown");
        }
        const ReadoutTable& hpf = kReadouts[0];
        Host brown(960, 640, startAt(hpf, 120.0f, false)), black(960, 640, startAt(hpf, 120.0f, true));
        detectsDial(sameVertices(bubble(brown, hpf.x, hpf.y), bubble(black, hpf.x, hpf.y)),
                    "HPF at 120 Hz: the same value bubble in Brown and Black");
    }

    // The pointer sits on the printed mark of its value, in both modes, at
    // any gain.
    const ImU32 consolePointer = IM_COL32(245, 245, 245, 255);
    const ImU32 filterPointer = IM_COL32(30, 30, 33, 255);
    for (const ReadoutTable& r : kReadouts)
        for (int black = 0; black < 2; ++black)
        {
            const bool filter = r.param == kHpfFreq || r.param == kLpfFreq;
            const Readout& mark = r.marks[r.count - 3];   // the 0.75 mark
            Values v = startAt(r, mark.value, black);
            if (r.param == kLfFreq) v.push_back({ kLfGain, 12.0f });
            if (r.param == kHfFreq) v.push_back({ kHfGain, -12.0f });
            Host h(960, 640, v);
            h.idle(2);
            OwnKnob k;
            k.x = r.x; k.y = r.y;
            const float angle = pointerAngle(h, k, filter ? filterPointer : consolePointer);
            detectsDial(std::fabs(angle - duskdaf::DuskPanel::knobAngle(0.75f)) < 0.005f,
                        std::string(r.name) + (black ? ", Black," : ", Brown,") + " at " + hz(mark.value)
                        + ": its pointer is on that printed mark (" + degrees(angle) + " deg, the mark at 67.5)");
        }

    // Typed values in the reference's format commit as themselves; "Out"
    // switches a filter out and nothing else; text that is neither leaves the
    // field open and writes nothing.
    struct Typed { int table; const char* text; float committed; };
    for (const Typed& t : { Typed { 3, "1.20 k", 1200.0f }, Typed { 2, "30.0 Hz", 30.0f }, Typed { 5, "12.5k", 12500.0f },
                            Typed { 4, "4500", 4500.0f }, Typed { 1, "8.80 kHz", 8800.0f }, Typed { 0, "45 hz", 45.0f } })
    {
        const ReadoutTable& r = kReadouts[t.table];
        Host h(960, 640, startAt(r, r.marks[2].value, false));
        h.doubleClick(r.x, r.y);
        h.idle();
        const size_t m = h.mark();
        h.type(t.text);
        h.key(ImGuiKey_Enter);
        h.idle(2);
        float value = -1.0f;
        const bool ok = !h.fieldActive() && h.lastWrite(m, r.param, value) && value == t.committed
            && h.count(m, HostEvent::Begin, r.param) == 1 && h.protocolOk(m);
        detectsDial(ok, std::string(r.name) + ": typing \"" + t.text + "\" commits " + hz(t.committed)
                    + " in one gesture (" + hz(value) + ")");
        if (!ok) h.printEvents(m);
    }
    for (const ReadoutTable& r : { kReadouts[0], kReadouts[1] })
    {
        Host h(960, 640, startAt(r, r.marks[2].value, false));
        h.doubleClick(r.x, r.y);
        h.idle();
        const size_t m = h.mark();
        h.type("Out");
        h.key(ImGuiKey_Enter);
        h.idle(2);
        float en = -1.0f;
        const bool ok = !h.fieldActive() && h.lastWrite(m, enableOf(r.param), en) && en == 0.0f
            && h.count(m, HostEvent::Write, r.param) == 0 && writesSince(h, m) == 1 && h.protocolOk(m);
        detectsDial(ok, std::string(r.name) + ": typing \"Out\" switches the filter out, in one gesture, and"
                    " writes nothing else");
        if (!ok) h.printEvents(m);
    }
    for (const char* text : { "Out", "1.2 kk", "k" })
    {
        const ReadoutTable& r = kReadouts[2];
        Host h(960, 640, startAt(r, 200.0f, false));
        h.doubleClick(r.x, r.y);
        h.idle();
        const size_t m = h.mark();
        h.type(text);
        h.key(ImGuiKey_Enter);
        h.idle(2);
        detectsDial(h.fieldActive() && h.eventsSince(m) == 0, std::string("LF FREQ: \"") + text
                    + "\" is not a frequency: the field stays open and nothing is written");
        if (h.eventsSince(m) != 0) h.printEvents(m);
    }
}

// The reference's dial at its positions k/20 of the travel, from k = first
// (the reference harness's probe_dense_bands.py FREQUENCY_DISPLAY and
// probe_dense_filters.py HP_DISPLAY and LP_DISPLAY; a filter's run starts at
// 0.1, k = 2, its first mark next to OUT). The printed marks sit at k = 0, 2,
// 5, 10, 15, 18 and 20.
struct Travel { int readout; int first; float dial[21]; };
const Travel kTravels[6] = {
    { 0, 2, { 16, 19, 30, 45, 60, 75, 90, 105, 120, 144, 168, 192, 220, 250, 280, 305, 320, 335, 350 } },
    { 1, 2, { 15201, 12800, 11200, 10000, 8800, 7700, 6800, 5900, 5000, 4700, 4400, 4100, 3900, 3750, 3600,
              3450, 3300, 3150, 3000 } },
    { 2, 0, { 30, 36, 42, 48, 60, 75, 90, 110, 140, 170, 200, 230, 260, 290, 315, 338, 360, 382, 405, 427, 450 } },
    { 3, 0, { 200, 230, 260, 290, 400, 550, 700, 820, 880, 940, 1000, 1150, 1300, 1450, 1600, 1750, 1900, 2050,
              2200, 2350, 2500 } },
    { 4, 0, { 600, 660, 720, 780, 940, 1150, 1360, 1650, 2100, 2550, 3000, 3450, 3900, 4350, 4800, 5250, 5700,
              6100, 6400, 6700, 7000 } },
    { 5, 0, { 1500, 1650, 1800, 1950, 2600, 3500, 4400, 5300, 6200, 7100, 8000, 8600, 9200, 9800, 10800, 12000,
              13200, 14200, 14800, 15400, 16000 } },
};

// Each frequency knob travels as the reference's dial does, at every one of
// its positions, the printed marks and the ends included. Set to the
// reference's value at a position, it points at that position. One wheel
// event from its 0.5 mark onto a position (2.5 notches per 0.05 of the
// travel) writes the reference's value there: a pointer cannot tell an end
// whose value moved inward (it clamps to the end), the value written there
// can.
void travelIsTheReferences()
{
    const ImU32 consolePointer = IM_COL32(245, 245, 245, 255);
    const ImU32 filterPointer = IM_COL32(30, 30, 33, 255);
    for (const Travel& tr : kTravels)
    {
        const ReadoutTable& r = kReadouts[tr.readout];
        const bool filter = tr.first == 2;
        OwnKnob knob;
        knob.x = r.x;
        knob.y = r.y;
        for (int p = tr.first; p <= 20; ++p)
        {
            const float value = tr.dial[p - tr.first];
            Host h(960, 640, startAt(r, value, false));
            h.idle(2);
            const float angle = pointerAngle(h, knob, filter ? filterPointer : consolePointer);
            const float at = duskdaf::DuskPanel::knobAngle((float) p / 20.0f);
            detectsDial(std::fabs(angle - at) < 0.005f, std::string(r.name) + " at " + hz(value) + ": its pointer"
                        " is at " + hz((float) p / 20.0f) + " of the travel, the reference's position for that"
                        " value (" + degrees(angle) + " deg, " + degrees(at) + " deg there)");
        }
        const float middle = tr.dial[10 - tr.first];   // the 0.5 mark
        for (int p = tr.first; p <= 20; ++p)
        {
            if (p == 10)
                continue;   // no movement
            const float notches = 2.5f * (float) (p - 10), to = tr.dial[p - tr.first];
            Host h(960, 640, startAt(r, middle, false));
            h.move(r.x, r.y);
            h.idle(2);
            const size_t m = h.mark();
            h.wheel(notches);
            h.idle(2);
            float value = -1.0f;
            const bool ok = h.lastWrite(m, r.param, value) && std::fabs(value - to) <= kComputed * to;
            detectsDial(ok, std::string(r.name) + " at " + hz(middle) + ": one wheel event of " + hz(notches)
                        + " notches lands on " + hz((float) p / 20.0f) + " of the travel and writes the"
                        " reference's " + hz(to) + " (" + hz(value) + ")");
            if (!ok) h.printEvents(m);
        }
    }
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
    hpf.parameters = { kHpfEnabled, kHpfFreq };
    hpf.valueParameter = kHpfFreq;
    hpf.start = { { kHpfEnabled, 1.0f }, { kHpfFreq, 120.0f } };   // the 120 detent, mid-travel
    hpf.typed = "250"; hpf.committed = 250.0f;
    hpf.resetModifier = ImGuiMod_Super;   // Cmd (macOS) / Ctrl+click: back to OUT
    hpf.resetParameter = kHpfEnabled; hpf.resetValue = 0.0f;
    hpf.fieldHalfWidth = kFrequencyFieldHalfWidth;

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

    // LF FREQ: a console knob on a frequency parameter, which types and reads
    // its dial value.
    ValueFieldKnob lfFreq = lfGain;
    lfFreq.name = "LF FREQ knob";
    lfFreq.y = 412.0f;
    lfFreq.outside = ImVec2(258.5f, 343.0f);
    lfFreq.parameters = { kLfFreq };
    lfFreq.valueParameter = kLfFreq;
    lfFreq.start = { { kLfFreq, 338.0f } };   // a detent away from the 200 Hz default
    lfFreq.typed = "100"; lfFreq.committed = 100.0f;
    lfFreq.resetParameter = kLfFreq; lfFreq.resetValue = kFourKParams[kLfFreq].def;
    lfFreq.fieldHalfWidth = kFrequencyFieldHalfWidth;

    dusktest::ValueFieldSuite<DAF::FourKEQUI> suite(960, 640);
    suite.run(hpf, lfGain);
    suite.run(lfGain, hpf);
    suite.run(lfFreq, hpf);

    ownKnobsEditOnlyOnMovement();
    readoutsAreTheDial();
    travelIsTheReferences();

    return summary();
}
