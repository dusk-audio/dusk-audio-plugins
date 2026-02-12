#!/usr/bin/env python3
"""
Extract warm-start JSON files from Velvet90Presets.h for the batch optimizer.

Parses the C++ preset struct array and converts each entry to the optimizer's
JSON result format, enabling warm-start re-optimization after DSP changes.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcm90_ir_mapping import get_all_ir_mappings

# Mode/color name lookup (matching FDNReverb.h enums)
MODE_NAMES = {
    0: 'Plate', 1: 'Room', 2: 'Hall', 3: 'Chamber', 4: 'Cathedral',
    5: 'Ambience', 6: 'Bright Hall', 7: 'Chorus Space', 8: 'Random Space',
    9: 'Dirty Hall',
}
COLOR_NAMES = {0: '1970s', 1: '1980s', 2: 'Now'}
ENV_MODE_NAMES = {0: 'Off', 1: 'Gate', 2: 'Reverse', 3: 'Swell', 4: 'Ducked'}


def size_normalized_to_seconds(value: float) -> float:
    """Convert JUCE normalized 0-1 size to seconds.
    Formula: seconds = 0.3 + value^1.5 * 9.7
    """
    return 0.3 + (value ** 1.5) * 9.7


def er_late_to_string(bal: float) -> str:
    """Convert earlyLateBal 0-1 to display string like 'E40/L60'."""
    if bal <= 0.01:
        return 'Early'
    if bal >= 0.99:
        return 'Late'
    e = int(round((1.0 - bal) * 100))
    l = int(round(bal * 100))
    return f'E{e}/L{l}'


def safe_filename(name: str) -> str:
    """Sanitize preset name for filename (matches batch_optimize_all.py convention)."""
    unsafe = ' \'"/?#\\:*<>|'
    result = name
    for char in unsafe:
        result = result.replace(char, '_' if char in ' /' else '')
    return result


def parse_presets(header_path: str) -> list[dict]:
    """Parse all presets from Velvet90Presets.h."""
    with open(header_path, 'r') as f:
        content = f.read()

    # Match each presets.push_back({...}); block
    pattern = r'presets\.push_back\(\{([^}]+(?:\{[^}]*\}[^}]*)*)\}\);'
    # Simpler: match everything between push_back({ and }); accounting for the
    # fact that comments with // are on each line
    blocks = re.findall(r'presets\.push_back\(\{\s*\n(.*?)\}\);', content, re.DOTALL)

    presets = []
    for block in blocks:
        # Extract values: strip comments (// ...) and parse
        lines = block.strip().split('\n')
        values = []
        for line in lines:
            line = line.split('//')[0].strip().rstrip(',').strip()
            if not line:
                continue
            # Handle string values (quoted)
            if line.startswith('"'):
                values.append(line.strip('"'))
            elif line in ('true', 'false'):
                values.append(line == 'true')
            elif line.endswith('f'):
                values.append(float(line.rstrip('f')))
            else:
                try:
                    if '.' in line:
                        values.append(float(line))
                    else:
                        values.append(int(line))
                except ValueError:
                    values.append(line)

        if len(values) < 44:
            continue  # skip malformed entries
        # Map positional values to struct fields
        preset = {
            'name': values[0],
            'category': values[1],
            'mode': int(values[2]),
            'color': int(values[3]),
            'size': float(values[4]),
            'damping': float(values[5]),
            'predelay': float(values[6]),
            'mix': float(values[7]),
            'modRate': float(values[8]),
            'modDepth': float(values[9]),
            'width': float(values[10]),
            'earlyDiff': float(values[11]),
            'lateDiff': float(values[12]),
            'bassMult': float(values[13]),
            'bassFreq': float(values[14]),
            'lowCut': float(values[15]),
            'highCut': float(values[16]),
            'freeze': bool(values[17]),
            'roomSize': float(values[18]),
            'earlyLateBal': float(values[19]),
            'highDecay': float(values[20]),
            'midDecay': float(values[21]),
            'highFreq': float(values[22]),
            'erShape': float(values[23]),
            'erSpread': float(values[24]),
            'erBassCut': float(values[25]),
            'trebleRatio': float(values[26]),
            'stereoCoupling': float(values[27]),
            'lowMidFreq': float(values[28]),
            'lowMidDecay': float(values[29]),
            'envMode': int(values[30]),
            'envHold': float(values[31]),
            'envRelease': float(values[32]),
            'envDepth': float(values[33]),
            'echoDelay': float(values[34]),
            'echoFeedback': float(values[35]),
            'outEQ1Freq': float(values[36]),
            'outEQ1Gain': float(values[37]),
            'outEQ1Q': float(values[38]),
            'outEQ2Freq': float(values[39]),
            'outEQ2Gain': float(values[40]),
            'outEQ2Q': float(values[41]),
            'stereoInvert': float(values[42]),
            'resonance': float(values[43]),
        }

        # Optional v13 fields (pingpong, dynamics)
        if len(values) > 44:
            preset['echoPingPong'] = float(values[44])
        else:
            preset['echoPingPong'] = 0.0
        if len(values) > 45:
            preset['dynAmount'] = float(values[45])
        else:
            preset['dynAmount'] = 0.0
        if len(values) > 46:
            preset['dynSpeed'] = float(values[46])
        else:
            preset['dynSpeed'] = 0.5

        presets.append(preset)

    return presets


def preset_to_optimizer_json(preset: dict, ir_mapping: dict = None) -> dict:
    """Convert parsed C++ preset to optimizer JSON result format."""
    size_seconds = size_normalized_to_seconds(preset['size'])

    params = {
        'mode': MODE_NAMES.get(preset['mode'], 'Hall'),
        'color': COLOR_NAMES.get(preset['color'], 'Now'),
        'size': f'{size_seconds:.1f}s',
        'room_size': preset['roomSize'] * 100.0,
        'damping': preset['damping'] * 100.0,
        'pre_delay_ms': preset['predelay'],
        'mix': preset['mix'] * 100.0,
        'mod_rate_hz': preset['modRate'],
        'mod_depth': preset['modDepth'] * 100.0,
        'width': preset['width'] * 100.0,
        'early_diff': preset['earlyDiff'] * 100.0,
        'late_diff': preset['lateDiff'] * 100.0,
        'er_late': er_late_to_string(preset['earlyLateBal']),
        'bass_mult_x': preset['bassMult'],
        'bass_freq_hz': preset['bassFreq'],
        'hf_decay_x': preset['highDecay'],
        'low_cut_hz': preset['lowCut'],
        'high_cut_hz': preset['highCut'],
        'mid_decay_x': preset['midDecay'],
        'high_freq_hz': preset['highFreq'],
        'er_shape': preset['erShape'] * 100.0,
        'er_spread': preset['erSpread'] * 100.0,
        'er_bass_cut_hz': preset['erBassCut'],
        'treble_ratio': preset['trebleRatio'],
        'stereo_coupling': preset['stereoCoupling'] * 100.0,
        'low_mid_freq_hz': preset['lowMidFreq'],
        'low_mid_decay_x': preset['lowMidDecay'],
        'env_mode': ENV_MODE_NAMES.get(preset['envMode'], 'Off'),
        'env_hold_ms': preset['envHold'],
        'env_release_ms': preset['envRelease'],
        'env_depth': preset['envDepth'],
        'echo_delay_ms': preset['echoDelay'],
        'echo_feedback': preset['echoFeedback'],
        'out_eq1_freq_hz': preset['outEQ1Freq'],
        'out_eq1_gain_db': preset['outEQ1Gain'],
        'out_eq1_q': preset['outEQ1Q'],
        'out_eq2_freq_hz': preset['outEQ2Freq'],
        'out_eq2_gain_db': preset['outEQ2Gain'],
        'out_eq2_q': preset['outEQ2Q'],
        'stereo_invert': preset['stereoInvert'],
        'resonance': preset['resonance'],
        'echo_pingpong': preset['echoPingPong'],
        'dyn_amount': preset['dynAmount'],
        'dyn_speed': preset['dynSpeed'],
        'freeze': preset['freeze'],
    }

    result = {
        'target_name': preset['name'],
        'category': preset['category'],
        'mode_index': preset['mode'],
        'mode_name': MODE_NAMES.get(preset['mode'], 'Hall'),
        'score': 0.0,  # unknown — will be re-evaluated
        'params': params,
        'iterations': 0,
        'elapsed_s': 0.0,
    }

    # Add IR mapping info if available
    if ir_mapping:
        result['ir_file'] = ir_mapping.get('ir_file', '')
        result['ir_path'] = ir_mapping.get('ir_path', '')
        result['description'] = ir_mapping.get('description', '')
        result['pcm90_bank'] = ir_mapping.get('pcm90_bank', '')

    return result


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    header_path = os.path.join(script_dir, '..', '..', 'plugins', 'Velvet90',
                               'Source', 'Velvet90Presets.h')

    if not os.path.exists(header_path):
        print(f"Error: {header_path} not found")
        sys.exit(1)

    # Parse presets
    print(f"Parsing presets from {os.path.basename(header_path)}...")
    presets = parse_presets(header_path)
    print(f"  Found {len(presets)} presets")

    # Build IR mapping lookup
    print("Loading IR mappings...")
    ir_mappings = get_all_ir_mappings()
    ir_by_name = {m['preset_name']: m for m in ir_mappings}
    print(f"  Found {len(ir_mappings)} IR mappings")

    # Output directory
    output_dir = os.path.join(script_dir, 'warmstart_v13')
    os.makedirs(output_dir, exist_ok=True)

    # Generate JSON files
    matched = 0
    unmatched = []
    for preset in presets:
        name = preset['name']
        ir_mapping = ir_by_name.get(name)

        if not ir_mapping:
            unmatched.append(name)
            continue

        result = preset_to_optimizer_json(preset, ir_mapping)
        filename = safe_filename(name) + '.json'
        filepath = os.path.join(output_dir, filename)

        with open(filepath, 'w') as f:
            json.dump(result, f, indent=2)
        matched += 1

    print(f"\nGenerated {matched} warm-start JSON files in {output_dir}/")
    if unmatched:
        print(f"  {len(unmatched)} presets had no IR mapping (skipped):")
        for name in unmatched[:10]:
            print(f"    - {name}")
        if len(unmatched) > 10:
            print(f"    ... and {len(unmatched) - 10} more")


if __name__ == '__main__':
    main()
