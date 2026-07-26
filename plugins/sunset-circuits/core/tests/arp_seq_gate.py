#!/usr/bin/env python3
"""Arpeggiator / acid-sequencer correctness gate.

arp_gate.py covers the STEP CLOCK: that the grid is on tempo and phase-locks to
the host. This one covers what the arp puts ON that clock -- swing parity, the
pitches it generates, which of the 16 grid cells the mute and accent rows
address -- plus the acid sequencer's enable edge, which strands a live note.

A. SWING TEMPO INTEGRITY (patternSize 1..5, swing 0.8)
    Symmetric swing lengthens the even step and shortens the odd one by the same
    amount, so a pair spans two grid steps and the mean stays on tempo. The
    parity has to come from the MONOTONIC step counter: taking it from the
    pattern position, which wraps at patternSize, flips the pairing every cycle
    at odd patternSize, and then the long and short steps stop cancelling -- the
    arp does not merely swing wrong, it runs at the wrong TEMPO.

    Mean step over a 4 s free run at 120 BPM, 1/16 (nominal 125.00 ms):

        patternSize   pre-fix              post-fix
             1        175.02 ms (+40.0%)   125.01 ms (+0.0%)
             2        125.01 ms ( +0.0%)   125.01 ms (+0.0%)
             3        140.40 ms (+12.3%)   125.01 ms (+0.0%)
             4        125.01 ms ( +0.0%)   125.01 ms (+0.0%)
             5        132.15 ms ( +5.7%)   125.01 ms (+0.0%)

    One held note is the common case and was the worst: every step took the
    "even" lengthening, so the arp ran 40% slow with no swing at all. Gated at
    2% on the mean AND on strict long/short alternation, because a mean can be
    right while the pairing is not (patternSize 3 pre-fix rendered two long
    steps back to back).

B. OCTAVE EXPANSION (octave range 4, held {100, 104, 107}, OS=Off)
    Expansion added 12 semitones per octave with no range check, so the pattern
    ran off the top of MIDI: ... 128 131 136 140 143. The voice tunes to those
    literally, and at 1x note 143 is 31.6 kHz nominal -- above Nyquist, so it
    renders as a WRONG PITCH, not a high note. Post-fix the pattern stops at
    124. Measured per step against the nominal frequency: 14/14 steps match
    post-fix, 13/14 mismatch pre-fix (24000 Hz, 13290 Hz, 21420 Hz, ...).

C. GRID ROWS ARE 16 CELLS, NOT patternSize CELLS
    The mute row and the accent patterns were indexed by the pattern position,
    which wraps at patternSize, so with fewer than 16 notes held -- i.e. almost
    always -- the cells at and above patternSize were unreachable while the UI
    drew all 16 as live controls.

        patternSize 3, muting one cell     pre-fix                 post-fix
          cell 0                           gaps at 0,3,6,9,12,15   gap at 0
          cell 4                           NO EFFECT               gap at 4
          cell 9                           NO EFFECT               gap at 9
          cell 15                          NO EFFECT               gap at 15

    All 16 cells are checked individually, each in its own render.

    Same bug in the accent patterns through getVelocity's step % 4. At
    patternSize 1 the step index was pinned to 0, so Downbeat accented EVERY
    note: measured -14.1 dB on every step pre-fix (margin -0.0 dB), and -14.2 on
    steps 0,4,8,12 against -17.1 elsewhere post-fix (margin 2.9 dB).

    All FOUR accent patterns are now reachable: arpAccentPattern (core param
    index 222) drives Arpeggiator::setAccentPattern, which getVelocity consumes
    ONLY in velocity mode 2 (AccentPattern). Because the shape is indexed off the
    16-cell GRID and not off the held-note pattern, the same step->level sequence
    must come out at EVERY patternSize -- which is exactly what the C fix above
    bought -- so each pattern is measured at patternSize 1 and 3 and the two runs
    are required to agree. Measured (patternSize 1, first 16 steps, dBFS):

        Downbeat     -14.1 -17.1 -17.1 -17.1  -14.2 -17.1 -17.1 -17.1  ...
        Every Other  -14.1 -17.1 -14.2 -17.1  -14.2 -17.1 -14.2 -17.1  ...
        Ramp Up      -18.9 -18.1 -17.4 -16.6  -16.0 -15.3 -14.8 -14.2  (repeats)
        Ramp Down    -14.1 -14.8 -15.3 -16.0  -16.6 -17.4 -18.1 -19.0  (repeats)

    The ramps span 8 grid cells (step % 8) and so run twice across the 16-cell
    row; the alternation patterns span 2 and 4. Gated on shape, not on absolute
    level: alternation needs the same >= 2.0 dB margin as Downbeat, and the ramps
    need strict monotonicity over each 8-cell run plus a >= 3.0 dB total span.

D. ACID SEQUENCER ENABLE EDGE
    Note-on and note-off each pick their routing from the CURRENT arpOn, so
    toggling the sequencer under a held note breaks the pair. Only the disable
    direction was handled: enabling it under a live note sent that note's key-up
    to the sequencer, which never reached the acidVoice the live path had gated.

        note held from 0 s, sequencer on at 1.0 s, key-up at 1.5 s
          0.2-0.9 s    -18.4 dBFS  (sounding, both builds)
          2.0-4.9 s    -18.4 dBFS  pre-fix -- droning, key-up never arrived
                      -400.0 dBFS  post-fix
        control, key-up at 0.5 s (before the edge)
          2.0-4.9 s   -400.0 dBFS  both builds
"""
import sys
import numpy as np
from _harness import render, peak_envelope, rising_edges, peak_hz

BPM = 120.0
STEP_S = 0.125               # 1/16 at 120 BPM
SR = 48000

# Short, percussive, one sine, nothing downstream: every step has to be one
# clean onset and one measurable pitch.
BASE = dict(sr=SR, osc1Wave=3, osc2Level=0, osc3Level=0, subLevel=0,
            noiseLevel=0, analogAmt=0, vintage=0, cosmosChorus=0,
            filterCutoff=20000, filterRes=0, filterEnvAmt=0,
            reverbOn=0, delayOn=0, chorusOn=0, driveOn=0,
            arpOn=1, arpMode=0, arpRate=4, arpGate=0.6, arpSwing=0,
            arpOctave=1, ampA=0.001, ampD=0.2, ampS=1.0, ampR=0.005,
            masterVol=-6)


def onsets(x, sr, min_gap_s=0.05):
    return rising_edges(peak_envelope(x[:, 0], sr, 0.003), sr, 0.3, min_gap_s)


def midi_hz(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


# --- A. swing tempo integrity -------------------------------------------------
SWING = 0.8
MEAN_TOL = 0.02              # 2% of nominal
SWING_TOL = 0.03             # 3% on each individual long/short step
CHORD = [60, 64, 67, 71, 74]
PRE_FIX_MEAN_MS = {1: 175.02, 2: 125.01, 3: 140.40, 4: 125.01, 5: 132.15}


def swing_tempo(fails):
    print("A. swing tempo integrity (120 BPM, 1/16, swing 0.8)")
    print(f"   {'patternSize':<13}{'mean ms':>9}{'err':>8}{'long ms':>9}{'short ms':>9}"
          f"{'alternates':>12}{'pre-fix ms':>12}   result")
    long_ideal, short_ideal = STEP_S * (1 + SWING / 2), STEP_S * (1 - SWING / 2)
    for n in (1, 2, 3, 4, 5):
        kw = dict(BASE, arpSwing=SWING)
        if n > 1:
            kw["hold"] = ",".join(str(v) for v in CHORD[1:n])
        _, x = render(0, CHORD[0], 4.0, 2, f"arpseq_swing_{n}",
                      tempo=BPM, playing=1, **kw)
        on = onsets(x, SR, 0.04)
        iv = np.diff(on[1:])          # drop step 1, same as arp_gate
        if len(iv) < 8:
            print(f"   patternSize {n}: FAIL (only {len(on)} onsets)")
            fails.append(f"A/{n}")
            continue
        mean = float(np.mean(iv))
        err = mean / STEP_S - 1.0

        # Strict alternation: every interval is within tolerance of exactly one
        # of the two ideal lengths, and consecutive intervals never share a
        # class. A correct mean with a broken pairing fails here.
        cls = []
        classified = True
        for v in iv:
            if abs(v - long_ideal) <= SWING_TOL * long_ideal:
                cls.append(1)
            elif abs(v - short_ideal) <= SWING_TOL * short_ideal:
                cls.append(0)
            else:
                classified = False
                cls.append(-1)
        alternates = classified and all(a != b for a, b in zip(cls, cls[1:]))
        longs = [v for v, c in zip(iv, cls) if c == 1]
        shorts = [v for v, c in zip(iv, cls) if c == 0]
        ok = abs(err) <= MEAN_TOL and alternates
        if not ok:
            fails.append(f"A/{n}")
        print(f"   {n:<13}{mean * 1000:>9.2f}{err * 100:>7.1f}%"
              f"{(np.mean(longs) * 1000 if longs else float('nan')):>9.1f}"
              f"{(np.mean(shorts) * 1000 if shorts else float('nan')):>9.1f}"
              f"{('yes' if alternates else 'NO'):>12}{PRE_FIX_MEAN_MS[n]:>12.2f}   "
              f"{'PASS' if ok else 'FAIL'}")


# --- B. octave expansion ------------------------------------------------------
OCT_HELD = [100, 104, 107]
OCT_RANGE = 4
# Every held note plus 12/24/36 semitones that still fits in MIDI, ascending.
OCT_PATTERN = sorted(n + 12 * o for o in range(OCT_RANGE) for n in OCT_HELD
                     if n + 12 * o <= 127)
OCT_STEPS = 14
PITCH_TOL = 0.02             # 2% -- a semitone is 5.9%


def octave_range(fails):
    print(f"\nB. octave expansion (range {OCT_RANGE}, held {OCT_HELD}, OS=Off)")
    _, x = render(0, OCT_HELD[0], 2.5, 1, "arpseq_octave", tempo=BPM, playing=1,
                  hold=",".join(str(n) for n in OCT_HELD[1:]),
                  **dict(BASE, arpOctave=OCT_RANGE))
    on = onsets(x, SR)
    if len(on) < OCT_STEPS:
        print(f"   FAIL (only {len(on)} onsets)")
        fails.append("B")
        return
    bad = []
    got = []
    for k in range(OCT_STEPS):
        i = int(on[k] * SR) + int(0.01 * SR)
        f = peak_hz(x[i:i + 2048, 0], SR, 200.0)
        want = midi_hz(OCT_PATTERN[k % len(OCT_PATTERN)])
        got.append((f, want))
        if abs(f / want - 1.0) > PITCH_TOL:
            bad.append(k)
    print(f"   pattern {OCT_PATTERN} ({len(OCT_PATTERN)} notes, nothing above 127)")
    print("   rendered/nominal Hz: " +
          "  ".join(f"{f:.0f}/{w:.0f}" for f, w in got[:7]))
    ok = not bad
    if not ok:
        fails.append("B")
        print(f"   mismatched steps: {bad}")
    print(f"   {OCT_STEPS - len(bad)}/{OCT_STEPS} steps on pitch  "
          f"(pre-fix 1/14)   {'PASS' if ok else 'FAIL'}")


# --- C. the 16-cell grid rows -------------------------------------------------
GRID_SECONDS = 2.2
GRID_CELLS = 16


def grid_rows(fails):
    print("\nC. grid rows address all 16 cells at patternSize 3")
    wrong = []
    for cell in range(GRID_CELLS):
        kw = dict(BASE)
        kw[f"arpStep{cell}"] = 0
        _, x = render(0, 60, GRID_SECONDS, 2, f"arpseq_mute_{cell}",
                      tempo=BPM, playing=1, hold="64,67", **kw)
        on = onsets(x, SR)
        # Which of steps 0..15 produced no onset. Step 16 aliases onto cell 0
        # (absStep & 15), so only the first 16 are judged.
        silent = [k for k in range(GRID_CELLS)
                  if not np.any(np.abs(on - k * STEP_S) < 0.02)]
        if silent != [cell]:
            wrong.append((cell, silent))
    ok = not wrong
    if not ok:
        fails.append("C/mute")
        for cell, silent in wrong:
            print(f"   mute cell {cell:2d}: silent steps {silent}, expected [{cell}]")
    print(f"   {GRID_CELLS - len(wrong)}/{GRID_CELLS} cells mute their own step and "
          f"nothing else   (pre-fix: 1/16 -- cell 0 muted every 3rd step, cells "
          f"3-15 did nothing)   {'PASS' if ok else 'FAIL'}")


def step_levels(name, **kw):
    """Peak dBFS of the first 16 arp steps, or None if too few onsets."""
    _, x = render(0, 60, GRID_SECONDS, 2, name, tempo=BPM, playing=1, **kw)
    on = onsets(x, SR)
    lv = [20.0 * np.log10(np.max(np.abs(x[int(t * SR):int(t * SR) + int(0.04 * SR), 0]))
                          + 1e-20)
          for t in on[:GRID_CELLS]]
    return lv if len(lv) == GRID_CELLS else None


# --- C2. the four accent patterns --------------------------------------------
# arpAccentPattern (core index 222) -> Arpeggiator::setAccentPattern, consumed
# only by getVelocity's AccentPattern branch (arpVelMode 2).
ACCENT_LOUD_DB = 2.0         # accented steps must stand this far above the rest
RAMP_SPAN_DB = 3.0           # quietest -> loudest cell of one 8-cell ramp run
RAMP_STEP_DB = 0.0           # every ramp step must move the right way, no plateau
CROSS_SIZE_TOL_DB = 0.6      # patternSize 1 vs 3 must render the same shape


def accent_shape(idx, lv):
    """(ok, description) for accent pattern `idx` given 16 step levels."""
    if idx in (0, 1):
        period = 4 if idx == 0 else 2
        loud = [lv[k] for k in range(GRID_CELLS) if k % period == 0]
        soft = [lv[k] for k in range(GRID_CELLS) if k % period != 0]
        margin = min(loud) - max(soft)
        return (margin >= ACCENT_LOUD_DB,
                f"loud {np.mean(loud):6.1f}  soft {np.mean(soft):6.1f}  "
                f"margin {margin:5.1f} dB (need >= {ACCENT_LOUD_DB:.1f})")
    # Ramps: two independent 8-cell runs, each strictly monotone with real span.
    up = (idx == 2)
    ok, spans = True, []
    for run in (lv[0:8], lv[8:16]):
        d = np.diff(run)
        ok = ok and (np.all(d > RAMP_STEP_DB) if up else np.all(d < -RAMP_STEP_DB))
        spans.append(abs(run[-1] - run[0]))
    ok = ok and min(spans) >= RAMP_SPAN_DB
    return (ok, f"runs {lv[0]:6.1f}->{lv[7]:6.1f} and {lv[8]:6.1f}->{lv[15]:6.1f}  "
                f"span {min(spans):4.1f} dB (need >= {RAMP_SPAN_DB:.1f}, monotone)")


def accent_patterns(fails):
    print("\nC2. accent patterns (arpVelMode=Accent, arpAccentPattern 0..3)")
    print("    the shape is keyed to the 16-cell GRID, so patternSize must not "
          "change it")
    for idx, nm in enumerate(("Downbeat", "Every Other", "Ramp Up", "Ramp Down")):
        # patternSize 1 (one held note) and 3 (a triad) -- the two ends of the
        # grid-vs-pattern indexing bug that C fixed.
        lvs = {}
        for psize, hold in ((1, None), (3, "64,67")):
            kw = dict(BASE, arpVelMode=2, arpAccentPattern=idx)
            if hold:
                kw["hold"] = hold
            lvs[psize] = step_levels(f"arpseq_acc{idx}_p{psize}", **kw)
            if lvs[psize] is None:
                print(f"   {nm:<12} patternSize {psize}: FAIL (too few onsets)")
                fails.append(f"C2/{idx}/{psize}")
        if any(v is None for v in lvs.values()):
            continue
        shapes = {p: accent_shape(idx, v) for p, v in lvs.items()}
        drift = float(np.max(np.abs(np.array(lvs[1]) - np.array(lvs[3]))))
        same = drift <= CROSS_SIZE_TOL_DB
        ok = shapes[1][0] and shapes[3][0] and same
        if not ok:
            fails.append(f"C2/{idx}")
        for psize in (1, 3):
            print(f"   {nm if psize == 1 else '':<12} n={psize}  {shapes[psize][1]}"
                  f"   {'ok' if shapes[psize][0] else 'BAD'}")
        print(f"   {'':<12} n=1 vs n=3 max |delta| {drift:.2f} dB "
              f"(limit {CROSS_SIZE_TOL_DB:.1f})   {'PASS' if ok else 'FAIL'}")

    # Host-locked arm: advanceLocked has its own accent lookup (globalStep-based)
    # so the free-run cases above cannot vouch for it. Every Other alternation is
    # the sharpest discriminator -- an unwired or mis-indexed locked path reads
    # as Downbeat and collapses the margin to ~0.
    # songpos 0.31 (same trick as arp_gate): at songpos 0.0 the frame-0 step
    # races the hold note-on and the first AUDIBLE onset is grid step 1, which
    # inverts the measured alternation. From 0.31 beats the first fired step is
    # global step 2 -- an EVEN offset, so Every Other's parity is preserved and
    # accent_shape stays valid. Only parity-safe patterns may use this arm.
    lv = step_levels("arpseq_acc1_locked", songpos=0.31,
                     **dict(BASE, arpVelMode=2, arpAccentPattern=1))
    if lv is None:
        print("   locked EveryOther: FAIL (too few onsets)")
        fails.append("C2/locked")
    else:
        ok, desc = accent_shape(1, lv)
        print(f"   locked (songpos) Every Other  {desc}   "
              f"{'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("C2/locked")


# --- D. acid sequencer enable edge --------------------------------------------
ACID = dict(sr=SR, analogAmt=0, vintage=0, noiseLevel=0, cosmosChorus=0,
            driveOn=0, reverbOn=0, delayOn=0, chorusOn=0,
            filterCutoff=8000, filterRes=0.1, filterEnvAmt=0,
            ampA=0.005, ampD=0.05, ampS=1.0, ampR=0.05, masterVol=-6)
STRANDED_MAX_DB = -90.0


def rms_db(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr), 0]
    return 20.0 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-20)


def acid_seq_edge(fails):
    print("\nD. acid sequencer enabled under a held note")
    _, x = render(5, 45, 5.0, 2, "arpseq_acid_edge", release=1.5,
                  setat="1.0:arpOn:1", **ACID)
    held = rms_db(x, SR, 0.2, 0.9)
    after = rms_db(x, SR, 2.0, 4.9)
    _, xc = render(5, 45, 5.0, 2, "arpseq_acid_ctl", release=0.5,
                   setat="1.0:arpOn:1", **ACID)
    ctl = rms_db(xc, SR, 2.0, 4.9)
    sounded = held > -40.0            # the note has to have played at all
    ok = sounded and after < STRANDED_MAX_DB and ctl < STRANDED_MAX_DB
    if not ok:
        fails.append("D")
    print(f"   held 0.2-0.9 s {held:8.1f} dBFS (must sound)")
    print(f"   after key-up 2.0-4.9 s {after:8.1f} dBFS  (limit {STRANDED_MAX_DB:.0f}, "
          f"pre-fix -18.4)")
    print(f"   control, key-up before the edge {ctl:8.1f} dBFS   "
          f"{'PASS' if ok else 'FAIL'}")


def main():
    fails = []
    swing_tempo(fails)
    octave_range(fails)
    grid_rows(fails)
    accent_patterns(fails)
    acid_seq_edge(fails)
    print(f"\narp_seq_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
