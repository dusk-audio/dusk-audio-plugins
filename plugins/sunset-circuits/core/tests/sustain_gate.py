#!/usr/bin/env python3
"""Sustain-pedal gate: MIDI CC64 must defer note-offs and must never strand a note.

The pedal captures a note-off instead of releasing it, and the pedal-up sweep
releases everything it captured whose key is not currently down. That creates
exactly the failure mode the stuck_gate exists for -- a note nobody can reach --
so every transition that clears note state has to clear the captured set too.

Scenarios (48k, 2x OS, sustained patch ampS=1 ampR=0.3, no reverb/delay):
  a. hold      : pedal down, keys up at 0.5 -> still sounding at 1.0-1.4;
                 pedal up at 2.0 -> final 1 s silent
  b. control   : identical WITHOUT the pedal -> silent at 1.0-1.4. Proves (a)
                 measures deferral and not a long tail.
  c. mode-switch under pedal : pedal-held notes + mode 0 -> 1 at 1.0 -> silent
  d. panic     : pedal-held notes + allNotesOff (CC120/123) at 1.0 -> silent
  e. re-press  : key re-pressed while the pedal holds it must survive pedal-up
                 (its key is down) and release on its own key-up
  f. arp       : the pedal feeds the arp held-set like a key -- pattern keeps
                 running with the keys up, and stops on pedal-up
  g. acid      : mode 5's mono last-note stack holds under the pedal and unwinds
                 to silence on pedal-up (no stranded voice, no slide blip)

Levels match stuck_gate.py: this engine's sustained note sits near -30..-37 dBFS
RMS, so the thresholds are set against measured reality, not an absolute -20.
"""
import sys
import numpy as np
from _harness import render

SILENT_DB = -60.0   # released + tail gone (floor)
LOUD_DB   = -45.0   # note clearly sounding

# Sustained, dry patch so any residual = a genuinely stuck note (not a tail).
PATCH = dict(ampS=1.0, ampR=0.3, reverbOn=0, delayOn=0, cosmosChorus=0)


def rms_db(sig):
    r = np.sqrt(np.mean(sig ** 2))
    return 20.0 * np.log10(r + 1e-20)


def window_db(x, sr, t0, t1):
    a = max(0, int(t0 * sr))
    b = min(x.shape[0], int(t1 * sr))
    # MAX per-channel RMS: a stuck note panned to either side must trip the
    # silence checks, and the LOUD checks still pass when either channel is loud.
    return max(rms_db(x[a:b, ch]) for ch in range(x.shape[1]))


def check(tag, ok, detail):
    print(f"({tag}) {detail}  {'PASS' if ok else 'FAIL'}")
    return [] if ok else [tag]


def main():
    seconds = 4.0
    fails = []

    # (a) pedal down at 0.1, all keys up at 0.5, pedal up at 2.0.
    sr, x = render(0, 60, seconds, 2, "sus_hold", hold="64,67", release=0.5,
                   sustainat=["0.1:1", "2.0:0"], **PATCH)
    a_mid = window_db(x, sr, 1.0, 1.4)
    a_end = window_db(x, sr, seconds - 1.0, seconds)
    fails += check("a", a_mid > LOUD_DB and a_end < SILENT_DB,
                   f"hold        : keys-up+pedal {a_mid:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after pedal-up {a_end:6.1f} dB (<{SILENT_DB:.0f})")

    # (b) control: same release, NO pedal -> the note is gone by 1.0 s.
    sr, x = render(0, 60, seconds, 2, "sus_control", hold="64,67", release=0.5, **PATCH)
    b_mid = window_db(x, sr, 1.0, 1.4)
    fails += check("b", b_mid < SILENT_DB,
                   f"control     : keys-up no pedal {b_mid:6.1f} dB (<{SILENT_DB:.0f})")

    # (c) mode switch while the pedal holds the notes: the poly voices are reset and
    #     the captured set is dropped with them, so nothing can be re-released later.
    sr, x = render(0, 60, seconds, 2, "sus_mode", hold="64,67", release=0.5,
                   sustainat="0.1:1", setat="1.0:mode:1", **PATCH)
    c_early = window_db(x, sr, 0.6, 0.9)
    c_end = window_db(x, sr, seconds - 1.0, seconds)
    fails += check("c", c_early > LOUD_DB and c_end < SILENT_DB,
                   f"mode-switch : held {c_early:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after switch {c_end:6.1f} dB (<{SILENT_DB:.0f})")

    # (d) panic (CC120/CC123) while the pedal holds the notes.
    sr, x = render(0, 60, seconds, 2, "sus_panic", hold="64,67", release=0.5,
                   sustainat="0.1:1", panicat=1.0, **PATCH)
    d_early = window_db(x, sr, 0.6, 0.9)
    d_end = window_db(x, sr, seconds - 1.0, seconds)
    fails += check("d", d_early > LOUD_DB and d_end < SILENT_DB,
                   f"panic       : held {d_early:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after panic {d_end:6.1f} dB (<{SILENT_DB:.0f})")

    # (e) re-press: key up at 0.5 (captured), same key pressed again at 1.0, pedal up
    #     at 1.5. The key is DOWN, so pedal-up must not cut it; its own key-up at 3.0
    #     must still release it (both the retrigger and the first, deferred instance).
    sr, x = render(0, 60, seconds, 2, "sus_repress", release=0.5,
                   sustainat=["0.1:1", "1.5:0"], noteon="1.0:60", noteoff="3.0:60",
                   **PATCH)
    e_mid = window_db(x, sr, 2.0, 2.5)
    e_end = window_db(x, sr, 3.6, seconds)
    fails += check("e", e_mid > LOUD_DB and e_end < SILENT_DB,
                   f"re-press    : after pedal-up {e_mid:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after key-up {e_end:6.1f} dB (<{SILENT_DB:.0f})")

    # (f) arp: the pedal feeds the held-set exactly like a key, so the pattern runs on
    #     with the keys up and stops when the pedal lifts.
    sr, x = render(0, 60, seconds, 2, "sus_arp", arpOn=1, arpRate=3, hold="64,67",
                   release=0.5, sustainat=["0.1:1", "2.0:0"], **PATCH)
    f_mid = window_db(x, sr, 1.0, 1.4)
    f_end = window_db(x, sr, seconds - 1.0, seconds)
    fails += check("f", f_mid > LOUD_DB and f_end < SILENT_DB,
                   f"arp         : keys-up+pedal {f_mid:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after pedal-up {f_end:6.1f} dB (<{SILENT_DB:.0f})")

    # (g) acid (mode 5) mono last-note-hold: three notes stacked, all keys up at 0.5,
    #     pedal up at 2.0 unwinds the stack to silence. Played 47 -> 45 -> 40 so the
    #     stack TOP (the sounding note) is the LOWEST MIDI number, which is the case
    #     the pedal-up sweep has to release last -- releasing it first would glide the
    #     mono voice onto a note that is released on the same sample (audible blip).
    sr, x = render(5, 47, seconds, 2, "sus_acid", hold="45,40", release=0.5,
                   sustainat=["0.1:1", "2.0:0"], **PATCH)
    g_mid = window_db(x, sr, 1.0, 1.4)
    g_end = window_db(x, sr, seconds - 1.0, seconds)
    fails += check("g", g_mid > LOUD_DB and g_end < SILENT_DB,
                   f"acid        : keys-up+pedal {g_mid:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after pedal-up {g_end:6.1f} dB (<{SILENT_DB:.0f})")

    # (h) preset load WITHIN the same mode while the pedal holds notes. Held notes
    #     are deliberately seamless across such a load (see snapshotParameters), and
    #     pedal-held notes are held notes -- so they must keep sounding AND still
    #     release on pedal-up rather than being stranded by the parameter rewrite.
    sr, x = render(0, 60, seconds, 2, "sus_preset", hold="64,67", release=0.5,
                   sustainat=["0.1:1", "2.0:0"],
                   setat=["1.0:filterCutoff:3000", "1.0:filterRes:0.5"], notifyat=1.0,
                   **PATCH)
    h_mid = window_db(x, sr, 1.2, 1.6)
    h_end = window_db(x, sr, seconds - 1.0, seconds)
    fails += check("h", h_mid > LOUD_DB and h_end < SILENT_DB,
                   f"preset load : after load {h_mid:6.1f} dB (>{LOUD_DB:.0f}), "
                   f"after pedal-up {h_end:6.1f} dB (<{SILENT_DB:.0f})")

    print(f"sustain_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
