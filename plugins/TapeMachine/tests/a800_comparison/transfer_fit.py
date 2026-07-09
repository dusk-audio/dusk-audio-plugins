#!/usr/bin/env python3
"""transfer_fit.py — derive a memoryless waveshaper that reproduces the UAD
A800's measured nonlinearity, for the TapeMachine A800 saturation rewrite.

Method: at each level of the stepped 1 kHz tone (thd_steps, where UAD's FR is
flat so EQ doesn't confound), extract the SIGNED harmonic amplitudes H_k
(phase-aligned to the fundamental). Reconstruct the output waveform
y(theta) = sum_k H_k cos(k theta) against the input x(theta) = A cos theta,
pool the (x, y) points across all levels, and least-squares fit an odd+even
polynomial y = f(x). That f IS the static transfer curve.

Emits C++ Horner coefficients for the DSP.
"""
import os
import sys
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 48000
LEVELS_DBFS = [-30, -24, -18, -12, -6, -3]
SEG = 1.2
NHARM = 9          # harmonics to capture
ORDER = int(os.environ.get("FIT_ORDER", "9"))   # polynomial order to fit
# FIT_INPUT: stepped-tone render to fit (default = A800 matrix); FIT_FUNC: emitted
# C++ function name. Set FIT_INPUT=renders/deep_atr/harm_u.wav FIT_FUNC=classicShaper
# for the ATR-102 (Classic102) fit.
FIT_INPUT = os.environ.get("FIT_INPUT", "")
FIT_FUNC = os.environ.get("FIT_FUNC", "a800Shaper")


def signed_harmonics(seg_audio, fund=1000.0):
    w = np.hanning(len(seg_audio))
    X = np.fft.rfft(seg_audio * w)
    f = np.fft.rfftfreq(len(seg_audio), 1 / SR)
    gain = np.sum(w)
    k1 = np.argmin(np.abs(f - fund))
    phi = np.angle(X[k1])
    out = []
    for k in range(1, NHARM + 1):
        kk = np.argmin(np.abs(f - fund * k))
        # amplitude phase-aligned to the fundamental (memoryless -> +/- real)
        amp = np.real(X[kk] * np.exp(-1j * k * phi)) / gain * 2.0
        out.append(amp)
    return np.array(out)


def main():
    uad = (os.path.join(HERE, FIT_INPUT) if FIT_INPUT and not os.path.isabs(FIT_INPUT)
           else FIT_INPUT) or os.path.join(HERE, "renders", "matrix", "uad", "456_NAB_15", "thd_steps.wav")
    if not os.path.exists(uad):
        sys.exit(f"missing {uad} — render the matrix first")
    x, sr = sf.read(uad)
    if x.ndim > 1:
        x = x.mean(axis=1)

    theta = np.linspace(0, 2 * np.pi, 2048, endpoint=False)
    xs, ys = [], []
    # small-signal gain from the lowest level, to put x and y in the same scale
    g0 = None
    for i, db in enumerate(LEVELS_DBFS):
        a = int((i * SEG + 0.3) * SR)
        b = int(((i + 1) * SEG - 0.3) * SR)
        H = signed_harmonics(x[a:b])
        A = 10 ** (db / 20.0)                      # input amplitude (normalised)
        if g0 is None:
            g0 = A / (abs(H[0]) + 1e-12)           # scale so low level is unity
        y = np.sum([H[k] * np.cos((k + 1) * theta) for k in range(NHARM)], axis=0) * g0
        xin = A * np.cos(theta)
        xs.append(xin); ys.append(y)
    xs = np.concatenate(xs); ys = np.concatenate(ys)

    # fit odd+even polynomial y = sum_{n=1..ORDER} c_n x^n  (no constant term)
    Vmat = np.vstack([xs ** n for n in range(1, ORDER + 1)]).T
    coef, *_ = np.linalg.lstsq(Vmat, ys, rcond=None)

    # report fit quality + the curve shape
    yhat = Vmat @ coef
    rms = np.sqrt(np.mean((ys - yhat) ** 2)) / (np.sqrt(np.mean(ys ** 2)) + 1e-12)
    print(f"# fit residual: {rms*100:.2f}%  (order {ORDER})")
    print("# transfer curve f(x), sampled:")
    for xv in [-0.7, -0.4, -0.2, 0.0, 0.2, 0.4, 0.7]:
        fv = sum(coef[n - 1] * xv ** n for n in range(1, ORDER + 1))
        print(f"#   f({xv:+.2f}) = {fv:+.4f}")
    print(f"\n// UAD static transfer curve (Horner form), fitted from harmonics -> {FIT_FUNC}")
    print(f"static inline float {FIT_FUNC} (float x) noexcept")
    print("{")
    # Horner: c1 + x*(c2 + x*(c3 + ...)); overall f = x*c1 + x^2*c2 + ...
    terms = " + ".join(f"x{'*x'*(n-1)}*{c:.6g}f" for n, c in enumerate(coef, 1))
    print(f"    return {terms};")
    print("}")
    # also emit plain array for reference
    print("// coeffs c1..c{}: ".format(ORDER) + ", ".join(f"{c:.6g}" for c in coef))

    # tanh-knee constants beyond |x|=x0: value P0=f(x0) and slope S0=f'(x0) so the
    # knee is value+slope matched (no hand arithmetic — scientific-method rule 2).
    x0 = 0.7
    P0 = sum(coef[n - 1] * x0 ** n for n in range(1, ORDER + 1))
    S0 = sum(n * coef[n - 1] * x0 ** (n - 1) for n in range(1, ORDER + 1))
    print(f"// knee @|x|={x0}:  P0=f(x0)={P0:.6f}f  S0=f'(x0)={S0:.6f}f   "
          f"(ceiling P0+S0*0.5 = {P0 + S0 * 0.5:.4f})")


if __name__ == "__main__":
    main()
