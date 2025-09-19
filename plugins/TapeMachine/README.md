# TapeMachine VST3 Plugin

A high-quality tape machine emulation plugin that models the Studer A800 and Ampex ATR-102 tape machines.

## Features

- **Dual Machine Emulation**: Choose between Studer A800, Ampex ATR-102, or a blend of both
- **Multiple Tape Speeds**: 7.5, 15, and 30 IPS
- **Tape Type Selection**: Ampex 456, GP9, and BASF 911 formulations
- **Comprehensive Controls**:
  - Input/Output Gain
  - Tape Saturation
  - High-pass and Low-pass Filters
  - Tape Noise with Enable/Disable
  - Wow & Flutter simulation
- **Anti-aliasing**: 2x oversampling to prevent digital artifacts
- **Vintage GUI**: Analog-style interface with animated reels

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
The plugin uses 2x oversampling with polyphase IIR filtering to prevent aliasing artifacts from the non-linear tape saturation processing.

### Machine Characteristics

**Studer A800**:
- Warm, slightly compressed sound
- Gentle high-frequency roll-off
- Enhanced low-end response
- Smooth harmonic distortion

**Ampex ATR-102**:
- Clear, wide frequency response
- Detailed transient response
- Less compression
- Transparent saturation

## License

This plugin is provided as-is for educational and production use.

## Credits

Developed with JUCE Framework