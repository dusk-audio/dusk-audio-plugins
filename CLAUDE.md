# Audio Plugins Project Documentation

## Project Overview
This is a collection of professional audio VST3/LV2/AU plugins built with the JUCE framework. All plugins are published under the company name "Luna Co. Audio".

## Plugins in this Repository

### 1. **4K EQ**
- **Location**: `plugins/4k-eq/`
- **Description**: SSL-style 4-band parametric equalizer with analog modeling
- **Features**:
  - 4 frequency bands (LF, LMF, HMF, HF) with SSL-style colored knobs
  - Oversampling (2x/4x) for high-quality processing
  - Input/Output gain controls
  - Analog saturation modeling
  - Professional metering

### 2. **TapeMachine**
- **Location**: `plugins/TapeMachine/`
- **Description**: Analog tape machine emulation (Studer A800 & Ampex ATR-102)
- **Features**:
  - Multiple tape machine models and tape types
  - Tape speed selection (7.5, 15, 30 IPS)
  - Saturation and hysteresis modeling
  - Wow & flutter simulation
  - Dual stereo VU meters with vintage styling
  - Real-time level monitoring

### 3. **Universal Compressor**
- **Location**: `plugins/universal-compressor/`
- **Description**: Multi-mode compressor with Opto, FET, VCA, and Bus emulations
- **Features**:
  - 4 compression types with unique characteristics
  - Advanced sidechain filtering
  - Analog-modeled saturation
  - Mix control for parallel compression
  - Professional metering

### 4. **Studio 480**
- **Location**: `plugins/Studio480/`
- **Description**: Classic digital reverb processor inspired by legendary hardware
- **Features**:
  - 5 reverb algorithms (Hall, Room, Plate, Random, Twin Delay)
  - Size and decay controls for space modeling
  - Damping for frequency-dependent decay
  - Predelay up to 200ms
  - Diffusion control for density
  - Stereo width adjustment
  - Professional dark-themed UI

### 5. **Harmonic Generator**
- **Location**: `plugins/harmonic-generator/`
- **Description**: Analog-style harmonic saturation processor
- **Features**:
  - Individual harmonic controls (2nd through 5th)
  - Global even/odd harmonic balance
  - Warmth and brightness character controls
  - 2x oversampling for alias-free processing
  - Real-time harmonic spectrum display
  - Stereo level metering

## Build System

### Comprehensive Build Script
A single script handles everything - clean, build, and install:
```bash
# From the plugins directory
./rebuild_all.sh           # Standard rebuild
./rebuild_all.sh --fast     # Use ccache if available
./rebuild_all.sh --clean    # Clean only
./rebuild_all.sh --debug    # Debug build
./rebuild_all.sh --parallel 8  # Specify job count
```

### Manual Build Commands
```bash
cd /home/marc/projects/plugins/build

# Clean rebuild all plugins
rm -rf * && cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j8

# Build specific plugin
cmake --build . --target TapeMachine_All
cmake --build . --target FourKEQ_All
cmake --build . --target UniversalCompressor_All
cmake --build . --target Studio480_All
cmake --build . --target HarmonicGeneratorPlugin_All
```

### Installation Paths
- **VST3**: `~/.vst3/`
- **LV2**: `~/.lv2/`
- **AU** (macOS): `~/Library/Audio/Plug-Ins/Components/`

## Known Issues & Fixes

### Bundle ID Warnings
Some plugins show "BUNDLE_ID contains spaces" warnings. This is cosmetic and doesn't affect functionality.

### VST3 Parameter Conflicts
Fixed by adding `JUCE_FORCE_USE_LEGACY_PARAM_IDS=1` to compile definitions.

### Build Optimization
For faster builds, install ccache:
```bash
sudo dnf install ccache
export CC="ccache gcc"
export CXX="ccache g++"
```

## Recent Work Completed

1. **Fixed 4K EQ knob colors**: Knobs now properly display SSL-style colors (red, yellow, blue, green)
2. **Updated company name**: All plugins now use "Luna Co. Audio" consistently
3. **Fixed TapeMachine VU meters**: Reduced from 4 meters to 2 stereo meters with dual-needle display
4. **Created Studio 480 reverb**: Clean implementation of classic digital reverb processor
5. **Created Harmonic Generator UI**: Complete analog-style interface with spectrum display
6. **Fixed compilation issues**: Resolved all VST3 module path and parameter ID conflicts

## Testing the Plugins

### Linux (using Carla or similar host)
```bash
carla
# Add plugin from ~/.vst3/ or ~/.lv2/
```

### Reaper
- Scan for new plugins in Options → Preferences → VST
- Plugins will appear under "Luna Co. Audio" manufacturer

## Development Notes

### Adding New Features
- All plugins use JUCE AudioProcessor base class
- Parameter handling via AudioProcessorValueTreeState (APVTS)
- Custom look and feel classes for unique UI styling
- Real-time metering using atomic variables for thread safety

### Code Style
- Modern C++17 features used throughout
- JUCE naming conventions (camelCase for methods, PascalCase for classes)
- Separate DSP processing in dedicated classes where appropriate
- Oversampling for high-quality anti-aliased processing

## JUCE Framework
- **Location**: `/home/marc/projects/JUCE/`
- **Version**: Latest from develop branch
- **Modules used**: audio_basics, audio_devices, audio_formats, audio_plugin_client, audio_processors, audio_utils, core, data_structures, dsp, events, graphics, gui_basics, gui_extra

## Contact & Support
For issues or questions about these plugins, check the build logs in `/home/marc/projects/plugins/build/` or review the individual plugin source code.

---
*Last updated: September 2024*
*Company: Luna Co. Audio*