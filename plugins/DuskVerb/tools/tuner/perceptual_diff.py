#!/usr/bin/env python3
"""Audible-difference finder: a perceptual time x frequency comparison of DV vs a
VVV/Lex anchor, rendered ON A DRY SNARE (dry+wet mix), onset-aligned.

Why this exists: full_check.py is a checklist of static/aggregate PHYSICAL gates on
the 100%-wet IR. It misses (1) masking — the reverb's effect on a DRY source, (2)
time-varying texture (crispy HF tail, decay-profile shape), and (3) perceptual
weighting. This tool renders DV and the anchor on the same dry snare at a realistic
mix, builds an equal-loudness-weighted 1/3-octave loudness surface over time for
each, and reports WHERE/WHEN (band x time) DV diverges most — the things the ear
catches that the gates don't.

Output: a ranked list of (freq band, time window, dB delta) divergences, each
classified (e.g. "8-12kHz 200-600ms: DV +4.2dB -> crispier/HF-tail hot";
"20-40Hz sustained: DV -3.4dB -> less full/deep-sub weak";
"2-5kHz 10-60ms: DV +Xdb -> masks the snare").

Usage:
  perceptual_diff.py --dv <dv_wet_snare.wav> --anchor <anchor_wet_snare.wav>
                     [--dry <dry_snare.wav>] [--wet 0.35] [--name "Bright Hall"]
"""
import argparse, sys
import numpy as np, soundfile as sf
from scipy.signal import hilbert

# 1/3-octave centres 20Hz..16kHz
F0 = 1000.0
THIRD = np.array([F0 * 2**(n/3) for n in range(-17, 13)])   # ~20Hz..16kHz
# ISO 226-ish equal-loudness weight (relative dB, ear sensitivity ~ inverse of
# threshold). Coarse: roll off lows hard, peak ~3-4kHz, gentle HF roll. Used to
# weight the divergence RANK so a 3dB sub error doesn't outrank a 3dB presence error.
def ear_weight(f):
    # A-weighting-ish (dB), normalized to 0 at peak
    f2 = f*f
    ra = (12194.0**2 * f2*f2) / ((f2+20.6**2)*np.sqrt((f2+107.7**2)*(f2+737.9**2))*(f2+12194.0**2))
    return 20*np.log10(ra+1e-12) + 2.0

def onset(x, sr, drop_db=40.0):
    e = np.abs(hilbert(x)); w = max(int(0.002*sr),1)
    e = np.convolve(e, np.ones(w)/w, 'same'); edb = 20*np.log10(e+1e-30)
    pk = int(np.argmax(edb)); thr = edb[pk]-drop_db
    pre = np.where(edb[:pk+1] >= thr)[0]
    return int(pre[0]) if len(pre) else 0

def load_mono(p):
    x, sr = sf.read(p); m = x.mean(1) if x.ndim>1 else x
    return m.astype(float), sr

def third_oct_surface(x, sr, frame_ms=10.0, dur_s=1.5):
    """Per-frame 1/3-oct band energy (dB), onset-aligned, first dur_s."""
    on = onset(x, sr); seg = x[on:on+int(dur_s*sr)]
    hop = int(frame_ms/1000.0*sr); nfft = 1<<int(np.ceil(np.log2(hop*4)))
    freqs = np.fft.rfftfreq(nfft, 1/sr)
    # band edges between the 1/3-oct centres
    edges = np.sqrt(THIRD[:-1]*THIRD[1:]); edges = np.concatenate([[THIRD[0]/2**(1/6)], edges, [THIRD[-1]*2**(1/6)]])
    nfr = max(1, (len(seg)-nfft)//hop + 1)   # +1: include the initial (i=0) frame
    surf = np.zeros((len(THIRD), nfr))
    win = np.hanning(nfft)
    for i in range(nfr):
        fr = seg[i*hop:i*hop+nfft]
        if len(fr) < nfft: break
        sp = np.abs(np.fft.rfft(fr*win))**2
        for b in range(len(THIRD)):
            m = (freqs>=edges[b])&(freqs<edges[b+1])
            surf[b,i] = 10*np.log10(sp[m].sum()+1e-12)
    return surf, hop/sr

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dv', required=True); ap.add_argument('--anchor', required=True)
    ap.add_argument('--dry'); ap.add_argument('--wet', type=float, default=0.35)
    ap.add_argument('--name', default='preset'); ap.add_argument('--topk', type=int, default=12)
    a = ap.parse_args()

    dv, sr = load_mono(a.dv); an, sr2 = load_mono(a.anchor)
    if sr2 != sr:
        sys.exit(f"sample-rate mismatch: dv {sr} Hz vs anchor {sr2} Hz (resample to a common rate first)")
    # level-match DV to anchor on broadband RMS (so divergences are spectral/temporal, not gain)
    dv *= (np.sqrt(np.mean(an**2))+1e-12)/(np.sqrt(np.mean(dv**2))+1e-12)

    tag = 'WET'
    if a.dry:
        dry, sr_dry = load_mono(a.dry)
        if sr_dry != sr:
            sys.exit(f"sample-rate mismatch: dry {sr_dry} Hz vs dv {sr} Hz (resample to a common rate first)")
        on_dry = onset(dry, sr)
        def mix(wet):
            ow = onset(wet, sr); n = min(len(dry)-on_dry, len(wet)-ow)
            mx = np.zeros(n); mx += dry[on_dry:on_dry+n]
            mx += a.wet * wet[ow:ow+n] * ((np.sqrt(np.mean(dry**2)))/(np.sqrt(np.mean(wet[ow:ow+n]**2))+1e-12))
            return mx
        dv, an = mix(dv), mix(an); tag = f'DRY+WET({int(a.wet*100)}%)'

    sdv,_ = third_oct_surface(dv, sr); san,dt = third_oct_surface(an, sr)
    nfr = min(sdv.shape[1], san.shape[1]); sdv,san = sdv[:,:nfr], san[:,:nfr]
    # normalize each surface to its own peak (remove residual gain), then diff
    sdv -= sdv.max(); san -= san.max()
    diff = sdv - san     # DV minus anchor, dB, per (band,frame)

    w = ear_weight(THIRD)[:,None]
    score = np.abs(diff) * (10**(w/20.0))   # ear-weighted magnitude

    # collapse into (band x time-window) cells for a readable ranking
    twins = [(0,30,'transient'),(30,80,'early'),(80,200,'build'),(200,600,'mid-tail'),(600,1500,'late-tail')]
    rows=[]
    for b,fc in enumerate(THIRD):
        for t0,t1,tn in twins:
            f0i,f1i = int(t0/1000/dt), min(nfr,int(t1/1000/dt))
            if f1i<=f0i: continue
            d = diff[b,f0i:f1i].mean(); s = score[b,f0i:f1i].mean()
            rows.append((s, fc, tn, d, t0, t1))
    rows.sort(reverse=True)

    print(f"=== Perceptual diff: {a.name}  DV vs anchor  [{tag}] ===")
    print(f"(DV minus anchor, ear-weighted; + = DV hotter, - = DV weaker)\n")
    print(f"{'band':>8} {'window':>10} {'dB':>7}   interpretation")
    seen=set()
    for s,fc,tn,d,t0,t1 in rows[:a.topk*3]:
        key=(round(fc),tn)
        if key in seen: continue
        seen.add(key)
        if len(seen)>a.topk: break
        bl = f"{fc:.0f}Hz" if fc<1000 else f"{fc/1000:.1f}k"
        interp = ''
        if abs(d)>=1.5:
            region = 'sub' if fc<60 else 'low' if fc<300 else 'mid' if fc<2000 else 'presence' if fc<6000 else 'air'
            if d>0: interp = f"DV +{region} hot @ {tn}" + (" -> crispy/masking" if (fc>2000 and tn in('early','transient','mid-tail','late-tail')) else "")
            else:   interp = f"DV -{region} weak @ {tn}" + (" -> less full" if fc<300 else " -> dull/short" if fc>4000 else "")
        print(f"{bl:>8} {tn:>10} {d:+7.1f}   {interp}")
    print(f"\nTotal ear-weighted divergence (L1) = {score.mean():.2f}")

if __name__=='__main__': main()
