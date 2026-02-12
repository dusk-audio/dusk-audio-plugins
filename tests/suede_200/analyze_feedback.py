#!/usr/bin/env python3
"""Analyze register feedback paths in WCS microcode.

Goal: Understand why the engine self-oscillates even when:
- Non-coefficient routing restricted to ctrl=0x1E only
- Per-step damping applied to coefficient results
- 16-bit clamps applied everywhere

The remaining feedback MUST be through coefficient steps with rai=0
(register-to-register), where registers persist between samples.
"""
import sys
sys.path.insert(0, '.')
from wcs_engine import MICROCODE, PROGRAM_NAMES, DecodedStep

def analyze_register_feedback():
    print("=" * 80)
    print("REGISTER FEEDBACK ANALYSIS — Coefficient steps with rai=0")
    print("These steps read from REGISTERS (not memory), creating inter-sample feedback")
    print("=" * 80)

    for prog_idx in range(6):
        print(f"\n{'─' * 80}")
        print(f"Program {prog_idx}: {PROGRAM_NAMES[prog_idx]}")
        print(f"{'─' * 80}")

        # Track register data flow
        reg_writers = {r: [] for r in range(8)}  # Who writes to each register
        reg_readers = {r: [] for r in range(8)}  # Who reads from each register

        coeff_steps = []
        noncoeff_steps = []

        for step_idx in range(128):
            word = MICROCODE[prog_idx][step_idx]
            s = DecodedStep.decode(word)
            if s.isNop:
                continue

            if s.hasCoeff:
            if s.hasCoeff:
                src_type = 'MEM' if s.rai else f'REG[{s.rad}]'
                acc = '+=' if s.acc0 else '='
                sign = '-' if (s.ctrl & 0x08) else '+'
                mwr = 'MWR' if (s.ctrl & 0x10) else '   '
                ctrl_low = s.ctrl & 0x07  # bits 0-2
                coeff_steps.append((step_idx, s, src_type, acc, sign, mwr, ctrl_low))
                reg_writers[s.wai].append(step_idx)
                if not s.rai:
                    reg_readers[s.rad].append(step_idx)
            else:
                noncoeff_steps.append((step_idx, s))
                if s.ctrl == 0x1E:
                    reg_writers[s.wai].append(step_idx)

        # Show register-to-register coefficient steps
        print(f"\n  Coefficient steps with rai=0 (register source):")
        print(f"  {'Step':>4s} {'Half':>4s} {'Operation':>25s} {'CTRL':>6s} {'Low3':>5s} "
              f"{'Sign':>4s} {'MWR':>3s} {'Ofst':>5s}")
        reg2reg_count = 0
        for step_idx, s, src_type, acc, sign, mwr, ctrl_low in coeff_steps:
            if s.rai:
                continue  # Skip memory-source steps
            half = "L" if step_idx < 64 else "R"
            op = f"r{s.wai}{acc}{sign}r{s.rad}*c{s.cCode:X}"
            print(f"  {step_idx:4d} {half:>4s} {op:>25s} 0x{s.ctrl:02X}  "
                  f"{ctrl_low:03b}   {sign:>2s} {mwr:>3s} {s.ofst:5d}")
            reg2reg_count += 1

        # Show memory-source coefficient steps for comparison
        print(f"\n  Coefficient steps with rai=1 (memory source): {len(coeff_steps) - reg2reg_count}")
        print(f"  Coefficient steps with rai=0 (register source): {reg2reg_count}")

        # Analyze CTRL bits 0-2 distribution for coefficient steps
        ctrl_low_dist = {}
        for step_idx, s, src_type, acc, sign, mwr, ctrl_low in coeff_steps:
            key = (ctrl_low, s.rai)
            if key not in ctrl_low_dist:
                ctrl_low_dist[key] = 0
            ctrl_low_dist[key] += 1

        print(f"\n  CTRL bits 0-2 distribution:")
        for (cl, rai), count in sorted(ctrl_low_dist.items()):
            src = "mem" if rai else "reg"
            print(f"    bits[2:0]={cl:03b} ({cl}) src={src}: {count} steps")

        # Trace register chains
        print(f"\n  Register data flow:")
        for r in range(8):
            writers = reg_writers[r]
            readers = reg_readers[r]
            if writers or readers:
                print(f"    r{r}: written by steps {writers[:10]}, read by steps {readers[:10]}")

    # Cross-program comparison of CTRL low bits
    print(f"\n\n{'=' * 80}")
    print("CTRL bits 0-2 cross-program summary (coefficient steps only)")
    print("=" * 80)

    all_ctrl_full = {}
    for prog_idx in range(6):
        for step_idx in range(128):
            word = MICROCODE[prog_idx][step_idx]
            s = DecodedStep.decode(word)
            if s.isNop or not s.hasCoeff:
                continue
            key = s.ctrl
            if key not in all_ctrl_full:
                all_ctrl_full[key] = []
            all_ctrl_full[key].append((prog_idx, step_idx, s))

    print(f"\n  {'CTRL':>6s} {'Binary':>7s} {'MWR':>4s} {'OP/':>4s} {'b2':>3s} {'b1':>3s} "
          f"{'b0':>3s} {'Count':>6s} {'rai=0':>6s} {'rai=1':>6s} {'acc0':>5s}")
    print("  " + "-" * 70)
    for ctrl in sorted(all_ctrl_full.keys()):
        entries = all_ctrl_full[ctrl]
        mwr = "Y" if (ctrl >> 4) & 1 else " "
        op = "Y" if (ctrl >> 3) & 1 else " "
        b2 = (ctrl >> 2) & 1
        b1 = (ctrl >> 1) & 1
        b0 = ctrl & 1
        rai0 = sum(1 for _, _, s in entries if not s.rai)
        rai1 = sum(1 for _, _, s in entries if s.rai)
        acc = sum(1 for _, _, s in entries if s.acc0)
        print(f"  0x{ctrl:02X}   {ctrl:05b}   {mwr:>2s}   {op:>2s}  {b2}   {b1}   "
              f"{b0}   {len(entries):4d}   {rai0:4d}   {rai1:4d}   {acc:3d}")

    # Key question: are there coefficient steps where CTRL bits 0-2
    # might indicate "don't actually multiply" or "don't accumulate"?
    print(f"\n\n{'=' * 80}")
    print("HYPOTHESIS: CTRL bits 0-2 may encode MCEN/ (coefficient multiply enable)")
    print("Testing: do certain bit patterns correlate with specific behaviors?")
    print("=" * 80)

    # Check if all CTRL values for coefficient steps share common low bit patterns
    for ctrl in sorted(all_ctrl_full.keys()):
        entries = all_ctrl_full[ctrl]
        low3 = ctrl & 0x07
        # Check coefficient code distribution
        ccodes = {}
        for _, _, s in entries:
            ccodes[s.cCode] = ccodes.get(s.cCode, 0) + 1
        ccode_str = ", ".join(f"c{c:X}:{n}" for c, n in sorted(ccodes.items()))
        print(f"  CTRL=0x{ctrl:02X} (low={low3:03b}): {ccode_str}")


def analyze_per_sample_accumulation():
    """Simulate one sample and trace register values to find feedback paths."""
    print(f"\n\n{'=' * 80}")
    print("PER-SAMPLE REGISTER TRACE — Program 0 (Concert Hall)")
    print("Shows how registers accumulate within a single sample")
    print("=" * 80)

    prog_idx = 0
    regs = [0.0] * 8
    regs[2] = 0.1  # Simulated input

    print(f"\n  Initial: regs = {[f'{r:.4f}' for r in regs]}")

    for step_idx in range(64):  # Left half only
        word = MICROCODE[prog_idx][step_idx]
        s = DecodedStep.decode(word)
        if s.isNop:
            continue

        old_regs = regs.copy()

        if s.hasCoeff:
            if s.rai:
                mul_input = 0.0  # Memory is empty at start
            else:
                mul_input = regs[s.rad]

            result = mul_input * 0.5  # Arbitrary coefficient
            if s.ctrl & 0x08:
                result = -result

            if s.acc0:
                regs[s.wai] += result
            else:
                regs[s.wai] = result

            src = f"mem[{s.ofst}]" if s.rai else f"r{s.rad}({old_regs[s.rad]:.4f})"
            acc = "+=" if s.acc0 else "="
            sign = "-" if (s.ctrl & 0x08) else "+"
            if abs(result) > 1e-10 or abs(regs[s.wai]) > 1e-10:
                print(f"  Step {step_idx:3d}: r{s.wai}{acc}{sign}{src}*c{s.cCode:X} "
                      f"→ result={result:.6f} → r{s.wai}={regs[s.wai]:.6f} "
                      f"  CTRL=0x{s.ctrl:02X}")
        elif s.ctrl == 0x1E:
            regs[s.wai] = 0.0  # Memory empty
            if abs(old_regs[s.wai]) > 1e-10:
                print(f"  Step {step_idx:3d}: r{s.wai}=mem[{s.ofst}] (I/O route) "
                      f"→ r{s.wai}={regs[s.wai]:.6f}  CTRL=0x{s.ctrl:02X}")

    print(f"\n  Final: regs = {[f'{r:.6f}' for r in regs]}")
    # Show which registers have non-zero values (these persist to next sample)
    print(f"  Non-zero registers that persist to next sample:")
    for i, v in enumerate(regs):
        if abs(v) > 1e-10:
            print(f"    r{i} = {v:.6f}")


if __name__ == '__main__':
    analyze_register_feedback()
    analyze_per_sample_accumulation()
