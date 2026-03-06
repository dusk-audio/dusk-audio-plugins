#!/usr/bin/env python3
"""
Discover ReferenceReverb factory presets and all parameter values via pedalboard.

Research script - prints findings to stdout, does not modify any files.

Key findings:
  - Factory presets are .vpreset files (XML format) stored in:
    /Library/Application Support/ReferenceReverb/ReferenceReverb/Presets/Factory/
  - 160 factory presets, 53 designer presets
  - ReferenceReverb v4.0.5 has 24+ reverb modes (not just 7!)
  - All parameters are 0.0-1.0 normalized
  - State format: binary plist wrapping XML
  - pedalboard has no factory preset enumeration API; we read .vpreset files directly
"""

import sys
import os
import xml.etree.ElementTree as ET
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from config import REFERENCE_REVERB_PATHS, find_plugin

try:
    import pedalboard
    from pedalboard import load_plugin
except ImportError:
    print("ERROR: pedalboard not installed. Run: pip install pedalboard")
    sys.exit(1)


# ---------------------------------------------------------------------------
# ReferenceReverb reverb modes (discovered from binary inspection)
# ---------------------------------------------------------------------------
# The ReverbMode parameter is 0.0-1.0 continuous. The plugin has 24 modes
# internally (discovered via binary strings and the Rev2010 method names).
# The 7 modes shown in the original UI are a subset; v4.0.5 added many more.

REVERB_MODES = {
    # Original 7 modes (v1-v3)
    "Concert Hall":     "slow attack, low initial echo density, chorused modulation, dark tone",
    "Bright Hall":      "slow attack, low initial echo density, lush chorused modulation, bright tone",
    "Plate":            "fast and diffuse attack, bright tone, fast build of echo density, chorused modulation",
    "Room":             "fast grainy attack, sparser early echo density, darker tone, chorused modulation",
    "Chamber":          "fast and diffuse attack, high echo density, low coloration, chorused modulation",
    "Random Space":     "slow diffuse attack, slower echo build, spacious, randomized delay modulation",
    "Chorus Space":     "slow diffuse attack, slower echo build, spacious, chorused modulation",
    # Added in later versions
    "Ambience":         "dense randomized early reflections, diffuse reverb tail, random+chorused modulation",
    "Sanctuary":        "fast attack w/discrete early reflections, high initial echo density, detuned modulation",
    "Dirty Hall":       "slow attack, low initial echo density, lush chorused modulation, dark and dirty tone",
    "Chaotic Hall":     "slow attack, low initial echo density, chorused and chaotic modulation, dark and dirty tone",
    "Chaotic Neutral":  "medium attack, high initial echo density, balanced chaotic modulation, clean and neutral tone",
    "Cathedral":        "medium attack, medium initial echo density, balanced ensemble modulation, clean and neutral tone",
    "Dirty Plate":      "fast diffuse attack, high initial echo density, lush chorused modulation, bright tone",
    "Smooth Plate":     "fast diffuse attack, fairly dense initial echo density, chorused modulation, exponential decay",
    "Chaotic Chamber":  "fast attack, sparser late echo density, chorused and chaotic modulation, exponential decay",
    "Smooth Room":      "fast diffuse attack, sparser early echo density, chorus modulation, exponential decay",
    "Nonlin":           "variable slope set by attack, chorus modulation, no decay",
    "Smooth Random":    "fast diffuse attack, random modulation, exponential decay",
    "Palace":           "room / large spaces simulator, sparse initial echo density, lush balanced modulation",
    "Chamber1979":      "late 70s chamber algorithm. lower initial echo density, lush balanced modulation",
    "Hall1984":         "slow attack, low initial echo density, lush random modulation, bright initial tone",
    # "Darkness" referenced in binary but may be a UI theme, not a reverb mode
}

COLOR_MODES = {
    "1970s": "downsampled (10 KHz bandwidth), dark, noisier modulation",
    "1980s": "no downsampling (full bandwidth), dark, noisier modulation",
    "Now":   "no downsampling (full bandwidth), bright, clean modulation",
}


def parse_vpreset(filepath):
    """Parse a .vpreset XML file and return (preset_name, params_dict)."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        # Handle XML declaration
        root = ET.fromstring(content)
        attrs = dict(root.attrib)
        name = attrs.pop('presetName', os.path.splitext(os.path.basename(filepath))[0])
        version = attrs.pop('pluginVersion', 'unknown')
        return name, version, attrs
    except Exception as e:
        return None, None, {}


def main():
    # --- 1. Load plugin for reference ---
    plugin_path = find_plugin(REFERENCE_REVERB_PATHS)
    if plugin_path is None:
        print("ERROR: ReferenceReverb not found")
        sys.exit(1)

    plugin = load_plugin(plugin_path)
    print(f"Plugin: {plugin.name} v{plugin.version}")
    print(f"Pedalboard: {pedalboard.__version__}")
    print(f"Path: {plugin_path}")
    print()

    # --- 2. List all parameters ---
    print("=" * 100)
    print("PARAMETERS (17 total, all 0.0-1.0 normalized)")
    print("=" * 100)
    print(f"  {'Name':25s} {'Default Value':>15s}  Display Label")
    print("  " + "-" * 60)
    for name, param in sorted(plugin.parameters.items()):
        val = getattr(plugin, name, '?')
        print(f"  {name:25s} {val:>15.6f}  {param.name}")
    print()

    # --- 3. Reverb modes discovered from binary ---
    print("=" * 100)
    print(f"REVERB MODES ({len(REVERB_MODES)} modes discovered from binary)")
    print("=" * 100)
    for i, (mode_name, description) in enumerate(REVERB_MODES.items()):
        # ReverbMode is 0-1, with 24 modes = steps of 1/23 = 0.04348
        mode_val = i / (len(REVERB_MODES) - 1) if len(REVERB_MODES) > 1 else 0
        print(f"  [{i:2d}] {mode_val:.4f}  {mode_name:20s}  {description}")
    print()
    print("  Note: ReverbMode values in .vpreset files use the exact float value.")
    print("  The mapping above is approximate; actual values vary per preset file.")
    print()

    # --- 4. Color modes ---
    print("=" * 100)
    print("COLOR MODES (3 modes)")
    print("=" * 100)
    for i, (name, desc) in enumerate(COLOR_MODES.items()):
        val = i / (len(COLOR_MODES) - 1) if len(COLOR_MODES) > 1 else 0
        print(f"  [{i}] {val:.4f}  {name:10s}  {desc}")
    print()

    # --- 5. Read all factory presets ---
    print("=" * 100)
    print("FACTORY PRESETS")
    print("=" * 100)

    factory_dir = "/Library/Application Support/ReferenceReverb/ReferenceReverb/Presets/Factory"
    designer_dir = "/Library/Application Support/ReferenceReverb/ReferenceReverb/Presets/Designer"

    all_presets = []
    categories = defaultdict(list)

    for preset_dir, preset_type in [(factory_dir, "Factory"), (designer_dir, "Designer")]:
        if not os.path.isdir(preset_dir):
            print(f"  {preset_type} directory not found: {preset_dir}")
            continue

        for root, dirs, files in os.walk(preset_dir):
            category = os.path.relpath(root, preset_dir)
            if category == '.':
                category = preset_type
            else:
                category = f"{preset_type}/{category}"

            for fname in sorted(files):
                if not fname.endswith('.vpreset'):
                    continue
                fpath = os.path.join(root, fname)
                name, version, params = parse_vpreset(fpath)
                if name is None:
                    continue
                preset = {
                    'name': name,
                    'category': category,
                    'type': preset_type,
                    'version': version,
                    'file': fname,
                    'params': params,
                }
                all_presets.append(preset)
                categories[category].append(preset)

    # Print summary by category
    print(f"\n  Total presets found: {len(all_presets)}")
    print(f"  Categories:")
    for cat in sorted(categories.keys()):
        print(f"    {cat:40s} ({len(categories[cat]):3d} presets)")
    print()

    # --- 6. Print all factory presets with full parameter values ---
    print("=" * 100)
    print("FACTORY PRESET PARAMETER VALUES")
    print("=" * 100)

    # Determine the standard parameter names from the first few presets
    all_param_names = set()
    for p in all_presets:
        all_param_names.update(p['params'].keys())

    # Known audio parameters (exclude metadata like mixLock, uiWidth, etc.)
    audio_params = [
        'Mix', 'PreDelay', 'Decay', 'Size', 'Attack',
        'BassMult', 'BassXover', 'HighShelf', 'HighFreq',
        'EarlyDiffusion', 'LateDiffusion', 'ModRate', 'ModDepth',
        'HighCut', 'LowCut', 'ColorMode', 'ReverbMode',
    ]

    # Also check for any unknown parameters
    known_meta = {'mixLock', 'uiWidth', 'uiHeight', 'VeeThreePalette'}
    unknown_params = all_param_names - set(audio_params) - known_meta
    if unknown_params:
        print(f"\n  Unknown parameters found in presets: {sorted(unknown_params)}")
        print()

    # Print header
    print(f"\n  {'Category':<30s} {'Preset Name':<40s} | {'ReverbMode':>10s} {'ColorMode':>10s} {'Decay':>8s} {'Size':>8s} {'PreDelay':>8s} {'Mix':>6s} {'Attack':>8s} | {'EarlyDif':>8s} {'LateDif':>8s} {'ModRate':>8s} {'ModDpth':>8s} | {'HighCut':>8s} {'LowCut':>8s} {'BassMul':>8s} {'BassXov':>8s} {'HiShelf':>8s} {'HiFreq':>8s}")
    print("  " + "-" * 240)

    for cat in sorted(categories.keys()):
        for p in sorted(categories[cat], key=lambda x: x['name']):
            params = p['params']
            row = f"  {cat:<30s} {p['name']:<40s}"
            row += f" | {params.get('ReverbMode', ''):>10s} {params.get('ColorMode', ''):>10s}"
            row += f" {params.get('Decay', ''):>8s} {params.get('Size', ''):>8s}"
            row += f" {params.get('PreDelay', ''):>8s} {params.get('Mix', ''):>6s}"
            row += f" {params.get('Attack', ''):>8s}"
            row += f" | {params.get('EarlyDiffusion', ''):>8s} {params.get('LateDiffusion', ''):>8s}"
            row += f" {params.get('ModRate', ''):>8s} {params.get('ModDepth', ''):>8s}"
            row += f" | {params.get('HighCut', ''):>8s} {params.get('LowCut', ''):>8s}"
            row += f" {params.get('BassMult', ''):>8s} {params.get('BassXover', ''):>8s}"
            row += f" {params.get('HighShelf', ''):>8s} {params.get('HighFreq', ''):>8s}"
            print(row)
    print()

    # --- 7. Analyze ReverbMode distribution across presets ---
    print("=" * 100)
    print("REVERBMODE VALUES ACROSS ALL PRESETS")
    print("=" * 100)

    mode_values = defaultdict(list)
    for p in all_presets:
        rm = p['params'].get('ReverbMode', '')
        if rm:
            try:
                rm_float = float(rm)
                mode_values[rm_float].append(p['name'])
            except ValueError:
                pass

    print(f"\n  {'ReverbMode Value':>16s}  {'Count':>5s}  Example Presets")
    print("  " + "-" * 80)
    for val in sorted(mode_values.keys()):
        names = mode_values[val]
        examples = ', '.join(names[:3])
        if len(names) > 3:
            examples += f", ... (+{len(names)-3} more)"
        print(f"  {val:>16.10f}  {len(names):>5d}  {examples}")
    print()

    # --- 8. Analyze ColorMode distribution ---
    print("=" * 100)
    print("COLORMODE VALUES ACROSS ALL PRESETS")
    print("=" * 100)

    color_values = defaultdict(list)
    for p in all_presets:
        cm = p['params'].get('ColorMode', '')
        if cm:
            try:
                cm_float = float(cm)
                color_values[cm_float].append(p['name'])
            except ValueError:
                pass

    print(f"\n  {'ColorMode Value':>16s}  {'Count':>5s}  Interpretation")
    print("  " + "-" * 60)
    for val in sorted(color_values.keys()):
        names = color_values[val]
        if val < 0.167:
            interp = "1970s"
        elif val < 0.5:
            interp = "1980s"
        else:
            interp = "Now"
        print(f"  {val:>16.10f}  {len(names):>5d}  {interp}")
    print()

    # --- 9. Try loading a factory preset via raw_state ---
    print("=" * 100)
    print("LOADING FACTORY PRESETS VIA PEDALBOARD raw_state")
    print("=" * 100)

    # Pick a few representative presets and try to load them
    test_presets = [
        os.path.join(factory_dir, "Halls", "84 Classic Hall.vpreset"),
        os.path.join(factory_dir, "Plates", "Vocal Plate.vpreset"),
        os.path.join(factory_dir, "Rooms", "84 Small Room.vpreset"),
        os.path.join(factory_dir, "Chambers", "Clear Chamber.vpreset"),
        os.path.join(factory_dir, "Huge Spaces", "EchoVerb.vpreset"),
    ]

    for fpath in test_presets:
        if not os.path.exists(fpath):
            print(f"  NOT FOUND: {fpath}")
            continue

        name, version, params = parse_vpreset(fpath)
        print(f"\n  Preset: {name} (v{version})")
        print(f"  File: {os.path.basename(fpath)}")

        # Set each parameter on the plugin
        success_count = 0
        fail_count = 0
        for param_name, param_val in params.items():
            if param_name in ('mixLock', 'uiWidth', 'uiHeight', 'VeeThreePalette'):
                continue
            try:
                py_name = param_name[0].lower() + param_name[1:]  # lowercase first letter
                # Convert to snake_case-ish for pedalboard
                # Actually pedalboard uses all-lowercase
                py_name = param_name.lower()
                val_float = float(param_val)
                setattr(plugin, py_name, val_float)
                readback = getattr(plugin, py_name)
                delta = abs(readback - val_float)
                status = "OK" if delta < 0.01 else f"DELTA={delta:.4f}"
                print(f"    {param_name:20s} = {param_val:>12s}  -> set={val_float:.6f}  readback={readback:.6f}  [{status}]")
                success_count += 1
            except Exception as e:
                print(f"    {param_name:20s} = {param_val:>12s}  -> ERROR: {e}")
                fail_count += 1

        print(f"    Result: {success_count} params set OK, {fail_count} failed")

    print()

    # --- 10. Summary ---
    print("=" * 100)
    print("SUMMARY OF FINDINGS")
    print("=" * 100)
    print(f"""
  Plugin: {plugin.name} v{plugin.version}
  Path: {plugin_path}

  PARAMETERS:
    17 parameters, all 0.0-1.0 normalized
    Names: mix, predelay, decay, size, attack, bassmult, bassxover,
           highshelf, highfreq, earlydiffusion, latediffusion, modrate,
           moddepth, highcut, lowcut, colormode, reverbmode

  REVERB MODES (from binary, {len(REVERB_MODES)} discovered):
    Concert Hall, Bright Hall, Plate, Room, Chamber, Random Space,
    Chorus Space, Ambience, Sanctuary, Dirty Hall, Chaotic Hall,
    Chaotic Neutral, Cathedral, Dirty Plate, Smooth Plate,
    Chaotic Chamber, Smooth Room, Nonlin, Smooth Random,
    Palace, Chamber1979, Hall1984

  COLOR MODES (3):
    1970s (dark, downsampled), 1980s (neutral), Now (bright)

  FACTORY PRESETS:
    Location: /Library/Application Support/ReferenceReverb/ReferenceReverb/Presets/
    Factory: {sum(1 for p in all_presets if p['type'] == 'Factory')} presets in {len([c for c in categories if c.startswith('Factory')])} categories
    Designer: {sum(1 for p in all_presets if p['type'] == 'Designer')} presets in {len([c for c in categories if c.startswith('Designer')])} categories
    Format: XML (.vpreset files), same format as raw_state XML

  PRESET LOADING VIA PEDALBOARD:
    No factory preset enumeration API in pedalboard
    CAN load presets by: parsing .vpreset XML, setting parameters individually
    raw_state property: binary plist wrapping XML (can also be used)

  UNIQUE REVERBMODE VALUES: {len(mode_values)}
  UNIQUE COLORMODE VALUES: {len(color_values)}
""")


if __name__ == "__main__":
    main()
