#!/usr/bin/env python3
"""
Automated preset-vs-anchor verifier — encodes the empirical listening
heuristics (volume-match first, multi-metric pass/fail) so the engineer
no longer needs to A/B every iteration in a DAW.

CRITERIA (validated via listening on Vocal Plate + Tiled Room):

  Gate 1 — LEVEL MUST MATCH FIRST (everything else is meaningless if not):
    RMS Δ        within ±1.5 dB  (auto-suggests Gain Trim if outside)

  Gate 2 — DECAY/SHAPE:
    rt60 Δ       within ±15 %
    env_p2p Δ    within ±2.5 dB  (engine ceiling tolerated)

  Gate 3 — SPECTRAL (the listener-perception bottleneck):
    cent_50 Δ    within ±10 %
    cent_500 Δ   within ±15 %
    treble_ratio Δ within ±15 %  (the "DARKER/BRIGHTER" perception driver)
    bass_ratio Δ within ±25 %    (plates allow more variance, halls less)
    spec_L1      < 2.0 dB        (RMS-normalized 1/3-oct EQ shape)

  Gate 4 — SPATIAL:
    stereo_r Δ   within ±0.10    (image, not critical for non-spatial presets)

VERDICT:
  PASS       all gates clean
  WARN       1 metric outside, others good (likely fine in listening)
  FAIL       2+ metrics outside, OR Gate 1 (RMS) fails — must re-tune
  ENGINE-CEILING  decay/timbre matches but TDC or env_p2p indicates
                  engine-architectural difference — accept

Run:  python3 dv_verify_preset.py
"""
from __future__ import annotations
import sys, subprocess, argparse, os
from pathlib import Path
import numpy as np, soundfile as sf

REPO_ROOT = Path(__file__).resolve().parents[4]
RENDER_BIN = REPO_ROOT / "build" / "tests" / "duskverb_render" / "duskverb_render"
OUTPUT_DIR = REPO_ROOT / "tests" / "duskverb_render" / "output"
DEFAULT_VST3 = Path.home() / ".vst3" / "DuskVerb.vst3"

# Directory holding the externally-captured anchor IR renders. Machine-specific
# by nature (typically a scratch dir), so it's overridable via the
# ANCHOR_RENDER_DIR env var; defaults to the historical /tmp location so
# existing local workflows keep working. CI / other developers can point this
# anywhere without editing the file.
ANCHOR_DIR = os.environ.get("ANCHOR_RENDER_DIR", "/tmp/anchor_renders")

sys.path.insert(0, str(REPO_ROOT / "plugins" / "DuskVerb" / "tools" / "tuner"))
from metrics_external import compute_metrics

# (preset_name, anchor_ir_path, slug). Anchor paths are either repo-relative
# (committed VVV renders) or built from ANCHOR_DIR (external captures).
PRESETS = [
    ("Vocal Plate",         os.path.join(ANCHOR_DIR, "vvv_vocal_plate_fresh_impulse.wav"),   "VocalPlate"),
    ("Vintage Vocal Plate", os.path.join(ANCHOR_DIR, "lex_vintage_vocal_plate_impulse.wav"), "VintageVocalPlate"),
    ("Snare Plate XL",      "tests/duskverb_render/output/vvv/vvv_Drum_Plate_impulse.wav",  "SnarePlateXL"),
    ("Vocal Hall",          "tests/duskverb_render/output/vvv/vvv_Vocal_Hall_impulse.wav",  "VocalHall"),
    ("Bright Hall",         "tests/duskverb_render/output/vvv/vvv_Bright_Hall_impulse.wav", "BrightHall"),
    ("Cathedral",           os.path.join(ANCHOR_DIR, "lex_cathedral_impulse.wav"),          "Cathedral"),
    ("Blade Runner 224",    os.path.join(ANCHOR_DIR, "lex_blade_runner_224_impulse.wav"),   "BladeRunner224"),
    ("Tiled Room",          os.path.join(ANCHOR_DIR, "vvv_tiled_room_impulse.wav"),          "TiledRoom"),
    ("Tight Drum Room",     os.path.join(ANCHOR_DIR, "vvv_fat_snare_room_impulse.wav"),      "TightDrumRoom"),
    ("Ambience",            "tests/duskverb_render/output/vvv/vvv_Ambience_impulse.wav",    "Ambience"),
    ("Realistic Chamber",   os.path.join(ANCHOR_DIR, "lex_chamber_large_impulse.wav"),       "RealisticChamber"),
    ("1981 Gated Snare",    os.path.join(ANCHOR_DIR, "vvv_84_small_room_impulse.wav"),       "1981GatedSnare"),
    ("Reverse Taps",        os.path.join(ANCHOR_DIR, "lex_reverse_1_impulse.wav"),           "ReverseTaps"),
]

# Anchor paths may be absolute (e.g. /tmp captures) or repo-relative; normalize
# the relative ones against REPO_ROOT so the script runs from any CWD.
PRESETS = [
    (name, anchor if os.path.isabs(anchor) else str(REPO_ROOT / anchor), slug)
    for name, anchor, slug in PRESETS
]


def rms_db(path, t0=0.05, t1=0.55):
    x, sr = sf.read(path)
    m = x.mean(axis=1) if x.ndim == 2 else x
    a, b = int(t0 * sr), min(int(t1 * sr), len(m))
    if b - a < 16: return None
    return float(20 * np.log10(np.sqrt(np.mean(m[a:b] ** 2) + 1e-30) + 1e-30))


def render(preset, vst3):
    """Render DV preset at 100% wet via harness. Raises on harness failure."""
    result = subprocess.run([
        str(RENDER_BIN),
        "--vst3", str(vst3),
        "--output-dir", str(OUTPUT_DIR),
        "--param", "Dry/Wet=1.0", "--param", "Bus Mode=1",
        # --program drives setCurrentProgram()/applyEngineConfig(); the
        # positional arg bypasses that canonical path (legacy preset table).
        "--program", preset
    ], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"render harness failed for '{preset}' (exit {result.returncode}):\n"
            f"{result.stdout}\n{result.stderr}"
        )



# Presets where a single metric is known to fail due to FDN-vs-MTDL or
# similar engine-architectural differences (validated via spec_L1 match
# + listening). Treat that metric as a known ceiling, not a fail.
ENGINE_CEILING = {
    "Vocal Plate":        {"cent_500": "FDN smooths anchor's MTDL mid-tail decay shape"},
    "Snare Plate XL":     {"cent_500": "FDN mid-tail vs Drum Plate MTDL — spec_L1 0.6 dB confirms EQ match"},
    "Realistic Chamber":  {"rt60":     "Lex Chamber Large 5.75 s anchor is hall-length; QuadTank max range falls short. spec_L1 0.72 dB confirms tonal match.",
                           "cent_50":  "anchor mid-tail brightness beyond QuadTank's filtered output"},
    "Vintage Vocal Plate": {"rt60":      "anchor RT60 metric artifact on 0.6s plate — slope-fit over noise floor returns ~9s; both plates are sub-1s musically",
                            "env_p2p":   "Dattorro dense wash vs Lex MTDL sparse early-reflection texture — architectural",
                            "bass_ratio":"Dattorro tank intrinsic bass decay rate — spec_L1 1.18 dB confirms EQ shape match despite faster tail bass falloff",
                            "stereo":    "Dattorro figure-8 dual-tank correlation is structurally different from Lex stereo plate"},
    "1981 Gated Snare":    {"rt60":      "NonLinear static-FIR hard cliff is the preset character; anchor (VVV 84 Small Room) is a continuous-decay small-room reverb with no time-domain gate",
                            "env_p2p":   "static-FIR plateau + hard cutoff vs anchor's continuous exponential decay — intentional gated envelope",
                            "bass_ratio":"gated envelope truncates bass tail before anchor's natural roll-off; engine character, not a tuning gap",
                            "treble_ratio":"gate window has no HF-vs-LF decay differential; anchor's continuous decay does",
                            "stereo":    "static-FIR per-tap L/R decorrelation differs from anchor's modal-density correlation",
                            "cent_50":   "gate window crops the centroid evolution window the anchor uses",
                            "cent_500":  "gate cliff truncates mid-tail centroid evolution"},
    # Reverse Taps exemptions REMOVED 2026-06-26: the old blanket list (rt60 /
    # env_p2p / bass_ratio / treble_ratio / stereo / cent_50 / cent_500) was written
    # for the retired NonLinear-vs-backwards-convolution mismatch. The current engine
    # (ReverseRoomEngine = swell-ER → VelvetTail → input-keyed gate) targets the GATED
    # lex-reverse-1 anchor faithfully — bright, wide, short per-band T60 + a gated
    # envelope — so those metrics SHOULD match. Exempting them masked real regressions;
    # let them surface as failures instead.
}

def evaluate(preset, anchor_path, slug, vst3):
    """Returns dict with all metrics + per-gate verdicts."""
    dv_path = str(OUTPUT_DIR / f"{slug}_impulse.wav")
    try:
        render(preset, vst3)
        dv = compute_metrics(dv_path)
        ref = compute_metrics(anchor_path)
    except Exception as e:
        return {'preset': preset, 'error': str(e)}

    dv_rms = rms_db(dv_path)
    ref_rms = rms_db(anchor_path)
    rms_d = (dv_rms - ref_rms) if (dv_rms is not None and ref_rms is not None) else None

    d50 = (dv['cent_50'] - ref['cent_50']) / max(ref['cent_50'], 1) * 100
    d500 = (dv['cent_500'] - ref['cent_500']) / max(ref['cent_500'], 1) * 100
    rt60_d = (dv['rt60'] - ref['rt60']) / max(ref['rt60'], 0.01) * 100 if abs(ref['rt60']) < 60 else 0
    env_d = dv['env_res_p2p'] - ref['env_res_p2p']
    tr_d = (dv['treble_ratio'] - ref['treble_ratio']) / max(ref['treble_ratio'], 1e-6) * 100
    br_d = (dv['bass_ratio'] - ref['bass_ratio']) / max(ref['bass_ratio'], 1e-6) * 100
    spec_l1 = float(np.mean(np.abs(dv['oct_db_norm'] - ref['oct_db_norm'])))
    stereo_d = dv['stereo_corr'] - ref['stereo_corr']

    fails = []
    warns = []
    if abs(rms_d or 0) > 1.5: fails.append(f"RMS Δ {rms_d:+.1f} dB (need ±1.5)")
    if abs(d50) > 10:
        if preset in ENGINE_CEILING and "cent_50" in ENGINE_CEILING[preset]:
            warns.append(f"cent_50 Δ {d50:+.1f}% [engine ceiling: {ENGINE_CEILING[preset]['cent_50']}]")
        else:
            (fails if abs(d50) > 15 else warns).append(f"cent_50 Δ {d50:+.1f}% (need ±10)")
    if abs(d500) > 15:
        if preset in ENGINE_CEILING and "cent_500" in ENGINE_CEILING[preset]:
            warns.append(f"cent_500 Δ {d500:+.1f}% [engine ceiling: {ENGINE_CEILING[preset]['cent_500']}]")
        else:
            (fails if abs(d500) > 25 else warns).append(f"cent_500 Δ {d500:+.1f}% (need ±15)")
    if abs(rt60_d) > 15:
        if preset in ENGINE_CEILING and "rt60" in ENGINE_CEILING[preset]:
            warns.append(f"rt60 Δ {rt60_d:+.0f}% [engine ceiling: {ENGINE_CEILING[preset]['rt60']}]")
        else:
            (fails if abs(rt60_d) > 30 else warns).append(f"rt60 Δ {rt60_d:+.0f}% (need ±15)")
    if abs(env_d) > 2.5:
        if preset in ENGINE_CEILING and "env_p2p" in ENGINE_CEILING[preset]:
            warns.append(f"env_p2p Δ {env_d:+.1f} dB [engine ceiling: {ENGINE_CEILING[preset]['env_p2p']}]")
        else:
            (fails if abs(env_d) > 5 else warns).append(f"env_p2p Δ {env_d:+.1f} dB (need ±2.5)")
    if abs(tr_d) > 15:
        if preset in ENGINE_CEILING and "treble_ratio" in ENGINE_CEILING[preset]:
            warns.append(f"treble_ratio Δ {tr_d:+.0f}% [engine ceiling: {ENGINE_CEILING[preset]['treble_ratio']}]")
        else:
            (fails if abs(tr_d) > 30 else warns).append(f"treble_ratio Δ {tr_d:+.0f}% (need ±15) {'[DARKER]' if tr_d < 0 else '[BRIGHTER]'}")
    if abs(br_d) > 25:
        if preset in ENGINE_CEILING and "bass_ratio" in ENGINE_CEILING[preset]:
            warns.append(f"bass_ratio Δ {br_d:+.0f}% [engine ceiling: {ENGINE_CEILING[preset]['bass_ratio']}]")
        else:
            (fails if abs(br_d) > 50 else warns).append(f"bass_ratio Δ {br_d:+.0f}% (need ±25) {'[THIN]' if br_d < 0 else '[FAT]'}")
    if spec_l1 > 2.0:
        (fails if spec_l1 > 3.5 else warns).append(f"spec_L1 {spec_l1:.2f} dB (need <2.0)")
    if abs(stereo_d) > 0.10:
        if preset in ENGINE_CEILING and "stereo" in ENGINE_CEILING[preset]:
            warns.append(f"stereo Δ {stereo_d:+.3f} [engine ceiling: {ENGINE_CEILING[preset]['stereo']}]")
        else:
            (fails if abs(stereo_d) > 0.20 else warns).append(f"stereo Δ {stereo_d:+.3f} (need ±0.10)")

    # Gate 1 (RMS level) is a HARD fail per the criteria — "everything else is
    # meaningless if not [matched]". A lone RMS fail must NOT be softened to
    # MINOR-FAIL, or an unmatched-level preset slips through as near-pass.
    rms_failed = any(f.startswith("RMS") for f in fails)
    if not fails and not warns: verdict = "✓ PASS"
    elif not fails: verdict = "~ WARN"
    elif len(fails) == 1 and not rms_failed: verdict = "~ MINOR-FAIL"
    else: verdict = "✗ FAIL"

    # Auto-suggest Gain Trim adjustment if level off
    trim_hint = ""
    if rms_d is not None and abs(rms_d) > 1.0:
        trim_hint = f"  [trim adjustment: {-rms_d:+.1f} dB]"

    return {
        'preset': preset,
        'verdict': verdict,
        'rms_d': rms_d,
        'metrics': {
            'cent_50_d_pct': d50, 'cent_500_d_pct': d500,
            'rt60_d_pct': rt60_d, 'env_p2p_d_db': env_d,
            'treble_d_pct': tr_d, 'bass_d_pct': br_d,
            'spec_l1_db': spec_l1, 'stereo_d': stereo_d,
        },
        'fails': fails, 'warns': warns, 'trim_hint': trim_hint,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--vst3", default=os.environ.get("DUSKVERB_VST3", str(DEFAULT_VST3)),
                    help=f"DuskVerb VST3 path (default {DEFAULT_VST3}, "
                         f"or DUSKVERB_VST3 env var).")
    args = ap.parse_args()
    vst3 = Path(args.vst3)
    if not vst3.exists():
        sys.exit(f"VST3 not found: {vst3}")
    if not RENDER_BIN.exists():
        sys.exit(f"render binary not found: {RENDER_BIN}  (build duskverb_render first)")

    print(f"{'PRESET':22s}  {'VERDICT':14s}  {'RMS Δ':>6s}  {'c50%':>5s}  {'c500%':>5s}  {'rt60%':>5s}  {'tr%':>5s}  {'br%':>5s}  {'sL1':>4s}")
    print('-' * 110)
    results = []
    for preset, anchor, slug in PRESETS:
        r = evaluate(preset, anchor, slug, vst3)
        results.append(r)
        if 'error' in r:
            print(f"{preset:22s}  ERROR: {r['error']}")
            continue
        m = r['metrics']
        rms = r['rms_d'] if r['rms_d'] is not None else 0
        print(f"{preset:22s}  {r['verdict']:14s}  {rms:+6.1f}  {m['cent_50_d_pct']:+5.1f}  {m['cent_500_d_pct']:+5.1f}  {m['rt60_d_pct']:+5.0f}  {m['treble_d_pct']:+5.0f}  {m['bass_d_pct']:+5.0f}  {m['spec_l1_db']:4.1f}")
    print()
    print("=" * 80)
    pass_count = sum(1 for r in results if 'PASS' in r.get('verdict', ''))
    warn_count = sum(1 for r in results if 'WARN' in r.get('verdict', '') or 'MINOR' in r.get('verdict',''))
    fail_count = sum(1 for r in results if 'FAIL' in r.get('verdict', '') and 'MINOR' not in r.get('verdict',''))
    print(f"  {pass_count} PASS,  {warn_count} WARN/MINOR,  {fail_count} FAIL")
    print()
    # Detail per non-PASS preset
    for r in results:
        if 'error' in r: continue
        if 'PASS' in r['verdict']: continue
        print(f"  {r['preset']}:  {r['verdict']}{r['trim_hint']}")
        for f in r['fails']:
            print(f"    FAIL: {f}")
        for w in r['warns']:
            print(f"    WARN: {w}")

    err_count = sum(1 for r in results if 'error' in r)
    if fail_count > 0 or err_count > 0:
        sys.exit(1)


if __name__ == '__main__':
    main()
