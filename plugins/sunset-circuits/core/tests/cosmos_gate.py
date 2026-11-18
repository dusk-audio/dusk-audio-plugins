#!/usr/bin/env python3
"""Cosmos DCO/filter/chorus regression checks; no proprietary plugin required.

These guard measured topology defects, not a claim of full hardware parity.
Isolated controls keep silence, stereo width, and mere level changes from
passing in place of the intended behaviour.
"""
import sys
import numpy as np
from scipy.signal import butter, hilbert, sosfiltfilt, welch
from _harness import render, peak_hz, cents

BASE = dict(sr=48000, analogAmt=0, vintage=0, masterVol=-24, arpOn=0,
            driveOn=0, chorusOn=0, delayOn=0, reverbOn=0, cosmosChorus=0,
            osc1Wave=0, osc1Level=1, osc2Level=0, subLevel=0, noiseLevel=0,
            ampA=.001, ampD=.002, ampS=1, ampR=.002, filterCutoff=20000,
            filterRes=0, filterEnvAmt=0, filterHP=20, unisonVoices=1,
            osc1Detune=0, osc2PW=.5, crossMod=0)
fails = []
checks = 0


def check(name, ok, value):
    global checks
    checks += 1
    print(f"{'PASS' if ok else 'FAIL'} {name}: {value}", flush=True)
    if not ok:
        fails.append(name)


def rms(x):
    return float(np.sqrt(np.mean(x*x)))


def db(x):
    return float(20*np.log10(max(x, 1e-30)))


def take(name, note=69, seconds=2, osfactor=2, **patch):
    params = dict(BASE, **patch)
    sr, x = render(0, note, seconds, osfactor, name,
                   **dict(params, release=seconds-.1))
    if not np.all(np.isfinite(x)):
        raise AssertionError(f'{name}: non-finite samples')
    return sr, x


def late(x, sr):
    return x[int(.5*sr):-int(.2*sr)]


def main():
    for note in (45, 69, 81):
        sr, saw = take(f'cosmos_saw_{note}', note=note)
        _, pulse = take(f'cosmos_pulse_{note}', note=note, osc1Level=0, osc2Level=1)
        _, both = take(f'cosmos_sum_{note}', note=note, osc2Level=1)
        _, sub = take(f'cosmos_sub_{note}', note=note, osc1Level=0, subLevel=1)
        levels = [rms(late(x, sr)) for x in (saw, pulse, both, sub)]
        gain = db(levels[2]/levels[0])
        check(f'saw+pulse additive note {note}', 6 < gain < 8.6, f'{gain:.3f} dB vs saw')
        pulse_gain = db(levels[1]/levels[0])
        check(f'pulse balance note {note}', 2.5 < pulse_gain < 4.3, f'{pulse_gain:.3f} dB vs saw')
        sub_gain = db(levels[3]/levels[0])
        check(f'sub balance note {note}', .4 < sub_gain < 3.5, f'{sub_gain:.3f} dB vs saw')

    for sr in (44100, 48000, 96000):
        for osfactor in (1, 2, 4):
            _, dry = take(f'cosmos_sub_dry_{sr}_{osfactor}', sr=sr, osfactor=osfactor,
                          osc1Level=0, subLevel=1)
            _, mod = take(f'cosmos_sub_mod_{sr}_{osfactor}', sr=sr, osfactor=osfactor,
                          osc1Level=0, subLevel=1, filtS=1,
                          modSrc0=3, modDst0=1, modAmt0=.5)
            # Filter-envelope source at sustain=1: +1 semitone on the DCO.
            f0 = peak_hz(late(dry, sr)[:, 0], sr)
            f1 = peak_hz(late(mod, sr)[:, 0], sr)
            shift = cents(f1, f0)
            check(f'sub follows DCO pitch {sr}/{osfactor}x', abs(shift-100) < 1,
                  f'{shift:.3f} cents')
    sr, detuned = take('cosmos_sub_detune', osc1Level=0, subLevel=1, osc1Detune=37)
    shift = cents(peak_hz(late(detuned, sr)[:, 0], sr), 220)
    check('sub follows DCO detune', abs(shift-37) < 1, f'{shift:.3f} cents')

    sr, noise = take('cosmos_white', seconds=4, osc1Level=0, noiseLevel=1)
    f, power = welch(late(noise, sr).mean(axis=1), sr, nperseg=16384)
    bands = [np.sum(power[(f >= a) & (f < 2*a)]) for a in (250, 500, 1000, 2000)]
    slope = 10*np.log10(bands[-1]/bands[0])/3
    check('white noise octave slope', 2.3 < slope < 3.7, f'{slope:.3f} dB/octave')

    # Equal noise density after decimation, not equal unfiltered RNG amplitude.
    noise_levels = []
    for sr in (44100, 96000):
        for osfactor in (1, 4):
            _, x = take(f'cosmos_noise_rate_{sr}_{osfactor}', sr=sr, osfactor=osfactor,
                        seconds=3, osc1Level=0, noiseLevel=.1, filterCutoff=4000)
            f, p = welch(late(x, sr).mean(axis=1), sr, nperseg=16384)
            noise_levels.append(10*np.log10(np.sum(p[(f > 500) & (f < 2000)]) * sr/16384))
    spread = np.ptp(noise_levels)
    check('white noise density across rates', spread < 1, f'{spread:.3f} dB spread')

    selfosc_levels = []
    for sr in (44100, 96000):
        for osfactor in (1, 4):
            for res in (0, 1):
                _, x = take(f'cosmos_selfosc_{sr}_{osfactor}_{res}', sr=sr,
                            osfactor=osfactor, seconds=3, osc1Level=0,
                            filterRes=res, filterCutoff=1000)
                tail = x[sr:2*sr, 0]
                level = db(rms(tail))
                if res == 1:
                    selfosc_levels.append(level)
                freq = peak_hz(tail, sr)
                ok = (level < -140 if res == 0 else
                      -65 < level < -10 and abs(cents(freq, 1000)) < 100)
                check(f'filter selfosc/control {sr}/{osfactor}x res{res}', ok,
                      f'{level:.3f} dBFS at {freq:.2f} Hz')
    spread = np.ptp(selfosc_levels)
    check('selfosc level across rates', spread < .25, f'{spread:.3f} dB spread')

    dry_level = None
    for mode in range(4):
        sr, x = take(f'cosmos_chorus_{mode}', seconds=8, cosmosChorus=mode)
        seg = x[sr:7*sr]
        mid = seg.mean(axis=1)
        narrow = sosfiltfilt(butter(3, [400, 480], btype='bandpass', fs=sr,
                                   output='sos'), mid)
        env = np.abs(hilbert(narrow))[sr//2:-sr//2]
        swing = (np.percentile(env, 95)-np.percentile(env, 5))/np.mean(env)
        if mode == 0:
            dry_level = rms(seg)
            check('chorus off control', swing < .01, f'mono swing {swing:.5f}')
        else:
            gain = db(rms(seg)/dry_level)
            corr = np.corrcoef(seg.T)[0, 1]
            check(f'chorus {mode} wet survives mono', .3 < swing < 3,
                  f'mono swing {swing:.4f}')
            check(f'chorus {mode} stereo/gain', abs(corr) < .9 and 1 < gain < 9,
                  f'correlation {corr:.4f}, gain {gain:.3f} dB')

    print(f'cosmos_gate: {checks} checks, {len(fails)} failures')
    return bool(fails)


if __name__ == '__main__':
    sys.exit(main())
