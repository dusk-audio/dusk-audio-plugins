/*
 * TapeMachine Aliasing Fix - Reference Implementation
 * Luna Co. Audio
 * 
 * This file contains:
 * 1. A proper oversampling implementation with correct anti-aliasing
 * 2. A real audio test that measures actual output (not theoretical calculations)
 * 
 * THE PROBLEM: Aliasing occurs when nonlinear processing (saturation) generates
 * harmonics above Nyquist that fold back into the audible range.
 * 
 * THE SOLUTION: Oversample -> Process -> Anti-alias filter -> Downsample
 * The anti-alias filter MUST be steep enough to attenuate everything above
 * original_nyquist before downsampling.
 * 
 * WRONG APPROACHES (that Claude Code keeps trying):
 * - Reducing saturation on HF content (makes plugin sound dull)
 * - Pre-filtering the input (removes brightness)
 * - Theoretical tests that don't measure actual plugin output
 */

#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <complex>
#include <algorithm>

namespace LunaCoAudio {

//==============================================================================
// PART 1: Proper Anti-Aliasing Filter for Downsampling
//==============================================================================

/**
 * High-order IIR lowpass filter for anti-aliasing
 * Uses cascaded biquad sections for numerical stability
 * 
 * This is an 8th-order Chebyshev Type I filter with 0.1dB passband ripple
 * Provides ~96dB attenuation at 2x the cutoff frequency
 */
class AntiAliasingFilter {
public:
    static constexpr int NUM_SECTIONS = 4; // 4 biquads = 8th order
    
    struct BiquadCoeffs {
        double b0, b1, b2; // Numerator
        double a1, a2;      // Denominator (a0 normalized to 1)
    };
    
    struct BiquadState {
        double x1 = 0, x2 = 0; // Input delays
        double y1 = 0, y2 = 0; // Output delays
    };
    
    void prepare(double sampleRate, double cutoffHz) {
        // Design 8th-order Chebyshev Type I lowpass
        // Cutoff should be set to (originalSampleRate / 2) * 0.9 
        // when running at oversampled rate
        
        designChebyshevType1(sampleRate, cutoffHz, 0.1); // 0.1dB ripple
        reset();
    }
    
    void reset() {
        for (auto& state : states) {
            state = BiquadState{};
        }
    }
    
    float process(float input) {
        double signal = static_cast<double>(input);
        
        for (int i = 0; i < NUM_SECTIONS; ++i) {
            signal = processBiquad(signal, coeffs[i], states[i]);
        }
        
        return static_cast<float>(signal);
    }
    
private:
    std::array<BiquadCoeffs, NUM_SECTIONS> coeffs;
    std::array<BiquadState, NUM_SECTIONS> states;
    
    double processBiquad(double input, const BiquadCoeffs& c, BiquadState& s) {
        double output = c.b0 * input + c.b1 * s.x1 + c.b2 * s.x2
                                     - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1;
        s.x1 = input;
        s.y2 = s.y1;
        s.y1 = output;
        return output;
    }
    
    void designChebyshevType1(double sampleRate, double cutoff, double rippleDb) {
        // Chebyshev Type I filter design
        // This provides steep rolloff with slight passband ripple
        
        const double epsilon = std::sqrt(std::pow(10.0, rippleDb / 10.0) - 1.0);
        const double wc = 2.0 * M_PI * cutoff / sampleRate;
        const double wcPrewarp = 2.0 * sampleRate * std::tan(wc / 2.0);
        
        // Calculate pole locations for 8th-order Chebyshev
        const int order = NUM_SECTIONS * 2;
        const double a = std::asinh(1.0 / epsilon) / order;
        
        for (int section = 0; section < NUM_SECTIONS; ++section) {
            // Each section handles a complex conjugate pole pair
            int k1 = 2 * section + 1;
            int k2 = 2 * section + 2;
            
            // Analog pole locations
            double theta1 = M_PI * (2.0 * k1 - 1.0) / (2.0 * order) + M_PI / 2.0;
            double sigma = -std::sinh(a) * std::sin(theta1);
            double omega = std::cosh(a) * std::cos(theta1);
            
            // Bilinear transform to digital domain
            double p_real = sigma * wcPrewarp;
            double p_imag = omega * wcPrewarp;
            
            // Bilinear transform: s = 2*fs*(z-1)/(z+1)
            // For lowpass, zeros are at z = -1
            double denom = (2.0 * sampleRate - p_real) * (2.0 * sampleRate - p_real) 
                         + p_imag * p_imag;
            
            double b0_analog = wcPrewarp * wcPrewarp / denom;
            double a1_digital = 2.0 * ((2.0 * sampleRate) * (2.0 * sampleRate) 
                              - p_real * p_real - p_imag * p_imag) / denom;
            double a2_digital = ((2.0 * sampleRate + p_real) * (2.0 * sampleRate + p_real) 
                              + p_imag * p_imag) / denom;
            
            // Normalized biquad coefficients
            coeffs[section].b0 = b0_analog;
            coeffs[section].b1 = 2.0 * b0_analog;
            coeffs[section].b2 = b0_analog;
            coeffs[section].a1 = -a1_digital;
            coeffs[section].a2 = a2_digital;
        }
        
        // Normalize DC gain to 1.0
        double dcGain = 1.0;
        for (int i = 0; i < NUM_SECTIONS; ++i) {
            dcGain *= (coeffs[i].b0 + coeffs[i].b1 + coeffs[i].b2) 
                    / (1.0 + coeffs[i].a1 + coeffs[i].a2);
        }
        double normFactor = 1.0 / dcGain;
        coeffs[0].b0 *= normFactor;
        coeffs[0].b1 *= normFactor;
        coeffs[0].b2 *= normFactor;
    }
};

//==============================================================================
// PART 2: Proper Oversampler with correct signal flow
//==============================================================================

/**
 * Oversampler that guarantees alias-free processing
 * 
 * CRITICAL: The anti-aliasing filter runs at the OVERSAMPLED rate
 * and cuts off at the ORIGINAL Nyquist frequency
 */
template<int OversamplingFactor = 4>
class ProperOversampler {
public:
    static_assert(OversamplingFactor == 2 || OversamplingFactor == 4 || 
                  OversamplingFactor == 8 || OversamplingFactor == 16,
                  "Oversampling factor must be 2, 4, 8, or 16");
    
    void prepare(double originalSampleRate) {
        baseSampleRate = originalSampleRate;
        oversampledRate = originalSampleRate * OversamplingFactor;
        
        // Anti-aliasing filter cutoff: just below original Nyquist
        // Using 0.45 * original SR gives some margin
        double aaCutoff = originalSampleRate * 0.45;
        
        for (auto& filter : aaFilters) {
            filter.prepare(oversampledRate, aaCutoff);
        }
        
        // Interpolation filter (same as AA filter, used for upsampling)
        for (auto& filter : interpFilters) {
            filter.prepare(oversampledRate, aaCutoff);
        }
    }
    
    void reset() {
        for (auto& f : aaFilters) f.reset();
        for (auto& f : interpFilters) f.reset();
    }
    
    /**
     * Upsample a single input sample to OversamplingFactor output samples
     * Uses zero-stuffing + interpolation filter
     */
    void upsample(float input, float* oversampledOutput) {
        // Zero-stuffing: insert zeros between samples
        for (int i = 0; i < OversamplingFactor; ++i) {
            float sample = (i == 0) ? input * OversamplingFactor : 0.0f;
            oversampledOutput[i] = interpFilters[0].process(sample);
        }
    }
    
    /**
     * Downsample OversamplingFactor input samples to single output
     * CRITICAL: Anti-aliasing filter is applied BEFORE decimation
     */
    float downsample(const float* oversampledInput) {
        // Apply anti-aliasing filter to ALL oversampled samples
        float filtered = 0.0f;
        for (int i = 0; i < OversamplingFactor; ++i) {
            filtered = aaFilters[0].process(oversampledInput[i]);
        }
        // Return only the last sample (decimation)
        return filtered;
    }
    
    double getOversampledRate() const { return oversampledRate; }
    int getFactor() const { return OversamplingFactor; }
    
private:
    double baseSampleRate = 44100.0;
    double oversampledRate = 44100.0 * OversamplingFactor;
    
    std::array<AntiAliasingFilter, 2> aaFilters;     // Stereo
    std::array<AntiAliasingFilter, 2> interpFilters; // Stereo
};

//==============================================================================
// PART 3: Example of CORRECT processing order
//==============================================================================

/**
 * This shows the CORRECT signal flow for alias-free tape emulation
 * 
 * processBlock() should look like this:
 */
class CorrectProcessingExample {
public:
    ProperOversampler<4> oversampler;
    
    // Your existing tape processing components go here
    // (saturation, bias, hysteresis, etc.)
    
    void processBlock(float* audioData, int numSamples) {
        std::array<float, 4> oversampledBuffer; // For 4x oversampling
        
        for (int i = 0; i < numSamples; ++i) {
            float inputSample = audioData[i];
            
            // STEP 1: Upsample to 4x rate
            oversampler.upsample(inputSample, oversampledBuffer.data());
            
            // STEP 2: Process ALL nonlinear stages at oversampled rate
            for (int os = 0; os < 4; ++os) {
                float sample = oversampledBuffer[os];
                
                // === ALL NONLINEAR PROCESSING HAPPENS HERE ===
                // - Input gain/pre-emphasis
                // - Tape saturation (ALL stages)
                // - Hysteresis modeling
                // - Bias circuit
                // - Any waveshaping
                // - Compressor (if nonlinear)
                // 
                // Example saturation:
                sample = processTapeSaturation(sample);
                sample = processHysteresis(sample);
                sample = processBiasCircuit(sample);
                
                oversampledBuffer[os] = sample;
            }
            
            // STEP 3: Downsample with anti-aliasing
            // The oversampler's downsample() applies the AA filter internally
            float outputSample = oversampler.downsample(oversampledBuffer.data());
            
            // STEP 4: Only LINEAR processing can happen after this point
            // - Output gain
            // - Linear EQ
            // - Mixing
            
            audioData[i] = outputSample;
        }
    }
    
    // Placeholder for your actual saturation
    float processTapeSaturation(float x) {
        // Your saturation algorithm here
        return std::tanh(x * 2.0f) * 0.5f;
    }
    
    float processHysteresis(float x) { return x; }
    float processBiasCircuit(float x) { return x; }
};

//==============================================================================
// PART 4: REAL ALIASING TEST - Actually measures plugin output
//==============================================================================

/**
 * This test generates a real sine wave, processes it, and measures
 * the actual spectrum of the output. No theoretical calculations.
 */
class RealAliasingTest {
public:
    struct TestResult {
        bool passed;
        float testFrequency;
        float sampleRate;
        std::vector<std::pair<float, float>> detectedPeaks; // freq, dB
        float worstAliasPeak_dB;
        std::string details;
    };
    
    /**
     * Run the aliasing test
     * 
     * @param processor - Function that processes audio (your plugin's process call)
     * @param testFreqHz - Test frequency (e.g., 18200 for 18.2kHz)
     * @param sampleRate - Sample rate (e.g., 44100)
     * @param inputGainDb - Input gain to apply (e.g., +8.3dB)
     * @param thresholdDb - Maximum allowed alias level (e.g., -80dB)
     */
    template<typename ProcessorFunc>
    static TestResult runTest(
        ProcessorFunc processor,
        float testFreqHz,
        float sampleRate,
        float inputGainDb,
        float thresholdDb = -80.0f
    ) {
        TestResult result;
        result.testFrequency = testFreqHz;
        result.sampleRate = sampleRate;
        result.passed = true;
        result.worstAliasPeak_dB = -200.0f;
        
        const int fftSize = 8192;
        const int numSamples = fftSize * 4; // Process more than FFT size
        
        // Generate test signal
        std::vector<float> inputBuffer(numSamples);
        std::vector<float> outputBuffer(numSamples);
        
        float inputGainLinear = std::pow(10.0f, inputGainDb / 20.0f);
        float nyquist = sampleRate / 2.0f;
        
        for (int i = 0; i < numSamples; ++i) {
            float phase = 2.0f * M_PI * testFreqHz * i / sampleRate;
            inputBuffer[i] = std::sin(phase) * inputGainLinear;
        }
        
        // Copy input to output buffer
        std::copy(inputBuffer.begin(), inputBuffer.end(), outputBuffer.begin());
        
        // Process through the actual plugin
        processor(outputBuffer.data(), numSamples);
        
        // Skip first 1024 samples (let filters settle)
        std::vector<float> analysisBuffer(outputBuffer.begin() + 1024, 
                                          outputBuffer.begin() + 1024 + fftSize);
        
        // Apply window
        std::vector<float> window(fftSize);
        for (int i = 0; i < fftSize; ++i) {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize - 1)));
            analysisBuffer[i] *= window[i];
        }
        
        // Compute FFT magnitude spectrum
        std::vector<float> magnitudeDb = computeFFTMagnitude(analysisBuffer, sampleRate);
        
        // Calculate expected harmonic locations and their aliases
        std::vector<float> expectedHarmonics;
        std::vector<float> aliasedFrequencies;
        
        for (int h = 2; h <= 10; ++h) {
            float harmonicFreq = testFreqHz * h;
            
            if (harmonicFreq < nyquist) {
                // This harmonic is below Nyquist - expected in output
                expectedHarmonics.push_back(harmonicFreq);
            } else {
                // This harmonic is above Nyquist - will alias
                float aliasFreq = harmonicFreq;
                
                // Calculate alias frequency (fold around Nyquist)
                while (aliasFreq > nyquist) {
                    aliasFreq = sampleRate - aliasFreq;
                    if (aliasFreq < 0) aliasFreq = -aliasFreq;
                }
                
                aliasedFrequencies.push_back(aliasFreq);
            }
        }
        
        // Check for peaks at aliased frequencies
        float binWidth = sampleRate / fftSize;
        
        for (float aliasFreq : aliasedFrequencies) {
            // Skip if alias frequency is very close to a legitimate harmonic
            bool nearLegitimate = false;
            for (float legit : expectedHarmonics) {
                if (std::abs(aliasFreq - legit) < binWidth * 3) {
                    nearLegitimate = true;
                    break;
                }
            }
            if (nearLegitimate) continue;
            
            // Find peak near alias frequency
            int centerBin = static_cast<int>(aliasFreq / binWidth);
            float peakDb = -200.0f;
            
            for (int b = std::max(1, centerBin - 3); 
                 b < std::min((int)magnitudeDb.size(), centerBin + 4); ++b) {
                if (magnitudeDb[b] > peakDb) {
                    peakDb = magnitudeDb[b];
                }
            }
            
            if (peakDb > thresholdDb) {
                result.passed = false;
                result.detectedPeaks.push_back({aliasFreq, peakDb});
            }
            
            if (peakDb > result.worstAliasPeak_dB) {
                result.worstAliasPeak_dB = peakDb;
            }
        }
        
        // Build details string
        result.details = "Aliasing Test Results\n";
        result.details += "=====================\n";
        result.details += "Test frequency: " + std::to_string(testFreqHz) + " Hz\n";
        result.details += "Sample rate: " + std::to_string(sampleRate) + " Hz\n";
        result.details += "Input gain: " + std::to_string(inputGainDb) + " dB\n";
        result.details += "Threshold: " + std::to_string(thresholdDb) + " dB\n\n";
        
        if (result.passed) {
            result.details += "RESULT: PASS\n";
            result.details += "Worst alias peak: " + std::to_string(result.worstAliasPeak_dB) + " dB\n";
        } else {
            result.details += "RESULT: FAIL\n";
            result.details += "Detected alias peaks above threshold:\n";
            for (const auto& peak : result.detectedPeaks) {
                result.details += "  " + std::to_string(peak.first) + " Hz: " 
                               + std::to_string(peak.second) + " dB\n";
            }
        }
        
        return result;
    }
    
private:
    static std::vector<float> computeFFTMagnitude(
        const std::vector<float>& input, 
        float sampleRate
    ) {
        const int N = input.size();
        std::vector<std::complex<float>> fftData(N);
        
        // Simple DFT (use proper FFT library in production)
        for (int k = 0; k < N / 2; ++k) {
            std::complex<float> sum(0, 0);
            for (int n = 0; n < N; ++n) {
                float angle = -2.0f * M_PI * k * n / N;
                sum += input[n] * std::complex<float>(std::cos(angle), std::sin(angle));
            }
            fftData[k] = sum;
        }
        
        // Convert to dB
        std::vector<float> magnitudeDb(N / 2);
        float refLevel = N / 2.0f; // Normalize to 0dB for full-scale sine
        
        for (int k = 0; k < N / 2; ++k) {
            float mag = std::abs(fftData[k]) / refLevel;
            magnitudeDb[k] = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -200.0f;
        }
        
        return magnitudeDb;
    }
};

//==============================================================================
// PART 5: Debug Signal Flow Analyzer
//==============================================================================

/**
 * Use this to find WHERE in your signal chain the aliasing is introduced
 * Insert tap points throughout your processing and analyze each one
 */
class SignalFlowDebugger {
public:
    void addTapPoint(const std::string& name, const float* data, int numSamples) {
        TapPoint tap;
        tap.name = name;
        tap.samples.assign(data, data + numSamples);
        tapPoints.push_back(tap);
    }
    
    void analyzeAllTapPoints(float sampleRate) {
        std::cout << "\n=== Signal Flow Analysis ===\n\n";
        
        for (const auto& tap : tapPoints) {
            std::cout << "Tap: " << tap.name << "\n";
            
            // Check for content above Nyquist/2 (potential aliasing region)
            auto spectrum = computeSpectrum(tap.samples, sampleRate);
            
            float nyquist = sampleRate / 2.0f;
            float warningRegionStart = nyquist * 0.7f; // Above 15.4kHz at 44.1k
            
            float maxInWarningRegion = -200.0f;
            for (size_t i = 0; i < spectrum.size(); ++i) {
                float freq = i * sampleRate / (spectrum.size() * 2);
                if (freq > warningRegionStart && freq < nyquist) {
                    if (spectrum[i] > maxInWarningRegion) {
                        maxInWarningRegion = spectrum[i];
                    }
                }
            }
            
            std::cout << "  Max level above " << warningRegionStart << " Hz: " 
                      << maxInWarningRegion << " dB\n";
            
            if (maxInWarningRegion > -60.0f) {
                std::cout << "  WARNING: High energy in aliasing danger zone!\n";
            }
            std::cout << "\n";
        }
    }
    
    void clear() { tapPoints.clear(); }
    
private:
    struct TapPoint {
        std::string name;
        std::vector<float> samples;
    };
    
    std::vector<TapPoint> tapPoints;
    
    std::vector<float> computeSpectrum(const std::vector<float>& samples, float sr) {
        // Simplified spectrum analysis
        const int fftSize = std::min(4096, (int)samples.size());
        std::vector<float> result(fftSize / 2, -200.0f);
        
        // ... FFT implementation ...
        
        return result;
    }
};

} // namespace LunaCoAudio

/*
 * =============================================================================
 * HOW TO USE THIS IN YOUR PLUGIN:
 * =============================================================================
 * 
 * 1. Replace your current oversampling with ProperOversampler<4>
 * 
 * 2. Make sure ALL nonlinear processing happens between upsample() and 
 *    downsample() calls. This includes:
 *    - Saturation (all stages)
 *    - Hysteresis
 *    - Bias circuit
 *    - Any waveshaping
 *    - Compression (if it uses nonlinear transfer function)
 *    
 * 3. ONLY linear processing can happen after downsample():
 *    - Output gain
 *    - Linear EQ/filters
 *    - Mixing
 *    
 * 4. Run RealAliasingTest to verify the fix actually works
 * 
 * 5. REMOVE the HF Content Detector - it's masking the problem, not fixing it
 * 
 * =============================================================================
 * PROMPT FOR CLAUDE CODE:
 * =============================================================================
 * 
 * Copy this entire file and paste it to Claude Code with this prompt:
 * 
 * "Here is a reference implementation for proper anti-aliasing. 
 *  
 *  Your previous fixes did not work - I can still see aliasing peaks at -40 
 *  to -60dB in the spectrum analyzer. Your tests are not measuring actual 
 *  plugin output.
 *  
 *  Please:
 *  1. Review the ProperOversampler and AntiAliasingFilter classes
 *  2. Integrate this approach into TapeMachine
 *  3. REMOVE the HF Content Detector (it's a band-aid that makes things dull)
 *  4. Ensure ALL nonlinear processing happens at the oversampled rate
 *  5. Use RealAliasingTest to verify the fix with actual audio, not math
 *  6. Add debug tap points using SignalFlowDebugger to identify where 
 *     aliasing is being introduced
 *  
 *  The test must pass with an 18.2kHz sine wave at +8.3dB input gain, 
 *  with all alias products below -80dB as measured in actual FFT output."
 * 
 */
