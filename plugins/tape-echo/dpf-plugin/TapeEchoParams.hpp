// TapeEchoParams.hpp — parameter ids and labels shared by DSP shell and UI.

#pragma once

#include <cstdint>

enum ParamId
{
    kParamMode = 0,
    kParamRepeatRate,
    kParamIntensity,
    kParamEchoLevel,
    kParamReverbLevel,
    kParamBass,
    kParamTreble,
    kParamInputGain,
    kParamWowFlutter,
    kParamDryLevel,
    kParamTempoSync,    // 1 = head-1 delay locks to a division of host tempo
    kParamSyncDivision, // note division index into kSyncDivisions
    kParamTapeAge,      // 0 = fresh tape/serviced transport (bit-identical to before this knob existed)
    // IDs 13 and 14 shipped in Tape Echo 0.1.x; append new parameters after them.
    kParamBypass = 13,       // host-designated bypass; the UI POWER switch (1 = off)
    kParamOutLevel = 14,     // output parameter: record-path average VU
    kParamOutputVolume = 15, // post-mix output trim, -20 to +20 dB
    kParamEchoPan = 16,      // linear wet-path pan: 0 = left, 0.5 = center, 1 = right
    kParamReverbPan = 17,    // linear spring-path pan: 0 = left, 0.5 = center, 1 = right
    kParamInputSend = 18,    // boolean program feed to the tape echo ("dub" switch)
    kParamWetSolo = 19,      // boolean dry-path mute
    kParamPeakLevel = 20,    // output parameter: record-path transient peak
    kParamMix = 21,          // dry / combined-wet crossfade
    kParamCount = 22
};

// Only IDs 0-14 have ever shipped (tags tape-echo-dpf-v0.1.0 through v0.1.2);
// those indices are saved-session ABI and must never move. IDs 15 and above
// were added for the unreleased 1.0.0 and are still free to change until it
// tags — which is why the retired Loop Splice slot was deleted outright rather
// than kept as a hidden placeholder.
static_assert(kParamDryLevel == 9 && kParamBypass == 13 && kParamOutLevel == 14,
              "Tape Echo 0.1.x parameter IDs are part of the saved-session ABI");

// Tempo-sync note divisions (fraction of a quarter-note beat).
struct SyncDivision { const char* name; double beats; };
static constexpr SyncDivision kSyncDivisions[] =
{
    { "1/32",  0.125       },
    { "1/16T", 1.0 / 6.0   },
    { "1/16",  0.25        },
    { "1/8T",  1.0 / 3.0   },
    { "1/16.", 0.375       },
    { "1/8",   0.5         },
    { "1/8.",  0.75        },
    { "1/4",   1.0         },
    // Added after the original 0.1.x choices so stored division indices keep
    // their meaning. These shorter values cover the remaining hosted rhythmic
    // states; the motor clamp below handles rates outside the physical range.
    { "1/32.", 3.0 / 16.0  },
    { "1/32T", 1.0 / 12.0  },
    { "1/64",  0.0625      },
    { "1/64.", 0.09375     },
    { "1/4T",  2.0 / 3.0   },
    { "5/32",  0.625       },
};
static constexpr int kNumSyncDivisions = (int)(sizeof(kSyncDivisions) / sizeof(kSyncDivisions[0]));

// STORAGE order above is frozen by the saved-session ABI, and indices 8..13 were
// appended later, so it is NOT monotonic in time: a knob sweeping raw indices
// runs long, snaps short, then long again. This table is the sweep order --
// storage indices sorted by ascending note length -- so the knob reads musically
// while the stored value keeps its meaning. UI-only; never serialized.
static constexpr uint8_t kDivSweep[kNumSyncDivisions] =
{
    10, //  1/64   0.0625
     9, //  1/32T  0.0833
    11, //  1/64.  0.0938
     0, //  1/32   0.125
     1, //  1/16T  0.1667
     8, //  1/32.  0.1875
     2, //  1/16   0.25
     3, //  1/8T   0.3333
     4, //  1/16.  0.375
     5, //  1/8    0.5
    13, //  5/32   0.625
    12, //  1/4T   0.6667
     6, //  1/8.   0.75
     7, //  1/4    1.0
};

// The table is hand-maintained alongside kSyncDivisions, so prove both of its
// invariants at compile time: it must list every storage index exactly once,
// and it must be sorted by ascending note length. Appending a division without
// updating the sweep order is then a build error, not a silently wrong knob.
static constexpr bool teDivSweepIsPermutation() noexcept
{
    bool seen[kNumSyncDivisions] = {};
    for (int i = 0; i < kNumSyncDivisions; ++i)
    {
        const int v = (int)kDivSweep[i];
        if (v < 0 || v >= kNumSyncDivisions || seen[v])
            return false;
        seen[v] = true;
    }
    return true;
}
static_assert(teDivSweepIsPermutation(),
              "kDivSweep must list every sync division index exactly once");

static constexpr bool teDivSweepIsAscending() noexcept
{
    for (int i = 1; i < kNumSyncDivisions; ++i)
        if (!(kSyncDivisions[kDivSweep[i - 1]].beats
                  < kSyncDivisions[kDivSweep[i]].beats))
            return false;
    return true;
}
static_assert(teDivSweepIsAscending(),
              "kDivSweep must be sorted by ascending note length");

// Sweep position -> storage index.
static inline int teDivisionForSweepPos(int pos) noexcept
{
    if (pos < 0) pos = 0;
    if (pos >= kNumSyncDivisions) pos = kNumSyncDivisions - 1;
    return (int)kDivSweep[pos];
}

// Storage index -> sweep position (inverse of the table above).
static inline int teSweepPosForDivision(int division) noexcept
{
    for (int i = 0; i < kNumSyncDivisions; ++i)
        if ((int)kDivSweep[i] == division)
            return i;
    return 0;
}

// Per-parameter descriptor used by the UI: `id` is the DPF parameter symbol
// (see TapeEchoPlugin::initParameter) and doubles as the key written into user
// preset files, so it must stay in step with the symbols there. min/max/def
// mirror initParameter's ranges; the UI uses them for knob ranges, INIT and
// value-based preset identity matching.
struct TeParam { const char* id; float min, max, def; };

static constexpr TeParam kTeParams[kParamCount] =
{
    { "mode",           1.0f, 12.0f, 1.0f }, // kParamMode
    { "repeat_rate",    0.0f,  1.0f, 0.0f },
    { "intensity",      0.0f,  1.0f, 0.0f },
    { "echo_volume",    0.0f,  1.0f, 0.5f },
    { "reverb_volume",  0.0f,  1.0f, 0.0f },
    { "bass",          -1.0f,  1.0f, 0.0f },
    { "treble",        -1.0f,  1.0f, 0.0f },
    { "input_volume",   0.0f,  1.0f, 0.5f },
    { "wow_flutter",    0.0f,  1.0f, 0.0f },
    { "dry_level",      0.0f,  1.0f, 1.0f },
    { "tempo_sync",     0.0f,  1.0f, 0.0f },
    { "sync_division",  0.0f, (float)(kNumSyncDivisions - 1), 2.0f },
    { "tape_age",       0.0f,  1.0f, 0.5f },
    // kParamBypass carries DPF's own designation symbol ("dpf_bypass"), not this
    // placeholder: it is excluded from presets by teIsPresetParam, so this `id` is
    // never a file key and never has to match. min/max/def do mirror the
    // designation's range, which is what the UI's POWER switch reads.
    { "bypass",         0.0f,  1.0f, 0.0f }, // kParamBypass  (host designation)
    { "out_level",      0.0f,  3.0f, 0.0f }, // kParamOutLevel (record-path VU)
    { "output_volume",  0.0f,  1.0f, 0.5f },
    { "echo_pan",       0.0f,  1.0f, 0.5f },
    { "reverb_pan",     0.0f,  1.0f, 0.5f },
    { "input_send",     0.0f,  1.0f, 1.0f },
    { "wet_solo",       0.0f,  1.0f, 0.0f },
    { "peak_level",     0.0f,  3.0f, 0.0f }, // kParamPeakLevel (output-only)
    { "mix",            0.0f,  1.0f, 0.5f }, // kParamMix
};

// Parameters a preset (factory or user) is allowed to carry. The meter outputs
// and the host-designated bypass are excluded: the meters are not controls, and
// bypass must never be fought by a preset load.
static inline bool teIsPresetParam(uint32_t index)
{
    return index < kParamCount
        && index != kParamOutLevel
        && index != kParamPeakLevel
        && index != kParamBypass;
}

// Requested head-1 delay for a division at the given tempo. The plugin wrapper
// clamps this nominal value against TapeEchoDSP's measured motor bounds, just
// as the modeled transport does when a rhythmic value is out of range.
static inline double syncDelayMs(double bpm, int divisionIndex)
{
    if (bpm < 20.0 || bpm > 999.0) bpm = 120.0;
    if (divisionIndex < 0) divisionIndex = 0;
    if (divisionIndex >= kNumSyncDivisions) divisionIndex = kNumSyncDivisions - 1;
    return kSyncDivisions[divisionIndex].beats * 60000.0 / bpm;
}

// Factory presets cover slapback, dub, ambient washes, runaway drones, and
// spring-only effects. Values for params kParamMode..kParamSyncDivision, in
// enum order.
struct TapeEchoPreset
{
    const char* name;
    float v[kParamTapeAge + 1]; // mode, rate, int, echo, rev, bass, treb, input, wow, dry, sync, div, age
    float outputVolume = 0.5f;
    float echoPan = 0.5f;
    float reverbPan = 0.5f;
    float inputSend = 1.0f;
    float wetSolo = 0.0f;
    float mix = 0.5f;
    // No bypass field: see teIsPresetParam — a recall never touches the
    // host-designated bypass, so a preset must not be able to carry one.
};

static constexpr TapeEchoPreset kFactoryPresets[] =
{
    //                          mode  rate  int   echo  rev   bass  treb  input wow   dry   sync div
    { "Default",              {  1,   0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0,   2 ,   0.5f } },
    { "Slapback Vocal",       {  1,   0.45498657f, 0.42501831f, 1.0f, 0.0f,
                                  0.11999512f, -0.08996582f, 0.51000977f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.49298415f, 0.5f, 0.49499512f },
    { "Rockabilly Guitar",    {  1,   1.0f, 0.28646851f, 1.0f, 0.0f,
                                 -0.42553711f, -0.59252930f, 0.5f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.49803940f, 0.5f, 0.51315308f },
    { "Classic Tape Echo",    {  6,   0.50497437f, 0.39001465f, 0.28500366f, 0.0f,
                                  0.01000977f, 0.02001953f, 0.51000977f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.46741243f },
    { "Dub Throw",            {  6,   1.0f, 0.52313232f, 1.0f, 0.0f,
                                 -0.42553711f, -0.59252930f, 0.5f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.46502309f, 0.5f, 0.51315308f },
    { "Synced 1/8 Dub",       {  6,   0.39999390f, 0.43499756f, 0.11499023f, 0.04000854f,
                                  0.0f, 0.0f, 0.29000854f, 0.0f, 1.0f,
                                  1, 5, 0.5f },
                                0.66342163f },
    { "Multi-Head Bounce",    {  9,   0.53500366f, 0.48001099f, 0.5f, 0.25997925f,
                                  0.66998291f, 0.0f, 0.5f, 0.0f, 1.0f,
                                  0, 2, 0.0f },
                                0.49048611f, 1.0f },
    { "Orbital Echo",         {  8,   0.39999390f, 0.45001221f, 0.5f, 0.66000366f,
                                  0.80999756f, -0.40997314f, 0.5f, 0.0f, 1.0f,
                                  1, 2, 0.5f },
                                0.43145752f, 0.28500366f, 0.73001099f },
    { "Full Wash",            { 11,   0.81033325f, 0.61618042f, 0.53720093f,
                                  0.58779907f, -1.0f, 0.21356201f,
                                  0.47543335f, 0.0f, 1.0f, 0, 2, 1.0f },
                                0.47718931f, 0.48483276f, 0.54565430f },
    { "Ambient Trails",       {  7,   0.81033325f, 0.57254028f, 0.53720093f,
                                  0.58779907f, -0.07104492f, -0.57696533f,
                                  0.47543335f, 0.0f, 1.0f, 0, 2, 1.0f },
                                0.43937928f, 0.0f, 1.0f },
    { "Worn Tape",            {  2,   0.20483398f, 0.61618042f, 0.53720093f, 0.0f,
                                 -1.0f, 0.21356201f, 0.47543335f, 0.0f, 1.0f,
                                  0, 2, 1.0f },
                                0.48252278f, 0.48483276f, 0.54565430f },
    { "Runaway Drone",        { 10,   0.0f, 0.59866333f, 1.0f, 0.0f,
                                  0.66699219f, -0.49353027f, 0.30581665f,
                                  0.0f, 1.0f, 0, 2, 0.0f },
                                0.58185389f, 0.53262329f, 0.5f },
    // Output compensation is kept within 1.5 dB of the decoded counterpart;
    // it balances the calibrated spring's sparse, transient and sustained
    // program levels without altering the matched control state.
    { "Spring Only",          { 12,   0.0f, 0.0f, 0.5f, 0.91986084f,
                                 0.0f, 0.0f, 0.5f, 0.0f, 1.0f,
                                 0,   0,   0.5f },
                               0.41785440f },
};
static constexpr int kNumFactoryPresets = (int)(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));

static constexpr const char* kModeNames[12] =
{
    "1: Head 1",
    "2: Head 2",
    "3: Head 3",
    "4: Heads 2+3",
    "5: Head 1 + Reverb",
    "6: Head 2 + Reverb",
    "7: Head 3 + Reverb",
    "8: Heads 1+2 + Reverb",
    "9: Heads 2+3 + Reverb",
    "10: Heads 1+3 + Reverb",
    "11: Heads 1+2+3 + Reverb",
    "12: Reverb Only",
};
