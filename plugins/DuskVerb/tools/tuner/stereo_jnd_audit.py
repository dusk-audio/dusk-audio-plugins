#!/usr/bin/env python3
"""Anchor-relative stereo transfer audit for DuskVerb factory presets.

The ordinary fleet audit evaluates renders made from centred stimuli.  That is
not sufficient for issue #123: a mono-input reverb followed by a static output
pan can reproduce broadband ILD while still discarding the input's stereo
information.  This tool renders matched hard-left, hard-right, and centred
bursts through DuskVerb and the documented reference state, then compares the
input-dependent stereo transfer at perceptual-scale tolerances.

Reference files and commercial plugins live in the private dusk-audio-tools
checkout / the user's local plugin installation.  Override those locations
with --audio-tools, --vvv-vst3, --shimmer-vst3, and --dv-vst3 as needed.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np
import soundfile as sf


SCRIPT = Path(__file__).resolve()
PLUGIN_REPO = SCRIPT.parents[4]
DEFAULT_AUDIO_TOOLS = Path.home() / "projects" / "dusk-audio-tools"
DEFAULT_RENDERER = PLUGIN_REPO / "build" / "tests" / "duskverb_render" / "duskverb_render"
DEFAULT_DV = PLUGIN_REPO / "build" / "bin" / "VST3" / "DuskVerb.vst3"
DEFAULT_VVV = Path.home() / ".vst3" / "yabridge" / "ValhallaVintageVerb.vst3"
DEFAULT_SHIMMER = Path.home() / ".vst3" / "yabridge" / "ValhallaShimmer.vst3"

SR = 48_000
TAIL_START = 2_000                 # issue #123's established 41.7 ms tail boundary
TAIL_END_SECONDS = 4.0
EPS = 1.0e-30

# Conservative gates.  One dB is the measured order of magnitude for binaural
# level discrimination with a contralateral signal; correlation gets the
# stricter 0.04 gate near a fully correlated reference and 0.10 elsewhere.
ILD_JND_DB = 1.0
PAN_SWAP_JND_DB = 1.0
# A moving, diffuse field has temporal integration/masking beyond the static
# interaural level JND; use 1.5 dB RMS across 100 ms frames while retaining the
# stricter 1.0 dB gate for broadband and each frequency band.
IMAGE_TRAJECTORY_JND_DB = 1.5
SIDE_MID_JND_DB = 1.0
TRAJECTORY_ILD_LIMIT_DB = 20.0   # quieter side is perceptually masked beyond this


@dataclass(frozen=True)
class Reference:
    kind: str
    plugin: str = ""
    state: str = ""
    vvv_key: str = ""


@dataclass
class Metrics:
    ild_db: float
    ild_low_db: float
    ild_mid_db: float
    ild_high_db: float
    pan_swap_db: float
    mirror_residual_db: float
    center_corr: float
    center_side_mid_db: float
    trajectory_rmse_db: float = math.nan


PRESETS: dict[str, Reference] = {
    "Vocal Plate": Reference("vvv", vvv_key="vvv-vocal-plate"),
    "Drum Plate": Reference("vvv", vvv_key="vvv-drum-plate"),
    "Bright Hall": Reference("vvv", vvv_key="vvv-bright-hall"),
    "Vocal Hall": Reference("vvv", vvv_key="vvv-vocal-hall"),
    "Cathedral Large Hall": Reference("vvv", vvv_key="vvv-cathedral"),
    "Blade Runner 224": Reference("vvv", vvv_key="vvv-blade-runner"),
    "79 Vocal Chamber": Reference("vvv", vvv_key="vvv-79vc"),
    "Small Drum Room": Reference("vvv", vvv_key="vvv-84-small-room"),
    "Medium Drum Room": Reference("vvv", vvv_key="vvv-fat-snare-room"),
    "Tiled Room": Reference("vvv", vvv_key="vvv-tiled-room"),
    "Ambience": Reference("vvv", vvv_key="vvv-ambience"),
    "Vintage Vocal Plate": Reference(
        "vst2", "LexVintagePlate.so", "anchors/lex/fxp/LexVintagePlate/Lpl0/lex-vintage-vocal-plate.fxp"),
    "Vintage Gold Plate": Reference(
        "vst2", "LexVintagePlate.so", "anchors/lex/fxp/LexVintagePlate/Lpl0/lex-vintage-gold-plate.fxp"),
    "Large Chamber": Reference(
        "vst2", "LexChamber.so", "anchors/lex/fxp/LexChamber/Lcm1/lex-chamber-large.fxp"),
    "Live Room": Reference(
        "vst2", "LexRoom.so", "anchors/lex/fxp/LexRoom/Lrm1/medium-live-room-1.fxp"),
    "Reverse Taps": Reference(
        "vst2", "LexRoom.so", "anchors/lex/fxp/LexRoom/Lrm1/lex-reverse-1.fxp"),
    "Black Hole": Reference("shimmer", state="BlackHole"),
    "Deep Blue Day": Reference("shimmer", state="DeepBlueDay"),
}


def _power_norm(value: float, lo: float, hi: float, exponent: float) -> float:
    if value <= lo:
        return 0.0
    if value >= hi:
        return 1.0
    return ((value - lo) / (hi - lo)) ** (1.0 / exponent)


def _vvv_normalised(entry: dict, modes: dict, colours: dict) -> dict[str, float]:
    """Invert ValhallaVintageVerb's display mappings.

    The exponents are derived from the checked-in vvv_calib.json points.  Using
    the analytic curves avoids the 1-2% error introduced by linear interpolation
    between calibration samples (important when the GUI readback is the source
    of truth for a reference preset).
    """
    pre_exp = math.log(20.0 / 500.0) / math.log(0.25)
    decay_exp = math.log((7.0 - 0.2) / (70.0 - 0.2)) / math.log(0.5)
    freq_exp = math.log((6000.0 - 100.0) / (20000.0 - 100.0)) / math.log(0.5)
    bass_x_exp = math.log((700.0 - 100.0) / (10000.0 - 100.0)) / math.log(0.5)
    bass_mult_exp = math.log((1.0 - 0.25) / (4.0 - 0.25)) / math.log(0.5)
    return {
        "Mix": entry["Mix_pct"] / 100.0,
        "PreDelay": _power_norm(entry["PreDelay_ms"], 0.0, 500.0, pre_exp),
        "Decay": _power_norm(entry["Decay_s"], 0.2, 70.0, decay_exp),
        "Size": entry["Size_pct"] / 100.0,
        "Attack": entry["Attack_pct"] / 100.0,
        "BassMult": _power_norm(entry["BassMult_X"], 0.25, 4.0, bass_mult_exp),
        "BassXover": _power_norm(entry["BassFreq_Hz"], 100.0, 10000.0, bass_x_exp),
        "HighShelf": (entry["HighShelf_dB"] + 24.0) / 24.0,
        "HighFreq": _power_norm(entry["HighFreq_Hz"], 100.0, 20000.0, freq_exp),
        "EarlyDiffusion": entry["Early_pct"] / 100.0,
        "LateDiffusion": entry["Late_pct"] / 100.0,
        "ModRate": (entry["Rate_Hz"] - 0.1) / 9.9,
        "ModDepth": entry["Depth_pct"] / 100.0,
        "HighCut": _power_norm(entry["HighCut_Hz"], 100.0, 20000.0, freq_exp),
        "LowCut": (entry["LowCut_Hz"] - 10.0) / 1490.0,
        "ColorMode": colours[entry["COLOR"]],
        "ReverbMode": modes[entry["MODE"]],
    }


def _make_inputs(root: Path) -> dict[str, Path]:
    root.mkdir(parents=True, exist_ok=True)
    n = int(2.5 * SR)
    rng = np.random.default_rng(42)
    burst = rng.uniform(-1.0, 1.0, int(0.005 * SR)).astype(np.float32) * 0.7
    out: dict[str, Path] = {}
    for name, channel in (("left", 0), ("right", 1), ("center", -1)):
        x = np.zeros((n, 2), dtype=np.float32)
        if channel < 0:
            x[1000:1000 + len(burst), :] = burst[:, None]
        else:
            x[1000:1000 + len(burst), channel] = burst
        path = root / f"{name}.wav"
        sf.write(path, x, SR, subtype="FLOAT")
        out[name] = path
    return out


def _run(cmd: list[str], timeout: int = 300) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout[-2000:]}\nstderr:\n{proc.stderr[-2000:]}")
    return proc.stdout


def _capture_set(renderer: Path, plugin_flag: str, plugin: Path, out_dir: Path,
                 slug: str, inputs: dict[str, Path], config: list[str]) -> dict[str, Path]:
    shutil.rmtree(out_dir, ignore_errors=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(renderer), plugin_flag, str(plugin), "--output-dir", str(out_dir),
           "--slug", slug, "--prerun-seconds", "5.0"]
    for input_wav in inputs.values():
        cmd.extend(["--input-wav", str(input_wav)])
    cmd.extend(config)
    cmd.append("RefRender")
    _run(cmd)
    stems = {side: out_dir / f"{slug}_{input_wav.stem}_stem.wav"
             for side, input_wav in inputs.items()}
    missing = [str(path) for path in stems.values() if not path.exists()]
    if missing:
        raise RuntimeError("renderer did not create " + ", ".join(missing))
    return stems


def _capture_dv(renderer: Path, dv: Path, preset: str, inputs: dict[str, Path], root: Path) -> dict[str, Path]:
    return _capture_set(
        renderer, "--vst3", dv, root, "dv", inputs,
        ["--program", preset, "--param", "Dry/Wet=1.0", "--param", "Bus Mode=1"])


def _reference_first_config(ref: Reference, audio_tools: Path, vvv: Path, shimmer: Path,
                            vvv_data: dict) -> tuple[str, Path, list[str]]:
    if ref.kind == "vvv":
        entry = vvv_data["presets"][ref.vvv_key]
        params = _vvv_normalised(entry, vvv_data["reverbmode_norm"], vvv_data["colormode_norm"])
        args: list[str] = ["--per-param-delay-ms", "30"]
        for name, value in params.items():
            args.extend(["--nparam", f"{name}={value:.9g}"])
        return "--vst3", vvv, args
    if ref.kind == "vst2":
        return "--vst2", Path.home() / ".vst" / "yabridge" / ref.plugin, [
            "--load-state", str(audio_tools / ref.state), "--param", "Mix=1.0",
            "--wait-after-load", "1000"]
    if ref.kind == "shimmer":
        # ValhallaShimmer exposes its eight canonical factory programs through
        # the host API.  Use those directly: the old saved state files both
        # replayed the same default and silently invalidated the comparison.
        return "--vst3", shimmer, ["--program", ref.state, "--nparam", "wetDry=1.0"]
    raise ValueError(f"unknown reference kind {ref.kind}")


def _capture_reference(renderer: Path, ref: Reference, audio_tools: Path, vvv: Path,
                       shimmer: Path, vvv_data: dict, inputs: dict[str, Path], root: Path) -> dict[str, Path]:
    flag, plugin, config = _reference_first_config(ref, audio_tools, vvv, shimmer, vvv_data)
    return _capture_set(renderer, flag, plugin, root, "ref", inputs, config)


def _load(path: Path) -> tuple[np.ndarray, int]:
    x, sr = sf.read(path, always_2d=True, dtype="float64")
    if x.shape[1] != 2:
        raise ValueError(f"expected stereo file: {path}")
    return x, sr


def _tail(x: np.ndarray, sr: int) -> np.ndarray:
    return x[TAIL_START:min(len(x), int(TAIL_END_SECONDS * sr))]


def _ild(x: np.ndarray) -> float:
    return 10.0 * math.log10((float(np.sum(x[:, 0] ** 2)) + EPS) /
                             (float(np.sum(x[:, 1] ** 2)) + EPS))


def _band_ild(x: np.ndarray, sr: int, low: float, high: float) -> float:
    if len(x) < 1024:
        return math.nan
    window = np.hanning(len(x))[:, None]
    spec = np.fft.rfft(x * window, axis=0)
    freq = np.fft.rfftfreq(len(x), 1.0 / sr)
    keep = (freq >= low) & (freq < high)
    energy = np.sum(np.abs(spec[keep]) ** 2, axis=0)
    if float(np.max(energy)) < EPS:
        return math.nan
    return 10.0 * math.log10((float(energy[0]) + EPS) / (float(energy[1]) + EPS))


def _normalised_difference_db(a: np.ndarray, b: np.ndarray) -> float:
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    numerator = float(np.sum((a - b) ** 2))
    denominator = 0.5 * float(np.sum(a ** 2) + np.sum(b ** 2)) + EPS
    return 10.0 * math.log10((numerator + EPS) / denominator)


def _corr(x: np.ndarray) -> float:
    l = x[:, 0] - float(np.mean(x[:, 0]))
    r = x[:, 1] - float(np.mean(x[:, 1]))
    den = math.sqrt(float(np.sum(l * l) * np.sum(r * r))) + EPS
    return float(np.sum(l * r)) / den


def _side_mid(x: np.ndarray) -> float:
    mid = (x[:, 0] + x[:, 1]) / math.sqrt(2.0)
    side = (x[:, 0] - x[:, 1]) / math.sqrt(2.0)
    return 10.0 * math.log10((float(np.sum(side * side)) + EPS) /
                             (float(np.sum(mid * mid)) + EPS))


def _trajectory(x: np.ndarray, sr: int, seconds: float = 2.0) -> np.ndarray:
    frame = int(0.1 * sr)
    stop = min(len(x), int(seconds * sr))
    values: list[float] = []
    peak = float(np.max(np.sum(x * x, axis=1))) + EPS
    for start in range(TAIL_START, stop - frame + 1, frame):
        seg = x[start:start + frame]
        if float(np.mean(np.sum(seg * seg, axis=1))) < peak * 1.0e-6:
            values.append(math.nan)
        else:
            values.append(float(np.clip(_ild(seg), -TRAJECTORY_ILD_LIMIT_DB,
                                        TRAJECTORY_ILD_LIMIT_DB)))
    return np.asarray(values)


def _measure(paths: dict[str, Path]) -> tuple[Metrics, np.ndarray]:
    left, sr = _load(paths["left"])
    right, sr_r = _load(paths["right"])
    center, sr_c = _load(paths["center"])
    if sr != sr_r or sr != sr_c:
        raise ValueError("sample-rate mismatch in capture set")
    l_tail, r_tail, c_tail = _tail(left, sr), _tail(right, sr), _tail(center, sr)
    n = min(len(l_tail), len(r_tail))
    mirror = r_tail[:n, ::-1]
    trajectory = _trajectory(left, sr)
    return Metrics(
        ild_db=_ild(l_tail),
        ild_low_db=_band_ild(l_tail, sr, 80.0, 300.0),
        ild_mid_db=_band_ild(l_tail, sr, 300.0, 2000.0),
        ild_high_db=_band_ild(l_tail, sr, 2000.0, 10000.0),
        pan_swap_db=_normalised_difference_db(l_tail[:n], r_tail[:n]),
        mirror_residual_db=_normalised_difference_db(l_tail[:n], mirror),
        center_corr=_corr(c_tail),
        center_side_mid_db=_side_mid(c_tail),
    ), trajectory


def _finite_error(a: float, b: float) -> float:
    return abs(a - b) if math.isfinite(a) and math.isfinite(b) else math.nan


def _compare(dv: Metrics, ref: Metrics, dv_traj: np.ndarray, ref_traj: np.ndarray) -> tuple[list[str], bool]:
    failures: list[str] = []
    actual_stereo = ref.pan_swap_db > -20.0
    if not actual_stereo:
        return failures, False

    checks = (
        ("ILD", _finite_error(dv.ild_db, ref.ild_db), ILD_JND_DB),
        ("low-band ILD", _finite_error(dv.ild_low_db, ref.ild_low_db), ILD_JND_DB),
        ("mid-band ILD", _finite_error(dv.ild_mid_db, ref.ild_mid_db), ILD_JND_DB),
        ("high-band ILD", _finite_error(dv.ild_high_db, ref.ild_high_db), ILD_JND_DB),
        ("pan-swap response", _finite_error(dv.pan_swap_db, ref.pan_swap_db), PAN_SWAP_JND_DB),
        ("center side/mid", _finite_error(dv.center_side_mid_db, ref.center_side_mid_db), SIDE_MID_JND_DB),
    )
    for label, error, gate in checks:
        if not math.isfinite(error) or error > gate:
            failures.append(f"{label} error {error:.2f} > {gate:.2f}")

    corr_gate = 0.04 if abs(ref.center_corr) >= 0.8 else 0.10
    corr_error = _finite_error(dv.center_corr, ref.center_corr)
    if not math.isfinite(corr_error) or corr_error > corr_gate:
        failures.append(f"center correlation error {corr_error:.3f} > {corr_gate:.3f}")

    n = min(len(dv_traj), len(ref_traj))
    good = np.isfinite(dv_traj[:n]) & np.isfinite(ref_traj[:n])
    trajectory_error = math.sqrt(float(np.mean((dv_traj[:n][good] - ref_traj[:n][good]) ** 2))) \
        if np.any(good) else math.nan
    dv.trajectory_rmse_db = trajectory_error
    ref.trajectory_rmse_db = 0.0
    if not math.isfinite(trajectory_error) or trajectory_error > IMAGE_TRAJECTORY_JND_DB:
        failures.append(f"ILD trajectory RMSE {trajectory_error:.2f} > {IMAGE_TRAJECTORY_JND_DB:.2f}")
    return failures, True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", action="store_true", help="render DV and references before analysing")
    parser.add_argument("--capture-dv-only", action="store_true",
                        help="render only DuskVerb and reuse an existing reference capture")
    parser.add_argument("--preset", action="append", choices=sorted(PRESETS), help="limit to one or more presets")
    parser.add_argument("--out", type=Path, default=Path("/tmp/duskverb-stereo-jnd"))
    parser.add_argument("--audio-tools", type=Path, default=DEFAULT_AUDIO_TOOLS)
    parser.add_argument("--renderer", type=Path, default=DEFAULT_RENDERER)
    parser.add_argument("--dv-vst3", type=Path, default=DEFAULT_DV)
    parser.add_argument("--vvv-vst3", type=Path, default=DEFAULT_VVV)
    parser.add_argument("--shimmer-vst3", type=Path, default=DEFAULT_SHIMMER)
    parser.add_argument("--json", type=Path, help="write machine-readable results")
    args = parser.parse_args()

    selected = args.preset or list(PRESETS)
    inputs = _make_inputs(args.out / "inputs")
    vvv_path = args.audio_tools / "tools" / "duskverb" / "tuner" / "vvv_anchor_presets.json"
    vvv_data = json.loads(vvv_path.read_text())
    results: list[dict] = []
    total_failures = 0

    for index, preset in enumerate(selected, 1):
        slug = preset.lower().replace(" ", "_").replace("'", "")
        root = args.out / slug
        dv_root, ref_root = root / "dv", root / "ref"
        print(f"[{index}/{len(selected)}] {preset}", flush=True)
        if args.capture or args.capture_dv_only:
            _capture_dv(args.renderer, args.dv_vst3, preset, inputs, dv_root)
        if args.capture:
            _capture_reference(args.renderer, PRESETS[preset], args.audio_tools, args.vvv_vst3,
                               args.shimmer_vst3, vvv_data, inputs, ref_root)
        dv_paths = {side: dv_root / f"dv_{side}_stem.wav" for side in inputs}
        ref_paths = {side: ref_root / f"ref_{side}_stem.wav" for side in inputs}
        missing = [str(path) for path in [*dv_paths.values(), *ref_paths.values()] if not path.exists()]
        if missing:
            print("  MISSING " + ", ".join(missing), file=sys.stderr)
            total_failures += 1
            continue
        dv_metrics, dv_traj = _measure(dv_paths)
        ref_metrics, ref_traj = _measure(ref_paths)
        failures, actual_stereo = _compare(dv_metrics, ref_metrics, dv_traj, ref_traj)
        total_failures += len(failures)
        status = "PASS" if not failures else "FAIL"
        if not actual_stereo:
            status = "MONO-REF"
        print(f"  {status}: ILD {dv_metrics.ild_db:+.2f}/{ref_metrics.ild_db:+.2f} dB, "
              f"pan-swap {dv_metrics.pan_swap_db:+.2f}/{ref_metrics.pan_swap_db:+.2f} dB, "
              f"center r {dv_metrics.center_corr:+.3f}/{ref_metrics.center_corr:+.3f}")
        for failure in failures:
            print(f"    - {failure}")
        results.append({
            "preset": preset,
            "actual_stereo_reference": actual_stereo,
            "dv": asdict(dv_metrics),
            "reference": asdict(ref_metrics),
            "dv_trajectory_db": [None if not math.isfinite(v) else float(v) for v in dv_traj],
            "reference_trajectory_db": [None if not math.isfinite(v) else float(v) for v in ref_traj],
            "failures": failures,
        })

    payload = {"gates": {
        "ild_db": ILD_JND_DB,
        "pan_swap_db": PAN_SWAP_JND_DB,
        "trajectory_rmse_db": IMAGE_TRAJECTORY_JND_DB,
        "side_mid_db": SIDE_MID_JND_DB,
        "trajectory_ild_limit_db": TRAJECTORY_ILD_LIMIT_DB,
        "correlation_near_one": 0.04,
        "correlation_elsewhere": 0.10,
    }, "results": results, "failure_count": total_failures}
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"\nStereo JND audit: {len(results)} presets, {total_failures} failed checks")
    return 1 if total_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
