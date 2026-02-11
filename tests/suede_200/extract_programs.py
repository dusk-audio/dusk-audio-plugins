#!/usr/bin/env python3
"""
Suede 200 ROM Program Record Extractor & Microcode Disassembler

Extracts the 6 reverb program records from ROM4/ROM5, decodes the WCS
microcode, and maps the 32-bit microinstruction bit fields to DSP operations.

ROM Memory Map (Z80 addresses):
  ROM1: 0x0000-0x0FFF  Boot, diagnostics, hardcoded program init
  ROM2: 0x1000-0x1FFF  Main control loop, UI, parameter processing
  ROM3: 0x2000-0x2FFF  WCS loading, real-time coefficient update
  ROM4: 0x3000-0x3FFF  Program records 1-5
  ROM5: 0x4000-0x4FFF  Program record 6 + runtime tables

WCS Memory Map (Z80 address space, memory-mapped):
  0xC000-0xC07F  MI24-MI31 (U61, MSB)   128 steps
  0xC080-0xC0FF  MI16-MI23 (U62)        128 steps
  0xC100-0xC17F  MI8-MI15  (U63)        128 steps
  0xC180-0xC1FF  MI0-MI7   (U64, LSB)   128 steps

Program Record Layout (819 = 0x333 bytes, stride 0x333):
  0x000-0x032  Header/config (51 bytes)
  0x033-0x132  Coefficient/patch table bytecode (256 bytes)
  0x133-0x1B2  Upper WCS MI24-31 (128 bytes) → WCS 0xC000
  0x1B3-0x232  Upper WCS MI16-23 (128 bytes) → WCS 0xC080
  0x233-0x2B2  Lower WCS MI8-15  (128 bytes) → WCS 0xC100
  0x2B3-0x332  Lower WCS MI0-7   (128 bytes) → WCS 0xC180

  Confirmed from ROM3 loading code:
    0x043F: ldir record[0x133:0x232] → RAM 0xA4A3 (upper WCS shadow)
    0x0566: ldir RAM 0xA4A3 → WCS 0xC000 (upper WCS bulk load)
    0x057C: ldir record[0x233:0x332] → WCS 0xC100 (lower WCS)

Microinstruction Bit Fields (from schematics, Sheets 2-4):
  MI0-MI7   (U64): OFST low byte — data memory offset address LSB
  MI8-MI15  (U63): OFST high byte — data memory offset address MSB
  MI16      (C8):  Carry/coefficient select bit 3
  MI17      (C1):  Carry/coefficient select bit 0
  MI18      (C2):  Carry/coefficient select bit 1
  MI19      (C3):  Carry/coefficient select bit 2
  MI20      (ACC0): Accumulator control (load/hold)
  MI21-MI22 (RAD): Read address for register file (2 bits)
  MI23      (RAI): Read address input select
  MI24-MI26 (WAI): Write address input (3 bits)
  MI27-MI31: Additional control (OP/, BCON, memory write, etc.)

References:
  - Service Manual (1984)
  - Schematics: Sheet 2 (WCS), Sheet 3 (ARU), Sheet 4 (Data Memory)
  - Error codes E20-E23 confirm WCS bit grouping
"""

import os
import sys
import struct
from pathlib import Path

# ─── Configuration ──────────────────────────────────────────────────────────

ROM_DIR = Path(os.environ.get(
    "SUEDE_ROM_DIR",
    os.path.expanduser("~/Downloads/Suede 200 ROM")
))

ROM_FILES = {
    1: "v1r3rom1.bin",
    2: "v1r3rom2.bin",
    3: "v1r3rom3.bin",
    4: "v1r3rom4.bin",
    5: "v1r3rom5.bin",
}

# Program record locations (offset within ROM4/ROM5 file)
RECORD_SIZE = 0x333  # 819 bytes
PROGRAM_RECORDS = {
    1: (4, 0x0000),  # ROM4 offset 0x000
    2: (4, 0x0333),  # ROM4 offset 0x333
    3: (4, 0x0666),  # ROM4 offset 0x666
    4: (4, 0x0999),  # ROM4 offset 0x999
    5: (4, 0x0CCC),  # ROM4 offset 0xCCC  (only 0x334 bytes fit in 4KB)
    6: (5, 0x0000),  # ROM5 offset 0x000
}

# Record internal offsets (confirmed from ROM3 loading code at 0x043F, 0x0566)
HEADER_OFFSET = 0x000
HEADER_SIZE = 0x033  # 51 bytes
COEFF_TABLE_OFFSET = 0x033  # Coefficient/patch table bytecode
COEFF_TABLE_SIZE = 0x100  # 256 bytes
UPPER_WCS_OFFSET = 0x133  # MI24-31 (128) + MI16-23 (128) → RAM shadow → WCS 0xC000
UPPER_WCS_SIZE = 0x100  # 256 bytes
LOWER_WCS_OFFSET = 0x233  # MI8-15 (128) + MI0-7 (128) → WCS 0xC100
LOWER_WCS_SIZE = 0x100  # 256 bytes

# WCS step count
WCS_STEPS = 128


# ─── MI Bit Field Definitions ───────────────────────────────────────────────

MI_FIELDS = {
    # (start_bit, width, name, description)
    "OFST_LO":   (0,  8, "OFST[7:0]",  "Data memory offset low byte"),
    "OFST_HI":   (8,  8, "OFST[15:8]", "Data memory offset high byte"),
    "C8":        (16, 1, "C8",          "Coefficient select bit 3"),
    "C1":        (17, 1, "C1",          "Coefficient select bit 0"),
    "C2":        (18, 1, "C2",          "Coefficient select bit 1"),
    "C3":        (19, 1, "C3",          "Coefficient select bit 2"),
    "ACC0":      (20, 1, "ACC0",        "Accumulator control"),
    "RAD":       (21, 2, "RAD[1:0]",    "Register file read address"),
    "RAI":       (23, 1, "RAI",         "Read address input select"),
    "WAI":       (24, 3, "WAI[2:0]",    "Register file write address"),
    "CTRL_HI":   (27, 5, "CTRL[31:27]", "High control bits (OP/, BCON, MWR, etc.)"),
}


# ─── ROM Loading ────────────────────────────────────────────────────────────

def load_roms():
    """Load all ROM binary files."""
    roms = {}
    for num, filename in ROM_FILES.items():
        path = ROM_DIR / filename
        if path.exists():
            with open(path, "rb") as f:
                roms[num] = f.read()
            print(f"  ROM{num}: {path.name} ({len(roms[num])} bytes)")
        else:
            print(f"  ROM{num}: {path.name} NOT FOUND")
    return roms


def extract_program_record(roms, prog_num):
    """Extract a single program record from the ROMs."""
    rom_num, offset = PROGRAM_RECORDS[prog_num]
    rom_data = roms.get(rom_num)
    if rom_data is None:
        return None

    # Handle records that might span ROM boundary
    end = offset + RECORD_SIZE
    if end <= len(rom_data):
        return rom_data[offset:end]
    else:
        # Record extends beyond ROM — take what we can
        available = rom_data[offset:]
        print(f"  Warning: Program {prog_num} record truncated "
              f"({len(available)}/{RECORD_SIZE} bytes)")
        return available


# ─── Record Parsing ─────────────────────────────────────────────────────────

def parse_header(record):
    """Parse the 51-byte header of a program record."""
    header = {}
    header["id_byte"] = record[0]
    header["raw"] = record[HEADER_OFFSET:HEADER_OFFSET + HEADER_SIZE]

    # The first byte is typically the program ID
    # Bytes 1-2 might be flags/config
    # Further bytes contain parameter ranges and tables

    # Try to identify sub-sections within the header
    header["config_flags"] = record[1:4]

    # Delay parameter table region (from previous analysis: offset 0x34-0x50)
    # But within a 51-byte header (0x00-0x32), this is offset 0x34 relative to
    # record start, which is actually byte 52 — just past the header.
    # Let me re-examine: the header might be smaller.

    return header


def extract_wcs_microcode(record):
    """
    Extract the 128-step x 32-bit WCS microcode from a program record.

    Returns list of 128 tuples: (mi31_24, mi23_16, mi15_8, mi7_0)
    """
    if len(record) < LOWER_WCS_OFFSET + LOWER_WCS_SIZE:
        return None

    # Upper WCS: first 128 bytes = MI24-31, next 128 = MI16-23
    upper = record[UPPER_WCS_OFFSET:UPPER_WCS_OFFSET + UPPER_WCS_SIZE]
    mi31_24 = upper[0:128]    # Maps to WCS address 0xC000+step
    mi23_16 = upper[128:256]  # Maps to WCS address 0xC080+step

    # Lower WCS: first 128 bytes = MI8-15, next 128 = MI0-7
    lower = record[LOWER_WCS_OFFSET:LOWER_WCS_OFFSET + LOWER_WCS_SIZE]
    mi15_8 = lower[0:128]     # Maps to WCS address 0xC100+step
    mi7_0 = lower[128:256]    # Maps to WCS address 0xC180+step

    microcode = []
    for step in range(WCS_STEPS):
        word = (mi31_24[step] << 24) | (mi23_16[step] << 16) | \
               (mi15_8[step] << 8) | mi7_0[step]
        microcode.append(word)

    return microcode


def extract_coeff_table(record):
    """Extract the coefficient/patch bytecode table."""
    return record[COEFF_TABLE_OFFSET:COEFF_TABLE_OFFSET + COEFF_TABLE_SIZE]


# ─── Microinstruction Decoding ──────────────────────────────────────────────

def decode_mi_word(word):
    """Decode a 32-bit microinstruction word into named fields."""
    fields = {}
    for name, (start, width, label, desc) in MI_FIELDS.items():
        mask = (1 << width) - 1
        fields[name] = (word >> start) & mask
    return fields


def format_mi_word(word, step):
    """Format a microinstruction word for display."""
    fields = decode_mi_word(word)

    # Memory offset (16-bit address into delay RAM)
    offset = fields["OFST_LO"] | (fields["OFST_HI"] << 8)

    # Coefficient select (4 bits, but scrambled bit order: C8,C1,C2,C3)
    coeff = (fields["C8"] << 3) | (fields["C3"] << 2) | \
            (fields["C2"] << 1) | fields["C1"]

    # Register addressing
    rad = fields["RAD"]
    rai = fields["RAI"]
    wai = fields["WAI"]
    acc = fields["ACC0"]
    ctrl = fields["CTRL_HI"]

    # Determine if this is a NOP (all FF = cleared WCS)
    if word == 0xFFFFFFFF:
        return f"  [{step:3d}] FFFFFFFF  NOP (unused)"

    line = f"  [{step:3d}] {word:08X}"
    line += f"  OFST={offset:04X}"
    line += f"  C={coeff:X}"
    line += f"  ACC={acc}"
    line += f"  RAD={rad} RAI={rai}"
    line += f"  WAI={wai}"
    line += f"  CTRL={ctrl:02X}"

    # Try to interpret the operation
    ops = []
    if offset != 0xFFFF:
        ops.append(f"mem[{offset}]")
    if ctrl & 0x10:  # Bit 31 - likely memory write
        ops.append("WR")
    if acc == 0:
        ops.append("ACC_LOAD")

    if ops:
        line += f"  ; {' '.join(ops)}"

    return line


def find_active_steps(microcode):
    """Find which WCS steps are actually used (not NOP/0xFFFFFFFF)."""
    active = []
    for step, word in enumerate(microcode):
        if word != 0xFFFFFFFF:
            active.append(step)
    return active


# ─── Analysis ───────────────────────────────────────────────────────────────

def analyze_program(prog_num, record, microcode):
    """Perform high-level analysis of a program's microcode."""
    active = find_active_steps(microcode)
    if not active:
        return {"active_steps": 0}

    analysis = {
        "active_steps": len(active),
        "first_step": active[0],
        "last_step": active[-1],
        "memory_offsets": set(),
        "coeff_values": set(),
        "wai_values": set(),
        "rad_values": set(),
    }

    for step in active:
        fields = decode_mi_word(microcode[step])
        offset = fields["OFST_LO"] | (fields["OFST_HI"] << 8)
        coeff = (fields["C8"] << 3) | (fields["C3"] << 2) | \
                (fields["C2"] << 1) | fields["C1"]

        if offset != 0xFFFF:
            analysis["memory_offsets"].add(offset)
        analysis["coeff_values"].add(coeff)
        analysis["wai_values"].add(fields["WAI"])
        analysis["rad_values"].add(fields["RAD"])

    return analysis


def compare_algorithms(programs_mc):
    """Compare microcode across programs to identify shared topologies."""
    print("\n" + "=" * 70)
    print("ALGORITHM TOPOLOGY COMPARISON")
    print("=" * 70)

    prog_nums = sorted(programs_mc.keys())

    # Compare each pair of programs
    for i, p1 in enumerate(prog_nums):
        for p2 in prog_nums[i + 1:]:
            mc1 = programs_mc[p1]
            mc2 = programs_mc[p2]
            if mc1 is None or mc2 is None:
                continue

            # Count matching steps
            matches = sum(1 for s in range(WCS_STEPS)
                          if mc1[s] == mc2[s])

            # Count matching non-NOP steps
            active_matches = sum(
                1 for s in range(WCS_STEPS)
                if mc1[s] == mc2[s] and mc1[s] != 0xFFFFFFFF
            )

            # Count steps where only the lower 16 bits differ
            # (same operation, different memory offset)
            upper_matches = sum(
                1 for s in range(WCS_STEPS)
                if (mc1[s] >> 16) == (mc2[s] >> 16)
                and mc1[s] != 0xFFFFFFFF
                and mc2[s] != 0xFFFFFFFF
            )

            active1 = len(find_active_steps(mc1))
            active2 = len(find_active_steps(mc2))

            print(f"\n  Prog {p1} vs Prog {p2}:")
            print(f"    Active steps: {active1} / {active2}")
            print(f"    Exact matches: {matches}/128 "
                  f"(non-NOP: {active_matches})")
            print(f"    Same upper 16 bits: {upper_matches}")

            if active_matches > 0 and active1 > 0:
                similarity = active_matches / max(active1, active2) * 100
                print(f"    Similarity: {similarity:.1f}%")
                if similarity > 80:
                    print(f"    → SAME ALGORITHM TOPOLOGY "
                          f"(differ only in coefficients/offsets)")


# ─── Verification against ROM1 hardcoded values ────────────────────────────

def verify_against_rom1(roms, programs_mc):
    """
    Cross-reference extracted WCS data against hardcoded values in ROM1.

    ROM1 sub_074fh (Program 2) writes specific values directly to WCS:
      Step 0: MI24-31=0xF4, MI8-15=0xCF, MI0-7=0x57
      Step 1: MI24-31=0xF4, MI8-15=0xD9, MI0-7=0x57
      Step 3: MI24-31=0xF8
    """
    print("\n" + "=" * 70)
    print("VERIFICATION: ROM1 hardcoded vs ROM4 record extraction")
    print("=" * 70)

    if 2 not in programs_mc or programs_mc[2] is None:
        print("  Program 2 not available for verification")
        return

    mc = programs_mc[2]

    # Known values from ROM1 sub_074fh (Program 2 init)
    known = {
        # step: (byte_name, expected_value)
        # MI24-31 values
        0: {"mi31_24": 0xF4},
        1: {"mi31_24": 0xF4},
        3: {"mi31_24": 0xF8},
        # MI8-15 values
        # Step 0: 0xCF, Step 1: 0xD9
        # MI0-7 values
        # Step 0: 0x57, Step 1: 0x57
    }

    # Extract bytes from the 32-bit microwords
    step0 = mc[0]
    step1 = mc[1]
    step3 = mc[3]

    print(f"\n  Step 0: extracted={step0:08X}")
    print(f"    MI24-31: {(step0 >> 24) & 0xFF:02X} "
          f"(expected 0xF4 from ROM1)")
    print(f"    MI8-15:  {(step0 >> 8) & 0xFF:02X} "
          f"(expected 0xCF from ROM1)")
    print(f"    MI0-7:   {step0 & 0xFF:02X} "
          f"(expected 0x57 from ROM1)")

    print(f"\n  Step 1: extracted={step1:08X}")
    print(f"    MI24-31: {(step1 >> 24) & 0xFF:02X} "
          f"(expected 0xF4 from ROM1)")
    print(f"    MI8-15:  {(step1 >> 8) & 0xFF:02X} "
          f"(expected 0xD9 from ROM1)")
    print(f"    MI0-7:   {step1 & 0xFF:02X} "
          f"(expected 0x57 from ROM1)")

    print(f"\n  Step 3: extracted={step3:08X}")
    print(f"    MI24-31: {(step3 >> 24) & 0xFF:02X} "
          f"(expected 0xF8 from ROM1)")

    # Check matches
    ok = True
    if (step0 >> 8) & 0xFF != 0xCF:
        print("\n  *** MI8-15 MISMATCH at step 0!")
        ok = False
    if step0 & 0xFF != 0x57:
        print("\n  *** MI0-7 MISMATCH at step 0!")
        ok = False
    if (step1 >> 8) & 0xFF != 0xD9:
        print("\n  *** MI8-15 MISMATCH at step 1!")
        ok = False

    if ok:
        print("\n  ✓ Lower WCS bytes (MI0-MI15) match ROM1 hardcoded values")
    else:
        print("\n  ✗ Mismatches found — record layout may need adjustment")
        print("    Try alternate offsets for WCS data within the record")


# ─── Main ───────────────────────────────────────────────────────────────────

def main():
    print("=" * 70)
    print("SUEDE 200 — ROM PROGRAM RECORD EXTRACTOR")
    print("=" * 70)
    print(f"\nROM directory: {ROM_DIR}")
    print("\nLoading ROMs...")
    roms = load_roms()

    if 4 not in roms:
        print("\nERROR: ROM4 is required (contains program records 1-5)")
        sys.exit(1)

    programs_mc = {}
    programs_records = {}

    for prog_num in range(1, 7):
        print(f"\n{'─' * 70}")
        print(f"PROGRAM {prog_num}")
        print(f"{'─' * 70}")

        record = extract_program_record(roms, prog_num)
        if record is None:
            print(f"  Could not extract record (ROM not loaded)")
            continue

        programs_records[prog_num] = record

        # Parse header
        header = parse_header(record)
        print(f"\n  Header (first 51 bytes):")
        print(f"    ID byte: 0x{header['id_byte']:02X}")
        print(f"    Config:  {' '.join(f'{b:02X}' for b in header['config_flags'])}")
        print(f"    Raw hex: ", end="")
        for i in range(0, min(HEADER_SIZE, len(record)), 16):
            chunk = record[i:min(i + 16, HEADER_SIZE)]
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            if i == 0:
                print(hex_str)
            else:
                print(f"             {hex_str}")

        # Extract WCS microcode
        microcode = extract_wcs_microcode(record)
        if microcode is None:
            print(f"  Could not extract microcode (record too short)")
            continue

        programs_mc[prog_num] = microcode

        # Analyze
        analysis = analyze_program(prog_num, record, microcode)
        active = find_active_steps(microcode)

        print(f"\n  WCS Microcode:")
        print(f"    Active steps: {analysis['active_steps']}/128")
        if active:
            print(f"    Step range: {analysis['first_step']}-{analysis['last_step']}")
            print(f"    Unique memory offsets: {len(analysis['memory_offsets'])}")
            print(f"    Coefficient values used: "
                  f"{sorted(analysis['coeff_values'])}")
            print(f"    WAI values: {sorted(analysis['wai_values'])}")
            print(f"    RAD values: {sorted(analysis['rad_values'])}")

        # Print first 32 active microinstructions
        print(f"\n  Microcode listing (active steps):")
        count = 0
        for step in range(WCS_STEPS):
            if microcode[step] != 0xFFFFFFFF:
                print(format_mi_word(microcode[step], step))
                count += 1
                if count >= 40:
                    remaining = len(active) - count
                    if remaining > 0:
                        print(f"    ... ({remaining} more active steps)")
                    break

        # Coefficient/patch table
        coeff_table = extract_coeff_table(record)
        # Find actual content (non-0xFF bytes)
        non_ff = [(i, b) for i, b in enumerate(coeff_table) if b != 0xFF]
        print(f"\n  Coefficient/Patch Table (offset 0x033-0x132):")
        print(f"    Non-0xFF bytes: {len(non_ff)}/{len(coeff_table)}")
        if non_ff:
            print(f"    First 64 bytes:")
            for i in range(0, min(64, COEFF_TABLE_SIZE), 16):
                chunk = coeff_table[i:i + 16]
                hex_str = " ".join(f"{b:02X}" for b in chunk)
                ascii_str = "".join(
                    chr(b) if 32 <= b < 127 else "." for b in chunk
                )
                print(f"      {i:04X}: {hex_str}  {ascii_str}")

    # Cross-program comparison
    compare_algorithms(programs_mc)

    # Verify against ROM1 hardcoded values
    verify_against_rom1(roms, programs_mc)

    # Summary
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    for prog_num in sorted(programs_mc.keys()):
        mc = programs_mc[prog_num]
        active = find_active_steps(mc)
        print(f"  Program {prog_num}: {len(active)} active steps, "
              f"range {active[0]}-{active[-1]}" if active else
              f"  Program {prog_num}: no active steps")


if __name__ == "__main__":
    main()
