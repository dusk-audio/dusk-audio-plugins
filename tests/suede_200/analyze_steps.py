#!/usr/bin/env python3
"""Analyze microcode step structure for signal flow tracing."""
import sys
sys.path.insert(0, '.')
from wcs_engine import MICROCODE

def decode_step(word):
    mi31_24 = (word >> 24) & 0xFF
    mi23_16 = (word >> 16) & 0xFF
    mi15_8  = (word >> 8) & 0xFF
    mi7_0   = word & 0xFF

    wai = mi31_24 & 7
    ctrl = (mi31_24 >> 3) & 0x1F
    ofst = (mi15_8 << 8) | mi7_0
    hasCoeff = mi23_16 != 0xFF
    isNop = mi31_24 == 0xFF and mi23_16 == 0xFF

    cCode = acc0 = rad = rai = 0
    if hasCoeff:
        c8 = (mi23_16 >> 0) & 1
        c1 = (mi23_16 >> 1) & 1
        c2 = (mi23_16 >> 2) & 1
        c3 = (mi23_16 >> 3) & 1
        cCode = (c8 << 3) | (c3 << 2) | (c2 << 1) | c1
        acc0 = (mi23_16 >> 4) & 1
        rad = (mi23_16 >> 5) & 3
        rai = (mi23_16 >> 7) & 1

    mem_write = bool((ctrl & 0x10) != 0 and ctrl != 0x1F)
    return dict(wai=wai, ctrl=ctrl, ofst=ofst, hasCoeff=hasCoeff, isNop=isNop,
                cCode=cCode, acc0=acc0, rad=rad, rai=rai, mem_write=mem_write)

def analyze_program(prog_idx, half='left'):
    prog = MICROCODE[prog_idx]
    start = 0 if half == 'left' else 64
    end = 64 if half == 'left' else 128

    names = ['Concert Hall', 'Plate', 'Chamber', 'Rich Plate', 'Rich Splits', 'Inverse Rooms']
    print("=== Program %d (%s) - %s Channel (Steps %d-%d) ===" % (prog_idx, names[prog_idx], half.title(), start, end-1))
    print("%4s %6s %9s  %5s %4s %5s %6s %5s %4s %4s %6s %s" % (
        "Step", "Offset", "Delay_ms", "CTRL", "WAI", "HasC", "cCode", "ACC0", "RAD", "RAI", "MemWr", "Notes"))
    print("-" * 110)

    input_ofst = None
    output_ofst = None
    coeff_steps = []
    routing_steps = []

    for i in range(start, end):
        s = decode_step(prog[i])
        delay_ms = s['ofst'] / 20.480

        notes = ''
        if s['isNop']:
            notes = 'NOP'
        elif s['ctrl'] == 0x1E and s['wai'] == 2:
            notes = '** INPUT **'
            input_ofst = s['ofst']
        elif s['ctrl'] == 0x1E and s['wai'] == 1 and not s['hasCoeff']:
            notes = '** OUTPUT **'
            output_ofst = s['ofst']
        elif not s['hasCoeff'] and s['ctrl'] != 0x1F:
            notes = 'ROUTING wai=%d' % s['wai']
            if s['mem_write']:
                notes += ' +WR'
            routing_steps.append((i, s))
        elif s['hasCoeff']:
            src = 'mem' if s['rai'] else 'r%d' % s['rad']
            acc = '+=' if s['acc0'] else '='
            notes = 'r%d%s%s*c%d' % (s['wai'], acc, src, s['cCode'])
            if s['mem_write']:
                notes += ' +memwr'
            coeff_steps.append((i, s))

        cc = str(s['cCode']) if s['hasCoeff'] else '-'
        a0 = str(s['acc0']) if s['hasCoeff'] else '-'
        rd = str(s['rad']) if s['hasCoeff'] else '-'
        ri = str(s['rai']) if s['hasCoeff'] else '-'

        print("%4d %6d %9.1f  0x%02X %4d %5s %6s %5s %4s %4s %6s %s" % (
            i, s['ofst'], delay_ms, s['ctrl'], s['wai'], str(s['hasCoeff']),
            cc, a0, rd, ri, str(s['mem_write']), notes))

    print()
    if input_ofst is not None:
        print("Input offset: %d (%.1fms)" % (input_ofst, input_ofst/20.48))
    if output_ofst is not None:
        print("Output offset: %d (%.1fms)" % (output_ofst, output_ofst/20.48))
    if input_ofst and output_ofst:
        diff = output_ofst - input_ofst
        print("Direct path: %d samples (%.2fms)" % (diff, diff/20.48))

    print("\nCoefficient steps: %d" % len(coeff_steps))
    print("Routing steps: %d" % len(routing_steps))

    # Show coeff steps sorted by offset
    coeff_steps.sort(key=lambda x: x[1]['ofst'])
    print("\nCoefficient step offsets (sorted):")
    for step_i, s in coeff_steps:
        src = 'mem' if s['rai'] else 'r%d' % s['rad']
        print("  Step %2d: offset %5d (%7.1fms) c%d %s->r%d %s%s" % (
            step_i, s['ofst'], s['ofst']/20.48, s['cCode'], src, s['wai'],
            '+=' if s['acc0'] else '=',
            ' +memwr' if s['mem_write'] else ''))

    # Show routing steps
    if routing_steps:
        print("\nRouting steps:")
        for step_i, s in routing_steps:
            print("  Step %2d: offset %5d (%7.1fms) ctrl=0x%02X wai=%d memwr=%s" % (
                step_i, s['ofst'], s['ofst']/20.48, s['ctrl'], s['wai'], s['mem_write']))

    # Distance analysis from input
    if input_ofst is not None and coeff_steps:
        print("\nPropagation from input (offset %d):" % input_ofst)
        for step_i, s in coeff_steps:
            if s['rai']:  # Memory-reading steps
                # How many samples until write_ptr makes the input visible
                # Input writes at: wp - input_ofst
                # Coeff reads at: wp - coeff_ofst
                # For coeff to see input: wp - coeff_ofst == old_wp - input_ofst
                # new_wp = old_wp + (input_ofst - coeff_ofst)
                delay = input_ofst - s['ofst']
                if delay < 0:
                    delay += 65536
                print("  Step %2d (offset %5d): input arrives after %d samples (%.1fms)" % (
                    step_i, s['ofst'], delay, delay/20.48))

# Analyze Programs 0 and 1
for prog_idx in [0, 1]:
    analyze_program(prog_idx, 'left')
    print("\n" + "=" * 110 + "\n")
