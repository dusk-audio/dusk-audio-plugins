# Opto R13: measured input element, Step 1 (2026-10-02)

## Verdict: Step 1 is not closed. Probe B fails its pre-stated test; no table, header or C++ follows

| Item | Result |
|---|---|
| Element shape | **Hard one-sided kink**, positive polarity. A free knee softness always fits to 0 (hard kink). T −26.0 to −26.1 dBFS at PR ≤ .4 |
| A: zero-GR element | **Memoryless plain kink**, T −26.1 dBFS, d(PR): H2 within ±0.2 dB from −24.5 to −2 dBFS at PR .15 (35/39 H2+H4 cells within 0.5 dB). **No saturation within the probed range**: W is not identified (fits to the edge of the data) |
| B: collapse against pre-cell drive (owner's test) | **Fails as stated.** Input-axis shifts are 0 / 1.3 / 1.0 / 3.0 / 9.0 / 11.0 / 11.1 dB (Compress, PR .15 → 1). The GR-onset offsets are 0 / 8.3 / 17.2 / 21.7 / 29.0 / > 29 / > 29 dB. The shifts do not track, and the residual is up to 3.0 dB rms. The output axis gives no collapse (residual up to 9.9 dB) |
| Depth against measured GR (reported, not part of B) | The H2 depth ratio falls **−1.00 dB per dB of GR** at PR .40 / .50 (rms about the line 0.02–0.05 dB, GR 1–19 dB, both modes), −0.96 to −0.98 at PR .65 / .80, and −0.82 / −0.90 at PR 1 |

Per the protocol ("collapse without tracking, or no collapse: report, stop"), I stopped here. The
GR row is a separate measurement on the same data. It is reported for your decision and is not acted on.

## 1. Corrections to earlier statements in this round

1. **Memoryless spline test (tool defect).** The residual was a dB error on `a_k` cells that sit at the
   noise floor, with eligibility gated on the UAD harmonic level. That is replaced (see section 2)
   by:
   - eligibility on |a_k| above its own floor: the PR 0 row's maximum, per harmonic;
   - SNR weighting: the residual in units of the PR 0 row RMS;
   - a 2- or 3-parameter kink/knee as the first hypothesis.

   The 350-knot spline result is superseded (`r13_curve_fit.py` and `element-memoryless-test.json`
   are partial).
2. **"Depth falls 2.5 / 4.1 / 5.0 dB at zero GR" was an artifact.** That earlier report normalised
   by a d = 1 kink, which is not linear in d (it rescales the fundamental). In the linear regime
   (d = 0.0015), PR .15 H2 depth is flat at −56.4 ± 0.03 dB from −18 to −6 dBFS. PR .25 is flat
   while GR = 0 and falls only once GR appears. The "GR collapse" table that followed from it
   (`element-gr-collapse.json`) is void.
3. **R13 AU renders (`renders-mc2-r13`) are invalid.** The PR control runs 0..100, and the element
   was handed it unscaled, so every PR above 1 used the PR 1 row. PR 1 cells matched the UAD to
   0.0 dB, and every other PR failed. Fixed in r13b, but **r13b is not verified**: the capture was
   stopped at 18 of 35 passes. The PR 0 byte-compare against f7723433 held (188/188 stems).
4. **Dynamics.** The 10–90 % crossing lands in the first analysis window that straddles the step, so
   t10 = t90 and the transition equals the window length. The element switches on and off **below the 2.5 ms
   window**; nothing finer was measured.

## 2. Kink against the tensor (onset to −18 dBFS, all six harmonics)

Fitted per PR. Floor and weighting are as in section 1.1. Columns: T (dBFS), d, cells within 0.5 dB, signs right.

| PR | Compress | Limit |
|---|---|---|
| .15 | −26.06, 0.0015, 51/52, 52/52 | same |
| .25 | −26.04, 0.0052, 67/69, 69/69 | −26.04, 0.0052, 67/69, 69/69 |
| .40 | −26.08, 0.0125, 57/71, 71/71 | −26.09, 0.0124, 54/71, 71/71 |
| .50 | −26.28, 0.0154, 13/75, 73/75 | −26.29, 0.0152, 12/75, 73/75 |
| .65 | −26.45, 0.0201, 10/84, 78/84 | −26.48, 0.0190, 10/84, 77/84 |
| .80 | −26.47, 0.0543, 10/84, 77/84 | −26.55, 0.0452, 10/84, 76/84 |
| 1.0 | −26.36, 0.314, 15/84, 80/84 | −26.48, 0.248, 9/84, 79/84 |

- Shape, signs and null positions follow the data from onset at every PR.
- At PR ≥ .5 the misses are systematic, not scatter:
  - **The −26.0 row.** Inside the onset, T sensitivity dominates: PR 1 is predicted at −56 dBc against
    a measured −77.
  - **A drift with level that is already inside the fit range** (PR 1 H2: −0.5 dB at −25 to +1.3 dB at
    −18). This is the GR fall of section 5, because GR is already 3–18 dB at the onset cells for PR ≥ .65.
- The T creep from −26.06 to −26.5 with PR is most likely the fit absorbing that fall. It is flagged,
  not tuned.
- H3, H5 and H7 also carry the cell's odd residual (at PR 1, −30 dBFS, H3 is −37 / −58 / −69 dBc at
  100 Hz / 1.2 kHz / 5 kHz), so the even harmonics are the clean witness.

## 3. Probe A: width-limited kink at zero GR

Model: `y = x − d·min(max(x − T, 0), W)`, with T fixed at −26.1 dBFS.
- Fitted on H2 and H4.
- Levels: −25.5 to 0 dBFS, excluding −26.0.
- I report two variants: all levels, and cells with measured GR < 0.5 dB only. The PR .25 cells are
  not all at zero GR: GR is 0.24 at −10, 1.9 at −6 and 6.0 at 0 dBFS. The PR .15 cell at 0 dBFS has GR 0.86.

| Variant | d | W | Saturates at | Within 0.5 dB (H2+H4) | Max abs error |
|---|---|---|---|---|---|
| PR .15, GR < 0.5 (to −2 dBFS) | 0.00151 | 0.74 (Limit: 1.0) | −2.1 dBFS (Limit: +0.4) | 35/39 | 0.98 dB |
| PR .25, GR < 0.5 (to −10 dBFS) | 0.00514 | 0.25 | −10.5 dBFS | 27/32 | 1.68 dB |
| PR .15, all levels | 0.00173 | 1.0 | +0.4 dBFS | 0/41 | 16.1 dB |
| PR .25, all levels | 0.00493 | 0.40 | −7.0 dBFS | 23/42 | 3.8 dB |

Signed errors (predicted − measured), PR .15 Compress, GR < 0.5:

| Level (dBFS) | H2 (dB) | H4 (dB) |
|---|---|---|
| −25.5 / −25.0 | +0.98 / +0.52 | +0.62 / +0.29 |
| −24.5 to −20 | −0.08 to +0.21 | −0.41 to +0.19 |
| −18 to −4 | −0.05 to +0.05 | −0.24 to +0.10 |
| −2 | +0.13 | +0.62 |

Every sign is right.

**Reading A:**
- At zero GR the element is a memoryless plain kink, with no saturation observed up to −2 dBFS.
- W is not a measured number. At PR .15 it fits to just beyond the last cell, and Limit (the same
  data) puts it at 1.0. At PR .25 it absorbs the onset of GR at −10 dBFS.
- In the all-levels variant, a single cell (PR .15, 0 dBFS, GR 0.86) pulls d by 1.2 dB. Its H2 is
  −8.3 dB from the kink and its H4 has the wrong sign. That is not explained by GR 0.86 (see section 5).
  The likely sources are the output stage or the R11a subtraction. **Unresolved.**

## 4. Probe B: H2 depth fall against drive (the owner's test)

Fall = 20 log10(a2 / a2 of a fixed-d kink).
- T is −26.1 dBFS.
- d is fitted on the onset cells, −25.5 to −24 dBFS, H2 + H4.
- Joint overlay: one master curve (piecewise linear, 1 dB knots) and one horizontal shift per PR,
  with PR .15 at 0.
- GR onset is the input level where the measured UAD GR first reaches 0.5 dB. At PR .80 and 1.0 it is
  below −40 dBFS, outside the tensor.

| PR | Compress, input x: shift / rms / max (dB) | Limit, input x | GR onset (Compress / Limit, dBFS) | Onset offset vs .15 (Compress / Limit) |
|---|---|---|---|---|
| .15 | 0 / 0.60 / 1.66 | 0 / 1.18 / 5.01 | −0.87 / −1.03 | 0 / 0 |
| .25 | +1.33 / 0.91 / 2.26 | +4.21 / 1.31 / 4.48 | −9.15 / −9.37 | 8.3 / 8.3 |
| .40 | +0.99 / 1.64 / 3.64 | +2.81 / 0.45 / 0.95 | −18.11 / −18.45 | 17.2 / 17.4 |
| .50 | +3.00 / 3.00 / 5.39 | +5.03 / 0.45 / 1.19 | −22.52 / −22.78 | 21.7 / 21.7 |
| .65 | +9.04 / 1.68 / 2.89 | +7.00 / 0.59 / 1.33 | −29.84 / −30.05 | 29.0 / 29.0 |
| .80 | +11.00 / 0.65 / 1.43 | +7.00 / 1.90 / 3.31 | < −40 | > 29 |
| 1.0 | +11.05 / 1.25 / 3.37 | +9.04 / 0.81 / 1.74 | < −40 | > 29 |

- The shifts are a third to a half of the onset offsets and not monotone (Compress .25 > .40), so they do not track.
- With output level as x (for the record): the shifts are within ±4 dB of 0, and the residual rises to
  3.3 dB rms (9.9 dB max).

**Verdict:** on input level, the curves overlay roughly but the shifts do not track. On output level they
do not overlay. That fails the test as stated.

## 5. Depth fall against measured GR (same data, reported, not acted on)

The same fall plotted against the cell's measured GR (UAD fundamental: GR = 1.0 dB makeup − (out − in)):

| PR | Slope, Compress (dB per dB GR) | GR range | rms about line | Slope, Limit | rms |
|---|---|---|---|---|---|
| .25 | −1.22 (4 cells) | 1.9–6.0 | 0.04 | −1.15 | 0.02 |
| .40 | −1.010 | 1.4–13.5 | 0.02 | −1.010 | 0.03 |
| .50 | −1.002 | 1.2–17.2 | 0.04 | −1.002 | 0.05 |
| .65 | −0.969 | 2.8–23.0 | 0.27 | −0.977 | 0.27 |
| .80 | −0.956 | 11.7–31.4 | 0.29 | −0.971 | 0.30 |
| 1.0 | −0.824 | 15.9–34.9 | 0.57 | −0.895 | 0.61 |

- Adding the measured GR back (fall + GR − GR at the onset cells) leaves, from −18 to 0 dBFS, a residual
  that is constant per PR to ±0.1 dB at PR .40–.80. The offsets are +0.3 / +0.75 / +1.0 / +2.4–3.1 dB at
  PR .5 / .65 / .8 / 1.0.
- Those constants are the onset-d reference. It was fitted where GR is already 0 / 3.4 / 12.3 / 16.6 dB
  and still moving across the four onset cells.
- Pooled over every PR: 84/146 cells within 0.5 dB, 122/146 within 1 dB (`element-fall-gain-test.json`).

**What this says, without a model:**
- Above the kink, the element's harmonic ratio to the output fundamental is proportional to the cell
  gain, to within 0.05 dB over 18 dB of GR at PR .4 and .5.
- Equivalently, the element's harmonics leave the box at about g² × input, while the fundamental is
  at g × input.
- The departures at PR ≥ .65 (slope −0.96 down to −0.82, curvature) are measured, not explained.

**A candidate five-number element, not implemented:** kink(T, d(PR)) with depth × cell gain. You decide
whether this is the next falsification target.

## 6. Open items

- **OPTO release bar values:** still the placeholder ("set like BUS, e.g. 0.35/0.75/2.0 dB"). Needed before Step 2.
- **PR .15, 0 dBFS cell:** H2 −8.3 dB from the kink, H4 with the wrong sign, at GR 0.86. Unresolved.
- **PR 1 slope (−0.82 / −0.90) and the high-PR offsets:** unexplained.
- **Working tree:** it still holds the R13 table hook (`MultiCompModes.hpp`, with the 0.01 PR scale
  fix) and the generated `MultiCompOptoInputElement.hpp`. Both are uncommitted. Per this round's
  ruling they are not to be extended. The header comment still carries the old dynamics wording.
- **Installed AU:** the reference f7723433 is installed again (hash checked).
- **Listed for your decision (no deletions made):**
  - `build-r13/`: a repo-root configure, unused;
  - `build-r13-mc2/`: the R13 build;
  - `element/curve-table.json` and `element/element-memoryless-test.json`: superseded or partial;
  - `element/element-gr-collapse.{json,png}`: void, see section 1.2;
  - `element/renders-mc2-r13/`: invalid, see section 1.3;
  - `element/renders-mc2-r13b/`: 18 of 35 passes, with `tensor_f01200_pr050_limit` incomplete.

## Evidence

- `build-multi-comp-1176/opto-r13-20261002/element/`:
  - `element-tensor.json` and `element-tensor-hi.json`;
  - `element-knee-test.json` (per cell, signed);
  - `element-drive-probe.json` (A, B);
  - `element-fall-vs-gr.json`, `element-fall-gain-test.json` and `element-fall-slope.json`;
  - `element-dynamics-residual.json`.
- Logs: `verify/`.
- Scripts, in the tools repo `plugins/MultiComp/tests/programme_parity/opto/r13/`:
  - `r13_knee_test.py` and `r13_drive_probe.py`;
  - `r13_gr_collapse.py` (void normaliser, kept for the record);
  - `r13_element_table.py` (`tablehi`, `dynres`, `verify`);
  - `r13_memoryless_test.py` (superseded).
