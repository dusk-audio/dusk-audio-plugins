// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

// Native UAD SSL G bus compressor gain reduction, measured 2026-09-09 from the
// captures in build-multi-comp-1176/bus-deepgr-20260909 (nat-th-15-r*-a5-rel*-hr3)
// and bus-gaps-20260909/sat-n3, plus the 2:1 / 0.1 s point added 2026-09-10 from
// bus-recal-codex-20260910.  Pins re-tightened 2026-09-11 after the 2:1 upper-law
// nodes, complete law/detector model and release charge offset (native unchanged).
// Threshold -15 dB, HEADROOM 16 dB, MAKE-UP +15 dB,
// SC FILTER off, stereo, 48 kHz, 2x oversampling.  Each row is a 2 s 997/1000 Hz
// 1000 Hz tone segment; the 16 columns are input levels -30 dB to +15 dB in 3 dB steps.
//
// These are REFERENCE numbers.  The paired bound is NOT a parity tolerance -- see
// MultiCompBusWitnessTests.cpp for what the gate does and does not claim.
namespace buswitness
{
struct Point
{
    int ratio, attack, release;   // BUS switch positions
    double nativeGrDb[16];        // native gain reduction, dB
    double pinnedWorstDb;         // divergence measured 2026-09-09
    double boundDb;               // today + 0.01 dB: growth fails, shrinking does not
    const char* label;
};

inline constexpr Point points[] = {
    // 2:1, attack 30 ms, release 1.2 s -- worst |error| 0.155151 dB on 2026-09-11 (3.761999 on 2026-09-09)
    {0, 5, 3,
     {10.0888186, 11.270981, 12.4806651, 13.77665, 15.1697747, 16.6791785, 18.3048779, 20.0375509, 21.8704218, 23.7891597, 25.7860039, 27.8500027, 29.9741541, 32.1527695, 34.3793278, 36.6495292},
     0.155151, 0.165151, "2:1 / 30 ms / 1.2 s"},
    // 4:1, attack 30 ms, release 1.2 s -- worst |error| 0.096962 dB on 2026-09-11 (0.952979 on 2026-09-09)
    {1, 5, 3,
     {10.960254, 13.0739592, 15.2451461, 17.4667618, 19.7330653, 22.0402041, 24.3824687, 26.7575227, 29.1622733, 31.5935848, 34.049463, 36.5279295, 39.0267301, 41.5436073, 44.0785735, 46.6300917},
     0.096962, 0.106962, "4:1 / 30 ms / 1.2 s"},
    // 10:1, attack 30 ms, release 1.2 s -- worst |error| 0.041290 dB on 2026-09-11 (0.532234 on 2026-09-09)
    {2, 5, 3,
     {11.6096267, 14.1475752, 16.7038531, 19.2787557, 21.8715846, 24.4802675, 27.1030183, 29.7372709, 32.3817606, 35.0353936, 37.6979212, 40.3684325, 43.0465754, 45.7316757, 48.4249419, 51.1253236},
     0.041290, 0.051290, "10:1 / 30 ms / 1.2 s"},
    // 10:1, attack 30 ms, release 0.1 s -- worst |error| 0.229766 dB on 2026-09-11 (0.710149 before)
    {2, 5, 0,
     {9.67199345, 11.9647186, 14.306535, 16.6922468, 19.1092104, 21.5521337, 24.0186572, 26.5101436, 28.9824011, 31.3128838, 33.5281756, 35.6012979, 37.5574164, 39.4342689, 41.1110311, 42.5626146},
     0.229766, 0.239766, "10:1 / 30 ms / 0.1 s"},
    // 2:1, attack 30 ms, release 0.1 s -- worst |error| 0.269220 dB on 2026-09-11
    // (8.539519 dB at +15 dBFS on 2026-09-10). Native from bus-recal-codex-20260910/
    // confirm-nat-th-15-r0-a5-rel0-hr3-makeup15, a byte-identical repeat of that
    // pass's nat-th-15-r0-a5-rel0-hr3; its window peaks stay below the output
    // ceiling, and makeup 0 gives 8.545731 dB, so this is not output limiting.
    {0, 5, 0,
     {5.93082795, 6.80991548, 7.80052378, 8.90795073, 10.1552664, 11.5341237, 13.0366862, 14.6529692, 16.374584, 18.1946687, 20.1023707, 22.0879606, 24.1434169, 26.2598026, 28.4308695, 30.6504847},
     0.269220, 0.279220, "2:1 / 30 ms / 0.1 s"},
};
} // namespace buswitness
