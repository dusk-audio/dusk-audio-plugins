// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiSynthUI.cpp — Dear ImGui / ImDrawList UI for Sunset Circuits (internal
// class/namespace names stay stable), implementing
// docs/dpf-migration/09-multi-synth-ui-spec.md: fixed 1240x780 design space
// (uniformly scaled, tape-echo pattern), six crossfaded mode skins, custom
// filter / ADSR / scope / VU displays, dual LFOs, mod-matrix overlay, 4-op FM
// operator matrix + algorithm diagram, 3-lane acid sequencer, and a playable
// 3-octave keyboard. All chrome is custom ImDrawList work; the only stock ImGui
// widgets are BeginCombo/Selectable and the shared inline value editor.
//
// Reuses the shared duskdpf::DuskPanel (chrome knob / LED / text / value bubble)
// and duskdpf::CrispFontSet; meters/scope/step index come through the weak
// MultiSynthAccess bridge with output-param fallback for split LV2 UIs.

#include "DistrhoUI.hpp"

#include "MultiSynthAccess.hpp"
#include "MultiSynthParams.hpp"
#include "UserPresetStore.hpp"    // file-based user preset library (UI-side only)
#include "MultiSynthDSP.hpp"      // MultiSynthDSP::copyScope / kScopeSize
#include "FMAlgorithms.hpp"       // msynth::kPrismAlgos — single source of truth
#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 1240.0f;
    constexpr float kDesignH = 780.0f;
    constexpr float kPi = 3.14159265358979f;

    inline ImU32 hx(uint32_t rgb) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255); }

    // Per-mode palette (exact hex from spec §4.0). textPanel = on-panel ink
    // (== text on dark panels; dark on Acid's silver panel).
    struct MSPal { ImU32 bg, panel, accent, text, textPanel, ledOn, ledOff; };
    const MSPal kPalettes[6] = {
        // Cosmos
        { hx(0x14161C), hx(0x1E2229), hx(0xE8C89A), hx(0xEFEAE0), hx(0xEFEAE0), hx(0xFF4B2E), hx(0x3A1712) },
        // Oracle
        { hx(0x1A130E), hx(0x241A12), hx(0xC8A15A), hx(0xEDE3CE), hx(0xEDE3CE), hx(0xFFB020), hx(0x3A2A0E) },
        // Mono
        { hx(0x0E0E10), hx(0x17181B), hx(0xC0C6CC), hx(0xE6E8EA), hx(0xE6E8EA), hx(0xFF3838), hx(0x3A1414) },
        // Modular
        { hx(0x121314), hx(0x1C1E20), hx(0x7FC8A9), hx(0xDDE2E0), hx(0xDDE2E0), hx(0x66E0A0), hx(0x123A2A) },
        // Prism
        { hx(0x071618), hx(0x0C2226), hx(0x2FD9C9), hx(0xCFEFEA), hx(0xCFEFEA), hx(0x24E0D0), hx(0x0E3A38) },
        // Acid (silver panel; dark on-panel ink)
        { hx(0x16171A), hx(0xC8CBD0), hx(0xFF5A00), hx(0xEDEFF2), hx(0x202226), hx(0xFF2A2A), hx(0x5A1414) },
    };
    const char* const kModeNames[6] = { "COSMOS", "ORACLE", "MONO", "MODULAR", "PRISM", "ACID" };

    // Combo option tables (labels only; no trademarks).
    const char* const kWave5[]    = { "Saw", "Square", "Triangle", "Sine", "Pulse" };
    const char* const kWave4[]    = { "Saw", "Square", "Triangle", "Sine" };
    const char* const kSubWave[]  = { "Square", "Sine" };
    const char* const kEnvCurve[] = { "Linear", "Exp", "Log", "Analog" };
    const char* const kArpMode[]  = { "Up", "Down", "Up/Down", "Down/Up", "Random", "Order", "Chord" };
    const char* const kDivName[]  = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                      "1/2.", "1/4.", "1/8.", "1/16.", "1/2T", "1/4T", "1/8T", "1/16T" };
    const char* const kArpVel[]   = { "As Played", "Fixed", "Accent" };
    const char* const kLfoShape[] = { "Sine", "Triangle", "Square", "S&H", "Random" };
    const char* const kDriveType[]= { "Soft", "Hard", "Tube" };
    const char* const kChorusOpt[]= { "Off", "I", "II", "I+II" };
    const char* const kGlide[]    = { "Time", "Rate" };
    const char* const kVelCurve[] = { "Linear", "Soft", "Hard", "S-Curve" };
    const char* const kOversmp[]  = { "1x", "2x", "4x" };
    const char* const kModSrc[]   = { "None", "LFO 1", "LFO 2", "Filt Env", "Mod Whl", "After.",
                                      "Velocity", "Key Trk", "Random", "P.Bend", "S&H" };
    const char* const kModDst[]   = { "None", "Osc1 Pitch", "Osc2 Pitch", "Osc1 PW", "Osc2 PW",
                                      "Cutoff", "Reso", "Amp", "Pan", "LFO1 Rate", "LFO2 Rate",
                                      "FX Mix", "Uni Det" };
}

class MultiSynthUI : public UI, public duskdpf::ParamHost
{
public:
    //--- duskdpf::ParamHost -----------------------------------------------------
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

    MultiSynthUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kNumCoreParams; ++i)
            defaults[i] = values[i] = kParamDefs[i].def;
        // Meters default to silence (-60 dBFS); 0.0f would read as 0 dBFS (full
        // bar + clip LED) on the fallback path before the host pushes real values.
        defaults[kParamOutLevelL] = values[kParamOutLevelL] = -60.0f;
        defaults[kParamOutLevelR] = values[kParamOutLevelR] = -60.0f;

        setGeometryConstraints((uint32_t)(kDesignW * 0.5f), (uint32_t)(kDesignH * 0.5f), true);

        static const float kFontSizes[] = { 9.f, 10.f, 11.f, 12.f, 13.f, 15.f, 20.f, 26.f };
        // The crisp font atlas is baked once here at the current scale factor.
        // U6 (skipped, documented limitation): rebuilding it on a live DPI change
        // is not safe with this DPF ImGui wrapper — the wrapper captures the scale
        // factor at construction and never fires a scale-change hook, and an
        // external io.Fonts->Clear()/Build() would (a) dangle the ImFont* faces
        // held in fontSet and (b) leave the GL backend on a stale font texture
        // (ImGui_ImplOpenGL3_NewFrame only recreates it after an explicit
        // ImGui_ImplOpenGL3_DestroyFontsTexture, which the wrapper does not
        // expose). Net effect: text is slightly blurry after a monitor-DPI change
        // until the plugin window is reopened. Fixing it properly needs a DPF /
        // shared-dpf change (out of scope here).
        fontSet = duskdpf::loadCrispFontSet(kFontSizes, 8, getScaleFactor());
        panel.setFontSet(fontSet);

        buildTooltips();

        presetStore.refresh();   // scan the user preset dir once at construction

        curMode = clampMode((int)std::lround(values[kParamMode]));
        prevMode = curMode;
        live = kPalettes[curMode];
        fromPal = live;
        modeBlend = 1.0f;
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount)
            values[index] = value;
    }

    // U1: the host loaded a factory program (preset). Reflect it in the name
    // display; the preset's actual parameter values arrive via parameterChanged
    // and drive the mode crossfade, so nothing else is needed here.
    void programLoaded(uint32_t index) override
    {
        if (index < (uint32_t)kNumFactoryPresets)
            currentPreset = (int)index;
    }

    void onImGuiDisplay() override
    {
        const auto _t0 = std::chrono::high_resolution_clock::now();
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s   = std::min(winW / kDesignW, winH / kDesignH);
        org = ImVec2(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));

        // ---- mode crossfade (spec §5) ----
        const int m = clampMode((int)std::lround(values[kParamMode]));
        if (m != curMode) { fromPal = live; prevMode = curMode; curMode = m; modeBlend = 0.0f; }
        const float dt = ImGui::GetIO().DeltaTime;
        modeBlend = std::min(1.0f, modeBlend + (dt > 0.f ? dt : 0.016f) / 0.28f);
        const float e = modeBlend * modeBlend * (3.0f - 2.0f * modeBlend);
        live = blendPal(fromPal, kPalettes[curMode], e);

        // Push the blended palette into the shared panel (on-panel ink + accent).
        duskdpf::Palette pp;
        pp.white    = live.textPanel;
        pp.whiteDim = lerpC(live.textPanel, live.panel, 0.38f);
        pp.accent   = live.accent;
        pp.ledOn    = live.ledOn;
        pp.ledOff   = live.ledOff;
        pp.ledGlow  = withA(live.ledOn, 90);
        panel.setPalette(pp);
        panel.begin(s, org, fontSet.primary(), this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        // The DPF DearImGui backend has no VtxOffset support, so a single window
        // draw list corrupts past 65535 vertices (ImDrawIdx is 16-bit). This UI's
        // ~200 chrome primitives blow past that, so the frame is split into a few
        // non-overlapping, borderless layer windows — each keeps its own draw list
        // well under budget, and because they don't overlap, hover/input is clean.

        // background: drawn into ImGui's dedicated background draw list, which is
        // always rendered behind every window (so it can't occlude the layers).
        dl = ImGui::GetBackgroundDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(6, 6, 7, 255));
        drawChassis();
        drawWoodCheeks();
        dl->AddRectFilled(P(0, 0), P(kDesignW, 3), metalCol());
        dl->AddLine(P(0, 54), P(kDesignW, 54), IM_COL32(70, 70, 72, 255), 1.5f * s);

        // The MOD MATRIX is a modal. DPF's ImGui integration does not render an
        // overlapping window on top of the base layers, so while the modal is open
        // it REPLACES the base panels (single window, no overlap) over a dark
        // scrim, rather than floating above them. Closing it restores the panels.
        if (showSaveModal)
        {
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(0, 0, 0, 170)); // scrim on bg list
            beginLayerScreen("MSsave", 0, 0, winW, winH, true);
            drawSaveModalOverlay();
            ImGui::End();
            ImGui::PopStyleVar(2);
           #ifdef MSYNTH_FRAME_PROFILE
            profileFrame(_t0);
           #endif
            return;
        }

        if (showMod)
        {
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(0, 0, 0, 170)); // scrim on bg list
            beginLayerScreen("MSmodal", 0, 0, winW, winH, true);
            drawModMatrixOverlay();
            ImGui::End();
            ImGui::PopStyleVar(2);
           #ifdef MSYNTH_FRAME_PROFILE
            profileFrame(_t0);
           #endif
            return;
        }

        beginLayer("MStop", 0, 0, kDesignW, 55);
        drawTopBar();
        ImGui::End();

        beginLayer("MSleft", 0, 55, 346, 545);
        if (curMode == 4) drawPrismOps();   // Prism swaps oscillators for the op matrix
        else              drawOscPanels();
        drawMixerVoice();
        ImGui::End();

        beginLayer("MScenter", 346, 55, 756, 545);
        drawFilter();
        drawEnvelopes();
        ImGui::End();

        beginLayer("MSright", 756, 55, kDesignW, 545);
        drawLFOs();
        drawModeSubPanelRegion();
        drawModMatrixBar();
        drawScope();
        drawOutputVU();
        ImGui::End();

        beginLayer("MSbottom", 0, 545, kDesignW, kDesignH);
        drawSequencer();
        drawFXStrip();
        drawKeyboard();
        ImGui::End();

        ImGui::PopStyleVar(2);
       #ifdef MSYNTH_FRAME_PROFILE
        profileFrame(_t0);
       #else
        (void)_t0;
       #endif
    }

   #ifdef MSYNTH_FRAME_PROFILE
    void profileFrame(std::chrono::high_resolution_clock::time_point t0)
    {
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (profN >= 0 && profN < 100)
        { profLogic[profN] = ms; profTotal[profN] = ImGui::GetIO().DeltaTime * 1000.0; }
        if (++profN == 100)
        {
            double a[100], b[100];
            std::memcpy(a, profLogic, sizeof a); std::memcpy(b, profTotal, sizeof b);
            std::sort(a, a + 100); std::sort(b, b + 100);
            std::fprintf(stderr, "MSYNTH_FRAME logic_median=%.3fms total_median=%.3fms (100 frames)\n",
                         a[50], b[50]);
        }
    }
   #endif

    // Begin a borderless, transparent layer window over a design-space rect.
    void beginLayer(const char* name, float dx0, float dy0, float dx1, float dy1, bool inputs = true)
    { beginLayerScreen(name, org.x + dx0 * s, org.y + dy0 * s, org.x + dx1 * s, org.y + dy1 * s, inputs); }

    void beginLayerScreen(const char* name, float x0, float y0, float x1, float y1, bool inputs)
    {
        ImGui::SetNextWindowPos(ImVec2(x0, y0));
        ImGui::SetNextWindowSize(ImVec2(x1 - x0, y1 - y0));
        ImGuiWindowFlags f = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing;
        if (!inputs) f |= ImGuiWindowFlags_NoInputs;
        ImGui::Begin(name, nullptr, f);
        dl = ImGui::GetWindowDrawList();
    }

private:
    //========================================================================
    // small helpers
    //========================================================================
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }
    static int clampMode(int m) { return m < 0 ? 0 : (m > 5 ? 5 : m); }
    static ImU32 withA(ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)a << 24); }
    static ImU32 mulA(ImU32 c, float f)
    { int a = (int)(((c >> IM_COL32_A_SHIFT) & 255) * f); if (a < 0) a = 0; if (a > 255) a = 255;
      return (c & 0x00FFFFFFu) | ((ImU32)a << IM_COL32_A_SHIFT); }

    static ImU32 lerpC(ImU32 a, ImU32 b, float t)
    {
        auto ch = [](ImU32 c, int sh) { return (int)((c >> sh) & 255); };
        auto L = [&](int sh) { return (int)(ch(a, sh) + (ch(b, sh) - ch(a, sh)) * t + 0.5f); };
        return IM_COL32(L(IM_COL32_R_SHIFT), L(IM_COL32_G_SHIFT), L(IM_COL32_B_SHIFT), 255);
    }
    static MSPal blendPal(const MSPal& a, const MSPal& b, float t)
    {
        return { lerpC(a.bg, b.bg, t), lerpC(a.panel, b.panel, t), lerpC(a.accent, b.accent, t),
                 lerpC(a.text, b.text, t), lerpC(a.textPanel, b.textPanel, t),
                 lerpC(a.ledOn, b.ledOn, t), lerpC(a.ledOff, b.ledOff, t) };
    }
    ImU32 metalCol() const // bevel frame: lighten dark panels, darken light panels
    {
        auto ch = [](ImU32 c, int sh) { return (int)((c >> sh) & 255); };
        const int r = ch(live.panel, IM_COL32_R_SHIFT), g = ch(live.panel, IM_COL32_G_SHIFT), b = ch(live.panel, IM_COL32_B_SHIFT);
        const int lum = (r * 30 + g * 59 + b * 11) / 100;
        if (lum > 150) // light panel (Acid silver): darker frame so the bevel reads with contrast
            return IM_COL32((int)(r * 0.42f), (int)(g * 0.42f), (int)(b * 0.42f), 255);
        auto U = [&](int v) { return v + (255 - v) / 2; };
        return IM_COL32(U(r), U(g), U(b), 255);
    }

    void text(float x, float y, float sz, ImU32 col, const char* t, int align, bool bold = false)
    { panel.text(dl, x, y, sz, col, t, align, bold); }

    void panelBox(float x0, float y0, float x1, float y1, float alpha = 1.0f)
    {
        dl->AddRectFilled(P(x0 - 3, y0 - 3), P(x1 + 3, y1 + 3), mulA(metalCol(), alpha), 8.0f * s);
        dl->AddRectFilled(P(x0, y0), P(x1, y1), mulA(live.panel, alpha), 6.0f * s);
        // engraved bevel: light top-edge highlight + dark bottom-edge shade
        dl->AddLine(P(x0 + 2, y0 + 1), P(x1 - 2, y0 + 1),
                    mulA(lerpC(live.panel, IM_COL32(255, 255, 255, 255), 0.22f), alpha), 1.0f * s);
        dl->AddLine(P(x0 + 2, y1 - 1), P(x1 - 2, y1 - 1),
                    mulA(lerpC(live.panel, IM_COL32(0, 0, 0, 255), 0.35f), alpha), 1.0f * s);
    }
    void sectionTitle(float x, float y, const char* t)
    {
        text(x + 0.6f, y + 1.0f, 11.0f, withA(IM_COL32(0, 0, 0, 255), 120), t, -1, true); // engraved shadow
        text(x, y, 11.0f, live.accent, t, -1, true);
    }
    void drawX(float cx, float cy, float r, ImU32 col) // close/clear glyph (no exotic font glyph)
    { dl->AddLine(P(cx - r, cy - r), P(cx + r, cy + r), col, 1.6f * s);
      dl->AddLine(P(cx - r, cy + r), P(cx + r, cy - r), col, 1.6f * s); }
    void klabel(float cx, float topY, const char* l) { text(cx, topY, 10.0f, live.textPanel, l, 0, true); }

    // Mode-accent value arc drawn just outside the shared knob body (spec §3.1).
    void accentArc(float cx, float cy, float r, float t, bool bipolar, float anchorT = -1.0f)
    {
        const ImVec2 c = P(cx, cy);
        const float R = (r + 3.0f) * s;
        const float t0 = anchorT >= 0.0f ? anchorT : (bipolar ? 0.5f : 0.0f);
        const float a = t0, b = t;
        const int N = 24;
        ImVec2 pts[N + 1]; int n = 0;
        for (int i = 0; i <= N; ++i)
        {
            const float tt = a + (b - a) * (float)i / (float)N;
            const float ang = duskdpf::DuskPanel::knobAngle(tt);
            pts[n++] = ImVec2(c.x + std::sin(ang) * R, c.y - std::cos(ang) * R);
        }
        if (n >= 2) dl->AddPolyline(pts, n, live.accent, 0, 2.4f * s);
    }

    ImU32 whiteDimCol() const { return lerpC(live.textPanel, live.panel, 0.38f); }

    // Chrome knob body (matches duskdpf::DuskPanel's chrome exactly) so the local
    // skew/ratio knobs are visually identical to the shared linear knobs.
    void drawKnobChrome(ImVec2 c, float R, float t, bool ticks = true)
    {
        if (ticks)
        for (int i = 0; i <= 10; ++i)
        {
            const float a = duskdpf::DuskPanel::knobAngle((float)i / 10.0f);
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(c.x + dir.x * (R + 2.5f * s), c.y + dir.y * (R + 2.5f * s)),
                        ImVec2(c.x + dir.x * (R + 6.5f * s), c.y + dir.y * (R + 6.5f * s)),
                        whiteDimCol(), 1.3f * s);
        }
        dl->AddCircleFilled(c, R, IM_COL32(70, 70, 73, 255), 48);
        dl->AddCircleFilled(c, R * 0.97f, IM_COL32(128, 128, 132, 255), 48);
        for (int i = 0; i < 24; ++i)
        {
            const float a = (float)i / 24.0f * 2.0f * kPi;
            const ImVec2 dir(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(c.x + dir.x * R * 0.82f, c.y + dir.y * R * 0.82f),
                        ImVec2(c.x + dir.x * R * 0.97f, c.y + dir.y * R * 0.97f),
                        IM_COL32(55, 55, 58, 130), 1.4f * s);
        }
        const float capR = R * 0.72f;
        dl->AddCircleFilled(c, capR, IM_COL32(96, 97, 100, 255), 40);
        dl->AddCircleFilled(ImVec2(c.x - capR * 0.15f, c.y - capR * 0.20f), capR * 0.93f, IM_COL32(176, 178, 182, 255), 40);
        dl->AddCircleFilled(ImVec2(c.x - capR * 0.25f, c.y - capR * 0.32f), capR * 0.55f, IM_COL32(225, 227, 231, 150), 40);
        dl->AddCircleFilled(c, capR * 0.42f, IM_COL32(158, 160, 164, 255), 40);
        dl->AddCircle(c, capR, IM_COL32(20, 20, 20, 255), 40, 1.4f * s);
        const float a = duskdpf::DuskPanel::knobAngle(t);
        const ImVec2 dir(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + dir.x * capR * 0.15f, c.y + dir.y * capR * 0.15f),
                    ImVec2(c.x + dir.x * capR * 0.95f, c.y + dir.y * capR * 0.95f),
                    IM_COL32(25, 25, 27, 255), 3.0f * s);
    }

    // Format a value for a knob bubble/readout, operating on the DISPLAY value
    // (values[p]*dmul+dadd). Auto-switches units on magnitude (spec §3.1a):
    //   " Hz" >= 1000            -> "%.2f kHz"
    //   " ms" >= 1000 (timeAuto) -> "%.2f s"
    //   " s"  <  1    (timeAuto) -> "%.0f ms"
    // timeAuto is opt-in per knob so families with their own convention (e.g.
    // reverb decay) keep their fixed suffix.
    void fmtVal(char* buf, int n, uint32_t p, const char* fmt, const char* suffix,
                float dmul, float dadd, bool timeAuto = false)
    {
        const float disp = values[p] * dmul + dadd;
        if (suffix)
        {
            if (std::strcmp(suffix, " Hz") == 0 && disp >= 1000.0f)
            { std::snprintf(buf, n, "%.2f kHz", disp / 1000.0f); return; }
            if (timeAuto && std::strcmp(suffix, " ms") == 0 && disp >= 1000.0f)
            { std::snprintf(buf, n, "%.2f s", disp / 1000.0f); return; }
            if (timeAuto && std::strcmp(suffix, " s") == 0 && disp < 1.0f)
            { std::snprintf(buf, n, "%.0f ms", disp * 1000.0f); return; }
        }
        char num[32]; std::snprintf(num, sizeof(num), fmt, disp);
        std::snprintf(buf, n, "%s%s", num, suffix ? suffix : "");
    }

    // Local log-taper knob (spec feel-fix): maps the vertical drag and pointer
    // position through log space for LOG-skew params (freq/time), so the low end
    // of the range gets proportional resolution. Reuses the shared panel's value
    // bubble + inline editor; the shared DuskPanel::knob is left untouched.
    bool knobSkewed(const char* id, uint32_t p, float cx, float cy, float r,
                    const char* fmt, const char* suffix, bool bipolar, bool persist,
                    float dmul, float dadd, bool ticks = true, bool timeAuto = false)
    {
        const ParamDef& d = kParamDefs[p];
        const bool logv = (d.kind == PK_LOG && d.min > 0.0f);
        const float lmin = logv ? std::log(d.min) : d.min;
        const float lmax = logv ? std::log(d.max) : d.max;
        const float lrange = lmax - lmin;
        auto toL   = [&](float v){ v = v < d.min ? d.min : (v > d.max ? d.max : v); return logv ? std::log(v) : v; };
        auto fromL = [&](float L){ float v = logv ? std::exp(L) : L; return v < d.min ? d.min : (v > d.max ? d.max : v); };

        const float R = r * s;
        const ImVec2 c = P(cx, cy);
        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
        ImGui::InvisibleButton(id, ImVec2(2.0f * R, 2.0f * R));
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();
        const bool modKey  = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        const bool editing = panel.isEditingValue(id);
        bool changed = false;

        if (tips[p] && tips[p][0] && hovered && !active) ImGui::SetTooltip("%s", tips[p]);
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) { beginEdit(p); values[p] = d.def; setParam(p, d.def); endEdit(p); changed = true; skewReset = true; }
                else        { beginEdit(p); skewL = toL(values[p]); skewReset = false; }
            }
            if (active && !skewReset)
            {
                const float speed = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                skewL -= ImGui::GetIO().MouseDelta.y * speed * lrange;
                if (skewL < lmin) skewL = lmin; if (skewL > lmax) skewL = lmax;
                const float nv = fromL(skewL);
                if (nv != values[p]) { values[p] = nv; setParam(p, nv); changed = true; }
            }
            if (ImGui::IsItemDeactivated()) { if (!skewReset) endEdit(p); skewReset = false; }
            if (!modKey && (hovered || active) && ImGui::IsMouseDoubleClicked(0))
            { panel.openValueEdit(id, values[p] * dmul + dadd); endEdit(p); }
            if (hovered && !active)
            {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    float L = toL(values[p]) + wheel * lrange * 0.02f;
                    if (L < lmin) L = lmin; if (L > lmax) L = lmax;
                    const float nv = fromL(L);
                    if (nv != values[p]) { beginEdit(p); values[p] = nv; setParam(p, nv); endEdit(p); changed = true; }
                }
            }
        }

        const float t = lrange > 0.0f ? (toL(values[p]) - lmin) / lrange : 0.0f;
        drawKnobChrome(c, R, t, ticks);
        accentArc(cx, cy, r, t, bipolar);

        float typed;
        if (panel.valueEdit(id, cx, cy, r, typed))
        {
            typed = (typed - dadd) / (dmul != 0.0f ? dmul : 1.0f);
            typed = typed < d.min ? d.min : (typed > d.max ? d.max : typed);
            if (typed != values[p]) { beginEdit(p); values[p] = typed; setParam(p, typed); endEdit(p); changed = true; }
        }
        else if ((hovered || active) && !panel.isEditingValue(id))
        {
            if (active) { char buf[48]; fmtVal(buf, sizeof buf, p, fmt, suffix, dmul, dadd, timeAuto); panel.valueBubble(dl, cx, cy, r, buf); }
            else        panel.valueBubble(dl, cx, cy, r, d.name);
        }
        if (persist && !panel.isEditingValue(id))
        { char buf[48]; fmtVal(buf, sizeof buf, p, fmt, suffix, dmul, dadd, timeAuto); text(cx, cy + r + 8.0f, 9.5f, whiteDimCol(), buf, 0); }
        return changed;
    }

    // Prism op-ratio snap list (classic FM ratios); plain drag snaps, Shift = fine.
    static const float* ratioList(int& n) { static const float L[] = { 0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }; n = 20; return L; }
    static float snapRatio(float v)
    { int n; const float* L = ratioList(n); float best = L[0], bd = 1e9f;
      for (int i = 0; i < n; ++i) { float dd = std::fabs(L[i] - v); if (dd < bd) { bd = dd; best = L[i]; } } return best; }

    bool knobRatio(const char* id, uint32_t p, float cx, float cy, float r, bool ticks = true)
    {
        const ParamDef& d = kParamDefs[p];               // 0.25..16 LOG
        const float lmin = std::log(d.min), lmax = std::log(d.max), lrange = lmax - lmin;
        const float R = r * s; const ImVec2 c = P(cx, cy);
        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
        ImGui::InvisibleButton(id, ImVec2(2.0f * R, 2.0f * R));
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();
        const bool modKey  = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        const bool editing = panel.isEditingValue(id);
        bool changed = false;
        if (tips[p] && tips[p][0] && hovered && !active) ImGui::SetTooltip("%s", tips[p]);
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) { beginEdit(p); values[p] = d.def; setParam(p, d.def); endEdit(p); changed = true; skewReset = true; }
                else        { beginEdit(p); skewL = std::log(values[p] < d.min ? d.min : values[p]); skewReset = false; }
            }
            if (active && !skewReset)
            {
                const bool fine = ImGui::GetIO().KeyShift;
                skewL -= ImGui::GetIO().MouseDelta.y * (fine ? 0.0016f : 0.006f) * lrange;
                if (skewL < lmin) skewL = lmin; if (skewL > lmax) skewL = lmax;
                float nv = std::exp(skewL);
                if (!fine) nv = snapRatio(nv);           // snap to the classic list on plain drag
                nv = nv < d.min ? d.min : (nv > d.max ? d.max : nv);
                if (nv != values[p]) { values[p] = nv; setParam(p, nv); changed = true; }
            }
            if (ImGui::IsItemDeactivated()) { if (!skewReset) endEdit(p); skewReset = false; }
            if (!modKey && (hovered || active) && ImGui::IsMouseDoubleClicked(0))
            { panel.openValueEdit(id, values[p]); endEdit(p); }
            if (hovered && !active)
            {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f)
                {
                    int n; const float* L = ratioList(n);
                    int idx = 0; float bd = 1e9f;
                    for (int i = 0; i < n; ++i) { float dd = std::fabs(L[i] - values[p]); if (dd < bd) { bd = dd; idx = i; } }
                    idx += wheel > 0 ? 1 : -1; if (idx < 0) idx = 0; if (idx > n - 1) idx = n - 1;
                    if (L[idx] != values[p]) { beginEdit(p); values[p] = L[idx]; setParam(p, L[idx]); endEdit(p); changed = true; }
                }
            }
        }
        const float t = (std::log(values[p] < d.min ? d.min : values[p]) - lmin) / lrange;
        drawKnobChrome(c, R, t, ticks);
        accentArc(cx, cy, r, t, false);
        float typed;
        if (panel.valueEdit(id, cx, cy, r, typed))
        {
            typed = typed < d.min ? d.min : (typed > d.max ? d.max : typed);
            if (typed != values[p]) { beginEdit(p); values[p] = typed; setParam(p, typed); endEdit(p); changed = true; }
        }
        else if ((hovered || active) && !panel.isEditingValue(id))
        { char buf[24]; std::snprintf(buf, sizeof buf, "%.2f\xC3\x97", values[p]); panel.valueBubble(dl, cx, cy, r, buf); }
        return changed;
    }

    // Chrome knob bound to a param, deriving range/name/tooltip from metadata.
    // LOG-skew params route through the local log-taper knob (spec feel-fix).
    bool knob(const char* id, uint32_t p, float cx, float cy, float r,
              const char* fmt = "%.2f", const char* suffix = "",
              bool bipolar = false, bool stepped = false, bool persist = false,
              float dmul = 1.0f, float dadd = 0.0f, bool ticks = true,
              bool timeAuto = false, bool anchorZero = false)
    {
        const ParamDef& d = kParamDefs[p];
        // LOG-skew params, and any time knob wanting the ms/s auto-switch, route
        // through the local taper knob (which formats via fmtVal).
        if (!stepped && ((d.kind == PK_LOG && d.min > 0.0f) || timeAuto))
            return knobSkewed(id, p, cx, cy, r, fmt, suffix, bipolar, persist, dmul, dadd, ticks, timeAuto);
        const bool ch = panel.knob(id, p, d.min, d.max, cx, cy, r, values[p], defaults[p],
                                   stepped, ticks, fmt, suffix, 0, false, persist,
                                   tips[p], false, dmul, dadd, d.name);
        const float t = (d.max > d.min) ? (values[p] - d.min) / (d.max - d.min) : 0.0f;
        // masterVol's bipolar arc anchors at the true 0 dB point, not the geometric
        // mid; symmetric bipolar ranges are unaffected (t0=0.5 either way).
        const float anchorT = (anchorZero && d.max > d.min) ? (0.0f - d.min) / (d.max - d.min) : -1.0f;
        accentArc(cx, cy, r, t, bipolar, anchorT);
        return ch;
    }

    // A knob a mode renders but does not use: drawn dimmed (~45% alpha) and inert
    // (no drag), with a tooltip explaining why. Value arc still reflects the stored
    // setting so switching to a mode that DOES use it shows the current value. (U2)
    void inertKnob(const char* id, uint32_t p, float cx, float cy, float r, bool bipolar,
                   const char* whyTip)
    {
        const ParamDef& d = kParamDefs[p];
        const float t = (d.max > d.min) ? (values[p] - d.min) / (d.max - d.min) : 0.0f;
        const int A = 115; // ~45% alpha, matching the other dead-control affordances
        const ImVec2 c = P(cx, cy);
        const float R = r * s;
        dl->AddCircleFilled(c, R, withA(IM_COL32(40, 40, 43, 255), A), 28);
        dl->AddCircle(c, R, withA(IM_COL32(90, 90, 94, 255), A), 28, 1.4f * s);
        // value arc (dimmed) + indicator
        const float t0 = bipolar ? 0.5f : 0.0f;
        const int N = 20; ImVec2 pts[N + 1];
        for (int i = 0; i <= N; ++i)
        {
            const float tt = t0 + (t - t0) * (float)i / (float)N;
            const float ang = duskdpf::DuskPanel::knobAngle(tt);
            pts[i] = ImVec2(c.x + std::sin(ang) * (R + 3.0f * s), c.y - std::cos(ang) * (R + 3.0f * s));
        }
        dl->AddPolyline(pts, N + 1, withA(live.accent, A), 0, 2.2f * s);
        const float ia = duskdpf::DuskPanel::knobAngle(t);
        dl->AddLine(c, ImVec2(c.x + std::sin(ia) * R * 0.8f, c.y - std::cos(ia) * R * 0.8f),
                    withA(live.text, A), 2.0f * s);
        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
        ImGui::InvisibleButton(id, ImVec2(2.0f * R, 2.0f * R));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", whyTip);
    }

    void setChoice(uint32_t p, int v)
    { const float nv = (float)v; beginEdit(p); values[p] = nv; setParam(p, nv); endEdit(p); }

    // forceDisplayIdx >= 0 overrides the CLOSED preview label (used when the engine
    // forces a fixed waveform in a mode) while the dropdown still selects/writes
    // the real param for other modes (U2).
    void comboBox(const char* id, uint32_t p, float x0, float y0, float x1, float y1,
                  const char* const* opts, int nopts, bool acid = false, int forceDisplayIdx = -1)
    {
        int idx = (int)std::lround(values[p]); idx = idx < 0 ? 0 : (idx >= nopts ? nopts - 1 : idx);
        const int shownIdx = (forceDisplayIdx >= 0 && forceDisplayIdx < nopts) ? forceDisplayIdx : idx;
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::SetNextItemWidth((x1 - x0) * s);
        ImFont* f = panel.pickFont(12.0f * s);
        ImGui::PushFont(f);
        float padY = ((y1 - y0) * s - f->FontSize) * 0.5f; if (padY < 1.0f) padY = 1.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, padY));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,  acid ? IM_COL32(70, 72, 78, 255) : IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,  IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header,   withA(live.accent, 150));
        ImGui::PushStyleColor(ImGuiCol_Text,     IM_COL32(235, 238, 242, 255));
        char cid[40]; std::snprintf(cid, sizeof(cid), "##%s", id);
        if (ImGui::BeginCombo(cid, opts[shownIdx]))
        {
            for (int i = 0; i < nopts; ++i)
                if (ImGui::Selectable(opts[i], i == idx)) setChoice(p, i);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered() && tips[p]) ImGui::SetTooltip("%s", tips[p]);
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }

    // Bool toggle with a lamp. Acid variant draws a round colored button.
    void ledButton(const char* id, uint32_t p, float x0, float y0, float x1, float y1,
                   const char* label, bool acid = false)
    {
        const bool on = values[p] > 0.5f;
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) setChoice(p, on ? 0 : 1);
        if (ImGui::IsItemHovered() && tips[p]) ImGui::SetTooltip("%s", tips[p]);
        if (acid)
        {
            const ImVec2 c((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
            const float rr = std::min(b1.x - b0.x, b1.y - b0.y) * 0.42f;
            dl->AddCircleFilled(c, rr, on ? live.ledOn : IM_COL32(150, 152, 158, 255), 24);
            dl->AddCircle(c, rr, IM_COL32(40, 42, 46, 255), 24, 1.4f * s);
            text((x0 + x1) * 0.5f, y1 + 1.0f, 9.0f, live.textPanel, label, 0, on);
            return;
        }
        dl->AddRectFilled(b0, b1, IM_COL32(40, 40, 43, 255), 3.0f * s);
        dl->AddRect(b0, b1, on ? withA(live.accent, 255) : IM_COL32(90, 90, 94, 255), 3.0f * s, 0, 1.4f * s);
        panel.led(dl, x0 + 8.0f, 0.5f * (y0 + y1), on, 3.2f);
        // label sits on the dark button, so always draw it light regardless of skin
        text(0.5f * (x0 + x1) + 8.0f, y0 + 0.30f * (y1 - y0), 10.0f,
             on ? IM_COL32(238, 238, 240, 255) : IM_COL32(150, 150, 154, 255), label, 0, on);
    }

    //========================================================================
    // Top bar (nameplate, mode rockers, preset browser)
    //========================================================================
    void drawTopBar()
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, 54), mulA(live.bg, 1.0f));
        dl->AddRectFilled(P(0, 0), P(kDesignW, 3), metalCol());
        dl->AddLine(P(0, 54), P(kDesignW, 54), IM_COL32(70, 70, 72, 255), 1.5f * s);

        text(18, 8, 20.0f, live.text, "SUNSET CIRCUITS", -1, true);
        text(20, 32, 11.0f, live.accent, "Dusk Audio", -1, true);

        // Mode rockers ×6 (spec §8.1)
        for (int i = 0; i < 6; ++i)
        {
            const float x0 = 306.0f + i * 106.0f, x1 = x0 + 100.0f, y0 = 10.0f, y1 = 46.0f;
            const bool sel = (i == curMode);
            char id[16]; std::snprintf(id, sizeof(id), "rocker%d", i);
            const ImVec2 p0 = P(x0, y0), p1 = P(x1, y1);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton(id, ImVec2(p1.x - p0.x, p1.y - p0.y));
            if (ImGui::IsItemClicked() && !sel) setChoice(kParamMode, i);
            if (ImGui::IsItemHovered())
            { char tt[40]; std::snprintf(tt, sizeof(tt), "Switch to %s mode", kModeNames[i]); ImGui::SetTooltip("%s", tt); }
            dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 27, 255), 5.0f * s);
            if (sel)
            {
                dl->AddRectFilled(p0, p1, withA(live.accent, 46), 5.0f * s);
                dl->AddRect(p0, p1, live.accent, 5.0f * s, 0, 1.6f * s);
            }
            else
                dl->AddRect(p0, p1, IM_COL32(70, 70, 74, 255), 5.0f * s, 0, 1.2f * s);
            panel.led(dl, x0 + 11.0f, 0.5f * (y0 + y1), sel, 3.0f);
            text(0.5f * (x0 + x1) + 8.0f, y0 + 11.0f, 12.0f,
                 sel ? live.text : lerpC(live.text, live.bg, 0.35f), kModeNames[i], 0, sel);
        }

        // Preset prev / combo / next / save. currentPreset is a combined index:
        // factory [0..kNumFactoryPresets) then user [kNumFactoryPresets..total).
        const char* preview = presetName(currentPreset);
        if (chevron("presetPrev", 952, 14, 978, 42, false, "Previous preset"))
            applyCombined(currentPreset < 0 ? comboTotal() - 1 : currentPreset - 1);

        ImGui::SetCursorScreenPos(P(982, 14));
        ImGui::SetNextItemWidth(168.0f * s);
        ImFont* f = panel.pickFont(12.0f * s);
        ImGui::PushFont(f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, (28.0f * s - f->FontSize) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header,  withA(live.accent, 150));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(235, 238, 242, 255));
        if (ImGui::BeginCombo("##presets", preview))
        {
            for (int i = 0; i < kNumFactoryPresets; ++i)
            {
                const bool sel = i == currentPreset;
                if (ImGui::Selectable(kFactoryPresets[i].name, sel)) applyPreset(i);
                if (sel) ImGui::SetItemDefaultFocus();   // auto-scroll to selection on open
            }
            const auto& users = presetStore.list();
            if (!users.empty())
            {
                ImGui::Separator();
                for (int u = 0; u < (int)users.size(); ++u)
                {
                    ImGui::PushID(u);   // user names may duplicate factory names
                    const bool sel = currentPreset == kNumFactoryPresets + u;
                    if (ImGui::Selectable(users[u].name.c_str(), sel)) applyUserPreset(u);
                    if (sel) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select a factory or user preset");
        ImGui::PopStyleColor(4); ImGui::PopStyleVar(); ImGui::PopFont();

        if (chevron("presetNext", 1154, 14, 1180, 42, true, "Next preset"))
            applyCombined(currentPreset < 0 ? 0 : currentPreset + 1);

        // Save ★ — opens the user-preset save modal (writes the current patch).
        const ImVec2 s0 = P(1186, 14), s1 = P(1222, 42);
        ImGui::SetCursorScreenPos(s0);
        ImGui::InvisibleButton("presetSave", ImVec2(s1.x - s0.x, s1.y - s0.y));
        const bool saveClicked = ImGui::IsItemClicked();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the current patch as a user preset");
        dl->AddRectFilled(s0, s1, IM_COL32(38, 38, 41, 255), 4.0f * s);
        dl->AddRect(s0, s1, ImGui::IsItemHovered() ? live.accent : IM_COL32(90, 90, 94, 255),
                    4.0f * s, 0, 1.2f * s);
        text(1204, 21, 9.0f, live.accent, "SAVE", 0, true);
        if (saveClicked) openSaveModal();
    }

    //========================================================================
    // User preset helpers (combined factory+user index space)
    //========================================================================
    int userCount()  const { return (int)presetStore.list().size(); }
    int comboTotal() const { return kNumFactoryPresets + userCount(); }

    const char* presetName(int combined) const
    {
        if (combined >= 0 && combined < kNumFactoryPresets)
            return kFactoryPresets[combined].name;
        const int u = combined - kNumFactoryPresets;
        if (u >= 0 && u < userCount()) return presetStore.list()[u].name.c_str();
        return "Presets";
    }

    // Recall by combined index (wraps). Factory -> applyPreset; user -> file load.
    void applyCombined(int idx)
    {
        const int total = comboTotal();
        if (total <= 0) return;
        idx = ((idx % total) + total) % total;
        if (idx < kNumFactoryPresets) applyPreset(idx);
        else                          applyUserPreset(idx - kNumFactoryPresets);
    }

    // Load user preset u through the same reset-then-apply path as applyPreset:
    // the store fills a temp array with defaults then overrides parsed symbols,
    // and we push all core params so missing symbols land on their defaults.
    void applyUserPreset(int u)
    {
        const auto& L = presetStore.list();
        if (u < 0 || u >= (int)L.size()) return;
        float tmp[kNumCoreParams];
        if (!presetStore.loadInto(L[u].path, tmp, (int)kNumCoreParams)) return;
        for (uint32_t i = 0; i < kNumCoreParams; ++i) pushParam(i, tmp[i]);
        currentPreset = kNumFactoryPresets + u;
    }

    void openSaveModal()
    {
        showSaveModal = true;
        saveModalJustOpened = true;
        overwriteConfirm = false;
        deleteConfirm = false;
        saveNameBuf[0] = '\0';
    }

    // Write values[] to disk, refresh the list, and select the new preset.
    void commitSave()
    {
        const std::string nm = scpreset::sanitize(saveNameBuf);
        if (nm.empty()) return;
        if (presetStore.save(saveNameBuf, values, (int)kNumCoreParams))
        {
            presetStore.refresh();
            const auto& L = presetStore.list();
            for (int u = 0; u < (int)L.size(); ++u)
                if (L[u].name == nm) { currentPreset = kNumFactoryPresets + u; break; }
        }
        showSaveModal = false;
        overwriteConfirm = false;
    }

    void commitDelete()
    {
        if (currentPreset >= kNumFactoryPresets)
        {
            const int u = currentPreset - kNumFactoryPresets;
            const auto& L = presetStore.list();
            if (u >= 0 && u < (int)L.size()) presetStore.remove(L[u].path);
            presetStore.refresh();
            currentPreset = -1;
        }
        showSaveModal = false;
        deleteConfirm = false;
    }

    bool chevron(const char* id, float x0, float y0, float x1, float y1, bool right, const char* tip = nullptr)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool clk = ImGui::IsItemClicked();
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        dl->AddRectFilled(b0, b1, IM_COL32(38, 38, 41, 255), 4.0f * s);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 4.0f * s, 0, 1.2f * s);
        const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
        if (right) dl->AddTriangleFilled(P(cx - 4, cy - 6), P(cx - 4, cy + 6), P(cx + 5, cy), live.text);
        else       dl->AddTriangleFilled(P(cx + 4, cy - 6), P(cx + 4, cy + 6), P(cx - 5, cy), live.text);
        return clk;
    }

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumFactoryPresets) return;
        currentPreset = idx;
        // NOTE (U1): DPF's DistrhoUI has no API for a UI to ask the host to load a
        // program (only the host->UI programLoaded callback exists), so we mirror
        // the shell's loadProgram by pushing the preset's parameters directly.
        // Mirror the shell's loadProgram: default -> baseline -> preset overrides.
        for (uint32_t i = 0; i < kNumCoreParams; ++i) pushParam(i, kParamDefs[i].def);
        for (int r = 0; r < kBaselineRows; ++r)
            pushParam((uint32_t)kPresetBaseline[r].index, kPresetBaseline[r].value);
        const FactoryPreset& pr = kFactoryPresets[idx];
        for (int r = 0; r < pr.nRows; ++r)
            pushParam((uint32_t)pr.rows[r].index, pr.rows[r].value);
    }
    void pushParam(uint32_t i, float v)
    { editParameter(i, true); values[i] = v; setParameterValue(i, v); editParameter(i, false); }

    // Brushed-metal chassis: subtle vertical gradient + noise-free procedural
    // brushing lines, all derived from live.bg so it crossfades with the mode
    // palette (spec §4 / defect 8). Drawn into the background draw list.
    void drawChassis()
    {
        const ImU32 top = lerpC(live.bg, IM_COL32(255, 255, 255, 255), 0.05f);
        const ImU32 bot = lerpC(live.bg, IM_COL32(0, 0, 0, 255), 0.18f);
        dl->AddRectFilledMultiColor(P(0, 0), P(kDesignW, kDesignH), top, top, bot, bot);
        const ImU32 lite = withA(lerpC(live.bg, IM_COL32(255, 255, 255, 255), 0.5f), 8);
        const ImU32 dark = withA(IM_COL32(0, 0, 0, 255), 10);
        for (int x = 0; x < (int)kDesignW; x += 3)
            dl->AddLine(P((float)x, 0), P((float)x, kDesignH), (x & 1) ? lite : dark, 1.0f);
    }

    void drawWoodCheeks()
    {
        // Oracle decorative wood side cheeks; alpha follows how "Oracle" the blend is.
        float oa = 0.0f;
        if (curMode == 1) oa = modeBlend >= 1.0f ? 1.0f : modeBlend;
        else if (prevMode == 1 && modeBlend < 1.0f) oa = 1.0f - modeBlend;
        if (oa <= 0.01f) return;
        const ImU32 wood = mulA(hx(0x2A1C10), oa);
        dl->AddRectFilled(P(0, 54), P(14, kDesignH), wood);
        dl->AddRectFilled(P(1226, 54), P(kDesignW, kDesignH), wood);
        for (int i = 1; i < 12; ++i)
        {
            const float y = 54 + i * (kDesignH - 54) / 12.0f;
            dl->AddLine(P(0, y), P(14, y), mulA(hx(0x1B120A), oa), 1.0f * s);
            dl->AddLine(P(1226, y), P(kDesignW, y), mulA(hx(0x1B120A), oa), 1.0f * s);
        }
    }

    //========================================================================
    // LEFT column — oscillators / mixer / voice
    //========================================================================
    void drawOscPanels()
    {
        // OSC 1
        panelBox(16, 60, 340, 178);
        sectionTitle(24, 64, "OSC 1");
        comboBox("o1w", kParamOsc1Wave, 150, 64, 332, 84, kWave5, 5, curMode == 5);
        klabel(60, 96, "DETUNE");  knob("o1det", kParamOsc1Detune, 60, 138, 20, "%+.0f", " ct", true);
        klabel(130, 96, "PW");     knob("o1pw", kParamOsc1PW, 130, 138, 20, "%.0f", " %", false, false, false, 100.0f);
        klabel(200, 96, "LEVEL");  knob("o1lvl", kParamOsc1Level, 200, 138, 20, "%.0f", " %", false, false, false, 100.0f);
        // crossMod lives with the oscillators (Cosmos/Oracle only) — see report.
        if (curMode == 0 || curMode == 1)
        { klabel(285, 96, "X-MOD"); knob("xmod", kParamCrossMod, 285, 138, 20, "%.0f", " %", false, false, false, 100.0f); }

        // OSC 2
        panelBox(16, 182, 340, 300);
        sectionTitle(24, 186, "OSC 2");
        // Cosmos (mode 0) forces OSC 2 to a Pulse locked to OSC 1's frequency and
        // detune, so the combo shows "Pulse" and SEMI/DETUNE are inert here (PW +
        // LEVEL stay live). The wave param remains selectable to store a choice for
        // the other modes. Pulse is index 4 in kWave5. (U2)
        const bool cosmosOsc2 = (curMode == 0);
        comboBox("o2w", kParamOsc2Wave, 150, 186, 332, 206, kWave5, 5, curMode == 5,
                 cosmosOsc2 ? 4 : -1);
        klabel(56, 218, "SEMI");
        klabel(114, 218, "DETUNE");
        if (cosmosOsc2)
        {
            inertKnob("o2semi", kParamOsc2Semi, 56, 258, 18, true,
                      "Inactive in Cosmos: OSC 2 tracks OSC 1's pitch.");
            inertKnob("o2det",  kParamOsc2Detune, 114, 258, 18, true,
                      "Inactive in Cosmos: OSC 2 uses OSC 1's detune.");
        }
        else
        {
            knob("o2semi", kParamOsc2Semi, 56, 258, 18, "%+.0f", " st", true, true);
            knob("o2det", kParamOsc2Detune, 114, 258, 18, "%+.0f", " ct", true);
        }
        klabel(172, 218, "PW");    knob("o2pw", kParamOsc2PW, 172, 258, 18, "%.0f", " %", false, false, false, 100.0f);
        klabel(230, 218, "LEVEL"); knob("o2lvl", kParamOsc2Level, 230, 258, 18, "%.0f", " %", false, false, false, 100.0f);

        // OSC 3 / SUB (mode-variant)
        panelBox(16, 304, 340, 410);
        if (curMode == 3) // Modular -> osc3
        {
            sectionTitle(24, 308, "OSC 3");
            comboBox("o3w", kParamOsc3Wave, 150, 308, 332, 328, kWave4, 4, false);
            klabel(90, 340, "LEVEL");  knob("o3lvl", kParamOsc3Level, 90, 380, 20, "%.0f", " %", false, false, false, 100.0f);
            klabel(240, 340, "FM AMT"); knob("fmamt", kParamFMAmount, 240, 380, 20, "%.0f", " %", false, false, false, 100.0f);
        }
        else if (curMode == 0 || curMode == 2) // Cosmos / Mono -> sub
        {
            sectionTitle(24, 308, "SUB OSC");
            comboBox("subw", kParamSubWave, 150, 308, 332, 328, kSubWave, 2, curMode == 5);
            klabel(178, 340, "LEVEL"); knob("sublvl", kParamSubLevel, 178, 380, 20, "%.0f", " %", false, false, false, 100.0f);
        }
        else
        {
            sectionTitle(24, 308, "OSC 3 / SUB");
            text(178, 356, 11.0f, lerpC(live.textPanel, live.panel, 0.4f), "(not used in this mode)", 0, false);
        }
    }

    void drawMixerVoice()
    {
        // CHARACTER (row 1) + VOICE (rows 2-3): 5-column grid with guaranteed
        // label-above-knob clearance and a divider between the two sub-groups.
        // All unison/porta/velocity/PB/oversampling params reachable (defect 2).
        panelBox(16, 414, 340, 542);
        sectionTitle(24, 418, "VOICE / CHARACTER");
        const float col0 = 46.0f, colStep = 62.0f;
        const float row1 = 456.0f, row2 = 492.0f, row3 = 528.0f;
        const float labOff = 24.0f;
        const float kr = 10.0f, comboHW = 29.0f, comboH = 9.0f;
        auto CX = [&](int c) { return col0 + c * colStep; };
        auto cLabel = [&](float cx, float y, const char* t) { text(cx, y, 9.0f, live.textPanel, t, 0, true); };

        // Row 1 — CHARACTER: noise / analog / vintage / tune / oversampling
        klabel(CX(0), row1 - labOff, "NOISE"); knob("noise", kParamNoiseLevel, CX(0), row1, kr, "%.0f", " %", false, false, false, 100.0f);
        klabel(CX(1), row1 - labOff, "ANALOG");knob("analog", kParamAnalogAmt, CX(1), row1, kr, "%.0f", " %", false, false, false, 100.0f);
        klabel(CX(2), row1 - labOff, "VNTG");  knob("vntg", kParamVintage, CX(2), row1, kr, "%.0f", " %", false, false, false, 100.0f);
        klabel(CX(3), row1 - labOff, "TUNE");  knob("mtune", kParamMasterTune, CX(3), row1, kr, "%+.0f", " ct", true);
        cLabel(CX(4), row1 - labOff, "OVERSMP");
        comboBox("ovs", kParamOversampling, CX(4) - comboHW, row1 - comboH, CX(4) + comboHW, row1 + comboH, kOversmp, 3, curMode == 5);

        // divider between CHARACTER and VOICE sub-groups
        dl->AddLine(P(24, 467), P(332, 467), withA(live.textPanel, 40), 1.0f * s);

        // Row 2 — VOICE: unison voices / detune / spread / porta / glide
        klabel(CX(0), row2 - labOff, "UNI V"); knob("univ", kParamUnisonVoices, CX(0), row2, kr, "%.0f", "", false, true);
        klabel(CX(1), row2 - labOff, "UNI DT");knob("unidt", kParamUnisonDetune, CX(1), row2, kr, "%.0f", " ct");
        klabel(CX(2), row2 - labOff, "UNI SP");knob("unisp", kParamUnisonSpread, CX(2), row2, kr, "%.0f", " %", false, false, false, 100.0f);
        klabel(CX(3), row2 - labOff, "PORTA"); knob("porta", kParamPortaTime, CX(3), row2, kr, "%.2f", " s", false, false, false, 1.0f, 0.0f, true, true);
        cLabel(CX(4), row2 - labOff, "GLIDE");
        comboBox("glide", kParamGlideMode, CX(4) - comboHW, row2 - comboH, CX(4) + comboHW, row2 + comboH, kGlide, 2, curMode == 5);

        // Row 3 — VOICE: legato / velocity / vel-curve / pitch-bend
        cLabel(CX(0), row3 - labOff, "LEGATO");
        ledButton("legato", kParamLegato, CX(0) - comboHW, row3 - comboH, CX(0) + comboHW, row3 + comboH, "", curMode == 5);
        klabel(CX(1), row3 - labOff, "VEL"); knob("vels", kParamVelSens, CX(1), row3, kr, "%.0f", " %", false, false, false, 100.0f);
        cLabel(CX(2), row3 - labOff, "V.CRV");
        comboBox("vcrv", kParamVelCurve, CX(2) - comboHW, row3 - comboH, CX(2) + comboHW, row3 + comboH, kVelCurve, 4, curMode == 5);
        klabel(CX(3), row3 - labOff, "PB"); knob("pb", kParamPbRange, CX(3), row3, kr, "%.0f", " st", false, true);
    }

    //========================================================================
    // CENTER — filter + envelopes
    //========================================================================
    void drawFilter()
    {
        panelBox(348, 60, 752, 300);
        sectionTitle(356, 64, "FILTER");
        drawFilterCurve(360, 74, 742, 156);

        // Oversized cutoff knob with a static CUTOFF label + persistent kHz readout
        // (defect 7). knobSkewed handles the log drag; readout drawn manually so it
        // stays inside the panel rather than under it.
        klabel(426, 159, "CUTOFF");
        knob("cutoff", kParamFilterCutoff, 426, 234, 54, "%.0f", " Hz");
        char rb[24];
        const float fc = values[kParamFilterCutoff];
        if (fc >= 1000.0f) std::snprintf(rb, sizeof rb, "%.2f kHz", fc / 1000.0f);
        else               std::snprintf(rb, sizeof rb, "%.0f Hz", fc);
        text(426, 290, 10.0f, live.accent, rb, 0, true);

        klabel(556, 184, "RES");    knob("res", kParamFilterRes, 556, 232, 30, "%.0f", " %", false, false, false, 100.0f);
        klabel(636, 184, "ENV AMT");knob("fenvamt", kParamFilterEnvAmt, 636, 232, 30, "%+.0f", " %", true, false, false, 100.0f);
        if (curMode == 0) // Cosmos HP
        { klabel(712, 184, "HP"); knob("hp", kParamFilterHP, 712, 232, 30, "%.0f", " Hz"); }
    }

    void drawFilterCurve(float rx0, float ry0, float rx1, float ry1)
    {
        dl->AddRectFilled(P(rx0, ry0), P(rx1, ry1), IM_COL32(10, 12, 14, 255), 4.0f * s);
        dl->PushClipRect(P(rx0, ry0), P(rx1, ry1), true);

        const float cutoff = values[kParamFilterCutoff];
        const float res    = values[kParamFilterRes];
        const float hp     = values[kParamFilterHP];
        if (cutoff != fcCutoff || res != fcRes || hp != fcHP || curMode != fcMode)
        {
            fcCutoff = cutoff; fcRes = res; fcHP = hp; fcMode = curMode;
            computeFilterCurve(rx0, ry0, rx1, ry1);
        }

        // gridlines 100/1k/10k
        const float fMin = 20.0f, fMax = 20000.0f;
        for (float fg : { 100.0f, 1000.0f, 10000.0f })
        {
            const float lx = (std::log10(fg) - std::log10(fMin)) / (std::log10(fMax) - std::log10(fMin));
            const float gx = rx0 + lx * (rx1 - rx0);
            dl->AddLine(P(gx, ry0), P(gx, ry1), IM_COL32(255, 255, 255, 18), 1.0f * s);
            text(gx, ry1 - 12, 9.0f, withA(live.textPanel, 120), fg >= 1000 ? (fg >= 10000 ? "10k" : "1k") : "100", 0);
        }
        // fill + stroke
        const float baseY = ry1;
        for (int i = 0; i + 1 < kFcN; ++i)
            dl->AddQuadFilled(P(fcX[i], fcY[i]), P(fcX[i + 1], fcY[i + 1]),
                              P(fcX[i + 1], baseY), P(fcX[i], baseY), withA(live.accent, 22));
        ImVec2 line[kFcN];
        for (int i = 0; i < kFcN; ++i) line[i] = P(fcX[i], fcY[i]);
        dl->AddPolyline(line, kFcN, live.accent, 0, 2.0f * s);

        // cutoff marker dot
        const float lxc = (std::log10(cutoff < 20 ? 20 : cutoff) - std::log10(fMin))
                          / (std::log10(fMax) - std::log10(fMin));
        const float mx = rx0 + lxc * (rx1 - rx0);
        // find nearest curve y
        int bi = 0; float bd = 1e9f;
        for (int i = 0; i < kFcN; ++i) { float d = std::fabs(fcX[i] - mx); if (d < bd) { bd = d; bi = i; } }
        dl->AddCircleFilled(P(fcX[bi], fcY[bi]), 3.0f * s, live.ledOn, 12);

        dl->PopClipRect();
        dl->AddRect(P(rx0, ry0), P(rx1, ry1), IM_COL32(0, 0, 0, 180), 4.0f * s, 0, 1.2f * s);
    }

    void computeFilterCurve(float rx0, float ry0, float rx1, float ry1)
    {
        const float fMin = 20.0f, fMax = 20000.0f, dbRange = 42.0f; // -24..+18
        const int N = fcMode == 5 ? 3 : 4;
        float k;
        switch (fcMode)
        {
            case 0: k = std::min(fcRes, 0.75f) * 3.0f; break; // Cosmos (clamped)
            case 1: k = fcRes * 4.2f; break;                  // Oracle
            case 2: k = fcRes * 4.0f; break;                  // Mono
            case 5: k = fcRes * 3.2f; break;                  // Acid
            default: k = fcRes * 3.8f; break;                 // Modular / Prism
        }
        const float lc = std::log10(fMin), hc = std::log10(fMax);
        for (int i = 0; i < kFcN; ++i)
        {
            const float lx = (float)i / (float)(kFcN - 1);
            const float f = std::pow(10.0f, lc + lx * (hc - lc));
            const float w = f / (fcCutoff < 20 ? 20 : fcCutoff);
            // (1+jw)^2
            float re2 = 1.0f - w * w, im2 = 2.0f * w, reN, imN;
            if (N == 4) { reN = re2 * re2 - im2 * im2; imN = 2.0f * re2 * im2; }
            else        { reN = re2 * 1.0f - im2 * w;  imN = re2 * w + im2 * 1.0f; } // (1+jw)^3=(1+jw)^2*(1+jw)
            reN += k;
            float mag = 1.0f / std::sqrt(reN * reN + imN * imN);
            if (fcMode == 0) { const float wh = f / (fcHP < 20 ? 20 : fcHP); mag *= wh / std::sqrt(1.0f + wh * wh); }
            float db = 20.0f * std::log10(mag > 1e-6f ? mag : 1e-6f);
            db = db < -24.0f ? -24.0f : (db > 18.0f ? 18.0f : db);
            // Map clamped dB to normalized Y: 0 at +18 (top), 1 at -24 (bottom).
            const float ny = (18.0f - db) / dbRange;
            fcX[i] = rx0 + lx * (rx1 - rx0);
            fcY[i] = ry0 + ny * (ry1 - ry0);
        }
    }

    void drawEnvelopes()
    {
        panelBox(348, 304, 548, 542);
        sectionTitle(356, 308, "AMP ENV");
        drawADSR(356, 320, 540, 404, kParamAmpA, kParamAmpS, ampEnv, ampHash, false);
        drawADSRKnobs(380, kParamAmpA, "amp");
        comboBox("ampcrv", kParamAmpCurve, 360, 516, 536, 540, kEnvCurve, 4);

        panelBox(552, 304, 752, 542);
        sectionTitle(560, 308, "FILTER ENV");
        drawADSR(560, 320, 744, 404, kParamFiltA, kParamFiltS, filtEnv, filtHash, true);
        drawADSRKnobs(584, kParamFiltA, "filt");
        comboBox("filtcrv", kParamFiltCurve, 564, 516, 740, 540, kEnvCurve, 4);
    }

    // ADSR knob row: A,D,S,R at r18, spaced 46 px. Labels top at y=446, knob
    // centers at y=484 so the r18+arc3 knob bottom (=505) clears the Curve combo
    // top (y=516) by 11px, and the label bottom (~456) clears the knob top
    // (=463) by 7px. base = param index of A.
    void drawADSRKnobs(float x0, uint32_t baseA, const char* pfx)
    {
        const char* labs[4] = { "A", "D", "S", "R" };
        for (int i = 0; i < 4; ++i)
        {
            char id[16]; std::snprintf(id, sizeof(id), "%s%s", pfx, labs[i]);
            const float cx = x0 + i * 46.0f;
            klabel(cx, 446, labs[i]);
            if (i == 2) knob(id, baseA + i, cx, 484, 18, "%.0f", " %", false, false, false, 100.0f); // sustain
            else        knob(id, baseA + i, cx, 484, 18, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, true, true); // times, auto s
        }
    }

    struct EnvCache { float x0, y0, x1, y1; };
    void drawADSR(float rx0, float ry0, float rx1, float ry1,
                  uint32_t baseA, uint32_t sParam, ImVec2* pts, float& hash, bool filt)
    {
        dl->AddRectFilled(P(rx0, ry0), P(rx1, ry1), IM_COL32(10, 12, 14, 255), 4.0f * s);
        const float A = values[baseA], D = values[baseA + 1], S = values[sParam], R = values[baseA + 3];
        const int crv = (int)std::lround(values[filt ? kParamFiltCurve : kParamAmpCurve]);
        const float h = A + D * 3.1f + S * 7.3f + R * 11.7f + crv * 101.0f + (rx1 - rx0) + (ry1 - ry0) * 3.0f;
        if (h != hash) { hash = h; computeADSR(rx0, ry0, rx1, ry1, A, D, S, R, crv, pts); }

        const float baseY = ry1;
        for (int i = 0; i + 1 < kAdsrN; ++i)
            dl->AddQuadFilled(P(pts[i].x, pts[i].y), P(pts[i + 1].x, pts[i + 1].y),
                              P(pts[i + 1].x, baseY), P(pts[i].x, baseY), withA(live.accent, 40));
        ImVec2 line[kAdsrN];
        for (int i = 0; i < kAdsrN; ++i) line[i] = P(pts[i].x, pts[i].y);
        dl->AddPolyline(line, kAdsrN, live.accent, 0, 2.0f * s);
        dl->AddRect(P(rx0, ry0), P(rx1, ry1), IM_COL32(0, 0, 0, 180), 4.0f * s, 0, 1.2f * s);
    }

    static float applyCurve(float t, int crv)
    {
        if (t < 0) t = 0; if (t > 1) t = 1;
        switch (crv)
        {
            case 1: return t * t;                              // Exp
            case 2: return std::sqrt(t);                       // Log
            case 3: return (1.0f - std::exp(-3.0f * t)) / (1.0f - std::exp(-3.0f)); // AnalogRC
            default: return t;                                 // Linear
        }
    }
    void computeADSR(float rx0, float ry0, float rx1, float ry1,
                     float A, float D, float S, float R, int crv, ImVec2* pts)
    {
        const float W = rx1 - rx0, H = ry1 - ry0, tRef = 0.6f;
        const float Wstage = 0.30f * W, Wsus = 0.10f * W;
        const float Wa = Wstage * (A / (A + tRef));
        const float Wd = Wstage * (D / (D + tRef));
        const float Wr = Wstage * (R / (R + tRef));
        int n = 0;
        const int seg = 12;
        // attack: 0 -> peak
        for (int i = 0; i <= seg; ++i)
        {
            const float u = (float)i / seg;
            pts[n++] = ImVec2(rx0 + Wa * u, ry1 - H * applyCurve(u, crv));
        }
        // decay: peak -> sustain
        for (int i = 1; i <= seg; ++i)
        {
            const float u = (float)i / seg;
            const float lvl = 1.0f - (1.0f - S) * applyCurve(u, crv);
            pts[n++] = ImVec2(rx0 + Wa + Wd * u, ry1 - H * lvl);
        }
        // sustain plateau
        pts[n++] = ImVec2(rx0 + Wa + Wd + Wsus, ry1 - H * S);
        // release: sustain -> 0
        for (int i = 1; i <= seg; ++i)
        {
            const float u = (float)i / seg;
            const float lvl = S * (1.0f - applyCurve(u, crv));
            pts[n++] = ImVec2(rx0 + Wa + Wd + Wsus + Wr * u, ry1 - H * lvl);
        }
        // pad remainder (fixed-size buffer) with the last point
        while (n < kAdsrN) { pts[n] = pts[n - 1]; ++n; }
    }

    //========================================================================
    // RIGHT column — LFOs / mode sub-panel / scope / output
    //========================================================================
    void drawLFOs()
    {
        drawOneLFO(760, 60, 1000, 190, "LFO 1", kParamLfo1Rate, kParamLfo1Shape, kParamLfo1Fade, kParamLfo1Sync, "l1");
        drawOneLFO(760, 194, 1000, 324, "LFO 2", kParamLfo2Rate, kParamLfo2Shape, kParamLfo2Fade, kParamLfo2Sync, "l2");
    }
    void drawOneLFO(float x0, float y0, float x1, float y1, const char* title,
                    uint32_t rate, uint32_t shape, uint32_t fade, uint32_t sync, const char* pfx)
    {
        panelBox(x0, y0, x1, y1);
        sectionTitle(x0 + 8, y0 + 4, title);
        char id[24];
        std::snprintf(id, sizeof(id), "%srate", pfx);
        klabel(x0 + 46, y0 + 26, "RATE"); knob(id, rate, x0 + 46, y0 + 68, 22, "%.2f", " Hz");
        std::snprintf(id, sizeof(id), "%sfade", pfx);
        klabel(x0 + 116, y0 + 26, "FADE"); knob(id, fade, x0 + 116, y0 + 68, 22, "%.2f", " s", false, false, false, 1.0f, 0.0f, true, true);
        text(x0 + 168, y0 + 24, 9.5f, live.textPanel, "SHAPE", 0, true);
        std::snprintf(id, sizeof(id), "%sshape", pfx);
        comboBox(id, shape, x0 + 150, y0 + 38, x0 + 232, y0 + 58, kLfoShape, 5);
        std::snprintf(id, sizeof(id), "%ssync", pfx);
        ledButton(id, sync, x0 + 150, y0 + 74, x0 + 232, y0 + 98, "SYNC");
    }

    void drawModeSubPanelRegion()
    {
        panelBox(760, 328, 1000, 478);
        if (modeBlend < 1.0f)
        {
            const float outA = 1.0f - std::min(1.0f, modeBlend * 2.0f);
            const float inA  = std::max(0.0f, modeBlend * 2.0f - 1.0f);
            if (outA > 0.01f) drawSubPanel(prevMode, outA, false);
            if (inA  > 0.01f) drawSubPanel(curMode, inA, true);
        }
        else drawSubPanel(curMode, 1.0f, true);
    }

    // The sub-panel content per mode. `live_` gates whether controls are hit-testable
    // (only the fully-shown panel is interactive during a crossfade).
    void drawSubPanel(int m, float a, bool interactive)
    {
        switch (m)
        {
            case 0: drawSubCosmos(a, interactive); break;
            case 1: drawSubOracle(a, interactive); break;
            case 2: drawSubMono(a, interactive); break;
            case 3: drawSubModular(a, interactive); break;
            case 4: drawSubPrism(a, interactive); break;
            case 5: drawSubAcid(a, interactive); break;
        }
    }

    void drawSubCosmos(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "BBD CHORUS", -1, true);
        const int cur = (int)std::lround(values[kParamCosmosChorus]); // 0 off,1 I,2 II,3 both
        const char* labs[3] = { "I", "II", "I+II" };
        const int enumv[3] = { 1, 2, 3 };
        const float cxs[3] = { 800, 880, 950 };
        for (int i = 0; i < 3; ++i)
        {
            const bool on = (cur == enumv[i]);
            char id[16]; std::snprintf(id, sizeof(id), "cho%d", i);
            const ImVec2 c = P(cxs[i], 386);
            const float rr = 16.0f * s;
            if (it)
            {
                ImGui::SetCursorScreenPos(ImVec2(c.x - rr, c.y - rr));
                ImGui::InvisibleButton(id, ImVec2(rr * 2, rr * 2));
                if (ImGui::IsItemClicked()) setChoice(kParamCosmosChorus, on ? 0 : enumv[i]);
                if (ImGui::IsItemHovered() && tips[kParamCosmosChorus]) ImGui::SetTooltip("%s", tips[kParamCosmosChorus]);
            }
            dl->AddCircleFilled(c, rr, mulA(on ? live.ledOn : IM_COL32(44, 46, 50, 255), a), 24);
            dl->AddCircle(c, rr, mulA(live.accent, a * (on ? 1.0f : 0.5f)), 24, 1.6f * s);
            text(cxs[i], 378, 12.0f, mulA(on ? live.text : live.textPanel, a), labs[i], 0, on);
            if (on) dl->AddRectFilled(P(cxs[i] - 16, 410), P(cxs[i] + 16, 413), mulA(live.accent, a), 2.0f * s);
        }
    }
    void drawSubOracle(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "POLY-MOD", -1, true);
        const uint32_t pp[4] = { kParamPmFenvOscA, kParamPmOscBOscA, kParamPmOscBPWM, kParamPmFenvFilt };
        // Clear routing labels (the font lacks a reliable arrow glyph, so "->").
        const char* labs[4] = { "F.ENV->OSC1", "OSC2->OSC1", "OSC2->PW", "F.ENV->FILT" };
        const float cx[4] = { 820, 940, 820, 940 };
        const float cy[4] = { 386, 386, 446, 446 };
        for (int i = 0; i < 4; ++i)
        {
            text(cx[i], cy[i] - 28, 9.0f, mulA(live.textPanel, a), labs[i], 0, true);
            if (it) knob(labs[i], pp[i], cx[i], cy[i], 18, "%.0f", " %", false, false, false, 100.0f);
            else    ghostKnob(pp[i], cx[i], cy[i], 18, a);
        }
    }
    void drawSubMono(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "RING \xC2\xB7 SYNC", -1, true);
        text(820, 356, 9.0f, mulA(live.textPanel, a), "RING", 0, true);
        if (it) knob("ringm", kParamRingMod, 820, 400, 22, "%.0f", " %", false, false, false, 100.0f);
        else    ghostKnob(kParamRingMod, 820, 400, 22, a);
        if (it) ledButton("hsync", kParamHardSync, 900, 388, 984, 412, "HARD SYNC", true);
        else { const ImVec2 c = P(942, 400); dl->AddCircleFilled(c, 10 * s, mulA(values[kParamHardSync] > 0.5f ? live.ledOn : IM_COL32(150,152,158,255), a), 20); }
    }
    void drawSubModular(float a, bool it)
    {
        // Modular consumes ringMod + hardSync (engine + 4 factory presets) but the
        // old layout only surfaced them in Mono; expose them here alongside S&H.
        text(768, 332, 11.0f, mulA(live.accent, a), "S&H \xC2\xB7 RING \xC2\xB7 SYNC", -1, true);
        text(798, 356, 9.0f, mulA(live.textPanel, a), "S&H", 0, true);
        if (it) knob("shrate", kParamShRate, 798, 398, 18, "%.2f", " Hz");
        else    ghostKnob(kParamShRate, 798, 398, 18, a);
        text(852, 356, 9.0f, mulA(live.textPanel, a), "RING", 0, true);
        if (it) knob("modring", kParamRingMod, 852, 398, 18, "%.0f", " %", false, false, false, 100.0f);
        else    ghostKnob(kParamRingMod, 852, 398, 18, a);
        if (it) ledButton("modsync", kParamHardSync, 786, 430, 862, 450, "SYNC");
        else { const ImVec2 c = P(796, 440); dl->AddCircleFilled(c, 8 * s, mulA(values[kParamHardSync] > 0.5f ? live.ledOn : IM_COL32(150, 152, 158, 255), a), 16); }
        // animated S&H staircase mini-scope (shrunk to the right column for room)
        const float rx0 = 884, ry0 = 360, rx1 = 988, ry1 = 456;
        dl->AddRectFilled(P(rx0, ry0), P(rx1, ry1), mulA(IM_COL32(10, 14, 12, 255), a), 3.0f * s);
        const float rate = values[kParamShRate];
        shPhase += ImGui::GetIO().DeltaTime * rate;
        const int steps = 8;
        ImVec2 stair[steps * 2];
        for (int i = 0; i < steps; ++i)
        {
            float v = std::sin((shPhase + i * 1.37f) * 2.1f) * 0.5f + 0.5f; // pseudo-random-ish
            v = std::fmod(v * 3.1f, 1.0f);
            const float y = ry1 - (ry1 - ry0) * (0.15f + 0.7f * v);
            stair[i * 2]     = P(rx0 + (rx1 - rx0) * i / steps, y);
            stair[i * 2 + 1] = P(rx0 + (rx1 - rx0) * (i + 1) / steps, y);
        }
        dl->AddPolyline(stair, steps * 2, mulA(live.accent, a), 0, 1.8f * s);
        // decorative patch jacks
        for (float jx : { rx0 + 6.0f, rx1 - 6.0f })
            for (float jy : { ry0 - 8.0f, ry1 + 8.0f })
            { dl->AddCircleFilled(P(jx, jy), 5 * s, mulA(IM_COL32(30, 32, 30, 255), a), 16);
              dl->AddCircleFilled(P(jx, jy), 2.4f * s, mulA(IM_COL32(70, 74, 70, 255), a), 12); }
    }
    void drawSubPrism(float a, bool it) { drawAlgoWidget(760, 328, 1000, 478, a, it); }
    void drawSubAcid(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "ACID", -1, true);
        text(806, 356, 9.0f, mulA(live.textPanel, a), "ACCENT", 0, true);
        if (it) knob("acc", kParamAcidAccentAmt, 806, 400, 22, "%.0f", " %", false, false, false, 100.0f);
        else    ghostKnob(kParamAcidAccentAmt, 806, 400, 22, a);
        text(890, 356, 9.0f, mulA(live.textPanel, a), "SLIDE", 0, true);
        if (it) knob("slide", kParamAcidSlideTime, 890, 400, 22, "%.0f", " ms", false, false, false, 1.0f, 0.0f, true, true);
        else    ghostKnob(kParamAcidSlideTime, 890, 400, 22, a);
        // big ACCENT lamp pulsing on accented steps
        const int step = liveStep();
        bool acc = false;
        if (step >= 0 && step < 16) acc = values[kParamSeqAccent0 + step] > 0.5f;
        const ImVec2 c = P(962, 400);
        dl->AddCircleFilled(c, 14 * s, mulA(acc ? live.ledOn : IM_COL32(150, 152, 158, 255), a), 24);
        dl->AddCircle(c, 14 * s, mulA(live.accent, a), 24, 1.6f * s);
        text(962, 420, 8.5f, mulA(live.textPanel, a), "ACC", 0, true);
    }

    // Non-interactive ghost of a knob (used for the fading-out sub-panel).
    void ghostKnob(uint32_t p, float cx, float cy, float r, float a)
    {
        const ImVec2 c = P(cx, cy);
        dl->AddCircleFilled(c, r * s, mulA(IM_COL32(96, 97, 100, 255), a), 32);
        const ParamDef& d = kParamDefs[p];
        const float t = (d.max > d.min) ? (values[p] - d.min) / (d.max - d.min) : 0.0f;
        const float ang = duskdpf::DuskPanel::knobAngle(t);
        dl->AddLine(c, ImVec2(c.x + std::sin(ang) * r * 0.9f * s, c.y - std::cos(ang) * r * 0.9f * s),
                    mulA(IM_COL32(25, 25, 27, 255), a), 2.4f * s);
    }

    //========================================================================
    // Prism operator matrix (left column) + algorithm diagram
    //========================================================================
    void drawPrismOps()
    {
        panelBox(16, 60, 340, 410);
        sectionTitle(24, 64, "OPERATOR MATRIX");
        const msynth::PrismAlgo& alg = msynth::kPrismAlgos[clampAlgo()];
        // Each operator gets its own block with two grouped sub-rows of compact,
        // tickless knobs so every control is legible (defect 3):
        //   sub-row 1: RATIO · FINE · LEVEL | VEL · KEY
        //   sub-row 2: A · D · S · R        (+ FB on op 4)
        const float cxc[5] = { 96.0f, 150.0f, 204.0f, 258.0f, 312.0f };
        const float kr = 11.0f;
        for (int op = 0; op < 4; ++op)
        {
            const float top = 92.0f + op * 78.0f;
            const float cy1 = top + 22.0f, cy2 = top + 56.0f;
            const bool carrier = (alg.carrierMask >> op) & 1;
            const uint32_t base = kParamOp1Ratio + op * 9;

            if (op > 0) dl->AddLine(P(22, top - 6), P(332, top - 6), withA(live.textPanel, 30), 1.0f * s);
            // op number + carrier/modulator LED
            panel.led(dl, 34, top + 34, carrier, 3.4f);
            char lab[8]; std::snprintf(lab, sizeof(lab), "OP %d", op + 1);
            text(48, top + 28, 10.0f, carrier ? live.accent : live.textPanel, lab, -1, true);
            text(48, top + 42, 7.5f, withA(live.textPanel, 150), carrier ? "carrier" : "mod", -1, false);
            // group divider between LEVEL and VEL in sub-row 1
            dl->AddLine(P(231, top + 10), P(231, top + 34), withA(live.textPanel, 30), 1.0f * s);

            auto L = [&](float cx, float y, const char* t) { text(cx, y - 20.0f, 8.0f, live.textPanel, t, 0, true); };
            char id[24];
            // sub-row 1
            L(cxc[0], cy1, "RATIO"); std::snprintf(id, sizeof(id), "op%dR", op); knobRatio(id, base + 0, cxc[0], cy1, kr, false);
            L(cxc[1], cy1, "FINE");  std::snprintf(id, sizeof(id), "op%dF", op); knob(id, base + 1, cxc[1], cy1, kr, "%+.0f", " ct", true, false, false, 1.0f, 0.0f, false);
            L(cxc[2], cy1, "LEVEL"); std::snprintf(id, sizeof(id), "op%dL", op); knob(id, base + 2, cxc[2], cy1, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            L(cxc[3], cy1, "VEL");   std::snprintf(id, sizeof(id), "op%dV", op); knob(id, base + 3, cxc[3], cy1, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            L(cxc[4], cy1, "KEY");   std::snprintf(id, sizeof(id), "op%dK", op); knob(id, base + 4, cxc[4], cy1, kr, "%+.0f", " %", true, false, false, 100.0f, 0.0f, false);
            // sub-row 2
            L(cxc[0], cy2, "A"); std::snprintf(id, sizeof(id), "op%dA", op); knob(id, base + 5, cxc[0], cy2, kr, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, false, true);
            L(cxc[1], cy2, "D"); std::snprintf(id, sizeof(id), "op%dD", op); knob(id, base + 6, cxc[1], cy2, kr, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, false, true);
            L(cxc[2], cy2, "S"); std::snprintf(id, sizeof(id), "op%dS", op); knob(id, base + 7, cxc[2], cy2, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            L(cxc[3], cy2, "R"); std::snprintf(id, sizeof(id), "op%dRl", op); knob(id, base + 8, cxc[3], cy2, kr, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, false, true);
            if (op == 3) // feedback op hosts the FB knob, aligned in the KEY column
            { text(cxc[4], cy2 - 20.0f, 8.0f, live.accent, "FB", 0, true);
              knob("prismfb", kParamPrismFB, cxc[4], cy2, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false); }
        }
    }

    int clampAlgo() const { int a = (int)std::lround(values[kParamPrismAlgo]); return a < 0 ? 0 : (a > 7 ? 7 : a); }

    void drawAlgoWidget(float x0, float y0, float x1, float y1, float a, bool it)
    {
        text(x0 + 8, y0 + 4, 11.0f, mulA(live.accent, a), "ALGORITHM", -1, true);
        const int active = clampAlgo();
        // 8 thumbnails 4x2 across the top
        const float tw = 56.0f, th = 30.0f;
        const float gx0 = x0 + 8, gy0 = y0 + 20;
        for (int i = 0; i < 8; ++i)
        {
            const float tx = gx0 + (i % 4) * 58.0f;
            const float ty = gy0 + (i / 4) * 34.0f;
            char id[16]; std::snprintf(id, sizeof(id), "algo%d", i);
            if (it)
            {
                ImGui::SetCursorScreenPos(P(tx, ty));
                ImGui::InvisibleButton(id, ImVec2(tw * s, th * s));
                if (ImGui::IsItemClicked()) setChoice(kParamPrismAlgo, i);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", msynth::kPrismAlgos[i].name);
            }
            const bool on = (i == active);
            dl->AddRectFilled(P(tx, ty), P(tx + tw, ty + th), mulA(IM_COL32(10, 20, 22, 255), a), 3.0f * s);
            dl->AddRect(P(tx, ty), P(tx + tw, ty + th),
                        mulA(on ? live.accent : IM_COL32(60, 70, 72, 255), a), 3.0f * s, 0, on ? 1.8f * s : 1.0f * s);
            drawAlgoDiagram(i, tx + 4, ty + 3, tx + tw - 4, ty + th - 3, a * (on ? 1.0f : 0.55f), false);
        }
        // large diagram of the active algorithm
        drawAlgoDiagram(active, x0 + 60, y0 + 92, x1 - 60, y1 - 8, a, true);
    }

    void drawAlgoDiagram(int idx, float x0, float y0, float x1, float y1, float a, bool big)
    {
        const msynth::PrismAlgo& alg = msynth::kPrismAlgos[idx];
        // grid extents
        int maxgx = 0, maxgy = 0;
        for (int i = 0; i < 4; ++i) { maxgx = std::max(maxgx, (int)alg.ops[i].gx); maxgy = std::max(maxgy, (int)alg.ops[i].gy); }
        const float pad = big ? 10.0f : 3.0f;
        const float cellW = (x1 - x0 - 2 * pad) / (maxgx + 1);
        const float busY = y1 - (big ? 14.0f : 4.0f);
        const float cellH = (busY - y0 - pad) / (maxgy + 1);
        const float box = std::min(cellW, cellH) * (big ? 0.5f : 0.55f);
        ImVec2 opc[4];
        for (int i = 0; i < 4; ++i)
        {
            const float cx = x0 + pad + (alg.ops[i].gx + 0.5f) * cellW;
            const float cy = y0 + pad + (alg.ops[i].gy + 0.5f) * cellH;
            opc[i] = P(cx, cy);
        }
        // edges
        for (int i = 0; i < alg.nEdges; ++i)
        {
            const ImVec2 f = opc[alg.edges[i].from], t = opc[alg.edges[i].to];
            dl->AddLine(f, t, mulA(lerpC(live.accent, live.textPanel, 0.3f), a), (big ? 1.8f : 1.0f) * s);
            // arrowhead at dest
            ImVec2 d(t.x - f.x, t.y - f.y); float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len > 1e-3f) { d.x /= len; d.y /= len; const float ah = (big ? 6.0f : 3.0f) * s;
                const ImVec2 tip(t.x - d.x * box, t.y - d.y * box);
                const ImVec2 n(-d.y, d.x);
                dl->AddTriangleFilled(tip, ImVec2(tip.x - d.x * ah + n.x * ah * 0.6f, tip.y - d.y * ah + n.y * ah * 0.6f),
                                      ImVec2(tip.x - d.x * ah - n.x * ah * 0.6f, tip.y - d.y * ah - n.y * ah * 0.6f),
                                      mulA(live.accent, a)); }
        }
        // output bus joining carriers
        ImVec2 busPts[4]; int nb = 0;
        for (int i = 0; i < 4; ++i) if ((alg.carrierMask >> i) & 1)
        {
            dl->AddLine(opc[i], P(0, 0), 0, 0); // no-op keeps structure clear
            dl->AddLine(ImVec2(opc[i].x, opc[i].y + box), ImVec2(opc[i].x, P(0, busY).y),
                        mulA(live.accent, a), (big ? 1.6f : 0.9f) * s);
            busPts[nb++] = opc[i];
        }
        if (big && nb > 0)
        {
            float minx = 1e9f, maxx = -1e9f;
            for (int i = 0; i < nb; ++i) { minx = std::min(minx, busPts[i].x); maxx = std::max(maxx, busPts[i].x); }
            const float by = P(0, busY).y;
            dl->AddLine(ImVec2(minx, by), ImVec2(maxx, by), mulA(live.accent, a), 2.2f * s);
        }
        // op boxes
        for (int i = 0; i < 4; ++i)
        {
            const bool carrier = (alg.carrierMask >> i) & 1;
            const ImVec2 c = opc[i];
            const float b = box;
            dl->AddRectFilled(ImVec2(c.x - b, c.y - b), ImVec2(c.x + b, c.y + b),
                              mulA(carrier ? withA(live.accent, 70) : IM_COL32(40, 50, 52, 255), a), 3.0f * s);
            dl->AddRect(ImVec2(c.x - b, c.y - b), ImVec2(c.x + b, c.y + b),
                        mulA(carrier ? live.accent : IM_COL32(90, 100, 102, 255), a), 3.0f * s, 0, 1.2f * s);
            if (big)
            { char n[4]; std::snprintf(n, sizeof(n), "%d", i + 1);
              text((c.x - org.x) / s, (c.y - org.y) / s - 6, 11.0f, mulA(live.text, a), n, 0, true); }
        }
        // feedback loop on fbOp
        if (big)
        {
            const ImVec2 c = opc[alg.fbOp];
            const float fb = values[kParamPrismFB];
            const float rr = box * (0.9f + 0.8f * fb);
            dl->AddCircle(ImVec2(c.x + box, c.y - box), rr, mulA(live.ledOn, a), 16, (1.0f + 2.0f * fb) * s);
        }
    }

    //========================================================================
    // MOD MATRIX bar + overlay
    //========================================================================
    int activeModSlots() const
    {
        int n = 0;
        for (int i = 0; i < 8; ++i)
            if (values[kParamModSrc0 + i] > 0.5f && values[kParamModDst0 + i] > 0.5f
                && std::fabs(values[kParamModAmt0 + i]) > 1e-4f) ++n;
        return n;
    }
    void drawModMatrixBar()
    {
        panelBox(760, 482, 1000, 540);
        const ImVec2 b0 = P(768, 490), b1 = P(992, 532);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton("modbar", ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) showMod = !showMod;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the modulation matrix");
        dl->AddRectFilled(b0, b1, IM_COL32(40, 40, 43, 255), 4.0f * s);
        dl->AddRect(b0, b1, showMod ? live.accent : IM_COL32(90, 90, 94, 255), 4.0f * s, 0, 1.4f * s);
        panel.led(dl, 782, 511, showMod, 4.0f);
        text(886, 502, 12.0f, IM_COL32(238, 238, 240, 255), "MOD MATRIX", 0, true);
        char cnt[24]; std::snprintf(cnt, sizeof(cnt), "%d active", activeModSlots());
        text(886, 518, 9.0f, live.accent, cnt, 0);
    }
    void drawModMatrixOverlay()
    {
        if (!showMod) return;
        // Dark scrim behind the modal. NOTE: we deliberately do NOT submit a
        // full-window InvisibleButton for the scrim. In Dear ImGui the first
        // overlapping item to be submitted claims the hover (no AllowOverlap), so a
        // scrim button drawn before the panel swallows every click meant for the
        // combos / knobs / ✕ and dismisses the overlay on the first interaction
        // (the reported "modal goes away when I select something" bug). Instead the
        // panel widgets are submitted normally and the scrim close is a manual
        // geometric hit-test done AFTER them, guarded so it yields to the panel rect
        // and to any open combo popup.
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(0, 0, 0, 150));
        // modal
        const ImVec2 pMin = P(220 - 3, 120 - 3), pMax = P(1020 + 3, 660 + 3);
        panelBox(220, 120, 1020, 660);
        text(240, 130, 15.0f, live.accent, "MODULATION MATRIX", -1, true);
        // close ✕
        const ImVec2 c0 = P(988, 128), c1 = P(1012, 152);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("modclose", ImVec2(c1.x - c0.x, c1.y - c0.y));
        if (ImGui::IsItemClicked()) showMod = false;
        dl->AddRect(c0, c1, IM_COL32(150, 150, 154, 255), 3.0f * s, 0, 1.2f * s);
        drawX(1000, 140, 5.0f, live.text);
        text(240, 156, 10.0f, live.textPanel, "SOURCE", -1, true);
        text(500, 156, 10.0f, live.textPanel, "DEST", -1, true);
        text(790, 156, 10.0f, live.textPanel, "AMOUNT", 0, true);

        for (int r = 0; r < 8; ++r)
        {
            const float y = 168.0f + r * 58.0f;
            char id[24];
            std::snprintf(id, sizeof(id), "msrc%d", r);
            comboBox(id, kParamModSrc0 + r, 240, y + 8, 470, y + 34, kModSrc, 11);
            { const float ay = y + 21; dl->AddLine(P(478, ay), P(492, ay), live.accent, 2.0f * s);
              dl->AddTriangleFilled(P(492, ay - 4), P(492, ay + 4), P(497, ay), live.accent); }
            std::snprintf(id, sizeof(id), "mdst%d", r);
            comboBox(id, kParamModDst0 + r, 500, y + 8, 760, y + 34, kModDst, 13);
            std::snprintf(id, sizeof(id), "mamt%d", r);
            knob(id, kParamModAmt0 + r, 790, y + 20, 18, "%+.0f", " %", true, false, false, 100.0f);
            // clear-row ✕
            std::snprintf(id, sizeof(id), "mclr%d", r);
            const ImVec2 x0 = P(972, y + 8), x1 = P(996, y + 32);
            ImGui::SetCursorScreenPos(x0);
            ImGui::InvisibleButton(id, ImVec2(x1.x - x0.x, x1.y - x0.y));
            if (ImGui::IsItemClicked())
            { setChoice(kParamModSrc0 + r, 0); setChoice(kParamModDst0 + r, 0);
              pushParam(kParamModAmt0 + r, 0.0f); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear this slot");
            dl->AddRect(x0, x1, IM_COL32(120, 120, 124, 255), 3.0f * s, 0, 1.0f * s);
            drawX(984, y + 20, 4.0f, live.textPanel);
        }

        // Scrim close: dismiss the overlay only on a click in the dark area OUTSIDE
        // the panel rect, and only when no combo popup is open (so choosing a
        // Source / Dest / clearing a slot never dismisses the modal). Clicks inside
        // the panel are handled by the widgets above and never reach here.
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool insidePanel = mp.x >= pMin.x && mp.x <= pMax.x
                              && mp.y >= pMin.y && mp.y <= pMax.y;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !insidePanel
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
            showMod = false;
    }

    // Stock ImGui button styled to the mode accent; returns true on click.
    bool modalButton(const char* label, float x0, float y0, float w, float h, bool accent)
    {
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::PushStyleColor(ImGuiCol_Button,        accent ? withA(live.accent, 200) : IM_COL32(48, 48, 52, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent ? live.accent            : IM_COL32(70, 70, 74, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accent ? live.accent            : IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(240, 242, 246, 255));
        const bool c = ImGui::Button(label, ImVec2(w * s, h * s));
        ImGui::PopStyleColor(4);
        return c;
    }

    // Compact centered "save user preset" modal. Same replace-panels pattern as
    // the mod matrix (DPF ImGui can't float an overlapping window). InputText for
    // the name; inline overwrite / delete confirm states.
    void drawSaveModalOverlay()
    {
        if (!showSaveModal) return;
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(0, 0, 0, 150));

        const float x0 = 420, y0 = 285, x1 = 820, y1 = 495;
        const ImVec2 pMin = P(x0 - 3, y0 - 3), pMax = P(x1 + 3, y1 + 3);
        panelBox(x0, y0, x1, y1);
        text(x0 + 20, y0 + 14, 15.0f, live.accent, "SAVE USER PRESET", -1, true);

        // close ✕
        const ImVec2 c0 = P(x1 - 32, y0 + 8), c1 = P(x1 - 8, y0 + 32);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("saveclose", ImVec2(c1.x - c0.x, c1.y - c0.y));
        if (ImGui::IsItemClicked()) showSaveModal = false;
        dl->AddRect(c0, c1, IM_COL32(150, 150, 154, 255), 3.0f * s, 0, 1.2f * s);
        drawX(x1 - 20, y0 + 20, 5.0f, live.text);

        ImFont* f = panel.pickFont(13.0f * s);
        ImGui::PushFont(f);

        text(x0 + 20, y0 + 50, 10.0f, live.textPanel, "NAME", -1, true);
        ImGui::SetCursorScreenPos(P(x0 + 20, y0 + 66));
        ImGui::SetNextItemWidth((x1 - x0 - 40) * s);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(238, 240, 244, 255));
        if (saveModalJustOpened) { ImGui::SetKeyboardFocusHere(); saveModalJustOpened = false; }
        const bool enter = ImGui::InputText("##presetname", saveNameBuf, sizeof(saveNameBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor(2);

        const std::string nm = scpreset::sanitize(saveNameBuf);
        const bool valid  = !nm.empty();
        const bool exists = valid && presetStore.exists(saveNameBuf);

        const float by = y0 + 150;   // button row baseline
        if (overwriteConfirm)
        {
            text(x0 + 20, y0 + 108, 11.0f, live.text, "A preset with that name exists. Overwrite?", -1);
            if (modalButton("OVERWRITE", x0 + 20,  by, 120, 30, true))  commitSave();
            if (modalButton("CANCEL",    x0 + 152, by, 120, 30, false)) overwriteConfirm = false;
        }
        else if (deleteConfirm)
        {
            char msg[160];
            std::snprintf(msg, sizeof msg, "Delete user preset \"%s\"?", presetName(currentPreset));
            text(x0 + 20, y0 + 108, 11.0f, live.text, msg, -1);
            if (modalButton("DELETE", x0 + 20,  by, 120, 30, true))  commitDelete();
            if (modalButton("CANCEL", x0 + 152, by, 120, 30, false)) deleteConfirm = false;
        }
        else
        {
            if (!valid)
                text(x0 + 20, y0 + 108, 10.0f, whiteDimCol(), "Enter a name to save.", -1);
            else if (exists)
                text(x0 + 20, y0 + 108, 10.0f, whiteDimCol(), "Name in use — saving will ask to overwrite.", -1);

            const bool doSave = modalButton("SAVE", x0 + 20, by, 120, 30, true) || (enter && valid);
            if (doSave && valid) { if (exists) overwriteConfirm = true; else commitSave(); }
            if (modalButton("CANCEL", x0 + 152, by, 120, 30, false)) showSaveModal = false;

            // DELETE affordance only when a user preset is currently recalled.
            if (currentPreset >= kNumFactoryPresets)
                if (modalButton("DELETE", x1 - 20 - 120, by, 120, 30, false)) deleteConfirm = true;
        }

        ImGui::PopFont();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showSaveModal = false;

        // Scrim close: click in the dark area outside the panel, no popup open.
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool insidePanel = mp.x >= pMin.x && mp.x <= pMax.x
                              && mp.y >= pMin.y && mp.y <= pMax.y;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !insidePanel
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
            showSaveModal = false;
    }

    //========================================================================
    // Scope + Output/VU
    //========================================================================
    void drawScope()
    {
        panelBox(1004, 60, 1224, 300);
        sectionTitle(1012, 64, "SCOPE");
        const float rx0 = 1012, ry0 = 82, rx1 = 1216, ry1 = 292;
        dl->AddRectFilled(P(rx0, ry0), P(rx1, ry1), IM_COL32(9, 11, 13, 255), 4.0f * s);
        dl->PushClipRect(P(rx0, ry0), P(rx1, ry1), true);
        const float midY = 0.5f * (ry0 + ry1);
        dl->AddLine(P(rx0, midY), P(rx1, midY), IM_COL32(255, 255, 255, 25), 1.0f * s);

        int count = 0;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiSynthGetDSP != nullptr)
            if (void* const inst = getPluginInstancePointer())
                if (msynth::MultiSynthDSP* d = multiSynthGetDSP(inst))
                    // Copy the ring (oldest->newest) into our preallocated buffer via
                    // the data-race-free bridge API (may tear, fine for a scope);
                    // no raw ring pointer / writePos math.
                    count = d->copyScope(scope, msynth::MultiSynthDSP::kScopeSize);
       #endif
        if (count > 0)
        {
            // rising zero-cross trigger over the first quarter
            int start = 0;
            for (int i = 1; i < count / 4; ++i)
                if (scope[i - 1] <= 0.0f && scope[i] > 0.0f) { start = i; break; }
            const int nPts = std::min(count - start, 204);
            // Need at least two points for a polyline; the (nPts-1) divisor and
            // AddPolyline both misbehave with a single point. The flat baseline
            // is already drawn above, so just skip the trace in that case.
            if (nPts >= 2)
            {
                const float halfH = 0.5f * (ry1 - ry0) * 0.9f;
                ImVec2 pts[204];
                const float midYpx = P(0, midY).y;
                for (int i = 0; i < nPts; ++i)
                {
                    const float x = rx0 + (rx1 - rx0) * (float)i / (float)(nPts - 1);
                    float v = scope[start + i]; if (v > 1) v = 1; if (v < -1) v = -1;
                    pts[i] = ImVec2(P(x, 0).x, midYpx - v * halfH * s);
                }
                dl->AddPolyline(pts, nPts, live.accent, 0, 1.6f * s);
            }
        }
        dl->PopClipRect();
        dl->AddRect(P(rx0, ry0), P(rx1, ry1), IM_COL32(0, 0, 0, 180), 4.0f * s, 0, 1.2f * s);
    }

    void drawOutputVU()
    {
        panelBox(1004, 304, 1224, 542);
        sectionTitle(1012, 308, "OUTPUT");

        float lL = values[kParamOutLevelL], lR = values[kParamOutLevelR];
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiSynthGetOutLevelL != nullptr)
            if (void* const inst = getPluginInstancePointer())
            { lL = multiSynthGetOutLevelL(inst); lR = multiSynthGetOutLevelR(inst); }
       #endif
        const float dt = ImGui::GetIO().DeltaTime;
        vuL = ballistic(vuL, lL, dt); vuR = ballistic(vuR, lR, dt);
        // bars start below the title so the per-channel clip LEDs (drawn at y0-8)
        // clear the "OUTPUT" header (defect 5)
        drawVUbar(1024, 338, 1048, 520, vuL, clipL);
        drawVUbar(1056, 338, 1080, 520, vuR, clipR);
        text(1036, 524, 9.0f, live.textPanel, "L", 0);
        text(1068, 524, 9.0f, live.textPanel, "R", 0);

        klabel(1130, 336, "VOLUME"); knob("mvol", kParamMasterVol, 1130, 372, 24, "%+.1f", " dB", true, false, true, 1.0f, 0.0f, true, false, true);
        klabel(1108, 440, "PAN");    knob("mpan", kParamMasterPan, 1108, 476, 20, "%+.0f", " %", true, false, false, 100.0f);
        klabel(1180, 440, "WIDTH");  knob("mwid", kParamStereoWidth, 1180, 476, 20, "%.0f", " %", false, false, false, 100.0f);
    }
    float ballistic(float disp, float target, float dt)
    {
        const float k = target > disp ? 18.0f : 5.0f;
        return disp + (target - disp) * (1.0f - std::exp(-dt * k));
    }
    void drawVUbar(float x0, float y0, float x1, float y1, float lvl, float& clipHold)
    {
        dl->AddRectFilled(P(x0 - 2, y0 - 2), P(x1 + 2, y1 + 2), IM_COL32(30, 30, 32, 255), 2.0f * s);
        dl->AddRectFilled(P(x0, y0), P(x1, y1), IM_COL32(10, 10, 11, 255));
        // lvl is ALREADY dBFS (core stores 20*log10 peak, -60..+6); do not re-log.
        const float dB = lvl;
        float h = (dB + 40.0f) / 46.0f; h = h < 0 ? 0 : (h > 1 ? 1 : h);
        const float top = y1 - (y1 - y0) * h;
        // segmented coloring
        const float dt = ImGui::GetIO().DeltaTime;
        if (lvl >= 0.0f) clipHold = 0.5f; else clipHold -= dt; if (clipHold < 0) clipHold = 0;
        for (float yy = y1; yy > top; yy -= 3.0f)
        {
            const float segDb = -40.0f + 46.0f * (y1 - yy) / (y1 - y0);
            ImU32 c = segDb > 0 ? IM_COL32(240, 60, 45, 255)
                    : segDb > -6 ? IM_COL32(240, 200, 40, 255) : IM_COL32(70, 210, 90, 255);
            dl->AddRectFilled(P(x0, yy - 2), P(x1, yy), c);
        }
        // clip LED
        dl->AddCircleFilled(P(0.5f * (x0 + x1), y0 - 8), 3.0f * s, clipHold > 0 ? IM_COL32(255, 40, 30, 255) : IM_COL32(60, 20, 18, 255), 12);
    }

    //========================================================================
    // Sequencer (mode-aware: single arp row, or 3-lane acid)
    //========================================================================
    int liveStep() const
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiSynthGetArpStep != nullptr)
            if (void* const inst = getPluginInstancePointer())
                return multiSynthGetArpStep(inst);
       #endif
        return -1;
    }
    void drawSequencer()
    {
        panelBox(16, 548, 700, 692);
        sectionTitle(24, 552, curMode == 5 ? "PATTERN SEQUENCER" : "SEQUENCER / ARP");

        // --- transport header (y 550..584): spaced so nothing collides (defect 4).
        // Mini-knobs get their label above and body below within the taller band.
        const bool acid = (curMode == 5);
        ledButton("arpon", kParamArpOn, 162, 560, 212, 580, "ARP");
        text(220, 552, 9.0f, live.textPanel, "MODE", -1, true);
        comboBox("arpmode", kParamArpMode, 218, 562, 300, 580, kArpMode, 7, acid);
        text(312, 552, 9.0f, live.textPanel, "RATE", -1, true);
        comboBox("arprate", kParamArpRate, 310, 562, 372, 580, kDivName, 14, acid);
        const float hy = 574.0f;
        klabel(398, 553, "OCT");   knob("arpoct", kParamArpOctave, 398, hy, 9, "%.0f", "", false, true);
        klabel(432, 553, "GATE");  knob("arpgate", kParamArpGate, 432, hy, 9, "%.0f", " %", false, false, false, 100.0f);
        klabel(470, 553, "SWING"); knob("arpswing", kParamArpSwing, 470, hy, 9, "%.0f", " %", false, false, false, 100.0f);
        ledButton("arplatch", kParamArpLatch, 500, 560, 548, 580, "LATCH");
        text(560, 552, 9.0f, live.textPanel, "VEL", -1, true);
        comboBox("arpvel", kParamArpVelMode, 556, 562, 628, 580, kArpVel, 3, acid);
        // Fixed-velocity value knob: only meaningful (and only shown) when the VEL
        // mode is Fixed (=1). Sits right of the combo, matching the OCT/GATE/SWING
        // minis; clears the LATCH button (ends x548) and the panel edge (x700).
        if ((int)std::lround(values[kParamArpVelMode]) == 1)
        { klabel(660, 553, "VEL"); knob("arpfvel", kParamArpFixedVel, 660, hy, 9, "%.0f", "", false, true); }

        const int step = liveStep();

        if (acid)
        {
            // 3-lane acid pattern sequencer with a left label gutter (x18..58) so
            // lane names never overlap the first cell (defect 6).
            const float gx = 62.0f, cw = (692.0f - gx) / 16.0f;
            drawLaneLabel(58, 590, 606, "GATE");
            drawStepRow(gx, 590, cw, 16, kParamArpStep0, step, false);
            drawLaneLabel(58, 612, 652, "PITCH");
            drawPitchLane(gx, 612, cw, 40, step);
            drawLaneLabel(58, 658, 672, "ACC");
            drawLaneLabel(58, 676, 690, "SLIDE");
            drawAccentSlideLanes(gx, 658, cw, step);
        }
        else
        {
            const float gx = 24.0f, cw = (692.0f - gx) / 16.0f;
            drawStepRow(gx, 590, cw, 96, kParamArpStep0, step, true);
        }
    }
    // Right-aligned lane label sitting in the acid sequencer's left gutter.
    void drawLaneLabel(float xRight, float y0, float y1, const char* t)
    { text(xRight, 0.5f * (y0 + y1) - 5.0f, 8.5f, live.textPanel, t, 1, true); }
    void drawStepRow(float x0, float y0, float cw, float h, uint32_t base, int step, bool tall)
    {
        for (int i = 0; i < 16; ++i)
        {
            const float cx0 = x0 + i * cw + 2, cx1 = x0 + (i + 1) * cw - 2;
            const float cy1 = y0 + h;
            const bool on = values[base + i] > 0.5f;
            char id[16]; std::snprintf(id, sizeof(id), "step%u_%d", (unsigned)base, i);
            const ImVec2 b0 = P(cx0, y0), b1 = P(cx1, cy1);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            if (ImGui::IsItemClicked()) setChoice(base + i, on ? 0 : 1);
            if (ImGui::IsItemHovered() && tips[base + i]) ImGui::SetTooltip("%s", tips[base + i]);
            dl->AddRectFilled(b0, b1, on ? withA(live.accent, 200) : IM_COL32(34, 36, 40, 255), 3.0f * s);
            if ((i % 4) == 0) dl->AddLine(P(cx0 - 2, y0), P(cx0 - 2, cy1), IM_COL32(255, 255, 255, 40), 1.2f * s);
            if (i == step)
            { dl->AddRect(b0, b1, live.ledOn, 3.0f * s, 0, 2.0f * s);
              dl->AddRectFilled(P(cx0, y0), P(cx1, y0 + 3), live.ledOn, 1.0f * s); }
            if (tall) { char n[4]; std::snprintf(n, sizeof(n), "%d", i + 1);
                        text(0.5f * (cx0 + cx1), y0 + h * 0.5f - 6, 10.0f, on ? live.text : live.textPanel, n, 0, on); }
        }
    }
    void drawPitchLane(float x0, float y0, float cw, float h, int step)
    {
        const float cy0 = y0, cy1 = y0 + h;
        const float midY = 0.5f * (cy0 + cy1);
        for (int i = 0; i < 16; ++i)
        {
            const float cx0 = x0 + i * cw + 1.5f, cx1 = x0 + (i + 1) * cw - 1.5f;
            const uint32_t p = kParamSeqPitch0 + i;
            char id[16]; std::snprintf(id, sizeof(id), "pit%d", i);
            const ImVec2 b0 = P(cx0, cy0), b1 = P(cx1, cy1);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            // cell background so the lane never reads as dead black (defect 6)
            dl->AddRectFilled(b0, b1, IM_COL32(26, 28, 32, 255), 2.0f * s);
            const bool active = ImGui::IsItemActive();
            if (active)
            {
                float raw = values[p] - ImGui::GetIO().MouseDelta.y * (48.0f / ((cy1 - cy0) * s));
                raw = raw < -24 ? -24 : (raw > 24 ? 24 : raw);
                // Shift = fine scrub: keep the sub-integer value internally for a
                // smooth drag, but ALWAYS push the ROUNDED integer. seqPitch is an
                // INT param; pushing a fraction lets the engine truncate toward
                // zero, which is asymmetric for negatives (-1.6 -> -1, not -2). (U5)
                const float fine = ImGui::GetIO().KeyShift ? raw : std::round(raw);
                if (fine != values[p])
                {
                    if (!pitchDragging) { beginEdit(p); pitchDragging = true; }
                    values[p] = fine;
                    setParam(p, std::round(fine));
                }
            }
            if (ImGui::IsItemDeactivated() && pitchDragging)
            {
                values[p] = std::round(values[p]); // snap the internal value on release
                setParam(p, values[p]);
                endEdit(p);
                pitchDragging = false;
            }
            // Double-click reset to 0 — but NOT while a drag is in progress, so the
            // reset branch can't open a second (nested) beginEdit on the frame a
            // drag just began. (U5)
            if (!pitchDragging && !ImGui::GetIO().KeyCtrl && ImGui::IsItemHovered()
                && ImGui::IsMouseDoubleClicked(0))
            { beginEdit(p); values[p] = 0; setParam(p, 0); endEdit(p); }
            if (ImGui::IsItemHovered())
            { char t[24]; std::snprintf(t, sizeof(t), "Step %d: %+d st", i + 1, (int)std::lround(values[p])); ImGui::SetTooltip("%s", t); }
            // filled bar from the 0-centre line to the value
            const float t01 = (values[p] + 24.0f) / 48.0f;
            const float vy = cy1 - (cy1 - cy0) * t01;
            const ImU32 col = i == step ? live.ledOn : withA(live.accent, 210);
            if (values[p] > 0)      dl->AddRectFilled(P(cx0, vy), P(cx1, midY), col, 1.0f * s);
            else if (values[p] < 0) dl->AddRectFilled(P(cx0, midY), P(cx1, vy), col, 1.0f * s);
            // per-step numeric value, always visible
            char nb[8]; std::snprintf(nb, sizeof(nb), "%+d", (int)std::lround(values[p]));
            text(0.5f * (cx0 + cx1), cy0 + 2.0f, 8.0f, IM_COL32(232, 235, 240, 255), nb, 0, true);
            if (i == step) dl->AddRect(b0, b1, live.ledOn, 2.0f * s, 0, 1.4f * s);
        }
        // 0-centre gridline on top
        dl->AddLine(P(x0, midY), P(x0 + 16 * cw, midY), IM_COL32(255, 255, 255, 90), 1.0f * s);
    }
    void drawAccentSlideLanes(float x0, float y0, float cw, int step)
    {
        for (int i = 0; i < 16; ++i)
        {
            const float cx0 = x0 + i * cw + 1.5f, cx1 = x0 + (i + 1) * cw - 1.5f;
            drawMiniCell(cx0, y0,      cx1, y0 + 14, kParamSeqAccent0 + i, i, step, IM_COL32(240, 200, 40, 255));
            drawMiniCell(cx0, y0 + 18, cx1, y0 + 32, kParamSeqSlide0 + i, i, step, IM_COL32(60, 200, 230, 255));
        }
    }
    void drawMiniCell(float x0, float y0, float x1, float y1, uint32_t p, int i, int step, ImU32 onCol)
    {
        const bool on = values[p] > 0.5f;
        char id[20]; std::snprintf(id, sizeof(id), "mc%u", (unsigned)p);
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) setChoice(p, on ? 0 : 1);
        if (ImGui::IsItemHovered() && tips[p]) ImGui::SetTooltip("%s", tips[p]);
        dl->AddRectFilled(b0, b1, on ? onCol : IM_COL32(34, 36, 40, 255), 2.0f * s);
        if (i == step) dl->AddRect(b0, b1, live.ledOn, 2.0f * s, 0, 1.4f * s);
    }

    //========================================================================
    // FX strip
    //========================================================================
    void drawFXStrip()
    {
        // Drive
        panelBox(708, 552, 834, 688);
        sectionTitle(714, 556, "DRIVE");
        ledButton("drvon", kParamDriveOn, 786, 556, 828, 572, "ON");
        comboBox("drvtype", kParamDriveType, 716, 580, 828, 600, kDriveType, 3);
        klabel(748, 606, "AMT"); knob("drvamt", kParamDriveAmt, 748, 640, 16, "%.0f", " %", false, false, false, 100.0f);
        klabel(796, 606, "MIX"); knob("drvmix", kParamDriveMix, 796, 640, 16, "%.0f", " %", false, false, false, 100.0f);

        // Chorus
        panelBox(838, 552, 964, 688);
        sectionTitle(844, 556, "CHORUS");
        ledButton("choon", kParamChorusOn, 916, 556, 958, 572, "ON");
        klabel(866, 588, "RATE");  knob("chorate", kParamChorusRate, 866, 620, 16, "%.2f", " Hz");
        klabel(910, 588, "DEPTH"); knob("chodep", kParamChorusDepth, 910, 620, 16, "%.0f", " %", false, false, false, 100.0f);
        klabel(940, 640, "MIX");   knob("chomix", kParamChorusMix, 940, 664, 12, "%.0f", " %", false, false, false, 100.0f);

        // Delay
        panelBox(968, 552, 1094, 688);
        sectionTitle(974, 556, "DELAY");
        ledButton("dlyon", kParamDelayOn, 1046, 556, 1088, 572, "ON");
        const bool sync = values[kParamDelaySync] > 0.5f;
        ledButton("dlysync", kParamDelaySync, 974, 578, 1030, 594, "SYNC");
        if (sync) { text(1036, 580, 8.0f, live.textPanel, "DIV", -1);
                    comboBox("dlydiv", kParamDelayDiv, 1036, 590, 1088, 606, kDivName, 14); }
        else      { klabel(1000, 598, "TIME"); knob("dlytime", kParamDelayTime, 1000, 626, 14, "%.0f", " ms", false, false, false, 1.0f, 0.0f, true, true); }
        klabel(1046, 598, "FB");  knob("dlyfb", kParamDelayFB, 1046, 626, 14, "%.0f", " %", false, false, false, 100.0f);
        klabel(1000, 640, "MIX"); knob("dlymix", kParamDelayMix, 1000, 664, 12, "%.0f", " %", false, false, false, 100.0f);
        ledButton("dlypp", kParamDelayPP, 1040, 656, 1064, 672, "P-P", true);
        ledButton("dlytape", kParamDelayTape, 1068, 656, 1092, 672, "TAPE", true);

        // Reverb
        panelBox(1098, 552, 1224, 688);
        sectionTitle(1104, 556, "REVERB");
        ledButton("rvbon", kParamReverbOn, 1176, 556, 1218, 572, "ON");
        // Modular auto-engages a spring reverb: show the badge as a bordered tag in
        // the panel body (under the header) so it never collides with the title.
        if (curMode == 3)
        {
            dl->AddRectFilled(P(1104, 573), P(1158, 585), withA(live.accent, 40), 3.0f * s);
            dl->AddRect(P(1104, 573), P(1158, 585), live.accent, 3.0f * s, 0, 1.0f * s);
            text(1131, 575, 8.0f, live.accent, "SPRING", 0, true);
        }
        klabel(1124, 588, "SIZE");  knob("rvbsize", kParamReverbSize, 1124, 618, 14, "%.0f", " %", false, false, false, 100.0f);
        klabel(1160, 588, "DECAY"); knob("rvbdec", kParamReverbDecay, 1160, 618, 14, "%.1f", " s");
        klabel(1196, 588, "DAMP");  knob("rvbdamp", kParamReverbDamp, 1196, 618, 14, "%.0f", " %", false, false, false, 100.0f);
        klabel(1124, 640, "MIX");   knob("rvbmix", kParamReverbMix, 1124, 664, 12, "%.0f", " %", false, false, false, 100.0f);
        klabel(1180, 640, "P-DLY"); knob("rvbpd", kParamReverbPD, 1180, 664, 12, "%.0f", " ms", false, false, false, 1.0f, 0.0f, true, true);
    }

    //========================================================================
    // Keyboard (playable, → MIDI via sendNote)
    //========================================================================
    void drawKeyboard()
    {
        // OCT- / OCT+
        octButton("octdn", 16, 700, 48, 738, "OCT-", -12, "Shift keyboard octave down");
        octButton("octup", 16, 742, 48, 780, "OCT+", +12, "Shift keyboard octave up");

        const float kx0 = 52.0f, kx1 = 1224.0f;
        const float w = (kx1 - kx0) / 21.0f;
        const int whiteSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const float yTop = 700, yBot = 780, yBlackBot = 750;
        const float bw = 0.62f * w;

        // white keys first (so overlapping black keys, submitted later, win hover)
        for (int i = 0; i < 21; ++i)
        {
            const float x = kx0 + i * w;
            const int note = clampMidi(baseMidi + (i / 7) * 12 + whiteSemi[i % 7]);
            char id[16]; std::snprintf(id, sizeof(id), "wk%d", i);
            keyHit(id, x + 1, yTop, x + w - 1, yBot, note);
            const bool lit = (kbNote == note);
            dl->AddRectFilled(P(x + 1, yTop), P(x + w - 1, yBot),
                              lit ? withA(live.accent, 220) : IM_COL32(238, 238, 240, 255), 2.0f * s);
            dl->AddRect(P(x + 1, yTop), P(x + w - 1, yBot), IM_COL32(60, 60, 64, 255), 2.0f * s, 0, 1.0f * s);
        }
        // black keys
        for (int i = 0; i < 20; ++i)
        {
            const int deg = i % 7;
            if (deg == 2 || deg == 6) continue; // no black after E or B
            const float boundary = kx0 + (i + 1) * w;
            const int note = clampMidi(baseMidi + (i / 7) * 12 + whiteSemi[deg] + 1);
            char id[16]; std::snprintf(id, sizeof(id), "bk%d", i);
            const float x0 = boundary - bw * 0.5f, x1 = boundary + bw * 0.5f;
            keyHit(id, x0, yTop, x1, yBlackBot, note);
            const bool lit = (kbNote == note);
            dl->AddRectFilled(P(x0, yTop), P(x1, yBlackBot),
                              lit ? withA(live.accent, 240) : IM_COL32(18, 18, 20, 255), 2.0f * s);
            dl->AddRect(P(x0, yTop), P(x1, yBlackBot), IM_COL32(0, 0, 0, 255), 2.0f * s, 0, 1.0f * s);
        }
        if (ImGui::IsMouseReleased(0) && kbNote >= 0) { sendNote(0, (uint8_t)kbNote, 0); kbNote = -1; }
    }
    void keyHit(const char* id, float x0, float y0, float x1, float y1, int note)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (hov && ImGui::IsMouseClicked(0)) pressKey(note);
        else if (hov && ImGui::IsMouseDown(0) && kbNote != note && kbNote >= 0) pressKey(note); // glissando
    }
    static int clampMidi(int n) noexcept { return n < 0 ? 0 : (n > 127 ? 127 : n); }
    void pressKey(int note)
    {
        note = clampMidi(note);               // defensive: never emit an out-of-range note
        if (kbNote == note) return;
        if (kbNote >= 0) sendNote(0, (uint8_t)kbNote, 0);
        sendNote(0, (uint8_t)note, 100);
        kbNote = note;
    }
    void octButton(const char* id, float x0, float y0, float x1, float y1, const char* lab, int delta, const char* tip = nullptr)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        // Cap baseMidi at 84 so the top generated key (base + 35) stays <= 127 MIDI.
        if (ImGui::IsItemClicked()) { baseMidi += delta; if (baseMidi < 12) baseMidi = 12; if (baseMidi > 84) baseMidi = 84; }
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        dl->AddRectFilled(b0, b1, IM_COL32(38, 38, 41, 255), 3.0f * s);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 3.0f * s, 0, 1.0f * s);
        text(0.5f * (x0 + x1), y0 + 8, 9.0f, live.text, lab, 0, true);
        char oc[8]; std::snprintf(oc, sizeof(oc), "C%d", baseMidi / 12 - 1);
        text(0.5f * (x0 + x1), y0 + 22, 8.0f, live.accent, oc, 0);
    }

    //========================================================================
    // tooltips
    //========================================================================
    void buildTooltips()
    {
        for (int i = 0; i < (int)kNumCoreParams; ++i) tips[i] = "";
        tips[kParamMode] = "Selects the synth engine and its personality.";
        tips[kParamMasterTune] = "Global fine tuning of every voice, in cents.";
        tips[kParamMasterVol] = "Overall output level.";
        tips[kParamMasterPan] = "Stereo position of the whole instrument.";
        tips[kParamStereoWidth] = "Widens or narrows the stereo image.";
        tips[kParamOversampling] = "Internal oversampling; higher rejects aliasing at more CPU cost.";
        tips[kParamAnalogAmt] = "Analog character: subtle drift, detune and noise.";
        tips[kParamVintage] = "Age and wear: slow pitch wobble plus faint background hiss.";
        tips[kParamOsc1Wave] = tips[kParamOsc2Wave] = tips[kParamOsc3Wave] = "Waveform of the oscillator.";
        tips[kParamOsc1Detune] = tips[kParamOsc2Detune] = "Fine detune of the oscillator, in cents.";
        tips[kParamOsc1PW] = tips[kParamOsc2PW] = "Pulse width of the oscillator (square and pulse waves).";
        tips[kParamOsc1Level] = tips[kParamOsc2Level] = tips[kParamOsc3Level] = "Level of the oscillator in the mix.";
        tips[kParamOsc2Semi] = "Coarse tuning of oscillator 2, in semitones.";
        tips[kParamSubLevel] = "Level of the sub-oscillator, one octave below oscillator 1.";
        tips[kParamSubWave] = "Sub-oscillator waveform.";
        tips[kParamNoiseLevel] = "Amount of noise blended into the voice.";
        tips[kParamFilterCutoff] = "Filter cutoff frequency.";
        tips[kParamFilterRes] = "Resonance; high settings emphasize the cutoff and can self-oscillate.";
        tips[kParamFilterHP] = "High-pass cutoff that thins the low end.";
        tips[kParamFilterEnvAmt] = "How far the filter envelope opens or closes the cutoff.";
        tips[kParamAmpA] = tips[kParamFiltA] = "Attack time of the envelope.";
        tips[kParamAmpD] = tips[kParamFiltD] = "Decay time of the envelope.";
        tips[kParamAmpS] = tips[kParamFiltS] = "Sustain level of the envelope.";
        tips[kParamAmpR] = tips[kParamFiltR] = "Release time of the envelope.";
        tips[kParamAmpCurve] = tips[kParamFiltCurve] = "Shape of the envelope segments.";
        tips[kParamCrossMod] = "Oscillator 2 modulates oscillator 1 frequency at audio rate.";
        tips[kParamRingMod] = "Ring modulation between oscillators 1 and 2.";
        tips[kParamHardSync] = "Oscillator 2 hard-syncs to oscillator 1 for tearing timbres.";
        tips[kParamFMAmount] = "Linear FM from oscillator 1 into oscillator 2.";
        tips[kParamPmFenvOscA] = "Poly-mod: filter envelope to oscillator 1 pitch.";
        tips[kParamPmFenvFilt] = "Poly-mod: filter envelope added to the filter cutoff.";
        tips[kParamPmOscBOscA] = "Poly-mod: oscillator 2 to oscillator 1 pitch.";
        tips[kParamPmOscBPWM] = "Poly-mod: oscillator 2 to oscillator 1 pulse width.";
        tips[kParamShRate] = "Sample-and-hold clock rate.";
        tips[kParamCosmosChorus] = "Built-in chorus mode: off, I, II, or both.";
        tips[kParamLfo1Rate] = tips[kParamLfo2Rate] = "Speed of the LFO.";
        tips[kParamLfo1Shape] = tips[kParamLfo2Shape] = "Waveform of the LFO.";
        tips[kParamLfo1Fade] = tips[kParamLfo2Fade] = "Time for the LFO to fade in after a note.";
        tips[kParamLfo1Sync] = tips[kParamLfo2Sync] = "Lock the LFO speed to host tempo.";
        tips[kParamUnisonVoices] = "Stacked detuned voices per note.";
        tips[kParamUnisonDetune] = "Spread of detuning across unison voices, in cents.";
        tips[kParamUnisonSpread] = "Stereo spread of unison voices.";
        tips[kParamPortaTime] = "Glide time between notes.";
        tips[kParamLegato] = "Glide only when notes overlap.";
        tips[kParamGlideMode] = "Glide as a fixed time or a fixed rate.";
        tips[kParamVelSens] = "How strongly velocity affects level.";
        tips[kParamVelCurve] = "Response curve applied to incoming velocity.";
        tips[kParamPbRange] = "Pitch-bend range, in semitones.";
        tips[kParamArpOn] = "Enable the arpeggiator / step sequencer.";
        tips[kParamArpMode] = "Note order the arpeggiator plays.";
        tips[kParamArpOctave] = "Range the arpeggio spans, in octaves.";
        tips[kParamArpRate] = "Step length as a note division.";
        tips[kParamArpGate] = "Length of each step relative to its slot.";
        tips[kParamArpSwing] = "Delays off-beat steps for a swung feel.";
        tips[kParamArpLatch] = "Hold the pattern after keys are released.";
        tips[kParamArpVelMode] = "Velocity source for steps: as played, fixed, or accented.";
        tips[kParamArpFixedVel] = "Velocity used when the mode is fixed.";
        for (int i = 0; i < 16; ++i) tips[kParamArpStep0 + i] = "Turn this step on or off.";
        tips[kParamDriveOn] = "Enable the drive stage.";
        tips[kParamDriveType] = "Drive character: soft, hard, or tube.";
        tips[kParamDriveAmt] = "Amount of drive.";
        tips[kParamDriveMix] = "Blend of driven and clean signal.";
        tips[kParamChorusOn] = "Enable the chorus.";
        tips[kParamChorusRate] = "Chorus modulation speed.";
        tips[kParamChorusDepth] = "Chorus modulation depth.";
        tips[kParamChorusMix] = "Chorus wet/dry blend.";
        tips[kParamDelayOn] = "Enable the delay.";
        tips[kParamDelaySync] = "Lock delay time to host tempo.";
        tips[kParamDelayTime] = "Delay time in milliseconds (when not synced).";
        tips[kParamDelayDiv] = "Delay time as a note division (when synced).";
        tips[kParamDelayFB] = "Delay feedback amount.";
        tips[kParamDelayMix] = "Delay wet/dry blend.";
        tips[kParamDelayPP] = "Ping-pong the delay across the stereo field.";
        tips[kParamDelayTape] = "Adds tape-style warmth and saturation to the delay.";
        tips[kParamReverbOn] = "Enable the reverb.";
        tips[kParamReverbSize] = "Size of the reverb space.";
        tips[kParamReverbDecay] = "Reverb tail length.";
        tips[kParamReverbDamp] = "High-frequency damping of the tail.";
        tips[kParamReverbMix] = "Reverb wet/dry blend.";
        tips[kParamReverbPD] = "Pre-delay before the reverb begins.";
        for (int i = 0; i < 8; ++i)
        { tips[kParamModSrc0 + i] = "Modulation source for this slot.";
          tips[kParamModDst0 + i] = "Modulation destination for this slot.";
          tips[kParamModAmt0 + i] = "Amount and polarity of this slot's modulation."; }
        tips[kParamPrismAlgo] = "Operator routing algorithm.";
        tips[kParamPrismFB] = "Feedback on the feedback operator, for growl and edge.";
        for (int op = 0; op < 4; ++op)
        {
            const uint32_t b = kParamOp1Ratio + op * 9;
            tips[b + 0] = "Frequency ratio of the operator to the played note.";
            tips[b + 1] = "Fine detune of the operator, in cents.";
            tips[b + 2] = "Output level of the operator (modulation depth or volume).";
            tips[b + 3] = "How strongly velocity affects the operator's level.";
            tips[b + 4] = "Level change of the operator across the keyboard.";
            tips[b + 5] = "Attack of the operator's envelope.";
            tips[b + 6] = "Decay of the operator's envelope.";
            tips[b + 7] = "Sustain of the operator's envelope.";
            tips[b + 8] = "Release of the operator's envelope.";
        }
        tips[kParamAcidAccentAmt] = "How much accented steps boost level, resonance, and envelope.";
        tips[kParamAcidSlideTime] = "Glide time for slid steps.";
        for (int i = 0; i < 16; ++i)
        { tips[kParamSeqPitch0 + i] = "Pitch of this step relative to the held note, in semitones.";
          tips[kParamSeqAccent0 + i] = "Accent this step.";
          tips[kParamSeqSlide0 + i] = "Slide into this step."; }
    }

    //========================================================================
    // state
    //========================================================================
    duskdpf::DuskPanel panel;
    duskdpf::CrispFontSet fontSet;
    ImDrawList* dl = nullptr;
    float  values[kParamCount] = {};
    float  defaults[kParamCount] = {};
    const char* tips[kNumCoreParams] = {};
    float  s = 1.0f;
    ImVec2 org = ImVec2(0, 0);

    // mode crossfade
    int    curMode = 0, prevMode = 0;
    float  modeBlend = 1.0f;
    MSPal  fromPal{}, live{};

    // currentPreset is a COMBINED index: 0..kNumFactoryPresets-1 select factory
    // presets; kNumFactoryPresets + n selects user preset n in presetStore.list().
    // -1 = nothing recalled. programLoaded() (host->UI) only ever maps to factory.
    int    currentPreset = -1;
    bool   showMod = false;

    // user preset library + save modal state
    scpreset::Store presetStore;
    bool   showSaveModal = false;
    bool   saveModalJustOpened = false;
    bool   overwriteConfirm = false;
    bool   deleteConfirm = false;
    char   saveNameBuf[128] = {};

    // filter-curve cache
    static constexpr int kFcN = 180;
    float  fcX[kFcN] = {}, fcY[kFcN] = {};
    float  fcCutoff = -1, fcRes = -1, fcHP = -1; int fcMode = -1;

    // ADSR caches
    static constexpr int kAdsrN = 40;
    ImVec2 ampEnv[kAdsrN], filtEnv[kAdsrN];
    float  ampHash = -1, filtHash = -1;

    // scope
    float  scope[msynth::MultiSynthDSP::kScopeSize] = {};

    // VU ballistics
    float  vuL = -60.0f, vuR = -60.0f, clipL = 0, clipR = 0; // vu smoothed in dBFS

    // keyboard
    int    baseMidi = 48;   // C3
    int    kbNote = -1;

    // misc animation
    float  shPhase = 0.0f;
    bool   pitchDragging = false;

    // local skew/ratio-knob drag state (one knob active at a time, like the shared knob)
    float  skewL = 0.0f;
    bool   skewReset = false;

   #ifdef MSYNTH_FRAME_PROFILE
    double profLogic[100] = {}, profTotal[100] = {};
    int    profN = -20; // skip warmup frames
   #endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiSynthUI)
};

UI* createUI()
{
    return new MultiSynthUI();
}

END_NAMESPACE_DISTRHO
