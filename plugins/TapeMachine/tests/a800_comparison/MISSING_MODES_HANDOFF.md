# Handoff: add the missing ATR-102 / Studer A800 modes to TapeMachine 2

Paste everything below as the opening prompt for a fresh session. Self-contained.
Caveman mode may be active (SessionStart hook) — technical substance is unchanged.

---

## Mission
TapeMachine 2 (DPF) has two machines — **Swiss800** (idx 0, tuned to the UAD Studer A800)
and **Classic102** (idx 1, tuned to the UAD Ampex ATR-102). Both are matched to the UAD
across the 7 measurement categories AND generalized across speed/tape/EQ/cal (FR ≤~1–2 dB).
**Now add the modes we are still missing**, so each machine covers every real hardware mode,
tuned to the UAD:

| Gap | Ours now | Target | Machine |
|---|---|---|---|
| **CAL remap** | `kCalibration` = 0 / +3 / +6 / +9 dB (`pCalibration*3`) | **+3 / +6 / +7.5 / +9** | both |
| **Tape 900** | `kTapeType` = 456 / GP9 / 911 / 250 | 250 / 456 / **900** / GP9 (911≠900) | both |
| **3.75 IPS** | `kTapeSpeed` = 7.5 / 15 / 30 | **+3.75** (ATR only; non-std for A800) | ATR |
| **Head Width** | *(no param)* | **1/4" / 1/2" / 1"** — new param + gap/HF DSP + UI | ATR |

Decisions already made by the user:
- **CAL: just remap (OK to break saved state).** Values become exactly +3/+6/+7.5/+9.
- **Head Width: FULL — param + DSP + UI.**

## Hard constraints
- Changing ONE machine must not regress the OTHER. Use the byte-identical guard
  `scratchpad/tape_guard.sh <machineIdx> <outdir>` (idx 0 = Swiss800, 1 = Classic102):
  capture a baseline before, diff md5s after. (Note: the gain-link change already altered
  BOTH machines' auto-comp output, so re-baseline fresh — don't diff against pre-gain-link refs.)
- **RT-safe** `processSample` (no alloc/lock/IO). Cache block-constants in `updateFilters`.
- No 8× global OS (4× chain). Anti-alias new nonlinearities with `duskaudio::Local2xStage`.
- `auval -v aufx DsTM Dusk` must pass. Build: `cd plugins/TapeMachine/dpf-plugin &&
  cmake --build build --target tape_machine_2-au -j8` — **`touch` the changed core/UI file first**
  (Ninja misses header-only edits; a stale binary reads old numbers).

## Current code state (all committed on branch `tapemachine2-tuning`)
- **`core/TapeMachineDSP.hpp`** — both machines saturate via measured waveshapers
  (`a800Shaper` / `classicShaper`, order-7 poly + tanh knee, in `Local2xStage`), NOT the old
  3-band J-A (removed). `getHeadBump(machine,speed)` has per-machine/speed rows (Classic102 has a
  NEGATIVE-gain LF dip @30 IPS; A800 a broad bump). `hfExt` is machine+speed specific
  (see `updateFilters`). `dcBlockFreq` is machine+speed gated. Idle noise = `idleNoise(scale…)`
  (a800IdleNoise / classicIdleNoise). Crosstalk in `core/TapeMachineDSP.cpp`.
- **`core/TapeMachineDSP.cpp`** — gain-link fix: `targetOutputGain = -inputGainDb + calDb + kFixed`
  (kFixed 6.3 Swiss / 5.5 Classic). **`calDb` here is `pCalibration*3` — UPDATE it when you remap CAL.**
- **`dpf-plugin/TapeMachineParams.hpp`** — `kTapeSpeed / kTapeType / kCalibration` label arrays +
  the `TmParam` table (min/max/def, `numChoices`). This is where speed/tape/cal CHOICES live.
- **`dpf-plugin/TapeMachineUI.cpp`** — ImGui UI. `selector()` = the choice dropdowns (drawSelectors),
  `knob()` returns a changed-bool, `applyParam()` sets a param + host-notify. Gain-link knob-lock +
  bias-knob veil already wired in `drawControls`. Head Width needs a new `selector` here.

## The 4 sub-tasks

### (a) CAL remap  0/3/6/9 → +3/+6/+7.5/+9   [do first; contained but has ripple]
1. `kCalibration[]` labels → `{"+3dB","+6dB","+7.5dB","+9dB"}`.
2. Replace `pCalibration*3.0f` with a lookup `{3, 6, 7.5, 9}` in **every** site: the main
   `calibrationDb` in `TapeMachineDSP.cpp` (~line 264), the gain-link `calDb` (autoComp block),
   and the `outGain.snap` in `prepare`. (grep `* 3.0f` and `Calibration`.)
3. **RIPPLE — update the harness/reference cal indices** (index 2 was +6, now +7.5!):
   - `deep_probe.py` `MINE_BASE` uses `Calibration=2` → change to `1` to keep the reference at +6.
   - `matrix_probe.py` `CAL = {"+3":("1",…),"+6":("2",…),"+9":("3",…)}` → new indices
     `+3=0, +6=1, +7.5=2, +9=3` (add +7.5). UAD `Cal Level` labels are unchanged (+3/+6/+7.5/+9 dB).
   - `probe_mine.py` / `fr_probe.py` bases likewise (Calibration `2`→`1` for +6).
4. Verify the reference-config numbers (deep_probe) are unchanged after re-indexing.

### (b) Tape 911 → 900
- `kTapeType[]` "Type 911" → "Type 900" (or add 900 if you want all of 250/456/900/GP9 +911).
  The UAD `Tape Type` label for the 900 formulation is **"900"** on the Studer, **"900"** on the ATR
  (verify with `--list-params` / by audio). Update `getJAParams`-era tape rows are GONE (J-A removed);
  the live per-tape data is `getTapeCharacteristics` (coercivity/saturationPoint/hfLoss/lfEmphasis/
  noiseFloor). Retune the 900 row vs the UAD via `matrix_probe.py tape`.

### (c) 3.75 IPS (ATR)
- Add `Speed_3_75_IPS` to the `TapeSpeed` enum (index 0, shifting others +1) OR append (index 3) —
  **appending is far safer** (don't renumber existing speeds; every `switch(speed)` and the harness
  SPEED indices assume 0/1/2 = 7.5/15/30). Append 3.75 as index 3.
- Extend EVERY per-speed table for the new case: `getHeadBump`, `hfExt`, gap-loss freq/amount,
  the EQ time-constants in `updateFilters` (NAB/CCIR/AES τ for 3.75), `SpeedCharacteristics`
  (headBumpMultiplier/hfExtension/noiseReduction/flutterRate/wowRate), the noise tilt, dcBlockFreq.
- `kTapeSpeed[]` += "3.75 IPS"; bump `tapeSpeed` param max to 3, update UI selector width.
- The ATR has 3.75; the Studer does NOT — mark 3.75 non-standard for A800 (the UI already has a
  non-standard amber indicator via `selectorIsStd`/`comboIsStd`; add the rule).
- TUNE vs the ATR: `MATRIX_MACHINE=ATR102 python3 matrix_probe.py` — add a "3.75" row (SPEED dict +
  the mode loops). UAD `IPS` label is **"3.75 IPS"**.

### (d) Head Width (ATR) — param + DSP + UI
- The ATR-102 `Head Width` param = **1/4", 1/2", 1"** (our tuning used the default 1/2"). Head width
  changes the gap loss (wider gap/track → different HF) and level. Model it as a machine-gated
  (Classic102) HF/gap effect in `updateFilters` (a width-dependent scale on the gap-loss / hfExt).
- New param: add `kParamHeadWidth` to `TapeMachineParams.hpp` (`kHeadWidth[] = {"1/4\"","1/2\"","1\""}`,
  choice, default 1/2"), plumb through the DPF plugin (setter in `TapeMachineDSP` like the others),
  add a `selector()` in the UI (there is room — or replace/relayout a selector cell).
- TUNE vs the ATR: render the ATR at each Head Width (UAD param `Head Width` labels `1/4`,`1/2`,`1`)
  and match FR. Add a HEAD dimension to `matrix_probe.py`.

## Harness (all committed under `tests/a800_comparison/`; renders/report git-ignored)
- **`matrix_probe.py`** — mine vs UAD across settings. `MATRIX_MACHINE=A800|ATR102`, modes
  `speed|tape|eq|cal|full`. THE tool for per-mode FR/THD tuning. **Bug already fixed**: render to
  UNIQUE dests (a shared tmp path once made every diff read +0.0 — the "suspiciously clean" tell).
- **`deep_probe.py`** — the 7-category probe. `DEEP_MACHINE=A800|ATR102`.
- **`probe_mine.py` / `fr_probe.py`** — fast mine-only harmonics / FR vs cached UAD refs.
- **`transfer_fit.py`** — refit a waveshaper: `FIT_INPUT=… FIT_FUNC=… FIT_ORDER=7`.
- UAD has **no factory presets** (`--list-programs` → 1 "Untitled"); "modes" = the settings matrix.
  The render binary DOES support `--list-params` / `--param <DisplayName>=<label|idx>` /
  `--nparam <name>=<0..1>` (floats). **DPF float params via `--param` are 0..1 normalised — use
  `--nparam`.** UAD needs PACE running or it silently bypasses (flat) — see the memory file.
- Reference config: 456 / NAB / 15 IPS / Repro / +6 cal, W&F off, both plugins.

## Mandatory reading (in order)
1. Memory `a800-comparison-harness.md` — the full campaign log + every gotcha (cal-index ripple,
   matrix-probe dest bug, kFixed derivation, the "suspiciously clean" rule, PACE).
2. `report/ATR102_TUNING_SUMMARY.md` + `report/A800_TUNING_SUMMARY.md` — what was tuned and how.
3. `core/TapeMachineDSP.hpp` `updateFilters` — where every per-speed/tape/cal filter is set.
4. `dpf-plugin/TapeMachineParams.hpp` — the choice arrays + param table.

## Verification obligations
1. Build (touch first) + `auval -v aufx DsTM Dusk` PASSES.
2. `MATRIX_MACHINE=ATR102 python3 matrix_probe.py full` and `MATRIX_MACHINE=A800 … full` —
   FR within ~1–2 dB across every config INCLUDING the new modes.
3. The OTHER machine stays byte-identical when you touch shared code (`tape_guard.sh`).
4. Reference-config numbers unchanged after the CAL re-index.
5. Update `report/` + the memory file when done.

## Scope wall
Touch the mode/param/DSP paths needed for the four gaps. Do NOT change the saturation waveshapers,
the aliasing/Local2xStage structure, the gain-link formula, or the tuned per-mode FR of the
EXISTING (already-verified) speeds/tapes — only ADD the new modes and RETUNE the new rows.
