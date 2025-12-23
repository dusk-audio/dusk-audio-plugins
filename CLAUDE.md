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
  - Multiple tape machine models (Swiss800 [Studer A800], Classic102 [Ampex ATR-102], Hybrid Blend)
  - Four tape formulations: Type 456 (warm), GP9 (modern), Type 911 (German precision), Type 250 (professional)
  - Tape speed selection (7.5, 15, 30 IPS)
  - Advanced saturation and hysteresis modeling (ImprovedTapeEmulation.h)
  - Separate Wow & Flutter controls with shared stereo processing:
    - Wow: Slow pitch drift (0.3-0.8 Hz) for vinyl-like wobble
    - Flutter: Faster modulation (3-7 Hz) for tape machine character
    - Single WowFlutterProcessor instance shared between channels for coherent modulation
    - Random modulation component for natural flutter
  - Bias and calibration controls for fine-tuning tape response
  - Auto-compensation mode (VTM-style output gain lock)
  - **Factory Preset System** (`TapeMachinePresets.h`):
    - 15 factory presets across 5 categories
    - **Subtle**: Gentle Warmth, Transparent Glue, Mastering Touch
    - **Warm**: Classic Analog, Vintage Warmth, Tube Console
    - **Character**: 70s Rock, Tape Saturation, Cassette Deck
    - **Lo-Fi**: Lo-Fi Warble, Worn Tape, Dusty Reel
    - **Mastering**: Master Bus Glue, Analog Sheen, Vintage Master
  - Dual stereo VU meters with vintage analog styling
  - Real-time level monitoring (input/output)
  - Animated reel components (ReelAnimation class):
    - Realistic reel rendering with shadow effects
    - Metal reel body with gradient fill
    - Animated tape spokes with tape transfer animation
    - Speed control synced to transport state with wow-based wobble
  - 2x/4x oversampling for alias-free saturation
- **Build Target**: `TapeMachine_All`

### 3. **Multi-Comp**
- **Location**: `plugins/multi-comp/`
- **Description**: Multi-mode compressor with seven compression styles plus 4-band multiband compression
- **Features**:
  - 8 compression modes:
    - Vintage Opto - Smooth, program-dependent optical compression
    - Vintage FET - Aggressive, punchy FET compression
    - Classic VCA - Fast, precise VCA with OverEasy soft knee
    - Bus Compressor - Glue and punch for bus compression
    - Studio FET - Clean FET with 30% harmonics of Vintage
    - Studio VCA - Modern VCA with RMS detection and soft knee
    - Digital - Transparent, precise compression
    - Multiband - 4-band multiband compression with adjustable crossovers
  - **Multiband Mode Features**:
    - 4 frequency bands (Low, Lo-Mid, Hi-Mid, High)
    - Vertical crossover faders between bands
    - Per-band threshold, ratio, attack, release, makeup controls
    - Per-band solo buttons
    - LED-style gain reduction meters per band
  - Global sidechain HP filter (20-500Hz) - Prevents pumping from bass
  - Auto-makeup gain - Automatic loudness compensation
  - Output distortion (Soft/Hard/Clip) - Adds character and saturation
  - Mix control for parallel compression
  - Mode-specific attack/release characteristics and saturation
  - Linked gain reduction metering per channel
  - Input/Output/GR metering with atomic thread safety
  - 2x/4x oversampling for anti-aliased processing
- **Build Target**: `MultiComp_All`

### 4. **Vintage Tape Echo**
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

### 5. **Harmonic Generator**
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

### 6. **DrummerClone**
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

### 7. **SilkVerb**
- **Location**: `plugins/SilkVerb/`
- **Description**: Lexicon/Valhalla-style algorithmic reverb with Plate, Room, Hall modes
- **Features**:
  - **FDN Architecture**: 8-channel stereo Feedback Delay Network with Hadamard matrix mixing
  - **Three Reverb Modes**:
    - **Plate**: Short prime-number delays (7.2-51.3ms), no early reflections (like real plates), fast modulation (1.8Hz), tight HF decay
    - **Room**: Medium delays (12-62ms), subtle early reflections, moderate modulation (1.2Hz), balanced frequency decay
    - **Hall**: Long delays (40-120ms), prominent early reflections, slow modulation (0.6Hz), extended low-frequency decay
  - **Lexicon-Style DSP Enhancements**:
    - **Two-Band Decay**: Separate low/high frequency decay multipliers with configurable crossover (~600-800Hz)
    - **Complex Modulation**: Three uncorrelated LFOs at golden-ratio-related rates plus smoothed random noise component
    - **Feedback Saturation**: Asymmetric soft clipping for analog warmth without harshness
    - **Pre-delay with Crossfeed**: Early reflections blend into late reverb for cohesive sound
  - **Controls**:
    - Size: 0.5s to 5.0s decay time
    - Damping: High-frequency absorption (bright to dark)
    - Width: Mono to stereo spread
    - Mix: Dry/wet balance
  - **DSP Components** (`FDNReverb.h`):
    - `TwoBandDecayFilter`: Frequency-dependent decay with crossover filter
    - `ComplexModulator`: Multi-LFO + random modulation system
    - `FeedbackSaturator`: Asymmetric soft saturation in feedback loop
    - `EarlyReflections`: 8-tap early reflections with pre-delay and crossfeed
    - `AllpassDiffuser`: 4-stage allpass network for density
    - `DampingFilter`: One-pole lowpass for HF absorption
    - Linear-interpolated delay lines with modulation
    - Orthogonal 8x8 Hadamard matrix for energy preservation
  - **UI**: Dark-themed interface with mode selector buttons, 2x2 knob grid
- **Build Target**: `SilkVerb_All`

### 8. **Convolution Reverb**
- **Location**: `plugins/convolution-reverb/`
- **Description**: Zero-latency convolution reverb with SDIR/AIFC impulse response support
- **Features**:
  - **IR Loading**: Supports WAV, AIFF, AIFC (Apple Spatial Audio), SDIR (Logic Pro spatial IR)
  - **Waveform Display**: Visual representation of loaded impulse response
  - **Zero-Latency Processing**: Partitioned convolution for real-time use
  - **Controls**: Size, pre-delay, damping, width, mix
  - **Thread-Safe**: Background IR loading with atomic state management
- **Build Target**: `ConvolutionReverb_All`

### 9. **Multi-Q**
- **Location**: `plugins/multi-q/`
- **Description**: Universal EQ with multiple EQ modes (Digital 8-band, British console)
- **Features**:
  - **EQ Mode Selector**: Dropdown to switch between EQ types
    - **Digital**: Clean 8-band parametric (default)
    - **British**: SSL 4000-style console EQ (integrated from 4K-EQ)
    - Future: Tube (Pultec-style) EQ
  - **Digital Mode - 8 Color-Coded Frequency Bands**:
    - Band 1 (Red): High-Pass Filter with variable slope (6/12/18/24/36/48 dB/oct)
    - Band 2 (Orange): Low Shelf with adjustable Q
    - Bands 3-6 (Yellow/Green/Aqua/Blue): Parametric EQ with Freq/Gain/Q
    - Band 7 (Purple): High Shelf with adjustable Q
    - Band 8 (Pink): Low-Pass Filter with variable slope
  - **British Mode - SSL Console EQ** (`BritishEQProcessor.h`):
    - 4-band parametric EQ (LF, LMF, HMF, HF) with SSL-style response
    - High-pass and low-pass filters
    - Brown/Black mode selector (E-Series/G-Series console variants)
    - SSL saturation with transformer and op-amp modeling (`SSLSaturation.h`)
    - Input/Output gain controls
  - **Real-Time FFT Analyzer**:
    - Peak and RMS display modes
    - Configurable resolution (Low=2048, Medium=4096, High=8192 points)
    - Adjustable decay rate (3-60 dB/s)
    - Pre/Post EQ display option
  - **Q-Coupling** (Gain-Q automatic adjustment):
    - 8 modes: Off, Proportional, Light, Medium, Strong
    - Asymmetric variants: stronger coupling for cuts than boosts
  - **Interactive Graphic Display**:
    - Draggable control points for each band
    - Color-coded band curves with shaded fill
    - Combined EQ curve display (white)
    - Logarithmic frequency scale (20 Hz - 20 kHz)
    - Display scale modes: ±12 dB, ±30 dB, ±60 dB, Warped
  - **Processing Options**:
    - HQ Mode: 2x oversampling for analog-matched response (prevents Nyquist cramping)
    - Stereo/Mid-Side processing modes (Stereo, Left, Right, Mid, Side)
    - Master gain with optional visualization overlay
  - **Analog-Matched Response**:
    - Bilinear transform with proper frequency pre-warping
    - No "digital" sound - response matches analog prototypes
    - Zero latency (without HQ mode)
  - **UI Features**:
    - EQ type dropdown on toolbar
    - Band enable buttons with color indicators
    - Selected band parameter controls (Freq/Gain/Q/Slope)
    - Professional LED meters (input/output)
    - Supporters overlay (click title)
- **Build Target**: `MultiQ_All`

## Build System

### Building Plugins (Default)
**Always use the Docker/Podman containerized build.** This ensures glibc compatibility and consistent builds across all Linux distributions:

```bash
# Build all plugins
./docker/build_release.sh

# Build a single plugin (faster for development)
./docker/build_release.sh 4keq         # 4K EQ
./docker/build_release.sh compressor   # Multi-Comp
./docker/build_release.sh tape         # TapeMachine
./docker/build_release.sh echo         # Vintage Tape Echo
./docker/build_release.sh drummer      # DrummerClone
./docker/build_release.sh harmonic     # Harmonic Generator
./docker/build_release.sh convolution  # Convolution Reverb
./docker/build_release.sh silkverb     # SilkVerb (algorithmic reverb)

# Show all available shortcuts
./docker/build_release.sh --help
```

**IMPORTANT FOR AI ASSISTANTS**: Always use `./docker/build_release.sh` to compile plugins. Do NOT use local cmake commands or `./rebuild_all.sh` for verification - the Docker build is the only approved build method to ensure consistent, distributable binaries. Use single-plugin builds (e.g., `./docker/build_release.sh 4keq`) for faster iteration when working on one plugin.

**MANDATORY POST-BUILD VALIDATION**: After ANY successful plugin build, you MUST run pluginval to validate the plugin:
```bash
# After building a specific plugin, validate it immediately:
./tests/run_plugin_tests.sh --plugin "Plugin Name" --skip-audio

# Examples after single-plugin builds:
./docker/build_release.sh multiq && ./tests/run_plugin_tests.sh --plugin "Multi-Q" --skip-audio
./docker/build_release.sh 4keq && ./tests/run_plugin_tests.sh --plugin "4K EQ" --skip-audio
./docker/build_release.sh compressor && ./tests/run_plugin_tests.sh --plugin "Multi-Comp" --skip-audio
./docker/build_release.sh tape && ./tests/run_plugin_tests.sh --plugin "TapeMachine" --skip-audio
./docker/build_release.sh echo && ./tests/run_plugin_tests.sh --plugin "Vintage Tape Echo" --skip-audio
./docker/build_release.sh silkverb && ./tests/run_plugin_tests.sh --plugin "SilkVerb" --skip-audio
./docker/build_release.sh convolution && ./tests/run_plugin_tests.sh --plugin "Convolution Reverb" --skip-audio
./docker/build_release.sh drummer && ./tests/run_plugin_tests.sh --plugin "DrummerClone" --skip-audio
./docker/build_release.sh harmonic && ./tests/run_plugin_tests.sh --plugin "Harmonic Generator" --skip-audio
```
The `--skip-audio` flag runs pluginval tests without audio analysis (faster). Do NOT skip this validation step - it catches crashes, parameter issues, and compatibility problems that would break the plugin in DAWs.

**Output**: Plugins are placed in `release/` directory with both VST3 and LV2 formats.

**Compatibility**: Binaries work on:
- Debian 12 (Bookworm) and newer
- Ubuntu 22.04 LTS and newer
- Most Linux distributions from 2022 onwards

**Requirements**: Either Podman (preferred on Fedora) or Docker must be installed.

**Plugins built:**
- FourKEQ_All (4K EQ)
- TapeMachine_All
- MultiComp_All
- TapeEcho_All (Vintage Tape Echo)
- DrummerClone_All
- HarmonicGeneratorPlugin_All
- ConvolutionReverb_All
- SilkVerb_All (Algorithmic Reverb)

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
cmake --build . --target MultiComp_All
cmake --build . --target TapeEcho_All
cmake --build . --target DrummerClone_All
cmake --build . --target HarmonicGeneratorPlugin_All
cmake --build . --target ConvolutionReverb_All
cmake --build . --target SilkVerb_All
```

### CMake Build Options
Available in the root CMakeLists.txt:
- `BUILD_4K_EQ` (default: ON)
- `BUILD_MULTI_COMP` (default: ON)
- `BUILD_HARMONIC_GENERATOR` (default: ON)
- `BUILD_TAPE_MACHINE` (default: ON)
- `BUILD_TAPE_ECHO` (default: ON)
- `BUILD_DRUMMER_CLONE` (default: ON)
- `BUILD_CONVOLUTION_REVERB` (default: ON)
- `BUILD_SILKVERB` (default: ON)
- `BUILD_MULTI_Q` (default: ON)

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
1. **Added SilkVerb**: Lexicon/Valhalla-style algorithmic reverb with Plate, Room, Hall modes
   - FDN architecture with 8-channel stereo Hadamard matrix
   - Two-band frequency-dependent decay (separate low/high multipliers)
   - Complex modulation (3 LFOs at golden-ratio rates + random noise)
   - Asymmetric feedback saturation for analog warmth
   - Pre-delay with crossfeed to late reverb
2. **Added Convolution Reverb**: IR-based reverb with SDIR support, waveform display, and zero-latency convolution
3. **Added Vintage Tape Echo**: Full-featured tape echo with 12 modes, spring reverb, and extensive modulation
4. **Enhanced TapeMachine**:
   - Added factory preset system with 15 presets across 5 categories (Subtle, Warm, Character, Lo-Fi, Mastering)
   - Added Type 250 professional tape formulation
   - Separate Wow and Flutter controls for independent modulation
   - Added shared wow/flutter processing with coherent stereo modulation
   - Improved reel animation with realistic visual components and tape transfer
   - Enhanced UI with vintage rotary switch for noise enable, text shadows for readability
   - Model names: Swiss800 (Studer A800) and Classic102 (Ampex ATR-102)
5. **Updated 4K EQ**:
   - Advanced SSL saturation modeling with E-Series/G-Series console emulation
   - Multi-stage signal path with transformer and op-amp modeling
   - Frequency-dependent saturation characteristics
   - Real-time spectral analysis for dynamic response
6. **Enhanced Multi-Comp**: Added 4-band multiband compression mode, linked gain reduction metering for stereo tracking with thread-safe atomic operations
7. **Added DrummerClone**: Logic Pro Drummer-inspired MIDI drum generator with Follow Mode, section-aware patterns, MIDI CC control, humanization, and MIDI export
8. **Removed Plate Reverb and StudioVerb**: Replaced with Convolution Reverb and SilkVerb for better reverb options

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
│   ├── multi-comp/           # Multi-mode compressor with multiband
│   ├── convolution-reverb/  # IR-based convolution reverb
│   ├── SilkVerb/            # Algorithmic reverb (Lexicon/Valhalla style)
│   ├── TapeEcho/            # Vintage tape echo
│   ├── harmonic-generator/  # Harmonic saturation processor
│   ├── DrummerClone/        # Intelligent MIDI drum generator
│   └── shared/              # Shared utilities and libraries
│       ├── AnalogEmulation/ # Shared analog saturation/tube/transformer library
│       ├── LunaLookAndFeel.h # Base look-and-feel for Luna plugins
│       ├── LEDMeter.h/cpp   # Shared LED-style level meter component
│       ├── PatreonBackers.h # Shared Patreon backer credits data
│       └── SupportersOverlay.h # Modal overlay for Patreon supporters display
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
6. **REQUIRED**: Implement Patreon Supporters overlay (see "Shared UI Components" section below)

### Common DSP Patterns
- **Oversampling**: Use `juce::dsp::Oversampling<float>` for anti-aliased processing
- **Filters**: Use `juce::dsp::IIR::Filter` or custom implementations
- **Level metering**: Use `std::atomic<float>` with relaxed memory ordering for UI updates
- **Parameter smoothing**: Use `juce::SmoothedValue` or APVTS built-in smoothing
- **Convolution**: See convolution-reverb's ConvolutionEngine for zero-latency IR processing
- **FDN Reverb**: See SilkVerb's FDNReverb.h for Lexicon-style algorithmic reverb with two-band decay, complex modulation
- **Saturation modeling**: See 4K-EQ's SSLSaturation.h for multi-stage analog emulation
- **Wow/Flutter**: See TapeMachine's WowFlutterProcessor for coherent stereo modulation
- **Animation**: See TapeMachine's ReelAnimation for timer-based UI animations
- **Factory Presets**: See TapeMachine's TapeMachinePresets.h or Multi-Comp's CompressorPresets.h for categorized preset systems with `getNumPrograms()`, `setCurrentProgram()`, `getProgramName()` implementation
- **MIDI Generation**: See DrummerClone's DrummerEngine for procedural pattern generation with Perlin noise variation
- **Groove Analysis**: See DrummerClone's TransientDetector and MidiGrooveExtractor for real-time groove extraction

### Shared Analog Emulation Library
**Location**: `plugins/shared/AnalogEmulation/`

A shared library for analog hardware emulation that should be used across all plugins to avoid code duplication. When implementing saturation, tube emulation, or transformer effects, **always use this library** instead of creating plugin-specific implementations.

**Usage**:
```cpp
#include "../shared/AnalogEmulation/AnalogEmulation.h"

// In prepareToPlay() - initialize singleton resources
AnalogEmulation::initializeLibrary();

// Use waveshaper curves (LA-2A, 1176, DBX, SSL, Transformer, Tape, Triode, Pentode)
auto& curves = AnalogEmulation::getWaveshaperCurves();
float saturated = curves.process(input, AnalogEmulation::WaveshaperCurves::CurveType::Tape);
float withDrive = curves.processWithDrive(input, AnalogEmulation::WaveshaperCurves::CurveType::Triode, 0.5f);

// Use tube emulation (12AX7, 12AT7, 12BH7, 6SN7)
AnalogEmulation::TubeEmulation tube;
tube.prepare(sampleRate, numChannels);
tube.setTubeType(AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
tube.setDrive(0.3f);
float output = tube.processSample(input, channel);

// Use transformer emulation with hardware profiles
AnalogEmulation::TransformerEmulation transformer;
transformer.prepare(sampleRate, numChannels);
transformer.setProfile(AnalogEmulation::HardwareProfileLibrary::getNeve1073().inputTransformer);
float output = transformer.processSample(input, channel);

// Use DC blocker
AnalogEmulation::DCBlocker dcBlocker;
dcBlocker.prepare(sampleRate, 5.0f);  // 5Hz cutoff
float dcFree = dcBlocker.processSample(input);

// Use HF estimator for adaptive saturation
AnalogEmulation::HighFrequencyEstimator hfEstimator;
hfEstimator.prepare(sampleRate);
float satReduction = hfEstimator.getSaturationReduction(input, 0.5f);
```

**Available Components**:
| File | Purpose |
|------|---------|
| `AnalogEmulation.h` | Main include (includes all components) |
| `WaveshaperCurves.h` | Lookup table saturation curves (9 types) |
| `TubeEmulation.h` | Vacuum tube modeling (4 tube types) |
| `TransformerEmulation.h` | Audio transformer saturation |
| `HardwareProfiles.h` | Measured hardware profiles (LA-2A, 1176, DBX, SSL, Neve, API, Studer, Ampex) |
| `DCBlocker.h` | DC blocking filter (mono + stereo) |
| `HighFrequencyEstimator.h` | HF content estimation for anti-aliasing |

**Hardware Profiles Available**:
- **Compressors**: LA-2A, 1176, DBX 160, SSL Bus, Studio FET, Studio VCA, Digital
- **Preamps**: Neve 1073, API 512c
- **Tape Machines**: Studer A800, Ampex ATR-102

**When to Add New Shared Code**:
1. If the same DSP algorithm is needed in 2+ plugins
2. If hardware emulation data (profiles, curves) could be reused
3. If utility functions (DC blocking, HF estimation) are duplicated

**Namespace**: All shared analog emulation code uses the `AnalogEmulation` namespace

### Shared UI Components
**Location**: `plugins/shared/`

All Luna plugins must use these shared UI components for consistency:

#### SupportersOverlay (REQUIRED for all plugins)
**File**: `shared/SupportersOverlay.h`

Modal overlay component that displays Patreon supporter credits when the user clicks on the plugin title. **Every plugin must implement this feature.**

**Usage**:
```cpp
// In PluginEditor.h:
#include "../../shared/SupportersOverlay.h"  // Or appropriate relative path

class MyPluginEditor : public juce::AudioProcessorEditor
{
public:
    void mouseDown(const juce::MouseEvent& e) override;

private:
    std::unique_ptr<SupportersOverlay> supportersOverlay;
    juce::Rectangle<int> titleClickArea;

    void showSupportersPanel();
    void hideSupportersPanel();
};

// In PluginEditor.cpp:

// In paint() - set up clickable area matching where plugin title is drawn:
titleClickArea = juce::Rectangle<int>(10, 5, 180, 30);  // Adjust to match header

// In resized():
if (supportersOverlay)
    supportersOverlay->setBounds(getLocalBounds());

// Implement mouseDown:
void MyPluginEditor::mouseDown(const juce::MouseEvent& e)
{
    if (titleClickArea.contains(e.getPosition()))
        showSupportersPanel();
}

void MyPluginEditor::showSupportersPanel()
{
    if (!supportersOverlay)
    {
        supportersOverlay = std::make_unique<SupportersOverlay>("Plugin Name");
        supportersOverlay->onDismiss = [this]() { hideSupportersPanel(); };
        addAndMakeVisible(supportersOverlay.get());
    }
    supportersOverlay->setBounds(getLocalBounds());
    supportersOverlay->toFront(true);
    supportersOverlay->setVisible(true);
}

void MyPluginEditor::hideSupportersPanel()
{
    if (supportersOverlay)
        supportersOverlay->setVisible(false);
}
```

**Plugins with SupportersOverlay implemented**:
- 4K EQ
- Multi-Comp
- TapeMachine

**Plugins needing SupportersOverlay**:
- SilkVerb
- Convolution Reverb
- Vintage Tape Echo
- DrummerClone
- Harmonic Generator

#### PatreonBackers
**File**: `shared/PatreonBackers.h`

Shared Patreon backer data used by SupportersOverlay. To add new backers:
1. Edit `shared/PatreonBackers.h` - add names to appropriate tier arrays
2. Rebuild all plugins
3. Also update the project README.md with the same names

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
./tests/run_plugin_tests.sh --plugin "Multi-Comp"
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
