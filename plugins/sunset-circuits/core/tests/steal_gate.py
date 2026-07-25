#!/usr/bin/env python3
"""Voice-lifecycle discontinuity gate — poly shrink and mode switch must not click.

Three engine-side holes cut a sounding voice mid-waveform:

  (1) POLY SHRINK. effectivePoly() = min(modeVoices, 16 / unisonCount). Raising the
      unison count (or entering a mode with a smaller voice budget) shrinks it, and
      VoiceAllocator::retireAbove() called SynthVoice::reset() on every voice at or
      above the new limit -- including ones still sounding. A full-scale step to
      zero in one sample: a click.

  (2) The per-voice headroom trim 2/sqrt(poly) stepped on the same edge, so the
      SURVIVING voices jumped level at the instant the retired ones vanished
      (6 -> 5 voices is +0.79 dB; the largest reachable step is 8 -> 4 or below at
      +3.01 dB, where the trim saturates at unity).

      The fade is budgeted (VoiceAllocator::retireAbove): a retiring voice keeps
      rendering, so only kMaxOscVoices/2 extra oscillator banks may fade at once,
      loudest voices first. Beyond that a voice is still reset outright — by
      construction the quietest one available, so the residual step is the smallest
      there is. None of the scenarios below reach the cap (they retire one voice of
      one sub-voice); cpu_bench "shrink" is where it is exercised and measured.

  (3) MODE SWITCH. The outgoing path (poly voices, or the mono acid voice) was
      released but never rendered again -- the render loop picks its branch from
      the NEW mode -- so the tail was dropped on the spot.

MEASUREMENT
    A cut is broadband: a step in the waveform splatters energy across the whole
    spectrum. The voice under test is a pure sine (Cosmos with only osc1, sine
    wave), so above a few kHz it has essentially nothing of its own and

        burst = (energy above 4 kHz / total energy) in a 30 ms window at the event
              - the same ratio over a 30 ms window of the untouched signal

    is the discontinuity and nothing else. The max sample-to-sample jump does NOT
    work here: the voices render at 2x and the halfband decimator spreads a
    one-sample internal step over ~24 host samples, which drops the peak slope to
    the level of the signal's own slew (measured: a hard cut reads +0.7 dB on that
    metric and +25.8 dB on this one).

    Scenario (a) cannot look at the raw output: raising the unison count changes
    the timbre of every SURVIVING voice at the same instant, which is a legitimate
    (large) waveform change, not a click. It is isolated by rendering the same
    performance twice -- once with 6 held notes, once with the first 5 -- and
    subtracting. Everything is linear below the soft limiter (no FX, no vintage, no
    analog drift, peaks under 0.6), voices are allocated in note order with fixed
    per-voice seeds, so the difference is EXACTLY the sixth voice: the one that
    gets retired.

BUFFER SIZE
    Anything the engine defers to a block boundary has to behave the same at every
    buffer size, so the mode-switch rows run at 64, 512 and 4096 frames. This is
    not decoration: the crossfade used to commit only at a boundary, which left the
    voice path muted for (blockSize - fade) samples, and scenario (f) -- press a
    key 50 ms after the switch -- read -24 dBFS at 64 and 512 frames but -178 dBFS
    at 4096, because the note was routed to the outgoing mode and then wiped by the
    commit. At 512 frames alone the hole is exactly zero samples and invisible.

Scenarios (48 kHz, 2x OS, sustained patch, all FX off, switch at t = 1 s):
  a. poly shrink  : 6-note chord, unisonVoices 1 -> 3 (effectivePoly 6 -> 5)
  b. poly -> acid : 6-note chord, mode 0 -> 5                    [64/512/4096]
  c. acid -> poly : live acid note, mode 5 -> 0                  [64/512/4096]
  d. no starvation: after the shrink the freed slot is reusable (a new note sounds)
  e. no zombie    : a retired voice must not resume when the limit grows back
  f. live key     : a note pressed 50 ms after a mode switch sounds [64/512/4096]

Calibration: the pre-fix column is the same probe on the build immediately before
these fixes landed (fe6ec66). Limits sit ~10 dB over the post-fix reading and
>=10 dB under the pre-fix one, so a regression fails loudly and noise cannot
flake the gate. Both numbers print on every run.
"""
import sys
import numpy as np
from _harness import render

SR = 48000
BLOCK = 512          # render_test's default block size
BLOCKS = (64, 512, 4096)   # buffer sizes the mode-switch rows are checked at
SECONDS = 3.0
SWITCH_T = 1.0
KEY_T = 1.05         # scenario (f): key pressed this long after the switch
HF_HZ = 4000.0
WIN = 1440           # 30 ms analysis window
QUIET_T = 0.5        # start of the reference window (steady, untouched signal)
PRE_MS = 2.0         # window starts this far before the event

# Pure sine voice, fully linear output chain: no FX, no chorus, no analog drift, no
# vintage noise, and masterVol low enough that softLimit() never engages -- it is
# the only nonlinearity between the voices and the WAV, and scenario (a) subtracts
# two renders, which requires superposition to hold.
POLY = dict(sr=SR, analogAmt=0, vintage=0, cosmosChorus=0,
            osc1Wave=3, osc1Level=1.0, osc2Level=0.0, osc3Level=0.0,
            subLevel=0.0, noiseLevel=0.0,
            ampA=0.005, ampD=0.01, ampS=1.0, ampR=0.3,
            filterCutoff=12000, filterRes=0.1, filterEnvAmt=0.0,
            filtA=0.005, filtD=0.01, filtS=1.0,
            masterVol=-12, masterPan=0.0, stereoWidth=0.5,
            arpOn=0, reverbOn=0, delayOn=0, chorusOn=0, driveOn=0,
            unisonVoices=1, unisonDetune=10, unisonSpread=1.0)

# Live (non-sequenced) acid: sustained so the mode-switch edge lands mid-note.
ACID = dict(sr=SR, analogAmt=0, vintage=0, arpOn=0,
            filterCutoff=800, filterRes=0.3, ampD=5.0, ampS=1.0,
            masterVol=-12, reverbOn=0, delayOn=0, chorusOn=0, driveOn=0)

ROOT = 48
CHORD = [52, 55, 59, 62, 64]     # + ROOT = six notes -> voices 0..5

SILENT_DB = -60.0
LOUD_DB = -45.0

# label: (limit dB, pre-fix dB at 512 frames)
BURST_LIMIT = {
    "a": (8.0, 25.76),
    "b": (14.0, 39.97),
    "c": (10.0, 24.14),
}


def switch_frame(block=BLOCK, t=SWITCH_T):
    """Frame at which render_test applies a setat= scheduled at time t.

    Scheduled events fire on the first block starting at or after their time, so
    this is block-quantised -- which is the detection latency the engine cannot
    avoid without sample-accurate parameter events, and is why the analysis window
    is placed relative to this frame rather than to t.
    """
    return int(np.ceil(int(t * SR) / block) * block)


def out_of_band_db(sig, start):
    """Worst-channel energy above HF_HZ relative to that channel's total, in dB."""
    seg = sig[start:start + WIN]
    win = np.hanning(seg.shape[0])
    worst = -300.0
    for ch in range(seg.shape[1]):
        spec = np.abs(np.fft.rfft(seg[:, ch] * win)) ** 2
        f = np.fft.rfftfreq(seg.shape[0], 1.0 / SR)
        worst = max(worst, 10.0 * np.log10((spec[f >= HF_HZ].sum() + 1e-30) /
                                           (spec.sum() + 1e-30)))
    return worst


def burst_db(sig, frame):
    """Broadband burst at `frame` over the same signal's untouched out-of-band floor."""
    ref = out_of_band_db(sig, int(QUIET_T * SR))
    hit = out_of_band_db(sig, frame - int(PRE_MS * SR / 1000.0))
    return hit - ref


def rms_db(sig, t0, t1):
    a, b = max(0, int(t0 * SR)), min(sig.shape[0], int(t1 * SR))
    seg = sig[a:b]
    return max(20.0 * np.log10(float(np.sqrt(np.mean(seg[:, ch] ** 2))) + 1e-20)
               for ch in range(seg.shape[1]))


def held(notes):
    return ",".join(str(n) for n in notes)


def main():
    fails = []
    frame = switch_frame()
    print(f"{'scenario':<24}{'measured':>9}{'limit':>9}{'pre-fix':>9}   result")

    def check(label, what, value, limit, prefix, ok):
        pre = f"{prefix:>9.2f}" if prefix is not None else f"{'--':>9}"
        print(f"({label}) {what:<19}{value:>9.2f}{limit:>9.1f}{pre}   "
              f"{'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append(label)

    # --- (a) poly shrink: unisonVoices 1 -> 3 drops effectivePoly 6 -> 5 ---------
    # The difference of the 6-note and 5-note renders is exactly the retired voice.
    setat = f"{SWITCH_T}:unisonVoices:3"
    _, x6 = render(0, ROOT, SECONDS, 2, "steal_shrink6",
                   hold=held(CHORD), setat=setat, **POLY)
    _, x5 = render(0, ROOT, SECONDS, 2, "steal_shrink5",
                   hold=held(CHORD[:-1]), setat=setat, **POLY)
    d = x6 - x5
    lim, pre = BURST_LIMIT["a"]
    val = burst_db(d, frame)
    check("a", "poly shrink", val, lim, pre, val <= lim)

    # ...and it must actually END: a bounded fade, not a frozen voice stuck at the
    # limit that a later increase could revive.
    val = rms_db(d, SECONDS - 0.5, SECONDS)
    check("a", "retired voice gone", val, SILENT_DB, None, val < SILENT_DB)

    # --- (b) mode switch poly -> acid, at every buffer size ----------------------
    lim, pre = BURST_LIMIT["b"]
    for blk in BLOCKS:
        _, xb = render(0, ROOT, SECONDS, 2, f"steal_mode_poly_acid_{blk}",
                       hold=held(CHORD), setat=f"{SWITCH_T}:mode:5", block=blk, **POLY)
        val = burst_db(xb, switch_frame(blk))
        check("b", f"poly -> acid @{blk}", val, lim, pre if blk == BLOCK else None,
              val <= lim)

    # --- (c) mode switch acid -> poly, at every buffer size ----------------------
    lim, pre = BURST_LIMIT["c"]
    for blk in BLOCKS:
        _, xc = render(5, ROOT, SECONDS, 2, f"steal_mode_acid_poly_{blk}",
                       setat=f"{SWITCH_T}:mode:0", block=blk, **ACID)
        val = burst_db(xc, switch_frame(blk))
        check("c", f"acid -> poly @{blk}", val, lim, pre if blk == BLOCK else None,
              val <= lim)

    # --- (d) no starvation: the freed slot is reusable ---------------------------
    # Shrink at 1 s, release every key at 1.5 s, press a new note at 2 s. If a
    # retired voice held its slot the new note could not sound.
    _, xd = render(0, ROOT, SECONDS, 2, "steal_starve",
                   hold=held(CHORD), setat=f"{SWITCH_T}:unisonVoices:3",
                   release=1.5, noteon="2.0:72", **POLY)
    val = rms_db(xd, 2.5, SECONDS)
    check("d", "new note sounds", val, LOUD_DB, None, val > LOUD_DB)

    # --- (e) no zombie: growing the limit back must not revive a retired voice ---
    # Keys stay DOWN for the whole render. A voice that were merely frozen above
    # the limit rather than ended would still be in Sustain when the limit grows
    # back at 2 s, and would resume for good. Measured on the 6-vs-5 difference so
    # the reading is the retired voice alone, exactly as in (a) -- an earlier
    # version released the keys at 1.5 s and measured 2.5-3.0 s, by which point
    # even a revived voice had rung out, so it could not fail.
    _, xe6 = render(0, ROOT, SECONDS, 2, "steal_zombie6", hold=held(CHORD),
                    setat=[f"{SWITCH_T}:unisonVoices:3", "2.0:unisonVoices:1"], **POLY)
    _, xe5 = render(0, ROOT, SECONDS, 2, "steal_zombie5", hold=held(CHORD[:-1]),
                    setat=[f"{SWITCH_T}:unisonVoices:3", "2.0:unisonVoices:1"], **POLY)
    val = rms_db(xe6 - xe5, 2.5, SECONDS)
    check("e", "no zombie", val, SILENT_DB, None, val < SILENT_DB)

    # --- (f) a key pressed just after a mode switch must sound -------------------
    # The mode-switch crossfade defers the switch; while it is in flight the engine
    # is still rendering the OLD mode, and a note arriving then used to be routed to
    # it, played at zero gain and wiped by the commit. Purely a buffer-size defect:
    # the dead window is (blockSize - fade) samples, so 512 frames shows nothing.
    for blk in BLOCKS:
        _, xf = render(0, ROOT, SECONDS, 2, f"steal_key_after_switch_{blk}",
                       hold=held(CHORD), setat=f"{SWITCH_T}:mode:1",
                       noteon=f"{KEY_T}:72", block=blk, **POLY)
        val = rms_db(xf, KEY_T + 0.1, 1.5)
        check("f", f"live key @{blk}", val, LOUD_DB, None, val > LOUD_DB)

    print("\n(a-c are the broadband burst in dB over the same signal's own "
          "out-of-band floor; d-f are dBFS RMS)")
    print(f"steal_gate: {'PASS' if not fails else 'FAIL (' + ','.join(fails) + ')'}")
    sys.exit(0 if not fails else 1)


if __name__ == "__main__":
    main()
