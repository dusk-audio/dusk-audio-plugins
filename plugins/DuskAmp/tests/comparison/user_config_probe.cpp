// user_config_probe.cpp — measure stage levels for a specific user-supplied
// preset configuration so we can compare to the plugin's output meter.
//
// Mirrors the screenshot settings (British tone stack, Clean channel,
// gain 100%, Bass/Mid/Treble 50%, Drive 30%, Sag 30%).

#include "ToneStack.h"
#include "PreampDSP.h"
#include "PowerAmp.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

static constexpr double kSr = 44100.0;

static void peakRms (const std::vector<float>& buf, int skip, float& peak, float& rms)
{
    double sumSq = 0.0;
    peak = 0.0f;
    int count = 0;
    const int n = (int) buf.size();
    for (int i = std::min (skip, n); i < n; ++i)
    {
        float a = std::abs (buf[(size_t) i]);
        peak = std::max (peak, a);
        sumSq += (double) buf[(size_t) i] * buf[(size_t) i];
        ++count;
    }
    rms = count > 0 ? (float) std::sqrt (sumSq / count) : 0.0f;
}

static float dbfs (float peak) { return peak > 1e-9f ? 20.0f * std::log10 (peak) : -120.0f; }

static void dumpStage (const char* name, const std::vector<float>& buf, int skip)
{
    float peak, rms;
    peakRms (buf, skip, peak, rms);
    std::cout << "  " << std::setw (12) << std::left << name
              << " | peak " << std::fixed << std::setprecision (3) << std::setw (6) << peak
              << " (" << std::setw (6) << std::setprecision (1) << dbfs (peak) << " dBFS)"
              << " | rms " << std::setprecision (3) << std::setw (6) << rms
              << " (" << std::setprecision (1) << std::setw (6) << dbfs (rms) << " dBFS)"
              << "\n";
}

int main()
{
    std::cout << "=== Gain-knob range check (British / Clean channel) ===\n";
    std::cout << "Test signal: 220 Hz sine, 1 s, -12 dBFS (peak 0.25) — typical guitar DI.\n\n";

    auto runAt = [] (float gain) {
        const int n = (int) (kSr * 1.0);
        std::vector<float> buf ((size_t) n);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = 0.25f * (float) std::sin (2.0 * M_PI * 220.0 * i / kSr);

        PreampDSP preamp;
        preamp.prepare (kSr);
        preamp.setChannel (PreampDSP::Channel::Clean);
        preamp.setGain (gain);
        preamp.setBright (false);

        constexpr int block = 512;
        for (int off = 0; off < n; off += block)
            preamp.process (buf.data() + off, std::min (block, n - off));

        int skip = (int) (0.1 * kSr);
        char label[32];
        std::snprintf (label, sizeof(label), "gain=%.2f", gain);
        dumpStage (label, buf, skip);
    };

    std::cout << "-- Clean channel preamp output vs GAIN knob --\n";
    for (float g : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        runAt (g);

    std::cout << "\n=== Full chain (user screenshot settings: British/Clean/Gain 100%) ===\n";
    const int n = (int) (kSr * 1.0);
    std::vector<float> buf ((size_t) n);
    for (int i = 0; i < n; ++i)
        buf[(size_t) i] = 0.25f * (float) std::sin (2.0 * M_PI * 220.0 * i / kSr);

    int skip = (int) (0.1 * kSr);
    dumpStage ("input", buf, skip);

    // Preamp: British / Clean / gain 1.0
    PreampDSP preamp;
    preamp.prepare (kSr);
    preamp.setChannel (PreampDSP::Channel::Clean);
    preamp.setGain (1.0f);
    preamp.setBright (false);

    constexpr int block = 512;
    for (int off = 0; off < n; off += block)
        preamp.process (buf.data() + off, std::min (block, n - off));
    dumpStage ("preamp", buf, skip);

    // Tone stack: British, all knobs at 0.5
    ToneStack ts;
    ts.prepare (kSr);
    ts.setType (ToneStack::Type::British);
    ts.setBass (0.5f);
    ts.setMid (0.5f);
    ts.setTreble (0.5f);
    for (int off = 0; off < n; off += block)
        ts.process (buf.data() + off, std::min (block, n - off));
    dumpStage ("tonestack", buf, skip);

    // Power amp: Marshall, drive 0.3, sag 0.3
    PowerAmp pa;
    pa.prepare (kSr);
    pa.setAmpType (PowerAmp::AmpType::Marshall);
    pa.setDrive (0.3f);
    pa.setPresence (0.5f);
    pa.setResonance (0.5f);
    pa.setSag (0.3f);
    for (int off = 0; off < n; off += block)
        pa.process (buf.data() + off, std::min (block, n - off));
    dumpStage ("poweramp", buf, skip);

    std::cout << "\nUser meter on screenshot shows output around the lower green band\n"
              << "(roughly -15 to -20 dBFS peak). Compare the 'poweramp' line above.\n"
              << "(Cab IR + NORM + output-level stages are on top of this value.)\n";

    return 0;
}
