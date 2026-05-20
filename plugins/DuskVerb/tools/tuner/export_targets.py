#!/usr/bin/env python3
"""One-shot batch exporter — captures Lex reference plugin metrics into
static JSON snapshots so future calibration runs need no live VST2.

Renders each known .fxp anchor through its Lex VST2 plugin via
duskverb_render (--vst2 + --load-state), measures the 19 metrics, and
dumps a `targets/lex_<preset>.json` file conforming to
`duskverb-target-v1` schema. See `target.py` for the loader.

Runs only on Linux with yabridge + Windows VST2 plugins installed.
Output JSONs are platform-independent — Mac/CI consume them via
`target.load_target()` without needing the VST2.

CLI:
    python3 export_targets.py              # export all 4
    python3 export_targets.py --preset hall_med_hall
    python3 export_targets.py --list       # show what's available

Output: plugins/DuskVerb/tools/targets/lex_<preset>.json
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

# Add this script's parent dirs to sys.path so `metrics` import works
# regardless of CWD.
_HERE = Path (__file__).resolve().parent
sys.path.insert (0, str (_HERE))            # tools/tuner/
sys.path.insert (0, str (_HERE.parent))     # tools/

import metrics as metrics_mod   # noqa: E402

# ─── Repo paths ──────────────────────────────────────────────────────
REPO_ROOT = _HERE.parent.parent.parent.parent   # plugins/DuskVerb/tools/tuner/ → repo root
RENDER    = REPO_ROOT / 'build' / 'tests' / 'duskverb_render' / 'duskverb_render'
TARGETS   = REPO_ROOT / 'plugins' / 'DuskVerb' / 'tools' / 'targets'
RAW       = TARGETS / '_raw'
ANCHORS_ROOT = Path ('/home/marc/projects/dusk-audio-tools/anchors/lex')
YABRIDGE  = Path (os.path.expanduser ('~/.vst/yabridge'))

# ─── Preset registry ────────────────────────────────────────────────
@dataclass
class PresetDef:
    key:           str       # filename stem (lex_<key>.json)
    display_name:  str
    anchor_source: str       # human-readable Lex preset path
    fxp_path:      Path
    vst2_plugin:   Path

PRESETS: list[PresetDef] = [
    PresetDef (
        key = 'med_hall',
        display_name = 'Lex Med Hall',
        anchor_source = 'LexHall/02.Medium Halls/000.Med Hall',
        fxp_path = ANCHORS_ROOT / 'fxp' / 'hall_med_hall' / 'Lhl1' / 'dv_lex_hall_med_hall.fxp',
        vst2_plugin = YABRIDGE / 'LexHall.so',
    ),
    PresetDef (
        key = 'concert_hall',
        display_name = 'Lex Concert Hall',
        anchor_source = 'LexConcertHall/factory preset',
        fxp_path = ANCHORS_ROOT / 'fxp' / 'concert_hall' / 'Lch1' / 'lex-concert-hall.fxp',
        vst2_plugin = YABRIDGE / 'LexConcertHall.so',
    ),
    PresetDef (
        key = 'rich_plate',
        display_name = 'Lex Rich Plate',
        anchor_source = 'LexVintagePlate/00.Instrument Plates/010.Rich Plate',
        fxp_path = ANCHORS_ROOT / 'fxp' / 'rich_plate' / 'Lpl0' / 'lex-rich-plate.fxp',
        vst2_plugin = YABRIDGE / 'LexVintagePlate.so',
    ),
    PresetDef (
        key = 'vocal_plate',
        display_name = 'Lex Vintage Vocal Plate',
        anchor_source = 'LexVintagePlate/Vintage Vocal Plate',
        fxp_path = ANCHORS_ROOT / 'fxp' / 'vocal_plate' / 'Lpl0' / 'lex-vintage-vocal-plate.fxp',
        vst2_plugin = YABRIDGE / 'LexVintagePlate.so',
    ),
]

# ─── Metric metadata — pinned to current grader semantics ───────────
# These mirror the JND map graders use today. Embedded in JSON so
# scoring stays version-pinned even if a global JND table drifts later.
GRADED_METRICS = {
    'rt60_per_band':           {'unit': 's',    'jnd': 0.10, 'bin_freqs_hz': [125, 250, 500, 1000, 2000, 4000, 8000, 16000]},
    'edt_500':                 {'unit': 's',    'jnd': 0.10},
    'edt':                     {'unit': 's',    'jnd': 0.10},
    'decay_envelope_db':       {'unit': 'dB',   'jnd': 3.0},   # 12-bin envelope
    'peak_locations_ms':       {'unit': 'ms',   'jnd': 1.0},   # top-4 detected peaks
    'c80':                     {'unit': 'dB',   'jnd': 0.8},
    'c80_per_octave':          {'unit': 'dB',   'jnd': 1.5,  'bin_freqs_hz': [250, 500, 1000, 2000]},
    'd50':                     {'unit': 'dB',   'jnd': 0.8},
    'bass_ratio':              {'unit': 'ratio','jnd': 0.15},
    'treble_ratio':            {'unit': 'ratio','jnd': 0.15},
    'centroid_drift_per_band': {'unit': 'Hz',   'jnd': 2.5,  'bin_freqs_hz': [500, 1000, 2000, 4000]},
    'box_ratio_db':            {'unit': 'dB',   'jnd': 1.0},
    'a_weighted_rms_db':       {'unit': 'dB',   'jnd': 0.5},
    'spectral_crest_db':       {'unit': 'dB',   'jnd': 1.0},
    'time_domain_crest':       {'unit': 'ratio','jnd': 1.5},
    'stereo_correlation':      {'unit': 'corr', 'jnd': 0.08},
    'late_tail_500ms_1s':      {'unit': 'dB',   'jnd': 0.8},
    'late_tail_1s_2s':         {'unit': 'dB',   'jnd': 1.0},
    'late_tail_2s_3s':         {'unit': 'dB',   'jnd': 1.5},
}


def git_sha() -> str:
    try:
        out = subprocess.run (
            ['git', 'rev-parse', '--short', 'HEAD'],
            capture_output=True, text=True, check=True, cwd=str (REPO_ROOT),
        )
        return out.stdout.strip()
    except Exception:
        return 'unknown'


def render_anchor (preset: PresetDef, out_dir: Path) -> tuple[Path, Path]:
    """Run duskverb_render against the Lex VST2 + .fxp. Returns
    (impulse_wav, noiseburst_wav) paths."""
    if out_dir.exists():
        shutil.rmtree (out_dir)
    out_dir.mkdir (parents=True)

    if not RENDER.exists():
        raise FileNotFoundError (f"duskverb_render binary not found at {RENDER}. "
                                 f"Build via: cmake --build build --target duskverb_render -j8")
    if not preset.fxp_path.exists():
        raise FileNotFoundError (f"fxp not found: {preset.fxp_path}")
    if not preset.vst2_plugin.exists():
        raise FileNotFoundError (f"VST2 not found: {preset.vst2_plugin} (check yabridge install)")

    cmd = [
        str (RENDER),
        '--vst2',         str (preset.vst2_plugin),
        '--load-state',   str (preset.fxp_path),
        '--slug',         preset.key,
        '--output-dir',   str (out_dir),
        '--wait-after-load', '2000',
    ]
    print (f"  rendering: {' '.join (cmd[:4])} ...")
    try:
        subprocess.run (cmd, check=True, capture_output=True, timeout=120)
    except subprocess.CalledProcessError as e:
        print (f"  RENDER FAILED stdout: {e.stdout.decode(errors='replace')[-500:]}",
               file=sys.stderr)
        print (f"  RENDER FAILED stderr: {e.stderr.decode(errors='replace')[-500:]}",
               file=sys.stderr)
        raise

    imp = out_dir / f'{preset.key}_impulse.wav'
    nb  = out_dir / f'{preset.key}_noiseburst.wav'
    if not imp.exists() or not nb.exists():
        raise RuntimeError (f"render did not produce expected wavs: {imp}, {nb}")
    return imp, nb


def _json_safe (v):
    """Convert numpy scalars / NaN / Inf into JSON-clean values.
    NaN preserved as the string "NaN" (loader maps back to float)."""
    if v is None:
        return None
    if isinstance (v, float):
        if math.isnan (v): return "NaN"
        if math.isinf (v): return "Inf" if v > 0 else "-Inf"
        return v
    try:
        f = float (v)
        if math.isnan (f): return "NaN"
        if math.isinf (f): return "Inf" if f > 0 else "-Inf"
        return f
    except (TypeError, ValueError):
        return None


def build_snapshot (preset: PresetDef, meas: dict, sha: str) -> dict:
    """Wrap a measure_pair() result dict into the duskverb-target-v1
    JSON schema."""
    metrics_block: dict[str, dict] = {}
    for key, meta in GRADED_METRICS.items():
        if key not in meas:
            # Missing in measurement → skip but flag in metadata so caller
            # knows the snapshot is incomplete.
            continue
        raw = meas[key]
        if isinstance (raw, list):
            value = [_json_safe (x) for x in raw]
        else:
            value = _json_safe (raw)
        entry = {
            'value': value,
            'unit':  meta['unit'],
            'jnd':   meta['jnd'],
        }
        if 'bin_freqs_hz' in meta:
            entry['bin_freqs_hz'] = meta['bin_freqs_hz']
        metrics_block[key] = entry

    return {
        'schema_version': 1,
        'preset_name':    preset.display_name,
        'anchor_source':  preset.anchor_source,
        'fxp_path':       str (preset.fxp_path.relative_to (Path.home()))
                              if preset.fxp_path.is_relative_to (Path.home())
                              else str (preset.fxp_path),
        'vst2_plugin':    str (preset.vst2_plugin),
        'render_settings': {
            'sample_rate': 48000,
            'block_size':  2048,
            'preroll_seconds':       0.5,
            'lex_mix_percent':       100,
            'duskverb_render_git_sha': sha,
        },
        'captured_iso':   datetime.now (timezone.utc).isoformat (timespec='seconds'),
        'metrics':        metrics_block,
        'graded_keys':    list (GRADED_METRICS.keys()),
        'graded_count':   len (GRADED_METRICS),
    }


def export_one (preset: PresetDef, sha: str) -> Path:
    print (f"\n→ {preset.display_name} ({preset.key})")
    raw_dir = RAW / preset.key
    imp, nb = render_anchor (preset, raw_dir)
    print (f"  measuring: {imp.name} + {nb.name}")
    meas = metrics_mod.measure_pair (imp, nb)
    snap = build_snapshot (preset, meas, sha)
    out_json = TARGETS / f'lex_{preset.key}.json'
    out_json.parent.mkdir (parents=True, exist_ok=True)
    out_json.write_text (json.dumps (snap, indent=2) + '\n')
    n_scalar = sum (1 for v in snap['metrics'].values() if not isinstance (v['value'], list))
    n_vector = sum (1 for v in snap['metrics'].values() if isinstance (v['value'], list))
    print (f"  wrote {out_json.relative_to (REPO_ROOT)}  ({n_scalar} scalar + {n_vector} vector metrics)")
    return out_json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument ('--preset', default='all',
                     help='one of: all | ' + ' | '.join (p.key for p in PRESETS))
    ap.add_argument ('--list', action='store_true',
                     help='list known presets and exit')
    args = ap.parse_args()

    if args.list:
        print (f'{"key":15s} {"display_name":30s} {"fxp":50s}  {"vst2"}')
        for p in PRESETS:
            print (f'{p.key:15s} {p.display_name:30s} '
                   f'{str (p.fxp_path)[-50:]:50s}  {p.vst2_plugin.name}')
        return

    sha = git_sha()
    print (f'Exporting Lex anchor targets. git sha: {sha}')
    print (f'Output dir: {TARGETS.relative_to (REPO_ROOT)}')

    todo = PRESETS if args.preset == 'all' else [p for p in PRESETS if p.key == args.preset]
    if not todo:
        print (f"unknown preset: {args.preset}", file=sys.stderr)
        sys.exit (1)

    written = []
    for preset in todo:
        try:
            written.append (export_one (preset, sha))
        except Exception as e:
            print (f"  ! FAILED: {e}", file=sys.stderr)

    print (f"\nDone. Wrote {len (written)}/{len (todo)} targets:")
    for p in written:
        print (f"  {p.relative_to (REPO_ROOT)}")


if __name__ == '__main__':
    main()
