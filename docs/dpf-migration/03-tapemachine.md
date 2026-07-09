# Handoff: TapeMachine → DPF

> Prompt for the executing agent: Read `docs/dpf-migration/00-OVERVIEW.md`
> first, then study `plugins/tape-echo/` (closest sibling: it already models
> tape saturation, wow/flutter, hiss, oversampled shaping). Execute on branch
> `tapemachine/dpf-core`. Requires shared-dpf + ideally the 4k-eq pilot.

## Source inventory

`plugins/TapeMachine/` — analog tape emulation. JUCE deps found:
- 23× `juce::dsp::IIR` → shared RBJ `Biquad`
- 10× `juce::dsp::Oversampling` → shared `HalfbandFIR` 2x/4x chains
- 4–6× `juce::dsp::StateVariableTPTFilter` → write a shared TPT SVF
  (Cytomic/Zavalishin topology, ~30 lines; add to `shared-dpf/dsp/`,
  verify magnitude response against the JUCE one in the A/B step)
- 4× `juce::SmoothedValue` → shared `SmoothedValue`
- ~20 parameters; read the exact layout from the processor source.

## DSP port plan

1. `plugins/TapeMachine/core/TapeMachineDSP.hpp/cpp`, framework-free.
2. Tape saturation stage: reuse the oversampled-shaper pattern from
   tape-echo (4x for full-band nonlinearities; measure aliasing to justify
   anything left at base rate).
3. Wow/flutter: tape-echo's dual-LFO + filtered-noise modulation with
   Hermite fractional reads is directly reusable if TapeMachine modulates a
   delay; if it modulates filters/gain instead, port its existing math.
4. Hiss/noise: tape-echo's Tape Age implementation (hiss written into the
   signal path, separate RNG stream, exact-zero at knob 0) is the pattern.
5. Head bump / tape EQ: biquads at block-rate recompute.
6. Latency from oversampling reported via DPF latency support; bypass
   designation param, bit-exact.

## UI plan

Reel-to-reel aesthetic: VU meters (shared widget — TapeMachine likely wants
two, L/R), tape-speed selector (stepped knob like tape-echo's mode selector),
saturation/bias/noise knobs. Keep the design-space + uniform-scale approach.

## Validation

- [ ] A/B vs installed JUCE `TapeMachine.vst3` through `duskverb_render`:
      matched params, pink noise + sine ladder + program material.
      Frequency response ±0.25 dB; THD character within ~10% relative at
      three drive levels; wow/flutter modulation rates and depths matched
      (instantaneous-frequency analysis).
- [ ] Aliasing: 10.1/14.1 kHz tones at max drive ≤ −46 dBc at 48k.
- [ ] Noise: knob-zero bit-clean (if JUCE version has a noise control,
      match its off state exactly); floor at max within ±3 dB of the JUCE
      build unless deliberately revoiced (document deviations).
- [ ] Bypass bit-exact; pluginval strictness 8; LV2 instantiation; Xvfb UI
      sweep with screenshots; presets ported.

## Notes

- TapeMachine has shipped releases (v1.x tags) — treat the JUCE build as the
  reference sound. Deviations are bugs unless the user approves them by ear.
- Add anything generic you build (TPT SVF, dual VU layout) to `shared-dpf`.
