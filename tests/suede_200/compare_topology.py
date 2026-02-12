#!/usr/bin/env python3
"""Compare algorithm topologies across Suede 200 programs."""

import os

rom_dir = os.path.expanduser("~/Downloads/Suede 200 ROM")
with open(os.path.join(rom_dir, "v1r3rom4.bin"), "rb") as f:
    rom4 = f.read()
with open(os.path.join(rom_dir, "v1r3rom5.bin"), "rb") as f:
    rom5 = f.read()

RECORDS = {
    1: (rom4, 0x000), 2: (rom4, 0x333), 3: (rom4, 0x666),
    4: (rom4, 0x999), 5: (rom4, 0xCCC), 6: (rom5, 0x000),
}

# Extract MI24-31 bytes (operation/routing control) for each program
mi24_31 = {}
for prog, (rom, off) in RECORDS.items():
    data = rom[off + 0x133 : off + 0x133 + 128]
    if len(data) != 128:
        raise ValueError(f"Program {prog}: expected 128 bytes at offset 0x{off + 0x133:X}, got {len(data)}")
    mi24_31[prog] = data
print("MI24-31 topology comparison (operation bytes only):")
print("=" * 60)
for p1 in range(1, 7):
    for p2 in range(p1 + 1, 7):
        matches = sum(
            1 for i in range(128) if mi24_31[p1][i] == mi24_31[p2][i]
        )
        active_matches = sum(
            1 for i in range(128)
            if mi24_31[p1][i] == mi24_31[p2][i] and mi24_31[p1][i] != 0xFF
        )
        if matches > 120:
            label = "*** SAME ALGORITHM ***"
        elif matches > 90:
            label = "** SIMILAR (variant) **"
        elif matches > 50:
            label = "partial overlap"
        else:
            label = "different"
        print(
            f"  Prog {p1} vs {p2}: {matches}/128 match "
            f"(non-NOP: {active_matches}) -> {label}"
        )

# Detailed diffs for suspected same-algorithm pairs
for pair_name, p1, p2 in [
    ("Prog 3 vs 4", 3, 4),
    ("Prog 3 vs 5", 3, 5),
    ("Prog 3 vs 6", 3, 6),
    ("Prog 5 vs 6", 5, 6),
    ("Prog 1 vs 2", 1, 2),
]:
    diffs = [
        (i, mi24_31[p1][i], mi24_31[p2][i])
        for i in range(128)
        if mi24_31[p1][i] != mi24_31[p2][i]
    ]
    print(f"\n{pair_name} MI24-31 differences ({len(diffs)} total):")
    for i, va, vb in diffs[:20]:
        print(f"  Step {i:3d}: {va:02X} vs {vb:02X}")
    if len(diffs) > 20:
        print(f"  ... ({len(diffs) - 20} more)")

# MI16-23 analysis
print("\n\nMI16-23 (coefficient select byte):")
print("=" * 60)
for prog, (rom, off) in RECORDS.items():
    mi1623 = rom[off + 0x133 + 128 : off + 0x133 + 256]
    non_ff = sum(1 for b in mi1623 if b != 0xFF)
    # Show the non-FF positions
    positions = [i for i, b in enumerate(mi1623) if b != 0xFF]
    vals = [(i, mi1623[i]) for i in positions]
    print(f"  Prog {prog}: {non_ff}/128 non-0xFF bytes")
    if vals:
        for pos, val in vals[:20]:
            print(f"    Step {pos:3d}: 0x{val:02X}")
        if len(vals) > 20:
            print(f"    ... ({len(vals) - 20} more)")

# Step 0 for all programs
print("\n\nStep 0 full 32-bit word for all programs:")
print("=" * 60)
for prog, (rom, off) in RECORDS.items():
    b3 = rom[off + 0x133]  # MI24-31
    b2 = rom[off + 0x133 + 128]  # MI16-23
    b1 = rom[off + 0x233]  # MI8-15
    b0 = rom[off + 0x233 + 128]  # MI0-7
    word = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0
    print(f"  Prog {prog}: 0x{word:08X}  ({b3:02X} {b2:02X} {b1:02X} {b0:02X})")

# Check if step 0 is a NOP (all FF) in all programs
print("\n\nAlgorithm grouping summary:")
print("=" * 60)

# Group programs by MI24-31 similarity
groups = {1: [1], 2: [2]}  # Start with 1 and 2 as separate
remaining = [3, 4, 5, 6]

for p in remaining:
    best_match = None
    best_score = 0
    for gid, members in groups.items():
        ref = members[0]
        score = sum(1 for i in range(128) if mi24_31[p][i] == mi24_31[ref][i])
        if score > best_score:
            best_score = score
            best_match = gid
    if best_score > 100:  # Very similar
        groups[best_match].append(p)
    else:
        groups[p] = [p]

for gid, members in sorted(groups.items()):
    print(f"  Algorithm group (ref=Prog {gid}): Programs {members}")

# Show first 32 MI24-31 values side by side for all programs
print("\n\nMI24-31 first 32 steps (all programs side by side):")
print("=" * 60)
header = "Step  " + "  ".join(f"P{p}" for p in range(1, 7))
print(header)
print("-" * len(header))
for step in range(32):
    vals = "  ".join(f"{mi24_31[p][step]:02X}" for p in range(1, 7))
    print(f"  {step:2d}   {vals}")
