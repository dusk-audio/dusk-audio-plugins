# DrummerClone Pattern Library

## Free MIDI Drum Pattern Resources

Download royalty-free MIDI drum patterns from these sources and place them in this directory.
The plugin will automatically import them on startup.

### Recommended Sources (All Royalty-Free)

1. **LANDR - 10 Essential Drum Beats**
   - URL: https://blog.landr.com/drum-beats/
   - Includes: Rock, Amen Break, Bossa Nova, Jazz, Trap, Afrobeats, Reggaeton, Funk
   - Download: https://app.landr.com/projects/6a9e04e4-cd6d-4215-bc9b-bd446d517a95/

2. **Prosonic Studios**
   - URL: https://www.prosonic-studios.com/midi-drum-beats
   - Categories: Blues, Country, Funk, Hard Rock, Metal, Jazz, Latin, Pop, R&B, Hip-Hop, Reggae, Techno
   - Free samples available from each category

3. **Odd Grooves**
   - URL: https://oddgrooves.com/free-midi-drum-jams/
   - 32-bar MIDI drum grooves in various styles
   - Tower of Power tributes, funk grooves

4. **Ghost Audio Factory**
   - URL: https://ghostaudiofactory.com/midi-drumbeats-free
   - Hip-Hop, Rap, Boom Bap, Detroit, Metal patterns

5. **MIDI Drum Files**
   - URL: https://mididrumfiles.com/
   - 950+ patterns, 100% royalty-free

6. **MIDI Mighty**
   - URL: https://midimighty.com/blogs/resources/midi-drum-patterns
   - Various genres, downloadable MIDI

## Directory Structure

Organize patterns by style for automatic categorization:

```
patterns/
├── rock/
│   ├── basic_rock_01.mid
│   └── driving_rock_02.mid
├── hiphop/
│   ├── boom_bap_01.mid
│   └── trap_beat_01.mid
├── electronic/
│   └── house_4otf_01.mid
├── jazz/
│   └── swing_brush_01.mid
├── rnb/
│   └── neosoul_01.mid
└── fills/
    ├── rock_fill_01.mid
    └── tom_roll_01.mid
```

## Supported Formats

- `.mid` / `.midi` - Standard MIDI files (Type 0 or Type 1)
- `.json` - DrummerClone JSON pattern format

## JSON Pattern Format

```json
{
  "id": "my_custom_pattern",
  "style": "Rock",
  "category": "groove",
  "tags": "heavy,driving",
  "bars": 1,
  "energy": 0.8,
  "hits": [
    {"tick": 0, "element": "kick", "velocity": 110, "duration": 120},
    {"tick": 960, "element": "snare", "velocity": 105, "duration": 120},
    {"tick": 1920, "element": "kick", "velocity": 100, "duration": 120},
    {"tick": 2880, "element": "snare", "velocity": 108, "duration": 120}
  ]
}
```

## Tick Reference (960 PPQ)

- Beat = 960 ticks
- 8th note = 480 ticks
- 16th note = 240 ticks
- 32nd note = 120 ticks

## Element Names

- `kick` - Bass drum (MIDI 36)
- `snare` - Snare drum (MIDI 38)
- `hihat` / `hh` - Closed hi-hat (MIDI 42)
- `hihat_open` / `hho` - Open hi-hat (MIDI 46)
- `crash` - Crash cymbal (MIDI 49)
- `ride` - Ride cymbal (MIDI 51)
- `tom_high` / `tom1` - High tom (MIDI 48)
- `tom_mid` / `tom2` - Mid tom (MIDI 45)
- `tom_low` / `tom3` - Low tom (MIDI 43)
- `tom_floor` / `tom4` - Floor tom (MIDI 41)
- `clap` - Hand clap (MIDI 39)
