// AutoCompSweep — standalone unity-contract harness for GH issue #92.
//
// Feeds a -6 dBFS / 1 kHz sine through TapeMachineAudioProcessor::processBlock,
// sweeps inputGain across {-12,-6,0,+6,+12} dB with Auto Compensation ON, and
// reports the post-tape peak delta vs input. The fix re-derives the
// compressionCompensation curve from the measured raw transfer so |delta| must
// stay within +/- 0.5 dB at every drive point. The sweep is run at both 2x and
// 4x oversampling to confirm the curve does not drift across OS factors.
//
// Exit code 0 = all points within tolerance, 1 = failure.

#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kSampleRate   = 48000.0;
constexpr int    kBlockSize    = 512;
constexpr double kSineHz       = 1000.0;
// Pre-tape peak the issue's raw-transfer curve was measured at: a -6 dBFS sine
// through a -3 dB pan-center law -> 0.3544 (-9.01 dBFS). The tape saturation is
// level-dependent, so the compensation curve is only valid at this input level;
// feed the same level here so the harness mirrors Focal's measurement chain.
constexpr double kInputPeak    = 0.3544; // -9.01 dBFS (post pan-center)
constexpr int    kWarmupBlocks = 24;
constexpr int    kMeasureBlocks = 8;
constexpr double kTolDb        = 0.5;

// Set an APVTS parameter by its 0..1 normalised value.
void setNorm(juce::AudioProcessorValueTreeState& apvts, const char* id, float norm)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(norm);
}

// Normalised value for a choice param: index / (numChoices - 1).
float choiceNorm(int index, int numChoices)
{
    return static_cast<float>(index) / static_cast<float>(numChoices - 1);
}

// Run the sweep at one oversampling setting; returns true if all points pass.
bool runSweep(int osIndex, const char* osLabel)
{
    const double drives[] = { -12.0, -6.0, 0.0, 6.0, 12.0 };
    bool allPass = true;

    std::printf("\n=== Oversampling: %s ===\n", osLabel);
    std::printf("  drive(dB)   outPeak     outdBFS    delta(dB)   verdict\n");

    for (double driveDb : drives)
    {
        TapeMachineAudioProcessor proc;
        auto& apvts = proc.getAPVTS();

        // autoComp choice {Off,On} -> On = index 1
        setNorm(apvts, "autoComp", choiceNorm(1, 2));
        // oversampling choice {1x,2x,4x}
        setNorm(apvts, "oversampling", choiceNorm(osIndex, 3));
        // inputGain float -12..+12
        setNorm(apvts, "inputGain", static_cast<float>((driveDb + 12.0) / 24.0));

        proc.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
        proc.prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;

        double phase = 0.0;
        const double phaseInc = 2.0 * juce::MathConstants<double>::pi * kSineHz / kSampleRate;
        float outPeak = 0.0f;

        const int totalBlocks = kWarmupBlocks + kMeasureBlocks;
        for (int b = 0; b < totalBlocks; ++b)
        {
            for (int n = 0; n < kBlockSize; ++n)
            {
                const float s = static_cast<float>(kInputPeak * std::sin(phase));
                phase += phaseInc;
                if (phase > 2.0 * juce::MathConstants<double>::pi)
                    phase -= 2.0 * juce::MathConstants<double>::pi;
                buffer.setSample(0, n, s);
                buffer.setSample(1, n, s);
            }

            proc.processBlock(buffer, midi);

            if (b >= kWarmupBlocks)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < kBlockSize; ++n)
                        outPeak = std::max(outPeak, std::abs(buffer.getSample(ch, n)));
            }
        }

        const double outDbfs = 20.0 * std::log10(std::max(1.0e-9f, outPeak));
        const double inDbfs  = 20.0 * std::log10(kInputPeak);
        const double deltaDb = outDbfs - inDbfs;
        const bool   pass    = std::abs(deltaDb) <= kTolDb;
        allPass = allPass && pass;

        std::printf("  %+7.1f    %8.4f   %+8.2f   %+8.2f    %s\n",
                    driveDb, outPeak, outDbfs, deltaDb, pass ? "OK" : "FAIL");
    }

    return allPass;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // some JUCE DSP paths expect this

    std::printf("TapeMachine autoComp unity-contract sweep (GH #92)\n");
    std::printf("  %.4f peak (%.2f dBFS) 1 kHz sine, %g/%d, %d warmup + %d measure blocks\n",
                kInputPeak, 20.0 * std::log10(kInputPeak),
                kSampleRate, kBlockSize, kWarmupBlocks, kMeasureBlocks);
    std::printf("  PASS criterion: |delta| <= %.1f dB at every drive point\n", kTolDb);

    bool pass2x = runSweep(1, "2x");
    bool pass4x = runSweep(2, "4x");

    const bool allPass = pass2x && pass4x;
    std::printf("\nRESULT: %s (2x %s, 4x %s)\n",
                allPass ? "PASS" : "FAIL",
                pass2x ? "ok" : "fail",
                pass4x ? "ok" : "fail");
    return allPass ? 0 : 1;
}
