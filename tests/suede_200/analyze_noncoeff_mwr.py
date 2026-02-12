#!/usr/bin/env python3
"""Find non-coefficient steps with MWR set that aren't ctrl=0x1E or 0x1F.
These create unity-gain memory write paths that bypass coefficient control."""
import sys
sys.path.insert(0, '.')
from wcs_engine import MICROCODE, PROGRAM_NAMES, DecodedStep

print("=" * 80)
print("Non-coefficient MWR steps (ctrl != 0x1E, 0x1F) — UNITY GAIN FEEDBACK")
print("These steps write register values to memory WITHOUT coefficient attenuation")
print("=" * 80)

for prog_idx in range(6):
    found = []
    for step_idx in range(128):
        word = MICROCODE[prog_idx][step_idx]
        s = DecodedStep.decode(word)
        if s.isNop or s.hasCoeff:
            continue
        # Check if MWR set but not known I/O or NOP
        if (s.ctrl & 0x10) and s.ctrl not in (0x1E, 0x1F):
            found.append((step_idx, s))

    if found:
        print(f"\n  Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} — {len(found)} unity-gain MWR steps")
        for step_idx, s in found:
            half = "L" if step_idx < 64 else "R"
            print(f"    Step {step_idx:3d} ({half}): ctrl=0x{s.ctrl:02X} ({s.ctrl:05b}) "
                  f"wai=r{s.wai} ofst={s.ofst}")
    else:
        print(f"\n  Program {prog_idx}: {PROGRAM_NAMES[prog_idx]} — none")

# Also check: do the current do_mem_write conditions catch non-coeff steps?
print(f"\n{'=' * 80}")
print("Verification: which non-coeff steps currently trigger do_mem_write?")
print("  do_mem_write = (ctrl & 0x10) && (ctrl != 0x1F)")
print("=" * 80)

for prog_idx in range(6):
    count_mwr_noncoeff = 0
    count_mwr_1e = 0
    count_mwr_other = 0
    for step_idx in range(128):
        word = MICROCODE[prog_idx][step_idx]
        s = DecodedStep.decode(word)
        if s.isNop or s.hasCoeff:
            continue
        do_mem_write = (s.ctrl & 0x10) and (s.ctrl != 0x1F)
        if do_mem_write:
            count_mwr_noncoeff += 1
            if s.ctrl == 0x1E:
                count_mwr_1e += 1
            else:
                count_mwr_other += 1
    print(f"  P{prog_idx} {PROGRAM_NAMES[prog_idx]:15s}: {count_mwr_noncoeff} non-coeff MWR "
          f"({count_mwr_1e} are 0x1E, {count_mwr_other} are OTHER/unity-gain)")
