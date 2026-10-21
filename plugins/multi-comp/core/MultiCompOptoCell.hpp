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
// gain; gain(), dynamicGrDb() (the cell reduction before the LF floor, as production).
// setParams()/params()/primeEquilibrium()/lightAt()/conductanceAt()/elLevel() are lab-only hooks used by the
// fitting kernel; OPTO_LAB_PROBE is empty unless a lab build defines it.
#pragma once
#include "MultiCompOptoShelf.hpp"
#include "MultiCompLaneMath.hpp"
#include "MultiCompLanes.hpp"
#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstddef>
#include <type_traits>

// Functions the linked pair's step inlines whole (see prepareLightT).
#if defined(_MSC_VER) && !defined(__clang__)
#define OPTO_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define OPTO_FORCE_INLINE [[gnu::always_inline]]
#else
#define OPTO_FORCE_INLINE
#endif
#ifndef OPTO_LAB_PROBE
#define OPTO_LAB_PROBE(channel, appliedGain, cellGainValue)
#endif

namespace duskaudio
{
// A value per channel of the OPTO cell's linked pair, adjacent (so a two-lane read or write is one access).
template <class T>
struct alignas(2 * sizeof(T)) OptoPair
{
    T v[2];
    constexpr OptoPair() noexcept : v{} {}
    constexpr OptoPair(T x) noexcept : v{x, x} {}
    T& operator[](size_t c) noexcept { return v[c]; }
    const T& operator[](size_t c) const noexcept { return v[c]; }
};
// A biquad section's TDF-II state for both channels: {z1 of channel 0, z1 of 1, z2 of 0, z2 of 1}.
struct alignas(16) OptoBiquadState
{
    OptoPair<float> z1, z2;
};

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
// 2026-10-08, twelfth-pass calibration history for the defaults and laws below.
// The measurements below describe that historical float32 calibration. Subsequent
// changes are documented in the dated notes that follow; these measurements are
// not a qualification of every later combination of defaults.
//
// Frozen twelfth-pass parameter-set SHA-256:
// 109f64fbd516d29873c788b77f3aacb36f24446b0507f678c7b9a25438c8c850. The corresponding header's 78 cell
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
// states through CellState{}. Complete native133 controls pass with no loss of the original124 passing rows.
// Final header-default tables, OFF346 and FET136 identities, release passing-cell masks and the scratch patch
// chain are retained with the 2026-10-08 twelfth-pass calibration evidence.
//
// The cell uses separate polarity charge stores, and the meter includes the input
// element's attenuation. The input element reuses its fixed strength table with
// the whole kink. Charge credit is controlled by elPolCredit; the dated values
// above describe their calibration stage. The following dated changes and the
// defaults table together define the active model.
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
// 2026-10-15, the emission's shape within a cycle of the drive (indices 99..105: emitPeakExp 3, emitRiseGain 2,
// emitFadeLoKHz .55, emitFadeHiKHz .85, emitFadeTopLoKHz 2, emitFadeTopHiKHz 3, emitConserveOn 1; see prepareLight (V),
// emitLight and advanceLight). Measured on the native unit at 1 kHz (tools opto/h5): the gain ripple's 2f and 4f are in
// phase at +85..+97 degrees to the tone and the fifth harmonic reads -70.6 dBc at -4 dBFS, Peak Reduction .7, where
// this cell read -81.7: the law's hump along |s| was too wide. The light is now the law's times (|s| / A)^3 times
// (1 + 2 rise)+, A the smaller of the last half-cycle's peak and the quadrature amplitude; a sine prior per table
// entry and a per-cycle scale keep the clock-weighted charge, the release keeps the unshaped emission's clock, and the
// shape is confined to .55-3 kHz and to drives within the table. Core harmonic rows at -24 / -12 / -4 dBFS, PR .7,
// H3 / H5 / H7 (reference): -55.19 / -64.31 / -80.67 (-54.21 / -63.13 / -82.37), -55.42 / -66.52 / -75.65 (-55.23 /
// -65.65 / -74.17), -54.73 / -69.20 / -81.34 (-55.98 / -70.64 / -81.71): every one within 1.7 dB; fifth at -4 dBFS
// -81.7 -> -69.2. F1-1k ladder rms 0.103 / worst 0.33, unchanged; every core-test switch as before (0 regressions).
// Gate screen against the previous defaults (tools triage/refit, label h5-r3 against p55): the only new cells over
// 1 dB are the short-burst b010 at Limit .81, 40 ms (-0.9674 -> -1.0022) and the settled lf010_08 at Limit (-0.9945
// -> -1.0007); pooled music +0.0013, eleven +0.007, short +0.005, E1 +0.006 dB; worst holdout / E1 / blind2 +0.007 /
// +0.038 / +0.009, from hot 1 kHz notes, which read 0.004-0.014 dB deeper settled and 0.07-0.13 dB deeper in their
// first 40 ms (not traced: the next OPTO item). Accepted by the owner under the patch-53 precedent.
// kOptoCellDefaults order: elTauSeconds, lightCeiling, fastRelease, fastBimolecular, slowCharge, slowRelease, slowBimolecular, memoryCharge, memoryRelease, limitOutputMix, limitInputMix, compressInputMix, timeScaleLight, timeScaleExponent, depthWeighting, lowFrequencyFloor, lfTermGainDb, lfTermFrequency, lfTermGrSpan, conductanceTableOn, flashCharge, flashRelease, flashBimolecular, lfShelfDb, hFloor, lfLitTauAttack, lfLitTauRelease, lfLitLevelDb, lfLitSpanDb, lfLitFloor, lfLitLog, hChargeOnly, attackRateDbPerS, attackRatePower, attackRateTau, elFreqExponent, elFreqTau, hfPeakDb, lfPeakDb, midPeakDb, hf10kPeakDb, elRevHysteresis, elRevRelease, elFreqCorner, trapCapture, trapEmit, trapCapacity, pStrength, pPrExp, pThresh, pKnee, pAsym, lightTableStartDb, lightTableStepDb, lightSlopeAbove, conductanceTableStartDb, conductanceTableStepDb, conductanceSlopeAbove, prStart, prStep, prSlopeBelow, prGate, lfCutDb, lfCutFrequency, lfCutStartDb, lfCutSpanDb, lfCutTauAttack, lfCutTauSlow, lfCutTransSpanDb, lfCutHoldSeconds, lfCutMaxGapDb, lfCutCompWeight, elPolCap, elPolLeak, elPolKnee, elPolRefill, elPolCredit, elPolCapExp, darkRateCeiling, darkTau, darkLight, darkFloorTau, darkHoldSeconds, inputCouplingOn, darkHistoryStrength, darkHistoryBase, darkHistoryMemory, darkHistoryShoulder, darkHistoryShoulderMemory, darkHistoryLow, darkHistoryLowMemory, darkHistoryDensity, darkTrapFillPower, elSatBoost, elSatMargin, elSatSpanDb, elSatKneeDb, elSatFadeLoKHz, elSatFadeHiKHz, emitPeakExp, emitRiseGain, emitFadeLoKHz, emitFadeHiKHz, emitFadeTopLoKHz, emitFadeTopHiKHz, emitConserveOn
inline constexpr std::array<float, 106> kOptoCellDefaults{{
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
    -33.3f, 6.0f, 7.5f, 3.0f, 2.0f, 0.55f, 0.85f, 2.0f, 3.0f, 1.0f
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
    // The emission's shape within a cycle of the drive (0 = the law's light as it is): see prepareLight().
    float emitPeakExp, emitRiseGain, emitFadeLoKHz, emitFadeHiKHz, emitFadeTopLoKHz, emitFadeTopHiKHz;
    // The shape's charge kept cycle by cycle on any waveform: see emitLight(). On (1) in the plugin; 0 is the lab's
    // measurement of the shape without it, shipped the way the cell's other lab switches are (inputCouplingOn,
    // conductanceTableOn, hChargeOnly).
    float emitConserveOn;
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

// Slots 0..51 of kOptoCellDefaults are the leading 52 floats of OptoCellParams, declared in slot order. Keep the two
// in step: optoCellParamsFrom assigns them by name, the order comment above lists them by slot.
static_assert(std::is_standard_layout_v<OptoCellParams>);
static_assert(offsetof(OptoCellParams, pAsym) - offsetof(OptoCellParams, elTauSeconds) == 51 * sizeof(float),
              "OptoCellParams: the 52 leading constants must stay contiguous floats in kOptoCellDefaults order");

// Parameters from a kOptoCellDefaults-ordered array (the tables stay the built-in ones). The plugin runs the
// defaults; a lab passes candidates through this. Slot k is the k-th name of the order comment above (the lab tools
// address a constant by that name), and every slot is assigned by name.
inline OptoCellParams optoCellParamsFrom(
    const std::array<float, kOptoCellDefaults.size()>& values) noexcept
{
    OptoCellParams p{};
    p.elTauSeconds = values[0]; p.lightCeiling = values[1];
    p.fastRelease = values[2]; p.fastBimolecular = values[3];
    p.slowCharge = values[4]; p.slowRelease = values[5]; p.slowBimolecular = values[6];
    p.memoryCharge = values[7]; p.memoryRelease = values[8];
    p.limitOutputMix = values[9]; p.limitInputMix = values[10]; p.compressInputMix = values[11];
    p.timeScaleLight = values[12]; p.timeScaleExponent = values[13];
    p.depthWeighting = values[14]; p.lowFrequencyFloor = values[15];
    p.lfTermGainDb = values[16]; p.lfTermFrequency = values[17]; p.lfTermGrSpan = values[18];
    p.conductanceTableOn = values[19];
    p.flashCharge = values[20]; p.flashRelease = values[21]; p.flashBimolecular = values[22];
    p.lfShelfDb = values[23]; p.hFloor = values[24];
    p.lfLitTauAttack = values[25]; p.lfLitTauRelease = values[26];
    p.lfLitLevelDb = values[27]; p.lfLitSpanDb = values[28];
    p.lfLitFloor = values[29]; p.lfLitLog = values[30]; p.hChargeOnly = values[31];
    p.attackRateDbPerS = values[32]; p.attackRatePower = values[33]; p.attackRateTau = values[34];
    p.elFreqExponent = values[35]; p.elFreqTau = values[36];
    p.hfPeakDb = values[37]; p.lfPeakDb = values[38]; p.midPeakDb = values[39]; p.hf10kPeakDb = values[40];
    p.elRevHysteresis = values[41]; p.elRevRelease = values[42]; p.elFreqCorner = values[43];
    p.trapCapture = values[44]; p.trapEmit = values[45]; p.trapCapacity = values[46];
    p.pStrength = values[47]; p.pPrExp = values[48]; p.pThresh = values[49]; p.pKnee = values[50]; p.pAsym = values[51];
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
    p.emitPeakExp = values[99]; p.emitRiseGain = values[100]; p.emitFadeLoKHz = values[101]; p.emitFadeHiKHz = values[102];
    p.emitFadeTopLoKHz = values[103]; p.emitFadeTopHiKHz = values[104];
    p.emitConserveOn = values[105];
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

    OptoCell() noexcept
    {
        cellParams = defaultOptoCellParams();
        clockPower.setExponent(static_cast<double>(cellParams.timeScaleExponent));
        buildEmitPrior();
        buildTableQuiet();
    }

    // Coefficients at the processing rate; history is kept.
    void setRate(float sr) noexcept
    {
        sampleRate = static_cast<double>(sr);
        dt = 1.0 / sampleRate;
        invDt = sampleRate;   // 1 / dt for the stores' moved charge per second
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
        for (auto& hold : state.driveHold.v) hold = std::min(hold, cutHoldSamples);
        // The panel's charge stores (see process()): per sample, the rate at which a store refills while the
        // drive has the other polarity and the share of what it has spent that is still spent after it.
        chargeRefillRate = cellParams.elPolRefill > 0.0f ? dt / static_cast<double>(cellParams.elPolRefill) : 0.0;
        chargeRefillKeep = std::exp(-chargeRefillRate);
        elementMeterStep = 1.0 - std::exp(-dt / 0.05);   // meterGain(): 50 ms running means
        darkStep = 1.0 - std::exp(-dt / std::max(static_cast<double>(cellParams.darkTau), 1.0e-6));
        darkFloorStep = cellParams.darkFloorTau > 0.0f ? 1.0 - std::exp(-dt / static_cast<double>(cellParams.darkFloorTau)) : 0.0;
        emitLeak = std::exp(-dt / 0.005);   // prepareLight: the shape's half-cycle reference leaks in digital silence (5 ms)
        // Truncated like the R14 hold; the clamp keeps a lab-set NaN or absurd value out of the int cast.
        const float darkHold = cellParams.darkHoldSeconds > 0.0f ? std::min(cellParams.darkHoldSeconds, 1.0f) : 0.0f;
        darkHoldSamples = static_cast<int>(static_cast<double>(darkHold) * sampleRate);
        for (size_t c = 0; c < kChannels; ++c)
        {
            state.darkHold[c] = std::min(state.darkHold[c], darkHoldSamples);
            state.floorInputHold[c] = std::min(state.floorInputHold[c], darkHoldSamples);
        }
        // Production LF floor energy path (exact coefficients).
        updateFloorBand();
        floorBandStep = 1.0f - std::exp(-6.283185307f * 1000.0f / sr);
        for (auto& filter : limitFloorFilter)
            filter.setCoeffs(Biquad::lowPass(sr, 300.0f, 0.70710678f));
        floorPowerStep = 1.0f - std::exp(-1.0f / (0.050f * sr));
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
        floorLimitRatioScale = std::pow(floorLaw[5], 0.5f * floorLaw[4]);   // k^(e/2) in Limit
        floorExpansionMode = -1;   // the blend's expansions start again
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
        state = CellState{};
        for (auto& channel : loopWeighting) for (auto& f : channel) f.reset();
        for (auto& channel : levelWeighting) for (auto& f : channel) f.reset();
        for (auto& channel : depthWeighting) for (auto& f : channel) f.reset();
        for (auto& f : lfTermFilter) f.reset();
        for (auto& f : limitFloorFilter) f.reset();
        for (auto& f : lfCutFilter) f.reset();
        for (auto& f : couplingInput) f.reset();
        loopWeightingState = {};
        levelWeightingState = {};
        depthWeightingState = {};
        lfTermState = limitFloorState = lfCutState = OptoBiquadState{};
    }

    // The only sample counters are the R14 hold countdown and the dark state's; setRate() clamps each to its
    // new hold length, which is all a rate change needs (a hold in progress ends no later than a fresh one would).
    template <typename ScaleCounter>
    void scaleCounters(const ScaleCounter&) noexcept {}

    // What one channel's side chain asks of the cell for this sample: the light after the frequency law, the
    // light the drive alone asks for, the signed drive, and the floor's drive and band / input powers.
    // One step's light on its way through the panel (prepareLight -> emitLight -> advanceLight), per lane:
    // LightFrame is the one-lane frame.
    template <class L> struct FrameT
    {
        using D = typename L::D;
        using F = typename L::F;
        D light, lawLight, s, floorDrive;
        F floorPower, inputPower;
        bool inert;
        // The emission's shape (prepareLight): the faded weight, the sine prior (raw: emitLight fades it with its
        // per-cycle scale) and the fade, applied behind the charge stores (emitLight); clockLight is the emission
        // without the shape: the release side's clock.
        D emitWeight = L::d(1.0), emitPrior = L::d(1.0), emitFade = L::d(0.0), clockLight = L::d(0.0);
        // timeScale() of the two lights emitLight's charge sums already evaluated, with the lights they were
        // evaluated at (NaN: none): advanceLight reuses a value whose light has not changed since (linkedFrame
        // can replace the light with the partner's).
        D scaledLight = L::d(std::numeric_limits<double>::quiet_NaN()), lightScale = L::d(0.0);
        D scaledClockLight = L::d(std::numeric_limits<double>::quiet_NaN()), clockLightScale = L::d(0.0);
    };
    using LightFrame = FrameT<lanes::Lane1>;

    // The channel's own side chain, up to the light. A linked pair meets behind this point (processLightPair).
    LightFrame prepareLight(int ch, float input, float /*compressed*/, float sidechain,
                            bool external, float optoDetector, bool useOptoDetector,
                            bool limit, float peakReduction) noexcept
    {
        return prepareLightT<lanes::Lane1>(ch, input, sidechain, external, optoDetector, useOptoDetector,
                                               limit, peakReduction);
    }

private:
    struct CellState;   // defined with the cell's other state below
public:
    // ------------------------------------------------------------------------------------------------ lanes
    // prepareLight() is written once over a lane policy (MultiCompLanes.hpp): Lane1 steps channel `ch`;
    // Lane2 steps both channels of a linked pair at once (`ch` unused) with each lane performing
    // exactly Lane1's arithmetic. State reads and writes go through get / put: with the state stored as channel
    // pairs (CellState), a pair's read or write is one access.
    template <class L, class T>
    auto get(int ch, OptoPair<T> CellState::* field) const noexcept
    {
        const OptoPair<T>& pair = state.*field;
        if constexpr (L::kLanes == 1) return pair[static_cast<size_t>(ch)];
#if defined(__aarch64__)
        else if constexpr (std::is_same_v<T, double>) return lanes::D2{vld1q_f64(pair.v)};
        else if constexpr (std::is_same_v<T, float>) return lanes::F2{vld1_f32(pair.v)};
        else return lanes::I2{vld1_s32(pair.v)};
#endif
    }
    template <class L, class T, class V>
    void put(int ch, OptoPair<T> CellState::* field, V value) noexcept
    {
        OptoPair<T>& pair = state.*field;
        if constexpr (L::kLanes == 1) pair[static_cast<size_t>(ch)] = value;
#if defined(__aarch64__)
        else if constexpr (std::is_same_v<T, double>) vst1q_f64(pair.v, value.v);
        else if constexpr (std::is_same_v<T, float>) vst1_f32(pair.v, value.v);
        else vst1_s32(pair.v, value.v);
#endif
    }
    template <class L, size_t N>
    typename L::D getAt(int ch, std::array<OptoPair<double>, N> CellState::* field, size_t k) const noexcept
    {
        const OptoPair<double>& pair = (state.*field)[k];
        if constexpr (L::kLanes == 1) return pair[static_cast<size_t>(ch)];
#if defined(__aarch64__)
        else return lanes::D2{vld1q_f64(pair.v)};
#endif
    }
    template <class L, size_t N>
    void putAt(int ch, std::array<OptoPair<double>, N> CellState::* field, size_t k, typename L::D value) noexcept
    {
        OptoPair<double>& pair = (state.*field)[k];
        if constexpr (L::kLanes == 1) pair[static_cast<size_t>(ch)] = value;
#if defined(__aarch64__)
        else vst1q_f64(pair.v, value.v);
#endif
    }
    // Biquad::process() for channel ch, or for both channels at once, on a section's coefficients (every channel's
    // filter carries the same: setRate sets them alike) and the section's state in the cell (OptoBiquadState).
    template <class L>
    typename L::F biquad(const Biquad& section, OptoBiquadState& st, int ch, typename L::F x) noexcept
    {
        const auto& k = section.coeffs();
        if constexpr (L::kLanes == 1)
        {
            const size_t c = static_cast<size_t>(ch);
            const float y = k.b0 * x + st.z1[c];
            st.z1[c] = k.b1 * x - k.a1 * y + st.z2[c];
            st.z2[c] = k.b2 * x - k.a2 * y;
            return y;
        }
#if defined(__aarch64__)
        else
        {
            const float32x4_t z = vld1q_f32(st.z1.v);   // {z1 0, z1 1, z2 0, z2 1}
            const auto s1 = lanes::F2{vget_low_f32(z)}, s2 = lanes::F2{vget_high_f32(z)};
            const auto y = L::f(k.b0) * x + s1;
            const auto n1 = L::f(k.b1) * x - L::f(k.a1) * y + s2;
            const auto n2 = L::f(k.b2) * x - L::f(k.a2) * y;
            vst1q_f32(st.z1.v, vcombine_f32(n1.v, n2.v));
            return y;
        }
#endif
    }
    template <class V> static V vclamp(V v, V lo, V hi) noexcept
    {   // std::clamp: (v < lo) ? lo : (hi < v) ? hi : v, NaN through
        using lanes::Lane1;
        if constexpr (std::is_same_v<V, double> || std::is_same_v<V, float>) return std::clamp(v, lo, hi);
#if defined(__aarch64__)
        else if constexpr (std::is_same_v<V, lanes::D2>)
            return lanes::Lane2::sel(v < lo, lo, lanes::Lane2::sel(hi < v, hi, v));
        else
            return lanes::Lane2::sel(v < lo, lo, lanes::Lane2::sel(hi < v, hi, v));
#endif
    }

    // prepareLightT(), emitLightRef() and advanceLightT() are forced inline (with their helpers): the pair's step
    // (processLightPair) then keeps the frame in registers across the three (3 % of the OPTO step, 2026-10-21).
    template <class L>
    OPTO_FORCE_INLINE FrameT<L> prepareLightT(int ch, typename L::F input, typename L::F sidechain, bool external,
                            typename L::F optoDetector, bool useOptoDetector, bool limit,
                            float peakReduction) noexcept
    {
        using D = typename L::D;
        using F = typename L::F;
        using I = typename L::I;
        using lanes::vmax; using lanes::vmin; using lanes::vabs; using lanes::vsqrt;
        using lanes::visfinite;
        const auto Kd = [](double x) noexcept { return L::d(x); };
        const auto Kf = [](float x) noexcept { return L::f(x); };
        const F base = useOptoDetector ? optoDetector : (external ? sidechain : input);

        // Production LF floor energies from the pre-gain detector; both mode paths advance.
        F floorHighPassLow = get<L>(ch, &CellState::floorHighPassLow);
        F floorLow = get<L>(ch, &CellState::floorLow);
        F floorBandLow = get<L>(ch, &CellState::floorBandLow);
        floorHighPassLow = floorHighPassLow + Kf(floorHighPassStep) * (base - floorHighPassLow);
        floorLow = floorLow + Kf(floorLowPassStep) * (base - floorHighPassLow - floorLow);
        floorBandLow = floorBandLow + Kf(floorBandStep) * (floorLow - floorBandLow);
        put<L>(ch, &CellState::floorHighPassLow, floorHighPassLow);
        put<L>(ch, &CellState::floorLow, floorLow);
        put<L>(ch, &CellState::floorBandLow, floorBandLow);
        const F limitFloorSignal = biquad<L>(limitFloorFilter[0], limitFloorState, ch, floorLow);
        const F floorSignal = limit ? limitFloorSignal : floorBandLow;
        F floorPower = get<L>(ch, &CellState::floorPower);
        F inputPower = get<L>(ch, &CellState::inputPower);
        floorPower = floorPower + Kf(floorPowerStep) * (floorSignal * floorSignal - floorPower);
        inputPower = inputPower + Kf(floorPowerStep) * (base * base - inputPower);
        put<L>(ch, &CellState::floorPower, floorPower);
        put<L>(ch, &CellState::inputPower, inputPower);

        // Colour-stage level: pre-gain weighted detector through a 50 us / 40 ms peak follower.
        F levelSignal = base;
        for (size_t section = 0; section < kWeightingSections; ++section)
            levelSignal = biquad<L>(levelWeighting[0][section], levelWeightingState[section], ch, levelSignal);
        const D levelAbs = vabs(L::toD(levelSignal) * Kd(levelNormalisation));

        // Feedback side chain on the cell gain (before the floor).
        const D gPrev = get<L>(ch, &CellState::cellGain);
        const D mix = limit
            ? Kd(static_cast<double>(cellParams.limitOutputMix)) * gPrev + Kd(cellParams.limitInputMix)
            : gPrev + Kd(cellParams.compressInputMix);
        F coupled = base;
        {   // couplingInput's process() on the state in the cell (every channel's filter has the same coefficients)
            const auto k = couplingInput[0].coefficients();
            const D x = L::toD(base);
            const D y = Kd(k.b0) * x + get<L>(ch, &CellState::couplingS1);
            put<L>(ch, &CellState::couplingS1, Kd(k.b1) * x - Kd(k.a1) * y + get<L>(ch, &CellState::couplingS2));
            put<L>(ch, &CellState::couplingS2, Kd(k.b2) * x - Kd(k.a2) * y);
            coupled = L::toF(y);
        }
        F loop = L::toF(mix * L::toD(cellParams.inputCouplingOn != 0.0f ? coupled : base));
        if (cellParams.lfLitSpanDb > 0.0f)
        {
            // Lit-gated LF shelf: the LF sensitivity needs an illuminated photocell (tracked EL level at n-1).
            for (size_t section = 1; section < kWeightingSections; ++section)
                loop = biquad<L>(loopWeighting[0][section], loopWeightingState[section], ch, loop);
            loop = loop * Kf(weightingNormalisation14);
            const F shelved = biquad<L>(loopWeighting[0][0], loopWeightingState[0], ch, loop) * Kf(shelfNormalisation);
            const D litLevel = get<L>(ch, &CellState::litLevel);
            const D litDb = cellParams.lfLitLog != 0.0f ? litLevel
                : Kd(20.0) * L::map([](double v) noexcept { return std::log10(v); }, vmax(litLevel, Kd(1.0e-12)));
            const F engage = vclamp(L::toF((litDb - Kd(cellParams.lightTableStartDb + cellParams.lfLitLevelDb))
                                           * Kd(invLfLitSpanDb)), Kf(0.0f), Kf(1.0f));
            const F w = Kf(cellParams.lfLitFloor) + Kf(1.0f - cellParams.lfLitFloor) * engage;
            loop = loop + w * (shelved - loop);
        }
        else
        {
            for (size_t section = 0; section < kWeightingSections; ++section)
                loop = biquad<L>(loopWeighting[0][section], loopWeightingState[section], ch, loop);
            loop = loop * Kf(weightingNormalisation);
        }
        if (cellParams.lfCutDb != 0.0f)
        {   // R14: LF loss in the side-chain drive at the onset of a loud passage (see prepareLight's notes)
            const D driveDb = get<L>(ch, &CellState::driveDb);
            const D driveSlow = get<L>(ch, &CellState::driveSlow);
            const F wAbs = vclamp(L::toF((driveDb - Kd(static_cast<double>(cellParams.lfCutStartDb)))
                                         / Kd(std::max(static_cast<double>(cellParams.lfCutSpanDb), 1.0e-6))), Kf(0.0f), Kf(1.0f));
            const F wTr = vclamp(L::toF((driveDb - driveSlow)
                                        / Kd(std::max(static_cast<double>(cellParams.lfCutTransSpanDb), 1.0e-6))), Kf(0.0f), Kf(1.0f));
            const F wc = wAbs * wTr * Kf(limit ? 1.0f : cellParams.lfCutCompWeight);
            loop = loop + wc * (biquad<L>(lfCutFilter[0], lfCutState, ch, loop) - loop);
        }
        // Production optoDepthWeighting, fed by the cell reduction at n-1; both mode paths advance.
        const F priorReduction = L::toF(get<L>(ch, &CellState::grDb));
        const F compressShelfWeight = vclamp((priorReduction - Kf(8.0f)) * Kf(1.0f / 20.0f), Kf(0.0f), Kf(1.0f));
        const F limitShelfWeight = vclamp((priorReduction - Kf(12.0f)) * Kf(1.0f / 28.0f), Kf(0.0f), Kf(1.0f));
        const F depthPeakWeight = vclamp((priorReduction - Kf(8.0f)) * Kf(0.125f), Kf(0.0f), Kf(1.0f));
        F compressDetector = loop + compressShelfWeight * (biquad<L>(depthWeighting[0][0], depthWeightingState[0], ch, loop) - loop);
        F limitDetector = loop + limitShelfWeight * (biquad<L>(depthWeighting[0][2], depthWeightingState[2], ch, loop) - loop);
        compressDetector = compressDetector
            + depthPeakWeight * (biquad<L>(depthWeighting[0][1], depthWeightingState[1], ch, compressDetector) - compressDetector);
        limitDetector = limitDetector + depthPeakWeight * (biquad<L>(depthWeighting[0][3], depthWeightingState[3], ch, limitDetector) - limitDetector);
        if (cellParams.depthWeighting != 0.0f)
            loop = limit ? limitDetector : compressDetector;
        // LF detector-law term, fed by the same reduction at n-1.
        if (cellParams.lfTermGainDb != 0.0f)
        {
            const F lfWeight = vclamp(priorReduction * Kf(invLfTermGrSpan), Kf(0.0f), Kf(1.0f));
            loop = loop + lfWeight * (biquad<L>(lfTermFilter[0], lfTermState, ch, loop) - loop);
        }

        const double prNorm = std::clamp(static_cast<double>(peakReduction) * 0.01, 0.0, 1.0);
        const bool inert = prNorm <= static_cast<double>(cellParams.prGate);
        if (prNorm != cachedPrNorm)
        {
            cachedPrNorm = prNorm;
            cachedSideChainGain = sideChainGain(prNorm);
        }
        // The input owns the floor and release laws; a coupling tail continues to supply light only.
        const D floorOwn = inert ? Kd(0.0) : Kd(cachedSideChainGain) * mix * levelAbs;
        const D floorDrive = floorOwn;
        D s = inert ? Kd(0.0) : Kd(cachedSideChainGain) * L::toD(loop);
        if (fluxLaw[0] > 0.0f)
        {   // side-chain flux limit (see kOptoFluxLimitDefaults); the scale comes from the flux up to n-1
            D fluxHighPassLow = get<L>(ch, &CellState::fluxHighPassLow);
            D fluxLow = get<L>(ch, &CellState::fluxLow);
            D fluxPeak = get<L>(ch, &CellState::fluxPeak);
            const D ratio = fluxPeak / Kd(static_cast<double>(fluxLaw[0]));
            fluxHighPassLow = fluxHighPassLow + Kd(fluxHighPassStep) * (s - fluxHighPassLow);
            fluxLow = fluxLow + Kd(fluxLowPassStep) * (s - fluxHighPassLow - fluxLow);
            fluxPeak = vmax(vabs(fluxLow), fluxPeak * Kd(fluxRelease));
            fluxHighPassLow = L::sel(vabs(fluxHighPassLow) < Kd(1.0e-30), Kd(0.0), fluxHighPassLow);
            fluxLow = L::sel(vabs(fluxLow) < Kd(1.0e-30), Kd(0.0), fluxLow);
            fluxPeak = L::sel(fluxPeak < Kd(1.0e-30), Kd(0.0), fluxPeak);
            put<L>(ch, &CellState::fluxHighPassLow, fluxHighPassLow);
            put<L>(ch, &CellState::fluxLow, fluxLow);
            put<L>(ch, &CellState::fluxPeak, fluxPeak);
            const double k = std::max(static_cast<double>(fluxLaw[5]), 1.0e-3);
            const double e = -static_cast<double>(fluxLaw[3]) / k;
            s = s * L::map([k, e](double r) noexcept { return std::pow(1.0 + std::pow(r, k), e); }, ratio);
        }
        if (cellParams.lfCutDb != 0.0f)
        {
            const D a = vabs(s);
            D driveEnv = get<L>(ch, &CellState::driveEnv);
            I driveHold = get<L>(ch, &CellState::driveHold);
            const auto attack = a >= driveEnv;
            const auto holding = !attack && (driveHold > L::i(0));
            D released = a + (driveEnv - a) * Kd(cutRelease);
            released = L::sel(released < Kd(1.0e-30), Kd(0.0), released);   // no denormal tail after release
            driveEnv = L::sel(attack, a + (driveEnv - a) * Kd(cutAttack), L::sel(holding, driveEnv, released));
            driveHold = L::sel(attack, L::i(cutHoldSamples), L::sel(holding, driveHold - L::i(1), driveHold));
            put<L>(ch, &CellState::driveEnv, driveEnv);
            put<L>(ch, &CellState::driveHold, driveHold);
            const D driveDb = Kd(20.0) * L::map([](double v) noexcept { return std::log10(v); },
                                               vmax(driveEnv, Kd(kLfCutEnvelopeFloor)));
            D driveSlow = get<L>(ch, &CellState::driveSlow);
            driveSlow = driveSlow + Kd(cutSlowStep) * (driveDb - driveSlow);
            if (cellParams.lfCutMaxGapDb > 0.0f)
                driveSlow = vmax(driveSlow, driveDb - Kd(static_cast<double>(cellParams.lfCutMaxGapDb)));
            put<L>(ch, &CellState::driveDb, driveDb);
            put<L>(ch, &CellState::driveSlow, driveSlow);
        }
        // The channel's own drive: a linked pair meets behind the panel (linkedFrame).
        const D panelDrive = vabs(s);
        D el = get<L>(ch, &CellState::el);
        el = el + Kd(elStep) * (panelDrive - el);
        put<L>(ch, &CellState::el, el);
        // el in dB, read by the lit level and by the light table: the table's own argument wherever it reads (el above
        // the table's quiet level, so above 1e-12).
        const bool litInDb = cellParams.lfLitLog != 0.0f;
        const D elLevelDb = litInDb || !inert ? Kd(8.685889638065035) * lanes::lanemath::log(vmax(el, Kd(1.0e-12)))
                                              : Kd(0.0);
        D litLevel = get<L>(ch, &CellState::litLevel);
        if (litInDb)
        {
            const D elDb = vmax(elLevelDb, Kd(static_cast<double>(cellParams.lightTableStartDb) - 60.0));
            litLevel = elDb + (litLevel - elDb) * L::sel(elDb > litLevel, Kd(litAttack), Kd(litRelease));
        }
        else
            litLevel = el + (litLevel - el) * L::sel(el > litLevel, Kd(litAttack), Kd(litRelease));
        put<L>(ch, &CellState::litLevel, litLevel);
        D light = inert ? Kd(0.0) : lightAtT<L>(el, elLevelDb);
        if (cellParams.lightCeiling > 0.0f)
            light = light / (Kd(1.0) + light / Kd(static_cast<double>(cellParams.lightCeiling)));
        const D lawLight = light;   // what the drive asks of the panel, ahead of the frequency law and the charge stores
        D emitWeight = Kd(1.0), emitPriorValue = Kd(1.0), emitFadeValue = Kd(0.0);
        if (cellParams.emitPeakExp > 0.0f || cellParams.emitRiseGain != 0.0f)
        {
            // (V) The emission's shape within a cycle of the drive (see prepareLight's notes).
            const D magnitude = vabs(s);
            D emitLast = get<L>(ch, &CellState::emitLast);
            const auto sPositive = s >= Kd(0.0);
            const auto lastPositive = emitLast >= Kd(0.0);
            const auto reversal = (sPositive && !lastPositive) || (!sPositive && lastPositive);
            const D slewRaw = (s - emitLast) * Kd(slewNormalisation);
            emitLast = L::sel(visfinite(s), s, Kd(0.0));
            put<L>(ch, &CellState::emitLast, emitLast);
            D emitDrivePower = get<L>(ch, &CellState::emitDrivePower);
            D emitSlewPower = get<L>(ch, &CellState::emitSlewPower);
            emitDrivePower = emitDrivePower + Kd(elFreqStep) * (s * s - emitDrivePower);
            emitSlewPower = emitSlewPower + Kd(elFreqStep) * (slewRaw * slewRaw - emitSlewPower);
            emitDrivePower = L::sel(!(emitDrivePower >= Kd(1.0e-30)) || !visfinite(emitDrivePower), Kd(0.0), emitDrivePower);
            emitSlewPower = L::sel(!(emitSlewPower >= Kd(1.0e-30)) || !visfinite(emitSlewPower), Kd(0.0), emitSlewPower);
            put<L>(ch, &CellState::emitDrivePower, emitDrivePower);
            put<L>(ch, &CellState::emitSlewPower, emitSlewPower);
            D emitPeakPrev = get<L>(ch, &CellState::emitPeakPrev);
            D emitPeakCur = get<L>(ch, &CellState::emitPeakCur);
            emitPeakPrev = L::sel(reversal, emitPeakCur, emitPeakPrev);
            emitPeakCur = L::sel(reversal, Kd(0.0), emitPeakCur);
            emitPeakCur = vmax(emitPeakCur, magnitude);
            emitPeakCur = L::sel(visfinite(emitPeakCur), emitPeakCur, Kd(0.0));
            {
                const auto silent = !(magnitude > Kd(1.0e-30));
                D leaked = emitPeakPrev * Kd(emitLeak);
                leaked = L::sel(leaked < Kd(1.0e-30), Kd(0.0), leaked);   // no denormal tail
                emitPeakPrev = L::sel(silent, leaked, emitPeakPrev);
            }
            emitPeakPrev = L::sel(visfinite(emitPeakPrev), emitPeakPrev, Kd(0.0));
            put<L>(ch, &CellState::emitPeakPrev, emitPeakPrev);
            put<L>(ch, &CellState::emitPeakCur, emitPeakCur);
            // A lane whose light is exactly zero and whose last emission is zero takes nothing from the weight, the
            // fade or the prior below: they multiply the zero light, and the charge term's trapezoid reads the fade
            // against the zero last emission. With every lane so they are skipped, the state above still advancing;
            // the shape's sin / cos and reference-log expansions then refresh from wherever the turn and the
            // reference have moved (results change at the rounding level).
            const auto shapeIdle = light == Kd(0.0) && get<L>(ch, &CellState::emitLastEmitted) == Kd(0.0);
            if (!L::all(shapeIdle))
            {
                const D ratio = vclamp(emitSlewPower / vmax(emitDrivePower, Kd(1.0e-30)), Kd(1.0e-4), Kd(1.0e4));
                const D halfTurn = vmin(Kd(3.141592653589793 * 1000.0) * vsqrt(ratio) * Kd(dt), Kd(1.2));
                // sin and cos of the half turn on their second-order expansion about the last exact evaluation, which
                // is redone (per lane) once the turn has moved kShapeTurnTolerance from it: error <= d^3 / 6.
                D turnRef = get<L>(ch, &CellState::shapeTurn);
                D sinRef = get<L>(ch, &CellState::shapeSin);
                D cosRef = get<L>(ch, &CellState::shapeCos);
                const auto turnStale = !(vabs(halfTurn - turnRef) <= Kd(kShapeTurnTolerance)) && !shapeIdle;
                if (L::any(turnStale))
                {
                    turnRef = L::sel(turnStale, halfTurn, turnRef);
                    sinRef = L::sel(turnStale, L::map([](double v) noexcept { return std::sin(v); }, halfTurn), sinRef);
                    cosRef = L::sel(turnStale, L::map([](double v) noexcept { return std::cos(v); }, halfTurn), cosRef);
                    put<L>(ch, &CellState::shapeTurn, turnRef);
                    put<L>(ch, &CellState::shapeSin, sinRef);
                    put<L>(ch, &CellState::shapeCos, cosRef);
                }
                const D turnStep = halfTurn - turnRef;
                const D turnHalfSquare = Kd(0.5) * turnStep * turnStep;
                const D sinTurn = sinRef + cosRef * turnStep - sinRef * turnHalfSquare;
                const D cosTurn = cosRef - sinRef * turnStep - cosRef * turnHalfSquare;
                const D cosinePart = (slewRaw / vsqrt(ratio) - s * sinTurn) / cosTurn;
                const D amplitude = vsqrt(s * s + cosinePart * cosinePart);
                const D reference = vmin(emitPeakPrev, amplitude);
                const auto beyond = !(reference > Kd(1.0e-30)) || magnitude > Kd(1.0001) * reference;
                D w = Kd(1.0);
                if (!L::all(beyond))
                {
                    D shaped = Kd(1.0);
                    if (cellParams.emitPeakExp > 0.0f)
                    {
                        const double exponent = static_cast<double>(cellParams.emitPeakExp);
                        const D v = vmin(magnitude / reference, Kd(1.0));
                        shaped = exponent == 3.0 ? v * v * v   // the shipped exponent: two multiplies for the library power
                            : L::map([exponent](double x) noexcept { return std::pow(x, exponent); }, v);
                    }
                    if (cellParams.emitRiseGain != 0.0f)
                    {
                        const D rise = L::sel(s >= Kd(0.0), cosinePart, -cosinePart) / amplitude;
                        shaped = shaped * vmax(Kd(0.0), Kd(1.0) + Kd(static_cast<double>(cellParams.emitRiseGain))
                                                               * vclamp(rise, Kd(-1.0), Kd(1.0)));
                    }
                    w = L::sel(beyond, w, shaped);
                }
                D fade = L::sel(beyond, Kd(0.0), Kd(1.0));
                if (cellParams.emitFadeHiKHz > cellParams.emitFadeLoKHz)
                {
                    const double lo = static_cast<double>(cellParams.emitFadeLoKHz) * cellParams.emitFadeLoKHz;
                    fade = fade * vclamp((ratio - Kd(lo)) * Kd(invFadeLowSpan), Kd(0.0), Kd(1.0));
                }
                if (cellParams.emitFadeTopHiKHz > cellParams.emitFadeTopLoKHz)
                {
                    const double hi = static_cast<double>(cellParams.emitFadeTopHiKHz) * cellParams.emitFadeTopHiKHz;
                    fade = fade * vclamp((Kd(hi) - ratio) * Kd(invFadeTopSpan), Kd(0.0), Kd(1.0));
                }
                // An idle lane's expansion is not refreshed and may read NaN: its level is taken as the floor's.
                const D referenceDb = L::sel(reference > Kd(1.0e-30) && !shapeIdle,
                    Kd(8.685889638065035) * expandedLog<L>(ch, reference, 0.0, &CellState::referenceAt,
                                                           &CellState::referenceInverse, &CellState::referenceLog,
                                                           !shapeIdle),
                    Kd(-1.0e3));
                {
                    const double top = cellParams.lightTableStartDb + cellParams.lightTableStepDb * (cellParams.lightTableSize - 1);
                    fade = fade * vclamp(Kd(1.0) - (referenceDb - Kd(top)) * Kd(1.0 / 6.0), Kd(0.0), Kd(1.0));
                }
                w = Kd(1.0) + fade * (w - Kd(1.0));
                D prior = Kd(1.0);
                if (emitPriorOn && !L::all(beyond))
                {
                    const D lookedUp = emitPriorT<L>(referenceDb);
                    prior = L::sel(beyond, prior, lookedUp);
                }
                // An idle lane takes the values the skip leaves (what its zero light multiplies must be finite).
                emitWeight = L::sel(shapeIdle, Kd(1.0), w);
                emitPriorValue = L::sel(shapeIdle, Kd(1.0), prior);
                emitFadeValue = L::sel(shapeIdle, Kd(0.0), fade);
            }
        }
        D estimatedFrequencyHz = Kd(1000.0);
        if (cellParams.elFreqExponent != 0.0f && cellParams.elRevHysteresis > 0.0f)
        {
            // EL light is emitted at polarity reversals: the last completed half-cycle between significant
            // reversals (Schmitt trigger at +-h x peak |s|) sets (f / 1 kHz)^kappa, held to the next reversal.
            const D magnitude = vabs(s);
            D reversalPeak = get<L>(ch, &CellState::reversalPeak);
            reversalPeak = L::sel(magnitude > reversalPeak, magnitude, reversalPeak * Kd(reversalRelease));
            put<L>(ch, &CellState::reversalPeak, reversalPeak);
            const D threshold = Kd(static_cast<double>(cellParams.elRevHysteresis)) * reversalPeak;
            I reversalCount = get<L>(ch, &CellState::reversalCount) + L::i(1);
            I reversalSign = get<L>(ch, &CellState::reversalSign);
            D reversalPhi = get<L>(ch, &CellState::reversalPhi);
            const auto flip = ((reversalSign > L::i(0)) && (s < -threshold)) || ((reversalSign < L::i(0)) && (s > threshold));
            if (L::any(flip))
            {
                const double exponent = static_cast<double>(cellParams.elFreqExponent);
                const D phi = L::map([this, exponent](double count) noexcept {
                    return std::pow(reversalHalfCycle1k / count, exponent); }, L::fromI(reversalCount));
                reversalSign = L::sel(flip, -reversalSign, reversalSign);
                reversalPhi = L::sel(flip, phi, reversalPhi);
                reversalCount = L::sel(flip, L::i(0), reversalCount);
            }
            put<L>(ch, &CellState::reversalCount, reversalCount);
            put<L>(ch, &CellState::reversalSign, reversalSign);
            put<L>(ch, &CellState::reversalPhi, reversalPhi);
            light = light * reversalPhi;
        }
        else if (cellParams.elFreqExponent != 0.0f)
        {
            // EL brightness frequency law (AC electroluminescence, light ~ f^kappa at a given drive amplitude):
            // the drive's slew-to-level mean-square ratio estimates (f / 1 kHz)^2, exactly 1 for a settled 1 kHz sine.
            D slew = (s - get<L>(ch, &CellState::drivePrev)) * Kd(slewNormalisation);
            const D rawSlew = slew;   // ahead of the corner: (f / 1 kHz)^2 against the level for a settled sine
            if (cellParams.elFreqCorner > 0.0f)
            {   // EL brightness flattens above the corner: low-pass the slew before its mean square
                D slewLow = get<L>(ch, &CellState::slewLow);
                slewLow = slewLow + Kd(slewLowPassStep) * (slew - slewLow);
                put<L>(ch, &CellState::slewLow, slewLow);
                slew = slewLow * Kd(slewLowPassNormalisation);
            }
            put<L>(ch, &CellState::drivePrev, s);
            D drivePower = get<L>(ch, &CellState::drivePower);
            D slewPower = get<L>(ch, &CellState::slewPower);
            drivePower = drivePower + Kd(elFreqStep) * (s * s - drivePower);
            slewPower = slewPower + Kd(elFreqStep) * (slew * slew - slewPower);
            put<L>(ch, &CellState::drivePower, drivePower);
            put<L>(ch, &CellState::slewPower, slewPower);
            const D ratio = vclamp(slewPower / vmax(drivePower, Kd(1.0e-30)), Kd(1.0e-4), Kd(1.0e4));
            estimatedFrequencyHz = Kd(1000.0) * vsqrt(ratio);
            const double exponent = 0.5 * static_cast<double>(cellParams.elFreqExponent);
            // ratio^exponent on its third-order expansion in u = ratio / reference - 1 about the last exact
            // evaluation, redone (per lane) once |u| passes kFreqLawTolerance: relative error at most
            // |e (e - 1)(e - 2)(e - 3)| / 24 |u|^4: 3e-6 for the shipped e = 0.18 at the 10 % tolerance (the second
            // order it replaced allowed 6e-6 at 5 %).
            // A lane whose light is exactly zero takes nothing from the law or the plateau boost below (they
            // multiply the zero light): its expansion is not refreshed, and its light stays the zero it is.
            const auto dark = light == Kd(0.0);
            D lawRatio = get<L>(ch, &CellState::freqLawRatio);
            D lawInverse = get<L>(ch, &CellState::freqLawInverse);
            D lawValue = get<L>(ch, &CellState::freqLawValue);
            D drift = (ratio - lawRatio) * lawInverse;
            const auto lawStale = !(vabs(drift) <= Kd(kFreqLawTolerance)) && !dark;
            if (L::any(lawStale))
            {
                lawRatio = L::sel(lawStale, ratio, lawRatio);
                lawInverse = L::sel(lawStale, Kd(1.0) / ratio, lawInverse);
                lawValue = L::sel(lawStale, lanes::lanemath::pow(ratio, exponent), lawValue);
                drift = L::sel(lawStale, Kd(0.0), drift);
                put<L>(ch, &CellState::freqLawRatio, lawRatio);
                put<L>(ch, &CellState::freqLawInverse, lawInverse);
                put<L>(ch, &CellState::freqLawValue, lawValue);
            }
            light = L::sel(dark, light,
                light * (lawValue * (Kd(1.0) + drift * (Kd(exponent) + drift * (Kd(0.5 * exponent * (exponent - 1.0))
                    + drift * Kd(exponent * (exponent - 1.0) * (exponent - 2.0) / 6.0))))));
            if (cellParams.elSatBoost > 0.0f)
            {   // The plateau of the light table is higher for a drive faster than 1 kHz (see prepareLight's notes).
                const double satStep = std::min(dt / 0.005, 1.0);
                D satSlew = get<L>(ch, &CellState::satSlew);
                D satRaw = get<L>(ch, &CellState::satRaw);
                D satDrive = get<L>(ch, &CellState::satDrive);
                satSlew = satSlew + Kd(satStep) * (slew * slew - satSlew);
                satRaw = satRaw + Kd(satStep) * (rawSlew * rawSlew - satRaw);
                satDrive = satDrive + Kd(satStep) * (s * s - satDrive);
                satSlew = L::sel(satSlew < Kd(1.0e-30), Kd(0.0), satSlew);     // no denormal tail in silence
                satRaw = L::sel(satRaw < Kd(1.0e-30), Kd(0.0), satRaw);
                satDrive = L::sel(satDrive < Kd(1.0e-30), Kd(0.0), satDrive);
                put<L>(ch, &CellState::satSlew, satSlew);
                put<L>(ch, &CellState::satRaw, satRaw);
                put<L>(ch, &CellState::satDrive, satDrive);
                const D level = vmax(satDrive, Kd(1.0e-30));
                const double cornerKHz = cellParams.elFreqCorner > 0.0f ? 1.0e-3 * static_cast<double>(cellParams.elFreqCorner) : 1.0e3;
                const D fast = vmin(satSlew / level, Kd(1.0 + cornerKHz * cornerKHz))
                    - Kd(1.0) - Kd(static_cast<double>(cellParams.elSatMargin));
                const auto lit = (fast > Kd(0.0)) && (el > Kd(1.0e-30)) && !dark;
                if (L::any(lit))
                {
                    const D above = (Kd(8.685889638065035) * L::map([](double v) noexcept { return std::log(v); }, el)
                                     - Kd(static_cast<double>(cellParams.elSatKneeDb)))
                        / Kd(std::max(static_cast<double>(cellParams.elSatSpanDb), 1.0e-6));
                    const double lo = static_cast<double>(cellParams.elSatFadeLoKHz) * cellParams.elSatFadeLoKHz;
                    const double hi = static_cast<double>(cellParams.elSatFadeHiKHz) * cellParams.elSatFadeHiKHz;
                    const D fadeSat = hi > lo ? vclamp((Kd(hi) - satRaw / level) / Kd(hi - lo), Kd(0.0), Kd(1.0)) : Kd(1.0);
                    const auto boost = lit && (above > Kd(0.0)) && (fadeSat > Kd(0.0));
                    const D w = L::sel(above >= Kd(1.0), Kd(1.0), above * above * (Kd(3.0) - Kd(2.0) * above));
                    light = L::sel(boost,
                        light * (Kd(1.0) + Kd(static_cast<double>(cellParams.elSatBoost)) * fast * w * fadeSat), light);
                }
            }
        }
        put<L>(ch, &CellState::estimatedFrequencyHz, estimatedFrequencyHz);
        FrameT<L> frame{light, lawLight, s, floorDrive, floorPower, inputPower, inert};
        frame.emitWeight = emitWeight;
        frame.emitPrior = emitPriorValue;
        frame.emitFade = emitFadeValue;
        return frame;
    }

    // The panel's emission: the light the channel's drive asks for (prepareLight), through the panel's charge
    // stores, which the drive's polarity picks.
    LightFrame emitLight(int ch, LightFrame frame) noexcept { return emitLightT<lanes::Lane1>(ch, frame); }

    // emitLight() over a lane policy (see prepareLightT): Lane2 runs both channels of a linked pair, each on its
    // own drive's polarity and its own stores.
    template <class L>
    FrameT<L> emitLightT(int ch, FrameT<L> frame) noexcept
    {
        emitLightRef<L>(ch, frame);
        return frame;
    }
    // emitLightT() on the caller's frame (no frame copies on the pair's per-sample path).
    template <class L>
    OPTO_FORCE_INLINE void emitLightRef(int ch, FrameT<L>& frame) noexcept
    {
        using D = typename L::D;
        using I = typename L::I;
        using lanes::vmax; using lanes::vmin; using lanes::vabs; using lanes::visfinite;
        const auto Kd = [](double x) noexcept { return L::d(x); };
        D light = frame.light;
        // The drive's polarity: side 1 (the negative store, and the shape's cycle) where !(s >= 0).
        const auto negative = !(frame.s >= Kd(0.0));
        if (cellParams.elPolCap > 0.0f && cellParams.elPolRefill > 0.0f)
        {
            // The panel's charge: two stores, one per polarity (see the notes at emitLight's head).
            const double leak = static_cast<double>(cellParams.elPolLeak);
            const D law = vmax(light, Kd(0.0));
            D cap = Kd(static_cast<double>(cellParams.elPolCap));
            if (cellParams.elPolCapExp != 0.0f && leak > 0.0)
            {
                const auto grows = law > Kd(leak);
                if (L::any(grows))
                {
                    const double exponent = static_cast<double>(cellParams.elPolCapExp);
                    const D scaled = cap * lanes::lanemath::pow(L::sel(grows, law / Kd(leak), Kd(1.0)), exponent);
                    cap = L::sel(grows, scaled, cap);
                }
            }
            const D fullCredit = Kd(-static_cast<double>(cellParams.elPolCredit)) * cap;
            D spent0 = getAt<L>(ch, &CellState::chargeSpent, 0);
            D spent1 = getAt<L>(ch, &CellState::chargeSpent, 1);
            D other = L::sel(negative, spent0, spent1);
            other = L::sel(other > Kd(0.0), other * Kd(chargeRefillKeep), other);
            const auto refills = other > fullCredit;
            if (L::any(refills))
            {
                const D lit = leak > 0.0 ? law / (law + Kd(leak)) : L::sel(law > Kd(0.0), Kd(1.0), Kd(0.0));
                const D exponent = Kd(-chargeRefillRate) * lit;
                const D refilled = fullCredit + (other - fullCredit)
                    * L::sel(exponent == Kd(0.0), Kd(1.0), lanes::lanemath::exp(exponent));
                other = L::sel(refills, refilled, other);
            }
            spent0 = L::sel(negative, other, spent0);
            spent1 = L::sel(negative, spent1, other);
            if (leak > 0.0)
            {
                const double drained = leak * dt;
                spent0 = L::sel(spent0 > Kd(0.0), vmax(spent0 - Kd(drained), Kd(0.0)), vmin(spent0 + Kd(drained), Kd(0.0)));
                spent1 = L::sel(spent1 > Kd(0.0), vmax(spent1 - Kd(drained), Kd(0.0)), vmin(spent1 + Kd(drained), Kd(0.0)));
            }
            D sideSpent = L::sel(negative, spent1, spent0);
            const D ahead = cap - sideSpent;
            const auto available = ahead > Kd(0.0);
            // The taper is integrated exactly over the sample for a constant light.
            const D taper = Kd(static_cast<double>(cellParams.elPolKnee)) * cap;
            const D wanted = law * Kd(dt);
            D moved = vmin(wanted, ahead);
            const auto untapered = !(taper > Kd(0.0)) || (ahead - wanted >= taper);
            if (L::any(available && !untapered))
            {
                const D intoTaper = (ahead - taper) + taper
                    * (Kd(1.0) - L::map([](double v) noexcept { return std::exp(v); }, -(wanted - (ahead - taper)) / taper));
                const D withinTaper = ahead
                    * (Kd(1.0) - L::map([](double v) noexcept { return std::exp(v); }, -wanted / taper));
                moved = L::sel(untapered, moved, L::sel(ahead > taper, intoTaper, withinTaper));
            }
            light = L::sel(available, moved * Kd(invDt), Kd(0.0));
            sideSpent = L::sel(available, sideSpent + moved, sideSpent);
            spent0 = L::sel(negative, spent0, sideSpent);
            spent1 = L::sel(negative, sideSpent, spent1);
            spent0 = L::sel(vabs(spent0) < Kd(1.0e-30), Kd(0.0), spent0);   // no denormal tail
            spent1 = L::sel(vabs(spent1) < Kd(1.0e-30), Kd(0.0), spent1);
            putAt<L>(ch, &CellState::chargeSpent, 0, spent0);
            putAt<L>(ch, &CellState::chargeSpent, 1, spent1);
        }
        frame.clockLight = light;   // the panel's emission without the shape below
        if (cellParams.emitPeakExp > 0.0f || cellParams.emitRiseGain != 0.0f)
        {
            // The shape (prepareLight) with its per-cycle charge conservation (see the notes at emitLight's head).
            D scale = get<L>(ch, &CellState::emitScale);
            D sumPlain = get<L>(ch, &CellState::emitSumPlain);
            D sumApplied = get<L>(ch, &CellState::emitSumApplied);
            D sumFade = get<L>(ch, &CellState::emitSumFade);
            if (cellParams.emitConserveOn != 0.0f)
            {
                const I sideNow = L::sel(negative, L::i(1), L::i(0));
                const I emitSide = get<L>(ch, &CellState::emitSide);
                const auto closing = !negative && (emitSide == L::i(1));
                if (L::any(closing))
                {
                    const D conserved = vclamp(scale * L::map([](double x, double y) noexcept { return lanes::lanemath::pow(x, y); },
                                                              sumPlain / sumApplied, sumFade / sumPlain), Kd(0.5), Kd(2.0));
                    D closed = L::sel(!(sumPlain > Kd(1.0e-30)), Kd(1.0),
                                      L::sel(sumApplied > Kd(1.0e-30), conserved, scale));
                    closed = L::sel(visfinite(closed), closed, Kd(1.0));
                    scale = L::sel(closing, closed, scale);
                    sumPlain = L::sel(closing, Kd(0.0), sumPlain);
                    sumApplied = L::sel(closing, Kd(0.0), sumApplied);
                    sumFade = L::sel(closing, Kd(0.0), sumFade);
                }
                put<L>(ch, &CellState::emitSide, sideNow);
                const auto silent = !(vabs(frame.s) > Kd(1.0e-30));   // digital silence: c relaxes to 1 (5 ms)
                scale = L::sel(silent, Kd(1.0) + (scale - Kd(1.0)) * Kd(emitLeak), scale);
                put<L>(ch, &CellState::emitScale, scale);
            }
            light = light * (frame.emitWeight * (Kd(1.0) + frame.emitFade * (frame.emitPrior * scale - Kd(1.0))));
            // The charge term's trapezoid: the mean of this and the last emission (see emitLight's notes).
            const D emitted = light;
            light = emitted - Kd(0.5) * frame.emitFade * (emitted - get<L>(ch, &CellState::emitLastEmitted));
            put<L>(ch, &CellState::emitLastEmitted, L::sel(visfinite(emitted), emitted, Kd(0.0)));
            if (cellParams.emitConserveOn != 0.0f)
            {   // the cycle's sums, on the clock the populations will run (advanceLight), the trapezoid included
                frame.scaledClockLight = frame.clockLight;
                frame.clockLightScale = timeScaleT<L>(frame.clockLight);
                frame.scaledLight = light;
                // Where the shape leaves the emission as it was (weight 1, no fade) the light is the clock's.
                const auto unshaped = light == frame.clockLight;
                frame.lightScale = L::all(unshaped) ? frame.clockLightScale
                    : L::sel(unshaped, frame.clockLightScale, timeScaleT<L>(light));
                const D plain = frame.clockLightScale * frame.clockLight;
                sumPlain = sumPlain + plain;
                sumApplied = sumApplied + frame.lightScale * light;
                sumFade = sumFade + frame.emitFade * plain;
                const auto bad = !visfinite(sumPlain) || !visfinite(sumApplied) || !visfinite(sumFade);
                sumPlain = L::sel(bad, Kd(0.0), sumPlain);   // a non-finite emission resets the cycle
                sumApplied = L::sel(bad, Kd(0.0), sumApplied);
                sumFade = L::sel(bad, Kd(0.0), sumFade);
                put<L>(ch, &CellState::emitSumPlain, sumPlain);
                put<L>(ch, &CellState::emitSumApplied, sumApplied);
                put<L>(ch, &CellState::emitSumFade, sumFade);
            }
        }
        frame.light = light;
    }

    // The cell behind the panel's emission (emitLight): populations, release, floor. withApplied = false (lab kernel
    // only) skips the applied-gain floor blend, which feeds no state; every state update is identical, and the returned
    // value is then the cell gain before the floor.
    template <bool withApplied = true>
    float advanceLight(int ch, const LightFrame& frame, bool limit) noexcept
    {
        return advanceLightT<lanes::Lane1, withApplied>(ch, frame, limit);
    }

    // advanceLight() over a lane policy (see prepareLightT): Lane2 advances both channels of a linked pair.
    template <class L, bool withApplied = true>
    OPTO_FORCE_INLINE typename L::F advanceLightT(int ch, const FrameT<L>& frame, bool limit) noexcept
    {
        using D = typename L::D;
        using F = typename L::F;
        using I = typename L::I;
        using lanes::vmax; using lanes::vmin; using lanes::vabs; using lanes::vsqrt;
        using lanes::visfinite;
        const auto Kd = [](double x) noexcept { return L::d(x); };
        const auto Kf = [](float x) noexcept { return L::f(x); };
        const auto Ki = [](int x) noexcept { return L::i(x); };
        const D light = frame.light;
        const D lawLight = frame.lawLight;
        const D floorDrive = frame.floorDrive;
        const bool inert = frame.inert;
        D floorEl = get<L>(ch, &CellState::floorEl);
        floorEl = floorEl + Kd(elStep) * (floorDrive - floorEl);
        put<L>(ch, &CellState::floorEl, floorEl);
        D floorLight = inert ? Kd(0.0) : lightForT<L>(floorEl);
        if (cellParams.lightCeiling > 0.0f)
            floorLight = floorLight / (Kd(1.0) + floorLight / Kd(static_cast<double>(cellParams.lightCeiling)));
        // The floor's input history (see advanceLight's notes): a hold, then two dark stages.
        const double floorThreshold = std::max(static_cast<double>(cellParams.darkLight), 1.0e-30);
        I floorInputHold = get<L>(ch, &CellState::floorInputHold);
        const auto floorLit = floorLight >= Kd(floorThreshold);
        floorInputHold = L::sel(floorLit, Ki(darkHoldSamples),
                                L::sel(floorInputHold > Ki(0), floorInputHold - Ki(1), floorInputHold));
        put<L>(ch, &CellState::floorInputHold, floorInputHold);
        // While the input hold runs (the floor lit within darkHoldSamples) the target is 0 without its quotient.
        const auto held = floorInputHold > Ki(0);
        const D floorTarget = L::all(held) ? Kd(0.0) : L::sel(held, Kd(0.0),
                                     Kd(floorThreshold) / (vmax(floorLight, Kd(0.0)) + Kd(floorThreshold)));
        D floorDark0 = getAt<L>(ch, &CellState::floorDark, 0);
        D floorDark1 = getAt<L>(ch, &CellState::floorDark, 1);
        floorDark0 = L::sel(floorTarget < floorDark0, floorTarget, floorDark0 + Kd(darkStep) * (floorTarget - floorDark0));
        floorDark1 = L::sel(floorDark0 < floorDark1, floorDark0, floorDark1 + Kd(darkStep) * (floorDark0 - floorDark1));
        putAt<L>(ch, &CellState::floorDark, 0, floorDark0);
        putAt<L>(ch, &CellState::floorDark, 1, floorDark1);
        // The populations' clocks (emitLight evaluated them already unless the link replaced the light).
        const auto fresh = light == frame.scaledLight;
        const D step = L::all(fresh) ? frame.lightScale : L::sel(fresh, frame.lightScale, timeScaleT<L>(light));
        const bool shapedEmission = cellParams.emitPeakExp > 0.0f || cellParams.emitRiseGain != 0.0f;
        D stepRelease = step;
        if (shapedEmission)
        {
            const auto freshClock = frame.clockLight == frame.scaledClockLight;
            stepRelease = L::all(freshClock) ? frame.clockLightScale
                : L::sel(freshClock, frame.clockLightScale, timeScaleT<L>(frame.clockLight));
        }
        D grRateSmooth = get<L>(ch, &CellState::grRateSmooth);
        D rateScale = Kd(1.0);
        if (cellParams.attackRateDbPerS > 0.0f)
        {
            const auto rising = grRateSmooth > Kd(0.0);
            if (L::any(rising))
            {
                const double power = static_cast<double>(cellParams.attackRatePower);
                const D limited = Kd(1.0) / (Kd(1.0) + L::map([power](double r) noexcept { return std::pow(r, power); },
                    grRateSmooth / Kd(static_cast<double>(cellParams.attackRateDbPerS))));
                rateScale = L::sel(rising, limited, rateScale);
            }
        }
        const D releaseBase = cellParams.hChargeOnly != 0.0f ? Kd(dt) : stepRelease;
        D population[kPopulations];
        for (size_t k = 0; k < kPopulations; ++k) population[k] = getAt<L>(ch, &CellState::population, k);
        D trap = get<L>(ch, &CellState::trap);
        D dark0 = getAt<L>(ch, &CellState::dark, 0);
        D dark1 = getAt<L>(ch, &CellState::dark, 1);
        D darkScale = Kd(1.0);
        if (cellParams.darkRateCeiling > 0.0f || cellParams.darkFloorTau > 0.0f)
        {
            if (cellParams.inputCouplingOn == 0.0f)
            {   // the drive's own dark stages; with the input coupling the input's (floorDark) own release instead
                const D law = vmax(lawLight, Kd(0.0));
                const double darkLight = std::max(static_cast<double>(cellParams.darkLight), 1.0e-30);
                I darkHold = get<L>(ch, &CellState::darkHold);
                darkHold = L::sel(law >= Kd(darkLight), Ki(darkHoldSamples),
                                  L::sel(darkHold > Ki(0), darkHold - Ki(1), darkHold));
                put<L>(ch, &CellState::darkHold, darkHold);
                const D target = L::sel(darkHold > Ki(0), Kd(0.0), Kd(darkLight) / (law + Kd(darkLight)));
                dark0 = L::sel(target < dark0, target, dark0 + Kd(darkStep) * (target - dark0));
                dark1 = L::sel(dark0 < dark1, dark0, dark1 + Kd(darkStep) * (dark0 - dark1));
                putAt<L>(ch, &CellState::dark, 0, dark0);
                putAt<L>(ch, &CellState::dark, 1, dark1);
            }
            const D releaseDark = cellParams.inputCouplingOn != 0.0f ? floorDark1 : dark1;
            // The ceiling slows only a lane in the dark (releaseDark > 0): none is, none is slowed, without the loss.
            if (cellParams.darkRateCeiling > 0.0f && L::any(releaseDark > Kd(0.0)))
            {
                const auto& g = population;
                D loss = g[0] * (Kd(cellParams.fastRelease) + Kd(cellParams.fastBimolecular) * g[0])
                    + g[1] * (Kd(cellParams.slowRelease) + Kd(cellParams.slowBimolecular) * g[1]);
                if (cellParams.memoryCharge > 0.0f) loss = loss + g[2] * Kd(cellParams.memoryRelease);
                if (cellParams.flashCharge > 0.0f)
                    loss = loss + g[3] * (Kd(cellParams.flashRelease) + Kd(cellParams.flashBimolecular) * g[3]);
                loss = loss * (releaseBase * Kd(invDt));
                if (cellParams.trapCapture > 0.0f)
                {
                    const D fill = cellParams.trapCapacity > 0.0f
                        ? vmax(Kd(0.0), Kd(1.0) - trap * Kd(invTrapCapacity)) : Kd(1.0);
                    loss = loss + (Kd(static_cast<double>(cellParams.trapCapture)) * fill * g[1]
                                   - Kd(static_cast<double>(cellParams.trapEmit)) * trap);
                }
                const D conductanceNow = g[0] + g[1] + g[2] + g[3];
                const auto slows = (releaseDark > Kd(0.0)) && (loss > Kd(0.0)) && (conductanceNow > Kd(1.0e-30));
                if (L::any(slows))
                {
                    const D ratio = loss / (conductanceNow * Kd(static_cast<double>(cellParams.darkRateCeiling)));
                    const D square = ratio * ratio;
                    const D filled = cellParams.trapCapture > 0.0f && cellParams.trapCapacity > 0.0f
                        ? vclamp(trap * Kd(invTrapCapacity), Kd(0.0), Kd(1.0)) : Kd(1.0);
                    const double fillPower = static_cast<double>(cellParams.darkTrapFillPower);
                    const D ownedFill = cellParams.darkTrapFillPower == 2.0f ? releaseDark * filled * filled
                        : releaseDark * expandedPow<L, double>(ch, filled, fillPower, &CellState::fillAt,
                                                               &CellState::fillInverse, &CellState::fillValue);
                    const D slowed = Kd(1.0) - ownedFill * (Kd(1.0) - Kd(1.0) / vsqrt(vsqrt(Kd(1.0) + square * square)));
                    darkScale = L::sel(slows, slowed, darkScale);
                }
            }
        }
        const D historyWeight = cellParams.inputCouplingOn != 0.0f ? floorDark1 : dark1;
        if (cellParams.darkHistoryStrength > 0.0f
            && (cellParams.darkRateCeiling > 0.0f || cellParams.darkFloorTau > 0.0f))
        {
            const D density = population[0] + population[1] + population[2] + population[3];
            const auto weighted = (historyWeight > Kd(0.0)) && (density > Kd(1.0e-30));
            if (L::any(weighted))
            {
                const D reduction = Kd(8.685889638065035) * expandedLog<L>(ch, density, 1.0, &CellState::densityAt,
                                                                           &CellState::densityInverse,
                                                                           &CellState::densityLog);
                const auto inRange = weighted && (reduction < Kd(24.0));
                if (L::any(inRange))
                {
                    const auto tent = [&reduction, &Kd](double left, double peak, double right) noexcept {
                        return vmax(Kd(0.0), vmin((reduction - Kd(left)) * Kd(1.0 / (peak - left)),
                                                  (Kd(right) - reduction) * Kd(1.0 / (right - peak))));
                    };
                    const std::array<double, 7> p{{cellParams.darkHistoryBase, cellParams.darkHistoryMemory,
                        cellParams.darkHistoryShoulder, cellParams.darkHistoryShoulderMemory,
                        cellParams.darkHistoryLow, cellParams.darkHistoryLowMemory, cellParams.darkHistoryDensity}};
                    const D history = population[2] / (population[2] + Kd(std::max(p[6], 1.0e-30)) * density);
                    const D correction = (Kd(p[0]) + Kd(p[1]) * history) * tent(2.5, 6.5, 13.0)
                        + (Kd(p[2]) + Kd(p[3]) * history) * tent(9.0, 16.0, 24.0)
                        - ((Kd(1.0) - history) * Kd(p[4]) + history * Kd(p[5])) * tent(0.0, 1.5, 2.5);
                    darkScale = L::sel(inRange,
                        darkScale * expandedExp<L>(ch, historyWeight * Kd(static_cast<double>(cellParams.darkHistoryStrength))
                                                           * correction, &CellState::historyAt, &CellState::historyExp),
                        darkScale);
                }
            }
        }
        const D chargeStep = step * rateScale * darkScale;
        const D releaseStep = releaseBase * darkScale;
        const D trapStep = Kd(dt) * darkScale;
        population[0] = (population[0] + chargeStep * light)
            / (Kd(1.0) + releaseStep * (Kd(cellParams.fastRelease) + Kd(cellParams.fastBimolecular) * population[0]));
        population[1] = (population[1] + chargeStep * Kd(cellParams.slowCharge) * light)
            / (Kd(1.0) + releaseStep * (Kd(cellParams.slowRelease) + Kd(cellParams.slowBimolecular) * population[1]));
        if (cellParams.memoryCharge > 0.0f)
            population[2] = (population[2] + chargeStep * Kd(cellParams.memoryCharge) * light)
                / (Kd(1.0) + releaseStep * Kd(cellParams.memoryRelease));
        if (cellParams.flashCharge > 0.0f)
            population[3] = (population[3] + chargeStep * Kd(cellParams.flashCharge) * light)
                / (Kd(1.0) + releaseStep * (Kd(cellParams.flashRelease) + Kd(cellParams.flashBimolecular) * population[3]));
        if (cellParams.trapCapture > 0.0f)
        {
            const D fill = cellParams.trapCapacity > 0.0f
                ? vmax(Kd(0.0), Kd(1.0) - trap * Kd(invTrapCapacity)) : Kd(1.0);
            const D capture = Kd(static_cast<double>(cellParams.trapCapture)) * fill * population[1] * trapStep;
            const D emit = Kd(static_cast<double>(cellParams.trapEmit)) * trap * trapStep;
            population[1] = vmax(Kd(0.0), population[1] - capture + emit);
            trap = vmax(Kd(0.0), trap + capture - emit);
        }
        put<L>(ch, &CellState::trap, trap);
        D conductance = Kd(0.0);
        for (size_t k = 0; k < kPopulations; ++k)
        {
            population[k] = L::sel(population[k] < Kd(1.0e-30), Kd(0.0), population[k]);   // no denormal tails
            conductance = conductance + population[k];
            putAt<L>(ch, &CellState::population, k, population[k]);
        }
        if (cellParams.conductanceTableOn != 0.0f)   // split static Psi
            conductance = conductanceForT<L>(conductance);
        const D cellGain = Kd(1.0) / (Kd(1.0) + conductance);
        put<L>(ch, &CellState::cellGain, cellGain);
        {
            const D newGr = Kd(8.685889638065035) * expandedLog<L>(ch, conductance, 1.0, &CellState::grAt,
                                                                   &CellState::grInverse, &CellState::grLog);
            if (cellParams.attackRateDbPerS > 0.0f)
                grRateSmooth = grRateSmooth + Kd(attackRateStep)
                    * ((newGr - get<L>(ch, &CellState::grDb)) / Kd(dt) - grRateSmooth);
            put<L>(ch, &CellState::grRateSmooth, grRateSmooth);
            put<L>(ch, &CellState::grDb, newGr);
        }

        F applied = L::toF(cellGain);
        if (!withApplied) return applied;
        if (cellParams.lowFrequencyFloor != 0.0f)
        {
            // g' = (g^e + (1 - g^e) q)^(1/e) with q = min(1, k share^p)^(e/2) = min(1, k^(e/2) share^(p e/2)). Each
            // power runs on its second-order expansion (expandedPow); the expansions belong to one mode's exponents.
            if (static_cast<int>(limit) != floorExpansionMode)
            {
                state.floorShareInverse = state.floorGainInverse = state.floorInnerInverse = OptoPair<float>{kStaleInverse};
                floorExpansionMode = static_cast<int>(limit);
            }
            const F floorShare = frame.floorPower / vmax(frame.inputPower, Kf(1.0e-12f));
            const float floorExponent = limit ? floorLaw[4] : floorLaw[3];
            const F floorRatioPower = vmin(Kf(limit ? floorLimitRatioScale : 1.0f)
                * expandedPow<L, float>(ch, floorShare, floorLaw[2] * 0.5f * floorExponent, &CellState::floorShareAt,
                                 &CellState::floorShareInverse, &CellState::floorShareValue), Kf(1.0f));
            const F gainPower = expandedPow<L, float>(ch, applied, floorExponent, &CellState::floorGainAt,
                                               &CellState::floorGainInverse, &CellState::floorGainValue);
            applied = expandedPow<L, float>(ch, gainPower + (Kf(1.0f) - gainPower) * floorRatioPower, 1.0f / floorExponent,
                                     &CellState::floorInnerAt, &CellState::floorInnerInverse,
                                     &CellState::floorInnerValue);
            applied = L::sel(visfinite(L::toD(applied)), applied, Kf(1.0f));
            if (cellParams.darkFloorTau > 0.0f)
            {
                // The share of the conductance the floor passes, for a gain and the floor's gain behind it.
                const auto passedOf = [&Kd](D gain, D floorGain) noexcept {
                    const D conductance = Kd(1.0) / vmax(gain, Kd(1.0e-12)) - Kd(1.0);
                    return L::sel(conductance > Kd(1.0e-12),
                        vclamp((Kd(1.0) / floorGain - Kd(1.0)) / conductance, Kd(0.0), Kd(1.0)), Kd(1.0));
                };
                const D dark = cellParams.inputCouplingOn != 0.0f ? floorDark1 : dark1;
                const auto darkened = dark > Kd(0.0);
                if (!L::any(darkened))
                {   // floorPassed is passedOf(cellGain, applied): kept as its two inputs until a dark step reads it
                    put<L>(ch, &CellState::floorPassedGain, cellGain);
                    put<L>(ch, &CellState::floorPassedFloorGain, L::toD(applied));
                    put<L>(ch, &CellState::floorPassedPending, L::i(1));
                }
                else
                {
                    const D cellConductance = conductance;
                    const D floorGain = L::toD(applied);
                    const D passed = L::sel(cellConductance > Kd(1.0e-12),
                        vclamp((Kd(1.0) - floorGain) / (floorGain * cellConductance), Kd(0.0), Kd(1.0)), Kd(1.0));
                    D floorPassed = get<L>(ch, &CellState::floorPassed);
                    const auto pending = get<L>(ch, &CellState::floorPassedPending) > L::i(0);
                    if (L::any(pending))
                        floorPassed = L::sel(pending, passedOf(get<L>(ch, &CellState::floorPassedGain),
                                                               get<L>(ch, &CellState::floorPassedFloorGain)), floorPassed);
                    floorPassed = L::sel(darkened,
                        floorPassed + ((Kd(1.0) - dark) * (passed - floorPassed) + dark * Kd(darkFloorStep) * (Kd(1.0) - floorPassed)),
                        passed);
                    put<L>(ch, &CellState::floorPassed, floorPassed);
                    put<L>(ch, &CellState::floorPassedPending, L::i(0));
                    applied = L::sel(darkened, L::toF(Kd(1.0) / (Kd(1.0) + floorPassed * cellConductance)), applied);
                }
            }
        }
        put<L>(ch, &CellState::gain, applied);
        if constexpr (L::kLanes == 1) { OPTO_LAB_PROBE(ch, applied, cellGain); }
#if defined(__aarch64__)
        else
        {
            OPTO_LAB_PROBE(0, L::laneF(applied, 0), L::lane(cellGain, 0));
            OPTO_LAB_PROBE(1, L::laneF(applied, 1), L::lane(cellGain, 1));
        }
#endif
        return applied;
    }

    // One channel on its own: mono, or a pair with no link.
    template <bool withApplied = true>
    float process(int ch, float input, float compressed, float sidechain,
                  bool external, float optoDetector, bool useOptoDetector,
                  bool limit, float peakReduction) noexcept
    {
        return advanceLight<withApplied>(ch, emitLight(ch, prepareLight(ch, input, compressed,
            sidechain, external, optoDetector, useOptoDetector, limit, peakReduction)), limit);
    }

    bool linked() const noexcept { return linkWeight > 0.0f; }

    // What a linked cell is lit by: the larger of its own panel's emission and its partner's, the partner's scaled
    // by the link amount. One law for every link amount: at 0 a cell sees only its own panel, at 1 both cells see
    // the same light and so stay alike. The floor's band power is shared in proportion (half each at a full link);
    // its input power takes the larger, with the link amount as an amplitude weight.
    LightFrame linkedFrame(const LightFrame& own, const LightFrame& partner) const noexcept
    {
        const double w = static_cast<double>(linkWeight);
        LightFrame frame = own;
        frame.light = std::max(own.light, w * partner.light);
        frame.lawLight = std::max(own.lawLight, w * partner.lawLight);
        frame.clockLight = std::max(own.clockLight, w * partner.clockLight);
        frame.floorDrive = std::max(own.floorDrive, w * partner.floorDrive);
        frame.floorPower = own.floorPower + 0.5f * linkWeight * (partner.floorPower - own.floorPower);
        frame.inputPower = std::max(own.inputPower, linkWeight * linkWeight * partner.inputPower);
        return frame;
    }

    // A linked stereo pair. Each channel has its own panel: its own filters, frequency law and charge stores, so
    // each emits at its own drive's polarity changes. The two emissions meet in a diode-OR (linkedFrame) and
    // everything behind that point (populations, release, floor) advances from the linked frame. Returns the
    // gains to apply to this sample, as process() leaves them.
    std::array<float, 2> processLightPair(const std::array<float, 2>& input,
                                          const std::array<float, 2>& sidechain, bool external,
                                          const std::array<float, 2>& detector, bool limit,
                                          float peakReduction) noexcept
    {
        const std::array<float, 2> applied{{state.gain[0], state.gain[1]}};
#if defined(__aarch64__)
        // The whole step with the two channels in the lanes of one register: each lane performs exactly the
        // single-channel arithmetic, and the diode-OR reads the partner from the other lane.
        using P = lanes::Lane2;
        FrameT<P> frame = prepareLightT<P>(0, P::f(input[0], input[1]), P::f(sidechain[0], sidechain[1]),
                                           external, P::f(detector[0], detector[1]), true, limit, peakReduction);
        emitLightRef<P>(0, frame);
        linkFrameT<P>(frame);
        // The floor's input-power history is shared in the same proportion before its next update.
        put<P>(0, &CellState::inputPower, frame.inputPower);
        advanceLightT<P>(0, frame, limit);
#else
        const auto ownLeft = emitLight(0, prepareLight(0, input[0], 0.0f, sidechain[0], external, detector[0], true,
                                                       limit, peakReduction));
        const auto ownRight = emitLight(1, prepareLight(1, input[1], 0.0f, sidechain[1], external, detector[1], true,
                                                        limit, peakReduction));
        const auto frameLeft = linkedFrame(ownLeft, ownRight);
        const auto frameRight = linkedFrame(ownRight, ownLeft);
        // The floor's input-power history is shared in the same proportion before its next update.
        state.inputPower[0] = frameLeft.inputPower;
        state.inputPower[1] = frameRight.inputPower;
        advanceLight(0, frameLeft, limit);
        advanceLight(1, frameRight, limit);
#endif
        return applied;
    }

#if defined(__aarch64__)
    // linkedFrame() for both channels of a pair at once, in place: lane k's partner is the other lane.
    template <class L>
    void linkFrameT(FrameT<L>& frame) const noexcept
    {
        using lanes::vmax;
        const auto w = L::d(static_cast<double>(linkWeight));
        frame.light = vmax(frame.light, w * L::swap(frame.light));
        frame.lawLight = vmax(frame.lawLight, w * L::swap(frame.lawLight));
        frame.clockLight = vmax(frame.clockLight, w * L::swap(frame.clockLight));
        frame.floorDrive = vmax(frame.floorDrive, w * L::swap(frame.floorDrive));
        frame.floorPower = frame.floorPower + L::f(0.5f * linkWeight) * (L::swap(frame.floorPower) - frame.floorPower);
        frame.inputPower = vmax(frame.inputPower, L::f(linkWeight * linkWeight) * L::swap(frame.inputPower));
        // timeScale() depends on the light alone: a lane whose light is now exactly the light its partner's
        // step was evaluated at (a full link takes the partner's light unscaled) takes that step too, so
        // advanceLight finds it fresh. A NaN (nothing evaluated) never compares equal.
        const auto lightTaken = frame.light == L::swap(frame.scaledLight);
        frame.lightScale = L::sel(lightTaken, L::swap(frame.lightScale), frame.lightScale);
        frame.scaledLight = L::sel(lightTaken, frame.light, frame.scaledLight);
        const auto clockTaken = frame.clockLight == L::swap(frame.scaledClockLight);
        frame.clockLightScale = L::sel(clockTaken, L::swap(frame.clockLightScale), frame.clockLightScale);
        frame.scaledClockLight = L::sel(clockTaken, frame.clockLight, frame.scaledClockLight);
    }
#endif

    float gain(int ch) const noexcept { return state.gain[static_cast<size_t>(ch)]; }
    // What a reduction meter reads: the gain times the share of the input's own component the input element
    // leaves. That share is 1 - <sample x term> / <sample x sample>, both running means over about 50 ms: for
    // a sine, the element's loss at the fundamental. Exactly gain() while the element is off or is its even
    // part alone (pAsym 0), which takes nothing from that component. Display only: nothing in the audio path
    // reads it.
    float meterGain(int ch) const noexcept
    {
        const size_t c = static_cast<size_t>(ch);
        if (!(state.elementPower[c] > 1.0e-18)) return state.gain[c];
        return state.gain[c] * static_cast<float>(1.0 - std::clamp(state.elementTaken[c] / state.elementPower[c], 0.0, 1.0));
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
        const size_t c = static_cast<size_t>(ch);
        updateElementStrength(peakReduction);
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
        // At pAsym = 1 the lower side's weight is 0: its term is +-0 and never changes the sum, so it is skipped.
        const auto kinkIntegral = [&overIntegral, up, down](double v) noexcept {
            return down != 0.0 ? up * overIntegral(v) - down * overIntegral(-v) : up * overIntegral(v);
        };
        const double last = state.elementInput[c];
        const double step = x - last;
        const double middle = 0.5 * (x + last);
        // The kink's mean over the line between the two samples; at a step too small to divide by, its
        // value in the middle. Both ends are evaluated with the constants of this call, so a change of
        // constants between two samples (a lab does that) leaves no stale integral behind. With both ends
        // under the knee's lower edge on every side the kink has, both integrals (and the middle's value) are
        // +0, so the mean is the same signed zero without evaluating them.
        const double lowerEdge = threshold - 0.5 * knee;
        const bool below = x <= lowerEdge && last <= lowerEdge && (down == 0.0 || (-x <= lowerEdge && -last <= lowerEdge));
        const double kink = below ? (std::abs(step) > 1.0e-6 ? 0.0 / step : 0.0)
            : std::abs(step) > 1.0e-6
            ? (kinkIntegral(x) - kinkIntegral(last)) / step
            : (down != 0.0 ? up * over(middle) + down * over(-middle) : up * over(middle));
        state.elementInput[c] = x;
        const float z = cachedElementStrength * gain;
        const float term = static_cast<float>(0.5 * kink) * z / (1.0f + z);
        if (cellParams.pAsym != 0.0f)
        {   // For the meter (meterGain()): what the element takes of the input's own component, as running
            // means of sample x term and of sample x sample. The even part alone takes none of it.
            state.elementTaken[c] += elementMeterStep * (x * static_cast<double>(term) - state.elementTaken[c]);
            state.elementPower[c] += elementMeterStep * (x * x - state.elementPower[c]);
            if (state.elementPower[c] < 1.0e-30) state.elementTaken[c] = state.elementPower[c] = 0.0;   // no denormal tail
        }
        return term;
    }
    // elementTerm() for both channels of a pair at once (each lane its channel's arithmetic). The shipped element
    // (no knee, the lower side's weight 0) runs in the lanes; any other runs channel by channel.
    std::array<float, 2> elementTermPair(const std::array<float, 2>& input, const std::array<float, 2>& gain,
                                         float peakReduction) noexcept
    {
        if (!(cellParams.pStrength > 0.0f)) return {{0.0f, 0.0f}};
#if defined(__aarch64__)
        const double knee = std::max(static_cast<double>(cellParams.pKnee) * cellParams.pThresh, 0.0);
        if (knee == 0.0 && cellParams.pAsym == 1.0f)
        {
            using P = lanes::Lane2;
            using D = P::D;
            using F = P::F;
            using lanes::vabs;
            updateElementStrength(peakReduction);
            const D threshold = P::d(static_cast<double>(cellParams.pThresh));
            const D up = P::d(1.0 + static_cast<double>(cellParams.pAsym));
            const D zero = P::d(0.0);
            const D x = P::d(static_cast<double>(input[0]), static_cast<double>(input[1]));
            const D last = get<P>(0, &CellState::elementInput);
            const D step = x - last;
            const auto big = vabs(step) > P::d(1.0e-6);
            const auto below = x <= threshold && last <= threshold;
            D kink;
            if (P::all(below))
                kink = P::sel(big, zero / step, zero);
            else
            {   // over() and overIntegral() without a knee: (v - threshold), 0.5 (v - threshold)^2 + 0 at and above it
                const D dx = x - threshold, dl = last - threshold;
                const D kneeTerm = P::d(knee * knee / 24.0);
                const D integralX = P::sel(x >= threshold, P::d(0.5) * dx * dx + kneeTerm, zero);
                const D integralLast = P::sel(last >= threshold, P::d(0.5) * dl * dl + kneeTerm, zero);
                const D middle = P::d(0.5) * (x + last);
                const D overMiddle = P::sel(middle >= threshold, middle - threshold, zero);
                kink = P::sel(below, P::sel(big, zero / step, zero),
                              P::sel(big, (up * integralX - up * integralLast) / step, up * overMiddle));
            }
            put<P>(0, &CellState::elementInput, x);
            const F z = P::f(cachedElementStrength) * P::f(gain[0], gain[1]);
            const F term = P::toF(P::d(0.5) * kink) * z / (P::f(1.0f) + z);
            D taken = get<P>(0, &CellState::elementTaken), power = get<P>(0, &CellState::elementPower);
            taken = taken + P::d(elementMeterStep) * (x * P::toD(term) - taken);
            power = power + P::d(elementMeterStep) * (x * x - power);
            const auto tiny = power < P::d(1.0e-30);   // no denormal tail
            put<P>(0, &CellState::elementTaken, P::sel(tiny, zero, taken));
            put<P>(0, &CellState::elementPower, P::sel(tiny, zero, power));
            return {{P::laneF(term, 0), P::laneF(term, 1)}};
        }
#endif
        return {{elementTerm(0, input[0], gain[0], peakReduction), elementTerm(1, input[1], gain[1], peakReduction)}};
    }
    void updateElementStrength(float peakReduction) noexcept
    {
        const double prNorm = std::clamp(static_cast<double>(peakReduction) * 0.01, 0.0, 1.0);
        if (prNorm != cachedElementPr)
        {
            cachedElementPr = prNorm;
            const float pr = static_cast<float>(prNorm);
            cachedElementStrength = prNorm <= static_cast<double>(cellParams.prGate) ? 0.0f
                : cellParams.pStrength * optoElementStrength(pr)
                    * (cellParams.pPrExp != 0.0f ? std::pow(pr, cellParams.pPrExp) : 1.0f);
        }
    }
    float dynamicGrDb(int ch) const noexcept { return static_cast<float>(state.grDb[static_cast<size_t>(ch)]); }
    float estimatedFrequencyHz(int ch) const noexcept
    {
        return static_cast<float>(state.estimatedFrequencyHz[static_cast<size_t>(ch)]);
    }

    // The emission prior at a reference level (dB): linear between its entries, held beyond its ends. A pair reads
    // both lanes at once, each lane with the one-lane arithmetic.
    template <class L>
    OPTO_FORCE_INLINE typename L::D emitPriorT(typename L::D referenceDb) const noexcept
    {
        const double start = cellParams.lightTableStartDb;
        const int n = emitPriorSize;
        if constexpr (L::kLanes == 1)
        {
            const double position = (referenceDb - start) * lightTableInvStep;
            if (position <= 0.0) return emitPrior[0];
            if (position >= n - 1) return emitPrior[static_cast<size_t>(n - 1)];
            const int k = static_cast<int>(position);
            const double f = position - k;
            return emitPrior[static_cast<size_t>(k)] + f * (emitPrior[static_cast<size_t>(k + 1)] - emitPrior[static_cast<size_t>(k)]);
        }
#if defined(__aarch64__)
        else
        {
            using D = typename L::D;
            const D position = (referenceDb - L::d(start)) * L::d(lightTableInvStep);
            // The interior's entry k = int(position); a lane at or beyond an end reads a valid pair it does not use.
            const int64x2_t index = vcvtq_s64_f64(lanes::vmax(lanes::vmin(position, L::d(n - 2)), L::d(0.0)).v);
            const float64x2_t pair0 = vld1q_f64(emitPrior.data() + vgetq_lane_s64(index, 0));
            const float64x2_t pair1 = vld1q_f64(emitPrior.data() + vgetq_lane_s64(index, 1));
            const D low{vzip1q_f64(pair0, pair1)}, high{vzip2q_f64(pair0, pair1)};
            const D inner = low + (position - D{vcvtq_f64_s64(index)}) * (high - low);
            return L::sel(position <= L::d(0.0), L::d(emitPrior[0]),
                          L::sel(position >= L::d(n - 1), L::d(emitPrior[static_cast<size_t>(n - 1)]), inner));
        }
#endif
    }

    // The emission prior depends on the cell's parameters only, never on the rate, so it is built here (on
    // construction and setParams) and not in setRate: rebuilding it there cost about 2.9 ms on the audio
    // thread at every oversampling change.
    void buildEmitPrior() noexcept
    {
        emitPriorOn = false;
        if (cellParams.emitPeakExp > 0.0f || cellParams.emitRiseGain != 0.0f)
        {   // The prior per light-table entry: for a sine whose |s| peaks at that entry, the factor p that gives
            // law x weight x p the law's own clock-weighted charge over the cycle (sum gate(l) l, below; midpoint rule
            // over the half-cycle), continued 20 entries above the table's top. The weight sits where the law's hump
            // is largest, so p is neither the weight's own mean nor a ratio of mean lights.
            constexpr int kPoints = 512;
            emitPriorSize = std::min(cellParams.lightTableSize + 20, static_cast<int>(emitPrior.size()));
            for (int k = 0; k < emitPriorSize; ++k)
            {
                const double peakDb = cellParams.lightTableStartDb + cellParams.lightTableStepDb * k;
                // The cell's clock reads the emitted light (advanceLight: step = dt x gate(light)), so what the
                // populations receive over a cycle is sum gate(l) l, not sum l. The prior p conserves that:
                // sum gate(law) law = sum gate(law w p) law w p, solved by a fixed point (the gate is a slowly
                // varying power law). Without the gate this is the plain light ratio.
                const auto gate = [this](double l) {
                    if (!(cellParams.timeScaleLight > 0.0f) || l <= 0.0) return 1.0;
                    const double r = std::pow(l / static_cast<double>(cellParams.timeScaleLight),
                                              static_cast<double>(cellParams.timeScaleExponent));
                    return cellParams.hFloor > 0.0f ? (static_cast<double>(cellParams.hFloor) + r) / (1.0 + r) : 1.0 / (1.0 + r);
                };
                std::array<double, kPoints> law{}, weight{};
                double plain = 0.0;
                for (int i = 0; i < kPoints; ++i)
                {
                    const double theta = 3.141592653589793 * (i + 0.5) / kPoints;
                    const double sine = std::sin(theta);
                    law[static_cast<size_t>(i)] = tableLookup(cellParams.lightTable, cellParams.lightTableSize, cellParams.lightTableStartDb,
                                                              cellParams.lightTableStepDb, cellParams.lightSlopeAbove,
                                                              peakDb + 8.685889638065035 * std::log(std::max(sine, 1.0e-12)));
                    double w = cellParams.emitPeakExp > 0.0f ? std::pow(sine, static_cast<double>(cellParams.emitPeakExp)) : 1.0;
                    w *= std::max(0.0, 1.0 + static_cast<double>(cellParams.emitRiseGain) * std::cos(theta));
                    weight[static_cast<size_t>(i)] = w;
                    plain += gate(law[static_cast<size_t>(i)]) * law[static_cast<size_t>(i)];
                }
                double prior = 1.0;
                for (int iteration = 0; iteration < 8; ++iteration)
                {
                    double weighted = 0.0;
                    for (int i = 0; i < kPoints; ++i)
                    {
                        const double l = law[static_cast<size_t>(i)] * weight[static_cast<size_t>(i)] * prior;
                        weighted += gate(l) * l;
                    }
                    if (!(weighted > 0.0)) { prior = 1.0; break; }
                    prior *= plain / weighted;
                }
                // Safety only: with the shipped constants every entry lies in 1.0 .. 2.36 (the largest at the table's
                // top entry, 2026-10-15), far inside the clamp.
                emitPrior[static_cast<size_t>(k)] = std::clamp(prior, 0.05, 20.0);
            }
            emitPriorOn = true;
        }
    }

    // ---- lab-only hooks (fitting kernel) ----
    void setParams(const OptoCellParams& p) noexcept
    {
        cellParams = p;
        clockPower.setExponent(static_cast<double>(cellParams.timeScaleExponent));
        cachedPrNorm = -1.0;
        cachedElementPr = -1.0;
        buildEmitPrior();
        buildTableQuiet();
        if (sampleRate > 0.0) setRate(static_cast<float>(sampleRate));
    }
    const OptoCellParams& params() const noexcept { return cellParams; }
    void primeEquilibrium(int ch, double el, const double* population) noexcept
    {
        const size_t c = static_cast<size_t>(ch);
        state.el[c] = el;
        state.litLevel[c] = cellParams.lfLitLog != 0.0f ? std::max(20.0 * std::log10(std::max(el, 1.0e-12)), static_cast<double>(cellParams.lightTableStartDb) - 60.0) : el;
        state.grRateSmooth[c] = 0.0;
        state.drivePower[c] = el * el;
        state.slewPower[c] = el * el;
        // R14 trackers settled at the primed drive, hold armed: no spurious onset cut. el must be the drive PEAK
        // (the envelope holds peaks); priming from an instantaneous |s| mid-cycle would read as a step up.
        state.driveEnv[c] = el;
        state.driveDb[c] = state.driveSlow[c] = 20.0 * std::log10(std::max(el, kLfCutEnvelopeFloor));
        state.driveHold[c] = cutHoldSamples;
        state.drivePrev[c] = 0.0;
        state.reversalPeak[c] = el;
        state.slewLow[c] = 0.0;
        // The emission's shape (prepareLight, emitLight): its estimator settled like the frequency law's above, the
        // half-cycle reference a settled sine of peak el, the charge scale at rest.
        state.emitLast[c] = 0.0;
        state.emitDrivePower[c] = state.emitSlewPower[c] = el * el;
        state.emitPeakPrev[c] = state.emitPeakCur[c] = el;
        state.emitScale[c] = 1.0;
        state.emitSumPlain[c] = state.emitSumApplied[c] = state.emitSumFade[c] = 0.0;
        state.emitSide[c] = 0;
        state.emitLastEmitted[c] = 0.0;
        state.shapeTurn[c] = -1.0;
        state.freqLawInverse[c] = state.referenceInverse[c] = state.grInverse[c] = std::numeric_limits<double>::quiet_NaN();
        state.fillInverse[c] = state.densityInverse[c] = state.historyAt[c] = std::numeric_limits<double>::quiet_NaN();
        state.floorShareInverse[c] = state.floorGainInverse[c] = state.floorInnerInverse[c] = kStaleInverse;
        if (cellParams.trapCapture > 0.0f)
        {
            const double g1 = population[1];
            const double kt = cellParams.trapCapture, kr = cellParams.trapEmit, tm = cellParams.trapCapacity;
            state.trap[c] = tm > 0.0 ? kt * g1 / (kr + kt * g1 / tm) : (kr > 0.0 ? kt * g1 / kr : 0.0);
        }
        else
            state.trap[c] = 0.0;
        state.reversalPhi[c] = 1.0;
        state.reversalSign[c] = 1;
        state.reversalCount[c] = 0;
        double conductance = 0.0;
        for (int i = 0; i < kPopulations; ++i)
        {
            state.population[static_cast<size_t>(i)][c] = population[i];
            conductance += population[i];
        }
        if (cellParams.conductanceTableOn != 0.0f) conductance = conductanceFor(conductance);
        state.cellGain[c] = 1.0 / (1.0 + conductance);
        state.gain[c] = static_cast<float>(state.cellGain[c]);
        state.grDb[c] = 8.685889638065035 * std::log1p(conductance);
    }
    double cellGain(int ch) const noexcept { return state.cellGain[static_cast<size_t>(ch)]; }
    double elLevel(int ch) const noexcept { return state.el[static_cast<size_t>(ch)]; }
    double lightAt(double el) const noexcept { return lightFor(el); }
    double conductanceAt(double sumG) const noexcept { return conductanceFor(sumG); }

private:
    static constexpr size_t kWeightingSections = 5;
    // R14 envelope: release after the hold (fixed; the hold and the slow tracker carry the fitted timing) and
    // the floor of its dB value (-240 dB, i.e. 1e-12 linear: below any drive the light table responds to).
    static constexpr double kLfCutEnvelopeRelease = 0.010;
    static constexpr double kLfCutEnvelopeFloor = 1.0e-12;
    static constexpr double kLfCutFloorDb = -240.0;   // 20 log10(kLfCutEnvelopeFloor); keep the two in step
    // How far the shape's half turn (radians) and the frequency law's ratio (relative) may move from their last
    // exact evaluation before prepareLight evaluates them again (second- and third-order expansions in between).
    static constexpr double kShapeTurnTolerance = 0.01;
    static constexpr double kFreqLawTolerance = 0.10;
    static constexpr float kFloorPowTolerance = 0.05f;   // expandedPow: relative move of x before a new evaluation
    static constexpr double kLogTolerance = 0.02;         // expandedLog: relative move before a new evaluation
    static constexpr double kExpTolerance = 0.02;         // expandedExp: move of the exponent before a new evaluation
    static constexpr float kStaleInverse = std::numeric_limits<float>::quiet_NaN();

    // Both channels' state, each field a {channel 0, channel 1} pair: one lane reads its channel's element, a
    // linked pair (lanes::Lane2) reads and writes both at once.
    struct CellState
    {
        OptoPair<double> cellGain{1.0}, grDb{0.0}, el{0.0}, litLevel{0.0}, grRateSmooth{0.0};
        // R14 drive trackers: peak-held envelope, its dB value, the slow dB tracker, and the hold countdown.
        OptoPair<double> driveEnv{0.0}, driveDb{kLfCutFloorDb}, driveSlow{kLfCutFloorDb};
        OptoPair<int> driveHold{0};
        OptoPair<double> estimatedFrequencyHz{1000.0};
        OptoPair<double> drivePower{0.0}, slewPower{0.0}, drivePrev{0.0};
        OptoPair<double> reversalPeak{0.0}, reversalPhi{1.0}, slewLow{0.0}, trap{0.0};
        OptoPair<double> fluxHighPassLow{0.0}, fluxLow{0.0}, fluxPeak{0.0};
        // What each polarity of the drive has moved through the panel, light x seconds (below zero: credit).
        std::array<OptoPair<double>, 2> chargeSpent{};
        // How long the panel has had no drive, through two stages (1 = dark, the state at rest).
        std::array<OptoPair<double>, 2> dark{{OptoPair<double>{1.0}, OptoPair<double>{1.0}}};
        OptoPair<int> darkHold{0};   // samples left before the dark state may build (restarted while the panel is driven)
        OptoPair<double> floorEl{0.0};
        OptoPair<double> couplingS1{0.0}, couplingS2{0.0};   // the input coupling high-pass (prepareLight)
        std::array<OptoPair<double>, 2> floorDark{{OptoPair<double>{1.0}, OptoPair<double>{1.0}}};
        OptoPair<int> floorInputHold{0};
        OptoPair<double> floorPassed{1.0};   // the share of the cell's conductance the LF floor passes (1 = no floor)
        // floorPassed while pending: passedOf() of these two (advanceLight keeps the inputs until a dark step reads it).
        OptoPair<double> floorPassedGain{1.0}, floorPassedFloorGain{1.0};
        OptoPair<int> floorPassedPending{0};
        // The floor blend's powers (advanceLight, expandedPow): the share, the gain and the blend each with its last
        // exact evaluation point, inverse (kStaleInverse: none) and value.
        OptoPair<float> floorShareAt{1.0f}, floorShareInverse{kStaleInverse}, floorShareValue{1.0f};
        OptoPair<float> floorGainAt{1.0f}, floorGainInverse{kStaleInverse}, floorGainValue{1.0f};
        OptoPair<float> floorInnerAt{1.0f}, floorInnerInverse{kStaleInverse}, floorInnerValue{1.0f};
        OptoPair<int> reversalSign{1}, reversalCount{0};
        OptoPair<double> satSlew{0.0}, satRaw{0.0}, satDrive{0.0};   // 5 ms power averages: slew, raw slew, level (HF plateau)
        // The emission's shape (prepareLight): the last drive sample and the mean squares of the drive and of its
        // normalised slew (one pole, elFreqTau), its own copies so the shape does not depend on the frequency law.
        OptoPair<double> emitLast{0.0}, emitDrivePower{0.0}, emitSlewPower{0.0};
        // The shape's half-cycle reference (prepareLight): the peak of |s| over the last completed half-cycle of
        // the drive (Prev) and over the current one so far (Cur).
        OptoPair<double> emitPeakPrev{0.0}, emitPeakCur{0.0};
        // The last exact evaluations behind the expansions in prepareLight: the shape's sin / cos of the half turn
        // (turn < 0: none yet), the frequency law's power of the ratio and the shape reference's log (inverse NaN:
        // none yet).
        OptoPair<double> shapeTurn{-1.0}, shapeSin{0.0}, shapeCos{1.0};
        OptoPair<double> referenceAt{1.0}, referenceInverse{std::numeric_limits<double>::quiet_NaN()}, referenceLog{0.0};
        // The cell reduction's log1p of the conductance (advanceLight, expandedLog).
        OptoPair<double> grAt{0.0}, grInverse{std::numeric_limits<double>::quiet_NaN()}, grLog{0.0};
        // The dark ceiling's trap-fill power and the dark history's log1p of the density and exp of its
        // correction (advanceLight: expandedPow, expandedLog, expandedExp; NaN: none yet).
        OptoPair<double> fillAt{1.0}, fillInverse{std::numeric_limits<double>::quiet_NaN()}, fillValue{1.0};
        OptoPair<double> densityAt{0.0}, densityInverse{std::numeric_limits<double>::quiet_NaN()}, densityLog{0.0};
        OptoPair<double> historyAt{std::numeric_limits<double>::quiet_NaN()}, historyExp{1.0};
        OptoPair<double> freqLawRatio{1.0}, freqLawInverse{std::numeric_limits<double>::quiet_NaN()}, freqLawValue{1.0};
        OptoPair<double> emitLastEmitted{0.0};   // the last shaped emission, for the charge term's trapezoid
        // The shape's charge per cycle (emitLight): the scale on the sine prior, the current cycle's clock-weighted
        // charge sums (without the shape, with it, and the first weighted by the fade), the drive's polarity at the
        // last sample. At rest: scale 1, sums 0.
        OptoPair<double> emitScale{1.0}, emitSumPlain{0.0}, emitSumApplied{0.0}, emitSumFade{0.0};
        OptoPair<int> emitSide{0};
        std::array<OptoPair<double>, kPopulations> population{};
        OptoPair<float> gain{1.0f};
        OptoPair<double> elementInput{0.0};   // elementTerm(): the last sample of the cell's input
        OptoPair<double> elementTaken{0.0}, elementPower{0.0};   // meterGain(): running means, see elementTerm()
        OptoPair<float> floorHighPassLow{0.0f}, floorLow{0.0f}, floorBandLow{0.0f};
        OptoPair<float> floorPower{0.0f}, inputPower{0.0f};
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
        // Negated so a NaN level takes this branch (NaN out) rather than
        // reaching the integer index below.
        if (!(levelDb >= start)) return t[0] * (levelDb - (start - step)) / step;
        const double position = (levelDb - start) / step;
        const int k = std::min(static_cast<int>(position), n - 2);
        const double f = position - k;
        return t[k] + f * (t[k + 1] - t[k]);
    }

    // tableLookup() on the step's reciprocal (the per-sample reads; the table builders keep tableLookup()).
    static double tableLookupInv(const float* t, int n, double start, double step, double invStep, double slopeAbove,
                                 double levelDb) noexcept
    {
        const double top = start + step * (n - 1);
        if (levelDb >= top)
            return t[n - 1] * std::exp(slopeAbove * (levelDb - top));
        if (levelDb <= start - step) return 0.0;
        if (!(levelDb >= start)) return t[0] * (levelDb - (start - step)) * invStep;
        const double position = (levelDb - start) * invStep;
        const int k = std::min(static_cast<int>(position), n - 2);
        const double f = position - k;
        return t[k] + f * (t[k + 1] - t[k]);
    }

    // ln(offset + a) (offset 0: ln a, a > 0; offset 1: log1p a, a >= 0) on its third-order expansion in
    // u = (a - a_c) / (offset + a_c) about the last exact evaluation a_c of the channel (lane), redone once |u|
    // passes kLogTolerance: error <= u^4 / 4. An inverse of NaN forces a new evaluation.
    template <class L>
    OPTO_FORCE_INLINE typename L::D expandedLog(int ch, typename L::D a, double offset, OptoPair<double> CellState::* at,
                              OptoPair<double> CellState::* inverse, OptoPair<double> CellState::* value,
                              typename L::M active = L::m(true)) noexcept
    {
        using lanes::vabs;
        const auto Kd = [](double v) noexcept { return L::d(v); };
        typename L::D reference = get<L>(ch, at), reciprocal = get<L>(ch, inverse), exact = get<L>(ch, value);
        typename L::D u = (a - reference) * reciprocal;
        // ln of a non-positive a is the caller's to mask; it is never evaluated (silence would do so every sample).
        // `active`: the lanes whose value is used (the others keep their reference).
        const auto stale = active && (offset != 0.0 ? a >= Kd(0.0) : a > Kd(1.0e-30)) && !(vabs(u) <= Kd(kLogTolerance));
        if (L::any(stale))
        {
            reference = L::sel(stale, a, reference);
            reciprocal = L::sel(stale, Kd(1.0) / (Kd(offset) + a), reciprocal);
            exact = L::sel(stale, offset == 1.0 ? L::map([](double v) noexcept { return std::log1p(v); }, a)
                                                : lanes::lanemath::log(a), exact);
            u = L::sel(stale, Kd(0.0), u);
            put<L>(ch, at, reference);
            put<L>(ch, inverse, reciprocal);
            put<L>(ch, value, exact);
        }
        return L::sel(a == a, exact + u * (Kd(1.0) - u * (Kd(0.5) - u * Kd(1.0 / 3.0))), a);   // NaN passes through
    }

    // x^y (float, y > 0) on its second-order expansion in u = x / x_c - 1 about the last exact evaluation x_c of the
    // channel (lane), redone once |u| passes kFloorPowTolerance: relative error <= |y (y - 1)(y - 2)| / 6 |u|^3.
    // x <= 0 gives 0 as std::pow does for these y; NaN passes through. The caller keeps y fixed while the
    // expansion lives (an inverse of kStaleInverse forces a new evaluation).
    template <class L, class T>
    OPTO_FORCE_INLINE auto expandedPow(int ch, std::conditional_t<std::is_same_v<T, float>, typename L::F, typename L::D> x, T y,
                     OptoPair<T> CellState::* at, OptoPair<T> CellState::* inverse, OptoPair<T> CellState::* value) noexcept
    {
        using lanes::vabs;
        using V = decltype(x);
        const auto K = [](T v) noexcept { if constexpr (std::is_same_v<T, float>) return L::f(v); else return L::d(v); };
        V reference = get<L>(ch, at), reciprocal = get<L>(ch, inverse), exact = get<L>(ch, value);
        V drift = (x - reference) * reciprocal;
        const auto positive = x > K(T(1.0e-30));
        const auto stale = positive && !(vabs(drift) <= K(T(kFloorPowTolerance)));
        if (L::any(stale))
        {
            reference = L::sel(stale, x, reference);
            reciprocal = L::sel(stale, K(T(1)) / x, reciprocal);
            if constexpr (std::is_same_v<T, float>)
                exact = L::sel(stale, L::mapF([y](float a, float) noexcept { return std::pow(a, y); }, x, x), exact);
            else
                exact = L::sel(stale, L::map([y](double a) noexcept { return std::pow(a, y); }, x), exact);
            drift = L::sel(stale, K(T(0)), drift);
            put<L>(ch, at, reference);
            put<L>(ch, inverse, reciprocal);
            put<L>(ch, value, exact);
        }
        const V expanded = exact * (K(T(1)) + drift * (K(y) + drift * K(T(0.5) * y * (y - T(1)))));
        return L::sel(!(x >= x), x, L::sel(positive, expanded, K(T(0))));   // !(x >= x): NaN
    }

    // exp(z) on its third-order expansion in d = z - z_c about the last exact evaluation z_c of the channel (lane),
    // redone once |d| passes kExpTolerance: relative error <= d^4 / 24. A z_c of NaN forces a new evaluation.
    template <class L>
    OPTO_FORCE_INLINE typename L::D expandedExp(int ch, typename L::D z, OptoPair<double> CellState::* at, OptoPair<double> CellState::* value) noexcept
    {
        using lanes::vabs;
        const auto Kd = [](double v) noexcept { return L::d(v); };
        typename L::D reference = get<L>(ch, at), exact = get<L>(ch, value);
        typename L::D d = z - reference;
        const auto stale = !(vabs(d) <= Kd(kExpTolerance));
        if (L::any(stale))
        {
            reference = L::sel(stale, z, reference);
            exact = L::sel(stale, L::map([](double v) noexcept { return std::exp(v); }, z), exact);
            d = L::sel(stale, Kd(0.0), d);
            put<L>(ch, at, reference);
            put<L>(ch, value, exact);
        }
        return exact * (Kd(1.0) + d * (Kd(1.0) + d * (Kd(0.5) + d * Kd(1.0 / 6.0))));
    }

    // The populations' time step for a sample of light l (advanceLight; emitLight's charge sums use the same).
    template <class L>
    OPTO_FORCE_INLINE typename L::D timeScaleT(typename L::D l) const noexcept
    {
        const auto Kd = [](double x) noexcept { return L::d(x); };
        using D = typename L::D;
        if (cellParams.timeScaleLight > 0.0f && cellParams.hFloor > 0.0f)
        {
            const D v = lanes::vmax(l, Kd(0.0)) * Kd(invTimeScaleLight);
            const auto zero = v == Kd(0.0);
            const D r = L::sel(zero, Kd(0.0), clockPower(L::sel(zero, Kd(1.0), v)));
            return Kd(dt) * (Kd(static_cast<double>(cellParams.hFloor)) + r) / (Kd(1.0) + r);
        }
        if (cellParams.timeScaleLight > 0.0f)
        {
            const auto lit = l > Kd(0.0);
            if (!L::any(lit)) return Kd(dt);   // no light in any lane: the step is dt without evaluating the law
            const D v = l * Kd(invTimeScaleLight);
            const auto positive = v > Kd(0.0);
            const D r = L::sel(positive, clockPower(L::sel(positive, v, Kd(1.0))), Kd(1.0));
            return L::sel(lit, Kd(dt) / (Kd(1.0) + r), Kd(dt));
        }
        return Kd(dt);
    }
    double timeScale(double l) const noexcept { return timeScaleT<lanes::Lane1>(l); }

    // The light and conductance tables. A pair reads both lanes' interior entries at once (tableLookup's other
    // branches lane by lane); the logarithm is lanemath's, the same for one lane or two.
    // The level at or below which a table reads 0: under its first point less a step (tableLookup), with a 1e-9
    // margin. tableT's lanes there skip the logarithm and the read (exactly what they would have returned).
    static double tableQuiet(double start, double step) noexcept
    {
        return std::exp((start - step) / 8.685889638065035) * (1.0 - 1.0e-9);
    }
    void buildTableQuiet() noexcept
    {
        lightTableQuiet = tableQuiet(cellParams.lightTableStartDb, cellParams.lightTableStepDb);
        // The per-sample divisions by parameters, as multiplications by their reciprocals.
        lightTableInvStep = 1.0 / static_cast<double>(cellParams.lightTableStepDb);
        conductanceTableInvStep = 1.0 / static_cast<double>(cellParams.conductanceTableStepDb);
        invTimeScaleLight = 1.0 / static_cast<double>(cellParams.timeScaleLight);
        invLfLitSpanDb = 1.0 / static_cast<double>(cellParams.lfLitSpanDb);
        invLfTermGrSpan = 1.0f / cellParams.lfTermGrSpan;
        invTrapCapacity = 1.0 / static_cast<double>(cellParams.trapCapacity);
        {
            const double lo = static_cast<double>(cellParams.emitFadeLoKHz) * cellParams.emitFadeLoKHz;
            const double hi = static_cast<double>(cellParams.emitFadeHiKHz) * cellParams.emitFadeHiKHz;
            invFadeLowSpan = 1.0 / (hi - lo);
            const double topLo = static_cast<double>(cellParams.emitFadeTopLoKHz) * cellParams.emitFadeTopLoKHz;
            const double topHi = static_cast<double>(cellParams.emitFadeTopHiKHz) * cellParams.emitFadeTopHiKHz;
            invFadeTopSpan = 1.0 / (topHi - topLo);
        }
        conductanceTableQuiet = tableQuiet(cellParams.conductanceTableStartDb, cellParams.conductanceTableStepDb);
    }
    template <class L>
    OPTO_FORCE_INLINE typename L::D tableT(typename L::D x, const float* t, int n, double start, double step,
                                                double invStep, double slopeAbove, double quiet) const noexcept
    {
        const auto Kd = [](double v) noexcept { return L::d(v); };
        if constexpr (L::kLanes == 1)
        {
            if (x <= quiet) return 0.0;
            return tableAtT<L>(x, 8.685889638065035 * lanes::lanemath::log(x), t, n, start, step, invStep, slopeAbove,
                               quiet);
        }
#if defined(__aarch64__)
        else
        {
            const auto silent = x <= Kd(quiet);
            if (L::all(silent)) return Kd(0.0);
            return tableAtT<L>(x, Kd(8.685889638065035) * lanes::lanemath::log(L::sel(silent, Kd(1.0), x)), t, n, start,
                               step, invStep, slopeAbove, quiet);
        }
#endif
    }
    // tableT() with the level in dB already taken (8.685889638065035 ln x, for every lane above `quiet`).
    template <class L>
    OPTO_FORCE_INLINE typename L::D tableAtT(typename L::D x, typename L::D levelDb, const float* t, int n,
                                                  double start, double step, double invStep, double slopeAbove,
                                                  double quiet) const noexcept
    {
        const auto Kd = [](double v) noexcept { return L::d(v); };
        if constexpr (L::kLanes == 1)
        {
            if (x <= quiet) return 0.0;
            const double read = tableLookupInv(t, n, start, step, invStep, slopeAbove, levelDb);
            return x <= 1.0e-30 ? 0.0 : read;
        }
#if defined(__aarch64__)
        else
        {
            using D = typename L::D;
            const auto silent = x <= Kd(quiet);
            if (L::all(silent)) return Kd(0.0);
            const double top = start + step * (n - 1);
            D read;
            if (L::any(levelDb >= Kd(top) && !silent))
                read = L::map([t, n, start, step, invStep, slopeAbove](double db) noexcept {
                    return tableLookupInv(t, n, start, step, invStep, slopeAbove, db); }, levelDb);
            else
            {   // tableLookupInv's branches below its top, for both lanes in its order: zero, the ramp, the interior
                const D position = (levelDb - Kd(start)) * Kd(invStep);
                // min(int(position), n - 2) as the truncation of min(position, n - 2); a lane below reads entry 0.
                const int64x2_t index = vcvtq_s64_f64(lanes::vmax(lanes::vmin(position, Kd(n - 2)), Kd(0.0)).v);
                const float32x2_t pair0 = vld1_f32(t + vgetq_lane_s64(index, 0));   // {t[k], t[k + 1]}, lane 0
                const float32x2_t pair1 = vld1_f32(t + vgetq_lane_s64(index, 1));
                const float32x2_t low = vzip1_f32(pair0, pair1), high = vzip2_f32(pair0, pair1);
                const D f = position - D{vcvtq_f64_s64(index)};
                const D interior = D{vcvt_f64_f32(low)} + f * D{vcvt_f64_f32(vsub_f32(high, low))};
                const D ramp = Kd(static_cast<double>(t[0])) * (levelDb - Kd(start - step)) * Kd(invStep);
                read = L::sel(levelDb <= Kd(start - step), Kd(0.0), L::sel(!(levelDb >= Kd(start)), ramp, interior));
            }
            return L::sel(x <= Kd(1.0e-30), Kd(0.0), L::sel(silent, Kd(0.0), read));
        }
#endif
    }
    template <class L>
    typename L::D lightAtT(typename L::D el, typename L::D levelDb) const noexcept
    {
        return tableAtT<L>(el, levelDb, cellParams.lightTable, cellParams.lightTableSize, cellParams.lightTableStartDb,
                           cellParams.lightTableStepDb, lightTableInvStep, cellParams.lightSlopeAbove, lightTableQuiet);
    }
    template <class L>
    typename L::D lightForT(typename L::D el) const noexcept
    {
        return tableT<L>(el, cellParams.lightTable, cellParams.lightTableSize, cellParams.lightTableStartDb,
                         cellParams.lightTableStepDb, lightTableInvStep, cellParams.lightSlopeAbove, lightTableQuiet);
    }
    template <class L>
    typename L::D conductanceForT(typename L::D sumG) const noexcept
    {
        return tableT<L>(sumG, cellParams.conductanceTable, cellParams.conductanceTableSize,
                         cellParams.conductanceTableStartDb, cellParams.conductanceTableStepDb, conductanceTableInvStep,
                         cellParams.conductanceSlopeAbove, conductanceTableQuiet);
    }
    double lightFor(double el) const noexcept { return lightForT<lanes::Lane1>(el); }
    double conductanceFor(double sumG) const noexcept { return conductanceForT<lanes::Lane1>(sumG); }

    OptoCellParams cellParams{};
    double lightTableQuiet = 0.0, conductanceTableQuiet = 0.0;   // tableQuiet() of the two tables (buildTableQuiet)
    float floorLimitRatioScale = 1.0f;   // the floor law's Limit ratio scale to the power e/2 (updateFloorBand)
    int floorExpansionMode = -1;         // the mode (Limit) the floor blend's expansions were made for; -1: none
    CellState state{};
    std::array<std::array<Biquad, kWeightingSections>, kChannels> loopWeighting;
    std::array<std::array<Biquad, kWeightingSections>, kChannels> levelWeighting;
    std::array<std::array<Biquad, 4>, kChannels> depthWeighting;
    std::array<Biquad, kChannels> lfTermFilter;
    std::array<Biquad, kChannels> limitFloorFilter;
    std::array<Biquad, kChannels> lfCutFilter;
    // The sections' states (biquad()): the filters above hold the coefficients.
    std::array<OptoBiquadState, kWeightingSections> loopWeightingState{}, levelWeightingState{};
    std::array<OptoBiquadState, 4> depthWeightingState{};
    OptoBiquadState lfTermState{}, limitFloorState{}, lfCutState{};
    std::array<OptoSubsonicHighPass, kChannels> couplingInput;
    double sampleRate = 0.0, dt = 1.0 / 96000.0, elStep = 0.0;
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
    std::array<double, 64> emitPrior{};   // per light-table entry: law mean / (law x weight) mean on a sine
    bool emitPriorOn = false;
    double emitLeak = 1.0;
    // Reciprocals of the per-sample divisors (buildTableQuiet, setRate).
    double lightTableInvStep = 1.0, conductanceTableInvStep = 1.0, invTimeScaleLight = 1.0, invLfLitSpanDb = 1.0;
    double invTrapCapacity = 1.0, invFadeLowSpan = 1.0, invFadeTopSpan = 1.0, invDt = 1.0;
    float invLfTermGrSpan = 1.0f;
    lanes::lanemath::FixedPower clockPower;   // timeScale()'s power of the light (timeScaleExponent; setParams)
    int emitPriorSize = 0;
    float floorHighPassStep = 0.0f, floorLowPassStep = 0.0f, floorBandStep = 0.0f, floorPowerStep = 0.0f;
};

} // namespace duskaudio
