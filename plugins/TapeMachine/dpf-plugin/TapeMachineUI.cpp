// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineUI.cpp — PLACEHOLDER Dear ImGui UI for TapeMachine 2.
//
// This is a minimal, valid UI so the plugin builds/loads/pluginval-passes while
// the DSP port is validated. The full reel-to-reel panel (dual VU, tape-speed
// selector, saturation/bias/noise controls on duskdpf::DuskPanel) lands in the
// dedicated UI phase.

#include "DistrhoUI.hpp"
#include "TapeMachineParams.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

namespace {
    constexpr float kDesignW = 920.0f;
    constexpr float kDesignH = 480.0f;
}

class TapeMachineUI : public UI
{
public:
    TapeMachineUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kTmParams[i].def;
        setGeometryConstraints(460, 240, true);
        labelFont = duskdpf::loadCrispFont(30.0f * getScaleFactor());
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount)
            values[index] = value;
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        const float s = std::min(winW / kDesignW, winH / kDesignH);
        const ImVec2 org(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("TapeMachine2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto P = [&](float x, float y) { return ImVec2(org.x + x * s, org.y + y * s); };

        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 8, 8, 255));
        dl->AddRectFilled(P(0, 0), P(kDesignW, kDesignH), IM_COL32(26, 26, 28, 255));
        dl->AddRectFilled(P(0, 0), P(kDesignW, 46), IM_COL32(16, 16, 17, 255));

        ImFont* font = labelFont ? labelFont : ImGui::GetFont();
        auto text = [&](float x, float y, float size, ImU32 c, const char* t) {
            dl->AddText(font, size * s, P(x, y), c, t);
        };
        text(38, 12, 20, IM_COL32(238, 236, 228, 255), "TapeMachine 2");
        text(kDesignW - 150, 14, 15, IM_COL32(238, 236, 228, 255), "Dusk Audio");
        text(38, kDesignH * 0.5f - 8, 13, IM_COL32(150, 150, 152, 255),
             "UI in progress \xE2\x80\x94 DSP validation build");

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    ImFont* labelFont = nullptr;
    float   values[kParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeMachineUI)
};

UI* createUI()
{
    return new TapeMachineUI();
}

END_NAMESPACE_DISTRHO
