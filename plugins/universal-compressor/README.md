# Universal Compressor

A professional-grade VST3 audio compressor plugin that emulates four classic compressor types:

## Compressor Types

### 🔸 Opto (LA-2A Style)
- **Character**: Smooth, musical optical compression with program-dependent release
- **Controls**: Peak Reduction, Gain, Limit/Compress mode
- **Sound**: Warm tube harmonics, gentle compression curve, vintage warmth

### 🔹 FET (1176 Style) 
- **Character**: Fast, aggressive FET compression with ultra-fast attack
- **Controls**: Input, Output, Attack (20μs-800μs), Release (50ms-1.1s), Ratio (4:1 to All-buttons)
- **Sound**: Punchy transients, aggressive odd harmonics, modern precision

### 🔶 VCA (DBX 160 Style)
- **Character**: Clean, precise VCA compression with hard knee
- **Controls**: Threshold, Ratio, Attack, Release, Output
- **Sound**: Transparent with subtle vintage character, excellent for dynamics control

### 🔷 Bus (SSL Style)
- **Character**: Glue compression with RMS detection and auto-release
- **Controls**: Threshold, Ratio, Attack (0.1ms-30ms), Release (0.1s-1.2s + Auto), Makeup
- **Sound**: Professional bus glue, very clean with subtle warmth

## Features

### 🎛️ Advanced DSP
- **Anti-aliasing**: 2x oversampling with high-quality polyphase filters
- **Authentic emulation**: Period-correct time constants and harmonic characteristics
- **Program-dependent behavior**: Release times that adapt to input material
- **Precision metering**: Real-time input, output, and gain reduction displays

### 🎨 Modern UI
- **Adaptive interface**: Color-coded mode selection with visual feedback
- **Vintage controls**: Custom knobs and buttons with classic feel
- **Professional metering**: VU-style meters for precise monitoring
- **Responsive design**: Scalable interface for different screen sizes

## Technical Specifications

- **Sample Rates**: 44.1kHz - 192kHz
- **Bit Depths**: 32-bit float processing
- **Formats**: VST3, AU, Standalone
- **Latency**: ~1ms with oversampling enabled, <1ms without
- **CPU Usage**: Optimized for real-time performance

## Recent Improvements

### ✅ Safety & Stability
- Sample rate validation guards prevent crashes
- Comprehensive NaN/Inf checks in all envelope followers
- Protected division operations with epsilon constants
- Parameter validation with bounds checking
- Debug assertions for development builds

### ✅ New Features  
- **Stereo Link Control**: Adjustable 0-100% channel linking
- **Mix Control**: Built-in parallel compression (0-100% dry/wet)
- **External Sidechain**: Dedicated sidechain input support
- **Envelope Curves**: Logarithmic (analog) or Linear (digital) options
- **Saturation Modes**: Vintage (warm), Modern (clean), or Pristine (minimal)

### ✅ Performance
- SIMD-optimized denormal prevention
- Lookup tables for exponential calculations (4096 entries)
- Improved CPU efficiency (~15-20% faster)

## Building from Source

### Requirements
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- JUCE Framework (included in project)
- CMake 3.15+ (optional but recommended)

### Quick Build
```bash
# Using the build script
chmod +x build.sh
./build.sh

# Or manually with CMake
mkdir build && cd build
cmake ..
make -j4
```

### Build Output
- `build/UniversalCompressor_artefacts/Release/VST3/` - VST3 plugin
- `build/UniversalCompressor_artefacts/Release/AU/` - Audio Unit (macOS)
- `build/UniversalCompressor_artefacts/Release/Standalone/` - Standalone app

## Installation

### Build from Source

1. **Prerequisites**: 
   - JUCE Framework 6.0+
   - CMake 3.15+
   - C++17 compiler

2. **Build**:
   ```bash
   cd UniversalCompressor
   ./build.sh
   ```

3. **Install**: Follow the build script prompts to install to system directories

### Usage Tips

#### Opto Mode (LA-2A)
- Use for vocals, bass, and program material
- Peak Reduction acts as threshold - higher values = more compression
- Limit mode for transparent peak limiting
- Program-dependent release creates natural breathing

#### FET Mode (1176)
- Perfect for drums, guitars, and aggressive compression
- Input knob controls threshold (higher = more compression)
- Ultra-fast attack captures transients
- "All" ratio setting for extreme effect

#### VCA Mode (DBX 160)
- Ideal for individual tracks and precise control
- Clean, transparent compression
- Hard knee for defined compression point
- Wide ratio range for versatility

#### Bus Mode (SSL)
- Best for mix bus, drum bus, and glue compression
- RMS detection for smooth operation
- Auto-release adapts to program material
- Subtle harmonic enhancement

## Parameter Ranges

| Mode | Parameter | Range | Default |
|------|-----------|-------|---------|
| Opto | Peak Reduction | 0-100 | 50 |
| Opto | Gain | 0-100 | 50 |
| FET | Input | -20dB to +60dB | +24dB |
| FET | Output | -20dB to +60dB | +24dB |
| FET | Attack | 20μs to 800μs | 20μs |
| FET | Release | 50ms to 1100ms | 400ms |
| VCA | Threshold | -40dB to +20dB | -10dB |
| VCA | Ratio | 1:1 to 100:1 | 4:1 |
| VCA | Attack | 0.1ms to 50ms | 1ms |
| VCA | Release | 10ms to 5s | 100ms |
| Bus | Threshold | -30dB to 0dB | -10dB |
| Bus | Ratio | 1:1 to 10:1 | 4:1 |
| Bus | Makeup | 0dB to +20dB | 0dB |

## Algorithm Details

Each compressor mode implements authentic characteristics:

- **Opto**: Optical cell simulation with frequency-dependent sidechaining
- **FET**: Field-effect transistor modeling with asymmetric clipping  
- **VCA**: Voltage-controlled amplifier with precise linear response
- **Bus**: RMS detection with sidechain filtering and soft-knee compression

All modes feature proper envelope following with attack/release coefficients derived from analog circuit analysis.

## License

This project is provided for educational and personal use. Commercial use requires appropriate licensing.

## Credits

- **DSP Design**: Based on analysis of classic analog compressors
- **UI Framework**: JUCE Framework
- **Algorithm Research**: Vintage gear circuit analysis and modeling