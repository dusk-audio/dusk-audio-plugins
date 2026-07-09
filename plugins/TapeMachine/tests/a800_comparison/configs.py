#!/usr/bin/env python3
"""configs.py — the matched TapeMachine-vs-UAD-A800 test matrix and the exact
per-plugin parameter maps. Single source of truth shared by render_matrix.py
and score_matrix.py so the two never drift.

Matrix = 3 tapes x 2 EQ standards x 3 speeds = 18 configs. Operating point:
Studer A800 / Repro / +6 dB cal / unity in-out, 4x oversampling on TapeMachine.
Noise off for the tone measurements (a separate off/on pass covers noise).

TapeMachine (DPF) param values: choice params take the INTEGER INDEX; float
params interpret the value as normalised 0-1, so leave gains at unity defaults.
UAD params take DISPLAY LABELS.
"""

# (key, mine Tape Type index, UAD "Tape Type" label). 911 has no UAD analogue.
TAPES = [
    ("456", 0, "456"),
    ("GP9", 1, "900"),   # Quantegy GP9 == UAD "900"
    ("250", 3, "250"),
]

# (key, mine EQ Standard index, UAD "Emphasis EQ" label)
EQS = [
    ("NAB", 0, "NAB"),
    ("CCIR", 1, "CCIR"),
]

# (ips, mine Tape Speed index, UAD "IPS" label)
SPEEDS = [
    (7.5, 0, "7.5 IPS"),
    (15, 1, "15 IPS"),
    (30, 2, "30 IPS"),
]

STIMULI = ["sweep", "thd_steps", "wf_3150"]

# Matched machine config held constant across the matrix.
MINE_A800 = "0"     # Tape Machine = Swiss800 (A800)
MINE_PATH = "0"     # Signal Path = Repro
MINE_CAL = "2"      # Calibration = +6 dB
MINE_OS = "2"       # Oversampling = 4x


def configs():
    """Yield dicts describing every matrix cell + both plugins' param lists."""
    for tk, mtape, utape in TAPES:
        for ek, meq, ueq in EQS:
            for ips, mspd, uspd in SPEEDS:
                key = f"{tk}_{ek}_{int(ips) if ips == int(ips) else ips}"
                yield {
                    "key": key, "tape": tk, "eq": ek, "ips": ips,
                    "mine": [
                        ("Tape Machine", MINE_A800), ("Tape Speed", str(mspd)),
                        ("Tape Type", str(mtape)), ("EQ Standard", str(meq)),
                        ("Signal Path", MINE_PATH), ("Calibration", MINE_CAL),
                        ("Noise Amount", "0"), ("Oversampling", MINE_OS),
                    ],
                    "uad": [
                        ("IPS", uspd), ("Tape Type", utape), ("Emphasis EQ", ueq),
                    ],
                }


if __name__ == "__main__":
    n = 0
    for c in configs():
        n += 1
        print(f"{c['key']:16s} mine={c['mine'][:4]} uad={c['uad']}")
    print(f"{n} configs")
