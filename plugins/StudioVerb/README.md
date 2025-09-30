# Studio Verb - Professional Reverb Plugin

**Developed by Luna CO. Audio**

A high-quality reverb processor featuring four distinct algorithms: Room, Hall, Plate, and Early Reflections. Built with JUCE framework for cross-platform compatibility.

## Features

### 🎛️ Four Reverb Algorithms

#### Room
- Simulates a medium-sized room (5-10m)
- 8-12 comb filters with shorter delays
- High density with quick buildup
- Warm, enclosed feel perfect for drums and vocals

#### Hall
- Simulates a large concert hall (20-50m)
- 12-16 comb filters with longer delays
- Smooth, ethereal wash with extended decay
- Ideal for orchestral and ambient music

#### Plate
- Emulates classic metal plate reverb
- Bright, metallic character with modulation
- Dense sustaining tail with shimmer
- Perfect for vocals and lead instruments

#### Early Reflections
- Focused on initial wall bounces
- No diffuse tail, just discrete reflections
- Quick decay (300ms max)
- Great for adding space without muddiness

### 🎚️ Controls

- **Size** (0-1): Controls overall delay scaling and room dimensions
- **Damping** (0-1): High-frequency damping (20kHz to 500Hz)
- **Predelay** (0-200ms): Delay before reverb onset
- **Mix** (0-100%): Dry/wet balance

### 📦 Preset System

20 factory presets included:

**Room Presets:**
- Small Office
- Living Room
- Conference Room
- Studio Live
- Drum Room

**Hall Presets:**
- Small Hall
- Concert Hall
- Cathedral
- Theater
- Arena

**Plate Presets:**
- Bright Plate
- Vintage Plate
- Shimmer Plate
- Dark Plate
- Studio Plate

**Early Reflections Presets:**
- Tight Slap
- Medium Bounce
- Distant Echo
- Ambience
- Pre-Verb

## Technical Details

### DSP Architecture

- **Comb Filter Cascade**: Up to 64 taps using JUCE dsp::DelayLine
- **Diffusion Network**: 4-8 allpass filters in series
- **Stereo Processing**: Independent L/R processing with slight offsets
- **Soft Limiting**: Output capped at 0.99 with soft limiter
- **Thread-Safe**: No allocations in audio thread
- **Efficient Processing**: Optimized for real-time performance

### Specifications

- Sample rates: 44.1kHz - 192kHz
- Bit depth: 32-bit float internal processing
- Latency: < 1ms (plus predelay)
- CPU usage: ~5-10% on modern processors

## Building from Source

### Requirements

- CMake 3.15 or higher
- C++17 compatible compiler
- JUCE 7.0.9 or higher

### Build Instructions

#### Linux/macOS:
```bash
chmod +x build.sh
./build.sh
```

#### Windows:
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
```

### Installation

**VST3:**
- Copy `build/StudioVerb_artefacts/VST3/StudioVerb.vst3` to your VST3 folder

**AU (macOS only):**
- Copy `build/StudioVerb_artefacts/AU/StudioVerb.component` to `~/Library/Audio/Plug-Ins/Components/`

**Standalone:**
- Run the executable in `build/StudioVerb_artefacts/Standalone/`

## License

Copyright © 2024 Luna CO. Audio. All rights reserved.

This software is proprietary and confidential. Unauthorized copying, modification, or distribution is strictly prohibited.

## Support

For support, feature requests, or bug reports, please contact:
- Email: support@lunaco.audio
- Website: https://lunaco.audio

## Credits

Developed by Luna CO. Audio engineering team.

Special thanks to the JUCE framework and the audio development community.

---

*Studio Verb - Bringing professional reverb to your productions*