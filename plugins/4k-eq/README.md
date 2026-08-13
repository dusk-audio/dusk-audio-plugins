# 4K EQ - Classic British Console EQ Emulation

Professional 4-band parametric equalizer with analog modeling, built with JUCE for VST3/LV2/AU/CLAP/Standalone formats.

![Plugin Type](https://img.shields.io/badge/Type-EQ%20%2F%20Filter-blue)
![Formats](https://img.shields.io/badge/Formats-VST3%20%7C%20LV2%20%7C%20AU%20%7C%20CLAP%20%7C%20Standalone-green)
![JUCE](https://img.shields.io/badge/JUCE-7%2B-orange)

## Features

### EQ Section
- **4-band parametric EQ** (Low, Low-Mid, High-Mid, High)
- **Brown/Black modes** with separately measured frequency, gain, Q, shelf/bell, and filter laws
- **Shared-stage interaction modeling** for the original LF/LM and HM/HF circuit pairs
- **High-pass/Low-pass filters** with measured mode-specific slopes, insertion trim, and control laws
- **Bell/Shelf switching** on LF and HF bands

### Processing Quality
- **1x/2x/4x oversampling** - Anti-aliased processing (4x default)
- **Measured native EQ-stage coloration and headroom** - mode-dependent low-level harmonics and calibrated overload rails
- **Native console nonlinearity** - fixed Brown/Black coloration driven by the Input control, matching the reference signal flow
- **Auto-gain compensation toggle** - Optional automatic output adjustment to maintain perceived loudness

### User Interface
- **Real-time spectrum analyzer** - FFT-based frequency visualization (30 Hz)
- **Color-coded knobs**:
  - 🔴 Red: Gain controls
  - 🟢 Green: Frequency controls
  - 🔵 Blue: Q controls
  - 🟠 Orange: Filters
- **Professional tick markings** - British-style graduated scales with labeled frequency/Q values
  - LF: 30, 50, 100, 200, 300, 400, 480 Hz
  - LMF: 200, 300, 800, 1k, 1.5k, 2k, 2.5k Hz
  - HMF: 600, 800, 1.5k, 3k, 4.5k, 6k, 7k Hz
  - HF: 1.5k, 2k, 5k, 8k, 10k, 14k, 16k Hz
  - Q: 4, 3, 2, 1.5, 1, .5, .4
  - Gain knobs: 0dB center indicator highlighted
- **Clear labeling** - All knobs labeled (GAIN, FREQ, Q, HPF, LPF, INPUT, OUTPUT)
- **Mouse wheel support** - Scroll to adjust knobs
- **Double-click to edit** - Type an exact value into any knob (Ctrl/Cmd+click resets to default)
- **Preset browser** - 14 factory presets + user state saving
- **Auto-gain button** - Toggle automatic gain compensation on/off

## Factory Presets

`INIT` provides the flat default. The preset browser contains these 14 calibrated
factory programs; all listed frequencies are audible/effective targets rather
than internal control coordinates.

1. **Vocal Presence** - HPF 80 Hz; +3 dB at 100 Hz, -3 dB at 300 Hz, +4 dB at 3.5 kHz, +2 dB at 8 kHz
2. **Kick Punch** - HPF 30 Hz; +4 dB at 50 Hz, -2.5 dB at 200 Hz, +3 dB at 2 kHz
3. **Snare Crack** - HPF 150 Hz; +4 dB at 250 Hz, +5 dB at 5 kHz, +3 dB bell at 8 kHz
4. **Drum Bus Punch** - Black mode; +4 dB at 70 Hz, -3 dB at 350 Hz, +3 dB at 3.5 kHz, +2.5 dB at 10 kHz
5. **Bass Warmth** - LPF 10 kHz; +4 dB at 80 Hz, -3 dB at 400 Hz, +2 dB at 1.5 kHz
6. **Bass Guitar Polish** - HPF 35 Hz; +5 dB at 60 Hz, -2 dB at 250 Hz, +3 dB at 1.2 kHz, +2 dB bell at 4.5 kHz
7. **Acoustic Guitar** - HPF 80 Hz; -2 dB at 100 Hz, +2 dB at 200 Hz, +3 dB at 2.5 kHz, +4 dB at 12 kHz
8. **Piano Brilliance** - HPF 30 Hz; +2 dB at 80 Hz, -2.5 dB at 500 Hz, +3 dB at 2 kHz, +3.5 dB at 8 kHz
9. **Bright Mix** - +2 dB at 60 Hz, -2 dB at 2.5 kHz, +2.5 dB at 10 kHz
10. **Glue Bus** - +2 dB at 100 Hz, -1.5 dB at 3 kHz, +2 dB at 10 kHz
11. **Telephone EQ** - HPF 300 Hz, LPF 3 kHz, +6 dB at 1 kHz
12. **Air & Silk** - +3 dB at 7 kHz, +4 dB at 15 kHz
13. **Master Sheen** - +1 dB at 5 kHz, +1.5 dB at 16 kHz
14. **Master Bus Sweetening** - +1 dB at 50 Hz, -1 dB at 600 Hz, +0.5 dB at 4 kHz, +1.5 dB at 15 kHz, -0.5 dB output

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
- **Filter topology**: Biquad IIR with British-style coefficient shaping
- **Frequency warping**: Pre-warped for HF accuracy (prevents digital cramping)
- **Nonlinearity model**: Measured, mode-dependent native console coloration and overload rails; level is driven by Input
- **Sample rates**: 44.1kHz - 192kHz (auto-limits oversampling at >96kHz)
- **Latency**:
  - 2x oversampling: ~32 samples
  - 4x oversampling: ~96 samples (auto-disabled at high sample rates)

### Parameter Ranges (Console Hardware-Accurate)
- **LF/HF Gain**: ±15dB
- **LF Freq**: 30-450Hz | **HF Freq**: 1.5kHz-16kHz
- **LM Freq**: 200-2500Hz | **HM Freq**: 600-7000Hz
- **Q Range**: 0.5-3.0
- **HPF**: Out, 16-350Hz | **LPF**: Out, 15.201kHz-3kHz
- **Input/Output Gain**: ±12dB
- **Auto-Gain**: ON/OFF toggle (default OFF)

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
cd /path/to/Dusk/plugins
./rebuild_all.sh              # Standard build
./rebuild_all.sh --fast        # With ccache (if available)
./rebuild_all.sh --debug       # Debug build
./rebuild_all.sh --parallel 8  # Specify job count
```

#### Manual CMake Build
```bash
cd /path/to/Dusk/plugins
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target FourKEQ_All -j8
```

#### Build Targets
- `FourKEQ_VST3` - VST3 plugin only
- `FourKEQ_LV2` - LV2 plugin only
- `FourKEQ_AU` - AU plugin (macOS only)
- `FourKEQ_CLAP` - CLAP plugin only
- `FourKEQ_Standalone` - Standalone application
- `FourKEQ_All` - All formats

### Installation Paths
- **VST3**: `~/.vst3/4K EQ.vst3` (Linux), `~/Library/Audio/Plug-Ins/VST3/` (macOS)
- **LV2**: `~/.lv2/4K EQ.lv2`
- **AU**: `~/Library/Audio/Plug-Ins/Components/4K EQ.component` (macOS)
- **CLAP**: `~/.clap/4K EQ.clap` (Linux), `~/Library/Audio/Plug-Ins/CLAP/` (macOS), `%COMMONPROGRAMFILES%\CLAP\4K EQ.clap` (Windows)

## Development

### Project Structure
```
4k-eq/
├── FourKEQ.cpp              # Audio processor (DSP engine)
├── FourKEQ.h                # Processor header
├── PluginEditor.cpp         # GUI implementation (includes spectrum analyzer)
├── PluginEditor.h           # Editor header
├── FourKLookAndFeel.cpp     # Custom British-style UI theme
├── FourKLookAndFeel.h       # Look and feel header
├── ConsoleSaturation.h      # Console saturation modeling
├── PatreonBackers.h         # Patreon supporters credits list
├── CMakeLists.txt           # Build configuration
└── README.md                # This file
```

### Known Issues & Limitations
- **Bundle ID warning** (macOS): Cosmetic only, doesn't affect functionality
- **High sample rate oversampling**: Auto-limited to 2x at >96kHz to prevent CPU overload
- **LV2 inline display**: Removed due to JUCE compatibility issues (full GUI still works)

### Performance Notes
- **CPU usage**: ~1-2% per instance (2x oversampling, 48kHz), ~3-4% with 4x
- **Recommendation**: Use 2x oversampling for most tracks, 4x for critical mastering if needed
- **Optimization**: Install `ccache` for faster rebuilds (`./rebuild_all.sh --fast`)
- **Memory**: ~10MB per instance (includes oversampling buffers)
- **Efficiency**: Highly optimized - can be used on every channel in a mix without CPU issues

## Changelog

### v2.0.11 (2026-08-12)

- Migrated all 14 factory programs from legacy control coordinates to their
  intended audible frequencies using the measured Brown/Black calibration.
- Versioned new user-preset files with an explicit effective-Hz frequency
  domain while preserving legacy user presets and existing DAW sessions.
- Made factory recall deterministic by disabling Auto Gain and removed the
  retired M/S compatibility slot from newly saved presets.
- Corrected the documented factory-preset list and settings.

### v2.0.10 (2026-08-12)

- Tightened `4K-2 EQ` into a single left-aligned nameplate title while keeping
  the version as the smaller right-aligned element.

### v2.0.9 (2026-08-12)

- Matched the `EQ` nameplate label to the size and baseline of `4K-2`.

### v2.0.8 (2026-08-12)

- Simplified the header lockup to prioritize the model name: `4K-2`, `EQ`,
  then the version, removing the duplicated `4K` label.

### v2.0.7 (2026-08-12)

- Calibrated all six frequency-knob pointers, hover values, dragging, and typed
  entry to the measured effective frequency. Hidden Brown/Black compensation
  coordinates remain internal, so the matched DSP response is unchanged while
  every visible frequency now agrees with the response graph and FFT.

### v2.0.6 (2026-08-12)

- Removed and neutralized the non-SSL M/S option while preserving its hidden
  parameter slot for session compatibility.
- Unified panel and header buttons with the silver SHELF-button treatment;
  Brown/Black remains the only colored selector, with a true-black Black mode.

### v2.0.5 (2026-08-11)

- Aligned the Input and Output controls with the first and second EQ knob rows,
  and moved Bypass and Auto Gain beneath the master knobs.

### v2.0.4 (2026-08-11)
- Added unlabeled minor bezel dots across the two wide upper gaps on every EQ frequency dial

### v2.0.3 (2026-08-11)
- Removed the user-facing Drive control; Input now sits above Output in the Master section
- Preserved the old saturation parameter index as an inert compatibility slot for existing sessions
- Native Brown/Black nonlinearity remains fixed and responds to Input level like the reference SSL path
- Neutral EQ stages now bypass exactly and clear recursive state, eliminating the sub-40 Hz numerical floor

### v1.0.10 (2026-05-08)
- ✅ Unified double-click-to-edit value entry across all knobs
- ✅ Added CLAP plugin format support
- ✅ Miscellaneous shared LookAndFeel polish

### v1.0.9 (2026-04-08)
- ✅ Maintenance release (shared code and build updates)

### v1.0.8 (2026-02-04)
- ✅ FabFilter-style Shift+drag fine control (5x finer)
- ✅ Velocity-sensitive dragging for natural feel
- ✅ Ctrl/Cmd+click to reset knobs to default
- ✅ Fixed jerky fine control

### v1.0.7 (2026-02-03)
- ✅ Window size now persists between sessions
- ✅ Adopted shared ScalableEditorHelper for consistent resize behavior

### v1.0.6 (2026-01-29)
- ✅ Selectable dB range for the EQ graph (±12/±24/±30/±60 dB, Warped)
- ✅ Expanded EQ graph height and cleaner FILTERS section layout
- ✅ British-style IN button positioning; width-only UI scaling fix

### v1.0.5 (2026-01-28)
- ✅ Automatic website version update on release-tag push

### v1.0.4 (2026-01-27)
- ✅ Cmd/Ctrl+drag fine control
- ✅ LED mono/stereo detection

### v1.0.2 (2025-10-21) - Professional Console Accuracy Update
- ✅ **CRITICAL**: Fixed frequency ranges to match console hardware specs
  - LF: 30-480Hz (was 20-600Hz)
  - HF: 1.5kHz-16kHz (was 1.5kHz-20kHz)
- ✅ **CRITICAL**: Limited Q ranges to realistic values (0.4-4.0, was 0.4-5.0)
- ✅ **CRITICAL**: Set default saturation to 0% (clean unless driven)
- ✅ **CRITICAL**: Fixed tick mark positioning (90° coordinate correction)
- ✅ Added auto-gain compensation toggle with UI button
- ✅ Optimized saturation wet/dry mix for more audible effect
- ✅ Enhanced UI readability:
  - Larger, brighter frequency labels (9.5pt bold, near-white color)
  - All knobs labeled (GAIN, FREQ, Q, HPF, LPF, OUTPUT, DRIVE)
  - Specific frequency values on all tick marks
  - Reduced knob sizes (70×70) for better label visibility
  - Improved vertical spacing and alignment
- ✅ Added 5 new factory presets (total: 15)
- ✅ Comprehensive README and documentation updates

### v1.0.1 (2025-10-02)
- ✅ Removed LV2 inline display (JUCE compatibility)
- ✅ Added mouse wheel support for knobs
- ✅ Added double-click reset to defaults
- ✅ Added professional knob tick markings (British-style)
- ✅ Added pre/post spectrum toggle
- ✅ Added "Master Sheen" factory preset
- ✅ SIMD-optimized spectrum analyzer (~5% CPU reduction)
- ✅ Fixed CMakeLists.txt duplicate warnings
- ✅ Comprehensive documentation updates

### v1.0.0 (2025-09)
- Initial release with VST3/LV2/AU support
- Brown/Black modes (E-series/G-series)
- 2x/4x oversampling
- Real-time spectrum analyzer
- 10 factory presets

## License

This plugin is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. See the [LICENSE](../../LICENSE) file in the repository root for the full license text.

**JUCE Framework**: This plugin uses JUCE, which is available under a GPL/Commercial dual-license. When distributed under GPL-3.0, JUCE's GPL license terms apply. For commercial (closed-source) distribution, a JUCE commercial license is required.

## Credits

**Developed by**: Dusk Audio

**Disclaimer**: This is an independent emulation inspired by classic British console EQs. This project is not affiliated with or endorsed by any hardware manufacturer.

---

## 💖 Special Thanks to Our Patreon Backers

This plugin is made possible by the generous support of our Patreon community:

### 🌟 Platinum Supporters
<!-- Add your platinum tier backers here -->
- *Your name could be here!*

### ⭐ Gold Supporters
<!-- Add your gold tier backers here -->
- *Your name could be here!*

### ✨ Silver Supporters
<!-- Add your silver tier backers here -->
- *Your name could be here!*

### 💙 Supporters
<!-- Add all other backers here -->
- *Your name could be here!*

**Want to support development?** [Become a Patreon backer](https://patreon.com/YourPatreonPage) and get your name listed here, plus early access to new plugins and exclusive presets!

## Support

For issues, feature requests, or questions:
- Check `CLAUDE.md` for project documentation
- Review `LV2_INLINE_DISPLAY_NOTES.md` for LV2-specific info
- Build logs available in `build/` directory

---

*Part of the Dusk Audio plugin suite*
