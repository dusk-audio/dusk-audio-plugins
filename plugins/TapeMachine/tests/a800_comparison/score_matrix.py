#!/usr/bin/env python3
"""score_matrix.py — per-config error scoreboard for the 18-config matrix.

For every config it prints mine-vs-UAD deltas (FR RMS error, head bump, HF -3dB,
THD@-6dBFS, wow&flutter) and an aggregate FR score, so a tuning iteration is a
handful of numbers to watch. Reads renders/matrix/{tapemachine,uad}/{key}/*.

  python3 score_matrix.py                 # all configs
  python3 score_matrix.py 456_NAB_15      # one config
  python3 score_matrix.py --fr            # FR-only compact table
"""
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from compare_a800 import freq_response, fr_features, thd_curve  # noqa: E402
from wow_flutter import measure_wow_flutter  # noqa: E402
from configs import configs  # noqa: E402

STIM = os.path.join(HERE, "stimuli")
MAT = os.path.join(HERE, "renders", "matrix")


def _fr(plug, key):
    return freq_response(os.path.join(STIM, "sweep.wav"),
                         os.path.join(MAT, plug, key, "sweep.wav"))


def fr_error(key):
    fm, mm = _fr("tapemachine", key)
    fu, mu = _fr("uad", key)
    band = (fm >= 25) & (fm <= 18000)
    diff = mm[band] - mu[band]
    return float(np.sqrt(np.mean(diff ** 2))), fm[band], diff


def have(key):
    return all(os.path.exists(os.path.join(MAT, p, key, "sweep.wav"))
               for p in ("tapemachine", "uad"))


def main():
    fr_only = "--fr" in sys.argv
    keys = [a for a in sys.argv[1:] if not a.startswith("--")]
    rows = []
    for c in configs():
        k = c["key"]
        if keys and k not in keys:
            continue
        if not have(k):
            continue
        err, f, diff = fr_error(k)
        fm = fr_features(*_fr("tapemachine", k))
        fu = fr_features(*_fr("uad", k))
        row = {"key": k, "fr_err": err,
               "bump_m": fm["head_bump_db"], "bump_hz_m": fm["head_bump_hz"],
               "bump_u": fu["head_bump_db"], "bump_hz_u": fu["head_bump_hz"],
               "hf_m": fm["hf_minus3db_hz"], "hf_u": fu["hf_minus3db_hz"]}
        if not fr_only:
            tm = dict(thd_curve(os.path.join(MAT, "tapemachine", k, "thd_steps.wav")))
            tu = dict(thd_curve(os.path.join(MAT, "uad", k, "thd_steps.wav")))
            row["thd6_m"], row["thd6_u"] = tm.get(-6, float("nan")), tu.get(-6, float("nan"))
            wm = measure_wow_flutter(os.path.join(MAT, "tapemachine", k, "wf_3150.wav"))
            wu = measure_wow_flutter(os.path.join(MAT, "uad", k, "wf_3150.wav"))
            row["wf_m"], row["wf_u"] = wm["pitch_dev_robust_pct"], wu["pitch_dev_robust_pct"]
        rows.append(row)

    if not rows:
        sys.exit("No matrix renders found. Run render_matrix.py first.")

    hdr = f"{'config':16s} {'FRerr':>6} {'bump mine/targ':>18} {'HF-3dB mine/targ':>18}"
    if not fr_only:
        hdr += f" {'THD@-6':>13} {'W&F':>13}"
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        line = (f"{r['key']:16s} {r['fr_err']:6.2f} "
                f"{r['bump_m']:+4.1f}@{r['bump_hz_m']:>4.0f}/{r['bump_u']:+4.1f}@{r['bump_hz_u']:>4.0f} "
                f"{r['hf_m']:>7.0f}/{r['hf_u']:>7.0f}")
        if not fr_only:
            line += f"  {r['thd6_m']:5.2f}/{r['thd6_u']:5.2f}  {r['wf_m']:5.2f}/{r['wf_u']:5.2f}"
        print(line)

    errs = np.array([r["fr_err"] for r in rows])
    print("-" * len(hdr))
    print(f"FR score: mean {errs.mean():.3f}  worst {errs.max():.3f} ({rows[int(errs.argmax())]['key']})")
    # systematic grouping
    for dim, idx in [("tape", 0), ("eq", 1), ("speed", 2)]:
        groups = {}
        for r in rows:
            g = r["key"].split("_")[idx]
            groups.setdefault(g, []).append(r["fr_err"])
        summ = "  ".join(f"{g}:{np.mean(v):.2f}" for g, v in sorted(groups.items()))
        print(f"  by {dim:5s}: {summ}")


if __name__ == "__main__":
    main()
