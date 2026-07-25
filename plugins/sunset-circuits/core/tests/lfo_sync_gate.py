#!/usr/bin/env python3
"""Tempo-synced LFO host phase-lock gate.

A synced LFO must be a function of the host SONG POSITION, not of whenever the
last note happened to start. Before the lock existed, sync only scaled the rate
by bpm/120: the average speed was right, the phase was wherever the note-on left
it, and it drifted against the bar line forever after (nothing ever pulled it
back). The three ways that shows up are what this gate measures.

Measurement rig: LFO1 -> Amplitude at full depth, triangle shape, so the output
envelope is |1 + tri(phase)| and touches ZERO exactly at phase 0. Envelope
minima are therefore the LFO's cycle starts, readable to well under a
millisecond off the analytic envelope of a 1 kHz carrier.

  rate 2.0 Hz + sync  ==  one cycle per quarter note at every tempo
  (synced rate = knob*bpm/120 cycles/s against bpm/60 beats/s -> 2/knob beats
  per cycle, tempo-invariant), so cycle starts must land ON the beat grid.

Cases:
  [grid]     120 BPM, synced 1/4: every cycle start after bar 1 is on a beat.
  [repeat]   two renders whose song position starts at different offsets land on
             the SAME phase once locked (the reproducibility the derivation
             buys); the sync-off control shows the same measurement telling the
             two apart by 0.37 beats, so the pass is the lock, not the tolerance.
  [acquire]  transport starts mid-note with the free-run phase 0.26 cycles off
             the grid: the phase must SLEW onto the grid (no step in the
             modulation -- a step is a click on every route it feeds) and be on
             the grid within the bar.
"""
import sys
import numpy as np
from _harness import render

BPM = 120.0
BEAT_S = 60.0 / BPM          # 0.5 s
LFO_RATE = 2.0               # Hz at 120 BPM -> 2/rate = 1 beat per cycle (1/4)
GRID_TOL_S = 0.005           # spec: extrema on the beat grid +-5 ms
PHASE_TOL_BEATS = 0.002      # "identical phase" between two locked renders (1 ms)
SMOOTH_S = 0.002             # envelope de-ripple; ~2 carrier periods at 1 kHz

# LFO1 -> Amplitude, depth 1.0. Everything that could colour the envelope
# (osc2/3, sub, noise, filter env, analog drift, FX) is off; masterVol keeps the
# 0..2 amplitude modulation clear of the output soft limiter.
PATCH = dict(osc1Wave=3, osc2Level=0, osc3Level=0, subLevel=0, noiseLevel=0,
             analogAmt=0, filterCutoff=12000, filterRes=0, filterEnvAmt=0,
             ampA=0.001, ampD=0.001, ampS=1.0, ampR=0.05, unisonVoices=1,
             cosmosChorus=0, reverbOn=0, delayOn=0, chorusOn=0, driveOn=0,
             vintage=0, masterVol=-12,
             lfo1Shape=1, lfo1Rate=LFO_RATE, lfo1Fade=0, lfo1Sync=1,
             modSrc0=1, modDst0=7, modAmt0=1.0)   # src LFO1 -> dst Amplitude


def envelope(x):
    """Analytic-signal envelope (Hilbert magnitude), numpy only."""
    n = len(x)
    X = np.fft.fft(x)
    h = np.zeros(n)
    h[0] = 1.0
    if n % 2 == 0:
        h[n // 2] = 1.0
        h[1:n // 2] = 2.0
    else:
        h[1:(n + 1) // 2] = 2.0
    return np.abs(np.fft.ifft(X * h))


def boxcar(sig, sr, seconds):
    w = max(1, int(seconds * sr))
    return np.convolve(sig, np.ones(w) / w, mode="same")


def cycle_starts(env, sr, t_from):
    """Times of the envelope's zero-dips = LFO cycle starts, after t_from.

    Found independently of any expected grid: runs below a fraction of the
    envelope's own median, each contributing its minimum. A gate that searched
    near the expected beats would only ever confirm itself.

    The envelope carries ~30% ripple at the carrier period, which crosses the
    threshold repeatedly inside one dip, so detection runs on a boxcar-smoothed
    copy (symmetric, so the minimum of a V does not move) and runs closer
    together than MERGE_S are one dip. The reported time is the RAW envelope's
    minimum inside that dip.
    """
    MERGE_S = 0.05      # << the shortest cycle here (0.5 s), >> the ripple
    lo = int(t_from * sr)
    seg = env[lo:]
    sm = boxcar(seg, sr, SMOOTH_S)
    below = sm < 0.12 * float(np.median(sm))

    # Contiguous below-threshold runs, then merged across short gaps.
    edges = np.flatnonzero(np.diff(below.astype(np.int8)))
    bounds = np.concatenate(([0], edges + 1, [len(seg)]))
    runs = [(bounds[i], bounds[i + 1]) for i in range(len(bounds) - 1)
            if below[bounds[i]]]
    merged = []
    for a, b in runs:
        if merged and (a - merged[-1][1]) < int(MERGE_S * sr):
            merged[-1] = (merged[-1][0], b)
        else:
            merged.append((a, b))

    starts = []
    for a, b in merged:
        if a == 0 or b == len(seg):
            continue                       # dip truncated by the window edge
        starts.append((lo + a + int(np.argmin(seg[a:b]))) / sr)
    return np.array(starts)


def grid_residual_beats(times, song_offset):
    """Signed distance from each cycle start to the nearest beat, in beats."""
    beats = song_offset + np.asarray(times) * BPM / 60.0
    return beats - np.round(beats)


def case_grid():
    """Cycle starts land on the host beat grid after bar 1."""
    sr, x = render(0, 84, 4.0, 2, "lfo_sync_grid", tempo=BPM, playing=1,
                   songpos=0.0, **PATCH)
    starts = cycle_starts(envelope(x[:, 0]), sr, t_from=2.0)   # after bar 1 (4/4)
    if len(starts) < 3:
        print(f"[grid]    FAIL (only {len(starts)} cycle starts found after bar 1)")
        return False
    dev_s = np.abs(grid_residual_beats(starts, 0.0)) * BEAT_S
    worst = float(np.max(dev_s))
    ok = worst <= GRID_TOL_S
    print(f"[grid]    {len(starts)} cycle starts after bar 1, worst off-grid "
          f"{worst * 1000:.2f} ms  {'PASS' if ok else 'FAIL'} "
          f"(tol +-{GRID_TOL_S * 1000:.0f} ms)")
    return ok


def locked_phase(offset, sync, tag):
    """Mean grid residual (in beats) of the cycle starts, after bar 1."""
    sr, x = render(0, 84, 4.0, 2, f"lfo_sync_{tag}", tempo=BPM, playing=1,
                   songpos=offset, **{**PATCH, "lfo1Sync": 1 if sync else 0})
    starts = cycle_starts(envelope(x[:, 0]), sr, t_from=2.0)
    if len(starts) < 3:
        return None
    return float(np.mean(grid_residual_beats(starts, offset)))


def case_repeat():
    """Two song-position offsets -> the same phase once locked."""
    a = locked_phase(0.0, True, "rep_a")
    b = locked_phase(0.37, True, "rep_b")
    # Control: with sync OFF the same measurement must SEPARATE the two renders,
    # which is what proves the [repeat] pass is the lock and not a blunt metric.
    ca = locked_phase(0.0, False, "ctl_a")
    cb = locked_phase(0.37, False, "ctl_b")
    if None in (a, b, ca, cb):
        print("[repeat]  FAIL (no cycle starts detected in one of the renders)")
        return False
    d = abs(a - b)
    cd = abs(ca - cb)
    ok = (d <= PHASE_TOL_BEATS) and (cd > 10 * PHASE_TOL_BEATS)
    print(f"[repeat]  synced phase {a:+.5f} vs {b:+.5f} beats -> delta {d:.5f} "
          f"(tol {PHASE_TOL_BEATS})   free-run control delta {cd:.5f} (must differ)  "
          f"{'PASS' if ok else 'FAIL'}")
    return ok


def case_acquire():
    """Transport starts mid-note: slew onto the grid, no step in the modulation."""
    PLAY_AT = 1.13   # free phase at lock = frac(2*1.13) = 0.26 cycles off the grid
    sr, x = render(0, 84, 6.0, 2, "lfo_sync_acquire", tempo=BPM, playing=1,
                   songpos=0.0, playat=PLAY_AT, **PATCH)
    env = envelope(x[:, 0])

    # Continuity: envelope change across a 5 ms window, measured on the
    # de-rippled envelope (raw carrier-period ripple is 10x the LFO's own slope
    # and would swamp the comparison). Nominal is the LFO's own slope; the slew
    # is allowed up to 2x nominal rate by construction, a phase SNAP is an order
    # out -- up to full scale inside one sample.
    w = int(0.0025 * sr)
    sm = boxcar(env, sr, SMOOTH_S)
    slope = np.abs(sm[2 * w:] - sm[:-2 * w])
    t = (np.arange(len(slope)) + w) / sr
    free = slope[(t > 0.2) & (t < PLAY_AT - 0.05)]
    near = slope[(t > PLAY_AT - 0.05) & (t < PLAY_AT + 0.05)]
    nominal = float(np.percentile(free, 99.0))
    jump = float(np.max(near))
    cont_ok = jump <= 3.0 * nominal

    # And it must actually be on the grid a bar later. Song position is 0 at the
    # moment the transport rolls, so the beat grid starts there.
    starts = cycle_starts(env, sr, t_from=PLAY_AT + 2.0)
    if len(starts) < 3:
        print(f"[acquire] FAIL (only {len(starts)} cycle starts after the lock)")
        return False
    dev_s = np.abs(grid_residual_beats(starts - PLAY_AT, 0.0)) * BEAT_S
    worst = float(np.max(dev_s))
    grid_ok = worst <= GRID_TOL_S

    ok = cont_ok and grid_ok
    print(f"[acquire] envelope step at lock {jump:.4f} vs {nominal:.4f} nominal "
          f"({jump / max(nominal, 1e-12):.2f}x, limit 3x)  "
          f"post-lock worst off-grid {worst * 1000:.2f} ms  "
          f"{'PASS' if ok else 'FAIL'}")
    return ok


def main():
    ok = case_grid()
    ok = case_repeat() and ok
    ok = case_acquire() and ok
    print(f"lfo_sync_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
