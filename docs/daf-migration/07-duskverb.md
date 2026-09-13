# Handoff: DuskVerb → DAF

> Prompt for the executing agent: Read `docs/daf-migration/00-OVERVIEW.md`
> first. Execute on branch `duskverb/daf-core`. Requires shared-daf. Do this
> AFTER several smaller ports — largest surface, most to protect.

## Why this one is special

DuskVerb's DSP is **already framework-free** (only 2 `juce::AudioBuffer`
references in `src/dsp/`): DattorroTank, AccurateHall, VelvetTail,
ShimmerEngine, DenseHall, the FDN engines — all custom C++. The port is
therefore mostly *packaging*, but the plugin carries a large calibrated
preset fleet whose sound is protected by an extensive measurement
infrastructure. **The prime directive: the DAF build must be bit-identical
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
3. Presets: port the full factory bank to DAF programs + dropdown. Preset
   values must be byte-for-byte the shipped ones (beware the known trap:
   `--preset` re-applies a stale hand-transcribed mirror; factory tables in
   the plugin source are the truth — see project memory on `--preset` vs
   `--program`).
4. UI: reproduce the JUCE editor's controls in ImGui (knobs + engine/preset
   selectors; check the editor source for meters/displays).
5. Bypass designation; latency (engines report none today — verify).

## Validation (the whole point)

- [ ] **Per-preset A/B null**: render JUCE VST3 vs DAF VST3 for EVERY
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
  memory). Build the DAF target with the same optimization flags as the
  JUCE build; if a null test fails mysteriously, suspect codegen/flags and
  TU layout before suspecting the port.
- The user has deep ear-history with this plugin. Every deviation, however
  measured, gets flagged to the user with renders, never silently accepted.

## Editor behaviour versus the JUCE editor (validated 2026-09-12)

Measured, not assumed. The offline program null (20 presets, 6 stems, 3
conditions) says nothing about a knob; these were checked separately:

- **Host units.** Nonlinear parameters expose the original JUCE normalized
  coordinate (0..1); linear parameters retain their plain domain. Custom host
  text callbacks display and parse physical units. The shared parameter table
  drives the UI, host mapping and DSP conversion, preserving the JUCE taper.
- **DSP value per knob position.** For every parameter and five positions
  along the JUCE travel, the plain value reaches the DSP as the float JUCE's
  APVTS produces (core test `testHostAndKnobDomains`), and rendering both
  VST3s with the same plain value is bit-identical (knob sweep, private tools
  repo).
- **Read-outs.** `daf-plugin/DuskVerbFormat.hpp` transcribes the JUCE
  `formatValue` rules and the Gated / Shimmer / Spring relabels and value
  overrides; the plugin-layer test holds it to them string for string.
- **Editor wiring.** A scratch variant of `DafClapUiDragTest` dragged every
  knob up and down and clicked every switch: each emits only its own
  parameter, up increases, down decreases.

Deliberate fleet differences from the JUCE editor (DuskPanel, shared by every
DAF plugin; change there, not here):

| Gesture | JUCE DuskVerb editor | DuskVerb 2 |
|---|---|---|
| Full-travel drag | 250 px (stock `juce::Slider`) | 200 px |
| Fine drag | none | Shift, 6x finer |
| Double-click | nothing | type a value |
| Reset to default | none | Alt-click / Ctrl-click, or the right-click menu |
| Mouse wheel | JUCE default | 2 % of travel per notch, Shift 0.4 % |
| Preset name after an edit | kept | kept, with a trailing `*` |

## Release-candidate follow-up

The approved native UI is implemented, including full-height LED meters and
the revised Input/Filter layout. The release registry and `manuals/duskverb-2.md`
now include DuskVerb 2. See `plugins/DuskVerb/daf-plugin/RELEASE_READINESS.md`
for current evidence and remaining release gates. The older checklist above is
the original migration plan, not a statement of completed release qualification.
