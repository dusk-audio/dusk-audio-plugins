# Sunset Circuits, next release

## Opening an older project

A project recalls its program first, then restores the knob values you saved. This preserves your edits. It also means a project saved with a factory sound uses its old settings with the updated circuits. To hear the revised factory sound, select the preset again. Save a copy first if you want to keep your edits.

In migration tests, Poly Brass played 1.09 dB louder and sounded brighter with the old settings. Brass Section gained 0.44 dB, Poly Mod Bells lost its metallic attack, and Noise Sweep gained 2.52 dB. Your own patches can also change with Oracle's new sync direction, Poly-Mod response, white noise and deeper bass, the louder Mono and Modular noise mixes, or the new oversampling rates.

## Oracle

- **Sync.** Oscillator 2 now restarts oscillator 1. For vibrato on a synced patch, route it to oscillator 2. Modulating oscillator 1 sweeps the tone while oscillator 2 holds the note. Oscillator 1 has no independent semitone control, so an older sync patch can need retuning by ear. Oracle Trumpet now routes its vibrato to oscillator 2. In Mono and Modular, oscillator 1 still restarts oscillator 2.
- **Poly-Mod.** The filter envelope raises oscillator 1 by the same musical interval on any note, up to 33.9 semitones. Oscillator 2 can bend it by up to 7.1 semitones each way. The envelope now narrows oscillator 1's pulse. Oscillator 2 swings its width about twice as far as before. Either route can close the pulse. The two cutoff routes reach 14.3 octaves from the envelope and 3.1 octaves either way from oscillator 2. To approximate an older filter sweep, start at 28 percent of its previous envelope amount or 97 percent of its oscillator 2 amount. The old pitch settings have no single equivalent across the keyboard.
- **Cross Mod.** The Poly-Mod panel now has a separate knob for linear frequency modulation. Set it to 50 percent for Poly Mod Bells or 60 percent for Glass Bells to recreate their metallic attack. Both bell presets use it. Brass Section, Dark Oracle, Stab Machine, Poly Brass, Regal Brass and Oracle Trumpet also have revised Poly-Mod settings.
- **Noise and bass.** Oracle now mixes in white noise without turning down the oscillators. Against the old pink noise at the same setting, the noise measured about 19 dB lower at 31 Hz, 4.5 dB lower at 1 kHz and 4 dB higher at 8 kHz. Noise level stays consistent across sample rates. Separately, the filter and stereo output now pass more low bass and still remove DC. In a pulse-closure test the tail fell to about 37 percent of its starting level in 31 ms. Bass-heavy patches can produce higher peaks.
- **Wheel Mod.** Open WHEEL MOD beside MOD MATRIX. One free-running LFO and one noise source move all voices together. Mix saw, triangle or square shapes with noise or one random step per cycle. The wheel moves between your lower and upper depth limits. Switch on any combination of oscillator 1 or 2 pitch, oscillator 1 or 2 pulse width, and filter cutoff. All five destinations start off. Older projects open with Wheel Mod off.
- **Front panel.** The black Oracle panel has white outlines and labels, red lamps and a walnut frame. Routing arrows now display their direction, including OSC 2 -> OSC 1 under Sync.

## Oscillators, noise and processing

Cosmos, Oracle, Mono and Modular use minimum-phase smoothing on oscillator edges at lower internal rates. Hard-sync restarts also land at the point within a sample where the master wraps. With Oracle playing a steady C7 (MIDI 96, three octaves above middle C) at 2x and 48 kHz, the strongest unwanted tone measured between -109.0 and -91.7 dBc across five waveforms. On Oracle Trumpet with the wheel up, a different test measured energy outside the moving harmonics at -66.6, -69.2 and -71.0 dB on C6, G6 and C7.

Oracle, Mono and Modular also filter the oscillator mix before the voice filter. The pre-filter uses four poles at 18 kHz from an internal rate of 88.2 kHz and two poles at 20 kHz from 176.4 kHz. With the filter wide open, the earlier 18 kHz comparison measured 0.7 dB less at 15 kHz and 3 dB less at 18.8 kHz. Oracle's oscillator 2 and Modular's oscillator 3 cutoff routes also limit the highest harmonics driving the filter. The shared voice filter takes less CPU, and the voices are laid out more efficiently in memory.

Mono and Modular keep pink noise. Raising it now leaves the oscillators at their set levels, so older mixes with noise can play louder. Sub Thunder, Alien Transmission, Noise Sweep and Modular English Horn have adjusted factory levels.

Oversampling now targets an internal rate. At 44.1 or 48 kHz, 1x, 2x and 4x still multiply the session rate. At 88.2 or 96 kHz, 1x and 2x both run at the session rate and 4x runs at twice that rate. Older projects at these rates can sound different at the same setting. At 176.4 or 192 kHz, all three run at the session rate. Reported latency stays at 13 host samples in every mode and setting.

The Analog control's slow pitch drift keeps its depth and follows the speed it had at 2x in a 48 kHz session. At 48 kHz the wander is about twice as fast as before at 1x and half as fast at 4x.

## Factory presets

The bank now has 57 sounds. Oracle Trumpet is a hard-synced brass lead, Modular English Horn is a reedy lead, and Prism Talking Keys is a four-operator FM patch with a vowel-like attack. Prism Talking Keys is the final name for the patch originally called Prism Talking Clav. All three retain their original 4x setting.
