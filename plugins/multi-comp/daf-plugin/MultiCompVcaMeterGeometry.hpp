// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

namespace multicompp::ui_detail
{
// The VCA face's meter, in canvas design coordinates (the 1120 x 380 canvas): the FET meter's footprint (252 x 150),
// between the model line above it and the badge below. The UI, the render test and the layout contract all read it
// from here; it carries no drawing, so the logic tests need no ImGui for it.
struct VcaMeterRect
{
    static constexpr float x0 = 740.0f, y0 = 138.0f, x1 = 992.0f, y1 = 288.0f;
};
} // namespace multicompp::ui_detail
