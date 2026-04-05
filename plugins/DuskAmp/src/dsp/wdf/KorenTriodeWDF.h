// KorenTriodeWDF.h — Koren SPICE triode model as a WDF nonlinear root element.
//
// Models a 12AX7 triode (or variant) as a 2-port nonlinear element:
//   Port 1: Grid circuit (input + grid leak + coupling cap)
//   Port 2: Plate-cathode circuit (plate load + cathode R/C)
//
// The Koren model equations:
//   E1 = (Vp/Kp) * ln(1 + exp(Kp * (1/mu + Vg/sqrt(Kvb + Vp^2))))
//   Ip = (E1^Ex) / Kg1    for E1 > 0, else 0
//
// Newton-Raphson solves for Vp given Vg and the WDF port impedances.

#pragma once

#include <algorithm>
#include <cmath>

namespace DuskAmpWDF {

struct TriodeParams
{
    float mu   = 100.0f;   // Amplification factor
    float Ex   = 1.4f;     // Plate current exponent
    float Kg1  = 1060.0f;  // Grid-cathode coupling coefficient
    float Kp   = 600.0f;   // Plate parameter
    float Kvb  = 300.0f;   // Knee voltage parameter
};

// 12AX7 (default), 12AT7, ECC83 variants
inline TriodeParams get12AX7Params()
{
    return { 100.0f, 1.4f, 1060.0f, 600.0f, 300.0f };
}

// Koren plate current computation
inline float korenPlateCurrent (float Vg, float Vp, const TriodeParams& p)
{
    float sqrtTerm = std::sqrt (p.Kvb + Vp * Vp);
    float expArg = p.Kp * (1.0f / p.mu + Vg / sqrtTerm);
    expArg = std::clamp (expArg, -80.0f, 80.0f);
    float E1 = (Vp / p.Kp) * std::log (1.0f + std::exp (expArg));
    if (E1 <= 0.0f) return 0.0f;
    return std::pow (E1, p.Ex) / p.Kg1;
}

// Derivative dIp/dVp (for Newton-Raphson)
inline float korenPlateCurrent_dVp (float Vg, float Vp, const TriodeParams& p)
{
    constexpr float dV = 0.1f;
    float Ip0 = korenPlateCurrent (Vg, Vp, p);
    float Ip1 = korenPlateCurrent (Vg, Vp + dV, p);
    return (Ip1 - Ip0) / dV;
}

// =============================================================================
// WDF Triode Stage: complete single-tube circuit solved per-sample
//
// Circuit:  Vin → [Cin] → [Rg] → Grid ← Tube → Plate → [Rp] → B+
//                                           ↓
//                                     [Rk || Ck] → GND
//
// The WDF tree is split into a grid subtree and a plate-cathode subtree,
// with the tube as the root nonlinear element connecting them.
// =============================================================================

class TriodeStage
{
public:
    TriodeStage() = default;

    void setParams (const TriodeParams& params) { tp_ = params; }

    void setCircuit (float Cin, float Rg, float Rp, float Rk, float Ck, float Bplus)
    {
        Cin_  = Cin;
        Rg_   = Rg;
        Rp_   = Rp;
        Rk_   = Rk;
        Ck_   = Ck;
        Bplus_ = Bplus;
    }

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;

        // Compute actual quiescent operating point (Vg=0, no signal)
        float VkQ = 0.0f;
        // Iterate to find steady-state Vk and Vp
        for (int i = 0; i < 50; ++i)
        {
            float Vgk = -VkQ;  // Grid at 0V, cathode at VkQ → Vgk = 0 - VkQ
            VpQ_ = solveLoadLine (Vgk, VkQ);
            float IpQ = korenPlateCurrent (Vgk, VpQ_ - VkQ, tp_);
            VkQ = IpQ * Rk_;
        }

        // Compute max plate swing for normalization
        // Drive the grid hard positive and hard negative to find output range
        float VpMax = solveLoadLine (-3.0f - VkQ, VkQ);  // Grid very negative → Vp high (cutoff)
        float VpMin = solveLoadLine (1.0f - VkQ, VkQ);   // Grid positive → Vp low (saturation)
        float maxSwing = std::max (std::abs (VpMax - VpQ_), std::abs (VpMin - VpQ_));
        ssGain_ = (maxSwing > 0.1f) ? maxSwing : 1.0f;   // Normalize so ±maxSwing → ±1

        // Precompute filter coefficients
        float fs = static_cast<float> (sampleRate_);
        // Coupling cap HPF: fc = 1/(2*pi*Cin*Rg)
        couplingCoeff_ = std::exp (-2.0f * 3.14159265f * (1.0f / (Cin_ * Rg_)) / fs);
        // Cathode bypass LPF: fc = 1/(2*pi*Rk*Ck)
        float wCathode = 2.0f * 3.14159265f / (Rk_ * Ck_) / fs;
        cathodeCoeff_ = wCathode / (wCathode + 1.0f);
        // DC blocker: ~20Hz highpass to aggressively remove asymmetric clipping DC
        float wDC = 2.0f * 3.14159265f * 20.0f / fs;
        dcBlockCoeff_ = wDC / (wDC + 1.0f);

        Vp_prev_ = VpQ_;
        reset();
    }

    void reset()
    {
        Vp_prev_ = VpQ_;
        Vk_state_ = 0.0f;
        couplingCapState_ = 0.0f;
        prevInput_ = 0.0f;
        dcState_ = 0.0f;
    }

    // Process one sample: input voltage → output plate voltage (inverted, centered)
    float processSample (float input)
    {
        // Coupling cap: subtract lowpassed DC from input (equivalent to HPF)
        // More robust than differential HPF at block boundaries
        couplingCapState_ += (input - couplingCapState_) * (1.0f - couplingCoeff_);
        float Vg = input - couplingCapState_;

        // === Cathode voltage (from previous iteration) ===
        float Vk = Vk_state_;

        // === Solve tube + load line via Newton-Raphson ===
        // Equation: Vp + Ip(Vg-Vk, Vp-Vk) * Rp = Bplus
        // Solve for Vp
        float Vp = Vp_prev_;  // Initial guess from previous sample

        for (int iter = 0; iter < 4; ++iter)
        {
            float Ip = korenPlateCurrent (Vg - Vk, Vp - Vk, tp_);
            float f = Vp + Ip * Rp_ - Bplus_;

            float dIp = korenPlateCurrent_dVp (Vg - Vk, Vp - Vk, tp_);
            float df = 1.0f + dIp * Rp_;

            float step = f / std::max (df, 0.01f);
            Vp -= step;
            Vp = std::clamp (Vp, 1.0f, Bplus_);

            if (std::abs (step) < 0.05f) break;
        }

        Vp_prev_ = Vp;

        // === Update cathode voltage ===
        // Cathode current = plate current (for triode, Ik ≈ Ip)
        float Ip = korenPlateCurrent (Vg - Vk, Vp - Vk, tp_);

        // Cathode bypass cap: Vk follows Ip * Rk at low freq, bypassed at high freq
        float cathodeTarget = Ip * Rk_;
        Vk_state_ += cathodeCoeff_ * (cathodeTarget - Vk_state_);

        // === Output: plate voltage centered on quiescent, normalized by gain ===
        float output = -(Vp - VpQ_) / ssGain_;  // Invert + normalize to ~unity at small signal

        // DC blocker: remove any residual DC from asymmetric clipping
        float dcBlocked = output - dcState_;
        dcState_ += dcBlocked * dcBlockCoeff_;

        return dcBlocked;
    }

private:
    TriodeParams tp_ = get12AX7Params();
    double sampleRate_ = 48000.0;

    // Circuit component values
    float Cin_   = 0.022e-6f;   // Coupling cap (F)
    float Rg_    = 1.0e6f;      // Grid leak (ohm)
    float Rp_    = 100.0e3f;    // Plate load (ohm)
    float Rk_    = 1500.0f;     // Cathode resistor (ohm)
    float Ck_    = 25.0e-6f;    // Cathode bypass cap (F)
    float Bplus_ = 300.0f;      // Supply voltage (V)

    // Quiescent operating point (computed at prepare time)
    float VpQ_ = 150.0f;        // Quiescent plate voltage
    float ssGain_ = 50.0f;      // Small-signal voltage gain (for normalization)

    // Precomputed filter coefficients
    float couplingCoeff_ = 0.999f;   // Coupling cap HPF
    float cathodeCoeff_ = 0.01f;     // Cathode bypass LPF
    float dcBlockCoeff_ = 0.0f;      // Output DC blocker (~5Hz)

    // State
    float Vp_prev_ = 150.0f;    // Previous plate voltage (Newton-Raphson seed)
    float Vk_state_ = 0.0f;     // Cathode voltage (LP filtered)
    float couplingCapState_ = 0.0f;
    float prevInput_ = 0.0f;
    float dcState_ = 0.0f;      // DC blocker state

    // Internal load line solver: Vgk = grid-cathode voltage, Vk = cathode voltage
    // Solves: Vp + Ip(Vgk, Vp - Vk) * Rp = B+
    float solveLoadLine (float Vgk, float Vk)
    {
        float Vp = Bplus_ * 0.5f;
        for (int iter = 0; iter < 20; ++iter)
        {
            float Ip = korenPlateCurrent (Vgk, Vp - Vk, tp_);
            float f = Vp + Ip * Rp_ - Bplus_;
            float dIp = korenPlateCurrent_dVp (Vgk, Vp - Vk, tp_);
            float df = 1.0f + dIp * Rp_;
            float step = f / std::max (df, 0.01f);
            Vp -= step;
            Vp = std::clamp (Vp, 1.0f, Bplus_);
            if (std::abs (step) < 0.01f) break;
        }
        return Vp;
    }

};

} // namespace DuskAmpWDF
