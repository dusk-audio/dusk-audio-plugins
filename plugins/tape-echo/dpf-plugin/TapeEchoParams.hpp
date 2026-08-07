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
    kParamTempoSync,    // 1 = the leading active head locks to host tempo
    kParamSyncDivision, // shipped semantic division index; compatibility only
    kParamTapeAge,      // 0 = fresh tape/serviced transport (bit-identical to before this knob existed)
    // IDs 13 and 14 shipped in Tape Echo 0.1.x; append new parameters after them.
    kParamBypass = 13,       // host-designated bypass; the UI POWER switch (1 = off)
    kParamOutLevel = 14,     // output parameter: record-path average VU
    kParamOutputVolume = 15, // post-mix output trim, -20 to +20 dB
    kParamEchoPan = 16,      // linear wet-path pan: 0 = left, 0.5 = center, 1 = right
    kParamReverbPan = 17,    // linear spring-path pan: 0 = left, 0.5 = center, 1 = right
    kParamInputSend = 18,    // boolean program feed to the tape echo ("dub" switch)
    kParamPeakLevel = 19,    // output parameter: record-path transient peak
    kParamMix = 20,          // dry / combined-wet crossfade
    // Physical 1..11 tempo-sync detent. Appended so the shipped semantic
    // kParamSyncDivision remains available for old sessions and automation.
    kParamEchoRateNote = 21,
    kParamCount = 22
};

// Only IDs 0-14 have ever shipped (tags tape-echo-dpf-v0.1.0 through v0.1.2);
// those indices are saved-session ABI and must never move. IDs 15 and above
// were added for the unreleased 1.0.0 and are still free to change until it
// tags — which is why the retired Loop Splice slot was deleted outright rather
// than kept as a hidden placeholder.
static_assert(kParamDryLevel == 9 && kParamBypass == 13 && kParamOutLevel == 14,
              "Tape Echo 0.1.x parameter IDs are part of the saved-session ABI");
static_assert(kParamOutputVolume == 15 && kParamEchoPan == 16
              && kParamReverbPan == 17 && kParamInputSend == 18
              && kParamPeakLevel == 19 && kParamMix == 20
              && kParamEchoRateNote == 21 && kParamCount == 22,
              "Tape Echo 1.0.0 parameter layout must remain append-only");

// The compatibility pair is ORDER-DEPENDENT and that ordering is load-bearing.
//
// kParamSyncDivision and kParamEchoRateNote are two representations of one
// control, and whichever is written LAST takes ownership of the delay (see
// legacySyncDivisionOverride in TapeEchoPlugin.cpp). Every DPF backend replays
// saved parameters in ascending index order, so a 1.0.0 session ends the
// restore on kParamEchoRateNote (the detent wins, correct) while a 0.1.x
// session has no kParamEchoRateNote to replay and therefore stays on its
// stored semantic division (also correct). Both outcomes depend on the
// appended parameter having the HIGHER index.
static_assert(kParamSyncDivision < kParamEchoRateNote,
              "the appended physical detent must load after the legacy "
              "semantic division, or restoring a 1.0.0 session silently "
              "hands the delay back to a stale compatibility value");

// Tempo-sync note divisions (fraction of a quarter-note beat).
struct SyncDivision { const char* name; double beats; };
static constexpr SyncDivision kSyncDivisions[] =
{
    { "1/32",  0.125       },
    { "1/16t", 1.0 / 6.0   },
    { "1/16",  0.25        },
    { "1/8t",  1.0 / 3.0   },
    { "1/16d", 0.375       },
    { "1/8",   0.5         },
    { "1/8d",  0.75        },
    { "1/4",   1.0         },
    // Added after the original 0.1.x choices so stored division indices keep
    // their meaning. The motor clamp below handles rates outside the physical
    // range.
    { "1/32d", 3.0 / 16.0  },
    { "1/32t", 1.0 / 12.0  },
    { "1/64",  0.0625      },
    { "1/64d", 0.09375     },
    { "1/4t",  2.0 / 3.0   },
    { "5/32",  0.625       },
    { "1/2t",  4.0 / 3.0   },
    { "5/16",  1.25        },
};
static constexpr int kNumSyncDivisions = (int)(sizeof(kSyncDivisions) / sizeof(kSyncDivisions[0]));

// STORAGE order above is frozen by the saved-session ABI, and indices 8..15 were
// appended later. The reference control gives the same eleven physical tick
// marks a DIFFERENT note table according to the leading active playback head.
// Rows are leading Heads 1, 2, and 3; columns run counter-clockwise (long/slow)
// to clockwise (short/fast). kParamEchoRateNote serializes the 1-based column;
// kParamSyncDivision remains the compatibility representation of its note.
static constexpr uint8_t kSyncKnobDivisions[3][11] =
{
    { 13, 5, 4, 3, 2, 8, 1, 0, 11, 9, 10 },
    {  7, 6,12,13, 5, 4, 3, 2,  8, 1,  0 },
    { 14,15, 7, 6,12,13, 5, 4,  3, 2,  8 },
};
static constexpr int kNumSyncKnobPositions = 11;
static_assert(kNumSyncKnobPositions == 11,
              "the tempo-sync Echo Rate control has eleven detents");

// Head combinations collapse to three leading-head cases. Reverb never changes
// the sync table, so modes 1/5/8/10/11 share Head 1, modes 2/4/6/9 share Head 2,
// and modes 3/7 share Head 3.
static constexpr int teLeadingHeadIndexForMode(int mode1to12) noexcept
{
    switch (mode1to12)
    {
    case 2: case 4: case 6: case 9:
        return 1;
    case 3: case 7:
        return 2;
    default:
        return 0;
    }
}
static_assert(teLeadingHeadIndexForMode(1) == 0
              && teLeadingHeadIndexForMode(2) == 1
              && teLeadingHeadIndexForMode(3) == 2
              && teLeadingHeadIndexForMode(4) == 1
              && teLeadingHeadIndexForMode(5) == 0
              && teLeadingHeadIndexForMode(6) == 1
              && teLeadingHeadIndexForMode(7) == 2
              && teLeadingHeadIndexForMode(8) == 0
              && teLeadingHeadIndexForMode(9) == 1
              && teLeadingHeadIndexForMode(10) == 0
              && teLeadingHeadIndexForMode(11) == 0,
              "every echo mode must select the captured leading-head table");

// The table is hand-maintained alongside kSyncDivisions, so prove that every
// detent names a valid, unique storage value and that delay decreases clockwise.
static constexpr bool teSyncKnobTableIsValid() noexcept
{
    for (int head = 0; head < 3; ++head)
    {
        bool seen[kNumSyncDivisions] = {};
        for (int i = 0; i < kNumSyncKnobPositions; ++i)
        {
            const int v = (int)kSyncKnobDivisions[head][i];
            if (v < 0 || v >= kNumSyncDivisions || seen[v])
                return false;
            seen[v] = true;
        }
    }
    return true;
}
static_assert(teSyncKnobTableIsValid(),
              "tempo-sync knob detents must be valid and unique");

static constexpr bool teSyncKnobTableIsDescending() noexcept
{
    for (int head = 0; head < 3; ++head)
        for (int i = 1; i < kNumSyncKnobPositions; ++i)
            if (!(kSyncDivisions[kSyncKnobDivisions[head][i - 1]].beats
                      > kSyncDivisions[kSyncKnobDivisions[head][i]].beats))
                return false;
    return true;
}
static_assert(teSyncKnobTableIsDescending(),
              "tempo-sync knob delay must decrease clockwise");

// Front-panel knob position -> storage index.
static constexpr int teDivisionForSyncKnobPos(int pos, int leadingHead) noexcept
{
    if (pos < 0) pos = 0;
    if (pos >= kNumSyncKnobPositions) pos = kNumSyncKnobPositions - 1;
    if (leadingHead < 0) leadingHead = 0;
    if (leadingHead > 2) leadingHead = 2;
    return (int)kSyncKnobDivisions[leadingHead][pos];
}

// Storage index -> front-panel position. Old sessions and host automation can
// supply a division absent from the current leading head's table; show it at the
// nearest physical detent without rewriting the stored or automated value.
static constexpr int teSyncKnobPosForDivision(int division, int leadingHead) noexcept
{
    if (division < 0) division = 0;
    if (division >= kNumSyncDivisions) division = kNumSyncDivisions - 1;
    if (leadingHead < 0) leadingHead = 0;
    if (leadingHead > 2) leadingHead = 2;
    for (int i = 0; i < kNumSyncKnobPositions; ++i)
        if ((int)kSyncKnobDivisions[leadingHead][i] == division)
            return i;

    int nearest = 0;
    double bestError = 1.0e9;
    for (int i = 0; i < kNumSyncKnobPositions; ++i)
    {
        const double delta =
            kSyncDivisions[kSyncKnobDivisions[leadingHead][i]].beats
            - kSyncDivisions[division].beats;
        const double error = delta < 0.0 ? -delta : delta;
        if (error < bestError)
        {
            bestError = error;
            nearest = i;
        }
    }
    return nearest;
}

static constexpr bool teSyncKnobMappingsRoundTrip() noexcept
{
    for (int head = 0; head < 3; ++head)
        for (int pos = 0; pos < kNumSyncKnobPositions; ++pos)
            if (teSyncKnobPosForDivision(
                    teDivisionForSyncKnobPos(pos, head), head) != pos)
                return false;
    return true;
}
static_assert(teSyncKnobMappingsRoundTrip(),
              "each captured tempo-sync detent must round-trip exactly");

// Exact LED strings captured from Galaxy 1.3.16 at all eleven detents. Each row
// is selected by leading head; inactive earlier heads are null. A single blink
// flag applies to every active display in the row, matching the reference UI.
//
// CROSS-CHECK, and how to redo it. Every entry should equal the leading head's
// note scaled by kHeadRatio[display] / kHeadRatio[leading], named as the
// nearest table note with '+' or '-' when it is off that note by more than
// about a percent and no sign when it is within a few. Thirty-one of these
// thirty-three satisfied that rule; the two that did not were both this row's
// third column, both 8% BELOW their printed note while carrying a '+', and
// both the same substitution (a triplet replaced by the dotted form of the
// next longer note). Re-reading Galaxy at Head Select 11 (the only detent with
// all three heads live), tempo sync on, 120 BPM settled it: the display reads
// "1/8  -1/4  +1/2t", all three blinking. So position 1 below was a
// transcription slip and is corrected; position 7 is the identical geometry
// (0.3451 beats, 1/8t +3.5% vs 1/16d -8.0%) and is corrected the same way,
// still pending its own eyeball -- see RELEASE_CHECKLIST item 8.
//
// The rule is a CHECK, not the source. Where a reading and the rule disagree,
// the reading wins; do not regenerate this table from the ratios.
static constexpr const char* kSyncReadoutText[3][11][3] =
{
    {
        { "5/32",   "+1/4",    "+1/2t"  },
        { "1/8",    "-1/4",    "+1/2t"  }, // read from Galaxy, was "+1/4d"
        { "1/16d",  "-1/8d",   "+1/4"   },
        { "1/8t",   "+5/32",   "-1/4"   },
        { "1/16",   "-1/8",    "+1/4t"  },
        { "1/32d",  "-1/16d",  "+1/8"   },
        { "1/16t",  "-1/8t",   "-1/8"   },
        { "1/32",   "-1/16",   "+1/8t"  }, // same slip as position 1, was "+1/16d"
        { "1/64d",  "-1/32d",  "1/16"   },
        { "1/32t",  "1/16t",   "-1/16"  },
        { "1/64",   "1/32",    "-1/32d" },
    },
    {
        { nullptr, "1/4",    "+1/2t"  },
        { nullptr, "1/8d",   "+1/4"   },
        { nullptr, "1/4t",   "-1/4"   },
        { nullptr, "5/32",   "-1/4"   },
        { nullptr, "1/8",    "-1/8d"  },
        { nullptr, "1/16d",  "+1/8"   },
        { nullptr, "1/8t",   "-1/8"   },
        { nullptr, "1/16",   "-1/16d" },
        { nullptr, "1/32d",  "+1/16"  },
        { nullptr, "1/16t",  "1/16"   },
        { nullptr, "1/32",   "1/32d"  },
    },
    {
        { nullptr, nullptr, "1/2t"  },
        { nullptr, nullptr, "5/16"  },
        { nullptr, nullptr, "1/4"   },
        { nullptr, nullptr, "1/8d"  },
        { nullptr, nullptr, "1/4t"  },
        { nullptr, nullptr, "5/32"  },
        { nullptr, nullptr, "1/8"   },
        { nullptr, nullptr, "1/16d" },
        { nullptr, nullptr, "1/8t"  },
        { nullptr, nullptr, "1/16"  },
        { nullptr, nullptr, "1/32d" },
    },
};

// Which detents blink, captured at 120 BPM. Blinking marks a note the transport
// cannot reach, so the true condition is the tempo-dependent kMin/kMaxDelayMs
// clamp the plugin applies in run() -- this row is exactly that clamp at 120 BPM
// and is used only as the fallback where the UI cannot see the DSP (split LV2).
static constexpr bool kSyncReadoutBlinks[3][11] =
{
    { true, true, true, false, false, false, false, true, true, true, true },
    { true, true, false, false, false, false, false, true, true, true, true },
    { true, true, true, false, false, false, false, true, true, true, true },
};

// New/Used/Old are the exact normalized half-step values used by the DSP,
// plugin cache, and UI cache.
static inline float teQuantizeTapeAge(float value) noexcept
{
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return value < 0.25f ? 0.0f : (value < 0.75f ? 0.5f : 1.0f);
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
    { "peak_level",     0.0f,  3.0f, 0.0f }, // kParamPeakLevel (output-only)
    { "mix",            0.0f,  1.0f, 0.5f }, // kParamMix
    { "echo_rate_note", 1.0f, 11.0f, 5.0f }, // kParamEchoRateNote (physical detent)
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
                                0.48663175f, 0.5f, 0.49499512f },
    { "Rockabilly Guitar",    {  1,   1.0f, 0.28646851f, 1.0f, 0.0f,
                                 -0.42553711f, -0.59252930f, 0.5f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.50108422f, 0.5f, 0.51315308f },
    { "Classic Tape Echo",    {  6,   0.50497437f, 0.39001465f, 0.28500366f, 0.0f,
                                  0.01000977f, 0.02001953f, 0.51000977f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.47127065f },
    { "Dub Throw",            {  6,   1.0f, 0.52313232f, 1.0f, 0.0f,
                                 -0.42553711f, -0.59252930f, 0.5f,
                                  0.0f, 1.0f, 0, 2, 0.5f },
                                0.46571655f, 0.5f, 0.51315308f },
    { "Synced 1/8 Dub",       {  6,   0.39999390f, 0.43499756f, 0.11499023f, 0.04000854f,
                                  0.0f, 0.0f, 0.29000854f, 0.0f, 1.0f,
                                  1, 5, 0.5f },
                                0.66342163f },
    { "Multi-Head Bounce",    {  9,   0.53500366f, 0.48001099f, 0.5f, 0.25997925f,
                                  0.66998291f, 0.0f, 0.5f, 0.0f, 1.0f,
                                  0, 2, 0.0f },
                                0.49356983f, 1.0f },
    { "Orbital Echo",         {  8,   0.39999390f, 0.45001221f, 0.5f, 0.66000366f,
                                  0.80999756f, -0.40997314f, 0.5f, 0.0f, 1.0f,
                                  1, 2, 0.5f },
                                0.43145752f, 0.28500366f, 0.73001099f },
    { "Full Wash",            { 11,   0.81033325f, 0.61618042f, 0.53720093f,
                                  0.58779907f, -1.0f, 0.21356201f,
                                  0.47543335f, 0.0f, 1.0f, 0, 2, 1.0f },
                                0.49737567f, 0.48483276f, 0.54565430f },
    { "Ambient Trails",       {  7,   0.81033325f, 0.57254028f, 0.53720093f,
                                  0.58779907f, -0.07104492f, -0.57696533f,
                                  0.47543335f, 0.0f, 1.0f, 0, 2, 1.0f },
                                0.44742326f, 0.0f, 1.0f },
    { "Worn Tape",            {  2,   0.20483398f, 0.61618042f, 0.53720093f, 0.0f,
                                 -1.0f, 0.21356201f, 0.47543335f, 0.0f, 1.0f,
                                  0, 2, 1.0f },
                                0.47920631f, 0.48483276f, 0.54565430f },
    { "Runaway Drone",        { 10,   0.0f, 0.59866333f, 1.0f, 0.0f,
                                  0.66699219f, -0.49353027f, 0.30581665f,
                                  0.0f, 1.0f, 0, 2, 0.0f },
                                0.58150547f, 0.53262329f, 0.5f },
    // Output compensation is kept within 1.5 dB of the decoded counterpart;
    // it balances the calibrated spring's sparse, transient and sustained
    // program levels without altering the matched control state.
    { "Spring Only",          { 12,   0.0f, 0.0f, 0.5f, 0.91986084f,
                                 0.0f, 0.0f, 0.5f, 0.0f, 1.0f,
                                 0,   0,   0.5f },
                               0.41722554f },
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
