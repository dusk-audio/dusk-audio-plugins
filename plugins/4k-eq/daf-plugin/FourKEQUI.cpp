// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// FourKEQUI.cpp — Dear ImGui UI for 4K EQ 2, matching the JUCE 4K EQ front
// panel: a full-width response graph over six console channel-strip columns
// (FILTERS | LF | LMF | HMF | HF | MASTER) with per-band colour coding
// (LF red, LMF orange, HMF green, HF blue), INPUT/OUTPUT edge meters, preset
// and oversample selectors, a Brown/Black voicing toggle and Hide Graph.
// All custom ImDrawList rendering in a 960x640 design space, uniformly scaled.
// The response curve is computed from the SAME coefficient math as the audio
// path (FourKEQDSP designers), never by probing audio; a live FFT of the
// pre/post spectrum is drawn behind it.

#include "DafUI.hpp"
#include "FourKEQAccess.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQPresetRuntime.hpp"
#include "FourKEQVersion.hpp"

#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskSupportersOverlay.hpp" // shared DAF Patreon "Special Thanks" overlay
#include "DuskUserPresetStore.hpp"
#include "util/CrashLog.hpp"

#include <numeric> // std::gcd, for the exact-aspect minimum size

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

START_NAMESPACE_DAF

namespace
{
    constexpr float kDesignW = 960.0f;
    constexpr float kDesignH = 640.0f;             // graph shown
    constexpr float kDesignHCollapsed = 516.0f;    // graph hidden (band removed)
    constexpr int kUserPresetFormatVersion = 2;
    constexpr float kDbRange = 20.0f;               // graph vertical: +-20 dB
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;

    // graph + meter rects
    constexpr float GX0 = 9, GY0 = 58, GX1 = 951, GY1 = 174; // outer frame lines up with IN/OUT meter outer edges
    constexpr float INX0 = 8,   INX1 = 34,  MET_Y0 = 246, MET_Y1 = 656;
    constexpr float MET_LBL_Y = 228;  // INPUT/OUTPUT caption, inside the control band
    constexpr float OUTX0 = 926, OUTX1 = 952;
    // column dividers
    constexpr float COL[7] = { 40, 186, 331, 477, 622, 768, 920 };

    // Header band (48 px, ONE row — the uniform Dusk DAF top row, Tape Echo 2
    // reference): nameplate + < preset combo > INIT SAVE + this plugin's option
    // buttons + brand, all centred on one line.
    constexpr float kHdrCy = 24.f;                  // header centreline

    // band face colours
    constexpr ImU32 C_LF_BROWN  = IM_COL32(96, 56, 48, 255);   // British LF maroon knob (Brown/E-series)
    constexpr ImU32 C_LF_BLACK  = IM_COL32(42, 42, 46, 255);   // British LF black knob (Black/G-series)
    constexpr ImU32 C_LMF_BLUE  = IM_COL32(56, 100, 156, 255); // British LMF blue knob
    constexpr ImU32 C_HMF_GREEN = IM_COL32(58, 108, 58, 255);  // British HMF green knob
    constexpr ImU32 C_HF_RED    = IM_COL32(158, 52, 46, 255);  // British HF red knob
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

    // Shared parameter table (FourKEQParams.hpp) so the UI, the DSP shell and
    // the preset files never drift out of sync — one source of truth.
    constexpr float kDefault(uint32_t i) { return kFourKParams[i].def; }

    const int   kGridF[]  = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    const char* kGridFL[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
}

class FourKEQUI : public UI, public duskdaf::ParamHost
{
public:
    FourKEQUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        supportersOverlay.setActionLink("Open crash log folder",
                                        [] { DuskCrashLog::openLogFolder(); });
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = kDefault(i);
        // No hard aspect lock: the Hide Graph toggle changes the window aspect
        // between the shown/collapsed heights, and onImGuiDisplay letterboxes any
        // size cleanly (scale = min ratio), so free resize never distorts.
        updateSizeConstraints();  // aspect-locked resize (no letterbox margins)
        // Multi-size atlas: the bold face at several native sizes spanning the
        // on-screen text range, so each label is drawn near-native (crisp) — one
        // oversized atlas blurred small text by downscaling it 3-5x.
        static const float kFontSizes[] = { 9.f, 11.f, 13.f, 16.f, 20.f, 26.f };
        fontSet = duskdaf::loadCrispFontSet(kFontSizes, 6, getScaleFactor());
        labelFont = fontSet.primary();
        panel.setFontSet(fontSet);
        fft.prepare(kFftSize);
        specDb.assign(kFftSize / 2 + 1, -120.0f);
        scanUserPresets();
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
        // Re-derive the active preset from the current values. Preset identity
        // is UI-only state, so it is lost across a project reload even though
        // the host restores every parameter value; matching the restored values
        // back to a preset recovers the selection. It also clears once an edit
        // diverges from every preset. Gated on the preset params so the
        // per-frame meter outputs never trigger a scan.
        if (fkIsPresetParam(index))
            syncPresetSelection();
    }

    void programLoaded(uint32_t index) override
    {
        currentPreset = index < (uint32_t)kNumFactoryPresets ? (int)index : -1;
        currentUserName.clear();
        currentUserPath.clear();
        if (currentPreset < 0)
            return;
        // A host-driven program change does NOT deliver parameterChanged() for
        // the values it moved — the plugin applied them itself in loadProgram()
        // and only the program index reaches the UI. Refresh the local mirror
        // directly (store only, no write-back: the host already has the values).
        forEachFourKEQFactoryPresetParam(currentPreset,
                                         [this](uint32_t param, float value)
                                         { values[param] = normalizeParamValue(param, value); });
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
        ctlDstTop_ = showGraph ? 180.0f : 56.0f;
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

        // Treat the supporters panel as a modal (the shared Dusk DAF pattern,
        // see Tape Echo 2): background controls remain visible beneath its
        // scrim but cannot receive click-through gestures. Snapshot before
        // drawHeader(), where a title click can open it.
        const bool modalOpen = showSupporters;
        if (modalOpen)
            ImGui::BeginDisabled();
        drawHeader(dl);
        if (showGraph)
            drawGraph(dl);
        drawColumns(dl);
        drawMeters(dl);
        if (modalOpen)
            ImGui::EndDisabled();
        if (showSupporters)
            duskdaf::drawSupportersOverlay(
                panel, dl, kDesignW, designH, showSupporters, "4K EQ 2",
                FOURKEQ2_VERSION_STRING, &supportersOverlay);

        // Own resize grip, submitted LAST so it wins ImGui's hover race (over
        // the credits card too) and paints over everything.
        // AUv2 hosts (Logic) never provide a window grip of their own; on VST3/CLAP
        // the host's grip stays available and this is simply a second way to do the
        // same thing. `designH`, not kDesignH: the design is shorter with the graph
        // hidden, and the grip must drive the same aspect the host is constrained
        // to (see updateSizeConstraints) or the two would fight over the height.
        const duskdaf::ResizeGripState grip =
            panel.resizeGrip(dl, winW, winH, kDesignW, designH);

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

        // Read the width BEFORE the constraints change. Updating the aspect can
        // make a host resize the window underneath us, and the new height has to
        // be derived from the width we actually had, not from whatever the host
        // just did in response to a ratio that no longer matches the old height.
        const uint w = (uint)getWidth();
        const uint h = (uint)std::lround((double)w * designH / kDesignW);

        updateSizeConstraints();
        setSize(w, h);
    }

    // Lock the resize aspect ratio to the current design (graph shown vs hidden)
    // so the host constrains dragging to it — the UI fills the window with no
    // letterbox margins. The onImGuiDisplay uniform scale is the fallback for
    // hosts that ignore the constraint.
    void updateSizeConstraints()
    {
        const float designH = showGraph ? kDesignH : kDesignHCollapsed;
        const uint minW = minWidthOnDesignRatio(designH);
        const uint minH = (uint)std::lround((double)minW * designH / kDesignW);
        setGeometryConstraints(minW, minH, /*keepAspectRatio*/ true);
    }

    // The aspect a host is told to preserve comes from the SAME pair of numbers as
    // the minimum size: puglSetGeometryConstraints stores (width, height) as
    // PUGL_MIN_SIZE and, with keepAspectRatio, as PUGL_FIXED_ASPECT as well, and
    // X11 advertises both out of that one pair. A minimum that is not exactly on
    // the design ratio therefore advertises the WRONG aspect.
    //
    // A 560 minimum did exactly that with the graph shown: 560 x lround(560*640/960)
    // is 560x373, an aspect of 1.50134 rather than the design 1.5. A host honouring
    // it resolves a 960-wide window to 639 tall, so the window sits a pixel off the
    // design; and when the graph is hidden the advertised ratio changes, so a host
    // that keeps the current height and derives the width from the new aspect
    // inflates the window well past its visible frame (639 * 1.86047 = 1189). The
    // editor then draws 1189 px of design into 960 px of window and the right-hand
    // column is cut off, which is what was reported against v1.0.3.
    //
    // Pick the minimum width that puts minH exactly on the design ratio: reduce
    // designH/kDesignW and round the target to the nearest multiple of the
    // denominator. 640/960 reduces to 2/3, so the width must be a multiple of 3
    // (561, minH 374); 516/960 reduces to 43/80, so a multiple of 80 (560, minH
    // 301, which is why the collapsed state was already exact and only the graph
    // state drifted).
    static uint minWidthOnDesignRatio(const float designH)
    {
        constexpr int kMinWidthTarget = 560;
        const int step = (int)kDesignW / std::gcd((int)kDesignW, (int)designH);
        const int nearest = (int)std::lround((double)kMinWidthTarget / step) * step;
        return (uint)std::max(step, nearest);
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
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 48), kHeader);
        dl->AddRectFilled(panel.P(0, 0), panel.P(kDesignW, 3), IM_COL32(150, 150, 152, 255));
        dl->AddLine(panel.P(0, 48), panel.P(kDesignW, 48), IM_COL32(60, 60, 63, 255), 1.5f * sc());

        // One row, the uniform Dusk DAF top row (Tape Echo 2 reference):
        // nameplate [TITLE  BADGE  vX.Y.Z], < [preset combo] > INIT SAVE, this
        // plugin's option buttons, brand — everything centred on kHdrCy.
        constexpr float kPlateX0 = 28.f, kPlateX1 = 222.f;
        constexpr float kPlateY0 = 8.f,  kPlateY1 = 40.f;
        dl->AddRectFilledMultiColor(
            panel.P(kPlateX0, kPlateY0), panel.P(kPlateX1, kPlateY1),
            IM_COL32(37, 37, 38, 255), IM_COL32(29, 29, 30, 255),
            IM_COL32(16, 16, 17, 255), IM_COL32(19, 19, 20, 255));
        dl->AddRect(panel.P(kPlateX0, kPlateY0), panel.P(kPlateX1, kPlateY1),
                    IM_COL32(185, 184, 180, 220), 3.5f * sc(), 0, 1.2f * sc());
        // Product, model and version share one bottom baseline; the amber rule
        // ties the identity block together across the width of the nameplate.
        dl->AddLine(panel.P(38, 36), panel.P(212, 36), IM_COL32(178, 118, 44, 210), 1.2f * sc());
        panel.text(dl, 40, 12, 22, pal().white, "4K-2 EQ", -1, true);
        panel.text(dl, 210, 21, 13, IM_COL32(160, 162, 166, 255), "v" FOURKEQ2_VERSION_STRING, 1);
        // Clickable nameplate -> Patreon supporters overlay.
        ImGui::SetCursorScreenPos(panel.P(kPlateX0, kPlateY0));
        ImGui::InvisibleButton("titlecredits",
                               ImVec2((kPlateX1 - kPlateX0) * sc(), (kPlateY1 - kPlateY0) * sc()));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked())
        {
            supportersOverlay.resetInteraction();
            showSupporters = true;
        }

        panel.text(dl, 878, 14, 20, pal().white, "DUSK AUDIO", 0, true);

        // Preset browser: < [combo] > INIT SAVE, centred on the nameplate's
        // centre line. Horizontal rhythm matches Tape Echo 2: 4 px inside the
        // < combo > group, 8 px before INIT (group break), 6 px INIT-to-SAVE.
        constexpr float kBandY0 = kHdrCy - 12.5f, kBandY1 = kHdrCy + 12.5f;
        constexpr float kBandH  = kBandY1 - kBandY0;
        if (chevron(dl, "##presetprev", 244.5f, kHdrCy, 0.5f * kBandH, true))
            stepPreset(-1);
        if (chevron(dl, "##presetnext", 421.5f, kHdrCy, 0.5f * kBandH, false))
            stepPreset(1);

        ImGui::SetCursorScreenPos(panel.P(258, kBandY0));
        ImGui::SetNextItemWidth(150.0f * sc());
        // Crisp combo text: a native-size face from the shared atlas, and a
        // frame height locked to the 26 px band (ImGui's default is font-size
        // driven and would drift; padY centres the preview text in the frame).
        ImFont* presetFont = panel.pickFont(13.0f * sc());
        if (presetFont != nullptr)
            ImGui::PushFont(presetFont);
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(6.0f * sc(),
                   std::max(0.0f, 0.5f * (kBandH * sc() - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(70, 90, 120, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(86, 108, 140, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(58, 76, 104, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(46, 46, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(58, 58, 62, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 40, 44, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(228, 228, 224, 255));
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(120, 150, 200, 255));
        const char* preview = (currentPreset >= 0 && currentPreset < kNumFactoryPresets)
                                  ? kFactoryPresets[currentPreset].name
                                  : (!currentUserName.empty() ? currentUserName.c_str()
                                                              : "Default");
        // BeginCombo otherwise caps its popup at eight entries. An explicit
        // identity constraint makes it auto-fit all factory presets, so the
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
        // on the same band as the combo.
        if (textButton(dl, "##init", 439, kBandY0, 481, kBandY1, "INIT"))
            initDefaults();
        if (textButton(dl, "##save", 487, kBandY0, 529, kBandY1, "SAVE"))
        {
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", currentUserName.c_str());
            ImGui::OpenPopup("Save Preset");
        }
        drawSaveModal();

        // Option buttons, kept from v2.0 with compact labels so the whole
        // header stays one row: 12 px group break after SAVE, 8 px rhythm,
        // clear of the right-aligned brand text.
        // Oversample (cycles 1x/2x/4x).
        static const char* kOsBtn[3] = { "OS: 1x", "OS: 2x", "OS: 4x" };
        int osi = (int)(values[kOversampling] + 0.5f); osi = osi < 0 ? 0 : (osi > 2 ? 2 : osi);
        headerButton(dl, "os", 541, kBandY0, 589, kBandY1, kOsBtn[osi], false,
                     [&]{ cycleParam(kOversampling, 3); });

        // Brown / Black is the sole coloured button: brown for E-series and
        // true black (not the old blue-grey) for G-series.
        const bool brown = values[kEqType] < 0.5f;
        headerButton(dl, "eqtype", 597, kBandY0, 649, kBandY1, brown ? "BROWN" : "BLACK",
                     false, [&]{ cycleParam(kEqType, 2); },
                     true, brown ? kAmber : IM_COL32(12, 12, 14, 255));

        // --- Analyzer group: Graph collapse | FFT on/off | spectrum source ---
        headerButton(dl, "hidegraph", 657, kBandY0, 707, kBandY1, "GRAPH",
                     showGraph,
                     [&]{ toggleGraph(); });

        headerButton(dl, "fft", 715, kBandY0, 755, kBandY1, "FFT",
                     showFft,
                     [&]{ showFft = !showFft; });

        headerButton(dl, "prepost", 763, kBandY0, 807, kBandY1,
                     values[kSpectrumPrePost] > 0.5f ? "PRE" : "POST",
                     values[kSpectrumPrePost] > 0.5f,
                     [&]{ toggleParam(kSpectrumPrePost); });
    }

    // Small chevron button ("<" / ">") for stepping the preset combo. Fill and
    // hover match the combo's FrameBg so the < combo > group reads as one control.
    bool chevron(ImDrawList* dl, const char* id, float cx, float cy,
                 float halfH, bool left)
    {
        const float s = sc();
        const ImVec2 b0 = panel.P(cx - 9.5f, cy - halfH);
        const ImVec2 b1 = panel.P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        drawSilverButtonFace(dl, b0, b1, false, hov, 2.0f);
        const ImVec2 c = panel.P(cx, cy);
        const float  d = 4.3f * s;
        const ImU32 ink = IM_COL32(34, 34, 38, 255);
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
    // flank the preset combo.
    bool textButton(ImDrawList* dl, const char* id, float x0, float y0,
                    float x1, float y1, const char* label)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        drawSilverButtonFace(dl, b0, b1, false, hov, 2.0f);
        // panel.text takes the top edge, so centre the 11 px label explicitly.
        constexpr float kTxt = 11.0f;
        panel.text(dl, 0.5f * (x0 + x1), 0.5f * (y0 + y1 - kTxt), kTxt,
                   IM_COL32(34, 34, 38, 255), label, 0, true);
        return ImGui::IsItemClicked();
    }

    // Name-entry modal for SAVE. Dark styling to match this chassis; Enter or the
    // Save button commits, Escape / Cancel dismisses.
    void drawSaveModal()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(228, 228, 224, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(90, 90, 94, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(40, 40, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 50, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(56, 56, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 90, 120, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(86, 108, 140, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(58, 76, 104, 255));
        if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool popupAppearing = ImGui::IsWindowAppearing();
            if (popupAppearing)
                saveFailed = false;
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(240.0f * sc());
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
                // Only dismiss on a save that actually wrote a file; a failed
                // write closing the dialog would read as success.
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

    template <class Fn>
    void headerButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                      const char* label, bool pressed, Fn onClick,
                      bool customColour = false, ImU32 customBg = 0)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        if (customColour)
        {
            dl->AddRectFilled(b0, b1, customBg, 4.0f * sc());
            dl->AddRect(b0, b1, hov ? IM_COL32(220, 220, 216, 230)
                                     : IM_COL32(90, 90, 94, 220),
                        4.0f * sc(), 0, 1.2f * sc());
        }
        else
        {
            drawSilverButtonFace(dl, b0, b1, pressed, hov, 4.0f);
        }
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 10.5f,
                   customColour ? IM_COL32(245, 240, 232, 255)
                                : IM_COL32(34, 34, 38, 255),
                   label, 0, true);
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

    //========================================================================
    // presets — factory recall, INIT/SAVE, user preset library, identity
    //========================================================================

    // Clamp to range and quantise the discrete parameters exactly the way the
    // DSP shell reads them, so the cached value can never disagree with what
    // the DSP acts on. values[] is not display-only: it feeds preset identity
    // matching and is what a saved user preset writes to disk.
    static float normalizeParamValue(uint32_t idx, float v) noexcept
    {
        v = std::max(kFourKParams[idx].min, std::min(kFourKParams[idx].max, v));
        switch (idx)
        {
        case kHpfEnabled: case kLpfEnabled: case kLfBell: case kHfBell:
        case kEqType: case kBypass: case kMsMode: case kSpectrumPrePost:
        case kAutoGain: case kShowGraph:
            // Folded to an exact 0/1 so the DSP shell's > 0.5f reads can never
            // disagree with this cache about which side a boundary value took.
            return v >= 0.5f ? 1.0f : 0.0f;
        case kOversampling:
            return std::round(v);
        default:
            return v;
        }
    }

    // Single write path for a preset-driven parameter change: normalises, keeps
    // the local cache in step and brackets the host write with edit markers.
    void setP(uint32_t param, float value)
    {
        if (param >= kParamCount || !std::isfinite(value))
            return;
        value = normalizeParamValue(param, value);
        values[param] = value;
        editParameter(param, true);
        setParameterValue(param, value);
        editParameter(param, false);
    }

    void applyPreset(int idx)
    {
        if (idx < 0 || idx >= kNumFactoryPresets) return;
        currentPreset = idx;
        currentUserName.clear();
        currentUserPath.clear();
        forEachFourKEQFactoryPresetParam(idx, [this](uint32_t param, float value)
                                             { setP(param, value); });
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

    // Reset every preset-owned control to its factory default (the "Default"
    // state the combo names when nothing is selected). BYPASS, oversampling and
    // the analyzer/graph state are left alone — see fkIsPresetParam.
    void initDefaults()
    {
        currentPreset = -1;
        currentUserName.clear();
        currentUserPath.clear();
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fkIsPresetParam(i))
                setP(i, kFourKParams[i].def);
        // Re-derive rather than assuming: a user preset saved at the defaults
        // is a recognised state and should read as such in the combo.
        syncPresetSelection();
    }

    //--- user preset file library (~/.config/DuskAudio/FourKEQ2/presets) --------
    std::filesystem::path configDir() const
    {
        return duskdaf::userPresetDirectory("FourKEQ2");
    }

    // Strict field parse, shared by the library scan and the loader so a file
    // can never mean two things. atof() reports failure as 0.0 and happily
    // yields NaN/inf for "nan"/"1e999", all of which would reach the DSP through
    // setP(); require the whole field to be one finite float. Range clamping is
    // done after the file's frequency domain is known: effective LPF values can
    // legitimately exceed the legacy host parameter's 15.201 kHz end stop.
    //
    // Locale-independent on purpose, in both directions (saveUserPreset() imbues
    // the same classic locale): plugin hosts do call setlocale(), and a
    // comma-decimal locale makes strtod() stop at the '.' in "0.5" — every value
    // in every preset file would silently read as its default.
    static bool parsePresetNumber(const std::string& line, std::size_t valueStart,
                                  float& out)
    {
        std::istringstream field(line.substr(valueStart));
        field.imbue(std::locale::classic());
        double d = 0.0;
        field >> d;
        if (field.fail() || !std::isfinite(d)
            || std::abs(d) > (double)std::numeric_limits<float>::max())
            return false;
        char trailing = '\0';
        if (field >> trailing)                   // trailing junk: not a number
            return false;
        out = (float)d;
        return true;
    }

    // Reads both legacy files (no frequency_domain line: raw/control Hz) and
    // v2 files (effective_hz). The returned array is always in the INTERNAL
    // host-parameter domain so preset identity and setP() see one representation.
    static bool readUserPresetFile(const std::string& path, std::string& name,
                                   float (&out)[kParamCount])
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            out[i] = kFourKParams[i].def;
        bool present[kParamCount] = {};
        bool effectiveHz = false;
        bool supportedDomain = true;

        std::ifstream f(path);
        if (!f)
            return false;
        std::string line;
        while (std::getline(f, line))
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = line.substr(0, eq);
            const std::string field = line.substr(eq + 1);
            if (key == "name") { name = field; continue; }
            if (key == "frequency_domain")
            {
                effectiveHz = field == "effective_hz";
                supportedDomain = effectiveHz || field == "control_hz";
                continue;
            }
            if (key == "format_version")
            {
                float version = 0.0f;
                if (!parsePresetNumber(line, eq + 1, version)
                    || version != (float)kUserPresetFormatVersion)
                    return false;
                continue;
            }
            for (uint32_t i = 0; i < kParamCount; ++i)
                if (fkIsPresetParam(i) && key == kFourKParams[i].key)
                {
                    float v = 0.0f;
                    if (!parsePresetNumber(line, eq + 1, v))
                        return false;
                    out[i] = v;
                    present[i] = true;
                    break;
                }
        }
        if (!supportedDomain)
            return false;

        // Normalise mode/gain/shape first: those values select the inverse
        // frequency law used below, regardless of their order in the file.
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (present[i] && !isFrequencyParam(i))
                out[i] = normalizeParamValue(i, out[i]);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (present[i] && isFrequencyParam(i))
                out[i] = normalizeParamValue(
                    i, effectiveHz ? controlForEffectiveFrequency(i, out[i], out)
                                   : out[i]);
        return true;
    }

    void scanUserPresets()
    {
        duskdaf::scanUserPresets(
            configDir(), ".4kpreset", userPresets,
            [](const std::filesystem::path&, UserPreset& preset) {
                return readUserPresetFile(preset.path, preset.name, preset.vals);
            });
    }

    // Returns false if nothing was written, so the caller can keep the dialog
    // open and say so instead of closing on a save that silently did nothing.
    bool saveUserPreset(const char* rawName)
    {
        const auto saved = duskdaf::writeUserPreset(
            configDir(), ".4kpreset", rawName,
            [this](std::ostream& output) {
                output << "format_version=" << kUserPresetFormatVersion << '\n';
                output << "frequency_domain=effective_hz\n";
                output << std::setprecision(std::numeric_limits<float>::max_digits10);
                for (uint32_t i = 0; i < kParamCount; ++i)
                    if (fkIsPresetParam(i))
                        output << kFourKParams[i].key << '='
                               << (isFrequencyParam(i) ? displayValue(i) : values[i])
                               << '\n';
            });
        if (!saved)
            return false;
        scanUserPresets();
        currentPreset = -1;
        currentUserName = saved.name;
        currentUserPath = saved.path;
        return true;
    }

    void loadUserPreset(const std::string& path, const std::string& name)
    {
        float loaded[kParamCount];
        std::string storedName = name;
        if (!readUserPresetFile(path, storedName, loaded))
            return;
        // Default every parameter first, then overlay the file's values. This
        // matches the cache built in scanUserPresets() (missing keys -> table
        // default), so an incomplete file loads to the exact values its cached
        // identity records - otherwise a missing key would keep the current
        // value and deriveUserPreset() could never match.
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fkIsPresetParam(i))
                setP(i, loaded[i]);
        currentPreset = -1;
        currentUserName = name;
        currentUserPath = path;
    }

    //--- preset identity recovery -------------------------------------------
    // The active preset is UI-only state; these re-derive it from the current
    // parameter values so a project reload (which restores parameters but not
    // the selection) shows the right preset again. Compares with a
    // range-relative tolerance to absorb host parameter quantisation.
    bool paramMatches(uint32_t id, float v) const
    {
        const FourKParam& d = kFourKParams[id];
        const float tol = std::max(1.0e-3f, (d.max - d.min) * 1.0e-4f);
        return std::fabs(values[id] - v) <= tol;
    }

    int deriveFactoryPreset() const
    {
        for (int idx = 0; idx < kNumFactoryPresets; ++idx)
        {
            bool ok = true;
            forEachFourKEQFactoryPresetParam(idx, [&](uint32_t id, float v) {
                if (ok && fkIsPresetParam(id) && !paramMatches(id, v))
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
                        if (fkIsPresetParam(id) && !paramMatches(id, userPresets[i].vals[id]))
                            ok = false;
                    if (ok)
                        return (int)i;
                    break;
                }

        for (size_t i = 0; i < userPresets.size(); ++i)
        {
            bool ok = true;
            for (uint32_t id = 0; id < kParamCount && ok; ++id)
                if (fkIsPresetParam(id) && !paramMatches(id, userPresets[i].vals[id]))
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

    //========================================================================
    // response graph + spectrum
    //========================================================================
    duskaudio::FourKEQDSP::CurveControls curveControls() const
    {
        duskaudio::FourKEQDSP::CurveControls c;
        c.baseSampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        c.oversampling = values[kOversampling];
        c.black        = values[kEqType] > 0.5f;
        c.hpfEnabled   = values[kHpfEnabled] > 0.5f;
        c.lpfEnabled   = values[kLpfEnabled] > 0.5f;
        c.hpfFreq      = values[kHpfFreq];
        c.lpfFreq      = values[kLpfFreq];
        c.lfGain = values[kLfGain]; c.lfFreq = values[kLfFreq]; c.lfBell = values[kLfBell];
        c.lmGain = values[kLmGain]; c.lmFreq = values[kLmFreq]; c.lmQ    = values[kLmQ];
        c.hmGain = values[kHmGain]; c.hmFreq = values[kHmFreq]; c.hmQ    = values[kHmQ];
        c.hfGain = values[kHfGain]; c.hfFreq = values[kHfFreq]; c.hfBell = values[kHfBell];
        // NOT values[kSaturation]. That index is a retired compatibility slot
        // kept so existing sessions do not remap; FourKEQPlugin.cpp answers it
        // with dsp.setSaturation(0.0f), so the standalone 4K DSP always runs at
        // the native drive. Drawing the slot's value would put a curve on screen
        // that the audio path never produces.
        c.saturation = 0.0f;
        return c;
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
        // Design the curve's sections ONCE, then evaluate per point: everything
        // except the magnitude lookup is identical at all N frequencies.
        const auto curve = duskaudio::FourKEQDSP::designCurve(curveControls());
        for (int i = 0; i < N; ++i)
        {
            const float lx = (float)i / (N - 1);
            const float freq = std::pow(10.0f, std::log10(kFMin) + lx * (std::log10(kFMax) - std::log10(kFMin)));
            float ny = dbToNy(duskaudio::FourKEQDSP::curveDbAt(curve, freq));
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
       #if DAF_PLUGIN_WANT_DIRECT_ACCESS
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

        // FILTERS — British-style stepped HPF & LPF (OUT folds in each enable).
        // Values are the hosted UAD/LUNA dial readbacks.
        static const char* const HPFL[7] = { "OUT", "16", "45", "120", "250", "320", "350" };
        static const float        HPFF[7] = { 16.f, 16.f, 45.f, 120.f, 250.f, 320.f, 350.f };
        static const char* const LPFL[7] = { "OUT", "15.2", "10", "5", "3.75", "3.3", "3" };
        static const float        LPFF[7] = { 15201.f, 15201.f, 10000.f, 5000.f, 3750.f, 3300.f, 3000.f };
        const float fcx = 0.5f * (COL[0] + COL[1]);
        steppedFilterKnob(dl, "hpfknob", fcx, cY(314), 28.f, kHpfEnabled, kHpfFreq, HPFL, HPFF, false, "Hz");
        steppedFilterKnob(dl, "lpfknob", fcx, cY(452), 28.f, kLpfEnabled, kLpfFreq, LPFL, LPFF, true,  "kHz");

        // Shared GAIN (0 top, +-15 dB) and Q (.5-3 descending) detent tables.
        static const float GT[11] = { 0.f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1.f };
        static const float GV[11] = { -15.f, -12.f, -9.f, -6.f, -3.f, 0.f, 3.f, 6.f, 9.f, 12.f, 15.f };
        static const char* const GL[11] = { "-15", "12", "9", "6", "3", "0", "3", "6", "9", "12", "+15" };
        static const float QT[5] = { 0.f, .25f, .5f, .75f, 1.f };
        static const float QV[5] = { 3.f, 2.25f, 1.5f, .88f, .5f };
        static const char* const QL[5] = { "3", "2.25", "1.5", ".88", ".5" };
        static const float FT7[7] = { 0.f, .1f, .25f, .5f, .75f, .9f, 1.f };

        // LF — British brown band: GAIN + FREQ (30-450 Hz) + BELL/SHELF button.
        // The LF caps signal the voicing like the consoles they emulate: maroon
        // on Brown (E-series), black on Black (G-series).
        {
            const float lcx = 0.5f * (COL[1] + COL[2]);
            static const float FT[7] = { 0.f, .1f, .25f, .5f, .75f, .9f, 1.f };
            static const float FV[7] = { 30.f, 42.f, 75.f, 200.f, 338.f, 405.f, 450.f };
            static const char* const FL[7] = { "30", "42", "75", "200", "338", "405", "450" };
            const ImU32 lfCap = values[kEqType] > 0.5f ? C_LF_BLACK : C_LF_BROWN;
            consoleDetentKnob(dl, "lfg", lcx, cY(314), 28.f, kLfGain, GT, GV, GL, 11, lfCap, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lff", lcx, cY(452), 28.f, kLfFreq, FT, FV, FL, 7, lfCap, "Hz", "%.0f Hz", true);
            metalButton(dl, "lfbell", lcx - 32.f, cY(560), lcx + 32.f, cY(584), kLfBell, "BELL", "SHELF");
        }
        // LMF — British blue band: GAIN + FREQ (.2-2.5 kHz, 1 at top) + Q
        // (.5-3, descending), with narrow/wide bandwidth symbols under Q.
        {
            const float mcx = 0.5f * (COL[2] + COL[3]);
            static const float MFV[7] = { 200.f, 260.f, 550.f, 1000.f, 1750.f, 2200.f, 2500.f };
            static const char* const MFL[7] = { ".2", ".26", ".55", "1", "1.75", "2.2", "2.5" };
            consoleDetentKnob(dl, "lmg", mcx, cY(314), 28.f, kLmGain, GT, GV, GL, 11, C_LMF_BLUE, "dB", "%.1f dB");
            consoleDetentKnob(dl, "lmf", mcx, cY(452), 28.f, kLmFreq, FT7, MFV, MFL, 7, C_LMF_BLUE, "kHz", "%.0f Hz", true);
            consoleDetentKnob(dl, "lmq", mcx, cY(590), 28.f, kLmQ, QT, QV, QL, 5, C_LMF_BLUE, "", "Q %.2f");
            bandwidthIcons(dl, mcx, cY(590) + 40.f);
        }
        // HMF — British green band: GAIN + FREQ (.6-7 kHz, 3 at top) + Q.
        {
            const float hcx = 0.5f * (COL[3] + COL[4]);
            static const float HFV[7] = { 600.f, 720.f, 1150.f, 3000.f, 5250.f, 6400.f, 7000.f };
            static const char* const HFL[7] = { ".6", ".72", "1.15", "3", "5.25", "6.4", "7" };
            consoleDetentKnob(dl, "hmg", hcx, cY(314), 28.f, kHmGain, GT, GV, GL, 11, C_HMF_GREEN, "dB", "%.1f dB");
            consoleDetentKnob(dl, "hmf", hcx, cY(452), 28.f, kHmFreq, FT7, HFV, HFL, 7, C_HMF_GREEN, "kHz", "%.0f Hz", true);
            consoleDetentKnob(dl, "hmq", hcx, cY(590), 28.f, kHmQ, QT, QV, QL, 5, C_HMF_GREEN, "", "Q %.2f");
            bandwidthIcons(dl, hcx, cY(590) + 40.f);
        }
        // HF — British red band: GAIN + FREQ (1.5-16 kHz, 8 at top) + BELL/SHELF.
        {
            const float hcx = 0.5f * (COL[4] + COL[5]);
            static const float XFV[7] = { 1500.f, 1800.f, 3500.f, 8000.f, 12000.f, 14800.f, 16000.f };
            static const char* const XFL[7] = { "1.5", "1.8", "3.5", "8", "12", "14.8", "16" };
            consoleDetentKnob(dl, "hfg", hcx, cY(314), 28.f, kHfGain, GT, GV, GL, 11, C_HF_RED, "dB", "%.1f dB");
            consoleDetentKnob(dl, "hff", hcx, cY(452), 28.f, kHfFreq, FT7, XFV, XFL, 7, C_HF_RED, "kHz", "%.0f Hz", true);
            metalButton(dl, "hfbell", hcx - 32.f, cY(560), hcx + 32.f, cY(584), kHfBell, "BELL", "SHELF");
        }

        // MASTER
        const float mcx = 0.5f * (COL[5] + COL[6]);
        // Keep the master gain stages on the same two row centres as the HF
        // gain/frequency knobs, then group the master switches beneath them.
        colMetalKnob(dl, "input", kInputGain, -12.f, 12.f, mcx, cY(314), 26, "INPUT", MK_GAIN_V, MK_GAIN_L, 5, "%.1f", " dB");
        colMetalKnob(dl, "outg", kOutputGain, -12.f, 12.f, mcx, cY(452), 26, "OUTPUT", MK_GAIN_V, MK_GAIN_L, 5, "%.1f", " dB");
        panelButton(dl, "bypass", mcx - 40, cY(544), mcx + 40, cY(568),
                    values[kBypass] > 0.5f ? "BYPASSED" : "BYPASS",
                    values[kBypass] > 0.5f,
                    [&]{ toggleParam(kBypass); });
        panelButton(dl, "autogain", mcx - 40, cY(576), mcx + 40, cY(600), "AUTO GAIN",
                    values[kAutoGain] > 0.5f,
                    [&]{ toggleParam(kAutoGain); });
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
        panel.knob(id, param, minV, maxV, cx, cy, r, values[param], kDefault(param),
                   false, false, fmt, suffix, face);
        // dial-end tick labels
        const float a0 = duskdaf::DuskPanel::knobAngle(0.0f), a1 = duskdaf::DuskPanel::knobAngle(1.0f);
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
            const float a = duskdaf::DuskPanel::knobAngle(tt);
            const float dx = std::sin(a), dy = -std::cos(a);
            dl->AddCircleFilled(panel.P(cx + dx * (r + 9.f), cy + dy * (r + 9.f)), 1.6f * sc(), IM_COL32(150, 152, 156, 255), 8);
            const int align = dx < -0.25f ? 1 : (dx > 0.25f ? -1 : 0);
            panel.text(dl, cx + dx * (r + 20.f), cy + dy * (r + 20.f) - 5.f, 10.5f, IM_COL32(206, 208, 212, 255), TL[i], align, true);
        }
        panel.text(dl, cx, cy + r + 10.f, 11.0f, IM_COL32(206, 208, 212, 255), name, 0, true);
        // gestures + value read-out on top (no body drawn)
        panel.knob(id, param, minV, maxV, cx, cy, r, values[param], kDefault(param),
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
        drawSilverButtonFace(dl, b0, b1, on, ImGui::IsItemHovered(), 3.0f);
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.28f * (y1 - y0), 9.0f,
                   IM_COL32(34, 34, 38, 255), label, 0, true);
    }

    template <class Fn>
    void panelButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     const char* label, bool pressed, Fn onClick)
    {
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) onClick();
        drawSilverButtonFace(dl, b0, b1, pressed, hov, 4.0f);
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 10.0f,
                   IM_COL32(34, 34, 38, 255), label, 0, true);
    }

    //========================================================================
    // Calibrated frequency read-outs. The panel legends keep the reference's
    // printed dial values, but the live value displays (hover/drag bubble and
    // the double-click editor) show the frequency the section actually centres
    // on — the same measured law the DSP and the response curve use, so the
    // read-out always agrees with the graph and the FFT. Display-only: the
    // parameter values, the automation domain and the emulation are untouched.
    //========================================================================
    float calibratedFreqFor(uint32_t paramId, float dialHz) const
    {
        using duskaudio::FourKEQDSP;
        const bool black = values[kEqType] > 0.5f;
        switch (paramId)
        {
        case kHpfFreq:
            return FourKEQDSP::calibratedFilterFrequency(dialHz, true, black);
        case kLpfFreq:
            return FourKEQDSP::calibratedFilterFrequency(dialHz, false, black);
        case kLfFreq:
            return FourKEQDSP::calibratedEqFrequency(
                dialHz, values[kLfGain], FourKEQDSP::Band::LF, black,
                values[kLfBell] > 0.5f);
        case kLmFreq:
            return FourKEQDSP::calibratedEqFrequency(
                dialHz, values[kLmGain], FourKEQDSP::Band::LM, black, true);
        case kHmFreq:
            return FourKEQDSP::calibratedEqFrequency(
                dialHz, values[kHmGain], FourKEQDSP::Band::HM, black, true);
        case kHfFreq:
            return FourKEQDSP::calibratedEqFrequency(
                dialHz, values[kHfGain], FourKEQDSP::Band::HF, black,
                values[kHfBell] > 0.5f);
        default:
            return dialHz;   // not a frequency control: display = dial value
        }
    }

    float displayValue(uint32_t paramId) const
    {
        return calibratedFreqFor(paramId, values[paramId]);
    }

    static bool isFrequencyParam(uint32_t paramId)
    {
        switch (paramId)
        {
        case kHpfFreq: case kLpfFreq:
        case kLfFreq: case kLmFreq: case kHmFreq: case kHfFreq:
            return true;
        default:
            return false;
        }
    }

    static float controlForEffectiveFrequency(uint32_t paramId, float target,
                                              const float* state) noexcept
    {
        using duskaudio::FourKEQDSP;
        const bool black = state[kEqType] > 0.5f;
        switch (paramId)
        {
        case kHpfFreq:
            return FourKEQDSP::controlForCalibratedFilterFrequency(target, true, black);
        case kLpfFreq:
            return FourKEQDSP::controlForCalibratedFilterFrequency(target, false, black);
        case kLfFreq:
            return FourKEQDSP::controlForCalibratedEqFrequency(
                target, state[kLfGain], FourKEQDSP::Band::LF, black,
                state[kLfBell] > 0.5f);
        case kLmFreq:
            return FourKEQDSP::controlForCalibratedEqFrequency(
                target, state[kLmGain], FourKEQDSP::Band::LM, black, true);
        case kHmFreq:
            return FourKEQDSP::controlForCalibratedEqFrequency(
                target, state[kHmGain], FourKEQDSP::Band::HM, black, true);
        case kHfFreq:
            return FourKEQDSP::controlForCalibratedEqFrequency(
                target, state[kHfGain], FourKEQDSP::Band::HF, black,
                state[kHfBell] > 0.5f);
        default:
            return target;
        }
    }

    // Inverse of calibratedFreqFor for typed entry: the dial position whose
    // ACTUAL frequency is `target`. This is the same core inverse used by
    // factory and versioned user presets.
    float dialForCalibrated(uint32_t paramId, float target) const
    {
        return controlForEffectiveFrequency(paramId, target, values);
    }

    //========================================================================
    // British-style stepped filter rotary (HPF & LPF): OUT + 6 labelled Hz detents,
    // dots + labels around a brushed-metal knob, vertical pointer, rolloff icon.
    // The OUT position folds in the filter enable (no separate IN switch). F[]
    // is t-indexed (F[0] == F[1]); frequencies may ascend (HPF) or descend (LPF).
    //========================================================================
    static float stepLT(int i) { static const float t[7] = {0.f, .1f, .25f, .5f, .75f, .9f, 1.f}; return t[i]; }

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
        if (en)
        {
            // F[] is the frequency printed around the bezel. Convert that
            // effective corner back to the fitted UAD control coordinate before
            // handing it to the DSP, so the pointer and response agree.
            f = normalizeParamValue(freqId, dialForCalibrated(freqId, f));
            values[freqId] = f;
            setParameterValue(freqId, f);
        }
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
        // Position the pointer by the measured corner, not by the hidden fitted
        // control coordinate. For example, a Brown HPF control value of 120 Hz
        // actually turns over near 85 Hz and must point between those markings.
        float t = stepStateToPos(F, en, displayValue(freqId));

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
            if (!modKey && (hov || act) && ImGui::IsMouseDoubleClicked(0)) // double-click: type a frequency (actual Hz)
            { panel.openValueEdit(id, displayValue(freqId)); editParameter(enId, false); editParameter(freqId, false); }
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
            const float a = duskdaf::DuskPanel::knobAngle(stepLT(i));
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
            // Typed frequency is the ACTUAL corner: clamp it in that display
            // domain, map back to the dial law, then normalize the control.
            float lo = F[1], hi = F[1];
            for (int i = 2; i <= 6; ++i) { lo = std::min(lo, F[i]); hi = std::max(hi, F[i]); }
            typed = typed < lo ? lo : (typed > hi ? hi : typed);
            typed = normalizeParamValue(freqId, dialForCalibrated(freqId, typed));
            editParameter(enId, true); editParameter(freqId, true);
            values[enId] = 1.f;    setParameterValue(enId, 1.f);
            values[freqId] = typed; setParameterValue(freqId, typed);
            editParameter(freqId, false); editParameter(enId, false);
        }
        else if ((hov || act) && !editing)
        {
            char buf[24];
            const float shown = displayValue(freqId);
            if (!en) std::snprintf(buf, sizeof(buf), "OUT");
            else if (shown >= 1000.f) std::snprintf(buf, sizeof(buf), "%.1f kHz", shown / 1000.f);
            else std::snprintf(buf, sizeof(buf), "%.0f Hz", shown);
            panel.valueBubble(cx, cy, R, buf);
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
            const float a = (float)i / 20.f * 2.f * duskdaf::DuskPanel::kPi;
            const ImVec2 d(std::sin(a), -std::cos(a));
            dl->AddLine(ImVec2(c.x + d.x * RR * 0.80f, c.y + d.y * RR * 0.80f),
                        ImVec2(c.x + d.x * RR * 0.93f, c.y + d.y * RR * 0.93f), IM_COL32(40, 40, 43, 160), 1.3f * s);
        }
        const float capR = RR * 0.74f;
        dl->AddCircleFilled(c, capR, capCol, 44);
        dl->PushClipRect(ImVec2(c.x - capR, c.y - capR), ImVec2(c.x + capR, c.y + capR), true);
        // Matte console cap: a gentle top-to-bottom shade (top a touch lighter,
        // bottom a touch darker) for roundness; the cap colour stays essentially
        // unchanged. Low-alpha discs centred just off the cap fade before centre.
        dl->AddCircleFilled(ImVec2(c.x, c.y + capR * 0.64f), capR * 0.82f, IM_COL32(0, 0, 0, 20), 40);       // subtle bottom shade
        dl->AddCircleFilled(ImVec2(c.x, c.y - capR * 0.60f), capR * 0.85f, IM_COL32(255, 255, 255, 13), 40); // top lift
        dl->AddCircleFilled(ImVec2(c.x, c.y - capR * 0.72f), capR * 0.55f, IM_COL32(255, 255, 255, 17), 40);
        dl->PopClipRect();
        dl->AddCircle(c, capR, IM_COL32(20, 20, 22, 255), 44, 1.4f * s);
        const float a = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 pd(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + pd.x * capR * 0.12f, c.y + pd.y * capR * 0.12f),
                    ImVec2(c.x + pd.x * capR * 0.92f, c.y + pd.y * capR * 0.92f), pointerCol, 3.4f * s);
    }

    //========================================================================
    // British-style brown console band knob: continuous knob over arbitrary
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
                           const char* unit, const char* fmt,
                           bool addWideGapDots = false)
    {
        const float s = sc();
        const ImVec2 c = panel.P(cx, cy);
        const float RR = R * s;
        auto c01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };

        const bool frequencyKnob = isFrequencyParam(paramId);
        // Frequency parameters retain the captured UAD control coordinate for
        // DSP/session compatibility, but the physical knob lives in effective
        // Hz. This makes its pointer, legends and live read-out agree with the
        // response curve and FFT in both Brown and Black modes.
        float t = detentValToPos(T, V, n,
                                frequencyKnob ? displayValue(paramId)
                                              : values[paramId]);

        ImGui::SetCursorScreenPos(ImVec2(c.x - RR, c.y - RR));
        ImGui::InvisibleButton(id, ImVec2(2.f * RR, 2.f * RR));
        const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        const bool editing = panel.isEditingValue(id);
        const bool modKey = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        auto setFromT = [&](float tt) {
            const float shown = detentPosToVal(T, V, n, tt);
            const float nv = normalizeParamValue(
                paramId, frequencyKnob ? dialForCalibrated(paramId, shown)
                                       : shown);
            values[paramId] = nv;
            setParameterValue(paramId, nv);
        };
        auto resetDefault = [&] {
            editParameter(paramId, true);
            values[paramId] = kDefault(paramId);
            setParameterValue(paramId, kDefault(paramId));
            editParameter(paramId, false);
            t = detentValToPos(T, V, n,
                              frequencyKnob ? displayValue(paramId)
                                            : values[paramId]);
        };
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
            { panel.openValueEdit(id, displayValue(paramId)); editParameter(paramId, false); } // double-click: type a value (freq knobs: actual Hz)
            else if (hov && !act)
            {
                const float wh = ImGui::GetIO().MouseWheel;
                if (wh != 0.f) { t = c01(t + wh * 0.02f); editParameter(paramId, true); setFromT(t); editParameter(paramId, false); }
            }
        }

        // The console frequency scales have deliberately non-uniform major
        // positions. Their two 0.25-wide segments leave conspicuous blank arcs
        // around the top detent, so add one unlabeled minor dot at each midpoint
        // (e.g. LF 75→200 and 200→338). This is visual only: interpolation
        // and the labeled parameter detents remain exactly as before.
        if (addWideGapDots)
            for (int i = 0; i < n - 1; ++i)
                if (T[i + 1] - T[i] >= 0.20f)
                {
                    const float mt = 0.5f * (T[i] + T[i + 1]);
                    const float ma = duskdaf::DuskPanel::knobAngle(mt);
                    const float mdx = std::sin(ma), mdy = -std::cos(ma);
                    dl->AddCircleFilled(
                        panel.P(cx + mdx * (R + 9.f), cy + mdy * (R + 9.f)),
                        1.45f * s, IM_COL32(150, 152, 156, 255), 8);
                }

        for (int i = 0; i < n; ++i)
        {
            const float a = duskdaf::DuskPanel::knobAngle(T[i]);
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
            // Frequency knobs take the typed value as the ACTUAL centre and map
            // it back to the dial law; every other knob types the dial value.
            float lo = V[0], hi = V[0];
            for (int i = 1; i < n; ++i) { lo = std::min(lo, V[i]); hi = std::max(hi, V[i]); }
            typed = typed < lo ? lo : (typed > hi ? hi : typed);
            if (frequencyKnob)
                typed = dialForCalibrated(paramId, typed);
            typed = normalizeParamValue(paramId, typed);
            editParameter(paramId, true); values[paramId] = typed; setParameterValue(paramId, typed); editParameter(paramId, false);
        }
        else if ((hov || act) && !editing)
        {
            char buf[24]; std::snprintf(buf, sizeof(buf), fmt, displayValue(paramId));
            panel.valueBubble(cx, cy, R, buf);
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

    // Shared raised silver face for every ordinary button. Active controls keep
    // the same material and read as pressed through the inverted gradient.
    void drawSilverButtonFace(ImDrawList* dl, const ImVec2& b0, const ImVec2& b1,
                              bool pressed, bool hovered, float rounding)
    {
        const float s = sc();
        dl->AddRectFilled(b0, b1, IM_COL32(18, 18, 20, 255), rounding * s);
        const ImVec2 f0(b0.x + 1.6f * s, b0.y + 1.6f * s);
        const ImVec2 f1(b1.x - 1.6f * s, b1.y - 1.6f * s);
        const ImU32 top = pressed ? IM_COL32(120, 122, 126, 255)
                                  : (hovered ? IM_COL32(194, 196, 200, 255)
                                             : IM_COL32(182, 184, 188, 255));
        const ImU32 bot = pressed ? IM_COL32(150, 152, 156, 255)
                                  : (hovered ? IM_COL32(150, 152, 156, 255)
                                             : IM_COL32(138, 140, 144, 255));
        dl->AddRectFilledMultiColor(f0, f1, top, top, bot, bot);
        dl->AddLine(ImVec2(f0.x, f0.y), ImVec2(f1.x, f0.y),
                    IM_COL32(255, 255, 255, pressed ? 40 : 150), 1.2f * s);
        dl->AddLine(ImVec2(f0.x, f1.y), ImVec2(f1.x, f1.y),
                    IM_COL32(0, 0, 0, pressed ? 120 : 60), 1.2f * s);
    }

    // Raised silver metal button (BELL / SHELF). Beveled, pressed-in when active.
    void metalButton(ImDrawList* dl, const char* id, float x0, float y0, float x1, float y1,
                     uint32_t paramId, const char* onLabel, const char* offLabel)
    {
        const bool on = values[paramId] > 0.5f;
        const ImVec2 b0 = panel.P(x0, y0), b1 = panel.P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) toggleParam(paramId);
        drawSilverButtonFace(dl, b0, b1, on, ImGui::IsItemHovered(), 5.0f);
        panel.text(dl, 0.5f * (x0 + x1), y0 + 0.30f * (y1 - y0), 11.f, IM_COL32(34, 34, 38, 255), on ? onLabel : offLabel, 0, true);
    }

    //========================================================================
    // edge meters (INPUT left, OUTPUT right)
    //========================================================================
    void drawMeters(ImDrawList* dl)
    {
        float inL = 0, inR = 0, outL = 0, outR = 0;
        bool haveDirect = false;
       #if DAF_PLUGIN_WANT_DIRECT_ACCESS
        if (fourKEQGetInputPeakL != nullptr)
            if (void* inst = getPluginInstancePointer())
            { inL = fourKEQGetInputPeakL(inst); inR = fourKEQGetInputPeakR(inst);
              outL = fourKEQGetOutputPeakL(inst); outR = fourKEQGetOutputPeakR(inst);
              haveDirect = true; }
       #endif
        if (! haveDirect)
        {
            // Out-of-process (no direct access): the OUTPUT peaks are mirrored via
            // output parameters, which parameterChanged() keeps current — use them
            // so the OUT meter still moves. INPUT peaks have no output-parameter
            // fallback, so the IN meter reads 0 here; input metering requires
            // same-process (direct-access) hosting.
            outL = values[kOutPeakL];
            outR = values[kOutPeakR];
        }
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

    duskdaf::DuskPanel panel;
    duskdaf::SupportersOverlay supportersOverlay;
    duskdaf::DuskImGuiTextInputFocus textInputFocus;
    duskdaf::RealFFT fft;
    std::vector<float> specDb;
    duskdaf::CrispFontSet fontSet;
    ImFont* labelFont = nullptr;
    // Throttled numeric meter readout (peak-hold over a ~150 ms window).
    float meterPkIn_ = 0.f, meterPkOut_ = 0.f;
    float meterDbIn_ = -60.f, meterDbOut_ = -60.f;
    float meterTimer_ = 0.f;
    float values[kParamCount] = {};
    int currentPreset = -1;
    bool showGraph = true;
    bool showSupporters = false; // Patreon supporters overlay (title click)
    // User preset library (see scanUserPresets): vals[] caches each file's
    // normalised values for identity matching against values[].
    struct UserPreset { std::string name, path; float vals[kParamCount]; };
    std::vector<UserPreset> userPresets;
    std::string currentUserName, currentUserPath; // active user preset ("" = none)
    char saveBuf[64] = {};       // SAVE modal name entry
    bool saveFailed = false;     // SAVE modal: last write failed, keep it open
    bool showFft = true;         // spectrum analyzer overlay on the graph
    int  graphRangeIdx = 2;      // 0:+-6 1:+-12 2:+-18 3:+-30 4:Warped
    bool needResize = false;
    bool gripCursorSet = false;  // NWSE cursor currently pushed to the window
    float ctlDstTop_ = 180.0f, ctlScaleY_ = 1.0f;
    float stepDragT = 0.0f; // stepped filter-knob drag origin (HPF/LPF)
    bool  stepModReset_ = false; // Ctrl/Cmd+click reset in progress (suppress drag)

    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourKEQUI)
};

UI* createUI() { return new FourKEQUI(); }

END_NAMESPACE_DAF
