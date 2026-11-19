# Opto R5 actual-AU harmonic checkpoint — 2026-09-28

## Verdict

**R5 is rejected for production.** It fixes the owner's reported 1 kHz case,
but it does not pass the preregistered whole-map bar and it loses one settled
cell. The code remains an uncommitted experimental working-tree change.

The 1 kHz, -16 dBFS peak, Peak Reduction 0.35, Gain 0.25, Compress witness is
excellent: after a -0.023839 dB output match, H2 through H7 differ from the UAD
by +0.011, +0.012, -0.016, -0.017, -0.038 and +0.012 dB. This audio came from
the two actual Audio Units, not a core simulator.

The complete map still passes only **1,202 / 1,683** eligible cells, with a
**36.742 dB** worst error. The remaining error is overwhelmingly the dynamic
100 Hz H5/H7 behavior. A static 1 kHz saturation fit cannot reproduce it.

## Frozen measurement boundary

- Actual AU components hosted by `duskverb_render`.
- 48 kHz, 512-sample blocks, stereo, fresh instance/reset per setting group,
  2 seconds of silent pre-roll, and latency compensation before analysis.
- UAD latency 87 samples; MC-2 latency 67 samples.
- UAD binary SHA-256 `8c97e490ab02c6d6a68a14621bcce660bd96a9f9d840d2ab4339f733f5651ba2`.
- R5 built and installed MC-2 binary SHA-256
  `eca69ad7d2ae8246b839d3552ade9ffd1b3f2c7518f94f59cb265b712ac63187`.
- Every MC-2 render explicitly sets SC HP to 0, Mix to 100%, Analog Noise off,
  generic Distortion off, Auto Makeup off, True Peak off, normal stereo link,
  and the requested Opto controls.
- H2-H7 magnitude and phase are measured relative to H1. Harmonics at or above
  Nyquist are marked unrepresentable, not scored.

## Lab base landing

TFU1 + G2 + G3 was copied from the frozen lab path. Before adding the fitted
residual, the built AU matched the lab production path on **122 / 122** settled
cells within 0.01 dB; worst error was **0.000032813 dB**. An earlier apparent
failure was traced to macOS resolving the component identifier to the installed
v0.1.0 binary. The old component was moved recoverably into the evidence tree,
the fresh build was installed, and build/install hashes were then checked for
every candidate.

## Harmonic scorecard

The declared bar is every representable H2-H7 component within 2 dB wherever
the UAD component is above -100 dBFS.

| Candidate | Passed / eligible | Worst error | Decision |
|---|---:|---:|---|
| Pre-fit TFU1+G2+G3 | 246 / 1,683 | 84.146 dB | fail |
| R1, absolute post-Gain residual | 981 / 1,683 | 25.661 dB | fail |
| R2, GR-coordinate table | 946 / 1,683 | 26.168 dB | reject |
| R3, detector-GR operating-level shift | 986 / 1,683 | 42.221 dB | reject |
| R4, P-law scaling from cell GR | 1,124 / 1,683 | 25.589 dB | reject |
| **R5, P-law scaling from applied audio gain** | **1,202 / 1,683** | **36.742 dB** | **reject** |

R5 detail:

| Slice | Passed / eligible | Mean absolute error | Worst |
|---|---:|---:|---:|
| 100 Hz | 265 / 695 | 3.755 dB | 36.742 dB |
| 1 kHz fit frequency | 620 / 626 | 0.330 dB | 3.498 dB |
| 5 kHz prediction | 317 / 362 | 1.057 dB | 8.647 dB |
| H2 | 438 / 448 | 0.555 dB | 2.771 dB |
| H3 | 276 / 457 | 1.675 dB | 8.647 dB |
| H4 | 217 / 282 | 1.443 dB | 11.818 dB |
| H5 | 126 / 257 | 4.121 dB | 18.068 dB |
| H6 | 78 / 106 | 1.196 dB | 5.600 dB |
| H7 | 67 / 133 | 4.448 dB | 36.742 dB |

Only 1 kHz reference rows enter the generated coefficient fit. R4 and R5 add
no fitted parameter: they test the pre-cell P element's measured transmission-
squared placement law. R5 correctly uses the LF-floor-blended gain that
actually multiplies audio. That materially improves the prediction, but the
remaining low-frequency phase and high-order pattern requires a dynamic
audio-path element; scaling a memoryless residual further produces cancellation.

## Owner's 1 kHz witness

Condition: 1 kHz stereo sine, -16 dBFS peak input, Peak Reduction 0.35, Gain
0.25, Compress. Candidate output was trimmed -0.023839 dB after the AU so RMS
matches exactly (well inside 0.1 dB).

| Harmonic | UAD dBFS | MC-2 R5 dBFS | Difference |
|---:|---:|---:|---:|
| H1 | -15.329 | -15.329 | -0.000 dB |
| H2 | -67.239 | -67.228 | +0.011 dB |
| H3 | -71.691 | -71.679 | +0.012 dB |
| H4 | -91.616 | -91.631 | -0.016 dB |
| H5 | -87.528 | -87.545 | -0.017 dB |
| H6 | -101.855 | -101.893 | -0.038 dB |
| H7 | -95.980 | -95.968 | +0.012 dB |

The local evidence directory contains latency-aligned 48 kHz/24-bit WAVs,
separate spectrum plots, the table and machine-readable metadata at
`build-multi-comp-1176/opto-production-harmonics-20260928/checkpoints/r5/one-khz-pr35/`.

## Parameter mapping and neutral-state audit

The MC-2 AU exposes a linear normalized mapping: 0.35 reads back as 0.349999994
and displays 35 for both Peak Reduction and Gain. The UAD uses a 12-bit host
grid and a different display curve: 0.35 reads back as 0.349975586 and displays
33; 0.70 displays 72; 0.90 displays 95. Peak Reduction and Gain use the same
UAD curve.

Therefore same normalized knob position/angle is the valid lab comparison.
Typing the same displayed number into the two plugins is **not** equivalent and
invalidates a same-number DAW comparison. Complete sweeps are in
`parameter-sweep/uad.json` and `parameter-sweep/mc2.json` under the evidence
directory.

MC-2's default Mix 100%, SC HP 0 Hz, Stereo Link 100%, Auto Makeup off,
generic Distortion off, True Peak off and Lookahead 0 are neutral. **Analog
Noise defaults on**, so the overall default is not measurement-neutral. The
recommended neutral default is Analog Noise off. Per instruction, this default
was not changed.

## Regression and build status

- Final R5 actual-AU settled score at the campaign's 0.5 dB cell bar is
  **119 / 122**, versus **120 / 122** for the lab base: regression, fail.
- Direct R5 AU versus lab-path reduction is 109 / 122 within 0.01 dB, worst
  0.037622 dB. The 0.01 dB landing check passed before saturation; R5 does not
  preserve it.
- Recovery control: exact base is 0.560685 dB RMS / 1.656391 dB worst; R5 is
  0.521573 / 1.680069 dB. Aggregate recovery improves, with a +0.023678 dB
  movement at the single worst cell.
- The AU target builds and signs successfully. All test targets build.
- Full CTest initially reached 19 / 20 passing; `MultiCompCore` exposed stale
  harmonic fixtures. They were replaced with actual-AU H2-H7 rows and were
  observed failing before and passing with R5. The exact TFU1 lab base also
  demonstrated that the old recovery bound was already failing.
- The final core run proceeds beyond harmonic and recovery checks, then fails
  the pre-existing short-event charge gate (R5 fitted RMS 1.242 dB, worst
  2.587 dB). Consequently the requested full C++ suite is **not green**.
- Knee and music were not re-scored through the actual AU after the harmonic
  failure. The production pass bar was already impossible, so this candidate
  was not committed or designated for release.

## Files in the experimental change

- `core/MultiCompOptoCell.hpp`: exact TFU1 generated cell.
- `core/MultiCompModes.hpp`: G2, G3 and the R5 residual path.
- `core/MultiCompOptoHarmonics.hpp`: generated 1 kHz-only coefficients.
- `core/tests/MultiCompCoreTests.cpp`: actual-AU harmonic fixture/guard repair.
- `tests/fit_opto_harmonics.py`: 1 kHz fit firewall and header generator.
- `tests/opto_au_harmonic_map.py`: actual-AU capture, comparison, settled and
  witness tooling.
- `docs/opto-harmonic-calibration-2026-09-28.md`: frozen preregistration.

No branch or commit was created because the repository's loaded `AGENTS.md`
forbids automated branch and commit operations. Nothing was pushed.
