// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQUI.cpp — MINIMAL placeholder Dear ImGui UI for Multi-Q 2 (Phase 2 shell).
// A titled panel and an EQ Type selector combo, nothing more; it only needs to
// build and let the plugin load in a host. The full 8-band curve editor UI lands
// in Phase 3.

#include "DistrhoUI.hpp"
#include "MultiQParams.hpp"

START_NAMESPACE_DISTRHO

class MultiQUI : public UI
{
public:
    MultiQUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kMqParams[i].def;
        setGeometryConstraints(520, 340, true);
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

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        if (ImGui::Begin("Multi-Q 2", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SetWindowFontScale(1.6f);
            ImGui::TextUnformatted("Multi-Q 2");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextDisabled("Universal EQ - Digital / Match / British / Tube");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 8));

            // EQ Type selector — the one live control in this placeholder shell.
            const MqParam& d = kMqParams[kParamEqType];
            int cur = (int)(values[kParamEqType] + 0.5f);
            if (cur < 0) cur = 0;
            if (cur >= d.numChoices) cur = d.numChoices - 1;

            ImGui::TextUnformatted("EQ Type");
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##eqtype", d.choices[cur]))
            {
                for (int i = 0; i < d.numChoices; ++i)
                {
                    if (ImGui::Selectable(d.choices[i], i == cur))
                    {
                        values[kParamEqType] = (float)i;
                        editParameter(kParamEqType, true);
                        setParameterValue(kParamEqType, (float)i);
                        editParameter(kParamEqType, false);
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Dummy(ImVec2(0, 16));
            ImGui::TextDisabled("Full UI arrives in Phase 3.");
        }
        ImGui::End();
    }

private:
    float values[kParamCount] = {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiQUI)
};

UI* createUI()
{
    return new MultiQUI();
}

END_NAMESPACE_DISTRHO
