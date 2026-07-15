---
slug: tapemachine-2
version: 1.0.0
last_updated: 2026-07-15
tagline: tracking and mastering tape color in one processor
---

# TapeMachine 2

## Overview

TapeMachine 2 combines two tape-deck models in one stereo effect. **Swiss**
provides tracking and mix coloration, while **American** provides a mastering
deck topology with additional transport and electronics options. The machine,
speed, tape formulation, equalization, bias, and signal path all change the
modeled recording and playback chain.

The factory bank contains 20 calibrated presets. They are useful starting
points, but Input, Bias, transport modulation, filters, and the Advanced
reproduce EQ remain fully editable.

TapeMachine 2 has a distinct plugin ID from TapeMachine 1.x, so both can be
installed for old-session compatibility.

## Quick start

1. Insert TapeMachine 2 on a stereo track or bus.
2. Choose **Classic Rock Crisp** for restrained Swiss color or **Big 456
   Master** for American mastering color.
3. Leave **Gain Link** on and raise **Input**. The linked Output control moves in
   the opposite direction so louder is not mistaken for better.
4. Compare with the top-right **Bypass** button.
5. For stronger color, try **Thick Saturation**, **Drum Bus**, or **Fat 456
   Master**. For deliberate degradation, try **Sunbaked Cassette** or **Old
   Tape**.

## Machine and tape controls

### Machine

- **Swiss** is the tracking and mix model. It supports 7.5, 15,
  and 30 IPS.
- **American** is the mastering model. It adds 3.75 IPS, head
  width, crosstalk, a master wow/flutter switch, and transformer switching.

### Tape speed

- **3.75 IPS** is American only; it has narrow bandwidth and the strongest
  slow-speed character.
- **7.5 IPS** gives pronounced head and bandwidth character.
- **15 IPS** is the general-purpose reference speed.
- **30 IPS** gives tighter low-frequency behavior and extended bandwidth.

### Tape type

- **456**: classic general-purpose formulation.
- **GP9**: high-output formulation.
- **900**: clean modern formulation.
- **250**: older, more colored formulation.

### Signal path

- **Repro**: complete record/tape/reproduce path.
- **Sync**: playback through the sync-head geometry.
- **Input**: electronics and EQ without tape saturation, modulation, or noise.
- **Thru**: direct signal path for comparison.

### EQ and bias

- **EQ Standard** selects NAB or CCIR.
- **Auto Bias** derives bias from the current tape and speed. Turn it off to use
  the Bias knob manually.

## Main controls

### Gain staging

- **Input** ranges from -12 to +12 dB and is the primary drive control.
- **Output** ranges from -12 to +12 dB.
- **Gain Link** applies inverse output compensation. With it on, Output acts as
  an additional makeup trim on top of the inverse Input gain.

### Tape character

- **Bias** ranges from 0 to 100%. It is inactive while Auto Bias is on.
- **Noise** controls the modeled deck-specific hiss and hum. Zero disables it.

### Transport

- **Wow** controls slow pitch drift.
- **Flutter** controls faster pitch variation.
- American also has a **W&F** master switch that gates both controls.

### Filters

- **Highpass** ranges from 20 to 500 Hz; 20 Hz displays as Off.
- **Lowpass** ranges from 3 to 20 kHz; 20 kHz displays as Off.

The nonlinear core is fixed at its fitted 2× oversampling mode. There is no
user-facing oversampling selector in TapeMachine 2.

## Advanced panel

Click **Advanced** to open the reproduce-head controls:

- **Low**: 80 Hz shelf, ±12 dB
- **Lo-Mid**: 160 Hz peak, ±12 dB
- **Hi-Mid**: 5 kHz peak, ±12 dB
- **High**: 9 kHz shelf, ±12 dB

On American, the panel also exposes:

- **Crosstalk**: adjacent-track bleed modeling
- **XFMR**: output-transformer low-frequency and even-harmonic character

These controls are part of the factory-preset fits. Resetting them to zero can
substantially change a preset's match and saturation reading.

## Factory presets

### American mastering

- Big 456 Master
- Nice 456 Master
- Jazz Vision Master
- Clean 900 Master

### American color

- Fat 456 Master
- GP9 Drum Bus
- Massive Bass
- Bright & Sizzly

### Swiss mix

- Classic Rock Crisp
- Modern Rock
- Drum Bus
- Hi-Fi Shine
- Lush Film
- Jazz Warmth

### Swiss color

- Thick Saturation
- Hip-Hop Punch
- Vocal Presence

### Lo-fi

- Sunbaked Cassette: American at 3.75 IPS
- Analog Warmth: American at 3.75 IPS
- Old Tape: Swiss at 7.5 IPS

The preset menu is split into American and Swiss columns. **Init** restores
defaults. **Save** stores user presets in
`~/.config/DuskAudio/TapeMachine2/presets`.

## Practical workflows

### Transparent mix color

Start with **Classic Rock Crisp** or **Modern Rock**. Leave wow, flutter, and
noise at zero. Keep Gain Link on, then adjust Input while level-matched. Switch
to 30 IPS if you want less low-speed character.

### Drum-bus saturation

Start with **Drum Bus**, **GP9 Drum Bus**, or **Fat 456 Master**. Preserve the
factory Advanced EQ at first. Adjust Input for density, then use Output only as
the final makeup trim.

### Strong tape color

Start with **Thick Saturation**. Its Input and Hi-Mid reproduce settings were
jointly fitted: changing one affects both frequency response and measured THD.
Use small moves and level-match before deciding.

### Lo-fi treatment

Start with **Sunbaked Cassette** or **Old Tape**. Raise Wow, Flutter, or Noise for
more instability; lower the Lowpass for a narrower result. Sunbaked uses the
American-only 3.75 IPS mode.

## Troubleshooting

**Input gets louder but Output turns down.** Gain Link is on. This is intentional
level compensation. Turn it off for independent input and output gains.

**Bias does not respond.** Auto Bias is on. Disable it before adjusting Bias.

**Wow and Flutter do nothing on American.** Enable the W&F master switch.

**I cannot select 3.75 IPS on Swiss.** That speed is American-only.

**I cannot find oversampling.** TapeMachine 2 is permanently tuned at 2×. The
legacy parameter is hidden and retained only for session-state compatibility.

**The factory preset changed after zeroing Advanced EQ.** The four reproduce EQ
values are intentional parts of each fitted preset.

## Formats and requirements

- macOS 10.15 or newer: AU, VST3, CLAP, LV2; universal arm64/x86_64
- Linux x86_64: VST3, CLAP, LV2
- Windows and Standalone builds are not currently shipped

Production macOS releases are Developer ID signed and Apple-notarized. Release
archives include GPL and third-party license notices plus SHA-256 checksums.
