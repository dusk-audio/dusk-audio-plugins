# Sunset Circuits QA Checklist (Phase 5)

Product name: Sunset Circuits (internal names MultiSynthDSP / msynth stay stable).

This document has two parts:

1. **Automated results** recorded during Phase 5 on the build machine. These are
   the measurable checks that do not need a human, run and signed off here.
2. **Marc's desktop checklist**, the human pass that automation cannot replace:
   real-DAW loading, playing, automating, session round-trips, editor lifecycle,
   and the final ear pass across all 54 presets and six modes.

The standing rule holds: Marc's ear pass is the final sign-off. Everything
measurable is verified first; the renders and the installed build are handed
over, never claims.

---

## Part 1 - Automated results (recorded)

Machine: Intel Core i7-8809G (4 cores / 8 threads, 3.1 GHz base), 31 GiB RAM,
Linux x86_64. All figures are single-machine and single-core unless noted.

The figures below were recorded by hand, but everything in this part now also
runs in CI. The `test-linux` job in `.github/workflows/dpf-release.yml` builds
the VST3 + LV2, runs the whole of `core/tests/run_all.sh` (every pass/fail gate,
the fm and acid sub-suites, `preset_audit` over all 54 factory presets, the
`lv2_smoke` host check and the `cpu_bench` sanity bar) and validates the VST3
with `pluginval --skip-gui-tests --strictness-level 8`. It runs on tag pushes
and on `workflow_dispatch`, and the `release` job needs it, so a red gate blocks
the publish. The Windows job additionally runs pluginval at strictness 5.

Note that two different things sweep all 54 presets: `preset_audit` (a pass/fail
gate, always run) and `fx_alias_gate` section 3 (report-only, and the slowest
item in the suite). Only the former runs in CI; the latter stays behind
`FX_ALIAS_FULL=1`.

Not covered by any gate: see the coverage-gap list at the top of
`core/tests/run_all.sh`.

### CPU worst-case profile

Tool: `core/tests/cpu_bench.cpp` (offline; times only the `processBlock` loop
after a 0.5 s warm-up, so prepare, note-on, and steady-state ramp-up are
excluded). 48 kHz, 512-frame blocks, 10 s of audio per scenario. The figure is
the percentage of one CPU core; below 100 percent means faster than real time.
Two consecutive runs shown to confirm stability.

| Scenario | Oversampling | Run 1 | Run 2 |
|---|---|---|---|
| (a) 8-voice Prism FM + drive + chorus + delay + reverb | 4x | 49.2% | 49.5% |
| (b) 6-voice Cosmos, 8x unison (poly auto-reduced), dual chorus + all FX | 2x | 21.6% | 21.8% |
| (c) 4-voice Oracle pad + reverb | 2x | 14.8% | 14.9% |
| (d) Acid sequencer + drive + delay + reverb | 2x | 2.6% | 2.7% |

Block budget at 512/48 kHz is 10666.7 us; worst-case per-block time for (a) was
about 6.9 ms, comfortably inside the budget. The manual Performance Notes now
carry these numbers (rounded) in place of the previous TBD.

Reproduce: `core/tests/build/cpu_bench all 10`.

### Host-integration smoke (LV2)

Installed hosts on this box: `ardour9`, `lv2info`, `lv2ls`, `amidi`, `aconnect`,
`pluginval`. No `jalv` or `carla` are installed, so the DSP was hosted through a
small `lilv`-based host (tracked at
`plugins/sunset-circuits/dpf-plugin/tools/lv2_smoke.c`, the same library `jalv` uses).

Results:

- Bundle loads; plugin resolves by URI `https://dusk-audio.github.io/plugins/sunset-circuits`.
- `instantiate` + `activate` + `run` all succeed (the DPF options feature and a
  URID map were provided, as any real host does).
- 228 ports enumerated; oversampling control input and the `lv2_latency`
  reporting output port located and connected.
- Audio output is finite at every oversampling factor with no notes held.
- MIDI-to-audio path: a note-on (C4, velocity 100) written into the input atom
  port produces audible output (peak about -33 dBFS over ~213 ms). This confirms
  MIDI reaches the engine through the real LV2 wrapper, not just the offline core.
- MIDI program change (0xC0): with the host's oversampling port parked at 4x
  (reported latency 14), program 0 pulls the reported latency to the preset
  baseline's 2x value (12), which is a parameter the preset owns observed through
  a real host port; program 127 (past the 54 factory presets) leaves it at 14.

The smoke tool is now pass/fail, not print-only: every check above feeds a
failure counter and the process exits 6 if any of them fails, so it can be
wired into a script without parsing its output.

Ardour is installed but a headless Ardour session was not scripted. Ardour
session automation via Lua is not straightforward and belongs to the desktop
checklist below; the lilv host covers the goal of catching gross
host-integration breakage.

### Reported latency per oversampling factor

Observable: the LV2 `lv2_latency` output port, read at runtime from the lilv
host above. Matches the design (halfband decimator group delay) and the manual:

| Oversampling | Reported latency (host samples) |
|---|---|
| 1x | 0 |
| 2x | 12 |
| 4x | 14 |

These values are rate-independent (they are filter group delays in samples).

Bug found and fixed during this check: the plugin constructor initialised the
oversampling parameter to its 2x default but never called `updateLatency()`, so
`getLatency()` returned 0 until the first `activate()`. A host that reads
reported latency at scan or load time (before activation) would set up plugin
delay compensation for 0 samples and be misaligned by 12 until activation. Fixed
by calling `updateLatency()` at the end of the constructor
(`MultiSynthPlugin.cpp`). pluginval prints "Reported latency: 0" in its
pre-`prepareToPlay` info dump; that line is the JUCE host's cached default before
activation and does not reflect the plugin's live report.

### State round-trip (222 parameters)

`pluginval --skip-gui-tests --strictness-level 8` on the VST3 is green
(`SUCCESS`). At strictness 8 this exercises, and passes:

- Plugin programs: 54 programs, all names checked, program changes applied.
- Plugin state: save/restore of the full parameter set.
- Plugin state restoration: parameters are randomised, state is restored, and
  values are compared to confirm the round-trip.
- Automatable Parameters, Parameters, Parameter thread safety, Fuzz parameters.

This is the authoritative 222-parameter save/restore proxy at the DPF/VST3 level
per the production plan, so a separate host-less round-trip test was not added.

### Core gate suite

`core/tests/run_all.sh` is green. The pass/fail gates, in the order the script
runs them: pitch, env, reverb, arp, lfo_sync, acid, stuck, sustain, polyat,
steal, zipper, user_preset, then the FM sub-suite, the acid sub-suite, the
54/54 preset audit (non-silent, peak <= -1 dBFS, no clip, finite), lv2_smoke
and the cpu_bench sanity bar.

What the gates added after the first pass cover:

| Gate | Covers |
|---|---|
| `lfo_sync_gate` | LFO host phase-lock: locked phase follows song position (grid/repeat/acquire/rate/loop cases); free-run before the transport starts is covered by the acquire case's pre-play prefix. |
| `sustain_gate` | CC64: hold and release across modes, both latch-off / pedal-up orderings, on a real factory program. |
| `polyat_gate` | 0xA0 poly key pressure reaches the right voice only, and is inert while the arpeggiator runs. |
| `steal_gate` | Voice lifecycle: poly-shrink retirement fade, the per-voice headroom trim on the same edge, and the mode-switch crossfade landing sample-accurate at every buffer size. |
| `zipper_gate` | Parameter smoothing: no click or step on a continuous parameter move, and a preset load still snaps. |
| `stuck_gate` | No hung note across panic, transport changes and arp clock switches. |

alias_gate and fx_alias_gate are both report-only. fx_alias_gate covers the
effects chain, which runs at host rate downstream of the voice decimation and
so is out of reach of the oversampling alias_gate measures; by default it runs
sections 0-2 only, and `FX_ALIAS_FULL=1 ./run_all.sh` adds its 54-preset sweep.

### Demo pack (for the ear pass)

Rendered with `core/tests/presets/preset_render` into the handoff folder. All
non-silent, finite, no clipping.

Six flagships:

| # | Preset | Mode | Performance |
|---|---|---|---|
| 15 | Upside Down | Oracle | hold C2/E2/G2/B2, 8 bars @ 132 BPM (latched up-down arp) |
| 40 | Glass Keys | Prism | Cmaj7 chord + phrase |
| 42 | Crystal Bells | Prism | Cmaj7 chord + phrase |
| 44 | Silver Squelch | Acid | 4-bar pattern sequence |
| 48 | Aurora Drift | Cosmos | Cmaj7 chord + phrase |
| 53 | Neon Sequence | Acid | 4-bar pattern sequence |

Mode init presets (one per init preset; Prism and Acid have no factory "Init"
preset, but both modes are represented above by their flagships):

| # | Preset | Mode |
|---|---|---|
| 36 | Init Cosmos | Cosmos |
| 37 | Init Oracle | Oracle |
| 38 | Init Mono | Mono |
| 39 | Init Modular | Modular |

Reproduce (flagship example):
`preset_render 15 upside_down.wav perf=hold hold=36,40,43,47 tempo=132 bars=8`;
the rest use auto performance selection at 120 BPM.

---

## Part 2 - Marc's desktop checklist (human pass)

Do this on the real desktop with the installed build in each DAW you support.
The headless XEmbed pluginval editor crash is a known host-side issue
(00-OVERVIEW.md landmine 9); real-session validation is the substitute for
editor lifecycle testing.

### Load and formats

- [ ] VST3 loads in the DAW and appears as an instrument.
- [ ] LV2 loads in the DAW and appears as an instrument.
- [ ] CLAP loads in the DAW and appears as an instrument.
- [ ] The nameplate reads "SUNSET CIRCUITS"; brand is Dusk Audio.

### Play

- [ ] Notes from a MIDI controller sound; the on-screen keyboard lights up.
- [ ] All six modes make sound: Cosmos, Oracle, Mono, Modular, Prism, Acid.
- [ ] No stuck notes when switching modes or presets while holding a chord.

### Automate

- [ ] Automate cutoff, resonance, and a mod-matrix amount from the DAW; the
      changes are smooth with no zipper noise.
- [ ] Automate oversampling; confirm the DAW re-reads reported latency and
      plugin delay compensation stays aligned (other tracks stay in time).

### Programs and preset-name sync

- [ ] Host program change steps through the 54 factory presets.
- [ ] The preset name shown in the plugin UI matches the host's program name.
- [ ] Loading a program updates all panels (mode, oscillators, FX) correctly.

### Session round-trip

- [ ] Save the session, close, reopen: the plugin restores its exact state
      (parameters, current program, oversampling).
- [ ] User presets saved from the SAVE button survive a session reload.

### Host sync (phase-lock)

- [ ] Arp and Acid sequencer lock to the host grid in 4/4.
- [ ] Arp and Acid sequencer lock to the host grid in 6/8.
- [ ] Tempo-synced delay tracks host tempo changes.
- [ ] Start/stop and loop points do not strand notes or drift the step clock.

### Preset browsing under load

- [ ] Browse presets while holding a chord; no stuck notes, no crashes.

### Editor lifecycle

- [ ] Open and close the editor 10 times; no crash, no leak, no black window.
- [ ] Resize the editor across its range 10 times; layout scales cleanly.
- [ ] Open two instances with editors at once; both render and respond.

### Ear pass (final sign-off)

- [ ] All 54 presets auditioned; each is musical, on-brand, and free of clicks,
      clipping, or AUDIBLE aliasing at 2x. (Zero aliasing at 2x is not physical;
      the bar is inaudibility in normal use. Engineering guide: alias_gate's
      report shows worst images around -47 dBc at 2x — anything that stands out
      by ear on a preset is a fail regardless of the number.)
- [ ] Spot-check exposed leads and bright FM at 4x for clean high end.
- [ ] The six flagship demos in the demo pack sound as intended.
