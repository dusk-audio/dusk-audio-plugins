# Opto R11a: input-classifier audit (report only, no code changed)

This is a read-only audit of the MC-2 Opto audio path against the owner's
standing rule: nothing in the audio path may detect or classify the input to
change its behaviour. Line numbers refer to the R11a working tree.

- `MultiCompDSP.cpp` has no Opto classifier. Its max-link and mode routing are
  configuration, not signal classification.
- `MultiCompOptoOutputStage.hpp` and `MultiCompOptoStageOversampler.hpp`
  classify nothing.
- All classifier logic sits in `MultiCompModes.hpp` `processOpto` (about lines
  2260–2802) and in the EL estimator in `MultiCompOptoCell.hpp`.
- The gated terms sum into `meterCorrectionDb`, which is applied to the audio
  as a gain and subtracted in the GR meter.

## Live classifiers and estimators (9)

| # | Where | Detects | Drives |
|---|---|---|---|
| C1 | Modes ~2412–2419 | **sineLike**: peak² < 3 × 20 ms mean power (crest < 4.77 dB); **sineSup** = held for 100 ms | Enables the sine-only corrections T5–T11 and T13; blocks T3 and T7 |
| C2 | Modes ~2297–2331 | Zero-crossing frequency `toneMeasuredFrequencyHz` (**defaults to 1000 Hz**) | Frequency axis of T5, T11, T12 and T13 |
| C3 | Modes ~2302–2316 | **Event drop**: frequency in **[900, 1100] Hz**, previous cycle peak > −21.9 dBFS, drop > 3.5 dB | T1 (fast, up to about +0.45 dB) and T2 (slow, up to about +0.25 dB) |
| C4 | Modes ~2388–2409 | **periodicTone**: 2-tap sinusoid predictor using the cell's frequency estimate; error ≤ 1e-5 × power | Disables T3 and T7 |
| C5 | Modes ~2336–2345, 2420–2427 | **stationaryBroadband**: 1 s power deviation < 6.5%, not periodic, held for 500 ms | Enables T3; disables T7 |
| C6 | Modes ~2570–2666 | **Short-event segmenter**: loud > −30 dBFS; arm after 75 ms of silence; first event only (`shortEventConsumed` is never cleared); "quiet probe" test tied, per its own comment, to the charge fixture | T4 and the event age for T8 |
| C7 | Modes ~2647–2656 | Event crest < 1.8 (sine-like event); long event ≥ 100 ms (the flag is never cleared) | Arms T4 and T6 |
| C8 | Cell ~465–482 | EL slew/drive ratio → (f/1 kHz)², i.e. a continuous frequency estimate | Light × ratio^(κ/2); also feeds C4 and the settled correction T13 |
| C9 | Cell ~370–376, 540–549 | LF-floor energy ratio (band-passed over broadband detector power), continuous | Applied-gain LF floor (borderline) |

## Live correction terms they drive (15)

| # | Term | Magnitude | Exists for (test / require message) | Nodes match a test grid? |
|---|---|---|---|---|
| T1/T2 | Event drop, fast and slow | up to +0.45 / +0.25 dB | `testOptoRecoveryParity` (1 kHz pedestal-event) | **Yes: a 900–1100 Hz window around the fixture carrier** |
| T3 | Broadband static law | 0 to +0.83 dB | `testOptoBroadbandStaticLaw` | Yes: test levels (−24 held out) |
| T4 | Short-event charge | −0.002 to +2.59 dB | `testOptoShortEventCharge`, `…Release`, wide 2x/4x charge; also fires in the crest and burst-rate tests | Yes: exactly the 17/48/144/480-sample × −12/−8/−4/−0.25 dBFS charge grid |
| T5 | Detector weighting (frequency) | −0.46 to +0.44 dB | `testOptoDetectorFrequencyWeighting` | Yes: the same 31 frequencies at −24 dBFS / PR 70 |
| T6 | Long-event release | 0.54 to 0.96 dB | `testOptoReferenceOutputMemory` | Yes: exactly the 16 output-memory gaps |
| T7 | Programme correction | −0.28 to +0.10 dB | `testOptoDenseProgrammeParity` | Yes: PR 40/70/85/100 |
| T8 | Limit attack | up to +1.2 dB | `testOptoLimitDynamics` | Yes: PR 60, −24/−12/−3 dBFS |
| T9 | Onset, **with a PR 40 / Gain < 23.75 knob switch** | −0.25 to +0.30 dB | `testOptoMeasuredOnsets`; the switch keeps it off the knee and settled fixtures | **Yes: a knob-value switch** |
| T10 | Curve correction | −0.44 to +0.17 dB | `testOptoThresholdOnlyCurveCollapse` | Yes: PR 40/70/100 × the test's GR points |
| T11 | High-curve, PR ≥ 95, −6…−5 dBFS | cancels T12 there | none: no fixture falls inside its window | Unknown target |
| T12 | LF / high-drive settled power | 0.1 to 1.19 dB | `testOptoSettledFrequencyParity` | Yes: the 12 fixture frequencies (including 82.41 Hz) and fixture levels |
| T13 | ≤ 0.027 dB LF settled correction | ≤ +0.027 dB | one 82.41 Hz settled row | Yes |
| T14 | HF mid-PR | −0.94 dB | steady row 109 only (20 kHz / −18 dBFS / PR 85) | **Yes: a single row** |
| T15 | Limit mid-PR | +0.65 dB | steady row 114 only (82.41 Hz / −18 dBFS / PR 85 Limit) | **Yes: a single row** |

## Inert

- R10's recurrence estimator, harmonic arming and structural harmonics sit
  behind `kOptoToneSynthesisEnabled = false`. Their state is still computed
  every sample.
- The cell's reversal estimator (hysteresis 0) and its P element (strength 0)
  are inert.
- Dead code: `optoHarmonicRatios`, `optoColourReference*`, `optoThresholdDb`;
  `MultiCompOptoHarmonics.hpp`, `MultiCompOptoFinalHarmonics.hpp` and
  `MultiCompOptoLfDynamicHarmonics.hpp` are included nowhere.

## Side effects found by reading the code (unmeasured)

1. Low-frequency sines restart the short-event segmenter every half cycle
   (at 50 Hz up to about −5.5 dBFS). In Limit around PR 35–85 this gives bass
   up to about 1.2 dB of extra reduction, with a per-cycle notch at each zero
   crossing.
2. A sustained Limit signal never ends its event, so T8 settles to a permanent
   offset of about 0.1 dB.
3. The pedestal-event rows reach T4 differently by pedestal level: −33 dBFS
   goes quiet at its zero crossings, while −27 and −15 dBFS never do.

## Summary

The only elements that act identically on any signal are the cell's EL
frequency law (C8) and the LF-floor ratio (C9), and both are still estimators.
Everything else is gated by signal classification. The clearest cases that
exist purely to hit gate stimuli:
- the event-drop window (T1/T2);
- the two single-row corrections (T14, T15);
- the PR 40 / Gain knob switch (T9);
- the first-event-only arming with its quiet-probe test (T4).

Most other tables have their nodes placed exactly on test grids. Per the
standing rule, these are replaced with physical dynamics in R12 or later.
Nothing was changed in R11a.
