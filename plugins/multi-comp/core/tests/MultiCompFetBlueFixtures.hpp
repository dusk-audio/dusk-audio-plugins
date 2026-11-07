// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// FET BLUE (mode 4): the second reference FET compressor's measured behaviour the voice is held to (core test
// --fet-blue). Every row is a reading of the reference at 48 kHz (tools fet_ae/: laws.py, tables.py, scf.py,
// ratio_gain.py, gate_burst.py; 2026-10-26/27), its knobs at Output .6259, Attack .5, Release .666 unless a row says
// otherwise.
#pragma once

#include <array>

namespace fetBlueFixtures
{
// The settled reduction (dB) on a 1 kHz staircase (-60..0 dBFS in 2 dB steps of 0.6 s, the last 0.3 s read, re
// the first step) at Input .375, for Ratio positions 2:1 (1), 4:1 (2) and 20:1 (4).
struct StaticRow { int ratio; float levelDbfs, reductionDb; };
inline constexpr std::array<StaticRow, 15> statics{{
    {1, -24.0f, -1.360f}, {1, -20.0f, -4.042f}, {1, -12.0f, -10.182f}, {1, -4.0f, -16.583f}, {1, 0.0f, -19.829f},
    {2, -24.0f, -0.838f}, {2, -20.0f, -3.798f}, {2, -12.0f, -10.373f}, {2, -4.0f, -17.220f}, {2, 0.0f, -20.714f},
    {4, -20.0f, -0.211f}, {4, -16.0f, -3.929f}, {4, -12.0f, -7.718f}, {4, -4.0f, -15.311f}, {4, 0.0f, -19.143f}}};
// The voice's static law fits these within its quadratic knee's worst over four Inputs (0.26 dB at 4:1).
inline constexpr float kStaticsBoundDb = 0.3f;

// Headroom moves the drive: 4:1, Input .375, its 8 dB and 24 dB positions (steps 1 and 5), reduction (dB) at -10
// and -20 dBFS (the reference reads hr24 at -20 dBFS as hr4 at 0 dBFS, -10.37).
struct HeadroomRow { int step; float levelDbfs, reductionDb; };
inline constexpr std::array<HeadroomRow, 4> headroom{{
    {1, -10.0f, -5.38f}, {1, 0.0f, -13.78f}, {5, -20.0f, -10.37f}, {5, -10.0f, -18.96f}}};

// The linear gain of each Ratio position re 4:1 (dB): the staircases' first step, -60 dBFS, at four Inputs within
// 0.003 dB of these means.
inline constexpr std::array<float, 11> ratioGainsDb{{
    -0.1006f, -0.1002f, 0.0f, 0.1003f, -0.3004f, -0.0386f, 1.2816f, 1.7017f, 1.7516f, 1.7517f, 1.3517f}};
inline constexpr float kRatioGainBoundDb = 0.01f;

// The amplifier with the gain element out (Ratio None, Input .375, 1 kHz steps of 1 s): the fundamental's gain change
// re -40 dBFS, H2 and H3 (dBc).
struct AmplifierRow { float levelDbfs, gainChangeDb, h2Dbc, h3Dbc; };
inline constexpr std::array<AmplifierRow, 3> amplifier{{
    {-20.0f, -0.08f, -45.3f, -53.3f}, {-8.0f, -0.43f, -36.0f, -38.4f}, {0.0f, -1.43f, -33.3f, -27.6f}}};
inline constexpr float kAmplifierGainBoundDb = 0.05f, kAmplifierHarmonicBoundDb = 0.5f;

// Release from the slow end (gate_burst.py): a 250 ms 2 kHz burst at 0.6 peak over a -40 dBFS bed, Input .5, Attack
// .126, Release 0: the carrier's gain (dB re the bed, 1 ms frames) 2 ms before the burst ends and 50 / 150 / 300 ms
// after it, at 2:1 (1), 4:1 (2), 8:1 (3) and 20:1 (4). The reference never sits deeper than at the burst's end. These
// captures are held out: no fit may read them. (release0.py's 1 kHz bursts, which these rows replace, are fit data.)
struct ReleaseRow { int ratio; float endDb, after50Db, after150Db, after300Db; };
inline constexpr std::array<ReleaseRow, 4> releaseFromSlowEnd{{
    {1, -22.53f, -22.20f, -21.37f, -19.83f}, {2, -23.52f, -23.18f, -22.14f, -20.26f},
    {3, -23.70f, -23.33f, -22.26f, -20.36f}, {4, -23.28f, -22.78f, -21.43f, -19.08f}}};
// The campaign's cell bar: a reading within 1.00 dB of the reference passes.
inline constexpr float kReleaseBoundDb = 1.0f;

// The SC Filter: settled gain with the filter in minus out (dB), -10 dBFS tones, 4:1, Input .5.
struct ScFilterRow { float hz, onMinusOffDb; };
inline constexpr std::array<ScFilterRow, 4> scFilter{{{100.0f, 3.65f}, {300.0f, -0.23f}, {1000.0f, -4.91f}, {4000.0f, -10.03f}}};
// The voice's three-shelf prototype, its gain set through the loop, reads these within 0.09 dB.
inline constexpr float kScFilterBoundDb = 0.15f;
}
