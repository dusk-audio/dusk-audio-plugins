#!/usr/bin/env python3
"""Generate the A800-comparison stimulus set as float32 48 kHz stereo WAVs.

Stimuli (all written to ./stimuli/):
  sweep.wav      log chirp 20 Hz -> 20 kHz, for frequency response.
  thd_steps.wav  1 kHz tone stepped through 6 levels, for THD-vs-level.
  wf_3150.wav    sustained 3150 Hz tone (DIN/IEC flutter freq), for wow & flutter.
  silence.wav    silence, for the plugin's added noise floor.

These are fed to both plugins verbatim via `duskverb_render --input-wav` so the
input is byte-identical for TapeMachine and the UAD A800; any output difference
is the emulation, not the stimulus.
"""
import os
import numpy as np
import soundfile as sf

SR = 48000
OUTDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stimuli")

# THD sweep levels in dBFS. Spans well below 0 VU (clean) up to hot tape.
THD_LEVELS_DBFS = [-30, -24, -18, -12, -6, -3]
THD_SEG_SEC = 1.2       # per-level segment length
THD_RAMP_SEC = 0.02     # click-free level transitions


def db2lin(db):
    return 10.0 ** (db / 20.0)


def stereo(mono):
    """Duplicate mono -> stereo float32 (both plugins are stereo-in/out)."""
    return np.column_stack([mono, mono]).astype(np.float32)


def write(name, mono):
    os.makedirs(OUTDIR, exist_ok=True)
    path = os.path.join(OUTDIR, name)
    sf.write(path, stereo(mono), SR, subtype="FLOAT")
    peak = float(np.max(np.abs(mono))) if mono.size else 0.0
    print(f"  wrote {name:16s} {len(mono)/SR:5.1f}s  peak {peak:.3f}")
    return path


def gen_sweep(seconds=6.0, f0=20.0, f1=20000.0, level_dbfs=-12.0, pad=0.5):
    """Log (exponential) chirp with silence lead/tail for latency alignment."""
    n = int(seconds * SR)
    t = np.arange(n) / SR
    k = (f1 / f0) ** (1.0 / seconds)
    # instantaneous phase of an exponential chirp
    phase = 2 * np.pi * f0 * (k ** t - 1.0) / np.log(k)
    x = np.sin(phase) * db2lin(level_dbfs)
    # short fades so the endpoints don't click
    fade = int(0.01 * SR)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    padn = int(pad * SR)
    return np.concatenate([np.zeros(padn), x, np.zeros(padn)]).astype(np.float32)


def gen_thd_steps(freq=1000.0):
    segs = []
    ramp = int(THD_RAMP_SEC * SR)
    seg_n = int(THD_SEG_SEC * SR)
    for db in THD_LEVELS_DBFS:
        t = np.arange(seg_n) / SR
        s = np.sin(2 * np.pi * freq * t) * db2lin(db)
        env = np.ones(seg_n)
        env[:ramp] = np.linspace(0, 1, ramp)
        env[-ramp:] = np.linspace(1, 0, ramp)
        segs.append(s * env)
    return np.concatenate(segs).astype(np.float32)


def gen_wf_tone(freq=3150.0, seconds=16.0, level_dbfs=-10.0):
    """Long steady tone; length chosen to resolve slow wow (~0.5 Hz)."""
    n = int(seconds * SR)
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * freq * t) * db2lin(level_dbfs)
    fade = int(0.05 * SR)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    return x.astype(np.float32)


def gen_silence(seconds=4.0):
    return np.zeros(int(seconds * SR), dtype=np.float32)


def gen_imd(f_lo=60.0, f_hi=7000.0, ratio=4.0, seconds=3.0, level_dbfs=-9.0):
    """SMPTE two-tone: 60 Hz + 7 kHz mixed 4:1, for intermodulation sidebands."""
    n = int(seconds * SR)
    t = np.arange(n) / SR
    a = db2lin(level_dbfs)
    lo = ratio / (ratio + 1.0)
    hi = 1.0 / (ratio + 1.0)
    x = a * (lo * np.sin(2 * np.pi * f_lo * t) + hi * np.sin(2 * np.pi * f_hi * t))
    fade = int(0.02 * SR)
    x[:fade] *= np.linspace(0, 1, fade); x[-fade:] *= np.linspace(1, 0, fade)
    return x.astype(np.float32)


def gen_transient(bursts=8, seconds=6.0, level_dbfs=-1.0):
    """Snare-like fast-attack noise bursts, for transient rounding / ballistics.
    Sharp attack + short decay so peak-vs-RMS shaving is measurable."""
    n = int(seconds * SR)
    x = np.zeros(n)
    rng = np.random.RandomState(0)
    gap = n // (bursts + 1)
    dur = int(0.12 * SR)
    for b in range(bursts):
        start = gap * (b + 1)
        env = np.exp(-np.arange(dur) / (0.02 * SR))     # 20 ms decay
        noise = rng.randn(dur) * env
        seg = min(dur, n - start)
        x[start:start + seg] += noise[:seg]
    x = x / (np.max(np.abs(x)) + 1e-9) * db2lin(level_dbfs)
    return x.astype(np.float32)


def gen_hot_sweep(**kw):
    """Sweep at a hot level for saturation-dependent HF + aliasing analysis."""
    return gen_sweep(level_dbfs=-2.0, **kw)


def gen_alias(freqs=(5000, 8000, 11000, 15000, 19000), seg_sec=1.0, level_dbfs=-1.0):
    """Stepped hot HF tones — drive hard so any aliasing folds into audible band."""
    segs = []
    ramp = int(0.02 * SR)
    seg_n = int(seg_sec * SR)
    for fr in freqs:
        t = np.arange(seg_n) / SR
        s = np.sin(2 * np.pi * fr * t) * db2lin(level_dbfs)
        env = np.ones(seg_n); env[:ramp] = np.linspace(0, 1, ramp); env[-ramp:] = np.linspace(1, 0, ramp)
        segs.append(s * env)
    return np.concatenate(segs).astype(np.float32)


def write_stereo_left(name, mono_left):
    """Write a TRUE stereo file with signal on L only (R silent) — for crosstalk."""
    os.makedirs(OUTDIR, exist_ok=True)
    path = os.path.join(OUTDIR, name)
    st = np.column_stack([mono_left, np.zeros_like(mono_left)]).astype(np.float32)
    sf.write(path, st, SR, subtype="FLOAT")
    print(f"  wrote {name:16s} {len(mono_left)/SR:5.1f}s  (L-only)")


if __name__ == "__main__":
    print(f"Generating stimuli -> {OUTDIR}")
    write("sweep.wav", gen_sweep())
    write("thd_steps.wav", gen_thd_steps())
    write("wf_3150.wav", gen_wf_tone())
    write("silence.wav", gen_silence())
    write("imd.wav", gen_imd())
    write("transient.wav", gen_transient())
    write("hot_sweep.wav", gen_hot_sweep())
    write("alias.wav", gen_alias())
    # 1 kHz tone on LEFT only for crosstalk bleed measurement
    _n = int(4.0 * SR); _t = np.arange(_n) / SR
    write_stereo_left("xtalk.wav", (np.sin(2 * np.pi * 1000 * _t) * db2lin(-6.0)).astype(np.float32))
    # record the THD segment layout for the analyzer
    meta = os.path.join(OUTDIR, "thd_levels.txt")
    with open(meta, "w") as f:
        f.write(",".join(str(x) for x in THD_LEVELS_DBFS) + "\n")
        f.write(f"{THD_SEG_SEC},{THD_RAMP_SEC}\n")
    print("Done.")
