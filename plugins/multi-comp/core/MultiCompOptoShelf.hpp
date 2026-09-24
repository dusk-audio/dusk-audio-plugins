// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <cmath>

namespace duskaudio
{
// Reference opto leveler linear HF response, white noise at 44.1/48/96 kHz,
// 2026-09-16: +.81 dB at 20 kHz relative to 1 kHz, independent of Gain.
// linear-path-20260916/{opto-shelf-digital-per-rate,opto-shelf-fit}.json;
// make_opto_linear_core.py (opto-linear): residual <= .036 dB through 18 kHz
// below output limiting; the shared 44.1 kHz oversampler rolloff is unchanged.
// Preserve the lab's rounded coefficients and arithmetic for sample identity.
// Runs at the processing rate, before the OPTO output ceiling, with unity DC.
class OptoHfShelf
{
public:
    // Coefficient changes preserve history during automated oversampling.
    void prepare(double processingRate) noexcept
    {
        static constexpr double rates[8] = {
            44100.0, 48000.0, 88200.0, 96000.0,
            176400.0, 192000.0, 352800.0, 384000.0};
        static constexpr double zq[8] = {
            -0.135864990, -0.081904665, 0.123852156, 0.152129595,
            0.373136347, 0.404622531, 0.611837309, 0.636772238};
        static constexpr double pp[8] = {
            -0.184163959, -0.134048999, 0.028834134, 0.050764062,
            0.242531233, 0.273213834, 0.495989458, 0.525133728};
        const double fsr = processingRate;
        double q = 0.0, p = 0.0;
        if (fsr > rates[7])
        {
            // Analog matched-Z continuation above the measured rate table.
            const double pi = 3.14159265358979323846;
            p = std::exp(-2.0 * pi * 39345.496 / fsr);
            const double ny = std::sqrt(
                (1.0 + (0.5 * fsr / 27579.759) * (0.5 * fsr / 27579.759))
                / (1.0 + (0.5 * fsr / 39345.496) * (0.5 * fsr / 39345.496)));
            const double A = ny * (1.0 + p) / (1.0 - p);
            q = (A - 1.0) / (A + 1.0);
        }
        else if (fsr <= rates[0])
        {
            q = zq[0];
            p = pp[0];
        }
        else
        {
            // Interpolate in reciprocal sample rate, as in opto-linear.
            for (int i = 1; i < 8; ++i)
            {
                if (fsr > rates[i]) continue;
                const double t = (1.0 / fsr - 1.0 / rates[i - 1])
                    / (1.0 / rates[i] - 1.0 / rates[i - 1]);
                q = zq[i - 1] + t * (zq[i] - zq[i - 1]);
                p = pp[i - 1] + t * (pp[i] - pp[i - 1]);
                break;
            }
        }
        const double g = (1.0 - p) / (1.0 - q);   // unity at DC
        b0 = static_cast<float>(g);
        b1 = static_cast<float>(-g * q);
        a1 = static_cast<float>(-p);
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process(float input) noexcept
    {
        const float output = b0 * input + b1 * x1 - a1 * y1;
        x1 = input;
        y1 = output;
        return output;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
};

// Reference opto leveler sub-audio high-passes, 2026-09-22 (T4-P5-OUTPUT,
// hp2-fit.json). The PR-0 impulse tails at five Gains sum to zero, and
// their early level splits into a part proportional to the LINEAR Gain and
// a part proportional to the clipped pulse area: one 2nd-order high-pass
// acts before the output nonlinearity (1.0108 Hz, Q .8388) and one after it
// (0.9891 Hz, Q .8208). Tail residual -28..-43 dB relative at every Gain,
// held-out Gains included. Bilinear transform prewarped at the corner;
// double-precision transposed direct form II, because the poles sit within
// 1e-4 of z = 1 at the oversampled processing rates.
inline constexpr double kOptoPreHighPassHz = 1.0107558853;
inline constexpr double kOptoPreHighPassQ = 0.8388057050;
inline constexpr double kOptoPostHighPassHz = 0.9890982535;
inline constexpr double kOptoPostHighPassQ = 0.8207559790;

class OptoSubsonicHighPass
{
public:
    // Coefficient changes preserve history during automated oversampling.
    void prepare(double processingRate, double cornerHz, double q) noexcept
    {
        const double pi = 3.14159265358979323846;
        const double k = std::tan(pi * cornerHz / processingRate);
        const double norm = 1.0 / (1.0 + k / q + k * k);
        b0 = norm;
        b1 = -2.0 * norm;
        b2 = norm;
        a1 = 2.0 * (k * k - 1.0) * norm;
        a2 = (1.0 - k / q + k * k) * norm;
    }

    void reset() noexcept { s1 = s2 = 0.0; }

    float process(float input) noexcept
    {
        const double x = input;
        const double y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;
};
} // namespace duskaudio
