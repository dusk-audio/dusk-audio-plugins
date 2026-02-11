# Add Plugin

Scaffold a new Dusk Audio plugin with all required files and configurations.

## Usage

```
/add-plugin <plugin-name>
```

**Arguments:**
- `plugin-name`: Name for the new plugin (e.g., "Spectrum Analyzer", "Chord Detector")

**Examples:**
- `/add-plugin "Spectrum Analyzer"` - Create new Spectrum Analyzer plugin
- `/add-plugin "Gate"` - Create new Gate plugin

## Instructions

### 1. Gather Plugin Information

Ask the user for:

```
Creating new plugin: <plugin-name>

Please provide:
1. Plugin slug (lowercase, hyphenated): e.g., "spectrum-analyzer"
2. Brief description (1-2 sentences): What does this plugin do?
3. Plugin code (4 chars): Unique identifier, e.g., "SPEC"
4. Initial version: Usually "1.0.0"
5. Plugin type: Effect, Instrument, Analyzer, or Utility
```

### 2. Create Directory Structure

```bash
mkdir -p plugins/<slug>/
```

### 3. Generate CMakeLists.txt

Create `plugins/<slug>/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)

# Include version extraction module
include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)

# Get version from git tag or CI environment
set(<UPPERCASE_SLUG>_DEFAULT_VERSION "1.0.0")
get_plugin_version("<slug>" <UPPERCASE_SLUG>_VERSION)
if(NOT <UPPERCASE_SLUG>_VERSION OR <UPPERCASE_SLUG>_VERSION STREQUAL "0.0.0")
    set(<UPPERCASE_SLUG>_VERSION ${<UPPERCASE_SLUG>_DEFAULT_VERSION})
endif()

project(<PluginName> VERSION ${<UPPERCASE_SLUG>_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

juce_add_plugin(<PluginName>
    PLUGIN_NAME "<Display Name>"
    PLUGIN_CODE <CODE>
    PLUGIN_MANUFACTURER_CODE LunC
    FORMATS VST3 LV2 AU Standalone
    PRODUCT_NAME "<Display Name>"
    COMPANY_NAME "Dusk Audio"
    COMPANY_WEBSITE "https://dusk-audio.github.io"
    COMPANY_EMAIL "support@dusk-audio.github.io"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{HOME}/.vst3"
    LV2_COPY_DIR "$ENV{HOME}/.lv2"
    AU_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components"
)

set_plugin_version_defines(<PluginName> ${<UPPERCASE_SLUG>_VERSION})

target_sources(<PluginName> PRIVATE
    <PluginName>.cpp
    <PluginName>.h
    <PluginName>Editor.cpp
    <PluginName>Editor.h
    ../shared/LEDMeter.cpp
)

target_compile_definitions(<PluginName> PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
)

target_link_libraries(<PluginName>
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
```

### 4. Generate Processor Files

Create `plugins/<slug>/<PluginName>.h` and `plugins/<slug>/<PluginName>.cpp`:

Use existing plugins as templates (e.g., Velvet 90 for effects, Multi-Q for analysis).

Required components:
- `juce::AudioProcessor` subclass
- `juce::AudioProcessorValueTreeState` for parameters
- `prepareToPlay()`, `processBlock()`, `releaseResources()`
- Parameter definitions

### 5. Generate Editor Files

Create `plugins/<slug>/<PluginName>Editor.h` and `plugins/<slug>/<PluginName>Editor.cpp`:

Required components:
- `juce::AudioProcessorEditor` subclass
- Timer for UI updates (30-60 Hz)
- `paint()` and `resized()` methods
- **MUST USE shared components**:
  - `LEDMeter` for level metering
  - `SupportersOverlay` for Patreon credits
  - `ScalableEditorHelper` for resizable UI

### 6. Update Root CMakeLists.txt

Add to `CMakeLists.txt` in project root:

```cmake
# Add option
option(BUILD_<UPPERCASE_NAME> "Build <Plugin Name> plugin" ON)

# Add subdirectory (in the plugins section)
if(BUILD_<UPPERCASE_NAME>)
    add_subdirectory(plugins/<slug>)
endif()
```

### 7. Update Build Scripts

**docker/build_release.sh:**
Add shortcut alias:
```bash
"<shortcut>") PLUGINS="<slug>" ;;
```

**rebuild_all.sh:**
Add to PLUGIN_TARGETS array.

### 8. Add to Website

**_data/plugins.yml:**
```yaml
- name: "<Plugin Name>"
  slug: "<slug>"
  tagline: "<Brief tagline>"
  description: "<Full description>"
  status: "in-dev"
  featured: false
```

### 9. Verify Setup

```bash
# Configure
mkdir -p build && cd build && cmake ..

# Build
cmake --build . --target <PluginName>_All -j8
```

### 10. Report Completion

```
New plugin "<Plugin Name>" created successfully!

Files created:
- plugins/<slug>/CMakeLists.txt
- plugins/<slug>/<PluginName>.h
- plugins/<slug>/<PluginName>.cpp
- plugins/<slug>/<PluginName>Editor.h
- plugins/<slug>/<PluginName>Editor.cpp

Updated:
- CMakeLists.txt (root)
- docker/build_release.sh
- _data/plugins.yml (website)

Next steps:
1. Implement the DSP in <PluginName>.cpp
2. Design the UI in <PluginName>Editor.cpp
3. Test with: /build-plugin <shortcut>
4. Validate with: /validate-plugin <shortcut>
```

## Shared Code Requirements

**MANDATORY:** All new plugins MUST use these shared components:

| Component | Location | Purpose |
|-----------|----------|---------|
| LEDMeter | `shared/LEDMeter.h/cpp` | Input/output level meters |
| SupportersOverlay | `shared/SupportersOverlay.h` | Patreon credits (click title) |
| ScalableEditorHelper | `shared/ScalableEditorHelper.h` | Resizable UI with persistence |
| DuskLookAndFeel | `shared/DuskLookAndFeel.h` | Consistent styling |
| AnalogEmulation | `shared/AnalogEmulation/` | Saturation, tubes, transformers |

**Check `plugins/shared/` BEFORE writing any new code.**
