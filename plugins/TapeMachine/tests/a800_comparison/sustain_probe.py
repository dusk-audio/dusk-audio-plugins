#!/usr/bin/env python3
"""sustain_probe.py — SUSTAINED-program brightness check for the low-level harmonic
floor (2026-07-12 brightness campaign).

program_probe uses a drum loop: its HF LTAS diff is TRANSIENT-dominated (loud, ~-6 dBFS)
where the low-level floor is inactive, so it can't reveal the floor's benefit. This probe
renders a SUSTAINED, decaying, harmonically-rich stimulus (stacked notes ringing out from
~-6 to ~-45 dBFS + a low pad) that spends most of its energy in the -18..-35 dBFS region
the floor targets. It then compares mine-vs-UAD LTAS (loudness-matched, rel-1k tilt) — the
'brightness on sustained music' the user hears. + = mine brighter than UAD.

  python3 sustain_probe.py            # A800 + ATR
"""
import os, tempfile, shutil, subprocess
import numpy as np, soundfile as sf
HERE=os.path.dirname(os.path.abspath(__file__))
from ladder_probe import BIN, MINE, mine_base, uad_base, UAD_A800, UAD_ATR, SR
STIM=os.path.join(HERE,"stimuli"); os.makedirs(STIM,exist_ok=True)

def synth(seconds=10.0, seed=3):
    rng=np.random.default_rng(seed); n=int(seconds*SR); x=np.zeros(n)
    # stacked decaying tones (notes ringing out through the low-level region) + slow pad
    notes=[98.0,146.83,196.0,246.94,293.66,392.0,493.88,587.33]
    t=np.arange(n)/SR
    step=1.1
    for k,f in enumerate(notes):
        for m in range(int(seconds/step)):
            on=int((m*step+0.05*k)%seconds*SR)
            if on>=n: continue
            dur=n-on
            tt=np.arange(dur)/SR
            env=np.exp(-tt/0.9)                       # ~0.9 s decay -> sweeps -6..-45 dBFS
            tone=(np.sin(2*np.pi*f*tt)+0.25*np.sin(2*np.pi*2*f*tt))*env
            x[on:on+dur]+=tone*0.11
    # low sustained pad (sub/low-mid body, always present, moderate level)
    pad=0.06*(np.sin(2*np.pi*65.4*t)+0.6*np.sin(2*np.pi*130.8*t))
    x[:n]+=pad*(0.6+0.4*np.sin(2*np.pi*0.3*t))
    x*= 0.5/ (np.max(np.abs(x))+1e-9)                 # peak ~ -6 dBFS
    return x.astype(np.float32)

def _pace_up():
    """UAD plugins bypass (pristine passthrough) when the PACE licensing daemon is down."""
    return os.path.isdir("/var/tmp/com.paceap.eden.licensed")

def render(plugin,params,src,mode="param"):
    # UAD renders (plugin != MINE) bypass to a pristine passthrough when PACE is down —
    # return no measurement so a bypassed stem is never analysed as a real result.
    if plugin!=MINE and not _pace_up():
        print(f"  ! PACE down — {os.path.basename(plugin)} would BYPASS; skipping"); return None
    tmp=tempfile.mkdtemp()
    cmd=[BIN,"--au",plugin,"--input-wav",src,"--slug","s","--output-dir",tmp,"--prerun-seconds","2"]
    flag="--param" if mode=="param" else "--nparam"
    for k,v in params: cmd+=[flag,f"{k}={v}"]
    proc=subprocess.run(cmd,capture_output=True,text=True)
    stem=os.path.join(tmp,"s_stem.wav")
    # Reject a failed render (nonzero exit or missing stem) even if a stale stem exists.
    if proc.returncode!=0 or not os.path.exists(stem):
        shutil.rmtree(tmp,ignore_errors=True)
        print(f"  ! render failed rc={proc.returncode}: {os.path.basename(plugin)}"); return None
    y,_=sf.read(stem)
    shutil.rmtree(tmp,ignore_errors=True)
    return y.mean(1) if (y is not None and y.ndim>1) else y

THIRD=[100,125,160,200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,
       4000,5000,6300,8000,10000,12500,16000]
def ltas(x):
    # loud region only (skip padded silence tail)
    a=np.abs(x); thr=0.02*np.max(a)
    idx=np.where(a>thr)[0]; x=x[idx[0]:idx[-1]] if len(idx) else x
    f=np.fft.rfftfreq(len(x),1/SR); X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2
    out=[]
    for c in THIRD:
        lo,hi=c/2**(1/6),c*2**(1/6); m=(f>=lo)&(f<hi)
        out.append(10*np.log10(np.sum(X[m])+1e-30))
    out=np.array(out); return out-out[THIRD.index(1000)]

def preset_cases():
    """Modern Rock (user-facing gate) rendered mine-preset-params vs UAD factory preset."""
    try:
        from preset_validate import (parse_presets, decode_uad, uad_nparams,
                                      mine_params, UAD_JSON, STUDER, ATR)
    except Exception as e:
        print(f"(preset cases skipped: {e})"); return []
    ps={p["name"]:p for p in parse_presets()}
    out=[]
    for pname in ["Modern Rock"]:
        p=ps[pname]; vec,_=decode_uad(p["machine"],UAD_JSON[pname])
        ubin=ATR if p["machine"]==1 else STUDER
        # W&F/noise off both sides (don't null); keep the preset's tape/EQ/drive tone
        mp=[(k,v) for k,v in mine_params(p) if k not in ("Wow","Flutter","Noise Amount","Noise Enabled")]
        mp+=[("Wow","0"),("Flutter","0"),("Noise Amount","0")]
        up=uad_nparams(p["machine"],vec)
        out.append((pname,mp,"param",ubin,up,"nparam"))
    return out

def main():
    src=os.path.join(STIM,"sustain.wav"); sf.write(src,synth(),SR)
    hf=[i for i,c in enumerate(THIRD) if c>=6300]
    def report(name,ym,yu):
        if ym is None or yu is None: print(f"{name}: render fail"); return
        L=min(len(ym),len(yu)); a,b=ym[:L],yu[:L]
        d=ltas(a)-ltas(b)
        print(f"\n### {name} sustained LTAS (mine-UAD, rel-1k tilt; + = mine brighter)")
        print("  "+"  ".join(f"{c/1000:g}k:{d[i]:+.1f}" for i,c in enumerate(THIRD) if c>=4000))
        worst=max(abs(d[i]) for i in hf)
        print(f"  >> HF 6.3-16k mean |tilt| = {np.mean([abs(d[i]) for i in hf]):.2f} dB  "
              f"(signed mean {np.mean([d[i] for i in hf]):+.2f}, worst |band| {worst:.2f})")
    for name,mi,uad in [("A800","0",UAD_A800),("ATR","1",UAD_ATR)]:
        report(name,render(MINE,mine_base(mi),src),render(uad,uad_base(),src))
    for name,mp,mmode,ubin,up,umode in preset_cases():
        report(name,render(MINE,mp,src,mmode),render(ubin,up,src,umode))

if __name__=="__main__": main()
