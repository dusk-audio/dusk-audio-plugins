#!/usr/bin/env python3
"""
Analyze non-coefficient CTRL field values across all 6 programs.

Goal: Determine which non-coefficient steps actually perform delay tap routing
(memory -> register) vs other bus control operations. Currently ALL non-coefficient
steps with MWR=0 are treated as "read delay memory -> register", but this creates
excess feedback (RT60 4.35s vs 0.86s target).

For each non-coefficient, non-NOP step we examine:
  - CTRL value (5 bits)
  - WAI (write register)
  - OFST (delay offset)
  - Whether a subsequent coefficient step reads from this register (via RAI=0, RAD)

CTRL bit decoding (MI27-MI31):
  Bit 4 (MI31): MWR (memory write enable)
  Bit 3 (MI30): OP/ (operation select / sign)
  Bits 0-2 (MI27-29): BCON / offset strobe selects
"""

import sys
sys.path.insert(0, '.')
from collections import defaultdict
from wcs_engine import MICROCODE, PROGRAM_NAMES, DecodedStep

ORIGINAL_SR = 20480


def analyze_all_programs():
    """Analyze non-coefficient CTRL values across all programs."""

    print("=" * 100)
    print("NON-COEFFICIENT CTRL FIELD ANALYSIS — All 6 Programs")
    print("=" * 100)

    # Global counters
    global_ctrl_counts = defaultdict(int)
    global_ctrl_steps = defaultdict(list)  # ctrl -> list of (prog, step, decoded)

    for prog_idx in range(6):
        prog = MICROCODE[prog_idx]
        steps = [DecodedStep.decode(w) for w in prog]

        print(f"\n{'=' * 100}")
        print(f"Program {prog_idx}: {PROGRAM_NAMES[prog_idx]}")
        print(f"{'=' * 100}")

        # Categorize all steps
        nop_count = 0
        coeff_count = 0
        noncoeff_steps = []  # (step_idx, decoded)

        for i, s in enumerate(steps):
            if s.isNop:
                nop_count += 1
            elif s.hasCoeff:
                coeff_count += 1
            else:
                noncoeff_steps.append((i, s))

        print(f"\n  Step counts: {nop_count} NOP, {coeff_count} coefficient, "
              f"{len(noncoeff_steps)} non-coefficient (non-NOP)")

        # Group non-coefficient steps by CTRL value
        ctrl_groups = defaultdict(list)
        for step_idx, s in noncoeff_steps:
            ctrl_groups[s.ctrl].append((step_idx, s))
            global_ctrl_counts[s.ctrl] += 1
            global_ctrl_steps[s.ctrl].append((prog_idx, step_idx, s))

        # Print each non-coefficient step with details
        print(f"\n  {'Step':>4s} {'Half':>4s} {'CTRL':>6s} {'Binary':>7s} "
              f"{'MWR':>3s} {'WAI':>3s} {'OFST':>6s} {'Delay_ms':>9s} "
              f"{'FedBy':>12s} {'Notes'}")
        print(f"  {'-'*95}")

        for step_idx, s in noncoeff_steps:
            half = "L" if step_idx < 64 else "R"
            mwr = "Y" if (s.ctrl & 0x10) else "N"
            delay_ms = s.ofst / (ORIGINAL_SR / 1000.0) if s.ofst != 0xFFFF else 0.0

            # Check if any coefficient step in this channel reads from this register
            # via RAI=0 (register read) and RAD matching this step's WAI (mod 4)
            fed_by = _find_consumers(steps, step_idx, s.wai)

            # Classify
            notes = _classify_noncoeff(s)

            print(f"  {step_idx:4d} {half:>4s} 0x{s.ctrl:02X}  {s.ctrl:05b} "
                  f"  {mwr:>3s} {s.wai:3d} 0x{s.ofst:04X} {delay_ms:9.2f} "
                  f"{fed_by:>12s} {notes}")

        # Summary by CTRL value for this program
        print(f"\n  CTRL value summary for Program {prog_idx}:")
        print(f"  {'CTRL':>6s} {'Binary':>7s} {'Bit4(MWR)':>9s} {'Bit3(OP)':>8s} "
              f"{'Bits2-0':>7s} {'Count':>5s} {'Steps'}")
        print(f"  {'-'*75}")
        for ctrl in sorted(ctrl_groups.keys()):
            entries = ctrl_groups[ctrl]
            b4 = (ctrl >> 4) & 1
            b3 = (ctrl >> 3) & 1
            lo3 = ctrl & 0x07
            step_list = [e[0] for e in entries]
            print(f"  0x{ctrl:02X}  {ctrl:05b}   {b4:>9d} {b3:>8d}   "
                  f"  {lo3:03b}  {len(entries):5d} {step_list}")

    # === GLOBAL SUMMARY ===
    print(f"\n\n{'=' * 100}")
    print("GLOBAL SUMMARY — All non-coefficient CTRL values across all 6 programs")
    print(f"{'=' * 100}")

    print(f"\n{'CTRL':>6s} {'Binary':>7s} {'Bit4':>4s} {'Bit3':>4s} {'Lo3':>5s} "
          f"{'Total':>5s} {'Classification'}")
    print(f"{'-'*70}")

    for ctrl in sorted(global_ctrl_counts.keys()):
        count = global_ctrl_counts[ctrl]
        b4 = (ctrl >> 4) & 1
        b3 = (ctrl >> 3) & 1
        lo3 = ctrl & 0x07
        mwr_str = "MWR" if b4 else "   "

        # Classify this CTRL value
        classification = _classify_ctrl_value(ctrl)

        print(f"0x{ctrl:02X}  {ctrl:05b}   {b4:>4d} {b3:>4d}  {lo3:03b}  "
              f"{count:5d}   {mwr_str}  {classification}")

    # === DETAILED: Which non-coeff steps feed coefficient steps? ===
    print(f"\n\n{'=' * 100}")
    print("REGISTER FLOW: Non-coefficient -> Coefficient step connections")
    print("For each non-coeff step, does a later coefficient step read its register?")
    print(f"{'=' * 100}")

    for ctrl in sorted(global_ctrl_steps.keys()):
        entries = global_ctrl_steps[ctrl]
        feeds_coeff = 0
        no_consumer = 0

        details = []
        for prog_idx, step_idx, s in entries:
            prog = MICROCODE[prog_idx]
            all_steps = [DecodedStep.decode(w) for w in prog]
            consumers = _find_consumers(all_steps, step_idx, s.wai)
            if consumers != "none":
                feeds_coeff += 1
            else:
                no_consumer += 1
            details.append((prog_idx, step_idx, s, consumers))

        print(f"\n  CTRL=0x{ctrl:02X} ({ctrl:05b}): "
              f"{len(entries)} total, {feeds_coeff} feed coeff steps, "
              f"{no_consumer} have no coefficient consumer")

        # Show a sample of the steps
        shown = 0
        for prog_idx, step_idx, s, consumers in details:
            if shown >= 12:
                remaining = len(details) - shown
                print(f"    ... and {remaining} more")
                break
            half = "L" if step_idx < 64 else "R"
            delay_ms = s.ofst / (ORIGINAL_SR / 1000.0) if s.ofst != 0xFFFF else 0.0
            print(f"    P{prog_idx} Step {step_idx:3d} ({half}): "
                  f"wai={s.wai} ofst=0x{s.ofst:04X} ({delay_ms:7.1f}ms) "
                  f"-> {consumers}")
            shown += 1

    # === ANALYSIS: Which CTRL values are likely delay-tap-reads vs bus control? ===
    print(f"\n\n{'=' * 100}")
    print("ANALYSIS: Delay Tap Reads vs Bus Control / Other")
    print(f"{'=' * 100}")

    print("""
    For non-coefficient steps, the key question is:
    Does "MWR=0" always mean "read delay memory -> register"?

    Evidence from the data:
    """)

    # Check OFST patterns for each CTRL value
    for ctrl in sorted(global_ctrl_steps.keys()):
        entries = global_ctrl_steps[ctrl]
        offsets = [s.ofst for _, _, s in entries]
        wais = [s.wai for _, _, s in entries]
        has_real_offset = sum(1 for o in offsets if o != 0xFFFF and o != 0x0000)
        has_ffff = sum(1 for o in offsets if o == 0xFFFF)

        # Check consumer relationship
        all_consumers = []
        for prog_idx, step_idx, s in entries:
            prog = MICROCODE[prog_idx]
            all_steps = [DecodedStep.decode(w) for w in prog]
            cons = _find_consumers(all_steps, step_idx, s.wai)
            all_consumers.append(cons)

        feeds = sum(1 for c in all_consumers if c != "none")

        b4 = (ctrl >> 4) & 1
        mwr = "MWR=1" if b4 else "MWR=0"

        print(f"  CTRL=0x{ctrl:02X} ({mwr}):")
        print(f"    Count={len(entries)}, "
              f"Real offsets={has_real_offset}, FFFF offsets={has_ffff}")
        print(f"    Unique WAIs: {sorted(set(wais))}")
        print(f"    Feeds coeff steps: {feeds}/{len(entries)}")

        # Verdict
        if b4:
            if ctrl == 0x1E:
                print(f"    VERDICT: I/O injection (input/output extraction)")
            else:
                print(f"    VERDICT: Memory write (register -> delay memory)")
        elif ctrl == 0x1F:
            print(f"    VERDICT: NOP / pass-through")
        else:
            if has_real_offset > 0 and feeds > 0:
                print(f"    VERDICT: LIKELY delay tap read (memory -> register)")
            elif has_ffff == len(entries):
                print(f"    VERDICT: LIKELY bus control / mode select (OFST=FFFF)")
            else:
                print(f"    VERDICT: UNCLEAR — needs further investigation")
        print()

    # === Final classification table ===
    print(f"\n{'=' * 100}")
    print("FINAL CLASSIFICATION TABLE")
    print(f"{'=' * 100}")
    print(f"\n{'CTRL':>6s} {'MWR':>3s} {'OP':>3s} {'Lo3':>5s} "
          f"{'Action':>30s} {'Should read mem->reg?':>25s}")
    print(f"{'-'*80}")

    for ctrl in sorted(set(list(global_ctrl_counts.keys()) + [0x1E, 0x1F])):
        b4 = (ctrl >> 4) & 1
        b3 = (ctrl >> 3) & 1
        lo3 = ctrl & 0x07

        if ctrl == 0x1F:
            action = "NOP (all bits set)"
            should_read = "NO"
        elif ctrl == 0x1E:
            action = "I/O (input inject / output extract)"
            should_read = "NO (writes mem)"
        elif b4:
            action = f"MWR: write reg -> mem"
            should_read = "NO (writes mem)"
        else:
            # MWR=0, not NOP, not I/O
            entries = global_ctrl_steps.get(ctrl, [])
            if entries:
                offsets = [s.ofst for _, _, s in entries]
                has_real = any(o != 0xFFFF for o in offsets)
                if has_real:
                    action = "Bus read (delay tap?)"
                    should_read = "INVESTIGATE"
                else:
                    action = "Bus control (OFST=FFFF)"
                    should_read = "PROBABLY NO"
            else:
                action = "(not observed)"
                should_read = "N/A"

        print(f"0x{ctrl:02X}  {b4:>3d} {b3:>3d}  {lo3:03b}  "
              f"{action:>30s}  {should_read:>25s}")


def _find_consumers(steps, source_step_idx, source_wai):
    """Find coefficient steps that read from a given register.

    A coefficient step reads from register R when RAI=0 and RAD == R (mod 4).
    We search within the same half (left 0-63, right 64-127).
    """
    half_start = 0 if source_step_idx < 64 else 64
    half_end = 64 if source_step_idx < 64 else 128

    consumers = []
    for i in range(half_start, half_end):
        s = steps[i]
        if s.isNop or not s.hasCoeff:
            continue
        # Check if this coefficient step reads from the register file
        # RAI=0 means read from register, RAD selects which register (2-bit, so mod 4)
        if not s.rai and (s.rad == (source_wai % 4)):
            consumers.append(f"s{i}(c{s.cCode:X})")

    if consumers:
        return ",".join(consumers[:3])
    return "none"


def _classify_noncoeff(s):
    """Classify a non-coefficient step."""
    ctrl = s.ctrl
    wai = s.wai

    if ctrl == 0x1F:
        return "NOP"
    if ctrl == 0x1E:
        if wai == 2:
            return "INPUT (write input to delay mem)"
        elif wai == 1:
            return "OUTPUT (extract from delay mem)"
        else:
            return f"I/O wai={wai}"

    mwr = (ctrl & 0x10) != 0
    b3 = (ctrl >> 3) & 1
    lo3 = ctrl & 0x07

    if mwr:
        return f"MEM_WRITE (reg[{wai}] -> delay mem)"
    else:
        if s.ofst == 0xFFFF:
            return f"BUS_CTRL? (OFST=FFFF, lo3={lo3:03b})"
        else:
            delay_ms = s.ofst / (ORIGINAL_SR / 1000.0)
            return f"DELAY_TAP? (mem[{delay_ms:.1f}ms] -> reg[{wai}])"


def _classify_ctrl_value(ctrl):
    """High-level classification of a CTRL value for non-coefficient steps."""
    if ctrl == 0x1F:
        return "NOP (all control bits set)"
    if ctrl == 0x1E:
        return "I/O: Input injection or output extraction"

    b4 = (ctrl >> 4) & 1
    b3 = (ctrl >> 3) & 1
    lo3 = ctrl & 0x07

    parts = []
    if b4:
        parts.append("MWR (memory write)")
    else:
        parts.append("no-MWR")

    if b3:
        parts.append("OP/ set")
    else:
        parts.append("OP/ clear")

    parts.append(f"BCON={lo3:03b}")
    return " | ".join(parts)


def trace_feedback_paths():
    """Trace which non-coefficient MWR=0 steps create feedback paths.

    A feedback path exists when:
    1. Non-coeff step reads delay memory -> register R (MWR=0)
    2. A coefficient step reads register R (RAI=0, RAD=R%4)
    3. That coefficient step's output (via WAI) eventually gets written back to memory
       by another step (either coeff+MWR or non-coeff+MWR)

    Steps that DON'T create feedback paths are those where the register
    value is:
    - Never read by a coefficient step (dead write)
    - Read by a coeff step but the result is only used for output, never
      written back to delay memory
    """
    print(f"\n\n{'=' * 100}")
    print("FEEDBACK PATH TRACING")
    print("Which non-coeff MWR=0 steps create feedback loops through delay memory?")
    print(f"{'=' * 100}")

    for prog_idx in range(6):
        prog = MICROCODE[prog_idx]
        steps = [DecodedStep.decode(w) for w in prog]

        print(f"\n  Program {prog_idx}: {PROGRAM_NAMES[prog_idx]}")
        print(f"  {'-'*90}")

        for half_name, half_start, half_end in [("LEFT", 0, 64), ("RIGHT", 64, 128)]:
            noncoeff_reads = []
            for i in range(half_start, half_end):
                s = steps[i]
                if s.isNop or s.hasCoeff:
                    continue
                if s.ctrl == 0x1F:  # NOP
                    continue
                if s.ctrl & 0x10:  # MWR set = memory write, not read
                    continue
                noncoeff_reads.append((i, s))

            if not noncoeff_reads:
                continue

            print(f"\n    {half_name} channel (steps {half_start}-{half_end-1}):")

            for step_idx, s in noncoeff_reads:
                delay_ms = s.ofst / (ORIGINAL_SR / 1000.0)

                # Find all coefficient steps that read this register
                consumers = []
                for j in range(half_start, half_end):
                    cs = steps[j]
                    if cs.isNop or not cs.hasCoeff:
                        continue
                    if not cs.rai and (cs.rad == (s.wai % 4)):
                        consumers.append((j, cs))

                # For each consumer, trace if its output register gets written to memory
                creates_feedback = False
                consumer_details = []
                for cj, cs in consumers:
                    # Does this coefficient step's WAI register eventually get
                    # written to memory?
                    writes_mem = False
                    # Check if this coefficient step itself has MWR
                    if (cs.ctrl & 0x10) and cs.ctrl != 0x1F:
                        writes_mem = True

                    # Check if any later step writes reg[cs.wai] to memory
                    for k in range(half_start, half_end):
                        ks = steps[k]
                        if ks.isNop:
                            continue
                        # Non-coeff MWR step writing this register
                        if not ks.hasCoeff and (ks.ctrl & 0x10) and ks.ctrl != 0x1F:
                            if ks.wai == cs.wai:
                                writes_mem = True
                        # Coeff step that accumulates into this register and has MWR
                        if ks.hasCoeff and ks.wai == cs.wai:
                            if (ks.ctrl & 0x10) and ks.ctrl != 0x1F:
                                writes_mem = True

                    if writes_mem:
                        creates_feedback = True

                    consumer_details.append(
                        f"s{cj}(c{cs.cCode:X},w{cs.wai},{'MWR' if writes_mem else 'no-MWR'})"
                    )

                fb_str = "FEEDBACK" if creates_feedback else "no-feedback"
                cons_str = ", ".join(consumer_details) if consumer_details else "NO CONSUMERS"

                print(f"      Step {step_idx:3d}: ctrl=0x{s.ctrl:02X} wai={s.wai} "
                      f"ofst=0x{s.ofst:04X} ({delay_ms:7.1f}ms) "
                      f"[{fb_str}] -> {cons_str}")


def analyze_overwritten_registers():
    """Find non-coeff MWR=0 steps whose register is overwritten before being read.

    If a non-coeff step writes to register R, but another step overwrites R
    before any coefficient step reads it (RAI=0, RAD=R%4), then the delay
    tap read is "dead" — it has no effect on the signal path.
    """
    print(f"\n\n{'=' * 100}")
    print("DEAD REGISTER WRITES")
    print("Non-coeff MWR=0 steps where the register is overwritten before being read")
    print(f"{'=' * 100}")

    for prog_idx in range(6):
        prog = MICROCODE[prog_idx]
        steps = [DecodedStep.decode(w) for w in prog]

        print(f"\n  Program {prog_idx}: {PROGRAM_NAMES[prog_idx]}")

        for half_name, half_start, half_end in [("LEFT", 0, 64), ("RIGHT", 64, 128)]:
            for i in range(half_start, half_end):
                s = steps[i]
                if s.isNop or s.hasCoeff:
                    continue
                if s.ctrl == 0x1F or (s.ctrl & 0x10):
                    continue

                # This is a non-coeff MWR=0 step. Check if reg[wai] is
                # read by a coeff step (RAI=0, RAD=wai%4) before being
                # overwritten by another step writing to wai.
                is_read = False
                is_overwritten_first = False

                for j in range(i + 1, half_end):
                    js = steps[j]
                    if js.isNop:
                        continue

                    # Check if this step reads reg[wai] (coefficient step with RAI=0)
                    if js.hasCoeff and not js.rai and (js.rad == (s.wai % 4)):
                        is_read = True
                        break

                    # Check if this step overwrites reg[wai]
                    if js.wai == s.wai and not js.isNop:
                        # This step writes to the same register
                        # If it's before any read, the original write was dead
                        is_overwritten_first = True
                        overwriter = j
                        break

                if is_overwritten_first:
                    delay_ms = s.ofst / (ORIGINAL_SR / 1000.0)
                    print(f"    {half_name} Step {i:3d}: ctrl=0x{s.ctrl:02X} wai={s.wai} "
                          f"ofst=0x{s.ofst:04X} ({delay_ms:7.1f}ms) "
                          f"DEAD — overwritten by step {overwriter} before read")
                elif not is_read:
                    delay_ms = s.ofst / (ORIGINAL_SR / 1000.0)
                    print(f"    {half_name} Step {i:3d}: ctrl=0x{s.ctrl:02X} wai={s.wai} "
                          f"ofst=0x{s.ofst:04X} ({delay_ms:7.1f}ms) "
                          f"UNUSED — no coefficient step reads reg[{s.wai}] (RAD={s.wai%4})")


def compare_left_right_symmetry():
    """Compare L and R channel non-coeff steps to identify asymmetric CTRL usage.

    Programs 0 and 1 use the same CTRL values for L/R but Programs 2-5
    (Algorithm C) have asymmetric structures. This may reveal where the
    hardware does something different.
    """
    print(f"\n\n{'=' * 100}")
    print("LEFT/RIGHT CHANNEL SYMMETRY CHECK")
    print("Comparing non-coeff MWR=0 CTRL values between L and R channels")
    print(f"{'=' * 100}")

    for prog_idx in range(6):
        prog = MICROCODE[prog_idx]
        steps = [DecodedStep.decode(w) for w in prog]

        l_ctrls = defaultdict(int)
        r_ctrls = defaultdict(int)

        for i in range(64):
            s = steps[i]
            if not s.isNop and not s.hasCoeff and s.ctrl != 0x1F and not (s.ctrl & 0x10):
                l_ctrls[s.ctrl] += 1

        for i in range(64, 128):
            s = steps[i]
            if not s.isNop and not s.hasCoeff and s.ctrl != 0x1F and not (s.ctrl & 0x10):
                r_ctrls[s.ctrl] += 1

        all_ctrls = sorted(set(list(l_ctrls.keys()) + list(r_ctrls.keys())))
        print(f"\n  Program {prog_idx}: {PROGRAM_NAMES[prog_idx]}")
        for ctrl in all_ctrls:
            lc = l_ctrls.get(ctrl, 0)
            rc = r_ctrls.get(ctrl, 0)
            sym = "=" if lc == rc else "ASYMMETRIC"
            print(f"    CTRL=0x{ctrl:02X}: L={lc}, R={rc}  {sym}")


if __name__ == "__main__":
    analyze_all_programs()
    trace_feedback_paths()
    analyze_overwritten_registers()
    compare_left_right_symmetry()
