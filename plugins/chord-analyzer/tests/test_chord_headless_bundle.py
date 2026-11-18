#!/usr/bin/env python3
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
"""Validate the actual relocated LV2 bundle or extracted release archive through lilv."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import tempfile
from urllib.parse import unquote, urlparse
import zipfile

from test_chord_analyzer_lv2 import exercise, require

URI = "https://dusk-audio.github.io/plugins/chord-analyzer-headless"
BUNDLE_NAME = "Chord Analyzer Headless.lv2"
BUNDLE_FILES = {"manifest.ttl", "chord-analyzer-headless.ttl", "ChordAnalyzerHeadless.so"}
NOTE_NAMES = ["C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"]
QUALITY_NAMES = ["-", "maj", "m", "dim", "aug", "7", "maj7", "m7", "mMaj7", "dim7", "m7b5", "aug7", "augMaj7",
                 "sus2", "sus4", "7sus4", "add9", "add11", "6", "m6", "maj9", "m9", "9", "maj11", "m11", "11",
                 "maj13", "m13", "13", "5", "7b5", "7#5", "7b9", "7#9"]
INVERSION_NAMES = ["-", "Root", "1st", "2nd", "3rd", "4th", "5th", "6th", "Slash"]
SYMBOLS = ["midi_in", "midi_out", "key_root", "key_mode", "suggestion_level", "show_inversions",
           "detected_root", "detected_quality", "detected_bass", "detected_inversion", "respect_sustain",
           *[f"suggestion_{i}_{kind}" for i in range(1, 5) for kind in ("root", "quality")]]


def inspect(bundle, expected_machine=None):
    require(bundle.name == BUNDLE_NAME and bundle.is_dir(), "retained bundle name exists")
    require({p.name for p in bundle.iterdir()} == BUNDLE_FILES, "bundle contains exactly its binary and two TTL files")
    require(all(p.is_file() and not p.is_symlink() for p in bundle.iterdir()), "bundle files are self-contained regular files")
    env = {**os.environ, "LV2_PATH": str(bundle.parent.resolve()), "LC_ALL": "C"}
    result = subprocess.run(["lv2info", URI], env=env, text=True, capture_output=True, check=True)
    require(not result.stderr.strip(), f"lilv parses the complete bundle without diagnostics: {result.stderr}")
    text = result.stdout
    require(re.search(r"Name:\s+Chord Analyzer Headless$", text, re.M), "host sees the retained product name")
    binary_uri = re.search(r"Binary:\s+(\S+)", text)
    require(binary_uri is not None, "lilv resolves a binary URI")
    binary = Path(unquote(urlparse(binary_uri[1]).path))
    require(binary.resolve() == (bundle / "ChordAnalyzerHeadless.so").resolve(), "manifest resolves the binary inside the relocated bundle")
    require("http://lv2plug.in/ns/ext/urid#map" in text and "http://lv2plug.in/ns/lv2core#hardRTCapable" in text,
            "retained URID mapping and hard-RT declarations exist")
    parts = re.split(r"^\s*Port ([0-9]+):\s*$", text, flags=re.M)
    ports = list(zip(parts[1::2], parts[2::2]))
    require([int(i) for i, _ in ports] == list(range(19)), "all 19 port indices are contiguous and retain their order")
    for (index, body), symbol in zip(ports, SYMBOLS):
        index = int(index)
        require(re.search(rf"Symbol:\s+{re.escape(symbol)}$", body, re.M), f"port {index} retains symbol {symbol}")
        expected_direction = "InputPort" if index in (0, 2, 3, 4, 5, 10) else "OutputPort"
        type_block = body.split("Type:", 1)[1].split("Symbol:", 1)[0].split("Scale Points:", 1)[0]
        classes = set(re.findall(r"http://\S+", type_block))
        type_uri = "http://lv2plug.in/ns/ext/atom#AtomPort" if index < 2 else "http://lv2plug.in/ns/lv2core#ControlPort"
        require(classes == {"http://lv2plug.in/ns/lv2core#" + expected_direction, type_uri}, f"{symbol}: exact input/output and atom/control classes")
        if index < 2:
            continue
        labels = []
        if index == 2:
            labels = NOTE_NAMES
        elif index == 3:
            labels = ["Major", "Minor"]
        elif index == 4:
            labels = ["Basic", "Basic + Intermediate", "All (+ Advanced)"]
        elif index in (6, 8, 11, 13, 15, 17):
            labels = ["-", *NOTE_NAMES]
        elif index in (7, 12, 14, 16, 18):
            labels = QUALITY_NAMES
        elif index == 9:
            labels = INVERSION_NAMES
        points = [(int(value), label) for value, label in re.findall(r'^\s+(-?\d+) = "([^"]*)"\s*$', body, re.M)]
        require(len(points) == len(labels) and dict(points) == dict(enumerate(labels)), f"{symbol}: complete scale-point values and labels")
        maximum = len(labels) - 1 if labels else 1
        expected_range = {"Minimum": 0, "Maximum": maximum, "Default": 2 if index == 4 else 1 if index in (5, 10) else 0}
        for name, value in expected_range.items():
            found = re.search(rf"{name}:\s+(-?[0-9.]+)", body)
            require(found is not None and float(found[1]) == value, f"{symbol}: retained {name.lower()}")
        properties = set(re.findall(r"http://\S+", body.split("Properties:", 1)[1]))
        require(properties == {"http://lv2plug.in/ns/lv2core#integer", "http://lv2plug.in/ns/lv2core#" + ("enumeration" if labels else "toggled")}, f"{symbol}: retained integer and choice properties")
    header = binary.read_bytes()[:20]
    require(len(header) == 20 and header[:6] == b"\x7fELF\x02\x01", "headless archive contains a little-endian 64-bit ELF")
    machine = struct.unpack_from("<H", header, 18)[0]
    # Captured from the actual published chord-analyzer-v1.3.0 headless assets.
    # Keep each architecture's imported runtime requirements at or below that
    # baseline; building on the newer desktop image must not silently raise it.
    limits = {62: {"GLIBC": (2, 30), "GLIBCXX": (3, 4, 29)},
              183: {"GLIBC": (2, 34), "GLIBCXX": (3, 4, 30)}}
    require(machine in limits, "headless release architecture is x86_64 or AArch64")
    require(expected_machine is None or machine == expected_machine, "archive filename matches the binary architecture")
    bounds = {**limits[machine], "CXXABI": (1, 3, 9), "GCC": (3, 0)}
    version_info = subprocess.run(["readelf", "--version-info", str(binary)], env=env, text=True, capture_output=True, check=True).stdout
    versions = set(re.findall(r"Name:\s+(\S+)", version_info))
    require(versions, "readelf enumerates the standard-runtime version requirements")
    for version in versions:
        namespace, _, numbers = version.partition("_")
        require(namespace in bounds and re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", numbers),
                f"unrecognized runtime version requirement: {version}")
        require(tuple(map(int, numbers.split("."))) <= bounds[namespace],
                f"runtime requirement exceeds the published headless baseline: {version}")
    relocations = subprocess.run(["ldd", "-r", str(binary)], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True).stdout
    require("undefined symbol" not in relocations and "not found" not in relocations, "binary loads without unresolved dependencies")
    dynamic = subprocess.run(["readelf", "-d", str(binary)], env=env, text=True, capture_output=True, check=True).stdout
    needed = set(re.findall(r"\(NEEDED\).*\[([^]]+)\]", dynamic))
    allowed = {"libstdc++.so.6", "libc++.so.1", "libc++abi.so.1", "libgcc_s.so.1", "libm.so.6", "libc.so.6",
               "libpthread.so.0", "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1"}
    require(needed <= allowed, f"headless binary depends only on standard C/C++ runtime libraries: {needed}")
    trace = exercise(binary)
    print(f"Headless bundle: 19 ordered ports, complete choices/ranges, {len(trace)} real MIDI blocks, standard-runtime dependencies only; published ABI floor retained")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", nargs="?", type=Path)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    require(bool(args.bundle) != bool(args.archive), "specify exactly one bundle or archive")
    expected_machine = None
    with tempfile.TemporaryDirectory(prefix="dusk-headless-") as directory:
        root = Path(directory)
        if args.archive:
            expected_machine = {"chord-analyzer-headless-linux.zip": 62,
                                "chord-analyzer-headless-linux-arm64.zip": 183}.get(args.archive.name)
            expected = {f"LV2/{BUNDLE_NAME}/{name}" for name in BUNDLE_FILES} | {"LICENSE", "README.md", "THIRD_PARTY_LICENSES.md"}
            with zipfile.ZipFile(args.archive) as archive:
                require(len(archive.namelist()) == len(expected) and set(archive.namelist()) == expected, "archive contains only the standalone LV2, installation guide and licenses")
                require(all(not ((info.external_attr >> 16) & 0o170000) == 0o120000 for info in archive.infolist()), "archive contains no symbolic links")
                archive.extractall(root)
            require("GNU GENERAL PUBLIC LICENSE" in (root / "LICENSE").read_text(), "archive includes the repository GPL license")
            require("ISC" in (root / "THIRD_PARTY_LICENSES.md").read_text(), "archive credits the LV2 SDK")
            require("Chord Analyzer Headless" in (root / "README.md").read_text(), "archive includes the headless installation guide")
            bundle = root / "LV2" / BUNDLE_NAME
        else:
            bundle = root / args.bundle.name
            shutil.copytree(args.bundle, bundle, symlinks=True)
        inspect(bundle, expected_machine)


if __name__ == "__main__":
    main()
