#!/usr/bin/env python3
"""
Conductance curve fitting: extract LA-2A's actual (cellResponse, conductance)
relationship and fit multiple function forms.

Approach:
1. Direct extraction: For each LA-2A operating point, compute target conductance
   from known gain, and compute cellResponse from the sidechain model.
2. Closed-loop verification: Bisect the feedback loop to verify fitted functions
   reproduce the LA-2A compression curve.
3. Multiple function forms: single power law, two-term power law, rational function,
   polynomial, and lookup table (piecewise linear).

Usage:
    python3 fit_conductance.py
"""

import numpy as np
from scipy.optimize import minimize, least_squares

# ─── Constants from multicomp.cpp ───
PHOSPHOR_COUPLING = 0.40
SC_DRIVER_SATURATION = 0.8
SC_DRIVER_THRESHOLD = 0.03
PEAK_REDUCTION_MAX_SC_GAIN = 14.0
T4B_MAX_CONDUCTANCE = 6.0

# Precompute |sin(θ)| over one period for numerical integration
N_PHASE = 500
_abs_sin = np.abs(np.sin(np.linspace(0, 2 * np.pi, N_PHASE, endpoint=False)))

# ─── LA-2A reference gain curve (from auto_compare output) ───
# 21 tones at -40 to 0 dBFS peak, 2 dB steps
TONE_LEVELS = np.arange(-40, 1, 2, dtype=float)
TONE_AMPS = 10.0 ** (TONE_LEVELS / 20.0)

# LA-2A GR at each tone (dB, compression only, flat-region gain subtracted)
LA2A_GR = np.array([
     0.0,  0.0,  0.0,  0.0,  0.0,   # -40 to -32
     0.0,  0.0,  0.0,  0.0,  0.0,   # -30 to -22
     0.0,  0.0,  0.0, -0.2, -0.8,   # -20 to -12
    -1.8, -3.2, -4.6, -6.2, -7.7,   # -10 to  -2
    -9.3                              #    0
])

TARGET_GAIN_AT_MAX = 10.0 ** (LA2A_GR[-1] / 20.0)  # ~0.343


# ─── Steady-state sidechain model ───

def sidechain_cellresponse(a_in, gain, pr_gain):
    """Compute steady-state cellResponse given input amplitude, gain, and PR gain."""
    compressed = a_in * gain
    drive = compressed * pr_gain * _abs_sin
    sc = np.tanh(np.maximum(drive - SC_DRIVER_THRESHOLD, 0.0) * SC_DRIVER_SATURATION)
    return min(float(np.mean(sc)) * (1.0 + PHOSPHOR_COUPLING), 1.0)


def ss_gain(a_in, pr, cond_fn):
    """Solve for steady-state gain by bisecting the feedback loop."""
    pr_gain = (pr / 100.0) ** 3 * PEAK_REDUCTION_MAX_SC_GAIN

    def cond_from(cond):
        g = max(1.0 / (1.0 + cond), 0.01)
        cr = sidechain_cellresponse(a_in, g, pr_gain)
        return min(max(cond_fn(cr), 0.0), T4B_MAX_CONDUCTANCE)

    lo, hi = 0.0, T4B_MAX_CONDUCTANCE
    for _ in range(40):
        mid = (lo + hi) / 2.0
        if cond_from(mid) > mid:
            lo = mid
        else:
            hi = mid
    cond = (lo + hi) / 2.0
    return max(1.0 / (1.0 + cond), 0.01)


def cal_pr(cond_fn, target=TARGET_GAIN_AT_MAX):
    """Binary-search PR to match LA-2A GR at 0 dBFS."""
    lo, hi = 0.0, 100.0
    for _ in range(40):
        mid = (lo + hi) / 2.0
        if ss_gain(1.0, mid, cond_fn) > target:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2.0


def gain_curve(cond_fn, pr):
    """Compute GR (dB) at all 21 tone levels."""
    return np.array([
        20.0 * np.log10(max(ss_gain(a, pr, cond_fn), 1e-20))
        for a in TONE_AMPS
    ])


# ─── Direct extraction of (cellResponse, conductance) pairs ───

def extract_target_pairs(pr):
    """Extract (cellResponse, target_conductance) pairs from LA-2A data.

    For each tone level where compression occurs:
    - target gain = 10^(GR/20)
    - target conductance = 1/gain - 1
    - cellResponse = sidechain model with target gain
    """
    pr_gain = (pr / 100.0) ** 3 * PEAK_REDUCTION_MAX_SC_GAIN
    cell_responses = []
    target_conds = []

    for i, (a_in, gr_db) in enumerate(zip(TONE_AMPS, LA2A_GR)):
        if gr_db < -0.05:  # Only where compression is active
            target_gain = 10.0 ** (gr_db / 20.0)
            target_cond = 1.0 / target_gain - 1.0
            cr = sidechain_cellresponse(a_in, target_gain, pr_gain)
            cell_responses.append(cr)
            target_conds.append(target_cond)

    return np.array(cell_responses), np.array(target_conds)


# ─── Function forms ───

def power_law(x, K, gamma):
    """Single power law: K * x^gamma"""
    return np.where(x > 0, K * np.power(x, gamma), 0.0)


def two_term_power(x, a, b, c, d):
    """Two-term power law: a*x^b + c*x^d"""
    return np.where(x > 0, a * np.power(x, b) + c * np.power(x, d), 0.0)


def rational_fn(x, a, b, c, d):
    """Rational function: (a*x^2 + b*x^3) / (1 + c*x + d*x^2)"""
    x2 = x * x
    x3 = x2 * x
    num = a * x2 + b * x3
    den = 1.0 + c * x + d * x2
    return np.where(x > 0, num / den, 0.0)


def poly5(x, *coeffs):
    """5th-order polynomial through origin: c1*x + c2*x^2 + c3*x^3 + c4*x^4 + c5*x^5"""
    result = np.zeros_like(x)
    for i, c in enumerate(coeffs):
        result += c * np.power(x, i + 1)
    return np.maximum(result, 0.0)


# ─── Fitting ───

def fit_all(cell_responses, target_conds):
    """Fit multiple function forms and return sorted results."""
    results = []

    # 1. Single power law: K * x^gamma
    def res_power(p):
        K, gamma = p
        if K <= 0 or gamma < 0.5:
            return np.full_like(target_conds, 1e6)
        return power_law(cell_responses, K, gamma) - target_conds

    try:
        r = least_squares(res_power, [8.0, 6.0], bounds=([0.01, 0.5], [200, 20]),
                          method='trf', max_nfev=2000)
        K, gamma = r.x
        pred = power_law(cell_responses, K, gamma)
        rms = np.sqrt(np.mean((pred - target_conds) ** 2))
        results.append(("power_law", {"K": K, "gamma": gamma}, rms, pred))
    except Exception as e:
        print(f"  Power law fit failed: {e}")

    # 2. Two-term power law: a*x^b + c*x^d
    def res_two_term(p):
        a, b, c, d = p
        if a < 0 or c < 0 or b < 0.3 or d < 0.3:
            return np.full_like(target_conds, 1e6)
        return two_term_power(cell_responses, a, b, c, d) - target_conds

    try:
        r = least_squares(res_two_term, [4.0, 3.0, 4.0, 8.0],
                          bounds=([0, 0.3, 0, 0.3], [500, 25, 500, 25]),
                          method='trf', max_nfev=5000)
        a, b, c, d = r.x
        pred = two_term_power(cell_responses, a, b, c, d)
        rms = np.sqrt(np.mean((pred - target_conds) ** 2))
        results.append(("two_term", {"a": a, "b": b, "c": c, "d": d}, rms, pred))
    except Exception as e:
        print(f"  Two-term fit failed: {e}")

    # 3. Rational function: (a*x^2 + b*x^3) / (1 + c*x + d*x^2)
    def res_rational(p):
        a, b, c, d = p
        return rational_fn(cell_responses, a, b, c, d) - target_conds

    try:
        r = least_squares(res_rational, [5.0, 10.0, 1.0, 1.0],
                          bounds=([-100, -100, -10, -10], [100, 100, 100, 100]),
                          method='trf', max_nfev=5000)
        a, b, c, d = r.x
        pred = rational_fn(cell_responses, a, b, c, d)
        rms = np.sqrt(np.mean((pred - target_conds) ** 2))
        results.append(("rational", {"a": a, "b": b, "c": c, "d": d}, rms, pred))
    except Exception as e:
        print(f"  Rational fit failed: {e}")

    # 4. 5th-order polynomial through origin
    def res_poly5(p):
        return poly5(cell_responses, *p) - target_conds

    try:
        r = least_squares(res_poly5, [0.0, 0.0, 5.0, 10.0, 5.0],
                          method='trf', max_nfev=5000)
        coeffs = r.x
        pred = poly5(cell_responses, *coeffs)
        rms = np.sqrt(np.mean((pred - target_conds) ** 2))
        results.append(("poly5", {f"c{i+1}": c for i, c in enumerate(coeffs)}, rms, pred))
    except Exception as e:
        print(f"  Poly5 fit failed: {e}")

    results.sort(key=lambda r: r[2])
    return results


def main():
    print("=" * 70)
    print("  Conductance Curve Fitting — Direct Extraction + Closed-Loop Verify")
    print("=" * 70)

    # ─── Step 1: Verify baseline ───
    base_fn = lambda x: 8.0 * x ** 6.0 if x > 0 else 0.0
    pr_b = cal_pr(base_fn)
    gc_b = gain_curve(base_fn, pr_b)
    err_b = np.sqrt(np.mean((gc_b - LA2A_GR) ** 2))
    max_b = np.max(np.abs(gc_b - LA2A_GR))
    print(f"\n  Baseline (K=8, γ=6): PR={pr_b:.1f}")
    print(f"  Gain curve RMS error: {err_b:.3f} dB, max error: {max_b:.3f} dB")

    print(f"\n  {'Level':>6}  {'LA-2A':>7}  {'Base':>7}  {'Diff':>7}")
    print("  " + "-" * 33)
    for i, lv in enumerate(TONE_LEVELS):
        if abs(LA2A_GR[i]) > 0.05 or abs(gc_b[i]) > 0.05:
            d = gc_b[i] - LA2A_GR[i]
            print(f"  {lv:6.0f}  {LA2A_GR[i]:+7.1f}  {gc_b[i]:+7.1f}  {d:+7.2f}")

    # ─── Step 2: Extract target (cellResponse, conductance) pairs ───
    print(f"\n  Extracting target pairs using PR={pr_b:.1f} ...")
    cr_targets, cond_targets = extract_target_pairs(pr_b)

    print(f"\n  {'cellResp':>10}  {'target_cond':>12}  {'base_cond':>12}  {'diff':>8}")
    print("  " + "-" * 48)
    for cr, ct in zip(cr_targets, cond_targets):
        bc = 8.0 * cr ** 6.0 if cr > 0 else 0.0
        print(f"  {cr:10.4f}  {ct:12.4f}  {bc:12.4f}  {bc - ct:+8.4f}")

    # ─── Step 3: Fit multiple function forms ───
    print(f"\n  Fitting {len(cr_targets)} data points ...")
    results = fit_all(cr_targets, cond_targets)

    print(f"\n  {'Rank':>4}  {'Form':>15}  {'RMS(cond)':>10}  Parameters")
    print("  " + "-" * 70)
    for i, (name, params, rms, _) in enumerate(results):
        pstr = ", ".join(f"{k}={v:.4f}" for k, v in params.items())
        print(f"  {i+1:4d}  {name:>15}  {rms:10.6f}  {pstr}")

    # ─── Step 4: Closed-loop verification of top candidates ───
    print("\n" + "=" * 70)
    print("  CLOSED-LOOP VERIFICATION")
    print("=" * 70)

    for name, params, rms_cond, _ in results[:4]:
        # Build callable from params
        if name == "power_law":
            K, gamma = params["K"], params["gamma"]
            fn = lambda x, K=K, g=gamma: K * x ** g if x > 0 else 0.0
            cpp_desc = f"K={K:.6f} * x^{gamma:.6f}"
        elif name == "two_term":
            a, b, c, d = params["a"], params["b"], params["c"], params["d"]
            fn = lambda x, a=a, b=b, c=c, d=d: a * x ** b + c * x ** d if x > 0 else 0.0
            cpp_desc = f"{a:.6f}*x^{b:.6f} + {c:.6f}*x^{d:.6f}"
        elif name == "rational":
            a, b, c, d = params["a"], params["b"], params["c"], params["d"]
            fn = lambda x, a=a, b=b, c=c, d=d: (a * x**2 + b * x**3) / (1.0 + c * x + d * x**2) if x > 0 else 0.0
            cpp_desc = f"({a:.4f}*x²+{b:.4f}*x³)/(1+{c:.4f}*x+{d:.4f}*x²)"
        elif name == "poly5":
            cs = [params[f"c{i+1}"] for i in range(5)]
            fn = lambda x, cs=cs: max(sum(c * x**(i+1) for i, c in enumerate(cs)), 0.0) if x > 0 else 0.0
            cpp_desc = " + ".join(f"{c:.4f}*x^{i+1}" for i, c in enumerate(cs))
        else:
            continue

        pr = cal_pr(fn)
        gc = gain_curve(fn, pr)
        gc_err = np.sqrt(np.mean((gc - LA2A_GR) ** 2))
        gc_max = np.max(np.abs(gc - LA2A_GR))

        print(f"\n  {name}: {cpp_desc}")
        print(f"  PR={pr:.1f}, RMS={gc_err:.4f} dB, max={gc_max:.4f} dB")
        print(f"  (Baseline: RMS={err_b:.4f}, max={max_b:.4f})")

        print(f"\n  {'Level':>6}  {'LA-2A':>7}  {'Fitted':>7}  {'Diff':>7}  {'Base_D':>7}")
        print("  " + "-" * 41)
        for i, lv in enumerate(TONE_LEVELS):
            if abs(LA2A_GR[i]) > 0.05 or abs(gc[i]) > 0.05:
                d = gc[i] - LA2A_GR[i]
                bd = gc_b[i] - LA2A_GR[i]
                print(f"  {lv:6.0f}  {LA2A_GR[i]:+7.1f}  {gc[i]:+7.1f}  {d:+7.2f}  {bd:+7.2f}")

    # ─── Step 5: Output C++ code for best candidate ───
    best_name, best_params, _, _ = results[0]

    print("\n" + "=" * 70)
    print(f"  C++ CODE — Best: {best_name}")
    print("=" * 70)

    if best_name == "power_law":
        K, gamma = best_params["K"], best_params["gamma"]
        print(f"\n  // Constants")
        print(f"  constexpr float T4B_CONDUCTANCE_K = {K:.6f}f;")
        print(f"  constexpr float T4B_GAMMA = {gamma:.6f}f;")
        print(f"\n  // Conductance (same form as baseline)")
        print(f"  float conductance = T4B_CONDUCTANCE_K * std::pow(cellResponse, T4B_GAMMA);")

    elif best_name == "two_term":
        a, b, c, d = best_params["a"], best_params["b"], best_params["c"], best_params["d"]
        print(f"\n  // Constants")
        print(f"  constexpr float T4B_COND_A = {a:.6f}f;")
        print(f"  constexpr float T4B_COND_B = {b:.6f}f;")
        print(f"  constexpr float T4B_COND_C = {c:.6f}f;")
        print(f"  constexpr float T4B_COND_D = {d:.6f}f;")
        print(f"\n  // Conductance")
        print(f"  float conductance = T4B_COND_A * std::pow(cellResponse, T4B_COND_B)")
        print(f"                    + T4B_COND_C * std::pow(cellResponse, T4B_COND_D);")

    elif best_name == "rational":
        a, b, c, d = best_params["a"], best_params["b"], best_params["c"], best_params["d"]
        print(f"\n  // Constants")
        print(f"  constexpr float T4B_COND_A = {a:.6f}f;")
        print(f"  constexpr float T4B_COND_B = {b:.6f}f;")
        print(f"  constexpr float T4B_COND_C = {c:.6f}f;")
        print(f"  constexpr float T4B_COND_D = {d:.6f}f;")
        print(f"\n  // Conductance (rational function)")
        print(f"  float x2 = cellResponse * cellResponse;")
        print(f"  float x3 = x2 * cellResponse;")
        print(f"  float conductance = (T4B_COND_A * x2 + T4B_COND_B * x3)")
        print(f"                    / (1.0f + T4B_COND_C * cellResponse + T4B_COND_D * x2);")

    elif best_name == "poly5":
        cs = [best_params[f"c{i+1}"] for i in range(5)]
        print(f"\n  // Constants")
        for i, c in enumerate(cs):
            print(f"  constexpr float T4B_COND_C{i+1} = {c:.6f}f;")
        print(f"\n  // Conductance (5th-order polynomial)")
        print(f"  float x = cellResponse;")
        print(f"  float conductance = x * (T4B_COND_C1 + x * (T4B_COND_C2 + x * (T4B_COND_C3")
        print(f"                    + x * (T4B_COND_C4 + x * T4B_COND_C5))));")
        print(f"  conductance = std::max(conductance, 0.0f);")


if __name__ == "__main__":
    main()
