# Sunset Circuits — behavioral accuracy upgrade

This pass closes the largest gaps between the six mode descriptions and the
shipped signal paths. It is a behavioral emulation pass, not a claim of
component-level or measurement-matched reproduction. The coefficients are
calibrated from expected topology, response, stability, and musical behavior;
they have not yet been fitted to captures from individual hardware units.

## Mode changes

| Mode | Upgrade |
|---|---|
| Cosmos | The built-in dual chorus now includes pre-emphasis, two-pole input filtering, companding/expanding, clock quantization and jitter, mode-specific bandwidth, clock noise, and DC blocking. |
| Oracle | The filter has an independently voiced stable four-pole path with bounded self-oscillation. Filter-envelope-to-pulse-width, oscillator-B-to-filter, oscillator-B-to-pulse-width, and oscillator-B-to-oscillator-A routes are audio-rate where appropriate. Oscillator hard sync now resets the slave at the master edge. |
| Mono | Its four-pole path now has separate drive, feedback, stage saturation, output gain, and resonance-dependent input calibration instead of sharing the poly filter coefficients. |
| Modular | The filter offers generic early/open and late/bandwidth-limited ladder revisions. Oscillator 2 can phase-modulate oscillator 1, oscillator 1 retains its oscillator-2 route, and oscillator 3 can modulate cutoff at the internal audio rate. |
| Prism | All eight diagrams and the DSP read the same algorithm table. Carrier counts and routing classes are checked at compile time. Operator levels use a logarithmic control law, ratios/fine tune/feedback are stepped, and the per-operator envelopes use quantized digital rates and levels. The global default remains 2× oversampling. |
| Acid | A dedicated saw/square oscillator, nonlinear three-pole TPT filter, asymmetric diode transfer, two-rate accent contour, nonlinear VCA, and log-frequency tied slide replace the more generic building blocks. |

All analogue-mode filters share only the stable TPT integration primitive.
Feedback, drive, saturation, compensation, cutoff behavior, and output
calibration remain mode-specific.

## Parameter compatibility

The frozen parameter order is preserved. Five parameters were appended after
the existing index 222:

| Index | Symbol | Purpose |
|---:|---|---|
| 223 | `pmFenvPWM` | Oracle filter envelope to oscillator-A pulse width |
| 224 | `pmOscBFilt` | Oracle oscillator B to filter cutoff |
| 225 | `modFilterModel` | Modular early/late filter revision |
| 226 | `modOsc2Osc1` | Modular oscillator 2 to oscillator 1 |
| 227 | `modOsc3Filter` | Modular oscillator 3 to filter cutoff |

Old host sessions keep every previous automation index. Old user presets omit
the new symbols and therefore receive their neutral defaults. The generated DPF
table, core enum, plugin shell, and render harness assert the new count and final
index against one another.

## Automated evidence

`core/tests/accuracy_gate.py` renders null comparisons for every newly exposed
Oracle and Modular route plus the Cosmos dual-chorus path. The existing suites
also cover:

- filter decay, bounded self-oscillation, and absence of Nyquist limit cycles;
- FM routing spectrum, envelope sample-rate independence, and bounded feedback;
- acid 18 dB/octave slope, resonance, accent brightness/level, slide time, and
  sequencer timing;
- pitch at 1×/2×/4×, mode-switch and voice-retirement continuity, parameter
  smoothing, presets, and gross CPU regressions.

These gates prove that the intended paths are active, stable, deterministic, and
within their calibration bounds. They do not establish a blind listening match
to a particular physical unit. A future measurement pass should use level-matched
hardware captures for cutoff tracking, resonance onset, drive transfer curves,
envelope timing, oscillator spectra, chorus noise/bandwidth, and parameter laws.
