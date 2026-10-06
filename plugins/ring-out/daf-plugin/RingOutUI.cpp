// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutUI.cpp — Dear ImGui UI for Ring Out, the feedback eliminator.
//
// Layout (960 x 600 design space, uniformly scaled):
//   header row      the fleet-standard nameplate / preset / brand band
//   graph card      IN meter | log-frequency graph (RTA + filter curve) | OUT meter
//   toolbar         SENSE Low/High, SETUP, ADD, RESET, status, filter nav < >
//   filter card     20 indicators + NEW, ON, CUT / FREQ / Q steppers, DEL,
//                   GLOBAL Q / AMP + LINK, GAIN OUT
//   countdown bar   drains while SETUP is armed
//
// The filter table is read straight off the DSP every frame (direct access);
// edits go back as single "edit" state commands, so the engine's own additions
// and the user's never fight over a stale copy.

#include "DafUI.hpp"
#include "RingOutAccess.hpp"
#include "RingOutDSP.hpp"
#include "RingOutFilterTable.hpp"
#include "RingOutParams.hpp"
#include "RingOutVersion.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskLedMeter.hpp"
#include "DuskLogFreqAxis.hpp"
#include "DuskStepperBox.hpp"
#include "DuskSupportersOverlay.hpp"
#include "DuskUserPresetStore.hpp"
#include "util/CrashLog.hpp"
#include "RingOutFontRegular.inc"
#include "RingOutFontSemiBold.inc"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

START_NAMESPACE_DAF

namespace
{
    using duskaudio::RingOutDSP;
    namespace ro = duskaudio::ringout;

    constexpr float kDesignW = 960.0f;
    constexpr float kDesignH = 600.0f;

    // Palette: the dark chassis of the other Dusk DAF effects, with the two
    // instrument colours of a feedback display: cyan for what the microphone
    // hears, red for what the filters remove, and yellow for the selection.
    constexpr ImU32 kColHeader   = IM_COL32(14, 14, 15, 255);
    constexpr ImU32 kColCard     = IM_COL32(26, 27, 29, 255);
    constexpr ImU32 kColCardEdge = IM_COL32(60, 60, 63, 255);
    constexpr ImU32 kColGraphBg  = IM_COL32(10, 11, 13, 255);
    constexpr ImU32 kColGrid     = IM_COL32(38, 41, 46, 255);
    constexpr ImU32 kColGridMinor = IM_COL32(26, 28, 32, 255);
    constexpr ImU32 kColWhite    = IM_COL32(241, 238, 226, 255);
    constexpr ImU32 kColWhiteDim = IM_COL32(176, 174, 166, 255);
    constexpr ImU32 kColCyan     = IM_COL32(40, 220, 235, 255);
    constexpr ImU32 kColCyanDim  = IM_COL32(40, 220, 235, 90);
    constexpr ImU32 kColCyanFill = IM_COL32(40, 180, 200, 42);
    constexpr ImU32 kColRed      = IM_COL32(228, 48, 40, 255);
    constexpr ImU32 kColRedFill  = IM_COL32(220, 40, 30, 110);
    constexpr ImU32 kColYellow   = IM_COL32(255, 214, 40, 255);
    constexpr ImU32 kColGreen    = IM_COL32(72, 200, 110, 255);
    constexpr ImU32 kColBtn      = IM_COL32(38, 38, 41, 255);
    constexpr ImU32 kColBtnHot   = IM_COL32(54, 54, 58, 255);
    constexpr ImU32 kColBtnEdge  = IM_COL32(90, 90, 94, 255);

    // Graph rectangle and axes.
    constexpr float GX0 = 58.0f, GX1 = 902.0f, GY0 = 62.0f, GY1 = 334.0f;
    constexpr float kRtaMinDb = -80.0f;        // analyser axis, 0 at the top
    constexpr float kCurveMinDb = -24.0f;      // filter axis, 0 at the top

    constexpr float kPi = 3.14159265358979f;
}

class RingOutUI : public UI, public duskdaf::ParamHost
{
public:
    //--- duskdaf::ParamHost ------------------------------------------------------
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override
    {
        if (idx >= kParamCount || !std::isfinite(v))
            return;
        v = roNormalizeParamValue(idx, v);
        values[idx] = v;
        setParameterValue(idx, v);
        if (roIsPresetParam(idx))
            syncPresetSelection();
    }

    RingOutUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        supportersOverlay.setActionLink("Open crash log folder",
                                        [] { DuskCrashLog::openLogFolder(); });
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kRoParams[i].def;
        setGeometryConstraints((uint32_t)kDesignW, (uint32_t)kDesignH, true);

        static constexpr float kLabelSizes[] =
            { 7.0f, 9.0f, 11.0f, 14.0f, 18.0f, 24.0f, 28.0f, 32.0f, 48.0f, 72.0f };
        static constexpr float kRegularSizes[] =
            { 7.0f, 9.0f, 11.0f, 14.0f, 18.0f, 24.0f, 32.0f, 48.0f };

        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        if (const char* const forced = std::getenv("DUSK_GL_MAX_TEXTURE"))
        {
            const int v = std::atoi(forced);
            if (v > 0) maxTextureSize = (GLint)v;
        }

        const duskdaf::EmbeddedFontRequest fontRequests[2] = {
            { kRoFontSemiBold, kRoFontSemiBold_len, kLabelSizes,
              (int)(sizeof(kLabelSizes) / sizeof(kLabelSizes[0])) },
            { kRoFontRegular, kRoFontRegular_len, kRegularSizes,
              (int)(sizeof(kRegularSizes) / sizeof(kRegularSizes[0])) },
        };
        duskdaf::CrispFontSet fontSets[2];
        duskdaf::loadEmbeddedCrispFontSets(fontRequests, 2, fontSets, 1.0f, (int)maxTextureSize);
        labelFonts   = fontSets[0];
        regularFonts = fontSets[1];
        labelFont = labelFonts.pick(12.5f * getScaleFactor());
        panel.setFontSet(labelFonts);

        rta.assign((size_t)(GX1 - GX0) + 1, kRtaMinDb);
        scanUserPresets();
    }

protected:
    //--- host -> UI --------------------------------------------------------------
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount || !std::isfinite(value))
            return;
        values[index] = roNormalizeParamValue(index, value);
        if (roIsPresetParam(index))
            syncPresetSelection();
    }

    void programLoaded(uint32_t index) override
    {
        if (index >= (uint32_t)kRoNumFactoryPresets)
            return;
        // The plugin applied the defaults and cleared its table itself; the
        // host does not deliver parameterChanged for a program change.
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (roIsPresetParam(i))
                values[i] = kRoParams[i].def;
        table.clear();
        ++tableStamp;
        selected = -1;
        currentPreset = 0;
        currentUserName.clear();
        currentUserPath.clear();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr || std::strcmp(key, "filters") != 0)
            return;
        ro::FilterTable t;
        if (!ro::parseTable(value, t))
            return;
        table = t;
        ++tableStamp;
        clampSelection();
        syncPresetSelection();
    }

    //--- frame -------------------------------------------------------------------
    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s   = std::min(winW / kDesignW, winH / kDesignH);
        org = ImVec2(0.5f * (winW - kDesignW * s), 0.5f * (winH - kDesignH * s));
        panel.begin(s, org, labelFont, this);

        pullEngineState();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("RingOut", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 8, 8, 255));
        drawChassis(dl);

        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);
        drawGraphCard(dl);
        drawToolbar(dl);
        drawFilterCard(dl);
        drawCountdown(dl);
        if (values[kParamBypass] >= 0.5f)
        {
            dl->AddRectFilled(P(0, 50), P(kDesignW, kDesignH), IM_COL32(0, 0, 0, 110));
            text(dl, 0.5f * kDesignW, 190.0f, 22.0f, IM_COL32(230, 226, 214, 200), "BYPASSED", 0, true);
        }
        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdaf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "Ring Out", RO_VERSION_STRING, &supportersOverlay);

        const duskdaf::ResizeGripState grip = panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH);

        ImGui::End();
        ImGui::PopStyleVar(2);
        textInputFocus.update(*this);

        if (grip.hot != gripCursorSet)
        {
            gripCursorSet = grip.hot;
            setCursor(gripCursorSet ? DGL_NAMESPACE::kMouseCursorUpLeftDownRight
                                    : DGL_NAMESPACE::kMouseCursorArrow);
        }
        if (grip.resized)
            setSize(grip.width, grip.height);
    }

private:
    //--- engine access -------------------------------------------------------------
    const RingOutDSP* dsp() const
    {
       #if DAF_PLUGIN_WANT_DIRECT_ACCESS
        if (ringOutGetDSP != nullptr)
            if (void* const inst = getPluginInstancePointer())
                return ringOutGetDSP(inst);
       #endif
        return nullptr;
    }

    // Once per frame: mirror the engine's table, status, spectrum and meters.
    void pullEngineState()
    {
        const float dt = ImGui::GetIO().DeltaTime;
        const RingOutDSP* const d = dsp();
        if (d == nullptr)
        {
            inMeter.update(-100.0f, -100.0f, dt, meterStyle);
            outMeter.update(-100.0f, -100.0f, dt, meterStyle);
            return;
        }
        engineLive = true;
        status = d->status();
        if (status.tableVersion != tableVersionSeen)
        {
            tableVersionSeen = status.tableVersion;
            d->getTable(table);
            ++tableStamp;
            clampSelection();
            syncPresetSelection();
        }
        // SETUP timed out while armed: put the host parameter back so the next
        // press is a fresh rising edge.
        if (values[kParamSetup] >= 0.5f && !status.setupActive && status.setupExpired)
            setP(kParamSetup, 0.0f);

        inMeter.update(d->inputPeakDb(0), d->inputPeakDb(1), dt, meterStyle);
        outMeter.update(d->outputPeakDb(0), d->outputPeakDb(1), dt, meterStyle);

        // Spectrum -> per-pixel-column dB with RTA ballistics (fast up, 40 dB/s down).
        const unsigned seq = d->spectrumSequence();
        if (seq != spectrumSeqSeen && d->copySpectrum(spectrum))
            spectrumSeqSeen = seq;
        if (spectrum.bins > 0)
        {
            const int cols = (int)rta.size();
            std::vector<float>& colMax = rtaScratch;
            colMax.assign((size_t)cols, -200.0f);
            for (int k = 1; k < spectrum.bins; ++k)
            {
                const float f = (float)k * spectrum.binHz;
                if (f < axis.fMin || f > axis.fMax) continue;
                const int col = (int)(axis.toNorm(f) * (float)(cols - 1) + 0.5f);
                if (col < 0 || col >= cols) continue;
                colMax[(size_t)col] = std::max(colMax[(size_t)col], spectrum.db[k]);
            }
            // Fill columns no bin landed in (low end at a coarse bin width) from
            // their neighbours so the trace is continuous.
            float last = kRtaMinDb;
            for (int c = 0; c < cols; ++c)
            {
                if (colMax[(size_t)c] > -199.0f) last = colMax[(size_t)c];
                else colMax[(size_t)c] = last;
            }
            const float fall = 40.0f * std::clamp(dt, 0.0f, 0.1f);
            for (int c = 0; c < cols; ++c)
            {
                const float target = std::max(kRtaMinDb, colMax[(size_t)c]);
                float& v = rta[(size_t)c];
                if (target > v) v += 0.6f * (target - v);
                else            v = std::max(target, v - fall);
            }
        }
    }

    void clampSelection()
    {
        if (table.count == 0) selected = -1;
        else if (selected >= table.count) selected = table.count - 1;
    }

    // One UI edit of the table: optimistic local apply + the command to the plugin.
    void sendEdit(const ro::EditCommand& c)
    {
        char buf[128];
        if (!ro::formatEditCommand(c, buf, (int)sizeof(buf)))
            return;
        const int touched = ro::applyEditCommand(table, c);
        ++tableStamp;
        setState("edit", buf);
        if (c.kind == ro::EditCommand::kAdd && touched >= 0)
            selected = touched;
        else if (c.kind == ro::EditCommand::kDelete)
            selected = table.count == 0 ? -1 : std::min(c.slot, table.count - 1);
        else if (c.kind == ro::EditCommand::kClear)
            selected = -1;
        clampSelection();
        syncPresetSelection();
    }

    void setSelectedFilter(const ro::Filter& f)
    {
        if (selected < 0 || selected >= table.count) return;
        ro::EditCommand c;
        c.kind = ro::EditCommand::kSet;
        c.slot = selected;
        c.filter = f;
        sendEdit(c);
    }

    //--- helpers -----------------------------------------------------------------
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }

    void text(ImDrawList* dl, float x, float y, float size, ImU32 col,
              const char* txt, int align, bool bold = false) const
    {
        panel.text(dl, x, y, size, col, txt, align, bold);
    }

    void regularText(ImDrawList* dl, float x, float y, float size, ImU32 col,
                     const char* txt, int align = -1) const
    {
        const float px = size * s;
        ImFont* font = regularFonts.pick(px);
        if (font == nullptr) { text(dl, x, y, size, col, txt, align); return; }
        const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, txt);
        ImVec2 pos = P(x, y);
        if (align == 0) pos.x -= 0.5f * ts.x;
        if (align == 1) pos.x -= ts.x;
        pos.x = std::floor(pos.x + 0.5f);
        pos.y = std::floor(pos.y + 0.5f);
        dl->AddText(font, px, pos, col, txt);
    }

    static ImU32 fade(ImU32 c, float amount)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        const ImU32 a = (c >> 24) & 0xffu;
        return (c & 0x00ffffffu) | ((ImU32)(a * amount + 0.5f) << 24);
    }

    void card(ImDrawList* dl, float x0, float y0, float x1, float y1) const
    {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kColCard, 4.0f * s);
        dl->AddRect(P(x0, y0), P(x1, y1), kColCardEdge, 4.0f * s, 0, 1.0f * s);
    }

    // Rectangular button. `lit` paints it in litCol; returns clicked. `heldOut`
    // reports press-and-hold for momentary controls.
    bool button(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                const char* label, bool lit, ImU32 litCol, bool enabled = true,
                float textSize = 11.0f, bool* heldOut = nullptr)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        if (!enabled) ImGui::BeginDisabled();
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = enabled && ImGui::IsItemHovered();
        const bool held = enabled && ImGui::IsItemActive();
        const bool clicked = enabled && ImGui::IsItemClicked();
        if (!enabled) ImGui::EndDisabled();
        if (heldOut != nullptr) *heldOut = held;

        const ImU32 bg = lit ? litCol : (hov || held ? kColBtnHot : kColBtn);
        dl->AddRectFilled(b0, b1, bg, 3.0f * s);
        dl->AddRect(b0, b1, lit ? fade(litCol, 0.6f) : kColBtnEdge, 3.0f * s, 0, 1.0f * s);
        const ImU32 ink = !enabled ? IM_COL32(110, 110, 108, 255)
                        : lit ? IM_COL32(16, 16, 16, 255)
                        : (hov ? kColWhite : kColWhiteDim);
        text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - textSize) - 0.5f, textSize, ink, label, 0, true);
        return clicked;
    }

    bool roundButton(ImDrawList* dl, const char* id, float cx, float cy, float r,
                     const char* label, bool lit, ImU32 litCol, bool enabled = true,
                     float textSize = 10.0f)
    {
        const ImVec2 c = P(cx, cy);
        ImGui::SetCursorScreenPos(ImVec2(c.x - r * s, c.y - r * s));
        if (!enabled) ImGui::BeginDisabled();
        ImGui::InvisibleButton(id, ImVec2(2.0f * r * s, 2.0f * r * s));
        const bool hov = enabled && ImGui::IsItemHovered();
        const bool clicked = enabled && ImGui::IsItemClicked();
        if (!enabled) ImGui::EndDisabled();

        dl->AddCircleFilled(c, (r + 2.5f) * s, IM_COL32(60, 60, 62, 255), 32);
        dl->AddCircleFilled(c, (r + 1.0f) * s, IM_COL32(0, 0, 0, 255), 32);
        dl->AddCircleFilled(c, r * s, lit ? litCol : (hov ? kColBtnHot : kColBtn), 32);
        if (lit)
            dl->AddCircleFilled(c, (r + 4.0f) * s, fade(litCol, 0.25f), 32);
        const ImU32 ink = !enabled ? IM_COL32(110, 110, 108, 255)
                        : lit ? IM_COL32(16, 16, 16, 255) : (hov ? kColWhite : kColWhiteDim);
        text(dl, cx, cy - 0.5f * textSize - 0.5f, textSize, ink, label, 0, true);
        return clicked;
    }

    bool chevron(ImDrawList* dl, const char* id, float cx, float cy, float halfH, bool left)
    {
        const ImVec2 b0 = P(cx - 9.5f, cy - halfH);
        const ImVec2 b1 = P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? kColBtnHot : kColBtn, 2.0f * s);
        dl->AddRect(b0, b1, kColBtnEdge, 2.0f * s, 0, 1.0f * s);
        const ImVec2 c = P(cx, cy);
        const float  d = 4.3f * s;
        const ImU32 ink = hov ? kColWhite : kColWhiteDim;
        if (left)
            dl->AddTriangleFilled(ImVec2(c.x + d * 0.5f, c.y - d), ImVec2(c.x + d * 0.5f, c.y + d),
                                  ImVec2(c.x - d * 0.7f, c.y), ink);
        else
            dl->AddTriangleFilled(ImVec2(c.x - d * 0.5f, c.y - d), ImVec2(c.x - d * 0.5f, c.y + d),
                                  ImVec2(c.x + d * 0.7f, c.y), ink);
        return ImGui::IsItemClicked();
    }

    bool textButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                    const char* label)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hov ? kColBtnHot : kColBtn, 2.0f * s);
        dl->AddRect(b0, b1, kColBtnEdge, 2.0f * s, 0, 1.0f * s);
        constexpr float kTxt = 11.0f;
        text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - kTxt), kTxt,
             hov ? kColWhite : kColWhiteDim, label, 0, true);
        return ImGui::IsItemClicked();
    }

    void drawChassis(ImDrawList* dl) const
    {
        dl->AddRectFilledMultiColor(P(0, 0), P(kDesignW, kDesignH),
                                    IM_COL32(34, 34, 35, 255), IM_COL32(30, 30, 31, 255),
                                    IM_COL32(20, 20, 21, 255), IM_COL32(22, 22, 23, 255));
    }

    //--- header (fleet-standard row) ----------------------------------------------
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, 48), kColHeader);
        dl->AddRectFilledMultiColor(P(0, 0), P(kDesignW, 4),
                                    IM_COL32(205, 203, 197, 255), IM_COL32(205, 203, 197, 255),
                                    IM_COL32(91, 91, 91, 255), IM_COL32(91, 91, 91, 255));
        dl->AddLine(P(0, 48), P(kDesignW, 48), IM_COL32(91, 90, 88, 255), 1.2f * s);
        dl->AddLine(P(0, 49), P(kDesignW, 49), IM_COL32(0, 0, 0, 160), 1.0f * s);

        constexpr float kHdrCy = 24.0f;
        constexpr float kPlateX0 = 28.0f, kPlateX1 = 318.0f;
        constexpr float kPlateY0 = 8.0f, kPlateY1 = 40.0f;
        dl->AddRectFilledMultiColor(P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
                                    IM_COL32(37, 37, 38, 255), IM_COL32(29, 29, 30, 255),
                                    IM_COL32(16, 16, 17, 255), IM_COL32(19, 19, 20, 255));
        dl->AddRect(P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
                    IM_COL32(185, 184, 180, 220), 3.5f * s, 0, 1.2f * s);
        dl->AddLine(P(40, 37), P(308, 37), fade(kColRed, 0.8f), 1.2f * s);

        text(dl, 42, 10.0f, 24.0f, kColWhite, "RING OUT", -1, true);
        text(dl, 202, 14.0f, 20.0f, kColWhite, "RO-1", 0, true);
        regularText(dl, 306, 20.0f, 14.0f, kColWhiteDim, "v" RO_VERSION_STRING, 1);
        text(dl, kDesignW - 87.0f, 12.0f, 22.0f, kColWhite, "DUSK AUDIO", 0, true);

        ImGui::SetCursorScreenPos(P(kPlateX0, kPlateY0));
        if (ImGui::InvisibleButton("##titlecredits",
                                   ImVec2((kPlateX1 - kPlateX0) * s, (kPlateY1 - kPlateY0) * s)))
        {
            supportersOverlay.resetInteraction();
            showSupporters = true;
        }

        constexpr float kBandY0 = kHdrCy - 12.5f, kBandY1 = kHdrCy + 12.5f;
        constexpr float kBandH  = kBandY1 - kBandY0;
        if (chevron(dl, "##presetprev", 350.5f, kHdrCy, 0.5f * kBandH, true))
            stepPreset(-1);
        if (chevron(dl, "##presetnext", 569.5f, kHdrCy, 0.5f * kBandH, false))
            stepPreset(1);

        ImGui::SetCursorScreenPos(P(364, kBandY0));
        ImGui::SetNextItemWidth(192.0f * s);
        ImFont* presetFont = labelFonts.pick(14.0f * s);
        if (presetFont != nullptr) ImGui::PushFont(presetFont);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(6.0f * s, std::max(0.0f, 0.5f * (kBandH * s - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(110, 45, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(132, 57, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(92, 39, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 28, 24, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(110, 45, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(92, 39, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(160, 84, 70, 255));
        const char* preview = (currentPreset >= 0 && currentPreset < kRoNumFactoryPresets)
                                  ? kRoFactoryPresetNames[currentPreset]
                                  : (!currentUserName.empty() ? currentUserName.c_str() : "Presets...");
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginCombo("##presets", preview))
        {
            for (int i = 0; i < kRoNumFactoryPresets; ++i)
                if (ImGui::Selectable(kRoFactoryPresetNames[i], i == currentPreset))
                {
                    applyPreset(i);
                    ImGui::CloseCurrentPopup();
                }
            if (!userPresets.empty())
            {
                ImGui::SeparatorText("User");
                for (size_t i = 0; i < userPresets.size(); ++i)
                {
                    const UserPreset& up = userPresets[i];
                    ImGui::PushID((int)i);
                    if (ImGui::Selectable(up.name.c_str(), currentPreset < 0 && up.path == currentUserPath))
                    {
                        loadUserPreset(up);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(12);
        ImGui::PopStyleVar();
        if (presetFont != nullptr) ImGui::PopFont();

        if (textButton(dl, "##init", 587, kBandY0, 635, kBandY1, "INIT"))
            initDefaults();
        if (textButton(dl, "##save", 642, kBandY0, 690, kBandY1, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Preset");
        }
        drawSaveModal();
    }

    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(40, 40, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 50, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(56, 56, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 28, 24, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(110, 45, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(92, 39, 34, 255));
        if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool appearing = ImGui::IsWindowAppearing();
            if (appearing) saveFailed = false;
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(240.0f * s);
            if (appearing) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##savename", saveBuf, sizeof(saveBuf),
                                                ImGuiInputTextFlags_EnterReturnsTrue
                                                | ImGuiInputTextFlags_AutoSelectAll);
            const bool doSave = ImGui::Button("Save") || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (doSave && saveBuf[0] != '\0')
            {
                if (saveUserPreset(saveBuf)) { saveFailed = false; ImGui::CloseCurrentPopup(); }
                else saveFailed = true;
            }
            if (saveFailed)
                ImGui::TextColored(ImVec4(0.90f, 0.42f, 0.35f, 1.0f), "Could not save. Try a different name.");
            if (cancel) { saveFailed = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(9);
    }

    //--- graph card ----------------------------------------------------------------
    float rtaY(float db) const
    {
        const float ny = std::clamp(-db / -kRtaMinDb, 0.0f, 1.0f);   // 0 dB top .. -80 bottom
        return GY0 + ny * (GY1 - GY0);
    }
    float curveY(float db) const
    {
        const float ny = std::clamp(-db / -kCurveMinDb, 0.0f, 1.0f);
        return GY0 + ny * (GY1 - GY0);
    }
    float freqX(float f) const { return GX0 + axis.toNorm(f) * (GX1 - GX0); }

    void drawGraphCard(ImDrawList* dl)
    {
        card(dl, 20, 56, 940, 352);

        // Meters: a click resets the peak hold.
        drawMeter(dl, "##inmeter", inMeter, 26, 66, 42, 328, "IN");
        drawMeter(dl, "##outmeter", outMeter, 918, 66, 934, 328, "OUT");

        dl->AddRectFilled(P(GX0, GY0), P(GX1, GY1), kColGraphBg);
        dl->PushClipRect(P(GX0, GY0), P(GX1, GY1), true);

        // Grid: 1-2-5 rules labelled, the other integers of each decade faint.
        for (float decade = 10.0f; decade <= 10000.0f; decade *= 10.0f)
            for (int m = 2; m <= 9; ++m)
            {
                const float f = (float)m * decade;
                if (f <= axis.fMin || f >= axis.fMax || m == 2 || m == 5) continue;
                const float x = freqX(f);
                dl->AddLine(P(x, GY0), P(x, GY1), kColGridMinor, 1.0f * s);
            }
        float grid[16];
        const int nGrid = axis.gridLines(grid, 16);
        for (int i = 0; i < nGrid; ++i)
        {
            const float x = freqX(grid[i]);
            dl->AddLine(P(x, GY0), P(x, GY1), kColGrid, 1.0f * s);
            char lbl[12];
            duskdaf::LogFreqAxis::label(grid[i], lbl, (int)sizeof(lbl));
            regularText(dl, x, GY1 - 12.0f, 9.0f, IM_COL32(150, 154, 160, 255), lbl, 0);
        }
        for (int db = -10; db > (int)kRtaMinDb; db -= 10)
        {
            const float y = rtaY((float)db);
            dl->AddLine(P(GX0, y), P(GX1, y), kColGrid, 1.0f * s);
        }
        // Left axis: analyser dB (cyan). Right axis: filter dB (red).
        for (int db = 0; db >= (int)kRtaMinDb; db -= 20)
        {
            char b[8]; std::snprintf(b, sizeof(b), "%d", db);
            regularText(dl, GX0 + 4.0f, std::clamp(rtaY((float)db) - 5.0f, GY0 + 1.0f, GY1 - 12.0f),
                        9.0f, kColCyanDim, b, -1);
        }
        for (int db = -4; db >= (int)kCurveMinDb; db -= 4)
        {
            char b[8]; std::snprintf(b, sizeof(b), "%d", db);
            regularText(dl, GX1 - 4.0f, std::clamp(curveY((float)db) - 5.0f, GY0 + 1.0f, GY1 - 12.0f),
                        9.0f, fade(kColRed, 0.55f), b, 1);
        }

        drawRta(dl);
        drawFilterCurve(dl);

        dl->PopClipRect();
        dl->AddRect(P(GX0, GY0), P(GX1, GY1), kColCardEdge, 0.0f, 0, 1.0f * s);

        // Click on the graph selects the nearest filter (within a few pixels).
        ImGui::SetCursorScreenPos(P(GX0, GY0));
        ImGui::InvisibleButton("##graphpick", ImVec2((GX1 - GX0) * s, (GY1 - GY0) * s));
        if (ImGui::IsItemClicked() && table.count > 0)
        {
            const float mx = (ImGui::GetIO().MousePos.x - org.x) / s;
            int best = -1; float bestDx = 14.0f;
            for (int i = 0; i < table.count; ++i)
            {
                const float dx = std::fabs(freqX(table.f[i].freqHz) - mx);
                if (dx < bestDx) { bestDx = dx; best = i; }
            }
            if (best >= 0) selected = best;
        }
    }

    void drawMeter(ImDrawList* dl, const char* id, duskdaf::LedLadder& meter,
                   float x0, float y0, float x1, float y1, const char* label)
    {
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s, (y1 - y0) * s));
        if (ImGui::IsItemClicked())
            meter.reset(meter.displayDb(0) < meter.displayDb(1) ? meter.displayDb(1) : meter.displayDb(0));
        dl->AddRectFilled(P(x0 - 2, y0 - 2), P(x1 + 2, y1 + 2), kColGraphBg, 2.0f * s);
        meter.draw(panel, dl, x0, y0, x1, y1, meterStyle);
        text(dl, 0.5f * (x0 + x1), y1 + 5.0f, 8.5f, kColWhiteDim, label, 0, true);
        char pk[16];
        std::snprintf(pk, sizeof(pk), "%.0f", (double)std::max(meter.peakDb(), kRtaMinDb));
        regularText(dl, 0.5f * (x0 + x1), y0 - 9.0f, 8.0f, kColWhiteDim, pk, 0);
    }

    void drawRta(ImDrawList* dl)
    {
        const int cols = (int)rta.size();
        if (cols < 2 || !engineLive) return;
        std::vector<ImVec2>& pts = ptsScratch;
        pts.clear();
        pts.reserve((size_t)cols);
        for (int c = 0; c < cols; ++c)
            pts.push_back(P(GX0 + (float)c, rtaY(rta[(size_t)c])));
        const float baseY = P(GX0, GY1).y;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            dl->AddQuadFilled(pts[i], pts[i + 1], ImVec2(pts[i + 1].x, baseY), ImVec2(pts[i].x, baseY), kColCyanFill);
        dl->AddPolyline(pts.data(), (int)pts.size(), kColCyan, 0, 1.3f * s);
    }

    // Composite response of the on filters, filled red down from the 0 dB line,
    // one dot per filter at its notch, the selected one with a yellow cursor.
    // The curve is the engine's own responseDb(), evaluated once per change of
    // table, trims or sample rate rather than every frame.
    void drawFilterCurve(ImDrawList* dl)
    {
        const float gq = values[kParamGlobalQ], amp = values[kParamGlobalAmp];
        const double sr = getSampleRate() > 1.0 ? getSampleRate() : 48000.0;

        constexpr int N = 420;
        bool anyOn = false;
        for (int i = 0; i < table.count && !anyOn; ++i) anyOn = table.f[i].on;

        if (curveStamp != tableStamp || curveGq != gq || curveAmp != amp || curveSr != sr
            || curveDb.size() != (size_t)N)
        {
            curveStamp = tableStamp; curveGq = gq; curveAmp = amp; curveSr = sr;
            curveDb.resize((size_t)N);
            for (int i = 0; i < N; ++i)
            {
                const float f = axis.fromNorm((float)i / (float)(N - 1));
                curveDb[(size_t)i] = anyOn
                    ? (float)RingOutDSP::responseDb(table, gq, amp, sr, (double)f) : 0.0f;
            }
        }

        if (anyOn)
        {
            std::vector<ImVec2>& pts = ptsScratch;
            pts.clear();
            pts.reserve(N);
            for (int i = 0; i < N; ++i)
            {
                const float lx = (float)i / (float)(N - 1);
                pts.push_back(P(GX0 + lx * (GX1 - GX0), curveY(curveDb[(size_t)i])));
            }
            const float topY = P(GX0, GY0).y;
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                if (pts[i].y > topY + 0.5f || pts[i + 1].y > topY + 0.5f)
                    dl->AddQuadFilled(ImVec2(pts[i].x, topY), ImVec2(pts[i + 1].x, topY), pts[i + 1], pts[i], kColRedFill);
            dl->AddPolyline(pts.data(), (int)pts.size(), kColRed, 0, 1.6f * s);
        }

        // Selected filter: cursor line first so the dots sit on top.
        if (selected >= 0 && selected < table.count)
        {
            const float x = freqX(table.f[selected].freqHz);
            dl->AddLine(P(x, GY0), P(x, GY1), kColYellow, 1.4f * s);
        }
        for (int i = 0; i < table.count; ++i)
        {
            const ro::Filter& f = table.f[i];
            const float x = freqX(f.freqHz);
            const float db = f.on ? RingOutDSP::effectiveCutDb(f.cutDb, amp) : 0.0f;
            const ImVec2 c = P(x, curveY(db));
            const bool sel = i == selected;
            if (f.on)
            {
                dl->AddCircleFilled(c, 4.2f * s, sel ? kColYellow : kColCyan, 16);
                dl->AddCircle(c, 4.2f * s, IM_COL32(0, 0, 0, 200), 16, 1.0f * s);
            }
            else
            {
                dl->AddCircle(c, 4.2f * s, sel ? kColYellow : kColCyanDim, 16, 1.4f * s);
            }
        }
    }

    //--- toolbar -----------------------------------------------------------------
    void drawToolbar(ImDrawList* dl)
    {
        constexpr float y0 = 362.0f, y1 = 388.0f;
        const bool live = engineLive;

        text(dl, 30, 369.5f, 10.0f, kColWhiteDim, "SENSE", -1, true);
        const bool high = values[kParamSense] >= 0.5f;
        if (button(dl, "##senselow", 70, y0, 124, y1, "LOW", !high, kColCyan))
            setP(kParamSense, 0.0f);
        if (button(dl, "##sensehigh", 128, y0, 182, y1, "HIGH", high, kColCyan))
            setP(kParamSense, 1.0f);

        // SETUP: a toggle that arms the engine for a minute. Arming also puts
        // GLOBAL Q and AMP back to their defaults, as the reference does. Those
        // are editor edits the host sees; the plugin itself never rewrites one
        // parameter because another moved (that fails AU validation).
        const bool setupOn = live ? status.setupActive : values[kParamSetup] >= 0.5f;
        if (button(dl, "##setup", 230, y0, 430, y1, setupOn ? "SETUP  ON" : "SETUP", setupOn, kColRed, true, 12.0f))
        {
            if (setupOn)
                setP(kParamSetup, 0.0f);
            else
            {
                if (values[kParamSetup] >= 0.5f)
                    setP(kParamSetup, 0.0f);          // stale 'on' with the engine idle: re-edge
                setP(kParamGlobalQ, ro::kGlobalQDefault);
                setP(kParamGlobalAmp, ro::kGlobalAmpDefault);
                setP(kParamSetup, 1.0f);
            }
        }

        // ADD: momentary. Held = searching; the engine stops itself after one
        // filter and the button turns green until it is released.
        bool addHeld = false;
        const bool addLit = live ? (status.addSearching || status.addSatisfied) : values[kParamAdd] >= 0.5f;
        const ImU32 addCol = (live && status.addSatisfied) ? kColGreen : IM_COL32(240, 150, 40, 255);
        button(dl, "##add", 436, y0, 506, y1, "ADD", addLit, addCol, true, 11.0f, &addHeld);
        if (addHeld != addHeldLocal)
        {
            addHeldLocal = addHeld;
            setP(kParamAdd, addHeld ? 1.0f : 0.0f);
        }

        if (button(dl, "##reset", 530, y0, 600, y1, "RESET", false, kColRed))
        {
            setP(kParamReset, 1.0f);
            table.clear();
            selected = -1;
            syncPresetSelection();
        }

        // Status read-out.
        char st[64] = "";
        if (live && status.setupActive)
        {
            const int secs = (int)std::ceil(status.setupRemainingSeconds);
            std::snprintf(st, sizeof(st), "LISTENING  %d:%02d", secs / 60, secs % 60);
        }
        else if (live && status.addSearching)
            std::snprintf(st, sizeof(st), "ADD: LISTENING");
        else if (live && status.addSatisfied)
            std::snprintf(st, sizeof(st), "ADD: FILTER PLACED");
        else if (table.count > 0)
            std::snprintf(st, sizeof(st), "%d FILTER%s", table.count, table.count == 1 ? "" : "S");
        else
            std::snprintf(st, sizeof(st), "NO FILTERS");
        regularText(dl, 722, 368.0f, 12.0f, (live && (status.setupActive || status.addSearching)) ? kColRed : kColWhiteDim, st, 0);

        // Filter navigation: clamped, never wrapping; from no selection both land on 1.
        if (chevron(dl, "##prevfilter", 867, 375, 13.0f, true) && table.count > 0)
            selected = selected < 0 ? 0 : std::max(0, selected - 1);
        if (chevron(dl, "##nextfilter", 915, 375, 13.0f, false) && table.count > 0)
            selected = selected < 0 ? 0 : std::min(table.count - 1, selected + 1);
    }

    //--- filter card -----------------------------------------------------------------
    void drawFilterCard(ImDrawList* dl)
    {
        card(dl, 20, 400, 940, 578);
        dl->AddLine(P(640, 410), P(640, 568), IM_COL32(20, 20, 22, 255), 1.4f * s);

        // Indicators 1..20 and NEW.
        text(dl, 330, 405, 11.0f, kColWhiteDim, "FILTERS", 0, true);
        const bool haveSel = selected >= 0 && selected < table.count;
        duskdaf::DuskPanel::LedStyle ledOn;
        ledOn.onColor = kColCyan; ledOn.glowColor = IM_COL32(40, 220, 235, 70);
        ledOn.offColor = IM_COL32(32, 44, 46, 255);
        for (int i = 0; i < ro::kMaxFilters; ++i)
        {
            const float cx = 56.0f + (float)i * 27.0f, cy = 432.0f;
            const bool exists = i < table.count;
            const bool lit = exists && table.f[i].on;
            char id[24]; std::snprintf(id, sizeof(id), "##led%d", i);
            ImGui::SetCursorScreenPos(P(cx - 11.0f, cy - 11.0f));
            ImGui::InvisibleButton(id, ImVec2(22.0f * s, 22.0f * s));
            if (exists && ImGui::IsItemClicked())
                selected = i;
            if (exists && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                ro::Filter f = table.f[i];
                f.on = !f.on;
                selected = i;
                setSelectedFilter(f);
            }
            panel.led(dl, cx, cy, lit, 6.5f, ledOn);
            if (exists && !lit)
                dl->AddCircle(P(cx, cy), 6.5f * s, kColCyanDim, 20, 1.2f * s);  // exists, switched off
            if (i == selected)
                dl->AddCircle(P(cx, cy), 9.5f * s, kColYellow, 24, 1.8f * s);
            char num[4]; std::snprintf(num, sizeof(num), "%d", i + 1);
            regularText(dl, cx, 444.0f, 8.0f, i == selected ? kColYellow : kColWhiteDim, num, 0);
        }
        if (roundButton(dl, "##new", 604, 432, 11.0f, "+", false, kColCyan, table.count < ro::kMaxFilters, 14.0f))
        {
            ro::EditCommand c;
            c.kind = ro::EditCommand::kAdd;
            c.filter = ro::defaultFilter();
            sendEdit(c);
        }
        regularText(dl, 604, 446.0f, 8.0f, kColWhiteDim, "NEW", 0);

        // Selected filter controls.
        const ro::Filter cur = haveSel ? table.f[selected] : ro::Filter();
        if (roundButton(dl, "##on", 72, 490, 16.0f, "ON", haveSel && cur.on, kColCyan, haveSel))
        {
            ro::Filter f = cur; f.on = !f.on; setSelectedFilter(f);
        }

        duskdaf::StepperBoxRule cutRule, freqRule, qRule;
        cutRule.step = [](float v, int d, void*) { return ro::stepCut(v, d); };
        cutRule.snap = [](float v, void*) { return ro::snapCut(v); };
        cutRule.minV = ro::kCutMin; cutRule.maxV = ro::kCutMax;
        freqRule.step = [](float v, int d, void*) { return ro::stepFreq(v, d); };
        freqRule.snap = [](float v, void*) { return ro::snapFreq(v); };
        freqRule.minV = ro::kFreqMin; freqRule.maxV = ro::kFreqMax;
        qRule.step = [](float v, int d, void*) { return ro::stepQ(v, d); };
        qRule.snap = [](float v, void*) { return ro::snapQ(v); };
        qRule.minV = ro::kQMin; qRule.maxV = ro::kQMax;

        float cut = cur.cutDb, freq = cur.freqHz, q = cur.q;
        constexpr float by0 = 476.0f, by1 = 504.0f;
        if (duskdaf::stepperBox(panel, dl, "cut", 104, by0, 208, by1, cut, cutRule, "%.1f", haveSel))
        { ro::Filter f = cur; f.cutDb = cut; setSelectedFilter(f); }
        // A tenth of a hertz below 1 kHz (a detected 66.3 Hz reads as such), whole
        // hertz above where the control steps by 100.
        if (duskdaf::stepperBox(panel, dl, "freq", 222, by0, 326, by1, freq, freqRule,
                                freq < 1000.0f ? "%.1f" : "%.0f", haveSel))
        { ro::Filter f = cur; f.freqHz = freq; setSelectedFilter(f); }
        if (duskdaf::stepperBox(panel, dl, "q", 340, by0, 444, by1, q, qRule, "%.1f", haveSel))
        { ro::Filter f = cur; f.q = q; setSelectedFilter(f); }
        text(dl, 156, 509, 9.0f, kColWhiteDim, "CUT (dB)", 0, true);
        text(dl, 274, 509, 9.0f, kColWhiteDim, "FREQ (Hz)", 0, true);
        text(dl, 392, 509, 9.0f, kColWhiteDim, "Q", 0, true);

        if (roundButton(dl, "##del", 480, 490, 16.0f, "DEL", false, kColRed, haveSel))
        {
            ro::EditCommand c;
            c.kind = ro::EditCommand::kDelete;
            c.slot = selected;
            sendEdit(c);
        }

        if (haveSel)
        {
            char info[48];
            std::snprintf(info, sizeof(info), "FILTER %d", selected + 1);
            regularText(dl, 72, 532.0f, 9.0f, kColYellow, info, 0);
        }
        regularText(dl, 300, 540.0f, 9.0f, IM_COL32(120, 120, 118, 255),
                    "Double-click an indicator to switch a filter off.  Click the graph to select.", 0);

        drawGlobals(dl);
    }

    // GLOBAL Q / AMP (+ LINK) and GAIN OUT. A global knob off its default wears a
    // red rim, as the reference does.
    void drawGlobals(ImDrawList* dl)
    {
        text(dl, 740, 405, 11.0f, kColWhiteDim, "GLOBAL", 0, true);
        text(dl, 890, 405, 11.0f, kColWhiteDim, "GAIN OUT", 0, true);

        text(dl, 700, 440, 10.0f, kColWhite, "Q", 0, true);
        text(dl, 780, 440, 10.0f, kColWhite, "AMP", 0, true);

        const float prevAmp = values[kParamGlobalAmp];
        const float prevGain = values[kParamGainOut];

        if (std::fabs(values[kParamGlobalQ] - ro::kGlobalQDefault) > 1.0e-4f)
            dl->AddCircle(P(700, 480), 25.0f * s, kColRed, 48, 2.0f * s);
        panel.knob("##globalq", kParamGlobalQ, ro::kGlobalQMin, ro::kGlobalQMax, 700, 480, 20.0f,
                   values[kParamGlobalQ], ro::kGlobalQDefault, false, true, "%.1f", "", 0, false,
                   true, nullptr, false, 1.0f, 0.0f, "Global Q", true);

        if (std::fabs(values[kParamGlobalAmp] - ro::kGlobalAmpDefault) > 1.0e-4f)
            dl->AddCircle(P(780, 480), 25.0f * s, kColRed, 48, 2.0f * s);
        const bool ampChanged = panel.knob("##globalamp", kParamGlobalAmp, ro::kGlobalAmpMin, ro::kGlobalAmpMax,
                                           780, 480, 20.0f, values[kParamGlobalAmp], ro::kGlobalAmpDefault,
                                           false, true, "%+.1f", " dB", 0, false, true, nullptr, false,
                                           1.0f, 0.0f, "Global Amp", true);

        const bool link = values[kParamLink] >= 0.5f;
        if (roundButton(dl, "##link", 740, 538, 9.0f, "", link, kColCyan))
            setP(kParamLink, link ? 0.0f : 1.0f);
        text(dl, 740, 552, 8.5f, link ? kColCyan : kColWhiteDim, "LINK", 0, true);

        const bool gainChanged = panel.knob("##gainout", kParamGainOut, ro::kGainOutMin, ro::kGainOutMax,
                                            890, 480, 24.0f, values[kParamGainOut], ro::kGainOutDefault,
                                            false, true, "%+.1f", " dB", IM_COL32(120, 36, 30, 255), false,
                                            true, nullptr, false, 1.0f, 0.0f, "Gain Out", true);

        // LINK: AMP and GAIN OUT move together in opposite directions, so a few dB
        // more output gain means the same few dB deeper on every filter. An
        // editor gesture coupling, like the reference's: both parameters reach
        // the host as edits. Host automation of one knob moves only that knob
        // (see RingOutPlugin.cpp, kParamLink, for why).
        if (link)
        {
            if (gainChanged)
                setP(kParamGlobalAmp, prevAmp - (values[kParamGainOut] - prevGain));
            else if (ampChanged)
                setP(kParamGainOut, prevGain - (values[kParamGlobalAmp] - prevAmp));
        }
    }

    void drawCountdown(ImDrawList* dl)
    {
        constexpr float x0 = 20.0f, x1 = 940.0f, y0 = 584.0f, y1 = 594.0f;
        dl->AddRectFilled(P(x0, y0), P(x1, y1), IM_COL32(12, 12, 13, 255), 2.0f * s);
        if (engineLive && status.setupActive)
        {
            const float frac = std::clamp(status.setupRemainingSeconds / ro::kSetupSeconds, 0.0f, 1.0f);
            dl->AddRectFilled(P(x0 + 1.0f, y0 + 1.0f), P(x0 + 1.0f + (x1 - x0 - 2.0f) * frac, y1 - 1.0f),
                              kColRed, 1.5f * s);
        }
        dl->AddRect(P(x0, y0), P(x1, y1), kColCardEdge, 2.0f * s, 0, 1.0f * s);
    }

    //--- parameter plumbing ---------------------------------------------------------
    void setP(uint32_t param, float value)
    {
        if (param >= kParamCount || !std::isfinite(value))
            return;
        value = roNormalizeParamValue(param, value);
        values[param] = value;
        editParameter(param, true);
        setParameterValue(param, value);
        editParameter(param, false);
        if (roIsPresetParam(param))
            syncPresetSelection();
    }

    //--- presets ---------------------------------------------------------------------
    // Factory: "Default" = every control at its default and no filters.
    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kRoNumFactoryPresets) return;
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (roIsPresetParam(i))
                setP(i, kRoParams[i].def);
        setState("filters", "");
        table.clear();
        ++tableStamp;
        selected = -1;
        currentPreset = idx;
        currentUserName.clear();
        currentUserPath.clear();
    }

    void initDefaults() { applyPreset(0); }

    void stepPreset(int dir)
    {
        int i = currentPreset < 0 ? (dir < 0 ? 0 : -1) : currentPreset;
        i += dir;
        i = std::clamp(i, 0, kRoNumFactoryPresets - 1);
        applyPreset(i);
    }

    struct UserPreset
    {
        std::string name, path;
        float vals[kParamCount];
        ro::FilterTable filters;
    };

    std::filesystem::path configDir() const { return duskdaf::userPresetDirectory("RingOut"); }

    static bool parsePresetValue(const std::string& line, std::size_t valueStart, uint32_t param, float& out)
    {
        std::istringstream field(line.substr(valueStart));
        field.imbue(std::locale::classic());
        double d = 0.0;
        field >> d;
        if (field.fail() || !std::isfinite(d)) return false;
        char trailing = '\0';
        if (field >> trailing) return false;
        out = roNormalizeParamValue(param, (float)d);
        return true;
    }

    static bool readUserPresetFile(const std::filesystem::path& path, UserPreset& up)
    {
        for (uint32_t i = 0; i < kParamCount; ++i) up.vals[i] = kRoParams[i].def;
        up.filters.clear();
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            if (key == "name") { up.name = line.substr(eq + 1); continue; }
            if (key == "filters")
            {
                ro::FilterTable t;
                if (ro::parseTable(line.c_str() + eq + 1, t)) up.filters = t;
                continue;
            }
            for (uint32_t i = 0; i < kParamCount; ++i)
                if (roIsPresetParam(i) && key == kRoParams[i].id)
                {
                    float v = 0.0f;
                    if (parsePresetValue(line, eq + 1, i, v)) up.vals[i] = v;
                    break;
                }
        }
        return true;
    }

    void scanUserPresets()
    {
        duskdaf::scanUserPresets(configDir(), ".ropreset", userPresets,
                                 [](const std::filesystem::path& path, UserPreset& preset)
                                 { return readUserPresetFile(path, preset); });
    }

    bool saveUserPreset(const char* rawName)
    {
        char filters[ro::kTableTextCapacity];
        if (!ro::serializeTable(table, filters, (int)sizeof(filters)))
            return false;
        const auto saved = duskdaf::writeUserPreset(
            configDir(), ".ropreset", rawName,
            [this, &filters](std::ostream& output)
            {
                for (uint32_t i = 0; i < kParamCount; ++i)
                    if (roIsPresetParam(i))
                        output << kRoParams[i].id << "=" << values[i] << '\n';
                output << "filters=" << filters << '\n';
            }, true);
        if (!saved) return false;
        scanUserPresets();
        currentPreset = -1;
        currentUserName = saved.name;
        currentUserPath = saved.path;
        return true;
    }

    void loadUserPreset(const UserPreset& up)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (roIsPresetParam(i))
                setP(i, up.vals[i]);
        char filters[ro::kTableTextCapacity];
        if (ro::serializeTable(up.filters, filters, (int)sizeof(filters)))
        {
            setState("filters", filters);
            table = up.filters;
            ++tableStamp;
        }
        clampSelection();
        currentPreset = -1;
        currentUserName = up.name;
        currentUserPath = up.path;
    }

    bool paramMatches(uint32_t id, float v) const
    {
        const RoParam& d = kRoParams[id];
        const float tol = std::max(1.0e-3f, (d.max - d.min) * 1.0e-4f);
        return std::fabs(values[id] - v) <= tol;
    }

    void syncPresetSelection()
    {
        bool isDefault = table.count == 0;
        for (uint32_t i = 0; i < kParamCount && isDefault; ++i)
            if (roIsPresetParam(i) && !paramMatches(i, kRoParams[i].def))
                isDefault = false;
        if (isDefault)
        {
            currentPreset = 0;
            currentUserName.clear();
            currentUserPath.clear();
            return;
        }
        const auto matches = [this](const UserPreset& up)
        {
            for (uint32_t i = 0; i < kParamCount; ++i)
                if (roIsPresetParam(i) && !paramMatches(i, up.vals[i])) return false;
            return ro::tablesEqual(up.filters, table);
        };
        // Keep the loaded file's identity when two files hold the same values.
        if (!currentUserPath.empty())
            for (const UserPreset& up : userPresets)
                if (up.path == currentUserPath && matches(up))
                {
                    currentPreset = -1;
                    return;
                }
        for (const UserPreset& up : userPresets)
            if (matches(up))
            {
                currentPreset = -1;
                currentUserName = up.name;
                currentUserPath = up.path;
                return;
            }
        currentPreset = -1;
        currentUserName.clear();
        currentUserPath.clear();
    }

    //--- state -------------------------------------------------------------------------
    float  s = 1.0f;
    ImVec2 org = ImVec2(0, 0);
    duskdaf::DuskPanel panel;
    duskdaf::CrispFontSet labelFonts, regularFonts;
    ImFont* labelFont = nullptr;
    duskdaf::DuskImGuiTextInputFocus textInputFocus;
    duskdaf::SupportersOverlay supportersOverlay;
    bool showSupporters = false;
    bool gripCursorSet = false;

    float values[kParamCount] = {};

    // Mirror of the engine.
    bool engineLive = false;
    RingOutDSP::Status status;
    ro::FilterTable table;
    unsigned tableVersionSeen = 0;
    unsigned tableStamp = 0;            // bumps on every change of the local mirror
    int selected = -1;
    bool addHeldLocal = false;

    // Filter curve cache (drawFilterCurve).
    std::vector<float> curveDb;
    unsigned curveStamp = ~0u;
    float curveGq = 0.0f, curveAmp = 0.0f;
    double curveSr = 0.0;

    duskdaf::LogFreqAxis axis { ro::kFreqMin, ro::kFreqMax };
    duskaudio::RingOutSpectrumFrame spectrum;
    unsigned spectrumSeqSeen = 0;
    std::vector<float> rta, rtaScratch;
    std::vector<ImVec2> ptsScratch;

    duskdaf::LedLadder inMeter, outMeter;
    duskdaf::LedLadderStyle meterStyle = [] {
        duskdaf::LedLadderStyle st;
        st.segments = 40; st.gap = 1.5f; st.minDb = -80.0f; st.maxDb = 0.0f;
        st.yellowFrom = 0.75f; st.redFrom = 0.9f; st.rounding = 1.0f;
        return st;
    }();

    // Presets.
    int currentPreset = 0;
    std::string currentUserName, currentUserPath;
    std::vector<UserPreset> userPresets;
    char saveBuf[64] = {};
    bool saveFailed = false;

    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingOutUI)
};

UI* createUI()
{
    return new RingOutUI();
}

END_NAMESPACE_DAF
