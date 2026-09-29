#!/usr/bin/env python3
"""Capture and measure the Opto harmonic map through the two actual AUs.

The Audio Unit host is the acceptance boundary.  This script never calls the
Multi-Comp core directly.  Captures are deliberately kept below build-* so the
large reference/render evidence remains local rather than entering git.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf


REPO = Path(__file__).resolve().parents[3]
OUT = REPO / "build-multi-comp-1176/opto-production-harmonics-20260928"
HOST = REPO / "build-multi-comp-1176/programme-native-20260911/duskverb_render"
MC2 = REPO / "build-mc2/bin/multi-comp-2.component"
UAD = Path("/Library/Audio/Plug-Ins/Components/uaudio_teletronix_la-2a_tc.component")
FS = 48_000
BLOCK = int(os.environ.get("OPTO_AU_BLOCK", "512"))
PRERUN = 2.0
FREQUENCIES = (100, 1000, 5000)
LEVELS = tuple(range(-40, 1, 4))
GAINS = (0.15, 0.25, 0.35)
ACTIVE = ((0.35, False), (0.70, False), (1.0, False),
          (0.35, True), (0.70, True), (1.0, True))


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def atomic_json(path: Path, obj: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")
    temp.replace(path)


def prepare() -> None:
    stimuli = OUT / "stimuli"
    stimuli.mkdir(parents=True, exist_ok=True)
    n = 8 * FS
    t = np.arange(n, dtype=np.float64) / FS
    records = []
    for frequency in FREQUENCIES:
        for level in LEVELS:
            peak = 10.0 ** (level / 20.0)
            mono = peak * np.sin(2.0 * np.pi * frequency * t)
            stereo = np.column_stack((mono, mono)).astype(np.float32)
            path = stimuli / f"tone_f{frequency:04d}_l{level:+04d}.wav"
            sf.write(path, stereo, FS, subtype="FLOAT")
            records.append({"frequency_hz": frequency, "input_dbfs": level,
                            "path": str(path), "sha256": sha256(path)})

    # A positive impulse makes latency measurement unambiguous.
    impulse = np.zeros((FS, 2), dtype=np.float32)
    impulse[1024, :] = 0.25
    impulse_path = stimuli / "latency_impulse.wav"
    sf.write(impulse_path, impulse, FS, subtype="FLOAT")
    atomic_json(stimuli / "manifest.json", {
        "sample_rate": FS, "frames_per_tone": n, "tones": records,
        "impulse": {"path": str(impulse_path), "sha256": sha256(impulse_path)},
    })
    print(f"prepared {len(records)} coherent stereo tones in {stimuli}")


def plugin_path(plugin: str) -> Path:
    return UAD if plugin == "uad" else MC2


def params(plugin: str, pr: float, gain: float, limit: bool) -> list[tuple[str, float]]:
    if plugin == "uad":
        return [("Peak Reduct", pr), ("Gain", gain), ("Comp/Limit", float(limit)),
                ("Meter", 0.5), ("Power", 1.0)]
    # Explicit plugin-only neutral state.  In particular: global Mix is fully
    # wet, detector HP is off, analogue noise is off, and the link mode is
    # ordinary stereo.  Generic distortion is not the Opto model's colour path.
    return [
        ("Mode", 0.0), ("Bypass", 0.0), ("Stereo Link", 1.0), ("Mix", 1.0),
        ("SC HP Filter", 0.0), ("True Peak", 0.0), ("TP Quality", 0.0),
        ("External Sidechain", 0.0), ("Auto Makeup", 0.0), ("Distortion", 0.0),
        ("Distortion Amt", 0.5), ("Oversampling", 0.5), ("Lookahead", 0.0),
        ("Peak Reduction", pr), ("Gain", gain), ("Limit Mode", float(limit)),
        ("SC Listen", 0.0), ("Analog Noise", 0.0), ("Link Mode", 0.0),
    ]


def print_indices(plugin: str) -> str:
    return "0,1,2,3,4" if plugin == "uad" else \
        "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,54,57,62"


READBACK = re.compile(
    r"--print-params \[(\d+)\] '([^']+)' norm=([-+0-9.eE]+) read_back='([^']*)'")


def run_host(plugin: str, out: Path, inputs: list[Path], pr: float, gain: float,
             limit: bool) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    argv = [str(HOST), "--au", str(plugin_path(plugin)), "--slug", "s",
            "--output-dir", str(out), "--sample-rate", str(FS),
            "--block-size", str(BLOCK), "--channels", "2",
            "--disable-aux-inputs", "--prerun-seconds", str(PRERUN),
            "--print-params", print_indices(plugin)]
    requested = params(plugin, pr, gain, limit)
    for name, value in requested:
        argv += ["--nparam", f"{name}={value:.9g}"]
    for path in inputs:
        argv += ["--input-wav", str(path)]
    result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    (out / "log.txt").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"AU host failed ({result.returncode}); see {out / 'log.txt'}")
    readbacks = [{"index": int(i), "name": name, "normalised": float(norm),
                  "display": display} for i, name, norm, display in READBACK.findall(result.stdout)]
    if len(readbacks) != len(requested):
        raise RuntimeError(f"expected {len(requested)} parameter readbacks, got {len(readbacks)}")
    by_name = {r["name"]: r for r in readbacks}
    worst = max(abs(by_name[name]["normalised"] - value) for name, value in requested)
    # UAD's AU exposes a 12-bit normalised control grid (1/4096 steps); values
    # such as 0.10 and 0.35 necessarily round.  Half a grid step is acceptable
    # and the exact post-apply value is carried into every measurement record.
    readback_bar = 1.23e-4
    if worst > readback_bar:
        raise RuntimeError(f"AU parameter readback error {worst:.9g} exceeds {readback_bar}")
    # The frozen host always renders its six built-in probes as well as explicit
    # inputs.  Only the explicit input stems belong to this capture.
    outputs = [out / f"s_{path.stem}_stem.wav" for path in inputs]
    missing = [path for path in outputs if not path.exists()]
    if missing:
        raise RuntimeError(f"missing explicit AU outputs: {missing}")
    complete = {
        "plugin": plugin, "component": str(plugin_path(plugin)),
        "component_binary_sha256": component_hash(plugin), "argv": argv,
        "peak_reduction_normalised": pr, "gain_normalised": gain,
        "limit": limit, "sample_rate": FS, "block_size": BLOCK,
        "prerun_seconds": PRERUN, "channels": 2, "readbacks": readbacks,
        "worst_normalised_readback_delta": worst, "readback_bar": readback_bar,
        "inputs": {p.name: sha256(p) for p in inputs},
        "outputs": {p.name: sha256(p) for p in outputs},
    }
    atomic_json(out / "complete.json", complete)
    return complete


def component_hash(plugin: str) -> str:
    path = plugin_path(plugin)
    if plugin == "uad":
        binary = path / "Contents/MacOS/uaudio_teletronix_la-2a_tc"
    else:
        binary = path / "Contents/MacOS/multi-comp-2"
    return sha256(binary)


def capture_map(plugin: str, tag: str) -> None:
    manifest = json.loads((OUT / "stimuli/manifest.json").read_text())
    tones = [Path(r["path"]) for r in manifest["tones"]]
    settings = [(0.0, False)] + list(ACTIVE)
    for gain in GAINS:
        for pr, limit in settings:
            label = f"g{gain:.2f}_pr{pr:.2f}_{'limit' if limit else 'comp'}"
            destination = OUT / "captures" / tag / plugin / label
            if (destination / "complete.json").exists():
                print(f"reuse {plugin} {label}")
                continue
            print(f"capture {plugin} {label}", flush=True)
            run_host(plugin, destination, tones, pr, gain, limit)


def sweep(plugin: str) -> None:
    prepare_if_needed()
    tone = OUT / "stimuli/tone_f1000_l-040.wav"
    rows = []
    # Sweep one control at a time so its displayed curve is independently visible.
    for control in ("peak_reduction", "gain"):
        for position in (0.0, 0.1, 0.2, 0.25, 0.3, 0.35, 0.4, 0.5, 0.6, 0.7,
                         0.8, 0.9, 1.0):
            pr = position if control == "peak_reduction" else 0.0
            gain = position if control == "gain" else 0.25
            dest = OUT / "parameter-sweep" / plugin / control / f"p{position:.2f}"
            complete = run_host(plugin, dest, [tone], pr, gain, False)
            wanted = "Peak Reduct" if plugin == "uad" and control == "peak_reduction" \
                else "Peak Reduction" if control == "peak_reduction" else "Gain"
            rb = next(x for x in complete["readbacks"] if x["name"] == wanted)
            rows.append({"plugin": plugin, "control": control, "requested_normalised": position,
                         "readback_normalised": rb["normalised"], "display": rb["display"]})
            print(plugin, control, position, rb["normalised"], rb["display"])
    atomic_json(OUT / "parameter-sweep" / f"{plugin}.json", rows)


def prepare_if_needed() -> None:
    if not (OUT / "stimuli/manifest.json").exists():
        prepare()


def complex_harmonic(signal: np.ndarray, frequency: float) -> complex:
    n = len(signal)
    phase = np.exp(-2j * np.pi * frequency * np.arange(n) / FS)
    return 2.0 * np.dot(signal.astype(np.float64), phase) / n


def wrap_degrees(value: float) -> float:
    return (value + 180.0) % 360.0 - 180.0


def analyse_capture(plugin: str, tag: str) -> list[dict]:
    rows = []
    root = OUT / "captures" / tag / plugin
    for complete_path in sorted(root.glob("*/complete.json")):
        complete = json.loads(complete_path.read_text())
        for output in sorted(complete_path.parent.glob("s_tone_*_stem.wav")):
            match = re.fullmatch(r"s_tone_f(\d+)_l([+-]\d+)_stem\.wav", output.name)
            if not match:
                continue
            frequency, level = int(match.group(1)), int(match.group(2))
            audio, sample_rate = sf.read(output, always_2d=True, dtype="float64")
            if sample_rate != FS or audio.shape[1] != 2 or len(audio) < FS:
                raise RuntimeError(f"unexpected format: {output}")
            # The host appends six seconds of silence to every explicit stem,
            # so the file's final second is tail, not tone.  Analyse input
            # seconds 7..8 after compensating each AU's reported latency.
            latency = 87 if plugin == "uad" else 67
            start = 7 * FS + latency
            window = audio[start:start + FS, :]
            if len(window) != FS:
                raise RuntimeError(f"latency-compensated window is short: {output}")
            lr_error = float(np.max(np.abs(window[:, 0] - window[:, 1])))
            c1 = complex_harmonic(window[:, 0], frequency)
            fund_phase = math.degrees(math.atan2(c1.imag, c1.real))
            fund_mag = 20.0 * math.log10(max(abs(c1), 1.0e-30))
            for harmonic in range(2, 8):
                harmonic_frequency = harmonic * frequency
                representable = harmonic_frequency < FS / 2
                if representable:
                    coefficient = complex_harmonic(window[:, 0], harmonic_frequency)
                    magnitude = 20.0 * math.log10(max(abs(coefficient), 1.0e-30))
                    phase = wrap_degrees(math.degrees(math.atan2(coefficient.imag,
                                                                 coefficient.real))
                                         - harmonic * fund_phase)
                else:
                    magnitude = phase = None
                rows.append({
                    "plugin": plugin, "tag": tag, "frequency_hz": frequency,
                    "input_dbfs": level,
                    "peak_reduction_normalised": complete["peak_reduction_normalised"],
                    "gain_normalised": complete["gain_normalised"],
                    "limit": complete["limit"], "harmonic": harmonic,
                    "representable": representable, "magnitude_dbfs": magnitude,
                    "relative_phase_degrees": phase, "fundamental_dbfs": fund_mag,
                    "left_right_max_abs_delta": lr_error, "render": str(output),
                    "latency_samples": latency, "analysis_start_sample": start,
                })
    atomic_json(OUT / f"harmonics-{tag}-{plugin}.json", rows)
    print(f"analysed {len(rows)} harmonic rows for {plugin}/{tag}")
    return rows


def compare(tag: str) -> None:
    # The UAD reference map is immutable and captured once. Candidate tags only
    # identify successive actual MC-2 AU builds.
    uad = analyse_capture("uad", "base")
    mc2 = analyse_capture("mc2", tag)
    key = lambda r: (r["frequency_hz"], r["input_dbfs"],
                     r["peak_reduction_normalised"], r["gain_normalised"],
                     r["limit"], r["harmonic"])
    right = {key(r): r for r in mc2}
    comparisons = []
    for reference in uad:
        candidate = right[key(reference)]
        error = None
        if reference["representable"]:
            error = candidate["magnitude_dbfs"] - reference["magnitude_dbfs"]
        comparisons.append({"key": key(reference), "uad_dbfs": reference["magnitude_dbfs"],
                            "mc2_dbfs": candidate["magnitude_dbfs"], "error_db": error,
                            "uad_phase_degrees": reference["relative_phase_degrees"],
                            "mc2_phase_degrees": candidate["relative_phase_degrees"],
                            "above_bar_floor": bool(reference["representable"] and
                                                    reference["magnitude_dbfs"] > -100.0),
                            "pass_2db": bool(error is None or abs(error) <= 2.0 or
                                             reference["magnitude_dbfs"] <= -100.0)})
    eligible = [r for r in comparisons if r["above_bar_floor"]]
    result = {
        "tag": tag, "eligible_cells": len(eligible),
        "passing_cells": sum(r["pass_2db"] for r in eligible),
        "worst_abs_error_db": max((abs(r["error_db"]) for r in eligible), default=None),
        "pass": all(r["pass_2db"] for r in eligible), "rows": comparisons,
    }
    atomic_json(OUT / f"comparison-{tag}.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "rows"}, indent=2))


def witness(tag: str) -> None:
    """Write the owner's repeatable 1 kHz AU witness from captured audio."""
    import matplotlib.pyplot as plt

    condition = "g0.25_pr0.35_comp"
    filename = "s_tone_f1000_l-016_stem.wav"
    sources = {
        "UAD": (OUT / "captures/base/uad" / condition / filename, 87),
        f"MC2-{tag.upper()}": (OUT / "captures" / tag / "mc2" / condition / filename, 67),
    }
    destination = OUT / "checkpoints" / tag / "one-khz-pr35"
    destination.mkdir(parents=True, exist_ok=True)
    audio = {}
    for name, (path, latency) in sources.items():
        data, rate = sf.read(path, always_2d=True, dtype="float64")
        if rate != FS:
            raise RuntimeError(f"unexpected witness rate: {path}")
        start = 7 * FS + latency
        audio[name] = data[start:start + FS]
    uad_rms = math.sqrt(float(np.mean(np.square(audio["UAD"][:, 0]))))
    candidate_name = f"MC2-{tag.upper()}"
    candidate_rms = math.sqrt(float(np.mean(np.square(audio[candidate_name][:, 0]))))
    trim = uad_rms / max(candidate_rms, 1.0e-30)
    trim_db = 20.0 * math.log10(trim)
    audio[candidate_name] *= trim
    matched_delta_db = 20.0 * math.log10(
        math.sqrt(float(np.mean(np.square(audio[candidate_name][:, 0])))) / uad_rms)
    rows = []
    for harmonic in range(1, 8):
        row = {"harmonic": harmonic, "frequency_hz": harmonic * 1000}
        for name, signal in audio.items():
            coefficient = complex_harmonic(signal[:, 0], harmonic * 1000)
            row[f"{name}_dbfs"] = 20.0 * math.log10(max(abs(coefficient), 1.0e-30))
        row["candidate_minus_uad_db"] = row[f"{candidate_name}_dbfs"] - row["UAD_dbfs"]
        rows.append(row)
    for name, signal in audio.items():
        slug = name.lower().replace("-", "_")
        sf.write(destination / f"{slug}.wav", signal, FS, subtype="PCM_24")
        spectrum = 20.0 * np.log10(np.maximum(
            2.0 * np.abs(np.fft.rfft(signal[:, 0])) / len(signal), 1.0e-8))
        frequency = np.fft.rfftfreq(len(signal), 1.0 / FS)
        fig, ax = plt.subplots(figsize=(12, 6))
        ax.semilogx(frequency[1:], spectrum[1:], linewidth=0.8)
        ax.set(xlim=(20, 20000), ylim=(-140, 0), xlabel="Frequency (Hz)",
               ylabel="Magnitude (dBFS)", title=f"{name}: 1 kHz, PR 0.35, Gain 0.25, Compress")
        ax.grid(True, which="both", alpha=0.25)
        fig.tight_layout()
        fig.savefig(destination / f"{slug}-spectrum.png", dpi=160)
        plt.close(fig)
    metadata = {
        "measurement_boundary": "actual Audio Units hosted by duskverb_render",
        "candidate": tag, "sample_rate": FS, "block_size": BLOCK,
        "prerun_seconds": PRERUN, "stereo": True, "input_dbfs_peak": -16,
        "frequency_hz": 1000, "peak_reduction_normalised": 0.35,
        "gain_normalised": 0.25, "limit": False,
        "mc2_neutral": {"sidechain_hp": 0.0, "mix": 1.0, "analog_noise": 0.0},
        "latency_samples": {"UAD": 87, candidate_name: 67},
        "candidate_match_trim_db": trim_db,
        "matched_rms_delta_db": matched_delta_db, "harmonics": rows,
    }
    atomic_json(destination / "witness.json", metadata)
    table = ["| Harmonic | UAD dBFS | MC-2 dBFS | MC-2 - UAD dB |",
             "|---:|---:|---:|---:|"]
    for row in rows:
        table.append(f"| H{row['harmonic']} | {row['UAD_dbfs']:.3f} | "
                     f"{row[f'{candidate_name}_dbfs']:.3f} | "
                     f"{row['candidate_minus_uad_db']:+.3f} |")
    (destination / "harmonic-table.md").write_text("\n".join(table) + "\n")
    print(json.dumps(metadata, indent=2))


STEADY_RE = re.compile(
    r"\{([-+0-9.eE]+)f,\s*([-+0-9.eE]+),\s*([-+0-9.eE]+)f,\s*"
    r"([-+0-9.eE]+)f,\s*([-+0-9.eE]+)f,\s*(true|false)\}")


def steady_rows() -> list[dict]:
    fixture = (REPO / "plugins/multi-comp/core/tests/MultiCompOptoParityFixtures.hpp").read_text()
    body = fixture.split("inline constexpr std::array<Steady, 122> steady{{", 1)[1].split("}};", 1)[0]
    rows = []
    for index, match in enumerate(STEADY_RE.finditer(body)):
        level, frequency, pr, gain, reference, limit = match.groups()
        rows.append({"row": index, "input_dbfs": float(level),
                     "frequency_hz": float(frequency), "peak_reduction": float(pr),
                     "gain": float(gain), "reference_db": float(reference),
                     "limit": limit == "true"})
    if len(rows) != 122:
        raise RuntimeError(f"parsed {len(rows)} settled rows, expected 122")
    return rows


def settled_stem(row: dict) -> str:
    # The input is independent of PR/Gain/mode; reuse identical tones.
    f = str(row["frequency_hz"]).replace(".", "p")
    level = f"{int(row['input_dbfs']):+03d}".replace("-", "m").replace("+", "p")
    return f"settled_f{f}_l{level}"


def prepare_settled(rows: list[dict]) -> dict[str, Path]:
    directory = OUT / "settled-stimuli"
    directory.mkdir(parents=True, exist_ok=True)
    t = np.arange(8 * FS, dtype=np.float64) / FS
    result = {}
    for row in rows:
        stem = settled_stem(row)
        if stem in result:
            continue
        path = directory / f"{stem}.wav"
        peak = 10.0 ** (row["input_dbfs"] / 20.0)
        mono = peak * np.sin(2.0 * np.pi * row["frequency_hz"] * t)
        sf.write(path, np.column_stack((mono, mono)).astype(np.float32), FS, subtype="FLOAT")
        result[stem] = path
    atomic_json(directory / "manifest.json", {
        "sample_rate": FS, "duration_seconds": 8,
        "files": {stem: {"path": str(path), "sha256": sha256(path)}
                  for stem, path in result.items()},
    })
    return result


def capture_settled(tag: str) -> None:
    rows = steady_rows()
    stimuli = prepare_settled(rows)
    root = OUT / f"settled-au-{tag}-b{BLOCK}"
    active_groups: dict[tuple[float, float, bool], list[dict]] = {}
    base_groups: dict[tuple[float, bool], list[dict]] = {}
    for row in rows:
        active_groups.setdefault((row["peak_reduction"], row["gain"], row["limit"]), []).append(row)
        base_groups.setdefault((row["gain"], row["limit"]), []).append(row)
    for (pr, gain, limit), group in sorted(active_groups.items()):
        label = f"active_pr{pr:g}_g{gain:.7g}_{'limit' if limit else 'comp'}"
        paths = sorted({stimuli[settled_stem(row)] for row in group})
        destination = root / label
        if not (destination / "complete.json").exists():
            print(f"capture MC-2 AU settled {label} ({len(paths)} tones)", flush=True)
            run_host("mc2", destination, paths, pr / 100.0, gain / 100.0, limit)
    for (gain, limit), group in sorted(base_groups.items()):
        label = f"base_pr0_g{gain:.7g}_{'limit' if limit else 'comp'}"
        paths = sorted({stimuli[settled_stem(row)] for row in group})
        destination = root / label
        if not (destination / "complete.json").exists():
            print(f"capture MC-2 AU settled {label} ({len(paths)} tones)", flush=True)
            run_host("mc2", destination, paths, 0.0, gain / 100.0, limit)

    lab = json.loads((OUT / "base-parity/result.json").read_text())["rows"]
    results = []
    begin, end = 6 * FS + 67, 15 * FS // 2 + 67
    for row in rows:
        active_label = (f"active_pr{row['peak_reduction']:g}_g{row['gain']:.7g}_"
                        f"{'limit' if row['limit'] else 'comp'}")
        base_label = (f"base_pr0_g{row['gain']:.7g}_"
                      f"{'limit' if row['limit'] else 'comp'}")
        name = f"s_{settled_stem(row)}_stem.wav"
        active, arate = sf.read(root / active_label / name, always_2d=True, dtype="float64")
        base, brate = sf.read(root / base_label / name, always_2d=True, dtype="float64")
        if arate != FS or brate != FS or len(active) < end or len(base) < end:
            raise RuntimeError(f"bad settled render format for row {row['row']}")
        active_power = float(np.sum(np.square(active[begin:end, 0])))
        base_power = float(np.sum(np.square(base[begin:end, 0])))
        measured = 10.0 * math.log10(base_power / active_power)
        lab_db = float(lab[row["row"]]["lab_db"])
        results.append({**row, "au_db": measured, "lab_db": lab_db,
                        "au_minus_lab_db": measured - lab_db,
                        "pass_0p01db": abs(measured - lab_db) <= 0.01,
                        "analysis_start_sample": begin, "analysis_end_sample": end,
                        "reported_latency_samples": 67})
    summary = {
        "measurement_boundary": "actual built MC-2 Audio Unit hosted by duskverb_render",
        "sample_rate": FS, "block_size": BLOCK, "prerun_seconds": PRERUN,
        "cells": len(results),
        "passing_cells": sum(x["pass_0p01db"] for x in results),
        "worst_abs_au_minus_lab_db": max(abs(x["au_minus_lab_db"]) for x in results),
        "pass": all(x["pass_0p01db"] for x in results), "rows": results,
    }
    atomic_json(OUT / f"settled-au-parity-{tag}-b{BLOCK}.json", summary)
    print(json.dumps({k: v for k, v in summary.items() if k != "rows"}, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("prepare")
    p = sub.add_parser("sweep"); p.add_argument("plugin", choices=("uad", "mc2"))
    p = sub.add_parser("capture"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    p = sub.add_parser("analyse"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    p = sub.add_parser("compare"); p.add_argument("tag")
    p = sub.add_parser("witness"); p.add_argument("tag")
    p = sub.add_parser("settled"); p.add_argument("tag")
    args = parser.parse_args()
    if args.command == "prepare": prepare()
    elif args.command == "sweep": sweep(args.plugin)
    elif args.command == "capture": prepare_if_needed(); capture_map(args.plugin, args.tag)
    elif args.command == "analyse": analyse_capture(args.plugin, args.tag)
    elif args.command == "compare": compare(args.tag)
    elif args.command == "witness": witness(args.tag)
    elif args.command == "settled": capture_settled(args.tag)


if __name__ == "__main__":
    main()
