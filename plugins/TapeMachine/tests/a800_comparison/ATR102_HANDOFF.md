# Handoff: tune TapeMachine Classic102 → UAD Ampex ATR-102

Paste everything below as the opening prompt for a fresh session. Self-contained.

---

## Mission
TapeMachine's **A800 mode** (`Swiss800`, machine index 0) is already tuned to the UAD Studer A800
across 7 categories (done — `report/A800_TUNING_SUMMARY.md`). Do the **same** for the **Classic102
mode** (`Classic102`, index 1), tuning it to the **UAD Ampex ATR-102**
(`/Library/Audio/Plug-Ins/Components/uaudio_ampex_atr-102_tape.component`).

Classic102 was left on the OLD 3-band Jiles-Atherton saturation (preserved untouched during the
A800 work). It is **massively over-driven** vs the clean ATR-102 mastering deck — that's the
dominant gap.

### Hard constraints
- **A800 (Swiss800) must stay byte-identical.** Every change is gated on `machine == Classic102`.
  The A800 is done — do not regress it. Verify with a Swiss800 render before/after.
- **RT-safety**: no allocation / lock / I/O in `processSample` (per sample at the OS rate × 2ch).
- **No 8× global oversampling** (per-track CPU budget; chain is 4×). Anti-alias nonlinearities with
  the shared `duskaudio::Local2xStage` (see below).
- Keep CPU light; cache block-constants in `updateFilters` (the A800 does this).

### Acceptance criteria (match the ATR-102, 456/NAB/15/Repro reference unless noted)
- Harmonics @−6 dBFS: 3rd within ~3 dB of ATR's −52, 2nd within ~3 dB of −56 (currently mine −25/−34).
- THD @−6: ~0.55% (currently 6.2%). IMD already ~matches (1.45 vs 1.47%).
- FR: head bump + HF track ATR (see baseline); Sync not too dark; Input slightly dark like ATR.
- Bias behaves like ATR; idle noise (Hiss&Hum) matched when enabled; crosstalk matched.
- Aliasing ≤ −60 dB (render with Wow=0 Flutter=0!). `auval -v aufx DsTM Dusk` passes. Swiss800 unchanged.

## Baseline gap (measured 2026-07-09, Classic102 vs ATR-102, 456/NAB/15/Repro)
| Cat | mine (Classic102) | ATR-102 | note |
|---|---|---|---|
| Harmonics 3f/2f @−6 | −25 / −34 (harsh) | −52 / −56 | mine ~27 dB too hot on 3rd |
| THD @−6 | 6.2% | 0.55% | ~11× too distorted |
| IMD | 1.45% | 1.47% | already close |
| Transient crest | 27.2 | 26.7 | close |
| Input HF@10k | +0.09 | −0.83 | ATR darker even in Input (transformer/amp) |
| Sync HF@10k | −2.76 | −0.83 | mine too dark in Sync |
| Repro HF@10k | −1.00 | −0.83 | ~close |
| FR (ATR sweep, rel 1k) | — | +3.0@30 +1.1@50 −0.1@100 +0.7@10k −3.5@20k | ATR: low bump + slight 20k rolloff |

## Mandatory reading (in order)
1. `report/A800_TUNING_SUMMARY.md` — the template: what was tuned and how.
2. `plugins/TapeMachine/core/TapeMachineDSP.hpp` — the A800 implementation to MIRROR for Classic102:
   - `a800Shaper` (order-7 poly + tanh knee), `a800Saturate` (shaper + even term), `a800SoftLimit`
     (tanh-knee limiter), all gated on `machine == Swiss800` in `processSample`'s saturation block.
   - `getHeadBump(machine, speed)` — per-machine/speed head-bump lookup (Classic102 slot currently
     holds the ORIGINAL values 40.3/6.30/1.82 @7.5, 62/4.5/1.4 @15, 93/3.15/1.12 @30 — retune these).
   - `hfLossScale`, `biasGainDb`, `syncGapMult` — machine-gated in `updateFilters`.
   - `m_a800Drive`/`m_a800DriveInv` cached in `updateFilters`; `m_a800ShaperOs`/`m_a800LimOs`
     (`Local2xStage`, local-2× antialiasing); `a800IdleNoise` + `m_humPhase`/`m_humPhaseInc`.
   - Crosstalk: `TapeMachineDSP.cpp` ~line 381, `crosstalkAmount` machine-gated.
   - Current Classic102 saturation: the `else if (drive > 0.001f)` 3-band J-A branch + band ratios
     `{0.65,1.0,0.30}` (`getBandDriveRatios`), `machineFactor 1.08`, `m_hasTransformers=(Classic102)`
     input/output `TransformerSaturation` stages.
3. `plugins/shared-dpf/dsp/DuskOversampler.hpp` — `duskaudio::Local2xStage` (now shared): antialias
   any memoryless nonlinearity: `y = stage.process(x, [](float s){ return f(s); });`. One instance
   per nonlinearity per channel; `reset()` in the core `reset()`.
4. `transfer_fit.py` — fits an order-7 waveshaper from a UAD render's harmonics-vs-level.

## Recommended approach (mirror the A800)
1. **Saturation (biggest lever).** Render the ATR-102's `thd_steps` (stepped 1 kHz), run an
   ATR-adapted `transfer_fit.py` to fit a `classicShaper` (order-7 poly + tanh knee). Add
   `classicSaturate` (+ small even term if the fit under-captures 2nd), a `classicSoftLimit`, and run
   both inside their own `Local2xStage` instances — exactly as the A800 does. Gate on `Classic102`.
   DECISION: the ATR has a transformer stage; mine's Classic102 already runs `inputTransformer`/
   `outputTransformer`. The fitted curve captures the WHOLE ATR chain's harmonics, so either (a)
   bypass mine's transformer stages for Classic102 and let the waveshaper do it (cleanest, like A800
   dropping the J-A), or (b) keep a light transformer + fit the residual. Measure both.
2. **FR.** Retune `getHeadBump` Classic102 rows to ATR's bump (+3@30, +1@50) and set an
   `hfLossScale`/rolloff so HF tracks ATR (flat-ish then −3.5 @20k). Add the ATR's slight
   Input/transformer HF rolloff (ATR is −0.83 @10k even in Input).
3. **Bias / Sync / noise / crosstalk.** Machine-gate like the A800: bias-vs-THD + bias-vs-HF fitted
   to ATR; Sync gap; `a800IdleNoise`-style hiss+hum for Classic102 (ATR "Hiss & Hum"); crosstalk to
   match ATR "Crosstalk On".
4. **Aliasing.** Reuse `Local2xStage` (shared). Verify ≤ −60 dB with Wow=0 Flutter=0.

## Harness (built; ATR support added — but finish the ATR param maps)
Directory `plugins/TapeMachine/tests/a800_comparison/`. Deps: `numpy scipy soundfile matplotlib`.
- **Build AU**: `cd plugins/TapeMachine/dpf-plugin && cmake --build build --target tape_machine_2-au -j8`.
  **Ninja often misses `core/`+`shared-dpf/dsp/` header edits — `touch` the changed file (or the .cpp)
  to force the rebuild**, then re-measure. (This bit us: a stale binary read the old numbers.)
- **`deep_probe.py` now takes `DEEP_MACHINE=ATR102`** (env var) → targets Classic102 vs the ATR-102,
  renders into `renders/deep_atr/`. `DEEP_MACHINE` defaults to A800. Run:
  `DEEP_MACHINE=ATR102 python3 deep_probe.py`.
- **STILL TODO in the harness for ATR** (A800 param names are hardcoded in a few probes):
  - Bias sweep uses `--nparam "Bias=..."`; ATR's bias params are **`L Bias` + `R Bias`** (or set
    `Stereo Link` and one). Fix the bias render for ATR.
  - Noise pass uses `--param "Noise=On"`; ATR's is **`Hiss & Hum=On`**. Fix.
  - Path modes use `Path Select` (ATR has it; labels `REPRO`/`INPUT`/`SYNC` — verify by audio).
  - `render_matrix.py`/`score_matrix.py`/`configs.py` are A800-only (UAD path + `Tape Machine=0`
    hardcoded). Generalize them (add a machine dimension) if you want the full 18-config matrix;
    or work from `deep_probe.py` at the 456/NAB/15 reference like the early A800 phases did.
- **ATR-102 param facts** (from `--list-params`): `IPS` {3.75,7.5,15,30}, `Tape Type` {250,456,900},
  `Cal Level` {+3,+6,+7.5,+9}, `Emphasis EQ` (NAB/CCIR/AES — set by label, `--dump-params` mis-decodes),
  `Head Width` (1/4,1/2 — no mine equivalent; leave default 1/2), `Path Select`, `L/R Bias`,
  `Crosstalk`, `Wow & Flutter`, `Hiss & Hum`, `Transformer`, `Stereo Link`, `Power`. Tape map
  456→456, GP9→900, 250→250 (911 has no ATR analogue). Classic102 std EQ = NAB/AES.
- **Aliasing metric**: `deep_probe.aliasing(wav)` on a render of `stimuli/alias.wav` — **render with
  `--param "Wow=0" "Flutter=0"`** or flutter FM sidebands read as ~−47 dB spurs (they are NOT aliasing).
- **UAD needs PACE running** or it silently bypasses (flat output). Fix:
  `sudo launchctl bootstrap system /Library/LaunchDaemons/com.paceap.eden.licensed.plist;
   sudo launchctl kickstart -k system/com.paceap.eden.licensed` (verify `/var/tmp/com.paceap.eden.licensed/`).
- **DPF param gotcha**: TapeMachine choice params take the integer index; FLOAT params read the value
  as NORMALISED 0-1 (`--param "Bias=20"` clamps to max) — use `--nparam` for floats, or defaults.

## Verification obligations
1. Build (touch to force) + `auval -v aufx DsTM Dusk` PASSES.
2. `DEEP_MACHINE=ATR102 python3 deep_probe.py` — harmonics/THD/IMD/FR/bias/noise/crosstalk within the
   acceptance bands; aliasing ≤ −60 dB (Wow=0/Flutter=0).
3. **Swiss800 (A800) render is byte-identical to before your changes** (prove the gating).
4. Report before/after per category + CPU note. Negative results with root cause are valid.

## Scope wall
Touch only the `Classic102` paths in `plugins/TapeMachine/core/`. Do NOT change Swiss800/A800
constants, the global OS factor, parameter IDs/ranges/defaults, or the UI. Update
`report/` docs + the memory file when done.
