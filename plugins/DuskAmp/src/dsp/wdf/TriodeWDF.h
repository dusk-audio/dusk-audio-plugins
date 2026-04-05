// TriodeWDF.h — Wave Digital Filter triode amplifier stage using chowdsp_wdf.
//
// Models a complete common-cathode triode gain stage:
//
//   Vin (ResistiveVoltageSource, ~1k source impedance)
//     -> Cin (coupling capacitor, blocks DC)
//     -> Rg (grid leak resistor)
//     -> [GRID]
//
//   B+ (supply rail)
//     -> Rp (plate load resistor)
//     -> [PLATE]
//
//   GND
//     -> Rk || Ck (cathode resistor with bypass cap)
//     -> [CATHODE]
//
// The triode is the nonlinear root element that connects two WDF subtrees:
//   1. Grid subtree:  Vin -> Cin -> Rg  (series chain)
//   2. Plate-cathode subtree:  Rp in series with (Rk || Ck)
//
// Internally solves the Koren SPICE model via Newton-Raphson each sample.
//
// Reference: Norman Koren, "Improved vacuum tube models for SPICE simulations"
//            http://www.normankoren.com/Audio/Tubes/Koren_tube_spice_models.html

#pragma once

#include <algorithm>
#include <cmath>
#include <chowdsp_wdf/chowdsp_wdf.h>

namespace DuskAmpWDF
{

// ============================================================================
// Koren triode model parameters
// ============================================================================

struct TriodeModelParams
{
    float mu  = 100.0f;   // Amplification factor
    float Ex  = 1.4f;     // Plate current exponent
    float Kg1 = 1060.0f;  // Transconductance coefficient
    float Kp  = 600.0f;   // Plate characteristic parameter
    float Kvb = 300.0f;   // Knee voltage parameter
};

inline TriodeModelParams get12AX7()  { return { 100.0f, 1.4f,  1060.0f, 600.0f, 300.0f }; }
inline TriodeModelParams get12AT7()  { return {  60.0f, 1.35f,  460.0f, 300.0f, 300.0f }; }
inline TriodeModelParams get12AU7()  { return {  21.5f, 1.3f,  1180.0f, 84.0f,  300.0f }; }

// ============================================================================
// Koren plate current: Ip(Vgk, Vpk)
//
//   E1 = (Vpk / Kp) * ln(1 + exp(Kp * (1/mu + Vgk / sqrt(Kvb + Vpk^2))))
//   Ip = E1^Ex / Kg1    (if E1 > 0, else 0)
// ============================================================================

inline float korenIp (float Vgk, float Vpk, const TriodeModelParams& p)
{
    if (Vpk < 0.0f) return 0.0f;

    const float sqrtTerm = std::sqrt (p.Kvb + Vpk * Vpk);
    float expArg = p.Kp * (1.0f / p.mu + Vgk / sqrtTerm);
    expArg = std::clamp (expArg, -80.0f, 80.0f);

    const float E1 = (Vpk / p.Kp) * std::log1p (std::exp (expArg));

    if (E1 <= 0.0f) return 0.0f;
    return std::pow (E1, p.Ex) / p.Kg1;
}

// Numerical derivative dIp/dVpk (central difference, more accurate than forward)
inline float korenIp_dVpk (float Vgk, float Vpk, const TriodeModelParams& p)
{
    constexpr float h = 0.05f;
    return (korenIp (Vgk, Vpk + h, p) - korenIp (Vgk, Vpk - h, p)) / (2.0f * h);
}

// Numerical derivative dIp/dVgk
inline float korenIp_dVgk (float Vgk, float Vpk, const TriodeModelParams& p)
{
    constexpr float h = 0.05f;
    return (korenIp (Vgk + h, Vpk, p) - korenIp (Vgk - h, Vpk, p)) / (2.0f * h);
}

// ============================================================================
// TriodeStageWDF — Complete single-tube gain stage using WDF primitives
//
// Two WDF subtrees connected at the triode nonlinearity:
//
//   Grid subtree (port 1):
//     Vin (ResistiveVoltageSource) --series-- Cin (Capacitor) --series-- Rg (Resistor)
//
//   Plate-cathode subtree (port 2):
//     Rp_src (ResistiveVoltageSource, B+) --series-- cathode_pair (Rk || Ck)
//
// The triode root element reads incident waves from both subtree tips,
// extracts Thevenin equivalents, solves Newton-Raphson for Vp and Vk,
// and sends reflected waves back.
// ============================================================================

class TriodeStageWDF
{
public:
    TriodeStageWDF() = default;

    void setTubeModel (const TriodeModelParams& params) { tp_ = params; }

    // Set circuit component values before calling prepare()
    void setCircuit (float Cin, float Rg, float Rp, float Rk, float Ck, float Bplus)
    {
        Cin_val_  = Cin;
        Rg_val_   = Rg;
        Rp_val_   = Rp;
        Rk_val_   = Rk;
        Ck_val_   = Ck;
        Bplus_    = Bplus;
    }

    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        const auto fs = static_cast<float> (sampleRate);

        // -- Configure WDF component values --
        Vin_.setResistanceValue (68.0e3f);  // Guitar source into amp input resistor
        Cin_.setCapacitanceValue (Cin_val_);
        Rg_.setResistanceValue (Rg_val_);
        Rp_src_.setResistanceValue (Rp_val_);
        Rk_.setResistanceValue (Rk_val_);
        Ck_.setCapacitanceValue (Ck_val_);

        // -- Prepare capacitors for this sample rate --
        Cin_.prepare (fs);
        Ck_.prepare (fs);

        // -- Compute quiescent operating point --
        // With Vg = 0 (no input), iterate to find steady-state Vp and Vk
        float VkQ = 0.0f;
        for (int i = 0; i < 50; ++i)
        {
            float Vgk = -VkQ;
            float Vpk = solveLoadLine (Vgk) - VkQ;
            float IpQ = korenIp (Vgk, Vpk, tp_);
            VkQ = IpQ * Rk_val_;
        }
        VpQ_ = solveLoadLine (-VkQ);

        // -- Compute normalization gain --
        // Drive grid hard in both directions to find max plate swing
        float VpCutoff = solveLoadLine (-3.0f - VkQ);  // Cutoff: Vp near B+
        float VpSat    = solveLoadLine ( 1.0f - VkQ);  // Saturation: Vp low
        float maxSwing = std::max (std::abs (VpCutoff - VpQ_),
                                   std::abs (VpSat    - VpQ_));
        outputNorm_ = (maxSwing > 0.1f) ? 1.0f / maxSwing : 1.0f;

        reset();
    }

    void reset()
    {
        Cin_.reset();
        Ck_.reset();

        Vp_state_ = VpQ_;
        Vk_state_ = 0.0f;
    }

    // Process a single sample: guitar-level input voltage -> normalized plate output
    float processSample (float input)
    {
        // ================================================================
        // Step 1: Drive the grid subtree
        //   Vin -> Cin -> Rg (all in series)
        // ================================================================
        Vin_.setVoltage (input);

        // Propagate reflected waves up through the grid subtree
        // Series chain: S_cin_rg -> S_vin_cin_rg -> grid tip
        gridParallel_.reflected();

        // Extract Thevenin equivalent at the grid port
        // The reflected wave is the Thevenin open-circuit voltage of the subtree
        const float a_grid = gridParallel_.wdf.b;

        // ================================================================
        // Step 2: Drive the plate-cathode subtree
        //   Rp_src (B+ through Rp) in series with (Rk || Ck)
        // ================================================================
        Rp_src_.setVoltage (-Bplus_);  // Negate: WDF series adaptor inverts polarity

        // Propagate reflected waves up through the plate-cathode subtree
        plateCathodeSeries_.reflected();

        const float a_pc = plateCathodeSeries_.wdf.b;
        const float R_pc = plateCathodeSeries_.wdf.R;

        // ================================================================
        // Step 3: Solve the triode nonlinear equation via Newton-Raphson
        //
        // The tube connects two Thevenin circuits:
        //   Grid side:    Vg = (a_grid + b_grid) / 2, where b_grid is what we send back
        //   PC side:      Vp_node = (a_pc + b_pc) / 2
        //
        // From the grid Thevenin: Vg = Vth_g + 0 (grid draws negligible current)
        //   -> Vg ≈ a_grid (grid is high impedance, current ≈ 0)
        //
        // From the plate-cathode Thevenin:
        //   Vp_node = Vth_pc - Ip * Rth_pc
        //   where Vth_pc = a_pc, Rth_pc = R_pc
        //
        // The plate-cathode series node voltage splits:
        //   Vp = Rp_src voltage part, Vk = cathode part
        //   But since they're in series with the tube current Ip:
        //   V_pc_node = Vp - Vk  (voltage across tube from plate to cathode)
        //
        // Actually, the WDF Thevenin at the plate-cathode port gives us:
        //   Vth_pc = a_pc (the reflected wave = Thevenin open-circuit voltage)
        //   The tube current Ip flows through R_pc
        //   So: V_across_port = Vth_pc - Ip * R_pc  (voltage at the port)
        //
        // For this series combination of Rp and (Rk||Ck):
        //   Plate sees: Vp = B+ - Ip * Rp
        //   Cathode sees: Vk = Ip * Zk (impedance of Rk||Ck pair)
        //   Vpk = Vp - Vk
        //
        // We solve using the overall Thevenin:
        //   Ip * R_pc = Vth_pc - V_port
        //   where V_port = Vpk (plate-to-cathode voltage across tube)
        //   So: Vpk = Vth_pc - Ip * R_pc
        //
        // The grid voltage (assuming negligible grid current):
        //   Vg ≈ (a_grid) / 1.0 (high impedance -> V ≈ Thevenin voltage)
        //   Actually: Vg = Vth_grid (since Ig ≈ 0, no voltage drop)
        //
        // Newton-Raphson on f(Vpk) = 0:
        //   f(Vpk) = Vpk - Vth_pc + korenIp(Vg, Vpk) * R_pc
        // ================================================================

        // Grid voltage: with negligible grid current, Vg = Thevenin voltage
        // The Thevenin voltage of the grid subtree = (a_grid) / 2 * 2 = a_grid
        // More precisely: V_open = a_grid (since b_grid contributes to voltage too,
        // but at open circuit b = a, so V = (a+a)/2 = a. This is the open-circuit
        // approximation valid when grid current is negligible.)
        const float Vg = a_grid;

        // Use previous sample's Vpk as initial guess for Newton-Raphson
        float Vpk = Vp_state_ - Vk_state_;
        Vpk = std::clamp (Vpk, 0.0f, Bplus_);

        // Precompute cathode impedance fraction (invariant across NR iterations)
        const float R_cathode = cathodePar_.wdf.R;   // WDF impedance of Rk || Ck
        const float cathFrac  = R_cathode / R_pc;    // Fraction of port current that develops Vk

        for (int iter = 0; iter < 4; ++iter)
        {
            // Estimate tube current from the Thevenin load line
            const float Ip_est = (a_pc - Vpk) / R_pc;

            // Cathode voltage: Vk = Ip * R_cathode (series current through cathode network)
            const float Vk_est  = std::max (Ip_est, 0.0f) * R_cathode;
            const float Vgk_est = Vg - Vk_est;

            // Plate current from Koren model
            const float Ip = korenIp (Vgk_est, std::max (Vpk, 0.0f), tp_);

            // Residual: Thevenin equation f(Vpk) = Vpk + Ip * R_pc - a_pc = 0
            const float f = Vpk + Ip * R_pc - a_pc;

            // Jacobian df/dVpk via chain rule:
            //   Vk = ((a_pc - Vpk) / R_pc) * R_cathode
            //   dVk/dVpk = -cathFrac
            //   dVgk/dVpk = +cathFrac  (since Vgk = Vg - Vk)
            //   df/dVpk = 1 + (dIp/dVpk + dIp/dVgk * cathFrac) * R_pc
            const float dIp_dVpk = korenIp_dVpk (Vgk_est, std::max (Vpk, 0.0f), tp_);
            const float dIp_dVgk = korenIp_dVgk (Vgk_est, std::max (Vpk, 0.0f), tp_);
            const float df = 1.0f + (dIp_dVpk + dIp_dVgk * cathFrac) * R_pc;

            const float step = f / std::max (df, 0.01f);
            Vpk -= step;
            Vpk = std::clamp (Vpk, 0.0f, Bplus_);

            if (std::abs (step) < 0.05f) break;
        }

        // Final current and voltages
        const float Ip_final = std::max ((a_pc - Vpk) / R_pc, 0.0f);
        const float Vk_final = Ip_final * R_cathode;
        const float Vp_final = Bplus_ - Ip_final * Rp_src_.wdf.R;

        // Store state for next sample's initial guess
        Vp_state_ = Vp_final;
        Vk_state_ = Vk_final;

        // ================================================================
        // Step 4: Send reflected waves back into the subtrees
        //
        // For a WDF port: b = 2*V - a  (reflected wave from voltage)
        // Grid port: grid draws ~0 current, so V_grid ≈ Vg, b_grid = 2*Vg - a_grid
        //   Since Vg ≈ a_grid, b_grid ≈ a_grid (reflect back the same wave)
        // Plate-cathode port: V_port = Vpk, b_pc = 2*Vpk - a_pc
        // ================================================================

        const float b_grid = a_grid;  // Negligible grid current: reflect without loss
        const float b_pc   = 2.0f * Vpk - a_pc;

        // Send incident waves back into subtrees
        gridParallel_.incident (b_grid);
        plateCathodeSeries_.incident (b_pc);

        // ================================================================
        // Step 5: Output = plate voltage, centered on quiescent, normalized
        // ================================================================
        return -(Vp_final - VpQ_) * outputNorm_;
    }

private:
    using FloatType = float;

    // -- Tube model --
    TriodeModelParams tp_ = get12AX7();
    float Bplus_ = 300.0f;

    // -- Circuit component values (set before prepare) --
    float Cin_val_ = 0.022e-6f;
    float Rg_val_  = 1.0e6f;
    float Rp_val_  = 100.0e3f;
    float Rk_val_  = 1500.0f;
    float Ck_val_  = 25.0e-6f;

    // -- Quiescent operating point --
    float VpQ_       = 150.0f;
    float outputNorm_ = 1.0f / 50.0f;
    double sampleRate_ = 48000.0;

    // -- Newton-Raphson state --
    float Vp_state_ = 150.0f;
    float Vk_state_ = 0.0f;

    // ====================================================================
    // WDF Tree Components
    // ====================================================================

    // --- Grid subtree ---
    // Circuit: Vin --series-- Cin connects to the grid node.
    //          Rg connects from grid node to ground.
    // At the grid node, these two paths are in PARALLEL (both connect to the grid).
    //
    // Subtree: parallel(series(Vin, Cin), Rg)
    //   - series(Vin, Cin): the input signal through coupling cap
    //   - Rg: grid leak to ground (provides DC return path)

    chowdsp::wdft::ResistiveVoltageSourceT<FloatType> Vin_ { 10.0e3f };  // Guitar source ~10k
    chowdsp::wdft::CapacitorT<FloatType>              Cin_ { 0.022e-6f };
    chowdsp::wdft::ResistorT<FloatType>               Rg_  { 1.0e6f };

    // Input path: Vin in series with Cin (source + coupling cap)
    chowdsp::wdft::WDFSeriesT<FloatType,
        decltype (Vin_), decltype (Cin_)>              inputSeries_ { Vin_, Cin_ };

    // Grid node: input path in parallel with grid leak Rg
    // This correctly models: Cin connects source to grid, Rg connects grid to ground
    chowdsp::wdft::WDFParallelT<FloatType,
        decltype (inputSeries_), decltype (Rg_)>       gridParallel_ { inputSeries_, Rg_ };

    // --- Plate-cathode subtree: Rp_src --series-- (Rk || Ck) ---
    chowdsp::wdft::ResistiveVoltageSourceT<FloatType>  Rp_src_ { 100.0e3f };
    chowdsp::wdft::ResistorT<FloatType>                Rk_ { 1500.0f };
    chowdsp::wdft::CapacitorT<FloatType>               Ck_ { 25.0e-6f };

    // Cathode: Rk in parallel with Ck (bypass capacitor)
    chowdsp::wdft::WDFParallelT<FloatType,
        decltype (Rk_), decltype (Ck_)>                cathodePar_ { Rk_, Ck_ };

    // Plate load in series with cathode network
    chowdsp::wdft::WDFSeriesT<FloatType,
        decltype (Rp_src_), decltype (cathodePar_)>    plateCathodeSeries_ { Rp_src_, cathodePar_ };

    // ====================================================================
    // DC load-line solver (used at prepare time only, not per-sample)
    // ====================================================================
    float solveLoadLine (float Vgk)
    {
        float Vp = Bplus_ * 0.5f;
        for (int i = 0; i < 30; ++i)
        {
            const float Vpk = Vp;  // At DC with Vk~0 approximation for initial solve
            const float Ip  = korenIp (Vgk, Vpk, tp_);
            const float f   = Vp + Ip * Rp_val_ - Bplus_;
            const float dIp = korenIp_dVpk (Vgk, Vpk, tp_);
            const float df  = 1.0f + dIp * Rp_val_;
            const float step = f / std::max (df, 0.01f);
            Vp -= step;
            Vp = std::clamp (Vp, 1.0f, Bplus_);
            if (std::abs (step) < 0.01f) break;
        }
        return Vp;
    }
};

} // namespace DuskAmpWDF
