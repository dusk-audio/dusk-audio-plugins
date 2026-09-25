// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace duskaudio::busLaw
{
// Native reference-plugin measurements at 1–60 s and 44.1/48/96 kHz. The displayed
// rate has two linear clock ranges. A float control ramp reproduces the
// measured changes in speed at binade boundaries on long fades; converting
// this accumulator to double changes the audible timing. This is an empirical
// control model, not a claim about the reference's circuit implementation.
// Evidence and independent holdouts: docs/multi-comp-2-bus-fade-2026-09-08.md.
inline constexpr float fadeControlMaximum = 1.25f;

inline float fadeStep(double seconds, double sampleRate) noexcept
{
    seconds = std::clamp(seconds, 1.0, 60.0);
    const double duration = 0.817068099355422
        + 1.0471226685010504 * (std::min(seconds, 6.0) - 1.0)
        + 0.8738719147286478 * std::max(seconds - 6.0, 0.0);
    return static_cast<float>(fadeControlMaximum / (duration * sampleRate));
}

inline float fadeGain(float control) noexcept
{
    // 129 samples of the measured contour, including the quiet tail. Keep
    // the control endpoint separate from the noise floor of the capture.
    static constexpr std::array<float, 129> db{{
        0.0000000f, -0.5705590f, -1.1344022f, -1.6978282f, -2.2613077f, -2.8262469f, -3.3913063f, -3.9558202f,
        -4.5195815f, -5.0832725f, -5.6479272f, -6.2126879f, -6.7775541f, -7.3425763f, -7.9093547f, -8.4745885f,
        -9.0393610f, -9.6042680f, -10.1706347f, -10.7372580f, -11.3035085f, -11.8690200f, -12.4346881f, -13.0015108f,
        -13.5689093f, -14.1363012f, -14.7027313f, -15.2690567f, -15.8366841f, -16.4045266f, -16.9725523f, -17.5407614f,
        -18.1091556f, -18.6779181f, -19.2481265f, -19.8172848f, -20.3880359f, -20.9594638f, -21.5306438f, -22.1006800f,
        -22.6707429f, -23.2425025f, -23.8166931f, -24.3926418f, -24.9689281f, -25.5454021f, -26.1210711f, -26.6953088f,
        -27.2684897f, -27.8450235f, -28.4294375f, -29.0217361f, -29.6219295f, -30.2217549f, -30.8135815f, -31.3972886f,
        -31.9726363f, -32.5744230f, -33.2383913f, -33.9653124f, -34.7564203f, -35.6166375f, -36.5510991f, -37.5590507f,
        -38.6445948f, -39.7583890f, -40.8540846f, -41.9315628f, -42.9908448f, -44.0443361f, -45.1040959f, -46.1701399f,
        -47.2424589f, -48.3197676f, -49.4036708f, -50.4881792f, -51.5751450f, -52.6585242f, -53.7335412f, -54.7956774f,
        -55.8483953f, -56.9136838f, -58.0140034f, -59.1495880f, -60.3201618f, -61.4738500f, -62.5594179f, -63.5772729f,
        -64.5277438f, -65.5589753f, -66.8147186f, -68.2967106f, -70.0049901f, -72.1939788f, -75.1185733f, -78.7735236f,
        -83.1602330f, -87.8157829f, -92.2860364f, -96.5687528f, -100.6654790f, -104.7079758f, -108.8256043f, -113.0182085f,
        -117.2860470f, -121.5748431f, -125.8316411f, -130.0560054f, -134.2482759f, -138.4314607f, -142.6281880f, -146.8382969f,
        -151.0615694f, -155.2874859f, -159.5055128f, -163.7143597f, -167.9120702f, -172.0993118f, -176.2745348f, -180.4273549f,
        -184.5390828f, -188.5745815f, -192.4690611f, -196.1146075f, -199.3430205f, -201.9614864f, -203.8692437f, -205.1295645f,
        -220.0000000f}};
    if (control <= 0.0f) return 1.0f;
    if (control >= fadeControlMaximum) return 0.0f;
    const double scaled = static_cast<double>(control) * 128.0 / fadeControlMaximum;
    const size_t i = static_cast<size_t>(scaled);
    const float fraction = static_cast<float>(scaled - static_cast<double>(i));
    return std::pow(10.0f, (db[i] + fraction * (db[i + 1] - db[i])) * 0.05f);
}

inline float headroomDrive(int position) noexcept
{
    return std::pow(10.0f, static_cast<float>((std::clamp(position, 0, 6) - 3) * 4) * 0.05f);
}
} // namespace duskaudio::busLaw
