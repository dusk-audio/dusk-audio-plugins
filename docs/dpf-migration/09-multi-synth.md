# 09 — Multi-Synth: JUCE → DPF port + design finalization

**Read `00-OVERVIEW.md` in full first.** This document is the design authority for the
Multi-Synth DPF port. The companion file `09-multi-synth-inventory.md` is the exhaustive
read-only inventory of the existing JUCE implementation (parameters, DSP, JUCE deps,
red flags) — treat it as ground truth about the current code.

## Product vision

One vintage synthesizer plugin that covers the classic ground: 60s/70s American mono
and modular synths, late-70s American poly, early-80s Japanese poly, mid-80s FM
digital, and the silver acid box. Six modes, one instrument. Never released, so there
is **no backwards-compatibility constraint** — fix everything, redesign freely.

**Display name**: "Multi-Synth" (family-consistent with Multi-Q / Multi-Comp).
**Brand**: Dusk Audio. **New IDs** (do not reuse JUCE `MSyn` VST3 code; pick a fresh
4-char code, fresh CLAP id `studio.dusk.multi-synth`, LV2 URI
`https://dusk-audio.github.io/plugins/multi-synth`). JUCE version stays buildable for
A/B validation only.

**Trademark rule (hard)**: no third-party brand/model names anywhere in code, UI,
preset names, or docs. Modes use codenames; docs describe hardware generically
("classic 6-voice Japanese poly", "5-voice American poly with poly-mod", etc.).

## The six modes

| Mode | Codename | Inspiration (docs only, generic) | Voices | Signature |
|---|---|---|---|---|
| 0 | **Cosmos** | early-80s Japanese 6-voice DCO poly | 6 | DCO (saw+pulse+sub), non-self-osc 24dB LPF + HPF, built-in BBD chorus I/II/Both |
| 1 | **Oracle** | late-70s American 5-voice poly | 5 | 2 VCOs, self-oscillating 24dB LPF, poly-mod (FEnv/OscB → OscA freq/PW/filter) |
| 2 | **Mono** | 70s Japanese aggressive mono | 1 | 2 VCO + sub, fat driven OTA LPF, ring mod, hard sync |
| 3 | **Modular** | 70s semi-modular | 2 | 3 VCO, transistor-ladder LPF, FM, S&H, spring reverb |
| 4 | **Prism** | mid-80s 4-op FM digital | 8 | 4 operators, 8 algorithms, per-op ratio/level/ADSR, op feedback |
| 5 | **Acid** | silver 303 bass box | 1 | 1 VCO (saw/square), 18dB diode-ladder scream filter, accent + slide, 16-step pitch pattern sequencer |

Modes 0–3 exist in the JUCE code; port them. Modes 4–5 are new engines.

## Architecture (per playbook, non-negotiable)

```
plugins/multi-synth/
├── core/                     # framework-free C++17, zero JUCE/DPF includes
│   ├── MultiSynthDSP.hpp/cpp # top engine: prepare/reset/processBlock + atomic setters
│   ├── Oscillator.hpp        # polyBLEP oscs, sub, S&H, pink noise (ported)
│   ├── FilterEngine.hpp      # OTA 4-pole ×3 tunings, ladder, NEW AcidFilter (3-pole diode)
│   ├── Envelope.hpp          # ADSR (4 curves), LFO (ported)
│   ├── ModMatrix.hpp         # 8-slot matrix (ported, dead dests wired — see fixes)
│   ├── Voice.hpp             # SynthVoice + VoiceAllocator (ported + real unison)
│   ├── FMEngine.hpp          # NEW: 4-op FM (Prism)
│   ├── AcidEngine.hpp        # NEW: 303 voice + pattern sequencer (Acid)
│   ├── Arpeggiator.hpp       # ported, allocation-free rework
│   ├── Effects.hpp           # drive/chorus/juno-chorus/delay (ported), Freeverb (new, replaces juce::Reverb)
│   └── tests/                # offline render harness (see Validation)
└── dpf-plugin/
    ├── DistrhoPluginInfo.h
    ├── MultiSynthParams.hpp  # param enum + ranges + factory preset table
    ├── MultiSynthPlugin.cpp  # thin shell, MIDI → engine
    ├── MultiSynthAccess.hpp  # weak-symbol bridge (meters, scope, arp step)
    ├── MultiSynthUI.cpp      # ImGui, custom ImDrawList (see UI spec)
    └── CMakeLists.txt
```

MIDI: DPF `DISTRHO_PLUGIN_WANT_MIDI_INPUT 1`; the shell converts `MidiEvent`s to
sample-offset note events consumed by the engine inside processBlock (split-block
rendering at each event offset, like every DPF synth).

Reuse `plugins/shared-dpf/dsp/` (`DuskSmoothed`, `DuskOversampler` HalfbandFIR,
`DuskDenormals`) and the tape-echo core's `SpringReverb` for Modular mode.

## Mandatory fixes (inventory §9 — all confirmed in code)

1. **Oversampling redesign.** Voices render at `hostRate × osFactor` (1/2/4, default 2×),
   engine re-prepares on factor change, decimation via cascaded `HalfbandFIR`
   (shared-dpf), NOT box-average. This fixes the octave-drop bug. Envelope/LFO/arp time
   constants derive from the internal rate consistently. Effects run at host rate.
2. **Pitch bend + master tune always affect oscillator base frequency** (bend range
   param honored). Mod-matrix PitchBend source stays as an extra.
3. **Real unison**: per-unison-voice detuned oscillator copies with constant-power pan
   spread; cap total osc-voices (poly × unison ≤ 16, auto-reduce polyphony).
4. **Cross Mod wired**: osc2 audio-rate → osc1 frequency (Cosmos/Oracle exposure kept).
5. **Dead mod destinations wired**: LFO1Rate, LFO2Rate, EffectsMix, UnisonDetune all
   consumed, or removed from the choice list — wire them (cheap).
6. **Arpeggiator allocation-free**: preallocated fixed arrays, member xorshift PRNG,
   no per-block `buildPattern` unless notes changed, no `std::shuffle`/`chrono` in RT.
7. **No string-keyed or per-sample param lookups** — atomics set by the shell.
8. **Freeverb ported into core** (8 comb + 4 allpass per channel, classic public-domain
   tunings) replacing `juce::Reverb`. SpringReverb = real dispersive one from tape-echo.
9. **Drop AnalogEmulation dependency** (dead include).
10. `rand()` → member PRNG. NaN guards kept.

## New engine specs

### Prism (FMEngine.hpp)
- 4 operators, sine core (phase accumulator + sine LUT or std::sin at first).
- 8 algorithms (classic 4-op set): 1) 4→3→2→1 serial; 2) (4→3)+(2)→1... use the
  standard OPM/4-op algorithm chart: serial, 3-into-1 variants, dual stacks 2×(2op),
  1 modulator 3 carriers, all-parallel (organ). Document each with an ASCII diagram.
- Per op: `ratio` (0.25–16, snap list 0.25/0.5/1/2/…14 + fine ±99c as separate param),
  `level` 0–1, ADSR (reuse core ADSR, Exponential curve default), `velSens` 0–1.
- Op 4 feedback 0–1 (self-phase-mod, classic growl).
- Key scaling: simple level keyfollow per op (−1..+1) — enough for e-piano tine rolloff.
- Global: existing amp env gates the voice; filter section stays in circuit (set cutoff
  open by default in presets) so FM can also be filtered (hybrid bonus).
- Runs inside SynthVoice as an alternative osc section (mode switch), so mod matrix,
  LFO vibrato, unison, FX all apply.
- FM depth scaling: modulation index = level × π × 2 (calibrate so level 1.0 ≈ classic
  bright e-piano bell edge; verify by spectrum in tests).

### Acid (AcidEngine.hpp)
- Mono voice: osc1Wave restricted saw/square (others allowed, don't fight the user).
- **AcidFilter**: 3-pole (18 dB/oct) diode-ladder-flavored lowpass, resonance up to
  screaming near-self-osc, strong env sensitivity; input drive `tanh`. New tuning in
  FilterEngine.hpp, same zero-delay one-pole cascade approach as FourPoleOTA but
  3 stages + diode-style feedback clipping.
- **Envelope behavior**: main env = fast-decay (filter env reused); `acidEnvMod` uses
  existing filterEnvAmt; `accent` boosts env depth + amp + adds resonance kick,
  smoothed (the "wow" circuit).
- **Slide**: 60 ms constant-time exponential glide when a step is marked slide (or
  legato overlapping notes when played live).
- **Pattern sequencer** (the point of the mode): 16 steps, reuses arp clock/rate/gate/
  swing/latch. Per step: on/off (existing `arpStep0-15`), NEW `seqPitch0-15`
  (semitones −24..+24 relative to held note), NEW `seqAccent0-15` (bool), NEW
  `seqSlide0-15` (bool). Player holds one note; the pattern transposes with it.
  In modes 0–4 these extra rows are hidden and the arp behaves classically.
- Global acid params: `acidAccentAmt` 0–1, `acidSlideTime` 10–200 ms.

## Parameter plan

Keep every existing param id/range (inventory §1) except where fixes demand behavior
changes. Add: 34 Prism params (`prismAlgo`, `prismFB`, `op{1-4}{Ratio,Fine,Level,Vel,
KeyScale,A,D,S,R}` — trim Fine/KeyScale if param count hurts, keep the rest), 3 acid
globals + 48 step params (`seqPitch/Accent/Slide 0-15`). DPF param table generated
from one X-macro list in `MultiSynthParams.hpp` so shell + UI + presets share it.
Output params: `outLevelL/R` (meters fallback; real path = weak-symbol bridge).

## Factory presets

Port all 40 (inventory §5) as a **static value table** (not procedural), then add ≥12
new: 4 Prism (glass e-piano, solid FM bass, bright bells, brass stab), 4 Acid
(classic squelch, rubber bass, hoover-adjacent, slow acid), plus flagship pads/leads.
**"Upside Down"** (already present, Modular) must be re-voiced on Oracle-style saw
arp so the famous 80s sci-fi title arpeggio plays out of the box: C2-E2-G2-B2 arp
up-down 1/8 @ ~132 BPM feel, 2 saws slight detune, filter ~2 kHz, slow LFO. Verify
by ear render. Every preset re-rendered at the FIXED oversampling pipeline (they were
authored against the octave-bug — re-voice by ear/measurement, don't trust old values).

## UI spec (the "amazing" part)

Fixed design space **1240 × 780**, uniformly scaled (tape-echo pattern), ImDrawList
custom rendering only (combos allowed). Load bold TTF at 30 × scaleFactor (playbook §4).

**Concept: one chassis, six personalities.** Dark instrument panel with brushed-metal
frame and subtle wood side cheeks. The **mode selector is the hero**: six backlit
rocker buttons top-center; switching modes crossfades the panel's accent color, section
labels, and swaps mode-specific sub-panels:

- Cosmos: cream/warm-white accents, red/orange section markers, chorus I/II buttons
- Oracle: walnut + black, cream text, poly-mod sub-panel
- Mono: black/silver, big filter knob
- Modular: patch-panel look (drawn jacks/cables purely decorative), S&H + spring
- Prism: dark teal membrane aesthetic, algorithm diagram widget (drawn op blocks +
  routing lines that highlight the selected algorithm), 4 op strips
- Acid: silver panel, round colored buttons, pattern row becomes pitch/accent/slide lanes

**Fixed layout regions** (identical bone structure across modes — users keep their
bearings): top bar (logo, mode rockers, preset browser prev/next + combo, save);
left column OSC/MIXER; center FILTER (oversized cutoff knob, drawn filter-curve
display) + ENV ×2 (drawn ADSR shape widgets, draggable handles later — v1 knobs);
right column LFO ×2 + MOD matrix (8-row grid panel, toggleable overlay); bottom strip:
16-step sequencer LEDs (arp steps; in Acid expands to 3 lanes) + FX section (drive/
chorus/delay/reverb mini-panels with enable LEDs); far right: oscilloscope (from
engine ring buffer via access bridge) + stereo VU. Full-width 3-octave clickable
keyboard at the very bottom (sends MIDI notes; steal the Multi-Q keyboard widget).

Knobs: Dusk chrome knob from tape-echo UI, mode-accent value arcs, double-click reset,
shift-drag fine, mouse wheel. Every control gets a tooltip. Meters/scope read via
`MultiSynthAccess.hpp` weak bridge (playbook landmine 2).

## Validation (playbook §Validation, adapted for a synth)

1. **Pitch correctness gate** (the octave-bug regression test): offline render A440
   note per mode per OS factor → FFT peak within ±5 cents of 440. MUST pass 1×/2×/4×.
2. **A/B vs JUCE build at 4× only** (the only correct JUCE setting): render identical
   note sequences (JUCE via `duskverb_render` VST3 hosting, DPF likewise), match
   loudness, compare spectra per mode. Document every intentional deviation
   (real unison, wired pitch bend, new decimator) in commit messages.
3. Aliasing: saw at high pitch, measure images below Nyquist at 2×/4×.
4. FM calibration: Prism e-piano patch spectrum sanity (partials, no aliasing wall).
5. Acid: render pattern with accent/slide on/off, verify accent level delta + slide
   glide time ~spec.
6. pluginval strictness 8 VST3, `--timeout-ms 120000`.
7. LV2: MONOLITHIC (direct access), lv2ls + instantiation.
8. Xvfb UI sweep: drag every control, screenshot, verify readouts.
9. Arp/seq timing: render at 120 BPM 1/8, onset grid within ±1 ms after step 1.

## Process

Branch `multi-synth/dpf-core` (this one). Granular commits, detailed messages, no
co-author trailers, no pushes unless asked. Phases:

- **P1** Core extraction: port modes 0–3 framework-free + all mandatory fixes + Freeverb + tests
- **P2a** FMEngine (standalone + calibration test) ∥ **P2b** AcidEngine (standalone + test)
- **P3** Integration (SynthMode 4/5 into Voice/params) + DPF shell + programs
- **P4** ImGui UI
- **P5** Preset re-voice + full validation sweep

One build/render lock holder at a time; P2a/P2b are file-disjoint and may run parallel.
