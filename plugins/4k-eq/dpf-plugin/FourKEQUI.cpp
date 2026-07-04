// FourKEQUI.cpp — Dear ImGui UI for 4K EQ 2. Console-strip layout: an analytic
// response curve with a live FFT spectrum overlay (pre/post), four parametric
// band columns plus HPF/LPF, global controls and L/R I/O meters. All custom
// ImDrawList rendering in a 980x520 design space, uniformly scaled. The curve
// is computed from the SAME coefficient math as the audio path
// (FourKEQDSP static designers), never by probing audio.

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
    constexpr float kDesignW = 980.0f;
    constexpr float kDesignH = 520.0f;
    constexpr float kDbRange = 15.0f;   // curve vertical: +-15 dB full height
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;

    // graph + meter rects (design space)
    constexpr float GX0 = 58, GY0 = 58, GX1 = 838, GY1 = 250;
    constexpr float MX0 = 852, MY0 = 58, MX1 = 966, MY1 = 250;

    constexpr float kDefaults[kParamCount] = {
        20.f, 0.f,      // hpf freq/en
        20000.f, 0.f,   // lpf freq/en
        0.f, 100.f, 0.f,        // lf
        0.f, 600.f, 0.7f,       // lm
        0.f, 2000.f, 0.7f,      // hm
        0.f, 8000.f, 0.f,       // hf
        0.f,            // eq type
        0.f,            // bypass
        0.f, 0.f, 0.f,  // in/out gain, sat
        0.f, 0.f, 0.f, 1.f,     // os, ms, prepost, autogain
        0.f, 0.f,       // out peaks
    };

    const int   kGridF[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
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
        setGeometryConstraints(560, 297, true);
        labelFont = duskdpf::loadCrispFont(30.0f * getScaleFactor());
        fft.prepare(kFftSize);
        specDb.assign(kFftSize / 2 + 1, -120.0f);
        duskdpf::Palette pal;
        pal.accent = IM_COL32(120, 175, 235, 255);
        panel.setPalette(pal);
    }

    //--- ParamHost ------------------------------------------------------------
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override { setParameterValue(idx, v); }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParamCount) values[index] = value;
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth(), winH = (float)getHeight();
        const float s = std::min(winW / kDesignW, winH / kDesignH);
        const ImVec2 org(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));
        panel.begin(s, org, labelFont, this);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("4KEQ2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 8, 8, 255));
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, kDesignH), IM_COL32(30, 30, 33, 255));

        drawHeader(dl);
        drawGraph(dl);
        drawMeters(dl);
        drawBands(dl);
        drawGlobals(dl);

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

private:
    static constexpr int kFftSize = 2048;

    //--- header + preset combo ------------------------------------------------
    void drawHeader(ImDrawList* dl)
    {
        const auto& pal = panel.palette();
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 46), IM_COL32(16, 16, 17, 255));
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddRect(panel.P(20, 8), panel.P(190, 38), IM_COL32(210, 210, 210, 200), 4.0f * panel.scale(), 0, 1.6f * panel.scale());
        panel.text(dl, 32, 13, 19, pal.white, "4K EQ 2", -1, true);
        panel.text(dl, kDesignW - 24, 14, 14, pal.white, "Dusk Audio", 1, true);

        ImGui::SetCursorScreenPos(panel.P(210, 10));
        ImGui::SetNextItemWidth(220.0f * panel.scale());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(60, 90, 130, 255));
        const char* preview = (currentPreset >= 0 && currentPreset < kNumFactoryPresets)
                                  ? kFactoryPresets[currentPreset].name : "Presets...";
        if (ImGui::BeginCombo("##presets", preview))
        {
            for (int i = 0; i < kNumFactoryPresets; ++i)
                if (ImGui::Selectable(kFactoryPresets[i].name, i == currentPreset))
                    applyPreset(i);
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(3);

        // spectrum pre/post toggle
        panel.toggle("prepost", kSpectrumPrePost, 452, 12, 556, 34, values[kSpectrumPrePost],
                     values[kSpectrumPrePost] > 0.5f ? "SPEC: PRE" : "SPEC: POST");
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
        for (const KV& e : kv)
        {
            editParameter(e.id, true); values[e.id] = e.v;
            setParameterValue(e.id, e.v); editParameter(e.id, false);
        }
    }

    //--- analytic composite response (same coeff math as the audio core) ------
    // Fixed display rate: the audible 20 Hz-20 kHz shape is essentially
    // rate-independent for these prewarped filters; 96 kHz matches 2x @ 48k.
    float responseDb(float freq) const
    {
        using duskaudio::Biquad;
        using duskaudio::BiquadCoeffs;
        const double fs = 96000.0;
        const double w = 2.0 * 3.14159265358979323846 * (double)freq / fs;
        const bool black = values[kEqType] > 0.5f;
        double magLin = 1.0;
        auto acc = [&](const BiquadCoeffs& c) { Biquad b; b.setCoeffs(c); magLin *= b.magnitude(w); };

        if (values[kHpfEnabled] > 0.5f)
        {
            acc(Biquad::firstOrderHighPass(fs, values[kHpfFreq]));
            acc(Biquad::highPass(fs, values[kHpfFreq], 0.54f));
        }
        // LF
        if (black && values[kLfBell] > 0.5f)
            acc(duskaudio::FourKEQDSP::consolePeak(fs, values[kLfFreq], 0.7f, values[kLfGain], black));
        else
            acc(duskaudio::FourKEQDSP::consoleShelf(fs, values[kLfFreq], 0.7f, values[kLfGain], false, black));
        // LM
        {
            float q = values[kLmQ];
            if (black) q = duskaudio::FourKEQDSP::dynamicQ(values[kLmGain], q);
            acc(duskaudio::FourKEQDSP::consolePeak(fs, values[kLmFreq], q, values[kLmGain], black));
        }
        // HM
        {
            float f = values[kHmFreq], q = values[kHmQ];
            if (black) q = duskaudio::FourKEQDSP::dynamicQ(values[kHmGain], q);
            else if (f > 7000.f) f = 7000.f;
            if (f > 3000.f) f = duskaudio::FourKEQDSP::preWarp(f, fs);
            acc(duskaudio::FourKEQDSP::consolePeak(fs, f, q, values[kHmGain], black));
        }
        // HF
        {
            const float fw = duskaudio::FourKEQDSP::preWarp(values[kHfFreq], fs);
            if (black && values[kHfBell] > 0.5f)
                acc(duskaudio::FourKEQDSP::consolePeak(fs, fw, 0.7f, values[kHfGain], black));
            else
                acc(duskaudio::FourKEQDSP::consoleShelf(fs, fw, 0.7f, values[kHfGain], true, black));
        }
        if (values[kLpfEnabled] > 0.5f)
        {
            float f = values[kLpfFreq];
            if (f > fs * 0.3) f = duskaudio::FourKEQDSP::preWarp(f, fs);
            acc(Biquad::lowPass(fs, f, black ? 0.8f : 0.707f));
        }
        return 20.0f * std::log10((float)std::max(magLin, 1e-6));
    }

    void drawGraph(ImDrawList* dl)
    {
        const auto& pal = panel.palette();
        const float sc = panel.scale();
        dl->AddRectFilled(panel.P(GX0 - 4, GY0 - 4), panel.P(GX1 + 4, GY1 + 4), IM_COL32(70, 70, 72, 255), 4.0f * sc);
        dl->AddRectFilled(panel.P(GX0, GY0), panel.P(GX1, GY1), IM_COL32(15, 17, 20, 255));
        dl->PushClipRect(panel.P(GX0, GY0), panel.P(GX1, GY1), true);

        // frequency grid
        for (int i = 0; i < (int)(sizeof(kGridF) / sizeof(kGridF[0])); ++i)
        {
            const float lx = (std::log10((float)kGridF[i]) - std::log10(kFMin)) / (std::log10(kFMax) - std::log10(kFMin));
            const float x = GX0 + lx * (GX1 - GX0);
            dl->AddLine(panel.P(x, GY0), panel.P(x, GY1), IM_COL32(45, 48, 52, 255), 1.0f * sc);
            panel.text(dl, x, GY1 - 12, 8.5f, IM_COL32(120, 124, 130, 255), kGridFL[i], 0);
        }
        // dB grid
        for (int db = -12; db <= 12; db += 6)
        {
            const float ny = 0.5f - 0.5f * ((float)db / kDbRange);
            const float y = GY0 + ny * (GY1 - GY0);
            dl->AddLine(panel.P(GX0, y), panel.P(GX1, y),
                        db == 0 ? IM_COL32(70, 74, 80, 255) : IM_COL32(38, 40, 44, 255), 1.0f * sc);
            char b[8]; std::snprintf(b, sizeof(b), "%+d", db);
            if (db != 0) panel.text(dl, GX0 + 3, y - 5, 8.0f, IM_COL32(110, 114, 120, 255), b, -1);
        }

        drawSpectrum(dl);

        // response curve
        const int N = 220;
        std::vector<ImVec2> pts; pts.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            const float lx = (float)i / (N - 1);
            const float freq = std::pow(10.0f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
            pts.push_back(panel.curvePoint(GX0, GY0, GX1, GY1, freq, responseDb(freq), kFMin, kFMax, kDbRange));
        }
        dl->AddPolyline(pts.data(), (int)pts.size(), pal.accent, 0, 2.2f * sc);

        dl->PopClipRect();
    }

    //--- live FFT spectrum overlay --------------------------------------------
    void drawSpectrum(ImDrawList* dl)
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        const duskaudio::SpectrumRing* ring = nullptr;
        const bool pre = values[kSpectrumPrePost] > 0.5f;
        if ((pre ? fourKEQGetPreSpectrum : fourKEQGetPostSpectrum) != nullptr)
            if (void* inst = getPluginInstancePointer())
                ring = pre ? fourKEQGetPreSpectrum(inst) : fourKEQGetPostSpectrum(inst);
        if (ring == nullptr) return;

        float buf[kFftSize];
        ring->snapshot(buf, kFftSize);
        float mag[kFftSize / 2 + 1];
        fft.magnitude(buf, mag);

        const float sc = panel.scale();
        const float dt = ImGui::GetIO().DeltaTime;
        const float smooth = 1.0f - std::exp(-dt * 12.0f);
        const int half = kFftSize / 2;
        std::vector<ImVec2> pts; pts.reserve(half);
        for (int k = 1; k <= half; ++k)
        {
            const float freq = (float)k * 48000.0f / kFftSize; // display bin freq @48k
            float db = 20.0f * std::log10(mag[k] > 1e-7f ? mag[k] : 1e-7f);
            specDb[(size_t)k] += (db - specDb[(size_t)k]) * smooth;
            if (freq < kFMin || freq > kFMax) continue;
            const float lx = (std::log10(freq) - std::log10(kFMin)) / (std::log10(kFMax) - std::log10(kFMin));
            const float x = GX0 + lx * (GX1 - GX0);
            // map -72..0 dBFS into the graph height
            float ny = 1.0f - (specDb[(size_t)k] + 72.0f) / 72.0f;
            ny = ny < 0.0f ? 0.0f : (ny > 1.0f ? 1.0f : ny);
            pts.push_back(panel.P(x, GY0 + ny * (GY1 - GY0)));
        }
        if (pts.size() > 2)
            dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(90, 130, 100, 150), 0, 1.3f * sc);
       #else
        (void)dl;
       #endif
    }

    //--- I/O meters -----------------------------------------------------------
    void drawMeters(ImDrawList* dl)
    {
        const auto& pal = panel.palette();
        const float sc = panel.scale();
        dl->AddRectFilled(panel.P(MX0 - 4, MY0 - 4), panel.P(MX1 + 4, MY1 + 4), IM_COL32(70, 70, 72, 255), 4.0f * sc);
        dl->AddRectFilled(panel.P(MX0, MY0), panel.P(MX1, MY1), IM_COL32(15, 17, 20, 255));

        float inL = values[kOutPeakL], inR = values[kOutPeakR], outL = inL, outR = inR;
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fourKEQGetInputPeakL != nullptr)
            if (void* inst = getPluginInstancePointer())
            {
                inL = fourKEQGetInputPeakL(inst); inR = fourKEQGetInputPeakR(inst);
                outL = fourKEQGetOutputPeakL(inst); outR = fourKEQGetOutputPeakR(inst);
            }
       #endif
        const float bw = 20.0f, gap = 6.0f;
        const float x0 = MX0 + 12;
        drawBar(dl, x0,             MY0 + 20, bw, MY1 - 18, inL,  "I-L");
        drawBar(dl, x0 + bw + gap,  MY0 + 20, bw, MY1 - 18, inR,  "I-R");
        drawBar(dl, x0 + 2*(bw+gap)+8, MY0 + 20, bw, MY1 - 18, outL, "O-L");
        drawBar(dl, x0 + 3*(bw+gap)+8, MY0 + 20, bw, MY1 - 18, outR, "O-R");
        panel.text(dl, 0.5f * (MX0 + MX1), MY0 + 6, 9.0f, pal.whiteDim, "METERS", 0);
    }

    void drawBar(ImDrawList* dl, float x, float yTop, float w, float yBot, float lin, const char* lbl)
    {
        const float sc = panel.scale();
        dl->AddRectFilled(panel.P(x, yTop), panel.P(x + w, yBot), IM_COL32(28, 30, 33, 255));
        float db = 20.0f * std::log10(lin > 1e-5f ? lin : 1e-5f);
        float t = (db + 60.0f) / 60.0f; t = t < 0 ? 0 : (t > 1 ? 1 : t); // -60..0 dBFS
        const float yFill = yBot - t * (yBot - yTop);
        ImU32 col = db > -3.0f ? IM_COL32(230, 70, 55, 255)
                  : db > -12.0f ? IM_COL32(230, 200, 70, 255)
                                : IM_COL32(90, 200, 110, 255);
        dl->AddRectFilled(panel.P(x, yFill), panel.P(x + w, yBot), col);
        panel.text(dl, x + w * 0.5f, yBot + 3, 7.5f, IM_COL32(120, 124, 130, 255), lbl, 0);
    }

    //--- band columns ---------------------------------------------------------
    void drawBands(ImDrawList* dl)
    {
        const float y = 300;      // knob row center
        const float top = 268;    // label baseline
        // HPF column
        panel.knobLabel(dl, 92, top, "HPF");
        panel.knob("hpf_f", kHpfFreq, 20.f, 500.f, 92, y, 24, values[kHpfFreq], kDefaults[kHpfFreq], false, true, "%.0f", " Hz");
        panel.toggle("hpf_en", kHpfEnabled, 70, 338, 114, 356, values[kHpfEnabled], values[kHpfEnabled] > 0.5f ? "ON" : "OFF");

        drawBand(dl, 210, "LF", kLfGain, kLfFreq, 30.f, 480.f, kLfBell, -1);
        drawBand(dl, 372, "LM", kLmGain, kLmFreq, 200.f, 2500.f, kLmQ, +1);
        drawBand(dl, 534, "HM", kHmGain, kHmFreq, 600.f, 7000.f, kHmQ, +1);
        drawBand(dl, 696, "HF", kHfGain, kHfFreq, 1500.f, 16000.f, kHfBell, -1);

        // LPF column
        panel.knobLabel(dl, 872, top, "LPF");
        panel.knob("lpf_f", kLpfFreq, 3000.f, 20000.f, 872, y, 24, values[kLpfFreq], kDefaults[kLpfFreq], false, true, "%.0f", " Hz");
        panel.toggle("lpf_en", kLpfEnabled, 850, 338, 894, 356, values[kLpfEnabled], values[kLpfEnabled] > 0.5f ? "ON" : "OFF");
    }

    // A parametric band column: gain knob (top), freq knob (below), and a
    // Q knob (mids, thirdParam>0) or a bell/shelf toggle (LF/HF, thirdParam<0).
    void drawBand(ImDrawList* dl, float cx, const char* name,
                  uint32_t gainId, uint32_t freqId, float fMin, float fMax,
                  uint32_t thirdId, int thirdKind)
    {
        panel.knobLabel(dl, cx, 268, name);
        panel.knob((std::string(name) + "_g").c_str(), gainId, -20.f, 20.f, cx - 26, 300, 20,
                   values[gainId], kDefaults[gainId], false, true, "%.1f", " dB");
        panel.knob((std::string(name) + "_f").c_str(), freqId, fMin, fMax, cx + 26, 300, 20,
                   values[freqId], kDefaults[freqId], false, true, "%.0f", " Hz");
        if (thirdKind > 0) // Q knob
            panel.knob((std::string(name) + "_q").c_str(), thirdId, 0.4f, 4.0f, cx, 360, 18,
                       values[thirdId], kDefaults[thirdId], false, true, "%.2f", "");
        else               // bell/shelf toggle
            panel.toggle((std::string(name) + "_b").c_str(), thirdId, cx - 26, 350, cx + 26, 370,
                         values[thirdId], values[thirdId] > 0.5f ? "BELL" : "SHELF");
    }

    //--- global row -----------------------------------------------------------
    void drawGlobals(ImDrawList* dl)
    {
        const auto& pal = panel.palette();
        const float sc = panel.scale();
        dl->AddRectFilled(panel.P(20, 392), panel.P(kDesignW - 20, 500), IM_COL32(22, 22, 24, 255), 6.0f * sc);
        const float y = 448, top = 404;

        panel.knobLabel(dl, 70, top, "INPUT");
        panel.knob("ingain", kInputGain, -12.f, 12.f, 70, y, 20, values[kInputGain], kDefaults[kInputGain], false, true, "%.1f", " dB");
        panel.knobLabel(dl, 158, top, "OUTPUT");
        panel.knob("outgain", kOutputGain, -12.f, 12.f, 158, y, 20, values[kOutputGain], kDefaults[kOutputGain], false, true, "%.1f", " dB");
        panel.knobLabel(dl, 246, top, "SATUR.");
        panel.knob("sat", kSaturation, 0.f, 100.f, 246, y, 20, values[kSaturation], kDefaults[kSaturation], false, true, "%.0f", " %");

        // EQ type (Brown/Black)
        panel.text(dl, 360, top + 4, 10, pal.white, "EQ VOICE", 0, true);
        panel.toggle("eqtype", kEqType, 330, 430, 434, 452, values[kEqType],
                     values[kEqType] > 0.5f ? "BLACK (G)" : "BROWN (E)");
        // Oversampling
        panel.text(dl, 360, top + 62, 10, pal.white, "OVERSAMPLE", 0, true);
        panel.toggle("os", kOversampling, 330, 464, 434, 486, values[kOversampling],
                     values[kOversampling] > 0.5f ? "4x" : "2x");

        // M/S + auto-gain
        panel.toggle("ms", kMsMode, 470, 430, 574, 452, values[kMsMode],
                     values[kMsMode] > 0.5f ? "M/S ON" : "M/S OFF");
        panel.toggle("autogain", kAutoGain, 470, 464, 574, 486, values[kAutoGain],
                     values[kAutoGain] > 0.5f ? "AUTOGAIN" : "AUTO OFF");

        // Power / bypass (host-designated)
        const bool on = values[kBypass] < 0.5f;
        panel.text(dl, 900, top, 10, pal.white, "POWER", 0, true);
        panel.led(dl, 900, top + 22, on, 7.0f);
        // toggle flips bypass; note inverted (on == not bypassed)
        float shown = on ? 0.0f : 1.0f; // toggle expects value>0.5 == active(bypassed)
        (void)shown;
        const ImVec2 h0 = panel.P(872, 440), h1 = panel.P(928, 488);
        ImGui::SetCursorScreenPos(h0);
        ImGui::InvisibleButton("power", ImVec2(h1.x - h0.x, h1.y - h0.y));
        if (ImGui::IsItemClicked())
        {
            const float nv = on ? 1.0f : 0.0f;
            editParameter(kBypass, true); values[kBypass] = nv;
            setParameterValue(kBypass, nv); editParameter(kBypass, false);
        }
        dl->AddRectFilled(panel.P(884, 448), panel.P(916, 484), IM_COL32(45, 45, 48, 255), 4.0f * sc);
        dl->AddRectFilled(on ? panel.P(888, 452) : panel.P(888, 468),
                          on ? panel.P(912, 466) : panel.P(912, 482),
                          IM_COL32(190, 190, 194, 255), 3.0f * sc);
        panel.text(dl, 900, 490, 8.5f, on ? pal.white : pal.whiteDim, on ? "ON" : "BYPASS", 0);
    }

    duskdpf::DuskPanel panel;
    duskdpf::RealFFT fft;
    std::vector<float> specDb;
    ImFont* labelFont = nullptr;
    float values[kParamCount] = {};
    int currentPreset = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQUI)
};

UI* createUI() { return new FourKEQUI(); }

END_NAMESPACE_DISTRHO
