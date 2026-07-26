#!/usr/bin/env python3
"""Calibrate the 6 PMB band t60 values (DUSKVERB_PMB) so the realized SUSTAINED-RELEASE
per-octave T60 matches the anchor. 6 bands map to 9 octaves: b0=63, b1=125/250,
b2=500, b3=1k/2k, b4=4k/8k, b5=16k. Iterate cmd *= (target/realized)^0.7. 2026-07-06."""
import os, sys, glob, shutil, subprocess, argparse
import numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt
ROOT=os.path.expanduser("~/projects/plugins"); REND=f"{ROOT}/build/tests/duskverb_render/duskverb_render"
VST3=os.path.expanduser("~/.vst3/DuskVerb.vst3"); PINK=4.0
OCT=[(44,88),(88,177),(177,355),(355,710),(710,1420),(1420,2840),(2840,5680),(5680,11360),(11360,18000)]
B2O={0:[0],1:[1,2],2:[3],3:[4,5],4:[6,7],5:[8]}  # band -> octave indices
def t60(path,lo,hi):
    x,sr=sf.read(path); m=x.mean(axis=1) if x.ndim>1 else x; off=int(PINK*sr)
    if off>=len(m): return None
    y=sosfiltfilt(butter(4,[max(lo,10),min(hi,sr*0.49)],'band',fs=sr,output='sos'),m[off:])
    if len(y)<int(sr*0.3): return None
    edc=np.cumsum((y**2)[::-1])[::-1]
    if edc[0]<=1e-30: return None
    db=10*np.log10(np.maximum(edc/edc[0],1e-12)); t=np.arange(len(db))/sr
    try: i5=np.where(db<=-5)[0][0]; i25=np.where(db<=-25)[0][0]
    except IndexError: return None
    if i25<=i5: return None
    sl=np.polyfit(t[i5:i25],db[i5:i25],1)[0]; return -60/sl if sl<0 else None
def oct_t60(path): return [t60(path,lo,hi) for lo,hi in OCT]
def render(name,pmb,outdir):
    shutil.rmtree(outdir,ignore_errors=True); os.makedirs(outdir); env=dict(os.environ); env["DUSKVERB_PMB"]=pmb
    r=subprocess.run([REND,"--vst3",VST3,"--program",name,"--output-dir",outdir,"--sustained-pink-seconds",str(PINK),
        "--param","Dry/Wet=1.0","--param","Bus Mode=1"],capture_output=True,env=env,timeout=300)
    s=glob.glob(f"{outdir}/*_sustained.wav")
    if r.returncode!=0 or not s:   # stop with a diagnostic, not sf.read(None) inside oct_t60()
        sys.exit(f"render failed for '{name}' (rc={r.returncode}); no *_sustained.wav in {outdir}\n"
                 + (r.stderr or b"").decode(errors="replace")[-400:])
    return s[0]
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--preset",required=True); ap.add_argument("--anchor",required=True)
    ap.add_argument("--seed",required=True); ap.add_argument("--lvl",default="1,1,1,1,1,1"); ap.add_argument("--dir",default="0,0,0.2,0.2,0.15,0.1")
    ap.add_argument("--wid",default="1,1,1,1,1,1"); ap.add_argument("--iters",type=int,default=8)
    a=ap.parse_args(); apref=os.path.basename(a.anchor)
    # Validate the 4 PMB vectors are exactly 6 bands each before the loop indexes them.
    for field,val in (("--seed",a.seed),("--lvl",a.lvl),("--dir",a.dir),("--wid",a.wid)):
        parts=val.split(",")
        if len(parts)!=6:
            sys.exit(f"{field} must have exactly 6 comma-separated values (got {len(parts)}: {val!r})")
    tgt=oct_t60(f"{a.anchor}/{apref}_sustained.wav")
    # band target = mean of the band's VALID octave T60s; None if the anchor gave
    # no usable T60 for any octave in the band (silent/too-short band) → that band
    # is skipped below so a NaN never poisons t[b] in the ratio update.
    def _bandmean(vals):
        vals=[v for v in vals if v]
        return float(np.mean(vals)) if vals else None
    bandtgt=[_bandmean([tgt[o] for o in B2O[b]]) for b in range(6)]
    print("band targets:",[round(x,3) if x else None for x in bandtgt])
    t=[float(x) for x in a.seed.split(",")]
    for it in range(a.iters):
        pmb=";".join([",".join(f"{v:.4f}" for v in t), a.lvl, a.dir, a.wid])
        real=oct_t60(render(a.preset,pmb,"/tmp/pmbcal")); err=[]
        for b in range(6):
            if not bandtgt[b]:   # no valid anchor T60 for this band → leave t[b] untouched (no NaN update)
                continue
            rvals=[real[o] for o in B2O[b] if real[o] and real[o]>0.03]
            if not rvals:
                continue
            r=float(np.mean(rvals))
            t[b]=float(np.clip(t[b]*(bandtgt[b]/r)**0.7,0.05,30)); err.append(abs(r-bandtgt[b])/bandtgt[b])
        print(f"iter {it}: maxerr {round(max(err)*100,1) if err else '-'}%  realized-oct",[round(x,2) if x else None for x in real])
    print("\nPMB t60:",",".join(f"{v:.4f}" for v in t))
    print("row t60: {"+",".join(f"{v:.3f}f" for v in t)+"}")
if __name__=="__main__": main()
