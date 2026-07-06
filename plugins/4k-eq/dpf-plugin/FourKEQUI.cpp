// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// FourKEQUI.cpp — Dear ImGui UI for 4K EQ 2, matching the JUCE 4K EQ front
// panel: a full-width response graph over six console channel-strip columns
// (FILTERS | LF | LMF | HMF | HF | MASTER) with per-band colour coding
// (LF red, LMF orange, HMF green, HF blue), INPUT/OUTPUT edge meters, preset
// and oversample selectors, a Brown/Black voicing toggle and Hide Graph.
// All custom ImDrawList rendering in a 960x680 design space, uniformly scaled.
// The response curve is computed from the SAME coefficient math as the audio
// path (FourKEQDSP designers), never by probing audio; a live FFT of the
// pre/post spectrum is drawn behind it.

#include "DistrhoUI.hpp"
#include "FourKEQAccess.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQDSP.hpp"

#include "DuskImGuiFont.hpp"
#include "DuskImGuiWidgets.hpp"
#include "PatreonBackersDpf.hpp"

#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 960.0f;
    constexpr float kDesignH = 680.0f;             // graph shown
    constexpr float kDesignHCollapsed = 556.0f;    // graph hidden (band removed)
    constexpr float kDbRange = 20.0f;               // graph vertical: +-20 dB
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;

    // graph + meter rects
    constexpr float GX0 = 9, GY0 = 98, GX1 = 951, GY1 = 214; // outer frame lines up with IN/OUT meter outer edges
    constexpr float INX0 = 8,   INX1 = 34,  MET_Y0 = 246, MET_Y1 = 656;
    constexpr float MET_LBL_Y = 228;  // INPUT/OUTPUT caption, inside the control band
    constexpr float OUTX0 = 926, OUTX1 = 952;
    // column dividers
    constexpr float COL[7] = { 40, 186, 331, 477, 622, 768, 920 };

    // preset dropdown box (design coords) — shared by the header box + popup.
    // Aligned to the header control-row height so the box + buttons line up.
    constexpr float kPresetX0 = 200.f, kPresetY0 = 26.f, kPresetX1 = 396.f, kPresetY1 = 56.f;
    constexpr float kPresetRowH = 22.f;
    constexpr float kHdrY0 = 26.f, kHdrY1 = 56.f;   // header button row

    // band face colours
    constexpr ImU32 C_LF_BROWN  = IM_COL32(96, 56, 48, 255);   // SSL LF maroon console knob
    constexpr ImU32 C_LMF_BLUE  = IM_COL32(56, 100, 156, 255); // SSL LMF blue console knob
    constexpr ImU32 C_HMF_GREEN = IM_COL32(58, 108, 58, 255);  // SSL HMF green console knob
    constexpr ImU32 C_HF_RED    = IM_COL32(158, 52, 46, 255);  // SSL HF red console knob
    constexpr ImU32 C_LF  = IM_COL32(196, 74, 66, 255);
    constexpr ImU32 C_LMF = IM_COL32(202, 132, 66, 255);
    constexpr ImU32 C_HMF = IM_COL32(104, 168, 92, 255);
    constexpr ImU32 C_HF  = IM_COL32(84, 146, 204, 255);
    constexpr ImU32 C_GREY = IM_COL32(92, 94, 99, 255);

    // Master-knob tick rings (continuous knobs: value + label per tick).
    constexpr float  MK_GAIN_V[]  = { -12.f, -6.f, 0.f, 6.f, 12.f };
    const     char*  MK_GAIN_L[]  = { "12", "6", "0", "6", "12" };
    constexpr float  MK_DRIVE_V[] = { 0.f, 25.f, 50.f, 75.f, 100.f };
    const     char*  MK_DRIVE_L[] = { "0", "25", "50", "75", "100" };

    constexpr ImU32 kPanel   = IM_COL32(34, 34, 37, 255);
    constexpr ImU32 kPanelDk = IM_COL32(24, 24, 26, 255);
    constexpr ImU32 kHeader  = IM_COL32(18, 18, 20, 255);
    constexpr ImU32 kAmber   = IM_COL32(150, 96, 32, 255);
    constexpr ImU32 kGreenBtn = IM_COL32(48, 108, 56, 255);

    constexpr float kDefaults[kParamCount] = {
        20.f, 0.f, 20000.f, 0.f,
        0.f, 100.f, 0.f, 0.f, 600.f, 0.7f, 0.f, 2000.f, 0.7f, 0.f, 8000.f, 0.f,
        0.f,            // eq type
        0.f,            // bypass
        0.f, 0.f, 0.f,  // in/out gain, sat
        0.f, 0.f, 0.f,  // os, ms, spectrum pre/post
        1.f,            // auto gain
        1.f,            // show graph
        0.f, 0.f,       // out peaks
    };

    const int   kGridF[]  = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const char* kGridFL[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
}

class FourKEQUI : public UI, public duskdpf::ParamHost
{
public:
    FourKEQUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kDefaults[i];
        // No hard aspect lock: the Hide Graph toggle changes the window aspect
        // between the shown/collapsed heights, and onImGuiDisplay letterboxes any
        // size cleanly (scale = min ratio), so free resize never distorts.
        updateSizeConstraints();  // aspect-locked resize (no letterbox margins)
        // Multi-size atlas: the bold face at several native sizes spanning the
        // on-screen text range, so each label is drawn near-native (crisp) — one
        // oversized atlas blurred small text by downscaling it 3-5x.
        static const float kFontSizes[] = { 9.f, 11.f, 13.f, 16.f, 20.f, 26.f };
        fontSet = duskdpf::loadCrispFontSet(kFontSizes, 6, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        fft.prepare(kFftSize);
        specDb.assign(kFftSize / 2 + 1, -120.0f);
    }

    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        values[index] = value;
        if (index == kShowGraph)
        {
            // restore persisted graph state on UI (re)open; size on next frame
            showGraph = value > 0.5f;
            needResize = true;
        }
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth(), winH = (float)getHeight();

        // Request the host window match the current graph state (some hosts honor
        // it and snap tight; those that keep their frame are handled by the fill
        // below). Done here, outside the ImGui frame, so the resize is not lost.
        if (needResize) { applyGraphSize(); needResize = false; }

        // Uniform scale + letterbox: scale the whole design by the SMALLER of the
        // width/height ratios and centre it. Scaling by width alone (with the
        // control band stretched to fill height) made knobs grow with width while
        // vertical spacing shrank on a wider-than-design window -> labels collided.
        // Uniform scale locks knobs and layout together at any window aspect;
        // leftover area becomes a clean chassis-coloured margin.
        const float designH = showGraph ? kDesignH : kDesignHCollapsed;
        const float s = std::min(winW / kDesignW, winH / designH);
        const ImVec2 org((winW - kDesignW * s) * 0.5f, (winH - designH * s) * 0.5f);
        panel.begin(s, org, labelFont, this);

        // Control rows keep their design spacing (no vertical compression); shift
        // up when the graph is hidden to fill the reclaimed band.
        ctlDstTop_ = showGraph ? 220.0f : 96.0f;
        ctlScaleY_ = 1.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("4KEQ2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(30, 30, 33, 255)); // chassis fills window

        drawHeader(dl);
        if (showGraph)
            drawGraph(dl);
        // While a modal (preset list / credits) is open, absorb input BEFORE the
        // controls are submitted so knobs/buttons underneath don't react through
        // it. Submitted here (before drawColumns) so it wins ImGui's hover — the
        // modal's own rows are hit-tested manually, so they still work over it.
        if (presetOpen || showCredits)
        {
            ImGui::SetCursorScreenPos(ImVec2(0, 0));
            ImGui::InvisibleButton("modalblock", ImVec2(winW, winH));
        }
        drawColumns(dl);
        drawMeters(dl);
        if (presetOpen)
            drawPresetPopup(dl, winW, winH);
        if (showCredits)
            drawCredits(dl, winW, winH);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    // Control-area vertical remap: maps a design Y in the [220..662] band to the
    // stretched/repositioned band so the strip always fills the window height.
    // Knob radii use the width scale only, so circles stay circular.
    float cY(float y) const { return ctlDstTop_ + (y - 220.0f) * ctlScaleY_; }

    // Match the JUCE Hide/Show Graph: persist the state and request the host to
    // resize (grow/shrink) to the matching height.
    void toggleGraph()
    {
        showGraph = !showGraph;
        editParameter(kShowGraph, true);
        values[kShowGraph] = showGraph ? 1.0f : 0.0f;
        setParameterValue(kShowGraph, values[kShowGraph]);
        editParameter(kShowGraph, false);
        applyGraphSize();
    }

    void applyGraphSize()
    {
        const float designH = showGraph ? kDesignH : kDesignHCollapsed;
        updateSizeConstraints();
        setSize((uint)getWidth(), (uint)std::lround((double)getWidth() * designH / kDesignW));
    }

    // Lock the resize aspect ratio to the current design (graph shown vs hidden)
    // so the host constrains dragging to it — the UI fills the window with no
    // letterbox margins. The onImGuiDisplay uniform scale is the fallback for
    // hosts that ignore the constraint.
    void updateSizeConstraints()
    {
        const float designH = showGraph ? kDesignH : kDesignHCollapsed;
        const uint minW = 560;
        const uint minH = (uint)std::lround((double)minW * designH / kDesignW);
        setGeometryConstraints(minW, minH, /*keepAspectRatio*/ true);
    }

private:
    static constexpr int kFftSize = 2048;
    const auto& pal() const { return panel.palette(); }
    float sc() const { return panel.scale(); }

    //========================================================================
    // header
    //========================================================================
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 88), kHeader);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, 88), panel.P(kDesignW, 88), IM_COL32(60, 60, 63, 255), 1.5f * sc());

        panel.text(dl, 28, 28, 25, pal().white, "4K EQ 2", -1, true);
        panel.text(dl, 30, 58, 10.5f, IM_COL32(150, 152, 156, 255), "Console-Style Equalizer", -1);
        // Clickable title -> Patreon supporters overlay.
        {
            const ImVec2 t0 = panel.P(26, 20), t1 = panel.P(150, 48);
            ImGui::SetCursorScreenPos(t0);
            ImGui::InvisibleButton("titlecredits", ImVec2(t1.x - t0.x, t1.y - t0.y));
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) { showCredits = true; creditsArmed = false; }
        }
        panel.text(dl, kDesignW - 26, 68, 10.5f, IM_COL32(140, 142, 146, 255), "DUSK AUDIO", 1, true);

        // preset dropdown — custom widget (ImGui combo's SetWindowFontScale was
        // blurry). Crisp panel.text; the expanded list is drawn later, on top.
        {
            const ImVec2 p0 = panel.P(kPresetX0, kPresetY0), p1 = panel.P(kPresetX1, kPresetY1);
            dl->AddRectFilled(p0, p1, IM_COL32(46, 46, 50, 255), 3.f * sc());
            dl->AddRect(p0, p1, presetOpen ? IM_COL32(120, 150, 200, 240) : IM_COL32(90, 90, 96, 220), 3.f * sc(), 0, 1.f * sc());
            const char* preview = (currentPreset >= 0 && currentPreset < kNumFactoryPresets)
                                      ? kFactoryPresets[currentPreset].name : "Default";
            panel.text(dl, kPresetX0 + 9.f, 0.5f * (kPresetY0 + kPresetY1) - 6.5f, 13.f, IM_COL32(228, 228, 224, 255), preview, -1);
            const ImVec2 ac = panel.P(kPresetX1 - 13.f, 0.5f * (kPresetY0 + kPresetY1));
            dl->AddTriangleFilled(ImVec2(ac.x - 4.f * sc(), ac.y - 2.5f * sc()),
                                  ImVec2(ac.x + 4.f * sc(), ac.y - 2.5f * sc()),
                                  ImVec2(ac.x, ac.y + 3.5f * sc()), IM_COL32(180, 182, 186, 255));
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("presetbox", ImVec2(p1.x - p0.x, p1.y - p0.y));
            if (ImGui::IsItemClicked()) presetOpen = !presetOpen;
        }

        // Control row — evenly spaced, uniform height (kHdrY0..kHdrY1).
        const ImU32 btnBg = IM_COL32(46, 46, 50, 255);

        // Oversample (cycles 1x/2x/4x).
        static const char* kOsBtn[3] = { "Oversample: 1x", "Oversample: 2x", "Oversample: 4x" };
        int osi = (int)(values[kOversampling] + 0.5f); osi = osi < 0 ? 0 : (osi > 2 ? 2 : osi);
        headerButton(dl, "os", 412, kHdrY0, 536, kHdrY1, kOsBtn[osi], btnBg, pal().white,
                     [&]{ cycleParam(kOversampling, 3); });

        // Brown / Black voicing (amber when Brown, blue-grey when Black).
        const bool brown = values[kEqType] < 0.5f;
        headerButton(dl, "eqtype", 548, kHdrY0, 640, kHdrY1, brown ? "BROWN" : "BLACK",
                     brown ? kAmber : IM_COL32(60, 74, 96, 255), IM_COL32(245, 240, 232, 255),
                     [&]{ cycleParam(kEqType, 2); });

        // --- Analyzer group: Graph collapse | FFT on/off | spectrum source ---
        headerButton(dl, "hidegraph", 652, kHdrY0, 740, kHdrY1, showGraph ? "Hide Graph" : "Show Graph",
                     btnBg, pal().white, [&]{ toggleGraph(); });

        headerButton(dl, "fft", 752, kHdrY0, 822, kHdrY1, showFft ? "FFT: On" : "FFT: Off",
                     showFft ? IM_COL32(52, 70, 92, 255) : btnBg,
                     showFft ? IM_COL32(210, 224, 240, 255) : IM_COL32(150, 152, 156, 255),
                     [&]{ showFft = !showFft; });

        headerButton(dl, "prepost", 834, kHdrY0, 920, kHdrY1,
                     values[kSpectrumPrePost] > 0.5f ? "FFT PRE" : "FFT POST",
                     btnBg, IM_COL32(180, 182, 186, 255),
                     [&]{ toggleParam(kSpectrumPrePost); });
    }

    template <class Fn>
    void headerButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                      const char* label, ImU32 bg, ImU32 fg, Fn onClick)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        dl->AddRectFilled(b0, b1, bg, 4.0f * sc());
        dl->AddRect(b0, b1, hov ? IM_COL32(200, 200, 205, 200) : IM_COL32(90, 90, 96, 200), 4.0f * sc(), 0, 1.2f * sc());
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 10.5f, fg, label, 0, true);
    }

    void cycleParam(uint32_t id, int n)
    {
        float nv = values[id] + 1.0f; if (nv > n - 1 + 0.5f) nv = 0.0f;
        editParameter(id, true); values[id] = nv; setParameterValue(id, nv); editParameter(id, false);
    }
    void toggleParam(uint32_t id)
    {
        float nv = values[id] > 0.5f ? 0.0f : 1.0f;
        editParameter(id, true); values[id] = nv; setParameterValue(id, nv); editParameter(id, false);
    }

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumFactoryPresets) return;
        currentPreset = idx;
        const FourKEQPreset& p = kFactoryPresets[idx];
        struct KV { uint32_t id; float v; };
        const KV kv[] = {
            {kLfGain,p.lfGain},{kLfFreq,p.lfFreq},{kLfBell,p.lfBell},
            {kLmGain,p.lmGain},{kLmFreq,p.lmFreq},{kLmQ,p.lmQ},
            {kHmGain,p.hmGain},{kHmFreq,p.hmFreq},{kHmQ,p.hmQ},
            {kHfGain,p.hfGain},{kHfFreq,p.hfFreq},{kHfBell,p.hfBell},
            {kHpfFreq,p.hpfFreq},{kLpfFreq,p.lpfFreq},
            {kSaturation,p.saturation},{kOutputGain,p.outputGain},
            {kInputGain,p.inputGain},{kEqType,p.eqType},
            {kHpfEnabled, p.hpfFreq > 20.5f ? 1.0f : 0.0f},
            {kLpfEnabled, p.lpfFreq < 19999.0f ? 1.0f : 0.0f},
        };
        for (const KV& e : kv) { editParameter(e.id, true); values[e.id] = e.v; setParameterValue(e.id, e.v); editParameter(e.id, false); }
    }

    //========================================================================
    // response graph + spectrum
    //========================================================================
    float responseDb(float freq) const
    {
        using duskaudio::Biquad; using duskaudio::BiquadCoeffs;
        using duskaudio::FourKEQDSP;
        // Evaluate at the DSP's ACTUAL processing rate: host base rate * the
        // rate-capped oversampling factor, exactly as recomputeCoeffs() designs
        // the biquads. A fixed 96 kHz warps the drawn curve away from the sound
        // at any other host rate / oversampling factor. kOversampling is already
        // the DSP mode (0=1x,1=2x,2=4x), so chooseFactor() applies the same cap.
        const double base = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const double fs = base * (double) FourKEQDSP::chooseFactor(base, (int)(values[kOversampling] + 0.5f));
        const double w = 2.0 * 3.14159265358979323846 * (double)freq / fs;
        const bool black = values[kEqType] > 0.5f;
        // Series filter magnitudes (genuine HP/LP stages, mirrors recomputeCoeffs).
        double filtMag = 1.0;
        if (values[kHpfEnabled] > 0.5f)
        {
            Biquad h1; h1.setCoeffs(Biquad::firstOrderHighPass(fs, values[kHpfFreq]));
            Biquad h2; h2.setCoeffs(Biquad::highPass(fs, values[kHpfFreq], 1.0f));
            filtMag *= h1.magnitude(w) * h2.magnitude(w);
        }
        if (values[kLpfEnabled] > 0.5f)
        {
            const float f = (float)std::max(1.0, std::min((double)values[kLpfFreq], fs * 0.4998));
            Biquad lp; lp.setCoeffs(Biquad::lowPass(fs, f, black ? 0.8f : 0.707f));
            filtMag *= lp.magnitude(w);
        }
        // Parallel-summing EQ: Heq = 1 + sum(K_i * F_i(w)), complex — mirrors the
        // audio path (dry + summed fixed-Q band blocks) so the curve == the sound.
        auto blockResp = [&](const BiquadCoeffs& c) {
            Biquad b; b.setCoeffs(c); return b.response(w);
        };
        const float bellQ = black ? 0.9f : 0.6f;
        std::complex<double> Heq(1.0, 0.0);
        // LF
        Heq += (double)FourKEQDSP::bandK(values[kLfGain]) * blockResp(values[kLfBell] > 0.5f
            ? Biquad::bandPassConstantPeak(fs, values[kLfFreq], bellQ)
            : (black ? Biquad::lowPass(fs, values[kLfFreq], 0.9f)
                     : Biquad::firstOrderLowPass(fs, values[kLfFreq])));
        // LM
        Heq += (double)FourKEQDSP::bandK(values[kLmGain]) * blockResp(
            Biquad::bandPassConstantPeak(fs, values[kLmFreq],
                FourKEQDSP::voicedMidQ(values[kLmGain], values[kLmQ], black)));
        // HM (Brown caps 7 kHz)
        { float hmFreq = values[kHmFreq]; if (!black && hmFreq > 7000.f) hmFreq = 7000.f;
          Heq += (double)FourKEQDSP::bandK(values[kHmGain]) * blockResp(
              Biquad::bandPassConstantPeak(fs, hmFreq,
                  FourKEQDSP::voicedMidQ(values[kHmGain], values[kHmQ], black))); }
        // HF
        Heq += (double)FourKEQDSP::bandK(values[kHfGain]) * blockResp(values[kHfBell] > 0.5f
            ? Biquad::bandPassConstantPeak(fs, values[kHfFreq], bellQ)
            : (black ? Biquad::highPass(fs, values[kHfFreq], 0.9f)
                     : Biquad::firstOrderHighPass(fs, values[kHfFreq])));

        const double magLin = std::abs(Heq) * filtMag;
        return 20.0f * std::log10((float)std::max(magLin, 1e-6));
    }

    // Selectable graph vertical scale (matches the JUCE ±12/±24/±30/±60/Warped).
    static constexpr const char* kRangeLabels[5] = { "+/-6 dB", "+/-12 dB", "+/-18 dB", "+/-30 dB", "Warped" };
    float graphRangeDb() const { static const float R[4] = { 6.f, 12.f, 18.f, 30.f }; return graphRangeIdx < 4 ? R[graphRangeIdx] : 18.f; }
    // dB -> normalized y in [0,1] (0 = top). Warped mode is a sqrt law that gives
    // more resolution near 0 dB.
    float dbToNy(float db) const
    {
        if (graphRangeIdx == 4)
        {
            const float m = 24.0f;
            float d = db < -m ? -m : (db > m ? m : db);
            const float tt = (d >= 0.f ? 1.f : -1.f) * std::sqrt(std::abs(d) / m);
            return 0.5f - 0.5f * tt;
        }
        const float r = graphRangeDb();
        float d = db < -r ? -r : (db > r ? r : db);
        return 0.5f - 0.5f * (d / r);
    }

    // Expanded preset list — drawn on top, crisp panel.text, all presets (no
    // scroll). Click a row to select; click outside or Esc to close.
    void drawPresetPopup(ImDrawList* dl, float winW, float winH)
    {
        const int n = kNumFactoryPresets;
        const float top = kPresetY1 + 2.f;
        const float bot = top + n * kPresetRowH + 6.f;
        const ImVec2 p0 = panel.P(kPresetX0, top), p1 = panel.P(kPresetX1, bot);
        dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 26, 255), 3.f * sc());
        dl->AddRect(p0, p1, IM_COL32(96, 100, 108, 235), 3.f * sc(), 0, 1.f * sc());

        // Manual hit-test (not ImGui items): the rows overlap the knob buttons
        // underneath, and ImGui's hover goes to the earlier-submitted item (the
        // knobs), so InvisibleButton rows over a knob were unclickable.
        const ImVec2 m = ImGui::GetMousePos();
        for (int i = 0; i < n; ++i)
        {
            const float ry0 = top + 3.f + i * kPresetRowH;
            const ImVec2 r0 = panel.P(kPresetX0 + 2.f, ry0), r1 = panel.P(kPresetX1 - 2.f, ry0 + kPresetRowH);
            const bool hov = m.x >= r0.x && m.x <= r1.x && m.y >= r0.y && m.y <= r1.y;
            if (hov)              dl->AddRectFilled(r0, r1, IM_COL32(70, 90, 120, 255), 2.f * sc());
            else if (i == currentPreset) dl->AddRectFilled(r0, r1, IM_COL32(48, 52, 58, 255), 2.f * sc());
            panel.text(dl, kPresetX0 + 11.f, ry0 + 0.5f * kPresetRowH - 6.f, 12.f, IM_COL32(224, 224, 220, 255), kFactoryPresets[i].name, -1);
            if (hov && ImGui::IsMouseClicked(0)) { applyPreset(i); presetOpen = false; }
        }

        // Close on Esc or a click outside the popup + box.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { presetOpen = false; return; }
        if (ImGui::IsMouseClicked(0))
        {
            const ImVec2 m = ImGui::GetMousePos();
            const ImVec2 b0 = panel.P(kPresetX0, kPresetY0), b1 = panel.P(kPresetX1, kPresetY1);
            const bool inPopup = m.x >= p0.x && m.x <= p1.x && m.y >= p0.y && m.y <= p1.y;
            const bool inBox   = m.x >= b0.x && m.x <= b1.x && m.y >= b0.y && m.y <= b1.y;
            if (!inPopup && !inBox) presetOpen = false;
            (void)winW; (void)winH;
        }
    }

    // Patreon supporters overlay (opened by clicking the title). Dim scrim +
    // centered card listing tiers; click anywhere or press Esc to close.
    void drawCredits(ImDrawList* dl, float winW, float winH)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(0, 0, 0, 205));

        const float s = sc();
        // Line heights (screen px). Content height is measured so the card fits.
        const float LN = 21.f * s, LH = 26.f * s, GAP = 12.f * s;
        const float titleH = 40.f * s, subH = 30.f * s, footH = 34.f * s, padY = 30.f * s;
        const auto& tiers = duskdpf::patreonTiers();
        float contentH = titleH + subH + footH;
        for (const auto& t : tiers)
            if (!t.names.empty()) contentH += LH + (float)t.names.size() * LN + GAP;

        const float cardW = 480.f * s;
        float cardH = contentH + 2.f * padY;
        if (cardH > winH * 0.95f) cardH = winH * 0.95f;
        const ImVec2 cc(winW * 0.5f, winH * 0.5f);
        const ImVec2 c0(cc.x - cardW * 0.5f, cc.y - cardH * 0.5f), c1(cc.x + cardW * 0.5f, cc.y + cardH * 0.5f);
        dl->AddRectFilled(c0, c1, IM_COL32(26, 27, 30, 255), 8.f * s);
        dl->AddRect(c0, c1, IM_COL32(90, 92, 98, 255), 8.f * s, 0, 1.5f * s);

        auto ctext = [&](float yPx, float sz, ImU32 col, const char* txt, bool bold) {
            const float fsz = sz * s;
            ImFont* f = panel.pickFont(fsz);
            const ImVec2 ts = f->CalcTextSizeA(fsz, FLT_MAX, 0.f, txt);
            const float px = std::floor(cc.x - ts.x * 0.5f + 0.5f), py = std::floor(yPx + 0.5f);
            dl->AddText(f, fsz, ImVec2(px, py), col, txt);
            (void)bold;
        };

        float y = c0.y + padY;
        ctext(y, 24.f, IM_COL32(238, 236, 228, 255), "SUPPORTERS", true); y += titleH;
        ctext(y, 13.f, IM_COL32(160, 162, 166, 255), "Thank you to our Patreon supporters", false); y += subH;

        for (const auto& tier : tiers)
        {
            if (tier.names.empty()) continue;
            ctext(y, 15.f, IM_COL32(150, 172, 214, 255), tier.title, true); y += LH;
            for (const char* nm : tier.names) { ctext(y, 15.f, IM_COL32(220, 220, 216, 255), nm, false); y += LN; }
            y += GAP;
        }
        ctext(c1.y - 24.f * s, 11.f, IM_COL32(140, 142, 148, 255), "click anywhere to close", false);

        // Dismiss on any click or Esc (manual — the modal input blocker owns the
        // ImGui hover/active id, so an InvisibleButton here would never click).
        // Arm only after the opening click is released so it doesn't self-close.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showCredits = false;
        else if (!creditsArmed) { if (!ImGui::IsMouseDown(0)) creditsArmed = true; }
        else if (ImGui::IsMouseClicked(0)) showCredits = false;
    }

    void drawGraph(ImDrawList* dl)
    {
        dl->AddRectFilled(panel.P(GX0 - 3, GY0 - 3), panel.P(GX1 + 3, GY1 + 3), IM_COL32(60, 60, 63, 255), 3.0f * sc());
        dl->AddRectFilled(panel.P(GX0, GY0), panel.P(GX1, GY1), IM_COL32(14, 16, 18, 255));
        dl->PushClipRect(panel.P(GX0, GY0), panel.P(GX1, GY1), true);

        for (int i = 0; i < (int)(sizeof(kGridF) / sizeof(kGridF[0])); ++i)
        {
            const float lx = flog(kGridF[i]);
            const float x = GX0 + lx * (GX1 - GX0);
            dl->AddLine(panel.P(x, GY0), panel.P(x, GY1), IM_COL32(40, 43, 47, 255), 1.0f * sc());
            if (kGridF[i] == 100 || kGridF[i] == 1000 || kGridF[i] == 10000)
                panel.text(dl, x, GY1 - 13, 8.5f, IM_COL32(120, 124, 130, 255), kGridFL[i], 0);
        }
        {
            const float R = graphRangeDb();
            const float ticks[5] = { -R, -0.5f * R, 0.f, 0.5f * R, R };
            for (int i = 0; i < 5; ++i)
            {
                const float db = ticks[i];
                const float y = GY0 + dbToNy(db) * (GY1 - GY0);
                dl->AddLine(panel.P(GX0, y), panel.P(GX1, y),
                            db == 0.f ? IM_COL32(64, 68, 74, 255) : IM_COL32(34, 36, 40, 255), 1.0f * sc());
                char b[8]; std::snprintf(b, sizeof(b), "%+d", (int)db);
                // Clamp the label fully inside the graph so the top (+R) and
                // bottom (-R) values are never clipped by the graph edge.
                float ly = y - 6.f;
                ly = ly < GY0 + 1.f ? GY0 + 1.f : (ly > GY1 - 13.f ? GY1 - 13.f : ly);
                panel.text(dl, GX0 + 5, ly, 11.f, IM_COL32(150, 154, 160, 255), db == 0.f ? "0" : b, -1);
            }
        }

        if (showFft)
            drawSpectrum(dl);
        const int N = 240;
        std::vector<ImVec2> pts; pts.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            const float lx = (float)i / (N - 1);
            const float freq = std::pow(10.0f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
            float ny = dbToNy(responseDb(freq));
            ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
            pts.push_back(panel.P(GX0 + lx * (GX1 - GX0), GY0 + ny * (GY1 - GY0)));
        }
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(236, 236, 236, 255), 0, 2.0f * sc());

        dl->PopClipRect();

        // Vertical-scale selector, top-right of the graph.
        ImGui::SetCursorScreenPos(panel.P(GX1 - 78, GY0 + 4));
        ImGui::SetNextItemWidth(74.0f * sc());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 32, 36, 210));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(206, 208, 212, 255));
        if (ImGui::BeginCombo("##graphrange", kRangeLabels[graphRangeIdx], ImGuiComboFlags_NoArrowButton))
        {
            for (int i = 0; i < 5; ++i)
                if (ImGui::Selectable(kRangeLabels[i], i == graphRangeIdx))
                    graphRangeIdx = i;
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(4);
    }

    void drawSpectrum(ImDrawList* dl)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        const duskaudio::SpectrumRing* ring = nullptr;
        const bool pre = values[kSpectrumPrePost] > 0.5f;
        if ((pre ? fourKEQGetPreSpectrum : fourKEQGetPostSpectrum) != nullptr)
            if (void* inst = getPluginInstancePointer())
                ring = pre ? fourKEQGetPreSpectrum(inst) : fourKEQGetPostSpectrum(inst);
        if (ring == nullptr) return;

        float buf[kFftSize]; ring->snapshot(buf, kFftSize);
        float mag[kFftSize / 2 + 1]; fft.magnitude(buf, mag);
        const float dt = ImGui::GetIO().DeltaTime;
        const float smooth = 1.0f - std::exp(-dt * 12.0f);
        // Rings are filled at the DSP base rate, so map bins with the host rate
        // (not a fixed 48 kHz) or peaks misalign with the response curve at other rates.
        const double sr = getSampleRate();
        const float binHz = (float)((sr > 1.0 ? sr : 48000.0) / kFftSize);
        const int half = kFftSize / 2;
        // Build the curve points only (one per in-range bin). The old code fed a
        // jagged, non-convex polygon (anchored at the bottom-left corner) to
        // AddConvexPolyFilled, which fans triangles from that corner — the stray
        // diagonal "blue lines" and translucent haze. Instead fill per-segment
        // quads down to the baseline (each convex) and stroke a clean top line.
        std::vector<ImVec2> curve; curve.reserve((size_t)half);
        for (int k = 1; k <= half; ++k)
        {
            const float freq = (float)k * binHz;
            float db = 20.0f * std::log10(mag[k] > 1e-7f ? mag[k] : 1e-7f);
            specDb[(size_t)k] += (db - specDb[(size_t)k]) * smooth;
            if (freq < kFMin || freq > kFMax) continue;
            float ny = 1.0f - (specDb[(size_t)k] + 72.0f) / 72.0f;
            ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
            curve.push_back(panel.P(GX0 + flog(freq) * (GX1 - GX0), GY0 + ny * (GY1 - GY0)));
        }
        if (curve.size() >= 2)
        {
            const float baseY = panel.P(GX0, GY1).y;
            for (size_t i = 0; i + 1 < curve.size(); ++i)
                dl->AddQuadFilled(curve[i], curve[i + 1],
                                  ImVec2(curve[i + 1].x, baseY), ImVec2(curve[i].x, baseY),
                                  IM_COL32(70, 110, 140, 46));
            dl->AddPolyline(curve.data(), (int)curve.size(), IM_COL32(96, 150, 190, 130), 0, 1.2f * sc());
        }
       #else
        (void)dl;
       #endif
    }

    static float flog(float f) { return (std::log10(f) - std::log10(kFMin)) / (std::log10(kFMax) - std::log10(kFMin)); }

    //========================================================================
    // channel-strip columns
    //========================================================================
    void drawColumns(ImDrawList* dl)
    {
        // panel background + dividers + headers (control-area Y goes through cY())
        dl->AddRectFilled(panel.P(COL[0], cY(220)), panel.P(COL[6], cY(662)), kPanel, 4.0f * sc());
        const char* names[6] = { "FILTERS", "LF", "LMF", "HMF", "HF", "MASTER" };
        for (int i = 0; i < 6; ++i)
        {
            const float cx = 0.5f * (COL[i] + COL[i + 1]);
            if (i > 0) dl->AddLine(panel.P(COL[i], cY(224)), panel.P(COL[i], cY(658)), IM_COL32(20, 20, 22, 255), 1.4f * sc());
            panel.text(dl, cx, cY(232), 12, IM_COL32(210, 210, 214, 255), names[i], 0, true);
        }

        // FILTERS — SSL-style stepped HPF & LPF (OUT folds in each enable), then
        // the input trim. HPF ascends 20-350 Hz; LPF descends 12-3 kHz.
        static const char* const HPFL[7] = { "OUT", "20", "70", "120", "200", "300", "350" };
        static const float        HPFF[7] = { 20.f, 20.f, 70.f, 120.f, 200.f, 300.f, 350.f };
        static const char* const LPFL[7] = { "OUT", "12", "8", "5", "4", "3.5", "3" };
        static const float        LPFF[7] = { 12000.f, 12000.f, 8000.f, 5000.f, 4000.f, 3500.f, 3000.f };
        const float fcx = 0.5f * (COL[0] + COL[1]);
        steppedFilterKnob(dl, "hpfknob", fcx, cY(314), 28.f, kHpfEnabled, kHpfFreq, HPFL, HPFF, false, "Hz");
        steppedFilterKnob(dl, "lpfknob", fcx, cY(452), 28.f, kLpfEnabled, kLpfFreq, LPFL, LPFF, true,  "kHz");
        colMetalKnob(dl, "input", kInputGain, -12.f, 12.f, fcx, cY(568), 26, "INPUT", MK_GAIN_V, MK_GAIN_L, 5, "%.1f", " dB");

        // Shared GAIN (0 top, +-15 dB) and Q (.5-3 descending) detent tables.
        static const float GT[11] = { 0.f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1.f };
        static const float GV[11] = { -15.f, -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f, 15.f };
        static const char* const GL[11] = { "-15", "12", "9", "6", "3", "0", "3", "6", "9", "12", "+15" };
        static const float QT[5] = { 0.f, .25f, .5f, .75f, 1.f };
        static const float QV[5] = { 3.f, 2.f, 1.5f, 1.f, .5f };
        static const char* const QL[5] = { "3", "2", "1.5", "1", ".5" };
        static const float FT7[7] = { 0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f }; // 7 evenly-spaced detents

        // LF — SSL brown console band: GAIN + FREQ (30-450 Hz) + BELL/SHELF button.
        {
            const float lcx = 0.5f * (COL[1] + COL[2]);
            static const float FT[7] = { 0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f };
            static const float FV[7] = { 30.f, 50.f, 100.f, 200.f, 300.f, 400.f, 450.f };
            static const char* const FL[7] = { "30", "50", "100", "200", "300", "400", "450" };
            consoleDetentKnob(dl, "lfg", lcx, cY(314), 28.f, kLfGain, GT, GV, GL, 11, C_LF_BROWN, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lff", lcx, cY(452), 28.f, kLfFreq, FT, FV, FL, 7, C_LF_BROWN, "Hz", "%.0f Hz");
            metalButton(dl, "lfbell", lcx - 32.f, cY(560), lcx + 32.f, cY(584), kLfBell, "BELL", "SHELF");
        }
        // LMF — SSL blue console band: GAIN + FREQ (.2-2.5 kHz, 1 at top) + Q
        // (.5-3, descending), with narrow/wide bandwidth symbols under Q.
        {
            const float mcx = 0.5f * (COL[2] + COL[3]);
            static const float MFV[7] = { 200.f, 300.f, 800.f, 1000.f, 1500.f, 2000.f, 2500.f };
            static const char* const MFL[7] = { ".2", ".3", ".8", "1", "1.5", "2", "2.5" };
            consoleDetentKnob(dl, "lmg", mcx, cY(314), 28.f, kLmGain, GT, GV, GL, 11, C_LMF_BLUE, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lmf", mcx, cY(452), 28.f, kLmFreq, FT7, MFV, MFL, 7, C_LMF_BLUE, "kHz", "%.0f Hz");
            consoleDetentKnob(dl, "lmq", mcx, cY(590), 28.f, kLmQ, QT, QV, QL, 5, C_LMF_BLUE, "", "Q %.2f");
            bandwidthIcons(dl, mcx, cY(590) + 40.f);
        }
        // HMF — SSL green console band: GAIN + FREQ (.6-7 kHz, 3 at top) + Q.
        {
            const float hcx = 0.5f * (COL[3] + COL[4]);
            static const float HFV[7] = { 600.f, 800.f, 1500.f, 3000.f, 4500.f, 6000.f, 7000.f };
            static const char* const HFL[7] = { ".6", ".8", "1.5", "3", "4.5", "6", "7" };
            consoleDetentKnob(dl, "hmg", hcx, cY(314), 28.f, kHmGain, GT, GV, GL, 11, C_HMF_GREEN, "dB", "%.1f dB");
            consoleDetentKnob(dl, "hmf", hcx, cY(452), 28.f, kHmFreq, FT7, HFV, HFL, 7, C_HMF_GREEN, "kHz", "%.0f Hz");
            consoleDetentKnob(dl, "hmq", hcx, cY(590), 28.f, kHmQ, QT, QV, QL, 5, C_HMF_GREEN, "", "Q %.2f");
            bandwidthIcons(dl, hcx, cY(590) + 40.f);
        }
        // HF — SSL red console band: GAIN + FREQ (1.5-16 kHz, 8 at top) + BELL/SHELF.
        {
            const float hcx = 0.5f * (COL[4] + COL[5]);
            static const float XFV[7] = { 1500.f, 2000.f, 5000.f, 8000.f, 10000.f, 14000.f, 16000.f };
            static const char* const XFL[7] = { "1.5", "2", "5", "8", "10", "14", "16" };
            consoleDetentKnob(dl, "hfg", hcx, cY(314), 28.f, kHfGain, GT, GV, GL, 11, C_HF_RED, "dB", "%.1f dB");
            consoleDetentKnob(dl, "hff", hcx, cY(452), 28.f, kHfFreq, FT7, XFV, XFL, 7, C_HF_RED, "kHz", "%.0f Hz");
            metalButton(dl, "hfbell", hcx - 32.f, cY(560), hcx + 32.f, cY(584), kHfBell, "BELL", "SHELF");
        }

        // MASTER
        const float mcx = 0.5f * (COL[5] + COL[6]);
        panelButton(dl, "bypass", mcx - 40, cY(268), mcx + 40, cY(292),
                    values[kBypass] > 0.5f ? "BYPASSED" : "BYPASS",
                    values[kBypass] > 0.5f ? IM_COL32(150, 60, 48, 255) : IM_COL32(50, 50, 54, 255),
                    [&]{ toggleParam(kBypass); });
        panelButton(dl, "autogain", mcx - 40, cY(300), mcx + 40, cY(324), "AUTO GAIN",
                    values[kAutoGain] > 0.5f ? kGreenBtn : IM_COL32(50, 50, 54, 255),
                    [&]{ toggleParam(kAutoGain); });
        colMetalKnob(dl, "drive", kSaturation, 0.f, 100.f, mcx, cY(430), 26, "DRIVE", MK_DRIVE_V, MK_DRIVE_L, 5, "%.0f", "");
        colMetalKnob(dl, "outg", kOutputGain, -12.f, 12.f, mcx, cY(556), 26, "OUTPUT", MK_GAIN_V, MK_GAIN_L, 5, "%.1f", " dB");
        smallToggle(dl, "ms", kMsMode, mcx - 24, cY(606), mcx + 24, cY(628), values[kMsMode], "M/S");
    }

    // A parametric band column: GAIN (top) + FREQ (mid) + Q knob or BELL toggle.
    void band(ImDrawList* dl, int col, const char* name, ImU32 color,
              uint32_t gainId, uint32_t freqId, uint32_t thirdId, int thirdKind,
              float fMin, float fMax)
    {
        const float cx = 0.5f * (COL[col] + COL[col + 1]);
        colKnob(dl, (std::string(name) + "g").c_str(), gainId, -20.f, 20.f, cx, cY(306), 26, color, "GAIN", "-20", "+20", "%.1f", " dB");
        char fmn[8], fmx[8]; freqLabel(fMin, fmn); freqLabel(fMax, fmx);
        colKnob(dl, (std::string(name) + "f").c_str(), freqId, fMin, fMax, cx, cY(430), 26, color, "FREQ", fmn, fmx, "%.0f", " Hz");
        if (thirdKind > 0)
            colKnob(dl, (std::string(name) + "q").c_str(), thirdId, 0.4f, 4.0f, cx, cY(556), 24, color, "Q", "0.4", "4", "%.2f", "");
        else
            smallToggle(dl, (std::string(name) + "b").c_str(), thirdId, cx - 30, cY(546), cx + 30, cY(568),
                        values[thirdId], values[thirdId] > 0.5f ? "BELL" : "SHELF");
    }

    static void freqLabel(float hz, char* out)
    {
        if (hz >= 1000.f) std::snprintf(out, 8, "%gk", hz / 1000.f);
        else              std::snprintf(out, 8, "%g", hz);
    }

    // knob + name label below + min/max tick labels at the dial ends.
    void colKnob(ImDrawList* dl, const char* id, uint32_t param, float minV, float maxV,
                 float cx, float cy, float r, ImU32 face, const char* name,
                 const char* lmin, const char* lmax, const char* fmt, const char* suffix)
    {
        panel.knob(id, param, minV, maxV, cx, cy, r, values[param], kDefaults[param],
                   false, false, fmt, suffix, face);
        // dial-end tick labels
        const float a0 = duskdpf::DuskPanel::knobAngle(0.0f), a1 = duskdpf::DuskPanel::knobAngle(1.0f);
        panel.text(dl, cx + std::sin(a0) * (r + 14), cy - std::cos(a0) * (r + 14) - 5, 9.5f, IM_COL32(170, 172, 176, 255), lmin, 1);
        panel.text(dl, cx + std::sin(a1) * (r + 14), cy - std::cos(a1) * (r + 14) - 5, 9.5f, IM_COL32(170, 172, 176, 255), lmax, -1);
        panel.text(dl, cx, cy + r + 8, 11.0f, IM_COL32(206, 208, 212, 255), name, 0, true);
    }

    // Brushed-metal continuous knob (INPUT/DRIVE/OUTPUT) matching the FILTERS and
    // band knobs' look: silver metal body drawn here, gestures + value bubble +
    // inline editor owned by panel.knob in bodyless mode.
    void colMetalKnob(ImDrawList* dl, const char* id, uint32_t param, float minV, float maxV,
                      float cx, float cy, float r, const char* name,
                      const float* TV, const char* const* TL, int nT,
                      const char* fmt, const char* suffix)
    {
        const float range = maxV - minV;
        float t = range > 0.f ? (values[param] - minV) / range : 0.f;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        drawMetalKnobBody(dl, panel.P(cx, cy), r * sc(), t,
                          IM_COL32(150, 152, 156, 255), IM_COL32(30, 30, 33, 255));
        // tick dots + labels around the dial, matching the band/filter knobs.
        for (int i = 0; i < nT; ++i)
        {
            const float tt = range > 0.f ? (TV[i] - minV) / range : 0.f;
            const float a = duskdpf::DuskPanel::knobAngle(tt);
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (r + 9.f), cy + dy * (r + 9.f)), 1.6f * sc(), IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (r + 20.f), cy + dy * (r + 20.f) - 5.f, 10.5f, IM_COL32(206, 208, 212, 255), TL[i], align, true);
        }
        panel.text(dl, cx, cy + r + 10.f, 11.0f, IM_COL32(206, 208, 212, 255), name, 0, true);
        // gestures + value read-out on top (no body drawn)
        panel.knob(id, param, minV, maxV, cx, cy, r, values[param], kDefaults[param],
                   false, false, fmt, suffix, 0, /*bodyless*/true);
    }

    void smallToggle(ImDrawList* dl, const char* id, uint32_t param, float x0, float y0, float x1, float y1,
                     float& value, const char* label)
    {
        const bool on = value > 0.5f;
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) toggleParam(param);
        dl->AddRectFilled(b0, b1, on ? IM_COL32(64, 96, 130, 255) : IM_COL32(44, 44, 48, 255), 3.0f * sc());
        dl->AddRect(b0, b1, IM_COL32(90, 90, 96, 220), 3.0f * sc(), 0, 1.0f * sc());
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.28f * (y1 - y0), 9.0f, on ? pal().white : IM_COL32(160, 162, 166, 255), label, 0, true);
    }

    template <class Fn>
    void panelButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     const char* label, ImU32 bg, Fn onClick)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        dl->AddRectFilled(b0, b1, bg, 4.0f * sc());
        dl->AddRect(b0, b1, hov ? IM_COL32(200, 200, 205, 220) : IM_COL32(90, 90, 96, 200), 4.0f * sc(), 0, 1.2f * sc());
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 10.0f, pal().white, label, 0, true);
    }

    //========================================================================
    // SSL-style stepped filter rotary (HPF & LPF): OUT + 6 labelled Hz detents,
    // dots + labels around a brushed-metal knob, vertical pointer, rolloff icon.
    // The OUT position folds in the filter enable (no separate IN switch). F[]
    // is t-indexed (F[0] == F[1]); frequencies may ascend (HPF) or descend (LPF).
    //========================================================================
    static float stepLT(int i) { static const float t[7] = {0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f}; return t[i]; }

    void stepPosToState(const float* F, float t, bool& en, float& f) const
    {
        if (t < 0.06f) { en = false; f = 0.f; return; }
        en = true;
        if (t <= stepLT(1)) { f = F[1]; return; }
        for (int i = 1; i <= 5; ++i)
            if (t <= stepLT(i + 1)) { const float a = (t - stepLT(i)) / (stepLT(i + 1) - stepLT(i)); f = F[i] + (F[i + 1] - F[i]) * a; return; }
        f = F[6];
    }
    float stepStateToPos(const float* F, bool en, float freq) const
    {
        if (!en) return 0.f;
        float lo = F[1], hi = F[1];
        for (int i = 2; i <= 6; ++i) { lo = std::min(lo, F[i]); hi = std::max(hi, F[i]); }
        const float fr = freq < lo ? lo : (freq > hi ? hi : freq);
        for (int i = 1; i <= 5; ++i) // segment containing fr (works ascending or descending)
            if ((fr - F[i]) * (fr - F[i + 1]) <= 0.f)
            {
                const float d = F[i + 1] - F[i];
                const float a = d != 0.f ? (fr - F[i]) / d : 0.f;
                return stepLT(i) + (stepLT(i + 1) - stepLT(i)) * a;
            }
        return 1.f;
    }
    void stepApply(uint32_t enId, uint32_t freqId, const float* F, float t)
    {
        bool en; float f; stepPosToState(F, t, en, f);
        values[enId] = en ? 1.f : 0.f; setParameterValue(enId, values[enId]);
        if (en) { values[freqId] = f; setParameterValue(freqId, f); }
    }

    void steppedFilterKnob(ImDrawList* dl, const char* id, float cx, float cy, float R,
                           uint32_t enId, uint32_t freqId, const char* const* labels,
                           const float* F, bool lowpass, const char* unit)
    {
        const float s = sc();
        const ImVec2 c = panel.P(cx, cy);
        const float RR = R * s;
        auto c01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

        const bool en = values[enId] > 0.5f;
        float t = stepStateToPos(F, en, values[freqId]);

        // interaction
        ImGui::SetCursorScreenPos(ImVec2(c.x - RR, c.y - RR));
        ImGui::InvisibleButton(id, ImVec2(2.f * RR, 2.f * RR));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) // Ctrl/Cmd+click: reset to OUT (default)
                { editParameter(enId, true); stepApply(enId, freqId, F, 0.f); editParameter(enId, false); t = 0.f; stepModReset_ = true; }
                else { editParameter(enId, true); editParameter(freqId, true); stepDragT = t; stepModReset_ = false; }
            }
            if (act && !stepModReset_)
            {
                const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                stepDragT = c01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
                t = stepDragT; stepApply(enId, freqId, F, t);
            }
            if (ImGui::IsItemDeactivated()) { if (!stepModReset_) { editParameter(enId, false); editParameter(freqId, false); } stepModReset_ = false; }
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0)) // double-click: type a frequency
            { panel.openValueEdit(id, values[freqId]); editParameter(enId, false); editParameter(freqId, false); }
            else if (hov && !act)
            {
                const float wh = ImGui::GetIO().MouseWheel;
                if (wh != 0.f)
                {
                    t = c01(t + wh * 0.02f);
                    editParameter(enId, true); editParameter(freqId, true);
                    stepApply(enId, freqId, F, t);
                    editParameter(freqId, false); editParameter(enId, false);
                }
            }
        }

        // detent dots + labels
        for (int i = 0; i < 7; ++i)
        {
            const float a = duskdpf::DuskPanel::knobAngle(stepLT(i));
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (R + 9.f), cy + dy * (R + 9.f)), 1.7f * s, IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (R + 20.f), cy + dy * (R + 20.f) - 5.f, 11.f, IM_COL32(206, 208, 212, 255), labels[i], align, true);
        }

        // brushed-metal body: silver cap + dark pointer for the filter knobs.
        drawMetalKnobBody(dl, c, RR, t, IM_COL32(150, 152, 156, 255), IM_COL32(30, 30, 33, 255));

        // rolloff icon + unit below (HPF: icon left / unit right; LPF: unit left / icon right)
        const float iy = cy + R + 22.f;
        if (lowpass)
        {
            ImVec2 ic[4] = { panel.P(cx + 4.f, iy - 3.f), panel.P(cx + 11.f, iy - 3.f), panel.P(cx + 18.f, iy), panel.P(cx + 23.f, iy + 6.f) };
            dl->AddPolyline(ic, 4, IM_COL32(196, 198, 202, 255), 0, 1.4f * s);
            panel.text(dl, cx - 4.f, iy - 9.f, 12.f, IM_COL32(210, 212, 216, 255), unit, 1, true);
        }
        else
        {
            ImVec2 ic[4] = { panel.P(cx - 22.f, iy + 6.f), panel.P(cx - 17.f, iy), panel.P(cx - 10.f, iy - 3.f), panel.P(cx + 4.f, iy - 3.f) };
            dl->AddPolyline(ic, 4, IM_COL32(196, 198, 202, 255), 0, 1.4f * s);
            panel.text(dl, cx + 8.f, iy - 9.f, 12.f, IM_COL32(210, 212, 216, 255), unit, -1, true);
        }

        float typed;
        if (panel.valueEdit(id, cx, cy, R, typed))
        {
            // Typed frequency: enable the filter and clamp to its range.
            float lo = F[1], hi = F[1];
            for (int i = 2; i <= 6; ++i) { lo = std::min(lo, F[i]); hi = std::max(hi, F[i]); }
            typed = typed < lo ? lo : (typed > hi ? hi : typed);
            editParameter(enId, true); editParameter(freqId, true);
            values[enId] = 1.f;    setParameterValue(enId, 1.f);
            values[freqId] = typed; setParameterValue(freqId, typed);
            editParameter(freqId, false); editParameter(enId, false);
        }
        else if ((hov || act) && !editing)
        {
            char buf[24];
            if (!en) std::snprintf(buf, sizeof(buf), "OUT");
            else if (values[freqId] >= 1000.f) std::snprintf(buf, sizeof(buf), "%.1f kHz", values[freqId] / 1000.f);
            else std::snprintf(buf, sizeof(buf), "%.0f Hz", values[freqId]);
            panel.valueBubble(dl, cx, cy, R, buf);
        }
    }

    // Shared brushed-metal knob body (skirt + knurl + cap + sheen + pointer).
    // capCol tints the cap (silver for filters, maroon for the console bands);
    // pointerCol is the indicator line (dark on silver, white on maroon).
    void drawMetalKnobBody(ImDrawList* dl, ImVec2 c, float RR, float t, ImU32 capCol, ImU32 pointerCol)
    {
        const float s = sc();
        dl->AddCircleFilled(c, RR, IM_COL32(18, 18, 20, 255), 48);          // rim
        dl->AddCircleFilled(c, RR * 0.95f, IM_COL32(88, 90, 94, 255), 48);  // skirt
        for (int i = 0; i < 20; ++i)                                        // knurl
        {
            const float a = (float)i / 20.f * 2.f * duskdpf::DuskPanel::kPi;
            const ImVec2 d(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(c.x + d.x * RR * 0.80f, c.y + d.y * RR * 0.80f),
                        ImVec2(c.x + d.x * RR * 0.93f, c.y + d.y * RR * 0.93f), IM_COL32(40, 40, 43, 160), 1.3f * s);
        }
        const float capR = RR * 0.74f;
        dl->AddCircleFilled(c, capR, capCol, 44);
        dl->PushClipRect(ImVec2(c.x - capR, c.y - capR), ImVec2(c.x + capR, c.y + capR), true);
        // Matte SSL cap: a gentle top-to-bottom shade (top a touch lighter,
        // bottom a touch darker) for roundness; the cap colour stays essentially
        // unchanged. Low-alpha discs centred just off the cap fade before centre.
        dl->AddCircleFilled(ImVec2(c.x, c.y + capR * 0.64f), capR * 0.82f, IM_COL32(0, 0, 0, 20), 40);       // subtle bottom shade
        dl->AddCircleFilled(ImVec2(c.x, c.y - capR * 0.60f), capR * 0.85f, IM_COL32(255, 255, 255, 13), 40); // top lift
        dl->AddCircleFilled(ImVec2(c.x, c.y - capR * 0.72f), capR * 0.55f, IM_COL32(255, 255, 255, 17), 40);
        dl->PopClipRect();
        dl->AddCircle(c, capR, IM_COL32(20, 20, 22, 255), 44, 1.4f * s);
        const float a = duskdpf::DuskPanel::knobAngle(t);
        const ImVec2 pd(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + pd.x * capR * 0.12f, c.y + pd.y * capR * 0.12f),
                    ImVec2(c.x + pd.x * capR * 0.92f, c.y + pd.y * capR * 0.92f), pointerCol, 3.4f * s);
    }

    //========================================================================
    // SSL-style brown console band knob: continuous knob over arbitrary
    // (t, value) breakpoints with dots + labels all around (e.g. LF GAIN with
    // 0 at top, +-15 dB; LF FREQ 30-450 Hz with 200 at top). Maroon cap, white
    // pointer, unit beneath.
    //========================================================================
    static float detentPosToVal(const float* T, const float* V, int n, float t)
    {
        if (t <= T[0]) return V[0];
        if (t >= T[n - 1]) return V[n - 1];
        for (int i = 0; i < n - 1; ++i)
            if (t <= T[i + 1]) { const float a = (t - T[i]) / (T[i + 1] - T[i]); return V[i] + (V[i + 1] - V[i]) * a; }
        return V[n - 1];
    }
    static float detentValToPos(const float* T, const float* V, int n, float v)
    {
        // clamp to the value range (works whether V ascends or descends)
        float lo = V[0], hi = V[0];
        for (int i = 1; i < n; ++i) { lo = std::min(lo, V[i]); hi = std::max(hi, V[i]); }
        v = v < lo ? lo : (v > hi ? hi : v);
        for (int i = 0; i < n - 1; ++i)
            if ((v - V[i]) * (v - V[i + 1]) <= 0.f) // v lies within this segment
            {
                const float d = V[i + 1] - V[i];
                const float a = d != 0.f ? (v - V[i]) / d : 0.f;
                return T[i] + (T[i + 1] - T[i]) * a;
            }
        return T[n - 1];
    }

    void consoleDetentKnob(ImDrawList* dl, const char* id, float cx, float cy, float R,
                           uint32_t paramId, const float* T, const float* V,
                           const char* const* labels, int n, ImU32 capCol,
                           const char* unit, const char* fmt)
    {
        const float s = sc();
        const ImVec2 c = panel.P(cx, cy);
        const float RR = R * s;
        auto c01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

        float t = detentValToPos(T, V, n, values[paramId]);

        ImGui::SetCursorScreenPos(ImVec2(c.x - RR, c.y - RR));
        ImGui::InvisibleButton(id, ImVec2(2.f * RR, 2.f * RR));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        auto setFromT = [&](float tt) { const float nv = detentPosToVal(T, V, n, tt); values[paramId] = nv; setParameterValue(paramId, nv); };
        auto resetDefault = [&] { editParameter(paramId, true); values[paramId] = kDefaults[paramId]; setParameterValue(paramId, kDefaults[paramId]); editParameter(paramId, false); t = detentValToPos(T, V, n, values[paramId]); };
        if (!editing)
        {
            if (ImGui::IsItemActivated())
            {
                if (modKey) { resetDefault(); stepModReset_ = true; }         // Ctrl/Cmd+click: reset
                else { editParameter(paramId, true); stepDragT = t; stepModReset_ = false; }
            }
            if (act && !stepModReset_)
            {
                const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
                stepDragT = c01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
                t = stepDragT; setFromT(t);
            }
            if (ImGui::IsItemDeactivated()) { if (!stepModReset_) editParameter(paramId, false); stepModReset_ = false; }
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0))
            { panel.openValueEdit(id, values[paramId]); editParameter(paramId, false); } // double-click: type a value
            else if (hov && !act)
            {
                const float wh = ImGui::GetIO().MouseWheel;
                if (wh != 0.f) { t = c01(t + wh * 0.02f); editParameter(paramId, true); setFromT(t); editParameter(paramId, false); }
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float a = duskdpf::DuskPanel::knobAngle(T[i]);
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (R + 9.f), cy + dy * (R + 9.f)), 1.6f * s, IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (R + 20.f), cy + dy * (R + 20.f) - 5.f, 10.5f, IM_COL32(206, 208, 212, 255), labels[i], align, true);
        }

        drawMetalKnobBody(dl, c, RR, t, capCol, IM_COL32(245, 245, 245, 255));
        panel.text(dl, cx, cy + R + 16.f, 12.f, IM_COL32(210, 212, 216, 255), unit, 0, true);

        float typed;
        if (panel.valueEdit(id, cx, cy, R, typed))
        {
            float lo = V[0], hi = V[0];
            for (int i = 1; i < n; ++i) { lo = std::min(lo, V[i]); hi = std::max(hi, V[i]); }
            typed = typed < lo ? lo : (typed > hi ? hi : typed);
            editParameter(paramId, true); values[paramId] = typed; setParameterValue(paramId, typed); editParameter(paramId, false);
        }
        else if ((hov || act) && !editing)
        {
            char buf[24]; std::snprintf(buf, sizeof(buf), fmt, values[paramId]);
            panel.valueBubble(dl, cx, cy, R, buf);
        }
    }

    // Narrow + wide "peak" bandwidth symbols (drawn under a Q knob).
    void bandwidthIcons(ImDrawList* dl, float cx, float iy)
    {
        const float s = sc();
        const ImU32 col = IM_COL32(150, 152, 156, 255);
        ImVec2 nar[5] = { panel.P(cx - 24.f, iy + 5.f), panel.P(cx - 20.f, iy + 5.f), panel.P(cx - 16.f, iy - 6.f), panel.P(cx - 12.f, iy + 5.f), panel.P(cx - 8.f, iy + 5.f) };
        dl->AddPolyline(nar, 5, col, 0, 1.4f * s);
        ImVec2 wid[6] = { panel.P(cx + 8.f, iy + 5.f), panel.P(cx + 12.f, iy + 5.f), panel.P(cx + 15.f, iy - 4.f), panel.P(cx + 19.f, iy - 4.f), panel.P(cx + 22.f, iy + 5.f), panel.P(cx + 26.f, iy + 5.f) };
        dl->AddPolyline(wid, 6, col, 0, 1.4f * s);
    }

    // Raised silver metal button (BELL / SHELF). Beveled, pressed-in when active.
    void metalButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     uint32_t paramId, const char* onLabel, const char* offLabel)
    {
        const bool on = values[paramId] > 0.5f;
        const float s = sc();
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) toggleParam(paramId);
        dl->AddRectFilled(b0, b1, IM_COL32(18, 18, 20, 255), 5.f * s); // dark border
        const ImVec2 f0(b0.x + 1.6f * s, b0.y + 1.6f * s), f1(b1.x - 1.6f * s, b1.y - 1.6f * s);
        const ImU32 top = on ? IM_COL32(120, 122, 126, 255) : IM_COL32(182, 184, 188, 255);
        const ImU32 bot = on ? IM_COL32(150, 152, 156, 255) : IM_COL32(138, 140, 144, 255);
        dl->AddRectFilledMultiColor(f0, f1, top, top, bot, bot); // vertical gradient (inverted when pressed)
        dl->AddLine(ImVec2(f0.x, f0.y), ImVec2(f1.x, f0.y), IM_COL32(255, 255, 255, on ? 40 : 150), 1.2f * s); // top highlight
        dl->AddLine(ImVec2(f0.x, f1.y), ImVec2(f1.x, f1.y), IM_COL32(0, 0, 0, on ? 120 : 60), 1.2f * s);       // bottom shadow
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 11.f, IM_COL32(34, 34, 38, 255), on ? onLabel : offLabel, 0, true);
    }

    //========================================================================
    // edge meters (INPUT left, OUTPUT right)
    //========================================================================
    void drawMeters(ImDrawList* dl)
    {
        float inL = 0, inR = 0, outL = 0, outR = 0;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fourKEQGetInputPeakL != nullptr)
            if (void* inst = getPluginInstancePointer())
            { inL = fourKEQGetInputPeakL(inst); inR = fourKEQGetInputPeakR(inst);
              outL = fourKEQGetOutputPeakL(inst); outR = fourKEQGetOutputPeakR(inst); }
       #endif
        panel.text(dl, 0.5f * (INX0 + INX1), cY(MET_LBL_Y), 10.f, IM_COL32(160, 162, 166, 255), "IN", 0, true);
        panel.text(dl, 0.5f * (OUTX0 + OUTX1), cY(MET_LBL_Y), 10.f, IM_COL32(160, 162, 166, 255), "OUT", 0, true);
        meterPair(dl, INX0, INX1, inL, inR);   // bars update every frame (smooth)
        meterPair(dl, OUTX0, OUTX1, outL, outR);

        // Numeric dB readout: hold the peak over a ~150 ms window and refresh the
        // digits at that slower rate (like commercial meters) so they read
        // cleanly instead of flickering every frame. One decimal place.
        meterPkIn_  = std::max(meterPkIn_,  std::max(inL, inR));
        meterPkOut_ = std::max(meterPkOut_, std::max(outL, outR));
        meterTimer_ += ImGui::GetIO().DeltaTime;
        if (meterTimer_ >= 0.15f)
        {
            meterTimer_ = 0.f;
            meterDbIn_  = 20.0f * std::log10(meterPkIn_  > 1e-5f ? meterPkIn_  : 1e-5f);
            meterDbOut_ = 20.0f * std::log10(meterPkOut_ > 1e-5f ? meterPkOut_ : 1e-5f);
            meterPkIn_ = meterPkOut_ = 0.f;
        }
        char b[16];
        std::snprintf(b, sizeof(b), "%.1f", meterDbIn_);
        panel.text(dl, 0.5f * (INX0 + INX1), cY(MET_Y1) + 5, 11.f, IM_COL32(160, 162, 166, 255), b, 0);
        std::snprintf(b, sizeof(b), "%.1f", meterDbOut_);
        panel.text(dl, 0.5f * (OUTX0 + OUTX1), cY(MET_Y1) + 5, 11.f, IM_COL32(160, 162, 166, 255), b, 0);
    }

    void meterPair(ImDrawList* dl, float x0, float x1, float l, float r)
    {
        const float y0 = cY(MET_Y0), y1 = cY(MET_Y1);
        dl->AddRectFilled(panel.P(x0 - 2, y0 - 2), panel.P(x1 + 2, y1 + 2), IM_COL32(60, 60, 63, 255), 2.0f * sc());
        dl->AddRectFilled(panel.P(x0, y0), panel.P(x1, y1), IM_COL32(14, 16, 18, 255));
        const float mid = 0.5f * (x0 + x1);
        meterBar(dl, x0 + 1, mid - 0.5f, l);
        meterBar(dl, mid + 0.5f, x1 - 1, r);
    }

    void meterBar(ImDrawList* dl, float x0, float x1, float lin)
    {
        const float y0 = cY(MET_Y0), y1 = cY(MET_Y1);
        float db = 20.0f * std::log10(lin > 1e-5f ? lin : 1e-5f);
        float t = (db + 60.0f) / 60.0f; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        const float yFill = y1 - t * (y1 - y0);
        ImU32 col = db > -1.5f ? IM_COL32(226, 70, 55, 255)
                  : db > -10.f ? IM_COL32(224, 196, 72, 255) : IM_COL32(96, 196, 112, 255);
        dl->AddRectFilled(panel.P(x0, yFill), panel.P(x1, y1), col);
        // segment ticks
        for (int i = 1; i < 12; ++i)
        {
            const float y = y0 + (float)i / 12.0f * (y1 - y0);
            dl->AddLine(panel.P(x0, y), panel.P(x1, y), IM_COL32(14, 16, 18, 200), 1.0f * sc());
        }
    }

    duskdpf::DuskPanel panel;
    duskdpf::RealFFT fft;
    std::vector<float> specDb;
    duskdpf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    // Throttled numeric meter readout (peak-hold over a ~150 ms window).
    float meterPkIn_ = 0.f, meterPkOut_ = 0.f;
    float meterDbIn_ = -60.f, meterDbOut_ = -60.f;
    float meterTimer_ = 0.f;
    float values[kParamCount] = {};
    int currentPreset = -1;
    bool showGraph = true;
    bool showCredits = false;    // Patreon supporters overlay (title click)
    bool creditsArmed = false;   // ignore the opening click until mouse released
    bool presetOpen = false;     // custom preset dropdown expanded
    bool showFft = true;         // spectrum analyzer overlay on the graph
    int  graphRangeIdx = 2;      // 0:+-6 1:+-12 2:+-18 3:+-30 4:Warped
    bool needResize = false;
    float ctlDstTop_ = 220.0f, ctlScaleY_ = 1.0f;
    float stepDragT = 0.0f; // stepped filter-knob drag origin (HPF/LPF)
    bool  stepModReset_ = false; // Ctrl/Cmd+click reset in progress (suppress drag)

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQUI)
};

UI* createUI() { return new FourKEQUI(); }

END_NAMESPACE_DISTRHO
