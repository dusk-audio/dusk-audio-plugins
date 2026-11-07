# Multi-Comp 2 1.0.0: release notes (draft)

Draft of 2026-11-06 for the owner to review, then paste into the GitHub release and the Patreon post. Everything
below the line is written to be pasted as it stands; a line marked [check] needs a fact confirmed at release time.

Sources: the branch `mc2/release-1.0.0` (`git log origin/main..HEAD`), DAF main through #109 (c74776b4), and for the
measured differences the 2026-11-03 baseline (tools repo, `handoff/reports/mc2-instrument-audit-phase3-2026-11-03.md`;
`parity-playbook.md`). The DSP changes since that baseline (bypass at activation, the switch duck) leave every render
without a switch unchanged (commit 1bea349e), so its counts stand for 1.0.0.

---

Multi-Comp 2 1.0.0 is the first public release of the successor to Multi-Comp: eight compression modes in one
plugin, each with its own face. Opto, FET Black, FET Blue, VCA, Bus and Studio VCA are matched to measurements of
reference units; Digital and Multiband are our own designs. Multi-Comp 2 has its own plugin identity, so it installs
next to the original Multi-Comp without replacing it. Thank you to everyone who tried the three alphas and sent notes.

### What's new since alpha 3

- **Settings drawer.** SETTINGS, at the right end of the new mode row, opens a drawer with the 18 settings that
  were only reachable from your host's automation list: External Key, SC Listen and the sidechain EQ; Oversampling,
  True Peak and its quality, and Analog Noise; Stereo Link and Link Mode; Drive and its amount; the Lookahead for
  Digital and Multiband; and FET Black's Threshold, Transient and Curve. A setting the current mode does not use
  stays in place, greyed out. Choosing a mode closes the drawer.
- **Every face laid out again, for reading.** All eight faces are redrawn inside the same 1120 x 380 window, so that
  at that size every control legend is at least 16 px and every value 17 px. The header has a mode row of its own:
  the eight modes, AUTO MAKEUP, BYPASS and SETTINGS. FET Blue takes its reference unit's arrangement, with Headroom
  above Mix between Input and Output, and the VCA meter keys read INPUT, OUTPUT and GR. The save-preset dialog,
  typed values, tooltips and menus now use the faces' typeface at 17 px instead of a 13 px framework font.
- **Every switch fades.** Changing the mode or the Oversampling setting, or pressing or releasing FET Black's All
  button, while audio plays now fades the output out, makes the change in silence and fades it back in, all within
  a few milliseconds. Before, most switches cut straight across: the waveform jumped, entering VCA dropped out for
  about 1.4 ms, entering or leaving Multiband replayed or skipped a little audio, and Oversampling changes clicked.
- **Lookahead moves with the switch.** When a switch changes the latency (a Lookahead set, and Digital or
  Multiband against another mode), the dry, bypassed and sidechain-listen signals fade with it, since their delay
  changes too. A preset or INIT that changes the mode and a Lookahead together no longer steps the output.
- **AU hosts follow latency changes.** The AU version now tells the host when Lookahead changes the latency, so
  hosts that listen for it, Logic among them, no longer keep the old delay compensation until the project is
  reopened.
- **AU input changes during playback.** When the host changes the plugin's inputs while audio runs (connecting a
  sidechain during playback, for example), the AU version plays silence for that moment instead of processing while
  its inputs change.
- **A session saved bypassed starts bypassed.** It used to play about 30 ms of processed audio each time the host
  activated it, and at the start of every bounce.
- **Host text in every language setting.** Hosts that follow a comma-decimal language setting (Qt and GTK hosts on
  Linux do) showed values such as "0,20 ms" or "8, kHz" and could not read their own text back. Host text now always
  uses a decimal point. The FET Black and Bus ratios no longer read "4:1 :1" in VST3 hosts, and the Bus Headroom
  steps carry their unit ("4 dB" to "28 dB").
- **Cleaner automation.** The Opto and FET meter buttons write only what a press changes: a press on the lit button
  used to record an undo step or an automation point, and the FET row rewrote Bypass on every press. A knob that
  automation makes inapplicable while you hold it (Auto Makeup or the external key switching on) stops writing at
  once.
- **VCA's PULL/SC**, pressed in a fresh editor, engages the sidechain filter at 20 Hz. It used 500 Hz, the top of
  the range, which the Opto, Bus and Digital faces then applied after a mode change.
- **The CLAP version reports its version number and home page** to the host; it reported none.
- **The FET factory presets do what their names say.** Vocal Presence, Modern Pop Control, Room Nuke, Rock Bass
  Anchor, Funk Rhythm Guitar and EDM Pump all loaded with slow attack and release dials (about Attack 2, Release 1
  to 2), so Room Nuke and EDM Pump did not pump. They now set the dials they were meant to: Room Nuke at Attack 6,
  Release 7, for example.
- **Closing a plugin window no longer risks a crash in AU hosts.** A host that removed the plugin while keeping its
  window could crash or misbehave a moment later. [check: the DAF fix is merged and in the release build]
- **Digital's lookahead lines up exactly** with the latency the host is told, at 2x and 4x oversampling as well;
  before, the processed signal could sit a fraction of a sample off.
- **The sidechain EQ's low shelf keeps its shape at very high rates** (192 kHz with oversampling, or hosts running
  at 384 kHz and above).

### Sessions

- Sessions saved with any alpha open in 1.0.0.
- Sessions saved in Studio FET with alpha 1 or 2 reopen in FET Black with their FET settings in AU, VST3 and CLAP
  hosts. In LV2 hosts they reopen in FET Blue.
- Studio VCA sessions from alpha 1 and 2 load with Auto Release off, so their Release knob still sets the release.

### Known issues

#### How close the measured modes are

Each measured mode is checked against its reference unit on the same audio at the same settings: test signals that
bring out one behaviour at a time (steady tones, bursts from silence, releases, trains of hits, linked stereo,
distortion) and 30 music clips at many settings. A reading compares the gain reduction Multi-Comp 2 applies with
the reference's, and it counts as a difference when the two are more than 1 dB apart.

On the music clips, every measured mode stays within 1 dB of its reference in every reading but one (FET Blue,
1.15 dB, one reading of 1,800). The differences below show up on the test signals. Readings more than 1 dB apart,
measured on November 3, 2026:

- **VCA:** none (0 of 13,192 test readings at five sample rates, 0 of 270 music readings).
- **Studio VCA:** none (0 of 540 music readings, and no test trace over).
- **Bus:** 2, one moment read on both channels. In the first millisecond of an attack from silence, at 10:1 with
  the 0.1 ms attack, Multi-Comp 2 reduces about 1.4 dB less than its reference. Music: 0 of 1,020.
- **FET Black:** 14. Music: 0 of 1,320.
  - With the All button in, the reference recovers from a heavy hit within about 20 ms, while Multi-Comp 2 still
    holds about 2 dB of gain reduction 50 ms after it (4 readings). After a train of bursts at 12:1 it is 1.02 dB
    off 200 ms after the last one (1 reading).
  - On some test tones, the level of a distortion harmonic differs by more than 1 dB (7 of 156 readings; the worst,
    the fifth harmonic of 100 Hz near the knee, by 3.2 dB).
  - In the first quarter of a millisecond of an attack, two readings are 1.14 and 1.01 dB off.
- **FET Blue:** 13, all one behaviour. Right after a very short hit at the fastest Release settings it holds more
  gain reduction than its reference for a moment: about 1 to 2.5 dB, some 50 ms after a 20 ms hit. Music: 1 of 1,800
  (1.15 dB).
- **Opto:** 103. Music: none of 210.
  - 88 readings on low tones at the Limit setting, almost all with the channels linked and their zero crossings
    apart (the same low tone on both channels with one of them shifted in phase, or a low tone with a mid tone
    above it). Multi-Comp 2 mostly reduces more, by up to 1.7 dB: each of its linked channels lights the opto cell
    at its own zero crossings, and the reference does not double up like that.
  - 7 readings after very short bursts of about 1 ms: Multi-Comp 2 reduces up to 1.9 dB less.
  - 2 readings on the peakiest test signals: up to 1.4 dB less reduction.
  - 4 distortion readings: a harmonic's level differs by up to 1.85 dB.
  - 2 readings just over the line (1.007 and 1.0002 dB).

#### Other known issues

- **LV2 hosts: a factory preset resets the global settings.** A factory preset chosen from an LV2 host's own preset
  list also sets Bypass and the Settings drawer (Oversampling, External Key, Lookahead, Stereo Link and the rest)
  back to their defaults. The plugin's own preset menu, and the AU, VST3 and CLAP versions, leave those settings as
  they are.
- **Lookahead changes the latency.** A Lookahead adds latency in Digital and Multiband, so changing one there, or
  switching between those modes and another mode while one is set, changes the plugin's latency. VST3 and CLAP hosts
  restart the plugin to take the new latency, which resets the compressor for a moment; AU hosts are told and adjust
  their delay compensation. A switch that changes the latency also mutes the bypassed and dry signal briefly, since
  its delay changes too: a few milliseconds between short fades, and up to about 13 ms when switching into Digital
  with its own Lookahead at 10 ms. With both Lookaheads at zero the latency is the same in every mode, so a mode
  switch never restarts the plugin or mutes the bypassed signal.
- **Moving a Lookahead while audio plays can click.** A Lookahead change on its own applies at once instead of
  fading, so that dragging or automating it never mutes the audio. The output can step while it moves.
- **FET Black, Curve set to Measured, All button in:** quiet passages come out about 1.2 dB lower than they should.
  Curve is Modern by default.
- **The FET needles show the true gain reduction.** The FET Black reference unit's own meter is livelier and slams
  to the left on big peaks, so the two needles look different even though they average about the same.
- **VST3 preset names.** In VST3 hosts, a session or preset saved after you changed a factory preset can reopen
  showing a factory preset's name in the host's preset list (the first preset, or the one the plugin was on). The
  sound is what you saved; only the name is off.
- [check: delete this line if the manual ships with 1.0.0] There is no user manual yet.

### Feedback

Report anything that sounds or reads off on GitHub, with the mode, the settings and the material: "FET Blue, 8:1,
fastest Release, on a snare, the gain comes back slower than I expect" is the kind of note that can be acted on.
