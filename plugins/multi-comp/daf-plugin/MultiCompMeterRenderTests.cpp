// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Exercise the production ImGui drawing, including its translated panel origin.
// No window, audio device, or graphics driver is required.
#include "DearImGui/imgui.h"
#include "MultiCompVcaMeter.hpp"
#include "MultiCompVintageMeter.hpp"
#include <algorithm>
#include <cmath>
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
    using Meter = multicompp::ui_detail::VcaMeterRect;
    duskdaf::drawVuMeter(panel, dl, Meter::x0, Meter::y0, Meter::x1, Meter::y1, db, needle, config,
                         IM_COL32(40, 40, 42, 255), style);
    ImGui::Render();

    // The face inside the bezel, and the arc drawVuMeter derives from it.
    const float fx0 = Meter::x0 + 7, fy0 = Meter::y0 + 7, fx1 = Meter::x1 - 7, fy1 = Meter::y1 - 7;
    const float cx = 0.5f * (fx0 + fx1), pivotY = fy1 + style.pivotBelowFace;
    const float half = style.sweepHalfAngleDeg * 3.14159265f / 180.0f;
    const float radius = std::min(pivotY - fy0 - style.radiusTopInset,
                                  (fx1 - fx0) * 0.5f / std::max(std::sin(half), 0.2f) - 6.0f);
    const auto lo = panel.P(fx0, fy0), hi = panel.P(fx1, fy1);
    const auto white = ImGui::GetIO().Fonts->TexUvWhitePixel;
    int inkVertices = 0, clippedInk = 0, labelVertices = 0;
    float tipY = std::numeric_limits<float>::infinity();
    bool arcVisible = false;
    // Between the central -10 dB major and its next minor tick (1.6 degrees
    // right of it): only the continuous blue baseline covers this point.
    constexpr float probeAngle = 1.6f * 3.14159265f / 180.0f;
    const auto arcProbe = panel.P(cx + 0.90f * radius * std::sin(probeAngle),
                                  pivotY - 0.90f * radius * std::cos(probeAngle));
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
    // Bounds cover the full sweep, not only the stationary zero position: the
    // tip runs at 0.95 radius from straight up to the sweep's ends, and each
    // bound keeps 5 % of the radius beyond that.
    const float tipHighest = pivotY - 0.95f * radius;
    const float tipLowest = pivotY - 0.95f * radius * std::cos(half);
    const bool ok = inkVertices > 100 && labelVertices > 100 && clippedInk == 0
        && arcVisible && tipY > tipHighest - 0.05f * radius && tipY < tipLowest + 0.05f * radius
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

// The shared knob edits only when its coordinate moves. A dial whose travel is narrower than its host range (the
// Studio VCA and BUS faces keep wider host ranges for older sessions) used to write a value beyond its travel back
// as the travel's end on a plain press, or on a drag pushing it further out: here +10 dB on a dial that ends at
// -10 dB. A drag the other way brings it onto the travel.
struct CountingHost final : duskdaf::ParamHost
{
    int writes = 0;
    void beginEdit(uint32_t) override {}
    void endEdit(uint32_t) override {}
    void setParam(uint32_t, float) override { ++writes; }
};

float travelPosition(float value, uint32_t, void*) { return std::clamp((value + 50.0f) / 40.0f, 0.0f, 1.0f); }
float travelValue(float position, uint32_t, void*) { return -50.0f + 40.0f * position; }

bool knobWritesOnlyOnMovement()
{
    CountingHost host;
    duskdaf::DuskPanel panel;
    float value = 10.0f;
    const auto frame = [&](float x, float y, bool down) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("knob", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoSavedSettings);
        panel.begin(1.0f, ImVec2(0, 0), ImGui::GetFont(), &host);
        panel.knob("##travel", 0, -50.0f, 20.0f, 100.0f, 100.0f, 30.0f, value, -10.0f, false, true, "%.1f", " dB",
                   0, false, false, nullptr, false, 1.0f, 0.0f, nullptr, false, nullptr, false, 0.0f, 0.0f, false,
                   false, 9.5f, false, false, travelPosition, travelValue, nullptr, true);
        ImGui::End();
        ImGui::Render();
    };
    frame(100, 100, false);
    frame(100, 100, true);    // press
    frame(100, 100, true);    // hold still
    frame(100, 100, false);   // release
    const int pressWrites = host.writes;
    const float pressed = value;
    for (int i = 0; i < 60; ++i) frame(100, 100, false);   // past the double-click time
    frame(100, 100, true);
    frame(100, 80, true);     // drag up 20 px: further out
    frame(100, 80, false);
    const int outwardWrites = host.writes - pressWrites;
    const float outward = value;
    for (int i = 0; i < 60; ++i) frame(100, 100, false);
    frame(100, 100, true);
    frame(100, 120, true);    // drag down 20 px: onto the travel
    frame(100, 120, false);
    const bool ok = pressWrites == 0 && pressed == 10.0f && outwardWrites == 0 && outward == 10.0f
        && host.writes > 0 && value >= -50.0f && value < -10.0f;
    std::printf("%s knob press without movement: %d writes, value %.2f; outward drag %d writes, %.2f; inward drag "
                "%.2f\n", ok ? "PASS" : "FAIL", pressWrites, pressed, outwardWrites, outward, value);
    return ok;
}

// The same for a knob without a display transform whose host range is wider than its own (the BUS face keeps
// Threshold down to -30 dB for older sessions on a dial from -15 dB): -25 dB stays put through a press, a drag and
// a wheel step further out, and a drag inward starts from the dial's end.
bool knobBeyondItsRangeStaysPut()
{
    CountingHost host;
    duskdaf::DuskPanel panel;
    float value = -25.0f;
    const auto frame = [&](float x, float y, bool down, float wheel = 0.0f) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, down);
        if (wheel != 0.0f) io.AddMouseWheelEvent(0.0f, wheel);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("knob", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoSavedSettings);
        panel.begin(1.0f, ImVec2(0, 0), ImGui::GetFont(), &host);
        panel.knob("##wide", 1, -15.0f, 15.0f, 100.0f, 100.0f, 30.0f, value, 0.0f);
        ImGui::End();
        ImGui::Render();
    };
    const auto idle = [&]() { for (int i = 0; i < 60; ++i) frame(100, 100, false); };   // past double-click time
    frame(100, 100, false);
    frame(100, 100, true); frame(100, 100, true); frame(100, 100, false);       // press
    idle();
    frame(100, 100, true); frame(100, 120, true); frame(100, 120, false);       // drag down 20 px: further out
    idle();
    frame(100, 100, false, -1.0f); frame(100, 100, false);                      // wheel down: further out
    const int outwardWrites = host.writes;
    const float outward = value;
    idle();
    frame(100, 100, true); frame(100, 80, true); frame(100, 80, false);         // drag up 20 px: onto the dial
    const bool ok = outwardWrites == 0 && outward == -25.0f && host.writes > 0 && value > -15.0f && value <= 15.0f;
    std::printf("%s knob beyond its range: press, outward drag and wheel %d writes, %.2f; inward drag %.2f\n",
                ok ? "PASS" : "FAIL", outwardWrites, outward, value);
    return ok;
}

// Committing a value editor's seed text, untouched (Enter) or edited back to it (then a click away), is no edit: the
// seed is the value rounded for display, and parsing it back would move the value (7.2305 seeds "7.231"). Typing
// another value still commits.
bool valueEditEnterOnSeedIsNoEdit()
{
    duskdaf::DuskPanel panel;
    int commits = 0;
    float committed = 0.0f;
    const auto frame = [&](bool enter, const char* typed, bool clickAway = false, bool backspace = false) {
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Enter, enter);
        io.AddKeyEvent(ImGuiKey_Backspace, backspace);
        if (typed != nullptr) io.AddInputCharactersUTF8(typed);
        io.AddMousePosEvent(clickAway ? 3000.0f : 100.0f, clickAway ? 1900.0f : 100.0f);
        io.AddMouseButtonEvent(0, clickAway);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("edit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoSavedSettings);
        panel.begin(1.0f, ImVec2(0, 0), ImGui::GetFont(), nullptr);
        float out = 0.0f;
        if (panel.valueEdit("##seed", 100.0f, 100.0f, 30.0f, out)) { ++commits; committed = out; }
        ImGui::End();
        ImGui::Render();
    };
    panel.openValueEdit("##seed", 7.2305f);
    frame(false, nullptr); frame(false, nullptr); frame(true, nullptr); frame(false, nullptr);
    const bool seedNoEdit = commits == 0 && !panel.isEditingValue("##seed");
    panel.openValueEdit("##seed", 7.2305f);
    frame(false, nullptr); frame(false, nullptr); frame(false, "9");          // an edit: "9"
    frame(false, nullptr, false, true); frame(false, nullptr);                 // backspace: ""
    frame(false, "7.231");                                                     // back to the seed
    frame(false, nullptr, true); frame(false, nullptr); frame(false, nullptr); // click away
    const bool retypedNoEdit = commits == 0 && !panel.isEditingValue("##seed");
    panel.openValueEdit("##seed", 7.2305f);
    frame(false, nullptr); frame(false, nullptr); frame(false, "8"); frame(true, nullptr); frame(false, nullptr);
    const bool ok = seedNoEdit && retypedNoEdit && commits == 1 && committed == 8.0f;
    std::printf("%s value editor: Enter on the seed %s, the seed typed back and clicked away %s, typed 8 commits "
                "%d x %.4f\n", ok ? "PASS" : "FAIL", seedNoEdit ? "closes without an edit" : "EDITED",
                retypedNoEdit ? "closes without an edit" : "EDITED", commits, committed);
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
    ok = knobWritesOnlyOnMovement() && ok;
    ok = knobBeyondItsRangeStaysPut() && ok;
    ok = valueEditEnterOnSeedIsNoEdit() && ok;
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
