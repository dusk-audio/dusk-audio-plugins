# DrummerClone VST3

**A Logic Pro Drummer-inspired MIDI drum generator for Linux**

DrummerClone is an open-source VST3 plugin that generates intelligent, musical drum patterns in real-time. It features **Follow Mode** - the ability to analyze incoming audio or MIDI and adapt its groove to match your playing, making it feel like a real session drummer responding to your music.

## Features

- **Follow Mode**: Analyze audio transients or MIDI input to extract groove/timing
- **Multiple Drummer Personalities**: 12+ virtual drummers with unique styles
- **Style Selection**: Rock, HipHop, Alternative, R&B, Electronic, Trap, Songwriter
- **Real-time Pattern Generation**: Procedural drums that never repeat exactly
- **XY Pad Control**: Intuitive Swing/Intensity control
- **MIDI Output Only**: Route to any drum VST/sampler of your choice

## Why DrummerClone?

Linux has amazing DAWs (Ardour, Reaper, Bitwig) but lacks an equivalent to Logic Pro's Drummer feature. DrummerClone fills this gap by providing intelligent drum generation that:

1. **Follows your playing** - Connect a guitar, bass, or keyboard and the drums adapt
2. **Outputs standard MIDI** - Works with DrumGizmo, Hydrogen, sfizz, or commercial plugins via Yabridge
3. **Never loops obviously** - Uses Perlin noise and variation algorithms for natural feel
4. **Runs on Linux** - Native VST3, no Wine required

## Building

### Prerequisites

- JUCE 7+ (https://juce.com)
- CMake 3.16+
- GCC/Clang with C++17 support

### Build Steps

```bash
# Clone JUCE if you don't have it
git clone https://github.com/juce-framework/JUCE.git ~/JUCE

# Build DrummerClone
cd DrummerClone
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install (copies to ~/.vst3)
make install
```

### Alternative: Using Projucer

1. Open `DrummerClone.jucer` in Projucer
2. Set your JUCE module path
3. Export to your preferred IDE/build system
4. Build

## Usage

1. **Add to DAW**: Insert DrummerClone on a MIDI track
2. **Route MIDI Output**: Send DrummerClone's MIDI to your drum plugin/sampler
3. **Select Style**: Choose a style (Rock, HipHop, etc.)
4. **Select Drummer**: Each drummer has unique personality traits
5. **Enable Follow Mode** (optional):
   - Connect audio from a guitar/bass track, or
   - Route MIDI from a keyboard track
   - DrummerClone analyzes input and adapts

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

## Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Complexity | 1-10 | Pattern complexity (more fills, ghost notes) |
| Loudness | 0-100 | Overall velocity/intensity |
| Swing | 0-100 | Swing amount (overrides Follow groove) |
| Follow Mode | On/Off | Enable groove following |
| Follow Source | MIDI/Audio | What to analyze |
| Sensitivity | 0.1-0.8 | Transient detection threshold |
| Style | Menu | Musical genre |
| Drummer | Menu | Virtual drummer personality |

## Drummer Personalities

Each drummer has unique DNA affecting:
- **Aggression**: Hit velocity/intensity
- **Ghost Notes**: Subtle snare embellishments
- **Fill Hunger**: How often fills occur
- **Swing Bias**: Natural groove tendency
- **Simplicity**: Pattern complexity preference

## Technical Details

- **Format**: VST3 only
- **Audio I/O**: 1 mono input (for analysis), 1 mono output (silent)
- **MIDI**: Full MIDI input/output
- **Timing Resolution**: 960 PPQ
- **Analysis Buffer**: 2 seconds

## File Structure

```
DrummerClone/
├── Source/
│   ├── PluginProcessor.*     # Main audio processor
│   ├── PluginEditor.*        # UI
│   ├── DrumMapping.h         # GM drum note mappings
│   ├── TransientDetector.*   # Audio onset detection
│   ├── MidiGrooveExtractor.* # MIDI timing analysis
│   ├── GrooveTemplateGenerator.* # Groove extraction
│   ├── GrooveFollower.*      # Real-time smoothing
│   ├── DrummerEngine.*       # Pattern generation
│   ├── DrummerDNA.*          # Drummer personalities
│   ├── VariationEngine.*     # Anti-repetition system
│   └── FollowModePanel.*     # Follow UI component
├── CMakeLists.txt
└── README.md
```

## Roadmap

- [ ] More drummer personalities (target: 28)
- [ ] Step sequencer integration
- [ ] MIDI export button
- [ ] Song section detection
- [ ] Custom drummer profile editor
- [ ] AU format for macOS
- [ ] LV2 format

## License

[Add your license here - MIT/GPL recommended for open source]

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