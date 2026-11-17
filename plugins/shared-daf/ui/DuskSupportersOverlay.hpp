// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskSupportersOverlay.hpp — framework-free (DAF/Dear ImGui) Patreon "Special Thanks"
// credits overlay. The non-JUCE counterpart of plugins/shared/SupportersOverlay.h, so
// DAF ports show the same click-title-to-see-supporters panel as the JUCE plugins.
//
// Usage: call it every frame, open or not, LAST (after the face; a resize grip may follow):
//     duskdaf::drawSupportersOverlay(panel, dl, kDesignW, kDesignH, showSupporters,
//                                    "TapeMachine 2", "1.0.1", &supportersOverlay);
// Open it from a title/logo hit-test:  if (titleClicked) showSupporters = true;
// While `open` is true it draws a scrim over the UI: draw the face under it inside
// ImGui::BeginDisabled()/EndDisabled() then. A click that closes it clears `open`
// at once, so the face is drawn as closed from the next frame, and for a double-click
// time after that click the call keeps the face from taking clicks. A caller that
// calls it only while `open` is true gets the older close instead: `open` stays true,
// and the face disabled, until that time is over.
// A SupportersOverlay passed as the last argument keeps the pointer state between
// frames and can put an action link in the footer (setActionLink); without one,
// that state lives in ImGui's storage for the calling window, keyed by `open`.
// Reads the generated, JUCE-free duskdaf::patreonTiers() list. Reusable across all DAF UIs.

#pragma once

#include "DuskImGuiWidgets.hpp"   // duskdaf::DuskPanel (crisp text + P()/scale); requires imgui.h pre-included
#include "PatreonBackersDaf.hpp"  // duskdaf::patreonTiers() — generated, framework-free

#include <algorithm>   // std::min, std::max
#include <cfloat>      // FLT_MAX
#include <cmath>       // std::floor
#include <cstdio>      // std::snprintf
#include <cstring>     // std::strcmp
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace duskdaf
{

class SupportersOverlay;

// Draws the overlay (see the definition below for how it closes).
inline void drawSupportersOverlay(DuskPanel& panel, ImDrawList* dl,
                                  float designW, float designH, bool& open,
                                  const char* pluginName, const char* version = "",
                                  SupportersOverlay* overlay = nullptr);

// Persistent action state for the immediate-mode overlay. The setter mirrors
// the JUCE SupportersOverlay API while the draw function remains compatible
// with existing callers that do not need an action.
class SupportersOverlay
{
public:
    void setActionLink(std::string label, std::function<void()> onClick)
    {
        actionLabel = std::move(label);
        onActionClick = std::move(onClick);
    }

    bool hasActionLink() const noexcept
    {
        return !actionLabel.empty() && static_cast<bool>(onActionClick);
    }

    const std::string& actionLinkLabel() const noexcept { return actionLabel; }
    void triggerAction() const { if (onActionClick) onActionClick(); }

    // Forgets a press in flight and a close in progress. The overlay also starts
    // afresh by itself whenever it opens, so calling this when opening it is no
    // longer required, only harmless.
    void resetInteraction() noexcept { interaction = Interaction{}; }

private:
    friend void drawSupportersOverlay(DuskPanel&, ImDrawList*, float, float, bool&,
                                      const char*, const char*, SupportersOverlay*);

    // What the press in flight began on. The press is recorded and acted on at
    // its release, so a click is a press and a release on the same target, as
    // with an ImGui button.
    enum class Press { None, Dismiss, Action };

    // The pointer state drawSupportersOverlay keeps from one frame to the next. Its timers count
    // seconds from an event and advance by each frame's DeltaTime; the opening one starts from
    // ImGui's own time of the opening press, taken as a difference of two doubles. A float copy of
    // ImGui::GetTime() itself would be too coarse: DAF's ImGui clock counts from the system's boot,
    // and as a float it steps by 1/128 s after 18 hours of uptime and by a quarter second after
    // 24 days.
    struct Interaction
    {
        int lastFrame = -1;              // the ImGui frame of the last call
        bool shown = false;              // `open` was true at the end of the last call
        Press press = Press::None;
        float sinceOpeningPress = 0.0f;  // since the opening press, or the last press chained to it
        bool closing = false;            // closed by a click; the face takes no clicks yet
        float sinceDismiss = 0.0f;       // since the closing release, or a later press
    };

    std::string actionLabel;
    std::function<void()> onActionClick;
    Interaction interaction;
    bool calledWhileClosed = false;      // the caller calls every frame (see the usage note)
};

// Per-tier accent colour, matched to the JUCE SupportersOverlay palette.
inline ImU32 supporterTierColor(const char* title) noexcept
{
    if (std::strcmp(title, "PATRONS") == 0)             return IM_COL32(255, 215,   0, 255);  // gold
    if (std::strcmp(title, "SUPPORTERS") == 0)          return IM_COL32(  0, 170, 255, 255);  // blue
    if (std::strcmp(title, "FOUNDING SUPPORTERS") == 0) return IM_COL32(106, 196, 126, 255);  // green
    if (std::strcmp(title, "PAST SUPPORTERS") == 0)     return IM_COL32(140, 140, 140, 255);  // grey
    return IM_COL32(200, 200, 204, 255);
}

// The overlay's colours by role, at the values it has always drawn. The close
// hint and the credit now take the caption grey: at 96 and 80 they were under
// 3:1 on the card, which a larger size alone does not make readable.
struct SupportersTheme
{
    ImU32 scrim     = IM_COL32( 16,  16,  16, 242);
    ImU32 card      = IM_COL32( 30,  30,  32, 255);
    ImU32 cardEdge  = IM_COL32( 80,  80,  84, 255);   // card border, footer separators, scrollbar
    ImU32 rule      = IM_COL32( 58,  58,  58, 255);
    ImU32 heading   = IM_COL32(232, 232, 232, 255);
    ImU32 caption   = IM_COL32(120, 120, 120, 255);   // subtitle, close hint, credit
    ImU32 name      = IM_COL32(206, 206, 206, 255);
    ImU32 pastName  = IM_COL32(130, 130, 130, 255);
    ImU32 link      = IM_COL32(184, 144,  96, 255);
    ImU32 linkHover = IM_COL32(218, 178, 126, 255);
    ImU32 clear     = IM_COL32(  0,   0,   0,   0);
};

// Type scale, in design px. The readability floor (owner, 2026-11-03) is 16 px
// for legends and 17 px for values on screen at a plugin's SMALLEST window; the
// names and tier titles count as values. One size for all of it, 20, still reads
// 17.0 px at a minimum window of 0.85x the design size, which covers the ports
// whose minimum is the design size and DuskVerb 2 (0.875x: 17.5 px). A port
// whose minimum is smaller (TapeMachine 2 at 0.5x and 4K EQ 2 at 0.58x, as of
// 2026-11) gets less there: nothing this function is handed says what a plugin's
// minimum is, and DAF sizes windows in backing pixels, so it cannot measure
// points on screen either.
struct SupportersType
{
    static constexpr float heading = 24.0f;
    static constexpr float text    = 20.0f;   // subtitle, tier titles, names, footer
};

// Spacing, in design px. Tape Echo 2's 340 px canvas is the shortest; once the
// list outgrows a card it scrolls.
struct SupportersLayout
{
    static constexpr float marginX    = 28.0f;    // card to canvas edge, at least
    static constexpr float marginY    = 8.0f;
    static constexpr float maxCardW   = 880.0f;
    static constexpr float padX       = 24.0f;    // text inset inside the card
    static constexpr float ruleInset  = 26.0f;
    static constexpr float padTop     = 12.0f;
    static constexpr float padBottom  = 10.0f;
    static constexpr float headingGap = 2.0f;     // heading to subtitle
    static constexpr float ruleGap    = 8.0f;     // text block to rule and rule to text block
    static constexpr float footerGap  = 8.0f;     // footer rule to footer text
    static constexpr float rowPitch   = 26.0f;    // row to row, and tier title to its first row
    static constexpr float tierGap    = 12.0f;    // last row of a tier to the next title
    static constexpr float itemGap    = 22.0f;    // between items on a row; a dot sits in the middle
    static constexpr float radius     = 11.0f;
    static constexpr float edge       = 1.6f;
    static constexpr float scrollbarW = 8.0f;
};

namespace supporters_detail
{
// Width of txt drawn at px design px, in design px.
inline float textWidth(const DuskPanel& panel, const char* txt, float px)
{
    const float s = panel.scale();
    const float size = px * s;
    return panel.pickFont(size)->CalcTextSizeA(size, FLT_MAX, 0.0f, txt).x / s;
}

// txt drawn at px design px with its top-left corner at the screen point `at`,
// snapped to a whole device pixel as DuskPanel::text does.
inline void drawText(ImDrawList* dl, const DuskPanel& panel, ImVec2 at, float px, ImU32 col,
                     const char* txt)
{
    const float size = px * panel.scale();
    dl->AddText(panel.pickFont(size), size,
                ImVec2(std::floor(at.x + 0.5f), std::floor(at.y + 0.5f)), col, txt);
}

// Breaks count items into rows no wider than maxW, as many per row as fit with
// gap between neighbours (an item wider than maxW gets a row of its own). Calls
// row(first, end, rowWidth, index) for each row and returns how many there are.
template <typename WidthOf, typename Row>
int flowRows(int count, float maxW, float gap, WidthOf widthOf, Row row)
{
    int rows = 0;
    for (int first = 0; first < count; ++rows)
    {
        float width = widthOf(first);
        int end = first + 1;
        while (end < count && width + gap + widthOf(end) <= maxW)
            width += gap + widthOf(end++);
        row(first, end, width, rows);
        first = end;
    }
    return rows;
}

// The narrowest row width (at most maxW) that flows the items into no more rows
// than maxW does, so the rows come out even (8 + 7 rather than 10 + 5).
template <typename WidthOf>
float balancedRowWidth(int count, float maxW, float gap, WidthOf widthOf)
{
    const auto ignore = [](int, int, float, int) {};
    const int rows = flowRows(count, maxW, gap, widthOf, ignore);
    if (rows <= 1)
        return maxW;
    float lo = 0.0f, hi = maxW;
    for (int i = 0; i < count; ++i)
        lo = std::max(lo, widthOf(i));
    for (int step = 0; step < 16 && hi - lo > 0.5f; ++step)
    {
        const float mid = 0.5f * (lo + hi);
        if (flowRows(count, mid, gap, widthOf, ignore) <= rows)
            hi = mid;
        else
            lo = mid;
    }
    return hi;
}

inline bool contains(ImVec2 lo, ImVec2 hi, ImVec2 p) noexcept
{
    return p.x >= lo.x && p.x < hi.x && p.y >= lo.y && p.y < hi.y;
}
} // namespace supporters_detail

// Full-canvas "Special Thanks" credits overlay. Immediate-mode: draws a dark scrim over the whole
// plugin area and a centred card: heading, the tiers with their names flowed into centred rows
// (the list scrolls once it outgrows the canvas), and a footer with the optional action link, the
// close hint and the credit. Coordinates are the plugin's DESIGN space (0..designW/H); the shared
// DuskPanel maps to screen.
//
// Closing: a press and its release both outside the name list (and off the action link) close it,
// wherever they land, card included. That is read from the mouse, not from an ImGui item, because
// the face under the scrim is drawn first and, although disabled, its items still claim the hover
// (Dear ImGui keeps HoveredId for a disabled item), so a full-canvas button drawn after them never
// received a click over a face control.
//
// The face sees none of a closing gesture, a double-click included. The caller draws the face
// disabled while `open` is true, before the overlay. At the release that closes it the panel
// draws one last time and `open` is cleared, so from the next frame the caller draws the face as
// it is when closed. Until the button is up and a double-click time (0.30 s) has passed since that
// release or any later press, the face takes no clicks at all, wherever they land: Dear ImGui
// chains a double-click only within 6 px, but a second click that drifted must not act as a
// single click on a control. That block is an invisible modal popup over the canvas (no
// background, border or dimming): Dear ImGui lets nothing outside a modal be hovered or clicked,
// windows that appear while it is up included (they go behind it). It also has Dear ImGui report
// the keyboard as captured until it closes.
//
// Opening: a press within a double-click time of the press that opened it, or of a press chained
// to that one the same way, is part of the opening gesture and ignored, timed as ImGui times a
// double-click: a double- or triple-click on the nameplate opens it and leaves it open, whether
// the title acts on its press or its release.
inline void drawSupportersOverlay(DuskPanel& panel, ImDrawList* dl,
                                  float designW, float designH, bool& open,
                                  const char* pluginName, const char* version,
                                  SupportersOverlay* overlay)
{
    using L = SupportersLayout;
    using T = SupportersType;
    using Press = SupportersOverlay::Press;
    using supporters_detail::drawText;
    using supporters_detail::textWidth;
    const SupportersTheme theme{};

    // Everything the overlay keys in ImGui (its stored state, its list window, its popup) is scoped
    // by the caller's flag, so two overlays drawn in one window keep apart.
    struct IdScope
    {
        explicit IdScope(const void* id) { ImGui::PushID(id); }
        ~IdScope() { ImGui::PopID(); }
    } idScope(&open);

    // --- pointer state between frames -----------------------------------------------------------
    SupportersOverlay::Interaction stored;
    bool storedEveryFrame = false;
    ImGuiStorage* const storage = overlay == nullptr ? ImGui::GetStateStorage() : nullptr;
    ImGuiID keys[7] = {};
    if (storage != nullptr)
    {
        static const char* const kKeyNames[7] = { "##frame", "##shown", "##press", "##openpress",
                                                  "##closing", "##dismiss", "##everyframe" };
        for (int i = 0; i < 7; ++i)
            keys[i] = ImGui::GetID(kKeyNames[i]);
        stored.lastFrame = storage->GetInt(keys[0], -1);
        stored.shown = storage->GetBool(keys[1], false);
        stored.press = static_cast<Press>(storage->GetInt(keys[2], 0));
        stored.sinceOpeningPress = storage->GetFloat(keys[3], 0.0f);
        stored.closing = storage->GetBool(keys[4], false);
        stored.sinceDismiss = storage->GetFloat(keys[5], 0.0f);
        storedEveryFrame = storage->GetBool(keys[6], false);
    }
    SupportersOverlay::Interaction& state = overlay != nullptr ? overlay->interaction : stored;
    bool& everyFrame = overlay != nullptr ? overlay->calledWhileClosed : storedEveryFrame;
    const auto keepState = [&]
    {
        if (storage == nullptr)
            return;
        storage->SetInt(keys[0], state.lastFrame);
        storage->SetBool(keys[1], state.shown);
        storage->SetInt(keys[2], static_cast<int>(state.press));
        storage->SetFloat(keys[3], state.sinceOpeningPress);
        storage->SetBool(keys[4], state.closing);
        storage->SetFloat(keys[5], state.sinceDismiss);
        storage->SetBool(keys[6], everyFrame);
    };

    const ImGuiIO& io = ImGui::GetIO();
    const float doubleClickTime = io.MouseDoubleClickTime;
    const int frame = ImGui::GetFrameCount();
    const bool continuous = state.lastFrame == frame - 1;
    state.lastFrame = frame;
    if (!open)
        everyFrame = true;   // so a click can clear `open` at once and leave the wait to the block

    // The closing wait's block (see above). `start` opens it; `keep` false closes it. It is only
    // reopened at the click that closes the panel, so it never displaces another popup.
    char blockName[24];
    std::snprintf(blockName, sizeof(blockName), "##supblock%08X", static_cast<unsigned>(ImGui::GetID("##supblock")));
    const auto block = [&](bool keep, bool start)
    {
        if (start && !ImGui::IsPopupOpen(blockName))
            ImGui::OpenPopup(blockName);
        if (!ImGui::IsPopupOpen(blockName))
            return;
        const ImVec2 lo = panel.P(0.0f, 0.0f), hi = panel.P(designW, designH);
        ImGui::SetNextWindowPos(lo);
        ImGui::SetNextWindowSize(ImVec2(hi.x - lo.x, hi.y - lo.y));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, theme.clear);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        if (ImGui::BeginPopupModal(blockName, nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                       | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
                                       | ImGuiWindowFlags_NoNav))
        {
            if (!keep)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    };

    // --- closed: by its caller, or by a click whose double-click time is not over yet ------------
    if (!open)
    {
        if (state.closing)
        {
            state.sinceDismiss += io.DeltaTime;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                state.sinceDismiss = 0.0f;
            state.closing = ImGui::IsMouseDown(ImGuiMouseButton_Left) || state.sinceDismiss < doubleClickTime;
        }
        block(state.closing, false);
        state.shown = false;
        state.press = Press::None;
        keepState();
        return;
    }

    // --- open -------------------------------------------------------------------------------------
    if (!state.shown || !continuous)   // opening (a gap: a caller that calls it only while open)
    {
        block(false, false);   // reopened before a click's wait was over
        state = SupportersOverlay::Interaction{};
        state.lastFrame = frame;
        // Timed from the opening press; for a title acting on its release that press came a hold
        // before this frame. With no click yet there is nothing to time.
        state.sinceOpeningPress =
            io.MouseClickedLastCount[ImGuiMouseButton_Left] > 0
                ? static_cast<float>(ImGui::GetTime() - io.MouseClickedTime[ImGuiMouseButton_Left])
                : doubleClickTime;
    }
    else
    {
        state.sinceOpeningPress += io.DeltaTime;
        state.sinceDismiss += io.DeltaTime;
    }
    state.shown = true;

    // The older close, for a caller that calls it only while `open` is true: nothing is drawn, and
    // `open` (so the face's disabling) holds until the button is up and a double-click time has
    // passed since the closing release or any later press.
    if (state.closing)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            state.sinceDismiss = 0.0f;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && state.sinceDismiss >= doubleClickTime)
        {
            state.closing = false;
            state.shown = false;
            open = false;
        }
        keepState();
        return;
    }

    const float s = panel.scale();
    const bool hasAction = overlay != nullptr && overlay->hasActionLink();

    // --- card geometry ----------------------------------------------------------------------------
    const float cardW = std::min(L::maxCardW, designW - 2.0f * L::marginX);
    const float innerW = cardW - 2.0f * L::padX;
    const float cardX = 0.5f * (designW - cardW);

    const char* const subtitle = "To our supporters who make this plugin possible";
    const char* const dot = "\xC2\xB7";   // U+00B7 MIDDLE DOT, inside the baked Latin-1 range
    const float dotW = textWidth(panel, dot, T::text);

    char credit[128];
    if (pluginName == nullptr || pluginName[0] == '\0')
        std::snprintf(credit, sizeof(credit), "by Dusk Audio");
    else if (version == nullptr || version[0] == '\0')
        std::snprintf(credit, sizeof(credit), "%s by Dusk Audio", pluginName);
    else
        std::snprintf(credit, sizeof(credit), "%s v%s by Dusk Audio", pluginName, version);

    struct FooterItem { const char* text; bool link; float width = 0.0f, x = 0.0f; int row = 0; };
    FooterItem footer[3];
    int footerCount = 0;
    if (hasAction)
        footer[footerCount++] = { overlay->actionLinkLabel().c_str(), true };
    footer[footerCount++] = { hasAction ? "Click anywhere else to close" : "Click anywhere to close", false };
    footer[footerCount++] = { credit, false };
    for (int i = 0; i < footerCount; ++i)
        footer[i].width = textWidth(panel, footer[i].text, T::text);
    const auto footerWidth = [&](int i) { return footer[i].width; };
    const float footerRowW = supporters_detail::balancedRowWidth(footerCount, innerW, L::itemGap, footerWidth);
    const int footerRows = supporters_detail::flowRows(footerCount, footerRowW, L::itemGap, footerWidth,
                                                       [](int, int, float, int) {});

    // The tiers, laid out (and drawn when `cdl` is set, at `origin`) in rows no wider than rowW
    // centred in innerW. Returns the height of the whole list.
    std::vector<float> widths;
    const auto layoutList = [&](float rowW, ImDrawList* cdl, ImVec2 origin) -> float
    {
        float y = 0.0f;
        bool first = true;
        for (const BackerTier& tier : patreonTiers())
        {
            if (tier.names.empty())
                continue;
            if (!first)
                y += T::text + L::tierGap;
            first = false;

            const ImU32 accent = supporterTierColor(tier.title);
            const bool isPast = std::strcmp(tier.title, "PAST SUPPORTERS") == 0;
            if (cdl != nullptr)
            {
                const float w = textWidth(panel, tier.title, T::text);
                drawText(cdl, panel, ImVec2(origin.x + 0.5f * (innerW - w) * s, origin.y + y * s),
                         T::text, accent, tier.title);
            }

            const int count = (int) tier.names.size();
            widths.clear();
            for (const char* name : tier.names)
                widths.push_back(textWidth(panel, name, T::text));
            const auto nameWidth = [&](int i) { return widths[(size_t) i]; };
            const int rows = supporters_detail::flowRows(
                count, supporters_detail::balancedRowWidth(count, rowW, L::itemGap, nameWidth),
                L::itemGap, nameWidth,
                [&](int begin, int end, float width, int row)
                {
                    if (cdl == nullptr)
                        return;
                    const float rowY = origin.y + (y + (float) (row + 1) * L::rowPitch) * s;
                    float x = 0.5f * (innerW - width);
                    for (int i = begin; i < end; ++i)
                    {
                        const float w = nameWidth(i);
                        drawText(cdl, panel, ImVec2(origin.x + x * s, rowY), T::text,
                                 isPast ? theme.pastName : theme.name, tier.names[(size_t) i]);
                        if (i + 1 < end)
                            drawText(cdl, panel,
                                     ImVec2(origin.x + (x + w + 0.5f * (L::itemGap - dotW)) * s, rowY),
                                     T::text, scaleAlpha(accent, 0.45f), dot);
                        x += w + L::itemGap;
                    }
                });
            y += (float) rows * L::rowPitch;
        }
        return first ? 0.0f : y + T::text;
    };

    const float headerH = L::padTop + T::heading + L::headingGap + T::text + L::ruleGap;   // to the rule
    const float listTop = headerH + 1.0f + L::ruleGap;
    const float footerH = L::ruleGap + 1.0f + L::footerGap
                        + T::text + (float) (footerRows - 1) * L::rowPitch + L::padBottom;
    const float listRoom = std::max(L::rowPitch, designH - 2.0f * L::marginY - listTop - footerH);
    float rowW = innerW;
    float listContent = layoutList(rowW, nullptr, ImVec2());
    const bool scrolls = listContent > listRoom;
    if (scrolls)
    {
        // Rows stay centred on the card and clear of the scrollbar on its right.
        rowW = innerW - 2.0f * (L::scrollbarW + 6.0f);
        listContent = layoutList(rowW, nullptr, ImVec2());
    }
    const float listH = std::min(listContent, listRoom);
    const float cardH = listTop + listH + footerH;
    const float cardY = 0.5f * (designH - cardH);
    const float footerRuleY = cardY + listTop + listH + L::ruleGap;

    const ImVec2 listMin = panel.P(cardX + L::padX, cardY + listTop);
    const ImVec2 listMax = panel.P(cardX + L::padX + innerW, cardY + listTop + listH);

    // Each footer item's place, for the link's hit test and for drawing.
    supporters_detail::flowRows(footerCount, footerRowW, L::itemGap, footerWidth,
        [&](int begin, int end, float width, int row)
        {
            float x = cardX + L::padX + 0.5f * (innerW - width);
            for (int i = begin; i < end; ++i)
            {
                footer[i].x = x;
                footer[i].row = row;
                x += footer[i].width + L::itemGap;
            }
        });
    const auto footerY = [&](int row) { return footerRuleY + 1.0f + L::footerGap + (float) row * L::rowPitch; };
    // The action link, when there is one, is the first item.
    const ImVec2 linkMin = hasAction ? panel.P(footer[0].x - 5.0f, footerY(footer[0].row) - 3.0f) : ImVec2();
    const ImVec2 linkMax = hasAction ? panel.P(footer[0].x + footer[0].width + 5.0f,
                                               footerY(footer[0].row) + T::text + 3.0f)
                                     : ImVec2();

    // --- pointer: record what a press begins on, act at its release -----------------------------
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overList = supporters_detail::contains(listMin, listMax, mouse);
    const bool overLink = hasAction && supporters_detail::contains(linkMin, linkMax, mouse);
    if (overLink)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (state.sinceOpeningPress < doubleClickTime)
            state.sinceOpeningPress = 0.0f;   // chained to the opening press: part of that gesture
        else
            state.press = overLink ? Press::Action
                        : (!overList && !ImGui::IsAnyItemActive()) ? Press::Dismiss
                        : Press::None;
    }
    bool act = false;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        // An item still active here was pressed after the overlay drew in that frame (the resize
        // grip, submitted after it): its drag is not a click on the scrim.
        act = state.press == Press::Action && overLink;
        if (state.press == Press::Dismiss && !overList && !overLink && !ImGui::IsAnyItemActive())
        {
            // See the top of this function. The panel still draws in this frame, whose face was
            // drawn disabled.
            state.closing = true;
            state.sinceDismiss = 0.0f;
            if (everyFrame)
            {
                open = false;
                state.shown = false;
            }
        }
        state.press = Press::None;
    }
    keepState();

    // --- scrim and card ---------------------------------------------------------------------------
    dl->AddRectFilled(panel.P(0.0f, 0.0f), panel.P(designW, designH), theme.scrim);
    dl->AddRectFilled(panel.P(cardX, cardY), panel.P(cardX + cardW, cardY + cardH), theme.card, L::radius * s);
    dl->AddRect(panel.P(cardX, cardY), panel.P(cardX + cardW, cardY + cardH), theme.cardEdge,
                L::radius * s, 0, L::edge * s);

    // --- header -------------------------------------------------------------------------------------
    const float centreX = cardX + 0.5f * cardW;
    panel.text(dl, centreX, cardY + L::padTop, T::heading, theme.heading, "Special Thanks", 0, true);
    panel.text(dl, centreX, cardY + L::padTop + T::heading + L::headingGap, T::text, theme.caption,
               subtitle, 0);
    dl->AddRectFilled(panel.P(cardX + L::ruleInset, cardY + headerH),
                      panel.P(cardX + cardW - L::ruleInset, cardY + headerH + 1.0f), theme.rule);

    // --- the name list, scrolling once it outgrows the card -----------------------------------------
    if (listH > 0.0f)   // a zero height would ask BeginChild for the rest of the window
    {
        ImGui::SetCursorScreenPos(listMin);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.clear);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, theme.clear);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, theme.cardEdge);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, theme.caption);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, theme.name);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));   // the Dummy below sets the extent
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, L::scrollbarW * s);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.5f * L::scrollbarW * s);
        ImGui::BeginChild("##suplist", ImVec2(innerW * s, listH * s), ImGuiChildFlags_None,
                          scrolls ? ImGuiWindowFlags_None
                                  : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 origin = ImGui::GetCursorScreenPos();   // moves with the scroll
        layoutList(rowW, ImGui::GetWindowDrawList(), origin);
        // As wide as the content region, which leaves out the scrollbar: any wider and the list
        // would scroll sideways too.
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, listContent * s));
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);
    }

    // --- footer -------------------------------------------------------------------------------------
    dl->AddRectFilled(panel.P(cardX + L::ruleInset, footerRuleY),
                      panel.P(cardX + cardW - L::ruleInset, footerRuleY + 1.0f), theme.rule);
    for (int i = 0; i < footerCount; ++i)
    {
        const FooterItem& item = footer[i];
        const float y = footerY(item.row);
        const ImU32 col = !item.link ? theme.caption : overLink ? theme.linkHover : theme.link;
        drawText(dl, panel, panel.P(item.x, y), T::text, col, item.text);
        if (item.link)
            dl->AddLine(panel.P(item.x, y + T::text + 1.0f), panel.P(item.x + item.width, y + T::text + 1.0f),
                        col, 1.0f * s);
        if (i + 1 < footerCount && footer[i + 1].row == item.row)   // a dot between neighbours on a row
            drawText(dl, panel, panel.P(item.x + item.width + 0.5f * (L::itemGap - dotW), y), T::text,
                     theme.cardEdge, dot);
    }

    if (state.closing && !open)   // closed by this frame's release: the block starts now
        block(true, true);
    if (act)
        overlay->triggerAction();
}

} // namespace duskdaf
