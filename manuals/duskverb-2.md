---
slug: duskverb-2
title: DuskVerb 2
version: 1.0.0
last_updated: 2026-11-08
tagline: Algorithmic reverb with 24 factory sounds
---

# DuskVerb 2

## Overview

DuskVerb 2 combines the original DuskVerb reverb engines, its own nonlinear
engine and updated voicing with a redesigned interface. Its 24 factory presets
provide starting points for plates, rooms, halls, springs and nonlinear bursts.
Engine selection changes the reverb topology;
some controls change labels or availability to match the selected engine.

![DuskVerb 2 on the Vocal Plate preset: graphite faceplate, amber engine and decay display, output history, and controls grouped in signal order](screenshots/duskverb-2/01-main-ui.png)

The amber display shows the engine, decay setting and output history. The large
Decay knob sits beside it; Input, Early Reflections, Modulation, Damping, Macro,
Filter and Output controls fill the rows below.

## Quick Start

1. Select a factory preset from the top preset menu. The arrows move through presets.
2. Adjust **Decay** and **Size** to fit the source and arrangement.
3. Set **Mix** for an insert, or enable **Bus** for a fully wet return. Turn on
   **Mix Lock** to keep Mix and Bus where they are while you audition presets.
4. Use **Pre-delay** to separate the source from the reverb. Choose a rhythmic
   division in **Sync** to follow the host tempo.
5. Shape the tail with the **Damping** multipliers and crossover controls.
   Use **Tone** for a broader adjustment, then set **Trim**.
6. Click **Save** to store your sound as a user preset.

## Controls

| Section | Purpose |
|---|---|
| Input | Pre-delay, tempo sync and saturation. |
| Filter | Low Cut, High Cut, Mono Below and Mono Depth. |
| Decay / Size | Tail length and space size. |
| Output | Dry / Wet, Width, Trim and fully wet Bus mode. |
| Early Reflections | Level, size and diffusion. |
| Damping | Three decay multipliers and two crossover frequencies. |
| Modulation | Depth and rate; labels follow the engine. |
| Macro | Tone, Character and Duck. |

Sync retains the free pre-delay setting, and Bus retains the Dry / Wet setting.

**Freeze** holds the reverb on supported engines; it is hidden for Reverse
and Nonlinear. The gated engine also exposes its **Gate** switch. Controls
unused by the selected engine are dimmed.
**Tonal Correction** appears only where it changes the sound: the Vocal Plate,
Ambience and Tiled Room sounds on the Hall and Tiled Room engines (and on the
older hall engines a restored session may still use).

The display at the top left shows the engine (click its name to choose another),
the decay time (click it to type a value), the **Freeze**, **Sync** and **Bus**
indicators, and **Output History**. Output History shows recent output level; it
is not a measured RT60 graph, and the decay readout is the parameter setting, not
a guarantee of a measured acoustic decay time. The side meters show input and
output levels.

Freeze can sustain a tail indefinitely on supported engines. Choose the desired
end of an offline bounce explicitly when using Freeze. CLAP and VST3 report an
infinite frozen tail; AU uses the framework's one-hour fallback because its tail
property has no infinite sentinel. Unfrozen tail estimates include long decay
and damping settings.

## Editing Values

- Drag a knob to adjust it; hold Shift for finer movement.
- Use the mouse wheel to step a value.
- Click a numeric readout or double-click a knob to type a value. Enter applies
  it; Escape cancels. Invalid input stays available for correction.
- Right-click a knob for its reset and value-entry menu.
- Ctrl-click or Alt-click resets a knob on Windows/Linux. On macOS, use
  Alt-click or Cmd-click.
- Drag the editor corner to resize. The minimum size is 1050 × 700.
- Click **?** for the in-plugin control reference. The amber display also shows
  this instance's approximate CPU load.

## Presets and Comparison

**INIT** restores defaults. Factory recall applies the factory sound while
preserving the settings intentionally excluded by the original preset format.
An asterisk beside the preset name indicates an edited sound. **Mix Lock**,
below the Bus switch, keeps Dry / Wet and Bus unchanged on preset recall and
INIT; A/B still restores the complete sound.

**A** and **B** switch between complete comparison snapshots. **COPY** copies
the active sound into the other slot without switching. Save sounds you want
to keep as user presets; comparison slots are a working audition tool.

The Save dialog requires explicit replacement confirmation for an existing
name. Names that map to the same filename, such as `Alpha/Beta` and
`Alpha-Beta`, receive distinct filenames. Right-click a user preset to delete
it. The preset menu also offers import of original DuskVerb preset files.
Malformed imports are rejected without partially applying their values.

User presets are stored in the `DuskAudio/DuskVerb2/presets` subdirectory
of the following configuration folder:

- Linux: `$XDG_CONFIG_HOME`, or `~/.config` when XDG_CONFIG_HOME is unset.
- macOS: `~/.config`.
- Windows: `%APPDATA%`, with LOCALAPPDATA as fallback.

Safe publication of new presets requires a filesystem with hard-link support.
A save failure is reported rather than replacing an unrelated preset.

## Compatibility

DuskVerb 2 has its own plugin identity and can coexist with the original
DuskVerb. Importing an original preset does not convert a DAW project or
replace an original plugin instance. Keep the original plugin available for
projects that use it.

The 1.0 Windows VST3 identity matches Linux and macOS. Windows projects saved
with an earlier DuskVerb 2 preview may need the preview instance replaced:
export its user preset first, insert 1.0, import the preset, and check automation
and routing. Keep the preview available until those projects are migrated.
Original DuskVerb has a separate identity and is unaffected.

The plugin supports mono, mono-to-stereo and stereo operation where the host
exposes those layouts. Select the appropriate host routing before playback.
