// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutUI.cpp — Dear ImGui UI for Ring Out, the feedback eliminator.
//
// Layout (1040 x 640 design space, uniformly scaled):
//   header row      the fleet-standard nameplate / preset / brand band
//   graph card      log-frequency graph: the analyser, the notch response and a
//                   numbered node per notch that drags (FREQ across, CUT up and
//                   down, the wheel for Q, double-click for on/off)
//   notch bank      twenty strips, one per notch, each with its own CUT fader;
//                   the selected notch's FREQ / CUT / Q steppers beneath them
//   detect card     LEARN with its countdown ring, CATCH, SENSITIVITY, status
//   output card     Q SCALE and CUT TRIM for every notch, LINK, OUTPUT, meters
//
// The filter table is read straight off the DSP every frame (direct access);
// edits go back as single "edit" state commands, so the engine's own additions
// and the user's never fight over a stale copy.

#include "DafUI.hpp"
#include "RingOutAccess.hpp"
#include "RingOutDSP.hpp"
#include "RingOutFilterTable.hpp"
#include "RingOutFilterEditor.hpp"
#include "RingOutParams.hpp"
#include "RingOutPreset.hpp"
#include "RingOutVersion.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskKnobRing.hpp"
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
#include <string>
#include <vector>

START_NAMESPACE_DAF

namespace
{
    using duskaudio::RingOutDSP;
    namespace ro = duskaudio::ringout;

    constexpr float kDesignW = 1040.0f;
    constexpr float kDesignH = 640.0f;
    constexpr float kPi = duskdaf::DuskPanel::kPi;

    // Palette: the graphite chassis of the other Dusk DAF effects with one warm
    // accent for what the plugin does (amber: the notches, an armed LEARN, a
    // caught ring), warm silver for what the microphone hears, and white for
    // the selection.
    constexpr ImU32 kColHeader    = IM_COL32(14, 14, 15, 255);
    constexpr ImU32 kColCard      = IM_COL32(27, 27, 29, 255);
    constexpr ImU32 kColCardEdge  = IM_COL32(58, 57, 55, 255);
    constexpr ImU32 kColInset     = IM_COL32(14, 14, 15, 255);   // graph, tracks, status
    constexpr ImU32 kColGrid      = IM_COL32(40, 40, 42, 255);
    constexpr ImU32 kColGridMinor = IM_COL32(26, 26, 28, 255);
    constexpr ImU32 kColWhite     = IM_COL32(241, 238, 226, 255);
    constexpr ImU32 kColWhiteDim  = IM_COL32(194, 190, 180, 255);
    constexpr ImU32 kColLabel     = IM_COL32(164, 160, 151, 255);
    constexpr ImU32 kColSilver    = IM_COL32(208, 202, 190, 215);
    constexpr ImU32 kColSilverDim = IM_COL32(164, 160, 151, 255);
    constexpr ImU32 kColSilverFill = IM_COL32(190, 184, 170, 30);
    constexpr ImU32 kColAmber     = IM_COL32(255, 158, 48, 255);
    constexpr ImU32 kColAmberDim  = IM_COL32(255, 158, 48, 130);
    constexpr ImU32 kColAmberFill = IM_COL32(255, 150, 40, 56);
    constexpr ImU32 kColAmberDeep = IM_COL32(70, 44, 16, 255);    // armed LEARN body
    constexpr ImU32 kColInk       = IM_COL32(24, 18, 10, 255);    // text on amber
    constexpr ImU32 kColGreen     = IM_COL32(110, 204, 120, 255);
    constexpr ImU32 kColWarn      = IM_COL32(255, 92, 64, 255);   // RING NOT COVERED
    constexpr ImU32 kColBtn       = IM_COL32(40, 40, 42, 255);
    constexpr ImU32 kColBtnHot    = IM_COL32(56, 55, 53, 255);
    constexpr ImU32 kColBtnEdge   = IM_COL32(84, 82, 78, 255);
    constexpr ImU32 kColTrack     = IM_COL32(50, 49, 47, 255);

    // Graph plot. Both axes leave room for their units above the 0 dB line
    // and for frequency labels below the traces and numbered nodes.
    constexpr float GX0 = 28.0f, GX1 = 1012.0f, GY0 = 64.0f, GY1 = 310.0f;
    // Side gutters keep the end nodes and the traces clear of the dB labels.
    constexpr float FX0 = GX0 + 36.0f, FX1 = GX1 - 36.0f;
    constexpr float kNotchY0 = GY0 + 30.0f;
    constexpr float kNotchY1 = GY1 - 28.0f;
    constexpr float kRtaMinDb = -80.0f;
    constexpr int   kCurvePoints = 480;
    constexpr float kNodeR = 9.0f;
    constexpr float kNodeHitR = 12.0f;

    // Notch bank: one strip per notch slot.
    constexpr float kBankX0 = 54.0f, kBankX1 = 700.0f;
    constexpr float kStripW = (kBankX1 - kBankX0) / (float)ro::kMaxFilters;
    constexpr float kStripY0 = 360.0f, kStripY1 = 554.0f;
    constexpr float kBadgeY = 374.0f;
    constexpr float kFaderY0 = 408.0f, kFaderY1 = 518.0f;

    constexpr float kRippleSeconds = 0.9f;
    constexpr float kKnobRingGap = 5.5f;       // knob body to its value ring

    // The knob ring's angles: DuskPanel::knobAngle measures from 12 o'clock,
    // ImDrawList::PathArcTo from +x.
    float arcAngle(float t) { return duskdaf::DuskPanel::knobAngle(t) - 0.5f * kPi; }
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
            { 11.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 24.0f, 28.0f, 32.0f, 48.0f };
        static constexpr float kRegularSizes[] =
            { 11.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 24.0f, 28.0f, 32.0f, 48.0f };

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

        rta.assign((size_t)(FX1 - FX0) + 1, kRtaMinDb);
        scanUserPresets();
    }

    // An editor closed while a LINK drag holds the mouse never sees the button
    // come up, so end the follower's host gesture here; otherwise the host
    // leaves CUT TRIM (or OUTPUT) in touch / write.
    ~RingOutUI() override
    {
        if (linkFollowerOpen >= 0)
            editParameter((uint32_t)linkFollowerOpen, false);
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

        // ImGui owns the text in tooltips, menus and inline editors. Give those
        // the same readable, scaled body face as the drawn panel labels.
        ImFont* bodyFont = regularFonts.pick(14.0f * s);
        if (bodyFont != nullptr) ImGui::PushFont(bodyFont);
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, kColInset);
        ImGui::PushStyleColor(ImGuiCol_Border, kColCardEdge);
        ImGui::PushStyleColor(ImGuiCol_Header, kColAmberDeep);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(118, 74, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(100, 62, 22, 255));
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
        ImGui::PopStyleVar(2);
        // Zero padding belongs only to the absolute-positioned root panel;
        // popovers need room around their text and controls.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 8.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, 4.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 6.0f * s));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 8, 8, 255));
        drawChassis(dl);

        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);
        drawGraphCard(dl);
        drawNotchBank(dl);
        drawDetectCard(dl);
        drawOutputCard(dl);
        if (values[kParamBypass] >= 0.5f)
        {
            dl->AddRectFilled(P(0, 50), P(kDesignW, kDesignH), IM_COL32(0, 0, 0, 120));
            duskdaf::spacedText(panel, dl, 0.5f * kDesignW, 172.0f, 22.0f,
                                IM_COL32(230, 226, 214, 210), "BYPASSED", 0, 0.2f);
        }
        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdaf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
                                           "Ring Out", RO_VERSION_STRING, &supportersOverlay);

        const duskdaf::ResizeGripState grip = panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH);

        ImGui::PopStyleVar(3);
        ImGui::End();
        ImGui::PopStyleColor(6);
        if (bodyFont != nullptr) ImGui::PopFont();
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
    RingOutDSP* dsp() const
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
        if (rippleSecondsLeft > 0.0f)
            rippleSecondsLeft -= std::clamp(dt, 0.0f, 0.1f);

        RingOutDSP* const d = dsp();
        if (d == nullptr)
        {
            inMeter.update(-100.0f, -100.0f, dt, meterStyle);
            outMeter.update(-100.0f, -100.0f, dt, meterStyle);
            return;
        }
        const bool firstPull = !engineLive;
        engineLive = true;
        status = d->status();
        // An editor opened on a running instance has not seen the engagements
        // that happened while it was closed; it must not ripple the last one.
        if (firstPull)
            engagementsSeen = status.engagementCount;
        if (status.tableVersion != tableVersionSeen)
        {
            tableVersionSeen = status.tableVersion;
            d->getTable(table);
            ++tableStamp;
            if (pendingSelectPulls > 0)
            {
                // The filter NEW just added is the last row equal to it. The
                // add travels through the host and may not have landed yet when
                // an engine change bumps the version, so keep looking for a few
                // pulls rather than giving up on the first one.
                bool found = false;
                for (int i = table.count - 1; i >= 0 && !found; --i)
                    if (ro::filtersEqual(table.f[i], pendingSelect)) { selected = i; found = true; }
                pendingSelectPulls = found ? 0 : pendingSelectPulls - 1;
            }
            clampSelection();
            syncPresetSelection();
        }
        // Ripple the notch the engine just placed or deepened. The table pull
        // above has already brought that row in.
        if (status.engagementCount != engagementsSeen)
        {
            engagementsSeen = status.engagementCount;
            const int row = status.lastEngagedRow;
            if (row >= 0 && row < table.count)
            {
                rippleRow = row;
                rippleRowId = table.id[row];
                rippleHz = table.f[row].freqHz;
                rippleSecondsLeft = kRippleSeconds;
            }
        }

        inMeter.update(d->inputPeakDb(0), d->inputPeakDb(1), dt, meterStyle);
        outMeter.update(d->outputPeakDb(0), d->outputPeakDb(1), dt, meterStyle);

        // Spectrum -> per-pixel-column dB, rebuilt only when a new frame arrived;
        // the RTA ballistics (fast up, 40 dB/s down) run every UI frame.
        const int cols = (int)rta.size();
        const unsigned seq = d->spectrumSequence();
        if (seq != spectrumSeqSeen && d->copySpectrum(spectrum))
        {
            spectrumSeqSeen = seq;
            std::vector<float>& colMax = rtaTarget;
            colMax.assign((size_t)cols, -200.0f);
            for (int k = 1; k < spectrum.bins; ++k)
            {
                const float f = (float)k * spectrum.binHz;
                if (f < axis.minHz() || f > axis.maxHz()) continue;
                const int col = (int)(axis.toNorm(f) * (float)(cols - 1) + 0.5f);
                if (col < 0 || col >= cols) continue;
                colMax[(size_t)col] = std::max(colMax[(size_t)col], spectrum.db[k]);
            }
            // Fill columns no bin landed in (low end at a coarse bin width) from
            // their neighbours so the trace is continuous: the columns before
            // the first in-band bin take that bin's level, the rest carry the
            // last level forward.
            float last = kRtaMinDb;
            for (int c = 0; c < cols; ++c)
                if (colMax[(size_t)c] > -199.0f) { last = colMax[(size_t)c]; break; }
            for (int c = 0; c < cols; ++c)
            {
                if (colMax[(size_t)c] > -199.0f) last = colMax[(size_t)c];
                else colMax[(size_t)c] = last;
            }
        }
        if (rtaTarget.size() == (size_t)cols)
        {
            const float fall = 40.0f * std::clamp(dt, 0.0f, 0.1f);
            for (int c = 0; c < cols; ++c)
            {
                const float target = std::max(kRtaMinDb, rtaTarget[(size_t)c]);
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
        // Apply to the mirror what the plugin will apply: the command as parsed
        // back from its own text, so both sides snap identically and the
        // added filter can be found again by value.
        ro::EditCommand asSent;
        if (!ro::parseEditCommand(buf, asSent))
            return;
        const int touched = ro::applyEditCommand(table, asSent);
        ++tableStamp;
        setState("edit", buf);
        if (asSent.kind == ro::EditCommand::kAdd && touched >= 0)
        {
            // The engine may have appended a filter of its own since the last
            // pull, so the row index is provisional: when the engine's table
            // arrives, the selection is re-resolved to the filter just added.
            selected = touched;
            pendingSelect = asSent.filter;
            pendingSelectPulls = 30;
        }
        else if (c.kind == ro::EditCommand::kDelete)
            selected = table.count == 0 ? -1 : std::min(c.slot, table.count - 1);
        else if (c.kind == ro::EditCommand::kClear)
            selected = -1;
        clampSelection();
        syncPresetSelection();
    }

    // LEARN / CATCH, over the same channel as the table edits.
    void sendEngineCommand(ro::EditCommand::Kind kind)
    {
        ro::EditCommand c;
        c.kind = kind;
        char buf[32];
        if (ro::formatEditCommand(c, buf, (int)sizeof(buf)))
            setState("edit", buf);
    }

    // Sends the row as edited AND as the mirror last saw it, so the plugin
    // writes only the field that changed: an engine deepen that landed since
    // the last pull survives a FREQ nudge.
    template <typename T>
    void setRowField(int row, T ro::Filter::* field, T value)
    {
        if (row < 0 || row >= table.count) return;
        sendEdit(roFilterFieldCommand(table, row, field, value));
    }

    template <typename T>
    void setSelectedFilter(T ro::Filter::* field, T value) { setRowField(selected, field, value); }

    void addNotch()
    {
        ro::EditCommand c;
        c.kind = ro::EditCommand::kAdd;
        c.filter = ro::defaultFilter();
        sendEdit(c);
    }

    void deleteSelected()
    {
        if (selected < 0 || selected >= table.count) return;
        ro::EditCommand c;
        c.kind = ro::EditCommand::kDelete;
        c.slot = selected;
        sendEdit(c);
    }

    // CLEAR goes through the edit channel like every other table edit; the
    // trigger parameter is for host automation and controller mapping. A
    // trigger written from here could be swallowed by a host that forwards
    // only control-port changes (an LV2 port already sitting at 1).
    void clearNotches()
    {
        ro::EditCommand c;
        c.kind = ro::EditCommand::kClear;
        sendEdit(c);
    }

    //--- helpers -----------------------------------------------------------------
    ImVec2 P(float x, float y) const { return ImVec2(org.x + x * s, org.y + y * s); }
    ImVec2 mouseDesign() const
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        return ImVec2((m.x - org.x) / s, (m.y - org.y) / s);
    }

    // Pointer motion this frame in design px. The activation frame measures
    // from the press, which can share a frame with motion before it.
    ImVec2 dragDelta() const
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 d = ImGui::IsItemActivated()
            ? ImVec2(io.MousePos.x - io.MouseClickedPos[0].x, io.MousePos.y - io.MouseClickedPos[0].y)
            : io.MouseDelta;
        return ImVec2(d.x / s, d.y / s);
    }

    // Whole wheel notches since the last call for the same target, so a
    // trackpad's fractional scroll steps once per notch's worth instead of once
    // per frame. A new target starts from zero rather than inheriting another
    // control's remainder. Targets: -1 - row for a graph node, row + 1 for a
    // bank fader.
    int takeWheelSteps(int target)
    {
        if (target != wheelTarget)
        {
            wheelTarget = target;
            wheelAccum = 0.0f;
        }
        wheelAccum += ImGui::GetIO().MouseWheel;
        const int steps = (int)wheelAccum;
        wheelAccum -= (float)steps;
        return steps;
    }

    static void tooltip(const char* txt)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", txt);
    }

    void text(ImDrawList* dl, float x, float y, float size, ImU32 col,
              const char* txt, int align, bool bold = false) const
    {
        panel.text(dl, x, y, size, col, txt, align, bold);
    }

    void spaced(ImDrawList* dl, float x, float y, float size, ImU32 col,
                const char* txt, int align, float tracking = 0.14f) const
    {
        duskdaf::spacedText(panel, dl, x, y, size, col, txt, align, tracking);
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

    float regularWidth(float size, const char* txt) const
    {
        ImFont* font = regularFonts.pick(size * s);
        if (font == nullptr) font = panel.pickFont(size * s);
        return font->CalcTextSizeA(size * s, FLT_MAX, 0.0f, txt).x / s;
    }

    float boldWidth(float size, const char* txt) const
    {
        return panel.pickFont(size * s)->CalcTextSizeA(size * s, FLT_MAX, 0.0f, txt).x / s;
    }

    static ImU32 fade(ImU32 c, float amount) { return duskdaf::scaleAlpha(c, amount); }

    // Fills between a trace (screen space, x ascending) and a horizontal line
    // as one strip of quads sharing their edges. A concave-polygon fill of the
    // same shape degenerates wherever the trace lies on the line (an unnotched
    // stretch at 0 dB, a silent analyser at its floor) and fills the wrong
    // region; quads need no triangulation, and shared vertices leave no seams.
    static void fillToLine(ImDrawList* dl, const ImVec2* pts, int n, float lineY, ImU32 col)
    {
        if (n < 2) return;
        dl->PrimReserve((n - 1) * 6, n * 2);
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        const unsigned base = dl->_VtxCurrentIdx;   // after PrimReserve, which may start a new vertex block
        for (int i = 0; i < n; ++i)
        {
            dl->PrimWriteVtx(ImVec2(pts[i].x, lineY), uv, col);
            dl->PrimWriteVtx(pts[i], uv, col);
        }
        for (int i = 0; i + 1 < n; ++i)
        {
            const unsigned a = base + 2u * (unsigned)i;
            dl->PrimWriteIdx((ImDrawIdx)a);
            dl->PrimWriteIdx((ImDrawIdx)(a + 1));
            dl->PrimWriteIdx((ImDrawIdx)(a + 3));
            dl->PrimWriteIdx((ImDrawIdx)a);
            dl->PrimWriteIdx((ImDrawIdx)(a + 3));
            dl->PrimWriteIdx((ImDrawIdx)(a + 2));
        }
    }

    void card(ImDrawList* dl, float x0, float y0, float x1, float y1) const
    {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kColCard, 6.0f * s);
        dl->AddRect(P(x0, y0), P(x1, y1), kColCardEdge, 6.0f * s, 0, 1.0f * s);
    }

    void sectionTitle(ImDrawList* dl, float x, float y, const char* title) const
    {
        spaced(dl, x, y, 13.0f, kColWhiteDim, title, -1, 0.12f);
    }

    // Rectangular button. `lit` paints it in litCol; returns clicked. `heldOut`
    // reports press-and-hold for momentary controls.
    bool button(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                const char* label, bool lit, ImU32 litCol, bool enabled = true,
                float textSize = 10.5f, bool* heldOut = nullptr, const char* tip = nullptr)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        if (!enabled) ImGui::BeginDisabled();
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = enabled && ImGui::IsItemHovered();
        const bool held = enabled && ImGui::IsItemActive();
        const bool clicked = enabled && ImGui::IsItemClicked();
        if (tip != nullptr && enabled) tooltip(tip);
        if (!enabled) ImGui::EndDisabled();
        if (heldOut != nullptr) *heldOut = held;

        const ImU32 bg = lit ? litCol : (hov || held ? kColBtnHot : kColBtn);
        dl->AddRectFilled(b0, b1, bg, 4.0f * s);
        dl->AddRect(b0, b1, lit ? fade(litCol, 0.7f) : kColBtnEdge, 4.0f * s, 0, 1.0f * s);
        const ImU32 ink = !enabled ? IM_COL32(96, 95, 92, 255)
                        : lit ? kColInk
                        : (hov ? kColWhite : kColWhiteDim);
        spaced(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - textSize) - 0.5f, textSize, ink, label, 0, 0.10f);
        return clicked;
    }

    // Two-position switch: a silver selection keeps detector sensitivity
    // distinct from the amber LEARN / CATCH actions.
    int segmentSwitch(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                      const char* left, const char* right, int index, const char* tip)
    {
        const float xm = 0.5f * (x0 + x1);
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kColInset, 4.0f * s);
        int clicked = -1;
        ImGui::PushID(id);
        for (int k = 0; k < 2; ++k)
        {
            const float sx0 = k == 0 ? x0 : xm, sx1 = k == 0 ? xm : x1;
            ImGui::SetCursorScreenPos(P(sx0, y0));
            ImGui::InvisibleButton(k == 0 ? "##a" : "##b", ImVec2((sx1 - sx0) * s, (y1 - y0) * s));
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) clicked = k;
            tooltip(tip);
            if (k == index)
                dl->AddRectFilled(P(sx0 + 2.0f, y0 + 2.0f), P(sx1 - 2.0f, y1 - 2.0f), kColWhiteDim, 3.0f * s);
            else if (hov)
                dl->AddRectFilled(P(sx0 + 2.0f, y0 + 2.0f), P(sx1 - 2.0f, y1 - 2.0f), kColBtnHot, 3.0f * s);
            const ImU32 ink = k == index ? kColInk : (hov ? kColWhite : kColWhiteDim);
            constexpr float kTxt = 12.0f;
            spaced(dl, 0.5f * (sx0 + sx1), 0.5f * (y0 + y1 - kTxt) - 0.5f, kTxt, ink,
                   k == 0 ? left : right, 0, 0.10f);
        }
        ImGui::PopID();
        dl->AddRect(P(x0, y0), P(x1, y1), kColBtnEdge, 4.0f * s, 0, 1.0f * s);
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
        constexpr float kTxt = 13.0f;
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
        dl->AddLine(P(40, 37), P(308, 37), fade(kColAmber, 0.8f), 1.2f * s);

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
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(118, 74, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(144, 92, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(100, 62, 22, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(66, 42, 16, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(118, 74, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(100, 62, 22, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kColWhite);
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(176, 116, 48, 255));
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
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(66, 42, 16, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(118, 74, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(100, 62, 22, 255));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, kColHeader);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kColHeader);
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 170));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f * s);
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
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(12);
    }

    //--- graph card ----------------------------------------------------------------
    float rtaY(float db) const
    {
        const float ny = std::clamp(-db / -kRtaMinDb, 0.0f, 1.0f);   // 0 dB top .. -80 bottom
        return kNotchY0 + ny * (kNotchY1 - kNotchY0);
    }
    // The notch axis runs 0 .. curveMinDb: -24 dB unless a notch (cut plus CUT
    // TRIM, or several notches stacked) goes deeper, then extended in 12 dB
    // steps to at most -48 dB (roNotchAxisFloorDb); deeper points clip at the
    // floor.
    float curveY(float db) const
    {
        const float ny = std::clamp(db / curveMinDb, 0.0f, 1.0f);
        return kNotchY0 + ny * (kNotchY1 - kNotchY0);
    }
    float freqX(float f) const { return FX0 + axis.toNorm(f) * (FX1 - FX0); }

    // A notch's node: at its effective depth when on, on the 0 dB line when off.
    ImVec2 nodePos(int row) const
    {
        const ro::Filter& f = table.f[row];
        const float db = f.on ? RingOutDSP::effectiveCutDb(f.cutDb, values[kParamGlobalAmp]) : 0.0f;
        return ImVec2(freqX(f.freqHz), curveY(db));
    }

    // The node under a design-space point; the selected one wins a tie, so a
    // notch stacked on another can always be dragged back out.
    int nodeAt(ImVec2 m) const
    {
        const auto dist2 = [&](int i) {
            const ImVec2 p = nodePos(i);
            return (p.x - m.x) * (p.x - m.x) + (p.y - m.y) * (p.y - m.y);
        };
        constexpr float kHit2 = kNodeHitR * kNodeHitR;
        if (selected >= 0 && selected < table.count && dist2(selected) <= kHit2)
            return selected;
        int best = -1;
        float bestD2 = kHit2;
        for (int i = 0; i < table.count; ++i)
        {
            const float d2 = dist2(i);
            if (d2 <= bestD2) { bestD2 = d2; best = i; }
        }
        return best;
    }

    // The notch whose frequency is within a few pixels of x, at any height: a
    // deep notch is easier to pick in its column than at its node.
    int notchInColumn(float x) const
    {
        int best = -1;
        float bestDx = 14.0f;
        for (int i = 0; i < table.count; ++i)
        {
            const float dx = std::fabs(freqX(table.f[i].freqHz) - x);
            if (dx < bestDx) { bestDx = dx; best = i; }
        }
        return best;
    }

    void drawGraphCard(ImDrawList* dl)
    {
        card(dl, 16, 56, 1024, 318);
        updateCurve();               // the axis scale the drag below measures in
        syncNotchAxis();
        graphInteraction();
        updateCurve();               // and the edits it just made
        syncNotchAxis();

        dl->AddRectFilled(P(GX0, GY0), P(GX1, GY1), kColInset, 3.0f * s);
        dl->PushClipRect(P(GX0, GY0), P(GX1, GY1), true);

        // Grid: 1-2-5 rules labelled, the other integers of each decade faint.
        for (float decade = 10.0f; decade <= 10000.0f; decade *= 10.0f)
            for (int m = 2; m <= 9; ++m)
            {
                const float f = (float)m * decade;
                if (f <= axis.minHz() || f >= axis.maxHz() || m == 2 || m == 5) continue;
                const float x = freqX(f);
                dl->AddLine(P(x, kNotchY0), P(x, kNotchY1), kColGridMinor, 1.0f * s);
            }
        float grid[16];
        const int nGrid = axis.gridLines(grid, 16);
        for (int i = 0; i < nGrid; ++i)
        {
            const float x = freqX(grid[i]);
            dl->AddLine(P(x, kNotchY0), P(x, kNotchY1), kColGrid, 1.0f * s);
            char lbl[12];
            duskdaf::LogFreqAxis::label(grid[i], lbl, (int)sizeof(lbl));
            regularText(dl, x, GY1 - 16.0f, 12.0f, kColLabel, lbl, 0);
        }
        for (int db = -20; db > (int)kRtaMinDb; db -= 20)
        {
            const float y = rtaY((float)db);
            dl->AddLine(P(FX0, y), P(FX1, y), kColGridMinor, 1.0f * s);
        }
        // Left axis: notch dB (amber), the graph's main reading. Right axis:
        // analyser dBFS (silver).
        {
            const int step = roNotchAxisStepDb(curveMinDb);
            for (int db = 0; db >= (int)curveMinDb; db -= step)
            {
                char b[8]; std::snprintf(b, sizeof(b), "%d", db);
                regularText(dl, GX0 + 8.0f, curveY((float)db) - 6.0f, 12.0f, kColAmber, b, -1);
            }
        }
        for (int db = 0; db >= (int)kRtaMinDb; db -= 20)
        {
            char b[8]; std::snprintf(b, sizeof(b), "%d", db);
            regularText(dl, GX1 - 8.0f, rtaY((float)db) - 6.0f, 12.0f, kColSilverDim, b, 1);
        }

        text(dl, GX0 + 10.0f, GY0 + 6.0f, 12.0f, kColAmber, "CUT dB", -1, true);
        regularText(dl, 0.5f * (GX0 + GX1), GY0 + 6.0f, 12.0f, kColLabel, "FREQUENCY (Hz)", 0);
        text(dl, GX1 - 10.0f, GY0 + 6.0f, 12.0f, kColWhiteDim, "INPUT dBFS", 1, true);

        drawRta(dl);
        drawNotchCurve(dl);
        drawRipple(dl);
        drawNodes(dl);
        if (table.count == 0 && values[kParamBypass] < 0.5f)
            drawGraphHint(dl);

        dl->PopClipRect();
        dl->AddRect(P(GX0, GY0), P(GX1, GY1), kColCardEdge, 3.0f * s, 0, 1.0f * s);
    }

    // The graph is a direct editor: a node drags (FREQ across, CUT up and down,
    // Shift for fine), the wheel over a node sets its Q, a double-click
    // switches it on or off, and a click selects it.
    void graphInteraction()
    {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetCursorScreenPos(P(GX0, GY0));
        ImGui::InvisibleButton("##graph", ImVec2((GX1 - GX0) * s, (GY1 - GY0) * s));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 m = mouseDesign();
        hoverNode = hovered && !active ? nodeAt(m) : -1;
        if (hoverNode >= 0)
            tooltip("Drag: frequency and cut (Shift for fine)\nWheel: Q\nDouble-click: on / off");

        if (ImGui::IsItemActivated())
        {
            const int hit = nodeAt(m);
            if (hit >= 0)
                beginDrag(hit);
            else if (const int col = notchInColumn(m.x); col >= 0)
                selected = col;   // a click, not a drag: only a node drags
        }
        dragRow = -1;
        if (active && notchDrag.active)
        {
            const ImVec2 d = dragDelta();
            const float octavesPerPx = std::log2(axis.maxHz() / axis.minHz()) / (FX1 - FX0);
            const float dbPerPx = -curveMinDb / (kNotchY1 - kNotchY0);
            // A switched-off notch sits on the 0 dB line whatever its cut, so a
            // vertical drag would change it out of sight: on the graph it moves
            // in frequency only (its fader still sets the cut).
            const int row = notchDrag.resolve(table);
            const bool cutShows = row >= 0 && table.f[row].on;
            notchDrag.move(d.x * octavesPerPx, cutShows ? -d.y * dbPerPx : 0.0f, io.KeyShift);
            applyDrag(true);
        }
        if (ImGui::IsItemDeactivated())
            notchDrag.end();

        if (hoverNode >= 0 && io.MouseWheel != 0.0f)
        {
            const int steps = takeWheelSteps(-1 - hoverNode);
            if (steps != 0)
            {
                selected = hoverNode;
                const float q = table.f[hoverNode].q;
                setRowField(hoverNode, &ro::Filter::q, ro::snapQ(q + (float)steps * (io.KeyShift ? 0.1f : 0.5f)));
            }
        }

        if (hovered && ImGui::IsMouseDoubleClicked(0))
        {
            const int hit = nodeAt(m);
            if (hit >= 0)
            {
                selected = hit;
                setRowField(hit, &ro::Filter::on, !table.f[hit].on);
            }
        }
    }

    // Writes the fields the drag has moved since it last wrote, and nothing
    // else: a FREQ-only drag leaves an engine deepen that landed meanwhile in
    // place.
    void applyDrag(bool withFreq)
    {
        const int row = notchDrag.resolve(table);
        if (row < 0) { notchDrag.end(); return; }
        selected = row;
        dragRow = row;
        const float hz = notchDrag.freqHz(), cut = notchDrag.cutDb();
        if (withFreq && hz != dragSentHz) { dragSentHz = hz; setRowField(row, &ro::Filter::freqHz, hz); }
        if (cut != dragSentCut)           { dragSentCut = cut; setRowField(row, &ro::Filter::cutDb, cut); }
    }

    void beginDrag(int row)
    {
        selected = row;
        notchDrag.begin(table, row);
        dragSentHz = notchDrag.freqHz();
        dragSentCut = notchDrag.cutDb();
    }

    // A value typed into the inspector commits in the very frame a press on
    // the same notch's node or fader starts a drag (the press is what takes
    // focus from the field), after the drag has already taken its snapshot.
    // Take it again, or the drag's first move would write the old value back
    // over the typed one. Only the press frame's motion is lost.
    void rebaseDragOn(int row)
    {
        if (notchDrag.active && notchDrag.resolve(table) == row)
            beginDrag(row);
    }

    void drawRta(ImDrawList* dl)
    {
        const int cols = (int)rta.size();
        if (cols < 2 || !engineLive || rtaTarget.empty()) return;
        std::vector<ImVec2>& pts = ptsScratch;
        pts.clear();
        pts.reserve((size_t)cols);
        for (int c = 0; c < cols; ++c)
            pts.push_back(P(FX0 + (float)c, rtaY(rta[(size_t)c])));
        fillToLine(dl, pts.data(), cols, P(FX0, kNotchY1).y, kColSilverFill);
        dl->AddPolyline(pts.data(), cols, kColSilver, 0, 1.2f * s);
    }

    // The composite response of the on filters, from the engine's own section
    // design and response formula, evaluated once per change of table, trims
    // or sample rate rather than every frame.
    void updateCurve()
    {
        const float gq = values[kParamGlobalQ], amp = values[kParamGlobalAmp];
        const double sr = getSampleRate() > 1.0 ? getSampleRate() : 48000.0;
        if (curveStamp == tableStamp && curveGq == gq && curveAmp == amp && curveSr == sr
            && curveDb.size() == (size_t)kCurvePoints)
            return;
        curveStamp = tableStamp; curveGq = gq; curveAmp = amp; curveSr = sr;
        curveDb.resize((size_t)kCurvePoints);
        curveAnyOn = false;
        for (int i = 0; i < table.count && !curveAnyOn; ++i) curveAnyOn = table.f[i].on;
        // Design each on filter once (the matched design is the costly part),
        // then only evaluate magnitudes along the axis. A drag rebuilds this
        // every frame.
        duskaudio::DoubleBiquad sections[ro::kMaxFilters];
        const int nSections = RingOutDSP::designSections(table, gq, amp, sr, sections, ro::kMaxFilters);
        float deepest = 0.0f;
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const float f = axis.fromNorm((float)i / (float)(kCurvePoints - 1));
            curveDb[(size_t)i] = (float)RingOutDSP::sectionsResponseDb(sections, nSections, sr, (double)f);
            deepest = std::min(deepest, curveDb[(size_t)i]);
        }
        for (int i = 0; i < table.count; ++i)
            if (table.f[i].on)
                deepest = std::min(deepest, RingOutDSP::effectiveCutDb(table.f[i].cutDb, amp));
        curveAxisTarget = roNotchAxisFloorDb(deepest);
    }

    // The axis follows the table except while a notch is being dragged: a
    // rescale mid-gesture would move the node out from under the pointer and
    // change the drag's dB-per-pixel halfway through. It catches up on release.
    void syncNotchAxis()
    {
        if (!notchDrag.active)
            curveMinDb = curveAxisTarget;
    }

    void drawNotchCurve(ImDrawList* dl)
    {
        dl->AddLine(P(FX0, kNotchY0), P(FX1, kNotchY0), fade(kColAmber, 0.3f), 1.0f * s);
        if (!curveAnyOn || curveDb.size() != (size_t)kCurvePoints)
            return;
        std::vector<ImVec2>& pts = ptsScratch;
        pts.clear();
        pts.reserve((size_t)kCurvePoints);
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const float lx = (float)i / (float)(kCurvePoints - 1);
            pts.push_back(P(FX0 + lx * (FX1 - FX0), curveY(curveDb[(size_t)i])));
        }
        fillToLine(dl, pts.data(), kCurvePoints, P(FX0, kNotchY0).y, kColAmberFill);
        dl->AddPolyline(pts.data(), kCurvePoints, kColAmber, 0, 1.8f * s);
    }

    // The row the last engine engagement touched, followed by id like a drag.
    int rippleTarget() const
    {
        if (rippleRowId != 0)
        {
            for (int i = 0; i < table.count; ++i)
                if (table.id[i] == rippleRowId) return i;
            return -1;
        }
        return rippleRow < table.count ? rippleRow : -1;
    }

    // Two rings spreading from the notch the engine just placed or deepened.
    void drawRipple(ImDrawList* dl)
    {
        if (rippleSecondsLeft <= 0.0f) return;
        const int row = rippleTarget();
        const ImVec2 p = row >= 0 ? nodePos(row) : ImVec2(freqX(rippleHz), kNotchY0);
        const float t = 1.0f - rippleSecondsLeft / kRippleSeconds;
        for (int k = 0; k < 2; ++k)
        {
            const float tk = t - 0.28f * (float)k;
            if (tk < 0.0f || tk > 1.0f) continue;
            const float r = kNodeR + 4.0f + 30.0f * tk;
            dl->AddCircle(P(p.x, p.y), r * s, fade(kColAmber, 0.85f * (1.0f - tk)), 48,
                          (2.2f * (1.0f - tk) + 0.6f) * s);
        }
    }

    void drawNodes(ImDrawList* dl)
    {
        // The selected node last, so it sits on top of any it overlaps.
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < table.count; ++i)
            {
                const bool sel = i == selected;
                if (sel != (pass == 1)) continue;
                const ro::Filter& f = table.f[i];
                const ImVec2 p = nodePos(i);
                const ImVec2 c = P(p.x, p.y);
                const float r = kNodeR * s;
                ImU32 ink;
                if (f.on)
                {
                    dl->AddCircleFilled(c, r, kColAmber, 24);
                    dl->AddCircle(c, r, IM_COL32(0, 0, 0, 170), 24, 1.0f * s);
                    ink = kColInk;
                }
                else
                {
                    dl->AddCircleFilled(c, r, kColInset, 24);
                    dl->AddCircle(c, r, kColAmberDim, 24, 1.4f * s);
                    ink = kColAmberDim;
                }
                if (sel)
                    dl->AddCircle(c, r + 3.0f * s, kColWhite, 28, 1.6f * s);
                else if (i == hoverNode || i == dragRow)
                    dl->AddCircle(c, r + 2.5f * s, fade(kColWhite, 0.45f), 28, 1.2f * s);
                char num[4]; std::snprintf(num, sizeof(num), "%d", i + 1);
                text(dl, p.x, p.y - 6.0f, 11.0f, ink, num, 0, true);
                // Deeper than the axis shows, which happens while the axis holds
                // still for a drag (one notch reaches -44 dB at most, inside the
                // -48 dB cap): the node sits on the floor with an arrow saying it
                // goes further, and the callout has the value.
                if (f.on && RingOutDSP::effectiveCutDb(f.cutDb, values[kParamGlobalAmp]) < curveMinDb - 0.05f)
                {
                    const float ax = p.x + kNodeR + 4.0f;
                    dl->AddTriangleFilled(P(ax, p.y - 3.0f), P(ax + 7.0f, p.y - 3.0f), P(ax + 3.5f, p.y + 3.0f),
                                          kColAmber);
                }
            }
        if (selected >= 0 && selected < table.count)
            drawCallout(dl, selected);
    }

    // The selected notch's values in a tag beside its node, flipped to the left
    // near the right edge and kept clear of the frequency labels.
    void drawCallout(ImDrawList* dl, int row)
    {
        const ro::Filter& f = table.f[row];
        char num[4];
        std::snprintf(num, sizeof(num), "%d", row + 1);
        // The node sits at the depth after CUT TRIM; when the trim changes it,
        // say both, or the tag would contradict the node it labels.
        char cut[40];
        const float effective = RingOutDSP::effectiveCutDb(f.cutDb, values[kParamGlobalAmp]);
        if (f.on && std::fabs(effective - f.cutDb) > 0.05f)
            std::snprintf(cut, sizeof(cut), "%.1f dB (%.1f with trim)", (double)f.cutDb, (double)effective);
        else
            std::snprintf(cut, sizeof(cut), "%.1f dB", (double)f.cutDb);
        char body[96];
        // "\xC2\xB7" is a middle dot (Latin-1, in the atlas).
        std::snprintf(body, sizeof(body),
                      f.freqHz < 1000.0f ? "  \xC2\xB7  %.1f Hz  \xC2\xB7  %s  \xC2\xB7  Q %.1f%s"
                                         : "  \xC2\xB7  %.0f Hz  \xC2\xB7  %s  \xC2\xB7  Q %.1f%s",
                      (double)f.freqHz, cut, (double)f.q, f.on ? "" : "  \xC2\xB7  OFF");
        constexpr float kTxt = 13.0f, kPadX = 9.0f, kH = 26.0f, kGap = 15.0f;
        const float wNum = boldWidth(kTxt, num), wBody = regularWidth(kTxt, body);
        const float w = wNum + wBody + 2.0f * kPadX;
        const ImVec2 p = nodePos(row);
        // Clear of the clip arrow drawNodes puts beside a node below the floor.
        const float gap = kGap + (f.on && effective < curveMinDb - 0.05f ? 12.0f : 0.0f);
        float x = p.x + gap;
        if (x + w > FX1) x = std::max(GX0 + 4.0f, p.x - gap - w);
        const float y = std::clamp(p.y - 0.5f * kH, GY0 + 24.0f, GY1 - 28.0f - kH);
        dl->AddRectFilled(P(x, y), P(x + w, y + kH), IM_COL32(20, 20, 21, 236), 4.0f * s);
        dl->AddRect(P(x, y), P(x + w, y + kH), fade(kColWhite, 0.35f), 4.0f * s, 0, 1.0f * s);
        text(dl, x + kPadX, y + 0.5f * (kH - kTxt) - 0.5f, kTxt, kColAmber, num, -1, true);
        regularText(dl, x + kPadX + wNum, y + 0.5f * (kH - kTxt) - 0.5f, kTxt, kColWhite, body, -1);
    }

    void drawGraphHint(ImDrawList* dl)
    {
        const bool listening = engineLive && status.setupActive;
        const char* line1 = listening ? "Listening. Raise the gain slowly."
                                      : "Press LEARN and raise the gain slowly: Ring Out notches each ring it hears.";
        const char* line2 = listening ? "Each ring it catches gets a notch here."
                                      : "Or click + in the notch bank to place a notch by hand.";
        const float hintW = std::max(regularWidth(16.0f, line1), regularWidth(14.0f, line2)) + 32.0f;
        const float hintX = 0.5f * (GX0 + GX1 - hintW);
        dl->AddRectFilled(P(hintX, 150.0f), P(hintX + hintW, 211.0f),
                          IM_COL32(14, 14, 15, 238), 4.0f * s);
        dl->AddRect(P(hintX, 150.0f), P(hintX + hintW, 211.0f), kColGrid, 4.0f * s, 0, 1.0f * s);
        regularText(dl, 0.5f * (GX0 + GX1), 161.0f, 16.0f, kColWhiteDim, line1, 0);
        regularText(dl, 0.5f * (GX0 + GX1), 185.0f, 14.0f, kColLabel, line2, 0);
    }

    //--- notch bank ----------------------------------------------------------------
    void drawNotchBank(ImDrawList* dl)
    {
        card(dl, 16, 326, 712, 628);
        sectionTitle(dl, 32, 336, "NOTCHES");
        char count[16];
        std::snprintf(count, sizeof(count), "%d / %d", table.count, ro::kMaxFilters);
        regularText(dl, 130, 336.0f, 12.0f, kColLabel, count, -1);
        if (button(dl, "##clear", 640, 331, 700, 355, "CLEAR", false, kColAmber, table.count > 0, 12.0f,
                   nullptr, "Remove every notch"))
            clearNotches();

        // One shared legend keeps each narrow strip readable without repeating
        // units twenty times: Hz beside the frequency row, the fader scale, and
        // dB beside the cut row. Both describe the per-notch CUT, before CUT TRIM.
        regularText(dl, 46, 387.0f, 11.0f, kColLabel, "Hz", 1);
        regularText(dl, 46, 524.0f, 11.0f, kColLabel, "dB", 1);
        for (int t = 0; t <= 4; ++t)
        {
            char db[8];
            std::snprintf(db, sizeof(db), "%d", -5 * t);
            const float y = kFaderY0 + (kFaderY1 - kFaderY0) * (float)t / 4.0f;
            regularText(dl, 46, y - 6.0f, 11.0f, kColLabel, db, 1);
        }

        for (int i = 0; i < ro::kMaxFilters; ++i)
            drawStrip(dl, i);

        dl->AddLine(P(28, 561), P(700, 561), kColGridMinor, 1.0f * s);
        drawInspector(dl);
    }

    // One notch slot: number badge, short frequency, CUT fader, cut and Q.
    // An empty slot is a dim recess; the first empty one carries the "+" that
    // places a new notch.
    void drawStrip(ImDrawList* dl, int i)
    {
        const float x0 = kBankX0 + (float)i * kStripW, x1 = x0 + kStripW;
        const float cx = 0.5f * (x0 + x1);
        const bool exists = i < table.count;
        const bool addSlot = i == table.count;
        const bool sel = exists && i == selected;

        dl->AddRectFilled(P(x0 + 1.5f, kStripY0), P(x1 - 1.5f, kStripY1),
                          sel ? IM_COL32(46, 44, 40, 255)
                              : exists ? IM_COL32(33, 33, 35, 255) : IM_COL32(23, 23, 25, 255),
                          3.0f * s);
        if (sel)
            dl->AddRect(P(x0 + 1.5f, kStripY0), P(x1 - 1.5f, kStripY1), fade(kColWhite, 0.5f),
                        3.0f * s, 0, 1.2f * s);

        ImGui::PushID(i);

        // Badge: click selects, double-click switches the notch on or off; on
        // the "+" slot, a click adds a notch.
        ImGui::SetCursorScreenPos(P(x0, kBadgeY - 13.0f));
        ImGui::InvisibleButton("##badge", ImVec2(kStripW * s, 26.0f * s));
        const bool badgeHot = ImGui::IsItemHovered();
        if (exists)
        {
            if (ImGui::IsItemClicked()) selected = i;
            if (badgeHot && ImGui::IsMouseDoubleClicked(0))
            {
                selected = i;
                setRowField(i, &ro::Filter::on, !table.f[i].on);
            }
            tooltip("Click: select\nDouble-click: on / off");
        }
        else if (addSlot)
        {
            if (ImGui::IsItemClicked()) addNotch();
            tooltip("Add a notch");
        }

        const ImVec2 bc = P(cx, kBadgeY);
        const float br = 9.5f * s;
        char num[4]; std::snprintf(num, sizeof(num), "%d", i + 1);
        if (exists)
        {
            const bool on = table.f[i].on;
            if (on) dl->AddCircleFilled(bc, br, kColAmber, 24);
            else    dl->AddCircle(bc, br, kColAmberDim, 24, 1.4f * s);
            if (badgeHot) dl->AddCircle(bc, br + 2.0f * s, fade(kColWhite, 0.4f), 24, 1.0f * s);
            text(dl, cx, kBadgeY - 6.0f, 11.0f, on ? kColInk : kColAmber, num, 0, true);
            if (rippleSecondsLeft > 0.0f && rippleTarget() == i)
                dl->AddCircleFilled(bc, br + 6.0f * s * (rippleSecondsLeft / kRippleSeconds),
                                    fade(kColAmber, 0.45f * rippleSecondsLeft / kRippleSeconds), 24);
        }
        else if (addSlot)
        {
            dl->AddCircle(bc, br, badgeHot ? kColWhite : kColWhiteDim, 24, 1.3f * s);
            const float a = 4.0f * s;
            const ImU32 ink = badgeHot ? kColWhite : kColWhiteDim;
            dl->AddLine(ImVec2(bc.x - a, bc.y), ImVec2(bc.x + a, bc.y), ink, 1.5f * s);
            dl->AddLine(ImVec2(bc.x, bc.y - a), ImVec2(bc.x, bc.y + a), ink, 1.5f * s);
        }
        else
            text(dl, cx, kBadgeY - 6.0f, 11.0f, IM_COL32(84, 82, 78, 255), num, 0, true);

        // Fader track, drawn for every slot so the bank reads as twenty channels.
        const float tx0 = cx - 2.5f, tx1 = cx + 2.5f;
        dl->AddRectFilled(P(tx0, kFaderY0), P(tx1, kFaderY1), kColInset, 2.0f * s);
        for (int t = 0; t <= 4; ++t)   // 0, -5, -10, -15, -20 dB
        {
            const float y = kFaderY0 + (kFaderY1 - kFaderY0) * (float)t / 4.0f;
            dl->AddLine(P(cx - 9.0f, y), P(cx - 5.5f, y), exists ? kColTrack : kColGridMinor, 1.0f * s);
        }

        if (exists)
            drawFader(dl, i, x0, x1, cx);

        ImGui::PopID();
    }

    void drawFader(ImDrawList* dl, int i, float x0, float x1, float cx)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const float trackH = kFaderY1 - kFaderY0;

        // The strip below the badge is the fader: drag for CUT (Shift for fine),
        // wheel for half-dB steps, and a press selects the notch.
        ImGui::SetCursorScreenPos(P(x0, kBadgeY + 13.0f));
        ImGui::InvisibleButton("##fader", ImVec2((x1 - x0) * s, (kStripY1 - kBadgeY - 13.0f) * s));
        const bool hov = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemActivated())
            beginDrag(i);
        if (active && notchDrag.active)
        {
            notchDrag.move(0.0f, -dragDelta().y * (-ro::kCutMin / trackH), io.KeyShift);
            applyDrag(false);
        }
        if (ImGui::IsItemDeactivated())
            notchDrag.end();
        if (hov && !active && io.MouseWheel != 0.0f)
        {
            const int steps = takeWheelSteps(i + 1);
            if (steps != 0)
            {
                selected = i;
                setRowField(i, &ro::Filter::cutDb,
                            ro::snapCut(table.f[i].cutDb + (float)steps * (io.KeyShift ? 0.1f : 0.5f)));
            }
        }

        const ro::Filter& f = table.f[i];
        const bool sel = i == selected;
        const float yCut = kFaderY0 + trackH * std::clamp(f.cutDb / ro::kCutMin, 0.0f, 1.0f);
        if (yCut > kFaderY0 + 0.5f)
            dl->AddRectFilled(P(cx - 2.5f, kFaderY0), P(cx + 2.5f, yCut),
                              f.on ? kColAmber : kColTrack, 2.0f * s);
        const bool hot = hov || active;
        dl->AddRectFilled(P(cx - 9.0f, yCut - 3.5f), P(cx + 9.0f, yCut + 3.5f),
                          hot ? IM_COL32(236, 232, 222, 255) : IM_COL32(196, 192, 182, 255), 2.0f * s);
        dl->AddLine(P(cx - 6.0f, yCut), P(cx + 6.0f, yCut), IM_COL32(40, 38, 34, 255), 1.2f * s);

        char hz[16];
        roShortHz(f.freqHz, hz, (int)sizeof(hz));
        regularText(dl, cx, 386.0f, 12.0f, sel ? kColWhite : kColWhiteDim, hz, 0);
        char cut[16];
        std::snprintf(cut, sizeof(cut), "%.1f", (double)f.cutDb);
        regularText(dl, cx, 523.0f, 12.0f, sel ? kColWhite : kColWhiteDim, cut, 0);
        char q[16];
        std::snprintf(q, sizeof(q), "Q%.1f", (double)f.q);
        regularText(dl, cx, 539.0f, 12.0f, kColLabel, q, 0);
    }

    // The selected notch's exact values: steppers with typed entry, ON, DELETE
    // and previous / next.
    void drawInspector(ImDrawList* dl)
    {
        filterEditSelection.update(panel, table, selected);
        const bool haveSel = selected >= 0 && selected < table.count;
        const ro::Filter cur = haveSel ? table.f[selected] : ro::Filter();

        char title[16] = "NOTCH";
        if (haveSel)
            std::snprintf(title, sizeof(title), "NOTCH %d", selected + 1);
        spaced(dl, 32, 577, 12.0f, haveSel ? kColAmber : kColLabel, title, -1, 0.10f);

        duskdaf::StepperBoxRule cutRule, freqRule, qRule;
        cutRule.step = [](float v, int d, void*) { return ro::stepCut(v, d); };
        cutRule.snap = [](float v, void*) { return ro::snapCut(v); };
        cutRule.minV = ro::kCutMin; cutRule.maxV = ro::kCutMax;
        cutRule.unit = duskdaf::StepperBoxRule::kDecibels;
        freqRule.step = [](float v, int d, void*) { return ro::stepFreq(v, d); };
        freqRule.snap = [](float v, void*) { return ro::snapFreq(v); };
        freqRule.minV = ro::kFreqMin; freqRule.maxV = ro::kFreqMax;
        freqRule.unit = duskdaf::StepperBoxRule::kHertz;
        qRule.step = [](float v, int d, void*) { return ro::stepQ(v, d); };
        qRule.snap = [](float v, void*) { return ro::snapQ(v); };
        qRule.minV = ro::kQMin; qRule.maxV = ro::kQMax;

        float cut = cur.cutDb, freq = cur.freqHz, q = cur.q;
        constexpr float by0 = 570.0f, by1 = 596.0f;
        duskdaf::StepperBoxStyle stepperStyle;
        stepperStyle.textSize = 16.0f;
        stepperStyle.text = kColWhite;
        // A tenth of a hertz below 1 kHz (a detected 66.3 Hz reads as such), whole
        // hertz above where the control steps by 100. With nothing selected the
        // read-outs show a dash, not a default that looks like a notch at 24 Hz.
        if (duskdaf::stepperBox(panel, dl, "freq", 128, by0, 238, by1, freq, freqRule,
                                !haveSel ? "-" : freq < 1000.0f ? "%.1f" : "%.0f", haveSel, stepperStyle))
        {
            setSelectedFilter(&ro::Filter::freqHz, freq);
            rebaseDragOn(selected);
        }
        if (duskdaf::stepperBox(panel, dl, "cut", 252, by0, 348, by1, cut, cutRule,
                                haveSel ? "%.1f" : "-", haveSel, stepperStyle))
        {
            setSelectedFilter(&ro::Filter::cutDb, cut);
            rebaseDragOn(selected);
        }
        if (duskdaf::stepperBox(panel, dl, "q", 362, by0, 446, by1, q, qRule,
                                haveSel ? "%.1f" : "-", haveSel, stepperStyle))
            setSelectedFilter(&ro::Filter::q, q);
        spaced(dl, 183, 601, 12.0f, kColLabel, "FREQ Hz", 0, 0.08f);
        spaced(dl, 300, 601, 12.0f, kColLabel, "CUT dB", 0, 0.08f);
        spaced(dl, 404, 601, 12.0f, kColLabel, "Q", 0, 0.08f);

        if (button(dl, "##on", 462, 571, 512, 595, cur.on || !haveSel ? "ON" : "OFF", haveSel && cur.on,
                   kColAmber, haveSel, 12.0f, nullptr, "Switch the selected notch on or off"))
            setSelectedFilter(&ro::Filter::on, !cur.on);
        if (button(dl, "##delete", 522, 571, 598, 595, "DELETE", false, kColAmber, haveSel, 12.0f,
                   nullptr, "Remove the selected notch"))
            deleteSelected();

        // Previous / next: clamped, never wrapping; from no selection both land on 1.
        if (chevron(dl, "##prevnotch", 646, 583, 11.0f, true) && table.count > 0)
            selected = selected < 0 ? 0 : std::max(0, selected - 1);
        if (chevron(dl, "##nextnotch", 680, 583, 11.0f, false) && table.count > 0)
            selected = selected < 0 ? 0 : std::min(table.count - 1, selected + 1);
    }

    //--- detect card -----------------------------------------------------------------
    void drawDetectCard(ImDrawList* dl)
    {
        card(dl, 720, 326, 1024, 470);
        sectionTitle(dl, 736, 336, "DETECT");
        const bool live = engineLive;

        // LEARN: arms the engine for a minute, or disarms it. Arming also puts
        // Q SCALE and CUT TRIM back to their defaults, so the notches about to
        // be placed are judged at face value. Those are editor edits the host
        // sees; the plugin itself never rewrites one parameter because another
        // moved (that fails AU validation).
        // The engine buttons travel the edit channel, like CLEAR: the trigger
        // parameters are for host automation and controllers, and a trigger
        // written from the editor could be swallowed by a host that forwards
        // only control-port changes.
        // The button sends its INTENT (arm or disarm), never a toggle: the
        // status it shows is a frame old, and a minute that expired in between
        // must turn a stop into a harmless no-op rather than into a re-arm.
        const bool learnOn = live && status.setupActive;
        if (learnButton(dl, 790, 406, 38.0f, learnOn))
        {
            if (!learnOn)
            {
                setP(kParamGlobalQ, ro::kGlobalQDefault);
                setP(kParamGlobalAmp, ro::kGlobalAmpDefault);
            }
            sendEngineCommand(learnOn ? ro::EditCommand::kSetupOff : ro::EditCommand::kSetupOn);
        }

        // CATCH: hold to search. The engine also stops itself after one notch
        // and the button turns green until it is released.
        bool catchHeld = false;
        const bool catchLit = live && (status.addSearching || status.addSatisfied);
        const ImU32 catchCol = (live && status.addSatisfied) ? kColGreen : kColAmber;
        button(dl, "##catch", 850, 350, 1010, 380, "HOLD TO CATCH", catchLit, catchCol, true, 13.0f, &catchHeld,
               "Hold while the system rings: places or deepens one notch, then stops");
        if (catchHeld != catchHeldLocal)
        {
            catchHeldLocal = catchHeld;
            catchLeaseTimer = 0.0f;
            sendEngineCommand(catchHeld ? ro::EditCommand::kAddStart : ro::EditCommand::kAddStop);
        }
        // While held, keep the engine's lease alive. If the editor closes or
        // stops drawing mid-press the lease lapses and the engine ends the
        // search by itself, which is the only release it can rely on.
        if (catchHeldLocal)
        {
            catchLeaseTimer += std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
            if (catchLeaseTimer >= 0.5f)
            {
                catchLeaseTimer = 0.0f;
                sendEngineCommand(ro::EditCommand::kAddHold);
            }
        }

        spaced(dl, 850, 386, 12.0f, kColLabel, "SENSITIVITY", -1, 0.10f);
        const int high = values[kParamSense] >= 0.5f ? 1 : 0;
        const int hit = segmentSwitch(dl, "##sense", 850, 401, 1010, 423, "LOW", "HIGH", high,
                                      "Low: only clear, steady rings (ringing out before the show)\n"
                                      "High: quieter and shorter rings too");
        if (hit >= 0 && hit != high)
            setP(kParamSense, (float)hit);

        drawStatus(dl, 850, 434, 1010, 458);
    }

    // The round LEARN button with its countdown: the amber arc drains clockwise
    // from twelve o'clock over the minute and the face shows the time left.
    bool learnButton(ImDrawList* dl, float cx, float cy, float r, bool armed)
    {
        const ImVec2 c = P(cx, cy);
        const float hit = r + 8.0f;
        ImGui::SetCursorScreenPos(ImVec2(c.x - hit * s, c.y - hit * s));
        ImGui::InvisibleButton("##learn", ImVec2(2.0f * hit * s, 2.0f * hit * s));
        // The button is round: a press in a corner of its square must not arm
        // the detector (and reset both trims).
        const ImGuiIO& io = ImGui::GetIO();
        const auto inside = [&](ImVec2 p) {
            const float dx = p.x - c.x, dy = p.y - c.y;
            return dx * dx + dy * dy <= (hit * s) * (hit * s);
        };
        const bool hov = ImGui::IsItemHovered() && inside(io.MousePos);
        const bool clicked = ImGui::IsItemClicked() && inside(io.MouseClickedPos[0]);
        if (hov)
            tooltip(armed ? "Stop listening"
                          : "Listen for one minute and notch every ring.\n"
                            "Raise the gain slowly while it listens.");

        const float ringR = (r + 7.0f) * s;
        if (armed)
        {
            const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 4.0f);
            dl->AddCircleFilled(c, ringR + 7.0f * s, fade(kColAmber, 0.05f + 0.06f * pulse), 64);
        }
        dl->AddCircle(c, ringR, armed ? kColTrack : fade(kColAmber, hov ? 0.8f : 0.5f),
                      64, (armed ? 3.5f : 2.0f) * s);
        if (armed)
        {
            const float frac = std::clamp(status.setupRemainingSeconds / ro::kSetupSeconds, 0.0f, 1.0f);
            if (frac > 0.0f)
            {
                const float a0 = -0.5f * kPi;
                dl->PathArcTo(c, ringR, a0, a0 + frac * 2.0f * kPi, 64);
                dl->PathStroke(kColAmber, 0, 3.5f * s);
            }
        }
        dl->AddCircleFilled(c, (r + 2.0f) * s, IM_COL32(10, 10, 11, 255), 48);
        dl->AddCircleFilled(c, r * s, armed ? kColAmberDeep : (hov ? IM_COL32(60, 58, 54, 255)
                                                                   : IM_COL32(46, 45, 42, 255)), 48);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.2f * s), r * 0.76f * s,
                            IM_COL32(255, 255, 255, armed ? 8 : 12), 40);
        dl->AddCircle(c, r * s, fade(kColAmber, armed || hov ? 0.8f : 0.5f), 48, 1.2f * s);

        if (armed)
        {
            const int secs = (int)std::ceil(status.setupRemainingSeconds);
            char t[12];
            std::snprintf(t, sizeof(t), "%d:%02d", secs / 60, secs % 60);
            text(dl, cx, cy - 18.0f, 24.0f, kColAmber, t, 0, true);
            spaced(dl, cx, cy + 11.0f, 11.0f, kColAmber, "LEARNING", 0, 0.08f);
        }
        else
        {
            spaced(dl, cx, cy - 13.0f, 17.0f, kColWhite, "LEARN", 0, 0.10f);
            regularText(dl, cx, cy + 11.0f, 12.0f, kColWhiteDim, "60 s", 0);
        }
        return clicked;
    }

    // One line saying what the engine is doing.
    void drawStatus(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kColInset, 3.0f * s);
        dl->AddRect(P(x0, y0), P(x1, y1), kColGrid, 3.0f * s, 0, 1.0f * s);

        const bool live = engineLive;
        const bool listening = live && (status.setupActive || status.addSearching);
        char st[48] = "";
        ImU32 col = kColWhiteDim;
        // The uncovered-ring report only exists while the engine listens, and
        // it outranks the listening line: it is the one thing to act on.
        if (listening && status.ringUncovered)
        {
            std::snprintf(st, sizeof(st), "RING NOT COVERED");
            const float blink = 0.6f + 0.4f * std::sin((float)ImGui::GetTime() * 6.0f);
            dl->AddRect(P(x0, y0), P(x1, y1), fade(kColWarn, blink), 3.0f * s, 0, 1.0f * s);
            col = kColWarn;
        }
        else if (live && status.setupActive)
        {
            std::snprintf(st, sizeof(st), "LEARNING");
            col = kColAmber;
        }
        else if (live && status.addSearching)
        {
            std::snprintf(st, sizeof(st), "CATCH: LISTENING");
            col = kColAmber;
        }
        else if (live && status.addSatisfied)
        {
            std::snprintf(st, sizeof(st), "CATCH: NOTCH PLACED");
            col = kColGreen;
        }
        else if (live && status.setupExpired)
            std::snprintf(st, sizeof(st), "LEARN ENDED: MINUTE UP");
        else if (table.count > 0)
            std::snprintf(st, sizeof(st), "%d NOTCH%s", table.count, table.count == 1 ? "" : "ES");
        else
            std::snprintf(st, sizeof(st), "NO NOTCHES");
        regularText(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 6.5f, 13.0f, col, st, 0);
    }

    //--- output card ---------------------------------------------------------------
    // Q SCALE and CUT TRIM act on every notch; LINK ties CUT TRIM to OUTPUT.
    // A trim away from its default reads in amber.
    void drawOutputCard(ImDrawList* dl)
    {
        card(dl, 720, 478, 1024, 628);

        spaced(dl, 806, 484, 11.0f, kColLabel, "ALL NOTCHES", 0, 0.08f);
        const ImU32 bracket = kColGrid;
        dl->AddLine(P(746, 491), P(770, 491), bracket, 1.0f * s);
        dl->AddLine(P(842, 491), P(866, 491), bracket, 1.0f * s);
        dl->AddLine(P(746, 491), P(746, 497), bracket, 1.0f * s);
        dl->AddLine(P(866, 491), P(866, 497), bracket, 1.0f * s);

        constexpr float kKnobY = 553.0f;
        const float prevAmp = values[kParamGlobalAmp];
        const float prevGain = values[kParamGainOut];
        const bool link = values[kParamLink] >= 0.5f;

        // Fleet hit-target minimums: 42 px for the small knobs, 52 px for OUTPUT.
        constexpr float kTrimX = 842.0f, kTrimR = 21.0f, kOutX = 932.0f, kOutR = 26.0f;
        knobCell(dl, "##globalq", kParamGlobalQ, 770, kKnobY, kTrimR, "Q SCALE", "Q Scale",
                 "Multiplies the Q of every notch: 0.5 makes every notch twice as wide");
        const bool ampChanged = knobCell(dl, "##globalamp", kParamGlobalAmp, kTrimX, kKnobY, kTrimR,
                                         "CUT TRIM", "Cut Trim",
                                         "Added to every notch's cut: negative deepens them all");
        const bool gainChanged = knobCell(dl, "##gainout", kParamGainOut, kOutX, kKnobY, kOutR,
                                          "OUTPUT", "Output", "Output level");

        // The chain sits between the two knobs it ties, reaching their rings.
        if (linkToggle(dl, kTrimX + kTrimR + kKnobRingGap, kOutX - kOutR - kKnobRingGap, kKnobY, link))
            setP(kParamLink, link ? 0.0f : 1.0f);

        // LINK: CUT TRIM and OUTPUT move together in opposite directions, so a
        // few dB more output means the same few dB deeper on every notch. An
        // editor gesture coupling: both parameters reach the host as edits, the
        // follower inside one gesture spanning the drag rather than a gesture
        // per frame. Host automation of one knob moves only that knob (see
        // RingOutPlugin.cpp, kParamLink).
        //
        // A drag is a run of changes while the mouse button is down; the
        // follower gesture opens on the first such change and closes when the
        // button comes up (or LINK goes off). The knob's own item state is not
        // consulted: knob() may submit an inline text field or a popup after
        // its button, so "last item" would be the wrong item.
        if (linkFollowerOpen >= 0 && (!link || !ImGui::IsMouseDown(0)))
        {
            editParameter((uint32_t)linkFollowerOpen, false);
            linkFollowerOpen = -1;
        }
        if (link)
        {
            if (gainChanged)
                setFollower(kParamGlobalAmp, prevAmp - (values[kParamGainOut] - prevGain));
            else if (ampChanged)
                setFollower(kParamGainOut, prevGain - (values[kParamGlobalAmp] - prevAmp));
        }

        drawMeter(dl, "##inmeter", inMeter, 970, 500, 986, 596, "IN");
        drawMeter(dl, "##outmeter", outMeter, 994, 500, 1010, 596, "OUT");
    }

    // A knob with its value arc, name above and read-out below. The arc runs
    // from the default to the value, so an untouched trim shows no arc at all.
    // The art is drawn before the gesture call (so it never paints over the
    // knob's own type-entry field) and the read-out after it, which keeps the
    // number exact during a drag. Returns whether the value changed.
    bool knobCell(ImDrawList* dl, const char* id, uint32_t param, float cx, float cy, float r,
                  const char* label, const char* name, const char* tip)
    {
        const RoParam& d = kRoParams[param];
        const auto tOf = [&](float v) { return std::clamp((v - d.min) / (d.max - d.min), 0.0f, 1.0f); };
        const bool offDefault = std::fabs(values[param] - d.def) > 1.0e-4f;

        const ImVec2 c = P(cx, cy);
        const float R = r * s, ringR = R + kKnobRingGap * s;
        const float t = tOf(values[param]), tDef = tOf(d.def);
        dl->PathArcTo(c, ringR, arcAngle(0.0f), arcAngle(1.0f), 48);
        dl->PathStroke(kColTrack, 0, 2.6f * s);
        if (std::fabs(t - tDef) > 0.002f)
        {
            dl->PathArcTo(c, ringR, arcAngle(std::min(t, tDef)), arcAngle(std::max(t, tDef)), 48);
            dl->PathStroke(kColAmber, 0, 2.6f * s);
        }
        const float ad = duskdaf::DuskPanel::knobAngle(tDef);
        dl->AddCircleFilled(ImVec2(c.x + std::sin(ad) * (ringR + 4.5f * s), c.y - std::cos(ad) * (ringR + 4.5f * s)),
                            1.4f * s, kColLabel, 8);
        dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f * s), R * 1.02f, IM_COL32(0, 0, 0, 120), 40);
        dl->AddCircleFilled(c, R, IM_COL32(48, 47, 44, 255), 40);
        dl->AddCircleFilled(ImVec2(c.x, c.y - R * 0.16f), R * 0.8f, IM_COL32(255, 255, 255, 12), 36);
        dl->AddCircle(c, R, IM_COL32(86, 84, 79, 255), 40, 1.1f * s);
        const float a = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 dir(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + dir.x * R * 0.3f, c.y + dir.y * R * 0.3f),
                    ImVec2(c.x + dir.x * R * 0.86f, c.y + dir.y * R * 0.86f),
                    offDefault ? kColAmber : kColWhite, 2.0f * s);

        const bool changed = panel.knob(id, param, d.min, d.max, cx, cy, r, values[param], d.def,
                                        /*stepped*/ false, /*panelTicks*/ false, "%.2f", "",
                                        /*faceColor*/ 0, /*bodyless*/ true, /*persistent*/ false, tip,
                                        /*rightClickReset*/ false, 1.0f, 0.0f, name,
                                        /*contextMenu*/ true, /*overrideText*/ nullptr,
                                        /*hasExternalReadout*/ true);

        spaced(dl, cx, cy - r - 23.0f, 12.0f, kColWhiteDim, label, 0, 0.08f);
        char buf[24];
        if (param == kParamGlobalQ)
            std::snprintf(buf, sizeof(buf), "\xC3\x97%.2f", (double)values[param]);   // multiplication sign
        else
            std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)values[param]);
        const bool nowOff = std::fabs(values[param] - d.def) > 1.0e-4f;
        regularText(dl, cx, cy + r + 9.0f, 13.0f, nowOff ? kColAmber : kColWhiteDim, buf, 0);
        return changed;
    }

    // LINK as a chain between CUT TRIM and OUTPUT (whose rings end at x0 and
    // x1): joined and amber, with a line out to each ring, when on; parted and
    // grey when off.
    bool linkToggle(ImDrawList* dl, float x0, float x1, float cy, bool on)
    {
        const float cx = 0.5f * (x0 + x1);
        ImGui::SetCursorScreenPos(P(cx - 13.0f, cy - 13.0f));
        ImGui::InvisibleButton("##link", ImVec2(26.0f * s, 30.0f * s));
        const bool hov = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        tooltip(on ? "LINK on: raising OUTPUT deepens every notch by the same amount"
                   : "LINK: tie CUT TRIM to OUTPUT, in opposite directions");

        const ImU32 col = on ? kColAmber : (hov ? kColWhiteDim : kColLabel);
        if (on)
        {
            dl->AddLine(P(x0, cy), P(cx - 11.0f, cy), fade(kColAmber, 0.5f), 1.2f * s);
            dl->AddLine(P(cx + 11.0f, cy), P(x1, cy), fade(kColAmber, 0.5f), 1.2f * s);
        }
        const float apart = on ? 3.0f : 7.5f;
        dl->AddRect(P(cx - apart - 5.0f, cy - 3.5f), P(cx - apart + 5.0f, cy + 3.5f), col, 3.5f * s, 0, 1.6f * s);
        dl->AddRect(P(cx + apart - 5.0f, cy - 3.5f), P(cx + apart + 5.0f, cy + 3.5f), col, 3.5f * s, 0, 1.6f * s);
        spaced(dl, cx, cy + 10.0f, 11.0f, col, "LINK", 0, 0.08f);
        return clicked;
    }

    // A follower write: inside the open drag gesture when there is one, opening
    // it on the first change of a drag, or a plain bracketed edit for a change
    // with the mouse up (a wheel step, a typed value).
    void setFollower(uint32_t param, float value)
    {
        if (linkFollowerOpen < 0 && ImGui::IsMouseDown(0))
        {
            editParameter(param, true);
            linkFollowerOpen = (int)param;
        }
        if (linkFollowerOpen == (int)param)
            setParam(param, value);
        else
            setP(param, value);
    }

    // Meters: a click resets the peak hold.
    void drawMeter(ImDrawList* dl, const char* id, duskdaf::LedLadder& meter,
                   float x0, float y0, float x1, float y1, const char* label)
    {
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s, (y1 - y0) * s));
        if (ImGui::IsItemClicked())
            meter.reset(meter.displayDb(0) < meter.displayDb(1) ? meter.displayDb(1) : meter.displayDb(0));
        dl->AddRectFilled(P(x0 - 2, y0 - 2), P(x1 + 2, y1 + 2), kColInset, 2.0f * s);
        meter.draw(panel, dl, x0, y0, x1, y1, meterStyle);
        spaced(dl, 0.5f * (x0 + x1), y1 + 5.0f, 11.0f, kColLabel, label, 0, 0.06f);
        char pk[16];
        std::snprintf(pk, sizeof(pk), "%.0f", (double)std::max(meter.peakDb(), kRtaMinDb));
        regularText(dl, 0.5f * (x0 + x1), y0 - 14.0f, 11.0f, kColLabel, pk, 0);
    }

    //--- parameter plumbing ---------------------------------------------------------
    // One bracketed edit: setParam() (the ParamHost write the knobs use) inside
    // its own begin/end gesture.
    void setP(uint32_t param, float value)
    {
        if (param >= kParamCount || !std::isfinite(value))
            return;
        editParameter(param, true);
        setParam(param, value);
        editParameter(param, false);
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

    // Step through the combo's list (factory, then user presets). Clamped, never
    // wrapping, and never re-applying the preset already loaded: here a preset
    // carries the ring-out itself, so a click past the end of the list must not
    // reload "Default" and wipe the notches. With nothing selected (an edited
    // table) the arrows do nothing; the combo is the explicit choice.
    void stepPreset(int dir)
    {
        const int total = kRoNumFactoryPresets + (int)userPresets.size();
        int current = -1;
        if (currentPreset >= 0)
            current = currentPreset;
        else if (!currentUserPath.empty())
            for (size_t i = 0; i < userPresets.size(); ++i)
                if (userPresets[i].path == currentUserPath) { current = kRoNumFactoryPresets + (int)i; break; }
        if (current < 0)
            return;
        const int next = std::clamp(current + dir, 0, total - 1);
        if (next == current)
            return;
        if (next < kRoNumFactoryPresets)
            applyPreset(next);
        else
            loadUserPreset(userPresets[(size_t)(next - kRoNumFactoryPresets)]);
    }

    using UserPreset = RoUserPreset;

    std::filesystem::path configDir() const { return duskdaf::userPresetDirectory("RingOut"); }

    void scanUserPresets()
    {
        duskdaf::scanUserPresets(configDir(), ".ropreset", userPresets,
                                 [](const std::filesystem::path& path, UserPreset& preset)
                                 { return roReadUserPresetFile(path, preset); });
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
        // A different table: the old row number would now name an unrelated
        // notch, so drop the selection as a factory preset does.
        selected = -1;
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
    RoFilterEditSelection filterEditSelection;
    bool catchHeldLocal = false;
    float catchLeaseTimer = 0.0f;
    ro::Filter pendingSelect;           // the filter "+" added, to re-select after the pull
    int pendingSelectPulls = 0;         // pulls left to find it (the add may still be in flight)
    unsigned engagementsSeen = 0;       // engine engagements already rippled
    int rippleRow = -1;                 // the notch the engine last engaged ...
    uint32_t rippleRowId = 0;           // ... followed by id
    float rippleHz = 0.0f;              // where to ripple if that row is gone
    float rippleSecondsLeft = 0.0f;
    int linkFollowerOpen = -1;          // parameter whose LINK follower gesture is open

    // Direct editing (graph nodes, bank faders, wheel).
    RoNotchDrag notchDrag;
    float dragSentHz = 0.0f, dragSentCut = 0.0f;   // what the drag last wrote
    int dragRow = -1;                   // row being dragged on the graph this frame
    int hoverNode = -1;
    float wheelAccum = 0.0f;
    int wheelTarget = 0;                // see takeWheelSteps()

    // Notch curve cache (updateCurve).
    std::vector<float> curveDb;
    unsigned curveStamp = ~0u;
    float curveGq = 0.0f, curveAmp = 0.0f;
    double curveSr = 0.0;
    bool curveAnyOn = false;
    float curveMinDb = kRoNotchAxisRestDb;       // bottom of the notch axis as drawn
    float curveAxisTarget = kRoNotchAxisRestDb;  // what the table asks for (see syncNotchAxis)

    duskdaf::LogFreqAxis axis { ro::kFreqMin, ro::kFreqMax };
    duskaudio::RingOutSpectrumFrame spectrum;
    unsigned spectrumSeqSeen = 0;
    std::vector<float> rta, rtaTarget;
    std::vector<ImVec2> ptsScratch;

    duskdaf::LedLadder inMeter, outMeter;
    duskdaf::LedLadderStyle meterStyle = [] {
        duskdaf::LedLadderStyle st;
        st.segments = 28; st.gap = 1.2f; st.minDb = -80.0f; st.maxDb = 0.0f;
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
