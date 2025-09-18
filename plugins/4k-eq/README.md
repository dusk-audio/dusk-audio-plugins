# SSL 4000 EQ Plugin

A high-quality emulation of the legendary SSL 4000 series console EQ section, available as both VST3 and LV2 plugins with full inline display support for Ardour.

## Features

- **Authentic SSL 4000 Console EQ Emulation**
  - Brown and Black knob EQ variants
  - 4-band parametric EQ (LF, LM, HM, HF)
  - High-pass and low-pass filters
  - Bell/Shelf modes for LF and HF bands
  - Analog saturation modeling

- **Plugin Formats**
  - VST3 (via JUCE framework)
  - LV2 with inline display support
  - Pure LV2 implementation for maximum compatibility

- **Professional Features**
  - 2x/4x oversampling for anti-aliasing
  - Thread-safe real-time processing
  - Authentic SSL console-style UI
  - Cairo-based inline display for Ardour

## Building

### Prerequisites
- JUCE Framework (for VST3/JUCE-based LV2)
- LV2 development libraries
- Cairo graphics library
- C++17 compiler (for JUCE version)
- C99 compiler (for pure LV2 version)

### Build VST3/JUCE Version
```bash
cd Builds/LinuxMakefile
make CONFIG=Release
```

### Build Pure LV2 Version
```bash
make -f Makefile.lv2
make -f Makefile.lv2 install
```

## Installation

The plugins will be installed to:
- VST3: `~/.vst3/SSL 4000 EQ.vst3`
- LV2 (JUCE): `~/.lv2/SSL 4000 EQ.lv2`
- LV2 (Pure): `~/.lv2/SSL_4000_EQ_Pure.lv2`

## Usage in Ardour

The pure LV2 version provides full inline display support in Ardour:
1. Add the plugin to a track
2. The EQ curve and controls appear directly in the mixer strip
3. Adjust parameters without opening the full GUI

## Technical Details

### DSP Implementation
- Biquad IIR filters with bilinear transform
- Authentic SSL console frequency response curves
- Analog saturation using tanh modeling
- Optimized for real-time performance

### Inline Display
- Cairo-based rendering
- 200x100 pixel frequency response display
- Real-time parameter visualization
- LV2 inline-display extension compliant

## License

This project is licensed under the GPL-2.0 License.

## Credits

Developed as an authentic emulation of the SSL 4000 series console EQ section.
SSL is a trademark of Solid State Logic.
