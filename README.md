# Dusk Audio Plugins

A collection of professional audio VST3/LV2 plugins built with JUCE.

> **Note:** These plugins are developed with the assistance of AI tools. If that bothers you, these aren't for you.

> **Production Ready:** Only **4K EQ**, **Multi-Comp**, and **TapeMachine** are currently released and recommended for production use. All other plugins are in active development.

## Plugins

### 4K EQ - RELEASED
Classic British console EQ emulation featuring:
- 4-band parametric EQ (LF, LMF, HMF, HF) with color-coded knobs
- High-pass and low-pass filters
- Brown/Black variants (two console voicings)
- Advanced analog saturation modeling
- 2x/4x oversampling for anti-aliasing

### Multi-Comp - RELEASED
Multi-mode compressor with seven classic compression styles plus 4-band multiband compression:

**Compression Modes:**
- **Vintage Opto** – Classic 1960s tube optical leveling amplifier. Program-dependent attack/release with smooth, musical compression. Features "Peak Reduction" and "Gain" controls with optional Limit mode.
- **Vintage FET** – Classic late-1960s FET limiting amplifier. All-discrete Class A design with ultra-fast attack times. Features "All Buttons" mode for extreme compression/distortion. Four ratio settings: 4:1, 8:1, 12:1, 20:1, plus All.
- **Classic VCA** – Fast, precise 1970s VCA compressor. Known for its punchy, aggressive character with soft-knee compression. Great for drums and percussive sources.
- **Bus Compressor** – Classic British console bus compressor. The quintessential mix bus glue with fixed attack/release detents and Auto release. 2:1, 4:1, and 10:1 ratios with that distinctive punchy character.
- **Studio FET** – Later revision FET limiter. Cleaner character with approximately 30% of the harmonic content of the vintage version. More controlled transient response.
- **Studio VCA** – Modern British dual VCA compressor. Clean, musical compression with RMS detection and soft knee. Excellent for vocals and mix bus applications.
- **Digital** – Transparent, mathematically precise digital compressor. Zero coloration with accurate peak/RMS detection. Ideal for surgical dynamics control where transparency is paramount.
- **Multiband** – 4-band multiband compressor with Linkwitz-Riley crossovers. Adjustable crossover frequencies, per-band threshold/ratio/attack/release/makeup, and solo buttons for each band.

**Features:** Sidechain HP filter (20-500Hz), sidechain low/high shelf EQ, auto-makeup gain, parallel mix, analog noise floor simulation, transformer emulation with mode-specific HF rolloff, 2x/4x oversampling, lookahead with true-peak detection.

### TapeMachine - RELEASED
Analog tape machine emulation featuring:
- Two tape machine models with distinct characters
- Four tape formulations: Type 456, GP9, Type 911, Type 250
- Tape speeds: 7.5, 15, 30 IPS
- Advanced saturation and hysteresis modeling
- Separate Wow & Flutter controls
- 15 factory presets across 5 categories (Subtle, Warm, Character, Lo-Fi, Mastering)
- Dual stereo VU meters with animated reels
- 2x/4x oversampling for alias-free processing

### Convolution Reverb - IN DEVELOPMENT
Zero-latency IR-based reverb:
- Supports WAV, AIFF, AIFC, SDIR impulse responses
- Waveform display
- Size, pre-delay, damping, width, mix controls

### Multi-Q - IN DEVELOPMENT
Universal EQ with multiple modes:
- **Digital Mode**: Clean 8-band parametric with color-coded bands
- **British Mode**: Classic console EQ with two console voicings
- Real-time FFT analyzer with pre/post display
- Q-coupling modes for natural EQ response
- Interactive graphic display with draggable points
- HQ mode with 2x oversampling

### Tape Echo - IN DEVELOPMENT
Classic tape delay with spring reverb:
- 12 echo modes
- Spring reverb modeling
- Tape saturation and wow/flutter
- Tempo sync with multiple note divisions
- Animated tape visualization

### GrooveMind - IN DEVELOPMENT
> ⚠️ Early development - not functional yet.
ML-powered intelligent drum pattern generator:
- Pattern generation from Groove MIDI Dataset
- Groove humanization with micro-timing
- Context-aware fills
- Style classification

## Building

### Recommended: Docker/Podman Build
For consistent, distributable binaries:
```bash
# Build all plugins
./docker/build_release.sh

# Build a single plugin (production-ready)
./docker/build_release.sh 4keq         # 4K EQ
./docker/build_release.sh compressor   # Multi-Comp
./docker/build_release.sh tape         # TapeMachine

# Build a single plugin (in development)
./docker/build_release.sh multiq       # Multi-Q
./docker/build_release.sh convolution  # Convolution Reverb
./docker/build_release.sh tapeecho     # Tape Echo
./docker/build_release.sh groovemind   # GrooveMind

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
# Production-ready
cmake --build . --target FourKEQ_All
cmake --build . --target MultiComp_All
cmake --build . --target TapeMachine_All

# In development
cmake --build . --target MultiQ_All
cmake --build . --target ConvolutionReverb_All
cmake --build . --target TapeEcho_All
cmake --build . --target GrooveMind_All
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
- `DuskLookAndFeel.h` - Base look-and-feel for consistent styling
- `LEDMeter.h/cpp` - Shared LED-style level meter component

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

---
*Dusk Audio*
