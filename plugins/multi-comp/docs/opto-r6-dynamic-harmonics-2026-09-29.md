# Opto R6 actual-AU dynamic-harmonic checkpoint — 2026-09-29

## Harmonic scorecard

All numbers below come from the actual UADx LA-2A and Multi-Comp 2 Audio
Units hosted by `duskverb_render`, not a simulated DSP entry point.  Both were
rendered stereo at 48 kHz in 512-sample blocks after a two-second silent
pre-roll.  The comparison removes the reported AU latencies (UAD 87 samples,
MC-2 67 samples).  MC-2's SC HP is 0, Mix is 100%, Analog Noise is off and the
ordinary linked-stereo mode is selected explicitly.

The owner's repeatable 1 kHz case is still matched.  This is a -16 dBFS-peak
sine, Peak Reduction position 0.35 (display 33), Gain position 0.25 (display
22), Compress.  A -0.023838 dB candidate trim makes RMS differ by less than
`2e-15` dB:

| Harmonic | UAD dBFS | MC-2 dBFS | MC-2 - UAD | UAD phase | MC-2 phase |
|---:|---:|---:|---:|---:|---:|
| H2 | -67.239 | -67.228 | +0.011 dB | -179.94° | +179.72° |
| H3 | -71.691 | -71.680 | +0.011 dB | +179.85° | +177.40° |
| H4 | -91.616 | -91.632 | -0.016 dB | -1.03° | -0.68° |
| H5 | -87.528 | -87.541 | -0.013 dB | +5.59° | +2.03° |
| H6 | -101.855 | -101.893 | -0.038 dB | -1.71° | -0.92° |
| H7 | -95.980 | -95.968 | +0.012 dB | +175.05° | +177.96° |

The preregistered whole-map bar is every eligible H2-H7 magnitude within 2 dB
where the UAD harmonic is above -100 dBFS.  R6 improves the map substantially
but **does not pass that bar**:

| Actual-AU map | R5 pass cells | R6 pass cells | R6 worst error | Verdict |
|---|---:|---:|---:|---|
| 100 Hz | 265 / 695 | 491 / 695 | 18.681 dB | fail |
| 1 kHz, out-of-fit prediction for the LF residual | 620 / 626 | 620 / 626 | 3.474 dB | fail |
| 5 kHz, out-of-fit prediction | 317 / 362 | 317 / 362 | 8.647 dB | fail |
| Whole map | 1202 / 1683 | **1428 / 1683** | 18.681 dB | **fail** |

The requested 50/100/200 Hz H3/H5/H7 map, aggregated over PR 0, 0.35, 0.7
and 1.0, both modes for nonzero PR, and -40..0 dBFS in 4 dB steps, is:

| Frequency/harmonic | R5 pass | R6 pass | R6 worst error |
|---|---:|---:|---:|
| 50 Hz H3 | 19 / 63 | 44 / 63 | 4.253 dB |
| 50 Hz H5 | 3 / 61 | 16 / 61 | 12.545 dB |
| 50 Hz H7 | 3 / 43 | 4 / 43 | 16.092 dB |
| 100 Hz H3 | 12 / 63 | 59 / 63 | 3.555 dB |
| 100 Hz H5 | 9 / 56 | 39 / 56 | 18.557 dB |
| 100 Hz H7 | 4 / 29 | 6 / 29 | 16.533 dB |
| 200 Hz H3 | 30 / 61 | 47 / 61 | 6.760 dB |
| 200 Hz H5 | 13 / 44 | 33 / 44 | 12.249 dB |
| 200 Hz H7 | 5 / 16 | 3 / 16 | 11.463 dB |
| Total | 98 / 436 | **251 / 436** | 18.557 dB |

At 100 Hz and PR 0, the answer to the requested control question is **no**:
the complete H2-H7 map passes 52/60 eligible cells, worst 9.987 dB.  Restricting
that control to H3/H5/H7 gives 8/10: H3 is 5/5 and H5 is 3/3, while H7 is 0/2
and worst by 6.778 dB.  Therefore compression is not the only 100 Hz cause;
there is a static audio-path miss as well.  Compression adds a second broad
error whose spectrum follows the physical cell's twice-carrier gain ripple.

R6 adds a polynomial of the cell's measured applied-gain ripple after Gain and
the existing R5 output stage.  Its three coefficients were fitted jointly on
the 50/100/200 Hz map; the 1 and 5 kHz results above are predictions with no
refit.  A fourth-order 300 Hz taper keeps R5's 1 kHz result intact.  The
correction is enabled only after 100 ms of continuous compression and only
when a causal second-order predictor identifies periodic input.  This last
condition was necessary: the ungated residual regressed actual-AU music from
0.825/1.184 to 0.945/1.297 dB, while the periodic version measures
0.856/1.189 dB, inside the campaign noise floors.

The full magnitude-and-relative-phase data are local evidence in
`build-multi-comp-1176/opto-production-harmonics-20260928/`:

- `harmonics-base-uad.json` and `harmonics-r6-tone-gated-mc2.json` (4,158 rows
  each, 100/1000/5000 Hz, three Gain positions, all requested PR/mode cells);
- `lf-dynamic-r6-uad.json` and `lf-dynamic-r6-tone-gated-mc2.json` (693 rows
  each for H3/H5/H7 at 50/100/200 Hz);
- `comparison-r6-tone-gated.json` and
  `lf-dynamic-comparison-r6-tone-gated.json` (the bar calculations);
- `checkpoints/r6-final-controls/one-khz-pr35/` (two 24-bit WAVs, two spectrum
  plots, JSON and the matched harmonic table, freshly rendered after the final
  display-only AU rebuild).

The full map was captured immediately before the display-text inverse was
corrected.  That last edit does not touch DSP; nevertheless the exact witness
was recaptured through the final installed binary and its raw rendered WAV is
SHA-256-identical (`7e24c0ff...61a282`) to the pre-display-edit render.

## Settled, charge, knee, recovery and music

R5's regressed settled cell was row 5: 82.41 Hz, -24 dBFS, PR 100, Gain
32.1868896, Compress.  The actual-AU error was +0.481363 dB on the base,
+0.503309 dB on R5 (fail), and is now **+0.483611 dB** (pass).  The correction
is an audio-path-only 0.027 dB maximum LF/PR taper; it does not alter detector
state.

| Settled actual-AU result | Base | R5 | R6 |
|---|---:|---:|---:|
| Cells within the 0.5 dB UAD bar | 120 / 122 | 119 / 122 | **120 / 122** |
| Worst UAD error | 0.658400 dB | 0.650522 dB | 0.646098 dB |
| Cells within 0.01 dB of lab output-power path | 122 / 122 | 109 / 122 | 99 / 122 |
| Worst AU-lab delta | 0.000033 dB | 0.037622 dB | 0.034778 dB |

The final two UAD-bar failures are the pre-existing base rows 109 (20 kHz,
-18 dBFS, PR 85 Compress, +0.611797 dB) and 114 (82.41 Hz, -18 dBFS, PR 85
Limit, -0.646098 dB).  The lower AU-lab identity count is expected once an
audio-path harmonic residual changes output power; it is reported separately
from the UAD settled gate.

The requested short-event charge control was captured through both actual
AUs.  The UAD self-control reconstructs the reference at 0.000567 dB fitted
RMS / 0.001633 dB worst and 0.000088 / 0.000177 dB held out.  The landed
TFU1+G2+G3 base is already **1.242447 / 2.587525 dB fitted and 0.890249 /
1.508616 dB held out**, so the gate fails before R5.  It is not an R5 or R6
regression.  R6's core result remains 1.242445 / 2.587469 and 0.890250 /
1.508611 dB because the periodic residual cannot arm inside these 0.354-10 ms
events.

The core recovery readout remains 0.521571 dB RMS / 1.680064 dB worst (R5 was
0.521573 / 1.680069).  The nine-cell knee test passes with 0.043379 dB worst.
The real-AU corrected music metric on the non-sealed LEWITT VAL corpus is:

| Path | Overall | High PR | Change from base | Regression decision |
|---|---:|---:|---:|---|
| TFU1+G2+G3 actual AU | 0.825163 dB | 1.184023 dB | reference | — |
| R5 actual AU | 0.836970 dB | 1.201492 dB | +0.011807 / +0.017469 | no change |
| R6 periodic actual AU | 0.856163 dB | 1.188709 dB | +0.031000 / +0.004686 | **no change** |

The campaign noise floors are 0.089 dB overall and 0.132 dB high PR.  No
sealed or Untitled material was opened.  The score evidence is
`music-au-score-{base-tfu1-g2-g3,r5,r6-tone-gated}.json` in the same local
evidence root.

## AU controls and parameter mapping

The Opto defaults are now measurement-neutral: Mix 100%, SC HP 0, Stereo Link
100%, Auto Makeup off, generic Distortion off, True Peak off, Lookahead 0 and
**Analog Noise off**.  Apple's AU validator reports Analog Noise default 0;
the descriptor and DSP-state defaults agree.  No additional default change is
proposed.

The UAD sweep was measured independently for Peak Reduction and Gain at every
0.05 normalized position.  Both controls use the same display curve.  MC-2 now
uses that curve for host text and its own knob readout while retaining the old
0..100 host/state/automation values:

| Normalized position | 0 | .05 | .10 | .20 | .35 | .50 | .70 | .80 | .95 | 1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| UAD display | Min | 0 | 5 | 16 | 33 | 50 | 72 | 84 | 100 | Max |
| MC-2 display | 0 | 0 | 5 | 16 | 33 | 50 | 72 | 84 | 100 | 100 |

The only differences are the UAD's cosmetic `Min`/`Max` endpoint words.  UAD
readback is quantized to 1/4096; MC-2 readback retains the requested float.
There is no internal knob-curve mismatch and existing automation is unchanged.
The dense sweep is in `parameter-sweep/{uad,mc2}.json` under the evidence root.

## Suite and build result

- Release build: all DAF targets built, including AU, VST3, CLAP, LV2 and JACK.
- Final AU target rebuild: pass; the installed and built AU binary SHA-256 is
  `25d0c37f706c9cdb84241d585bd651660cf5e30b02a4fd8d08cb462ebf694c2e`.
- Apple `auval -v aufx DsMc Dusk`: **PASS**, including mono/stereo render,
  scheduled parameter and multi-rate/block tests.
- CTest: **19 / 20 pass** in 272.10 s.  `MultiCompCore` is the sole failure,
  specifically the pre-existing short-event charge assertion measured above.
  Plugin-layer, AU, non-finite, meter, bus and format tests all pass.
- The new display/default assertion was proved live: changing its expected
  display at host 35 from 33 to 34 made `MultiCompPluginLayer` fail; restoring
  33 made the complete test pass.  The inverse guard was also observed failing
  with a deliberately wrong 35.1 expected host value and passing again at the
  measured 35.0 value (display 33); display 72 similarly maps exactly to host
  70.
- `git diff --check`: pass.
- The older `tests/run_plugin_tests.sh --plugin "Multi-Comp" --skip-audio`
  reports 3 pass / 1 fail / 2 skip because it expects the retired installed
  `Multi-Comp.vst3` binary layout and pluginval is absent.  This is not the DAF
  Multi-Comp 2 AU validation result; it is recorded rather than hidden.

## Verdict and uncommitted files

This round fixes the settled regression, preserves the owner's 1 kHz result,
improves LF dynamic harmonics by 153 eligible cells, keeps music within its
noise floor, makes the defaults neutral and makes the displayed Opto controls
match the UAD.  It is **not a full harmonic-map pass**: 255 eligible cells
remain outside 2 dB, concentrated in LF H5/H7 plus 5 kHz H3/H4.  The static
100 Hz PR-0 miss and the failed 5 kHz prediction show that a single 1 kHz
static waveshaper plus one LF ripple polynomial is not the complete output
stage.  This is a measured negative result, not a release claim.

Leave these files uncommitted for owner review:

- `plugins/multi-comp/core/MultiCompModes.hpp`
- `plugins/multi-comp/core/MultiCompOptoCell.hpp`
- `plugins/multi-comp/core/MultiCompParams.hpp`
- `plugins/multi-comp/daf-plugin/MultiCompParams.hpp`
- `plugins/multi-comp/daf-plugin/MultiCompPlugin.cpp`
- `plugins/multi-comp/daf-plugin/MultiCompPluginLayerTests.cpp`
- `plugins/multi-comp/daf-plugin/MultiCompUI.cpp`
- `plugins/multi-comp/multicomp.cpp`
- `plugins/multi-comp/tests/opto_au_harmonic_map.py`
- `plugins/multi-comp/docs/opto-r6-dynamic-harmonics-2026-09-29.md`

No commit, branch operation or push was performed.
