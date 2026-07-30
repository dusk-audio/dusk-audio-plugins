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
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cfloat>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>

// Single-sourced from CMake project(VERSION) via SC_VERSION_STRING; fallback keeps
// an ad-hoc compile (no build defs) valid. Shown in the nameplate hover tooltip.
#ifndef SC_VERSION_STRING
 #define SC_VERSION_STRING "1.0.0"
#endif

START_NAMESPACE_DISTRHO

namespace
{
    constexpr float kDesignW = 1240.0f;
    constexpr float kDesignH = 780.0f;
    constexpr float kPi = 3.14159265358979f;
    constexpr float kDimTextBlend = 0.25f;

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

    // Version chip under the "Dusk Audio" byline. Middle dot, not an em dash:
    // the crisp atlas is baked over Latin-1 and U+2014 draws as a "?" box.
    const char* const kVerLabel = "\xC2\xB7 v" SC_VERSION_STRING;

    // Combo option tables (labels only; no trademarks).
    const char* const kWave5[]    = { "Saw", "Square", "Triangle", "Sine", "Pulse" };
    const char* const kWave4[]    = { "Saw", "Square", "Triangle", "Sine" };
    const char* const kSubWave[]  = { "Square", "Sine" };
    const char* const kEnvCurve[] = { "Linear", "Exp", "Log", "Analog" };
    const char* const kArpMode[]  = { "Up", "Down", "Up/Down", "Down/Up", "Random", "Order", "Chord" };
    const char* const kDivName[]  = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                      "1/2.", "1/4.", "1/8.", "1/16.", "1/2T", "1/4T", "1/8T", "1/16T" };
    const char* const kArpVel[]   = { "As Played", "Fixed", "Accent" };
    // msynth::ArpAccentPattern. Only reachable/meaningful while kArpVel is "Accent".
    const char* const kArpAccent[]= { "Downbeat", "Every Other", "Ramp Up", "Ramp Down" };
    const char* const kLfoShape[] = { "Sine", "Triangle", "Square", "S&H", "Random" };
    const char* const kDriveType[]= { "Soft", "Hard", "Tube" };
    const char* const kChorusOpt[]= { "Off", "I", "II", "I+II" };
    const char* const kModFilterOpt[] = { "EARLY", "LATE" };
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
    // Every host edit gesture this UI opens is TRACKED, not just forwarded. A
    // gesture is a promise to the host ("automation is being written by hand,
    // hold your recording/latch here") that only an endEdit can retract, and the
    // widget that made the promise is not always around to keep it:
    //
    //   * the ACID pitch lane, and every knob that lives in a mode-conditional
    //     branch, stops being submitted the instant the Mode param changes — and
    //     Mode changes from the HOST too (automation, a MIDI program change),
    //     with no regard for a drag in progress. The widget's IsItemDeactivated()
    //     endEdit is in the branch that no longer runs;
    //   * opening a modal (mod matrix / browser / save) early-returns past the
    //     base layers with exactly the same effect;
    //   * closing the editor mid-drag destroys the widget outright.
    //
    // In every one of those the host is left with an open gesture on a parameter
    // nothing will ever touch again — in a DAW that reads as a control stuck in
    // "being edited" forever. Only ONE gesture can be open at a time here (a
    // drag captures the mouse, and every other path is begin/set/end inside one
    // call), so one slot is enough; closeOrphanedEdit() below force-closes it and
    // the destructor is the last-resort backstop.
    void beginEdit(uint32_t idx) override { openEditParam = (int)idx; editParameter(idx, true); }
    void endEdit(uint32_t idx) override
    {
        if (openEditParam == (int)idx) openEditParam = -1;
        editParameter(idx, false);
    }
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

        // Tooltips should read as deliberate UI, not text that appears immediately
        // under the pointer. This ImGui context belongs to this editor, so setting
        // the popup treatment once also keeps combo menus and tooltips consistent.
        ImGuiStyle& style = ImGui::GetStyle();
        style.PopupRounding = 5.0f;
        style.PopupBorderSize = 1.0f;
        style.HoverStationaryDelay = 0.12f;
        style.HoverDelayNormal = 0.42f;
        style.HoverFlagsForTooltipMouse =
            ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal;
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.060f, 0.070f, 0.98f);
        style.Colors[ImGuiCol_Border]  = ImVec4(0.42f, 0.43f, 0.46f, 0.92f);

        buildTooltips();

        presetStore.refresh();   // scan the user preset dir once at construction

        // Mode tag per factory preset for the browser's badge + mode filter: one
        // walk of each override table, here, instead of on every filter change.
        for (int i = 0; i < kNumFactoryPresets; ++i) factoryMode[i] = factoryPresetMode(i);

        curMode = clampMode((int)std::lround(values[kParamMode]));
        prevMode = curMode;
        live = kPalettes[curMode];
        fromPal = live;
        modeBlend = 1.0f;
    }

    // Teardown has to undo everything this UI is the sole owner of, because once the
    // window is gone there is no widget left to undo it with:
    //
    //  * a key held under the mouse — sendNote() note-off, or the voice hangs for the
    //    life of the plugin (the release path is IsMouseReleased, which never arrives);
    //  * pitch bend — but ONLY a bend this UI authored (pbLocal) AND that the engine
    //    still agrees is ours (getPitchBend() == pbSent). The spring normally lands
    //    it, and a window closed mid-drag or mid-spring would otherwise leave the
    //    engine detuned with no widget left to release it. A bend the HOST or a
    //    hardware wheel is holding is NOT ours to cancel: zeroing it here would snap
    //    the pitch of a note the player is still bending just because they closed the
    //    editor. pbLocal alone does not settle that: the host can write 0xE0 DURING a
    //    local drag, leaving pbLocal true and pbSent non-zero while the atomic holds
    //    the host's value — so the engine has to be asked.
    //      Residual hole (accepted): a held drag re-asserts every frame, so an
    //    external write during a drag is normally overwritten on the next frame and by
    //    teardown the engine does agree with pbSent, so the recentre fires. Only a
    //    close inside the one frame between the host's write and our next push escapes
    //    — ~16 ms wide, and self-correcting on the next message. The gate still earns
    //    its keep by closing the same window during the SPRING, where nothing
    //    re-asserts and adoption would need one more frame to clear pbLocal.
    //
    // The mod wheel is deliberately NOT reset. It latches like the hardware it stands
    // in for and the engine keeps the value while the editor is shut; the old reset
    // existed only because the engine had no getter, so a reopened UI would have drawn
    // the wheel at 0 and disagreed with the sound. MultiSynthDSP::getModWheel() now
    // seeds the widget from the engine on the first frame, so the honest behaviour —
    // leave the latch alone — is finally available.
    // An open edit gesture is also this UI's to undo: a window closed mid-drag has
    // no widget left to fire the endEdit, and the host would hold the parameter in
    // "being edited" for the life of the plugin. editParameter() directly rather
    // than endEdit(): the tracking state is about to be destroyed anyway.
    ~MultiSynthUI() override
    {
        if (openEditParam >= 0) { editParameter((uint32_t)openEditParam, false); openEditParam = -1; }
        if (kbNote >= 0) sendNote(0, (uint8_t)kbNote, 0);
        if (msynth::MultiSynthDSP* d = dspAccess())
            if (pbLocal && pbSent != 0.0f && d->getPitchBend() == pbSent)
                d->pitchBend(0.0f);
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

        closeOrphanedEdit();   // before anything can re-open one (see beginEdit)
        syncMidiProgramChange();
        updateWheels();   // before the modal early-returns below, so the bend spring
                          // keeps running while an overlay is up

        // ---- mode crossfade (spec §5) ----
        const int m = clampMode((int)std::lround(values[kParamMode]));
        if (m != curMode)
        {
            fromPal = live;
            prevMode = curMode;
            curMode = m;
            modeBlend = 0.0f;
            if (m == 5)
                showMod = false;
        }
        const float dt = ImGui::GetIO().DeltaTime;
        modeBlend = std::min(1.0f, modeBlend + (dt > 0.f ? dt : 0.016f) / 0.28f);
        const float e = modeBlend * modeBlend * (3.0f - 2.0f * modeBlend);
        live = blendPal(fromPal, kPalettes[curMode], e);

        // Push the blended palette into the shared panel (on-panel ink + accent).
        duskdpf::Palette pp;
        pp.white    = live.textPanel;
        pp.whiteDim = lerpC(live.textPanel, live.panel, kDimTextBlend);
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
            const duskdpf::ResizeGripState grip = submitGrip(winW, winH);
            endLayer(kLayerModal);
            ImGui::PopStyleVar(2);
            applyGrip(grip);
           #ifdef MSYNTH_FRAME_PROFILE
            profileFrame(_t0);
           #endif
            return;
        }

        if (showBrowse)
        {
            dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(0, 0, 0, 170)); // scrim on bg list
            beginLayerScreen("MSbrowse", 0, 0, winW, winH, true);
            drawPresetBrowserOverlay();
            const duskdpf::ResizeGripState grip = submitGrip(winW, winH);
            endLayer(kLayerBrowse);
            ImGui::PopStyleVar(2);
            applyGrip(grip);
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
            const duskdpf::ResizeGripState grip = submitGrip(winW, winH);
            endLayer(kLayerModal);
            ImGui::PopStyleVar(2);
            applyGrip(grip);
           #ifdef MSYNTH_FRAME_PROFILE
            profileFrame(_t0);
           #endif
            return;
        }

        beginLayer("MStop", 0, 0, kDesignW, 55);
        drawTopBar();
        endLayer(kLayerTop);

        // Layer seam at y=545. The lower body ROW of the upper three layers (VOICE/
        // CHARACTER, AMP/FILTER ENV, MOD bar, OUTPUT) ends at y=542, so its outer
        // bevel reaches y=544 and leaves a narrow chassis rule at the y=545 layer
        // seam. The SEQUENCER panel in MSbottom starts at y=548 (bevel top 546), so
        // neither frame clips and the rows read as separate hierarchy bands. This
        // +24 shift grew the lower body row
        // (osc panels shrank to feed it) and trimmed the sequencer's height to match.
        beginLayer("MSleft", 0, 55, 346, 545);
        if (curMode == 4) drawPrismOps();   // Prism swaps oscillators for the op matrix
        else              drawOscPanels();
        drawMixerVoice();
        endLayer(kLayerLeft);

        beginLayer("MScenter", 346, 55, 756, 545);
        drawFilter();
        drawEnvelopes();
        endLayer(kLayerCenter);

        beginLayer("MSright", 756, 55, kDesignW, 545);
        drawLFOs();
        drawModeSubPanelRegion();
        drawModMatrixBar();
        drawScope();
        drawOutputVU();
        endLayer(kLayerRight);

        beginLayer("MSbottom", 0, 545, kDesignW, kDesignH);
        drawSequencer();
        drawFXStrip();
        drawWheels();
        drawKeyboard();
        // MSbottom owns the window's bottom-right corner (the design is aspect-
        // locked, so the letterbox margin here is sub-pixel), and it is the last
        // base layer submitted, hence the frontmost -- so the grip both wins the
        // hover race and paints over the keyboard it overlaps.
        const duskdpf::ResizeGripState grip = submitGrip(winW, winH);
        endLayer(kLayerBottom);

        ImGui::PopStyleVar(2);
        applyGrip(grip);
       #ifdef MSYNTH_FRAME_PROFILE
        profileFrame(_t0);
       #else
        (void)_t0;
       #endif
    }

    // Per-layer VERTEX CENSUS. The DPF DearImGui backend has no VtxOffset support,
    // so a single window's draw list corrupts past 65535 vertices (ImDrawIdx is
    // 16-bit) — the whole reason this UI is split into non-overlapping layer
    // windows. Every layer therefore ends through endLayer(), which (in a
    // -DMSYNTH_FRAME_PROFILE build) samples that window's draw list before
    // ImGui::End() and keeps a running per-layer maximum, printed with the frame
    // timings. Re-run it after adding chrome.
    //
    // WORST CASE: MSleft in PRISM — 49466 / 65535 (75.5%). Not the mod-matrix
    // modal, which this comment used to name: measured on the same build the
    // modal peaks at 9692 (14.8%) with a Source dropdown open and the browser at
    // 10616 (16.2%), i.e. five times under the real leader. The operator matrix
    // is what runs away with it: 37 r13 knobs in ONE window, against MSleft's
    // 19856 (30.3%) when the same layer draws the two oscillator panels instead.
    //
    // Measured headroom, so an addition can be checked by arithmetic rather than
    // re-litigated: one r13 knob (chrome + ticks + accent arc + chip label) costs
    // ~968 vertices — a control run with one extra knob per op strip moved MSleft
    // 49466 -> 53338, i.e. 3872 for 4. The remaining 16069 vertices are therefore
    // room for ~16 more knobs in Prism's left column, and for nothing like that
    // many if they bring panels and read-outs with them. Anything bigger than a
    // knob or two belongs in another layer; MScenter (25.0%) and MSright (20.3%)
    // both have the budget for it.
    //
    // Per-mode figures (base layers, no overlay), for reference:
    //   left   Cosmos 19856 · Oracle 20434 · Mono 20380 · Modular 21428 ·
    //          PRISM 49466 · Acid 19856
    //   center 16376 (all modes) · right 10528..13272 · bottom 20514..22094
    //
    // kLayerPopup is the one layer that is NOT a layer window: it is the preset
    // combo's own popup, sampled from inside drawPresetPopupBody(). A popup is a
    // separate ImGui window with its own draw list, so the multi-column grid has
    // its own 16-bit budget and would otherwise never appear in the census.
    enum { kLayerTop, kLayerLeft, kLayerCenter, kLayerRight, kLayerBottom, kLayerModal,
           kLayerBrowse, kLayerPopup, kNumLayers };
    void endLayer(int layer)
    {
       #ifdef MSYNTH_FRAME_PROFILE
        const int v = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        if (layer >= 0 && layer < kNumLayers && v > vtxMax[layer]) vtxMax[layer] = v;
       #else
        (void)layer;
       #endif
        ImGui::End();
    }

   #ifdef MSYNTH_FRAME_PROFILE
    void profileFrame(std::chrono::high_resolution_clock::time_point t0)
    {
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (profN >= 0 && profN < 100)
        { profLogic[profN] = ms; profTotal[profN] = ImGui::GetIO().DeltaTime * 1000.0; }
        // Report every 100 frames (not once), so a census taken with the mod matrix
        // or the save modal open actually reaches the log.
        if (++profN >= 100)
        {
            double a[100], b[100];
            std::memcpy(a, profLogic, sizeof a); std::memcpy(b, profTotal, sizeof b);
            std::sort(a, a + 100); std::sort(b, b + 100);
            std::fprintf(stderr, "MSYNTH_FRAME logic_median=%.3fms total_median=%.3fms (100 frames)\n",
                         a[50], b[50]);
            static const char* const kLayerNames[kNumLayers] =
                { "top", "left", "center", "right", "bottom", "modal", "browse", "popup" };
            for (int i = 0; i < kNumLayers; ++i)
                std::fprintf(stderr, "MSYNTH_VTX %-7s max=%5d / 65535 (%.1f%%)\n",
                             kLayerNames[i], vtxMax[i], 100.0 * vtxMax[i] / 65535.0);
            profN = 0;
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

    // Own in-UI resize grip. AUv2 hosts (Logic) never provide a window grip of
    // their own; on VST3/CLAP the host's grip stays available and this is simply
    // a second way to do the same thing.
    //
    // Shared by every terminal path of onImGuiDisplay because this UI has four of
    // them -- three modals that REPLACE the panels, plus the base frame -- and the
    // grip has to be in all of them or the window would stop being resizable
    // whenever an overlay is up.
    //
    // Submitted into the FRONTMOST LAYER rather than a full-window grip layer of
    // its own: the layer windows are deliberately non-overlapping (see the vertex
    // census above), and one stacked over all of them would steal hover from every
    // panel underneath. The window that owns the bottom-right corner is MSbottom
    // in the base frame and the modal's own full-window layer otherwise, so
    // calling this last in that layer gives the grip exactly the hover priority it
    // needs and nothing more. Costs ~24 vertices, i.e. nothing against MSbottom's
    // measured 22094/65535 worst case.
    duskdpf::ResizeGripState submitGrip(float winW, float winH)
    {
        return panel.resizeGrip(dl, winW, winH, kDesignW, kDesignH);
    }

    // The grip's two side effects, both DPF window calls the shared widget header
    // deliberately cannot make. Applied AFTER the owning layer's ImGui::End(): a
    // host may service setSize() synchronously and re-enter the UI, which must not
    // happen in the middle of a window's submission.
    void applyGrip(const duskdpf::ResizeGripState& grip)
    {
        textInputFocus.update(*this);

        // The DPF-Widgets ImGui backend never forwards ImGui::SetMouseCursor() to
        // the window, so drive DGL's cursor directly. Edge-triggered: setCursor()
        // is a window-level call, not a per-frame one.
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
    // Force-close an edit gesture whose owning widget stopped being submitted.
    //
    // Every gesture that SPANS frames in this UI is backed by an ImGui ACTIVE item
    // — the knob, the pitch cell, the sequencer lane being dragged — because that
    // is what holds the mouse capture; the begin/set/end-in-one-call paths (combo
    // pick, wheel, ctrl-reset, double-click reset) are already balanced when they
    // return. So "a gesture is open at the top of a frame and NOTHING is active"
    // can only mean the owner is gone: ImGui clears the active id in NewFrame once
    // an active item goes one frame without being submitted, which is exactly the
    // mode flip / modal open / teardown case.
    //
    // Running at the TOP of the frame (before any widget) is what makes the test
    // unambiguous: a fresh activation cannot have happened yet, so a non-zero
    // active id here always belongs to the still-live owner of our gesture.
    //
    // pitchDragging is the pitch lane's own "I already opened a gesture" latch and
    // has to fall with it, or the lane would come back inert — its next drag would
    // see the latch still set and push values with no gesture around them.
    void closeOrphanedEdit()
    {
        if (openEditParam < 0 || ImGui::IsAnyItemActive()) return;
        endEdit((uint32_t)openEditParam);
        pitchDragging = false;
    }

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

    // Advance width of `t` in DESIGN units, measured through the same atlas face
    // text() would pick. Lets a row lay itself out AFTER a variable-width label
    // instead of hardcoding a gap that only balances for one of the strings.
    float textW(float sz, const char* t) const
    {
        const float px = sz * s;
        ImFont* const f = panel.pickFont(px);
        return f->CalcTextSizeA(px, FLT_MAX, 0.0f, t).x / s;
    }

    void panelBox(float x0, float y0, float x1, float y1, float alpha = 1.0f)
    {
        // The older 3 px bright metal frame gave every nested section the same
        // visual weight. A slimmer, chassis-blended rail keeps the hardware
        // character while allowing titles, controls, and mode colour to lead.
        const ImU32 frame = lerpC(metalCol(), live.bg, 0.34f);
        dl->AddRectFilled(P(x0 - 2, y0 - 2), P(x1 + 2, y1 + 2),
                          mulA(frame, alpha), 7.0f * s);
        dl->AddRectFilled(P(x0, y0), P(x1, y1), mulA(live.panel, alpha), 6.0f * s);
        // engraved bevel: light top-edge highlight + dark bottom-edge shade
        dl->AddLine(P(x0 + 2, y0 + 1), P(x1 - 2, y0 + 1),
                    mulA(lerpC(live.panel, IM_COL32(255, 255, 255, 255), 0.13f), alpha), 1.0f * s);
        dl->AddLine(P(x0 + 2, y1 - 1), P(x1 - 2, y1 - 1),
                    mulA(lerpC(live.panel, IM_COL32(0, 0, 0, 255), 0.26f), alpha), 1.0f * s);
    }
    void sectionTitle(float x, float y, const char* t)
    {
        // Dark panels benefit from the engraved shadow. On Acid's light silver
        // skin it reads as a second, misregistered heading instead of depth.
        if (curMode != 5)
            text(x + 0.6f, y + 1.0f, 11.0f,
                 withA(IM_COL32(0, 0, 0, 255), 120), t, -1, true);
        text(x, y, 11.5f, live.accent, t, -1, true);
    }
    void drawX(float cx, float cy, float r, ImU32 col) // close/clear glyph (no exotic font glyph)
    { dl->AddLine(P(cx - r, cy - r), P(cx + r, cy + r), col, 1.6f * s);
      dl->AddLine(P(cx - r, cy + r), P(cx + r, cy - r), col, 1.6f * s); }
    void klabel(float cx, float topY, const char* l) { text(cx, topY, 10.0f, live.textPanel, l, 0, true); }

    //--- persistent value read-outs (spec §3.1b) --------------------------------
    // Performance-critical knobs show their value under the knob at ALL times, not
    // just in the hover bubble. Two gates keep that from becoming visual noise:
    //   * SCALE — the read-out is 9.5 px of DESIGN space, so at the 620x390 minimum
    //     (s = 0.5) it lands at <5 device px and is unreadable mush. Below
    //     kReadoutMinS every persistent read-out is suppressed and the 12 px hover
    //     bubble (drawn on the foreground list, never clipped) is the read-out.
    //   * DENSITY — panels with no free band under their knobs (FX DELAY / REVERB,
    //     whose knob rows are boxed in by the P-P/TAPE row and the panel floor)
    //     swap the knob LABEL to the value while the pointer is anywhere in that
    //     panel, rather than dropping to hover-only. Hovering the panel then reads
    //     out every value in it at once, which is what A/B-ing a mix needs.
    static constexpr float kReadoutMinS = 0.72f;   // 9.5 * 0.72 = 6.8 device px
    bool readoutsOn() const { return s >= kReadoutMinS; }

    bool mouseInRect(float x0, float y0, float x1, float y1) const
    {
        const ImVec2 m = ImGui::GetIO().MousePos, a = P(x0, y0), b = P(x1, y1);
        return m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y;
    }

    // Knob label that reads out the knob's VALUE (accent ink) while `showValue`,
    // and the static label otherwise. Identical size/weight either way, so the row
    // never reflows as the pointer moves across it.
    void klabelOrValue(float cx, float topY, const char* l, bool showValue, uint32_t p,
                       const char* fmt, const char* suffix, float dmul = 1.0f,
                       float dadd = 0.0f, bool timeAuto = false)
    {
        if (!showValue || !readoutsOn()) { klabel(cx, topY, l); return; }
        char buf[48]; fmtVal(buf, sizeof buf, p, fmt, suffix, dmul, dadd, timeAuto);
        text(cx, topY, 10.0f, live.accent, buf, 0, true);
    }

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

    // Return the matrix destination represented by a visible control. Destinations
    // without a truthful one-to-one control (voice amplitude and per-voice pan)
    // stay on the matrix badge; Effects Mix intentionally marks all four wet-mix
    // knobs because that destination scales the entire effects chain.
    static int modDestForParam(uint32_t p)
    {
        switch (p)
        {
            case kParamOsc1Detune:   return 1;
            case kParamOsc2Detune:   return 2;
            case kParamOsc1PW:       return 3;
            case kParamOsc2PW:       return 4;
            case kParamFilterCutoff: return 5;
            case kParamFilterRes:    return 6;
            case kParamLfo1Rate:     return 9;
            case kParamLfo2Rate:     return 10;
            case kParamDriveMix:
            case kParamChorusMix:
            case kParamDelayMix:
            case kParamReverbMix:    return 11;
            case kParamUnisonDetune: return 12;
            default:                 return 0;
        }
    }

    // Commercial synths make modulation visible at its destination. Draw signed
    // range arcs outside the normal value/tick ring: positive depth travels right
    // from noon, negative depth left. Multiple slots accumulate independently so
    // opposing routings remain visible instead of cancelling into "no modulation".
    void modulationRange(uint32_t p, float cx, float cy, float r, bool ticks)
    {
        if (curMode == 5) return; // AcidVoice bypasses the modulation matrix.
        const int dest = modDestForParam(p);
        if (dest == 0) return;

        float pos = 0.0f, neg = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            if (values[kParamModSrc0 + i] <= 0.5f
                || (int)std::lround(values[kParamModDst0 + i]) != dest)
                continue;
            const float amount = values[kParamModAmt0 + i];
            if (amount > 0.0f) pos += amount;
            else               neg -= amount;
        }
        pos = std::min(pos, 1.0f);
        neg = std::min(neg, 1.0f);
        if (pos <= 1e-4f && neg <= 1e-4f) return;

        const ImVec2 c = P(cx, cy);
        // Ticked knobs already reach r+6.5; tickless compact FX knobs have tightly
        // stacked labels, so their range rides closer to the body.
        const float R = (r + (ticks ? 9.0f : 5.5f)) * s;
        const ImU32 col = lerpC(live.ledOn, live.accent, 0.24f);
        auto arc = [&](float end)
        {
            constexpr int N = 18;
            ImVec2 pts[N + 1];
            for (int i = 0; i <= N; ++i)
            {
                const float t = 0.5f + (end - 0.5f) * (float)i / (float)N;
                const float a = duskdpf::DuskPanel::knobAngle(t);
                pts[i] = ImVec2(c.x + std::sin(a) * R, c.y - std::cos(a) * R);
            }
            dl->AddPolyline(pts, N + 1, col, 0, 2.0f * s);
            dl->AddCircleFilled(pts[N], 2.2f * s, col, 10);
        };
        if (pos > 1e-4f) arc(0.5f + 0.45f * pos);
        if (neg > 1e-4f) arc(0.5f - 0.45f * neg);
    }

    ImU32 whiteDimCol() const
    {
        return lerpC(live.textPanel, live.panel, kDimTextBlend);
    }

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

    // Reset modifier for the LOCAL knobs below. Must stay identical to the block
    // in DuskPanel::knob (DuskImGuiWidgets.hpp) or one panel would answer to two
    // conventions: Option/Alt everywhere, plus Cmd on macOS or Ctrl elsewhere.
    // macOS deliberately excludes Ctrl -- the OS routes Ctrl+left-click as a
    // right-click, so it must reach the context menu rather than reset.
    static bool resetModKey() noexcept
    {
       #if defined(__APPLE__)
        return ImGui::GetIO().KeyAlt || ImGui::GetIO().KeySuper;
       #else
        return ImGui::GetIO().KeyAlt || ImGui::GetIO().KeyCtrl;
       #endif
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
        const bool modKey  = resetModKey();
        const bool editing = panel.isEditingValue(id);
        bool changed = false;

        if (tips[p] && tips[p][0] && !active
            && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tips[p]);
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
                    // Shift = fine, same 0.004/0.02 split the shared knob uses.
                    const float step = ImGui::GetIO().KeyShift ? 0.004f : 0.02f;
                    float L = toL(values[p]) + wheel * lrange * step;
                    if (L < lmin) L = lmin; if (L > lmax) L = lmax;
                    const float nv = fromL(L);
                    if (nv != values[p]) { beginEdit(p); values[p] = nv; setParam(p, nv); endEdit(p); changed = true; }
                }
            }
        }

        const float t = lrange > 0.0f ? (toL(values[p]) - lmin) / lrange : 0.0f;
        drawKnobChrome(c, R, t, ticks);
        accentArc(cx, cy, r, t, bipolar);
        modulationRange(p, cx, cy, r, ticks);

        float typed;
        if (panel.valueEdit(id, cx, cy, r, typed))
        {
            typed = (typed - dadd) / (dmul != 0.0f ? dmul : 1.0f);
            typed = typed < d.min ? d.min : (typed > d.max ? d.max : typed);
            if (typed != values[p]) { beginEdit(p); values[p] = typed; setParam(p, typed); endEdit(p); changed = true; }
        }
        else if (active && !panel.isEditingValue(id))
        {
            char buf[48];
            fmtVal(buf, sizeof buf, p, fmt, suffix, dmul, dadd, timeAuto);
            panel.valueBubble(cx, cy, r, buf);
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
        const bool modKey  = resetModKey();
        const bool editing = panel.isEditingValue(id);
        bool changed = false;
        if (tips[p] && tips[p][0] && !active
            && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tips[p]);
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
                    // No Shift-fine here by design: this wheel steps the discrete
                    // classic-ratio list, matching the shared knob's `stepped`
                    // path (which also uses a fixed 1-step and ignores Shift).
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
        else if (active && !panel.isEditingValue(id))
        { char buf[24]; std::snprintf(buf, sizeof buf, "%.2f\xC3\x97", values[p]); panel.valueBubble(cx, cy, r, buf); }
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
                                   tips[p], false, dmul, dadd, d.name,
                                   /*contextMenu*/ false, /*overrideText*/ nullptr,
                                   /*hasExternalReadout*/ false,
                                   /*dispMin*/ 0.0f, /*dispMax*/ 0.0f,
                                   /*nameOnHover*/ false,
                                   /*doubleClickReset*/ false,
                                   /*persistentTextSize*/ 9.5f,
                                   /*bubbleOnActiveOnly*/ true);
        const float t = (d.max > d.min) ? (values[p] - d.min) / (d.max - d.min) : 0.0f;
        // masterVol's bipolar arc anchors at the true 0 dB point, not the geometric
        // mid; symmetric bipolar ranges are unaffected (t0=0.5 either way).
        const float anchorT = (anchorZero && d.max > d.min) ? (0.0f - d.min) / (d.max - d.min) : -1.0f;
        accentArc(cx, cy, r, t, bipolar, anchorT);
        modulationRange(p, cx, cy, r, ticks);
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", whyTip);
    }

    void readOnlyField(const char* id, float x0, float y0, float x1, float y1,
                       const char* label, const char* whyTip, float alpha = 1.0f)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", whyTip);
        const ImU32 bg = lerpC(IM_COL32(38, 38, 41, 255), live.accent, 0.08f);
        dl->AddRectFilled(b0, b1, mulA(bg, alpha), 3.0f * s);
        dl->AddRect(b0, b1, mulA(withA(live.accent, 150), alpha),
                    3.0f * s, 0, 1.0f * s);
        text(0.5f * (x0 + x1), y0 + 0.28f * (y1 - y0), 10.0f,
             mulA(IM_COL32(230, 232, 235, 255), alpha), label, 0, true);
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
        // ImGui's default combo-arrow button is bright blue. A dark tint derived
        // from the current mode accent keeps dropdowns recognizable without
        // turning every row into a repeated blue stripe (especially in Acid).
        ImGui::PushStyleColor(ImGuiCol_Button,
                              lerpC(live.accent, IM_COL32(30, 31, 35, 255), 0.68f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              lerpC(live.accent, IM_COL32(30, 31, 35, 255), 0.48f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              lerpC(live.accent, IM_COL32(30, 31, 35, 255), 0.30f));
        char cid[40]; std::snprintf(cid, sizeof(cid), "##%s", id);
        if (ImGui::BeginCombo(cid, opts[shownIdx]))
        {
            for (int i = 0; i < nopts; ++i)
                if (ImGui::Selectable(opts[i], i == idx)) setChoice(p, i);
            ImGui::EndCombo();
        }
        if (tips[p] && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tips[p]);
        ImGui::PopStyleColor(7);
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
        if (tips[p] && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tips[p]);
        if (acid)
        {
            const ImVec2 c((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
            const float rr = std::min(b1.x - b0.x, b1.y - b0.y) * 0.42f;
            dl->AddCircleFilled(c, rr, on ? live.ledOn : IM_COL32(150, 152, 158, 255), 24);
            dl->AddCircle(c, rr, IM_COL32(40, 42, 46, 255), 24, 1.4f * s);
            text((x0 + x1) * 0.5f, y1 + 1.0f, 9.0f, live.textPanel, label, 0, on);
            return;
        }
        const ImU32 offBg = IM_COL32(40, 40, 43, 255);
        const ImU32 buttonBg = on ? lerpC(offBg, live.accent, 0.18f) : offBg;
        dl->AddRectFilled(b0, b1, buttonBg, 3.0f * s);
        dl->AddRect(b0, b1, on ? withA(live.accent, 255) : IM_COL32(90, 90, 94, 255), 3.0f * s, 0, 1.4f * s);
        panel.led(dl, x0 + 8.0f, 0.5f * (y0 + y1), on, 3.2f);
        // label sits on the dark button, so always draw it light regardless of skin
        text(0.5f * (x0 + x1) + 8.0f, y0 + 0.30f * (y1 - y0), 10.0f,
             on ? IM_COL32(238, 238, 240, 255) : IM_COL32(150, 150, 154, 255), label, 0, on);
    }

    // Compact binary control for secondary state such as LEGATO and SYNC. Unlike
    // ledButton(), the target hugs its content instead of filling an entire combo
    // column, so an LED does not read as an unexplained empty text field.
    void compactToggle(const char* id, uint32_t p, float x0, float y0, float x1, float y1,
                       const char* label, bool interactive = true, float alpha = 1.0f)
    {
        const bool on = values[p] > 0.5f;
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        if (interactive)
        {
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            if (ImGui::IsItemClicked()) setChoice(p, on ? 0 : 1);
            if (tips[p] && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", tips[p]);
        }

        const ImU32 offBg = IM_COL32(38, 39, 43, 255);
        dl->AddRectFilled(b0, b1, mulA(on ? lerpC(offBg, live.accent, 0.20f) : offBg, alpha),
                          0.5f * (y1 - y0) * s);
        dl->AddRect(b0, b1,
                    mulA(on ? live.accent : IM_COL32(91, 93, 99, 255), alpha),
                    0.5f * (y1 - y0) * s, 0, 1.2f * s);
        const float cy = 0.5f * (y0 + y1);
        const ImVec2 lc = P(x0 + 9.0f, cy);
        dl->AddCircleFilled(lc, 4.4f * s,
                            mulA(on ? withA(live.ledOn, 75)
                                    : IM_COL32(28, 29, 32, 255), alpha), 16);
        dl->AddCircleFilled(lc, 2.6f * s,
                            mulA(on ? live.ledOn : IM_COL32(82, 83, 88, 255), alpha), 16);
        text(0.5f * (x0 + x1) + 5.0f, y0 + 0.27f * (y1 - y0), 9.0f,
             mulA(on ? IM_COL32(238, 238, 240, 255)
                     : IM_COL32(155, 156, 161, 255), alpha),
             label && label[0] ? label : (on ? "ON" : "OFF"), 0, on);
    }

    //========================================================================
    // Top bar (nameplate, mode rockers, preset browser)
    //========================================================================
    // TOP-BAR LAYOUT (design space; the bar is y 0..54).
    //
    // Three zones, left to right: NAMEPLATE (title + "Dusk Audio · vX.Y.Z"), the
    // six MODE rockers, and the PRESET cluster, right-anchored at 1222.
    //
    // The rockers used to be pinned at x306, leaving a ~120 px hole between them
    // and the nameplate and squeezing the preset cluster into the last 270 px.
    // They now start immediately after the nameplate — MEASURED with textW()
    // rather than guessed, so the gap survives a font substitution — and the
    // width that frees goes entirely to the presets, which is what was actually
    // short: the combo grew 126 -> 208 (at 126 it clipped the longest factory
    // names, "Alien Transmission" among them), BROWSE 40 -> 76, SAVE 36 -> 58,
    // and the chevrons 26x28 -> 34x32. Every cluster element now shares one
    // 12..44 row, so the bar reads as a single band instead of three heights.
    //
    // The cluster is laid out right-to-left from kBarClusterX1 because it is the
    // anchored end: gaps between its five elements are constants, so any future
    // width change moves the LEFT edge (and, through the clamp below, the
    // rockers) rather than silently overrunning the panel wall.
    static constexpr float kBarClusterX1 = 1222.0f;
    static constexpr float kBarY0 = 12.0f, kBarY1 = 44.0f;
    static constexpr float kBarSaveW = 58.0f, kBarBrowseW = 76.0f;
    static constexpr float kBarChevW = 34.0f, kBarComboW = 208.0f;
    static constexpr float kBarSaveX0   = kBarClusterX1 - kBarSaveW;                 // 1164
    static constexpr float kBarBrowseX0 = kBarSaveX0   - 6.0f - kBarBrowseW;         // 1082
    static constexpr float kBarNextX0   = kBarBrowseX0 - 6.0f - kBarChevW;           // 1042
    static constexpr float kBarComboX0  = kBarNextX0   - 4.0f - kBarComboW;          //  830
    static constexpr float kBarPrevX0   = kBarComboX0  - 4.0f - kBarChevW;           //  792
    // Rockers: 90 wide on a 96 pitch. "MODULAR" at font 12 is ~46 wide and is
    // drawn at centre+7 (the LED owns x0+11), so it ends ~14 short of the right
    // edge — the narrowest of the six fits with room, and the row is 570 wide.
    static constexpr float kBarModeW = 90.0f, kBarModePitch = 96.0f;

    void drawTopBar()
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, 54), mulA(live.bg, 1.0f));
        dl->AddRectFilled(P(0, 0), P(kDesignW, 3), metalCol());
        dl->AddLine(P(0, 54), P(kDesignW, 54), IM_COL32(70, 70, 72, 255), 1.5f * s);

        // ---- nameplate ----
        // The version rides with the "Dusk Audio" byline, one step down the type
        // hierarchy from it (10 px vs 11, dim ink vs accent): it has to be
        // FINDABLE — a bug report starts with it — not read on every glance.
        const float titleX = 18.0f, subX = 20.0f;
        const float verX   = subX + textW(11.0f, "Dusk Audio") + 7.0f;
        const float blockX1 = std::max(titleX + textW(20.0f, "SUNSET CIRCUITS"),
                                       verX  + textW(10.0f, kVerLabel));
        text(titleX, 8, 20.0f, live.text, "SUNSET CIRCUITS", -1, true);
        text(subX, 32, 11.0f, live.accent, "Dusk Audio", -1, true);
        // Scale-gated exactly like the persistent read-outs (§3.1b): 10 px of
        // design space is under 6 device px at the 620x390 minimum, which is the
        // same illegible mush that gate exists to suppress. The nameplate tooltip
        // carries the version at every size, so nothing is lost by dropping it.
        if (readoutsOn()) text(verX, 33, 10.0f, withA(live.text, 140), kVerLabel, -1);
        {
            const ImVec2 np0 = P(14, 4), np1 = P(blockX1 + 6.0f, 50);
            ImGui::SetCursorScreenPos(np0);
            ImGui::InvisibleButton("nameplate", ImVec2(np1.x - np0.x, np1.y - np0.y));
            // Full version (single-sourced from the git tag through CMake's
            // SC_VERSION_STRING, so it IS the build identity) plus the live
            // surface size and scale, which is the one other thing worth asking
            // for in a bug report. Deliberately NOT __DATE__/__TIME__: those
            // would make every rebuild a different binary and cost the release
            // builds their reproducibility for a line nobody can act on.
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Sunset Circuits v%s\nDusk Audio\n%d x %d \xC2\xB7 scale %.2f",
                                  SC_VERSION_STRING, (int)getWidth(), (int)getHeight(), s);
        }

        // ---- mode rockers x6 (spec §8.1) ----
        // Start at the nameplate, but never at the cost of the preset cluster:
        // the clamp is what guarantees the two zones cannot collide however wide
        // the title measures on a substituted font.
        float modeX0 = blockX1 + 16.0f;
        const float modeMax = kBarPrevX0 - 14.0f - (5.0f * kBarModePitch + kBarModeW);
        if (modeX0 > modeMax) modeX0 = modeMax;
        for (int i = 0; i < 6; ++i)
        {
            const float x0 = modeX0 + i * kBarModePitch, x1 = x0 + kBarModeW;
            const float y0 = 10.0f, y1 = 46.0f;
            const bool sel = (i == curMode);
            char id[16]; std::snprintf(id, sizeof(id), "rocker%d", i);
            const ImVec2 p0 = P(x0, y0), p1 = P(x1, y1);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton(id, ImVec2(p1.x - p0.x, p1.y - p0.y));
            if (ImGui::IsItemClicked() && !sel) setChoice(kParamMode, i);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
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
            text(0.5f * (x0 + x1) + 7.0f, y0 + 11.0f, 12.0f,
                 sel ? live.text : lerpC(live.text, live.bg, 0.35f), kModeNames[i], 0, sel);
        }

        // ---- preset cluster ----
        // currentPreset is a COMBINED index: factory [0..kNumFactoryPresets) then
        // user [kNumFactoryPresets..total).
        const char* preview = presetName(currentPreset);
        if (chevron("presetPrev", kBarPrevX0, kBarY0, kBarPrevX0 + kBarChevW, kBarY1, false, "Previous preset"))
            applyCombined(currentPreset < 0 ? comboTotal() - 1 : currentPreset - 1);

        ImGui::SetCursorScreenPos(P(kBarComboX0, kBarY0));
        ImGui::SetNextItemWidth(kBarComboW * s);
        ImFont* f = panel.pickFont(12.0f * s);
        ImGui::PushFont(f);
        // FramePadding.y floored at 1 px, as in comboBox(): at the 620x390 minimum
        // the atlas face can be taller than the row, and a negative padding
        // collapses the combo frame rather than tightening it.
        const float comboPadY = std::max(1.0f, ((kBarY1 - kBarY0) * s - f->FontSize) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, comboPadY));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header,  withA(live.accent, 150));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(235, 238, 242, 255));
        ImGui::PushStyleColor(ImGuiCol_Button,
                              lerpC(live.accent, IM_COL32(30, 31, 35, 255), 0.68f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              lerpC(live.accent, IM_COL32(30, 31, 35, 255), 0.48f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              lerpC(live.accent, IM_COL32(30, 31, 35, 255), 0.30f));
        // Setting the constraint OURSELVES is what unlocks the grid: BeginCombo
        // only imposes its "8 items tall, one column wide" default when no
        // constraint is pending (imgui.cpp, BeginComboPopup), so supplying one
        // hands the popup back its natural, content-driven size. The height cap
        // keeps a large USER bank from growing a popup taller than the plugin
        // window — ImGui clamps a popup's POSITION into the viewport, never its
        // size, so an uncapped one would simply hang off the bottom.
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                            ImVec2(FLT_MAX, (float)getHeight() * 0.86f));
        if (ImGui::BeginCombo("##presets", preview))
        {
            drawPresetPopupBody();
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Select a preset \xC2\xB7 grouped by mode, %d in all", comboTotal());
        ImGui::PopStyleColor(7); ImGui::PopStyleVar(); ImGui::PopFont();

        if (chevron("presetNext", kBarNextX0, kBarY0, kBarNextX0 + kBarChevW, kBarY1, true, "Next preset"))
            applyCombined(currentPreset < 0 ? 0 : currentPreset + 1);

        // BROWSE — full-screen searchable browser over factory + user presets.
        if (barButton("presetBrowse", kBarBrowseX0, kBarY0, kBarBrowseX0 + kBarBrowseW, kBarY1, "BROWSE",
                      "Browse every preset: search by name, filter by mode or bank"))
            openBrowse();

        // SAVE — opens the user-preset save modal (writes the current patch).
        if (barButton("presetSave", kBarSaveX0, kBarY0, kBarSaveX0 + kBarSaveW, kBarY1, "SAVE",
                      "Save the current patch as a user preset"))
            openSaveModal();
    }

    //========================================================================
    // Preset combo popup — grouped, multi-column
    //========================================================================
    // TapeMachine 2's preset combo groups its list with TextDisabled category
    // headers in ONE column and lets ImGui scroll it. That reads fine at twenty
    // presets; at 54 it is a scroll hunt. So the same idea is laid out ACROSS:
    // one group per MODE — which is this synth's strongest cue, and the same
    // grouping the browser's chips use — stacked into as many columns as the
    // window can actually hold, so the whole library is on screen at once.
    //
    // Still ONE ImGui window: the columns are BeginGroup/SameLine inside the
    // combo's own popup, not sibling windows, so the backend's
    // no-overlapping-windows limit is not in play. (A combo popup is the one
    // layer this backend does composite over the base windows, which is why the
    // single-column version worked; nothing here changes that.)
    //
    // Column WIDTH is measured from the actual atlas face rather than assumed
    // from the design size: pickFont() snaps to the nearest baked size, so at
    // small scales the glyphs are larger than 12*s and a design-space width
    // would clip exactly the names this change exists to stop clipping.
    void drawPresetPopupBody()
    {
        const auto& users = presetStore.list();
        const int nUsers  = (int)users.size();
        const int nGroups = 6 + (nUsers > 0 ? 1 : 0);

        if (ImGui::IsWindowAppearing() || popupColW <= 0.0f) measurePopupColumn();

        const ImGuiStyle& st = ImGui::GetStyle();
        const float pitch = popupColW + st.ItemSpacing.x;

        // Rows per group (header + entries), for the balance below.
        int rows[7] = {};
        for (int i = 0; i < kNumFactoryPresets; ++i) ++rows[clampMode(factoryMode[i])];
        for (int m = 0; m < 6; ++m) rows[m] += 1;
        if (nUsers > 0) rows[6] = 1 + nUsers;

        // How many columns fit. Not "one per group": seven 1:1 columns are ~1.1
        // kpx and sit inside 1240 comfortably, but at the 620x390 minimum the
        // atlas floor keeps the glyphs near full size while the window halves,
        // and the same seven would run off the screen edge.
        int nCols = (int)std::floor(((float)getWidth() - 2.0f * st.WindowPadding.x) / pitch);
        nCols = std::max(1, std::min(nGroups, nCols));

        // Greedy height balance: keep filling a column until the next group would
        // push it past the average, then move on. A group is never split across
        // columns — a mode's presets are the unit being grouped — so with the
        // usual "one column per group" outcome the target simply never binds; it
        // only does the work when the window forces two small groups to share.
        int total = 0; for (int g = 0; g < nGroups; ++g) total += rows[g];
        const int target = (total + nCols - 1) / nCols;
        int colRows = 0, colIdx = 0;
        bool open = false;
        for (int g = 0; g < nGroups; ++g)
        {
            if (open && colRows > 0 && colRows + rows[g] > target && colIdx + 1 < nCols)
            {
                ImGui::EndGroup();
                ImGui::SameLine(0.0f, st.ItemSpacing.x);
                ++colIdx; colRows = 0; open = false;
            }
            if (!open) { ImGui::BeginGroup(); open = true; }
            drawPresetPopupGroup(g, users);
            colRows += rows[g];
        }
        if (open) ImGui::EndGroup();

       #ifdef MSYNTH_FRAME_PROFILE
        // The popup is its own window, so it has its own 16-bit index budget and
        // is invisible to endLayer(). Sample it here, inside the popup.
        { const int v = ImGui::GetWindowDrawList()->VtxBuffer.Size;
          if (v > vtxMax[kLayerPopup]) vtxMax[kLayerPopup] = v; }
       #endif
    }

    // One group: an accent header with a hairline rule, then its presets. The
    // header is drawn in that MODE's accent (not the live one) so the column a
    // preset lives in is identifiable at a glance even mid-crossfade.
    void drawPresetPopupGroup(int g, const std::vector<scpreset::Entry>& users)
    {
        const bool isUser = (g >= 6);
        const ImU32 col = isUser ? live.accent : kPalettes[g].accent;
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(isUser ? "USER" : kModeNames[g]);
        ImGui::PopStyleColor();
        { const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
          ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + popupColW, b.y),
                                              withA(col, 80), 1.0f); }

        const ImVec2 cell(popupColW, 0.0f);
        if (!isUser)
        {
            for (int i = 0; i < kNumFactoryPresets; ++i)
            {
                if (clampMode(factoryMode[i]) != g) continue;
                const bool sel = (i == currentPreset);
                ImGui::PushID(i);
                if (ImGui::Selectable(kFactoryPresets[i].name, sel, 0, cell)) applyPreset(i);
                if (sel) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        }
        else
        {
            for (int u = 0; u < (int)users.size(); ++u)
            {
                ImGui::PushID(kNumFactoryPresets + u);   // user names may duplicate factory ones
                const bool sel = (currentPreset == kNumFactoryPresets + u);
                if (ImGui::Selectable(users[u].name.c_str(), sel, 0, cell)) applyUserPreset(u);
                if (sel) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        }
    }

    // Widest label in the popup, measured through the face ImGui will actually
    // draw with. Run once per popup opening (IsWindowAppearing), not per frame:
    // it is ~60 string measurements, and nothing it depends on can change while
    // the popup is up — the store is only rescanned by save/delete, both of
    // which live behind modals that cannot be open at the same time.
    void measurePopupColumn()
    {
        ImFont* const f = ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        float w = 0.0f;
        const auto widen = [&](const char* t)
        { w = std::max(w, f->CalcTextSizeA(fs, FLT_MAX, 0.0f, t).x); };
        for (int i = 0; i < kNumFactoryPresets; ++i) widen(kFactoryPresets[i].name);
        for (const auto& e : presetStore.list())     widen(e.name.c_str());
        for (int m = 0; m < 6; ++m)                  widen(kModeNames[m]);
        // A user name can be up to 128 chars (saveNameBuf); cap the column so one
        // long name cannot push the grid off the window. Selectable clips its own
        // label, and the browser (§8.10) is where full names are guaranteed.
        popupColW = std::min(w + 2.0f * ImGui::GetStyle().FramePadding.x + 8.0f * s,
                             (float)getWidth() * 0.32f);
    }

    // Small labelled button in the top bar's preset cluster (BROWSE / SAVE): the
    // chevron chrome with a 10 px accent label instead of a triangle. (9 px was
    // sized for the old 36/40-wide buttons; on the widened ones it read as a
    // caption floating in a box rather than the button's own label.)
    bool barButton(const char* id, float x0, float y0, float x1, float y1,
                   const char* label, const char* tip)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool clicked = ImGui::IsItemClicked();
        const bool hovered = ImGui::IsItemHovered();
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tip);
        dl->AddRectFilled(b0, b1, IM_COL32(38, 38, 41, 255), 4.0f * s);
        dl->AddRect(b0, b1, hovered ? live.accent : IM_COL32(90, 90, 94, 255), 4.0f * s, 0, 1.2f * s);
        text(0.5f * (x0 + x1), 0.5f * (y0 + y1) - 5.0f, 10.0f, live.accent, label, 0, true);
        return clicked;
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
        notifyDspProgramChange();
        currentPreset = kNumFactoryPresets + u;
    }

    void openSaveModal()
    {
        showSaveModal = true;
        saveModalJustOpened = true;
        overwriteConfirm = false;
        deleteConfirm = false;
        saveNameBuf[0] = '\0';
        saveNameHadText = false;
        saveNameDirty = true;
        saveError[0] = '\0';
    }

    // Write values[] to disk, refresh the list, and select the new preset.
    //
    // A FAILED save keeps the modal open and puts the reason in the hint slot. It
    // used to close unconditionally, which made the two failures a player actually
    // meets — a read-only preset folder and a full disk — completely invisible:
    // the modal dismissed exactly as it does on success and the patch was gone.
    // The typed name is deliberately left in the field so the retry (a different
    // name, or after freeing space) costs nothing.
    void commitSave()
    {
        const std::string nm = scpreset::sanitize(saveNameBuf);
        if (nm.empty()) return;
        std::string err;
        if (!presetStore.save(saveNameBuf, values, (int)kNumCoreParams, &err))
        {
            std::snprintf(saveError, sizeof saveError, "Save failed: %s", err.c_str());
            overwriteConfirm = false;   // back out of the confirm, keep name + modal
            return;
        }
        saveError[0] = '\0';
        presetStore.refresh();
        const auto& L = presetStore.list();
        for (int u = 0; u < (int)L.size(); ++u)
            if (L[u].name == nm) { currentPreset = kNumFactoryPresets + u; break; }
        showSaveModal = false;
        overwriteConfirm = false;
    }

    // Same contract as commitSave: a delete that did not happen (read-only folder,
    // file locked) leaves the modal up with the reason, rather than closing and
    // leaving the preset sitting in the list as if nothing had been asked.
    void commitDelete()
    {
        if (currentPreset >= kNumFactoryPresets)
        {
            const int u = currentPreset - kNumFactoryPresets;
            const auto& L = presetStore.list();
            if (u >= 0 && u < (int)L.size() && !presetStore.remove(L[u].path))
            {
                std::snprintf(saveError, sizeof saveError,
                              "Delete failed: could not remove the file");
                deleteConfirm = false;
                return;
            }
            presetStore.refresh();
            currentPreset = -1;
        }
        saveError[0] = '\0';
        showSaveModal = false;
        deleteConfirm = false;
    }

    bool chevron(const char* id, float x0, float y0, float x1, float y1, bool right, const char* tip = nullptr)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool clk = ImGui::IsItemClicked();
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tip);
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
        notifyDspProgramChange();
    }
    void pushParam(uint32_t i, float v)
    { editParameter(i, true); values[i] = v; setParameterValue(i, v); editParameter(i, false); }

    // A MIDI program change (0xC0) is applied by the plugin to ITSELF on the audio
    // thread, and DPF has no plugin->host parameter notification, so neither the
    // host's parameter cache nor this UI's values[] hears about it. Poll the shell's
    // signal each frame and, on a change, run the very same applyPreset() a click on
    // that preset would: the engine already holds those values, so re-pushing them
    // is inaudible, and it is what makes the knobs, the preset name and the host's
    // automation state agree with what is sounding. Split LV2 UI: no bridge, no
    // sync (audio is still correct, the display just lags until the user touches
    // something) — matching every other bridge accessor's fallback.
    void syncMidiProgramChange()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiSynthGetMidiProgramSignal == nullptr) return;
        void* const inst = getPluginInstancePointer();
        if (inst == nullptr) return;
        const uint32_t sig = multiSynthGetMidiProgramSignal(inst);
        if (sig == lastMidiProgramSignal) return;
        lastMidiProgramSignal = sig;
        if (sig != 0u) applyPreset((int)(sig & 0xFFu));
       #endif
    }

    // Tell the engine the push above was a preset load, so the smoothed controls
    // LAND on it rather than gliding. The UI pushes parameters one at a time
    // through the host, which is indistinguishable from automation at the DSP, so
    // the signal has to be explicit. Called after the pushes so the snapshot that
    // consumes it sees the finished patch.
    //
    // Only reachable through the same-process direct-access bridge; a split LV2
    // UI has no pointer and falls back to MultiSynthDSP's bulk-change heuristic.
    void notifyDspProgramChange()
    {
        if (msynth::MultiSynthDSP* d = dspAccess()) d->notifyProgramChange();
    }

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
    // The three oscillator panels are STRUCTURALLY IDENTICAL — section title + wave
    // combo on the top row, then exactly one row of knobs — so they share one
    // geometry instead of three hand-tuned sets. Those had drifted to r20 / r18 / r14
    // on panels of h112 / h112 / h80, a staircase that encoded nothing: OSC 1 read as
    // the "important" oscillator purely because its panel happened to be the tallest,
    // and OSC 3 as an afterthought. Now all three are the same size and the same
    // radius, and adding a knob to one cannot silently desynchronise it from the rest.
    //
    // RADIUS r18 is the house standard for a primary body control, not a compromise:
    // AMP ENV and FILTER ENV — the panels immediately right of this stack, at the same
    // visual weight — use r18 for their four knobs each, as do the S&H rate and the
    // mod-matrix amount. The r13/r14 TICKLESS family belongs to the bottom utility
    // strip (FX + sequencer) and the r13 VOICE/CHARACTER grid is a dense 10-knob
    // matrix; neither is the right neighbour to match here. So: r18, ticks on.
    //
    // HEIGHT. r18 with its tick ring reaches +/-24.5, so a panel needs
    //   30 (label top: title + combo + gap) + 9 (label ink — MEASURED; see
    //   drawSequencer on why the 0.675*size convention in this file's older comments
    //   is wrong) + 4.5 (label -> ring gap) + 49 (ring) + 3 (inner floor)
    //   = 95.5 px minimum, which is exactly what h101 realises.
    // OSC 3's old h80 could not host it — which is also why its r14 tick ring already
    // poked 0.5 px through its own panel floor. Rather than grow the stack (VOICE /
    // CHARACTER below is documented as needing every one of its 166 px), the existing
    // 60..372 block is split EQUALLY: three h101 panels with 4 px gutters. OSC 1 and
    // OSC 2 give up 11 px each, which they had spare; OSC 3 gains 21 px, which it
    // needed. The block ends at 371, so the gutter before VOICE / CHARACTER is 5 px
    // rather than 4 — which reads as the group break it actually is.
    //
    // Per-panel clearances (identical for all three, by construction):
    //   combo bottom -> label top   6.00 px
    //   label ink    -> ring top    4.50 px   (ink bottom = label top + 9, measured)
    //   ring bottom  -> inner floor 5.50 px
    struct OscGeom { float y0, y1, rowY, comboY1, labelY, cy; };
    static OscGeom oscGeom(float y0)
    { return { y0, y0 + 101.0f, y0 + 4.0f, y0 + 24.0f, y0 + 30.0f, y0 + 68.0f }; }
    static constexpr float kOscR = 18.0f;   // shared oscillator knob radius

    void drawOscPanels()
    {
        const OscGeom g1 = oscGeom(60.0f), g2 = oscGeom(165.0f), g3 = oscGeom(270.0f);

        // OSC 1 — DETUNE / PW / LEVEL, plus Cosmos X-MOD. Oracle's defining
        // OSC 2 -> OSC 1 routing belongs exclusively to its Poly-Mod panel; showing
        // X-MOD here as well exposed two controls for effectively the same route.
        panelBox(16, g1.y0, 340, g1.y1);
        sectionTitle(24, g1.rowY, "OSC 1");
        comboBox("o1w", kParamOsc1Wave, 150, g1.rowY, 332, g1.comboY1, kWave5, 5, curMode == 5);
        klabel(60, g1.labelY, "DETUNE");
        if (curMode == 5)
            inertKnob("o1det", kParamOsc1Detune, 60, g1.cy, kOscR, true,
                      "Acid uses a dedicated oscillator; OSC 1 detune is not used.");
        else
            knob("o1det", kParamOsc1Detune, 60, g1.cy, kOscR, "%+.0f", " ct", true);
        klabel(130, g1.labelY, "PW");     knob("o1pw", kParamOsc1PW, 130, g1.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
        klabel(200, g1.labelY, "LEVEL");
        if (curMode == 5)
            inertKnob("o1lvl", kParamOsc1Level, 200, g1.cy, kOscR, false,
                      "Acid has a fixed oscillator gain; OSC 1 level is not used.");
        else
            knob("o1lvl", kParamOsc1Level, 200, g1.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
        // Cross-mod is the Cosmos oscillator treatment. Oracle exposes its
        // equivalent routing in the dedicated Poly-Mod panel below the LFOs.
        if (curMode == 0)
        { klabel(285, g1.labelY, "X-MOD"); knob("xmod", kParamCrossMod, 285, g1.cy, kOscR, "%.0f", " %", false, false, false, 100.0f); }

        // OSC 2 — SEMI / DETUNE / PW / LEVEL. Four columns 58 apart against the same
        // 49 px ring, so 9 px of daylight (VOICE / CHARACTER runs 6, so this is the
        // roomier of the two grids).
        panelBox(16, g2.y0, 340, g2.y1);
        sectionTitle(24, g2.rowY, "OSC 2");
        if (curMode == 5)
        {
            text(178, g2.cy - 13.0f, 11.0f, whiteDimCol(),
                 "SINGLE-OSCILLATOR ENGINE", 0, true);
            text(178, g2.cy + 5.0f, 9.5f,
                 lerpC(live.textPanel, live.panel, 0.42f),
                 "OSC 2 is bypassed in Acid mode", 0, false);
        }
        // Cosmos (mode 0) forces OSC 2 to a Pulse locked to OSC 1's frequency and
        // detune. Render that waveform as a read-only field: the old forced combo
        // still opened and wrote a stored value, even though no selection could
        // change Cosmos sound. PW + LEVEL remain live.
        const bool cosmosOsc2 = (curMode == 0);
        if (curMode != 5)
        {
            if (cosmosOsc2)
            {
                readOnlyField("o2w_fixed", 150, g2.rowY, 332, g2.comboY1,
                              "Pulse  \xC2\xB7  Fixed",
                              "Cosmos fixes OSC 2 to Pulse; pulse width remains adjustable.");
                text(85, g2.labelY, 9.0f, live.accent, "TRACKS OSC 1", 0, true);
                inertKnob("o2semi", kParamOsc2Semi, 56, g2.cy, kOscR, true,
                          "Inactive in Cosmos: OSC 2 tracks OSC 1's pitch.");
                inertKnob("o2det",  kParamOsc2Detune, 114, g2.cy, kOscR, true,
                          "Inactive in Cosmos: OSC 2 uses OSC 1's detune.");
            }
            else
            {
                comboBox("o2w", kParamOsc2Wave, 150, g2.rowY, 332, g2.comboY1,
                         kWave5, 5);
                klabel(56, g2.labelY, "SEMI");
                klabel(114, g2.labelY, "DETUNE");
                knob("o2semi", kParamOsc2Semi, 56, g2.cy, kOscR, "%+.0f", " st", true, true);
                knob("o2det", kParamOsc2Detune, 114, g2.cy, kOscR, "%+.0f", " ct", true);
            }
            klabel(172, g2.labelY, "PW");    knob("o2pw", kParamOsc2PW, 172, g2.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
            klabel(230, g2.labelY, "LEVEL"); knob("o2lvl", kParamOsc2Level, 230, g2.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
        }

        // OSC 3 / SUB — mode-variant contents, but the SAME frame and knob row as the
        // two panels above it in every mode (that is the whole point of the rework).
        panelBox(16, g3.y0, 340, g3.y1);
        if (curMode == 3) // Modular -> osc3
        {
            sectionTitle(24, g3.rowY, "OSC 3");
            comboBox("o3w", kParamOsc3Wave, 150, g3.rowY, 332, g3.comboY1, kWave4, 4, false);
            // Centred as a pair on the panel's midline (178) like the SUB OSC
            // variant's lone LEVEL knob; 90/240 sat 13 px left of centre.
            klabel(118, g3.labelY, "LEVEL");  knob("o3lvl", kParamOsc3Level, 118, g3.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
            klabel(238, g3.labelY, "OSC1\xE2\x86\x92OSC2"); knob("fmamt", kParamFMAmount, 238, g3.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
        }
        else if (curMode == 0 || curMode == 2) // Cosmos / Mono -> sub
        {
            sectionTitle(24, g3.rowY, "SUB OSC");
            comboBox("subw", kParamSubWave, 150, g3.rowY, 332, g3.comboY1, kSubWave, 2, curMode == 5);
            klabel(178, g3.labelY, "LEVEL"); knob("sublvl", kParamSubLevel, 178, g3.cy, kOscR, "%.0f", " %", false, false, false, 100.0f);
        }
        else
        {
            sectionTitle(24, g3.rowY, "AUX OSC");
            text(178, g3.cy - 13.0f, 11.0f, whiteDimCol(),
                 "NO AUXILIARY OSCILLATOR", 0, true);
            text(178, g3.cy + 5.0f, 9.5f,
                 lerpC(live.textPanel, live.panel, 0.42f),
                 curMode == 1 ? "Oracle uses its two-oscillator poly-mod path"
                              : "Acid uses its dedicated single oscillator",
                 0, false);
        }
    }

    void drawMixerVoice()
    {
        if (curMode == 5)
        {
            drawAcidCharacter();
            return;
        }

        // VOICE / CHARACTER — all 14 controls (10 knobs, 3 combos, 1 legato toggle).
        // Ten knobs sit in a 2-row x 5-col grid at the left; the three combos and the
        // LEGATO lamp stack in a right-hand column (x 246..336) wide enough that
        // "S-Curve"/"Linear" never clip. Two geometries share this one body:
        //
        //   B  (modes 0-3): panel 376..542 (h166). r13 knobs — tick ring reaches
        //      R+6.5 -> ±19.5. font-10 knob labels (ink = 0.675*size = 6.75, ink top
        //      = y+1). row centres 430 / 494, label top = centre-32.
        //        Row1: label ink 399..405.75 | ring top 410.5 -> 4.75 px daylight.
        //        Gap : row1 ring bottom 449.5 | row2 label top 463 -> 13.5 px.
        //        Row2: label ink 463..469.75 | ring top 474.5 -> 4.75 px.
        //        Row2 ring bottom 513.5 clears the 539 inner floor by 25.5 px.
        //        Row1 labels clear the title (11 px, ink bottom 386.4) by 12.6 px.
        //        Column spacing 45 >= ring diameter 39 + 4 = 43 (6 px gap).
        //        Right column left edge 246 clears the row's ring (x 241.5) by 4.5 px.
        //   C  (Prism, mode 4): panel 412..542 (h130). r11 knobs -> ring ±17.5.
        //      font-9 knob labels (ink 6.075). row centres 460 / 514, label top
        //      = centre-30.
        //        Row1: label ink 431..437.075 | ring top 442.5 -> 5.425 px.
        //        Row2 ring bottom 531.5 clears the 539 inner floor by 7.5 px.
        //        Row1 labels clear the title (ink bottom 423.4) by 7.6 px.
        const bool prism  = (curMode == 4);
        const float pTop  = prism ? 412.0f : 376.0f;
        const float titleY= prism ? 415.0f : 379.0f;
        const float yc1   = prism ? 460.0f : 430.0f;   // knob row centres
        const float yc2   = prism ? 514.0f : 494.0f;
        const float kr    = prism ? 11.0f  : 13.0f;    // knob radius
        const float labOff= prism ? 30.0f  : 32.0f;    // knob label top above centre
        const float kFont = prism ? 9.0f   : 10.0f;    // knob label font
        const float KX0 = 42.0f, KDX = 45.0f;          // knob columns 42..222
        const float comboH = 9.0f;                     // combo/led half-height
        // Right-hand column: 4 stacked items (OVERSMP, GLIDE, LEGATO, V.CRV). Body
        // centres chosen so each label (baseline at centre-itemLabOff) clears the body
        // above it, and the last body bottom clears the 539 inner floor.
        const float colCx = 291.0f, colHw = 45.0f;     // right column centre / half-width
        const float itemLabOff = 19.0f;                // item label baseline above body centre
        const float iFont = prism ? 8.5f : 9.0f;
        const float is0 = prism ? 436.0f : 416.0f;
        const float is1 = prism ? 464.0f : 452.0f;
        const float is2 = prism ? 492.0f : 488.0f;
        const float is3 = prism ? 520.0f : 524.0f;

        panelBox(16, pTop, 340, 542);
        sectionTitle(24, titleY, "VOICE / CHARACTER");
        auto KX = [&](int c) { return KX0 + c * KDX; };
        auto vLabel = [&](float cx, float y, const char* t)
        { text(cx, y, kFont, live.textPanel, t, 0, true); };
        auto iLabel = [&](float cx, float cy, const char* t)
        { text(cx, cy - itemLabOff, iFont, live.textPanel, t, 0, true); };
        auto iCombo = [&](const char* id, uint32_t p, float cy,
                          const char* const* opts, int n, const char* lab)
        { iLabel(colCx, cy, lab);
          comboBox(id, p, colCx - colHw, cy - comboH,
                   colCx + colHw, cy + comboH, opts, n); };

        // Row 1 — CHARACTER (noise/analog/vintage/tune) + unison voices.
        vLabel(KX(0), yc1 - labOff, "NOISE");
        knob("noise", kParamNoiseLevel, KX(0), yc1, kr,
             "%.0f", " %", false, false, false, 100.0f);
        vLabel(KX(1), yc1 - labOff, "ANALOG");
        knob("analog", kParamAnalogAmt, KX(1), yc1, kr,
             "%.0f", " %", false, false, false, 100.0f);
        vLabel(KX(2), yc1 - labOff, "VNTG");  knob("vntg", kParamVintage, KX(2), yc1, kr, "%.0f", " %", false, false, false, 100.0f);
        vLabel(KX(3), yc1 - labOff, "TUNE");
        knob("mtune", kParamMasterTune, KX(3), yc1, kr, "%+.0f", " ct", true);
        vLabel(KX(4), yc1 - labOff, "UNI V");
        knob("univ", kParamUnisonVoices, KX(4), yc1, kr,
             "%.0f", "", false, true);

        // Row 2 — VOICE (unison detune/spread, porta, velocity, pitch-bend). This is
        // the performance row, so it carries permanent read-outs (geometry B: ink
        // 515..521.4, 17.6 px above the 539 inner floor). Geometry C (Prism) packs
        // the same row at yc2 = 514 with r11 knobs, where the read-out would land at
        // 533..539.4 and cross the floor — so it is suppressed there; the hover
        // bubble still covers it. Row 1 is set-and-forget character, no read-outs.
        const bool ro = readoutsOn() && !prism;
        vLabel(KX(0), yc2 - labOff, "UNI DT");
        knob("unidt", kParamUnisonDetune, KX(0), yc2, kr,
             "%.0f", " ct", false, false, ro);
        vLabel(KX(1), yc2 - labOff, "UNI SP");
        knob("unisp", kParamUnisonSpread, KX(1), yc2, kr,
             "%.0f", " %", false, false, ro, 100.0f);
        vLabel(KX(2), yc2 - labOff, "PORTA");
        knob("porta", kParamPortaTime, KX(2), yc2, kr,
             "%.2f", " s", false, false, ro, 1.0f, 0.0f, true, true);
        vLabel(KX(3), yc2 - labOff, "VEL");
        knob("vels", kParamVelSens, KX(3), yc2, kr,
             "%.0f", " %", false, false, ro, 100.0f);
        vLabel(KX(4), yc2 - labOff, "PB");
        knob("pb", kParamPbRange, KX(4), yc2, kr,
             "%.0f", " st", false, true, ro);

        // Right column — 3 combos + LEGATO lamp.
        iCombo("ovs",   kParamOversampling, is0, kOversmp, 3, "OVERSMP");
        iCombo("glide", kParamGlideMode, is1, kGlide, 2, "GLIDE");
        compactToggle("legato", kParamLegato, colCx - 33.0f, is2 - comboH,
                      colCx + 33.0f, is2 + comboH, "LEGATO");
        iCombo("vcrv", kParamVelCurve, is3, kVelCurve, 4, "V.CRV");
    }

    void drawAcidCharacter()
    {
        panelBox(16, 376, 340, 542);
        sectionTitle(24, 379, "ACID CHARACTER");

        // Only controls consumed by the dedicated Acid path or its shared output
        // stage live here. The previous generic voice grid contained ten dim knobs
        // and three N/A fields, making the two real controls harder to find.
        klabel(82, 410, "VINTAGE");
        knob("acid_vntg", kParamVintage, 82, 458, 24, "%.0f", " %",
             false, false, readoutsOn(), 100.0f);

        text(242, 408, 9.5f, live.textPanel, "OVERSAMPLING", 0, true);
        comboBox("acid_ovs", kParamOversampling, 158, 422, 326, 446,
                 kOversmp, 3, true);

        text(242, 460, 9.0f, live.textPanel, "VOICE ARCHITECTURE", 0, true);
        const char* const tags[3] = { "MONO", "LAST NOTE", "TIED SLIDES" };
        const float tx[4] = { 158, 207, 267, 326 };
        for (int i = 0; i < 3; ++i)
        {
            dl->AddRectFilled(P(tx[i], 476), P(tx[i + 1] - 4, 496),
                              lerpC(IM_COL32(36, 38, 42, 255), live.accent, 0.10f),
                              3.0f * s);
            dl->AddRect(P(tx[i], 476), P(tx[i + 1] - 4, 496),
                        withA(live.accent, 120), 3.0f * s, 0, 1.0f * s);
            text(0.5f * (tx[i] + tx[i + 1] - 4), 482, i == 2 ? 7.5f : 8.0f,
                 IM_COL32(222, 224, 228, 255), tags[i], 0, true);
        }
        text(242, 510, 9.0f, whiteDimCol(),
             "Dedicated single-voice acid engine", 0, false);
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

        // RES / ENV AMT / HP all carry a permanent read-out (y 270, clear of the 297
        // panel floor): they are the row you ride while playing.
        const bool ro = readoutsOn();
        klabel(556, 184, "RES");    knob("res", kParamFilterRes, 556, 232, 30, "%.0f", " %", false, false, ro, 100.0f);
        klabel(636, 184, "ENV AMT");knob("fenvamt", kParamFilterEnvAmt, 636, 232, 30, "%+.0f", " %", true, false, ro, 100.0f);
        if (curMode == 0) // Cosmos HP
        { klabel(712, 184, "HP"); knob("hp", kParamFilterHP, 712, 232, 30, "%.0f", " Hz", false, false, ro); }
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
        if (curMode == 5)
        {
            drawAcidEnvelope();
            return;
        }

        panelBox(348, 304, 548, 542);
        sectionTitle(356, 308, "AMP ENV");
        drawADSR(356, 320, 540, 420, kParamAmpA, kParamAmpS, ampEnv, ampHash, false);
        drawADSRKnobs(380, kParamAmpA, "amp");
        comboBox("ampcrv", kParamAmpCurve, 360, 514, 536, 538, kEnvCurve, 4);

        panelBox(552, 304, 752, 542);
        sectionTitle(560, 308, "FILTER ENV");
        drawADSR(560, 320, 744, 420, kParamFiltA, kParamFiltS, filtEnv, filtHash, true);
        drawADSRKnobs(584, kParamFiltA, "filt");
        comboBox("filtcrv", kParamFiltCurve, 564, 514, 740, 538, kEnvCurve, 4);
    }

    // Acid has one fixed-fast-attack envelope shared by amplitude and filter.
    // Showing the two generic four-stage envelopes made six inert controls look
    // editable, so this mode gets one truthful, wider envelope with only the
    // Decay and Sustain parameters the engine actually consumes.
    void drawAcidEnvelope()
    {
        panelBox(348, 304, 752, 542);
        sectionTitle(356, 308, "ACID ENVELOPE \xC2\xB7 AMP + FILTER");

        constexpr float rx0 = 356.0f, ry0 = 324.0f, rx1 = 744.0f, ry1 = 420.0f;
        dl->AddRectFilled(P(rx0, ry0), P(rx1, ry1), IM_COL32(10, 12, 14, 255), 4.0f * s);

        const float D = values[kParamAmpD], S = values[kParamAmpS];
        const float h = D * 3.1f + S * 7.3f + 501.0f;
        if (h != acidEnvHash)
        {
            acidEnvHash = h;
            computeADSR(rx0, ry0, rx1, ry1, 0.003f, D, S,
                        msynth::AcidVoice::kRelease, 1, acidEnv);
        }
        for (int i = 0; i + 1 < kAdsrN; ++i)
            dl->AddQuadFilled(P(acidEnv[i].x, acidEnv[i].y),
                              P(acidEnv[i + 1].x, acidEnv[i + 1].y),
                              P(acidEnv[i + 1].x, ry1), P(acidEnv[i].x, ry1),
                              withA(live.accent, 40));
        ImVec2 line[kAdsrN];
        for (int i = 0; i < kAdsrN; ++i) line[i] = P(acidEnv[i].x, acidEnv[i].y);
        dl->AddPolyline(line, kAdsrN, live.accent, 0, 2.0f * s);
        dl->AddRect(P(rx0, ry0), P(rx1, ry1), IM_COL32(0, 0, 0, 180),
                    4.0f * s, 0, 1.2f * s);

        const bool ro = readoutsOn();
        klabel(470, 438, "DECAY");
        knob("aciddecay", kParamAmpD, 470, 478, 22, "%.0f", " ms",
             false, false, ro, 1000.0f, 0.0f, true, true);
        klabel(630, 438, "SUSTAIN");
        knob("acidsustain", kParamAmpS, 630, 478, 22, "%.0f", " %",
             false, false, ro, 100.0f);
        text(550, 520, 9.5f, whiteDimCol(),
             "FAST ATTACK + RELEASE \xC2\xB7 SHARED SIGNAL SHAPE", 0, true);
    }

    // ADSR knob row: A,D,S,R at r18, spaced 46 px. The label+knob block is centered
    // in the band between the envelope display (bottom y=420) and the Curve combo
    // (top y=514): labels top y=436, knob centers y=474, so the block spans
    // 436..498.5 (tick ring reaches R+6.5 = ±24.5) leaving 16 px above the labels
    // and 15.5 px below the ring — visually centered. Label ink bottom (~445)
    // clears the ring top (449.5) by 4.5 px; the combo bottom (538) clears the
    // panel inner floor (539) by 1 px. base = param index of A.
    void drawADSRKnobs(float x0, uint32_t baseA, const char* pfx)
    {
        const char* labs[4] = { "A", "D", "S", "R" };
        for (int i = 0; i < 4; ++i)
        {
            char id[16]; std::snprintf(id, sizeof(id), "%s%s", pfx, labs[i]);
            const float cx = x0 + i * 46.0f;
            klabel(cx, 436, labs[i]);
            // Permanent read-out at y 500..506.4 — 1.5 px under the knob body and
            // 7.6 px above the Curve combo (514).
            const bool ro = readoutsOn();
            if (i == 2) knob(id, baseA + i, cx, 474, 18, "%.0f", " %", false, false, ro, 100.0f); // sustain
            else        knob(id, baseA + i, cx, 474, 18, "%.0f", " ms", false, false, ro, 1000.0f, 0.0f, true, true); // times, auto s
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
        if (curMode == 5)
        {
            drawAcidEngineOverview();
            return;
        }
        drawOneLFO(760, 60, 1000, 190, "LFO 1", kParamLfo1Rate, kParamLfo1Shape, kParamLfo1Fade, kParamLfo1Sync, "l1");
        drawOneLFO(760, 194, 1000, 324, "LFO 2", kParamLfo2Rate, kParamLfo2Shape, kParamLfo2Fade, kParamLfo2Sync, "l2");
    }

    void drawAcidEngineOverview()
    {
        panelBox(760, 60, 1000, 324);
        sectionTitle(768, 64, "ACID ENGINE");
        text(880, 88, 9.5f, live.textPanel,
             "DEDICATED MONO SIGNAL PATH", 0, true);

        const float bx[4] = { 776, 838, 920, 984 };
        const char* const blocks[3] = { "OSC", "18 dB FILTER", "VCA" };
        for (int i = 0; i < 3; ++i)
        {
            dl->AddRectFilled(P(bx[i], 116), P(bx[i + 1] - 8, 158),
                              lerpC(IM_COL32(30, 33, 36, 255), live.accent, 0.09f),
                              4.0f * s);
            dl->AddRect(P(bx[i], 116), P(bx[i + 1] - 8, 158),
                        withA(live.accent, 145), 4.0f * s, 0, 1.1f * s);
            text(0.5f * (bx[i] + bx[i + 1] - 8), 131,
                 i == 1 ? 8.0f : 9.5f, IM_COL32(229, 231, 234, 255),
                 blocks[i], 0, true);
            if (i < 2)
            {
                const float ax = bx[i + 1] - 5;
                dl->AddLine(P(ax - 4, 137), P(ax + 3, 137), live.accent, 1.5f * s);
                dl->AddTriangleFilled(P(ax + 5, 137), P(ax, 133), P(ax, 141),
                                      live.accent);
            }
        }

        const int selectedWave = std::max(0, std::min(4,
            (int)std::lround(values[kParamOsc1Wave])));
        const int wi = (selectedWave == 1 || selectedWave == 4) ? 1 : 0;
        char oscLine[48];
        std::snprintf(oscLine, sizeof oscLine, "%s OSCILLATOR", kWave5[wi]);
        text(880, 174, 9.0f, live.accent, oscLine, 0, true);

        text(880, 208, 9.5f, live.textPanel, "PLAY BEHAVIOR", 0, true);
        const char* const tags[3] = { "LAST NOTE", "ACCENT", "TIED SLIDES" };
        const float tx[4] = { 776, 844, 910, 984 };
        for (int i = 0; i < 3; ++i)
        {
            dl->AddRectFilled(P(tx[i], 228), P(tx[i + 1] - 6, 254),
                              IM_COL32(35, 37, 41, 255), 4.0f * s);
            dl->AddRect(P(tx[i], 228), P(tx[i + 1] - 6, 254),
                        withA(live.accent, 120), 4.0f * s, 0, 1.0f * s);
            text(0.5f * (tx[i] + tx[i + 1] - 6), 236,
                 i == 2 ? 7.5f : 8.0f, IM_COL32(218, 220, 224, 255),
                 tags[i], 0, true);
        }
        text(880, 274, 9.0f, whiteDimCol(),
             "Accent and slide follow the pattern lanes below.", 0, false);
        text(880, 292, 9.0f, whiteDimCol(),
             "LFO and matrix routing are bypassed in this mode.", 0, false);
    }

    void drawOneLFO(float x0, float y0, float x1, float y1, const char* title,
                    uint32_t rate, uint32_t shape, uint32_t fade, uint32_t sync, const char* pfx)
    {
        panelBox(x0, y0, x1, y1);
        sectionTitle(x0 + 8, y0 + 4, title);
        char id[24];
        std::snprintf(id, sizeof(id), "%srate", pfx);
        // Permanent read-outs at y0+98..y0+104.4: clear of the panel floor (y0+127)
        // and, in x, of the SYNC lamp column (x0+150..x0+232).
        const bool ro = readoutsOn();
        klabel(x0 + 46, y0 + 26, "RATE"); knob(id, rate, x0 + 46, y0 + 68, 22, "%.2f", " Hz", false, false, ro);
        std::snprintf(id, sizeof(id), "%sfade", pfx);
        klabel(x0 + 116, y0 + 26, "FADE"); knob(id, fade, x0 + 116, y0 + 68, 22, "%.2f", " s", false, false, ro, 1.0f, 0.0f, true, true);
        text(x0 + 168, y0 + 24, 9.5f, live.textPanel, "SHAPE", 0, true);
        std::snprintf(id, sizeof(id), "%sshape", pfx);
        comboBox(id, shape, x0 + 150, y0 + 38, x0 + 232, y0 + 58, kLfoShape, 5);
        std::snprintf(id, sizeof(id), "%ssync", pfx);
        compactToggle(id, sync, x0 + 160, y0 + 76, x0 + 222, y0 + 96, "SYNC");
    }

    void drawModeSubPanelRegion()
    {
        panelBox(760, 328, 1000, 462);
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
        const char* const labs[4] = { "OFF", "I", "II", "I+II" };
        const float edges[5] = { 776, 826, 876, 926, 984 };
        text(880, 355, 9.0f, mulA(live.textPanel, a), "MODE", 0, true);
        for (int i = 0; i < 4; ++i)
        {
            const bool on = (cur == i);
            char id[16]; std::snprintf(id, sizeof(id), "cho%d", i);
            const float x0 = edges[i], x1 = edges[i + 1];
            const ImVec2 b0 = P(x0, 373), b1 = P(x1, 409);
            if (it)
            {
                ImGui::SetCursorScreenPos(b0);
                ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
                if (ImGui::IsItemClicked()) setChoice(kParamCosmosChorus, i);
                if (tips[kParamCosmosChorus]
                    && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s", tips[kParamCosmosChorus]);
            }
            const ImU32 off = IM_COL32(34, 36, 40, 255);
            const ImDrawFlags corners =
                i == 0 ? ImDrawFlags_RoundCornersLeft
                       : i == 3 ? ImDrawFlags_RoundCornersRight : 0;
            dl->AddRectFilled(b0, b1, mulA(on ? lerpC(off, live.accent, 0.30f) : off, a),
                              4.0f * s, corners);
            dl->AddRect(b0, b1,
                        mulA(on ? live.accent : IM_COL32(78, 81, 86, 255), a),
                        4.0f * s, corners,
                        (on ? 1.6f : 1.0f) * s);
            const float cx = 0.5f * (x0 + x1);
            text(cx, 385, 10.0f,
                 mulA(on ? IM_COL32(240, 241, 243, 255)
                         : IM_COL32(158, 160, 165, 255), a),
                 labs[i], 0, on);
            if (on)
                dl->AddRectFilled(P(x0 + 5, 405), P(x1 - 5, 408),
                                  mulA(live.accent, a), 1.5f * s);
        }
        text(880, 424, 9.0f, mulA(whiteDimCol(), a),
             cur == 0 ? "BYPASSED"
                      : cur == 1 ? "CHORUS I"
                                 : cur == 2 ? "CHORUS II" : "CHORUS I + II",
             0, true);
    }
    void drawSubOracle(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "POLY-MOD \xC2\xB7 SYNC", -1, true);

        // Two hardware sources, each able to reach all three destinations.
        constexpr float dividerX = 883.0f;
        dl->AddLine(P(dividerX, 350), P(dividerX, 450),
                    mulA(lerpC(live.textPanel, live.panel, 0.68f), a), 1.0f * s);
        text(825, 350, 9.5f, mulA(live.textPanel, a), "FILTER ENV", 0, true);
        text(943, 350, 9.5f, mulA(live.textPanel, a), "OSC 2", 0, true);
        dl->AddLine(P(778, 363), P(871, 363), mulA(live.accent, a * 0.55f), 1.0f * s);
        dl->AddLine(P(895, 363), P(990, 363), mulA(live.accent, a * 0.55f), 1.0f * s);

        const uint32_t pp[6] = {
            kParamPmFenvOscA, kParamPmFenvPWM, kParamPmFenvFilt,
            kParamPmOscBOscA, kParamPmOscBPWM, kParamPmOscBFilt
        };
        const char* const ids[6] = {
            "pm_fenv_osc1", "pm_fenv_pwm", "pm_fenv_filter",
            "pm_osc2_osc1", "pm_osc2_pwm", "pm_osc2_filter"
        };
        const char* const dest[6] = {
            "OSC 1", "PW", "FILT", "OSC 1", "PW", "FILT"
        };
        const float cx[6] = { 790, 823, 856, 910, 943, 976 };
        constexpr float cy = 387.0f;
        for (int i = 0; i < 6; ++i)
        {
            if (it) knob(ids[i], pp[i], cx[i], cy, 14, "%.0f", " %",
                         false, false, false, 100.0f);
            else    ghostKnob(pp[i], cx[i], cy, 14, a);
            text(cx[i], 410, 8.5f, mulA(whiteDimCol(), a), dest[i], 0, true);
        }
        compactToggle("oracle_sync", kParamHardSync, 850, 427, 916, 449,
                      "SYNC", it, a);
        text(956, 438, 8.5f, mulA(whiteDimCol(), a), "OSC 1 \xE2\x86\x92 OSC 2", 0, true);
    }
    void drawSubMono(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "RING \xC2\xB7 SYNC", -1, true);
        text(820, 356, 10.0f, mulA(live.textPanel, a), "RING", 0, true);
        if (it) knob("ringm", kParamRingMod, 820, 400, 22, "%.0f", " %", false, false, false, 100.0f);
        else    ghostKnob(kParamRingMod, 820, 400, 22, a);
        text(940, 356, 10.0f, mulA(live.textPanel, a), "HARD SYNC", 0, true);
        compactToggle("hsync", kParamHardSync, 909, 390, 971, 410, "",
                      it, a);
    }
    void drawSubModular(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "AUDIO PATCH \xC2\xB7 FILTER", -1, true);
        const float cx[4] = { 790, 842, 894, 946 };
        const char* const labels[4] = { "S&H", "OSC2\xE2\x86\x92OSC1", "OSC3\xE2\x86\x92VCF", "RING" };
        for (int i = 0; i < 4; ++i)
            text(cx[i], 354, 8.5f, mulA(live.textPanel, a), labels[i], 0, true);
        if (it)
        {
            knob("shrate", kParamShRate, cx[0], 390, 16, "%.2f", " Hz");
            knob("mod21", kParamModOsc2Osc1, cx[1], 390, 16, "%.0f", " %",
                 false, false, false, 100.0f);
            knob("mod3f", kParamModOsc3Filter, cx[2], 390, 16, "%.0f", " %",
                 false, false, false, 100.0f);
            knob("modring", kParamRingMod, cx[3], 390, 16, "%.0f", " %",
                 false, false, false, 100.0f);
        }
        else
        {
            ghostKnob(kParamShRate, cx[0], 390, 16, a);
            ghostKnob(kParamModOsc2Osc1, cx[1], 390, 16, a);
            ghostKnob(kParamModOsc3Filter, cx[2], 390, 16, a);
            ghostKnob(kParamRingMod, cx[3], 390, 16, a);
        }
        compactToggle("modsync", kParamHardSync, 778, 428, 842, 450, "SYNC",
                      it, a);
        if (it)
            comboBox("modfilt", kParamModFilterModel, 858, 425, 988, 451,
                     kModFilterOpt, 2);
        else
        {
            const int model = std::max(0, std::min(1,
                (int)std::lround(values[kParamModFilterModel])));
            readOnlyField("modfilt_fixed", 858, 425, 988, 451,
                          kModFilterOpt[model], tips[kParamModFilterModel], a);
        }
    }
    void drawSubPrism(float a, bool it) { drawAlgoWidget(760, 328, 1000, 462, a, it); }
    void drawSubAcid(float a, bool it)
    {
        text(768, 332, 11.0f, mulA(live.accent, a), "ACID", -1, true);
        text(806, 356, 10.0f, mulA(live.textPanel, a), "ACCENT", 0, true);
        if (it) knob("acc", kParamAcidAccentAmt, 806, 400, 22, "%.0f", " %", false, false, false, 100.0f);
        else    ghostKnob(kParamAcidAccentAmt, 806, 400, 22, a);
        text(890, 356, 10.0f, mulA(live.textPanel, a), "SLIDE", 0, true);
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
    // BUDGET NOTE: this is the single heaviest thing the UI draws. Its 37 r13
    // knobs put MSleft at 49466 / 65535 vertices in Prism (75.5%) — every other
    // layer in every other mode sits between 10k and 22k. See the census comment
    // on endLayer() for the measured per-knob cost (~968 vertices) and re-run the
    // -DMSYNTH_FRAME_PROFILE census before adding anything here.
    void drawPrismOps()
    {
        panelBox(16, 60, 340, 408);
        sectionTitle(24, 64, "OPERATOR MATRIX");
        const msynth::PrismAlgo& alg = msynth::kPrismAlgos[clampAlgo()];
        // Each operator gets its own block with two grouped sub-rows of larger,
        // tickless knobs (r13, accent-arc STROKE spans R+1.8..R+4.2 -> reach ±17.2)
        // so every control is legible (defect 3):
        //   sub-row 1: RATIO · FINE · LEVEL | VEL · KEY
        //   sub-row 2: A · D · S · R        (+ FB on op 4)
        // Strip pitch 80: top = 84 + op*80, sub-row centres cy1 = top+18, cy2 = top+58
        // (40 apart). Labels font 9.5 (ink ~6.4 px from y+1) at centre-22, and they are
        // drawn AFTER the knobs: at mid-range values the accent arc passes through 12
        // o'clock, i.e. through the label's bottom ink band (arc band top+40.8..+42.2
        // vs row-2 ink top+37..+43.4) — label-last ordering keeps the letter ink on
        // top instead of letting the arc slice it (the round-2 illegibility). Row-1
        // arc bottom band ends top+35.2, 1.8 px above the row-2 ink top (top+37).
        // Columns cxc spacing 54 >= arc-reach diameter 34.4 + 4. Last strip bottom
        // (84+3*80+58+17.2 = 399.2) clears the panel inner floor (405) by ~6 px.
        const float cxc[5] = { 96.0f, 150.0f, 204.0f, 258.0f, 312.0f };
        const float kr = 13.0f;
        for (int op = 0; op < 4; ++op)
        {
            const float top = 84.0f + op * 80.0f;
            const float cy1 = top + 18.0f, cy2 = top + 58.0f;
            const bool carrier = (alg.carrierMask >> op) & 1;
            const uint32_t base = kParamOp1Ratio + op * 9;

            if (op > 0) dl->AddLine(P(22, top - 6), P(332, top - 6), withA(live.textPanel, 30), 1.0f * s);
            // op number + carrier/modulator LED
            panel.led(dl, 34, top + 34, carrier, 3.4f);
            char lab[8]; std::snprintf(lab, sizeof(lab), "OP %d", op + 1);
            text(48, top + 28, 10.0f, carrier ? live.accent : live.textPanel, lab, -1, true);
            text(48, top + 42, 7.5f, withA(live.textPanel, 150), carrier ? "carrier" : "mod", -1, false);
            // Permanent RATIO x LEVEL read-out for the operator. There is no free
            // band under the op knobs (row 1's would land in row 2's label chips,
            // row 2's in the next strip's divider), so the two values that define
            // an operator's voice ride in the strip's left gutter instead: centred
            // on x 48 at font 8.5 the widest string ("16.00x 100%") spans ~28..68,
            // inside the panel wall (19) and clear of column 0's arc reach (78.8).
            if (readoutsOn())
            {
                char ov[24];
                std::snprintf(ov, sizeof ov, "%.2f\xC3\x97 %.0f%%",
                              values[base + 0], values[base + 2] * 100.0f);
                text(48, top + 51, 8.5f, whiteDimCol(), ov, 0);
            }
            // group divider between LEVEL and VEL in sub-row 1
            dl->AddLine(P(231, top + 8), P(231, top + 32), withA(live.textPanel, 30), 1.0f * s);

            char id[24];
            // knobs first ...
            std::snprintf(id, sizeof(id), "op%dR", op); knobRatio(id, base + 0, cxc[0], cy1, kr, false);
            std::snprintf(id, sizeof(id), "op%dF", op); knob(id, base + 1, cxc[1], cy1, kr, "%+.0f", " ct", true, false, false, 1.0f, 0.0f, false);
            std::snprintf(id, sizeof(id), "op%dL", op); knob(id, base + 2, cxc[2], cy1, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            std::snprintf(id, sizeof(id), "op%dV", op); knob(id, base + 3, cxc[3], cy1, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            std::snprintf(id, sizeof(id), "op%dK", op); knob(id, base + 4, cxc[4], cy1, kr, "%+.0f", " %", true, false, false, 100.0f, 0.0f, false);
            std::snprintf(id, sizeof(id), "op%dA", op); knob(id, base + 5, cxc[0], cy2, kr, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, false, true);
            std::snprintf(id, sizeof(id), "op%dD", op); knob(id, base + 6, cxc[1], cy2, kr, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, false, true);
            std::snprintf(id, sizeof(id), "op%dS", op); knob(id, base + 7, cxc[2], cy2, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            std::snprintf(id, sizeof(id), "op%dRl", op); knob(id, base + 8, cxc[3], cy2, kr, "%.0f", " ms", false, false, false, 1000.0f, 0.0f, false, true);
            if (op == 3) // feedback op hosts the FB knob, aligned in the KEY column
                knob("prismfb", kParamPrismFB, cxc[4], cy2, kr, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            // ... labels after, each on a small panel-colored chip: at high values
            // the accent arc passes through 12 o'clock, i.e. straight through the
            // label band, and letter-ink-over-arc alone still reads as "covered"
            // (the arc line crosses the glyphs). The chip masks the arc under the
            // whole label so the text always sits on a clean plate.
            auto L = [&](float cx, float y, const char* t, bool accent = false)
            {
                const float hw = 0.5f * ((float)std::strlen(t) * 5.3f + 5.0f);
                dl->AddRectFilled(P(cx - hw, y - 23.0f), P(cx + hw, y - 13.0f),
                                  live.panel, 2.0f * s);
                text(cx, y - 22.0f, 9.5f, accent ? live.accent : live.textPanel, t, 0, true);
            };
            L(cxc[0], cy1, "RATIO"); L(cxc[1], cy1, "FINE"); L(cxc[2], cy1, "LEVEL");
            L(cxc[3], cy1, "VEL");   L(cxc[4], cy1, "KEY");
            L(cxc[0], cy2, "A"); L(cxc[1], cy2, "D"); L(cxc[2], cy2, "S"); L(cxc[3], cy2, "R");
            if (op == 3) L(cxc[4], cy2, "FB", true);
        }
    }

    int clampAlgo() const { int a = (int)std::lround(values[kParamPrismAlgo]); return a < 0 ? 0 : (a > 7 ? 7 : a); }

    void drawAlgoWidget(float x0, float y0, float x1, float y1, float a, bool it)
    {
        text(x0 + 8, y0 + 4, 11.0f, mulA(live.accent, a), "ALGORITHM", -1, true);
        const int active = clampAlgo();

        // Eight tiny topology thumbnails were too small to decode and left only a
        // shallow strip for the selected routing. Use a compact numbered/name
        // selector on the left and devote the rest of the panel to one diagram
        // large enough to trace.
        const float listX0 = x0 + 8.0f, listX1 = x0 + 118.0f;
        const float listY0 = y0 + 22.0f;
        constexpr float rowPitch = 12.5f, rowH = 11.5f;
        for (int i = 0; i < 8; ++i)
        {
            const float ty = listY0 + i * rowPitch;
            char id[16]; std::snprintf(id, sizeof(id), "algo%d", i);
            if (it)
            {
                ImGui::SetCursorScreenPos(P(listX0, ty));
                ImGui::InvisibleButton(id, ImVec2((listX1 - listX0) * s, rowH * s));
                if (ImGui::IsItemClicked()) setChoice(kParamPrismAlgo, i);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Algorithm %d: %s", i + 1, msynth::kPrismAlgos[i].name);
            }
            const bool on = (i == active);
            dl->AddRectFilled(P(listX0, ty), P(listX1, ty + rowH),
                              mulA(on ? lerpC(live.panel, live.accent, 0.20f)
                                      : IM_COL32(10, 20, 22, 255), a),
                              2.0f * s);
            if (on)
                dl->AddRectFilled(P(listX0, ty), P(listX0 + 3, ty + rowH),
                                  mulA(live.accent, a), 1.0f * s);
            char label[40];
            std::snprintf(label, sizeof(label), "%d  %s", i + 1, msynth::kPrismAlgos[i].name);
            text(listX0 + 7, ty + 1.0f, 8.5f,
                 mulA(on ? live.textPanel : whiteDimCol(), a), label, -1, on);
        }

        const float dividerX = x0 + 124.0f;
        dl->AddLine(P(dividerX, y0 + 20.0f), P(dividerX, y1 - 8.0f),
                    mulA(lerpC(live.textPanel, live.panel, 0.65f), a), 1.0f * s);

        const msynth::PrismAlgo& selected = msynth::kPrismAlgos[active];
        int carrierCount = 0;
        for (int i = 0; i < 4; ++i)
            if ((selected.carrierMask >> i) & 1) ++carrierCount;
        char selectedLabel[64];
        std::snprintf(selectedLabel, sizeof(selectedLabel), "%d \xC2\xB7 %s \xC2\xB7 %d OUT%s",
                      active + 1, selected.name, carrierCount, carrierCount == 1 ? "" : "S");
        const float detailX0 = x0 + 128.0f;
        const float detailX1 = x1 - 4.0f;
        text(0.5f * (detailX0 + detailX1), y0 + 21.0f, 8.5f,
             mulA(live.textPanel, a), selectedLabel, 0, true);

        drawAlgoDiagram(active, detailX0, y0 + 32.0f, detailX1, y1 - 8.0f, a, true);
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
            dl->AddLine(ImVec2(opc[i].x, opc[i].y + box), ImVec2(opc[i].x, P(0, busY).y),
                        mulA(live.accent, a), (big ? 1.6f : 0.9f) * s);
            busPts[nb++] = opc[i];
        }
        if (big && nb > 0)
        {
            float minx = 1e9f, maxx = -1e9f;
            for (int i = 0; i < nb; ++i) { minx = std::min(minx, busPts[i].x); maxx = std::max(maxx, busPts[i].x); }
            const float by = P(0, busY).y;
            // A single carrier previously produced a zero-length "bus", making
            // the bottom of half the algorithms look unfinished.
            if (maxx - minx < 2.0f * s)
            {
                minx -= 9.0f * s;
                maxx += 9.0f * s;
            }
            dl->AddLine(ImVec2(minx, by), ImVec2(maxx, by), mulA(live.accent, a), 2.2f * s);
            text((0.5f * (minx + maxx) - org.x) / s, busY + 2.0f, 7.5f,
                 mulA(live.accent, a), "OUT", 0, true);
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
            text((c.x - org.x) / s + box / s + 2.0f,
                 (c.y - org.y) / s - box / s - 6.0f, 7.0f,
                 mulA(live.ledOn, a), "FB", -1, true);
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
        if (curMode == 5)
        {
            // Acid is rendered by AcidVoice, outside the poly voice/mod-matrix
            // path. Replace the clickable matrix launcher with an explicit routing
            // summary so the surface never promises modulation that cannot sound.
            panelBox(760, 466, 1000, 542);
            sectionTitle(768, 470, "ACID ROUTING");
            dl->AddRectFilled(P(768, 493), P(992, 534),
                              IM_COL32(39, 40, 44, 255), 4.0f * s);
            dl->AddRect(P(768, 493), P(992, 534), withA(live.accent, 110),
                        4.0f * s, 0, 1.0f * s);
            text(880, 499, 9.5f, IM_COL32(229, 231, 234, 255),
                 "FIXED MONO ROUTING", 0, true);
            text(880, 516, 8.5f, whiteDimCol(),
                 "No modulation-matrix processing", 0, false);
            return;
        }

        // Panel extended 518 -> 542 (+24 lower-body shift); the clickable button grows
        // vertically, centred in the taller panel (mid 504).
        panelBox(760, 466, 1000, 542);
        const ImVec2 b0 = P(768, 474), b1 = P(992, 534);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton("modbar", ImVec2(b1.x - b0.x, b1.y - b0.y));
        if (ImGui::IsItemClicked()) { showMod = !showMod; modPopupWasOpen = false; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Open the modulation matrix");
        dl->AddRectFilled(b0, b1, IM_COL32(40, 40, 43, 255), 4.0f * s);
        dl->AddRect(b0, b1, showMod ? live.accent : IM_COL32(90, 90, 94, 255), 4.0f * s, 0, 1.4f * s);
        panel.led(dl, 782, 504, showMod, 4.0f);
        text(886, 496, 12.0f, IM_COL32(238, 238, 240, 255), "MOD MATRIX", 0, true);
        char cnt[24]; std::snprintf(cnt, sizeof(cnt), "%d active", activeModSlots());
        text(886, 514, 9.0f, live.accent, cnt, 0);
    }
    void drawModMatrixOverlay()
    {
        if (!showMod) return;
        // Was a Source/Dest dropdown open at the START of this frame? Needed by the
        // Esc handler at the bottom — see there.
        const bool popupWasOpen = modPopupWasOpen;
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Close the modulation matrix");
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
            // Permanent read-out: the depth of a routing is what you dial by ear.
            // Placed BESIDE the knob (dead space x 812..968, between the knob's arc
            // and the clear-row ✕) rather than under it — rows are only 58 apart, so
            // an under-knob read-out sits almost exactly between two knobs and reads
            // as a caption for the wrong one.
            if (readoutsOn())
            { char ab[24]; std::snprintf(ab, sizeof ab, "%+.0f %%", values[kParamModAmt0 + r] * 100.0f);
              text(816, y + 14, 11.0f, whiteDimCol(), ab, -1); }
            // clear-row ✕
            std::snprintf(id, sizeof(id), "mclr%d", r);
            const ImVec2 x0 = P(972, y + 8), x1 = P(996, y + 32);
            ImGui::SetCursorScreenPos(x0);
            ImGui::InvisibleButton(id, ImVec2(x1.x - x0.x, x1.y - x0.y));
            if (ImGui::IsItemClicked())
            { setChoice(kParamModSrc0 + r, 0); setChoice(kParamModDst0 + r, 0);
              pushParam(kParamModAmt0 + r, 0.0f); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Clear this slot");
            dl->AddRect(x0, x1, IM_COL32(120, 120, 124, 255), 3.0f * s, 0, 1.0f * s);
            drawX(984, y + 20, 4.0f, live.textPanel);
        }

        // Esc closes the overlay, matching the browser and the save modal (its
        // absence here was the odd one out: the only modal you could not dismiss
        // from the keyboard).
        //
        // One step at a time, as everywhere else: Esc with a Source/Dest dropdown
        // open belongs to the DROPDOWN. ImGui closes that popup itself, but it
        // does so in NewFrame (NavUpdateCancelRequest) and does not consume the
        // key, so by the time this line runs the popup is already gone and
        // IsPopupOpen() would report a clear field — one keystroke would close
        // both. The test therefore uses the popup state as it stood at the top of
        // this frame, the same before-the-fact trick browseSearchHadText uses.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !popupWasOpen
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
            showMod = false;

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

        modPopupWasOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
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
        // The name as it stood BEFORE this frame's InputText ran — the browser's
        // Esc handler needs the same thing, for the same reason (see there).
        const bool nameHadText = saveNameHadText;
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Close without saving");
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
        // Name VALIDITY and COLLISION are cached, not recomputed per frame: the
        // pair costs a sanitize() allocation plus presetDir() (four more
        // fs::path allocations) plus a stat() of the preset folder, and the answer
        // can only change when the text does. Same browseDirty idiom as the
        // browser's filter index — recompute on an actual edit, and once on open.
        if (ImGui::IsItemEdited()) { saveNameDirty = true; saveError[0] = '\0'; }
        if (saveNameDirty)
        {
            saveNameValid  = !scpreset::sanitize(saveNameBuf).empty();
            saveNameExists = saveNameValid && presetStore.exists(saveNameBuf);
            saveNameDirty  = false;
        }
        saveNameHadText = (saveNameBuf[0] != '\0');   // for next frame's Esc test
        ImGui::PopStyleColor(2);

        const bool valid  = saveNameValid;
        const bool exists = saveNameExists;

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
            // Hint slot, one line, three tenants in priority order: a failed write
            // outranks both advisories — it is the only one reporting something
            // that already went wrong, and it stays until the name is edited.
            if (saveError[0] != '\0')
                text(x0 + 20, y0 + 108, 10.0f, IM_COL32(240, 96, 80, 255), saveError, -1, true);
            else if (!valid)
                text(x0 + 20, y0 + 108, 10.0f, whiteDimCol(), "Enter a name to save.", -1);
            else if (exists)
                // ASCII punctuation only in on-screen strings: the crisp atlas is
                // baked from the Latin-1 range, so an em dash renders as a "?"
                // box. (The \xC2\xB7 middle dot used elsewhere IS in the range.)
                text(x0 + 20, y0 + 108, 10.0f, whiteDimCol(),
                     "Name in use \xC2\xB7 saving will ask to overwrite.", -1);

            const bool doSave = modalButton("SAVE", x0 + 20, by, 120, 30, true) || (enter && valid);
            if (doSave && valid) { if (exists) overwriteConfirm = true; else commitSave(); }
            if (modalButton("CANCEL", x0 + 152, by, 120, 30, false)) showSaveModal = false;

            // DELETE affordance only when a user preset is currently recalled.
            if (currentPreset >= kNumFactoryPresets)
                if (modalButton("DELETE", x1 - 20 - 120, by, 120, 30, false)) deleteConfirm = true;
        }

        ImGui::PopFont();

        // Esc backs out ONE step at a time, same ladder as the browser: a confirm
        // first, then the typed name, and only then the modal itself. It used to
        // close the modal outright, so Esc mid-typing — the reflex for "no, not
        // that name" — threw away the whole dialog along with the name.
        //
        // The name test uses the value captured at the TOP of this frame, because
        // ImGui's InputText handles Esc inside its own call: it reverts the buffer
        // to its focus-time value and drops the active id, so a field that held
        // text a moment ago can already read as empty here. Testing the live
        // buffer alone would close the modal on the very keystroke meant to clear
        // it. (Same reasoning, verbatim, as browseSearchHadText.)
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (overwriteConfirm)      overwriteConfirm = false;
            else if (deleteConfirm)    deleteConfirm = false;
            else if (nameHadText || saveNameBuf[0] != '\0')
            {
                saveNameBuf[0] = '\0';
                saveNameHadText = false;
                saveNameDirty = true;
                saveError[0] = '\0';
                saveModalJustOpened = true;   // put the caret back in the field
            }
            else showSaveModal = false;
        }

        // Scrim close: click in the dark area outside the panel, no popup open.
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool insidePanel = mp.x >= pMin.x && mp.x <= pMax.x
                              && mp.y >= pMin.y && mp.y <= pMax.y;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !insidePanel
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
            showSaveModal = false;
    }

    //========================================================================
    // Preset browser modal
    //========================================================================
    // Same replace-panels pattern as the mod matrix and the save modal: this
    // backend does not composite overlapping windows, so while the browser is up
    // the base layers are not submitted at all and it owns the whole surface over
    // a dark scrim, in its own layer window (its own draw list, own vertex budget).
    //
    // Geometry, design space. Panel (48,58)-(1192,740); a 20 px inner margin puts
    // the content band at x 68..1172 (1104 wide):
    //   title + close ✕   y  66..90
    //   search row        y 104..130   FIND label + input; bank chips at the right
    //   mode chip row     y 140..166   ALL + the six mode names
    //   grid              y 178..678   12 rows of h38 on a 42 pitch (last row
    //                                  640..678); 4 columns of w270 on a 278 pitch
    //   footer            y 690..720   status + hint text, APPLY / CLOSE buttons
    // 4*270 + 3*8 = 1104 exactly, and 12*42 - 4 = 500 = 678-178, so the grid fills
    // its band with no slack either way.
    static constexpr float kBrX0 = 48.0f, kBrY0 = 58.0f, kBrX1 = 1192.0f, kBrY1 = 740.0f;
    static constexpr float kBrCX0 = 68.0f, kBrCX1 = 1172.0f;      // content band
    static constexpr float kBrGridY0 = 178.0f;
    static constexpr float kBrCellW = 270.0f, kBrCellH = 38.0f;
    static constexpr float kBrColPitch = 278.0f, kBrRowPitch = 42.0f;
    static constexpr int   kBrCols = 4, kBrRows = 12;             // 48 cells on screen
    static constexpr float kBrGridY1 = kBrGridY0 + (kBrRows - 1) * kBrRowPitch + kBrCellH;
    // Filtered index capacity: every factory preset plus the store's own hard cap.
    static constexpr int   kBrowseMax = kNumFactoryPresets + scpreset::kMaxUserPresets;

    void openBrowse()
    {
        showBrowse = true;
        browseJustOpened = true;
        browseSearch[0] = '\0';
        browseSearchHadText = false;
        browseModeFilter = -1;   // all modes
        browseSrcFilter  = 0;    // all banks
        browseSel = -1;          // rebuild parks the cursor on the loaded preset
        browseRow0 = 0;
        browseWheelAcc = 0.0f;
        browseDirty = true;
        // NOT presetStore.refresh(): currentPreset addresses the user bank BY INDEX,
        // and a rescan can renumber it (a file added or removed behind the plugin's
        // back), which would silently repoint the highlight — and DELETE in the save
        // modal — at a different patch. The combo has the same contract; the store is
        // rescanned only where the UI itself changed it (save / delete).
    }

    // Mode a factory preset selects, read out of its own override table. Every
    // table carries a kParamMode row; the Mode default covers a hypothetical one
    // that does not, which is what loadProgram() would leave in place. Cached per
    // preset in the constructor — this is a table walk, not something to repeat.
    static int factoryPresetMode(int i)
    {
        const FactoryPreset& pr = kFactoryPresets[i];
        int m = (int)std::lround(kParamDefs[kParamMode].def);
        for (int r = 0; r < pr.nRows; ++r)
            if (pr.rows[r].index == kParamMode) m = (int)std::lround(pr.rows[r].value);
        return clampMode(m);
    }

    // Mode of any COMBINED index: factory from the cached table walk, user from the
    // `mode=` line the store parsed at refresh() time.
    int presetMode(int combined) const
    {
        if (combined >= 0 && combined < kNumFactoryPresets) return factoryMode[combined];
        const int u = combined - kNumFactoryPresets;
        if (u >= 0 && u < userCount()) return clampMode(presetStore.list()[u].mode);
        return 0;
    }

    // Case-insensitive substring test. `needleLower` is pre-lowered by the caller
    // so the per-candidate cost is one pass with no allocation and no <string>.
    static bool containsCI(const char* hay, const char* needleLower)
    {
        if (needleLower[0] == '\0') return true;
        if (hay == nullptr) return false;
        for (const char* h = hay; *h != '\0'; ++h)
        {
            const char* a = h;
            const char* b = needleLower;
            while (*a != '\0' && *b != '\0'
                   && (char)std::tolower((unsigned char)*a) == *b) { ++a; ++b; }
            if (*b == '\0') return true;
        }
        return false;
    }

    // Rebuild the filtered index. Runs ONLY when a filter actually changes (or the
    // browser opens) — never per frame; the grid draw walks browseIdx[] straight.
    void rebuildBrowseIndex()
    {
        char needle[sizeof(browseSearch)];
        int n = 0;
        for (; browseSearch[n] != '\0' && n < (int)sizeof(needle) - 1; ++n)
            needle[n] = (char)std::tolower((unsigned char)browseSearch[n]);
        needle[n] = '\0';

        // The preset the cursor is on now, so it can be found again in the new list.
        const int keep = (browseSel >= 0 && browseSel < browseN) ? browseIdx[browseSel]
                                                                 : currentPreset;
        browseN = 0;
        const int total = std::min(comboTotal(), kBrowseMax);
        for (int i = 0; i < total; ++i)
        {
            const bool user = (i >= kNumFactoryPresets);
            if (browseSrcFilter == 1 && user) continue;
            if (browseSrcFilter == 2 && !user) continue;
            if (browseModeFilter >= 0 && presetMode(i) != browseModeFilter) continue;
            if (!containsCI(presetName(i), needle)) continue;
            browseIdx[browseN++] = i;
        }
        // Keep the cursor on the same preset when it survived the new filter,
        // otherwise park it on the first row so Enter always has a sane target.
        browseSel = browseN > 0 ? 0 : -1;
        for (int i = 0; i < browseN; ++i) if (browseIdx[i] == keep) { browseSel = i; break; }
        browseRow0 = 0;
        scrollBrowseToSel();
        browseDirty = false;
    }

    void clampBrowseScroll()
    {
        const int rows = (browseN + kBrCols - 1) / kBrCols;
        const int maxRow0 = rows > kBrRows ? rows - kBrRows : 0;
        if (browseRow0 > maxRow0) browseRow0 = maxRow0;
        if (browseRow0 < 0) browseRow0 = 0;
    }
    void scrollBrowseToSel()
    {
        if (browseSel < 0) { browseRow0 = 0; return; }
        const int row = browseSel / kBrCols;
        if (row < browseRow0) browseRow0 = row;
        if (row >= browseRow0 + kBrRows) browseRow0 = row - kBrRows + 1;
        clampBrowseScroll();
    }
    // The mirror of scrollBrowseToSel: after the WHEEL moves the view, drag the
    // cursor back into it (keeping its column). Enter loads whatever the cursor is
    // on, and a patch load has no undo in this UI — so the cursor must never be
    // parked on a preset that scrolled off screen.
    void clampBrowseSelToView()
    {
        if (browseSel < 0 || browseN <= 0) return;
        const int col   = browseSel % kBrCols;
        const int first = browseRow0 * kBrCols;
        const int last  = std::min(browseN, (browseRow0 + kBrRows) * kBrCols) - 1;
        if (browseSel < first)     browseSel = std::min(first + col, last);
        else if (browseSel > last) browseSel = std::min((browseRow0 + kBrRows - 1) * kBrCols + col, last);
    }

    // Filter chip (mode / bank). Deliberately dark-on-panel in every skin, like the
    // combos, so it reads on Acid's silver panel as well as the five dark ones.
    bool chip(const char* id, float x0, float y0, float x1, float y1, const char* label, bool on)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool clicked = ImGui::IsItemClicked();
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, on ? withA(live.accent, 52) : IM_COL32(34, 34, 38, 255), 4.0f * s);
        dl->AddRect(b0, b1, on ? live.accent
                               : (hov ? IM_COL32(130, 130, 136, 255) : IM_COL32(84, 84, 88, 255)),
                    4.0f * s, 0, on ? 1.6f * s : 1.1f * s);
        text(0.5f * (x0 + x1), 0.5f * (y0 + y1) - 4.5f, 10.0f,
             on ? live.accent : IM_COL32(198, 200, 206, 255), label, 0, on);
        return clicked;
    }

    // Ink that reads on a filled swatch of `c`: the six mode accents run from
    // Cosmos' pale sand to Acid's saturated orange, so the badge label picks its
    // ink per swatch rather than committing to one colour.
    static ImU32 inkOn(ImU32 c)
    {
        const int r = (int)((c >> IM_COL32_R_SHIFT) & 255);
        const int g = (int)((c >> IM_COL32_G_SHIFT) & 255);
        const int b = (int)((c >> IM_COL32_B_SHIFT) & 255);
        return ((r * 30 + g * 59 + b * 11) / 100) > 140 ? IM_COL32(16, 16, 18, 255)
                                                        : IM_COL32(240, 242, 246, 255);
    }

    // One preset cell. It does NOT apply anything itself: it reports through
    // applyIdx / closeNow so the whole grid is submitted before any parameter push
    // runs (a push mid-grid would reorder nothing today, but it keeps the frame's
    // widget submission and the full parameter write strictly separated).
    void drawBrowseCell(int fi, float x0, float y0, int& applyIdx, bool& closeNow)
    {
        const int idx = browseIdx[fi];
        const float x1 = x0 + kBrCellW, y1 = y0 + kBrCellH;
        const bool user = (idx >= kNumFactoryPresets);
        const bool cur  = (idx == currentPreset);
        const bool sel  = (fi == browseSel);
        const int  m    = presetMode(idx);
        const ImU32 mc  = kPalettes[m].accent;

        char id[24]; std::snprintf(id, sizeof id, "brc%d", fi);
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        // One gesture must cost ONE patch load. A double-click is two clicks: the
        // first loads, the second would load the same parameter set again (visible to
        // the host as a second burst of automation writes), so the single-click
        // branch stands down once the click is part of a multi-click, and the
        // double-click branch only loads if click 1 did not already land it.
        if (ImGui::IsItemClicked() && ImGui::GetIO().MouseClickedCount[0] == 1)
        { browseSel = fi; applyIdx = idx; }
        if (hov && ImGui::IsMouseDoubleClicked(0))
        { browseSel = fi; if (idx != currentPreset) applyIdx = idx; closeNow = true; }

        dl->AddRectFilled(b0, b1, cur ? withA(live.accent, 40)
                                      : (hov ? IM_COL32(52, 53, 58, 255)
                                             : IM_COL32(32, 33, 37, 255)), 4.0f * s);
        dl->AddRect(b0, b1, sel ? live.accent : IM_COL32(70, 70, 76, 255),
                    4.0f * s, 0, sel ? 1.8f * s : 1.0f * s);
        // Loaded preset: accent gutter bar + accent name, so "where am I" survives
        // even when the keyboard cursor has moved somewhere else entirely.
        if (cur) dl->AddRectFilled(P(x0 + 2, y0 + 2), P(x0 + 5, y1 - 2), live.accent, 1.5f * s);

        // Badge + tag are 8 px of design space, i.e. 4 device px at the 620x390
        // minimum (s = 0.5) — the same mush kReadoutMinS exists to suppress (§3.1b).
        // Below it the row degrades instead of disappearing: the badge becomes a
        // text-less colour swatch, which still names the mode (the palette IS the
        // mode cue, §4.0), the FACTORY / USER tag drops, and the name takes the
        // freed width. The hover tooltip carries mode and bank verbatim at any size.
        const bool ro = readoutsOn();
        // Name ink band x0+12 .. (x1-122 with badge+tag, x1-36 without): 136 / 222 px,
        // ~24 / ~40 chars at font 11. The longest factory name is 18, but a USER name
        // is whatever fitted in saveNameBuf (128) and panel.text() never clips — so
        // the draw is clipped to the band and an over-long name is CUT, not allowed to
        // overprint the badge and bleed into the next column. Tooltip has it in full.
        const float nameX1 = ro ? x1 - 122.0f : x1 - 36.0f;
        dl->PushClipRect(P(x0 + 12, y0), P(nameX1, y1), true);
        text(x0 + 12, y0 + 14, 11.0f, cur ? live.accent : IM_COL32(226, 229, 234, 255),
             presetName(idx), -1, cur || sel);
        dl->PopClipRect();
        if (ro)
        {
            dl->AddRectFilled(P(x1 - 116, y0 + 11), P(x1 - 58, y0 + 27), mc, 3.0f * s);
            text(x1 - 87, y0 + 15, 8.0f, inkOn(mc), kModeNames[m], 0, true);
            text(x1 - 12, y0 + 15, 8.0f, user ? live.accent : IM_COL32(140, 142, 148, 255),
                 user ? "USER" : "FACTORY", 1, user);
        }
        else
            dl->AddRectFilled(P(x1 - 30, y0 + 11), P(x1 - 12, y0 + 27), mc, 3.0f * s);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s \xC2\xB7 %s \xC2\xB7 %s preset%s", presetName(idx), kModeNames[m],
                              user ? "user" : "factory", cur ? " (loaded)" : "");
    }

    void drawPresetBrowserOverlay()
    {
        if (!showBrowse) return;
        if (browseDirty) rebuildBrowseIndex();
        // Query as it stood before this frame's InputText ran — see the Esc handler
        // at the bottom for why the live buffer cannot answer that question.
        const bool searchHadText = browseSearchHadText;

        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(0, 0, 0, 150));

        const ImVec2 pMin = P(kBrX0 - 3, kBrY0 - 3), pMax = P(kBrX1 + 3, kBrY1 + 3);
        panelBox(kBrX0, kBrY0, kBrX1, kBrY1);
        text(kBrCX0, kBrY0 + 12, 15.0f, live.accent, "PRESET BROWSER", -1, true);

        // close ✕
        const ImVec2 c0 = P(kBrX1 - 36, kBrY0 + 8), c1 = P(kBrX1 - 12, kBrY0 + 32);
        ImGui::SetCursorScreenPos(c0);
        ImGui::InvisibleButton("brclose", ImVec2(c1.x - c0.x, c1.y - c0.y));
        if (ImGui::IsItemClicked()) showBrowse = false;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Close the browser");
        dl->AddRect(c0, c1, IM_COL32(150, 150, 154, 255), 3.0f * s, 0, 1.2f * s);
        drawX(kBrX1 - 24, kBrY0 + 20, 5.0f, live.text);

        int  applyIdx = -1;      // combined index to load at the end of the frame
        bool closeNow = false;

        // ---- search field ----
        ImFont* f = panel.pickFont(13.0f * s);
        ImGui::PushFont(f);
        text(kBrCX0, 110, 10.0f, live.textPanel, "FIND", -1, true);
        ImGui::SetCursorScreenPos(P(kBrCX0 + 42, 104));
        ImGui::SetNextItemWidth(318.0f * s);
        // FramePadding.y floored at 1 px, same as comboBox(): at the 620x390
        // minimum the crisp atlas hands back a face whose FontSize can exceed the
        // 26*s row height, and the resulting negative padding collapses the frame
        // (and clips the caret) instead of merely tightening it.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(6.0f * s, std::max(1.0f, (26.0f * s - f->FontSize) * 0.5f)));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(238, 240, 244, 255));
        if (browseJustOpened) { ImGui::SetKeyboardFocusHere(); browseJustOpened = false; }
        const bool searchEnter = ImGui::InputText("##brsearch", browseSearch, sizeof(browseSearch),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
        // EnterReturnsTrue makes the return value mean "Enter", so edits are picked
        // up separately; the rebuild then happens at the top of the NEXT frame,
        // which keeps the index stable for the grid already being submitted.
        if (ImGui::IsItemEdited()) browseDirty = true;
        browseSearchHadText = (browseSearch[0] != '\0');   // for next frame's Esc test
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::PopFont();
        if (browseSearch[0] == '\0' && !ImGui::IsItemActive())
            text(kBrCX0 + 52, 110, 10.0f, withA(live.textPanel, 110), "search preset names", -1);

        // ---- bank chips (right of the search field, same row) ----
        {
            const char* const labs[3] = { "ALL", "FACTORY", "USER" };
            for (int i = 0; i < 3; ++i)
            {
                char id[16]; std::snprintf(id, sizeof id, "brsrc%d", i);
                const float x0 = 908.0f + i * 90.0f;   // 908..992 / 998..1082 / 1088..1172
                if (chip(id, x0, 104, x0 + 84, 130, labs[i], browseSrcFilter == i))
                { browseSrcFilter = i; browseDirty = true; }
            }
        }

        // ---- mode chips ----
        {
            for (int i = 0; i < 7; ++i)   // 0 = ALL, then the six modes
            {
                char id[16]; std::snprintf(id, sizeof id, "brmode%d", i);
                const float x0 = kBrCX0 + i * 102.0f;  // 7 * 96 + 6 * 6 = 708 -> 68..776
                const int   mf = i - 1;
                if (chip(id, x0, 140, x0 + 96, 166, i == 0 ? "ALL" : kModeNames[mf],
                         browseModeFilter == mf))
                { browseModeFilter = mf; browseDirty = true; }
            }
        }

        // ---- grid ----
        // The wheel scrolls by whole ROWS: a pixel scroll would leave half-cells at
        // the band edges whose ImGui hit boxes still stick out (ImGui only culls
        // items that are FULLY clipped), so every visible cell is a whole cell.
        // The fractional deltas a precision touchpad sends are ACCUMULATED rather
        // than rounded per event — rounding drops every |delta| < 0.5 on the floor,
        // which reads as a dead grid on exactly the hardware that sends the smallest
        // steps. A partial notch is dropped when the pointer leaves the grid, so a
        // half-scroll cannot leak into a later, unrelated gesture.
        if (mouseInRect(kBrCX0, kBrGridY0, kBrCX1, kBrGridY1))
        {
            browseWheelAcc += ImGui::GetIO().MouseWheel;
            const int rows = (int)browseWheelAcc;   // truncate toward zero
            if (rows != 0)
            {
                browseWheelAcc -= (float)rows;
                browseRow0 -= rows;
                clampBrowseScroll();
                clampBrowseSelToView();
            }
        }
        else browseWheelAcc = 0.0f;
        for (int k = 0; k < kBrRows * kBrCols; ++k)
        {
            const int fi = browseRow0 * kBrCols + k;
            if (fi >= browseN) break;                        // list exhausted
            drawBrowseCell(fi, kBrCX0 + (k % kBrCols) * kBrColPitch,
                           kBrGridY0 + (k / kBrCols) * kBrRowPitch, applyIdx, closeNow);
        }
        if (browseN == 0)
            text(0.5f * (kBrCX0 + kBrCX1), kBrGridY0 + 40, 13.0f, whiteDimCol(),
                 "No presets match this filter.", 0, true);

        // scroll indicator (x 1178..1183, inside the panel's 1192 inner wall)
        const int totalRows = (browseN + kBrCols - 1) / kBrCols;
        if (totalRows > kBrRows)
        {
            const float h = kBrGridY1 - kBrGridY0;
            const float th = h * (float)kBrRows / (float)totalRows;
            const float ty = kBrGridY0 + h * (float)browseRow0 / (float)totalRows;
            dl->AddRectFilled(P(kBrCX1 + 6, kBrGridY0), P(kBrCX1 + 11, kBrGridY1),
                              IM_COL32(28, 28, 32, 255), 2.5f * s);
            dl->AddRectFilled(P(kBrCX1 + 6, ty), P(kBrCX1 + 11, ty + th),
                              withA(live.accent, 190), 2.5f * s);
        }

        // ---- footer ----
        char st[64];
        std::snprintf(st, sizeof st, "%d of %d presets", browseN, comboTotal());
        text(kBrCX0, 694, 11.0f, live.textPanel, st, -1, true);
        text(kBrCX0, 710, 9.0f, whiteDimCol(),
             "Click loads \xC2\xB7 double-click loads and closes \xC2\xB7 "
             "arrow keys move \xC2\xB7 Enter loads and closes", -1);
        ImGui::PushFont(panel.pickFont(13.0f * s));
        if (modalButton("APPLY", 920, 690, 120, 30, true) && browseSel >= 0 && browseSel < browseN)
        { applyIdx = browseIdx[browseSel]; closeNow = true; }
        if (modalButton("CLOSE", 1052, 690, 120, 30, false)) showBrowse = false;
        ImGui::PopFont();

        // ---- keyboard navigation ----
        // Up/Down are read unconditionally: a single-line InputText only takes
        // ownership of Left/Right/Enter/Home/End (imgui_widgets.cpp
        // "always_owned_keys"; Up/Down are claimed for multiline only), so the grid
        // stays navigable while the search box holds focus — type, then arrow down
        // into the results. Left/Right yield to an active field so they still walk
        // the search text.
        //
        // A search EDIT only sets browseDirty — the rebuild lands at the top of the
        // next frame, so the index stays stable for the grid already submitted this
        // one. Enter, though, acts NOW: typing a query and hitting Enter in a single
        // gesture would otherwise load out of the PRE-EDIT list, i.e. the wrong
        // preset entirely. Rebuild first when both land on the same frame.
        if (searchEnter && browseDirty) rebuildBrowseIndex();
        if (browseN > 0)
        {
            const bool editing = ImGui::IsAnyItemActive();
            int move = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) move += kBrCols;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))   move -= kBrCols;
            if (!editing && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) move += 1;
            if (!editing && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  move -= 1;
            if (move != 0)
            {
                const int t = (browseSel < 0 ? 0 : browseSel) + move;
                browseSel = t < 0 ? 0 : (t >= browseN ? browseN - 1 : t);
                scrollBrowseToSel();
            }
            // Enter: from the search box it arrives as the InputText return value;
            // with nothing active it is read as a plain key. The !searchEnter guard
            // stops the two paths firing on the same frame (InputText clears the
            // active id as it returns, so IsAnyItemActive() is already false).
            const bool enterKey = searchEnter
                || (!searchEnter && !editing && (ImGui::IsKeyPressed(ImGuiKey_Enter)
                                                 || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)));
            if (enterKey && browseSel >= 0) { applyIdx = browseIdx[browseSel]; closeNow = true; }
        }

        // Esc backs out one step at a time: it clears a typed query first and only
        // closes the browser once the list is unfiltered again — losing the query
        // AND the browser to one keystroke is the wrong ratio.
        //
        // The test is the query as it stood at the START of this frame, not as it
        // stands now: ImGui's InputText handles Esc inside its own call, reverting
        // the buffer to its focus-time value and clearing the active id, so by the
        // time this runs a field that held "bass" a moment ago reads as empty and
        // inactive. Testing the live buffer would therefore close the browser on the
        // very keystroke that was meant to clear the search.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (searchHadText || browseSearch[0] != '\0')
            { browseSearch[0] = '\0'; browseDirty = true; }
            else showBrowse = false;
        }

        // Scrim close: a click in the dark area outside the panel, no popup open —
        // same manual hit test as the other two modals, done AFTER the widgets so
        // it can never swallow a click meant for one of them.
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool insidePanel = mp.x >= pMin.x && mp.x <= pMax.x
                              && mp.y >= pMin.y && mp.y <= pMax.y;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !insidePanel
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
            showBrowse = false;

        // Deferred load: applying pushes every core parameter, so it runs once, here,
        // after every widget in the frame has been submitted.
        if (applyIdx >= 0) applyCombined(applyIdx);
        if (closeNow) showBrowse = false;
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
        // A restrained oscilloscope grid makes the large display intentional even
        // before audio arrives and gives the live trace a useful reference.
        for (int i = 1; i < 4; ++i)
        {
            const float x = rx0 + (rx1 - rx0) * (float)i / 4.0f;
            const float y = ry0 + (ry1 - ry0) * (float)i / 4.0f;
            dl->AddLine(P(x, ry0), P(x, ry1), IM_COL32(255, 255, 255, 12), 1.0f * s);
            dl->AddLine(P(rx0, y), P(rx1, y),
                        IM_COL32(255, 255, 255, i == 2 ? 30 : 12), 1.0f * s);
        }

        int count = 0;
        // Copy the ring (oldest->newest) into our preallocated buffer via the
        // data-race-free bridge API (may tear, fine for a scope); no raw ring
        // pointer / writePos math.
        if (msynth::MultiSynthDSP* d = dspAccess())
            count = d->copyScope(scope, msynth::MultiSynthDSP::kScopeSize);
        bool hasUsableSignal = false;
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
                float peak = 0.0f;
                for (int i = 0; i < nPts; ++i)
                {
                    const float x = rx0 + (rx1 - rx0) * (float)i / (float)(nPts - 1);
                    float v = scope[start + i]; if (v > 1) v = 1; if (v < -1) v = -1;
                    peak = std::max(peak, std::fabs(v));
                    pts[i] = ImVec2(P(x, 0).x, midYpx - v * halfH * s);
                }
                if (peak > 1e-4f)
                {
                    dl->AddPolyline(pts, nPts, live.accent, 0, 1.8f * s);
                    hasUsableSignal = true;
                }
            }
        }
        if (!hasUsableSignal)
            text(1114, midY - 5.0f, 9.0f, withA(live.text, 105),
                 "WAITING FOR SIGNAL", 0, true);
        dl->PopClipRect();
        dl->AddRect(P(rx0, ry0), P(rx1, ry1), IM_COL32(0, 0, 0, 180), 4.0f * s, 0, 1.2f * s);
    }

    void drawOutputVU()
    {
        panelBox(1004, 304, 1224, 542);
        sectionTitle(1012, 308, "OUTPUT");

        float lL = values[kParamOutLevelL], lR = values[kParamOutLevelR];
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        // BOTH accessors are tested: the bridge's contract is per-SYMBOL (each is
        // its own weak symbol, resolved or not on its own), so "L resolved" says
        // nothing about R. Taking L as proof of both is a null call away from a
        // crash the moment the two ever ship apart, and the pair is meaningless
        // half-filled anyway — fall back to the output params together.
        if (multiSynthGetOutLevelL != nullptr && multiSynthGetOutLevelR != nullptr)
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

        klabel(1130, 336, "VOLUME"); knob("mvol", kParamMasterVol, 1130, 372, 24, "%+.1f", " dB", true, false, readoutsOn(), 1.0f, 0.0f, true, false, true);
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
    // Sequencer (mode-aware: arpeggiator or dedicated Acid pattern renderer)
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
        // Panel top 548 (was 524): the lower body row above grew +24, so the
        // sequencer gives that height back at the top. The transport header (y
        // 552..606) carries the labels, controls and read-outs; the step lanes
        // occupy the remaining height down to the 692 floor.
        panelBox(16, 548, 700, 692);
        if (curMode == 5)
        {
            sectionTitle(24, 552, "PATTERN SEQUENCER");
            drawAcidSequencerBody();
            return;
        }
        const char* const seqTitle = "SEQUENCER / ARP";
        sectionTitle(24, 552, seqTitle);

        // --- transport header: ONE rhythm, no dead air ------------------------
        // The row is six groups — ARP | MODE | RATE | OCT/GATE/SWING | LATCH |
        // VEL — laid out right-to-left off the 692 rule the step lane below also
        // ends on, with a single gap `g` repeated between EVERY pair including
        // title -> ARP. The old fixed x=160 start left the five arpeggiator
        // modes opening on a 45 px hole while their own inter-group gaps were
        // 6..11 px. Solving the gap from the title width keeps one even rhythm
        // across the row.
        //
        // Knobs are r13 and TICKLESS, matching the FX strip idiom next door:
        // reach is r+3+1.2 = +/-17.2 (value arc + half its stroke) instead of the
        // tick ring's +/-20.5, which makes all three read as one family (as r14
        // ringed knobs they differed enough in apparent size/weight to look like
        // three different widgets).
        //
        // MEASURED ink extents, not the 0.675*size convention the older comments in
        // this file use — that convention is wrong. Rendered at s=1 through this
        // atlas, a label drawn with its top at y552 puts ink on rows 555..560, i.e.
        // the ink box runs 9.0 px below the draw origin for every size in this row
        // (9.5 / 10 / 11 all snap to neighbouring atlas faces). So:
        //   label top 552          -> ink bottom 561
        //   knob centre hy = 580   -> arc top 562.8   =>  1.8 px clearance
        // hy was 578, which put the arc top at 560.8 and had the label ink
        // overlapping it by 0.2 px. Downstream of hy = 580 (all verified by pixel
        // scan): the read-out (top hy+r+8 = 601) has ink bottom 610, clearing the
        // step lane at y612 by 2.0 px.
        const int  velMode  = (int)std::lround(values[kParamArpVelMode]);
        const bool velFixed = velMode == 1;
        const bool velAccent = velMode == 2;
        const float kr = 13.0f, khw = kr + 4.5f, kdx = 44.0f;  // radius / half-reach / pitch
        const float wArp = 52.0f, wMode = 84.0f, wRate = 64.0f, wLatch = 52.0f;
        const float wKnobs = 2.0f * kdx + 2.0f * khw;          // 123
        // The VEL group is 100 wide in BOTH velocity modes so that switching to
        // Fixed does not reflow the whole row: Fixed spends the width on a 58 px
        // combo ("Fixed" is short) plus the value knob, the other modes give all
        // 100 to the combo (which "As Played" wants — it was clipping at 86).
        const float wVel = 100.0f;
        const float xR = 692.0f;
        const float titleEnd = 24.0f + textW(11.0f, seqTitle);
        const float wSum = wArp + wMode + wRate + wKnobs + wLatch + wVel;   // 475
        float g = (xR - titleEnd - wSum) / 6.0f;
        g = g < 12.0f ? 12.0f : (g > 22.0f ? 22.0f : g);        // font-metric guard
        auto hlabel = [&](float x, const char* t, int align)
        { text(x, 552, 10.0f, live.textPanel, t, align, true); };

        const float hy = 580.0f;
        // Persistent read-outs for OCT/GATE/SWING (spec §3.1b step 7): drawn top
        // hy + r + 8 = 601, ink bottom 610, clearing the lane top at 612.
        const bool ro = readoutsOn();

        // Walking left-to-right from the title only lands on the 692 rule while `g`
        // is the SOLVED value; once the clamp engages (a title wide enough to drive g
        // below 12) the row would march straight through the right wall and into the
        // FX strip, so pin the start to whatever still ends at xR and let the
        // title gap absorb the difference instead.
        float x = titleEnd + g;
        const float xMax = xR - wSum - 5.0f * g;
        if (x > xMax) x = xMax;
        // LED buttons are h24 centred on 575, the same centre line as the combos
        // (they used to sit 5 px high, which read as a misaligned row).
        ledButton("arpon", kParamArpOn, x, 563, x + wArp, 587, "ARP");
        x += wArp + g;
        hlabel(x + 2, "MODE", -1);
        comboBox("arpmode", kParamArpMode, x, 564, x + wMode, 586, kArpMode, 7);
        x += wMode + g;
        hlabel(x + 2, "RATE", -1);
        comboBox("arprate", kParamArpRate, x, 564, x + wRate, 586, kDivName, 14);
        x += wRate + g;
        const float kx0 = x + khw;
        hlabel(kx0, "OCT", 0);
        knob("arpoct", kParamArpOctave, kx0, hy, kr, "%.0f", "", false, true, ro, 1.0f, 0.0f, false);
        hlabel(kx0 + kdx, "GATE", 0);
        knob("arpgate", kParamArpGate, kx0 + kdx, hy, kr, "%.0f", " %", false, false, ro, 100.0f, 0.0f, false);
        hlabel(kx0 + 2.0f * kdx, "SWING", 0);
        knob("arpswing", kParamArpSwing, kx0 + 2.0f * kdx, hy, kr, "%.0f", " %", false, false, ro, 100.0f, 0.0f, false);
        x += wKnobs + g;
        ledButton("arplatch", kParamArpLatch, x, 563, x + wLatch, 587, "LATCH");
        x += wLatch + g;
        hlabel(x + 2, "VEL", -1);
        // Fixed mode: combo 58 wide + the value knob on the right end of the
        // group (centre xR-13 = 679, reach 696.5, inside the 700 panel wall).
        comboBox("arpvel", kParamArpVelMode, x, 564,
                 velFixed ? x + 58.0f : x + wVel, 586, kArpVel, 3);
        if (velFixed)
        {
            const float fx = xR - kr;
            hlabel(fx, "FIX", 0);
            knob("arpfvel", kParamArpFixedVel, fx, hy, kr, "%.0f", "", false, true, ro, 1.0f, 0.0f, false);
        }
        // ACC — the accent SHAPE, only meaningful while VEL is "Accent" (it is the
        // sole consumer: Arpeggiator::getVelocity reads accentPattern in that
        // branch and nowhere else). Shown/hidden, not dimmed, matching the FIX
        // knob one line up, which is the established idiom for "this control has
        // no meaning in the other velocity modes".
        //
        // It is NOT a seventh group in the solved rhythm, and that is arithmetic,
        // not taste. A seventh group of width W re-solves g to
        // (692 - titleEnd - 475 - W)/7, which hits the g >= 12 floor at W = 18 px.
        // Splitting the existing 100 px VEL group in two does not work either:
        // a combo spends 6 px of frame padding plus
        // ~24 px of arrow before any text, so 100 px cannot carry two of them.
        // Widening the VEL group to fit both would reflow all six groups on a
        // velocity-mode change — precisely what the fixed 100 px width exists to
        // prevent (see the note above it).
        //
        // So it takes the free band directly UNDER the VEL combo instead, right-
        // aligned on the same 692 rule and the same 100 px wide, which keeps the
        // column and costs the row nothing. Vertical fit (ink extents measured the
        // same way as the rest of this header): VEL combo bottom 586 -> 2 px -> ACC
        // 588..606 -> 6 px -> step lane at 612. The OCT/GATE/SWING read-
        // outs share the 601..610 band but end at x~501, well left of 592; the
        // Fixed-VEL knob's read-out does reach into this x-range, but Fixed and
        // Accent are mutually exclusive so the two can never draw together.
        //
        if (velAccent)
        {
            // x still holds the VEL group's left edge (last group in the walk);
            // deriving from it keeps ACC under VEL even if the walk's start
            // clamp ever moves the row off the 692 rule.
            const float ax = x;
            text(ax - 4.0f, 590.0f, 8.5f, live.textPanel, "ACC", 1, true);
            comboBox("arpacc", kParamArpAccentPattern, ax, 588, xR, 606, kArpAccent, 4);
        }

        const int step = liveStep();
        // Single mute row, y 612..680 (h68), starting 6.6 px below the knob
        // read-out ink. The 16 cells are split into four bar-groups with an
        // 8 px gap between groups, so the pattern reads as 4 bars of 4.
        drawStepRow(24, 612, 692, 68, kParamArpStep0, step, true, 8.0f);
    }

    void drawAcidSequencerBody()
    {
        // AcidSequencer consumes RUN, RATE, GATE, SWING and LATCH. The generic
        // arpeggiator's MODE, OCT and VEL controls are intentionally absent: the
        // dedicated Acid path never reads them.
        auto hlabel = [&](float x, const char* t, int align)
        { text(x, 552, 10.0f, live.textPanel, t, align, true); };

        ledButton("acid_run", kParamArpOn, 158, 563, 216, 587, "RUN");

        hlabel(230, "RATE", -1);
        comboBox("acid_rate", kParamArpRate, 228, 564, 294, 586,
                 kDivName, 14, true);

        constexpr float kr = 13.0f;
        hlabel(326, "GATE", 0);
        knob("acid_gate", kParamArpGate, 326, 580, kr, "%.0f", " %",
             false, false, false, 100.0f, 0.0f, false);
        hlabel(374, "SWING", 0);
        knob("acid_swing", kParamArpSwing, 374, 580, kr, "%.0f", " %",
             false, false, false, 100.0f, 0.0f, false);

        ledButton("acid_latch", kParamArpLatch, 406, 563, 470, 587, "LATCH");

        const int step = liveStep();
        dl->AddRectFilled(P(484, 563), P(692, 587),
                          IM_COL32(38, 39, 43, 255), 4.0f * s);
        dl->AddRect(P(484, 563), P(692, 587), withA(live.accent, 120),
                    4.0f * s, 0, 1.0f * s);
        if (step >= 0 && step < 16)
        {
            const bool accent = values[kParamSeqAccent0 + step] > 0.5f;
            const bool slide  = values[kParamSeqSlide0 + step] > 0.5f;
            char status[64];
            std::snprintf(status, sizeof status, "STEP %02d  \xC2\xB7  %s%s",
                          step + 1,
                          accent ? "ACC" : "NORMAL",
                          slide ? " + SLIDE" : "");
            text(588, 570, 9.0f, IM_COL32(231, 233, 236, 255),
                 status, 0, true);
        }
        else
        {
            text(588, 570, 9.0f, whiteDimCol(),
                 values[kParamArpOn] > 0.5f ? "WAITING FOR TRANSPORT" : "PATTERN READY",
                 0, true);
        }

        // Four truthful lanes with a left label gutter. ACC and SLIDE remain real
        // click targets rather than compressed status marks.
        const float gx = 62.0f, cw = (692.0f - gx) / 16.0f;
        drawLaneLabel(58, 600, 616, "GATE");
        drawStepRow(gx, 600, 692, 16, kParamArpStep0, step, false, 0.0f);
        drawLaneLabel(58, 620, 654, "PITCH");
        drawPitchLane(gx, 620, cw, 34, step);
        drawLaneLabel(58, 658, 673, "ACC");
        drawLaneLabel(58, 677, 692, "SLIDE");
        drawAccentSlideLanes(gx, 658, cw, step);
    }

    // Right-aligned lane label sitting in the acid sequencer's left gutter.
    void drawLaneLabel(float xRight, float y0, float y1, const char* t)
    { text(xRight, 0.5f * (y0 + y1) - 5.0f, 8.5f, live.textPanel, t, 1, true); }
    // Shared face for every clickable step cell: flat fill plus the panelBox bevel
    // idiom (light top edge, dark bottom edge) so a cell reads as a physical pad
    // rather than a painted rectangle, plus a hover outline — the row had no hover
    // affordance at all, so nothing told you the slabs were targets.
    void cellFace(float x0, float y0, float x1, float y1, ImU32 fill, bool hov, float round,
                  bool inset)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        const ImU32 lit = lerpC(fill, IM_COL32(255, 255, 255, 255), 0.26f);
        const ImU32 shd = lerpC(fill, IM_COL32(0, 0, 0, 255), 0.34f);
        dl->AddRectFilled(b0, b1, fill, round * s);
        // `inset` flips the bevel so an OFF cell reads as a pad pressed INTO the
        // lane and an ON cell as one standing proud of it — the state is then
        // legible from the relief as well as from the fill colour.
        dl->AddLine(P(x0 + 2, y0 + 1), P(x1 - 2, y0 + 1), inset ? shd : lit, 1.0f * s);
        dl->AddLine(P(x0 + 2, y1 - 1), P(x1 - 2, y1 - 1), inset ? lit : shd, 1.0f * s);
        if (hov) dl->AddRect(b0, b1, IM_COL32(255, 255, 255, 115), round * s, 0, 1.4f * s);
    }
    // 16 on/off cells from x0 to x1. `groupGap` inserts real daylight between the
    // four bar-groups (non-acid mute row); pass 0 to keep a flush lane whose cell
    // pitch stays aligned with the other acid lanes, which then get the hairline
    // group divider instead. `tall` adds the 1..16 step numbers.
    void drawStepRow(float x0, float y0, float x1, float h, uint32_t base, int step,
                     bool tall, float groupGap)
    {
        const float pitch = (x1 - x0 - 3.0f * groupGap) / 16.0f;
        const float pad = 2.0f;
        const float cy1 = y0 + h;
        for (int i = 0; i < 16; ++i)
        {
            const float gx  = x0 + i * pitch + (float)(i / 4) * groupGap;
            const float cx0 = gx + pad, cx1 = gx + pitch - pad;
            const bool on = values[base + i] > 0.5f;
            const bool down = (i % 4) == 0;   // bar downbeat
            char id[16]; std::snprintf(id, sizeof(id), "step%u_%d", (unsigned)base, i);
            const ImVec2 b0 = P(cx0, y0), b1 = P(cx1, cy1);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
            if (ImGui::IsItemClicked()) setChoice(base + i, on ? 0 : 1);
            const bool hov = ImGui::IsItemHovered();
            if (tips[base + i] && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", tips[base + i]);
            // Downbeats carry a touch more ink in BOTH states, so the beat grid is
            // legible whether the pattern is mostly lit or mostly muted.
            const ImU32 fill = on ? withA(live.accent, down ? 235 : 185)
                                  : (down ? IM_COL32(48, 50, 56, 255) : IM_COL32(32, 34, 38, 255));
            cellFace(cx0, y0, cx1, cy1, fill, hov, 3.0f, !on);
            if (groupGap <= 0.0f && down)
                dl->AddLine(P(cx0 - 2, y0), P(cx0 - 2, cy1), IM_COL32(255, 255, 255, 40), 1.2f * s);
            if (i == step)
            { dl->AddRect(b0, b1, live.ledOn, 3.0f * s, 0, 2.0f * s);
              dl->AddRectFilled(P(cx0, y0), P(cx1, y0 + 3), live.ledOn, 1.0f * s); }
            // Number ink is chosen against the fill (inkOn) rather than fixed:
            // live.text on the pale Cosmos accent was near-invisible. Muted cells
            // stay deliberately dim so on/off reads from the numbers too.
            if (tall) { char n[4]; std::snprintf(n, sizeof(n), "%d", i + 1);
                        text(0.5f * (cx0 + cx1), y0 + h * 0.5f - 6, 10.0f,
                             on ? inkOn(live.accent) : IM_COL32(124, 128, 136, 255), n, 0, on); }
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
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
        // Rows: ACC y0..y0+15, SLIDE y0+19..y0+34 (h15 each — real click targets).
        // The 4-step group dividers match the gate row's idiom so the off-state
        // cells read as a lane, not as loose slits on the panel.
        for (int i = 0; i < 16; ++i)
        {
            const float cx0 = x0 + i * cw + 1.5f, cx1 = x0 + (i + 1) * cw - 1.5f;
            if ((i % 4) == 0)
                dl->AddLine(P(cx0 - 1.5f, y0), P(cx0 - 1.5f, y0 + 34), IM_COL32(255, 255, 255, 40), 1.2f * s);
            drawMiniCell(cx0, y0,      cx1, y0 + 15, kParamSeqAccent0 + i, i, step, IM_COL32(240, 200, 40, 255));
            drawMiniCell(cx0, y0 + 19, cx1, y0 + 34, kParamSeqSlide0 + i, i, step, IM_COL32(60, 200, 230, 255));
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
        const bool hov = ImGui::IsItemHovered();
        if (tips[p] && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tips[p]);
        cellFace(x0, y0, x1, y1, on ? onCol : ((i % 4) == 0 ? IM_COL32(48, 50, 56, 255)
                                                           : IM_COL32(32, 34, 38, 255)), hov, 2.0f, !on);
        if (i == step) dl->AddRect(b0, b1, live.ledOn, 2.0f * s, 0, 1.4f * s);
    }

    //========================================================================
    // FX strip
    //========================================================================
    void drawFXStrip()
    {
        // Which of the two dense FX panels reads out through its labels. Latched, and
        // only re-evaluated while nothing is being dragged: a knob drag can sweep the
        // pointer anywhere on screen, so a raw hit test made the DELAY labels flip to
        // values while you were dragging a REVERB knob (and vice versa). Latching also
        // keeps the panel you GRABBED in reading out for the whole gesture, which is
        // the panel whose numbers you want.
        if (!ImGui::IsAnyItemActive())
            fxReadoutPanel = mouseInRect(968, 552, 1094, 688) ? 1
                           : mouseInRect(1098, 552, 1224, 688) ? 2 : 0;

        // Drive
        panelBox(708, 552, 834, 688);
        sectionTitle(714, 556, "DRIVE");
        const bool acid = (curMode == 5);
        ledButton("drvon", kParamDriveOn, 786, 556, 828, 572,
                  acid ? "FX" : "ON");
        if (acid)
            text(716, 574, 7.5f, live.textPanel, "FX TYPE", -1, true);
        comboBox("drvtype", kParamDriveType, 716, 580, 828, 600, kDriveType, 3);
        // Tickless like the rest of the FX strip, centered in the body below the combo.
        // DRIVE and CHORUS have a free band under their knob row, so their read-outs
        // are permanent (ink 668..674.4 / 658..664.4 vs the 685 inner floor).
        const bool ro = readoutsOn();
        klabel(748, 612, acid ? "FILTER+FX" : "AMT");
        knob("drvamt", kParamDriveAmt, 748, 646, 14, "%.0f", " %",
             false, false, ro, 100.0f, 0.0f, false);
        klabel(796, 612, acid ? "FX MIX" : "MIX");
        knob("drvmix", kParamDriveMix, 796, 646, 14, "%.0f", " %",
             false, false, ro, 100.0f, 0.0f, false);

        // Chorus — three tickless r14 knobs in ONE balanced row (the old layout
        // orphaned a smaller MIX bottom-right over dead space). Tickless reach is
        // R+3.2 = ±17.2, so x {863,901,939} (38 apart) clears the inner walls
        // (845.8 >= 844, 956.2 <= 958) and each neighbor by 3.6 px.
        panelBox(838, 552, 964, 688);
        sectionTitle(844, 556, "CHORUS");
        ledButton("choon", kParamChorusOn, 916, 556, 958, 572, "ON");
        klabel(863, 598, "RATE");  knob("chorate", kParamChorusRate, 863, 636, 14, "%.2f", " Hz", false, false, ro, 1.0f, 0.0f, false);
        klabel(901, 598, "DEPTH"); knob("chodep", kParamChorusDepth, 901, 636, 14, "%.0f", " %", false, false, ro, 100.0f, 0.0f, false);
        klabel(939, 598, "MIX");   knob("chomix", kParamChorusMix, 939, 636, 14, "%.0f", " %", false, false, ro, 100.0f, 0.0f, false);

        // Delay — row structure instead of the old jumble (the DIV combo used to
        // end at y=606 with the FB label starting at 598 UNDER it): SYNC + the
        // context DIV combo share the top row (578..598); the knob row (labels
        // 608, centres 642) holds FB+MIX when synced or TIME+FB+MIX when free;
        // P-P and TAPE ride a bottom row of their own (664..680).
        panelBox(968, 552, 1094, 688);
        sectionTitle(974, 556, "DELAY");
        ledButton("dlyon", kParamDelayOn, 1046, 556, 1088, 572, "ON");
        const bool sync = values[kParamDelaySync] > 0.5f;
        compactToggle("dlysync", kParamDelaySync, 976, 578, 1032, 596, "SYNC");
        // The knob row is boxed in (labels above, P-P/TAPE row at 664 below), so the
        // read-outs live in the LABEL slot and appear while the pointer is anywhere
        // in the DELAY panel — see klabelOrValue.
        const bool dro = (fxReadoutPanel == 1);
        if (sync)
        {
            comboBox("dlydiv", kParamDelayDiv, 1038, 578, 1088, 598, kDivName, 14);
            klabelOrValue(1013, 608, "FB",  dro, kParamDelayFB,  "%.0f", " %", 100.0f);
            knob("dlyfb", kParamDelayFB, 1013, 642, 14, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            klabelOrValue(1049, 608, "MIX", dro, kParamDelayMix, "%.0f", " %", 100.0f);
            knob("dlymix", kParamDelayMix, 1049, 642, 14, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
        }
        else
        {
            klabelOrValue(993, 608, "TIME", dro, kParamDelayTime, "%.0f", " ms", 1.0f, 0.0f, true);
            knob("dlytime", kParamDelayTime, 993, 642, 14, "%.0f", " ms", false, false, false, 1.0f, 0.0f, false, true);
            klabelOrValue(1031, 608, "FB",  dro, kParamDelayFB,  "%.0f", " %", 100.0f);
            knob("dlyfb", kParamDelayFB, 1031, 642, 14, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
            klabelOrValue(1069, 608, "MIX", dro, kParamDelayMix, "%.0f", " %", 100.0f);
            knob("dlymix", kParamDelayMix, 1069, 642, 14, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
        }
        ledButton("dlypp", kParamDelayPP, 995, 664, 1031, 680, "P-P", true);
        ledButton("dlytape", kParamDelayTape, 1037, 664, 1073, 680, "TAPE", true);

        // Reverb — aligned two-row grid: SIZE/DECAY/DAMP r13 (labels 590, centres
        // 620), MIX/P-DLY r12 (labels 642, centres 668); row-2 label ink (..649.6)
        // clears the row-1 knob bottoms (636.2) and the row-2 knob tops (652.8),
        // and the row-2 knob bottoms (683.2) clear the inner floor (685).
        panelBox(1098, 552, 1224, 688);
        sectionTitle(1104, 556, "REVERB");
        ledButton("rvbon", kParamReverbOn, 1176, 556, 1218, 572, "ON");
        // Modular auto-engages a separate spring reverb at a fixed 15% mix. Say so
        // explicitly: the five visible knobs still control the normal reverb.
        if (curMode == 3)
        {
            dl->AddRectFilled(P(1104, 573), P(1218, 585),
                              withA(live.accent, 40), 3.0f * s);
            dl->AddRect(P(1104, 573), P(1218, 585), live.accent,
                        3.0f * s, 0, 1.0f * s);
            text(1161, 575, 7.5f, live.accent,
                 "FIXED SPRING \xC2\xB7 15%", 0, true);
        }
        // Two knob rows back to back: row 1's read-out band IS row 2's label band, and
        // row 2 ends 5 px above the panel floor. Both rows therefore read out through
        // their labels while the pointer is in the REVERB panel.
        const bool rro = (fxReadoutPanel == 2);
        klabelOrValue(1123, 590, "SIZE",  rro, kParamReverbSize,  "%.0f", " %", 100.0f);
        knob("rvbsize", kParamReverbSize, 1123, 620, 13, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
        klabelOrValue(1161, 590, "DECAY", rro, kParamReverbDecay, "%.1f", " s");
        knob("rvbdec", kParamReverbDecay, 1161, 620, 13, "%.1f", " s", false, false, false, 1.0f, 0.0f, false);
        klabelOrValue(1199, 590, "DAMP",  rro, kParamReverbDamp,  "%.0f", " %", 100.0f);
        knob("rvbdamp", kParamReverbDamp, 1199, 620, 13, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
        klabelOrValue(1142, 642, "MIX",   rro, kParamReverbMix,   "%.0f", " %", 100.0f);
        knob("rvbmix", kParamReverbMix, 1142, 668, 12, "%.0f", " %", false, false, false, 100.0f, 0.0f, false);
        klabelOrValue(1180, 642, "P-DLY", rro, kParamReverbPD,    "%.0f", " ms", 1.0f, 0.0f, true);
        knob("rvbpd", kParamReverbPD, 1180, 668, 12, "%.0f", " ms", false, false, false, 1.0f, 0.0f, false, true);
    }

    //========================================================================
    // Performance wheels (pitch bend + mod), keyboard row, left of the keys
    //========================================================================
    // Live DSP instance, or null in a split LV2 UI (see DuskAccessBridge.hpp).
    msynth::MultiSynthDSP* dspAccess()
    {
       #if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (multiSynthGetDSP != nullptr)
            if (void* const inst = getPluginInstancePointer())
                return multiSynthGetDSP(inst);
       #endif
        return nullptr;
    }

    // The wheels drive the SAME engine entry points the shell's MIDI handler calls
    // for 0xE0 (pitch bend) and CC 1 (mod wheel) — MultiSynthDSP::pitchBend() /
    // ::modWheel(), both plain relaxed atomic stores, so writing them from the UI
    // thread is safe and is what makes "Mod Whl" / "P.Bend" usable as mod-matrix
    // sources without a hardware controller. There is no host-facing parameter for
    // either (they are performance state, not patch state), so nothing to automate
    // and nothing to save. They are also READ BACK every frame through
    // getPitchBend()/getModWheel(), so bend and CC 1 arriving from the host or a
    // hardware controller move the drawn wheels; updateWheels() arbitrates that
    // against a local drag. Consequence for a split LV2 UI: no bridge, so the wheels
    // neither drive nor follow the engine, and are drawn inert with a tooltip saying so.
    void drawWheels()
    {
        const bool live_ = dspAccess() != nullptr;
        drawWheel("pbwheel", 54, 80, true, pbValue, "PB",
                  live_ ? "Pitch bend. Drag up or down; springs back to centre on release.\n"
                          "Follows bend sent by the host or a hardware wheel."
                        : "Pitch bend is unavailable in a remote (split) UI.", live_);
        drawWheel("modwheel", 86, 112, false, modValue, "MOD",
                  live_ ? "Mod wheel (CC 1). Latches where you leave it; wheel-scroll to trim.\n"
                          "Follows CC 1 sent by the host or a hardware wheel."
                        : "The mod wheel is unavailable in a remote (split) UI.", live_);
        pushWheels();   // again, so a live drag reaches the engine with no frame of lag
    }

    // Wheel arbitration + spring return + engine push, run once per FRAME from
    // onImGuiDisplay rather than from a wheel's own draw. The mod-matrix and save
    // modals REPLACE the base layers and return early, so drawWheels() does not run
    // while one is open: a bend released as a modal opened used to freeze at whatever
    // it had reached and stay there, detuning the engine until the modal was closed.
    // Frame-driven, the spring keeps running whatever is on screen.
    //
    // OWNERSHIP (spec §8.9). One atomic per wheel is shared with the shell's MIDI
    // handler, so "who is the wheel" has to be resolved every frame:
    //   * the local widget owns its value WHILE HELD, and must ASSERT that every
    //     frame — see below;
    //   * otherwise the ENGINE owns it. An engine value that differs from what we
    //     last pushed can only have come from somewhere else (0xE0 / CC 1, i.e. the
    //     host or a hardware controller), so we adopt it and redraw at that
    //     deflection. This is what makes the drawn wheels follow the keyboard.
    void updateWheels()
    {
        // These flags are one frame stale: drawWheel() sets them AFTER this runs, so
        // they describe the previous frame's grip. Harmless — a grip lasts far longer
        // than a frame, so the only effect is that authority is asserted (and the
        // spring starts) one frame late, i.e. ~16 ms, which is invisible.
        const bool pbHeld  = pbDragging,  modHeld = modDragging;
        pbDragging = modDragging = false;   // re-armed by drawWheel() while held

        if (msynth::MultiSynthDSP* const d = dspAccess())
        {
            // HELD -> RE-ASSERT, unconditionally. Suppressing adoption is only half of
            // owning the value: pushWheels() is change-detected, so holding the wheel
            // STILL leaves *Value == *Sent and nothing gets written. A host 0xE0 / CC 1
            // landing in that window would overwrite the atomic and never be corrected
            // — the screen would show the drag while the engine sounded the host. That
            // is the exact inverse of the divergence adoption fixes, so both directions
            // are resolved here, in one place.
            // NOT HELD -> ADOPT. Runs BEFORE the spring step, so an incoming bend wins
            // the frame it arrives rather than one frame later.
            if (pbHeld) { d->pitchBend(pbValue); pbSent = pbValue; }
            else
            {
                const float e = d->getPitchBend();
                if (e != pbSent) { pbValue = e; pbSent = e; pbLocal = false; }
            }
            if (modHeld) { d->modWheel(modValue); modSent = modValue; }
            else
            {
                const float e = d->getModWheel();
                if (e != modSent) { modValue = e; modSent = e; }
            }
        }

        // --- pitch spring. Gated on pbLocal (this UI authored the current bend), NOT
        // merely on "not held and off centre": a bend the HOST is holding must stay
        // deflected on screen, and springing it would push our decaying value back
        // over the host's and detune the note the player is still bending. The exit
        // conditions are therefore (a) reaching centre, which is where a released
        // local drag belongs, or (b) adoption above clearing pbLocal because an
        // external write landed mid-spring — the spring abandons the gesture to
        // whoever is now driving instead of fighting it to zero.
        if (pbLocal && !pbHeld)
        {
            // Exponential return, time constant 1/45 s = 22 ms; from a full bend it is
            // inaudible (|v| < 0.002) after ~140 ms. Glided rather than snapped so a
            // released bend lands instead of clicking. A release EXACTLY at centre
            // skips the ramp but must still drop ownership, or pbLocal would go on
            // claiming a bend that no longer exists until the next adoption.
            const float dt = ImGui::GetIO().DeltaTime;
            if (pbValue != 0.0f)
                pbValue -= pbValue * (1.0f - std::exp(-(dt > 0.0f ? dt : 0.016f) * 45.0f));
            if (std::fabs(pbValue) < 0.002f) { pbValue = 0.0f; pbLocal = false; }
        }
        pushWheels();
    }

    // Push only on change; the engine holds the last value like a hardware wheel.
    void pushWheels()
    {
        if (pbValue != pbSent)   { if (auto* d = dspAccess()) { d->pitchBend(pbValue); pbSent = pbValue; } }
        if (modValue != modSent) { if (auto* d = dspAccess()) { d->modWheel(modValue); modSent = modValue; } }
    }

    // One vertical wheel. `bipolar` = pitch bend: centre rest, springs back to 0 on
    // release. Otherwise a mod wheel, which latches. The pointer maps ABSOLUTELY
    // into the slot (grab-and-bend) rather than as a relative drag: on a 62 px tall
    // wheel a relative drag would need repeated nudges to reach full bend.
    void drawWheel(const char* id, float x0, float x1, bool bipolar, float& v,
                   const char* label, const char* tip, bool enabled)
    {
        const float top = 700.0f, bot = 762.0f;
        const float mid = 0.5f * (top + bot), halfH = 0.5f * (bot - top);
        const ImVec2 b0 = P(x0, top), b1 = P(x1, bot);

        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool active  = enabled && ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (!active && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tip);

        if (active)
        {
            const float h = b1.y - b0.y;
            float t = h > 1.0f ? (ImGui::GetIO().MousePos.y - b0.y) / h : 0.5f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            v = bipolar ? (1.0f - 2.0f * t) : (1.0f - t);
        }
        else if (enabled && !bipolar && hovered)
        {
            const float wh = ImGui::GetIO().MouseWheel;
            if (wh != 0.0f) { v += wh * 0.05f; v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        }
        // The spring and the engine/widget arbitration both live in updateWheels(),
        // which runs every frame; this only reports that the wheel is currently held,
        // so the spring waits and an incoming host message does not steal the drag.
        // pbLocal additionally marks the bend as THIS UI's, which is what licenses the
        // spring to pull it back and the destructor to centre it.
        if (active) { if (bipolar) { pbDragging = true; pbLocal = true; } else modDragging = true; }

        // bezel + recessed slot
        dl->AddRectFilled(P(x0 - 2, top - 2), P(x1 + 2, bot + 2), metalCol(), 5.0f * s);
        dl->AddRectFilled(b0, b1, IM_COL32(14, 14, 16, 255), 4.0f * s);
        // cylinder shading: lit band across the middle, falling off to both rims
        const ImU32 rim = IM_COL32(44, 45, 48, 255), lit = IM_COL32(122, 124, 130, 255);
        dl->AddRectFilledMultiColor(P(x0 + 1, top + 1), P(x1 - 1, mid), rim, rim, lit, lit);
        dl->AddRectFilledMultiColor(P(x0 + 1, mid), P(x1 - 1, bot - 1), lit, lit, rim, rim);
        // Drum ridges: ridge k sits at u = frac(k/N + phase) and is projected as
        // y = mid - halfH*cos(pi*u), so ridges bunch toward both rims exactly as a
        // real cylinder's do; the value drives the phase, so the wheel visibly rolls.
        const float phase = bipolar ? (0.25f - 0.25f * v) : (0.5f - 0.5f * v);
        const int N = 12;
        for (int k = 0; k < N; ++k)
        {
            float u = (float)k / (float)N + phase;
            u -= std::floor(u);
            const float y = mid - halfH * std::cos(kPi * u) * 0.95f;
            int a = (int)(190.0f * std::sin(kPi * u));
            a = a < 0 ? 0 : a;
            dl->AddLine(P(x0 + 1.5f, y), P(x1 - 1.5f, y), IM_COL32(20, 20, 22, a), 1.2f * s);
        }
        // value indicator + (pitch only) centre detent notches
        const float vy = bipolar ? (mid - halfH * 0.90f * v)
                                 : (bot - 3.0f - (bot - top - 6.0f) * v);
        dl->AddRectFilled(P(x0 + 1, vy - 1.3f), P(x1 - 1, vy + 1.3f),
                          enabled ? live.accent : withA(live.accent, 90), 1.0f * s);
        if (bipolar)
        {
            dl->AddLine(P(x0 + 1.5f, mid), P(x0 + 5.5f, mid), withA(live.text, 150), 1.2f * s);
            dl->AddLine(P(x1 - 5.5f, mid), P(x1 - 1.5f, mid), withA(live.text, 150), 1.2f * s);
        }
        dl->AddRect(b0, b1, IM_COL32(0, 0, 0, 200), 4.0f * s, 0, 1.2f * s);
        text(0.5f * (x0 + x1), bot + 5.0f, 8.5f,
             enabled ? live.text : withA(live.text, 110), label, 0, true);
    }

    //========================================================================
    // Keyboard (playable, → MIDI via sendNote)
    //========================================================================
    void drawKeyboard()
    {
        // OCT- / OCT+
        octButton("octdn", 16, 700, 48, 738, "OCT-", -12, "Shift keyboard octave down");
        octButton("octup", 16, 742, 48, 780, "OCT+", +12, "Shift keyboard octave up");

        // Held-note mask from the engine (spec §8.7): lights keys the player is
        // holding on a hardware MIDI keyboard (or via the host), not just local
        // mouse presses. Null bridge (split LV2 UI) -> mask stays 0, local-only.
        uint64_t heldLo = 0, heldHi = 0;
        if (msynth::MultiSynthDSP* d = dspAccess()) d->getHeldNotes(heldLo, heldHi);
        auto held = [&](int n) {
            return n >= 0 && n < 128
                && (((n < 64 ? heldLo : heldHi) >> (n & 63)) & 1ull) != 0;
        };

        // White keys are submitted FIRST, and a black key overlaps the top 50 px of
        // the two beneath it. Submission order alone does not decide that contest:
        // since ImGui 1.89 the first item to be submitted claims g.HoveredId and the
        // later, overlapping one is rejected — so the black keys were unhittable,
        // every click in the overlap sounding the white key underneath while the
        // tooltip named the black one. SetNextItemAllowOverlap() on each white key is
        // what turns submission order into a front-to-back hit test: ItemHoverable()
        // then refuses the white key unless it also owned HoveredId last frame
        // (imgui.cpp "AllowOverlap mode requires previous frame HoveredId to match"),
        // which lets the black key take it.
        for (int i = 0; i < 21; ++i)
        {
            const KeyRect k = whiteKeyRect(i);
            char id[16]; std::snprintf(id, sizeof(id), "wk%d", i);
            ImGui::SetNextItemAllowOverlap();
            keyHit(id, k);
            const bool lit = (kbNote == k.note) || held(k.note);
            // Opaque accent, not withA(accent, 220): the translucent fill let the dark
            // chassis under the keybed bleed through (and dulled the accent ~14%).
            dl->AddRectFilled(P(k.x0, k.y0), P(k.x1, k.y1),
                              lit ? live.accent : IM_COL32(238, 238, 240, 255), 2.0f * s);
            dl->AddRect(P(k.x0, k.y0), P(k.x1, k.y1), IM_COL32(60, 60, 64, 255), 2.0f * s, 0, 1.0f * s);
        }
        // Black keys. A black key is centered exactly on a white-key boundary (see
        // blackKeyRect), so the two white outline strokes and the gap between them run
        // straight down its centerline: the old withA(accent, 240) lit fill was 6%
        // translucent and showed that as a vertical seam through the pressed key. The
        // fill is opaque now, and stays distinct from a lit white key by being a darker
        // shade of the accent rather than a see-through one. Same reason only the
        // BOTTOM corners round — the key sits flush at kKbTop, so rounded top corners
        // cut away to the white key behind them.
        for (int i = 0; i < 20; ++i)
        {
            KeyRect k;
            if (!blackKeyRect(i, k)) continue;   // no black key after E or B
            char id[16]; std::snprintf(id, sizeof(id), "bk%d", i);
            keyHit(id, k);
            const bool lit = (kbNote == k.note) || held(k.note);
            dl->AddRectFilled(P(k.x0, k.y0), P(k.x1, k.y1),
                              lit ? lerpC(live.accent, IM_COL32(0, 0, 0, 255), 0.35f)
                                  : IM_COL32(18, 18, 20, 255),
                              2.0f * s, ImDrawFlags_RoundCornersBottom);
            dl->AddRect(P(k.x0, k.y0), P(k.x1, k.y1), IM_COL32(0, 0, 0, 255),
                        2.0f * s, ImDrawFlags_RoundCornersBottom, 1.0f * s);
        }

        // GLISSANDO. This cannot go through IsItemHovered(): the moment one key takes
        // the ActiveId, ImGui's ItemHoverable() returns false for every OTHER item
        // ("if (g.ActiveId != 0 && g.ActiveId != id && !g.ActiveIdAllowOverlap) return
        // false"), so a per-key hover test during a drag is unreachable code — the
        // press-then-drag emitted nothing at all. SetNextItemAllowOverlap does not
        // help here either; it only governs HoveredId. So the drag is resolved
        // geometrically instead, black keys first to match the visual stacking.
        if (kbNote >= 0 && ImGui::IsMouseDown(0))
        {
            KeyRect k;
            if (keyAt(ImGui::GetIO().MousePos, k) && k.note != kbNote)
                pressKey(k.note, velFromY(P(0, k.y0).y, P(0, k.y1).y));
        }
        if (ImGui::IsMouseReleased(0) && kbNote >= 0) { sendNote(0, (uint8_t)kbNote, 0); kbNote = -1; }
    }

    // Key geometry, shared by the draw loops, the ImGui hit boxes and the glissando
    // hit test, so those three can never drift apart. The playable span starts at 118
    // (was 52): the pitch/mod wheels took x 52..114 out of the keyboard's left end
    // rather than the design space growing. White keys went 55.81 -> 52.67 px wide.
    struct KeyRect { float x0, y0, x1, y1; int note; };
    static constexpr float kKbX0 = 118.0f, kKbX1 = 1224.0f;
    static constexpr float kKbTop = 700.0f, kKbBot = 780.0f, kKbBlackBot = 750.0f;
    static constexpr int kWhiteSemi[7] = { 0, 2, 4, 5, 7, 9, 11 };

    KeyRect whiteKeyRect(int i) const
    {
        const float w = (kKbX1 - kKbX0) / 21.0f;
        const float x = kKbX0 + i * w;
        return { x + 1.0f, kKbTop, x + w - 1.0f, kKbBot,
                 clampMidi(baseMidi + (i / 7) * 12 + kWhiteSemi[i % 7]) };
    }
    // false for the E / B slots, which have no black key above them.
    bool blackKeyRect(int i, KeyRect& r) const
    {
        const int deg = i % 7;
        if (deg == 2 || deg == 6) return false;
        const float w = (kKbX1 - kKbX0) / 21.0f, bw = 0.62f * w;
        const float boundary = kKbX0 + (i + 1) * w;
        r = { boundary - bw * 0.5f, kKbTop, boundary + bw * 0.5f, kKbBlackBot,
              clampMidi(baseMidi + (i / 7) * 12 + kWhiteSemi[deg] + 1) };
        return true;
    }
    // Front-to-back hit test in screen space: black keys sit on top, so they win the
    // 50 px overlap band, matching both the drawing and the AllowOverlap ImGui path.
    bool keyAt(ImVec2 mp, KeyRect& out) const
    {
        auto inside = [&](const KeyRect& r)
        {
            const ImVec2 a = P(r.x0, r.y0), b = P(r.x1, r.y1);
            return mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= b.y;
        };
        KeyRect r;
        for (int i = 0; i < 20; ++i) if (blackKeyRect(i, r) && inside(r)) { out = r; return true; }
        for (int i = 0; i < 21; ++i) { r = whiteKeyRect(i); if (inside(r)) { out = r; return true; } }
        return false;
    }

    void keyHit(const char* id, const KeyRect& k)
    {
        const ImVec2 b0 = P(k.x0, k.y0), b1 = P(k.x1, k.y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hov = ImGui::IsItemHovered();
        // Only the INITIAL strike goes through ImGui hovering; the drag that follows
        // is resolved geometrically in drawKeyboard (see the glissando note there).
        // Velocity comes from WHERE in the key you click.
        if (hov && ImGui::IsMouseClicked(0)) pressKey(k.note, velFromY(b0.y, b1.y));
        // Note name + the velocity THIS pointer position would play: the only way the
        // strike-position mapping is discoverable. Suppressed while the button is
        // down so the tooltip does not chase the pointer during a glissando.
        if (!ImGui::IsMouseDown(0)
            && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            static const char* const kNoteNames[12] =
                { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            ImGui::SetTooltip("%s%d \xC2\xB7 velocity %d (strike lower = harder)",
                              kNoteNames[k.note % 12], k.note / 12 - 1, (int)velFromY(b0.y, b1.y));
        }
    }

    // Click position within the key -> velocity: top edge soft, bottom edge hard.
    // The range top deliberately crosses the engine's Acid accent threshold (MIDI
    // velocity > 100, MultiSynthDSP::kAcidAccentVel), so a hit near the bottom of a
    // key accents the step the way a hardware bassline keyboard does — the old fixed
    // 100 could never reach it. Black keys are 50 design px tall and white keys 80,
    // so each key maps its OWN height across the range and both feel the same.
    static constexpr int kVelMin = 30, kVelMax = 120;
    uint8_t velFromY(float yTopPx, float yBotPx) const
    {
        const float h = yBotPx - yTopPx;
        float f = h > 1.0f ? (ImGui::GetIO().MousePos.y - yTopPx) / h : 0.5f;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        const int v = kVelMin + (int)std::lround(f * (float)(kVelMax - kVelMin));
        return (uint8_t)(v < 1 ? 1 : (v > 127 ? 127 : v));
    }
    static int clampMidi(int n) noexcept { return n < 0 ? 0 : (n > 127 ? 127 : n); }
    void pressKey(int note, uint8_t vel)
    {
        note = clampMidi(note);               // defensive: never emit an out-of-range note
        if (kbNote == note) return;
        if (kbNote >= 0) sendNote(0, (uint8_t)kbNote, 0);
        sendNote(0, (uint8_t)note, vel);
        kbNote = note;
    }
    void octButton(const char* id, float x0, float y0, float x1, float y1, const char* lab, int delta, const char* tip = nullptr)
    {
        const ImVec2 b0 = P(x0, y0), b1 = P(x1, y1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        // Cap baseMidi at 84 so the top generated key (base + 35) stays <= 127 MIDI.
        if (ImGui::IsItemClicked()) { baseMidi += delta; if (baseMidi < 12) baseMidi = 12; if (baseMidi > 84) baseMidi = 84; }
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tip);
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
        tips[kParamPmFenvPWM] = "Poly-mod: filter envelope to oscillator 1 pulse width.";
        tips[kParamPmOscBOscA] = "Poly-mod: oscillator 2 to oscillator 1 pitch.";
        tips[kParamPmOscBPWM] = "Poly-mod: oscillator 2 to oscillator 1 pulse width.";
        tips[kParamPmOscBFilt] = "Poly-mod: oscillator 2 to filter cutoff at audio rate.";
        tips[kParamModFilterModel] = "Select the open early ladder or bandwidth-limited late revision.";
        tips[kParamModOsc2Osc1] = "Audio-rate phase modulation from oscillator 2 to oscillator 1.";
        tips[kParamModOsc3Filter] = "Audio-rate cutoff modulation from oscillator 3.";
        tips[kParamShRate] = "Sample-and-hold clock rate.";
        tips[kParamCosmosChorus] = "Built-in chorus mode: off, I, II, or both.";
        tips[kParamLfo1Rate] = tips[kParamLfo2Rate] = "Speed of the LFO.";
        tips[kParamLfo1Shape] = tips[kParamLfo2Shape] = "Waveform of the LFO.";
        tips[kParamLfo1Fade] = tips[kParamLfo2Fade] = "Time for the LFO to fade in after a note.";
        tips[kParamLfo1Sync] = tips[kParamLfo2Sync] = "Lock the LFO to the host tempo and song position; notes no longer restart it.";
        tips[kParamUnisonVoices] = "Stacked detuned voices per note.";
        tips[kParamUnisonDetune] = "Spread of detuning across unison voices, in cents.";
        tips[kParamUnisonSpread] = "Stereo spread of unison voices.";
        tips[kParamPortaTime] = "Glide time between notes.";
        tips[kParamLegato] = "Glide only when notes overlap.";
        tips[kParamGlideMode] = "Glide as a fixed time or a fixed rate.";
        tips[kParamVelSens] = "How strongly velocity affects level.";
        tips[kParamVelCurve] = "Response curve applied to incoming velocity.";
        tips[kParamPbRange] = "Pitch-bend range, in semitones.";
        tips[kParamArpOn] = "Enable the arpeggiator, or run the pattern sequencer in Acid.";
        tips[kParamArpMode] = "Note order the arpeggiator plays.";
        tips[kParamArpOctave] = "Range the arpeggio spans, in octaves.";
        tips[kParamArpRate] = "Step length as a note division.";
        tips[kParamArpGate] = "Length of each step relative to its slot.";
        tips[kParamArpSwing] = "Delays off-beat steps for a swung feel.";
        tips[kParamArpLatch] = "Hold the pattern after keys are released.";
        tips[kParamArpVelMode] = "Velocity source for steps: as played, fixed, or accented.";
        tips[kParamArpFixedVel] = "Velocity used when the mode is fixed.";
        tips[kParamArpAccentPattern] = "Accent shape over the 16-step grid, "
                                       "used when the velocity mode is Accent.";
        for (int i = 0; i < 16; ++i) tips[kParamArpStep0 + i] = "Turn this step on or off.";
        tips[kParamDriveOn] = "Enable the post-synth drive effect. Acid filter drive remains active.";
        tips[kParamDriveType] = "Post-synth drive character: soft, hard, or tube.";
        tips[kParamDriveAmt] = "Drive amount. In Acid this always drives the filter input and also feeds enabled FX drive.";
        tips[kParamDriveMix] = "Blend of post-synth driven and clean signal.";
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
    duskdpf::DuskImGuiTextInputFocus textInputFocus;
    duskdpf::CrispFontSet fontSet;
    ImDrawList* dl = nullptr;
    float  values[kParamCount] = {};
    float  defaults[kParamCount] = {};
    const char* tips[kNumCoreParams] = {};
    float  s = 1.0f;
    ImVec2 org = ImVec2(0, 0);
    bool   gripCursorSet = false;  // NWSE cursor currently pushed to the window

    // The one edit gesture that can be open at a time, or -1. Written only by the
    // beginEdit/endEdit overrides; read by closeOrphanedEdit() and the destructor.
    int    openEditParam = -1;

    // mode crossfade
    int    curMode = 0, prevMode = 0;
    float  modeBlend = 1.0f;
    MSPal  fromPal{}, live{};

    // currentPreset is a COMBINED index: 0..kNumFactoryPresets-1 select factory
    // presets; kNumFactoryPresets + n selects user preset n in presetStore.list().
    // -1 = nothing recalled. programLoaded() (host->UI) only ever maps to factory.
    int    currentPreset = -1;
    // Last (sequence << 8 | program) seen from the shell's MIDI program-change
    // signal; 0 matches its initial value, so an untouched plugin never syncs.
    uint32_t lastMidiProgramSignal = 0;
    bool   showMod = false;
    bool   modPopupWasOpen = false;   // a Source/Dest dropdown was up last frame

    // user preset library + save modal state
    scpreset::Store presetStore;
    bool   showSaveModal = false;
    bool   saveModalJustOpened = false;
    bool   overwriteConfirm = false;
    bool   deleteConfirm = false;
    char   saveNameBuf[128] = {};
    // Cached answers about saveNameBuf (sanitize + a stat), recomputed on edit.
    bool   saveNameDirty = true;
    bool   saveNameValid = false;
    bool   saveNameExists = false;
    bool   saveNameHadText = false;   // buffer non-empty at the END of last frame
    char   saveError[192] = {};       // last failed write, shown in the hint slot

    // preset browser modal. browseIdx[] holds COMBINED indices that survive the
    // current filter; it is rebuilt only when a filter changes (browseDirty), and
    // is a fixed member array, so an open browser allocates nothing per frame.
    bool   showBrowse = false;
    bool   browseJustOpened = false;
    bool   browseDirty = true;
    char   browseSearch[64] = {};
    bool   browseSearchHadText = false;   // buffer non-empty at the END of last frame
    int    browseModeFilter = -1;    // -1 = every mode, else 0..5
    int    browseSrcFilter = 0;      // 0 both banks, 1 factory only, 2 user only
    int    browseSel = -1;           // cursor: index INTO browseIdx, not a preset id
    int    browseRow0 = 0;           // first visible grid row (whole-row scrolling)
    float  browseWheelAcc = 0.0f;    // sub-row wheel remainder (touchpad deltas)
    int    browseN = 0;
    int    browseIdx[kBrowseMax] = {};
    int    factoryMode[kNumFactoryPresets] = {};   // derived once in the ctor
    // Preset-combo popup: column width in DEVICE px, measured from the live atlas
    // face each time the popup opens (see measurePopupColumn).
    float  popupColW = 0.0f;

    // filter-curve cache
    static constexpr int kFcN = 180;
    float  fcX[kFcN] = {}, fcY[kFcN] = {};
    float  fcCutoff = -1, fcRes = -1, fcHP = -1; int fcMode = -1;

    // ADSR caches
    static constexpr int kAdsrN = 40;
    ImVec2 ampEnv[kAdsrN], filtEnv[kAdsrN], acidEnv[kAdsrN];
    float  ampHash = -1, filtHash = -1, acidEnvHash = -1;

    // scope
    float  scope[msynth::MultiSynthDSP::kScopeSize] = {};

    // VU ballistics
    float  vuL = -60.0f, vuR = -60.0f, clipL = 0, clipR = 0; // vu smoothed in dBFS

    // keyboard
    int    baseMidi = 48;   // C3
    int    kbNote = -1;

    // performance wheels. *Value is what the widget shows, *Sent is what the engine
    // was last told, so the atomics are written only on change.
    float  pbValue = 0.0f,  modValue = 0.0f;
    float  pbSent  = 0.0f,  modSent  = 0.0f;
    bool   pbDragging = false, modDragging = false;  // set by drawWheel(), consumed by updateWheels()
    bool   pbLocal = false;      // the current bend was authored HERE, not by the host

    // Latched FX read-out panel: 0 none, 1 DELAY, 2 REVERB (see drawFXStrip).
    int    fxReadoutPanel = 0;

    // misc animation
    bool   pitchDragging = false;

    // local skew/ratio-knob drag state (one knob active at a time, like the shared knob)
    float  skewL = 0.0f;
    bool   skewReset = false;

   #ifdef MSYNTH_FRAME_PROFILE
    double profLogic[100] = {}, profTotal[100] = {};
    int    profN = -20; // skip warmup frames
    int    vtxMax[kNumLayers] = {};   // running per-layer vertex maximum
   #endif

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiSynthUI)
};

UI* createUI()
{
    return new MultiSynthUI();
}

END_NAMESPACE_DISTRHO
