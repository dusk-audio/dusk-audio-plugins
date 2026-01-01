#!/usr/bin/env python3
"""
GrooveMind - Pattern Library Builder
Converts GMD and Pettinhouse patterns to unified metadata format.
"""

import os
import csv
import json
import hashlib
from pathlib import Path
from collections import defaultdict
import statistics
import shutil

try:
    import pretty_midi
except ImportError:
    print("Error: pretty_midi required. Run 'pip install pretty_midi'")
    exit(1)

# Output directories
OUTPUT_DIR = Path("library")
PATTERNS_DIR = OUTPUT_DIR / "patterns"
METADATA_FILE = OUTPUT_DIR / "index.json"

# MIDI note to instrument category mapping (GM standard)
INSTRUMENT_MAP = {
    35: 'kick', 36: 'kick',
    37: 'snare', 38: 'snare', 40: 'snare',
    42: 'hihat', 44: 'hihat', 46: 'hihat',
    41: 'toms', 43: 'toms', 45: 'toms', 47: 'toms', 48: 'toms', 50: 'toms',
    49: 'crash', 52: 'crash', 55: 'crash', 57: 'crash',
    51: 'ride', 53: 'ride', 59: 'ride',
}

# Style normalization
STYLE_MAP = {
    'afrobeat': 'afrobeat',
    'afrocuban': 'afrocuban',
    'blues': 'blues',
    'country': 'country',
    'dance': 'electronic',
    'disco': 'dance',
    'funk': 'funk',
    'gospel': 'gospel',
    'highlife': 'world',
    'hiphop': 'hiphop',
    'jazz': 'jazz',
    'latin': 'latin',
    'middleeastern': 'world',
    'neworleans': 'neworleans',
    'pop': 'pop',
    'punk': 'punk',
    'reggae': 'reggae',
    'rock': 'rock',
    'soul': 'soul',
    'shuffle': 'blues',
    'bossa': 'latin',
}


def calculate_energy(notes, duration, bpm):
    """Estimate energy level from note density and velocity"""
    if duration == 0:
        return 0.5

    # Notes per second
    density = len(notes) / duration

    # Average velocity
    avg_vel = statistics.mean([n.velocity for n in notes]) if notes else 64

    # Normalize: typical range is 2-15 notes/sec, velocity 40-120
    density_score = min(1.0, density / 12)
    velocity_score = (avg_vel - 40) / 80

    return round(min(1.0, max(0.0, (density_score * 0.6 + velocity_score * 0.4))), 2)


def calculate_complexity(notes, bars, bpm):
    """Estimate rhythmic complexity"""
    if not notes or bars == 0:
        return 0.5

    # Notes per bar
    notes_per_bar = len(notes) / bars

    # Unique pitch count
    unique_pitches = len(set(n.pitch for n in notes))

    # Velocity variation
    vel_std = statistics.stdev([n.velocity for n in notes]) if len(notes) > 1 else 0

    # Normalize
    density_score = min(1.0, notes_per_bar / 32)
    pitch_score = min(1.0, unique_pitches / 8)
    vel_score = min(1.0, vel_std / 30)

    return round((density_score * 0.4 + pitch_score * 0.3 + vel_score * 0.3), 2)


def calculate_groove_stats(notes, bpm):
    """Calculate groove characteristics from timing data"""
    if len(notes) < 2:
        return {'swing': 0, 'push_pull': 0, 'tightness': 0.5}

    beat_duration = 60.0 / bpm
    sixteenth = beat_duration / 4

    timing_offsets = []
    for note in notes:
        grid_pos = round(note.start / sixteenth) * sixteenth
        offset_ms = (note.start - grid_pos) * 1000
        timing_offsets.append(offset_ms)

    # Timing tightness (inverse of std dev)
    timing_std = statistics.stdev(timing_offsets) if len(timing_offsets) > 1 else 0
    tightness = max(0, 1 - (timing_std / 50))  # 50ms std = 0 tightness

    # Push/pull (mean offset)
    mean_offset = statistics.mean(timing_offsets)
    push_pull = max(-1, min(1, mean_offset / 20))  # ±20ms = ±1

    # Swing detection (comparing 8th note pairs)
    # TODO: More sophisticated swing detection
    swing = 0

    return {
        'swing': round(swing, 2),
        'push_pull': round(push_pull, 2),
        'tightness': round(tightness, 2)
    }


def detect_instruments(notes):
    """Detect which instruments are used in the pattern"""
    instruments = {
        'kick': False, 'snare': False, 'hihat': False,
        'ride': False, 'crash': False, 'toms': False, 'percussion': False
    }

    for note in notes:
        category = INSTRUMENT_MAP.get(note.pitch)
        if category and category in instruments:
            instruments[category] = True
        elif note.pitch not in INSTRUMENT_MAP:
            instruments['percussion'] = True

    return instruments


def detect_articulations(notes):
    """Detect special articulations"""
    articulations = {
        'ghost_notes': False,
        'rimshots': False,
        'brush_sweeps': False,
        'cross_stick': False,
        'open_hihat': False
    }

    for note in notes:
        # Ghost notes: low velocity snare
        if note.pitch in [38, 40] and note.velocity < 50:
            articulations['ghost_notes'] = True
        # Cross stick / rimshot
        if note.pitch == 37:
            articulations['cross_stick'] = True
        # Open hihat
        if note.pitch == 46:
            articulations['open_hihat'] = True
        # Brush sweep (Pettinhouse key 40 with specific velocity pattern)
        if note.pitch == 40:
            articulations['brush_sweeps'] = True

    return articulations


def tempo_feel(bpm):
    """Categorize tempo"""
    if bpm < 70:
        return 'ballad'
    elif bpm < 95:
        return 'slow'
    elif bpm < 125:
        return 'medium'
    elif bpm < 160:
        return 'uptempo'
    else:
        return 'fast'


def generate_pattern_id(source, style, pattern_type, index):
    """Generate unique pattern ID"""
    return f"{source}_{style}_{pattern_type}_{index:04d}"


def process_gmd_pattern(row, midi_path, index):
    """Process a single GMD pattern"""
    try:
        pm = pretty_midi.PrettyMIDI(str(midi_path))
    except Exception as e:
        print(f"  Error loading {midi_path}: {e}")
        return None

    # Collect all drum notes
    notes = []
    for instrument in pm.instruments:
        if instrument.is_drum:
            notes.extend(instrument.notes)

    if not notes:
        return None

    # Parse style
    style_parts = row['style'].split('/')
    main_style = STYLE_MAP.get(style_parts[0].lower(), style_parts[0].lower())
    substyle = style_parts[1] if len(style_parts) > 1 else None

    bpm = int(row['bpm'])
    duration = float(row['duration'])
    beat_duration = 60.0 / bpm
    bars = max(1, int(duration / (beat_duration * 4)))

    pattern_type = row['beat_type']
    pattern_id = generate_pattern_id('gmd', main_style, pattern_type, index)

    metadata = {
        'id': pattern_id,
        'name': f"{main_style.title()} {pattern_type.title()} {index}",
        'style': main_style,
        'substyle': substyle,
        'tempo': {
            'bpm': bpm,
            'range_min': max(40, bpm - 20),
            'range_max': min(300, bpm + 20),
            'feel': tempo_feel(bpm)
        },
        'time_signature': row['time_signature'].replace('-', '/'),
        'type': pattern_type,
        'section': 'any',
        'bars': bars,
        'energy': calculate_energy(notes, duration, bpm),
        'complexity': calculate_complexity(notes, bars, bpm),
        'groove': calculate_groove_stats(notes, bpm),
        'kit': 'acoustic',
        'instruments': detect_instruments(notes),
        'articulations': detect_articulations(notes),
        'source': {
            'dataset': 'gmd',
            'file': row['midi_filename'],
            'drummer_id': row['drummer'],
            'license': 'cc-by-4.0'
        },
        'tags': generate_tags(main_style, pattern_type, bpm, notes),
        'ml_features': {
            'velocity_mean': round(statistics.mean([n.velocity for n in notes]), 1),
            'velocity_std': round(statistics.stdev([n.velocity for n in notes]), 1) if len(notes) > 1 else 0,
            'note_density': round(len(notes) / duration, 2) if duration > 0 else 0,
        }
    }

    return metadata, midi_path


def process_pettinhouse_pattern(midi_path, style_folder, index):
    """Process a single Pettinhouse brush pattern"""
    try:
        pm = pretty_midi.PrettyMIDI(str(midi_path))
    except Exception as e:
        print(f"  Error loading {midi_path}: {e}")
        return None

    # Collect all notes
    notes = []
    for instrument in pm.instruments:
        notes.extend(instrument.notes)

    if not notes:
        return None

    # Parse folder for tempo and style
    parts = style_folder.split()
    style = parts[0].lower()
    main_style = STYLE_MAP.get(style, style)

    # Extract tempo from folder name
    bpm = 120
    for part in parts:
        if part.isdigit():
            bpm = int(part)
            break

    duration = pm.get_end_time()
    beat_duration = 60.0 / bpm
    bars = max(1, int(duration / (beat_duration * 4)))

    # Determine pattern type from path
    pattern_type = 'fill' if 'fill' in str(midi_path).lower() else 'beat'

    pattern_id = generate_pattern_id('brush', main_style, pattern_type, index)

    metadata = {
        'id': pattern_id,
        'name': f"Brush {main_style.title()} {pattern_type.title()} {index}",
        'style': main_style,
        'substyle': 'brush',
        'tempo': {
            'bpm': bpm,
            'range_min': max(40, bpm - 15),
            'range_max': min(200, bpm + 15),
            'feel': tempo_feel(bpm)
        },
        'time_signature': '4/4',
        'type': pattern_type,
        'section': 'any',
        'bars': bars,
        'energy': calculate_energy(notes, duration, bpm),
        'complexity': calculate_complexity(notes, bars, bpm),
        'groove': calculate_groove_stats(notes, bpm),
        'kit': 'brush',
        'instruments': detect_instruments(notes),
        'articulations': detect_articulations(notes),
        'source': {
            'dataset': 'pettinhouse',
            'file': str(midi_path.name),
            'license': 'royalty-free'
        },
        'tags': generate_tags(main_style, pattern_type, bpm, notes, is_brush=True),
        'ml_features': {
            'velocity_mean': round(statistics.mean([n.velocity for n in notes]), 1),
            'velocity_std': round(statistics.stdev([n.velocity for n in notes]), 1) if len(notes) > 1 else 0,
            'note_density': round(len(notes) / duration, 2) if duration > 0 else 0,
        }
    }

    return metadata, midi_path


def generate_tags(style, pattern_type, bpm, notes, is_brush=False):
    """Generate descriptive tags"""
    tags = [style]

    # Tempo tags
    if bpm < 80:
        tags.append('slow')
    elif bpm > 140:
        tags.append('fast')

    # Pattern type
    tags.append(pattern_type)

    # Kit type
    if is_brush:
        tags.extend(['brush', 'acoustic', 'jazz-feel'])

    # Instrument tags
    pitches = set(n.pitch for n in notes)
    if 51 in pitches or 53 in pitches:
        tags.append('ride')
    if 42 in pitches or 44 in pitches:
        tags.append('hihat')
    if any(p in [41, 43, 45, 47, 48, 50] for p in pitches):
        tags.append('toms')

    # Velocity-based tags
    avg_vel = statistics.mean([n.velocity for n in notes]) if notes else 64
    if avg_vel < 60:
        tags.append('soft')
    elif avg_vel > 100:
        tags.append('loud')

    return list(set(tags))


def main():
    print("="*60)
    print("GrooveMind Pattern Library Builder")
    print("="*60)

    # Create output directories
    OUTPUT_DIR.mkdir(exist_ok=True)
    PATTERNS_DIR.mkdir(exist_ok=True)

    all_metadata = []
    pattern_index = 0

    # Process GMD
    gmd_csv = Path("groove/info.csv")
    if gmd_csv.exists():
        print(f"\nProcessing Groove MIDI Dataset...")
        with open(gmd_csv, 'r') as f:
            reader = csv.DictReader(f)
            gmd_data = list(reader)

        for row in gmd_data:
            midi_path = Path("groove") / row['midi_filename']
            if midi_path.exists():
                result = process_gmd_pattern(row, midi_path, pattern_index)
                if result:
                    metadata, src_path = result

                    # Copy MIDI to library
                    dest_path = PATTERNS_DIR / f"{metadata['id']}.mid"
                    shutil.copy(src_path, dest_path)

                    all_metadata.append(metadata)
                    pattern_index += 1

                    if pattern_index % 100 == 0:
                        print(f"  Processed {pattern_index} patterns...")

        print(f"  GMD: {pattern_index} patterns processed")

    # Process Pettinhouse brush patterns
    brush_dir = Path("brush-patterns")
    if brush_dir.exists():
        print(f"\nProcessing Pettinhouse Brush Patterns...")
        brush_count = 0

        for midi_path in brush_dir.rglob("*.mid"):
            # Get style from parent folder
            style_folder = midi_path.parent.parent.name if midi_path.parent.name in ['Style', 'Fills', 'Bossa fills'] else midi_path.parent.name

            result = process_pettinhouse_pattern(midi_path, style_folder, pattern_index)
            if result:
                metadata, src_path = result

                # Copy MIDI to library
                dest_path = PATTERNS_DIR / f"{metadata['id']}.mid"
                shutil.copy(src_path, dest_path)

                all_metadata.append(metadata)
                pattern_index += 1
                brush_count += 1

        print(f"  Pettinhouse: {brush_count} brush patterns processed")

    # Save metadata index
    library_index = {
        'version': '1.0.0',
        'total_patterns': len(all_metadata),
        'styles': list(set(m['style'] for m in all_metadata)),
        'kits': list(set(m['kit'] for m in all_metadata)),
        'patterns': all_metadata
    }

    with open(METADATA_FILE, 'w') as f:
        json.dump(library_index, f, indent=2)

    print(f"\n" + "="*60)
    print(f"Library built successfully!")
    print(f"  Total patterns: {len(all_metadata)}")
    print(f"  Styles: {', '.join(library_index['styles'])}")
    print(f"  Kits: {', '.join(library_index['kits'])}")
    print(f"  Output: {METADATA_FILE}")
    print("="*60)


if __name__ == '__main__':
    main()
