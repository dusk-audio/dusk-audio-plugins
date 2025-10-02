# 4K EQ - SSL 4000 Series Console EQ Emulation

Professional SSL-style 4-band parametric equalizer with analog modeling, built with JUCE for VST3/LV2/AU/Standalone formats.

![Plugin Type](https://img.shields.io/badge/Type-EQ%20%2F%20Filter-blue)
![Formats](https://img.shields.io/badge/Formats-VST3%20%7C%20LV2%20%7C%20AU%20%7C%20Standalone-green)
![JUCE](https://img.shields.io/badge/JUCE-7%2B-orange)

## Features

### EQ Section
- **4-band parametric EQ** (Low, Low-Mid, High-Mid, High)
- **Brown/Black modes** - SSL E-series vs G-series characteristics
  - **Brown (E-series)**: Musical, broader curves, gentle shelves
  - **Black (G-series)**: Surgical, proportional Q, tighter response
- **High-pass/Low-pass filters** (18dB/oct HPF, 12dB/oct LPF)
- **Bell/Shelf switching** on LF and HF bands (Black mode only)

### Processing Quality
- **2x/4x oversampling** - Anti-aliased, high-quality processing
- **Analog saturation modeling** - Asymmetric op-amp saturation (NE5534 character)
- **Per-band saturation** - Subtle harmonic enhancement on each EQ stage
- **Auto-gain compensation** - Maintains perceived loudness like SSL hardware
- **M/S processing mode** - Mid/Side encoding for stereo width control

### User Interface
- **Real-time spectrum analyzer** - FFT-based frequency visualization (30 Hz)
- **SSL-style color-coded knobs**:
  - 🔴 Red: Gain controls
  - 🟢 Green: Frequency controls
  - 🔵 Blue: Q controls
  - 🟠 Orange: Filters & saturation
- **Professional tick markings** - SSL-style graduated scales around each knob
  - Gain knobs: 0dB center indicator highlighted
  - Filter knobs: Major + minor ticks for precision
  - Context-aware tick density based on knob type
- **Mouse wheel support** - Scroll to adjust knobs
- **Double-click reset** - Quick return to default values
- **Preset browser** - 10 factory presets + user state saving

## Factory Presets

1. **Default** - Flat response, neutral starting point
2. **Vocal Presence** - Clarity boost without harshness (+3dB@3.5kHz, -3dB@300Hz)
3. **Kick Punch** - Tight low-end thump (+6dB@50Hz, -4dB@200Hz)
4. **Snare Crack** - Body and snap (+4dB@250Hz, +5dB@5kHz)
5. **Bass Warmth** - Definition without mud (+4dB@80Hz, +2dB@1.5kHz)
6. **Bright Mix** - Polished enhancement (+2dB@60Hz, +3dB@12kHz, 20% saturation)
7. **Telephone EQ** - Lo-fi narrow bandwidth (HPF@300Hz, LPF@3kHz)
8. **Air & Silk** - High-end sparkle (+3dB@7kHz, +4dB@15kHz)
9. **Mix Bus Glue** - Subtle cohesion (+1.5dB@100Hz, 30% saturation)
10. **Master Sheen** - Polished top-end for mastering (+1dB@5kHz, +1.5dB@16kHz, 10% saturation)

## DAW Compatibility

### ✅ Fully Tested
- **Reaper** - VST3/LV2, all features working
- **Ardour** - LV2 with full GUI (inline display removed in v1.0.1)
- **Carla** - VST3/LV2, standalone host
- **Standalone** - JUCE standalone application

### ⚙️ Expected to Work (VST3)
- Bitwig Studio
- Studio One
- FL Studio (Windows/macOS)
- Ableton Live
- Logic Pro (AU format on macOS)
- Cubase/Nuendo

### ℹ️ Notes
- **LV2 inline display removed** - Conflicted with JUCE wrapper (see `LV2_INLINE_DISPLAY_NOTES.md`)
- **VST3 is recommended** - Best compatibility and features
- **AU support on macOS** - Full compatibility with Logic Pro, GarageBand

## Technical Specifications

### DSP Details
- **Filter topology**: Biquad IIR with SSL-specific coefficient shaping
- **Frequency warping**: Pre-warped for HF accuracy (prevents digital cramping)
- **Saturation model**: Asymmetric soft-clipping (NE5534 op-amp characteristic)
- **Sample rates**: 44.1kHz - 192kHz (auto-limits oversampling at >96kHz)
- **Latency**:
  - 2x oversampling: ~32 samples
  - 4x oversampling: ~96 samples (auto-disabled at high sample rates)

### Parameter Ranges
- **LF/HF Gain**: ±20dB (±15dB typical SSL range + headroom)
- **LF Freq**: 20-600Hz | **HF Freq**: 1.5-20kHz
- **LM Freq**: 200-2500Hz | **HM Freq**: 600-7000Hz (Brown), 600-13kHz (Black)
- **Q Range**: 0.4-5.0 (proportional in Black mode)
- **HPF**: 20-500Hz (18dB/oct) | **LPF**: 3-20kHz (12dB/oct)
- **Saturation**: 0-100%
- **Output Gain**: ±12dB

## Building from Source

### Prerequisites
- **JUCE Framework 7.0+** (tested with JUCE 7.x-8.x)
- **CMake 3.15+**
- **C++17 compiler** (GCC 9+, Clang 10+, MSVC 2019+)
- **Platform libraries**:
  - Linux: `libasound2-dev`, `libfreetype6-dev`, `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`
  - macOS: Xcode Command Line Tools
  - Windows: Visual Studio 2019+

### Build Instructions

#### Quick Build (All Plugins)
```bash
cd /path/to/Luna/plugins
./rebuild_all.sh              # Standard build
./rebuild_all.sh --fast        # With ccache (if available)
./rebuild_all.sh --debug       # Debug build
./rebuild_all.sh --parallel 8  # Specify job count
```

#### Manual CMake Build
```bash
cd /path/to/Luna/plugins
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target FourKEQ_All -j8
```

#### Build Targets
- `FourKEQ_VST3` - VST3 plugin only
- `FourKEQ_LV2` - LV2 plugin only
- `FourKEQ_AU` - AU plugin (macOS only)
- `FourKEQ_Standalone` - Standalone application
- `FourKEQ_All` - All formats

### Installation Paths
- **VST3**: `~/.vst3/4K EQ.vst3` (Linux), `~/Library/Audio/Plug-Ins/VST3/` (macOS)
- **LV2**: `~/.lv2/4K EQ.lv2`
- **AU**: `~/Library/Audio/Plug-Ins/Components/4K EQ.component` (macOS)

## Development

### Project Structure
```
4k-eq/
├── FourKEQ.cpp              # Audio processor (DSP engine)
├── FourKEQ.h                # Processor header
├── PluginEditor.cpp         # GUI implementation
├── PluginEditor.h           # Editor header
├── SpectrumAnalyzer.h       # FFT-based spectrum display
├── FourKLookAndFeel.cpp     # Custom UI theme
├── CMakeLists.txt           # Build configuration
└── README.md                # This file
```

### Known Issues & Limitations
- **Bundle ID warning** (macOS): Cosmetic only, doesn't affect functionality
- **High sample rate oversampling**: Auto-limited to 2x at >96kHz to prevent CPU overload
- **LV2 inline display**: Removed due to JUCE compatibility issues (full GUI still works)

### Performance Notes
- **CPU usage**: ~2-5% per instance (2x oversampling, 48kHz)
- **Optimization**: Install `ccache` for faster rebuilds (`./rebuild_all.sh --fast`)
- **Memory**: ~10MB per instance (includes oversampling buffers)

## Changelog

### v1.0.1 (2025-10-02)
- ✅ Removed LV2 inline display (JUCE compatibility)
- ✅ Added mouse wheel support for knobs
- ✅ Added double-click reset to defaults
- ✅ Added professional knob tick markings (SSL-style)
- ✅ Added pre/post spectrum toggle
- ✅ Added "Master Sheen" factory preset
- ✅ SIMD-optimized spectrum analyzer (~5% CPU reduction)
- ✅ Fixed CMakeLists.txt duplicate warnings
- ✅ Comprehensive documentation updates

### v1.0.0 (2025-09)
- Initial release with VST3/LV2/AU support
- SSL Brown/Black modes
- 2x/4x oversampling
- Real-time spectrum analyzer
- 9 factory presets

## Credits & License

**Developed by**: Luna Co. Audio
**Framework**: JUCE 7+ (GPL/Commercial dual-license)
**License**: GPL-2.0 (plugin code) + JUCE license

**Disclaimer**: This is an independent emulation inspired by SSL 4000 series consoles. SSL and Solid State Logic are trademarks of Solid State Logic Ltd. This project is not affiliated with or endorsed by SSL.

## Support

For issues, feature requests, or questions:
- Check `CLAUDE.md` for project documentation
- Review `LV2_INLINE_DISPLAY_NOTES.md` for LV2-specific info
- Build logs available in `build/` directory

---

*Part of the Luna Co. Audio plugin suite*
