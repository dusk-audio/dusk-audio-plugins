// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DuskSupportersOverlay.hpp — framework-free (DPF/Dear ImGui) Patreon "Special Thanks"
// credits overlay. The non-JUCE counterpart of plugins/shared/SupportersOverlay.h, so
// DPF ports show the same click-title-to-see-supporters panel as the JUCE plugins.
//
// Usage (call every frame, LAST, while `open` is true — it draws a scrim over the UI):
//     if (showSupporters)
//         duskdpf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH,
//                                        showSupporters, "TapeMachine 2", "1.0.1");
// Open it from a title/logo hit-test:  if (titleClicked) showSupporters = true;
// Reads the generated, JUCE-free duskdpf::patreonTiers() list. Reusable across all DPF UIs.

#pragma once

#include "DuskImGuiWidgets.hpp"   // duskdpf::DuskPanel (crisp text + P()/scale); requires imgui.h pre-included
#include "PatreonBackersDpf.hpp"  // duskdpf::patreonTiers() — generated, framework-free

#include <algorithm>   // std::min
#include <cstdio>      // std::snprintf
#include <cstring>     // std::strcmp

namespace duskdpf
{

// Per-tier accent colour, matched to the JUCE SupportersOverlay palette (HUGS is DPF-only, warm).
inline ImU32 supporterTierColor(const char* title) noexcept
{
    if (std::strcmp(title, "CHAMPIONS") == 0)       return IM_COL32(255, 215,   0, 255);  // gold
    if (std::strcmp(title, "PATRONS") == 0)         return IM_COL32(  0, 170, 255, 255);  // blue
    if (std::strcmp(title, "SUPPORTERS") == 0)      return IM_COL32(106, 196, 126, 255);  // green
    if (std::strcmp(title, "HUGS") == 0)            return IM_COL32(224, 156, 112, 255);  // warm peach
    if (std::strcmp(title, "PAST SUPPORTERS") == 0) return IM_COL32(140, 140, 140, 255);  // grey
    return IM_COL32(200, 200, 204, 255);
}

// Full-canvas "Special Thanks" credits overlay. Immediate-mode: draws a dark scrim over the whole
// plugin area, a centred panel with per-tier accented names (scrollable so a growing list never
// overflows), and a footer. Clicking the scrim (anywhere outside the scrolling list) dismisses.
// Coordinates are the plugin's DESIGN space (0..designW/H); the shared DuskPanel maps to screen.
inline void drawSupportersOverlay(DuskPanel& panel, ImDrawList* dl,
                                  float designW, float designH, bool& open,
                                  const char* pluginName, const char* version = "")
{
    if (!open)
        return;

    const float s = panel.scale();

    // --- scrim + click-to-dismiss (any click outside the scrolling name list) ---------------
    dl->AddRectFilled(panel.P(0.0f, 0.0f), panel.P(designW, designH), IM_COL32(16, 16, 16, 242));
    ImGui::SetCursorScreenPos(panel.P(0.0f, 0.0f));
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##supscrim", ImVec2(designW * s, designH * s)))
        open = false;

    // --- centred panel (design coords) ------------------------------------------------------
    const float pw = std::min(384.0f, designW - 56.0f);
    const float ph = std::min(452.0f, designH - 40.0f);
    const float px = 0.5f * (designW - pw);
    const float py = 0.5f * (designH - ph);

    dl->AddRectFilled(panel.P(px, py), panel.P(px + pw, py + ph), IM_COL32(30, 30, 32, 255), 11.0f * s);
    dl->AddRect      (panel.P(px, py), panel.P(px + pw, py + ph), IM_COL32(80, 80, 84, 255), 11.0f * s, 0, 1.6f * s);

    // header
    panel.text(dl, px + pw * 0.5f, py + 20.0f, 18.0f, IM_COL32(232, 232, 232, 255), "Special Thanks", 0, true);
    panel.text(dl, px + pw * 0.5f, py + 46.0f, 9.5f,  IM_COL32(120, 120, 120, 255),
               "To our supporters who make this plugin possible", 0);
    dl->AddRectFilled(panel.P(px + 26.0f, py + 66.0f), panel.P(px + pw - 26.0f, py + 67.0f), IM_COL32(58, 58, 58, 255));

    // --- scrolling tier/name list -----------------------------------------------------------
    const float listTop = py + 76.0f;
    const float footerH = 40.0f;
    const float listH   = ph - (listTop - py) - footerH;

    ImGui::SetCursorScreenPos(panel.P(px + 14.0f, listTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##suplist", ImVec2((pw - 28.0f) * s, listH * s), false,
                      ImGuiWindowFlags_None);   // native scrollbar appears only when the list overflows
    {
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        const float avail = ImGui::GetContentRegionAvail().x;

        auto centred = [&](const char* txt, float px_sz, ImU32 col, float advance)
        {
            ImFont* f = panel.pickFont(px_sz * s);
            const float fsz = px_sz * s;
            const ImVec2 ts = f->CalcTextSizeA(fsz, FLT_MAX, 0.0f, txt);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            cdl->AddText(f, fsz, ImVec2(std::floor(cur.x + 0.5f * (avail - ts.x) + 0.5f),
                                        std::floor(cur.y + 0.5f)), col, txt);
            ImGui::Dummy(ImVec2(avail, advance * s));
        };

        bool first = true;
        for (const BackerTier& tier : patreonTiers())
        {
            if (tier.names.empty())
                continue;
            const bool isPast = (std::strcmp(tier.title, "PAST SUPPORTERS") == 0);
            if (!first)
                ImGui::Dummy(ImVec2(avail, 12.0f * s));   // gap between tiers
            first = false;

            const ImU32 accent = supporterTierColor(tier.title);
            centred(tier.title, 9.5f, accent, 17.0f);

            // short accent line under the heading
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const float lineW = 26.0f * s;
            cdl->AddRectFilled(ImVec2(cur.x + 0.5f * (avail - lineW), cur.y),
                               ImVec2(cur.x + 0.5f * (avail + lineW), cur.y + 1.0f * s),
                               (accent & 0x00ffffff) | 0x66000000);
            ImGui::Dummy(ImVec2(avail, 8.0f * s));

            const ImU32 nameCol = isPast ? IM_COL32(130, 130, 130, 255) : IM_COL32(206, 206, 206, 255);
            for (const char* name : tier.names)
                centred(name, isPast ? 11.0f : 12.0f, nameCol, 15.5f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // --- footer -----------------------------------------------------------------------------
    dl->AddRectFilled(panel.P(px + 26.0f, py + ph - footerH), panel.P(px + pw - 26.0f, py + ph - footerH + 1.0f),
                      IM_COL32(58, 58, 58, 255));
    panel.text(dl, px + pw * 0.5f, py + ph - footerH + 8.0f, 10.0f, IM_COL32(96, 96, 96, 255),
               "Click anywhere to close", 0);

    char credit[128];
    if (pluginName == nullptr || pluginName[0] == '\0')
        std::snprintf(credit, sizeof(credit), "by Dusk Audio");
    else if (version == nullptr || version[0] == '\0')
        std::snprintf(credit, sizeof(credit), "%s by Dusk Audio", pluginName);
    else
        std::snprintf(credit, sizeof(credit), "%s v%s by Dusk Audio", pluginName, version);
    panel.text(dl, px + pw * 0.5f, py + ph - footerH + 23.0f, 10.0f, IM_COL32(80, 80, 80, 255), credit, 0);
}

} // namespace duskdpf
