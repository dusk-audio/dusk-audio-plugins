# TapeMachine

**Professional analog tape emulation for your DAW**

TapeMachine brings the warmth, saturation, and character of classic reel-to-reel tape machines to your productions. Calibrated against real Studer A800 and Ampex ATR-102 hardware measurements, it delivers authentic tape sound from subtle warmth to full saturation.

## Features

### Two Classic Machines
- **Swiss800** (Studer A800 MkIII): Clean, punchy, transformerless design with tight low end and extended highs. The studio workhorse.
- **Classic102** (Ampex ATR-102): Rich transformer coloration with pronounced head bump and vintage character. The mastering legend.

### Four Tape Formulations
- **Type 456**: Warm and punchy — perfect for rock, pop, and mix bus
- **GP9**: Clean with extended headroom — ideal for mastering and classical
- **Type 911**: European character with early saturation — great for warmth
- **Type 250**: Vintage saturation — perfect for lo-fi and creative effects

### Signal Path Modes
- **Repro**: Full tape processing (the classic sound)
- **Sync**: Record head playback with extra HF rolloff (for overdub workflows)
- **Input**: Electronics only — hear just the transformers and EQ coloration
- **Thru**: Clean bypass for A/B comparison

### Professional Controls
- **Input/Output Gain** with automatic level compensation
- **Saturation** control for dialing in the perfect amount of tape warmth
- **Bias** adjustment for fine-tuning the tape response
- **Wow & Flutter** for authentic pitch modulation (or creative wobble)
- **Tape Noise** with adjustable amount and vintage-style enable switch
- **High-pass/Low-pass filters** for shaping your sound
- **Mix** control for parallel processing

### 15 Factory Presets
Professionally designed starting points across five categories:
- **Subtle**: Gentle Warmth, Transparent Glue, Mastering Touch
- **Warm**: Classic Analog, Vintage Warmth, Tube Console
- **Character**: 70s Rock, Tape Saturation, Cassette Deck
- **Lo-Fi**: Lo-Fi Warble, Worn Tape, Dusty Reel
- **Mastering**: Master Bus Glue, Analog Sheen, Vintage Master

### Quality Options
- **Tape Speed**: 7.5, 15, and 30 IPS with appropriate EQ curves
- **EQ Standard**: NAB (American), CCIR (European), or AES (modern)
- **Oversampling**: 1x, 2x, or 4x for pristine anti-aliasing

### Vintage Interface
- Animated tape reels that respond to playback
- Dual VU meters with authentic ballistics
- Premium dark theme with metal textures

## System Requirements

- **Formats**: VST3, LV2, AU (macOS), Standalone
- **Platforms**: Linux, macOS, Windows
- **Linux**: glibc 2.31+ (Ubuntu 20.04+, Debian 11+, Fedora 34+)

## Installation

### Linux
Copy to your plugin folders:
- VST3: `~/.vst3/TapeMachine.vst3`
- LV2: `~/.lv2/TapeMachine.lv2`

### macOS
- VST3: `/Library/Audio/Plug-Ins/VST3/`
- AU: `/Library/Audio/Plug-Ins/Components/`

### Windows
- VST3: `C:\Program Files\Common Files\VST3\`

---

## Technical Details

For those interested in what's under the hood:

### Signal Chain (20 stages)
1. Input metering and gain staging
2. Input transformer saturation (Classic102 only)
3. Pre-emphasis EQ (NAB/CCIR/AES record curve)
4. Bias filter and pre-saturation limiting
5. **3-band Langevin waveshaper** — physically-modeled tape saturation
6. Gap loss and HF rolloff
7. Wow & Flutter modulation
8. Head bump resonance
9. Playback head response
10. De-emphasis EQ (complementary playback curve)
11. Phase smearing (analog electronics modeling)
12. Output transformer (Classic102 only)
13. Tape noise (pink noise + modulation)
14. DC blocking and anti-aliasing

### Saturation Model
The core uses the **Langevin function** from magnetic hysteresis theory, providing physically-accurate odd-harmonic distortion. Even harmonics come from transformer asymmetry modeling (Classic102 only).

- 3-band processing at 200Hz and 5kHz crossovers
- Calibrated to match published Studer/Ampex THD measurements
- Pade [2,2] approximation for CPU efficiency

### Harmonic Targets (calibrated against real hardware)

| Machine | H2 | H3 | THD @ 0VU |
|---------|-----|-----|-----------|
| Swiss800 (Studer) | negligible | -50 to -54dB | ~0.3% |
| Classic102 (Ampex) | -52 to -58dB | -46 to -50dB | ~0.5% |

### EQ Standards

| Standard | Character |
|----------|-----------|
| **NAB** | American standard — most HF pre-emphasis, warmest saturation character |
| **CCIR** | European standard — moderate emphasis, balanced response |
| **AES** | Modern standard — minimal emphasis, most transparent |

---

## Credits

- Developed with the JUCE Framework
- DSP based on Jiles-Atherton magnetic hysteresis theory
- Pink noise algorithm by Paul Kellett

**Dusk Audio**
