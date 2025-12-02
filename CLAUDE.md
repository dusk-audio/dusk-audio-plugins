# Audio Plugins Project Documentation

## Project Overview
This is a collection of professional audio VST3/LV2/AU plugins built with the JUCE framework. All plugins are published under the company name "Luna Co. Audio".

## Plugins in this Repository

### 1. **4K EQ**
- **Location**: `plugins/4k-eq/`
- **Description**: SSL 4000 Series Console EQ Emulation
- **Features**:
  - 4-band parametric EQ (LF, LMF, HMF, HF) with SSL-style colored knobs
  - High-pass and low-pass filters
  - Brown/Black knob variants (E-Series/G-Series console emulation)
  - 2x/4x oversampling for anti-aliasing
  - Advanced SSL saturation modeling (SSLSaturation.h):
    - E-Series (Brown knobs) vs G-Series (Black knobs) console types with different harmonic signatures
    - Multi-stage processing: Input transformer → NE5534 op-amp → Output transformer (E-Series only)
    - Frequency-dependent saturation (transformers saturate more at low frequencies)
    - Asymmetric op-amp clipping with slew-rate limiting
    - Drive range up to 13x-16x gain for authentic "pushed" console sound
    - DC blocker to prevent offset accumulation
    - Real-time high-frequency content estimation for dynamic response
  - Input/Output gain controls
  - Professional metering
- **Build Target**: `FourKEQ_All`

### 2. **TapeMachine**
- **Location**: `plugins/TapeMachine/`
- **Description**: Analog tape machine emulation (Swiss800 & Classic102)
- **Features**:
  - Multiple tape machine models (Swiss800 [Studer A800], Classic102 [Ampex ATR-102], Blend)
  - Multiple tape types
  - Tape speed selection (7.5, 15, 30 IPS)
  - Advanced saturation and hysteresis modeling (ImprovedTapeEmulation.h)
  - Wow & flutter simulation with shared stereo processing:
    - Single WowFlutterProcessor instance shared between channels for coherent modulation
    - Dynamic delay buffer (up to 50ms) sized based on sample rate
    - Double-precision phase tracking
    - Linear interpolation for fractional delay samples
    - Random modulation component for natural flutter
  - Dual stereo VU meters with vintage analog styling
  - Real-time level monitoring (input/output)
  - Animated reel components (ReelAnimation class):
    - Realistic reel rendering with shadow effects
    - Metal reel body with gradient fill
    - Animated tape spokes
    - Speed control synced to transport state
- **Build Target**: `TapeMachine_All`

### 3. **Universal Compressor**
- **Location**: `plugins/universal-compressor/`
- **Description**: Multi-mode compressor with four classic emulations
- **Features**:
  - 4 compression modes: Opto (LA-2A style), FET (1176 style), VCA (DBX 160 style), Bus (SSL Bus style)
  - Mode-specific characteristics and behaviors
  - Advanced sidechain filtering
  - Analog-modeled saturation
  - Mix control for parallel compression
  - Linked gain reduction metering per channel
  - Input/Output/GR metering with atomic thread safety
- **Build Target**: `UniversalCompressor_All`

### 4. **Plate Reverb**
- **Location**: `plugins/PlateReverb/`
- **Description**: High-quality plate reverb based on the Dattorro algorithm
- **Features**:
  - Dattorro plate reverb algorithm (industry-standard digital plate)
  - Size control for space modeling
  - Decay time adjustment
  - Damping for frequency-dependent decay
  - Stereo width control
  - Mix control (dry/wet balance)
  - Mono-in, stereo-out architecture
  - Professional dark-themed UI
- **Build Target**: `PlateReverb_All`
- **Status**: Replaced the previous Studio 480/StudioVerb plugin

### 5. **Vintage Tape Echo**
- **Location**: `plugins/TapeEcho/`
- **Description**: Classic tape echo/delay emulation inspired by vintage hardware
- **Features**:
  - 12 operation modes (various delay/echo patterns)
  - Repeat rate control (delay time)
  - Intensity control (feedback amount)
  - Separate echo and reverb volume controls
  - Bass and treble tone shaping
  - Wow & flutter simulation for authentic tape character
  - Tape age modeling for vintage degradation
  - Motor torque control affecting playback stability
  - Stereo mode options
  - LFO with shape and rate controls
  - Spring reverb simulation
  - Preamp saturation
  - Dual VU meters for level monitoring
  - Mode selector with visual feedback
- **Build Target**: `TapeEcho_All`
- **Product Name**: "Vintage Tape Echo"

### 6. **Harmonic Generator**
- **Location**: `plugins/harmonic-generator/`
- **Description**: Analog-style harmonic saturation processor with hardware emulation
- **Features**:
  - Individual harmonic controls (2nd through 5th harmonics)
  - Global even/odd harmonic balance
  - Warmth and brightness character controls
  - Hardware saturation modes (emulation of classic analog gear)
  - Hardware preset system
  - 2x oversampling for alias-free processing
  - Real-time harmonic spectrum display
  - Stereo level metering (input/output)
  - Professional analog-styled UI
- **Build Target**: `HarmonicGeneratorPlugin_All` (Note: not currently in rebuild_all.sh)

### 7. **DrummerClone**
- **Location**: `plugins/DrummerClone/`
- **Description**: Logic Pro Drummer-inspired intelligent MIDI drum pattern generator
- **Features**:
  - **Follow Mode**: Real-time groove analysis from audio transients or MIDI input
    - TransientDetector for audio onset detection
    - MidiGrooveExtractor for MIDI timing/velocity analysis
    - GrooveFollower with groove lock percentage indicator
  - **Pattern Generation**:
    - 12+ virtual drummer personalities with unique DNA (aggression, ghost notes, fill hunger, etc.)
    - 7 musical styles: Rock, HipHop, Alternative, R&B, Electronic, Trap, Songwriter
    - Section-aware patterns: Intro, Verse, Pre-Chorus, Chorus, Bridge, Breakdown, Outro
    - Intelligent fills with configurable frequency, intensity, and length
    - Ghost notes, cymbal patterns, and kick/snare variations
    - VariationEngine using Perlin noise for anti-repetition
  - **Humanization**:
    - Timing variation (jitter)
    - Velocity variation
    - Push/Drag (ahead/behind beat)
    - Groove depth control
  - **MIDI CC Control** (for DAW automation):
    - Section control via CC (default: 102)
    - Fill trigger via CC (default: 103)
    - Visual "MIDI" indicator when externally controlled
  - **User Interface**:
    - XY Pad for Swing/Intensity (Logic Pro style)
    - Follow Mode panel with groove lock indicator
    - Fills panel with manual trigger button
    - Humanization panel (collapsible)
    - MIDI CC control panel (collapsible)
    - Step Sequencer component (UI ready, not wired)
    - MIDI export to file (4/8/16/32 bars)
- **Technical Details**:
  - MIDI-only output (route to any drum sampler)
  - 960 PPQ timing resolution
  - 2-second analysis buffer for Follow Mode
- **Build Target**: `DrummerClone_All`

## Build System

### Comprehensive Build Script
A single script handles everything - clean, build, and install:
```bash
# From the plugins directory: /home/marc/projects/Luna/plugins/
./rebuild_all.sh           # Standard rebuild (Release mode)
./rebuild_all.sh --fast     # Use ccache and ninja if available
./rebuild_all.sh --clean    # Clean only, don't build
./rebuild_all.sh --debug    # Debug build
./rebuild_all.sh --release  # Release build (default)
./rebuild_all.sh --parallel 8  # Specify job count (default: auto-detect)
```

**Current plugins built by rebuild_all.sh:**
- FourKEQ_All
- TapeMachine_All
- UniversalCompressor_All
- PlateReverb_All
- TapeEcho_All
- DrummerClone_All

**Note**: The Harmonic Generator is not currently included in the rebuild_all.sh script but can be built via CMake options.

### Manual Build Commands
```bash
cd /home/marc/projects/Luna/plugins/build

# Clean rebuild all plugins
rm -rf * && cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j8

# Build specific plugins
cmake --build . --target FourKEQ_All
cmake --build . --target TapeMachine_All
cmake --build . --target UniversalCompressor_All
cmake --build . --target PlateReverb_All
cmake --build . --target TapeEcho_All
cmake --build . --target DrummerClone_All
cmake --build . --target HarmonicGeneratorPlugin_All
```

### CMake Build Options
Available in the root CMakeLists.txt:
- `BUILD_4K_EQ` (default: ON)
- `BUILD_UNIVERSAL_COMPRESSOR` (default: ON)
- `BUILD_HARMONIC_GENERATOR` (default: ON)
- `BUILD_TAPE_MACHINE` (default: ON)
- `BUILD_TAPE_ECHO` (default: ON)
- `BUILD_PLATE_REVERB` (default: ON)
- `BUILD_DRUMMER_CLONE` (default: ON)

### Installation Paths
- **VST3**: `~/.vst3/`
- **LV2**: `~/.lv2/`
- **AU** (macOS): `~/Library/Audio/Plug-Ins/Components/`
- **Standalone**: Builds standalone applications for each plugin

The build script automatically copies plugins to these directories after successful compilation.

## Known Issues & Fixes

### Bundle ID Warnings
Some plugins show "BUNDLE_ID contains spaces" warnings. This is cosmetic and doesn't affect functionality.

### VST3 Parameter Conflicts
Fixed by adding `JUCE_FORCE_USE_LEGACY_PARAM_IDS=1` to compile definitions in all plugin CMakeLists.txt files.

### Build Optimization
For faster builds, install ccache and ninja:
```bash
sudo dnf install ccache ninja-build
# Then use: ./rebuild_all.sh --fast
```

The build script will automatically detect and use these tools when available.

## Recent Changes

### Plugin Updates
1. **Removed StudioVerb/Studio 480**: Replaced with Plate Reverb (Dattorro algorithm implementation). StudioVerb directory still exists but is disabled in build configuration.
2. **Added Vintage Tape Echo**: Full-featured tape echo with 12 modes, spring reverb, and extensive modulation
3. **Enhanced TapeMachine**:
   - Added shared wow/flutter processing with coherent stereo modulation
   - Improved reel animation with realistic visual components
   - Model names: Swiss800 (Studer A800) and Classic102 (Ampex ATR-102)
4. **Updated 4K EQ**:
   - Advanced SSL saturation modeling with E-Series/G-Series console emulation
   - Multi-stage signal path with transformer and op-amp modeling
   - Frequency-dependent saturation characteristics
   - Real-time spectral analysis for dynamic response
5. **Enhanced Universal Compressor**: Added linked gain reduction metering for stereo tracking with thread-safe atomic operations
6. **Added DrummerClone**: Logic Pro Drummer-inspired MIDI drum generator with Follow Mode, section-aware patterns, MIDI CC control, humanization, and MIDI export

### Build System Improvements
1. **Comprehensive rebuild script**: Color-coded output, progress tracking, error logging
2. **Build optimization support**: ccache and ninja integration
3. **Per-plugin build logs**: Saved to `/tmp/` for debugging
4. **Automatic installation verification**: Lists installed VST3/LV2 plugins after build
5. **Proper JUCE path**: Updated from `/home/marc/Projects/JUCE` to `/home/marc/projects/JUCE`

### Code Quality
1. **Thread safety**: All metering uses std::atomic with proper memory ordering
2. **Modern C++17**: Consistent use across all plugins
3. **JUCE best practices**: Proper APVTS usage, parameter management
4. **Oversampling**: Anti-aliased processing where appropriate
5. **Custom DSP classes**: Separation of concerns (e.g., DattorroPlate, TapeDelay, SpringReverb)

## Testing the Plugins

### Linux (using Carla or similar host)
```bash
carla
# Add plugin from ~/.vst3/ or ~/.lv2/
```

### Reaper
- Scan for new plugins in Options → Preferences → VST
- Plugins will appear under "Luna Co. Audio" manufacturer
- All plugins support both VST3 and standalone formats

### Standalone Applications
Each plugin can also be run as a standalone application for testing without a DAW.

## Development Notes

### Project Structure
```
plugins/
├── cmake/                    # CMake configuration files
│   ├── GlobalSettings.cmake
│   └── JuceDefaults.cmake
├── plugins/                  # Individual plugin directories
│   ├── 4k-eq/               # SSL 4000 Series EQ
│   ├── TapeMachine/         # Tape machine emulation
│   ├── universal-compressor/ # Multi-mode compressor
│   ├── PlateReverb/         # Dattorro plate reverb
│   ├── TapeEcho/            # Vintage tape echo
│   ├── harmonic-generator/  # Harmonic saturation processor
│   ├── DrummerClone/        # Intelligent MIDI drum generator
│   ├── StudioVerb/          # (Deprecated - disabled in build)
│   └── shared/              # Shared utilities
├── build/                    # Build output (gitignored)
├── CMakeLists.txt           # Root build configuration
└── rebuild_all.sh           # Comprehensive build script
```

### Adding New Plugins
1. Create plugin directory under `plugins/`
2. Add CMakeLists.txt with juce_add_plugin()
3. Add to root CMakeLists.txt with option flag
4. Add build target to rebuild_all.sh PLUGINS array
5. Follow naming convention: `PluginName_All` for build targets

### Common DSP Patterns
- **Oversampling**: Use `juce::dsp::Oversampling<float>` for anti-aliased processing
- **Filters**: Use `juce::dsp::IIR::Filter` or custom implementations
- **Level metering**: Use `std::atomic<float>` with relaxed memory ordering for UI updates
- **Parameter smoothing**: Use `juce::SmoothedValue` or APVTS built-in smoothing
- **Custom delays**: See PlateReverb's CustomDelays class for efficient implementations
- **Saturation modeling**: See 4K-EQ's SSLSaturation.h for multi-stage analog emulation
- **Wow/Flutter**: See TapeMachine's WowFlutterProcessor for coherent stereo modulation
- **Animation**: See TapeMachine's ReelAnimation for timer-based UI animations
- **MIDI Generation**: See DrummerClone's DrummerEngine for procedural pattern generation with Perlin noise variation
- **Groove Analysis**: See DrummerClone's TransientDetector and MidiGrooveExtractor for real-time groove extraction

### UI Design Guidelines
- **Color schemes**: Dark themes with analog-inspired controls
- **Metering**: VU meters for vintage gear, LED-style for modern
- **Custom LookAndFeel**: Each plugin has custom styling (e.g., FourKLookAndFeel, AnalogLookAndFeel)
- **Resizable**: Consider making UIs resizable for different screen sizes
- **Performance**: Use timer callbacks sparingly (30-60 Hz max for UI updates)

### Code Style
- Modern C++17 features used throughout
- JUCE naming conventions (camelCase for methods, PascalCase for classes)
- Separate DSP processing in dedicated classes where appropriate
- Header-only implementations for small utility classes
- Forward declarations to minimize compile times
- Consistent parameter naming (e.g., `PARAM_*` constants)

## JUCE Framework
- **Location**: `/home/marc/projects/JUCE/`
- **Version**: Latest from develop branch
- **Modules used**: audio_basics, audio_devices, audio_formats, audio_plugin_client, audio_processors, audio_utils, core, data_structures, dsp, events, graphics, gui_basics, gui_extra

## Troubleshooting

### Build Failures
1. Check build logs in `/tmp/PluginName_build.log`
2. Verify JUCE path is correct in root CMakeLists.txt
3. Ensure all dependencies are installed
4. Try clean rebuild: `./rebuild_all.sh --clean && ./rebuild_all.sh`

### Plugin Not Showing in DAW
1. Verify installation: `ls ~/.vst3/` or `ls ~/.lv2/`
2. Rescan plugins in your DAW
3. Check DAW plugin blacklist/blocklist
4. Try standalone version first to verify plugin works

### Audio Issues
1. Check sample rate in prepareToPlay() is handled correctly
2. Verify buffer sizes are appropriate
3. Look for NaN/inf values in debug builds
4. Use JUCE's AudioBuffer assertions in debug mode

## Contact & Support
For issues or questions about these plugins:
- Check the build logs in `/home/marc/projects/Luna/plugins/build/`
- Review individual plugin source code
- Check JUCE forum for framework-related questions

---
*Last updated: December 2025*
*Company: Luna Co. Audio*
*Build System: CMake + JUCE 7.x*
