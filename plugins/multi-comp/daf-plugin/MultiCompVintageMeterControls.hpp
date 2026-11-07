// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "MultiCompParams.hpp"

namespace multicompp::ui_detail
{
inline int vintageMeterChoice(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(static_cast<int>(std::round(std::clamp(value, 0.0f, 2.0f))), 0, 2) : 0;
}

inline float vintageMeterDb(bool opto, int choice, float gr, float outputRmsDb) noexcept
{
    if (choice == 0) return std::isfinite(gr) ? std::min(gr, 0.0f) : 0.0f;
    // Calibrated independently against the installed reference output meters. The
    // envelope is expressed as sine-equivalent RMS dBFS, not block peak dBFS.
    const float reference = opto ? (choice == 1 ? -12.0f : -18.0f)
                                 : (choice == 1 ? -17.0103f : -21.0103f);
    return std::isfinite(outputRmsDb) ? outputRmsDb - reference : -120.0f;
}

inline int fetMeterButton(float choice, bool bypass) noexcept
{
    return bypass ? 3 : vintageMeterChoice(choice);
}

// OFF uses the same bypass parameter as the shell and host. A meter selection
// restores processing; choosing OFF preserves the previous meter reference.
template <class SetValue>
void selectFetMeterButton(int row, SetValue&& setValue)
{
    if (row < 0 || row > 3) return;
    if (row != 3) setValue(static_cast<uint32_t>(ParamId::FetMeter), static_cast<float>(row));
    setValue(static_cast<uint32_t>(ParamId::Bypass), row == 3 ? 1.0f : 0.0f);
}

// FET BLUE's ratio buttons, 2:1, 4:1, 8:1 and 20:1 (bits 0..3), are pressed one at a time or two together: the lit
// buttons of each Ratio position (kFetBlueRatios order), None with none lit.
inline constexpr std::array<int, 11> kFetBlueRatioButtons{{
    0b0000, 0b0001, 0b0010, 0b0100, 0b1000, 0b0011, 0b0110, 0b1100, 0b0101, 0b1010, 0b1001}};

inline int fetBlueRatioButtons(int position) noexcept
{
    return kFetBlueRatioButtons[static_cast<size_t>(std::clamp(position, 0, 10))];
}

// The Ratio position a press of `button` (0..3) leads to: a lit button goes out, an unlit one joins a single lit
// button, and replaces a lit pair.
inline int fetBlueRatioAfterPress(int position, int button) noexcept
{
    if (button < 0 || button > 3) return position;
    const int lit = fetBlueRatioButtons(position);
    const int bit = 1 << button;
    const bool pair = (lit & (lit - 1)) != 0;
    const int next = (lit & bit) != 0 ? lit & ~bit : pair ? bit : lit | bit;
    for (size_t p = 0; p < kFetBlueRatioButtons.size(); ++p)
        if (kFetBlueRatioButtons[p] == next) return static_cast<int>(p);
    return position;
}
}
