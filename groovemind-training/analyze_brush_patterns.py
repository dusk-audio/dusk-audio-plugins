#!/usr/bin/env python3
"""
GrooveMind - Brush Pattern Analyzer
Analyzes brush drum MIDI patterns from Pettinhouse collection.
"""

import os
import json
from pathlib import Path
from collections import defaultdict
import statistics

try:
    import pretty_midi
    HAS_PRETTY_MIDI = True
except ImportError:
    HAS_PRETTY_MIDI = False
    print("Error: pretty_midi required. Run 'pip install pretty_midi'")
    exit(1)

# Pettinhouse Brush Kit MIDI Mapping (from SFZ analysis)
PETTINHOUSE_MAP = {
    36: 'Kick (Brush)',
    37: 'Rim Shot',
    38: 'Snare Tap (Brush)',
    39: 'Snare Tap Variant',
    40: 'Snare Brush Stroke/Swirl',  # Key articulation for brush feel
    41: 'Floor Tom (Brush)',
    42: 'Closed Hi-Hat',
    44: 'Pedal Hi-Hat',
    45: 'Mid Tom (Brush)',
    46: 'Open Hi-Hat',
    48: 'High Tom (Brush)',
    49: 'Crash (Brush)',
    51: 'Ride (Brush)',
    53: 'Ride Cup (Brush)',
}


class BrushPatternAnalyzer:
    def __init__(self, patterns_dir: str):
        self.patterns_dir = Path(patterns_dir)
        self.patterns = []
        self.stats = {}

    def load_all_patterns(self):
        """Load all MIDI files from the patterns directory"""
        midi_files = list(self.patterns_dir.rglob("*.mid"))
        print(f"Found {len(midi_files)} MIDI files")

        for midi_path in midi_files:
            try:
                pm = pretty_midi.PrettyMIDI(str(midi_path))

                # Extract path info for style/type classification
                rel_path = midi_path.relative_to(self.patterns_dir)
                parts = rel_path.parts

                # Determine style and type from folder structure
                style_folder = parts[0] if len(parts) > 0 else "unknown"
                pattern_type = "fill" if "fill" in str(midi_path).lower() else "beat"

                # Extract tempo from folder name (e.g., "Jazz 118 BPM")
                tempo = 120  # default
                for part in parts:
                    if "BPM" in part:
                        try:
                            tempo = int(part.split()[1])
                        except:
                            pass

                # Analyze notes
                notes_by_pitch = defaultdict(list)
                for instrument in pm.instruments:
                    for note in instrument.notes:
                        notes_by_pitch[note.pitch].append({
                            'start': note.start,
                            'end': note.end,
                            'velocity': note.velocity,
                            'duration': note.end - note.start
                        })

                pattern_data = {
                    'file': str(midi_path.name),
                    'path': str(rel_path),
                    'style': style_folder.split()[0].lower(),  # e.g., "jazz", "latin"
                    'tempo': tempo,
                    'type': pattern_type,
                    'duration': pm.get_end_time(),
                    'notes_by_pitch': dict(notes_by_pitch),
                    'total_notes': sum(len(v) for v in notes_by_pitch.values()),
                }

                self.patterns.append(pattern_data)

            except Exception as e:
                print(f"  Error parsing {midi_path}: {e}")

        print(f"Loaded {len(self.patterns)} patterns")
        return self

    def analyze_brush_technique(self):
        """Analyze how brush strokes (key 40) are used"""
        brush_stats = {
            'patterns_with_brush': 0,
            'avg_brush_hits_per_pattern': 0,
            'avg_brush_velocity': 0,
            'avg_brush_duration_ms': 0,
            'brush_interval_stats': {},
            'by_style': {},
        }

        all_brush_velocities = []
        all_brush_durations = []
        all_brush_intervals = []
        brush_by_style = defaultdict(lambda: {'count': 0, 'hits': []})

        for pattern in self.patterns:
            notes_40 = pattern['notes_by_pitch'].get(40, [])

            if notes_40:
                brush_stats['patterns_with_brush'] += 1
                brush_by_style[pattern['style']]['count'] += 1
                brush_by_style[pattern['style']]['hits'].extend(notes_40)

                for note in notes_40:
                    all_brush_velocities.append(note['velocity'])
                    all_brush_durations.append(note['duration'] * 1000)  # ms

                # Calculate intervals between brush strokes
                if len(notes_40) > 1:
                    starts = sorted([n['start'] for n in notes_40])
                    intervals = [starts[i+1] - starts[i] for i in range(len(starts)-1)]
                    all_brush_intervals.extend(intervals)

        if all_brush_velocities:
            brush_stats['avg_brush_velocity'] = round(statistics.mean(all_brush_velocities), 1)
        if all_brush_durations:
            brush_stats['avg_brush_duration_ms'] = round(statistics.mean(all_brush_durations), 1)
        if self.patterns:
            brush_stats['avg_brush_hits_per_pattern'] = round(
                sum(len(p['notes_by_pitch'].get(40, [])) for p in self.patterns) / len(self.patterns), 1
            )

        if all_brush_intervals:
            brush_stats['brush_interval_stats'] = {
                'mean_seconds': round(statistics.mean(all_brush_intervals), 3),
                'stdev_seconds': round(statistics.stdev(all_brush_intervals), 3) if len(all_brush_intervals) > 1 else 0,
                'min_seconds': round(min(all_brush_intervals), 3),
                'max_seconds': round(max(all_brush_intervals), 3),
            }

        # Per-style stats
        for style, data in brush_by_style.items():
            if data['hits']:
                brush_stats['by_style'][style] = {
                    'patterns': data['count'],
                    'total_hits': len(data['hits']),
                    'avg_velocity': round(statistics.mean([h['velocity'] for h in data['hits']]), 1),
                }

        self.stats['brush_technique'] = brush_stats
        return self

    def analyze_overall_stats(self):
        """Compute overall pattern statistics"""
        self.stats['total_patterns'] = len(self.patterns)

        # By style
        style_counts = defaultdict(int)
        for p in self.patterns:
            style_counts[p['style']] += 1
        self.stats['patterns_by_style'] = dict(style_counts)

        # By type
        type_counts = defaultdict(int)
        for p in self.patterns:
            type_counts[p['type']] += 1
        self.stats['patterns_by_type'] = dict(type_counts)

        # Tempo distribution
        tempos = [p['tempo'] for p in self.patterns]
        self.stats['tempo'] = {
            'values': list(set(tempos)),
            'distribution': {str(t): tempos.count(t) for t in set(tempos)}
        }

        # Duration stats
        durations = [p['duration'] for p in self.patterns]
        self.stats['duration'] = {
            'min_seconds': round(min(durations), 2),
            'max_seconds': round(max(durations), 2),
            'mean_seconds': round(statistics.mean(durations), 2),
        }

        # Note usage across all patterns
        note_usage = defaultdict(int)
        for p in self.patterns:
            for pitch, notes in p['notes_by_pitch'].items():
                note_usage[pitch] += len(notes)

        self.stats['note_usage'] = {
            PETTINHOUSE_MAP.get(pitch, f'Note {pitch}'): count
            for pitch, count in sorted(note_usage.items(), key=lambda x: -x[1])
        }

        return self

    def print_summary(self):
        """Print analysis summary"""
        print("\n" + "="*70)
        print("BRUSH PATTERN ANALYSIS (Pettinhouse Collection)")
        print("="*70)

        print(f"\n## Overview")
        print(f"Total patterns: {self.stats['total_patterns']}")
        print(f"Styles: {', '.join(self.stats['patterns_by_style'].keys())}")

        print(f"\n## Patterns by Style")
        for style, count in sorted(self.stats['patterns_by_style'].items()):
            print(f"  {style}: {count}")

        print(f"\n## Patterns by Type")
        for ptype, count in self.stats['patterns_by_type'].items():
            print(f"  {ptype}: {count}")

        print(f"\n## Tempo Distribution")
        for tempo, count in self.stats['tempo']['distribution'].items():
            print(f"  {tempo} BPM: {count} patterns")

        print(f"\n## Duration")
        d = self.stats['duration']
        print(f"  Range: {d['min_seconds']}s - {d['max_seconds']}s")
        print(f"  Mean: {d['mean_seconds']}s")

        print(f"\n## Note Usage (Pettinhouse Mapping)")
        for note, count in list(self.stats['note_usage'].items())[:10]:
            print(f"  {note}: {count}")

        bt = self.stats.get('brush_technique', {})
        if bt:
            print(f"\n## Brush Stroke Analysis (Key 40)")
            print(f"  Patterns using brush strokes: {bt['patterns_with_brush']}/{self.stats['total_patterns']}")
            print(f"  Avg brush hits per pattern: {bt['avg_brush_hits_per_pattern']}")
            print(f"  Avg brush velocity: {bt['avg_brush_velocity']}")
            print(f"  Avg brush duration: {bt['avg_brush_duration_ms']}ms")

            if bt.get('brush_interval_stats'):
                bi = bt['brush_interval_stats']
                print(f"\n  Brush stroke intervals:")
                print(f"    Mean: {bi['mean_seconds']*1000:.0f}ms")
                print(f"    Std:  {bi['stdev_seconds']*1000:.0f}ms")
                print(f"    Range: {bi['min_seconds']*1000:.0f}ms - {bi['max_seconds']*1000:.0f}ms")

            if bt.get('by_style'):
                print(f"\n  Brush usage by style:")
                for style, data in bt['by_style'].items():
                    print(f"    {style}: {data['patterns']} patterns, {data['total_hits']} hits, vel={data['avg_velocity']}")

        print("\n" + "="*70)

    def save_json(self, output_path: str):
        """Save analysis to JSON"""
        with open(output_path, 'w') as f:
            json.dump(self.stats, f, indent=2)
        print(f"\nSaved analysis to {output_path}")


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Analyze Brush MIDI Patterns')
    parser.add_argument('--patterns-dir', default='brush-patterns',
                        help='Path to brush patterns directory')
    parser.add_argument('--output', default='brush_analysis.json',
                        help='Output JSON file')
    args = parser.parse_args()

    analyzer = BrushPatternAnalyzer(args.patterns_dir)
    analyzer.load_all_patterns()
    analyzer.analyze_overall_stats()
    analyzer.analyze_brush_technique()
    analyzer.print_summary()
    analyzer.save_json(args.output)


if __name__ == '__main__':
    main()
