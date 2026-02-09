#!/usr/bin/env python3
"""
Convert batch optimization JSON results to SilkVerbPresets.h C++ code.

Reads all individual result JSON files from the results directory and generates
a complete SilkVerbPresets.h header with factory presets organized by category.

Usage:
    # Generate from results directory
    python convert_results_to_presets.py --input results/

    # Filter by minimum score
    python convert_results_to_presets.py --input results/ --min-score 80

    # Custom output path
    python convert_results_to_presets.py --input results/ --output /tmp/SilkVerbPresets.h

    # Preview without writing
    python convert_results_to_presets.py --input results/ --preview
"""

import argparse
import glob
import json
import math
import os
import sys
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


# Mode string → int mapping
MODE_MAP = {
    'Plate': 0, 'Room': 1, 'Hall': 2, 'Chamber': 3, 'Cathedral': 4,
    'Ambience': 5, 'Bright Hall': 6, 'Chorus Space': 7,
    'Random Space': 8, 'Dirty Hall': 9,
}

MODE_NAMES = {v: k for k, v in MODE_MAP.items()}

# Color string → int mapping
COLOR_MAP = {'1970s': 0, '1980s': 1, 'Now': 2}
COLOR_NAMES = {v: k for k, v in COLOR_MAP.items()}

# Default mix values by category (optimizer runs at 100% wet,
# but presets need a user-facing mix level)
DEFAULT_MIX = {
    'Halls': 0.30,
    'Rooms': 0.22,
    'Plates': 0.28,
    'Creative': 0.35,
}


def size_string_to_normalized(size_str: str) -> float:
    """Convert size string like '2.0s' to normalized 0-1 value.

    JUCE formula: seconds = 0.3 + value^1.5 * 9.7
    Inverse: value = ((seconds - 0.3) / 9.7)^(1/1.5)
    """
    seconds = float(size_str.replace('s', ''))
    if seconds <= 0.3:
        return 0.0
    if seconds >= 10.0:
        return 1.0
    return math.pow((seconds - 0.3) / 9.7, 1.0 / 1.5)


def er_late_to_normalized(er_late: str) -> float:
    """Convert ER/Late string to normalized 0-1 value.

    JUCE display: 'E{(1-value)*100}/L{value*100}'
    So 'E50/L50' → 0.5, 'E30/L70' → 0.7, 'Early' → 0.0, 'Late' → 1.0
    """
    if er_late == 'Early':
        return 0.0
    if er_late == 'Late':
        return 1.0
    try:
        parts = er_late.split('/')
        e = int(parts[0][1:])
        return 1.0 - e / 100.0
    except (ValueError, IndexError):
        return 0.7  # default


def params_to_preset(result: dict) -> dict:
    """Convert optimizer result dict to SilkVerbPresets.h Preset struct values.

    The optimizer uses pedalboard parameter ranges. Pedalboard exposes
    JUCE 0-1 range params as 0-100 (percentage via display string).
    This function converts back to the Preset struct's native ranges.
    """
    p = result['params']
    category = result.get('category', 'Halls')

    return {
        'name': result['target_name'],
        'category': category,
        'mode': MODE_MAP.get(p['mode'], 2),
        'color': COLOR_MAP.get(p['color'], 2),
        'size': round(size_string_to_normalized(p['size']), 4),
        'damping': round(p['damping'] / 100.0, 4),
        'predelay': round(p['pre_delay_ms'], 1),
        'mix': DEFAULT_MIX.get(category, 0.30),
        'modRate': round(p['mod_rate_hz'], 2),
        'modDepth': round(p['mod_depth'] / 100.0, 4),
        'width': round(p['width'] / 100.0, 4),
        'earlyDiff': round(p['early_diff'] / 100.0, 4),
        'lateDiff': round(p['late_diff'] / 100.0, 4),
        'bassMult': round(p['bass_mult_x'], 2),
        'bassFreq': round(p['bass_freq_hz'], 1),
        'lowCut': round(p['low_cut_hz'], 1),
        'highCut': round(p['high_cut_hz'], 1),
        'freeze': p.get('freeze', False),
        'roomSize': round(p['room_size'] / 100.0, 4),
        'earlyLateBal': round(er_late_to_normalized(p.get('er_late', 'E30/L70')), 4),
        'highDecay': round(p['hf_decay_x'], 2),
        'midDecay': round(p.get('mid_decay_x', 1.0), 2),
        'highFreq': round(p.get('high_freq_hz', 4000.0), 1),
        'erShape': round(p.get('er_shape', 50.0) / 100.0, 4),
        'erSpread': round(p.get('er_spread', 50.0) / 100.0, 4),
        'erBassCut': round(p.get('er_bass_cut_hz', 20.0), 1),
        'score': result.get('score', 0),
        'description': result.get('description', ''),
    }


def format_preset_cpp(preset: dict) -> str:
    """Format a single preset as C++ struct initialization."""
    mode_name = MODE_NAMES.get(preset['mode'], 'Hall')
    color_name = COLOR_NAMES.get(preset['color'], 'Now')

    lines = []
    desc = preset.get('description', '')
    score = preset.get('score', 0)
    if desc:
        lines.append(f'    // {desc} (match: {score:.0f}%)')
    lines.append('    presets.push_back({')
    lines.append(f'        "{preset["name"]}",')
    lines.append(f'        "{preset["category"]}",')
    lines.append(f'        {preset["mode"]},  // {mode_name}')
    lines.append(f'        {preset["color"]},  // {color_name}')
    lines.append(f'        {preset["size"]:.4f}f,  // size')
    lines.append(f'        {preset["damping"]:.4f}f,  // damping')
    lines.append(f'        {preset["predelay"]:.1f}f,  // predelay ms')
    lines.append(f'        {preset["mix"]:.2f}f,  // mix')
    lines.append(f'        {preset["modRate"]:.2f}f,  // modRate Hz')
    lines.append(f'        {preset["modDepth"]:.4f}f,  // modDepth')
    lines.append(f'        {preset["width"]:.4f}f,  // width')
    lines.append(f'        {preset["earlyDiff"]:.4f}f,  // earlyDiff')
    lines.append(f'        {preset["lateDiff"]:.4f}f,  // lateDiff')
    lines.append(f'        {preset["bassMult"]:.2f}f,  // bassMult')
    lines.append(f'        {preset["bassFreq"]:.1f}f,  // bassFreq Hz')
    lines.append(f'        {preset["lowCut"]:.1f}f,  // lowCut Hz')
    lines.append(f'        {preset["highCut"]:.1f}f,  // highCut Hz')
    lines.append(f'        false,')
    lines.append(f'        {preset["roomSize"]:.4f}f,  // roomSize')
    lines.append(f'        {preset["earlyLateBal"]:.4f}f,  // earlyLateBal')
    lines.append(f'        {preset["highDecay"]:.2f}f,  // highDecay')
    lines.append(f'        {preset["midDecay"]:.2f}f,  // midDecay')
    lines.append(f'        {preset["highFreq"]:.1f}f,  // highFreq Hz')
    lines.append(f'        {preset["erShape"]:.4f}f,  // erShape')
    lines.append(f'        {preset["erSpread"]:.4f}f,  // erSpread')
    lines.append(f'        {preset["erBassCut"]:.1f}f  // erBassCut Hz')
    lines.append('    });')
    return '\n'.join(lines)


def generate_header(presets_by_category: dict, stats: dict) -> str:
    """Generate the complete SilkVerbPresets.h file content."""
    total = sum(len(v) for v in presets_by_category.values())

    header = f"""#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace SilkVerbPresets
{{

//==============================================================================
struct Preset
{{
    juce::String name;
    juce::String category;
    int mode;           // 0=Plate..5=Ambience, 6=BrightHall, 7=ChorusSpace, 8=RandomSpace, 9=DirtyHall
    int color;          // 0=1970s, 1=1980s, 2=Now

    float size;         // 0.0 - 1.0
    float damping;      // 0.0 - 1.0
    float predelay;     // 0 - 250 ms
    float mix;          // 0.0 - 1.0
    float modRate;      // 0.1 - 5.0 Hz
    float modDepth;     // 0.0 - 1.0
    float width;        // 0.0 - 1.0
    float earlyDiff;    // 0.0 - 1.0
    float lateDiff;     // 0.0 - 1.0
    float bassMult;     // 0.5 - 2.0
    float bassFreq;     // 100 - 1000 Hz
    float lowCut;       // 20 - 500 Hz
    float highCut;      // 1000 - 20000 Hz
    bool freeze;        // typically false for presets
    float roomSize = 0.5f;      // 0.0 - 1.0 (default center)
    float earlyLateBal = 0.7f;  // 0.0 = all ER, 1.0 = all late
    float highDecay = 1.0f;     // 0.25 - 4.0
    float midDecay = 1.0f;      // 0.25 - 4.0
    float highFreq = 4000.0f;   // 1000 - 12000 Hz
    float erShape = 0.5f;       // 0.0 - 1.0
    float erSpread = 0.5f;      // 0.0 - 1.0
    float erBassCut = 20.0f;    // 20 - 500 Hz
}};

//==============================================================================
// PCM 90-inspired categories: algorithm type grouping
inline const juce::StringArray Categories = {{
    "Halls",
    "Rooms",
    "Plates",
    "Creative"
}};

//==============================================================================
// {total} factory presets matched from Lexicon PCM 90 impulse responses
// Generated: {stats.get('date', 'unknown')}
// Average match score: {stats.get('avg_score', 0):.1f}%
inline std::vector<Preset> getFactoryPresets()
{{
    std::vector<Preset> presets;
    presets.reserve({total});
"""

    # Add presets by category
    for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
        cat_presets = presets_by_category.get(cat, [])
        if not cat_presets:
            continue

        header += f'\n    // {"=" * 20} {cat.upper()} ({len(cat_presets)}) {"=" * 20}\n\n'

        for preset in cat_presets:
            header += format_preset_cpp(preset) + '\n\n'

    header += """    return presets;
}

//==============================================================================
inline void applyPreset(juce::AudioProcessorValueTreeState& params, const Preset& preset)
{
    // Mode (10 choices: normalize by 9.0)
    if (auto* p = params.getParameter("mode"))
        p->setValueNotifyingHost(preset.mode / 9.0f);

    // Color (3 choices: normalize by 2.0)
    if (auto* p = params.getParameter("color"))
        p->setValueNotifyingHost(preset.color / 2.0f);

    // Continuous parameters — use convertTo0to1 for ranged params
    if (auto* p = params.getParameter("size"))
        p->setValueNotifyingHost(preset.size);

    if (auto* p = params.getParameter("damping"))
        p->setValueNotifyingHost(preset.damping);

    if (auto* p = params.getParameter("predelay"))
        p->setValueNotifyingHost(params.getParameterRange("predelay").convertTo0to1(preset.predelay));

    if (auto* p = params.getParameter("mix"))
        p->setValueNotifyingHost(preset.mix);

    if (auto* p = params.getParameter("modrate"))
        p->setValueNotifyingHost(params.getParameterRange("modrate").convertTo0to1(preset.modRate));

    if (auto* p = params.getParameter("moddepth"))
        p->setValueNotifyingHost(preset.modDepth);

    if (auto* p = params.getParameter("width"))
        p->setValueNotifyingHost(preset.width);

    if (auto* p = params.getParameter("earlydiff"))
        p->setValueNotifyingHost(preset.earlyDiff);

    if (auto* p = params.getParameter("latediff"))
        p->setValueNotifyingHost(preset.lateDiff);

    if (auto* p = params.getParameter("bassmult"))
        p->setValueNotifyingHost(params.getParameterRange("bassmult").convertTo0to1(preset.bassMult));

    if (auto* p = params.getParameter("bassfreq"))
        p->setValueNotifyingHost(params.getParameterRange("bassfreq").convertTo0to1(preset.bassFreq));

    if (auto* p = params.getParameter("lowcut"))
        p->setValueNotifyingHost(params.getParameterRange("lowcut").convertTo0to1(preset.lowCut));

    if (auto* p = params.getParameter("highcut"))
        p->setValueNotifyingHost(params.getParameterRange("highcut").convertTo0to1(preset.highCut));

    if (auto* p = params.getParameter("freeze"))
        p->setValueNotifyingHost(preset.freeze ? 1.0f : 0.0f);

    // New parameters (with defaults in Preset struct)
    if (auto* p = params.getParameter("roomsize"))
        p->setValueNotifyingHost(preset.roomSize);

    if (auto* p = params.getParameter("erlatebal"))
        p->setValueNotifyingHost(preset.earlyLateBal);

    if (auto* p = params.getParameter("highdecay"))
        p->setValueNotifyingHost(params.getParameterRange("highdecay").convertTo0to1(preset.highDecay));

    if (auto* p = params.getParameter("middecay"))
        p->setValueNotifyingHost(params.getParameterRange("middecay").convertTo0to1(preset.midDecay));

    if (auto* p = params.getParameter("highfreq"))
        p->setValueNotifyingHost(params.getParameterRange("highfreq").convertTo0to1(preset.highFreq));

    if (auto* p = params.getParameter("ershape"))
        p->setValueNotifyingHost(preset.erShape);

    if (auto* p = params.getParameter("erspread"))
        p->setValueNotifyingHost(preset.erSpread);

    if (auto* p = params.getParameter("erbasscut"))
        p->setValueNotifyingHost(params.getParameterRange("erbasscut").convertTo0to1(preset.erBassCut));
}

} // namespace SilkVerbPresets
"""
    return header


def load_results(input_dir: str, min_score: float = 0.0) -> list[dict]:
    """Load all individual result JSON files from a directory."""
    results = []

    # Load individual files (not the combined file)
    for json_file in sorted(glob.glob(os.path.join(input_dir, '*.json'))):
        basename = os.path.basename(json_file)
        if basename.startswith('_'):
            continue  # Skip combined results file

        try:
            with open(json_file) as f:
                data = json.load(f)

            if 'error' in data:
                print(f"  SKIP (error): {data.get('target_name', basename)}")
                continue

            score = data.get('score', 0)
            if score < min_score:
                print(f"  SKIP (score {score:.1f} < {min_score}): "
                      f"{data.get('target_name', basename)}")
                continue

            if 'params' not in data:
                print(f"  SKIP (no params): {basename}")
                continue

            results.append(data)

        except (json.JSONDecodeError, KeyError) as e:
            print(f"  SKIP (parse error): {basename}: {e}")

    return results


def main():
    parser = argparse.ArgumentParser(
        description='Convert optimization results to SilkVerbPresets.h',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument('--input', '-i', default='results',
                        help='Input directory with JSON results (default: results/)')
    parser.add_argument('--output', '-o',
                        default=os.path.join(os.path.dirname(__file__),
                                             '../../plugins/SilkVerb/Source/SilkVerbPresets.h'),
                        help='Output .h file path')
    parser.add_argument('--min-score', type=float, default=0.0,
                        help='Minimum match score to include (default: 0)')
    parser.add_argument('--preview', action='store_true',
                        help='Print summary without writing file')

    args = parser.parse_args()

    print(f"Loading results from: {args.input}")
    results = load_results(args.input, args.min_score)

    if not results:
        print("No valid results found!")
        return

    # Convert to preset format
    presets = [params_to_preset(r) for r in results]
    print(f"\nConverted {len(presets)} presets")

    # Organize by category
    by_category = {}
    for p in presets:
        cat = p['category']
        by_category.setdefault(cat, []).append(p)

    # Sort within each category by name
    for cat in by_category:
        by_category[cat].sort(key=lambda p: p['name'])

    # Stats
    scores = [p['score'] for p in presets]
    stats = {
        'date': datetime.now().strftime('%Y-%m-%d'),
        'total': len(presets),
        'avg_score': sum(scores) / len(scores),
        'min_score': min(scores),
        'max_score': max(scores),
    }

    # Summary
    print(f"\n{'='*50}")
    print(f"  Preset Summary")
    print(f"{'='*50}")
    for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
        cat_presets = by_category.get(cat, [])
        if cat_presets:
            cat_scores = [p['score'] for p in cat_presets]
            print(f"  {cat:10s}: {len(cat_presets):3d} presets  "
                  f"(avg score: {sum(cat_scores)/len(cat_scores):.1f})")
    print(f"  {'Total':10s}: {len(presets):3d} presets  "
          f"(avg score: {stats['avg_score']:.1f})")

    if args.preview:
        print(f"\nPreview mode — not writing file.")
        # Show a sample
        for cat in ['Halls', 'Rooms', 'Plates', 'Creative']:
            cat_presets = by_category.get(cat, [])
            if cat_presets:
                print(f"\n--- {cat} (sample) ---")
                print(format_preset_cpp(cat_presets[0]))
        return

    # Generate header
    header_content = generate_header(by_category, stats)

    # Write output
    output_path = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w') as f:
        f.write(header_content)

    print(f"\nWritten: {output_path}")
    print(f"  {len(presets)} presets in {len(header_content)} bytes")


if __name__ == '__main__':
    main()
