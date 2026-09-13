// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskVerbUI.cpp — the Dear ImGui editor for DuskVerb 2.
//
// Design space is 1200 x 800 (exact 3:2), drawn with custom ImDrawList work and
// scaled uniformly; the window is aspect-locked with a 1050 x 700 minimum, which
// is on the same ratio (see DafPluginInfo.h for why that matters).
//
// Layout, top to bottom:
//   * header: name, preset, engine, INIT/SAVE, A/B, COPY, help
//   * IN and OUT LED rails down the full body height
//   * INPUT / FILTER | OUTPUT HISTORY / DECAY / SIZE | OUTPUT / EARLY REFLECTIONS
//   * full-width DAMPING and TONAL CORRECTION
//   * MODULATION | MACRO
//
// PARAMETER DOMAIN. The knobs work in the HOST domain, which for a tapered
// parameter is the normalised 0..1 coordinate (see DuskVerbParamTable.hpp:
// DAF exposes no taper the shipping formats honour). That is deliberate and not
// a shortcut — dragging in the normalised coordinate reproduces the JUCE build's
// feel, where a skewed NormalisableRange is what the slider travels through.
// DuskPanel::knob's toDisplay/fromDisplay hooks map that coordinate to the
// physical value for the read-out and for typed entry, so what the user sees and
// types is always the plain value.
//
// WHAT IS DELIBERATELY NOT ON THE PANEL. About seventy of the ninety-two
// parameters are tuning-only (pteq_*, post_band_*, edt_*, in_loop_*,
// bass_shelf_*, dpv_*, qt_*, er_boost/rise/bus_*, tank_*, mb_*, the transient
// shaper, the input band gains, tail_spin_*, ...). They are baked per preset by
// the calibration campaign and the JUCE editor does not show them either; they
// stay reachable to a host as automation targets and through saved state.

#include "DafUI.hpp"
#include "DuskVerbAccess.hpp"
#include "DuskVerbFormat.hpp"
#include "DuskVerbPresetImport.hpp"
#include "DuskVerbVersion.hpp"

#include "../core/DuskVerbParamTable.hpp"   // paramDesc / host<->plain / factory presets
#include "dsp/AlgorithmConfig.h"            // engine roster + the `visible` curation

#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"
#include "DuskKnobRing.hpp"        // spacedText: the letter-spaced small caps
#include "DuskLedMeter.hpp"
#include "DuskSupportersOverlay.hpp"
#include "DuskUserPresetStore.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

// Barlow Condensed, bundled under the SIL OFL. Generated into the build tree by
// dusk_daf_embed_font() from the single copy in plugins/shared-daf/fonts, so the
// typography is identical on every OS without committing the bytes twice.
#include "DuskVerbFontSemiBold.inc"
#include "DuskVerbFontRegular.inc"

START_NAMESPACE_DAF

namespace
{
    constexpr float kDesignW = 1200.0f;
    constexpr float kDesignH = 800.0f;
    constexpr float kMinW = 1050.0f;
    constexpr float kMinH = 700.0f;
    constexpr float kMinScale = kMinW / kDesignW;

    constexpr ImU32 kPageBg     = IM_COL32(11, 17, 23, 255);
    constexpr ImU32 kCardBg     = IM_COL32(16, 24, 32, 255);
    constexpr ImU32 kCardEdge   = IM_COL32(43, 57, 68, 255);
    constexpr ImU32 kCardEdgeHi = IM_COL32(73, 91, 104, 255);
    constexpr ImU32 kDim        = IM_COL32(163, 177, 190, 255);
    constexpr ImU32 kText       = IM_COL32(243, 234, 216, 255);
    constexpr ImU32 kAccent     = IM_COL32(189, 239, 87, 255);
    constexpr ImU32 kRingTrack  = IM_COL32(35, 47, 59, 255);
    constexpr ImU32 kKnobTop    = IM_COL32(43, 57, 70, 255);
    constexpr ImU32 kKnobBottom = IM_COL32(17, 25, 33, 255);
    constexpr ImU32 kKnobRim    = IM_COL32(69, 87, 102, 255);
    constexpr ImU32 kPointer    = IM_COL32(244, 231, 201, 255);
    constexpr ImU32 kStripBg    = IM_COL32(21, 31, 41, 255);
    constexpr ImU32 kHeaderBg   = IM_COL32(10, 16, 22, 255);
    constexpr ImU32 kLabelInk   = IM_COL32(225, 229, 235, 255);
    constexpr ImU32 kValueInk   = IM_COL32(220, 246, 139, 255);

    // Larger native font sizes preserve readability at the minimum window.
    constexpr float kLabelPx = 18.0f, kValuePx = 22.0f;
    constexpr float kLabelSmallPx = 18.0f, kValueSmallPx = 22.0f;
    constexpr float kComboPx = 18.0f;
    constexpr float kHeroValuePx = 30.0f, kHeroUnitPx = 18.0f;
    constexpr float kMeterPx = 14.0f;
    constexpr float kPi = 3.14159265358979f;

    constexpr float kHeaderH = 62.0f;
    constexpr float kRailLX0 = 12.0f, kRailLX1 = 42.0f;
    constexpr float kRailRX0 = 1158.0f, kRailRX1 = 1188.0f;
    constexpr float kBodyX0 = 52.0f, kBodyX1 = 1148.0f;
    constexpr float kRow1Y0 = 76.0f, kRow1Y1 = 188.0f;
    constexpr float kRow2Y0 = 76.0f, kRow2Y1 = 270.0f;
    constexpr float kRow3Y0 = 455.0f, kRow3Y1 = 623.0f;
    constexpr float kRow4Y0 = 631.0f, kRow4Y1 = 784.0f;
    constexpr float kInputX0 = 52.0f, kInputX1 = 440.0f;
    constexpr float kFiltX0 = 52.0f, kFiltX1 = 440.0f;
    constexpr float kOutX0 = 800.0f, kOutX1 = 1148.0f;
    constexpr float kDampX0 = 52.0f, kDampX1 = 1148.0f;
    constexpr float kTimeX0 = 455.0f, kTimeX1 = 785.0f;
    constexpr float kErX0 = 800.0f, kErX1 = 1148.0f;
    constexpr float kModX0 = 52.0f, kModX1 = 475.0f;
    constexpr float kMacroX0 = 490.0f, kMacroX1 = 1148.0f;
    constexpr float kKnobR = 34.0f, kKnobSmallR = 28.0f;
    constexpr float kKnobErR = 34.0f, kKnobDampR = 34.0f;
    constexpr float kHeroR = 73.0f;
    constexpr float kRow2Cy = 190.0f, kRow3Cy = 572.0f, kRow4Cy = 740.0f;
    constexpr float kHeroCx = 570.0f, kHeroCy = 318.0f;
    constexpr float kLabelDy = 48.0f, kValueDy = 29.0f;

    using namespace duskverb::ui;   // formatPlainValue / formatDecayParts / labelFor

    // Tooltips, word for word from the JUCE editor so the two builds document
    // the same controls. Anything not listed falls back to the parameter name.
    const char* tooltipFor(EngineType e, int p)
    {
        const bool spring = e == EngineType::Spring, shimmer = e == EngineType::Shimmer,
                   gated  = e == EngineType::NonLinear;
        switch (p)
        {
            case duskverb::ModDepth:
                if (spring)  return "Spring Length: read-position LFO depth (subtle wobble that gives the tank its 'drip' character)";
                if (shimmer) return "Pitch: in-loop pitch interval (0 = unity, 50% = +12 semitones / +1 octave, 100% = +24 / +2 octaves)";
                if (gated)   return "Attack: gate open time (1 - 50 ms). 1-3 ms gives the classic instant-snap; longer for a softer breathing feel.";
                break;
            case duskverb::ModRate:
                if (spring)  return "Drip: spring-tank LFO rate (Hz)";
                if (shimmer) return "Feedback: cascade strength (0 = single pitched pass, 95% = long cascading octaves). Higher feedback builds more shimmer at the cost of slower decay.";
                if (gated)   return "Release: gate close time (5 - 2000 ms). Short values (5-50 ms) = the classic 80s gated-snare cliff; longer values let the hall tail fade naturally after the gate.";
                break;
            case duskverb::Diffusion:
                if (gated)   return "Hold: how long the gate stays fully open after the dry input drops below threshold (0 - 500 ms). 100-200 ms is classic gated-snare territory.";
                if (spring)  return "Chirp: dispersion-AP coefficient - 0 = plain delay, 1 = full spring 'boing' on transients";
                break;
            case duskverb::MidMult:
                if (gated)   return "Threshold: dry-input level above which the gate opens. -32 dB is typical for snare; lower triggers on quieter hits, higher only on loud transients.";
                break;
            default: break;
        }
        switch (p)
        {
            case duskverb::Predelay:
                return "Delay before reverb starts. Creates space between dry signal and reverb tail";
            case duskverb::Decay:
                return "Reverb tail length (RT60). Hero control - drag vertically.";
            case duskverb::Size:
                return "Virtual room size - affects echo density and spacing";
            case duskverb::ModDepth:
                return "Chorus-like modulation depth. Reduces metallic ringing";
            case duskverb::ModRate:
                return "Speed of internal pitch modulation";
            case duskverb::Damping:
                return "High-frequency decay multiplier. <1x = natural air absorption";
            case duskverb::BassMult:
                return "Low-frequency decay multiplier. >1x = bass rings longer than mids";
            case duskverb::MidMult:
                return "Mid-band decay multiplier (between low and high crossovers).\n"
                       "1.0x = natural rate. >1x = mids ring longer; <1x = mids decay faster.";
            case duskverb::Crossover:
                return "Bass/mid split frequency. Below this, bass multiplier applies.";
            case duskverb::HighCrossover:
                return "Mid/treble split frequency. Above this, treble multiplier applies.";
            case duskverb::Saturation:
                return "In-loop tanh drive. 0% = clean (transparent reverb).\n"
                       "100% = warm analog-style saturation on every loop pass.";
            case duskverb::Diffusion:
                return "Tail density. Low = sparse, audible echo grain. High = smooth dense wash.\n"
                       "Affects both input transient smear and in-loop tank density across all engines.";
            case duskverb::ErLevel:
                return "Early reflections level. First echoes that define room shape";
            case duskverb::ErSize:
                return "Early reflection spacing. Larger = bigger perceived room";
            case duskverb::Mix:
                return "Balance between dry input and reverb. Use BUS mode for send/return";
            case duskverb::LoCut:
                return "High-pass filter on the wet signal";
            case duskverb::HiCut:
                return "Low-pass filter on the wet signal";
            case duskverb::MonoBelow:
                return "Sums the wet signal to MONO below this cutoff. 20 Hz = bypass.\n"
                       "Use 80-150 Hz to keep low-end punch tight in a mix.";
            case duskverb::MonoBelowDepth:
                return "How much of the low band is summed to mono.\n"
                       "100% = fully mono below the cutoff, 0% = untouched.";
            case duskverb::Width:
                return "Stereo width of the wet signal";
            case duskverb::GainTrim:
                return "Output gain offset applied after the dry/wet mix";
            case duskverb::Duck:
                return "Ducks the wet tail while the dry input is loud, then lets it swell back\n"
                       "as the source decays. 0 = off. Keeps vocals/drums up front.";
            case duskverb::Tone:
                return "Global spectral tilt over any space: left = darker, centre = preset as-voiced,\n"
                       "right = brighter. Layers on top of the preset's voicing.";
            case duskverb::Character:
                return "Adds movement + grit: more modulation/chorus and saturation\n"
                       "as you turn it up. 0 = the preset as-voiced.";
            case duskverb::Freeze:
                return "Freeze the reverb tail - input is muted and the existing tail\n"
                       "loops indefinitely. Useful for ambient pads and risers.";
            case duskverb::GateEnabled:
                return "Gate (Gated engine only): when ON the envelope shapes the per-tap gains.\n"
                       "When OFF the envelope is bypassed and you hear the underlying dense\n"
                       "tap wash with no gating character. No effect on other engines.";
            case duskverb::BusMode:
                return "Bus mode - outputs 100% wet signal regardless of DRY/WET.\n"
                       "Use on a send/return aux with the DRY/WET knob disabled.";
            case duskverb::PredelaySync:
                return "Tempo-sync pre-delay to the host's BPM.\n"
                       "When set to a note value, overrides the PRE-DELAY knob.";
            case duskverb::Algorithm:
                return "Reverb space. Switching here changes the DSP\n"
                       "without overwriting your knob values.";
            case duskverb::TonalCorrection:
                return "Hall engines only: flattens the decay-coupled steady-state energy of the\n"
                       "octave feedback network. Helps a space whose tail has drifted bright\n"
                       "or dark; off is the as-voiced preset.";
            default:
                return duskverb::paramDesc(p).name;
        }
    }

    // DuskPanel::knob gesture domain (the knob domain: JUCE-skew normalised for
    // tapered parameters, plain otherwise) -> printed value, and back for typed
    // entry. The printed value is what the DSP receives for that knob position
    // (knob -> plain host value -> the JUCE quantisation chain), so the read-out
    // cannot disagree with the sound.
    ImU32 fade(ImU32 c, float amount)
    {
        amount = std::clamp(amount, 0.0f, 1.0f);
        const ImU32 a = (c >> 24) & 0xffu;
        return (c & 0x00ffffffu) | ((ImU32)(a * amount + 0.5f) << 24);
    }

    ImU32 blend(ImU32 a, ImU32 b, float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const auto ch = [t](ImU32 x, ImU32 y, int shift)
        {
            const float xv = (float)((x >> shift) & 0xffu);
            const float yv = (float)((y >> shift) & 0xffu);
            return (ImU32)(xv + (yv - xv) * t + 0.5f) << shift;
        };
        return ch(a, b, 0) | ch(a, b, 8) | ch(a, b, 16) | ch(a, b, 24);
    }

    // DuskPanel::knobAngle measures from 12 o'clock with the pointer direction
    // (sin a, -cos a); ImDrawList::PathArcTo measures from +x. Same dial, 90°
    // apart.
    float arcAngle(float t) { return duskdaf::DuskPanel::knobAngle(t) - 0.5f * kPi; }

} // namespace

class DuskVerbUI : public UI, public duskdaf::ParamHost
{
public:
    //--- duskdaf::ParamHost ---------------------------------------------------
    void beginEdit(uint32_t idx) override { editParameter(idx, true); }
    void endEdit(uint32_t idx) override   { editParameter(idx, false); }
    void setParam(uint32_t idx, float v) override
    {
        if (idx >= (uint32_t)duskverb::kNumParams || !std::isfinite(v))
            return;
        const duskverb::ParamDesc& d = duskverb::paramDesc((int)idx);
        v = duskverb::snapKnobValue(d, v);
        values_[idx] = v;
        markPresetModified();
        setParameterValue(idx, duskverb::knobToHost(d, v));
    }

    DuskVerbUI()
        : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        for (int i = 0; i < duskverb::kNumParams; ++i)
            values_[i] = duskverb::knobDefault(duskverb::paramDesc(i));

        // The minimum is exactly on the design ratio: pugl stores one pair as
        // both minimum and locked aspect, so anything else advertises a ratio
        // the editor is not drawn to (DafClapResizeTest guards this).
        setGeometryConstraints((uint32_t)kMinW, (uint32_t)kMinH, true);

        // Two weights of the bundled Barlow Condensed: semibold for control
        // labels and product marks, regular for numeric read-outs. The atlas is
        // baked at the EXACT pixel sizes the layout draws at the current scale,
        // and re-baked (onImGuiPrepareFrame) whenever the window settles at a
        // new size: ImGui rescales a bitmap glyph and blurs it otherwise
        // (playbook landmine 4), which is what a 0.89x REAPER window showed.
        // The loader measures the BUILT atlas and shrinks until it fits
        // GL_MAX_TEXTURE_SIZE — a software GL context caps that far below what
        // these bakes want, and an oversized upload fails silently with every
        // label painting as a solid rectangle.
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize_);
        if (const char* const forced = std::getenv("DUSK_GL_MAX_TEXTURE"))
        {
            const int v = std::atoi(forced);
            if (v > 0) maxTextureSize_ = (GLint)v;
        }
        bakeFonts(1.0f);

        duskdaf::Palette pal;
        pal.white    = kText;
        pal.whiteDim = kDim;
        pal.accent   = kAccent;
        panel_.setPalette(pal);

        for (float& f : tail_) f = -100.0f;
        inMeter_.reset();
        outMeter_.reset();

        scanUserPresets();
        syncFromBridge();
        activeSlot_ = editorState().activeSlot;
    }

protected:
    //--- host -> UI -----------------------------------------------------------
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= (uint32_t)duskverb::kNumParams || !std::isfinite(value))
            return;
        const duskverb::ParamDesc& d = duskverb::paramDesc((int)index);
        values_[index] = duskverb::hostToKnob(d, duskverb::snapHostValue(d, value));
        // Automation does not publish a new snapshot revision. Read its edited
        // status here so host edits show the same marker as native gestures,
        // without treating a factory-program parameter burst as an edit.
        if (!presetModified_ && duskVerbReadSnapshot != nullptr)
        {
            duskverb::StateValues state;
            if (duskVerbReadSnapshot(getPluginInstancePointer(), state))
                presetModified_ = state.edited;
        }
        // Deliberately does NOT clear the displayed preset identity, unlike an
        // edit made here. A host program change is delivered as a burst of these
        // AFTER programLoaded() on some wrappers, so clearing would wipe the name
        // the program change just set. The identity still follows the DSP through
        // syncFromBridge(), which is the authority for a host-driven change.
    }

    void programLoaded(uint32_t index) override
    {
        const auto& presets = getFactoryPresets();
        if (index >= presets.size())
            return;
        currentPreset_ = (int)index;
        currentUserName_.clear();
        currentUserPath_.clear();
        // A host-driven program change does not deliver parameterChanged() for
        // the values it moved (the VST3 wrapper flags only the program index),
        // so mirror the preset into the local cache or every knob keeps drawing
        // the value the DSP has already left behind. Store only: the plugin
        // applied these itself when the host called loadProgram.
        mirrorPreset(presets[index]);
        lastBridgeProgram_ = (int)index;
        presetModified_ = false;
    }

    // Every design size the panel draws, per weight, so that at any scale the
    // requested pixel size lands on a baked face exactly (CrispFontSet::pick
    // then scales by 1.0). Sizes are design px; bakeFonts multiplies by scale.
    static constexpr float kLabelDesignSizes[] =
        { 14.0f, 16.0f, kLabelPx, kLabelPx + 1.0f, 20.0f, 22.0f, 24.0f, 30.0f, 36.0f };
    static constexpr float kValueDesignSizes[] =
        { kMeterPx, kValueSmallPx, kValuePx, kHeroUnitPx, 16.0f, kHeroValuePx };

    void bakeFonts(float scale)
    {
        const duskdaf::EmbeddedFontRequest requests[2] = {
            { kDvFontSemiBold, kDvFontSemiBold_len, kLabelDesignSizes,
              (int)(sizeof(kLabelDesignSizes) / sizeof(kLabelDesignSizes[0])) },
            { kDvFontRegular, kDvFontRegular_len, kValueDesignSizes,
              (int)(sizeof(kValueDesignSizes) / sizeof(kValueDesignSizes[0])) },
        };
        duskdaf::CrispFontSet sets[2];
        duskdaf::loadEmbeddedCrispFontSets(requests, 2, sets, scale, (int)maxTextureSize_);
        labelFonts_ = sets[0];
        valueFonts_ = sets[1];
        labelFont_  = labelFonts_.pick(kLabelPx * scale);
        panel_.setFontSet(labelFonts_);
        bakedScale_ = scale;
    }

    // Re-bake once the window has settled at a new scale (unchanged for a
    // frame): during a resize drag the old atlas is stretched for a few frames,
    // which is fine; afterwards every glyph is native again. Runs before the
    // ImGui frame with the GL context current, the only place this is safe.
    void onImGuiPrepareFrame() override
    {
        const float scale = (float)getWidth() / kDesignW;
        if (scale <= 0.0f) return;
        if (std::fabs(scale - lastSeenScale_) > 1e-4f) { lastSeenScale_ = scale; return; }
        if (std::fabs(scale - bakedScale_) <= 1e-4f) return;
        bakeFonts(scale);
        rebuildFontTexture();
    }

    void onImGuiDisplay() override
    {
        const float winW = (float)getWidth();
        const float winH = (float)getHeight();
        s_   = std::min(winW / kDesignW, winH / kDesignH);
        org_ = ImVec2(0.5f * (winW - kDesignW * s_), 0.5f * (winH - kDesignH * s_));
        panel_.begin(s_, org_, labelFont_, this);

        syncFromBridge();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("DuskVerb2", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), kPageBg);

        // Stock-ImGui text that this file does not place itself — above all the
        // knob tooltips, which DuskPanel::knob raises internally — would
        // otherwise render in ImGui's 13 px default face, unscaled and blurry at
        // every zoom but one. A body font for the whole window fixes those; the
        // combos and the modals push their own on top of it.
        ImFont* bodyFont = panel_.pickFont(kComboPx * s_);
        if (bodyFont != nullptr) ImGui::PushFont(bodyFont);
        // Same reasoning for the tooltip CHROME: ImGui's own popup colours and
        // its unscaled 8 px padding give a pale, cramped box that clips its own
        // descenders on this panel. Pushed after Begin so it reaches only the
        // windows opened later in the frame (tooltips, popups); the combos and
        // the modals still push their own on top.
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0x12, 0x17, 0x20, 246));
        ImGui::PushStyleColor(ImGuiCol_Border, fade(kAccent, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f * s_, 7.0f * s_));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * s_);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * s_);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f * s_);

        // The help overlay draws no interactive content of its own, so it needs a
        // full-window blocker submitted BEFORE the controls (it has to win
        // ImGui's hover race) both to swallow clicks and to be the thing that
        // dismisses it.
        if (showHelp_)
        {
            ImGui::SetCursorScreenPos(ImVec2(0, 0));
            ImGui::InvisibleButton("##dv_modalblock", ImVec2(winW, winH));
            if (ImGui::IsItemClicked())
                showHelp_ = false;
        }

        // The body is disabled rather than covered while a modal is up, because
        // the supporters overlay DOES have interactive content — a scrolling
        // name list and its own click-to-dismiss scrim — and a blocker submitted
        // ahead of it would win the hover race and eat both. Disabling also stops
        // a knob under the scrim from painting its value bubble, which goes on
        // the foreground draw list and would otherwise composite over the top.
        const bool disableBody = showSupporters_ || showHelp_;
        if (disableBody) ImGui::BeginDisabled();

        drawHeader(dl);
        drawRails(dl);
        drawEnvelopeCard(dl);
        drawInputCard(dl);
        drawFilterCard(dl);
        drawOutputCard(dl);
        drawDampingCard(dl);
        drawTimeCard(dl);
        drawEarlyCard(dl);
        drawModulationCard(dl);
        drawMacroCard(dl);
        drawDeleteModal();
        drawPresetErrorModal();

        if (disableBody) ImGui::EndDisabled();

        handleKeyboard();

        if (showHelp_)
            drawHelpOverlay(dl, winW, winH);
        if (showSupporters_)
            duskdaf::drawSupportersOverlay(panel_, dl, kDesignW, kDesignH, showSupporters_,
                                           "DuskVerb 2", DUSKVERB2_VERSION_STRING,
                                           &supporters_);

        // Submitted LAST so it wins the hover race and paints over everything.
        // AUv2 hosts never give a plugin window a grip of their own; elsewhere
        // this is simply a second way to do what the host's grip does.
        const duskdaf::ResizeGripState grip =
            panel_.resizeGrip(dl, winW, winH, kDesignW, kDesignH, kMinScale, 3.0f);

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(3);
        if (bodyFont != nullptr) ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar(2);
        textInputFocus_.update(*this);

        // The DAF-Widgets ImGui backend never forwards ImGui::SetMouseCursor()
        // to the window, so drive DGL's cursor directly. Edge-triggered:
        // setCursor() is a window call, not a per-frame one.
        if (grip.hot != gripCursorSet_)
        {
            gripCursorSet_ = grip.hot;
            setCursor(gripCursorSet_ ? DGL_NAMESPACE::kMouseCursorUpLeftDownRight
                                     : DGL_NAMESPACE::kMouseCursorArrow);
        }
        if (grip.resized)
            setSize(grip.width, grip.height);   // after End(): a host may run it synchronously
    }

private:
    //======================================================================
    // small helpers
    //======================================================================
    ImVec2 P(float x, float y) const { return ImVec2(org_.x + x * s_, org_.y + y * s_); }
    float  sc() const { return s_; }

    void label(ImDrawList* dl, float x, float y, float size, ImU32 col,
               const char* txt, int align) const
    {
        panel_.text(dl, x, y, size, col, txt, align, true);
    }

    // Numeric read-outs use the regular weight. The mockup asks for a monospace
    // face; shared-daf bundles only Barlow Condensed, and pulling in a second
    // family for the digits alone is a new dependency, so the regular weight
    // stands in for it. Digits in this face are uniform width, so a changing
    // value does not jitter a centred read-out.
    // Width of a string in DESIGN px for a given face set and design size, for
    // the few places that lay a label out against a number whose width moves.
    float textWidth(const duskdaf::CrispFontSet& set, float size, const char* txt) const
    {
        const float px = size * s_;
        ImFont* font = set.pick(px);
        if (font == nullptr) font = panel_.pickFont(px);
        return font->CalcTextSizeA(px, FLT_MAX, 0.0f, txt).x / s_;
    }

    void valueText(ImDrawList* dl, float x, float y, float size, ImU32 col,
                   const char* txt, int align) const
    {
        const float px = size * s_;
        ImFont* font = valueFonts_.pick(px);
        if (font == nullptr) { panel_.text(dl, x, y, size, col, txt, align, false); return; }
        const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, txt);
        ImVec2 pos = P(x, y);
        if (align == 0) pos.x -= 0.5f * ts.x;
        if (align == 1) pos.x -= ts.x;
        pos.x = std::floor(pos.x + 0.5f);
        pos.y = std::floor(pos.y + 0.5f);
        dl->AddText(font, px, pos, col, txt);
    }

    // The plain value the DSP holds for the knob's position: knob -> plain host
    // value -> the JUCE quantisation chain.
    float plainOf(int p) const
    {
        const duskverb::ParamDesc& d = duskverb::paramDesc(p);
        return duskverb::dspFromHost(d, duskverb::knobToHost(d, values_[p]));
    }

    // Single write path for a UI-driven change that is NOT a live knob drag:
    // normalise, keep the cache in step, bracket with the host edit gesture.
    //
    // Flags the preset as edited for the same reason ParamHost::setParam does — a
    // switch, an engine change or a sync division the user moved means the panel
    // is no longer exactly the preset it names. The recall paths (a factory
    // preset, a user preset, INIT, an A/B swap) all drive this in a loop and then
    // set the identity they want AFTERWARDS, clearing the flag, so they are
    // unaffected by it being raised here.
    void setP(int p, float knobValue)
    {
        if (p < 0 || p >= duskverb::kNumParams || !std::isfinite(knobValue))
            return;
        const duskverb::ParamDesc& d = duskverb::paramDesc(p);
        const float v = duskverb::snapKnobValue(d, knobValue);
        values_[p] = v;
        markPresetModified();
        editParameter((uint32_t)p, true);
        setParameterValue((uint32_t)p, duskverb::knobToHost(d, v));
        editParameter((uint32_t)p, false);
    }

    void setPlain(int p, float plainValue)
    {
        setP(p, duskverb::plainToKnob(duskverb::paramDesc(p), plainValue));
    }

    // A parameter a preset, INIT or an A/B swap is allowed to move. Bypass is
    // the host's, never a preset's — recalling a preset must not un-bypass the
    // plugin, and neither must swapping snapshots.
    static bool isRecallable(int p) { return p != duskverb::Bypass; }

    EngineType currentEngine() const
    {
        int idx = (int)std::lround(values_[duskverb::Algorithm]);
        idx = std::clamp(idx, 0, getNumAlgorithms() - 1);
        return getAlgorithmConfig(idx).engine;
    }

    static float knobToPlainCb(float knob, uint32_t param, void* context)
    {
        auto& ui = *static_cast<DuskVerbUI*>(context);
        const auto& desc = duskverb::paramDesc(param);
        return duskverb::ui::displayNumber(ui.currentEngine(), param,
            duskverb::dspFromHost(desc, duskverb::knobToHost(desc, knob)));
    }
    static float plainToKnobCb(float value, uint32_t param, void* context)
    {
        auto& ui = *static_cast<DuskVerbUI*>(context);
        return duskverb::plainToKnob(duskverb::paramDesc(param),
            duskverb::ui::plainFromDisplayNumber(ui.currentEngine(), param, value, ui.plainOf(param)));
    }
    static bool parseKnobText(const char* text, uint32_t param, void* context, float& value)
    {
        auto& ui = *static_cast<DuskVerbUI*>(context);
        float plain;
        if (!duskverb::ui::parsePlainValue(ui.currentEngine(), param, text, ui.plainOf(param), plain)) return false;
        value = duskverb::plainToKnob(duskverb::paramDesc(param), plain);
        return true;
    }

    bool gateApplies() const { return currentEngine() == EngineType::NonLinear; }
    bool tonalCorrectionApplies() const
    {
        const EngineType e = currentEngine();
        return e == EngineType::AccurateHall || e == EngineType::AccurateHall32;
    }

    // A panel edit does NOT throw the preset name away. The JUCE editor kept it,
    // and every DAW marks an edited preset rather than forgetting which one it
    // came from — losing the name also loses the "step to the next one from
    // here" that the chevrons depend on. The name is flagged instead, and the
    // combo and the header print "<name> *" until a recall clears the flag.
    void markPresetModified() { presetModified_ = true; }

    // The genuine "nothing is loaded" state: only INIT and a failed recall.
    void clearPresetIdentity()
    {
        currentPreset_ = -1;
        currentUserName_.clear();
        currentUserPath_.clear();
        presetModified_ = false;
    }

    // Preview text for the header combo: the factory name, else the user preset
    // name, else nothing loaded — with a trailing marker while edited.
    const char* presetPreview()
    {
        const auto& presets = getFactoryPresets();
        const char* name = nullptr;
        if (currentPreset_ >= 0 && currentPreset_ < (int)presets.size())
            name = presets[(size_t)currentPreset_].name;
        else if (!currentUserName_.empty())
            name = currentUserName_.c_str();
        if (name == nullptr)
            return "Presets...";
        std::snprintf(previewBuf_, sizeof(previewBuf_), "%s%s", name,
                      presetModified_ ? " *" : "");
        return previewBuf_;
    }

    void mirrorPreset(const FactoryPreset& preset)
    {
        preset.collectParameters([this](const char* id, float plainValue)
        {
            const int index = duskverb::paramIndexForId(id);
            if (index < 0) return;
            values_[index] = duskverb::plainToKnob(duskverb::paramDesc(index), plainValue);
        });
    }

    // Which factory preset's name-keyed engine configuration the DSP currently
    // holds. This is NOT the same as currentPreset_: it survives knob edits,
    // because editing a knob does not undo the post-tank EQ and modulation
    // topology the preset installed. -1 means "defaults".
    int bridgeProgram() const
    {
        return duskVerbGetCurrentProgram != nullptr
             ? duskVerbGetCurrentProgram(getPluginInstancePointer()) : -1;
    }

    // Adopt the DSP's current program without acting on it, so the next
    // syncFromBridge() only reacts to a change that came from somewhere else.
    void latchBridgeProgram() { lastBridgeProgram_ = bridgeProgram(); }

    void setPresetConfig(int index)
    {
        if (duskVerbApplyPresetConfig != nullptr)
            duskVerbApplyPresetConfig(getPluginInstancePointer(), index);
        lastBridgeProgram_ = index;
    }

    // The DSP owns the authoritative program index (loadProgram and setState
    // both set it), so a session restore or a host program change shows up here
    // even though neither routes through programLoaded on every format.
    void syncFromBridge()
    {
        if (duskVerbStateRevision == nullptr || duskVerbReadSnapshot == nullptr)
            return;
        const uint64_t revision = duskVerbStateRevision(getPluginInstancePointer());
        if (revision == lastStateRevision_) return;
        duskverb::StateValues state;
        if (!duskVerbReadSnapshot(getPluginInstancePointer(), state)) return;
        mirrorSnapshot(state);
        lastStateRevision_ = revision;
    }

    duskverb::EditorState& editorState()
    {
        if (duskVerbEditorState != nullptr)
            if (auto* state = duskVerbEditorState(getPluginInstancePointer())) return *state;
        return fallbackEditorState_;
    }

    duskverb::StateValues captureSnapshot() const
    {
        auto state = duskverb::makeDefaultState();
        if (duskVerbReadSnapshot != nullptr) duskVerbReadSnapshot(getPluginInstancePointer(), state);
        // Include the latest gesture even if the host has not echoed it yet.
        for (int i = 0; i < duskverb::kNumParams; ++i)
            state.params[i] = duskverb::knobToHost(duskverb::paramDesc(i), values_[i]);
        state.userName = currentUserName_;
        state.edited = presetModified_;
        return state;
    }

    void mirrorSnapshot(const duskverb::StateValues& state)
    {
        for (int i = 0; i < duskverb::kNumParams; ++i)
            values_[i] = duskverb::hostToKnob(duskverb::paramDesc(i), state.params[i]);
        currentUserName_ = state.userName;
        currentPreset_ = -1;
        if (currentUserName_.empty())
        {
            const auto& presets = getFactoryPresets();
            for (size_t i = 0; i < presets.size(); ++i)
                if (state.presetName == presets[i].name) currentPreset_ = static_cast<int>(i);
        }
        presetModified_ = state.edited;
        lastBridgeProgram_ = bridgeProgram();
    }

    void recallSnapshot(duskverb::StateValues state)
    {
        state.params[duskverb::Bypass] = values_[duskverb::Bypass];
        const auto encoded = duskverb::encodeState(state);
        setState("parameters", encoded.c_str());
        mirrorSnapshot(state);
        lastStateRevision_ = duskVerbStateRevision != nullptr
                           ? duskVerbStateRevision(getPluginInstancePointer()) : 0;
    }

    //======================================================================
    // primitive widgets
    //======================================================================
    void card(ImDrawList* dl, float x0, float y0, float x1, float y1,
              const char* title, bool hero = false) const
    {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kCardBg, 9.0f * s_);
        dl->AddRect(P(x0, y0), P(x1, y1), hero ? kCardEdgeHi : kCardEdge, 9.0f * s_);
        if (title == nullptr) return;
        const float size = 20.0f;
        ImFont* font = panel_.pickFont(size * s_);
        const float width = font->CalcTextSizeA(size * s_, FLT_MAX, 0.0f, title).x / s_
                          + static_cast<float>(std::strlen(title)) * 1.8f;
        duskdaf::spacedText(panel_, dl, x0 + 20.0f, y0 + 12.0f, size, kText, title, -1, 0.14f);
        if (x0 + width + 42.0f < x1 - 18.0f)
            dl->AddLine(P(x0 + width + 42.0f, y0 + 22.0f), P(x1 - 18.0f, y0 + 22.0f), kCardEdgeHi, s_);
    }

    void knobArt(ImDrawList* dl, float cx, float cy, float r, float t, bool hero) const
    {
        const ImVec2 c = P(cx, cy);
        const float R = r * s_;
        const float ringR = R + (hero ? 11.0f : 7.0f) * s_;
        const float ringW = (hero ? 6.0f : 3.2f) * s_;
        const float a0 = arcAngle(0.0f), a1 = arcAngle(1.0f), av = arcAngle(t);

        dl->PathArcTo(c, ringR, a0, a1, 64);
        dl->PathStroke(kRingTrack, 0, ringW);

        if (hero)   // a soft bloom under the hero arc
        {
            dl->PathArcTo(c, ringR, a0, av, 64);
            dl->PathStroke(fade(kAccent, 0.20f), 0, ringW + 6.0f * s_);
        }
        if (t > 0.0005f)
        {
            dl->PathArcTo(c, ringR, a0, av, 64);
            dl->PathStroke(kAccent, 0, ringW);
        }

        // Body: a dark disc lit from the top. Horizontal strips analytically
        // clipped to the circle give a smooth response with no concentric
        // banding, and stay sharp at every scale because it is geometry.
        dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f * s_), R * 1.02f,
                            IM_COL32(0, 0, 0, 120), 48);
        const int bands = std::max(14, (int)(2.0f * R));
        for (int i = 0; i <= bands; ++i)
        {
            const float u = -0.999f + 1.998f * (float)i / (float)bands;
            const float half = std::sqrt(std::max(0.0f, 1.0f - u * u)) * R;
            const ImU32 col = blend(kKnobTop, kKnobBottom, 0.5f * (u + 1.0f));
            dl->AddLine(ImVec2(c.x - half, c.y + u * R), ImVec2(c.x + half, c.y + u * R),
                        col, 1.15f);
        }
        dl->AddCircle(c, R, kKnobRim, 48, 1.1f * s_);

        const float a = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 dir(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + dir.x * R * 0.34f, c.y + dir.y * R * 0.34f),
                    ImVec2(c.x + dir.x * R * 0.88f, c.y + dir.y * R * 0.88f),
                    kPointer, (hero ? 3.0f : 2.0f) * s_);
    }

    // One complete knob cell: art, gestures, name and live read-out.
    // The art is drawn BEFORE the gesture call (so a knob never paints over its
    // own type-entry field) and the read-out AFTER it, which is what keeps the
    // number exact during a drag while the pointer lags by a single frame.
    void knobCell(ImDrawList* dl, int p, float cx, float cy, float r,
                  const char* name, float labelPx = kLabelPx,
                  float valuePx = kValuePx, float tracking = 0.12f)
    {
        const duskverb::ParamDesc& d = duskverb::paramDesc(p);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        const float t = hi > lo ? std::clamp((values_[p] - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        knobArt(dl, cx, cy, r, t, /*hero*/ false);   // the hero is laid out by hand

        char buf[48];
        formatPlainValue(currentEngine(), p, plainOf(p), buf, sizeof(buf));
        char id[24];
        std::snprintf(id, sizeof(id), "##dvk%d", p);
        // Labels and numeric readouts are direct entry targets too. This opens
        // the same edit field as the knob body, with the same unit parser.
        ImGui::PushID(p);
        ImGui::SetCursorScreenPos(P(cx - r - 6.0f, cy - r - kLabelDy - 3.0f));
        ImGui::InvisibleButton("##value_entry", ImVec2((2.0f * r + 12.0f) * s_,
                                                       (kLabelDy - 3.0f) * s_));
        if (ImGui::IsItemClicked()) panel_.openValueEdit(id, values_[p], buf);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        ImGui::PopID();
        panel_.knob(id, (uint32_t)p, lo, hi, cx, cy, r, values_[p], duskverb::knobDefault(d),
                    /*stepped*/ false, /*panelTicks*/ false, "%.2f", "",
                    /*faceColor*/ 0, /*bodyless*/ true, /*persistent*/ false,
                    tooltipFor(currentEngine(), p), /*rightClickReset*/ false, 1.0f, 0.0f, d.name,
                    /*contextMenu*/ true, /*overrideText*/ buf,
                    /*hasExternalReadout*/ true, 0.0f, 0.0f,
                    /*nameOnHover*/ false, /*doubleClickReset*/ false, 9.5f,
                    /*bubbleOnActiveOnly*/ false, /*omitCenterTick*/ false,
                    &knobToPlainCb, &plainToKnobCb, this, /*dragInDisplayDomain*/ false, &parseKnobText);

        formatPlainValue(currentEngine(), p, plainOf(p), buf, sizeof(buf));
        if (name != nullptr)
            duskdaf::spacedText(panel_, dl, cx, cy - r - kLabelDy, labelPx, kLabelInk,
                                name, 0, tracking);
        valueText(dl, cx, cy - r - kValueDy, valuePx, kValueInk, buf, 0);
    }

    // Dark strip with a caption and a pill switch at its right end. This is what
    // the mockup uses for FREEZE / BUS / GATE / TONAL CORRECTION.
    void switchStrip(ImDrawList* dl, const char* id, int p,
                     float x0, float y0, float x1, float y1, const char* caption)
    {
        const bool on = values_[p] > 0.5f;
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s_, (y1 - y0) * s_));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltipFor(currentEngine(), p));
        if (ImGui::IsItemClicked())
            setP(p, on ? 0.0f : 1.0f);

        dl->AddRectFilled(P(x0, y0), P(x1, y1), kStripBg, 5.0f * s_);
        dl->AddRect(P(x0, y0), P(x1, y1), on ? fade(kAccent, 0.55f)
                                             : (hovered ? kKnobRim : kCardEdge),
                    5.0f * s_, 0, 1.0f * s_);
        duskdaf::spacedText(panel_, dl, x0 + 12.0f, 0.5f * (y0 + y1) - 5.5f, kLabelPx,
                            on ? kAccent : kLabelInk, caption, -1, 0.14f);

        const float cy = 0.5f * (y0 + y1);
        const float tx1 = x1 - 9.0f, tx0 = tx1 - 30.0f, th = 7.0f;
        dl->AddRectFilled(P(tx0, cy - th), P(tx1, cy + th),
                          on ? fade(kAccent, 0.35f) : IM_COL32(0x22, 0x27, 0x31, 255),
                          th * s_);
        dl->AddCircleFilled(P(on ? tx1 - th : tx0 + th, cy), (th - 1.5f) * s_,
                            on ? kAccent : IM_COL32(0x6a, 0x73, 0x82, 255), 20);
    }

    // The fleet's small header button. Returns true on click.
    bool headerButton(ImDrawList* dl, const char* id, float x0, float x1,
                      const char* caption, bool lit = false)
    {
        constexpr float y0 = 16.0f, y1 = 46.0f;
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s_, (y1 - y0) * s_));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(P(x0, y0), P(x1, y1),
                          lit ? IM_COL32(0x2c, 0x55, 0x3a, 255)
                              : (hovered ? IM_COL32(54, 54, 58, 255) : IM_COL32(38, 38, 41, 255)),
                          2.0f * s_);
        dl->AddRect(P(x0, y0), P(x1, y1),
                    lit ? kAccent : IM_COL32(90, 90, 94, 255), 2.0f * s_, 0, 1.0f * s_);
        panel_.text(dl, 0.5f * (x0 + x1), 21.0f, 18.0f,
                    lit ? kAccent : (hovered ? kText : kDim), caption, 0, true);
        return ImGui::IsItemClicked();
    }

    bool headerChevron(ImDrawList* dl, const char* id, float cx, bool left)
    {
        constexpr float cy = 31.0f, halfH = 15.0f;
        const ImVec2 b0 = P(cx - 9.5f, cy - halfH), b1 = P(cx + 9.5f, cy + halfH);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hovered ? IM_COL32(54, 54, 58, 255) : IM_COL32(38, 38, 41, 255),
                          2.0f * s_);
        dl->AddRect(b0, b1, IM_COL32(90, 90, 94, 255), 2.0f * s_, 0, s_);
        const ImVec2 c = P(cx, cy);
        const float d = 4.3f * s_;
        const ImU32 ink = hovered ? kText : kDim;
        if (left)
            dl->AddTriangleFilled(ImVec2(c.x + d * 0.5f, c.y - d), ImVec2(c.x + d * 0.5f, c.y + d),
                                  ImVec2(c.x - d * 0.7f, c.y), ink);
        else
            dl->AddTriangleFilled(ImVec2(c.x - d * 0.5f, c.y - d), ImVec2(c.x - d * 0.5f, c.y + d),
                                  ImVec2(c.x + d * 0.7f, c.y), ink);
        return ImGui::IsItemClicked();
    }

    // Combo styling shared by the preset, engine and sync dropdowns. Returns
    // true while the popup is open; the caller emits the rows and calls
    // endCombo().
    bool beginCombo(const char* id, float x0, float y0, float x1, float y1,
                    const char* preview, float fontSize = kComboPx)
    {
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::SetNextItemWidth((x1 - x0) * s_);
        comboFont_ = panel_.pickFont(fontSize * s_);
        if (comboFont_ != nullptr) ImGui::PushFont(comboFont_);
        const float bandH = (y1 - y0) * s_;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(6.0f * s_,
                                   std::max(0.0f, 0.5f * (bandH - ImGui::GetFontSize()))));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(38, 38, 41, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(48, 48, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(55, 55, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 24, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(76, 103, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(92, 124, 58, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(62, 85, 39, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(46, 46, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(58, 58, 62, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 40, 44, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, kText);
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, kAccent);
        // BeginCombo otherwise caps its popup at eight rows; an explicit
        // identity constraint lets the whole preset list show at once.
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
        comboOpen_ = ImGui::BeginCombo(id, preview);
        return comboOpen_;
    }

    void endCombo()
    {
        if (comboOpen_) ImGui::EndCombo();
        ImGui::PopStyleColor(12);
        ImGui::PopStyleVar();
        if (comboFont_ != nullptr) ImGui::PopFont();
        comboOpen_ = false;
        comboFont_ = nullptr;
    }

    //======================================================================
    // header
    //======================================================================
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, kHeaderH), kHeaderBg);
        dl->AddLine(P(0, kHeaderH), P(kDesignW, kHeaderH), kCardEdgeHi, s_);
        duskdaf::spacedText(panel_, dl, 64, 13, 36, kText, "DUSKVERB 2", -1, 0.16f);
        ImGui::SetCursorScreenPos(P(60, 12));
        ImGui::InvisibleButton("##dv_titlecredits", ImVec2(245.0f * s_, 38.0f * s_));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) { supporters_.resetInteraction(); showSupporters_ = true; }
        if (headerChevron(dl, "##dv_prev", 335, true)) stepPreset(-1);
        if (headerChevron(dl, "##dv_next", 575, false)) stepPreset(1);
        drawPresetCombo();
        drawEngineCombo(dl);
        if (headerButton(dl, "##dv_init", 842, 896, "INIT")) initDefaults();
        if (headerButton(dl, "##dv_save", 905, 960, "SAVE"))
        {
            std::snprintf(saveBuf_, sizeof(saveBuf_), "%s", currentUserName_.c_str());
            ImGui::OpenPopup("Save DuskVerb Preset");
        }
        drawSaveModal();
        drawAbButtons(dl);
        if (headerButton(dl, "##dv_help", 1122, 1148, "?", showHelp_)) showHelp_ = !showHelp_;
    }

    void drawPresetCombo()
    {
        const auto& presets = getFactoryPresets();
        if (beginCombo("##dv_presets", 346, 16, 564, 46, presetPreview()))
        {
            if (ImGui::Selectable("Import preset..."))
                importRequested_ = true;
            ImGui::Separator();
            // Factory presets are grouped contiguously by category (see the
            // note in FactoryPresets.h), so a heading whenever the category
            // changes reproduces the JUCE editor's sectioned menu.
            const char* lastCategory = nullptr;
            for (size_t i = 0; i < presets.size(); ++i)
            {
                const FactoryPreset& fp = presets[i];
                if (fp.category != nullptr
                    && (lastCategory == nullptr || std::strcmp(lastCategory, fp.category) != 0))
                {
                    ImGui::SeparatorText(fp.category);
                    lastCategory = fp.category;
                }
                const bool selected = (int)i == currentPreset_;
                if (ImGui::Selectable(fp.name, selected))
                {
                    applyPreset((int)i);
                    ImGui::CloseCurrentPopup();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            if (!userPresets_.empty())
            {
                ImGui::SeparatorText("User");
                // Index-scoped IDs: two files can carry the same display name
                // and ImGui would otherwise give both rows one shared ID.
                for (size_t i = 0; i < userPresets_.size(); ++i)
                {
                    ImGui::PushID((int)i);
                    const UserPreset& up = userPresets_[i];
                    if (ImGui::Selectable(up.name.c_str(),
                                          currentPreset_ < 0 && up.path == currentUserPath_))
                    {
                        loadUserPreset(up);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::BeginPopupContextItem("##dv_userctx"))
                    {
                        if (ImGui::MenuItem("Delete preset"))
                        {
                            deleteName_ = up.name;
                            deletePath_ = up.path;
                            deleteRequested_ = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
        }
        endCombo();
        if (importRequested_)
        {
            importRequested_ = false;
            FileBrowserOptions options;
            options.title = "Import DuskVerb preset (.xml or .dvpreset)";
            if (!openFileBrowser(options))
                presetError_ = "Could not open the preset file browser.";
        }
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename == nullptr) return;
        const std::filesystem::path path(filename);
        loadUserPreset({path.stem().string(), path.string()});
        // Import recalls the sound. Save creates a DuskVerb 2 library copy;
        // the original file remains owned by the original plugin/library.
        if (presetError_.empty()) currentUserPath_.clear();
    }

    void drawAbButtons(ImDrawList* dl)
    {
        const bool flashing = copiedFlash_ > 0.0f;
        if (flashing) copiedFlash_ -= ImGui::GetIO().DeltaTime;
        for (int slot = 0; slot < 2; ++slot)
        {
            const float x0 = 971.0f + static_cast<float>(slot) * 42.0f;
            char id[16]; std::snprintf(id, sizeof(id), "##dv_ab%d", slot);
            if (headerButton(dl, id, x0, x0 + 34.0f, slot == 0 ? "A" : "B", slot == activeSlot_)
                && slot != activeSlot_) switchToSlot(slot);
        }
        if (headerButton(dl, "##dv_copy", 1056, 1113, flashing ? "COPIED" : "COPY")) copyActiveToOther();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Copy %s to %s without switching", activeSlot_ == 0 ? "A" : "B", activeSlot_ == 0 ? "B" : "A");
    }

    void captureSlot(int slot)
    {
        auto& state = editorState();
        state.slots[slot] = captureSnapshot();
        state.valid[slot] = true;
    }

    void switchToSlot(int slot)
    {
        captureSlot(activeSlot_);
        activeSlot_ = slot;
        auto& state = editorState();
        state.activeSlot = slot;
        if (!state.valid[slot])
        {
            captureSlot(slot);
            return;
        }
        recallSnapshot(state.slots[slot]);
    }

    void copyActiveToOther()
    {
        captureSlot(activeSlot_ ^ 1);
        copiedFlash_ = 1.0f;
    }

    //======================================================================
    // presets
    //======================================================================
    void applyPreset(int index)
    {
        const auto& presets = getFactoryPresets();
        if (index < 0 || index >= (int)presets.size())
            return;
        const FactoryPreset& preset = presets[(size_t)index];
        auto state = captureSnapshot();
        duskverb::applyFactoryPresetToHostParameters(preset,
            [&](int i, float value) { state.params[i] = value; });
        state.presetName = preset.name;
        state.userName.clear();
        state.edited = false;
        state.sixAP.densityBaseline = preset.sixAPDensityBaseline;
        state.sixAP.bloomCeiling = preset.sixAPBloomCeiling;
        state.sixAP.earlyMix = preset.sixAPEarlyMix;
        state.sixAP.outputTrim = preset.sixAPOutputTrim;
        std::copy_n(preset.sixAPBloomStagger, 6, state.sixAP.bloomStagger);
        recallSnapshot(state);
        currentUserPath_.clear();
    }

    // Clamped, never wrapping: stepping off either end parks on the end preset.
    void stepPreset(int direction)
    {
        const int count = (int)getFactoryPresets().size();
        int i = currentPreset_ < 0 ? (direction < 0 ? 0 : -1) : currentPreset_;
        i = std::clamp(i + direction, 0, count - 1);
        applyPreset(i);
    }

    // Everything back to the factory defaults, INCLUDING the engine
    // configuration: resetting only the parameters would leave the last
    // preset's post-tank EQ, modulation topology and FDN base delays in place,
    // so "INIT" would not actually be the plugin's default sound. BYPASS is the
    // one exception, as everywhere else — it belongs to the host.
    void initDefaults()
    {
        recallSnapshot(duskverb::makeDefaultState());
        currentUserPath_.clear();
    }

    //--- user preset library --------------------------------------------------
    struct UserPreset { std::string name, path; };

    static std::filesystem::path configDir()
    {
        return duskdaf::userPresetDirectory("DuskVerb2");
    }

    void scanUserPresets()
    {
        duskdaf::scanUserPresets(
            configDir(), ".dvpreset", userPresets_,
            [](const std::filesystem::path& path, UserPreset& preset)
            {
                std::ifstream input(path);
                if (!input) return false;
                const std::string stored = duskdaf::storedUserPresetName(path);
                if (!stored.empty()) preset.name = stored;
                return true;
            });
    }

    bool saveUserPreset(const char* rawName)
    {
        auto state = captureSnapshot();
        state.userName = duskdaf::normaliseUserPresetName(rawName);
        if (state.userName.empty() || state.userName.find_first_of("\r\n") != std::string::npos)
            return false;
        state.edited = false;
        const auto saved = duskdaf::writeUserPreset(
            configDir(), ".dvpreset", state.userName.c_str(),
            [&state](std::ostream& output) { output << duskverb::encodeState(state) << '\n'; });
        if (!saved) return false;
        state.userName = saved.name;
        recallSnapshot(state);
        currentUserPath_ = saved.path;
        scanUserPresets();
        return true;
    }

    void loadUserPreset(const UserPreset& preset)
    {
        std::ifstream input(preset.path, std::ios::binary);
        if (!input) { presetError_ = "Could not open this preset."; return; }
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        duskverb::StateValues state;
        const bool imported = std::filesystem::path(preset.path).extension() == ".xml";
        if (!(imported ? duskverb::decodeJucePreset(text, state)
                       : duskverb::decodeUserPreset(text, state)))
        {
            presetError_ = "This preset is incomplete or invalid. Current settings were kept.";
            return;
        }
        if (state.userName.empty()) state.userName = preset.name;
        state.edited = false;
        recallSnapshot(state);
        currentUserPath_ = preset.path;
        presetError_.clear();
    }

    // Both dialogs are stock ImGui widgets, so they have to be told about the
    // editor's scale explicitly: ImGui's defaults are 13 px text and 4 px
    // padding in PHYSICAL pixels, which reads as a postage stamp on a 2x window
    // and is the one place a Dusk panel would otherwise show an unscaled,
    // uncrisp face.
    int pushModalStyle()
    {
        modalFont_ = panel_.pickFont(kComboPx * s_);
        if (modalFont_ != nullptr) ImGui::PushFont(modalFont_);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * s_, 14.0f * s_));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f * s_, 6.0f * s_));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f * s_, 9.0f * s_));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * s_);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * s_);
        // Counted rather than written out as a literal: a hand-maintained count
        // that drifts by one leaks a style push EVERY frame (both dialogs are
        // submitted unconditionally), and ImGui answers that with a "Missing
        // PopStyleColor()" error window painted over the editor.
        int colors = 0;
        const auto color = [&colors](ImGuiCol index, ImU32 value)
        { ImGui::PushStyleColor(index, value); ++colors; };
        color(ImGuiCol_PopupBg, IM_COL32(0x14, 0x19, 0x22, 252));
        color(ImGuiCol_TitleBg, IM_COL32(0x14, 0x19, 0x22, 255));
        color(ImGuiCol_TitleBgActive, IM_COL32(0x1c, 0x24, 0x2e, 255));
        color(ImGuiCol_ModalWindowDimBg, IM_COL32(8, 11, 16, 190));
        color(ImGuiCol_Text, kText);
        color(ImGuiCol_Border, fade(kAccent, 0.35f));
        color(ImGuiCol_FrameBg, IM_COL32(0x1d, 0x23, 0x2d, 255));
        color(ImGuiCol_FrameBgHovered, IM_COL32(0x25, 0x2c, 0x38, 255));
        color(ImGuiCol_FrameBgActive, IM_COL32(0x2a, 0x32, 0x3f, 255));
        color(ImGuiCol_Button, IM_COL32(0x2c, 0x55, 0x3a, 255));
        color(ImGuiCol_ButtonHovered, IM_COL32(0x3c, 0x73, 0x4e, 255));
        color(ImGuiCol_ButtonActive, IM_COL32(0x24, 0x45, 0x30, 255));
        return colors;
    }

    void popModalStyle(int colors)
    {
        ImGui::PopStyleColor(colors);
        ImGui::PopStyleVar(5);
        if (modalFont_ != nullptr) { ImGui::PopFont(); modalFont_ = nullptr; }
    }

    void drawSaveModal()
    {
        const int colors = pushModalStyle();
        if (ImGui::BeginPopupModal("Save DuskVerb Preset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool appearing = ImGui::IsWindowAppearing();
            if (appearing) { saveFailed_ = false; overwriteAccepted_ = false; }
            ImGui::TextUnformatted("Preset name");
            ImGui::SetNextItemWidth(260.0f * s_);
            if (appearing) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##dv_savename", saveBuf_, sizeof(saveBuf_),
                                                ImGuiInputTextFlags_EnterReturnsTrue
                                                | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemEdited()) overwriteAccepted_ = false;
            const std::string name = duskdaf::normaliseUserPresetName(saveBuf_);
            const bool exists = std::any_of(userPresets_.begin(), userPresets_.end(),
                [&name](const UserPreset& preset) { return preset.name == name; });
            if (exists)
                ImGui::Checkbox("Replace the existing preset", &overwriteAccepted_);
            ImGui::BeginDisabled(name.empty() || (exists && !overwriteAccepted_));
            const bool save = ImGui::Button(exists ? "Replace" : "Save")
                || (enter && !name.empty() && (!exists || overwriteAccepted_));
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel");
            if (save && saveBuf_[0] != '\0')
            {
                // Only dismiss on a save that actually wrote a file: a failed
                // write that closed the dialog would read as success.
                if (saveUserPreset(saveBuf_)) { saveFailed_ = false; ImGui::CloseCurrentPopup(); }
                else                          { saveFailed_ = true; }
            }
            if (saveFailed_)
                ImGui::TextColored(ImVec4(0.90f, 0.42f, 0.35f, 1.0f),
                                   "Could not save. Try a different name.");
            if (cancel) { saveFailed_ = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        popModalStyle(colors);
    }

    void drawDeleteModal()
    {
        // Opened a frame late on purpose: the request comes from inside the
        // preset combo's popup, and opening a modal from there would be nested
        // inside a popup ImGui is about to close.
        if (deleteRequested_)
        {
            deleteRequested_ = false;
            deleteFailed_ = false;
            ImGui::OpenPopup("Delete DuskVerb Preset");
        }
        const int colors = pushModalStyle();
        if (ImGui::BeginPopupModal("Delete DuskVerb Preset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete \"%s\"?", deleteName_.c_str());
            if (ImGui::Button("Delete"))
            {
                std::error_code error;
                std::filesystem::remove(deletePath_, error);
                deleteFailed_ = bool(error);
                if (!error && currentUserPath_ == deletePath_)
                {
                    // Deleting a library file does not erase the active sound's identity.
                    currentUserPath_.clear();
                }
                if (!error) { scanUserPresets(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            if (deleteFailed_) ImGui::TextUnformatted("Could not delete the preset file.");
            ImGui::EndPopup();
        }
        popModalStyle(colors);
    }

    void drawPresetErrorModal()
    {
        if (!presetError_.empty() && !ImGui::IsPopupOpen("Preset error"))
            ImGui::OpenPopup("Preset error");
        const int colors = pushModalStyle();
        if (ImGui::BeginPopupModal("Preset error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(presetError_.c_str());
            if (ImGui::Button("OK"))
            {
                presetError_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        popModalStyle(colors);
    }

    //======================================================================
    // rails
    //======================================================================
    void drawRails(ImDrawList* dl)
    {
        const float dt = ImGui::GetIO().DeltaTime;
        const void* instance = getPluginInstancePointer();
        // Null-check every bridge symbol: a split UI links without the DSP, and
        // a null bridge must draw idle meters rather than crash.
        const float inL  = duskVerbGetInputLevelL  != nullptr ? duskVerbGetInputLevelL((void*)instance)  : -100.0f;
        const float inR  = duskVerbGetInputLevelR  != nullptr ? duskVerbGetInputLevelR((void*)instance)  : -100.0f;
        const float outL = duskVerbGetOutputLevelL != nullptr ? duskVerbGetOutputLevelL((void*)instance) : -100.0f;
        const float outR = duskVerbGetOutputLevelR != nullptr ? duskVerbGetOutputLevelR((void*)instance) : -100.0f;
        inMeter_.update(inL, inR, dt, meterStyle_);
        outMeter_.update(outL, outR, dt, meterStyle_);

        drawRail(dl, kRailLX0, kRailLX1, "IN", inMeter_);
        drawRail(dl, kRailRX0, kRailRX1, "OUT", outMeter_);
    }

    void drawRail(ImDrawList* dl, float x0, float x1, const char* title,
                  const duskdaf::LedLadder& meter) const
    {
        const float y0 = kRow1Y0, y1 = kRow4Y1;
        card(dl, x0, y0, x1, y1, nullptr);
        duskdaf::spacedText(panel_, dl, 0.5f * (x0 + x1), y0 + 5.0f, 14.0f, kText, title, 0, 0.08f);
        meter.draw(panel_, dl, x0 + 6.0f, y0 + 28.0f, x1 - 6.0f, y1 - 26.0f, meterStyle_);

        char buf[16];
        const float peak = meter.peakDb();
        if (peak <= meterStyle_.minDb + 0.5f) std::snprintf(buf, sizeof(buf), "-inf");
        else                                  std::snprintf(buf, sizeof(buf), "%.0f", (double)peak);
        valueText(dl, 0.5f * (x0 + x1), y1 - 20.0f, kMeterPx,
                  peak > -1.0f ? IM_COL32(0xf8, 0x71, 0x71, 255) : kValueInk, buf, 0);
    }

    //======================================================================
    // Central output history and CPU readback; this is not a measured RT60.
    //======================================================================
    void drawEnvelopeCard(ImDrawList* dl)
    {
        card(dl, kTimeX0, kRow1Y0, kTimeX1, kRow1Y1, nullptr);

        // The title and CPU readout sit above the live history trace.
        const float px0 = kTimeX0 + 12.0f, px1 = kTimeX1 - 12.0f;
        const float py0 = kRow1Y0 + 36.0f, py1 = kRow1Y1 - 6.0f;

        // Faint grid, proportional to the mockup's 50 x 25 px at 1500 wide.
        for (float x = px0 + 35.0f; x < px1; x += 35.0f)
            dl->AddLine(P(x, py0), P(x, py1), IM_COL32(0x1a, 0x20, 0x2a, 255), 1.0f);
        for (float y = py0 + 15.0f; y < py1; y += 15.0f)
            dl->AddLine(P(px0, y), P(px1, y), IM_COL32(0x1a, 0x20, 0x2a, 255), 1.0f);

        // The DSP samples output peak dB at a fixed 15 Hz cadence, independent
        // of audio block size and editor frame rate.
        int frames = 0;
        if (duskVerbGetTailHistory != nullptr)
            frames = duskVerbGetTailHistory(getPluginInstancePointer(), tail_, kTailFrames);
        if (frames < 2) frames = 0;

        // Same dB -> y curve as the JUCE TailMeter: -60..0 dB with a 1.6 power
        // so the interesting -40..0 band gets most of the height.
        const auto dbToY = [&](float db)
        {
            const float clamped = std::clamp(db, -60.0f, 0.0f);
            return py1 - std::pow((clamped + 60.0f) / 60.0f, 1.6f) * (py1 - py0);
        };

        if (frames >= 2)
        {
            const float step = (px1 - px0) / (float)(frames - 1);
            for (int i = 0; i < frames - 1; ++i)
            {
                const float xa = px0 + step * (float)i, xb = xa + step;
                const float ya = dbToY(tail_[i]), yb = dbToY(tail_[i + 1]);
                dl->AddQuadFilled(P(xa, ya), P(xb, yb), P(xb, py1), P(xa, py1),
                                  fade(kAccent, 0.22f));
            }
            dl->PathClear();
            for (int i = 0; i < frames; ++i)
                dl->PathLineTo(P(px0 + step * (float)i, dbToY(tail_[i])));
            dl->PathStroke(kAccent, 0, 1.4f * s_);
        }

        duskdaf::spacedText(panel_, dl, kTimeX0 + 14.0f, kRow1Y0 + 11.0f, 16.0f, kLabelInk,
                            "OUTPUT HISTORY", -1, 0.12f);
        const float cpu = duskVerbGetCpuLoad != nullptr
                        ? duskVerbGetCpuLoad(getPluginInstancePointer()) : 0.0f;
        char cpuText[32];
        std::snprintf(cpuText, sizeof(cpuText), "CPU %.1f%%", (double)std::clamp(cpu * 100.0f, 0.0f, 999.0f));
        valueText(dl, kTimeX1 - 14.0f, kRow1Y0 + 11.0f, 16.0f, kAccent, cpuText, 1);
    }

    void drawEngineCombo(ImDrawList* dl)
    {
        const int current = std::clamp((int)std::lround(values_[duskverb::Algorithm]),
                                       0, getNumAlgorithms() - 1);
        duskdaf::spacedText(panel_, dl, 660.0f, 24.0f, kLabelPx, kDim,
                            "ENGINE", 1, 0.14f);
        if (beginCombo("##dv_engine", 674, 16, 813, 46,
                       getAlgorithmConfig(current).name, kComboPx))
        {
            for (int i = 0; i < getNumAlgorithms(); ++i)
            {
                // Curated roster: only the engines a preset actually uses are
                // offered. A hidden engine restored from saved state or set by
                // automation still appears, so the selection stays recoverable
                // and the box never reads blank.
                if (!getAlgorithmConfig(i).visible && i != current)
                    continue;
                if (ImGui::Selectable(getAlgorithmConfig(i).name, i == current))
                {
                    setP(duskverb::Algorithm, (float)i);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        endCombo();
    }

    //======================================================================
    // row 2
    //======================================================================
    void drawInputCard(ImDrawList* dl)
    {
        card(dl, kInputX0, kRow2Y0, kInputX1, kRow2Y1, "INPUT");
        knobCell(dl, duskverb::Predelay, 150.0f, kRow2Cy, kKnobR, "PRE-DELAY");
        knobCell(dl, duskverb::Saturation, 342.0f, kRow2Cy, kKnobR, "SATURATION");

        const int sync = std::clamp((int)std::lround(values_[duskverb::PredelaySync]), 0, 6);
        dl->AddRectFilled(P(68, 237), P(424, 263), kStripBg, 4.0f * s_);
        duskdaf::spacedText(panel_, dl, 78, 243, 16, sync != 0 ? kAccent : kLabelInk,
                            sync != 0 ? "SYNC / FREE VALUE RETAINED" : "PRE-DELAY SYNC", -1, 0.06f);
        if (beginCombo("##dv_sync", 308, 236, 424, 264,
                       duskverb::kPreDelaySyncLabels[sync], kComboPx))
        {
            for (int i = 0; i < 7; ++i)
                if (ImGui::Selectable(duskverb::kPreDelaySyncLabels[i], i == sync))
                {
                    setP(duskverb::PredelaySync, (float)i);
                    ImGui::CloseCurrentPopup();
                }
        }
        endCombo();
    }

    void drawFilterCard(ImDrawList* dl)
    {
        card(dl, kFiltX0, 282, kFiltX1, 444, "FILTER");
        knobCell(dl, duskverb::LoCut, 104, 394, kKnobSmallR, "LOW CUT");
        knobCell(dl, duskverb::HiCut, 200, 394, kKnobSmallR, "HIGH CUT");
        knobCell(dl, duskverb::MonoBelow, 296, 394, kKnobSmallR, "MONO BELOW", 18, 22, 0.03f);
        knobCell(dl, duskverb::MonoBelowDepth, 392, 394, kKnobSmallR, "MONO DEPTH", 18, 22, 0.03f);
    }

    void drawOutputCard(ImDrawList* dl)
    {
        card(dl, kOutX0, kRow2Y0, kOutX1, kRow2Y1, "OUTPUT");
        knobCell(dl, duskverb::Mix, 866.0f, kRow2Cy, kKnobR, "DRY / WET");
        knobCell(dl, duskverb::Width, 976.0f, kRow2Cy, kKnobR, "WIDTH");
        knobCell(dl, duskverb::GainTrim, 1086.0f, kRow2Cy, kKnobR, "TRIM");
        switchStrip(dl, "##dv_bus", duskverb::BusMode, kOutX0 + 16.0f, 238.0f,
                    kOutX1 - 16.0f, 263.0f, values_[duskverb::BusMode] > 0.5f ? "BUS / 100% WET" : "BUS");
    }

    //======================================================================
    // row 3
    //======================================================================
    void drawDampingCard(ImDrawList* dl)
    {
        card(dl, kDampX0, kRow3Y0, kDampX1, kRow3Y1, "DAMPING");
        // Five evenly spaced controls share the full-width damping section.
        const float lp = kLabelSmallPx, vp = kValueSmallPx, tr = 0.07f;
        knobCell(dl, duskverb::BassMult,      160.0f, kRow3Cy, kKnobDampR, "BASS MULT",   lp, vp, tr);
        knobCell(dl, duskverb::MidMult,       315.0f, kRow3Cy, kKnobDampR, labelFor(currentEngine(), duskverb::MidMult, "MID MULT"), lp, vp, tr);
        knobCell(dl, duskverb::Damping,       470.0f, kRow3Cy, kKnobDampR, "TREBLE MULT", lp, vp, tr);
        knobCell(dl, duskverb::Crossover,     625.0f, kRow3Cy, kKnobDampR, "LOW XOVER",   lp, vp, tr);
        knobCell(dl, duskverb::HighCrossover, 780.0f, kRow3Cy, kKnobDampR, "HIGH XOVER",  lp, vp, tr);

        // Only the Hall engines run the octave feedback network this corrects,
        // so it is hidden everywhere else rather than shown as a dead switch.
        if (tonalCorrectionApplies())
            switchStrip(dl, "##dv_tonal", duskverb::TonalCorrection,
                        885.0f, 532.0f, 1128.0f, 570.0f, "TONAL CORRECTION");
    }

    void drawTimeCard(ImDrawList* dl)
    {
        card(dl, kTimeX0, 201, kTimeX1, 444, nullptr);

        duskdaf::spacedText(panel_, dl, kHeroCx, kHeroCy - kHeroR - 39.0f, kLabelPx + 1.0f,
                            kValueInk, "DECAY", 0, 0.18f);
        const duskverb::ParamDesc& d = duskverb::paramDesc(duskverb::Decay);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        const float t = std::clamp((values_[duskverb::Decay] - lo) / (hi - lo), 0.0f, 1.0f);
        knobArt(dl, kHeroCx, kHeroCy, kHeroR, t, true);
        char decaySeed[48];
        formatPlainValue(currentEngine(), duskverb::Decay, plainOf(duskverb::Decay), decaySeed, sizeof(decaySeed));
        ImGui::SetCursorScreenPos(P(kHeroCx - 78.0f, kHeroCy + kHeroR + 15.0f));
        ImGui::InvisibleButton("##decay_value_entry", ImVec2(156.0f * s_, 40.0f * s_));
        if (ImGui::IsItemClicked()) panel_.openValueEdit("##dvk_decay", values_[duskverb::Decay], decaySeed);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        panel_.knob("##dvk_decay", (uint32_t)duskverb::Decay, lo, hi, kHeroCx, kHeroCy, kHeroR,
                    values_[duskverb::Decay], duskverb::knobDefault(d),
                    false, false, "%.2f", "", 0, true, false, tooltipFor(currentEngine(), duskverb::Decay),
                    false, 1.0f, 0.0f, d.name, true, decaySeed, true, 0.0f, 0.0f,
                    false, false, 9.5f, false, false,
                    &knobToPlainCb, &plainToKnobCb, this, false, &parseKnobText);

        char number[24], unit[8];
        formatDecayParts(plainOf(duskverb::Decay), number, sizeof(number), unit, sizeof(unit));
        const float numW  = textWidth(valueFonts_, kHeroValuePx, number);
        ImFont* small = panel_.pickFont(kHeroUnitPx * s_);
        const float unitW = small->CalcTextSizeA(kHeroUnitPx * s_, FLT_MAX, 0.0f, unit).x / s_;
        const float totalW = numW + 6.0f + unitW;
        const float valueY = kHeroCy + kHeroR + 20.0f;
        valueText(dl, kHeroCx - totalW * 0.5f, valueY, kHeroValuePx, kAccent, number, -1);
        panel_.text(dl, kHeroCx - totalW * 0.5f + numW + 6.0f,
                    valueY + kHeroValuePx - kHeroUnitPx - 3.0f,
                    kHeroUnitPx, fade(kAccent, 0.78f), unit, -1, true);

        knobCell(dl, duskverb::Size, 710.0f, 318.0f, kKnobR, "SIZE");

        // FREEZE owns the bottom strip; on the Gated engine it shares the row
        // with the gate switch, which is dead on every other engine and so is
        // not shown there.
        if (gateApplies())
        {
            switchStrip(dl, "##dv_freeze", duskverb::Freeze, 657.0f, 372.0f, 772.0f, 401.0f, "FREEZE");
            switchStrip(dl, "##dv_gate", duskverb::GateEnabled, 657.0f, 407.0f, 772.0f, 436.0f, "GATE");
        }
        else
        {
            switchStrip(dl, "##dv_freeze", duskverb::Freeze, 657.0f, 386.0f, 772.0f, 421.0f, "FREEZE");
        }
    }

    void drawEarlyCard(ImDrawList* dl)
    {
        card(dl, kErX0, 282, kErX1, 444, "EARLY REFLECTIONS");
        knobCell(dl, duskverb::ErLevel,   866.0f, 402.0f, kKnobErR, "ER LEVEL");
        knobCell(dl, duskverb::ErSize,    976.0f, 402.0f, kKnobErR, "ER SIZE");
        knobCell(dl, duskverb::Diffusion, 1086.0f, 402.0f, kKnobErR, labelFor(currentEngine(), duskverb::Diffusion, "DIFFUSION"));
    }

    //======================================================================
    // row 4
    //======================================================================
    void drawModulationCard(ImDrawList* dl)
    {
        // Re-titled GATE on the Gated engine, where these two knobs are the
        // gate's ATTACK and RELEASE (the JUCE editor does the same).
        card(dl, kModX0, kRow4Y0, kModX1, kRow4Y1, gateApplies() ? "GATE" : "MODULATION");
        knobCell(dl, duskverb::ModDepth, 172.0f, kRow4Cy, 30.0f, labelFor(currentEngine(), duskverb::ModDepth, "DEPTH"));
        knobCell(dl, duskverb::ModRate,  340.0f, kRow4Cy, 30.0f, labelFor(currentEngine(), duskverb::ModRate, "RATE"));
    }

    void drawMacroCard(ImDrawList* dl)
    {
        card(dl, kMacroX0, kRow4Y0, kMacroX1, kRow4Y1, "MACRO");
        knobCell(dl, duskverb::Tone,      636.0f, kRow4Cy, 30.0f, "TONE");
        knobCell(dl, duskverb::Character, 819.0f, kRow4Cy, 30.0f, "CHARACTER");
        knobCell(dl, duskverb::Duck,      1002.0f, kRow4Cy, 30.0f, "DUCK");
    }

    //======================================================================
    // help overlay
    //======================================================================
    void handleKeyboard()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
            return;
        if (showHelp_)
        {
            // Any KEYBOARD key dismisses. The scan stops at ImGuiKey_GamepadStart
            // because everything from there to ImGuiKey_NamedKey_END is gamepad,
            // mouse buttons and the reserved modifier slots — and mouse buttons
            // being in that range is not a detail: the '?' button opens the
            // overlay on ImGuiKey_MouseLeft's own press frame, so scanning the
            // whole range made the overlay close in the frame it opened, and the
            // button looked dead. The scrim click already handles dismissal by
            // mouse (see the blocker in onImGuiDisplay).
            for (ImGuiKey k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart;
                 k = (ImGuiKey)(k + 1))
                if (ImGui::IsKeyPressed(k, false)) { showHelp_ = false; break; }
            return;
        }
        if (showSupporters_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            showSupporters_ = false;
            return;
        }
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Slash, false))
            showHelp_ = true;
    }

    void drawHelpOverlay(ImDrawList* dl, float winW, float winH)
    {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), IM_COL32(8, 11, 16, 228));
        // No input blocker here: it is submitted with the other modal blockers
        // before the controls, because an InvisibleButton submitted at this
        // point loses ImGui's hover race to the already-submitted knobs.

        const float px0 = 230.0f, py0 = 96.0f, px1 = 970.0f, py1 = 716.0f;
        const float cx = 0.5f * (px0 + px1);
        dl->AddRectFilled(P(px0, py0), P(px1, py1), IM_COL32(0x14, 0x19, 0x22, 246), 10.0f * s_);
        dl->AddRect(P(px0, py0), P(px1, py1), fade(kAccent, 0.45f), 10.0f * s_, 0, 1.2f * s_);
        duskdaf::spacedText(panel_, dl, cx, py0 + 16.0f, 24.0f, kValueInk, "DUSKVERB 2", 0, 0.20f);

        struct Row { const char* key; const char* text; };
        static const Row left[] = {
            { "Drag a knob",      "Adjust" },
            { "Shift + drag",     "Fine adjust" },
           #if defined(__APPLE__)
            { "Alt / Cmd click", "Reset to default" },
           #else
            { "Ctrl / Alt click", "Reset to default" },
           #endif
            { "Double-click",     "Type a value" },
            { "Right-click",      "Reset / type menu" },
            { "Mouse wheel",      "Step the value" },
            { "Drag the corner",  "Resize the editor" },
        };
        static const Row right[] = {
            { "< / >",         "Previous / next preset" },
            { "INIT",          "Everything back to defaults" },
            { "SAVE",          "Store a user preset" },
            { "Right-click a user preset", "Delete it" },
            { "A / B",         "Switch complete sound snapshots" },
            { "COPY",          "Copy active sound to the other slot" },
            { "Click DUSKVERB", "Supporters" },
            { "?  or Shift + /", "Show or hide this" },
        };
        const float top = py0 + 56.0f, lineH = 26.0f;
        for (int i = 0; i < (int)(sizeof(left) / sizeof(left[0])); ++i)
        {
            panel_.text(dl, cx - 24.0f, top + (float)i * lineH, 18.0f, kAccent, left[i].key, 1, true);
            valueText(dl, cx - 14.0f, top + (float)i * lineH, kComboPx, kLabelInk, left[i].text, -1);
        }
        const float top2 = top + 8.0f * lineH;
        for (int i = 0; i < (int)(sizeof(right) / sizeof(right[0])); ++i)
        {
            panel_.text(dl, cx - 24.0f, top2 + (float)i * lineH, 18.0f, kAccent, right[i].key, 1, true);
            valueText(dl, cx - 14.0f, top2 + (float)i * lineH, kComboPx, kLabelInk, right[i].text, -1);
        }

        valueText(dl, cx, py1 - 62.0f, kComboPx, kDim,
                  "GATE appears on the Gated engine; TONAL CORRECTION on the Hall engines.", 0);
        valueText(dl, cx, py1 - 44.0f, kComboPx, kDim,
                  "MONO DEPTH is always visible in FILTER. COPY preserves the active slot.", 0);
        valueText(dl, cx, py1 - 24.0f, 14.0f, kDim,
                  "Press any key to dismiss", 0);
    }

    //======================================================================
    // state
    //======================================================================
    static constexpr int kTailFrames = 256;   // duskverb::DuskVerbDSP::kTailHistorySize

    float  values_[duskverb::kNumParams] = {};
    float  tail_[kTailFrames] = {};

    duskdaf::DuskPanel panel_;
    duskdaf::CrispFontSet labelFonts_, valueFonts_;
    GLint maxTextureSize_ = 0;
    float bakedScale_ = 0.0f, lastSeenScale_ = 0.0f;
    ImFont* labelFont_ = nullptr;
    ImFont* comboFont_ = nullptr;
    ImFont* modalFont_ = nullptr;
    bool    comboOpen_ = false;
    duskdaf::DuskImGuiTextInputFocus textInputFocus_;
    duskdaf::SupportersOverlay supporters_;

    duskdaf::LedLadderStyle meterStyle_ = [] {
        duskdaf::LedLadderStyle st;
        // Full-body rails extend beside Modulation/Macro. A finer ladder keeps
        // the long meters readable while the colour zones stay
        // at the same FRACTIONS of the scale (see DuskLedMeter.hpp).
        st.segments = 24;
        st.gap = 2.0f;
        st.rounding = 1.5f;
        return st;
    }();
    duskdaf::LedLadder inMeter_, outMeter_;

    float  s_ = 1.0f;
    ImVec2 org_ = ImVec2(0, 0);
    bool   gripCursorSet_ = false;

    int         currentPreset_ = -1;
    int         lastBridgeProgram_ = -2;
    std::string currentUserName_, currentUserPath_;
    std::vector<UserPreset> userPresets_;
    char        saveBuf_[64] = {};
    bool        saveFailed_ = false;
    bool        deleteRequested_ = false;
    bool        presetModified_ = false;
    std::string presetError_;
    bool importRequested_ = false;
    bool overwriteAccepted_ = false;
    bool deleteFailed_ = false;
    char        previewBuf_[96] = {};
    std::string deleteName_, deletePath_;

    // A/B
    duskverb::EditorState fallbackEditorState_;
    uint64_t lastStateRevision_ = 0;
    int         activeSlot_ = 0;
    float       copiedFlash_ = 0.0f;

    bool showSupporters_ = false;
    bool showHelp_ = false;

    DAF_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuskVerbUI)
};

UI* createUI() { return new DuskVerbUI(); }

END_NAMESPACE_DAF
