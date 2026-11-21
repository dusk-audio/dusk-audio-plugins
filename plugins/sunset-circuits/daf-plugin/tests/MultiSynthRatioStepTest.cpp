// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Prism's operator RATIO knob, on the production editor. A plain drag and the
// mouse wheel step through one list: the classic FM ratios (0.25, 0.5, 0.75, 1,
// 1.5, 2..16) together with the frequency-ratio table of the DX21/27/100 that
// Prism is modelled on (coarse 0.5, 1..15 times the chip's DT2 offsets of 0,
// +600, +781 and +950 cents), up to the knob's 16. Before, plain drag snapped to
// the classic ratios only, so the DX's inharmonic ratios could only be typed.
// One step is a fixed 5 px of drag, so the dense top of the list stays usable.
#include "DafUI.hpp"   // the window-free host (shared-daf/ui/tests/daf-ui-test-host), first on the path
#include "MultiSynthUI.cpp"
#include "DuskValueFieldTest.hpp"
#include "MultiSynthTestBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
using Host = dusktest::EditorHost<DAF::MultiSynthUI>;

// Operator 1 RATIO in Prism (drawPrismOps), design pixels; at the design size a
// design coordinate is a window pixel. The same knob MultiSynthValueFieldTest uses.
constexpr float kKnobX = 94.0f, kKnobY = 102.0f;
constexpr float kStepPx = 5.0f;

// The expected list, built here from the hardware's definition rather than
// taken from the editor.
std::vector<float> expectedList()
{
    std::vector<float> v = { 0.25f, 0.5f, 0.75f, 1.5f };
    for (int coarse = 0; coarse <= 16; ++coarse)
        for (double cents : { 0.0, 600.0, 781.0, 950.0 })
        {
            const double r = (coarse == 0 ? 0.5 : coarse) * std::exp2(cents / 1200.0);
            if (r <= 16.0) v.push_back((float)r);
        }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end(), [](float a, float b) { return std::fabs(a / b - 1.0f) < 1e-6f; }),
            v.end());
    return v;
}

// The value the editor last sent the host for operator 1's ratio since `start`,
// or NaN when it sent none.
float lastRatioWrite(const Host& h, size_t start)
{
    float v = NAN;
    for (size_t e = start; e < h.ui.hostEvents.size(); ++e)
        if (h.ui.hostEvents[e].kind == dusktest::HostEvent::Write && h.ui.hostEvents[e].parameter == kParamOp1Ratio)
            v = h.ui.hostEvents[e].value;
    return v;
}

bool near(float a, float b) { return std::fabs(a / b - 1.0f) < 1e-5f; }   // 0.02 cents

std::string num(float v) { char b[32]; std::snprintf(b, sizeof b, "%.4f", (double)v); return b; }

float dragUp(Host& h, float px)
{
    const size_t start = h.ui.hostEvents.size();
    h.drag(kKnobX, kKnobY, 0.0f, -px);
    h.idle();   // past ImGui's double-click window: the next press is a new drag, not a double-click
    return lastRatioWrite(h, start);
}

float wheel(Host& h, float steps)
{
    const size_t start = h.ui.hostEvents.size();
    h.move(kKnobX, kKnobY);
    h.wheel(steps);
    h.idle(2);
    return lastRatioWrite(h, start);
}
} // namespace

int main()
{
    dusktest::isolateHome(SUNSET_UI_TEST_HOME);
    using dusktest::guards;
    const std::vector<float> list = expectedList();
    guards(list.size() == 53, "the ratio list has 53 entries (has " + std::to_string(list.size()) + ")");

    // Plain drag, one step per 5 px, from the bottom of the list to the top: it
    // visits every entry in order and stops at 16.
    {
        Host h(1240, 780, { { kParamMode, 4.0f }, { kParamOp1Ratio, 0.25f } });
        std::vector<float> walk = { 0.25f };
        bool stepped = true;
        for (size_t k = 1; k < list.size(); ++k)
        {
            const float got = dragUp(h, kStepPx);
            if (std::isnan(got)) { stepped = false; std::printf("  drag %zu sent no value\n", k); break; }
            walk.push_back(got);
        }
        bool same = stepped && walk.size() == list.size();
        for (size_t k = 0; same && k < list.size(); ++k)
            if (!near(walk[k], list[k]))
            {
                std::printf("  entry %zu: dragged to %s, expected %s\n", k, num(walk[k]).c_str(), num(list[k]).c_str());
                same = false;
            }
        guards(same, "a 5 px plain drag steps to the next entry, through all 53 from 0.25 to 16");
        const float past = dragUp(h, kStepPx);
        guards(std::isnan(past) || near(past, 16.0f), "a drag past 16 stays at 16");
    }

    // A longer drag takes one step per 5 px: four steps up from 1 is the DX's x1.73.
    {
        Host h(1240, 780, { { kParamMode, 4.0f }, { kParamOp1Ratio, 1.0f } });
        const float got = dragUp(h, 4.0f * kStepPx);
        guards(!std::isnan(got) && near(got, (float)std::exp2(950.0 / 1200.0)),
               "a 20 px drag from 1 lands four entries up, on x1.7311 (got " + num(got) + ")");
        const size_t start = h.ui.hostEvents.size();
        h.drag(kKnobX, kKnobY, 0.0f, -(kStepPx - 1.0f));
        h.idle();
        guards(std::isnan(lastRatioWrite(h, start)), "a drag shorter than one step leaves the ratio alone");
    }

    // The wheel steps the same list, and from a typed value between two entries
    // it moves to the neighbour, not past it.
    {
        Host h(1240, 780, { { kParamMode, 4.0f }, { kParamOp1Ratio, 2.0f } });
        const float up = wheel(h, 1.0f);
        guards(!std::isnan(up) && near(up, (float)(2.0 * std::exp2(0.5))), "wheel up from 2 gives x2.8284 (got " + num(up) + ")");
        h.ui.parameter(kParamOp1Ratio, 1.45f);
        h.idle(2);
        const float upTyped = wheel(h, 1.0f);
        guards(!std::isnan(upTyped) && near(upTyped, 1.5f), "wheel up from a typed 1.45 gives 1.5 (got " + num(upTyped) + ")");
        h.ui.parameter(kParamOp1Ratio, 1.45f);
        h.idle(2);
        const float downTyped = wheel(h, -1.0f);
        guards(!std::isnan(downTyped) && near(downTyped, (float)std::exp2(0.5)),
               "wheel down from a typed 1.45 gives x1.4142 (got " + num(downTyped) + ")");
    }

    // Shift-drag stays continuous: 10 px up from 2 is off the list.
    {
        Host h(1240, 780, { { kParamMode, 4.0f }, { kParamOp1Ratio, 2.0f } });
        h.modifier(ImGuiMod_Shift, true);   // io.KeyShift follows the Shift modifier key
        const float got = dragUp(h, 10.0f);
        h.modifier(ImGuiMod_Shift, false);
        bool onList = false;
        for (float e : list) onList = onList || (!std::isnan(got) && near(got, e));
        guards(!std::isnan(got) && got > 2.0f && got < 2.828f && !onList,
               "a Shift-drag moves continuously between entries (got " + num(got) + ")");
    }

    return dusktest::summary();
}
