# DrummerClone VST3

**A Logic Pro Drummer-inspired MIDI drum generator for Linux**

DrummerClone is an open-source VST3/LV2 plugin that generates intelligent, musical drum patterns in real-time. It features **Follow Mode** - the ability to analyze incoming audio or MIDI and adapt its groove to match your playing, making it feel like a real session drummer responding to your music.

## Features

### Core Features
- **Follow Mode**: Analyze audio transients or MIDI input to extract groove/timing in real-time
- **Multiple Drummer Personalities**: 12+ virtual drummers with unique styles and characteristics
- **Style Selection**: Rock, HipHop, Alternative, R&B, Electronic, Trap, Songwriter
- **Real-time Pattern Generation**: Procedural drums that never repeat exactly
- **Section-Aware Patterns**: Intro, Verse, Pre-Chorus, Chorus, Bridge, Breakdown, Outro with automatic pattern variation
- **MIDI CC Control**: Control sections and fills via MIDI CC for DAW automation
- **MIDI Export**: Export generated patterns to standard MIDI files

### Pattern Generation
- **Intelligent Fills**: Automatic fills with configurable frequency, intensity, and length
- **Ghost Notes**: Subtle snare embellishments for groove
- **Cymbal Patterns**: Hi-hats, rides, and crashes with section-appropriate placement
- **Variation Engine**: Anti-repetition system using Perlin noise for natural feel

### Humanization
- **Timing Variation**: Adjustable timing jitter for human feel
- **Velocity Variation**: Dynamic velocity randomization
- **Push/Drag**: Adjust feel to play ahead or behind the beat
- **Groove Depth**: How strongly the groove template affects output

### User Interface
- **XY Pad Control**: Intuitive Swing/Intensity control (Logic Pro style)
- **Follow Mode Panel**: Visual groove lock indicator and source selection
- **Fills Panel**: Frequency, intensity, length controls with manual trigger button
- **Step Sequencer**: Visual pattern editing (collapsible)
- **Details Panel**: Fine-grained pattern control (kick patterns, snare patterns, hi-hat open amount)
- **MIDI Control Panel**: Configure CC numbers for section and fill control

## Why DrummerClone?

Linux has amazing DAWs (Ardour, Reaper, Bitwig) but lacks an equivalent to Logic Pro's Drummer feature. DrummerClone fills this gap by providing intelligent drum generation that:

1. **Follows your playing** - Connect a guitar, bass, or keyboard and the drums adapt
2. **Outputs standard MIDI** - Works with DrumGizmo, Hydrogen, sfizz, or commercial plugins via Yabridge
3. **Never loops obviously** - Uses Perlin noise and variation algorithms for natural feel
4. **Supports DAW automation** - MIDI CC control for section changes like Logic Pro
5. **Runs on Linux** - Native VST3 and LV2, no Wine required

## Building

### Prerequisites

- JUCE 7+ (https://juce.com)
- CMake 3.16+
- GCC/Clang with C++17 support

### Build Steps

The easiest way to build is using the rebuild script from the repository root:
```bash
# From the plugins repository root
./rebuild_all.sh
```

Or build just DrummerClone manually:
```bash
# From the plugins repository root
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target DrummerClone_All -j$(nproc)
```

Plugins are automatically installed to:
- VST3: `~/.vst3/DrummerClone.vst3`
- LV2: `~/.lv2/DrummerClone.lv2`

## Usage

### Basic Setup
1. **Add to DAW**: Insert DrummerClone on a MIDI track
2. **Route MIDI Output**: Send DrummerClone's MIDI to your drum plugin/sampler
3. **Select Style**: Choose a style (Rock, HipHop, etc.)
4. **Select Drummer**: Each drummer has unique personality traits
5. **Press Play**: DrummerClone generates patterns in sync with your DAW

### Follow Mode
1. **Enable Follow Mode** in the Follow Mode panel
2. **Select Source**: Choose MIDI or Audio
3. **Adjust Sensitivity**: Set transient detection threshold
4. **Route Input**:
   - For Audio: Send audio from a guitar/bass track to DrummerClone's sidechain
   - For MIDI: Route MIDI from a keyboard track to DrummerClone's input
5. **Watch Groove Lock**: The indicator shows how well the groove is locked

### MIDI CC Control (Cross-DAW Section Automation)
DrummerClone supports MIDI CC control for section changes, allowing automation in any DAW:

1. **Enable MIDI Control**: Click "MIDI Control" button in the sidebar
2. **Configure CC Numbers**:
   - Section CC# (default: 102) - Controls current section
   - Fill Trigger CC# (default: 103) - Triggers fills
3. **Create MIDI CC Automation** in your DAW on a track routed to DrummerClone
4. **Section CC Values**:
   - 0-17: Intro
   - 18-35: Verse
   - 36-53: Pre-Chorus
   - 54-71: Chorus
   - 72-89: Bridge
   - 90-107: Breakdown
   - 108-127: Outro
5. **Fill Trigger**: Any CC value > 64 triggers a fill

When section is controlled via MIDI, a green "MIDI" indicator appears next to the Section dropdown.

### Routing Example (Ardour)

```
Track 1: Guitar (Audio)
    |
    v
Track 2: DrummerClone (MIDI FX)
    |
    +---> Audio Sidechain to DrummerClone
    |
    v
Track 3: DrumGizmo (Instrument)
```

### MIDI Export
1. Select number of bars to export (4, 8, 16, or 32)
2. Click "Export MIDI" button
3. Choose save location
4. Import the .mid file into your DAW

## Parameters

### Main Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| Complexity | 1-10 | Pattern complexity (more fills, ghost notes) |
| Loudness | 0-100 | Overall velocity/intensity |
| Style | Menu | Musical genre (Rock, HipHop, etc.) |
| Drummer | Menu | Virtual drummer personality |
| Section | Menu | Current song section |

### Follow Mode Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| Follow Mode | On/Off | Enable groove following |
| Follow Source | MIDI/Audio | What to analyze |
| Sensitivity | 0.1-0.8 | Transient detection threshold |

### Fill Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| Frequency | 0-100% | Chance of fill per bar |
| Intensity | 0-100% | Fill complexity and velocity |
| Length | 1/2/4 beats | Duration of fills |
| FILL! button | - | Manual fill trigger |

### Humanization Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| Timing | 0-100% | Timing variation amount |
| Velocity | 0-100% | Velocity randomization |
| Push/Drag | -50 to +50 | Play ahead/behind the beat |
| Groove | 0-100% | Groove template influence |

### MIDI CC Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| Enable MIDI CC | On/Off | Enable MIDI CC control |
| Section CC# | 1-127 | CC number for section control |
| Fill Trigger CC# | 1-127 | CC number for fill trigger |

## Drummer Personalities

Each drummer has unique DNA affecting:
- **Aggression**: Hit velocity/intensity
- **Ghost Notes**: Subtle snare embellishments
- **Fill Hunger**: How often fills occur
- **Swing Bias**: Natural groove tendency
- **Simplicity**: Pattern complexity preference
- **Tom Love**: Tom usage in fills
- **Ride Preference**: Ride vs hi-hat preference
- **Crash Happiness**: Crash cymbal frequency
- **Laid Back**: Timing feel (behind/on top of beat)

### Included Drummers (29 Total)

**Rock (5)**: Kyle, Anders, Max, Ricky, Jake
**Alternative (4)**: Logan, Aidan, River, Quinn
**HipHop (4)**: Austin, Tyrell, Marcus, Kira
**R&B (4)**: Brooklyn, Darnell, Aaliyah, Andre
**Electronic (4)**: Niklas, Lexi, Sasha, Felix
**Songwriter (4)**: Jesse, Maya, Emily, Sam
**Trap (4)**: Xavier, Jayden, Zion, Luna

## Technical Details

- **Format**: VST3 and LV2
- **Audio I/O**: 1 mono input (for Follow Mode analysis), 1 mono output (silent)
- **MIDI**: Full MIDI input/output
- **Timing Resolution**: 960 PPQ
- **Analysis Buffer**: 2 seconds for Follow Mode
- **Platform**: Linux (x86_64)

## File Structure

```
DrummerClone/
├── Source/
│   ├── PluginProcessor.*        # Main audio/MIDI processor
│   ├── PluginEditor.*           # UI components
│   ├── DrumMapping.h            # GM drum note mappings
│   ├── TransientDetector.*      # Audio onset detection (Follow Mode)
│   ├── MidiGrooveExtractor.*    # MIDI timing analysis (Follow Mode)
│   ├── GrooveTemplateGenerator.* # Groove extraction
│   ├── GrooveFollower.*         # Real-time groove smoothing
│   ├── DrummerEngine.*          # Pattern generation core
│   ├── DrummerDNA.*             # Drummer personalities (29 profiles)
│   ├── VariationEngine.*        # Anti-repetition system
│   ├── FollowModePanel.*        # Follow Mode UI component
│   ├── StepSequencer.*          # Step sequencer (wired to engine)
│   ├── ProfileEditorPanel.*     # Custom drummer profile editor
│   └── MidiExporter.*           # MIDI file export
├── CMakeLists.txt
├── README.md
└── PROJECT_STATUS.md
```

## Roadmap

- [x] Follow Mode (Audio + MIDI)
- [x] Section-aware patterns
- [x] Fills with configurable frequency/intensity
- [x] Humanization controls
- [x] MIDI export
- [x] MIDI CC control for sections
- [x] LV2 format
- [x] Step sequencer wiring (UI connected to engine)
- [x] 29 drummer personalities across 7 styles
- [x] Custom drummer profile editor (save/load JSON profiles)
- [ ] AU format for macOS

## License

GPL-3.0 License

## Contributing

Contributions welcome! Areas that need help:
- More drummer profiles/styles
- UI improvements
- Testing on various Linux DAWs
- Documentation

## Credits

Inspired by Apple's Logic Pro Drummer feature.
Built with [JUCE](https://juce.com).

---

*DrummerClone is not affiliated with Apple Inc. "Logic Pro" and "Drummer" are trademarks of Apple Inc.*
*Company: Luna Co. Audio*
