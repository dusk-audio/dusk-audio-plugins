#!/usr/bin/env python3
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
"""#148: parity must reject missing, silent or non-finite measurement data."""
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import unittest


def wav(path, samples):
    data = struct.pack(f"<{len(samples)}f", *samples)
    fmt = struct.pack("<HHIIHH", 3, 2, 48000, 48000 * 8, 8, 32)
    path.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt "
                     + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data)


class NullValidity(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        rng = random.Random(18371)
        self.signal = [rng.uniform(-0.2, 0.2) for _ in range(48000 * 2)]
        self.tool = pathlib.Path(__file__).with_name("ab_null.py")

    def compare(self, first, second, *args):
        a, b = self.root / "a.wav", self.root / "b.wav"
        wav(a, first); wav(b, second)
        return subprocess.run([sys.executable, str(self.tool), str(a), str(b), *args],
                              text=True, capture_output=True)

    def accepted(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def rejected(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_identical_excited_audio(self):
        self.accepted(self.compare(self.signal, self.signal))

    def test_real_difference(self):
        changed = self.signal.copy(); changed[60000] += 0.1
        self.rejected(self.compare(self.signal, changed))

    def test_nonfinite_audio(self):
        changed = self.signal.copy(); changed[60000] = float("nan")
        self.rejected(self.compare(self.signal, changed))

    def test_silent_pair(self):
        silent = [0.0] * len(self.signal)
        self.rejected(self.compare(silent, silent))

    def test_no_settled_overlap(self):
        self.rejected(self.compare(self.signal, self.signal, "--skip-start", "2"))

    def test_no_settled_spectrum(self):
        self.rejected(self.compare(self.signal, self.signal, "--spectral", "--skip-start", "2"))

    def test_too_few_spectral_samples(self):
        self.rejected(self.compare(self.signal, self.signal, "--spectral", "--skip-start", "0.99"))

    def test_one_silent_channel(self):
        routed = self.signal.copy(); routed[1::2] = [0.0] * (len(routed) // 2)
        self.accepted(self.compare(routed, routed))

    def test_right_only_delay(self):
        routed = self.signal.copy(); routed[0::2] = [0.0] * (len(routed) // 2)
        delayed = [0.0] * 96 + routed[:-96]
        self.accepted(self.compare(routed, delayed))

    def test_spectral_difference_on_right(self):
        changed = self.signal.copy(); changed[1::2] = [2.0 * x for x in changed[1::2]]
        self.rejected(self.compare(self.signal, changed, "--spectral"))

    def test_spectral_alignment(self):
        delayed = [0.0] * 96 + self.signal[:-96]
        self.accepted(self.compare(self.signal, delayed, "--spectral", "--thresh-db", "0.000001"))

    def test_bit_exact_rejects_one_ulp(self):
        changed = self.signal.copy()
        bits = struct.unpack("<I", struct.pack("<f", changed[60000]))[0]
        changed[60000] = struct.unpack("<f", struct.pack("<I", bits ^ 1))[0]
        self.accepted(self.compare(self.signal, changed))  # below the numerical tolerance
        self.rejected(self.compare(self.signal, changed, "--bit-exact"))

    def test_bit_exact_distinguishes_signed_zero(self):
        first = self.signal.copy(); first[60000] = 0.0
        second = first.copy(); second[60000] = -0.0
        self.rejected(self.compare(first, second, "--bit-exact"))


if __name__ == "__main__":
    unittest.main()
