// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <array>

// Measured on the reference studio VCA compressor, 2026-10-25 (Studio VCA campaign: the square-wave dynamics probe
// in the tools repo, programme_parity/svca/). The reference is a zero-latency digital gain: on a square wave (|x|
// constant, no detector ripple) its gain is output / input on every sample, so each row below is that ratio in dB at
// one sample of the reference's render, or the mean over a window where stated. Every stimulus is a 100 Hz square at
// 48 kHz, the same on both channels, positive while (n mod 480) < 240 (n counted from the start of the file, across
// level steps), at the levels and for the lengths of its sequence. Settings are the reference's: threshold, ratio
// knob (0..1; 0 = 1.5:1, .4 = 3:1, 1 = 50:1, shown as infinity), attack knob (0 and .75, which
// MultiCompStudioVcaLaw.hpp maps to this plugin's Attack 0.1805 and 35.431 ms), release seconds, Auto Release, input
// and make-up at 0 dB, dry/wet 100 %. These are reference observations, not golden vectors regenerated from this
// plugin.
namespace studioVcaFixtures
{
inline constexpr double kSampleRate = 48000.0;
inline constexpr int kSquarePeriod = 480;   // 100 Hz

// One step of a level sequence: level (dBFS peak) and length (seconds).
struct Step { double dbfs, seconds; };

// A gain read at one sample of a reference trace: milliseconds after the sequence event the row belongs to, the
// trace's sample index (event sample + ms * 48), and the gain there (dB).
struct TracePoint { int msAfter, sample; double gainDb; };

// Statics: family 'statics', tags r0.00 / r0.40 / r1.00 (dyn.json 'stair'). Threshold -50 dBFS, attack knob 0,
// release 0.1 s, Auto off; a staircase from -70 to 0 dBFS in 1 dB steps of 0.25 s, each step read as the mean gain of
// its last 50 ms (2400 samples). Rows: ratio knob, its ratio, step level (dBFS), gain (dB).
inline constexpr double kStaticsThresholdDb = -50.0;
inline constexpr double kStaircaseFirstDbfs = -70.0, kStaircaseStepSeconds = 0.25;
inline constexpr int kStaticsReadSamples = 2400;
struct StaticPoint { double knob, ratio, inputDbfs, gainDb; };
inline constexpr std::array<StaticPoint, 12> statics{{
    {0.00, 1.5, -55.0, -0.4006967122},
    {0.00, 1.5, -50.0, -1.8597120313},
    {0.00, 1.5, -40.0, -6.2583303994},
    {0.00, 1.5, -20.0, -18.9330349755},
    {0.40, 3.0, -55.0, -0.2965890266},
    {0.40, 3.0, -50.0, -2.3702279735},
    {0.40, 3.0, -40.0, -8.8578899914},
    {0.40, 3.0, -20.0, -28.4336054907},
    {1.00, 50.0, -55.0, -0.2015046257},
    {1.00, 50.0, -50.0, -2.4967943630},
    {1.00, 50.0, -40.0, -9.7675136347},
    {1.00, 50.0, -20.0, -32.0657691013},
}};

// Fixed release: family 'release', tag rel1.00 (gain/release/rel1.00.npy). Threshold -40 dBFS, ratio knob 1, attack
// knob 0, release 1 s, Auto off; REL_SEQ below. Event: the -10 -> -40 dBFS step at 2.5 s (sample 120000), after
// 1.5 s held at -32.0657692 dB.
inline constexpr double kReleaseThresholdDb = -40.0, kReleaseSeconds = 1.0;
inline constexpr std::array<Step, 3> releaseSequence{{{-40.0, 1.0}, {-10.0, 1.5}, {-40.0, 8.0}}};
inline constexpr int kReleaseEventStep = 2;   // the event is the start of this step
inline constexpr std::array<TracePoint, 3> releaseTrace{{
    {50, 122400, -29.5337791},
    {300, 134400, -19.5239296},
    {900, 163200, -7.11292028},
}};

// Attack: family 'attack', tag a0.75 (gain/attack/a0.75.npy). Threshold -40 dBFS, ratio knob 1, attack knob .75,
// release 0.1 s, Auto off; ATT_SEQ below. Event: the -40 -> -10 dBFS step at 1 s (sample 48000), from -2.4967947 dB.
// kAttackKnobMs is the plugin Attack that MultiCompStudioVcaLaw.hpp gives for knob .75.
inline constexpr double kAttackThresholdDb = -40.0, kAttackKnobMs = 35.431, kAttackReleaseSeconds = 0.1;
inline constexpr std::array<Step, 7> attackSequence{{
    {-40.0, 1.0}, {-10.0, 1.0}, {-40.0, 3.0}, {0.0, 1.0}, {-40.0, 3.0}, {-20.0, 1.0}, {-40.0, 3.0}}};
inline constexpr int kAttackEventStep = 1;
inline constexpr std::array<TracePoint, 3> attackTrace{{
    {10, 48480, -9.78106308},
    {36, 49728, -21.3674736},
    {100, 52800, -30.3084888},
}};

// Auto Release: family 'auto', tag a0.00 (gain/auto/a0.00.npy). Threshold -40 dBFS, ratio knob 1, attack knob 0,
// release 1 s (Auto ignores it), Auto on; AUTO_SEQ below: -10 dBFS bursts of 10 ms, 50 ms, 200 ms, 1 s and 3 s on a
// -40 dBFS bed. Events: the end of the 10 ms burst (1.01 s, sample 48480) and of the 3 s burst (24.26 s, sample
// 1164480), each from -32.066 dB.
inline constexpr double kAutoThresholdDb = -40.0;
inline constexpr std::array<Step, 11> autoSequence{{
    {-40.0, 1.0}, {-10.0, 0.01}, {-40.0, 4.0}, {-10.0, 0.05}, {-40.0, 4.0}, {-10.0, 0.2}, {-40.0, 5.0},
    {-10.0, 1.0}, {-40.0, 6.0}, {-10.0, 3.0}, {-40.0, 8.0}}};
inline constexpr int kAutoShortBurstEndStep = 2, kAutoLongBurstEndStep = 10;
inline constexpr std::array<TracePoint, 3> autoAfterShortBurst{{
    {100, 53280, -28.8594818},
    {600, 77280, -14.5259409},
    {1200, 106080, -6.25607681},
}};
inline constexpr std::array<TracePoint, 3> autoAfterLongBurst{{
    {100, 1169280, -28.0240211},
    {600, 1193280, -14.0826731},
    {1200, 1222080, -6.05904531},
}};
} // namespace studioVcaFixtures
