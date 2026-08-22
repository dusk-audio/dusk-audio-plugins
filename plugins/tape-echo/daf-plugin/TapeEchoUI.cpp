// TapeEchoUI.cpp — Dear ImGui UI for Tape Echo, modeled on the original
// classic tape-echo hardware front panel: black chassis, green MODE SELECTOR block with
// numbered dial, recessed metal-trimmed control panel, chrome knobs with
// triangle markers, VU meter and peak LED. Hardware I/O (jacks, switches
// for mic routing) is intentionally not reproduced.
// All rendering is custom ImDrawList work in a 900x340 design space.

#include "DafUI.hpp"
#include "TapeEchoAccess.hpp"
#include "TapeEchoDSP.hpp"
#include "TapeEchoParams.hpp"
#include "TapeEchoVersion.hpp"
#include "DuskImGuiFont.hpp"      // shared crisp-bold loader (candidate search + DPI)
#include "DuskImGuiTextInput.hpp" // Windows host focus while typing values
#include "DuskImGuiWidgets.hpp"   // shared DuskPanel: chrome knob, LED, text, value bubble
#include "DuskSupportersOverlay.hpp" // shared DAF Patreon "Special Thanks" overlay
#include "util/CrashLog.hpp"
#include "TapeEchoFontRegular.inc"
#include "TapeEchoFontSemiBold.inc"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

START_NAMESPACE_DAF

namespace
{
    constexpr float kDesignW = 900.0f;
    constexpr float kDesignH = 340.0f;

    // Restrained hardware palette: graphite chassis, aged olive paint, warm
    // nickel and a single red status color. Every highlight assumes a top-left
    // light source so separate components still read as one manufactured object.
    constexpr ImU32 kColHeader    = IM_COL32(14, 14, 15, 255);
    constexpr ImU32 kColGreen     = IM_COL32(76, 103, 48, 255);
    constexpr ImU32 kColGreenDk   = IM_COL32(39, 57, 24, 255);
    constexpr ImU32 kColWhite     = IM_COL32(241, 238, 226, 255);
    constexpr ImU32 kColWhiteDim  = IM_COL32(196, 193, 182, 255);
    constexpr ImU32 kColRailInk   = IM_COL32(16, 16, 15, 255);
    constexpr ImU32 kColLedOn     = IM_COL32(255, 60, 40, 255);
    constexpr ImU32 kColLedGlow   = IM_COL32(255, 70, 45, 80);

    constexpr float kPi = 3.14159265358979f;

}

class TapeEchoUI : public UI, public duskdaf::ParamHost
{
public:
    //--- duskdaf::ParamHost: forward the shared widgets' edits to the host ------
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override
    {
        if (idx >= kParamCount || !std::isfinite(v))
            return;
        v = normalizeParamValue(idx, v);
        storeParamLocally(idx, v);
        setParameterValue(idx, v);
    }

    // Opt-in field diagnostic: set DUSK_GL_DEBUG=1 in the host's environment to
    // drop one line per editor open describing the GL context and what the font
    // atlas settled on. Silent otherwise, so nothing is written on a user's
    // machine unless they are helping debug a rendering report.
    static void logGlDiagnostics(GLint maxTextureSize, const duskdaf::AtlasFitResult& fit)
    {
        if (std::getenv("DUSK_GL_DEBUG") == nullptr)
            return;

        const char* const vendor   = (const char*)glGetString(GL_VENDOR);
        const char* const renderer = (const char*)glGetString(GL_RENDERER);
        const char* const version  = (const char*)glGetString(GL_VERSION);

        // Try each plausible writable root rather than one per platform: a host
        // can run without HOME set, and a log nobody can find is a log nobody reads.
        const char* const roots[] = { std::getenv("LOCALAPPDATA"), std::getenv("HOME"),
                                      std::getenv("USERPROFILE"), std::getenv("TMPDIR"),
                                      std::getenv("TEMP") };
        for (const char* const root : roots)
        {
            if (root == nullptr || root[0] == '\0')
                continue;

            // Keep trying: a root can exist in the environment and still be
            // unwritable for this process, and stopping at the first candidate
            // would then lose the log for no reason.
            const std::string path = std::string(root) + "/dusk-gl-debug.log";
            FILE* const f = std::fopen(path.c_str(), "a");
            if (f == nullptr)
                continue;

            std::fprintf(f,
                "tape-echo-2 vendor=%s renderer=%s version=%s max_texture=%d "
                "atlas=%dx%d oversample=%d dropped=%d attempts=%d fits=%d\n",
                vendor != nullptr ? vendor : "?",
                renderer != nullptr ? renderer : "?",
                version != nullptr ? version : "?",
                (int)maxTextureSize, fit.atlasWidth, fit.atlasHeight,
                fit.oversample, fit.droppedSizes, fit.attempts, fit.fits ? 1 : 0);
            std::fclose(f);
            return;
        }
    }

    TapeEchoUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        supportersOverlay.setActionLink("Open crash log folder",
                                        [] { DuskCrashLog::openLogFolder(); });
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kTeParams[i].def;
        setGeometryConstraints((uint32_t)kDesignW, (uint32_t)kDesignH, true);

        // Barlow Condensed is bundled under the SIL OFL. Unlike the previous
        // system-font search this produces identical metrics on every OS. Two
        // weights preserve a real hierarchy: semibold for controls/product marks,
        // regular for values, version copy and secondary annotations.
        // DAF coordinates are physical pixels. Cover the full resize range in
        // native atlas sizes instead of baking only the launch DPI: 7..24 px
        // design text remains near-native from the 1x minimum through 3x.
        static constexpr float kLabelSizes[] =
            { 7.0f, 9.0f, 11.0f, 14.0f, 18.0f, 24.0f, 28.0f, 32.0f, 48.0f, 72.0f };
        static constexpr float kRegularSizes[] =
            { 7.0f, 9.0f, 11.0f, 14.0f, 18.0f, 24.0f, 32.0f, 48.0f };
        // Both faces share one ImGui atlas, so the size that matters is the one
        // built from every bake together. Software OpenGL (a virtual machine with
        // no 3D acceleration, a Remote Desktop session) caps textures far below
        // what those bakes need at 2x2 oversampling, the oversized upload fails
        // silently, and every label then paints as a solid rectangle in the text
        // colour while the knobs and meters still look right. Hand the loader the
        // real limit and let it measure the built atlas and shrink until it fits,
        // rather than guessing a face count that happens to work on one machine.
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

        // DUSK_GL_MAX_TEXTURE reproduces a small-texture context on hardware that
        // has no such limit, which is the only practical way to exercise the
        // shrink path without the machine that reported the bug.
        if (const char* const forced = std::getenv("DUSK_GL_MAX_TEXTURE"))
        {
            const int v = std::atoi(forced);
            if (v > 0)
                maxTextureSize = (GLint)v;
        }

        const duskdaf::EmbeddedFontRequest fontRequests[2] = {
            { kTeFontSemiBold, kTeFontSemiBold_len, kLabelSizes,
              (int)(sizeof(kLabelSizes) / sizeof(kLabelSizes[0])) },
            { kTeFontRegular, kTeFontRegular_len, kRegularSizes,
              (int)(sizeof(kRegularSizes) / sizeof(kRegularSizes[0])) },
        };
        duskdaf::CrispFontSet fontSets[2];
        duskdaf::AtlasFitResult fit;
        duskdaf::loadEmbeddedCrispFontSets(
            fontRequests, 2, fontSets, 1.0f, (int)maxTextureSize, &fit);

        const float dpi = getScaleFactor();
        labelFonts   = fontSets[0];
        regularFonts = fontSets[1];

        logGlDiagnostics(maxTextureSize, fit);
        labelFont = labelFonts.pick(12.5f * dpi);
        panel.setFontSet(labelFonts);

        scanUserPresets();
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount)
            return;
        if (!std::isfinite(value))
            return;
        value = normalizeParamValue(index, value);
        storeParamLocally(index, value);
        // Re-derive the active preset from the current values. Preset identity is
        // UI-only state (not a DAF parameter), so it is lost across a project
        // reload even though the host restores every parameter value; matching
        // the restored values back to a preset recovers the selection. It also
        // survives a POWER/bypass toggle (no sound parameter changes) and clears
        // once an edit diverges from every preset. Gated on the preset params so
        // the per-frame meter output never triggers a scan.
        if (teIsPresetParam(index))
            syncPresetSelection();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = index < (uint32_t)kNumFactoryPresets
                      ? (int)index
                      : -1;
        currentUserName.clear();
        currentUserPath.clear();
        if (currentPreset < 0)
            return;
        // Re-derive the local cache from the preset. A host-driven program
        // change does NOT deliver parameterChanged() for the values it moved:
        // the VST3 wrapper calls loadProgram(), refreshes its own parameter
        // cache, and then flags ONLY the program index for the UI
        // (DafPluginVST3.cpp, kVst3InternalParameterProgram). Without this
        // every knob would keep drawing its pre-program value, and
        // legacySyncDivisionOverride would keep the stale ownership the plugin
        // has already changed -- so the head strip would name a division the
        // DSP is not playing.
        //
        // Store only. The plugin applied these values itself when the host
        // called loadProgram, so writing them back would be redundant traffic
        // and, for the compatibility pair, would re-run the ownership latch
        // from the UI side. storeParamLocally keeps that latch in step exactly
        // as the plugin's own loadProgram does, because both walk the pair in
        // the same order.
        forEachPresetParam(currentPreset,
                           [this](uint32_t param, float value)
                           { storeParamLocally(param, normalizeParamValue(param, value)); });
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s   = std::min(winW / kDesignW, winH / kDesignH);
        org = ImVec2(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));

        // Bind the shared widget panel to this frame's scale/origin/font. `this`
        // is the ParamHost the knob/LED gestures call back into.
        panel.begin(s, org, labelFont, this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("TapeEcho", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 8, 8, 255));
        drawChassis(dl);

        // Treat the supporters panel as a modal: background controls remain
        // visible beneath its scrim but cannot receive click-through gestures.
        // Snapshot before drawHeader(), where a title click can open it.
        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);
        drawMeterBlock(dl);
        drawInputRow(dl);
        drawModeSelector(dl);
        drawControlPanel(dl);
        if (values[kParamBypass] >= 0.5f)
        {
            dl->AddRectFilled(P(2, 50), P(kDesignW - 2, 289),
                              IM_COL32(0, 0, 0, 92));
            text(dl, 450, 163, 18.0f, IM_COL32(220, 216, 204, 175),
                 "STANDBY", 0, true);
        }
        drawUtilityRail(dl);
        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdaf::drawSupportersOverlay(
                panel, dl, kDesignW, kDesignH, showSupporters, "Tape Echo 2",
                TE2_VERSION_STRING, &supportersOverlay);

        // Own resize grip, submitted LAST so it wins ImGui's hover race and
        // paints over everything. AUv2 hosts (Logic) never provide a window
        // grip of their own; on VST3/CLAP the host's grip stays available and
        // this is simply a second way to do the same thing.
        const duskdaf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH);

        ImGui::End();
        ImGui::PopStyleVar(2);
        textInputFocus.update(*this);

        // Cursor feedback. The DAF-Widgets ImGui backend never forwards
        // ImGui::SetMouseCursor() to the window, so drive DGL's cursor directly.
        // Edge-triggered: setCursor() is a window-level call, not a per-frame one.
        if (grip.hot != gripCursorSet)
        {
            gripCursorSet = grip.hot;
            setCursor(gripCursorSet ? DGL_NAMESPACE::kMouseCursorUpLeftDownRight
                                    : DGL_NAMESPACE::kMouseCursorArrow);
        }

        // Applied after End() so a host that services the resize synchronously
        // cannot re-enter the UI in the middle of this window's submission.
        if (grip.resized)
            setSize(grip.width, grip.height);
    }

private:
    //--- helpers -----------------------------------------------------------------

    // Keep this widget the same size as the window it draws into.
    //
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }

    static ImU32 fade(ImU32 c, float amount)
    {
        amount = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
        const ImU32 a = (c >> 24) & 0xffu;
        return (c & 0x00ffffffu) | ((ImU32)(a * amount + 0.5f) << 24);
    }

    static ImU32 blend(ImU32 a, ImU32 b, float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const auto ch = [t](ImU32 x, ImU32 y, int shift)
        {
            const float xv = (float)((x >> shift) & 0xffu);
            const float yv = (float)((y >> shift) & 0xffu);
            return (ImU32)(xv + (yv - xv) * t + 0.5f) << shift;
        };
        return ch(a, b, 0) | ch(a, b, 8) | ch(a, b, 16) | ch(a, b, 24);
    }

    void text(ImDrawList* dl, float x, float y, float size, ImU32 col,
              const char* txt, int align /* -1 left, 0 center, 1 right */,
              bool bold = false) const
    {
        panel.text(dl, x, y, size, col, txt, align, bold);
    }

    void regularText(ImDrawList* dl, float x, float y, float size, ImU32 col,
                     const char* txt, int align = -1) const
    {
        const float px = size * s;
        ImFont* font = regularFonts.pick(px);
        if (font == nullptr)
        {
            text(dl, x, y, size, col, txt, align);
            return;
        }
        const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, txt);
        ImVec2 pos = P(x, y);
        if (align == 0) pos.x -= 0.5f * ts.x;
        if (align == 1) pos.x -= ts.x;
        pos.x = std::floor(pos.x + 0.5f);
        pos.y = std::floor(pos.y + 0.5f);
        dl->AddText(font, px, pos, col, txt);
    }

    void led(ImDrawList* dl, float x, float y, bool on, float r = 5.0f) const
    {
        panel.led(dl, x, y, on, r);
    }

    static float knobAngle(float t) { return duskdaf::DuskPanel::knobAngle(t); }

    void brushedRect(ImDrawList* dl, float x0, float y0, float x1, float y1,
                     ImU32 top, ImU32 bottom, float rounding = 0.0f) const
    {
        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y1),
                                    top, top, bottom, bottom);
        // Low-contrast deterministic horizontal brushing. Geometry instead of a
        // bitmap keeps the surface sharp at every supported UI scale.
        for (int y = (int)y0 + 2; y < (int)y1 - 1; y += 3)
        {
            const int n = (y * 37 + 11) & 31;
            const float inset = (float)n * 0.31f;
            dl->AddLine(P(x0 + inset, (float)y),
                        P(x1 - 5.0f - 0.4f * inset, (float)y),
                        (y & 1) ? IM_COL32(255, 255, 255, 10)
                                : IM_COL32(0, 0, 0, 12),
                        0.55f * s);
        }
        if (rounding > 0.0f)
            dl->AddRect(P(x0, y0), P(x1, y1), IM_COL32(255, 255, 255, 18),
                        rounding * s, 0, 0.8f * s);
    }

    void drawChassis(ImDrawList* dl) const
    {
        dl->AddRectFilledMultiColor(
            P(0, 0), P(kDesignW, kDesignH),
            IM_COL32(34, 34, 35, 255), IM_COL32(30, 30, 31, 255),
            IM_COL32(20, 20, 21, 255), IM_COL32(22, 22, 23, 255));
        for (int y = 52; y < 289; y += 4)
            dl->AddLine(P(0, (float)y), P(kDesignW, (float)y),
                        (y & 4) ? IM_COL32(255, 255, 255, 5)
                                : IM_COL32(0, 0, 0, 8),
                        0.6f * s);
        // Deep outer lip and a fine top-left catch-light sell the chassis as a
        // single object instead of a collection of floating rectangles.
        dl->AddRect(P(1, 1), P(kDesignW - 1, kDesignH - 1),
                    IM_COL32(7, 7, 8, 255), 7.0f * s, 0, 2.0f * s);
        dl->AddLine(P(8, 4), P(kDesignW - 8, 4),
                    IM_COL32(225, 222, 211, 90), 1.0f * s);
    }

    void drawKnobBody(ImDrawList* dl, float cx, float cy, float radius,
                      float t, bool enabled = true, bool ticks = true) const
    {
        const ImVec2 c = P(cx, cy);
        const float R = radius * s;
        const float dim = enabled ? 1.0f : 0.38f;

        if (ticks)
            for (int i = 0; i <= 10; ++i)
            {
                // The fixed triangle replaces the exact 12-o'clock scale mark.
                if (i == 5)
                    continue;
                const float a = knobAngle((float)i / 10.0f);
                const ImVec2 d(std::sin(a), -std::cos(a));
                const float inner = R + 3.0f * s;
                const float outer = R + 6.5f * s;
                dl->AddLine(ImVec2(c.x + d.x * inner, c.y + d.y * inner),
                            ImVec2(c.x + d.x * outer, c.y + d.y * outer),
                            fade(kColWhiteDim, dim), 1.1f * s);
            }

        // Contact shadow, seating washer and ridged skirt.
        dl->AddCircleFilled(P(cx + 1.3f, cy + 3.2f), R * 1.08f,
                            IM_COL32(0, 0, 0, (int)(110.0f * dim)), 56);
        dl->AddCircleFilled(c, R * 1.03f, fade(IM_COL32(16, 16, 17, 255), dim), 56);
        dl->AddCircleFilled(P(cx, cy - 0.5f), R, fade(IM_COL32(88, 88, 89, 255), dim), 56);
        for (int i = 0; i < 36; ++i)
        {
            const float a = 2.0f * kPi * (float)i / 36.0f;
            const ImVec2 d(std::sin(a), -std::cos(a));
            const ImU32 ridge = (i % 3 == 0)
                              ? IM_COL32(208, 207, 202, (int)(90.0f * dim))
                              : IM_COL32(22, 22, 23, (int)(130.0f * dim));
            dl->AddLine(ImVec2(c.x + d.x * R * 0.82f, c.y + d.y * R * 0.82f),
                        ImVec2(c.x + d.x * R * 0.98f, c.y + d.y * R * 0.98f),
                        ridge, 1.0f * s);
        }

        // Warm nickel cap. Horizontal strips are analytically clipped to the
        // circle, producing a smooth top-to-bottom metal response without the
        // concentric "bullseye" look of stacked circles.
        const float capR = R * 0.73f;
        dl->AddCircleFilled(P(cx, cy + 0.8f), capR * 1.04f,
                            fade(IM_COL32(24, 24, 25, 255), dim), 52);
        dl->AddCircleFilled(c, capR, fade(IM_COL32(100, 101, 102, 255), dim), 52);
        // One strip per physical pixel avoids visible scanlines at either 1x or
        // Retina scale while retaining the deterministic gradient.
        const int kMetalBands = std::max(16, (int)(2.0f * capR + 0.5f));
        for (int i = 0; i <= kMetalBands; ++i)
        {
            const float u = -0.96f + 1.92f * (float)i / (float)kMetalBands;
            const float half = std::sqrt(std::max(0.0f, 1.0f - u * u)) * capR;
            const float shade = 0.5f * (u + 1.0f);
            const ImU32 col = blend(IM_COL32(218, 217, 210, 255),
                                    IM_COL32(91, 92, 94, 255), shade);
            const float yy = c.y + u * capR;
            dl->AddLine(ImVec2(c.x - half, yy), ImVec2(c.x + half, yy),
                        fade(col, dim), 1.15f);
        }
        dl->AddCircle(c, capR, fade(IM_COL32(17, 17, 18, 255), dim),
                      52, 1.1f * s);
        dl->AddCircleFilled(P(cx - radius * 0.20f, cy - radius * 0.24f),
                            capR * 0.17f,
                            IM_COL32(255, 255, 250, (int)(22.0f * dim)), 24);

        const float a = knobAngle(t);
        const ImVec2 d(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + d.x * capR * 0.10f + 0.8f * s,
                           c.y + d.y * capR * 0.10f + 1.0f * s),
                    ImVec2(c.x + d.x * capR * 0.91f + 0.8f * s,
                           c.y + d.y * capR * 0.91f + 1.0f * s),
                    IM_COL32(255, 255, 255, (int)(30.0f * dim)), 3.1f * s);
        dl->AddLine(ImVec2(c.x + d.x * capR * 0.10f, c.y + d.y * capR * 0.10f),
                    ImVec2(c.x + d.x * capR * 0.91f, c.y + d.y * capR * 0.91f),
                    fade(IM_COL32(18, 18, 19, 255), dim), 2.8f * s);
    }

    // Hardware-specific visual body with shared DuskPanel gestures/readouts.
    // Double-click reset and a right-click Reset/Type value menu follow common
    // commercial plugin conventions without changing the rest of the Dusk fleet.
    void knob(const char* id, uint32_t param, float minV, float maxV,
              float cx, float cy, float radius, bool stepped = false,
              bool panelTicks = true, const char* fmt = nullptr,
              const char* suffix = "", float dispMul = 1.0f, float dispAdd = 0.0f,
              bool persistent = false, bool enabled = true,
              const char* overrideText = nullptr)
    {
        float shownValue = values[param];
        const float shownDefault = kTeParams[param].def;

        const float range = maxV - minV;
        float t = range > 0.0f ? (shownValue - minV) / range : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        drawKnobBody(ImGui::GetWindowDrawList(), cx, cy, radius, t, enabled, panelTicks);

        const char* bubbleText = overrideText;
        if (!enabled && bubbleText == nullptr)
            bubbleText = "INACTIVE";
        panel.knob(id, param, minV, maxV, cx, cy, radius,
                   shownValue, shownDefault, stepped, panelTicks,
                   fmt != nullptr ? fmt : (stepped ? "%.0f" : "%.2f"), suffix,
                   /*faceColor*/ 0, /*bodyless*/ true, persistent,
                   /*tooltip*/ nullptr, /*rightClickReset*/ false,
                   dispMul, dispAdd, /*name*/ nullptr,
                   /*contextMenu*/ true, bubbleText,
                   /*hasExternalReadout*/ true,
                   /*dispMin*/ 0.0f, /*dispMax*/ 0.0f,
                   /*nameOnHover*/ false,
                   /*doubleClickReset*/ true,
                   /*persistentTextSize*/ 11.5f);
    }

    void knobLabel(ImDrawList* dl, float cx, float knobCy, float knobRadius,
                   const char* l1, const char* l2 = nullptr,
                   bool enabled = true) const
    {
        const ImU32 ink = enabled ? kColWhite : IM_COL32(148, 146, 139, 255);
        constexpr float kLabelSize = 12.0f;
        constexpr float kLineStep = 12.5f;
        constexpr float kTriangleH = 6.0f;
        // Replace the 12-o'clock tick and meet the knob silhouette, matching
        // TapeMachine 2. Deriving the stack from each knob's geometry keeps
        // every label/marker gap uniform across the two panels.
        const float tipY = knobCy - knobRadius;
        const float triangleTopY = tipY - kTriangleH;
        const float topY = triangleTopY - (l2 != nullptr ? 27.0f : 15.0f);
        text(dl, cx, topY, kLabelSize, ink, l1, 0, true);
        if (l2 != nullptr)
            text(dl, cx, topY + kLineStep, kLabelSize, ink, l2, 0, true);
        dl->AddTriangleFilled(P(cx - 4.0f, triangleTopY),
                              P(cx + 4.0f, triangleTopY),
                              P(cx, tipY), ink);
    }

    // Small chevron button ("<" / ">") for stepping the preset combo. Styled to
    // the dark header instead of the silver TapeMachine treatment: it has to read
    // as part of the combo it flanks, so fill/hover match ImGuiCol_FrameBg.
    bool chevron(ImDrawList* dl, const char* id, float cx, float cy,
                 float halfH, bool left)
    {
        const ImVec2 b0 = P(cx - 9.5f, cy - halfH);
        const ImVec2 b1 = P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(54, 54, 58, 255)
                                      : IM_COL32(38, 38, 41, 255), 2.0f * s);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * s, 0, 1.0f * s);
        const ImVec2 c = P(cx, cy);
        const float  d = 4.3f * s;
        const ImU32 ink = hov ? kColWhite : kColWhiteDim;
        if (left)
            dl->AddTriangleFilled(ImVec2(c.x + d * 0.5f, c.y - d),
                                  ImVec2(c.x + d * 0.5f, c.y + d),
                                  ImVec2(c.x - d * 0.7f, c.y), ink);
        else
            dl->AddTriangleFilled(ImVec2(c.x - d * 0.5f, c.y - d),
                                  ImVec2(c.x - d * 0.5f, c.y + d),
                                  ImVec2(c.x + d * 0.7f, c.y), ink);
        return ImGui::IsItemClicked();
    }

    // Small header text button (INIT / SAVE), styled to match the chevrons that
    // flank the preset combo rather than TapeMachine's silver caps: this header
    // is a black chassis strip.
    bool textButton(ImDrawList* dl, const char* id, float x0, float y0,
                    float x1, float y1, const char* label)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? IM_COL32(54, 54, 58, 255)
                                      : IM_COL32(38, 38, 41, 255), 2.0f * s);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * s, 0, 1.0f * s);
        // text() takes the top edge, so centre it explicitly rather than with a
        // fixed fraction of the height (the band is 30 px tall now, not 20).
        constexpr float kTxt = 11.0f;
        text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - kTxt), kTxt,
             hov ? kColWhite : kColWhiteDim, label, 0, true);
        return ImGui::IsItemClicked();
    }

    //--- sections ---------------------------------------------------------------------
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, 48), kColHeader);
        brushedRect(dl, 0, 0, kDesignW, 4,
                    IM_COL32(205, 203, 197, 255),
                    IM_COL32(91, 91, 91, 255));
        dl->AddLine(P(0, 48), P(kDesignW, 48),
                    IM_COL32(91, 90, 88, 255), 1.2f * s);
        dl->AddLine(P(0, 49), P(kDesignW, 49),
                    IM_COL32(0, 0, 0, 160), 1.0f * s);

        constexpr float kHdrCy = 24.0f;
        constexpr float kPlateX0 = 28.0f, kPlateX1 = 318.0f;
        constexpr float kPlateY0 = 8.0f, kPlateY1 = 40.0f;
        dl->AddRectFilledMultiColor(
            P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
            IM_COL32(37, 37, 38, 255), IM_COL32(29, 29, 30, 255),
            IM_COL32(16, 16, 17, 255), IM_COL32(19, 19, 20, 255));
        dl->AddRect(P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
                    IM_COL32(185, 184, 180, 220), 3.5f * s, 0, 1.2f * s);
        dl->AddLine(P(40, 37), P(308, 37),
                    IM_COL32(116, 145, 75, 210), 1.2f * s);

        // Product, version and model share one baseline; the green rule ties the
        // complete identity block together across the width of the nameplate.
        text(dl, 42, 10.0f, 24.0f, kColWhite, "TAPE ECHO", -1, true);
        text(dl, 202, 14.0f, 20.0f, kColWhite, "TE-2", 0, true);
        regularText(dl, 306, 20.0f, 14.0f, kColWhiteDim,
                    "v" TE2_VERSION_STRING, 1);

        text(dl, 813, 12.0f, 22.0f, kColWhite, "DUSK AUDIO", 0, true);

        // Match the other DAF plugins: clicking the title nameplate opens the
        // generated Patreon "Special Thanks" panel.
        ImGui::SetCursorScreenPos(P(kPlateX0, kPlateY0));
        if (ImGui::InvisibleButton(
                "##titlecredits",
                ImVec2((kPlateX1 - kPlateX0) * s, (kPlateY1 - kPlateY0) * s)))
        {
            supportersOverlay.resetInteraction();
            showSupporters = true;
        }

        // Preset browser: < [combo] > INIT SAVE, all centred on the nameplate's
        // centre line. The band is 26 px, not the plate's full 30: these are solid
        // edge-to-edge fills where the plate is only a hairline outline, so at
        // equal height they read heavier than the plate they sit beside.
        // Chevron half-width and arrow size were scaled by the same 26/30.
        // Horizontal rhythm: 4 px inside the < combo > group, 8 px before INIT
        // (group break), 6 px between INIT and SAVE.
        constexpr float kBandY0 = kHdrCy - 12.5f, kBandY1 = kHdrCy + 12.5f;
        constexpr float kBandH  = kBandY1 - kBandY0;
        if (chevron(dl, "##presetprev", 350.5f, kHdrCy, 0.5f * kBandH, true))
            stepPreset(-1);
        if (chevron(dl, "##presetnext", 569.5f, kHdrCy, 0.5f * kBandH, false))
            stepPreset(1);

        ImGui::SetCursorScreenPos(P(364, kBandY0));
        ImGui::SetNextItemWidth(192.0f * s);
        // Lock the combo frame to the same 26 px height as the hand-drawn controls
        // (ImGui's default is font-size driven and would drift). Vertical padding
        // is the leftover half-height, which also keeps the preview text centred.
        // Use a dedicated native-size atlas face for both the preview and rows;
        // this avoids scaling blur across the supported editor sizes.
        // Presets are first-class control labels, so use the same semibold face
        // as the rest of the panel and a larger 14 px design size. The font
        // atlas includes the 28 px Retina counterpart above to keep it crisp.
        ImFont* presetFont = labelFonts.pick(14.0f * s);
        if (presetFont != nullptr)
            ImGui::PushFont(presetFont);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(6.0f * s,
                   std::max(0.0f, 0.5f * (kBandH * s - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(110, 45, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(132, 57, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(92, 39, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kColGreenDk);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(122, 160, 84, 255));
        const char* preview = (currentPreset >= 0 && currentPreset < kNumFactoryPresets)
                                  ? kFactoryPresets[currentPreset].name
                                  : (!currentUserName.empty() ? currentUserName.c_str()
                                                              : "Presets...");
        // BeginCombo otherwise caps its popup at eight entries. An explicit
        // identity constraint makes it auto-fit all 13 factory presets, so the
        // complete list is visible without a scrollbar.
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginCombo("##presets", preview))
        {
            for (int i = 0; i < kNumFactoryPresets; ++i)
            {
                if (ImGui::Selectable(kFactoryPresets[i].name, i == currentPreset))
                {
                    applyPreset(i);
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!userPresets.empty())
            {
                ImGui::SeparatorText("User");
                // Index-scoped IDs: two rows can carry the same display name (a
                // hand-written file, or one with no name= line falling back to a
                // stem another file already uses), and ImGui would then give both
                // rows one shared ID. The label stays the name.
                for (size_t i = 0; i < userPresets.size(); ++i)
                {
                    const UserPreset& up = userPresets[i];
                    ImGui::PushID((int)i);
                    if (ImGui::Selectable(up.name.c_str(),
                                          currentPreset < 0 && up.path == currentUserPath))
                    {
                        loadUserPreset(up.path, up.name);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(12);
        ImGui::PopStyleVar();
        if (presetFont != nullptr)
            ImGui::PopFont();

        // INIT (back to factory defaults) and SAVE (name + write a user preset),
        // on the same band as the combo, clear of the top-right brand text.
        if (textButton(dl, "##init", 587, kBandY0, 635, kBandY1, "INIT"))
            initDefaults();
        if (textButton(dl, "##save", 642, kBandY0, 690, kBandY1, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Preset");
        }
        drawSaveModal();
    }

    // Name-entry modal for SAVE. Dark styling to match this chassis; Enter or the
    // Save button commits, Escape / Cancel dismisses.
    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(40, 40, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 50, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(56, 56, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, kColGreenDk);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColGreen);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 58, 27, 255));
        if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool popupAppearing = ImGui::IsWindowAppearing();
            if (popupAppearing)
                saveFailed = false;
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(240.0f * s);
            if (popupAppearing)
                ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##savename", saveBuf, sizeof(saveBuf),
                                                ImGuiInputTextFlags_EnterReturnsTrue
                                                | ImGuiInputTextFlags_AutoSelectAll);
            const bool doSave = ImGui::Button("Save") || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (doSave && saveBuf[0] != '\0')
            {
                // Only dismiss on a save that actually wrote a file. A failed
                // write used to close the dialog too, which read as success.
                if (saveUserPreset(saveBuf))
                {
                    saveFailed = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    saveFailed = true;
                }
            }
            if (saveFailed)
                ImGui::TextColored(ImVec4(0.90f, 0.42f, 0.35f, 1.0f),
                                   "Could not save. Try a different name.");
            if (cancel)
            {
                saveFailed = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(9);
    }

    // Clamped, never wrapping: stepping off either end parks on the end preset.
    // With no preset loaded (currentPreset < 0) either direction lands on the first.
    void stepPreset(int dir)
    {
        int i = currentPreset < 0 ? (dir < 0 ? 0 : -1) : currentPreset;
        i += dir;
        if (i < 0) i = 0;
        if (i >= kNumFactoryPresets) i = kNumFactoryPresets - 1;
        applyPreset(i);
    }

    // Single write path for a UI-driven parameter change: keeps the local cache
    // in step and brackets the host write with the edit-gesture markers.
    // Clamp to range and quantise the discrete parameters exactly the way the
    // processing side reads them, so the cached value can never disagree with
    // what the DSP acts on. values[] is not display-only: it feeds preset
    // identity matching and is what a saved user preset writes to disk.
    // Callers must range-check `idx` first.
    static float normalizeParamValue(uint32_t idx, float v) noexcept
    {
        v = std::max(kTeParams[idx].min, std::min(kTeParams[idx].max, v));
        switch (idx)
        {
        case kParamMode:
        case kParamSyncDivision:
        case kParamEchoRateNote:
            return std::round(v);
        case kParamTapeAge:
            return teQuantizeTapeAge(v);
        // All three booleans use the SAME threshold the plugin applies in
        // setParameterValue (>= 0.5f). They previously disagreed for Bypass and
        // Tempo Sync (> 0.5f here), so an incoming exact 0.5 latched ON in the
        // plugin while the UI cache read OFF -- POWER showing ON while the DSP
        // was bypassed.
        case kParamBypass:
        case kParamTempoSync:
        case kParamInputSend:
            return v >= 0.5f ? 1.0f : 0.0f;
        default:
            return v;
        }
    }

    // Keep the appended physical detent and the shipped semantic division in
    // step without making the knob jump when Head Select changes. The detent is
    // authoritative for new UI/host edits; writing the hidden legacy parameter
    // derives the closest reference detent for old projects.
    void storeParamLocally(uint32_t param, float value)
    {
        values[param] = value;
        if (param == kParamSyncDivision)
        {
            const int knobPos = teSyncKnobPosForDivision(
                (int)(value + 0.5f), leadingHeadIndex());
            values[kParamEchoRateNote] = (float)(knobPos + 1);
            legacySyncDivisionOverride = true;
        }
        else if (param == kParamEchoRateNote)
        {
            values[kParamSyncDivision] = (float)teDivisionForSyncKnobPos(
                syncKnobPosition(), leadingHeadIndex());
            legacySyncDivisionOverride = false;
        }
        else if (param == kParamMode)
        {
            if (legacySyncDivisionOverride)
            {
                const int knobPos = teSyncKnobPosForDivision(
                    (int)(values[kParamSyncDivision] + 0.5f), leadingHeadIndex());
                values[kParamEchoRateNote] = (float)(knobPos + 1);
            }
            else
            {
                values[kParamSyncDivision] = (float)teDivisionForSyncKnobPos(
                    syncKnobPosition(), leadingHeadIndex());
            }
        }
    }

    void setP(uint32_t param, float value)
    {
        if (param >= kParamCount || !std::isfinite(value))
            return;
        value = normalizeParamValue(param, value);
        storeParamLocally(param, value);
        editParameter(param, true);
        setParameterValue(param, value);
        editParameter(param, false);
    }

    // Walk a factory preset's parameter/value pairs. Shared by applyPreset() and
    // the identity matcher so the two can never disagree about what a preset sets.
    // BYPASS is deliberately absent: a preset recall must never fight the host's
    // own bypass state (see teIsPresetParam).
    template <typename Fn>
    static void forEachPresetParam(int idx, Fn&& fn)
    {
        const TapeEchoPreset& preset = kFactoryPresets[idx];
        for (uint32_t i = 0; i <= (uint32_t)kParamTapeAge; ++i)
            fn(i, preset.v[i]);
        fn((uint32_t)kParamOutputVolume, preset.outputVolume);
        fn((uint32_t)kParamEchoPan, preset.echoPan);
        fn((uint32_t)kParamReverbPan, preset.reverbPan);
        fn((uint32_t)kParamInputSend, preset.inputSend);
        fn((uint32_t)kParamMix, preset.mix);
        const int leadingHead = teLeadingHeadIndexForMode(
            (int)(preset.v[kParamMode] + 0.5f));
        const int knobPos = teSyncKnobPosForDivision(
            (int)(preset.v[kParamSyncDivision] + 0.5f), leadingHead);
        fn((uint32_t)kParamEchoRateNote, (float)(knobPos + 1));
    }

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumFactoryPresets)
            return;
        currentPreset = idx;
        currentUserName.clear();
        currentUserPath.clear();
        forEachPresetParam(idx, [this](uint32_t param, float value)
                                { setP(param, value); });
    }

    // Reset every control to its factory default. BYPASS is left alone so INIT
    // never fights the host's own bypass state.
    void initDefaults()
    {
        currentPreset = -1;
        currentUserName.clear();
        currentUserPath.clear();
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (teIsPresetParam(i))
                setP(i, kTeParams[i].def);
        // The defaults ARE the "Default" factory preset here, so re-derive rather
        // than leaving the combo reading "Presets..." over a recognised state.
        syncPresetSelection();
    }

    //--- user preset file library (~/.config/DuskAudio/TapeEcho2/presets) -------
    // Base dir: %APPDATA% (or %LOCALAPPDATA%) on Windows, else $XDG_CONFIG_HOME,
    // else $HOME/.config. macOS deliberately stays on the ~/.config layout the
    // shipped builds already write, so an update never orphans a user's library.
    // "." is the last resort only: never write relative to the host's CWD while
    // any supported variable is set.
    std::string configDir() const
    {
        std::string base;
       #if defined(_WIN32)
        for (const char* var : { "APPDATA", "LOCALAPPDATA" })
            if (const char* v = std::getenv(var); v != nullptr && *v != '\0')
            { base = v; break; }
       #elif defined(__APPLE__)
        // XDG_CONFIG_HOME is deliberately NOT read here: a mac with it set in a
        // dotfile would otherwise stop seeing the library shipped builds wrote.
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
            base = std::string(home) + "/.config";
       #else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
            base = xdg;
        else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
            base = std::string(home) + "/.config";
       #endif
        if (base.empty())
            base = ".";
        return base + "/DuskAudio/TapeEcho2/presets";
    }

    // Strict field parse, shared by the library scan and the loader so a file can
    // never mean two things. atof() reports failure as 0.0 and happily yields
    // NaN/inf for "nan"/"1e999", all of which would reach the DSP through setP();
    // require the whole field to be one finite number and clamp it into the
    // parameter's declared range (mode and sync division included, before either
    // is folded to an index). Returns false for a line to skip.
    //
    // Locale-independent on purpose, in both directions (saveUserPreset() imbues
    // the same classic locale): plugin hosts do call setlocale(), and a
    // comma-decimal locale makes strtod() stop at the '.' in "0.5" — every value
    // in every preset file would silently read as its default.
    static bool parsePresetValue(const std::string& line, std::size_t valueStart,
                                 uint32_t param, float& out)
    {
        std::istringstream field(line.substr(valueStart));
        field.imbue(std::locale::classic());
        double d = 0.0;
        field >> d;
        if (field.fail() || !std::isfinite(d))   // unparsable, or overflowed to inf
            return false;
        char trailing = '\0';
        if (field >> trailing)                   // trailing junk: not a number
            return false;
        const TeParam& p = kTeParams[param];
        out = (float)(d < (double)p.min ? (double)p.min
                                        : (d > (double)p.max ? (double)p.max : d));
        return true;
    }

    void scanUserPresets()
    {
        userPresets.clear();
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::directory_iterator it(configDir(), ec), end; !ec && it != end; it.increment(ec))
        {
            if (it->path().extension() != ".tepreset")
                continue;
            UserPreset up;
            up.path = it->path().string();
            up.name = it->path().stem().string();
            for (uint32_t i = 0; i < kParamCount; ++i)
                up.vals[i] = kTeParams[i].def;
            std::ifstream f(it->path());
            std::string line;
            bool hasEchoRateNote = false;
            while (std::getline(f, line))
            {
                const auto eq = line.find('=');
                if (eq == std::string::npos)
                    continue;
                const std::string key = line.substr(0, eq);
                if (key == "name") { up.name = line.substr(eq + 1); continue; }
                for (uint32_t i = 0; i < kParamCount; ++i)
                    if (teIsPresetParam(i) && key == kTeParams[i].id)
                    {
                        float v = 0.0f;
                        // Normalise on the way in, exactly as loadUserPreset()'s
                        // setP() will. This cache is compared against values[] to
                        // recover preset identity, so an un-normalised discrete
                        // value here (a hand-edited file, or one written before
                        // the values were quantised) would fail to match the very
                        // preset that had just been loaded.
                        if (parsePresetValue(line, eq + 1, i, v))
                        {
                            up.vals[i] = normalizeParamValue(i, v);
                            if (i == kParamEchoRateNote)
                                hasEchoRateNote = true;
                        }
                        break;   // else keep the default already in place
                    }
            }
            if (!hasEchoRateNote)
            {
                const int leadingHead = teLeadingHeadIndexForMode(
                    (int)(up.vals[kParamMode] + 0.5f));
                const int knobPos = teSyncKnobPosForDivision(
                    (int)(up.vals[kParamSyncDivision] + 0.5f), leadingHead);
                up.vals[kParamEchoRateNote] = (float)(knobPos + 1);
            }
            userPresets.push_back(std::move(up));
        }
        std::sort(userPresets.begin(), userPresets.end(),
                  [](const UserPreset& a, const UserPreset& b) { return a.name < b.name; });
    }

    // Display name stored inside a preset file, or "" when the file is missing or
    // carries no name= line (a foreign or truncated file in our own directory).
    static std::string storedPresetName(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
            if (line.compare(0, 5, "name=") == 0)
                return line.substr(5);
        return {};
    }

    // Returns false if nothing was written, so the caller can keep the dialog
    // open and say so instead of closing on a save that silently did nothing.
    bool saveUserPreset(const char* rawName)
    {
        std::string name(rawName);
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        if (name.empty())
            return false;
        const std::string dir = configDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return false; // no usable library directory (permissions, file in the way)
        // Filename stem: every non-alphanumeric collapses to '_', so distinct
        // display names can share a stem ("A B" and "A-B" both give "A_B").
        // Re-saving the SAME name still overwrites its own file; a stem clash
        // with a different name takes the next free suffix instead of silently
        // clobbering that preset.
        std::string stem;
        for (char c : name)
            stem += std::isalnum((unsigned char)c) ? c : '_';
        std::string path;
        bool usable = false;
        for (int n = 1; n <= 99 && !usable; ++n)
        {
            path = dir + "/" + stem
                 + (n == 1 ? std::string() : "_" + std::to_string(n)) + ".tepreset";
            // A free path, or one whose file already stores THIS display name.
            // An existing file without a name= line is not free: the library
            // lists it under its stem, so overwriting it would drop a preset
            // the player can see. A path that cannot be probed is never assumed
            // free either - exists() reports an error as false.
            ec.clear();
            const bool present = std::filesystem::exists(path, ec);
            if (ec)
                continue;
            usable = !present || storedPresetName(path) == name;
        }
        if (!usable)
            return false; // 99 colliding stems: refuse rather than overwrite one
        std::ofstream f(path, std::ios::trunc);
        if (!f)
            return false;
        // Classic locale so the values are written with '.' whatever locale the
        // host installed, matching parsePresetValue() on the way back in.
        f.imbue(std::locale::classic());
        f << "name=" << name << "\n";
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (!teIsPresetParam(i))
                continue;
            // Do not write the DERIVED half of the compatibility pair. While an
            // old project's semantic division owns the delay, Echo Rate Note is
            // only the nearest physical detent to it, and that division may not
            // be representable on the current head at all. Writing both would
            // lose the exact division on reload: the file is read in ascending
            // index order, so echo_rate_note (21) would land after
            // sync_division (12) and take ownership. Omitting it leaves
            // loadUserPreset's default pass followed by the file's
            // sync_division, which ends with the legacy value authoritative --
            // and scanUserPresets already derives a detent for files without
            // the key, so the preset browser still matches.
            if (i == kParamEchoRateNote && legacySyncDivisionOverride)
                continue;
            f << kTeParams[i].id << "=" << values[i] << "\n";
        }
        f.close();
        if (!f)
            return false; // flush/close failed: the file on disk is not complete
        scanUserPresets();
        currentPreset = -1;
        currentUserName = name;
        currentUserPath = path;
        return true;
    }

    void loadUserPreset(const std::string& path, const std::string& name)
    {
        std::ifstream f(path);
        if (!f)
            return;
        // Default every parameter first, then overlay the file's values. This
        // matches the cache built in scanUserPresets() (missing keys ->
        // kTeParams[i].def), so an incomplete or legacy file loads to the exact
        // values its cached identity records - otherwise a missing key would keep
        // the current value and deriveUserPreset() could never match. BYPASS is
        // left alone (never saved, and not part of preset identity).
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (teIsPresetParam(i))
                setP(i, kTeParams[i].def);
        std::string line;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = line.substr(0, eq);
            if (key == "name")
                continue;
            for (uint32_t i = 0; i < kParamCount; ++i)
                if (teIsPresetParam(i) && key == kTeParams[i].id)
                {
                    float v = 0.0f;
                    if (parsePresetValue(line, eq + 1, i, v))
                        setP(i, v);   // else keep the default written above
                    break;
                }
        }
        currentPreset = -1;
        currentUserName = name;
        currentUserPath = path;
    }

    //--- preset identity recovery ----------------------------------------------
    // The active preset is UI-only state; these re-derive it from the current
    // parameter values so a project reload (which restores parameters but not the
    // selection) shows the right preset again. Compares with a range-relative
    // tolerance to absorb host parameter quantisation, and never compares BYPASS
    // or the meter output.
    bool paramMatches(uint32_t id, float v) const
    {
        const TeParam& d = kTeParams[id];
        const float tol = std::max(1.0e-3f, (d.max - d.min) * 1.0e-4f);
        return std::fabs(values[id] - v) <= tol;
    }

    int deriveFactoryPreset() const
    {
        for (int idx = 0; idx < kNumFactoryPresets; ++idx)
        {
            bool ok = true;
            forEachPresetParam(idx, [&](uint32_t id, float v)
            {
                if (ok && teIsPresetParam(id) && !paramMatches(id, v))
                    ok = false;
            });
            if (ok)
                return idx;
        }
        return -1;
    }

    int deriveUserPreset() const
    {
        // Preserve the loaded file's identity when duplicate presets contain the
        // same values; parameter matching alone cannot distinguish those files.
        if (!currentUserPath.empty())
            for (size_t i = 0; i < userPresets.size(); ++i)
                if (userPresets[i].path == currentUserPath)
                {
                    bool ok = true;
                    for (uint32_t id = 0; id < kParamCount && ok; ++id)
                        if (teIsPresetParam(id) && !paramMatches(id, userPresets[i].vals[id]))
                            ok = false;
                    if (ok)
                        return (int)i;
                    break;
                }

        for (size_t i = 0; i < userPresets.size(); ++i)
        {
            bool ok = true;
            for (uint32_t id = 0; id < kParamCount && ok; ++id)
                if (teIsPresetParam(id) && !paramMatches(id, userPresets[i].vals[id]))
                    ok = false;
            if (ok)
                return (int)i;
        }
        return -1;
    }

    // Recompute the active selection from the current values (factory wins).
    void syncPresetSelection()
    {
        const int f = deriveFactoryPreset();
        if (f >= 0)
        {
            currentPreset = f;
            currentUserName.clear();
            currentUserPath.clear();
            return;
        }
        const int u = deriveUserPreset();
        if (u >= 0)
        {
            currentPreset = -1;
            currentUserName = userPresets[(size_t)u].name;
            currentUserPath = userPresets[(size_t)u].path;
            return;
        }
        currentPreset = -1;
        currentUserName.clear();
        currentUserPath.clear();
    }

    void drawMeterBlock(ImDrawList* dl)
    {
        // The reference Peak lamp starts to illuminate around -2 to -1.5 dBFS.
        led(dl, 34, 93, peakLevel > 0.82f, 5.2f);
        text(dl, 34, 104.5f, 11.0f, kColWhite, "PEAK", 0, true);
        regularText(dl, 34, 116.5f, 9.5f, kColWhiteDim, "LEVEL", 0);

        constexpr float x0 = 70.0f, y0 = 60.0f, x1 = 274.0f, y1 = 153.0f;
        dl->AddRectFilled(P(x0 - 5, y0 - 5), P(x1 + 5, y1 + 5),
                          IM_COL32(5, 5, 6, 210), 5.5f * s);
        brushedRect(dl, x0 - 3, y0 - 3, x1 + 3, y1 + 3,
                    IM_COL32(189, 187, 181, 255),
                    IM_COL32(77, 77, 79, 255), 4.5f);
        dl->AddRectFilledMultiColor(
            P(x0, y0), P(x1, y1),
            IM_COL32(23, 25, 20, 255), IM_COL32(14, 16, 13, 255),
            IM_COL32(7, 9, 7, 255), IM_COL32(10, 11, 9, 255));

        // Read the DSP meter directly when the plugin runs in-process
        // (CLAP/LV2 hosts do not forward output parameters to the UI), else
        // fall back to the output parameter.
        float lvl = values[kParamOutLevel];
        float peak = values[kParamPeakLevel];
       #if DAF_PLUGIN_WANT_DIRECT_ACCESS
        if (tapeEchoGetRecordVuLevel != nullptr) // weak: null in the split LV2 UI
            if (void* const inst = getPluginInstancePointer())
                lvl = tapeEchoGetRecordVuLevel(inst);
        if (tapeEchoGetRecordPeakLevel != nullptr)
            if (void* const inst = getPluginInstancePointer())
                peak = tapeEchoGetRecordPeakLevel(inst);
        #endif
        meterLevel = lvl;
        peakLevel = peak;
        // Hosted-reference calibration: a -18 dBFS RMS tone at unity Input
        // Volume settles at 0 VU. The DSP publishes a sine-RMS-calibrated
        // linear record level, so add 18 dB before mapping it to the face.
        constexpr float kVuReferenceDbfs = -18.0f;
        const float vuDb = 20.0f * std::log10(lvl > 1e-5f ? lvl : 1e-5f)
                         - kVuReferenceDbfs;
        float target = (vuDb + 20.0f) / 23.0f; // -20 VU .. +3 VU across the arc
        target = target < 0.0f ? 0.0f : (target > 1.0f ? 1.0f : target);
        // TapeEchoDSP already applies the complete 225 ms attack / 200 ms
        // release ballistic. A second visual smoother here would make the
        // displayed needle lag the actual meter value and vary with frame rate.
        needlePos = target;

        dl->PushClipRect(P(x0, y0), P(x1, y1), true);

        const ImVec2 pivot = P(172, 239);
        const float rArc   = 155.0f * s;
        const float aMin   = -0.62f, aMax = 0.62f; // radians from vertical

        // Twin-rail scale inspired by classic illuminated VU faces: a broad,
        // segmented outer band plus a slimmer inner rail. Both change from
        // green to red exactly at 0 dB, with a dark outline keeping the color
        // controlled against the black meter face.
        constexpr float kZeroDbT = 20.0f / 23.0f;
        const float redStart = aMin + (aMax - aMin) * kZeroDbT;
        const float outerRadius = rArc + 7.0f * s;
        const float innerRadius = rArc - 2.0f * s;
        constexpr ImU32 kScaleGreen = IM_COL32(73, 183, 102, 230);
        constexpr ImU32 kScaleRed = IM_COL32(226, 66, 46, 235);
        constexpr ImU32 kScaleEdge = IM_COL32(5, 8, 5, 235);

        const auto strokeArc = [&](float radius, float from, float to,
                                   ImU32 color, float thickness, int segments)
        {
            dl->PathClear();
            dl->PathArcTo(pivot, radius, from - 0.5f * kPi,
                          to - 0.5f * kPi, segments);
            dl->PathStroke(color, 0, thickness * s);
        };

        strokeArc(outerRadius, aMin, aMax, kScaleEdge, 9.0f, 64);
        strokeArc(innerRadius, aMin, aMax, kScaleEdge, 6.0f, 64);
        strokeArc(outerRadius, aMin, redStart, kScaleGreen, 6.0f, 52);
        strokeArc(outerRadius, redStart, aMax, kScaleRed, 6.0f, 18);
        strokeArc(innerRadius, aMin, redStart, kScaleGreen, 3.5f, 52);
        strokeArc(innerRadius, redStart, aMax, kScaleRed, 3.5f, 18);

        // Major-value separators cut only the wide outer rail. The 0 dB split
        // continues through both rails to make the color transition deliberate.
        static constexpr float kSeparatorsDb[] = { -10.0f, -7.0f, -5.0f, -3.0f };
        for (const float db : kSeparatorsDb)
        {
            const float t = (db + 20.0f) / 23.0f;
            const float a = aMin + (aMax - aMin) * t;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(
                ImVec2(pivot.x + dir.x * (outerRadius - 4.8f * s),
                       pivot.y + dir.y * (outerRadius - 4.8f * s)),
                ImVec2(pivot.x + dir.x * (outerRadius + 4.8f * s),
                       pivot.y + dir.y * (outerRadius + 4.8f * s)),
                kScaleEdge, 1.25f * s);
        }
        {
            const ImVec2 dir(std::sin(redStart), -std::cos(redStart));
            dl->AddLine(
                ImVec2(pivot.x + dir.x * (innerRadius - 3.2f * s),
                       pivot.y + dir.y * (innerRadius - 3.2f * s)),
                ImVec2(pivot.x + dir.x * (outerRadius + 4.8f * s),
                       pivot.y + dir.y * (outerRadius + 4.8f * s)),
                kScaleEdge, 1.5f * s);
        }

        struct MeterMark { float db; const char* label; };
        static constexpr MeterMark kMarks[] =
        {
            { -20.0f, "-20" }, { -10.0f, "-10" }, { -7.0f, "-7" },
            { -5.0f, "-5" }, { -3.0f, "-3" }, { 0.0f, "0" }, { 3.0f, "+3" },
        };
        for (const MeterMark& mark : kMarks)
        {
            const float t = (mark.db + 20.0f) / 23.0f;
            const float a = aMin + (aMax - aMin) * t;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            const ImU32 ink = mark.db >= 0.0f
                            ? IM_COL32(235, 75, 54, 255)
                            : IM_COL32(229, 226, 213, 255);
            regularText(dl, 172.0f + dir.x * 137.0f,
                        239.0f + dir.y * 137.0f - 4.8f,
                        9.5f, ink, mark.label, 0);
        }

        text(dl, 172, 119.5f, 13.5f, IM_COL32(151, 224, 117, 255), "VU", 0, true);
        regularText(dl, 172, 134.5f, 9.5f, kColWhiteDim,
                    "RECORD LEVEL", 0);

        // needle
        {
            const float a = aMin + (aMax - aMin) * needlePos;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(pivot.x + dir.x * 38.0f * s + 1.0f * s,
                               pivot.y + dir.y * 38.0f * s + 1.2f * s),
                        ImVec2(pivot.x + dir.x * (rArc + 5.0f * s) + 1.0f * s,
                               pivot.y + dir.y * (rArc + 5.0f * s) + 1.2f * s),
                        IM_COL32(0, 0, 0, 150), 2.4f * s);
            dl->AddLine(ImVec2(pivot.x + dir.x * 38.0f * s,
                               pivot.y + dir.y * 38.0f * s),
                        ImVec2(pivot.x + dir.x * (rArc + 5.0f * s),
                               pivot.y + dir.y * (rArc + 5.0f * s)),
                        IM_COL32(239, 234, 215, 255), 1.45f * s);
        }

        dl->AddRectFilledMultiColor(P(x0, y0), P(x1, y0 + 34),
                                    IM_COL32(255, 255, 255, 34), IM_COL32(255, 255, 255, 9),
                                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->AddLine(P(x0 + 8, y0 + 5), P(x1 - 28, y0 + 5),
                    IM_COL32(255, 255, 255, 24), 0.8f * s);
        dl->PopClipRect();
    }

    void drawInputRow(ImDrawList* dl)
    {
        constexpr float kKnobY = 234.0f;
        constexpr float x[4] = { 38.0f, 106.0f, 174.0f, 242.0f };

        knobLabel(dl, x[0], kKnobY, 23.0f, "INPUT");
        knob("input", kParamInputGain, 0.0f, 1.0f, x[0], kKnobY, 23.0f,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, true);

        knobLabel(dl, x[1], kKnobY, 22.0f, "MIX");
        knob("mix", kParamMix, 0.0f, 1.0f, x[1], kKnobY, 22.0f,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, true);

        knobLabel(dl, x[2], kKnobY, 22.0f, "WOW &", "FLUTTER");
        knob("wow", kParamWowFlutter, 0.0f, 1.0f, x[2], kKnobY, 22.0f,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, true);

        knobLabel(dl, x[3], kKnobY, 23.0f, "OUTPUT");
        knob("output", kParamOutputVolume, 0.0f, 1.0f, x[3], kKnobY, 23.0f,
             false, true, "%+.1f", " dB", 40.0f, -20.0f, true, true,
             nullptr);
    }

    void drawModeSelector(ImDrawList* dl)
    {
        constexpr float x0 = 290.0f, y0 = 58.0f, x1 = 460.0f, y1 = 282.0f;
        dl->AddRectFilled(P(x0 + 2, y0 + 4), P(x1 + 3, y1 + 4),
                          IM_COL32(0, 0, 0, 90), 7.0f * s);
        brushedRect(dl, x0, y0, x1, y1,
                    IM_COL32(91, 122, 60, 255),
                    IM_COL32(61, 85, 37, 255), 7.0f);
        dl->AddRect(P(x0, y0), P(x1, y1), kColGreenDk,
                    7.0f * s, 0, 2.0f * s);
        dl->AddLine(P(x0 + 8, y0 + 4), P(x1 - 8, y0 + 4),
                    IM_COL32(191, 207, 163, 54), 0.9f * s);

        text(dl, 375, 66, 17.0f, kColWhite,
             "HEAD SELECT", 0, true);

        const float cx = 375, cy = 157, R = 36;
        const int mode = modeIndex();

        dl->AddCircleFilled(P(cx + 1.5f, cy + 3.5f), (R + 12.0f) * s,
                            IM_COL32(0, 0, 0, 95), 56);
        dl->AddCircleFilled(P(cx, cy), (R + 9.0f) * s,
                            IM_COL32(28, 37, 19, 255), 56);
        dl->AddCircleFilled(P(cx, cy - 1.2f), (R + 7.4f) * s,
                            IM_COL32(188, 187, 181, 255), 56);
        dl->AddCircleFilled(P(cx, cy + 0.5f), (R + 5.7f) * s,
                            IM_COL32(76, 77, 79, 255), 56);
        dl->AddCircleFilled(P(cx, cy + 1.0f), (R + 3.8f) * s,
                            IM_COL32(29, 38, 20, 255), 56);

        knob("mode", kParamMode, 1.0f, 12.0f, cx, cy, R, true, false,
             "%.0f", "", 1.0f, 0.0f, false, true, kModeShort[mode]);

        // Detent ticks + numbered arc. The tick ring sits in the gap between the
        // bezel and the numbers; the selected detent goes white along with its number.
        for (int i = 0; i < 12; ++i)
        {
            const float a = knobAngle((float)i / 11.0f);
            const ImVec2 dir(std::sin(a), -std::cos(a));
            const bool cur = (i == mode);
            const float t0 = cur ? 48.0f : 49.0f;
            const float t1 = cur ? 54.0f : 53.0f;
            dl->AddLine(P(cx + dir.x * t0, cy + dir.y * t0),
                        P(cx + dir.x * t1, cy + dir.y * t1),
                        cur ? kColWhite : IM_COL32(28, 41, 16, 255),
                        (cur ? 2.2f : 1.5f) * s);

            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            text(dl, cx + dir.x * 61.0f, cy + dir.y * 61.0f - 5.5f,
                 cur ? 15.0f : 12.5f,
                 cur ? kColWhite : IM_COL32(209, 220, 191, 255),
                 num, 0, cur);
        }

        dl->AddRectFilled(P(298, 213), P(452, 237),
                          IM_COL32(31, 46, 21, 210), 3.0f * s);
        dl->AddRect(P(298, 213), P(452, 237),
                    IM_COL32(126, 151, 94, 170), 3.0f * s, 0, 0.9f * s);
        text(dl, 375, 217.0f, 13.0f, kColWhite, kModeShort[mode], 0, true);

        drawHeadTimingStrip(dl, values[kParamTempoSync] > 0.5f);
    }

    void drawControlPanel(ImDrawList* dl)
    {
        constexpr float x0 = 470.0f, y0 = 58.0f, x1 = 888.0f, y1 = 282.0f;
        dl->AddRectFilled(P(x0 - 4, y0 - 3), P(x1 + 3, y1 + 4),
                          IM_COL32(0, 0, 0, 125), 7.0f * s);
        brushedRect(dl, x0 - 2, y0 - 2, x1 + 2, y1 + 2,
                    IM_COL32(190, 188, 183, 255),
                    IM_COL32(86, 86, 87, 255), 7.0f);
        dl->AddRectFilledMultiColor(
            P(x0 + 2, y0 + 2), P(x1 - 2, y1 - 2),
            IM_COL32(25, 25, 26, 255), IM_COL32(20, 20, 21, 255),
            IM_COL32(13, 13, 14, 255), IM_COL32(16, 16, 17, 255));
        dl->AddRect(P(x0 + 2, y0 + 2), P(x1 - 2, y1 - 2),
                    IM_COL32(4, 4, 5, 220), 5.0f * s, 0, 1.2f * s);

        const int mode = modeIndex();
        const bool reverbActive = mode >= 4;
        const bool echoActive = mode != 11;

        knobLabel(dl, 520, 121, 25, "BASS");
        knob("bass", kParamBass, -1.0f, 1.0f, 520, 121, 25,
             false, true, "%+.1f", " dB", 17.0f, 0.0f, true, true);
        knobLabel(dl, 620, 121, 25, "TREBLE");
        knob("treble", kParamTreble, -1.0f, 1.0f, 620, 121, 25,
             false, true, "%+.1f", " dB", 17.0f, 0.0f, true, true);
        knobLabel(dl, 720, 121, 25, "REVERB LEVEL", nullptr, reverbActive);
        knob("reverbvol", kParamReverbLevel, 0.0f, 1.0f, 720, 121, 25,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, reverbActive);
        knobLabel(dl, 830, 121, 25, "REVERB PAN", nullptr, reverbActive);
        knob("reverbpan", kParamReverbPan, 0.0f, 1.0f, 830, 121, 25,
             false, true, "%+.0f", "", 200.0f, -100.0f, true, reverbActive);

        knobLabel(dl, 520, 229, 25, "REPEAT RATE", nullptr, echoActive);
        const bool sync = values[kParamTempoSync] > 0.5f;
        // Distinct ImGui ids per branch: DuskPanel keys its inline "Type value"
        // edit state on this string, so sharing one id let a pending edit opened
        // on Repeat Rate commit into Echo Rate Note when TEMPO SYNC toggled
        // mid-gesture.
        if (sync) // knob steps through note divisions while synced
            knob("rate_sync", kParamEchoRateNote, 1.0f,
                 (float)kNumSyncKnobPositions,
                 520, 229, 25, true, true, "%.0f", "", 1.0f, 0.0f, true,
                 echoActive, kSyncDivisions[divIndex()].name);
        else
            knob("rate_free", kParamRepeatRate, 0.0f, 1.0f, 520, 229, 25,
                 false, true, "%.0f", "%", 100.0f, 0.0f, true, echoActive,
                 nullptr);

        knobLabel(dl, 620, 229, 25, "INTENSITY", nullptr, echoActive);
        knob("intensity", kParamIntensity, 0.0f, 1.0f, 620, 229, 25,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, echoActive,
             nullptr);
        knobLabel(dl, 720, 229, 25, "ECHO LEVEL", nullptr, echoActive);
        knob("echovol", kParamEchoLevel, 0.0f, 1.0f, 720, 229, 25,
             false, true, "%.0f", "%", 100.0f, 0.0f, true, echoActive,
             nullptr);
        knobLabel(dl, 830, 229, 25, "ECHO PAN", nullptr, echoActive);
        knob("echopan", kParamEchoPan, 0.0f, 1.0f, 830, 229, 25,
             false, true, "%+.0f", "", 200.0f, -100.0f, true, echoActive,
             nullptr);
    }

    void drawHeadTimingStrip(ImDrawList* dl, bool sync)
    {
        float motorMs = duskaudio::TapeEchoDSP::delayMsForRepeatRate(
            values[kParamRepeatRate]);

        float slow01 =
            (motorMs - duskaudio::TapeEchoDSP::kMinDelayMs)
            / (duskaudio::TapeEchoDSP::kMaxDelayMs
               - duskaudio::TapeEchoDSP::kMinDelayMs);
        slow01 = slow01 < 0.0f ? 0.0f : (slow01 > 1.0f ? 1.0f : slow01);

        // Documented front-panel timing ranges. The calibrated DSP
        // retains the more precise hosted arrival times internally.
        constexpr float kFastMs[3] = { 69.0f, 131.0f, 189.0f };
        constexpr float kSlowMs[3] = { 177.0f, 337.0f, 489.0f };
        // Bottom of the green head-select panel, three equal timing cells.
        constexpr float kCellX[4] =
            { 298.0f, 349.33333f, 400.66667f, 452.0f };
        constexpr float kStripY0 = 244.0f, kStripY1 = 276.0f;
        constexpr const char* kLabels[3] = { "HEAD 1", "HEAD 2", "HEAD 3" };
        constexpr uint8_t kHeadMask[12] =
            { 1, 2, 4, 6, 1, 2, 4, 3, 6, 5, 7, 0 };
        const uint8_t activeMask = kHeadMask[modeIndex()];
        const int leadingHead = leadingHeadIndex();
        const int knobPos = syncKnobPosition();
        // Blinking means "this note is outside the transport's physical range",
        // which depends on host tempo: the captured table is that same decision
        // at 120 BPM only. Read the clamp the audio path actually applied when
        // the DSP is in-process, and keep the table for the split LV2 UI, where
        // the bridge is null.
        bool syncBlinks = kSyncReadoutBlinks[leadingHead][knobPos];
       #if DAF_PLUGIN_WANT_DIRECT_ACCESS
        if (tapeEchoGetSyncNoteOutOfRange != nullptr) // weak: null in split LV2
            if (void* const inst = getPluginInstancePointer())
                syncBlinks = tapeEchoGetSyncNoteOutOfRange(inst);
       #endif
        const bool flashVisible =
            !syncBlinks || std::fmod(ImGui::GetTime(), 0.8) < 0.4;

        dl->AddRectFilled(P(kCellX[0], kStripY0), P(kCellX[3], kStripY1),
                          IM_COL32(30, 42, 22, 255), 4.0f * s);
        dl->AddRect(P(kCellX[0], kStripY0), P(kCellX[3], kStripY1),
                    IM_COL32(111, 143, 85, 180), 4.0f * s, 0, 1.0f * s);

        for (int i = 0; i < 3; ++i)
        {
            const bool active = (activeMask & (1u << i)) != 0;
            const float cx = 0.5f * (kCellX[i] + kCellX[i + 1]);
            if (active)
                dl->AddRectFilled(
                    P(kCellX[i] + 1.0f, kStripY0 + 1.0f),
                    P(kCellX[i + 1] - 1.0f, kStripY1 - 1.0f),
                    IM_COL32(45, 62, 32, 255), 2.0f * s);
            if (i > 0)
                dl->AddLine(P(kCellX[i], kStripY0 + 2.0f), P(kCellX[i], kStripY1 - 2.0f),
                            IM_COL32(78, 101, 58, 255), 1.0f * s);

            text(dl, cx, 247.0f, 10.5f,
                 active ? kColWhiteDim : IM_COL32(159, 171, 146, 255),
                 kLabels[i], 0, active);

            // Five dashes, not three: the reference's blanked head cells were
            // photographed at "-----", and the widest live string in the sync
            // table is six characters, so the cell already has the room.
            static constexpr const char* kBlankReadout = "-----";
            char valueText[16];
            if (!active)
            {
                std::snprintf(valueText, sizeof(valueText), "%s", kBlankReadout);
            }
            else if (sync)
            {
                // Galaxy uses a measured per-leading-head lookup, including
                // intentional +/- omissions at the two shortest Head-2 values.
                // Do not re-derive this from ideal head-spacing ratios.
                const char* const readout =
                    kSyncReadoutText[leadingHead][knobPos][i];
                std::snprintf(valueText, sizeof(valueText), "%s",
                              readout != nullptr ? readout : kBlankReadout);
            }
            else
            {
                const int delayMs = (int)std::lround(
                    kFastMs[i] + slow01 * (kSlowMs[i] - kFastMs[i]));
                std::snprintf(
                    valueText, sizeof(valueText), "%d ms", delayMs);
            }
            // 14.5 px LCD-green value: the 12 px near-white original read as
            // faint and small against the recessed cell.
            if (!active || !sync || flashVisible)
                regularText(dl, cx, 260.5f, 14.5f,
                     active ? IM_COL32(136, 230, 102, 255)
                            : IM_COL32(182, 195, 166, 255),
                     valueText, 0);
        }
    }

    bool railToggle(ImDrawList* dl, const char* id, uint32_t param,
                    float cx, const char* label, bool invert = false)
    {
        const bool on = invert ? values[param] < 0.5f : values[param] >= 0.5f;
        const ImVec2 hit0 = P(cx - 39, 292);
        const ImVec2 hit1 = P(cx + 39, 339);
        ImGui::SetCursorScreenPos(hit0);
        ImGui::InvisibleButton(id, ImVec2(hit1.x - hit0.x, hit1.y - hit0.y));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            const float nv = invert ? (on ? 1.0f : 0.0f) : (on ? 0.0f : 1.0f);
            setP(param, nv);
        }

        text(dl, cx, 292.0f, 12.5f, kColRailInk, label, 0, true);
        // Lift the mechanism enough to give its state legends the same clear,
        // semibold treatment as the rest of the panel instead of squeezing a
        // tiny regular face against the lower chassis edge.
        const float baseX = cx, baseY = 318.0f;
        dl->AddCircleFilled(P(baseX + 1.0f, baseY + 1.8f), 9.0f * s,
                            IM_COL32(0, 0, 0, 85), 28);
        dl->AddCircleFilled(P(baseX, baseY), 8.0f * s,
                            IM_COL32(70, 69, 66, 255), 28);
        dl->AddCircleFilled(P(baseX, baseY - 0.7f), 6.3f * s,
                            hovered ? IM_COL32(211, 209, 201, 255)
                                    : IM_COL32(176, 174, 168, 255), 28);
        dl->AddCircleFilled(P(baseX, baseY), 3.5f * s,
                            IM_COL32(28, 28, 28, 255), 20);
        const float ex = baseX + (on ? 8.5f : -8.5f);
        const float ey = baseY + (on ? -6.0f : 5.0f);
        dl->AddLine(P(baseX + 0.8f, baseY + 1.0f), P(ex + 0.8f, ey + 1.2f),
                    IM_COL32(0, 0, 0, 100), 4.2f * s);
        dl->AddLine(P(baseX, baseY), P(ex, ey),
                    IM_COL32(205, 204, 199, 255), 3.2f * s);
        dl->AddCircleFilled(P(ex, ey), 3.2f * s,
                            IM_COL32(225, 224, 219, 255), 18);
        text(dl, cx - 22.0f, 328.0f, 10.0f,
             on ? IM_COL32(52, 51, 49, 255) : kColRailInk, "OFF", 0, true);
        text(dl, cx + 22.0f, 328.0f, 10.0f,
             on ? kColRailInk : IM_COL32(52, 51, 49, 255), "ON", 0, true);
        return ImGui::IsItemClicked();
    }

    void drawUtilityRail(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 287), P(kDesignW, 340), IM_COL32(4, 4, 5, 190));
        brushedRect(dl, 0, 291, kDesignW, 340,
                    IM_COL32(211, 209, 203, 255),
                    IM_COL32(126, 124, 120, 255));
        dl->AddLine(P(0, 291), P(kDesignW, 291),
                    IM_COL32(246, 244, 235, 100), 1.0f * s);
        dl->AddLine(P(0, 339), P(kDesignW, 339),
                    IM_COL32(20, 20, 20, 180), 1.0f * s);
        for (float x : { 225.0f, 450.0f, 675.0f })
            dl->AddLine(P(x, 295), P(x, 336),
                        IM_COL32(48, 47, 45, 110), 1.0f * s);

        // Each control sits on the centre line of its equal-width rail section.
        railToggle(dl, "##rail_input", kParamInputSend, 112.5f, "RECORD INPUT");
        railToggle(dl, "##rail_sync", kParamTempoSync, 337.5f, "TEMPO SYNC");

        // Three replaceable-cartridge conditions are exposed rather than a
        // continuous wear percentage. A compact engraved scale replaces the
        // detached LCD-style value box so the state reads as part of the rail.
        text(dl, 562.5f, 292.0f, 12.5f, kColRailInk, "TAPE AGE", 0, true);
        knob("rail_age", kParamTapeAge, 0.0f, 1.0f, 562.5f, 316.0f, 10.5f,
             false, false, "%.0f", "", 1.0f, 0.0f, false, true);

        static constexpr const char* kAgeLabels[] = { "NEW", "USED", "OLD" };
        static constexpr float kAgeLabelX[] = { 533.0f, 562.5f, 592.0f };
        const int ageIndex = values[kParamTapeAge] < 0.25f
                           ? 0 : (values[kParamTapeAge] < 0.75f ? 1 : 2);
        for (int i = 0; i < 3; ++i)
        {
            const bool selected = i == ageIndex;
            text(dl, kAgeLabelX[i], 328.0f, 10.0f,
                 selected ? kColRailInk : IM_COL32(52, 51, 49, 255),
                 kAgeLabels[i], 0, true);
            if (selected)
                dl->AddLine(P(kAgeLabelX[i] - (i == 1 ? 11.0f : 9.0f), 338.0f),
                            P(kAgeLabelX[i] + (i == 1 ? 11.0f : 9.0f), 338.0f),
                            kColGreenDk, 1.4f * s);
        }

        const bool on = values[kParamBypass] < 0.5f;
        railToggle(dl, "##rail_power", kParamBypass, 787.5f, "POWER", true);
        led(dl, 828, 317, on, 5.0f);
    }

    // Division the DSP is actually running. While an old project's semantic
    // division owns the delay (see storeParamLocally), that stored value may be
    // absent from the current head's table and its knob position is only the
    // nearest detent -- report the stored value so the readouts cannot name a
    // different note than the one being played.
    int divIndex() const
    {
        if (legacySyncDivisionOverride)
        {
            const int stored = (int)(values[kParamSyncDivision] + 0.5f);
            return stored < 0 ? 0
                              : (stored >= kNumSyncDivisions
                                    ? kNumSyncDivisions - 1 : stored);
        }
        return teDivisionForSyncKnobPos(syncKnobPosition(), leadingHeadIndex());
    }

    int syncKnobPosition() const
    {
        int pos = (int)(values[kParamEchoRateNote] + 0.5f) - 1;
        return pos < 0 ? 0
                       : (pos >= kNumSyncKnobPositions
                            ? kNumSyncKnobPositions - 1 : pos);
    }

    int leadingHeadIndex() const
    {
        return teLeadingHeadIndexForMode(modeIndex() + 1);
    }

    int modeIndex() const
    {
        int m = (int)(values[kParamMode] + 0.5f) - 1;
        return m < 0 ? 0 : (m > 11 ? 11 : m);
    }

    static constexpr const char* kModeShort[12] =
    {
        "HEAD 1", "HEAD 2", "HEAD 3", "HEADS 2+3",
        "HEAD 1 + REV", "HEAD 2 + REV", "HEAD 3 + REV", "HEADS 1+2 + REV",
        "HEADS 2+3 + REV", "HEADS 1+3 + REV", "ALL HEADS + REV", "REVERB ONLY",
    };

    duskdaf::DuskPanel panel;
    duskdaf::SupportersOverlay supportersOverlay;
    duskdaf::DuskImGuiTextInputFocus textInputFocus;
    duskdaf::CrispFontSet labelFonts;
    duskdaf::CrispFontSet regularFonts;
    ImFont* labelFont = nullptr;
    float  values[kParamCount] = {};
    float  needlePos = 0.0f;
    float  meterLevel = 0.0f;
    float  peakLevel = 0.0f;
    int    currentPreset = -1;
    std::string currentUserName;   // display name of the active user preset
    std::string currentUserPath;   // stable identity of the active user preset

    // Cached user preset library (file name + display name + every preset param).
    struct UserPreset { std::string name, path; float vals[kParamCount]; };
    std::vector<UserPreset> userPresets;
    char   saveBuf[64] = {};
    bool   saveFailed = false;      // last SAVE wrote nothing; dialog stays open
    bool   legacySyncDivisionOverride = false;

    bool   showSupporters = false;
    bool   gripCursorSet = false;   // NWSE cursor currently pushed to the window
    float  s = 1.0f;
    ImVec2 org = ImVec2(0, 0);

    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TapeEchoUI)
};

constexpr const char* TapeEchoUI::kModeShort[12];

UI* createUI()
{
    return new TapeEchoUI();
}

END_NAMESPACE_DAF
