"""
Velvet 90 IR Capture — Generate impulse responses from Velvet 90 via pedalboard.

Renders a dirac impulse through Velvet 90 with configurable parameters,
capturing the resulting impulse response for analysis and comparison.
"""

import os
import numpy as np
from dataclasses import dataclass, field
from typing import Optional

# Pedalboard import (deferred to allow module import without pedalboard installed)
_plugin_cache = {}


@dataclass
class Velvet90Params:
    """Complete Velvet 90 parameter set for IR capture."""
    mode: str = 'Hall'
    color: str = 'Now'
    size: str = '2.0s'
    room_size: float = 50.0
    damping: float = 50.0
    pre_delay_ms: float = 0.0
    mix: float = 100.0

    mod_rate_hz: float = 1.0
    mod_depth: float = 30.0
    width: float = 100.0

    early_diff: float = 50.0
    late_diff: float = 50.0
    er_late: str = 'E50/L50'

    bass_mult_x: float = 1.0
    bass_freq_hz: float = 300.0
    hf_decay_x: float = 1.0
    mid_decay_x: float = 1.0
    high_freq_hz: float = 4000.0

    er_shape: float = 50.0
    er_spread: float = 50.0
    er_bass_cut_hz: float = 20.0

    low_cut_hz: float = 20.0
    high_cut_hz: float = 20000.0

    treble_ratio: float = 1.0
    stereo_coupling: float = 15.0

    low_mid_freq_hz: float = 700.0
    low_mid_decay_x: float = 1.0

    env_mode: str = 'Off'
    env_hold_ms: float = 500.0
    env_release_ms: float = 500.0
    env_depth: float = 0.0
    echo_delay_ms: float = 0.0
    echo_feedback: float = 0.0

    out_eq1_freq_hz: float = 1000.0
    out_eq1_gain_db: float = 0.0
    out_eq1_q: float = 1.0
    out_eq2_freq_hz: float = 4000.0
    out_eq2_gain_db: float = 0.0
    out_eq2_q: float = 1.0

    stereo_invert: float = 0.0
    resonance: float = 0.0

    echo_pingpong: float = 0.0
    dyn_amount: float = 0.0
    dyn_speed: float = 0.5

    freeze: bool = False

    def apply_to_plugin(self, plugin):
        """Apply all parameters to a pedalboard plugin instance."""
        plugin.mode = self.mode
        plugin.color = self.color
        plugin.size = self.size
        plugin.room_size = self.room_size
        plugin.damping = self.damping
        plugin.pre_delay_ms = self.pre_delay_ms
        plugin.mix = self.mix
        plugin.mod_rate_hz = self.mod_rate_hz
        plugin.mod_depth = self.mod_depth
        plugin.width = self.width
        plugin.early_diff = self.early_diff
        plugin.late_diff = self.late_diff
        plugin.er_late = self.er_late
        plugin.bass_mult_x = self.bass_mult_x
        plugin.bass_freq_hz = self.bass_freq_hz
        plugin.hf_decay_x = self.hf_decay_x
        plugin.mid_decay_x = self.mid_decay_x
        plugin.high_freq_hz = self.high_freq_hz
        plugin.er_shape = self.er_shape
        plugin.er_spread = self.er_spread
        plugin.er_bass_cut_hz = self.er_bass_cut_hz
        plugin.low_cut_hz = self.low_cut_hz
        plugin.high_cut_hz = self.high_cut_hz
        plugin.treble_ratio = self.treble_ratio
        plugin.stereo_coupling = self.stereo_coupling
        plugin.low_mid_freq_hz = self.low_mid_freq_hz
        plugin.low_mid_decay_x = self.low_mid_decay_x
        plugin.env_mode = self.env_mode
        plugin.env_hold_ms = self.env_hold_ms
        plugin.env_release_ms = self.env_release_ms
        plugin.env_depth = self.env_depth
        plugin.echo_delay_ms = self.echo_delay_ms
        plugin.echo_feedback = self.echo_feedback
        plugin.out_eq1_freq_hz = self.out_eq1_freq_hz
        plugin.out_eq1_gain_db = self.out_eq1_gain_db
        plugin.out_eq1_q = self.out_eq1_q
        plugin.out_eq2_freq_hz = self.out_eq2_freq_hz
        plugin.out_eq2_gain_db = self.out_eq2_gain_db
        plugin.out_eq2_q = self.out_eq2_q
        plugin.stereo_invert = self.stereo_invert
        plugin.resonance = self.resonance
        plugin.echo_ping_pong = self.echo_pingpong
        # dyn_amount uses bipolar string labels (pedalboard sees it as enum)
        if self.dyn_amount < -0.005:
            plugin.dyn_amount = f"Duck {round(abs(self.dyn_amount) * 100)}%"
        elif self.dyn_amount > 0.005:
            plugin.dyn_amount = f"Expand {round(self.dyn_amount * 100)}%"
        else:
            plugin.dyn_amount = "Off"
        plugin.dyn_speed = self.dyn_speed
        plugin.freeze = self.freeze

    def to_dict(self) -> dict:
        """Convert to dict for serialization."""
        return {
            'mode': self.mode, 'color': self.color, 'size': self.size,
            'room_size': self.room_size, 'damping': self.damping,
            'pre_delay_ms': self.pre_delay_ms, 'mix': self.mix,
            'mod_rate_hz': self.mod_rate_hz, 'mod_depth': self.mod_depth,
            'width': self.width, 'early_diff': self.early_diff,
            'late_diff': self.late_diff, 'er_late': self.er_late,
            'bass_mult_x': self.bass_mult_x, 'bass_freq_hz': self.bass_freq_hz,
            'hf_decay_x': self.hf_decay_x, 'mid_decay_x': self.mid_decay_x,
            'high_freq_hz': self.high_freq_hz, 'er_shape': self.er_shape,
            'er_spread': self.er_spread, 'er_bass_cut_hz': self.er_bass_cut_hz,
            'low_cut_hz': self.low_cut_hz,
            'high_cut_hz': self.high_cut_hz,
            'treble_ratio': self.treble_ratio,
            'stereo_coupling': self.stereo_coupling,
            'low_mid_freq_hz': self.low_mid_freq_hz,
            'low_mid_decay_x': self.low_mid_decay_x,
            'env_mode': self.env_mode,
            'env_hold_ms': self.env_hold_ms,
            'env_release_ms': self.env_release_ms,
            'env_depth': self.env_depth,
            'echo_delay_ms': self.echo_delay_ms,
            'echo_feedback': self.echo_feedback,
            'out_eq1_freq_hz': self.out_eq1_freq_hz,
            'out_eq1_gain_db': self.out_eq1_gain_db,
            'out_eq1_q': self.out_eq1_q,
            'out_eq2_freq_hz': self.out_eq2_freq_hz,
            'out_eq2_gain_db': self.out_eq2_gain_db,
            'out_eq2_q': self.out_eq2_q,
            'stereo_invert': self.stereo_invert,
            'resonance': self.resonance,
            'echo_pingpong': self.echo_pingpong,
            'dyn_amount': self.dyn_amount,
            'dyn_speed': self.dyn_speed,
            'freeze': self.freeze,
        }

    @staticmethod
    def from_dict(d: dict) -> 'Velvet90Params':
        # Filter out unknown keys and handle type mismatches
        valid_keys = {f.name for f in __import__('dataclasses').fields(Velvet90Params)}
        filtered = {k: v for k, v in d.items() if k in valid_keys}
        return Velvet90Params(**filtered)


# Valid values for discrete parameters (for optimizer bounds)
VALID_MODES = ['Plate', 'Room', 'Hall', 'Chamber', 'Cathedral', 'Ambience',
               'Bright Hall', 'Chorus Space', 'Random Space', 'Dirty Hall']

VALID_COLORS = ['1970s', '1980s', 'Now']

VALID_SIZES = [
    '0.1s', '0.2s', '0.3s', '0.4s', '0.5s', '0.6s', '0.7s', '0.8s', '0.9s',
    '1.0s', '1.1s', '1.2s', '1.3s', '1.4s', '1.5s', '1.6s', '1.7s', '1.8s', '1.9s',
    '2.0s', '2.1s', '2.2s', '2.3s', '2.4s', '2.5s', '2.6s', '2.7s', '2.8s', '2.9s',
    '3.0s', '3.1s', '3.2s', '3.3s', '3.4s', '3.5s', '3.6s', '3.7s', '3.8s', '3.9s',
    '4.0s', '4.1s', '4.2s', '4.4s', '4.5s', '4.6s', '4.7s', '4.8s', '4.9s',
    '5.1s', '5.2s', '5.3s', '5.4s', '5.5s', '5.7s', '5.8s', '5.9s',
    '6.0s', '6.1s', '6.3s', '6.4s', '6.5s', '6.7s', '6.8s', '6.9s',
    '7.1s', '7.2s', '7.3s', '7.5s', '7.6s', '7.7s', '7.9s',
    '8.0s', '8.1s', '8.3s', '8.4s', '8.6s', '8.7s', '8.8s',
    '9.0s', '9.1s', '9.3s', '9.4s', '9.6s', '9.7s', '9.9s', '10.0s',
]

# Map size strings to numeric seconds for optimization
SIZE_TO_SECONDS = {}
for s in VALID_SIZES:
    SIZE_TO_SECONDS[s] = float(s.replace('s', ''))
SECONDS_TO_SIZE = {v: k for k, v in SIZE_TO_SECONDS.items()}


def nearest_size(seconds: float) -> str:
    """Find the nearest valid size string for a given seconds value."""
    best = min(VALID_SIZES, key=lambda s: abs(SIZE_TO_SECONDS[s] - seconds))
    return best


_DEFAULT_VST_PATH = os.environ.get('VELVET90_VST3')


def _get_vst_path(vst_path: str = None) -> str:
    """Resolve VST path, raising a clear error if not configured."""
    path = vst_path or _DEFAULT_VST_PATH
    if path is None:
        raise EnvironmentError(
            "Velvet90 VST3 path not configured. "
            "Set VELVET90_VST3 environment variable or pass vst_path explicitly."
        )
    return path


def load_plugin(vst_path: str = None):
    """Load or return cached Velvet 90 plugin instance."""
    vst_path = _get_vst_path(vst_path)
    if vst_path not in _plugin_cache:
        from pedalboard import load_plugin as _load
        _plugin_cache[vst_path] = _load(vst_path)
    return _plugin_cache[vst_path]


def capture_ir(params: Velvet90Params,
               sr: int = 48000,
               duration_s: float = 6.0,
               vst_path: str = None,
               normalize: bool = True) -> np.ndarray:
    """
    Capture an impulse response from Velvet 90.

    Returns stereo IR as numpy array shape (2, N).
    Uses a dirac delta impulse and captures the full reverb tail.
    """
    plugin = load_plugin(vst_path)
    plugin.reset()

    # Apply parameters
    params.apply_to_plugin(plugin)

    # Generate stereo dirac impulse
    n_samples = int(sr * duration_s)
    impulse = np.zeros((2, n_samples), dtype=np.float32)
    impulse[0, 0] = 1.0
    impulse[1, 0] = 1.0

    # Process through plugin
    output = plugin.process(impulse, sr)

    if normalize and np.max(np.abs(output)) > 1e-10:
        output = output / np.max(np.abs(output))

    return output


def capture_ir_averaged(params: Velvet90Params,
                        sr: int = 48000,
                        duration_s: float = 6.0,
                        n_averages: int = 1,
                        vst_path: str = None) -> np.ndarray:
    """
    Capture IR with optional averaging to reduce modulation artifacts.

    For reverbs with heavy modulation (Chorus Space, Random Space),
    averaging multiple captures can provide a more stable reference.
    """
    irs = []
    for _ in range(n_averages):
        ir = capture_ir(params, sr, duration_s, vst_path, normalize=False)
        irs.append(ir)

    avg = np.mean(irs, axis=0)
    peak = np.max(np.abs(avg))
    if peak > 1e-10:
        avg = avg / peak
    return avg


# Pre-built parameter configurations for common starting points
def default_params_for_category(category: str) -> Velvet90Params:
    """Get sensible default Velvet 90 parameters for a PCM 90 category."""
    if category == 'Halls':
        return Velvet90Params(
            mode='Hall', color='1980s', size='2.0s', room_size=60.0,
            damping=45.0, pre_delay_ms=15.0, mod_rate_hz=0.8, mod_depth=25.0,
            width=80.0, early_diff=55.0, late_diff=60.0, er_late='E40/L59',
            bass_mult_x=1.2, bass_freq_hz=400.0, hf_decay_x=0.8,
        )
    elif category == 'Plates':
        return Velvet90Params(
            mode='Plate', color='1980s', size='1.5s', room_size=40.0,
            damping=35.0, pre_delay_ms=0.0, mod_rate_hz=1.2, mod_depth=20.0,
            width=90.0, early_diff=70.0, late_diff=65.0, er_late='E30/L70',
            bass_mult_x=0.9, bass_freq_hz=300.0, hf_decay_x=1.0,
        )
    elif category == 'Rooms':
        return Velvet90Params(
            mode='Room', color='1980s', size='1.0s', room_size=35.0,
            damping=50.0, pre_delay_ms=5.0, mod_rate_hz=0.5, mod_depth=15.0,
            width=70.0, early_diff=45.0, late_diff=50.0, er_late='E60/L39',
            bass_mult_x=1.1, bass_freq_hz=350.0, hf_decay_x=0.9,
        )
    else:  # Post / creative
        return Velvet90Params(
            mode='Chamber', color='Now', size='1.5s', room_size=50.0,
            damping=50.0, pre_delay_ms=10.0, mod_rate_hz=1.0, mod_depth=30.0,
            width=80.0, early_diff=50.0, late_diff=50.0, er_late='E50/L50',
        )
