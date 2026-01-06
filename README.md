# Luna Co. Audio Plugins

A collection of professional audio VST3/LV2 plugins built with JUCE.

## Plugins

### 4K EQ
Classic British console EQ emulation featuring:
- 4-band parametric EQ (LF, LMF, HMF, HF) with color-coded knobs
- High-pass and low-pass filters
- Brown/Black variants (E-Series/G-Series console emulation)
- Advanced analog saturation modeling
- 2x/4x oversampling for anti-aliasing

### Multi-Comp
Multi-mode compressor with seven classic compression styles plus 4-band multiband compression:

**Compression Modes:**
- Vintage Opto – Smooth, program-dependent optical compression
- Vintage FET – Aggressive, punchy FET compression
- Classic VCA – Fast, precise VCA compression
- Bus Compressor – Glue and punch for mix bus
- Studio FET – Clean FET with modern character
- Studio VCA – Modern VCA with soft knee
- Digital – Transparent, precise compression
- Multiband – 4-band with adjustable crossovers

**Features:** Sidechain HP filter, auto-makeup gain, parallel mix, 2x/4x oversampling, per-band solo.

### TapeMachine
Analog tape machine emulation featuring:
- Swiss800 and Classic102 tape machine models
- Four tape formulations: Type 456, GP9, Type 911, Type 250
- Tape speeds: 7.5, 15, 30 IPS
- Advanced saturation and hysteresis modeling
- Separate Wow & Flutter controls
- 15 factory presets across 5 categories (Subtle, Warm, Character, Lo-Fi, Mastering)
- Dual stereo VU meters with animated reels
- 2x/4x oversampling for alias-free processing

### SilkVerb
Professional algorithmic reverb:
- Three modes: Plate, Room, Hall
- FDN architecture with Hadamard matrix mixing
- Two-band frequency-dependent decay
- Complex modulation (3 LFOs + random noise)
- Feedback saturation for analog warmth

### Convolution Reverb
Zero-latency IR-based reverb:
- Supports WAV, AIFF, AIFC, SDIR impulse responses
- Waveform display
- Size, pre-delay, damping, width, mix controls

### Vintage Tape Echo
Classic tape echo/delay emulation:
- 12 operation modes
- Wow & flutter simulation
- Spring reverb
- Tape age modeling

### DrummerClone
Intelligent MIDI drum pattern generator:
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
- Waveshaper lookup tables (Opto, FET, VCA, Console, Tape, Triode)
- Hardware profiles for various compression and tape styles
- DC blocking filters
- High-frequency content estimation

### Shared UI Components
- `LunaLookAndFeel.h` - Base look-and-feel for consistent styling
- `LEDMeter.h/cpp` - Shared LED-style level meter component

## License

See individual plugin directories for licensing information.

---
*Luna Co. Audio*
