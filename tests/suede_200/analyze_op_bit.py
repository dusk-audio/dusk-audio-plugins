#!/usr/bin/env python3
"""
Suede 200 — OP/ (Negate) Bit Identification

Determines whether the OP/ (negate) control signal for coefficient steps
is CTRL bit 3 (0x08) or CTRL bit 2 (0x04) by analyzing allpass filter
structures across all 6 programs.

Allpass filters require BOTH a positive and negative coefficient path:
    y = -g*x + delay[n] + g*y_prev

So in a typical allpass, one coefficient step should have OP/ SET (negate)
and another should have OP/ CLEAR. We look for pairs of coefficient steps
using the SAME cCode where one has the candidate OP/ bit set and the other
does not.

CTRL field (5 bits, MI31-MI27):
    Bit 4 (0x10) = MI31 = MWR  (Memory Write Enable)    - confirmed
    Bit 3 (0x08) = MI30 = MCEN/ (Memory Clock Enable, active low) - per schematics
    Bit 2 (0x04) = MI29 = OP/  (negate) - per schematics
    Bits 0-1     = MI27-28 = BCON (bus control)
"""

import os
import sys
from collections import defaultdict

# ROM directory
ROM_DIR = os.path.expanduser("~/Downloads/Lexicon 200")

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
    4: "Rich Plate (Algorithm C variant)",
    5: "Rich Splits (Algorithm C)",
    6: "Inverse Rooms (Algorithm C)",
}

SAMPLE_RATE = 20480


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
    """Extract and decode 128 WCS steps for a program."""
    fn, off = RECORDS[prog]
    rom = roms[fn]
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
        is_nop = (mi31_24 == 0xFF and mi23_16 == 0xFF)

        steps.append({
            "step": step,
            "word": (mi31_24 << 24) | (mi23_16 << 16) | (mi15_8 << 8) | mi7_0,
            "mi31_24": mi31_24,
            "mi23_16": mi23_16,
            "mi15_8": mi15_8,
            "mi7_0": mi7_0,
            "wai": wai,
            "ctrl": ctrl,
            "c_code": c_code,
            "acc0": acc0,
            "rad": rad,
            "rai": rai,
            "has_coeff": has_coeff,
            "ofst": ofst,
            "ms": ms,
            "is_nop": is_nop,
        })
    return steps


def format_ctrl_bits(ctrl):
    """Format CTRL field with named bit flags."""
    bits = []
    if ctrl & 0x10:
        bits.append("MWR")
    if ctrl & 0x08:
        bits.append("b3")
    if ctrl & 0x04:
        bits.append("b2")
    if ctrl & 0x02:
        bits.append("b1")
    if ctrl & 0x01:
        bits.append("b0")
    return "|".join(bits) if bits else "none"


def print_coeff_steps(steps, prog):
    """Print all coefficient steps for a program with full detail."""
    print(f"\n{'='*100}")
    print(f"PROGRAM {prog}: {PROGRAM_NAMES.get(prog, '?')}")
    print(f"{'='*100}")
    print(f"{'Step':>4s}  {'CTRL':>6s}  {'CTRL bin':>10s}  {'Bits':>15s}  "
          f"{'cCode':>5s}  {'WAI':>3s}  {'RAI':>3s}  {'RAD':>3s}  "
          f"{'ACC0':>4s}  {'OFST':>6s}  {'ms':>8s}  {'b3=OP?':>6s}  {'b2=OP?':>6s}")
    print("-" * 100)

    for s in steps:
        if not s["has_coeff"]:
            continue
        ctrl = s["ctrl"]
        b3_set = "NEG" if (ctrl & 0x08) else "pos"
        b2_set = "NEG" if (ctrl & 0x04) else "pos"
        print(f"{s['step']:4d}  "
              f"0x{ctrl:02X}    "
              f"{ctrl:05b}      "
              f"{format_ctrl_bits(ctrl):>15s}  "
              f"  C{s['c_code']:X}   "
              f" {s['wai']}    "
              f" {s['rai']}    "
              f" {s['rad']}   "
              f"  {s['acc0']}   "
              f"0x{s['ofst']:04X}  "
              f"{s['ms']:8.2f}  "
              f"{b3_set:>6s}  "
              f"{b2_set:>6s}")


def find_allpass_pairs(steps, op_bit_mask, bit_name):
    """
    Find pairs of coefficient steps with the same cCode where one has
    the candidate OP/ bit set and one has it clear.

    For a true allpass, you need:
    - Two steps using the same coefficient (cCode)
    - One with OP/ set (negate path: -g*x)
    - One with OP/ clear (positive path: +g*y_prev)

    Returns list of detected pairs.
    """
    # Group coefficient steps by cCode
    by_ccode = defaultdict(list)
    for s in steps:
        if s["has_coeff"]:
            by_ccode[s["c_code"]].append(s)

    pairs = []
    for ccode, step_list in sorted(by_ccode.items()):
        with_bit = [s for s in step_list if s["ctrl"] & op_bit_mask]
        without_bit = [s for s in step_list if not (s["ctrl"] & op_bit_mask)]

        if with_bit and without_bit:
            pairs.append({
                "ccode": ccode,
                "with_bit": with_bit,
                "without_bit": without_bit,
            })

    return pairs


def find_allpass_pairs_by_offset(steps, op_bit_mask, bit_name):
    """
    Alternative approach: find pairs of coefficient steps accessing the SAME
    memory offset (delay line) where one has the candidate OP/ bit set and
    one has it clear.

    In an allpass structure:
    - Read delay[n] with coefficient g (positive or negative)
    - Write result back to delay memory with MWR
    - The two coefficient steps at the same offset form the allpass

    Returns list of detected offset pairs.
    """
    # Group coefficient steps by memory offset
    by_offset = defaultdict(list)
    for s in steps:
        if s["has_coeff"] and s["ofst"] != 0xFFFF:
            by_offset[s["ofst"]].append(s)

    pairs = []
    for ofst, step_list in sorted(by_offset.items()):
        with_bit = [s for s in step_list if s["ctrl"] & op_bit_mask]
        without_bit = [s for s in step_list if not (s["ctrl"] & op_bit_mask)]

        if with_bit and without_bit:
            # Check if the ccodes match between the with/without groups
            with_ccodes = set(s["c_code"] for s in with_bit)
            without_ccodes = set(s["c_code"] for s in without_bit)
            shared_ccodes = with_ccodes & without_ccodes

            pairs.append({
                "ofst": ofst,
                "ms": step_list[0]["ms"],
                "with_bit": with_bit,
                "without_bit": without_bit,
                "shared_ccodes": shared_ccodes,
            })

    return pairs


def analyze_ctrl_distribution(all_coeff_steps):
    """Analyze the distribution of CTRL values across all coefficient steps."""
    ctrl_counts = defaultdict(int)
    for s in all_coeff_steps:
        ctrl_counts[s["ctrl"]] += 1

    print(f"\n{'='*70}")
    print(f"CTRL VALUE DISTRIBUTION (coefficient steps only, all programs)")
    print(f"{'='*70}")
    print(f"{'CTRL':>6s}  {'Binary':>8s}  {'Count':>5s}  {'Bits set':>20s}  "
          f"{'MWR':>3s}  {'b3':>3s}  {'b2':>3s}  {'b1':>3s}  {'b0':>3s}")
    print("-" * 70)

    for ctrl, count in sorted(ctrl_counts.items()):
        print(f"0x{ctrl:02X}    {ctrl:05b}     {count:5d}  "
              f"{format_ctrl_bits(ctrl):>20s}  "
              f"{'Y' if ctrl & 0x10 else '.':>3s}  "
              f"{'Y' if ctrl & 0x08 else '.':>3s}  "
              f"{'Y' if ctrl & 0x04 else '.':>3s}  "
              f"{'Y' if ctrl & 0x02 else '.':>3s}  "
              f"{'Y' if ctrl & 0x01 else '.':>3s}")


def main():
    print("=" * 100)
    print("SUEDE 200 — OP/ (NEGATE) BIT IDENTIFICATION")
    print("=" * 100)
    print(f"\nROM directory: {ROM_DIR}")
    print(f"\nQuestion: Is OP/ (negate for allpass) at CTRL bit 3 (0x08) or bit 2 (0x04)?")
    print(f"  Schematic assignment: bit 3 = MI30 = MCEN/, bit 2 = MI29 = OP/")
    print(f"  Current wcs_engine.py uses: bit 3 (0x08) for OP/")

    roms = load_roms()

    all_coeff_steps = []

    # ── Per-program coefficient step listing ──
    for prog in range(1, 7):
        steps = extract_microcode(roms, prog)
        print_coeff_steps(steps, prog)

        coeff_steps = [s for s in steps if s["has_coeff"]]
        for s in coeff_steps:
            s["prog"] = prog
        all_coeff_steps.extend(coeff_steps)

    # ── CTRL distribution ──
    analyze_ctrl_distribution(all_coeff_steps)

    # ── Allpass detection: Theory 1 — bit 3 (0x08) is OP/ ──
    print(f"\n{'='*100}")
    print(f"ALLPASS ANALYSIS: THEORY 1 — bit 3 (0x08) = OP/ (negate)")
    print(f"{'='*100}")
    print(f"Looking for coefficient step pairs with SAME cCode,")
    print(f"one with bit 3 SET (negate) and one with bit 3 CLEAR (positive).")

    theory1_total_pairs = 0
    theory1_total_offset_pairs = 0
    for prog in range(1, 7):
        steps = extract_microcode(roms, prog)
        pairs = find_allpass_pairs(steps, 0x08, "bit3")
        offset_pairs = find_allpass_pairs_by_offset(steps, 0x08, "bit3")

        print(f"\n  Program {prog} ({PROGRAM_NAMES.get(prog, '?')}):")
        print(f"    cCode-based allpass pairs: {len(pairs)}")
        for p in pairs:
            with_steps = [s["step"] for s in p["with_bit"]]
            without_steps = [s["step"] for s in p["without_bit"]]
            print(f"      C{p['ccode']:X}: "
                  f"bit3=SET steps={with_steps} / "
                  f"bit3=CLR steps={without_steps}")

        print(f"    Offset-based allpass pairs: {len(offset_pairs)}")
        for p in offset_pairs:
            with_steps = [(s["step"], f"C{s['c_code']:X}") for s in p["with_bit"]]
            without_steps = [(s["step"], f"C{s['c_code']:X}") for s in p["without_bit"]]
            shared = [f"C{c:X}" for c in p["shared_ccodes"]]
            print(f"      OFST=0x{p['ofst']:04X} ({p['ms']:.1f}ms): "
                  f"bit3=SET {with_steps} / "
                  f"bit3=CLR {without_steps}"
                  f"  shared_ccodes={shared}")

        theory1_total_pairs += len(pairs)
        theory1_total_offset_pairs += len(offset_pairs)

    # ── Allpass detection: Theory 2 — bit 2 (0x04) is OP/ ──
    print(f"\n{'='*100}")
    print(f"ALLPASS ANALYSIS: THEORY 2 — bit 2 (0x04) = OP/ (negate)")
    print(f"{'='*100}")
    print(f"Looking for coefficient step pairs with SAME cCode,")
    print(f"one with bit 2 SET (negate) and one with bit 2 CLEAR (positive).")

    theory2_total_pairs = 0
    theory2_total_offset_pairs = 0
    for prog in range(1, 7):
        steps = extract_microcode(roms, prog)
        pairs = find_allpass_pairs(steps, 0x04, "bit2")
        offset_pairs = find_allpass_pairs_by_offset(steps, 0x04, "bit2")

        print(f"\n  Program {prog} ({PROGRAM_NAMES.get(prog, '?')}):")
        print(f"    cCode-based allpass pairs: {len(pairs)}")
        for p in pairs:
            with_steps = [s["step"] for s in p["with_bit"]]
            without_steps = [s["step"] for s in p["without_bit"]]
            print(f"      C{p['ccode']:X}: "
                  f"bit2=SET steps={with_steps} / "
                  f"bit2=CLR steps={without_steps}")

        print(f"    Offset-based allpass pairs: {len(offset_pairs)}")
        for p in offset_pairs:
            with_steps = [(s["step"], f"C{s['c_code']:X}") for s in p["with_bit"]]
            without_steps = [(s["step"], f"C{s['c_code']:X}") for s in p["without_bit"]]
            shared = [f"C{c:X}" for c in p["shared_ccodes"]]
            print(f"      OFST=0x{p['ofst']:04X} ({p['ms']:.1f}ms): "
                  f"bit2=SET {with_steps} / "
                  f"bit2=CLR {without_steps}"
                  f"  shared_ccodes={shared}")

        theory2_total_pairs += len(pairs)
        theory2_total_offset_pairs += len(offset_pairs)

    # ── Detailed allpass structure analysis ──
    print(f"\n{'='*100}")
    print(f"DETAILED ALLPASS STRUCTURE ANALYSIS")
    print(f"{'='*100}")
    print(f"\nFor each theory, examine step sequences around detected pairs")
    print(f"to see if they form plausible allpass structures.")
    print(f"\nAllpass requires: y = -g*x + delay[n] + g*delay_feedback")
    print(f"  Step A: ACC = -coeff * input     (OP/ SET, ACC0=0 load)")
    print(f"  Step B: ACC += coeff * delay[n]   (OP/ CLR, ACC0=1 accumulate)")
    print(f"  or the opposite polarity arrangement.\n")

    for theory_name, op_bit_mask in [("bit3 (0x08)", 0x08), ("bit2 (0x04)", 0x04)]:
        print(f"\n  --- Theory: {theory_name} = OP/ ---")
        for prog in range(1, 7):
            steps = extract_microcode(roms, prog)
            pairs = find_allpass_pairs(steps, op_bit_mask, theory_name)
            if not pairs:
                continue
            print(f"\n  Program {prog}:")
            for p in pairs:
                print(f"    cCode C{p['ccode']:X}:")
                for s in sorted(p["with_bit"] + p["without_bit"], key=lambda x: x["step"]):
                    neg = "NEG" if (s["ctrl"] & op_bit_mask) else "pos"
                    mwr = "MWR" if (s["ctrl"] & 0x10) else "   "
                    acc = "ACC" if s["acc0"] else "LOD"
                    src = "mem" if s["rai"] else f"R{s['rad']}"
                    print(f"      step {s['step']:3d}: {neg} {mwr} {acc} "
                          f"src={src} WAI={s['wai']} "
                          f"CTRL=0x{s['ctrl']:02X} "
                          f"OFST=0x{s['ofst']:04X} ({s['ms']:.1f}ms)")

    # ── Summary ──
    print(f"\n{'='*100}")
    print(f"SUMMARY")
    print(f"{'='*100}")
    print(f"\n  Theory 1: bit 3 (0x08) = OP/ (negate)")
    print(f"    cCode-based allpass pairs found: {theory1_total_pairs} (across all 6 programs)")
    print(f"    Offset-based allpass pairs found: {theory1_total_offset_pairs}")
    print(f"\n  Theory 2: bit 2 (0x04) = OP/ (negate)")
    print(f"    cCode-based allpass pairs found: {theory2_total_pairs} (across all 6 programs)")
    print(f"    Offset-based allpass pairs found: {theory2_total_offset_pairs}")

    # Determine winner
    print(f"\n  ---")
    if theory1_total_pairs > theory2_total_pairs:
        print(f"  CONCLUSION: bit 3 (0x08) produces MORE allpass pairs "
              f"({theory1_total_pairs} vs {theory2_total_pairs})")
        print(f"  This suggests bit 3 IS the OP/ negate bit.")
        print(f"  The current wcs_engine.py implementation (ctrl & 0x08) appears CORRECT.")
    elif theory2_total_pairs > theory1_total_pairs:
        print(f"  CONCLUSION: bit 2 (0x04) produces MORE allpass pairs "
              f"({theory2_total_pairs} vs {theory1_total_pairs})")
        print(f"  This suggests bit 2 IS the OP/ negate bit.")
        print(f"  The current wcs_engine.py implementation (ctrl & 0x08) may be INCORRECT.")
        print(f"  Consider changing to (ctrl & 0x04) for OP/.")
    else:
        print(f"  INCONCLUSIVE: Both theories produce the same number of pairs "
              f"({theory1_total_pairs})")
        print(f"  Further analysis needed.")

    # Additional analysis: check which CTRL values appear with MWR for coeff steps
    print(f"\n  --- Additional: MWR + coefficient combinations ---")
    mwr_coeff_ctrls = defaultdict(int)
    non_mwr_coeff_ctrls = defaultdict(int)
    for s in all_coeff_steps:
        if s["ctrl"] & 0x10:
            mwr_coeff_ctrls[s["ctrl"]] += 1
        else:
            non_mwr_coeff_ctrls[s["ctrl"]] += 1

    print(f"  Coefficient steps WITH MWR (bit 4 set):")
    for ctrl, count in sorted(mwr_coeff_ctrls.items()):
        b3 = "b3" if ctrl & 0x08 else "  "
        b2 = "b2" if ctrl & 0x04 else "  "
        print(f"    CTRL=0x{ctrl:02X} ({ctrl:05b}): count={count:3d}  {b3} {b2}")

    print(f"  Coefficient steps WITHOUT MWR:")
    for ctrl, count in sorted(non_mwr_coeff_ctrls.items()):
        b3 = "b3" if ctrl & 0x08 else "  "
        b2 = "b2" if ctrl & 0x04 else "  "
        print(f"    CTRL=0x{ctrl:02X} ({ctrl:05b}): count={count:3d}  {b3} {b2}")

    # Check the actual step sequences for Concert Hall (program 1) in detail
    # to understand the allpass topology
    print(f"\n  --- Detailed: Program 1 coefficient step sequence ---")
    steps = extract_microcode(roms, 1)
    for half_name, half_range in [("Left (0-63)", range(64)), ("Right (64-127)", range(64, 128))]:
        print(f"\n  {half_name}:")
        for idx in half_range:
            s = steps[idx]
            if not s["has_coeff"]:
                continue
            ctrl = s["ctrl"]
            neg_b3 = "NEG3" if (ctrl & 0x08) else "    "
            neg_b2 = "NEG2" if (ctrl & 0x04) else "    "
            mwr = "MWR" if (ctrl & 0x10) else "   "
            acc = "ACC" if s["acc0"] else "LOD"
            src = "mem" if s["rai"] else f"R{s['rad']}"
            print(f"    [{s['step']:3d}] CTRL=0x{ctrl:02X}({ctrl:05b}) "
                  f"C{s['c_code']:X} {neg_b3} {neg_b2} {mwr} {acc} "
                  f"src={src} W{s['wai']} "
                  f"OFST=0x{s['ofst']:04X}({s['ms']:6.1f}ms)")


if __name__ == "__main__":
    main()
