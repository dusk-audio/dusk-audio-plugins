"""Factory-program recall at A2 and C5, with the wheel down and velocity 100.

Two pitch anchors distinguish the intended oscillator registers. The two release
anchors compare the tail to the held note in the same render. Reference captures
at both pitches gave -15.2/-15.9 dB for the horns and -0.3/+0.3 dB for the bells.
Allow 1% on frequency and 6 dB on the release-to-sustain level difference.
"""
import subprocess
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf


RENDER = Path(__file__).resolve().parents[1] / "build" / "preset_render"


def check(program, note, wav):
    subprocess.run([str(RENDER), str(program), str(wav), "perf=single",
                    f"note={note}", "vel=0.787401575", "bars=1",
                    "tempo=369.230769", "tail=2.5"], check=True,
                   capture_output=True, timeout=30)
    audio, rate = sf.read(wav, always_2d=True)
    held = audio[round(.32 * rate):round(.62 * rate)]
    power = np.mean(held ** 2)
    if not np.isfinite(audio).all() or power < 1e-8:
        return False, "silent or invalid held note"

    if program in (22, 23):
        window = audio[round(.16 * rate):round(.45 * rate)]
        spectrum = np.fft.rfft(window * np.hanning(len(window))[:, None],
                               n=65536, axis=0)
        frequency = np.argmax(np.sum(abs(spectrum) ** 2, axis=1)) * rate / 65536
        root = 440 * 2 ** ((note - 69) / 12)
        target = root * (0.5 if program == 22 else 2)
        return abs(frequency / target - 1) <= .01, f"{frequency:.1f} Hz (target {target:.1f})"

    start, stop, target = (.90, 1.15, -15.5) if program == 5 else (2.50, 2.75, 0.0)
    tail = audio[round(start * rate):round(stop * rate)]
    level = 10 * np.log10(max(np.mean(tail ** 2) / power, 1e-15))
    return abs(level - target) <= 6, f"{level:+.1f} dB relative to held note (target {target:+.1f})"


if __name__ == "__main__":
    passed = True
    with tempfile.TemporaryDirectory() as directory:
        for program, name in ((22, "Poly Brass"), (5, "Brass Section"),
                              (7, "Poly Mod Bells"), (23, "Glass Bells")):
            for note in (45, 72):
                ok, detail = check(program, note, Path(directory) / "note.wav")
                print(f"{'PASS' if ok else 'FAIL'} {name}, MIDI {note}: {detail}")
                passed = passed and ok
    raise SystemExit(0 if passed else 1)
