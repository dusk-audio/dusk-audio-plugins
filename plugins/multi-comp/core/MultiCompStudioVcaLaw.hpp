// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <cmath>

// Studio VCA: the measured laws of the reference studio VCA compressor (2026-10-25 campaign; probes and fits in the
// tools repo, programme_parity/svca/). The reference is a clean digital compressor: zero latency, no colour, the
// gain applied to the same sample it is computed from. Its gain computer, identified on sample-accurate square-wave
// gain traces (gain = output / input on every sample):
//   linked instantaneous peak level -> input gain -> static law (over-threshold dB, ratio-knob position)
//   -> release stage (fixed: toward kReleaseOffsetDb; Auto: a rate state) -> attack one-pole in dB -> gain.
namespace duskaudio::studioVcaLaw
{
// Static law: gain in dB by ratio-knob position (rows, 0..1 in steps of 0.05) and dB over threshold (columns,
// -20..+50 in 1 dB steps); square-wave staircases at threshold -50 dBFS, release 0.1 s, attack fastest, each step
// read on its last 50 ms (the square-wave statics probe). Below the first column there is no reduction; beyond the last the
// last segment continues.
inline constexpr int kKnobRows = 21, kOverColumns = 71;
inline constexpr float kOverMinDb = -20.0f, kOverStepDb = 1.0f;
inline constexpr float kStaticLawDb[kKnobRows][kOverColumns] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1662f, -0.4007f, -0.6536f, -0.9254f, -1.2168f, -1.5282f, -1.8597f, -2.2116f, -2.5840f, -2.9766f, -3.3892f, -3.8216f, -4.2731f, -4.7432f, -5.2313f, -5.7366f, -6.2583f, -6.7956f, -7.3476f, -7.9134f, -8.4922f, -9.0831f, -9.6852f, -10.2978f, -10.9202f, -11.5515f, -12.1911f, -12.8383f, -13.4927f, -14.1537f, -14.8209f, -15.4937f, -16.1719f, -16.8552f, -17.5432f, -18.2359f, -18.9330f, -19.6345f, -20.3403f, -21.0503f, -21.7648f, -22.4837f, -23.2072f, -23.9356f, -24.6691f, -25.4082f, -26.1531f, -26.9046f, -27.6630f, -28.4291f, -29.2037f, -29.9878f, -30.7822f, -31.5882f, -32.4070f, -33.2402f, -34.0895f},  // knob 0.00
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1550f, -0.4066f, -0.6783f, -0.9706f, -1.2841f, -1.6193f, -1.9767f, -2.3563f, -2.7582f, -3.1823f, -3.6285f, -4.0963f, -4.5853f, -5.0948f, -5.6242f, -6.1726f, -6.7391f, -7.3230f, -7.9232f, -8.5387f, -9.1687f, -9.8122f, -10.4682f, -11.1359f, -11.8144f, -12.5029f, -13.2007f, -13.9071f, -14.6214f, -15.3431f, -16.0716f, -16.8065f, -17.5473f, -18.2937f, -19.0455f, -19.8023f, -20.5641f, -21.3306f, -22.1019f, -22.8778f, -23.6585f, -24.4441f, -25.2347f, -26.0305f, -26.8319f, -27.6392f, -28.4528f, -29.2734f, -30.1014f, -30.9377f, -31.7831f, -32.6385f, -33.5050f, -34.3838f, -35.2763f, -36.1842f, -37.1090f},  // knob 0.05
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1344f, -0.4026f, -0.6921f, -1.0040f, -1.3387f, -1.6970f, -2.0791f, -2.4854f, -2.9159f, -3.3707f, -3.8494f, -4.3518f, -4.8772f, -5.4252f, -5.9949f, -6.5856f, -7.1962f, -7.8258f, -8.4735f, -9.1381f, -9.8186f, -10.5140f, -11.2233f, -11.9456f, -12.6798f, -13.4251f, -14.1807f, -14.9457f, -15.7196f, -16.5016f, -17.2911f, -18.0877f, -18.8909f, -19.7003f, -20.5155f, -21.3363f, -22.1625f, -22.9939f, -23.8304f, -24.6720f, -25.5188f, -26.3708f, -27.2282f, -28.0913f, -28.9603f, -29.8356f, -30.7177f, -31.6071f, -32.5045f, -33.4107f, -34.3264f, -35.2528f, -36.1910f, -37.1421f, -38.1078f, -39.0896f, -40.0895f},  // knob 0.10
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1050f, -0.3887f, -0.6955f, -1.0260f, -1.3811f, -1.7614f, -2.1674f, -2.5994f, -3.0576f, -3.5420f, -4.0523f, -4.5882f, -5.1493f, -5.7348f, -6.3440f, -6.9760f, -7.6298f, -8.3044f, -8.9988f, -9.7117f, -10.4421f, -11.1888f, -11.9508f, -12.7271f, -13.5164f, -14.3181f, -15.1310f, -15.9543f, -16.7873f, -17.6293f, -18.4796f, -19.3377f, -20.2029f, -21.0749f, -21.9533f, -22.8379f, -23.7282f, -24.6243f, -25.5259f, -26.4330f, -27.3456f, -28.2639f, -29.1880f, -30.1180f, -31.0544f, -31.9975f, -32.9478f, -33.9058f, -34.8723f, -35.8480f, -36.8338f, -37.8308f, -38.8402f, -39.8633f, -40.9016f, -41.9569f, -43.0311f},  // knob 0.15
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.0669f, -0.3655f, -0.6885f, -1.0369f, -1.4115f, -1.8130f, -2.2419f, -2.6987f, -3.1836f, -3.6966f, -4.2375f, -4.8061f, -5.4017f, -6.0238f, -6.6716f, -7.3441f, -8.0403f, -8.7591f, -9.4993f, -10.2598f, -11.0394f, -11.8368f, -12.6509f, -13.4806f, -14.3246f, -15.1820f, -16.0518f, -16.9330f, -17.8249f, -18.7265f, -19.6372f, -20.5563f, -21.4833f, -22.4178f, -23.3592f, -24.3072f, -25.2615f, -26.2220f, -27.1884f, -28.1608f, -29.1392f, -30.1235f, -31.1140f, -32.1108f, -33.1144f, -34.1251f, -35.1433f, -36.1697f, -37.2050f, -38.2499f, -39.3055f, -40.3727f, -41.4529f, -42.5475f, -43.6579f, -44.7861f, -45.9341f},  // knob 0.20
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.0454f, -0.3518f, -0.6834f, -1.0411f, -1.4258f, -1.8383f, -2.2793f, -2.7490f, -3.2479f, -3.7758f, -4.3328f, -4.9184f, -5.5322f, -6.1735f, -6.8415f, -7.5352f, -8.2537f, -8.9957f, -9.7601f, -10.5456f, -11.3511f, -12.1752f, -13.0168f, -13.8746f, -14.7475f, -15.6343f, -16.5341f, -17.4459f, -18.3687f, -19.3018f, -20.2445f, -21.1959f, -22.1556f, -23.1230f, -24.0976f, -25.0792f, -26.0674f, -27.0619f, -28.0627f, -29.0696f, -30.0827f, -31.1019f, -32.1276f, -33.1598f, -34.1989f, -35.2452f, -36.2994f, -37.3620f, -38.4336f, -39.5152f, -40.6076f, -41.7120f, -42.8297f, -43.9620f, -45.1106f, -46.2773f, -47.4643f},  // knob 0.25
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.0218f, -0.3358f, -0.6756f, -1.0424f, -1.4370f, -1.8604f, -2.3131f, -2.7955f, -3.3081f, -3.8508f, -4.4235f, -5.0259f, -5.6576f, -6.3178f, -7.0058f, -7.7206f, -8.4610f, -9.2261f, -10.0144f, -10.8248f, -11.6560f, -12.5066f, -13.3755f, -14.2613f, -15.1628f, -16.0790f, -17.0087f, -17.9509f, -18.9047f, -19.8692f, -20.8436f, -21.8272f, -22.8195f, -23.8198f, -24.8277f, -25.8428f, -26.8647f, -27.8933f, -28.9283f, -29.9697f, -31.0174f, -32.0715f, -33.1322f, -34.1997f, -35.2743f, -36.3563f, -37.4464f, -38.5450f, -39.6530f, -40.7711f, -41.9003f, -43.0418f, -44.1968f, -45.3668f, -46.5534f, -47.7585f, -48.9843f},  // knob 0.30
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.3173f, -0.6653f, -1.0409f, -1.4453f, -1.8792f, -2.3434f, -2.8383f, -3.3643f, -3.9214f, -4.5097f, -5.1287f, -5.7780f, -6.4569f, -7.1646f, -7.9002f, -8.6624f, -9.4502f, -10.2623f, -11.0974f, -11.9540f, -12.8310f, -13.7270f, -14.6406f, -15.5707f, -16.5160f, -17.4755f, -18.4480f, -19.4326f, -20.4284f, -21.4346f, -22.4504f, -23.4751f, -24.5083f, -25.5493f, -26.5978f, -27.6534f, -28.7159f, -29.7852f, -30.8610f, -31.9433f, -33.0323f, -34.1280f, -35.2307f, -36.3407f, -37.4584f, -38.5843f, -39.7189f, -40.8631f, -42.0177f, -43.1836f, -44.3621f, -45.5543f, -46.7619f, -47.9864f, -49.2298f, -50.4943f},  // knob 0.35
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2966f, -0.6524f, -1.0367f, -1.4505f, -1.8948f, -2.3702f, -2.8774f, -3.4165f, -3.9879f, -4.5914f, -5.2267f, -5.8934f, -6.5908f, -7.3180f, -8.0741f, -8.8579f, -9.6682f, -10.5038f, -11.3633f, -12.2453f, -13.1484f, -14.0714f, -15.0127f, -15.9712f, -16.9455f, -17.9346f, -18.9373f, -19.9527f, -20.9797f, -22.0175f, -23.0653f, -24.1224f, -25.1883f, -26.2624f, -27.3443f, -28.4336f, -29.5300f, -30.6334f, -31.7435f, -32.8605f, -33.9842f, -35.1149f, -36.2528f, -37.3982f, -38.5514f, -39.7130f, -40.8837f, -42.0640f, -43.2550f, -44.4576f, -45.6729f, -46.9023f, -48.1473f, -49.4096f, -50.6911f, -51.9941f},  // knob 0.40
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.3027f, -0.6638f, -1.0538f, -1.4738f, -1.9247f, -2.4073f, -2.9220f, -3.4693f, -4.0494f, -4.6620f, -5.3070f, -5.9838f, -6.6919f, -7.4302f, -8.1979f, -8.9938f, -9.8167f, -10.6652f, -11.5380f, -12.4337f, -13.3509f, -14.2882f, -15.2442f, -16.2177f, -17.2073f, -18.2119f, -19.2303f, -20.2616f, -21.3047f, -22.3588f, -23.4231f, -24.4969f, -25.5796f, -26.6706f, -27.7695f, -28.8760f, -29.9897f, -31.1104f, -32.2381f, -33.3726f, -34.5141f, -35.6626f, -36.8185f, -37.9819f, -39.1533f, -40.3332f, -41.5223f, -42.7212f, -43.9309f, -45.1524f, -46.3868f, -47.6355f, -48.9000f, -50.1820f, -51.4836f, -52.8070f},  // knob 0.45
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.3086f, -0.6750f, -1.0707f, -1.4969f, -1.9545f, -2.4441f, -2.9665f, -3.5219f, -4.1106f, -4.7324f, -5.3870f, -6.0740f, -6.7927f, -7.5422f, -8.3215f, -9.1295f, -9.9649f, -10.8263f, -11.7124f, -12.6218f, -13.5530f, -14.5047f, -15.4754f, -16.4638f, -17.4687f, -18.4887f, -19.5229f, -20.5701f, -21.6293f, -22.6997f, -23.7805f, -24.8710f, -25.9704f, -27.0784f, -28.1943f, -29.3179f, -30.4489f, -31.5870f, -32.7322f, -33.8844f, -35.0436f, -36.2099f, -37.3837f, -38.5651f, -39.7547f, -40.9529f, -42.1604f, -43.3779f, -44.6063f, -45.8467f, -47.1002f, -48.3682f, -49.6522f, -50.9540f, -52.2757f, -53.6194f},  // knob 0.50
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.3144f, -0.6861f, -1.0875f, -1.5198f, -1.9840f, -2.4808f, -3.0108f, -3.5743f, -4.1716f, -4.8025f, -5.4668f, -6.1639f, -6.8933f, -7.6539f, -8.4448f, -9.2648f, -10.1127f, -10.9870f, -11.8864f, -12.8095f, -13.7548f, -14.7208f, -15.7062f, -16.7096f, -17.7296f, -18.7652f, -19.8151f, -20.8782f, -21.9536f, -23.0403f, -24.1375f, -25.2446f, -26.3608f, -27.4857f, -28.6187f, -29.7594f, -30.9077f, -32.0632f, -33.2259f, -34.3957f, -35.5726f, -36.7567f, -37.9484f, -39.1479f, -40.3557f, -41.5722f, -42.7981f, -44.0342f, -45.2813f, -46.5406f, -47.8131f, -49.1004f, -50.4039f, -51.7255f, -53.0672f, -54.4312f},  // knob 0.55
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.3201f, -0.6970f, -1.1041f, -1.5426f, -2.0134f, -2.5173f, -3.0548f, -3.6265f, -4.2324f, -4.8724f, -5.5463f, -6.2536f, -6.9935f, -7.7653f, -8.5678f, -9.3998f, -10.2602f, -11.1474f, -12.0601f, -12.9969f, -13.9562f, -14.9365f, -15.9366f, -16.9549f, -17.9902f, -19.0413f, -20.1069f, -21.1859f, -22.2774f, -23.3804f, -24.4941f, -25.6178f, -26.7508f, -27.8926f, -29.0426f, -30.2005f, -31.3661f, -32.5390f, -33.7191f, -34.9065f, -36.1011f, -37.3031f, -38.5127f, -39.7303f, -40.9562f, -42.1910f, -43.4353f, -44.6899f, -45.9558f, -47.2339f, -48.5256f, -49.8321f, -51.1551f, -52.4965f, -53.8582f, -55.2426f},  // knob 0.60
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2881f, -0.6666f, -1.0756f, -1.5162f, -1.9895f, -2.4963f, -3.0371f, -3.6124f, -4.2224f, -4.8670f, -5.5460f, -6.2589f, -7.0050f, -7.7834f, -8.5931f, -9.4328f, -10.3014f, -11.1974f, -12.1194f, -13.0659f, -14.0354f, -15.0264f, -16.0375f, -17.0673f, -18.1145f, -19.1777f, -20.2558f, -21.3476f, -22.4522f, -23.5686f, -24.6958f, -25.8333f, -26.9803f, -28.1362f, -29.3006f, -30.4729f, -31.6531f, -32.8407f, -34.0356f, -35.2379f, -36.4475f, -37.6646f, -38.8893f, -40.1221f, -41.3632f, -42.6133f, -43.8730f, -45.1431f, -46.4244f, -47.7180f, -49.0251f, -50.3472f, -51.6858f, -53.0427f, -54.4200f, -55.8200f},  // knob 0.65
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2553f, -0.6353f, -1.0462f, -1.4889f, -1.9646f, -2.4742f, -3.0181f, -3.5970f, -4.2111f, -4.8602f, -5.5441f, -6.2625f, -7.0146f, -7.7995f, -8.6163f, -9.4637f, -10.3404f, -11.2450f, -12.1762f, -13.1323f, -14.1119f, -15.1135f, -16.1357f, -17.1769f, -18.2358f, -19.3112f, -20.4017f, -21.5063f, -22.6239f, -23.7536f, -24.8944f, -26.0456f, -27.2065f, -28.3765f, -29.5552f, -30.7420f, -31.9367f, -33.1390f, -34.3487f, -35.5659f, -36.7904f, -38.0226f, -39.2624f, -40.5104f, -41.7668f, -43.0322f, -44.3072f, -45.5926f, -46.8893f, -48.1984f, -49.5210f, -50.8586f, -52.2127f, -53.5851f, -54.9780f, -56.3936f},  // knob 0.70
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2218f, -0.6034f, -1.0159f, -1.4607f, -1.9387f, -2.4509f, -2.9980f, -3.5804f, -4.1983f, -4.8518f, -5.5407f, -6.2644f, -7.0224f, -7.8138f, -8.6375f, -9.4924f, -10.3771f, -11.2903f, -12.2306f, -13.1963f, -14.1859f, -15.1980f, -16.2311f, -17.2836f, -18.3543f, -19.4417f, -20.5446f, -21.6619f, -22.7925f, -23.9354f, -25.0897f, -26.2546f, -27.4295f, -28.6136f, -29.8065f, -31.0077f, -32.2169f, -33.4338f, -34.6584f, -35.8904f, -37.1299f, -38.3771f, -39.6320f, -40.8951f, -42.1667f, -43.4474f, -44.7377f, -46.0385f, -47.3506f, -48.6751f, -50.0132f, -51.3662f, -52.7359f, -54.1238f, -55.5322f, -56.9634f},  // knob 0.75
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1877f, -0.5706f, -0.9848f, -1.4316f, -1.9119f, -2.4267f, -2.9767f, -3.5624f, -4.1842f, -4.8420f, -5.5356f, -6.2646f, -7.0284f, -7.8261f, -8.6567f, -9.5190f, -10.4116f, -11.3333f, -12.2825f, -13.2577f, -14.2573f, -15.2798f, -16.3237f, -17.3875f, -18.4698f, -19.5692f, -20.6845f, -21.8145f, -22.9580f, -24.1141f, -25.2819f, -26.4605f, -27.6492f, -28.8473f, -30.0544f, -31.2700f, -32.4937f, -33.7253f, -34.9646f, -36.2114f, -37.4659f, -38.7281f, -39.9981f, -41.2763f, -42.5632f, -43.8591f, -45.1647f, -46.4808f, -47.8083f, -49.1482f, -50.5017f, -51.8702f, -53.2553f, -54.6588f, -56.0827f, -57.5293f},  // knob 0.80
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1911f, -0.5765f, -0.9933f, -1.4428f, -1.9261f, -2.4442f, -2.9977f, -3.5871f, -4.2128f, -4.8747f, -5.5727f, -6.3063f, -7.0749f, -7.8776f, -8.7134f, -9.5811f, -10.4794f, -11.4069f, -12.3620f, -13.3434f, -14.3493f, -15.3782f, -16.4287f, -17.4992f, -18.5882f, -19.6946f, -20.8169f, -21.9540f, -23.1047f, -24.2681f, -25.4432f, -26.6292f, -27.8253f, -29.0310f, -30.2457f, -31.4690f, -32.7004f, -33.9397f, -35.1867f, -36.4414f, -37.7038f, -38.9739f, -40.2519f, -41.5382f, -42.8331f, -44.1372f, -45.4511f, -46.7754f, -48.1113f, -49.4596f, -50.8216f, -52.1987f, -53.5925f, -55.0048f, -56.4377f, -57.8934f},  // knob 0.85
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1946f, -0.5824f, -1.0018f, -1.4541f, -1.9404f, -2.4617f, -3.0187f, -3.6118f, -4.2414f, -4.9074f, -5.6097f, -6.3479f, -7.1213f, -7.9291f, -8.7701f, -9.6432f, -10.5472f, -11.4804f, -12.4416f, -13.4290f, -14.4412f, -15.4766f, -16.5336f, -17.6108f, -18.7067f, -19.8199f, -20.9492f, -22.0934f, -23.2513f, -24.4220f, -25.6044f, -26.7978f, -28.0015f, -29.2147f, -30.4370f, -31.6679f, -32.9070f, -34.1541f, -35.4089f, -36.6715f, -37.9417f, -39.2197f, -40.5058f, -41.8001f, -43.1031f, -44.4153f, -45.7374f, -47.0700f, -48.4142f, -49.7710f, -51.1415f, -52.5272f, -53.9297f, -55.3509f, -56.7927f, -58.2575f},  // knob 0.90
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.1981f, -0.5882f, -1.0102f, -1.4654f, -1.9547f, -2.4793f, -3.0397f, -3.6365f, -4.2699f, -4.9401f, -5.6468f, -6.3896f, -7.1678f, -7.9806f, -8.8268f, -9.7054f, -10.6149f, -11.5540f, -12.5211f, -13.5147f, -14.5331f, -15.5749f, -16.6385f, -17.7224f, -18.8251f, -19.9453f, -21.0816f, -22.2329f, -23.3980f, -24.5759f, -25.7657f, -26.9665f, -28.1777f, -29.3984f, -30.6283f, -31.8668f, -33.1136f, -34.3685f, -35.6311f, -36.9015f, -38.1796f, -39.4656f, -40.7596f, -42.0620f, -43.3731f, -44.6934f, -46.0237f, -47.3647f, -48.7172f, -50.0824f, -51.4614f, -52.8557f, -54.2669f, -55.6969f, -57.1477f, -58.6216f},  // knob 0.95
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.2015f, -0.5941f, -1.0187f, -1.4767f, -1.9690f, -2.4968f, -3.0607f, -3.6611f, -4.2985f, -4.9729f, -5.6839f, -6.4313f, -7.2143f, -8.0320f, -8.8835f, -9.7675f, -10.6827f, -11.6275f, -12.6006f, -13.6003f, -14.6251f, -15.6733f, -16.7435f, -17.8340f, -18.9435f, -20.0706f, -21.2140f, -22.3724f, -23.5447f, -24.7299f, -25.9270f, -27.1352f, -28.3538f, -29.5821f, -30.8196f, -32.0658f, -33.3203f, -34.5828f, -35.8533f, -37.1315f, -38.4175f, -39.7114f, -41.0134f, -42.3238f, -43.6430f, -44.9715f, -46.3100f, -47.6593f, -49.0201f, -50.3937f, -51.7813f, -53.1842f, -54.6042f, -56.0429f, -57.5027f, -58.9857f},  // knob 1.00
};

// The ratio knob's display is piecewise linear between the face marks 1.5, 2, 3, 5, 10 and 50 (shown as infinity)
// at positions 0, .2, .4, .6, .8 and 1 (0.01-step display scan). Below 1.5:1, a range only this plugin has, the
// knob-0 law is scaled toward unity at 1:1.
inline constexpr float kRatioMarks[6] = {1.5f, 2.0f, 3.0f, 5.0f, 10.0f, 50.0f};

inline float knobForRatio(float ratio) noexcept
{
    if (!(ratio > kRatioMarks[0])) return 0.0f;
    for (int i = 1; i < 6; ++i)
        if (ratio <= kRatioMarks[i])
            return 0.2f * (static_cast<float>(i - 1) + (ratio - kRatioMarks[i - 1]) / (kRatioMarks[i] - kRatioMarks[i - 1]));
    return 1.0f;
}

// knobForRatio() in reverse: the ratio at knob position `knob` (0..1), as the Studio VCA face's dial reads it.
inline float ratioForKnob(float knob) noexcept
{
    const float k = std::clamp(knob, 0.0f, 1.0f) * 5.0f;
    const int i = std::min(static_cast<int>(k), 4);
    return kRatioMarks[i] + (kRatioMarks[i + 1] - kRatioMarks[i]) * (k - static_cast<float>(i));
}

// Share of the knob-0 law applied below 1.5:1 (1 at and above 1.5:1, 0 at 1:1).
inline float lowRatioShare(float ratio) noexcept
{
    return std::clamp((ratio - 1.0f) / (kRatioMarks[0] - 1.0f), 0.0f, 1.0f);
}

// The static law at one ratio setting: the table row at its knob position (linear between rows) times its
// lowRatioShare(), built once per setting. gainDb() is then linear between the columns.
struct StaticLaw
{
    float row[kOverColumns] = {};

    void set(float ratio) noexcept
    {
        const float k = knobForRatio(ratio) * static_cast<float>(kKnobRows - 1);
        int k0 = static_cast<int>(k);
        if (k0 >= kKnobRows - 1) k0 = kKnobRows - 2;
        const float kf = k - static_cast<float>(k0);
        const float share = lowRatioShare(ratio);
        for (int j = 0; j < kOverColumns; ++j)
            row[j] = (kStaticLawDb[k0][j] + (kStaticLawDb[k0 + 1][j] - kStaticLawDb[k0][j]) * kf) * share;
    }

    // Gain in dB (<= 0) for a level `overDb` above threshold. Below the first column (and for NaN) the position
    // is that column, whose gain is 0; beyond the last column the last segment continues.
    float gainDb(float overDb) const noexcept
    {
        const float o = std::max(0.0f, (overDb - kOverMinDb) / kOverStepDb);
        const int o0 = static_cast<int>(std::min(o, static_cast<float>(kOverColumns - 2)));
        const float of = o - static_cast<float>(o0);
        const float g = row[o0] + (row[o0 + 1] - row[o0]) * of;
        return g > 0.0f ? 0.0f : g;
    }
};

// Attack: a one-pole on the gain in dB after the release stage. Its time constant against the reference's knob
// position is piecewise linear between these points (fitted on the attack holds of 21 knob positions, error
// 0.0000 dB; identical for 20, 30 and 40 dB steps). This plugin's Attack control is that time constant in
// milliseconds; the Studio VCA face's dial follows the knob law (attackMsForKnob / knobForAttackMs).
inline constexpr float kAttackKnob[4] = {0.0f, 0.5f, 0.75f, 1.0f};
inline constexpr float kAttackKnobMs[4] = {0.1805f, 7.2305f, 35.4312f, 70.6865f};

inline float attackMsForKnob(float knob) noexcept
{
    const float k = std::clamp(knob, 0.0f, 1.0f);
    int i = 1;
    while (i < 3 && k > kAttackKnob[i]) ++i;
    return kAttackKnobMs[i - 1] + (kAttackKnobMs[i] - kAttackKnobMs[i - 1])
        * (k - kAttackKnob[i - 1]) / (kAttackKnob[i] - kAttackKnob[i - 1]);
}

inline float knobForAttackMs(float ms) noexcept
{
    if (!(ms > kAttackKnobMs[0])) return 0.0f;
    for (int i = 1; i < 4; ++i)
        if (ms <= kAttackKnobMs[i])
            return kAttackKnob[i - 1] + (kAttackKnob[i] - kAttackKnob[i - 1])
                * (ms - kAttackKnobMs[i - 1]) / (kAttackKnobMs[i] - kAttackKnobMs[i - 1]);
    return 1.0f;
}

// Fixed release: the gain decays exponentially toward kReleaseOffsetDb above unity and stops at the static target
// (an attack is instant here; the attack one-pole follows). Time constant against the Release control R (ms):
// tau = 0.5654 R + 46.8 ms (fits at 0.1..4 s, worst error 0.006 dB).
inline constexpr double kReleaseOffsetDb = 0.36;
inline double releaseTauMs(double releaseMs) noexcept { return 0.5654 * releaseMs + 46.8; }

// Auto Release: the same decay toward kAutoOffsetDb with a time constant of kAutoTauMs divided by a rate state (the
// decay rate is scaled by it). Each instantaneous attack of
// delta dB scales the state by exp(-delta / kAutoDropDb); while holding, the state relaxes toward kAutoHold
// (kAutoChargeMs), while releasing toward 1 (kAutoRecoverMs). Ratio-independent; joint fit over bursts of 10 ms..3 s
// from the knee, 1..20 dB attacks on a compressed bed and trains: worst 0.074 dB outside attack onsets.
inline constexpr double kAutoTauMs = 738.3, kAutoOffsetDb = 0.336, kAutoHold = 1.2;
inline constexpr double kAutoChargeMs = 72.9, kAutoRecoverMs = 30.8, kAutoDropDb = 9.79;
inline constexpr double kAutoDropPerDb = 1.0 / kAutoDropDb;

struct Coefficients
{
    double attack = 0.0;      // per-sample one-pole coefficient of the attack stage
    double release = 0.0;     // fixed release, per sample
    double autoRate = 0.0;    // Auto: 1 / (kAutoTauMs * fs / 1000), the exponent per sample at rate 1
    double autoCharge = 0.0, autoRecover = 0.0;
    bool autoRelease = false;
};

inline double onePole(double tauMs, double sampleRate) noexcept
{
    return tauMs > 0.0 ? std::exp(-1000.0 / (tauMs * sampleRate)) : 0.0;
}

// exp(-x) for x >= 0 inside the Auto feedback, which runs through it every sample. Its arguments are nearly always
// small (the decay exponent is under 1e-4 at 22.05 kHz and above; most attack steps are a small fraction of
// kAutoDropDb), where a Taylor polynomial whose first omitted term is below 1e-17 matches exp() to double rounding.
inline double expNegative(double x) noexcept
{
    if (x < 1.0e-4)   // first omitted term x^4 / 24 < 4.2e-18
        return 1.0 - x * (1.0 - x * (0.5 - x * (1.0 / 6.0)));
    if (x < 0.02)     // first omitted term x^8 / 40320 < 6.4e-19
        return 1.0 - x * (1.0 - x * (0.5 - x * (1.0 / 6.0 - x * (1.0 / 24.0 - x * (1.0 / 120.0
            - x * (1.0 / 720.0 - x * (1.0 / 5040.0)))))));
    return std::exp(-x);
}

inline Coefficients coefficients(double attackMs, double releaseMs, bool autoRelease, double sampleRate) noexcept
{
    Coefficients c;
    c.attack = onePole(attackMs, sampleRate);
    c.release = onePole(releaseTauMs(releaseMs), sampleRate);
    c.autoRate = 1000.0 / (kAutoTauMs * sampleRate);
    c.autoCharge = onePole(kAutoChargeMs, sampleRate);
    c.autoRecover = onePole(kAutoRecoverMs, sampleRate);
    c.autoRelease = autoRelease;
    return c;
}

// One channel's gain computer. State in dB (the release and attack stages) and the Auto rate state.
struct GainComputer
{
    double release = 0.0, attack = 0.0, rate = kAutoHold;

    bool operator==(const GainComputer& o) const noexcept
    {
        return release == o.release && attack == o.attack && rate == o.rate;
    }

    // n samples whose static-law gains (dB, <= 0) are target(i): the applied gain in dB for each in gainDb. The
    // release stage is the fixed one or Auto's as c.autoRelease says. The state lives in locals for the block.
    template <class Target>
    void run(int n, Target&& target, const Coefficients& c, float* gainDb) noexcept
    {
        double rel = release, att = attack, r = rate;
        if (c.autoRelease)
            for (int i = 0; i < n; ++i)
            {
                const double t = target(i);
                const double decayed = kAutoOffsetDb + (rel - kAutoOffsetDb) * expNegative(r * c.autoRate);
                if (t < decayed)
                {
                    r = kAutoHold + (r - kAutoHold) * c.autoCharge;
                    r *= expNegative((decayed - t) * kAutoDropPerDb);
                    rel = t;
                }
                else
                {
                    r = 1.0 + (r - 1.0) * c.autoRecover;
                    rel = decayed;
                }
                att = rel + (att - rel) * c.attack;
                gainDb[i] = static_cast<float>(att);
            }
        else
            for (int i = 0; i < n; ++i)
            {
                const double t = target(i);
                const double decayed = kReleaseOffsetDb + (rel - kReleaseOffsetDb) * c.release;
                rel = t < decayed ? t : decayed;
                att = rel + (att - rel) * c.attack;
                gainDb[i] = static_cast<float>(att);
            }
        release = rel;
        attack = att;
        rate = r;
    }
};
} // namespace duskaudio::studioVcaLaw
