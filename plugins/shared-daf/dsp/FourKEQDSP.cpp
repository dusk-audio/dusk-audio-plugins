// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// FourKEQDSP.cpp — implementation of the framework-free 4K console EQ core.

#include "FourKEQDSP.hpp"

#include <algorithm>
#include <cmath>

namespace duskaudio
{

static inline float dbToGain(float db) noexcept { return std::pow(10.0f, 0.05f * db); }
static inline float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

template <size_t N>
static float interpolateAnchors(float x, const float (&xs)[N], const float (&ys)[N]) noexcept
{
    if (x <= xs[0]) return ys[0];
    for (size_t i = 1; i < N; ++i)
    {
        if (x <= xs[i])
        {
            const float t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return ys[i - 1] + t * (ys[i] - ys[i - 1]);
        }
    }
    return ys[N - 1];
}

static float interpolateAnchors(
    float x, const float* xs, const float* ys, size_t count) noexcept
{
    if (count == 0) return x;
    if (x <= xs[0]) return ys[0];
    for (size_t i = 1; i < count; ++i)
    {
        if (x <= xs[i])
        {
            const float t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return ys[i - 1] + t * (ys[i] - ys[i - 1]);
        }
    }
    return ys[count - 1];
}

template <size_t N>
static float inverseAnchorPosition(float value, const float (&values)[N]) noexcept
{
    if (value <= values[0]) return 0.0f;
    for (size_t i = 1; i < N; ++i)
    {
        if (value <= values[i])
        {
            const float width = values[i] - values[i - 1];
            const float t = width > 0.0f ? (value - values[i - 1]) / width : 0.0f;
            return (static_cast<float>(i - 1) + t) / static_cast<float>(N - 1);
        }
    }
    return 1.0f;
}

struct DenseBandCalibration
{
    float frequency[21];
    float qAtFrequency[21];
    float gain[11];
    float frequencyAtGain[11];
    float qAtGain[11];
    float qControl[21];
};

static constexpr float kEqGainControls[11] =
{
    0.0f, 1.5f, 3.0f, 4.5f, 6.0f, 7.5f,
    9.0f, 10.5f, 12.0f, 13.5f, 15.0f
    };
static constexpr float kEqQControls[21] =
{
    0.5f, 0.58f, 0.65f, 0.73f, 0.8f, 0.88f,
    0.95f, 1.05f, 1.2f, 1.35f, 1.5f, 1.65f,
    1.8f, 1.95f, 2.1f, 2.25f, 2.4f, 2.55f,
    2.7f, 2.85f, 3.0f
    };
static constexpr float kEqFrequencyControls[4][21] = {
    {
            30.0f, 36.0f, 42.0f, 48.0f, 60.0f, 75.0f,
            90.0f, 110.0f, 140.0f, 170.0f, 200.0f, 230.0f,
            260.0f, 290.0f, 315.0f, 338.0f, 360.0f, 382.0f,
            405.0f, 427.0f, 450.0f
        },
    {
            200.0f, 230.0f, 260.0f, 290.0f, 400.0f, 550.0f,
            700.0f, 820.0f, 880.0f, 940.0f, 1000.0f, 1150.0f,
            1300.0f, 1450.0f, 1600.0f, 1750.0f, 1900.0f, 2050.0f,
            2200.0f, 2350.0f, 2500.0f
        },
    {
            600.0f, 660.0f, 720.0f, 780.0f, 940.0f, 1150.0f,
            1360.0f, 1650.0f, 2100.0f, 2550.0f, 3000.0f, 3450.0f,
            3900.0f, 4350.0f, 4800.0f, 5250.0f, 5700.0f, 6100.0f,
            6400.0f, 6700.0f, 7000.0f
        },
    {
            1500.0f, 1650.0f, 1800.0f, 1950.0f, 2600.0f, 3500.0f,
            4400.0f, 5300.0f, 6200.0f, 7100.0f, 8000.0f, 8600.0f,
            9200.0f, 9800.0f, 10800.0f, 12000.0f, 13200.0f, 14200.0f,
            14800.0f, 15400.0f, 16000.0f
        }
};

struct PairCorrectionModel
{
    float inputWeight[24][6];
    float inputBias[24];
    float outputWeight[9][24];
    float outputBias[9];
};

#include "FourKEQPairCorrection.inc"

struct FilterCalibration
{
    const float* control;
    const float* frequency;
    const float* trimDb;
    size_t count;
};

#include "FourKEQFilterCalibration.inc"

static constexpr DenseBandCalibration kBrownEqBands[6] = {
    {
        {
                54.526927f, 54.5268f, 59.638562f, 69.095762f, 81.755218f, 99.771469f,
                127.602f, 169.01922f, 180.31532f, 193.20513f, 208.07055f, 225.37496f,
                245.78356f, 270.21133f, 299.91758f, 336.84465f, 383.85019f, 445.64498f,
                530.32524f, 601.23385f, 601.23381f
            },
        {
                0.48042084f, 0.48042039f, 0.48140213f, 0.48255243f, 0.48340248f, 0.48405778f,
                0.48471689f, 0.48580657f, 0.4861773f, 0.48664382f, 0.48723867f, 0.48800447f,
                0.48900163f, 0.49031045f, 0.49204222f, 0.49435437f, 0.49745045f, 0.50159917f,
                0.50709343f, 0.51130374f, 0.51130375f
            },
        {
                0.0f, 0.94717516f, 1.9307097f, 2.993341f, 4.1893973f, 5.6036997f,
                7.3763001f, 9.7899399f, 13.562968f, 14.473567f, 14.473567f
            },
        {
                188.57442f, 188.57442f, 190.6041f, 194.21026f, 199.78807f, 208.07055f,
                220.38834f, 239.41571f, 271.05186f, 278.5147f, 278.5147f
            },
        {
                0.49344888f, 0.49344888f, 0.4928644f, 0.49178702f, 0.49003294f, 0.48723867f,
                0.48267898f, 0.47468617f, 0.45844616f, 0.45390625f, 0.45390623f
            },
        {
                0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f,
                0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f,
                0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f, 0.48723867f,
                0.48723867f, 0.48723867f, 0.48723867f
            }
    },
    {
        {
                35.981072f, 35.98107f, 39.382642f, 45.675139f, 54.075625f, 65.966982f,
                84.178726f, 110.93543f, 118.16682f, 126.3849f, 135.81934f, 146.74753f,
                159.56098f, 174.80285f, 193.20772f, 215.89899f, 244.52229f, 281.76553f,
                332.22793f, 374.07419f, 374.07422f
            },
        {
                0.42107283f, 0.42107274f, 0.4243215f, 0.42869452f, 0.43261083f, 0.43614886f,
                0.43925609f, 0.44154608f, 0.44194624f, 0.44235232f, 0.44277836f, 0.44325237f,
                0.44381109f, 0.44451089f, 0.44545555f, 0.44677035f, 0.44864704f, 0.45135153f,
                0.45519389f, 0.45830572f, 0.45830562f
            },
        {
                0.0f, 0.9543137f, 1.9450048f, 3.0147377f, 4.2176458f, 5.6378136f,
                7.4135185f, 9.8220777f, 13.55975f, 14.455593f, 14.455592f
            },
        {
                135.52434f, 135.52434f, 135.55356f, 135.60737f, 135.69121f, 135.81934f,
                136.01981f, 136.36439f, 137.09854f, 137.31842f, 137.31843f
            },
        {
                0.48863078f, 0.48863078f, 0.48341368f, 0.47441793f, 0.46115288f, 0.44277836f,
                0.41804698f, 0.38494058f, 0.34039707f, 0.33139552f, 0.33139551f
            },
        {
                0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f,
                0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f,
                0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f, 0.44277836f,
                0.44277836f, 0.44277836f, 0.44277836f
            }
    },
    {
        {
                201.32119f, 201.3212f, 226.59715f, 260.88539f, 307.77403f, 375.92679f,
                483.73284f, 647.83864f, 690.74407f, 739.7266f, 796.24363f, 862.08496f,
                939.80854f, 1033.0054f, 1146.6411f, 1288.443f, 1470.0828f, 1711.2236f,
                2047.018f, 2058.9371f, 2058.937f
            },
        {
                0.64184768f, 0.64184739f, 0.63686357f, 0.63146262f, 0.62562503f, 0.61977375f,
                0.61420313f, 0.61011989f, 0.60952441f, 0.608988f, 0.60851506f, 0.60810947f,
                0.60776493f, 0.60749075f, 0.60729027f, 0.60717358f, 0.60714666f, 0.60721062f,
                0.60735317f, 0.60735908f, 0.60735907f
            },
        {
                0.0f, 0.91185996f, 1.8590923f, 2.8831856f, 4.0368273f, 5.4019972f,
                7.1132041f, 9.4392266f, 13.048844f, 14.23776f, 14.23776f
            },
        {
                792.92695f, 792.92695f, 793.24929f, 793.83509f, 794.7736f, 796.24363f,
                798.6116f, 802.75525f, 811.35368f, 814.77789f, 814.77791f
            },
        {
                0.66715239f, 0.66715239f, 0.66049405f, 0.64899551f, 0.63202594f, 0.60851506f,
                0.5768408f, 0.53440416f, 0.4773232f, 0.4616881f, 0.46168809f
            },
        {
                0.26580837f, 0.26580836f, 0.26974748f, 0.28740053f, 0.31095173f, 0.34404551f,
                0.39424272f, 0.47997875f, 0.54596899f, 0.57490716f, 0.60851497f, 0.64806864f,
                0.69524756f, 0.7525874f, 0.82368923f, 0.91427084f, 1.0337444f, 1.1983613f,
                1.4400857f, 1.6928682f, 1.6928681f
            }
    },
    {
        {
                650.2776f, 660.57571f, 745.80827f, 856.50843f, 1005.7951f, 1218.2302f,
                1543.6318f, 2103.8267f, 2319.9542f, 2492.9185f, 2693.6579f, 2929.1315f,
                3209.4f, 3548.8432f, 3967.9014f, 4499.167f, 5193.9459f, 6142.5739f,
                6392.7425f, 6392.7425f, 6392.7425f
            },
        {
                0.58483312f, 0.58460739f, 0.58300908f, 0.58156563f, 0.58041764f, 0.57984008f,
                0.58035091f, 0.58273879f, 0.58376035f, 0.58455061f, 0.58541476f, 0.58634014f,
                0.58730461f, 0.58827015f, 0.58917465f, 0.58992593f, 0.59038636f, 0.59035526f,
                0.59026024f, 0.59026024f, 0.59026024f
            },
        {
                0.0f, 0.96946043f, 1.9736898f, 3.0532416f, 4.258767f, 5.6680633f,
                7.4056078f, 9.7133035f, 13.164696f, 15.446458f, 15.446459f
            },
        {
                2685.799f, 2685.799f, 2686.5717f, 2687.9697f, 2690.198f, 2693.6579f,
                2699.1537f, 2708.5424f, 2727.1216f, 2742.3986f, 2742.3986f
            },
        {
                0.64290054f, 0.64290054f, 0.63630804f, 0.62496358f, 0.60831161f, 0.58541476f,
                0.55487773f, 0.51449414f, 0.4609782f, 0.43224028f, 0.43224031f
            },
        {
                0.25071917f, 0.25071917f, 0.25383211f, 0.2719566f, 0.2970065f, 0.33398427f,
                0.3945348f, 0.49807697f, 0.52299764f, 0.55177536f, 0.58541475f, 0.6253035f,
                0.67331852f, 0.73232941f, 0.80653144f, 0.90277439f, 1.0327504f, 1.2177894f,
                1.5028976f, 1.7200852f, 1.7200851f
            }
    },
    {
        {
                591.65026f, 605.22366f, 684.07102f, 787.48381f, 929.13314f, 1135.9661f,
                1467.9304f, 1934.4463f, 2041.8927f, 2162.7948f, 2300.1286f, 2457.4025f,
                2639.5531f, 2853.2767f, 3107.3509f, 3414.8752f, 3794.0848f, 4273.2185f,
                4896.9658f, 5109.7338f, 5109.7336f
            },
        {
                0.49421338f, 0.49512442f, 0.50006094f, 0.50566227f, 0.51190766f, 0.51854877f,
                0.52451798f, 0.52654047f, 0.52633832f, 0.52591172f, 0.52522405f, 0.5242371f,
                0.52290734f, 0.5211911f, 0.51905091f, 0.5164499f, 0.51337256f, 0.50981905f,
                0.50581789f, 0.50460745f, 0.50460747f
            },
        {
                0.0f, 0.944657f, 1.9236363f, 2.9769852f, 4.1547825f, 5.5339244f,
                7.2374354f, 9.5037979f, 12.895482f, 14.103227f, 14.103227f
            },
        {
                2500.1752f, 2500.1752f, 2477.355f, 2438.0182f, 2380.1011f, 2300.1286f,
                2192.8989f, 2050.2026f, 1860.1985f, 1802.719f, 1802.7191f
            },
        {
                0.53368863f, 0.53368863f, 0.53285679f, 0.53134816f, 0.52894225f, 0.52522405f,
                0.51940036f, 0.5097744f, 0.49198338f, 0.48482044f, 0.48482044f
            },
        {
                0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f,
                0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f,
                0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f, 0.52522405f,
                0.52522405f, 0.52522405f, 0.52522405f
            }
    },
    {
        {
                1573.3546f, 1607.7009f, 1806.3489f, 2064.9372f, 2416.3831f, 2925.8618f,
                3739.4565f, 4880.1787f, 5142.4901f, 5437.3549f, 5771.825f, 6154.1262f,
                6595.7746f, 7112.2671f, 7723.7128f, 8460.0109f, 9362.4688f, 10495.089f,
                11959.798f, 12457.744f, 12457.744f
            },
        {
                0.24923706f, 0.25052977f, 0.25755989f, 0.26562025f, 0.27479287f, 0.2850052f,
                0.29565143f, 0.30332833f, 0.30436118f, 0.30530196f, 0.306138f, 0.30685341f,
                0.30743316f, 0.30786194f, 0.3081221f, 0.30819551f, 0.30805816f, 0.30766749f,
                0.30692731f, 0.30662457f, 0.30662457f
            },
        {
                0.0f, 1.1684153f, 2.3744339f, 3.6618172f, 5.0846145f, 6.7256833f,
                8.7164648f, 11.312442f, 15.123335f, 16.469625f, 16.469625f
            },
        {
                5759.4029f, 5759.4029f, 5760.5256f, 5762.6048f, 5766.0606f, 5771.825f,
                5782.067f, 5802.5793f, 5852.5133f, 5876.8718f, 5876.8718f
            },
        {
                0.33783895f, 0.33783895f, 0.33416374f, 0.32786592f, 0.31867687f, 0.306138f,
                0.28956804f, 0.26787547f, 0.2394477f, 0.23090969f, 0.23090969f
            },
        {
                0.306138f, 0.306138f, 0.306138f, 0.306138f, 0.306138f, 0.306138f,
                0.306138f, 0.306138f, 0.306138f, 0.306138f, 0.306138f, 0.306138f,
                0.306138f, 0.306138f, 0.306138f, 0.306138f, 0.306138f, 0.306138f,
                0.306138f, 0.306138f, 0.306138f
            }
    }
};

static constexpr DenseBandCalibration kBlackEqBands[6] = {
    {
        {
                90.828085f, 90.828036f, 99.115749f, 114.65451f, 135.65484f, 165.68688f,
                212.03429f, 280.3238f, 298.74433f, 319.63987f, 343.56044f, 371.15956f,
                403.35305f, 441.38267f, 486.89329f, 542.37302f, 611.40595f, 699.8086f,
                817.54162f, 913.84865f, 913.8487f
            },
        {
                0.48015917f, 0.48015895f, 0.48069637f, 0.48160636f, 0.4828407f, 0.48481606f,
                0.48843751f, 0.49460913f, 0.49634246f, 0.4983079f, 0.50053667f, 0.50304672f,
                0.50585758f, 0.50896986f, 0.51234454f, 0.51589743f, 0.51944375f, 0.52265745f,
                0.5250058f, 0.52568413f, 0.52568417f
            },
        {
                0.0f, 1.0807153f, 2.2000104f, 3.4030733f, 4.746963f, 6.3205018f,
                8.2693555f, 10.887739f, 14.926308f, 15.895238f, 15.895238f
            },
        {
                309.50803f, 309.50803f, 313.08211f, 319.4189f, 329.17382f, 343.56044f,
                364.74803f, 396.99391f, 449.24255f, 461.2762f, 461.27619f
            },
        {
                0.50766403f, 0.50766403f, 0.50700187f, 0.50577492f, 0.50376522f, 0.50053667f,
                0.49520333f, 0.48572542f, 0.46619678f, 0.46070809f, 0.46070809f
            },
        {
                0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f,
                0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f,
                0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f, 0.50053667f,
                0.50053667f, 0.50053667f, 0.50053667f
            }
    },
    {
        {
                36.470087f, 36.470083f, 39.636702f, 45.592655f, 53.603223f, 65.009697f,
                82.537361f, 108.34387f, 115.33385f, 123.28372f, 132.41048f, 142.9768f,
                155.365f, 170.10658f, 187.91767f, 209.89074f, 237.56845f, 273.47172f,
                321.91314f, 361.83144f, 361.83145f
            },
        {
                1.1265235f, 1.1265233f, 1.1250291f, 1.1237961f, 1.1233206f, 1.1203271f,
                1.1160165f, 1.1052078f, 1.1025506f, 1.0999124f, 1.097317f, 1.0945085f,
                1.0913086f, 1.0877515f, 1.0839952f, 1.080677f, 1.0783926f, 1.0765468f,
                1.0748896f, 1.0736741f, 1.073674f
            },
        {
                0.0f, 1.390124f, 2.8246678f, 4.3559557f, 6.0500855f, 8.0119792f,
                10.416867f, 13.63622f, 18.708427f, 19.972592f, 19.972588f
            },
        {
                132.03067f, 132.03067f, 132.06791f, 132.13541f, 132.24308f, 132.41048f,
                132.67704f, 133.13514f, 134.05931f, 134.31838f, 134.31837f
            },
        {
                1.2190897f, 1.2190897f, 1.2046306f, 1.1800626f, 1.1446566f, 1.097317f,
                1.0367014f, 0.96216205f, 0.88047264f, 0.86881341f, 0.86881331f
            },
        {
                1.097317f, 1.097317f, 1.097317f, 1.097317f, 1.097317f, 1.097317f,
                1.097317f, 1.097317f, 1.097317f, 1.097317f, 1.097317f, 1.097317f,
                1.097317f, 1.097317f, 1.097317f, 1.097317f, 1.097317f, 1.097317f,
                1.097317f, 1.097317f, 1.097317f
            }
    },
    {
        {
                138.92627f, 138.92628f, 156.97178f, 181.51875f, 215.29748f, 264.81862f,
                344.35532f, 468.53307f, 501.64915f, 539.79309f, 584.25422f, 636.66141f,
                699.37528f, 775.79149f, 870.78305f, 992.17788f, 1152.465f, 1373.9433f,
                1699.9826f, 1711.9537f, 1711.9537f
            },
        {
                0.65253943f, 0.65253947f, 0.6510558f, 0.64882508f, 0.64572761f, 0.64147055f,
                0.63568258f, 0.62930335f, 0.62805194f, 0.62680065f, 0.62556173f, 0.62436152f,
                0.62320482f, 0.62210288f, 0.62104997f, 0.62000656f, 0.61891161f, 0.61762678f,
                0.6157869f, 0.61571749f, 0.6157174f
            },
        {
                0.0f, 0.99895408f, 2.0347604f, 3.1505796f, 4.4008896f, 5.870244f,
                7.6968619f, 10.156623f, 13.938678f, 15.17946f, 15.17946f
            },
        {
                581.93838f, 581.93838f, 582.16346f, 582.57227f, 583.22773f, 584.25422f,
                585.90933f, 588.80941f, 594.85255f, 597.27039f, 597.27039f
            },
        {
                0.68941533f, 0.68941533f, 0.68209875f, 0.6695064f, 0.6510144f, 0.62556173f,
                0.59159694f, 0.54666469f, 0.48741439f, 0.47149748f, 0.47149746f
            },
        {
                0.24260659f, 0.25347188f, 0.26713844f, 0.28482908f, 0.30858934f, 0.34229225f,
                0.39412803f, 0.48471187f, 0.55624831f, 0.58812923f, 0.62556161f, 0.67017679f,
                0.72419977f, 0.79107266f, 0.8759051f, 0.98716693f, 1.139638f, 1.3611609f,
                1.7131045f, 1.7720564f, 1.7720562f
            }
    },
    {
        {
                642.96067f, 653.25408f, 738.46084f, 849.15925f, 998.49838f, 1211.112f,
                1536.9802f, 2098.3048f, 2314.9142f, 2488.265f, 2689.4454f, 2925.4154f,
                3206.2328f, 3546.2664f, 3965.9261f, 4497.7459f, 5192.9106f, 6141.5285f,
                7514.954f, 8791.7832f, 8791.7832f
            },
        {
                0.59897598f, 0.59889381f, 0.59833272f, 0.59790419f, 0.5977378f, 0.59812333f,
                0.599655f, 0.60332134f, 0.60476567f, 0.60587372f, 0.60708788f, 0.60839529f,
                0.60977613f, 0.61119042f, 0.61256581f, 0.6137921f, 0.61468758f, 0.61498315f,
                0.61428006f, 0.61303728f, 0.61303728f
            },
        {
                0.0f, 1.0158728f, 2.0673434f, 3.1959199f, 4.453347f, 5.9191261f,
                7.7203848f, 10.104732f, 13.662529f, 15.503746f, 15.503746f
            },
        {
                2681.9035f, 2681.9035f, 2682.6446f, 2683.9866f, 2686.1255f, 2689.4454f,
                2694.7183f, 2703.7266f, 2721.562f, 2732.8524f, 2732.8524f
            },
        {
                0.66802434f, 0.66802434f, 0.66100381f, 0.64894368f, 0.63128474f, 0.60708788f,
                0.57497018f, 0.53278268f, 0.47750266f, 0.45409392f, 0.45409392f
            },
        {
                0.24295077f, 0.24356588f, 0.25727122f, 0.2755098f, 0.30087146f, 0.33864575f,
                0.40140182f, 0.51144438f, 0.53846562f, 0.56993442f, 0.60708788f, 0.6516663f,
                0.70609453f, 0.77416665f, 0.8616711f, 0.97846081f, 1.1424134f, 1.3891358f,
                1.7927859f, 1.7927859f, 1.7927859f
            }
    },
    {
        {
                537.81874f, 572.64029f, 653.78932f, 761.19569f, 909.73304f, 1128.9714f,
                1486.0064f, 1999.4902f, 2119.9817f, 2256.6559f, 2413.3614f, 2594.7881f,
                2807.6224f, 3061.1558f, 3368.0288f, 3747.5728f, 4228.0646f, 4855.3665f,
                5707.1064f, 6367.5557f, 6367.5555f
            },
        {
                0.52463591f, 0.52564039f, 0.52790005f, 0.53064781f, 0.5338611f, 0.53723999f,
                0.5394799f, 0.53754831f, 0.53654285f, 0.53525021f, 0.53362453f, 0.53162081f,
                0.52919073f, 0.52628759f, 0.52287863f, 0.51893674f, 0.51446861f, 0.50950096f,
                0.50408325f, 0.50069336f, 0.50069338f
            },
        {
                0.0f, 1.0073913f, 2.0503486f, 3.1703458f, 4.4191045f, 5.876046f,
                7.6680877f, 10.041824f, 13.582675f, 14.486407f, 14.486407f
            },
        {
                2624.3805f, 2624.3805f, 2600.2209f, 2558.6243f, 2497.5061f, 2413.3614f,
                2301.02f, 2152.5068f, 1957.2599f, 1914.9119f, 1914.9118f
            },
        {
                0.54395404f, 0.54395404f, 0.54291725f, 0.54105105f, 0.5381089f, 0.53362453f,
                0.52672436f, 0.51555065f, 0.49537209f, 0.48966656f, 0.48966657f
            },
        {
                0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f,
                0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f,
                0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f, 0.53362453f,
                0.53362453f, 0.53362453f, 0.53362453f
            }
    },
    {
        {
                1594.6557f, 1689.5888f, 1908.9439f, 2196.0938f, 2589.3102f, 3165.7847f,
                4103.6295f, 5456.7283f, 5774.3856f, 6134.4345f, 6546.6953f, 7022.9974f,
                7580.1157f, 8241.2037f, 9037.5239f, 10016.882f, 11249.289f, 12849.875f,
                15020.753f, 16716.676f, 16716.676f
            },
        {
                0.34553524f, 0.34962567f, 0.35831571f, 0.36812643f, 0.37893258f, 0.39032029f,
                0.40133739f, 0.40880074f, 0.40978275f, 0.4106703f, 0.41144817f, 0.4120937f,
                0.41257911f, 0.41286527f, 0.41289601f, 0.41258259f, 0.41177347f, 0.41017322f,
                0.40710579f, 0.40396014f, 0.40396013f
            },
        {
                0.0f, 1.352316f, 2.7449607f, 4.224962f, 5.8503297f, 7.7107072f,
                9.9493364f, 12.849801f, 17.113907f, 18.20433f, 18.20433f
            },
        {
                6526.4317f, 6526.4317f, 6528.3213f, 6531.7893f, 6537.4643f, 6546.6953f,
                6562.5094f, 6592.7516f, 6662.6645f, 6685.9801f, 6685.9801f
            },
        {
                0.45485434f, 0.45485434f, 0.44975239f, 0.44105243f, 0.42845348f, 0.41144817f,
                0.38932093f, 0.36104259f, 0.32576943f, 0.31852753f, 0.31852753f
            },
        {
                0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f,
                0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f,
                0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f, 0.41144817f,
                0.41144817f, 0.41144817f, 0.41144817f
            }
    }
};

static int denseBandIndex(FourKEQDSP::Band band, bool bell) noexcept
{
    switch (band)
    {
    case FourKEQDSP::Band::LF: return bell ? 1 : 0;
    case FourKEQDSP::Band::LM: return 2;
    case FourKEQDSP::Band::HM: return 3;
    case FourKEQDSP::Band::HF: return bell ? 5 : 4;
    }
    return 0;
}

static const DenseBandCalibration& denseBandCalibration(
    FourKEQDSP::Band band, bool black, bool bell) noexcept
{
    const int index = denseBandIndex(band, bell);
    return black ? kBlackEqBands[index] : kBrownEqBands[index];
}

//==============================================================================
// Static coefficient designers (shared with the UI response curve)
//==============================================================================
float FourKEQDSP::calibratedEqFrequency(float hz, float gainDb, Band band,
                                        bool black, bool bell) noexcept
{
    const DenseBandCalibration& calibration =
        denseBandCalibration(band, black, bell);
    const int bandIndex = static_cast<int>(band);
    const float base = interpolateAnchors(
        hz, kEqFrequencyControls[bandIndex], calibration.frequency);
    const float atGain = interpolateAnchors(
        std::abs(gainDb), kEqGainControls, calibration.frequencyAtGain);
    return base * atGain / calibration.frequency[10];
}

float FourKEQDSP::controlForCalibratedEqFrequency(float frequencyHz, float gainDb,
                                                  Band band, bool black, bool bell) noexcept
{
    const int bandIndex = static_cast<int>(band);
    float lo = kEqFrequencyControls[bandIndex][0];
    float hi = kEqFrequencyControls[bandIndex][20];
    if (!(frequencyHz > calibratedEqFrequency(lo, gainDb, band, black, bell)))
        return lo;
    if (!(frequencyHz < calibratedEqFrequency(hi, gainDb, band, black, bell)))
        return hi;
    for (int i = 0; i < 48; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (calibratedEqFrequency(mid, gainDb, band, black, bell) < frequencyHz ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

float FourKEQDSP::calibratedEqGain(float db, Band band,
                                   bool black, bool bell) noexcept
{
    const DenseBandCalibration& calibration =
        denseBandCalibration(band, black, bell);
    const float magnitude = interpolateAnchors(
        std::abs(db), kEqGainControls, calibration.gain);
    return std::copysign(magnitude, db);
}

float FourKEQDSP::calibratedEqQ(float q, float hz, float gainDb, Band band,
                                bool black, bool bell) noexcept
{
    const DenseBandCalibration& calibration =
        denseBandCalibration(band, black, bell);
    const int bandIndex = static_cast<int>(band);
    const float centerQ = calibration.qAtFrequency[10];
    const float atFrequency = interpolateAnchors(
        hz, kEqFrequencyControls[bandIndex], calibration.qAtFrequency);
    const float atControl = interpolateAnchors(
        q, kEqQControls, calibration.qControl);
    const float atGain = interpolateAnchors(
        std::abs(gainDb), kEqGainControls, calibration.qAtGain);
    return atFrequency * (atControl / centerQ) * (atGain / centerQ);
}

std::array<BiquadCoeffs, 3> FourKEQDSP::calibratedPairCorrection(
    double sampleRate, bool highPair, bool black,
    float firstGainDb, float firstControlHz, float firstShape,
    float secondGainDb, float secondControlHz, float secondShape) noexcept
{
    const Band firstBand = highPair ? Band::HM : Band::LF;
    const Band secondBand = highPair ? Band::HF : Band::LM;
    const bool firstBell = highPair || firstShape > 0.5f;
    const bool secondBell = !highPair || secondShape > 0.5f;
    const float firstFrequency = calibratedEqFrequency(
        firstControlHz, firstGainDb, firstBand, black, firstBell);
    const float secondFrequency = calibratedEqFrequency(
        secondControlHz, secondGainDb, secondBand, black, secondBell);

    // Convert our physical controls back to the normalized coordinates used by
    // the British console EQ measurement campaign. Q runs in the opposite
    // direction.
    float controls[6] = {
        clampf(firstGainDb / 15.0f, -1.0f, 1.0f),
        2.0f * inverseAnchorPosition(
            firstControlHz, kEqFrequencyControls[static_cast<int>(firstBand)]) - 1.0f,
        highPair
            ? 1.0f - 2.0f * inverseAnchorPosition(firstShape, kEqQControls)
            : (firstShape > 0.5f ? 1.0f : -1.0f),
        clampf(secondGainDb / 15.0f, -1.0f, 1.0f),
        2.0f * inverseAnchorPosition(
            secondControlHz, kEqFrequencyControls[static_cast<int>(secondBand)]) - 1.0f,
        highPair
            ? (secondShape > 0.5f ? 1.0f : -1.0f)
            : 1.0f - 2.0f * inverseAnchorPosition(secondShape, kEqQControls),
    };

    const PairCorrectionModel& model =
        kPairCorrectionModels[(black ? 2 : 0) + (highPair ? 1 : 0)];
    float hidden[24];
    for (int row = 0; row < 24; ++row)
    {
        float sum = model.inputBias[row];
        for (int column = 0; column < 6; ++column)
            sum += model.inputWeight[row][column] * controls[column];
        hidden[row] = std::tanh(sum);
    }
    float raw[9];
    for (int row = 0; row < 9; ++row)
    {
        float sum = model.outputBias[row];
        for (int column = 0; column < 24; ++column)
            sum += model.outputWeight[row][column] * hidden[column];
        raw[row] = sum;
    }

    const float gate = std::max(std::abs(controls[0]), std::abs(controls[3]));
    const float centerLogFrequency = 0.5f * (
        std::log(std::max(firstFrequency, 1.0f))
        + std::log(std::max(secondFrequency, 1.0f)));
    std::array<BiquadCoeffs, 3> result;
    for (int index = 0; index < 3; ++index)
    {
        const int offset = index * 3;
        const float gainDb = 20.0f * gate * std::tanh(raw[offset]);
        const float frequency = std::min(
            std::exp(centerLogFrequency + 2.5f * std::tanh(raw[offset + 1])),
            std::min(30000.0f, static_cast<float>(sampleRate * 0.49)));
        const float sigmoid = 1.0f / (1.0f + std::exp(-raw[offset + 2]));
        const float q = std::exp(std::log(0.05f) + std::log(200.0f) * sigmoid);
        result[static_cast<size_t>(index)] = Biquad::peak(
            sampleRate, frequency, gainDb, q);
    }
    return result;
}

float FourKEQDSP::calibratedFilterFrequency(float hz, bool highPass,
                                            bool black) noexcept
{
    const FilterCalibration& calibration =
        kFilterCalibrations[(black ? 2 : 0) + (highPass ? 0 : 1)];
    return interpolateAnchors(
        hz, calibration.control, calibration.frequency, calibration.count);
}

float FourKEQDSP::controlForCalibratedFilterFrequency(float frequencyHz, bool highPass,
                                                      bool black) noexcept
{
    const FilterCalibration& calibration =
        kFilterCalibrations[(black ? 2 : 0) + (highPass ? 0 : 1)];
    float lo = calibration.control[0];
    float hi = calibration.control[calibration.count - 1];
    if (!(frequencyHz > calibratedFilterFrequency(lo, highPass, black)))
        return lo;
    if (!(frequencyHz < calibratedFilterFrequency(hi, highPass, black)))
        return hi;
    for (int i = 0; i < 48; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (calibratedFilterFrequency(mid, highPass, black) < frequencyHz ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

float FourKEQDSP::calibratedHpfTrimDb(float hz, bool black) noexcept
{
    const FilterCalibration& calibration = kFilterCalibrations[black ? 2 : 0];
    return interpolateAnchors(
        hz, calibration.control, calibration.trimDb, calibration.count);
}

float FourKEQDSP::calibratedFilterQ(bool highPass, bool black) noexcept
{
    return highPass ? (black ? 0.87429652f : 0.76532684f) : 0.706625f;
}

// The modelled British console path has a small native mode-dependent nonlinear
// residue even with its excluded preamp/dynamics blocks neutral. The user
// control adds colour above that measured baseline; 0% remains the reference
// match. See the header for why the resulting loss is a closed form.
float FourKEQDSP::consoleSatAmount(bool black, float saturationPercent) noexcept
{
    const float nativeSatAmt = black ? 0.50f : 0.25f;
    const float userSatAmt = clampf(saturationPercent * 0.01f, 0.0f, 1.0f);
    return nativeSatAmt + userSatAmt * (1.0f - nativeSatAmt);
}

FourKEQDSP::ConsoleSatResponse
FourKEQDSP::consoleSatResponse(float satAmt, double oversampledRate) noexcept
{
    // Mirrors the tail of ConsoleSaturationCore::processSample in float, in the
    // same order, so these are the coefficients the audio path actually applies
    // rather than a double-precision approximation of them.
    const float trim   = 1.0f / (1.0f + satAmt * 0.15f);
    const float wetMix = clampf(satAmt * 1.4f, 0.0f, 1.0f);

    ConsoleSatResponse r;
    r.dry = (double) (1.0f - wetMix);
    r.wet = (double) (wetMix * trim);

    // Same derivation as ConsoleSaturationCore::setSampleRate, at the same rate
    // the core is prepared with (FourKEQDSP hands it the oversampled rate).
    const float sr = (float) (oversampledRate > 0.0 ? oversampledRate : 48000.0);
    const float dcCutoff = 5.0f;
    const float dcRC = 1.0f / (kDuskTwoPi * dcCutoff);
    r.dcCoeff = (double) (dcRC / (dcRC + 1.0f / sr));
    return r;
}

double FourKEQDSP::consoleSatMagnitude(const ConsoleSatResponse& r, double omega) noexcept
{
    // Wet branch H_dc(z) = (1 - z^-1) / (1 - a z^-1), summed with the flat dry
    // path. Complex, not a magnitude product: the dry path carries no phase
    // shift and the two partially cancel near DC, which is the whole effect.
    const std::complex<double> zInv = std::polar(1.0, -omega);
    const std::complex<double> hDc = (1.0 - zInv) / (1.0 - r.dcCoeff * zInv);
    return std::abs(r.dry + r.wet * hDc);
}

// Frequency-INDEPENDENT half of the drawn response: every biquad the curve
// needs, designed once, plus the saturator's flat broadband trim. Splitting
// this out matters because the graph evaluates a few hundred points per repaint
// and all of this work (four calibrated band designs, up to two three-section
// pair corrections, the filters and the HPF trim) is identical at every one of
// them.
//
// Designs at the DSP's ACTUAL processing rate: host base rate * the rate-capped
// oversampling factor, exactly as recomputeCoeffs() does. A fixed 96 kHz would
// warp the drawn curve away from the sound at any other host rate or
// oversampling factor. The HPF is the exception: the core designs it at the
// BASE rate, so it is kept separate and evaluated against a base-rate omega.
FourKEQDSP::CurveCoeffs FourKEQDSP::designCurve(const CurveControls& c) noexcept
{
    CurveCoeffs d;
    const double base = c.baseSampleRate > 0.0 ? c.baseSampleRate : 48000.0;
    d.baseSampleRate = base;
    d.sampleRate = base * (double) chooseFactor(base, (int)(c.oversampling + 0.5f));
    const bool black = c.black;

    // The saturator is always in circuit, so the flat-band response is not
    // 0.000 dB and never was (GH #169). Designed at the oversampled rate,
    // which is the rate FourKEQDSP prepares ConsoleSaturationCore with.
    d.saturation = consoleSatResponse(consoleSatAmount(black, c.saturation), d.sampleRate);

    if (c.hpfEnabled)
    {
        const float f = std::min(calibratedFilterFrequency(c.hpfFreq, true, black),
                                 static_cast<float>(base * 0.49));
        if (black)
        {
            d.hpfFirstOrder = Biquad::firstOrderHighPass(base, f * 0.96134252f);
            d.hasHpfFirstOrder = true;
        }
        d.hpf = Biquad::highPass(base, f, calibratedFilterQ(true, black));
        d.hasHpf = true;
        d.hpfTrimLinear = std::pow(10.0, calibratedHpfTrimDb(c.hpfFreq, black) / 20.0);
    }
    if (c.lpfEnabled)
    {
        const float f = std::min(calibratedFilterFrequency(c.lpfFreq, false, black),
                                 static_cast<float>(d.sampleRate * 0.49));
        d.lpf = Biquad::lowPass(d.sampleRate, f, calibratedFilterQ(false, black));
        d.hasLpf = true;
    }

    auto designBand = [&](Band band, float controlGain, float controlFreq,
                          float controlQ, bool bell, bool highShelf) {
        const float f = std::min(calibratedEqFrequency(controlFreq, controlGain, band, black, bell),
                                 static_cast<float>(d.sampleRate * 0.49));
        const float g = calibratedEqGain(controlGain, band, black, bell);
        const float q = calibratedEqQ(controlQ, controlFreq, controlGain, band, black, bell);
        return bell || band == Band::LM || band == Band::HM
            ? Biquad::peak(d.sampleRate, f, g, q)
            : Biquad::shelf(d.sampleRate, f, g, q, highShelf);
    };

    const bool lfActive = std::abs(c.lfGain) > 1.0e-6f;
    const bool lmActive = std::abs(c.lmGain) > 1.0e-6f;
    const bool hmActive = std::abs(c.hmGain) > 1.0e-6f;
    const bool hfActive = std::abs(c.hfGain) > 1.0e-6f;
    if (lfActive)
        d.bands[0] = designBand(Band::LF, c.lfGain, c.lfFreq, 1.5f, c.lfBell > 0.5f, false);
    if (lmActive)
        d.bands[1] = designBand(Band::LM, c.lmGain, c.lmFreq, c.lmQ, true, false);
    if (hmActive)
        d.bands[2] = designBand(Band::HM, c.hmGain, c.hmFreq, c.hmQ, true, false);
    if (hfActive)
        d.bands[3] = designBand(Band::HF, c.hfGain, c.hfFreq, 1.5f, c.hfBell > 0.5f, true);
    d.hasBand[0] = lfActive; d.hasBand[1] = lmActive;
    d.hasBand[2] = hmActive; d.hasBand[3] = hfActive;

    if (lfActive && lmActive)
    {
        d.lowCorrection = calibratedPairCorrection(
            d.sampleRate, false, black,
            c.lfGain, c.lfFreq, c.lfBell,
            c.lmGain, c.lmFreq, c.lmQ);
        d.hasLowCorrection = true;
    }
    if (hmActive && hfActive)
    {
        d.highCorrection = calibratedPairCorrection(
            d.sampleRate, true, black,
            c.hmGain, c.hmFreq, c.hmQ,
            c.hfGain, c.hfFreq, c.hfBell);
        d.hasHighCorrection = true;
    }
    return d;
}

// Frequency-DEPENDENT half: magnitude of the already-designed sections.
float FourKEQDSP::curveDbAt(const CurveCoeffs& d, float freq) noexcept
{
    constexpr double kPi = 3.14159265358979323846;
    const double w = 2.0 * kPi * (double) freq / d.sampleRate;

    auto magnitudeAt = [](const BiquadCoeffs& coeffs, double omega) {
        Biquad b; b.setCoeffs(coeffs); return b.magnitude(omega);
    };

    double filtMag = 1.0;
    if (d.hasHpf)
    {
        const double wBase = 2.0 * kPi * (double) freq / d.baseSampleRate;
        if (d.hasHpfFirstOrder)
            filtMag *= magnitudeAt(d.hpfFirstOrder, wBase);
        filtMag *= magnitudeAt(d.hpf, wBase);
        filtMag *= d.hpfTrimLinear;
    }
    if (d.hasLpf)
        filtMag *= magnitudeAt(d.lpf, w);

    double eqMag = 1.0;
    for (int i = 0; i < 4; ++i)
        if (d.hasBand[i])
            eqMag *= magnitudeAt(d.bands[(size_t)i], w);
    if (d.hasLowCorrection)
        for (const BiquadCoeffs& correction : d.lowCorrection)
            eqMag *= magnitudeAt(correction, w);
    if (d.hasHighCorrection)
        for (const BiquadCoeffs& correction : d.highCorrection)
            eqMag *= magnitudeAt(correction, w);

    const double magLin = eqMag * filtMag * consoleSatMagnitude(d.saturation, w);
    return 20.0f * std::log10((float) std::max(magLin, 1e-6));
}

// mode: 0 = 1x (off), 1 = 2x, 2 = 4x. Capped so the oversampled rate stays sane
// at already-high base rates (>=176.4k -> 1x, >=88.2k -> max 2x).
int FourKEQDSP::chooseFactor(double baseSampleRate, int mode) noexcept
{
    const int req = mode <= 0 ? 1 : (mode == 1 ? 2 : 4);
    const int cap = baseSampleRate >= 176400.0 ? 1 : (baseSampleRate >= 88200.0 ? 2 : 4);
    return req < cap ? req : cap;
}

//==============================================================================
// Lifecycle
//==============================================================================
void FourKEQDSP::prepare(double sampleRate, int maxBlockSize)
{
    baseSampleRate = sampleRate;
    maxBlock = std::max(1, maxBlockSize);

    scratchL.assign((size_t)maxBlock, 0.0f);
    scratchR.assign((size_t)maxBlock, 0.0f);

    curFactor = chooseFactor(baseSampleRate, (int)(pOversampling.load(R) + 0.5f));
    const double osRate = baseSampleRate * curFactor;

    for (auto& o : os) { o.setFactor(curFactor); o.reset(); }
    reportedLatency.store((int)std::lround(os[0].latency()), std::memory_order_relaxed);

    consoleSat.setSampleRate(osRate);
    consoleSat.setNoiseEnabled(false); // Measured EQ/filter path preserves digital silence.
    consoleSat.reset();

    meterDecay = std::exp(-1.0f / (0.3f * (float)baseSampleRate));
    powerSmoother.prepare(baseSampleRate, 0.03f); // ~30 ms bypass crossfade
    powerSmoother.snap(pBypass.load(R) > 0.5f ? 0.0f : 1.0f);
    lastSmoothedPower.store(powerSmoother.value(), R);

    lastHpfEnabled = pHpfEnabled.load(R) > 0.5f;
    lastLpfEnabled = pLpfEnabled.load(R) > 0.5f;

    recomputeCoeffs(osRate);
    reset();
}

void FourKEQDSP::reset()
{
    for (auto& c : ch) c.reset();
    for (auto& o : os) o.reset();
    consoleSat.reset();
    preRing.reset();
    postRing.reset();
    std::fill(scratchL.begin(), scratchL.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    inPeakL.store(0.f, R); inPeakR.store(0.f, R);
    outPeakL.store(0.f, R); outPeakR.store(0.f, R);
    autoCompValid_ = false; // force an auto-gain re-scan after a (re-)prepare / rate change
}

//==============================================================================
// Coefficients (both channels share identical coefficients, as in JUCE)
//==============================================================================
void FourKEQDSP::recomputeCoeffs(double fs) noexcept
{
    const bool black = pEqType.load(R) > 0.5f;

    // Captured British console filters: Brown/E-series HPF is two-pole,
    // Black/G-series is a split three-pole; both LPFs are two-pole. The
    // dial-to-cutoff anchors and HPF insertion trim are fitted from all six
    // hosted readback markings.
    const float hpfControl = pHpfFreq.load(R);
    // The sub-20 Hz HPF poles run at base rate. At 4x/192 kHz their float TDF-II
    // state can overflow for some in-between dial values due to coefficient
    // cancellation, while the base-rate response differs by far below 0.01 dB.
    const double hpfFs = baseSampleRate;
    const float hpfFreq = std::min(calibratedFilterFrequency(hpfControl, true, black),
                                   static_cast<float>(hpfFs * 0.49));
    const BiquadCoeffs hpf1 = black
        ? Biquad::firstOrderHighPass(hpfFs, hpfFreq * 0.96134252f)
        : BiquadCoeffs{};
    const BiquadCoeffs hpf2 = Biquad::highPass(
        hpfFs, hpfFreq, calibratedFilterQ(true, black));
    hpfTrimGain = dbToGain(calibratedHpfTrimDb(hpfControl, black));

    const float lpfFreq = std::min(
        calibratedFilterFrequency(pLpfFreq.load(R), false, black),
        static_cast<float>(fs * 0.49));
    const BiquadCoeffs lpf = Biquad::lowPass(
        fs, lpfFreq, calibratedFilterQ(false, black));

    auto bandCoeffs = [fs, black](Band band, float controlGain, float controlFreq,
                                  float controlQ, bool bell, bool highShelf) noexcept
    {
        const float freq = std::min(calibratedEqFrequency(controlFreq, controlGain, band, black, bell),
                                    static_cast<float>(fs * 0.49));
        const float gain = calibratedEqGain(controlGain, band, black, bell);
        const float q = calibratedEqQ(
            controlQ, controlFreq, controlGain, band, black, bell);
        return bell || band == Band::LM || band == Band::HM
            ? Biquad::peak(fs, freq, gain, q)
            : Biquad::shelf(fs, freq, gain, q, highShelf);
    };

    const bool lfBell = pLfBell.load(R) > 0.5f;
    const bool hfBell = pHfBell.load(R) > 0.5f;
    const BiquadCoeffs lf = bandCoeffs(
        Band::LF, pLfGain.load(R), pLfFreq.load(R), 1.5f, lfBell, false);
    const BiquadCoeffs lm = bandCoeffs(
        Band::LM, pLmGain.load(R), pLmFreq.load(R), pLmQ.load(R), true, false);
    const BiquadCoeffs hm = bandCoeffs(
        Band::HM, pHmGain.load(R), pHmFreq.load(R), pHmQ.load(R), true, false);
    const BiquadCoeffs hf = bandCoeffs(
        Band::HF, pHfGain.load(R), pHfFreq.load(R), 1.5f, hfBell, true);
    const auto lowCorrection = calibratedPairCorrection(
        fs, false, black,
        pLfGain.load(R), pLfFreq.load(R), lfBell ? 1.0f : 0.0f,
        pLmGain.load(R), pLmFreq.load(R), pLmQ.load(R));
    const auto highCorrection = calibratedPairCorrection(
        fs, true, black,
        pHmGain.load(R), pHmFreq.load(R), pHmQ.load(R),
        pHfGain.load(R), pHfFreq.load(R), hfBell ? 1.0f : 0.0f);

    for (auto& c : ch)
    {
        c.hpf1.setCoeffs(hpf1); c.hpf2.setCoeffs(hpf2);
        c.lf.setCoeffs(lf); c.lm.setCoeffs(lm);
        c.lowCorrection1.setCoeffs(lowCorrection[0]);
        c.lowCorrection2.setCoeffs(lowCorrection[1]);
        c.lowCorrection3.setCoeffs(lowCorrection[2]);
        c.hm.setCoeffs(hm); c.hf.setCoeffs(hf);
        c.highCorrection1.setCoeffs(highCorrection[0]);
        c.highCorrection2.setCoeffs(highCorrection[1]);
        c.highCorrection3.setCoeffs(highCorrection[2]);
        c.lpf.setCoeffs(lpf);
    }
}

float FourKEQDSP::calcAutoGainCompensation() const noexcept
{
    // Measure the actual serial response (reusing the coefficients just built)
    // and undo
    // its pink-weighted (equal-energy-per-octave) RMS level. Works for any band
    // combination because it evaluates the real magnitude — overlapping boosts
    // that sub-add in the summing node are counted once, not double-counted like
    // the old per-band gain*bandwidth heuristic did.
    const double osRate = baseSampleRate * (double)curFactor;
    const bool hpfEn = pHpfEnabled.load(R) > 0.5f;
    const bool lpfEn = pLpfEnabled.load(R) > 0.5f;
    const bool lfActive = std::abs(pLfGain.load(R)) > 1.0e-6f;
    const bool lmActive = std::abs(pLmGain.load(R)) > 1.0e-6f;
    const bool hmActive = std::abs(pHmGain.load(R)) > 1.0e-6f;
    const bool hfActive = std::abs(pHfGain.load(R)) > 1.0e-6f;
    const ChannelFilters& cf = ch[0];

    double sumSq = 0.0; int cnt = 0;
    // log-spaced probes, 25 Hz .. 20 kHz — one every ~1/3 octave (pink weight).
    for (double f = 25.0; f <= 20000.0; f *= 1.2589254) // 2^(1/3)
    {
        const double w = 2.0 * kDuskPi * f / osRate;
        const double wBase = 2.0 * kDuskPi * f / baseSampleRate;
        if (w >= kDuskPi) break; // above Nyquist of the OS rate
        std::complex<double> H = 1.0;
        if (lfActive) H *= cf.lf.response(w);
        if (lmActive) H *= cf.lm.response(w);
        if (lfActive && lmActive)
            H *= cf.lowCorrection1.response(w) * cf.lowCorrection2.response(w)
               * cf.lowCorrection3.response(w);
        if (hmActive) H *= cf.hm.response(w);
        if (hfActive) H *= cf.hf.response(w);
        if (hmActive && hfActive)
            H *= cf.highCorrection1.response(w) * cf.highCorrection2.response(w)
               * cf.highCorrection3.response(w);
        double mag = std::abs(H);
        if (hpfEn) mag *= hpfTrimGain * cf.hpf1.magnitude(wBase) * cf.hpf2.magnitude(wBase);
        if (lpfEn) mag *= cf.lpf.magnitude(w);
        if (std::isfinite(mag)) { sumSq += mag * mag; ++cnt; }
    }
    if (cnt == 0) return 1.0f;
    const double rms = std::sqrt(sumSq / (double)cnt);
    if (!std::isfinite(rms) || rms <= 1e-6) return 1.0f;
    float compDb = clampf(-20.0f * (float)std::log10(rms), -12.0f, 12.0f);
    return dbToGain(compDb);
}

//==============================================================================
// processBlock
//==============================================================================
void FourKEQDSP::processBlock(const float* const* inputs, float* const* outputs,
                              int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    ScopedFlushDenormals ftz;

    const int nCh = std::max(1, std::min(numChannels, kMaxChannels));

    // Chunk oversized host buffers (larger than the prepared maxBlock) so every
    // output sample is written and the scratch buffers never overflow.
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min(numSamples - offset, maxBlock);
        const float* in[kMaxChannels]; float* out[kMaxChannels];
        for (int c = 0; c < nCh; ++c) { in[c] = inputs[c] + offset; out[c] = outputs[c] + offset; }
        processChunk(in, out, nCh, n);
    }
}

void FourKEQDSP::processChunk(const float* const* inputs, float* const* outputs,
                              int nCh, int nS) noexcept
{
    // Oversampling factor may change with sample rate / user param at block rate.
    const int wantFactor = chooseFactor(baseSampleRate, (int)(pOversampling.load(R) + 0.5f));
    if (wantFactor != curFactor)
    {
        curFactor = wantFactor;
        for (auto& o : os) { o.setFactor(curFactor); o.reset(); }
        for (auto& c : ch) c.reset();
        consoleSat.setSampleRate(baseSampleRate * curFactor);
        consoleSat.reset();
        reportedLatency.store((int)std::lround(os[0].latency()), std::memory_order_relaxed);
    }
    const double osRate = baseSampleRate * curFactor;

    // Console voicing follows the mode; keep the saturator's type in sync.
    const bool black = pEqType.load(R) > 0.5f;
    consoleSat.setConsoleType(black ? ConsoleSaturationCore::ConsoleType::GSeries
                                    : ConsoleSaturationCore::ConsoleType::ESeries);

    recomputeCoeffs(osRate);

    // HPF/LPF re-enable: clear stale state so toggling on does not click.
    const bool hpfEn = pHpfEnabled.load(R) > 0.5f;
    const bool lpfEn = pLpfEnabled.load(R) > 0.5f;
    if (hpfEn && !lastHpfEnabled) for (auto& c : ch) { c.hpf1.reset(); c.hpf2.reset(); }
    if (lpfEn && !lastLpfEnabled) for (auto& c : ch) c.lpf.reset();
    lastHpfEnabled = hpfEn;
    lastLpfEnabled = lpfEn;

    float* wet[kMaxChannels] = { scratchL.data(), scratchR.data() };

    //--- input metering (pre-gain) --------------------------------------------
    float inPk[kMaxChannels] = { 0.f, 0.f };
    for (int c = 0; c < nCh; ++c)
        for (int n = 0; n < nS; ++n)
            inPk[c] = std::max(inPk[c], std::abs(inputs[c][n]));

    //--- input gain -> wet scratch --------------------------------------------
    const float inGain = dbToGain(pInputGain.load(R));
    for (int c = 0; c < nCh; ++c)
        for (int n = 0; n < nS; ++n)
            wet[c][n] = inputs[c][n] * inGain;

    //--- pre-EQ spectrum tap (mono) -------------------------------------------
    for (int n = 0; n < nS; ++n)
        preRing.push(nCh == 2 ? 0.5f * (wet[0][n] + wet[1][n]) : wet[0][n]);

    //--- M/S encode -----------------------------------------------------------
    const bool ms = pMsMode.load(R) > 0.5f && nCh == 2;
    if (ms)
        for (int n = 0; n < nS; ++n)
        {
            const float L = wet[0][n], Rr = wet[1][n];
            wet[0][n] = (L + Rr) * 0.5f;
            wet[1][n] = (L - Rr) * 0.5f;
        }

    //--- oversampled EQ + saturation chain ------------------------------------
    // Shared with designCurve() so the drawn curve shows the drive the audio
    // path actually runs at; see the header. This is NOT a free refactor to
    // assume: it changed processChunk's compiled size, and GCC contracts to FMA
    // by default, so it was verified two ways rather than argued. Both forms
    // return bit-identical floats across 204016 values spanning the whole
    // 0..100 domain in both voicings, and a render of eight configurations
    // (both voicings x four saturation settings, 102400 samples stereo each,
    // all bands and both filters live) is byte-identical before and after.
    const float satAmt = consoleSatAmount(black, pSaturation.load(R));
    const float rail = black ? 1.50602738f : 1.50020749f;
    // A 0 dB peaking/shelf section is mathematically unity. Running all ten
    // neutral biquads anyway leaves their near-unit float states active at up
    // to 192 kHz, producing an input-correlated sub-40 Hz floor around
    // -100 dBFS. The measured reference path is silent there, so bypass neutral
    // stages exactly.
    const bool lfActive = std::abs(pLfGain.load(R)) > 1.0e-6f;
    const bool lmActive = std::abs(pLmGain.load(R)) > 1.0e-6f;
    const bool hmActive = std::abs(pHmGain.load(R)) > 1.0e-6f;
    const bool hfActive = std::abs(pHfGain.load(R)) > 1.0e-6f;
    // These models contain only the residual interaction left after the two
    // isolated band responses. With either member at 0 dB there is no pair
    // interaction to correct; applying the model there double-corrects the
    // already calibrated isolated band.
    const bool lowCorrectionActive = lfActive && lmActive;
    const bool highCorrectionActive = hmActive && hfActive;
    // Do not retain a stale recursive state while a section is bypassed. This
    // also makes automation away from exactly 0 dB start from a clean state.
    for (auto& cf : ch)
    {
        if (!lfActive) cf.lf.reset();
        if (!lmActive) cf.lm.reset();
        if (!lowCorrectionActive)
        {
            cf.lowCorrection1.reset();
            cf.lowCorrection2.reset();
            cf.lowCorrection3.reset();
        }
        if (!hmActive) cf.hm.reset();
        if (!hfActive) cf.hf.reset();
        if (!highCorrectionActive)
        {
            cf.highCorrection1.reset();
            cf.highCorrection2.reset();
            cf.highCorrection3.reset();
        }
    }
    for (int c = 0; c < nCh; ++c)
    {
        ChannelFilters& cf = ch[(size_t)c];
        Oversampler& o = os[(size_t)c];
        const bool left = (c == 0);
        for (int n = 0; n < nS; ++n)
        {
            float input = wet[c][n];
            if (hpfEn)
            {
                input = cf.hpf1.process(input);
                input = cf.hpf2.process(input) * hpfTrimGain;
            }
            wet[c][n] = o.processSample(input, [&](float x) noexcept
            {
                // Captured boost and cut responses are reciprocal in dB. A
                // serial chain of calibrated peaking/shelf stages reproduces
                // that behavior and makes multi-band curves add in dB.
                if (lfActive) x = cf.lf.process(x);
                if (lmActive) x = cf.lm.process(x);
                if (lowCorrectionActive)
                {
                    x = cf.lowCorrection1.process(x);
                    x = cf.lowCorrection2.process(x);
                    x = cf.lowCorrection3.process(x);
                }
                if (hmActive) x = cf.hm.process(x);
                if (hfActive) x = cf.hf.process(x);
                if (highCorrectionActive)
                {
                    x = cf.highCorrection1.process(x);
                    x = cf.highCorrection2.process(x);
                    x = cf.highCorrection3.process(x);
                }
                if (lpfEn) x = cf.lpf.process(x);
                x = consoleSat.processSample(x, satAmt, left);
                // Measured post-EQ headroom: transparent below the rail,
                // followed by sharp odd-harmonic overload at maximum boost.
                return clampf(x, -rail, rail);
            });
        }
    }

    //--- M/S decode -----------------------------------------------------------
    if (ms)
        for (int n = 0; n < nS; ++n)
        {
            const float m = wet[0][n], s = wet[1][n];
            wet[0][n] = m + s;
            wet[1][n] = m - s;
        }

    //--- output gain * auto-gain ----------------------------------------------
    // Re-run the expensive probe scan only when an auto-gain-relevant param
    // moved; otherwise reuse the cached value (see AutoGainSnapshot).
    float autoComp = 1.0f;
    if (pAutoGain.load(R) > 0.5f)
    {
        const AutoGainSnapshot snap = { {
            pLfGain.load(R), pLfFreq.load(R), pLfBell.load(R),
            pLmGain.load(R), pLmFreq.load(R), pLmQ.load(R),
            pHmGain.load(R), pHmFreq.load(R), pHmQ.load(R),
            pHfGain.load(R), pHfFreq.load(R), pHfBell.load(R),
            pEqType.load(R), pHpfFreq.load(R), pHpfEnabled.load(R),
            pLpfFreq.load(R), pLpfEnabled.load(R), (float)curFactor } };
        if (!autoCompValid_ || snap != autoGainSnap_)
        {
            autoGainSnap_  = snap;
            autoCompCached_ = calcAutoGainCompensation();
            autoCompValid_ = true;
        }
        autoComp = autoCompCached_;
    }
    const float outGain = dbToGain(pOutputGain.load(R)) * autoComp;
    for (int c = 0; c < nCh; ++c)
        for (int n = 0; n < nS; ++n)
            wet[c][n] *= outGain;

    //--- post-EQ spectrum tap (mono) ------------------------------------------
    for (int n = 0; n < nS; ++n)
        postRing.push(nCh == 2 ? 0.5f * (wet[0][n] + wet[1][n]) : wet[0][n]);

    //--- bypass crossfade to bit-exact dry passthrough + output metering ------
    powerSmoother.setTarget(pBypass.load(R) > 0.5f ? 0.0f : 1.0f);
    float outPk[kMaxChannels] = { 0.f, 0.f };
    for (int n = 0; n < nS; ++n)
    {
        const float p = powerSmoother.next();
        for (int c = 0; c < nCh; ++c)
        {
            const float dry = inputs[c][n];
            const float y = dry + p * (wet[c][n] - dry);
            outputs[c][n] = y;
            outPk[c] = std::max(outPk[c], std::abs(y));
        }
    }
    // Publish the settled crossfade level for latency gating (see getLatencySamples).
    lastSmoothedPower.store(powerSmoother.value(), R);

    //--- metering store (peak-hold with ~300 ms release) ----------------------
    const float dk = std::pow(meterDecay, (float)nS);
    auto storePeak = [dk](std::atomic<float>& m, float pk)
    {
        const float decayed = m.load(std::memory_order_relaxed) * dk;
        m.store(pk > decayed ? pk : decayed, std::memory_order_relaxed);
    };
    storePeak(inPeakL, inPk[0]);  storePeak(inPeakR, nCh == 2 ? inPk[1] : inPk[0]);
    storePeak(outPeakL, outPk[0]); storePeak(outPeakR, nCh == 2 ? outPk[1] : outPk[0]);
}

} // namespace duskaudio
