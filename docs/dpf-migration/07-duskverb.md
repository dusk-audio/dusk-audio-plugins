# Handoff: DuskVerb → DPF

> Prompt for the executing agent: Read `docs/dpf-migration/00-OVERVIEW.md`
> first. Execute on branch `duskverb/dpf-core`. Requires shared-dpf. Do this
> AFTER several smaller ports — largest surface, most to protect.

## Why this one is special

DuskVerb's DSP is **already framework-free** (only 2 `juce::AudioBuffer`
references in `src/dsp/`): DattorroTank, AccurateHall, VelvetTail,
ShimmerEngine, DenseHall, the FDN engines — all custom C++. The port is
therefore mostly *packaging*, but the plugin carries a large calibrated
preset fleet whose sound is protected by an extensive measurement
infrastructure. **The prime directive: the DPF build must be bit-identical
(or measurably indistinguishable) to the JUCE build per preset.**

## Protected assets

- `plugins/DuskVerb/src/dsp/` — engines. Port verbatim; edits forbidden, with
  ONE mechanical exception: the two `juce::AudioBuffer` call sites that form the
  `DuskVerbDSP` I/O boundary (see Port plan step 1) are de-JUCE'd to raw
  `float* const*` pointers. Nothing else in `src/dsp/` — no DSP logic, no
  coefficients — may change.
- Factory presets + per-preset octave calibration tables — the product of a
  long tuning campaign. Any drift is a regression.
- Validation tooling: `tests/duskverb_render/` (hosted renderer),
  `full_check` gates, and `~/projects/dusk-audio-tools/` (private repo,
  symlinked at `plugins/DuskVerb/tests/reference_comparison/`) — anchors and
  calibration scripts. Use them; do not reinvent.

## Port plan

1. Core wrapper: the engines already expose processor-style APIs; write
   `DuskVerbDSP` facade (framework-free) that owns engine instances, preset
   application, and parameter smoothing — replacing the JUCE
   `PluginProcessor` glue only. Strip the 2 `juce::AudioBuffer` uses in dsp/
   behind raw pointers (mechanical).
2. Parameters: large set (~30+; read the APVTS layout). Exact names/ranges/
   defaults — the tuning tables reference them.
3. Presets: port the full factory bank to DPF programs + dropdown. Preset
   values must be byte-for-byte the shipped ones (beware the known trap:
   `--preset` re-applies a stale hand-transcribed mirror; factory tables in
   the plugin source are the truth — see project memory on `--preset` vs
   `--program`).
4. UI: reproduce the JUCE editor's controls in ImGui (knobs + engine/preset
   selectors; check the editor source for meters/displays).
5. Bypass designation; latency (engines report none today — verify).

## Validation (the whole point)

- [ ] **Per-preset A/B null**: render JUCE VST3 vs DPF VST3 for EVERY
      factory preset — impulse, noiseburst, snare, sine1k, long-sine stems
      (the render tool already generates these). Since the engine code is
      identical, target: bit-identical or ≤ −120 dB residual. Any preset
      that fails gets diagnosed, not waved through ("diagnose, don't count
      gates" — project rule).
- [ ] `full_check` gate suite per preset: scores must equal the JUCE
      baseline exactly. Run the fleet audit
      (`fleet_audit.py --verify-tables --verify-calibration` in the tools
      repo) after the port.
- [ ] Renders 100% wet where the methodology requires it (project rule).
- [ ] pluginval strictness 8; LV2 instantiation; Xvfb UI sweep; presets in
      host program menus.

## Warnings

- Engines are sensitive to compile flags: unity builds and added hot-loop
  code have caused FP codegen drift through recursive loops before (project
  memory). Build the DPF target with the same optimization flags as the
  JUCE build; if a null test fails mysteriously, suspect codegen/flags and
  TU layout before suspecting the port.
- The user has deep ear-history with this plugin. Every deviation, however
  measured, gets flagged to the user with renders, never silently accepted.
