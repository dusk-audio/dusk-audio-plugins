# Handoff: Chord Analyzer → DAF

> Prompt for the executing agent: Read `docs/daf-migration/00-OVERVIEW.md`
> first. Execute on branch `chord-analyzer/daf-core`. Requires shared-daf.
> This is the MIDI-path pathfinder — its lessons feed any future MIDI work.

## Source inventory

`plugins/chord-analyzer/` — MIDI chord detection + music theory display.
Three JUCE targets exist (main, MIDI variant, headless — see its
CMakeLists, `ChAn`/`ChMi` codes). Minimal audio DSP (~14 param mentions,
2 `juce::AudioBuffer` refs); the value is the detection logic + UI.

## Port plan

1. Core `ChordAnalyzerCore` (framework-free): note-on/off state machine +
   chord identification + theory naming. Input: `(status, data1, data2)`
   events; output: current chord struct (root, quality, extensions,
   inversion, note set). The existing detection code is almost certainly
   plain C++ — port verbatim, keep its test coverage if any exists in the
   repo tests.
2. DAF MIDI plumbing:
   - `DAF_PLUGIN_WANT_MIDI_INPUT 1` (+ OUTPUT if the JUCE MIDI variant
     passes/transforms MIDI through — check what `ChMi` does and mirror it,
     possibly as a second DAF target like the JUCE dual-target setup).
   - Events arrive in `run(inputs, outputs, frames, midiEvents, midiEventCount)`
     — the DAF run signature changes with MIDI enabled.
   - Audio buses: pass audio through untouched if the JUCE version does.
3. UI: the showcase — big chord name, note letters, staff or keyboard
   visualization (ImDrawList: piano keys = rects, highlight held notes).
   Chord/state flows DSP→UI via the direct-access bridge (a small struct of
   atomics: packed note bitmask + chord id), not output parameters.
4. Headless variant: DAF target without UI (`DAF_PLUGIN_HAS_UI 0`
   variant) if the JUCE headless target has users.

## Validation

- [ ] Unit tests on the core: a table of MIDI note sets → expected chord
      names (port/extend any existing tests; add inversions, slash chords,
      extensions — whatever the current engine claims to support, from its
      source).
- [ ] MIDI integration: drive the JACK standalone with a scripted MIDI
      source (`aseqdump`/`aplaymidi` through PipeWire, or a tiny ALSA-seq
      sender) and screenshot the UI showing the right chord.
- [ ] pluginval strictness 8 (it exercises MIDI buses); LV2 instantiation
      (atom MIDI port); Xvfb screenshot evidence.
- [ ] Audio passthrough (if applicable) bit-exact.

## Notes

- No aliasing/oversampling concerns; the risk is entirely in MIDI event
  plumbing per format (VST3 note expression quirks, LV2 atom sequences).
  pluginval + a real DAW check matter more than DSP metrics here.
