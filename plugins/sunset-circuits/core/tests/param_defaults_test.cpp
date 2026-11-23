// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The core engine keeps its own default table (MultiSynthDSP.cpp, kParamDefs)
// beside the plugin's generated one (daf-plugin/MultiSynthParams.hpp, from
// tools/gen_params.py). The plugin shell writes its own defaults over the core's
// at start-up, so a drift between the two is invisible in the plugin and shows
// only in core-level renders and gates, which then start from other values.
// This fails on any difference, compared exactly, for every core parameter.
#include "MultiSynthDSP.hpp"
#include "MultiSynthParams.hpp"

#include <cstdio>

int main()
{
    static_assert((int)kNumCoreParams == (int)msynth::kNumParams,
                  "core and plugin parameter counts differ");
    msynth::MultiSynthDSP synth;
    int bad = 0;
    for (uint32_t i = 0; i < kNumCoreParams; ++i)
    {
        const float core = synth.getParameter((int)i);
        const float plugin = kParamDefs[i].def;
        if (core != plugin)
        {
            std::printf("  %-18s core %.9g  plugin %.9g\n", kParamDefs[i].symbol, core, plugin);
            ++bad;
        }
    }
    if (bad)
        std::printf("param_defaults: FAIL (%d of %u defaults differ)\n", bad, (unsigned)kNumCoreParams);
    else
        std::printf("param_defaults: PASS (%u defaults identical)\n", (unsigned)kNumCoreParams);
    return bad ? 1 : 0;
}
