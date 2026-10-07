---
slug: ring-out
version: 0.1.0
last_updated: 2026-10-06
tagline: feedback eliminator for ringing out monitors and PA
---

# Ring Out User Manual

## Overview

Ring Out finds the frequencies at which a sound system starts to feed back and notches them out. You insert it on a monitor send or the master bus, arm it, and push the gain up the way you would when ringing out by hand; every time a tone starts to build, Ring Out places a narrow cut at exactly that frequency, deep enough to stop it, and moves on to the next one. After a handful of filters you take the gain back down and have a few dB more headroom before the system rings.

It shines at exactly that job: the pre-show ring-out of wedges, in-ears driven from a floor mic, and a PA in a lively room. Twenty filters, each with its own cut, frequency and Q, can also be placed and adjusted by hand, so it doubles as a surgical notch EQ.

It is not a tool to leave hunting during the show. The detector tells feedback from complex programme well, but it cannot tell a ringing wedge from a guitarist's deliberate sustain, and a filter placed on the wrong thing stays until you remove it.

## Quick Start

Install from the Dusk Audio website and insert Ring Out as the last thing on the monitor send (or on the master bus for front of house). Set the channel, aux and amplifier gains the way the show will run them.

Three controls to learn first:

1. **SETUP** arms the detector for one minute. The button turns red and the bar at the bottom of the window drains while it listens.
2. **GAIN OUT** is how you provoke the system. With SETUP armed, raise it slowly. When a frequency starts to ring you will see a peak climb on the analyser and, a fraction of a second later, a red notch appear under it and the ringing stop.
3. **RESET** clears every filter if you want to start again.

Stop after four to six filters, bring GAIN OUT back to 0 dB, and talk into the microphone. The system now sits a few dB further from feedback than before, and the notches are narrow enough that the tone of the mix is barely changed.

## Workflows

### Ringing out a vocal wedge

Source: a dynamic vocal microphone on a stand, feeding a floor wedge through an aux send, singer not yet on stage.

Target: the wedge loud enough for the singer to hear, with the usual three to five ring frequencies removed.

Settings: SENSE Low, GAIN OUT 0 dB, no filters. Press SETUP. Raise GAIN OUT about 1 dB every two seconds. Each time a filter appears, pause a moment and keep going. After five or six filters, or when the detector has been quiet for a while at a level well beyond where you will run the show, press SETUP again to stop, and return GAIN OUT to 0 dB.

Why: the slow raise lets each tone build on its own, so each filter lands on one frequency instead of the detector having to pick apart several ringing at once.

### Adding one more filter during line check

Source: a system already rung out, but one frequency starts to sing when the singer cups the microphone.

Target: one extra notch, nothing else touched.

Settings: hold **ADD** while the singer repeats the move. ADD searches for as long as you hold it and stops the moment it has placed or deepened one filter; the button turns green to tell you. Release it. If the tone persists, hold ADD again: a second pass on the same frequency deepens the existing filter by 3 dB rather than placing a duplicate.

Why: ADD never runs away. One press, one filter, and nothing is placed on the band's downbeat.

### Pulling a notch by hand

Source: a room resonance you already know, say 125 Hz in a small club.

Target: a 6 dB cut at 125 Hz, Q 4, before anything rings.

Settings: press **NEW**. The new filter arrives at 24 Hz, 0 dB, Q 2.5 and is selected (yellow ring on its indicator, yellow cursor on the graph). Set FREQ to 125 with the + and - ends or by double-clicking the read-out and typing 125. Set CUT to -6.0 and Q to 4.0 the same way.

Why: a filter placed by hand behaves exactly like one the detector placed; the engine will deepen it later if that frequency rings anyway.

### Trading gain for depth with LINK

Source: a rung-out system, show about to start, the band wants the wedges 3 dB louder.

Target: 3 dB more wedge level with the same margin against feedback.

Settings: switch **LINK** on. Turn GAIN OUT up to +3.0 dB; AMP follows to -3.0 dB, so every filter cuts 3 dB deeper at the same time. Both knobs show a red rim while they are away from their defaults.

Why: the feedback margin you built lives in the notches. If the whole system goes up 3 dB, the notches have to go down 3 dB to keep the loop gain where it was.

### Checking the damage

Source: a mix that sounds thinner than before the ring-out.

Target: find the filter that hurts and soften it.

Settings: use the < and > buttons to step through the filters; the yellow cursor shows each one on the graph and the CUT / FREQ / Q read-outs show its values. A filter in the 200 Hz to 500 Hz region with a deep cut and a low Q is the usual culprit. Raise its Q a step or two, or reduce its cut by a few dB, and listen. Or switch it off with **ON** to compare, and double-click its indicator to switch it off from the indicator row.

Why: the detector errs on the side of stopping the ringing. Once the pressure is off, a narrower or shallower notch often still holds.

## Parameter Reference

### Toolbar

**SENSE**, Low or High, default Low. How readily the detector calls a tone feedback. Low needs the tone to stand clearly above everything else, have no harmonics and persist for about 130 ms; High accepts quieter and shorter tones. Use Low for ringing out before the show; use High on a system that only rings briefly before someone pulls the fader. Common mistake: High on a stage with sustained synth pads, which places filters on the pads.

**SETUP**, press to arm, press again to stop. Arms the detector for 60 seconds, after which it switches itself off. Arming from the editor also puts GLOBAL Q and AMP back to their defaults, so the filters you are about to place are judged at face value; a host/controller trigger leaves those trims unchanged. The armed state is never saved with a session: a project you reload is always idle. Common mistake: leaving it armed during the show.

**ADD**, momentary. Searches while held, stops after one filter has been placed or deepened. Release and press again for another. From a mapped controller a tap starts the same search; it ends with its filter or after ten seconds, so a button that sticks cannot keep it hunting.

**RESET** removes every filter.

**< >** select the previous or next filter. Clicking a filter's dot on the graph also selects it.

### Filter controls

**Indicators 1 to 20** light cyan for a filter that is on, show a dim ring for one that exists but is off, and carry a yellow ring on the selected filter. Double-click an indicator to switch that filter off (or on again). **NEW** adds a filter; **DEL** removes the selected one and the indicators to its right move left to close the gap.

**ON** switches the selected filter in or out.

**CUT**, -20 to 0 dB in 0.1 dB steps, default 0 dB. The depth of the selected notch. Deeper is not better: 6 to 8 dB stops most ringing, and 20 dB with a wide Q removes a noticeable slice of the mix.

**FREQ**, 24 Hz to 20 kHz, default 24 Hz. The + and - ends step by 1 Hz below 500 Hz, 10 Hz to 1 kHz and 100 Hz above. A filter the detector placed shows its exact frequency (66.3 Hz, for example); stepping from there keeps that precision.

**Q**, 0.5 to 20 in 0.1 steps, default 2.5. Higher is narrower. The detector places filters between Q 3 and Q 8 and only widens them if a tone keeps ringing through a 20 dB cut.

### Global

**Q**, 0.2 to 10, default 1.0. Multiplies the Q of every filter; 0.5 makes every notch twice as wide. Red rim when not at 1.0. Reset when SETUP is armed from the editor.

**AMP**, -24 to +24 dB, default 0 dB. Added to every filter's cut. Negative deepens all notches, positive shallows them; a filter never turns into a boost, so +24 dB simply flattens everything. Red rim when not at 0. Reset when SETUP is armed from the editor.

**LINK**, on or off. Ties AMP to GAIN OUT in opposite directions when you turn either knob in the editor: raise GAIN OUT by 3 dB and AMP drops by 3 dB. Host automation of one knob moves only that knob.

**GAIN OUT**, -24 to +24 dB, default 0 dB. The output trim, and the control you raise to provoke the system during setup.

### Meters and graph

The **IN** and **OUT** meters read peak level from -80 to 0 dBFS with a peak-hold mark; click a meter to reset its hold. The graph shows the input analyser in cyan against the left-hand dB scale and the combined response of all filters in red against the right-hand scale, which runs from 0 to -24 dB.

## Tips and Traps

Raise the gain slowly while SETUP listens. Two frequencies ringing at once still get caught, but one at a time gives cleaner filters.

A tone that keeps ringing after a filter has been placed is not ignored: the detector deepens that filter in 3 dB steps to -20 dB and then widens it toward Q 0.7. If you find a filter at -20 dB and a low Q after a session, the system was a long way over the edge at that frequency; look at the gain structure or the microphone placement rather than asking the plugin for more.

More than about twelve filters, or filters at -16 dB and beyond, mean the same thing. With all twenty filters in use the detector will still deepen a filter whose band a new ring falls into, or widen the nearest one toward a ring just outside it, but it will not touch an unrelated one; NEW is greyed out. When a ring keeps going and the detector has nothing left to do about it, the status line reads RING NOT COVERED while it is listening: the filters it could use are at their limits, and the fix is in the gain structure.

The detector ignores tones with harmonics, tones that decay, and tones that are themselves a harmonic of something louder. A pure test oscillator held steady will be treated as feedback; so will a flute note held long enough. Stop SETUP before a soundcheck starts in earnest.

Ring Out adds no latency and runs the same filters on both channels of a stereo insert.

The filter table is saved with your session and in user presets, so a ring-out done at soundcheck survives a project reload.

## Presets Explained

**Default** is the only factory preset: every control at its default and no filters. It is also what INIT loads. The header arrows step through the preset list without wrapping and never reload the preset already shown, and they do nothing while an edited, unsaved table is on screen; choosing Default from the list, or INIT, is how you deliberately clear a ring-out.

User presets, saved with SAVE in the header, hold the five settings (SENSE, GLOBAL Q, AMP, LINK, GAIN OUT) and the complete filter table. Save one per venue or per wedge mix and recall it at the next show as a starting point; a quick SETUP pass then catches whatever the room does differently that night.

## Troubleshooting

**SETUP is armed but nothing is being placed.** The system is not ringing yet, or it rings so briefly that Low sensitivity will not commit. Raise GAIN OUT further, or try SENSE High. Also check that the plugin is in the path that feeds back: it has to be between the microphone and the loudspeaker.

**Filters appear on the music, not on feedback.** SENSE is on High during the performance, or SETUP was left armed. Switch to Low, stop SETUP, remove the stray filters with DEL, and use ADD held for a moment when you actually hear a ring.

**The ringing came back after a session.** Something changed the loop gain: a microphone moved, a monitor was turned up, a fader was pushed. Hold ADD while it rings to deepen the existing filter, or run SETUP again for a minute. If filters sit at -20 dB and the ringing persists, the problem is the gain structure, not the filters.
