# Handoff: Multi-Q → DPF

> Prompt for the executing agent: Read `docs/dpf-migration/00-OVERVIEW.md`
> first. Execute on branch `multi-q/dpf-core`. Requires shared-dpf; strongly
> recommended after the 4k-eq pilot (same domain, bigger scale).

## Source inventory

`plugins/multi-q/` — universal EQ (Digital/British/Tube characters), the
largest EQ (~30+ real parameters; ~160 param mentions in source).
Good news: the interesting DSP is **already framework-free custom code**:
- `AnalogMatchedBiquad.h` — analog-matched biquad (port verbatim)
- `ADAASaturation.h` — antiderivative antialiasing saturation (port verbatim;
  ADAA is the reason tube mode may NOT need oversampling — verify by
  measurement, don't assume)
- Remaining JUCE deps: APVTS, buffers, editor, and possibly
  `juce::dsp::Oversampling`/FFT for the analyzer.

## DSP port plan

1. `plugins/multi-q/core/MultiQDSP.hpp/cpp`.
2. Port `AnalogMatchedBiquad` and `ADAASaturation` as-is (strip any juce
   types at the edges). These already encode the "no cramping" project
   mandate — preserve their math exactly; the A/B test must null closely.
3. Band architecture: N bands × (type, freq, gain, Q, enable) — coefficient
   recompute at block rate on change, per band dirty-check.
4. Spectrum analyzer: needs an FFT. Add **pffft** (single-file, BSD) to
   `shared-dpf/third_party/`. DSP side: lock-free ring buffer of recent output
   samples (write index published release/acquire, per-slot atomic samples),
   reached from the UI via the direct-access bridge. UI side, concrete snapshot
   protocol (the atomic write index alone does NOT make the shared buffer safe
   to read while the audio thread writes it): (1) ACQUIRE-load the write index
   `w0`; (2) copy the most-recent N slots into a private UI buffer (per-slot
   relaxed atomic loads); (3) ACQUIRE-load the write index again as `w1`. If
   `w1 - w0 >= kSize - N` the writer lapped the copied window mid-read — the
   frame is torn, so drop it (reuse the last good frame) or retry once. Only a
   clean frame is windowed + FFT'd + magnitude-smoothed at frame rate; never
   window/FFT the shared ring in place. (Equivalently, double-buffer: the DSP
   writes into one of two snapshot buffers and publishes the ready index, so the
   UI always reads a complete, non-racing frame.) Never FFT on the audio thread.
5. Bypass designation, latency (only if oversampling is actually used).

## UI plan

The big one for this plugin: interactive EQ curve editor.
- Draggable band handles on a log-frequency response plot (ImGui invisible
  buttons per handle + ImDrawList curve; composite response evaluated from
  the same biquad coefficient math on the UI thread).
- Spectrum analyzer behind the curve (filled polyline, dark).
- Band strips below (the JUCE `BandStripComponent` layout is the reference).
- Character selector (Digital/British/Tube) as a stepped control.

## Validation

- [ ] A/B vs JUCE `Multi-Q.vst3`: per-band and composite responses within
      ±0.25 dB to 20 kHz at 44.1/48/96k, all three characters. ADAA
      saturation THD matched within ~10% relative at three drive levels.
- [ ] Cramping check at high frequencies at 44.1k (project mandate).
- [ ] Analyzer sanity: known sine input shows a peak at the right bin/level.
- [ ] Aliasing probe on Tube character at full drive: ≤ −46 dBc, else add
      oversampling around the nonlinearity.
- [ ] Bypass bit-exact; pluginval strictness 8; LV2 instantiation; Xvfb
      sweep including handle-drag on the curve editor (verify a band's
      freq/gain readouts change).
- [ ] Presets ported to dropdown + host programs.
