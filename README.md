# Luna Co. Audio Plugins

A collection of professional audio VST3/LV2 plugins built with JUCE.

## Plugins

### 4K EQ
SSL 4000 Series Console EQ emulation featuring:
- 4-band parametric EQ (LF, LMF, HMF, HF) with SSL-style colored knobs
- High-pass and low-pass filters
- Brown/Black variants (E-Series/G-Series console emulation)
- Advanced SSL saturation modeling
- 2x/4x oversampling for anti-aliasing
- Collapsible frequency response graph
- A/B comparison

### Universal Compressor
Multi-mode compressor with seven classic hardware emulations:
- Vintage Opto (LA-2A style)
- Vintage FET (1176 Bluestripe)
- Classic VCA (DBX 160)
- Vintage VCA/Bus (SSL G-Series)
- Studio FET (1176 Rev E Blackface)
- Studio VCA (Focusrite Red 3)
- Digital (Transparent) - Clean digital compression with lookahead

Features: Sidechain HP filter, auto-makeup gain, parallel mix, 2x oversampling.

### TapeMachine
Analog tape machine emulation featuring:
- Swiss800 (Studer A800) and Classic102 (Ampex ATR-102) models
- Multiple tape types and speeds (7.5, 15, 30 IPS)
- Advanced saturation and hysteresis modeling
- Wow & flutter simulation
- Dual stereo VU meters with animated reels

### Plate Reverb
High-quality plate reverb based on the Dattorro algorithm:
- Size, decay, and damping controls
- Stereo width control
- Mono-in, stereo-out architecture

### Vintage Tape Echo
Classic tape echo/delay emulation:
- 12 operation modes
- Wow & flutter simulation
- Spring reverb
- Tape age modeling

### DrummerClone
Logic Pro Drummer-inspired intelligent MIDI drum pattern generator:
- Follow Mode with real-time groove analysis
- 12+ virtual drummer personalities
- Section-aware patterns and intelligent fills
- MIDI CC control for DAW automation
- MIDI export functionality

### Harmonic Generator
Analog-style harmonic saturation processor:
- Individual harmonic controls (2nd-5th)
- Hardware saturation modes
- 2x oversampling

## Building

### Prerequisites
- CMake 3.15+
- C++17 compatible compiler
- JUCE framework

### Build All Plugins
```bash
./rebuild_all.sh              # Standard build
./rebuild_all.sh --fast       # Use ccache and ninja if available
./rebuild_all.sh --debug      # Debug build
```

### Build Individual Plugin
```bash
cd build
cmake --build . --target FourKEQ_All
cmake --build . --target UniversalCompressor_All
cmake --build . --target TapeMachine_All
cmake --build . --target PlateReverb_All
cmake --build . --target TapeEcho_All
cmake --build . --target DrummerClone_All
```

### Installation Paths
- **VST3**: `~/.vst3/`
- **LV2**: `~/.lv2/`

## License

See individual plugin directories for licensing information.

---
*Luna Co. Audio*
