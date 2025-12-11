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
- **Description**: Multi-mode compressor with seven compression styles
- **Features**:
  - 7 compression modes:
    - Vintage Opto - Smooth, program-dependent optical compression
    - Vintage FET - Aggressive, punchy FET compression
    - Classic VCA - Fast, precise VCA with OverEasy soft knee
    - Bus Compressor - Glue and punch for bus compression
    - Studio FET - Clean FET with 30% harmonics of Vintage
    - Studio VCA - Modern VCA with RMS detection and soft knee
    - Digital - Transparent, precise compression
  - Global sidechain HP filter (20-500Hz) - Prevents pumping from bass
  - Auto-makeup gain - Automatic loudness compensation
  - Output distortion (Soft/Hard/Clip) - Adds character and saturation
  - Mix control for parallel compression
  - Mode-specific attack/release characteristics and saturation
  - Linked gain reduction metering per channel
  - Input/Output/GR metering with atomic thread safety
  - 2x internal oversampling for anti-aliased processing
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

### Building Plugins (Default)
**Always use the Docker/Podman containerized build.** This ensures glibc compatibility and consistent builds across all Linux distributions:

```bash
# Build all plugins
./docker/build_release.sh

# Build a single plugin (faster for development)
./docker/build_release.sh 4keq         # 4K EQ
./docker/build_release.sh compressor   # Universal Compressor
./docker/build_release.sh tape         # TapeMachine
./docker/build_release.sh echo         # Vintage Tape Echo
./docker/build_release.sh reverb       # Plate Reverb
./docker/build_release.sh drummer      # DrummerClone
./docker/build_release.sh harmonic     # Harmonic Generator

# Show all available shortcuts
./docker/build_release.sh --help
```

**IMPORTANT FOR AI ASSISTANTS**: Always use `./docker/build_release.sh` to compile plugins. Do NOT use local cmake commands or `./rebuild_all.sh` for verification - the Docker build is the only approved build method to ensure consistent, distributable binaries. Use single-plugin builds (e.g., `./docker/build_release.sh 4keq`) for faster iteration when working on one plugin.

**Output**: Plugins are placed in `release/` directory with both VST3 and LV2 formats.

**Compatibility**: Binaries work on:
- Debian 12 (Bookworm) and newer
- Ubuntu 22.04 LTS and newer
- Most Linux distributions from 2022 onwards

**Requirements**: Either Podman (preferred on Fedora) or Docker must be installed.

**Plugins built:**
- FourKEQ_All (4K EQ)
- TapeMachine_All
- UniversalCompressor_All
- PlateReverb_All
- TapeEcho_All (Vintage Tape Echo)
- DrummerClone_All

**Note**: The Harmonic Generator is not currently included but can be added to the Docker build script.

### Local Development Builds (Alternative)
Only use the local rebuild script if Docker/Podman is unavailable or for quick debugging iterations:
```bash
# From the plugins directory
./rebuild_all.sh           # Standard rebuild (Release mode)
./rebuild_all.sh --fast     # Use ccache and ninja if available
./rebuild_all.sh --clean    # Clean only, don't build
./rebuild_all.sh --debug    # Debug build
./rebuild_all.sh --parallel 8  # Specify job count (default: auto-detect)
```

**Warning**: Local builds may not be compatible with other Linux distributions due to glibc version differences.

### Post-Build Validation
After building, validate all plugins work correctly:
```bash
# Quick validation - verifies plugins are built and loadable
./tests/quick_validate.sh

# Full validation with pluginval (if installed)
./tests/run_plugin_tests.sh
```

### Manual Build Commands
```bash
cd build

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
5. **Proper JUCE path**: JUCE should be located at `../JUCE` relative to project root (sibling directory)

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
├── docker/                   # Primary build environment (always use this)
│   ├── Dockerfile.build     # Ubuntu 22.04 build image
│   └── build_release.sh     # Main build script
├── plugins/                  # Individual plugin directories
│   ├── 4k-eq/               # 4K EQ (console-style EQ)
│   ├── TapeMachine/         # Tape machine emulation
│   ├── universal-compressor/ # Multi-mode compressor
│   ├── PlateReverb/         # Dattorro plate reverb
│   ├── TapeEcho/            # Vintage tape echo
│   ├── harmonic-generator/  # Harmonic saturation processor
│   ├── DrummerClone/        # Intelligent MIDI drum generator
│   ├── StudioVerb/          # (Deprecated - disabled in build)
│   └── shared/              # Shared utilities
├── tests/                    # Plugin validation framework
│   ├── quick_validate.sh    # Fast plugin check
│   └── run_plugin_tests.sh  # Full test suite
├── release/                  # Build output from Docker (gitignored)
├── build/                    # Local dev build output (gitignored)
├── CMakeLists.txt           # Root build configuration
└── rebuild_all.sh           # Local build script (use docker/build_release.sh instead)
```

### Adding New Plugins
1. Create plugin directory under `plugins/`
2. Add CMakeLists.txt with juce_add_plugin()
3. Add to root CMakeLists.txt with option flag
4. Add build target to `docker/build_release.sh` PLUGINS array
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
- **Location**: `../JUCE/` (sibling directory to project root)
- **Version**: Latest from develop branch
- **Modules used**: audio_basics, audio_devices, audio_formats, audio_plugin_client, audio_processors, audio_utils, core, data_structures, dsp, events, graphics, gui_basics, gui_extra

## Troubleshooting

### Build Failures
1. Ensure Docker or Podman is installed and running
2. Try rebuilding the container: `./docker/build_release.sh` (it will rebuild if needed)
3. Check container logs for errors
4. For local builds only: Check build logs in `/tmp/PluginName_build.log`

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

## Plugin Testing Framework

A comprehensive testing framework is available in `tests/` for automated validation and audio analysis.

### Quick Validation
```bash
# Fast sanity check - verifies all plugins are built and installed
./tests/quick_validate.sh

# Full test suite with all validations
./tests/run_plugin_tests.sh

# Test a specific plugin
./tests/run_plugin_tests.sh --plugin "Universal Compressor"
```

### Test Components

#### 1. Plugin Validation (`run_plugin_tests.sh`)
- **File existence checks**: Verifies VST3/LV2 files are installed correctly
- **Binary analysis**: Checks for required symbols (GetPluginFactory)
- **Pluginval integration**: Runs Tracktion's pluginval at multiple strictness levels
- **Dependency verification**: Ensures all shared libraries are available

#### 2. Audio Analysis (`audio_analyzer.py`)
Python-based audio analysis providing PluginDoctor-like functionality:

```bash
# Generate test signals and analyze
python3 tests/audio_analyzer.py --plugin "4K EQ" --output-dir tests/output

# Analyze previously processed files
python3 tests/audio_analyzer.py --plugin "4K EQ" --analyze
```

**Analysis capabilities:**
- **THD (Total Harmonic Distortion)**: Measures harmonic content at 1kHz
- **THD+N**: Total harmonic distortion plus noise
- **Frequency Response**: Using logarithmic sweeps
- **Phase Response**: Phase shift across frequencies
- **Aliasing Detection**: High-frequency artifacts at 18kHz
- **Noise Floor**: Self-noise with silent input
- **Null Testing**: Bypass verification (should be < -120dB residual)
- **Latency Measurement**: Plugin processing delay in samples/ms

**Test signal files generated:**
- `*_test_sine_1khz.wav` - For THD measurement
- `*_test_sine_18khz.wav` - For aliasing detection
- `*_test_sweep.wav` - For frequency/phase response
- `*_test_impulse.wav` - For latency measurement
- `*_test_silence.wav` - For noise floor
- `*_test_multitone.wav` - For IMD testing (60Hz + 7kHz)
- `*_test_pink_noise.wav` - For general testing and null tests

#### 3. DSP Unit Tests (`dsp_test_harness.cpp`)
Standalone C++ test harness for testing DSP algorithms without plugin wrappers:

```bash
# Compile the test harness
cd tests
g++ -std=c++17 -O2 dsp_test_harness.cpp -o dsp_test_harness -lm

# Run tests
./dsp_test_harness
```

**Test categories:**
- Sample validity (no NaN/Inf)
- DC offset verification
- Clipping detection
- Noise floor measurement
- Bypass null testing
- Gain accuracy
- Compression behavior

### Manual Audio Testing Workflow

For comprehensive plugin testing similar to PluginDoctor:

1. **Generate test signals:**
   ```bash
   python3 tests/audio_analyzer.py --plugin "Plugin Name"
   ```

2. **Process through plugin:**
   - Load test WAV files into DAW
   - Insert plugin on track
   - Record output with `_processed` suffix
   - For bypass test, record with `_bypass` suffix

3. **Analyze results:**
   ```bash
   python3 tests/audio_analyzer.py --plugin "Plugin Name" --analyze
   ```

4. **Review report:**
   - JSON report saved to `tests/output/plugin_name_report.json`
   - Console output shows pass/fail for each test

### Test Thresholds

| Test | Pass Threshold | Description |
|------|----------------|-------------|
| THD @ 1kHz | < 1.0% | Total harmonic distortion |
| Noise Floor | < -80 dB | Self-noise with silent input |
| Bypass Null | < -100 dB | Residual when bypassed |
| Aliasing | None detected | No unexpected spectral content |
| Clipping | 0% samples | No samples exceeding ±1.0 |
| DC Offset | < 0.001 | Mean sample value |

### Installing Test Dependencies

```bash
# Pluginval (download AppImage from releases)
wget https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip
unzip pluginval_Linux.zip
chmod +x pluginval
sudo mv pluginval /usr/local/bin/

# Python dependencies
pip3 install numpy scipy

# Build DSP test harness
cd tests && g++ -std=c++17 -O2 dsp_test_harness.cpp -o dsp_test_harness -lm
```

### Continuous Integration Testing

For automated testing in CI/CD:

```bash
# Full validation (returns non-zero on failure)
./tests/run_plugin_tests.sh --skip-audio

# Quick check only
./tests/quick_validate.sh
```

### Adding Tests for New Plugins

1. Add plugin name to `PLUGINS` array in `run_plugin_tests.sh`
2. Create plugin-specific DSP tests in `dsp_test_harness.cpp`
3. Run `quick_validate.sh` to verify basic installation
4. Run full audio analysis to establish baseline measurements

## Contact & Support
For issues or questions about these plugins:
- Check the build logs in `build/` directory or `/tmp/PluginName_build.log`
- Review individual plugin source code
- Check JUCE forum for framework-related questions
---
*Last updated: December 2025*
*Company: Luna Co. Audio*
*Build System: CMake + JUCE 7.x*
