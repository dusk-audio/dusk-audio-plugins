#!/usr/bin/env python3
"""Held-note hand-over gate: a routing edge must not silence the notes being held.

The arp toggle, the acid sequencer toggle and a mode switch each move where note
events go. The edge used to end whatever was sounding and stop there, so a held
chord went silent until the keys were pressed again: switch the arp on over a
held chord and nothing played; switch it off and the chord vanished; change the
engine mid-phrase and the phrase stopped. Each edge now ends the old path and
hands every held note (keys and sustain pedal) to the new one, in press order.
stuck_gate.py proves the other half: once the keys go up, everything stops.

Scenarios (48k, 2x OS, dry):
  a. arp on      : Cosmos chord held, arpOn 0 -> 1 at 1.0 s: the arp plays it
                   (step onsets in 1.2-2.8 s); keys up at 3.0 -> silent
  b. arp off     : arp running on a held chord, arpOn 1 -> 0 at 1.0 s: the chord
                   sounds, steady, in 1.2-2.8 s; keys up at 3.0 -> silent
  c. mode switch : Cosmos chord held, mode 0 -> 1 at 1.0 s: Oracle plays it in
                   1.2-2.8 s; keys up at 3.0 -> silent
  d. into Acid   : Cosmos note held, mode 0 -> 5 with the sequencer on: the pattern
                   runs on the held root in 1.2-2.8 s; key up at 3.0 -> silent
  e. seq on      : Acid live note held, arpOn 0 -> 1 at 1.0 s: the sequencer runs
                   on it; key up at 3.0 -> silent
  f. seq off     : Acid sequencer on a held root, arpOn 1 -> 0 at 1.0 s: the note
                   sounds live and steady; key up at 3.0 -> silent
  g. pedal       : keys up under the pedal, arp switched on at 1.0 s: the arp plays
                   the pedal-held notes; pedal up at 2.0 -> silent
  h. press order : 60, then 67, then 64 pressed in Cosmos, switch to Mono at 1.0 s.
                   Mono keeps the LAST key pressed, so it must sound 64 -- replaying
                   in note-number order would leave 67.
  i. live Acid   : 45, then 52, then 48 held in Cosmos, switch to Acid (sequencer
                   off) at 1.0 s. The one Acid voice must land on 48 at once: right
                   after the switch and later the pitch is 48's. Replaying the keys
                   one by one slid the voice up from 45 over the 200 ms slide.
  j. Mono glide  : (h) with Legato and a 0.5 s Glide. Mono must start on 64 at
                   once; replaying 60 and 67 first glided the one voice up from 60.
  k. arp back on : one key held, arp at full gate with Legato, switched off at 1.0 s
                   and on again at 2.0 s, in Mono and in Cosmos. The pattern must
                   sound after 2.0 s. Every step was a tie to the note the arp had
                   kept from before it was switched off, which was gone, so the
                   pattern stayed silent.
  l. stray key-up: a note-off for a key that was never pressed, under the pedal,
                   then the arp on at 1.0 s. The render must be identical to one
                   without that note-off; the pedal used to capture it, and the arp
                   played it.

(b) and (c) also check each held note on its own: the fundamental of each of 60,
64 and 67 is present after the hand-over, within 15 dB of the strongest, so one
note carried over cannot pass for the chord.
"""
import sys

import numpy as np

from _harness import render

SILENT_DB = -60.0
LOUD_DB = -45.0
SECONDS = 4.0

SUSTAIN = dict(ampA=0.002, ampD=0.1, ampS=1.0, ampR=0.05, reverbOn=0, delayOn=0,
               cosmosChorus=0)
PLUCK = dict(ampA=0.001, ampD=0.04, ampS=0.0, ampR=0.03, reverbOn=0, delayOn=0,
             cosmosChorus=0, arpRate=4, arpGate=0.5)


def db(v):
    return 20.0 * np.log10(max(float(v), 1e-20))


def window_db(x, sr, t0, t1):
    a, b = int(t0 * sr), int(t1 * sr)
    return max(db(np.sqrt(np.mean(x[a:b, ch] ** 2))) for ch in range(x.shape[1]))


def win_db(x, sr, t0, t1, win=0.01):
    a = x[int(t0 * sr):int(t1 * sr), 0]
    n = int(win * sr)
    k = len(a) // n
    e = np.sqrt(np.mean(a[:k * n].reshape(k, n) ** 2, axis=1))
    return 20.0 * np.log10(np.maximum(e, 1e-20))


def onsets(x, sr, t0, t1):
    """Rises through (peak - 20 dB) in 10 ms RMS windows: one per plucked step."""
    e = win_db(x, sr, t0, t1)
    thr = e.max() - 20.0
    return int(np.sum((e[1:] > thr) & (e[:-1] <= thr)))


def zc_hz(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr), 0]
    zc = np.flatnonzero((seg[1:] >= 0) & (seg[:-1] < 0))
    return (len(zc) - 1) / ((zc[-1] - zc[0]) / sr) if len(zc) > 2 else 0.0


def check(tag, ok, detail):
    print(f"({tag}) {detail}  {'PASS' if ok else 'FAIL'}")
    return [] if ok else [tag]


def pattern_case(tag, label, mode, note, **kw):
    """An arp / sequencer must be RUNNING in 1.2-2.8 s and silent at the end."""
    sr, x = render(mode, note, SECONDS, 2, f"ho_{tag}", release=3.0, **kw)
    n = onsets(x, sr, 1.2, 2.8)
    lvl = window_db(x, sr, 1.2, 2.8)
    end = window_db(x, sr, 3.4, SECONDS)
    return check(tag, n >= 6 and lvl > LOUD_DB and end < SILENT_DB,
                 f"{label}: {n} onsets in 1.2-2.8 s (>=6), {lvl:6.1f} dB (>{LOUD_DB:.0f}), "
                 f"after key-up {end:6.1f} dB (<{SILENT_DB:.0f})")


def band_db(x, sr, t0, t1, f0):
    seg = x[int(t0 * sr):int(t1 * sr), 0] * np.hanning(int(t1 * sr) - int(t0 * sr))
    spec = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(len(seg), 1.0 / sr)
    return db(spec[(f > f0 * 0.98) & (f < f0 * 1.02)].max() / len(seg))


def steady_case(tag, label, mode, note, notes=(), **kw):
    """A held note or chord must SOUND, steadily, in 1.2-2.8 s and stop at the end.

    With `notes`, each of them must also be present on its own: its fundamental
    within 15 dB of the strongest one's."""
    sr, x = render(mode, note, SECONDS, 2, f"ho_{tag}", release=3.0, **kw)
    e = win_db(x, sr, 1.2, 2.8, win=0.02)
    lvl = window_db(x, sr, 1.2, 2.8)
    end = window_db(x, sr, 3.4, SECONDS)
    spread = float(e.max() - e.min())
    fails = check(tag, lvl > LOUD_DB and spread < 6.0 and end < SILENT_DB,
                  f"{label}: {lvl:6.1f} dB (>{LOUD_DB:.0f}), 20 ms level spread {spread:4.1f} dB (<6), "
                  f"after key-up {end:6.1f} dB (<{SILENT_DB:.0f})")
    if notes:
        lv = [band_db(x, sr, 1.2, 2.8, 440.0 * 2 ** ((n - 69) / 12)) for n in notes]
        low = max(lv) - min(lv)
        fails += check(tag + "-notes", low < 15.0,
                       f"{label}: fundamentals of {'/'.join(map(str, notes))} at "
                       f"{'/'.join(f'{v:.1f}' for v in lv)} dB, weakest {low:.1f} dB down (<15)")
    return fails


def main():
    fails = []
    fails += pattern_case("a", "arp on      ", 0, 60, hold="64,67",
                          setat="1.0:arpOn:1", **PLUCK)
    fails += steady_case("b", "arp off     ", 0, 60, notes=(60, 64, 67), hold="64,67",
                         arpOn=1, arpRate=4, setat="1.0:arpOn:0", **SUSTAIN)
    fails += steady_case("c", "mode switch ", 0, 60, notes=(60, 64, 67), hold="64,67",
                         setat="1.0:mode:1", **SUSTAIN)
    fails += pattern_case("d", "into Acid   ", 0, 45, arpOn=1,
                          setat="1.0:mode:5", **PLUCK)
    fails += pattern_case("e", "seq on      ", 5, 45, setat="1.0:arpOn:1", **PLUCK)
    fails += steady_case("f", "seq off     ", 5, 45, arpOn=1, arpRate=4,
                         setat="1.0:arpOn:0", **SUSTAIN)

    # (g) pedal-held notes: keys up at 0.5 under the pedal, arp on at 1.0, pedal up
    #     at 2.0. Shorter windows than (a) because the pedal ends it at 2.0.
    sr, x = render(0, 60, SECONDS, 2, "ho_g", hold="64,67", release=0.5,
                   sustainat=["0.1:1", "2.0:0"], setat="1.0:arpOn:1", **PLUCK)
    n = onsets(x, sr, 1.1, 1.95)
    end = window_db(x, sr, 2.5, SECONDS)
    fails += check("g", n >= 4 and end < SILENT_DB,
                   f"pedal       : {n} onsets in 1.1-1.95 s (>=4), after pedal-up "
                   f"{end:6.1f} dB (<{SILENT_DB:.0f})")

    # (h) press order. Sine through an open filter so the zero-crossing count reads
    #     the fundamental.
    tone = dict(SUSTAIN, osc1Wave=3, osc2Level=0, subLevel=0, noiseLevel=0, analogAmt=0,
                filterCutoff=12000, filterEnvAmt=0, filterRes=0)
    sr, x = render(0, 60, 2.0, 2, "ho_h", noteon=["0.2:67", "0.4:64"],
                   setat="1.0:mode:2", **tone)
    f = zc_hz(x, sr, 1.3, 1.8)
    f64 = 440.0 * 2 ** ((64 - 69) / 12)
    ct = 1200 * np.log2(max(f, 1.0) / f64)
    fails += check("h", abs(ct) < 30,
                   f"press order : Mono after the switch plays {f:6.1f} Hz ({ct:+.0f} ct vs 64, want +-30)")

    # (i) several keys into live Acid: only the newest sounds, with no slide up to it.
    acid = dict(SUSTAIN, osc1Wave=1, filterCutoff=12000, filterEnvAmt=0, filterRes=0,
                acidSlideTime=200)
    sr, x = render(0, 45, 2.0, 2, "ho_i", noteon=["0.2:52", "0.4:48"],
                   setat="1.0:mode:5", **acid)
    f48 = 440.0 * 2 ** ((48 - 69) / 12)
    early = 1200 * np.log2(max(zc_hz(x, sr, 1.06, 1.12), 1.0) / f48)
    late = 1200 * np.log2(max(zc_hz(x, sr, 1.5, 1.8), 1.0) / f48)
    fails += check("i", abs(early) < 30 and abs(late) < 30,
                   f"live Acid   : {early:+.0f} ct right after the switch, {late:+.0f} ct later "
                   f"(vs 48, want +-30 both)")

    # (j) Mono with Legato and Glide: the newest key at once, no glide up to it.
    sr, x = render(0, 60, 2.0, 2, "ho_j", noteon=["0.2:67", "0.4:64"],
                   setat="1.0:mode:2", legato=1, portaTime=0.5, **tone)
    early = 1200 * np.log2(max(zc_hz(x, sr, 1.06, 1.14), 1.0) / f64)
    fails += check("j", abs(early) < 30,
                   f"Mono glide  : {early:+.0f} ct right after the switch (vs 64, want +-30)")

    # (k) arp off and on again over one held key, at full gate with Legato.
    for mode, name in ((2, "Mono"), (0, "Cosmos")):
        sr, x = render(mode, 60, SECONDS, 2, f"ho_k_{mode}", release=3.0, arpOn=1,
                       legato=1, arpGate=1.0, arpRate=4,
                       setat=["1.0:arpOn:0", "2.0:arpOn:1"], **SUSTAIN)
        lvl = window_db(x, sr, 2.2, 2.9)
        end = window_db(x, sr, 3.4, SECONDS)
        fails += check(f"k-{name}", lvl > LOUD_DB and end < SILENT_DB,
                       f"arp back on : {name:6s} {lvl:6.1f} dB after 2.0 s (>{LOUD_DB:.0f}), "
                       f"after key-up {end:6.1f} dB (<{SILENT_DB:.0f})")

    # (l) a key-up for a key nobody pressed changes nothing, pedal or not.
    stray = dict(release=0.5, sustainat=["0.1:1", "3.0:0"], setat="1.0:arpOn:1", **PLUCK)
    _, a = render(0, 60, SECONDS, 2, "ho_l_ref", **stray)
    _, b = render(0, 60, SECONDS, 2, "ho_l_stray", noteoff="0.3:66", **stray)
    diff = float(np.max(np.abs(a - b)))
    fails += check("l", diff == 0.0,
                   f"stray key-up: largest difference from the render without it "
                   f"{diff:.3g} (want 0)")

    print(f"handover_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
