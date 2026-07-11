---
slug: sunset-circuits
version: 1.0.0
last_updated: 2026-07-11
tagline: Six vintage synth circuits in one instrument (pre-release)
---

# Sunset Circuits

> **Pre-release.** Sunset Circuits has not shipped a public release yet. Parameter ranges, preset names, and mode behavior may change before the 1.0 release. This manual reflects the current pre-release build; check the website for updates if you are reading an older copy.

## Overview

Sunset Circuits is six vintage synthesizers in one instrument. A row of six backlit mode rockers across the top picks the engine, and each mode is a different classic circuit with its own voice architecture, its own signature controls, and its own panel color. Switching modes is a creative decision, not just a preset change; the whole personality of the instrument changes with it.

The six modes cover the ground that defined subtractive and FM synthesis:

- **Cosmos** is an early-80s Japanese six-voice poly built on digitally controlled oscillators, with a clean non-resonant filter and a built-in bucket-brigade chorus.
- **Oracle** is a late-70s American five-voice poly with two analog oscillators, a self-oscillating filter, and poly-modulation routing.
- **Mono** is an aggressive 70s monophonic voice with two oscillators plus a sub, a fat driven filter, ring modulation, and hard sync.
- **Modular** is a 70s semi-modular patchable voice with three oscillators, a transistor-ladder filter, sample and hold, and a spring reverb.
- **Prism** is a mid-80s four-operator FM digital engine with eight routing algorithms and per-operator envelopes.
- **Acid** is the silver bass box: one oscillator through a screaming diode-ladder filter, with accent and slide and a sixteen-step pitch pattern sequencer.

The same bone structure is present in every mode. The oscillator and mixer panels sit on the left, the filter and two envelopes fill the center, the two LFOs and the mod matrix are on the right, and the sequencer and effects run along the bottom with a playable keyboard beneath them. Because the layout never moves, you keep your bearings when you switch circuits; only the colors, the mode sub-panel, and a handful of mode-specific controls change.

Sunset Circuits ships with 54 factory presets spread across all six modes, plus a user preset library you build yourself. It is a full instrument, not an effect: insert it on an instrument track, feed it MIDI, and play.

## Getting Started

### Formats and installation

Sunset Circuits builds as VST3, CLAP, and LV2 on every platform, plus an Audio Unit on macOS. The binaries share the base name `sunset_circuits`.

**Linux**

```
VST3: ~/.vst3/sunset_circuits.vst3
CLAP: ~/.clap/sunset_circuits.clap
LV2:  ~/.lv2/sunset_circuits.lv2
```

**Windows**

```
VST3: C:\Program Files\Common Files\VST3\sunset_circuits.vst3
CLAP: C:\Program Files\Common Files\CLAP\sunset_circuits.clap
```

**macOS**

```
VST3: ~/Library/Audio/Plug-Ins/VST3/sunset_circuits.vst3
CLAP: ~/Library/Audio/Plug-Ins/CLAP/sunset_circuits.clap
AU:   ~/Library/Audio/Plug-Ins/Components/sunset_circuits.component
```

After installing, rescan plugins in your DAW. If the instrument does not appear, confirm the file landed in the folder above and that your host scans that folder.

### The six mode rockers

The six mode rockers at the top center are the most important control in the instrument. Click one to switch circuits. The panel color crossfades over about a quarter second, the mode sub-panel (the small panel on the right, below the LFOs) morphs to show that mode's signature controls, and a few controls in the standard panels appear or disappear depending on which mode is active. The mode is a normal automatable parameter, so you can switch circuits from the host if you want.

Loading a factory preset also sets the mode, because the mode is stored with the preset. If you switch modes by hand without loading a preset, the other parameters keep their current values, so the sound may not match what you expect from that circuit until you load a preset or dial it in.

### Playing

The full-width keyboard at the bottom is playable with the mouse and sends MIDI notes to the engine. The OCT- and OCT+ buttons at its left shift the played octave. Notes arriving from your DAW light up on the keyboard as they play. For real performance, drive the instrument from a MIDI controller or a piano roll; the on-screen keyboard is for quick auditioning.

## The Six Modes

### Cosmos: six-voice DCO poly

Cosmos is modeled on an early-80s Japanese polysynth with digitally controlled oscillators. It is six-voice polyphonic. The DCO gives it rock-steady tuning, which is what made this class of synth a workhorse for pads, plucks, and arpeggios. The filter is a clean 24 dB per octave low-pass that does not self-oscillate, paired with a separate high-pass control for thinning the low end.

The signature of Cosmos is the built-in bucket-brigade chorus. The mode sub-panel shows three round chorus buttons, **I**, **II**, and **I+II**, which set the `Chorus Mode`. Chorus I is the subtle single-rate chorus; Chorus II is faster and deeper; I+II runs both together for the lush, wide, slightly seasick wash this synth is famous for. This chorus is separate from the global chorus in the effects section, and it is the fastest way to get the classic warm poly sound.

Mode-specific controls visible in Cosmos: the **Sub** oscillator (wave and level), the filter **HP** knob, the **Cross Mod** knob (oscillator 2 modulating oscillator 1 at audio rate), and the three chorus buttons.

### Oracle: five-voice poly with poly-mod

Oracle is modeled on a late-70s American five-voice polysynth with two true analog oscillators per voice and a self-oscillating low-pass filter. Where Cosmos is clean and stable, Oracle is characterful and alive. The filter reaches self-oscillation at high resonance, so it can sing on its own.

The signature of Oracle is poly-modulation. The mode sub-panel is a four-knob **POLY-MOD** grid that routes modulation sources into destinations per voice:

- **FEnv to OscA**: the filter envelope modulates oscillator 1 pitch, for pitched attack transients and bell tones.
- **OscB to OscA**: oscillator 2 modulates oscillator 1 pitch at audio rate, for clangorous inharmonic timbres.
- **OscB to PW**: oscillator 2 modulates oscillator 1 pulse width, for animated pulse textures.
- **FEnv to Filter**: the filter envelope is added on top of the filter cutoff, for extra bite.

Mode-specific controls visible in Oracle: the **Cross Mod** knob and the four poly-mod knobs. There is no sub oscillator and no filter high-pass in this mode; the OSC3/SUB panel is inactive.

### Mono: aggressive monophonic

Mono is modeled on a 70s Japanese monophonic synth voiced for aggression: two oscillators plus a sub, a fat driven filter, ring modulation, and hard sync. It is a single voice (one note at a time), which is exactly right for basses and leads that need weight and glide.

Mode-specific controls visible in Mono: the **Sub** oscillator (wave and level), and the mode sub-panel's **RING / SYNC** section with the **Ring Mod** knob (ring modulation between oscillators 1 and 2) and the **Hard Sync** button (oscillator 2 hard-syncs to oscillator 1 for tearing, vocal-formant timbres). The oversized cutoff knob dominates the panel, as it should for a mono lead.

### Modular: semi-modular

Modular is modeled on a 70s semi-modular synthesizer: three oscillators, a transistor-ladder filter, linear FM between oscillators, a sample and hold, and a real dispersive spring reverb. It is two-voice, so you can play the occasional dyad, but it is at its best on evolving drones, sci-fi effects, and sequences.

Mode-specific controls visible in Modular: the third oscillator (**Osc 3** wave and level), the **Ring Mod** knob, the **Hard Sync** button, the **FM Amount** knob (linear FM from oscillator 1 into oscillator 2), and the mode sub-panel's **SAMPLE & HOLD** section with the **S&H Rate** knob and an animated staircase display. The panel wears decorative patch jacks and cables to complete the look; they are visual only and never interactive. When Modular is active the effects reverb shows **SPRING** to indicate the spring reverb model is engaged for that mode.

### Prism: four-operator FM

Prism is a mid-80s four-operator FM digital engine. It is eight-voice polyphonic. Instead of oscillators and a filter shaping a rich waveform, FM builds timbre by having sine-wave operators modulate each other's phase. The result is the glassy electric pianos, chiming bells, punchy basses, and metallic brass that defined 80s digital synthesis.

When Prism is active, the left column re-skins into an **operator matrix**: four stacked operator strips replace the standard oscillator panels. Each strip has these controls:

- **Ratio**: the operator's frequency as a multiple of the played note, stepped through a musical snap list.
- **Fine**: fine detune of the operator in cents, for slight beating and inharmonicity.
- **Level**: the operator's output. For a carrier this is volume; for a modulator this is modulation depth (FM brightness).
- **Vel**: how strongly note velocity affects that operator's level, so harder playing gets brighter.
- **Key Scale**: how the operator's level changes across the keyboard, for rolling a bright tine or edge off the top of the range.
- **A / D / S / R**: a full envelope per operator, so each partial can have its own attack and decay.

Operator 4 additionally carries the **Feedback** knob, which feeds the operator back into its own phase for growl and edge. The filter section stays in circuit in Prism, so you can still filter the FM tone if a preset opens the cutoff.

The mode sub-panel is the **algorithm** widget: eight clickable thumbnail diagrams that select the operator routing, above a large diagram of the active algorithm. See the FM guide below for what each algorithm does.

### Acid: bass box and pattern sequencer

Acid is the silver bass box: one oscillator (saw or square) through a three-pole diode-ladder low-pass filter that screams near self-oscillation, with an accent circuit and note-to-note slide, driven by a sixteen-step pattern sequencer. It is monophonic. This is the sound of a thousand acid basslines.

Acid wears a silver panel instead of the dark chassis of the other modes. The oscillator waveform combo defaults to saw and is intended to be saw or square, though the other waves remain available.

**The shared-knob mapping in Acid.** Rather than adding a wall of new controls, Acid re-purposes the standard panels you already know:

- **Filter Cutoff** sets the base brightness of the line, the knob you sweep for the classic wah.
- **Filter Resonance** is the squelch. Push it toward the top (around 0.8 and up) for the resonant whistle; near 0.95 the filter is at the edge of self-oscillation, which is the scream.
- **Filter Env Amt** is how far each note's envelope opens the filter, the pluck of the sound.
- **Amp Decay** (with Amp Sustain at zero) sets how quickly each step decays, from short and staccato to long and connected.
- The two Acid globals live in the mode sub-panel: **Acid Accent** sets how much an accented step boosts level, resonance, and envelope depth (the "wow" that makes accents jump out), and **Acid Slide** sets the glide time for slid steps.

The pattern itself lives in the sequencer, which expands to three lanes in Acid mode. See the sequencer chapter below for the pitch, accent, and slide lanes.

### Per-mode visibility summary

| Control | Cosmos | Oracle | Mono | Modular | Prism | Acid |
|---|---|---|---|---|---|---|
| Standard OSC 1 / OSC 2 panels | yes | yes | yes | yes | hidden (operator matrix) | yes (saw/square) |
| Osc 3 wave and level | no | no | no | yes | no | no |
| Sub wave and level | yes | no | yes | no | no | no |
| Filter HP | yes | no | no | no | no | no |
| Cross Mod | yes | yes | no | no | no | no |
| Poly-mod (4 knobs) | no | yes | no | no | no | no |
| Ring Mod | no | no | yes | yes | no | no |
| Hard Sync | no | no | yes | yes | no | no |
| FM Amount | no | no | no | yes | no | no |
| S&H Rate | no | no | no | yes | no | no |
| Chorus I / II / I+II | yes | no | no | no | no | no |
| Operator strips and algorithm | no | no | no | no | yes | no |
| Acid globals and 3 sequencer lanes | no | no | no | no | no | yes |

Controls that are irrelevant to a mode are hidden, not greyed out. A hidden widget only removes the on-screen control; the underlying parameter always stays in the host's parameter list, so automation lanes never break.

## FM Guide (Prism)

FM synthesis in Prism uses four sine-wave **operators**. An operator that reaches the output is a **carrier** (you hear it). An operator that instead modulates another operator's phase is a **modulator** (you hear its effect on the carrier's timbre, not the operator itself). The **algorithm** is the wiring diagram that decides which operators are carriers and which modulate which.

The two levers that shape an FM tone are operator **Level** and operator **Ratio**. Raising a modulator's Level adds harmonics and makes the carrier brighter (this is the FM equivalent of opening a filter). The modulator's Ratio decides where those harmonics land: integer ratios (1, 2, 3) give harmonic, pitched tones, while non-integer ratios (2.76, 5.4) give inharmonic, bell-like and metallic tones. Operator 4's **Feedback** turns operator 4 into a controllable noise and edge source, from a touch of grit to a full growl.

### The eight algorithms

Operators are numbered 1 to 4. In the table, `a to b` means operator `a` modulates operator `b`, and the carriers are the operators you hear. Operator 4 is the feedback operator in every algorithm.

| # | Name | Routing | Carriers | Character |
|---|---|---|---|---|
| 1 | Serial | 4 to 3, 3 to 2, 2 to 1 | 1 | One long modulation stack into a single carrier. The brightest and most metallic algorithm; the classic bright bass and lead voice. |
| 2 | Stack-2M | 4 to 2, 3 to 2, 2 to 1 | 1 | Two modulators feed one, which feeds the carrier. Rich and vocal, good for reeds and complex leads. |
| 3 | Branch | 4 to 2, 4 to 3, 2 to 1, 3 to 1 | 1 | One modulator fans out into two, both of which modulate the carrier. Dense and harmonically complex. |
| 4 | Y-Split | 4 to 3, 3 to 1, 3 to 2 | 1, 2 | A serial modulation chain that splits into two carriers. The classic tine electric-piano structure. |
| 5 | Dual | 2 to 1, 4 to 3 | 1, 3 | Two independent two-operator stacks. Layer a body tone against a tine or edge; the workhorse e-piano algorithm. |
| 6 | Twin+1 | 3 to 1, 3 to 2 | 1, 2, 4 | One modulator into two carriers, plus a third clean standalone carrier. Adds a pure sine partial under a modulated pair. |
| 7 | Tri+FM | 4 to 3 | 1, 2, 3 | One modulated tone plus two clean carriers. Mostly additive with a single FM color; organ-like with an edge. |
| 8 | Additive | (none) | 1, 2, 3, 4 | Four parallel carriers, no modulation. Pure additive synthesis; drawbar-organ and formant tones. |

```
  Alg 1 (serial)       Alg 4 (Y-split)      Alg 5 (dual stack)     Alg 8 (additive)
     [4]                  [4]                  [2]   [4]            [1][2][3][4]
      |                    |                    |     |              |  |  |  |
     [3]                  [3]                  [1]   [3]             ============
      |                   /  \                  |     |               (output bus)
     [2]                [1]  [2]              ==== output bus ====
      |                 ==== output ====
     [1]
   ==== output ====
```

The algorithm diagram widget in the Prism sub-panel draws the active routing live, brightens the carriers, and shows the feedback loop on operator 4 with a thickness that tracks the Feedback knob. Click any of the eight thumbnails to switch algorithms.

## Sequencer and Arpeggiator

The sequencer strip along the bottom is an arpeggiator and step sequencer shared by all six modes. In modes Cosmos through Prism it behaves as a classic arpeggiator with sixteen step-mute cells; in Acid it expands into a three-lane pattern sequencer.

### Arpeggiator controls

- **Arp On**: enable the arpeggiator or step sequencer.
- **Arp Mode**: the note order the arpeggiator plays: Up, Down, Up/Down, Down/Up, Random, Order (as played), or Chord (all held notes on every step).
- **Arp Octave**: how many octaves the arpeggio spans, 1 to 4.
- **Arp Rate**: the step length as a note division, from 1/1 down to 1/32, including dotted (1/8. and so on) and triplet (1/8T and so on) divisions.
- **Arp Gate**: the length of each step relative to its slot, from staccato to fully connected.
- **Arp Swing**: delays the off-beat steps for a swung feel while keeping the downbeats on the grid.
- **Arp Latch**: holds the pattern after you release the keys, so it keeps playing hands-free.
- **Arp Vel Mode**: where each step's velocity comes from: As Played, Fixed, or Accent.
- **Arp Fixed Vel**: the velocity used when the mode is Fixed.
- **Step cells**: the sixteen cells turn individual steps on or off (step-mutes) in every mode.

### Host sync behavior

The arpeggiator always follows the host tempo. The step length is computed from the host BPM and the Arp Rate division, so at 128 BPM a 1/8 step is the correct length automatically, whether or not the transport is running. Auditioning with the transport stopped still steps at the host tempo.

When the DAW transport is playing and reports a valid song position, the arpeggiator phase-locks to the absolute host beat grid. Steps land on the beat rather than on wherever you happened to press the key, and the pattern re-syncs cleanly on a loop wrap so it never drifts over a long section. This is a quantized start: the first step of a held chord snaps to the grid.

When the transport is stopped, the arpeggiator free-runs from the moment you play, still at the host tempo, so you can practice or sound-design without pressing play. Switching between the locked and free states cleanly releases any sounding note so a transport change can never leave a note stuck on.

### Acid pattern lanes

In Acid mode the sequencer expands into three lanes that together define the sixteen-step pattern. You hold a single root note and the pattern transposes with it.

- **Gate lane** (top): the sixteen on/off cells, the same step-mutes used in the other modes. A muted step is a rest.
- **Pitch lane** (middle): sixteen vertical drag columns, one per step, setting each step's pitch offset from -24 to +24 semitones relative to the held root. Drag a column up or down; the bar fills from the center zero line. This is where the melodic shape of the line lives.
- **Accent and Slide lanes** (bottom): two rows of sixteen cells. An **Accent** cell makes that step jump out using the Acid Accent amount (louder, more resonant, more envelope). A **Slide** cell glides the pitch into that step over the Acid Slide time, for the legato portamento that defines the acid sound.

The live step position highlights the current column across all three lanes as the pattern plays.

## Mod Matrix

The mod matrix is an eight-slot modulation router, opened from the **MOD MATRIX** bar on the right (the bar shows how many slots are active). Each slot connects one source to one destination with a bipolar amount, so you can invert a modulation by using a negative amount. All eight slots are always available in every mode.

**Sources**

| Source | What it is |
|---|---|
| None | Slot inactive. |
| LFO 1 / LFO 2 | The two low-frequency oscillators. |
| Filt Env | The filter envelope. |
| Mod Whl | The MIDI modulation wheel. |
| Aftertouch | Channel aftertouch (pressure). |
| Velocity | Note-on velocity. |
| Key Track | Note pitch across the keyboard. |
| Random | A new random value per note. |
| Pitch Bend | The pitch-bend wheel (in addition to its normal pitch effect). |
| S&H | The sample-and-hold value. |

**Destinations**

| Destination | What it modulates |
|---|---|
| None | Slot inactive. |
| Osc1 Pitch / Osc2 Pitch | Oscillator 1 or 2 frequency. |
| Osc1 PW / Osc2 PW | Oscillator 1 or 2 pulse width. |
| Cutoff | Filter cutoff frequency. |
| Reso | Filter resonance. |
| Amp | Voice amplitude. |
| Pan | Stereo position. |
| LFO1 Rate / LFO2 Rate | The speed of LFO 1 or 2. |
| FX Mix | The wet/dry balance of the effects. |
| Uni Det | The unison detune spread. |

A classic starting patch is LFO 1 to Cutoff for a filter wobble, or Key Track to Cutoff so the filter opens as you play higher. Aftertouch to LFO1 Rate gives expressive vibrato that speeds up under pressure.

## User Presets

Beyond the 54 factory presets, Sunset Circuits keeps a personal preset library you build yourself. It is stored as files on disk, independent of any DAW session, so your patches follow you between projects and hosts.

### Saving a preset

Click the **star** button in the top bar next to the preset browser. A name-entry box appears; type a name and confirm. The current state of all parameters is written to a file, and the new preset appears in the preset menu below a separator, in its own user section under the factory presets. If a preset with that name already exists you are asked to confirm the overwrite. You can delete a user preset from the same modal.

Loading a user preset works exactly like a factory preset: pick it from the menu, or step to it with the previous and next arrows, and every parameter (including the mode) is restored.

### File location

User presets are plain files in a per-user application-data folder:

```
Linux:   ~/.config/DuskAudio/SunsetCircuits/presets/
macOS:   ~/Library/Application Support/DuskAudio/SunsetCircuits/presets/
Windows: %APPDATA%\DuskAudio\SunsetCircuits\presets\
```

On Linux, if `XDG_CONFIG_HOME` is set it is used in place of `~/.config`. Each preset is one file named after the preset with the extension `.scpreset`.

### The .scpreset format

A `.scpreset` file is versioned plain text: a header line, a `name=` line, and one `symbol=value` line per parameter. It is human-readable and safe to back up, copy between machines, or share. Because it is plain text, you can inspect or hand-edit a patch if you want, though the plugin is the intended way to create them. When loading, any parameter the file does not mention keeps its factory default, and any symbol the plugin does not recognize is skipped, so presets stay forward-compatible as the instrument grows. A file with an unknown format version is rejected rather than loaded incorrectly.

## Effects

Every mode runs through the same effects chain at the bottom right: Drive, Chorus, Delay, and Reverb, each with its own enable button. The Cosmos mode additionally has its own built-in vintage chorus, separate from the effects chorus.

### Drive

A saturation stage with three characters selected by **Drive Type**: **Soft** (gentle tube-like warmth), **Hard** (aggressive clipping), and **Tube** (asymmetric tube drive). **Drive Amount** sets how hard it pushes and **Drive Mix** blends the driven signal against the clean one. Use it for grit on basses and leads, or a light Soft setting to thicken pads.

### Chorus

A standard stereo chorus with **Rate**, **Depth**, and **Mix**. This is the global chorus available in every mode. It widens and animates any sound.

### Vintage chorus (Cosmos)

Cosmos has its own bucket-brigade chorus built into the circuit, controlled by the three **I / II / I+II** buttons in the mode sub-panel rather than by the effects Chorus. This is the authentic vintage chorus of the original hardware and is the source of that synth's signature warmth. You can run it alongside the effects chorus, but usually one or the other is plenty.

### Delay

A stereo delay with **Feedback** and **Mix**, plus **Ping-Pong** and **Tape** options. Ping-Pong bounces the echoes across the stereo field; Tape adds tape-style warmth and saturation to the repeats. The delay can run free or synced:

- **Free**: the **Delay Time** knob sets the delay in milliseconds, up to 2000 ms.
- **Synced** (Delay Sync on): the **Delay Division** knob sets the delay as a note division locked to the host tempo.

The delay buffer holds 2 seconds. In synced mode at a slow tempo, a long division can ask for more than 2 seconds (for example a dotted half note below about 90 BPM); the delay is clamped to the 2-second buffer, so at very slow tempi the longest divisions stop tracking the tempo and hold at 2 seconds. Free mode is already capped at the 2000 ms knob maximum.

### Reverb

A reverb with **Size**, **Decay**, **Damping**, **Pre-Delay**, and **Mix**. In modes Cosmos through Prism this is an algorithmic reverb. In **Modular** mode the reverb becomes a real dispersive **spring** reverb, matching the spring tank of the semi-modular hardware; the panel shows SPRING to indicate this. The spring gives the boingy, dispersive character that a digital reverb cannot, and it is part of what makes Modular sound like a patchable vintage instrument.

## Performance Notes

### Oversampling

The **Oversampling** control sets the internal processing rate: **1x**, **2x**, or **4x**. Higher factors reject aliasing in the oscillators, the filter, and the FM operators at the cost of more CPU. The voices render at the oversampled rate; the effects run at the host rate.

- **1x**: lowest CPU, most aliasing on bright and high-pitched sounds. Fine for pads and bass.
- **2x**: the default and the recommended everyday setting. A good balance of clean high end and reasonable CPU.
- **4x**: the cleanest high end, for exposed leads, bright FM tones, and high notes where aliasing would be audible. Use it when you can afford the CPU.

Factory presets ship at 2x.

### Reported latency

Oversampling adds a small, fixed processing latency from the decimation filter, which the plugin reports to the host so plugin delay compensation keeps everything aligned:

| Oversampling | Reported latency |
|---|---|
| 1x | 0 samples |
| 2x | 12 samples |
| 4x | 14 samples |

These are host samples and are independent of the sample rate. If other tracks sound slightly out of time when you change oversampling, confirm plugin delay compensation is enabled in your DAW.

### CPU

CPU cost scales with the number of sounding voices, the oversampling factor, and how many effects are engaged. The oscillators, filter, and FM operators run at the oversampled rate, so 4x costs roughly twice what 2x costs for the same patch; the effects run once at the host rate regardless.

The figures below are single-core measurements taken at 48 kHz with a 512-sample buffer, expressed as a percentage of one CPU core. They come from an offline profile of the engine's processing loop on an Intel Core i7-8809G (4 cores, 3.1 GHz base), so treat them as a guide; a faster or slower machine will scale the numbers accordingly, and a smaller buffer raises the per-block overhead slightly.

| Patch | Oversampling | CPU (one core) |
|---|---|---|
| 8-voice Prism FM with drive, chorus, delay, and reverb | 4x | about 50% |
| 6-voice Cosmos with 8x unison, dual chorus, and all effects | 2x | about 22% |
| 4-voice Oracle pad with reverb | 2x | about 15% |
| Acid sequence with drive, delay, and reverb | 2x | about 3% |

The first row is close to the worst case the instrument can produce: a full 8-voice FM patch, every effect on, at the highest oversampling factor. Even there it uses about half of one core, so a typical session runs several instances comfortably. If you need more headroom, drop from 4x to 2x oversampling first, since that is where most of the voice cost lives, then thin out effects you are not using.

## Factory Presets

Sunset Circuits ships with 54 factory presets, grouped below by mode.

### Cosmos (six-voice DCO poly)

- **Neon Nights**: warm detuned DCO pad with dual chorus and a long release.
- **Glass Highway**: bright chorused arpeggio with a touch of delay.
- **Velvet Fog**: soft, dark, vintage pad for beds and atmospheres.
- **Sunset Strip**: wide, heavily detuned pad with full dual chorus.
- **Crystal Rain**: sparkly fast arpeggio through delay and reverb.
- **Midnight Drive**: driven chorused pad with tape delay, cinematic and wide.
- **Starfield**: octave-spanning sparkle arpeggio with delay and reverb.
- **Warm Keys**: mellow triangle-and-sine keys with single chorus.
- **Arp Factory**: swung up-down arpeggio across three octaves.
- **Aurora Drift**: huge slow-swelling pad, full chorus, very wide.
- **Init Cosmos**: a clean starting point for the Cosmos engine.

### Oracle (five-voice poly, poly-mod)

- **Brass Section**: poly-mod brass with filter-envelope bite.
- **Wooden Keys**: mellow plucked keys, short and woody.
- **Poly Mod Bells**: inharmonic bells driven by oscillator-to-oscillator poly-mod.
- **Dark Oracle**: dark sustained pad with vintage character.
- **Stab Machine**: short percussive poly stab.
- **Upside Down**: the flagship Up/Down latched saw arpeggio, the famous 80s sci-fi title-sequence sound out of the box.
- **Poly Brass**: fuller poly-mod brass ensemble.
- **Glass Bells**: bright long-decay bells with poly-mod and reverb.
- **Analog Strings**: four-voice unison string pad with chorus.
- **Fat Fifth**: unison fifths, wide and thick.
- **Regal Brass**: self-oscillating poly-mod brass with two-voice unison.
- **Init Oracle**: a clean starting point for the Oracle engine.

### Mono (aggressive monophonic)

- **Pulsing Darkness**: pulsing arpeggiated sub-bass, dark and vintage.
- **Acid Squelch**: resonant squelching bass line with portamento.
- **Screaming Lead**: loud driven lead with delay.
- **Sub Thunder**: deep sine-and-sub sub-bass.
- **Sync Sweep**: ring-modulated hard-sync sweep.
- **Acid Machine**: glide acid arpeggio, high resonance.
- **Thunder Sub**: square-sub sub-bass, tuned low.
- **Wobble Bass**: LFO-to-cutoff wobble bass.
- **Tape Lead**: glide lead through tape delay.
- **Siren Lead**: hard-sync screaming lead, driven, with synced delay and glide.
- **Init Mono**: a clean starting point for the Mono engine.

### Modular (semi-modular)

- **Sci-Fi Computer**: random FM arpeggio with hard sync.
- **Horror Drone**: slow evolving ring-modulated drone with a long reverb tail.
- **Voltage Ghost**: FM and sync sweep drone with tape delay.
- **Retro Sequence**: tape-delay sequence with filter-envelope movement.
- **Voltage Seq**: sample-and-hold sequence across two octaves.
- **Alien Transmission**: ring, FM, and sync texture with a long reverb.
- **Drone Machine**: huge slow FM drone with LFO cutoff movement.
- **Noise Sweep**: filtered-noise sweep for risers and effects.
- **Nebula Static**: sample-and-hold texture modulating cutoff and resonance, with spring and hall reverb.
- **Init Modular**: a clean starting point for the Modular engine.

### Prism (four-operator FM)

- **Glass Keys**: dual-stack tine electric piano, the tine rolling off up the keyboard.
- **Solid Bass**: serial FM bass with a touch of feedback grit and a filter-envelope pluck.
- **Crystal Bells**: additive inharmonic bells with staggered decays and a reverb tail.
- **Brass Machine**: serial FM brass with strong feedback growl and an attack swell.
- **Glass Cathedral**: dual-stack FM pad with a long swell, chorus, and big reverb.

### Acid (bass box and pattern sequencer)

- **Silver Squelch**: classic high-resonance saw line, accents and slides, rolling 1/16 pattern.
- **Rubber Bass**: square wave, low resonance, tight fast decay for a round rubbery bounce.
- **Night Crawler**: slow dark 1/8 pattern drenched in long slides.
- **Screamer**: near-self-oscillating resonance, maxed accent, drive on, the aggressive scream.
- **Neon Sequence**: resonant saw line with a ping-pong tempo-synced delay groove.

## Troubleshooting

**A note is stuck on.** Sunset Circuits guards against stuck notes: when the arpeggiator switches between its host-locked and free-running clocks, or when you start or stop the transport, any sounding note is released cleanly. If you ever hear a hung note, sending an all-notes-off from your DAW (or stopping and starting the transport) clears it, but this should not happen in normal use.

**Switching modes did not change the sound the way I expected.** Switching the mode by hand keeps all other parameters where they were, so the new circuit plays with your old settings. Load a preset for that mode, or dial in the mode-specific controls, to hear the circuit as intended. Loading a preset always sets the mode along with everything else.

**A mode-specific control disappeared.** Controls that do not apply to the current mode are hidden, not disabled. For example, the poly-mod knobs only appear in Oracle, and the operator strips only appear in Prism. The parameters still exist for automation; only the on-screen widgets change per mode.

**The synced delay is not tracking a very slow tempo.** The delay buffer is 2 seconds. At slow tempi the longest note divisions ask for more than 2 seconds and are clamped to the buffer, so they hold at 2 seconds instead of tracking further. Use a shorter division or the free (milliseconds) mode if you need the delay to follow a very slow tempo exactly.

**Bright or high notes sound harsh or buzzy.** That is aliasing. Raise the Oversampling from 1x to 2x or 4x. FM tones in Prism and high-resonance filter sweeps benefit the most.

**The interface looks small on a high-resolution display.** The UI scales with the window: drag the window corner to resize it and the whole panel scales to fit while keeping its aspect ratio. On a HiDPI display the font atlas is rebuilt at the base scale, so extreme zoom levels can look slightly soft; resizing the window to a comfortable size gives the crispest text. A font-atlas rebuild for live rescaling is planned for a future release.

**My host shows extra latency.** Oversampling at 2x or 4x reports a small fixed latency (12 or 14 samples) so the host can compensate. Confirm plugin delay compensation is enabled in your DAW if other tracks sound out of sync.
