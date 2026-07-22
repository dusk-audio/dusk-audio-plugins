#pragma once

#include <array>
#include <string_view>

namespace DuskVerbStereoImage
{
struct PresetCalibration
{
    std::string_view name;
    float steerAmount;
    float nativeStereoAmount;
    float targetIldDb;
    bool hasMeasuredAnchor;
};

struct SteerProfile
{
    std::string_view name;
    std::array<float, 3> earlyK;
    std::array<float, 3> middleK;
    std::array<float, 3> lateK;
    float holdMs;
    float fastReleaseMs;
    float slowReleaseMs;
    std::array<float, 3> wanderDepth { 0.0f, 0.0f, 0.0f };
    float wanderRateHz = 0.0f;
    float wanderDecayMs = 0.0f;
    float wanderPhase = 0.0f;
};

// Issue #123 anchor-matched calibration. Positive amounts retain the source-side
// lean in the wet tail; negative amounts reduce an engine's existing over-lean.
// A zero entry is intentional and documents that the preset already matches its
// anchor (or is natively true stereo). Surf '63 Spring and 1981 Gated Snare do
// not have external anchors; the gated preset's 0.38 value retains the neutral
// +3 dB source-side target used for its ear pass without claiming an anchor fit.
inline constexpr std::array<PresetCalibration, 20> kPresetCalibrations = {{
    { "Vocal Plate",           0.35f, 0.00f,  2.90f, true  },
    { "Vintage Vocal Plate",  -0.10f, 2.00f, -0.68f, true  },
    { "Drum Plate",            0.12f, 1.00f,  1.81f, true  },
    { "Vintage Gold Plate",    0.00f, 1.00f, -0.44f, true  },
    { "Surf '63 Spring",       0.00f, 0.00f, 26.00f, false },
    { "Bright Hall",           0.48f, 0.00f,  4.85f, true  },
    { "Vocal Hall",            0.40f, 0.00f,  7.04f, true  },
    { "Cathedral Large Hall",  0.97f, 0.00f, 11.87f, true  },
    { "Blade Runner 224",      0.29f, 0.00f,  2.38f, true  },
    { "79 Vocal Chamber",      0.00f, 1.10f,  0.11f, true  },
    { "Large Chamber",         0.29f, 0.00f,  2.17f, true  },
    { "Small Drum Room",       0.42f, 1.00f,  3.80f, true  },
    { "Medium Drum Room",     -0.32f, 0.00f,  2.41f, true  },
    { "Live Room",            -0.34f, 1.00f,  0.44f, true  },
    { "Tiled Room",            0.23f, 2.00f,  0.96f, true  },
    { "Ambience",              0.29f, 0.00f,  5.32f, true  },
    { "1981 Gated Snare",      0.38f, 0.00f,  3.00f, false },
    { "Reverse Taps",          0.00f, 0.00f, -0.10f, true  },
    { "Black Hole",            0.00f, 0.00f, -0.03f, true  },
    { "Deep Blue Day",         0.69f, 0.00f,  6.28f, true  },
}};

// Three-band, causal wet-image profiles fitted against the hard-left/right
// anchor transfers. The three unlisted anchored presets (79 Vocal Chamber,
// Vintage Gold Plate, and Black Hole) pass without steering. Centre input is
// an exact bypass in the DSP stage.
inline constexpr std::array<SteerProfile, 15> kSteerProfiles = {{
    { "Vocal Plate",          {-0.999194f, -0.999860f, -0.999894f},
                              { 0.999900f,  0.999900f,  0.999900f},
                              {-0.198594f, -0.387628f, -0.076121f},
                                3.325498f,  32.484969f,  213.060025f },
    { "Drum Plate",           { 0.999115f, -0.964484f,  0.620021f},
                              { 0.163756f, -0.999529f,  0.166394f},
                              {-0.651113f,  0.812543f, -0.303058f},
                              347.292363f,  19.817494f,  162.829810f },
    { "Bright Hall",          {-0.938968f,  0.333632f,  0.962742f},
                              {-0.999857f,  0.982228f, -0.999900f},
                              { 0.167645f, -0.999447f,  0.523451f},
                              158.599974f, 126.492427f, 1067.129252f },
    { "Vocal Hall",           {-0.999900f,  0.999900f,  0.881401f},
                              { 0.976267f, -0.857370f, -0.771060f},
                              {-0.156224f,  0.130883f,  0.188154f},
                              130.281838f,  55.706880f,  505.971808f },
    { "Cathedral Large Hall", { 0.925012f,  0.991191f,  0.999900f},
                              {-0.999900f, -0.999900f, -0.410055f},
                              { 0.860332f,  0.753670f,  0.999289f},
                              363.389375f, 179.022909f, 1911.634927f },
    { "Blade Runner 224",     {-0.175468f,  0.999900f,  0.999900f},
                              {-0.999900f, -0.999900f, -0.999900f},
                              {-0.295770f, -0.044678f,  0.060637f},
                              133.739919f,  52.643493f,  222.425826f,
                              {-0.681895f,  0.000003f,  1.496012f},
                                1.878793f, 2068.029951f, 0.153856f },
    { "Small Drum Room",      {-0.990000f,  0.900000f,  0.900000f},
                              {-0.999000f, -0.999000f, -0.999000f},
                              {-0.300000f, -0.300000f, -0.400000f},
                               80.000000f,  15.000000f,   80.000000f },
    { "Medium Drum Room",     { 0.999900f,  0.292492f, -0.999900f},
                              { 0.166396f,  0.968955f,  0.999900f},
                              {-0.999900f, -0.999900f,  0.999900f},
                              136.719830f, 100.546246f,  366.448283f },
    { "Live Room",            { 0.972364f,  0.999900f, -0.712640f},
                              { 0.982313f,  0.194656f, -0.999831f},
                              {-0.523535f, -0.136194f,  0.759479f},
                                2.098050f,  20.481730f,  202.218632f },
    { "Tiled Room",           {-0.951977f, -0.993095f,  0.999818f},
                              { 0.531915f,  0.174409f, -0.524205f},
                              { 0.809051f,  0.259909f, -0.173413f},
                              145.628651f,  18.945244f,   94.254526f },
    { "Ambience",             {-0.956815f,  0.212066f,  0.694890f},
                              { 0.999900f,  0.035900f, -0.859175f},
                              {-0.999837f, -0.517147f,  0.999485f},
                               48.554750f, 192.573738f, 1232.075092f },
    { "Vintage Vocal Plate",  {-0.248823f, -0.174873f, -0.114758f},
                              { 0.993427f,  0.996914f,  0.355765f},
                              {-0.996145f,  0.976762f, -0.999894f},
                              533.636737f,  15.045375f,   42.930747f },
    { "Large Chamber",        {-0.969331f, -0.803797f,  0.838036f},
                              {-0.998640f,  0.020109f,  0.999627f},
                              { 0.164799f, -0.282396f, -0.625336f},
                              112.359666f,  62.606808f,  380.010958f },
    { "Reverse Taps",         { 0.104900f, -0.224065f,  0.005324f},
                              {-0.683702f,  0.948521f,  0.420577f},
                              { 0.403576f,  0.999865f,  0.999900f},
                              652.892649f,  19.214673f,   19.255034f },
    { "Deep Blue Day",        { 0.070083f,  0.928497f,  0.999900f},
                              { 0.855823f,  0.283575f,  0.914550f},
                              { 0.429479f, -0.595935f,  0.549649f},
                              602.068083f, 1116.456602f, 3089.842991f },
}};

constexpr const PresetCalibration* findPresetCalibration (std::string_view name) noexcept
{
    for (const auto& calibration : kPresetCalibrations)
        if (calibration.name == name)
            return &calibration;

    return nullptr;
}

constexpr const SteerProfile* findSteerProfile (std::string_view name) noexcept
{
    for (const auto& profile : kSteerProfiles)
        if (profile.name == name)
            return &profile;

    return nullptr;
}

constexpr float steerAmountForPreset (std::string_view name) noexcept
{
    if (const auto* calibration = findPresetCalibration (name))
        return calibration->steerAmount;

    return 0.0f;
}
} // namespace DuskVerbStereoImage
