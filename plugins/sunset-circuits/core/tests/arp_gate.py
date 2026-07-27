#!/usr/bin/env python3
"""Arpeggiator timing + pattern-shape gate.

120 BPM, 1/8 pattern (0.25 s/step), 3 held notes, Up. Detects note onsets and
checks the inter-onset intervals (after step 1) are on the grid within +-1 ms.

Also checks the DownUp pattern SHAPE against octave clipping (see downup_return).
"""
import sys
import numpy as np
from _harness import render, peak_envelope, rising_edges, peak_hz

BPM = 120.0
STEP_S = 60.0 / BPM * 0.5   # 1/8 note = 0.25 s
TOL_S = 0.001

PATCH = dict(osc1Wave=3, osc2Level=0, subLevel=0, noiseLevel=0, analogAmt=0,
             filterEnvAmt=0, filterCutoff=6000, filterRes=0, reverbOn=0, cosmosChorus=0,
             arpOn=1, arpMode=0, arpRate=3, arpGate=0.5, arpOctave=1,
             ampA=0.001, ampD=0.04, ampS=0.2, ampR=0.02)


def detect_onsets(sig, sr, thresh_frac=0.3, min_gap_s=0.1):
    # Peak envelope follower: instant attack, ~3 ms release. Detects rising
    # edges (note onsets) on the smoothed envelope, immune to the carrier's
    # per-cycle zero crossings.
    return rising_edges(peak_envelope(sig, sr, 0.003), sr, thresh_frac, min_gap_s)


def free_run():
    sr, x = render(0, 60, 2.0, 2, "arp", tempo=BPM, playing=1, hold="64,67", **PATCH)
    onsets = detect_onsets(x[:, 0], sr)
    if len(onsets) < 4:
        print(f"[free]  FAIL (only {len(onsets)} onsets detected)")
        return False

    intervals = np.diff(onsets[1:])   # ignore the first (step 1) as spec allows
    worst = float(np.max(np.abs(intervals - STEP_S)))
    ok = worst <= TOL_S
    print(f"[free]  onsets {len(onsets)}  (first at {onsets[0]*1000:.1f} ms)  "
          f"ideal {STEP_S*1000:.1f} ms  worst dev {worst*1000:.2f} ms  "
          f"{'PASS' if ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")
    return ok


def host_locked():
    # Host phase-lock: song position 0.31 beats at frame 0, note pressed at t=0,
    # 120 BPM 1/8. Strict quantize -> nothing sounds until the next 1/8 grid
    # boundary of the SONG position: beat 0.5 - 0.31 = 0.19 beats -> 95 ms. Every
    # later onset lands on the absolute host grid (250 ms apart).
    SONGPOS = 0.31
    FIRST_S = (0.5 - SONGPOS) * 60.0 / BPM   # 0.095 s
    FIRST_TOL_S = 0.002
    sr, x = render(0, 60, 2.0, 2, "arp_locked", tempo=BPM, playing=1,
                   songpos=SONGPOS, hold="64,67", **PATCH)
    onsets = detect_onsets(x[:, 0], sr)
    if len(onsets) < 4:
        print(f"[locked] FAIL (only {len(onsets)} onsets detected)")
        return False

    first_dev = abs(onsets[0] - FIRST_S)
    intervals = np.diff(onsets)
    worst = float(np.max(np.abs(intervals - STEP_S)))
    ok = (first_dev <= FIRST_TOL_S) and (worst <= TOL_S)
    print(f"[locked] onsets {len(onsets)}  first {onsets[0]*1000:.1f} ms "
          f"(grid {FIRST_S*1000:.1f} ms, dev {first_dev*1000:.2f} ms, tol +-{FIRST_TOL_S*1000:.0f})  "
          f"step worst dev {worst*1000:.2f} ms  {'PASS' if ok else 'FAIL'} (tol +-{TOL_S*1000:.0f} ms)")
    return ok


# DownUp pattern shape under octave clipping.
#
# Octave expansion SKIPS any note it pushes above MIDI 127 (pushPattern), so the
# descending run that DownUp builds is shorter than heldCount * octaveRange. The
# return leg used to be RECOMPUTED from the sorted held notes instead of read back
# out of the pattern, which addresses the unclipped enumeration and therefore
# re-derives exactly the notes that were skipped -- pushPattern rejected them a
# second time and the climb came out short.
#
# Held 60 + 120 at octave range 4. Expansion asks for 60, 120, 72, 132, 84, 144,
# 96, 156; everything above 127 is dropped, leaving the 5-note run 60/120/72/84/96.
# DownUp reverses it and climbs back through the interior:
#
#     96 84 72 120 60 | 120 72 84        <- 8 steps, the return leg mirrors the down
#     96 84 72 120 60 | 120 72           <- 7 steps, what the recomputation built
#
# Pitches are read on the FIXED STEP GRID (1/8 at 120 BPM, free-run from t=0), not
# from detected onsets: note 120 is 8372 Hz and sits a few dB under the rest, so an
# amplitude-threshold onset detector merges and shifts edges even though every step
# is present. The grid is exact to under 1 ms over this length -- free_run() above
# is what proves that -- and the patch is a bare sine with the filter wide open, so
# each mid-gate window reads its note number to a hundredth of a semitone.
#
# Two full cycles are measured, so a wrong pattern LENGTH also fails: a 7-step
# cycle puts step 8 on 96 where an 8-step cycle repeats 84.
DOWNUP_PATCH = dict(osc1Wave=3, osc2Level=0, osc3Level=0, subLevel=0, noiseLevel=0,
                    analogAmt=0, filterEnvAmt=0, filterCutoff=20000, filterRes=0,
                    reverbOn=0, delayOn=0, cosmosChorus=0,
                    arpOn=1, arpMode=3, arpRate=3, arpGate=0.6, arpOctave=4,
                    ampA=0.001, ampD=0.04, ampS=0.6, ampR=0.02)
DOWNUP_EXPECTED = [96, 84, 72, 120, 60, 120, 72, 84]
NOTE_TOL = 0.2   # semitones; measured worst deviation is 0.01
STEP_MIN_RMS = 0.01


def downup_return():
    cycles = 2
    n = len(DOWNUP_EXPECTED)
    sr, x = render(0, 60, n * cycles * STEP_S + 0.2, 2, "arp_downup",
                   tempo=BPM, playing=1, hold="120", **DOWNUP_PATCH)
    mono = x[:, 0]
    notes, quiet = [], []
    for k in range(n * cycles):
        a = int((k * STEP_S + 0.03) * sr)
        b = int((k * STEP_S + 0.13) * sr)
        seg = mono[a:b]
        if float(np.sqrt(np.mean(seg ** 2))) < STEP_MIN_RMS:
            quiet.append(k)
        notes.append(69.0 + 12.0 * np.log2(peak_hz(seg, sr) / 440.0))

    want = DOWNUP_EXPECTED * cycles
    worst = max(abs(m - w) for m, w in zip(notes, want))
    # Assert the MEASURED cycle is a mirror in its own right, not just equal to the
    # literal above: an 8-step DownUp is a 5-note down leg plus its 3-note interior
    # climbed back (n = base + (base - 2), so base = (n + 2) / 2). A change that
    # keeps the length but scrambles the climb fails here.
    meas = [round(v) for v in notes[:n]]
    base = (n + 2) // 2
    mirror_ok = meas[base:] == list(reversed(meas[1:base - 1]))
    ok = (worst <= NOTE_TOL) and not quiet and mirror_ok
    print(f"[downup] steps {[round(v) for v in notes]}")
    print(f"         want  {want}  worst dev {worst:.2f} st (tol {NOTE_TOL}), "
          f"silent steps {quiet if quiet else 'none'}, mirror={mirror_ok}  "
          f"{'PASS' if ok else 'FAIL'}")
    return ok


def main():
    ok = free_run()
    ok = host_locked() and ok
    ok = downup_return() and ok
    print(f"arp_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
