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
    kParamOutLevel = 14,     // output parameter: peak level for the VU meter
    kParamOutputVolume = 15, // post-mix output trim, -20 to +20 dB
    kParamEchoPan = 16,      // linear wet-path pan: 0 = left, 0.5 = center, 1 = right
    kParamReverbPan = 17,    // linear spring-path pan: 0 = left, 0.5 = center, 1 = right
    kParamInputSend = 18,    // boolean input feed to tape and spring paths
    kParamWetSolo = 19,      // boolean dry-path mute
    kParamLoopSplice = 20,   // momentary relocation of the circulating tape splice
    kParamCount = 21
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
static constexpr int kNumSyncDivisions = (int)(sizeof(kSyncDivisions) / sizeof(kSyncDivisions[0]));

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
    { "bypass",         0.0f,  1.0f, 0.0f }, // kParamBypass  (host designation)
    { "out_level",      0.0f,  3.0f, 0.0f }, // kParamOutLevel (output-only meter)
    { "output_volume",  0.0f,  1.0f, 0.5f },
    { "echo_pan",       0.0f,  1.0f, 0.5f },
    { "reverb_pan",     0.0f,  1.0f, 0.5f },
    { "input_send",     0.0f,  1.0f, 1.0f },
    { "wet_solo",       0.0f,  1.0f, 0.0f },
    { "loop_splice",    0.0f,  1.0f, 0.0f }, // momentary trigger
};

// Parameters a preset (factory or user) is allowed to carry. The meter output,
// the host-designated bypass and the momentary splice trigger are all excluded:
// the first is not a control, the second must never be fought by a preset load,
// and the third would fire a splice on recall.
static inline bool teIsPresetParam(uint32_t index)
{
    return index < kParamCount
        && index != kParamOutLevel
        && index != kParamBypass
        && index != kParamLoopSplice;
}

// Requested head-1 delay for a division at the given tempo. The plugin wrapper
// octave-folds this nominal value against TapeEchoDSP's measured motor bounds;
// keeping that policy beside the DSP prevents these UI-facing definitions from
// drifting away from the supported delay range.
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
                                0.48999023f, 0.5f, 0.49499512f },
    { "Rockabilly Guitar",    {  1,   1.0f, 0.28646851f, 1.0f, 0.0f,
                                 -0.42553711f, -0.59252930f, 0.5f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.5f, 0.5f, 0.51315308f },
    { "Classic Tape Echo",    {  6,   0.50497437f, 0.39001465f, 0.28500366f, 0.0f,
                                  0.01000977f, 0.02001953f, 0.51000977f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.47065759f },
    { "Dub Throw",            {  6,   1.0f, 0.52313232f, 1.0f, 0.0f,
                                 -0.42553711f, -0.59252930f, 0.5f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.47153696f, 0.5f, 0.51315308f },
    { "Synced 1/8 Dub",       {  6,   0.39999390f, 0.43499756f, 0.11499023f, 0.04000854f,
                                  0.0f, 0.0f, 0.29000854f, 0.0f, 1.0f,
                                  1, 5, 0.5f },
                                0.66342163f },
    { "Multi-Head Bounce",    {  9,   0.53500366f, 0.48001099f, 0.5f, 0.25997925f,
                                  0.66998291f, 0.0f, 0.5f, 0.0f, 1.0f,
                                  0, 2, 0.0f },
                                0.5f, 1.0f },
    { "Orbital Echo",         {  8,   0.39999390f, 0.45001221f, 0.5f, 0.66000366f,
                                  0.80999756f, -0.40997314f, 0.5f, 0.0f, 1.0f,
                                  1, 2, 0.5f },
                                0.43145752f, 0.28500366f, 0.73001099f },
    { "Full Wash",            { 11,   0.5f, 0.22f,0.7f, 0.45f,0.0f, -0.1f,0.5f, 0.55f,1.0f, 0,   2 ,   0 } },
    { "Ambient Trails",       {  7,   0.81033325f, 0.57254028f, 0.53720093f,
                                  0.58779907f, -0.07104492f, -0.57696533f,
                                  0.47543335f, 0.0f, 1.0f, 0, 2, 1.0f },
                                0.41147773f, 0.0f, 1.0f },
    { "Worn Tape",            {  2,   0.20483398f, 0.61618042f, 0.53720093f, 0.0f,
                                 -1.0f, 0.22f, 0.47543335f, 0.0f, 1.0f,
                                  0, 2, 0.20f },
                                0.492f, 0.48483276f, 0.54565430f },
    { "Runaway Drone",        { 10,   0.0f, 0.59866333f, 1.0f, 0.0f,
                                  0.66699219f, -0.49353027f, 0.30581665f,
                                  0.0f, 1.0f, 0, 2, 0.0f },
                                0.58154625f, 0.53262329f, 0.5f },
    // Output volume was 0.38354333f, set while the spring send ran about
    // 1.6 dB hot; with the send calibrated to measured parity that trim left
    // the program 1.33 dB under its hosted counterpart.
    { "Spring Only",          { 12,   0.0f, 0.0f, 0.5f, 0.91986084f,
                                 0.0f, 0.0f, 0.5f, 0.0f, 1.0f,
                                 0,   0,   0.5f },
                               0.41680908f },
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
