# Opto: known deltas against the UAD LA-2A at campaign close (2026-10-15)

Owner decision 2026-10-15: OPTO ships as it stands. The four gate readings still over the 1.00 dB bar are recorded
here with their root cause and left as known deltas. Everything else in the probe set (fifteen screen tables, the
re-captured core fixtures, the release / onset / harmonic rows) is at or under the bar on the working tree that
carries the 2026-10-15 emission shape (commit 743d2347).

Why they are left: the owner's blind ABX against the UAD on thirty real clips, including a kick-only round, was at
chance (13 / 22 pairs, p = 0.26; `opto-abx-2026-10-03.md`). All four readings below come from synthetic probes that no
clip in that corpus resembles. Every cheap mechanism that could have moved them has been measured and falsified; the
one lever left is a structural refit of the cell's time step, which the owner stopped on cost (2026-10-14).

| probe | cell | UAD | MC-2 | delta | direction |
|---|---|---|---|---|---|
| Phase offset (25 Hz on both channels, right 90 degrees behind, Limit, Peak Reduction .8125, stereo link) | `pr0.8125_lim1 l25_p090`, left | -23.90 dB | -25.58 dB | 1.68 | MC-2 reduces more |
| Low + mid family (60 Hz with 300 Hz 6 dB below it in quadrature, Limit, Peak Reduction 1, stereo link), settled | `pr1_lim1 l60_m300_d06_quad`, left | -29.49 dB | -30.99 dB | 1.50 | MC-2 reduces more |
| Crest sweep, gain reduction on the 23.8 dB crest-factor signal | `crest_sweep/crest23.8/gr` | 8.52 dB | 7.02 dB | 1.51 | MC-2 reduces less |
| Short charge, reduction after a 1 ms burst at -0.25 dBFS | `short_charge/1.000ms_peak-0.25` | 12.73 dB | 11.23 dB | 1.50 | MC-2 reduces less |

Counts behind the worst cells: phase probe 25 of 162 cells over (all at Limit, 60-120 degrees, 25-40 Hz); family 63 of
4,392 over (57 deeper, 6 shallower; 60 linked rows and 3 mono masters at 60 + 300 Hz Limit 100 and 25 + 150 Hz
Limit 81); crest sweep 2 of 4 (20.0 dB crest 1.10, 23.8 dB 1.51); short charge 1 of 16 (the 1 ms -4 dBFS row 0.76).

## Root cause, measured

**Phase and family (the stereo link).** Each channel has its own panel, and a panel's emission is a burst at each
polarity change of its drive plus a steady part. A linked pair's cell is lit by the larger of the two emissions. When
the channels' zero crossings are offset the cell sees four bursts per cycle instead of two; the native unit's single
cell does not double them (its added reduction over the in-phase pair is complete within about 1 ms of offset and flat
to 150 degrees: 1.77 / 2.18 / 2.76 dB at 25 / 40 / 60 Hz, Limit .8125). The partner's steady light filling this
panel's dark window is only 0.2-0.3 dB of the excess. Forms that pass the partner's burst and withhold its steady
light per channel (B', C12 to C20, 2026-10-13/14) fix the probe and the family but set the two channels of real stereo
music up to 0.3 dB apart where the native unit reads alike, and raise the pooled error of the stationary stereo and
split-spectrum tables over the never-worse floor; no symmetric (one-light) form reaches the probe with the present
cell, whose time step follows the instantaneous light and so reads the in-cycle shape of what it is lit by. Measured
and falsified on 2026-10-15 (cell observers, `triage/panel-clock/killtests-20261015/`): one panel on the signed union
of both drives (adds grow with the offset instead of saturating), the steady light held through the crossing with the
cycle's charge conserved (90 degree cells unchanged, mono moves).

**Crest sweep and short charge (the time step).** A hot, short burst charges this cell less than the native unit:
the populations' time step slows as the light rises (dt / (1 + (light / 19.9)^0.33)), so the same charge delivered
brighter and shorter moves them less. The second clock exponent above the light table's top (plan B2, 2026-10-15)
buys at most +0.42 dB and breaks the 2 ms burst row; the time step on a cycle-averaged light (T1, 2026-10-14) loses
the 1 kHz statics (worst 2.42 dB against 0.33) and its light table could not be re-solved within the campaign. The
pedestal "3.7 dB" reading of the same fixture family was a one-sample window misalignment in the test helper, fixed in
the test code on 2026-10-15; its real shortfall is 0.3-1.0 dB and belongs to the same time-step question.

## What stays in place

- The CTest bounds, the fifteen screen tables and the fixture gate keep their current bars; nothing is loosened. The
  four rows above stay red in their report-only readers and the screen's "cells over" count stays 25 / 63 / 2 / 1 as
  the never-worse base for any later OPTO change (`screen.py compare <cand> h5-r3`).
- The native captures stay (local, never in a repo) for a re-ABX if the cell is changed.
- Reopen only on a user report that names material and setting: run `abx_kit.py focus` on that material first, then
  the time-step refit plan (`handoff/PLAN-MC2-OPTO-TIMELAW-REFIT-20261014.md`), never a per-channel link law.
