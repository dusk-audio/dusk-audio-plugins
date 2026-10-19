# Compressor parity playbook (2026-10-03)

Process that produced the OPTO result in one day, written down for FET, VCA and BUS. Tools live in the tools
repo under `plugins/MultiComp/tests/programme_parity/kit/`; evidence stays in the local, ignored
`build-multi-comp-1176/` tree. Every step of the loop below was run on OPTO on 2026-10-03 and reproduced that
day's tables. The later sections are updates and carry their own dates: the probe set (2026-10-06, with lessons to
2026-10-09) and the per-mode state (OPTO closed 2026-10-15).

## The loop

1. **Listen first, blind.** Build an ABX from the reference captures and the installed MC-2 before any DSP
   work. Tiers: 0 dB GR (linear, doubles as a null control), 1-3 dB, 7-10 dB, and the loud cells the
   measurement flags. If the owner is at chance everywhere, ship. If not, the trials the owner gets right
   are the entire target. Twenty trials detect only differences heard most of the time; that is the bar.
2. **Measure where the residual is.** `corpus_diff.py` over the 30-clip corpus: per setting, per material
   family, worst cells, with level and 20-120 Hz offsets. The audible-sized residual is usually one family
   at one range of settings (OPTO: kick and bass at PR 1 / Limit, 2-3.6 dB). Everything under ~0.6 dB rms
   on vocals was inaudible in four listening rounds.
3. **Probe the mechanism with synthetic signals.** `probes.py synth` then render both devices at the
   settings under study. Read the table in this order: settled column (statics) first, then onsets, then
   low vs high drive, then from dark vs already lit. Each contrast rules a class of mechanism in or out
   before any constant moves. OPTO: statics matched everywhere, the error was the 60 Hz onset from dark
   and flipped sign with drive.
4. **State the hypothesis and its falsifier, then build the candidate off by default.** New constants
   appended to the defaults array, bit-identical renders with the element off (verify, do not assume).
   Three OPTO candidates were falsified in under an hour each because the lab answers in seconds.
5. **Fit in the lab, select on FIT clips, hold out VAL and the sealed mixes.** Grid first (coarse, then
   around the edge it lands on), one mechanism per round. A machine fit is exempt from the one-knob rule;
   a hand round is not.
6. **Gates before landing, in this order:** probe table not worse on any row class; FIT and held-out
   music cells; the full DEV set unchanged within self-noise (0.001-0.01 dB); CTest; byte-compare the
   corpus renders when a change claims to be transparent (120 of 120 stems, not "should be").
7. **Re-ABX on the cells that changed, fresh seed.** Then write the report with the negative results in
   it, and keep each change as its own patch so the owner can commit them separately.

## What did not work, so it is not repeated

- Instrument-only loops with many simultaneous gates and no audibility reference. Twelve hand rounds in
  two weeks traded gates and produced no shippable decision.
- A pooled fit over the whole corpus to fix a minority family: the optimiser moves the error to where
  the loss is cheapest (iteration 6 fixed Comp .5 and made deep settings 7 dB worse).
- Confidence labels in listening tests. "Sure" and "probably" never beat chance; null controls drew
  confident answers. Only the hit count over many trials means anything.
- End-to-end learned capture on a small corpus (closed 2026-10-02).

## What the probe set must cover (2026-10-06: each line is an error several passes could not see)

- **Every control position, not only the fitted ones.** The FET Release knob was 3.7 dB wrong between .75
  and .95 while 0, .33, .666 and 1 fitted. Capture a few positions that enter no fit and keep them out.
- **Music per cell, not only per tier.** Tiers under 1 dB hid sixteen FET cells above it, one clip at five
  conditions among them. Print every cell at or above the bar by name.
- **An event from rest, at the deepest setting.** A probe on a bed that already holds reduction keeps the
  slow states charged. FET stopped 3-5 dB short on any deep burst from silence and no table showed it.
- **A train of events, read between them and after the last.** What a single burst leaves behind says
  nothing about what six leave: the FET reference builds 6 dB over six bursts at its fastest release.
- **More than two frequencies in every dynamic table.** The OPTO onset table held 60 Hz and 1 kHz; the
  element fitted on it was unconstrained from 80 to 400 Hz, where a kick drum is.
- **Where in the cycle the gain moves.** Fold the reduction on a 15-45 Hz sine over the half-cycle (output
  over the device's own low-level output at the same sample). The phase of the ripple says where the side
  chain reads the signal and when the element responds; a level table cannot. FET read the wrong side of
  a high-pass (56 degrees at 15 Hz); the OPTO reference lights right after the zero crossing at deep
  settings.
- **A linear reference that is linear.** A "no reduction" setting was still compressing. Use the same
  stimulus 60 dB down at the same setting, per device.
- **A music cell split by what the stimulus does, not only by what the reference's gain does.** The frames
  where the reference's gain "moves fastest" were read as attacks for two passes; split by the stimulus
  (onset, offset, low level, rest) 60 % of one OPTO cell's squared error sat in the frames after gated note
  ENDS, where the stimulus is at -46 to -87 dBFS and the gate's own 20 Hz high-pass is still ringing above
  its -60 dBFS keep rule. An element was built for the attacks and did not move the cell.
- **Before adding a state, read the one you have through the device's law.** The FET reference leaves more
  memory after its second burst than after its first; an exposure state was built for that. The square
  root of the memory is first order: one state, read through a gain law that is quadratic at the bottom,
  and the same law explained two other readings (the memory counting for more under a fast charge, the
  loop sitting low from rest at the highest ratio).
- **Fit a part of the model only on the cells that read that part.** Low-carrier windows after a burst
  read the output stage; they judge a gain-computer fit and do not enter it.
- **Which side of the gain element a filter sits on.** A linear capture gives the product of all linear
  sections and nothing else. Step the gain with a burst the model already holds, under a steady low tone
  50 dB below it, in both polarities: half the difference is the tone's response alone. What is behind
  the gain element rings with its own poles when the gain steps; what is ahead of it does nothing. Read
  the product itself on the tails that low bursts leave when they end, at a level too low to compress
  (the FET path's low end was one second-order section fitted on phase; it is two, one each side, and
  sixteen of the twenty burst-train cells left were that).
- **What the device does once the stimulus is gone.** A gate that needs stimulus level cannot read the
  gain after a note is cut. A pilot 60 dB under the note reads it, and the output's mean after the note
  reads what the device was adding to the mean during it (the OPTO reference's one-sided clip: 3 % second
  harmonic on programme at full Peak Reduction, and the step when the note ends was the last music cell).
- **A table read through the model's kernel is only the plugin while nothing acts on the audio sample.**
  Once an element does, read the same rows on what the plugin puts out, through the same extraction as
  the reference's capture, and feed the kernel the audio.
- **An element found late was inside every earlier fit.** The tables were fitted with its level in them;
  adding it on top counts that twice. Split it: the part that changes level stays with the tables, the
  part no table can hold is added (the OPTO clip is one-sided; its odd half is a loss of level, its even
  half is the even harmonics and the step of the mean, and the even half is memoryless). Giving the loss
  back through a gain instead needs a memory, and a memory leaves a bump behind every loud event. The
  alternative is to refit every table that held it.
- **A kink evaluated point by point folds.** Take the nonlinear term off the sample itself, averaged over
  the line from the last sample (antiderivative form), not through a gain made from one sample and
  applied to the next; and ask the reference how much it folds before deciding how clean to be.
- **A new table at one level is one point of a law.** The OPTO release-into-silence table (five notes at
  -8 dBFS) read eight cells over, the worst 2.25 dB; the same reading at -2 dBFS, at Limit 1 and at 3 kHz
  (2026-10-09, `opto/silence_holdout.py`) read 6.7 dB, and showed what the table could not: the reference
  never lets go faster than 0.245 dB/ms from any depth. Capture the level, the length and the control
  positions around a table before fitting anything to it, and keep those captures out of the fit.
- **Then ask where the law stops.** That limit holds after 60 ms of note and not after a 2-20 ms burst
  (`opto/silence_short.py`: 0.80 dB/ms after 2 ms). An element built on the long notes alone read the
  bursts 4 dB wrong and failed a core test that reads the same event on a bed. Probe the short side of
  every time-dependent law before landing it.
- **"Lit tables unchanged" is a measurement, not a property of the code.** A state meant to exist only in
  silence built up inside the zero crossings of a 55 Hz sawtooth and moved two settled rows by 0.07 dB.
  Run the settled table with each new element alone; one of the two elements was exact and the other was not.
- **A hidden state shows when its mask goes.** After a low note the cell sat 2.7 dB deeper than the applied
  gain (the LF floor's lift). Slowing the cell's release to the reference's exposed that as a slow release
  after low notes only; the release table needed both elements together.

## Kit commands (tools repo, `programme_parity/kit/`)

```
# render host with #INDEX parameter addressing (MC-2 reuses "Threshold"/"Attack" across modes)
cmake --build build --target duskverb_render
# capture a device over a clip directory at one setting (values 0..1, snapped to the UAD 1/32768 grid)
python3 render.py 1176     OUT --stim CLIPS --set "Input=0.6,Ratio=0"                      --tag in60_r4
python3 render.py mc2:fet  OUT --stim CLIPS --set "input=0.6,output=0.625916,attack=0.5,release=0.665955,ratio=0" --tag in60_r4
# (FET knob map is identity on the normalised axis, fet/knob_map.py 2026-10-03: give MC-2 the 1176 values, Output
#  included; Output 0.5 vs the 1176 neutral 0.625916 is 8.9 dB of linear gain)
# where the residual is (lags measured on the first tag; pass a linear tag first, then pin them)
python3 corpus_diff.py REF_ROOT DUT_ROOT --stim CLIPS --tags lin,in60_r4 --json diff.json
# mechanism probes
python3 probes.py synth PROBES; render both devices on PROBES; python3 probes.py eval REF DUT --lag-ref N --lag-dut N --stim PROBES
# blind ABX
python3 abx.py build OUT REF_ROOT DUT_ROOT --stim CLIPS --lag-ref N --lag-dut N --seed S clip:tag clip:tag:null ...
python3 abx.py score OUT results.json
```

Device specs (AU paths, parameter names, MC-2 mode and control indices, measured latencies) are in
`references.py`; add a reference there, nowhere else. The exact-core lab used for OPTO (`greybox/gb_r14.py`,
`gb_common.Cell`) wraps `OptoCell::setParams`; FET, VCA and BUS have no such hook yet, so their candidates
go through `lab.py` (full DSP from a patched core copy, byte-exact baseline check) until one is added.

## Per-mode starting state

| Mode | Reference | Access | Existing probes and captures | First step |
|---|---|---|---|---|
| OPTO | UADx LA-2A | installed | campaign closed 2026-10-15; four synthetic-probe readings left over the bar (phase and family screen tables, crest and short-charge fixture rows), plus the settled screen table's edge cell (1.0061 dB since 2026-10-18), see `opto-known-deltas-2026-10-15.md` | Reopen only on a user report: `abx_kit.py focus` on that material first |
| FET | UADx 1176 | installed | `fet/static_grid.py`, `fet/taper_sweep.py`; earlier campaign log in memory. State 2026-10-18 (4:1 release depth term landed): 0 of 4,340 probe cells and 0 cells on all 18 music tiers over 1 dB; harmonic rows over 1 dB 7 of 156, worst +3.16 dB (H5 of 100 Hz at the bottom of the knee, 0.95 dB GR); the programme-recovery fixture reads 2.18 dB 50 ms after a short burst (rows 28-31; partial-regression bound 4.0) | Tiered ABX, then the 100 Hz knee rows and the after-burst release |
| VCA | UADx dbx 160 | installed | `vca/` (18 scripts: tilt, onset, fits); VCA faceplate UI on branch `mc2/dbx160-vca-ui`. State 2026-10-12: 0 cells over 1 dB in every table (music 0 of 270, worst 0.073 dB) | Tiered ABX |
| BUS | UADx SSL G | demo expired 2026-09-22, parked | `bus/` (137 scripts), accepted at the internal bar 2026-09-21 | Needs a licence before any capture |
| Studio FET / VCA, Digital, Multiband | none | | | Not parity targets |

Rules that stay: never loosen a test bound; no classifiers or hand corrections in the audio path; captures
and candidate renders never enter a repo; the owner commits.

## Open before the stable 1.0.0 (from the 1.0.0-alpha1 review, 2026-10-18)

- FET ratio buttons switch without a crossfade; going to or from All jumps.
- Global Lookahead in Multiband, with the internal detector, delays the detector along with the audio
  (`processMultiband` splits the already delayed input), so it adds up to 10 ms of latency and no lead; with an
  external sidechain the lead is real. Either split an undelayed copy for the detector or drop Multiband from
  `globalLookaheadForMode`; `--global-lookahead-scope` prints Multiband's onset lead (0.00 dB today).
- Latency held constant across modes (2026-10-19): the plugin pads each mode's output to Opto's latency
  (`MultiCompLatencyPad.hpp`, `MultiCompDSP::getHostLatencySamples()`). Only the lookaheads (Global
  Lookahead in Digital and Multiband, Digital's own) still move it, and the AU wrapper still never tells
  the host when they do.
- Sixteen automatable parameters have no control on any face, four VCA parameters do nothing, and several host
  names repeat (Threshold five times). FET and Studio FET share every `fet_*` parameter, so Curve and Transient
  set on the Studio FET face act in FET mode.
- FET Curve Measured with All: the small-signal gain sits 1.20 dB under the reference (pinned by
  `testFetMeasuredCurveAllButtons`).
- From code review, not yet measured: `MultiCompDSP::prepare()` at the same rate keeps most modes' state (the
  plugin masks it: the framework deactivates first; the same-rate test runs no audio between the two prepares),
  and the Studio VCA, Digital, FET All and Studio FET envelopes, and the FET feedback capacitor's leak (whose time
  the 4:1 release depth term stretches up to about 40x at extreme depth), are float one-poles at the mode rate, so at
  192 kHz and above with oversampling a long release may stall short of unity.
- Dead weight: the legacy `SidechainTilt` class (kept only for a compile-time disabled-element control) and the
  fixed-Hz branch in `MultiCompVcaLaw.hpp`; `MultiCompOptoFinalHarmonics.hpp` and
  `MultiCompOptoLfDynamicHarmonics.hpp` (about 390 KB) are included nowhere.
- The CLAP version string is empty (framework), and alpha and stable both report 1.0.0.
- No user manual yet.
