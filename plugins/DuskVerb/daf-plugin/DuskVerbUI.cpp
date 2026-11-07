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
// Layout, top to bottom, every row the full body width:
//   * the fleet header: nameplate, preset browser, INIT/SAVE, A/B, COPY, help
//   * IN and OUT LED rails down the full body height
//   * SPACE (engine, pre-delay, size, diffusion) | DECAY | OUTPUT (width, trim, mix)
//   * TONE: the decay-by-frequency graph over the low cut, damping, high cut and
//     tone knobs, in frequency order
//   * EARLY | the engine card (modulation, or the engine's own controls) |
//     BASS MONO | MACRO
// Signal order reads left to right: pre-delay sits beside the IN meter, mix
// beside the OUT meter.
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
#include "DuskVerbToneGraph.hpp"
#include "DuskVerbVersion.hpp"

#include "../core/DuskVerbParamTable.hpp"   // paramDesc / host<->plain / factory presets
#include "../core/DuskVerbKnobLiveness.hpp" // which controls the engine hears
#include "../core/DuskVerbToneMacro.hpp"    // the Tone tilt, for the graph's ghost
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
    constexpr ImU32 kLabelInk   = IM_COL32(225, 229, 235, 255);
    constexpr ImU32 kValueInk   = IM_COL32(220, 246, 139, 255);

    constexpr ImU32 kGridLine   = IM_COL32(31, 43, 54, 255);
    constexpr ImU32 kGridLineHi = IM_COL32(46, 61, 74, 255);
    constexpr ImU32 kGridInk    = IM_COL32(112, 128, 142, 255);
    constexpr ImU32 kCutShade   = IM_COL32(4, 7, 10, 150);

    // A knob the selected engine does not use (see DuskVerbKnobLiveness.hpp).
    constexpr float kUnusedKnobAlpha = 0.38f;
    constexpr const char* kUnusedKnobTooltip = "Not used by this engine";
    // A knob another control overrides (MIX under BUS, PRE-DELAY under SYNC).
    constexpr float kOverriddenAlpha = 0.5f;

    // Larger native font sizes preserve readability at the minimum window.
    constexpr float kLabelPx = 18.0f, kValuePx = 22.0f;
    constexpr float kComboPx = 18.0f, kCaptionPx = 16.0f;
    constexpr float kHeroValuePx = 30.0f, kHeroUnitPx = 18.0f;
    constexpr float kMeterPx = 14.0f, kAxisPx = 13.0f;
    constexpr float kPi = 3.14159265358979f;

    // The fleet header: Multi-Comp 2's band and controls at this canvas's scale
    // (1200 drawn at 1050, so x1.14), which makes it match the fleet at the
    // default size and keeps its buttons 25 px tall at the minimum.
    constexpr float kHeaderH = 55.0f;
    constexpr float kHdrY0 = 13.0f, kHdrY1 = 42.0f;

    // The body: IN and OUT rails down both sides, three rows between them, each
    // the full body width. The primary row's cards are equal thirds, so the hero
    // sits on the canvas centre line.
    constexpr float kRailLX0 = 12.0f, kRailLX1 = 42.0f;
    constexpr float kRailRX0 = 1158.0f, kRailRX1 = 1188.0f;
    constexpr float kBodyX0 = 52.0f, kBodyX1 = 1148.0f, kBodyY0 = 66.0f, kBodyY1 = 788.0f;
    constexpr float kGutter = 12.0f;
    constexpr float kRow1Y1 = 336.0f;
    constexpr float kToneY0 = 348.0f, kToneY1 = 618.0f;
    constexpr float kDetailY0 = 630.0f;
    constexpr float kSpaceX1 = 409.0f, kOutputX0 = 791.0f;   // DECAY sits between

    // Three knob sizes: the hero, the primary row, everything else. At the 1050 x
    // 700 minimum they are 115.5, 63 and 52.5 px across.
    constexpr float kHeroR = 66.0f, kPrimaryR = 36.0f, kKnobR = 30.0f;
    constexpr float kHeroCx = 600.0f, kHeroCy = 182.0f;
    constexpr float kPrimaryCy = 244.0f, kToneKnobCy = 570.0f, kDetailCy = 742.0f;
    constexpr float kLabelDy = 48.0f, kValueDy = 29.0f;   // above a knob's top edge

    // The TONE graph's plot area; its decay scale is labelled in the margin to
    // its left, clear of the Low Cut shading.
    constexpr float kGraphX0 = 96.0f, kGraphY0 = 386.0f, kGraphX1 = 1128.0f, kGraphY1 = 484.0f;

    // The x of slot `i` of `count` equal slots between x0 and x1.
    constexpr float slotX(float x0, float x1, int count, int i)
    {
        return x0 + ((float)i + 0.5f) * (x1 - x0) / (float)count;
    }

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
                return "Balance between dry input and reverb. Use BUS mode for send/return.";
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
                return "Bus mode - outputs 100% wet signal regardless of MIX.\n"
                       "Use on a send/return aux; MIX LOCK keeps it on while you change presets.";
            case duskverb::PredelaySync:
                return "Tempo-sync pre-delay to the host's BPM.\n"
                       "When set to a note value, overrides the PRE-DELAY knob.";
            case duskverb::Algorithm:
                return "Reverb space. Switching here changes the DSP\n"
                       "without overwriting your knob values.";
            case duskverb::TonalCorrection:
                return "Keeps the tail's overall tone even when bass and treble\n"
                       "decay at different rates. Off is the preset as voiced.";
            default:
                return duskverb::paramDesc(p).name;
        }
    }

    // DuskPanel::knob gesture domain (the knob domain: JUCE-skew normalised for
    // tapered parameters, plain otherwise) -> printed value, and back for typed
    // entry. The printed value is what the DSP receives for that knob position
    // (knob -> plain host value -> the JUCE quantisation chain), so the read-out
    // cannot disagree with the sound.
    ImU32 fade(ImU32 c, float amount) { return duskdaf::scaleAlpha(c, amount); }

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
        markPresetModified((int)idx);
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
        const auto& presets = duskverb::dv2FactoryPresets();
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
        presetModified_ = false;
    }

    // MIX LOCK arrives here when the editor opens and when a session restores it.
    void stateChanged(const char* key, const char* value) override
    {
        if (key != nullptr && value != nullptr && std::strcmp(key, "mix_lock") == 0)
            mixLocked_ = std::strcmp(value, "1") == 0;
    }

    // Every design size the panel draws, per weight, so that at any scale the
    // requested pixel size lands on a baked face exactly (CrispFontSet::pick
    // then scales by 1.0). Sizes are design px; bakeFonts multiplies by scale.
    static constexpr float kLabelDesignSizes[] =
        { 13.0f, 14.0f, kCaptionPx, kLabelPx, 20.0f, 23.0f, 24.0f, 25.0f, 27.0f };
    static constexpr float kValueDesignSizes[] =
        { kAxisPx, kMeterPx, kCaptionPx, kHeroUnitPx, kValuePx, kHeroValuePx };

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
        drawSpaceCard(dl);
        drawDecayCard(dl);
        drawOutputCard(dl);
        drawToneCard(dl);
        drawDetailRow(dl);
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

    // Design px from a screen position (the pointer, for the TONE graph).
    ImVec2 toDesign(ImVec2 screen) const
    {
        return ImVec2((screen.x - org_.x) / s_, (screen.y - org_.y) / s_);
    }

    // The line top that puts the capitals of `size` design-px text centred on `cy`.
    // ImGui lays a line out from its top, ascender room included, so centring the
    // line (or guessing an offset) sits capitals low in a strip or a button; this
    // centres the face's own 'H' instead. `face` selects the value face.
    float capsTop(float cy, float size, const duskdaf::CrispFontSet* face = nullptr) const
    {
        ImFont* font = face != nullptr ? face->pick(size * s_) : panel_.pickFont(size * s_);
        const ImFontGlyph* h = font != nullptr ? font->FindGlyph('H') : nullptr;
        if (h == nullptr || font->FontSize <= 0.0f)
            return cy - 0.5f * size;
        return cy - 0.5f * (h->Y0 + h->Y1) * size / font->FontSize;
    }

    // Design px of letter-spaced text (duskdaf::spacedText): each glyph's advance,
    // plus `tracking` x size between glyphs.
    float spacedWidth(const char* text, float size, float tracking) const
    {
        ImFont* font = panel_.pickFont(size * s_);
        float width = 0.0f;
        for (const char* c = text; *c; ++c)
            width += font->CalcTextSizeA(size * s_, FLT_MAX, 0.0f, c, c + 1).x / s_ + (c[1] ? tracking * size : 0.0f);
        return width;
    }

    // The letter spacing a knob name gets: the panel's 0.12, tightened toward
    // none when that would not fit `room` design px (0 = no limit).
    float fittedTracking(const char* name, float room) const
    {
        constexpr float kTracking = 0.12f;
        const int gaps = (int)std::strlen(name) - 1;
        if (room <= 0.0f || gaps <= 0)
            return kTracking;
        const float fit = (room - spacedWidth(name, kLabelPx, 0.0f)) / (kLabelPx * (float)gaps);
        return std::clamp(fit, 0.0f, kTracking);
    }

    // Width of a string in DESIGN px for a given face set and design size, for
    // the few places that lay a label out against a number whose width moves.
    float textWidth(const duskdaf::CrispFontSet& set, float size, const char* txt) const
    {
        const float px = size * s_;
        ImFont* font = set.pick(px);
        if (font == nullptr) font = panel_.pickFont(px);
        return font->CalcTextSizeA(px, FLT_MAX, 0.0f, txt).x / s_;
    }

    // Numeric read-outs use the regular weight. The mockup asks for a monospace
    // face; shared-daf bundles only Barlow Condensed, and pulling in a second
    // family for the digits alone is a new dependency, so the regular weight
    // stands in for it. Digits in this face are uniform width, so a changing
    // value does not jitter a centred read-out.
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
        markPresetModified(p);
        editParameter((uint32_t)p, true);
        setParameterValue((uint32_t)p, duskverb::knobToHost(d, v));
        editParameter((uint32_t)p, false);
    }

    // A parameter a preset, INIT or an A/B swap is allowed to move. Bypass is
    // the host's, never a preset's — recalling a preset must not un-bypass the
    // plugin, and neither must swapping snapshots.
    static bool isRecallable(int p) { return p != duskverb::Bypass; }

    EngineType currentEngine() const
    {
        int idx = (int)std::lround(values_[duskverb::Algorithm]);
        return duskverb::controlEngine(idx);
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

    // A panel edit does NOT throw the preset name away. The JUCE editor kept it,
    // and every DAW marks an edited preset rather than forgetting which one it
    // came from — losing the name also loses the "step to the next one from
    // here" that the chevrons depend on. The name is flagged instead, and the
    // combo and the header print "<name> *" until a recall clears the flag.
    // Under MIX LOCK, Mix and Bus are not part of the sound a preset names, so
    // moving them leaves the name unmarked.
    void markPresetModified(int p)
    {
        if (!(mixLocked_ && duskverb::isMixLockParam(p)))
            presetModified_ = true;
    }

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
        const auto& presets = duskverb::dv2FactoryPresets();
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
        duskverb::collectDv2Parameters(preset, [this](const char* id, float plainValue)
        {
            const int index = duskverb::paramIndexForId(id);
            if (index < 0 || (mixLocked_ && duskverb::isMixLockParam(index))) return;
            values_[index] = duskverb::plainToKnob(duskverb::paramDesc(index), plainValue);
        });
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
            const auto& presets = duskverb::dv2FactoryPresets();
            for (size_t i = 0; i < presets.size(); ++i)
                if (state.presetName == presets[i].name) currentPreset_ = static_cast<int>(i);
        }
        presetModified_ = state.edited;
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
    // A card, its title centred on a rule that runs out to both edges. Nothing
    // else shares the title row, so every section reads the same way.
    void card(ImDrawList* dl, float x0, float y0, float x1, float y1,
              const char* title, bool hero = false, ImU32 titleInk = kText) const
    {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), kCardBg, 9.0f * s_);
        dl->AddRect(P(x0, y0), P(x1, y1), hero ? kCardEdgeHi : kCardEdge, 9.0f * s_);
        if (title == nullptr) return;
        constexpr float kSize = 20.0f, kTracking = 0.14f, kGap = 14.0f, kInset = 18.0f;
        const float cx = 0.5f * (x0 + x1);
        const float half = 0.5f * spacedWidth(title, kSize, kTracking);
        const float ruleY = y0 + 22.0f;
        duskdaf::spacedText(panel_, dl, cx, capsTop(ruleY, kSize), kSize, titleInk, title, 0, kTracking);
        if (cx - half - kGap > x0 + kInset)
        {
            dl->AddLine(P(x0 + kInset, ruleY), P(cx - half - kGap, ruleY), kCardEdgeHi, s_);
            dl->AddLine(P(cx + half + kGap, ruleY), P(x1 - kInset, ruleY), kCardEdgeHi, s_);
        }
    }

    // `alpha` < 1 draws the whole knob faded (a control the engine does not use).
    void knobArt(ImDrawList* dl, float cx, float cy, float r, float t, bool hero,
                 float alpha = 1.0f) const
    {
        const ImVec2 c = P(cx, cy);
        const float R = r * s_;
        const float ringR = R + (hero ? 11.0f : 7.0f) * s_;
        const float ringW = (hero ? 6.0f : 3.2f) * s_;
        const float a0 = arcAngle(0.0f), a1 = arcAngle(1.0f), av = arcAngle(t);

        dl->PathArcTo(c, ringR, a0, a1, 64);
        dl->PathStroke(fade(kRingTrack, alpha), 0, ringW);

        if (hero)   // a soft bloom under the hero arc
        {
            dl->PathArcTo(c, ringR, a0, av, 64);
            dl->PathStroke(fade(kAccent, 0.20f * alpha), 0, ringW + 6.0f * s_);
        }
        if (t > 0.0005f)
        {
            dl->PathArcTo(c, ringR, a0, av, 64);
            dl->PathStroke(fade(kAccent, alpha), 0, ringW);
        }

        // Body: a dark disc lit from the top. Horizontal strips analytically
        // clipped to the circle give a smooth response with no concentric
        // banding, and stay sharp at every scale because it is geometry.
        dl->AddCircleFilled(ImVec2(c.x, c.y + 1.5f * s_), R * 1.02f,
                            fade(IM_COL32(0, 0, 0, 120), alpha), 48);
        const int bands = std::max(14, (int)(2.0f * R));
        for (int i = 0; i <= bands; ++i)
        {
            const float u = -0.999f + 1.998f * (float)i / (float)bands;
            const float half = std::sqrt(std::max(0.0f, 1.0f - u * u)) * R;
            const ImU32 col = blend(kKnobTop, kKnobBottom, 0.5f * (u + 1.0f));
            dl->AddLine(ImVec2(c.x - half, c.y + u * R), ImVec2(c.x + half, c.y + u * R),
                        fade(col, alpha), 1.15f);
        }
        dl->AddCircle(c, R, fade(kKnobRim, alpha), 48, 1.1f * s_);

        const float a = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 dir(std::sin(a), -std::cos(a));
        dl->AddLine(ImVec2(c.x + dir.x * R * 0.34f, c.y + dir.y * R * 0.34f),
                    ImVec2(c.x + dir.x * R * 0.88f, c.y + dir.y * R * 0.88f),
                    fade(kPointer, alpha), (hero ? 3.0f : 2.0f) * s_);
    }

    // A knob another control currently overrides: drawn faded, its read-out names
    // what is in charge (BUS, a sync division), and it still turns, keeping its own
    // value for when the override is lifted.
    struct KnobOverride { const char* readout; const char* tooltip; };

    // One complete knob cell: art, gestures, name and live read-out.
    // The art is drawn BEFORE the gesture call (so a knob never paints over its
    // own type-entry field) and the read-out AFTER it, which is what keeps the
    // number exact during a drag while the pointer lags by a single frame.
    // A cell that is not `live` (the engine has no such control) is drawn faded
    // and says so on hover; it still turns, and its value carries to an engine
    // that reads it.
    // `labelRoom` (design px, 0 = unlimited) tightens the name's letter spacing,
    // never its size, until it fits its slot.
    void knobCell(ImDrawList* dl, int p, float cx, float cy, float r, const char* name,
                  bool live = true, const KnobOverride* over = nullptr, float labelRoom = 0.0f)
    {
        const duskverb::ParamDesc& d = duskverb::paramDesc(p);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        const float t = hi > lo ? std::clamp((values_[p] - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        const float alpha = ! live ? kUnusedKnobAlpha : (over != nullptr ? kOverriddenAlpha : 1.0f);
        knobArt(dl, cx, cy, r, t, /*hero*/ false, alpha);   // the hero is laid out by hand

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
        const char* tip = ! live ? kUnusedKnobTooltip
                        : (over != nullptr ? over->tooltip : tooltipFor(currentEngine(), p));
        panel_.knob(id, (uint32_t)p, lo, hi, cx, cy, r, values_[p], duskverb::knobDefault(d),
                    /*stepped*/ false, /*panelTicks*/ false, "%.2f", "",
                    /*faceColor*/ 0, /*bodyless*/ true, /*persistent*/ false, tip,
                    /*rightClickReset*/ false, 1.0f, 0.0f, d.name,
                    /*contextMenu*/ true, /*overrideText*/ buf,
                    /*hasExternalReadout*/ true, 0.0f, 0.0f,
                    /*nameOnHover*/ false, /*doubleClickReset*/ false, 9.5f,
                    /*bubbleOnActiveOnly*/ false, /*omitCenterTick*/ false,
                    &knobToPlainCb, &plainToKnobCb, this, /*dragInDisplayDomain*/ false, &parseKnobText);

        formatPlainValue(currentEngine(), p, plainOf(p), buf, sizeof(buf));
        if (name != nullptr)
            duskdaf::spacedText(panel_, dl, cx, cy - r - kLabelDy, kLabelPx, fade(kLabelInk, alpha),
                                name, 0, fittedTracking(name, labelRoom));
        valueText(dl, cx, cy - r - kValueDy, kValuePx, fade(kValueInk, alpha),
                  over != nullptr ? over->readout : buf, 0);
    }

    // An empty slot: on this engine the control lives in the engine card. A
    // faint ring under the slot's own label, inert, saying where it went.
    void placeholderCell(ImDrawList* dl, float cx, float cy, float r, const char* name,
                         const char* tooltip, int id)
    {
        const ImVec2 c = P(cx, cy);
        dl->AddCircle(c, (r + 7.0f) * s_, fade(kRingTrack, 0.7f), 48, 2.0f * s_);
        dl->AddLine(P(cx - 9.0f, cy), P(cx + 9.0f, cy), fade(kDim, 0.5f), 2.0f * s_);
        duskdaf::spacedText(panel_, dl, cx, cy - r - kLabelDy, kLabelPx, fade(kLabelInk, kUnusedKnobAlpha),
                            name, 0, 0.12f);
        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(P(cx - r - 6.0f, cy - r - kLabelDy - 3.0f));
        ImGui::InvisibleButton("##placeholder", ImVec2((2.0f * r + 12.0f) * s_, (2.0f * r + kLabelDy + 12.0f) * s_));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltip);
        ImGui::PopID();
    }

    // A control in its home slot, or the placeholder when the engine card has
    // taken it (HOLD and CHIRP are Diffusion, THRESHOLD is Mid Multiply).
    void homeSlot(ImDrawList* dl, int p, float cx, float cy, float r, const char* name)
    {
        const EngineType e = currentEngine();
        if (! inEngineCard(e, p))
        {
            knobCell(dl, p, cx, cy, r, name, liveHere(p));
            return;
        }
        char tip[96];
        std::snprintf(tip, sizeof(tip), "On this engine this control is %s,\nin the %s card.",
                      labelFor(e, p, name), engineCard(e).title);
        placeholderCell(dl, cx, cy, r, name, tip, 1000 + p);
    }

    // Dark strip with a caption and a pill switch at its right end. This is what
    // the mockup uses for FREEZE / BUS / GATE / TONAL CORRECTION.
    // A strip that is not `live` (the engine has no such switch) is drawn faded and
    // says so on hover; like a knob, it still toggles and its state carries over.
    void switchStrip(ImDrawList* dl, const char* id, int p,
                     float x0, float y0, float x1, float y1, const char* caption, bool live = true)
    {
        const bool on = values_[p] > 0.5f;
        if (toggleStrip(dl, id, on, x0, y0, x1, y1, caption,
                        live ? tooltipFor(currentEngine(), p) : kUnusedKnobTooltip, live))
            setP(p, on ? 0.0f : 1.0f);
    }

    // The strip itself, for a parameter or for editor state (MIX LOCK). Returns
    // true on click.
    bool toggleStrip(ImDrawList* dl, const char* id, bool on, float x0, float y0, float x1, float y1,
                     const char* caption, const char* tooltip, bool live = true)
    {
        const float alpha = live ? 1.0f : kUnusedKnobAlpha;
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s_, (y1 - y0) * s_));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltip);

        dl->AddRectFilled(P(x0, y0), P(x1, y1), kStripBg, 5.0f * s_);
        dl->AddRect(P(x0, y0), P(x1, y1), fade(on ? fade(kAccent, 0.55f)
                                                  : (hovered ? kKnobRim : kCardEdge), alpha),
                    5.0f * s_, 0, 1.0f * s_);
        duskdaf::spacedText(panel_, dl, x0 + 12.0f, capsTop(0.5f * (y0 + y1), kLabelPx), kLabelPx,
                            fade(on ? kAccent : kLabelInk, alpha), caption, -1, 0.14f);

        const float cy = 0.5f * (y0 + y1);
        const float tx1 = x1 - 9.0f, tx0 = tx1 - 30.0f, th = 7.0f;
        dl->AddRectFilled(P(tx0, cy - th), P(tx1, cy + th),
                          fade(on ? fade(kAccent, 0.35f) : IM_COL32(0x22, 0x27, 0x31, 255), alpha),
                          th * s_);
        dl->AddCircleFilled(P(on ? tx1 - th : tx0 + th, cy), (th - 1.5f) * s_,
                            fade(on ? kAccent : IM_COL32(0x6a, 0x73, 0x82, 255), alpha), 20);
        return clicked;
    }

    // The fleet's header button and chevron, on the header's control band.
    // Returns true on click.
    bool headerButton(ImDrawList* dl, const char* id, float x0, float x1,
                      const char* caption, bool lit = false)
    {
        ImGui::SetCursorScreenPos(P(x0, kHdrY0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s_, (kHdrY1 - kHdrY0) * s_));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(P(x0, kHdrY0), P(x1, kHdrY1),
                          lit ? IM_COL32(0x2c, 0x55, 0x3a, 255)
                              : (hovered ? IM_COL32(54, 54, 58, 255) : IM_COL32(38, 38, 41, 255)),
                          2.0f * s_);
        dl->AddRect(P(x0, kHdrY0), P(x1, kHdrY1),
                    lit ? kAccent : IM_COL32(90, 90, 94, 255), 2.0f * s_, 0, 1.0f * s_);
        panel_.text(dl, 0.5f * (x0 + x1), capsTop(0.5f * (kHdrY0 + kHdrY1), kCaptionPx), kCaptionPx,
                    lit ? kAccent : (hovered ? kText : kDim), caption, 0, true);
        return ImGui::IsItemClicked();
    }

    bool headerChevron(ImDrawList* dl, const char* id, float cx, bool left)
    {
        const float cy = 0.5f * (kHdrY0 + kHdrY1);
        const ImVec2 b0 = P(cx - 9.5f, kHdrY0), b1 = P(cx + 9.5f, kHdrY1);
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
    // The fleet header (TapeEchoUI.cpp drawHeader is the reference; Multi-Comp 2
    // has the same band at a nearer width): the nameplate, < preset >, INIT and
    // SAVE, this plugin's own buttons, and DUSK AUDIO on the right. The engine
    // menu lives in the body, in SPACE.
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, kHeaderH), IM_COL32(14, 14, 15, 255));
        dl->AddRectFilledMultiColor(P(0, 0), P(kDesignW, 4.5f),
                                    IM_COL32(205, 203, 197, 255), IM_COL32(155, 154, 151, 255),
                                    IM_COL32(91, 91, 91, 255), IM_COL32(118, 118, 116, 255));
        dl->AddLine(P(0, kHeaderH), P(kDesignW, kHeaderH), IM_COL32(91, 90, 88, 255), 1.2f * s_);
        dl->AddLine(P(0, kHeaderH + 1.0f), P(kDesignW, kHeaderH + 1.0f), IM_COL32(0, 0, 0, 160), s_);

        constexpr float plateX0 = 32.0f, plateX1 = 352.0f, plateY0 = 9.0f, plateY1 = 46.0f;
        dl->AddRectFilledMultiColor(P(plateX0, plateY0), P(plateX1, plateY1),
                                    IM_COL32(37, 37, 38, 255), IM_COL32(29, 29, 30, 255),
                                    IM_COL32(16, 16, 17, 255), IM_COL32(19, 19, 20, 255));
        dl->AddRect(P(plateX0, plateY0), P(plateX1, plateY1), IM_COL32(185, 184, 180, 220),
                    4.0f * s_, 0, 1.2f * s_);
        dl->AddLine(P(46, 42), P(338, 42), IM_COL32(116, 145, 75, 210), 1.2f * s_);
        panel_.text(dl, 48, 11, 27, kText, "DUSKVERB", -1, true);
        panel_.text(dl, 262, 16, 23, kText, "DV-2", 0, true);
        panel_.text(dl, 340, 23, 13, kDim, "v" DUSKVERB2_VERSION_STRING, 1, true);
        panel_.text(dl, kDesignW - 34, 14, 25, kText, "DUSK AUDIO", 1, true);

        ImGui::SetCursorScreenPos(P(plateX0, plateY0));
        ImGui::InvisibleButton("##dv_titlecredits", ImVec2((plateX1 - plateX0) * s_, (plateY1 - plateY0) * s_));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) { supporters_.resetInteraction(); showSupporters_ = true; }

        if (headerChevron(dl, "##dv_prev", 378, true)) stepPreset(-1);
        if (headerChevron(dl, "##dv_next", 654, false)) stepPreset(1);
        drawPresetCombo();
        if (headerButton(dl, "##dv_init", 676, 736, "INIT")) initDefaults();
        if (headerButton(dl, "##dv_save", 744, 804, "SAVE"))
        {
            std::snprintf(saveBuf_, sizeof(saveBuf_), "%s", currentUserName_.c_str());
            ImGui::OpenPopup("Save DuskVerb Preset");
        }
        drawSaveModal();
        drawAbButtons(dl);
        if (headerButton(dl, "##dv_help", 976, 1004, "?", showHelp_)) showHelp_ = !showHelp_;
    }

    void drawPresetCombo()
    {
        const auto& presets = duskverb::dv2FactoryPresets();
        if (beginCombo("##dv_presets", 392, kHdrY0, 640, kHdrY1, presetPreview()))
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
            const float x0 = 822.0f + static_cast<float>(slot) * 40.0f;
            char id[16]; std::snprintf(id, sizeof(id), "##dv_ab%d", slot);
            if (headerButton(dl, id, x0, x0 + 34.0f, slot == 0 ? "A" : "B", slot == activeSlot_)
                && slot != activeSlot_) switchToSlot(slot);
        }
        if (headerButton(dl, "##dv_copy", 904, 968, flashing ? "COPIED" : "COPY")) copyActiveToOther();
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
        const auto& presets = duskverb::dv2FactoryPresets();
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
        recallPresetSound(state);
        currentUserPath_.clear();
    }

    // Clamped, never wrapping: stepping off either end parks on the end preset.
    void stepPreset(int direction)
    {
        const int count = (int)duskverb::dv2FactoryPresets().size();
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
        recallPresetSound(duskverb::makeDefaultState());
        currentUserPath_.clear();
    }

    // A preset, INIT or an import: under MIX LOCK the Mix and Bus in effect stay.
    // (A/B swaps go straight to recallSnapshot: they restore a complete sound.)
    void recallPresetSound(duskverb::StateValues state)
    {
        if (mixLocked_)
            duskverb::holdMixLock(state, captureSnapshot());
        recallSnapshot(std::move(state));
    }

    void setMixLock(bool locked)
    {
        mixLocked_ = locked;
        setState("mix_lock", locked ? "1" : "0");
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

    bool saveUserPreset(const char* rawName, bool allowReplace)
    {
        auto state = captureSnapshot();
        state.userName = duskdaf::normaliseUserPresetName(rawName);
        if (state.userName.empty() || state.userName.find_first_of("\r\n") != std::string::npos)
            return false;
        state.edited = false;
        const auto saved = duskdaf::writeUserPreset(
            configDir(), ".dvpreset", state.userName.c_str(),
            [&state](std::ostream& output) { output << duskverb::encodeState(state) << '\n'; }, allowReplace);
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
        recallPresetSound(state);
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
                if (saveUserPreset(saveBuf_, overwriteAccepted_)) { saveFailed_ = false; ImGui::CloseCurrentPopup(); }
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
        const float y0 = kBodyY0, y1 = kBodyY1;
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
    // liveness: what the selected engine hears (DuskVerbKnobLiveness.hpp)
    //======================================================================
    int algorithmSlot() const { return (int)std::lround(values_[duskverb::Algorithm]); }
    bool liveHere(int p) const { return duskverb::controlLive(algorithmSlot(), p); }

    //======================================================================
    // primary row: SPACE | DECAY | OUTPUT
    //======================================================================
    void drawSpaceCard(ImDrawList* dl)
    {
        card(dl, kBodyX0, kBodyY0, kSpaceX1, kRow1Y1, "SPACE");
        drawEngineCombo(dl);

        const int sync = std::clamp((int)std::lround(values_[duskverb::PredelaySync]), 0, 6);
        char syncTip[96];
        std::snprintf(syncTip, sizeof(syncTip), "Synced to the host tempo (%s).\nSet SYNC to Free to use this knob.",
                      duskverb::kPreDelaySyncLabels[sync]);
        const KnobOverride synced { duskverb::kPreDelaySyncLabels[sync], syncTip };
        const float preX = slotX(kBodyX0, kSpaceX1, 3, 0);
        knobCell(dl, duskverb::Predelay, preX, kPrimaryCy, kPrimaryR, "PRE-DELAY",
                 liveHere(duskverb::Predelay), sync != 0 ? &synced : nullptr);
        knobCell(dl, duskverb::Size, slotX(kBodyX0, kSpaceX1, 3, 1), kPrimaryCy, kPrimaryR, "SIZE",
                 liveHere(duskverb::Size));
        homeSlot(dl, duskverb::Diffusion, slotX(kBodyX0, kSpaceX1, 3, 2), kPrimaryCy, kPrimaryR, "DIFFUSION");

        // The sync division sits under PRE-DELAY, as BUS sits under MIX.
        char preview[24];
        std::snprintf(preview, sizeof(preview), "SYNC  %s", duskverb::kPreDelaySyncLabels[sync]);
        if (beginCombo("##dv_sync", preX - 52.0f, 298.0f, preX + 52.0f, 326.0f, preview, kComboPx))
        {
            for (int i = 0; i < 7; ++i)
                if (ImGui::Selectable(duskverb::kPreDelaySyncLabels[i], i == sync))
                {
                    setP(duskverb::PredelaySync, (float)i);
                    ImGui::CloseCurrentPopup();
                }
        }
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltipFor(currentEngine(), duskverb::PredelaySync));
        endCombo();
    }

    void drawEngineCombo(ImDrawList* dl)
    {
        const int current = duskverb::resolveAlgorithm(algorithmSlot());
        duskdaf::spacedText(panel_, dl, kBodyX0 + 20.0f, capsTop(119.0f, kCaptionPx), kCaptionPx, kDim,
                            "ENGINE", -1, 0.14f);
        if (beginCombo("##dv_engine", kBodyX0 + 100.0f, 104.0f, kSpaceX1 - 20.0f, 134.0f,
                       duskverb::algorithmName(current), kComboPx))
        {
            for (int i = 0; i < duskverb::kNumAlgorithms; ++i)
            {
                // Curated roster: only the engines a preset actually uses are
                // offered. A hidden engine restored from saved state or set by
                // automation still appears, so the selection stays recoverable
                // and the box never reads blank.
                if (!duskverb::algorithmVisible(i) && i != current)
                    continue;
                if (ImGui::Selectable(duskverb::algorithmName(i), i == current))
                {
                    setP(duskverb::Algorithm, (float)i);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltipFor(currentEngine(), duskverb::Algorithm));
        endCombo();
    }

    void drawDecayCard(ImDrawList* dl)
    {
        card(dl, kSpaceX1 + kGutter, kBodyY0, kOutputX0 - kGutter, kRow1Y1, "DECAY", /*hero*/ true, kValueInk);
        const duskverb::ParamDesc& d = duskverb::paramDesc(duskverb::Decay);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        const float t = std::clamp((values_[duskverb::Decay] - lo) / (hi - lo), 0.0f, 1.0f);
        knobArt(dl, kHeroCx, kHeroCy, kHeroR, t, true);
        char decaySeed[48];
        formatPlainValue(currentEngine(), duskverb::Decay, plainOf(duskverb::Decay), decaySeed, sizeof(decaySeed));
        ImGui::SetCursorScreenPos(P(kHeroCx - 78.0f, kHeroCy + kHeroR + 12.0f));
        ImGui::InvisibleButton("##decay_value_entry", ImVec2(156.0f * s_, 34.0f * s_));
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
        const float valueY = kHeroCy + kHeroR + 16.0f;   // in the opening at the foot of the arc
        valueText(dl, kHeroCx - totalW * 0.5f, valueY, kHeroValuePx, kAccent, number, -1);
        panel_.text(dl, kHeroCx - totalW * 0.5f + numW + 6.0f,
                    valueY + kHeroValuePx - kHeroUnitPx - 3.0f,
                    kHeroUnitPx, fade(kAccent, 0.78f), unit, -1, true);

        // How the tail behaves: FREEZE, and beside it the engine's own tail switch
        // where it has one, the Gated engine's gate or Tonal Correction on the
        // engines that run the octave network it corrects (no engine has both).
        // Elsewhere either would be a dead switch, so it is not shown.
        struct Strip { const char* id; int param; const char* caption; };
        Strip strips[2] = { { "##dv_freeze", duskverb::Freeze, "FREEZE" }, {} };
        int count = 1;
        if (liveHere(duskverb::GateEnabled))
            strips[count++] = { "##dv_gate", duskverb::GateEnabled, "GATE" };
        else if (liveHere(duskverb::TonalCorrection))
            strips[count++] = { "##dv_tonal", duskverb::TonalCorrection, "TONAL CORRECTION" };
        constexpr float kStripGap = 8.0f;
        float widths[2], total = kStripGap * (float)(count - 1);
        for (int i = 0; i < count; ++i)
            total += widths[i] = stripWidth(strips[i].caption);
        float x = kHeroCx - 0.5f * total;
        for (int i = 0; i < count; ++i)
        {
            switchStrip(dl, strips[i].id, strips[i].param, x, 300.0f, x + widths[i], 324.0f,
                        strips[i].caption, liveHere(strips[i].param));
            x += widths[i] + kStripGap;
        }
    }

    // The width a switch strip needs for its caption: the caption at its left,
    // the pill at its right (switchStrip's insets).
    float stripWidth(const char* caption) const
    {
        return 12.0f + spacedWidth(caption, kLabelPx, 0.14f) + 12.0f + 30.0f + 9.0f;
    }

    void drawOutputCard(ImDrawList* dl)
    {
        card(dl, kOutputX0, kBodyY0, kBodyX1, kRow1Y1, "OUTPUT");
        // MIX LOCK takes the row ENGINE takes in SPACE.
        if (toggleStrip(dl, "##dv_mixlock", mixLocked_, kBodyX1 - 158.0f, 104.0f, kBodyX1 - 20.0f, 132.0f,
                        "MIX LOCK", "Keeps MIX and BUS where they are when a preset, INIT or an\n"
                                    "import is recalled, or the host changes program. A/B is unaffected."))
            setMixLock(!mixLocked_);
        knobCell(dl, duskverb::Width, slotX(kOutputX0, kBodyX1, 3, 0), kPrimaryCy, kPrimaryR, "WIDTH",
                 liveHere(duskverb::Width));
        knobCell(dl, duskverb::GainTrim, slotX(kOutputX0, kBodyX1, 3, 1), kPrimaryCy, kPrimaryR, "TRIM",
                 liveHere(duskverb::GainTrim));

        // MIX at the end of the signal, beside the OUT meter; BUS under it.
        static constexpr KnobOverride busWet { "BUS", "BUS is on: the output is 100% wet.\nTurn BUS off to use MIX." };
        const float mixX = slotX(kOutputX0, kBodyX1, 3, 2);
        knobCell(dl, duskverb::Mix, mixX, kPrimaryCy, kPrimaryR, "MIX", liveHere(duskverb::Mix),
                 values_[duskverb::BusMode] > 0.5f ? &busWet : nullptr);
        switchStrip(dl, "##dv_bus", duskverb::BusMode, mixX - 52.0f, 298.0f, mixX + 52.0f, 326.0f, "BUS");
    }

    //======================================================================
    // TONE: the graph over its knobs, in frequency order
    //======================================================================
    void drawToneCard(ImDrawList* dl)
    {
        card(dl, kBodyX0, kToneY0, kBodyX1, kToneY1, "TONE");
        drawToneGraph(dl);

        struct Slot { int param; const char* name; };
        static constexpr Slot slots[] = {
            { duskverb::LoCut, "LOW CUT" },          { duskverb::BassMult, "BASS MULT" },
            { duskverb::Crossover, "LOW XOVER" },    { duskverb::MidMult, "MID MULT" },
            { duskverb::HighCrossover, "HIGH XOVER" }, { duskverb::Damping, "TREBLE MULT" },
            { duskverb::HiCut, "HIGH CUT" },         { duskverb::Tone, "TONE" },
        };
        constexpr int n = (int)(sizeof(slots) / sizeof(slots[0]));
        for (int i = 0; i < n; ++i)
            homeSlot(dl, slots[i].param, slotX(kBodyX0, kBodyX1, n, i), kToneKnobCy, kKnobR, slots[i].name);
        // TONE tilts the whole wet, so a rule sets it apart from the controls it bends.
        const float ruleX = 0.5f * (slotX(kBodyX0, kBodyX1, n, n - 2) + slotX(kBodyX0, kBodyX1, n, n - 1));
        dl->AddLine(P(ruleX, kToneKnobCy - kKnobR - kLabelDy), P(ruleX, kToneKnobCy + kKnobR + 6.0f),
                    kCardEdge, s_);
    }

    //======================================================================
    // TONE graph: DuskVerbToneGraph.hpp holds the arithmetic
    //======================================================================
    duskverb::ui::ToneValues toneValues() const
    {
        duskverb::ui::ToneValues v;
        v.loCut  = plainOf(duskverb::LoCut);
        v.hiCut  = plainOf(duskverb::HiCut);
        v.bass   = plainOf(duskverb::BassMult);
        v.mid    = plainOf(duskverb::MidMult);
        v.treble = plainOf(duskverb::Damping);
        v.lowX   = plainOf(duskverb::Crossover);
        v.highX  = plainOf(duskverb::HighCrossover);
        return v;
    }

    // The decay curve and its handles appear where the engine runs the
    // three-band profile the graph draws: every shared engine but Spring and
    // Reverse, which damp their own way, and the nonlinear burst, which has only
    // its HF damping. The cuts are on every engine.
    bool toneProfileApplies() const
    {
        return liveHere(duskverb::BassMult) && liveHere(duskverb::Damping)
            && liveHere(duskverb::Crossover) && liveHere(duskverb::HighCrossover);
    }

    duskverb::ui::ToneLive toneLive() const
    {
        const bool profile = toneProfileApplies();
        duskverb::ui::ToneLive l;
        l.loCut = liveHere(duskverb::LoCut);
        l.hiCut = liveHere(duskverb::HiCut);
        l.bass = l.treble = l.lowX = l.highX = profile;
        l.mid = profile && liveHere(duskverb::MidMult) && !inEngineCard(currentEngine(), duskverb::MidMult);
        return l;
    }

    // The parameters handle `h` moves, live ones only, frequency first.
    static int toneParams(duskverb::ui::ToneHandle h, const duskverb::ui::ToneLive& l, int out[2])
    {
        using duskverb::ui::ToneHandle;
        int n = 0;
        switch (h)
        {
            case ToneHandle::LoCut:  if (l.loCut) out[n++] = duskverb::LoCut; break;
            case ToneHandle::HiCut:  if (l.hiCut) out[n++] = duskverb::HiCut; break;
            case ToneHandle::Bass:
                if (l.lowX) out[n++] = duskverb::Crossover;
                if (l.bass) out[n++] = duskverb::BassMult;
                break;
            case ToneHandle::Treble:
                if (l.highX)  out[n++] = duskverb::HighCrossover;
                if (l.treble) out[n++] = duskverb::Damping;
                break;
            case ToneHandle::Mid:    if (l.mid) out[n++] = duskverb::MidMult; break;
            case ToneHandle::None:   break;
        }
        return n;
    }

    static float toneField(const duskverb::ui::ToneValues& v, int p)
    {
        switch (p)
        {
            case duskverb::LoCut:         return v.loCut;
            case duskverb::HiCut:         return v.hiCut;
            case duskverb::BassMult:      return v.bass;
            case duskverb::MidMult:       return v.mid;
            case duskverb::Damping:       return v.treble;
            case duskverb::Crossover:     return v.lowX;
            case duskverb::HighCrossover: return v.highX;
            default:                      return 0.0f;
        }
    }

    void dashedLine(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 col) const
    {
        const int n = std::max(1, (int)(std::hypot(x1 - x0, y1 - y0) / 5.0f));
        for (int i = 0; i < n; i += 2)
        {
            const float a = (float)i / (float)n, b = std::min(1.0f, (float)(i + 1) / (float)n);
            dl->AddLine(P(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a),
                        P(x0 + (x1 - x0) * b, y0 + (y1 - y0) * b), col, s_);
        }
    }

    // The decay curve, filled down to the floor, or dashed for the TONE ghost.
    void drawToneCurve(ImDrawList* dl, const duskverb::ui::ToneGraph& g,
                       const duskverb::ui::ToneValues& v, bool solid) const
    {
        constexpr int kSteps = 176;
        ImVec2 pts[kSteps + 1];
        for (int i = 0; i <= kSteps; ++i)
        {
            const float x = g.left() + (g.right() - g.left()) * (float)i / (float)kSteps;
            pts[i] = P(x, g.yOf(duskverb::ui::ToneGraph::multiplierAt(v, g.hzOf(x))));
        }
        if (!solid)
        {
            for (int i = 0; i < kSteps; i += 2)
                dl->AddLine(pts[i], pts[i + 1], fade(kAccent, 0.6f), 1.5f * s_);
            return;
        }
        const float floorY = P(0.0f, g.bottom()).y;
        for (int i = 0; i < kSteps; ++i)
            dl->AddQuadFilled(pts[i], pts[i + 1], ImVec2(pts[i + 1].x, floorY), ImVec2(pts[i].x, floorY),
                              fade(kAccent, 0.10f));
        dl->AddPolyline(pts, kSteps + 1, kAccent, 0, 2.0f * s_);
    }

    void drawToneGraph(ImDrawList* dl)
    {
        using duskverb::ui::ToneGraph;
        using duskverb::ui::ToneHandle;
        const ToneGraph g(kGraphX0, kGraphY0, kGraphX1, kGraphY1);
        const duskverb::ui::ToneLive live = toneLive();
        const bool profile = toneProfileApplies();
        duskverb::ui::ToneValues v = toneValues();

        // Input first, so what is drawn below is this frame's values. A drag
        // moves its parameters from where they were when it began, by how far the
        // pointer has gone, with the host told it is one gesture.
        // The hit area stops 4 px under the plot: the knob names below are click
        // targets too, and ImGui gives an overlap to whichever came first.
        ImGui::SetCursorScreenPos(P(g.left() - ToneGraph::kBarGrab, g.top() - ToneGraph::kHandleGrab));
        ImGui::InvisibleButton("##dv_tone_graph",
                               ImVec2((g.right() - g.left() + 2.0f * ToneGraph::kBarGrab) * s_,
                                      (g.bottom() - g.top() + ToneGraph::kHandleGrab + 4.0f) * s_));
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 m = toDesign(io.MousePos);
        ToneHandle hot = toneDrag_;
        if (hot == ToneHandle::None && ImGui::IsItemHovered())
            hot = g.hitTest(m.x, m.y, v, live);
        if (ImGui::IsItemActivated() && hot != ToneHandle::None)
        {
            int params[2];
            const int n = toneParams(hot, live, params);
            if (io.KeyAlt)   // Alt-click: back to the defaults, as on a knob
            {
                for (int i = 0; i < n; ++i)
                    setP(params[i], duskverb::knobDefault(duskverb::paramDesc(params[i])));
                v = toneValues();
            }
            else
            {
                toneDrag_ = hot;
                toneDragStart_ = v;
                toneDragMouse_ = m;
                toneDragCount_ = n;
                for (int i = 0; i < n; ++i)
                {
                    toneDragParams_[i] = params[i];
                    beginEdit((uint32_t)params[i]);
                }
            }
        }
        if (toneDrag_ != ToneHandle::None)
        {
            if (ImGui::IsItemActive())
            {
                const duskverb::ui::ToneValues to =
                    g.dragged(toneDrag_, toneDragStart_, m.x - toneDragMouse_.x, m.y - toneDragMouse_.y,
                              io.KeyShift, io.KeyCtrl || io.KeySuper, live);
                for (int i = 0; i < toneDragCount_; ++i)
                {
                    const int p = toneDragParams_[i];
                    if (toneField(to, p) != toneField(v, p))
                        setParam((uint32_t)p, duskverb::plainToKnob(duskverb::paramDesc(p), toneField(to, p)));
                }
                v = toneValues();
            }
            else
            {
                for (int i = 0; i < toneDragCount_; ++i)
                    endEdit((uint32_t)toneDragParams_[i]);
                toneDrag_ = ToneHandle::None;
                toneDragCount_ = 0;
            }
        }

        // Plot and frequency grid. Where the engine runs the profile, its decay
        // scale (x1 is the Decay knob's own time) and the curve go on top.
        dl->AddRectFilled(P(g.left(), g.top()), P(g.right(), g.bottom()), kStripBg, 5.0f * s_);
        float grid[24];
        const int lines = g.axis().gridLines(grid, 24);
        for (int i = 0; i < lines; ++i)
        {
            const float x = g.xOf(grid[i]);
            if (x <= g.left() + 0.5f || x >= g.right() - 0.5f)
                continue;
            const bool decade = grid[i] == 100.0f || grid[i] == 1000.0f || grid[i] == 10000.0f;
            dl->AddLine(P(x, g.top()), P(x, g.bottom()), decade ? kGridLineHi : kGridLine, s_);
        }
        for (const float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            char label[8];
            duskdaf::LogFreqAxis::label(f, label, (int)sizeof(label));
            valueText(dl, g.xOf(f) + 3.0f, g.bottom() - 16.0f, kAxisPx, kGridInk, label, -1);
        }
        const float tone = plainOf(duskverb::Tone);
        const bool tilted = std::fabs(tone) > 1.0e-3f;
        if (profile)
        {
            for (const float mult : { 0.5f, 1.0f, 2.0f })
            {
                const float y = g.yOf(mult);
                dashedLine(dl, g.left(), y, g.right(), y, mult == 1.0f ? kGridLineHi : kGridLine);
                valueText(dl, g.left() - 6.0f, y - 8.0f, kAxisPx, kGridInk,
                          mult == 0.5f ? "x0.5" : (mult == 1.0f ? "x1" : "x2"), 1);
            }

            // The decay curve, with TONE's take on the treble dashed beside it.
            duskverb::ui::ToneValues drawn = v;
            if (!live.mid)
                drawn.mid = 1.0f;   // the Gated engine's Mid Multiply is its gate THRESHOLD
            drawToneCurve(dl, g, drawn, true);
            if (tilted)
            {
                duskverb::ui::ToneValues ghost = drawn;
                ghost.treble = drawn.treble * duskverb::toneTrebleFactor(tone);
                drawToneCurve(dl, g, ghost, false);
            }
        }
        else
        {
            panel_.text(dl, 0.5f * (g.left() + g.right()), 0.5f * (g.top() + g.bottom()) - 0.5f * kCaptionPx,
                        kCaptionPx, kGridInk, "THIS ENGINE SHAPES ITS OWN DECAY", 0, true);
        }

        // The wet filters: what they take away is shaded over everything.
        const float xLo = g.handleAt(ToneHandle::LoCut, v).x, xHi = g.handleAt(ToneHandle::HiCut, v).x;
        if (xLo > g.left() + 0.5f)
            dl->AddRectFilled(P(g.left(), g.top()), P(xLo, g.bottom()), kCutShade, 5.0f * s_,
                              ImDrawFlags_RoundCornersLeft);
        if (xHi < g.right() - 0.5f)
            dl->AddRectFilled(P(xHi, g.top()), P(g.right(), g.bottom()), kCutShade, 5.0f * s_,
                              ImDrawFlags_RoundCornersRight);
        if (tilted)
        {
            const float x = std::clamp(g.xOf(duskverb::effectiveHiCut(v.hiCut, duskverb::toneHiCutFactor(tone))),
                                       g.left(), g.right());
            dashedLine(dl, x, g.top(), x, g.bottom(), fade(kAccent, 0.6f));
        }

        // Handles: the cut bars with a grab tab, then the band handles on the curve.
        for (const ToneHandle h : { ToneHandle::LoCut, ToneHandle::HiCut })
        {
            const bool canMove = ToneGraph::movable(h, v, live);
            const float x = g.handleAt(h, v).x;
            const ImU32 ink = fade(hot == h ? kAccent : kDim, canMove ? 1.0f : kUnusedKnobAlpha);
            dl->AddLine(P(x, g.top()), P(x, g.bottom()), ink, (hot == h ? 2.0f : 1.4f) * s_);
            dl->AddRectFilled(P(x - 5.0f, g.top() + 3.0f), P(x + 5.0f, g.top() + 17.0f), ink, 2.0f * s_);
        }
        if (profile)
            for (const ToneHandle h : { ToneHandle::Bass, ToneHandle::Mid, ToneHandle::Treble })
            {
                if (!ToneGraph::movable(h, v, live))
                    continue;
                const auto p = g.handleAt(h, v);
                dl->AddCircleFilled(P(p.x, p.y), 7.0f * s_, hot == h ? kAccent : kCardBg, 24);
                dl->AddCircle(P(p.x, p.y), 7.0f * s_, kAccent, 24, 1.8f * s_);
            }

        if (hot != ToneHandle::None)
        {
            int params[2];
            const int n = toneParams(hot, live, params);
            char text[128] = "";
            for (int i = 0; i < n; ++i)
            {
                char value[48];
                formatPlainValue(currentEngine(), params[i], plainOf(params[i]), value, sizeof(value));
                const size_t used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, "%s%s  %s", i > 0 ? "\n" : "",
                              duskverb::paramDesc(params[i]).name, value);
            }
            ImGui::SetTooltip("%s", text);
        }
    }

    //======================================================================
    // detail row: EARLY | engine card | BASS MONO | MACRO
    //======================================================================
    // A re-purposed control's own name, for labelFor to keep where the engine
    // does not rename it.
    static const char* universalName(int p)
    {
        switch (p)
        {
            case duskverb::ModDepth:  return "DEPTH";
            case duskverb::ModRate:   return "RATE";
            case duskverb::Diffusion: return "DIFFUSION";
            case duskverb::MidMult:   return "MID MULT";
            default:                  return duskverb::paramDesc(p).name;
        }
    }

    void drawDetailRow(ImDrawList* dl)
    {
        const EngineType e = currentEngine();
        const EngineCard engine = engineCard(e);
        // One pitch for the whole row: each card is as wide as its knobs, so the
        // row re-flows (outer edges fixed) when an engine card holds three or four.
        const int counts[4] = { 2, engine.count, 2, 3 };
        const int slots = counts[0] + counts[1] + counts[2] + counts[3];
        const float pitch = (kBodyX1 - kBodyX0 - 3.0f * kGutter) / (float)slots;
        float x0[4], x1[4];
        float x = kBodyX0;
        for (int c = 0; c < 4; ++c)
        {
            x0[c] = x;
            x1[c] = x + pitch * (float)counts[c];
            x = x1[c] + kGutter;
        }
        const auto knobAt = [&](int c, int i, int p, const char* name) {
            knobCell(dl, p, slotX(x0[c], x1[c], counts[c], i), kDetailCy, kKnobR, name, liveHere(p),
                     nullptr, pitch - 16.0f);
        };

        card(dl, x0[0], kDetailY0, x1[0], kBodyY1, "EARLY");
        knobAt(0, 0, duskverb::ErLevel, "ER LEVEL");
        knobAt(0, 1, duskverb::ErSize, "ER SIZE");

        // The engine card (the gate's own switch sits beside FREEZE in DECAY).
        card(dl, x0[1], kDetailY0, x1[1], kBodyY1, engine.title);
        for (int i = 0; i < engine.count; ++i)
            knobAt(1, i, engine.params[i], labelFor(e, engine.params[i], universalName(engine.params[i])));

        // The card title says what is made mono, so the knobs need only say how.
        card(dl, x0[2], kDetailY0, x1[2], kBodyY1, "BASS MONO");
        knobAt(2, 0, duskverb::MonoBelow, "BELOW");
        knobAt(2, 1, duskverb::MonoBelowDepth, "DEPTH");

        card(dl, x0[3], kDetailY0, x1[3], kBodyY1, "MACRO");
        knobAt(3, 0, duskverb::Saturation, "SATURATION");
        knobAt(3, 1, duskverb::Character, "CHARACTER");
        knobAt(3, 2, duskverb::Duck, "DUCK");
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

        const float px0 = 130.0f, py0 = 150.0f, px1 = 1070.0f, py1 = 650.0f;
        const float cx = 0.5f * (px0 + px1);
        dl->AddRectFilled(P(px0, py0), P(px1, py1), IM_COL32(0x14, 0x19, 0x22, 246), 10.0f * s_);
        dl->AddRect(P(px0, py0), P(px1, py1), fade(kAccent, 0.45f), 10.0f * s_, 0, 1.2f * s_);
        duskdaf::spacedText(panel_, dl, cx, py0 + 16.0f, 24.0f, kValueInk, "DUSKVERB 2", 0, 0.20f);

        struct Row { const char* key; const char* text; };
        static const Row controls[] = {
            { "Drag a knob",         "Adjust" },
            { "Shift + drag",        "Fine adjust" },
           #if defined(__APPLE__)
            { "Alt / Cmd click",     "Reset to default" },
           #else
            { "Ctrl / Alt click",    "Reset to default" },
           #endif
            { "Double-click",        "Type a value" },
            { "Right-click",         "Reset / type menu" },
            { "Mouse wheel",         "Step the value" },
            { "Drag a graph handle", "Its band's crossover and decay" },
           #if defined(__APPLE__)
            { "Shift / Cmd + drag",  "Frequency / level only" },
           #else
            { "Shift / Ctrl + drag", "Frequency / level only" },
           #endif
            { "Alt click a handle",  "Reset it" },
            { "Drag the corner",     "Resize the editor" },
        };
        static const Row session[] = {
            { "< / >",                     "Previous / next preset" },
            { "INIT",                      "Everything back to defaults" },
            { "SAVE",                      "Store a user preset" },
            { "Right-click a user preset", "Delete it" },
            { "A / B",                     "Switch complete sound snapshots" },
            { "COPY",                      "Copy this sound to the other slot" },
            { "MIX LOCK",                  "Keep MIX and BUS across presets" },
            { "Click the nameplate",       "Supporters" },
            { "?  or Shift + /",           "Show or hide this" },
        };
        const float top = py0 + 64.0f, lineH = 26.0f;
        const auto column = [&](const Row* rows, int count, float keyX) {
            for (int i = 0; i < count; ++i)
            {
                panel_.text(dl, keyX, top + (float)i * lineH, 18.0f, kAccent, rows[i].key, 1, true);
                valueText(dl, keyX + 12.0f, top + (float)i * lineH, kComboPx, kLabelInk, rows[i].text, -1);
            }
        };
        constexpr int kControlRows = (int)(sizeof(controls) / sizeof(controls[0]));
        column(controls, kControlRows, px0 + 200.0f);
        column(session, (int)(sizeof(session) / sizeof(session[0])), cx + 200.0f);

        const float notesY = top + (float)kControlRows * lineH + 18.0f;
        valueText(dl, cx, notesY, kComboPx, kDim,
                  "Dimmed controls do nothing on the selected engine; they keep their value for one that does.", 0);
        valueText(dl, cx, notesY + 22.0f, kComboPx, kDim,
                  "GATE and TONAL CORRECTION appear only on the engines they affect.", 0);
        valueText(dl, cx, notesY + 44.0f, kComboPx, kDim,
                  "The second card of the bottom row holds the selected engine's own controls.", 0);
        const float cpu = duskVerbGetCpuLoad != nullptr ? duskVerbGetCpuLoad(getPluginInstancePointer()) : 0.0f;
        char cpuText[48];
        std::snprintf(cpuText, sizeof(cpuText), "This instance: CPU %.1f%%",
                      (double)std::clamp(cpu * 100.0f, 0.0f, 999.0f));
        valueText(dl, cx, notesY + 72.0f, kComboPx, kAccent, cpuText, 0);
        valueText(dl, cx, py1 - 24.0f, 14.0f, kDim, "Press any key to dismiss", 0);
    }

    //======================================================================
    // state
    //======================================================================
    float  values_[duskverb::kNumParams] = {};
    bool   mixLocked_ = false;   // the plugin's "mix_lock" state, mirrored

    // The TONE graph handle being dragged, where it and the pointer started, and
    // the parameters whose edit gesture the drag opened (closed on release even
    // if the engine changed under the pointer).
    duskverb::ui::ToneHandle toneDrag_ = duskverb::ui::ToneHandle::None;
    duskverb::ui::ToneValues toneDragStart_ {};
    ImVec2 toneDragMouse_ { 0.0f, 0.0f };
    int toneDragParams_[2] = { 0, 0 };
    int toneDragCount_ = 0;

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
