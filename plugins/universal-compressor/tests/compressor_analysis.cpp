/*
  ==============================================================================

    compressor_analysis.cpp
    Test harness for analyzing Universal Compressor characteristics

    Measures:
    - THD (Total Harmonic Distortion) at various levels
    - Frequency response
    - Attack/Release timing accuracy
    - Harmonic spectrum analysis

    Compare against reference measurements from:
    - LA-2A: THD < 0.5% @ +10dBm, 2nd harmonic dominant
    - 1176: THD < 0.5% @ limiting, odd harmonics
    - SSL Bus: THD < 0.01% @ 0dB GR, 0.1% @ 12dB GR

  ==============================================================================
*/

#include <iostream>
#include <fstream>
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <string>
#include <iomanip>

// Include hardware emulation headers for standalone testing
#include "../HardwareEmulation/HardwareMeasurements.h"
#include "../HardwareEmulation/WaveshaperCurves.h"
#include "../HardwareEmulation/TransformerEmulation.h"
#include "../HardwareEmulation/TubeEmulation.h"
#include "../HardwareEmulation/ConvolutionEngine.h"

constexpr double PI = 3.14159265358979323846;
constexpr int SAMPLE_RATE = 48000;
constexpr int FFT_SIZE = 8192;

//==============================================================================
// Simple FFT for harmonic analysis (Cooley-Tukey)
class SimpleFFT
{
public:
    static void fft(std::vector<std::complex<double>>& x)
    {
        const size_t N = x.size();
        if (N <= 1) return;
        
        // Verify N is a power of 2
        if ((N & (N - 1)) != 0)
        {
            throw std::invalid_argument("FFT size must be a power of 2");
        }
        
        // Bit-reversal permutation
        for (size_t i = 1, j = 0; i < N; ++i)
        {
            size_t bit = N >> 1;            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(x[i], x[j]);
        }

        // Cooley-Tukey iterative FFT
        for (size_t len = 2; len <= N; len <<= 1)
        {
            double angle = -2.0 * PI / len;
            std::complex<double> wlen(std::cos(angle), std::sin(angle));
            for (size_t i = 0; i < N; i += len)
            {
                std::complex<double> w(1.0, 0.0);
                for (size_t j = 0; j < len / 2; ++j)
                {
                    std::complex<double> u = x[i + j];
                    std::complex<double> v = x[i + j + len / 2] * w;
                    x[i + j] = u + v;
                    x[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }
};

//==============================================================================
// Generate test signals
std::vector<float> generateSineWave(double frequency, int numSamples, double amplitude = 1.0)
{
    std::vector<float> signal(numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        signal[i] = static_cast<float>(amplitude * std::sin(2.0 * PI * frequency * i / SAMPLE_RATE));
    }
    return signal;
}

std::vector<float> generateImpulse(int numSamples)
{
    std::vector<float> signal(numSamples, 0.0f);
    signal[0] = 1.0f;
    return signal;
}

//==============================================================================
// Measure THD from FFT
struct THDResult
{
    double thd;           // Total harmonic distortion (%)
    double fundamental;   // Fundamental level (dB)
    double h2;           // 2nd harmonic (dB relative to fundamental)
    double h3;           // 3rd harmonic (dB relative to fundamental)
    double h4;           // 4th harmonic (dB)
    double h5;           // 5th harmonic (dB)
};

THDResult measureTHD(const std::vector<float>& signal, double fundamentalFreq)
{
    THDResult result = {0.0, 0.0, -120.0, -120.0, -120.0, -120.0};

    // Zero-pad to FFT size
    std::vector<std::complex<double>> fftData(FFT_SIZE, {0.0, 0.0});
    int copyLen = std::min(static_cast<int>(signal.size()), FFT_SIZE);
    for (int i = 0; i < copyLen; ++i)
        fftData[i] = {signal[i], 0.0};

    // Apply Hann window
    for (int i = 0; i < copyLen; ++i)
    {
        double window = 0.5 * (1.0 - std::cos(2.0 * PI * i / (copyLen - 1)));
        fftData[i] *= window;
    }

    SimpleFFT::fft(fftData);

    // Find bin for each harmonic
    double binWidth = static_cast<double>(SAMPLE_RATE) / FFT_SIZE;
    int fundBin = static_cast<int>(fundamentalFreq / binWidth + 0.5);
    int h2Bin = fundBin * 2;
    int h3Bin = fundBin * 3;
    int h4Bin = fundBin * 4;
    int h5Bin = fundBin * 5;

    // Get magnitudes
    auto getMagnitude = [&](int bin) -> double {
        if (bin >= FFT_SIZE / 2) return 0.0;
        return std::abs(fftData[bin]) * 2.0 / FFT_SIZE;
    };

    double fundMag = getMagnitude(fundBin);
    double h2Mag = getMagnitude(h2Bin);
    double h3Mag = getMagnitude(h3Bin);
    double h4Mag = getMagnitude(h4Bin);
    double h5Mag = getMagnitude(h5Bin);

    // Calculate THD
    double harmonicSum = h2Mag * h2Mag + h3Mag * h3Mag + h4Mag * h4Mag + h5Mag * h5Mag;
    result.thd = (fundMag > 1e-10) ? (std::sqrt(harmonicSum) / fundMag * 100.0) : 0.0;
    // Convert to dB relative to fundamental
    result.fundamental = 20.0 * std::log10(fundMag + 1e-10);
    result.h2 = 20.0 * std::log10(h2Mag / fundMag + 1e-10);
    result.h3 = 20.0 * std::log10(h3Mag / fundMag + 1e-10);
    result.h4 = 20.0 * std::log10(h4Mag / fundMag + 1e-10);
    result.h5 = 20.0 * std::log10(h5Mag / fundMag + 1e-10);

    return result;
}

//==============================================================================
// Test waveshaper curves
void testWaveshaperCurves()
{
    std::cout << "\n=== Waveshaper Curve Analysis ===\n\n";

    auto& waveshapers = HardwareEmulation::getWaveshaperCurves();

    struct CurveTest {
        HardwareEmulation::WaveshaperCurves::CurveType type;
        const char* name;
        double expectedTHD;  // Expected THD at 0dB input
    };

    std::array<CurveTest, 5> curves = {{
        {HardwareEmulation::WaveshaperCurves::CurveType::LA2A_Tube, "LA-2A Tube", 0.5},
        {HardwareEmulation::WaveshaperCurves::CurveType::FET_1176, "1176 FET", 0.5},
        {HardwareEmulation::WaveshaperCurves::CurveType::DBX_VCA, "DBX VCA", 0.1},
        {HardwareEmulation::WaveshaperCurves::CurveType::SSL_Bus, "SSL Bus", 0.1},
        {HardwareEmulation::WaveshaperCurves::CurveType::Transformer, "Transformer", 0.3}
    }};

    std::cout << std::setw(15) << "Curve"
              << std::setw(10) << "THD %"
              << std::setw(10) << "Target"
              << std::setw(10) << "H2 dB"
              << std::setw(10) << "H3 dB"
              << std::setw(10) << "H4 dB"
              << std::setw(10) << "Status"
              << "\n";
    std::cout << std::string(75, '-') << "\n";

    for (const auto& curve : curves)
    {
        // Generate 1kHz sine and process through waveshaper
        auto testSignal = generateSineWave(1000.0, FFT_SIZE, 0.5);  // -6dB input

        for (auto& sample : testSignal)
            sample = waveshapers.processWithDrive(sample, curve.type, 0.5f);

        THDResult thd = measureTHD(testSignal, 1000.0);

        std::cout << std::setw(15) << curve.name
                  << std::setw(10) << std::fixed << std::setprecision(3) << thd.thd
                  << std::setw(10) << std::fixed << std::setprecision(1) << curve.expectedTHD
                  << std::setw(10) << std::fixed << std::setprecision(1) << thd.h2
                  << std::setw(10) << std::fixed << std::setprecision(1) << thd.h3
                  << std::setw(10) << std::fixed << std::setprecision(1) << thd.h4
                  << std::setw(10) << (thd.thd < curve.expectedTHD * 2 ? "PASS" : "CHECK")
                  << "\n";
    }
}

//==============================================================================
// Test transformer emulation
void testTransformerEmulation()
{
    std::cout << "\n=== Transformer Emulation Analysis ===\n\n";

    HardwareEmulation::TransformerEmulation transformer;
    transformer.prepare(SAMPLE_RATE, 1);

    // Test with LA-2A transformer profile
    transformer.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().inputTransformer);
    transformer.setEnabled(true);

    std::cout << "LA-2A Input Transformer:\n";
    std::cout << std::string(50, '-') << "\n";

    // Test at various input levels
    std::array<double, 4> levels = {-20.0, -10.0, -6.0, 0.0};

    std::cout << std::setw(12) << "Input dB"
              << std::setw(12) << "THD %"
              << std::setw(12) << "H2 dB"
              << std::setw(12) << "H3 dB"
              << "\n";

    for (double levelDb : levels)
    {
        double amplitude = std::pow(10.0, levelDb / 20.0);
        auto testSignal = generateSineWave(1000.0, FFT_SIZE, amplitude);

        transformer.reset();
        for (auto& sample : testSignal)
            sample = transformer.processSample(sample, 0);

        THDResult thd = measureTHD(testSignal, 1000.0);

        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << levelDb
                  << std::setw(12) << std::fixed << std::setprecision(4) << thd.thd
                  << std::setw(12) << std::fixed << std::setprecision(1) << thd.h2
                  << std::setw(12) << std::fixed << std::setprecision(1) << thd.h3
                  << "\n";
    }
}

//==============================================================================
// Test tube emulation
void testTubeEmulation()
{
    std::cout << "\n=== Tube Emulation Analysis ===\n\n";

    HardwareEmulation::TubeEmulation tube;
    tube.prepare(SAMPLE_RATE, 1);

    struct TubeTest {
        HardwareEmulation::TubeEmulation::TubeType type;
        const char* name;
    };

    std::array<TubeTest, 4> tubes = {{
        {HardwareEmulation::TubeEmulation::TubeType::Triode_12AX7, "12AX7"},
        {HardwareEmulation::TubeEmulation::TubeType::Triode_12AT7, "12AT7"},
        {HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7, "12BH7"},
        {HardwareEmulation::TubeEmulation::TubeType::Triode_6SN7, "6SN7"}
    }};

    std::cout << std::setw(12) << "Tube"
              << std::setw(12) << "THD %"
              << std::setw(12) << "H2 dB"
              << std::setw(12) << "H3 dB"
              << std::setw(12) << "Even/Odd"
              << "\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto& tubeTest : tubes)
    {
        tube.setTubeType(tubeTest.type);
        tube.setDrive(0.3f);
        tube.reset();

        auto testSignal = generateSineWave(1000.0, FFT_SIZE, 0.5);

        for (auto& sample : testSignal)
            sample = tube.processSample(sample, 0);

        THDResult thd = measureTHD(testSignal, 1000.0);

        // Calculate even/odd ratio
        double evenPower = std::pow(10.0, thd.h2 / 10.0) + std::pow(10.0, thd.h4 / 10.0);
        double oddPower = std::pow(10.0, thd.h3 / 10.0) + std::pow(10.0, thd.h5 / 10.0);
        double evenOddRatio = evenPower / (oddPower + 1e-10);

        std::cout << std::setw(12) << tubeTest.name
                  << std::setw(12) << std::fixed << std::setprecision(3) << thd.thd
                  << std::setw(12) << std::fixed << std::setprecision(1) << thd.h2
                  << std::setw(12) << std::fixed << std::setprecision(1) << thd.h3
                  << std::setw(12) << std::fixed << std::setprecision(2) << evenOddRatio
                  << "\n";
    }
}

//==============================================================================
// Test convolution engine
void testConvolutionEngine()
{
    std::cout << "\n=== Convolution Engine Analysis ===\n\n";

    HardwareEmulation::ShortConvolution conv;
    conv.prepare(SAMPLE_RATE);

    struct IRTest {
        HardwareEmulation::ShortConvolution::TransformerType type;
        const char* name;
    };

    std::array<IRTest, 4> irs = {{
        {HardwareEmulation::ShortConvolution::TransformerType::LA2A, "LA-2A"},
        {HardwareEmulation::ShortConvolution::TransformerType::FET_1176, "1176"},
        {HardwareEmulation::ShortConvolution::TransformerType::SSL_Console, "SSL"},
        {HardwareEmulation::ShortConvolution::TransformerType::Generic, "Generic"}
    }};

    std::cout << std::setw(12) << "IR Type"
              << std::setw(12) << "Latency"
              << std::setw(15) << "1kHz Gain dB"
              << std::setw(15) << "10kHz Gain dB"
              << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (const auto& ir : irs)
    {
        conv.loadTransformerIR(ir.type);
        conv.reset();

        // Test frequency response at 1kHz and 10kHz
        auto test1k = generateSineWave(1000.0, 4096, 0.5);
        auto test10k = generateSineWave(10000.0, 4096, 0.5);

        double rms1kIn = 0.0, rms1kOut = 0.0;
        double rms10kIn = 0.0, rms10kOut = 0.0;

        for (int i = 0; i < 4096; ++i)
        {
            rms1kIn += test1k[i] * test1k[i];
            float out1k = conv.processSample(test1k[i]);
            rms1kOut += out1k * out1k;
        }

        conv.reset();
        for (int i = 0; i < 4096; ++i)
        {
            rms10kIn += test10k[i] * test10k[i];
            float out10k = conv.processSample(test10k[i]);
            rms10kOut += out10k * out10k;
        }

        double gain1k = 10.0 * std::log10(rms1kOut / rms1kIn + 1e-10);
        double gain10k = 10.0 * std::log10(rms10kOut / rms10kIn + 1e-10);

        std::cout << std::setw(12) << ir.name
                  << std::setw(12) << conv.getLatency()
                  << std::setw(15) << std::fixed << std::setprecision(2) << gain1k
                  << std::setw(15) << std::fixed << std::setprecision(2) << gain10k
                  << "\n";
    }
}

//==============================================================================
// Reference comparison
void printReferenceComparison()
{
    std::cout << "\n=== Reference Measurements (Target Values) ===\n\n";

    std::cout << "LA-2A (Teletronix):\n";
    std::cout << "  - THD @ +10dBm: < 0.5% (0.25% typical)\n";
    std::cout << "  - 2nd harmonic dominant (tube character)\n";
    std::cout << "  - HF rolloff: -3dB @ 15-18kHz\n\n";

    std::cout << "1176 Rev A (UREI):\n";
    std::cout << "  - THD @ limiting: < 0.5%\n";
    std::cout << "  - Odd harmonics dominant (FET character)\n";
    std::cout << "  - All-buttons: 3x harmonic content\n\n";

    std::cout << "SSL G-Bus Compressor:\n";
    std::cout << "  - THD @ 0dB GR: 0.01%\n";
    std::cout << "  - THD @ 12dB GR: 0.1%\n";
    std::cout << "  - Very clean, subtle coloration\n\n";

    std::cout << "DBX 160:\n";
    std::cout << "  - THD: < 0.1% (very clean VCA)\n";
    std::cout << "  - Minimal harmonic distortion\n";
    std::cout << "  - Transparent compression\n\n";
}

//==============================================================================
int main()
{
    std::cout << "========================================\n";
    std::cout << "  Universal Compressor Analysis Tool\n";
    std::cout << "  Hardware Emulation Verification\n";
    std::cout << "========================================\n";

    printReferenceComparison();
    testWaveshaperCurves();
    testTransformerEmulation();
    testTubeEmulation();
    testConvolutionEngine();

    std::cout << "\n=== Analysis Complete ===\n";
    std::cout << "Compare results against reference measurements above.\n";
    std::cout << "Tune waveshaper curves and profiles to match target THD.\n\n";

    return 0;
}
