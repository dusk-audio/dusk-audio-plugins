# Opto R9 stop report and Claude Code handoff — 2026-09-30

Work stopped immediately at the user's request because Codex weekly usage was
nearly exhausted.  No capture, fitting, build, test, or campaign loop remains
running.  This is an **unfinished R9 checkpoint**, not a claim that the R9
pass bar has been met.

## Repository state

- Repository: `/Users/marckorte/projects/dusk-audio-plugins`
- Committed starting point: `775f1da1` (the user's R8 checkpoint)
- Current installed AU binary SHA-256:
  `fd2534d6b399b49fa2347671bfbcb6b2320981d2b59e40e27d94cdb4203ece5c`
- Free space at stop: approximately 3.4 GiB.  Evidence is large; archive each
  completed capture batch before starting another.
- Nothing was staged or committed.  Do not discard the working tree.

Uncommitted files:

```text
 M plugins/multi-comp/core/MultiCompModes.hpp
 M plugins/multi-comp/tests/opto_au_harmonic_map.py
?? plugins/multi-comp/core/MultiCompOptoSmoothHarmonics.hpp
?? plugins/multi-comp/tests/fit_opto_smooth_harmonics.py
?? plugins/multi-comp/tests/fit_opto_spline_harmonics.py
```

## Harmonic results first

All reported AU measurements used the real Audio Units through
`duskverb_render`: 48 kHz, 512-sample blocks, two seconds of silent pre-roll,
stereo linked operation, latency compensation, Mix 100%, side-chain HP off,
and Analog Noise off.

### Fitted and held-out grids

| Grid / binary | Eligible | Within 2 dB | Worst error | Verdict |
|---|---:|---:|---:|---|
| Fitted 50/100/200/500/1k/2k/5k/8k, `r9-verdict-2` | 4,447 | 4,447 | 1.994 dB | PASS |
| Held out 150/300/700/1.4k/3.5k/12k, same binary | 2,999 | 2,851 | 26.128 dB | **FAIL: 148 cells** |
| Fitted grid after later dynamics edits plus the now-removed stability guard, `r9-final-verified` | 4,447 | 4,257 | 26.395 dB | FAIL: 190 cells |

The first row proves the smooth calibration can fit its knots.  The second row
is the untouched verdict and proves that it does **not** yet generalise between
them.  Consequently R9 step 1 has not passed and the learned GME gain computer
was deliberately not integrated.

The 190 later fitted-grid failures were diagnosed, not accepted.  They were
174 at 50 Hz and 16 at 100 Hz; by harmonic they were H7 48, H5 47, H4 29,
H3 28, H2 26, and H6 12.  Comparing the same 50 Hz cells before and after the
change showed that a cycle-count stability guard prevented the residual from
arming at low frequencies.  For example, the 50 Hz/-24 dBFS/PR 1/Gain .35 H2
moved from -72.371 dBFS to -88.386 dBFS solely because of that guard.  The
guard was therefore removed.  The installed AU contains that removal, but its
complete fitted and held-out grids have **not** yet been rendered.

Authoritative evidence:

- `build-multi-comp-1176/opto-production-harmonics-20260928/r9-fit-comparison-r9-verdict-2.json`
- `build-multi-comp-1176/opto-production-harmonics-20260928/r9-holdout-comparison-r9-verdict-2.json`
- `build-multi-comp-1176/opto-production-harmonics-20260928/r9-fit-comparison-r9-final-verified.json`

The JSON comparisons contain every eligible row, including miss size and the
UAD harmonic's absolute dBFS level.  Do not regenerate or fit against the
held-out reference before the final verdict.

### Owner's 1 kHz witness

The most recent complete witness was rendered before the low-frequency guard
was removed.  That guard did not affect the already-stable 1 kHz condition,
but rerender the witness once for strict binary identity.

Settings: 1 kHz, -16 dBFS peak input, PR .35, Gain .25, Compress, matched
output RMS.  Applied match trim was +0.003500 dB.

| Harmonic | UAD dBFS | MC-2 dBFS | MC-2 minus UAD |
|---|---:|---:|---:|
| H1 | -15.328772 | -15.328772 | +0.000000 dB |
| H2 | -67.239014 | -67.235498 | +0.003516 dB |
| H3 | -71.690623 | -71.687066 | +0.003557 dB |
| H4 | -91.615892 | -91.611782 | +0.004110 dB |
| H5 | -87.528001 | -87.501540 | +0.026461 dB |
| H6 | -101.854769 | -101.851711 | +0.003057 dB |
| H7 | -95.979772 | -95.976863 | +0.002908 dB |

Evidence and spectrum plots are in
`build-multi-comp-1176/opto-production-harmonics-20260928/checkpoints/r9-final/one-khz-pr35/`.

## What changed

`MultiCompOptoSmoothHarmonics.hpp` is generated calibration data.  It stores a
C2-continuous natural cubic spline in log frequency at eight fitted knots,
with separate polynomial coefficients by condition, harmonic, and quadrature
axis.  It is not a runtime per-frequency lookup table.

`MultiCompModes.hpp` now:

- removes the overlapping R5–R8 harmonic tables from the live signal path;
- applies the smooth harmonic calibration as the final stage, after all
  gain-law and gain-computer corrections;
- isolates the recurrence estimator used by the harmonic calibration from the
  legacy gain-law classifier;
- applies the calibration fully through Gain 35 and fades it to zero by Gain
  40, avoiding the unmeasured high-drive output ceiling;
- fades applicability from 40 to 50 Hz rather than extrapolating below the
  measured range;
- restores/refits the long-event release and Limit attack corrections;
- recalibrates the short-event correction at PR 70 and blends back to the R8
  values by PR 100;
- caps the event-drop PR scale above PR 70 and reduces its high-PR slow weight;
- removes the disproven cycle-count stability guard that suppressed 50/100 Hz
  residuals.

The generator reads only the named fitting-grid files.  It does not open the
R9 holdout grid.  The AU harness now supports `.wav.zst`, byte-verified batch
archives, the R9 fit/holdout grids, real-AU settled/music/charge commands, an
actual-AU 1 kHz witness, and selective refresh through
`OPTO_GRID_FREQUENCIES` / `OPTO_GRID_GAIN`.

## Settled, charge, music, suite, and builds

These are the last complete measurements.  The final source edit was removal
of the harmonic stability guard, so results marked `pre-final-edit` need one
confirmation run even where the edit is not expected to affect them.

| Check | Result | Status relative to installed AU |
|---|---|---|
| Settled AU/core parity | 122/122; worst 0.002172 dB | PASS, pre-final-edit |
| Short-event charge, real AU | fit RMS/worst 0.074182/0.207759 dB; held-out RMS/worst 0.313465/0.510393 dB | PASS, pre-final-edit |
| LEWITT VAL music, real AU | 0.860610 overall / 1.216541 high PR | Measured, pre-final-edit |
| R9 minus TFU1+G2+G3 bootstrap | +0.035446 dB overall, CI [0.005213, 0.069909]; +0.032519 high PR, CI [0.003535, 0.078796] | Statistically detectable, but below preregistered 0.089/0.132 dB clip-noise floors: campaign verdict **NO CHANGE** |
| Full CTest | 20/20 passed in 522.84 s | PASS, pre-final-edit |
| Full release build / AU validation | Completed successfully | PASS, pre-final-edit |

For context, the base scored 0.825163 / 1.184023; the user's R8 reference was
0.856 / 1.189.  R9 has not improved music.  Do not describe the statistically
positive paired delta as an actionable regression under the campaign's stated
noise-floor rule.

Evidence:

- `settled-au-parity-r9-final-b512.json`
- `charge-au-r9-complete-mc2-b512.json`
- `music-au-score-r9-final-verified.json`
- `music-paired-bootstrap-r9-final-verified-minus-base-tfu1-g2-g3.json`
- `ctest-r9-final.log`
- `core-r9-final.log`

All are under
`build-multi-comp-1176/opto-production-harmonics-20260928/`.

CPU was not remeasured because the prerequisite harmonic held-out gate failed
and GME was not integrated.

## Interrupted capture state

The final operation was a selective real-AU refresh of the 50 and 100 Hz fitted
cells under tag `r9-final-verified`, after removal of the stability guard.  It
was interrupted on the user's stop instruction while beginning Gain .25 PR 0.
Gain .15 conditions had completed.  Gain .25 may contain a partial overwritten
batch, and Gain .35 was not refreshed.  Do not score this mixed tag until the
selective refresh is complete.

Resume from repository root with the existing installed AU:

```bash
OPTO_GRID_FREQUENCIES=50,100 OPTO_GRID_GAIN=0.25 \
  python3 plugins/multi-comp/tests/opto_au_harmonic_map.py \
  r9-fit-capture mc2 r9-final-verified

OPTO_GRID_FREQUENCIES=50,100 OPTO_GRID_GAIN=0.35 \
  python3 plugins/multi-comp/tests/opto_au_harmonic_map.py \
  r9-fit-capture mc2 r9-final-verified

python3 plugins/multi-comp/tests/opto_au_harmonic_map.py \
  r9-fit-compare r9-final-verified
```

The capture code reruns a forced selective batch even when an older
`complete.json` exists, so the interrupted Gain .25 batch can be overwritten
safely.  Also rerun Gain .15 only if its batch archive/integrity check fails.

## Shortest safe continuation for Claude Code

1. Re-read `AGENTS.md`, `CLAUDE.md`, this handoff, and the R8 report.  Preserve
   the five uncommitted files and do not commit; the human owns commits.
2. Confirm no capture process is running and confirm the installed component
   hash shown above.  Do not rebuild before finishing the selective 50/100 Hz
   refresh unless the installed component differs.
3. Complete Gain .25 and .35 refresh and run `r9-fit-compare`.  If the fitted
   grid does not return to 4,447/4,447, diagnose that before touching the
   spline or held-out set.
4. Build/install the exact working tree, render a **new tag** for the complete
   held-out grid, and compare it once.  The prior honest held-out verdict is
   2,851/2,999 with 148 misses.  Do not fit to those held-out frequencies.
5. Rerender the 1 kHz witness on the exact final component.  Keep every H2–H7
   delta within 0.1 dB for this owner case.
6. Run the complete release build, verbose CTest, `auval`, charge, settled AU
   parity, and music/bootstrap on that exact binary.  For any new assertion,
   follow the repository rule: make it fail, record the failure, restore, and
   record the pass.
7. Because the present smooth spline fails held-out generalisation, replace or
   constrain the frequency law using a genuinely predictive structure (the
   user's suggested fixed pre/post filters is the leading option).  Fit only
   the eight fitting frequencies and keep the six holdouts sealed from fitting.
8. Integrate the lab GME only after both harmonic generalisation and music
   regression requirements pass.  No GME code is present in this working tree.
9. Report harmonic fitted and held-out tables first, then settled, music with
   bootstrap, suite/build/AU validation, and CPU.  Leave changes uncommitted.

## Explicitly unfinished

- The held-out harmonic pass bar is not met.
- The installed final edit lacks a full exact-binary build/test/capture sweep.
- The final 50/100 Hz fitted capture is incomplete and mixed with the preceding
  binary until the two commands above finish.
- GME was not integrated.
- CPU was not measured for this R9 spline or GME.
- Nothing was committed or pushed.

