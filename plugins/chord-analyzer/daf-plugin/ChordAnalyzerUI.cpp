// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components are attributed in shared-daf/THIRD_PARTY_LICENSES.md.
#include "DafUI.hpp"
#include "ChordAnalyzerAccess.hpp"
#include "ChordAnalyzerRuntime.hpp"
#include "ChordAnalyzerVersion.hpp"
#include "ChordMidiExport.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "DuskImGuiWidgets.hpp"      // DuskPanel: the design-space mapper the shared overlay draws through
#include "DuskSupportersOverlay.hpp" // shared DAF Patreon "Special Thanks" overlay
#include "ChordFontRegular.inc"
#include "ChordFontSemiBold.inc"
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

START_NAMESPACE_DAF
using namespace duskaudio::chord;
namespace
{
constexpr float designWidth = 960, designHeight = 720;

// A colour at an opacity from 0 to 1.
constexpr ImU32 withAlpha(ImU32 colour, float alpha)
{
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
}
// Part way from one colour to another, t from 0 (a) to 1 (b).
constexpr ImU32 mix(ImU32 a, ImU32 b, float t)
{
    ImU32 out = 0;
    for (const int shift : {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT, IM_COL32_A_SHIFT})
    {
        const float from = static_cast<float>((a >> shift) & 0xffu), to = static_cast<float>((b >> shift) & 0xffu);
        out |= static_cast<ImU32>(from + (to - from) * t + 0.5f) << shift;
    }
    return out;
}

// Every colour on the face is one of these roles; nothing below picks its own RGB.
// Amber is the one accent. It marks state (a held note, a ticked option, the
// previewing display, a control under the hand) and the detected chord name, the
// signature read-out of the chord display.
struct Theme
{
    ImU32 ground    = IM_COL32( 17,  17,  19, 255);  // window behind the sections
    ImU32 face      = IM_COL32( 29,  29,  32, 255);  // section cards, popups
    ImU32 well      = IM_COL32( 12,  12,  13, 255);  // chord display glass
    ImU32 edge      = IM_COL32( 59,  58,  57, 255);  // card outlines, hairline rules
    ImU32 field     = IM_COL32( 42,  42,  46, 255);  // combo and checkbox bodies
    ImU32 control   = IM_COL32( 48,  46,  43, 255);  // button bodies
    ImU32 hover     = IM_COL32( 92,  67,  35, 255);  // control under the pointer
    ImU32 press     = IM_COL32(123,  81,  34, 255);  // control held down
    ImU32 selected  = IM_COL32(105,  71,  33, 255);  // chosen row in an open list
    ImU32 legend    = IM_COL32(171, 169, 159, 255);  // legends, captions, secondary read-outs
    ImU32 value     = IM_COL32(239, 236, 226, 255);  // read-outs and control text
    ImU32 accent    = IM_COL32(255, 171,  65, 255);  // the one accent
    ImU32 accentKey = IM_COL32(211, 126,  36, 255);  // the accent on a black key
    ImU32 alarm     = IM_COL32(145,  46,  38, 255);  // armed Record
    ImU32 ivory     = IM_COL32(216, 213, 201, 255);  // white keys
    ImU32 ebony     = IM_COL32( 19,  19,  21, 255);  // black keys
    ImU32 staffLine = IM_COL32(108, 107, 102, 255);  // staff lines
    // Derived from the roles above.
    ImU32 selection  = withAlpha(accent, 0.35f);     // selected text in the save dialog
    ImU32 dragFrame  = withAlpha(accent, 0.45f);     // chord display under the pointer, a drag source
    ImU32 plateRule  = withAlpha(accent, 0.8f);      // nameplate accent rule
    ImU32 alarmHover = mix(alarm, value, 0.18f);     // armed Record under the pointer
    ImU32 alarmPress = mix(alarm, ground, 0.3f);     // armed Record held down
    // The family top row: Tape Echo 2's drawHeader colours, so the row reads the
    // same on every DAF port.
    ImU32 headerBand = IM_COL32( 14,  14,  15, 255);  // band behind the row and the status line; dialog title bar
    ImU32 stripLight = IM_COL32(205, 203, 197, 255);  // strip along the top edge, light at the top ...
    ImU32 stripDark  = IM_COL32( 91,  91,  91, 255);  // ... fading to dark (a plain gradient)
    ImU32 headerRule = IM_COL32( 91,  90,  88, 255);
    ImU32 shadow     = IM_COL32(  0,   0,   0, 160);  // under the header rule; dims behind the dialog
    ImU32 plateLight = IM_COL32( 37,  37,  38, 255);  // nameplate, lit from the top left
    ImU32 plateMid   = IM_COL32( 29,  29,  30, 255);
    ImU32 plateDark  = IM_COL32( 16,  16,  17, 255);
    ImU32 plateShade = IM_COL32( 19,  19,  20, 255);
    ImU32 plateEdge  = IM_COL32(185, 184, 180, 220);
};
constexpr Theme kTheme{};
// Every ImGui colour the face can show, so no stock blue leaks through a hover,
// a focus cursor, a list row, a scrollbar, a text selection or the save dialog.
struct StyleColour {ImGuiCol slot; ImU32 colour;};
constexpr StyleColour kStyleColours[] = {
    {ImGuiCol_Text, kTheme.value},                 {ImGuiCol_TextDisabled, kTheme.legend},
    {ImGuiCol_PopupBg, kTheme.face},               {ImGuiCol_Border, kTheme.edge},
    {ImGuiCol_FrameBg, kTheme.field},              {ImGuiCol_FrameBgHovered, kTheme.hover},
    {ImGuiCol_FrameBgActive, kTheme.press},        {ImGuiCol_TitleBg, kTheme.headerBand},
    {ImGuiCol_TitleBgActive, kTheme.headerBand},   {ImGuiCol_TitleBgCollapsed, kTheme.headerBand},
    {ImGuiCol_ScrollbarBg, kTheme.face},           {ImGuiCol_ScrollbarGrab, kTheme.edge},
    {ImGuiCol_ScrollbarGrabHovered, kTheme.hover}, {ImGuiCol_ScrollbarGrabActive, kTheme.press},
    {ImGuiCol_CheckMark, kTheme.accent},           {ImGuiCol_Button, kTheme.control},
    {ImGuiCol_ButtonHovered, kTheme.hover},        {ImGuiCol_ButtonActive, kTheme.press},
    {ImGuiCol_Header, kTheme.selected},            {ImGuiCol_HeaderHovered, kTheme.hover},
    {ImGuiCol_HeaderActive, kTheme.press},         {ImGuiCol_Separator, kTheme.edge},
    {ImGuiCol_TextSelectedBg, kTheme.selection},
    {ImGuiCol_NavCursor, kTheme.accent},           {ImGuiCol_ModalWindowDimBg, kTheme.shadow},
};

// Type scale, design px. The window cannot shrink below its design size, so these
// are also the on-screen sizes at the smallest window: on the face, legends 16 px
// or more and read-outs 17 px or more (owner floor, 2026-11-03). Readability sets
// them, not looks.
constexpr float kTypeLegend  = 16;  // section legends, octave names, clef names, display state
constexpr float kTypeVersion = 17;  // nameplate version (family top row)
constexpr float kTypeBody    = 18;  // ImGui controls, tooltips, save dialog, read-out lines, status, staff sharps
constexpr float kTypeBadge   = 20;  // nameplate model badge (family top row)
constexpr float kTypeBrand   = 22;  // DUSK AUDIO (family top row)
constexpr float kTypeTitle   = 24;  // nameplate title (family top row); Roman numeral
constexpr float kTypeHero    = 44;  // chord name; a name too long for the display shrinks,
constexpr float kTypeHeroMin = 20;  // never below this
constexpr float kTypeHeroMid = 32;  // a bake between kTypeTitle and kTypeHero for a shrunk name
// Barlow Condensed (ascent 1000, descent -200, cap height 700): the middle of the
// capitals sits this far below the text top, per px of size. Centres a label on a line.
constexpr float kCapMiddle = (1000.0f - 350.0f) / 1200.0f;
// The bakes are the type scale, made at the window's scale (see bakeFonts()).
// DuskSupportersOverlay draws its own text at 20 px and its heading at 24 px, on the
// kTypeBadge and kTypeTitle bakes; its sizes are shared code.
constexpr float kBoldSizes[] = {kTypeLegend, kTypeBadge, kTypeBrand, kTypeTitle, kTypeHeroMid, kTypeHero};
constexpr float kRegularSizes[] = {kTypeLegend, kTypeVersion, kTypeBody};

// Layout, design px; rectangles are x, y, width, height. tests/ChordAnalyzerUiTest.cpp
// clicks and hashes these in the real editor at 960x720 (Linux CI), as noted: move a
// control there too.
constexpr float kLeft = 20, kRight = 940;                  // outer margins
constexpr float kTextX = 24;                               // text outside a card
constexpr float kCardInset = 14, kCardLegendDy = 7;        // card edge to its legend
constexpr float kCardRounding = 7;
// Family top row, Tape Echo 2's drawHeader geometry.
constexpr float kHeaderH = 48, kTopStripH = 4, kRuleWidth = 1.2f;
constexpr float kPlateX0 = 28, kPlateX1 = 318, kPlateY0 = 8, kPlateY1 = 40, kPlateRounding = 3.5f;
constexpr float kPlateRuleX0 = 40, kPlateRuleX1 = 308, kPlateRuleY = 37;
constexpr float kTitleX = 42, kTitleY = 10;
constexpr float kBadgeMinX = 202, kBadgeY = 14;            // badge centre, never left of Tape Echo 2's
constexpr float kVersionX = 306, kVersionY = 17.5f;        // the version ends at kVersionX
constexpr float kBrandInset = 87, kBrandY = 12;            // DUSK AUDIO centres this far from the right edge
// Selector row. The test clicks each selector at y 88 and each option at y 91.
constexpr float kLegendY = 54, kFieldY = 75;
constexpr float kKeyRootX = 24, kKeyRootW = 120;           // clicked at x 80; hashed at 24, 75, 120, 27
constexpr float kKeyModeX = 154, kKeyModeW = 100;          // clicked at x 205
constexpr float kLevelX = 268, kLevelW = 225;              // clicked at x 350
constexpr float kInversionsX = 520;                        // clicked at x 531; hashed at 520, 78, 205, 30
constexpr float kSustainX = 730;                           // clicked at x 741
// Chord display.
constexpr float kDisplayY0 = 113, kDisplayY1 = 237;
constexpr float kHeroX = 36, kHeroY = 127, kHeroW = 710, kHeroH = 107;  // dragged from (120, 150); hashed at 36, 127, 710, 53
constexpr float kReadoutX = 38, kNumeralY = 186, kNotesY = 211;
constexpr float kFunctionX = 128, kFunctionY = 190, kFunctionGap = 16;  // the function clears the numeral by this
constexpr float kNotesClipY0 = 209, kNotesClipX1 = 748, kNotesClipY1 = 236;
constexpr float kDragFrameX0 = 28, kDragFrameY0 = 121, kDragFrameX1 = 750, kDragFrameY1 = 233, kDragFrameRounding = 5;
constexpr float kSideX = 770, kSideW = 154, kSideTagY = 123;  // the display's right-hand column
constexpr float kSaveChordY = 153, kSaveChordH = 34;       // clicked at (840, 169)
constexpr float kReturnY = 195, kReturnH = 28;             // clicked at (840, 208)
// Keyboard and staff cards, hashed at 20, 248, 506, 156 and 538, 248, 402, 156.
constexpr float kNotesY0 = 248, kNotesY1 = 404, kKeyboardX1 = 526, kStaffX0 = 538;
constexpr float kKeysX = 34, kKeysW = 478, kKeyTop = 282, kKeyBottom = 372, kBlackBottom = 338;
constexpr float kWhiteKeyGap = 1, kBlackKeyW = 0.64f;      // black key width, of a white key's
constexpr float kKeyRounding = 2, kOctaveLabelDy = 4;      // key bottom to its octave name
constexpr int kKeyboardLow = 48, kKeyboardHigh = 83;       // C3..B5 always show
constexpr float kStaffLineX0 = 615, kStaffLineX1 = 924;
constexpr float kStaffBottomY = 387, kStaffSpanH = 108;    // the shown positions fill this height
constexpr int kStaffLow = 16, kStaffHigh = 40, kStaffMargin = 2;  // E2..A5 always show
constexpr int kBassLine = 18, kTrebleLine = 30, kMiddleC = 28;    // bottom lines G2, E4; C4 up on the treble
constexpr int kStaffLines = 5, kLineStep = 2;              // a line on every second position
constexpr float kClefX = 550;
constexpr float kNoteX0 = 636, kNoteSpacing = 32, kNoteSpan = 284;  // heads step at most this far within the span
constexpr float kNoteRadius = 4.5f, kNoteRadiusPerStep = 0.9f, kLedgerHalfW = 7, kSharpGap = 2;
// History card, hashed at 20, 415, 920, 82.
constexpr float kHistoryY0 = 415, kHistoryY1 = 497;
constexpr float kClearHistoryX = 830, kClearHistoryY = kHistoryY0 + 4, kClearHistoryW = 96, kClearHistoryH = 28;  // clicked at (875, 433)
constexpr float kChipStripX = 32, kChipStripY = kHistoryY0 + 36, kChipStripW = 896, kChipStripH = 44;  // clicked and dragged at (42, 460)
constexpr float kChipH = 30, kChipStripScrollbar = 12;     // chips plus a scrollbar once the strip overflows
// Suggestion tiles, hashed at 20, 509, 920, 120.
constexpr float kTryY = 507, kTileY = 531, kTileW = 296, kTileH = 43, kTilePitchX = 312, kTilePitchY = 53;
constexpr std::size_t kTileColumns = 3, kTiles = 6;
// Transport row, hashed at 20, 644, 920, 55; clicked at y 675.
constexpr float kTransportRuleY = 640, kTransportY = 655, kTransportH = 32;
constexpr float kRecordX = kLeft, kRecordW = 132;          // clicked at x 80
constexpr float kClearX = 164, kClearW = 74;               // clicked at x 196
constexpr float kSaveSessionX = 250, kSaveSessionW = 200;  // clicked at x 350
constexpr float kDragSessionX = 462, kDragSessionW = 118;  // dragged from x 520
constexpr float kSummaryX = 598, kSummaryDy = 7;
// Status band.
constexpr float kFooterY = 695, kStatusDy = 3;
constexpr float kStatusClipY = 699;                        // first row below the transport hash (644..698)
// Save dialog (Linux without a desktop portal). With one message or none, its
// buttons sit at kDialogButtonsY: Replace clicked at (225, 405), Cancel at (724, 405).
constexpr float kDialogCentreX = 480, kDialogCentreY = 350, kDialogW = 640;
constexpr float kDialogInsetX = 16, kDialogLabelY = 38, kDialogFieldY = 60, kDialogFieldW = 608;
constexpr float kDialogMessageY = 96, kDialogButtonsY = 132, kDialogButtonGap = 18, kDialogBottom = 22;
constexpr float kDialogButtonW = 120, kDialogBackW = 100, kDialogButtonH = 30, kDialogCancelX = 504;
constexpr float kTooltipWrap = 350;
// ImGui spacing and rounding.
constexpr float kPopupPadX = 10, kPopupPadY = 8, kFramePadX = 8, kFramePadY = 5, kItemGapX = 8, kItemGapY = 6;
constexpr float kFrameRounding = 3, kPopupRounding = 4, kDialogRounding = 6, kScrollbarRounding = 3;

#if CHORD_ANALYZER_DAF_MIDI_MODE
constexpr const char* kModelBadge = "CA-2 MIDI";
#else
constexpr const char* kModelBadge = "CA-2";
#endif

bool blackKey(int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
int staffPosition(int note)
{
    constexpr int degrees[] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    return (note / 12 - 1) * 7 + degrees[note % 12];
}
}

class ChordAnalyzerUI : public UI
{
public:
    ChordAnalyzerUI() : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        for (unsigned i = 0; i < PARAM_COUNT; ++i) values[i] = parameterSpecs[i].initial;
        setGeometryConstraints(static_cast<uint32_t>(designWidth), static_cast<uint32_t>(designHeight), true);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
        if (const char* forced = std::getenv("DUSK_GL_MAX_TEXTURE"))
            if (const int size = std::atoi(forced); size > 0) maxTexture = size;
        bakeFonts(std::max(1.0f, static_cast<float>(getScaleFactor())));
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < PARAM_COUNT && std::isfinite(value)) values[index] = snapParameter(index, value);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename && *filename && !pendingBytes.empty())
        {
            try
            {
                status = midi::writeFile(std::filesystem::u8path(filename), pendingBytes)
                       ? "MIDI clip saved." : "Could not write the MIDI file. Choose a writable location.";
            }
            catch (...) {status = "Could not use that file path.";}
        }
        pendingBytes.clear();
    }
    // Re-bake once the window has settled at a new scale (unchanged for a frame), as
    // DuskVerb 2 does: during a resize drag the old faces stretch for a few frames,
    // then every glyph is native again. Runs before the ImGui frame with the GL
    // context current, the one safe point (DearImGui.hpp).
    void onImGuiPrepareFrame() override
    {
        const float now = windowScale();
        if (now <= 0) return;
        if (std::fabs(now - lastSeenScale) > 1e-4f) {lastSeenScale = now; return;}
        if (std::fabs(now - bakedScale) <= 1e-4f) return;
        bakeFonts(now);
        rebuildFontTexture();
    }
    void onImGuiDisplay() override
    {
        scale = windowScale();
        origin = {(getWidth() - designWidth * scale) * 0.5f, (getHeight() - designHeight * scale) * 0.5f};
        panel.begin(scale, origin, bold.pick(kTypeLegend * scale), nullptr);
        auto* const engine = runtime();
        AnalyzerView view;
        if (engine)
        {
            view = engine->view();
            // Seed a newly opened editor once. Afterwards host callbacks update
            // the UI mirror, retaining local edits between notifications.
            if (!inputsInitialized)
            {
                const auto inputs = engine->inputValues();
                for (unsigned i = 0; i < inputParameterCount; ++i) values[i] = inputs[i];
                inputsInitialized = true;
            }
        }
        else
        {
            view.chord.name = ChordAnalyzerText::decodeLabel(static_cast<int>(values[PARAM_DETECTED_CHORD]));
            view.chord.isValid = values[PARAM_DETECTED_ROOT] != 0;
        }
        // A Return/Clear action can reset preview later in this same frame.
        // Keep the displayed chord/voicing alive until drawing has finished.
        const bool previewing = preview.has_value();
        auto shown = view.chord;
        std::vector<ChordSuggestionText> previewSuggestions;
        const auto* displayedSuggestions = &view.suggestions;
        if (previewing)
        {
            ChordAnalyzerText context;
            context.setKey(static_cast<int>(values[PARAM_KEY_ROOT]), values[PARAM_KEY_MODE] != 0);
            shown = context.analyze(preview->midiNotes);
            previewSuggestions = context.getSuggestions(shown,
                static_cast<SuggestionCategory>(static_cast<int>(values[PARAM_SUGGESTION_LEVEL])));
            displayedSuggestions = &previewSuggestions;
        }

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({static_cast<float>(getWidth()), static_cast<float>(getHeight())});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("ChordAnalyzer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar(2);
        // Zero padding belongs to the root panel only; popups, tooltips and the
        // save dialog get room around their text.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {kPopupPadX * scale, kPopupPadY * scale});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {kFramePadX * scale, kFramePadY * scale});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {kItemGapX * scale, kItemGapY * scale});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kFrameRounding * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kPopupRounding * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kDialogRounding * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, kScrollbarRounding * scale);
        constexpr int styleVars = 7;
        for (const auto& style : kStyleColours) ImGui::PushStyleColor(style.slot, style.colour);
        ImFont* body = regular.pick(kTypeBody * scale);
        if (body) ImGui::PushFont(body);
        draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({0, 0}, {static_cast<float>(getWidth()), static_cast<float>(getHeight())}, kTheme.ground);

        // The supporters panel is modal: the face stays visible under its scrim but
        // takes no input. Snapshot before header(), where a title click opens it.
        const bool modalOpen = showSupporters;
        if (modalOpen) ImGui::BeginDisabled();
        header();
        controls();

        // The chord display, the face's signature: a darker glass well holding the
        // chord name, the Roman numeral and function, the voiced notes and, on the
        // right, the display state and the voicing's export.
        draw->AddRectFilled(point(kLeft, kDisplayY0), point(kRight, kDisplayY1), kTheme.well, kCardRounding * scale);
        draw->AddRect(point(kLeft, kDisplayY0), point(kRight, kDisplayY1), kTheme.edge, kCardRounding * scale);
        std::string label = shown.name;
        if (values[PARAM_SHOW_INVERSIONS] != 0 && shown.slashBass) label += shown.extensions;
        if (label.empty() || label == "-") label = "Play some notes";
        const float labelWidth = textWidth(kTypeHero, label.c_str(), true);
        // Accent only for a detected chord (a bare fifth counts: it reads as a power
        // chord, "C5"); a single note, any other interval, an unmatched set ("C?")
        // and "Play some notes" take the legend colour.
        text(kHeroX, kHeroY, labelWidth > kHeroW ? std::max(kTypeHeroMin, kTypeHero * kHeroW / labelWidth) : kTypeHero,
             shown.isValid ? kTheme.accent : kTheme.legend, label.c_str(), true);
        // The numeral is a read-out under the name; "-" and "?" are placeholders.
        text(kReadoutX, kNumeralY, kTypeTitle, shown.isValid ? kTheme.value : kTheme.legend, shown.romanNumeral.c_str(), true);
        const auto function = shown.isValid ? ChordAnalyzerText::functionToString(shown.function) : std::string{};
        const float functionX = std::max(kFunctionX, kReadoutX + textWidth(kTypeTitle, shown.romanNumeral.c_str(), true) + kFunctionGap);
        text(functionX, kFunctionY, kTypeBody, kTheme.legend, function.c_str());
        std::string notes;
        const auto& displayNotes = previewing ? shown.midiNotes : view.notes;
        for (int note : displayNotes)
        {
            if (!notes.empty()) notes += "  ";
            notes += ChordAnalyzerText::noteToName(note);
        }
        draw->PushClipRect(point(kHeroX, kNotesClipY0), point(kNotesClipX1, kNotesClipY1), true);
        text(kReadoutX, kNotesY, kTypeBody, kTheme.legend, notes.empty() ? "MIDI input / all channels" : notes.c_str());
        draw->PopClipRect();
        ImGui::SetCursorScreenPos(point(kHeroX, kHeroY));
        const bool chordReady = shown.isValid && !shown.midiNotes.empty();
        ImGui::BeginDisabled(!chordReady);
        ImGui::InvisibleButton("chordDrag", {kHeroW * scale, kHeroH * scale});
        // The voicing is a drag source: outline it under the pointer.
        if (chordReady && ImGui::IsItemHovered())
            draw->AddRect(point(kDragFrameX0, kDragFrameY0), point(kDragFrameX1, kDragFrameY1),
                          kTheme.dragFrame, kDragFrameRounding * scale, 0, scale);
        fileDragSource([&] {return midi::temporaryChordFile(shown);});
        ImGui::EndDisabled();
        tip((notes + "\nDrag this voicing into a DAW MIDI track, or use Save chord MIDI.").c_str());
        text(kSideX, kSideTagY, kTypeLegend, previewing ? kTheme.accent : kTheme.legend,
             previewing ? "HISTORY PREVIEW" : "LIVE ANALYSIS", true);
        if (button("Save chord MIDI...", kSideX, kSaveChordY, kSideW, kSaveChordH, chordReady))
            save(midi::chordBytes(shown), "chord.mid");
        tip("Export this voicing as a one-bar MIDI clip at 120 BPM.");
        if (previewing && button("Return to live", kSideX, kReturnY, kSideW, kReturnH)) preview.reset();

        keyboard(displayNotes, previewing);
        staff(displayNotes);
        history(view, engine);
        suggestions(*displayedSuggestions);
        recording(view, engine);
        saveDialog();
        statusLine();
        if (modalOpen) ImGui::EndDisabled();
        // Every frame, open or not: after a click closes it, it still blocks the face for a double-click time.
        duskdaf::drawSupportersOverlay(panel, draw, designWidth, designHeight, showSupporters,
                                       DAF_PLUGIN_NAME, CA2_VERSION_STRING, &supportersOverlay);
        if (body) ImGui::PopFont();
        ImGui::PopStyleColor(static_cast<int>(std::size(kStyleColours)));
        ImGui::PopStyleVar(styleVars);
        ImGui::End();
        textInputFocus.update(*this);
    }

private:
    ChordAnalyzerRuntime* runtime() const noexcept
    {
        if (chordAnalyzerGetRuntime)
            if (void* instance = getPluginInstancePointer()) return chordAnalyzerGetRuntime(instance);
        return nullptr;
    }
    float windowScale() const
    {
        return std::min(static_cast<float>(getWidth()) / designWidth,
                        static_cast<float>(getHeight()) / designHeight);
    }
    // Every type-scale size is baked at the window's scale, so each lands on a native
    // face (CrispFontSet::pick then scales by 1.0; ImGui rounds a baked size down to a
    // whole pixel, so text sits within 1 px of its layout size). That matters most for
    // ImGui's own widget text, which ImGui draws at the pushed face's baked size: a
    // face baked for another scale would leave it smaller than the controls around it.
    void bakeFonts(float bakeScale)
    {
        const duskdaf::EmbeddedFontRequest requests[] = {
            {kChordFontRegular, kChordFontRegular_len, kRegularSizes, static_cast<int>(std::size(kRegularSizes))},
            {kChordFontSemiBold, kChordFontSemiBold_len, kBoldSizes, static_cast<int>(std::size(kBoldSizes))}
        };
        duskdaf::CrispFontSet fonts[2];
        duskdaf::loadEmbeddedCrispFontSets(requests, 2, fonts, bakeScale, maxTexture);
        regular = fonts[0]; bold = fonts[1];
        panel.setFontSet(bold);
        bakedScale = bakeScale;
    }
    ImVec2 point(float x, float y) const {return {origin.x + x * scale, origin.y + y * scale};}
    ImFont* fontFor(float size, bool heavy) const
    {
        auto* font = (heavy ? bold : regular).pick(size * scale);
        return font ? font : ImGui::GetFont();
    }
    float textWidth(float size, const char* value, bool heavy = false) const
    {
        return fontFor(size, heavy)->CalcTextSizeA(size * scale, FLT_MAX, 0, value).x / scale;
    }
    // align: -1 starts at x, 0 centres on x, 1 ends at x. The origin snaps to a
    // whole device pixel, as DuskPanel::text does, so glyphs stay crisp.
    void text(float x, float y, float size, ImU32 color, const char* value, bool heavy = false, int align = -1)
    {
        ImVec2 at = point(x, y);
        if (align != -1) at.x -= (align == 0 ? 0.5f : 1.0f) * textWidth(size, value, heavy) * scale;
        draw->AddText(fontFor(size, heavy), size * scale, {std::floor(at.x + 0.5f), std::floor(at.y + 0.5f)}, color, value);
    }
    // The Dusk DAF top row, Tape Echo 2's drawHeader geometry. Chord Analyzer 2 has
    // no presets, so the row carries the nameplate and the brand only.
    void header()
    {
        draw->AddRectFilled(point(0, 0), point(designWidth, kHeaderH), kTheme.headerBand);
        draw->AddRectFilledMultiColor(point(0, 0), point(designWidth, kTopStripH),
                                      kTheme.stripLight, kTheme.stripLight, kTheme.stripDark, kTheme.stripDark);
        draw->AddLine(point(0, kHeaderH), point(designWidth, kHeaderH), kTheme.headerRule, kRuleWidth * scale);
        draw->AddLine(point(0, kHeaderH + 1), point(designWidth, kHeaderH + 1), kTheme.shadow, scale);
        // Nameplate: title, model badge and version on one baseline, tied together
        // by the accent rule.
        draw->AddRectFilledMultiColor(point(kPlateX0, kPlateY0), point(kPlateX1, kPlateY1),
                                      kTheme.plateLight, kTheme.plateMid, kTheme.plateDark, kTheme.plateShade);
        draw->AddRect(point(kPlateX0, kPlateY0), point(kPlateX1, kPlateY1), kTheme.plateEdge,
                      kPlateRounding * scale, 0, kRuleWidth * scale);
        draw->AddLine(point(kPlateRuleX0, kPlateRuleY), point(kPlateRuleX1, kPlateRuleY), kTheme.plateRule, kRuleWidth * scale);
        // Tape Echo 2 centres its badge on x202. This title is 41% wider than the
        // next longest in the family (MULTI-COMP) and 60% wider than TAPE ECHO, so
        // the badge centres in the gap the title leaves before the version, never
        // left of x202.
        constexpr const char* title = "CHORD ANALYZER";
        constexpr const char* version = "v" CA2_VERSION_STRING;
        const float titleEnd = kTitleX + textWidth(kTypeTitle, title, true);
        const float versionStart = kVersionX - textWidth(kTypeVersion, version);
        text(kTitleX, kTitleY, kTypeTitle, kTheme.value, title, true);
        text(std::max(kBadgeMinX, 0.5f * (titleEnd + versionStart)), kBadgeY, kTypeBadge, kTheme.value, kModelBadge, true, 0);
        text(kVersionX, kVersionY, kTypeVersion, kTheme.legend, version, false, 1);
        text(designWidth - kBrandInset, kBrandY, kTypeBrand, kTheme.value, "DUSK AUDIO", true, 0);
        // Clicking the nameplate opens the generated Patreon "Special Thanks" panel,
        // as on every DAF port.
        ImGui::SetCursorScreenPos(point(kPlateX0, kPlateY0));
        if (ImGui::InvisibleButton("##titlecredits", {(kPlateX1 - kPlateX0) * scale, (kPlateY1 - kPlateY0) * scale}))
        {
            supportersOverlay.resetInteraction();
            showSupporters = true;
        }
    }
    // Help and export messages in a footer band, the bottom counterpart of the
    // header band. The clip keeps every glyph below the transport row, whose
    // pixels the Linux editor test compares across states; it cuts nothing, as
    // the tallest printable ASCII glyph ($, 795 of 1200 units) starts 3.1 px
    // below the text top at this size.
    void statusLine()
    {
        draw->AddRectFilled(point(0, kFooterY), point(designWidth, designHeight), kTheme.headerBand);
        draw->AddLine(point(0, kFooterY), point(designWidth, kFooterY), kTheme.edge, scale);
        draw->PushClipRect(point(kLeft, kStatusClipY), point(kRight, designHeight), true);
        text(kTextX, kFooterY + kStatusDy, kTypeBody, kTheme.legend,
             status.empty() ? "Hover a control or suggestion for help." : status.c_str());
        draw->PopClipRect();
    }
    void card(float x0, float y0, float x1, float y1)
    {
        draw->AddRectFilled(point(x0, y0), point(x1, y1), kTheme.face, kCardRounding * scale);
        draw->AddRect(point(x0, y0), point(x1, y1), kTheme.edge, kCardRounding * scale);
    }
    bool button(const char* label, float x, float y, float w, float h, bool enabled = true)
    {
        ImGui::SetCursorScreenPos(point(x, y));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(label, {w * scale, h * scale});
        ImGui::EndDisabled();
        return clicked;
    }
    void tip(const char* value)
    {
        // The face is disabled under the supporters panel; its help must not float
        // over the panel.
        if (showSupporters) return;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(kTooltipWrap * scale);
            ImGui::TextUnformatted(value); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
    }
    void set(unsigned id, float value)
    {
        values[id] = snapParameter(id, value);
        editParameter(id, true); setParameterValue(id, values[id]); editParameter(id, false);
    }
    void combo(unsigned id, float x, float width, const char* help)
    {
        const auto labels = parameterChoices(id);
        int value = static_cast<int>(values[id]);
        ImGui::SetCursorScreenPos(point(x, kFieldY)); ImGui::SetNextItemWidth(width * scale);
        ImGui::PushID(static_cast<int>(id));
        // Show every choice (12 keys) instead of ImGui's default 8 rows and a scrollbar.
        const int count = static_cast<int>(labels.count);
        if (ImGui::Combo("##choice", &value, labels.labels, count, count)) set(id, static_cast<float>(value));
        ImGui::PopID(); tip(help);
    }
    void controls()
    {
        text(kKeyRootX, kLegendY, kTypeLegend, kTheme.legend, "KEY", true);
        text(kLevelX, kLegendY, kTypeLegend, kTheme.legend, "SUGGESTIONS", true);
        combo(PARAM_KEY_ROOT, kKeyRootX, kKeyRootW, "Key root for Roman numerals and harmonic suggestions.");
        combo(PARAM_KEY_MODE, kKeyModeX, kKeyModeW, "Analyze harmony in a major or minor key.");
        combo(PARAM_SUGGESTION_LEVEL, kLevelX, kLevelW, "Basic: common progressions. Intermediate adds borrowed chords; Advanced adds chromatic choices.");
        bool inversions = values[PARAM_SHOW_INVERSIONS] != 0;
        ImGui::SetCursorScreenPos(point(kInversionsX, kFieldY));
        if (ImGui::Checkbox("Show inversions", &inversions)) set(PARAM_SHOW_INVERSIONS, inversions ? 1.0f : 0.0f);
        tip("Append the voiced bass note after a slash in the displayed chord name.");
        bool sustain = values[PARAM_RESPECT_SUSTAIN] != 0;
        ImGui::SetCursorScreenPos(point(kSustainX, kFieldY));
        if (ImGui::Checkbox("Respect sustain", &sustain)) set(PARAM_RESPECT_SUSTAIN, sustain ? 1.0f : 0.0f);
        tip("CC64 holds released keys on each MIDI channel. Turning this off releases notes held only by the pedal.");
    }
    void keyboard(const std::vector<int>& notes, bool previewing)
    {
        card(kLeft, kNotesY0, kKeyboardX1, kNotesY1);
        text(kLeft + kCardInset, kNotesY0 + kCardLegendDy, kTypeLegend, kTheme.legend,
             previewing ? "PREVIEW VOICING" : "HELD NOTES", true);
        int low = kKeyboardLow, high = kKeyboardHigh;
        for (int n : notes) {low = std::min(low, (n / 12) * 12); high = std::max(high, std::min(127, (n / 12) * 12 + 11));}
        int whites = 0;
        for (int n = low; n <= high; ++n) if (!blackKey(n)) ++whites;
        const float width = kKeysW / static_cast<float>(whites);
        const auto active = [&](int n) {return std::find(notes.begin(), notes.end(), n) != notes.end();};
        int index = 0;
        for (int n = low; n <= high; ++n)
            if (!blackKey(n))
            {
                const float x = kKeysX + index++ * width;
                draw->AddRectFilled(point(x, kKeyTop), point(x + width - kWhiteKeyGap, kKeyBottom),
                                    active(n) ? kTheme.accent : kTheme.ivory, kKeyRounding * scale);
                // Octave names sit under their C, so they keep their size however
                // narrow the keys get when held notes widen the range.
                if (n % 12 == 0)
                    text(x + 0.5f * (width - kWhiteKeyGap), kKeyBottom + kOctaveLabelDy, kTypeLegend, kTheme.legend,
                         ChordAnalyzerText::noteToName(n).c_str(), false, 0);
            }
        index = 0;
        for (int n = low; n <= high; ++n)
            if (!blackKey(n)) ++index;
            else
            {
                const float x = kKeysX + (index - 0.5f * kBlackKeyW) * width;
                draw->AddRectFilled(point(x, kKeyTop), point(x + width * kBlackKeyW, kBlackBottom),
                                    active(n) ? kTheme.accentKey : kTheme.ebony, kKeyRounding * scale);
            }
    }
    void staff(const std::vector<int>& notes)
    {
        card(kStaffX0, kNotesY0, kRight, kNotesY1);
        text(kStaffX0 + kCardInset, kNotesY0 + kCardLegendDy, kTypeLegend, kTheme.legend, "STAFF / C4 IS MIDDLE C", true);
        int low = kStaffLow, high = kStaffHigh;
        for (int n : notes)
        {
            low = std::min(low, staffPosition(n) - kStaffMargin);
            high = std::max(high, staffPosition(n) + kStaffMargin);
        }
        const float step = kStaffSpanH / static_cast<float>(high - low);
        const auto y = [&](int position) {return kStaffBottomY - (position - low) * step;};
        for (int base : {kBassLine, kTrebleLine}) for (int i = 0; i < kStaffLines; ++i)
            draw->AddLine(point(kStaffLineX0, y(base + i * kLineStep)), point(kStaffLineX1, y(base + i * kLineStep)),
                          kTheme.staffLine);
        // Each clef name centres on its staff's middle line (B4, D3).
        constexpr int middleLine = kStaffLines / 2 * kLineStep;
        text(kClefX, y(kTrebleLine + middleLine) - kTypeLegend * kCapMiddle, kTypeLegend, kTheme.legend, "Treble");
        text(kClefX, y(kBassLine + middleLine) - kTypeLegend * kCapMiddle, kTypeLegend, kTheme.legend, "Bass");
        const float spacing = std::min(kNoteSpacing, kNoteSpan / static_cast<float>(std::max<std::size_t>(1, notes.size())));
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const int n = notes[i], pos = staffPosition(n), base = pos >= kMiddleC ? kTrebleLine : kBassLine;
            const float x = kNoteX0 + static_cast<float>(i) * spacing;
            for (int ledger = base - kLineStep; ledger >= pos; ledger -= kLineStep)
                draw->AddLine(point(x - kLedgerHalfW, y(ledger)), point(x + kLedgerHalfW, y(ledger)), kTheme.legend);
            for (int ledger = base + kStaffLines * kLineStep; ledger <= pos; ledger += kLineStep)
                draw->AddLine(point(x - kLedgerHalfW, y(ledger)), point(x + kLedgerHalfW, y(ledger)), kTheme.legend);
            const float radius = std::min(kNoteRadius, step * kNoteRadiusPerStep);
            draw->AddCircleFilled(point(x, y(pos)), radius * scale, kTheme.accent);
            // The sharp reads out the held note, so it takes the read-out size.
            if (blackKey(n))
                text(x - radius - kSharpGap, y(pos) - kTypeBody * kCapMiddle, kTypeBody, kTheme.legend, "#", false, 1);
        }
    }
    void history(const AnalyzerView& view, ChordAnalyzerRuntime* engine)
    {
        card(kLeft, kHistoryY0, kRight, kHistoryY1);
        text(kLeft + kCardInset, kHistoryY0 + kCardLegendDy, kTypeLegend, kTheme.legend, "RECENT CHORDS", true);
        if (button("Clear history", kClearHistoryX, kClearHistoryY, kClearHistoryW, kClearHistoryH, engine != nullptr))
        {engine->clearHistory(); preview.reset();}
        // The strip is a child window, and ImGui draws a child above its parent's
        // draw list, so above the supporters scrim: leave it out while that is up.
        if (showSupporters) return;
        ImGui::SetCursorScreenPos(point(kChipStripX, kChipStripY));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, kChipStripScrollbar * scale);
        ImGui::BeginChild("history", {kChipStripW * scale, kChipStripH * scale}, false, ImGuiWindowFlags_HorizontalScrollbar);
        if (view.history.empty()) ImGui::TextDisabled("Distinct chords appear here as you play.");
        for (std::size_t i = 0; i < view.history.size(); ++i)
        {
            if (i != 0) ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            const bool clicked = ImGui::Button(view.history[i].name.c_str(), {0, kChipH * scale});
            const bool dragged = fileDragSource([&] {return midi::temporaryChordFile(view.history[i]);});
            if (clicked && !dragged) preview = view.history[i];
            tip("Click to preview this chord. Drag it into a DAW MIDI track to export its original voicing.");
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }
    void suggestions(const std::vector<ChordSuggestionText>& items)
    {
        text(kTextX, kTryY, kTypeLegend, kTheme.legend, "TRY NEXT", true);
        for (std::size_t i = 0; i < kTiles; ++i)
        {
            const float x = kLeft + static_cast<float>(i % kTileColumns) * kTilePitchX;
            const float y = kTileY + static_cast<float>(i / kTileColumns) * kTilePitchY;
            ImGui::PushID(static_cast<int>(i));
            if (i < items.size())
            {
                const auto& suggestion = items[i];
                const auto label = suggestion.romanNumeral + "   " + suggestion.chordName;
                if (button(label.c_str(), x, y, kTileW, kTileH)) status = suggestion.reason;
                tip(suggestion.reason.c_str());
            }
            else button("-", x, y, kTileW, kTileH, false);
            ImGui::PopID();
        }
    }
    void recording(const AnalyzerView& view, ChordAnalyzerRuntime* engine)
    {
        draw->AddLine(point(kLeft, kTransportRuleY), point(kRight, kTransportRuleY), kTheme.edge);
        // Armed, Record turns alarm red, and its hover and press follow that red.
        if (view.recording)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kTheme.alarm);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kTheme.alarmHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kTheme.alarmPress);
        }
        if (button(view.recording ? "Stop recording" : "Record", kRecordX, kTransportY, kRecordW, kTransportH, engine != nullptr))
        {
            if (view.recording) engine->stopRecording();
            else engine->startRecording(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }
        if (view.recording) ImGui::PopStyleColor(3);
        tip("Record distinct played chords. Stop includes the chord you are still holding.");
        if (button("Clear", kClearX, kTransportY, kClearW, kTransportH, engine != nullptr)) engine->clearRecording();
        tip("Clear the recorded progression. Recording remains armed if it is running.");
        if (button("Save progression MIDI...", kSaveSessionX, kTransportY, kSaveSessionW, kTransportH,
                   engine && view.recordedEvents > 0))
        {
            const auto session = engine->session();
            save(midi::sessionBytes(session.events, session.tempoBPM), "chord_progression.mid");
        }
        tip("Export completed chords at the recording's captured tempo. Stop first to include the held final chord.");
        button("Drag MIDI", kDragSessionX, kTransportY, kDragSessionW, kTransportH, engine && view.recordedEvents > 0);
        fileDragSource([&] {return midi::temporarySessionFile(engine->session());});
        tip("Drag the progression into a DAW MIDI track at the recording's captured tempo. Stop first to include the held final chord.");
        char summary[128];
        std::snprintf(summary, sizeof(summary), "%s  /  %d events  /  Host %.1f BPM", view.recording ? "RECORDING" : "READY", view.recordedEvents, view.tempoBPM);
        text(kSummaryX, kTransportY + kSummaryDy, kTypeBody, view.recording ? kTheme.accent : kTheme.legend, summary);
    }
    template<class MakeFile>
    bool fileDragSource(MakeFile makeFile)
    {
        const ImGuiID item = ImGui::GetItemID();
        if (ImGui::IsItemActivated()) {dragItem = item; dragConsumed = false;}
        if (ImGui::IsItemActive() && dragItem == item && !dragConsumed
            && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4 * scale))
        {
            dragConsumed = true;
            try
            {
                const auto file = makeFile();
                if (file.empty()) status = "Could not create the MIDI clip.";
                else
                {
                    const auto path = file.u8string();
                    status = getWindow().startFileDrag(path.c_str())
                           ? "Drop the MIDI clip into a DAW track."
                           : "Could not start MIDI dragging. Use Save MIDI to export the clip.";
                }
            }
            catch (...) {status = "Could not create the MIDI clip.";}
            // The native drag owns the physical release, which may occur in
            // another app. A consumed history drag must not become a click.
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        }
        return dragItem == item && dragConsumed;
    }
    void save(midi::Bytes bytes, const char* defaultName)
    {
        if (bytes.empty()) {status = "No completed notes to export."; return;}
        pendingBytes = std::move(bytes);
        FileBrowserOptions options; options.saving = true; options.defaultName = defaultName; options.title = "Export MIDI clip";
        if (!openFileBrowser(options))
        {
            // X11's native fallback only opens files; without a desktop portal
            // it cannot save. Keep export usable there with an explicit path.
            try
            {
                std::error_code error;
                auto directory = std::filesystem::current_path(error);
                if (const char* home = std::getenv("HOME")) directory = std::filesystem::u8path(home);
                if (std::filesystem::is_directory(directory / "Documents", error)) directory /= "Documents";
                const auto utf8 = (directory / defaultName).u8string();
                if (utf8.size() < savePath.size()) std::snprintf(savePath.data(), savePath.size(), "%s", utf8.c_str());
                else savePath[0] = '\0';
            }
            catch (...) {savePath[0] = '\0';}
            overwrite = false; saveError.clear(); saveRequested = true;
        }
    }
    void saveDialog()
    {
        if (saveRequested) {ImGui::OpenPopup("Save MIDI clip"); saveRequested = false;}
        // Each message takes its own row. The buttons stay at kDialogButtonsY below
        // one message or none and move down, with the dialog, below a second one
        // (a replacement that then fails to write), so no message runs into them.
        const float wrap = (kDialogW - kPopupPadX - kDialogInsetX) * scale;
        float messages = overwrite ? ImGui::GetTextLineHeight() : 0;
        if (!saveError.empty())
            messages += (overwrite ? ImGui::GetStyle().ItemSpacing.y : 0)
                      + ImGui::CalcTextSize(saveError.c_str(), nullptr, false, wrap).y;
        const float buttonsY = messages > 0
            ? std::max(kDialogButtonsY, kDialogMessageY + messages / scale + kDialogButtonGap) : kDialogButtonsY;
        ImGui::SetNextWindowPos(point(kDialogCentreX, kDialogCentreY), ImGuiCond_Appearing, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({kDialogW * scale, (buttonsY + kDialogButtonH + kDialogBottom) * scale}, ImGuiCond_Always);
        bool open = true;
        if (ImGui::BeginPopupModal("Save MIDI clip", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetCursorPos({kDialogInsetX * scale, kDialogLabelY * scale});
            ImGui::TextUnformatted("Destination (.mid)");
            ImGui::SetCursorPos({kDialogInsetX * scale, kDialogFieldY * scale});
            ImGui::SetNextItemWidth(kDialogFieldW * scale);
            const bool enter = ImGui::InputText("##destination", savePath.data(), savePath.size(),
                ImGuiInputTextFlags_EnterReturnsTrue | (overwrite ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_AutoSelectAll));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
            ImGui::SetCursorPos({kDialogInsetX * scale, kDialogMessageY * scale});
            if (overwrite) ImGui::TextUnformatted("This file already exists. Replace it?");
            if (!saveError.empty())
            {
                // A new line starts at the window padding; keep the error on the inset.
                ImGui::SetCursorPosX(kDialogInsetX * scale);
                ImGui::TextWrapped("%s", saveError.c_str());
            }
            ImGui::SetCursorPos({kDialogInsetX * scale, buttonsY * scale});
            const bool clicked = ImGui::Button(overwrite ? "Replace file" : "Save", {kDialogButtonW * scale, kDialogButtonH * scale});
            if (enter || clicked)
            {
                try
                {
                    const auto destination = std::filesystem::u8path(savePath.data());
                    std::error_code error;
                    const bool exists = std::filesystem::exists(destination, error);
                    if (destination.empty() || error || std::filesystem::is_directory(destination, error))
                        saveError = "Choose a writable file path.";
                    else if (exists && !overwrite) {overwrite = true; saveError.clear();}
                    else if (midi::writeFile(destination, pendingBytes))
                    {
                        pendingBytes.clear(); status = "MIDI clip saved."; ImGui::CloseCurrentPopup();
                    }
                    else saveError = "Could not write the MIDI file. Choose a writable location.";
                }
                catch (...) {saveError = "Could not use that file path.";}
            }
            if (overwrite)
            {
                ImGui::SameLine();
                if (ImGui::Button("Back", {kDialogBackW * scale, kDialogButtonH * scale})) overwrite = false;
            }
            ImGui::SetCursorPos({kDialogCancelX * scale, buttonsY * scale});
            if (ImGui::Button("Cancel", {kDialogButtonW * scale, kDialogButtonH * scale}))
            {pendingBytes.clear(); ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        if (!open) pendingBytes.clear();
    }
    std::array<float, PARAM_COUNT> values{};
    std::optional<ChordDescription> preview;
    midi::Bytes pendingBytes;
    std::string status;
    std::array<char, 4096> savePath{};
    std::string saveError;
    bool saveRequested = false, overwrite = false;
    bool inputsInitialized = false;
    ImGuiID dragItem = 0;
    bool dragConsumed = false;
    duskdaf::DuskImGuiTextInputFocus textInputFocus;
    duskdaf::DuskPanel panel;                      // design-space mapper for the shared overlay
    duskdaf::SupportersOverlay supportersOverlay;
    bool showSupporters = false;                   // Patreon "Special Thanks" panel (click the nameplate)
    duskdaf::CrispFontSet regular, bold;
    GLint maxTexture = 0;                          // GL_MAX_TEXTURE_SIZE, for the atlas fit in bakeFonts()
    float bakedScale = 0, lastSeenScale = 0;       // window scale the faces were baked for; the last one seen
    float scale = 1;
    ImVec2 origin{};
    ImDrawList* draw = nullptr;
};

UI* createUI() {return new ChordAnalyzerUI;}
END_NAMESPACE_DAF
