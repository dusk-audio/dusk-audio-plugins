#!/usr/bin/env python3
"""
Dusk Audio Plugin Audio Analyzer
=====================================
Performs comprehensive audio analysis similar to DDMF PluginDoctor:
- Harmonic distortion analysis (THD, THD+N)
- Frequency response measurement
- Phase response analysis
- Aliasing detection
- Null testing for bypass verification
- Transient response analysis

This script generates test signals, processes them through plugins,
and analyzes the results.
"""

import numpy as np
import scipy.signal as signal
import scipy.fft as fft
import json
import os
import sys
import argparse
from dataclasses import dataclass, asdict
from typing import List, Tuple, Optional, Dict
import wave
import struct
from pathlib import Path


@dataclass
class TestResult:
    """Container for test results"""
    test_name: str
    passed: bool
    value: float
    unit: str
    threshold: float
    details: str = ""


@dataclass
class PluginAnalysisReport:
    """Complete analysis report for a plugin"""
    plugin_name: str
    sample_rate: int
    test_results: List[TestResult]
    timestamp: str


class AudioTestGenerator:
    """Generates various test signals for audio analysis"""

    def __init__(self, sample_rate: int = 48000):
        self.sample_rate = sample_rate

    def sine_wave(self, frequency: float, duration: float, amplitude: float = 0.5) -> np.ndarray:
        """Generate a pure sine wave"""
        t = np.arange(int(duration * self.sample_rate)) / self.sample_rate
        return amplitude * np.sin(2 * np.pi * frequency * t)

    def multi_tone(self, frequencies: List[float], duration: float, amplitude: float = 0.3) -> np.ndarray:
        """Generate multiple sine waves summed together"""
        t = np.arange(int(duration * self.sample_rate)) / self.sample_rate
        signal = np.zeros_like(t)
        for freq in frequencies:
            signal += amplitude * np.sin(2 * np.pi * freq * t)
        return signal / len(frequencies)

    def sweep(self, start_freq: float, end_freq: float, duration: float, amplitude: float = 0.5) -> np.ndarray:
        """Generate a logarithmic frequency sweep"""
        t = np.arange(int(duration * self.sample_rate)) / self.sample_rate
        return amplitude * signal.chirp(t, start_freq, duration, end_freq, method='logarithmic')

    def impulse(self, duration: float, amplitude: float = 0.9) -> np.ndarray:
        """Generate an impulse (for measuring impulse response)"""
        samples = int(duration * self.sample_rate)
        imp = np.zeros(samples)
        imp[samples // 10] = amplitude  # Place impulse at 10% into buffer
        return imp

    def white_noise(self, duration: float, amplitude: float = 0.3) -> np.ndarray:
        """Generate white noise"""
        samples = int(duration * self.sample_rate)
        return amplitude * np.random.randn(samples)

    def pink_noise(self, duration: float, amplitude: float = 0.3) -> np.ndarray:
        """Generate pink noise (1/f spectrum)"""
        samples = int(duration * self.sample_rate)
        white = np.random.randn(samples)
        # Apply 1/f filter
        b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
        a = [1, -2.494956002, 2.017265875, -0.522189400]
        return amplitude * signal.lfilter(b, a, white)

    def silence(self, duration: float) -> np.ndarray:
        """Generate silence (for noise floor testing)"""
        return np.zeros(int(duration * self.sample_rate))

    def step(self, duration: float, amplitude: float = 0.5) -> np.ndarray:
        """Generate a step function (for transient analysis)"""
        samples = int(duration * self.sample_rate)
        step = np.zeros(samples)
        step[samples // 4:] = amplitude
        return step


class AudioAnalyzer:
    """Performs audio analysis on processed signals"""

    def __init__(self, sample_rate: int = 48000):
        self.sample_rate = sample_rate

    def compute_spectrum(self, audio: np.ndarray, window: str = 'hann') -> Tuple[np.ndarray, np.ndarray]:
        """Compute magnitude spectrum in dB"""
        n = len(audio)
        win = signal.get_window(window, n)
        windowed = audio * win

        spectrum = np.abs(fft.rfft(windowed))
        spectrum_db = 20 * np.log10(spectrum + 1e-10)

        frequencies = fft.rfftfreq(n, 1 / self.sample_rate)
        return frequencies, spectrum_db

    def find_harmonics(self, audio: np.ndarray, fundamental: float, num_harmonics: int = 10) -> List[Tuple[float, float]]:
        """Find harmonic amplitudes relative to fundamental"""
        frequencies, spectrum_db = self.compute_spectrum(audio)

        harmonics = []
        freq_resolution = frequencies[1] - frequencies[0]

        for h in range(1, num_harmonics + 1):
            target_freq = fundamental * h
            if target_freq > self.sample_rate / 2:
                break

            # Find closest bin
            idx = int(target_freq / freq_resolution)
            if idx < len(spectrum_db):
                # Use peak in neighborhood
                start = max(0, idx - 2)
                end = min(len(spectrum_db), idx + 3)
                peak_db = np.max(spectrum_db[start:end])
                harmonics.append((target_freq, peak_db))

        return harmonics

    def calculate_thd(self, audio: np.ndarray, fundamental: float) -> float:
        """Calculate Total Harmonic Distortion (THD) in percent"""
        harmonics = self.find_harmonics(audio, fundamental, num_harmonics=10)

        if len(harmonics) < 2:
            return 0.0

        # Convert from dB to linear
        fundamental_amp = 10 ** (harmonics[0][1] / 20)
        harmonic_power = sum((10 ** (h[1] / 20)) ** 2 for h in harmonics[1:])

        thd = 100 * np.sqrt(harmonic_power) / fundamental_amp
        return thd

    def calculate_thd_plus_noise(self, audio: np.ndarray, fundamental: float) -> float:
        """Calculate THD+N (Total Harmonic Distortion plus Noise) in percent"""
        frequencies, spectrum_db = self.compute_spectrum(audio)

        # Find fundamental
        freq_resolution = frequencies[1] - frequencies[0]
        fund_idx = int(fundamental / freq_resolution)

        # Total signal power
        spectrum_linear = 10 ** (spectrum_db / 20)
        total_power = np.sum(spectrum_linear ** 2)

        # Fundamental power (include nearby bins)
        fund_start = max(0, fund_idx - 3)
        fund_end = min(len(spectrum_linear), fund_idx + 4)
        fund_power = np.sum(spectrum_linear[fund_start:fund_end] ** 2)

        # THD+N is everything except fundamental
        noise_plus_harmonics = total_power - fund_power
        thd_n = 100 * np.sqrt(noise_plus_harmonics / fund_power) if fund_power > 0 else 100

        return thd_n

    def calculate_noise_floor(self, audio: np.ndarray) -> float:
        """Calculate RMS noise floor in dB"""
        rms = np.sqrt(np.mean(audio ** 2))
        return 20 * np.log10(rms + 1e-10)

    def calculate_frequency_response(self, input_sweep: np.ndarray, output_sweep: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Calculate frequency response from sweep test"""
        # Use FFT division for frequency response
        input_fft = fft.rfft(input_sweep)
        output_fft = fft.rfft(output_sweep)

        # Avoid division by zero
        epsilon = 1e-10
        response = output_fft / (input_fft + epsilon)
        response_db = 20 * np.log10(np.abs(response) + epsilon)

        frequencies = fft.rfftfreq(len(input_sweep), 1 / self.sample_rate)
        return frequencies, response_db

    def calculate_phase_response(self, input_signal: np.ndarray, output_signal: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Calculate phase response"""
        input_fft = fft.rfft(input_signal)
        output_fft = fft.rfft(output_signal)

        phase = np.angle(output_fft) - np.angle(input_fft)
        phase = np.unwrap(phase)
        phase_degrees = np.degrees(phase)

        frequencies = fft.rfftfreq(len(input_signal), 1 / self.sample_rate)
        return frequencies, phase_degrees

    def null_test(self, original: np.ndarray, processed: np.ndarray) -> float:
        """
        Perform null test - return residual level in dB.
        For a perfect null, the result would be -infinity.
        For bypass testing, residual should be < -120 dB.
        """
        # Align signals (in case of latency)
        correlation = signal.correlate(processed, original, mode='full')
        lag = np.argmax(np.abs(correlation)) - len(original) + 1

        # Check for extreme lag values where no meaningful overlap is possible
        min_signal_len = min(len(original), len(processed))
        if abs(lag) >= min_signal_len:
            # No meaningful overlap - return 0 dB (signals completely different)
            return 0.0

        if lag > 0:
            # Positive lag: processed is delayed relative to original
            # Drop first 'lag' samples from processed, drop last 'lag' from original
            aligned_orig = original[:-lag]
            aligned_proc = processed[lag:]
        elif lag < 0:
            # Negative lag: original is delayed relative to processed
            # Drop first '-lag' samples from original, drop last '-lag' from processed
            abs_lag = -lag
            aligned_orig = original[abs_lag:]
            aligned_proc = processed[:-abs_lag]
        else:
            aligned_orig = original
            aligned_proc = processed

        # Trim to same length (should already be equal, but safety check)
        min_len = min(len(aligned_orig), len(aligned_proc))
        if min_len == 0:
            # No overlap after alignment - signals are completely different
            return 0.0
        aligned_orig = aligned_orig[:min_len]
        aligned_proc = aligned_proc[:min_len]

        # Calculate residual
        residual = aligned_proc - aligned_orig
        residual_rms = np.sqrt(np.mean(residual ** 2))
        original_rms = np.sqrt(np.mean(aligned_orig ** 2))

        if original_rms > 0:
            null_db = 20 * np.log10(residual_rms / original_rms + 1e-15)
        else:
            null_db = 20 * np.log10(residual_rms + 1e-15)

        return null_db

    def detect_aliasing(self, audio: np.ndarray, test_frequency: float) -> bool:
        """
        Detect aliasing by looking for unexpected frequency content.
        Test with a high frequency sine wave and check for mirror frequencies.
        """
        frequencies, spectrum_db = self.compute_spectrum(audio)

        # Expected harmonic frequencies
        expected_freqs = set()
        for h in range(1, 20):
            freq = test_frequency * h
            if freq < self.sample_rate / 2:
                expected_freqs.add(int(freq))
            # Also add aliased frequencies
            aliased = self.sample_rate - freq
            if 0 < aliased < self.sample_rate / 2:
                expected_freqs.add(int(aliased))

        # Find peaks above noise floor
        noise_floor = np.median(spectrum_db)
        threshold = noise_floor + 20  # 20 dB above noise floor

        peaks = []
        freq_resolution = frequencies[1] - frequencies[0]

        for i in range(1, len(spectrum_db) - 1):
            if spectrum_db[i] > threshold:
                if spectrum_db[i] > spectrum_db[i-1] and spectrum_db[i] > spectrum_db[i+1]:
                    peak_freq = frequencies[i]
                    # Check if this is an unexpected frequency
                    is_expected = any(abs(peak_freq - ef) < freq_resolution * 3 for ef in expected_freqs)
                    if not is_expected and peak_freq > 100:  # Ignore DC/low freq
                        peaks.append(peak_freq)

        return len(peaks) > 0  # Aliasing detected if unexpected peaks found

    def measure_latency(self, input_signal: np.ndarray, output_signal: np.ndarray) -> int:
        """Measure plugin latency in samples"""
        correlation = signal.correlate(output_signal, input_signal, mode='full')
        lag = np.argmax(np.abs(correlation)) - len(input_signal) + 1
        return max(0, lag)


class PluginTester:
    """Main class for testing plugins"""

    def __init__(self, sample_rate: int = 48000, output_dir: str = "./test_output"):
        self.sample_rate = sample_rate
        self.generator = AudioTestGenerator(sample_rate)
        self.analyzer = AudioAnalyzer(sample_rate)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def save_wav(self, filename: str, audio: np.ndarray):
        """Save audio to WAV file"""
        filepath = self.output_dir / filename
        audio_int16 = (audio * 32767).astype(np.int16)
        with wave.open(str(filepath), 'w') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(self.sample_rate)
            wav.writeframes(audio_int16.tobytes())

    def load_wav(self, filename: str) -> np.ndarray:
        """Load audio from WAV file"""
        filepath = self.output_dir / filename
        with wave.open(str(filepath), 'r') as wav:
            frames = wav.readframes(wav.getnframes())
            audio = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32767
            return audio

    def generate_test_files(self, plugin_name: str) -> Dict[str, str]:
        """Generate all test signal files for a plugin"""
        prefix = plugin_name.replace(" ", "_").lower()
        test_files = {}

        # 1. 1kHz sine for THD testing
        sine_1k = self.generator.sine_wave(1000, 2.0, 0.5)
        filename = f"{prefix}_test_sine_1khz.wav"
        self.save_wav(filename, sine_1k)
        test_files['sine_1khz'] = filename

        # 2. High frequency sine for aliasing test
        sine_18k = self.generator.sine_wave(18000, 1.0, 0.5)
        filename = f"{prefix}_test_sine_18khz.wav"
        self.save_wav(filename, sine_18k)
        test_files['sine_18khz'] = filename

        # 3. Frequency sweep for response measurement
        sweep = self.generator.sweep(20, 20000, 5.0, 0.5)
        filename = f"{prefix}_test_sweep.wav"
        self.save_wav(filename, sweep)
        test_files['sweep'] = filename

        # 4. Impulse for latency and IR measurement
        impulse = self.generator.impulse(1.0, 0.9)
        filename = f"{prefix}_test_impulse.wav"
        self.save_wav(filename, impulse)
        test_files['impulse'] = filename

        # 5. Silence for noise floor testing
        silence = self.generator.silence(2.0)
        filename = f"{prefix}_test_silence.wav"
        self.save_wav(filename, silence)
        test_files['silence'] = filename

        # 6. Multi-tone for IMD testing
        multitone = self.generator.multi_tone([60, 7000], 2.0, 0.4)
        filename = f"{prefix}_test_multitone.wav"
        self.save_wav(filename, multitone)
        test_files['multitone'] = filename

        # 7. Pink noise for general testing
        pink = self.generator.pink_noise(3.0, 0.3)
        filename = f"{prefix}_test_pink_noise.wav"
        self.save_wav(filename, pink)
        test_files['pink_noise'] = filename

        return test_files

    def analyze_processed_files(self, plugin_name: str, test_files: Dict[str, str],
                                 processed_suffix: str = "_processed") -> List[TestResult]:
        """Analyze processed files and return test results"""
        results = []
        prefix = plugin_name.replace(" ", "_").lower()

        # THD Test (1kHz sine)
        try:
            input_file = test_files.get('sine_1khz', '')
            output_file = input_file.replace('.wav', f'{processed_suffix}.wav')

            if os.path.exists(self.output_dir / output_file):
                processed = self.load_wav(output_file)
                thd = self.analyzer.calculate_thd(processed, 1000)
                results.append(TestResult(
                    test_name="THD at 1kHz",
                    passed=thd < 1.0,  # Less than 1% THD
                    value=thd,
                    unit="%",
                    threshold=1.0,
                    details="Total Harmonic Distortion at 1kHz"
                ))
        except Exception as e:
            results.append(TestResult(
                test_name="THD at 1kHz",
                passed=False,
                value=0,
                unit="%",
                threshold=1.0,
                details=f"Error: {str(e)}"
            ))

        # Aliasing Test (18kHz sine)
        try:
            input_file = test_files.get('sine_18khz', '')
            output_file = input_file.replace('.wav', f'{processed_suffix}.wav')

            if os.path.exists(self.output_dir / output_file):
                processed = self.load_wav(output_file)
                has_aliasing = self.analyzer.detect_aliasing(processed, 18000)
                results.append(TestResult(
                    test_name="Aliasing Detection (18kHz)",
                    passed=not has_aliasing,
                    value=1.0 if has_aliasing else 0.0,
                    unit="bool",
                    threshold=0.5,
                    details="Checks for aliasing artifacts at high frequencies"
                ))
        except Exception as e:
            results.append(TestResult(
                test_name="Aliasing Detection (18kHz)",
                passed=False,
                value=1.0,
                unit="bool",
                threshold=0.5,
                details=f"Error: {str(e)}"
            ))

        # Noise Floor Test (silence input)
        try:
            input_file = test_files.get('silence', '')
            output_file = input_file.replace('.wav', f'{processed_suffix}.wav')

            if os.path.exists(self.output_dir / output_file):
                processed = self.load_wav(output_file)
                noise_floor = self.analyzer.calculate_noise_floor(processed)
                results.append(TestResult(
                    test_name="Noise Floor",
                    passed=noise_floor < -80,  # Below -80 dB
                    value=noise_floor,
                    unit="dB",
                    threshold=-80,
                    details="Self-noise of the plugin with silent input"
                ))
        except Exception as e:
            results.append(TestResult(
                test_name="Noise Floor",
                passed=False,
                value=0,
                unit="dB",
                threshold=-80,
                details=f"Error: {str(e)}"
            ))

        # Null Test (for bypass verification - requires bypass processed file)
        try:
            input_file = test_files.get('pink_noise', '')
            bypass_file = input_file.replace('.wav', '_bypass.wav')

            if os.path.exists(self.output_dir / bypass_file):
                original = self.load_wav(input_file)
                bypassed = self.load_wav(bypass_file)
                null_db = self.analyzer.null_test(original, bypassed)
                results.append(TestResult(
                    test_name="Bypass Null Test",
                    passed=null_db < -100,  # Should be essentially silent
                    value=null_db,
                    unit="dB",
                    threshold=-100,
                    details="Residual signal when plugin is bypassed (should be silent)"
                ))
        except Exception as e:
            results.append(TestResult(
                test_name="Bypass Null Test",
                passed=False,
                value=0,
                unit="dB",
                threshold=-100,
                details=f"Error or bypass file not found: {str(e)}"
            ))

        # Latency Test (impulse)
        try:
            input_file = test_files.get('impulse', '')
            output_file = input_file.replace('.wav', f'{processed_suffix}.wav')

            if os.path.exists(self.output_dir / output_file):
                original = self.load_wav(input_file)
                processed = self.load_wav(output_file)
                latency = self.analyzer.measure_latency(original, processed)
                latency_ms = latency * 1000 / self.sample_rate
                results.append(TestResult(
                    test_name="Plugin Latency",
                    passed=True,  # Just informational
                    value=latency_ms,
                    unit="ms",
                    threshold=100,  # Just for reference
                    details=f"Measured latency: {latency} samples at {self.sample_rate} Hz"
                ))
        except Exception as e:
            results.append(TestResult(
                test_name="Plugin Latency",
                passed=True,
                value=0,
                unit="ms",
                threshold=100,
                details=f"Could not measure: {str(e)}"
            ))

        return results

    def run_offline_analysis(self, plugin_name: str) -> PluginAnalysisReport:
        """Run complete offline analysis on a plugin"""
        from datetime import datetime

        print(f"\n{'='*60}")
        print(f"Audio Analysis: {plugin_name}")
        print(f"{'='*60}")

        # Generate test files
        print("\nGenerating test signals...")
        test_files = self.generate_test_files(plugin_name)

        print(f"Test files saved to: {self.output_dir}")
        print("\nGenerated files:")
        for name, filename in test_files.items():
            print(f"  - {name}: {filename}")

        print("\n" + "-"*40)
        print("INSTRUCTIONS FOR MANUAL TESTING:")
        print("-"*40)
        print("1. Load the plugin in your DAW")
        print("2. Process each test file through the plugin")
        print("3. Save the output with '_processed' suffix")
        print("4. For bypass test, save with '_bypass' suffix")
        print("5. Run this script again with --analyze flag")
        print("-"*40)

        # Try to analyze any existing processed files
        results = self.analyze_processed_files(plugin_name, test_files)

        report = PluginAnalysisReport(
            plugin_name=plugin_name,
            sample_rate=self.sample_rate,
            test_results=results,
            timestamp=datetime.now().isoformat()
        )

        # Save report
        report_file = self.output_dir / f"{plugin_name.replace(' ', '_').lower()}_report.json"
        with open(report_file, 'w') as f:
            json.dump(asdict(report), f, indent=2)
        print(f"\nReport saved to: {report_file}")

        # Print results
        if results:
            print("\n" + "="*60)
            print("TEST RESULTS:")
            print("="*60)
            for result in results:
                status = "[PASS]" if result.passed else "[FAIL]"
                print(f"{status} {result.test_name}: {result.value:.4f} {result.unit} (threshold: {result.threshold} {result.unit})")
                if result.details:
                    print(f"       {result.details}")

        return report


def main():
    parser = argparse.ArgumentParser(description="Dusk Audio Plugin Analyzer")
    parser.add_argument("--plugin", type=str, default="Universal Compressor",
                       help="Plugin name to test")
    parser.add_argument("--output-dir", type=str, default="./test_output",
                       help="Directory for test files and results")
    parser.add_argument("--sample-rate", type=int, default=48000,
                       help="Sample rate for test signals")
    parser.add_argument("--analyze", action="store_true",
                       help="Analyze existing processed files")

    args = parser.parse_args()

    tester = PluginTester(
        sample_rate=args.sample_rate,
        output_dir=args.output_dir
    )

    report = tester.run_offline_analysis(args.plugin)

    # Return exit code based on results
    failed = sum(1 for r in report.test_results if not r.passed)
    return 1 if failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
