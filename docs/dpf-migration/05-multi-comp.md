# Handoff: Multi-Comp → DPF

> Prompt for the executing agent: Read `docs/dpf-migration/00-OVERVIEW.md`
> first. Execute on branch `multi-comp/dpf-core`. Requires shared-dpf.

## Source inventory

`plugins/multi-comp/` — 8-mode compressor + multiband. Biggest UI surface in
the fleet (~40+ real params; mode-dependent panels). JUCE deps found:
- 22× `juce::dsp::IIR` (sidechain filters, crossovers) → shared Biquad;
  crossovers likely Linkwitz-Riley — implement LR4 as cascaded Butterworth
  biquads in `shared-dpf` and verify flat-sum reconstruction.
- 6× `juce::dsp::Oversampling` → shared halfbands.
- 5× `juce::dsp::AudioBlock` → raw pointer loops.
- `HardwareEmulation/` dir — check contents; emulation math is likely plain
  C++ already (port verbatim).
- Envelope followers / gain computers: custom code, port verbatim.

## Known quirks (from project memory — verify, then preserve or fix openly)

- The JUCE version's oversampling parameter accepts only '2x'/'4x' strings
  and reports ~59 samples latency regardless of factor. Decide consciously:
  reproduce exactly (compat) or fix and document. Recommend fix + document,
  since DPF version is a new plugin identity anyway.
- v1.3.1 fixed bypass-related DAW instability (repo commit `7ff0ad9`) — the
  DPF bypass must use the designation-param + crossfade pattern and be
  regression-tested hard (toggle under processing, verify no dropouts,
  bit-exact when settled).

## DSP port plan

1. `plugins/multi-comp/core/MultiCompDSP.hpp/cpp`.
2. Per mode (8 compressor characters): port each gain-computer/envelope
   verbatim; they are the product. The A/B test must show matched
   attack/release envelopes and static curves per mode.
3. Multiband: LR4 crossovers (validate: bands summed with no compression =
   input within float noise), per-band compressors, per-band metering
   atomics (GR meters are core UX).
4. Sidechain HP/filters via shared Biquad. External sidechain: DPF supports
   extra input buses — check `DISTRHO_PLUGIN_NUM_INPUTS` + port groups; if
   the JUCE version has ext-SC, wire it, else skip.
5. Gain-reduction metering: atomic per band + master, direct-access bridge
   to UI (CLAP output-param landmine applies — see overview).

## UI plan

Mode-dependent panel layout like the JUCE `ModernCompressorPanels`:
- mode selector (stepped), threshold/ratio/attack/release/makeup knobs,
- GR meters (needle or LED-ladder — shared widget),
- multiband view with crossover handles + per-band strips when in MB mode.
Largest ImGui build in the fleet; budget accordingly.

## Validation

- [ ] A/B vs JUCE `Multi-Comp.vst3`: static compression curves (sine-level
      ladder → in/out level per mode) matched within 0.5 dB; attack/release
      envelope shapes on step signals matched per mode; crossover sum flat.
- [ ] Bypass torture: toggle every 100 ms under program material, no clicks
      (max sample delta gate), bit-exact when settled.
- [ ] GR meter matches measured gain reduction within 0.5 dB.
- [ ] pluginval strictness 8; LV2 instantiation; Xvfb sweep across ALL modes
      (each mode's panel renders, controls respond); presets ported.
