# DuskVerb tuning handoff — next session

Paste this as the opening prompt when resuming preset-vs-anchor calibration.

---

## Mission
Calibrate DuskVerb factory presets to SOUND like their external reference anchors
(Valhalla VintageVerb "VVV", Lexicon, Valhalla Shimmer), measured by `full_check.py`
gate count (`n_fail`). The EAR is the final arbiter, not the gate count. Brutal
honesty over false optimism. Never bake gamed / marginal / overshoot params. Never
mention convolution — these are ALGORITHMIC reverbs, fix them algorithmically.
Never use pedalboard (BANNED). Renders MUST be 100% wet: `--param "Dry/Wet=1.0"
--param "Bus Mode=1" --param "Freeze=0"`.

## State on entry (branch 87-fix-fdn-quadtank)
- Commits NOT yet pushed (held for CodeRabbit free-tier cooldown — ask user before push):
  - `bf8977d` 79 Vocal Chamber Width 1.14->0.97 (26->23)
  - `8ca38ec` QuadTank 3-band -> 5-band damping (79VC 23->21)
- QuadTank engine now has 5-band damping (FiveBandDamping in QuadTank.cpp/.h),
  per-preset via params `qt_himid_mult` / `qt_air_mult` (default -1 = transparent
  = bit-identical 3-band). Only 79 Vocal Chamber overrides them (kQuadBandByName).

## Tooling (no rebuild needed for APVTS-param sweeps)
```
R=build/tests/duskverb_render/duskverb_render; VST=~/.vst3/DuskVerb.vst3
FC=plugins/DuskVerb/tools/tuner/full_check.py
ANC=~/projects/dusk-audio-tools/tuner_runs/anchors   # vvv-79vc, vvv-ambience, vvv-vocal-hall, vvv-cathedral, vvv-blade-runner, vvv-drum-plate, vvv-tiled-room, vvv-vocal-plate, vvv-84-small-room, vvv-bright-hall
rm -rf /tmp/q; mkdir -p /tmp/q                       # ALWAYS rm -rf between renders (stale-wav bug)
$R --vst3 $VST --program "<PRESET>" --param "Dry/Wet=1.0" --param "Bus Mode=1" --param "Freeze=0" \
   --slug S --prerun-seconds 5 --sustained-pink-seconds 4 --output-dir /tmp/q
python3 $FC /tmp/q $ANC/<anchor> --name "<PRESET>" --json   # JSON_RESULT line has n_fail + fails[]
```
- Diagnose engine by INDEX (`--param "Algorithm=N"`), NOT by name (harness name->index buggy).
  Engines: Dattorro 0, DattorroVintage 1, SixAPTank 2, QuadTank 3, FDN 4, Spring 5,
  NonLinear 6, Shimmer 7, VintageTank 8, ReverseRoom 9, AccurateHall 10,
  SparseField 11, AccurateHall32 12, TiledRoom 13, DenseHall 14.
- QuadTank 5-band sweep, live: `--param "QT Hi-Mid Multiply=X" --param "QT Air Multiply=Y"`
  (lower = more HF damping; -1 = transparent). Width is also live: `--param "Width=W"`.
- After ANY .cpp/.h edit: `cmake --build build --config Release --target DuskVerb_VST3 -j$(nproc)`
  WAIT for "Installing ... DuskVerb.so" before rendering (mid-install race gives wrong n_fail).
- Validate: `./tests/run_plugin_tests.sh --plugin "DuskVerb" --skip-audio`
  (NOTE: "Bus Mode not restored on setStateInformation" pluginval state failure is
  PRE-EXISTING on the clean baseline — unrelated, separate ticket if you want it.)

## Per-preset n_fail + engine (as of 2026-06-03)
| Preset | Engine | n_fail | Notes |
|---|---|---|---|
| Vocal Hall | FDN(4) | 14 | shipped baseline, PR #96 frozen, do not regress |
| 79 Vocal Chamber | QuadTank(3) | 21 | width + 5-band done; floor reached |
| Ambience | QuadTank(3) | 25 | attack-walled (see below) |
| Small Drum Room | Dattorro(0) | 28 | NOT QuadTank — re-engined |
| Tiled Room | FDN(4) | ~22 | |
| Vocal Plate | FDN(4) | ~26 | |
| Cathedral | FDN(4) | ~26 | |
| Blade Runner 224 | FDN(4) | ~26 | |
| Drum Plate | FDN(4) | ~27 | |

## DEMONSTRATED WALLS — do NOT re-grind (see memories)
- **Per-band T60 / edt / decay-SHAPE coupling** (FDN AND QuadTank): a band's
  feedback gain sets its decay AND its level AND (via curve) its shape — can't
  bend one without the others. 5-band/multiband split narrows the collateral but
  doesn't remove it. ~11 of 79VC's 21 are this. [[duskverb_fdn_t60_coupling_wall]]
  [[duskverb_quadtank_coupling_wall]]
- **FDN attack ~26ms floor** (shortest line 1151 smp). [[duskverb_fdn_attack_floor]]
- **edt sub <100Hz** follower deaf <200Hz. **Low-end time-freq coupling** (<250Hz
  warmth vs boom). **diffusion_flux** = engine modal character (FDN dense-Gaussian
  vs VVV sparse-modal). All structural.
- **Shimmer 12kHz AA ceiling** (Black Hole / Deep Blue Day top octave).
  [[duskverb_shimmer_12k_aa_ceiling]]
- QuadTank loMid split breaks T60-500; one-pole/high-shelf structHF darken globally.

## CLEAN levers that DID work (the playbook)
1. **Width retune** (output-tap decorrelation, NOT coupling-bound): if a preset's
   stereo_corr / width-bands are anti-phase vs anchor, the baked Width went past
   unity — drop it. 79VC 1.14->0.97 closed 3-4 gates. CHECK every QuadTank/FDN
   preset's width direction vs its anchor first (Ambience is OPPOSITE — anchor is
   decorrelated -0.32, needs a per-band width TILT, global Width saturates).
2. **QuadTank 5-band hiMid+air** (now wired): closes cent_500 + HF-T60 partial
   WITHOUT darkening the mid (which holds centroid). Needs BOTH hiMid (4-8k) and
   air (>8k) — hiMid alone is worse. Per-preset values only.

## Concrete next targets (highest value first)
1. **FDN presets (Tiled/VP/Cathedral/BR/DP)** — the bulk of remaining fails. They
   already have FiveBandDamping + kFiveBandByName. Re-run the width-direction check
   + scoreboard/warm-start Optuna on full_check --json (NOT the proxy loss; NOT
   tune_preset — parse_best bug). [[duskverb_tuning_method_scoreboard_warmstart]]
2. **Ambience** — needs an ER/early-field fix for attack +58ms (washy onset; the
   ER rising-onset infra from Vocal Hall: er_boost/er_rise, but tuned for a SHORT
   room) + per-band width tilt. Damping won't help (proven). Engine-side: QuadTank
   has no rising-ER; consider routing the FDN ER path or an input-transient gate.
3. **79 Vocal Chamber** is at floor 21 — only structural left, leave it.

## Standing constraints
- Commits: NO Co-Authored-By Claude trailer. PR bodies: omit Test Plan + robot footer.
- No em dashes in manuals; no "Recipe" in manual headings.
- Caveman mode may be active (terse). Phased execution: <=5 files per phase, verify
  (build + render + full_check) before baking. Validate ALL targeted presets, not one.
- Every listening-test issue -> automated gate + Optuna loss term, never manual patch.
