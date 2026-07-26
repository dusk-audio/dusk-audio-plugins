#!/usr/bin/env python3
"""debug_12k_spike — isolate the recurring 'spec_L1 max @ 12902 Hz' failure.

spec_L1 max is the 1/3-octave band where DV vs anchor RMS-normalized dB diverge
MOST. 12902 Hz = the band center 80*2^(22/3). A big number there can be:
  (B) a real sharp HF ring/spike in DV's audio (DSP bug), OR
  (A) a measurement artifact — a near-silent / steep-rolloff band whose
      normalized-dB difference is amplified by the log (metric ghost), i.e.
      DV is too QUIET there (a deficit), not a spike.

This runs an independent numpy FFT on the raw rendered noiseburst tail and
reports ABSOLUTE band power (dB) for DV vs anchor at 12902 Hz + neighbours,
plus the raw-bin shape around 12.9 kHz (sharp narrow peak => Case B).
"""
import os, sys, glob
import numpy as np
import soundfile as sf

# Anchor base is user-specific — resolve from DUSKVERB_ANCHORS or the per-user
# default (NOT a hardcoded /home/<dev> path), so this runs on any machine/CI.
ANCH_BASE = os.environ.get(
    "DUSKVERB_ANCHORS",
    os.path.expanduser("~/projects/dusk-audio-tools/tuner_runs/anchors"))
PAIRS = [
    ("79 Vocal Chamber",
     "/tmp/cg_79_Vocal_Chamber/L_noiseburst.wav",
     f"{ANCH_BASE}/vvv-79vc"),
    ("Vocal Plate",
     "/tmp/cg_Vocal_Plate/L_noiseburst.wav",
     f"{ANCH_BASE}/vvv-vocal-plate"),
]
FC = 12902.0
BANDS = [10238.0, FC, 16255.0]          # neighbours around the suspect band


def band_power_db(S, f, fc):
    lo, hi = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(S[m].sum() + 1e-30) if m.any() else None


def analyse(label, path):
    x, sr = sf.read(path)
    m = x.mean(1) if x.ndim > 1 else x
    S = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2   # same as metric
    f = np.fft.rfftfreq(len(m), 1 / sr)
    bands = {fc: band_power_db(S, f, fc) for fc in BANDS}
    total = 10 * np.log10(S.sum() + 1e-30)
    # raw-bin shape in 11.5-14.5 kHz: peak vs median tells spike vs rolloff
    seg = (f >= 11500) & (f <= 14500)
    peak = 10 * np.log10(S[seg].max() + 1e-30)
    med = 10 * np.log10(np.median(S[seg]) + 1e-30)
    fpk = f[seg][int(np.argmax(S[seg]))]
    return bands, total, peak, med, fpk, sr


for name, dvpath, anchordir in PAIRS:
    print(f"\n══════ {name} ══════")
    apath = sorted(glob.glob(anchordir + "/*noiseburst*.wav"))
    if not glob.glob(dvpath) or not apath:
        print(f"  missing render(s): dv={dvpath} anchor={anchordir}")
        continue
    db_dv, tot_dv, pk_dv, md_dv, fpk_dv, sr = analyse("DV", dvpath)
    db_lx, tot_lx, pk_lx, md_lx, fpk_lx, _ = analyse("anchor", apath[0])
    print(f"  (sr={sr}, Nyquist={sr/2:.0f} Hz)")
    print(f"  {'band Hz':>10} {'DV dB':>9} {'anchor dB':>10} {'Δ(raw)':>8}"
          f" {'DV-norm':>9} {'anch-norm':>9} {'Δ(norm)':>8}")
    for fc in BANDS:
        d, a = db_dv[fc], db_lx[fc]
        dn, an = d - tot_dv, a - tot_lx          # RMS-ish normalisation
        flag = "  <<< spec_L1 band" if abs(fc - FC) < 1 else ""
        print(f"  {fc:>10.0f} {d:>9.1f} {a:>10.1f} {d-a:>8.1f}"
              f" {dn:>9.1f} {an:>9.1f} {dn-an:>8.1f}{flag}")
    print(f"  11.5-14.5kHz raw bins: DV peak={pk_dv:.1f}dB @ {fpk_dv:.0f}Hz "
          f"median={md_dv:.1f}dB (peak-med={pk_dv-md_dv:.1f}dB) | "
          f"anchor peak-med={pk_lx-md_lx:.1f}dB")
    verdict = ("CASE B (real sharp ring in DV)" if (pk_dv - md_dv) > 25 and (db_dv[FC] - db_lx[FC]) > 6
               else "CASE A (DV deficit / near-floor band → normalized-dB ghost)")
    print(f"  → {verdict}")
