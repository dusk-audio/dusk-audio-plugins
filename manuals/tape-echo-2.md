---
slug: tape-echo-2
version: 1.0.0
last_updated: 2026-08-08
tagline: three-head tape delay with spring reverb
---

# Tape Echo 2 User Manual

Version 1.0.0

Tape Echo 2 is a three-head tape-delay and spring-reverb effect. It models the
mechanical timing, record and playback coloration, regeneration, tape wear,
splice dropout, input/output amplifiers, and independent echo and spring
returns of a classic hardware echo workflow.

## Signal flow

The stereo input is summed to the modeled record and spring paths. Input drives
both paths. Record Input interrupts the tape-record feed while the spring
remains live, which makes dub-style echo tails and reverb-only operation
possible. The selected playback heads feed both the echo output and the
regeneration loop. Bass and Treble affect only the echo return. Echo and spring
then receive their own level and pan settings. Mix crossfades the direct path
against the combined echo and spring returns before Output applies the final
stereo trim.

The VU and peak lamp monitor the record path after Input and include
regeneration. They remain active when Record Input is off and go dark when
Power is off.

Control names in this manual are the legends printed on the panel. Seven host
automation parameters carry a different name from the legend that drives them,
because those names were fixed by the 0.1 series and are kept so old projects
keep loading; each one is noted in parentheses where the control is described.

## Controls

### Echo section

- **Head Select** (automation name: Mode) chooses one of 12 head/spring
  routings:
  1. Head 1
  2. Head 2
  3. Head 3
  4. Heads 2 + 3
  5. Head 1 + Reverb
  6. Head 2 + Reverb
  7. Head 3 + Reverb
  8. Heads 1 + 2 + Reverb
  9. Heads 2 + 3 + Reverb
  10. Heads 1 + 3 + Reverb
  11. Heads 1 + 2 + 3 + Reverb
  12. Reverb Only
- **Repeat Rate** controls tape speed. Head 1 spans approximately 177 ms at
  the slow end to 69 ms at the fast end. The corresponding Head 2 range is
  337 to 131 ms and Head 3 is 489 to 189 ms.
- **Intensity** controls regeneration. High values can self-oscillate and become
  substantially louder than the input.
- **Echo Level** (automation name: Echo Volume) sets echo-return level.
- **Reverb Level** (automation name: Reverb Volume) sets spring-return level in
  modes 5 through 12.
- **Bass** and **Treble** shape the echo return without changing the dry or
  spring paths.

### Record and transport section

- **Input** (automation name: Input Volume) sets record/spring send gain and
  drives the modeled input amplifier and tape nonlinearity.
- **Record Input** (automation name: Input Send) toggles new material into the
  tape loop. Switching it off lets existing repeats decay while the dry and
  spring paths continue.
- **Tape Age** selects **New**, **Used**, or **Old** tape. New retains a very
  quiet tape/electronics bed; older states progressively add bandwidth loss,
  noise, transport variation, level wear, and a more audible tape splice.
- **Wow & Flutter** extends the intrinsic transport movement from subtle
  mechanical motion to a creative wobble range.
- **Power** is the host-designated bypass and uses a click-free transition.

### Tempo and mix section

- **Tempo Sync** derives delay from the host tempo. The rhythmic value belongs
  to the leading active head in the selected mode. Head 1 spans roughly 70 to
  178 ms, and a note longer than the tape can physically carry is held at that
  maximum rather than transposed, exactly as the hardware behaves. Longer
  divisions therefore converge on the same endpoint timing at a given tempo,
  and slow tempos reach the limit sooner. Use a shorter division, or a
  multi-head mode, when you need the repeat to track the bar at low tempos.
- While Tempo Sync is enabled, the **Repeat Rate** knob becomes the note
  selector (automation name: Echo Rate Note). It keeps its panel legend and
  steps through eleven detents instead of sweeping continuously. Each detent
  selects a rhythmic value for the leading active head, so the available values
  change with Head Select. The three head displays show the exact `d` (dotted),
  `t` (triplet),
  `+`/`-`, and out-of-range blinking indications used by the reference unit.
  Changing Head Select keeps the knob at the same detent and updates its note
  assignment. The older Sync Division parameter remains hidden so projects and
  automation saved by the 0.1 series continue to load correctly.
- **Mix** crossfades the direct signal against the combined echo and spring
  returns. At 0% the output is dry-only, 50% preserves both paths at unity, and
  100% is fully wet for send/return use.
- **Echo Pan** and **Reverb Pan** place the two wet returns independently.
- **Output** (automation name: Output Volume) provides −20 dB to +20 dB after
  the complete mix; its midpoint is unity gain.

## Factory presets

| Preset | Purpose |
| --- | --- |
| Default | Neutral Head 1 starting point |
| Slapback Vocal | Short, full-level vocal slap |
| Rockabilly Guitar | Bright single-head rockabilly echo |
| Classic Tape Echo | Medium Head 2 echo |
| Dub Throw | Long feedback throw with a dark return |
| Synced 1/8 Dub | Host-synced Head 2 dub echo |
| Multi-Head Bounce | Heads 2 + 3 with spring and right-panned echo |
| Orbital Echo | Host-synced Heads 1 + 2 with split wet panning |
| Full Wash | Old-tape three-head and spring wash |
| Ambient Trails | Old-tape Head 3 and spring ambience |
| Worn Tape | Old-tape pitch and tone character |
| Runaway Drone | High-feedback Heads 1 + 3 texture |
| Spring Only | Head Select 12 (Reverb Only) spring-reverb return |

Factory programs never change host bypass or restore meter outputs. The two synced programs use host tempo; all other programs use the
stored free-time rate.

## Host and format notes

Tape Echo 2 is available as VST3, CLAP, LV2, and Audio Unit on supported
platforms. It accepts mono or stereo program material and produces stereo
output. Tempo Sync requires a host that supplies a valid tempo; 120 BPM is used
as a safe fallback when tempo information is unavailable.

By default, macOS builds are ad-hoc signed and are not notarized. The first time
a downloaded build is loaded, Gatekeeper may report that it cannot verify the
developer. Approve the plugin once under System Settings > Privacy & Security
("Open Anyway"), or remove the quarantine attribute from the installed bundle
in Terminal, for example: `xattr -dr com.apple.quarantine
~/Library/Audio/Plug-Ins/Components/tape_echo.component`. Rescan or restart the
host afterward; Logic Pro users should re-run the Audio Unit scan.

When `MACOS_SIGNING_ENABLED=true` and the required Apple signing and
notarization secrets are configured, the release workflow instead produces
Developer ID signed and notarized AU, VST3, and CLAP bundles. The LV2 bundle is
Developer ID signed but is not submitted for notarization.

Parameter IDs from the 0.1 series are preserved. Mix was appended after all
previously shipped IDs. The hidden Dry Level parameter retains its original
meaning for old project state and automation. Tape Age snaps stored continuous
values to the nearest New/Used/Old state.

## Gain and safety

Self-oscillation is an intentional part of the model and can exceed 0 dBFS.
Lower Intensity or Echo Level before changing modes on loud material, and use
Output for final gain staging. Power clears the modeled loop so stale
regeneration does not return when the effect is enabled again.

## License and support

Tape Echo 2 is released under GPL-3.0-or-later. Third-party component notices
are included with release archives. Support and source information are
available at <https://dusk-audio.github.io/>.
