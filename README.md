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
- **Vintage Opto** – Classic 1960s tube optical leveling amplifier (inspired by the LA-2A). Program-dependent attack/release with smooth, musical compression. Features the iconic "Peak Reduction" and "Gain" controls with optional Limit mode.
- **Vintage FET** – Classic 1967 Rev A "Bluestripe" FET limiting amplifier (inspired by the 1176). All-discrete Class A design with ultra-fast attack times. Features the famous "All Buttons" mode for extreme compression/distortion. Four ratio settings: 4:1, 8:1, 12:1, 20:1, plus All.
- **Classic VCA** – Fast, precise 1970s VCA compressor (inspired by the dbx 160). Known for its punchy, aggressive character with "OverEasy" soft-knee compression. Great for drums and percussive sources.
- **Bus Compressor** – Classic British console bus compressor (inspired by the SSL G-Series). The quintessential mix bus glue with fixed attack/release detents and Auto release. 2:1, 4:1, and 10:1 ratios with that distinctive punchy character.
- **Studio FET** – Later revision "Blackface" FET limiter (inspired by the 1176 Rev E/F). Cleaner character with approximately 30% of the harmonic content of the vintage version. More controlled transient response.
- **Studio VCA** – Modern British dual VCA compressor (inspired by the Focusrite Red 3). Clean, musical compression with RMS detection and soft knee. Excellent for vocals and mix bus applications.
- **Digital** – Transparent, mathematically precise digital compressor. Zero coloration with accurate peak/RMS detection. Ideal for surgical dynamics control where transparency is paramount.
- **Multiband** – 4-band multiband compressor with Linkwitz-Riley crossovers. Adjustable crossover frequencies, per-band threshold/ratio/attack/release/makeup, and solo buttons for each band.

**Features:** Sidechain HP filter (20-500Hz), sidechain low/high shelf EQ, auto-makeup gain, parallel mix, analog noise floor simulation, hardware-accurate transformer emulation with mode-specific HF rolloff, 2x/4x oversampling, lookahead with true-peak detection.

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
