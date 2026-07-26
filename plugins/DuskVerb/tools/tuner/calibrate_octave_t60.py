#!/usr/bin/env python3
"""Recalibrate a preset's per-octave T60 table to the anchor's SUSTAINED-RELEASE T60
(the #1 gate's ISO interrupted-noise method). Iterates the commanded octave values via
DUSKVERB_DHOCT (DenseHall) or DUSKVERB_AHOCT (AccurateHall) until realized ~= anchor.
T60 is a decay RATE (level-independent) so no gain-match is needed. Rebuilt 2026-07-06
for the release gate (the old noiseburst-calibrated tables mis-read after the #1 change)."""
import os, sys, glob, shutil, subprocess, argparse
import numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt
ROOT=os.path.expanduser("~/projects/plugins"); REND=f"{ROOT}/build/tests/duskverb_render/duskverb_render"
VST3=os.path.expanduser("~/.vst3/DuskVerb.vst3"); PINK=4.0
BANDS=[(44,88),(88,177),(177,355),(355,710),(710,1420),(1420,2840),(2840,5680),(5680,11360),(11360,18000)]
LBL=['63','125','250','500','1k','2k','4k','8k','16k']
def t60_release(path, lo, hi):
    x,sr=sf.read(path); m=x.mean(axis=1) if x.ndim>1 else x
    off=int(PINK*sr)
    if off>=len(m): return None
    tail=m[off:]; sos=butter(4,[max(lo,10),min(hi,sr*0.49)],'band',fs=sr,output='sos')
    y=sosfiltfilt(sos,tail)
    if len(y)<int(sr*0.3): return None
    edc=np.cumsum((y**2)[::-1])[::-1]
    if edc[0]<=1e-30: return None
    db=10*np.log10(np.maximum(edc/edc[0],1e-12)); t=np.arange(len(db))/sr
    try: i5=np.where(db<=-5)[0][0]; i25=np.where(db<=-25)[0][0]
    except IndexError: return None
    if i25<=i5: return None
    sl=np.polyfit(t[i5:i25],db[i5:i25],1)[0]
    return -60/sl if sl<0 else None
def measure(path): return [t60_release(path,lo,hi) for lo,hi in BANDS]
def render(name, env_var, cmd, outdir):
    shutil.rmtree(outdir,ignore_errors=True); os.makedirs(outdir)
    env=dict(os.environ); env[env_var]=",".join(f"{v:.4f}" for v in cmd)
    r=subprocess.run([REND,"--vst3",VST3,"--program",name,"--output-dir",outdir,
        "--sustained-pink-seconds",str(PINK),"--param","Dry/Wet=1.0","--param","Bus Mode=1"],
        capture_output=True,env=env,timeout=300)
    s=glob.glob(f"{outdir}/*_sustained.wav")
    if r.returncode!=0 or not s:   # stop with a diagnostic, not a crash inside measure()/t60_release
        sys.exit(f"render failed for '{name}' (rc={r.returncode}); no *_sustained.wav in {outdir}\n"
                 + (r.stderr or b"").decode(errors="replace")[-400:])
    return s[0]
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--preset",required=True); ap.add_argument("--anchor",required=True)
    ap.add_argument("--env",required=True,choices=["DUSKVERB_DHOCT","DUSKVERB_AHOCT"])
    ap.add_argument("--seed",required=True); ap.add_argument("--iters",type=int,default=8)
    a=ap.parse_args(); apref=os.path.basename(a.anchor)
    cmd=[float(x) for x in a.seed.split(",")]
    if len(cmd)!=9:   # fail fast: cmd[b] for b in range(9) would IndexError mid-loop otherwise
        ap.error(f"--seed must be exactly 9 comma-separated values (one per octave 63..16k); got {len(cmd)}")
    tgt=measure(f"{a.anchor}/{apref}_sustained.wav")
    print("anchor release T60:",{LBL[b]:(round(tgt[b],3) if tgt[b] else None) for b in range(9)})
    for it in range(a.iters):
        real=measure(render(a.preset,a.env,cmd,"/tmp/cal_dv")); err=[]
        for b in range(9):
            if tgt[b] and real[b] and real[b]>0.03:
                cmd[b]=float(np.clip(cmd[b]*(tgt[b]/real[b])**0.7,0.05,30)); err.append(abs(real[b]-tgt[b])/tgt[b])
        print(f"iter {it}: maxerr {round(max(err)*100,1) if err else '-'}%  realized",[round(x,2) if x else None for x in real])
    print("\nROW: {{ "+", ".join(f"{v:.4f}f" for v in cmd)+" }}")
if __name__=="__main__": main()
