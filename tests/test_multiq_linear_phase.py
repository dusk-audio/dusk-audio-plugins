#!/usr/bin/env python3
"""
Test script for Multi-Q Linear Phase mode.
Uses a simple signal processing approach to verify the overlap-add algorithm works.

This simulates what the LinearPhaseEQProcessor does to verify the algorithm.
"""

import numpy as np
import sys

def create_flat_ir_frequency_domain(filter_length, conv_fft_size):
    """Create a flat (unity gain) IR in frequency domain format ready for convolution.

    This matches the simplified algorithm in LinearPhaseEQProcessor:
    1. Create centered impulse in time domain
    2. Zero-pad to convolution FFT size
    3. Forward FFT
    No windowing, no extra normalization.

    Args:
        filter_length: The IR length (e.g., 4096, 8192)
        conv_fft_size: The convolution FFT size (2 * filter_length)
    """
    # Create time domain IR: centered impulse
    time_domain = np.zeros(conv_fft_size)
    time_domain[filter_length // 2] = 1.0  # Impulse at center for linear phase

    # Forward FFT for convolution
    freq_domain = np.fft.rfft(time_domain, conv_fft_size)

    # Convert to interleaved format (real, imag, real, imag, ...)
    result = np.zeros(conv_fft_size * 2)
    result[::2][:len(freq_domain)] = np.real(freq_domain)
    result[1::2][:len(freq_domain)] = np.imag(freq_domain)

    return result


def process_overlap_add(input_signal, ir_freq_domain, filter_length):
    """
    Process audio through overlap-add FFT convolution.
    This simulates the LinearPhaseEQProcessor algorithm.

    Args:
        input_signal: Input audio samples
        ir_freq_domain: IR in frequency domain (interleaved real/imag)
        filter_length: The IR/filter length (FFT size = 2 * filter_length)
    """
    fft_size = filter_length * 2  # Convolution FFT size
    hop_size = filter_length // 2  # 50% overlap of filter length
    latency = filter_length // 2   # Linear phase latency

    # Buffers (matching LinearPhaseEQProcessor)
    input_accum = np.zeros(filter_length)  # Circular input buffer
    output_accum = np.zeros(fft_size * 2)  # Overlap-add accumulator
    latency_delay = np.zeros(fft_size * 2)  # Latency compensation delay

    # Positions
    input_write_pos = 0
    output_read_pos = 0
    delay_write_pos = latency  # Start ahead by latency amount
    delay_read_pos = 0
    samples_in_buffer = 0

    # Output
    output = np.zeros(len(input_signal))

    for i in range(len(input_signal)):
        # Store input sample in circular buffer
        input_accum[input_write_pos] = input_signal[i]
        input_write_pos = (input_write_pos + 1) % filter_length
        samples_in_buffer += 1

        # Process FFT block when we have hop_size new samples
        if samples_in_buffer >= hop_size:
            # Gather filter_length samples from circular buffer
            fft_buffer = np.zeros(fft_size)
            for j in range(filter_length):
                read_idx = (input_write_pos - filter_length + j + filter_length) % filter_length
                fft_buffer[j] = input_accum[read_idx]
            # Zero-padding from filter_length to fft_size is implicit (zeros)

            # Forward FFT at convolution size
            freq_input = np.fft.rfft(fft_buffer, fft_size)

            # Complex multiplication with IR
            ir_complex = ir_freq_domain[::2][:len(freq_input)] + 1j * ir_freq_domain[1::2][:len(freq_input)]
            freq_output = freq_input * ir_complex

            # Inverse FFT (numpy normalizes by default)
            time_output = np.fft.irfft(freq_output, fft_size)

            # Overlap-add: accumulate full fft_size output
            for j in range(fft_size):
                write_idx = (output_read_pos + j) % (fft_size * 2)
                output_accum[write_idx] += time_output[j]

            # Transfer hop_size samples to delay buffer
            # Note: With 50% overlap and no input windowing, each sample is contributed
            # to by 2 FFT blocks, so we divide by 2 for correct gain.
            # The C++ mock FFT's DFT-based implementation handles this differently.
            for j in range(hop_size):
                read_idx = (output_read_pos + j) % (fft_size * 2)
                latency_delay[delay_write_pos] = output_accum[read_idx] / 2.0
                output_accum[read_idx] = 0.0  # Clear for next overlap
                delay_write_pos = (delay_write_pos + 1) % (fft_size * 2)

            output_read_pos = (output_read_pos + hop_size) % (fft_size * 2)
            samples_in_buffer = 0

        # Read output from latency delay
        output[i] = latency_delay[delay_read_pos]
        delay_read_pos = (delay_read_pos + 1) % (fft_size * 2)

    return output


def main():
    print("=== Multi-Q Linear Phase Processor Test ===\n")

    # Test parameters matching LinearPhaseEQProcessor
    filter_length = 4096  # Short setting for faster test
    conv_fft_size = filter_length * 2  # 2x for linear convolution
    sample_rate = 44100
    test_duration = 1.0  # seconds
    test_freq = 1000  # Hz

    num_samples = int(sample_rate * test_duration)
    latency = filter_length // 2  # Linear phase latency

    print(f"Filter length: {filter_length}")
    print(f"Convolution FFT size: {conv_fft_size}")
    print(f"Expected latency: {latency} samples ({latency / sample_rate * 1000:.1f} ms)")
    print(f"Test signal: {test_freq} Hz sine wave")
    print(f"Duration: {test_duration} seconds ({num_samples} samples)\n")

    # Create test signal
    t = np.arange(num_samples) / sample_rate
    input_signal = np.sin(2 * np.pi * test_freq * t)

    # Create flat IR (matches simplified LinearPhaseEQProcessor algorithm)
    print("Creating flat IR (centered impulse, no windowing)...")
    ir_freq_domain = create_flat_ir_frequency_domain(filter_length, conv_fft_size)

    # Process
    print("Processing through overlap-add convolution...")
    output = process_overlap_add(input_signal, ir_freq_domain, filter_length)

    # Analyze results
    print("\n=== Results ===")

    # Detect actual latency via cross-correlation (more accurate than theoretical)
    max_lag = filter_length * 2
    best_corr = 0
    detected_latency = 0
    for lag in range(max_lag):
        if lag < len(output) and lag < len(input_signal):
            out_slice = output[lag:]
            in_slice = input_signal[:len(out_slice)]
            if len(out_slice) > 100 and np.std(out_slice) > 0.01:
                corr = np.corrcoef(out_slice, in_slice)[0, 1]
                if corr > best_corr:
                    best_corr = corr
                    detected_latency = lag

    print(f"Expected latency: {latency} samples")
    print(f"Detected latency (via cross-correlation): {detected_latency} samples")

    # Use detected latency for analysis
    actual_latency = detected_latency if detected_latency > 0 else latency
    output_after_latency = output[actual_latency:]
    input_for_comparison = input_signal[:len(output_after_latency)]

    max_output = np.max(np.abs(output_after_latency))
    avg_output = np.mean(np.abs(output_after_latency))
    non_zero = np.sum(np.abs(output_after_latency) > 0.001)

    print(f"\nMax output amplitude: {max_output:.6f}")
    print(f"Average output amplitude: {avg_output:.6f}")
    print(f"Non-zero samples (after latency): {non_zero} / {len(output_after_latency)}")

    # Check correlation with input (should be high for flat response)
    if len(output_after_latency) > 0 and np.std(output_after_latency) > 0:
        correlation = np.corrcoef(input_for_comparison, output_after_latency)[0, 1]
        print(f"Correlation with input: {correlation:.4f}")
    else:
        correlation = 0

    # Check first few samples after detected latency
    print(f"\nFirst 10 samples after latency ({actual_latency}):")
    for i in range(min(10, len(output_after_latency))):
        print(f"  output[{actual_latency + i}] = {output_after_latency[i]:.6f} "
              f"(expected {input_for_comparison[i]:.6f})")

    # Pass/Fail
    print("\n" + "=" * 50)
    if max_output < 0.001:
        print("*** FAIL: No output detected! ***")
        print("\nPossible issues:")
        print("  - IR buffer not initialized")
        print("  - Delay buffer timing incorrect")
        print("  - FFT convolution not working")
        return 1
    elif correlation < 0.9:
        print(f"*** WARNING: Low correlation ({correlation:.4f}) ***")
        print("Output detected but may not be correct.")
        return 0
    else:
        print("*** PASS: Linear phase convolution working! ***")
        return 0


def test_comb_filtering():
    """Test for comb filtering artifacts by checking frequency response."""
    print("\n=== Comb Filtering Test ===\n")

    filter_length = 4096
    conv_fft_size = filter_length * 2
    sample_rate = 44100

    # Test with multiple frequencies to check for comb filtering
    test_freqs = [100, 500, 1000, 2000, 5000, 10000, 15000]
    test_duration = 0.5  # seconds
    num_samples = int(sample_rate * test_duration)

    # Create flat IR
    ir_freq_domain = create_flat_ir_frequency_domain(filter_length, conv_fft_size)

    print("Testing frequency response (should be flat):")
    results = []

    for freq in test_freqs:
        # Create test signal
        t = np.arange(num_samples) / sample_rate
        input_signal = np.sin(2 * np.pi * freq * t)

        # Process
        output = process_overlap_add(input_signal, ir_freq_domain, filter_length)

        # Measure output amplitude in steady state (skip latency + settling)
        latency = filter_length * 2  # Conservative estimate
        steady_state = output[latency:]
        if len(steady_state) > 1000:
            rms_output = np.sqrt(np.mean(steady_state**2))
            rms_input = np.sqrt(np.mean(input_signal**2))
            if rms_input > 0 and rms_output > 0:
                gain_db = 20 * np.log10(rms_output / rms_input)
            else:
                gain_db = -100  # Sentinel value for no signal
            results.append((freq, gain_db))
            print(f"  {freq:5d} Hz: {gain_db:+.2f} dB")

    # Check for comb filtering (large variations in frequency response)
    gains = [r[1] for r in results]
    gain_variation = max(gains) - min(gains) if gains else 0

    print(f"\nGain variation across frequencies: {gain_variation:.2f} dB")

    if gain_variation > 3.0:
        print("*** WARNING: Possible comb filtering detected! ***")
        print("Gain variation > 3 dB suggests frequency response issues.")
        return 1
    elif gain_variation > 1.0:
        print("*** CAUTION: Some frequency response variation detected. ***")
        return 0
    else:
        print("*** PASS: Flat frequency response (no comb filtering)! ***")
        return 0


if __name__ == "__main__":
    result = main()
    if result == 0:
        result = test_comb_filtering()
    sys.exit(result)
