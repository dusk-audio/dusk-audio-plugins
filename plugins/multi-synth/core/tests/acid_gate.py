#!/usr/bin/env python3
"""Acid-in-engine smoke gate (mode 5 through the full MultiSynthDSP path).

The standalone acid harness (core/tests/acid/) exercises the AcidVoice/Filter/
Sequencer in isolation; this gate confirms they are wired correctly INSIDE the
engine: the 16-step pattern sequencer replaces the arp when arpOn is set, notes
render, and accent + slide are audible in the rendered waveform statistics.
"""
import sys
import numpy as np
from _harness import render, rms_envelope, has_nan_inf

# Common acid patch: sequencer on, screaming filter, fast pluck decay. Every step
# on; the pattern transposes from the held root (C3 = 48). 120 BPM, 1/8 steps.
BASE = dict(arpOn=1, arpRate=3, arpGate=0.5, filterCutoff=350, filterRes=0.85,
            filterEnvAmt=0.9, ampD=0.18, ampS=0.0, osc1Wave=0,
            acidAccentAmt=0.9, acidSlideTime=60, reverbOn=0)


def onset_count(sig, sr):
    _, env = rms_envelope(sig, sr, win_ms=5.0)
    thr = 0.15 * env.max()
    above = env > thr
    return int(np.sum(above[1:] & ~above[:-1]))


def main():
    ok = True

    # 1. Pattern renders: 2 s @ 120 BPM 1/8 -> ~16 eighth-notes; every step on.
    steps = {f"arpStep{i}": 1 for i in range(16)}
    sr, x = render(5, 48, 2.0, 2, "acid_pat", **BASE, **steps)
    mono = x[:, 0]
    finite = not has_nan_inf(x)
    onsets = onset_count(mono, sr)
    rms = float(np.sqrt(np.mean(mono ** 2)))
    sounds = rms > 1e-3
    # 2 s at 120 BPM = 4 beats = 8 eighths; allow slack for gate/decay merging.
    onsets_ok = 5 <= onsets <= 20
    print(f"[pattern] finite={finite} rms={rms:.3f} onsets={onsets} (expect ~8)")
    ok &= finite and sounds and onsets_ok

    # 2. Accent: one accented step should be louder than the same pattern with no
    #    accents. Compare peak level of an all-accent vs no-accent run.
    accents_on = {f"seqAccent{i}": 1 for i in range(16)}
    _, xa = render(5, 48, 2.0, 2, "acid_acc_on", **BASE, **steps, **accents_on)
    _, xo = render(5, 48, 2.0, 2, "acid_acc_off", **BASE, **steps)
    pk_on = float(np.max(np.abs(xa)))
    pk_off = float(np.max(np.abs(xo)))
    accent_db = 20.0 * np.log10((pk_on + 1e-12) / (pk_off + 1e-12))
    accent_ok = accent_db > 1.0
    print(f"[accent]  peak accent-on {pk_on:.3f} vs off {pk_off:.3f} -> +{accent_db:.1f} dB (need > 1)")
    ok &= accent_ok

    # 3. Slide: a pattern with slide steps + pitch offsets must stay finite and
    #    sounding (glide ties suppress note-off gaps -> continuous energy).
    slide_steps = {f"seqSlide{i}": 1 for i in range(16)}
    pitch_steps = {f"seqPitch{i}": (12 if i % 2 else 0) for i in range(16)}
    _, xs = render(5, 48, 2.0, 2, "acid_slide", **BASE, **steps, **slide_steps, **pitch_steps)
    slide_ok = (not has_nan_inf(xs)) and float(np.sqrt(np.mean(xs[:, 0] ** 2))) > 1e-3
    print(f"[slide]   finite+sounding={slide_ok}")
    ok &= slide_ok

    print(f"acid_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
