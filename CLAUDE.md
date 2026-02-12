# Dusk Audio Plugins - Reference Documentation

## Quick Reference

| Task | Command |
|------|---------|
| Release a plugin | `/release-plugin <slug> [version]` |
| Build a plugin | `/build-plugin <slug>` |
| Validate a plugin | `/validate-plugin <slug>` |
| Add new plugin | `/add-plugin <name>` |

## Project Overview

Professional audio VST3/LV2/AU plugins built with JUCE. Published as "Dusk Audio".

**Website**: https://luna-co-software.github.io/lunacoaudio.github.io/ (will become https://dusk-audio.github.io/ after GitHub org rename)
**Website repo**: `~/projects/lunacoaudio.github.io/`

## Plugins

| Plugin | Slug | Directory | Description |
|--------|------|-----------|-------------|
| 4K EQ | `4k-eq` | `plugins/4k-eq/` | British console EQ emulation |
| Multi-Comp | `multi-comp` | `plugins/multi-comp/` | 8-mode compressor + multiband |
| TapeMachine | `tapemachine` | `plugins/TapeMachine/` | Analog tape emulation |
| Tape Echo | `tape-echo` | `plugins/tape-echo/` | Classic tape delay |
| Multi-Q | `multi-q` | `plugins/multi-q/` | Universal EQ (Digital/British/Tube) |
| Velvet 90 | `velvet-90` | `plugins/Velvet90/` | Algorithmic reverb |
| Convolution Reverb | `convolution-reverb` | `plugins/convolution-reverb/` | IR-based reverb |
| Neural Amp | `neural-amp` | `plugins/neural-amp/` | Neural amp modeler (NAM) |
| GrooveMind | `groovemind` | `plugins/groovemind/` | ML drum generator (future) |

## Version Management & Releasing

### MANDATORY: Always use `/release-plugin` for version bumps and tags

**NEVER manually bump versions or create tags.** Always use the `/release-plugin` skill which automatically:
1. Bumps CMakeLists.txt version(s)
2. Updates the website `_data/plugins.yml`
3. Commits both repos
4. Creates annotated git tag(s) with changelog
5. Pushes everything (commits + tags + website)

```bash
# Single plugin release
/release-plugin multi-comp 1.2.4

# Auto-increment patch version
/release-plugin 4k-eq

# Batch release (patch bump all)
/release-plugin 4k-eq multi-comp tapemachine multi-q
```

**Version locations** (managed by `/release-plugin`):

| Plugin | CMakeLists.txt Variable |
|--------|------------------------|
| 4K EQ | `FOURKEQ_DEFAULT_VERSION` |
| Multi-Comp | `MULTICOMP_DEFAULT_VERSION` |
| TapeMachine | `TAPEMACHINE_DEFAULT_VERSION` |
| Multi-Q | `project(MultiQ VERSION X.Y.Z)` (inline) |
| Others | `<NAME>_DEFAULT_VERSION` |

**Website**: `~/projects/lunacoaudio.github.io/_data/plugins.yml` (path will change after org rename) - updated automatically by `/release-plugin`

## Shared Code (MANDATORY)

**Before writing ANY new code, check `plugins/shared/` first!**

| Component | File | Use For |
|-----------|------|---------|
| LEDMeter | `LEDMeter.h/cpp` | All level meters |
| SupportersOverlay | `SupportersOverlay.h` | Patreon credits (click title) |
| DuskSlider | `DuskLookAndFeel.h` | Rotary/slider controls with fine control |
| DuskTooltips | `DuskLookAndFeel.h` | Consistent tooltip text |
| ScalableEditorHelper | `ScalableEditorHelper.h` | Resizable UI with persistence |
| AnalogEmulation | `AnalogEmulation/*.h` | Saturation, tubes, transformers |

## Build System

**Release builds**: GitHub Actions (automatic on tag push)
**Local builds**: `./docker/build_release.sh <shortcut>`

| Shortcut | Plugin |
|----------|--------|
| `4keq` | 4K EQ |
| `compressor` | Multi-Comp |
| `tape` | TapeMachine |
| `tapeecho` | Tape Echo |
| `multiq` | Multi-Q |
| `velvet-90` | Velvet 90 |
| `convolution` | Convolution Reverb |
| `nam` | Neural Amp |

**Validation**: `./tests/run_plugin_tests.sh --plugin "<Name>" --skip-audio`

## Project Structure

```
plugins/
├── plugins/
│   ├── 4k-eq/
│   ├── multi-comp/
│   ├── TapeMachine/
│   ├── tape-echo/
│   ├── multi-q/
│   ├── Velvet90/
│   ├── convolution-reverb/
│   ├── neural-amp/
│   └── shared/           # SHARED CODE - CHECK HERE FIRST
├── docker/
│   └── build_release.sh  # Primary build script
├── tests/
│   └── run_plugin_tests.sh
└── CMakeLists.txt
```

## Common DSP Patterns

- **Oversampling**: `juce::dsp::Oversampling<float>`
- **Filters**: `juce::dsp::IIR::Filter`
- **Metering**: `std::atomic<float>` with relaxed ordering
- **Smoothing**: `juce::SmoothedValue` or APVTS smoothing
- **Saturation**: Use `AnalogEmulation` library

## Code Style

- C++17
- JUCE naming: camelCase methods, PascalCase classes
- `PARAM_*` constants for parameter IDs
- Header-only for small utilities
- Separate DSP classes from editor

## JUCE

- **Location**: `../JUCE/` (sibling directory)
- **Modules**: audio_processors, audio_utils, dsp, gui_basics

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build fails | Check Docker is running, rebuild container |
| Plugin not in DAW | Check `~/.vst3/`, rescan in DAW |
| Validation fails | Run pluginval locally, check parameters |

---
*Dusk Audio | CMake + JUCE 7.x | Shared code in `plugins/shared/`*
