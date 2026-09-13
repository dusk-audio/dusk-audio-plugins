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
    return failures ? 1 : 0;
}
