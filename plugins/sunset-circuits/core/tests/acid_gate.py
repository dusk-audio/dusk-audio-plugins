#!/usr/bin/env python3
"""Acid-in-engine smoke gate (mode 5 through the full MultiSynthDSP path).

The standalone acid harness (core/tests/acid/) exercises the AcidVoice/Filter/
Sequencer in isolation; this gate confirms they are wired correctly INSIDE the
engine: the 16-step pattern sequencer replaces the arp when arpOn is set, notes
render, accent + slide are audible in the rendered waveform statistics, and the
sequencer root unwinds a held chord last-note-first instead of stopping dead.
"""
import sys
import numpy as np
from _harness import render, rms_envelope, has_nan_inf, peak_hz

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

    # 1. Pattern renders: 2 s @ 120 BPM 1/8 -> ~8 eighth-notes; every step on.
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

    # 4. Sequencer root is a last-note-priority STACK, not a single note.
    #
    #    Hold C3 (48), then G3 (55) -- the newest key takes the root, so the pattern
    #    transposes from G3. Release G3 with C3 still physically down: the pattern
    #    must keep running, re-rooted on C3. AcidSequencer::noteOff used to test
    #    "is this the root?" and drop `held` outright, so the whole sequence stopped
    #    while a key was still down (measured: -400 dB from the moment G3 lifted).
    #
    #    Measured as PITCH on a fixed step grid, not just as level: "still sounding"
    #    alone would also pass if the root had never moved off G3. The patch is
    #    deliberately clean (open static filter, no resonance, sustained) so the FFT
    #    peak IS the fundamental; every seqPitch row is 0, so every step plays the
    #    root itself and a mid-gate window is a steady tone. 1/8 at 120 BPM = 0.25 s
    #    per step, free-running from t=0.
    CLEAN_SEQ = dict(arpOn=1, arpRate=3, arpGate=0.95, osc1Wave=0, analogAmt=0,
                     filterCutoff=6000, filterRes=0.0, filterEnvAmt=0.0,
                     ampD=5.0, ampS=1.0, reverbOn=0, delayOn=0, cosmosChorus=0)
    STEP_S = 0.25
    ROOT_TOL_CENTS = 60.0   # C3 and G3 are 700 cents apart; this only has to resolve that

    def step_pitch(sig, sr, step):
        """FFT peak of the mid-gate window of one sequencer step, as a MIDI note.

        NaN on a silent window (peak_hz returns 0 there): that is the failure this
        check exists for, and it must read as "no pitch", not log2(0).
        """
        a = int((step * STEP_S + 0.03) * sr)
        b = int((step * STEP_S + 0.13) * sr)
        f = peak_hz(sig[a:b], sr)
        return 69.0 + 12.0 * np.log2(f / 440.0) if f > 0.0 else float("nan")

    def step_rms(sig, sr, step):
        a = int((step * STEP_S + 0.03) * sr)
        b = int((step * STEP_S + 0.13) * sr)
        return float(np.sqrt(np.mean(sig[a:b] ** 2)))

    sr, xr = render(5, 48, 3.0, 2, "acid_seq_unwind", hold="55",
                    noteoff="1.0:55", **CLEAN_SEQ, **steps)
    mono = xr[:, 0]
    # Steps 1-2 (0.25-0.75 s): both keys down, root = G3. Steps 8-10 (2.0-2.75 s):
    # G3 released at 1.0 s, root must have fallen back to C3.
    pre = [step_pitch(mono, sr, k) for k in (1, 2)]
    post = [step_pitch(mono, sr, k) for k in (8, 9, 10)]
    post_rms = min(step_rms(mono, sr, k) for k in (8, 9, 10))
    pre_ok = all(abs(n - 55.0) * 100.0 <= ROOT_TOL_CENTS for n in pre)
    running = post_rms > 1e-3
    post_ok = running and all(abs(n - 48.0) * 100.0 <= ROOT_TOL_CENTS for n in post)
    # Control: release BOTH keys -> the stack empties and the pattern DOES stop.
    # Without this, "keeps running" could be satisfied by a sequencer that never
    # stops at all, which is the stuck-note bug in the other direction.
    _, xs = render(5, 48, 3.0, 2, "acid_seq_unwind_ctl", hold="55",
                   noteoff=["1.0:55", "1.0:48"], **CLEAN_SEQ, **steps)
    stop_rms = float(np.sqrt(np.mean(xs[int(2.0 * sr):, 0] ** 2)))
    stop_ok = stop_rms < 1e-5
    unwind_ok = pre_ok and post_ok and stop_ok
    print(f"[seqroot] newest key held -> {'/'.join(f'{n:.2f}' for n in pre)} (want 55.00), "
          f"newest released -> {'/'.join(f'{n:.2f}' for n in post)} (want 48.00, "
          f"running={running}); both keys up -> rms {stop_rms:.2e} (want < 1e-5)")
    ok &= unwind_ok

    print(f"acid_gate: {'PASS' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
