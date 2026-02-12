#!/usr/bin/env python3
"""Analyze CTRL field values across all programs to identify OP/ (sign) bit."""
import sys
sys.path.insert(0, '.')
from wcs_engine import MICROCODE, PROGRAM_NAMES, DecodedStep

def decode_all():
    """Decode all steps and analyze ctrl field patterns."""

    print("=" * 80)
    print("CTRL Field Analysis — Finding OP/ (sign/negate) bit")
    print("=" * 80)

    # Collect all ctrl values across all programs
    all_ctrl_coeff = {}  # ctrl → list of (prog, step, context)
    all_ctrl_noncoeff = {}

    for prog_idx in range(6):
        for step_idx in range(128):
            word = MICROCODE[prog_idx][step_idx]
            s = DecodedStep.decode(word)
            if s.isNop:
                continue

            half = "L" if step_idx < 64 else "R"
            ctx = f"P{prog_idx}:{PROGRAM_NAMES[prog_idx][:4]} S{step_idx}{half}"

            if s.hasCoeff:
                src = 'mem' if s.rai else f'r{s.rad}'
                acc = '+=' if s.acc0 else '='
                desc = f"r{s.wai}{acc}{src}*c{s.cCode:X}"
                if s.ctrl not in all_ctrl_coeff:
                    all_ctrl_coeff[s.ctrl] = []
                all_ctrl_coeff[s.ctrl].append((prog_idx, step_idx, desc, s))
            else:
                else:
                kind = "OUTPUT" if s.ctrl == 0x1E else "ROUTING"
                print(f"  Step {step_idx:3d}: {kind}  ctrl=0x{s.ctrl:02X}")                if s.ctrl not in all_ctrl_noncoeff:
                    all_ctrl_noncoeff[s.ctrl] = []
                all_ctrl_noncoeff[s.ctrl].append((prog_idx, step_idx, kind, s))

    print("\n--- Coefficient step CTRL values ---")
    print(f"{'CTRL':>6s} {'Binary':>7s} {'Bit4':>4s} {'Bit3':>4s} {'Bit2':>4s} "
          f"{'Bit1':>4s} {'Bit0':>4s} {'Count':>6s} Examples")
    print("-" * 80)
    for ctrl in sorted(all_ctrl_coeff.keys()):
        entries = all_ctrl_coeff[ctrl]
        b4 = (ctrl >> 4) & 1
        b3 = (ctrl >> 3) & 1
        b2 = (ctrl >> 2) & 1
        b1 = (ctrl >> 1) & 1
        b0 = ctrl & 1
        examples = ", ".join(f"{e[2]}" for e in entries[:3])
        mwr = "MWR" if b4 else "   "
        print(f"0x{ctrl:02X}   {ctrl:05b}   {b4}    {b3}    {b2}    "
              f"{b1}    {b0}    {len(entries):4d}   {mwr} {examples}")

    print(f"\n--- Non-coefficient step CTRL values ---")
    for ctrl in sorted(all_ctrl_noncoeff.keys()):
        entries = all_ctrl_noncoeff[ctrl]
        kinds = set(e[2] for e in entries)
        b4 = (ctrl >> 4) & 1
        print(f"0x{ctrl:02X}   {ctrl:05b}   bit4={b4}  count={len(entries):3d}  "
              f"types: {', '.join(kinds)}")

    # Per-program ctrl distribution for coefficient steps
    print(f"\n{'=' * 80}")
    print("Per-program CTRL distribution (coefficient steps only)")
    print(f"{'=' * 80}")

    for prog_idx in range(6):
        ctrl_dist = {}
        for step_idx in range(128):
            word = MICROCODE[prog_idx][step_idx]
            s = DecodedStep.decode(word)
            if s.isNop or not s.hasCoeff:
                continue
            ctrl_dist[s.ctrl] = ctrl_dist.get(s.ctrl, 0) + 1

        print(f"\nProgram {prog_idx}: {PROGRAM_NAMES[prog_idx]}")
        total = sum(ctrl_dist.values())
        for ctrl in sorted(ctrl_dist.keys()):
            count = ctrl_dist[ctrl]
            b3 = (ctrl >> 3) & 1
            b4 = (ctrl >> 4) & 1
            mwr = "MWR" if b4 else "   "
            op3 = "OP?" if b3 else "   "
            print(f"  0x{ctrl:02X} ({ctrl:05b}): {count:3d} steps  {mwr} {op3}")
        print(f"  Total: {total} coefficient steps")

    # Hypothesis test: what fraction of coefficient steps have each bit set?
    print(f"\n{'=' * 80}")
    print("Bit frequency in coefficient steps (all programs)")
    print(f"{'=' * 80}")
    total = sum(len(v) for v in all_ctrl_coeff.values())
    for bit in range(5):
        count_set = sum(len(v) for ctrl, v in all_ctrl_coeff.items()
                        if (ctrl >> bit) & 1)
        pct = count_set / total * 100
        print(f"  Bit {bit}: {count_set:4d}/{total} ({pct:.1f}%) set")

    # Detailed look at Concert Hall steps that write to regs[1] (output register)
    print(f"\n{'=' * 80}")
    print("Steps writing to regs[1] (output register) — all programs")
    print(f"{'=' * 80}")
    for prog_idx in range(6):
        print(f"\nProgram {prog_idx}: {PROGRAM_NAMES[prog_idx]}")
        for step_idx in range(128):
            word = MICROCODE[prog_idx][step_idx]
            s = DecodedStep.decode(word)
            if s.isNop or s.wai != 1:
                continue
            if s.hasCoeff:
                src = 'mem' if s.rai else f'r{s.rad}'
                acc = '+=' if s.acc0 else '='
                b3 = (s.ctrl >> 3) & 1
                b4 = (s.ctrl >> 4) & 1
                sign = "-" if b3 else "+"
                mwr = "+MW" if b4 else ""
                print(f"  Step {step_idx:3d}: r1{acc}{src}*c{s.cCode:X}  "
                      f"ctrl=0x{s.ctrl:02X}({s.ctrl:05b}) "
                      f"bit3={b3}({sign}) bit4={b4}{mwr}  "
                      f"ofst={s.ofst}")
            else:
                kind = "INPUT" if s.ctrl == 0x1E and s.wai == 2 else \
                       "OUTPUT" if s.ctrl == 0x1E and s.wai == 1 else \
                       "ROUTING"
                print(f"  Step {step_idx:3d}: {kind}  ctrl=0x{s.ctrl:02X}")

decode_all()
