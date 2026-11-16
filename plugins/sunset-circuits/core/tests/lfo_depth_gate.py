#!/usr/bin/env python3
"""LFO depth mod destinations: the mod wheel brings in vibrato (issue #303).

Destinations 13 (LFO1 Depth) and 14 (LFO2 Depth). While at least one slot with a
source targets an LFO's depth, that LFO's output is multiplied by the sum of those
slots' contributions (source x amount), clamped to 0..1; with no such slot nothing
changes at all. So "Mod Whl -> LFO1 Depth, +100 %" is no LFO at wheel 0 and the
full LFO at wheel 1. Rule and rationale: SynthVoice::applyLfoDepth (Voice.hpp).

Rig: LFO n (5 Hz; sine, or triangle where noted) -> Osc1 Pitch at 50 %, i.e.
+-100 cents of vibrato, plus the route under test in slot 2. The mod wheel is set
before the note through render_test's modwheel= key, or moved with modwheelat=.

  [zero]   wheel 0 removes the LFO's effect: byte-identical to the same patch with
           the vibrato route's amount at 0 and no depth route (all five matrix
           modes, both LFOs, sine and triangle). Exact, not a tolerance: the
           depth is 0.0f from the note's first sample. Only the TRIANGLE runs can
           see a depth that arrives a sample late (a sine starts at exactly 0, a
           triangle at -1); a one-sample-late mutant fails them. Control: the
           unrouted render must NOT match that reference, or the comparison could
           not see the LFO at all.
  [full]   wheel 1 restores it: byte-identical to the patch without the depth
           route (same modes, LFOs, shapes). Exact because clamp(1.0f x 1.0f) is
           1.0f and x * 1.0f is x.
  [first]  a new note on a voice whose previous note had the full depth: note 1 at
           wheel 1, released, wheel to 0 while the voice is idle, note 2 on the
           same (Mono) voice. Byte-identical to the reference whose vibrato amount
           drops to 0 at the same moment, so note 2 has no LFO from its first
           sample (triangle; a one-sample-late mutant fails this too).
  [half]   wheel 0.5 gives half the vibrato depth, measured: cents trajectory from
           the analytic signal, least-squares fit at the LFO rate, ratio to the
           wheel-1 depth within 0.500 +- 0.010 (pre-registered).
  [sum]    two depth routes add: wheel 0.5 x 0.5 + velocity 1.0 x 0.25 = 0.5 is
           byte-identical to the single wheel 0.5 x 1.0 route.
  [clamp]  a sum above 1 behaves as 1 (byte-identical to [full]'s reference), a
           sum below 0 as 0 (byte-identical to [zero]'s).
  [rule]   what makes a depth slot count (ModMatrix::targets): a source. A depth
           slot at 0 % with the wheel at 1 holds its LFO at zero (byte-identical to
           the vibrato amount at 0); a depth slot whose source is None is inert
           (byte-identical to the unrouted render). The editor's "N active" badge
           counts by the same rule.
  [move]   the wheel moved from 0 to 1 during a held note (modwheelat=): up to the
           block the move lands on, the render is byte-identical to the wheel-0
           render and has no vibrato (under 1 cent); afterwards the vibrato is the
           full wheel-1 depth (ratio 1.000 +- 0.010, same measurement as [half]).
  [inert]  a depth route on an LFO that modulates nothing changes nothing (all five
           modes), and neither does one on the Init patch.
  [acid]   Acid bypasses the matrix, so a depth route changes nothing there.
  [edge]   an LFO's first depth route added, or its last one removed, while the
           note sounds does not click: LFO -> Amp at 100 % on a 1 kHz carrier, HF
           energy around the edge against the steady floor, limit 3x
           (lfo_sync_gate's click detector and limit). The floor is the higher of
           the steady spans before and after the edge (the reason is at the
           check). Controls, one per direction: the same change in LFO effect made
           by the matrix's instantaneous amount step must exceed the limit under
           the same floor rule, or the detector could not see the click the
           crossfade removes. Only these two edges are crossfaded (Voice.hpp,
           applyLfoDepth); other depth edits step like any matrix edit and are
           not checked here.
  [stale]  no stale depth. A route removed mid-note: from 0.2 s after the edge the
           render matches the never-routed one (max |diff| <= 1e-4 of its peak;
           only the output DC blocker's memory of the different past remains). A
           route removed while the voice is idle: the next note on that voice
           matches the never-routed render from its first sample (same bound), and
           the control with the route still in place does not.
"""
import filecmp
import os
import sys

import numpy as np
import soundfile as sf

from _harness import OUT, render
from lfo_sync_gate import click_vs_floor

LFO_HZ = 5.0
VIB_AMT = 0.5            # Osc1 Pitch: 2 semitones per unit -> +-100 cents
HALF_TOL = 0.010         # [half] / [move] ratio tolerance, fixed before measuring
SINE, TRIANGLE = 0, 1    # LFO shapes: a sine starts at 0, a triangle at -1
MOVE_ABSENT_CT = 1.0     # [move] "no vibrato yet": under 1 cent against the 100 of full depth.
                         # The estimator itself reads 0.54 ct on a render with no vibrato, and
                         # about 100 ct when the depth is not applied, so 1 ct separates the two.
CLICK_LIMIT = 3.0        # [edge] same limit as lfo_sync_gate's [loop]
STALE_REL = 1.0e-4       # [stale] max |diff| relative to the reference's peak

SRC_LFO = {1: 1, 2: 2}
SRC_WHEEL, SRC_VELOCITY = 4, 6
DST_OSC1_PITCH, DST_AMP = 1, 7
DST_DEPTH = {1: 13, 2: 14}
MATRIX_MODES = (0, 1, 2, 3, 4)    # Cosmos, Oracle, Mono, Modular, Prism

BASE = dict(sr=48000, analogAmt=0, vintage=0, masterVol=-12, arpOn=0,
            driveOn=0, chorusOn=0, delayOn=0, reverbOn=0, cosmosChorus=0,
            osc1Wave=3, osc1Level=1, osc2Level=0, osc3Level=0, subLevel=0,
            noiseLevel=0, ampA=0.001, ampD=0.01, ampS=1, ampR=0.01,
            filtA=0.001, filtD=0.01, filtS=1, filterEnvAmt=0,
            filterCutoff=18000, filterRes=0)

failures = []


def check(name, ok, detail):
    print(f"{name}: {'PASS' if ok else 'FAIL'} ({detail})")
    if not ok:
        failures.append(name)


def same(a, b):
    """Byte comparison of two renders written by _harness.render."""
    return filecmp.cmp(os.path.join(OUT, a + ".wav"), os.path.join(OUT, b + ".wav"),
                       shallow=False)


def vib(lfo, amount=VIB_AMT, shape=0):
    """LFO `lfo` -> Osc1 Pitch in slot 1."""
    return {f"lfo{lfo}Rate": LFO_HZ, f"lfo{lfo}Shape": shape, f"lfo{lfo}Fade": 0,
            "modSrc0": SRC_LFO[lfo], "modDst0": DST_OSC1_PITCH, "modAmt0": amount}


def depth(lfo, src=SRC_WHEEL, amount=1.0, slot=1):
    return {f"modSrc{slot}": src, f"modDst{slot}": DST_DEPTH[lfo], f"modAmt{slot}": amount}


def run(tag, mode, seconds=1.0, note=69, **p):
    render(mode, note, seconds, 2, tag, **dict(BASE, **p))
    return tag


def wav(tag):
    x, rate = sf.read(os.path.join(OUT, tag + ".wav"), always_2d=True)
    return x, rate


def vibrato_cents(tag, t0=0.5, t1=1.9):
    """Vibrato depth (cents, zero-to-peak) of a render: analytic-signal frequency,
    least-squares fit of a sinusoid at the LFO rate plus an offset."""
    x, rate = wav(tag)
    seg = x[int(t0 * rate):int(t1 * rate), 0]
    n = len(seg)
    X = np.fft.fft(seg)
    h = np.zeros(n)
    h[0] = 1.0
    if n % 2 == 0:
        h[n // 2] = 1.0
        h[1:n // 2] = 2.0
    else:
        h[1:(n + 1) // 2] = 2.0
    phase = np.unwrap(np.angle(np.fft.ifft(X * h)))
    f = np.diff(phase) * rate / (2.0 * np.pi)
    trim = int(0.05 * rate)                 # analytic-signal edge effects
    f = f[trim:-trim]
    t = (np.arange(len(f)) + trim) / rate
    c = 1200.0 * np.log2(np.maximum(f, 1.0) / 440.0)
    A = np.column_stack([np.sin(2 * np.pi * LFO_HZ * t), np.cos(2 * np.pi * LFO_HZ * t),
                         np.ones_like(t)])
    coef, *_ = np.linalg.lstsq(A, c, rcond=None)
    return float(np.hypot(coef[0], coef[1]))


def case_zero_full():
    for mode in MATRIX_MODES:
        for lfo in (1, 2):
            for shape, sname in ((SINE, "sine"), (TRIANGLE, "tri")):
                k = f"m{mode}_l{lfo}_{sname}"
                ref_off = run(f"zf_{k}_off", mode, **vib(lfo, amount=0.0, shape=shape))
                ref_on = run(f"zf_{k}_on", mode, **vib(lfo, shape=shape))
                w0 = run(f"zf_{k}_w0", mode, modwheel=0, **vib(lfo, shape=shape), **depth(lfo))
                w1 = run(f"zf_{k}_w1", mode, modwheel=1, **vib(lfo, shape=shape), **depth(lfo))
                control = not same(ref_on, ref_off)
                check(f"[zero]  mode={mode} LFO{lfo} {sname} wheel 0", same(w0, ref_off) and control,
                      f"byte-identical to amount 0: {same(w0, ref_off)}; "
                      f"control (unrouted LFO is audible): {control}")
                check(f"[full]  mode={mode} LFO{lfo} {sname} wheel 1", same(w1, ref_on),
                      f"byte-identical to the unrouted render: {same(w1, ref_on)}")


def case_first():
    # Mono: one voice, so note 2 lands on the voice note 1 used. Note 1 (0..0.3 s)
    # at wheel 1 leaves the full depth behind; the wheel goes to 0 at 0.5 s, while
    # the voice is idle (10 ms release), and note 2 starts at 0.6 s. The reference
    # has no depth route and drops the vibrato amount to 0 at the same block, so
    # note 1 is the same in both (as [full] shows) and note 2 has no LFO at all.
    for lfo in (1, 2):
        k = f"l{lfo}"
        events = dict(release=0.3, noteon="0.6:69", modwheel=1)
        ref = run(f"fi_{k}_ref", 2, **events, setat=["0.5:modAmt0:0"],
                  **vib(lfo, shape=TRIANGLE))
        test = run(f"fi_{k}_test", 2, **events, modwheelat="0.5:0",
                   **vib(lfo, shape=TRIANGLE), **depth(lfo))
        check(f"[first] LFO{lfo} next note after a full-depth note, wheel now 0", same(test, ref),
              f"byte-identical to the reference whose vibrato amount went to 0: "
              f"{same(test, ref)}")


def case_half_sum_clamp():
    for lfo in (1, 2):
        k = f"l{lfo}"
        on = run(f"h_{k}_on", 2, seconds=2.0, **vib(lfo))
        half = run(f"h_{k}_half", 2, seconds=2.0, modwheel=0.5, **vib(lfo), **depth(lfo))
        d_on, d_half = vibrato_cents(on), vibrato_cents(half)
        ratio = d_half / max(d_on, 1e-9)
        check(f"[half]  LFO{lfo} wheel 0.5", abs(ratio - 0.5) <= HALF_TOL,
              f"vibrato {d_half:.2f} ct vs {d_on:.2f} ct at wheel 1, ratio {ratio:.4f} "
              f"(want 0.500 +- {HALF_TOL:.3f})")

        two = run(f"s_{k}_two", 2, seconds=2.0, modwheel=0.5, vel=1.0, **vib(lfo),
                  **depth(lfo, amount=0.5),
                  **depth(lfo, src=SRC_VELOCITY, amount=0.25, slot=2))
        check(f"[sum]   LFO{lfo} 0.5x0.5 + 1.0x0.25", same(two, half),
              f"byte-identical to wheel 0.5 x 1.0: {same(two, half)}")

    for lfo in (1, 2):
        k = f"l{lfo}"
        on = run(f"c_{k}_on", 2, **vib(lfo))
        off = run(f"c_{k}_off", 2, **vib(lfo, amount=0.0))
        over = run(f"c_{k}_over", 2, modwheel=1, vel=1.0, **vib(lfo), **depth(lfo),
                   **depth(lfo, src=SRC_VELOCITY, slot=2))
        under = run(f"c_{k}_under", 2, modwheel=1, vel=1.0, **vib(lfo),
                    **depth(lfo, amount=-1.0),
                    **depth(lfo, src=SRC_VELOCITY, amount=0.5, slot=2))
        check(f"[clamp] LFO{lfo} sum 2.0", same(over, on),
              f"byte-identical to the unrouted render: {same(over, on)}")
        check(f"[clamp] LFO{lfo} sum -0.5", same(under, off),
              f"byte-identical to amount 0: {same(under, off)}")


def case_rule():
    # A depth slot counts when it has a source, at any amount (ModMatrix::targets).
    for lfo in (1, 2):
        k = f"l{lfo}"
        on = run(f"r_{k}_on", 2, **vib(lfo, shape=TRIANGLE))
        off = run(f"r_{k}_off", 2, **vib(lfo, amount=0.0, shape=TRIANGLE))
        zero_amt = run(f"r_{k}_amt0", 2, modwheel=1, **vib(lfo, shape=TRIANGLE),
                       **depth(lfo, amount=0.0))
        no_src = run(f"r_{k}_nosrc", 2, modwheel=1, **vib(lfo, shape=TRIANGLE),
                     **depth(lfo, src=0, amount=1.0))
        check(f"[rule]  LFO{lfo} depth slot at 0 %, wheel 1", same(zero_amt, off),
              f"LFO held at zero, byte-identical to amount 0: {same(zero_amt, off)}")
        check(f"[rule]  LFO{lfo} depth slot with source None", same(no_src, on),
              f"inert, byte-identical to the unrouted render: {same(no_src, on)}")


def case_move():
    # The wheel goes from 0 to 1 at 1.0 s during a held note. render_test applies
    # it on the first block starting at or after that time. The block size is
    # passed, not left to render_test's default, because `edge` below depends on it.
    T, BLOCK = 1.0, 512
    for lfo in (1, 2):
        k = f"l{lfo}"
        w0 = run(f"mv_{k}_w0", 2, seconds=2.5, modwheel=0, block=BLOCK, **vib(lfo), **depth(lfo))
        w1 = run(f"mv_{k}_w1", 2, seconds=2.5, modwheel=1, block=BLOCK, **vib(lfo), **depth(lfo))
        moved = run(f"mv_{k}_moved", 2, seconds=2.5, modwheel=0, modwheelat=f"{T}:1",
                    block=BLOCK, **vib(lfo), **depth(lfo))
        xm, sr = wav(moved)
        x0, _ = wav(w0)
        edge = -(-int(T * sr) // BLOCK) * BLOCK
        prefix = bool(np.array_equal(xm[:edge], x0[:edge]))
        d_before = vibrato_cents(moved, 0.3, 0.95)
        d_moved, d_full = vibrato_cents(moved, 1.3, 2.4), vibrato_cents(w1, 1.3, 2.4)
        ratio = d_moved / max(d_full, 1e-9)
        check(f"[move]  LFO{lfo} wheel 0 -> 1 mid-note",
              prefix and d_before < MOVE_ABSENT_CT and abs(ratio - 1.0) <= HALF_TOL,
              f"before the move (frames < {edge}) byte-identical to wheel 0: {prefix}, "
              f"vibrato {d_before:.2f} ct (want < {MOVE_ABSENT_CT:.0f}); after it "
              f"{d_moved:.2f} ct vs {d_full:.2f} ct at wheel 1, ratio {ratio:.4f} "
              f"(want 1.000 +- {HALF_TOL:.3f})")


def case_inert():
    for mode in MATRIX_MODES:
        for lfo, other in ((1, 2), (2, 1)):
            k = f"m{mode}_l{lfo}"
            plain = run(f"i_{k}_plain", mode, **vib(other))
            routed = run(f"i_{k}_routed", mode, modwheel=0, **vib(other), **depth(lfo))
            check(f"[inert] mode={mode} LFO{lfo} Depth, LFO{other} on pitch", same(routed, plain),
                  f"byte-identical without the depth route: {same(routed, plain)}")
    init = "i_init"
    render(0, 60, 1.0, 2, init)
    render(0, 60, 1.0, 2, "i_init_routed", modwheel=0.3, **depth(1), **depth(2, slot=2))
    check("[inert] Init patch + both depth routes", same("i_init_routed", init),
          f"byte-identical to Init: {same('i_init_routed', init)}")


def case_acid():
    for lfo in (1, 2):
        plain = run(f"a_l{lfo}_plain", 5, note=45, **vib(lfo))
        routed = run(f"a_l{lfo}_routed", 5, note=45, modwheel=0, **vib(lfo), **depth(lfo))
        check(f"[acid]  LFO{lfo} Depth route", same(routed, plain),
              f"byte-identical without it: {same(routed, plain)}")


def case_edge_stale():
    # lfo_sync_gate's click rig: 1 kHz-ish sine carrier, 12 kHz filter, LFO -> Amp
    # at full depth. 2 Hz from the note-on, so it peaks at 0.125 + 0.5 k s; the
    # edge lands on a peak, where the step it would make is largest.
    T = 1.125
    for lfo in (1, 2):
        amp = dict(filterCutoff=12000, block=64, masterVol=-12,
                   **{f"lfo{lfo}Rate": 2.0, f"lfo{lfo}Shape": 0, f"lfo{lfo}Fade": 0,
                      "modSrc0": SRC_LFO[lfo], "modDst0": DST_AMP, "modAmt0": 1.0})
        k = f"l{lfo}"
        never = run(f"e_{k}_never", 2, seconds=2.0, note=84, modwheel=0, **amp)
        added = run(f"e_{k}_add", 2, seconds=2.0, note=84, modwheel=0,
                    setat=[f"{T}:modDst1:{DST_DEPTH[lfo]}"],
                    **dict(amp, modSrc1=SRC_WHEEL, modDst1=0, modAmt1=1.0))
        removed = run(f"e_{k}_remove", 2, seconds=2.0, note=84, modwheel=0,
                      setat=[f"{T}:modDst1:0"], **dict(amp, **depth(lfo)))
        # Controls: the same change in LFO effect as each edge, made by the matrix's
        # instantaneous amount step (LFO effect out like "added", back in like
        # "removed").
        step_out = run(f"e_{k}_step_out", 2, seconds=2.0, note=84, modwheel=0,
                       setat=[f"{T}:modAmt0:0"], **amp)
        step_in = run(f"e_{k}_step_in", 2, seconds=2.0, note=84, modwheel=0,
                      setat=[f"{T}:modAmt0:1"], **dict(amp, modAmt0=0.0))
        clicks = {}
        for name, tag in (("added", added), ("removed", removed),
                          ("control out", step_out), ("control in", step_in)):
            x, sr = wav(tag)
            # FLOOR RULE: the higher of the steady spans before and after the edge.
            # The edge sits on an LFO peak, where the amplitude-modulated signal is
            # at 1 + sin = 2x its unmodulated level, so the side that carries the
            # modulation has a steady HF level about twice the other side's
            # (1.21e-4 against 6.17e-5 here). Against the quieter side alone, the
            # steady modulated signal would read about 2x with no click at all. A
            # click is energy above the steady signal on BOTH sides. This rule was
            # adopted after the first measurement (2026-11-15), on which "removed"
            # read 1.99x against the pre-edge floor alone; it reads 1.01x under
            # this rule. Both readings are printed. The two controls show the rule
            # still flags a step in either direction.
            c, before = click_vs_floor(x[:, 0], sr, T, 0.5, T - 0.05)
            _, after = click_vs_floor(x[:, 0], sr, T, T + 0.05, T + 0.6)
            clicks[name] = (c, max(before, after), before)

        def reading(name):
            c, fl, before = clicks[name]
            return (c, fl, f"HF click {c:.2e} vs {fl:.2e} floor ({c / max(fl, 1e-20):.2f}x; "
                           f"{c / max(before, 1e-20):.2f}x of the pre-edge floor alone)")
        for name in ("added", "removed"):
            c, fl, detail = reading(name)
            check(f"[edge]  LFO{lfo} first route {name} mid-note"
                  if name == "added" else f"[edge]  LFO{lfo} last route {name} mid-note",
                  c <= CLICK_LIMIT * fl, f"{detail}, limit {CLICK_LIMIT:.0f}x")
        for name in ("control out", "control in"):
            c, fl, detail = reading(name)
            check(f"[edge]  LFO{lfo} {name}: instantaneous amount step", c > CLICK_LIMIT * fl,
                  f"{detail}, must exceed {CLICK_LIMIT:.0f}x so the detector sees the step")

        xr, sr = wav(removed)
        xn, _ = wav(never)
        a = int((T + 0.2) * sr)
        diff = float(np.max(np.abs(xr[a:] - xn[a:])))
        peak = float(np.max(np.abs(xn[a:])))
        check(f"[stale] LFO{lfo} removed mid-note", diff <= STALE_REL * peak,
              f"max |diff| vs never routed from +0.2 s: {diff:.2e} "
              f"(limit {STALE_REL * peak:.2e} = {STALE_REL:.0e} of peak {peak:.3f})")

        # Idle voice: note 1 with the route (LFO silenced), released; the route is
        # removed while nothing sounds; note 2 on the same voice. Triangle, so a
        # stale depth would show from the very first sample (it starts at -1).
        tri = dict(amp, **{f"lfo{lfo}Shape": 1})
        events = dict(release=0.4, noteon="1.5:84", noteoff="2.4:84")
        ref = run(f"st_{k}_never", 0, seconds=2.5, note=84, modwheel=0, **events, **tri)
        gone = run(f"st_{k}_gone", 0, seconds=2.5, note=84, modwheel=0, **events,
                   setat=["1.0:modDst1:0"], **dict(tri, **depth(lfo)))
        kept = run(f"st_{k}_kept", 0, seconds=2.5, note=84, modwheel=0, **events,
                   **dict(tri, **depth(lfo)))
        xg, sr = wav(gone)
        xk, _ = wav(kept)
        xn, _ = wav(ref)
        a, b = int(1.5 * sr), int(2.4 * sr)
        peak = float(np.max(np.abs(xn[a:b])))
        d_gone = float(np.max(np.abs(xg[a:b] - xn[a:b])))
        d_kept = float(np.max(np.abs(xk[a:b] - xn[a:b])))
        check(f"[stale] LFO{lfo} removed while the voice was idle",
              d_gone <= STALE_REL * peak and d_kept > STALE_REL * peak,
              f"next note vs never routed: max |diff| {d_gone:.2e} (limit "
              f"{STALE_REL * peak:.2e}); control with the route kept: {d_kept:.2e}")


def main():
    try:
        case_zero_full()
        case_first()
        case_half_sum_clamp()
        case_rule()
        case_move()
        case_inert()
        case_acid()
        case_edge_stale()
    except RuntimeError as e:
        # A stale render_test (built before the modwheel= key existed) exits
        # nonzero; report it as a gate failure rather than a traceback.
        print(f"lfo_depth_gate: FAIL (render harness error: {e})")
        sys.exit(1)
    print(f"lfo_depth_gate: {'PASS' if not failures else 'FAIL'} "
          f"({len(failures)} failed checks)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
