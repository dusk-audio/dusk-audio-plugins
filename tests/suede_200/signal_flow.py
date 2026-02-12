#!/usr/bin/env python3
"""
Suede 200 — Signal Flow Diagram Generator

Traces through the 128-step WCS microcode to identify DSP elements and
generate text-based signal flow diagrams for each algorithm topology.

Hardware reference (from schematics):
  - 4x F181 ALU slices (16-bit arithmetic with saturation)
  - 4x16 register file: R0-R3 (read via RAD), W0-W7 (write via WAI)
  - Accumulator with load/add control (ACC0)
  - Transfer register for pipeline staging
  - 64K x 16-bit delay memory (CPC-based circular buffer)
  - Coefficient multiplier (C8:C3:C2:C1 selects coefficient)

Operation per WCS step:
  1. Read from delay memory at CPC + OFST → data_in
  2. Multiply data_in by coefficient (selected by C code)
  3. If ACC0=0: ACC = result (load fresh)
     If ACC0=1: ACC = ACC + result (accumulate)
  4. Read register file at RAD → reg_data
  5. Write ACC or data through to register file at WAI
  6. Optionally write to delay memory (MWR control)

The CTRL field (MI27-31) decodes to:
  - Memory write enable (MWR/)
  - Operation select (OP/)
  - Bus connection control (BCON)
  - Offset strobe enables

Confirmed record layout:
  MI24-31: record[0x133 + step]     WAI[2:0] + CTRL[4:0]
  MI16-23: record[0x1B3 + step]     RAI + RAD[1:0] + ACC0 + C3 + C2 + C1 + C8
  MI8-15:  record[0x233 + step]     OFST high byte
  MI0-7:   record[0x2B3 + step]     OFST low byte
"""

import os
from collections import defaultdict

ROM_DIR = os.path.expanduser("~/Downloads/Suede 200 ROM")
SAMPLE_RATE = 20480

RECORDS = {
    1: ("v1r3rom4.bin", 0x000),
    2: ("v1r3rom4.bin", 0x333),
    3: ("v1r3rom4.bin", 0x666),
    4: ("v1r3rom4.bin", 0x999),
    5: ("v1r3rom4.bin", 0xCCC),
    6: ("v1r3rom5.bin", 0x000),
}

PROGRAM_NAMES = {
    1: "Concert Hall (Algorithm A)",
    2: "Plate (Algorithm B)",
    3: "Chamber (Algorithm C)",
    4: "Random Hall (Algorithm C variant)",
    5: "Church (Algorithm C)",
    6: "Cathedral (Algorithm C)",
}


def load_rom(filename):
    path = os.path.join(ROM_DIR, filename)
    with open(path, "rb") as f:
        return f.read()


def load_roms():
    roms = {}
    for fn in set(fn for fn, _ in RECORDS.values()):
        roms[fn] = load_rom(fn)
    return roms


def extract_microcode(roms, prog):
    fn, off = RECORDS[prog]
    rom = roms[fn]
    required_size = off + 0x2B3 + 128  # Max offset accessed
    if len(rom) < required_size:
        raise ValueError(
            f"ROM '{fn}' too small: {len(rom)} bytes, need {required_size}"
        )
    steps = []
    for step in range(128):
        mi31_24 = rom[off + 0x133 + step]
        mi23_16 = rom[off + 0x1B3 + step]
        mi15_8 = rom[off + 0x233 + step]
        mi7_0 = rom[off + 0x2B3 + step]

        wai = mi31_24 & 7
        ctrl = (mi31_24 >> 3) & 0x1F

        c8 = (mi23_16 >> 0) & 1
        c1 = (mi23_16 >> 1) & 1
        c2 = (mi23_16 >> 2) & 1
        c3 = (mi23_16 >> 3) & 1
        acc0 = (mi23_16 >> 4) & 1
        rad = (mi23_16 >> 5) & 3
        rai = (mi23_16 >> 7) & 1
        c_code = (c8 << 3) | (c3 << 2) | (c2 << 1) | c1
        has_coeff = mi23_16 != 0xFF

        ofst = mi7_0 | (mi15_8 << 8)
        ms = (ofst / SAMPLE_RATE) * 1000.0 if ofst != 0xFFFF else 0

        steps.append({
            "step": step,
            "word": (mi31_24 << 24) | (mi23_16 << 16) | (mi15_8 << 8) | mi7_0,
            "mi31_24": mi31_24, "mi23_16": mi23_16,
            "mi15_8": mi15_8, "mi7_0": mi7_0,
            "wai": wai, "ctrl": ctrl,
            "c_code": c_code, "c8": c8, "c1": c1, "c2": c2, "c3": c3,
            "acc0": acc0, "rad": rad, "rai": rai,
            "has_coeff": has_coeff,
            "ofst": ofst, "ms": ms,
            "is_nop": mi31_24 == 0xFF and mi23_16 == 0xFF,
        })
    return steps


def classify_operation(s):
    """Classify a WCS step into a DSP operation type."""
    if s["is_nop"]:
        return "NOP"

    has_c = s["has_coeff"]
    acc = s["acc0"] if has_c else None
    ctrl = s["ctrl"]
    wai = s["wai"]
    ofst = s["ofst"]

    # CTRL field decoding (approximate from schematic analysis):
    # Bit 4 (MI31): Related to memory write / output strobe
    # Bit 3 (MI30): OP/ select
    # Bits 0-2 (MI27-29): BCON / offset strobe selects

    is_mem_write = ctrl & 0x10  # Approximate: high bit of CTRL
    is_ctrl_1f = ctrl == 0x1F   # All control bits set → special/pass-through

    if not has_c and is_ctrl_1f:
        # No coefficient, CTRL=0x1F: data routing / NOP-like
        if wai == 7:
            return "NOP"
        else:
            return "ROUTE"  # Just moving data to a register

    if has_c and acc == 0:
        return "LOAD"  # ACC = coeff * mem[ofst] (fresh load)

    if has_c and acc == 1:
        if is_ctrl_1f:
            return "FEEDBACK_READ"  # Read from feedback delay, accumulate
        elif is_mem_write:
            return "MEM_WRITE_ACC"  # Write accumulator to memory + accumulate
        else:
            return "MAC"  # Multiply-accumulate

    return "MISC"


def identify_delay_pairs(steps):
    """Find delay line read/write pairs that form allpass or comb filters."""
    # Group steps by memory offset
    offset_steps = defaultdict(list)
    for s in steps:
        if s["ofst"] != 0xFFFF and not s["is_nop"]:
            offset_steps[s["ofst"]].append(s)

    pairs = []
    for ofst, step_list in sorted(offset_steps.items()):
        if len(step_list) >= 2:
            ops = [classify_operation(s) for s in step_list]
            has_load = any(o in ("LOAD", "MAC") for o in ops)
            has_feedback = any(o in ("FEEDBACK_READ",) for o in ops)
            has_route = any(o in ("ROUTE",) for o in ops)

            pair_type = "unknown"
            if has_load and has_feedback:
                pair_type = "allpass"
            elif has_load and has_route:
                pair_type = "comb_read_write"
            elif len(step_list) >= 4:
                pair_type = "multi_tap"
            else:
                pair_type = "dual_use"

            pairs.append({
                "ofst": ofst,
                "ms": step_list[0]["ms"],
                "steps": [s["step"] for s in step_list],
                "ops": ops,
                "type": pair_type,
            })

    return pairs


def identify_processing_sections(steps):
    """Identify distinct processing sections within the microcode."""
    sections = []
    current_section = None

    for s in steps:
        op = classify_operation(s)

        if op == "NOP":
            if current_section is not None:
                sections.append(current_section)
                current_section = None
            continue

        if current_section is None:
            current_section = {
                "start": s["step"],
                "end": s["step"],
                "steps": [],
                "ops": defaultdict(int),
            }

        current_section["end"] = s["step"]
        current_section["steps"].append(s)
        current_section["ops"][op] += 1

    if current_section is not None:
        sections.append(current_section)

    return sections


def analyze_feedback_structure(steps):
    """Identify the feedback delay network structure.

    In the Suede 200, the FDN is implemented as:
    1. Read from long delay lines (2400-3200ms)
    2. Apply feedback coefficients
    3. Sum into accumulator
    4. Write back to delay lines

    The key is matching which delay reads feed into which writes.
    """
    long_delays = []  # > 500ms
    short_delays = []  # < 500ms

    for s in steps:
        if s["is_nop"] or s["ofst"] == 0xFFFF:
            continue
        if s["ms"] > 500:
            long_delays.append(s)
        elif s["ms"] > 0:
            short_delays.append(s)

    return {
        "long_delays": long_delays,
        "short_delays": short_delays,
        "feedback_taps": len(set(s["ofst"] for s in long_delays)),
        "diffusion_taps": len(set(s["ofst"] for s in short_delays)),
    }


def generate_signal_flow(prog, steps):
    """Generate a text-based signal flow diagram."""
    print(f"\n{'='*70}")
    print(f"SIGNAL FLOW — Program {prog}: {PROGRAM_NAMES.get(prog, '?')}")
    print(f"{'='*70}")

    # Classify all operations
    op_counts = defaultdict(int)
    for s in steps:
        op = classify_operation(s)
        op_counts[op] += 1

    print(f"\n  Operation Summary:")
    for op, count in sorted(op_counts.items(), key=lambda x: -x[1]):
        print(f"    {op:20s}: {count:3d} steps")

    # Find processing sections (separated by NOP gaps)
    sections = identify_processing_sections(steps)
    print(f"\n  Processing Sections: {len(sections)}")
    for i, sec in enumerate(sections):
        step_range = f"{sec['start']}-{sec['end']}"
        n_steps = len(sec['steps'])
        ops = ", ".join(f"{op}={cnt}" for op, cnt in
                        sorted(sec['ops'].items(), key=lambda x: -x[1]))
        print(f"    Section {i}: steps {step_range:>8s} "
              f"({n_steps:2d} active)  [{ops}]")

    # Feedback structure
    fb = analyze_feedback_structure(steps)
    print(f"\n  Feedback Delay Network:")
    print(f"    Long delay taps (>500ms): {fb['feedback_taps']}")
    print(f"    Short delay taps (<500ms): {fb['diffusion_taps']}")

    # Detailed delay line analysis
    offset_usage = defaultdict(list)
    for s in steps:
        if s["ofst"] != 0xFFFF and not s["is_nop"]:
            offset_usage[s["ofst"]].append(s)

    # Sort by delay time
    sorted_offsets = sorted(offset_usage.keys())

    print(f"\n  Delay Lines (unique offsets: {len(sorted_offsets)}):")
    print(f"  {'OFST':>6s}  {'ms':>8s}  {'samples':>7s}  {'#uses':>5s}  "
          f"{'Steps':>20s}  {'Operations'}")

    for ofst in sorted_offsets:
        step_list = offset_usage[ofst]
        ms = step_list[0]["ms"]
        ops = [classify_operation(s) for s in step_list]
        step_nums = [s["step"] for s in step_list]
        wai_vals = [s["wai"] for s in step_list]
        c_codes = [f"C{s['c_code']:X}" if s["has_coeff"] else "--"
                   for s in step_list]

        ops_str = " ".join(f"{o}(s{n},w{w},{c})"
                           for o, n, w, c in zip(ops, step_nums, wai_vals, c_codes))

        print(f"  0x{ofst:04X}  {ms:8.2f}  {ofst:7d}  {len(step_list):5d}  "
              f"{str(step_nums):>20s}  {ops_str}")

    # Identify delay line pairs (allpass/comb structures)
    pairs = identify_delay_pairs(steps)
    allpass_count = sum(1 for p in pairs if p["type"] == "allpass")
    multi_tap_count = sum(1 for p in pairs if p["type"] == "multi_tap")

    print(f"\n  Delay Line Pairs:")
    print(f"    Potential allpass: {allpass_count}")
    print(f"    Multi-tap points: {multi_tap_count}")
    for p in pairs:
        print(f"    0x{p['ofst']:04X} ({p['ms']:7.2f}ms): "
              f"{p['type']:15s} steps={p['steps']} ops={p['ops']}")

    # Generate text flow diagram
    generate_text_diagram(prog, steps, sections, fb, pairs)


def generate_text_diagram(prog, steps, sections, fb, pairs):
    """Generate a text-based block diagram of the algorithm."""
    print(f"\n  BLOCK DIAGRAM:")
    print(f"  {'─'*60}")

    # Categorize delay lines
    short_lines = []
    long_lines = []
    for s in steps:
        if s["is_nop"] or s["ofst"] == 0xFFFF:
            continue
        if s["ms"] < 500:
            short_lines.append(s)
        else:
            long_lines.append(s)

    # Get unique delay offsets sorted
    short_offsets = sorted(set(s["ofst"] for s in short_lines))
    long_offsets = sorted(set(s["ofst"] for s in long_lines))

    # Count coefficient codes used
    c_code_counts = defaultdict(int)
    for s in steps:
        if s["has_coeff"]:
            c_code_counts[s["c_code"]] += 1

    # Estimate algorithm structure
    n_short = len(short_offsets)
    n_long = len(long_offsets)

    if n_short > 0:
        short_range = (short_offsets[0] / SAMPLE_RATE * 1000,
                       short_offsets[-1] / SAMPLE_RATE * 1000)
    else:
        short_range = (0, 0)

    if n_long > 0:
        long_range = (long_offsets[0] / SAMPLE_RATE * 1000,
                      long_offsets[-1] / SAMPLE_RATE * 1000)
    else:
        long_range = (0, 0)

    # Format offset/coeff lists truncated to fit box width (39 inner chars)
    def _fmt_offsets(offsets, max_chars=27):
        parts = [f'0x{o:04X}' for o in offsets]
        result = ', '.join(parts)
        if len(result) <= max_chars:
            return result
        # Truncate and add ellipsis
        for n in range(len(parts), 0, -1):
            candidate = ', '.join(parts[:n]) + '...'
            if len(candidate) <= max_chars:
                return candidate
        return '...'

    def _fmt_coeffs(counts, max_chars=23):
        parts = [f'C{c:X}(x{n})' for c, n in sorted(counts.items())]
        result = ', '.join(parts)
        if len(result) <= max_chars:
            return result
        for n in range(len(parts), 0, -1):
            candidate = ', '.join(parts[:n]) + '...'
            if len(candidate) <= max_chars:
                return candidate
        return '...'

    short_ofst_str = _fmt_offsets(short_offsets)
    long_ofst_str = _fmt_offsets(long_offsets)
    coeff_str = _fmt_coeffs(c_code_counts)

    print(f"""
  Input ──┐
          │
          ▼
  ┌───────────────────────────────────────┐
  │  Input Diffusion Network              │
  │  {n_short} short delay taps                   │
  │  Range: {short_range[0]:.1f} - {short_range[1]:.1f} ms           │
  │  Offsets: {short_ofst_str:<28s}│
  └───────────────┬───────────────────────┘
                  │
                  ▼
  ┌───────────────────────────────────────┐
  │  Feedback Delay Network (FDN)         │
  │  {n_long} long delay taps                    │
  │  Range: {long_range[0]:.1f} - {long_range[1]:.1f} ms         │
  │  Offsets: {long_ofst_str:<28s}│
  │                                       │
  │  Coeff codes: {coeff_str:<24s}│
  │                                       │
  │  Feedback ◄──────────────────────┐    │
  │      │                           │    │
  │      ▼    ┌──────┐               │    │
  │    ┌───┐  │Delay │  ┌───┐    ┌───┐   │
  │    │×Ci│──│Lines │──│×Cj│──► │Σ  │   │
  │    └───┘  │      │  └───┘    └─┬─┘   │
  │           └──────┘             │      │
  │                                │      │
  └────────────────────────────────┼──────┘
                                   │
                                   ▼
                                 Output
""")

    # WCS execution timeline
    print(f"  WCS Execution Timeline (128 steps):")
    print(f"  {'─'*60}")

    # Map step ranges to functions
    for sec in sections:
        start = sec["start"]
        end = sec["end"]
        n = len(sec["steps"])
        primary_op = max(sec["ops"].items(), key=lambda x: x[1])[0]

        # Categorize this section
        section_delays = [s for s in sec["steps"]
                          if s["ofst"] != 0xFFFF]
        if section_delays:
            avg_ms = sum(s["ms"] for s in section_delays) / len(section_delays)
            if avg_ms > 1000:
                section_type = "FDN Processing"
            elif avg_ms > 100:
                section_type = "Diffusion/Early Reflections"
            else:
                section_type = "Input Processing"
        else:
            section_type = "Routing"

        print(f"  Steps {start:3d}-{end:3d} ({n:2d} active): "
              f"{section_type:30s} [{primary_op}]")


def compare_topologies(roms):
    """Compare the 3 algorithm topologies side by side."""
    print(f"\n{'='*70}")
    print(f"ALGORITHM TOPOLOGY COMPARISON")
    print(f"{'='*70}")

    # Representative programs: 1 (Algo A), 2 (Algo B), 3 (Algo C)
    reps = {
        "A (Prog 1)": 1,
        "B (Prog 2)": 2,
        "C (Prog 3)": 3,
    }

    for name, prog in reps.items():
        steps = extract_microcode(roms, prog)
        fb = analyze_feedback_structure(steps)

        # Count active steps
        active = [s for s in steps if not s["is_nop"]]
        coeff_steps = [s for s in steps if s["has_coeff"]]

        # Unique offsets
        offsets = sorted(set(s["ofst"] for s in active if s["ofst"] != 0xFFFF))

        # WAI usage
        wai_counts = defaultdict(int)
        for s in active:
            wai_counts[s["wai"]] += 1

        # C-code usage
        c_counts = defaultdict(int)
        for s in coeff_steps:
            c_counts[s["c_code"]] += 1

        print(f"\n  Algorithm {name}:")
        print(f"    Active steps:     {len(active)}/128")
        print(f"    Coefficient ops:  {len(coeff_steps)}")
        print(f"    Unique offsets:   {len(offsets)}")
        print(f"    FDN taps (>500ms): {fb['feedback_taps']}")
        print(f"    Diffusion (<500ms): {fb['diffusion_taps']}")
        print(f"    WAI registers:    "
              f"{', '.join(f'W{w}={c}' for w, c in sorted(wai_counts.items()))}")
        print(f"    Coeff codes:      "
              f"{', '.join(f'C{c:X}={n}' for c, n in sorted(c_counts.items()))}")

        if offsets:
            print(f"    Short delays:     "
                  f"{', '.join(f'{o/SAMPLE_RATE*1000:.0f}ms' for o in offsets if o/SAMPLE_RATE*1000 < 500)}")
            print(f"    Long delays:      "
                  f"{', '.join(f'{o/SAMPLE_RATE*1000:.0f}ms' for o in offsets if o/SAMPLE_RATE*1000 >= 500)}")


def main():
    print("=" * 70)
    print("SUEDE 200 — SIGNAL FLOW DIAGRAM GENERATOR")
    print(f"Sample rate: {SAMPLE_RATE} Hz")
    print("=" * 70)

    roms = load_roms()

    # Generate diagrams for representative programs of each algorithm
    for prog in [1, 2, 3]:
        steps = extract_microcode(roms, prog)
        generate_signal_flow(prog, steps)

    # Show all 6 programs briefly
    print(f"\n{'='*70}")
    print(f"ALL PROGRAMS — QUICK SUMMARY")
    print(f"{'='*70}")
    for prog in range(1, 7):
        steps = extract_microcode(roms, prog)
        active = [s for s in steps if not s["is_nop"]]
        coeff = [s for s in steps if s["has_coeff"]]
        offsets = set(s["ofst"] for s in active if s["ofst"] != 0xFFFF)
        fb = analyze_feedback_structure(steps)
        print(f"  Prog {prog} ({PROGRAM_NAMES.get(prog, '?'):30s}): "
              f"{len(active):3d} active, {len(coeff):2d} coeff ops, "
              f"{len(offsets):2d} offsets, "
              f"{fb['feedback_taps']:2d} FDN/{fb['diffusion_taps']:2d} diff taps")

    # Side-by-side topology comparison
    compare_topologies(roms)


if __name__ == "__main__":
    main()
