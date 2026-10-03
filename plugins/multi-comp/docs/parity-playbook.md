# Compressor parity playbook (2026-10-03)

Process that produced the OPTO result in one day, written down for FET, VCA and BUS. Tools live in the tools
repo under `plugins/MultiComp/tests/programme_parity/kit/`; evidence stays in the local, ignored
`build-multi-comp-1176/` tree. Every step below was run on OPTO on 2026-10-03 and reproduces today's tables.

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

## Kit commands (tools repo, `programme_parity/kit/`)

```
# render host with #INDEX parameter addressing (MC-2 reuses "Threshold"/"Attack" across modes)
cmake --build build --target duskverb_render
# capture a device over a clip directory at one setting (values 0..1, snapped to the UAD 1/32768 grid)
python3 render.py 1176     OUT --stim CLIPS --set "Input=0.6,Ratio=0"                      --tag in60_r4
python3 render.py mc2:fet  OUT --stim CLIPS --set "input=0.6,output=0.5,attack=0.5,release=0.5,ratio=0" --tag in60_r4
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
| FET | UADx 1176 | installed | `fet/static_grid.py`, `fet/taper_sweep.py`; earlier campaign log in memory | Tiered ABX, then corpus_diff |
| VCA | UADx dbx 160 | installed | `vca/` (18 scripts: tilt, onset, fits); VCA faceplate UI on branch `mc2/dbx160-vca-ui` | Tiered ABX, then corpus_diff |
| BUS | UADx SSL G | demo expired 2026-09-22, parked | `bus/` (137 scripts), accepted at the internal bar 2026-09-21 | Needs a licence before any capture |
| Studio FET / VCA, Digital, Multiband | none | | | Not parity targets |

Rules that stay: never loosen a test bound; no classifiers or hand corrections in the audio path; captures
and candidate renders never enter a repo; the owner commits.
