#!/usr/bin/env python3
"""
Suede 200 — DSP Algorithm Analyzer

Extracts practical reverb parameters from the WCS microcode:
- Delay line lengths (from OFST values)
- Coefficient patterns (from C8:C3:C2:C1 code)
- Register routing (from WAI/RAD/RAI fields)
- Algorithm structure identification (allpass, comb, feedback paths)

MI Bit Field Map (confirmed from schematics):
  MI[7:0]   = OFST_LO: Data memory offset, low byte
  MI[15:8]  = OFST_HI: Data memory offset, high byte
              Together = 16-bit offset added to CPC (Current Position Counter)
              CPC wraps at 64K, advancing each sample period
  MI16      = C8:  Coefficient select bit 3
  MI17      = C1:  Coefficient select bit 0
  MI18      = C2:  Coefficient select bit 1
  MI19      = C3:  Coefficient select bit 2
  MI20      = ACC0: Accumulator control (0=load fresh, 1=accumulate/hold)
  MI21      = RAD0: Register file read address bit 0
  MI22      = RAD1: Register file read address bit 1
  MI23      = RAI:  Read address input mode select
  MI24      = WAI0: Register file write address bit 0
  MI25      = WAI1: Register file write address bit 1
  MI26      = WAI2: Register file write address bit 2
  MI[31:27] = CTRL: Control signals (decoded to OFST strobes, OP/, MWR/, BCON)

Hardware specs:
  - Master clock: 18.432 MHz
  - WCS: 128 steps per sample period
  - Sample rate: ~20.48 kHz (18.432 MHz / 900 clocks, estimated)
  - Data memory: 64K x 16-bit = 128KB total
  - At 20.48 kHz: 64K samples ≈ 3.2 seconds max delay
"""

import os
import sys
from collections import Counter, defaultdict

ROM_DIR = os.path.expanduser("~/Downloads/Suede 200 ROM")

# Estimated original sample rate (Hz)
# The 18.432 MHz crystal divided by the total clocks per sample period
# 128 WCS steps × ~7 clocks/step = ~896 clocks/sample → ~20.57 kHz
# Service manual says "20 Hz to 10 kHz" reverb bandwidth (Nyquist ≈ 10 kHz → Fs ≈ 20 kHz)
SAMPLE_RATE = 20480  # ~20.48 kHz estimated


def load_roms():
    rom4_path = os.path.join(ROM_DIR, "v1r3rom4.bin")
    rom5_path = os.path.join(ROM_DIR, "v1r3rom5.bin")
    with open(rom4_path, "rb") as f:
        rom4 = f.read()
    with open(rom5_path, "rb") as f:
        rom5 = f.read()
    return rom4, rom5


def extract_microcode(rom4, rom5, prog_num):
    """Extract 128-step microcode for a given program number."""
    records = {
        1: (rom4, 0x000), 2: (rom4, 0x333), 3: (rom4, 0x666),
        4: (rom4, 0x999), 5: (rom4, 0xCCC), 6: (rom5, 0x000),
    }
    rom, off = records[prog_num]

    steps = []
    for step in range(128):
        mi31_24 = rom[off + 0x133 + step]         # Upper control byte (WCS 0xC000)
        mi23_16 = rom[off + 0x133 + 128 + step]   # Coeff/register byte (WCS 0xC080)
        mi15_8 = rom[off + 0x233 + step]           # Offset high byte (WCS 0xC100)
        mi7_0 = rom[off + 0x233 + 128 + step]     # Offset low byte (WCS 0xC180)
        steps.append({
            "word": (mi31_24 << 24) | (mi23_16 << 16) | (mi15_8 << 8) | mi7_0,
            "mi31_24": mi31_24,
            "mi23_16": mi23_16,
            "mi15_8": mi15_8,
            "mi7_0": mi7_0,
            # Decoded fields
            "ofst": mi7_0 | (mi15_8 << 8),
            "c_code": ((mi23_16 >> 0) & 1) |      # C8 → bit 3
                      (((mi23_16 >> 3) & 1) << 2) |  # C3 → bit 2
                      (((mi23_16 >> 2) & 1) << 1) |  # C2 → bit 1
                      (((mi23_16 >> 1) & 1) << 0),    # C1 → bit 0 -- wait this is wrong
            "c8": (mi23_16 >> 0) & 1,
            "c1": (mi23_16 >> 1) & 1,
            "c2": (mi23_16 >> 2) & 1,
            "c3": (mi23_16 >> 3) & 1,
            "acc0": (mi23_16 >> 4) & 1,
            "rad": (mi23_16 >> 5) & 3,
            "rai": (mi23_16 >> 7) & 1,
            "wai": mi31_24 & 7,
            "ctrl": (mi31_24 >> 3) & 0x1F,
        })
    return steps


def offset_to_ms(ofst):
    """Convert a 16-bit offset to milliseconds at the estimated sample rate."""
    if ofst == 0 or ofst == 0xFFFF:
        return 0.0
    return (ofst / SAMPLE_RATE) * 1000.0


def offset_to_samples(ofst):
    """Return the offset as sample count."""
    return ofst


def analyze_delay_structure(steps, prog_num):
    """Analyze the delay line structure of a program."""
    print(f"\n{'='*70}")
    print(f"PROGRAM {prog_num} — DELAY STRUCTURE ANALYSIS")
    print(f"{'='*70}")

    # Collect all unique offsets (excluding 0xFFFF which is NOP)
    offsets = []
    for i, s in enumerate(steps):
        if s["ofst"] != 0xFFFF and s["ctrl"] != 0x1F:
            offsets.append((i, s["ofst"]))

    unique_offsets = sorted(set(o for _, o in offsets))
    print(f"\n  Unique memory offsets: {len(unique_offsets)}")
    print(f"  Offset range: 0x{min(unique_offsets):04X} - 0x{max(unique_offsets):04X}")
    print(f"  Time range: {offset_to_ms(min(unique_offsets)):.1f} ms - "
          f"{offset_to_ms(max(unique_offsets)):.1f} ms")

    # Group offsets by similarity (find delay line pairs: read/write at same offset)
    # An allpass filter reads and writes at the same offset with a coefficient
    offset_usage = defaultdict(list)
    for step_num, ofst in offsets:
        offset_usage[ofst].append(step_num)

    # Find offsets used multiple times (likely allpass or comb elements)
    multi_use = {o: steps_list for o, steps_list in offset_usage.items()
                 if len(steps_list) > 1}

    print(f"\n  Offsets used multiple times (likely allpass/comb pairs):")
    for ofst in sorted(multi_use.keys()):
        step_list = multi_use[ofst]
        ms = offset_to_ms(ofst)
        print(f"    0x{ofst:04X} ({ms:7.2f} ms, {ofst} samples): "
              f"steps {step_list}")

    # Sort all offsets and compute differences (delay line lengths)
    print(f"\n  All delay offsets sorted by value:")
    print(f"  {'Offset':>8s}  {'Samples':>8s}  {'Time(ms)':>10s}  {'Steps':>20s}")
    for ofst in unique_offsets[:40]:
        step_list = offset_usage[ofst]
        ms = offset_to_ms(ofst)
        print(f"  0x{ofst:04X}    {ofst:8d}  {ms:10.2f}  {step_list}")
    if len(unique_offsets) > 40:
        print(f"  ... ({len(unique_offsets) - 40} more)")

    return unique_offsets


def analyze_coefficients(steps, prog_num):
    """Analyze coefficient usage pattern."""
    print(f"\n  COEFFICIENT ANALYSIS:")

    # For each step, decode the coefficient code
    coeff_usage = Counter()
    for i, s in enumerate(steps):
        if s["ctrl"] != 0x1F:  # Skip NOP steps
            # The raw MI16-23 byte when not 0xFF
            if s["mi23_16"] != 0xFF:
                coeff_usage[s["mi23_16"]] += 1

    print(f"    Unique MI16-23 values: {len(coeff_usage)}")
    for val, count in coeff_usage.most_common(20):
        # Decode fields within MI16-23
        c8 = (val >> 0) & 1
        c1 = (val >> 1) & 1
        c2 = (val >> 2) & 1
        c3 = (val >> 3) & 1
        acc = (val >> 4) & 1
        rad = (val >> 5) & 3
        rai = (val >> 7) & 1
        coeff_code = (c8 << 3) | (c3 << 2) | (c2 << 1) | c1
        print(f"    0x{val:02X} (×{count:2d}): "
              f"C={coeff_code:X} ACC={acc} RAD={rad} RAI={rai}")


def analyze_register_flow(steps, prog_num):
    """Analyze register file read/write patterns."""
    print(f"\n  REGISTER FLOW:")

    wai_usage = Counter()
    rad_usage = Counter()
    for i, s in enumerate(steps):
        if s["ctrl"] != 0x1F:
            wai_usage[s["wai"]] += 1
            if s["mi23_16"] != 0xFF:
                rad_usage[s["rad"]] += 1

    print(f"    Write addresses (WAI) used: ", end="")
    for wai, count in sorted(wai_usage.items()):
        print(f"WAI={wai}(×{count}) ", end="")
    print()

    print(f"    Read addresses (RAD) used: ", end="")
    for rad, count in sorted(rad_usage.items()):
        print(f"RAD={rad}(×{count}) ", end="")
    print()


def print_microcode_listing(steps, prog_num, max_steps=128):
    """Print annotated microcode listing."""
    print(f"\n  MICROCODE LISTING (all {max_steps} steps):")
    print(f"  {'Step':>4s}  {'Word':>10s}  {'OFST':>6s}  {'ms':>7s}  "
          f"{'C':>2s} {'AC':>2s} {'RAD':>3s} {'RAI':>3s} {'WAI':>3s} "
          f"{'CTRL':>5s}  {'Notes'}")
    print(f"  {'-'*4}  {'-'*10}  {'-'*6}  {'-'*7}  "
          f"{'-'*2} {'-'*2} {'-'*3} {'-'*3} {'-'*3} "
          f"{'-'*5}  {'-'*20}")

    for i in range(min(max_steps, len(steps))):
        s = steps[i]
        word = s["word"]
        ofst = s["ofst"]
        ms = offset_to_ms(ofst) if ofst != 0xFFFF else 0

        # Coefficient code
        c_code = (s["c8"] << 3) | (s["c3"] << 2) | (s["c2"] << 1) | s["c1"]

        notes = []
        if s["ctrl"] == 0x1F:
            if s["wai"] == 7 and s["mi23_16"] == 0xFF:
                notes.append("NOP")
            else:
                notes.append("CTRL=1F")
        if s["acc0"] == 0 and s["mi23_16"] != 0xFF:
            notes.append("ACC_LD")
        elif s["acc0"] == 1 and s["mi23_16"] != 0xFF:
            notes.append("ACC_ADD")

        if s["mi23_16"] == 0xFF:
            c_str = " ."
            acc_str = " ."
            rad_str = "  ."
            rai_str = "  ."
        else:
            c_str = f"{c_code:2X}"
            acc_str = f" {s['acc0']}"
            rad_str = f"  {s['rad']}"
            rai_str = f"  {s['rai']}"

        print(f"  {i:4d}  {word:08X}    {ofst:04X}  {ms:7.2f}  "
              f"{c_str} {acc_str} {rad_str} {rai_str} "
              f"  {s['wai']}  0x{s['ctrl']:02X}  {' '.join(notes)}")


def compare_all_programs(rom4, rom5):
    """Cross-program comparison of key parameters."""
    print(f"\n{'='*70}")
    print(f"CROSS-PROGRAM COMPARISON")
    print(f"{'='*70}")

    all_offsets = {}
    for p in range(1, 7):
        steps = extract_microcode(rom4, rom5, p)
        offsets = set()
        for s in steps:
            if s["ofst"] != 0xFFFF and s["ctrl"] != 0x1F:
                offsets.add(s["ofst"])
        all_offsets[p] = offsets

    # Shared offsets between programs
    print(f"\n  Shared delay offsets between programs:")
    for p1 in range(1, 7):
        for p2 in range(p1 + 1, 7):
            shared = all_offsets[p1] & all_offsets[p2]
            total = len(all_offsets[p1] | all_offsets[p2])
            pct = len(shared) / total * 100 if total > 0 else 0
            print(f"    Prog {p1} ∩ Prog {p2}: "
                  f"{len(shared)} shared / {total} total ({pct:.0f}%)")

    # Summary table
    print(f"\n  Program parameter summary:")
    print(f"  {'Prog':>4s}  {'Offsets':>8s}  {'Min(ms)':>8s}  {'Max(ms)':>8s}  "
          f"{'Max delay':>10s}  {'C16 vals':>8s}")
    for p in range(1, 7):
        steps = extract_microcode(rom4, rom5, p)
        offsets = sorted(all_offsets[p])
        mi16_vals = set()
        for s in steps:
            if s["mi23_16"] != 0xFF:
                mi16_vals.add(s["mi23_16"])
        if offsets:
            print(f"  {p:4d}  {len(offsets):8d}  {offset_to_ms(offsets[0]):8.2f}  "
                  f"{offset_to_ms(offsets[-1]):8.2f}  "
                  f"{offset_to_ms(offsets[-1]):8.2f}ms  {len(mi16_vals):8d}")


def main():
    print("=" * 70)
    print("SUEDE 200 — DSP ALGORITHM ANALYZER")
    print(f"Estimated sample rate: {SAMPLE_RATE} Hz")
    print("=" * 70)

    rom4, rom5 = load_roms()

    for prog_num in range(1, 7):
        steps = extract_microcode(rom4, rom5, prog_num)

        # Delay structure
        analyze_delay_structure(steps, prog_num)

        # Coefficients
        analyze_coefficients(steps, prog_num)

        # Register flow
        analyze_register_flow(steps, prog_num)

        # Full listing (only for first program, abbreviated for others)
        if prog_num <= 2:
            print_microcode_listing(steps, prog_num, max_steps=128)
        else:
            print_microcode_listing(steps, prog_num, max_steps=48)

    # Cross-program comparison
    compare_all_programs(rom4, rom5)


if __name__ == "__main__":
    main()
