// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
// Native reference console bus compressor captures for the BUS output stage: the dry/wet Mix control
// and the internal-magnitude-3.3 output ceiling under COMPRESSION. Every number
// here is measured from a native reference WAV in
// capture set bus-completion-20260909/ (abbreviated C below); none is
// generated from the candidate model. Provenance is named per row.
// See docs/multi-comp-2-bus-completion-2026-09-09.md.
//
// Programme for every row (C/capture_matrix.py, `high-output.wav` / `.f32`):
// 997 Hz sine at phase 0.37, four 5 s segments at -30, -18, -6 and -0.1 dBFS,
// 48 kHz stereo, both channels identical. Deliberately reaching -0.1 dBFS is
// what makes the ceiling observable at all.
//
// `gain[i]` is 10*log10 of the mean square of reference channel 0 over
// [i*240000 + 144000, +38400) at the render latency, divided by the mean square
// of the source over the same unshifted window (C/score_mix.py and
// C/score_output.py, which use the native 86-sample render latency). `peak` is
// max|channel 0| over the whole reference file.
//
// Shared controls: ratio index 2 (10:1), threshold +15 dB, attack index 5,
// release index 3, no sidechain filter, internal detection, stereo link 100,
// 2x oversampling. Headroom index h displays 4h+4 dB, so index 6 is HR28 and
// index 3 is HR16.
namespace busoutput {

// Mix holdouts at HR28 (headroom index 6) with +15 dB makeup: the hottest
// makeup/headroom corner, where a reordering of makeup, mix or the ceiling is
// most visible. Captured by C/mix_capture.py, scored by C/score_mix.py into
// C/mix-reference-scores.json.
struct Mix { int mix; double gain[4]; double peak; };
constexpr Mix mix[] = {
    // C/mix-holdout-0/LushDarkHall_high-output_stem.wav (native Mix=0).
    {  0, {0.0, 0.0, 0.0, 0.0},
          0.9885531067848206},
    // C/mix-holdout-0.5/LushDarkHall_high-output_stem.wav (native Mix=0.5).
    { 50, {10.522606114776325, 10.520643316523849,
           3.1231599828393923, -0.11591143768141578},
          0.9075433015823364},
    // C/output-holdout-hr6-m15/LushDarkHall_high-output_stem.wav (native Mix=1),
    // the same native render the ceiling row hr6/+15 below uses.
    {100, {15.143017392389764, 15.140711226849193,
           5.430464415544618, -0.19194343806027098},
          0.8376891613006592},
};

// Structural invariants measured on those same native files, not on the model.
//
// The native Mix=0 render is SAMPLE-IDENTICAL to its input across all 960000
// programme frames (max difference exactly 0.0) even at HR28 with +15 dB
// makeup: makeup, the ceiling and the transformer/convolution colour all live
// in the wet path only. The candidate reaches this through its oversampling
// round trip rather than by copying samples, so it is held to a small absolute
// difference instead of bit identity.
constexpr double mixNativeUnityDifference = 0.0;

// Native Mix=0.5 equals half dry plus half wet sample by sample to 2.98e-8,
// i.e. the control is a plain linear crossfade of exactly those two signals and
// nothing downstream of it re-scales the sum.
constexpr double mixNativeAffineDifference = 2.9802322387695312e-08;

// Output-ceiling holdouts, driven into COMPRESSION (threshold +15 with a
// -0.1 dBFS segment) so the ceiling is exercised on a gain-reduced signal, not
// only on the silent-sidechain clean path. Both headroom indices present in
// C/output-holdouts.json at both makeup values. Captured by C/output_holdout.py,
// scored by C/score_output.py; reference columns are identical in
// C/output-scores.json, C/output-scores-clean-linear.json and
// C/output-scores-clean-clip.json, which differ only in the candidate columns.
//
// The ceiling sits after makeup and inside headroom compensation, so it scales
// with 1/headroomDrive: HR16 (drive 1.0) permits 3.3 linear, HR28 (drive 3.981)
// only 0.83. hr6/+15 is the row the closeout quotes: native peak 0.837689,
// against 2.906693 for the pre-ceiling candidate.
struct Ceiling { int headroom; int makeup; double gain[4]; double peak; };
constexpr Ceiling ceiling[] = {
    // C/output-holdout-hr3-m0/LushDarkHall_high-output_stem.wav.
    {3,  0, {0.13769736592127257, 0.1376973964181836,
             0.1375006391910841, -3.525953677647184},
            1.0011062622070312},
    // C/output-holdout-hr3-m15/LushDarkHall_high-output_stem.wav.
    {3, 15, {15.143017623024411, 15.143017106638844,
             15.140710963781974, 11.087910261909343},
            3.311180353164673},
    // C/output-holdout-hr6-m0/LushDarkHall_high-output_stem.wav.
    {6,  0, {0.13769766571928582, 0.13750092524299398,
             -8.501792356658802, -13.453312842284008},
            0.5024784803390503},
    // C/output-holdout-hr6-m15/LushDarkHall_high-output_stem.wav.
    {6, 15, {15.143017392389764, 15.140711226849193,
             5.430464415544618, -0.19194343806027098},
            0.8376891613006592},
};
}
