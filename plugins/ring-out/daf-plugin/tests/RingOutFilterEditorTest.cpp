// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "imgui.h"
#include "DuskStepperBox.hpp"
#include "RingOutFilterEditor.hpp"
#include <cstdio>

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
    if (!failures) std::puts("PASS: Ring Out filter text-entry selection and row identity");
    return failures == 0 ? 0 : 1;
}
