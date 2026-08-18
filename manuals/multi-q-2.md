---
slug: multi-q-2
title: Multi-Q 2
version: 1.0.0
last_updated: 2026-08-18
tagline: Four-personality EQ with dynamic bands, Match mode, and console color
---

# Multi-Q 2

## Overview

Multi-Q 2 is four EQs behind one switch. The EQ Type control picks between
**Digital** (an 8-band parametric with per-band dynamics and per-band
saturation), **Match** (learn a reference spectrum, learn your current source,
and compute a correction curve), **British** (a four-band console EQ with Brown
and Black voicings, shared with 4K EQ 2), and **Tube** (a passive tube-style EQ
with the classic boost-and-cut interaction). A real-time spectrum analyzer sits
behind the curve display in every mode.

Use Digital for surgical and dynamic frequency work, Match for tonal matching
between mixes or takes, British for broad console-style shaping with
saturation, and Tube for vintage low-end weight and air. It is not a
linear-phase mastering EQ and not a de-noiser; the built-in limiter is a safety
ceiling, not a loudness maximizer.

Multi-Q 2 is the DPF successor to the JUCE Multi-Q. All 190 host parameters
match the original in name, order, and range, and the Digital path nulls
against it bit-exactly at 1x, so existing ears carry over even though sessions
from the JUCE build do not load into it.

## Quick Start

1. Insert Multi-Q 2 and pick an EQ Type in the header: **Digital**, **Match**,
   **British**, or **Tube**.
2. In Digital mode, drag a band handle on the graph: horizontal sets frequency,
   vertical sets gain. A value bubble follows the drag.
3. Double-click an empty spot on the graph to grab the nearest spectrum peak
   with a band. Right-click a handle for shape, routing, and dynamics options.
   Alt-drag a handle to reset it.
4. Toggle **BYPASS** in the header to A/B against the dry signal, and use the
   **A**/**B** button for two full parameter banks you can flip between.
5. The analyzer runs by default. **PRE**/**POST** switches the tap;
   **FREEZE** holds the trace while you compare.

You should hear the tonal balance move as soon as a band leaves 0 dB. If a
drag changes nothing audible, check that the band is enabled and that its Q is
not so narrow that a small gain move disappears.

## Workflows

### Digital: removing a snare ring

**Source:** A snare with a ringy resonance near 380 Hz.
**Goal:** Kill the ring, keep the crack.

- EQ Type: Digital
- Band 4 handle to 380 Hz
- Shape: Peaking (right-click for Notch if the ring survives)
- Gain: -8 dB
- Q: 8

To find the exact frequency, boost +8 dB first, sweep until the ring jumps
out, then flip the gain negative. The narrow Q keeps the cut from thinning
the body of the drum. The Q range runs all the way to 100 for true
notch-width surgery.

### Digital: dynamic de-essing

**Source:** A vocal whose sibilance spikes between 6 and 8 kHz.
**Goal:** Tame the esses only when they happen.

- EQ Type: Digital
- Band 6 to 7 kHz, Peaking, Gain 0 dB, Q 2
- Right-click the band, enable **Dynamics**
- Threshold: -18 dB
- Attack: 5 ms
- Release: 80 ms
- Range: 6 dB
- Ratio: 4:1

The static gain stays at zero, so the band is inaudible until sibilance
crosses the threshold; then the band ducks by up to 6 dB at the set ratio.
The factory program "De-Esser" is this exact recipe. Switch Dynamics
Detection to RMS when the trigger feels too jumpy on dense material.

### Match: making a mix sit like the reference

**Source:** Your mix bus, plus a reference track you can route through the
plugin.
**Goal:** Move your mix's tonal balance toward the reference.

1. EQ Type: **Match**.
2. Play the reference through the plugin and press **LEARN REFERENCE**; stop
   after 10 to 20 seconds of representative audio.
3. Play your own mix and press **LEARN CURRENT** for a similar stretch.
4. Press **MATCH**. The correction curve appears on the graph.
5. Set **Apply** to taste; 100% is the full correction, and 30 to 60% is
   usually plenty. Raise **Smoothing** (default 12 semitones) if the curve
   looks jagged, and engage **LIMIT +** or **LIMIT -** to cap extreme boosts
   or cuts.

Match computes a static correction, not a mastering chain. If the corrected
mix pumps or sounds over-bright at 100%, lower Apply rather than re-learning.

### British: vocal sweetening with console color

**Source:** A lead vocal that needs presence without surgery.
**Goal:** Warmth, clarity, and a little saturation glue.

- EQ Type: British, Mode: Brown
- HPF: On, 80 Hz
- LM: -1.5 dB at 400 Hz, Q 0.8
- HM: +3 dB at 3 kHz, Q 1.2
- HF: +1 dB at 8 kHz, shelf
- Saturation: 20%

This is the "Console Vocal Chain" factory program. Brown weights the
saturation toward the low end for body; Black is the tighter, cleaner
voicing, and its proportional Q narrows as you push gain harder. The filter
engine is shared with 4K EQ 2, so moves you know from there translate
directly.

### Tube: bass weight with the boost-and-cut trick

**Source:** Bass guitar that needs fullness and definition at once.
**Goal:** More low end that does not turn to mud.

- EQ Type: Tube
- LF Boost: 6 at 60 Hz
- LF Atten: 4
- HF Boost: 3 at 5 kHz, Bandwidth 0.5
- Tube Drive: 0.3

Boosting and attenuating the same low frequency is the classic passive-EQ
move: the two curves interact to lift the fundamental while dipping the mud
just above it. The "Vintage Bass Trick" program ships these settings. Tube
Drive past 0.5 adds obvious harmonic thickening; keep it near 0.3 for warmth
without hair.

## Parameter Reference

### Header and global

- **EQ Type:** Digital, Match, British, or Tube; default Digital. Each type
  keeps its own parameter set; switching is a hard switch, not a translation.
- **A/B:** Two full parameter banks for comparison. The banks swap everything
  except EQ Type, so a compare always stays within the current personality.
  Banks live for the session; they are not saved with presets.
- **Bypass:** Host-visible bypass, default off, click-free.
- **Master Gain:** -24 to +24 dB, default 0. Final output trim.
- **Auto Gain:** Off by default. Estimates loudness compensation from the
  curve. Handy while auditioning; leave it off for prints you will match by
  hand.
- **Oversampling:** Off, 2x, or 4x, default Off. Reduces aliasing from the
  saturation stages in all modes at a CPU and latency cost. The common
  mistake is leaving 4x on for a plain clean cut, where it buys nothing.
- **Processing Mode:** Stereo, Left, Right, Mid, or Side; default Stereo.
  Applies the whole EQ to one channel component. Per-band Routing below is
  the finer tool.
- **Q-Couple Mode:** Off through Proportional, Light, Medium, Strong, three
  Asymmetric variants, and Vintage; default Off. Narrows Q automatically as
  gain grows, in the chosen flavor.
- **Limiter:** Enable plus Ceiling from -1 to 0 dB. A safety brickwall on the
  output; watch its gain-reduction readout in the header. It is not a
  loudness tool.

### Digital mode (bands 1 to 8)

Band 1 is a high-pass and band 8 a low-pass; both offer slopes of 6, 12, 18,
24, 36, 48, 72, or 96 dB/oct (default 12) and no gain control. Bands 2
through 7 are the shaping bands. Every band has:

- **Frequency:** 20 Hz to 20 kHz, log-scaled drag.
- **Gain** (bands 2 to 7): -24 to +24 dB, default 0.
- **Q:** 0.1 to 100, default 0.71.
- **Shape** (bands 2 to 7): band 2 offers Low Shelf, Peaking, or High Pass;
  band 7 offers High Shelf, Peaking, or Low Pass; bands 3 to 6 offer Peaking,
  Notch, Band Pass, or Tilt Shelf.
- **Routing:** Global, Stereo, Left, Right, Mid, or Side per band, plus a
  per-band **Pan** and **Invert**/**Phase Invert**. Mid/Side per band is how
  the M/S factory programs are built.
- **Saturation:** Off, Tape, Tube, Console, or FET per band with a 0 to 1
  Drive (default 0.3). Saturates only that band's range; a common mistake is
  stacking several saturated bands and reading the color as "the EQ sounding
  wrong".
- **Dynamics:** per band: Threshold -48 to 0 dB (default -20), Attack 0.1 to
  500 ms (default 10), Release 10 to 5000 ms (default 100), Range 0 to 24 dB
  (default 12), Ratio 1:1 to 100:1 (default 4). **Dynamics Detection** picks
  Peak or RMS globally.

### Match mode

- **LEARN CURRENT / LEARN REFERENCE:** Capture averaged spectra while audio
  plays; press again to stop. **MATCH** computes the correction; **CLEAR**
  discards captures.
- **Apply:** -100 to +100%, default 100. Negative values invert the
  correction.
- **Smoothing:** 1 to 24 semitones, default 12. Higher is broader and safer.
- **Limit Boost / Limit Cut:** Caps the correction's extremes.

### British mode

- **HPF:** 20 to 500 Hz with enable. **LPF:** 3 to 20 kHz with enable.
- **LF:** +/-20 dB, 30 to 480 Hz, shelf by default with a Bell switch.
- **LM:** +/-20 dB, 200 Hz to 2.5 kHz, Q 0.4 to 4.
- **HM:** +/-20 dB, 600 Hz to 7 kHz, Q 0.4 to 4.
- **HF:** +/-20 dB, 1.5 to 16 kHz, shelf by default with a Bell switch.
- **Mode:** Brown or Black. **Saturation:** 0 to 100%. **Input/Output:**
  +/-12 dB. Drive Input into the saturation, trim Output to compare fairly.

### Tube mode

- **LF Boost:** 0 to 10 at 20, 30, 60, or 100 Hz. **LF Atten:** 0 to 10 at
  the same frequency; running both is the point.
- **HF Boost:** 0 to 10 at 3, 4, 5, 8, 10, 12, or 16 kHz with a 0 to 1
  Bandwidth. **HF Atten:** 0 to 10 at 5, 10, or 20 kHz.
- **Mid section:** Low Peak (0.2 to 1 kHz), Dip (0.2 to 2 kHz), and High
  Peak (1.5 to 5 kHz), each 0 to 10.
- **Tube Drive:** 0 to 1, default 0.3. **Input/Output:** +/-12 dB.

### Analyzer and display

- **Analyzer:** On by default; **PRE/POST** tap select; Peak or RMS mode;
  resolution 2048, 4096, or 8192; smoothing Off to Heavy; decay 3 to 60 dB/s
  (default 20). **FREEZE** holds the current trace.
- **Display Scale:** +/-12, +/-24, +/-30, +/-60 dB, or Warped.
  **Visualize Master Gain** folds the master trim into the drawn curve.

## Tips and Traps

- **Modes do not share settings.** A boost made in Digital is absent in
  British. The A/B banks respect this: they never swap the EQ Type.
- **Right-click is half the plugin.** Shape, routing, saturation, and
  dynamics all live in the band context menu. Alt-drag resets a band.
- **Per-band saturation is per band.** Its drive only colors that band's
  region, and every saturated band adds CPU; oversampling multiplies that
  cost. 4x on six saturated bands is an expensive way to mix.
- **The boost-and-cut interaction is Tube-only.** In Digital or British the
  same move mostly cancels.
- **Match learns what you feed it.** Learning eight seconds of chorus against
  a quiet verse produces a lopsided correction. Learn comparable sections,
  and lower Apply before reaching for re-learning.
- **The analyzer is a meter, not a judge.** PRE/POST and FREEZE are for
  confirming what you hear, not replacing it.

## Presets Explained

Multi-Q 2 ships 69 factory programs: 46 Digital, 13 British, and 10 Tube.
Loading a British or Tube program also switches the EQ Type, so a single
click gets you the whole sound. Themes worth knowing:

- **Vocals:** "Vocal Presence", "Vocal De-Mud", and "Broadcast Vocal" in
  Digital; "Console Vocal Chain" and "Vocal Channel" in British; "Warm Vocal
  (Tube)" for the passive-EQ flavor. Pick by character, then adjust the
  presence band first.
- **Drums and bass:** "Punchy Kick", "Snare Crack", "Overhead Clarity", and
  "Bass Definition" in Digital; "Rock Drums" and "Drum Bus Punch" in
  British; "Vintage Bass Trick" and "Bass Thickener" in Tube.
- **Mastering:** "Mastering Surgical", "Mastering Air", "Mastering Wide"
  (Mid/Side routing), and "Gentle Smile" in Digital; "Mix Bus Glue" in
  British; "Gentle Master (Tube)" and "Mastering Sheen" in Tube.
- **Dynamic programs:** "De-Esser", "Dynamic Low Cut", "Presence Ducker",
  "Dynamic Resonance Tamer", and "Multiband Compress" demonstrate per-band
  dynamics; open one and right-click its active bands to see the wiring.
- **Creative:** "Telephone Effect", "Lo-Fi Warmth", and "M/S Stereo Width"
  are effects rather than starting points.

To study any program, load it and inspect the bands it enables; the layout
is the technique.

## Troubleshooting

**No audible change in any mode.** Check Bypass, then confirm a band is
enabled and away from 0 dB. In Digital mode the band handles show enabled
state; a disabled band's handle is dimmed.

**Match produces a wild curve.** The two captures were probably unequal in
level or length. CLEAR, re-learn both with comparable material, raise
Smoothing, and start with Apply near 50%.

**Sessions from the JUCE Multi-Q do not load.** By design: Multi-Q 2 is a
separate plugin with its own identity so both can coexist. Keep the JUCE
build installed for old sessions; recreate keepers by hand (the parameters
line up one-to-one).

**CPU spikes when switching oversampling.** 2x and 4x rebuild the processing
chain and raise latency; switch while the transport is stopped, and avoid
automating the oversampling choice.
