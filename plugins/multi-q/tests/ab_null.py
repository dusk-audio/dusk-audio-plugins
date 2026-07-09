#!/usr/bin/env python3
# ab_null.py — null-compare two float32 WAV renders (JUCE vs DPF Multi-Q).
# Cross-correlates channel 0 to remove any reported-latency offset, aligns, then
# reports max-abs and RMS sample difference in dBFS. Exit 0 if under threshold.
#
#   ab_null.py JUCE.wav DPF.wav [--thresh-db -120] [--max-shift 4096] [--label X]
#
# Threshold guidance: bit-identical DSP nulls near the float floor (< -140 dB).
# 1x transparent paths should pass -120 dB. Oversampled (2x/4x) paths use a
# looser, reporting-only threshold — the two frameworks' halfband filters differ.
import sys, struct, math

def read_wav_f32(path):
    # Minimal RIFF/WAVE reader — Python's `wave` module rejects IEEE-float
    # (fmt tag 3), which is what clap_ab_host writes.
    with open(path, 'rb') as f:
        buf = f.read()
    if buf[0:4] != b'RIFF' or buf[8:12] != b'WAVE':
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    ch = sr = sw = fmt = None
    data = None
    pos = 12
    while pos + 8 <= len(buf):
        cid = buf[pos:pos+4]
        clen = struct.unpack('<I', buf[pos+4:pos+8])[0]
        body = buf[pos+8:pos+8+clen]
        if cid == b'fmt ':
            fmt, ch, sr, _br, _ba, bits = struct.unpack('<HHIIHH', body[:16])
            sw = bits // 8
        elif cid == b'data':
            data = body
        pos += 8 + clen + (clen & 1)
    if fmt != 3 or sw != 4:
        raise SystemExit(f"{path}: expected float32 (fmt=3, 32-bit), got fmt={fmt} bits={sw*8}")
    samples = struct.unpack('<%df' % (len(data)//4), data)
    chans = [list(samples[c::ch]) for c in range(ch)]
    return sr, ch, chans

def rms(xs):
    if not xs: return 0.0
    return math.sqrt(sum(x*x for x in xs)/len(xs))

def db(x):
    return -999.0 if x <= 1e-12 else 20.0*math.log10(x)

def best_shift(a, b, max_shift):
    # integer lag of b vs a maximising cross-correlation, via FFT. Positive lag
    # means b is delayed relative to a. An exact integer lag matters: a smeared
    # lag inflates the time-domain residual.
    import numpy as np
    x = np.asarray(a, dtype=np.float64); y = np.asarray(b, dtype=np.float64)
    N = min(len(x), len(y)); x = x[:N]; y = y[:N]
    n = 1 << (2*N - 1).bit_length()
    corr = np.fft.irfft(np.fft.rfft(x, n) * np.conj(np.fft.rfft(y, n)), n)
    # lags 0..max_shift live at the front, negative lags wrap to the tail
    lags = np.concatenate([np.arange(0, max_shift+1), np.arange(-max_shift, 0)])
    idx = np.concatenate([np.arange(0, max_shift+1), n + np.arange(-max_shift, 0)])
    peak = int(lags[int(np.argmax(corr[idx]))])
    # caller aligns as a[i] - b[i+lag]; correlation peak is +delay of a vs b, so
    # the aligning shift is its negation.
    return -peak

def main():
    args = sys.argv[1:]
    if len(args) < 2:
        raise SystemExit("usage: ab_null.py JUCE.wav DPF.wav [--thresh-db N] [--max-shift N] [--label X]")
    jpath, dpath = args[0], args[1]
    thresh, max_shift, label, skip = -120.0, 4096, "", 0.25
    spectral = False
    i = 2
    while i < len(args):
        if args[i] == '--thresh-db': thresh = float(args[i+1]); i += 2
        elif args[i] == '--max-shift': max_shift = int(args[i+1]); i += 2
        elif args[i] == '--label': label = args[i+1]; i += 2
        elif args[i] == '--skip-start': skip = float(args[i+1]); i += 2
        elif args[i] == '--spectral': spectral = True; i += 1
        else: i += 1
    # In spectral mode the threshold is a magnitude error in dB (not dBFS).
    if spectral and thresh == -120.0: thresh = 0.1

    sr_j, ch_j, cj = read_wav_f32(jpath)
    sr_d, ch_d, cd = read_wav_f32(dpath)
    if sr_j != sr_d or ch_j != ch_d:
        raise SystemExit(f"format mismatch: {sr_j}Hz/{ch_j}ch vs {sr_d}Hz/{ch_d}ch")

    lag = best_shift(cj[0], cd[0], max_shift)
    skip_smp = int(skip * sr_j)  # ignore initial settling (param smoothing ramps)
    import numpy as np
    worst_max, worst_rms = 0.0, 0.0
    for c in range(ch_j):
        a = np.asarray(cj[c], dtype=np.float64)
        b = np.asarray(cd[c], dtype=np.float64)
        N = min(len(a), len(b))
        # align b to a by lag, restrict to the overlapping settled region
        lo = max(skip_smp, -lag); hi = min(N, N - lag)
        d = a[lo:hi] - b[lo+lag:hi+lag]
        if d.size:
            worst_max = max(worst_max, float(np.max(np.abs(d))))
            worst_rms = max(worst_rms, float(np.sqrt(np.mean(d*d))))

    tag = f"[{label}] " if label else ""

    if spectral:
        # Magnitude-response comparison — the correct tool when the two chains
        # have different (possibly fractional) group delay, e.g. oversampled
        # paths whose halfband filters differ. Time-domain subtraction there is
        # dominated by sub-sample phase, not by any audible magnitude error.
        import numpy as np
        a = np.asarray(cj[0][skip_smp:], dtype=np.float64)
        b = np.asarray(cd[0][skip_smp:], dtype=np.float64)
        N = min(len(a), len(b)); a = a[:N]; b = b[:N]
        nfft = 1 << 13
        win = np.hanning(nfft)
        def avg_mag(x):
            acc = np.zeros(nfft//2+1); k = 0
            for s in range(0, N-nfft, nfft//4):
                acc += np.abs(np.fft.rfft(x[s:s+nfft]*win)); k += 1
            return acc/max(k,1)
        ma, mb = avg_mag(a), avg_mag(b)
        freqs = np.fft.rfftfreq(nfft, 1.0/sr_j)
        band = (freqs >= 20) & (freqs <= 20000)
        eps = 1e-12
        ddb = np.abs(20*np.log10((ma[band]+eps)/(mb[band]+eps)))
        # Gate on the MEDIAN magnitude error: with a random-noise probe single
        # bins have high Welch variance, so max|Δmag| is a noisy outlier, not a
        # real audible error. Median answers "is this the same EQ curve?".
        med = float(np.median(ddb))
        p95 = float(np.percentile(ddb, 95))
        worst = float(np.max(ddb)); wf = float(freqs[band][int(np.argmax(ddb))])
        print(f"{tag}spectral 20Hz-20kHz  median|Δmag|={med:.3f} dB  p95={p95:.3f} dB  max={worst:.2f} dB @ {wf:.0f}Hz  (thresh median {thresh:.2f} dB)")
        ok = med <= thresh
        print(f"{tag}{'PASS' if ok else 'FAIL'}")
        return 0 if ok else 1

    print(f"{tag}lag={lag} smp  max_diff={db(worst_max):.1f} dBFS  rms_diff={db(worst_rms):.1f} dBFS  (thresh {thresh:.0f})")
    ok = db(worst_max) <= thresh
    print(f"{tag}{'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
