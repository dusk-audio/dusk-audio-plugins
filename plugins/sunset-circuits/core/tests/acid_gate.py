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

    # 3. Slide: slide steps TIE gate-ends (they suppress the note-off between
    #    steps), so with a nonzero sustain the envelope FLOOR between plucks stays
    #    elevated. The same pattern WITHOUT slides note-offs at each gate-end and
    #    decays into silence in the gaps. Render both (identical except the seqSlide
    #    flags), compare the 10th-percentile of the short-window RMS envelope over
    #    the steady middle third: slide-on must exceed slide-off by a solid factor.
    #    Zeroing the seqSlide params makes the two renders identical -> ratio ~0 and
    #    the gate FAILS (verified during development), so it truly exercises slide.
    slide_steps = {f"seqSlide{i}": 1 for i in range(16)}
    pitch_steps = {f"seqPitch{i}": (12 if i % 2 else 0) for i in range(16)}
    slide_base = {**BASE, "ampS": 0.3}   # nonzero sustain so ties keep the note sounding
    _, xs_on  = render(5, 48, 2.0, 2, "acid_slide_on",  **slide_base, **steps, **slide_steps, **pitch_steps)
    _, xs_off = render(5, 48, 2.0, 2, "acid_slide_off", **slide_base, **steps, **pitch_steps)

    def env_floor(sig):
        _, env = rms_envelope(sig, sr, win_ms=5.0)
        mid = env[len(env) // 3: 2 * len(env) // 3]   # steady middle third
        return float(np.percentile(mid, 10))

    finite_slide = (not has_nan_inf(xs_on)) and (not has_nan_inf(xs_off))
    # BOTH renders must actually sound: a broken slide-off render (silence) would
    # make floor_off ~ 0 and the ratio spuriously huge -> false PASS.
    sounding = (float(np.sqrt(np.mean(xs_on[:, 0] ** 2))) > 1e-3
                and float(np.sqrt(np.mean(xs_off[:, 0] ** 2))) > 1e-3)
    floor_on = env_floor(xs_on[:, 0])
    floor_off = env_floor(xs_off[:, 0])
    ratio = floor_on / (floor_off + 1e-12)
    SLIDE_MIN_RATIO = 2.0
    slide_ok = finite_slide and sounding and ratio >= SLIDE_MIN_RATIO
    print(f"[slide]   env floor on {floor_on:.4f} vs off {floor_off:.4f} -> {ratio:.1f}x "
          f"(need >= {SLIDE_MIN_RATIO:.0f}x, finite={finite_slide}, both sounding={sounding})")
    ok &= slide_ok

    print(f"acid_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
