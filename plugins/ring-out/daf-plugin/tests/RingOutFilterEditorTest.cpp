// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "imgui.h"
#include "DuskStepperBox.hpp"
#include "RingOutFilterEditor.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(960, 600);
    io.DeltaTime = 1.0f/60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    duskdaf::DuskPanel panel;
    panel.begin(1, ImVec2(0,0), nullptr, nullptr);
    duskaudio::ringout::FilterTable table;
    auto filter = duskaudio::ringout::defaultFilter();
    filter.freqHz = 1000; table.add(filter, 1);
    filter.freqHz = 2000; table.add(filter, 2);
    RoFilterEditSelection selection;
    int failures = 0;
    const auto check = [&](bool ok, const char* message)
    {
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
    };
    int selected = 0;
    duskdaf::StepperBoxRule rule;
    rule.step = [](float v, int d, void*){return duskaudio::ringout::stepFreq(v,d);};
    rule.snap = [](float v,void*){return duskaudio::ringout::snapFreq(v);};
    rule.minV = 24; rule.maxV = 20000; rule.unit = duskdaf::StepperBoxRule::kHertz;
    const auto frame = [&]
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(960,600));
        ImGui::Begin("RingOut",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize);
        ImGui::SetCursorScreenPos(ImVec2(70,420));
        ImGui::InvisibleButton("##led1",ImVec2(22,22));
        if(ImGui::IsItemClicked()) selected=1;
        selection.update(panel, table, selected);
        const auto cur = table.f[selected];
        ImGui::SetCursorScreenPos(ImVec2(56,474));
        ImGui::InvisibleButton("##on", ImVec2(32,32));
        if (ImGui::IsItemClicked())
            duskaudio::ringout::applyEditCommand(table,
                roFilterFieldCommand(table, selected, &duskaudio::ringout::Filter::on, !cur.on));
        float freq = cur.freqHz;
        if(duskdaf::stepperBox(panel,ImGui::GetWindowDrawList(),"freq",222,476,326,504,freq,rule,"%.0f",true))
        {
            std::printf("COMMIT row=%d value=%g\n", selected, freq);
            duskaudio::ringout::applyEditCommand(table,
                roFilterFieldCommand(table, selected, &duskaudio::ringout::Filter::freqHz, freq));
        }
        ImGui::End(); ImGui::Render();
    };
    frame();
    panel.openValueEdit("freq",1000,"1000");
    frame(); frame();
    io.AddInputCharactersUTF8("1500");
    frame(); frame();
    io.AddMousePosEvent(81,431);
    io.AddMouseButtonEvent(0,true);
    frame(); frame();
    io.AddMouseButtonEvent(0,false);
    frame(); frame();
    std::printf("selection change: selected=%d frequencies=%g,%g\n", selected,
                table.f[0].freqHz, table.f[1].freqHz);
    check(selected == 1, "click reached the second filter");
    check(table.f[0].freqHz == 1000 && table.f[1].freqHz == 2000,
          "changing selection cancels pending text without retuning either filter");
    check(!panel.isEditingValue("freq"), "old field editor is retired");

    // The unchanged row still accepts typed entry; cancellation cannot make all
    // text edits no-ops and pass the previous assertion vacuously.
    panel.openValueEdit("freq", table.f[selected].freqHz, "2000");
    frame(); frame();
    io.AddInputCharactersUTF8("2500");
    frame(); frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(); frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    check(table.f[1].freqHz == 2500, "unchanged selected row accepts typed entry");

    panel.openValueEdit("freq", 2500, "2500");
    frame(); frame();
    io.AddInputCharactersUTF8("3000");
    frame(); frame();
    io.AddMousePosEvent(72,490);
    io.AddMouseButtonEvent(0,true);
    frame(); frame();
    io.AddMouseButtonEvent(0,false);
    frame(); frame();
    std::printf("sibling edits: on=%d frequency=%g\n", table.f[1].on, table.f[1].freqHz);
    check(!table.f[1].on && table.f[1].freqHz == 3000,
          "focus-loss frequency commit preserves same-frame ON toggle");
    // The same one-field path serves CUT/Q and preserves detector updates too.
    table.f[1].cutDb = -12.0f;
    duskaudio::ringout::applyEditCommand(table,
        roFilterFieldCommand(table, 1, &duskaudio::ringout::Filter::q, 8.0f));
    check(table.f[1].cutDb == -12.0f && table.f[1].q == 8.0f && !table.f[1].on,
          "Q change preserves independent cut and on state");

    for (const char* field : {"cut", "freq", "q"})
    {
        panel.openValueEdit(field, 1, "2");
        ++table.id[selected];
        selection.update(panel, table, selected);
        check(!panel.isEditingValue(field), "same row index with new identity cancels text entry");
    }
    panel.openValueEdit("freq", 2500, "2700");
    selection.update(panel, table, -1);
    check(!panel.isEditingValue("freq"), "no selection cancels text entry");
    panel.openValueEdit("##globalq", 1, "2");
    selection.update(panel, table, 0);
    check(panel.isEditingValue("##globalq"), "selection changes preserve global knob text entry");
    ImGui::DestroyContext();

    // RoNotchDrag, the arithmetic behind both the graph-node and the fader
    // drags: accumulated, snapped, clamped, and followed by row id. (The pixel
    // scales, the frozen axis and the ImGui gestures live in RingOutUI.cpp and
    // are checked by hand and by the ring-outUiDrag gate, not here.)
    {
        namespace ro = duskaudio::ringout;
        const auto near = [](float a, float b) { return std::fabs(a - b) < 1.0e-3f; };
        ro::Filter f = ro::defaultFilter();
        f.freqHz = 1000.0f;
        f.cutDb = -6.0f;
        ro::FilterTable t;
        t.add(f, 7);

        RoNotchDrag drag;
        check(drag.resolve(t) == -1, "an idle drag resolves to no row");
        drag.begin(t, 0);
        drag.move(1.0f, -3.0f, false);
        check(near(drag.freqHz(), 2000.0f) && near(drag.cutDb(), -9.0f),
              "one octave up and 3 dB deeper");
        drag.move(1.0f, 10.0f, true);
        check(near(drag.freqHz(), 2140.0f) && near(drag.cutDb(), -8.0f),
              "Shift scales the motion by a tenth (2143.5 Hz, dragged, lands on 2140)");
        drag.move(20.0f, -40.0f, false);
        check(near(drag.freqHz(), ro::kFreqMax) && near(drag.cutDb(), ro::kCutMin),
              "clamped at 20 kHz and the cut floor");
        drag.move(-60.0f, 80.0f, false);
        check(near(drag.freqHz(), ro::kFreqMin) && near(drag.cutDb(), ro::kCutMax),
              "clamped at 24 Hz and 0 dB");
        drag.move(1.0f, -3.0f, false);
        check(near(drag.freqHz(), 2.0f * ro::kFreqMin) && near(drag.cutDb(), -3.0f),
              "overshoot past a clamp is dropped, so turning back moves at once");

        drag.begin(t, 0);
        t.f[0].cutDb = -15.0f;   // an engine deepen landing mid-drag
        drag.move(0.0f, -1.0f, false);
        check(near(drag.cutDb(), -7.0f), "the drag edits from its own start, not the moving mirror");

        ro::FilterTable moved;
        moved.add(ro::defaultFilter(), 3);
        moved.add(f, 7);
        check(drag.resolve(moved) == 1, "the drag follows its row id to a new index");
        moved.remove(1);
        check(drag.resolve(moved) == -1, "a removed notch ends the drag");

        ro::FilterTable fresh;
        fresh.add(f, 0);
        drag.begin(fresh, 0);
        check(drag.resolve(fresh) == 0, "a row with no id yet is followed by index");
        fresh.id[0] = 9;
        fresh.add(ro::defaultFilter(), 10);
        check(drag.resolve(fresh) == 0 && drag.rowId == 9, "it adopts the id the engine assigns");
        fresh.clear();
        check(drag.resolve(fresh) == -1, "a cleared table ends the drag");
        drag.end();
        check(drag.resolve(t) == -1, "an ended drag resolves to no row");

        // A drag reads in three significant figures, and a notch that has not
        // moved reads the same every frame, so a press alone writes nothing.
        ro::FilterTable exact;
        ro::Filter detected = ro::defaultFilter();
        detected.freqHz = 66.2837f;
        exact.add(detected, 1);
        drag.begin(exact, 0);
        check(near(drag.freqHz(), 66.3f), "a detected 66.2837 Hz reads 66.3 under the drag");
        drag.move(0.0005f, 0.0f, false);
        check(near(drag.freqHz(), 66.3f), "motion inside the rounding leaves it where it was");
        exact.f[0].freqHz = 15001.0f;
        drag.begin(exact, 0);
        check(near(drag.freqHz(), 15000.0f), "15001 Hz drags as 15000, not \"15001\"");
    }

    // Significant-figure rounding and the notch axis it sits beside.
    {
        const auto near = [](float a, float b) { return std::fabs(a - b) < 1.0e-3f; };
        check(near(roRoundSignificant(66.25f, 3), 66.3f) && near(roRoundSignificant(249.6f, 3), 250.0f)
                  && near(roRoundSignificant(1254.0f, 3), 1250.0f) && near(roRoundSignificant(19996.0f, 3), 20000.0f),
              "three significant figures across the band");
        check(roRoundSignificant(0.0f, 3) == 0.0f && roRoundSignificant(-5.0f, 3) == -5.0f,
              "non-positive values pass through");

        const struct { float deepest, floor; int step; } axis[] = {
            { 0.0f, -24.0f, 6 },  { -3.0f, -24.0f, 6 },   { -24.0f, -24.0f, 6 }, { -24.01f, -24.0f, 6 },
            { -26.0f, -36.0f, 12 }, { -40.0f, -48.0f, 12 }, { -168.0f, -48.0f, 12 },
        };
        for (const auto& a : axis)
        {
            const float got = roNotchAxisFloorDb(a.deepest);
            const int step = roNotchAxisStepDb(got);
            if (got != a.floor || step != a.step)
            {
                ++failures;
                std::fprintf(stderr, "FAIL: axis for %g dB: floor %g step %d, want %g / %d\n",
                             (double)a.deepest, (double)got, step, (double)a.floor, a.step);
            }
        }
    }

    // Strip read-outs stay inside their band's width.
    {
        char b[16];
        const struct { float hz; const char* want; } cases[] = {
            { 24.0f, "24.0" }, { 66.34f, "66.3" }, { 99.97f, "100" }, { 880.0f, "880" },
            { 999.6f, "1.00k" }, { 1250.0f, "1.25k" }, { 9996.0f, "10.0k" }, { 20000.0f, "20.0k" },
        };
        for (const auto& c : cases)
        {
            roShortHz(c.hz, b, (int)sizeof(b));
            if (std::strcmp(b, c.want) != 0)
            {
                ++failures;
                std::fprintf(stderr, "FAIL: roShortHz(%g) = \"%s\", want \"%s\"\n", (double)c.hz, b, c.want);
            }
        }
    }

    if (!failures) std::puts("PASS: Ring Out filter text-entry selection, row identity, notch drags and notch axis");
    return failures == 0 ? 0 : 1;
}
