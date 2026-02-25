#!/usr/bin/env python3
"""Diagnose pink noise compression: run JUST the pink noise signal through MC
   to determine if the over-compression is steady-state or carry-over from hot tests."""

import numpy as np
import soundfile as sf
import pedalboard
import os
import glob

VST3_PATH = os.path.expanduser("~/Library/Audio/Plug-Ins/VST3/Multi-Comp.vst3")
DRY_DIR = os.path.join(os.path.dirname(__file__), "test_signals")
CAPTURED_DIR = os.environ.get("CAPTURED_DIR",
    os.path.join(os.path.dirname(__file__), "captured"))
LA2A_PATH = os.path.join(CAPTURED_DIR, "LA2A_1.wav")

def load_mono(path):
    data, sr = sf.read(path)
    if data.ndim > 1:
        data = data[:, 0]
    return data, sr

def rms_db(x):
    r = np.sqrt(np.mean(x**2))
    return 20 * np.log10(r + 1e-12)

def setup_plugin(plugin, pr, gain):
    """Set opto mode with given PR and Gain"""
    plugin.reset()
    plugin.mode = "Vintage Opto"
    plugin.oversampling = "2x"
    plugin.analog_noise = True
    plugin.mix = 100.0
    plugin.stereo_link = 100.0
    plugin.bypass = False
    plugin.limit_mode = False
    plugin.sc_listen = False
    plugin.external_sidechain = False
    plugin.auto_makeup = False
    plugin.distortion = "Off"
    plugin.input = 0.0
    plugin.output = 0.0
    plugin.peak_reduction = pr
    plugin.gain = gain

def main():
    # Load just the pink noise test signal (test 07)
    pink_noise_file = os.path.join(DRY_DIR, "07_pink_noise.wav")
    if not os.path.exists(pink_noise_file):
        print(f"  Pink noise file not found: {pink_noise_file}")
        # Try to extract from combined file
        files = sorted(glob.glob(os.path.join(DRY_DIR, "*.wav")))
        print(f"  Available files: {[os.path.basename(f) for f in files]}")
        return

    pink_dry, sr = load_mono(pink_noise_file)
    print(f"  Pink noise: {len(pink_dry)/sr:.1f}s, RMS={rms_db(pink_dry):.1f} dB")

    # Also load a hot signal to pre-condition the compressor
    thd_file = os.path.join(DRY_DIR, "06_thd.wav")
    has_thd = os.path.exists(thd_file)
    if has_thd:
        thd_dry, thd_sr = load_mono(thd_file)
        if thd_sr != sr:
            raise ValueError(f"THD signal sample rate mismatch: expected {sr}, got {thd_sr}")
        print(f"  THD signal: {len(thd_dry)/sr:.1f}s, RMS={rms_db(thd_dry):.1f} dB")

    # Load plugin
    plugin = pedalboard.load_plugin(VST3_PATH)

    # Use calibrated values from auto_compare
    PR = 43.4
    GAIN = 53.7

    # Test -1: Digital mode baseline (no hardware emulation, no compression)
    # Use threshold=0 dB (maximum, no compression engages) to get transparent passthrough
    silence = np.zeros(int(2.0 * sr), dtype=np.float32)
    tone_dur = 2.0
    t = np.arange(int(sr * tone_dur)) / sr

    def setup_digital_passthrough(p):
        p.reset()
        p.mode = "Digital"
        p.oversampling = "2x"
        p.analog_noise = False
        p.mix = 100.0
        p.bypass = False
        p.auto_makeup = False
        p.distortion = "Off"
        p.input = 0.0
        p.output = 0.0
        p.threshold = 0.0     # 0 dB threshold = max, nothing compresses
        p.ratio = "4:1"       # Doesn't matter — threshold is 0 dB so nothing triggers

    # Tone through Digital mode
    tone = (0.126 * np.sin(2 * np.pi * 1000 * t)).astype(np.float32)
    setup_digital_passthrough(plugin)
    tone_signal_d = np.concatenate([silence, tone])
    result_tone_d = plugin(np.stack([tone_signal_d, tone_signal_d]), sr)[0]
    tone_out_d = result_tone_d[len(silence):]
    gain_tone_d = rms_db(tone_out_d) - rms_db(tone)

    # Pink noise through Digital mode
    setup_digital_passthrough(plugin)
    test_signal_d = np.concatenate([silence, pink_dry.astype(np.float32)])
    result_d = plugin(np.stack([test_signal_d, test_signal_d]), sr)[0]
    pink_d = result_d[len(silence):]
    gain_pink_d = rms_db(pink_d) - rms_db(pink_dry)

    # White noise through Digital mode
    white_d = np.random.randn(int(sr * tone_dur)).astype(np.float32)
    white_d *= 10**(rms_db(pink_dry)/20) / (np.sqrt(np.mean(white_d**2)) + 1e-12)
    setup_digital_passthrough(plugin)
    wn_signal_d = np.concatenate([silence, white_d])
    wn_result_d = plugin(np.stack([wn_signal_d, wn_signal_d]), sr)[0]
    wn_out_d = wn_result_d[len(silence):]
    gain_wn_d = rms_db(wn_out_d) - rms_db(white_d)

    print(f"\n  Test -1 — Digital mode (no hardware emulation, no compression):")
    print(f"    Tone gain:        {gain_tone_d:+.3f} dB")
    print(f"    White noise gain: {gain_wn_d:+.3f} dB")
    print(f"    Pink noise gain:  {gain_pink_d:+.3f} dB")
    print(f"    Tone vs pink diff: {gain_tone_d - gain_pink_d:+.3f} dB")
    print(f"    White vs pink diff: {gain_wn_d - gain_pink_d:+.3f} dB")
    print(f"    → If tone-pink diff >0.1: issue is in oversampling/pedalboard")
    print(f"    → If ~0: issue is Opto-specific")

    # Also test Digital mode without oversampling
    def setup_digital_no_os(p):
        setup_digital_passthrough(p)
        p.oversampling = "Off"

    setup_digital_no_os(plugin)
    result_tone_d_no = plugin(np.stack([tone_signal_d, tone_signal_d]), sr)[0]
    tone_out_d_no = result_tone_d_no[len(silence):]
    gain_tone_d_no = rms_db(tone_out_d_no) - rms_db(tone)

    setup_digital_no_os(plugin)
    result_d_no = plugin(np.stack([test_signal_d, test_signal_d]), sr)[0]
    pink_d_no = result_d_no[len(silence):]
    gain_pink_d_no = rms_db(pink_d_no) - rms_db(pink_dry)

    print(f"\n  Test -1b — Digital mode, NO oversampling:")
    print(f"    Tone gain:       {gain_tone_d_no:+.3f} dB")
    print(f"    Pink noise gain: {gain_pink_d_no:+.3f} dB")
    print(f"    Tone vs pink diff: {gain_tone_d_no - gain_pink_d_no:+.3f} dB")
    print(f"    OS contribution to gap: {(gain_tone_d - gain_pink_d) - (gain_tone_d_no - gain_pink_d_no):+.3f} dB")

    # Test 0: PR=0 baseline (NO compression, just tube+transformer+makeup)
    setup_plugin(plugin, 0.0, GAIN)
    silence = np.zeros(int(2.0 * sr), dtype=np.float32)
    test_signal = np.concatenate([silence, pink_dry.astype(np.float32)])
    result = plugin(np.stack([test_signal, test_signal]), sr)[0]
    pink_pr0 = result[len(silence):]
    gain_pr0 = rms_db(pink_pr0) - rms_db(pink_dry)
    expected_makeup = (GAIN - 50) * 0.8  # dB
    print(f"\n  Test 0 — PR=0 (no compression), Gain={GAIN}:")
    print(f"    Expected makeup gain: {expected_makeup:+.1f} dB")
    print(f"    Actual gain on pink noise: {gain_pr0:+.1f} dB")
    print(f"    Hardware chain effect: {gain_pr0 - expected_makeup:+.1f} dB")

    # Also test PR=0 with a -18dB 1kHz tone for comparison
    tone_dur = 2.0
    t = np.arange(int(sr * tone_dur)) / sr
    tone = (0.126 * np.sin(2 * np.pi * 1000 * t)).astype(np.float32)  # -18dB
    setup_plugin(plugin, 0.0, GAIN)
    tone_signal = np.concatenate([silence, tone])
    result_tone = plugin(np.stack([tone_signal, tone_signal]), sr)[0]
    tone_out = result_tone[len(silence):]
    gain_tone_pr0 = rms_db(tone_out) - rms_db(tone)
    print(f"\n  Test 0b — PR=0, -18dB 1kHz tone:")
    print(f"    Actual gain on tone: {gain_tone_pr0:+.1f} dB")
    print(f"    Tone vs noise diff: {gain_tone_pr0 - gain_pr0:+.1f} dB")

    # Test 0d: Oversampling OFF — check if oversampling filters cause the gap
    setup_plugin(plugin, 0.0, GAIN)
    plugin.oversampling = "Off"
    result_no_os = plugin(np.stack([test_signal, test_signal]), sr)[0]
    pink_no_os = result_no_os[len(silence):]
    gain_no_os = rms_db(pink_no_os) - rms_db(pink_dry)

    setup_plugin(plugin, 0.0, GAIN)
    plugin.oversampling = "Off"
    result_tone_no_os = plugin(np.stack([tone_signal, tone_signal]), sr)[0]
    tone_no_os = result_tone_no_os[len(silence):]
    gain_tone_no_os = rms_db(tone_no_os) - rms_db(tone)

    print(f"\n  Test 0d — Oversampling OFF:")
    print(f"    Tone gain: {gain_tone_no_os:+.1f} dB, Noise gain: {gain_no_os:+.1f} dB")
    print(f"    Tone vs noise diff (no OS): {gain_tone_no_os - gain_no_os:+.1f} dB")
    print(f"    OS contribution to gap: {(gain_tone_pr0 - gain_pr0) - (gain_tone_no_os - gain_no_os):+.1f} dB")

    # Test 0e: Frequency sweep — measure gain at different frequencies
    print(f"\n  Test 0e — Frequency sweep (PR=0, Gain={GAIN}, 2x OS):")
    freqs = [50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 20000]
    freq_gains = []
    for freq in freqs:
        setup_plugin(plugin, 0.0, GAIN)
        t_tone = np.arange(int(sr * tone_dur)) / sr
        f_tone = (0.126 * np.sin(2 * np.pi * freq * t_tone)).astype(np.float32)
        f_signal = np.concatenate([silence, f_tone])
        f_result = plugin(np.stack([f_signal, f_signal]), sr)[0]
        f_out = f_result[len(silence):]
        f_gain = rms_db(f_out) - rms_db(f_tone)
        freq_gains.append(f_gain)
        print(f"    {freq:6d} Hz: {f_gain:+.2f} dB")
    print(f"    Spread: {max(freq_gains) - min(freq_gains):.2f} dB")
    print(f"    Pink noise: {gain_pr0:+.2f} dB")

    # Test 0f: Multitone — sum of tones (broadband-like) vs individual
    print(f"\n  Test 0f — Multitone test:")
    multitone_freqs = [100, 300, 1000, 3000, 10000]
    t_mt = np.arange(int(sr * tone_dur)) / sr
    # Generate sum of tones with equal amplitude, scaled to match pink noise RMS
    multi = np.zeros(len(t_mt), dtype=np.float32)
    for f in multitone_freqs:
        multi += np.sin(2 * np.pi * f * t_mt).astype(np.float32)
    multi_amp = 10**(rms_db(pink_dry)/20) / (np.sqrt(np.mean(multi**2)) + 1e-12)
    multi *= multi_amp  # Same RMS as pink noise
    print(f"    Multitone RMS: {rms_db(multi):.1f} dB, Peak: {20*np.log10(np.max(np.abs(multi))+1e-12):.1f} dBFS")
    print(f"    Pink noise RMS: {rms_db(pink_dry):.1f} dB, Peak: {20*np.log10(np.max(np.abs(pink_dry))+1e-12):.1f} dBFS")
    setup_plugin(plugin, 0.0, GAIN)
    mt_signal = np.concatenate([silence, multi])
    mt_result = plugin(np.stack([mt_signal, mt_signal]), sr)[0]
    mt_out = mt_result[len(silence):]
    gain_mt = rms_db(mt_out) - rms_db(multi)
    print(f"    Multitone gain: {gain_mt:+.2f} dB")
    print(f"    Noise gain:     {gain_pr0:+.2f} dB")
    print(f"    Multi vs noise: {gain_mt - gain_pr0:+.2f} dB")

    # Test 0g: White noise (for comparison)
    white = np.random.randn(len(t_mt)).astype(np.float32)
    white *= 10**(rms_db(pink_dry)/20) / (np.sqrt(np.mean(white**2)) + 1e-12)
    print(f"\n  Test 0g — White noise:")
    print(f"    White RMS: {rms_db(white):.1f} dB, Peak: {20*np.log10(np.max(np.abs(white))+1e-12):.1f} dBFS")
    setup_plugin(plugin, 0.0, GAIN)
    wn_signal = np.concatenate([silence, white])
    wn_result = plugin(np.stack([wn_signal, wn_signal]), sr)[0]
    wn_out = wn_result[len(silence):]
    gain_wn = rms_db(wn_out) - rms_db(white)
    print(f"    White noise gain: {gain_wn:+.2f} dB")
    print(f"    Pink noise gain:  {gain_pr0:+.2f} dB")
    print(f"    White vs pink:    {gain_wn - gain_pr0:+.2f} dB")

    # Test 0c: PR=0 with a QUIET tone at same RMS level as pink noise
    quiet_amp = 10**(rms_db(pink_dry)/20) * np.sqrt(2)  # match pink noise RMS
    quiet_tone = (quiet_amp * np.sin(2 * np.pi * 1000 * t)).astype(np.float32)
    setup_plugin(plugin, 0.0, GAIN)
    qt_signal = np.concatenate([silence, quiet_tone])
    result_qt = plugin(np.stack([qt_signal, qt_signal]), sr)[0]
    qt_out = result_qt[len(silence):]
    gain_qt = rms_db(qt_out) - rms_db(quiet_tone)
    print(f"\n  Test 0c — PR=0, {rms_db(quiet_tone):.1f}dB 1kHz tone (same RMS as pink):")
    print(f"    Actual gain on quiet tone: {gain_qt:+.1f} dB")
    print(f"    Quiet tone vs noise diff: {gain_qt - gain_pr0:+.1f} dB")
    print(f"    → If ~0: level-dependent; If >0: spectrum-dependent")

    # Test 1: Pink noise in isolation (fresh plugin state)
    setup_plugin(plugin, PR, GAIN)
    test_signal = np.concatenate([silence, pink_dry.astype(np.float32)])
    result = plugin(np.stack([test_signal, test_signal]), sr)[0]
    pink_isolated = result[len(silence):]
    gain_isolated = rms_db(pink_isolated) - rms_db(pink_dry)
    print(f"\n  Test 1 — Pink noise in ISOLATION (PR={PR}, fresh state):")
    print(f"    Output RMS: {rms_db(pink_isolated):.1f} dB")
    print(f"    Gain: {gain_isolated:+.1f} dB")
    print(f"    Compression: {gain_pr0 - gain_isolated:+.1f} dB (relative to PR=0)")

    # Test 2: Pink noise after hot signal (carry-over state)
    gain_after_hot = None
    if has_thd:
        setup_plugin(plugin, PR, GAIN)
        # Run THD first, then 2s silence, then pink noise
        combined = np.concatenate([
            thd_dry.astype(np.float32),
            silence,
            pink_dry.astype(np.float32)
        ])
        result = plugin(np.stack([combined, combined]), sr)[0]
        pink_after_hot = result[len(thd_dry) + len(silence):]
        gain_after_hot = rms_db(pink_after_hot) - rms_db(pink_dry)
        print(f"\n  Test 2 — Pink noise AFTER hot signal (+2s silence):")
        print(f"    Output RMS: {rms_db(pink_after_hot):.1f} dB")
        print(f"    Gain: {gain_after_hot:+.1f} dB")
        print(f"    Carry-over effect: {gain_after_hot - gain_isolated:+.1f} dB")

    # Test 3: Run the full test signal like auto_compare does
    gain_full_run = None
    all_files = sorted(glob.glob(os.path.join(DRY_DIR, "*.wav")))
    file_data = {}
    if all_files:
        setup_plugin(plugin, PR, GAIN)
        full_signal = []
        test_starts = []
        offset = 0
        for f in all_files:
            test_starts.append(offset)
            data, file_sr = load_mono(f)
            if file_sr != sr:
                raise ValueError(f"Sample rate mismatch in {os.path.basename(f)}: expected {sr}, got {file_sr}")
            file_data[f] = data
            full_signal.append(data.astype(np.float32))
            offset += len(data)
        full = np.concatenate(full_signal)
        result = plugin(np.stack([full, full]), sr)[0]

        # Find pink noise section
        for i, f in enumerate(all_files):
            if 'pink' in os.path.basename(f).lower():
                start = test_starts[i]
                end = start + len(file_data[f])
                pink_full_run = result[start:end]
                gain_full_run = rms_db(pink_full_run) - rms_db(pink_dry[:len(pink_full_run)])
                print(f"\n  Test 3 — Pink noise in FULL test sequence:")
                print(f"    Output RMS: {rms_db(pink_full_run):.1f} dB")
                print(f"    Gain: {gain_full_run:+.1f} dB")
                print(f"    Full-sequence effect: {gain_full_run - gain_isolated:+.1f} dB")
                break

    # Compare with LA-2A reference
    gain_la2a = None
    if os.path.exists(LA2A_PATH):
        try:
            la2a_full, la2a_sr = load_mono(LA2A_PATH)
            if la2a_sr != sr:
                print(f"  WARNING: LA-2A sample rate ({la2a_sr}) != expected ({sr})")
                print(f"  Skipping LA-2A comparison due to sample rate mismatch")
                raise ValueError(f"Sample rate mismatch: {la2a_sr} != {sr}")
            # Find approximate pink noise section in LA-2A (test 07 starts at ~sum of first 6 tests)
            offset = 0
            pink_found = False
            for i, f in enumerate(all_files):
                if 'pink' in os.path.basename(f).lower():
                    pink_found = True
                    break
                data = file_data.get(f)
                if data is None:
                    data, _ = load_mono(f)
                offset += len(data)

            if not pink_found:
                print(f"\n  Warning: No pink noise file found in test signals, LA-2A comparison may be incorrect")
                pink_la2a = np.array([])  # Skip comparison
            else:
                pink_la2a = la2a_full[offset:offset+len(pink_dry)]
            if len(pink_la2a) > 0:
                gain_la2a = rms_db(pink_la2a) - rms_db(pink_dry[:len(pink_la2a)])
                print(f"\n  LA-2A reference:")
                print(f"    Output RMS: {rms_db(pink_la2a):.1f} dB")
                print(f"    Gain: {gain_la2a:+.1f} dB")
        except Exception as e:
            print(f"\n  Could not load LA-2A reference: {e}")

    print(f"\n  Summary:")
    print(f"    MC isolated:    {gain_isolated:+.1f} dB")
    if has_thd and gain_after_hot is not None:
        print(f"    MC after hot:   {gain_after_hot:+.1f} dB")
    if gain_full_run is not None:
        print(f"    MC full run:    {gain_full_run:+.1f} dB")
    if gain_la2a is not None:
        print(f"    LA-2A:          {gain_la2a:+.1f} dB")
        print(f"    Gap (isolated): {abs(gain_isolated - gain_la2a):.1f} dB")
        if has_thd and gain_after_hot is not None:
            print(f"    Gap (after hot): {abs(gain_after_hot - gain_la2a):.1f} dB")
        if gain_full_run is not None:
            print(f"    Gap (full run): {abs(gain_full_run - gain_la2a):.1f} dB")
if __name__ == "__main__":
    main()
