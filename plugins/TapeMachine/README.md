# TapeMachine VST3 Plugin

A high-quality tape machine emulation plugin with two distinct tape machine models: Swiss800 and Classic102.

## Features

- **Dual Machine Emulation**: Choose between Swiss800 or Classic102 tape machine models
- **Multiple Tape Speeds**: 7.5, 15, and 30 IPS
- **Four Tape Formulations**: Type 456 (warm), GP9 (modern), Type 911 (German precision), Type 250 (professional)
- **Comprehensive Controls**:
  - Input/Output Gain with auto-compensation mode
  - Bias and Calibration controls
  - High-pass and Low-pass Filters
  - Tape Noise with vintage rotary switch enable
  - Separate Wow & Flutter controls for independent modulation
- **Factory Preset System**: 15 professional presets across 5 categories:
  - **Subtle**: Gentle Warmth, Transparent Glue, Mastering Touch
  - **Warm**: Classic Analog, Vintage Warmth, Tube Console
  - **Character**: 70s Rock, Tape Saturation, Cassette Deck
  - **Lo-Fi**: Lo-Fi Warble, Worn Tape, Dusty Reel
  - **Mastering**: Master Bus Glue, Analog Sheen, Vintage Master
- **Anti-aliasing**: 2x/4x oversampling to prevent digital artifacts
- **Vintage GUI**: Analog-style interface with animated reels and dual VU meters

## Building

### Prerequisites

- C++17 compatible compiler
- CMake 3.15 or higher
- JUCE Framework (path configured in CMakeLists.txt)

### Build Instructions

#### Using CMake (Recommended)

```bash
cd plugins/TapeMachine
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

#### Using Projucer

1. Open `TapeMachine.jucer` in Projucer
2. Update JUCE module paths if necessary
3. Generate project files for your platform
4. Build using your IDE or make

### Platform-Specific Notes

#### Linux
```bash
make -j$(nproc)
```

#### macOS
```bash
cmake --build . --config Release -- -j$(sysctl -n hw.ncpu)
```

#### Windows
Use Visual Studio 2022 or newer:
```cmd
cmake --build . --config Release
```

## Installation

After building, copy the generated VST3 file to your plugin folder:
- **Windows**: `C:\Program Files\Common Files\VST3`
- **macOS**: `~/Library/Audio/Plug-Ins/VST3`
- **Linux**: `~/.vst3` or `/usr/local/lib/vst3`

## Technical Details

### DSP Chain
1. Input Gain Stage
2. High-pass Filter (20-500 Hz)
3. Tape Emulation:
   - Pre-emphasis
   - Hysteresis modeling
   - Magnetic saturation
   - Crossover distortion
   - Head bump resonance
   - Tape response filtering
   - De-emphasis
4. Low-pass Filter (3-20 kHz)
5. Wow & Flutter (LFO-modulated delay)
6. Tape Noise (optional)
7. Output Gain Stage

### Anti-Aliasing
The plugin uses selectable 2x or 4x oversampling with FIR equiripple filtering to prevent aliasing artifacts from the non-linear tape saturation processing.

### Machine Characteristics

**Swiss800**:
- Warm, slightly compressed sound
- Gentle high-frequency roll-off
- Enhanced low-end response
- Smooth harmonic distortion

**Classic102**:
- Clear, wide frequency response
- Detailed transient response
- Less compression
- Transparent saturation

## License

This plugin is provided as-is for educational and production use.

## Credits

Developed with JUCE Framework