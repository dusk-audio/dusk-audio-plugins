// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// CaptureStimulus.hpp - the fixed stereo programme the core capture test feeds
// (GH #184, migration plan step 1).
//
// The programme must be bit-identical on every platform and in every build,
// or a frozen reading could not tell a changed meter from a changed input. So
// it uses no libm call and no library distribution: tones come from an exact
// integer phase and a fixed polynomial in IEEE double, noise from mt19937's
// standard-defined integer sequence, and CaptureStimulus.cpp is compiled
// without floating-point contraction (see core/CMakeLists.txt). The test
// checks a hash of the programme against the frozen one before it trusts any
// reading.
#pragma once

#include <cstdint>
#include <vector>

namespace spectrum_capture
{
// Seven segments, each a whole number of 100 ms loudness blocks:
//   [0, 1) s   1 kHz at -62 dBFS (about -65 LUFS: inside the -70 LUFS absolute
//              gate by 5 dB), the first blocks the gate stores
//   [1, 2) s   1 kHz at -72 dBFS (about -75 LUFS: outside the gate by 5 dB)
//   [2, 6) s   1 kHz at -18 dBFS; right channel 7 samples late (partial correlation)
//   [6, 9) s   1 kHz at -30 dBFS left, 1.5 kHz at -27 dBFS right (near-zero
//              correlation; ~10 LU below [2, 6) s). The quiet first seconds
//              pull the ungated mean down, so the relative gate never drops
//              these blocks; test_core_capture.cpp pins the relative gates
//              with dedicated block runs instead.
//   [9, 10) s  digital silence (below the absolute gate)
//   [10, 12) s fs/4 at 45 degrees, amplitude 1.25: samples peak at 0.884 while the
//              waveform reaches 1.25 (intersample over, latches the clip flag)
//   [12, 14) s white noise, right = -0.5 x left: the noise is exactly
//              anti-correlated, but the meter's sums still carry the earlier
//              segments, so it reads -0.81 raw and -0.64 smoothed at 14 s.
//              L + R = L / 2, so the spectrum, which analyses a sum, still
//              shows the noise.
// The quiet segments come first on purpose: after loud material the float32
// meters cannot resolve a -65 LUFS signal (the K-weighting filter's rounding
// noise from the loud part rings into it, and the correlation sums swamp it).
inline constexpr int kProgrammeSeconds = 14;

struct StereoProgramme
{
    std::vector<float> left;
    std::vector<float> right;
};

// sampleRate must be a whole multiple of 100 Hz so 10 ms blocks tile it.
StereoProgramme makeProgramme(int sampleRate);

// 64-bit FNV-1a over both channels' IEEE-754 bit patterns, left then right.
std::uint64_t hashProgramme(const StereoProgramme& programme);
} // namespace spectrum_capture
