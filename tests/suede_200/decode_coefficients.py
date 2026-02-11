#!/usr/bin/env python3
"""
Suede 200 — Coefficient Table Bytecode Decoder

Decodes the 256-byte coefficient/patch table at record offset 0x033-0x132.
This bytecode controls how front-panel knobs modulate the WCS microcode.

Record layout (confirmed from ROM3 loading code):
  0x000-0x032: Header/config (51 bytes)
  0x033-0x05C: Extended config (delay params, DSP config) (42 bytes)
  0x05D-0x132: Coefficient table bytecode (up to 214 bytes, 0xFF-terminated)
  0x133-0x1B2: MI24-31 upper WCS (128 bytes)
  0x1B3-0x232: MI16-23 upper WCS (128 bytes)
  0x233-0x2B2: MI8-15 lower WCS (128 bytes)
  0x2B3-0x332: MI0-7 lower WCS (128 bytes)

Bytecode format (ROM3 interpreter at sub_04A3h):
  - Series of blocks, each starting with a control byte
  - Block terminates on 0xFF byte
  - Control byte:
      Bits 0-3: Entry count
      Bits 4-5: Entry type (0, 1, 2, 3)
      Bit 6: Flag (varies by type)
      Bit 7: Flag (varies by type)

Entry types:
  Type 0 (0x00): Simple WCS step list — [count], count × [step_ref]
  Type 1 (0x10): Coefficient entries — [count|flags], variable format
  Type 2 (0x20): 3-byte entries — [count], count × [step_ref, byte1, byte2]
  Type 3 (0x30): Skip/advance — just skip 1 byte

Runtime coefficient update (ROM3 sub_0A0Eh):
  Each processed entry becomes 3 bytes in RAM:
    Byte 0-1: WCS address (0xC100 + step for MI8-15 register)
    Byte 2: Coefficient control byte
      Bits 7-5 (0xE0): Scale factor (multiplied by pot position)
      Bits 4-0 (0x1F): Maximum value clamp

Pot curve lookup (ROM3 sub_0A94h):
  - 16 pot curves, 5 control points each
  - Table at ROM3 address 0x0ABF
  - Maps pot position (0-255) × scale factor → coefficient (0-31)

WCS field write (ROM3 sub_0903h):
  - Writes coefficient to MI16-23 (C8:C3:C2:C1 and ACC/RAD flags)
  - Also modifies MI8-15 (OFST_HI control bits)
  - Each coefficient written to TWO WCS addresses (stereo pair, DE and DE+2)
"""

import os
import sys

ROM_DIR = os.path.expanduser("~/Downloads/Suede 200 ROM")

RECORDS = {
    1: ("v1r3rom4.bin", 0x000),
    2: ("v1r3rom4.bin", 0x333),
    3: ("v1r3rom4.bin", 0x666),
    4: ("v1r3rom4.bin", 0x999),
    5: ("v1r3rom4.bin", 0xCCC),
    6: ("v1r3rom5.bin", 0x000),
}

SAMPLE_RATE = 20480


def load_rom(filename):
    path = os.path.join(ROM_DIR, filename)
    with open(path, "rb") as f:
        return f.read()


def load_roms():
    roms = {}
    for prog, (fn, _) in RECORDS.items():
        if fn not in roms:
            roms[fn] = load_rom(fn)
    return roms


def get_record(roms, prog):
    fn, off = RECORDS[prog]
    rom = roms[fn]
    return rom[off:off + 0x333]


def decode_header(record):
    """Decode the 51-byte header region."""
    header = {
        "raw": record[0x000:0x033],
        "id_byte": record[0],
    }
    # Known header fields:
    # record[0x000]: Program ID (1-6)
    # record[0x01A-0x01C]: 3-byte config copied to RAM 0xA2BE
    return header


def decode_extended_config(record):
    """Decode the extended config region (0x033-0x05C, 42 bytes).

    This region sits between the header and the coefficient bytecode.
    Contains delay parameter tables and DSP configuration.
    """
    config = record[0x033:0x05D]
    return {
        "raw": config,
        "hex": " ".join(f"{b:02X}" for b in config),
    }


def decode_coeff_bytecode(record, prog_num, verbose=True):
    """Decode the coefficient table bytecode starting at record offset 0x05D.

    Returns list of decoded blocks with their entries.
    """
    data = record[0x033:0x133]  # Full 256-byte region
    bytecode_start = 0x05D - 0x033  # Offset within the region = 0x2A = 42
    pos = bytecode_start

    blocks = []
    block_num = 0

    if verbose:
        print(f"\n  Coefficient Bytecode (Program {prog_num}):")
        print(f"  {'='*60}")
        print(f"  Region: record[0x033:0x132] (256 bytes)")
        print(f"  Bytecode starts at record offset 0x05D (byte 42 in region)")

    while pos < len(data):
        ctrl = data[pos]
        if ctrl == 0xFF:
            if verbose:
                print(f"\n  [{pos:3d}] 0xFF — End of bytecode")
            break

        count = ctrl & 0x0F
        entry_type = (ctrl >> 4) & 0x03
        flag6 = (ctrl >> 6) & 1
        flag7 = (ctrl >> 7) & 1

        block = {
            "offset": pos + 0x033,  # Record-relative offset
            "ctrl": ctrl,
            "count": count,
            "type": entry_type,
            "flag6": flag6,
            "flag7": flag7,
            "entries": [],
        }

        if verbose:
            print(f"\n  [{pos:3d}] Block #{block_num}: ctrl=0x{ctrl:02X} "
                  f"type={entry_type} count={count} "
                  f"flags=({flag7},{flag6})")

        pos += 1

        if count == 0:
            # Zero-count block — just the control byte
            if entry_type == 3:
                # Type 3: skip 1 byte
                if pos < len(data):
                    skip_byte = data[pos]
                    if verbose:
                        print(f"    Type 3 skip: 0x{skip_byte:02X}")
                    pos += 1
            blocks.append(block)
            block_num += 1
            continue

        if entry_type == 0:
            # Type 0: Simple WCS step reference list
            # Format: [sub_count], sub_count × [step_ref]
            if pos < len(data):
                sub_count = data[pos] & 0x0F
                pos += 1
                if verbose:
                    print(f"    Type 0: sub_count={sub_count}")
                for i in range(sub_count):
                    if pos < len(data):
                        step_ref = data[pos] & 0x7F
                        pos += 1
                        block["entries"].append({
                            "type": "step_ref",
                            "step": step_ref,
                        })
                        if verbose:
                            print(f"      Step {step_ref}")

        elif entry_type == 1:
            # Type 1: Coefficient entries with variable format
            # Format depends on flag in sub_count byte
            if pos < len(data):
                sub_byte = data[pos]
                sub_count = sub_byte & 0x0F
                has_extra = (sub_byte >> 7) & 1
                pos += 1
                if verbose:
                    print(f"    Type 1: sub_count={sub_count} "
                          f"has_extra={has_extra}")
                for i in range(sub_count):
                    entry = {"type": "coeff_type1"}
                    if has_extra and pos < len(data):
                        extra = data[pos]
                        pos += 1
                        entry["extra_byte"] = extra
                        if verbose:
                            # Extra byte is a WCS step reference
                            step = extra & 0x7F
                            print(f"      Extra step ref: {step}")
                        # Another step ref follows
                        if pos < len(data):
                            step_ref = data[pos] & 0x7F
                            pos += 1
                            entry["step"] = step_ref
                    if pos < len(data):
                        coeff_byte = data[pos]
                        pos += 1
                        entry["coeff"] = coeff_byte
                        if verbose:
                            print(f"      Coeff: 0x{coeff_byte:02X}")
                    block["entries"].append(entry)

        elif entry_type == 2:
            # Type 2: 3-byte coefficient entries
            # Format: [sub_count], sub_count × [step_ref, coeff1, coeff2]
            if pos < len(data):
                sub_count = data[pos] & 0x0F
                pos += 1
                if verbose:
                    print(f"    Type 2: sub_count={sub_count}")
                for i in range(sub_count):
                    if pos + 2 < len(data):
                        step_ref = data[pos] & 0x7F
                        coeff1 = data[pos + 1]
                        coeff2 = data[pos + 2]
                        pos += 3
                        entry = {
                            "type": "coeff_type2",
                            "step": step_ref,
                            "coeff1": coeff1,
                            "coeff2": coeff2,
                        }
                        block["entries"].append(entry)
                        if verbose:
                            # coeff2 is the control byte for runtime update
                            scale = (coeff2 >> 5) & 0x07
                            max_val = coeff2 & 0x1F
                            print(f"      Step {step_ref:3d}: "
                                  f"init=0x{coeff1:02X} "
                                  f"ctrl=0x{coeff2:02X} "
                                  f"(scale={scale}, max={max_val})")
                    else:
                        break

        elif entry_type == 3:
            # Type 3: Skip/advance
            if pos < len(data):
                skip = data[pos]
                pos += 1
                if verbose:
                    print(f"    Type 3 skip: 0x{skip:02X}")

        blocks.append(block)
        block_num += 1

    return blocks


def decode_runtime_coeff_table(record, prog_num):
    """Decode the runtime coefficient entries as used by sub_0A0Eh.

    The runtime coefficient table (built in RAM from bytecode) consists of
    3-byte entries: [WCS_addr_lo, WCS_addr_hi, control_byte]

    The control byte encodes:
      Bits 7-5: Scale factor (how much the pot affects this coefficient)
      Bits 4-0: Maximum value (coefficient clamp)

    This function tries to identify the runtime coefficient entries by
    analyzing which WCS steps have coefficient operations.
    """
    print(f"\n  Runtime Coefficient Mapping (Program {prog_num}):")
    print(f"  {'='*60}")

    # Extract WCS microcode to find steps with active coefficients
    mi31_24 = record[0x133:0x133 + 128]
    mi23_16 = record[0x1B3:0x1B3 + 128]
    mi15_8 = record[0x233:0x233 + 128]
    mi7_0 = record[0x2B3:0x2B3 + 128]

    print(f"\n  Steps with active coefficients (MI16-23 != 0xFF):")
    print(f"  {'Step':>4s}  {'MI24-31':>8s}  {'MI16-23':>8s}  {'C-code':>6s}  "
          f"{'ACC':>3s}  {'RAD':>3s}  {'RAI':>3s}  {'WAI':>3s}  {'CTRL':>5s}  "
          f"{'OFST':>6s}  {'ms':>8s}")

    coeff_steps = []
    for step in range(128):
        if mi23_16[step] != 0xFF:
            c8 = (mi23_16[step] >> 0) & 1
            c1 = (mi23_16[step] >> 1) & 1
            c2 = (mi23_16[step] >> 2) & 1
            c3 = (mi23_16[step] >> 3) & 1
            acc = (mi23_16[step] >> 4) & 1
            rad = (mi23_16[step] >> 5) & 3
            rai = (mi23_16[step] >> 7) & 1
            wai = mi31_24[step] & 7
            ctrl = (mi31_24[step] >> 3) & 0x1F
            c_code = (c8 << 3) | (c3 << 2) | (c2 << 1) | c1
            ofst = mi7_0[step] | (mi15_8[step] << 8)
            ms = (ofst / SAMPLE_RATE) * 1000.0 if ofst != 0xFFFF else 0

            coeff_steps.append({
                "step": step,
                "c_code": c_code,
                "acc": acc,
                "rad": rad,
                "rai": rai,
                "wai": wai,
                "ctrl": ctrl,
                "ofst": ofst,
                "ms": ms,
            })

            print(f"  {step:4d}  "
                  f"0x{mi31_24[step]:02X}      "
                  f"0x{mi23_16[step]:02X}      "
                  f"  0x{c_code:X}   "
                  f" {acc}    "
                  f" {rad}    "
                  f" {rai}    "
                  f" {wai}   "
                  f"0x{ctrl:02X}  "
                  f"0x{ofst:04X}  "
                  f"{ms:8.2f}")

    print(f"\n  Total coefficient steps: {len(coeff_steps)}")

    # Analyze coefficient code distribution
    c_codes = {}
    for s in coeff_steps:
        c = s["c_code"]
        c_codes[c] = c_codes.get(c, 0) + 1
    print(f"\n  Coefficient code distribution:")
    for c, count in sorted(c_codes.items()):
        print(f"    C={c:X}: {count} steps")

    return coeff_steps


def analyze_delay_params(record, prog_num):
    """Analyze the delay parameter/pot curve region (record 0x033-0x05C)."""
    config = record[0x033:0x05D]

    print(f"\n  Extended Config / Delay Parameters (Program {prog_num}):")
    print(f"  {'='*60}")
    print(f"  Record offset 0x033-0x05C (42 bytes):")

    # Hex dump
    for i in range(0, len(config), 16):
        chunk = config[i:i + 16]
        hex_str = " ".join(f"{b:02X}" for b in chunk)
        addr = 0x033 + i
        print(f"    0x{addr:03X}: {hex_str}")

    # Identify sub-regions by looking for patterns
    non_ff = [(i, b) for i, b in enumerate(config) if b != 0xFF]
    ff_count = len(config) - len(non_ff)
    print(f"\n    Active bytes: {len(non_ff)}/42 (rest are 0xFF padding)")

    # Look for the DSP config region (from ROM3 code: record + 0x1A → 3 bytes to RAM)
    # But 0x1A is relative to the info struct, not the record start
    # The delay param tables and DSP config are embedded here

    return config


def analyze_pot_curves():
    """Extract pot curve table from ROM3 at offset 0x0ABF.

    The pot curve table contains 16 curves × 5 control points.
    Used by sub_0A94h to map pot position → coefficient value.
    """
    rom3_path = os.path.join(ROM_DIR, "v1r3rom3.bin")
    with open(rom3_path, "rb") as f:
        rom3 = f.read()

    # Pot curve table at ROM3 local offset 0x0ABF
    table_offset = 0x0ABF
    table = rom3[table_offset:table_offset + 80]  # 16 × 5 = 80 bytes

    print(f"\n  Pot Curve Table (ROM3 offset 0x0ABF):")
    print(f"  {'='*60}")
    print(f"  16 curves × 5 control points each:")
    print(f"  {'Curve':>5s}  {'Pt0':>4s}  {'Pt1':>4s}  {'Pt2':>4s}  "
          f"{'Pt3':>4s}  {'Pt4':>4s}  {'Hex':>20s}")

    curves = []
    for i in range(16):
        pts = table[i * 5:(i + 1) * 5]
        curves.append(pts)
        hex_str = " ".join(f"{b:02X}" for b in pts)
        print(f"  {i:5d}  {pts[0]:4d}  {pts[1]:4d}  {pts[2]:4d}  "
              f"{pts[3]:4d}  {pts[4]:4d}  {hex_str}")

    return curves


def cross_program_comparison(roms):
    """Compare coefficient tables across programs."""
    print(f"\n{'='*70}")
    print(f"CROSS-PROGRAM COEFFICIENT COMPARISON")
    print(f"{'='*70}")

    all_configs = {}
    all_bytecodes = {}

    for prog in range(1, 7):
        record = get_record(roms, prog)
        all_configs[prog] = record[0x033:0x05D]
        all_bytecodes[prog] = record[0x05D:0x133]

    # Compare extended config regions
    print(f"\n  Extended config (0x033-0x05C) comparison:")
    for p1 in range(1, 7):
        for p2 in range(p1 + 1, 7):
            matches = sum(1 for i in range(42)
                          if all_configs[p1][i] == all_configs[p2][i])
            print(f"    Prog {p1} vs {p2}: {matches}/42 bytes match "
                  f"({matches/42*100:.0f}%)")

    # Compare bytecode regions
    print(f"\n  Bytecode (0x05D-0x132) comparison:")
    for p1 in range(1, 7):
        for p2 in range(p1 + 1, 7):
            bc1 = all_bytecodes[p1]
            bc2 = all_bytecodes[p2]
            matches = sum(1 for i in range(len(bc1))
                          if bc1[i] == bc2[i])
            non_ff = sum(1 for i in range(len(bc1))
                         if bc1[i] != 0xFF or bc2[i] != 0xFF)
            print(f"    Prog {p1} vs {p2}: {matches}/{len(bc1)} bytes match "
                  f"(non-FF: {non_ff})")


def main():
    print("=" * 70)
    print("SUEDE 200 — COEFFICIENT TABLE DECODER")
    print("=" * 70)

    roms = load_roms()

    # Pot curve table from ROM3
    analyze_pot_curves()

    for prog in range(1, 7):
        print(f"\n{'='*70}")
        print(f"PROGRAM {prog}")
        print(f"{'='*70}")

        record = get_record(roms, prog)

        # Extended config (delay params)
        analyze_delay_params(record, prog)

        # Raw bytecode hex dump
        bytecode = record[0x05D:0x133]
        non_ff = sum(1 for b in bytecode if b != 0xFF)
        print(f"\n  Raw Bytecode (0x05D-0x132): {non_ff}/{len(bytecode)} "
              f"non-0xFF bytes")
        for i in range(0, min(128, len(bytecode)), 16):
            chunk = bytecode[i:i + 16]
            hex_str = " ".join(f"{b:02X}" for b in chunk)
            addr = 0x05D + i
            print(f"    0x{addr:03X}: {hex_str}")
        if len(bytecode) > 128:
            print(f"    ... ({len(bytecode) - 128} more bytes)")

        # Decode bytecode structure
        decode_coeff_bytecode(record, prog, verbose=True)

        # Runtime coefficient analysis from WCS
        decode_runtime_coeff_table(record, prog)

    # Cross-program comparison
    cross_program_comparison(roms)


if __name__ == "__main__":
    main()
