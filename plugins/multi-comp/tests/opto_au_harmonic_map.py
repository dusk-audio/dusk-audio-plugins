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
from scipy.signal import butter, sosfilt


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
CHARGE_DURATIONS = (17, 48, 144, 480)
CHARGE_LEVELS = (-12.0, -8.0, -4.0, -0.25)
CHARGE_REFERENCE = (
    (6.92236, 8.13157, 8.82621, 8.69921),
    (9.55162, 10.59441, 11.62389, 12.72483),
    (13.34629, 15.57380, 17.44177, 18.84350),
    (15.07678, 17.90595, 20.62950, 23.07068),
)
CHARGE_HELD_OUT = (
    (False, False, False, True),
    (False, True, False, False),
    (False, False, True, False),
    (True, False, False, False),
)
LF_FREQUENCIES = (50, 100, 200)
LF_LEVELS = tuple(range(-40, 1, 4))
LF_SETTINGS = ((0.0, False),) + ACTIVE
MUSIC_ROOT = REPO / "build-multi-comp-1176/opto-music-20260923"
MUSIC_PRS = (.3125, .40625, .5, .625, .71875, .8125, .90625, 1.0)
MUSIC_SETTINGS = ((0.0, False),) + tuple(
    (pr, limit) for limit in (False, True) for pr in MUSIC_PRS)
MUSIC_LATENCY = 67
MUSIC_FRAME = 240


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
        for position in tuple(i / 20.0 for i in range(21)):
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


def prepare_charge() -> list[dict]:
    """Write the frozen isolated-event grid used by the short-charge gate."""
    directory = OUT / "charge-stimuli"
    directory.mkdir(parents=True, exist_ok=True)
    event_start = FS // 10
    # Long enough for the event, the AU latency and the first 1 kHz probe.
    total = event_start + max(CHARGE_DURATIONS) + 4096
    records = []
    for duration in CHARGE_DURATIONS:
        for level in CHARGE_LEVELS:
            signal = np.empty(total, dtype=np.float64)
            amplitude = 10.0 ** (level / 20.0)
            probe = 10.0 ** (-48.0 / 20.0)
            sample = np.arange(total, dtype=np.float64)
            envelope = np.where(
                sample < event_start, 0.0,
                np.where(sample < event_start + duration, amplitude, probe))
            signal[:] = envelope * np.sin(2.0 * np.pi * 1000.0 * sample / FS)
            stem = f"charge_n{duration:03d}_l{level:+06.2f}".replace("+", "p").replace("-", "m").replace(".", "p")
            path = directory / f"{stem}.wav"
            sf.write(path, np.column_stack((signal, signal)).astype(np.float32),
                     FS, subtype="FLOAT")
            records.append({"duration_samples": duration, "peak_dbfs": level,
                            "event_start_sample": event_start, "path": str(path),
                            "sha256": sha256(path)})
    atomic_json(directory / "manifest.json", {"sample_rate": FS, "records": records})
    return records


def capture_charge(plugin: str, tag: str) -> None:
    """Capture every charge cell in a fresh AU instance through the frozen host."""
    records = prepare_charge()
    root = OUT / "charge-captures" / tag / plugin
    for record in records:
        path = Path(record["path"])
        for pr, role in ((0.0, "control"), (0.70, "active")):
            destination = root / path.stem / role
            if (destination / "complete.json").exists():
                continue
            print(f"capture charge {tag}/{plugin} {path.stem} {role}", flush=True)
            run_host(plugin, destination, [path], pr, 0.23754, False)


def score_charge(plugin: str, tag: str) -> None:
    records = prepare_charge()
    latency = 87 if plugin == "uad" else 67
    root = OUT / "charge-captures" / tag / plugin
    rows = []
    fitted_squared = held_squared = 0.0
    fitted_worst = held_worst = 0.0
    fitted_count = held_count = 0
    for record in records:
        duration = record["duration_samples"]
        level = record["peak_dbfs"]
        duration_index = CHARGE_DURATIONS.index(duration)
        level_index = CHARGE_LEVELS.index(level)
        name = f"s_{Path(record['path']).stem}_stem.wav"
        signals = {}
        for role in ("control", "active"):
            path = root / Path(record["path"]).stem / role / name
            signals[role], rate = sf.read(path, always_2d=True, dtype="float64")
            if rate != FS:
                raise RuntimeError(f"unexpected charge render rate: {path}")
        start = record["event_start_sample"] + duration + latency
        stop = start + FS // 1000
        sample = np.arange(start, stop, dtype=np.float64)
        basis = np.exp(-2j * np.pi * 1000.0 * sample / FS)
        amplitudes = {role: 2.0 * abs(np.dot(signal[start:stop, 0], basis)) / len(basis)
                      for role, signal in signals.items()}
        measured = 20.0 * math.log10(amplitudes["control"] / amplitudes["active"])
        reference = CHARGE_REFERENCE[duration_index][level_index]
        delta = measured - reference
        held_out = CHARGE_HELD_OUT[duration_index][level_index]
        rows.append({**record, "reference_db": reference, "measured_db": measured,
                     "error_db": delta, "held_out": held_out,
                     "analysis_start_sample": start, "analysis_end_sample": stop,
                     "reported_latency_samples": latency})
        if held_out:
            held_squared += delta * delta
            held_worst = max(held_worst, abs(delta))
            held_count += 1
        else:
            fitted_squared += delta * delta
            fitted_worst = max(fitted_worst, abs(delta))
            fitted_count += 1
    fitted_rms = math.sqrt(fitted_squared / fitted_count)
    held_rms = math.sqrt(held_squared / held_count)
    summary = {
        "measurement_boundary": "actual Audio Unit hosted by duskverb_render",
        "plugin": plugin, "tag": tag, "sample_rate": FS, "block_size": BLOCK,
        "prerun_seconds": PRERUN, "stereo": True,
        "fitted_rms_error_db": fitted_rms, "fitted_worst_error_db": fitted_worst,
        "held_out_rms_error_db": held_rms, "held_out_worst_error_db": held_worst,
        "pass": fitted_rms < 0.42 and fitted_worst < 0.75
                and held_rms < 0.50 and held_worst < 0.75,
        "rows": rows,
    }
    atomic_json(OUT / f"charge-au-{tag}-{plugin}-b{BLOCK}.json", summary)
    print(json.dumps({k: v for k, v in summary.items() if k != "rows"}, indent=2))


def prepare_lf() -> list[dict]:
    directory = OUT / "lf-dynamic-stimuli"
    directory.mkdir(parents=True, exist_ok=True)
    sample = np.arange(8 * FS, dtype=np.float64)
    records = []
    for frequency in LF_FREQUENCIES:
        for level in LF_LEVELS:
            peak = 10.0 ** (level / 20.0)
            signal = peak * np.sin(2.0 * np.pi * frequency * sample / FS)
            path = directory / f"lf_f{frequency:04d}_l{level:+04d}.wav"
            sf.write(path, np.column_stack((signal, signal)).astype(np.float32),
                     FS, subtype="FLOAT")
            records.append({"frequency_hz": frequency, "input_dbfs": level,
                            "path": str(path), "sha256": sha256(path)})
    atomic_json(directory / "manifest.json", {"sample_rate": FS, "records": records})
    return records


def capture_lf(plugin: str, tag: str) -> None:
    records = prepare_lf()
    inputs = [Path(record["path"]) for record in records]
    for pr, limit in LF_SETTINGS:
        label = f"g0.25_pr{pr:.2f}_{'limit' if limit else 'comp'}"
        destination = OUT / "lf-dynamic-captures" / tag / plugin / label
        if (destination / "complete.json").exists():
            continue
        print(f"capture LF dynamic {tag}/{plugin} {label}", flush=True)
        run_host(plugin, destination, inputs, pr, 0.25, limit)


def analyse_lf(plugin: str, tag: str) -> list[dict]:
    records = prepare_lf()
    latency = 87 if plugin == "uad" else 67
    root = OUT / "lf-dynamic-captures" / tag / plugin
    rows = []
    fundamentals = {}
    for pr, limit in LF_SETTINGS:
        label = f"g0.25_pr{pr:.2f}_{'limit' if limit else 'comp'}"
        complete = json.loads((root / label / "complete.json").read_text())
        for record in records:
            path = root / label / f"s_{Path(record['path']).stem}_stem.wav"
            signal, rate = sf.read(path, always_2d=True, dtype="float64")
            if rate != FS:
                raise RuntimeError(f"unexpected LF render rate: {path}")
            start = 7 * FS + latency
            window = signal[start:start + FS, 0]
            frequency = record["frequency_hz"]
            fundamental = complex_harmonic(window, frequency)
            fundamental_dbfs = 20.0 * math.log10(max(abs(fundamental), 1.0e-30))
            fundamentals[(frequency, record["input_dbfs"], pr, limit)] = fundamental_dbfs
            fund_phase = math.degrees(math.atan2(fundamental.imag, fundamental.real))
            for harmonic in (3, 5, 7):
                coefficient = complex_harmonic(window, harmonic * frequency)
                magnitude = 20.0 * math.log10(max(abs(coefficient), 1.0e-30))
                phase = wrap_degrees(math.degrees(math.atan2(coefficient.imag,
                                                              coefficient.real))
                                     - harmonic * fund_phase)
                rows.append({"plugin": plugin, "tag": tag,
                             "frequency_hz": frequency,
                             "input_dbfs": record["input_dbfs"],
                             "peak_reduction_normalised": pr, "gain_normalised": 0.25,
                             "limit": limit, "harmonic": harmonic,
                             "magnitude_dbfs": magnitude,
                             "relative_phase_degrees": phase,
                             "fundamental_dbfs": fundamental_dbfs,
                             "render": str(path), "latency_samples": latency,
                             "component_binary_sha256": complete["component_binary_sha256"]})
    baseline = {(f, level): value for (f, level, pr, limit), value in fundamentals.items()
                if pr == 0.0 and not limit}
    for row in rows:
        row["gain_reduction_db"] = baseline[(row["frequency_hz"], row["input_dbfs"])] \
            - row["fundamental_dbfs"]
    atomic_json(OUT / f"lf-dynamic-{tag}-{plugin}.json", rows)
    print(f"analysed {len(rows)} LF dynamic harmonic rows for {plugin}/{tag}")
    return rows


def compare_lf(tag: str) -> None:
    reference = analyse_lf("uad", "r6")
    candidate = analyse_lf("mc2", tag)
    key = lambda r: (r["frequency_hz"], r["input_dbfs"],
                     r["peak_reduction_normalised"], r["limit"], r["harmonic"])
    right = {key(row): row for row in candidate}
    rows = []
    for uad in reference:
        mc2 = right[key(uad)]
        error = mc2["magnitude_dbfs"] - uad["magnitude_dbfs"]
        eligible = uad["magnitude_dbfs"] > -100.0
        rows.append({"key": key(uad), "uad_dbfs": uad["magnitude_dbfs"],
                     "mc2_dbfs": mc2["magnitude_dbfs"], "error_db": error,
                     "uad_phase_degrees": uad["relative_phase_degrees"],
                     "mc2_phase_degrees": mc2["relative_phase_degrees"],
                     "uad_gain_reduction_db": uad["gain_reduction_db"],
                     "mc2_gain_reduction_db": mc2["gain_reduction_db"],
                     "above_bar_floor": eligible,
                     "pass_2db": not eligible or abs(error) <= 2.0})
    eligible_rows = [row for row in rows if row["above_bar_floor"]]
    summary = {"measurement_boundary": "actual Audio Units hosted by duskverb_render",
               "tag": tag, "eligible_cells": len(eligible_rows),
               "passing_cells": sum(row["pass_2db"] for row in eligible_rows),
               "worst_abs_error_db": max(abs(row["error_db"]) for row in eligible_rows),
               "pass": all(row["pass_2db"] for row in eligible_rows), "rows": rows}
    atomic_json(OUT / f"lf-dynamic-comparison-{tag}.json", summary)
    print(json.dumps({k: v for k, v in summary.items() if k != "rows"}, indent=2))


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


def music_clips() -> list[Path]:
    manifest = json.loads((MUSIC_ROOT / "stimuli/stimuli.json").read_text())
    names = sorted(name for name in manifest["stimuli"] if name.startswith("v_"))
    if not names or any(name.startswith("z_") or "untitled" in name.lower()
                        for name in names):
        raise RuntimeError("invalid or forbidden music validation selection")
    return [MUSIC_ROOT / "stimuli" / f"{name}.wav" for name in names]


def music_setting_label(pr: float, limit: bool) -> str:
    return f"pr{pr:.5f}_{'limit' if limit else 'comp'}"


def capture_music(tag: str) -> None:
    """Render only the established non-sealed LEWITT VAL set through the AU."""
    inputs = music_clips()
    root = OUT / "music-au" / tag
    for pr, limit in MUSIC_SETTINGS:
        label = music_setting_label(pr, limit)
        destination = root / label
        if (destination / "complete.json").exists():
            print(f"reuse MC-2 AU music {label}")
            continue
        print(f"capture MC-2 AU music {label} ({len(inputs)} clips)", flush=True)
        run_host("mc2", destination, inputs, pr, 0.25, limit)


def score_music(tag: str) -> None:
    """Score actual-AU output with the campaign's corrected causal metric."""
    inputs = music_clips()
    root = OUT / "music-au" / tag
    hp20 = butter(4, 20.0, "high", fs=FS, output="sos")

    def energies(signal: np.ndarray) -> np.ndarray:
        filtered = sosfilt(hp20, signal, axis=0)
        frames = len(filtered) // MUSIC_FRAME
        framed = filtered[:frames * MUSIC_FRAME].reshape(
            frames, MUSIC_FRAME, filtered.shape[1])
        return np.sum(np.square(framed), axis=(1, 2)) \
            / (MUSIC_FRAME * filtered.shape[1])

    rows = []
    for input_path in inputs:
        clip = input_path.stem
        source, rate = sf.read(input_path, always_2d=True, dtype="float64")
        if rate != FS:
            raise RuntimeError(f"unexpected music sample rate for {input_path}")
        cache_path = MUSIC_ROOT / "fit-cache-causal" / f"{clip}.npz"
        cache = np.load(cache_path, allow_pickle=True)
        native_gr = cache["gr"].item()
        keep = cache["keep"].item()
        base_path = root / music_setting_label(0.0, False) / f"s_{clip}_stem.wav"
        base, base_rate = sf.read(base_path, always_2d=True, dtype="float64")
        if base_rate != FS or len(base) < len(source) + MUSIC_LATENCY:
            raise RuntimeError(f"bad actual-AU music base render for {clip}")
        base_energy = energies(base[MUSIC_LATENCY:MUSIC_LATENCY + len(source)])
        for pr, limit in MUSIC_SETTINGS[1:]:
            rendered_path = root / music_setting_label(pr, limit) / f"s_{clip}_stem.wav"
            rendered, rendered_rate = sf.read(
                rendered_path, always_2d=True, dtype="float64")
            if rendered_rate != FS or len(rendered) < len(source) + MUSIC_LATENCY:
                raise RuntimeError(f"bad actual-AU music render for {clip}")
            active_energy = energies(
                rendered[MUSIC_LATENCY:MUSIC_LATENCY + len(source)])
            measured = 10.0 * np.log10(
                (base_energy + 1.0e-30) / (active_energy + 1.0e-30))
            reference = native_gr[(pr, int(limit))]
            mask = keep[(pr, int(limit))]
            length = min(len(measured), len(reference), len(mask))
            error = (measured[:length] - reference[:length])[mask[:length]]
            rows.append({
                "clip": clip, "peak_reduction_normalised": pr,
                "limit": limit, "frames": int(error.size),
                "mean_error_db": float(np.mean(error)),
                "rms_error_db": float(np.sqrt(np.mean(np.square(error)))),
                "mean_square_error_db2": float(np.mean(np.square(error))),
            })
    overall = math.sqrt(float(np.mean([row["mean_square_error_db2"] for row in rows])))
    high_rows = [row for row in rows if row["peak_reduction_normalised"] >= .90625]
    high = math.sqrt(float(np.mean([row["mean_square_error_db2"] for row in high_rows])))
    summary = {
        "measurement_boundary": "actual built MC-2 Audio Unit hosted by duskverb_render",
        "metric": "causal_hp20_v1", "corpus": "LEWITT VAL only",
        "sample_rate": FS, "block_size": BLOCK, "prerun_seconds": PRERUN,
        "latency_compensation_samples": MUSIC_LATENCY,
        "overall_rms_error_db": overall, "high_pr_rms_error_db": high,
        "rows": rows,
    }
    atomic_json(OUT / f"music-au-score-{tag}.json", summary)
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
    p = sub.add_parser("charge-capture"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    p = sub.add_parser("charge-score"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    p = sub.add_parser("lf-capture"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    p = sub.add_parser("lf-analyse"); p.add_argument("plugin", choices=("uad", "mc2")); p.add_argument("tag")
    p = sub.add_parser("lf-compare"); p.add_argument("tag")
    p = sub.add_parser("music-capture"); p.add_argument("tag")
    p = sub.add_parser("music-score"); p.add_argument("tag")
    args = parser.parse_args()
    if args.command == "prepare": prepare()
    elif args.command == "sweep": sweep(args.plugin)
    elif args.command == "capture": prepare_if_needed(); capture_map(args.plugin, args.tag)
    elif args.command == "analyse": analyse_capture(args.plugin, args.tag)
    elif args.command == "compare": compare(args.tag)
    elif args.command == "witness": witness(args.tag)
    elif args.command == "settled": capture_settled(args.tag)
    elif args.command == "charge-capture": capture_charge(args.plugin, args.tag)
    elif args.command == "charge-score": score_charge(args.plugin, args.tag)
    elif args.command == "lf-capture": capture_lf(args.plugin, args.tag)
    elif args.command == "lf-analyse": analyse_lf(args.plugin, args.tag)
    elif args.command == "lf-compare": compare_lf(args.tag)
    elif args.command == "music-capture": capture_music(args.tag)
    elif args.command == "music-score": score_music(args.tag)


if __name__ == "__main__":
    main()
