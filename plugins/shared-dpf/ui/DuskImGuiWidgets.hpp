// DuskImGuiWidgets.hpp — reusable Dear ImGui panel toolkit for Dusk DPF UIs.
//
// Extracted from plugins/tape-echo/dpf-plugin/TapeEchoUI.cpp. Provides a fixed
// design-space coordinate mapper, a chrome knob (drag / shift-fine / wheel /
// double-click-reset, single active-knob drag state), LED, toggle, styled text,
// plus an analytic response-curve polyline mapper and a small real-FFT +
// spectrum drawer for EQ/analyzer UIs. Colors come from a Palette struct;
// host parameter edits go through a ParamHost interface so this stays free of
// any DPF include (the UI subclass adapts editParameter/setParameterValue).
//
// Requires imgui.h already included by the translation unit.

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace duskdpf
{

// Adapter the UI implements to forward parameter edits to the host.
struct ParamHost
{
    virtual ~ParamHost() = default;
    virtual void beginEdit(uint32_t idx) = 0;
    virtual void endEdit(uint32_t idx) = 0;
    virtual void setParam(uint32_t idx, float value) = 0;
};

struct Palette
{
    ImU32 white    = IM_COL32(238, 236, 228, 255);
    ImU32 whiteDim = IM_COL32(202, 200, 191, 255);
    ImU32 ledOn    = IM_COL32(255, 60, 40, 255);
    ImU32 ledGlow  = IM_COL32(255, 70, 45, 90);
    ImU32 ledOff   = IM_COL32(70, 20, 15, 255);
    ImU32 accent   = IM_COL32(120, 170, 235, 255);
};

class DuskPanel
{
public:
    static constexpr float kPi = 3.14159265358979f;

    void begin(float scale, ImVec2 origin, ImFont* font, ParamHost* h) noexcept
    {
        s = scale; org = origin; labelFont = font; host = h;
    }
    void setPalette(const Palette& p) noexcept { pal = p; }
    float scale() const noexcept { return s; }

    ImVec2 P(float x, float y) const noexcept { return ImVec2(org.x + x * s, org.y + y * s); }

    void text(ImDrawList* dl, float x, float y, float size, ImU32 col,
              const char* txt, int align /*-1 L,0 C,1 R*/, bool bold = false) const
    {
        ImFont* font = labelFont != nullptr ? labelFont : ImGui::GetFont();
        const float sz = size * s;
        const ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0.0f, txt);
        ImVec2 pos = P(x, y);
        if (align == 0) pos.x -= 0.5f * ts.x;
        if (align == 1) pos.x -= ts.x;
        dl->AddText(font, sz, pos, col, txt);
        if (bold && labelFont == nullptr)
            dl->AddText(font, sz, ImVec2(pos.x + 0.6f * s, pos.y), col, txt);
    }

    void led(ImDrawList* dl, float x, float y, bool on, float r = 5.0f) const
    {
        const ImVec2 c = P(x, y);
        dl->AddCircleFilled(c, (r + 2.5f) * s, IM_COL32(60, 60, 62, 255), 20);
        dl->AddCircleFilled(c, (r + 1.0f) * s, IM_COL32(0, 0, 0, 255), 20);
        if (on)
        {
            dl->AddCircleFilled(c, (r + 5.0f) * s, pal.ledGlow, 20);
            dl->AddCircleFilled(c, r * s, pal.ledOn, 20);
            dl->AddCircleFilled(ImVec2(c.x - 1.5f * s, c.y - 1.5f * s), 1.6f * s,
                                IM_COL32(255, 215, 205, 230), 10);
        }
        else
            dl->AddCircleFilled(c, r * s, pal.ledOff, 20);
    }

    static float knobAngle(float t) { return (-135.0f + 270.0f * t) * kPi / 180.0f; }

    // Chrome knob bound to value (mutated in place). fmt/suffix control the
    // hover readout (e.g. "%.1f"/" dB"). Returns true if the value changed.
    bool knob(const char* id, uint32_t param, float minV, float maxV,
              float cx, float cy, float radius, float& value, float defaultVal,
              bool stepped = false, bool panelTicks = true,
              const char* fmt = "%.2f", const char* suffix = "")
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float R  = radius * s;
        const ImVec2 c = P(cx, cy);
        const float range = maxV - minV;
        bool changed = false;

        ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
        ImGui::InvisibleButton(id, ImVec2(2.0f * R, 2.0f * R));
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();

        if (ImGui::IsItemActivated()) { host->beginEdit(param); dragValue = value; }
        if (active)
        {
            const float speed = ImGui::GetIO().KeyShift ? 0.0008f : 0.005f;
            dragValue -= ImGui::GetIO().MouseDelta.y * speed * range;
            dragValue = dragValue < minV ? minV : (dragValue > maxV ? maxV : dragValue);
            const float nv = stepped ? std::round(dragValue) : dragValue;
            if (nv != value) { value = nv; host->setParam(param, nv); changed = true; }
        }
        if (ImGui::IsItemDeactivated()) host->endEdit(param);

        if ((hovered || active) && ImGui::IsMouseDoubleClicked(0))
        {
            host->beginEdit(param);
            value = defaultVal; dragValue = value;
            host->setParam(param, value); host->endEdit(param); changed = true;
        }
        else if (hovered && !active)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                float nv = value + wheel * (stepped ? 1.0f : range * 0.02f);
                nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
                nv = stepped ? std::round(nv) : nv;
                if (nv != value)
                {
                    host->beginEdit(param); value = nv;
                    host->setParam(param, nv); host->endEdit(param); changed = true;
                }
            }
        }

        const float t = range > 0.0f ? (value - minV) / range : 0.0f;

        if (panelTicks)
            for (int i = 0; i <= 10; ++i)
            {
                const float a = knobAngle((float)i / 10.0f);
                const ImVec2 dir(std::sin(a), -std::cos(a));
                dl->AddLine(ImVec2(c.x + dir.x * (R + 2.5f * s), c.y + dir.y * (R + 2.5f * s)),
                            ImVec2(c.x + dir.x * (R + 6.5f * s), c.y + dir.y * (R + 6.5f * s)),
                            pal.whiteDim, 1.3f * s);
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

        const float a = knobAngle(t);
        const ImVec2 dir(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + dir.x * capR * 0.15f, c.y + dir.y * capR * 0.15f),
                    ImVec2(c.x + dir.x * capR * 0.95f, c.y + dir.y * capR * 0.95f),
                    IM_COL32(25, 25, 27, 255), 3.0f * s);

        if (hovered || active)
        {
            char buf[48];
            char num[32];
            std::snprintf(num, sizeof(num), fmt, value);
            std::snprintf(buf, sizeof(buf), "%s%s", num, suffix);
            text(dl, cx, cy + radius + 9.0f, 10.0f, pal.whiteDim, buf, 0);
        }
        return changed;
    }

    void knobLabel(ImDrawList* dl, float cx, float topY, const char* l1, const char* l2 = nullptr) const
    {
        text(dl, cx, topY, 11.0f, pal.white, l1, 0, true);
        if (l2 != nullptr) text(dl, cx, topY + 12.0f, 11.0f, pal.white, l2, 0, true);
        const float ty = topY + (l2 != nullptr ? 25.0f : 14.0f);
        dl->AddTriangleFilled(P(cx - 4.0f, ty), P(cx + 4.0f, ty), P(cx, ty + 6.0f), pal.white);
    }

    // Momentary/latching toggle button. Flips value between 0 and 1. Returns
    // true if toggled this frame.
    bool toggle(const char* id, uint32_t param, float x0, float y0, float x1, float y1,
                float& value, const char* label)
    {
        const bool on = value > 0.5f;
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        bool toggled = false;
        if (ImGui::IsItemClicked())
        {
            const float nv = on ? 0.0f : 1.0f;
            host->beginEdit(param); value = nv; host->setParam(param, nv); host->endEdit(param);
            toggled = true;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(b0, b1, IM_COL32(40, 40, 43, 255), 3.0f * s);
        dl->AddRect(b0, b1, on ? IM_COL32(200, 60, 45, 255) : IM_COL32(90, 90, 94, 255), 3.0f * s, 0, 1.4f * s);
        if (on) dl->AddCircleFilled(P(x0 + 7.0f, 0.5f * (y0 + y1)), 2.6f * s, pal.ledOn, 12);
        text(dl, 0.5f * (x0 + x1) + 6.0f, y0 + 0.30f * (y1 - y0), 10.0f,
             on ? pal.white : pal.whiteDim, label, 0, on);
        return toggled;
    }

    // Map a (log-frequency, dB) point into a pixel inside a design-space rect.
    ImVec2 curvePoint(float rx0, float ry0, float rx1, float ry1,
                      float freq, float db, float fMin, float fMax, float dbRange) const
    {
        const float lx = (std::log10(freq) - std::log10(fMin)) / (std::log10(fMax) - std::log10(fMin));
        const float ny = 0.5f - 0.5f * (db / dbRange); // 0 dB at vertical center
        return P(rx0 + lx * (rx1 - rx0), ry0 + ny * (ry1 - ry0));
    }

    const Palette& palette() const noexcept { return pal; }

private:
    float  s = 1.0f;
    ImVec2 org = ImVec2(0, 0);
    ImFont* labelFont = nullptr;
    ParamHost* host = nullptr;
    Palette pal;
    float dragValue = 0.0f;
};

//==============================================================================
// Small real-input radix-2 FFT for the spectrum analyzer (UI thread only).
// In-place, size must be a power of two. Returns magnitude (linear) bins 0..N/2.
//==============================================================================
class RealFFT
{
public:
    void prepare(int size)
    {
        n = size;
        re.assign((size_t)n, 0.0f);
        im.assign((size_t)n, 0.0f);
        window.assign((size_t)n, 0.0f);
        constexpr float kPi = 3.14159265358979f;
        for (int i = 0; i < n; ++i) // Hann
            window[(size_t)i] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (n - 1));
    }

    // in: n time samples. out: n/2+1 linear magnitudes (normalized).
    void magnitude(const float* in, float* out)
    {
        for (int i = 0; i < n; ++i) { re[(size_t)i] = in[i] * window[(size_t)i]; im[(size_t)i] = 0.0f; }
        transform();
        const float norm = 2.0f / (float)n;
        for (int k = 0; k <= n / 2; ++k)
            out[k] = std::sqrt(re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k]) * norm;
    }

    int size() const noexcept { return n; }

private:
    void transform()
    {
        // bit-reversal
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[(size_t)i], re[(size_t)j]); std::swap(im[(size_t)i], im[(size_t)j]); }
        }
        constexpr float kPi = 3.14159265358979f;
        for (int len = 2; len <= n; len <<= 1)
        {
            const float ang = -2.0f * kPi / len;
            const float wr = std::cos(ang), wi = std::sin(ang);
            for (int i = 0; i < n; i += len)
            {
                float cwr = 1.0f, cwi = 0.0f;
                for (int k = 0; k < len / 2; ++k)
                {
                    const int a = i + k, b = i + k + len / 2;
                    const float ur = re[(size_t)a], ui = im[(size_t)a];
                    const float vr = re[(size_t)b] * cwr - im[(size_t)b] * cwi;
                    const float vi = re[(size_t)b] * cwi + im[(size_t)b] * cwr;
                    re[(size_t)a] = ur + vr; im[(size_t)a] = ui + vi;
                    re[(size_t)b] = ur - vr; im[(size_t)b] = ui - vi;
                    const float ncwr = cwr * wr - cwi * wi;
                    cwi = cwr * wi + cwi * wr; cwr = ncwr;
                }
            }
        }
    }

    int n = 0;
    std::vector<float> re, im, window;
};

} // namespace duskdpf
