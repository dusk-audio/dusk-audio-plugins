#include <JuceHeader.h>

#include "../src/FactoryPresets.h"
#include "../src/PluginProcessor.h"
#include "../src/StereoImagePresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;
constexpr int kRenderSamples = static_cast<int> (kSampleRate * 2.0);
constexpr int kBurstSamples = static_cast<int> (kSampleRate * 0.005);

int failures = 0;

void check (bool condition, const std::string& description)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << '\n';
    if (! condition)
        ++failures;
}

float nextNoise (uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (static_cast<int32_t> (state)) / 2147483648.0f;
}

struct Render
{
    std::vector<float> left;
    std::vector<float> right;
    int burstSamples = 0;
};

enum class InputPan
{
    left,
    centre,
    right
};

Render renderDattorro (float steerAmount, InputPan inputPan,
                       float nativeStereoAmount = 0.0f, bool profileEnabled = false,
                       float panRotationRadians = 0.0f, bool mirrorHardRight = false,
                       double sampleRate = kSampleRate)
{
    const int renderSamples = static_cast<int> (sampleRate * 2.0);
    const int burstSamples = static_cast<int> (sampleRate * 0.005);

    DuskVerbEngine engine;
    engine.prepare (sampleRate, kBlockSize);
    engine.setAlgorithm (0);
    engine.setWidth (1.0f);
    engine.setMonoBelow (20.0f);
    engine.setPostSteer (steerAmount);
    engine.setDattorroStereoInput (nativeStereoAmount);
    if (profileEnabled)
    {
        engine.setPostSteerProfile (0.9f, -0.8f, 0.7f,
                                    -0.7f, 0.6f, -0.5f,
                                    0.4f, -0.3f, 0.2f,
                                    75.0f, 40.0f, 300.0f);
        engine.setPostSteerWander (-0.5f, 0.25f, 0.75f, 1.8f, 1500.0f, 0.2f);
        engine.setPostSteerPanRotation (panRotationRadians);
        engine.setPostSteerHardRightMirror (mirrorHardRight);
    }

    Render render { std::vector<float> (static_cast<size_t> (renderSamples), 0.0f),
                    std::vector<float> (static_cast<size_t> (renderSamples), 0.0f),
                    burstSamples };

    uint32_t rng = 0x12345678u;
    for (int i = 0; i < burstSamples; ++i)
    {
        const float sample = nextNoise (rng) * 0.25f;
        render.left[static_cast<size_t> (i)] = inputPan != InputPan::right ? sample : 0.0f;
        render.right[static_cast<size_t> (i)] = inputPan != InputPan::left ? sample : 0.0f;
    }

    for (int offset = 0; offset < renderSamples; offset += kBlockSize)
    {
        const int count = std::min (kBlockSize, renderSamples - offset);
        engine.process (render.left.data() + offset, render.right.data() + offset, count);
    }

    return render;
}

float tailIldDb (const Render& render)
{
    double energyL = 0.0;
    double energyR = 0.0;
    for (size_t i = static_cast<size_t> (render.burstSamples); i < render.left.size(); ++i)
    {
        const double left = render.left[i];
        const double right = render.right[i];
        energyL += left * left;
        energyR += right * right;
    }

    return static_cast<float> (10.0 * std::log10 ((energyL + 1.0e-30) / (energyR + 1.0e-30)));
}

void testPresetCalibrationWiring()
{
    const auto& presets = getFactoryPresets();
    check (presets.size() == DuskVerbStereoImage::kPresetCalibrations.size(),
           "every factory preset has a stereo-image calibration entry");
    const auto anchored = std::count_if (
        DuskVerbStereoImage::kPresetCalibrations.begin(),
        DuskVerbStereoImage::kPresetCalibrations.end(),
        [] (const auto& calibration) { return calibration.hasMeasuredAnchor; });
    check (anchored == 18, "the calibration table distinguishes 18 anchors from two design-led presets");
    check (std::none_of (DuskVerbStereoImage::kPresetCalibrations.begin(),
                         DuskVerbStereoImage::kPresetCalibrations.end(),
                         [] (const auto& calibration) {
                             return std::abs (calibration.nativeStereoAmount) > 1.0e-6f;
                         }),
           "factory presets keep native tank-side injection disabled");

    // Independent guard against a profile-table name typo: the assertion above
    // derives both sides from the same findSteerProfile() lookup, so a mistyped
    // kSteerProfiles name would pass silently. This list is the set of preset
    // names EXPECTED to carry a fitted post-steer profile, hardcoded from
    // StereoImagePresets.h kSteerProfiles (17 entries) so the two must agree.
    static constexpr std::array<const char*, 17> kExpectedProfilePresets = { {
        "Vocal Plate", "Drum Plate", "Bright Hall", "Vocal Hall",
        "Cathedral Large Hall", "Blade Runner 224", "79 Vocal Chamber",
        "Small Drum Room", "Medium Drum Room", "Live Room", "Tiled Room",
        "Ambience", "Vintage Vocal Plate", "Vintage Gold Plate",
        "Large Chamber", "Reverse Taps", "Deep Blue Day" } };
    check (kExpectedProfilePresets.size() == DuskVerbStereoImage::kSteerProfiles.size(),
           "expected profile-set size matches kSteerProfiles size (17)");
    const auto expectsProfile = [] (std::string_view presetName) {
        return std::any_of (kExpectedProfilePresets.begin(), kExpectedProfilePresets.end(),
                            [presetName] (const char* n) { return presetName == n; });
    };

    DuskVerbEngine engine;
    engine.prepare (kSampleRate, kBlockSize);

    int profileBearingPresets = 0;
    for (const auto& preset : presets)
    {
        const auto* calibration = DuskVerbStereoImage::findPresetCalibration (preset.name);
        check (calibration != nullptr, std::string (preset.name) + " calibration exists");
        if (calibration == nullptr)
            continue;

        preset.applyEngineConfig (engine);
        check (std::abs (engine.getPostSteerAmount() - calibration->steerAmount) < 1.0e-6f,
               std::string (preset.name) + " applies its calibrated steer amount");
        check (engine.hasPostSteerProfile()
                   == (DuskVerbStereoImage::findSteerProfile (preset.name) != nullptr),
               std::string (preset.name) + " applies or clears its measured profile");
        check (engine.hasPostSteerProfile() == expectsProfile (preset.name),
               std::string (preset.name) + " profile presence matches the explicit expected set");
        if (engine.hasPostSteerProfile())
            ++profileBearingPresets;
    }
    check (profileBearingPresets == 17,
           "exactly 17 factory presets apply a fitted post-steer profile");

    engine.setPostSteer (0.75f);
    engine.reapplyNeutralEngineConfig();
    check (std::abs (engine.getPostSteerAmount()) < 1.0e-6f,
           "unknown/session-neutral configuration clears prior preset steer");
}

void testCentredInputIsBitIdentical()
{
    const auto off = renderDattorro (0.0f, InputPan::centre);
    const auto on = renderDattorro (0.97f, InputPan::centre);
    const auto profiled = renderDattorro (0.0f, InputPan::centre, 0.0f, true, 0.7f, true);
    const auto nativeStereo = renderDattorro (0.0f, InputPan::centre, 2.0f, false);
    const auto bytes = static_cast<size_t> (kRenderSamples) * sizeof (float);

    check (std::memcmp (off.left.data(), on.left.data(), bytes) == 0
               && std::memcmp (off.right.data(), on.right.data(), bytes) == 0,
           "centred input stays bit-identical with post-steer enabled");
    check (std::memcmp (off.left.data(), profiled.left.data(), bytes) == 0
               && std::memcmp (off.right.data(), profiled.right.data(), bytes) == 0,
           "centred input stays bit-identical with every calibrated final-image operation enabled");
    check (std::memcmp (off.left.data(), nativeStereo.left.data(), bytes) == 0
               && std::memcmp (off.right.data(), nativeStereo.right.data(), bytes) == 0,
           "centred input stays bit-identical with native tank-side injection enabled");
}

void testHardPannedInputRetainsSourceSide()
{
    const auto leftOff = renderDattorro (0.0f, InputPan::left);
    const auto leftOn = renderDattorro (0.38f, InputPan::left);
    const float leftOffIld = tailIldDb (leftOff);
    const float leftOnIld = tailIldDb (leftOn);

    std::cout << "       Dattorro hard-left tail ILD: " << leftOffIld
              << " dB off -> " << leftOnIld << " dB on\n";
    check (leftOnIld >= 2.0f, "hard-left input produces at least +2 dB source-side tail ILD");
    check (leftOnIld - leftOffIld >= 1.5f,
           "positive post-steer materially improves hard-left source-side retention");

    const auto rightOff = renderDattorro (0.0f, InputPan::right);
    const auto rightOn = renderDattorro (0.38f, InputPan::right);
    const float rightOffIld = tailIldDb (rightOff);
    const float rightOnIld = tailIldDb (rightOn);

    std::cout << "       Dattorro hard-right tail ILD: " << rightOffIld
              << " dB off -> " << rightOnIld << " dB on\n";
    check (rightOnIld <= -2.0f, "hard-right input produces at least -2 dB source-side tail ILD");
    check (rightOffIld - rightOnIld >= 1.5f,
           "positive post-steer materially improves hard-right source-side retention");

    const auto deSteered = renderDattorro (-0.38f, InputPan::left);
    const float deSteeredIld = tailIldDb (deSteered);
    std::cout << "       Dattorro hard-left negative-steer ILD: " << leftOffIld
              << " dB off -> " << deSteeredIld << " dB on\n";
    check (leftOffIld - deSteeredIld >= 1.5f,
           "negative post-steer reduces an engine's existing source-side over-lean");

    const auto profiled = renderDattorro (0.0f, InputPan::left, 0.0f, true);
    const auto rotated = renderDattorro (0.0f, InputPan::left, 0.0f, true, 0.65f);
    const auto bytes = static_cast<size_t> (kRenderSamples) * sizeof (float);
    check (std::memcmp (profiled.left.data(), rotated.left.data(), bytes) != 0
               || std::memcmp (profiled.right.data(), rotated.right.data(), bytes) != 0,
           "hard-panned input activates the source-keyed output rotation");

    const auto rightProfiled = renderDattorro (0.0f, InputPan::right, 0.0f, true);
    const auto rightMirrored = renderDattorro (0.0f, InputPan::right, 0.0f, true, 0.0f, true);
    check (std::memcmp (rightProfiled.left.data(), rightMirrored.right.data(), bytes) == 0
               && std::memcmp (rightProfiled.right.data(), rightMirrored.left.data(), bytes) == 0,
           "hard-right mirror swaps channels without changing their samples");
}

// The full trajectory/profile checks above stay 48 kHz-only to bound runtime;
// the two rate-invariant guarantees (centred bit-identity + hard-panned sign)
// are re-run here at 44.1 kHz and 96 kHz.
void testSampleRateInvariants (double sampleRate)
{
    const std::string hz = std::to_string (static_cast<int> (sampleRate)) + " Hz";

    const auto off = renderDattorro (0.0f, InputPan::centre, 0.0f, false, 0.0f, false, sampleRate);
    const auto on = renderDattorro (0.97f, InputPan::centre, 0.0f, false, 0.0f, false, sampleRate);
    const auto bytes = off.left.size() * sizeof (float);
    check (off.left.size() == on.left.size()
               && std::memcmp (off.left.data(), on.left.data(), bytes) == 0
               && std::memcmp (off.right.data(), on.right.data(), bytes) == 0,
           "centred input stays bit-identical with post-steer enabled @ " + hz);

    const auto leftOn = renderDattorro (0.38f, InputPan::left, 0.0f, false, 0.0f, false, sampleRate);
    const auto rightOn = renderDattorro (0.38f, InputPan::right, 0.0f, false, 0.0f, false, sampleRate);
    const float leftOnIld = tailIldDb (leftOn);
    const float rightOnIld = tailIldDb (rightOn);
    std::cout << "       Dattorro @ " << hz << " tail ILD: hard-left " << leftOnIld
              << " dB, hard-right " << rightOnIld << " dB\n";
    check (leftOnIld >= 2.0f, "hard-left input produces at least +2 dB source-side tail ILD @ " + hz);
    check (rightOnIld <= -2.0f, "hard-right input produces at least -2 dB source-side tail ILD @ " + hz);
}
} // namespace

int main()
{
    testPresetCalibrationWiring();
    testCentredInputIsBitIdentical();
    testHardPannedInputRetainsSourceSide();
    testSampleRateInvariants (44100.0);
    testSampleRateInvariants (96000.0);

    if (failures != 0)
        std::cerr << failures << " stereo-image test(s) failed\n";

    return failures == 0 ? 0 : 1;
}
