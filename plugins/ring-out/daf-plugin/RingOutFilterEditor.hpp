// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include "RingOutFilterTable.hpp"
#include "DuskImGuiWidgets.hpp"

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
