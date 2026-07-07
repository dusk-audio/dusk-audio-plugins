// Offline renderer for the Multi-Q Digital core (Phase 2b A/B).
// Renders deterministic white noise through MultiQDSP and writes a float32 WAV.
// Two modes: --curve eq (a feature-exercising EQ) or --curve flat (all bands
// disabled = passthrough reference). The Python side normalises each build by
// its own passthrough, so the resulting transfer function is input-independent
// and the core (white noise) can be compared to the JUCE VST3 (render tool's
// pink noise) directly.
//
// Build: g++ -std=c++17 -O2 -I../../shared test_multiq_digital_ab.cpp \
//        ../core/MultiQDSP.cpp -o /tmp/mqab

#include "../core/MultiQDSP.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <random>

static void writeWavF32(const char* path, const std::vector<float>& inter, int ch, int sr)
{
    FILE* f = fopen(path, "wb");
    uint32_t dataBytes = (uint32_t)(inter.size() * 4);
    fwrite("RIFF", 1, 4, f); uint32_t riff = 36 + dataBytes; fwrite(&riff, 4, 1, f);
    fwrite("WAVE", 1, 4, f); fwrite("fmt ", 1, 4, f);
    uint32_t fmtLen = 16; fwrite(&fmtLen, 4, 1, f);
    uint16_t fmt = 3; fwrite(&fmt, 2, 1, f);
    uint16_t chs = (uint16_t)ch; fwrite(&chs, 2, 1, f);
    uint32_t srate = (uint32_t)sr; fwrite(&srate, 4, 1, f);
    uint32_t byteRate = (uint32_t)(sr * ch * 4); fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = (uint16_t)(ch * 4); fwrite(&blockAlign, 2, 1, f);
    uint16_t bits = 32; fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(inter.data(), 4, inter.size(), f);
    fclose(f);
}

int main(int argc, char** argv)
{
    std::string outPath = "/tmp/mqab.wav";
    std::string curve = "eq";
    bool matchClap = false; // replicate claphost input exactly for sample-level A/B
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--curve" && i + 1 < argc) curve = argv[++i];
        else if (a == "--match-clap") matchClap = true;
    }
    const bool eq = (curve == "eq" || curve == "sat" || curve == "dyn");
    const bool satMode = (curve == "sat");
    const bool dynMode = (curve == "dyn");

    const int sr = 48000, ch = 2, block = 512;
    const int total = matchClap ? sr * 2 : sr * 4; // match claphost's 2 s when doing sample-level A/B

    duskaudio::MultiQDSP dsp;
    dsp.prepare(sr, block);

    duskaudio::MultiQDSP::Params p; // defaults: all bands enabled, gain 0, HPF@20/12dB, LPF@20k/12dB
    if (!eq)
    {
        // passthrough reference: disable every band
        for (int b = 0; b < 8; ++b) p.bandEnabled[(size_t)b] = false;
    }
    else
    {
        // feature-exercising curve (Digital, Stereo, no sat/dyn)
        p.eqType = 0; p.processingMode = 0; p.qCoupleMode = 0;
        // HPF band0: 80 Hz, 24 dB/oct (Slope24dB=3)
        p.bandEnabled[0] = true; p.bandFreq[0] = 80.f;  p.bandQ[0] = 0.71f; p.bandSlope[0] = 3;
        // band1 low shelf: 120 Hz +6 dB
        p.bandEnabled[1] = true; p.bandShape[1] = 0; p.bandFreq[1] = 120.f; p.bandGain[1] = 6.f;  p.bandQ[1] = 0.7f;
        // band2 peak: 400 Hz -5 dB Q2
        p.bandEnabled[2] = true; p.bandShape[2] = 0; p.bandFreq[2] = 400.f; p.bandGain[2] = -5.f; p.bandQ[2] = 2.f;
        // band3 peak: 800 Hz +4 dB Q1.5
        p.bandEnabled[3] = true; p.bandShape[3] = 0; p.bandFreq[3] = 800.f; p.bandGain[3] = 4.f;  p.bandQ[3] = 1.5f;
        // band4 peak: 2500 Hz -8 dB Q3
        p.bandEnabled[4] = true; p.bandShape[4] = 0; p.bandFreq[4] = 2500.f; p.bandGain[4] = -8.f; p.bandQ[4] = 3.f;
        // band5 peak: 5000 Hz +5 dB Q1
        p.bandEnabled[5] = true; p.bandShape[5] = 0; p.bandFreq[5] = 5000.f; p.bandGain[5] = 5.f;  p.bandQ[5] = 1.f;
        // band6 high shelf: 8000 Hz +7 dB
        p.bandEnabled[6] = true; p.bandShape[6] = 0; p.bandFreq[6] = 8000.f; p.bandGain[6] = 7.f;  p.bandQ[6] = 0.7f;
        // LPF band7: 16000 Hz 12 dB/oct (Slope12dB=1)
        p.bandEnabled[7] = true; p.bandFreq[7] = 16000.f; p.bandQ[7] = 0.71f; p.bandSlope[7] = 1;
        // saturation A/B: band 4 (index 3) Tape @ drive 0.6
        if (satMode) { p.bandSatType[3] = 1; p.bandSatDrive[3] = 0.6f; }
        // dynamics A/B: band 4 (index 3) dynamic EQ, low threshold so noise triggers it
        if (dynMode) { p.bandDynEnabled[3] = true; p.bandDynThreshold[3] = -30.f; p.bandDynRatio[3] = 4.f;
                       p.bandDynAttack[3] = 10.f; p.bandDynRelease[3] = 100.f; p.bandDynRange[3] = 12.f; }
    }

    // claphost uses mt19937(12345) with ONE uniform(-0.25,0.25) draw per sample
    // written to both channels; replicate exactly under --match-clap so the two
    // renders can be diffed sample-for-sample.
    std::mt19937 rng(matchClap ? 12345u : 777u);
    std::uniform_real_distribution<float> nd(-0.25f, 0.25f);

    std::vector<float> L(block), R(block);
    std::vector<float> inter; inter.reserve((size_t)total * ch);
    const float* inp[2] = { L.data(), R.data() };
    float* outp[2] = { L.data(), R.data() };

    for (int pos = 0; pos < total; pos += block)
    {
        int n = std::min(block, total - pos);
        for (int i = 0; i < n; ++i)
        {
            if (matchClap) { float s = nd(rng); L[(size_t)i] = s; R[(size_t)i] = s; }
            else           { L[(size_t)i] = nd(rng); R[(size_t)i] = nd(rng); }
        }
        dsp.process(inp, outp, ch, n, p);
        for (int i = 0; i < n; ++i) { inter.push_back(L[(size_t)i]); inter.push_back(R[(size_t)i]); }
    }

    writeWavF32(outPath.c_str(), inter, ch, sr);
    fprintf(stderr, "wrote %s (%s, %d frames)\n", outPath.c_str(), eq ? "eq" : "flat", total);
    return 0;
}
