# DuskVerb Automated Preset Tuner

One command per preset. No manual param edits.

## Files

| File | Role |
|---|---|
| `metrics_external.py` | Peak-aligned, noise-floor-gated metrics. Don't edit unless adding a new metric. |
| `wav_audit.py` | Independent cross-validator (no shared code). Use to verify metrics_external isn't lying. |
| `full_check.py` | PASS/FAIL gates per category. Every category user has flagged → one gate here. |
| `preset_vs_external_optuna.py` | Optuna sweep. Loss function = every gate's metric. Free param ranges = clamped to physical bands. |
| `tune_preset.py` | One-command wrapper: sweep → apply → auto level-match → full_check. |

## Workflow

### Tuning a new preset

1. Render the reference anchor (Lex / VVV / etc.) at 100% wet, 5 s preroll:
   ```
   build/tests/duskverb_render/duskverb_render \
       --vst2 <YABRIDGE_VST2_DIR>/<RefPlugin>.so \
       --load-state <ref-preset>.fxp \
       --param "Mix=1.0" \
       --prerun-seconds 5.0 \
       --output-dir /tmp/anchor_<name> "RefAnchor"
   ```

2. Run the tuner:
   ```
   python3 plugins/DuskVerb/tools/tuner/tune_preset.py "<DV preset name>" \
       --anchor-rendered /tmp/anchor_<name>/RefAnchor_noiseburst.wav \
       --trials 1500 --workers 4
   ```

3. Wait ~12-15 minutes.

4. If all gates pass → paste the printed lock-in values into
   `FactoryPresets.h` + `tests/duskverb_render/render.cpp`, commit.

5. If any gate fails → **DO NOT** patch params by hand. Either:
   - Tighten the failing gate's loss weight in `preset_vs_external_optuna.py`
   - Add a new loss term if the gate has no Optuna equivalent
   - Re-run `tune_preset.py`

### When a listening test catches an issue full_check missed

This is the contract. Every issue from listening → permanent tooling
upgrade. Manual fixes are forbidden.

1. **Find a metric** that captures the issue. Use `wav_audit.py` or
   ad-hoc scripts. If no existing metric works, build one.

2. **Add a gate** to `full_check.py` with a numeric threshold.

3. **Add a loss term** to `preset_vs_external_optuna.py` with a
   reasonable weight.

4. **Re-run tune_preset.py** for the affected preset(s).

5. **Commit** the new gate + loss term to the repo so all future
   presets benefit.

## Gate categories (current)

| Category | Gate | Why it exists |
|---|---|---|
| Level — snare RMS | ±1.5 dB | Loudness must match first |
| Level — noiseburst RMS | ±2.0 dB | Same on broadband stimulus |
| Band — sub <100 Hz | ±2.0 dB | User caught sub-bass surplus on a previous sweep |
| Band — low 100-250 Hz | ±2.0 dB | User caught low-mid surplus on a previous sweep |
| Band — mid 250-1k Hz | ±2.0 dB | Required for spectral neutrality |
| Band — mid 1-4k Hz | ±2.0 dB | Required for upper-mid neutrality |
| Band — hi 4-12k Hz | ±3.0 dB | Air-band engine ceiling tolerated |
| Decay — tail_t30 | ±15 % | Initial-tail length |
| Decay — tail_t60 | ±25 % | Late-tail length (loose because engine ceilings) |
| Spectral — cent_50 | ±15 % | Early-bloom brightness |
| Spectral — cent_500 | ±15 % (skipped if Lex below noise floor) | Mid-tail darkening |
| Envelope — env_p2p | ±5 dB (skipped if Lex tail below floor) | Transient density |
| Stereo — correlation | ±0.15 | Mono-compatibility + image |
| EQ shape — spec_L1 mean | ±2.0 dB | RMS-norm 1/3-oct shape |
| EQ shape — spec_L1 max | ±5.0 dB | No single-band spike |
| Oscillation — env P2P delta | ±4 dB (skipped if Lex envelope below floor) | Modulator artifact |

## DSP guard-rails enforced via FREE_PARAMS clamps

| Param | Range | Reason |
|---|---|---|
| Width | [0.5, 1.05] | >1.05 with sparse Dattorro produces anti-correlated L/R |
| Bass Multiply | [0.5, 1.5] | Wider → Optuna abuses as EQ knob |
| Mid Multiply | [0.5, 1.5] | Same |
| Treble Multiply | [0.5, 1.5] | Same |
| Low Crossover | [80, 600] Hz | Below 80 collapses sub/bass; above 600 collapses bass/mid |
| High Crossover | [3000, 10000] Hz | Above 10k collapses mid/treble |

## DO NOT

- Edit FactoryPresets.h positional values in response to a listening report
- Edit render.cpp preset rows in response to a listening report
- Run an Optuna sweep without the gates active
- Trust spec_L1 alone — it's RMS-normalized and hides absolute level mismatches
- Compare DV vs Lex without peak-alignment (Lex VVP has ~150 ms pre-delay baked in)
- Render without 5 s preroll (modulator + biquad state need time to settle)
- Tune on impulse stimulus (Dirac aliases on time-variant reverbs)
- Tune without snare-RMS level match first (loudness invalidates every other metric)
