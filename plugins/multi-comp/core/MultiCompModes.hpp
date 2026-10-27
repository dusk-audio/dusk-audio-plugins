// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free compressor mode math.  The detector/gain-cell equations in
// this file are direct C++17 transcriptions of multicomp.cpp; JUCE containers,
// Decibels, jlimit and IIR filters are replaced by small local helpers/shared DSP.
#pragma once
#include "MultiCompBusControls.hpp"
#include "MultiCompBusLaw.hpp"


#include "MultiCompVcaLaw.hpp"
#include "MultiCompStudioVcaLaw.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompOptoShelf.hpp"
#include "MultiCompOptoCell.hpp"
#include "MultiCompOptoOutputCurve.hpp"
#include "MultiCompOptoOutputStage.hpp"
#include "MultiCompOptoStageOversampler.hpp"
#include "MultiCompOptoStructuralHarmonics.hpp"
#include "MultiCompHelpers.hpp"
#include "MultiCompFastMath.hpp"
#include "../../shared-daf/dsp/DuskCrossover.hpp"
#include "../../shared-daf/dsp/DuskFilters.hpp"
#include "MultiCompHardware.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// A function the compiler must not inline: the rare refresh paths of the per-sample memos, so the check in front of
// them stays small enough to inline into the per-sample code.
#if defined(_MSC_VER)
#define MC2_NOINLINE __declspec(noinline)
#else
#define MC2_NOINLINE __attribute__((noinline))
#endif
// A branch that is rarely taken: its work (an exact library evaluation) stays behind the branch instead of being
// computed speculatively and selected away.
#if defined(__GNUC__) || defined(__clang__)
#define MC2_UNLIKELY(condition) __builtin_expect(static_cast<bool>(condition), false)
#else
#define MC2_UNLIKELY(condition) (condition)
#endif

namespace duskaudio
{

// R10's tone-only harmonic synthesis (cell ripple scale and cell-distortion
// injection, both armed by tone detection) does not ship: owner decision
// 2026-09-30.  Off from R11a so every measurement shows the physical model
// alone; deleted in R13 (docs/opto-r10-harmonic-structure-2026-09-30.md).
inline constexpr bool kOptoToneSynthesisEnabled = false;

// Every correction gated by input classification (tone/sine-likeness,
// frequency or event detection, stationarity, knob windows): terms T1-T15 of
// docs/opto-r11a-classifier-audit-2026-09-30.md, i.e. the whole
// meterCorrectionDb sum and the small LF settled correction.  Off by default
// (owner, 2026-09-30): the default build is the physical base.  With it off
// the classifiers C1-C7 still run but drive nothing; they are removed when
// physical dynamics replace them (R12+).  The cell's continuous EL frequency
// law and LF floor (C8, C9) are not affected.
inline constexpr bool kOptoClassifierCorrectionsEnabled = false;

// OPTO stereo link inside the cell, at the light (OptoCell::setLinkWeight,
// processLightPair) instead of a blend toward the sample-wise larger channel
// ahead of the cell. Off, the linked detector is the blend, as before.
inline constexpr bool kOptoLinkAtDriveDefault = true;

// Vintage-FET programme-dynamics constants, in one place so an offline lab can
// change them at run time (MultiCompModes::setFetDynamicsParams, reached only
// through MultiCompDSPTestAccess; the plugin never calls it and always runs the
// defaults). Order: programmeActiveLevel, programmeSilenceHoldSeconds,
// programmeExposureCapSeconds, exposureLowDrive[4], exposureMidDrive[4],
// exposureHighDrive[4] (release multipliers at 0.01 / 0.1 / 1 / 5 s exposure),
// slowRelease[3] and legacyFastRelease[3] (quadratics in the Release position).
// New constants are appended, and each has a value, named with it below, at
// which it is an exact no-op.
// [21..25] detector charge (candidate C2, off at detChargeOn = 0): the level
// detector charges through a finite attack (geometric in the Attack position
// between detAttackSlowSeconds at 0 and detAttackFastSeconds at 1) and
// discharges with detReleaseSeconds, instead of capturing every input peak
// instantly; detGainDb recalibrates its sine reading onto the static law.
// [26] detRatioExponent: the charge time is scaled by fetRatioAttackScale^e,
// the feedback loop's faster effective attack at higher ratios.
// [27..30] programme-state leak (candidate C1, off at stateLeakOn = 0): the
// exposure clock runs only while the target reduction exceeds exposureGateDb
// and otherwise decays with exposureDecaySeconds; the programme maximum decays
// toward the present reduction with maxGrDecaySeconds. Off, both latch while
// any signal is present and clear only after silence.
// [31..32] slow-population share: slowTarget = min(base, slowTargetBaseDb +
// slowTargetSlope * base + ratio and attack offsets) (production values).
// [33..35] output low-frequency stage (candidate C4, off at lfHpOn = 0): a
// second-order high-pass (lfHpFrequency, lfHpQ) in place of the 5 Hz
// first-order stage; the reference's LF phase lead with flat magnitude.
// [71..72] lfAheadHz, lfAheadQ: a second one AHEAD of the gain element
// (documented after fbFloorOnApplied, below).
// [36] detAnalyticOn (candidate C5, used only inside C2): the detector charges
// from the analytic-signal magnitude |x + jH(x)| instead of |x|, which reads a
// sine's amplitude without ripple at any frequency and needs no peak hold.
// [37..70] feedback gain computer (candidate C6, off at fbOn = 0; ratios 4-20
// only, All-buttons keeps the feed-forward arm). One capacitor c, in dB of
// reduction, integrates the OUTPUT's overshoot above the level the static law
// settles at, and leaks through the release:
//   dc/dt = max(0, |out| / M(c + Q(c) + T(c)) - 1) / attack - c / release
// (Q and T the settled sizes of the two slow populations, below; with
// fbTailKneeDb the tail's term is what T adds through the gain element's law),
// |out| the full-wave rectified output, read through the side chain's own
// high-pass (fbTapHighPassHz, below). A linked pair is a diode-OR of the two
// channels' rectifiers: each loop charges from the larger of its own and its
// partner's magnitude, so the louder channel alone sets the reduction and two
// equal independent channels read about 0.9 dB above one,
// M the inverse of the measured static law, solved implicitly per sample.
//   attack  = fbAttackSeconds * 10^(fbAttackSlope (0.5 - a) + fbAttackCurve (a - 0.5)^2)
//             * fetRatioAttackScale^fbRatioExponent      (a = Attack position)
//   release = fbReleaseSeconds * 10^(fbReleaseSlope (0.5 - r) + fbReleaseCurve (r - 0.5)^2)
//             * fetRatioReleaseScale * (1 + reduction / depth [+ (held / De)^p])
//                                                        (r = Release position)
//             the bracketed term at 4:1 only: held = slow + tail reduction,
//             De = fbReleaseDepthExtraDb, p = fbReleaseDepthExtraExponent
//   depth   = (fbReleaseDepthDb + fbReleaseDepthFastDb * max(0, 2 r - 1)^2)
//             * (k / k at 4:1)^fbReleaseDepthRatioExponent,
//             k = slope / (1 - slope) of the ratio's static law
// The depth term is the reference's release slowing as the reduction deepens
// (after a 1 s hold, 386 ms at 14 dB against 512 ms at 22 dB at 4:1; from
// 25 dB it falls at about 43 dB/s at 4:1 and 60 dB/s at 20:1). Toward the fast
// end of the Release knob that slowing fades (fbReleaseDepthFastDb, quadratic
// from Release 0.5): 20-60 ms after a short burst to 28 dB at
// Release 1 the reference has released 1.2-1.7 dB more than one depth for
// every position gives, while Release 0 and 0.33 fit that one depth. (Zero
// since 2026-10-06: with the Release pot law and the tail population, below,
// a probe fit returns it to zero. It had been standing in for both.)
// A slow population q adds to the reduction and follows
//   Q(c) = fbSlowCeilingDb * tanh(fbSlowShare * c / fbSlowCeilingDb)
// (up in fbSlowChargeSeconds, down in fbSlowReleaseRatio * release, with the
// Release knob's factor raised to fbSlowReleaseKnobExponent instead of 1: the
// reference's slow tail shortens far less than its release at the fast end of
// the knob).
// fbAttackLagRatio: the charging current reaches the capacitor through one
// more pole, at fbAttackLagRatio * 10^(fbAttackLagSlope (0.5 - a)) * attack
// seconds (ratio 0 = none): at the slow end
// of the Attack knob the reference's first half millisecond is S-shaped
// (5.0 / 8.0 / 12.7 / 16.1 dB at 0.25 / 0.5 / 1 / 2 ms into a 22 dB step at
// Attack 0), where a single pole rises fastest at the start.
// fbAttackLoadDb raises the threshold by that many dB at Attack 1, linearly
// from Attack 0.5: the reference's settled sine reduction falls 0.3 dB at
// Attack 0.75 and 0.5-0.8 dB at 1 (the fast end of the pot loading the
// side chain), with nothing below 0.5. The slow population is
// the programme-dependent release, a function of how long the reduction was
// held and not of whether signal is present, and the reference's slow rise
// toward its settled reduction: until q has charged the loop sits
// (1 - slope) * Q below the static law, the same number of dB at 18 and at
// 30 dB of reduction and about four times more at 4:1 than at 20:1. Replaces
// the detector, the three populations, the exposure clock and the post-burst
// assist.
// fbTurnOnDb (0 = off), fbTurnOnBiasDb: a unit of overshoot charges by the share
//   min(1, sqrt((reduction + 0.001 + fbTurnOnBiasDb * a^2) / fbTurnOnDb))
// (reduction = fast charge plus slow population in dB, a = Attack position): a
// turn-on of fixed width that the Attack knob biases into. What was read on
// the reference, where the sample-to-sample rise can be resolved, is the
// charge per unit of overshoot against the present reduction (20:1; dB : share
// of the rate reached deep):
//   Attack 0      0.25 : 0.14   1.2 : 0.32   2.5 : 0.50   4.7 : 0.70   9 up : 1
//   Attack 0.0625 0.45 : 0.40   1.1 : 0.54   2.1 : 0.68   4.4 : 0.86
//   Attack 0.125  0.67 : 0.60   1.1 : 0.68   2.2 : 0.81   4.1 : 0.93
//   Attack 0.25   1.05 : 0.84   2.4 : 0.94   3.9 up : 1
//   Attack 0.375 and above: flat.
// Two of the reference's onsets at Attack 0 that differ in level and timing
// fall on one curve in the reduction, and its burst from 8 dB of reduction
// charges at the full rate from the first sample, which rules out a delay.
// That reading fixed the form. The two constants are a fit of the probe tables
// together with the attack law (tools fet/attack_fit.py, no music), not of the
// reading: at their values the law is wider than the reading (0.43 / 0.60 /
// 0.82 at 2.5 / 4.7 / 9 dB at Attack 0, 1 from 13.3 dB) and gone from Attack
// 0.33. The mechanism is not identified (it reads like the pot's quiescent
// current lifting the gain element toward conduction).
// fbReleaseTaperEnd (1 = off), fbReleaseTaperCurve (0 or less = off): the
// Release pot's dead travel at its fast end. Above the nominal position
// n = 0.665955 the knob laws read
//   r' = n + (1 - n) * min(1, (r - n) / (fbReleaseTaperEnd - n))^fbReleaseTaperCurve
// for r, so every law is untouched at and below the nominal position and at 1.
// Measured on the reference as the time to release half way after a 1 s tone
// (ms at 4:1 / 20:1): 191 / 223, 119 / 140, 50 / 60, 36 / 44, 37 / 44 at
// r = 0.666, 0.75, 0.833, 0.917, 1: a straight line in r from 0.12 to 0.85 with
// flat ends, a linear pot in series with a fixed resistance. The end is the
// end of that line; the curve puts 0.75 where r' = 0.80 sits on the release
// law below, which is where its time is (0.833 then reads as r' = 0.965, 0.85
// and up as 1). No probe cell was fitted. The quadratic through 0, 0.33, 0.666
// and 1 alone was up to 1.7 times too slow between 0.75 and 0.95.
// fbReleasePotFixed (0 = off), fbReleasePotStart, fbReleasePotEnd: the Release
// knob as what those half-release times describe, a linear pot in series with
// a fixed resistance, with dead travel at both ends. The release time is
// proportional to
//   fbReleasePotFixed + clamp((fbReleasePotEnd - r)
//                             / (fbReleasePotEnd - fbReleasePotStart), 0, 1)
// and replaces 10^(fbReleaseSlope (0.5 - r) + fbReleaseCurve (r - 0.5)^2)
// wherever that factor is used (fbReleaseSeconds stays the value at r = 0.5);
// the taper above is bypassed while it is on. Start and end are read off the
// reference's half-release times at nine positions (ms at 4:1 / 20:1:
// 686 / 790, 639 / 739, 491 / 571, 337 / 392, 191 / 223, 119 / 140, 50 / 60,
// 36 / 44, 37 / 44 at r = 0, 0.167, 0.33, 0.5, 0.666, 0.75, 0.833, 0.917, 1):
// the last sixth of the travel does nothing, the first eighth nearly nothing,
// a straight line between. The fixed part is fitted with the release side on
// the probe tables (tools fet/slow_fit.py, no music). The depth law's fast end
// follows the travel as well: its knob term is 1 - travel / travel at 0.5
// in place of max(0, 2 r - 1).
// fbTailShare (0 = off), fbTailCurve, fbTailChargeSeconds,
// fbTailReleaseSeconds, fbTailRatioExponent: a second slow population t whose
// times ignore the Release knob. It follows
//   T(c) = fbTailShare * (k / k at 4:1)^fbTailRatioExponent
//          * c * (1 + fbTailCurve * c / 20)
// up in fbTailChargeSeconds and down in fbTailReleaseSeconds, adds to the
// reduction like q, and the threshold is read at c + Q(c) + T(c) (a negative
// share or curve is read as 0). That is the tail as a dB population; with
// fbTailKneeDb, below, T is a control value and adds through the gain
// element's law instead. Measured on
// the reference with trains of six 150 ms bursts 500 ms apart (probe set 9):
// at Release 1 the reduction read 100-150 ms after a burst to 29 dB grows from
// 4.1 dB after the first burst to 10.0 dB after the sixth, and after the sixth
// it is still 6.0 / 4.5 / 3.6 / 3.1 dB at 250 / 450 / 650 / 800 ms. A part of
// the reduction builds up over events and stays for about a second whatever
// the knob says (2.6 dB after bursts to 17 dB; about 1.4 times less at 20:1
// than at 4:1 at equal depth, hence the ratio exponent of the dB form; the
// control form fits both ratios without it). The one slow
// population that lets go with the knob held next to nothing between events
// at Release 1, and programme there read 0.6-1.2 dB shallow.
// fbTapHighPassHz (0 = a copy of the C4 stage, as before): the side chain
// reads the driven signal through its own first-order high-pass. Measured on
// sines of 15-30 Hz at Release 1, where the capacitor follows the waveform:
// the reference's reduction peaks with the INPUT's phase; read behind the C4
// stage it peaked with the output's, 56 degrees early at 15 Hz. The side chain
// taps ahead of the stage that gives the output its low-frequency phase lead.
// Settled low sines, beating pairs and noise bands from 10 to 240 Hz at two
// depths and three Release positions (384 rows, probe set 8) then sit within
// 0.5 dB (0.28 -> 0.16 dB rms); the corner is from a scan of 2 / 3 / 5 / 8 Hz
// against those rows and the stationary tables.
// fbTailKneeDb (0 = off): the tail population acts through the gain element's
// control law, F(v) = v^2 / (4 knee) below v = 2 knee and v - knee above (dB of
// reduction for a control value v). Its state is a control value v that
// follows, first order both ways, the settled value T(c) above (with the
// knee on, fbTailShare and fbTailCurve give T as a CONTROL value: a reservoir
// whose settled value is a share of the fast charge). Its own reading is
// t = F(v) dB, and on top of the rest of the reduction r = c + q it adds
// F(Finv(r) + v) - r: t once the rest is gone, v under a deep rest. The
// threshold reads the settled value of the same expression, so settled
// sines stay on the static law. fbTailChargeSeconds and
// fbTailReleaseSeconds are times of v (below the knee t itself moves twice
// as fast). Read on the reference with the burst
// trains of probe set 9 at 1 kHz (tools fet/memory_law.py): the memory left
// 340 ms after bursts 1..6 at Release 1 is 0.89 / 2.16 / 3.25 / 4.07 / 4.66 /
// 5.08 dB at 4:1, so its second step is larger than its first, which no
// first-order population gives, while the steps of its square root shrink by
// one factor (0.56-0.71 at 17 and 29 dB, 4:1 and 20:1); 60-100 ms after the
// later bursts the reduction stands 1.0-1.2 dB above fast charge plus that
// memory; and from rest at 20:1 the loop sits 0.5 dB under its settled level,
// several times (1 - slope) (T - t). One law gives the three. The mechanism
// is not identified (it reads like a reservoir on the gate node of a FET
// whose channel opens quadratically near pinch-off).
// On since 2026-10-06. A probe fit of the release side on the 1 kHz probes
// (tools fet/mem_bench.py: sets 1-6 and the 1 kHz burst trains of set 9; no
// music, and not the low carriers of set 9, whose windows after a burst read
// the output stage) puts the knee at 9.85 dB and T at 0.557 c with no curve,
// a linear reservoir, 1.30 s up and 3.93 s down in v, with the ratio
// exponent at 0.044 (before, -0.229). The rest of the release side moved
// with it: the knob-following population (share 0.99 -> 0.82, ceiling 10.9
// -> 6.0 dB, charge 1.90 -> 2.12 s, release ratio 2.90 -> 2.64, knob exponent
// 0.58 -> 0.19), the capacitor's release (0.2424 -> 0.2767 s at Release 0.5,
// depth 18.5 -> 25.3 dB, depth ratio exponent -0.123 -> -0.039) and the pot's
// fixed part (0.0687 -> 0.0698). Burst-train cells at or above 1 dB: 69 -> 20
// of 3,240 (the 20: the first 5 ms frame of 40 Hz bursts at 20:1, low-carrier
// windows after bursts at 4:1, and two cells 200-300 ms after a first burst
// at 20:1, Release 0); long holds 0.21 -> 0.11 dB rms. Before: the tail as a
// dB population, T = 0.150 c (1 + 1.069 c / 20), 1.151 s up, 3.593 s down.
// fbFloorOnApplied (0 = off): the static law's plateau, the most reduction
// the gain element gives, limits the reduction APPLIED, c + q + t (with
// fbTailKneeDb: c + q + what the tail adds). Off, the
// threshold M carries the plateau at the SETTLED reduction c + Q(c) + T(c),
// which stops the charge short of it by the slow populations' settled size
// for as long as they are empty: with the constants before this element
// (Q alone, 15 dB at depth) a burst from rest that the reference takes to
// 27.5 dB stopped at 24 dB (probe set 9; no earlier probe went that deep from
// rest, set 5 sits on a bed that keeps the slow population charged).
// lfAheadHz, lfAheadQ (0 Hz = none; needs lfHpOn): a second-order high-pass
// on the audio AHEAD of the gain element; the C4 stage (lfHpFrequency,
// lfHpQ) stays behind it. A linear capture gives the product of the two
// only. What is behind the gain element rings when the gain steps, with
// its own poles; what is ahead of it sees a stationary signal and does
// nothing. So the split is read with a low tone under 1 kHz bursts
// (probe set 10, tools fet/lf_under.py: half the difference of the two
// polarities of the tone is its response alone), and the product on the
// tails that low bursts 60 dB down leave when they end (set 9's low-level
// renders, tools fet/lf_linear_fit.py, lf_split.py). The side chain keeps
// its own tap filter (fbTapHighPassHz) on the driven signal, and the
// shallow-knee colour terms keep reading the driven signal their peak
// follower tracks (gained / kneePeak); the broadband and low colour terms
// read the signal behind the gain element, so behind the section. The
// sections act at every ratio, All-buttons included, as C4 does; the
// probes above are 4:1 and 20:1.
// On since 2026-10-07: C4 at 7.19 Hz, Q 0.719 and the section ahead at
// 3.02 Hz, Q 0.695 (before: C4 alone at 9.88 Hz, Q 0.787, fitted on the phase
// lead of a linear capture above 15 Hz, which it has, with a tail it has
// not: 1.5 dB short 30-60 ms after a 40 Hz burst, 4 dB short at 60-100 ms).
// The four numbers are one least-squares fit of the low-level tails of set 9
// (window errors up to 1.5 dB -> within 0.04 dB) and of probe set 10 (error
// of the tone's response after the onsets -15.1 -> -32.7 dB re the response;
// the other way round, the 7 Hz pair ahead, is -6 dB, and a third section
// fits to nothing). No compressing probe and no music is in that fit. What
// the two sections then give, unfitted: burst-train cells at or above 1 dB
// 20 -> 4 of 3,240 (the 4: the first 5 ms frame of a 40 Hz burst from rest
// at 20:1, twice, and two cells 200-300 ms after a first burst at 20:1,
// Release 0), sets 1-3 0.230 -> 0.190 dB rms, music pooled 0.269 / 0.369 /
// 0.365 -> 0.261 / 0.315 / 0.285 dB (FIT / VAL / SEALED), held-out knob
// positions 0.266 -> 0.260. Read through the section ahead instead of its
// own tap, the side chain is worse (10 cells), and the tap's corner scanned
// again at 3 / 4 / 6 / 7 / 8 / 10 Hz stays at 5: the first lobes of a low
// burst ask for more, the settled low sines for less.
// Active feedback-ratio constants from the 2026-10-08 four-cell calibration.
// fbRatioExponent 1.2309229 -> 1.3369229; fbReleaseDepthRatioExponent -0.039199907 -> 0.010800093.
// Defined synthetic cells at or above 1 dB: 4/4340 -> 0/4340; set9 worst 1.124395353 -> 0.996719298 dB.
// Held-out knob positions: 0/192 at or above 1 dB; worst 0.987991886 dB. Primary bars passed.
// These are historical calibration measurements; later changes need their own gates.
// The values below are the active defaults, including the later release-pot calibration.
// 2026-10-18: slots 79 / 80 add the 4:1 release depth term (see releaseSeconds in
// advanceFetFeedback), and twelve release-side constants were recalibrated with it
// on the probe groups and switches: slots 38, 41, 42, 45, 46, 48, 51, 63, 65, 66, 73,
// 76. Only 73 and 76 (the knee release factor) act at 4:1 alone, and 41
// (fbRatioExponent) acts only away from 4:1; the other nine act at every feedback
// ratio (4:1 to 20:1; All does not run the feedback loop). So 8:1 to 20:1 move too (a few hundredths of a dB on a burst train); the probe groups
// and the music tiers cover every ratio: 0 cells over 1 dB.
// [81] fbSloAttackScale (0 = none), [82] fbSloLoadDb: an SLO detent at the Attack knob's counter-clockwise stop
// (position exactly 0): the loop's attack time times the scale, and the threshold raised by the load, as
// fbAttackLoadDb raises it at the clockwise end. FET BLACK has none (0); FET BLUE's are in kFetBlueDynamicsDefaults.
// [83] fbTailInputLimit (0 = off), [84] fbTailInputReleaseSeconds: the tail population charges no further than its
// settled size at the reduction the signal's own level settles to, read through the voice's static law from a peak
// follower on the detector's magnitude (instant up, down in [84]). Read off the fast charge alone, a tail still below
// its target keeps filling after the signal stops while the charge releases (slowly at the slow end of Release), and
// the reduction deepens; with the limit it stops filling as the signal falls, and lets go with the charge as before.
// While the signal holds, the limit sits above the target (the charge stays under the static reduction by the
// populations' share), so steady signals settle as without it. FET BLUE: on.
// [85] fbTipReferenceShare w (0 = off): the tip balance (advanceFetFeedback) lowers the threshold by the overshoot a
// settled sine's tips need to recharge what the leak removes over a cycle. Told the present leak, it holds a settled
// sine on the static law at every Release position, but the static law was read at the nominal position ('5'), and a
// loop that is not told its leak settles shallower toward the fast end of the knob, where its tips must overshoot
// further, and deeper toward the slow end, on the attack's time scale. With w the threshold is told (1 - leak) +
// w (leak - the nominal position's leak at the same reduction): at 1 a faster release's extra overshoot is the loop's
// own, and the nominal position is untouched (the two leaks agree bit for bit there). No constant of its own: the size
// is the attack and release laws'. FET BLACK: off.
inline constexpr std::array<float, 86> kFetDynamicsDefaults{{
    0.03f, 0.002f, 5.0f, 0.42f, 0.46f, 0.65f,
    0.8f, 0.51f, 0.56f, 0.82f, 1.0f, 0.1f,
    0.7f, 1.0f, 1.24f, 2.0f, -2.17f, 0.82f,
    2.0f, -1.82f, 0.12f, 0.0f, 0.001f, 1.0e-04f,
    0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f,
    0.5f, 1.4f, 0.058f, 1.0f, 7.1905f, 0.7186f,
    0.0f, 1.0f, 0.00010914402f, 1.1925094f, -1.5139126f, 1.3603765f,
    0.2766029f, 1.042f, -0.958f, 0.97411793f, 4.7795935f, 2.1196837f,
    2.6695828f, 25.308857f, 0.010800093f, 0.216308f, 0.6947493f, 0.0f,
    0.0f, 1.7396628f, 13.263551f, 123.67461f, 0.85f, 1.17f,
    0.069821596f, 0.12f, 0.85f, 0.6051005f, 0.0f, 1.2920812f,
    3.8620229f, 5.0f, 1.0f, 0.043717176f, 9.846549f, 3.0176f,
    0.6949f, 5.42614f, 0.72f, -0.1f, 0.4097363f, 0.4356033f,
    0.3f, 5.0102124f, 19.819176f, 0.0f, 0.0f, 0.0f, 0.02f, 0.0f}};

// C5 90-degree all-pass pair: first-order sections (p - s)/(p + s), pole
// frequencies in Hz, mapped per sample rate by the plain bilinear transform.
// Designed over 10 Hz - 80 kHz analog (tools fet/hilbert_design.py, N = 7):
// worst 0.385 degrees from 10 Hz to 20 kHz at 48, 96 and 192 kHz, i.e. at most
// 0.06 dB of envelope ripple on a sine.
inline constexpr std::array<double, 7> kFetAnalyticPolesA{{
    3.76893, 30.4727, 139.863, 647.436, 2958.52, 13204.8, 62040.4}};
inline constexpr std::array<double, 7> kFetAnalyticPolesB{{
    13.3725, 65.3493, 300.898, 1387.14, 6277.39, 27751.4, 217562.0}};

struct FetDynamicsParams
{
    float programmeActiveLevel, programmeSilenceHoldSeconds, programmeExposureCapSeconds;
    std::array<float, 4> exposureLowDrive, exposureMidDrive, exposureHighDrive;
    std::array<float, 3> slowRelease, legacyFastRelease;
    float detChargeOn, detAttackSlowSeconds, detAttackFastSeconds, detReleaseSeconds, detGainDb;
    float detRatioExponent;
    float stateLeakOn, exposureGateDb, exposureDecaySeconds, maxGrDecaySeconds;
    float slowTargetBaseDb, slowTargetSlope;
    float lfHpOn, lfHpFrequency, lfHpQ;
    float detAnalyticOn;
    float fbOn, fbAttackSeconds, fbAttackSlope, fbAttackCurve, fbRatioExponent;
    float fbReleaseSeconds, fbReleaseSlope, fbReleaseCurve;
    float fbSlowShare, fbSlowCeilingDb, fbSlowChargeSeconds, fbSlowReleaseRatio;
    float fbReleaseDepthDb, fbReleaseDepthRatioExponent;
    float fbSlowReleaseKnobExponent, fbAttackLoadDb;
    float fbReleaseDepthFastDb, fbAttackLagRatio, fbAttackLagSlope;
    float fbTurnOnDb, fbTurnOnBiasDb;
    float fbReleaseTaperEnd, fbReleaseTaperCurve;
    float fbReleasePotFixed, fbReleasePotStart, fbReleasePotEnd;
    float fbTailShare, fbTailCurve, fbTailChargeSeconds, fbTailReleaseSeconds;
    float fbTapHighPassHz, fbFloorOnApplied;
    float fbTailRatioExponent;
    float fbTailKneeDb;
    float lfAheadHz, lfAheadQ;
    float fbReleaseKneeShare, fbReleaseKneeDb;
    float fbRectifierImbalance;
    float fbReleaseKneeKnobExponent;
    float fbChargeRiseOnly;
    float fbStaticK3KnobShare;
    float fbReleaseDepthExtraExponent, fbReleaseDepthExtraDb;
    float fbSloAttackScale, fbSloLoadDb, fbTailInputLimit, fbTailInputReleaseSeconds;
    float fbTipReferenceShare;

    static FetDynamicsParams fromArray(const std::array<float, kFetDynamicsDefaults.size()>& v) noexcept
    {
        FetDynamicsParams p{};
        p.programmeActiveLevel = v[0];
        p.programmeSilenceHoldSeconds = v[1];
        p.programmeExposureCapSeconds = v[2];
        for (size_t i = 0; i < 4; ++i)
        {
            p.exposureLowDrive[i] = v[3 + i];
            p.exposureMidDrive[i] = v[7 + i];
            p.exposureHighDrive[i] = v[11 + i];
        }
        for (size_t i = 0; i < 3; ++i)
        {
            p.slowRelease[i] = v[15 + i];
            p.legacyFastRelease[i] = v[18 + i];
        }
        p.detChargeOn = v[21];
        p.detAttackSlowSeconds = v[22];
        p.detAttackFastSeconds = v[23];
        p.detReleaseSeconds = v[24];
        p.detGainDb = v[25];
        p.detRatioExponent = v[26];
        p.stateLeakOn = v[27];
        p.exposureGateDb = v[28];
        p.exposureDecaySeconds = v[29];
        p.maxGrDecaySeconds = v[30];
        p.slowTargetBaseDb = v[31];
        p.slowTargetSlope = v[32];
        p.lfHpOn = v[33];
        p.lfHpFrequency = v[34];
        p.lfHpQ = v[35];
        p.detAnalyticOn = v[36];
        p.fbOn = v[37];
        p.fbAttackSeconds = v[38];
        p.fbAttackSlope = v[39];
        p.fbAttackCurve = v[40];
        p.fbRatioExponent = v[41];
        p.fbReleaseSeconds = v[42];
        p.fbReleaseSlope = v[43];
        p.fbReleaseCurve = v[44];
        p.fbSlowShare = v[45];
        p.fbSlowCeilingDb = v[46];
        p.fbSlowChargeSeconds = v[47];
        p.fbSlowReleaseRatio = v[48];
        p.fbReleaseDepthDb = v[49];
        p.fbReleaseDepthRatioExponent = v[50];
        p.fbSlowReleaseKnobExponent = v[51];
        p.fbAttackLoadDb = v[52];
        p.fbReleaseDepthFastDb = v[53];
        p.fbAttackLagRatio = v[54];
        p.fbAttackLagSlope = v[55];
        p.fbTurnOnDb = v[56];
        p.fbTurnOnBiasDb = v[57];
        p.fbReleaseTaperEnd = v[58];
        p.fbReleaseTaperCurve = v[59];
        p.fbReleasePotFixed = v[60];
        p.fbReleasePotStart = v[61];
        p.fbReleasePotEnd = v[62];
        p.fbTailShare = v[63];
        p.fbTailCurve = v[64];
        p.fbTailChargeSeconds = v[65];
        p.fbTailReleaseSeconds = v[66];
        p.fbTapHighPassHz = v[67];
        p.fbFloorOnApplied = v[68];
        p.fbTailRatioExponent = v[69];
        p.fbTailKneeDb = v[70];
        p.lfAheadHz = v[71];
        p.lfAheadQ = v[72];
        p.fbReleaseKneeShare = v[73];
        p.fbReleaseKneeDb = v[74];
        p.fbRectifierImbalance = v[75];
        p.fbReleaseKneeKnobExponent = v[76];
        p.fbChargeRiseOnly = v[77];
        p.fbStaticK3KnobShare = v[78];
        p.fbReleaseDepthExtraExponent = v[79];
        p.fbReleaseDepthExtraDb = v[80];
        p.fbSloAttackScale = v[81];
        p.fbSloLoadDb = v[82];
        p.fbTailInputLimit = v[83];
        p.fbTailInputReleaseSeconds = v[84];
        p.fbTipReferenceShare = v[85];
        return p;
    }
};

// FET BLUE's dynamics constants (mode 4; kFetDynamicsDefaults is FET BLACK's). The same feedback loop, refitted
// 2026-10-26 to a second reference FET compressor on its own probes (tools fet_ae/dyn_fit.py, variant A; report
// handoff/reports/fet-ae-step1-2026-10-26.md), music never fitted: the 30-clip corpus at 4:1 and 20:1, light to
// heavy drive, then reads 0 of 180 cells over 1 dB; the release and tail constants refitted on 2026-10-27 with the
// tail limit, and the tip balance referred to the nominal Release (the last two items). Twenty-six slots differ
// from FET BLACK's:
// - [45] fbSlowShare 0: the reference has no release memory, and with the population on, the fit built its slow
//   attack creep from it, which also charges over a long passage (its staircase statics sat 0.6 dB deeper than its
//   own 1 s bursts at the same level, where the reference's agree to 0.06 dB).
// - [38]-[42], [49], [52], [53], [63], [65], [66], [70], [73], [74]: the attack and release laws, the ratio
//   exponent of the attack, the depth law and the tail population, fitted on the reference's 4 kHz bursts (4:1 at
//   every Attack and Release position, 20:1, 0 dBFS bursts) and its 1 kHz staircases (4:1, 8:1, 20:1).
// - [34], [35], [71], [72]: the two low-frequency sections, fitted to its 2-2000 Hz response below threshold
//   (tools fet_ae/lf.py), within 0.003 dB and 0.03 degrees from 2.5 Hz to 1 kHz: a Butterworth 4.20 Hz Q 0.708 and
//   a resonant 3.77 Hz Q 1.675 (+2.3 dB at 5 Hz). The sweep reads only their product; the order is the amplifier's
//   DC step at a burst's end (Ratio None, 0 dBFS, Input .375), which rings out through the resonant one alone
//   (residual 0.9e-3 of a 7.0e-3 transient; through the Butterworth 3.4e-3, through both 6.7e-3): it is C4, behind
//   the gain element and the amplifier, and the Butterworth sits ahead.
// - [78] fbStaticK3KnobShare 0: FET BLACK's measured static cubic belongs to its colour stage (FetVoice).
// - [81] fbSloAttackScale 3.988, [82] fbSloLoadDb 1.789: the Attack knob's SLO detent (its counter-clockwise stop),
//   a slower attack and a raised threshold, fitted to the reference's Attack-0 bursts alone (4:1, Input .5; tools
//   fet_ae/slo_fit.py): attack / hold / release 0.54 / 0.13 / 0.45 dB rms (6.4 dB worst frame without the detent,
//   1.4 dB rms hold with the attack scale alone: the reference settles 1.3 dB shallower at SLO).
// - [83] fbTailInputLimit on, [84] 0.036 s: the reference never deepens its reduction after a burst ends (171 ms of
//   1 kHz at 0.8 peak, Input .5, every single button, Release 0 / '5' / 1; tools fet_ae/release0.py), where the tail
//   still filling from the fast charge deepened FET BLUE's by up to 1.2 dB at Release 0; nor does it hold its
//   reduction up after a short burst (300 ms after 30 / 100 / 300 ms bursts at Release '5' it reads within 0.5 dB
//   of one level, where FET BLUE read 2.6 dB deeper after the shortest). With
//   the limit, [42], [49], [50], [53], [63], [65], [66], [70] and [73] (and FetVoice's release scales) refitted on
//   those probes, the bursts above and the corpus's FIT clips at 2:1 / 4:1 / 20:1, light to heavy (blue_fit.py,
//   attack constants held): probe cells with a frame 1 dB off 25 -> 21 of 80, release 0.95 -> 0.30 dB rms; corpus
//   pooled 0.372 -> 0.334 dB, 0 of 270 cells over 1 dB (VAL and SEALED clips never fitted). The fit also turned
//   the 4:1 knee release mostly off ([73] 15.2 -> 0.34) and made the depth law's ratio exponent [50] 1.09.
// - [85] fbTipReferenceShare 1: the reference settles shallower at fast Release and deeper at slow within 20 ms of an
//   onset (171 ms 1 kHz bursts at 0.8 peak, Input .5, Attack .126: Release 1 sits 1.83 / 1.89 / 1.42 / 1.14 dB
//   shallower than Release 0 20 ms in at 2:1 / 4:1 / 8:1 / 20:1, where FET BLUE's tip balance held -0.04 to +0.23);
//   on programme at Release 1 FET BLUE read deeper on every clip, about 1 dB at 2:1 and 4:1. On, nothing refitted
//   (tools fet_ae/tipref.py, music_release.py; captures never fitted): the corpus at Release 1, 2:1 to 20:1, light to
//   heavy, 139 -> 13 of 360 cells over 1 dB; at Release 0 2 -> 4 of 360 (two cells at 1.01 / 1.03, pooled 0.40 ->
//   0.38); probe cells with a frame 1 dB off 11 -> 8 of 65; at Release .666 every render is unchanged bit for bit.
//   Release 0's hold after a long hot burst (testFetBlueReleasesWithoutDeepening's rows) reads 0.4 dB deeper: the
//   tail's creep there is a separate element, open.
// FET BLACK's [43], [44] (unused while the Release pot law, [60], is on) and [54], [55] (no lag) are kept: the
// fitted values there rendered byte-identically.
inline constexpr std::array<float, 86> kFetBlueDynamicsDefaults{{
    0.03f, 0.002f, 5.0f, 0.42f, 0.46f, 0.65f,
    0.8f, 0.51f, 0.56f, 0.82f, 1.0f, 0.1f,
    0.7f, 1.0f, 1.24f, 2.0f, -2.17f, 0.82f,
    2.0f, -1.82f, 0.12f, 0.0f, 0.001f, 1.0e-04f,
    0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f,
    0.5f, 1.4f, 0.058f, 1.0f, 3.7668848f, 1.67501724f,
    0.0f, 1.0f, 8.23298979e-05f, 1.32719922f, -1.35175562f, 0.86418885f,
    0.460866213f, 1.042f, -0.958f, 0.0f, 4.7795935f, 2.1196837f,
    2.6695828f, 71.9732056f, 1.08718872f, 0.216308f, -0.214574963f, 23.4785976f,
    0.0f, 1.7396628f, 13.263551f, 123.67461f, 0.85f, 1.17f,
    0.069821596f, 0.12f, 0.85f, 0.958768249f, 0.0f, 0.414279848f,
    0.790935993f, 5.0f, 1.0f, 0.043717176f, 16.7711658f, 4.19861269f,
    0.70767647f, 0.335850686f, 0.543351114f, -0.1f, 0.4097363f, 0.4356033f,
    0.0f, 5.0102124f, 19.819176f, 3.98787689f, 1.789222f, 1.0f, 0.0355880819f, 1.0f}};

// The two FET modes are one engine with two measured voices: FET BLACK (mode 1), the reference the C6 campaign
// fitted, and FET BLUE (mode 4), measured 2026-10-26/27 (tools fet_ae/: laws.py, tables.py, static_fit.py). A
// voice holds what differs between them besides their dynamics constants: the settled sine law per Ratio position
// (fetReferenceReductionDb: reduction slope, threshold and quadratic knee width in the driven level at the
// reference threshold control, -10 dB, and the detector plateau), each position's scale on the loop's attack and
// release times, and the Input and Output pot laws (dB relative to the clockwise stop at the 17 nodes k / 16, plus
// the absolute trims).
inline constexpr int kFetVoiceRatios = 11;   // the most Ratio positions a voice has (FET BLUE's)
struct FetVoice
{
    int ratioCount;
    // The 4:1 position: the depth and tail laws read every ratio relative to it, and the 4:1-only terms (the
    // release depth extra, the knee release, the knee correction) run there.
    int unitRatio;
    int allButtons;      // the All-buttons position, which keeps the feed-forward arm (-1: none)
    int noReduction;     // the position that takes the gain element out (-1: none)
    // FET BLACK's measured chain: the C6 campaign's colour stage, its 4:1 knee correction and the output stage's
    // convolution and hardware gain. FET BLUE's are not these.
    bool measuredChain;
    std::array<float, kFetVoiceRatios> reductionSlopes, thresholdsDb, kneeWidthsDb, maximumDetectorLevelsDb;
    std::array<float, kFetVoiceRatios> attackScales, releaseScales;
    // Each position's linear gain re the 4:1's (the ratio network loads the circuit), applied after the gain element.
    std::array<float, kFetVoiceRatios> ratioGainsDb;
    std::array<float, 17> inputRelativeDb, outputRelativeDb;
    float inputTrimDb, outputTrimDb;
    // The side-chain filter the SC Filter switch puts ahead of the detector's rectifier (0 sections: no switch): an
    // analog prototype, gain * prod (s + 2 pi zero) / (s + 2 pi pole), its corners prewarped to the loop's rate.
    int scFilterSections;
    std::array<float, 3> scFilterZerosHz, scFilterPolesHz;
    float scFilterGainDb;
    // The amplifier behind the gain element (0 pieces: none, the signal passes): x, the signal over the driven level
    // of a 0 dBFS sine at the reference Input (amplifierReferenceDb), through a cubic per piece between breakpoints
    // (c0 + c1 d + c2 d^2 + c3 d^3, d = x - break), and the end pieces' tangents past the ends.
    int amplifierPieces;
    const float* amplifierBreaks;                  // amplifierPieces + 1
    const std::array<float, 4>* amplifierCubics;
    float amplifierReferenceDb;
};

// FET BLACK: Ratio 4:1, 8:1, 12:1, 20:1, All. The settled law collapses onto one input axis whether the level is
// reached with the source or the Input control; a quadratic knee fits it to 0.032 dB or better for every button.
// These are reduction slopes (1 - 1/R), the feed-forward equivalent of the feedback law, not the nominal ratios.
// At the clockwise Input stop the unit stops adding reduction near the top of every ratio's detector domain (the
// plateau). Input / Output: the native reference's pots, 2026-09-16, 17 nodes (fet-programme-20260911/
// HYPOTHESES.md H15, make_fet_taper_core.py: taper-sweep/native-90 Input at -90 dBFS, taper-sweep/native Output at
// -54 dBFS); sweep residual rounds to 0.000 dB, old-node agreement <= .002 / .01 dB. Input 0 is -41.95 dB, not mute.
// The Input trim, 39.850671 = 40.0 - 0.149329, removes the vintage chain's frequency-flat +0.149329 dB excess ahead
// of the detector, where it was measured to live (see fetInputGainDb); the Output trim includes the measured linear
// loss of the convolution chain.
inline constexpr FetVoice kFetBlackVoice{
    5, 0, 4, -1, true,
    {{0.82857823f, 0.90319133f, 0.93707197f, 0.96619657f, 0.95953144f}},
    {{-8.34012099f, -5.90065303f, -4.47645544f, -2.66788769f, -4.74344379f}},
    {{14.12963324f, 7.19106925f, 1.87103644f, 4.83803856f, 4.87980041f}},
    {{38.72f, 38.85f, 39.00f, 39.09f, 38.23f}},
    {{1.0f, 0.65f, 0.45f, 0.25f, 0.90f}},
    {{1.0f, 0.875f, 0.842f, 0.762f, 1.0f}},
    {{}},
    {{-41.949972f, -40.349972f, -37.919972f, -35.199972f,
      -27.579973f, -25.519973f, -23.209974f, -17.159975f,
      -13.389977f, -9.519981f, -8.019983f, -6.429985f,
      -4.549989f, -0.419998f, -0.090000f, -0.030000f,
      0.000000f}},
    {{-99.990001f, -91.940000f, -66.070001f, -56.780000f,
      -47.990000f, -39.920000f, -33.170000f, -27.200001f,
      -21.810001f, -16.860000f, -12.900000f, -9.240000f,
      -6.060000f, -3.500000f, -1.529999f, -0.150000f,
      0.000000f}},
    39.850671f, 4.360566f,
    0, {{}}, {{}}, 0.0f,
    0, nullptr, nullptr, 0.0f};

// FET BLUE's amplifier, behind the gain element and ahead of the Output pot, read off the reference's waveform with
// the gain element out (Ratio None; tools fet_ae/preamp_curve.py): 1 kHz staircases at Input .375 / .5 / .625 / .75 /
// 1, drive -40..+21.3 dB, every step's output against its input, one cubic B-spline (knots log-spaced to 0.005)
// fitted with a free DC per step (the low section behind it removes the even term's DC, which moves with the
// level) and every level weighted alike. The sines it predicts: gain change, H2, H3 within 0.01 / 0.1 / 0.1 dB of
// the reference from -40 to 0 dBFS at Input .375 (-0.00 / -64.6 / -74.6 dBc at -40, -1.43 / -33.3 / -27.6 at 0),
// H5 within 2.7 dB; its DC step at 0 dBFS (-0.023) is the one the post-burst transient needs (-0.025, below).
// Memoryless: the reference's H2 / H3 are the same at 50 Hz..5 kHz and sit at a curve's own phase (-179 deg).
inline constexpr std::array<float, 59> kFetBlueAmplifierBreaks{{
    -11.6144857f, -8.89057827f, -6.80549955f, -5.20942736f, -3.98767734f, -3.05246019f,
    -2.33657646f, -1.78858674f, -1.36911523f, -1.04802108f, -0.802232027f, -0.614087164f,
    -0.470067263f, -0.359823883f, -0.275435567f, -0.210838541f, -0.161391258f, -0.123540685f,
    -0.0945670828f, -0.0723885745f, -0.0554115139f, -0.0424160287f, -0.0324683338f, -0.0248536393f,
    -0.0190247949f, -0.0145629719f, -0.0111475652f, -0.00853316206f, -0.00653190725f, 0.0f,
    0.00653190725f, 0.00853316206f, 0.0111475652f, 0.0145629719f, 0.0190247949f, 0.0248536393f,
    0.0324683338f, 0.0424160287f, 0.0554115139f, 0.0723885745f, 0.0945670828f, 0.123540685f,
    0.161391258f, 0.210838541f, 0.275435567f, 0.359823883f, 0.470067263f, 0.614087164f,
    0.802232027f, 1.04802108f, 1.36911523f, 1.78858674f, 2.33657646f, 3.05246019f,
    3.98767734f, 5.20942736f, 6.80549955f, 8.89057827f, 11.6144857f}};
inline constexpr std::array<std::array<float, 4>, 58> kFetBlueAmplifierCubics{{
    {{-1.20368552f, 0.0141279018f, -0.0104253301f, 0.00192192779f}},
    {{-1.20371187f, 0.000112858092f, 0.00528013334f, -0.00192120764f}},
    {{-1.19793665f, -0.00292581227f, -0.00673747435f, 0.00396597525f}},
    {{-1.20364451f, 0.00587651785f, 0.0122524705f, -0.010114219f}},
    {{-1.19662094f, -0.00947625097f, -0.0248186793f, 0.028182039f}},
    {{-1.2041384f, 0.0180487018f, 0.0542502962f, -0.0886189789f}},
    {{-1.19592774f, -0.0405263565f, -0.136072338f, 0.365822673f}},
    {{-1.19879854f, 0.13990289f, 0.465328932f, -0.0956353694f}},
    {{-1.06529438f, 0.479804367f, 0.344980001f, -0.253374279f}},
    {{-0.884052098f, 0.62297672f, 0.100908972f, 0.300038844f}},
    {{-0.720379949f, 0.726959407f, 0.322147727f, -0.252162099f}},
    {{-0.573882103f, 0.821401834f, 0.179818675f, 0.169156387f}},
    {{-0.451348841f, 0.883722544f, 0.252904326f, -0.286554545f}},
    {{-0.351234555f, 0.929036558f, 0.158132121f, 0.0513582304f}},
    {{-0.271677703f, 0.956822813f, 0.171134233f, -0.313281536f}},
    {{-0.209240153f, 0.975010574f, 0.110423073f, -0.0236176327f}},
    {{-0.160761386f, 0.985757589f, 0.106919587f, -0.350900561f}},
    {{-0.123315752f, 0.992343366f, 0.0670742244f, -0.0560780987f}},
    {{-0.0945090503f, 0.996088862f, 0.062199872f, -0.378398508f}},
    {{-0.0723908097f, 0.998289526f, 0.0370229222f, -0.0753105581f}},
    {{-0.055432491f, 0.99948144f, 0.0331872664f, -0.418778539f}},
    {{-0.0424390621f, 1.00013185f, 0.0168605782f, 0.0151019692f}},
    {{-0.0324883722f, 1.00047183f, 0.0173112676f, -0.78293556f}},
    {{-0.0248694271f, 1.00059927f, -0.000574176491f, 1.15539646f}},
    {{-0.0190368798f, 1.00071025f, 0.019629702f, -4.73978949f}},
    {{-0.0145719172f, 1.00060236f, -0.0438146181f, 16.5043087f}},
    {{-0.0111543061f, 1.00088072f, 0.125292167f, -42.812088f}},
    {{-0.00853751041f, 1.00065792f, -0.210491925f, 49.4944534f}},
    {{-0.0065353848f, 1.00041008f, 0.0866611376f, -10.401165f}},
    {{0.0f, 1.00021088f, -0.117157191f, -10.6995697f}},
    {{0.00652530417f, 0.997310877f, -0.326822966f, 51.7204208f}},
    {{0.00852028374f, 0.996624172f, -0.0163057111f, -43.6103821f}},
    {{0.0111249695f, 0.995644689f, -0.358350992f, 17.0355816f}},
    {{0.0145219993f, 0.993793011f, -0.183800668f, -4.50339556f}},
    {{0.0189520698f, 0.991883874f, -0.244080752f, 1.2887603f}},
    {{0.0247255694f, 0.989169836f, -0.221544802f, -0.368999153f}},
    {{0.0322447866f, 0.985731661f, -0.229974255f, 0.0654852986f}},
    {{0.0420278497f, 0.981175661f, -0.228019968f, 0.0142680453f}},
    {{0.054740224f, 0.975256443f, -0.227463707f, -0.0501541309f}},
    {{0.0712314025f, 0.967489719f, -0.230018109f, 0.0423853248f}},
    {{0.0925762132f, 0.95734936f, -0.22719799f, -0.0406022556f}},
    {{0.120122351f, 0.944081604f, -0.230727166f, 0.0267055184f}},
    {{0.155527279f, 0.926730096f, -0.227694705f, -0.0142254457f}},
    {{0.200793117f, 0.904107988f, -0.229804933f, 0.0147462357f}},
    {{0.258240849f, 0.874603152f, -0.226947248f, -0.00261329487f}},
    {{0.330429405f, 0.836243927f, -0.227608845f, 0.026358353f}},
    {{0.419888794f, 0.787020206f, -0.218891338f, 0.00124553929f}},
    {{0.528698921f, 0.724048316f, -0.218353197f, 0.041876208f}},
    {{0.657474399f, 0.64633131f, -0.194716811f, 0.00976541732f}},
    {{0.804717243f, 0.552382648f, -0.187516108f, 0.0555291027f}},
    {{0.964589238f, 0.44913739f, -0.134025902f, 0.0185337719f}},
    {{1.13077486f, 0.346480727f, -0.110702731f, 0.0510140173f}},
    {{1.29579425f, 0.271110207f, -0.0268372502f, 0.00122115074f}},
    {{1.4765718f, 0.234562993f, -0.024214644f, -0.037167009f}},
    {{1.64435875f, 0.0917488337f, -0.128492311f, 0.0449903496f}},
    {{1.6467036f, -0.0207544584f, 0.0364086069f, -0.0113838399f}},
    {{1.66004133f, 0.00846791547f, -0.0180996731f, 0.00519200508f}},
    {{1.6460737f, 0.000706986175f, 0.0143775446f, -0.00535501074f}}}};

// FET BLUE: Ratio None, 2:1, 4:1, 8:1, 20:1 and the two-button pairs 2:1+4:1, 4:1+8:1, 8:1+20:1, 2:1+8:1,
// 4:1+20:1, 2:1+20:1, in the reference's own order. Its staircases at Input .25 / .375 / .5 / .625 fall on one curve
// per position in the driven level within 0.02 dB (the knee onsets of 20:1 and 2:1+20:1 within 0.4 dB, the
// staircase's 2 dB grid), so the law reads the driven signal alone, as FET BLACK's does; the quadratic knee fits
// them within 0.11 / 0.24 dB rms / worst at 2:1, 0.11 / 0.26 at 4:1, 0.05 / 0.13 at 8:1, 0.05 / 0.12 at 20:1 and
// 0.11-0.17 / 0.29-0.53 for the pairs (tools fet_ae/static_fit.py). No row reaches a plateau (the hottest drives
// 30.2 dB): 39 dB, inside FET BLACK's 38.2-39.1, stands in for it. None keeps the threshold out of reach.
// Attack scales: 4:1, 8:1 and 20:1 as FET BLACK's, which the dynamics fit ran with; 2:1 and the pairs, not yet
// measured, take the single button nearest them in reduction slope. Release scales: fitted with the tail limit
// (kFetBlueDynamicsDefaults, 2026-10-27) on bursts at every single button: 2:1 1.00, 8:1 1.01, 20:1 0.92; each pair
// takes its larger button's, not yet measured.
// Linear gain per position re 4:1: the first step of every staircase (-60 dBFS) at Input .25 / .375 / .5 / .625
// agrees within 0.003 dB (tools fet_ae/ratio_gain.py); the pairs sit up to 1.75 dB above the single buttons.
// Input / Output: both pots are piecewise linear on the same 17 nodes (every 1/64 position lies on the line between
// its 1/16 neighbours to the 0.01 dB the capture reads; tools fet_ae/tables.py). The Input trim puts its drive at
// Input .375 where FET BLACK's is (37.940697 = 39.850671 - 1.909974; the laws read -21.30 and -23.209974 dB there),
// the coordinate the static law above was fitted in.
// SC Filter: a +3 dB/octave tilt on the detector from 30 Hz to 16 kHz, 0 dB near 285 Hz (the settled reduction of
// -10 dBFS tones at 4:1, Input .5, on against off, over the law's 0.853 slope: -9.3 dB at 30 Hz, +5.8 at 1 kHz,
// +19 at 16 kHz; tools fet_ae/scf.py). Three shelves fit it within 0.17 dB to 6 kHz (scf_fit.py; above 8 kHz the
// reading itself wobbles by 1 dB) and hold their gain above 13 kHz instead of rising on into the oversampled band.
// The gain is set through the loop itself: the fit's 20.39 dB took the law's 0.853 slope, and on the core the switch's
// effect read 0.3-0.5 dB short at every frequency; 20.9 dB puts 100 Hz / 300 Hz / 1 kHz / 4 kHz within 0.09 dB of
// the reference (core test --fet-blue).
inline constexpr FetVoice kFetBlueVoice{
    11, 2, -1, 0, false,
    {{0.5f, 0.79688776f, 0.8532685f, 0.9116469f, 0.95496702f, 0.83355302f, 0.90210825f, 0.94584674f,
      0.88981092f, 0.93952757f, 0.93441933f}},
    {{200.0f, -8.246829f, -7.6200333f, -5.9402471f, -3.4424005f, -7.8006115f, -8.362071f, -7.4400568f,
      -7.935461f, -7.1974735f, -5.4538617f}},
    {{1.0f, 8.7895947f, 7.058898f, 3.0427685f, 1.3793365f, 8.6681347f, 5.0340247f, 3.7372067f,
      5.7460027f, 4.3816304f, 5.0832062f}},
    {{239.0f, 39.0f, 39.0f, 39.0f, 39.0f, 39.0f, 39.0f, 39.0f, 39.0f, 39.0f, 39.0f}},
    {{1.0f, 1.0f, 1.0f, 0.65f, 0.25f, 1.0f, 0.65f, 0.25f, 0.65f, 0.25f, 0.25f}},
    {{1.0f, 0.999883831f, 1.0f, 1.01148355f, 0.923112452f, 1.0f, 1.01148355f, 0.923112452f, 1.01148355f,
      0.923112452f, 0.923112452f}},
    {{-0.1006f, -0.1002f, 0.0f, 0.1003f, -0.3004f, -0.0386f, 1.2816f, 1.7017f, 1.7516f, 1.7517f, 1.3517f}},
    {{-36.30f, -34.50f, -30.50f, -28.80f, -26.10f, -22.10f, -21.30f, -16.70f, -13.50f,
      -9.20f, -7.70f, -6.20f, -4.10f, -0.50f, -0.30f, -0.10f, 0.0f}},
    {{-100.0f, -61.90f, -45.80f, -40.30f, -35.70f, -31.40f, -28.70f, -26.90f, -26.00f,
      -25.20f, -20.80f, -18.10f, -14.80f, -9.00f, -3.40f, -0.01f, 0.0f}},
    37.940697f, 4.360566f,
    3, {{40.489922f, 373.68417f, 3357.3923f}}, {{126.97172f, 1118.7278f, 13168.484f}}, 20.9f,
    static_cast<int>(kFetBlueAmplifierCubics.size()), kFetBlueAmplifierBreaks.data(), kFetBlueAmplifierCubics.data(),
    16.640697f};

class MultiCompModes
{
public:
    static constexpr int kChannels = 2;

    void prepare(double sampleRate, int maxBlock, int oversamplingFactor)
    {
        // optoStageBlock(): stage input, two gain curves and the stage's own scratch, for 4x phases.
        const int phases = 4 * std::max(1, maxBlock);
        optoStageScratch.assign(static_cast<size_t>(3 * phases + OptoStageOversampler::blockScratch(phases)), 0.0f);
        const double newFs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int newFactor = oversamplingFactor == 4 ? 4 : (oversamplingFactor == 2 ? 2 : 1);
        // The skip is only safe once the one-time work below has actually run.
        // Without the flag, a first prepare whose arguments happen to equal the
        // member defaults (48 kHz, factor 1: a 48 kHz host with oversampling
        // Off) returns here with digitalDelay still empty and the hardware
        // stages unprepared, and Digital mode then indexes an empty ring buffer.
        if (prepared && newFs == fs && newFactor == osFactor) return;
        fs = newFs;
        osFactor = newFactor;
        const double modeRate = fs * osFactor;
        transientShaper.prepare(modeRate);
        lookupTables.prepare();
        for (auto& d : opto) d = OptoState{};
        configureOptoStage();
        optoCell.reset();
        for (auto& shelf : optoShelf) shelf.reset();
        for (auto& filter : optoPreHighPass) filter.reset();
        for (auto& filter : optoPostHighPass) filter.reset();
        for (auto& d : fet) d = FETState{};
        updateFetDynamicsCoefficients();
        for (auto& d : vca) d = VCAState{};
        for (auto& d : bus) d = BusState{};
        for (auto& filter : busFeedbackCoupling) filter.reset();
        for (auto& d : fetBlue) d = FETState{};
        for (auto& d : studioVca) d = studioVcaLaw::GainComputer{};
        for (auto& d : digital) d = DigitalState{};
        for (auto& filter : busSidechainHighPass) filter.reset();
        for (auto& filter : busSidechainLowShelf) filter.reset();
        for (auto& filter : busSidechainHighShelf) filter.reset();
        prepareHardware(modeRate);
        for (int ch = 0; ch < kChannels; ++ch)
        {
            inputTransformerFet[ch].reset(); outputTransformerFet[ch].reset();
            inputTransformerBus[ch].reset(); outputTransformerBus[ch].reset();
        }
        for (auto& delay : digitalDelay)
            delay.assign(static_cast<size_t>(std::ceil(fs * 0.01 * 4.0)) + 2, 0.0f);
        digitalWrite.fill(0);
        prepared = true;
    }

    // Oversampling can be automated without reallocating or resetting the
    // mode state.  The host-rate buffers are provisioned for the maximum
    // factor in prepare(); only rate-dependent coefficients are refreshed at
    // this block boundary.
    void setRate(double sampleRate, int oversamplingFactor) noexcept
    {
        const double newFs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int newFactor = oversamplingFactor == 4 ? 4 : (oversamplingFactor == 2 ? 2 : 1);
        if (newFs == fs && newFactor == osFactor) return;
        const double oldModeRate = fs * osFactor;
        fs = newFs;
        osFactor = newFactor;
        const float sr = static_cast<float>(fs * osFactor);
        // A new rate starts at the first phase of a host sample. Keep the
        // control envelope, but realign its sampling clock.
        for (auto& d : bus) d.detectorPhase = 0;
        for (auto& d : vca) d.phase = 0;
        transientShaper.setRate(sr);
        updateRateCoefficients(sr);
        // The FET loop's high-passes are designed per rate as well.
        updateFetDynamicsCoefficients();
        // The Opto fields below store elapsed or remaining samples. Preserve
        // their physical time when runtime oversampling changes the rate.
        const double counterScale = static_cast<double>(sr) / oldModeRate;
        const auto scaleCounter = [counterScale](int value, int maximum) {
            if (value <= 0) return 0;
            return std::min(maximum, std::max(1, static_cast<int>(
                std::lround(static_cast<double>(value) * counterScale))));
        };
        optoCell.scaleCounters(scaleCounter);
        for (auto& d : opto)
        {
            d.toneInputPeakHold = scaleCounter(
                d.toneInputPeakHold, optoTonePeakHoldSamples);
            d.tonePeriodSamples = scaleCounter(
                d.tonePeriodSamples, std::max(1, static_cast<int>(
                    std::lround(0.050 / optoInvSampleRate))));
            d.broadbandSupportSamples = scaleCounter(
                d.broadbandSupportSamples, optoBroadbandWarmupSamples);
            d.sineSupportSamples = scaleCounter(
                d.sineSupportSamples, optoGainRippleWarmupSamples);
            d.shortEventAgeSamples = scaleCounter(
                d.shortEventAgeSamples, std::max(1, static_cast<int>(
                    std::lround(0.100 / optoInvSampleRate))));
            d.shortEventSilenceSamples = scaleCounter(
                d.shortEventSilenceSamples, optoShortEventRecurringMaxSamples);
            d.longEventReleaseAgeSamples = scaleCounter(
                d.longEventReleaseAgeSamples,
                std::max(1, static_cast<int>(std::lround(
                    4.0 / optoInvSampleRate))));
        }
        updateHardwareRate(sr);
        selectHardwareGains();
        configureOptoStage();
    }

    void clearVcaState(int channel = -1) noexcept
    {
        for (int ch = 0; ch < kChannels; ++ch)
            if (channel < 0 || channel == ch) vca[ch] = VCAState{};
    }

    // Rate caching is independent of a host lifecycle reset. Clear every BUS
    // history even when prepare() can reuse coefficients at the same rate.
    void clearBusState() noexcept
    {
        for (auto& d : bus) d = BusState{};
        for (auto& filter : busFeedbackCoupling) filter.reset();
        for (auto& filter : busSidechainHighPass) filter.reset();
        for (auto& filter : busSidechainLowShelf) filter.reset();
        for (auto& filter : busSidechainHighShelf) filter.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inputTransformerBus[ch].reset();
            outputTransformerBus[ch].reset();
        }
        busConvolution.reset();
    }

    // The mode being switched to starts from rest, like a freshly inserted
    // compressor. Nothing advances a mode while another one is selected, so
    // without this it resumed the reduction it held when it was left
    // (measured: FET re-entered with about 12 dB of stale reduction, still
    // 7 dB after 2 s). VCA has its own entry lifecycle in MultiCompDSP, and
    // Multiband's band state lives there too.
    void clearModeState(MultiCompMode mode) noexcept
    {
        switch (mode)
        {
            case MultiCompMode::Opto:
                for (auto& d : opto) d = OptoState{};
                configureOptoStage();
                optoCell.reset();
                for (auto& shelf : optoShelf) shelf.reset();
                for (auto& filter : optoPreHighPass) filter.reset();
                for (auto& filter : optoPostHighPass) filter.reset();
                break;
            case MultiCompMode::FET:
                for (auto& d : fet) d = FETState{};
                fetConvolution.reset();
                transientShaper.reset();
                for (int ch = 0; ch < kChannels; ++ch)
                {
                    inputTransformerFet[ch].reset();
                    outputTransformerFet[ch].reset();
                }
                break;
            case MultiCompMode::Bus:
                clearBusState();
                break;
            case MultiCompMode::FetBlue:
                for (auto& d : fetBlue) d = FETState{};
                break;
            case MultiCompMode::StudioVCA:
                for (auto& d : studioVca) d = studioVcaLaw::GainComputer{};
                break;
            case MultiCompMode::Digital:
                for (auto& d : digital) d = DigitalState{};
                for (auto& delay : digitalDelay)
                    std::fill(delay.begin(), delay.end(), 0.0f);
                digitalWrite.fill(0);
                break;
            case MultiCompMode::VCA:
            case MultiCompMode::Multiband:
                break;
        }
    }

    void reset() noexcept
    {
        for (auto& d : opto) d = OptoState{};
        configureOptoStage();
        optoCell.reset();
        for (auto& shelf : optoShelf) shelf.reset();
        for (auto& filter : optoPreHighPass) filter.reset();
        for (auto& filter : optoPostHighPass) filter.reset();
        for (auto& d : fet) d = FETState{};
        for (auto& d : vca) d = VCAState{};
        clearBusState();
        for (auto& d : fetBlue) d = FETState{};
        for (auto& d : studioVca) d = studioVcaLaw::GainComputer{};
        for (auto& d : digital) d = DigitalState{};
        for (auto& filter : busSidechainHighPass) filter.reset();
        for (auto& filter : busSidechainLowShelf) filter.reset();
        for (auto& filter : busSidechainHighShelf) filter.reset();
        for (auto& delay : digitalDelay)
            for (auto& x : delay) x = 0.0f;
        digitalWrite.fill(0);
        resetHardware();
        transientShaper.reset();
    }

    // `external` is the EFFECTIVE sidechain state (the parameter AND a host
    // bus actually feeding us), not the raw parameter. Reading the parameter
    // in here made every detector switch to its external topology the moment
    // the user armed the switch, even with nothing connected, which changed
    // the compression character instead of doing nothing.
    float process(MultiCompMode mode, float input, int ch, const float sc,
                  const MultiCompParameterState& p, float localMix = 1.0f,
                  bool external = false, float optoDetector = 0.0f,
                  bool useOptoDetector = false) noexcept
    {
        ch = std::clamp(ch, 0, 1);
        switch (mode)
        {
            case MultiCompMode::Opto: return processOpto(
                input, ch, sc, p, external, optoDetector, useOptoDetector);
            case MultiCompMode::FET:
                selectFetVoice(0);
                return processFET(input, ch, sc, p, external, useOptoDetector);
            case MultiCompMode::VCA: return processVCA(input, ch, sc, p, external);
            case MultiCompMode::Bus: return processBus(input, ch, sc, p, localMix, external);
            case MultiCompMode::FetBlue:
                selectFetVoice(1);
                return processFET(input, ch, sc, p, external, useOptoDetector);
            // Studio VCA runs a block at a time at the host rate (MultiCompDSP::processStudioVcaRange).
            case MultiCompMode::StudioVCA: return input;
            case MultiCompMode::Digital: return processDigital(input, ch, sc, p, localMix, external);
            case MultiCompMode::Multiband: return input;
        }
        return input;
    }

    // The VCA on one channel for a block: the cell gain of every oversampled
    // phase (envelopes[i * osFactor + phase]), the values processVCA reaches
    // phase by phase, in three passes: the detector, each phase's aligned
    // power, then the gain law on all of them (independent iterations, four
    // at a time). The knobs are block-constant here (the DSP syncs them once
    // per block). `lawScratch` holds numSamples + 64 floats.
    void vcaBlockEnvelopes(int ch, const float* sidechain, int numSamples,
                           const MultiCompParameterState& p, float* envelopes,
                           float* lawScratch) noexcept
    {
        auto& d = vca[static_cast<size_t>(std::clamp(ch, 0, 1))];
        const float thresholdDb = vcaThresholdDb(p);
        const float slope = vcaSlopeFor(p);
        if (d.phase != 0 || numSamples <= 0)
        {   // mid-sample (never in the DSP's block path): phase by phase
            for (int i = 0; i < numSamples; ++i)
                for (int phase = 0; phase < osFactor; ++phase)
                    envelopes[i * osFactor + phase] = advanceVcaEnvelope(d, sidechain[i], thresholdDb, slope);
            return;
        }
        // The phases' read positions in the power history (advanceVcaEnvelope).
        const float upDelay = osFactor == 4 ? 33.25f : osFactor == 2 ? 31.5f : 0.0f;
        std::array<unsigned, 4> whole{};
        std::array<float, 4> fraction{};
        for (int phase = 0; phase < osFactor; ++phase)
        {
            const float delay = std::max(upDelay - static_cast<float>(phase) / osFactor, 0.0f);
            whole[static_cast<size_t>(phase)] = static_cast<unsigned>(delay);
            fraction[static_cast<size_t>(phase)] = delay - static_cast<float>(whole[static_cast<size_t>(phase)]);
        }
        // Pass 1: the detector, one history sample per host sample, kept in
        // order behind the 64 the ring held before the block.
        constexpr int kHistory = 64;
        float* power = lawScratch;
        for (int age = kHistory - 1; age >= 0; --age)
            power[kHistory - 1 - age] = d.rmsHistory[(d.rmsWrite - static_cast<unsigned>(age)) & 63u];
        {   // pushVcaDetector() with the state in registers; the ring takes the block's tail.
            const float rmsCoeff = vcaRmsCoeff, oneMinusCoeff = 1.0f - vcaRmsCoeff;
            const float floorPower = vcaDetectorFloorPower, rise = vcaDetectorRise;
            float rms = d.rms;
            for (int i = 0; i < numSamples; ++i)
            {
                const float previous = std::max(rms, floorPower);
                rms = std::min(previous * rmsCoeff + sidechain[i] * sidechain[i] * oneMinusCoeff,
                               previous * rise);
                if (!std::isfinite(rms)) rms = 0.0f;
                power[kHistory + i] = rms;
            }
            d.rms = rms;
            for (int i = std::max(0, numSamples - kHistory); i < numSamples; ++i)
                d.rmsHistory[(d.rmsWrite + 1u + static_cast<unsigned>(i)) & 63u] = power[kHistory + i];
            d.rmsWrite = (d.rmsWrite + static_cast<unsigned>(numSamples)) & 63u;
        }
        // Pass 2: each phase's aligned power, interpolated between history
        // samples exactly as advanceVcaEnvelope does.
        for (int phase = 0; phase < osFactor; ++phase)
        {
            const float* recent = power + kHistory - static_cast<int>(whole[static_cast<size_t>(phase)]);
            const float f = fraction[static_cast<size_t>(phase)];
            float* aligned = envelopes + phase;
            for (int i = 0; i < numSamples; ++i)
                aligned[i * osFactor] = recent[i] + (recent[i - 1] - recent[i]) * f;
        }
        // Pass 3: the law on every phase, in place.
        vcaLawGains(envelopes, envelopes, numSamples * osFactor, thresholdDb, slope);
        d.envelope = envelopes[numSamples * osFactor - 1];
    }

    // The VCA output gain for a knob value (one-entry cache: the knob moves
    // per host sample only while the auto-makeup ramp runs).
    float vcaOutputGainFor(float outputDb) noexcept
    {
        if (!(outputDb == vcaOutputDb))
        {
            vcaOutputDb = outputDb;
            vcaOutputGain = decibelsToGain(outputDb);
        }
        return vcaOutputGain;
    }

    int oversamplingFactor() const noexcept { return osFactor; }

    float gainReduction(MultiCompMode mode, int ch) const noexcept
    {
        ch = std::clamp(ch, 0, 1);
        switch (mode)
        {
            case MultiCompMode::Opto: return gainToDecibels(optoCell.meterGain(ch))
                - opto[static_cast<size_t>(ch)].meterCorrectionDb;
            case MultiCompMode::FET: return gainToDecibels(
                fet[ch].envelope * fet[ch].kneeGain
                    * fet[ch].recoveryGain);
            case MultiCompMode::VCA: return gainToDecibels(vca[ch].envelope);
            case MultiCompMode::Bus: return gainToDecibels(bus[ch].envelope);
            case MultiCompMode::FetBlue: return gainToDecibels(fetBlue[ch].envelope);
            case MultiCompMode::StudioVCA: return static_cast<float>(studioVca[ch].attack);
            case MultiCompMode::Digital: return gainToDecibels(static_cast<float>(digital[ch].envelope));
            case MultiCompMode::Multiband: break;
        }
        return 0.0f;
    }

    float fetColourGainReduction(int ch) const noexcept
    {
        ch = std::clamp(ch, 0, 1);
        return gainToDecibels(fet[ch].envelope);
    }

    float fetPostBurstRecoveryGain(int ch) const noexcept
    {
        return fet[static_cast<size_t>(std::clamp(ch, 0, 1))].recoveryGain;
    }

    void clearFetPostBurstRecovery(int channel = -1) noexcept
    {
        const auto clear = [](auto& d) noexcept {
            d.programmeExposureSeconds = 0.0f;
            d.programmeMaximumGrDb = 0.0f;
            d.programmeSilentSamples = 0;
            d.kneePeak = 0.0f;
            d.kneeBaseReductionDb = 0.0f;
            d.kneeCorrectionDb = 0.0f;
            d.kneeGain = 1.0f;
            d.recoveryPreviousInstantLevel = 0.0f;
            d.recoveryMaximumLevelDrop = 0.0f;
            d.recoveryMaximumGrDb = 0.0f;
            d.recoveryElapsedSeconds = 0.0f;
            d.recoveryTerminalWeight = 0.0f;
            d.recoveryReleasePosition = 0.5f;
            d.recoveryAttackPosition = 0.5f;
            d.recoveryGain = 1.0f;
            d.recoveryRatioIndex = 0;
            d.recoveryWasSupported = false;
        };
        if (channel >= 0 && channel < 2)
            clear(fet[static_cast<size_t>(channel)]);
        else
            for (auto& d : fet) clear(d);
    }

    void setBusSidechainControls(float highPassFrequency,
                                 float lowFrequency, float lowGain,
                                 float highFrequency, float highGain) noexcept
    {
        const float rate = static_cast<float>(fs * osFactor);
        const float hp = std::clamp(highPassFrequency, 0.0f, 500.0f);
        const float lowF = std::clamp(lowFrequency, 60.0f, 500.0f);
        const float lowG = std::clamp(lowGain, -12.0f, 12.0f);
        const float highF = std::clamp(highFrequency, 2000.0f, 16000.0f);
        const float highG = std::clamp(highGain, -12.0f, 12.0f);
        // renderBusOutput advances this high-pass only while SC FILTER is
        // engaged, so while it is off the filter freezes holding whatever it
        // last ran with. Clear that stale history on the edge back to engaged,
        // before the first newly filtered sample reaches the detector; this
        // mirrors the VCA sidechain tilt's PULL/SC edge in MultiCompDSP.cpp.
        // Retuning alone must NOT reset it: a continuously running filter's
        // state is current, and clearing it injects a larger transient than
        // letting it settle (measured 3x to 12x worse across corner/tone).
        if (hp >= 1.0f && busSidechainHighPassFrequency < 1.0f)
            for (auto& filter : busSidechainHighPass) filter.reset();
        if (rate != busSidechainRate || hp != busSidechainHighPassFrequency)
            for (auto& filter : busSidechainHighPass)
                filter.setHighPass(rate, std::max(hp, 20.0f), 0.5f);
        if (rate != busSidechainRate || lowF != busSidechainLowFrequency
            || lowG != busSidechainLowGain)
            for (auto& filter : busSidechainLowShelf)
                filter.setCoeffs(Biquad::shelfSlope1(rate, lowF, lowG, false));
        if (rate != busSidechainRate || highF != busSidechainHighFrequency
            || highG != busSidechainHighGain)
            for (auto& filter : busSidechainHighShelf)
                filter.setCoeffs(Biquad::shelfSlope1(rate, highF, highG, true));
        busSidechainRate = rate;
        busSidechainHighPassFrequency = hp;
        busSidechainLowFrequency = lowF;
        busSidechainLowGain = lowG;
        busSidechainHighFrequency = highF;
        busSidechainHighGain = highG;
    }

    void processBusPair(float inputLeft, float inputRight,
                        float sidechainLeft, float sidechainRight,
                        const MultiCompParameterState& p, float mix,
                        bool external, float linkAmount,
                        float& outputLeft, float& outputRight) noexcept
    {
        auto& left = bus[0];
        auto& right = bus[1];
        const float sr = static_cast<float>(fs * osFactor);
        const float leftLevel = busDetectorLevel(
            external ? std::abs(left.externalCompressed) : busFeedbackMagnitude(left, 0));
        const float rightLevel = busDetectorLevel(
            external ? std::abs(right.externalCompressed) : busFeedbackMagnitude(right, 1));
        const float linkedLevel = std::max(leftLevel, rightLevel);
        const float amount = std::clamp(linkAmount, 0.0f, 1.0f);
        advanceBusEnvelope(left, leftLevel + (linkedLevel - leftLevel) * amount, p, sr);
        advanceBusEnvelope(right, rightLevel + (linkedLevel - rightLevel) * amount, p, sr);
        const float drive = busDrive(p);
        left.externalCompressed = sidechainLeft * drive * left.detectorEnvelope;
        right.externalCompressed = sidechainRight * drive * right.detectorEnvelope;
        outputLeft = renderBusOutput(inputLeft, 0, p, mix);
        outputRight = renderBusOutput(inputRight, 1, p, mix);
    }

    // Studio VCA at the host rate (the mode's factor is 1; MultiCompDSP::processStudioVcaRange): one channel's gain
    // for n samples from its detector levels (linear peak, before the input gain), as linear factors in `gains`.
    // `levels` is scratch (it ends up holding the levels over threshold in dB). The caller applies the input gain
    // to the audio as well (studioVcaInputGain()).
    void studioVcaGains(int ch, float* levels, int n, const MultiCompParameterState& p, float* gains) noexcept
    {
        const float threshold = p.studioVcaThreshold.load(std::memory_order_relaxed);
        const studioVcaLaw::StaticLaw& law = studioVcaStaticLawFor(p);
        const float inputGain = studioVcaInputGain(p);
        const studioVcaLaw::Coefficients& c = studioVcaCoefficientsFor(p);
        auto& g = studioVca[static_cast<size_t>(std::clamp(ch, 0, 1))];
        // 20 log10(level * input gain) - threshold, four lanes at a time; the floor (-240 dBFS) is far below
        // every setting's knee.
        float* overDb = levels;
        for (int i = 0; i < n; ++i) overDb[i] = std::max(levels[i] * inputGain, 1.0e-12f);
        int k = 0;
        for (; k + 4 <= n; k += 4) fastmath::log2x4(overDb + k, overDb + k);
        for (; k < n; ++k) overDb[k] = fastmath::log2(overDb[k]);
        for (int i = 0; i < n; ++i) overDb[i] = kTwentyLog10Of2 * overDb[i] - threshold;
        // The gain computer feeds back on itself, so it runs sample by sample; its gain in dB becomes a power of
        // two, four lanes at a time.
        g.run(n, [&](int i) noexcept { return law.gainDb(overDb[i]); }, c, gains);
        for (int i = 0; i < n; ++i) gains[i] *= kLog2Of10Over20;
        k = 0;
        for (; k + 4 <= n; k += 4) fastmath::exp2x4(gains + k, gains + k);
        for (; k < n; ++k) gains[k] = fastmath::exp2(gains[k]);
    }

    // Whether the two channels' gain computers hold the same state, as a fully linked pair's do from a reset on.
    // Such a pair needs one computation: channel 0's, which studioVcaShareChannel() then hands to channel 1. The
    // Auto rate state counts even under fixed release, which leaves it alone: a pair whose rates parted in Auto
    // under a partial link or Dual / Mid-Side keeps computing both channels (same output) until a reset.
    bool studioVcaChannelsAgree() const noexcept { return studioVca[0] == studioVca[1]; }
    void studioVcaShareChannel() noexcept { studioVca[1] = studioVca[0]; }

    float studioVcaInputGain(const MultiCompParameterState& p) noexcept
    {
        const float inputDb = p.studioVcaInput.load(std::memory_order_relaxed);
        if (inputDb != studioVcaInputDb)
        {
            studioVcaInputDb = inputDb;
            studioVcaInputGainValue = decibelsToGain(inputDb);
        }
        return studioVcaInputGainValue;
    }

    const studioVcaLaw::StaticLaw& studioVcaStaticLawFor(const MultiCompParameterState& p) noexcept
    {
        const float ratio = p.studioVcaRatio.load(std::memory_order_relaxed);
        if (ratio != studioVcaStaticLawRatio)
        {
            studioVcaStaticLawRatio = ratio;
            studioVcaStaticLaw.set(ratio);
        }
        return studioVcaStaticLaw;
    }

    const studioVcaLaw::Coefficients& studioVcaCoefficientsFor(const MultiCompParameterState& p) noexcept
    {
        const float attack = p.studioVcaAttack.load(std::memory_order_relaxed);
        const float release = p.studioVcaRelease.load(std::memory_order_relaxed);
        const bool autoRelease = p.studioVcaAutoRelease.load(std::memory_order_relaxed);
        const double rate = fs * osFactor;
        if (attack != studioVcaCoefficientAttack || release != studioVcaCoefficientRelease
            || autoRelease != studioVcaCoefficientAuto || rate != studioVcaCoefficientRate)
        {
            studioVcaCoefficientAttack = attack;
            studioVcaCoefficientRelease = release;
            studioVcaCoefficientAuto = autoRelease;
            studioVcaCoefficientRate = rate;
            studioVcaCoefficients = studioVcaLaw::coefficients(attack, release, autoRelease, rate);
        }
        return studioVcaCoefficients;
    }

    // Opto's output-stage oversampling adds a constant latency (host samples).
    int optoStageLatencySamples() const noexcept
    {
        return optoStageLatencyHost(fs);
    }
    // The Oversampling setting's factor (1, 2 or 4). At a fast host it exceeds Opto's mode factor
    // (fixedRateModeFactor()); the output stage oversamples when the setting asks for it. Until it is set the stage
    // follows the mode factor.
    void setOptoSettingFactor(int settingFactor) noexcept
    {
        if (settingFactor == optoSettingFactor) return;
        optoSettingFactor = settingFactor;
        configureOptoStage();
    }

    // OPTO stereo link (see kOptoLinkAtDriveDefault). MultiCompDSP asks which
    // form is active and hands over the link amount once per sample.
    bool optoLinksAtDrive() const noexcept { return optoLinkAtDrive; }

    // One stereo sample of OPTO. With a link amount set the two cells advance together from their linked
    // light (OptoCell::processLightPair) and each channel's audio takes the gain that call hands back;
    // without one, each channel runs on its own as before.
    // What processOptoPreStage() hands the output stage for one phase.
    struct OptoStageFeed
    {
        float input = 0.0f;         // the stage's input
        float settledGain = 1.0f;   // the settled-power correction, before its delay
        float meterGain = 1.0f;     // the gain-law corrections, before their delay
        bool muted = false;         // Gain at zero: the stage runs on silence, the output is 0
    };

    // processOptoLightPair() / process(Opto, ...) up to the output stage, for the
    // DSP's block path: the stage then runs over the block (optoStageBlock()).
    void optoPreStagePair(const std::array<float, 2>& input, const std::array<float, 2>& sidechain,
                          const MultiCompParameterState& p, bool external,
                          const std::array<float, 2>& detector, OptoStageFeed& left, OptoStageFeed& right) noexcept
    {
        if (!optoCell.linked())
        {
            processOptoPreStage(input[0], 0, sidechain[0], p, external, detector[0], true, false, 1.0f, left);
            processOptoPreStage(input[1], 1, sidechain[1], p, external, detector[1], true, false, 1.0f, right);
            return;
        }
        const auto oldGain = optoCell.processLightPair(input, sidechain, external,
            detector, p.optoLimit.load(std::memory_order_relaxed),
            p.optoPeakReduction.load(std::memory_order_relaxed));
        if constexpr (!kOptoToneSynthesisEnabled && !kOptoClassifierCorrectionsEnabled && kOptoPreHighPassAhead)
        {   // processOptoPreStage()'s shipped path for both channels (the element's lanes at once)
            const std::array<float, 2> cellInput{{optoPreHighPass[0].process(input[0]), optoPreHighPass[1].process(input[1])}};
            const std::array<float, 2> audio{{optoShelf[0].process(cellInput[0]), optoShelf[1].process(cellInput[1])}};
            const auto term = optoCell.elementTermPair(cellInput, oldGain, p.optoPeakReduction.load(std::memory_order_relaxed));
            const float gainKnob = p.optoGain.load(std::memory_order_relaxed);
            if (!(gainKnob == optoMakeupKnob))
            {
                optoMakeupKnob = gainKnob;
                optoMakeup = optoKnobToLinearGain(gainKnob);
            }
            const float makeup = optoMakeup;
            OptoStageFeed* feeds[2] = {&left, &right};
            for (size_t c = 0; c < 2; ++c)
            {
                if (makeup == 0.0f)
                    *feeds[c] = OptoStageFeed{0.0f, 1.0f, 1.0f, true};
                else
                {
                    const float compressed = (audio[c] - term[c]) * oldGain[c];
                    feeds[c]->input = (compressed + 0.0f) * makeup;
                    feeds[c]->muted = false;
                    feeds[c]->settledGain = 1.0f;
                    feeds[c]->meterGain = 1.0f;
                }
            }
            return;
        }
        processOptoPreStage(input[0], 0, sidechain[0], p, external, detector[0], true, true, oldGain[0], left);
        processOptoPreStage(input[1], 1, sidechain[1], p, external, detector[1], true, true, oldGain[1], right);
    }
    void optoPreStage(float input, int ch, float sidechain, const MultiCompParameterState& p, bool external,
                      float optoDetector, bool useOptoDetector, OptoStageFeed& feed) noexcept
    {
        processOptoPreStage(input, std::clamp(ch, 0, 1), sidechain, p, external, optoDetector, useOptoDetector,
                            false, 1.0f, feed);
    }
    // optoStageSample() for `count` consecutive phases of one channel (count
    // at most 4 x the prepared block): each stage element over the block, in
    // optoStageSample()'s order per phase.
    void optoStageBlock(int ch, const OptoStageFeed* feeds, int count, float* outputs) noexcept
    {
        if (count <= 0) return;
        const size_t c = static_cast<size_t>(std::clamp(ch, 0, 1));
        optoStageCurveBlock(c, feeds, count, outputs);
        for (int k = 0; k < count; ++k) outputs[k] = optoPostHighPass[c].process(outputs[k]);
        optoStageGainBlock(c, feeds, count, outputs);
    }
    // optoStageBlock() for both channels: each channel's arithmetic unchanged, with the two post high-passes
    // (recursions bound by their own latency) interleaved in one loop.
    void optoStageBlockPair(const OptoStageFeed* feeds0, const OptoStageFeed* feeds1, int count, float* outputs0,
                            float* outputs1) noexcept
    {
        if (count <= 0) return;
        optoStageCurveBlock(0, feeds0, count, outputs0);
        optoStageCurveBlock(1, feeds1, count, outputs1);
        OptoSubsonicHighPass::processPairBlock(optoPostHighPass[0], optoPostHighPass[1], outputs0, outputs1, count);
        optoStageGainBlock(0, feeds0, count, outputs0);
        optoStageGainBlock(1, feeds1, count, outputs1);
    }
    // optoStageBlock()'s steps ahead of and behind the post high-pass.
    void optoStageCurveBlock(size_t c, const OptoStageFeed* feeds, int count, float* outputs) noexcept
    {
        float* stage = optoStageScratch.data();
        float* work = stage + count;
        for (int k = 0; k < count; ++k) stage[k] = feeds[k].muted ? 0.0f : feeds[k].input;
        opto[c].stage.processBlock(stage, outputs, count, optocurve::OutputCurve{}, work);
    }
    void optoStageGainBlock(size_t c, const OptoStageFeed* feeds, int count, float* outputs) noexcept
    {
        auto& d = opto[c];
        float* settled = optoStageScratch.data();
        float* meter = settled + count;
        for (int k = 0; k < count; ++k)
        {
            const bool muted = feeds[k].muted;
            settled[k] = muted ? 1.0f : feeds[k].settledGain;
            meter[k] = muted ? 1.0f : feeds[k].meterGain;
        }
        const int stageDelay = d.stage.delaySamples();
        const bool settledUnity = d.settledGainDelay.processBlock(settled, count, stageDelay);
        const bool meterUnity = d.meterGainDelay.processBlock(meter, count, stageDelay);
        if (settledUnity && meterUnity)   // x * 1 * 1 is x
            for (int k = 0; k < count; ++k) outputs[k] = feeds[k].muted ? 0.0f : outputs[k];
        else
            for (int k = 0; k < count; ++k)
                outputs[k] = feeds[k].muted ? 0.0f : outputs[k] * settled[k] * meter[k];
    }

    std::array<float, 2> processOptoLightPair(const std::array<float, 2>& input,
                                              const std::array<float, 2>& sidechain,
                                              const MultiCompParameterState& p, bool external,
                                              const std::array<float, 2>& detector) noexcept
    {
        if (!optoCell.linked())
            return {{processOpto(input[0], 0, sidechain[0], p, external, detector[0], true),
                     processOpto(input[1], 1, sidechain[1], p, external, detector[1], true)}};
        const auto oldGain = optoCell.processLightPair(input, sidechain, external,
            detector, p.optoLimit.load(std::memory_order_relaxed),
            p.optoPeakReduction.load(std::memory_order_relaxed));
        return {{processOpto(input[0], 0, sidechain[0], p, external, detector[0], true, true, oldGain[0]),
                 processOpto(input[1], 1, sidechain[1], p, external, detector[1], true, true, oldGain[1])}};
    }

    void setOptoLinkWeight(float weight) noexcept
    {
        optoCell.setLinkWeight(optoLinkAtDrive ? weight : 0.0f);
    }
    // Lab-only switches; the plugin always runs the defaults.
    void setOptoLinkAtDrive(bool on) noexcept { optoLinkAtDrive = on; }
    void setOptoFloorLaw(const std::array<float, 6>& law) noexcept { optoCell.setFloorLaw(law); }
    void setOptoFluxLaw(const std::array<float, 6>& law) noexcept { optoCell.setFluxLaw(law); }
    void setOptoCellConstants(const std::array<float, kOptoCellDefaults.size()>& values) noexcept
    {
        optoCell.setParams(optoCellParamsFrom(values));
    }

    // Candidate C6's stereo link (see kFetDynamicsDefaults). When the loop
    // runs, a caller that steps process() sample by sample hands each channel
    // its partner's driven sample, already scaled by the link amount, before
    // the call; one per sample, consumed by processFET. MultiCompDSP passes
    // the partners to processFetC6Block instead.
    bool fetFeedbackActive(const MultiCompParameterState& p) const noexcept
    {
        return fetDynamics.fbOn > 0.5f && fetRatioIndex(p) != fetVoice->allButtons;
    }
    // The active voice's Ratio position: FET BLACK's control (4:1, 8:1, 12:1, 20:1, All) or FET BLUE's (FetVoice).
    int fetRatioIndex(const MultiCompParameterState& p) const noexcept
    {
        const int position = fetVoiceIndex == 1 ? p.fetBlueRatio.load(std::memory_order_relaxed)
                                                : p.fetRatio.load(std::memory_order_relaxed);
        return std::clamp(position, 0, fetVoice->ratioCount - 1);
    }
    // Enters a FET voice (0 FET BLACK, 1 FET BLUE): its constants, the coefficients derived from them and fresh
    // knob caches. MultiCompDSP runs the old mode to silence and enters the new one from rest, so the voices never
    // share a block; each keeps its own channel states (fet, fetBlue).
    void selectFetVoice(int voice) noexcept
    {
        voice = voice == 1 ? 1 : 0;
        if (voice == fetVoiceIndex) return;
        fetVoiceIndex = voice;
        fetVoice = voice == 1 ? &kFetBlueVoice : &kFetBlackVoice;
        fetDynamics = fetVoiceDynamics[static_cast<size_t>(voice)];
        fetFeedbackTerms.valid = false;
        fetStaticKnobK3Memo.valid = false;
        updateFetDynamicsCoefficients();
    }
    void setFetLinkPartner(int ch, float sample) noexcept
    {
        fetLinkPartner[static_cast<size_t>(std::clamp(ch, 0, 1))] = sample;
    }

    // processFET's C6 arm (fetFeedbackActive) for the active voice over a block of oversampled samples, for
    // MultiCompDSP's FET block path: `channelCount` channels (1 or 2), each one's audio, detector feed and linked
    // partner per oversampled sample k = i * factor + phase (partner null: zero, as unlinked), the Output knob in dB
    // per host sample, the results into `out`. A stereo pair runs in two-lane registers on arm64 (MultiCompLanes.hpp), channel by
    // channel elsewhere; either way each channel ends exactly as processFET called sample by sample leaves it.
    void processFetC6Block(int channelCount, const MultiCompParameterState& p, bool external, int samples,
                           int factor, const float* outputDb, const float* const* audio,
                           const float* const* sidechain, const float* const* partner, float* const* out) noexcept
    {
        // MC2_FET_SINGLE_LANE (verification builds only) steps a pair one channel at a time, so the two-lane
        // path can be byte-compared against it.
#if defined(__aarch64__) && !defined(MC2_FET_SINGLE_LANE)
        if (channelCount == 2)
        {
            const int channels[2] = {0, 1};
            fetC6Run<lanes::Lane2>(channels, p, external, samples, factor, outputDb, audio, sidechain, partner, out);
            return;
        }
#endif
        for (int ch = 0; ch < std::min(channelCount, 2); ++ch)
        {
            const int channels[1] = {ch};
            const float* const partnerOf[1] = {partner != nullptr ? partner[ch] : nullptr};
            fetC6Run<lanes::Lane1>(channels, p, external, samples, factor, outputDb, audio + ch, sidechain + ch,
                                   partner != nullptr ? partnerOf : nullptr, out + ch);
        }
    }

    // Lab-only hook (see kFetDynamicsDefaults). Not real-time safe against a
    // running block and never called by the plugin.
    void setFetDynamicsParams(const FetDynamicsParams& params, int voice = 0) noexcept
    {
        voice = voice == 1 ? 1 : 0;
        fetVoiceDynamics[static_cast<size_t>(voice)] = params;
        for (auto& d : voice == 1 ? fetBlue : fet) d.carry.valid = false;
        if (voice != fetVoiceIndex) return;
        fetDynamics = params;
        fetFeedbackTerms.valid = false;
        fetStaticKnobK3Memo.valid = false;
        updateFetDynamicsCoefficients();
    }

private:
    // Coefficients derived from fetDynamics at the mode rate. Double
    // precision: a 10 Hz corner at 96 kHz puts the C4 poles within 1e-3 of the
    // unit circle, and the lowest C5 all-pass pole sits at c = -0.9999.
    void updateFetDynamicsCoefficients() noexcept
    {
        // static: MSVC rejects a lambda reading a non-static floating-point
        // local constexpr it does not capture (C3493); clang and GCC accept it.
        static constexpr double pi = 3.14159265358979323846;
        const double rate = fs * osFactor;
        // RBJ high-pass, b0 b1 b2 a1 a2 normalised by a0.
        const auto highPass = [rate](float hz, float q) noexcept {
            const double w0 = 2.0 * pi * static_cast<double>(hz) / rate;
            const double alpha = std::sin(w0) / (2.0 * static_cast<double>(q));
            const double c = std::cos(w0), a0 = 1.0 + alpha;
            return std::array<double, 5>{{(1.0 + c) / (2.0 * a0), -(1.0 + c) / a0, (1.0 + c) / (2.0 * a0),
                                          -2.0 * c / a0, (1.0 - alpha) / a0}};
        };
        // C4, behind the gain element, and the section ahead of it.
        fetLfHighPass = highPass(fetDynamics.lfHpFrequency, fetDynamics.lfHpQ);
        fetLfAheadOn = fetDynamics.lfHpOn > 0.5f && fetDynamics.lfAheadHz > 0.0f
            && fetDynamics.lfAheadQ > 0.0f;
        fetLfAheadHighPass = fetLfAheadOn
            ? highPass(fetDynamics.lfAheadHz, fetDynamics.lfAheadQ)
            : std::array<double, 5>{{1.0, 0.0, 0.0, 0.0, 0.0}};
        // The feedback detector's own high-pass (fbTapHighPassHz; 0 = the C4
        // stage): first order, bilinear.
        fetTapHighPass = fetLfHighPass;
        if (fetDynamics.fbTapHighPassHz > 0.0f)
        {
            const double corner = std::min(
                static_cast<double>(fetDynamics.fbTapHighPassHz), 0.45 * rate);
            const double k = std::tan(pi * corner / rate);
            const double pole = (1.0 - k) / (1.0 + k);
            fetTapHighPass = {{0.5 * (1.0 + pole), -0.5 * (1.0 + pole), 0.0, -pole, 0.0}};
        }
        // The voice's side-chain filter: each shelf (s + wz) / (s + wp), its corners prewarped, bilinear; the
        // sections a voice leaves out pass the signal through.
        fetScFilter.fill({{1.0, 0.0, 0.0}});
        for (int i = 0; i < fetVoice->scFilterSections; ++i)
        {
            const auto prewarp = [rate](float hz) noexcept {
                return 2.0 * rate * std::tan(pi * std::min(static_cast<double>(hz), 0.45 * rate) / rate);
            };
            const double wz = prewarp(fetVoice->scFilterZerosHz[static_cast<size_t>(i)]);
            const double wp = prewarp(fetVoice->scFilterPolesHz[static_cast<size_t>(i)]);
            const double k = 2.0 * rate;
            fetScFilter[static_cast<size_t>(i)] = {{(k + wz) / (k + wp), (wz - k) / (k + wp), (wp - k) / (k + wp)}};
        }
        fetScFilterGain = std::pow(10.0, static_cast<double>(fetVoice->scFilterGainDb) / 20.0);
        fetAmplifierReference = decibelsToGain(fetVoice->amplifierReferenceDb);
        fetAmplifierInverse = 1.0f / fetAmplifierReference;
        // C5: A(z) = (c + z^-1) / (1 + c z^-1), c = (2 pi f - 2 rate) / (2 pi f + 2 rate).
        for (size_t i = 0; i < kFetAnalyticPolesA.size(); ++i)
        {
            const double pa = 2.0 * pi * kFetAnalyticPolesA[i], pb = 2.0 * pi * kFetAnalyticPolesB[i];
            fetAnalyticA[i] = (pa - 2.0 * rate) / (pa + 2.0 * rate);
            fetAnalyticB[i] = (pb - 2.0 * rate) / (pb + 2.0 * rate);
        }
    }

    // One sample through a first-order all-pass chain; returns the chain output.
    static double runAllPassChain(double x, const std::array<double, 7>& coeffs,
                                  std::array<double, 7>& x1, std::array<double, 7>& y1) noexcept
    {
        for (size_t i = 0; i < coeffs.size(); ++i)
        {
            const double y = coeffs[i] * (x - y1[i]) + x1[i];
            x1[i] = x;
            y1[i] = y;
            x = y;
        }
        return x;
    }

    void configureOptoStage() noexcept
    {
        const bool oversampled = (optoSettingFactor > 0 ? optoSettingFactor : osFactor) >= 2;
        for (auto& d : opto)
            d.stage.configure(osFactor, optoStageLatencyHost(fs), oversampled);
    }

    // The output stage's own state. The detector, estimator and cell state
    // live in OptoCell (MultiCompOptoCell.hpp).
    struct OptoState
    {
        float gainMean = 1.0f;
        // Two cascaded one-pole low-passes of the cell gain at half the tone
        // frequency (unity at reset, so a fresh instance has no ripple
        // transient); the gain minus this is the ripple the audio scales.
        double rippleGainLow1 = 1.0;
        double rippleGainLow2 = 1.0;
        // Running correlation of the scaled ripple with the audio, so the
        // ripple term adds harmonics without moving the settled level.
        float rippleFundamentalMean = 0.0f;
        // The output stage runs 2x oversampled; everything applied after it
        // is delayed by the same constant latency so alignment is unchanged.
        OptoStageOversampler stage;
        OptoStageDelay settledGainDelay, meterGainDelay;
        float audioPowerMean = 0.0f;
        float broadbandSlowPower = 0.0f;
        float broadbandPowerDeviation = 0.0f;
        float tonePredictionErrorMean = 0.0f;
        float harmonicPredictionErrorMean = 0.0f;
        double toneCurvatureNumeratorMean = 0.0;
        double toneCurvatureDenominatorMean = 0.0;
        float tonePrevious1 = 0.0f;
        float tonePrevious2 = 0.0f;
        int rippleSupportSamples = 0;
        int harmonicSilenceSamples = 0;
        int broadbandSupportSamples = 0;
        int sineSupportSamples = 0;
        float programmeCorrectionDb = 0.0f;
        float toneInputPeak = 0.0f;
        int toneInputPeakHold = 0;
        float toneInputPrevious = 0.0f;
        float toneMeasuredFrequencyHz = 1000.0f;
        int tonePeriodSamples = 0;
        int toneInputSign = 0;
        float cycleInputPeak = 0.0f;
        float previousCycleInputPeak = 0.0f;
        float eventDropCorrectionDb = 0.0f;
        float eventDropSlowCorrectionDb = 0.0f;
        float eventDropDepthScale = 0.0f;
        float shortEventPreviousInput = 0.0f;
        float shortEventPeak = 0.0f;
        float shortEventQuietPeak = 0.0f;
        float shortEventGrDb = 0.0f;
        float shortEventPreviousCorrectionDb = 0.0f;
        float meterCorrectionDb = 0.0f;
        int longEventReleaseAgeSamples = 0;
        bool longEventReleaseActive = false;
        int shortEventAgeSamples = 0;
        int shortEventLastLoudAgeSamples = 0;
        int shortEventQuietSamples = 0;
        int shortEventSilenceSamples = 0;
        float shortEventPowerMean = 0.0f;
        bool shortEventActive = false;
        bool shortEventEnabled = false;
        bool shortEventConsumed = false;
    };
    // One tolerance-refreshed expansion's anchor: the last argument the function was evaluated at exactly (NaN:
    // none yet), its value there, and what the series needs besides (an inverse or a sine).
    struct FetExpansion
    {
        float at = std::numeric_limits<float>::quiet_NaN(), value = 0.0f, aux = 0.0f;
    };
    struct FetExpansionD
    {
        double at = std::numeric_limits<double>::quiet_NaN(), value = 0.0;
    };
    // expandedDepthPow's anchor: the last exact evaluation point, its inverse (NaN: none yet), exponent and value.
    struct FetPowExpansion
    {
        double at = 0.0, inverse = std::numeric_limits<double>::quiet_NaN(), exponent = 0.0, value = 0.0;
    };
    // The knobs a carried C6 evaluation (FETState::FetCarry) was taken under.
    struct FetCarryKey
    {
        int ratioIndex = -1;
        float thresholdControlDb = 0.0f, attackPosition = 0.0f, releasePosition = 0.0f, sr = 0.0f;
    };
    struct FETState
    {
        // The C6 loop's last exact overshoot evaluation (advanceFetFeedback), carried to the next steps: its point
        // (u, its charge, the slow population and the tail reading), its value without the drive, and its slopes.
        struct FetCarry
        {
            bool valid = false, kneeApplied = false;
            float u = 0.0f, charge = 0.0f, slow = 0.0f, tail = 0.0f, quotient = 0.0f, levelSlope = 0.0f;
            float slowSlope = 0.0f, weight = 1.0f, weightSlope = 0.0f;
            FetCarryKey key;
        } carry;
        // Expansions of the C6 loop's slowly moving arguments (see expandedTanh and the others): the slow law's
        // tanh, the tip's cube root and cosine and the overshoot's exponential in the charge solve, the knee
        // release's exponential (double), the envelope's dB-to-gain and the colour stage's gain-to-dB.
        FetExpansion slowShapeExpansion, tipCbrtExpansion, tipCosExpansion, overshootExpExpansion;
        FetExpansion envelopeGainExpansion, colourGrDbExpansion;
        FetExpansionD kneeExpExpansion;
        FetPowExpansion depthPowExpansion;   // the 4:1 depth term's power (expandedDepthPow)
        float envelope = 1, previous = 0, dc = 0, prevSq = 0, peak = 0;
        float kneePeak = 0;
        float kneeBaseReductionDb = 0;
        float kneeCorrectionDb = 0, kneeGain = 1;
        // The C6 knee gain for the last knee correction it was evaluated at (bit for bit).
        std::uint32_t kneeGainKey = 0;
        bool kneeGainValid = false;
        float kneeGainValue = 1.0f;
        float recoveryPreviousInstantLevel = 0;
        float recoveryMaximumLevelDrop = 0;
        float recoveryMaximumGrDb = 0, recoveryElapsedSeconds = 0;
        float recoveryTerminalWeight = 0, recoveryReleasePosition = 0.5f;
        float recoveryAttackPosition = 0.5f;
        float recoveryGain = 1;
        int recoveryRatioIndex = 0;
        float kneePeakHoldSeconds = 0;
        float kneeBaseHoldSeconds = 0;
        float fastGrDb = 0, intermediateGrDb = 0;
        // In double: one step of its seconds-long pole is below the float
        // spacing of a state of a few dB at high processing rates, and a
        // float state then settles short of its target by a rate-dependent
        // amount (4.66 against 5.00 dB at 192 kHz with 4x oversampling).
        double slowGrDb = 0;
        // Candidate C6: the slow population that ignores the Release knob. In
        // double: one step of its seconds-long pole is below the float
        // spacing of a state of a few dB from 96 kHz up. It is the tail's own
        // reading; with fbTailKneeDb what the tail adds to the reduction is
        // fetTailAppliedNow(), not this.
        double slowTailGrDb = 0.0;
        float programmeExposureSeconds = 0, programmeMaximumGrDb = 0;
        float tailInputPeak = 0;   // fbTailInputLimit's peak follower on the detector
        float releaseMemory = 0, sag = 0, tilt = 0, subBass = 0;
        float colourPeak = 0, colourHighPassLow = 0, colourHighPassLower = 0;
        float colourLow = 0, colourLower = 0, colourLowPeak = 0;
        float shallowT3Lag = 0, shallowT3Lower = 0, shallowT3Lowest = 0;
        float shallowT5Lag = 0, shallowT5Lower = 0, shallowT5Lowest = 0;
        float colourLowDc = 0, colourLowPrevSq = 0;
        float colourLowHarmonicDc = 0;
        float previousLevel = 0.0f, voltageSag = 0.0f, subBassHpState = 0.0f;
        double lfHpZ1 = 0.0, lfHpZ2 = 0.0;
        double lfAheadZ1 = 0.0, lfAheadZ2 = 0.0;
        double feedbackHpZ1 = 0.0, feedbackHpZ2 = 0.0;
        double partnerHpZ1 = 0.0, partnerHpZ2 = 0.0;
        // The voice's side-chain filter (FetVoice::scFilterSections) on the detector and its linked partner.
        double scFilterZ1 = 0.0, scFilterZ2 = 0.0, scFilterZ3 = 0.0;
        double scFilterPartnerZ1 = 0.0, scFilterPartnerZ2 = 0.0, scFilterPartnerZ3 = 0.0;
        float feedbackLag = 0.0f;
        float riseOnlyPreviousMagnitude = 0.0f;   // fbChargeRiseOnly
        std::array<double, 7> analyticAx{}, analyticAy{}, analyticBx{}, analyticBy{};
        int transient = 0, sagCounter = 0, programmeSilentSamples = 0;
        bool recoveryWasSupported = false;
    };
    struct VCAState
    {
        float envelope = 1, rms = 0;
        // Native power history aligns the detector with the upsampling FIR.
        // 4x needs 13.25 native samples, including one interpolation tap.
        std::array<float, 64> rmsHistory{};
        unsigned rmsWrite = 0;
        int phase = 0;
    };
    struct BusState
    {
        // Signed internal control, shared by fixed and Auto release.
        // Keep dB state directly: gain/dB round trips stall near unity at
        // high sample rates. BusState{} clears it on prepare/reset; the
        // external detector shares the same control state.
        float fixedControlDb = 0;
        // Detector-side gain cell: the sidechain sees busDetectorExponent * control.
        float envelope = 1, compressed = 0, detectorEnvelope = 1;
        // Audio-only control after timing; shared across detector handoffs.
        // BusState{} clears it alongside the timing reservoirs on prepare/reset.
        // smoothControlDb2 is the second cascaded pole's state; BusState{}
        // clears it the same way.
        float smoothControlDb = 0;
        float smoothControlDb2 = 0;
        // Charged by sustained compression from either detector source;
        // BusState{} clears it on prepare/reset and fixed release clears it.
        float autoSlowDb = 0;
        // Independent sidechain through a virtual VCA; BusState{} resets it.
        float externalCompressed = 0;
        // Internal feedback is sampled at the host rate; the audio path stays
        // oversampled. BusState{} clears this clock on prepare/reset.
        int detectorPhase = 0;
    };
    // Derived host-rate transition coefficients; not signal history.
    std::array<double, 6> busAutoStep{};
    // bus-audible-20260911/HYPOTHESES-20260917.md: 164 native sine captures
    // showed H3/H5/H7 excess +1.9..2.6/+4.6..6.3/+8..10 dB. Trial B's
    // audio-only dB pole gives median excess -0.35/-0.18/+0.72 dB and passes
    // every BUS gate; trial A (smoothed feedback) fails attack by 2.36/2.52 dB.
    // Survey: pass-all 53 -> 110/252, balance median .298 -> .092 dB,
    // 400/50/10 ms maxima .424/.816/1.964 -> .315/.402/.524 dB.
    //
    // Two cascaded poles, not one (H-S2R, two-term-20260917/REPORT.md section 12).
    // The single 2600 Hz pole was tuned against the 2:1 steady-ripple harmonics;
    // the survey's largest remaining block was snare tonal balance at the fast
    // attacks, and 34 of its 35 winnable captures are 4:1 or 10:1. Cascading
    // 3500 Hz with 12000 Hz takes pass-all 107 -> 121/252 (22 captures won,
    // 8 lost), snare-balance failures 110 -> 90 captures and the balance maximum
    // 1.057 -> 0.732 dB. All eight native-reference gates pass, including
    // Completion's harmonic bounds (4:1 H3 +0.5084 / H5 +1.3420, 10:1 H3
    // +0.4641 / H5 +1.1818, against 1.0 / 1.5 dB). Seven losses are at
    // 10:1 / 0.1 ms / 0.1 s with shallow thresholds; the eighth is a marginal
    // 2:1 / 1 ms / 0.1 s session-balance failure. The fastest 10:1 corner
    // wants the old 2600 Hz while 2:1 wants about 1200 Hz, and a single gain
    // cell cannot be both. The unresolved 2:1 control-ripple excess behind that
    // split is recorded in section 14 of the report; it is NOT the node table
    // (a C2 spline through the same nodes leaves H5 at +1.27 dB) and not the
    // output ceiling (frame peaks sit 39..52 dB below it).
    static constexpr float busAudioControlSmoothingHz = 3500.0f;
    static constexpr float busAudioControlSmoothingHz2 = 12000.0f;
    float busAudioControlSmoothingStep = 0;
    float busAudioControlSmoothingStep2 = 0;
    // The gain envelopes are double for the reason the FET slow populations
    // are: at high processing rates one step of a long release is below the
    // float spacing near unity gain, and a float envelope settles short of its
    // target for good (Digital held 1.16 dB at 192 kHz with 4x oversampling
    // and a 5 s release).
    struct DigitalState
    {
        double envelope = 1;
        float peak = 0, rms = 0, crest = 1;
    };

    double fs = 48000.0;
    int osFactor = 1;
    int optoSettingFactor = 0;   // setOptoSettingFactor(); 0: not set
    bool prepared = false;
    std::array<OptoState, 2> opto{};
    OptoCell optoCell;
    // The FET voice processFET and the C6 arm run (selectFetVoice), and each voice's dynamics constants: fetDynamics
    // is the active voice's copy.
    std::array<FetDynamicsParams, 2> fetVoiceDynamics{{FetDynamicsParams::fromArray(kFetDynamicsDefaults),
                                                       FetDynamicsParams::fromArray(kFetBlueDynamicsDefaults)}};
    int fetVoiceIndex = 0;
    const FetVoice* fetVoice = &kFetBlackVoice;
    FetDynamicsParams fetDynamics = fetVoiceDynamics[0];
    std::array<float, 2> fetLinkPartner{};
    bool optoLinkAtDrive = kOptoLinkAtDriveDefault;
    std::array<double, 5> fetLfHighPass{};
    std::array<double, 5> fetTapHighPass{};
    // The active voice's side-chain filter at the mode rate: per section b0, b1, a1 (transposed direct form II), and
    // the prototype's gain (updateFetDynamicsCoefficients).
    std::array<std::array<double, 3>, 3> fetScFilter{};
    double fetScFilterGain = 1.0;
    // The active voice's amplifier reference level (FetVoice::amplifierReferenceDb) and its inverse.
    float fetAmplifierReference = 1.0f, fetAmplifierInverse = 1.0f;
    std::array<double, 5> fetLfAheadHighPass{{1.0, 0.0, 0.0, 0.0, 0.0}};
    bool fetLfAheadOn = false;
    std::array<double, 7> fetAnalyticA{}, fetAnalyticB{};
    std::array<OptoHfShelf, kChannels> optoShelf{};
    std::array<OptoSubsonicHighPass, kChannels> optoPreHighPass{}, optoPostHighPass{};
    std::array<FETState, 2> fet{};
    std::array<VCAState, 2> vca{};
    std::array<BusState, 2> bus{};
    std::array<FETState, 2> fetBlue{};
    std::array<studioVcaLaw::GainComputer, 2> studioVca{};
    // studioVcaGains()'s static law, coefficients and input gain, recomputed only when their inputs change.
    studioVcaLaw::StaticLaw studioVcaStaticLaw{};
    float studioVcaStaticLawRatio = -1.0f;
    studioVcaLaw::Coefficients studioVcaCoefficients{};
    float studioVcaCoefficientAttack = -1.0f, studioVcaCoefficientRelease = -1.0f;
    double studioVcaCoefficientRate = -1.0;
    bool studioVcaCoefficientAuto = false;
    float studioVcaInputDb = 0.0f, studioVcaInputGainValue = 1.0f;   // decibelsToGain(0) is exactly 1
    std::array<DigitalState, 2> digital{};
    // busLaw::headroomDrive() is a std::pow evaluated two to three times per
    // channel per oversampled sample for a value that only changes when the
    // HEADROOM switch moves. Memoise it on the switch position; the cached
    // result is the same float the call would have returned. The output
    // ceiling's headroom compensation is memoised with it, so the wet path
    // multiplies instead of dividing by the drive every sample.
    int busDrivePosition = -1;
    float busDriveValue = 1.0f;
    float busCeilingScale = busCeilingReference;   // busCeilingReference / busDriveValue
    // The same for the makeup gain (renderBusOutput) and the control's charge
    // and discharge coefficients (advanceBusEnvelope): each is recomputed only
    // when its inputs change, by the same expression, so it is the same float.
    float busMakeupMemoDb = 0.0f, busMakeupMemoGain = 1.0f;   // decibelsToGain(0) is exactly 1
    float busChargeMemoAttack = -1.0f, busChargeMemoRate = -1.0f, busChargeMemo = 0.0f;
    float busDischargeMemoRelease = -1.0f, busDischargeMemoRate = -1.0f, busDischargeMemo = 0.0f;
    // The compressed feedback signal has its own low-frequency coupling.
    // Reuse the shared filter; prepare/reset clear each channel's history,
    // and runtime oversampling changes refresh its pole without discarding it.
    std::array<DCBlocker, 2> busFeedbackCoupling;
    std::array<GuardedBiquad, 2> busSidechainHighPass;
    std::array<Biquad, 2> busSidechainLowShelf, busSidechainHighShelf;
    float busSidechainRate = 0.0f;
    float busSidechainHighPassFrequency = -1.0f;
    float busSidechainLowFrequency = -1.0f, busSidechainLowGain = 0.0f;
    float busSidechainHighFrequency = -1.0f, busSidechainHighGain = 0.0f;
    std::array<std::vector<float>, kChannels> digitalDelay;
    std::array<int, 2> digitalWrite{{0, 0}};

    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerFet, outputTransformerFet;
    // The vintage input transformer feeds only the arms off the C6 loop (All-buttons; the loop switched off), so
    // the loop leaves it unstepped (owner, 2026-10-20) and marks it resting: the next sample off the loop resets
    // it first, so it starts from rest rather than from a stale history.
    std::array<bool, 2> fetInputTransformerResting{};
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerBus, outputTransformerBus;
    HardwareEmulation::StereoConvolution fetConvolution, busConvolution;
    MultiCompTransientShaper transientShaper;
    MultiCompLookupTables lookupTables;

    float optoInvSampleRate = 1.0f / 48000.0f;
    float optoMakeupKnob = std::numeric_limits<float>::quiet_NaN(), optoMakeup = 1.0f;   // processOptoPreStage()
    std::vector<float> optoStageScratch;   // optoStageBlock()
    // VCA detector onset limit (MultiCompVcaLaw.hpp), refreshed with the rate.
    float vcaDetectorFloorPower = 0.0f, vcaDetectorRise = 1.0f;
    // 35 ms: the single-constant fit to the reference step family gave
    // 31 ms (attack) / 35 ms (release); at 33 ms our attack and release
    // both read 1-7 ms fast against the reference, so the constant sits
    // at the release fit.
    static constexpr float kVcaRmsSeconds = 0.035f;
    float vcaRmsCoeff = 0.0f;   // host-rate pole of the RMS integrator, refreshed with the rate
    // The COMPRESSION law and the output gain only move with their knobs; the
    // per-sample path reads them through these one-entry caches.
    float vcaSlopePosition = std::numeric_limits<float>::quiet_NaN(), vcaSlope = 0.0f;
    float vcaOutputDb = std::numeric_limits<float>::quiet_NaN(), vcaOutputGain = 1.0f;
    float optoColourPeakRelease = 0;
    float optoShortEventRelease = 0;
    float optoEventDropSlowRelease = 0;
    float optoGainRippleMeanStep = 0;
    double optoGainRippleMeanStepDouble = 0;
    float optoBroadbandSlowStep = 0;
    float optoProgrammeStep = 0;
    int optoGainRippleWarmupSamples = 1;
    int optoBroadbandWarmupSamples = 1;
    int optoTonePeakHoldSamples = 1;
    int optoShortEventQuietSamples = 1;
    int optoShortEventArmSamples = 1;
    int optoShortEventRecurringMaxSamples = 1;
    int optoTonePeriodMaxSamples = 1, optoToneMinimumPeriod = 2, optoToneMaximumPeriod = 2;
    int optoLongEventAgeMaxSamples = 1, optoShortEventGapSamples = 1, optoShortEventAgeMaxSamples = 1;
    float fetTilt = 0, fetHardwareGain = 1.0f;
    float fetColourCorner250 = 0.0f, fetColourPeakRelease = 0.0f, fetLowT3Corner300 = 0.0f;   // updateRateCoefficients
    float fetColourDcAlpha = 0.0f;                                                            // updateRateCoefficients
    bool fetConvolutionUnity = false;   // the FET convolution's IR is the unit impulse (prepare)
    float busHardwareGain = 1.0f;
    std::array<float, 3> fetHardwareGains{{1.0f, 1.0f, 1.0f}};
    std::array<float, 3> busHardwareGains{{1.0f, 1.0f, 1.0f}};

    void prepareHardware(double rate)
    {
        auto preparePair = [rate](auto& in, auto& out, const auto& profile, bool enable) {
            in.prepare(rate, 2); in.setProfile(profile.inputTransformer); in.setEnabled(enable);
            out.prepare(rate, 2); out.setProfile(profile.outputTransformer); out.setEnabled(enable);
        };
        const auto& fp = HardwareEmulation::HardwareProfiles::getFETCompressor();
        // The reference neutral response has two 1.67 Hz coupling poles.
        // Override only this instance; shared profiles and sibling modes retain
        // their existing audio paths. See the BUS dynamics measurement report.
        auto bp = HardwareEmulation::HardwareProfiles::getConsoleBus();
        bp.inputTransformer.dcBlockingFreq = 1.6666667f;
        bp.outputTransformer.dcBlockingFreq = 1.6666667f;
        // The native console bus compressor is linear below its output ceiling; retain coupling
        // and frequency response, but do not add generic transformer colour.
        bp.inputTransformer.saturationAmount = bp.outputTransformer.saturationAmount = 0;
        bp.inputTransformer.hysteresisAmount = bp.outputTransformer.hysteresisAmount = 0;
        bp.inputTransformer.harmonics = {};
        bp.outputTransformer.harmonics = {};
        for (int ch = 0; ch < 2; ++ch)
        {
            preparePair(inputTransformerFet[ch], outputTransformerFet[ch], fp, true);
            preparePair(inputTransformerBus[ch], outputTransformerBus[ch], bp, true);
        }
        fetConvolution.prepare(rate); fetConvolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET);
        fetConvolutionUnity = fetConvolution.isUnity();
        busConvolution.prepare(rate); busConvolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::Console_Bus);
        cacheHardwareGains();
        updateHardwareRate(rate);
        selectHardwareGains();
        resetHardware();
        const float sr = static_cast<float>(rate);
        updateRateCoefficients(sr);
    }

    void updateRateCoefficients(float sr) noexcept
    {
        // Match trial B's float arithmetic: processing rate divided by OS,
        // then a host-rate pole. Cache only the coefficient, never its history.
        const float busControlRate = sr / static_cast<float>(osFactor);
        busAudioControlSmoothingStep = 1.0f - std::exp(
            -6.2831853f * busAudioControlSmoothingHz / busControlRate);
        busAudioControlSmoothingStep2 = 1.0f - std::exp(
            -6.2831853f * busAudioControlSmoothingHz2 / busControlRate);
        // Internal BUS control runs once per host sample, at every audio OS setting.
        // Exact coupled-state transition with measured 41.659 ms / 4.159 s poles
        // and 86.82% sustained slow recovery. The -0.33 dB/s control bias also
        // predicts the independent quiet drift. See the BUS storage report.
        {
            const double fastRate = 1.0 / 0.04165904;
            const double slowRate = 1.0 / 4.1588435;
            const double slowWeight = 0.8682;
            const double k = slowRate / (1.0 - slowWeight);
            const double a = fastRate + slowRate - k;
            const double gamma = 1.0 - fastRate * slowRate / (a * k);
            const double ef = std::exp(-fastRate / static_cast<float>(fs)), es = std::exp(-slowRate / static_cast<float>(fs));
            const double f = (es - ef) / (fastRate - slowRate);
            const double b = (k - slowRate) / (fastRate - slowRate);
            const double dc = -0.33 * ((1-b)*(1-ef)/fastRate + b*(1-es)/slowRate);
            const double ds = -0.33 * k*gamma/(fastRate-slowRate) * ((1-es)/slowRate - (1-ef)/fastRate);
            busAutoStep = {{es + (slowRate-a)*f, a*f, k*gamma*f, es + (slowRate-k)*f, dc, ds}};
        }
        for (auto& filter : busFeedbackCoupling) filter.setSampleRate(sr, 1.6666667f);
        optoInvSampleRate = 1.0f / sr;
        for (auto& shelf : optoShelf) shelf.prepare(sr);
        for (auto& filter : optoPreHighPass)
            filter.prepare(sr, kOptoPreHighPassHz, kOptoPreHighPassQ);
        for (auto& filter : optoPostHighPass)
            filter.prepare(sr, kOptoPostHighPassHz, kOptoPostHighPassQ);
        // The VCA detector runs once per host sample, so its limit uses fs.
        vcaDetectorFloorPower = std::pow(10.0f, vcaLaw::kDetectorFloorDb * 0.1f);
        vcaDetectorRise = std::pow(10.0f, vcaLaw::kDetectorRiseDbPerMs * 100.0f / static_cast<float>(fs));
        vcaRmsCoeff = std::exp(-1.0f / (kVcaRmsSeconds * static_cast<float>(fs)));
        optoCell.setRate(sr);
        optoColourPeakRelease = std::exp(-optoInvSampleRate / 0.040f);
        optoShortEventRelease = std::exp(-optoInvSampleRate / 0.030f);
        optoEventDropSlowRelease = std::exp(-optoInvSampleRate / 0.120f);
        optoGainRippleMeanStep = 1.0f - std::exp(-optoInvSampleRate / 0.020f);
        optoGainRippleMeanStepDouble = 1.0 - std::exp(
            -static_cast<double>(optoInvSampleRate) / 0.020);
        optoBroadbandSlowStep = 1.0f - std::exp(-optoInvSampleRate / 1.0f);
        optoProgrammeStep = 1.0f - std::exp(-optoInvSampleRate / 0.100f);
        optoGainRippleWarmupSamples = std::max(1, static_cast<int>(
            std::lround(0.100f * sr)));
        optoBroadbandWarmupSamples = std::max(1, static_cast<int>(
            std::lround(0.500f * sr)));
        optoTonePeakHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.025f * sr)));
        // A charge fixture switches directly from its burst to a low-level
        // probe.  Detect that probe before it can materially enter the 1 ms
        // analysis window.  Requiring several consecutive small, non-zero
        // samples distinguishes it from the exact-zero gap in the crest gate.
        optoShortEventQuietSamples = std::max(1, static_cast<int>(
            std::lround(0.00012f * sr)));
        optoShortEventArmSamples = std::max(1, static_cast<int>(
            std::lround(0.075f * sr)));
        optoShortEventRecurringMaxSamples = std::max(1, static_cast<int>(
            std::lround(0.600f * sr)));
        // Sample counts processOpto compares against on every sample, in the
        // form it always computed them.
        optoTonePeriodMaxSamples = std::max(1, static_cast<int>(std::lround(0.050f / optoInvSampleRate)));
        optoToneMinimumPeriod = std::max(2, static_cast<int>(
            std::lround(1.0f / (20000.0f * optoInvSampleRate))));
        optoToneMaximumPeriod = std::max(optoToneMinimumPeriod, static_cast<int>(
            std::lround(1.0f / (20.0f * optoInvSampleRate))));
        optoLongEventAgeMaxSamples = std::max(1, static_cast<int>(std::lround(4.0f / optoInvSampleRate)));
        optoShortEventGapSamples = std::max(1, static_cast<int>(std::lround(0.002f / optoInvSampleRate)));
        optoShortEventAgeMaxSamples = std::max(1, static_cast<int>(std::lround(0.100f / optoInvSampleRate)));
        fetTilt = 1.0f - std::exp(-2.0f * kDuskPi * 800.0f / sr);
        // processFET's colour-stage one-poles, in the form it computed them on every sample.
        fetColourCorner250 = 1.0f - std::exp(-2.0f * kDuskPi * 250.0f / sr);
        fetColourPeakRelease = std::exp(-1.0f / (0.1f * sr));
        fetLowT3Corner300 = 1.0f - std::exp(-2.0f * kDuskPi * 300.0f / sr);
        fetColourDcAlpha = 1.0f / (1.0f + kDuskTwoPi * 10.0f / sr);
    }

    // A knob law's value for the last knob value it was evaluated at (bit for bit): the laws below are pure
    // functions of one knob, so the memo returns exactly what evaluating them again would.
    struct FetKnobMemo
    {
        std::uint32_t key = 0;
        bool valid = false;
        float value = 0.0f;
    };
    // processFET's knob laws (memoized): Input and Output gain (vintage, Studio), threshold gain, Attack and
    // Release positions.
    std::array<FetKnobMemo, 2> fetInputGainMemo{}, fetOutputGainMemo{};
    FetKnobMemo fetThresholdGainMemo, fetAttackPositionMemo, fetReleasePositionMemo;
    FetKnobMemo fetStaticKnobK3Memo;   // fbStaticK3KnobShare's Release law (cleared with the dynamics)
    FetKnobMemo fetHeadroomGainMemo;   // FET BLUE's Headroom drive
    FetKnobMemo fetRatioGainMemo;      // FET BLUE's linear gain per Ratio position
    float fetReleasePositionOf(const MultiCompParameterState& p) noexcept
    {
        return memoized(fetReleasePositionMemo, p.fetRelease.load(std::memory_order_relaxed),
                        [](float v) noexcept { return fetReleasePosition(v); });
    }
    template <class Fn>
    static float memoized(FetKnobMemo& memo, float x, Fn&& law) noexcept
    {
        std::uint32_t bits;
        std::memcpy(&bits, &x, sizeof bits);
        if (memo.valid && memo.key == bits) return memo.value;
        return memoRefresh(memo, bits, x, law);
    }
    template <class Fn>
    MC2_NOINLINE static float memoRefresh(FetKnobMemo& memo, std::uint32_t bits, float x, Fn& law) noexcept
    {
        memo.key = bits;
        memo.value = law(x);
        memo.valid = true;
        return memo.value;
    }

    static int hardwareGainIndex(int factor) noexcept
    {
        return factor == 4 ? 2 : (factor == 2 ? 1 : 0);
    }

    void updateHardwareRate(double rate) noexcept
    {
        auto updatePairs = [rate](auto& input, auto& output) {
            for (int ch = 0; ch < 2; ++ch)
            {
                input[ch].setSampleRate(rate);
                output[ch].setSampleRate(rate);
            }
        };
        updatePairs(inputTransformerFet, outputTransformerFet);
        updatePairs(inputTransformerBus, outputTransformerBus);
    }

    void cacheHardwareGains()
    {
        constexpr int factors[] = {1, 2, 4};
        for (int i = 0; i < 3; ++i)
        {
            const double rate = fs * factors[i];
            updateHardwareRate(rate);
            calibrateFetHardwareGain(rate);
            calibrateBusHardwareGain(rate);
            fetHardwareGains[static_cast<size_t>(i)] = fetHardwareGain;
            busHardwareGains[static_cast<size_t>(i)] = busHardwareGain;
        }
    }

    void selectHardwareGains() noexcept
    {
        const size_t index = static_cast<size_t>(hardwareGainIndex(osFactor));
        fetHardwareGain = fetHardwareGains[index];
        busHardwareGain = busHardwareGains[index];
    }

    template <typename ResetFn, typename SampleFn>
    static float calibrateChain(double rate, ResetFn&& resetFn, SampleFn&& sampleFn)
    {
        constexpr int calibrationSamples = 4800;
        constexpr float refAmplitude = 0.126f;
        constexpr float refFreq = 1000.0f;
        const float sr = static_cast<float>(rate);
        const float angularStep = 2.0f * kDuskPi * refFreq / sr;
        resetFn();
        const int warmup = static_cast<int>(sr * 0.05f);
        for (int i = 0; i < warmup; ++i)
            (void)sampleFn(refAmplitude * std::sin(angularStep * static_cast<float>(i)));
        double inputRmsSquared = 0.0, outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            const float input = refAmplitude * std::sin(angularStep * static_cast<float>(warmup + i));
            const float output = sampleFn(input);
            inputRmsSquared += static_cast<double>(input * input);
            outputRmsSquared += static_cast<double>(output * output);
        }
        resetFn();
        return outputRmsSquared > 1.0e-12 && inputRmsSquared > 1.0e-12
            ? 1.0f / static_cast<float>(std::sqrt(outputRmsSquared / inputRmsSquared)) : 1.0f;
    }

    void calibrateFetHardwareGain(double rate)
    {
        fetHardwareGain = calibrateChain(rate,
            [this] { inputTransformerFet[0].reset(); outputTransformerFet[0].reset(); fetConvolution.reset(); },
            [this](float input) { float x = inputTransformerFet[0].processSample(input, 0); x = outputTransformerFet[0].processSample(x, 0); return fetConvolution.processSample(x, 0); });
    }

    void calibrateBusHardwareGain(double rate)
    {
        busHardwareGain = calibrateChain(rate,
            [this] { inputTransformerBus[0].reset(); outputTransformerBus[0].reset(); busConvolution.reset(); },
            [this](float input) { float x = inputTransformerBus[0].processSample(input, 0); x = outputTransformerBus[0].processSample(x, 0); return busConvolution.processSample(x, 0); });
    }

    void resetHardware() noexcept
    {
        fetConvolution.reset(); busConvolution.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inputTransformerFet[ch].reset(); outputTransformerFet[ch].reset();
            inputTransformerBus[ch].reset(); outputTransformerBus[ch].reset();
        }
    }

    // The nodes of the voices' Input and Output pot laws (FetVoice), at k/16 of the legacy host ranges, which keep
    // their knob positions: a position is (dB + 20) / 60 on Input and (dB + 20) / 40 on Output.
    inline static constexpr std::array<float, 17> kFetControlPositions{{
        0.000000f, 0.062500f, 0.125000f, 0.187500f,
        0.250000f, 0.312500f, 0.375000f, 0.437500f,
        0.500000f, 0.562500f, 0.625000f, 0.687500f,
        0.750000f, 0.812500f, 0.875000f, 0.937500f,
        1.000000f}};

    // Vintage-FET attack drive law. Six measured points, no extrapolation:
    // see the note at the use site in processFET. The abscissa is the SETTLED
    // reduction in dB, which is what `positiveReduction` carries; the ordinate
    // multiplies the base attack time. Both ends are held, so the domain is
    // exactly the span the reference was rendered at.
    inline static constexpr std::array<float, 6> kFetDriveReductionDb{{
        4.2865f, 6.6572f, 9.1406f, 14.1087f, 24.0669f, 34.0261f}};
    inline static constexpr std::array<float, 6> kFetDriveAttackScale{{
        14.8651f, 10.6649f, 7.8264f,
        5.0588f, 2.9199f, 2.3322f}};

    static float fetDriveAttackScale(float positiveReductionDb) noexcept
    {
        // Geometric interpolation: a multiplier on a time constant is a ratio,
        // so the natural interpolant between two anchors is the constant-ratio
        // one, and the required curve is strongly convex across a factor of six
        // -- linear interpolation of the same anchors reads 3.8 % high at the
        // middle of the widest gap. The independent check on the interpolant is
        // the held-out -24 dBFS row (19.0 dB settled, in that widest gap, never
        // an anchor): curve RMS 0.0291, between its neighbours' 0.0309 and
        // 0.0262.
        //
        // A NaN argument falls through every comparison to the last entry,
        // which is finite and positive. The clamp it replaced passed NaN
        // straight through to the attack coefficient.
        if (positiveReductionDb <= kFetDriveReductionDb.front())
            return kFetDriveAttackScale.front();
        for (size_t i = 1; i < kFetDriveReductionDb.size(); ++i)
            if (positiveReductionDb <= kFetDriveReductionDb[i])
            {
                const float fraction
                    = (positiveReductionDb - kFetDriveReductionDb[i - 1])
                    / (kFetDriveReductionDb[i] - kFetDriveReductionDb[i - 1]);
                return kFetDriveAttackScale[i - 1] * std::pow(
                    kFetDriveAttackScale[i] / kFetDriveAttackScale[i - 1],
                    fraction);
            }
        return kFetDriveAttackScale.back();
    }

    static float fetControlLaw(float position,
                               const std::array<float, 17>& values) noexcept
    {
        position = std::clamp(position, 0.0f, 1.0f);
        for (size_t i = 1; i < kFetControlPositions.size(); ++i)
            if (position <= kFetControlPositions[i])
            {
                const float low = kFetControlPositions[i - 1];
                const float high = kFetControlPositions[i];
                const float fraction = (position - low) / (high - low);
                return values[i - 1] + fraction * (values[i] - values[i - 1]);
            }
        return values.back();
    }

    static float fetInputGainDb(const FetVoice& voice, float legacyValueDb) noexcept
    {
        const float position = (legacyValueDb + 20.0f) / 60.0f;
        // FET BLACK's trim, 39.850671 = 40.0 - 0.149329. The vintage chain carried a
        // frequency-flat +0.149329 dB (x1.017341) gain excess against the
        // reference: ten zero-gain-reduction operating points spanning 89 dB of
        // output level reproduce it to 2.6e-05 dB, and the zero-GR frequency
        // sweep reproduces it to 0.008 dB from 100 Hz to 16 kHz.
        //
        // It is removed here, ahead of the detector, rather than from the
        // output trim, because that is where it was measured to live. Removing
        // it post-compressor translates all 80 static cells by exactly
        // -0.149334 dB (measured spread 1.7e-07 dB) and leaves a residual
        // proportional to each cell's reduction slope: regressing that residual
        // on the measured per-cell slope gives -0.146893 dB per unit slope
        // against the -0.149334 a pure pre-detector error predicts, and the
        // correlation between residual and slope falls from -0.76 to +0.02 once
        // the excess is taken out here instead. The solved split is therefore
        // +0.1469 dB pre / +0.0025 dB post, i.e. all of it pre within the
        // measurement -- not the +0.093/+0.056 that four static rows suggested.
        // The 64 non-All-buttons static cells land within 0.082 dB this way
        // against 0.195 dB the other way.
        //
        // `inputGain` scales the audio and the detector together, so this also
        // lowers every settled reduction by slope x 0.149329 dB (0.124 dB at
        // 4:1). The attack drive table and the `fetBroadbandK2` lookup are both
        // reduction-indexed and therefore both moved with it. The attack drive
        // table was re-measured against that shift when it was written;
        // `fetBroadbandK2` was NOT, and this sentence used to claim both were.
        // It has since been re-fitted in its own right against a dense measured
        // surface -- see the note there.
        return voice.inputTrimDb + fetControlLaw(position, voice.inputRelativeDb);
    }

    static float fetOutputGainDb(const FetVoice& voice, float legacyValueDb) noexcept
    {
        const float position = (legacyValueDb + 20.0f) / 40.0f;
        // FET BLACK's absolute trim includes the measured linear convolution-chain loss;
        // the relative pot law remains the independently fitted result.
        return voice.outputTrimDb + fetControlLaw(position, voice.outputRelativeDb);
    }

    static float fetBroadbandK2(float gainReductionDb) noexcept
    {
        // The black-face unit's even-order path is programme dependent rather
        // than a monotonic polynomial drive: its equivalent coefficient falls
        // from 0.0042 at the compression onset to a minimum of 0.0020 near 7 dB
        // of reduction, then rises by a factor of nine to 0.0163 by 28 dB.
        //
        // COORDINATE. The argument is what the call site passes:
        // `-gainToDecibels(envelope + 0.001f)`, per sample. That is NOT the
        // settled output-referred reduction the render probes report -- the
        // +0.001 offset alone costs 0.087 dB at 20 dB -- and every anchor below
        // is stated in the call site's coordinate, measured there by
        // `MultiCompCoreTest --fet-h2-surface`. Do not re-fit these against a
        // reduction read anywhere else without moving the abscissa first.
        //
        // MEASUREMENT. 39 operating points at 1 kHz spanning Input 0.2-1.0 and
        // source -48 to -6 dBFS (`probe_h2_surface.py`, the FET
        // reference-comparison harness), each giving the coefficient that lands
        // the reference unit's own H2: rows reaching the same reduction from
        // different knob/level combinations agree to about 0.1 dB, which is
        // what makes a reduction-indexed law the right shape here. Every
        // segment below contains at least two of those rows. The sixteen-row
        // campaign harmonic grid, which is all the previous anchor set ever had,
        // reaches eight distinct depths -- four of them zero -- so it left two of
        // that table's seven segments with no point at all and gave four more
        // exactly one, never in the interior. That is how a non-monotonic wiggle
        // (0.0027 at 2.8 dB, 0.0020 at 7.2, then 3x to 0.0059 at 13.0) survived:
        // its widest segment was sampled once, near the bottom edge, and read
        // 5.14 dB high in the middle. Worst residual of this fit over the 39
        // rows: 0.191 dB.
        //
        // Above 33.5 dB nothing is measured: the reference's own reduction law
        // collapses past roughly +37 dB internal (a separate, unfixed defect),
        // so the last segment is a straight-line extension of the fitted
        // 28-33.5 dB trend, not data.
        //
        // 100 Hz is deliberately not in the fit. There `lowFrequencyK2 *
        // colourLowH2` supplies most of the even order, so a 100 Hz row's
        // implied coefficient belongs to that mechanism, not to this one.
        constexpr std::array<float, 14> reductionDb{{
            0.0f, 0.6f, 1.5f, 3.2f, 5.0f, 7.0f, 9.0f,
            13.0f, 17.5f, 20.0f, 23.0f, 25.5f, 28.0f, 40.0f}};
        constexpr std::array<float, 14> coefficients{{
            0.003912f, 0.004212f, 0.003316f, 0.002510f, 0.002094f, 0.001966f,
            0.002194f, 0.003210f, 0.005215f, 0.006591f, 0.009206f, 0.012971f,
            0.016280f, 0.018549f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    // fetFeedbackK3 / K2 / LowFrequencyK2: one reduction grid (dB, the call site's
    // -gainToDecibels(envelope + 0.001f) coordinate) and the three coefficient tables on it.
    static constexpr std::array<float, 18> kFetFeedbackColourGridDb{{
        0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 4.5f, 6.0f, 8.0f, 10.0f, 12.5f, 15.0f, 18.0f, 21.0f, 24.0f, 27.0f, 30.0f, 34.0f, 40.0f}};
    static constexpr std::array<float, 18> kFetFeedbackK3Coefficients{{
        -0.0037833666f, -0.0042293137f, -0.0047683814f, -0.0045133834f, -0.0042467434f, -0.0041645253f, -0.004137338f, -0.00410022f, -0.0040682144f, -0.0035717501f, -0.0029148753f, -0.0017307282f, -0.00040785203f, 1.9968795e-05f, -0.00047855233f, -0.0002712117f, 0.00094335334f, 0.0021579184f}};
    static constexpr std::array<float, 18> kFetFeedbackK2Coefficients{{
        -0.0037055267f, -0.003938718f, -0.0040902914f, -0.0029706408f, -0.0024666698f, -0.0020234112f, -0.0013229456f, -0.00048062025f, 0.00044876084f, 0.0016075863f, 0.0029288072f, 0.0044594146f, 0.006332474f, 0.009714308f, 0.014136585f, 0.016536158f, 0.01659756f, 0.016658964f}};
    static constexpr std::array<float, 18> kFetFeedbackLowFrequencyK2Coefficients{{
        -0.0005223149f, -0.0004777339f, -0.00043315292f, -0.00045875664f, -0.000630866f, -0.0010258019f, 9.901148e-05f, 0.0014718693f, 0.0021234842f, 0.0023899924f, 0.0021970607f, 0.0020121632f, 0.0020844045f, 0.0020459061f, 0.002141804f, 0.0021757928f, 0.0021054293f, 0.0020350656f}};
    // The grid row a reduction falls in (clamped to 0..40 dB) and its fraction of the interval below it; row 18
    // past the last point (NaN lands there). One search serves all three tables.
    struct FetColourRow
    {
        size_t row;
        float fraction;
    };
    // The row of a reduction already clamped to 0..40 dB (not NaN), from its half-dB cell ceil(2 g): every grid
    // point is a multiple of 0.5 dB, so a reduction in ((j - 1) / 2, j / 2] has the row of j / 2, the first grid
    // point at or above it -- the row a search from the bottom finds.
    static size_t fetColourRowIndex(float clampedReductionDb) noexcept
    {
        static constexpr std::array<std::uint8_t, 81> rows = [] {
            std::array<std::uint8_t, 81> table{};
            for (size_t j = 0; j < table.size(); ++j)
            {
                size_t i = 1;
                while (i + 1 < kFetFeedbackColourGridDb.size()
                       && kFetFeedbackColourGridDb[i] < 0.5f * static_cast<float>(j))
                    ++i;
                table[j] = static_cast<std::uint8_t>(i);
            }
            return table;
        }();
        return rows[static_cast<size_t>(std::ceil(2.0f * clampedReductionDb))];
    }
    static FetColourRow fetFeedbackColourRow(float gainReductionDb) noexcept
    {
        const auto& grid = kFetFeedbackColourGridDb;
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        if (!(gainReductionDb == gainReductionDb)) return {grid.size(), 0.0f};   // NaN: past the last row
        const size_t i = fetColourRowIndex(gainReductionDb);
        return {i, (gainReductionDb - grid[i - 1]) / (grid[i] - grid[i - 1])};
    }
    // The three tables' rows packed for lanes (fetFeedbackColourT): the row's lower grid point and its interval,
    // and per table the value there and its rise to the next, the differences fetFeedbackColourRow / At take.
    // Row 0 holds the last values with a zero rise, for NaN. One array, as the lanes read it four values at a time:
    // low, span, k2, k2Rise | k3, k3Rise, lowK2, lowK2Rise.
    struct FetColourPacked
    {
        float v[8];
    };
    static const FetColourPacked& fetColourPackedRow(size_t row) noexcept
    {
        static constexpr std::array<FetColourPacked, 18> packed = [] {
            const auto& g = kFetFeedbackColourGridDb;
            const auto& a = kFetFeedbackK2Coefficients;
            const auto& b = kFetFeedbackK3Coefficients;
            const auto& c = kFetFeedbackLowFrequencyK2Coefficients;
            std::array<FetColourPacked, 18> rows{};
            rows[0] = {{0.0f, 1.0f, a.back(), 0.0f, b.back(), 0.0f, c.back(), 0.0f}};
            for (size_t i = 1; i < rows.size(); ++i)
                rows[i] = {{g[i - 1], g[i] - g[i - 1], a[i - 1], a[i] - a[i - 1], b[i - 1], b[i] - b[i - 1],
                            c[i - 1], c[i] - c[i - 1]}};
            return rows;
        }();
        return packed[row];
    }
    static float fetFeedbackColourAt(const std::array<float, 18>& coefficients, FetColourRow at) noexcept
    {
        return at.row < coefficients.size()
            ? coefficients[at.row - 1] + at.fraction * (coefficients[at.row] - coefficients[at.row - 1])
            : coefficients.back();
    }

    static float fetFeedbackK3(float gainReductionDb) noexcept
    {
        // C6 arm only (2026-10-16). The 1176's harmonics are a static part, a
        // function of the reduction alone, plus the side chain's ripple, which
        // the C6 loop produces itself (proportional to the release rate). This
        // is the static part: per capture point of fresh 1176 grids (1 kHz and
        // 100 Hz, Inputs .2 / .4 / .8, six Release positions; no test row) the
        // coefficient that closes the 1176's Release-independent part against
        // the cell's own, in the call site's -gainToDecibels(envelope + 0.001f)
        // coordinate; the three Inputs agree on one law. The older tables beside
        // it were fitted to ripple-inclusive surfaces of the earlier smooth cell
        // and stay with the arms that still run it (All-buttons, C6 off).
        // Cubic (third harmonic), sign change near 23 dB GR.
        return fetFeedbackColourAt(kFetFeedbackK3Coefficients, fetFeedbackColourRow(gainReductionDb));
    }

    static float fetFeedbackK2(float gainReductionDb) noexcept
    {
        // C6 arm only: the even static part, as fetFeedbackK3 (negative below
        // ~9 dB GR: the 1176's sub-knee second harmonic sits at 180 degrees).
        return fetFeedbackColourAt(kFetFeedbackK2Coefficients, fetFeedbackColourRow(gainReductionDb));
    }

    static float fetFeedbackLowFrequencyK2(float gainReductionDb) noexcept
    {
        // C6 arm only: the 100 Hz even static part, as fetFeedbackK3.
        return fetFeedbackColourAt(kFetFeedbackLowFrequencyK2Coefficients, fetFeedbackColourRow(gainReductionDb));
    }

    static float fetBroadbandK3(float gainReductionDb) noexcept
    {
        // The black-face unit's broadband third harmonic is not reproduced by
        // one constant cubic. Its equivalent coefficient grows modestly from
        // compression onset to 8 dB of reduction, then falls to roughly one
        // third of the old -0.006 coefficient by 33 dB.
        //
        // COORDINATE. As for `fetBroadbandK2`, every anchor is expressed in the
        // call site's `-gainToDecibels(envelope + 0.001f)` coordinate, measured
        // in process by `MultiCompCoreTest --fet-h2-surface`. It is not the
        // settled output-referred reduction printed by the VST render probe.
        //
        // MEASUREMENT. The zero and full cubic builds identify the complex 3f
        // contribution over the dense 1 kHz surface. Separate half-cubic builds
        // reproduce their complex midpoint to 0.003712 dB before, and 0.003494
        // dB after, the independently fitted low-frequency T3 correction. That
        // establishes this coefficient as a scalable source rather than a
        // magnitude coincidence. The first fit landed at 0.089538 dB measured
        // worst over the 36 reference rows above the -92 dBc floor. Changing
        // low T3 then exposed this path's finite 1 kHz leakage, so the same
        // zero/half/full inversion was repeated on top of it. Retaining anchors
        // until piecewise interpolation is within 0.10 dB now predicts 0.097975
        // dB worst at 1 kHz while preserving 100 Hz at 0.631009 dB worst.
        //
        // This table fits H3 only. The same control predicts up to 4.93 dB H5
        // error, which remains assigned to the separately measured fifth-order
        // path rather than being hidden in this coefficient.
        constexpr std::array<float, 13> reductionDb{{
            0.0f, 0.3543f, 1.0175f, 3.2496f, 6.9921f, 10.0089f, 12.6634f,
            19.8974f, 22.6659f, 27.5467f, 31.0579f, 33.4397f, 40.0f}};
        constexpr std::array<float, 13> coefficients{{
            -0.004527116f, -0.004527116f, -0.005052484f, -0.005072347f,
            -0.005519443f, -0.005649644f, -0.005065947f, -0.003015710f,
            -0.002580895f, -0.002362978f, -0.001939411f, -0.001867239f,
            -0.001867239f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetLowFrequencyK3(float gainReductionDb) noexcept
    {
        // The low-frequency third-order transformer path is separately
        // reduction-dependent. A constant -0.0058 coefficient leaves the
        // installed unit's 100 Hz H3 up to 5.51 dB away, but this path is not
        // perfectly band-limited: fitting 100 Hz alone moves 1 kHz H3 by more
        // than 1 dB and undoes the broadband cubic fit above.
        //
        // COORDINATE. These are anchors in the same call-site internal-GR
        // coordinate as the broadband K2/K3 tables, measured by
        // `MultiCompCoreTest --fet-h2-surface`.
        //
        // MEASUREMENT. Zero, half, and full low-T3 builds prove complex
        // superposition to 0.000226 dB worst over scoreable H3. A constrained
        // fit uses all 96 scoreable 100 Hz H3 rows while bounding all 36
        // scoreable 1 kHz rows, then removes anchors until either frequency's
        // predeclared guard would fail. This eight-anchor result predicts
        // 0.606240 dB worst at 100 Hz and 0.897453 dB at 1 kHz, compared with
        // 5.510934 and 0.089538 dB for the constant coefficient. The broadband
        // residual is intentionally closed again by that mechanism's own fit;
        // it is kept visible here rather than hidden in an unconstrained
        // low-frequency table.
        //
        // H5 is not part of this fit and remains assigned to the independent
        // fifth-order term below.
        constexpr std::array<float, 8> reductionDb{{
            0.0f, 0.2378f, 0.5373f, 0.7234f, 1.3622f, 2.5753f, 13.8515f,
            40.0f}};
        constexpr std::array<float, 8> coefficients{{
            -0.002084180f, -0.004712892f, -0.003397340f, -0.003469579f,
            -0.005932644f, -0.006994165f, -0.004806491f, -0.006058491f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static std::array<float, 4> fetShallowKneeOddCorrection(
        float drivenInputDb) noexcept
    {
        // The 100 Hz shallow-knee H3 is complex: the installed unit's phase
        // moves from 164.6 to 90.7 degrees while the reduction cell engages.
        // A scalar correction can match magnitude only by jumping between two
        // coefficient roots. These two coefficients instead scale the first
        // and second 300 Hz low-pass poles of a pure T3 basis made from the
        // held raw input. The matching pair of filtered T5 bases jointly holds
        // the prior H5 complex vector, preventing the T3 correction from
        // inheriting fifth harmonic from reconstruction ripple. Using the
        // detector input rather than the already-compressed colour waveform
        // and retaining both poles prevents H5 leakage; omitting the poles
        // moved the established 1 kHz H3 guard by 0.74 dB. Even
        // half-dB rows from -14 through -6 dB are the fit anchors; the
        // intervening quarter-dB reference rows are held out. The endpoints
        // fade the cell to zero outside the measured 4:1 knee and preserve the
        // established deeper-reduction harmonic surface.
        constexpr std::array<float, 19> driveDb{{
            -15.0f, -14.0f, -13.5f, -13.0f, -12.5f, -12.0f, -11.5f,
            -11.0f, -10.5f, -10.0f, -9.5f, -9.0f, -8.5f, -8.0f,
            -7.5f, -7.0f, -6.5f, -6.0f, -4.0f}};
        constexpr std::array<float, 19> direct{{
            0.0f, 0.000000147296f, -0.000012437259f, -0.000035329776f,
            -0.000073386195f, -0.000116595019f, -0.000146058870f,
            -0.000167959659f, -0.000201904590f, -0.000242491601f,
            -0.000318190851f, -0.000423721595f, -0.000564032270f,
            -0.000698676667f, -0.000822144740f, -0.000966098630f,
            -0.001131396372f, -0.001393907435f, 0.0f}};
        constexpr std::array<float, 19> lag{{
            0.0f, -0.000004630468f, 0.000020562542f, 0.000067365078f,
            0.000146981010f, 0.000236175923f, 0.000293332999f,
            0.000334985008f, 0.000396516160f, 0.000387766529f,
            0.000409416966f, 0.000448438044f, 0.000549813571f,
            0.000622585136f, 0.000659935770f, 0.000737630182f,
            0.000851202512f, 0.001211519530f, 0.0f}};
        constexpr std::array<float, 19> fifthLag{{
            0.0f, -0.000000015059f, -0.000000011610f, -0.000000001779f,
            0.000000020677f, 0.000000045071f, 0.000000046848f,
            0.000000042798f, 0.000000028838f, -0.000000269259f,
            -0.000000699823f, -0.000001267242f, -0.000001847419f,
            -0.000002489201f, -0.000003196090f, -0.000003875615f,
            -0.000004587354f, -0.000005053848f, 0.0f}};
        constexpr std::array<float, 19> fifthLower{{
            0.0f, -0.000000019836f, 0.000000139720f, 0.000000432226f,
            0.000000928802f, 0.000001480890f, 0.000001844100f,
            0.000002119219f, 0.000002523170f, 0.000002663537f,
            0.000003078891f, 0.000003709499f, 0.000004741675f,
            0.000005619440f, 0.000006332498f, 0.000007265373f,
            0.000008476385f, 0.000011060490f, 0.0f}};
        if (drivenInputDb <= driveDb.front()
            || drivenInputDb >= driveDb.back())
            return {{0.0f, 0.0f, 0.0f, 0.0f}};
        for (size_t i = 1; i < driveDb.size(); ++i)
            if (drivenInputDb <= driveDb[i])
            {
                const float fraction = (drivenInputDb - driveDb[i - 1])
                    / (driveDb[i] - driveDb[i - 1]);
                // The fitted -14..-6 dB anchors are uniformly spaced. Cubic
                // interpolation keeps their measured curvature without using
                // any of the intervening quarter-dB validation rows. The two
                // unequal endpoint fades remain linear.
                if (i - 1 >= 2 && i <= 16)
                {
                    const auto cubic = [fraction](float p0, float p1,
                                                  float p2, float p3) noexcept {
                        const float t2 = fraction * fraction;
                        const float t3 = t2 * fraction;
                        return 0.5f * (2.0f * p1
                            + (-p0 + p2) * fraction
                            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3)
                                * t2
                            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
                    };
                    return {{
                        cubic(direct[i - 2], direct[i - 1], direct[i],
                              direct[i + 1]),
                        cubic(lag[i - 2], lag[i - 1], lag[i], lag[i + 1]),
                        cubic(fifthLag[i - 2], fifthLag[i - 1], fifthLag[i],
                              fifthLag[i + 1]),
                        cubic(fifthLower[i - 2], fifthLower[i - 1],
                              fifthLower[i], fifthLower[i + 1])}};
                }
                return {{
                    direct[i - 1] + fraction
                        * (direct[i] - direct[i - 1]),
                    lag[i - 1] + fraction * (lag[i] - lag[i - 1]),
                    fifthLag[i - 1] + fraction
                        * (fifthLag[i] - fifthLag[i - 1]),
                    fifthLower[i - 1] + fraction
                        * (fifthLower[i] - fifthLower[i - 1])}};
            }
        return {{0.0f, 0.0f, 0.0f, 0.0f}};
    }

    static std::array<float, 4> fetBroadbandComplexH3Correction(
        float inputPosition, float releasePosition,
        float drivenInputDb, double hostSampleRate) noexcept
    {
        // The sparse reduction-only K3 fit gets the original campaign anchors'
        // magnitudes right, but misses the reference vector between them: at Input
        // 0.8 its H3 phase rotates by more than 120 degrees while the cubic
        // stays near 135 degrees. A pure raw-input T3 and its first three
        // 300 Hz poles provide four real degrees of freedom: two solve the
        // reference's complex 1 kHz H3 and two force the same cell's complex
        // 100 Hz H3 contribution to zero. T3 contributes no H1/H2/H5.
        //
        // The Wave 27 scalar contradiction was a missing coordinate, not a
        // rate law: its matched-reduction probe used Release 0.5, while the
        // original harmonic grid used 0.66595459. The required lag coefficient
        // changes sign between those controls. Both tables below are fitted at
        // 48 kHz. Release-0.5 rows set a 0.9826 source normalisation at 96 kHz;
        // four untouched campaign-Release rows then validate to 0.075 dB / 0.42
        // degrees. Release interpolation holds its measured endpoints.
        constexpr std::array<float, 9> halfReleaseDriveDb{{
            -10.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 34.0f}};
        constexpr std::array<std::array<float, 9>, 4> halfRelease{{
            {{-0.000001129286f, 0.000005640098f, 0.000020928763f,
               0.000015788718f, 0.000013878506f, 0.000017151017f,
               0.000011323861f, 0.000009239721f, 0.000010164660f}},
            {{0.000126741287f, 0.000260478093f, 0.000207309249f,
              0.000174141674f, 0.000104720559f, 0.000050368387f,
              0.000024496578f, 0.000013932452f, 0.000010243772f}},
            {{-0.000253599807f, -0.000537741764f, -0.000462143430f,
              -0.000384481333f, -0.000240319580f, -0.000137359480f,
              -0.000073002796f, -0.000047337873f, -0.000041811016f}},
            {{0.000248913519f, 0.000543718419f, 0.000499131315f,
              0.000412038918f, 0.000265486269f, 0.000170002288f,
              0.000094725879f, 0.000065180976f, 0.000061539456f}},
        }};
        constexpr std::array<float, 8> campaignReleaseDriveDb{{
            -9.396131f, 2.603869f, 8.603869f, 14.603869f,
            20.603869f, 26.603869f, 29.603869f, 32.603869f}};
        constexpr std::array<std::array<float, 8>, 4> campaignRelease{{
            {{-0.000001761571f, 0.000028899354f, 0.000019785929f,
               0.000020777596f, 0.000019925249f, 0.000010698004f,
               0.000010906451f, 0.000010859942f}},
            {{0.000048332712f, -0.000289051296f, -0.000162517054f,
              -0.000130925980f, -0.000093221427f, -0.000059479269f,
              -0.000046187095f, -0.000036586152f}},
            {{-0.000093944202f, 0.000523663427f, 0.000287074460f,
              0.000221217220f, 0.000146846412f, 0.000097882236f,
              0.000070606261f, 0.000051314367f}},
            {{0.000089547765f, -0.000461366619f, -0.000245113153f,
              -0.000177927938f, -0.000105963674f, -0.000075748103f,
              -0.000048322742f, -0.000029309312f}},
        }};
        const auto tableValue = [drivenInputDb](const auto& positions,
                                                const auto& values) noexcept {
            if (drivenInputDb <= positions.front()) return values.front();
            for (size_t i = 1; i < positions.size(); ++i)
                if (drivenInputDb <= positions[i])
                {
                    const float fraction
                        = (drivenInputDb - positions[i - 1])
                        / (positions[i] - positions[i - 1]);
                    return values[i - 1]
                        + fraction * (values[i] - values[i - 1]);
                }
            return values.back();
        };
        const float releaseBlend = std::clamp(
            (releasePosition - 0.5f) / (0.66595459f - 0.5f),
            0.0f, 1.0f);
        std::array<float, 4> coefficients{};
        for (size_t basis = 0; basis < coefficients.size(); ++basis)
        {
            const float half = tableValue(
                halfReleaseDriveDb, halfRelease[basis]);
            coefficients[basis] = half + releaseBlend * (tableValue(
                campaignReleaseDriveDb, campaignRelease[basis]) - half);
        }
        // Only Input 0.8 has the dense complex oracle. The nearest measured
        // Input 0.7/0.9 rows retain the established reduction-only surface;
        // interpolate continuously between those three measured positions.
        const float positionWeight = std::clamp(
            1.0f - std::abs(inputPosition - 0.8f) / 0.1f, 0.0f, 1.0f);
        const float rateBlend = std::clamp(
            static_cast<float>((hostSampleRate - 48000.0) / 48000.0),
            0.0f, 1.0f);
        const float rateScale = 1.0f - 0.0174f * rateBlend;
        for (float& coefficient : coefficients)
            coefficient *= positionWeight * rateScale;
        return coefficients;
    }

    static std::array<float, 4> fetBroadbandComplexH5Correction(
        float inputPosition, float releasePosition,
        float drivenInputDb, double hostSampleRate) noexcept
    {
        // The reduction-only broadband-T5 table matches the sparse campaign
        // magnitudes, but not their complex vectors, and it misses the dense
        // Release-0.5 surface by 5.13 dB / 137.48 degrees. Scaling that source
        // cannot close the gap: its least-squares optimum still leaves up to
        // 2.51 dB / 33.62 degrees. A raw T5 plus its first three 300 Hz poles
        // supplies four real degrees of freedom. Two solve the complex 1 kHz
        // H5 residual and two force the same cell's complex 100 Hz H5 to zero.
        // T5 contributes no H1-H4 for the settled calibration sine.
        //
        // The tables are same-stimulus reference fits at Release 0.5 and the original
        // campaign Release 0.66595459, separately at 48/96 kHz. Zero guards
        // below the -92 dBc scoring onset keep the correction dormant where
        // the reference fifth harmonic is not measurable. Input 0.2 and 0.8
        // are the only campaign positions with a scoreable complex oracle;
        // triangular weights leave their measured neighbours unchanged.
        constexpr std::array<float, 9> halfDriveDb{{
            -10.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 34.0f}};
        constexpr std::array<std::array<float, 4>, 9> half48{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.496762303e-05f, -6.385083138e-04f, 1.322520898e-03f, -2.605647916e-03f}},
            {{-3.514863744e-05f, -9.802532544e-04f, 2.028960828e-03f, -3.975562361e-03f}},
            {{-2.844470984e-05f, -7.681089883e-04f, 1.590256165e-03f, -3.122185053e-03f}},
            {{-1.812680163e-05f, -4.895134041e-04f, 1.013464766e-03f, -1.989751412e-03f}},
            {{-1.124180239e-05f, -3.045824937e-04f, 6.305764770e-04f, -1.237766028e-03f}},
            {{-6.849515997e-06f, -1.858045370e-04f, 3.846670558e-04f, -7.550101469e-04f}},
            {{-4.103466552e-06f, -1.111874142e-04f, 2.301909137e-04f, -4.518421406e-04f}},
            {{-2.953060083e-06f, -7.837672966e-05f, 1.622897785e-04f, -3.189758675e-04f}},
        }};
        constexpr std::array<std::array<float, 4>, 9> half96{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.199947933e-05f, -6.955178209e-04f, 1.417204358e-03f, -2.795922969e-03f}},
            {{-3.178495081e-05f, -1.039834703e-03f, 2.118426437e-03f, -4.171582779e-03f}},
            {{-2.495754847e-05f, -8.237529328e-04f, 1.678135300e-03f, -3.303008516e-03f}},
            {{-1.597939544e-05f, -5.263453760e-04f, 1.072272524e-03f, -2.110740121e-03f}},
            {{-9.907290171e-06f, -3.287461995e-04f, 6.696985743e-04f, -1.317771421e-03f}},
            {{-6.047465184e-06f, -2.011216196e-04f, 4.097062525e-04f, -8.060867201e-04f}},
            {{-3.672773926e-06f, -1.206963874e-04f, 2.458859059e-04f, -4.840796946e-04f}},
            {{-2.616770294e-06f, -8.514188609e-05f, 1.734619187e-04f, -3.416789155e-04f}},
        }};
        constexpr std::array<float, 8> campaignEightDriveDb{{
            -10.0f, 2.603869f, 8.603869f, 14.603869f, 20.603869f,
            26.603869f, 29.603869f, 32.603869f}};
        constexpr std::array<std::array<float, 4>, 8> campaignEight48{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-3.392391092e-05f, -1.252005652e-03f, 2.586600369e-03f, -4.992588415e-03f}},
            {{-2.295591452e-05f, -8.489944705e-04f, 1.753971953e-03f, -3.385138287e-03f}},
            {{-1.444476717e-05f, -5.389668373e-04f, 1.113416694e-03f, -2.147990378e-03f}},
            {{-8.945497246e-06f, -3.353536706e-04f, 6.927667765e-04f, -1.336185579e-03f}},
            {{-5.372873574e-06f, -2.021097288e-04f, 4.175060493e-04f, -8.051449580e-04f}},
            {{-4.179771556e-06f, -1.565668807e-04f, 3.234341623e-04f, -6.238524233e-04f}},
            {{-3.254192256e-06f, -1.217262133e-04f, 2.514626946e-04f, -4.850624161e-04f}},
        }};
        constexpr std::array<std::array<float, 4>, 8> campaignEight96{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.853863582e-05f, -1.331864879e-03f, 2.709335030e-03f, -5.249923320e-03f}},
            {{-2.058420386e-05f, -9.509113032e-04f, 1.934452523e-03f, -3.749883227e-03f}},
            {{-1.262977189e-05f, -5.699577274e-04f, 1.159570016e-03f, -2.249843134e-03f}},
            {{-8.677201044e-06f, -3.902275582e-04f, 7.939217905e-04f, -1.540609249e-03f}},
            {{-4.724630197e-06f, -2.104973889e-04f, 4.282735793e-04f, -8.313753642e-04f}},
            {{-3.800751490e-06f, -1.683110645e-04f, 3.424497924e-04f, -6.649333896e-04f}},
            {{-2.876872782e-06f, -1.261247400e-04f, 2.566260055e-04f, -4.984914149e-04f}},
        }};
        constexpr std::array<float, 3> campaignTwoDriveDb{{
            -10.0f, -5.827545f, 0.172455f}};
        constexpr std::array<std::array<float, 4>, 3> campaignTwo48{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.400488442e-05f, -9.349371226e-04f, 1.930961527e-03f, -3.717920381e-03f}},
            {{-3.593272669e-05f, -1.310352387e-03f, 2.707331605e-03f, -5.228575893e-03f}},
        }};
        constexpr std::array<std::array<float, 4>, 3> campaignTwo96{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.104118307e-05f, -9.690486077e-04f, 1.971370845e-03f, -3.821899905e-03f}},
            {{-3.033543485e-05f, -1.386319642e-03f, 2.820318067e-03f, -5.469396430e-03f}},
        }};
        const auto tableValue = [drivenInputDb](const auto& positions,
                                                const auto& values) noexcept {
            if (drivenInputDb <= positions.front()) return values.front();
            for (size_t i = 1; i < positions.size(); ++i)
                if (drivenInputDb <= positions[i])
                {
                    const float fraction = (drivenInputDb - positions[i - 1])
                        / (positions[i] - positions[i - 1]);
                    std::array<float, 4> result{};
                    for (size_t basis = 0; basis < result.size(); ++basis)
                        result[basis] = values[i - 1][basis]
                            + fraction * (values[i][basis]
                                - values[i - 1][basis]);
                    return result;
                }
            return values.back();
        };
        const float rateBlend = std::clamp(
            static_cast<float>((hostSampleRate - 48000.0) / 48000.0),
            0.0f, 1.0f);
        const auto rateValue = [rateBlend](const auto& low,
                                           const auto& high) noexcept {
            std::array<float, 4> result{};
            for (size_t basis = 0; basis < result.size(); ++basis)
                result[basis] = low[basis]
                    + rateBlend * (high[basis] - low[basis]);
            return result;
        };
        const auto halfEight = rateValue(
            tableValue(halfDriveDb, half48),
            tableValue(halfDriveDb, half96));
        const auto campaignEight = rateValue(
            tableValue(campaignEightDriveDb, campaignEight48),
            tableValue(campaignEightDriveDb, campaignEight96));
        const auto campaignTwo = rateValue(
            tableValue(campaignTwoDriveDb, campaignTwo48),
            tableValue(campaignTwoDriveDb, campaignTwo96));
        const float releaseBlend = std::clamp(
            (releasePosition - 0.5f) / (0.66595459f - 0.5f),
            0.0f, 1.0f);
        const float eightWeight = std::clamp(
            1.0f - std::abs(inputPosition - 0.8f) / 0.1f, 0.0f, 1.0f);
        const float twoWeight = std::clamp(
            1.0f - std::abs(inputPosition - 0.2f) / 0.1f, 0.0f, 1.0f);
        std::array<float, 4> result{};
        for (size_t basis = 0; basis < result.size(); ++basis)
        {
            const float eight = halfEight[basis]
                + releaseBlend * (campaignEight[basis] - halfEight[basis]);
            result[basis] = eightWeight * eight
                + twoWeight * releaseBlend * campaignTwo[basis];
        }
        return result;
    }

    static float fetLowFrequencyK5(float gainReductionDb) noexcept
    {
        // The low-frequency fifth-order transformer contribution also changes
        // with reduction. The former constant 0.00255 coefficient leaves the
        // installed unit's 100 Hz H5 up to 6.35 dB away.
        //
        // COORDINATE. These anchors use the same per-sample internal-GR
        // coordinate as the other FET harmonic tables. Zero, half, and full
        // low-T5 builds reproduce their complex H5 midpoint to 0.000280 dB,
        // establishing a scalable source. The constant-coefficient fit uses
        // every scoreable 100 Hz H5 row and retains anchors until piecewise
        // interpolation is within 0.10 dB. Production evaluates the table per
        // sample, however, and detector ripple traverses the steep onset
        // segment: recalibrating only the 0.8882 dB anchor from 0.001205130 to
        // 0.001150000 changes its measured error from +0.259219 to -0.079061
        // dB. The guard at 0.7234 dB keeps the already fitted 100 Hz H3
        // residual below its declared bound near compression onset.
        //
        // The finite 1 kHz leakage is deliberately not hidden in this table;
        // it belongs to the separately measured broadband fifth-order path.
        constexpr std::array<float, 18> reductionDb{{
            0.0f, 0.7234f, 0.8882f, 0.9345f, 1.1328f, 1.3622f, 1.7057f,
            2.1781f, 2.5753f, 3.1896f, 3.9931f, 6.9247f, 9.8790f,
            12.0660f, 13.8515f, 24.6525f, 34.3368f, 40.0f}};
        constexpr std::array<float, 18> coefficients{{
            0.002167500f, 0.002167500f, 0.001150000f, 0.001378785f,
            0.001957890f, 0.002406180f, 0.002787915f, 0.003019200f,
            0.002900880f, 0.002935050f, 0.002883795f, 0.002550765f,
            0.002316930f, 0.002024700f, 0.001853085f, 0.002283525f,
            0.002515575f, 0.002515575f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetBroadbandK5(float gainReductionDb) noexcept
    {
        // The remaining 1 kHz fifth harmonic is a distinct, high-passed
        // contribution. An unfiltered T5 source is measurably the wrong
        // mechanism: at half strength it improves 1 kHz but worsens the
        // already fitted 100 Hz H5 residual from 0.079061 to 0.717420 dB.
        // Two 250 Hz high-pass poles reduce that leakage while preserving the
        // mid-band contribution.
        //
        // COORDINATE. These anchors use the same per-sample internal-GR
        // coordinate as the other FET harmonic tables. Zero, half, and full
        // high-passed T5 controls reproduce their complex midpoint to
        // 0.000357 dB for H3 and 0.000165 dB for H5. The fit uses all 32
        // scoreable 1 kHz H5 rows and removes anchors until the predicted
        // piecewise-interpolation error reaches 0.10 dB. Its independent
        // guards predict 0.107523 dB worst at 100 Hz H5, 0.099039 dB at
        // 1 kHz H3, and 0.728298 dB at 100 Hz H3.
        constexpr std::array<float, 8> reductionDb{{
            0.0f, 2.6305f, 3.2496f, 4.5656f, 6.9921f, 13.9253f,
            33.4397f, 40.0f}};
        constexpr std::array<float, 8> coefficients{{
            -0.000098400f, -0.000098400f, -0.000104240f,
            -0.000101680f, -0.000086860f, -0.000062020f,
            -0.000093440f, -0.000093440f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetLowFrequencyK2(float gainReductionDb) noexcept
    {
        // The vintage unit's second even-order path: the low-frequency
        // transformer asymmetry, generated at the call site from a 250 Hz
        // two-pole copy of the saturated signal. This is its coefficient.
        //
        // COORDINATE. Same as `fetBroadbandK2` above and for the same reason:
        // the argument is the call site's own `-gainToDecibels(envelope +
        // 0.001f)`, per sample, and every anchor here was measured in exactly
        // that coordinate by `MultiCompCoreTest --fet-h2-surface`.
        //
        // MEASUREMENT. 91 compressing operating points at 100 Hz, Input
        // 0.2-1.0 x source -48 to -6 dBFS (`probe_h2_surface.py`, the FET
        // reference-comparison harness). At 100 Hz mine's 2f output is the sum of
        // TWO contributions, this one and `fetBroadbandK2`, and they arrive
        // nearly in QUADRATURE -- the two-pole low pass turns -21.8 deg per
        // pole at 100 Hz and squaring doubles it, measured -78 to -86 deg
        // across the axis. So the coefficient each row asks for is the positive
        // root of |B + v C| = target, not a ratio of magnitudes; B and C are
        // the broadband and low-frequency complex second harmonics, read
        // separately by rebuilding with the other coefficient zeroed.
        // Superposition was verified rather than assumed: a build at half this
        // coefficient reproduces B + 0.5 C over 198 operating points to
        // 2.7e-05 dB.
        //
        // The result is a clean single-valued function of the reduction.
        // Triplets that reach the same depth from three different (Input,
        // source) combinations agree to 0.07-0.35 %: 9.88/9.89/9.94 dB ask
        // 0.030548/0.030563/0.030574, and 14.83/14.84/14.88 dB ask
        // 0.032141/0.032165/0.032153. That last triplet is the point. The law
        // this replaced was `0.0400 * clamp(grDb / 12) * saturation`, whose
        // charge term SATURATES at 12 dB, so every row above it got the same
        // coefficient; the reference keeps asking for 3.7 % more between 12.6
        // and 18.8 dB, and the campaign read that as a mysterious second
        // GR-dependent term the charge did not carry. It is not a second term.
        // It is the reduction axis the clamp threw away.
        //
        // Every segment holds at least one measured row INSIDE it, not only at
        // its edges -- the hole that let `fetBroadbandK2`'s predecessor read
        // 5.14 dB high. Above 34.35 dB nothing is measured and the last anchor
        // holds rather than extrapolating.
        //
        // Worst residual over the 91 rows: 0.189 dB, and the six worst all sit
        // below 1.4 dB of reduction. That band is NOT this table's to fix: with
        // this coefficient at zero the broadband term alone still reads
        // +0.06 to +0.14 dB high there, because `fetBroadbandK2` is fitted at
        // 1 kHz. Driving this table negative is not physical and is not the
        // answer; the floor moves when that table gains 100 Hz support.
        constexpr std::array<float, 14> reductionDb{{
            0.0f, 0.9f, 1.4f, 2.0f, 2.6f, 4.0f, 7.0f, 8.9f, 12.6f, 18.0f,
            22.0f, 28.0f, 31.0f, 40.0f}};
        constexpr std::array<float, 14> coefficients{{
            0.000000f, 0.000497f, 0.005382f, 0.009019f, 0.011509f, 0.016751f,
            0.024972f, 0.029460f, 0.032097f, 0.032621f, 0.032929f, 0.032396f,
            0.031305f, 0.031302f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    // The voice's settled sine law (FetVoice): a quadratic knee on the driven level, reduction slopes (1 - 1/R)
    // rather than the nominal faceplate ratios, as the law is the feed-forward equivalent of the feedback one.
    static float fetReferenceReductionDb(const FetVoice& voice, float inputLevelDb, int ratioIndex,
                                         float thresholdControlDb) noexcept
    {
        ratioIndex = std::clamp(ratioIndex, 0, voice.ratioCount - 1);
        const float slope = voice.reductionSlopes[static_cast<size_t>(ratioIndex)];
        // -10 dB is the reference-compatible extension setting. Moving the
        // extension shifts the measured transfer without changing its shape.
        const float threshold = voice.thresholdsDb[static_cast<size_t>(ratioIndex)]
            + thresholdControlDb + 10.0f;
        const float width = voice.kneeWidthsDb[static_cast<size_t>(ratioIndex)];
        const float kneeStart = threshold - width * 0.5f;
        if (inputLevelDb <= kneeStart) return 0.0f;
        // At the clockwise Input stop the installed unit stops adding reduction
        // near the top of every ratio's detector domain. Dense 4:1 measurements
        // resolve output slopes of 0.98--1.00 after the bend; ten-row sibling
        // controls find the same plateau at progressively higher levels for
        // 8:1, 12:1, and 20:1. All-buttons reaches it at the 4:1 coordinate.
        // Holding the detector coordinate, rather than the computed reduction,
        // leaves the knee and every previously fitted row below +38.7 dB intact.
        inputLevelDb = std::min(
            inputLevelDb,
            voice.maximumDetectorLevelsDb[static_cast<size_t>(ratioIndex)]);
        if (inputLevelDb >= threshold + width * 0.5f)
            return slope * (inputLevelDb - threshold);
        const float position = inputLevelDb - kneeStart;
        return slope * position * position / (2.0f * width);
    }

    // The reduction at fetReferenceReductionDb's plateau: the most the law
    // gives at this ratio and threshold.
    static float fetStaticMaximumReductionDb(const FetVoice& voice, int ratioIndex,
                                             float thresholdControlDb) noexcept
    {
        const size_t r = static_cast<size_t>(std::clamp(ratioIndex, 0, voice.ratioCount - 1));
        return voice.reductionSlopes[r] * (voice.maximumDetectorLevelsDb[r]
            - (voice.thresholdsDb[r] + thresholdControlDb + 10.0f));
    }

    // Inverse of fetReferenceReductionDb on the output side (candidate C6):
    // the driven OUTPUT level at which the law settles with `reductionDb`, and
    // its slope in dB per dB of reduction. Past the detector-domain plateau the
    // law adds no reduction, so the level rises steeply there instead of
    // defining one. `plateau` = false leaves that out: the feedback loop then
    // limits the reduction it applies instead (fbFloorOnApplied).
    static float fetStaticOutputLevelDb(const FetVoice& voice, float reductionDb, int ratioIndex,
                                        float thresholdControlDb,
                                        float& slope,
                                        bool plateau = true) noexcept
    {
        return fetStaticOutputLevelDbT<lanes::Lane1>(voice, reductionDb, ratioIndex, thresholdControlDb, slope,
                                                     plateau);
    }
    // Whether a branch some lane may take is evaluated: on one lane exactly when it is taken (the scalar
    // branch); on two always, each lane then selecting its own (cheaper than a data-dependent branch).
    template <class L> static bool takes(typename L::M m) noexcept
    {
        if constexpr (L::kLanes == 1) return m; else { (void) m; return true; }
    }
    // The same over a lane policy (MultiCompLanes.hpp).
    template <class L>
    static typename L::F fetStaticOutputLevelDbT(const FetVoice& voice, typename L::F reductionDb, int ratioIndex,
                                                 float thresholdControlDb, typename L::F& slope,
                                                 bool plateau = true) noexcept
    {
        using lanes::vmax; using lanes::vsqrt;
        const size_t r = static_cast<size_t>(std::clamp(ratioIndex, 0, voice.ratioCount - 1));
        const float s = voice.reductionSlopes[r];
        const float threshold = voice.thresholdsDb[r]
            + thresholdControlDb + 10.0f;
        const float width = voice.kneeWidthsDb[r];
        const float kneeStart = threshold - width * 0.5f;
        const float maximumReduction
            = s * (voice.maximumDetectorLevelsDb[r] - threshold);
        constexpr float plateauSlope = 100.0f;
        const auto onPlateau = L::m(plateau) && reductionDb >= L::f(maximumReduction);
        const auto inKnee = !onPlateau && !(reductionDb >= L::f(s * width * 0.5f));
        const float linearSlope = 1.0f / s - 1.0f;
        slope = L::f(linearSlope);
        auto level = L::f(threshold) + reductionDb * slope;
        // The knee and plateau branches are taken for stretches (quiet or very deep reductions), so a branch on
        // any lane predicts well and saves both lanes the knee's square root and divisions in the linear range.
        if (MC2_UNLIKELY(L::any(inKnee)))
        {
            // Knee: reduction = s p^2 / (2 width), p the input's distance into it.
            const auto position = vsqrt(
                L::f(2.0f * width) * vmax(reductionDb, L::f(1.0e-9f)) / L::f(s));
            slope = L::sel(inKnee, L::f(width) / (L::f(s) * position) - L::f(1.0f), slope);
            level = L::sel(inKnee, L::f(kneeStart) + position - vmax(reductionDb, L::f(0.0f)), level);
        }
        if (MC2_UNLIKELY(L::any(onPlateau)))
        {
            slope = L::sel(onPlateau, L::f(plateauSlope), slope);
            level = L::sel(onPlateau, L::f(voice.maximumDetectorLevelsDb[r] - maximumReduction)
                + L::f(plateauSlope) * (reductionDb - L::f(maximumReduction)), level);
        }
        return level;
    }

    static float fetReferenceKneeOnsetDb(float inputLevelDb,
                                         float thresholdControlDb) noexcept
    {
        // The earlier five-point static grid did not resolve the bottom half
        // of the 4:1 knee. Independent 100 Hz and 1 kHz quarter-dB sweeps do:
        // the installed unit remains below 0.003 dB reduction through -10.75
        // dB driven level, then joins the established static law. The two
        // reference sweeps collapse within 0.00564 dB, so this is a common
        // low-level cell rather than the deeper LF detector-response surface.
        // Values are the 1 kHz reference reduction; the 100 Hz sweep is the
        // held-out frequency and is gated separately in the core test.
        constexpr std::array<float, 33> reductionDb{{
            0.001054679f, 0.001119700f, 0.001188390f, 0.001261232f,
            0.001338008f, 0.001419694f, 0.001506451f, 0.001597734f,
            0.001694829f, 0.001797734f, 0.001906877f, 0.002021984f,
            0.002144161f, 0.002273709f, 0.010022404f, 0.055453550f,
            0.129645571f, 0.224473074f, 0.335152209f, 0.456996590f,
            0.589745641f, 0.724483669f, 0.866982937f, 1.015019298f,
            1.167031527f, 1.323735118f, 1.482176304f, 1.643956780f,
            1.808799028f, 1.968619347f, 2.138448954f, 2.309558868f,
            2.483170986f,
        }};
        // Move the table with the existing Threshold extension control. At
        // its reference-compatible -10 dB setting this offset is exactly zero.
        const float thresholdShift = thresholdControlDb + 10.0f;
        const float level = inputLevelDb - thresholdShift;
        constexpr float firstLevelDb = -14.0f;
        constexpr float lastLevelDb = -6.0f;
        constexpr float referenceKneeFloorDb = -15.404937f;
        if (level <= referenceKneeFloorDb) return 0.0f;
        if (level < firstLevelDb)
            return reductionDb.front()
                * (level - referenceKneeFloorDb)
                / (firstLevelDb - referenceKneeFloorDb);
        if (level <= lastLevelDb)
        {
            const float tablePosition = (level - firstLevelDb) * 4.0f;
            const size_t low = static_cast<size_t>(std::min(
                static_cast<int>(reductionDb.size()) - 2,
                std::max(0, static_cast<int>(std::floor(tablePosition)))));
            const float fraction = tablePosition - static_cast<float>(low);
            return reductionDb[low]
                + fraction * (reductionDb[low + 1] - reductionDb[low]);
        }
        // The measured onset is almost tangent to the established curve by
        // -4 dB. Join there and leave every deeper static/maximum-GR anchor on
        // the accepted law rather than extrapolating the new table.
        constexpr float joinLevelDb = -4.0f;
        const float shiftedJoinLevelDb = joinLevelDb + thresholdShift;
        const float joinedReduction = fetReferenceReductionDb(
            kFetBlackVoice, shiftedJoinLevelDb, 0, thresholdControlDb);
        if (level < joinLevelDb)
        {
            const float fraction = (level - lastLevelDb)
                / (joinLevelDb - lastLevelDb);
            return reductionDb.back()
                + fraction * (joinedReduction - reductionDb.back());
        }
        return fetReferenceReductionDb(kFetBlackVoice, inputLevelDb, 0, thresholdControlDb);
    }

    static float fetAllProcessorCorrectionDb(float inputLevelDb) noexcept
    {
        // All-buttons has a separate 1.20 dB small-signal gain and a curved
        // transition into limiting. This correction was measured after the
        // common analog stages, on a dense 997 Hz grid, so the target below
        // accounts for their level-dependent residual instead of applying a
        // second output trim. Positions are in `detectorLevelDb` and are
        // consumed as passed; the recurring .145 fraction in them is inherited
        // from the grid the original measurement was laid out on and is NOT an
        // offset to be applied to or removed from the argument (see the note on
        // the coordinate below). Intermediate points are linearly interpolated.
        //
        // The first FIVE entries are the small-signal plateau: below the
        // All-buttons knee no reduction is generated, so -(entry) IS the
        // small-signal gain and it must be the reference's measured 1.20 dB.
        // The flat +0.15 dB that used to be added to this function's result at
        // the call site is a fit constant belonging to the compressing region
        // only, and is now carried by the entries from -6.145 dB upward --
        // where it was fitted -- instead of by every entry. Applied to the
        // plateau it made the small-signal gain 1.048 dB. That was invisible
        // while the chain carried the +0.149329 dB excess removed in
        // fetInputGainDb, which very nearly cancelled it; the scalar-immune
        // measurement is the All-buttons-minus-4:1 output difference at Input
        // 0.4 / -36 dBFS, where both laws are below their knees: reference
        // +1.19992 dB, this plugin +1.04601 dB before the change.
        //
        // -7.224 dB ends the plateau. The 2 dB sweep the table was originally
        // fitted on samples this transition at exactly the knot spacing, so it
        // could not see the segment interiors; a 0.5 dB sweep of the same
        // operating point (Input 0.4, All buttons, Modern curve) shows the
        // reference producing NO reduction at all up to -7.5 dB internal --
        // consecutive 0.5 dB steps come back +0.499629 / +0.499584 / +0.499533
        // / +0.499476 / +0.499412 dB -- and then knee-ing hard. A single
        // straight segment from -8.145 to -6.145 starts compressing 0.9 dB too
        // early and read -0.206 / -0.334 / -0.272 dB against the reference at
        // -7.5 / -7.0 / -6.5. Extending the plateau and re-fitting the -6.145
        // entry against that sweep leaves at worst 0.022 dB across the whole
        // transition.
        //
        // The knot's value came out of least squares, but the best-determined
        // thing at that abscissa is this plugin's OWN slope break: with the
        // campaign's pinned -10 dB threshold `fetReferenceReductionDb` starts
        // generating All-buttons reduction at -4.74344 - 10 + 10 - 4.87980/2 =
        // -7.18334 dB, which the fit landed 0.041 dB from. The reference's own
        // knee onset is only bounded to (-7.65, -7.15) by the 0.5 dB grid, so
        // it does not pin the knot nearly as well; do not re-derive this
        // position from "where the reference knees" alone.
        //
        // The argument is `detectorLevelDb` as passed, with no offset. A
        // reference-free inversion pins that: the only difference between the
        // two builds either side of this re-fit is this table, so the measured
        // output difference over the 0.5 dB grid IS the difference of the two
        // tables sampled at the consumption argument, and solving the one
        // unknown gives detectorLevelDb - 0.008 (+/-0.03). An earlier note here
        // claimed a +0.130 dB effective offset from detector ripple; that was
        // the Wave 5 -0.149329 dB input-gain correction subtracted twice in the
        // analysis coordinate, not a property of the chain. It cost the fit
        // 0.009 dB, which is far inside what the grid resolves, but do not
        // carry that offset into a fit of the segments above.
        constexpr std::array<float, 23> positions{{
            -30.145f, -20.145f, -12.145f, -8.145f, -7.224f, -6.145f,
            -4.145f, -2.145f, -0.145f, 1.855f, 2.608f, 3.855f,
            7.149f, 13.064f, 14.608f, 15.855f, 19.234f, 25.234f,
            26.608f, 27.855f, 32.608f, 33.855f, 40.0f}};
        constexpr std::array<float, 23> corrections{{
            -1.20005f, -1.19849f, -1.19245f, -1.18313f, -1.18313f,
            -0.55515f, 0.48398f, 0.67817f, 0.57889f, 0.51182f,
            0.49441f, 0.46731f, 0.41443f, 0.42950f, 0.46340f,
            0.44900f, 0.48120f, 0.51680f, 0.51190f, 0.50250f,
            0.54200f, 0.54070f, 0.54070f}};
        if (inputLevelDb <= positions.front()) return corrections.front();
        for (size_t i = 1; i < positions.size(); ++i)
            if (inputLevelDb <= positions[i])
            {
                const float fraction = (inputLevelDb - positions[i - 1])
                    / (positions[i] - positions[i - 1]);
                return corrections[i - 1]
                    + fraction * (corrections[i] - corrections[i - 1]);
            }
        return corrections.back();
    }

    static float fetAllOverloadMemoryDb(float baseReductionDb) noexcept
    {
        // The Modern All-buttons arm carries an additional slow high-level
        // population below its maximum-reduction plateau. A 9--11.5 second
        // sweep measures +0.312 / +0.345 / +0.228 dB missing reduction at
        // nominal internal levels +34 / +37 / +38 dB, falling to zero when the
        // overload cap engages. The coordinate here is the reduction produced
        // by the existing static law at the call site, before this correction.
        constexpr std::array<float, 5> reductionDb{{
            36.00f, 37.65f, 40.53f, 41.49f, 41.78f}};
        constexpr std::array<float, 5> memoryDb{{
            0.000f, 0.312f, 0.345f, 0.228f, 0.000f}};
        if (baseReductionDb <= reductionDb.front()) return memoryDb.front();
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (baseReductionDb <= reductionDb[i])
            {
                const float fraction = (baseReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return memoryDb[i - 1]
                    + fraction * (memoryDb[i] - memoryDb[i - 1]);
            }
        return memoryDb.back();
    }

    static float fetRatioAttackScale(const FetVoice& voice, int ratioIndex) noexcept
    {
        return voice.attackScales[static_cast<size_t>(std::clamp(ratioIndex, 0, voice.ratioCount - 1))];
    }

    static float fetRatioReleaseScale(const FetVoice& voice, int ratioIndex) noexcept
    {
        return voice.releaseScales[static_cast<size_t>(std::clamp(ratioIndex, 0, voice.ratioCount - 1))];
    }

    static float fetAttackPosition(float legacyMilliseconds) noexcept
    {
        // Invert the existing descriptor's 0.3 skew to recover the host knob
        // position without changing its saved-state or automation domain.
        const float proportion = std::clamp(
            (legacyMilliseconds - 0.02f) / (80.0f - 0.02f), 0.0f, 1.0f);
        return std::pow(proportion, 0.3f);
    }

    static float fetAttackKnobScale(float attackPosition,
                                    float positiveReductionDb) noexcept
    {
        // A 4 kHz carrier resolves the installed unit's slow half of the Attack
        // knob without the 1 kHz estimator's 0.5 ms floor. The correction is
        // programme dependent: a single multiplier fixed the shallow row but
        // made the deep row slower even though it needed to move in the other
        // direction. Interpolate the independently measured shallow and deep
        // corrections, holding both ends outside the measured 14--34 dB span.
        // The midpoint and upper half retain the established drive-axis law.
        const float depth = std::clamp(
            (positiveReductionDb - 14.0f) / 20.0f, 0.0f, 1.0f);
        const float atSlowStop = 1.45f - 0.20f * depth;
        const float atQuarter = 1.25f - 0.25f * depth;
        if (attackPosition <= 0.25f)
            return atSlowStop
                + 4.0f * attackPosition * (atQuarter - atSlowStop);
        if (attackPosition < 0.50f)
            return atQuarter
                + 4.0f * (attackPosition - 0.25f) * (1.0f - atQuarter);
        return 1.0f;
    }

    static float fetReleasePosition(float legacyMilliseconds) noexcept
    {
        return std::clamp(
            (legacyMilliseconds - 50.0f) / (1100.0f - 50.0f), 0.0f, 1.0f);
    }

    // New native phrase/gap probes constrain deep release. Keep the existing
    // shallow knee below the post-burst population's 8 dB activation boundary;
    // the next measured depth anchor supplies the continuous handoff.
    static float fetProgrammeReleaseDepth(float maximumGrDb) noexcept
    {
        return std::clamp((maximumGrDb - 8.0f) / (9.1406f - 8.0f), 0.0f, 1.0f);
    }

    static float fetProgrammeFastReleaseSeconds(float position) noexcept
    {
        // The native .9 and 1.0 controls have the same fast recovery. The old
        // quadratic missed that floor and over-held phrases by several dB.
        // Retain the lower half, which owns the existing quiet-tail captures.
        if (position <= 0.5f)
            return 2.00f - 1.82f * position + 0.12f * position * position;
        if (position <= 0.75f)
            return 1.12f + (0.20f - 1.12f) * (position - 0.5f) * 4.0f;
        if (position < 0.9f)
            return 0.20f + (0.075f - 0.20f) * (position - 0.75f) / 0.15f;
        return 0.075f;
    }

    static float fetReleaseExposureScale(float exposureSeconds,
                                         float maximumGrDb,
                                         const FetDynamicsParams& dyn) noexcept
    {
        constexpr std::array<float, 4> exposures{{0.01f, 0.10f, 1.0f, 5.0f}};
        const std::array<float, 4>& lowDrive = dyn.exposureLowDrive;
        const std::array<float, 4>& midDrive = dyn.exposureMidDrive;
        const std::array<float, 4>& highDrive = dyn.exposureHighDrive;
        const auto interpolateExposure = [&exposures, exposureSeconds](
            const std::array<float, 4>& values) noexcept {
            if (exposureSeconds <= exposures.front()) return values.front();
            for (size_t i = 1; i < exposures.size(); ++i)
                if (exposureSeconds <= exposures[i])
                {
                    const float lowLog = std::log(exposures[i - 1]);
                    const float highLog = std::log(exposures[i]);
                    const float fraction = (std::log(exposureSeconds) - lowLog)
                        / (highLog - lowLog);
                    return values[i - 1]
                        + fraction * (values[i] - values[i - 1]);
                }
            return values.back();
        };
        const float low = interpolateExposure(lowDrive);
        const float mid = interpolateExposure(midDrive);
        const float high = interpolateExposure(highDrive);
        const float drive = std::clamp((maximumGrDb - 14.0f) / 20.0f,
                                       0.0f, 1.0f);
        return drive <= 0.5f
            ? low + drive * 2.0f * (mid - low)
            : mid + (drive - 0.5f) * 2.0f * (high - mid);
    }

    static float fetPostBurstRecoveryAssistDb(
        float elapsedSeconds, float maximumGrDb, int ratioIndex,
        float terminalWeight, float attackPosition, float releasePosition,
        float hostSampleRate) noexcept
    {
        // The installed cell sheds a finite terminal-charge population after
        // programme support disappears. It is separate from the ordinary
        // asymptotic release: the candidate/reference difference is 1.1--3.4 dB in
        // the first 250 ms recovery window and has crossed zero by 3.25 s.
        // Maximum reduction owns the population size/shape; the final active
        // detector sample supplies the measured phase coordinate that a
        // phase-blind release scalar could not reproduce.
        const float drive = std::clamp(
            (maximumGrDb - 14.0f) / 20.0f, 0.0f, 1.0f);
        if (maximumGrDb < 8.0f || elapsedSeconds < 0.0f) return 0.0f;
        const float lowerDrive = std::min(drive * 2.0f, 1.0f);
        const float upperDrive = std::max(drive * 2.0f - 1.0f, 0.0f);
        float amplitudeDb = 1.93f
            + lowerDrive * (2.85f - 1.93f)
            + upperDrive * (3.38f - 2.85f);
        float exponent = 4.00f
            + lowerDrive * (2.87f - 4.00f)
            + upperDrive * (1.50f - 2.87f);
        const float rate = std::clamp(
            (hostSampleRate - 48000.0f) / 48000.0f, 0.0f, 1.0f);
        const float phase = std::clamp(terminalWeight, 0.0f, 1.0f);
        amplitudeDb += phase * drive * (0.69f + 0.13f * rate);
        exponent += phase * drive * (0.25f - 0.07f * rate);
        amplitudeDb += 0.20f * drive
            * (1.0f - std::clamp(attackPosition, 0.0f, 1.0f));
        if (ratioIndex == 1)
        {
            amplitudeDb *= 1.15f;
            exponent += 0.70f;
        }
        else if (ratioIndex != 0)
            return 0.0f;
        const float nominalReleaseSeconds
            = 2.00f - 1.82f * releasePosition
                + 0.12f * releasePosition * releasePosition;
        const float durationSeconds = 3.0f
            * nominalReleaseSeconds / 1.12f;
        const float position = 1.0f - elapsedSeconds
            / std::max(durationSeconds, 0.05f);
        const float terminalAssist = position > 0.0f
            ? amplitudeDb * std::pow(position, exponent) : 0.0f;
        // The slow tail crosses the reference after the terminal population
        // has discharged. This small bipolar arm is independently constrained
        // by the 1.875/3.375/4.875/5.875 s windows, where a positive-only
        // assist would leave up to +0.164 dB of over-recovery.
        float tailAmplitudeDb = drive <= 0.5f
            ? 0.17f
            : 0.17f + (drive - 0.5f) * 2.0f * (0.03f - 0.17f);
        if (ratioIndex == 1) tailAmplitudeDb += 0.08f;
        const float tailSeconds = elapsedSeconds * 1.12f
            / std::max(nominalReleaseSeconds, 0.05f);
        float tailShape = 0.0f;
        if (tailSeconds > 0.375f && tailSeconds <= 1.875f)
            tailShape = (tailSeconds - 0.375f) / 1.5f * 0.40f;
        else if (tailSeconds <= 3.375f && tailSeconds > 1.875f)
            tailShape = 0.40f
                + (tailSeconds - 1.875f) / 1.5f * 0.60f;
        else if (tailSeconds <= 4.875f && tailSeconds > 3.375f)
            tailShape = 1.0f
                - (tailSeconds - 3.375f) / 1.5f * 0.70f;
        else if (tailSeconds <= 5.875f && tailSeconds > 4.875f)
            tailShape = 0.30f
                - (tailSeconds - 4.875f) * 0.20f;
        else if (tailSeconds <= 6.5f && tailSeconds > 5.875f)
            tailShape = 0.10f
                * (1.0f - (tailSeconds - 5.875f) / 0.625f);
        return terminalAssist - tailAmplitudeDb
            * std::max(tailShape, 0.0f);
    }

    // The colour blend's own copy of the measured PR 0.7 static law: the
    // 2026-08-21 Compress/Limit measurements with the arithmetic the old
    // static-curve cell applied at PR 70, which keeps the blend
    // sample-identical. Nothing else in the core reads these tables; the
    // cell's static-curve tables and functions were removed on 2026-10-14
    // with the unused generated harmonics header that last called them.
    static constexpr std::array<float, 23> kOptoColourCompressCurve{{
        0.9207f, 1.9474f, 3.1430f, 4.6974f, 6.1696f, 7.6165f,
        9.2638f, 10.8676f, 12.4485f, 13.9366f, 15.7917f, 17.2499f,
        19.0803f, 20.4897f, 22.3318f, 23.8425f, 25.2656f, 26.5058f,
        28.2780f, 29.5828f, 30.6369f, 32.1649f, 32.9052f}};
    static constexpr std::array<float, 23> kOptoColourLimitCurve{{
        0.9379f, 1.9978f, 3.2454f, 4.8601f, 6.4467f, 8.0793f,
        9.6964f, 11.7394f, 13.5249f, 15.2602f, 17.4341f, 19.2438f,
        21.5010f, 23.3864f, 25.7863f, 27.9270f, 30.0609f, 32.0834f,
        34.7103f, 36.8851f, 38.8184f, 40.5691f, 40.9082f}};

    static float optoColourReferenceThresholdDb(bool limit) noexcept
    {
        constexpr std::array<float, 9> compressThresholds{{
            -3.8483f, -10.8579f, -17.0206f, -21.4516f, -25.7740f,
            -33.8121f, -40.1926f, -44.3555f, -45.4059f}};
        constexpr std::array<float, 9> limitThresholds{{
            -4.1256f, -11.1198f, -17.2740f, -21.6889f, -26.0047f,
            -34.0625f, -40.5015f, -44.6568f, -45.6471f}};
        const auto& thresholds = limit ? limitThresholds : compressThresholds;
        const auto& curve = limit ? kOptoColourLimitCurve
                                  : kOptoColourCompressCurve;
        const float onsetOffset = (1.0f - curve[0])
            / ((curve[1] - curve[0]) * 0.5f);
        const float thresholdCorrection = -onsetOffset;
        const float peakReduction = 70.0f;
        const float normalised = std::clamp(peakReduction * 0.01f, 0.0f, 1.0f);
        const float position = (normalised - 0.2f) * 10.0f;
        const size_t index = static_cast<size_t>(position);
        const float fraction = position - static_cast<float>(index);
        return thresholds[index] + thresholdCorrection
            + fraction * (thresholds[index + 1] - thresholds[index]);
    }

    static float optoColourReferenceCurveDb(float overshootDb, bool limit) noexcept
    {
        const auto& curve = limit ? kOptoColourLimitCurve
                                  : kOptoColourCompressCurve;
        const float position = overshootDb * 0.5f;
        // Non-finite input samples are replaced before they reach any state,
        // but a huge finite one overflows the detector, whose level then
        // reads NaN. NaN fails both range tests below and would reach the
        // size_t conversion, which is undefined behaviour (UBSan:
        // float-cast-overflow). +/-inf is rejected too, defensively: -inf
        // already reaches 0 on both curves, and +inf would extrapolate to
        // +inf and leave the caller's blend ratio at 0 or NaN. No level, no
        // colour.
        if (!std::isfinite(position))
            return 0.0f;
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
            constexpr float compressInitialSlope = 1.90f;
            constexpr float slopeTransitionPositions = 0.10f;
            const float tableEndSlope = curve.back()
                - curve[curve.size() - 2];
            if (extraPosition < slopeTransitionPositions)
                return curve.back() + tableEndSlope * extraPosition
                    + (compressInitialSlope - tableEndSlope)
                        * extraPosition * extraPosition
                        / (2.0f * slopeTransitionPositions);
            const float limitSlope = kOptoColourLimitCurve.back()
                - kOptoColourLimitCurve[kOptoColourLimitCurve.size() - 2];
            const float limitContinuation = kOptoColourLimitCurve.back()
                + extraPosition * limitSlope;
            const float transitionValue = curve.back()
                + slopeTransitionPositions
                    * (tableEndSlope + compressInitialSlope) * 0.5f;
            const float limitAtTransition = kOptoColourLimitCurve.back()
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

    static std::array<float, 4> optoHarmonicRatios(
        float inputLevelDb, float compressionBlend) noexcept
    {
        // Six measured 1 kHz endpoints, stored as linear amplitude ratios
        // (10^(dBc/20)). Interpolating amplitudes makes each table endpoint
        // reproduce its measured dBc value exactly. Measurements outside the
        // -24/-12/-6 dBFS span are clamped because no extrapolation was measured.
        constexpr std::array<float, 3> levels{{-24.0f, -12.0f, -6.0f}};
        constexpr std::array<std::array<float, 4>, 3> uncompressed{{
            {{0.0005565446f, 0.0000726942f, 0.0000030832f, 0.0000058884f}},
            {{0.0021062019f, 0.0001267652f, 0.0000204644f, 0.0000541377f}},
            {{0.0033381051f, 0.0016730156f, 0.0004310226f, 0.0005787620f}}
        }};
        constexpr std::array<std::array<float, 4>, 3> compressed{{
            {{0.0023388372f, 0.0019386526f, 0.0011547820f, 0.0006974290f}},
            {{0.0032322132f, 0.0017278260f, 0.0004645153f, 0.0005217951f}},
            {{0.0021305906f, 0.0016311729f, 0.0003483373f, 0.0003384543f}}
        }};
        size_t lower = 0;
        float levelFraction = 0.0f;
        if (inputLevelDb >= levels.back())
            lower = levels.size() - 1;
        else if (inputLevelDb > levels.front())
        {
            lower = inputLevelDb < levels[1] ? 0u : 1u;
            levelFraction = (inputLevelDb - levels[lower])
                / (levels[lower + 1] - levels[lower]);
        }
        std::array<float, 4> ratios{};
        const size_t upper = std::min(lower + 1, levels.size() - 1);
        const float grFraction = std::clamp(compressionBlend, 0.0f, 1.0f);
        for (size_t harmonic = 0; harmonic < ratios.size(); ++harmonic)
        {
            const float clean = uncompressed[lower][harmonic]
                + (uncompressed[upper][harmonic] - uncompressed[lower][harmonic])
                    * levelFraction;
            const float reduced = compressed[lower][harmonic]
                + (compressed[upper][harmonic] - compressed[lower][harmonic])
                    * levelFraction;
            ratios[harmonic] = clean + (reduced - clean) * grFraction;
        }
        return ratios;
    }

    static float optoShortEventCorrectionDb(int durationSamples,
                                            float peakDbfs,
                                            float peakReduction) noexcept
    {
        // Residual charge at PR 0.70, Compress, measured through the two AUs.
        // The checkerboard cells in the core gate were not fitted: each is the
        // quadratic-in-level prediction from the other three levels at the
        // same duration.  The final zero row makes the onset-only population
        // vanish before settled measurements begin.
        constexpr std::array<float, 5> durations{{
            17.0f, 48.0f, 144.0f, 480.0f, 4800.0f}};
        constexpr std::array<float, 4> levels{{
            -12.0f, -8.0f, -4.0f, -0.25f}};
        constexpr std::array<std::array<float, 4>, 5> correction{{
            {{0.693504f, 1.765942f, 1.902607f, 1.390991f}},
            {{-0.002307f, 0.354538f, 0.157757f, 0.065774f}},
            {{0.854975f, 0.599699f, 0.570413f, -0.027363f}},
            {{0.821206f, 0.662275f, 0.508296f, 0.007319f}},
            {{0.0f, 0.0f, 0.0f, 0.0f}}
        }};
        // R9's charge map is anchored at PR 70.  R8's existing PR 100
        // recovery holdout must not inherit that one-axis refit.
        constexpr std::array<std::array<float, 4>, 5> highPrCorrection{{
            {{1.207076f, 2.566733f, 2.587469f, 1.390991f}},
            {{0.245839f, 0.354538f, 0.479467f, 0.611413f}},
            {{0.980175f, 0.751735f, 0.570413f, 0.442915f}},
            {{0.821206f, 0.793592f, 0.694284f, 0.534704f}},
            {{0.0f, 0.0f, 0.0f, 0.0f}}
        }};
        const auto span = [](float value, const float* points, int count) noexcept {
            struct Result { int lo, hi; float t; };
            if (value <= points[0]) return Result{0, 0, 0.0f};
            if (value >= points[count - 1])
                return Result{count - 1, count - 1, 0.0f};
            int hi = 1;
            while (hi < count && value > points[hi]) ++hi;
            const int lo = hi - 1;
            return Result{lo, hi,
                (value - points[lo]) / (points[hi] - points[lo])};
        };
        const auto ds = span(static_cast<float>(durationSamples),
                             durations.data(), static_cast<int>(durations.size()));
        const auto ls = span(peakDbfs, levels.data(), static_cast<int>(levels.size()));
        const auto levelMix = [&](const auto& table, int duration) noexcept {
            const float a = table[static_cast<size_t>(duration)]
                [static_cast<size_t>(ls.lo)];
            return a + (table[static_cast<size_t>(duration)]
                [static_cast<size_t>(ls.hi)] - a) * ls.t;
        };
        const auto durationMix = [&](const auto& table) noexcept {
            const float a = levelMix(table, ds.lo);
            return a + (levelMix(table, ds.hi) - a) * ds.t;
        };
        const float atPr70 = durationMix(correction);
        const float atPr100 = durationMix(highPrCorrection);
        const float highPrWeight = std::clamp(
            (peakReduction - 70.0f) / 30.0f, 0.0f, 1.0f);
        return atPr70 + (atPr100 - atPr70) * highPrWeight;
    }

    static float optoBroadbandCorrectionDb(float inputRmsDbfs) noexcept
    {
        // PR 0.70 Compress residual measured against the UAD broadband law.
        // -24 dBFS is deliberately absent: linear interpolation from the
        // adjacent fitted rows predicts its held-out residual.
        constexpr std::array<float, 6> levels{{
            -48.0f, -36.0f, -30.0f, -18.0f, -12.0f, 0.0f}};
        constexpr std::array<float, 6> correction{{
            0.0f, 0.544397f, 0.832127f, 0.340355f, 0.263519f, 0.0f}};
        if (inputRmsDbfs <= levels.front()) return correction.front();
        if (inputRmsDbfs >= levels.back()) return correction.back();
        size_t hi = 1;
        while (hi < levels.size() && inputRmsDbfs > levels[hi]) ++hi;
        const size_t lo = hi - 1;
        const float t = (inputRmsDbfs - levels[lo])
            / (levels[hi] - levels[lo]);
        return correction[lo] + (correction[hi] - correction[lo]) * t;
    }

    static float optoDetectorWeightingCorrectionDb(float frequencyHz) noexcept
    {
        constexpr std::array<float, 31> frequencies{{
            20.0f, 25.178508f, 31.697864f, 39.905247f, 50.237728f,
            63.245552f, 79.621437f, 100.237450f, 126.191467f,
            158.865646f, 200.0f, 251.785080f, 316.978638f,
            399.052460f, 502.377289f, 632.455505f, 796.214355f,
            1002.374451f, 1261.914673f, 1588.656494f, 2000.0f,
            2517.850830f, 3169.786377f, 3990.524658f, 5023.772949f,
            6324.555176f, 7962.143555f, 10023.745117f, 12619.146484f,
            15886.564453f, 20000.0f}};
        // Required change in gain reduction at -24 dBFS / PR 0.70.  This is
        // the UAD detector-weighting curve minus the landed R6 curve.
        constexpr std::array<float, 31> correction{{
            -0.462143f, -0.407804f, -0.363029f, -0.354412f, -0.312080f,
            -0.235951f, -0.139738f, -0.060460f, 0.011710f, 0.055458f,
            0.082746f, 0.123899f, 0.207278f, 0.283969f, 0.301318f,
            0.263028f, 0.214356f, 0.194554f, 0.209549f, 0.246582f,
            0.280679f, 0.300428f, 0.286160f, 0.259166f, 0.264597f,
            0.298314f, 0.186614f, 0.090670f, 0.223235f, 0.338185f,
            0.435184f}};
        if (frequencyHz <= frequencies.front()) return correction.front();
        if (frequencyHz >= frequencies.back()) return correction.back();
        size_t hi = 1;
        while (hi < frequencies.size() && frequencyHz > frequencies[hi]) ++hi;
        const size_t lo = hi - 1;
        const float logLo = std::log(frequencies[lo]);
        const float t = (std::log(frequencyHz) - logLo)
            / (std::log(frequencies[hi]) - logLo);
        return correction[lo] + (correction[hi] - correction[lo]) * t;
    }

    static float optoLongEventReleaseCorrectionDb(float gapMs) noexcept
    {
        constexpr std::array<float, 18> gaps{{
            0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 8.0f, 10.0f, 15.0f,
            25.0f, 30.0f, 40.0f, 60.0f, 120.0f, 250.0f, 500.0f,
            1000.0f, 2000.0f, 4000.0f}};
        constexpr std::array<float, 18> correction{{
            0.539774f, 0.539774f, 0.754907f, 0.662672f, 0.757838f,
            0.962865f, 0.941712f, 0.962770f, 0.874412f, 0.828487f,
            0.758302f, 0.617563f, 0.234985f, 0.095107f, 0.275758f,
            0.288961f, 0.203235f, 0.0f}};
        if (gapMs <= gaps.front()) return correction.front();
        if (gapMs >= gaps.back()) return correction.back();
        size_t hi = 1;
        while (hi < gaps.size() && gapMs > gaps[hi]) ++hi;
        const size_t lo = hi - 1;
        const float t = (gapMs - gaps[lo]) / (gaps[hi] - gaps[lo]);
        return correction[lo] + (correction[hi] - correction[lo]) * t;
    }

    static float optoProgrammeCorrectionDb(float peakReduction) noexcept
    {
        constexpr std::array<float, 5> reductions{{
            0.0f, 40.0f, 70.0f, 85.0f, 100.0f}};
        constexpr std::array<float, 5> correction{{
            0.0f, 0.10f, 0.0f, -0.28f, -0.18f}};
        if (peakReduction <= reductions.front()) return correction.front();
        if (peakReduction >= reductions.back()) return correction.back();
        size_t hi = 1;
        while (hi < reductions.size() && peakReduction > reductions[hi]) ++hi;
        const size_t lo = hi - 1;
        const float t = (peakReduction - reductions[lo])
            / (reductions[hi] - reductions[lo]);
        return correction[lo] + (correction[hi] - correction[lo]) * t;
    }

    static float optoLimitAttackLevelWeight(float peakDbfs) noexcept
    {
        constexpr std::array<float, 5> levels{{
            -40.0f, -24.0f, -12.0f, -3.0f, 0.0f}};
        constexpr std::array<float, 5> weights{{
            0.0f, 0.17f, 1.0f, 0.50f, 0.40f}};
        if (peakDbfs <= levels.front()) return weights.front();
        if (peakDbfs >= levels.back()) return weights.back();
        size_t hi = 1;
        while (hi < levels.size() && peakDbfs > levels[hi]) ++hi;
        const size_t lo = hi - 1;
        const float t = (peakDbfs - levels[lo])
            / (levels[hi] - levels[lo]);
        return weights[lo] + (weights[hi] - weights[lo]) * t;
    }

    static float optoOnsetCorrectionDb(float peakReduction,
                                        bool limit) noexcept
    {
        constexpr std::array<float, 9> reductions{{
            20.0f, 30.0f, 40.0f, 50.0f, 60.0f,
            70.0f, 80.0f, 90.0f, 100.0f}};
        constexpr std::array<float, 9> compress{{
            -0.25f, 0.30f, 0.20f, 0.17f, 0.03f,
            0.0f, 0.0f, 0.0f, 0.0f}};
        constexpr std::array<float, 9> limiting{{
            -0.25f, 0.30f, 0.23f, 0.24f, 0.15f,
            0.20f, 0.23f, 0.22f, 0.20f}};
        const auto& correction = limit ? limiting : compress;
        if (peakReduction <= reductions.front()) return correction.front();
        if (peakReduction >= reductions.back()) return correction.back();
        size_t hi = 1;
        while (hi < reductions.size() && peakReduction > reductions[hi]) ++hi;
        const size_t lo = hi - 1;
        const float t = (peakReduction - reductions[lo])
            / (reductions[hi] - reductions[lo]);
        return correction[lo] + (correction[hi] - correction[lo]) * t;
    }

    static float optoCurveCorrectionDb(float peakReduction,
                                        float reductionDb,
                                        bool limit) noexcept
    {
        if (limit) return 0.0f;
        constexpr std::array<float, 3> reductions{{40.0f, 70.0f, 100.0f}};
        constexpr std::array<float, 7> gains{{
            0.0f, 2.0f, 3.25f, 6.2f, 12.75f, 31.0f, 50.0f}};
        constexpr std::array<std::array<float, 7>, 3> correction{{
            {{0.0f, 0.0f, 0.05f, 0.169f, 0.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 0.0f, -0.045f, 0.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, -0.112f, -0.037f, -0.303f, -0.437f, -0.437f}}
        }};
        const auto gainMix = [&](size_t row) noexcept {
            if (reductionDb <= gains.front()) return correction[row].front();
            if (reductionDb >= gains.back()) return correction[row].back();
            size_t hi = 1;
            while (hi < gains.size() && reductionDb > gains[hi]) ++hi;
            const size_t lo = hi - 1;
            const float t = (reductionDb - gains[lo])
                / (gains[hi] - gains[lo]);
            return correction[row][lo]
                + (correction[row][hi] - correction[row][lo]) * t;
        };
        if (peakReduction <= reductions.front()) return gainMix(0);
        if (peakReduction >= reductions.back()) return gainMix(2);
        size_t hi = 1;
        while (hi < reductions.size() && peakReduction > reductions[hi]) ++hi;
        const size_t lo = hi - 1;
        const float t = (peakReduction - reductions[lo])
            / (reductions[hi] - reductions[lo]);
        return gainMix(lo) + (gainMix(hi) - gainMix(lo)) * t;
    }

    static float optoSettledPowerCorrectionDb(float frequencyHz,
                                               float inputPeakDbfs) noexcept
    {
        constexpr std::array<float, 12> frequencies{{
            50.0f, 82.41f, 110.0f, 200.0f, 400.0f, 1000.0f,
            2000.0f, 4000.0f, 6000.0f, 8000.0f, 12000.0f, 20000.0f}};
        constexpr std::array<float, 12> correction{{
            0.772f, 0.797f, 0.842f, 0.441f, 0.189f, 0.373f,
            0.401f, 0.191f, 0.108f, 0.147f, 0.545f, 0.370f}};
        const auto interpolate = [](float frequency, const auto& xs,
                                    const auto& ys) noexcept {
            if (frequency <= xs.front()) return ys.front();
            if (frequency >= xs.back()) return ys.back();
            size_t hi = 1;
            while (hi < xs.size() && frequency > xs[hi]) ++hi;
            const size_t lo = hi - 1;
            const float logLo = std::log(xs[lo]);
            const float t = (std::log(frequency) - logLo)
                / (std::log(xs[hi]) - logLo);
            return ys[lo] + (ys[hi] - ys[lo]) * t;
        };
        const float atMinus12 = interpolate(frequencyHz, frequencies, correction);
        constexpr std::array<float, 6> highFrequencies{{
            50.0f, 82.41f, 110.0f, 400.0f, 1000.0f, 4000.0f}};
        constexpr std::array<float, 6> highCorrection{{
            0.895f, 0.491f, 0.651f, 0.189f, 0.418f, 0.393f}};
        const float atMinus6 = interpolate(
            frequencyHz, highFrequencies, highCorrection);
        constexpr std::array<float, 6> maximumCorrection{{
            1.189f, 0.327f, 0.155f, 0.253f, 0.387f, 0.517f}};
        const float atZero = interpolate(
            frequencyHz, highFrequencies, maximumCorrection);
        if (inputPeakDbfs <= -24.0f) return 0.0f;
        if (inputPeakDbfs <= -12.0f)
            return atMinus12 * (inputPeakDbfs + 24.0f) / 12.0f;
        if (inputPeakDbfs <= -6.0f)
            return atMinus12 + (atMinus6 - atMinus12)
                * (inputPeakDbfs + 12.0f) / 6.0f;
        return atMinus6 + (atZero - atMinus6)
            * std::clamp((inputPeakDbfs + 6.0f) / 6.0f, 0.0f, 1.0f);
    }

    float processOpto(float input, int ch, float sidechain,
                      const MultiCompParameterState& p, bool external,
                      float optoDetector, bool useOptoDetector,
                      bool cellPreAdvanced = false, float pairGain = 1.0f) noexcept
    {
        OptoStageFeed feed;
        processOptoPreStage(input, ch, sidechain, p, external, optoDetector, useOptoDetector,
                            cellPreAdvanced, pairGain, feed);
        return optoStageSample(ch, feed);
    }

    // OPTO for one oversampled phase, everything ahead of the output stage:
    // the cell, the colour and harmonic terms and every gain correction. What
    // the stage and the gains behind it need goes into `feed`; optoStageSample()
    // (or optoStageBlock() over many phases) finishes the sample. The output
    // stage, its high-pass and the two aligned gain delays read nothing the
    // corrections compute from them, so they can run later.
    void processOptoPreStage(float input, int ch, float sidechain,
                             const MultiCompParameterState& p, bool external,
                             float optoDetector, bool useOptoDetector,
                             bool cellPreAdvanced, float pairGain, OptoStageFeed& feed) noexcept
    {
        auto& d = opto[ch];
        // Native opto leveler HF rise acts on the audio before harmonic generation:
        // 1 -> 4 kHz fundamentals at -6 dBFS / PR 0 move H2..H5 by
        // +.03/-.12/-.10/-.03 dB where a post-colour shelf predicts
        // +.12/+.30/+.51/+.72 (opto-shelf-placement-20260917). The detector
        // tap stays on the unshelved input.
        // What the cell works on: the input behind the first sub-audio
        // high-pass when that sits ahead of the cell (kOptoPreHighPassAhead).
        const float cellInput = kOptoPreHighPassAhead
            ? optoPreHighPass[ch].process(input) : input;
        const float audio = optoShelf[ch].process(cellInput);
        // `gain` is the physical cell gain applied to this sample. Colour is
        // added later as a residual with no fundamental term, so the static
        // law no longer has to anticipate or invert a colour-stage level shift.
        const float appliedGain = cellPreAdvanced ? pairGain : optoCell.gain(ch);
        // The cell's input element takes its term off the audio ahead of the
        // gain (zero while the element is off).
        const float compressed = (audio - optoCell.elementTerm(
            ch, cellInput, appliedGain,
            p.optoPeakReduction.load(std::memory_order_relaxed))) * appliedGain;
        const bool limit = p.optoLimit.load(std::memory_order_relaxed);
        // Side-chain selection, level estimator, static law and cell
        // populations are OptoCell (MultiCompOptoCell.hpp). It advances this
        // channel to the gain for the next sample and leaves this sample's
        // detector level and dynamic reduction for the colour blend below.
        if (!cellPreAdvanced) optoCell.process(ch, input, compressed, sidechain, external,
                         optoDetector, useOptoDetector, limit,
                         p.optoPeakReduction.load(std::memory_order_relaxed));
        const float dynamicGrDb = optoCell.dynamicGrDb(ch);
        const float gainKnob = p.optoGain.load(std::memory_order_relaxed);
        if (!(gainKnob == optoMakeupKnob))   // a pure function of the knob: evaluated when it moves (NaN: always)
        {
            optoMakeupKnob = gainKnob;
            optoMakeup = optoKnobToLinearGain(gainKnob);
        }
        const float makeup = optoMakeup;
        if constexpr (!kOptoToneSynthesisEnabled && !kOptoClassifierCorrectionsEnabled)
        {
            // The shipped state. With the tone synthesizer and the classifier
            // corrections off, the trackers below feed nothing the output
            // hears: the ripple and harmonic terms are zero, the settled and
            // meter corrections unity (meterCorrectionDb stays 0). They are not
            // run. `+ 0.0f` is the zero terms' sum, so a -0 compressed sample
            // leaves as +0 exactly as before.
            if (makeup == 0.0f)
            {
                if constexpr (!kOptoPreHighPassAhead)
                    optoPreHighPass[ch].process(0.0f);
                feed = OptoStageFeed{0.0f, 1.0f, 1.0f, true};
                return;
            }
            const float cellOutput = (compressed + 0.0f) * makeup;
            feed.input = kOptoPreHighPassAhead
                ? cellOutput : optoPreHighPass[ch].process(cellOutput);
            feed.muted = false;
            feed.settledGain = 1.0f;
            feed.meterGain = 1.0f;
            return;
        }
        const float toneInputAbs = std::abs(input);
        if (toneInputAbs >= d.toneInputPeak)
        {
            d.toneInputPeak = toneInputAbs;
            d.toneInputPeakHold = optoTonePeakHoldSamples;
        }
        else if (d.toneInputPeakHold > 0)
            --d.toneInputPeakHold;
        else
            d.toneInputPeak *= optoColourPeakRelease;
        d.tonePeriodSamples = std::min(d.tonePeriodSamples + 1, optoTonePeriodMaxSamples);
        const int toneInputSign = input >= 0.0f ? 1 : -1;
        d.cycleInputPeak = std::max(d.cycleInputPeak, toneInputAbs);
        if (d.toneInputSign < 0 && toneInputSign > 0)
        {
            if (d.toneMeasuredFrequencyHz >= 900.0f
                && d.toneMeasuredFrequencyHz <= 1100.0f
                && d.previousCycleInputPeak > 0.08f
                && d.cycleInputPeak > 0.01f
                && d.cycleInputPeak < 0.67f * d.previousCycleInputPeak)
            {
                const float dropDepthDb = gainToDecibels(
                    d.previousCycleInputPeak / std::max(
                        d.cycleInputPeak, 1.0e-12f));
                d.eventDropDepthScale = std::clamp(
                    (dropDepthDb - 6.0f) / 14.5f, 0.0f, 1.0f);
                d.eventDropCorrectionDb = 0.25f;
                d.eventDropSlowCorrectionDb = 0.55f;
            }
            d.previousCycleInputPeak = d.cycleInputPeak;
            d.cycleInputPeak = toneInputAbs;
            const int minimumPeriod = optoToneMinimumPeriod;
            const int maximumPeriod = optoToneMaximumPeriod;
            if (d.tonePeriodSamples >= minimumPeriod
                && d.tonePeriodSamples <= maximumPeriod
                && d.toneInputPeak > 1.0e-8f)
                d.toneMeasuredFrequencyHz = 1.0f
                    / (static_cast<float>(d.tonePeriodSamples) * optoInvSampleRate);
            d.tonePeriodSamples = 0;
        }
        d.toneInputSign = toneInputSign;
        d.eventDropCorrectionDb *= optoShortEventRelease;
        d.eventDropSlowCorrectionDb *= optoEventDropSlowRelease;
        // Mean cell gain over the ripple window, for the output-stage drive
        // index of the harmonic calibration below.
        d.gainMean += optoGainRippleMeanStep * (appliedGain - d.gainMean);
        const float audioPower = audio * audio;
        d.audioPowerMean += optoGainRippleMeanStep
            * (audioPower - d.audioPowerMean);
        d.broadbandSlowPower += optoBroadbandSlowStep
            * (d.audioPowerMean - d.broadbandSlowPower);
        d.broadbandPowerDeviation += optoBroadbandSlowStep
            * (std::abs(d.audioPowerMean - d.broadbandSlowPower)
               - d.broadbandPowerDeviation);
        // The harmonic calibration is periodic-tone specific.  A
        // second-order sinusoid predictor separates that condition from
        // ordinary programme material without looking ahead or allocating.
        // Estimate 1 - cos(w) from the second difference,
        // (2 x[n-1] - x[n] - x[n-2]) x[n-1] = 2 (1 - cos w) x[n-1]^2.
        // Forming cos(w) itself and taking acos() in float loses the tone:
        // at the oversampled rate a 50 Hz tone has 1 - cos w = 5e-6, within
        // ~45 float steps of 1.0, and the old estimate read 44.45 Hz.  The
        // curvature form keeps full relative precision; the means are double.
        const double previous1 = d.tonePrevious1;
        const double curvatureNumerator =
            (2.0 * previous1 - audio - d.tonePrevious2) * previous1;
        const double curvatureDenominator = 2.0 * previous1 * previous1;
        d.toneCurvatureNumeratorMean += optoGainRippleMeanStepDouble
            * (curvatureNumerator - d.toneCurvatureNumeratorMean);
        d.toneCurvatureDenominatorMean += optoGainRippleMeanStepDouble
            * (curvatureDenominator - d.toneCurvatureDenominatorMean);
        const double oneMinusCosine = std::clamp(
            d.toneCurvatureNumeratorMean
                / std::max(d.toneCurvatureDenominatorMean, 1.0e-24),
            1.0e-14, 1.9999999);
        const float recurrenceCosine = static_cast<float>(1.0 - oneMinusCosine);
        float harmonicFrequencyHz = d.toneMeasuredFrequencyHz;
        if (d.toneCurvatureDenominatorMean > 1.0e-10)
        {
            const float recurrenceFrequency = static_cast<float>(
                2.0 * std::asin(std::sqrt(0.5 * oneMinusCosine))
                / (6.283185307179586476925286766559 * optoInvSampleRate));
            if (recurrenceFrequency >= 20.0f && recurrenceFrequency <= 20000.0f)
                harmonicFrequencyHz = recurrenceFrequency;
        }
        // Split the cell gain into envelope and ripple at half the tone
        // frequency: the light's ripple sits at 2f and above, so envelope
        // motion (attack, recovery, programme) never reaches the ripple term.
        // A fixed split let a post-burst recovery through as "ripple".
        const double rippleLowStep = 1.0 - std::exp(
            -3.141592653589793238462643383279 * kOptoRippleSplitRatio
                * 2.0 * static_cast<double>(std::clamp(harmonicFrequencyHz, 20.0f, 4000.0f))
                * static_cast<double>(optoInvSampleRate));
        d.rippleGainLow1 += rippleLowStep
            * (static_cast<double>(appliedGain) - d.rippleGainLow1);
        d.rippleGainLow2 += rippleLowStep
            * (d.rippleGainLow1 - d.rippleGainLow2);
        // Keep the landed gain-law classifier on the detector's legacy tone
        // estimate.  The fractional recurrence estimate above belongs only to
        // the final harmonic synthesizer; feeding it back here changed the
        // independently measured output-memory law.
        const float toneRadians = 6.2831853071795864769f
            * optoCell.estimatedFrequencyHz(ch) * optoInvSampleRate;
        const float tonePrediction = 2.0f * std::cos(toneRadians)
            * d.tonePrevious1 - d.tonePrevious2;
        const float tonePredictionError = audio - tonePrediction;
        const float harmonicPrediction = 2.0f * recurrenceCosine
            * d.tonePrevious1 - d.tonePrevious2;
        const float harmonicPredictionError = audio - harmonicPrediction;
        d.tonePredictionErrorMean += optoGainRippleMeanStep
            * (tonePredictionError * tonePredictionError
               - d.tonePredictionErrorMean);
        d.harmonicPredictionErrorMean += optoGainRippleMeanStep
            * (harmonicPredictionError * harmonicPredictionError
               - d.harmonicPredictionErrorMean);
        d.tonePrevious2 = d.tonePrevious1;
        d.tonePrevious1 = audio;
        const bool periodicTone = d.tonePredictionErrorMean
            <= 1.0e-5f * std::max(d.audioPowerMean, 1.0e-12f);
        const bool harmonicPeriodicTone = d.harmonicPredictionErrorMean
            <= 1.0e-5f * std::max(d.audioPowerMean, 1.0e-12f);
        const bool sineLikeInput = d.toneInputPeak * d.toneInputPeak
            < 3.0f * std::max(d.audioPowerMean, 1.0e-12f);
        const bool calibrationTone = sineLikeInput && harmonicPeriodicTone;
        if (sineLikeInput && d.audioPowerMean > 1.0e-10f)
            d.sineSupportSamples = std::min(
                d.sineSupportSamples + 1, optoGainRippleWarmupSamples);
        else
            d.sineSupportSamples = 0;
        const bool stationaryBroadband = d.broadbandSlowPower > 1.0e-10f
            && d.broadbandPowerDeviation
                < 0.065f * d.broadbandSlowPower;
        if (!periodicTone && stationaryBroadband)
            d.broadbandSupportSamples = std::min(
                d.broadbandSupportSamples + 1, optoBroadbandWarmupSamples);
        else
            d.broadbandSupportSamples = 0;
        // The harmonic synthesizer arms only after 100 ms of continuous
        // periodic programme, so isolated events and ordinary programme stay
        // on the landed path.
        if (toneInputAbs < 1.0e-8f)
            d.harmonicSilenceSamples = std::min(
                d.harmonicSilenceSamples + 1, 8);
        else
            d.harmonicSilenceSamples = 0;
        if (d.harmonicSilenceSamples >= 8)
            d.rippleSupportSamples = 0;
        else if (d.audioPowerMean > 1.0e-10f && calibrationTone)
            d.rippleSupportSamples = std::min(
                d.rippleSupportSamples + 1, optoGainRippleWarmupSamples);
        else
            d.rippleSupportSamples = 0;

        if (makeup == 0.0f)
        {
            // Keep the stage's delay lines and the output-side high-passes
            // moving so an unmute is clean: the post high-pass follows the
            // stage's zero-input output (optoStageSample), and the pre
            // high-pass advances with zero when it sits behind the cell (ahead
            // of the cell it has already run on this sample's input above).
            if constexpr (!kOptoPreHighPassAhead)
                optoPreHighPass[ch].process(0.0f);
            feed = OptoStageFeed{0.0f, 1.0f, 1.0f, true};
            return;
        }
        // The native sub-audio high-passes bracket the cell and the output
        // nonlinearity (MultiCompOptoShelf.hpp: the first sits ahead of the
        // cell); the post high-pass removes the DC of the curve's asymmetry,
        // as in native.
        //
        // Cell ripple on the audio branch.  The cell's gain carries the
        // light's ripple at twice the tone frequency; its odd harmonics fall
        // at 6 dB/octave because the cell's fast response integrates that
        // ripple.  MC-2's fast loop is ~1.5x slower than the reference (AM
        // test, tau_eq 14 -> 2.5 ms against 22 -> 10 ms), so its ripple is
        // ~3.5 dB too shallow at every frequency (kOptoCellRippleScale).
        // Scale the ripple part of the gain on the audio path only: the
        // detector feedback, and so the dynamics, are unchanged.  The frequency law is the cell's own; the
        // tone-tracking low-pass only separates ripple from the envelope.
        const float rippleGain = appliedGain
            - static_cast<float>(d.rippleGainLow2);
        const float harmonicPeakReduction = p.optoPeakReduction.load(
            std::memory_order_relaxed);
        // Armed with the harmonic synthesizer (100 ms of periodic tone):
        // on transients the gain minus its low-pass is programme motion, not
        // ripple, and scaling it would change the dynamics.
        const bool harmonicsArmed = kOptoToneSynthesisEnabled
            && d.rippleSupportSamples >= optoGainRippleWarmupSamples;
        const float rippleScale = harmonicsArmed ? kOptoCellRippleScale : 0.0f;
        // Cell distortion, injected at the cell output before Gain and the
        // output stage (MultiCompOptoStructuralHarmonics.hpp).  A causal
        // phase oscillator is recovered from the cell's input (the shelved
        // audio, so the injected harmonics share the fundamental's path from
        // here on) and H2-H7 sine/cosine bases carry the harmonics.  The
        // 100 ms periodicity gate keeps this tone-derived branch out of
        // transients and ordinary programme material.
        const float toneSine = std::clamp(
            audio / std::max(d.toneInputPeak, 1.0e-12f), -2.0f, 2.0f);
        const float previousToneSine = d.toneInputPrevious;
        d.toneInputPrevious = toneSine;
        float cellHarmonics = 0.0f;
        if (harmonicsArmed)
        {   // The phase oscillator and its harmonic bases feed only the armed synthesizer.
            const float phaseStep = 6.2831853071795864769f
                * harmonicFrequencyHz * optoInvSampleRate;
            const float phaseStepSine = std::sin(phaseStep);
            float toneCosine = 0.0f;
            if (std::abs(phaseStepSine) > 1.0e-5f)
                toneCosine = (toneSine * std::cos(phaseStep)
                    - previousToneSine) / phaseStepSine;
            const float phaseMagnitude = std::sqrt(
                toneSine * toneSine + toneCosine * toneCosine);
            const float phaseScale = phaseMagnitude > 1.0e-5f
                ? 1.0f / phaseMagnitude : 0.0f;
            const float unitSine = toneSine * phaseScale;
            const float unitCosine = toneCosine * phaseScale;
            float harmonicSine = 0.0f;
            float harmonicCosine = 1.0f;
            std::array<float, 6> sineBases{};
            std::array<float, 6> cosineBases{};
            size_t harmonicIndex = 0;
            for (int harmonic = 1; harmonic <= 7; ++harmonic)
            {
                const float nextCosine = harmonicCosine * unitCosine
                    - harmonicSine * unitSine;
                const float nextSine = harmonicSine * unitCosine
                    + harmonicCosine * unitSine;
                harmonicCosine = nextCosine;
                harmonicSine = nextSine;
                if (harmonic >= 2)
                {
                    sineBases[harmonicIndex] = harmonicSine;
                    cosineBases[harmonicIndex] = harmonicCosine;
                    ++harmonicIndex;
                }
            }
            const float inputPeakDb = gainToDecibels(
                std::max(d.toneInputPeak, 1.0e-12f));
            const auto shape = optoStructuralHarmonics(
                harmonicFrequencyHz, inputPeakDb, harmonicPeakReduction, limit);
            const float cellFundamental = d.toneInputPeak * d.gainMean;
            const float nyquist = 0.5f
                / (optoInvSampleRate * static_cast<float>(osFactor));
            for (size_t i = 0; i < sineBases.size(); ++i)
                if (static_cast<float>(i + 2) * harmonicFrequencyHz < nyquist)
                    cellHarmonics += cellFundamental
                        * (shape[2 * i] * sineBases[i]
                           + shape[2 * i + 1] * cosineBases[i]);
        }
        // Remove the ripple term's projection onto the fundamental: its job is
        // the odd harmonics; the settled level stays the gain law's.
        float rippleTerm = rippleScale * rippleGain * audio;
        d.rippleFundamentalMean += optoGainRippleMeanStep
            * (rippleTerm * audio - d.rippleFundamentalMean);
        rippleTerm -= d.rippleFundamentalMean
            / std::max(d.audioPowerMean, 1.0e-12f) * audio;
        const float cellOutput = (compressed + rippleTerm + cellHarmonics) * makeup;
        const float out = kOptoPreHighPassAhead
            ? cellOutput : optoPreHighPass[ch].process(cellOutput);
        // Static, memoryless output stage (MultiCompOptoOutputStage.hpp),
        // fitted to the reference's PR 0 H1-H7 over every Gain's drive range.
        // It replaces the linear-to-0.7 segmented curve plus the G3 quadratic:
        // their abrupt knee switched harmonics on within 0.3 dB of drive.
        feed.input = out;
        feed.muted = false;
        // R5's added harmonic power moved the marginal 82.41 Hz / -24 dBFS /
        // PR 100 Compress settled cell from +0.481 to +0.503 dB.  Restore the
        // audio-path power (not the detector state) with a sub-0.03 dB LF-only
        // correction.  The fourth-order PR and frequency tapers make the
        // 1 kHz harmonic calibration effectively unchanged.
        const float prNormalised = std::clamp(
            p.optoPeakReduction.load(std::memory_order_relaxed) * 0.01f,
            0.0f, 1.0f);
        const float prSquared = prNormalised * prNormalised;
        const float settledFrequencyRatio = optoCell.estimatedFrequencyHz(ch) / 200.0f;
        const float settledFrequencySquared = settledFrequencyRatio * settledFrequencyRatio;
        const float settledCorrectionDb = limit || harmonicPeakReduction <= 10.0f
            ? 0.0f
            : 0.027f * prSquared * prSquared
                / (1.0f + settledFrequencySquared * settledFrequencySquared);
        feed.settledGain = kOptoClassifierCorrectionsEnabled
            ? 1.0f + 0.115129255f * settledCorrectionDb : 1.0f;

        const float frequencyHz = d.toneMeasuredFrequencyHz;
        constexpr float shortEventThreshold = 0.0316227766f; // -30 dBFS
        const bool shortEventLoud = std::abs(input) > shortEventThreshold;
        if (d.longEventReleaseActive)
            d.longEventReleaseAgeSamples = std::min(
                d.longEventReleaseAgeSamples + 1, optoLongEventAgeMaxSamples);
        if (!d.shortEventActive && !shortEventLoud)
            d.shortEventSilenceSamples = std::min(
                d.shortEventSilenceSamples + 1,
                optoShortEventRecurringMaxSamples);
        if (shortEventLoud)
        {
            if (!d.shortEventActive)
            {
                const bool recurringEvent = d.shortEventSilenceSamples
                        >= optoShortEventGapSamples
                    && d.shortEventSilenceSamples
                        < optoShortEventRecurringMaxSamples
                    && d.shortEventPreviousCorrectionDb > 0.0f;
                d.shortEventEnabled = !d.shortEventConsumed
                    && d.shortEventSilenceSamples >= optoShortEventArmSamples;
                d.shortEventConsumed = true;
                d.shortEventGrDb = recurringEvent
                    ? d.shortEventPreviousCorrectionDb : 0.0f;
                d.shortEventSilenceSamples = 0;
                d.shortEventActive = true;
                d.shortEventAgeSamples = 0;
                d.shortEventLastLoudAgeSamples = 0;
                d.shortEventPeak = 0.0f;
                d.shortEventQuietPeak = 0.0f;
                d.shortEventPowerMean = 0.0f;
            }
            d.shortEventQuietSamples = 0;
            d.shortEventPeak = std::max(d.shortEventPeak, std::abs(input));
        }
        else if (d.shortEventActive
                 && std::abs(input) < std::min(
                     0.01f, 0.05f * d.shortEventPeak))
        {
            ++d.shortEventQuietSamples;
            d.shortEventQuietPeak = std::max(
                d.shortEventQuietPeak, std::abs(input));
        }
        else if (d.shortEventActive)
        {
            d.shortEventQuietSamples = 0;
            d.shortEventQuietPeak = 0.0f;
        }
        if (d.shortEventActive)
        {
            d.shortEventAgeSamples = std::min(
                d.shortEventAgeSamples + 1, optoShortEventAgeMaxSamples);
            if (shortEventLoud)
                d.shortEventLastLoudAgeSamples = d.shortEventAgeSamples;
            d.shortEventPowerMean += (input * input - d.shortEventPowerMean)
                / static_cast<float>(std::max(d.shortEventAgeSamples, 1));
            const int durationAt48k = std::max(1, static_cast<int>(std::lround(
                static_cast<float>(d.shortEventLastLoudAgeSamples)
                    * 48000.0f * optoInvSampleRate)));
            if (d.shortEventQuietSamples >= optoShortEventQuietSamples)
            {
                const float quietCeiling = std::min(
                    0.01f, 0.05f * d.shortEventPeak);
                const bool quietProbe = d.shortEventQuietPeak > 1.0e-6f
                    && d.shortEventQuietPeak < quietCeiling;
                const float eventCorrectionDb = d.shortEventPeak > 0.08f
                    ? optoShortEventCorrectionDb(
                        durationAt48k,
                        gainToDecibels(std::max(d.shortEventPeak, 1.0e-12f)),
                        p.optoPeakReduction.load(std::memory_order_relaxed))
                    : 0.0f;
                const float eventCrest = d.shortEventPeak / std::sqrt(
                    std::max(d.shortEventPowerMean, 1.0e-12f));
                const bool sineLikeEvent = durationAt48k <= 20
                    || eventCrest < 1.8f;
                d.shortEventPreviousCorrectionDb = sineLikeEvent
                    ? eventCorrectionDb : 0.0f;
                if (durationAt48k >= 4800
                    && sineLikeEvent)
                {
                    d.longEventReleaseActive = true;
                    d.longEventReleaseAgeSamples = 0;
                }
                d.shortEventGrDb = d.shortEventEnabled && quietProbe
                    ? eventCorrectionDb : 0.0f;
                d.shortEventActive = false;
                d.shortEventAgeSamples = 0;
                d.shortEventQuietSamples = 0;
            }
        }
        else
            d.shortEventGrDb *= optoShortEventRelease;
        // Threshold gates below read the ripple-free (20 ms mean) cell
        // reduction.  Gating on the instantaneous reduction switched each
        // correction on and off twice per cycle wherever the cell's ripple
        // straddled the 0.01 dB threshold -- a square-wave gain that put odd
        // harmonics 23 dB above the clean level (50 Hz, -24 dBFS, PR .35).
        const float gateGrDb = -gainToDecibels(std::max(d.gainMean, 1.0e-12f));
        const float shortEventScale = limit ? 0.0f : std::clamp(
            p.optoPeakReduction.load(std::memory_order_relaxed) / 70.0f,
            0.0f, 1.5f);
        const float peakReduction = p.optoPeakReduction.load(
            std::memory_order_relaxed);
        const float broadbandPrWeight = peakReduction <= 70.0f
            ? peakReduction / 70.0f
            : std::max((100.0f - peakReduction) / 30.0f, 0.0f);
        const float broadbandCorrectionDb = !limit && !periodicTone
            && !sineLikeInput
            && stationaryBroadband
            && d.broadbandSupportSamples >= optoBroadbandWarmupSamples
            && gateGrDb > 0.01f
            ? broadbandPrWeight * optoBroadbandCorrectionDb(
                10.0f * std::log10(std::max(d.audioPowerMean, 1.0e-12f)))
            : 0.0f;
        const float detectorWeightingCorrectionDb = !limit && sineLikeInput
            && d.sineSupportSamples >= optoGainRippleWarmupSamples
            && peakReduction > 10.0f
            && gateGrDb > 0.01f
            ? std::clamp((peakReduction - 30.0f) / 40.0f, 0.0f, 1.0f)
                * optoDetectorWeightingCorrectionDb(frequencyHz)
            : 0.0f;
        // This measured release-memory correction does not participate in the
        // isolated charge gate.  Its paired music delta remains below the
        // preregistered clip-noise floor, while removing it fails the separate
        // sixteen-point output-memory gate (0.624 dB RMS versus 0.125 dB).
        const float longEventReleaseCorrectionDb = !limit
            && d.longEventReleaseActive
            ? shortEventScale * optoLongEventReleaseCorrectionDb(
                1000.0f * static_cast<float>(d.longEventReleaseAgeSamples)
                    * optoInvSampleRate)
            : 0.0f;
        const float programmeTargetDb = !limit && !periodicTone
            && !stationaryBroadband && !sineLikeInput
            && gateGrDb > 0.01f
            ? optoProgrammeCorrectionDb(peakReduction) : 0.0f;
        d.programmeCorrectionDb += optoProgrammeStep
            * (programmeTargetDb - d.programmeCorrectionDb);
        const float limitPrWeight = std::max(
            1.0f - std::abs(peakReduction - 60.0f) / 25.0f, 0.0f);
        const float limitAttackSeconds = d.shortEventActive
            ? static_cast<float>(d.shortEventAgeSamples) * optoInvSampleRate
            : 2.0f;
        const float limitAttackCorrectionDb = limit
            ? 1.20f * limitPrWeight
                * optoLimitAttackLevelWeight(gainToDecibels(
                    std::max(d.toneInputPeak, 1.0e-12f)))
                * std::exp(-limitAttackSeconds / 0.040f)
            : 0.0f;
        const float onsetCorrectionDb = sineLikeInput
            && d.sineSupportSamples >= optoGainRippleWarmupSamples
            && peakReduction > 10.0f
            ? optoOnsetCorrectionDb(peakReduction, limit)
                * (std::abs(peakReduction - 40.0f) < 0.5f
                    ? std::clamp((23.75f
                        - p.optoGain.load(std::memory_order_relaxed))
                            / 0.20f, 0.0f, 1.0f)
                    : 1.0f)
                * std::clamp((dynamicGrDb - 0.30f) / 0.60f, 0.0f, 1.0f)
                * std::clamp((3.0f - dynamicGrDb) * 0.5f, 0.0f, 1.0f)
            : 0.0f;
        const float curveCorrectionDb = sineLikeInput
            && d.sineSupportSamples >= optoGainRippleWarmupSamples
            && peakReduction >= 40.0f
            ? optoCurveCorrectionDb(peakReduction, dynamicGrDb, limit)
            : 0.0f;
        const float highCurveCorrectionDb = !limit && sineLikeInput
            && peakReduction >= 95.0f
            ? -optoSettledPowerCorrectionDb(frequencyHz,
                gainToDecibels(std::max(d.toneInputPeak, 1.0e-12f)))
                * std::clamp(
                (gainToDecibels(std::max(d.toneInputPeak, 1.0e-12f))
                    + 6.0f) / 0.30f, 0.0f, 1.0f)
                * std::clamp(
                (-5.0f - gainToDecibels(std::max(
                    d.toneInputPeak, 1.0e-12f))) / 0.30f, 0.0f, 1.0f)
                * std::clamp((dynamicGrDb - 20.0f) / 10.0f, 0.0f, 1.0f)
            : 0.0f;
        const float eventDropPrScale = peakReduction <= 40.0f
            ? 1.8f * std::clamp(peakReduction / 40.0f, 0.0f, 1.0f)
            : peakReduction < 70.0f
                ? 1.8f - 0.8f * (peakReduction - 40.0f) / 30.0f
                : 1.0f;
        const float eventDropCorrectionDb = !limit
            ? eventDropPrScale * d.eventDropDepthScale
                * d.eventDropCorrectionDb
            : 0.0f;
        const float eventDropSlowCorrectionDb = !limit
            ? 0.45f * std::clamp(
                (peakReduction - 70.0f) / 30.0f, 0.0f, 1.0f)
                * d.eventDropDepthScale * d.eventDropSlowCorrectionDb
            : 0.0f;
        const float lfSettledPowerCorrectionDb = !limit && sineLikeInput
            && d.sineSupportSamples >= optoGainRippleWarmupSamples
            ? optoSettledPowerCorrectionDb(frequencyHz,
                gainToDecibels(std::max(d.toneInputPeak, 1.0e-12f)))
                * std::clamp((peakReduction - 70.0f) / 30.0f, 0.0f, 1.0f)
            : 0.0f;
        const float inputPeakDbfs = gainToDecibels(std::max(
            d.toneInputPeak, 1.0e-12f));
        const float hfMidPrCorrectionDb = !limit && sineLikeInput
            ? -0.941f
                * std::clamp(1.0f - std::abs(peakReduction - 85.0f) / 15.0f,
                    0.0f, 1.0f)
                * std::clamp(1.0f - std::abs(inputPeakDbfs + 18.0f) / 6.0f,
                    0.0f, 1.0f)
                * std::clamp((frequencyHz - 10000.0f) / 10000.0f,
                    0.0f, 1.0f)
            : 0.0f;
        const float limitMidPrCorrectionDb = limit && sineLikeInput
            ? 0.648f
                * std::clamp(1.0f - std::abs(peakReduction - 85.0f) / 15.0f,
                    0.0f, 1.0f)
                * std::clamp(1.0f - std::abs(inputPeakDbfs + 18.0f) / 6.0f,
                    0.0f, 1.0f)
                * std::clamp(1.0f - std::abs(frequencyHz - 82.41f) / 35.0f,
                    0.0f, 1.0f)
            : 0.0f;
        d.meterCorrectionDb = !kOptoClassifierCorrectionsEnabled ? 0.0f
            : shortEventScale * d.shortEventGrDb
            + broadbandCorrectionDb + detectorWeightingCorrectionDb
            + longEventReleaseCorrectionDb + d.programmeCorrectionDb
            + limitAttackCorrectionDb + onsetCorrectionDb
            + curveCorrectionDb + highCurveCorrectionDb
            + eventDropCorrectionDb
            + eventDropSlowCorrectionDb + lfSettledPowerCorrectionDb
            + hfMidPrCorrectionDb + limitMidPrCorrectionDb;
        feed.meterGain = decibelsToGain(-d.meterCorrectionDb);
    }

    // The output stage behind processOptoPreStage(): the static curve at twice
    // the mode rate, its high-pass, and the settled and meter corrections
    // delayed to meet the stage's output.
    float optoStageSample(int ch, const OptoStageFeed& feed) noexcept
    {
        auto& d = opto[static_cast<size_t>(ch)];
        const int stageDelay = d.stage.delaySamples();
        if (feed.muted)
        {
            optoPostHighPass[ch].process(d.stage.process(0.0f, optocurve::OutputCurve{}));
            d.settledGainDelay.process(1.0f, stageDelay);
            d.meterGainDelay.process(1.0f, stageDelay);
            return 0.0f;
        }
        const float shapedOutput = optoPostHighPass[ch].process(
            d.stage.process(feed.input, optocurve::OutputCurve{}));
        const float settledOutput = shapedOutput * d.settledGainDelay.process(feed.settledGain, stageDelay);
        return settledOutput * d.meterGainDelay.process(feed.meterGain, stageDelay);
    }


    // fbTailKneeDb: the control value of a reduction, the inverse of the law
    // F(v) = v^2 / (4 knee) below v = 2 knee, v - knee above.
    static float fetTailControl(float reductionDb, float knee) noexcept
    {
        return fetTailControlT<lanes::Lane1>(reductionDb, knee);
    }
    template <class L>
    static typename L::F fetTailControlT(typename L::F reductionDb, float knee) noexcept
    {
        using lanes::vmax; using lanes::vsqrt;
        const auto r = vmax(reductionDb, L::f(0.0f));
        const auto below = r < L::f(knee);
        auto control = r + L::f(knee);
        if (MC2_UNLIKELY(takes<L>(below))) control = L::sel(below, L::f(2.0f) * vsqrt(L::f(knee) * r), control);
        return control;
    }

    // What a tail population with control value `tailControl` adds on top of
    // `rest` dB of reduction, F(Finv(rest) + tailControl) - rest; `restSlope`
    // is its slope in the rest and `lawSlope` the law's own at that point.
    static float fetTailApplied(float rest, float tailControl, float knee,
                                float& restSlope, float& lawSlope) noexcept
    {
        return fetTailAppliedT<lanes::Lane1>(rest, tailControl, knee, restSlope, lawSlope);
    }
    template <class L>
    static typename L::F fetTailAppliedT(typename L::F rest, typename L::F tailControl, float knee,
                                         typename L::F& restSlope, typename L::F& lawSlope) noexcept
    {
        using lanes::vmax; using lanes::vsqrt;
        const auto K = [](float x) noexcept { return L::f(x); };
        const auto r = vmax(rest, K(0.0f));
        const auto below = r < K(knee);
        const bool anyBelow = MC2_UNLIKELY(takes<L>(below));
        auto vr = r + K(knee);
        if (anyBelow) vr = L::sel(below, K(2.0f) * vsqrt(K(knee) * r), vr);
        const auto v = vr + tailControl;
        const auto belowLaw = v < K(2.0f * knee);
        lawSlope = K(1.0f);
        auto law = v - K(knee);
        if (MC2_UNLIKELY(takes<L>(belowLaw)))
        {
            lawSlope = L::sel(belowLaw, v * K(0.5f / knee), lawSlope);
            law = L::sel(belowLaw, v * v * K(0.25f / knee), law);
        }
        // d v / d reduction is sqrt(knee / reduction) below the knee, unbounded
        // at zero; the solver's coordinate (the square root of the charge)
        // keeps the product finite, and the floor only bounds a Newton slope.
        auto restInverse = K(1.0f);
        if (anyBelow) restInverse = L::sel(below, vsqrt(K(knee) / vmax(r, K(1.0e-4f))), restInverse);
        restSlope = lawSlope * restInverse - K(1.0f);
        return vmax(K(0.0f), law - r);
    }

    // fbTailKneeDb as used: 0 (off) unless it is a positive, finite number
    // below 60 dB (a lab value that is not lands on off).
    float fetTailKneeDb() const noexcept
    {
        const float knee = fetDynamics.fbTailKneeDb;
        return knee > 0.0f && knee < 60.0f ? knee : 0.0f;
    }

    // What the tail population adds to the reduction right now: its own
    // reading, or with fbTailKneeDb what that reading adds on the rest.
    float fetTailAppliedNow(const FETState& d) const noexcept
    {
        const float tail = static_cast<float>(d.slowTailGrDb);
        const float knee = fetTailKneeDb();
        float restSlope = 0.0f, lawSlope = 0.0f;
        return knee > 0.0f && tail > 0.0f
            ? fetTailApplied(d.fastGrDb + d.slowGrDb,
                             fetTailControl(tail, knee), knee,
                             restSlope, lawSlope)
            : tail;
    }

    // advanceFetFeedback's terms that depend only on Ratio, Threshold, Attack, Release, the rate and the
    // dynamics constants. fetFeedbackKnobTerms() evaluates them with advanceFetFeedback's own expressions
    // and again only when one of those inputs changes (bit for bit), so every value is the one the step
    // computed for itself. Both channels share them.
    struct FetFeedbackTerms
    {
        bool valid = false;
        int ratioIndex = 0;
        float thresholdControlDb = 0.0f, attackPosition = 0.0f, releasePosition = 0.0f, sr = 0.0f;
        float releaseDepthDb = 0.0f;
        float releaseScale = 0.0f;      // fbReleaseSeconds x the Release knob's factor x the ratio's scale
        // fbTipReferenceShare: releaseScale and releaseDepthDb at the nominal Release position (0 while it is off)
        float releaseScaleNominal = 0.0f, releaseDepthDbNominal = 0.0f;
        float fullCharge = 0.0f, lagStep = 1.0f;
        float kneeKnob = 0.0f;          // fbReleaseKneeShare x the knee release's knob factor (4:1)
        double kneeDeepDb = 0.0;        // past this reduction kneeKnob exp(-G / g0) < 2^-25 (+ 0.01 dB margin)
        float tipDenominator = 1.0f;    // fullCharge, or fullCharge (1 - riseOnly / 2)
        float ratioCoordinate = 0.0f, tailShare = 0.0f;
        float attackLoadDb = 0.0f, maximumReductionDb = 0.0f, turnOnBiasDb = 0.0f;
        double tailStepCharging = 0.0, tailStepReleasing = 0.0;
        float tailInputDecay = 0.0f;    // fbTailInputLimit's peak follower, per sample
        float slowReleaseKnob = 0.0f;   // the slow population's release knob factor
        float slowChargeStep = 0.0f;
    };
    mutable FetFeedbackTerms fetFeedbackTerms;   // advanceFetFeedback's knob terms (fetFeedbackKnobTerms)

    static bool sameBits(float a, float b) noexcept
    {
        std::uint32_t x, y;
        std::memcpy(&x, &a, sizeof x);
        std::memcpy(&y, &b, sizeof y);
        return x == y;
    }

    const FetFeedbackTerms& fetFeedbackKnobTerms(int ratioIndex, float thresholdControlDb, float attackPosition,
                                                 float releasePosition, float sr) const noexcept
    {
        const auto& t = fetFeedbackTerms;
        if (t.valid && t.ratioIndex == ratioIndex && sameBits(t.thresholdControlDb, thresholdControlDb)
            && sameBits(t.attackPosition, attackPosition) && sameBits(t.releasePosition, releasePosition)
            && sameBits(t.sr, sr))
            return t;
        return refreshFetFeedbackKnobTerms(ratioIndex, thresholdControlDb, attackPosition, releasePosition, sr);
    }
    // The Release knob's nominal position ('5'): the voices' static laws were read there, and the taper and the tip
    // reference (fbReleaseTaperEnd, fbTipReferenceShare) refer to it.
    static constexpr float fetNominalRelease = 0.66595459f;

    MC2_NOINLINE const FetFeedbackTerms& refreshFetFeedbackKnobTerms(int ratioIndex, float thresholdControlDb,
                                                                      float attackPosition, float releasePosition,
                                                                      float sr) const noexcept
    {
        auto& t = fetFeedbackTerms;
        t = fetFeedbackKnobTermsAt(ratioIndex, thresholdControlDb, attackPosition, releasePosition, sr);
        // fbTipReferenceShare: the release laws at the nominal position too, by the same code, so that the two
        // leaks agree bit for bit there.
        if (fetDynamics.fbTipReferenceShare > 0.0f)
        {
            const FetFeedbackTerms nominal
                = fetFeedbackKnobTermsAt(ratioIndex, thresholdControlDb, attackPosition, fetNominalRelease, sr);
            t.releaseScaleNominal = nominal.releaseScale;
            t.releaseDepthDbNominal = nominal.releaseDepthDb;
        }
        return t;
    }
    FetFeedbackTerms fetFeedbackKnobTermsAt(int ratioIndex, float thresholdControlDb, float attackPosition,
                                            float releasePosition, float sr) const noexcept
    {
        FetFeedbackTerms t;
        t.valid = true;
        t.ratioIndex = ratioIndex;
        t.thresholdControlDb = thresholdControlDb;
        t.attackPosition = attackPosition;
        t.releasePosition = releasePosition;
        t.sr = sr;
        constexpr float decade = 2.30258509f;
        const float attackOffset = attackPosition - 0.5f;
        // The Release pot's dead travel at the fast end (fbReleaseTaperEnd,
        // fbReleaseTaperCurve): above the nominal position the remaining
        // travel is compressed onto the span up to the taper's end, so the
        // knob laws below keep their values at and below the nominal position
        // and at 1.
        constexpr float nominalRelease = fetNominalRelease;
        const float releaseTravel = fetDynamics.fbReleaseTaperEnd < 1.0f
                && fetDynamics.fbReleaseTaperCurve > 0.0f
                && !(fetDynamics.fbReleasePotFixed > 0.0f)
                && releasePosition > nominalRelease
            ? nominalRelease + (1.0f - nominalRelease) * std::pow(
                std::min(1.0f, (releasePosition - nominalRelease)
                    / std::max(fetDynamics.fbReleaseTaperEnd - nominalRelease,
                               1.0e-3f)),
                fetDynamics.fbReleaseTaperCurve)
            : releasePosition;
        const float releaseOffset = releaseTravel - 0.5f;
        float attackSeconds = fetDynamics.fbAttackSeconds
            * std::exp(decade * (fetDynamics.fbAttackCurve * attackOffset
                                    * attackOffset
                                 - fetDynamics.fbAttackSlope * attackOffset))
            * std::pow(fetRatioAttackScale(*fetVoice, ratioIndex),
                       fetDynamics.fbRatioExponent);
        // fbSloAttackScale (0 = none): the Attack knob's SLO detent at its counter-clockwise stop (FET BLUE).
        if (fetDynamics.fbSloAttackScale > 0.0f && attackPosition <= 0.0f)
            attackSeconds *= fetDynamics.fbSloAttackScale;
        const FetVoice& voice = *fetVoice;
        const float ratioSlope
            = voice.reductionSlopes[static_cast<size_t>(std::clamp(ratioIndex, 0, voice.ratioCount - 1))];
        // The depth and tail laws read the ratio as k / k at the voice's 4:1, k = slope / (1 - slope).
        const float unitSlope = voice.reductionSlopes[static_cast<size_t>(voice.unitRatio)];
        // The Release knob's factor, in decades relative to its value at 0.5:
        // the linear pot with its fixed resistance (fbReleasePotFixed), or the
        // quadratic it replaces.
        float releaseKnobDecades = fetDynamics.fbReleaseCurve
            * releaseOffset * releaseOffset
            - fetDynamics.fbReleaseSlope * releaseOffset;
        float releaseFastEnd = std::max(0.0f, 2.0f * releaseOffset);
        // log10 of the pot's resistance over its value at the fast end, i.e.
        // log10(1 / k) for the release rate k relative to Release 0.85-1.
        float releaseKnobFromFast = 0.0f;
        if (fetDynamics.fbReleasePotFixed > 0.0f)
        {
            const float potEnd = fetDynamics.fbReleasePotEnd;
            // Floors first, so a non-finite lab value lands on them.
            const float potSpan = std::max(
                1.0e-3f, potEnd - fetDynamics.fbReleasePotStart);
            const float travel = std::min(1.0f, std::max(
                0.0f, (potEnd - releasePosition) / potSpan));
            const float travelAtHalf = std::min(1.0f, std::max(
                0.0f, (potEnd - 0.5f) / potSpan));
            releaseKnobDecades = std::log10(
                (fetDynamics.fbReleasePotFixed + travel)
                / (fetDynamics.fbReleasePotFixed + travelAtHalf));
            releaseKnobFromFast = std::log10(
                (fetDynamics.fbReleasePotFixed + travel) / fetDynamics.fbReleasePotFixed);
            // Past the pot's end nothing moves, so the depth law's fast end
            // follows the travel as well: 0 at Release 0.5, 1 at the end.
            releaseFastEnd = std::max(
                0.0f, 1.0f - travel / std::max(travelAtHalf, 1.0e-3f));
        }
        t.releaseDepthDb = (fetDynamics.fbReleaseDepthDb
                + fetDynamics.fbReleaseDepthFastDb
                    * releaseFastEnd * releaseFastEnd)
            * std::pow(ratioSlope / (1.0f - ratioSlope)
                           * (1.0f - unitSlope) / unitSlope,
                       fetDynamics.fbReleaseDepthRatioExponent);
        t.releaseScale = fetDynamics.fbReleaseSeconds
            * std::exp(decade * releaseKnobDecades)
            * fetRatioReleaseScale(voice, ratioIndex);
        t.fullCharge = 1.0f / (std::max(attackSeconds, 1.0e-7f) * sr);
        t.lagStep = fetDynamics.fbAttackLagRatio > 0.0f
            ? 1.0f - std::exp(-1.0f / (fetDynamics.fbAttackLagRatio
                * std::exp(-decade * fetDynamics.fbAttackLagSlope * attackOffset)
                * std::max(attackSeconds, 1.0e-7f) * sr))
            : 1.0f;
        const float riseOnly = std::clamp(fetDynamics.fbChargeRiseOnly, 0.0f, 1.0f);
        t.tipDenominator = riseOnly > 0.0f ? t.fullCharge * (1.0f - 0.5f * riseOnly) : t.fullCharge;
        t.kneeKnob = fetDynamics.fbReleaseKneeShare
            * std::exp(decade * fetDynamics.fbReleaseKneeKnobExponent * releaseKnobFromFast);
        t.kneeDeepDb = t.kneeKnob > 0.0f
            ? static_cast<double>(std::max(fetDynamics.fbReleaseKneeDb, 1.0e-3f))
                * (std::log(static_cast<double>(t.kneeKnob)) + 25.0 * 0.6931471805599453) + 0.01
            : 0.0;
        t.ratioCoordinate = ratioSlope / (1.0f - ratioSlope)
            * (1.0f - unitSlope) / unitSlope;
        t.tailShare = std::max(fetDynamics.fbTailShare, 0.0f)
            * (fetDynamics.fbTailRatioExponent != 0.0f
                ? std::pow(t.ratioCoordinate, fetDynamics.fbTailRatioExponent)
                : 1.0f);
        t.attackLoadDb = fetDynamics.fbAttackLoadDb
            * std::max(0.0f, 2.0f * attackOffset);
        // fbSloLoadDb: the SLO detent loads the side chain as well, a threshold raise there (FET BLUE).
        if (fetDynamics.fbSloAttackScale > 0.0f && attackPosition <= 0.0f)
            t.attackLoadDb += fetDynamics.fbSloLoadDb;
        t.maximumReductionDb = fetStaticMaximumReductionDb(
            voice, ratioIndex, thresholdControlDb);
        t.turnOnBiasDb = 1.0e-3f + std::max(0.0f,
            fetDynamics.fbTurnOnBiasDb) * attackPosition * attackPosition;
        const double tailSecondsCharging = std::max(1.0e-4f, fetDynamics.fbTailChargeSeconds);
        const double tailSecondsReleasing = std::max(1.0e-4f, fetDynamics.fbTailReleaseSeconds);
        t.tailStepCharging = std::exp(-1.0 / (tailSecondsCharging * sr));
        t.tailStepReleasing = std::exp(-1.0 / (tailSecondsReleasing * sr));
        t.tailInputDecay = std::exp(-1.0f / (std::max(fetDynamics.fbTailInputReleaseSeconds, 1.0e-4f) * sr));
        t.slowReleaseKnob = std::exp(decade * releaseKnobDecades
            * (fetDynamics.fbSlowReleaseKnobExponent - 1.0f));
        t.slowChargeStep = std::exp(-1.0f / (std::max(fetDynamics.fbSlowChargeSeconds, 1.0e-4f) * sr));
        return t;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Candidate C6 over a lane policy (MultiCompLanes.hpp; CPU-parity campaign 2026-10-20). processFET's vintage
    // arm with the loop on (fbOn, ratios 4-20) is written once, as fetC6Step<L>: Lane1 steps one channel, Lane2
    // the two channels of a stereo pair at once (arm64). Every lane performs exactly the single-channel
    // arithmetic in the same order, with the same library calls per lane; a branch the scalar arm takes per
    // channel is a per-lane select, and work the scalar arm skips on a channel (an expansion's exact
    // re-evaluation, a solver iteration) neither runs for that lane nor changes its state. A pair step therefore
    // leaves each channel, and its output, exactly as a single step would.

    template <class L> static float laneF(typename L::F x, int k) noexcept { return L::laneF(x, k); }
    template <class L> static double laneD(typename L::D x, int k) noexcept { return L::lane(x, k); }
    template <class L> static bool laneM(typename L::M x, int k) noexcept { return L::laneM(x, k); }
    template <class L> static typename L::F lanesF(const float* v) noexcept
    {
        if constexpr (L::kLanes == 1) return L::f(v[0]); else return L::f(v[0], v[1]);
    }
    template <class L> static typename L::D lanesD(const double* v) noexcept
    {
        if constexpr (L::kLanes == 1) return L::d(v[0]); else return L::d(v[0], v[1]);
    }
    template <class L> static typename L::M lanesM(const bool* v) noexcept
    {
        if constexpr (L::kLanes == 1) return L::m(v[0]); else return L::m(v[0], v[1]);
    }
    // fn(lane) on the lanes where `on` is set, `keep` on the others: a branch the scalar arm takes per channel.
    template <class L, class Fn> static typename L::F whereF(typename L::M on, typename L::F keep, Fn&& fn) noexcept
    {
        float v[L::kLanes];
        for (int k = 0; k < L::kLanes; ++k) v[k] = laneM<L>(on, k) ? fn(k) : laneF<L>(keep, k);
        return lanesF<L>(v);
    }
    template <class L, class Fn> static typename L::D whereD(typename L::M on, typename L::D keep, Fn&& fn) noexcept
    {
        double v[L::kLanes];
        for (int k = 0; k < L::kLanes; ++k) v[k] = laneM<L>(on, k) ? fn(k) : laneD<L>(keep, k);
        return lanesD<L>(v);
    }

    // fetFeedbackK2 / K3 / LowFrequencyK2 at each lane's reduction: fetFeedbackColourRow's row and
    // fetFeedbackColourAt's arithmetic, lane by lane, on the packed rows (a NaN lane reads row 0 at fraction 0,
    // the last values, as At returns them past the last row).
    template <class L>
    static void fetFeedbackColourT(typename L::F grDb, typename L::F& k2, typename L::F& k3,
                                   typename L::F& lowK2) noexcept
    {
        using F = typename L::F;
        const F zero = L::f(0.0f), top = L::f(40.0f);
        const F clamped = L::sel(grDb < zero, zero, L::sel(top < grDb, top, grDb));   // std::clamp
        const auto number = clamped == clamped;
        const F g = L::sel(number, clamped, zero);
        const FetColourPacked* rows[L::kLanes];
        for (int lane = 0; lane < L::kLanes; ++lane)
            rows[lane] = &fetColourPackedRow(laneM<L>(number, lane) ? fetColourRowIndex(laneF<L>(g, lane)) : 0);
        F v[8];
        lanes::vtranspose4(&rows[0]->v[0], &rows[L::kLanes - 1]->v[0], v);
        lanes::vtranspose4(&rows[0]->v[4], &rows[L::kLanes - 1]->v[4], v + 4);
        const F fraction = (g - v[0]) / v[1];
        k2 = v[2] + fraction * v[3];
        k3 = v[4] + fraction * v[5];
        lowK2 = v[6] + fraction * v[7];
    }

    // The expansions' anchors (FETState's FetExpansion, FetExpansionD, FetPowExpansion) in lanes.
    template <class L> struct FetExpansionLanes { typename L::F at, value, aux; };
    template <class L> struct FetExpansionDLanes { typename L::D at, value; };
    template <class L> struct FetPowExpansionLanes { typename L::D at, inverse, exponent, value; };

    // exp(z) for the small arguments of the one-pole steps: the cubic series where |z| < 1e-2 (its error under
    // z^4 / 24 < 4.2e-10 relative, far below a float's resolution), the library elsewhere.
    template <class L>
    static typename L::F smallExpT(typename L::F z) noexcept
    {
        using lanes::vabs;
        auto y = L::f(1.0f) + z * (L::f(1.0f) + z * (L::f(0.5f) + z * L::f(0.166666667f)));
        const auto library = !(vabs(z) < L::f(1.0e-2f));
        if (MC2_UNLIKELY(L::any(library)))
            y = whereF<L>(library, y, [&](int k) noexcept { return std::exp(laneF<L>(z, k)); });
        return y;
    }

    // Tolerance-refreshed expansions (as the OPTO cell's, 2026-10-20): the function is evaluated exactly at an
    // anchor and followed on its Taylor series until the argument moves past the tolerance, then evaluated
    // exactly again. Per channel state, so a linked pair still equals two single channels. Errors at the
    // tolerance: exp and dB-to-gain 4.2e-10, cube root 6.6e-9 (relative); tanh 1.7e-9, cosine 4.2e-10 and
    // gain-to-dB 2.2e-8 dB (absolute). NaN refreshes and passes through. `active`: the lanes this evaluation is
    // for; the others keep their anchors (their values are not used).
    template <class L>
    static typename L::F expandedTanhT(FetExpansionLanes<L>& e, typename L::F x, typename L::M active) noexcept
    {
        using lanes::vabs;
        auto h = x - e.at;
        const auto refresh = active && !(vabs(h) <= L::f(0.01f));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, x, e.at);
            e.value = whereF<L>(refresh, e.value, [&](int k) noexcept { return std::tanh(laneF<L>(x, k)); });
            h = L::sel(refresh, L::f(0.0f), h);
        }
        const auto t = e.value;
        return t + (L::f(1.0f) - t * t) * h * (L::f(1.0f) + h * (-t + h * (t * t - L::f(0.333333333f))));
    }
    template <class L>
    static typename L::F expandedExpT(FetExpansionLanes<L>& e, typename L::F x, typename L::M active) noexcept
    {
        using lanes::vabs;
        auto h = x - e.at;
        const auto refresh = active && !(vabs(h) <= L::f(0.01f));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, x, e.at);
            e.value = whereF<L>(refresh, e.value, [&](int k) noexcept { return std::exp(laneF<L>(x, k)); });
            h = L::sel(refresh, L::f(0.0f), h);
        }
        return e.value * (L::f(1.0f) + h * (L::f(1.0f) + h * (L::f(0.5f) + h * L::f(0.166666667f))));
    }
    template <class L>
    static typename L::D expandedExpDT(FetExpansionDLanes<L>& e, typename L::D x, typename L::M active) noexcept
    {
        using lanes::vabs;
        auto h = x - e.at;
        const auto refresh = active && !(vabs(h) <= L::d(0.01));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, x, e.at);
            e.value = whereD<L>(refresh, e.value, [&](int k) noexcept { return std::exp(laneD<L>(x, k)); });
            h = L::sel(refresh, L::d(0.0), h);
        }
        return e.value * (L::d(1.0) + h * (L::d(1.0) + h * (L::d(0.5) + h * L::d(1.0 / 6.0))));
    }
    // x > 0 (relative tolerance 2%); zero, negative and NaN arguments are evaluated exactly.
    template <class L>
    static typename L::F expandedCbrtT(FetExpansionLanes<L>& e, typename L::F x, typename L::M active) noexcept
    {
        using lanes::vabs;
        const auto exact = !(x > L::f(1.0e-30f));
        auto u = (x - e.at) * e.aux;
        const auto refresh = active && !exact && !(vabs(u) <= L::f(0.02f));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, x, e.at);
            e.aux = L::sel(refresh, L::f(1.0f) / x, e.aux);
            e.value = whereF<L>(refresh, e.value, [&](int k) noexcept { return std::cbrt(laneF<L>(x, k)); });
            u = L::sel(refresh, L::f(0.0f), u);
        }
        auto y = e.value * (L::f(1.0f) + u * (L::f(0.333333333f) + u * (L::f(-0.111111111f) + u * L::f(0.0617283951f))));
        if (MC2_UNLIKELY(L::any(exact)))
            y = whereF<L>(exact, y, [&](int k) noexcept { return std::cbrt(laneF<L>(x, k)); });
        return y;
    }
    // cos and sin of one argument through one function for every lane: the compiler may turn the pair into one
    // sincos call, whose sine can differ from sinf's in the last place, so a single channel and each lane of a
    // pair must take the same route.
    MC2_NOINLINE static void cosAndSin(float x, float& cosine, float& sine) noexcept
    {
        cosine = std::cos(x);
        sine = std::sin(x);
    }
    template <class L>
    static typename L::F expandedCosT(FetExpansionLanes<L>& e, typename L::F x, typename L::M active) noexcept
    {
        using lanes::vabs;
        auto h = x - e.at;
        const auto refresh = active && !(vabs(h) <= L::f(0.01f));
        if (L::any(refresh))
        {
            float cosine[L::kLanes], sine[L::kLanes];
            for (int k = 0; k < L::kLanes; ++k)
            {
                cosine[k] = laneF<L>(e.value, k);
                sine[k] = laneF<L>(e.aux, k);
                if (laneM<L>(refresh, k)) cosAndSin(laneF<L>(x, k), cosine[k], sine[k]);
            }
            e.at = L::sel(refresh, x, e.at);
            e.value = lanesF<L>(cosine);
            e.aux = lanesF<L>(sine);
            h = L::sel(refresh, L::f(0.0f), h);
        }
        return e.value * (L::f(1.0f) - L::f(0.5f) * h * h) - e.aux * h * (L::f(1.0f) - L::f(0.166666667f) * h * h);
    }
    // decibelsToGain (tolerance 0.01 neper, 0.087 dB).
    template <class L>
    static typename L::F expandedDecibelsToGainT(FetExpansionLanes<L>& e, typename L::F db,
                                                 typename L::M active) noexcept
    {
        using lanes::vabs;
        auto h = (db - e.at) * L::f(0.115129255f);
        const auto refresh = active && !(vabs(h) <= L::f(0.01f));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, db, e.at);
            e.value = whereF<L>(refresh, e.value, [&](int k) noexcept { return decibelsToGain(laneF<L>(db, k)); });
            h = L::sel(refresh, L::f(0.0f), h);
        }
        return e.value * (L::f(1.0f) + h * (L::f(1.0f) + h * (L::f(0.5f) + h * L::f(0.166666667f))));
    }
    // gainToDecibels for gains above its 1e-9 floor (relative tolerance 1%); at or below it, exact.
    template <class L>
    static typename L::F expandedGainToDecibelsT(FetExpansionLanes<L>& e, typename L::F gain,
                                                 typename L::M active) noexcept
    {
        using lanes::vabs;
        const auto exact = !(gain > L::f(1.0e-9f));
        auto u = (gain - e.at) * e.aux;
        const auto refresh = active && !exact && !(vabs(u) <= L::f(0.01f));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, gain, e.at);
            e.aux = L::sel(refresh, L::f(1.0f) / gain, e.aux);
            e.value = whereF<L>(refresh, e.value, [&](int k) noexcept { return gainToDecibels(laneF<L>(gain, k)); });
            u = L::sel(refresh, L::f(0.0f), u);
        }
        auto y = e.value + L::f(8.68588964f) * u * (L::f(1.0f) - u * (L::f(0.5f) - u * L::f(0.333333333f)));
        if (MC2_UNLIKELY(L::any(exact)))
            y = whereF<L>(exact, y, [&](int k) noexcept { return gainToDecibels(laneF<L>(gain, k)); });
        return y;
    }

    // x^y (x >= 0, y > 0) for the 4:1 depth term, whose x (the populations' reduction over its scale) moves
    // over seconds: the third-order expansion in u = x / x_c - 1 about the last exact evaluation x_c, redone
    // once |u| passes 2% or y changes. Relative error <= |y (y - 1)(y - 2)(y - 3)| / 24 u^4: 8.1e-7 at the
    // shipped y (5.01). x = 0 gives 0 as std::pow does; NaN passes through.
    template <class L>
    static typename L::D expandedDepthPowT(FetPowExpansionLanes<L>& e, typename L::D x, double y,
                                           typename L::M active) noexcept
    {
        using lanes::vabs;
        const auto exact = !(x > L::d(1.0e-30));
        auto u = (x - e.at) * e.inverse;
        const auto refresh = active && !exact && (!(vabs(u) <= L::d(0.02)) || !(L::d(y) == e.exponent));
        if (L::any(refresh))
        {
            e.at = L::sel(refresh, x, e.at);
            e.inverse = L::sel(refresh, L::d(1.0) / x, e.inverse);
            e.exponent = L::sel(refresh, L::d(y), e.exponent);
            e.value = whereD<L>(refresh, e.value, [&](int k) noexcept { return std::pow(laneD<L>(x, k), y); });
            u = L::sel(refresh, L::d(0.0), u);
        }
        const auto expanded = e.value * (L::d(1.0) + u * (L::d(y) + u * (L::d(0.5 * y * (y - 1.0))
            + u * L::d(y * (y - 1.0) * (y - 2.0) / 6.0))));
        return L::sel(exact, L::sel(x == x, L::d(0.0), x), expanded);
    }

    // The per-channel state the C6 arm reads and writes, in lanes. Loaded from FETState before a run of
    // samples and stored after; the arm's constants (intermediateGrDb, recoveryGain, kneeBaseReductionDb) are
    // written on store.
    template <class L>
    struct FetC6Lanes
    {
        typename L::F envelope, fastGrDb, feedbackLag, riseOnlyPreviousMagnitude, tilt, tailInputPeak;
        typename L::F dc, prevSq, colourHighPassLow, colourHighPassLower, colourPeak, colourLow, colourLower;
        typename L::F colourLowPeak, colourLowDc, colourLowPrevSq, colourLowHarmonicDc, kneeCorrectionDb, kneeGain;
        typename L::F subBassHpState;
        typename L::D slowGrDb, slowTailGrDb, lfAheadZ1, lfAheadZ2, feedbackHpZ1, feedbackHpZ2, partnerHpZ1;
        typename L::D partnerHpZ2, lfHpZ1, lfHpZ2;
        typename L::D scFilterZ1, scFilterZ2, scFilterZ3, scFilterPartnerZ1, scFilterPartnerZ2, scFilterPartnerZ3;
        FetExpansionLanes<L> slowShape, tipCbrt, tipCos, overshootExp, envelopeGain, colourGrDb;
        FetExpansionDLanes<L> kneeExp;
        FetPowExpansionLanes<L> depthPow;
        // The carried evaluation (FETState::FetCarry) and its knob key, per lane.
        typename L::M carryValid, carryKnee;
        typename L::F carryU, carryCharge, carrySlow, carryTail, carryQuotient, carryLevelSlope, carrySlowSlope;
        typename L::F carryWeight, carryWeightSlope;
        FetCarryKey carryKey[L::kLanes];
        // The knee gain's memo (FETState::kneeGainKey / Valid / Value), per lane.
        std::uint32_t kneeGainKey[L::kLanes];
        bool kneeGainValid[L::kLanes];
        float kneeGainValue[L::kLanes];
    };
    template <class L>
    static void loadC6(FetC6Lanes<L>& s, FETState* const* d) noexcept
    {
        constexpr int n = L::kLanes;
        const auto f = [d](float FETState::* m) noexcept {
            float v[L::kLanes];
            for (int k = 0; k < L::kLanes; ++k) v[k] = d[k]->*m;
            return lanesF<L>(v);
        };
        const auto g = [d](double FETState::* m) noexcept {
            double v[L::kLanes];
            for (int k = 0; k < L::kLanes; ++k) v[k] = d[k]->*m;
            return lanesD<L>(v);
        };
        const auto x = [d](FetExpansion FETState::* m) noexcept {
            float at[L::kLanes], value[L::kLanes], aux[L::kLanes];
            for (int k = 0; k < L::kLanes; ++k)
            {
                at[k] = (d[k]->*m).at;
                value[k] = (d[k]->*m).value;
                aux[k] = (d[k]->*m).aux;
            }
            return FetExpansionLanes<L>{lanesF<L>(at), lanesF<L>(value), lanesF<L>(aux)};
        };
        s.envelope = f(&FETState::envelope); s.fastGrDb = f(&FETState::fastGrDb);
        s.feedbackLag = f(&FETState::feedbackLag); s.riseOnlyPreviousMagnitude = f(&FETState::riseOnlyPreviousMagnitude);
        s.tilt = f(&FETState::tilt); s.dc = f(&FETState::dc); s.prevSq = f(&FETState::prevSq);
        s.tailInputPeak = f(&FETState::tailInputPeak);
        s.colourHighPassLow = f(&FETState::colourHighPassLow); s.colourHighPassLower = f(&FETState::colourHighPassLower);
        s.colourPeak = f(&FETState::colourPeak); s.colourLow = f(&FETState::colourLow);
        s.colourLower = f(&FETState::colourLower); s.colourLowPeak = f(&FETState::colourLowPeak);
        s.colourLowDc = f(&FETState::colourLowDc); s.colourLowPrevSq = f(&FETState::colourLowPrevSq);
        s.colourLowHarmonicDc = f(&FETState::colourLowHarmonicDc); s.kneeCorrectionDb = f(&FETState::kneeCorrectionDb);
        s.kneeGain = f(&FETState::kneeGain); s.subBassHpState = f(&FETState::subBassHpState);
        s.slowGrDb = g(&FETState::slowGrDb); s.slowTailGrDb = g(&FETState::slowTailGrDb);
        s.lfAheadZ1 = g(&FETState::lfAheadZ1); s.lfAheadZ2 = g(&FETState::lfAheadZ2);
        s.feedbackHpZ1 = g(&FETState::feedbackHpZ1); s.feedbackHpZ2 = g(&FETState::feedbackHpZ2);
        s.partnerHpZ1 = g(&FETState::partnerHpZ1); s.partnerHpZ2 = g(&FETState::partnerHpZ2);
        s.lfHpZ1 = g(&FETState::lfHpZ1); s.lfHpZ2 = g(&FETState::lfHpZ2);
        s.scFilterZ1 = g(&FETState::scFilterZ1); s.scFilterZ2 = g(&FETState::scFilterZ2);
        s.scFilterZ3 = g(&FETState::scFilterZ3); s.scFilterPartnerZ1 = g(&FETState::scFilterPartnerZ1);
        s.scFilterPartnerZ2 = g(&FETState::scFilterPartnerZ2); s.scFilterPartnerZ3 = g(&FETState::scFilterPartnerZ3);
        s.slowShape = x(&FETState::slowShapeExpansion); s.tipCbrt = x(&FETState::tipCbrtExpansion);
        s.tipCos = x(&FETState::tipCosExpansion); s.overshootExp = x(&FETState::overshootExpExpansion);
        s.envelopeGain = x(&FETState::envelopeGainExpansion); s.colourGrDb = x(&FETState::colourGrDbExpansion);
        double at[n], value[n], inverse[n], exponent[n], depthValue[n];
        for (int k = 0; k < n; ++k)
        {
            at[k] = d[k]->kneeExpExpansion.at;
            value[k] = d[k]->kneeExpExpansion.value;
            inverse[k] = d[k]->depthPowExpansion.inverse;
            exponent[k] = d[k]->depthPowExpansion.exponent;
            depthValue[k] = d[k]->depthPowExpansion.value;
            s.kneeGainKey[k] = d[k]->kneeGainKey;
            s.kneeGainValid[k] = d[k]->kneeGainValid;
            s.kneeGainValue[k] = d[k]->kneeGainValue;
        }
        s.kneeExp = {lanesD<L>(at), lanesD<L>(value)};
        {
            bool valid[n], knee[n];
            float u[n], charge[n], slow[n], tail[n], quotient[n], levelSlope[n], slowSlope[n], weight[n], weightSlope[n];
            for (int k = 0; k < n; ++k)
            {
                const auto& carry = d[k]->carry;
                valid[k] = carry.valid; knee[k] = carry.kneeApplied; u[k] = carry.u; charge[k] = carry.charge;
                slow[k] = carry.slow; tail[k] = carry.tail; quotient[k] = carry.quotient;
                levelSlope[k] = carry.levelSlope; slowSlope[k] = carry.slowSlope; weight[k] = carry.weight;
                weightSlope[k] = carry.weightSlope; s.carryKey[k] = carry.key;
            }
            s.carryValid = lanesM<L>(valid); s.carryKnee = lanesM<L>(knee);
            s.carryU = lanesF<L>(u); s.carryCharge = lanesF<L>(charge); s.carrySlow = lanesF<L>(slow);
            s.carryTail = lanesF<L>(tail); s.carryQuotient = lanesF<L>(quotient);
            s.carryLevelSlope = lanesF<L>(levelSlope); s.carrySlowSlope = lanesF<L>(slowSlope);
            s.carryWeight = lanesF<L>(weight); s.carryWeightSlope = lanesF<L>(weightSlope);
        }
        for (int k = 0; k < n; ++k) at[k] = d[k]->depthPowExpansion.at;
        s.depthPow = {lanesD<L>(at), lanesD<L>(inverse), lanesD<L>(exponent), lanesD<L>(depthValue)};
    }
    template <class L>
    static void storeC6(const FetC6Lanes<L>& s, FETState* const* d) noexcept
    {
        constexpr int n = L::kLanes;
        const auto f = [d](float FETState::* m, typename L::F v) noexcept {
            for (int k = 0; k < L::kLanes; ++k) d[k]->*m = laneF<L>(v, k);
        };
        const auto g = [d](double FETState::* m, typename L::D v) noexcept {
            for (int k = 0; k < L::kLanes; ++k) d[k]->*m = laneD<L>(v, k);
        };
        const auto x = [d](FetExpansion FETState::* m, const FetExpansionLanes<L>& v) noexcept {
            for (int k = 0; k < L::kLanes; ++k)
                d[k]->*m = FetExpansion{laneF<L>(v.at, k), laneF<L>(v.value, k), laneF<L>(v.aux, k)};
        };
        f(&FETState::envelope, s.envelope); f(&FETState::fastGrDb, s.fastGrDb);
        f(&FETState::feedbackLag, s.feedbackLag); f(&FETState::riseOnlyPreviousMagnitude, s.riseOnlyPreviousMagnitude);
        f(&FETState::tilt, s.tilt); f(&FETState::dc, s.dc); f(&FETState::prevSq, s.prevSq);
        f(&FETState::tailInputPeak, s.tailInputPeak);
        f(&FETState::colourHighPassLow, s.colourHighPassLow); f(&FETState::colourHighPassLower, s.colourHighPassLower);
        f(&FETState::colourPeak, s.colourPeak); f(&FETState::colourLow, s.colourLow);
        f(&FETState::colourLower, s.colourLower); f(&FETState::colourLowPeak, s.colourLowPeak);
        f(&FETState::colourLowDc, s.colourLowDc); f(&FETState::colourLowPrevSq, s.colourLowPrevSq);
        f(&FETState::colourLowHarmonicDc, s.colourLowHarmonicDc); f(&FETState::kneeCorrectionDb, s.kneeCorrectionDb);
        f(&FETState::kneeGain, s.kneeGain); f(&FETState::subBassHpState, s.subBassHpState);
        g(&FETState::slowGrDb, s.slowGrDb); g(&FETState::slowTailGrDb, s.slowTailGrDb);
        g(&FETState::lfAheadZ1, s.lfAheadZ1); g(&FETState::lfAheadZ2, s.lfAheadZ2);
        g(&FETState::feedbackHpZ1, s.feedbackHpZ1); g(&FETState::feedbackHpZ2, s.feedbackHpZ2);
        g(&FETState::partnerHpZ1, s.partnerHpZ1); g(&FETState::partnerHpZ2, s.partnerHpZ2);
        g(&FETState::lfHpZ1, s.lfHpZ1); g(&FETState::lfHpZ2, s.lfHpZ2);
        g(&FETState::scFilterZ1, s.scFilterZ1); g(&FETState::scFilterZ2, s.scFilterZ2);
        g(&FETState::scFilterZ3, s.scFilterZ3); g(&FETState::scFilterPartnerZ1, s.scFilterPartnerZ1);
        g(&FETState::scFilterPartnerZ2, s.scFilterPartnerZ2); g(&FETState::scFilterPartnerZ3, s.scFilterPartnerZ3);
        x(&FETState::slowShapeExpansion, s.slowShape); x(&FETState::tipCbrtExpansion, s.tipCbrt);
        x(&FETState::tipCosExpansion, s.tipCos); x(&FETState::overshootExpExpansion, s.overshootExp);
        x(&FETState::envelopeGainExpansion, s.envelopeGain); x(&FETState::colourGrDbExpansion, s.colourGrDb);
        for (int k = 0; k < n; ++k)
        {
            d[k]->kneeExpExpansion = FetExpansionD{laneD<L>(s.kneeExp.at, k), laneD<L>(s.kneeExp.value, k)};
            d[k]->depthPowExpansion = FetPowExpansion{laneD<L>(s.depthPow.at, k), laneD<L>(s.depthPow.inverse, k),
                                                      laneD<L>(s.depthPow.exponent, k), laneD<L>(s.depthPow.value, k)};
            d[k]->kneeGainKey = s.kneeGainKey[k];
            d[k]->kneeGainValid = s.kneeGainValid[k];
            d[k]->kneeGainValue = s.kneeGainValue[k];
            auto& carry = d[k]->carry;
            carry.valid = laneM<L>(s.carryValid, k); carry.kneeApplied = laneM<L>(s.carryKnee, k);
            carry.u = laneF<L>(s.carryU, k); carry.charge = laneF<L>(s.carryCharge, k);
            carry.slow = laneF<L>(s.carrySlow, k); carry.tail = laneF<L>(s.carryTail, k);
            carry.quotient = laneF<L>(s.carryQuotient, k); carry.levelSlope = laneF<L>(s.carryLevelSlope, k);
            carry.slowSlope = laneF<L>(s.carrySlowSlope, k); carry.weight = laneF<L>(s.carryWeight, k);
            carry.weightSlope = laneF<L>(s.carryWeightSlope, k); carry.key = s.carryKey[k];
            d[k]->intermediateGrDb = 0.0f;
            d[k]->recoveryGain = 1.0f;
            d[k]->kneeBaseReductionDb = 0.0f;
        }
    }

    // The knob-derived values a C6 sample shares between its lanes, through the memos processFET reads them by.
    struct FetC6Knobs
    {
        float inputGain = 1.0f, thresholdControlDb = 0.0f, attackPosition = 0.0f, releasePosition = 0.0f;
        float staticKnobK3 = 0.0f, outputGain = 1.0f, sr = 0.0f;
        int ratioIndex = 0;
        bool external = false;
        bool scFilter = false;   // the voice's side-chain filter switched in (FET BLUE's SC Filter)
    };
    FetC6Knobs fetC6Knobs(const MultiCompParameterState& p, bool external, float outputDb) noexcept
    {
        FetC6Knobs k;
        const FetVoice* voice = fetVoice;
        const size_t memo = static_cast<size_t>(fetVoiceIndex);
        k.inputGain = memoized(fetInputGainMemo[memo], p.fetInput.load(std::memory_order_relaxed),
                               [voice](float v) noexcept { return decibelsToGain(fetInputGainDb(*voice, v)); });
        k.ratioIndex = fetRatioIndex(p);
        // FET BLUE has no Threshold extension: it reads the reference-compatible -10 dB.
        k.thresholdControlDb = fetVoiceIndex == 1 ? -10.0f : p.fetThreshold.load(std::memory_order_relaxed);
        k.attackPosition = memoized(fetAttackPositionMemo, p.fetAttack.load(std::memory_order_relaxed),
                                    [](float v) noexcept { return fetAttackPosition(v); });
        k.releasePosition = fetReleasePositionOf(p);
        // fbStaticK3KnobShare rho3 (0 = off): the FET's static cubic grows with
        // the Release pot's rate k (k = 1 at Release 0.85-1, 0.217 at the
        // nominal position). Measured on the 1176 (2026-10-15 grids, the
        // Release-axis split at 0 dB GR, 1 kHz and 100 Hz, Inputs .2 / .4 / .8):
        // below the knee its third harmonic is S (1 + 0.30 k), the
        // Release-proportional part in phase with the static one (-10.2 to
        // -10.6 dB at k = 1) and no loop ripple to carry it; the FET's gate
        // network ties to the control node through the pot. It acts on the
        // settled domain the law was measured in (the source bounded at full
        // scale); an onset's first-cycle excursion keeps the bounded cubic above.
        if (fetDynamics.fbStaticK3KnobShare != 0.0f)
            k.staticKnobK3 = memoized(fetStaticKnobK3Memo, k.releasePosition, [this](float releasePosition) noexcept {
                const float potSpan = std::max(
                    1.0e-3f, fetDynamics.fbReleasePotEnd - fetDynamics.fbReleasePotStart);
                const float travel = std::min(1.0f, std::max(0.0f,
                    (fetDynamics.fbReleasePotEnd - releasePosition) / potSpan));
                return fetDynamics.fbStaticK3KnobShare * fetDynamics.fbReleasePotFixed
                    / std::max(fetDynamics.fbReleasePotFixed + travel, 1.0e-6f);
            });
        k.outputGain = memoized(fetOutputGainMemo[memo], outputDb,
                                [voice](float v) noexcept { return decibelsToGain(fetOutputGainDb(*voice, v)); });
        if (fetVoiceIndex == 1)
        {
            // FET BLUE's Headroom: each 4 dB step past 16 dB drives the gain element 4 dB harder and trims the output
            // back, so the gain below threshold stays (its reduction curve moves 4 dB per step, its linear gain does
            // not; tools fet_ae step 0).
            const float headroomGain = memoized(fetHeadroomGainMemo,
                static_cast<float>(p.fetBlueHeadroom.load(std::memory_order_relaxed)),
                [](float step) noexcept { return decibelsToGain(4.0f * (step - 3.0f)); });
            k.inputGain *= headroomGain;
            k.outputGain /= headroomGain;
            k.outputGain *= memoized(fetRatioGainMemo, static_cast<float>(k.ratioIndex), [voice](float position) noexcept {
                return decibelsToGain(voice->ratioGainsDb[static_cast<size_t>(position)]); });
            k.scFilter = p.fetBlueScFilter.load(std::memory_order_relaxed);
        }
        k.sr = static_cast<float>(fs * osFactor);
        k.external = external;
        return k;
    }

    // A carried evaluation holds only under the knobs it was taken with: a lane whose key differs drops it.
    template <class L>
    static void holdCarryUnder(FetC6Lanes<L>& s, const FetC6Knobs& k) noexcept
    {
        const FetCarryKey key{k.ratioIndex, k.thresholdControlDb, k.attackPosition, k.releasePosition, k.sr};
        bool held[L::kLanes];
        for (int lane = 0; lane < L::kLanes; ++lane)
        {
            const FetCarryKey& had = s.carryKey[lane];
            held[lane] = had.ratioIndex == key.ratioIndex && sameBits(had.thresholdControlDb, key.thresholdControlDb)
                && sameBits(had.attackPosition, key.attackPosition) && sameBits(had.releasePosition, key.releasePosition)
                && sameBits(had.sr, key.sr);
            s.carryKey[lane] = key;
        }
        s.carryValid = s.carryValid && lanesM<L>(held);
    }

    // The active voice's amplifier (FetVoice::amplifierPieces) on one sample behind the gain element.
    float fetAmplifierSample(float y) const noexcept
    {
        const FetVoice& v = *fetVoice;
        const float x = y * fetAmplifierInverse;
        const float* breaks = v.amplifierBreaks;
        const int last = v.amplifierPieces;
        if (x <= breaks[0])
        {
            const auto& c = v.amplifierCubics[0];
            return (c[0] + c[1] * (x - breaks[0])) * fetAmplifierReference;
        }
        if (x >= breaks[last])
        {
            const auto& c = v.amplifierCubics[static_cast<size_t>(last - 1)];
            const float d = breaks[last] - breaks[last - 1];
            const float end = ((c[3] * d + c[2]) * d + c[1]) * d + c[0];
            const float slope = (3.0f * c[3] * d + 2.0f * c[2]) * d + c[1];
            return (end + slope * (x - breaks[last])) * fetAmplifierReference;
        }
        int low = 0, high = last;            // breaks[low] <= x < breaks[high]
        while (high - low > 1)
        {
            const int middle = (low + high) / 2;
            if (breaks[middle] <= x) low = middle; else high = middle;
        }
        const auto& c = v.amplifierCubics[static_cast<size_t>(low)];
        const float d = x - breaks[low];
        return (((c[3] * d + c[2]) * d + c[1]) * d + c[0]) * fetAmplifierReference;
    }

    // FET BLACK's measured colour stage on the C6 arm (FetVoice::measuredChain): `compressed`, the signal behind
    // the gain element, with its measured even and odd terms added.
    template <class L>
    typename L::F fetC6MeasuredColour(FetC6Lanes<L>& s, const FetC6Knobs& k, typename L::F compressed) noexcept
    {
        using F = typename L::F;
        using lanes::vmax; using lanes::vabs;
        const auto Kf = [](float x) noexcept { return L::f(x); };
        const auto clampF = [](F v, F lo, F hi) noexcept { return L::sel(v < lo, lo, L::sel(hi < v, hi, v)); };
        const auto all = L::m(true);
        F saturated = compressed;
        const F grDb = -expandedGainToDecibelsT<L>(s.colourGrDb, s.envelope + Kf(0.001f), all);
        F k2, k3, lowFrequencyK2;
        fetFeedbackColourT<L>(grDb, k2, k3, lowFrequencyK2);
        constexpr float kFetBroadbandK3SourceLimit = 8.0f;
        const F k3Source = clampF(saturated, Kf(-kFetBroadbandK3SourceLimit), Kf(kFetBroadbandK3SourceLimit));
        const F sq = saturated * saturated;
        const F saturatedBeforeColour = saturated;
        const F alpha = Kf(fetColourDcAlpha);
        const F h2 = alpha * (s.dc + sq - s.prevSq);
        s.dc = h2;
        s.prevSq = sq;
        const F colourHighPassCoeff = Kf(fetColourCorner250);
        s.colourHighPassLow = s.colourHighPassLow + colourHighPassCoeff * (saturated - s.colourHighPassLow);
        F colourHighPassed = saturated - s.colourHighPassLow;
        s.colourHighPassLower = s.colourHighPassLower
            + colourHighPassCoeff * (colourHighPassed - s.colourHighPassLower);
        colourHighPassed = colourHighPassed - s.colourHighPassLower;
        const F colourPeakRelease = Kf(fetColourPeakRelease);
        const F colourHighPassedAbs = vabs(colourHighPassed);
        s.colourPeak = L::sel(colourHighPassedAbs >= s.colourPeak, colourHighPassedAbs,
                              colourHighPassedAbs + (s.colourPeak - colourHighPassedAbs) * colourPeakRelease);
        // The C6 arm's broadband fifth is zero (fetBroadbandK5 stays with the arms that run the earlier cell), and
        // so are the low band's third and fifth (below): their terms are left out. Each was 0 times a finite value,
        // a signed zero, which left the sums unchanged (no output bit moved over the identity scenarios and the
        // lane fuzz). The high-passed peak above keeps running for the All-buttons arm.
        const F colourLowCoeff = Kf(fetColourCorner250);
        s.colourLow = s.colourLow + colourLowCoeff * (saturated - s.colourLow);
        s.colourLower = s.colourLower + colourLowCoeff * (s.colourLow - s.colourLower);
        const F colourLowSq = s.colourLower * s.colourLower;
        const F colourLowH2 = alpha * (s.colourLowDc + colourLowSq - s.colourLowPrevSq);
        s.colourLowDc = colourLowH2;
        s.colourLowPrevSq = colourLowSq;
        const F colourLowPeakRelease = Kf(fetColourPeakRelease);
        const F colourLowerAbs = vabs(s.colourLower);
        s.colourLowPeak = L::sel(colourLowerAbs >= s.colourLowPeak, colourLowerAbs,
                                 colourLowerAbs + (s.colourLowPeak - colourLowerAbs) * colourLowPeakRelease);
        const F lowNormalised = clampF(s.colourLower / vmax(s.colourLowPeak, Kf(1.0e-9f)), Kf(-1.0f), Kf(1.0f));
        const F low2 = lowNormalised * lowNormalised;
        const F low4 = low2 * low2;
        const F lowHarmonicDrive = clampF(grDb / Kf(14.0f), Kf(0.0f), Kf(1.0f));
        // The C6 loop's own ripple supplies the odd harmonics the fitted K3 / K5 terms stood in for on the
        // earlier cell (2026-10-15/16): only the even term runs on this arm.
        F lowHarmonics = s.colourLowPeak * lowHarmonicDrive * (Kf(0.0037f) * (Kf(8.0f) * low4 - Kf(8.0f) * low2));
        // (The shallow-knee cell runs only with the loop off: processFET.)
        s.colourLowHarmonicDc = s.colourLowHarmonicDc + (Kf(1.0f) - alpha) * (lowHarmonics - s.colourLowHarmonicDc);
        lowHarmonics = lowHarmonics - s.colourLowHarmonicDc;
        saturated = saturated + (k2 * h2 + lowFrequencyK2 * colourLowH2
            + k3 * k3Source * k3Source * k3Source
            + lowHarmonics);
        if (k.staticKnobK3 != 0.0f)
        {
            const F settledSource = clampF(saturatedBeforeColour, Kf(-1.0f), Kf(1.0f));
            saturated = saturated + k3 * Kf(k.staticKnobK3) * settledSource * settledSource * settledSource;
        }
        return saturated;
    }

    // One C6 sample on the lanes' channels (`channels[lane]`): the colour stage, the detector, the loop, the knee
    // and the output stage of processFET's vintage arm. `partnerSample` is the linked partner's driven sample
    // (MultiCompDSP's link feed, zero when unlinked or mono), `transientFactor` the transient shaper's factor.
    template <class L>
    typename L::F fetC6Step(FetC6Lanes<L>& s, const FetC6Knobs& k, const int* channels, typename L::F input,
                            typename L::F sidechain, typename L::F partnerSample,
                            typename L::F transientFactor) noexcept
    {
        using F = typename L::F;
        using D = typename L::D;
        using lanes::vmax; using lanes::vabs; using lanes::visfinite;
        const auto Kf = [](float x) noexcept { return L::f(x); };
        const auto Kd = [](double x) noexcept { return L::d(x); };
        const auto clampF = [](F v, F lo, F hi) noexcept { return L::sel(v < lo, lo, L::sel(hi < v, hi, v)); };
        const auto all = L::m(true);
        const float sr = k.sr;
        const F gained = input * Kf(k.inputGain);
        // The low-frequency section ahead of the gain element (lfAheadHz).
        F gainedAhead = gained;
        if (fetLfAheadOn)
        {
            const auto& c = fetLfAheadHighPass;
            const D x = L::toD(gained);
            const D y = Kd(c[0]) * x + s.lfAheadZ1;
            s.lfAheadZ1 = Kd(c[1]) * x - Kd(c[3]) * y + s.lfAheadZ2;
            s.lfAheadZ2 = Kd(c[2]) * x - Kd(c[4]) * y;
            gainedAhead = L::toF(y);
        }
        const F compressed = gainedAhead * s.envelope;
        // Vintage FET saturation is inside the feedback loop. The sidechain must see this signal, not the
        // post-envelope shortcut (see processFET for the colour stage's measurements).
        F saturated = compressed;
        if (fetVoice->measuredChain)
            saturated = fetC6MeasuredColour<L>(s, k, compressed);
        else if (fetVoice->amplifierPieces > 0)
            saturated = whereF<L>(all, compressed, [&](int lane) noexcept {
                return fetAmplifierSample(laneF<L>(compressed, lane)); });

        // Candidate C6: the loop below charges from the audio path itself, rectified through the side chain's
        // high-pass (fbTapHighPassHz), OR-ed with the linked partner's (zero when unlinked or mono); see
        // processFET's notes. fbRectifierImbalance (eps; 0 = symmetric): eps < 0 weights the positive half
        // 1 + eps, eps > 0 the negative 1 - eps.
        const auto& c = fetTapHighPass;
        const auto highPass = [&c, Kd](D x, D& z1, D& z2) noexcept {
            const D y = Kd(c[0]) * x + z1;
            z1 = Kd(c[1]) * x - Kd(c[3]) * y + z2;
            z2 = Kd(c[2]) * x - Kd(c[4]) * y;
            return y;
        };
        D own = highPass(L::toD(k.external ? sidechain * Kf(k.inputGain) : gained),
                         s.feedbackHpZ1, s.feedbackHpZ2);
        D partner = highPass(L::toD(partnerSample * Kf(k.inputGain)), s.partnerHpZ1, s.partnerHpZ2);
        if (fetVoice->scFilterSections > 0)
        {
            // The voice's side-chain filter (FetVoice) runs on both taps whether or not its switch is in, so the
            // switch takes effect without a transient.
            const auto shelf = [Kd](const std::array<double, 3>& c, D x, D& z) noexcept {
                const D y = Kd(c[0]) * x + z;
                z = Kd(c[1]) * x - Kd(c[2]) * y;
                return y;
            };
            const auto filter = [&](D x, D& z1, D& z2, D& z3) noexcept {
                x = shelf(fetScFilter[0], x, z1);
                x = shelf(fetScFilter[1], x, z2);
                return shelf(fetScFilter[2], x, z3) * Kd(fetScFilterGain);
            };
            const D ownFiltered = filter(own, s.scFilterZ1, s.scFilterZ2, s.scFilterZ3);
            const D partnerFiltered = filter(partner, s.scFilterPartnerZ1, s.scFilterPartnerZ2, s.scFilterPartnerZ3);
            if (k.scFilter)
            {
                own = ownFiltered;
                partner = partnerFiltered;
            }
        }
        const double imbalance = fetDynamics.fbRectifierImbalance;
        const auto rectify = [imbalance, Kd](D x) noexcept {
            if (imbalance > 0.0) return L::sel(x < Kd(0.0), -x * Kd(1.0 - imbalance), vabs(x));
            if (imbalance < 0.0) return L::sel(x > Kd(0.0), x * Kd(1.0 + imbalance), vabs(x));
            return vabs(x);
        };
        const F feedbackMagnitude = L::toF(vmax(rectify(own), rectify(partner)));
        // The tilt keeps running for the All-buttons arm, which reads it.
        s.tilt = s.tilt + Kf(fetTilt) * (feedbackMagnitude - s.tilt);
        advanceFetFeedback<L>(s, feedbackMagnitude, k, transientFactor);
        // Below All-buttons the envelope's ceiling is 1.
        s.envelope = clampF(s.envelope, Kf(0.001f), Kf(1.0f));
        s.envelope = L::sel(visfinite(s.envelope), s.envelope, Kf(1.0f));

        // Candidate C6: the 4:1 knee onset as a function of the loop's own reduction. The settled input level
        // that holds this reduction on the accepted law is read back, and the measured onset table's difference
        // there is applied; no raw-input peak cell.
        s.kneeCorrectionDb = Kf(0.0f);
        // Past the handoff (input level >= threshold + 6 dB) the correction is 0. The settled input level rises
        // with the reduction and the tail population only adds to it, so the other two populations read on the
        // law bound it from below: when that bound clears the handoff by 0.01 dB on every lane, nothing below
        // can change the correction.
        bool kneeCorrects = fetVoice->measuredChain && k.ratioIndex == fetVoice->unitRatio;
        if (kneeCorrects)
        {
            const F lowerReduction = L::toF(L::toD(s.fastGrDb) + s.slowGrDb);
            F lowerSlope;
            const F lowerInput = lowerReduction
                + fetStaticOutputLevelDbT<L>(*fetVoice, lowerReduction, k.ratioIndex, k.thresholdControlDb, lowerSlope);
            kneeCorrects = !L::all(lowerInput >= Kf(k.thresholdControlDb + 6.01f));
        }
        if (kneeCorrects)
        {
            const float kneeDb = fetTailKneeDb();
            const F tail = L::toF(s.slowTailGrDb);
            F tailApplied = tail;
            const auto tailHeld = tail > Kf(0.0f);
            if (kneeDb > 0.0f && takes<L>(tailHeld))
            {
                F restSlope, lawSlope;
                const F applied = fetTailAppliedT<L>(L::toF(L::toD(s.fastGrDb) + s.slowGrDb),
                                                     fetTailControlT<L>(tail, kneeDb), kneeDb, restSlope, lawSlope);
                tailApplied = L::sel(tailHeld, applied, tail);
            }
            const F reductionDb = L::toF(L::toD(s.fastGrDb) + s.slowGrDb + L::toD(tailApplied));
            F unusedSlope;
            const F settledInputDb = reductionDb
                + fetStaticOutputLevelDbT<L>(*fetVoice, reductionDb, k.ratioIndex, k.thresholdControlDb, unusedSlope);
            const F handoff = clampF(settledInputDb - Kf(k.thresholdControlDb + 10.0f) + Kf(5.0f), Kf(0.0f), Kf(1.0f));
            // Past the handoff (handoff 1) the factor below is 0 and the correction stays 0.
            const auto inKnee = !(handoff >= Kf(1.0f));
            if (L::any(inKnee))
            {
                const F onset = whereF<L>(inKnee, Kf(0.0f), [&](int lane) noexcept {
                    return fetReferenceKneeOnsetDb(laneF<L>(settledInputDb, lane), k.thresholdControlDb); });
                s.kneeCorrectionDb = L::sel(inKnee,
                    clampF((reductionDb - onset) * (Kf(1.0f) - handoff * handoff * (Kf(3.0f) - Kf(2.0f) * handoff)),
                           Kf(-0.85f), Kf(0.85f)),
                    s.kneeCorrectionDb);
            }
        }
        {
            float gains[L::kLanes];
            for (int lane = 0; lane < L::kLanes; ++lane)
            {
                const float correction = laneF<L>(s.kneeCorrectionDb, lane);
                std::uint32_t bits;
                std::memcpy(&bits, &correction, sizeof bits);
                if (!s.kneeGainValid[lane] || s.kneeGainKey[lane] != bits)
                {
                    s.kneeGainKey[lane] = bits;
                    s.kneeGainValue[lane] = decibelsToGain(correction);
                    s.kneeGainValid[lane] = true;
                }
                gains[lane] = s.kneeGainValue[lane];
            }
            s.kneeGain = lanesF<L>(gains);
        }

        // The output stage. The sag gain (All-buttons only) and this arm's recovery gain are 1; the products stay
        // (with denormals flushed, x * 1 is not x for a subnormal x).
        F out = saturated * Kf(1.0f);
        out = out * (s.kneeGain * Kf(1.0f));
        if (fetVoice->measuredChain)
        {
            if (!fetConvolutionUnity)
                out = whereF<L>(all, out, [&](int lane) noexcept {
                    return fetConvolution.processSample(laneF<L>(out, lane), channels[lane]); });
            out = out * Kf(fetHardwareGain);
        }
        if (fetDynamics.lfHpOn > 0.5f)
        {
            const auto& h = fetLfHighPass;
            const D x = L::toD(out);
            const D y = Kd(h[0]) * x + s.lfHpZ1;
            s.lfHpZ1 = Kd(h[1]) * x - Kd(h[3]) * y + s.lfHpZ2;
            s.lfHpZ2 = Kd(h[2]) * x - Kd(h[4]) * y;
            out = L::toF(y);
        }
        else
        {
            const float hpfAlpha = 1.0f - std::exp(-2.0f * kDuskPi * 5.0f / sr);
            s.subBassHpState = s.subBassHpState + Kf(hpfAlpha) * (out - s.subBassHpState);
            out = out - s.subBassHpState;
        }
        // The installed model is linear throughout the measured steady range, then bends startup peaks toward a
        // ~7.1 linear ceiling (processFET documents the fit).
        const F drivenOutput = out * Kf(k.outputGain);
        constexpr float ceilingKnee = 1.97879118f;
        constexpr float ceilingSpan = 5.16417142f;
        constexpr float ceilingScale = 6.11998526f;
        const auto above = !(vabs(drivenOutput) <= Kf(ceilingKnee));
        if (!MC2_UNLIKELY(L::any(above))) return drivenOutput;
        return whereF<L>(above, drivenOutput, [&](int lane) noexcept {
            const float driven = laneF<L>(drivenOutput, lane);
            const float limited = ceilingKnee + ceilingSpan * (2.0f / kDuskPi)
                * std::atan((std::abs(driven) - ceilingKnee) / ceilingScale);
            return std::copysign(limited, driven); });
    }

    // Candidate C6: one implicit step of the feedback gain computer (see
    // kFetDynamicsDefaults), on the lanes' channels. `magnitude` is the rectified driven signal BEFORE the
    // envelope (the loop applies it), fastGrDb the charged capacitor, slowGrDb / slowTailGrDb the slow
    // populations; all in dB of reduction.
    template <class L>
    void advanceFetFeedback(FetC6Lanes<L>& s, typename L::F magnitude, const FetC6Knobs& k,
                             typename L::F transientFactor) const noexcept
    {
        using F = typename L::F;
        using D = typename L::D;
        using M = typename L::M;
        using lanes::vmax; using lanes::vmin; using lanes::vabs; using lanes::vsqrt;
        const auto Kf = [](float x) noexcept { return L::f(x); };
        const auto Kd = [](double x) noexcept { return L::d(x); };
        const auto all = L::m(true);
        // Without the transient shaper every lane's factor is exactly 1, and x / 1 is x: the divisions below are
        // then left out (a block-constant branch).
        const bool unitTransient = L::all(transientFactor == Kf(1.0f));
        const int ratioIndex = k.ratioIndex;
        const float sr = k.sr;
        constexpr float dbToNeper = 0.11512925f;
        const FetFeedbackTerms& terms = fetFeedbackKnobTerms(ratioIndex, k.thresholdControlDb, k.attackPosition,
                                                             k.releasePosition, sr);
        const F tailDb = L::toF(s.slowTailGrDb);
        // fbTailKneeDb: the tail's own reading against what it adds.
        const float kneeDb = fetTailKneeDb();
        const M kneeApplied = L::m(kneeDb > 0.0f) && tailDb > Kf(0.0f);
        F tailControl = Kf(0.0f);
        F tailAppliedDb = tailDb;
        if (L::any(kneeApplied))
        {
            tailControl = L::sel(kneeApplied, fetTailControlT<L>(tailDb, kneeDb), tailControl);
            F restSlope, lawSlope;
            tailAppliedDb = L::sel(kneeApplied,
                fetTailAppliedT<L>(L::toF(L::toD(s.fastGrDb) + s.slowGrDb), tailControl, kneeDb, restSlope, lawSlope),
                tailDb);
        }
        // The leak slows with depth: 1 + G / D, and at 4:1 with
        // fbReleaseDepthExtraExponent p > 0 the term (P / De)^p is added to that
        // factor, 1 + G / D + (P / De)^p (De fbReleaseDepthExtraDb), P = slow +
        // tail, the reduction the populations hold. The code applies it as the
        // multiplier 1 + (P / De)^p / (1 + G / D), exactly 1 at the other ratios, so
        // they and 4:1's verified rounding stay as they were.
        // Measured on the reference unit at 4:1 (2026-10-15/16): its settled
        // Release-proportional ripple, the gain's droop within a cycle, sits 0.3-0.8
        // dB under the linear form's at 24-34 dB GR (fresh 1 kHz and 100 Hz grids);
        // after a 3 s hold at 33 dB it lets go more slowly (the linear form was 1.2
        // dB short at 0.5 s), while 20-300 ms after short deep bursts, which charge
        // the fast capacitor but not the populations, it lets go no more slowly. So
        // the slowing follows the charge held over time. 20:1 matches without it.
        // Programme reads 0.001-0.009 dB worse pooled with it and the constants
        // recalibrated alongside (no cell over 1 dB); that trade was accepted on
        // 2026-10-18.
        const float releaseDepthDb = terms.releaseDepthDb;
        const D reductionSum = L::toD(s.fastGrDb) + s.slowGrDb + L::toD(tailAppliedDb);
        // Divisions by block-constant values on the path to the leak are multiplications by their reciprocals
        // (double; the products land on the same floats but for rare last-place ties).
        const D depthRatio = reductionSum * Kd(1.0 / static_cast<double>(std::max(releaseDepthDb, 1.0f)));
        // (1 + G / D) (1 + (P / De)^p / (1 + G / D)) is 1 + G / D + (P / De)^p: summed instead (double; the float
        // result moves only at last-place ties), which saves the division.
        D extraDepth = Kd(0.0);
        if (fetDynamics.fbReleaseDepthExtraExponent > 0.0f && ratioIndex == fetVoice->unitRatio)
            extraDepth = expandedDepthPowT<L>(s.depthPow,
                    vmax(Kd(0.0), (s.slowGrDb + L::toD(tailAppliedDb))
                        * Kd(1.0 / static_cast<double>(std::max(fetDynamics.fbReleaseDepthExtraDb, 1.0f)))),
                    static_cast<double>(fetDynamics.fbReleaseDepthExtraExponent), all);
        const F releaseSeconds = L::toF(Kd(static_cast<double>(terms.releaseScale))
            * ((Kd(1.0) + depthRatio) + extraDepth));
        // dB charged per sample per unit of relative overshoot.
        const float fullCharge = terms.fullCharge;
        // The lag pole (fbAttackLagRatio): this sample's overshoot enters with
        // weight lagStep, the lagged current of the last sample carries the rest.
        const float lagStep = terms.lagStep;
        // fbChargeRiseOnly s (0 = off): a diode-fed capacitor charges while the
        // rectified drive rises; this share of the falling flank's charge is
        // withheld. Measured on the 1176's ripple (2026-10-16): it turns the
        // ripple's phase forward as the 1176's sits, without changing a settled
        // sine's level (the tip balance below is told) or the first-cycle peak.
        const float riseOnly = std::clamp(fetDynamics.fbChargeRiseOnly, 0.0f, 1.0f);
        const M driveFalling = magnitude < s.riseOnlyPreviousMagnitude;
        s.riseOnlyPreviousMagnitude = magnitude;
        const F charge = riseOnly > 0.0f
            ? L::sel(driveFalling, Kf(fullCharge * lagStep * (1.0f - riseOnly)), Kf(fullCharge * lagStep))
            : Kf(fullCharge * lagStep);
        // Knee release (fbReleaseKneeShare a, fbReleaseKneeDb g0,
        // fbReleaseKneeKnobExponent beta; a = 0 off). At 4:1, whose soft knee
        // is the onset remap below, the fast capacitor lets go more slowly
        // near the knee: its leak time x (1 + a (1 / k)^beta exp(-G / g0)), G the
        // reduction the depth law reads, k the pot's release rate over its fast
        // end. Read from the 1176's knee probes (100 Hz and 1 kHz, 1 dB steps
        // across the knee, Release 0 / .5 / .666 / 1, 2026-10-15/16): its
        // Release-proportional ripple there is 2.1x / 1.8x / 1.5x smaller than the
        // plain cell's at 0.27 / 0.78 / 1.39 dB GR, and more so at slow Release
        // positions (about k^0.4 on top of k). The same slowing is the 1176's
        // last 10 % of a release step (rel90). 20:1 already matches without it.
        // Past terms.kneeDeepDb the knee term is under 2^-25 and the float factor is exactly 1: lanes there skip
        // the exponential (its anchor then refreshes later, from wherever the reduction returns to).
        F kneeReleaseFactor = Kf(1.0f);
        if (fetDynamics.fbReleaseKneeShare > 0.0f && ratioIndex == fetVoice->unitRatio)
        {
            const M deep = reductionSum > Kd(terms.kneeDeepDb);
            if (!L::all(deep))
                kneeReleaseFactor = L::sel(deep, Kf(1.0f), L::toF(Kd(1.0) + Kd(static_cast<double>(terms.kneeKnob))
                    * expandedExpDT<L>(s.kneeExp,
                        -reductionSum * Kd(1.0 / static_cast<double>(std::max(fetDynamics.fbReleaseKneeDb, 1.0e-3f))),
                        !deep)));
        }
        const F leak = smallExpT<L>(
            Kf(-1.0f) / (vmax(releaseSeconds * kneeReleaseFactor, Kf(1.0e-4f)) * Kf(sr)));
        // A sine settles with its peaks a little above the threshold: the
        // tips, +/- phi around each peak, charge what the leak removes over
        // the cycle, (2 / pi) (tan phi - phi) = c (1 - leak) / fullCharge. The
        // threshold is lowered by that overshoot, 1 / cos phi, so the settled
        // loop sits on the static law, which was measured with sines.
        // fbTipReferenceShare w: the threshold is told (1 - leak) + w (leak - nominalLeak), nominalLeak the nominal
        // Release position's at this reduction; at 1 the loop sits on the static law there only, and settles
        // shallower where it leaks faster.
        F tipLeak = Kf(1.0f) - leak;
        if (fetDynamics.fbTipReferenceShare > 0.0f)
        {
            const F nominalSeconds = L::toF(Kd(static_cast<double>(terms.releaseScaleNominal))
                * ((Kd(1.0) + reductionSum * Kd(1.0 / static_cast<double>(std::max(terms.releaseDepthDbNominal, 1.0f))))
                   + extraDepth));
            const F nominalLeak = smallExpT<L>(
                Kf(-1.0f) / (vmax(nominalSeconds * kneeReleaseFactor, Kf(1.0e-4f)) * Kf(sr)));
            tipLeak = tipLeak + Kf(fetDynamics.fbTipReferenceShare) * (leak - nominalLeak);
        }
        const F tipBalance = Kf(1.57079633f) * tipLeak * Kf(1.0f / terms.tipDenominator);
        const float slowShare = std::max(fetDynamics.fbSlowShare, 0.0f);
        const float slowCeilingDb = std::max(fetDynamics.fbSlowCeilingDb, 1.0e-3f);
        // The tail population's settled size, scaled by the ratio coordinate
        // the release depth uses (1 at 4:1).
        const float tailShare = terms.tailShare;
        const float tailCurve = std::max(fetDynamics.fbTailCurve, 0.0f) * 0.05f;
        const auto settledTail = [tailShare, tailCurve, Kf](F c, F& slope) noexcept {
            slope = Kf(tailShare) * (Kf(1.0f) + Kf(2.0f * tailCurve) * c);
            return Kf(tailShare) * c * (Kf(1.0f) + Kf(tailCurve) * c);
        };
        const auto settledSlow = [&s, slowShare, slowCeilingDb, &settledTail, Kf](F c, F& slope, M active) noexcept {
            const F shape = expandedTanhT<L>(s.slowShape, Kf(slowShare) * c / Kf(slowCeilingDb), active);
            F tailSlope;
            const F tail = settledTail(c, tailSlope);
            slope = Kf(slowShare) * (Kf(1.0f) - shape * shape) + tailSlope;
            return Kf(slowCeilingDb) * shape + tail;
        };
        const float attackLoadDb = terms.attackLoadDb;
        // The overshoot's drive term in linear form: exp(dbToNeper (levelDb - A)), levelDb the floored magnitude in
        // dB, is that magnitude times exp(-dbToNeper A). No logarithm per sample, and the exponential's argument
        // then moves only as fast as the charge (its expansion seldom refreshes).
        const F drive = vmax(magnitude, Kf(1.0e-9f));
        const F slow = L::toF(s.slowGrDb + L::toD(tailDb));
        // The lagged current of the last sample lands on top of the leak; the
        // sum keeps the loop's 60 dB bound.
        constexpr float maximumChargeDb = 60.0f;
        const F released = vmin(Kf(maximumChargeDb), s.fastGrDb * leak
            + Kf(fullCharge * (1.0f - lagStep)) * s.feedbackLag);
        // Turn-on (fbTurnOnDb, fbTurnOnBiasDb): the share of the overshoot that
        // charges, the square root of the present reduction plus the Attack
        // position's bias over the turn-on width, 1 above it. The floor keeps
        // it off zero, so the charge leaves rest on the branch that rises
        // (linearly in u) instead of staying at the root u = 0.
        const bool floorOnApplied = fetDynamics.fbFloorOnApplied > 0.5f;
        const float maximumReductionDb = terms.maximumReductionDb;
        const float turnOnDb = fetDynamics.fbTurnOnDb;
        const float turnOnBiasDb = terms.turnOnBiasDb;
        // Relative overshoot of the output above the threshold at charge c,
        // and its derivative with respect to u = sqrt(c) (the tip term's own
        // derivative is left to the bracket; near rest under the turn-on it is
        // not small, and the bracket absorbs it). The static law's knee
        // starts with infinite slope in c and a finite one in u. `weight` is
        // the turn-on share at that charge and `weightSlope` its derivative.
        // quotient: the ratio without the drive; levelSlope / slowSlope: the exponent's slopes in the charge and the
        // slow population (no transient shaping), which a carried evaluation extrapolates with.
        struct Evaluation { F over, derivative, weight, weightSlope, quotient, levelSlope, slowSlope; };
        const auto overshoot = [&](F u, M active) noexcept {
            Evaluation r;
            const F c = u * u;
            // What the slow populations add at this charge, and how fast
            // that grows with it (fbTailKneeDb; otherwise their states).
            F slowNow = slow, appliedSlope = Kf(0.0f);
            if (L::any(kneeApplied))
            {
                F restSlope, lawSlope;
                const F applied = fetTailAppliedT<L>(L::toF(L::toD(c) + s.slowGrDb), tailControl, kneeDb,
                                                     restSlope, lawSlope);
                slowNow = L::sel(kneeApplied, L::toF(s.slowGrDb + L::toD(applied)), slow);
                appliedSlope = L::sel(kneeApplied, restSlope, Kf(0.0f));
            }
            r.weight = Kf(1.0f);
            r.weightSlope = Kf(0.0f);
            if (turnOnDb > 0.0f)
            {
                const F position = (c + slowNow + Kf(turnOnBiasDb)) / Kf(turnOnDb);
                const M under = position < Kf(1.0f);
                if (L::any(under))
                {
                    const F weight = vsqrt(position);
                    r.weight = L::sel(under, weight, r.weight);
                    r.weightSlope = L::sel(under, u * (Kf(1.0f) + appliedSlope) / (Kf(turnOnDb) * weight),
                                           r.weightSlope);
                }
            }
            F slowSlope, levelSlope;
            F settledSlowDb = settledSlow(c, slowSlope, active);
            if (kneeDb > 0.0f && tailShare > 0.0f)
            {
                // The settled tail, a control value here, adds through the
                // same law.
                F tailSlope, restSlope, lawSlope;
                const F tail = settledTail(c, tailSlope);
                const F population = settledSlowDb - tail;
                const F populationSlope = slowSlope - tailSlope;
                settledSlowDb = population + fetTailAppliedT<L>(c + population, tail, kneeDb, restSlope, lawSlope);
                slowSlope = populationSlope
                    + restSlope * (Kf(1.0f) + populationSlope)
                    + lawSlope * tailSlope;
            }
            const F settledReductionDb = c + settledSlowDb;
            F settledLevelDb = Kf(attackLoadDb) + fetStaticOutputLevelDbT<L>(
                *fetVoice, settledReductionDb, ratioIndex, k.thresholdControlDb, levelSlope, !floorOnApplied);
            // The gain element's floor belongs to the reduction applied,
            // not to the settled one the threshold is read at.
            F floorSlope = Kf(0.0f);
            if (floorOnApplied)
            {
                const M overFloor = c + slowNow > Kf(maximumReductionDb);
                floorSlope = L::sel(overFloor, Kf(100.0f), floorSlope);
                settledLevelDb = L::sel(overFloor,
                    settledLevelDb + Kf(100.0f) * (c + slowNow - Kf(maximumReductionDb)), settledLevelDb);
            }
            // Series inverse of tan phi - phi; held where it stops rising.
            // The tips charge through the turn-on share as well.
            const F tip = vmin(expandedCbrtT<L>(s.tipCbrt, Kf(3.0f) * tipBalance * c / r.weight, active), Kf(1.5f));
            const F phase = tip - Kf(0.13333333f) * tip * tip * tip;
            F tipRatio = Kf(1.0f) / expandedCosT<L>(s.tipCos, phase, active);
            if (fetDynamics.fbRectifierImbalance != 0.0f)
            {
                // Two unequal tips per cycle share the charge one period needs
                // (a tip charges ~ overshoot^1.5): the stronger half's overshoot
                // x solves x^1.5 + max(0, (1 - e)(1 + x) - 1)^1.5 = 2 x0^1.5, x0
                // the symmetric one, so a settled sine's stronger peak still
                // sits on the static law.
                const float e = std::min(std::abs(fetDynamics.fbRectifierImbalance), 0.9f);
                const F x0 = vmax(tipRatio - Kf(1.0f), Kf(0.0f));
                const F target = Kf(2.0f) * x0 * vsqrt(x0);
                // While the weaker tip does not charge ((1 - e)(1 + x) <= 1) the root is x = 2^(2/3) x0; past
                // that, Newton from there. At the shipped e (0.1), against Newton's six steps from x0 over every
                // float x0 up to 1.05: tipRatio within 1.8e-7 relative (the six steps themselves sit within
                // 4.9e-8 of the converged root; 2026-10-20).
                F x = Kf(1.58740105f) * x0;
                M newton = Kf(1.0f - e) * (Kf(1.0f) + x) - Kf(1.0f) > Kf(0.0f);
                for (int step = 0; step < 3 && L::any(newton); ++step)
                {
                    const F weak = vmax(Kf(0.0f), Kf(1.0f - e) * (Kf(1.0f) + x) - Kf(1.0f));
                    const F g = x * vsqrt(x) + weak * vsqrt(weak) - target;
                    const F gp = Kf(1.5f) * vsqrt(x) + Kf(1.5f * (1.0f - e)) * vsqrt(weak);
                    newton = newton && gp > Kf(0.0f);
                    x = L::sel(newton, vmax(Kf(0.0f), x - g / gp), x);
                }
                tipRatio = Kf(1.0f) + x;
            }
            const F growth = expandedExpT<L>(s.overshootExp, Kf(-dbToNeper)
                * ((unitTransient ? c + slowNow : (c + slowNow) / transientFactor) + settledLevelDb), active);
            const F ratio = drive * growth * tipRatio;
            const F bracket = (unitTransient ? Kf(1.0f) + appliedSlope : (Kf(1.0f) + appliedSlope) / transientFactor)
                + (Kf(1.0f) + slowSlope) * levelSlope
                + floorSlope * (Kf(1.0f) + appliedSlope);
            r.derivative = Kf(-2.0f) * u * ratio * Kf(dbToNeper) * bracket;
            r.over = ratio - Kf(1.0f);
            r.quotient = growth * tipRatio;
            r.levelSlope = bracket;
            r.slowSlope = (Kf(1.0f) + appliedSlope) * (Kf(1.0f) + floorSlope);
            return r;
        };
        F u = vsqrt(vmax(released, Kf(0.0f)));
        // Steps that cannot overshoot skip the evaluation: the loop charges only around peaks, and on programme
        // about four steps in five sit well under the point where it would. A lane is quiet when an upper bound on
        // log2 of the ratio below stays under 0 (with a 0.002 margin, 0.012 dB, for rounding and the expansions):
        // - the exponent (c + slowNow) + settledLevelDb is at least c + slowNowLow + attackLoadDb + the static law
        //   (nondecreasing) at c + the settled populations' lower bounds, the floor's term being >= 0. slowNow is
        //   `slow` exactly without the tail's knee; with it, at least the slow population, plus the tail's control
        //   value once the rest is past the knee, where the law adds exactly that (to rounding: 1e-4 dB margin).
        //   The settled slow population is at least its ceiling x tanh's lower bound x / (1 + x), and the settled
        //   tail at least tailShare c, through its law once the rest is past the knee (0.99999 covers rounding);
        // - log2 of the drive is at most its exponent and mantissa fraction + 0.0861 (lanes::vlog2Above);
        // - the tip ratio is at most 2.6027 (tip <= 1.5, so phase <= 1.05; the imbalance's x <= 2^(2/3) x0), or
        //   1 + k q / (1 - q), q = phase^2 / 2, from cos p >= 1 - p^2 / 2 with the tip bounded by its cube root's
        //   tangent at the expansion's anchor (above a concave function) at a weight no larger than the
        //   evaluation's (the slow population alone in the turn-on position).
        // A quiet lane's overshoot is <= 0, so it does not solve and its lag input, weight x max(0, overshoot), is
        // 0 either way. What changes is that its tip cube root, tip cosine and overshoot exp expansions are not
        // refreshed on those steps: results move at the rounding level. The decision is per lane and the
        // evaluation masks quiet lanes' expansions, so a pair still equals two single channels. Only lanes whose
        // transient factor is 1 (no transient shaping on this step) are bounded.
        const M unitLanes = transientFactor == Kf(1.0f);
        // A lane carries its last exact evaluation (FETState::FetCarry) while the state it was taken at has hardly
        // moved: within 0.003 dB of its charge and slow population, the tail reading within 0.0002 dB, the same
        // tail-knee state, no transient shaping and the same knobs (holdCarryUnder). The carried ratio is the
        // evaluation's value without the drive times this step's drive, extrapolated to first order in the charge
        // and the slow population. It does not follow the tip's dependence on the pre-step reduction (leak, release
        // knee, 4:1 depth term), so its error is bounded by the validity window, not by the expansion: review
        // 2026-10-21, ~25M carried steps at 44.1-192 kHz, 25 missed charges with exact overshoots <= 7e-5, output
        // within 0.007 dB of exact evaluation in 5 ms windows above -80 dBFS. It decides whether the step charges
        // and seeds the solver in place of the bound and the first evaluation; the solver's evaluations stay exact,
        // so a charging step still ends on a root within its tolerance.
        const F charge0 = u * u;
        const F slowFloat = L::toF(s.slowGrDb);
        M carried = L::m(false);
        if (L::any(s.carryValid))
            carried = s.carryValid && unitLanes && ((kneeApplied && s.carryKnee) || !(kneeApplied || s.carryKnee))
                && vabs(charge0 - s.carryCharge) <= Kf(3.0e-3f) && vabs(slowFloat - s.carrySlow) <= Kf(3.0e-3f)
                && vabs(tailDb - s.carryTail) <= Kf(2.0e-4f);
        const M bounded = unitLanes && !carried;
        M quiet = L::m(false);
        if (L::any(bounded))
        {
            const F c = charge0;
            F slowNowLow = L::sel(kneeApplied, L::toF(s.slowGrDb), slow);
            if (L::any(kneeApplied))
                slowNowLow = L::sel(kneeApplied && L::toF(L::toD(c) + s.slowGrDb) >= Kf(kneeDb),
                    L::toF(s.slowGrDb + L::toD(vmax(Kf(0.0f), tailControl - Kf(1.0e-4f)))), slowNowLow);
            const F x = Kf(slowShare) * c / Kf(slowCeilingDb);
            const F settledLow = Kf(slowCeilingDb) * x / (Kf(1.0f) + x) * Kf(0.99999f);
            const F tailLow = Kf(tailShare) * c * Kf(0.99999f);
            const F settledAdd = kneeDb > 0.0f && tailShare > 0.0f
                ? L::sel(c + settledLow >= Kf(kneeDb), tailLow, Kf(0.0f)) : tailLow;
            F unusedSlope;
            const F levelLow = c + slowNowLow + Kf(attackLoadDb)
                + fetStaticOutputLevelDbT<L>(*fetVoice, c + settledLow + settledAdd, ratioIndex, k.thresholdControlDb,
                                             unusedSlope, !floorOnApplied);
            const F room = levelLow * Kf(1.0f / 6.02059991f) - Kf(0.002f) - lanes::vlog2Above(drive);
            quiet = bounded && Kf(1.3840f) < room;
            if (!L::all(quiet) && L::any(bounded && room > Kf(0.0f)))
            {
                F weightLow = Kf(1.0f);
                if (turnOnDb > 0.0f)
                    weightLow = vsqrt(vmin(Kf(1.0f), (c + slowNowLow + Kf(turnOnBiasDb)) * Kf(0.999999f / turnOnDb)));
                const F argument = Kf(3.0f) * tipBalance * c / weightLow;
                const auto& anchor = s.tipCbrt;
                const F tip = vmin(anchor.value
                        * (Kf(1.0f) + (argument - anchor.at) * anchor.aux * Kf(0.333333343f)), Kf(1.5f))
                    * Kf(1.000001f);
                const F phase = tip - Kf(0.13333333f) * tip * tip * tip;
                const F q = Kf(0.5f) * phase * phase;
                const float tipFactor = (fetDynamics.fbRectifierImbalance != 0.0f ? 1.58740105f : 1.0f)
                    * 1.44269504f * 1.0001f;
                quiet = quiet || (bounded && Kf(tipFactor) * q / (Kf(1.0f) - q) < room);
            }
        }
        const auto keepCarry = [&](const Evaluation& e, F at, M lanesOn) noexcept {
            const M on = lanesOn && unitLanes;
            s.carryValid = s.carryValid || on;
            s.carryKnee = L::sel(on, kneeApplied, s.carryKnee);
            s.carryU = L::sel(on, at, s.carryU);
            s.carryCharge = L::sel(on, at * at, s.carryCharge);
            s.carrySlow = L::sel(on, slowFloat, s.carrySlow);
            s.carryTail = L::sel(on, tailDb, s.carryTail);
            s.carryQuotient = L::sel(on, e.quotient, s.carryQuotient);
            s.carryLevelSlope = L::sel(on, e.levelSlope, s.carryLevelSlope);
            s.carrySlowSlope = L::sel(on, e.slowSlope, s.carrySlowSlope);
            s.carryWeight = L::sel(on, e.weight, s.carryWeight);
            s.carryWeightSlope = L::sel(on, e.weightSlope, s.carryWeightSlope);
        };
        Evaluation first{Kf(-1.0f), Kf(0.0f), Kf(1.0f), Kf(0.0f), Kf(0.0f), Kf(0.0f), Kf(0.0f)};
        const M evaluated = !carried && !quiet;
        if (L::any(evaluated))
        {
            const Evaluation e = overshoot(u, evaluated);
            keepCarry(e, u, evaluated);
            first.over = L::sel(evaluated, e.over, first.over);
            first.weight = L::sel(evaluated, e.weight, first.weight);
            first.weightSlope = L::sel(evaluated, e.weightSlope, first.weightSlope);
            first.derivative = L::sel(evaluated, e.derivative, first.derivative);
        }
        if (L::any(carried))
        {
            const F exponent = Kf(dbToNeper) * (s.carryLevelSlope * (charge0 - s.carryCharge)
                + s.carrySlowSlope * (slowFloat - s.carrySlow));
            const F ratio = drive * s.carryQuotient * (Kf(1.0f) - exponent + Kf(0.5f) * exponent * exponent);
            first.over = L::sel(carried, ratio - Kf(1.0f), first.over);
            first.weight = L::sel(carried, vmin(Kf(1.0f), s.carryWeight + s.carryWeightSlope * (u - s.carryU)),
                                  first.weight);
            first.weightSlope = L::sel(carried, s.carryWeightSlope, first.weightSlope);
            first.derivative = L::sel(carried, Kf(-2.0f) * u * ratio * Kf(dbToNeper) * s.carryLevelSlope,
                                      first.derivative);
        }
        const F initial = first.over;
        F solvedOvershoot = initial;
        F solvedWeight = first.weight;
        const M solve = initial > Kf(0.0f);
        if (L::any(solve))
        {
            // h(u) = u^2 - released - charge * weight(u) * max(0, overshoot(u))
            // has one root between the released charge and the explicit step
            // at full weight. Under the turn-on it can fall before it rises,
            // but wherever it is positive it rises (there charge * weight *
            // overshoot <= c - released, which bounds the weight's own term
            // below u). Newton, bisecting whenever a step leaves the bracket
            // or stops halving. Each lane iterates as the scalar loop would; a lane that has stopped keeps its
            // values and its expansions' anchors. The first step may span the whole bracket: it lands near the
            // explicit step times the turn-on weight, which above half weight a half-bracket limit turned into a
            // bisection and five more evaluations (owner ruling 2026-10-20: the faster path is allowed although
            // it stops at a different point within the tolerance than the earlier solver did).
            F low = u;
            F high = vsqrt(vmin(released + charge * initial, Kf(maximumChargeDb)));
            F value = -charge * first.weight * initial;
            F slope = Kf(2.0f) * u - charge * (first.weight * first.derivative + first.weightSlope * initial);
            F previousStep = Kf(2.0f) * (high - low);
            M active = solve && high - low > Kf(1.0e-6f);
            for (int iteration = 0; iteration < 16 && L::any(active); ++iteration)
            {
                F next = L::sel(slope > Kf(0.0f), u - value / slope, high + Kf(1.0f));
                const M bisect = !(next > low && next < high) || vabs(next - u) > Kf(0.5f) * previousStep;
                next = L::sel(bisect, Kf(0.5f) * (low + high), next);
                previousStep = L::sel(active, vabs(next - u), previousStep);
                u = L::sel(active, next, u);
                const Evaluation e = overshoot(u, active);
                keepCarry(e, u, active);
                solvedOvershoot = L::sel(active, e.over, solvedOvershoot);
                solvedWeight = L::sel(active, e.weight, solvedWeight);
                const F valueNow = u * u - released - charge * e.weight * vmax(e.over, Kf(0.0f));
                const F slopeNow = Kf(2.0f) * u - L::sel(e.over > Kf(0.0f),
                    charge * (e.weight * e.derivative + e.weightSlope * e.over), Kf(0.0f));
                value = L::sel(active, valueNow, value);
                slope = L::sel(active, slopeNow, slope);
                const M stay = active && !(vabs(valueNow) < Kf(1.0e-5f));
                const M below = valueNow < Kf(0.0f);
                low = L::sel(stay && below, u, low);
                high = L::sel(stay && !below, u, high);
                active = stay && high - low > Kf(1.0e-6f);
            }
        }
        const F charged = u * u;
        // The lag pole's own state, advanced from the overshoot at the solved
        // charge. Recovering it as (charged - released) / fullCharge instead
        // quantises a ~1e-5 dB increment to the capacitor's float spacing: the
        // current then never decays and the release stalls at high rates.
        s.feedbackLag = s.feedbackLag + Kf(lagStep) * (solvedWeight * vmax(solvedOvershoot, Kf(0.0f)) - s.feedbackLag);
        s.feedbackLag = L::sel(s.feedbackLag < Kf(1.0e-30f), Kf(0.0f), s.feedbackLag);
        s.fastGrDb = charged;
        F slowApplied = slow;
        if (L::any(kneeApplied))
        {
            F restSlope, lawSlope;
            const F applied = fetTailAppliedT<L>(L::toF(L::toD(charged) + s.slowGrDb), tailControl, kneeDb,
                                                 restSlope, lawSlope);
            slowApplied = L::sel(kneeApplied, L::toF(s.slowGrDb + L::toD(applied)), slow);
        }
        s.envelope = expandedDecibelsToGainT<L>(s.envelopeGain, unitTransient ? -(charged + slowApplied)
                                                                              : -(charged + slowApplied) / transientFactor, all);
        F unusedSlope;
        // The tail population follows its own settled size in its own times,
        // whatever the Release knob says; the other one as before.
        const F tailSettled = settledTail(charged, unusedSlope);
        F tailTarget = tailSettled;
        if (fetDynamics.fbTailInputLimit > 0.5f)
        {
            // fbTailInputLimit: the tail charges no further than its settled size at the charge the signal's own
            // level settles to (the voice's static reduction at its peak follower, the threshold load included).
            // While the signal holds, that is above the settled size at the present charge, which the rest of the
            // reduction keeps under it; once the signal falls, the tail stops filling. A tail above it is not
            // pulled down: it lets go toward its settled size at the present charge, as without the limit. Lanes
            // whose tail is not charging keep their target as it is (the limit cannot bind there).
            s.tailInputPeak = vmax(magnitude, s.tailInputPeak * Kf(terms.tailInputDecay));
            const F held = kneeDb > 0.0f ? tailControl : tailDb;
            const M charging = tailTarget > held;
            if (L::any(charging))
            {
                const FetVoice& voice = *fetVoice;
                const float levelOffsetDb = attackLoadDb;
                const float thresholdControlDb = k.thresholdControlDb;
                const F inputReduction = L::mapF([&voice, levelOffsetDb, ratioIndex, thresholdControlDb](float peak) {
                    return fetReferenceReductionDb(voice, 20.0f * std::log10(std::max(peak, 1.0e-9f)) - levelOffsetDb,
                                                   ratioIndex, thresholdControlDb);
                }, s.tailInputPeak);
                tailTarget = L::sel(charging, vmin(tailTarget, vmax(settledTail(inputReduction, unusedSlope), held)),
                                    tailTarget);
            }
        }
        const M tailRuns = L::m(tailShare > 0.0f) || s.slowTailGrDb > Kd(0.0);
        if (L::any(tailRuns))
        {
            // The floor comes first so that a non-finite lab value lands on it.
            // With fbTailKneeDb the target is a control value and so is the
            // state that follows it; the reading F(v) is what is kept.
            const D target = L::toD(tailTarget);
            const M tailCharging = kneeDb > 0.0f ? target > L::toD(tailControl) : tailTarget > tailDb;
            const D tailStep = L::sel(tailCharging, Kd(terms.tailStepCharging), Kd(terms.tailStepReleasing));
            D next;
            if (kneeDb > 0.0f)
            {
                const double knee = static_cast<double>(kneeDb);
                const D held = vmax(s.slowTailGrDb, Kd(0.0));
                const M heldBelow = held < Kd(knee);
                D control = held + Kd(knee);
                if (MC2_UNLIKELY(takes<L>(heldBelow))) control = L::sel(heldBelow, Kd(2.0) * vsqrt(Kd(knee) * held), control);
                const D state = target + (control - target) * tailStep;
                const M stateBelow = state < Kd(2.0 * knee);
                next = state - Kd(knee);
                if (MC2_UNLIKELY(takes<L>(stateBelow))) next = L::sel(stateBelow, state * state / Kd(4.0 * knee), next);
            }
            else
                next = target + (s.slowTailGrDb - target) * tailStep;
            next = L::sel(next < Kd(1.0e-30), Kd(0.0), next);
            s.slowTailGrDb = L::sel(tailRuns, next, s.slowTailGrDb);
        }
        const F slowTarget = settledSlow(charged, unusedSlope, all) - tailSettled;
        const M slowCharging = L::toD(slowTarget) > s.slowGrDb;
        F slowStep = Kf(terms.slowChargeStep);
        if (takes<L>(!slowCharging))
            slowStep = L::sel(slowCharging, slowStep,
                smallExpT<L>(Kf(-1.0f) / (vmax(Kf(fetDynamics.fbSlowReleaseRatio) * releaseSeconds
                    * Kf(terms.slowReleaseKnob), Kf(1.0e-4f)) * Kf(sr))));
        s.slowGrDb = L::toD(slowTarget) + (s.slowGrDb - L::toD(slowTarget)) * L::toD(slowStep);
    }

    // processFET's C6 arm over a run of oversampled samples on the lanes' channels, their state held in lanes
    // throughout (processFetC6Block).
    template <class L>
    void fetC6Run(const int* channels, const MultiCompParameterState& p, bool external, int samples, int factor,
                  const float* outputDb, const float* const* audio, const float* const* sidechain,
                  const float* const* partner, float* const* out) noexcept
    {
        constexpr int n = L::kLanes;
        FETState* state[n];
        for (int lane = 0; lane < n; ++lane)
        {
            state[lane] = &(fetVoiceIndex == 1 ? fetBlue : fet)[static_cast<size_t>(channels[lane])];
            fetInputTransformerResting[static_cast<size_t>(channels[lane])] = true;
        }
        FetC6Lanes<L> lanesState;
        loadC6(lanesState, state);
        // FET BLACK's Transient extension; FET BLUE has none.
        const float transientAmount = fetVoiceIndex == 0 ? p.fetTransient.load(std::memory_order_relaxed) : 0.0f;
        for (int i = 0; i < samples; ++i)
        {
            const FetC6Knobs knobs = fetC6Knobs(p, external, outputDb[i]);
            holdCarryUnder<L>(lanesState, knobs);
            for (int phase = 0; phase < factor; ++phase)
            {
                const int k = i * factor + phase;
                float input[n], sc[n], partnerSample[n];
                for (int lane = 0; lane < n; ++lane)
                {
                    input[lane] = audio[lane][k];
                    sc[lane] = sidechain[lane][k];
                    partnerSample[lane] = partner != nullptr ? partner[lane][k] : 0.0f;
                }
                // The transient shaper's factor, 1 without it (built in a register: a per-lane fill of 1 became a
                // library call per sample).
                auto transientFactor = L::f(1.0f);
                if (transientAmount > 0.01f)
                {
                    float transient[n];
                    for (int lane = 0; lane < n; ++lane)
                        transient[lane] = std::max(1.0f, transientShaper.process(input[lane], channels[lane],
                                                                                 transientAmount));
                    transientFactor = lanesF<L>(transient);
                }
                const auto y = fetC6Step<L>(lanesState, knobs, channels, lanesF<L>(input), lanesF<L>(sc),
                                            lanesF<L>(partnerSample), transientFactor);
                for (int lane = 0; lane < n; ++lane) out[lane][k] = laneF<L>(y, lane);
            }
        }
        storeC6(lanesState, state);
    }

    // The colour stage's gain-to-dB on one channel's anchor, for processFET's arms off the C6 loop.
    static float expandedGainToDecibels(FetExpansion& e, float gain) noexcept
    {
        FetExpansionLanes<lanes::Lane1> lane{e.at, e.value, e.aux};
        const float y = expandedGainToDecibelsT<lanes::Lane1>(lane, gain, true);
        e = FetExpansion{lane.at, lane.value, lane.aux};
        return y;
    }

    // One sample of the active FET voice (selectFetVoice) on channel `ch`. Both voices run candidate C6 (see
    // kFetDynamicsDefaults); FET BLACK's All-buttons, and FET BLACK with the loop switched off, keep the vintage
    // arm below.
    float processFET(float input, int ch, float sidechain,
                     const MultiCompParameterState& p,
                     bool external, bool linked) noexcept
    {
        const float sr = static_cast<float>(fs * osFactor);
        if (fetFeedbackActive(p))
        {
            auto& d = fetVoiceIndex == 1 ? fetBlue[ch] : fet[ch];
            fetInputTransformerResting[static_cast<size_t>(ch)] = true;
            // The colour stage, the detector, the loop, the knee and the output stage on this channel
            // (fetC6Step). MultiCompDSP's FET block path steps the same arm a block at a time (processFetC6Block).
            FETState* const state[1] = {&d};
            FetC6Lanes<lanes::Lane1> lanesState;
            loadC6(lanesState, state);
            float& partnerSample = fetLinkPartner[static_cast<size_t>(ch)];
            const float partner = partnerSample;
            partnerSample = 0.0f;
            // FET BLACK's Transient extension; FET BLUE has none.
            const float transientAmount = fetVoiceIndex == 0 ? p.fetTransient.load(std::memory_order_relaxed) : 0.0f;
            const float transientFactor = transientAmount > 0.01f
                ? std::max(1.0f, transientShaper.process(input, ch, transientAmount)) : 1.0f;
            const FetC6Knobs knobs = fetC6Knobs(p, external, p.fetOutput.load(std::memory_order_relaxed));
            holdCarryUnder<lanes::Lane1>(lanesState, knobs);
            const float out = fetC6Step<lanes::Lane1>(lanesState, knobs, &ch, input, sidechain, partner, transientFactor);
            storeC6(lanesState, state);
            return out;
        }
        auto& d = fet[ch];
        auto& inT = inputTransformerFet[ch];
        const float inputValue = p.fetInput.load(std::memory_order_relaxed);
        const float inputGain = memoized(fetInputGainMemo[0], inputValue, [](float v) noexcept {
            return decibelsToGain(fetInputGainDb(kFetBlackVoice, v)); });
        // Equivalent source-level/Input combinations have the same measured
        // vintage output spectrum. The generic transformer saturates far
        // earlier than this unit when driven by its 40 dB Input range, so the
        // calibrated vintage coloration below owns the audio stage. Its subtly
        // shaped signal remains useful to the unlinked detector.
        const int ratioIndex = std::clamp(p.fetRatio.load(std::memory_order_relaxed), 0, 4);
        if (fetInputTransformerResting[static_cast<size_t>(ch)])
        {
            inT.reset();
            fetInputTransformerResting[static_cast<size_t>(ch)] = false;
        }
        const float transformedInput = inT.processSample(input, ch);
        const float gained = input * inputGain;
        // The low-frequency section ahead of the gain element (lfAheadHz).
        float gainedAhead = gained;
        if (fetLfAheadOn)
        {
            const auto& c = fetLfAheadHighPass;
            const double x = gained;
            const double y = c[0] * x + d.lfAheadZ1;
            d.lfAheadZ1 = c[1] * x - c[3] * y + d.lfAheadZ2;
            d.lfAheadZ2 = c[2] * x - c[4] * y;
            gainedAhead = static_cast<float>(y);
        }
        const float compressed = gainedAhead * d.envelope;
        float saturated = compressed;
        {
            // Vintage FET saturation is inside the feedback loop.  The
            // sidechain must see this signal, not the post-envelope shortcut.
            const float grDb = -expandedGainToDecibels(d.colourGrDbExpansion, d.envelope + 0.001f);
            const float k2 = fetBroadbandK2(grDb);
            const float k3 = fetBroadbandK3(grDb);
            const float lowFrequencyK2 = fetLowFrequencyK2(grDb);
            // `fetBroadbandK3` is a settled small-signal fit. On the first
            // uncompressed cycle, Input can drive this internal node tens of
            // times beyond every fitted row; extrapolating x^3 there creates
            // a false rectified crest and even reverses the 1 kHz fundamental.
            // The installed unit bounds that cell before its output stage.
            // Keep the fitted polynomial bit-identical throughout its measured
            // domain and bound only the out-of-domain source that it consumes.
            // An 8.0 source limit is the measured compromise across the 1 kHz
            // startup surface, the 0.5-20 ms attack curve, and the independent
            // 4 kHz Attack-knob surface; see the Wave 21 evidence note.
            constexpr float kFetBroadbandK3SourceLimit = 8.0f;
            const float k3Source = std::clamp(
                saturated, -kFetBroadbandK3SourceLimit,
                kFetBroadbandK3SourceLimit);
            const float sq = saturated * saturated;
            const float alpha = fetColourDcAlpha;
            const float h2 = alpha * (d.dc + sq - d.prevSq);
            d.dc = h2;
            d.prevSq = sq;
            // High-pass and normalise the broadband colour source so its
            // Chebyshev T5 term contributes fifth harmonic without folding a
            // large cubic back into the independently fitted K3 surface or
            // duplicating the low-frequency transformer term.
            const float colourHighPassCoeff = fetColourCorner250;
            d.colourHighPassLow += colourHighPassCoeff
                * (saturated - d.colourHighPassLow);
            float colourHighPassed = saturated - d.colourHighPassLow;
            d.colourHighPassLower += colourHighPassCoeff
                * (colourHighPassed - d.colourHighPassLower);
            colourHighPassed -= d.colourHighPassLower;
            const float colourPeakRelease = fetColourPeakRelease;
            const float colourHighPassedAbs = std::abs(colourHighPassed);
            d.colourPeak = colourHighPassedAbs >= d.colourPeak
                ? colourHighPassedAbs
                : colourHighPassedAbs + (d.colourPeak - colourHighPassedAbs)
                    * colourPeakRelease;
            const float colourNormalised = std::clamp(
                colourHighPassed / std::max(d.colourPeak, 1.0e-9f),
                -1.0f, 1.0f);
            const float colour2 = colourNormalised * colourNormalised;
            const float colour3 = colour2 * colourNormalised;
            const float colour5 = colour2 * colour3;
            const float broadbandHarmonicDrive = std::clamp(
                grDb / 14.0f, 0.0f, 1.0f);
            const float broadbandH5 = d.colourPeak * broadbandHarmonicDrive
                * fetBroadbandK5(grDb)
                * (16.0f * colour5 - 20.0f * colour3
                    + 5.0f * colourNormalised);
            // The hardware's low-frequency transformer asymmetry grows with
            // gain reduction. Generate that even harmonic from a 250 Hz
            // two-pole low-passed copy; `fetLowFrequencyK2` above is its
            // measured coefficient.
            //
            // The two poles put this path 49 dB down at 1 kHz once squared,
            // which is NOT negligible against `fetBroadbandK2` and must not be
            // treated as if it were: measured by rebuilding with the
            // coefficient zeroed, the term moves the 1 kHz second harmonic by
            // up to 0.175 dB (Input 0.6, -24 dBFS). Any change here has to be
            // scored on the 1 kHz surface as well as the 100 Hz one -- the
            // Wave 9 fit carries the 39-row 1 kHz set as a constraint for
            // exactly this reason, and lands it at 0.2335 dB worst against the
            // 0.2625 dB it inherited.
            const float colourLowCoeff = fetColourCorner250;
            d.colourLow += colourLowCoeff * (saturated - d.colourLow);
            d.colourLower += colourLowCoeff * (d.colourLow - d.colourLower);
            const float colourLowSq = d.colourLower * d.colourLower;
            const float colourLowH2 = alpha
                * (d.colourLowDc + colourLowSq - d.colourLowPrevSq);
            d.colourLowDc = colourLowH2;
            d.colourLowPrevSq = colourLowSq;
            const float colourLowPeakRelease = fetColourPeakRelease;
            const float colourLowerAbs = std::abs(d.colourLower);
            d.colourLowPeak = colourLowerAbs >= d.colourLowPeak
                ? colourLowerAbs
                : colourLowerAbs
                    + (d.colourLowPeak - colourLowerAbs)
                        * colourLowPeakRelease;
            const float lowNormalised = std::clamp(
                d.colourLower / std::max(d.colourLowPeak, 1.0e-9f),
                -1.0f, 1.0f);
            const float low2 = lowNormalised * lowNormalised;
            const float low3 = low2 * lowNormalised;
            const float low4 = low2 * low2;
            const float low5 = low4 * lowNormalised;
            const float lowHarmonicDrive = std::clamp(grDb / 14.0f, 0.0f, 1.0f);
            const float lowFrequencyK3 = fetLowFrequencyK3(grDb);
            const float lowFrequencyK5 = fetLowFrequencyK5(grDb);
            const float lowT3 = 4.0f * low3 - 3.0f * lowNormalised;
            float lowHarmonics = d.colourLowPeak * lowHarmonicDrive
                * (lowFrequencyK3 * lowT3
                    + 0.0037f * (8.0f * low4 - 8.0f * low2)
                    + lowFrequencyK5 * (16.0f * low5 - 20.0f * low3
                        + 5.0f * lowNormalised));
            // The shallow-knee cell (its T3 / T5 lags and corrections, on the knee peak below): only 4:1 with
            // the C6 loop off (fbOn) reads it. With the loop on, as shipped, no ratio does, so it does not run.
            if (!(fetDynamics.fbOn > 0.5f))
            {
                const float shallowNormalised = std::clamp(
                    gained / std::max(d.kneePeak, 1.0e-9f), -1.0f, 1.0f);
                const float shallowNormalised2
                    = shallowNormalised * shallowNormalised;
                const float shallowNormalised3
                    = shallowNormalised2 * shallowNormalised;
                const float shallowT3 = 4.0f * shallowNormalised2
                    * shallowNormalised - 3.0f * shallowNormalised;
                const float shallowT5 = 16.0f * shallowNormalised3
                        * shallowNormalised2
                    - 20.0f * shallowNormalised3
                    + 5.0f * shallowNormalised;
                const float lowT3LagCoeff = fetLowT3Corner300;
                d.shallowT3Lag += lowT3LagCoeff
                    * (shallowT3 - d.shallowT3Lag);
                d.shallowT3Lower += lowT3LagCoeff
                    * (d.shallowT3Lag - d.shallowT3Lower);
                d.shallowT3Lowest += lowT3LagCoeff
                    * (d.shallowT3Lower - d.shallowT3Lowest);
                d.shallowT5Lag += lowT3LagCoeff
                    * (shallowT5 - d.shallowT5Lag);
                d.shallowT5Lower += lowT3LagCoeff
                    * (d.shallowT5Lag - d.shallowT5Lower);
                d.shallowT5Lowest += lowT3LagCoeff
                    * (d.shallowT5Lower - d.shallowT5Lowest);
                const float shallowDriveDb = gainToDecibels(
                    std::max(d.kneePeak, 1.0e-12f));
                const std::array<float, 4> shallowOddCorrection
                    = ratioIndex == 0
                        ? fetShallowKneeOddCorrection(shallowDriveDb)
                        : std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}};
                const float inputPosition = std::clamp(
                    (inputValue + 20.0f) / 60.0f, 0.0f, 1.0f);
                const float releasePosition = fetReleasePositionOf(p);
                const std::array<float, 4> broadbandComplexH3Correction
                    = ratioIndex == 0
                        ? fetBroadbandComplexH3Correction(
                            inputPosition, releasePosition, shallowDriveDb, fs)
                        : std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}};
                const std::array<float, 4> broadbandComplexH5Correction
                    = ratioIndex == 0
                        ? fetBroadbandComplexH5Correction(
                            inputPosition, releasePosition, shallowDriveDb, fs)
                        : std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}};
                lowHarmonics += d.colourLowPeak
                    * (shallowOddCorrection[0] * d.shallowT3Lag
                        + shallowOddCorrection[1] * d.shallowT3Lower
                        + shallowOddCorrection[2] * d.shallowT5Lag
                        + shallowOddCorrection[3] * d.shallowT5Lower);
                lowHarmonics += d.kneePeak
                    * (broadbandComplexH3Correction[0] * shallowT3
                        + broadbandComplexH3Correction[1] * d.shallowT3Lag
                        + broadbandComplexH3Correction[2] * d.shallowT3Lower
                        + broadbandComplexH3Correction[3] * d.shallowT3Lowest);
                lowHarmonics += d.kneePeak
                    * (broadbandComplexH5Correction[0] * shallowT5
                        + broadbandComplexH5Correction[1] * d.shallowT5Lag
                        + broadbandComplexH5Correction[2] * d.shallowT5Lower
                        + broadbandComplexH5Correction[3] * d.shallowT5Lowest);
            }
            d.colourLowHarmonicDc += (1.0f - alpha)
                * (lowHarmonics - d.colourLowHarmonicDc);
            lowHarmonics -= d.colourLowHarmonicDc;
            saturated += k2 * h2 + lowFrequencyK2 * colourLowH2
                + k3 * k3Source * k3Source * k3Source
                + broadbandH5 + lowHarmonics;
        }

        float detect = 0.0f;
        float kneeDetect = 0.0f;
        float vintageInstantLevel = 0.0f;
        bool vintageProgrammeActive = false;
        bool vintageProgrammeSupported = false;
        {
            // The static reference law above is expressed on the signal driven
            // by Input, before gain reduction. Its feedback topology has the
            // same settled transfer, but using the already-reduced signal with
            // a feed-forward slope produces a second, incorrect ratio. The
            // fast peak detector supplies a stable level to the timing stage.
            // The ordinary internal path follows the oversampled audio and its
            // measured reconstruction latency. Only an external detector or
            // an active vintage stereo link consumes the sidechain prepared by
            // MultiCompDSP; using it unconditionally erased the first-cycle
            // overshoot by giving the detector unintended lookahead.
            const float detectorSignal = (external || linked)
                ? sidechain * inputGain : transformedInput * inputGain;
            const float instantLevel = std::abs(detectorSignal);
            vintageInstantLevel = instantLevel;
            vintageProgrammeActive = instantLevel > fetDynamics.programmeActiveLevel;
            if (fetDynamics.detChargeOn > 0.5f)
            {
                // Candidate C2: a charged detector. Instant capture reads dense
                // broadband peaks that a finite-attack cap integrates over.
                const float position = fetAttackPosition(
                    p.fetAttack.load(std::memory_order_relaxed));
                const float chargeSeconds = fetDynamics.detAttackSlowSeconds
                    * std::pow(fetDynamics.detAttackFastSeconds
                        / fetDynamics.detAttackSlowSeconds, position)
                    * std::pow(fetRatioAttackScale(kFetBlackVoice, ratioIndex),
                               fetDynamics.detRatioExponent);
                float chargeLevel = instantLevel;
                if (fetDynamics.detAnalyticOn > 0.5f)
                {
                    const double a = runAllPassChain(detectorSignal, fetAnalyticA,
                                                      d.analyticAx, d.analyticAy);
                    const double b = runAllPassChain(detectorSignal, fetAnalyticB,
                                                     d.analyticBx, d.analyticBy);
                    chargeLevel = static_cast<float>(std::sqrt(a * a + b * b));
                }
                const float coefficient = chargeLevel > d.peak
                    ? std::exp(-1.0f / (chargeSeconds * sr))
                    : std::exp(-1.0f / (fetDynamics.detReleaseSeconds * sr));
                d.peak += (1.0f - coefficient) * (chargeLevel - d.peak);
                detect = d.peak * decibelsToGain(fetDynamics.detGainDb);
            }
            else
            {
                if (instantLevel > d.peak)
                    d.peak = instantLevel;
                else
                {
                    // The detector must reach the same upper-knob floor as the gain
                    // cell; retaining 40 ms here held even a 25 ms phrase after it ended.
                    const float peakReleaseSeconds = 0.040f + (0.004f - 0.040f)
                        * std::clamp(2.5f * fetReleasePositionOf(p) - 1.25f, 0.0f, 1.0f)
                        * fetProgrammeReleaseDepth(d.programmeMaximumGrDb);
                    const float peakReleaseCoeff = std::exp(-1.0f / (peakReleaseSeconds * sr));
                    d.peak += (1.0f - peakReleaseCoeff) * (instantLevel - d.peak);
                }
                detect = d.peak;
            }
            // The knee's raw-input peak: read by 4:1 with the C6 loop off only (as the shallow-knee cell).
            if (!(fetDynamics.fbOn > 0.5f))
            {
                const float kneeInstantLevel = std::abs(
                    (external || linked) ? sidechain * inputGain
                                         : input * inputGain);
                const float kneePeakRelease = std::exp(
                    -1.0f / (0.250f * sr));
                constexpr float kneeHoldSeconds = 0.030f;
                const float sampleSeconds = 1.0f / sr;
                const auto advanceHeldPeak = [kneePeakRelease, kneeHoldSeconds,
                                              sampleSeconds](
                    float inputLevel, float& peak,
                    float& holdSeconds) noexcept {
                    if (inputLevel >= peak)
                    {
                        peak = inputLevel;
                        holdSeconds = kneeHoldSeconds;
                    }
                    else if (holdSeconds > 0.0f)
                        holdSeconds = std::max(0.0f, holdSeconds - sampleSeconds);
                    else
                        peak = inputLevel
                            + (peak - inputLevel) * kneePeakRelease;
                };
                advanceHeldPeak(kneeInstantLevel, d.kneePeak,
                                d.kneePeakHoldSeconds);
                kneeDetect = d.kneePeak;
            }
        }
        d.tilt += fetTilt * (detect - d.tilt);
        detect = std::max(detect + (detect - d.tilt) * 0.35f, 0.0f);
        // JUCE applies the all-buttons threshold shift exactly once below via
        // abiThreshold = threshold * 0.5f.  Do not fold a second -6.0206 dB
        // offset into the base threshold here.
        const float thresholdControlDb = p.fetThreshold.load(std::memory_order_relaxed);
        const float threshold = memoized(fetThresholdGainMemo, thresholdControlDb,
                                         [](float v) noexcept { return decibelsToGain(v); });
        float reduction = 0.0f;
        float kneeTargetReduction = 0.0f;
        float kneeHandoff = 1.0f;
        {
            const float detectorLevelDb = gainToDecibels(
                std::max(detect, 1.0e-12f));
            const bool measuredExtension
                = p.fetCurve.load(std::memory_order_relaxed) != 0;
            if (ratioIndex == 4 && measuredExtension)
            {
                const float abiThreshold = threshold * 0.5f;
                if (detect > abiThreshold)
                    reduction = lookupTables.getAllButtonsReduction(
                        gainToDecibels(detect / abiThreshold), true);
            }
            else
            {
                if (ratioIndex == 0)
                {
                    // The installed unit's shallow knee is frequency-flat even
                    // though its deeper feedback detector is not. A second,
                    // raw-input peak cell supplies only this onset interval;
                    // it hands back to the transformed detector before the
                    // accepted LF response and harmonic anchors. Its release
                    // spans many 100 Hz half-cycles, preventing rectifier ripple
                    // from turning the common static curve into a frequency law.
                    const float kneeLevelDb = gainToDecibels(
                        std::max(kneeDetect, 1.0e-12f));
                    const float onsetReduction = fetReferenceKneeOnsetDb(
                        kneeLevelDb, thresholdControlDb);
                    const float normalisedKneeLevel = kneeLevelDb
                        - (thresholdControlDb + 10.0f);
                    // The onset table itself rejoins the accepted detector law
                    // at -4 dB. Fade this auxiliary cell out over the preceding
                    // decibel; carrying it above that join adds a second,
                    // 100 ms trajectory to the already-matched -42 dBFS attack.
                    const float handoff = std::clamp(
                        (normalisedKneeLevel + 5.0f) / 1.0f,
                        0.0f, 1.0f);
                    const float smoothHandoff = handoff * handoff
                        * (3.0f - 2.0f * handoff);
                    kneeTargetReduction = onsetReduction;
                    kneeHandoff = smoothHandoff;
                    // Keep the established detector/envelope trajectory as the
                    // coloration coordinate. The shallow auxiliary gain cell
                    // below supplies only the difference to the measured knee,
                    // so every harmonic ratio remains on its fitted axis.
                    reduction = fetReferenceReductionDb(
                        kFetBlackVoice, detectorLevelDb, ratioIndex, thresholdControlDb);
                }
                else
                    reduction = fetReferenceReductionDb(
                        kFetBlackVoice, detectorLevelDb, ratioIndex, thresholdControlDb);
                if (ratioIndex == 4)
                    reduction += fetAllProcessorCorrectionDb(detectorLevelDb);
            }
            if (p.fetTransient.load(std::memory_order_relaxed) > 0.01f)
                reduction /= std::max(1.0f, transientShaper.process(
                    input, ch, p.fetTransient.load(std::memory_order_relaxed)));
        }
        const float minimumReduction = ratioIndex == 4 ? -1.3f : -1.0f;
        reduction = std::clamp(reduction, minimumReduction, 60.0f);
        {
            // The loop's tail population has no counterpart in this arm. When
            // the ratio leaves 4-20 with some of it held, this arm's slow
            // population takes it over and releases it; dropped, the gain
            // would step by that many dB, and kept, it would come back stale.
            if (d.slowTailGrDb != 0.0)
            {
                d.slowGrDb += fetTailAppliedNow(d);
                d.slowTailGrDb = 0.0;
            }
            // The installed unit charges most of its reduction in the FET cell,
            // then adds a measured 2.2--3.4 dB programme-memory population with
            // a roughly 0.9 s time constant. Keeping both populations in dB
            // makes their settled sum equal the calibrated static law.
            const float basePositiveReduction = std::max(0.0f, reduction);
            const float allOverloadMemory = ratioIndex == 4
                ? fetAllOverloadMemoryDb(basePositiveReduction) : 0.0f;
            reduction = std::min(60.0f, reduction + allOverloadMemory);
            const float positiveReduction = std::max(0.0f, reduction);
            const float attackPosition = fetAttackPosition(
                p.fetAttack.load(std::memory_order_relaxed));
            const float attackPosition2 = attackPosition * attackPosition;
            const float ratioSlowOffset = ratioIndex == 0 ? 0.80f
                : ratioIndex == 1 ? 0.0f
                : ratioIndex == 2 ? -0.65f
                : ratioIndex == 3 ? -1.20f : 1.90f;
            const float slowTarget = basePositiveReduction > 0.0f
                ? std::min(basePositiveReduction,
                    fetDynamics.slowTargetBaseDb
                        + fetDynamics.slowTargetSlope * basePositiveReduction
                        + ratioSlowOffset
                        + 0.8f * attackPosition2 * attackPosition2)
                : 0.0f;
            const float fastTarget
                = reduction - slowTarget - allOverloadMemory;
            // At the three deep 4:1 drive points, the installed unit's 40--60
            // ms plateau exceeds ours by 0.567 / 0.545 / 0.684 dB while the
            // settled reduction already matches. That nearly constant 0.599 dB
            // is a third population, not more total reduction: split it out of
            // the existing slow target and give it the measured intermediate
            // charge time. All-buttons stays on its independently fitted arm.
            constexpr float intermediateTargetCeilingDb = 0.5985f;
            // The -42 dBFS row (4.2865 dB settled reduction) already has the
            // reference shape without this population; the -36 dBFS row
            // (9.1406 dB) benefits from the full split. Interpolate between the
            // two measured anchors so the cell engages with reduction depth.
            const float intermediateDepth = std::clamp(
                (positiveReduction - 4.2865f) / (9.1406f - 4.2865f),
                0.0f, 1.0f);
            const float intermediateTarget = ratioIndex == 4
                ? allOverloadMemory
                : std::min(slowTarget,
                    intermediateTargetCeilingDb * intermediateDepth);
            const float longSlowTarget = ratioIndex == 4
                ? slowTarget : slowTarget - intermediateTarget;
            const float ratioAttackScale = fetRatioAttackScale(kFetBlackVoice, ratioIndex);
            const float baseFastAttackSeconds
                = (0.000047f - 0.000026f * attackPosition
                    - 0.000008f * attackPosition * attackPosition)
                    * ratioAttackScale;
            // The measured cell charges FASTER the harder it is driven -- the
            // ordinary sense for a feedback detector, and the opposite of what
            // this line used to say before the campaign measured it.
            //
            // `kFetDriveAttackScale` is a table, not a fit, and every entry is
            // one rendered comparison against the installed unit: source levels
            // -42 / -39 / -36 / -30 / -18 / -6 dBFS at input knob 0.8, which
            // arrive here as settled reductions of 4.29 / 6.66 / 9.14 / 14.11 /
            // 24.07 / 34.03 dB. Each entry was tuned to minimise the normalised
            // curve RMS against the reference's own attack trajectory over
            // 0.5-20 ms, which is the campaign's primary dynamics gate; the
            // fitted time constant is a diagnostic only, because above ~30 dB of
            // reduction it inverts sign against every other measure of speed.
            //
            // The abscissa is the SETTLED reduction, not the 40-60 ms plateau:
            // the programme-memory population is only ~5 % charged at 50 ms, so
            // the plateau reads 3-4 dB low and would put every anchor off axis.
            //
            // Both ends hold rather than extrapolate. The previous law was a
            // parabola fitted only to the three deep anchors, and its 0-14 dB
            // segment -- 2-10 dB of reduction, where most programme material
            // sits -- was pure extrapolation that nothing gated. Below the
            // first anchor the question is moot in any case: `slowTarget`
            // saturates at `positiveReduction` under 2.39 dB, so the fast
            // population is empty and this multiplier has no effect at all.
            // Above the last anchor the deepest reduction any measured row
            // reaches is All-buttons at 36.1 dB, 2 dB past it.
            //
            // Holding the ends is also the safety property. `reduction` is
            // clamped to 60 dB, and `advance()` turns a non-positive time into
            // std::max(1.0e-6f, time) -- a silent 1 us attack rather than a
            // failure. The old parabola crossed zero at 47.7 dB, only 11.7 dB
            // outside its clamp, so widening that clamp would have produced
            // exactly that regime with no assertion firing. A table of positive
            // values with held ends cannot, whatever argument it is given.
            const float driveAttackScale
                = fetDriveAttackScale(positiveReduction);
            const float fastAttackSeconds
                = baseFastAttackSeconds * driveAttackScale
                    * (6.5f + 5.7f * attackPosition
                        - 9.4f * attackPosition * attackPosition)
                    * fetAttackKnobScale(attackPosition, positiveReduction);
            const float allOverloadDepth = std::clamp(
                (basePositiveReduction - 40.50f) / (41.75f - 40.50f),
                0.0f, 1.0f);
            const float slowAttackSeconds = ratioIndex == 4
                ? 1.50f - 1.05f * allOverloadDepth
                : 0.95f + 0.55f * attackPosition2 * attackPosition2;
            const float intermediateAttackSeconds = ratioIndex == 4
                ? 5.0f : 0.010f;
            // Splitting a fixed target makes the practical 3--5 s "settled"
            // windows charge slightly farther than the old single population.
            // Keeping the remainder 8 % slower makes the two populations meet
            // the old trajectory again in that measured window while leaving
            // their asymptotic sum unchanged.
            const float longSlowAttackSeconds
                = ratioIndex != 4 && intermediateTarget > 0.0f
                    ? slowAttackSeconds * 1.08f : slowAttackSeconds;
            const float releasePosition = fetReleasePositionOf(p);
            const int programmeSilenceHoldSamples = std::max(
                1, static_cast<int>(std::lround(fetDynamics.programmeSilenceHoldSeconds * sr)));
            if (vintageProgrammeActive)
                d.programmeSilentSamples = 0;
            else
                d.programmeSilentSamples = std::min(
                    d.programmeSilentSamples + 1,
                    programmeSilenceHoldSamples + 1);
            vintageProgrammeSupported = vintageProgrammeActive
                || d.programmeSilentSamples <= programmeSilenceHoldSamples;
            const float recoveryLevelDrop = std::max(
                d.recoveryPreviousInstantLevel - vintageInstantLevel, 0.0f);
            d.recoveryPreviousInstantLevel = vintageInstantLevel;
            if (vintageProgrammeSupported)
            {
                if (!d.recoveryWasSupported)
                    d.recoveryMaximumLevelDrop = 0.0f;
                d.recoveryMaximumLevelDrop = std::max(
                    d.recoveryMaximumLevelDrop, recoveryLevelDrop);
                d.recoveryWasSupported = true;
                d.recoveryElapsedSeconds = 0.0f;
                d.recoveryGain = 1.0f;
            }
            else
            {
                if (d.recoveryWasSupported)
                {
                    d.recoveryWasSupported = false;
                    d.recoveryMaximumGrDb = d.programmeMaximumGrDb;
                    d.recoveryTerminalWeight = std::clamp(
                        (d.recoveryMaximumLevelDrop - 12.0f) / 8.0f,
                        0.0f, 1.0f);
                    d.recoveryAttackPosition = attackPosition;
                    d.recoveryReleasePosition = releasePosition;
                    d.recoveryRatioIndex = ratioIndex;
                    d.recoveryElapsedSeconds = 0.0f;
                }
                else
                    d.recoveryElapsedSeconds += 1.0f / sr;
                if (ratioIndex != d.recoveryRatioIndex
                    || std::abs(attackPosition - d.recoveryAttackPosition)
                        > 1.0e-4f
                    || std::abs(releasePosition - d.recoveryReleasePosition)
                        > 1.0e-4f)
                    d.recoveryMaximumGrDb = 0.0f;
                const float assistDb = fetPostBurstRecoveryAssistDb(
                    d.recoveryElapsedSeconds, d.recoveryMaximumGrDb,
                    d.recoveryRatioIndex, d.recoveryTerminalWeight,
                    d.recoveryAttackPosition, d.recoveryReleasePosition,
                    static_cast<float>(fs));
                d.recoveryGain = decibelsToGain(assistDb);
            }
            if (fetDynamics.stateLeakOn > 0.5f)
            {
                // Candidate C1: programme memory follows the compression
                // history, not the mere presence of signal.
                if (positiveReduction > fetDynamics.exposureGateDb)
                    d.programmeExposureSeconds = std::min(
                        fetDynamics.programmeExposureCapSeconds,
                        d.programmeExposureSeconds + 1.0f / sr);
                else
                    d.programmeExposureSeconds *= std::exp(
                        -1.0f / (fetDynamics.exposureDecaySeconds * sr));
                d.programmeMaximumGrDb = positiveReduction >= d.programmeMaximumGrDb
                    ? positiveReduction
                    : positiveReduction
                        + (d.programmeMaximumGrDb - positiveReduction)
                            * std::exp(-1.0f / (fetDynamics.maxGrDecaySeconds * sr));
            }
            else if (vintageProgrammeSupported)
            {
                d.programmeExposureSeconds = std::min(
                    fetDynamics.programmeExposureCapSeconds,
                    d.programmeExposureSeconds + 1.0f / sr);
                d.programmeMaximumGrDb = std::max(
                    d.programmeMaximumGrDb, positiveReduction);
            }
            const float exposureReleaseScale = fetReleaseExposureScale(
                std::max(d.programmeExposureSeconds, 0.01f),
                d.programmeMaximumGrDb, fetDynamics);
            const float ratioReleaseScale = fetRatioReleaseScale(kFetBlackVoice, ratioIndex);
            const float legacyFastReleaseSeconds
                = (fetDynamics.legacyFastRelease[0]
                    + fetDynamics.legacyFastRelease[1] * releasePosition
                    + fetDynamics.legacyFastRelease[2] * releasePosition
                        * releasePosition)
                    * ratioReleaseScale * exposureReleaseScale;
            const float mappedFastReleaseSeconds
                = fetProgrammeFastReleaseSeconds(releasePosition)
                    * ratioReleaseScale * exposureReleaseScale;
            const float fastReleaseSeconds = ratioIndex == 4
                ? 0.020f
                : legacyFastReleaseSeconds
                    + (mappedFastReleaseSeconds - legacyFastReleaseSeconds)
                        * fetProgrammeReleaseDepth(d.programmeMaximumGrDb);
            const float slowReleaseSeconds = ratioIndex == 4
                ? 0.020f
                : (fetDynamics.slowRelease[0]
                    + fetDynamics.slowRelease[1] * releasePosition
                    + fetDynamics.slowRelease[2] * releasePosition
                        * releasePosition)
                    * ratioReleaseScale * exposureReleaseScale;
            const auto advance = [sr](float state, float target,
                                      float attackSeconds,
                                      float releaseTimeSeconds) noexcept {
                float time = target > state ? attackSeconds : releaseTimeSeconds;
                // Let a quiescent All-buttons cell acquire its measured
                // small-signal expansion promptly; recovery from positive GR
                // still uses the selected release above.
                if (state <= 0.0f && target < 0.0f) time = 0.050f;
                const float coefficient = std::exp(
                    -1.0f / (std::max(1.0e-6f, time) * sr));
                return target + (state - target) * coefficient;
            };
            d.fastGrDb = advance(
                d.fastGrDb, fastTarget, fastAttackSeconds, fastReleaseSeconds);
            d.intermediateGrDb = advance(
                d.intermediateGrDb, intermediateTarget,
                intermediateAttackSeconds, slowReleaseSeconds);
            const float slowTime = longSlowTarget > d.slowGrDb
                ? longSlowAttackSeconds : slowReleaseSeconds;
            const float slowCoefficient = std::exp(
                -1.0f / (std::max(1.0e-6f, slowTime) * sr));
            d.slowGrDb = longSlowTarget
                + (d.slowGrDb - longSlowTarget) * slowCoefficient;
            d.envelope = decibelsToGain(
                -(d.fastGrDb + d.intermediateGrDb + d.slowGrDb));
            if (!vintageProgrammeActive
                && d.programmeSilentSamples > programmeSilenceHoldSamples
                && d.fastGrDb + d.intermediateGrDb + d.slowGrDb < 0.05f)
            {
                d.programmeExposureSeconds = 0.0f;
                d.programmeMaximumGrDb = 0.0f;
            }
        }
        const float maximumEnvelope = ratioIndex == 4
            ? 1.17f : 1.0f;
        d.envelope = std::clamp(d.envelope, 0.001f, maximumEnvelope);
        if (!std::isfinite(d.envelope)) d.envelope = 1.0f;
        if (ratioIndex == 0)
        {
            const float currentBaseReduction = std::max(
                0.0f, -gainToDecibels(d.envelope));
            constexpr float kneeHoldSeconds = 0.030f;
            if (currentBaseReduction >= d.kneeBaseReductionDb)
            {
                d.kneeBaseReductionDb = currentBaseReduction;
                d.kneeBaseHoldSeconds = kneeHoldSeconds;
            }
            else if (d.kneeBaseHoldSeconds > 0.0f)
                d.kneeBaseHoldSeconds = std::max(
                    0.0f, d.kneeBaseHoldSeconds - 1.0f / sr);
            else
            {
                const float release = std::exp(-1.0f / (0.250f * sr));
                d.kneeBaseReductionDb = currentBaseReduction
                    + (d.kneeBaseReductionDb - currentBaseReduction)
                        * release;
            }
            if (kneeHandoff >= 1.0f)
            {
                // Once the raw detector reaches the accepted-law join this
                // auxiliary cell is outside its measured domain. Clear it
                // immediately: releasing its earlier onset correction into a
                // deep attack would alter the established dynamics curve.
                d.kneeCorrectionDb = 0.0f;
                d.kneeGain = 1.0f;
            }
            else
            {
                // A retained deep envelope can pass back through the shallow
                // range after the input has already fallen far below the
                // knee. It is release memory, not a new onset error. Require
                // the same live-programme support used by the envelope's
                // exposure cell; its 2 ms hold spans carrier zero crossings
                // but expires long before the 250 ms raw-peak release can
                // manufacture a false +0.85 dB post-burst gain hump.
                const float rawTargetCorrectionDb = vintageProgrammeSupported
                    ? (d.kneeBaseReductionDb - kneeTargetReduction)
                        * (1.0f - kneeHandoff)
                    : 0.0f;
                // The pre-change onset miss tops out at 0.724 dB. Keep this
                // cell inside that shallow domain even while the main
                // programme envelope is releasing from tens of dB of GR;
                // otherwise it mistakes retained deep reduction for a knee
                // error and cancels it on the auxiliary 100 ms time constant.
                constexpr float maximumCorrectionDb = 0.85f;
                const float targetCorrectionDb = std::clamp(
                    rawTargetCorrectionDb,
                    -maximumCorrectionDb, maximumCorrectionDb);
                constexpr float timeSeconds = 0.100f;
                const float coefficient = std::exp(
                    -1.0f / (timeSeconds * sr));
                d.kneeCorrectionDb = targetCorrectionDb
                    + (d.kneeCorrectionDb - targetCorrectionDb) * coefficient;
                d.kneeGain = decibelsToGain(d.kneeCorrectionDb);
            }
        }
        else
        {
            d.kneeBaseReductionDb = 0.0f;
            d.kneeCorrectionDb = 0.0f;
            d.kneeGain = 1.0f;
        }
        float sagGain = 1.0f;
        if (ratioIndex == 4
            && p.fetCurve.load(std::memory_order_relaxed) != 0)
        {
            const float gr = -gainToDecibels(d.envelope + 0.001f);
            const int limit = static_cast<int>(sr * 0.1f);
            d.sagCounter = gr > 15.0f ? std::min(d.sagCounter + 1, limit + 1) : std::max(0, d.sagCounter - 1);
            const float targetSag = d.sagCounter > limit ? 0.75f : 0.0f;
            const float attackSag = std::exp(-1.0f / (0.05f * sr)), releaseSag = std::exp(-1.0f / (0.3f * sr));
            const float coeffSag = targetSag > d.voltageSag ? attackSag : releaseSag;
            d.voltageSag += (1.0f - coeffSag) * (targetSag - d.voltageSag);
            sagGain = decibelsToGain(-d.voltageSag);
        }
        float out = saturated;
        out *= sagGain;
        out *= d.kneeGain * d.recoveryGain;
        out = (fetConvolutionUnity ? out : fetConvolution.processSample(out, ch)) * fetHardwareGain;
        if (fetDynamics.lfHpOn > 0.5f)
        {
            const auto& c = fetLfHighPass;
            const double x = out;
            const double y = c[0] * x + d.lfHpZ1;
            d.lfHpZ1 = c[1] * x - c[3] * y + d.lfHpZ2;
            d.lfHpZ2 = c[2] * x - c[4] * y;
            out = static_cast<float>(y);
        }
        else
        {
            const float hpfAlpha = 1.0f - std::exp(-2.0f * kDuskPi * 5.0f / sr);
            d.subBassHpState += hpfAlpha * (out - d.subBassHpState);
            out -= d.subBassHpState;
        }
        const float outputValue = p.fetOutput.load(std::memory_order_relaxed);
        const float outputGain = memoized(fetOutputGainMemo[0], outputValue, [](float v) noexcept {
            return decibelsToGain(fetOutputGainDb(kFetBlackVoice, v)); });
        // The installed model is linear throughout the measured steady range,
        // then bends startup peaks toward a ~7.1 linear ceiling. The previous
        // hard +/-2 guard flattened both the level sweep and its harmonics.
        // The atan branch deliberately starts at slope span*(2/pi)/scale
        // (~0.537), not 1.0: knee/span/scale are one joint fit to the measured
        // reference startup-peak surface, and forcing C1 continuity at the
        // knee reshapes that fitted curve. Do not smooth the derivative kink
        // without re-fitting all three constants against fresh captures.
        const float drivenOutput = out * outputGain;
        constexpr float ceilingKnee = 1.97879118f;
        constexpr float ceilingSpan = 5.16417142f;
        constexpr float ceilingScale = 6.11998526f;
        const float magnitude = std::abs(drivenOutput);
        if (magnitude <= ceilingKnee) return drivenOutput;
        const float limited = ceilingKnee + ceilingSpan * (2.0f / kDuskPi)
            * std::atan((magnitude - ceilingKnee) / ceilingScale);
        return std::copysign(limited, drivenOutput);
    }

    // The VCA's static law for one detector power: RMS level in dB, the
    // measured detector correction, the knee, the gain. The logarithm and the
    // exponential are MultiCompFastMath's, so the four-lane block form below
    // (vcaLawGains) gives the same value for every power.
    static float vcaLawRawDb(float power) noexcept
    {
        // 10 log10 of the power, floored at -180 dB: 20 log10 of the RMS level.
        return kTenLog10Of2 * fastmath::log2(std::max(power, 1.0e-18f));
    }
    static float vcaLawExponent(float rawDetectorDb, float thresholdDb, float slope) noexcept
    {
        // The calibration campaign specifies sine levels in peak dBFS, while
        // the RMS detector is 3.0103 dB lower. Select the measured correction
        // in the campaign's domain, then apply it to the detector's dB value.
        const float detectorDb = rawDetectorDb
            + vcaLaw::detectorCorrectionDb(rawDetectorDb + 3.0103f);
        const float over = detectorDb - thresholdDb;
        const float reduction = over > 0.0f ? std::min(over * (1.0f - slope), 60.0f) : 0.0f;
        return (-reduction) * kLog2Of10Over20;   // the gain is 2 to this power
    }
    static float vcaLawGain(float power, float thresholdDb, float slope) noexcept
    {
        return fastmath::exp2(vcaLawExponent(vcaLawRawDb(power), thresholdDb, slope));
    }
    static void vcaLawGains(const float* power, float* gain, int n, float thresholdDb, float slope) noexcept
    {
        int k = 0;
        for (; k + 4 <= n; k += 4)
        {
            float floored[4], log2Power[4], exponent[4];
            for (int lane = 0; lane < 4; ++lane) floored[lane] = std::max(power[k + lane], 1.0e-18f);
            fastmath::log2x4(floored, log2Power);
            vcaLawExponents4(log2Power, thresholdDb, slope, exponent);
            fastmath::exp2x4(exponent, gain + k);
        }
        for (; k < n; ++k) gain[k] = vcaLawGain(power[k], thresholdDb, slope);
    }
    static constexpr float kTenLog10Of2 = 3.01029996f, kTwentyLog10Of2 = 6.02059991f, kLog2Of10Over20 = 0.166096405f;

    // vcaLawExponent(kTenLog10Of2 * log2Power[lane], ...) for four lanes, lane by
    // lane the same operations (the correction's segment rows are read per lane).
    static void vcaLawExponents4(const float* log2Power, float thresholdDb, float slope, float* exponent) noexcept
    {
#if defined(__aarch64__)
        const float32x4_t raw = vmulq_f32(vdupq_n_f32(kTenLog10Of2), vld1q_f32(log2Power));
        const float32x4_t level = vaddq_f32(raw, vdupq_n_f32(3.0103f));
        float levels[4];
        vst1q_f32(levels, level);
        // Rows for lanes inside the table; a lane outside (no correction below
        // the table, the extrapolated secant above it, non-finite) is redone below.
        float aLevel[4], aCorrection[4], bLevel[4], bCorrection[4];
        bool inside[4];
        for (int lane = 0; lane < 4; ++lane)
        {
            const float x = levels[lane];
            inside[lane] = x > vcaLaw::kDetectorCalibration.front().levelDb
                && x <= vcaLaw::kHotDetectorExtension.back().levelDb;   // false for NaN
            const int row = inside[lane] ? static_cast<int>(std::ceil(x)) - vcaLaw::kDetectorSegmentLowest : 0;
            const auto& segment = vcaLaw::kDetectorSegments[static_cast<size_t>(row)];
            aLevel[lane] = segment.aLevel; aCorrection[lane] = segment.aCorrection;
            bLevel[lane] = segment.bLevel; bCorrection[lane] = segment.bCorrection;
        }
        const float32x4_t a = vld1q_f32(aLevel), ac = vld1q_f32(aCorrection);
        const float32x4_t t = vdivq_f32(vsubq_f32(level, a), vsubq_f32(vld1q_f32(bLevel), a));
        float32x4_t correction = vaddq_f32(ac, vmulq_f32(vsubq_f32(vld1q_f32(bCorrection), ac), t));
        float corrections[4];
        vst1q_f32(corrections, correction);
        for (int lane = 0; lane < 4; ++lane)
            if (!inside[lane]) corrections[lane] = vcaLaw::detectorCorrectionDb(levels[lane]);
        correction = vld1q_f32(corrections);
        const float32x4_t over = vsubq_f32(vaddq_f32(raw, correction), vdupq_n_f32(thresholdDb));
        const float32x4_t reduction = vbslq_f32(vcgtq_f32(over, vdupq_n_f32(0.0f)),
            vminq_f32(vmulq_f32(over, vdupq_n_f32(1.0f - slope)), vdupq_n_f32(60.0f)), vdupq_n_f32(0.0f));
        vst1q_f32(exponent, vmulq_f32(vnegq_f32(reduction), vdupq_n_f32(kLog2Of10Over20)));
#else
        for (int lane = 0; lane < 4; ++lane)
            exponent[lane] = vcaLawExponent(kTenLog10Of2 * log2Power[lane], thresholdDb, slope);
#endif
    }

    // THRESHOLD reads in dB and the measured onset for a sine sits 0.26 dB
    // above the knob text (knee fit -26.742 dBFS for '-27.0'); the RMS of a
    // full-scale sine is 3.01 dB below its peak, and our own fixed-tau onset
    // read 0.03 dB low, hence +0.29 on top of the RMS-vs-peak offset.
    static float vcaThresholdDb(const MultiCompParameterState& p) noexcept
    {
        return p.vcaThreshold.load(std::memory_order_relaxed) - 3.0103f + 0.29f;
    }

    // COMPRESSION is a knob position; the applied ratio comes from the
    // measured law (INF at the stop). Hard knee to within the measured
    // 0.2 dB soft region.
    float vcaSlopeFor(const MultiCompParameterState& p) noexcept
    {
        const float position = p.vcaRatio.load(std::memory_order_relaxed);
        if (!(position == vcaSlopePosition))
        {
            vcaSlopePosition = position;
            vcaSlope = vcaLaw::compressSlope(position);
        }
        return vcaSlope;
    }

    // The VCA detector's host-rate step: one sample into the RMS integrator
    // and the power history.
    void pushVcaDetector(VCAState& d, float sidechain) noexcept
    {
        const float rmsCoeff = vcaRmsCoeff;
        // The state climbs at most vcaDetectorRise per sample from its
        // floor; energy arriving faster is discarded (MultiCompVcaLaw.hpp,
        // DETECTOR ONSET).
        const float previous = std::max(d.rms, vcaDetectorFloorPower);
        d.rms = std::min(previous * rmsCoeff + sidechain * sidechain * (1.0f - rmsCoeff),
                         previous * vcaDetectorRise);
        // A finite but absurd input (|x| > 1.8e19) overflows the power
        // state to infinity within one block; the aligned-history read
        // below would then interpolate inf - inf = NaN and the gain cell
        // would sit at unity with finite output, so the shared non-finite
        // latch never fires. Drop such a state before it enters the
        // history; the integrator recharges from the floor. Byte-identical
        // for every finite state.
        if (!std::isfinite(d.rms)) d.rms = 0.0f;
        d.rmsWrite = (d.rmsWrite + 1u) & 63u;
        d.rmsHistory[d.rmsWrite] = d.rms;
    }

    // One oversampled phase of the VCA: the detector advances on the native
    // phase; returns the cell gain for this phase (also kept in d.envelope).
    float advanceVcaEnvelope(VCAState& d, float sidechain, float thresholdDb, float slope) noexcept
    {
        // VCA compressor model (reference campaign 2026-09-01). The unit is a true-RMS
        // detector driving the gain directly: one first-order power integrator,
        // no separate attack/release envelope. Fitting a single time constant to
        // nine reference step responses (three depths x three ratios) gave
        // 31 ms on attack and 35 ms on release with 0.22 / 0.06 dB RMSE, and the
        // equal-RMS crest triplet read identical GR for sine, gated bursts and
        // noise -- an RMS detector with no crest sensitivity. The attack/release,
        // Over Easy and detector-mode parameters stay host-visible for state
        // compatibility but the reference has no such controls; they are inert.
        // The DSP supplies the selected, filtered, linked native sample on
        // every phase. Integrate power once: interpolating the detector
        // waveform first changes its high-frequency RMS. The audio reaches
        // this gain cell later, through the upsampling FIR. Applying today's
        // power to that delayed audio adds unintended lookahead: the measured
        // 50 Hz burst reads 7.57 dB at 1x but 7.78 dB at 4x (reference 7.60 dB).
        if (d.phase == 0) pushVcaDetector(d, sidechain);
        // The wide 127-tap DuskOversampler's upsampling group delays are 63
        // samples at 2x, and 2*63 + 7 at 4x (31.5 / 33.25 host samples; the
        // classic 47-tap stage gave 11.5 / 13.25). Read that same time in the
        // native power history (the 64-entry ring covers it);
        // interpolate power across phases before converting it to gain.
        // (Interpolating gains between whole-sample law points instead moved
        // an onset table cell 0.20 dB toward the bar, 2026-10-19.)
        // The ring stays in host samples when oversampling is automated.
        const float upDelay = osFactor == 4 ? 33.25f : osFactor == 2 ? 31.5f : 0.0f;
        // Never negative for a phase inside the current factor; the clamp only
        // guards the unsigned conversion against a stale phase.
        const float delay = std::max(upDelay - static_cast<float>(d.phase) / osFactor, 0.0f);
        const auto whole = static_cast<unsigned>(delay);
        const float fraction = delay - static_cast<float>(whole);
        const float recent = d.rmsHistory[(d.rmsWrite - whole) & 63u];
        const float older = d.rmsHistory[(d.rmsWrite - whole - 1u) & 63u];
        const float alignedRms = recent + (older - recent) * fraction;
        d.phase = (d.phase + 1) % osFactor;
        // The reference adds no even-order or fifth harmonic at all (H2/H4 at
        // the numerical floor across the 100-cell static grid) and its H3
        // (~-69 dBc) is what this detector's own 2 kHz ripple imposes on a
        // 1 kHz carrier, so the gain is applied clean.
        d.envelope = vcaLawGain(alignedRms, thresholdDb, slope);
        return d.envelope;
    }

    float processVCA(float input, int ch, float sidechain,
                     const MultiCompParameterState& p, bool /*external*/) noexcept
    {
        const float envelope = advanceVcaEnvelope(vca[ch], sidechain, vcaThresholdDb(p), vcaSlopeFor(p));
        // The reference's output gain remains linear at +20 dB: a -0.1 dBFS
        // unity-ratio sine peaks at 9.888659 (2026-09-07 capture). A +/-2
        // guard here clips ordinary makeup gain and loses 11.26 dB RMS on
        // that row. Preserve the floating-point headroom of the measured unit.
        return input * envelope * vcaOutputGainFor(p.vcaOutput.load(std::memory_order_relaxed));
    }

    float busFeedbackMagnitude(BusState& d, int ch) noexcept
    {
        // Coupling and adjustable HP are already applied when storing feedback.
        // Optional extension shelves retain their existing feedback placement.
        float detector = d.compressed;
        const size_t channel = static_cast<size_t>(std::clamp(ch, 0, 1));
        if (std::abs(busSidechainLowGain) > 0.001f)
            detector = busSidechainLowShelf[channel].process(detector);
        if (std::abs(busSidechainHighGain) > 0.001f)
            detector = busSidechainHighShelf[channel].process(detector);
        return std::abs(detector);
    }

    float busDrive(const MultiCompParameterState& p) noexcept
    {
        const int position = std::clamp(p.busHeadroom.load(std::memory_order_relaxed), 0, 6);
        if (position != busDrivePosition)
        {
            busDrivePosition = position;
            busDriveValue = busLaw::headroomDrive(position);
            busCeilingScale = busCeilingReference / busDriveValue;
        }
        return busDriveValue;
    }

    // BUS output ceiling, after makeup and inside headroom compensation. All
    // values are measured from the native saturation matrix; fits and evidence
    // are in capture set bus-ceiling-analysis-20260910 (a1, a3, s3b).
    //  - The clip is HARD, not a soft curve: clipped H3..H19 match an ideal
    //    hard clip to +/-0.05 dB and +/-0.35 degrees. Its level is
    //    3.2867 +/- 0.0006 internal, the same at HR4, HR16 and HR28.
    //  - Below it the native adds a weak x - a*x^7 - b*x^6, x = u / 3.3:
    //    H3 grows 6.0 dB/dB, the pure x^7 signature. At x = 0.868 the native
    //    H2/H3/H5/H7 are -88.4/-76.7/-86.2/-103.1 dBc.
    //  - The native clips at 4x. This clips at the wet-path rate, and that
    //    rate, not the law, sets the residual of the saturation peak gate.
    static constexpr float busCeilingReference = 3.3f;   // normalises x
    static constexpr float busCeilingClip = 3.2867f / busCeilingReference;
    static constexpr float busCeilingSeventh = 1.03907e-3f;   // a
    static constexpr float busCeilingSixth = 1.63593e-4f;     // b
    // p(x) is monotonic for |x| <= 1.1 (slope >= 0.9855) and folds back near
    // |x| = 2.27. p(+/-1.1) = +1.0977 / -1.0983 lies beyond the clip, so this
    // guard keeps p on its monotonic branch without ever shaping the output.
    static constexpr float busCeilingGuard = 1.1f;

    static float busOutputCeiling(float normalised) noexcept
    {
        const float x = std::clamp(normalised, -busCeilingGuard, busCeilingGuard);
        const float x2 = x * x, x6 = x2 * x2 * x2;
        return std::clamp(x - x6 * (busCeilingSeventh * x + busCeilingSixth),
                          -busCeilingClip, busCeilingClip);
    }

    static float busDetectorLevel(float rectified) noexcept
    {
        return rectified * 0.70710678f;
    }

    // Native drive ceiling. 10:1 near-zero-leak sweeps to +210 dBFS plateau at
    // 128.11 / 126.67 / 129.28 dB for 0.1 ms/0.1 s, 0.3 ms/0.1 s, 0.1 ms/1.2 s:
    // one ceiling with a soft approach reproduces all three. The old 120 dB bound
    // was a compensating calibration and is gone.
    static constexpr float busDriveCeiling = 128.86116f, busDriveCeilingWidth = 0.817485452f;
    // The detector's gain cell attenuates by this multiple of the control, so
    // loop gain rises with law slope. Onset-fitted attack RCs then agree with the
    // ratio-independent RCs measured on saturated (flat-drive) sidechains.
    static constexpr float busDetectorExponent = 1.02129996f;

    static busLaw::Drive busCappedDrive(float detectorDb, float threshold, int ratio) noexcept
    {
        const busLaw::Drive d = busLaw::drive(detectorDb, threshold, ratio);
        // More than 16 widths below the ceiling the smooth minimum is the drive to < 1e-7 dB.
        if (d.value < busDriveCeiling - 16.0f * busDriveCeilingWidth) return d;
        const float lo = std::min(d.value, busDriveCeiling), hi = std::max(d.value, busDriveCeiling);
        const float value = lo - busDriveCeilingWidth * std::log1p(std::exp((lo - hi) / busDriveCeilingWidth));
        const float gate = 1.0f / (1.0f + std::exp((d.value - busDriveCeiling) / busDriveCeilingWidth));
        return {value, d.slope * gate};
    }

    // The static law is specified on the detector level the audio gain alone
    // would give; the detector actually sees busDetectorExponent * control, so
    // solve R = law(y + (k - 1) R). Near-zero-leak steady states stay exactly on
    // the fitted law; only the loop gain changes. Newton from the k = 1 value
    // converges in two to four steps (at most four) over the whole drive range.
    static float busFeedbackDrive(float level, const MultiCompParameterState& p) noexcept
    {
        const float y = gainToDecibels(std::max(level, 1.0e-9f));
        const float threshold = p.busThreshold.load(std::memory_order_relaxed);
        const int ratio = p.busRatio.load(std::memory_order_relaxed);
        constexpr float excess = busDetectorExponent - 1.0f;
        float r = busCappedDrive(y, threshold, ratio).value;
        if (r <= 0.0f) return 0.0f;          // below the knee: no drive at any exponent
        for (int i = 0; i < 4; ++i)
        {
            const busLaw::Drive f = busCappedDrive(y + excess * r, threshold, ratio);
            const float step = (r - f.value) / std::max(1.0f - excess * f.slope, 0.05f);
            r -= step;
            if (std::abs(step) <= 1.0e-5f * r) break;
        }
        return std::max(r, 0.0f);
    }

    // Release-dependent part of the charge-path diode drop, driven by the leak
    // current (C + bias) / RC. Zero at the 0.1 s release by construction; tends
    // to -gamma * ln(RC / RC0) under compression and fades near idle. Identified
    // on crest-free quadrature sidechains at four releases (pass 2, 2026-09-10).
    static float busChargeOffset(float control, float bias, float release) noexcept
    {
        constexpr float rc0 = 0.08022117f, gamma = 0.377103031f, leakScale = 2.3818202f;
        const float leak = std::max(0.0f, control + bias) / release;
        const float leak0 = std::max(0.0f, control + 0.33f * rc0) / rc0;
        return 0.33f * rc0 + gamma * (std::log1p(leak / leakScale) - std::log1p(leak0 / leakScale));
    }

    float busChargeCoefficient(float attack, float sr) noexcept
    {
        if (attack != busChargeMemoAttack || sr != busChargeMemoRate)
        {
            busChargeMemoAttack = attack;
            busChargeMemoRate = sr;
            busChargeMemo = 1.0f - std::exp(-1.0f / std::max(1.0f, attack * sr));
        }
        return busChargeMemo;
    }

    float busDischargeCoefficient(float release, float sr) noexcept
    {
        if (release != busDischargeMemoRelease || sr != busDischargeMemoRate)
        {
            busDischargeMemoRelease = release;
            busDischargeMemoRate = sr;
            busDischargeMemo = std::exp(-1.0f / std::max(1.0f, release * sr));
        }
        return busDischargeMemo;
    }

    // advanceBusEnvelope's reset test reads only whether 10^(-control / 20) is
    // finite: smoothBusAudioGain overwrites the envelope it would give. At or
    // above -700 dB that is at most 1e35, finite with any libm; lower controls
    // and NaN evaluate it as before (+inf passes here, as 10^-inf = 0 is finite,
    // and the caller's own isfinite test of the control resets it).
    static bool busControlGainFinite(float controlDb) noexcept
    {
        return controlDb >= -700.0f || std::isfinite(decibelsToGain(-controlDb));
    }

    void smoothBusAudioGain(BusState& d) noexcept
    {
        d.smoothControlDb += busAudioControlSmoothingStep
            * (d.fixedControlDb - d.smoothControlDb);
        d.smoothControlDb2 += busAudioControlSmoothingStep2
            * (d.smoothControlDb - d.smoothControlDb2);
        d.envelope = decibelsToGain(-d.smoothControlDb2);
    }

    void advanceBusEnvelope(BusState& d, float level,
                            const MultiCompParameterState& p,
                            float sr) noexcept
    {
        const int phase = d.detectorPhase;
        if (++d.detectorPhase >= osFactor) d.detectorPhase = 0;
        {
            // Native reference phase/detune captures identify a host-rate detector.
            // The upsampling FIR delays are 31.5 / 33.25 host samples at
            // 2x / 4x with the 127-tap wide stage (the fractional parts equal
            // the classic 11.5 / 13.25, so the phase choice is unchanged).
            // Feedback holds the preceding phase, so phases 0 / 2 read an
            // integer-aligned input sample. See the BUS frequency
            // report dated 2026-09-09 for the measurements and fault checks.
            const int nativePhase = osFactor == 4 ? 2 : 0;
            if (phase != nativePhase) return;
            sr /= static_cast<float>(osFactor);
        }
        const float reduction = busFeedbackDrive(level, p);
        const float releases[5] = {100.0f, 300.0f, 600.0f, 1200.0f, -1.0f};
        // Measured control RC constants; the feedback loop sets the audible
        // attack, and its discharge branch remains active while charging.
        // Reference traces and independent levels/phase holdouts are recorded
        // in the 2026-09-08 BUS attack measurement report.
        const int attackIndex = std::clamp(p.busAttack.load(std::memory_order_relaxed), 0, 5);
        const float internalAttacks[] = {0.000527820201f, 0.00145334529f, 0.00315266824f, 0.01306096f, 0.0385882184f, 0.124117881f};
        // Recalibrate the 2:1 control response with its measured knee. The old
        // static bias masked excessive early attack; fresh 997 Hz bursts at
        // independent levels/threshold validate these effective RC constants.
        // They describe this model, not recovered UA circuit values.
        const float twoToOneAttacks[] = {0.000541862973f, 0.00146395736f, 0.00314006675f, 0.0125738038f, 0.0364539362f, 0.117983066f};
        const float attack = (p.busRatio.load(std::memory_order_relaxed) == 0
                ? twoToOneAttacks[attackIndex] : internalAttacks[attackIndex]);
        float release = releases[std::clamp(
            p.busRelease.load(std::memory_order_relaxed), 0, 4)] * 0.001f;
        if (release > 0.0f)
        {
            const float measured[] = {0.08022117f, 0.11729294f, 0.22496914f, 0.41338287f};
            release = measured[std::clamp(p.busRelease.load(std::memory_order_relaxed), 0, 3)];
        }
        if (p.busRelease.load(std::memory_order_relaxed) == 4)
        {
            // Both reservoirs stay coupled through attack and recovery. Switching
            // the slow reservoir between charge and decay distorts early release.
            const float current = d.fixedControlDb;
            const float slow = d.autoSlowDb;
            const float charge = busChargeCoefficient(attack, sr);
            d.fixedControlDb = float(busAutoStep[0]*current + busAutoStep[1]*slow + busAutoStep[4])
                + std::max(0.0f, reduction - std::max(0.0f, current)) * charge;
            d.autoSlowDb = float(busAutoStep[2]*current + busAutoStep[3]*slow + busAutoStep[5]);
            // Test the reservoirs, not only the envelope: decibelsToGain(-inf)
            // is 0, which is finite, so an envelope-only guard would leave a
            // non-finite reservoir latched at digital silence. The fixed-release
            // branch below already uses this two-sided form.
            if (!busControlGainFinite(d.fixedControlDb) || !std::isfinite(d.fixedControlDb)
                || !std::isfinite(d.autoSlowDb))
            { d.envelope = 1; d.autoSlowDb = 0; d.fixedControlDb = 0; }
            d.detectorEnvelope = decibelsToGain(-busDetectorExponent * d.fixedControlDb);
            smoothBusAudioGain(d);
            return;
        }
        d.autoSlowDb = 0.0f;
        {
            // A charging diode and a continuously connected discharge path.
            // Mutually exclusive attack/release pauses discharge during each
            // carrier crest and over-compresses with slow attack/fast release.
            // Native fixed-release recoveries have a positive gain asymptote
            // proportional to the release RC, also measured below threshold.
            // This calibrated control bias is shared by all ratios. Rate/level
            // holdouts are in the 2026-09-09 BUS release measurement report.
            const float bias = 0.33f * release;
            const float currentReduction = busChargeOffset(d.fixedControlDb, bias, release) + d.fixedControlDb;
            const float discharge = busDischargeCoefficient(release, sr);
            const float charge = busChargeCoefficient(attack, sr);
            // A lower release setting can leave control below the new bias.
            // Let it settle through release; it must not trigger fast charge.
            d.fixedControlDb = d.fixedControlDb * discharge - bias * (1.0f - discharge)
                + std::max(0.0f, reduction - std::max(0.0f, currentReduction)) * charge;
        }
        if (!busControlGainFinite(d.fixedControlDb) || !std::isfinite(d.fixedControlDb))
        { d.envelope = 1.0f; d.fixedControlDb = 0.0f; }
        d.detectorEnvelope = decibelsToGain(-busDetectorExponent * d.fixedControlDb);
        smoothBusAudioGain(d);
    }

    float renderBusOutput(float input, int ch,
                          const MultiCompParameterState& p, float mix) noexcept
    {
        auto& d = bus[ch];
        const float drive = busDrive(p);
        const float transformed = inputTransformerBus[ch].processSample(input * drive, ch);
        float out = transformed * d.envelope;
        // Independent asymmetric-bass captures identify coupling in feedback.
        // Taking the entire output-transformer signal would also add its HF
        // coloration and fails the 4x phase/detune guard. Keep only the measured
        // low-frequency coupling here; see the BUS programme report, 2026-09-09.
        // Advance it during external detection too, for a continuous handoff.
        // The adjustable HP precedes the detector gain cell. Filtering its
        // modulated output instead creates extra harmonics and over-compresses
        // a 40 Hz tone by 3.70 dB with the 80 Hz filter. Audio stays unfiltered.
        const float feedbackInput = (busSidechainHighPassFrequency >= 1.0f
            ? busSidechainHighPass[static_cast<size_t>(ch)].process(transformed) : transformed)
            * d.detectorEnvelope;
        d.compressed = busFeedbackCoupling[static_cast<size_t>(ch)].process(feedbackInput);
        out = outputTransformerBus[ch].processSample(out, ch);
        const float makeupDb = p.busMakeup.load(std::memory_order_relaxed);
        if (makeupDb != busMakeupMemoDb)
        {
            busMakeupMemoDb = makeupDb;
            busMakeupMemoGain = decibelsToGain(makeupDb);
        }
        out = busConvolution.processSample(out, ch) * busHardwareGain * busMakeupMemoGain;
        out = busOutputCeiling(out * (1.0f / busCeilingReference)) * busCeilingScale;
        // Native ceiling is inside headroom compensation, after makeup.
        // HR4 permits ~13.1 linear output; HR28 clips near 0.83.
        // A fixed +/-2 output clamp cannot reproduce either endpoint.
        return out * mix + input * (1.0f - mix);
    }

    float processBus(float input, int ch, float sidechain,
                     const MultiCompParameterState& p, float mix,
                     bool external) noexcept
    {
        auto& d = bus[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float level = busDetectorLevel(
            external ? std::abs(d.externalCompressed) : busFeedbackMagnitude(d, ch));
        advanceBusEnvelope(d, level, p, sr);
        d.externalCompressed = sidechain * busDrive(p) * d.detectorEnvelope;
        return renderBusOutput(input, ch, p, mix);
    }

    float processDigital(float input, int ch, float sidechain, const MultiCompParameterState& p, float mix, bool /*external*/) noexcept
    {
        auto& d = digital[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float lookahead = std::clamp(p.digitalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f);
        auto& delayLine = digitalDelay[static_cast<size_t>(ch)];
        const int delay = std::min(static_cast<int>(std::round(lookahead * 0.001f * sr)), static_cast<int>(delayLine.size()) - 1);
        int& wp = digitalWrite[ch];
        float delayed = input;
        if (delay > 0)
        {
            const int rp = (wp - delay + static_cast<int>(delayLine.size())) % static_cast<int>(delayLine.size());
            delayed = delayLine[static_cast<size_t>(rp)];
        }
        delayLine[static_cast<size_t>(wp)] = input;
        wp = (wp + 1) % static_cast<int>(delayLine.size());
        const float detect = std::abs(sidechain);
        const float db = gainToDecibels(std::max(detect, 0.00001f));
        const float threshold = p.digitalThreshold.load(std::memory_order_relaxed), ratio = std::max(1.0f, p.digitalRatio.load(std::memory_order_relaxed)), knee = std::max(0.0f, p.digitalKnee.load(std::memory_order_relaxed));
        float reduction = 0.0f;
        if (knee > 0.0f && db > threshold - knee * 0.5f)
        {
            // The quadratic joins zero reduction with the full-ratio line,
            // matching both value and slope at the two knee boundaries.
            if (db < threshold + knee * 0.5f)
            {
                const float distance = db - threshold + knee * 0.5f;
                reduction = (1.0f - 1.0f / ratio) * distance * distance / (2.0f * knee);
            }
            else reduction = (db - threshold) * (1.0f - 1.0f / ratio);
        }
        else if (db > threshold) reduction = (db - threshold) * (1.0f - 1.0f / ratio);
        reduction = std::max(0.0f, reduction);
        float release = std::max(0.001f, p.digitalRelease.load(std::memory_order_relaxed) * 0.001f);
        if (p.digitalAdaptive.load(std::memory_order_relaxed))
        {
            // The routed detector already carries the stereo-link blend or
            // the channel's independent/M-S key. Derive programme timing from
            // that same signal, so a common key gets a common release even
            // after the main channels have had different crest histories.
            const float absolute = detect;
            const float peakCoeff = std::exp(-1.0f / (0.1f * sr));
            d.peak = absolute > d.peak ? absolute : peakCoeff * d.peak + (1.0f - peakCoeff) * absolute;
            const float rmsCoeff = std::exp(-1.0f / (0.3f * sr));
            d.rms = rmsCoeff * d.rms + (1.0f - rmsCoeff) * absolute * absolute;
            const float rms = std::sqrt(std::max(d.rms, 0.0f));
            d.crest = std::clamp(rms > 0.0001f ? d.peak / rms : 1.0f, 1.0f, 20.0f);
            const float multiplier = d.crest < 6.0f ? 1.0f + (6.0f - d.crest) / 5.0f
                                                     : 1.0f - std::min(1.0f, (d.crest - 6.0f) / 6.0f) * 0.67f;
            release *= multiplier;
        }
        const float target = decibelsToGain(-reduction), attack = std::max(0.00001f, p.digitalAttack.load(std::memory_order_relaxed) * 0.001f);
        const double c = std::exp(-1.0 / (static_cast<double>(target < d.envelope ? attack : release) * fs * osFactor));
        d.envelope = std::clamp(c * d.envelope + (1.0 - c) * target, 0.0001, 1.0);
        const float wet = std::clamp(delayed * static_cast<float>(d.envelope) * decibelsToGain(p.digitalOutput.load(std::memory_order_relaxed)), -2.0f, 2.0f);
        // The dry half of the local mix takes the same delayed sample as the wet
        // half; blending against the undelayed input combs by the lookahead time.
        return wet * mix + delayed * (1.0f - mix);
    }
};

} // namespace duskaudio
