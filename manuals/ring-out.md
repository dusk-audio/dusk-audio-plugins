---
slug: ring-out
version: 1.0.0
last_updated: 2026-10-12
tagline: feedback eliminator for ringing out monitors and PA
---

# Ring Out User Manual

## Overview

Ring Out finds the frequencies at which a sound system starts to feed back and notches them out. You insert it on a monitor send or the master bus, arm it, and push the gain up the way you would when ringing out by hand; every time a tone starts to build, Ring Out places a narrow cut at exactly that frequency, deep enough to stop it, and moves on to the next one. After a handful of notches you take the gain back down and have a few dB more headroom before the system rings.

It shines at exactly that job: the pre-show ring-out of wedges, in-ears driven from a floor mic, and a PA in a lively room. Twenty notches, each with its own cut, frequency and Q, can also be placed and adjusted by hand, on the graph or in the notch bank, so it doubles as a surgical notch EQ.

It is not a tool to leave hunting during the show. The detector tells feedback from complex programme well, but it cannot tell a ringing wedge from a guitarist's deliberate sustain, and a notch placed on the wrong thing stays until you remove it.

## Quick Start

Install from the Dusk Audio website and insert Ring Out as the last thing on the monitor send (or on the master bus for front of house). Set the channel, aux and amplifier gains the way the show will run them.

Three controls to learn first:

1. **LEARN** arms the detector for one minute. The round button turns amber, shows the time left, and the ring around it drains while it listens.
2. **OUTPUT** is how you provoke the system. With LEARN armed, raise it slowly. When a frequency starts to ring you will see a peak climb on the analyser and, a fraction of a second later, an amber notch open under it with a ripple around its node, and the ringing stop.
3. **CLEAR** removes every notch if you want to start again.

Stop after four to six notches, bring OUTPUT back to 0 dB, and talk into the microphone. The system now sits a few dB further from feedback than before, and the notches are narrow enough that the tone of the mix is barely changed.

## Workflows

### Ringing out a vocal wedge

Source: a dynamic vocal microphone on a stand, feeding a floor wedge through an aux send, singer not yet on stage.

Target: the wedge loud enough for the singer to hear, with the usual three to five ring frequencies removed.

Settings: SENSITIVITY Low, OUTPUT 0 dB, no notches. Press LEARN. Raise OUTPUT about 1 dB every two seconds. Each time a notch appears, pause a moment and keep going. After five or six notches, or when the detector has been quiet for a while at a level well beyond where you will run the show, press LEARN again to stop, and return OUTPUT to 0 dB.

Why: the slow raise lets each tone build on its own, so each notch lands on one frequency instead of the detector having to pick apart several ringing at once.

### Catching one more ring during line check

Source: a system already rung out, but one frequency starts to sing when the singer cups the microphone.

Target: one extra notch, nothing else touched.

Settings: hold **HOLD TO CATCH** while the singer repeats the move. CATCH searches for as long as you hold it and stops the moment it has placed or deepened one notch; the button turns green to tell you. Release it. If the tone persists, hold CATCH again: a second pass on the same frequency deepens the existing notch by 3 dB rather than placing a duplicate.

Why: CATCH never runs away. One press, one notch, and nothing is placed on the band's downbeat.

### Pulling a notch by hand

Source: a room resonance you already know, say 125 Hz in a small club.

Target: a 6 dB cut at 125 Hz, Q 4, before anything rings.

Settings: click the **+** on the first empty strip of the notch bank. The new notch arrives at 24 Hz, 0 dB, Q 2.5 and is selected: its strip is outlined, its node on the graph wears a white ring and a callout with its values. Drag the node to the right until the callout reads about 125 Hz and down to about -6 dB, then roll the mouse wheel over it until Q reads 4.0. For exact values, use the FREQ, CUT and Q read-outs under the bank: the + and - ends step the value, and a double-click lets you type it.

Why: a notch placed by hand behaves exactly like one the detector placed; the engine will deepen it later if that frequency rings anyway.

### Trading gain for depth with LINK

Source: a rung-out system, show about to start, the band wants the wedges 3 dB louder.

Target: 3 dB more wedge level with the same margin against feedback.

Settings: click the chain between CUT TRIM and OUTPUT so it joins and turns amber (**LINK**). Turn OUTPUT up to +3.0 dB; CUT TRIM follows to -3.0 dB, so every notch cuts 3 dB deeper at the same time. Both read-outs turn amber while they are away from their defaults.

Why: the feedback margin you built lives in the notches. If the whole system goes up 3 dB, the notches have to go down 3 dB to keep the loop gain where it was.

### Checking the damage

Source: a mix that sounds thinner than before the ring-out.

Target: find the notch that hurts and soften it.

Settings: the notch bank shows every notch at once, so scan the faders: a long amber bar is a deep cut. Click a strip, or use the < and > buttons under the bank, to select a notch and see it on the graph. A notch in the 200 Hz to 500 Hz region with a deep cut and a low Q is the usual culprit. Pull its fader up a few dB, or roll the wheel over its node to raise its Q a step or two, and listen. To compare, double-click its number badge (or press ON under the bank) to switch it off and on.

Why: the detector errs on the side of stopping the ringing. Once the pressure is off, a narrower or shallower notch often still holds.

## Parameter Reference

### Detect

**LEARN**, press to arm, press again to stop. Arms the detector for 60 seconds, after which it switches itself off; the amber ring around the button drains over the minute and the button shows the time left. Arming from the editor also puts Q SCALE and CUT TRIM back to their defaults, so the notches you are about to place are judged at face value; a host or controller trigger leaves those trims unchanged. The armed state is never saved with a session: a project you reload is always idle. Common mistake: leaving it armed during the show.

**CATCH** (the HOLD TO CATCH button), momentary. Searches while held, stops after one notch has been placed or deepened, then turns green until you release it. Press again for another. From a mapped controller a tap starts the same search; it ends with its notch or after ten seconds, so a button that sticks cannot keep it hunting.

**SENSITIVITY**, Low or High, default Low. How readily the detector calls a tone feedback. Low needs the tone to stand clearly above everything else, have no harmonics and persist for about 130 ms; High accepts quieter and shorter tones. Use Low for ringing out before the show; use High on a system that only rings briefly before someone pulls the fader. Common mistake: High on a stage with sustained synth pads, which places notches on the pads.

The **status line** under SENSITIVITY says what the detector is doing: LEARNING, CATCH: LISTENING, CATCH: NOTCH PLACED, LEARN ENDED when the minute runs out, or how many notches are in place. RING NOT COVERED shows in red while it listens if a ring keeps going and the detector has nothing left to do about it (see Tips and Traps).

### Notch bank

Twenty strips, one per notch. Each strip shows the notch's number badge (amber when the notch is on, outlined when it is off), its frequency, a **CUT** fader, its cut in dB and its Q. Click a badge or a strip to select the notch; double-click the badge to switch the notch off or on again. Drag a fader to set the cut (hold Shift for fine steps) or roll the mouse wheel over it for 0.5 dB steps. The **+** on the first empty strip adds a notch. **CLEAR**, above the bank, removes every notch.

Under the bank, the selected notch's exact values:

**FREQ**, 24 Hz to 20 kHz, default 24 Hz. The + and - ends step by 1 Hz below 500 Hz, 10 Hz to 1 kHz and 100 Hz above. A notch the detector placed shows its exact frequency (66.3 Hz, for example); stepping from there keeps that precision.

**CUT**, -20 to 0 dB in 0.1 dB steps, default 0 dB. The depth of the notch. Deeper is not better: 6 to 8 dB stops most ringing, and 20 dB with a wide Q removes a noticeable slice of the mix.

**Q**, 0.5 to 20 in 0.1 steps, default 2.5. Higher is narrower. The detector places notches between Q 3 and Q 8 and only widens them if a tone keeps ringing through a 20 dB cut.

Each read-out steps with its + and - ends, drags up and down, and takes a typed value on a double-click. **ON** switches the selected notch in or out; **DELETE** removes it and the strips to its right move left to close the gap; **< >** select the previous or next notch.

### Graph

The graph shows the input analyser in silver against the right-hand dBFS scale, and the combined response of all notches in amber, hanging from its 0 dB line against the left-hand scale, which runs from 0 to -24 dB and extends in 12 dB steps when the notches go deeper, down to -48 dB; anything deeper than that is drawn flat on the floor. Each notch is a numbered node at the bottom of its cut, after CUT TRIM; a switched-off notch sits outlined on the 0 dB line.

The graph is also an editor. Drag a node across to change its frequency and up or down to change its cut (hold Shift for fine steps); a switched-off notch drags in frequency only. A drag rounds the frequency to three significant figures; use the FREQ read-out for anything finer. The scale holds still while you drag, and a node pulled below the floor waits there with a small arrow until you let go. Roll the mouse wheel over a node to change its Q (0.5 per step, 0.1 with Shift); double-click a node to switch it off or on. Clicking a node, or anywhere in a notch's frequency column, selects it, and the selected node shows a callout with its number, frequency, cut and Q; with CUT TRIM set, the callout also gives the depth after the trim. A notch the detector has just placed or deepened ripples amber for a moment, as does its badge in the bank.

### All notches and output

**Q SCALE**, 0.2 to 10, default 1.0. Multiplies the Q of every notch; 0.5 makes every notch twice as wide. Reset when LEARN is armed from the editor.

**CUT TRIM**, -24 to +24 dB, default 0 dB. Added to every notch's cut. Negative deepens all notches, positive shallows them; a notch never turns into a boost, so +24 dB simply flattens everything. Reset when LEARN is armed from the editor.

Both trims show an amber arc from their default to their value and an amber read-out while they are away from the default; untouched, they show no arc.

**LINK**, on or off: the chain between CUT TRIM and OUTPUT, joined and amber when on. Ties CUT TRIM to OUTPUT in opposite directions when you turn either knob in the editor: raise OUTPUT by 3 dB and CUT TRIM drops by 3 dB. Host automation of one knob moves only that knob.

**OUTPUT**, -24 to +24 dB, default 0 dB. The output level, and the control you raise to provoke the system during setup.

The **IN** and **OUT** meters read peak level from -80 to 0 dBFS with a peak-hold mark; click a meter to reset its hold.

## Tips and Traps

Raise the gain slowly while LEARN listens. Two frequencies ringing at once still get caught, but one at a time gives cleaner notches.

A tone that keeps ringing after a notch has been placed is not ignored: the detector deepens that notch in 3 dB steps to -20 dB and then widens it toward Q 0.7. If you find a notch at -20 dB and a low Q after a session, the system was a long way over the edge at that frequency; look at the gain structure or the microphone placement rather than asking the plugin for more.

More than about twelve notches, or notches at -16 dB and beyond, mean the same thing. With all twenty notches in use the detector will still deepen a notch whose band a new ring falls into, or widen the nearest one toward a ring just outside it, but it will not touch an unrelated one, and the bank has no + left. When a ring keeps going and the detector has nothing left to do about it, the status line shows RING NOT COVERED in red while it is listening: the notches it could use are at their limits, and the fix is in the gain structure.

The detector ignores tones with harmonics, tones that decay, and tones that are themselves a harmonic of something louder. A pure test oscillator held steady will be treated as feedback; so will a flute note held long enough. Stop LEARN before a soundcheck starts in earnest.

Ring Out adds no latency and runs the same notches on both channels of a stereo insert.

The notch table is saved with your session and in user presets, so a ring-out done at soundcheck survives a project reload.

## Presets Explained

**Default** is the only factory preset: every control at its default and no notches. It is also what INIT loads. The header arrows step through the preset list without wrapping and never reload the preset already shown, and they do nothing while an edited, unsaved table is on screen; choosing Default from the list, or INIT, is how you deliberately clear a ring-out.

User presets, saved with SAVE in the header, hold the five settings (SENSITIVITY, Q SCALE, CUT TRIM, LINK, OUTPUT) and the complete notch table. Save one per venue or per wedge mix and recall it at the next show as a starting point; a quick LEARN pass then catches whatever the room does differently that night.

## Troubleshooting

**LEARN is armed but nothing is being placed.** The system is not ringing yet, or it rings so briefly that Low sensitivity will not commit. Raise OUTPUT further, or try SENSITIVITY High. Also check that the plugin is in the path that feeds back: it has to be between the microphone and the loudspeaker.

**Notches appear on the music, not on feedback.** SENSITIVITY is on High during the performance, or LEARN was left armed. Switch to Low, stop LEARN, remove the stray notches with DELETE, and hold CATCH for a moment when you actually hear a ring.

**The ringing came back after a session.** Something changed the loop gain: a microphone moved, a monitor was turned up, a fader was pushed. Hold CATCH while it rings to deepen the existing notch, or run LEARN again for a minute. If notches sit at -20 dB and the ringing persists, the problem is the gain structure, not the notches.

**Automation lanes have new names.** Ring Out 1.1 renamed its parameters to match the editor: Sense is now Sensitivity, Setup is Learn, Add is Catch, Reset is Clear, Global Q is Q Scale, Global Amp is Cut Trim and Gain Out is Output. Existing sessions and automation load unchanged; only the names your host shows are different.
