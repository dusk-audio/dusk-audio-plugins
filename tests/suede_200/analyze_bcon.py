#!/usr/bin/env python3
"""
Analyze BCON (Bus Control) signals in WCS microcode.

From the Lexicon 200 schematics (ARU board, Sheet 3):
  - BCON is bits 0-1 of the CTRL field (MI27-28)
  - CTRL = bits [7:3] of MI31-24 byte = (mi31_24 >> 3) & 0x1F
  - So BCON = ctrl & 0x03

From the service manual (page 4-34):
  - Accumulator (U11,U12,U14,U16) - controlled by BCON1/
  - Transfer Register (U7,U9) - controlled by MWR/ and XCLK/
  - Register File (U38-U43) - controlled by AREG/ via BCON3

BCON is decoded by T&C board into:
  BCON0, BCON1/, BCON2/, BCON3, BCON5

This script analyzes what BCON values appear in the microcode and how
they correlate with coefficient steps, memory writes, and other operations.
"""

import sys
sys.path.insert(0, '.')
from wcs_engine import MICROCODE, DecodedStep, PROGRAM_NAMES
from collections import Counter, defaultdict

def analyze_program(program_idx, microcode):
    """Analyze BCON patterns for a single program."""
    steps = [DecodedStep.decode(w) for w in microcode]

    print(f"\n{'='*70}")
    print(f"Program {program_idx}: {PROGRAM_NAMES[program_idx]}")
    print(f"{'='*70}")

    # Count BCON values for different step types
    bcon_coeff = Counter()
    bcon_noncoeff = Counter()
    bcon_nop = Counter()

    # Detailed breakdown
    coeff_details = defaultdict(list)
    noncoeff_details = defaultdict(list)

    for i, step in enumerate(steps):
        bcon = step.ctrl & 0x03
        half = "L" if i < 64 else "R"
        if step.isNop:
            bcon_nop[bcon] += 1
            continue

        if step.hasCoeff:
            bcon_coeff[bcon] += 1
            mwr = (step.ctrl >> 4) & 1
            mcen = (step.ctrl >> 3) & 1
            op = (step.ctrl >> 2) & 1
            coeff_details[bcon].append({
                'step': i, 'half': half, 'ctrl': step.ctrl,
                'mwr': mwr, 'mcen': mcen, 'op': op,
                'wai': step.wai, 'rai': step.rai, 'rad': step.rad,
                'acc0': step.acc0, 'cCode': step.cCode, 'ofst': step.ofst
            })
        else:
            bcon_noncoeff[bcon] += 1
            mwr = (step.ctrl >> 4) & 1
            mcen = (step.ctrl >> 3) & 1
            op = (step.ctrl >> 2) & 1
            noncoeff_details[bcon].append({
                'step': i, 'half': half, 'ctrl': step.ctrl,
                'mwr': mwr, 'mcen': mcen, 'op': op,
                'wai': step.wai, 'ofst': step.ofst
            })

    print(f"\n  BCON distribution (coefficient steps):")
    for bcon in sorted(bcon_coeff.keys()):
        print(f"    BCON={bcon} ({bcon:02b}): {bcon_coeff[bcon]} steps")

    print(f"\n  BCON distribution (non-coefficient steps):")
    for bcon in sorted(bcon_noncoeff.keys()):
        print(f"    BCON={bcon} ({bcon:02b}): {bcon_noncoeff[bcon]} steps")

    print(f"\n  BCON distribution (NOP steps):")
    for bcon in sorted(bcon_nop.keys()):
        print(f"    BCON={bcon} ({bcon:02b}): {bcon_nop[bcon]} steps")

    # Detailed analysis of each BCON value for coefficient steps
    print(f"\n  --- Coefficient step BCON details ---")
    for bcon in sorted(coeff_details.keys()):
        entries = coeff_details[bcon]
        mwr_count = sum(1 for e in entries if e['mwr'])
        mcen_count = sum(1 for e in entries if e['mcen'])
        op_count = sum(1 for e in entries if e['op'])
        acc_count = sum(1 for e in entries if e['acc0'])
        load_count = len(entries) - acc_count
        rai_mem = sum(1 for e in entries if e['rai'])
        rai_reg = len(entries) - rai_mem

        print(f"\n    BCON={bcon} ({bcon:02b}): {len(entries)} coefficient steps")
        print(f"      MWR set: {mwr_count}, MCEN/ set: {mcen_count}, OP/ set: {op_count}")
        print(f"      ACC (accumulate): {acc_count}, LOAD: {load_count}")
        print(f"      RAI=MEM: {rai_mem}, RAI=REG: {rai_reg}")

        # Show unique CTRL values
        ctrl_values = Counter(e['ctrl'] for e in entries)
        print(f"      CTRL values: {dict(ctrl_values)}")

        # Show WAI distribution
        wai_values = Counter(e['wai'] for e in entries)
        print(f"      WAI (write addr): {dict(wai_values)}")

        # Show C-code distribution
        ccode_values = Counter(e['cCode'] for e in entries)
        print(f"      C-codes used: {dict(sorted(ccode_values.items()))}")

        # Show first few examples
        print(f"      Examples (first 5):")
        for e in entries[:5]:
            flags = []
            if e['mwr']: flags.append('MWR')
            if e['mcen']: flags.append('MCEN/')
            if e['op']: flags.append('OP/')
            mode = 'ACC' if e['acc0'] else 'LOAD'
            src = f"MEM[{e['ofst']}]" if e['rai'] else f"REG[{e['rad']}]"
            print(f"        Step {e['step']:3d}({e['half']}): CTRL=0x{e['ctrl']:02X} "
                  f"WAI={e['wai']} C{e['cCode']:X} {mode} {src} "
                  f"{'  '.join(flags)}")

    # Detailed analysis of non-coefficient steps
    print(f"\n  --- Non-coefficient step BCON details ---")
    for bcon in sorted(noncoeff_details.keys()):
        entries = noncoeff_details[bcon]
        print(f"\n    BCON={bcon} ({bcon:02b}): {len(entries)} non-coeff steps")

        ctrl_values = Counter(e['ctrl'] for e in entries)
        print(f"      CTRL values: ", end="")
        for ctrl, count in sorted(ctrl_values.items()):
            mwr = (ctrl >> 4) & 1
            mcen = (ctrl >> 3) & 1
            op = (ctrl >> 2) & 1
            flags = []
            if mwr: flags.append('MWR')
            if mcen: flags.append('MCEN/')
            if op: flags.append('OP/')
            print(f"0x{ctrl:02X}({'+'.join(flags) if flags else 'none'})×{count}", end="  ")
        print()

        wai_values = Counter(e['wai'] for e in entries)
        print(f"      WAI: {dict(wai_values)}")

        # Show examples
        for e in entries[:5]:
            flags = []
            if e['mwr']: flags.append('MWR')
            if e['mcen']: flags.append('MCEN/')
            if e['op']: flags.append('OP/')
            print(f"        Step {e['step']:3d}({e['half']}): CTRL=0x{e['ctrl']:02X} "
                  f"WAI={e['wai']} OFST={e['ofst']} {'  '.join(flags)}")

    # Cross-reference: look at BCON transitions (what follows what?)
    print(f"\n  --- BCON transitions (consecutive active steps) ---")
    prev_bcon = None
    transitions = Counter()
    for i, step in enumerate(steps):
        if step.isNop:
            continue
        bcon = step.ctrl & 0x03
        if prev_bcon is not None:
            transitions[(prev_bcon, bcon)] += 1
        prev_bcon = bcon

    for (a, b), count in sorted(transitions.items()):
        print(f"    BCON {a} → {b}: {count} times")


def analyze_all():
    print("BCON (Bus Control) Analysis — Lexicon 200 WCS Microcode")
    print("=" * 70)
    print()
    print("CTRL bit mapping from schematics:")
    print("  Bit 4 (0x10) = MWR     (Memory Write)")
    print("  Bit 3 (0x08) = MCEN/   (Memory Clock Enable, active low)")
    print("  Bit 2 (0x04) = OP/     (Negate/subtract)")
    print("  Bit 1 (0x02) = BCON[1] (Bus Control high)")
    print("  Bit 0 (0x01) = BCON[0] (Bus Control low)")

    for prog_idx in range(6):
        analyze_program(prog_idx, MICROCODE[prog_idx])

    # Summary across all programs
    print(f"\n\n{'='*70}")
    print("SUMMARY: BCON value semantics hypothesis")
    print(f"{'='*70}")
    print("""
Based on the ARU schematic (Sheet 3) and T&C signal decoding:

BCON controls data routing from ALU output to destinations:
  - BCON=0 (00): ???
  - BCON=1 (01): ??? (BCON1/ → accumulator latch)
  - BCON=2 (10): ??? (BCON2/ → output buffer)
  - BCON=3 (11): ??? (BCON3 → AREG/ register file write)

The T&C board decodes BCON into:
  BCON0 = bit 0 of BCON
  BCON1/ = inverted decode of BCON value 1
  BCON2/ = inverted decode of BCON value 2
  BCON3 = decode of BCON value 3
  BCON5 = ??? (maybe combined decode)
""")


if __name__ == '__main__':
    analyze_all()
