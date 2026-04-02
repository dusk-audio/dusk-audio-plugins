#!/usr/bin/env python3
"""
Tone Stack Frequency Response Validator.

Reimplements the ToneStack.cpp coefficient computation in Python and plots
frequency responses at various knob positions. Compare against:
- Duncan Amps Tone Stack Calculator
- Yeh's DAFx-06 paper figures
- https://tonestack.yuriturov.com/

Usage:
    python3 tone_stack_validate.py           # Plot all 3 models
    python3 tone_stack_validate.py --model round   # Plot one model
"""

import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')  # non-interactive backend
import matplotlib.pyplot as plt
from scipy.signal import freqz

SAMPLE_RATE = 48000.0

# Component values from ToneStack.cpp
MODELS = {
    "Round (Fender Deluxe Reverb AB763)": {
        "R1": 100000.0, "R2": 1e6, "R3": 6800.0, "R4": 250000.0,
        "C1": 250e-12, "C2": 0.1e-6, "C3": 0.1e-6,
        "has_mid": False,
    },
    "Chime (Vox AC30 Top Boost)": {
        "R1": 100000.0, "R2": 1e6, "R3": 250000.0, "R4": 1e6,
        "C1": 56e-12, "C2": 0.022e-6, "C3": 0.1e-6,
        "has_mid": False,
    },
    "Punch (Marshall JCM800)": {
        "R1": 33000.0, "R2": 1e6, "R3": 25000.0, "R4": 250000.0,
        "C1": 470e-12, "C2": 0.022e-6, "C3": 0.022e-6,
        "has_mid": True,
    },
}

def log_taper(linear01):
    """Simulate logarithmic pot taper."""
    return np.log10(1.0 + 9.0 * np.clip(linear01, 0.001, 1.0))


def compute_tmb_coefficients(comp, bass, treble, mid, sr=SAMPLE_RATE):
    """Compute 3rd-order IIR filter coefficients from Yeh's nodal analysis."""
    R1 = comp["R1"]
    R2 = comp["R2"]
    R3 = comp["R3"]
    R4 = comp["R4"]
    C1 = comp["C1"]
    C2 = comp["C2"]
    C3 = comp["C3"]

    b = log_taper(bass)
    t = treble
    m = mid if comp["has_mid"] else 0.5

    b = np.clip(b, 0.01, 0.99)
    t = np.clip(t, 0.01, 0.99)
    m = np.clip(m, 0.01, 0.99)

    # Yeh's symbolic coefficients (numerator)
    b1v = (t * C1 * R1
           + m * C3 * R3
           + b * (C1 * R2 + C2 * R2)
           + (C1 * R3 + C2 * R3))

    b2v = (t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
           - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + b * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
           + (C1 * C2 * R1 * R3 + C1 * C3 * R1 * R3 + C2 * C3 * R3 * R4))

    b3v = (b * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
           + m * (C1 * C2 * C3 * R1 * R3 * R4 + C1 * C2 * C3 * R3 * R3 * R4)
           + b * (C1 * C2 * C3 * R1 * R2 * R4))

    # Denominator
    a1v = ((C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4)
           + m * C3 * R3
           + b * (C1 * R2 + C2 * R2))

    a2v = (m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4
               + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + b * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R1 * R4
             + C2 * C3 * R3 * R4 + C1 * C2 * R1 * R4 + C1 * C3 * R3 * R4)
           + b * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4
                 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4))

    a3v = (b * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
           + m * (C1 * C2 * C3 * R1 * R3 * R4 + C1 * C2 * C3 * R3 * R3 * R4)
           + b * (C1 * C2 * C3 * R1 * R2 * R4)
           + (C1 * C2 * C3 * R1 * R3 * R4))

    # s-domain: H(s) = (b1v*s + b2v*s^2 + b3v*s^3) / (1 + a1v*s + a2v*s^2 + a3v*s^3)
    b0s, b1s, b2s, b3s = 0.0, b1v, b2v, b3v
    a0s, a1s, a2s, a3s = 1.0, a1v, a2v, a3v

    # Bilinear transform
    c = 2.0 * sr
    c2 = c * c
    c3 = c2 * c

    A0 = a0s + a1s * c + a2s * c2 + a3s * c3
    A1 = 3.0 * a0s + a1s * c - a2s * c2 - 3.0 * a3s * c3
    A2 = 3.0 * a0s - a1s * c - a2s * c2 + 3.0 * a3s * c3
    A3 = a0s - a1s * c + a2s * c2 - a3s * c3

    B0 = b0s + b1s * c + b2s * c2 + b3s * c3
    B1 = 3.0 * b0s + b1s * c - b2s * c2 - 3.0 * b3s * c3
    B2 = 3.0 * b0s - b1s * c - b2s * c2 + 3.0 * b3s * c3
    B3 = b0s - b1s * c + b2s * c2 - b3s * c3

    inv = 1.0 / A0
    b_coeffs = np.array([B0, B1, B2, B3]) * inv
    a_coeffs = np.array([1.0, A1 * inv, A2 * inv, A3 * inv])

    return b_coeffs, a_coeffs


def plot_model(model_name, comp, ax):
    """Plot frequency response for one amp model at various knob settings."""
    settings = [
        ("All noon (5/5/5)", 0.5, 0.5, 0.5, "C0"),
        ("Bass max (10/5/5)", 1.0, 0.5, 0.5, "C1"),
        ("Mid scoop (5/0/5)", 0.5, 0.0, 0.5, "C2"),
        ("Mid boost (5/10/5)", 0.5, 1.0, 0.5, "C3"),
        ("Treble max (5/5/10)", 0.5, 0.5, 1.0, "C4"),
        ("Everything max (10/10/10)", 1.0, 1.0, 1.0, "C5"),
        ("Everything min (0/0/0)", 0.0, 0.0, 0.0, "C6"),
    ]

    freqs = np.logspace(np.log10(20), np.log10(20000), 500)

    for label, bass, mid, treble, color in settings:
        b, a = compute_tmb_coefficients(comp, bass, treble, mid)
        w, h = freqz(b, a, worN=freqs, fs=SAMPLE_RATE)
        mag_db = 20 * np.log10(np.maximum(np.abs(h), 1e-10))
        ax.semilogx(freqs, mag_db, label=label, color=color, linewidth=1.2)

    ax.set_title(model_name, fontsize=12, fontweight='bold')
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Magnitude (dB)")
    ax.set_xlim(20, 20000)
    ax.set_ylim(-40, 5)
    ax.grid(True, alpha=0.3, which='both')
    ax.legend(fontsize=7, loc='lower right')


def main():
    parser = argparse.ArgumentParser(description="Tone stack frequency response validation")
    parser.add_argument("--model", choices=["round", "chime", "punch"],
                        help="Plot only one model")
    args = parser.parse_args()

    if args.model:
        model_map = {"round": 0, "chime": 1, "punch": 2}
        idx = model_map[args.model]
        names = list(MODELS.keys())
        fig, ax = plt.subplots(1, 1, figsize=(10, 6))
        plot_model(names[idx], MODELS[names[idx]], ax)
    else:
        fig, axes = plt.subplots(1, 3, figsize=(18, 6))
        for i, (name, comp) in enumerate(MODELS.items()):
            plot_model(name, comp, axes[i])

    fig.suptitle("DuskAmp Tone Stack Validation — Circuit-Modeled (Yeh Method)",
                 fontsize=14, fontweight='bold')
    plt.tight_layout()

    out_path = "tone_stack_response.png"
    plt.savefig(out_path, dpi=150)
    print(f"Saved: {out_path}")

    # Print some key values for quick validation
    print("\n--- Quick validation (all knobs at noon) ---")
    for name, comp in MODELS.items():
        b, a = compute_tmb_coefficients(comp, 0.5, 0.5, 0.5)
        w, h = freqz(b, a, worN=[100, 500, 1000, 3000, 10000], fs=SAMPLE_RATE)
        mags = 20 * np.log10(np.maximum(np.abs(h), 1e-10))
        print(f"\n{name}:")
        for freq, mag in zip([100, 500, 1000, 3000, 10000], mags):
            print(f"  {freq:>5} Hz: {mag:+.1f} dB")


if __name__ == "__main__":
    main()
