# DuskVerb Preset Tuner

Tooling for matching each anchored DuskVerb preset to its commercial anchor
(Valhalla VintageVerb, Lexicon, Valhalla Shimmer). The operating picture,
fleet map, engine workstreams, and traps live in
`HANDOFF_2026-07-06_opus48.md`; read it first. This README is just the
tool index plus the day-to-day flow.

## Core scripts

| File | Role |
|---|---|
| `fleet_audit.py` | Renders + scores the whole fleet; anchor-hygiene precheck; table/calibration verifiers. Run at every session start/end. |
| `full_check.py` | PASS/FAIL gate harness for one preset vs its anchor dir. `--json` emits the machine-readable failing-gate list. The Optuna objective and the scoreboard both call this. |
| `preset_vs_external_optuna.py` | Optuna sweep. Objective = `full_check.py --json` n_fail (+ margin). Warm-start via `--enqueue-json`. Cold-start use only. |
| `tune_preset.py` | Thin cold-start wrapper around the Optuna sweep. NOT the primary path — see its docstring. `--self-test` proves parse_best still tracks the sweep output. |
| `calibrate_octave_t60.py` | Renders + measures the per-octave release-T60 vs the anchor and prints a `ROW: { ... }` payload to hand-paste into `kAccurateHallT60ByName` / `kDenseHallOctaveT60ByName` (it does NOT rewrite the source). Re-run after any engine delay-line change. |
| `metrics_external.py` | Peak-aligned, noise-floor-gated metrics used by full_check. Edit only when adding a metric. |
| `wav_audit.py` | Independent cross-validator (no shared code) to confirm metrics_external isn't lying. |

Anchors: VVV under `~/projects/dusk-audio-tools/tuner_runs/anchors/`; Lexicon +
Shimmer under `~/projects/dusk-audio-tools/anchors/rendered/`.

## Fleet audit

```
python3 fleet_audit.py --out            # dated scoreboard_<date>.md + .json (per-preset failing gates)
python3 fleet_audit.py --verify-tables         # keyed-map rows all live (exit 0 = clean)
python3 fleet_audit.py --verify-calibration    # commanded-vs-realized octave-T60 drift
python3 fleet_audit.py --no-render             # reuse /tmp/audit_* renders (STALE after a rebuild)
```

The anchor-hygiene precheck runs automatically: missing/silent anchor = FATAL
abort; non-FLOAT subtype or dry-only suspicion = warning.

A `--verify-calibration` BROKEN flag is HYGIENE, not a free gate win: it means
commanded != realized, but the gates score the *realized* value, which often
already matches the anchor. Recalibrating can REGRESS a tuned preset.

## Score one preset

```
python3 full_check.py <dv_render_dir> <anchor_dir> --name "Vocal Hall"          # human report
python3 full_check.py <dv_render_dir> <anchor_dir> --name "Vocal Hall" --json   # failing-gate list
```

## Per-preset tuning order (HANDOFF §5)

Manual per-gate tuning is the primary method. Optuna is cold-start ONLY —
re-sweeping an already-tuned preset floors at its baseline.

1. **Gain-match** DV to anchor RMS first (biggest single gate-count mover).
2. **1-D clean levers** one at a time, re-score after each: Width, Lo Cut, Decay.
3. **Manual per-gate deltas**: read the failing-gate list, map each gate to its
   lever (HANDOFF walls/levers history).
4. **Baked-table edits**: FactoryPresets.h positional row + keyed maps
   (kPmbByName, octave-T60, kPteqByName). Edit the map + rebuild — runtime
   `--param` edits of these are desync-BLOCKED by design (load-bearing).
5. **Octave-T60 recalibration** (`calibrate_octave_t60.py`) after any engine
   delay-line change.
6. **Listen**: render snare + piano + a sustained pad, A/B at matched loudness,
   then hand off to the user's ear (final sign-off, not QA).

## Env-var engine sweeps (`DUSKVERB_*`)

New/experimental engine paths ship behind `DUSKVERB_*` env vars (default =
bit-null off) so they can be A/B'd via `duskverb_render` without a rebuild,
e.g. `DUSKVERB_PMB`, `DUSKVERB_DIFFER` / `DUSKVERB_DIFFERHP`, `DUSKVERB_VELVET`.
Sweep by exporting the var, rendering, and scoring with full_check; bake the
winner into its keyed map once the ear approves. Grep the C++ for the current
`DUSKVERB_` set — the list changes per campaign.

## Cold-start Optuna (when warranted)

```
# Warm-start from the shipping baseline so trial 0 = baseline (can only improve):
duskverb_render --program "Vocal Hall" --dump-params > /tmp/vh_baseline.json

python3 tune_preset.py "Vocal Hall" \
    --anchor-rendered <anchor_dir>/<Anchor>_noiseburst.wav \
    --enqueue-json /tmp/vh_baseline.json \
    --trials 300 --workers 6
```

Limits: `--workers <= 6`, `--trials <= 300` (a 1500/4-worker cold sweep OOM'd
the 32 GB box; the wrapper clamps). The wrapper prints the winning params for
you to bake by hand — it does not edit any source file. Drive
`preset_vs_external_optuna.py` directly if you need locks / range overrides.

## The gate contract

Every listening issue full_check missed becomes permanent tooling:
add a metric (`metrics_external.py` / `wav_audit.py`), add a gate
(`full_check.py`) with a numeric threshold, add a loss term
(`preset_vs_external_optuna.py`), re-score. Manual one-off patches without a
gate don't scale to the 18 anchored presets.

## Render rules (violations have burned days — HANDOFF §0)

- 100% wet always: `--param "Dry/Wet=1.0" --param "Bus Mode=1"` (Freeze=0).
- `--program <name>`, NOT `--preset` (the latter re-applies a stale mirror).
- Level-match to anchor RMS before judging any spectral/centroid metric.
- Write analysis WAVs `sf.write(..., subtype='FLOAT')` (PCM16 requant fakes walls).
- NEVER use pedalboard. `duskverb_render` (JUCE CLI) only.
