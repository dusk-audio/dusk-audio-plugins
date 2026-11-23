# DuskVerb 2, next release

Entries for the next release go here, above the released ones. The version number is set when that release is made.

## Released as 1.0.3 on 2026-11-23

- Windows VST3: a project saved with the first Windows build, 1.0.0, finds DuskVerb 2 again. 1.0.1 changed the Windows VST3 class ID to the string Linux and macOS show, and such a project opened with the plugin reported as not available. The plugin now declares the 1.0.0 ID an older ID of itself (VST3's plugin compatibility information), and a host that reads it loads the project with its settings. Checked in REAPER 7.78 on Windows with a project saved by 1.0.0 and one saved by 1.0.2: both open with their settings and no warning.
- A host that does not read the compatibility information still reports DuskVerb 2 as missing in a project saved with 1.0.0 and does not load its settings. If 1.0.0 is still installed, open the project with it before updating and save each instance's settings as a user preset; after the update insert DuskVerb 2 and load the preset. If 1.0.0 is gone and no such preset was saved, the settings in that project cannot be brought back in that host: insert DuskVerb 2 and set it up again.
- The class ID itself does not change in this release: `2046504473616C63764473446B737544` on every platform, as in 1.0.1 and 1.0.2. Projects saved with those versions, on any platform, load as before. The 1.0.0 Windows ID was `445046206173636C764473446B737544`. CLAP, AU and LV2 are not affected.

## Released as 1.0.2 on 2026-11-22

- Nonlinear engine: Decay sets the length of the burst up to 2.5 s. In 1.0.1 the burst stopped getting longer at 783 ms while the Decay control and its display went on up to 30 s, so the burst seemed to be cut off at about 0.8 s. Above 2.5 s the burst stays at 2.5 s, and the decay readout and the host's parameter display show 2.50 s (an LV2 host's own display shows the knob position from 0 to 1, not a time). Settings up to 783 ms sound exactly as before.
- A project saved with 1.0.1 on the Nonlinear engine with Decay above 783 ms played that 783 ms burst and now plays the Decay it shows; set Decay to 783 ms to get the old length back (when typing it, include the unit). Recorded Decay automation above 783 ms likewise now plays what it says.
- The longer bursts cost more. Measured on the development machine, the Nonlinear engine at Decay 2.5 s needs about 3 times the processing of the 1.0.1 burst at its longest. While Decay is moving it needs about 1.8 times its own cost at rest, because the old and the new burst play together while each change settles, and for the 50 ms of a crossfade between two Nonlinear presets both bursts play. Settings up to 783 ms cost what they did. Every instance, whichever engine it plays, now reserves about 8.7 MB for the Nonlinear engine at 48 kHz and 33 MB at 192 kHz (3.3 MB and 12 MB in 1.0.1).
- Five factory presets are retuned. At their own Dry / Wet setting each plays within about 0.3 dB of its 1.0.1 loudness; the other 19 presets are unchanged (the Linux build renders the same samples as 1.0.1). Measured on the reverb alone:
  - Vocal Plate: a touch more air, about 0.2 dB from 8 to 11 kHz.
  - Vintage Vocal Plate: less sizzle, about 1.4 dB less around 11 kHz.
  - Medium Drum Room: less low-mid weight (about 1 to 2 dB from 125 Hz to 2 kHz), a little more presence from 4 to 8 kHz and a softer top at 16 kHz. The reverb is about 0.8 dB quieter.
  - Vocal Hall: Low Cut moves from 33 Hz to 29 Hz.
  - Deep Blue Day: darker and a little narrower, with a shorter pre-delay: about 2 dB less at 11 kHz and 4.6 dB less at 16 kHz (High Cut 19.1 kHz to 14.3 kHz with its shelf at -13.4 dB, Treble Multiply 1.00x to 0.96x), Pre-Delay 25 ms to 20 ms, Width 107% to 97%.
- A project saved with 1.0.1 on Vocal Plate, Vintage Vocal Plate or Medium Drum Room plays the retuned sound. A project on Vocal Hall or Deep Blue Day keeps the settings it saved and sounds as before; select the preset again to get the retuned version.
- Lower CPU on four presets: Reverse Taps needs about a quarter of its 1.0.1 processing, 79 Vocal Chamber about 40% less, Medium Drum Room and Vocal Hall about a third less. Measured on the development machine at 48 kHz in 128-sample blocks; no preset got slower. The CPU work itself changes no sound: Reverse Taps and 79 Vocal Chamber play exactly as in 1.0.1, and Medium Drum Room and Vocal Hall change only by the retuning above.
- The supporters window (click the DuskVerb title) has larger text and closes on any click. In 1.0.1 a click over a greyed-out control, as on the Nonlinear engine, did not close it.
