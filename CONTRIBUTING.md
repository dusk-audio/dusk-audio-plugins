# Contributing to Dusk Audio Plugins

Thanks for taking the time to contribute. This document covers what you need to know to build, work on, and submit changes to this repository.

## License

All contributions are licensed under [GPLv3](LICENSE) — the same license as the rest of the project. By submitting a pull request, you confirm that your contribution may be distributed under those terms.

Source files use SPDX identifiers:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
```

Add this line as the very first line of any new `*.h` or `*.cpp` file you create.

## Building from Source

### Prerequisites

- **CMake** ≥ 3.22
- **C++17** compiler (Clang 12+, GCC 11+, MSVC 2019+)
- **JUCE 7.x or 8.x** at `../JUCE/` (sibling directory to this repo)
- **Git** (for submodules)

Clone with submodules:

```bash
git clone --recursive https://github.com/dusk-audio/plugins.git
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

### macOS / Linux

```bash
mkdir -p build && cd build
cmake ..
cmake --build . --config Release --target <PluginName>_All -j8
```

Replace `<PluginName>` with the JUCE target (e.g. `MultiComp`, `FourKEQ`, `DuskAmp`, `DuskVerb`). Use `_AU`, `_VST3`, `_LV2`, or `_All` for individual formats or all of them.

JUCE installs the artefacts to:

- **macOS AU:** `~/Library/Audio/Plug-Ins/Components/`
- **macOS VST3:** `~/Library/Audio/Plug-Ins/VST3/`
- **Linux VST3:** `~/.vst3/`
- **Linux LV2:** `~/.lv2/`

### Windows

Use Visual Studio 2019+ with the "Desktop development with C++" workload. From a Developer Command Prompt:

```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release --target <PluginName>_All -j8
```

VST3 artefacts land in `C:\Program Files\Common Files\VST3\`.

### Cross-Platform via Docker

Reproducible builds for Linux + Windows artefacts:

```bash
./docker/build_release.sh duskamp        # or any other plugin slug
```

See `./docker/build_release.sh --help` for the full slug list.

### Faster Local Builds

Install [ccache](https://ccache.dev/):

- macOS: `brew install ccache`
- Linux: `sudo apt install ccache`

Then use the helper:

```bash
./rebuild_all.sh --fast
```

This enables Ninja + ccache automatically.

## Development Guidelines

Read [CLAUDE.md](CLAUDE.md) — it covers the project's conventions including:

- Audio thread rules (no allocation, no locks, no I/O in `processBlock`)
- Parameter setup pattern
- DSP lifecycle (`prepareToPlay`, smoothing, latency)
- State save/load pattern
- Common DSP patterns and shared library usage

The most important rule: **always check `plugins/shared/` before writing new utilities.** Several reusable components already exist (`LEDMeter`, `DuskLookAndFeel`, `Oversampling`, `UserPresetManager`, `SupportersOverlay`, `AnalogEmulation`).

## Validating a Change

Before opening a pull request:

```bash
# Build the plugin you changed
cmake --build build --config Release --target <PluginName>_All -j8

# Run validation
./tests/run_plugin_tests.sh --plugin "<PluginName>" --skip-audio

# Optional: full pluginval scan at strictness 10
pluginval --strictness-level 10 --validate <path-to-plugin-binary>
```

For DSP changes, also test in a real DAW (Logic, Reaper, Live, Bitwig, FL Studio) — DAW host quirks catch bugs that pluginval misses.

### DuskAmp audio regression (Phase 0.1 onward)

DuskAmp ships a deterministic golden-render regression suite that bit-compares all factory presets against a baseline. Use it whenever you touch any DSP file under `plugins/DuskAmp/src/`:

```bash
./tests/duskamp_regression_check.sh --build
```

The first run on a fresh checkout has no baseline — render once to bless your local set:

```bash
cmake --build build --target duskamp_golden_render -j8
./build/tests/duskamp_golden_render/duskamp_golden_render
```

After that, the regression script renders to a temp dir and md5-compares against the local baseline. **Bit-identical** = no regression. **Different** = either a real bug or an intentional DSP change that needs re-blessing (re-render to overwrite the local baseline). See [tests/golden_renders/README.md](tests/golden_renders/README.md) for the full workflow.

## Pull Request Checklist

- [ ] Builds cleanly on at least one platform (preferably your dev platform)
- [ ] `pluginval --strictness-level 10` passes (or you've documented why it shouldn't)
- [ ] No allocations, locks, or I/O in `processBlock` (`grep` it)
- [ ] No new files without an SPDX-License-Identifier header
- [ ] Existing presets still load (run the regression suite if you touched DSP)
- [ ] CLAUDE.md updated if you changed conventions or added shared code

## Reporting Bugs

Open an issue at https://github.com/dusk-audio/plugins/issues. Please include:

- Plugin name and version
- Host DAW and version
- OS and version
- Steps to reproduce
- The contents of the local crash log if the plugin crashed (open the log folder from the plugin's About panel)

## Questions

For architecture questions or design discussions, prefer GitHub Discussions over issues.

For urgent direct contact, reach the maintainers via the [Patreon](https://patreon.com/duskaudio) or the website at https://dusk-audio.github.io/.
