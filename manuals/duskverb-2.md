---
slug: duskverb-2
title: DuskVerb 2
version: 0.1.0
last_updated: 2026-09-12
tagline: Algorithmic reverb with the original DuskVerb engines
---

# DuskVerb 2

## Overview

DuskVerb 2 combines the original DuskVerb reverb engines with a redesigned
interface. The factory presets provide starting points for plates, rooms,
halls, springs and other spaces. Engine selection changes the reverb topology;
some controls change labels or availability to match the selected engine.

## Quick Start

1. Select a factory preset from the top preset menu. The arrows move through presets.
2. Adjust **Decay** and **Size** to fit the source and arrangement.
3. Set **Dry / Wet** for an insert, or enable **Bus** for a fully wet return.
4. Use **Pre-delay** to separate the source from the reverb. Choose a rhythmic
   division in **Pre-delay Sync** to follow the host tempo.
5. Shape the reverb with the Filter and Damping sections, then adjust **Trim**.
6. Click **Save** to store your sound as a user preset.

## Controls

| Section | Purpose |
|---|---|
| Input | Pre-delay, tempo sync and saturation. When sync is active, the free pre-delay value is retained. |
| Filter | Low Cut, High Cut, Mono Below and Mono Depth shape filtering and low-frequency stereo content. |
| Decay / Size | Set the decay and the scale of the selected space. |
| Output | Dry / Wet, Width and Trim. Bus overrides Dry / Wet with a fully wet output while retaining its setting. |
| Early Reflections | Set early-reflection level, size and diffusion; labels can vary by engine. |
| Damping | Shape decay across frequency using the three multipliers and two crossover frequencies. |
| Modulation | Set modulation depth and rate, with engine-specific labels where applicable. |
| Macro | Tone, Character and Duck provide broader sound adjustments. |

**Freeze** holds the reverb. The gated engine also exposes its **Gate** switch.
**Tonal Correction** appears for the Hall engines that support it.

The side meters show input and output levels. **Output History** shows recent
output level; it is not a measured RT60 graph. The central decay readout is the
parameter setting, not a guarantee of a measured acoustic decay time.

## Editing Values

- Drag a knob to adjust it; hold Shift for finer movement.
- Use the mouse wheel to step a value.
- Click a numeric readout or double-click a knob to type a value. Enter applies
  it; Escape cancels. Invalid input stays available for correction.
- Right-click a knob for its reset and value-entry menu.
- Ctrl-click or Alt-click resets a knob on Windows/Linux. On macOS, use
  Alt-click or Cmd-click.
- Drag the editor corner to resize. The minimum size is 1050 × 700.
- Click **?** for the in-plugin control reference.

## Presets and Comparison

**INIT** restores defaults. Factory recall applies the factory sound while
preserving the settings intentionally excluded by the original preset format.
An asterisk beside the preset name indicates an edited sound.

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

The plugin supports mono, mono-to-stereo and stereo operation where the host
exposes those layouts. Select the appropriate host routing before playback.
