# Build Plugin

Build Dusk Audio plugins using Docker containerized builds for consistent, distributable binaries.

## Usage

```
/build-plugin [plugin-name]
```

**Arguments:**
- `plugin-name` (optional): Plugin shortcut or slug. If not provided, shows available options.

**Examples:**
- `/build-plugin multiq` - Build Multi-Q plugin
- `/build-plugin 4keq` - Build 4K EQ plugin
- `/build-plugin` - Show available plugins and let user choose

## Instructions

### 1. Determine What to Build

If a plugin name was provided, map it to the build shortcut:

| Plugin Name | Shortcuts | Full Slug |
|-------------|-----------|-----------|
| 4K EQ | `4keq`, `4k-eq` | 4k-eq |
| Multi-Comp | `compressor`, `multi-comp`, `multicomp` | multi-comp |
| TapeMachine | `tape`, `tapemachine` | tapemachine |
| Tape Echo | `tapeecho`, `tape-echo` | tape-echo |
| Multi-Q | `multiq`, `multi-q` | multi-q |
| Velvet 90 | `velvet-90` | velvet-90 |
| Convolution Reverb | `convolution`, `convolution-reverb` | convolution-reverb |
| Neural Amp | `nam`, `neural-amp` | neural-amp |
| GrooveMind | `groovemind` | groovemind |

If no plugin provided, ask the user:
```
Which plugin would you like to build?
- 4keq (4K EQ)
- compressor (Multi-Comp)
- tape (TapeMachine)
- tapeecho (Tape Echo)
- multiq (Multi-Q)
- velvet-90 (Velvet 90)
- convolution (Convolution Reverb)
- nam (Neural Amp)
- all (Build all plugins)
```

### 2. Check Build Environment

Verify Docker/Podman is available:
```bash
which docker || which podman
```

If neither is available, inform the user they need to install Docker or Podman.

### 3. Run the Build

**Single plugin:**
```bash
./docker/build_release.sh <shortcut>
```

**All plugins:**
```bash
./docker/build_release.sh
```

Monitor the build output. The script will:
1. Pull/build the Docker image if needed
2. Compile the plugin(s)
3. Output to `release/` directory

### 4. Validate the Build (Automatic)

After successful build, automatically run pluginval:

```bash
./tests/run_plugin_tests.sh --plugin "<Plugin Name>" --skip-audio
```

Plugin name mapping for validation:
| Shortcut | Plugin Name for Validation |
|----------|---------------------------|
| 4keq | "4K EQ" |
| compressor | "Multi-Comp" |
| tape | "TapeMachine" |
| tapeecho | "Tape Echo" |
| multiq | "Multi-Q" |
| velvet-90 | "Velvet 90" |
| convolution | "Convolution Reverb" |
| nam | "Neural Amp" |

### 5. Report Results

**If build succeeds:**
```
Build completed successfully!

Plugin: <Plugin Name>
Output: release/<plugin-slug>/
  - VST3: <plugin>.vst3
  - LV2: <plugin>.lv2

Validation: PASSED (pluginval)
```

**If build fails:**
1. Show the error output
2. Suggest common fixes:
   - Missing include: Check file paths
   - Undefined reference: Add source to CMakeLists.txt
   - Compiler error: Check syntax in recent changes

**If validation fails:**
```
Build completed but validation FAILED.

The plugin compiled but pluginval found issues:
<error output>

This should be fixed before release.
```

## Build Output Location

All builds output to `release/` directory:
```
release/
├── <plugin-slug>/
│   ├── VST3/
│   │   └── <Plugin>.vst3/
│   └── LV2/
│       └── <Plugin>.lv2/
```

## Local Development Alternative

For quick local testing (macOS/Linux only, not for releases):
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target <PluginTarget>_All -j8
```

Build targets:
- `FourKEQ_All`, `MultiComp_All`, `TapeMachine_All`, `TapeEcho_All`
- `MultiQ_All`, `Velvet90_All`, `ConvolutionReverb_All`, `NeuralAmp_All`

**Note:** Local builds may not be compatible across Linux distributions due to glibc version differences. Use Docker builds for releases.
