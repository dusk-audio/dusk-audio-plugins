// TapeEchoParams.hpp — parameter ids and labels shared by DSP shell and UI.

#pragma once

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
    kParamBypass,       // host-designated bypass; the UI POWER switch (1 = off)
    kParamOutLevel,     // output parameter: peak level for the VU meter
    kParamCount
};

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
};
static constexpr int kNumSyncDivisions = 8;

// Head-1 delay for a division at the given tempo, octave-folded into the
// motor's mechanical range (69-177 ms). The fold always converges because
// the range spans more than one octave (177/69 > 2).
static inline double syncDelayMs(double bpm, int divisionIndex)
{
    if (bpm < 20.0 || bpm > 999.0) bpm = 120.0;
    if (divisionIndex < 0) divisionIndex = 0;
    if (divisionIndex >= kNumSyncDivisions) divisionIndex = kNumSyncDivisions - 1;
    double ms = kSyncDivisions[divisionIndex].beats * 60000.0 / bpm;
    while (ms > 177.0) ms *= 0.5;
    while (ms < 69.0)  ms *= 2.0;
    return ms;
}

// Factory presets: classic RE-201 use cases (slapback, dub, ambient washes,
// runaway drones, spring-only), same territory the commercial references
// cover. Values for params kParamMode..kParamSyncDivision, in enum order.
struct TapeEchoPreset
{
    const char* name;
    float v[kParamTapeAge + 1]; // mode, rate, int, echo, rev, bass, treb, input, wow, dry, sync, div, age
};

static constexpr TapeEchoPreset kFactoryPresets[] =
{
    //                          mode  rate  int   echo  rev   bass  treb  input wow   dry   sync div
    { "Default",              {  1,   0.5f, 0.4f, 0.8f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 0,   2 ,   0 } },
    { "Slapback Vocal",       {  1,   0.7f, 0.0f, 0.7f, 0.0f, 0.0f, 0.1f, 0.5f, 0.3f, 1.0f, 0,   2 ,   0 } },
    { "Rockabilly Guitar",    {  1,   0.8f, 0.15f,0.75f,0.0f, 0.0f, 0.2f, 0.6f, 0.4f, 1.0f, 0,   2 ,   0 } },
    { "Classic Tape Echo",    {  2,   0.35f,0.35f,0.8f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 0,   2 ,   0 } },
    { "Dub Throw",            {  2,   0.3f, 0.68f,0.9f, 0.0f, 0.35f,-0.2f,0.55f,0.5f, 1.0f, 0,   2 ,   0 } },
    { "Synced 1/8 Dub",       {  2,   0.5f, 0.6f, 0.85f,0.0f, 0.25f,-0.1f,0.5f, 0.5f, 1.0f, 1,   5 ,   0 } },
    { "Multi-Head Bounce",    {  4,   0.45f,0.25f,0.8f, 0.0f, 0.0f, 0.0f, 0.5f, 0.45f,1.0f, 0,   2 ,   0 } },
    { "Space Echo",           {  8,   0.4f, 0.40f,0.8f, 0.5f, 0.1f, 0.0f, 0.5f, 0.5f, 1.0f, 0,   2 ,   0 } },
    { "Full Wash",            { 11,   0.5f, 0.22f,0.7f, 0.45f,0.0f, -0.1f,0.5f, 0.55f,1.0f, 0,   2 ,   0 } },
    { "Ambient Trails",       {  7,   0.25f,0.72f,0.8f, 0.6f, 0.0f, -0.3f,0.45f,0.65f,1.0f, 0,   2 ,   0.25f } },
    { "Worn Tape",            {  2,   0.4f, 0.45f,0.8f, 0.0f, 0.15f,-0.5f,0.6f, 0.95f,1.0f, 0,   2 ,   0.85f } },
    { "Runaway Drone",        {  1,   0.5f, 0.95f,0.6f, 0.0f, 0.0f, 0.0f, 0.55f,0.5f, 1.0f, 0,   2 ,   0 } },
    { "Spring Only",          { 12,   0.5f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f, 0.5f, 0.3f, 1.0f, 0,   2 ,   0 } },
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
