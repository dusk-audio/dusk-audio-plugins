#!/usr/bin/env python3
"""check_anchors.py — validate the fleet's ANCHOR renders for capture defects.

Motivation (2026-07-07): the Live Room anchor IMPULSE was dry-contaminated — a dry
click at t≈1 ms with the wet reverb tail buried 84 dB down. The boing gate runs on
the impulse and floor-guards a tail-less anchor to a phantom 0.0 dB, so DV scored
against pure artifact → a fake "structurally unreachable 0 dB boing wall" that nearly
sent us building a whole new reverb engine. A bad anchor produces phantom gates for
EVERY metric that uses that stimulus, silently. This script catches such captures
before they poison the scoreboard.

Run:  python3 check_anchors.py            # audit all fleet anchors
      python3 check_anchors.py --strict   # exit 1 if any HARD defect (for CI/capture gating)

Detects, per (preset, stimulus):
  DRY      dry click on a wet stimulus (impulse/snare/noiseburst peak < 3 ms, non-shimmer)
  TAILLESS impulse tail (peak+0.2s) > 70 dB below peak → boing floor-guards to phantom 0
  SILENT   peak amplitude < -40 dB (dead/failed render)
  CLIP     |sample| >= 0.999 (clipped capture)
  SHORT    < 1.5 s (too short for the tail windows)
  MISSING  file absent
Gated-reverse presets (Reverse Taps) legitimately have dead tails → TAILLESS is
downgraded to a note there. Shimmer presets (50% wet) legitimately peak at t≈0 → DRY
is exempt on them.
"""
import numpy as np, soundfile as sf, glob, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fleet_audit as fa

STIM = ['impulse', 'noiseburst', 'sustained', 'snare', 'sine1k', 'piano']
# Optional stimuli mirror fleet_audit.py's OPTIONAL_STIM: a missing render is a
# SKIP, not a hard failure (piano is not captured for every anchor, and the new
# optional-stimulus flow in fleet_audit already treats it that way).
OPTIONAL_STIM = {'piano'}
# presets whose tail is SUPPOSED to be gated/dead (not a capture defect)
GATED = {'Reverse Taps'}


def measure(path):
    x, sr = sf.read(path); x = x.mean(1) if x.ndim > 1 else x
    if len(x) == 0:
        return None
    pk = int(np.argmax(np.abs(x))); mx = float(np.abs(x[pk]))
    t0 = pk + int(0.2 * sr); tail = x[t0:t0 + sr]
    trel = (20 * np.log10(np.sqrt(np.mean(tail ** 2)) / (mx + 1e-12))
            if len(tail) >= 2048 else None)
    return dict(pkms=pk / sr * 1000.0, peak_db=20 * np.log10(mx + 1e-12),
                trel=trel, dur=len(x) / sr, clip=float(np.max(np.abs(x))),
                nan=bool(np.any(~np.isfinite(x))))


def main():
    strict = '--strict' in sys.argv
    hard = 0        # count of hard (boing-poisoning / dead / clip) defects
    print(f"{'preset':22} flags per stimulus")
    for name, (adir, shim) in fa.FLEET.items():
        flags = []
        for stim in STIM:
            fs = glob.glob(f"{adir}/*_{stim}.wav")
            if not fs:
                # Optional stimuli (piano) skip like fleet_audit.py — no flag,
                # no hard count, so --strict does not abort on a missing capture.
                if stim not in OPTIONAL_STIM:
                    flags.append(f"{stim}:MISSING"); hard += 1
                continue
            m = measure(fs[0])
            if m is None:
                flags.append(f"{stim}:EMPTY"); hard += 1; continue
            fl = []
            if m['nan']: fl.append('NAN'); hard += 1
            if m['clip'] >= 0.999: fl.append('CLIP'); hard += 1
            if m['peak_db'] < -40: fl.append('SILENT'); hard += 1
            if m['dur'] < 1.5: fl.append(f"SHORT{m['dur']:.1f}s"); hard += 1
            if (m['pkms'] < 3 and not shim
                    and stim in ('impulse', 'snare', 'noiseburst')):
                fl.append(f"DRY@{m['pkms']:.0f}ms"); hard += 1
            # tail-less impulse = the boing-poisoning defect (unless gated by design)
            if (stim == 'impulse' and m['trel'] is not None and m['trel'] < -70):
                if name in GATED:
                    fl.append('tailless(gated-ok)')
                else:
                    fl.append(f"TAILLESS{m['trel']:.0f}dB"); hard += 1
            if fl:
                flags.append(f"{stim}:{'+'.join(fl)}")
        print(f"{name:22} {'  '.join(flags) if flags else 'ok'}")
    print(f"\n{'HARD defects (would poison gates): ' + str(hard) if hard else 'All anchors clean.'}")
    if strict and hard:
        print("STRICT: re-render the flagged anchors (100% wet, full tail) before scoring.")
        sys.exit(1)


if __name__ == '__main__':
    main()
