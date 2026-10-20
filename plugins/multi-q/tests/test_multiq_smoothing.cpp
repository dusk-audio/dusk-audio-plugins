// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: coefficient ramps must reach their calibrated target at every OS rate.
#include "../core/MultiQFilters.hpp"
#if defined(MULTIQ_TEST_JUCE)
#include "../MultiQ.h"
using Biquad = ::StereoBiquad;
using Coeffs = ::BiquadCoeffs;
using SVF = ::CytomicSVF;
using SVFC = ::SVFCoeffs;
#else
using Biquad = duskaudio::StereoBiquad;
using Coeffs = duskaudio::MqBiquadCoeffs;
using SVF = duskaudio::CytomicSVF;
using SVFC = duskaudio::SVFCoeffs;
#endif

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
Coeffs peak(double sr, double gain)
{
    duskaudio::MqBiquadCoeffs source;
    duskaudio::amb::computePeaking(source, 500.0, sr, gain, double(0.71f));
    Coeffs result;
    std::memcpy(result.coeffs, source.coeffs, sizeof(source.coeffs));
    return result;
}
std::array<float,6> values(const SVFC& c) { return {c.a1,c.a2,c.a3,c.m0,c.m1,c.m2}; }
bool equal(const SVFC& a, const SVFC& b) { return values(a) == values(b); }

void atRate(double sr)
{
    const float alpha = 1.0f - std::exp(-1.0f / (0.001f * float(sr)));
    const int settle = int(0.030 * sr); // thirty time constants, not a float-epsilon guess
    Biquad biquad;
    biquad.setSmoothCoeff(alpha);
    Coeffs target = peak(sr, 6.0);
    biquad.setCoeffs(target);
    biquad.stepSmoothing();
    check(std::memcmp(biquad.coeffs.coeffs, target.coeffs, sizeof(target.coeffs)) != 0,
          "active biquad target really ramps instead of jumping");
    const float first = biquad.coeffs.coeffs[0];
    for (int i = 1; i < settle; ++i)
    {
        // DSP republishes unchanged targets each block. It must not restart the
        // ramp's fractional progress and keep it permanently short of target.
        if (i % 64 == 0) biquad.setCoeffs(target);
        biquad.stepSmoothing();
    }
    check(std::memcmp(biquad.coeffs.coeffs, target.coeffs, sizeof(target.coeffs)) == 0,
          "biquad reaches the exact designed target after smoothing");
    check(first > 1.0f && first < target.coeffs[0], "biquad first step has the requested direction and finite speed");
    // Opposite direction and an interrupted ramp must both converge.
    target = peak(sr, -6.0); biquad.setCoeffs(target);
    for (int i = 0; i < settle / 10; ++i) biquad.stepSmoothing();
    biquad.reset();
    target = peak(sr, 12.0); biquad.setCoeffs(target);
    for (int i = 0; i < settle; ++i) biquad.stepSmoothing();
    check(std::memcmp(biquad.coeffs.coeffs, target.coeffs, sizeof(target.coeffs)) == 0,
          "reset during a biquad ramp cannot retain an old smoothing accumulator");
    const float wet = biquad.processSampleL(0.25f);
    check(std::isfinite(wet) && wet != 0.25f, "the settled active biquad still processes audio");
    target.setIdentity(); biquad.setCoeffs(target);
    for (int i = 0; i < settle; ++i) biquad.stepSmoothing();
    for (int i = 0; i < 8; ++i) { biquad.processSampleL(0); biquad.processSampleR(0); }
    check(std::memcmp(biquad.coeffs.coeffs, target.coeffs, sizeof(target.coeffs)) == 0
              && biquad.processSampleL(0.125f) == 0.125f && biquad.processSampleR(-0.25f) == -0.25f,
          "biquad return to identity removes the preceding active response");

    SVF svf; svf.setSmoothCoeff(alpha);
    const double g = std::tan(duskaudio::kMultiQPi * 500.0 / sr), k = 1.0 / 0.71;
    SVFC goal;
    goal.a1 = float(1.0 / (1.0 + g * (g + k))); goal.a2 = float(g * goal.a1); goal.a3 = float(g * goal.a2);
    goal.m0 = 1.0f; goal.m1 = 0.5f; goal.m2 = -0.25f;
    svf.setTarget(goal); svf.stepSmoothing();
    check(!equal(svf.coeffs, goal), "SVF target really ramps");
    for (int i = 1; i < settle; ++i)
    {
        if (i % 64 == 0) svf.setTarget(goal);
        svf.stepSmoothing();
    }
    check(equal(svf.coeffs, goal), "dynamic SVF reaches the exact target after smoothing");
    goal.m0 = 0.75f; goal.m1 = -0.25f;
    svf.setTarget(goal);
    for (int i = 0; i < settle / 10; ++i) svf.stepSmoothing();
    svf.reset(); goal.m0 = 1.25f; svf.setTarget(goal);
    for (int i = 0; i < settle; ++i) svf.stepSmoothing();
    check(equal(svf.coeffs, goal), "SVF reset during a ramp discards stale fractional state");
    svf.snapToTarget(); goal.setIdentity(); svf.setTarget(goal);
    for (int i = 0; i < settle; ++i) svf.stepSmoothing();
    check(equal(svf.coeffs, goal) && svf.processSample(0.25f) == 0.25f,
          "SVF smoothing can return to identity after an active target");
}
}

int main()
{
    for (double sr : {44100.0,48000.0,96000.0,192000.0,384000.0}) atRate(sr);
    std::printf("Multi-Q coefficient smoothing: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
