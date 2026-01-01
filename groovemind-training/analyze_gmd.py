#!/usr/bin/env python3
"""
GrooveMind - Groove MIDI Dataset Analyzer
Analyzes the GMD dataset to understand patterns, styles, tempos, and drummer characteristics.
"""

import os
import csv
import json
from collections import defaultdict
from pathlib import Path
import statistics

# Try to import pretty_midi for detailed MIDI analysis
try:
    import pretty_midi
    HAS_PRETTY_MIDI = True
except ImportError:
    HAS_PRETTY_MIDI = False
    print("Note: pretty_midi not installed. Run 'pip install pretty_midi' for detailed MIDI analysis.")

# GM Drum Map (Standard General MIDI)
GM_DRUM_MAP = {
    35: 'Acoustic Bass Drum',
    36: 'Bass Drum 1',
    37: 'Side Stick',
    38: 'Acoustic Snare',
    39: 'Hand Clap',
    40: 'Electric Snare',
    41: 'Low Floor Tom',
    42: 'Closed Hi-Hat',
    43: 'High Floor Tom',
    44: 'Pedal Hi-Hat',
    45: 'Low Tom',
    46: 'Open Hi-Hat',
    47: 'Low-Mid Tom',
    48: 'Hi-Mid Tom',
    49: 'Crash Cymbal 1',
    50: 'High Tom',
    51: 'Ride Cymbal 1',
    52: 'Chinese Cymbal',
    53: 'Ride Bell',
    54: 'Tambourine',
    55: 'Splash Cymbal',
    56: 'Cowbell',
    57: 'Crash Cymbal 2',
    59: 'Ride Cymbal 2',
}

# Simplified drum categories
DRUM_CATEGORIES = {
    'kick': [35, 36],
    'snare': [38, 40, 37],  # Including side stick
    'hihat': [42, 44, 46],
    'tom': [41, 43, 45, 47, 48, 50],
    'crash': [49, 57, 55, 52],
    'ride': [51, 53, 59],
    'other': [39, 54, 56],  # Clap, tambourine, cowbell
}


class GMDAnalyzer:
    def __init__(self, groove_dir: str):
        self.groove_dir = Path(groove_dir)
        self.info_csv = self.groove_dir / 'info.csv'
        self.data = []
        self.stats = {}

    def load_metadata(self):
        """Load and parse info.csv"""
        with open(self.info_csv, 'r') as f:
            reader = csv.DictReader(f)
            self.data = list(reader)
        print(f"Loaded {len(self.data)} entries from info.csv")
        return self

    def analyze_basic_stats(self):
        """Analyze basic dataset statistics"""
        self.stats['total_files'] = len(self.data)

        # Drummers
        drummers = set(row['drummer'] for row in self.data)
        self.stats['drummers'] = sorted(drummers)
        self.stats['num_drummers'] = len(drummers)

        # Files per drummer
        drummer_counts = defaultdict(int)
        for row in self.data:
            drummer_counts[row['drummer']] += 1
        self.stats['files_per_drummer'] = dict(drummer_counts)

        # Styles
        styles = set(row['style'] for row in self.data)
        self.stats['styles'] = sorted(styles)
        self.stats['num_styles'] = len(styles)

        # Style categories (main style before /)
        style_categories = set(row['style'].split('/')[0] for row in self.data)
        self.stats['style_categories'] = sorted(style_categories)

        # Files per style
        style_counts = defaultdict(int)
        for row in self.data:
            style_counts[row['style']] += 1
        self.stats['files_per_style'] = dict(sorted(style_counts.items(), key=lambda x: -x[1]))

        # Files per style category
        style_cat_counts = defaultdict(int)
        for row in self.data:
            style_cat_counts[row['style'].split('/')[0]] += 1
        self.stats['files_per_style_category'] = dict(sorted(style_cat_counts.items(), key=lambda x: -x[1]))

        # Beat types
        beat_types = set(row['beat_type'] for row in self.data)
        self.stats['beat_types'] = sorted(beat_types)

        beat_type_counts = defaultdict(int)
        for row in self.data:
            beat_type_counts[row['beat_type']] += 1
        self.stats['files_per_beat_type'] = dict(beat_type_counts)

        # Time signatures
        time_sigs = set(row['time_signature'] for row in self.data)
        self.stats['time_signatures'] = sorted(time_sigs)

        time_sig_counts = defaultdict(int)
        for row in self.data:
            time_sig_counts[row['time_signature']] += 1
        self.stats['files_per_time_signature'] = dict(time_sig_counts)

        # Train/test/validation splits
        split_counts = defaultdict(int)
        for row in self.data:
            split_counts[row['split']] += 1
        self.stats['split_distribution'] = dict(split_counts)

        return self

    def analyze_tempo_stats(self):
        """Analyze tempo/BPM statistics"""
        bpms = [int(row['bpm']) for row in self.data]

        self.stats['tempo'] = {
            'min': min(bpms),
            'max': max(bpms),
            'mean': round(statistics.mean(bpms), 1),
            'median': statistics.median(bpms),
            'stdev': round(statistics.stdev(bpms), 1),
        }

        # BPM distribution buckets
        bpm_buckets = defaultdict(int)
        for bpm in bpms:
            bucket = (bpm // 10) * 10
            bpm_buckets[f"{bucket}-{bucket+9}"] += 1
        self.stats['tempo']['distribution'] = dict(sorted(bpm_buckets.items()))

        # Tempo by style category
        tempo_by_style = defaultdict(list)
        for row in self.data:
            style_cat = row['style'].split('/')[0]
            tempo_by_style[style_cat].append(int(row['bpm']))

        self.stats['tempo_by_style'] = {
            style: {
                'min': min(temps),
                'max': max(temps),
                'mean': round(statistics.mean(temps), 1),
            }
            for style, temps in tempo_by_style.items()
        }

        return self

    def analyze_duration_stats(self):
        """Analyze duration statistics"""
        durations = [float(row['duration']) for row in self.data]

        self.stats['duration'] = {
            'min_seconds': round(min(durations), 2),
            'max_seconds': round(max(durations), 2),
            'mean_seconds': round(statistics.mean(durations), 2),
            'total_hours': round(sum(durations) / 3600, 2),
        }

        # Duration by beat type
        duration_by_type = defaultdict(list)
        for row in self.data:
            duration_by_type[row['beat_type']].append(float(row['duration']))

        self.stats['duration_by_beat_type'] = {
            bt: {
                'mean_seconds': round(statistics.mean(durs), 2),
                'total_minutes': round(sum(durs) / 60, 1),
            }
            for bt, durs in duration_by_type.items()
        }

        return self

    def analyze_midi_content(self, sample_size: int = 50):
        """Analyze MIDI content using pretty_midi (if available)"""
        if not HAS_PRETTY_MIDI:
            print("Skipping MIDI content analysis (pretty_midi not installed)")
            return self

        print(f"\nAnalyzing MIDI content from {sample_size} sample files...")

        # Sample files across different styles and drummers
        sample_indices = []
        styles_seen = set()
        drummers_seen = set()

        for i, row in enumerate(self.data):
            style_cat = row['style'].split('/')[0]
            drummer = row['drummer']
            if row['beat_type'] == 'beat':  # Focus on beats, not fills
                if style_cat not in styles_seen or drummer not in drummers_seen:
                    sample_indices.append(i)
                    styles_seen.add(style_cat)
                    drummers_seen.add(drummer)
                    if len(sample_indices) >= sample_size:
                        break

        # Add more random samples if needed
        import random
        remaining = [i for i in range(len(self.data)) if i not in sample_indices and self.data[i]['beat_type'] == 'beat']
        random.shuffle(remaining)
        sample_indices.extend(remaining[:sample_size - len(sample_indices)])

        # Analyze MIDI files
        all_notes = defaultdict(int)
        velocity_stats = defaultdict(list)
        timing_offsets = []
        notes_per_bar = []

        for idx in sample_indices:
            row = self.data[idx]
            midi_path = self.groove_dir / row['midi_filename']

            if not midi_path.exists():
                continue

            try:
                pm = pretty_midi.PrettyMIDI(str(midi_path))

                for instrument in pm.instruments:
                    if instrument.is_drum:
                        for note in instrument.notes:
                            all_notes[note.pitch] += 1
                            velocity_stats[note.pitch].append(note.velocity)

                            # Calculate timing offset from grid (16th note grid)
                            bpm = int(row['bpm'])
                            beat_duration = 60.0 / bpm
                            sixteenth = beat_duration / 4

                            grid_position = round(note.start / sixteenth) * sixteenth
                            offset_ms = (note.start - grid_position) * 1000
                            timing_offsets.append(offset_ms)

                        # Notes per bar estimate
                        duration = float(row['duration'])
                        bars = duration / (4 * 60.0 / int(row['bpm']))  # 4 beats per bar
                        if bars > 0:
                            notes_per_bar.append(len(instrument.notes) / bars)

            except Exception as e:
                print(f"  Error parsing {midi_path}: {e}")
                continue

        # Compile MIDI analysis results
        self.stats['midi_analysis'] = {
            'samples_analyzed': len(sample_indices),
            'drum_hits_by_pitch': dict(sorted(all_notes.items(), key=lambda x: -x[1])),
            'drum_hits_by_name': {
                GM_DRUM_MAP.get(pitch, f'Unknown ({pitch})'): count
                for pitch, count in sorted(all_notes.items(), key=lambda x: -x[1])[:15]
            },
        }

        # Velocity statistics
        self.stats['midi_analysis']['velocity'] = {
            'overall_mean': round(statistics.mean([v for vels in velocity_stats.values() for v in vels]), 1),
            'overall_stdev': round(statistics.stdev([v for vels in velocity_stats.values() for v in vels]), 1),
        }

        # Timing offset statistics (humanization/groove)
        if timing_offsets:
            self.stats['midi_analysis']['timing_offsets_ms'] = {
                'mean': round(statistics.mean(timing_offsets), 2),
                'stdev': round(statistics.stdev(timing_offsets), 2),
                'min': round(min(timing_offsets), 2),
                'max': round(max(timing_offsets), 2),
            }

        # Notes per bar
        if notes_per_bar:
            self.stats['midi_analysis']['notes_per_bar'] = {
                'mean': round(statistics.mean(notes_per_bar), 1),
                'min': round(min(notes_per_bar), 1),
                'max': round(max(notes_per_bar), 1),
            }

        # Category analysis
        category_hits = defaultdict(int)
        for pitch, count in all_notes.items():
            for cat, pitches in DRUM_CATEGORIES.items():
                if pitch in pitches:
                    category_hits[cat] += count
                    break
            else:
                category_hits['other'] += count

        total_hits = sum(category_hits.values())
        self.stats['midi_analysis']['hits_by_category'] = {
            cat: {
                'count': count,
                'percentage': round(100 * count / total_hits, 1)
            }
            for cat, count in sorted(category_hits.items(), key=lambda x: -x[1])
        }

        return self

    def analyze_drummer_characteristics(self):
        """Analyze characteristics per drummer"""
        drummer_stats = defaultdict(lambda: {
            'styles': set(),
            'tempos': [],
            'durations': [],
            'beats': 0,
            'fills': 0,
        })

        for row in self.data:
            d = row['drummer']
            drummer_stats[d]['styles'].add(row['style'].split('/')[0])
            drummer_stats[d]['tempos'].append(int(row['bpm']))
            drummer_stats[d]['durations'].append(float(row['duration']))
            if row['beat_type'] == 'beat':
                drummer_stats[d]['beats'] += 1
            else:
                drummer_stats[d]['fills'] += 1

        self.stats['drummer_characteristics'] = {}
        for drummer, data in sorted(drummer_stats.items()):
            self.stats['drummer_characteristics'][drummer] = {
                'total_files': data['beats'] + data['fills'],
                'beats': data['beats'],
                'fills': data['fills'],
                'styles': sorted(data['styles']),
                'tempo_range': f"{min(data['tempos'])}-{max(data['tempos'])} BPM",
                'tempo_mean': round(statistics.mean(data['tempos']), 1),
                'total_duration_minutes': round(sum(data['durations']) / 60, 1),
            }

        return self

    def print_summary(self):
        """Print a human-readable summary"""
        print("\n" + "="*70)
        print("GROOVE MIDI DATASET ANALYSIS")
        print("="*70)

        print(f"\n## Overview")
        print(f"Total files: {self.stats['total_files']}")
        print(f"Drummers: {self.stats['num_drummers']}")
        print(f"Unique styles: {self.stats['num_styles']}")
        print(f"Style categories: {', '.join(self.stats['style_categories'])}")
        print(f"Total duration: {self.stats['duration']['total_hours']} hours")

        print(f"\n## Beat Types")
        for bt, count in self.stats['files_per_beat_type'].items():
            print(f"  {bt}: {count} ({100*count/self.stats['total_files']:.1f}%)")

        print(f"\n## Time Signatures")
        for ts, count in self.stats['files_per_time_signature'].items():
            print(f"  {ts}: {count}")

        print(f"\n## Tempo Statistics")
        t = self.stats['tempo']
        print(f"  Range: {t['min']} - {t['max']} BPM")
        print(f"  Mean: {t['mean']} BPM")
        print(f"  Median: {t['median']} BPM")
        print(f"  Std Dev: {t['stdev']} BPM")

        print(f"\n## Style Categories (by file count)")
        for style, count in list(self.stats['files_per_style_category'].items())[:10]:
            tempo_info = self.stats['tempo_by_style'].get(style, {})
            tempo_str = f"(tempo: {tempo_info.get('mean', '?')} avg)" if tempo_info else ""
            print(f"  {style}: {count} files {tempo_str}")

        print(f"\n## Files per Drummer")
        for drummer, count in sorted(self.stats['files_per_drummer'].items()):
            char = self.stats['drummer_characteristics'].get(drummer, {})
            print(f"  {drummer}: {count} files, {char.get('total_duration_minutes', '?')} min")

        print(f"\n## Train/Test/Validation Split")
        for split, count in self.stats['split_distribution'].items():
            print(f"  {split}: {count} ({100*count/self.stats['total_files']:.1f}%)")

        if 'midi_analysis' in self.stats:
            print(f"\n## MIDI Content Analysis")
            ma = self.stats['midi_analysis']
            print(f"  Samples analyzed: {ma['samples_analyzed']}")
            print(f"\n  Most common drum hits:")
            for drum, count in list(ma['drum_hits_by_name'].items())[:8]:
                print(f"    {drum}: {count}")

            print(f"\n  Hits by category:")
            for cat, info in ma['hits_by_category'].items():
                print(f"    {cat}: {info['percentage']}%")

            print(f"\n  Velocity: mean={ma['velocity']['overall_mean']}, stdev={ma['velocity']['overall_stdev']}")

            if 'timing_offsets_ms' in ma:
                to = ma['timing_offsets_ms']
                print(f"  Timing offsets: mean={to['mean']}ms, stdev={to['stdev']}ms")
                print(f"    (This is the 'groove' - deviation from quantized grid)")

            if 'notes_per_bar' in ma:
                npb = ma['notes_per_bar']
                print(f"  Notes per bar: mean={npb['mean']}, range={npb['min']}-{npb['max']}")

        print("\n" + "="*70)

    def save_json(self, output_path: str):
        """Save analysis results to JSON"""
        # Convert sets to lists for JSON serialization
        stats_json = json.loads(json.dumps(self.stats, default=list))

        with open(output_path, 'w') as f:
            json.dump(stats_json, f, indent=2)
        print(f"\nSaved analysis to {output_path}")


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Analyze Groove MIDI Dataset')
    parser.add_argument('--groove-dir', default='groove',
                        help='Path to groove dataset directory')
    parser.add_argument('--output', default='gmd_analysis.json',
                        help='Output JSON file')
    parser.add_argument('--midi-samples', type=int, default=100,
                        help='Number of MIDI files to analyze in detail')
    args = parser.parse_args()

    analyzer = GMDAnalyzer(args.groove_dir)
    analyzer.load_metadata()
    analyzer.analyze_basic_stats()
    analyzer.analyze_tempo_stats()
    analyzer.analyze_duration_stats()
    analyzer.analyze_drummer_characteristics()
    analyzer.analyze_midi_content(sample_size=args.midi_samples)

    analyzer.print_summary()
    analyzer.save_json(args.output)


if __name__ == '__main__':
    main()
