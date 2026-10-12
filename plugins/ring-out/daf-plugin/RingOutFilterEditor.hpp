// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include "RingOutFilterTable.hpp"
#include "DuskImGuiWidgets.hpp"

#include <cmath>
#include <cstdio>

// Build a one-field edit from the current mirror. Two controls can commit in a
// single frame (clicking ON while a text field loses focus), so a draw-time row
// snapshot must not restore fields an earlier control already changed.
template <typename T>
inline duskaudio::ringout::EditCommand roFilterFieldCommand(
    const duskaudio::ringout::FilterTable& table, int row,
    T duskaudio::ringout::Filter::* field, T value) noexcept
{
    duskaudio::ringout::EditCommand command;
    if (row < 0 || row >= table.count) return command;
    command.kind = duskaudio::ringout::EditCommand::kSet;
    command.slot = row;
    command.previous = table.f[row];
    command.filter = command.previous;
    command.filter.*field = value;
    command.hasPrevious = true;
    return command;
}

// Inline text belongs to the filter that was selected when entry began. A new
// selection or replacement of that row must cancel it before focus loss can
// commit the old text to the newly selected filter. Global knob edits remain
// independent of selection.
struct RoFilterEditSelection
{
    void update(duskdaf::DuskPanel& panel, const duskaudio::ringout::FilterTable& table,
                int selected) noexcept
    {
        const bool valid = selected >= 0 && selected < table.count;
        const int nextRow = valid ? selected : -1;
        const uint32_t nextId = valid ? table.id[selected] : 0;
        if ((nextRow != row || nextId != rowId)
            && (panel.isEditingValue("cut") || panel.isEditingValue("freq") || panel.isEditingValue("q")))
            panel.cancelValueEdit();
        row = nextRow;
        rowId = nextId;
    }

    int row = -1;
    uint32_t rowId = 0;
};

// `v` rounded to `digits` significant figures (66.25 -> 66.3, 15001 -> 15000).
inline float roRoundSignificant(float v, int digits) noexcept
{
    if (!(v > 0.0f) || !std::isfinite(v) || digits < 1) return v;
    const float unit = std::pow(10.0f, std::floor(std::log10(v)) - (float)(digits - 1));
    return std::round(v / unit) * unit;
}

// One drag of a notch, by its node on the graph or by its fader in the bank.
// The gesture accumulates pointer motion in the units it edits, scaled frame by
// frame (so Shift pressed mid-drag slows the motion from there on rather than
// jumping), and derives each frame's value from where the drag began rather
// than from the mirror: a deepen the engine lands mid-drag cannot compound into
// the gesture. A node is drawn at its effective depth (CUT TRIM applied) and
// the drag moves its own cut by the pointer's motion; where a positive CUT TRIM
// clamps that depth at 0 dB the node holds on the line while the cut moves,
// until the cut is deep enough to show.
//
// A dragged frequency lands on three significant figures (66.3, 250, 1.25k,
// 15.0k), the resolution the strip and the callout read in, rather than on the
// tenth of a hertz that turns 15 kHz into "15001". The steppers and the
// detector keep their finer values; only a drag rounds.
//
// The row is followed by its id, so the drag stays on its notch when rows
// around it change. A row the editor has only just added has no id until the
// engine's table comes back; it is followed by index until then and adopts the
// id that arrives. (Rows only ever append while a drag holds the mouse; a host
// CLEAR empties the table, which ends the drag.)
struct RoNotchDrag
{
    static constexpr float kFineScale = 0.1f;

    void begin(const duskaudio::ringout::FilterTable& table, int row) noexcept
    {
        active = row >= 0 && row < table.count;
        if (!active) return;
        rowIndex = row;
        rowId = table.id[row];
        startHz = table.f[row].freqHz;
        startCut = table.f[row].cutDb;
        octaves = 0.0f;
        db = 0.0f;
    }

    // Motion past either end of a range is dropped, as on the knobs, so turning
    // back moves the notch at once instead of first unwinding the overshoot.
    void move(float deltaOctaves, float deltaDb, bool fine) noexcept
    {
        namespace ro = duskaudio::ringout;
        const float k = fine ? kFineScale : 1.0f;
        octaves = ro::clampf(octaves + k * deltaOctaves,
                             std::log2(ro::kFreqMin / startHz), std::log2(ro::kFreqMax / startHz));
        db = ro::clampf(db + k * deltaDb, ro::kCutMin - startCut, ro::kCutMax - startCut);
    }

    // The row being dragged in `table`, or -1 when idle or the notch is gone.
    int resolve(const duskaudio::ringout::FilterTable& table) noexcept
    {
        if (!active) return -1;
        if (rowId != 0)
        {
            for (int i = 0; i < table.count; ++i)
                if (table.id[i] == rowId) return rowIndex = i;
            return -1;
        }
        if (rowIndex >= table.count) return -1;
        rowId = table.id[rowIndex];
        return rowIndex;
    }

    float freqHz() const noexcept
    {
        return duskaudio::ringout::snapFreq(roRoundSignificant(startHz * std::exp2(octaves), 3));
    }
    float cutDb() const noexcept  { return duskaudio::ringout::snapCut(startCut + db); }

    void end() noexcept { active = false; }

    bool     active = false;
    int      rowIndex = -1;
    uint32_t rowId = 0;
    float    startHz = duskaudio::ringout::kFreqDefault;
    float    startCut = duskaudio::ringout::kCutDefault;
    float    octaves = 0.0f;   // accumulated motion
    float    db = 0.0f;
};

// The notch axis floor for the deepest point the graph has to show: -24 dB at
// rest, extended in 12 dB steps for deeper notches, and capped at -48 dB.
// Stacked notches under a deep CUT TRIM can sum past -150 dB; following them
// would flatten every other notch into the top of the graph, so anything deeper
// than the cap is drawn clipped at the floor instead. The small allowance keeps
// a response that lands a hair under a step (-24.00003) from extending it.
constexpr float kRoNotchAxisRestDb = -24.0f;
constexpr float kRoNotchAxisCapDb  = -48.0f;

inline float roNotchAxisFloorDb(float deepestDb) noexcept
{
    const float need = deepestDb < 0.0f ? 12.0f * std::ceil((-deepestDb - 0.05f) / 12.0f) : 0.0f;
    return -duskaudio::ringout::clampf(need, -kRoNotchAxisRestDb, -kRoNotchAxisCapDb);
}

// Label spacing on that axis, so the labels are round numbers: 6 dB at rest,
// 12 dB once it extends (0 / -12 / -24 / -36 rather than 0 / -9 / -18 / -27).
inline int roNotchAxisStepDb(float floorDb) noexcept
{
    return floorDb >= kRoNotchAxisRestDb ? 6 : 12;
}

// A frequency short enough for a notch strip: "66.3", "880", "1.25k", "12.5k".
// The thresholds sit on the rounding boundaries, so a value never prints one
// digit wider than its band (99.97 Hz reads "100", not "100.0").
inline void roShortHz(float hz, char* buf, int cap) noexcept
{
    if (buf == nullptr || cap <= 0) return;
    if (!std::isfinite(hz)) { std::snprintf(buf, (size_t)cap, "-"); return; }
    if (hz < 99.95f)       std::snprintf(buf, (size_t)cap, "%.1f", (double)hz);
    else if (hz < 999.5f)  std::snprintf(buf, (size_t)cap, "%.0f", (double)hz);
    else if (hz < 9995.0f) std::snprintf(buf, (size_t)cap, "%.2fk", (double)hz / 1000.0);
    else                   std::snprintf(buf, (size_t)cap, "%.1fk", (double)hz / 1000.0);
}
