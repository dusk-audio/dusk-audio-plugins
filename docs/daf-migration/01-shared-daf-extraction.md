# Handoff: Extract `shared-daf` Common Library

> Prompt for the executing agent: Read `docs/daf-migration/00-OVERVIEW.md`
> first. Then execute this task on a new branch `shared-daf/extraction`.

## Goal

Pull the reusable pieces out of `plugins/tape-echo/` into
`plugins/shared-daf/` so every subsequent port includes them instead of
copying. Tape Echo itself must switch to consuming the shared versions and
remain **bit-identical** (prove it: render its validation scenarios before
and after, `max|diff| == 0`).

## Deliverables

```
plugins/shared-daf/
├── dsp/
│   ├── DuskSmoothed.hpp      # SmoothedValue (one-pole)
│   ├── DuskFilters.hpp       # OnePoleLP/HP, DCBlocker, RBJ biquad —
│   │                         #   extend ShelfFilter into a general Biquad
│   │                         #   with lowShelf/highShelf/peak/lowpass/
│   │                         #   highpass coefficient functions (RBJ)
│   ├── DuskOversampler.hpp   # HalfbandFIR<L,NSide> + A47/B15 taps + the 2x/4x
│   │                         #   streaming up-shape-down oversampler (one file)
│   └── DuskDenormals.hpp     # ScopedFlushDenormals (SSE + ARM64 FPCR)
├── ui/
│   ├── DuskImGuiWidgets.hpp  # knob (chrome, scalloped skirt, drag/wheel/
│   │                         #   double-click-reset), led, toggle, vu meter,
│   │                         #   knobLabel with triangle marker, text helper
│   └── DuskImGuiFont.hpp     # crisp bold font loader with fallback list
├── DuskAccessBridge.hpp      # documented weak-symbol pattern (template/macro)
└── cmake/DuskDafPlugin.cmake # wrapper around daf_add_plugin encoding the
                              #   MONOLITHIC + include-path conventions
```

## Rules

- Header-only where practical (matches repo convention for small utilities).
- Namespace `duskdaf` (UI) / keep `duskaudio` (DSP) to avoid churn in
  tape-echo includes.
- The UI widget extraction must parameterize what tape-echo hardcodes:
  colors via a small palette struct, `editParameter/setParameterValue`
  callbacks via a host-interface argument (the DAF `UI*`), design-space
  scale/origin passed in.
- The knob drag state (`dragValue`) lives per-UI, not per-widget — preserve
  the existing single-active-knob behavior.

## Acceptance criteria

- [ ] Tape Echo builds against `plugins/shared-daf/` with its local copies
      deleted, all four formats.
- [ ] Bit-identity: tape-echo offline validation renders (recreate
      `render_validation.cpp` from the branch history if missing) identical
      before/after extraction.
- [ ] pluginval strictness 8 SUCCESS on the rebuilt Tape Echo VST3.
- [ ] Xvfb UI sweep: knobs, mode selector, power, sync, preset dropdown all
      still function (screenshot evidence).
- [ ] No JUCE includes anywhere under `plugins/shared-daf/`.
