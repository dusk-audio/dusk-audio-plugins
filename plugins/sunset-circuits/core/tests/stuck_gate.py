#!/usr/bin/env python3
"""Stuck-note gate: switching presets while notes sound must not drone forever.

Two engine-side holes caused notes to sustain indefinitely until plugin re-init:
  (a) Mode-dependent note routing — a note started in a poly mode has its key-up
      routed through the NEW mode's path after a preset changes mode, so the old
      voice never receives its noteOff.
  (b) Arp disable (arpOn 1->0) resets the arp (dropping the pending note-off) and
      processBlock stops advancing it, so the arp-triggered voice keeps sounding.

The fix handles these transitions engine-side in snapshotParameters (release
semantics, tails ring out). This gate proves the fix while also proving ordinary
held notes are NOT killed (scenario c).

Scenarios (48k, 2x OS, 4 s, sustained patch ampS=1 ampR=0.3, no reverb/delay):
  a. mode-switch : note held, setat 1.0:mode:1   -> final 1 s silent, note sounded
  b. arp-off     : arp on, chord held, setat 1.0:arpOn:0 -> final 1 s silent
  c. control     : note held, no switch          -> final 1 s LOUD (not killed)

Note: scenario (a) switches between two POLY modes (Cosmos 0 -> Oracle 1). A
switch INTO Acid (mode 5) leaves the poly voice stuck in the allocator but
INAUDIBLE (the Acid render branch bypasses the poly voices), so it cannot be
measured at the output; a poly->poly switch is the reproduction that drones.
Levels: this engine's sustained note tops out near -30..-37 dBFS RMS, so the
thresholds are set with margin against measured reality (not an absolute -20).
"""
import sys
import numpy as np
from _harness import render

SILENT_DB = -60.0   # released + tail gone (floor)
LOUD_DB   = -45.0   # note clearly sounding (sustained note ~ -30..-37 dBFS)

# Sustained, dry patch so any residual = a genuinely stuck note (not a tail).
PATCH = dict(ampS=1.0, ampR=0.3, reverbOn=0, delayOn=0, cosmosChorus=0)


def rms_db(sig):
    r = np.sqrt(np.mean(sig ** 2))
    return 20.0 * np.log10(r + 1e-20)


def window_db(x, sr, t0, t1):
    a = max(0, int(t0 * sr))
    b = min(x.shape[0], int(t1 * sr))
    # Evaluate BOTH channels and return the MAX per-channel RMS dB: a stuck note
    # panned to either side must trip the silence checks, and the LOUD checks
    # still pass when either channel is loud.
    return max(rms_db(x[a:b, ch]) for ch in range(x.shape[1]))


def main():
    seconds = 4.0
    fails = []

    # (a) mode-switch: Cosmos poly note held, preset jumps to Oracle (mode 1) at 1 s.
    sr, x = render(0, 60, seconds, 2, "stuck_mode", setat="1.0:mode:1", **PATCH)
    a_early = window_db(x, sr, 0.0, 0.9)
    a_final = window_db(x, sr, seconds - 1.0, seconds)
    a_ok = (a_early > LOUD_DB) and (a_final < SILENT_DB)
    print(f"(a) mode-switch : early {a_early:6.1f} dB (>{LOUD_DB:.0f}), "
          f"final {a_final:6.1f} dB (<{SILENT_DB:.0f})  {'PASS' if a_ok else 'FAIL'}")
    if not a_ok:
        fails.append("a")

    # (b) arp-off: arp running on a held chord, arpOn 1->0 at 1 s.
    sr, x = render(0, 60, seconds, 2, "stuck_arp", arpOn=1, arpRate=3,
                   hold="64,67", setat="1.0:arpOn:0", **PATCH)
    b_early = window_db(x, sr, 0.0, 0.9)
    b_final = window_db(x, sr, seconds - 1.0, seconds)
    b_ok = (b_early > LOUD_DB) and (b_final < SILENT_DB)
    print(f"(b) arp-off     : early {b_early:6.1f} dB (>{LOUD_DB:.0f}), "
          f"final {b_final:6.1f} dB (<{SILENT_DB:.0f})  {'PASS' if b_ok else 'FAIL'}")
    if not b_ok:
        fails.append("b")

    # (c) control: identical to (a) but NO switch — held note must still sustain.
    sr, x = render(0, 60, seconds, 2, "stuck_control", **PATCH)
    c_final = window_db(x, sr, seconds - 1.0, seconds)
    c_ok = c_final > LOUD_DB
    print(f"(c) control     : final {c_final:6.1f} dB (>{LOUD_DB:.0f}, must sustain)"
          f"       {'PASS' if c_ok else 'FAIL'}")
    if not c_ok:
        fails.append("c")

    print(f"stuck_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
