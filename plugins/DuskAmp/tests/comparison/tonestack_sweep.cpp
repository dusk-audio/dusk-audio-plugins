// tonestack_sweep.cpp — measure tone-stack magnitude vs each knob position.
//
// Runs the same ToneStack class the plugin uses, with `mid` (and bass/treble)
// swept 0→1 in 11 steps, and reports steady-state RMS gain at several
// frequencies. The goal is to see whether the user's knob actually changes the
// response, and to quantify the unity-midband-compensation's effect on the
// mid knob (which the compensation frequency sits exactly on).

#include "ToneStack.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

static constexpr double kSr = 352800.0; // tonestack now runs at 8× of 44.1k by default

static float measureGainDb (ToneStack& ts, double freqHz, double seconds = 0.3)
{
    const int n = static_cast<int> (seconds * kSr);
    std::vector<float> buf (n);
    for (int i = 0; i < n; ++i)
        buf[i] = 0.3f * static_cast<float> (
            std::sin (2.0 * M_PI * freqHz * i / kSr));

    const int skip = static_cast<int> (0.05 * kSr);
    const int block = 512;
    for (int off = 0; off < n; off += block)
        ts.process (buf.data() + off, std::min (block, n - off));

    double sumSq = 0.0;
    int count = 0;
    for (int i = skip; i < n; ++i)
    {
        sumSq += double (buf[i]) * double (buf[i]);
        ++count;
    }
    float rms = std::sqrt (sumSq / count);
    // input RMS of 0.3 * sin = 0.3 / sqrt(2)
    float inputRms = 0.3f / std::sqrt (2.0f);
    return 20.0f * std::log10 (rms / inputRms + 1e-20f);
}

int main()
{
    const double freqs[] = { 80, 220, 440, 1000, 2000, 4000, 8000 };
    const char* labels[] = { "80", "220", "440", "1k", "2k", "4k", "8k" };
    const int NF = 7;

    auto runSweep = [&] (const char* name, ToneStack::Type type, int whichKnob) {
        // whichKnob: 0=bass, 1=mid, 2=treble
        const char* knobNames[] = { "bass", "mid", "treble" };
        std::cout << "\n=== " << name << " — sweeping " << knobNames[whichKnob]
                  << " (others at 0.5) ===\n";

        std::cout << std::setw (6) << knobNames[whichKnob] << " |";
        for (int f = 0; f < NF; ++f) std::cout << std::setw (8) << labels[f];
        std::cout << "  (dB)\n";

        for (int step = 0; step <= 10; ++step)
        {
            float v = step * 0.1f;
            ToneStack ts;
            ts.prepare (kSr);
            ts.setType (type);
            ts.setBass   (whichKnob == 0 ? v : 0.5f);
            ts.setMid    (whichKnob == 1 ? v : 0.5f);
            ts.setTreble (whichKnob == 2 ? v : 0.5f);

            std::cout << std::setw (6) << std::fixed << std::setprecision (1) << v << " |";
            for (int f = 0; f < NF; ++f)
            {
                ToneStack ts2;
                ts2.prepare (kSr);
                ts2.setType (type);
                ts2.setBass   (whichKnob == 0 ? v : 0.5f);
                ts2.setMid    (whichKnob == 1 ? v : 0.5f);
                ts2.setTreble (whichKnob == 2 ? v : 0.5f);
                float db = measureGainDb (ts2, freqs[f]);
                std::cout << std::setw (8) << std::setprecision (1) << db;
            }
            std::cout << "\n";
        }
    };

    runSweep ("American", ToneStack::Type::American, 0);
    runSweep ("American", ToneStack::Type::American, 1);
    runSweep ("American", ToneStack::Type::American, 2);

    runSweep ("British",  ToneStack::Type::British,  0);
    runSweep ("British",  ToneStack::Type::British,  1);
    runSweep ("British",  ToneStack::Type::British,  2);

    runSweep ("AC/TopBoost", ToneStack::Type::AC, 0);
    runSweep ("AC/TopBoost", ToneStack::Type::AC, 1);  // ignored for AC
    runSweep ("AC/TopBoost", ToneStack::Type::AC, 2);

    return 0;
}
