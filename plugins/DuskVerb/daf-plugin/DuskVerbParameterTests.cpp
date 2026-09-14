// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
#include "../core/DuskVerbParamTable.hpp"
#include <cstdio>
#include <cstring>
#include <limits>

int main()
{
    using namespace duskverb;
    int failures = 0;
    const auto check = [&](bool pass, const char* why, const char* id) {
        if (!pass) { std::printf("FAIL %s: %s\n", id, why); ++failures; }
    };
    // Measured from the independently hosted original JUCE VST3. This is the
    // automation coordinate, not the position of a custom UI slider.
    check(std::abs(dspFromHost(paramDesc(Decay), 0.5f) - 5.4679451f) < 0.000002f,
          "normalized midpoint must produce JUCE's 5.4679451 seconds", "decay");
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDesc(i);
        check(hostMin(d) == (hasSkew(d) ? 0.0f : d.min)
           && hostMax(d) == (hasSkew(d) ? 1.0f : d.max), "host domain", d.id);
        for (float proportion : {0.0f, 0.17f, 0.5f, 0.83f, 1.0f})
        {
            const float host = hostMin(d) + proportion * (hostMax(d) - hostMin(d));
            const float snapped = snapHostValue(d, host);
            const float knob = hostToKnob(d, snapped);
            check(knobToHost(d, knob) == snapped, "host/UI coordinate roundtrip", d.id);
            check(hostValueInRange(d, snapped), "snapped input is valid state", d.id);
            if (!hasSkew(d))
                check(snapped == rangeSnap(d, host), "all declared intervals snap at ingress", d.id);
        }
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()})
        {
            check(snapHostValue(d, invalid) == hostDefault(d), "nonfinite ingress uses default", d.id);
            check(!hostValueInRange(d, invalid), "nonfinite state is rejected", d.id);
        }
    }
    std::printf("Parameter regression: %d failures (%d parameters)\n", failures, kNumParams);
    return failures ? 1 : 0;
}
