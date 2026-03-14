#!/usr/bin/env python3
"""
DuskVerb Room Algorithm — Spectral Response Model

Models the cumulative frequency response of the DuskVerb Room reverb tail,
combining the input bandwidth filter with the FDN feedback damping loop.
Allows comparison against a Match EQ screenshot from Logic Pro.

Parameters verified against source code:
  - AlgorithmConfig.h: kRoom config (bandwidth, damping scales, delays, etc.)
  - FDNReverb.cpp: updateDecayCoefficients(), TwoBandDamping filter
  - DuskVerbEngine.cpp: treble/bass multiply scaling curves
  - PluginProcessor.cpp: default parameter values
"""

import numpy as np
import matplotlib.pyplot as plt
import os

# ============================================================================
# Room algorithm parameters (from AlgorithmConfig.h kRoom)
# ============================================================================

SAMPLE_RATE = 44100.0
RT60 = 0.7  # seconds (test condition)

# Room delay lengths (at 44.1kHz base rate)
ROOM_DELAYS = [1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
               2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137]
AVG_DELAY = np.mean(ROOM_DELAYS)  # ~2003 samples

# Room bandwidth (Hz) — first-order LP on input
BANDWIDTH_CURRENT = 5000.0   # kRoom.bandwidthHz
BANDWIDTH_PROPOSED = 12000.0  # proposed improvement

# Room damping config (from kRoom in AlgorithmConfig.h)
TREBLE_MULT_SCALE = 0.45       # trebleMultScale (dark end)
TREBLE_MULT_SCALE_MAX = 0.95   # trebleMultScaleMax (bright end)
BASS_MULT_SCALE = 0.85         # bassMultScale

# Default parameter values (from PluginProcessor.cpp)
USER_TREBLE = 0.5   # "damping" param default
USER_BASS = 1.2     # "bass_mult" param default (note: range 0.5-2.0, default 1.2)
CROSSOVER_HZ = 1000.0  # default crossover frequency

# Room decay time scale (from kRoom)
DECAY_TIME_SCALE = 1.4  # Room multiplies user's decay time by 1.4x

# Structural LF damping (Room uses 40Hz HP in feedback)
STRUCT_LF_HZ = 40.0

# ============================================================================
# Derived parameters (from DuskVerbEngine.cpp and FDNReverb.cpp)
# ============================================================================

# Treble multiply calculation (DuskVerbEngine.cpp lines 412-414):
#   trebleCurve = mult * mult  (squared)
#   scaledTreble = trebleMultScale * (1 - trebleCurve) + trebleMultScaleMax * trebleCurve
treble_curve = USER_TREBLE * USER_TREBLE  # 0.25
scaled_treble = TREBLE_MULT_SCALE * (1.0 - treble_curve) + TREBLE_MULT_SCALE_MAX * treble_curve
# = 0.45 * 0.75 + 0.95 * 0.25 = 0.3375 + 0.2375 = 0.575

# Bass multiply calculation (DuskVerbEngine.cpp line 401):
#   setBassMultiply(mult * config_->bassMultScale)
scaled_bass = USER_BASS * BASS_MULT_SCALE  # 1.2 * 0.85 = 1.02

# Effective decay time (DuskVerbEngine.cpp line 379):
effective_rt60 = RT60 * DECAY_TIME_SCALE  # 0.7 * 1.4 = 0.98s

# Base feedback gain (FDNReverb.cpp line 708):
# gBase = 10^(-3 * delayLength / (RT60 * sampleRate))
g_base = 10.0 ** (-3.0 * AVG_DELAY / (effective_rt60 * SAMPLE_RATE))

# Two-band damping gains (FDNReverb.cpp lines 713, 717):
# gLow = gBase^(1/bassMultiply), gHigh = gBase^(1/trebleMultiply)
g_low = g_base ** (1.0 / scaled_bass)
g_high = g_base ** (1.0 / scaled_treble)

# ============================================================================
# Frequency axis
# ============================================================================

freqs = np.logspace(np.log10(20), np.log10(20000), 2000)


# ============================================================================
# 1. Input bandwidth filter: first-order LP
# ============================================================================
# The code uses: coeff = exp(-2*pi*fc/sr)
# Filter: y[n] = (1-coeff)*x[n] + coeff*y[n-1]
# Transfer function: H(z) = (1-c) / (1 - c*z^{-1})
# Magnitude: |H(f)| = (1-c) / sqrt(1 - 2c*cos(w) + c^2)  where w = 2*pi*f/sr

def input_bw_response(freqs, fc, sr):
    """First-order LP magnitude response (DuskVerbEngine input bandwidth filter)."""
    c = np.exp(-2.0 * np.pi * fc / sr)
    w = 2.0 * np.pi * freqs / sr
    mag = (1.0 - c) / np.sqrt(1.0 - 2.0 * c * np.cos(w) + c**2)
    return mag


def input_bw_response_db(freqs, fc, sr):
    return 20.0 * np.log10(input_bw_response(freqs, fc, sr))


# ============================================================================
# 2. TwoBandDamping per-loop frequency response
# ============================================================================
# From TwoBandDamping.h:
#   lpState = (1-c)*input + c*lpState  (first-order LP at crossover)
#   output = gHigh * input + (gLow - gHigh) * lpState
#
# Transfer function:
#   H_lp(z) = (1-c)/(1-c*z^{-1})     (lowpass part)
#   H_damp(z) = gHigh + (gLow - gHigh) * H_lp(z)
#            = gHigh + (gLow - gHigh) * (1-c)/(1-c*z^{-1})
#
# At DC (f=0): H_lp=1, H_damp = gHigh + (gLow-gHigh) = gLow
# At Nyquist: H_lp→0, H_damp → gHigh

def twoband_damping_response(freqs, g_low, g_high, crossover_hz, sr):
    """Per-loop magnitude response of TwoBandDamping filter."""
    c = np.exp(-2.0 * np.pi * crossover_hz / sr)
    w = 2.0 * np.pi * freqs / sr
    # LP magnitude
    lp_mag = (1.0 - c) / np.sqrt(1.0 - 2.0 * c * np.cos(w) + c**2)
    # LP phase
    lp_phase = np.arctan2(-(1.0 - c) * np.sin(w),
                           (1.0 - c) * np.cos(w) - (1.0 - c))
    # Actually, let's compute the complex response properly
    z = np.exp(1j * w)
    H_lp = (1.0 - c) / (1.0 - c * z**(-1))
    H_damp = g_high + (g_low - g_high) * H_lp
    return np.abs(H_damp)


# ============================================================================
# 3. Structural LF damping (first-order highpass in feedback)
# ============================================================================
# From FDNReverb.cpp lines 288-295:
#   hp = filtered - structLFState
#   structLFState = (1-c)*filtered + c*structLFState
# This is: HP(z) = 1 - LP(z) where LP(z) = (1-c)/(1-c*z^{-1})
# So HP(z) = (1 - z^{-1}) / (1 - c*z^{-1})  ... wait, let's derive properly.
#
# State update: s[n] = (1-c)*x[n] + c*s[n-1]  (same LP)
# Output: y[n] = x[n] - s[n-1]
# In z-domain: S(z) = (1-c)*X(z) + c*z^{-1}*S(z)  =>  S(z) = (1-c)/(1-c*z^{-1}) * X(z)
# But y[n] = x[n] - s[n-1]  =>  Y(z) = X(z) - z^{-1}*S(z)
# Y(z) = X(z) - z^{-1} * (1-c)/(1-c*z^{-1}) * X(z)
# Y(z)/X(z) = 1 - (1-c)*z^{-1}/(1-c*z^{-1})
# = (1 - c*z^{-1} - (1-c)*z^{-1}) / (1-c*z^{-1})
# = (1 - z^{-1}) / (1 - c*z^{-1})

def struct_lf_response(freqs, hp_hz, sr):
    """Per-loop magnitude response of structural LF damping (first-order HP)."""
    if hp_hz <= 0:
        return np.ones_like(freqs)
    c = np.exp(-2.0 * np.pi * hp_hz / sr)
    w = 2.0 * np.pi * freqs / sr
    z = np.exp(1j * w)
    H = (1.0 - z**(-1)) / (1.0 - c * z**(-1))
    return np.abs(H)


# ============================================================================
# Combined per-loop feedback gain
# ============================================================================

damping_mag = twoband_damping_response(freqs, g_low, g_high, CROSSOVER_HZ, SAMPLE_RATE)
lf_damp_mag = struct_lf_response(freqs, STRUCT_LF_HZ, SAMPLE_RATE)
per_loop_gain = damping_mag * lf_damp_mag

# Number of loops per second
loops_per_sec = SAMPLE_RATE / AVG_DELAY

# Time points for cumulative response
time_points = [0.1, 0.3, 0.5, 0.7]

# ============================================================================
# PLOTTING
# ============================================================================

fig, axes = plt.subplots(3, 1, figsize=(12, 14))
fig.suptitle('DuskVerb Room Algorithm — Spectral Response Model\n'
             f'RT60={RT60}s (effective={effective_rt60:.2f}s), '
             f'Treble={USER_TREBLE}, Bass={USER_BASS}, '
             f'Crossover={CROSSOVER_HZ:.0f}Hz',
             fontsize=13, fontweight='bold')

# --- Subplot 1: Input bandwidth filter ---
ax1 = axes[0]
bw5k_db = input_bw_response_db(freqs, BANDWIDTH_CURRENT, SAMPLE_RATE)
bw12k_db = input_bw_response_db(freqs, BANDWIDTH_PROPOSED, SAMPLE_RATE)

ax1.semilogx(freqs, bw5k_db, 'r-', linewidth=2, label=f'Bandwidth = {BANDWIDTH_CURRENT:.0f} Hz (current)')
ax1.semilogx(freqs, bw12k_db, 'b--', linewidth=2, label=f'Bandwidth = {BANDWIDTH_PROPOSED:.0f} Hz (proposed)')
ax1.set_xlim(20, 20000)
ax1.set_ylim(-30, 3)
ax1.set_ylabel('Magnitude (dB)')
ax1.set_title('Input Bandwidth Filter (first-order LP)')
ax1.legend(loc='lower left')
ax1.grid(True, which='both', alpha=0.3)
ax1.axhline(y=-3, color='gray', linestyle=':', alpha=0.5, label='-3dB')
ax1.axhline(y=0, color='gray', linestyle='-', alpha=0.3)
# Mark key frequencies
for f_mark in [1000, 5000, 10000, 20000]:
    ax1.axvline(x=f_mark, color='gray', linestyle=':', alpha=0.2)

# --- Subplot 2: Cumulative feedback damping ---
ax2 = axes[1]
colors = ['#2196F3', '#4CAF50', '#FF9800', '#F44336']
for idx, t in enumerate(time_points):
    n_loops = t * loops_per_sec
    cumulative_db = 20.0 * np.log10(per_loop_gain ** n_loops)
    ax2.semilogx(freqs, cumulative_db, color=colors[idx], linewidth=2,
                 label=f't = {t}s ({n_loops:.0f} loops)')

ax2.set_xlim(20, 20000)
ax2.set_ylim(-60, 5)
ax2.set_ylabel('Magnitude (dB)')
ax2.set_title(f'Cumulative Feedback Damping (TwoBandDamping + 40Hz HP)\n'
              f'gBase={g_base:.6f}, gLow={g_low:.6f}, gHigh={g_high:.6f}, '
              f'scaledTreble={scaled_treble:.3f}, scaledBass={scaled_bass:.3f}')
ax2.legend(loc='lower left')
ax2.grid(True, which='both', alpha=0.3)
ax2.axhline(y=0, color='gray', linestyle='-', alpha=0.3)
for f_mark in [1000, 5000, 10000, 20000]:
    ax2.axvline(x=f_mark, color='gray', linestyle=':', alpha=0.2)

# --- Subplot 3: Combined response (input filter x feedback) ---
ax3 = axes[2]
t_combined = 0.5  # seconds — representative tail measurement point
n_loops_combined = t_combined * loops_per_sec

# Combined: input filter x feedback^N
input_5k = input_bw_response(freqs, BANDWIDTH_CURRENT, SAMPLE_RATE)
input_12k = input_bw_response(freqs, BANDWIDTH_PROPOSED, SAMPLE_RATE)
feedback_cumulative = per_loop_gain ** n_loops_combined

combined_5k = input_5k * feedback_cumulative
combined_12k = input_12k * feedback_cumulative

# Convert to dB and normalize to 0 dB at 1 kHz
combined_5k_db = 20.0 * np.log10(combined_5k)
combined_12k_db = 20.0 * np.log10(combined_12k)

# Find the index closest to 1 kHz for normalization
idx_1k = np.argmin(np.abs(freqs - 1000))
combined_5k_db_norm = combined_5k_db - combined_5k_db[idx_1k]
combined_12k_db_norm = combined_12k_db - combined_12k_db[idx_1k]

ax3.semilogx(freqs, combined_5k_db_norm, 'r-', linewidth=2.5,
             label=f'BW = {BANDWIDTH_CURRENT:.0f} Hz (current Room)')
ax3.semilogx(freqs, combined_12k_db_norm, 'b--', linewidth=2.5,
             label=f'BW = {BANDWIDTH_PROPOSED:.0f} Hz (proposed)')
ax3.set_xlim(20, 20000)
ax3.set_ylim(-40, 5)
ax3.set_xlabel('Frequency (Hz)')
ax3.set_ylabel('Magnitude (dB, normalized to 0 dB @ 1 kHz)')
ax3.set_title(f'Combined Response at t={t_combined}s — What Match EQ Shows\n'
              f'(Input BW filter x Feedback damping, {n_loops_combined:.0f} loops)')
ax3.legend(loc='lower left')
ax3.grid(True, which='both', alpha=0.3)
ax3.axhline(y=0, color='gray', linestyle='-', alpha=0.3)
ax3.axhline(y=-10, color='gray', linestyle=':', alpha=0.3)
ax3.axhline(y=-25, color='gray', linestyle=':', alpha=0.3)

# Annotate key reference points from the Match EQ observation
for f_mark, label in [(5000, '5k'), (10000, '10k'), (20000, '20k')]:
    ax3.axvline(x=f_mark, color='gray', linestyle=':', alpha=0.3)
    idx_f = np.argmin(np.abs(freqs - f_mark))
    val_5k = combined_5k_db_norm[idx_f]
    val_12k = combined_12k_db_norm[idx_f]
    ax3.annotate(f'{val_5k:.1f} dB', xy=(f_mark, val_5k),
                 xytext=(f_mark * 1.15, val_5k + 2),
                 fontsize=8, color='red', fontweight='bold')
    ax3.annotate(f'{val_12k:.1f} dB', xy=(f_mark, val_12k),
                 xytext=(f_mark * 1.15, val_12k - 3),
                 fontsize=8, color='blue', fontweight='bold')

plt.tight_layout()

# Save
script_dir = os.path.dirname(os.path.abspath(__file__))
output_path = os.path.join(script_dir, 'spectral_model.png')
plt.savefig(output_path, dpi=150, bbox_inches='tight')
print(f"Plot saved to: {output_path}")

# ============================================================================
# Print key values
# ============================================================================

print("\n" + "=" * 80)
print("DuskVerb Room — Spectral Model Key Values")
print("=" * 80)

print(f"\n--- Algorithm Parameters ---")
print(f"  Average delay length:     {AVG_DELAY:.0f} samples ({AVG_DELAY/SAMPLE_RATE*1000:.1f} ms)")
print(f"  Loops per second:         {loops_per_sec:.1f}")
print(f"  User decay time:          {RT60}s")
print(f"  Effective RT60:           {effective_rt60:.2f}s (x{DECAY_TIME_SCALE} scale)")
print(f"  gBase:                    {g_base:.6f} ({20*np.log10(g_base):.2f} dB/loop)")
print(f"  Scaled treble multiply:   {scaled_treble:.4f}")
print(f"  Scaled bass multiply:     {scaled_bass:.4f}")
print(f"  gLow:                     {g_low:.6f} ({20*np.log10(g_low):.2f} dB/loop)")
print(f"  gHigh:                    {g_high:.6f} ({20*np.log10(g_high):.2f} dB/loop)")
print(f"  Crossover:                {CROSSOVER_HZ:.0f} Hz")
print(f"  Structural LF damping:    {STRUCT_LF_HZ:.0f} Hz HP")
print(f"  Loops at t={t_combined}s:          {n_loops_combined:.0f}")

print(f"\n--- Combined Response at t={t_combined}s (normalized to 0 dB @ 1 kHz) ---")
print(f"  {'Freq':>8s}  {'BW=5kHz':>10s}  {'BW=12kHz':>10s}  {'Delta':>10s}")
print(f"  {'':>8s}  {'(current)':>10s}  {'(proposed)':>10s}  {'(HF boost)':>10s}")
print(f"  {'-'*8}  {'-'*10}  {'-'*10}  {'-'*10}")

for f_check in [1000, 2000, 5000, 10000, 15000, 20000]:
    idx_f = np.argmin(np.abs(freqs - f_check))
    v5k = combined_5k_db_norm[idx_f]
    v12k = combined_12k_db_norm[idx_f]
    delta = v12k - v5k
    print(f"  {f_check:>7.0f}Hz  {v5k:>+9.1f} dB  {v12k:>+9.1f} dB  {delta:>+9.1f} dB")

print(f"\n--- Input Filter Only (dB) ---")
print(f"  {'Freq':>8s}  {'BW=5kHz':>10s}  {'BW=12kHz':>10s}")
for f_check in [1000, 5000, 10000, 20000]:
    idx_f = np.argmin(np.abs(freqs - f_check))
    v5k = input_bw_response_db(freqs, BANDWIDTH_CURRENT, SAMPLE_RATE)[idx_f]
    v12k = input_bw_response_db(freqs, BANDWIDTH_PROPOSED, SAMPLE_RATE)[idx_f]
    print(f"  {f_check:>7.0f}Hz  {v5k:>+9.1f} dB  {v12k:>+9.1f} dB")

print(f"\n--- Feedback Damping Only at t={t_combined}s (dB, absolute) ---")
cumulative_db = 20.0 * np.log10(per_loop_gain ** n_loops_combined)
for f_check in [100, 500, 1000, 5000, 10000, 20000]:
    idx_f = np.argmin(np.abs(freqs - f_check))
    print(f"  {f_check:>7.0f}Hz  {cumulative_db[idx_f]:>+9.1f} dB")

print()
