"""Oracle factory voicing and chord headroom with the wheel down.

Single notes at A2 and C5, velocity 100, check the oscillator register or the
release level relative to the held note in the same render. Reference captures
at both pitches gave -15.2/-15.9 dB for the horns and -0.3/+0.3 dB for the bells.
The late bell window checks that the long initial ring eventually dies away.
Upper-register chords exercise factory headroom where key tracking opens the filter.
"""
import numpy as np
from scipy.signal import resample_poly

from preset_audit import PEAK_CLIP_DBFS, SILENCE_DBFS, db, render


def check(program, note):
    rate, audio, _, _ = render(program, perf="single", note=note, vel=100 / 127,
                               bars=1, tempo=369.230769, tail=12.5 if program == 7 else 2.5)
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
    ok = abs(level - target) <= 6
    detail = f"{level:+.1f} dB relative to held note (target {target:+.1f})"
    if program == 7:
        # Key-up is at 0.65 s. A lost note-off also passes the early ring check.
        late = audio[round(12.65 * rate):round(12.90 * rate)]
        late_level = 10 * np.log10(max(np.mean(late ** 2) / power, 1e-15))
        ok = ok and late_level <= -30
        detail += f", {late_level:+.1f} dB 12 s after key-up (limit -30 dB)"
    return ok, detail


def check_chord_headroom(program, **performance):
    _, audio, _, _ = render(program, bars=1, tail=0.5, **performance)
    if not np.isfinite(audio).all() or db(np.sqrt(np.mean(audio ** 2))) <= SILENCE_DBFS:
        return False, "silent or invalid chord"
    # Check both the actual samples and the peaks between them.
    upsampled = resample_poly(audio, 4, 1, axis=0, window=("kaiser", 8))
    peak = db(max(np.max(np.abs(audio)), np.max(np.abs(upsampled))))
    return peak <= PEAK_CLIP_DBFS, f"{peak:+.2f} dBTP (limit {PEAK_CLIP_DBFS:g})"


if __name__ == "__main__":
    passed = True
    for program, name in ((22, "Poly Brass"), (5, "Brass Section"),
                          (7, "Poly Mod Bells"), (23, "Glass Bells")):
        for note in (45, 72):
            ok, detail = check(program, note)
            print(f"{'PASS' if ok else 'FAIL'} {name}, MIDI {note}: {detail}")
            passed = passed and ok

    for program, name, root, high_root in ((22, "Poly Brass", 82, 82),
                                           (7, "Poly Mod Bells", 90, 106)):
        ninth = ",".join(str(high_root + interval) for interval in (0, 4, 7, 11, 14))
        for label, performance in (
                (f"chord rooted at MIDI {root}", dict(perf="chord", note=root, vel=100 / 127)),
                (f"five held notes {ninth}", dict(perf="hold", hold=ninth, vel=1))):
            ok, detail = check_chord_headroom(program, **performance)
            print(f"{'PASS' if ok else 'FAIL'} {name}, {label}: {detail}")
            passed = passed and ok
    raise SystemExit(0 if passed else 1)
