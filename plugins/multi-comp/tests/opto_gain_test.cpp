/*
  ==============================================================================
    opto_gain_test.cpp
    Measures the gain of the Opto mode's hardware emulation chain at PR=0.

    Tests the exact signal path: input transformer → tube → output transformer
    to determine the gain compensation factor needed for unity at PR=0.

    Build: clang++ -std=c++17 -O2 -I.. opto_gain_test.cpp -o opto_gain_test
    Run:   ./opto_gain_test
  ==============================================================================
*/

#include <iostream>
#include <cmath>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "../HardwareEmulation/HardwareMeasurements.h"
#include "../HardwareEmulation/TransformerEmulation.h"
#include "../HardwareEmulation/TubeEmulation.h"

constexpr double PI = 3.14159265358979323846;

// Generate a sine wave at the specified frequency and dB level
std::vector<float> generateSine(double freqHz, int numSamples, int sampleRate, double levelDb)
{
    double amplitude = std::pow(10.0, levelDb / 20.0);
    std::vector<float> signal(numSamples);
    for (int i = 0; i < numSamples; ++i)
        signal[i] = static_cast<float>(amplitude * std::sin(2.0 * PI * freqHz * i / sampleRate));
    return signal;
}

// Measure RMS of a signal (skip first N samples for settling)
double measureRMS(const std::vector<float>& signal, int skipSamples = 0)
{
    if (skipSamples >= static_cast<int>(signal.size()))
    {
        std::cerr << "Warning: skipSamples (" << skipSamples << ") >= signal size ("
                  << signal.size() << "), returning 0\n";
        return 0.0;
    }
    double sum = 0.0;
    int count = 0;
    for (int i = skipSamples; i < static_cast<int>(signal.size()); ++i)
    {
        sum += signal[i] * signal[i];
        count++;
    }
    return std::sqrt(sum / std::max(1, count));
}
double rmsToDb(double rms)
{
    return 20.0 * std::log10(std::max(rms, 1e-10));
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "  Opto Mode Gain Measurement\n";
    std::cout << "========================================\n\n";

    int failCount = 0;

    // Test at multiple sample rates (the plugin prepares at 4x oversampled rate)
    int sampleRates[] = {48000, 96000, 192000};
    double testLevels[] = {-24.0, -18.0, -12.0, -6.0, 0.0};

    for (int sr : sampleRates)
    {
        std::cout << "=== Sample Rate: " << sr << " Hz ===\n\n";

        // Set up the exact same chain as OptoCompressor::prepare()
        HardwareEmulation::TransformerEmulation inputTransformer;
        inputTransformer.prepare(sr, 1);
        inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().inputTransformer);
        inputTransformer.setEnabled(true);

        HardwareEmulation::TransformerEmulation outputTransformer;
        outputTransformer.prepare(sr, 1);
        outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().outputTransformer);
        outputTransformer.setEnabled(true);

        HardwareEmulation::TubeEmulation tubeStage;
        tubeStage.prepare(sr, 1);
        tubeStage.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tubeStage.setDrive(0.2f);

        std::cout << std::setw(12) << "Input dB"
                  << std::setw(15) << "XfrmIn dB"
                  << std::setw(15) << "Tube dB"
                  << std::setw(15) << "XfrmOut dB"
                  << std::setw(15) << "Gain dB"
                  << "\n";
        std::cout << std::string(72, '-') << "\n";

        for (double levelDb : testLevels)
        {
            int numSamples = sr;  // 1 second
            auto input = generateSine(1000.0, numSamples, sr, levelDb);

            // Stage 1: Input transformer
            inputTransformer.reset();
            std::vector<float> afterInputXfrm(numSamples);
            for (int i = 0; i < numSamples; ++i)
                afterInputXfrm[i] = inputTransformer.processSample(input[i], 0);

            // Stage 2: Tube (12BH7, drive=0.2)
            tubeStage.reset();
            std::vector<float> afterTube(numSamples);
            for (int i = 0; i < numSamples; ++i)
                afterTube[i] = tubeStage.processSample(afterInputXfrm[i], 0);

            // Stage 3: Output transformer
            outputTransformer.reset();
            std::vector<float> afterOutputXfrm(numSamples);
            for (int i = 0; i < numSamples; ++i)
                afterOutputXfrm[i] = outputTransformer.processSample(afterTube[i], 0);

            // Measure RMS (skip first 100ms for filter settling)
            int skip = sr / 10;
            double rmsIn = measureRMS(input, skip);
            double rmsAfterXfrmIn = measureRMS(afterInputXfrm, skip);
            double rmsAfterTube = measureRMS(afterTube, skip);
            double rmsAfterXfrmOut = measureRMS(afterOutputXfrm, skip);

            double dbIn = rmsToDb(rmsIn);
            double dbAfterXfrmIn = rmsToDb(rmsAfterXfrmIn);
            double dbAfterTube = rmsToDb(rmsAfterTube);
            double dbAfterXfrmOut = rmsToDb(rmsAfterXfrmOut);
            double totalGain = dbAfterXfrmOut - dbIn;

            std::cout << std::setw(12) << std::fixed << std::setprecision(1) << levelDb
                      << std::setw(15) << std::fixed << std::setprecision(2) << dbAfterXfrmIn
                      << std::setw(15) << std::fixed << std::setprecision(2) << dbAfterTube
                      << std::setw(15) << std::fixed << std::setprecision(2) << dbAfterXfrmOut
                      << std::setw(15) << std::fixed << std::setprecision(2) << totalGain
                      << "\n";
        }

        // Measure the compensation factor at -18dB reference level
        {
            auto ref = generateSine(1000.0, sr, sr, -18.0);
            inputTransformer.reset();
            tubeStage.reset();
            outputTransformer.reset();

            std::vector<float> output(sr);
            for (int i = 0; i < sr; ++i)
            {
                float x = inputTransformer.processSample(ref[i], 0);
                x = tubeStage.processSample(x, 0);
                x = outputTransformer.processSample(x, 0);
                output[i] = x;
            }

            int skip = sr / 10;
            double rmsIn = measureRMS(ref, skip);
            double rmsOut = measureRMS(output, skip);
            double gainLin = (rmsIn > 1e-10) ? (rmsOut / rmsIn) : 1.0;
            double gainDb = 20.0 * std::log10(std::max(gainLin, 1e-10));
            double compensationDb = -gainDb;
            double compensation = 1.0 / gainLin;
            std::cout << "\n  Reference: -18dB 1kHz sine\n";
            std::cout << "  Hardware chain gain: " << std::fixed << std::setprecision(3)
                      << gainDb << " dB (linear: " << std::setprecision(4) << gainLin << ")\n";
            std::cout << "  Compensation needed: " << std::fixed << std::setprecision(3)
                      << compensationDb << " dB (linear: " << std::setprecision(4) << compensation << ")\n";
        }
        std::cout << "\n";
    }

    // === Verify compensated chain at 192kHz (the actual prepare rate) ===
    std::cout << "=== COMPENSATED CHAIN VERIFICATION (192kHz) ===\n\n";
    {
        int sr = 192000;

        HardwareEmulation::TransformerEmulation inputXfrm;
        inputXfrm.prepare(sr, 1);
        inputXfrm.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().inputTransformer);
        inputXfrm.setEnabled(true);

        HardwareEmulation::TransformerEmulation outputXfrm;
        outputXfrm.prepare(sr, 1);
        outputXfrm.setProfile(HardwareEmulation::HardwareProfiles::getLA2A().outputTransformer);
        outputXfrm.setEnabled(true);

        HardwareEmulation::TubeEmulation tube;
        tube.prepare(sr, 1);
        tube.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tube.setDrive(0.2f);

        // Calibrate: measure gain at -18dB reference (same as OptoCompressor::calibrateHardwareGain)
        constexpr int calSamples = 4800;
        constexpr float refAmp = 0.126f;
        float angStep = static_cast<float>(2.0 * PI * 1000.0 / sr);

        // Warmup
        int warmup = sr / 20;  // 50ms
        for (int i = 0; i < warmup; ++i)
        {
            float x = refAmp * std::sin(angStep * static_cast<float>(i));
            x = inputXfrm.processSample(x, 0);
            x = tube.processSample(x, 0);
            outputXfrm.processSample(x, 0);
        }

        double inRms2 = 0.0, outRms2 = 0.0;
        for (int i = 0; i < calSamples; ++i)
        {
            float inp = refAmp * std::sin(angStep * static_cast<float>(warmup + i));
            float x = inputXfrm.processSample(inp, 0);
            x = tube.processSample(x, 0);
            x = outputXfrm.processSample(x, 0);
            inRms2 += inp * inp;
            outRms2 += x * x;
        }
        float compensation;
        if (outRms2 > 1e-20) {
            compensation = static_cast<float>(std::sqrt(inRms2 / outRms2));
        } else {
            std::cerr << "  WARNING: Calibration produced near-zero output (outRms2=" << outRms2
                      << "), using fallback compensation=1.0\n";
            compensation = 1.0f;
            failCount++;
        }

        std::cout << "  Calibrated compensation factor: " << std::fixed << std::setprecision(4)
                  << compensation << " ("
                  << std::setprecision(2) << (20.0 * std::log10(compensation)) << " dB)\n\n";

        // Now test the FULL compensated chain at multiple levels
        inputXfrm.reset();
        outputXfrm.reset();
        tube.reset();

        std::cout << std::setw(12) << "Input dB"
                  << std::setw(18) << "Output dB"
                  << std::setw(18) << "Error dB"
                  << std::setw(12) << "Status"
                  << "\n";
        std::cout << std::string(60, '-') << "\n";

        for (double levelDb : testLevels)
        {
            auto input = generateSine(1000.0, sr, sr, levelDb);

            inputXfrm.reset();
            tube.reset();
            outputXfrm.reset();

            // Warmup
            for (int i = 0; i < warmup; ++i)
            {
                float x = input[i % static_cast<int>(input.size())];
                x = inputXfrm.processSample(x, 0);
                x = tube.processSample(x, 0);
                outputXfrm.processSample(x, 0);
            }

            // Measure compensated output
            double inRms = 0.0, outRms = 0.0;
            int measureSamples = sr / 2;  // 500ms
            for (int i = 0; i < measureSamples; ++i)
            {
                int idx = (warmup + i) % static_cast<int>(input.size());
                float inp = input[idx];
                float x = inputXfrm.processSample(inp, 0);
                x = tube.processSample(x, 0);
                x = outputXfrm.processSample(x, 0);
                x *= compensation;  // Apply compensation
                inRms += inp * inp;
                outRms += x * x;
            }
            inRms = std::sqrt(inRms / measureSamples);
            outRms = std::sqrt(outRms / measureSamples);

            double inDb = rmsToDb(inRms);
            double outDb = rmsToDb(outRms);
            double errorDb = outDb - inDb;
            bool pass = std::abs(errorDb) < 0.5;
            if (!pass)
                ++failCount;

            std::cout << std::setw(12) << std::fixed << std::setprecision(1) << levelDb
                      << std::setw(18) << std::fixed << std::setprecision(2) << outDb
                      << std::setw(18) << std::fixed << std::setprecision(3) << errorDb
                      << std::setw(12) << (pass ? "PASS" : "FAIL")
                      << "\n";
        }

        if (failCount > 0)
            std::cout << "\n  " << failCount << " test(s) FAILED\n";
    }

    return failCount > 0 ? 1 : 0;
}
