"""
Visual report generator for DuskVerb vs VintageVerb comparison.

Generates multi-panel matplotlib figures (saved as PNG) showing
side-by-side analysis of reverb characteristics.
"""

import os
import numpy as np
import matplotlib
matplotlib.use('Agg')  # Non-interactive backend
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec
import matplotlib.ticker as ticker

import reverb_metrics as metrics

# Color palette
COLOR_DV = "#2196F3"    # DuskVerb: blue
COLOR_VH = "#FF5722"    # VintageVerb: orange
COLOR_REF = "#999999"   # Reference: gray
COLOR_BG = "#1a1a2e"    # Dark background
COLOR_FG = "#e0e0e0"    # Light foreground

# Apply dark theme
plt.rcParams.update({
    'figure.facecolor': COLOR_BG,
    'axes.facecolor': '#16213e',
    'axes.edgecolor': '#444',
    'axes.labelcolor': COLOR_FG,
    'text.color': COLOR_FG,
    'xtick.color': COLOR_FG,
    'ytick.color': COLOR_FG,
    'grid.color': '#333',
    'grid.alpha': 0.4,
    'legend.facecolor': '#1a1a2e',
    'legend.edgecolor': '#444',
    'font.size': 10,
})


def generate_report(all_results, output_dir, sr):
    """Generate visual comparison reports for all mode pairings.

    Args:
        all_results: dict of mode_name -> {"duskverb": {...}, "valhalla": {...}}
        output_dir: directory for output PNGs
        sr: sample rate
    """
    os.makedirs(output_dir, exist_ok=True)

    for mode_name, data in all_results.items():
        dv = data["duskverb"]
        vh = data.get("valhalla")

        fig = _create_mode_report(mode_name, dv, vh, sr)
        path = os.path.join(output_dir, f"{mode_name.lower()}_comparison.png")
        fig.savefig(path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f"  Report saved: {path}")

    # Summary report if multiple modes
    if len(all_results) > 1:
        fig = _create_summary_report(all_results)
        path = os.path.join(output_dir, "summary_comparison.png")
        fig.savefig(path, dpi=150, bbox_inches='tight')
        plt.close(fig)
        print(f"  Summary saved: {path}")


def _create_mode_report(mode_name, dv, vh, sr):
    """Create a 6x3 panel comparison report for one mode."""
    has_vh = vh is not None

    fig = plt.figure(figsize=(20, 42))
    fig.suptitle(f"DuskVerb vs VintageVerb — {mode_name} Mode",
                 fontsize=18, fontweight='bold', y=0.99)

    gs = GridSpec(6, 3, figure=fig, hspace=0.35, wspace=0.30,
                  top=0.97, bottom=0.02, left=0.06, right=0.97)

    dv_impulse = dv.get("impulse", {})
    vh_impulse = vh.get("impulse", {}) if vh else {}

    # Row 1: RT60, EDC, Frequency Response
    _plot_rt60(fig.add_subplot(gs[0, 0]), dv_impulse.get("rt60", {}),
               vh_impulse.get("rt60"), mode_name)
    _plot_edc(fig.add_subplot(gs[0, 1]), dv_impulse.get("edc"),
              vh_impulse.get("edc") if vh_impulse else None)
    _plot_freq_response(fig.add_subplot(gs[0, 2]), dv_impulse.get("freq_response", {}),
                        vh_impulse.get("freq_response") if vh_impulse else None)

    # Row 2: Spectrograms
    _plot_spectrogram(fig.add_subplot(gs[1, 0]),
                      dv_impulse.get("spectrogram", ([], [], [[]])),
                      "DuskVerb Waterfall")
    if vh_impulse:
        _plot_spectrogram(fig.add_subplot(gs[1, 1]),
                          vh_impulse.get("spectrogram", ([], [], [[]])),
                          "VintageVerb Waterfall")
    else:
        ax = fig.add_subplot(gs[1, 1])
        ax.text(0.5, 0.5, "VintageVerb\nnot available",
                ha='center', va='center', fontsize=14, color='#666')
        ax.set_facecolor('#111')

    _plot_spectral_diff(fig.add_subplot(gs[1, 2]),
                        dv_impulse.get("spectrogram"),
                        vh_impulse.get("spectrogram") if vh_impulse else None)

    # Row 3: Ringing, Echo Density, Stereo
    _plot_ringing(fig.add_subplot(gs[2, 0]),
                  dv_impulse.get("resonances", {}),
                  vh_impulse.get("resonances") if vh_impulse else None,
                  dv.get("tone_1500hz_ringing"))
    _plot_echo_density(fig.add_subplot(gs[2, 1]),
                       dv_impulse.get("echo_density", ([], [])),
                       vh_impulse.get("echo_density") if vh_impulse else None)
    _plot_stereo(fig.add_subplot(gs[2, 2]),
                 dv_impulse.get("stereo", ([], [])),
                 vh_impulse.get("stereo") if vh_impulse else None)

    # Row 4: Early Reflections, Decay Rates, Score Table
    _plot_early_reflections(fig.add_subplot(gs[3, 0]),
                            dv_impulse.get("early_reflections", {}),
                            vh_impulse.get("early_reflections") if vh_impulse else None)
    _plot_decay_rates(fig.add_subplot(gs[3, 1]),
                      dv_impulse.get("decay_rates", {}),
                      vh_impulse.get("decay_rates") if vh_impulse else None)
    _plot_score_table(fig.add_subplot(gs[3, 2]), dv_impulse, vh_impulse)

    # Row 5 (Error Delta): IACC, Crest Factor, EDC Difference
    _plot_iacc(fig.add_subplot(gs[4, 0]),
               dv_impulse.get("iacc", ([], [])),
               vh_impulse.get("iacc") if vh_impulse else None)
    _plot_crest_factor(fig.add_subplot(gs[4, 1]),
                       dv_impulse.get("crest_factor", ([], [])),
                       vh_impulse.get("crest_factor") if vh_impulse else None)
    _plot_edc_diff(fig.add_subplot(gs[4, 2]),
                   dv_impulse.get("edc"),
                   vh_impulse.get("edc") if vh_impulse else None)

    # Row 6 (Error Delta): Spectral MSE, Pitch Variance, Time-Domain Error
    _plot_spectral_mse(fig.add_subplot(gs[5, 0]),
                       dv.get("spectral_mse", {}))
    _plot_pitch_variance(fig.add_subplot(gs[5, 1]),
                         dv_impulse.get("pitch_variance", {}),
                         vh_impulse.get("pitch_variance") if vh_impulse else None)
    _plot_time_domain_error(fig.add_subplot(gs[5, 2]),
                            dv_impulse, vh_impulse, sr)

    return fig


# ---------------------------------------------------------------------------
# Individual panel plot functions
# ---------------------------------------------------------------------------
def _plot_rt60(ax, dv_rt60, vh_rt60, mode_name):
    """RT60 per octave band — grouped bar chart."""
    bands = list(metrics.OCTAVE_BANDS.keys())
    x = np.arange(len(bands))
    width = 0.35

    dv_vals = [dv_rt60.get(b, 0) or 0 for b in bands]
    ax.bar(x - width/2, dv_vals, width, label="DuskVerb", color=COLOR_DV, alpha=0.85)

    if vh_rt60:
        vh_vals = [vh_rt60.get(b, 0) or 0 for b in bands]
        ax.bar(x + width/2, vh_vals, width, label="VintageVerb", color=COLOR_VH, alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(bands, rotation=45, ha='right', fontsize=8)
    ax.set_ylabel("RT60 (seconds)")
    ax.set_title("RT60 by Octave Band", fontweight='bold')
    ax.legend(fontsize=8)
    ax.grid(True, axis='y')


def _plot_edc(ax, dv_edc, vh_edc):
    """Energy Decay Curves overlaid."""
    if dv_edc:
        t_dv, edc_dv = dv_edc
        if len(t_dv) > 0:
            ax.plot(t_dv, edc_dv, color=COLOR_DV, linewidth=1.5, label="DuskVerb")

    if vh_edc:
        t_vh, edc_vh = vh_edc
        if len(t_vh) > 0:
            ax.plot(t_vh, edc_vh, color=COLOR_VH, linewidth=1.5, label="VintageVerb")

    # Reference lines
    ax.axhline(-60, color=COLOR_REF, linestyle='--', linewidth=0.8, alpha=0.5, label="-60 dB")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Energy (dB)")
    ax.set_title("Energy Decay Curve (Schroeder)", fontweight='bold')
    ax.set_ylim(-80, 5)
    ax.set_xlim(0, None)
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_freq_response(ax, dv_fr, vh_fr):
    """Frequency response bar chart."""
    if not dv_fr:
        ax.text(0.5, 0.5, "No data", ha='center', va='center')
        return

    bands = list(dv_fr.keys())
    x = np.arange(len(bands))
    width = 0.35

    dv_vals = [dv_fr.get(b, -100) for b in bands]
    ax.bar(x - width/2, dv_vals, width, label="DuskVerb", color=COLOR_DV, alpha=0.85)

    if vh_fr:
        vh_vals = [vh_fr.get(b, -100) for b in bands]
        ax.bar(x + width/2, vh_vals, width, label="VintageVerb", color=COLOR_VH, alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels([b.split('(')[0].strip() for b in bands], rotation=45, ha='right', fontsize=7)
    ax.set_ylabel("Level (dB)")
    ax.set_title("Frequency Response", fontweight='bold')
    ax.legend(fontsize=8)
    ax.grid(True, axis='y')


def _plot_spectrogram(ax, spectrogram_data, title):
    """Spectral waterfall display."""
    times, freqs, mag_db = spectrogram_data

    if len(times) == 0 or len(freqs) == 0 or mag_db.size < 4:
        ax.text(0.5, 0.5, "No data", ha='center', va='center')
        ax.set_title(title, fontweight='bold')
        return

    # Limit to 0-3 seconds for visibility
    time_mask = times <= 3.0
    if np.any(time_mask):
        times = times[time_mask]
        mag_db = mag_db[time_mask]

    im = ax.pcolormesh(times, freqs, mag_db.T, shading='gouraud',
                       cmap='inferno', vmin=-80, vmax=-10)
    ax.set_yscale('log')
    ax.set_ylim(80, 16000)
    ax.set_ylabel('Frequency (Hz)')
    ax.set_xlabel('Time (s)')
    ax.set_title(title, fontweight='bold')
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.yaxis.set_minor_formatter(ticker.NullFormatter())
    plt.colorbar(im, ax=ax, label='dB', shrink=0.8)


def _plot_spectral_diff(ax, dv_spec, vh_spec):
    """Spectral difference between the two reverbs."""
    if dv_spec is None or vh_spec is None:
        ax.text(0.5, 0.5, "Need both plugins\nfor comparison",
                ha='center', va='center', fontsize=12, color='#666')
        ax.set_title("Spectral Difference", fontweight='bold')
        return

    t_dv, f_dv, m_dv = dv_spec
    t_vh, f_vh, m_vh = vh_spec

    if len(t_dv) == 0 or len(t_vh) == 0:
        ax.text(0.5, 0.5, "No data", ha='center', va='center')
        ax.set_title("Spectral Difference", fontweight='bold')
        return

    # Align to shorter time axis
    min_frames = min(m_dv.shape[0], m_vh.shape[0])
    min_bins = min(m_dv.shape[1], m_vh.shape[1])
    diff = m_dv[:min_frames, :min_bins] - m_vh[:min_frames, :min_bins]

    times = t_dv[:min_frames]
    freqs = f_dv[:min_bins]

    im = ax.pcolormesh(times, freqs, diff.T, shading='gouraud',
                       cmap='RdBu_r', vmin=-20, vmax=20)
    ax.set_yscale('log')
    ax.set_ylim(80, 16000)
    ax.set_ylabel('Frequency (Hz)')
    ax.set_xlabel('Time (s)')
    ax.set_title("Spectral Diff (DV - VH)", fontweight='bold')
    ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.yaxis.set_minor_formatter(ticker.NullFormatter())
    plt.colorbar(im, ax=ax, label='dB diff', shrink=0.8)


def _plot_ringing(ax, dv_res, vh_res, dv_tone_res):
    """Modal resonance detection visualization."""
    # Plot DuskVerb persistent peaks
    if dv_res and dv_res.get("persistent_peaks"):
        freqs = [p[0] for p in dv_res["persistent_peaks"]]
        proms = [p[1] for p in dv_res["persistent_peaks"]]
        ax.bar(freqs, proms, width=50, color=COLOR_DV, alpha=0.7, label="DuskVerb (impulse)")

    # Plot VintageVerb persistent peaks
    if vh_res and vh_res.get("persistent_peaks"):
        freqs = [p[0] for p in vh_res["persistent_peaks"]]
        proms = [p[1] for p in vh_res["persistent_peaks"]]
        ax.bar([f+25 for f in freqs], proms, width=50, color=COLOR_VH, alpha=0.7,
               label="VintageVerb (impulse)")

    # Highlight 1500 Hz tone burst results
    if dv_tone_res and dv_tone_res.get("persistent_peaks"):
        freqs = [p[0] for p in dv_tone_res["persistent_peaks"]]
        proms = [p[1] for p in dv_tone_res["persistent_peaks"]]
        ax.scatter(freqs, proms, s=80, marker='x', color='#ffeb3b', linewidths=2,
                   label="DV 1500Hz probe", zorder=5)

    # Reference line
    ax.axhline(8.0, color=COLOR_REF, linestyle='--', linewidth=0.8, label="8 dB threshold")
    ax.axvline(1500, color='#ffeb3b', linestyle=':', linewidth=0.8, alpha=0.5, label="1500 Hz")

    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Prominence (dB)")
    ax.set_title("Modal Ringing Detection", fontweight='bold')
    ax.set_xlim(100, 10000)
    ax.set_xscale('log')
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.legend(fontsize=7, loc='upper right')
    ax.grid(True)


def _plot_echo_density(ax, dv_dens, vh_dens):
    """Echo density evolution over time."""
    t_dv, d_dv = dv_dens
    if len(t_dv) > 0:
        ax.plot(t_dv, d_dv, color=COLOR_DV, linewidth=1.5, label="DuskVerb")

    if vh_dens:
        t_vh, d_vh = vh_dens
        if len(t_vh) > 0:
            ax.plot(t_vh, d_vh, color=COLOR_VH, linewidth=1.5, label="VintageVerb")

    ax.axhline(1000, color=COLOR_REF, linestyle='--', linewidth=0.8,
               label="Schroeder threshold")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Echoes/sec")
    ax.set_title("Echo Density Over Time", fontweight='bold')
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_stereo(ax, dv_stereo, vh_stereo):
    """Stereo decorrelation over time."""
    t_dv, c_dv = dv_stereo
    if len(t_dv) > 0:
        ax.plot(t_dv, c_dv, color=COLOR_DV, linewidth=1.5, label="DuskVerb")

    if vh_stereo:
        t_vh, c_vh = vh_stereo
        if len(t_vh) > 0:
            ax.plot(t_vh, c_vh, color=COLOR_VH, linewidth=1.5, label="VintageVerb")

    ax.axhline(0.3, color=COLOR_REF, linestyle='--', linewidth=0.8, label="Good (<0.3)")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("L-R Correlation")
    ax.set_title("Stereo Decorrelation", fontweight='bold')
    ax.set_ylim(-0.1, 1.1)
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_early_reflections(ax, dv_er, vh_er):
    """Early reflection pattern visualization."""
    if dv_er and dv_er.get("peaks"):
        times = [p["time_ms"] for p in dv_er["peaks"]]
        levels = [p["level_db"] for p in dv_er["peaks"]]
        ax.stem(times, levels, linefmt=COLOR_DV, markerfmt='o', basefmt='',
                label="DuskVerb")

    if vh_er and vh_er.get("peaks"):
        times = [p["time_ms"] for p in vh_er["peaks"]]
        levels = [p["level_db"] for p in vh_er["peaks"]]
        ax.stem(times, levels, linefmt=COLOR_VH, markerfmt='s', basefmt='',
                label="VintageVerb")

    ax.set_xlabel("Time (ms)")
    ax.set_ylabel("Level (dB)")
    ax.set_title("Early Reflections", fontweight='bold')
    ax.set_xlim(0, 100)
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_decay_rates(ax, dv_rates, vh_rates):
    """Frequency-dependent decay rates."""
    bands = list(metrics.OCTAVE_BANDS.keys())
    x = np.arange(len(bands))
    width = 0.35

    dv_vals = [abs(dv_rates.get(b, 0)) for b in bands]
    ax.bar(x - width/2, dv_vals, width, label="DuskVerb", color=COLOR_DV, alpha=0.85)

    if vh_rates:
        vh_vals = [abs(vh_rates.get(b, 0)) for b in bands]
        ax.bar(x + width/2, vh_vals, width, label="VintageVerb", color=COLOR_VH, alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(bands, rotation=45, ha='right', fontsize=8)
    ax.set_ylabel("|Decay Rate| (dB/sec)")
    ax.set_title("Decay Rate by Band", fontweight='bold')
    ax.legend(fontsize=8)
    ax.grid(True, axis='y')


def _plot_score_table(ax, dv_impulse, vh_impulse):
    """Score comparison table."""
    ax.axis('off')

    metrics_list = [
        ("RT60 Balance", _rt60_balance_score, dv_impulse, vh_impulse),
        ("Modal Cleanliness", _ringing_score, dv_impulse, vh_impulse),
        ("Echo Density", _density_score, dv_impulse, vh_impulse),
        ("Tail Smoothness", _smoothness_score, dv_impulse, vh_impulse),
        ("Stereo Width", _stereo_score, dv_impulse, vh_impulse),
    ]

    # Header
    cols = ["Metric", "DuskVerb", "VintageVerb", "Delta"]
    if not vh_impulse:
        cols = ["Metric", "DuskVerb", "", ""]

    y_start = 0.95
    row_height = 0.12

    # Title
    ax.text(0.5, 1.0, "Quality Scores (0-100)", fontsize=12,
            fontweight='bold', ha='center', va='top', transform=ax.transAxes)

    # Column headers
    col_x = [0.02, 0.45, 0.65, 0.88]
    for i, col in enumerate(cols):
        ax.text(col_x[i], y_start, col, fontsize=9, fontweight='bold',
                ha='left' if i == 0 else 'center', va='top', transform=ax.transAxes)

    dv_total = 0
    vh_total = 0
    n_metrics = 0

    for idx, (name, score_fn, dv_data, vh_data) in enumerate(metrics_list):
        y = y_start - (idx + 1) * row_height

        dv_score = score_fn(dv_data)
        vh_score = score_fn(vh_data) if vh_data else None

        dv_total += dv_score
        if vh_score is not None:
            vh_total += vh_score
        n_metrics += 1

        ax.text(col_x[0], y, name, fontsize=9, ha='left', va='top', transform=ax.transAxes)
        ax.text(col_x[1], y, f"{dv_score:.0f}", fontsize=9, ha='center', va='top',
                transform=ax.transAxes, color=COLOR_DV)
        if vh_score is not None:
            ax.text(col_x[2], y, f"{vh_score:.0f}", fontsize=9, ha='center', va='top',
                    transform=ax.transAxes, color=COLOR_VH)
            delta = dv_score - vh_score
            color = '#4caf50' if delta > 0 else '#f44336' if delta < -5 else COLOR_FG
            ax.text(col_x[3], y, f"{delta:+.0f}", fontsize=9, ha='center', va='top',
                    transform=ax.transAxes, color=color)

    # Total
    y = y_start - (len(metrics_list) + 1) * row_height
    ax.plot([0.02, 0.95], [y + 0.03, y + 0.03], color='#444', linewidth=0.5,
            transform=ax.transAxes)
    ax.text(col_x[0], y, "OVERALL", fontsize=10, fontweight='bold', ha='left', va='top',
            transform=ax.transAxes)
    ax.text(col_x[1], y, f"{dv_total/n_metrics:.0f}", fontsize=10, fontweight='bold',
            ha='center', va='top', transform=ax.transAxes, color=COLOR_DV)
    if vh_impulse:
        ax.text(col_x[2], y, f"{vh_total/n_metrics:.0f}", fontsize=10, fontweight='bold',
                ha='center', va='top', transform=ax.transAxes, color=COLOR_VH)
        delta = (dv_total - vh_total) / n_metrics
        color = '#4caf50' if delta > 0 else '#f44336' if delta < -5 else COLOR_FG
        ax.text(col_x[3], y, f"{delta:+.0f}", fontsize=10, fontweight='bold',
                ha='center', va='top', transform=ax.transAxes, color=color)


# ---------------------------------------------------------------------------
# Error Delta panel plot functions (Row 5-6)
# ---------------------------------------------------------------------------
def _plot_iacc(ax, dv_iacc, vh_iacc):
    """IACC (Inter-Aural Cross-Correlation) over time."""
    t_dv, v_dv = dv_iacc
    if len(t_dv) > 0:
        ax.plot(t_dv, v_dv, color=COLOR_DV, linewidth=1.5, label="DuskVerb")

    if vh_iacc:
        t_vh, v_vh = vh_iacc
        if len(t_vh) > 0:
            ax.plot(t_vh, v_vh, color=COLOR_VH, linewidth=1.5, label="VintageVerb")

    ax.axhline(0.3, color=COLOR_REF, linestyle='--', linewidth=0.8, label="Wide (<0.3)")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("IACC")
    ax.set_title("IACC (ISO 3382-1 Stereo Width)", fontweight='bold')
    ax.set_ylim(-0.05, 1.05)
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_crest_factor(ax, dv_crest, vh_crest):
    """Crest factor (peak/RMS) over time."""
    t_dv, v_dv = dv_crest
    if len(t_dv) > 0:
        ax.plot(t_dv, v_dv, color=COLOR_DV, linewidth=1.5, label="DuskVerb")

    if vh_crest:
        t_vh, v_vh = vh_crest
        if len(t_vh) > 0:
            ax.plot(t_vh, v_vh, color=COLOR_VH, linewidth=1.5, label="VintageVerb")

    # Reference: Gaussian noise crest factor = sqrt(2) ≈ 1.41
    ax.axhline(1.41, color=COLOR_REF, linestyle='--', linewidth=0.8,
               label="Gaussian (smooth)")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Crest Factor")
    ax.set_title("Crest Factor (Texture Density)", fontweight='bold')
    ax.set_ylim(0, 6)
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_edc_diff(ax, dv_edc, vh_edc):
    """EDC difference curve (DV - VH)."""
    if dv_edc is None or vh_edc is None:
        ax.text(0.5, 0.5, "Need both plugins\nfor comparison",
                ha='center', va='center', fontsize=12, color='#666')
        ax.set_title("EDC Difference", fontweight='bold')
        return

    t_dv, edc_dv = dv_edc
    t_vh, edc_vh = vh_edc

    if len(t_dv) == 0 or len(t_vh) == 0:
        ax.text(0.5, 0.5, "No data", ha='center', va='center')
        ax.set_title("EDC Difference", fontweight='bold')
        return

    # Interpolate to common time axis
    min_len = min(len(edc_dv), len(edc_vh))
    diff = edc_dv[:min_len] - edc_vh[:min_len]
    t = t_dv[:min_len]

    ax.plot(t, diff, color='#ffeb3b', linewidth=1.5)
    ax.axhline(0, color=COLOR_REF, linestyle='--', linewidth=0.8)
    ax.fill_between(t, diff, 0, where=(diff > 0), color=COLOR_DV, alpha=0.3)
    ax.fill_between(t, diff, 0, where=(diff < 0), color=COLOR_VH, alpha=0.3)

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("dB (DV - VH)")
    ax.set_title("EDC Difference (DV - VH)", fontweight='bold')
    ax.set_ylim(-15, 15)
    ax.grid(True)


def _plot_spectral_mse(ax, mse_data):
    """Spectral MSE per 1/3-octave band bar chart."""
    if not mse_data:
        ax.text(0.5, 0.5, "No spectral MSE data\n(need both plugins)",
                ha='center', va='center', fontsize=12, color='#666')
        ax.set_title("Spectral MSE (1/3-Octave)", fontweight='bold')
        return

    bands = list(mse_data.keys())
    values = [mse_data[b] for b in bands]
    x = np.arange(len(bands))

    colors = ['#4caf50' if v < 10 else '#ffeb3b' if v < 25 else '#f44336'
              for v in values]
    ax.bar(x, values, color=colors, alpha=0.85)

    ax.set_xticks(x)
    ax.set_xticklabels(bands, rotation=45, ha='right', fontsize=7)
    ax.set_ylabel("MSE (dB\u00b2)")
    ax.set_title("Spectral MSE (1/3-Octave, Level-Normalized)", fontweight='bold')
    ax.axhline(10, color=COLOR_REF, linestyle='--', linewidth=0.8, label="Good (<10)")
    ax.legend(fontsize=8)
    ax.grid(True, axis='y')


def _plot_pitch_variance(ax, dv_pv, vh_pv):
    """Pitch variance (ZCR) over time."""
    if dv_pv and len(dv_pv.get("times", [])) > 0:
        ax.plot(dv_pv["times"], dv_pv["zcr_values"], color=COLOR_DV,
                linewidth=1.5, label=f"DV (var={dv_pv['zcr_variance_ratio']:.3f})")

    if vh_pv and len(vh_pv.get("times", [])) > 0:
        ax.plot(vh_pv["times"], vh_pv["zcr_values"], color=COLOR_VH,
                linewidth=1.5, label=f"VH (var={vh_pv['zcr_variance_ratio']:.3f})")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Zero-Crossing Rate (Hz)")
    ax.set_title("Pitch Variance (Modulation Character)", fontweight='bold')
    ax.legend(fontsize=8)
    ax.grid(True)


def _plot_time_domain_error(ax, dv_impulse, vh_impulse, sr):
    """Time-domain error envelope |DV(t) - VH(t)| in dB."""
    if not dv_impulse or not vh_impulse:
        ax.text(0.5, 0.5, "Need both plugins\nfor comparison",
                ha='center', va='center', fontsize=12, color='#666')
        ax.set_title("Time-Domain Error", fontweight='bold')
        return

    dv_edc = dv_impulse.get("edc")
    vh_edc = vh_impulse.get("edc")

    if dv_edc is None or vh_edc is None:
        ax.text(0.5, 0.5, "No EDC data", ha='center', va='center')
        ax.set_title("Time-Domain Error", fontweight='bold')
        return

    # Use the EDC as a proxy for time-domain error envelope
    # (actual sample-level |DV-VH| is too noisy to plot directly)
    t_dv, edc_dv = dv_edc
    t_vh, edc_vh = vh_edc

    min_len = min(len(edc_dv), len(edc_vh))
    if min_len < 2:
        ax.text(0.5, 0.5, "Insufficient data", ha='center', va='center')
        ax.set_title("Time-Domain Error", fontweight='bold')
        return

    error = np.abs(edc_dv[:min_len] - edc_vh[:min_len])
    t = t_dv[:min_len]

    ax.fill_between(t, error, color='#ff5722', alpha=0.4)
    ax.plot(t, error, color='#ff5722', linewidth=1.0)
    ax.axhline(3.0, color=COLOR_REF, linestyle='--', linewidth=0.8, label="3 dB threshold")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("|Error| (dB)")
    ax.set_title("Time-Domain Error (|EDC_DV - EDC_VH|)", fontweight='bold')
    ax.set_ylim(0, 20)
    ax.legend(fontsize=8)
    ax.grid(True)


def _create_summary_report(all_results):
    """Create a summary comparing all modes."""
    n_modes = len(all_results)
    fig, axes = plt.subplots(2, 1, figsize=(16, 12))
    fig.suptitle("DuskVerb vs VintageVerb — All Modes Summary",
                 fontsize=16, fontweight='bold')

    # Panel 1: RT60 ratios across modes and bands
    ax = axes[0]
    bands = list(metrics.OCTAVE_BANDS.keys())
    x = np.arange(len(bands))
    width = 0.8 / n_modes
    colors = plt.cm.Set2(np.linspace(0, 1, n_modes))

    for i, (mode_name, data) in enumerate(all_results.items()):
        dv_rt60 = data["duskverb"].get("impulse", {}).get("rt60", {})
        vh_rt60 = (data["valhalla"].get("impulse", {}).get("rt60", {})
                   if data.get("valhalla") else {})

        if vh_rt60:
            ratios = []
            for b in bands:
                dv = dv_rt60.get(b)
                vh = vh_rt60.get(b)
                ratios.append(dv / vh if dv and vh and vh > 0 else 1.0)
            offset = (i - n_modes / 2 + 0.5) * width
            ax.bar(x + offset, ratios, width, label=mode_name, color=colors[i], alpha=0.8)

    ax.axhline(1.0, color=COLOR_REF, linestyle='--', linewidth=1)
    ax.axhline(0.7, color='#f44336', linestyle=':', linewidth=0.8, alpha=0.5)
    ax.set_xticks(x)
    ax.set_xticklabels(bands)
    ax.set_ylabel("RT60 Ratio (DV / VH)")
    ax.set_title("RT60 Ratio by Mode — below 1.0 = DuskVerb is darker", fontweight='bold')
    ax.legend(fontsize=9)
    ax.grid(True, axis='y')

    # Panel 2: Overall scores
    ax = axes[1]
    mode_names = list(all_results.keys())
    score_names = ["RT60 Balance", "Ringing", "Density", "Smoothness", "Stereo"]
    x = np.arange(len(mode_names))

    for i, (mode_name, data) in enumerate(all_results.items()):
        dv_imp = data["duskverb"].get("impulse", {})
        scores = [
            _rt60_balance_score(dv_imp),
            _ringing_score(dv_imp),
            _density_score(dv_imp),
            _smoothness_score(dv_imp),
            _stereo_score(dv_imp),
        ]
        avg = np.mean(scores)
        ax.bar(i, avg, color=COLOR_DV, alpha=0.8, width=0.6)
        ax.text(i, avg + 1, f"{avg:.0f}", ha='center', fontsize=10, fontweight='bold')

    ax.set_xticks(x)
    ax.set_xticklabels(mode_names)
    ax.set_ylabel("Overall Quality Score")
    ax.set_title("DuskVerb Quality Scores by Mode", fontweight='bold')
    ax.set_ylim(0, 110)
    ax.grid(True, axis='y')

    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# Scoring functions (0-100)
# ---------------------------------------------------------------------------
def _rt60_balance_score(impulse_data):
    """Score RT60 balance (HF should be 50-70% of LF for natural sound)."""
    rt60 = impulse_data.get("rt60", {})
    if not rt60:
        return 50

    hf = [rt60[b] for b in ["4 kHz", "8 kHz"] if rt60.get(b)]
    lf = [rt60[b] for b in ["250 Hz", "500 Hz"] if rt60.get(b)]

    if not hf or not lf:
        return 50

    ratio = np.mean(hf) / np.mean(lf) if np.mean(lf) > 0 else 0
    # Ideal ratio ~0.5-0.7 for natural reverb
    if 0.45 <= ratio <= 0.75:
        return 100
    elif 0.3 <= ratio <= 0.9:
        return 70
    elif 0.2 <= ratio <= 1.0:
        return 40
    else:
        return 20


def _ringing_score(impulse_data):
    """Score modal cleanliness (lower ringing = higher score)."""
    res = impulse_data.get("resonances", {})
    prom = res.get("max_peak_prominence_db", 0)
    if prom < 6:
        return 100
    elif prom < 8:
        return 80
    elif prom < 12:
        return 60
    elif prom < 16:
        return 40
    else:
        return max(0, 100 - prom * 5)


def _density_score(impulse_data):
    """Score echo density (target ~1000 echoes/sec)."""
    _, densities = impulse_data.get("echo_density", ([], []))
    if len(densities) == 0:
        return 50
    avg = np.mean(densities)
    return min(100, avg / 10)  # 1000 echoes/sec = 100


def _smoothness_score(impulse_data):
    """Score tail smoothness (lower std = smoother = higher score)."""
    smooth = impulse_data.get("smoothness", {})
    std = smooth.get("envelope_std_db", 5)
    if std < 1.5:
        return 100
    elif std < 2.5:
        return 80
    elif std < 4.0:
        return 60
    elif std < 6.0:
        return 40
    else:
        return 20


def _stereo_score(impulse_data):
    """Score stereo decorrelation (lower tail correlation = better)."""
    _, correlations = impulse_data.get("stereo", ([], []))
    if len(correlations) == 0:
        return 50
    # Average correlation in the tail (last 75%)
    tail = correlations[len(correlations)//4:]
    if len(tail) == 0:
        return 50
    avg = np.mean(tail)
    if avg < 0.2:
        return 100
    elif avg < 0.3:
        return 85
    elif avg < 0.5:
        return 65
    elif avg < 0.7:
        return 40
    else:
        return 20
