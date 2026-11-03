// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later.
#include "DearImGui/imgui.h"
#include "../DuskImGuiWidgets.hpp"
#include <cmath>
#include <cstdio>

struct Host final : duskdaf::ParamHost
{
    int begins = 0, ends = 0, edits = 0;
    void beginEdit(uint32_t) override { ++begins; }
    void endEdit(uint32_t) override { ++ends; }
    void setParam(uint32_t, float) override { ++edits; }
};

int main()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
        failures += !ok;
    };
    for (float previousY : {20.0f, 200.0f, 260.0f})
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(400, 300);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Host host;
        duskdaf::DuskPanel panel;
        float value = 0.5f;
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 300));
            ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings);
            panel.begin(1.0f, ImVec2(0, 0), ImGui::GetFont(), &host);
            panel.knob("knob", 0, 0, 1, 100, 100, 30, value, 0.5f);
            ImGui::End();
            ImGui::Render();
        };
        io.AddMousePosEvent(200, previousY);
        frame(); frame();
        // Real backends can deliver both events before the next UI frame.
        io.AddMousePosEvent(100, 100);
        io.AddMouseButtonEvent(0, true);
        frame();
        check(value == 0.5f && host.edits == 0,
              "coalesced approach and press do not change the value");
        check(host.begins == 1, "press opens exactly one host gesture");
        io.AddMousePosEvent(100, 88);
        frame();
        check(std::abs(value - 0.56f) < 1.0e-6f,
              "drag uses only the twelve pixels after the press");
        io.AddKeyEvent(ImGuiMod_Shift, true);
        frame();
        io.AddMousePosEvent(100, 78);
        frame();
        check(std::abs(value - 0.568f) < 1.0e-6f,
              "shift fine adjustment keeps its existing sensitivity");
        io.AddMouseButtonEvent(0, false);
        frame();
        check(host.ends == 1, "release closes exactly one host gesture");
        ImGui::DestroyContext();
    }

    // A caller's own entry target (the read-out under a knob) opens the inline
    // field. A second click on that target while the field is open is not a
    // click-away: the field stays open and keeps what was typed. A click
    // anywhere else still closes it.
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(400, 300);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Host host;
        duskdaf::DuskPanel panel;
        float value = 0.5f;
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 300));
            ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings);
            panel.begin(1.0f, ImVec2(0, 0), ImGui::GetFont(), &host);
            ImGui::SetCursorScreenPos(ImVec2(60, 150));
            ImGui::InvisibleButton("##entry", ImVec2(80, 20));
            if (ImGui::IsItemClicked()) panel.openValueEdit("knob", value);
            panel.knob("knob", 0, 0, 1, 100, 100, 30, value, 0.5f);
            ImGui::End();
            ImGui::Render();
        };
        const auto click = [&](float x, float y) {
            io.AddMousePosEvent(x, y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        };
        frame();
        click(100, 160);
        check(panel.isEditingValue("knob"), "a click on the entry target opens the field");
        click(100, 160);
        check(panel.isEditingValue("knob"), "a second click on the entry target keeps the field open");
        io.AddInputCharactersUTF8("0.7");
        frame();
        click(100, 160);
        check(panel.isEditingValue("knob"), "a re-click after typing keeps the field open");
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        check(std::abs(value - 0.7f) < 1.0e-6f && !panel.isEditingValue("knob"),
              "text typed before a re-click commits on Enter");
        click(100, 160);
        click(300, 260);
        check(!panel.isEditingValue("knob"), "a click elsewhere still closes the field");

        // Text kept across a re-click still commits when focus leaves the field:
        // ImGui forgets the edit when it re-activates the field, the text does not.
        click(100, 160);
        io.AddInputCharactersUTF8("0.3");
        frame();
        click(100, 160);
        click(300, 260);
        check(std::abs(value - 0.3f) < 1.0e-6f && !panel.isEditingValue("knob"),
              "text kept across a re-click commits on a click elsewhere");
        ImGui::DestroyContext();
    }

    // Two knobs, each with its entry target drawn just before it. Moving from one
    // open field to the other knob's entry target commits the open field first,
    // whichever of the two is drawn earlier, and a click inside the open field
    // stays in the field: it is not a press on the knob underneath.
    {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(400, 300);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Host host;
        duskdaf::DuskPanel panel;
        float a = 0.5f, b = 0.5f;
        bool drawA = true;
        const auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(400, 300));
            ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings);
            panel.begin(1.0f, ImVec2(0, 0), ImGui::GetFont(), &host);
            if (drawA)
            {
                ImGui::SetCursorScreenPos(ImVec2(60, 150));
                ImGui::InvisibleButton("##entryA", ImVec2(80, 20));
                if (ImGui::IsItemClicked()) panel.openValueEdit("A", a);
                panel.knob("A", 0, 0, 1, 100, 100, 30, a, 0.5f);
            }
            ImGui::SetCursorScreenPos(ImVec2(260, 150));
            ImGui::InvisibleButton("##entryB", ImVec2(80, 20));
            if (ImGui::IsItemClicked()) panel.openValueEdit("B", b);
            panel.knob("B", 1, 0, 1, 300, 100, 30, b, 0.5f);
            ImGui::End();
            ImGui::Render();
        };
        const auto click = [&](float x, float y) {
            io.AddMousePosEvent(x, y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        };
        frame();
        click(300, 160);                       // open B (drawn later)
        io.AddInputCharactersUTF8("0.2");
        frame();
        click(100, 160);                       // A's entry is drawn before B's field
        check(std::abs(b - 0.2f) < 1.0e-6f, "the open field commits before an earlier knob's field opens");
        check(panel.isEditingValue("A"), "the earlier knob's field opens after the commit");
        io.AddInputCharactersUTF8("0.8");
        frame();
        click(300, 160);                       // B's entry is drawn after A's field
        check(std::abs(a - 0.8f) < 1.0e-6f, "the open field commits before a later knob's field opens");
        check(panel.isEditingValue("B"), "the later knob's field opens after the commit");

        const int begins = host.begins, ends = host.ends;
        const float before = b;
        click(300, 100);                       // inside B's field, over B's knob
        check(panel.isEditingValue("B"), "a click inside the open field keeps it open");
        check(host.begins == begins && host.ends == ends && b == before,
              "a click inside the open field is not a gesture on the knob under it");

        // An open field that does not draw any more (its knob went away) hands
        // over to the field asked for.
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        click(100, 160);
        check(panel.isEditingValue("A"), "A opens again");
        drawA = false;
        click(300, 160);
        check(panel.isEditingValue("B"), "a field asked for while an undrawn one is open still opens");
        drawA = true;
        frame();

        // An open field whose text does not parse hands over too, dropping that
        // text, and a double-click on another knob in that state leaves no host
        // gesture open and drags nothing.
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        click(100, 160);
        io.AddInputCharactersUTF8("abc");
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
        check(panel.isEditingValue("A"), "unparseable text keeps its field open");
        const float aBefore = a, bBefore = b;
        const int openBegins = host.begins, openEnds = host.ends;
        io.AddMousePosEvent(300, 100);
        frame();
        io.AddMouseButtonEvent(0, true);   // double-click B's knob
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMousePosEvent(300, 60);      // and drag while still holding
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        check(panel.isEditingValue("B"), "a double-click on another knob opens its field");
        check(a == aBefore && b == bBefore, "the dropped text and the held press change nothing");
        check(host.begins - openBegins == host.ends - openEnds,
              "every host gesture the double-click opened is closed exactly once");
        ImGui::DestroyContext();
    }
    return failures ? 1 : 0;
}
