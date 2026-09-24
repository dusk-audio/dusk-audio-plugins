// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Exercise the production ImGui drawing, including its translated panel origin.
// No window, audio device, or graphics driver is required.
#include "DearImGui/imgui.h"
#include "MultiCompVcaMeter.hpp"
#include "MultiCompVintageMeter.hpp"
#include <cstdio>
#include <limits>

namespace
{
bool triangleContains(ImVec2 p, ImVec2 a, ImVec2 b, ImVec2 c)
{
    const auto side = [](ImVec2 u, ImVec2 v, ImVec2 w) {
        return (u.x - w.x) * (v.y - w.y) - (v.x - w.x) * (u.y - w.y);
    };
    const float ab = side(p, a, b), bc = side(p, b, c), ca = side(p, c, a);
    return !((ab < 0 || bc < 0 || ca < 0) && (ab > 0 || bc > 0 || ca > 0));
}

bool drawCase(float size, const char* source, float db)
{
    ImGui::NewFrame();
    duskdaf::DuskPanel panel;
    const ImVec2 origin(19.0f, 11.0f - 274.0f * size);
    panel.begin(size, origin, ImGui::GetFont(), nullptr);
    auto* dl = ImGui::GetBackgroundDrawList();
    // Keep strokes untextured so font-atlas vertices identify actual labels;
    // ImGui's optional line texture would otherwise inflate the glyph count.
    dl->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    const auto config = multicompp::ui_detail::vcaMeterScale(source);
    const auto style = multicompp::ui_detail::vcaMeterStyle();
    float needle = std::isfinite(db) ? duskdaf::vuDeflection(db, config) : 0.5f;
    duskdaf::drawVuMeter(panel, dl, 700, 364, 1046, 552, db, needle, config,
                         IM_COL32(40, 40, 42, 255), style);
    ImGui::Render();

    const auto lo = panel.P(707, 371), hi = panel.P(1039, 545);
    const auto white = ImGui::GetIO().Fonts->TexUvWhitePixel;
    int inkVertices = 0, clippedInk = 0, labelVertices = 0;
    float tipY = std::numeric_limits<float>::infinity();
    bool arcVisible = false;
    // Between the central -10 dB major and its next minor tick: only the
    // continuous blue baseline should cover this point (design coordinates).
    const auto arcProbe = panel.P(879.33f, 444.29f);
    for (const auto& cmd : dl->CmdBuffer)
    {
        for (unsigned i = 0; i < cmd.ElemCount; i += 3)
        {
            const ImDrawVert* v[3];
            bool blueTriangle = true;
            for (unsigned k = 0; k < 3; ++k)
            {
                v[k] = &dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + i + k]];
                const auto& vert = *v[k];
                const bool solid = vert.uv.x == white.x && vert.uv.y == white.y;
                blueTriangle &= solid && vert.col == style.ink;
                if (vert.col == style.ink)
                {
                    ++inkVertices;
                    if (!solid) ++labelVertices;
                    if (vert.pos.x < lo.x || vert.pos.x > hi.x
                        || vert.pos.y < lo.y || vert.pos.y > hi.y
                        || vert.pos.x < cmd.ClipRect.x || vert.pos.x > cmd.ClipRect.z
                        || vert.pos.y < cmd.ClipRect.y || vert.pos.y > cmd.ClipRect.w)
                        ++clippedInk;
                }
                if (vert.col == style.needle)
                    tipY = std::min(tipY, (vert.pos.y - origin.y) / size);
            }
            if (blueTriangle && triangleContains(arcProbe, v[0]->pos, v[1]->pos, v[2]->pos))
                arcVisible = true;
        }
    }
    // Bounds cover the full sweep, not only the stationary zero position.
    const bool ok = inkVertices > 100 && labelVertices > 100 && clippedInk == 0
        && arcVisible && tipY > 420.0f && tipY < 480.0f
        && std::isfinite(needle) && needle >= 0 && needle <= 1;
    std::printf("%s scale=%.2f source=%s db=%g ink=%d glyph=%d clipped=%d arc=%d tipY=%.3f\n",
                ok ? "PASS" : "FAIL", size, source, db, inkVertices, labelVertices,
                clippedInk, arcVisible, tipY);
    return ok;
}
bool drawVintageCase(float size, bool opto, float db, const char* caption, bool powered)
{
    ImGui::NewFrame();
    duskdaf::DuskPanel panel;
    const ImVec2 origin(19, 11 - 274 * size);
    panel.begin(size, origin, ImGui::GetFont(), nullptr);
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    const float x = opto ? 441 : 743, y = opto ? 405 : 431;
    const float w = opto ? 238 : 234, h = 141;
    multicompp::ui_detail::drawVintageMeter(panel, dl, x, y, w, h, db, opto, caption, powered);
    ImGui::Render();
    const auto style = multicompp::ui_detail::vintageVuStyle(opto);
    const auto white = ImGui::GetIO().Fonts->TexUvWhitePixel;
    int glyphs = 0, clipped = 0, redGlyphs = 0, captionGlyphs = 0;
    float tipY = std::numeric_limits<float>::infinity();
    for (const auto& cmd : dl->CmdBuffer)
        for (unsigned i = 0; i < cmd.ElemCount; ++i)
        {
            const auto& v = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + i]];
            const bool text = v.uv.x != white.x || v.uv.y != white.y;
            if (text && (v.col == style.ink || v.col == style.hot || v.col == style.sublabelColor))
            {
                ++glyphs;
                if (v.col == style.hot) ++redGlyphs;
                if (v.col == style.sublabelColor) ++captionGlyphs;
                if (v.pos.x < cmd.ClipRect.x || v.pos.x > cmd.ClipRect.z
                    || v.pos.y < cmd.ClipRect.y || v.pos.y > cmd.ClipRect.w) ++clipped;
            }
            if (v.col == style.needle) tipY = std::min(tipY, (v.pos.y - origin.y) / size);
        }
    const bool ok = glyphs >= 120 && redGlyphs >= 24 && captionGlyphs >= 18 && clipped == 0
        && std::isfinite(tipY) && tipY > y + 10 && tipY < y + h - 10;
    std::printf("%s vintage=%s caption=%s powered=%d scale=%.2f db=%g glyph=%d red=%d captionGlyph=%d clipped=%d tipY=%.3f\n",
                ok ? "PASS" : "FAIL", opto ? "Opto" : "FET", caption, powered, size, db, glyphs, redGlyphs, captionGlyphs, clipped, tipY);
    return ok;
}
bool drawNeedlePolicy()
{
    ImGui::NewFrame();
    duskdaf::DuskPanel panel;
    panel.begin(1, ImVec2(0,0), ImGui::GetFont(), nullptr);
    auto* dl = ImGui::GetBackgroundDrawList();
    duskdaf::VuScaleConfig cfg; cfg.minDb=0; cfg.maxDb=20;
    cfg.deflection=[](float db, const duskdaf::VuScaleConfig&) { return std::clamp(db/20,0.0f,1.1f); };
    duskdaf::VuStyle bus; bus.smoothNeedle=false; bus.maximumNeedleDeflection=1.1f;
    float immediate=0, overtravel=0, sibling=0;
    duskdaf::drawVuMeter(panel,dl,0,0,250,150,12,immediate,cfg,0,bus);
    duskdaf::drawVuMeter(panel,dl,0,0,250,150,22,overtravel,cfg,0,bus);
    duskdaf::drawVuMeter(panel,dl,0,0,250,150,12,sibling,cfg,0);
    ImGui::Render();
    const bool ok=std::abs(immediate-.6f)<1e-6f && std::abs(overtravel-1.1f)<1e-6f
        && std::abs(sibling-.29194973f)<1e-6f;
    std::printf("%s needle policy BUS %.8f overtravel %.8f default %.8f\n",ok?"PASS":"FAIL",immediate,overtravel,sibling);
    return ok;
}
} // namespace

int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(3600, 2000);
    io.DeltaTime = 1.0f / 60.0f;
    duskdaf::loadCrispFont(24.0f);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    bool ok = drawNeedlePolicy();
    for (float size : {1.0f, 1.25f, 1.8f, 2.0f, 3.0f})
        for (const char* source : {"INPUT", "OUTPUT", "GAIN CHANGE"})
            for (float db : {-80.0f, -40.0f, -20.0f, 0.0f, 20.0f, 40.0f,
                             std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity()})
                ok = drawCase(size, source, db) && ok;
    for (float size : {1.0f, 1.25f, 1.8f, 2.0f, 3.0f})
        for (bool opto : {true, false})
            for (float db : {-80.0f, -20.0f, -10.0f, -3.0f, 0.0f, 3.0f,
                             std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity()})
            {
                ok = drawVintageCase(size, opto, db, "GAIN REDUCTION", true) && ok;
                ok = drawVintageCase(size, opto, db, opto ? "OUTPUT +10" : "OUTPUT +8", true) && ok;
                ok = drawVintageCase(size, opto, db, "OUTPUT +4", true) && ok;
                if (!opto) ok = drawVintageCase(size, opto, db, "OFF", false) && ok;
            }
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
