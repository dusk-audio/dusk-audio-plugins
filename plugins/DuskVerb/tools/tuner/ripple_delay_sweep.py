#!/usr/bin/env python3
"""Base-delay sweep to minimize per-band tail-envelope ripple std vs a VVV
anchor (the gate the VH/BH delay sets already satisfy). Optuna over 16 FDN
base delays in [700, 6700], applied via the DUSKVERB_FDN_DELAYS env override
(no rebuild per trial). Objective = sum over the preset's FAILING ripple bands
of max(0, dv_std - anchor_std), measured gate-exact (sustained 0.5-3s, or the
noiseburst fallback when the anchor has no sustained render), gain-matched. A
light guard penalizes broadband tail-length drift so the octave-T60 recal that
follows stays small. Prints best delays as a C++ initializer.

Usage: ripple_delay_sweep.py "Ambience" --trials 200 --bands high
"""
import argparse, os, sys, glob, shutil, subprocess
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import soundfile as sf, numpy as np, optuna
from full_check import _tail_env_ripple_db, _t60_band_schroeder

REPO = Path(__file__).resolve().parents[4]
REND = REPO / "build/tests/duskverb_render/duskverb_render"
ANCH = Path.home() / "projects/dusk-audio-tools/tuner_runs/anchors"
VVV  = REPO / "tests/duskverb_render/output/vvv"
PR = {"Drum Plate": (VVV, "vvv_Drum_Plate"),
      "Ambience": (ANCH/"vvv-ambience", "vvv-ambience"),
      "Medium Drum Room": (ANCH/"vvv-fat-snare-room", "vvv-fat-snare-room")}
BANDS = {"bass": (40,250), "lowmid": (250,1000), "mid": (1000,4000), "high": (4000,12000)}

def rms(p):
    x,_=sf.read(p); m=x.mean(axis=1) if x.ndim>1 else x; return float(np.sqrt(np.mean(m*m)))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("preset"); ap.add_argument("--trials",type=int,default=200)
    ap.add_argument("--bands",nargs="+",required=True); ap.add_argument("--workers",type=int,default=6)
    a=ap.parse_args()
    adir,apref=PR[a.preset]; slug=a.preset.lower().replace(" ","_")
    asus=adir/f"{apref}_sustained.wav"; anb=adir/f"{apref}_noiseburst.wav"
    use_sus=asus.exists(); aref=str(asus if use_sus else anb)
    anchor_std={b:_tail_env_ripple_db(aref,0.5,3.0,*BANDS[b]) for b in a.bands}
    anchor_std_all={b:_tail_env_ripple_db(aref,0.5,3.0,*BANDS[b]) for b in BANDS}
    anchor_nb_rms=rms(str(anb))
    # anchor per-octave T60 (9 ISO bands) measured on the anchor noiseburst —
    # the sweep must hold these (±5% gate) so NO post-sweep octave recal is
    # needed (recal would shift decay and re-open the ripple it just fixed).
    OCT=[(44,88),(88,177),(177,355),(355,710),(710,1420),(1420,2840),(2840,5680),(5680,11360),(11360,18000)]
    anchor_t60=[_t60_band_schroeder(str(anb),lo,hi) for lo,hi in OCT]
    # ss-energy reference = the ANCHOR's steady-state per-band energy (the
    # actual ss-gate target). The swept delays must fix ripple WITHOUT moving
    # DV's gain-matched ss bands away from the anchor. Anchor needs no gain
    # term (DV is matched TO it).
    import numpy as np
    from scipy.signal import butter as _bt0, sosfiltfilt as _sf0
    _xr,_sr=sf.read(aref); _mr=_xr.mean(axis=1) if _xr.ndim>1 else _xr
    _seg=_mr[int(2.5*_sr):min(len(_mr),int(4.0*_sr))]
    SSB=[(50,100),(100,250),(250,500),(500,2000),(2000,5000),(5000,10000),(10000,20000)]
    ss_ref={}
    for lo,hi in SSB:
        sos=_bt0(4,[lo,min(hi,_sr*0.49)],'band',fs=_sr,output='sos')
        ss_ref[(lo,hi)]=20*np.log10(np.sqrt(np.mean(_sf0(sos,_seg)**2))+1e-12)
    def obj(trial):
        ds=sorted(trial.suggest_int(f"d{i}",700,6700) for i in range(16))
        d=f"/tmp/rds_{slug}_{os.getpid()}_t{trial.number}"; shutil.rmtree(d,ignore_errors=True); os.makedirs(d)
        env=dict(os.environ, DUSKVERB_FDN_DELAYS=",".join(map(str,ds)))
        r=subprocess.run([str(REND),"--program",a.preset,"--output-dir",d,"--param","Dry/Wet=1.0",
                          "--param","Bus Mode=1","--param","Freeze=0","--sustained-pink-seconds","4.0"],
                         capture_output=True,env=env)
        cand=glob.glob(f"{d}/*sustained*.wav") if use_sus else glob.glob(f"{d}/*_noiseburst.wav")
        if r.returncode!=0 or not cand: return 1e3
        dv=cand[0]; pen=0.0
        # (1) ripple: keep EVERY band's std within +1.0 dB of the anchor (margin
        #     under the +1.5 gate). No reward for over-smoothing — that is what
        #     redistributed band energy and opened ss/boom gates.
        for b in ["bass","lowmid","mid","high"]:
            s=_tail_env_ripple_db(dv,0.5,3.0,*BANDS[b])
            if s is None: return 1e3
            ar=anchor_std_all[b]
            if ar is not None: pen += 3.0*max(0.0, (s-ar)-1.0)
        # (2) preserve steady-state per-band energy vs the default-delay render
        #     (so the ripple fix does not move the ss/boom gates). Band RMS over
        #     the sustained steady window, gain-matched to anchor.
        # (3) octave-T60 match on the trial noiseburst (hold anchor decay so no recal)
        nbf=glob.glob(f"{d}/*_noiseburst.wav")
        if not nbf: return 1e3
        if nbf:
            for (lo,hi),at in zip(OCT,anchor_t60,strict=True):
                if at is None or at<=0.05: continue
                mt=_t60_band_schroeder(nbf[0],lo,hi)
                if mt is None: pen+=2.0; continue
                pen += 2.0*max(0.0, abs(mt-at)/at - 0.05)
        import numpy as _np
        x,sr=sf.read(dv); m=x.mean(axis=1) if x.ndim>1 else x
        _nbx=sf.read(nbf[0])[0]; _nbm=_nbx.mean(axis=1) if _nbx.ndim>1 else _nbx
        g=anchor_nb_rms/ (np.sqrt(np.mean(_nbm**2))+1e-12)
        from scipy.signal import butter as _bt, sosfiltfilt as _sf
        i0=int(2.5*sr); i1=min(len(m), int(4.0*sr)); seg=m[i0:i1]*g
        for (lo,hi),e0 in (ss_ref.items() if use_sus else []):
            sos=_bt(4,[lo,min(hi,sr*0.49)],'band',fs=sr,output='sos')
            e=20*np.log10(np.sqrt(np.mean(_sf(sos,seg)**2))+1e-12)
            pen += 0.5*max(0.0, abs(e-e0)-1.6)
        return pen
    study=optuna.create_study(direction="minimize",sampler=optuna.samplers.TPESampler(seed=42))
    optuna.logging.set_verbosity(optuna.logging.WARNING)
    study.optimize(obj,n_trials=a.trials,n_jobs=a.workers)
    ds=sorted(study.best_params[f"d{i}"] for i in range(16))
    print(f"\n{a.preset} best ripple penalty={study.best_value:.3f}")
    print("  { "+", ".join(map(str,ds))+" }")
    # final gate-exact readout at best
    d=f"/tmp/rds_{slug}_best"; shutil.rmtree(d,ignore_errors=True); os.makedirs(d)
    env=dict(os.environ, DUSKVERB_FDN_DELAYS=",".join(map(str,ds)))
    subprocess.run([str(REND),"--program",a.preset,"--output-dir",d,"--param","Dry/Wet=1.0","--param","Bus Mode=1","--param","Freeze=0","--sustained-pink-seconds","4.0"],capture_output=True,env=env)
    dv=(glob.glob(f"{d}/*sustained*.wav") if use_sus else glob.glob(f"{d}/*_noiseburst.wav"))[0]
    for b in ["bass","lowmid","mid","high"]:
        s=_tail_env_ripple_db(dv,0.5,3.0,*BANDS[b]); ar=_tail_env_ripple_db(aref,0.5,3.0,*BANDS[b])
        dl=s-ar; print(f"    {b:7s} Δ={dl:+.2f} {'PASS' if dl<=1.5 else 'FAIL'}")

if __name__=="__main__": main()
