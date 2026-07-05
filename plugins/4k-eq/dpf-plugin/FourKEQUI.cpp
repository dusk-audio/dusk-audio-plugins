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
    constexpr float GX0 = 42, GY0 = 98, GX1 = 918, GY1 = 214;
    constexpr float INX0 = 8,   INX1 = 34,  MET_Y0 = 224, MET_Y1 = 656;
    constexpr float OUTX0 = 926, OUTX1 = 952;
    // column dividers
    constexpr float COL[7] = { 40, 197, 335, 469, 603, 737, 920 };

    // band face colours
    constexpr ImU32 C_LF_BROWN = IM_COL32(96, 56, 48, 255); // SSL LF maroon console knob
    constexpr ImU32 C_LF  = IM_COL32(196, 74, 66, 255);
    constexpr ImU32 C_LMF = IM_COL32(202, 132, 66, 255);
    constexpr ImU32 C_HMF = IM_COL32(104, 168, 92, 255);
    constexpr ImU32 C_HF  = IM_COL32(84, 146, 204, 255);
    constexpr ImU32 C_GREY = IM_COL32(92, 94, 99, 255);

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
        setGeometryConstraints(576, 320, false);
        labelFont = duskdpf::loadCrispFont(32.0f * getScaleFactor());
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

        // Scale by WIDTH so knobs stay circular; the control block then stretches
        // vertically to fill whatever height the host actually gave us — no dead
        // space when a host declines to shrink its window after a collapse.
        const float s = winW / kDesignW;
        const ImVec2 org(0.0f, 0.0f);
        panel.begin(s, org, labelFont, this);

        // control-area vertical remap: design rows [220..662] -> [dstTop..dstBot]
        ctlDstTop_ = showGraph ? 220.0f : 96.0f;
        const float avail = winH / s;                       // window height in design units
        ctlDstBot_ = std::max(ctlDstTop_ + 300.0f, avail - 10.0f);
        ctlScaleY_ = (ctlDstBot_ - ctlDstTop_) / (662.0f - 220.0f);

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
        drawColumns(dl);
        drawMeters(dl);

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
        setSize((uint)getWidth(), (uint)std::lround((double)getWidth() * designH / kDesignW));
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

        panel.text(dl, 28, 30, 26, pal().white, "4K EQ", -1, true);
        panel.text(dl, 30, 60, 11, IM_COL32(150, 152, 156, 255), "Console-Style Equalizer", -1);
        panel.text(dl, kDesignW - 26, 62, 11, IM_COL32(150, 152, 156, 255), "DUSK AUDIO", 1, true);

        // preset dropdown
        ImGui::SetCursorScreenPos(panel.P(226, 30));
        ImGui::SetNextItemWidth(196.0f * sc());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(46, 46, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(228, 228, 224, 255));
        const char* preview = (currentPreset >= 0 && currentPreset < kNumFactoryPresets)
                                  ? kFactoryPresets[currentPreset].name : "Default";
        if (ImGui::BeginCombo("##presets", preview))
        {
            for (int i = 0; i < kNumFactoryPresets; ++i)
                if (ImGui::Selectable(kFactoryPresets[i].name, i == currentPreset))
                    applyPreset(i);
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(4);

        // Oversample selector (cycles 2x/4x)
        headerButton(dl, "os", 440, 30, 578, 54,
                     values[kOversampling] > 0.5f ? "Oversample: 4x" : "Oversample: 2x",
                     IM_COL32(46, 46, 50, 255), pal().white,
                     [&]{ cycleParam(kOversampling, 2); });

        // Hide/Show Graph toggle — collapses the window like the JUCE editor.
        headerButton(dl, "hidegraph", 590, 30, 690, 54, showGraph ? "Hide Graph" : "Show Graph",
                     IM_COL32(46, 46, 50, 255), pal().white, [&]{ toggleGraph(); });

        // Brown / Black voicing (amber when Brown, blue-grey when Black)
        const bool brown = values[kEqType] < 0.5f;
        headerButton(dl, "eqtype", 702, 30, 800, 54, brown ? "BROWN" : "BLACK",
                     brown ? kAmber : IM_COL32(60, 74, 96, 255), IM_COL32(245, 240, 232, 255),
                     [&]{ cycleParam(kEqType, 2); });

        // Spectrum source (pre/post) — small, right of voicing
        headerButton(dl, "prepost", 806, 30, 892, 54,
                     values[kSpectrumPrePost] > 0.5f ? "SPEC PRE" : "SPEC POST",
                     IM_COL32(40, 40, 44, 255), IM_COL32(180, 182, 186, 255),
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
        const double fs = 96000.0;
        const double w = 2.0 * 3.14159265358979323846 * (double)freq / fs;
        const bool black = values[kEqType] > 0.5f;
        double magLin = 1.0;
        auto acc = [&](const BiquadCoeffs& c) { Biquad b; b.setCoeffs(c); magLin *= b.magnitude(w); };
        // Mirrors FourKEQDSP::recomputeCoeffs exactly (analog-matched, HPF Q=1.0,
        // no piecewise prewarp — the console builders prewarp fc internally).
        if (values[kHpfEnabled] > 0.5f)
        {
            acc(Biquad::firstOrderHighPass(fs, values[kHpfFreq]));
            acc(Biquad::highPass(fs, values[kHpfFreq], 1.0f));
        }
        if (values[kLfBell] > 0.5f)
            acc(duskaudio::FourKEQDSP::consolePeak(fs, values[kLfFreq], 0.7f, values[kLfGain], black));
        else
            acc(duskaudio::FourKEQDSP::consoleShelf(fs, values[kLfFreq], 0.7f, values[kLfGain], false, black));
        { float q = values[kLmQ]; if (black) q = duskaudio::FourKEQDSP::dynamicQ(values[kLmGain], q);
          acc(duskaudio::FourKEQDSP::consolePeak(fs, values[kLmFreq], q, values[kLmGain], black)); }
        { float f = values[kHmFreq], q = values[kHmQ];
          if (black) q = duskaudio::FourKEQDSP::dynamicQ(values[kHmGain], q); else if (f > 7000.f) f = 7000.f;
          acc(duskaudio::FourKEQDSP::consolePeak(fs, f, q, values[kHmGain], black)); }
        { if (values[kHfBell] > 0.5f) acc(duskaudio::FourKEQDSP::consolePeak(fs, values[kHfFreq], 0.7f, values[kHfGain], black));
          else acc(duskaudio::FourKEQDSP::consoleShelf(fs, values[kHfFreq], 0.7f, values[kHfGain], true, black)); }
        if (values[kLpfEnabled] > 0.5f)
        { const float f = (float)std::max(1.0, std::min((double)values[kLpfFreq], fs * 0.4998));
          acc(Biquad::lowPass(fs, f, black ? 0.8f : 0.707f)); }
        return 20.0f * std::log10((float)std::max(magLin, 1e-6));
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
        for (int db = -20; db <= 20; db += 10)
        {
            const float y = GY0 + (0.5f - 0.5f * ((float)db / kDbRange)) * (GY1 - GY0);
            dl->AddLine(panel.P(GX0, y), panel.P(GX1, y),
                        db == 0 ? IM_COL32(64, 68, 74, 255) : IM_COL32(34, 36, 40, 255), 1.0f * sc());
            char b[8]; std::snprintf(b, sizeof(b), "%+d", db);
            if (db != -20) panel.text(dl, GX0 + 4, y - 5, 8.0f, IM_COL32(120, 124, 130, 255), db == 0 ? "0" : b, -1);
        }

        drawSpectrum(dl);
        const int N = 240;
        std::vector<ImVec2> pts; pts.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            const float lx = (float)i / (N - 1);
            const float freq = std::pow(10.0f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
            float ny = 0.5f - 0.5f * (responseDb(freq) / kDbRange);
            ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
            pts.push_back(panel.P(GX0 + lx * (GX1 - GX0), GY0 + ny * (GY1 - GY0)));
        }
        dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(236, 236, 236, 255), 0, 2.0f * sc());

        dl->PopClipRect();
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
        std::vector<ImVec2> pts; pts.reserve((size_t)half + 2);
        pts.push_back(panel.P(GX0, GY1));
        for (int k = 1; k <= half; ++k)
        {
            const float freq = (float)k * binHz;
            float db = 20.0f * std::log10(mag[k] > 1e-7f ? mag[k] : 1e-7f);
            specDb[(size_t)k] += (db - specDb[(size_t)k]) * smooth;
            if (freq < kFMin || freq > kFMax) continue;
            float ny = 1.0f - (specDb[(size_t)k] + 72.0f) / 72.0f;
            ny = ny < 0 ? 0 : (ny > 1 ? 1 : ny);
            pts.push_back(panel.P(GX0 + flog(freq) * (GX1 - GX0), GY0 + ny * (GY1 - GY0)));
        }
        pts.push_back(panel.P(GX1, GY1));
        if (pts.size() > 3)
        {
            dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), IM_COL32(70, 110, 140, 46));
            dl->AddPolyline(pts.data() + 1, (int)pts.size() - 2, IM_COL32(96, 150, 190, 130), 0, 1.2f * sc());
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
        colKnob(dl, "input", kInputGain, -12.f, 12.f, fcx, cY(568), 26, C_GREY, "INPUT", "-12", "+12", "%.1f", " dB");

        // LF — SSL brown console band: GAIN (0 top, +-15 dB) + FREQ (200 top,
        // 30-450 Hz) with labelled detents, plus a metal BELL/SHELF button.
        {
            const float lcx = 0.5f * (COL[1] + COL[2]);
            static const float GT[11] = { 0.f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1.f };
            static const float GV[11] = { -15.f, -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f, 15.f };
            static const char* const GL[11] = { "-15", "12", "9", "6", "3", "0", "3", "6", "9", "12", "+15" };
            static const float FT[7] = { 0.f, 1.f/6, 2.f/6, 3.f/6, 4.f/6, 5.f/6, 1.f };
            static const float FV[7] = { 30.f, 50.f, 100.f, 200.f, 300.f, 400.f, 450.f };
            static const char* const FL[7] = { "30", "50", "100", "200", "300", "400", "450" };
            consoleDetentKnob(dl, "lfg", lcx, cY(314), 28.f, kLfGain, GT, GV, GL, 11, C_LF_BROWN, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lff", lcx, cY(452), 28.f, kLfFreq, FT, FV, FL, 7, C_LF_BROWN, "Hz", "%.0f Hz");
            metalButton(dl, "lfbell", lcx - 32.f, cY(560), lcx + 32.f, cY(584), kLfBell, "BELL", "SHELF");
        }
        band(dl, 2, "LMF", C_LMF, kLmGain, kLmFreq, kLmQ,    +1, 200.f, 2500.f);
        band(dl, 3, "HMF", C_HMF, kHmGain, kHmFreq, kHmQ,    +1, 600.f, 7000.f);
        band(dl, 4, "HF",  C_HF,  kHfGain, kHfFreq, kHfBell, -1, 1500.f, 16000.f);

        // MASTER
        const float mcx = 0.5f * (COL[5] + COL[6]);
        panelButton(dl, "bypass", mcx - 40, cY(268), mcx + 40, cY(292),
                    values[kBypass] > 0.5f ? "BYPASSED" : "BYPASS",
                    values[kBypass] > 0.5f ? IM_COL32(150, 60, 48, 255) : IM_COL32(50, 50, 54, 255),
                    [&]{ toggleParam(kBypass); });
        panelButton(dl, "autogain", mcx - 40, cY(300), mcx + 40, cY(324), "AUTO GAIN",
                    values[kAutoGain] > 0.5f ? kGreenBtn : IM_COL32(50, 50, 54, 255),
                    [&]{ toggleParam(kAutoGain); });
        colKnob(dl, "drive", kSaturation, 0.f, 100.f, mcx, cY(430), 26, C_GREY, "DRIVE", "0", "100", "%.0f", "");
        colKnob(dl, "outg", kOutputGain, -12.f, 12.f, mcx, cY(556), 26, C_GREY, "OUTPUT", "-12", "+12", "%.1f", " dB");
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
        if (ImGui::IsItemActivated()) { editParameter(enId, true); editParameter(freqId, true); stepDragT = t; }
        if (act)
        {
            const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
            stepDragT = c01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
            t = stepDragT; stepApply(enId, freqId, F, t);
        }
        if (ImGui::IsItemDeactivated()) { editParameter(enId, false); editParameter(freqId, false); }
        if (hov && !act)
        {
            if (ImGui::IsMouseDoubleClicked(0)) // reset to OUT
            { editParameter(enId, true); stepApply(enId, freqId, F, 0.f); editParameter(enId, false); t = 0.f; }
            const float wh = ImGui::GetIO().MouseWheel;
            if (wh != 0.f)
            {
                t = c01(t + wh * 0.02f);
                editParameter(enId, true); editParameter(freqId, true);
                stepApply(enId, freqId, F, t);
                editParameter(freqId, false); editParameter(enId, false);
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

        if (hov || act)
        {
            char buf[24];
            if (!en) std::snprintf(buf, sizeof(buf), "OUT");
            else if (values[freqId] >= 1000.f) std::snprintf(buf, sizeof(buf), "%.1f kHz", values[freqId] / 1000.f);
            else std::snprintf(buf, sizeof(buf), "%.0f Hz", values[freqId]);
            panel.text(dl, cx, cy - R - 14.f, 9.5f, pal().whiteDim, buf, 0);
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
        dl->AddCircleFilled(ImVec2(c.x - capR * 0.22f, c.y - capR * 0.28f), capR * 0.7f, IM_COL32(255, 255, 255, 34), 40); // top-left sheen
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
        if (v <= V[0]) return T[0];
        if (v >= V[n - 1]) return T[n - 1];
        for (int i = 0; i < n - 1; ++i)
            if (v <= V[i + 1]) { const float a = (v - V[i]) / (V[i + 1] - V[i]); return T[i] + (T[i + 1] - T[i]) * a; }
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
        auto setFromT = [&](float tt) { const float nv = detentPosToVal(T, V, n, tt); values[paramId] = nv; setParameterValue(paramId, nv); };
        if (ImGui::IsItemActivated()) { editParameter(paramId, true); stepDragT = t; }
        if (act)
        {
            const float sp = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
            stepDragT = c01(stepDragT - ImGui::GetIO().MouseDelta.y * sp);
            t = stepDragT; setFromT(t);
        }
        if (ImGui::IsItemDeactivated()) editParameter(paramId, false);
        if (hov && !act)
        {
            if (ImGui::IsMouseDoubleClicked(0))
            { editParameter(paramId, true); values[paramId] = kDefaults[paramId]; setParameterValue(paramId, kDefaults[paramId]); editParameter(paramId, false); t = detentValToPos(T, V, n, values[paramId]); }
            const float wh = ImGui::GetIO().MouseWheel;
            if (wh != 0.f) { t = c01(t + wh * 0.02f); editParameter(paramId, true); setFromT(t); editParameter(paramId, false); }
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

        if (hov || act)
        {
            char buf[24]; std::snprintf(buf, sizeof(buf), fmt, values[paramId]);
            panel.text(dl, cx, cy - R - 13.f, 9.5f, pal().whiteDim, buf, 0);
        }
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
        panel.text(dl, INX0 - 2, cY(208), 8.5f, IM_COL32(150, 152, 156, 255), "INPUT", -1, true);
        panel.text(dl, OUTX1 + 2, cY(208), 8.5f, IM_COL32(150, 152, 156, 255), "OUTPUT", 1, true);
        meterPair(dl, INX0, INX1, inL, inR);
        meterPair(dl, OUTX0, OUTX1, outL, outR);
        char b[16];
        std::snprintf(b, sizeof(b), "%.0f", 20.0f * std::log10(std::max(inL, inR) > 1e-5f ? std::max(inL, inR) : 1e-5f));
        panel.text(dl, 0.5f * (INX0 + INX1), cY(MET_Y1) + 4, 8.0f, IM_COL32(140, 142, 146, 255), b, 0);
        std::snprintf(b, sizeof(b), "%.0f", 20.0f * std::log10(std::max(outL, outR) > 1e-5f ? std::max(outL, outR) : 1e-5f));
        panel.text(dl, 0.5f * (OUTX0 + OUTX1), cY(MET_Y1) + 4, 8.0f, IM_COL32(140, 142, 146, 255), b, 0);
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
    ImFont* labelFont = nullptr;
    float values[kParamCount] = {};
    int currentPreset = -1;
    bool showGraph = true;
    bool needResize = false;
    float ctlDstTop_ = 220.0f, ctlDstBot_ = 662.0f, ctlScaleY_ = 1.0f;
    float stepDragT = 0.0f; // stepped filter-knob drag origin (HPF/LPF)

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQUI)
};

UI* createUI() { return new FourKEQUI(); }

END_NAMESPACE_DISTRHO
