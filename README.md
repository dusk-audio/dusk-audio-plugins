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

### Multi-Comp
Multi-mode compressor with seven classic hardware emulations plus 4-band multiband compression:

**Compression Modes:**
- Vintage Opto (LA-2A style)
- Vintage FET (1176 Bluestripe)
- Classic VCA (DBX 160)
- Bus Compressor (SSL G-Series)
- Studio FET (1176 Rev E Blackface)
- Studio VCA (Focusrite Red 3)
- Digital (Transparent)
- Multiband (4-band with adjustable crossovers)

**Features:** Sidechain HP filter, auto-makeup gain, parallel mix, 2x/4x oversampling, per-band solo.

### TapeMachine
Analog tape machine emulation featuring:
- Swiss800 (Studer A800) and Classic102 (Ampex ATR-102) models
- Four tape formulations: Type 456, GP9, Type 911, Type 250
- Tape speeds: 7.5, 15, 30 IPS
- Advanced saturation and hysteresis modeling
- Separate Wow & Flutter controls
- 15 factory presets across 5 categories (Subtle, Warm, Character, Lo-Fi, Mastering)
- Dual stereo VU meters with animated reels
- 2x/4x oversampling for alias-free processing

### SilkVerb
Lexicon/Valhalla-style algorithmic reverb:
- Three modes: Plate, Room, Hall
- FDN architecture with Hadamard matrix mixing
- Two-band frequency-dependent decay
- Complex modulation (3 LFOs + random noise)
- Feedback saturation for analog warmth

### Convolution Reverb
Zero-latency IR-based reverb:
- Supports WAV, AIFF, AIFC, SDIR (Logic Pro) impulse responses
- Waveform display
- Size, pre-delay, damping, width, mix controls

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

### Recommended: Docker/Podman Build
For consistent, distributable binaries:
```bash
# Build all plugins
./docker/build_release.sh

# Build a single plugin
./docker/build_release.sh silkverb     # SilkVerb
./docker/build_release.sh convolution  # Convolution Reverb
./docker/build_release.sh 4keq         # 4K EQ
./docker/build_release.sh compressor   # Multi-Comp
./docker/build_release.sh tape         # TapeMachine
./docker/build_release.sh echo         # Vintage Tape Echo
./docker/build_release.sh drummer      # DrummerClone
./docker/build_release.sh harmonic     # Harmonic Generator

# Show all available shortcuts
./docker/build_release.sh --help
```

### Local Development Build
```bash
./rebuild_all.sh              # Standard build
./rebuild_all.sh --fast       # Use ccache and ninja if available
./rebuild_all.sh --debug      # Debug build
```

### Build Individual Plugin
```bash
cd build
cmake --build . --target FourKEQ_All
cmake --build . --target MultiComp_All
cmake --build . --target TapeMachine_All
cmake --build . --target SilkVerb_All
cmake --build . --target ConvolutionReverb_All
cmake --build . --target TapeEcho_All
cmake --build . --target DrummerClone_All
cmake --build . --target HarmonicGeneratorPlugin_All
```

### Installation Paths
- **VST3**: `~/.vst3/`
- **LV2**: `~/.lv2/`

## Shared Libraries

### Analog Emulation Library
Location: `plugins/shared/AnalogEmulation/`

Reusable analog hardware emulation components:
- Transformer saturation modeling
- Vacuum tube emulation (12AX7, 12AT7, 12BH7, 6SN7)
- Waveshaper lookup tables (LA-2A, 1176, DBX, SSL, Tape, Triode)
- Hardware profiles (Neve, API, Studer, Ampex)
- DC blocking filters
- High-frequency content estimation

### Shared UI Components
- `LunaLookAndFeel.h` - Base look-and-feel for consistent styling
- `LEDMeter.h/cpp` - Shared LED-style level meter component

## License

See individual plugin directories for licensing information.

---
*Luna Co. Audio*
