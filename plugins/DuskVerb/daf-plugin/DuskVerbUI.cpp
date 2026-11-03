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
// The face is a studio reverb processor: a graphite faceplate with printed
// legends, one recessed amber display, and controls in signal-flow order. Every
// colour is a Theme role, every text size a type-scale entry, and every control
// sits on an eight-column grid (DECAY on the line between two columns). Layout,
// top to bottom:
//   * the Dusk DAF top row: nameplate, preset browser, INIT/SAVE, A/B, COPY,
//     help, brand
//   * IN and OUT LED rails down the full body height
//   * row A: display (engine, decay time, annunciators, output history) |
//     DECAY | SIZE | FREEZE and the engine's own switch (GATE or TONAL)
//   * row B: INPUT | EARLY REFLECTIONS | MODULATION (GATE on the Gated engine)
//   * row C: DAMPING | MACRO
//   * row D: FILTER | OUTPUT
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

    // The faceplate palette. Every colour on the panel is one of these roles or
    // a blend of two of them; nothing below picks its own RGB. Amber is the one
    // accent. It marks state (the display, a lit LED, a knob under the hand) and
    // tints the nameplate rule, which every DAF port draws in its own accent.
    struct Theme
    {
        ImU32 face      = IM_COL32( 44,  44,  46, 255);  // anodised graphite faceplate
        ImU32 faceDeep  = IM_COL32( 26,  26,  28, 255);  // header band, wells around caps
        ImU32 well      = IM_COL32( 12,  11,  10, 255);  // display glass, meter wells
        ImU32 legend    = IM_COL32(212, 208, 198, 255);  // silk-screen print
        ImU32 legendDim = IM_COL32(124, 121, 114, 255);  // scale ticks, group brackets
        ImU32 value     = IM_COL32(238, 235, 227, 255);  // read-outs, nameplate, pointers
        ImU32 amber     = IM_COL32(255, 170,  62, 255);  // the accent
        ImU32 signal    = IM_COL32(124, 196,  96, 255);  // meter safe zone
        ImU32 alarm     = IM_COL32(238,  74,  56, 255);  // meter clip, errors
    };
    constexpr Theme kTheme{};

    // Type scale in design px. Semibold for print (legends, brackets, buttons,
    // combos, nameplate), regular for numbers; the font bakes list exactly these.
    // The floor is the SMALLEST window (0.875x): there a control legend still
    // renders near 16 px and a read-out near 18 px, as large as the stock DAW
    // plugins this sits beside. Readability sets these sizes, not looks.
    constexpr float kTypeSmall   = 16.0f;   // header buttons, combo previews, brackets, captions, rails
    constexpr float kTypeLegend  = 18.0f;   // control legends; popup rows; tooltip, modal and help text
    constexpr float kTypeValue   = 20.0f;   // knob read-outs; decay unit
    constexpr float kTypeVersion = 14.0f;   // nameplate version (family top row)
    constexpr float kTypeBadge   = 20.0f;   // nameplate model badge (family top row)
    constexpr float kTypeBrand   = 22.0f;   // DUSK AUDIO (family top row)
    constexpr float kTypeTitle   = 24.0f;   // nameplate title (family top row); engine name
    constexpr float kTypeHero    = 52.0f;   // decay digits in the display
    constexpr float kTypeOverlaySmall = 12.0f;   // DuskSupportersOverlay's 9.5-12 px text
    // Letter spacing as a fraction of the size: control legends barely
    // tracked, group brackets and display captions set wide.
    constexpr float kLegendTracking = 0.05f, kBracketTracking = 0.16f;
    constexpr float kPi = 3.14159265358979f;

    // Header: the Dusk DAF top row (Tape Echo 2 geometry; brand 87 px from the
    // right edge). One 25 px band centred on y = 24.
    constexpr float kHeaderH = 48.0f;
    constexpr float kBandY0 = 11.5f, kBandY1 = 36.5f;

    // Eight equal columns between the meter rails. Every control centres on
    // one, except DECAY, which spans two.
    constexpr float kGridX0 = 56.0f, kColW = 136.0f;
    constexpr float colX(int c) { return kGridX0 + kColW * ((float)c + 0.5f); }
    constexpr float colL(int c) { return kGridX0 + kColW * (float)c; }

    // Row A carries the display and the tank; B..D are knob rows that share
    // one vertical rhythm: bracket, legend, knob centre, read-out.
    constexpr float kRowB0 = 288.0f, kRowC0 = 458.0f, kRowD0 = 628.0f;
    constexpr float kBracketDy = 12.0f, kLegendDy = 28.0f, kKnobDy = 96.0f, kValueDy = 134.0f;

    // Knob sizes by importance, plus the space the printed scale and the text
    // keep from the knob body.
    constexpr float kHeroR = 64.0f, kPrimaryR = 33.0f, kKnobR = 28.0f;
    constexpr float kTickGap = 4.0f, kTickMinor = 7.0f, kTickMajor = 10.0f;
    constexpr float kLegendGap = 40.0f;   // legend top sits this far above the knob body
    constexpr float kValueGap  = 6.0f;    // read-out top sits this far below it

    // Row A.
    constexpr float kDispX0 = 60.0f, kDispX1 = 596.0f, kDispY0 = 66.0f, kDispY1 = 270.0f;
    constexpr float kHeroCx = colL(5), kHeroCy = 168.0f;   // (736, 168): CMakeLists aims the UI drag test here
    constexpr float kSizeCx = colX(6);
    constexpr float kSwitchCx = colX(7);
    constexpr float kCapW = 46.0f, kCapH = 24.0f;         // LED push-button cap

    // Meter rails, outside the grid.
    constexpr float kRailLX0 = 14.0f, kRailLX1 = 40.0f;
    constexpr float kRailRX0 = 1160.0f, kRailRX1 = 1186.0f;
    constexpr float kRailY0 = 82.0f, kRailY1 = 766.0f;

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

    // Material tones: knob caps and buttons, lit from above, and the amber
    // levels of the display. Derived from the theme so a palette change carries
    // them with it.
    ImU32 capTop()     { return blend(kTheme.face, kTheme.legend, 0.20f); }
    ImU32 capBottom()  { return blend(kTheme.faceDeep, kTheme.face, 0.35f); }
    ImU32 capRim()     { return blend(kTheme.face, kTheme.legend, 0.36f); }
    ImU32 shadow()     { return fade(kTheme.well, 0.55f); }
    ImU32 buttonFill() { return blend(kTheme.faceDeep, kTheme.face, 0.60f); }
    ImU32 buttonHot()  { return blend(kTheme.face, kTheme.legendDim, 0.14f); }
    ImU32 buttonEdge() { return blend(kTheme.face, kTheme.legendDim, 0.55f); }
    ImU32 amberDim()   { return blend(kTheme.well, kTheme.amber, 0.55f); }
    ImU32 amberGhost() { return blend(kTheme.well, kTheme.amber, 0.30f); }   // unlit segment
    ImU32 amberRow(float t) { return blend(kTheme.faceDeep, kTheme.amber, t); }

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
        pal.white    = kTheme.value;
        pal.whiteDim = kTheme.legend;
        pal.accent   = kTheme.amber;
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
        // The DSP owns the preset identity: loadProgram and setState both set it
        // and both publish a new state revision. The CLAP and VST3 wrappers also
        // call this every time the editor opens, with their own cached program,
        // which is 0 until the host picks one and is left alone by a user-preset
        // restore or a preset chosen in this editor. Trusting that index drew
        // "Vocal Plate" over every other identity on reopen, so follow the DSP;
        // a real host program change arrives there as a new revision.
        if (duskVerbStateRevision != nullptr && duskVerbReadSnapshot != nullptr)
        {
            syncFromBridge();
            return;
        }
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
        presetModified_ = false;
    }

    // Every design size the panel draws, per weight, so that at any scale the
    // requested pixel size lands on a baked face exactly (CrispFontSet::pick
    // then scales by 1.0). Sizes are design px; bakeFonts multiplies by scale.
    // kTypeLegend also covers the 18 px title DuskSupportersOverlay draws.
    static constexpr float kLabelDesignSizes[] =
        { kTypeOverlaySmall, kTypeSmall, kTypeLegend, kTypeBadge, kTypeBrand, kTypeTitle };
    static constexpr float kValueDesignSizes[] =
        { kTypeVersion, kTypeSmall, kTypeLegend, kTypeValue, kTypeHero };

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
        labelFont_  = labelFonts_.pick(kTypeLegend * scale);
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
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), kTheme.face);

        // Stock-ImGui text that this file does not place itself — above all the
        // knob tooltips, which DuskPanel::knob raises internally — would
        // otherwise render in ImGui's 13 px default face, unscaled and blurry at
        // every zoom but one. A body font for the whole window fixes those; the
        // combos and the modals push their own on top of it.
        ImFont* bodyFont = panel_.pickFont(kTypeLegend * s_);
        if (bodyFont != nullptr) ImGui::PushFont(bodyFont);
        // Same reasoning for the tooltip CHROME: ImGui's own popup colours and
        // its unscaled 8 px padding give a pale, cramped box that clips its own
        // descenders on this panel. Pushed after Begin so it reaches only the
        // windows opened later in the frame (tooltips, popups); the combos and
        // the modals still push their own on top.
        ImGui::PushStyleColor(ImGuiCol_PopupBg, fade(kTheme.faceDeep, 0.97f));
        ImGui::PushStyleColor(ImGuiCol_Border, kTheme.legendDim);
        ImGui::PushStyleColor(ImGuiCol_Text, kTheme.value);
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
        drawDisplay(dl);
        drawTank(dl);
        drawInputSection(dl);
        drawEarlySection(dl);
        drawModulationSection(dl);
        drawDampingSection(dl);
        drawMacroSection(dl);
        drawFilterSection(dl);
        drawOutputSection(dl);
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

    // Ascent in design px of the face a size resolves to, so two sizes can
    // share a baseline.
    float ascent(const duskdaf::CrispFontSet& set, float size) const
    {
        const float px = size * s_;
        ImFont* font = set.pick(px);
        if (font == nullptr) font = panel_.pickFont(px);
        return font->Ascent * (px / font->FontSize) / s_;
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
    // preset, a user preset, INIT, an A/B swap) do not come through here: they
    // go through recallSnapshot(), which sets the identity and the flag whole.
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

    EngineType currentEngine() const
    {
        int idx = (int)std::lround(values_[duskverb::Algorithm]);
        idx = std::clamp(idx, 0, getNumAlgorithms() - 1);
        return getAlgorithmConfig(idx).engine;
    }

    // DuskPanel::knob gesture domain (the knob domain: JUCE-skew normalised for
    // tapered parameters, plain otherwise) -> printed value, and back for typed
    // entry. The printed value is what the DSP receives for that knob position
    // (knob -> plain host value -> the JUCE quantisation chain), so the read-out
    // cannot disagree with the sound.
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
        // The highlighted library file belongs to the name it was loaded or
        // saved under. Any other identity arriving here (a host restore, an A/B
        // swap, a factory program) is not that file, so stop highlighting it.
        if (currentUserName_ != currentUserPathName_) currentUserPath_.clear();
        currentPreset_ = -1;
        if (currentUserName_.empty())
        {
            const auto& presets = getFactoryPresets();
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
    // Width in design px of a letter-spaced string, as spacedText() sets it.
    float spacedWidth(float size, const char* txt, float tracking) const
    {
        const float px = size * s_;
        ImFont* font = panel_.pickFont(px);
        float total = 0.0f;
        for (const char* c = txt; *c; ++c)
            total += font->CalcTextSizeA(px, FLT_MAX, 0.0f, c, c + 1).x + (c[1] ? tracking * px : 0.0f);
        return total / s_;
    }

    // A printed group bracket over grid columns col0..col1: a hairline with
    // turned-down ends and the group name set into it, the way a faceplate
    // groups its controls.
    void bracket(ImDrawList* dl, int col0, int col1, float rowY0, const char* title) const
    {
        const float x0 = colL(col0) + 10.0f, x1 = colL(col1 + 1) - 10.0f;
        const float y = rowY0 + kBracketDy;
        const float cx = 0.5f * (x0 + x1);
        const float half = 0.5f * spacedWidth(kTypeSmall, title, kBracketTracking) + 9.0f;
        dl->AddLine(P(x0, y), P(cx - half, y), kTheme.legendDim, s_);
        dl->AddLine(P(cx + half, y), P(x1, y), kTheme.legendDim, s_);
        dl->AddLine(P(x0, y), P(x0, y + 6.0f), kTheme.legendDim, s_);
        dl->AddLine(P(x1, y), P(x1, y + 6.0f), kTheme.legendDim, s_);
        duskdaf::spacedText(panel_, dl, cx, y - 9.0f, kTypeSmall, kTheme.legend, title, 0,
                            kBracketTracking);
    }

    float knobT(int p) const
    {
        const duskverb::ParamDesc& d = duskverb::paramDesc(p);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        return hi > lo ? std::clamp((values_[p] - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    }

    // The knob body: printed scale, shadow, skirt and a cap lit from above. The
    // pointer is drawn separately, after the gesture call, because it lights up
    // while the knob is under the hand.
    void knobBody(ImDrawList* dl, float cx, float cy, float r, bool hero, bool inactive) const
    {
        const ImVec2 c = P(cx, cy);
        const float R = r * s_;
        const float a = inactive ? 0.45f : 1.0f;

        const int ticks = hero ? 31 : 11;
        for (int i = 0; i < ticks; ++i)
        {
            const bool major = hero ? (i % 5 == 0) : (i == 0 || i == ticks - 1);
            const float ang = duskdaf::DuskPanel::knobAngle((float)i / (float)(ticks - 1));
            const ImVec2 d(std::sin(ang), -std::cos(ang));
            const float r0 = R + kTickGap * s_;
            const float r1 = R + (major ? kTickMajor : kTickMinor) * s_;
            dl->AddLine(ImVec2(c.x + d.x * r0, c.y + d.y * r0), ImVec2(c.x + d.x * r1, c.y + d.y * r1),
                        fade(kTheme.legendDim, a), (major ? 1.5f : 1.0f) * s_);
        }

        dl->AddCircleFilled(ImVec2(c.x, c.y + 2.0f * s_), R * 1.03f, fade(shadow(), a), 48);
        dl->AddCircleFilled(c, R, fade(kTheme.faceDeep, a), 48);

        // Cap: horizontal strips analytically clipped to the circle give a smooth
        // top-lit response with no radial banding, and stay sharp at every scale
        // because it is geometry.
        const float capR = R * (hero ? 0.86f : 0.90f);
        const ImU32 top = fade(capTop(), a), bottom = fade(capBottom(), a);
        const int bands = std::max(14, (int)(2.0f * capR));
        for (int i = 0; i <= bands; ++i)
        {
            const float u = -0.999f + 1.998f * (float)i / (float)bands;
            const float half = std::sqrt(std::max(0.0f, 1.0f - u * u)) * capR;
            dl->AddLine(ImVec2(c.x - half, c.y + u * capR), ImVec2(c.x + half, c.y + u * capR),
                        blend(top, bottom, 0.5f * (u + 1.0f)), 1.15f);
        }
        // A catch-light on the upper rim only: the light is above the panel.
        dl->PathArcTo(c, capR, -0.92f * kPi, -0.08f * kPi, 32);
        dl->PathStroke(fade(capRim(), a), 0, 1.0f * s_);

        if (hero)   // the one big machined knob gets a knurled skirt
            for (int i = 0; i < 60; ++i)
            {
                const float ang = (float)i / 60.0f * 2.0f * kPi;
                const ImVec2 d(std::cos(ang), std::sin(ang));
                dl->AddLine(ImVec2(c.x + d.x * capR * 1.03f, c.y + d.y * capR * 1.03f),
                            ImVec2(c.x + d.x * R * 0.97f, c.y + d.y * R * 0.97f),
                            fade(kTheme.face, a), 1.0f * s_);
            }
    }

    void knobPointer(ImDrawList* dl, float cx, float cy, float r, float t,
                     bool hero, bool hot, bool inactive) const
    {
        const ImVec2 c = P(cx, cy);
        const float R = r * s_;
        const float ang = duskdaf::DuskPanel::knobAngle(t);
        const ImVec2 d(std::sin(ang), -std::cos(ang));
        const ImU32 ink = hot ? kTheme.amber : (inactive ? fade(kTheme.value, 0.45f) : kTheme.value);
        dl->AddLine(ImVec2(c.x + d.x * R * 0.30f, c.y + d.y * R * 0.30f),
                    ImVec2(c.x + d.x * R * 0.82f, c.y + d.y * R * 0.82f),
                    ink, (hero ? 3.4f : 2.4f) * s_);
    }

    // One complete knob: printed legend, body, gestures, pointer and read-out.
    // The body is drawn BEFORE the gesture call (so a knob never paints over its
    // own type-entry field), the pointer and read-out AFTER it, which keeps the
    // number exact during a drag and lets the pointer light while the knob is
    // under the hand. `inactive` greys a knob the current mode overrides (DRY /
    // WET under BUS); it stays editable.
    void knobCell(ImDrawList* dl, int p, float cx, float cy, float r,
                  float legendY, float valueY, const char* name, bool inactive = false)
    {
        const duskverb::ParamDesc& d = duskverb::paramDesc(p);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        knobBody(dl, cx, cy, r, /*hero*/ false, inactive);

        char buf[48];
        formatPlainValue(currentEngine(), p, plainOf(p), buf, sizeof(buf));
        char id[24];
        std::snprintf(id, sizeof(id), "##dvk%d", p);
        // The read-out is a direct entry target too: the same edit field as the
        // knob body, with the same unit parser.
        ImGui::PushID(p);
        ImGui::SetCursorScreenPos(P(cx - 0.5f * kColW + 10.0f, valueY - 2.0f));
        ImGui::InvisibleButton("##value_entry", ImVec2((kColW - 20.0f) * s_, (kTypeValue + 6.0f) * s_));
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
        // knob()'s InvisibleButton is the last item unless its type-entry field
        // is open, which knob() submits after it (a popup or tooltip restores the
        // item state when it ends). While the field is open the knob has no
        // hover or drag state of its own.
        const bool editing = panel_.isEditingValue(id);
        const bool live = !editing && ImGui::IsItemActive();
        const bool hot  = live || (!editing && ImGui::IsItemHovered());
        if (!editing)
            knobPointer(dl, cx, cy, r, knobT(p), /*hero*/ false, hot, inactive);

        formatPlainValue(currentEngine(), p, plainOf(p), buf, sizeof(buf));
        if (name != nullptr)
            duskdaf::spacedText(panel_, dl, cx, legendY, kTypeLegend,
                                inactive ? kTheme.legendDim : kTheme.legend, name, 0, kLegendTracking);
        valueText(dl, cx, valueY, kTypeValue,
                  live ? kTheme.amber : (inactive ? kTheme.legendDim : kTheme.value), buf, 0);
    }

    // A knob on the grid of a knob row, on that row's shared text lines.
    void rowKnob(ImDrawList* dl, int p, int col, float rowY0, float r, const char* name,
                 bool inactive = false)
    {
        knobCell(dl, p, colX(col), rowY0 + kKnobDy, r, rowY0 + kLegendDy, rowY0 + kValueDy,
                 name, inactive);
    }

    // A latching push button with an LED, its legend printed above it the way a
    // knob's is. The LED is amber while on; the cap darkens while held.
    void ledButton(ImDrawList* dl, const char* id, int p, float cx, float legendY,
                   float capCy, const char* legend, const char* legend2 = nullptr)
    {
        const bool on = values_[p] > 0.5f;
        const float x0 = cx - 0.5f * kCapW, x1 = cx + 0.5f * kCapW;
        const float y0 = capCy - 0.5f * kCapH, y1 = capCy + 0.5f * kCapH;
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::InvisibleButton(id, ImVec2(kCapW * s_, kCapH * s_));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", tooltipFor(currentEngine(), p));
        if (ImGui::IsItemClicked())
            setP(p, on ? 0.0f : 1.0f);

        dl->AddRectFilled(P(x0 - 3.0f, y0 - 3.0f), P(x1 + 3.0f, y1 + 3.0f), kTheme.faceDeep, 4.0f * s_);
        const ImU32 top = held ? capBottom() : (hovered ? blend(capTop(), kTheme.legend, 0.10f) : capTop());
        dl->AddRectFilled(P(x0, y0), P(x1, y1), capBottom(), 2.5f * s_);
        dl->AddRectFilled(P(x0, y0), P(x1, 0.5f * (y0 + y1)), top, 2.5f * s_, ImDrawFlags_RoundCornersTop);
        dl->AddLine(P(x0 + 2.0f, y0 + 0.5f), P(x1 - 2.0f, y0 + 0.5f), capRim(), s_);

        const ImVec2 led = P(cx, capCy);
        if (on) dl->AddCircleFilled(led, 6.5f * s_, fade(kTheme.amber, 0.20f), 20);
        dl->AddCircleFilled(led, 4.8f * s_, kTheme.well, 16);
        dl->AddCircleFilled(led, 3.4f * s_, on ? kTheme.amber : amberGhost(), 16);

        duskdaf::spacedText(panel_, dl, cx, legendY, kTypeLegend, kTheme.legend, legend, 0,
                            kLegendTracking);
        if (legend2 != nullptr)
            duskdaf::spacedText(panel_, dl, cx, legendY + kTypeLegend + 2.0f, kTypeLegend,
                                kTheme.legend, legend2, 0, kLegendTracking);
    }

    // The family's header button on the 25 px band; `lit` marks a latched state
    // (the active A/B slot, the open help overlay).
    bool headerButton(ImDrawList* dl, const char* id, float x0, float x1,
                      const char* caption, bool lit = false)
    {
        ImGui::SetCursorScreenPos(P(x0, kBandY0));
        ImGui::InvisibleButton(id, ImVec2((x1 - x0) * s_, (kBandY1 - kBandY0) * s_));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(P(x0, kBandY0), P(x1, kBandY1), hovered ? buttonHot() : buttonFill(), 2.0f * s_);
        dl->AddRect(P(x0, kBandY0), P(x1, kBandY1), lit ? amberDim() : buttonEdge(), 2.0f * s_, 0, s_);
        label(dl, 0.5f * (x0 + x1), kBandY0 + 3.5f, kTypeSmall,
              lit ? kTheme.amber : (hovered ? kTheme.value : kTheme.legend), caption, 0);
        return ImGui::IsItemClicked();
    }

    bool headerChevron(ImDrawList* dl, const char* id, float cx, bool left)
    {
        const ImVec2 b0 = P(cx - 9.5f, kBandY0), b1 = P(cx + 9.5f, kBandY1);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, ImVec2(b1.x - b0.x, b1.y - b0.y));
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, hovered ? buttonHot() : buttonFill(), 2.0f * s_);
        dl->AddRect(b0, b1, buttonEdge(), 2.0f * s_, 0, s_);
        const ImVec2 c = P(cx, 0.5f * (kBandY0 + kBandY1));
        const float d = 4.3f * s_;
        const ImU32 ink = hovered ? kTheme.value : kTheme.legend;
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
    // endCombo(). The Display skin sets the engine selector into the amber
    // glass; `lit` puts a panel combo's own text in amber while its mode is on.
    // Rows always print in the value ink.
    enum class ComboSkin { Panel, Display };

    bool beginCombo(const char* id, float x0, float y0, float x1, float y1,
                    const char* preview, float fontSize = kTypeSmall,
                    ComboSkin skin = ComboSkin::Panel, bool lit = false,
                    ImGuiComboFlags flags = 0)
    {
        ImGui::SetCursorScreenPos(P(x0, y0));
        ImGui::SetNextItemWidth((x1 - x0) * s_);
        comboFont_ = panel_.pickFont(fontSize * s_);
        if (comboFont_ != nullptr) ImGui::PushFont(comboFont_);
        const float bandH = (y1 - y0) * s_;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(6.0f * s_,
                                   std::max(0.0f, 0.5f * (bandH - ImGui::GetFontSize()))));
        const bool glass = skin == ComboSkin::Display;
        const ImU32 frame = glass ? kTheme.well : buttonFill();
        const ImU32 hot   = glass ? blend(kTheme.well, kTheme.amber, 0.08f) : buttonHot();
        // Counted, not written out: a count that drifts by one leaks a style
        // push every frame and ImGui paints an error window over the editor.
        comboColors_ = 0;
        const auto color = [this](ImGuiCol index, ImU32 value)
        { ImGui::PushStyleColor(index, value); ++comboColors_; };
        color(ImGuiCol_FrameBg, frame);
        color(ImGuiCol_FrameBgHovered, hot);
        color(ImGuiCol_FrameBgActive, hot);
        color(ImGuiCol_Button, frame);
        color(ImGuiCol_ButtonHovered, hot);
        color(ImGuiCol_ButtonActive, hot);
        color(ImGuiCol_PopupBg, kTheme.faceDeep);
        color(ImGuiCol_Header, amberRow(0.34f));
        color(ImGuiCol_HeaderHovered, amberRow(0.44f));
        color(ImGuiCol_HeaderActive, amberRow(0.26f));
        color(ImGuiCol_Separator, kTheme.legendDim);
        color(ImGuiCol_NavHighlight, kTheme.amber);
        color(ImGuiCol_Text, glass || lit ? kTheme.amber : kTheme.value);
        // BeginCombo otherwise caps its popup at eight rows; an explicit
        // identity constraint lets the whole preset list show at once.
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
        comboOpen_ = ImGui::BeginCombo(id, preview, flags);
        // The row ink is pushed inside the popup and popped before it ends:
        // ImGui checks the style stack when a window ends.
        comboRowInk_ = comboOpen_ && (glass || lit);
        if (comboRowInk_) ImGui::PushStyleColor(ImGuiCol_Text, kTheme.value);
        // Rows print at the legend size, larger than a header preview, so the
        // whole preset list stays readable at the smallest window. Pushed and
        // popped inside the popup for the same reason as the row ink.
        comboRowFont_ = comboOpen_ ? panel_.pickFont(kTypeLegend * s_) : nullptr;
        if (comboRowFont_ != nullptr) ImGui::PushFont(comboRowFont_);
        return comboOpen_;
    }

    void endCombo()
    {
        if (comboRowFont_ != nullptr) ImGui::PopFont();
        if (comboRowInk_) ImGui::PopStyleColor();
        if (comboOpen_) ImGui::EndCombo();
        ImGui::PopStyleColor(comboColors_);
        ImGui::PopStyleVar();
        if (comboFont_ != nullptr) ImGui::PopFont();
        comboOpen_ = false;
        comboRowInk_ = false;
        comboRowFont_ = nullptr;
        comboColors_ = 0;
        comboFont_ = nullptr;
    }

    //======================================================================
    // header
    //======================================================================
    void drawHeader(ImDrawList* dl)
    {
        dl->AddRectFilled(P(0, 0), P(kDesignW, kHeaderH), kTheme.faceDeep);
        dl->AddRectFilled(P(0, 0), P(kDesignW, 3.0f), blend(kTheme.face, kTheme.legend, 0.5f));
        dl->AddLine(P(0, kHeaderH), P(kDesignW, kHeaderH), buttonEdge(), 1.2f * s_);
        dl->AddLine(P(0, kHeaderH + 1.0f), P(kDesignW, kHeaderH + 1.0f), shadow(), s_);

        // Nameplate: title, model badge and version on one plate, the accent
        // rule tying them together. Tape Echo 2 geometry, like every DAF port.
        constexpr float kPlateX0 = 28.0f, kPlateX1 = 318.0f, kPlateY0 = 8.0f, kPlateY1 = 40.0f;
        const ImU32 plateTop = blend(kTheme.faceDeep, kTheme.face, 0.55f);
        const ImU32 plateLow = blend(kTheme.well, kTheme.faceDeep, 0.50f);
        dl->AddRectFilledMultiColor(P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
                                    plateTop, plateTop, plateLow, plateLow);
        dl->AddRect(P(kPlateX0, kPlateY0), P(kPlateX1, kPlateY1),
                    fade(blend(kTheme.legend, kTheme.legendDim, 0.35f), 0.86f), 3.5f * s_, 0, 1.2f * s_);
        dl->AddLine(P(40, 37), P(308, 37), fade(kTheme.amber, 0.82f), 1.2f * s_);
        label(dl, 42, 10, kTypeTitle, kTheme.value, "DUSKVERB", -1);
        label(dl, 202, 14, kTypeBadge, kTheme.value, "DV-2", 0);
        valueText(dl, 306, 20, kTypeVersion, kTheme.legend, "v" DUSKVERB2_VERSION_STRING, 1);
        label(dl, kDesignW - 87.0f, 12, kTypeBrand, kTheme.value, "DUSK AUDIO", 0);

        // Clicking the nameplate opens the supporters panel, as on every port.
        ImGui::SetCursorScreenPos(P(kPlateX0, kPlateY0));
        ImGui::InvisibleButton("##dv_titlecredits",
                               ImVec2((kPlateX1 - kPlateX0) * s_, (kPlateY1 - kPlateY0) * s_));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) { supporters_.resetInteraction(); showSupporters_ = true; }

        // Preset browser on the band: < [combo] > INIT SAVE.
        if (headerChevron(dl, "##dv_prev", 350.5f, true)) stepPreset(-1);
        if (headerChevron(dl, "##dv_next", 569.5f, false)) stepPreset(1);
        drawPresetCombo();
        if (headerButton(dl, "##dv_init", 587, 635, "INIT")) initDefaults();
        if (headerButton(dl, "##dv_save", 642, 690, "SAVE"))
        {
            std::snprintf(saveBuf_, sizeof(saveBuf_), "%s", currentUserName_.c_str());
            ImGui::OpenPopup("Save DuskVerb Preset");
        }
        drawSaveModal();

        // This plugin's own buttons, clear of the brand: a 12 px group break
        // after SAVE, A / B / COPY on the 8 px rhythm, another 12 px break before
        // help.
        drawAbButtons(dl);
        if (headerButton(dl, "##dv_help", 854, 880, "?", showHelp_)) showHelp_ = !showHelp_;
    }

    void drawPresetCombo()
    {
        const auto& presets = getFactoryPresets();
        if (beginCombo("##dv_presets", 364, kBandY0, 556, kBandY1, presetPreview()))
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
            const float x0 = 702.0f + static_cast<float>(slot) * 38.0f;
            char id[16]; std::snprintf(id, sizeof(id), "##dv_ab%d", slot);
            if (headerButton(dl, id, x0, x0 + 30.0f, slot == 0 ? "A" : "B", slot == activeSlot_)
                && slot != activeSlot_) switchToSlot(slot);
        }
        if (headerButton(dl, "##dv_copy", 778, 842, flashing ? "COPIED" : "COPY")) copyActiveToOther();
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
        currentUserPathName_ = state.userName;
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
        currentUserPathName_ = state.userName;
        presetError_.clear();
    }

    // Both dialogs are stock ImGui widgets, so they have to be told about the
    // editor's scale explicitly: ImGui's defaults are 13 px text and 4 px
    // padding in PHYSICAL pixels, which reads as a postage stamp on a 2x window
    // and is the one place a Dusk panel would otherwise show an unscaled,
    // uncrisp face.
    int pushModalStyle()
    {
        modalFont_ = panel_.pickFont(kTypeLegend * s_);
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
        color(ImGuiCol_PopupBg, kTheme.faceDeep);
        color(ImGuiCol_TitleBg, kTheme.faceDeep);
        color(ImGuiCol_TitleBgActive, kTheme.face);
        color(ImGuiCol_ModalWindowDimBg, fade(kTheme.well, 0.75f));
        color(ImGuiCol_Text, kTheme.value);
        color(ImGuiCol_Border, buttonEdge());
        color(ImGuiCol_FrameBg, kTheme.well);
        color(ImGuiCol_FrameBgHovered, blend(kTheme.well, kTheme.legendDim, 0.12f));
        color(ImGuiCol_FrameBgActive, blend(kTheme.well, kTheme.legendDim, 0.18f));
        color(ImGuiCol_Button, buttonFill());
        color(ImGuiCol_ButtonHovered, buttonHot());
        color(ImGuiCol_ButtonActive, capBottom());
        color(ImGuiCol_CheckMark, kTheme.amber);
        color(ImGuiCol_TextSelectedBg, fade(kTheme.amber, 0.35f));
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
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kTheme.alarm),
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

    // A recessed LED well: legend above, held peak below.
    void drawRail(ImDrawList* dl, float x0, float x1, const char* title,
                  const duskdaf::LedLadder& meter) const
    {
        const float cx = 0.5f * (x0 + x1);
        duskdaf::spacedText(panel_, dl, cx, kRailY0 - 24.0f, kTypeSmall, kTheme.legend, title, 0,
                            kLegendTracking);
        dl->AddRectFilled(P(x0, kRailY0), P(x1, kRailY1), kTheme.well, 3.0f * s_);
        dl->AddLine(P(x0 + 2.0f, kRailY1 + 1.0f), P(x1 - 2.0f, kRailY1 + 1.0f),
                    fade(kTheme.legendDim, 0.55f), s_);
        meter.draw(panel_, dl, x0 + 3.0f, kRailY0 + 4.0f, x1 - 3.0f, kRailY1 - 4.0f, meterStyle_);

        char buf[16];
        const float peak = meter.peakDb();
        if (peak <= meterStyle_.minDb + 0.5f) std::snprintf(buf, sizeof(buf), "-inf");
        else                                  std::snprintf(buf, sizeof(buf), "%.0f", (double)peak);
        valueText(dl, cx, kRailY1 + 7.0f, kTypeSmall, peak > -1.0f ? kTheme.alarm : kTheme.legend, buf, 0);
    }

    //======================================================================
    // row A: the display and the tank
    //======================================================================
    // The signature element: one recessed amber display carrying the engine, the
    // decay time, the mode annunciators and the output history. The engine name
    // is the engine selector and the digits are the decay entry target. The
    // history is the output peak the DSP samples at 15 Hz, not a measured RT60.
    void drawDisplay(ImDrawList* dl)
    {
        dl->AddRectFilled(P(kDispX0, kDispY0), P(kDispX1, kDispY1), kTheme.faceDeep, 6.0f * s_);
        const float gx0 = kDispX0 + 7.0f, gx1 = kDispX1 - 7.0f;
        const float gy0 = kDispY0 + 7.0f, gy1 = kDispY1 - 7.0f;
        dl->AddRectFilled(P(gx0, gy0), P(gx1, gy1), kTheme.well, 3.0f * s_);
        // The glass sits below the faceplate, lit from above: a catch-light on
        // its lower lip.
        dl->AddLine(P(gx0 + 3.0f, gy1 + 1.0f), P(gx1 - 3.0f, gy1 + 1.0f),
                    fade(kTheme.legendDim, 0.55f), s_);

        const float ix0 = gx0 + 16.0f, ix1 = gx1 - 16.0f;
        duskdaf::spacedText(panel_, dl, ix0, gy0 + 9.0f, kTypeSmall, amberDim(), "ENGINE", -1,
                            kBracketTracking);
        drawEngineCombo(dl, ix0 - 6.0f, gy0 + 32.0f, gy0 + 66.0f);
        drawDecayReadout(dl, ix1, gy0 + 2.0f);

        // Annunciators: lit while the mode is on, a ghost segment otherwise.
        const float annY = gy0 + 80.0f;
        const int sync = std::clamp((int)std::lround(values_[duskverb::PredelaySync]), 0, 6);
        const struct { const char* text; bool on; } flags[] = {
            { "FREEZE", values_[duskverb::Freeze] > 0.5f },
            { "SYNC",   sync != 0 },
            { "BUS",    values_[duskverb::BusMode] > 0.5f },
        };
        float ax = ix0;
        for (const auto& flag : flags)
        {
            duskdaf::spacedText(panel_, dl, ax, annY, kTypeSmall,
                                flag.on ? kTheme.amber : amberGhost(), flag.text, -1, kBracketTracking);
            ax += spacedWidth(kTypeSmall, flag.text, kBracketTracking) + 18.0f;
        }
        const float cpu = duskVerbGetCpuLoad != nullptr
                        ? duskVerbGetCpuLoad(getPluginInstancePointer()) : 0.0f;
        char cpuText[32];
        std::snprintf(cpuText, sizeof(cpuText), "CPU %.1f%%", (double)std::clamp(cpu * 100.0f, 0.0f, 999.0f));
        valueText(dl, ix1, annY, kTypeSmall, amberDim(), cpuText, 1);

        drawHistory(dl, ix0, annY + 26.0f, ix1, gy1 - 12.0f);
    }

    // Decay time right-aligned at xRight, digits and unit on one baseline. The
    // digits open the same type-entry field as the DECAY knob.
    void drawDecayReadout(ImDrawList* dl, float xRight, float top)
    {
        char number[24], unit[8];
        formatDecayParts(plainOf(duskverb::Decay), number, sizeof(number), unit, sizeof(unit));
        const float numW  = textWidth(valueFonts_, kTypeHero, number);
        const float unitW = textWidth(valueFonts_, kTypeValue, unit);
        const float unitX = xRight - unitW;
        const float numX  = unitX - 6.0f - numW;
        valueText(dl, numX, top, kTypeHero, kTheme.amber, number, -1);
        valueText(dl, unitX, top + ascent(valueFonts_, kTypeHero) - ascent(valueFonts_, kTypeValue),
                  kTypeValue, amberDim(), unit, -1);

        char seed[48];
        formatPlainValue(currentEngine(), duskverb::Decay, plainOf(duskverb::Decay), seed, sizeof(seed));
        ImGui::SetCursorScreenPos(P(numX - 8.0f, top));
        ImGui::InvisibleButton("##decay_value_entry", ImVec2((xRight - numX + 8.0f) * s_, kTypeHero * s_));
        if (ImGui::IsItemClicked()) panel_.openValueEdit("##dvk_decay", values_[duskverb::Decay], seed);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }

    void drawHistory(ImDrawList* dl, float x0, float y0, float x1, float y1)
    {
        // Faint divisions: the graticule of a scope, so the trace has a scale.
        const ImU32 grid = fade(kTheme.amber, 0.08f);
        for (int i = 1; i < 8; ++i)
        {
            const float x = x0 + (x1 - x0) * (float)i / 8.0f;
            dl->AddLine(P(x, y0), P(x, y1), grid, 1.0f);
        }
        for (int i = 1; i < 4; ++i)
        {
            const float y = y0 + (y1 - y0) * (float)i / 4.0f;
            dl->AddLine(P(x0, y), P(x1, y), grid, 1.0f);
        }

        // The DSP samples output peak dB at a fixed 15 Hz cadence, independent
        // of audio block size and editor frame rate.
        int frames = 0;
        if (duskVerbGetTailHistory != nullptr)
            frames = duskVerbGetTailHistory(getPluginInstancePointer(), tail_, kTailFrames);
        if (frames < 2) return;

        // Same dB -> y curve as the JUCE TailMeter: -60..0 dB with a 1.6 power
        // so the interesting -40..0 band gets most of the height.
        const auto dbToY = [&](float db)
        {
            const float clamped = std::clamp(db, -60.0f, 0.0f);
            return y1 - std::pow((clamped + 60.0f) / 60.0f, 1.6f) * (y1 - y0);
        };
        const float step = (x1 - x0) / (float)(frames - 1);
        for (int i = 0; i < frames - 1; ++i)
        {
            const float xa = x0 + step * (float)i, xb = xa + step;
            dl->AddQuadFilled(P(xa, dbToY(tail_[i])), P(xb, dbToY(tail_[i + 1])), P(xb, y1), P(xa, y1),
                              fade(kTheme.amber, 0.16f));
        }
        dl->PathClear();
        for (int i = 0; i < frames; ++i)
            dl->PathLineTo(P(x0 + step * (float)i, dbToY(tail_[i])));
        dl->PathStroke(kTheme.amber, 0, 1.4f * s_);
    }

    void drawEngineCombo(ImDrawList* dl, float x0, float y0, float y1)
    {
        const int current = std::clamp((int)std::lround(values_[duskverb::Algorithm]),
                                       0, getNumAlgorithms() - 1);
        const char* name = getAlgorithmConfig(current).name;
        const float nameW = textWidth(labelFonts_, kTypeTitle, name);
        const float x1 = x0 + 6.0f + nameW + 26.0f;
        const bool open = beginCombo("##dv_engine", x0, y0, x1, y1, name, kTypeTitle,
                                     ComboSkin::Display, false, ImGuiComboFlags_NoArrowButton);
        const ImVec2 c = P(x0 + 6.0f + nameW + 13.0f, 0.5f * (y0 + y1) + 1.0f);
        const float d = 4.0f * s_;
        dl->AddTriangleFilled(ImVec2(c.x - d, c.y - 0.6f * d), ImVec2(c.x + d, c.y - 0.6f * d),
                              ImVec2(c.x, c.y + 0.6f * d), amberDim());
        if (open)
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

    // DECAY, SIZE and FREEZE, and below FREEZE the one switch the current engine
    // adds: GATE on the Gated engine, TONAL CORRECTION on the Hall engine
    // (gateApplies() / tonalCorrectionApplies(), the previous face's rules).
    // Elsewhere neither is shown. The decay value lives in the display, beside
    // the knob.
    void drawTank(ImDrawList* dl)
    {
        const duskverb::ParamDesc& d = duskverb::paramDesc(duskverb::Decay);
        const float lo = duskverb::knobMin(d), hi = duskverb::knobMax(d);
        knobBody(dl, kHeroCx, kHeroCy, kHeroR, /*hero*/ true, /*inactive*/ false);
        char decaySeed[48];
        formatPlainValue(currentEngine(), duskverb::Decay, plainOf(duskverb::Decay), decaySeed, sizeof(decaySeed));
        panel_.knob("##dvk_decay", (uint32_t)duskverb::Decay, lo, hi, kHeroCx, kHeroCy, kHeroR,
                    values_[duskverb::Decay], duskverb::knobDefault(d),
                    false, false, "%.2f", "", 0, true, false, tooltipFor(currentEngine(), duskverb::Decay),
                    false, 1.0f, 0.0f, d.name, true, decaySeed, true, 0.0f, 0.0f,
                    false, false, 9.5f, false, false,
                    &knobToPlainCb, &plainToKnobCb, this, false, &parseKnobText);
        const bool editing = panel_.isEditingValue("##dvk_decay");   // see knobCell()
        const bool hot = !editing && (ImGui::IsItemActive() || ImGui::IsItemHovered());
        if (!editing)
            knobPointer(dl, kHeroCx, kHeroCy, kHeroR, knobT(duskverb::Decay), /*hero*/ true, hot, false);
        duskdaf::spacedText(panel_, dl, kHeroCx, kHeroCy - kHeroR - kLegendGap, kTypeLegend,
                            kTheme.legend, "DECAY", 0, kLegendTracking);

        const float legendY = kHeroCy - kPrimaryR - kLegendGap;
        knobCell(dl, duskverb::Size, kSizeCx, kHeroCy, kPrimaryR, legendY,
                 kHeroCy + kPrimaryR + kValueGap, "SIZE");

        ledButton(dl, "##dv_freeze", duskverb::Freeze, kSwitchCx, legendY, legendY + 38.0f, "FREEZE");
        if (gateApplies())
            ledButton(dl, "##dv_gate", duskverb::GateEnabled, kSwitchCx,
                      legendY + 78.0f, legendY + 116.0f, "GATE");
        else if (tonalCorrectionApplies())   // two lines: one does not fit the column at this size
            ledButton(dl, "##dv_tonal", duskverb::TonalCorrection, kSwitchCx,
                      legendY + 78.0f, legendY + 136.0f, "TONAL", "CORRECTION");
    }

    //======================================================================
    // row B: INPUT | EARLY REFLECTIONS | MODULATION
    //======================================================================
    void drawInputSection(ImDrawList* dl)
    {
        bracket(dl, 0, 2, kRowB0, "INPUT");
        // PRE-DELAY stays lit under SYNC: the DSP follows a note value only
        // while the host supplies a tempo, and falls back to this knob otherwise,
        // which the editor cannot see. The lit selector shows the mode instead.
        const int sync = std::clamp((int)std::lround(values_[duskverb::PredelaySync]), 0, 6);
        rowKnob(dl, duskverb::Predelay, 0, kRowB0, kPrimaryR, "PRE-DELAY");

        const float cx = colX(1), cy = kRowB0 + kKnobDy;
        duskdaf::spacedText(panel_, dl, cx, kRowB0 + kLegendDy, kTypeLegend, kTheme.legend,
                            "SYNC", 0, kLegendTracking);
        dl->AddRectFilled(P(cx - 53.0f, cy - 16.0f), P(cx + 53.0f, cy + 16.0f), kTheme.faceDeep, 4.0f * s_);
        if (beginCombo("##dv_sync", cx - 50.0f, cy - 13.0f, cx + 50.0f, cy + 13.0f,
                       duskverb::kPreDelaySyncLabels[sync], kTypeSmall, ComboSkin::Panel, sync != 0))
        {
            for (int i = 0; i < 7; ++i)
                if (ImGui::Selectable(duskverb::kPreDelaySyncLabels[i], i == sync))
                {
                    setP(duskverb::PredelaySync, (float)i);
                    ImGui::CloseCurrentPopup();
                }
        }
        endCombo();

        rowKnob(dl, duskverb::Saturation, 2, kRowB0, kKnobR, "SATURATION");
    }

    void drawEarlySection(ImDrawList* dl)
    {
        bracket(dl, 3, 5, kRowB0, "EARLY REFLECTIONS");
        rowKnob(dl, duskverb::ErLevel, 3, kRowB0, kKnobR, "ER LEVEL");
        rowKnob(dl, duskverb::ErSize,  4, kRowB0, kKnobR, "ER SIZE");
        rowKnob(dl, duskverb::Diffusion, 5, kRowB0, kKnobR,
                labelFor(currentEngine(), duskverb::Diffusion, "DIFFUSION"));
    }

    void drawModulationSection(ImDrawList* dl)
    {
        // Re-titled GATE on the Gated engine, where these two knobs are the
        // gate's ATTACK and RELEASE (the JUCE editor does the same).
        bracket(dl, 6, 7, kRowB0, gateApplies() ? "GATE" : "MODULATION");
        rowKnob(dl, duskverb::ModDepth, 6, kRowB0, kKnobR,
                labelFor(currentEngine(), duskverb::ModDepth, "DEPTH"));
        rowKnob(dl, duskverb::ModRate, 7, kRowB0, kKnobR,
                labelFor(currentEngine(), duskverb::ModRate, "RATE"));
    }

    //======================================================================
    // row C: DAMPING | MACRO
    //======================================================================
    void drawDampingSection(ImDrawList* dl)
    {
        bracket(dl, 0, 4, kRowC0, "DAMPING");
        rowKnob(dl, duskverb::BassMult,      0, kRowC0, kKnobR, "BASS MULT");
        rowKnob(dl, duskverb::MidMult,       1, kRowC0, kKnobR,
                labelFor(currentEngine(), duskverb::MidMult, "MID MULT"));
        rowKnob(dl, duskverb::Damping,       2, kRowC0, kKnobR, "TREBLE MULT");
        rowKnob(dl, duskverb::Crossover,     3, kRowC0, kKnobR, "LOW XOVER");
        rowKnob(dl, duskverb::HighCrossover, 4, kRowC0, kKnobR, "HIGH XOVER");
    }

    void drawMacroSection(ImDrawList* dl)
    {
        bracket(dl, 5, 7, kRowC0, "MACRO");
        rowKnob(dl, duskverb::Tone,      5, kRowC0, kKnobR, "TONE");
        rowKnob(dl, duskverb::Character, 6, kRowC0, kKnobR, "CHARACTER");
        rowKnob(dl, duskverb::Duck,      7, kRowC0, kKnobR, "DUCK");
    }

    //======================================================================
    // row D: FILTER | OUTPUT
    //======================================================================
    void drawFilterSection(ImDrawList* dl)
    {
        bracket(dl, 0, 3, kRowD0, "FILTER");
        rowKnob(dl, duskverb::LoCut,          0, kRowD0, kKnobR, "LOW CUT");
        rowKnob(dl, duskverb::HiCut,          1, kRowD0, kKnobR, "HIGH CUT");
        rowKnob(dl, duskverb::MonoBelow,      2, kRowD0, kKnobR, "MONO BELOW");
        rowKnob(dl, duskverb::MonoBelowDepth, 3, kRowD0, kKnobR, "MONO DEPTH");
    }

    void drawOutputSection(ImDrawList* dl)
    {
        bracket(dl, 4, 7, kRowD0, "OUTPUT");
        // BUS outputs 100% wet whatever DRY / WET says, so that knob greys
        // while BUS is on.
        const bool bus = values_[duskverb::BusMode] > 0.5f;
        rowKnob(dl, duskverb::Width,    4, kRowD0, kKnobR, "WIDTH");
        rowKnob(dl, duskverb::Mix,      5, kRowD0, kPrimaryR, "DRY / WET", bus);
        rowKnob(dl, duskverb::GainTrim, 6, kRowD0, kKnobR, "TRIM");
        ledButton(dl, "##dv_bus", duskverb::BusMode, colX(7), kRowD0 + kLegendDy, kRowD0 + kKnobDy, "BUS");
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
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(winW, winH), fade(kTheme.well, 0.90f));
        // No input blocker here: it is submitted with the other modal blockers
        // before the controls, because an InvisibleButton submitted at this
        // point loses ImGui's hover race to the already-submitted knobs.

        const float px0 = 230.0f, py0 = 96.0f, px1 = 970.0f, py1 = 716.0f;
        const float cx = 0.5f * (px0 + px1);
        dl->AddRectFilled(P(px0, py0), P(px1, py1), kTheme.faceDeep, 8.0f * s_);
        dl->AddRect(P(px0, py0), P(px1, py1), buttonEdge(), 8.0f * s_, 0, s_);
        label(dl, cx, py0 + 18.0f, kTypeTitle, kTheme.value, "DUSKVERB 2", 0);

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
            { "Click the nameplate", "Supporters" },
            { "?  or Shift + /", "Show or hide this" },
        };
        const float top = py0 + 60.0f, lineH = 28.0f;
        for (int i = 0; i < (int)(sizeof(left) / sizeof(left[0])); ++i)
        {
            label(dl, cx - 24.0f, top + (float)i * lineH, kTypeLegend, kTheme.value, left[i].key, 1);
            valueText(dl, cx - 14.0f, top + (float)i * lineH, kTypeLegend, kTheme.legend, left[i].text, -1);
        }
        const float top2 = top + 8.0f * lineH;
        for (int i = 0; i < (int)(sizeof(right) / sizeof(right[0])); ++i)
        {
            label(dl, cx - 24.0f, top2 + (float)i * lineH, kTypeLegend, kTheme.value, right[i].key, 1);
            valueText(dl, cx - 14.0f, top2 + (float)i * lineH, kTypeLegend, kTheme.legend, right[i].text, -1);
        }

        valueText(dl, cx, py1 - 80.0f, kTypeLegend, kTheme.legendDim,
                  "GATE appears on the Gated engine; TONAL CORRECTION on the Hall engine.", 0);
        valueText(dl, cx, py1 - 56.0f, kTypeLegend, kTheme.legendDim,
                  "MONO DEPTH is always visible in FILTER. COPY preserves the active slot.", 0);
        valueText(dl, cx, py1 - 28.0f, kTypeSmall, kTheme.legendDim,
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
    bool    comboRowInk_ = false;
    ImFont* comboRowFont_ = nullptr;
    int     comboColors_ = 0;
    duskdaf::DuskImGuiTextInputFocus textInputFocus_;
    duskdaf::SupportersOverlay supporters_;

    duskdaf::LedLadderStyle meterStyle_ = [] {
        duskdaf::LedLadderStyle st;
        // Full-body rails: a finer ladder keeps the long meters readable while
        // the colour zones stay at the same FRACTIONS of the scale (see
        // DuskLedMeter.hpp). The zones take the panel's LED colours, unbloomed.
        st.segments = 24;
        st.gap = 2.0f;
        st.rounding = 1.0f;
        st.glow = false;
        const auto zone = [](ImU32 lit)
        { return duskdaf::LedLadderColors{ lit, blend(kTheme.well, lit, 0.11f), lit }; };
        st.greenZone  = zone(kTheme.signal);
        st.yellowZone = zone(kTheme.amber);
        st.redZone    = zone(kTheme.alarm);
        return st;
    }();
    duskdaf::LedLadder inMeter_, outMeter_;

    float  s_ = 1.0f;
    ImVec2 org_ = ImVec2(0, 0);
    bool   gripCursorSet_ = false;

    int         currentPreset_ = -1;
    std::string currentUserName_, currentUserPath_;
    std::string currentUserPathName_;   // the name currentUserPath_ was loaded or saved under
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
