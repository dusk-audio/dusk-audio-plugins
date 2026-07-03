---
slug: duskverb
version: 0.6.0
last_updated: 2026-07-02
tagline: Algorithmic reverb with eleven engines (pre-release)
---

# DuskVerb

> **Pre-release.** DuskVerb is currently in pre-release status. Parameter ranges, preset names, and engine behavior may change before the 1.0 release. This manual reflects the current 0.6.x line; check the website for updates if you are reading an older copy.

## Overview

![DuskVerb main UI](screenshots/duskverb/01-main-ui.png)

DuskVerb is an algorithmic reverb with eleven distinct engines under one user interface. Each engine targets a different reverb territory: **Plate** is the classic dense plate that works on almost everything; **Vintage Plate** is the darker, steely vintage-plate character; **Smooth Plate** is a smooth six-allpass plate; **Chamber** is a studio chamber and tight-room voice; **Spring** is the surf-guitar spring sound; **Gated** produces the gated 1980s drum sound and other non-natural curves; **Shimmer** adds pitched sparkle above and warmth below the tail; **Reverse** is the swelling reverse effect; **Hall** is a realistic per-octave-tuned hall; **Tiled Room** is a bright tiled room; and **Dense Hall** is a lush, densely diffused hall.

Use it where you would use any reverb. Vocals, drums, guitars, and full-mix space all have an engine in DuskVerb that is voiced for them. The 20 factory presets are anchored to specific hardware references (Lexicon 224 and 480L, EMT 140, AMS RMX16, and others) and serve both as starting points and as a tutorial in what each engine can do.

It is not a convolution reverb (use Convolution Reverb for IR-based work), and it is not a delay/multitap effect. It is an eleven-in-one algorithmic reverb that lets you pick the right engine for the job.

## Quick Start

1. Insert DuskVerb on a return bus or directly on a track. For most workflows, an aux/return at 100% wet is the cleanest setup.
2. Pick an **Algorithm** from the top dropdown. If you are not sure, start with **Plate**; it works on almost everything.

![Algorithm dropdown showing the engine list](screenshots/duskverb/02-algorithm-dropdown.png)
3. Open the preset menu. Pick one that matches your source (Vocal Plate for vocals, Small Drum Room for drums, and so on). Presets are laid out in a two-column menu grouped by category.
4. Adjust **Decay Time** to taste. The Decay knob is calibrated so the position reflects the actual tail length; longer decays sustain more, shorter decays sit more discreetly.
5. Use **Pre-Delay** (0 to 250 ms) to push the reverb tail later, which keeps the dry signal forward and the reverb perceived as space rather than smear.
6. The **Dry/Wet** knob controls the mix. On a return bus, leave at 100%. On an insert, dial back to 20-40% wet.

You should hear a tail when you stop the source. If the tail is too dark, raise **Treble Multiply** (or nudge the **Tone** macro brighter); too bright, lower it. If the tail rings or sounds metallic, drop **Diffusion** below the preset value or pick a different engine.

## Workflows

### Vocal plate on a lead vocal

**Source:** A lead vocal that needs space without sounding wet.
**Goal:** Smooth, classic plate tail that thickens the vocal without obscuring it.

Settings (or load the **Vocal Plate** preset and tweak from there):

- **Algorithm:** Plate
- **Decay Time:** 1.8 s
- **Pre-Delay:** 30 ms
- **Size:** 0.6
- **Mod Depth:** 0.15
- **Mod Rate:** 1.0 Hz
- **Treble Multiply:** 0.8 (slight high-frequency rolloff in the tail)
- **Lo Cut:** 200 Hz (keeps the tail out of the bass range)
- **Hi Cut:** 8000 Hz
- **Dry/Wet:** 100% (use as a send return)

Why this works. Plate reverbs are dense from the first reflection; they do not have audible early reflections. Pre-delay of 30 ms separates the tail from the dry signal so the vocal stays forward. Lo Cut at 200 Hz prevents the tail from muddying the low end. Subtle modulation (depth 0.15, rate 1 Hz) keeps the plate from sounding static. The 1.8-second decay is a typical pop-vocal length.

### Tight drum room

**Source:** Drum bus or a snare track that needs room without long tail.
**Goal:** Short, punchy ambience that adds size without smearing transients.

Settings (or load the **Small Drum Room** preset):

- **Algorithm:** Chamber
- **Decay Time:** 0.6 s
- **Pre-Delay:** 0 ms
- **Size:** 0.4
- **Diffusion:** 0.5 (less than fully diffuse keeps transients distinct)
- **Early Ref Level:** 0.7 (early reflections are most of what you hear)
- **Treble Multiply:** 0.95 (keep highs in the room)
- **Lo Cut:** 80 Hz
- **Dry/Wet:** 25% (insert) or 100% on a return

Why this works. Short decay plus low diffusion plus prominent early reflections gives a "room" sound rather than a "reverb" sound. The drums still hit hard; the room just adds a bit of three-dimensional space. The **Small Drum Room** and **Medium Drum Room** presets use these proportions.

### Realistic concert hall on piano

**Source:** Solo piano or piano in a sparse mix.
**Goal:** A long, lush hall tail that sounds like a real space.

Settings (or use the **Cathedral Large Hall** preset):

- **Algorithm:** Hall
- **Decay Time:** 4.5 s
- **Pre-Delay:** 60 ms
- **Size:** 0.85
- **Diffusion:** 0.85
- **Early Ref Level:** 0.4
- **Early Ref Size:** 0.7
- **Bass Multiply:** 1.2 (emphasize the low frequencies in the tail, characteristic of large rooms)
- **Mid Multiply:** 1.0
- **Treble Multiply:** 0.7 (concert halls absorb highs)
- **Lo Cut:** 60 Hz
- **Hi Cut:** 12000 Hz
- **Dry/Wet:** 100% on a return at -10 to -6 dB send level

Why this works. The Hall engine tunes decay per octave, so it produces realistic late reverberation with audible per-frequency decay differences. Bass multiply above 1 emphasizes the long bass tail typical of real halls. Treble multiply below 1 captures the high-frequency absorption you hear in rooms with absorptive surfaces. Long pre-delay (60 ms) maintains piano clarity. For a denser, smoother variant of the same space, try the **Dense Hall** engine.

### Gated 80s snare

![Gated engine with Gate enabled](screenshots/duskverb/03-engine-non-linear.png)

**Source:** Snare drum that needs the classic 1980s gated sound.
**Goal:** A non-natural reverb shape that cuts off after a fixed time.

Settings (or load the **1981 Gated Snare** preset):

- **Algorithm:** Gated
- **Decay Time:** 1.2 s (the gate length, not natural decay)
- **Pre-Delay:** 0 ms
- **Size:** 0.6
- **Treble Multiply:** 1.0
- **Gate:** Enabled (default; this is the parameter that makes the engine non-linear)
- **Dry/Wet:** 35% (insert)

Why this works. The Gated engine mimics the AMS RMX16's "Non-Lin 2" algorithm: the reverb has constant level for a fixed time (set by Decay Time) and then cuts to silence rather than fading naturally. This produces the 1980s snare sound that defined a decade of records. For the swelling backward version instead, load **Reverse Taps** on the **Reverse** engine.

## Parameter Reference

### Algorithm

- **Algorithm:** Selects which DSP engine processes the audio. Eleven choices: Plate, Vintage Plate, Smooth Plate, Chamber, Spring, Gated, Shimmer, Reverse, Hall, Tiled Room, Dense Hall. Switching engines crossfades over a few hundred milliseconds.

### Macros

The bottom macro row applies three global shapers on top of whatever engine and preset you have loaded. At their defaults they are neutral, so a preset sounds exactly as designed until you move them.

- **Tone:** -1 to +1. Spectral tilt from dark to bright. Negative values roll the tail down (lower Treble Multiply and Hi Cut); positive values open it up. A fast way to match a reverb to a track without touching the per-band controls.
- **Character:** 0 to 1. Movement and grit. Adds modulation depth and saturation together, taking a static tail toward a livelier, slightly driven one.
- **Duck:** 0 to 1. Wet ducking depth, sidechained off the dry input. Pulls the reverb down while the source plays and lets it bloom in the gaps, keeping dense mixes clear.

### Mix and routing

- **Dry/Wet:** 0 to 100%. Wet/dry mix at the plugin output. 100% on returns; lower on inserts.
- **Bus Mode:** On or off. When on, the plugin assumes a 100% wet send/return setup and bypasses the dry path entirely (slight CPU savings).
- **Bypass:** Reports zero latency to the host while bypassed.

### Time and size

- **Pre-Delay:** 0 to 250 ms. Delay before the reverb tail begins. Longer pre-delay separates the dry signal from the reverb.
- **Pre-Delay Sync:** Free, 1/32, 1/16, 1/8, 1/4, 1/2, 1/1. When set to a note value, pre-delay locks to the host tempo.
- **Decay Time:** 0.2 to 30 s. Total tail length. The knob is calibrated so its position tracks the actual decay time across engines. The Hall engine reaches the highest decays; the Gated engine treats this as a gate length.
- **Size:** 0 to 1. Perceived room size. Engine-dependent: in Chamber, low values are cabinet-sized and high values are arena-sized.

### Modulation

- **Mod Depth:** 0 to 1. Amount of pitch modulation in the tail. Subtle values (0.1 to 0.2) keep the tail moving; high values (0.5 plus) produce chorus-like effects.
- **Mod Rate:** 0.1 to 10 Hz. Speed of the modulation. Slower rates (around 0.5 Hz) sound natural; faster rates produce flanger-like motion.

### Frequency shaping

- **Bass Multiply:** 0.3 to 2.5. Decay-time multiplier for low frequencies. Above 1 makes the bass decay longer than the rest (typical of large halls). Below 1 shortens the bass tail (typical of small studios with absorbers).
- **Mid Multiply:** 0.3 to 2.5. Decay multiplier for the mid range.
- **Treble Multiply:** 0.1 to 1.5. Decay multiplier for the high frequencies. Below 1 produces high-frequency damping (the "warm" tail).
- **Low Crossover:** 200 to 4000 Hz. Frequency where Bass Multiply gives way to Mid Multiply.
- **High Crossover:** 1000 to 12000 Hz. Frequency where Mid Multiply gives way to Treble Multiply.
- **Lo Cut / Hi Cut:** Filters the input into the reverb. Lo Cut keeps low frequencies dry; Hi Cut keeps highs dry. Useful for keeping the reverb out of the kick and the air bands respectively.

### Color and density

- **Saturation:** 0 to 1. Drives a soft saturation stage in the reverb feedback path. Adds warmth and harmonic content.
- **Diffusion:** 0 to 1. Density of the reverb tail. High diffusion is dense and smooth; low diffusion sounds grainier and more echo-like.
- **Early Ref Level:** 0 to 1. How loud the early reflections are relative to the main tail.
- **Early Ref Size:** 0 to 1. The simulated room size for the early reflections.

### Output and stereo

- **Width:** 0 to 2. Stereo width of the reverb output. 1 is unity; 2 is double-wide (M/S based).
- **Freeze:** Off or On. When on, the reverb tail cycles infinitely.
- **Gate:** Off or On. Enables the Gated engine's gating behavior.
- **Mono Below:** 20 to 300 Hz. Frequencies below this cutoff are summed to mono in the reverb output. Default 20 Hz (effectively bypass); typical settings 80 to 150 Hz to keep low-frequency reverb mono-compatible.
- **Gain Trim:** -48 to +48 dB. Final output level adjustment.

## Tips and Traps

- **Pre-Delay is your most important parameter for clarity.** Without pre-delay, the reverb tail starts on the same sample as the dry signal and the result smears. 20 to 60 ms of pre-delay keeps vocals and drums distinct from their reverb.
- **The macros are the fast path.** Tone, Character, and Duck reshape any preset without diving into the per-band controls. Reach for them first when a preset is close but not quite matched to the track.
- **Hardware anchors are real.** The presets are tuned to specific hardware references. "Vintage Vocal Plate" leans on the EMT 140's darker, steely plate character; "Blade Runner 224" captures the long-decay Lexicon 224 sound; "Cathedral Large Hall" is a long Lexicon-style hall. If you are familiar with the source hardware, the preset name tells you what to expect.
- **Engine switching is not parameter-preserving.** Each engine has its own internal state. Switching engines while a tail is decaying produces a crossfade; do not expect identical-sounding results across engines at the same parameter values.
- **Freeze is loud.** Freeze captures the current reverb tail and loops it indefinitely. Levels can build dramatically; pull Gain Trim back before enabling Freeze on a busy mix.

![Freeze engaged](screenshots/duskverb/04-freeze-engaged.png)
- **Mono Below preserves bass mono compatibility.** Stereo reverb on bass frequencies often phases on mono fold-down. Set Mono Below to 80 to 120 Hz on mastering or mix-bus reverb.
- **The Gated engine treats Decay Time as a gate length.** It does not behave like other engines for that parameter. Refer to the gated-snare workflow above.

## Presets Explained

![Preset menu by category](screenshots/duskverb/05-presets-by-category.png)

DuskVerb ships with 20 factory presets, laid out in a two-column menu grouped by category. Each is hardware-anchored and serves as a starting point.

### Plates

Four plate presets: **Vocal Plate** is the bright, smooth plate for lead vocals; **Vintage Vocal Plate** is the darker, steely EMT 140 character; **Drum Plate** is the punchy plate for snares and drums; **Vintage Gold Plate** is the long, lush plate that helped define 1980s record production.

### Halls

Four halls, from bright studio (**Bright Hall**) through vocal-tuned (**Vocal Hall**) to massive (**Cathedral Large Hall**, a long Lexicon-style concert hall). **Blade Runner 224** captures the long-decay extended-tail Lexicon 224 sound. Pick by length and brightness.

### Chambers

**79 Vocal Chamber** is a studio chamber voiced for vocals (Chamber engine); **Large Chamber** is a bigger, denser chamber space (Dense Hall engine). Both sit between a room and a hall in size.

### Rooms

Four rooms covering tight (**Small Drum Room**) through medium (**Medium Drum Room**, **Live Room**) to bright and reflective (**Tiled Room**). Use **Small Drum Room** as your default for drum-bus ambience.

### Springs

**Surf '63 Spring** is the Dick Dale "Misirlou" spring sound; aggressive and bouncy, for surf and reggae guitar work.

### Gated and Reverse

**1981 Gated Snare** is the classic gated-reverb snare on the Gated engine. **Reverse Taps** is the swelling backward-reverb effect on the Reverse engine.

### Ambient and Shimmer

**Ambience** is a soft atmospheric wash for pads and sound design. The shimmer presets use the overhauled Shimmer engine, which adds a post-loop high-frequency air voice for sparkle above the tail and a down-octave voice for warmth below it: **Deep Blue Day** (after the Brian Eno track) is the lush, warm shimmer; **Black Hole** is the huge, sustaining ambient shimmer.

## Troubleshooting

**The tail sounds metallic or rings.** Drop **Diffusion** to around 0.5 to 0.7 if the tail is buzzing or sounds like a ring modulator. Try a different engine; the Hall and Dense Hall engines are the smoothest at long decays, while the Plate engines are naturally denser.

**The reverb is too loud at low frequencies.** Raise **Lo Cut** to 100 to 200 Hz so the reverb input is high-passed before reaching the engine. Bass frequencies in reverb tails build up quickly and muddy a mix.

**Switching engines drops the tail.** That is intentional. Each engine has its own internal state; the crossfade between engines is brief (a few hundred milliseconds) and the new engine starts with a clean tail. Plan engine switches between sections, not within a phrase.

**The sound is the same regardless of which preset I load.** Confirm you are loading from the **Preset** menu and not just changing the **Algorithm**. Loading a preset sets all parameters including the algorithm; changing only the algorithm leaves the other parameters at their previous values.

**My host shows extra latency on insert.** DuskVerb has a small fixed latency from its early-reflection delay line. The host applies plug-in delay compensation automatically; check that PDC is enabled in your DAW preferences if other tracks sound out of sync.
