#!/usr/bin/env python3
"""Legato gate: a legato note ties to a HELD note and never to a dying one.

LEGATO ("Glide only when notes overlap") ties a note that lands on a voice whose
note is still held: the envelopes run on, only the pitch moves, and it glides
when Glide is up. Any other note starts one of its own and does not glide.

The tie used to be decided by "the amp envelope is active", which is also true
in RELEASE. Every arp step (released before the next starts) and every voice
steal (which released the voice first) then tied to a dying note and rode its
release down to silence: 58 of 64 Mono arp steps and 51 of 64 Cosmos steps went
silent with a 1 s release, and a held Mono legato note was gone within 2 s.

Scenarios (48k, 1x OS, plain square through an open filter, dry):
  a. arp, gate 50 %    : Cosmos and Mono, release 1 s -- no step more than 20 dB
                         below the first, with legato on as with it off, and the
                         first above -50 dBFS (a silent render has no step below
                         its own first one)
  b. live Mono tie     : 60 held, 64 added at 1.0 s, both held -- the level holds
                         (within 1 dB) instead of dying, and does not re-attack
                         (ampS 0.5 patch: legato peak stays at sustain, while the
                         legato-off control re-attacks, proving the probe sees it)
  c. arp, gate 100 %   : legato ties the steps, in Mono and in Cosmos -- per-step
                         level swing under 2 dB with no silent floor; the
                         legato-off control re-attacks every step (swing over
                         3 dB). Cosmos is the poly case: each step has to take
                         over the outgoing step's voice, where the allocator
                         would start it on a free one with an attack of its own
  d. glide             : glide 0.2 s, 60 -> 72. Legato on: a detached note lands
                         on pitch (no glide), an overlapping one glides. Legato
                         off: both glide (control, unchanged behaviour)
  e. poly steal        : Cosmos, six held notes, a seventh pressed at 1.0 s steals
                         a held voice; with legato on it ties and SOUNDS (its
                         fundamental is present 1-2 s later at the legato-off
                         level, within 6 dB)
"""
import sys

import numpy as np

from _harness import render

BPM, STEP = 120.0, 0.125
ABS_FLOOR_DB = -50.0
BASE = dict(osc1Wave=1, osc2Level=0, subLevel=0, noiseLevel=0, analogAmt=0,
            filterEnvAmt=0, filterCutoff=6000, filterRes=0, reverbOn=0, delayOn=0,
            cosmosChorus=0)


def db(v):
    return 20.0 * np.log10(max(float(v), 1e-12))


def rms(x, sr, t0, t1):
    a = x[int(t0 * sr):int(t1 * sr), 0]
    return float(np.sqrt(np.mean(a * a)))


def win_rms(x, sr, t0, t1, win):
    a = x[int(t0 * sr):int(t1 * sr), 0]
    n = int(win * sr)
    k = len(a) // n
    return np.sqrt(np.mean(a[:k * n].reshape(k, n) ** 2, axis=1))


def zc_hz(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr), 0]
    zc = np.flatnonzero((seg[1:] >= 0) & (seg[:-1] < 0))
    return (len(zc) - 1) / ((zc[-1] - zc[0]) / sr) if len(zc) > 2 else 0.0


def band_db(x, sr, t0, t1, f0, half_bw=15.0):
    seg = x[int(t0 * sr):int(t1 * sr), 0] * np.hanning(int(t1 * sr) - int(t0 * sr))
    spec = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(len(seg), 1.0 / sr)
    band = (f > f0 - half_bw) & (f < f0 + half_bw)
    return db(float(np.sqrt(np.sum(spec[band] ** 2))) / len(seg))


def check(tag, ok, detail):
    print(f"({tag}) {detail}  {'PASS' if ok else 'FAIL'}")
    return [] if ok else [tag]


def arp_dead_steps(mode, legato):
    sr, x = render(mode, 60, 8.0, 1, f"leg_arp_{mode}_{legato}", tempo=BPM, playing=0,
                   hold="64,67", legato=legato, ampR=1.0, arpOn=1, arpMode=0,
                   arpRate=4, arpGate=0.5, arpOctave=1, ampA=0.001, ampD=0.05,
                   ampS=0.8, **BASE)
    a = np.abs(x[:, 0])
    seg = int(STEP * sr)
    n = int(8.0 / STEP)
    p = np.array([a[k * seg:k * seg + int(0.03 * sr)].max() for k in range(n)])
    d = 20 * np.log10(np.maximum(p, 1e-9) / max(p[0], 1e-9))
    return int(np.sum(d < -20.0)), n, db(p[0])


def main():
    fails = []

    # (a) arp at gate 50 %: no dead steps with legato on.
    for mode, name in ((2, "Mono"), (0, "Cosmos")):
        dead, n, first = arp_dead_steps(mode, 1)
        fails += check(f"a-{name}", dead == 0 and first > ABS_FLOOR_DB,
                       f"arp 50 % {name:6s}: {dead}/{n} steps >20 dB down (want 0), "
                       f"first step {first:6.1f} dBFS (>{ABS_FLOOR_DB:.0f})")

    # (b) live Mono tie: level holds, no re-attack.
    P = dict(BASE, ampA=0.005, ampD=0.1, ampS=0.8, ampR=0.3)
    sr, x = render(2, 60, 3.0, 1, "leg_live", legato=1, noteon="1.0:64", **P)
    before, after = db(rms(x, sr, 0.5, 0.9)), db(rms(x, sr, 2.0, 2.9))
    fails += check("b-hold", abs(after - before) < 1.0,
                   f"live tie    : {before:6.1f} dB before, {after:6.1f} dB 1-2 s after (within 1 dB)")
    P = dict(BASE, ampA=0.05, ampD=0.3, ampS=0.5, ampR=0.3)
    rise = {}
    for legato in (0, 1):
        sr, x = render(2, 60, 2.0, 1, f"leg_reattack_{legato}", legato=legato,
                       noteon="1.0:64", **P)
        rise[legato] = db(win_rms(x, sr, 1.0, 1.4, 0.005).max()) - db(rms(x, sr, 0.8, 0.95))
    fails += check("b-attack", rise[1] < 1.0 and rise[0] > 3.0,
                   f"re-attack   : legato +{rise[1]:.1f} dB (<1), control +{rise[0]:.1f} dB (>3)")

    # (c) arp at gate 100 %: legato ties the steps, in Mono and in a poly circuit.
    # Cosmos plays about 10 dB below Mono (its output trim), so its floor sits lower.
    for mode, name, min_floor in ((2, "Mono", -30.0), (0, "Cosmos", -40.0)):
        swing, floor = {}, {}
        for legato in (0, 1):
            sr, x = render(mode, 60, 4.0, 1, f"leg_tie_{mode}_{legato}", tempo=BPM,
                           playing=0, hold="64,67", legato=legato, arpOn=1, arpMode=0,
                           arpRate=4, arpGate=1.0, arpOctave=1, ampA=0.02, ampD=0.2,
                           ampS=0.5, ampR=0.05, **BASE)
            s, m = [], []
            for k in range(8, 32):
                e = win_rms(x, sr, k * STEP, (k + 1) * STEP, 0.002)
                s.append(db(e.max()) - db(e.min()))
                m.append(db(e.min()))
            swing[legato], floor[legato] = float(np.median(s)), min(m)
        fails += check(f"c-{name}", swing[1] < 2.0 and floor[1] > min_floor and swing[0] > 3.0,
                       f"arp 100 % {name:6s}: legato swing {swing[1]:.1f} dB (<2), floor "
                       f"{floor[1]:.1f} dB (>{min_floor:.0f}); control swing {swing[0]:.1f} dB (>3)")

    # (d) glide only when notes overlap.
    P = dict(BASE, ampA=0.002, ampD=0.1, ampS=0.8, ampR=0.02, portaTime=0.2)
    f72 = 440.0 * 2 ** ((72 - 69) / 12)
    cents = {}
    for legato in (0, 1):
        for overlap in (0, 1):
            extra = dict(noteon="1.0:72")
            if not overlap:
                extra["release"] = 0.5
            sr, x = render(2, 60, 1.5, 1, f"leg_glide_{legato}_{overlap}", legato=legato,
                           **P, **extra)
            cents[(legato, overlap)] = 1200 * np.log2(max(zc_hz(x, sr, 1.01, 1.03), 1.0) / f72)
    fails += check("d", abs(cents[(1, 0)]) < 20 and cents[(1, 1)] < -300
                   and cents[(0, 0)] < -300 and cents[(0, 1)] < -300,
                   f"glide       : legato detached {cents[(1, 0)]:+.0f} ct (on pitch), "
                   f"overlapping {cents[(1, 1)]:+.0f} ct (gliding); control "
                   f"{cents[(0, 0)]:+.0f} / {cents[(0, 1)]:+.0f} ct (both gliding)")

    # (e) poly steal of a held voice: the stealing note sounds. Sine oscillator so
    #     the seventh note's fundamental is not masked by the chord's harmonics.
    P = dict(BASE, osc1Wave=3, ampA=0.005, ampD=0.1, ampS=0.8, ampR=0.3)
    f84 = 440.0 * 2 ** ((84 - 69) / 12)
    level = {}
    for legato in (0, 1):
        sr, x = render(0, 48, 3.0, 1, f"leg_steal_{legato}", legato=legato,
                       hold="52,55,59,62,65", noteon="1.0:84", **P)
        level[legato] = band_db(x, sr, 2.0, 3.0, f84)
    fails += check("e", level[1] > level[0] - 6.0 and level[0] > -80.0,
                   f"poly steal  : stealing note {level[1]:.1f} dB with legato, "
                   f"{level[0]:.1f} dB without (within 6 dB)")

    print(f"legato_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
