// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// OPTO (LA-2A) cell: CANDIDATE from package T4-P4c (2026-09-23). Lab-core use only.
//
// FEEDBACK T4 cell with the production statics frozen in:
//   side chain  d = useOptoDetector ? optoDetector : (external ? sidechain : input)
//               Compress u = (g_cell[n-1] + b_c) * d      Limit u = (a * g_cell[n-1] + b) * d
//               loop = LF_GR(Depth_GR(W(u)))  W = the production 5-biquad detector weighting (exact
//               coefficients), unity at 1 kHz; Depth = production optoDepthWeighting fed by the cell's own
//               reduction at n-1; LF_GR = the LF detector-law term, a low shelf blended in by the same
//               reduction: loop += clamp(GR / span, 0, 1) * (Shelf(loop) - loop)
//               (R14) transient LF cut ahead of Depth: loop += w_abs * w_tr * (LowShelf(loop) - loop), keyed on the
//               peak-held envelope of s at n-1; w_tr is zero once the slow tracker has met the envelope
//               s = 10^(P(pr)/20) * loop
//   EL law      (R) l *= (P_slew / P_drive)^(kappa / 2): brightness ~ f^kappa, exactly 1 at 1 kHz
//               (S) or, with elRevHysteresis > 0, l *= (f_half / 1 kHz)^kappa from significant reversals
//               (T) elFreqCorner > 0: slew low-passed at the corner first (brightness flattens above it)
//   traps       (U) G_1 <-> T: capture k_t (1 - T/T_max) G_1, emission k_r T (statics unchanged)
//   element     (P) pStrength > 0 and Peak Reduction above prGate: a term taken off the audio ahead of the
//               gain (elementTerm(), called by the mode beside gain()): the even part of a one-sided kink
//               that takes c over(x), a divider c = z / (1 + z), z = pStrength Z(PR) (PR/100)^pPrExp gain,
//               Z the measured strength per knob position (kOptoElementStrengthDb)
//   EL panel    e += beta * (|s| - e); l = Lambda(20 log10 e) (static-constrained light table, solved
//               exactly on the ripple of the settled 1 kHz sine); optional light ceiling l / (1 + l / Lmax).
//   CdS         h = 1 / (1 + (l / l_h)^m)   (equilibrium-preserving time-scale modulation)
//               G_i = (G_i + h dt A_i l) / (1 + h dt (R_i + B_i G_i)) for each population
//   conductance C = sum G_i, or with the split static C = Psi(20 log10 sum G_i) (conductance table)
//   divider     g_cell = 1 / (1 + C)   (the feedback loop taps g_cell)
//   LF floor    production energy ratio r(f) of the pre-gain detector and gain blend
//               g = (g_cell^k + (1 - g_cell^k) r^(k/2))^(1/k), returned for the NEXT sample.
// With full stereo link g_L == g_R, so the per-channel cell forms the linked feedback side chain as
// g_cell[n-1] * optoDetector. Interface as defined by T4-P1-CELL: process(...) returns the next-sample
// gain; gain(), inputLevelDb(), dynamicGrDb() (the cell reduction before the LF floor, as production).
// setParams()/params()/primeEquilibrium()/lightAt()/conductanceAt()/elLevel() are lab-only hooks used by the
// fitting kernel; OPTO_LAB_PROBE is empty unless a lab build defines it.
#pragma once
#include "MultiCompOptoShelf.hpp"
#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#ifndef OPTO_LAB_PROBE
#define OPTO_LAB_PROBE(channel, appliedGain, cellGainValue)
#endif

namespace duskaudio
{

inline constexpr std::array<float, 23> kOptoCompressCurve{{
    0.9207f, 1.9474f, 3.1430f, 4.6974f, 6.1696f, 7.6165f,
    9.2638f, 10.8676f, 12.4485f, 13.9366f, 15.7917f, 17.2499f,
    19.0803f, 20.4897f, 22.3318f, 23.8425f, 25.2656f, 26.5058f,
    28.2780f, 29.5828f, 30.6369f, 32.1649f, 32.9052f}};
inline constexpr std::array<float, 23> kOptoLimitCurve{{
    0.9379f, 1.9978f, 3.2454f, 4.8601f, 6.4467f, 8.0793f,
    9.6964f, 11.7394f, 13.5249f, 15.2602f, 17.4341f, 19.2438f,
    21.5010f, 23.3864f, 25.7863f, 27.9270f, 30.0609f, 32.0834f,
    34.7103f, 36.8851f, 38.8184f, 40.5691f, 40.9082f}};

inline float optoThresholdDb(float peakReduction, bool limit) noexcept
{
    // The reference knob is normalised 0..1; Multi-Comp exposes the same
    // control as a displayed 0..100 percentage. Compress and Limit use
    // their independently measured onset tables.
    constexpr std::array<float, 9> compressThresholds{{
        -3.8483f, -10.8579f, -17.0206f, -21.4516f, -25.7740f,
        -33.8121f, -40.1926f, -44.3555f, -45.4059f}};
    constexpr std::array<float, 9> limitThresholds{{
        -4.1256f, -11.1198f, -17.2740f, -21.6889f, -26.0047f,
        -34.0625f, -40.5015f, -44.6568f, -45.6471f}};
    const auto& thresholds = limit ? limitThresholds : compressThresholds;
    const auto& curve = limit ? kOptoLimitCurve : kOptoCompressCurve;
    const float onsetOffset = (1.0f - curve[0])
        / ((curve[1] - curve[0]) * 0.5f);
    const float thresholdCorrection = -onsetOffset;
    const float normalised = std::clamp(peakReduction * 0.01f, 0.0f, 1.0f);
    if (normalised <= 0.1f) return 1000.0f;

    // The first measured threshold is at 0.2.  Continue its measured
    // 0.2->0.3 slope toward 0.1 so automation stays continuous while still
    // leaving 0.0 and 0.1 inactive over the measured input range.
    if (normalised < 0.2f)
        return thresholds[0] + thresholdCorrection
            + (normalised - 0.2f) * (thresholds[1] - thresholds[0]) * 10.0f;
    if (normalised >= 1.0f) return thresholds.back() + thresholdCorrection;
    // The original tenth-step sweep did not sample PR 0.55.  A dedicated
    // steady-state capture there places the Compress threshold 0.53 dB
    // below the linear 0.50 -> 0.60 interpolation (measured GR residuals
    // were -0.239/-0.377/-0.500 dB at -24/-18/-12 dBFS).  Preserve the
    // measured endpoints and interpolate through the new midpoint.
    if (!limit && normalised >= 0.5f && normalised < 0.6f)
    {
        constexpr float midpointThreshold = -24.1428f;
        if (normalised < 0.55f)
            return thresholds[3] + thresholdCorrection
                + (normalised - 0.5f)
                    * (midpointThreshold - thresholds[3]) * 20.0f;
        return midpointThreshold + thresholdCorrection
            + (normalised - 0.55f)
                * (thresholds[4] - midpointThreshold) * 20.0f;
    }
    const float position = (normalised - 0.2f) * 10.0f;
    const size_t index = static_cast<size_t>(position);
    const float fraction = position - static_cast<float>(index);
    return thresholds[index] + thresholdCorrection
        + fraction * (thresholds[index + 1] - thresholds[index]);
}

inline float optoCurveDb(float overshootDb, bool limit) noexcept
{
    const auto& curve = limit ? kOptoLimitCurve : kOptoCompressCurve;
    const float position = overshootDb * 0.5f;
    if (position <= 0.0f) {
        if (limit) return std::max(0.0f, curve[0] + position * (curve[1] - curve[0]));
        return curve[0] * std::exp(position * (curve[1] - curve[0]) / curve[0]);
    }
    if (position >= static_cast<float>(curve.size() - 1))
    {
        const float extraPosition
            = position - static_cast<float>(curve.size() - 1);
        if (limit)
            return curve.back() + extraPosition
                * (curve.back() - curve[curve.size() - 2]);

        // Transition from the table's final measured slope to just below
        // unity (1.90 dB GR per 2 dB input), then approach the Limit
        // continuation smoothly. Both joins are C1: a hard slope change at
        // the table edge and the old min() crossing produced unmeasured
        // output-slope steps.
        constexpr float compressInitialSlope = 1.90f;
        constexpr float slopeTransitionPositions = 0.10f;
        const float tableEndSlope = curve.back()
            - curve[curve.size() - 2];
        if (extraPosition < slopeTransitionPositions)
            return curve.back() + tableEndSlope * extraPosition
                + (compressInitialSlope - tableEndSlope)
                    * extraPosition * extraPosition
                    / (2.0f * slopeTransitionPositions);
        const float limitSlope = kOptoLimitCurve.back()
            - kOptoLimitCurve[kOptoLimitCurve.size() - 2];
        const float limitContinuation = kOptoLimitCurve.back()
            + extraPosition * limitSlope;
        const float transitionValue = curve.back()
            + slopeTransitionPositions
                * (tableEndSlope + compressInitialSlope) * 0.5f;
        const float limitAtTransition = kOptoLimitCurve.back()
            + slopeTransitionPositions * limitSlope;
        const float initialGap = limitAtTransition - transitionValue;
        const float convergenceRate = (compressInitialSlope - limitSlope)
            / initialGap;
        return limitContinuation - initialGap
            * std::exp(-convergenceRate
                * (extraPosition - slopeTransitionPositions));
    }
    const size_t index = static_cast<size_t>(position);
    const float fraction = position - static_cast<float>(index);
    return curve[index] + fraction * (curve[index + 1] - curve[index]);
}

// The one-sided input element (OptoCell::elementTerm). The reference clips the positive side of the cell's
// input softly above a fixed level, more the further Peak Reduction is turned up and the less the cell is
// reducing: a second harmonic of 2.5-5 % on a settled 1 kHz sine at Compress, Peak Reduction 1, on any
// programme, with its companion, a step of the mean when a note ends. Measured (R12 / R13, 2026-10-02): a
// hard kink at -26.1 dBFS peak whatever the knob, mode or frequency, positive side only, its second harmonic
// falling 1.00 dB per dB of reduction at PR .4 / .5, 0.96-0.98 at .65 / .8 and 0.82-0.90 at PR 1.
// The form that gives that bend with nothing added is a divider: of what is over the kink the element takes
// c = z / (1 + z), z = Z(PR) times the gain (a conduction path through a resistance set by the knob onto a
// node whose impedance follows the cell's transmission).
// Since 2026-10-08 the cell uses the WHOLE kink (pAsym = 1), taken off the sample ahead of the gain. Its odd
// part is a loss of level: the cell's tables were refitted with that loss outside them (the defaults' history
// below), and the reduction meter reads it (OptoCell::meterGain). It is memoryless, so there is no time
// constant. The strength table below was solved for the form before and has not been solved again.
// Before (2026-10-07) the cell used the kink's EVEN part alone (pAsym = 0): half of what the kink takes, taken
// from a positive sample and from a negative one alike, because every table then carried the loss. That form
// has no odd harmonics of its own, and it read the first positive half-cycle of a low tone from dark too
// shallow at high Peak Reduction (the kink takes most where the gain is still high). Its figures: on a
// settled 1 kHz sine at Peak Reduction 1 the reference's third harmonic is 0.3-1.8 %, here 0.15 %; at Peak
// Reduction .7 (the core test's rows at -24 / -12 / -4 dBFS) the third and fifth stay where they were
// without the element (fifth: -76 / -78 / -97 dBc against the reference's -63 / -66 / -71; third at
// -24 dBFS -59 against -54), and that test stays red on them. The whole kink (pAsym = 1) with its loss
// returned through the gain reads -64 / -68 / -83 and -55 there.
// Z(PR), below, is solved per knob position so that the second harmonic of a settled 1 kHz sine at -14 dBFS rms
// equals the reference's (tools opto/p_sweep.py; the sines carry a -70 dBFS pilot). The two modes were solved
// separately: within 0.05 dB of each other up to .75, 0.15-0.4 dB apart from .8 to .925 and 0.7 dB at .95 and
// above, where Compress asks for more. One table serves both, their mean. (With the whole kink and its loss
// kept in the gain the two modes agreed within 0.15 dB everywhere: what separates them here is the level the
// tables carry instead.) The same sine at -20 and -8 dBFS rms, not used, then reads within 5 % of the
// reference's second harmonic through .925 and within 9 % at the top. Linear in dB between the positions,
// the first slope continued below .2; the knob does nothing more above .95, as the reference's.
inline constexpr std::array<float, 15> kOptoElementPr{{
    0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.925f, 0.95f, 0.975f, 1.0f}};
inline constexpr std::array<float, 15> kOptoElementStrengthDb{{
    -49.881f, -42.566f, -37.486f, -34.251f, -31.558f, -26.907f, -19.006f, -13.420f,
    -8.637f, -3.642f, 2.925f, 7.670f, 14.136f, 14.175f, 14.175f}};

inline float optoElementStrength(float prNormalised) noexcept
{
    const auto& pr = kOptoElementPr;
    const auto& db = kOptoElementStrengthDb;
    size_t k = 1;
    while (k + 1 < pr.size() && prNormalised > pr[k]) ++k;
    const float slope = (db[k] - db[k - 1]) / (pr[k] - pr[k - 1]);
    return decibelsToGain(db[k - 1] + (std::min(prNormalised, pr.back()) - pr[k - 1]) * slope);
}

// ---- BEGIN GENERATED DEFAULTS (Q) ----
// Indices 0..61: production TFU1, landed from the frozen lab fit on 2026-09-28, except lfLitFloor (index 29).
// HAND-MAINTAINED since R14 (2026-10-03, docs/opto-r14-lf-onset-2026-10-03.md): lfLitFloor 0 -> 0.8 (LF shelf mostly
// on when dark) and the transient LF cut (indices 62..70), fitted on the 60 Hz / 1 kHz onset probes and selected on
// FIT music cells. Refitted 2026-10-05 on the same 84 probe rows after the side-chain drive limit landed (tools
// opto/greybox/gb_r14_fit.py, bounded, no music in the fit): lfLitFloor 0.8 -> 0.980904 and the cut 14 dB / 300 Hz /
// -38 / 6 / 0.3 ms / 30 ms / 12 -> 15.38 dB / 74.4 Hz / -35.70 / 1.645 / 0.152 ms / 3.29 ms / 1.22 (hold and gap
// unchanged); probe rows at or above 1 dB 14 -> 4. Then with the cut weighted per mode (lfCutCompWeight, index 71:
// Limit wants more cut, Compress less, which one depth cannot give): 22.37 dB / 54.7 Hz / -36.04 / 1.49 / 3.36 ms /
// 0.90, Compress weight 0.70; rows at or above 1 dB 4 -> 1 (1.01).
// Refitted 2026-10-06 with the onset read across frequency: the table above holds 60 Hz and 1 kHz only, so the cut's
// shape between them, where a kick drum lives, was never constrained. Native captures of the same tone and burst
// forms at 40 / 80 / 100 / 150 / 200 / 400 Hz and of a burst sweeping 160 -> 50 Hz at Limit .8125, Comp 1, Comp .625
// and Comp .5 (364 rows; tools opto/greybox/gb_onsetf.py) read 39 rows at or above 1 dB with the constants above (kick
// forms 1.0-1.5 dB deep in the first 20 ms at Limit, 1.0-1.6 dB shallow at Comp .625). Thirteen constants fitted on
// those rows and the 84 together, no music in the fit: the cut 27.23 dB / 67.6 Hz / -36.51 / 1.01 / 3.35 ms / 0.88,
// Compress weight 0.64, lfLitFloor 1.0; the flash population 0.784 / 287.6 / 0.0992 -> 2.073 / 148.6 / 0.0597 and the
// time scale 5.761 / 0.2715 -> 17.63 / 0.3612 (indices 20-22, 12, 13). Rows at or above 1 dB: 1 -> 0 of the 84,
// 39 -> 28 of the 364. Not reachable by constants: the native unit's light pulse comes right after the drive's zero
// crossing at deep settings (its reduction on a 20 Hz sine steps up within the first fifth of each half-cycle and is
// flat for the rest; tools opto/lf_cycle.py) while this cell's light follows the drive's level and peaks with it.
// 2026-10-07: the input element on (indices 47..51: pStrength 0 -> 1, the measured table as it is; pPrExp
// 10 -> 0; pThresh .047 -> .0497, R13's -26.07 dBFS; pKnee .1 -> 0, a hard kink; pAsym .3 -> 0, its even
// part alone). See kOptoElementStrengthDb.
// 2026-10-08, twelfth-pass continuation. These scratch defaults qualified on every requested candidate table.
// The measurements below are the ob1 qualification of these exact float32 defaults; final header-default
// verification and the two scratch patches are recorded separately. The plugin source is not edited or approved
// for landing by this history.
//
// Frozen candidate: direction2-original-bars-direction1.json, SHA-256
// 109f64fbd516d29873c788b77f3aacb36f24446b0507f678c7b9a25438c8c850. The ob1 landing-form header's 78 cell
// defaults and both six-entry laws match this candidate at float32, including inherited defaults. The fitted two4
// form with this candidate and the landing form with header defaults produced 28 of 28 byte-identical renders. FET
// identity against mc2_lab_s11final3 was 136 of 136 renders.
//
// Plugin synthetic tables, measured before -> after (RMS dB / worst absolute dB / cells over 1.00 dB): onset364
// 0.554792221 / 3.466613098 / 22 -> 0.343269838 / 0.957042943 / 0; E1 84 0.354904400 / 0.846924624 / 0 ->
// 0.340184573 / 0.930487600 / 0; settled186 0.269145465 / 0.821229119 / 0 -> 0.334423586 / 0.995912341 / 0.
// Release280 0.444939883 / 2.558974590 / 12 -> 0.430558029 / 2.247932318 / 8. The before arrays are the fresh
// unchanged-source capture baseline_raw_fresh_1545.npz; all plugin gate cells are within the original 1.00 dB bar,
// and release improves all three reported aggregate measures.
//
// Music210: 0 cells at or above 1.00 dB, worst 0.980710368 dB; FIT / VAL / SEALED tier RMS 0.369562741 /
// 0.556171539 / 0.520562237 dB, against original-source tiers 0.484 / 0.588 / 0.603 dB. Stereo-link worst 0.92 dB.
// The cell-kernel onset364 reads RMS 0.342 dB, worst 0.99 dB, 0 cells at or above 1.00 dB; kernel E1 84 reads
// 0.337 dB / 0.94 dB / 0. C2 DEV732 pooled RMS 0.3985 dB, against original-source 0.4687 dB. These kernel and
// stereo figures are the evaluator's printed precision.
//
// Recovery20: all rows pass the unchanged original criteria, against four failing original-source rows (2, 5, 8,
// 14). Native controls: all133 rows pass; the complete original-source passing mask protects124 rows. Worst
// steady-state absolute error is 0.498872757 dB, against the original strict 0.50 dB limit; knee and rate retain
// their original strict 0.10 and 0.30 dB limits, and the separate rate-spread limit remains 0.01 dB. Original
// tests and goldens were not changed or re-recorded.
//
// This generation retains the existing continuous two-polarity charge law and changes its constants and the
// existing light, population, floor and input-element constants. No new detector, classifier, estimator, counter,
// table shape or element form was introduced. The drive-limit scale flux[0] is 0, lfCutDb is 0, and scratch-only
// elPolRelax is 0 and is omitted from the shipping 78-slot form. The input element uses the whole kink, pAsym = 1,
// with pStrength = 0.8967061 and the existing fixed strength table. That table was not solved again for the whole
// kink.
//
// The unchanged original recovery/frequency/knee/reset switches all exit 0. Against the freshly recompiled
// original source, there is no earlier or new core failure: the default suite reaches the same FET-golden red
// pair, the prefix reaches the same H2-H5 red assertion, and 13 of all14 neighbors pass with only the same
// internal-link assertion still red. Its first failure leaves later internal-link cases unreached. All22 test
// files remain unchanged. Active OPTO stereo-to-mono-to-stereo, active-to-inactive Peak Reduction, repeated
// same-rate preparation with panel history, and OPTO-specific zero/one-sample blocks remain unverified by the
// existing neighbor tests; generic controls do not prove those transitions. Reset clears the added channel
// states through ChannelState{}. Complete native133 controls pass with no loss of the original124 passing rows.
// Final header-default tables, OFF346 and FET136 identities, release passing-cell masks and the scratch patch
// chain are retained in report-s12-final-obfinal20261008 and its linked evidence.
//
// Owner decisions remain open: the per-polarity charge law under the 09-30 no-classifier rule; patch33's unrelated
// FET golden pair; elPolCredit above 1 as an unresolved model-policy flag (this candidate is below 1); the meter
// reading the input element's loss; and the strength table not re-solved for the whole kink. This note makes none
// of those decisions. The landing and patches are prepared for review only and are not applied to the plugin
// source.
// 2026-10-09, fifteenth pass: the release in the dark (indices 78..82, see process(): a limit on how fast the
// conductance falls once the panel is not driven and the traps are full, and the floor's share of it held
// meanwhile). 30 /s, 0.3 ms, 0.01, 0.25 s, 2 ms; nothing else changed. Read on a pilot through the silence after
// a note, native unit against the plugin, cells at or over 1 dB / worst cell / rms, before -> after:
//   the release table (tools opto/silence_probe.py, five notes at -8 dBFS, four settings, 280 cells)
//     8 / 2.25 / 0.431 -> 2 / 1.34 / 0.334;
//   notes in no table (opto/silence_holdout.py: 1 kHz at -2 / -14 / -20 dBFS, 300 Hz, 3 kHz, 70 Hz, 2 s and
//     60 ms notes; Compress 1 and .8, Limit .8125 and 1; 416 cells) 87 / 6.70 / 1.319 -> 35 / 2.80 / 0.641;
//   2-20 ms bursts (opto/silence_short.py, 130 cells) 9 / 2.12 / 0.550 -> 3 / 1.19 / 0.467.
// The ceiling was set on the table and the notes in no table together; the bursts and the notes judged the
// form (a limit without the traps' fill reads the bursts 4 dB wrong and fails the core suite's 2 ms recovery
// row). Onset 364, E1 84 and settled 186 read the same to the printed digit; music 210: 0 cells at or over
// 1 dB, worst 0.981 -> 0.977, largest change of a cell 0.019 dB; FET 136 of 136 renders byte-identical.
// Still over in the table: Compress 1, the 50 Hz note with a second harmonic, 150 and 200 ms after it
// (-1.34 / -1.21). The native unit holds 0.5-1.4 dB more reduction there after 50 Hz notes at Compress 1
// only (not at Limit .8125, not after 70 Hz, 100 Hz or 1 kHz), the same for both polarities of the note
// (opto/silence_asym.py); the cause is not found. Still over in no table: 40 dB and deeper (the 2 ms the
// state takes to come on), 2 s notes at Limit 1 from 150 ms on (+1.4 .. +1.9, as before), 3 kHz at Limit 1
// (1.25 dB too little reduction in the note itself, as before).
// The lab generator (tools repo, opto/pt_header.py) emits the 62-entry form and refuses to overwrite
// a header that carries lfCutDb; port indices 62..71 there before regenerating.
// 2026-10-10, patch 40: the measured 1.0108 Hz input high-pass also feeds the detector. Its gate-edge
// tail explains the low-note contrasts; no follower or pulse element is added. The pre-coupling input owns
// the floor hand-back and release laws. Measured master ceiling 27.87476 /s, existing two 0.3 ms stages,
// hold 0.5 ms; a memory-population rate correction acts below 24 dB and reverses below 2.5 dB. Light-table
// coordinates stay fixed. The floor/onset nine-constant joint fit moves each by at most 0.867 percent.
// Qualified production build, original patch 39 -> candidate: release280 failures 2 -> 0 (worst 0.694 dB);
// held-out416 35 -> 5 (worst 2.795 -> 1.776, RMS .6411 -> .3147); short130 3 -> 1 (1.192 -> 1.097).
// Blind154 and blind238 both pass (worst .772 / .973); onset364, E1 84, settled186 pass. Music210 passes,
// worst FIT/VAL/SEALED tiers .37093/.55776/.52088 -> .36766/.55447/.50605; C2 .39847 -> .32856.
// Core recovery/frequency/knee/reset and lifecycle pass; off346 and FET136 are byte-identical. Remaining
// held-out misses are the Limit 1 3 kHz row; the remaining short cell is the 10 ms Limit .8125 burst at40ms.
// The seven history coefficients below are rounded to float; all production tables were requalified.
// Indices 83 (input coupling) and 84 (history strength) are on at 1; 0 at either restores patch 39's law. The
// light-law coordinates and table stay frozen.
// 2026-10-10, patch 41: long notes and short bursts. After 10 to 120 s notes at Limit patch 40 held too much
// reduction (72 one-kHz notes with masters: 15 over 1 dB, worst +3.09 dB at Limit 1, 0 dBFS, 120 s, 315 ms after
// the note; Compress within 0.61). The memory share was too saturated: darkHistoryMemory 3 -> 16.31,
// darkHistoryShoulderMemory 2.30 -> 17.39, darkHistoryDensity 0.943 -> 5 (the share is bounded by 1 / (1 + 5)).
// The ceiling's exposure weight is the traps' fill to the power darkTrapFillPower, 1.7349 from separate
// 6 to 75 ms bursts (2 = patch 40). Patch 40 -> 41: long notes 15 -> 0 over (worst 0.988); short bursts 130
// 1 -> 0 over (1.097 -> 0.968); held-out 416 5 over, worst 1.776 -> 1.772, RMS .3147 -> .3121; onset, E1, settled,
// release 280 unchanged in their worst cells; blind 154 / 238 pass (worst .772 / .966); C2 .32856 -> .32844;
// music 210 cells pass, worst tiers FIT .36766 -> .36724, VAL .55447 -> .55468, SEALED .50605 -> .50617 (the two
// rises, +.00021 and +.00012 dB, are inside the .001 dB equality floor for pooled RMS the owner set 2026-10-10).
// Remaining over 1 dB: the Limit 1 3 kHz row, first 60 ms (in-note high-frequency miss).
// 2026-10-10, patch 42: the light plateau of a drive faster than 1 kHz. The native unit saturates on a 1 kHz
// note (Limit 1, in-note reduction 39.81 / 41.16 / 41.97 / 42.51 dB at -8 / -6 / -4 / -2 dBFS) but reaches 45.5 dB
// at 3 to 5 kHz; this cell's light table is flat above its entry 27 for every frequency, so hot notes of 1.5 to
// 6 kHz stopped near 43 dB (20 notes at Limit .9 / 1, -8 / -2 dBFS: 15 over 1 dB, worst -2.26). Not the
// per-polarity charge cap: a table that keeps rising moved 1 kHz by +1.7 dB. Above elSatKneeDb the light is
// multiplied by 1 + elSatBoost x (speed - 1 - elSatMargin), coming in over elSatSpanDb; speed is the ratio of
// 5 ms averages of slew power (through the 875 Hz corner) and level power, 1 for a settled 1 kHz sine, limited
// to a settled tone's maximum. Forms that failed: a 5 ms copy of the per-sample ratio kept what preceded the
// note and counted a 1 kHz burst as fast (7 of 288 burst readings over 1 dB); without a band limit the element
// over-delivered from 7 to 10 kHz (blind notes at 8 / 12 kHz +1.17 / +1.13), and with a fade to 9 kHz one
// blind note at 7.5 kHz ended 0.24 dB worse than before; so the element is confined to the band it was
// calibrated in and fades out between elSatFadeLoKHz and elSatFadeHiKHz, read from the unfiltered slew
// ratio. From 8 kHz up, at 1 kHz and below the knee nothing changes (within 0.01 dB; at a hot note's
// onset the speed reads high for a fraction of a millisecond). Constants from 44 calibration notes only.
// Patch 41 -> 42: held-out 416 5 -> 0 over, worst 1.772 -> 0.930, RMS 0.3121 -> 0.2600; calibration notes 1.5 to
// 6 kHz worst 0.48; third blind HF set (17 notes, never fitted) 7 -> 0 over, worst 2.17 -> 0.85; short 130,
// onset 364, E1 84, settled 186, release 280, blind 154 / 238, long notes 72, burst calibration 288, native
// frequency 122 and recovery 20 pass; music 210 cells pass, worst tiers FIT / VAL / SEALED 0.36724 / 0.55468 / 0.50617
// -> 0.36641 / 0.55285 / 0.50597; C2 0.32844 -> 0.32831. NOT fixed, older than this patch: hot notes above 7.5 kHz at Limit
// (8 kHz up to -1.65 dB, 12 to 16 kHz -0.7 to -4.4 dB at -2 dBFS: the side-chain weighting up there and the
// table's uneven top).
// kOptoCellDefaults order: elTauSeconds, lightCeiling, fastRelease, fastBimolecular, slowCharge, slowRelease, slowBimolecular, memoryCharge, memoryRelease, limitOutputMix, limitInputMix, compressInputMix, timeScaleLight, timeScaleExponent, depthWeighting, lowFrequencyFloor, lfTermGainDb, lfTermFrequency, lfTermGrSpan, conductanceTableOn, flashCharge, flashRelease, flashBimolecular, lfShelfDb, hFloor, lfLitTauAttack, lfLitTauRelease, lfLitLevelDb, lfLitSpanDb, lfLitFloor, lfLitLog, hChargeOnly, attackRateDbPerS, attackRatePower, attackRateTau, elFreqExponent, elFreqTau, hfPeakDb, lfPeakDb, midPeakDb, hf10kPeakDb, elRevHysteresis, elRevRelease, elFreqCorner, trapCapture, trapEmit, trapCapacity, pStrength, pPrExp, pThresh, pKnee, pAsym, lightTableStartDb, lightTableStepDb, lightSlopeAbove, conductanceTableStartDb, conductanceTableStepDb, conductanceSlopeAbove, prStart, prStep, prSlopeBelow, prGate, lfCutDb, lfCutFrequency, lfCutStartDb, lfCutSpanDb, lfCutTauAttack, lfCutTauSlow, lfCutTransSpanDb, lfCutHoldSeconds, lfCutMaxGapDb, lfCutCompWeight, elPolCap, elPolLeak, elPolKnee, elPolRefill, elPolCredit, elPolCapExp, darkRateCeiling, darkTau, darkLight, darkFloorTau, darkHoldSeconds, inputCouplingOn, darkHistoryStrength, darkHistoryBase, darkHistoryMemory, darkHistoryShoulder, darkHistoryShoulderMemory, darkHistoryLow, darkHistoryLowMemory, darkHistoryDensity, darkTrapFillPower, elSatBoost, elSatMargin, elSatSpanDb, elSatKneeDb, elSatFadeLoKHz, elSatFadeHiKHz
inline constexpr std::array<float, 99> kOptoCellDefaults{{
    2.11744767e-06f, 0.0f, 8.05137539f, 2.89539862f, 4.60660553f, 23.5234127f,
    0.586750925f, 0.00118519559f, 0.999977779f, 0.990760822f, 0.0365957224f, 2.38673383e-05f,
    19.8983517f, 0.326484203f, 1.0f, 1.0f, -1.21699251f, 379.874676f,
    40.0f, 0.0f, 2.51558924f, 92.4281616f, 10.2401028f, 6.85569138f,
    0.0f, 0.0193125313f, 0.020685854f, -28.593274f, 42.5204821f, 0.985030591f,
    1.0f, 0.0f, 0.0f, 2.0f, 0.01f, 0.357124388f,
    0.000936036813f, 1.55828697f, 0.301784265f, 0.00367525373f, -1.19515582f, 0.0f,
    0.02f, 875.517212f, 62.0298299f, 3.32803035f, 14.1897856f, 0.896706104f,
    0.0f, 0.0497f, 0.0f, 1.0f, -58.541214f, 0.990164995f,
    0.082666399f, 0.0f, 0.1f, 0.0f, 0.3125f, 0.015625f,
    59.1923887f, 0.15f, 0.0f, 67.6368847f, -36.5089119f, 1.00680706f,
    0.000151601f, 0.00335447044f, 0.882035191f, 0.02f, 40.0f, 0.635185614f,
    1.031764415f, 107.610489f, 0.7941493488f, 6.07100083e-05f, 0.9903732738f, 0.0265073478f,
    27.87475932f, 0.0003f, 0.01f, 0.25f, 0.0005f, 1.0f,
    1.0f, 0.1630389988f, 16.31156673f, 0.1077029333f, 17.38801884f, 0.2356715649f,
    5.404491238e-18f, 5.0f, 1.734882487f, 1.2f, 0.05f, 2.5f,
    -33.3f, 6.0f, 7.5f
}};

inline constexpr std::array<float, 37> kOptoCellLightTable{{
    0.0f, 0.00912684295f, 0.0119820964f, 0.0055029667f, 0.00670960546f, 0.00696813967f,
    0.00804904569f, 0.0093537569f, 0.0112371184f, 0.0152263958f, 0.0775384083f, 0.359695494f,
    0.89149797f, 2.84241629f, 6.8655405f, 14.5869112f, 31.5150318f, 63.6852684f,
    133.770477f, 229.237335f, 339.253937f, 462.929565f, 680.991394f, 987.750671f,
    1365.13977f, 1858.52783f, 2430.92554f, 3437.19482f, 3202.54712f, 3421.12549f,
    2995.16431f, 2868.4541f, 2989.75293f, 2424.57397f, 2666.40479f, 2836.38477f,
    3145.78638f,
}};

inline constexpr std::array<float, 2> kOptoCellConductanceTable{{
    0.0f, 0.0f,
}};

inline constexpr std::array<float, 45> kOptoCellPrGainDb{{
    -33.9895821f, -33.0647011f, -31.7282295f, -30.5767021f, -29.6676674f, -28.8836613f,
    -28.1747055f, -27.5470505f, -26.8787422f, -26.1037903f, -25.3618774f, -24.7241745f,
    -24.0803757f, -23.129549f, -22.2184792f, -21.4799328f, -20.9825573f, -20.4134197f,
    -19.8114147f, -19.2458191f, -18.5005341f, -17.7552509f, -16.0216465f, -14.4301796f,
    -12.8165121f, -11.2860289f, -9.74315739f, -8.65912247f, -7.55919933f, -6.66117764f,
    -6.04502869f, -5.35649109f, -4.63854742f, -3.85050201f, -3.09465241f, -2.24689794f,
    -1.72590232f, -1.26240313f, -0.875841439f, -0.556340396f, -0.211995497f, 0.0f,
    0.0f, 0.0f, 0.0f,
}};

// ---- END GENERATED DEFAULTS (Q) ----

struct OptoCellParams
{
    float elTauSeconds, lightCeiling;
    float fastRelease, fastBimolecular;
    float slowCharge, slowRelease, slowBimolecular;
    float memoryCharge, memoryRelease;
    float limitOutputMix, limitInputMix, compressInputMix;
    float timeScaleLight, timeScaleExponent;
    float depthWeighting, lowFrequencyFloor;
    float lfTermGainDb, lfTermFrequency, lfTermGrSpan;
    float conductanceTableOn;
    float flashCharge, flashRelease, flashBimolecular;
    float lfShelfDb, hFloor;
    float lfLitTauAttack, lfLitTauRelease, lfLitLevelDb, lfLitSpanDb, lfLitFloor, lfLitLog, hChargeOnly;
    float attackRateDbPerS, attackRatePower, attackRateTau;
    float elFreqExponent, elFreqTau, hfPeakDb, lfPeakDb, midPeakDb, hf10kPeakDb;
    float elRevHysteresis, elRevRelease, elFreqCorner;
    float trapCapture, trapEmit, trapCapacity;
    float pStrength, pPrExp, pThresh, pKnee, pAsym;
    // R14 transient LF cut in the side-chain drive (see process()): shelf depth and corner, absolute drive gate
    // (start, span), envelope attack, slow-tracker time constant, transient gate span, envelope peak hold, and
    // the largest gap the slow tracker may trail the envelope by, and the cut's weight in Compress mode relative
    // to Limit (1 = the same; the two modes feed the side chain differently).
    float lfCutDb, lfCutFrequency, lfCutStartDb, lfCutSpanDb, lfCutTauAttack, lfCutTauSlow, lfCutTransSpanDb;
    float lfCutHoldSeconds, lfCutMaxGapDb, lfCutCompWeight;
    // The panel's charge stores (see process()): the most one polarity of the drive can move (light x
    // seconds; 0 = off), the fixed rate at which a store returns to zero (light), the share of the cap over
    // which the emission tapers off, the time a store takes to refill while the drive has the other polarity
    // (seconds; 0 = off), the credit past full it earns meanwhile (share of the cap), and the exponent by
    // which the cap grows with the light law's value above the steady light.
    float elPolCap, elPolLeak, elPolKnee, elPolRefill, elPolCredit, elPolCapExp;
    // The release in the dark (see process()): the most the conductance falls by once the panel has had no
    // drive for a while (per second; 0 = off), the time constant of each of the two stages through which that
    // state builds (seconds), the light law's value under which the panel counts as not driven, and how long
    // it must have stayed under it before the state may build (seconds).
    float darkRateCeiling, darkTau, darkLight, darkHoldSeconds;
    // The LF floor in the dark (see process()): the time over which the share of the cell's conductance that
    // the floor passes returns to all of it once the panel is not driven (seconds; 0 = off).
    float darkFloorTau;
    // Input-side coupling and ownership of the release/floor laws (0 restores the original feed).
    float inputCouplingOn;
    // Log-rate correction below 24 dB, weighted by the existing dark state. The memory fraction is
    // M / (M + darkHistoryDensity * C); no elapsed-note-time state is introduced. Strength 0 disables it.
    float darkHistoryStrength, darkHistoryBase, darkHistoryMemory;
    float darkHistoryShoulder, darkHistoryShoulderMemory, darkHistoryLow, darkHistoryLowMemory;
    float darkHistoryDensity;
    float darkTrapFillPower;
    // The light plateau of a drive faster than 1 kHz (0 = off): see process().
    float elSatBoost, elSatMargin, elSatSpanDb, elSatKneeDb, elSatFadeLoKHz, elSatFadeHiKHz;
    const float* lightTable;
    int lightTableSize;
    float lightTableStartDb, lightTableStepDb, lightSlopeAbove;
    const float* conductanceTable;
    int conductanceTableSize;
    float conductanceTableStartDb, conductanceTableStepDb, conductanceSlopeAbove;
    const float* prGainDb;
    int prGainRows;
    float prStart, prStep, prSlopeBelow, prGate;
};

// Parameters from a kOptoCellDefaults-ordered array (the tables stay the built-in ones). The plugin runs the
// defaults; a lab passes candidates through this.
inline OptoCellParams optoCellParamsFrom(
    const std::array<float, kOptoCellDefaults.size()>& values) noexcept
{
    OptoCellParams p{};
    float* lead = &p.elTauSeconds;
    for (int i = 0; i < 52; ++i) lead[i] = values[static_cast<size_t>(i)];
    p.lightTable = kOptoCellLightTable.data();
    p.lightTableSize = static_cast<int>(kOptoCellLightTable.size());
    p.lightTableStartDb = values[52];
    p.lightTableStepDb = values[53];
    p.lightSlopeAbove = values[54];
    p.conductanceTable = kOptoCellConductanceTable.data();
    p.conductanceTableSize = static_cast<int>(kOptoCellConductanceTable.size());
    p.conductanceTableStartDb = values[55];
    p.conductanceTableStepDb = values[56];
    p.conductanceSlopeAbove = values[57];
    p.prGainDb = kOptoCellPrGainDb.data();
    p.prGainRows = static_cast<int>(kOptoCellPrGainDb.size());
    p.prStart = values[58];
    p.prStep = values[59];
    p.prSlopeBelow = values[60];
    p.prGate = values[61];
    p.lfCutDb = values[62]; p.lfCutFrequency = values[63]; p.lfCutStartDb = values[64];
    p.lfCutSpanDb = values[65]; p.lfCutTauAttack = values[66]; p.lfCutTauSlow = values[67];
    p.lfCutTransSpanDb = values[68]; p.lfCutHoldSeconds = values[69]; p.lfCutMaxGapDb = values[70];
    p.lfCutCompWeight = values[71];
    p.elPolCap = values[72]; p.elPolLeak = values[73]; p.elPolKnee = values[74];
    p.elPolRefill = values[75]; p.elPolCredit = values[76]; p.elPolCapExp = values[77];
    p.darkRateCeiling = values[78]; p.darkTau = values[79]; p.darkLight = values[80]; p.darkFloorTau = values[81];
    p.darkHoldSeconds = values[82];
    p.inputCouplingOn = values[83];
    p.darkHistoryStrength = values[84];
    p.darkHistoryBase = values[85];
    p.darkHistoryMemory = values[86];
    p.darkHistoryShoulder = values[87];
    p.darkHistoryShoulderMemory = values[88];
    p.darkHistoryLow = values[89];
    p.darkHistoryLowMemory = values[90];
    p.darkHistoryDensity = values[91];
    p.darkTrapFillPower = values[92];
    p.elSatBoost = values[93]; p.elSatMargin = values[94]; p.elSatSpanDb = values[95]; p.elSatKneeDb = values[96];
    p.elSatFadeLoKHz = values[97]; p.elSatFadeHiKHz = values[98];
    return p;
}

inline OptoCellParams defaultOptoCellParams() noexcept { return optoCellParamsFrom(kOptoCellDefaults); }

// LF floor law (OptoCell::process): the applied gain is the e-norm of the cell gain g and the floor,
//   applied^e = g^e + (1 - g^e) rho^(e / 2),   rho = limitScale? * (P_band / P_input)^shareExponent,
// P_band the detector's power through a first-order high-pass at highPassHz and low-pass at lowPassHz.
// Order: highPassHz, lowPassHz, shareExponent, compress e, limit e, limit scale.
// Refitted 2026-10-05 on the native unit's settled gain for 186 synthetic rows at Limit .8125, Comp 1 and Comp .5
// (sines 4 Hz - 1 kHz at four levels, two-tone shares, saw / square / two-tone low notes, pink noise; tools
// opto/floor_fit.py, no music in the fit): rms 0.51 -> 0.36 dB, worst 2.0 -> 1.6 dB. Before: 30, 2.016362169, 1,
// 2.146063511, 1.662649637, 1.155625, calibrated on sines from about 55 Hz up. The band's high-pass moved up: the
// old one floored mixes by sub-bass content the native unit does not weigh that way (pink noise 2.0 dB shallow at
// -8 dBFS, now 1.2). Still off, and not reachable inside this law: a 55 Hz saw reads 1.1-1.6 dB deep at -14 and
// -8 dBFS (at equal RMS the native unit lets it through 0.6 dB LOUDER than a 55 Hz sine, which no sum of
// per-component floor power can do). Refitted again the same day jointly with the side-chain flux limit below
// (kOptoFluxLimitDefaults), which takes over part of the low-tone ceiling: before that 37.0023, 2.29543, 1.02267,
// 2.05593, 1.80142, 1.29485.
// Refitted 2026-10-07 on the same 186 rows (same tool, no music in the fit), after the first sub-audio high-pass
// moved ahead of the cell and the input element came on: before that 23.522, 1.7836, 1.0755, 3.9129, 2.6821, 1.0853
// (rows at 0.267 dB rms, worst 0.90, at the start of that pass: the 0.24 / 0.84 quoted further down is the
// 2026-10-05 fit, and the cell's 2026-10-06 refits moved the rows since).
// The pink rows hold a third of their power below 20 Hz; with the high-pass behind the cell that band read 0.8-1.5 dB
// lower than the rest and hid what the audible band was missing (pink at -8 dBFS, Limit .8125: +0.90 dB in all,
// +1.41 dB above 200 Hz). With it ahead the bands agree and the row reads what it is, +1.04 dB; the refit brings it
// to +0.68 (186 rows, this build with the old law against the new: rms 0.282 -> 0.269 dB, worst 1.04 -> 0.82 dB).
inline constexpr std::array<float, 6> kOptoFloorLawDefaults{{
    41.89003522f, 3.176946341f, 1.284316874f, 1.503481107f, 1.634105506f, 1.446758042f
}};

// Side-chain drive limit: the drive to the panel is scaled by
//   (1 + (F / limit)^k)^(-beta / k),   F = held peak (release releaseSeconds) of the drive through a first-order
// high-pass at highPassHz followed by a first-order low-pass at lowPassHz,
// so above `limit` the drive grows only as F^(1 - beta). limit = 0 switches it off.
// Order: limit, highPassHz, lowPassHz, beta, releaseSeconds, k.
// With the fitted corners (low-pass 2 Hz, high-pass 89 Hz) F weighs everything below 89 Hz alike and falls 6 dB per
// octave above it, so the reduction any low tone can reach is held back and a 1 kHz tone is not touched. The element
// started as a volt-second (flux) ceiling, hence the name; the fit moved the corners to this band.
// Why it exists: the native unit's gain on a 55 Hz sine, a 55 Hz sawtooth and pink noise stops falling near -28 dB
// while a 1 kHz sine keeps limiting (-33 dB at -8 dBFS in), and it reduces the sawtooth like the equal-RMS sine.
// The floor law alone reads the sawtooth by its band POWER (1.8 dB under the sine's) and compressed it 1.1-1.6 dB
// too far; this element reads the band's PEAK, which for the two is within 0.4 dB. Fitted 2026-10-05 together with
// kOptoFloorLawDefaults on the same 186 synthetic rows (tools opto/flux_fit.py, no music in the fit): rms
// 0.36 -> 0.24 dB, worst 1.62 -> 0.84 dB.
inline constexpr std::array<float, 6> kOptoFluxLimitDefaults{{
    0.0f, 88.976f, 2.074f, 0.81077f, 0.028321f, 5.2497f}};

class OptoCell
{
public:
    static constexpr int kChannels = 2;
    static constexpr int kPopulations = 4;   // fast, slow, exposure memory, flash

    OptoCell() noexcept { cellParams = defaultOptoCellParams(); }

    // Coefficients at the processing rate; history is kept.
    void setRate(float sr) noexcept
    {
        sampleRate = static_cast<double>(sr);
        dt = 1.0 / sampleRate;
        for (auto& f : couplingInput) f.prepare(sampleRate, kOptoPreHighPassHz, kOptoPreHighPassQ);
        // Production detector weighting (exact coefficients).
        const std::array<BiquadCoeffs, kWeightingSections> weightingCoeffs{{
            Biquad::shelf(sr, 319.1844220f, cellParams.lfShelfDb, 0.5894442553f, false),
            Biquad::peak(sr, 134.4305880f, cellParams.lfPeakDb, 0.5136042617f),
            Biquad::peak(sr, 880.5758706f, cellParams.midPeakDb, 0.6986416568f),
            Biquad::peak(sr, 5840.123777f, cellParams.hfPeakDb, 0.4820592696f),
            Biquad::peak(sr, 9991.669467f, cellParams.hf10kPeakDb, 0.7988600998f)
        }};
        double magnitudeAt1k = 1.0;
        const double w1k = 6.283185307179586 * 1000.0 / sampleRate;
        for (size_t section = 0; section < kWeightingSections; ++section)
        {
            for (auto& channel : loopWeighting) channel[section].setCoeffs(weightingCoeffs[section]);
            for (auto& channel : levelWeighting) channel[section].setCoeffs(weightingCoeffs[section]);
            magnitudeAt1k *= loopWeighting[0][section].magnitude(w1k);
        }
        weightingNormalisation = static_cast<float>(1.0 / magnitudeAt1k);
        {   // Colour-stage level weighting keeps the production W, independent of the loop's lfShelfDb.
            const auto productionShelf = Biquad::shelf(sr, 319.1844220f, 4.73782359f, 0.5894442553f, false);
            for (auto& channel : levelWeighting) channel[0].setCoeffs(productionShelf);
            const std::array<BiquadCoeffs, 4> productionPeaks{{
                Biquad::peak(sr, 134.4305880f, 0.59151688f, 0.5136042617f),
                Biquad::peak(sr, 880.5758706f, -0.4288385533f, 0.6986416568f),
                Biquad::peak(sr, 5840.123777f, 1.410524878f, 0.4820592696f),
                Biquad::peak(sr, 9991.669467f, -1.407323237f, 0.7988600998f)}};
            for (auto& channel : levelWeighting)
                for (size_t section = 1; section < kWeightingSections; ++section)
                    channel[section].setCoeffs(productionPeaks[section - 1]);
            double levelAt1k = 1.0;
            for (size_t section = 0; section < kWeightingSections; ++section)
                levelAt1k *= levelWeighting[0][section].magnitude(w1k);
            levelNormalisation = static_cast<float>(1.0 / levelAt1k);
        }
        {   // Lit-gated LF shelf: section 0 and sections 1..4 each unity at 1 kHz; tracker coefficients.
            const double s0 = loopWeighting[0][0].magnitude(w1k);
            shelfNormalisation = static_cast<float>(1.0 / s0);
            weightingNormalisation14 = static_cast<float>(s0 / magnitudeAt1k);
            litAttack = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.lfLitTauAttack), 1.0e-6) * sampleRate));
            litRelease = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.lfLitTauRelease), 1.0e-6) * sampleRate));
            attackRateStep = 1.0 - std::exp(-1.0 / (std::max(static_cast<double>(cellParams.attackRateTau), 1.0e-6) * sampleRate));
            elFreqStep = 1.0 - std::exp(-1.0 / (std::max(static_cast<double>(cellParams.elFreqTau), 1.0e-6) * sampleRate));
            slewNormalisation = 1.0 / (2.0 * std::sin(3.141592653589793 * 1000.0 / sampleRate));
            reversalRelease = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.elRevRelease), 1.0e-6) * sampleRate));
            reversalHalfCycle1k = sampleRate / 2000.0;
            if (cellParams.elFreqCorner > 0.0f)
            {   // one-pole low-pass on the slew; |H(1 kHz)| normalised away so a 1 kHz sine keeps ratio 1
                slewLowPassStep = 1.0 - std::exp(-6.283185307179586 * static_cast<double>(cellParams.elFreqCorner) / sampleRate);
                const double w = 6.283185307179586 * 1000.0 / sampleRate;
                const double pole = 1.0 - slewLowPassStep;
                const double mag = slewLowPassStep / std::sqrt(1.0 - 2.0 * pole * std::cos(w) + pole * pole);
                slewLowPassNormalisation = 1.0 / mag;
            }
        }
        // Production optoDepthWeighting (exact coefficients).
        for (auto& channel : depthWeighting)
        {
            channel[0].setCoeffs(Biquad::shelf(sr, 2000.0f, 1.25f, 0.70710678f, true));
            channel[1].setCoeffs(Biquad::peak(sr, 20000.0f, 2.0f, 2.5f));
            channel[2].setCoeffs(Biquad::shelf(sr, 2200.0f, 2.4f, 0.70710678f, true));
            channel[3].setCoeffs(Biquad::peak(sr, 20000.0f, 1.0f, 2.5f));
        }
        // LF detector-law term (low shelf; blended by the cell reduction).
        for (auto& filter : lfTermFilter)
            filter.setCoeffs(Biquad::shelf(sr, cellParams.lfTermFrequency > 0.0f ? cellParams.lfTermFrequency : 100.0f,
                                           cellParams.lfTermGainDb, 0.70710678f, false));
        // R14 transient LF cut (low shelf) and its drive trackers: a peak-held envelope (attack lfCutTauAttack,
        // hold lfCutHoldSeconds, then kLfCutEnvelopeRelease) and a slow dB tracker (lfCutTauSlow) whose gap to the
        // envelope marks a step up from a lower level.
        for (auto& filter : lfCutFilter)
            filter.setCoeffs(Biquad::shelf(sr, std::max(cellParams.lfCutFrequency, 20.0f), -cellParams.lfCutDb, 0.70710678f, false));
        cutAttack = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.lfCutTauAttack), 1.0e-6) * sampleRate));
        cutRelease = std::exp(-1.0 / (kLfCutEnvelopeRelease * sampleRate));
        cutSlowStep = 1.0 - std::exp(-1.0 / (std::max(static_cast<double>(cellParams.lfCutTauSlow), 1.0e-6) * sampleRate));
        // Truncated, not rounded (1919 samples at 96 kHz for 20 ms): the validated renders were made this way.
        // The clamp keeps a lab-set NaN or absurd value out of the int cast.
        const float holdSeconds = cellParams.lfCutHoldSeconds > 0.0f ? std::min(cellParams.lfCutHoldSeconds, 1.0f) : 0.0f;
        cutHoldSamples = static_cast<int>(static_cast<double>(holdSeconds) * sampleRate);
        for (auto& channel : channels) channel.driveHold = std::min(channel.driveHold, cutHoldSamples);
        // The panel's charge stores (see process()): per sample, the rate at which a store refills while the
        // drive has the other polarity and the share of what it has spent that is still spent after it.
        chargeRefillRate = cellParams.elPolRefill > 0.0f ? dt / static_cast<double>(cellParams.elPolRefill) : 0.0;
        chargeRefillKeep = std::exp(-chargeRefillRate);
        elementMeterStep = 1.0 - std::exp(-dt / 0.05);   // meterGain(): 50 ms running means
        darkStep = 1.0 - std::exp(-dt / std::max(static_cast<double>(cellParams.darkTau), 1.0e-6));
        darkFloorStep = cellParams.darkFloorTau > 0.0f ? 1.0 - std::exp(-dt / static_cast<double>(cellParams.darkFloorTau)) : 0.0;
        // Truncated like the R14 hold; the clamp keeps a lab-set NaN or absurd value out of the int cast.
        const float darkHold = cellParams.darkHoldSeconds > 0.0f ? std::min(cellParams.darkHoldSeconds, 1.0f) : 0.0f;
        darkHoldSamples = static_cast<int>(static_cast<double>(darkHold) * sampleRate);
        for (auto& channel : channels)
        {
            channel.darkHold = std::min(channel.darkHold, darkHoldSamples);
            channel.floorInputHold = std::min(channel.floorInputHold, darkHoldSamples);
        }
        // Production LF floor energy path (exact coefficients).
        updateFloorBand();
        floorBandStep = 1.0f - std::exp(-6.283185307f * 1000.0f / sr);
        for (auto& filter : limitFloorFilter)
            filter.setCoeffs(Biquad::lowPass(sr, 300.0f, 0.70710678f));
        floorPowerStep = 1.0f - std::exp(-1.0f / (0.050f * sr));
        levelPeakAttack = std::exp(-1.0 / (0.000050 * sampleRate));
        levelPeakRelease = std::exp(-1.0 / (0.040 * sampleRate));
        elStep = 1.0 - std::exp(-1.0 / (static_cast<double>(cellParams.elTauSeconds) * sampleRate));
    }

    // Stereo link amount for the panel-drive OR in process(); 0 = each channel on its own.
    void setLinkWeight(float weight) noexcept { linkWeight = weight; }
    // Lab-only: the plugin runs kOptoFloorLawDefaults.
    void setFloorLaw(const std::array<float, 6>& law) noexcept
    {
        floorLaw = law;
        updateFloorBand();
    }

    void updateFloorBand() noexcept
    {
        const float sr = static_cast<float>(sampleRate);
        floorHighPassStep = 1.0f - std::exp(-6.283185307f * floorLaw[0] / sr);
        floorLowPassStep = 1.0f - std::exp(-6.283185307f * floorLaw[1] / sr);
        fluxHighPassStep = 1.0 - std::exp(-6.283185307179586 * static_cast<double>(fluxLaw[1]) / sampleRate);
        fluxLowPassStep = 1.0 - std::exp(-6.283185307179586 * static_cast<double>(fluxLaw[2]) / sampleRate);
        fluxRelease = std::exp(-1.0 / (std::max(static_cast<double>(fluxLaw[4]), 1.0e-4) * sampleRate));
    }
    // Lab-only: the plugin runs kOptoFluxLimitDefaults.
    void setFluxLaw(const std::array<float, 6>& law) noexcept
    {
        fluxLaw = law;
        updateFloorBand();
    }

    void reset() noexcept
    {
        for (auto& c : channels) c = ChannelState{};
        for (auto& channel : loopWeighting) for (auto& f : channel) f.reset();
        for (auto& channel : levelWeighting) for (auto& f : channel) f.reset();
        for (auto& channel : depthWeighting) for (auto& f : channel) f.reset();
        for (auto& f : lfTermFilter) f.reset();
        for (auto& f : limitFloorFilter) f.reset();
        for (auto& f : lfCutFilter) f.reset();
        for (auto& f : couplingInput) f.reset();
    }

    // The only sample counters are the R14 hold countdown and the dark state's; setRate() clamps each to its
    // new hold length, which is all a rate change needs (a hold in progress ends no later than a fresh one would).
    template <typename ScaleCounter>
    void scaleCounters(const ScaleCounter&) noexcept {}

    // withApplied = false (lab kernel only) skips the applied-gain floor blend, which feeds no state; every
    // state update is identical, and the returned value is then the cell gain before the floor.
    template <bool withApplied = true>
    float process(int ch, float input, float /*compressed*/, float sidechain,
                  bool external, float optoDetector, bool useOptoDetector,
                  bool limit, float peakReduction) noexcept
    {
        const size_t c = static_cast<size_t>(ch);
        auto& d = channels[c];
        const float base = useOptoDetector ? optoDetector : (external ? sidechain : input);

        // Production LF floor energies from the pre-gain detector; both mode paths advance.
        d.floorHighPassLow += floorHighPassStep * (base - d.floorHighPassLow);
        d.floorLow += floorLowPassStep * (base - d.floorHighPassLow - d.floorLow);
        d.floorBandLow += floorBandStep * (d.floorLow - d.floorBandLow);
        const float limitFloorSignal = limitFloorFilter[c].process(d.floorLow);
        const float floorSignal = limit ? limitFloorSignal : d.floorBandLow;
        d.floorPower += floorPowerStep * (floorSignal * floorSignal - d.floorPower);
        d.inputPower += floorPowerStep * (base * base - d.inputPower);

        // Colour-stage level: pre-gain weighted detector through a 50 us / 40 ms peak follower.
        float levelSignal = base;
        for (auto& f : levelWeighting[c]) levelSignal = f.process(levelSignal);
        const double levelAbs = std::abs(static_cast<double>(levelSignal) * levelNormalisation);
        const double levelCoeff = levelAbs > d.levelPeak ? levelPeakAttack : levelPeakRelease;
        d.levelPeak = levelAbs + (d.levelPeak - levelAbs) * levelCoeff;

        // Feedback side chain on the cell gain (before the floor).
        const double gPrev = d.cellGain;
        const double mix = limit
            ? static_cast<double>(cellParams.limitOutputMix) * gPrev + cellParams.limitInputMix
            : gPrev + cellParams.compressInputMix;
        float loop = static_cast<float>(mix * base);
        const float coupled = couplingInput[c].process(base);
        if (cellParams.inputCouplingOn != 0.0f)
            loop = static_cast<float>(mix * coupled);
        if (cellParams.lfLitSpanDb > 0.0f)
        {
            // Lit-gated LF shelf: the LF sensitivity needs an illuminated photocell (tracked EL level at n-1).
            for (size_t section = 1; section < kWeightingSections; ++section) loop = loopWeighting[c][section].process(loop);
            loop *= weightingNormalisation14;
            const float shelved = loopWeighting[c][0].process(loop) * shelfNormalisation;
            const double litDb = cellParams.lfLitLog != 0.0f ? d.litLevel : 20.0 * std::log10(std::max(d.litLevel, 1.0e-12));
            const float engage = std::clamp(static_cast<float>((litDb - (cellParams.lightTableStartDb + cellParams.lfLitLevelDb))
                                                               / cellParams.lfLitSpanDb), 0.0f, 1.0f);
            const float w = cellParams.lfLitFloor + (1.0f - cellParams.lfLitFloor) * engage;
            loop += w * (shelved - loop);
        }
        else
        {
            for (auto& f : loopWeighting[c]) loop = f.process(loop);
            loop *= weightingNormalisation;
        }
        if (cellParams.lfCutDb != 0.0f)
        {   // R14: LF loss in the side-chain drive at the onset of a loud passage (measured on the native unit:
            // from dark, 60 Hz lags 1 kHz by 0 dB at -30 dBFS and by 11.5 dB at -4 dBFS, recovering within ~100 ms;
            // once lit the LF response is the static one). Keyed on the drive envelope at n-1: an absolute drive
            // gate times a transient gate (envelope above the slow tracker). The envelope holds each peak for
            // lfCutHoldSeconds, which removes the per-cycle ripple that would otherwise keep the gate open on a
            // settled low tone. It is exactly flat only open loop: in the feedback cell the drive peaks drift down
            // by ~0.001 dB/s as the slow populations settle, the hold lapses a few times a second and the gate
            // opens by at most a few percent. Measured with the 2026-10-03 constants (not re-measured after the
            // 2026-10-05 refit), cut on against cut off, settled tone at PR 70: 60 Hz
            // 0.002-0.005 dB, 30 Hz 0.01-0.03 dB, 25 Hz 0.02-0.04 dB, 15 Hz 0.04-0.10 dB (without the hold:
            // 0.06-0.20 dB at 30-100 Hz).
            const double driveDb = d.driveDb;
            const float wAbs = std::clamp(static_cast<float>((driveDb - static_cast<double>(cellParams.lfCutStartDb))
                                                            / std::max(static_cast<double>(cellParams.lfCutSpanDb), 1.0e-6)), 0.0f, 1.0f);
            const float wTr = std::clamp(static_cast<float>((driveDb - d.driveSlow)
                                                           / std::max(static_cast<double>(cellParams.lfCutTransSpanDb), 1.0e-6)), 0.0f, 1.0f);
            const float wc = wAbs * wTr * (limit ? 1.0f : cellParams.lfCutCompWeight);
            loop += wc * (lfCutFilter[c].process(loop) - loop);
        }
        // Production optoDepthWeighting, fed by the cell reduction at n-1; both mode paths advance.
        const float priorReduction = static_cast<float>(d.grDb);
        const float compressShelfWeight = std::clamp((priorReduction - 8.0f) / 20.0f, 0.0f, 1.0f);
        const float limitShelfWeight = std::clamp((priorReduction - 12.0f) / 28.0f, 0.0f, 1.0f);
        const float depthPeakWeight = std::clamp((priorReduction - 8.0f) / 8.0f, 0.0f, 1.0f);
        auto& depth = depthWeighting[c];
        float compressDetector = loop + compressShelfWeight * (depth[0].process(loop) - loop);
        float limitDetector = loop + limitShelfWeight * (depth[2].process(loop) - loop);
        compressDetector += depthPeakWeight * (depth[1].process(compressDetector) - compressDetector);
        limitDetector += depthPeakWeight * (depth[3].process(limitDetector) - limitDetector);
        if (cellParams.depthWeighting != 0.0f)
            loop = limit ? limitDetector : compressDetector;
        // LF detector-law term, fed by the same reduction at n-1.
        if (cellParams.lfTermGainDb != 0.0f)
        {
            const float lfWeight = std::clamp(priorReduction / cellParams.lfTermGrSpan, 0.0f, 1.0f);
            loop += lfWeight * (lfTermFilter[c].process(loop) - loop);
        }

        const double prNorm = std::clamp(static_cast<double>(peakReduction) * 0.01, 0.0, 1.0);
        const bool inert = prNorm <= static_cast<double>(cellParams.prGate);
        if (prNorm != cachedPrNorm)
        {
            cachedPrNorm = prNorm;
            cachedSideChainGain = sideChainGain(prNorm);
        }
        // The input owns the floor and release laws; a coupling tail continues to supply light only.
        const double floorOwn = inert ? 0.0 : cachedSideChainGain * mix * levelAbs;
        double floorDrive = floorOwn;
        if (linkWeight > 0.0f)
            floorDrive = std::max(d.floorOwn, static_cast<double>(linkWeight)
                * (c == 0 ? channels[1].floorOwn : channels[0].floorOwnPrev));
        d.floorOwnPrev = d.floorOwn;
        d.floorOwn = floorOwn;
        d.floorEl += elStep * (floorDrive - d.floorEl);
        double floorLight = inert ? 0.0 : lightFor(d.floorEl);
        if (cellParams.lightCeiling > 0.0f)
            floorLight /= 1.0 + floorLight / static_cast<double>(cellParams.lightCeiling);
        const double floorThreshold = std::max(static_cast<double>(cellParams.darkLight), 1.0e-30);
        if (floorLight >= floorThreshold) d.floorInputHold = darkHoldSamples;
        else if (d.floorInputHold > 0) --d.floorInputHold;
        const double floorTarget = d.floorInputHold > 0 ? 0.0 : floorThreshold / (std::max(floorLight, 0.0) + floorThreshold);
        d.floorDark[0] = floorTarget < d.floorDark[0] ? floorTarget : d.floorDark[0] + darkStep * (floorTarget - d.floorDark[0]);
        d.floorDark[1] = d.floorDark[0] < d.floorDark[1] ? d.floorDark[0] : d.floorDark[1] + darkStep * (d.floorDark[0] - d.floorDark[1]);
        double s = inert ? 0.0 : cachedSideChainGain * static_cast<double>(loop);
        if (fluxLaw[0] > 0.0f)
        {   // side-chain flux limit (see kOptoFluxLimitDefaults); the scale comes from the flux up to n-1
            // (two pow per oversampled sample; the cell already spends several on the light and floor laws)
            const double ratio = d.fluxPeak / static_cast<double>(fluxLaw[0]);
            d.fluxHighPassLow += fluxHighPassStep * (s - d.fluxHighPassLow);
            d.fluxLow += fluxLowPassStep * (s - d.fluxHighPassLow - d.fluxLow);
            d.fluxPeak = std::max(std::abs(d.fluxLow), d.fluxPeak * fluxRelease);
            if (std::abs(d.fluxHighPassLow) < 1.0e-30) d.fluxHighPassLow = 0.0;   // no denormal tails in silence
            if (std::abs(d.fluxLow) < 1.0e-30) d.fluxLow = 0.0;
            if (d.fluxPeak < 1.0e-30) d.fluxPeak = 0.0;
            const double k = std::max(static_cast<double>(fluxLaw[5]), 1.0e-3);
            s *= std::pow(1.0 + std::pow(ratio, k), -static_cast<double>(fluxLaw[3]) / k);
        }
        if (cellParams.lfCutDb != 0.0f)
        {
            const double a = std::abs(s);
            if (a >= d.driveEnv) { d.driveEnv = a + (d.driveEnv - a) * cutAttack; d.driveHold = cutHoldSamples; }
            else if (d.driveHold > 0) --d.driveHold;
            else
            {
                d.driveEnv = a + (d.driveEnv - a) * cutRelease;
                if (d.driveEnv < 1.0e-30) d.driveEnv = 0.0;   // no denormal tail after release
            }
            d.driveDb = 20.0 * std::log10(std::max(d.driveEnv, kLfCutEnvelopeFloor));
            d.driveSlow += cutSlowStep * (d.driveDb - d.driveSlow);
            // The slow tracker trails the envelope by at most lfCutMaxGapDb, so the length of the cut is the same
            // after any floor more than that far below the onset envelope (digital silence against a -90 dB
            // floor: 0.005 dB at 100 ms). A floor closer than the cap shortens it (-60 dB: 0.12 dB, 9 ms).
            if (cellParams.lfCutMaxGapDb > 0.0f)
                d.driveSlow = std::max(d.driveSlow, d.driveDb - static_cast<double>(cellParams.lfCutMaxGapDb));
        }
        // A linked pair is a diode-OR of the two side chains at the panel: each cell lights from the larger of its
        // own rectified drive and its partner's, scaled by the link amount. Every filter and the polarity-reversal
        // frequency law stay on the channel's own drive. Feeding one cell a sample-wise pick of the larger channel
        // ahead of those instead adds reversals between the channels that neither drive has (quadrature sines read
        // 0.6-1.1 dB deep against the native unit; 0.3-0.7 dB with this OR).
        // The native unit's own law is not an instantaneous OR either (measured 2026-10-04: against an OR-driven
        // dual-mono signal it reduces quadrature sines about 1 dB less and independent noise up to 1.2 dB more), so
        // independent channels at deep Limit still read about 0.9 dB shallow here.
        // Both drives are taken one sample back, so the two cells of a dual-mono pair light exactly alike: the
        // caller runs channel 0 then channel 1 for each sample, and channel 0 has already stored this sample's.
        const double ownDrive = std::abs(s);
        double panelDrive = ownDrive;
        if (linkWeight > 0.0f)
            panelDrive = std::max(d.panelOwn, static_cast<double>(linkWeight)
                                                  * (c == 0 ? channels[1].panelOwn : channels[0].panelOwnPrev));
        d.panelOwnPrev = d.panelOwn;
        d.panelOwn = ownDrive;
        d.el += elStep * (panelDrive - d.el);
        if (cellParams.lfLitLog != 0.0f)
        {
            const double elDb = std::max(20.0 * std::log10(std::max(d.el, 1.0e-12)),
                                         static_cast<double>(cellParams.lightTableStartDb) - 60.0);
            d.litLevel = elDb + (d.litLevel - elDb) * (elDb > d.litLevel ? litAttack : litRelease);
        }
        else
            d.litLevel = d.el + (d.litLevel - d.el) * (d.el > d.litLevel ? litAttack : litRelease);
        double light = inert ? 0.0 : lightFor(d.el);
        if (cellParams.lightCeiling > 0.0f)
            light = light / (1.0 + light / static_cast<double>(cellParams.lightCeiling));
        const double lawLight = light;   // what the drive asks of the panel, ahead of the frequency law and the charge stores
        double estimatedFrequencyHz = 1000.0;
        if (cellParams.elFreqExponent != 0.0f && cellParams.elRevHysteresis > 0.0f)
        {
            // EL light is emitted at polarity reversals: the last completed half-cycle between significant
            // reversals (Schmitt trigger at +-h x peak |s|) sets (f / 1 kHz)^kappa, held to the next reversal.
            const double magnitude = std::abs(s);
            d.reversalPeak = magnitude > d.reversalPeak ? magnitude : d.reversalPeak * reversalRelease;
            const double threshold = static_cast<double>(cellParams.elRevHysteresis) * d.reversalPeak;
            ++d.reversalCount;
            if ((d.reversalSign > 0 && s < -threshold) || (d.reversalSign < 0 && s > threshold))
            {
                d.reversalSign = -d.reversalSign;
                d.reversalPhi = std::pow(reversalHalfCycle1k / static_cast<double>(d.reversalCount),
                                         static_cast<double>(cellParams.elFreqExponent));
                d.reversalCount = 0;
            }
            light *= d.reversalPhi;
        }
        else if (cellParams.elFreqExponent != 0.0f)
        {
            // EL brightness frequency law (AC electroluminescence, light ~ f^kappa at a given drive amplitude):
            // the drive's slew-to-level mean-square ratio estimates (f / 1 kHz)^2, exactly 1 for a settled 1 kHz sine.
            double slew = (s - d.drivePrev) * slewNormalisation;
            const double rawSlew = slew;   // ahead of the corner: (f / 1 kHz)^2 against the level for a settled sine
            if (cellParams.elFreqCorner > 0.0f)
            {   // EL brightness flattens above the corner: low-pass the slew before its mean square
                d.slewLow += slewLowPassStep * (slew - d.slewLow);
                slew = d.slewLow * slewLowPassNormalisation;
            }
            d.drivePrev = s;
            d.drivePower += elFreqStep * (s * s - d.drivePower);
            d.slewPower += elFreqStep * (slew * slew - d.slewPower);
            const double ratio = std::clamp(d.slewPower / std::max(d.drivePower, 1.0e-30), 1.0e-4, 1.0e4);
            estimatedFrequencyHz = 1000.0 * std::sqrt(ratio);
            light *= std::pow(ratio, 0.5 * static_cast<double>(cellParams.elFreqExponent));
            if (cellParams.elSatBoost > 0.0f)
            {   // The plateau of the light table is higher for a drive faster than 1 kHz. Its speed is the
                // ratio of two 5 ms power averages (a loud note outweighs what preceded it at once),
                // limited to what a settled tone can reach, 1 + (corner / 1 kHz)^2. The element is confined
                // to the band it was measured in: it fades out between elSatFadeLoKHz and elSatFadeHiKHz,
                // read from the unfiltered slew-to-level ratio, (f / 1 kHz)^2.
                const double satStep = std::min(dt / 0.005, 1.0);
                d.satSlew += satStep * (slew * slew - d.satSlew);
                d.satRaw += satStep * (rawSlew * rawSlew - d.satRaw);
                d.satDrive += satStep * (s * s - d.satDrive);
                if (d.satSlew < 1.0e-30) d.satSlew = 0.0;     // no denormal tail in silence
                if (d.satRaw < 1.0e-30) d.satRaw = 0.0;
                if (d.satDrive < 1.0e-30) d.satDrive = 0.0;
                const double level = std::max(d.satDrive, 1.0e-30);
                const double cornerKHz = cellParams.elFreqCorner > 0.0f ? 1.0e-3 * static_cast<double>(cellParams.elFreqCorner) : 1.0e3;
                const double fast = std::min(d.satSlew / level, 1.0 + cornerKHz * cornerKHz)
                    - 1.0 - static_cast<double>(cellParams.elSatMargin);
                if (fast > 0.0 && d.el > 1.0e-30)
                {
                    const double above = (8.685889638065035 * std::log(d.el) - static_cast<double>(cellParams.elSatKneeDb))
                        / std::max(static_cast<double>(cellParams.elSatSpanDb), 1.0e-6);
                    const double lo = static_cast<double>(cellParams.elSatFadeLoKHz) * cellParams.elSatFadeLoKHz;
                    const double hi = static_cast<double>(cellParams.elSatFadeHiKHz) * cellParams.elSatFadeHiKHz;
                    const double fade = hi > lo ? std::clamp((hi - d.satRaw / level) / (hi - lo), 0.0, 1.0) : 1.0;
                    if (above > 0.0 && fade > 0.0)
                    {
                        const double w = above >= 1.0 ? 1.0 : above * above * (3.0 - 2.0 * above);
                        light *= 1.0 + static_cast<double>(cellParams.elSatBoost) * fast * w * fade;
                    }
                }
            }
        }
        if (cellParams.elPolCap > 0.0f && cellParams.elPolRefill > 0.0f)
        {
            // The panel's charge. An electroluminescent panel emits while charge crosses the phosphor, and one
            // polarity of the drive can move only so much of it before the field of the moved charge stops the
            // emission: elPolCap, in light x seconds. Measured on the native unit (tools opto/onset_modes.py,
            // opto/lf_cycle.py): from dark, a 40 or an 80 Hz tone builds its reduction in steps, one per
            // polarity change of the drive and the same at both frequencies, each within about 2 ms of the
            // change, with no further fall until the next; the step hardly depends on the input level (17 %
            // over 12 dB) and not on Limit / Compress; settled sines of 4 to 110 Hz at Limit read
            // 1 / gain - 1 = G0 + c f.
            // Two stores, one per polarity. chargeSpent[k] is what polarity k has moved. While the drive has
            // polarity k the light law's value passes unchanged as long as more than elPolKnee x cap is still
            // ahead of that store and tapers to nothing over that last share, so a drive that never spends its
            // charge (a 1 kHz tone, any low level) is not touched. Meanwhile the OTHER store gets back what it
            // has spent (time constant elPolRefill) and, in proportion to how lit the panel is
            // (light / (light + elPolLeak)), runs on to a credit of elPolCredit x cap: the half-cycle after a
            // lit one of the other polarity can move up to (1 + credit) x cap, the first one from dark only
            // cap. Both stores return to zero at the fixed rate elPolLeak (light): that is the steady light of
            // a polarity that is held (G0 above). The cap itself grows a little with the drive, by
            // (light / elPolLeak)^elPolCapExp above that steady light (the measured step grows 17 % over 12 dB).
            // Nothing here detects or counts anything: the stores are continuous states, and the drive's sign
            // picks one of them the way a rectifier picks a polarity. A linked pair lights from the larger
            // drive (above) and spends the store of its own drive's polarity.
            const size_t side = s >= 0.0 ? 0 : 1;
            const double leak = static_cast<double>(cellParams.elPolLeak);
            const double law = std::max(light, 0.0);
            double cap = static_cast<double>(cellParams.elPolCap);
            if (cellParams.elPolCapExp != 0.0f && leak > 0.0 && law > leak)
                cap *= std::pow(law / leak, static_cast<double>(cellParams.elPolCapExp));
            const double lit = leak > 0.0 ? law / (law + leak) : (law > 0.0 ? 1.0 : 0.0);
            const double fullCredit = -static_cast<double>(cellParams.elPolCredit) * cap;
            double other = d.chargeSpent[1 - side];
            if (other > 0.0) other *= chargeRefillKeep;
            if (other > fullCredit) other = fullCredit + (other - fullCredit) * std::exp(-chargeRefillRate * lit);
            d.chargeSpent[1 - side] = other;
            if (leak > 0.0)
            {
                const double drained = leak * dt;
                for (auto& spent : d.chargeSpent)
                    spent = spent > 0.0 ? std::max(spent - drained, 0.0) : std::min(spent + drained, 0.0);
            }
            const double ahead = cap - d.chargeSpent[side];
            if (ahead > 0.0)
            {
                // The taper is integrated exactly over the sample for a constant light.
                const double taper = static_cast<double>(cellParams.elPolKnee) * cap;
                const double wanted = law * dt;
                double moved;
                if (!(taper > 0.0) || ahead - wanted >= taper)
                    moved = std::min(wanted, ahead);
                else if (ahead > taper)
                    moved = (ahead - taper) + taper * (1.0 - std::exp(-(wanted - (ahead - taper)) / taper));
                else
                    moved = ahead * (1.0 - std::exp(-wanted / taper));
                light = moved / dt;
                d.chargeSpent[side] += moved;
            }
            else
                light = 0.0;
            for (auto& spent : d.chargeSpent)
                if (std::abs(spent) < 1.0e-30) spent = 0.0;   // no denormal tail
        }
        d.estimatedFrequencyHz = estimatedFrequencyHz;
        double step = dt;
        if (cellParams.timeScaleLight > 0.0f && cellParams.hFloor > 0.0f)
        {
            const double r = std::pow(std::max(light, 0.0) / static_cast<double>(cellParams.timeScaleLight),
                                      static_cast<double>(cellParams.timeScaleExponent));
            step = dt * (static_cast<double>(cellParams.hFloor) + r) / (1.0 + r);
        }
        else if (cellParams.timeScaleLight > 0.0f && light > 0.0)
            step = dt / (1.0 + std::pow(light / static_cast<double>(cellParams.timeScaleLight),
                                        static_cast<double>(cellParams.timeScaleExponent)));
        // Attack-rate limit (measured native attack saturation, ratelaw map): charge slows when the
        // ripple-averaged GR rise rate (from previous samples) approaches Smax; ~0 at steady state.
        const double rateScale = (cellParams.attackRateDbPerS > 0.0f && d.grRateSmooth > 0.0)
            ? 1.0 / (1.0 + std::pow(d.grRateSmooth / static_cast<double>(cellParams.attackRateDbPerS),
                                    static_cast<double>(cellParams.attackRatePower)))
            : 1.0;
        const double releaseBase = cellParams.hChargeOnly != 0.0f ? dt : step;
        double darkScale = 1.0;
        if (cellParams.darkRateCeiling > 0.0f || cellParams.darkFloorTau > 0.0f)
        {
            // The release in the dark. Measured on the native unit (tools opto/silence_probe.py and
            // opto/silence_holdout.py, a pilot through the silence after a note): into silence its reduction
            // never leaves faster than about 0.245 dB/ms, from 27 dB and from 42 dB alike, after 60 ms of note
            // or 2 s, at 70 Hz, 1 kHz or 3 kHz (the conductance falls by 28 /s at most). This cell's
            // density-dependent release terms have no such limit: 41 /s from 33 dB, over 60 /s from 42 dB,
            // 2 dB and 6 dB too little reduction 40 ms after the note. With a signal still driving the panel
            // the same terms read the native unit's attack and its release (every lit table), so they stay,
            // and the limit comes on once the drive has been absent for darkHoldSeconds and about two darkTau
            // more: a countdown restarts at every sample whose light law is at or above darkLight, and only
            // when it has run out does what the law lacks of that, darkLight / (law + darkLight), go through
            // two one-pole stages on the way up; it falls at once. The 0.5 ms hold retains the settled-tone
            // bar while reducing the excess loss after the cut. With input coupling enabled, the same
            // input-owned state used by the floor drives this limit, so the coupling tail cannot disable it.
            // In that state the populations and the trap run on one slowed clock: with r the rate at which
            // the conductance would fall by the laws below, the clock is (1 + (r / ceiling)^4)^(-1/4), so it
            // falls by r where r is well under the ceiling and by the ceiling where r is over it, and the
            // shares of the populations move as they would have, only later.
            // The limit belongs to a cell whose traps are full. After a short burst the native unit lets go
            // much faster (opto/silence_short.py, 1 kHz at -8 dBFS into silence: 0.80 dB/ms after 2 ms,
            // 0.51 after 5 ms, 0.43 after 10 ms, 0.33 after 20 ms, 0.24 from 60 ms on), which is what this
            // cell's capture into empty traps already gives; so the slowing is weighted by the traps' fill
            // raised to darkTrapFillPower, the exposure state the cell has. The exponent was calibrated on
            // separate 6/12/18/24/45/75 ms bursts; 2 restores the patch-40 ceiling's exposure law.
            const double law = std::max(lawLight, 0.0);
            const double darkLight = std::max(static_cast<double>(cellParams.darkLight), 1.0e-30);
            if (law >= darkLight) d.darkHold = darkHoldSamples;
            else if (d.darkHold > 0) --d.darkHold;
            const double target = d.darkHold > 0 ? 0.0 : darkLight / (law + darkLight);
            d.dark[0] = target < d.dark[0] ? target : d.dark[0] + darkStep * (target - d.dark[0]);
            d.dark[1] = d.dark[0] < d.dark[1] ? d.dark[0] : d.dark[1] + darkStep * (d.dark[0] - d.dark[1]);
            const double releaseDark = cellParams.inputCouplingOn != 0.0f ? d.floorDark[1] : d.dark[1];
            if (cellParams.darkRateCeiling > 0.0f && releaseDark > 0.0)
            {
                const auto& g = d.population;
                double loss = g[0] * (cellParams.fastRelease + cellParams.fastBimolecular * g[0])
                    + g[1] * (cellParams.slowRelease + cellParams.slowBimolecular * g[1]);
                if (cellParams.memoryCharge > 0.0f) loss += g[2] * cellParams.memoryRelease;
                if (cellParams.flashCharge > 0.0f)
                    loss += g[3] * (cellParams.flashRelease + cellParams.flashBimolecular * g[3]);
                loss *= releaseBase / dt;
                if (cellParams.trapCapture > 0.0f)
                {
                    const double fill = cellParams.trapCapacity > 0.0f
                        ? std::max(0.0, 1.0 - d.trap / static_cast<double>(cellParams.trapCapacity)) : 1.0;
                    loss += static_cast<double>(cellParams.trapCapture) * fill * g[1]
                        - static_cast<double>(cellParams.trapEmit) * d.trap;
                }
                const double conductanceNow = g[0] + g[1] + g[2] + g[3];
                if (loss > 0.0 && conductanceNow > 1.0e-30)
                {
                    const double ratio = loss / (conductanceNow * static_cast<double>(cellParams.darkRateCeiling));
                    const double square = ratio * ratio;
                    const double filled = cellParams.trapCapture > 0.0f && cellParams.trapCapacity > 0.0f
                        ? std::clamp(d.trap / static_cast<double>(cellParams.trapCapacity), 0.0, 1.0) : 1.0;
                    const double ownedFill = cellParams.darkTrapFillPower == 2.0f ? releaseDark * filled * filled
                        : releaseDark * std::pow(filled, static_cast<double>(cellParams.darkTrapFillPower));
                    darkScale = 1.0 - ownedFill
                        * (1.0 - 1.0 / std::sqrt(std::sqrt(1.0 + square * square)));
                }
            }
        }
        const double historyWeight = cellParams.inputCouplingOn != 0.0f ? d.floorDark[1] : d.dark[1];
        if (cellParams.darkHistoryStrength > 0.0f && historyWeight > 0.0
            && (cellParams.darkRateCeiling > 0.0f || cellParams.darkFloorTau > 0.0f))
        {
            const double density = d.population[0] + d.population[1] + d.population[2] + d.population[3];
            if (density > 1.0e-30)
            {
                const double reduction = 8.685889638065035 * std::log1p(density);
                if (reduction < 24.0)
                {
                    const auto tent = [reduction](double left, double peak, double right) {
                        return std::max(0.0, std::min((reduction - left) / (peak - left),
                                                     (right - reduction) / (right - peak)));
                    };
                    const std::array<double, 7> p{{cellParams.darkHistoryBase, cellParams.darkHistoryMemory,
                        cellParams.darkHistoryShoulder, cellParams.darkHistoryShoulderMemory,
                        cellParams.darkHistoryLow, cellParams.darkHistoryLowMemory, cellParams.darkHistoryDensity}};
                    const double history = d.population[2] / (d.population[2] + std::max(p[6], 1.0e-30) * density);
                    const double correction = (p[0] + p[1] * history) * tent(2.5, 6.5, 13.0)
                        + (p[2] + p[3] * history) * tent(9.0, 16.0, 24.0)
                        - ((1.0 - history) * p[4] + history * p[5]) * tent(0.0, 1.5, 2.5);
                    darkScale *= std::exp(historyWeight * static_cast<double>(cellParams.darkHistoryStrength) * correction);
                }
            }
        }
        const double chargeStep = step * rateScale * darkScale;
        const double releaseStep = releaseBase * darkScale;
        const double trapStep = dt * darkScale;
        d.population[0] = (d.population[0] + chargeStep * light)
            / (1.0 + releaseStep * (cellParams.fastRelease + cellParams.fastBimolecular * d.population[0]));
        d.population[1] = (d.population[1] + chargeStep * cellParams.slowCharge * light)
            / (1.0 + releaseStep * (cellParams.slowRelease + cellParams.slowBimolecular * d.population[1]));
        if (cellParams.memoryCharge > 0.0f)
            d.population[2] = (d.population[2] + chargeStep * cellParams.memoryCharge * light)
                / (1.0 + releaseStep * cellParams.memoryRelease);
        if (cellParams.flashCharge > 0.0f)
            d.population[3] = (d.population[3] + chargeStep * cellParams.flashCharge * light)
                / (1.0 + releaseStep * (cellParams.flashRelease + cellParams.flashBimolecular * d.population[3]));
        if (cellParams.trapCapture > 0.0f)
        {
            // CdS trap filling: population-1 carriers are captured into traps and re-emitted. Capture equals
            // emission at equilibrium (statics unchanged); filled traps sustain the conductance after exposure.
            const double fill = cellParams.trapCapacity > 0.0f
                ? std::max(0.0, 1.0 - d.trap / static_cast<double>(cellParams.trapCapacity)) : 1.0;
            const double capture = static_cast<double>(cellParams.trapCapture) * fill * d.population[1] * trapStep;
            const double emit = static_cast<double>(cellParams.trapEmit) * d.trap * trapStep;
            d.population[1] = std::max(0.0, d.population[1] - capture + emit);
            d.trap = std::max(0.0, d.trap + capture - emit);
        }
        double conductance = 0.0;
        for (auto& pop : d.population)
        {
            if (pop < 1.0e-30) pop = 0.0;          // no denormal tails after release
            conductance += pop;
        }
        if (cellParams.conductanceTableOn != 0.0f) conductance = conductanceFor(conductance);   // split static Psi
        d.cellGain = 1.0 / (1.0 + conductance);
        {
            const double newGr = 8.685889638065035 * std::log1p(conductance);
            if (cellParams.attackRateDbPerS > 0.0f)
                d.grRateSmooth += attackRateStep * ((newGr - d.grDb) / dt - d.grRateSmooth);
            d.grDb = newGr;
        }

        // Production LF floor gain blend on the applied gain.
        float applied = static_cast<float>(d.cellGain);
        if (!withApplied) return applied;
        if (cellParams.lowFrequencyFloor != 0.0f)
        {
            float floorShare = d.floorPower / std::max(d.inputPower, 1.0e-12f);
            if (floorLaw[2] != 1.0f) floorShare = std::pow(std::max(floorShare, 0.0f), floorLaw[2]);
            const float floorPowerRatio = std::clamp(floorShare * (limit ? floorLaw[5] : 1.0f), 0.0f, 1.0f);
            const float floorExponent = limit ? floorLaw[4] : floorLaw[3];
            const float gainPower = std::pow(applied, floorExponent);
            applied = std::pow(gainPower + (1.0f - gainPower)
                * std::pow(floorPowerRatio, floorExponent * 0.5f), 1.0f / floorExponent);
            if (!std::isfinite(applied)) applied = 1.0f;
            if (cellParams.darkFloorTau > 0.0f)
            {
                // The floor in the dark. The floor law reads the low band's share of the input, and with no
                // input there is no share to read: both running powers fall together and the last share stays,
                // so the blend above gives its lift back as the cell lets go, and the applied reduction leaves
                // more slowly than the cell's. The native unit's release after a 50 or 100 Hz note is the one
                // it has after a 1 kHz note from the same reduction (tools opto/silence_probe.py: 0.224 against
                // 0.225 dB/ms from 27 dB), as if the low note had left the cell with that much less
                // conductance. So while the panel is not driven (the state the release limit above comes on
                // with) the share of the cell's conductance that the floor passes is kept, and
                // returns to all of it over darkFloorTau; while it is driven it is the blend's own, sample by
                // sample.
                const double cellConductance = 1.0 / std::max(d.cellGain, 1.0e-12) - 1.0;
                const double passed = cellConductance > 1.0e-12
                    ? std::clamp((1.0 / static_cast<double>(applied) - 1.0) / cellConductance, 0.0, 1.0) : 1.0;
                const double dark = cellParams.inputCouplingOn != 0.0f ? d.floorDark[1] : d.dark[1];
                if (dark > 0.0)
                {
                    d.floorPassed += (1.0 - dark) * (passed - d.floorPassed) + dark * darkFloorStep * (1.0 - d.floorPassed);
                    applied = static_cast<float>(1.0 / (1.0 + d.floorPassed * cellConductance));
                }
                else
                    d.floorPassed = passed;
            }
        }
        d.gain = applied;
        OPTO_LAB_PROBE(ch, applied, d.cellGain);
        return applied;
    }

    float gain(int ch) const noexcept { return channels[static_cast<size_t>(ch)].gain; }
    // What a reduction meter reads: the gain times the share of the input's own component the input element
    // leaves. That share is 1 - <sample x term> / <sample x sample>, both running means over about 50 ms: for
    // a sine, the element's loss at the fundamental. Exactly gain() while the element is off or is its even
    // part alone (pAsym 0), which takes nothing from that component. Display only: nothing in the audio path
    // reads it.
    float meterGain(int ch) const noexcept
    {
        const auto& d = channels[static_cast<size_t>(ch)];
        if (!(d.elementPower > 1.0e-18)) return d.gain;
        return d.gain * static_cast<float>(1.0 - std::clamp(d.elementTaken / d.elementPower, 0.0, 1.0));
    }

    // (P) The input element for one sample of the cell's input: what the mode takes off that sample ahead of
    // the gain (see kOptoElementStrengthDb). A kink k(x) = (1 + pAsym) over(x) + (1 - pAsym) over(-x), over(v)
    // the part of v above pThresh (through a quadratic knee pKnee pThresh wide), of which the element takes
    // c / 2, c = z / (1 + z), z = the strength at this knob position times `gain`. pAsym = 0 is the kink's
    // even part, pAsym = 1 the whole one-sided kink. The kink is not evaluated at the sample but averaged
    // over the straight line from the last sample to this one (first-order antiderivative anti-aliasing: a
    // hard kink evaluated point by point folds its harmonics back into the band), which reads it half a
    // sample late. At 2x and 4x the folded products of a -14 dBFS tone at Peak Reduction 100 stay under
    // -75 dB re the tone (the reference's own: -69 .. -91 dB for 10 kHz); with oversampling off the
    // first-order form leaves more above 5 kHz (-53 dB for 6 kHz, -38 dB for 12 kHz on a 44.1 kHz host).
    // Zero and stateless while the element is off; inert at and below prGate like the cell.
    float elementTerm(int ch, float input, float gain, float peakReduction) noexcept
    {
        if (!(cellParams.pStrength > 0.0f)) return 0.0f;
        auto& d = channels[static_cast<size_t>(ch)];
        const double prNorm = std::clamp(static_cast<double>(peakReduction) * 0.01, 0.0, 1.0);
        if (prNorm != cachedElementPr)
        {
            cachedElementPr = prNorm;
            const float pr = static_cast<float>(prNorm);
            cachedElementStrength = prNorm <= static_cast<double>(cellParams.prGate) ? 0.0f
                : cellParams.pStrength * optoElementStrength(pr)
                    * (cellParams.pPrExp != 0.0f ? std::pow(pr, cellParams.pPrExp) : 1.0f);
        }
        const double threshold = cellParams.pThresh;
        const double knee = std::max(static_cast<double>(cellParams.pKnee) * threshold, 0.0);
        // One side of the kink and its integral, for a sample v counted toward that side.
        const auto over = [threshold, knee](double v) noexcept {
            if (v >= threshold + 0.5 * knee) return v - threshold;
            if (knee > 0.0 && v > threshold - 0.5 * knee)
            {
                const double u = v - threshold + 0.5 * knee;
                return u * u / (2.0 * knee);
            }
            return 0.0;
        };
        const auto overIntegral = [threshold, knee](double v) noexcept {
            if (v >= threshold + 0.5 * knee)
                return 0.5 * (v - threshold) * (v - threshold) + knee * knee / 24.0;
            if (knee > 0.0 && v > threshold - 0.5 * knee)
            {
                const double u = v - threshold + 0.5 * knee;
                return u * u * u / (6.0 * knee);
            }
            return 0.0;
        };
        const double x = input;
        const double up = 1.0 + static_cast<double>(cellParams.pAsym);
        const double down = 1.0 - static_cast<double>(cellParams.pAsym);
        const auto kinkIntegral = [&overIntegral, up, down](double v) noexcept {
            return up * overIntegral(v) - down * overIntegral(-v);
        };
        const double last = d.elementInput;
        const double step = x - last;
        const double middle = 0.5 * (x + last);
        // The kink's mean over the line between the two samples; at a step too small to divide by, its
        // value in the middle. Both ends are evaluated with the constants of this call, so a change of
        // constants between two samples (a lab does that) leaves no stale integral behind.
        const double kink = std::abs(step) > 1.0e-6
            ? (kinkIntegral(x) - kinkIntegral(last)) / step
            : up * over(middle) + down * over(-middle);
        d.elementInput = x;
        const float z = cachedElementStrength * gain;
        const float term = static_cast<float>(0.5 * kink) * z / (1.0f + z);
        if (cellParams.pAsym != 0.0f)
        {   // For the meter (meterGain()): what the element takes of the input's own component, as running
            // means of sample x term and of sample x sample. The even part alone takes none of it.
            d.elementTaken += elementMeterStep * (x * static_cast<double>(term) - d.elementTaken);
            d.elementPower += elementMeterStep * (x * x - d.elementPower);
            if (d.elementPower < 1.0e-30) d.elementTaken = d.elementPower = 0.0;   // no denormal tail
        }
        return term;
    }
    // The colour-stage level of the last processed sample (converted on read; same value as a stored conversion).
    float inputLevelDb(int ch) const noexcept
    {
        return gainToDecibels(static_cast<float>(channels[static_cast<size_t>(ch)].levelPeak));
    }
    float dynamicGrDb(int ch) const noexcept { return static_cast<float>(channels[static_cast<size_t>(ch)].grDb); }
    float estimatedFrequencyHz(int ch) const noexcept
    {
        return static_cast<float>(channels[static_cast<size_t>(ch)].estimatedFrequencyHz);
    }

    // ---- lab-only hooks (fitting kernel) ----
    void setParams(const OptoCellParams& p) noexcept
    {
        cellParams = p;
        cachedPrNorm = -1.0;
        cachedElementPr = -1.0;
        if (sampleRate > 0.0) setRate(static_cast<float>(sampleRate));
    }
    const OptoCellParams& params() const noexcept { return cellParams; }
    void primeEquilibrium(int ch, double el, const double* population) noexcept
    {
        auto& d = channels[static_cast<size_t>(ch)];
        d.el = el;
        d.litLevel = cellParams.lfLitLog != 0.0f ? std::max(20.0 * std::log10(std::max(el, 1.0e-12)), static_cast<double>(cellParams.lightTableStartDb) - 60.0) : el;
        d.grRateSmooth = 0.0;
        d.drivePower = el * el;
        d.slewPower = el * el;
        // R14 trackers settled at the primed drive, hold armed: no spurious onset cut. el must be the drive PEAK
        // (the envelope holds peaks); priming from an instantaneous |s| mid-cycle would read as a step up.
        d.driveEnv = el;
        d.driveDb = d.driveSlow = 20.0 * std::log10(std::max(el, kLfCutEnvelopeFloor));
        d.driveHold = cutHoldSamples;
        d.drivePrev = 0.0;
        d.reversalPeak = el;
        d.slewLow = 0.0;
        if (cellParams.trapCapture > 0.0f)
        {
            const double g1 = population[1];
            const double kt = cellParams.trapCapture, kr = cellParams.trapEmit, tm = cellParams.trapCapacity;
            d.trap = tm > 0.0 ? kt * g1 / (kr + kt * g1 / tm) : (kr > 0.0 ? kt * g1 / kr : 0.0);
        }
        else
            d.trap = 0.0;
        d.reversalPhi = 1.0;
        d.reversalSign = 1;
        d.reversalCount = 0;
        double conductance = 0.0;
        for (int i = 0; i < kPopulations; ++i)
        {
            d.population[static_cast<size_t>(i)] = population[i];
            conductance += population[i];
        }
        if (cellParams.conductanceTableOn != 0.0f) conductance = conductanceFor(conductance);
        d.cellGain = 1.0 / (1.0 + conductance);
        d.gain = static_cast<float>(d.cellGain);
        d.grDb = 8.685889638065035 * std::log1p(conductance);
    }
    double cellGain(int ch) const noexcept { return channels[static_cast<size_t>(ch)].cellGain; }
    double elLevel(int ch) const noexcept { return channels[static_cast<size_t>(ch)].el; }
    double lightAt(double el) const noexcept { return lightFor(el); }
    double conductanceAt(double sumG) const noexcept { return conductanceFor(sumG); }

private:
    static constexpr size_t kWeightingSections = 5;
    // R14 envelope: release after the hold (fixed; the hold and the slow tracker carry the fitted timing) and
    // the floor of its dB value (-240 dB, i.e. 1e-12 linear: below any drive the light table responds to).
    static constexpr double kLfCutEnvelopeRelease = 0.010;
    static constexpr double kLfCutEnvelopeFloor = 1.0e-12;
    static constexpr double kLfCutFloorDb = -240.0;   // 20 log10(kLfCutEnvelopeFloor); keep the two in step

    struct ChannelState
    {
        double cellGain = 1.0, grDb = 0.0, el = 0.0, levelPeak = 0.0, litLevel = 0.0, grRateSmooth = 0.0;
        // R14 drive trackers: peak-held envelope, its dB value, the slow dB tracker, and the hold countdown.
        double driveEnv = 0.0, driveDb = kLfCutFloorDb, driveSlow = kLfCutFloorDb;
        int driveHold = 0;
        double panelOwn = 0.0, panelOwnPrev = 0.0;   // |side-chain drive| of the last two samples, for the linked pair
        double estimatedFrequencyHz = 1000.0;
        double drivePower = 0.0, slewPower = 0.0, drivePrev = 0.0;
        double reversalPeak = 0.0, reversalPhi = 1.0, slewLow = 0.0, trap = 0.0;
        double fluxHighPassLow = 0.0, fluxLow = 0.0, fluxPeak = 0.0;
        // What each polarity of the drive has moved through the panel, light x seconds (below zero: credit).
        std::array<double, 2> chargeSpent{};
        // How long the panel has had no drive, through two stages (1 = dark, the state at rest).
        std::array<double, 2> dark{{1.0, 1.0}};
        int darkHold = 0;   // samples left before the dark state may build (restarted while the panel is driven)
        double floorEl = 0.0, floorOwn = 0.0, floorOwnPrev = 0.0;
        std::array<double, 2> floorDark{{1.0, 1.0}};
        int floorInputHold = 0;
        double floorPassed = 1.0;   // the share of the cell's conductance the LF floor passes (1 = no floor)
        int reversalSign = 1, reversalCount = 0;
        double satSlew = 0.0, satRaw = 0.0, satDrive = 0.0;   // 5 ms power averages: slew, raw slew, level (HF plateau)
        std::array<double, kPopulations> population{};
        float gain = 1.0f;
        double elementInput = 0.0;   // elementTerm(): the last sample of the cell's input
        double elementTaken = 0.0, elementPower = 0.0;   // meterGain(): running means, see elementTerm()
        float floorHighPassLow = 0.0f, floorLow = 0.0f, floorBandLow = 0.0f;
        float floorPower = 0.0f, inputPower = 0.0f;
    };

    double sideChainGain(double prNorm) const noexcept
    {
        const double position = (prNorm - cellParams.prStart) / cellParams.prStep;
        double db;
        if (position <= 0.0)
            db = cellParams.prGainDb[0] + cellParams.prSlopeBelow * (prNorm - cellParams.prStart);
        else if (position >= cellParams.prGainRows - 1)
            db = cellParams.prGainDb[cellParams.prGainRows - 1];
        else
        {
            const int k = static_cast<int>(position);
            const double f = position - k;
            db = cellParams.prGainDb[k] + f * (cellParams.prGainDb[k + 1] - cellParams.prGainDb[k]);
        }
        return std::pow(10.0, db * 0.05);
    }

    // Piecewise-linear table over a dB axis: zero one step below the start, linear ramp to t[0] at the start,
    // exponential continuation above the top.
    static double tableLookup(const float* t, int n, double start, double step, double slopeAbove,
                              double levelDb) noexcept
    {
        const double top = start + step * (n - 1);
        if (levelDb >= top)
            return t[n - 1] * std::exp(slopeAbove * (levelDb - top));
        if (levelDb <= start - step) return 0.0;
        if (levelDb < start) return t[0] * (levelDb - (start - step)) / step;
        const double position = (levelDb - start) / step;
        const int k = std::min(static_cast<int>(position), n - 2);
        const double f = position - k;
        return t[k] + f * (t[k + 1] - t[k]);
    }

    double lightFor(double el) const noexcept
    {
        if (el <= 1.0e-30) return 0.0;
        return tableLookup(cellParams.lightTable, cellParams.lightTableSize, cellParams.lightTableStartDb,
                           cellParams.lightTableStepDb, cellParams.lightSlopeAbove,
                           8.685889638065035 * std::log(el));
    }

    double conductanceFor(double sumG) const noexcept
    {
        if (sumG <= 1.0e-30) return 0.0;
        return tableLookup(cellParams.conductanceTable, cellParams.conductanceTableSize,
                           cellParams.conductanceTableStartDb, cellParams.conductanceTableStepDb,
                           cellParams.conductanceSlopeAbove, 8.685889638065035 * std::log(sumG));
    }

    OptoCellParams cellParams{};
    std::array<ChannelState, kChannels> channels{};
    std::array<std::array<Biquad, kWeightingSections>, kChannels> loopWeighting;
    std::array<std::array<Biquad, kWeightingSections>, kChannels> levelWeighting;
    std::array<std::array<Biquad, 4>, kChannels> depthWeighting;
    std::array<Biquad, kChannels> lfTermFilter;
    std::array<Biquad, kChannels> limitFloorFilter;
    std::array<Biquad, kChannels> lfCutFilter;
    std::array<OptoSubsonicHighPass, kChannels> couplingInput;
    double sampleRate = 0.0, dt = 1.0 / 96000.0, elStep = 0.0;
    double levelPeakAttack = 0.0, levelPeakRelease = 0.0;
    double cachedPrNorm = -1.0, cachedSideChainGain = 0.0;
    // The input element (P): its strength at the knob's position (zero at and below prGate).
    double cachedElementPr = -1.0;
    float cachedElementStrength = 0.0f;
    float linkWeight = 0.0f;
    std::array<float, 6> floorLaw = kOptoFloorLawDefaults;
    std::array<float, 6> fluxLaw = kOptoFluxLimitDefaults;
    double fluxHighPassStep = 0.0, fluxLowPassStep = 0.0, fluxRelease = 0.0;
    float weightingNormalisation = 1.0f;
    float levelNormalisation = 1.0f;
    float shelfNormalisation = 1.0f, weightingNormalisation14 = 1.0f;
    double litAttack = 0.0, litRelease = 0.0, cutAttack = 0.0, cutRelease = 0.0, cutSlowStep = 0.0;
    int cutHoldSamples = 0, darkHoldSamples = 0;
    double attackRateStep = 1.0;
    double elFreqStep = 1.0, slewNormalisation = 1.0;
    double reversalRelease = 0.0, reversalHalfCycle1k = 48.0;
    double chargeRefillRate = 0.0, chargeRefillKeep = 1.0, elementMeterStep = 0.0, darkStep = 0.0, darkFloorStep = 0.0;
    double slewLowPassStep = 1.0, slewLowPassNormalisation = 1.0;
    float floorHighPassStep = 0.0f, floorLowPassStep = 0.0f, floorBandStep = 0.0f, floorPowerStep = 0.0f;
};

} // namespace duskaudio
