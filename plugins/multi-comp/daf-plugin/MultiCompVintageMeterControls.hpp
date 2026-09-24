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
}
