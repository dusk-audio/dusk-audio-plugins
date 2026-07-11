// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// acid_test — standalone offline renderer for the Acid engine validation gates.
//
// The Acid engine (AcidEngine.hpp) is framework-free and self-contained, so this
// harness drives its classes DIRECTLY (it does not go through MultiSynthDSP —
// that wiring is Phase 3). Three subcommands, each writing a mono float32 WAV:
//
//   acid_test filter <out.wav> [key=value ...]
//       Feeds a source through AcidFilter only (no oscillator envelope / amp).
//       keys: sr seconds src(0=noise,1=saw) oscHz cutoff res drive
//             sweep(0/1) cutlo cuthi
//
//   acid_test voice  <out.wav> [key=value ...]
//       Plays scripted note events on one AcidVoice.
//       keys: sr seconds wave cutoff res drive envMod decay sustain accentAmt
//             slideTime gain off(=noteOff time, s)
//             events="t:midi:accent:slide;t:midi:accent:slide;..."
//               (all four fields required per event; t finite in [0,seconds]
//                and nondecreasing; midi 0..127; accent/slide 0 or 1)
//
//   acid_test seq    <out.wav> [key=value ...]
//       Runs AcidSequencer -> AcidVoice.
//       keys: sr seconds bpm rate(division idx) gate swing latch root
//             wave cutoff res drive envMod decay sustain accentAmt slideTime
//             on="1111000011110000"  pitches="0,12,..."
//             accents="0,1,..."      slides="0,0,..."
//
// key=value ordering is free; unknown keys abort with an error.

#include "AcidEngine.hpp"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
void writeFloatWavMono(const char* path, const std::vector<float>& mono, int sampleRate)
{
    // Mono float RIFF limit: 36 + numFrames*4 must fit in uint32 -> (UINT32_MAX-36)/4.
    if (mono.size() > 1073741814u)
    {
        std::fprintf(stderr, "render exceeds RIFF WAV frame limit\n");
        std::exit(2);
    }
    const uint32_t numFrames = (uint32_t)mono.size();
    const uint16_t channels = 1;
    const uint16_t bits = 32;
    const uint32_t byteRate = (uint32_t)sampleRate * channels * (bits / 8);
    const uint16_t blockAlign = channels * (bits / 8);
    const uint32_t dataBytes = numFrames * blockAlign;

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(3 /* IEEE float */); u16(channels);
    u32((uint32_t)sampleRate); u32(byteRate); u16(blockAlign); u16(bits);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    std::fwrite(mono.data(), sizeof(float), mono.size(), f);
    std::fclose(f);
}

// Strict string->double: reject empty, trailing garbage, or non-finite results.
double parseNum(const char* key, const std::string& v)
{
    if (v.empty())
    {
        std::fprintf(stderr, "empty value for key '%s'\n", key);
        std::exit(2);
    }
    const char* start = v.c_str();
    char* end = nullptr;
    const double d = std::strtod(start, &end);
    if (end == start || *end != '\0' || !std::isfinite(d))
    {
        std::fprintf(stderr, "invalid numeric value for key '%s': %s\n", key, v.c_str());
        std::exit(2);
    }
    return d;
}

// Strict string->long: reject empty, trailing garbage, or out-of-range.
long parseInt(const char* key, const std::string& v)
{
    if (v.empty())
    {
        std::fprintf(stderr, "empty integer value for key '%s'\n", key);
        std::exit(2);
    }
    const char* start = v.c_str();
    char* end = nullptr;
    errno = 0;
    const long n = std::strtol(start, &end, 10);
    if (end == start || *end != '\0' || errno == ERANGE)
    {
        std::fprintf(stderr, "invalid integer value for key '%s': %s\n", key, v.c_str());
        std::exit(2);
    }
    return n;
}

// Range-check render extents before any DSP prepare/allocation.
void validateExtents(double sr, double seconds)
{
    if (!(sr >= 8000.0 && sr <= 768000.0))
    {
        std::fprintf(stderr, "invalid sample rate: %g (want 8000..768000)\n", sr);
        std::exit(2);
    }
    // The WAV header stores the rate as (int); the DSP runs at the double rate.
    // Require a whole number so the two agree exactly.
    if (sr != std::floor(sr))
    {
        std::fprintf(stderr, "invalid sample rate: %g (must be a whole number)\n", sr);
        std::exit(2);
    }
    if (!(std::isfinite(seconds) && seconds >= 0.0 && seconds <= 3600.0))
    {
        std::fprintf(stderr, "invalid seconds: %g (want 0 <= seconds <= 3600)\n", seconds);
        std::exit(2);
    }
    // Mono float RIFF limit: (UINT32_MAX-36)/4 frames.
    if (seconds * sr > 1073741814.0)
    {
        std::fprintf(stderr, "render too long: %g frames exceeds RIFF WAV limit\n", seconds * sr);
        std::exit(2);
    }
}

// Minimal key=value map over argv[3..].
struct Args
{
    std::vector<std::pair<std::string, std::string>> kv;

    void parse(int argc, char** argv, int start)
    {
        for (int a = start; a < argc; ++a)
        {
            std::string s = argv[a];
            const auto eq = s.find('=');
            if (eq == std::string::npos)
            {
                std::fprintf(stderr, "malformed argument (expected key=value): %s\n", s.c_str());
                std::exit(2);
            }
            kv.emplace_back(s.substr(0, eq), s.substr(eq + 1));
        }
    }
    bool has(const char* k) const
    {
        for (auto& p : kv) if (p.first == k) return true;
        return false;
    }
    std::string str(const char* k, const char* def) const
    {
        for (auto& p : kv) if (p.first == k) return p.second;
        return def;
    }
    double num(const char* k, double def) const
    {
        for (auto& p : kv) if (p.first == k) return parseNum(k, p.second);
        return def;
    }
    int intv(const char* k, int def) const { return (int)num(k, (double)def); }

    // Reject any key not in the subcommand's allowed set (the header promises
    // "unknown keys abort with an error").
    void validate(const std::vector<std::string>& allowed) const
    {
        for (auto& p : kv)
        {
            bool ok = false;
            for (auto& a : allowed) if (p.first == a) { ok = true; break; }
            if (!ok)
            {
                std::fprintf(stderr, "unknown key: %s\n", p.first.c_str());
                std::exit(2);
            }
        }
    }
};

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size())
    {
        size_t c = s.find(sep, pos);
        if (c == std::string::npos) { out.push_back(s.substr(pos)); break; }
        out.push_back(s.substr(pos, c - pos));
        pos = c + 1;
    }
    return out;
}

std::vector<int> parseIntList(const std::string& s)
{
    std::vector<int> out;
    if (s.empty()) return out;
    for (auto& t : split(s, ',')) if (!t.empty()) out.push_back(std::atoi(t.c_str()));
    return out;
}

msynth::Waveform waveFromInt(int w)
{
    switch (w)
    {
        case 1: return msynth::Waveform::Square;
        case 2: return msynth::Waveform::Triangle;
        case 3: return msynth::Waveform::Sine;
        case 4: return msynth::Waveform::Pulse;
        case 5: return msynth::Waveform::Noise;
        default: return msynth::Waveform::Saw;
    }
}

// -----------------------------------------------------------------------------
int runFilter(const Args& g, const char* out)
{
    const double sr = g.num("sr", 48000.0);
    const double seconds = g.num("seconds", 1.0);
    validateExtents(sr, seconds);
    const int    src = g.intv("src", 0);        // 0 noise, 1 saw
    const float  oscHz = (float)g.num("oscHz", 55.0);
    const float  cutoff = (float)g.num("cutoff", 1000.0);
    const float  res = (float)g.num("res", 0.0);
    const float  drive = (float)g.num("drive", 1.0);
    const bool   sweep = g.intv("sweep", 0) != 0;
    const float  cutlo = (float)g.num("cutlo", 200.0);
    const float  cuthi = (float)g.num("cuthi", 2000.0);

    msynth::AcidFilter filter;
    filter.prepare(sr);

    msynth::Oscillator osc;
    osc.prepare(sr);
    osc.seedNoise(0x1357u);
    osc.setWaveform(msynth::Waveform::Saw);
    osc.setFrequency(oscHz);

    msynth::Xorshift noise;
    noise.seed(0x2468u);

    const int N = (int)(seconds * sr);
    std::vector<float> mono((size_t)N, 0.0f);

    for (int i = 0; i < N; ++i)
    {
        float cut = cutoff;
        if (sweep)
        {
            const float t = (float)i / (float)(N > 1 ? N - 1 : 1);
            cut = cutlo * std::pow(cuthi / cutlo, t); // log sweep 200 -> 2000
        }
        filter.setParameters(cut, res, drive);

        float in = (src == 1) ? osc.processSample() : (noise.nextBipolar() * 0.1f);
        mono[(size_t)i] = filter.process(in);
    }

    writeFloatWavMono(out, mono, (int)sr);
    return 0;
}

// -----------------------------------------------------------------------------
struct NoteEvent { double t; int midi; bool accent; bool slide; };

int runVoice(const Args& g, const char* out)
{
    const double sr = g.num("sr", 48000.0);
    const double seconds = g.num("seconds", 2.0);
    validateExtents(sr, seconds);

    msynth::AcidVoice voice;
    voice.prepare(sr);
    voice.setWaveform(waveFromInt(g.intv("wave", 0)));
    voice.setCutoff((float)g.num("cutoff", 800.0));
    voice.setResonance((float)g.num("res", 0.5));
    voice.setDrive((float)g.num("drive", 1.0));
    voice.setEnvMod((float)g.num("envMod", 0.5));
    voice.setDecay((float)g.num("decay", 0.3));
    voice.setSustain((float)g.num("sustain", 0.0));
    voice.setAccentAmount((float)g.num("accentAmt", 0.7));
    voice.setSlideTime((float)g.num("slideTime", 60.0));
    voice.setGain((float)g.num("gain", 0.7));

    std::vector<NoteEvent> events;
    const std::string evstr = g.str("events", "");
    if (!evstr.empty())
    {
        double prevT = 0.0;
        bool havePrev = false;
        for (auto& e : split(evstr, ';'))
        {
            if (e.empty()) continue;
            auto f = split(e, ':');
            if (f.size() != 4)
            {
                std::fprintf(stderr, "invalid event '%s': expected exactly t:midi:accent:slide\n",
                             e.c_str());
                std::exit(2);
            }
            NoteEvent ne{};
            ne.t = parseNum("events.t", f[0]);
            const long midi   = parseInt("events.midi", f[1]);
            const long accent = parseInt("events.accent", f[2]);
            const long slide  = parseInt("events.slide", f[3]);
            if (!(std::isfinite(ne.t) && ne.t >= 0.0 && ne.t <= seconds))
            {
                std::fprintf(stderr, "invalid event time %g (want 0 <= t <= %g)\n", ne.t, seconds);
                std::exit(2);
            }
            if (havePrev && ne.t < prevT)
            {
                std::fprintf(stderr, "event times must be nondecreasing: %g after %g\n", ne.t, prevT);
                std::exit(2);
            }
            if (midi < 0 || midi > 127)
            {
                std::fprintf(stderr, "invalid event midi %ld (want 0..127)\n", midi);
                std::exit(2);
            }
            if (accent < 0 || accent > 1 || slide < 0 || slide > 1)
            {
                std::fprintf(stderr, "invalid event accent/slide %ld/%ld (want 0 or 1)\n",
                             accent, slide);
                std::exit(2);
            }
            ne.midi   = (int)midi;
            ne.accent = (accent != 0);
            ne.slide  = (slide != 0);
            events.push_back(ne);
            prevT = ne.t;
            havePrev = true;
        }
    }
    const double offT = g.num("off", -1.0);

    const int N = (int)(seconds * sr);
    std::vector<float> mono((size_t)N, 0.0f);

    size_t nextEv = 0;
    bool released = false;
    for (int i = 0; i < N; ++i)
    {
        const double t = (double)i / sr;
        while (nextEv < events.size() && t >= events[nextEv].t)
        {
            const auto& e = events[nextEv++];
            voice.noteOn(msynth::midiToHz((float)e.midi), e.accent, e.slide, 1.0f);
        }
        if (!released && offT >= 0.0 && t >= offT) { voice.noteOff(); released = true; }
        mono[(size_t)i] = voice.processSample();
    }

    writeFloatWavMono(out, mono, (int)sr);
    return 0;
}

// -----------------------------------------------------------------------------
int runSeq(const Args& g, const char* out)
{
    const double sr = g.num("sr", 48000.0);
    const double seconds = g.num("seconds", 4.0);
    validateExtents(sr, seconds);
    const double bpm = g.num("bpm", 120.0);
    const bool   playing = g.intv("playing", 1) != 0;

    msynth::AcidVoice voice;
    voice.prepare(sr);
    voice.setWaveform(waveFromInt(g.intv("wave", 0)));
    voice.setCutoff((float)g.num("cutoff", 800.0));
    voice.setResonance((float)g.num("res", 0.5));
    voice.setDrive((float)g.num("drive", 1.0));
    voice.setEnvMod((float)g.num("envMod", 0.5));
    voice.setDecay((float)g.num("decay", 0.2));
    voice.setSustain((float)g.num("sustain", 0.0));
    voice.setAccentAmount((float)g.num("accentAmt", 0.7));
    voice.setSlideTime((float)g.num("slideTime", 60.0));
    voice.setGain((float)g.num("gain", 0.7));

    msynth::AcidSequencer seq;
    seq.prepare(sr);
    seq.setEnabled(true);
    seq.setRate((msynth::ArpRateDivision)g.intv("rate", (int)msynth::ArpRateDivision::Sixteenth));
    seq.setGate((float)g.num("gate", 0.5));
    seq.setSwing((float)g.num("swing", 0.0));
    seq.setLatch(g.intv("latch", 0) != 0);

    const std::string onStr = g.str("on", "1111111111111111");
    const std::vector<int> pitches = parseIntList(g.str("pitches", ""));
    const std::vector<int> accents = parseIntList(g.str("accents", ""));
    const std::vector<int> slides  = parseIntList(g.str("slides", ""));
    for (int i = 0; i < 16; ++i)
    {
        const bool on = (i < (int)onStr.size()) ? (onStr[(size_t)i] == '1') : true;
        const int  p  = (i < (int)pitches.size()) ? pitches[(size_t)i] : 0;
        const bool ac = (i < (int)accents.size()) ? (accents[(size_t)i] != 0) : false;
        const bool sl = (i < (int)slides.size())  ? (slides[(size_t)i] != 0)  : false;
        seq.setStep(i, on, p, ac, sl);
    }

    seq.noteOn(g.intv("root", 36)); // default C2

    // Host phase-lock: when songpos is given the harness acts as the DAW host,
    // feeding an advancing song-beat cursor and hostLocked=true. loopbeats>0
    // wraps the cursor (simulates a transport loop) to exercise wrap re-sync.
    // Without songpos the sequencer free-runs (every existing gate preserved).
    const bool   locked    = g.has("songpos");
    const double startBeat = g.num("songpos", 0.0);
    const double loopBeats  = g.num("loopbeats", 0.0);
    const double beatsPerFrame = bpm / 60.0 / sr;

    const int N = (int)(seconds * sr);
    std::vector<float> mono((size_t)N, 0.0f);

    for (int i = 0; i < N; ++i)
    {
        double songBeat = 0.0;
        if (locked)
        {
            songBeat = startBeat + (double)i * beatsPerFrame;
            if (loopBeats > 0.0) songBeat = std::fmod(songBeat, loopBeats);
        }
        const auto ev = seq.advanceSample(bpm, playing, songBeat, locked);
        if (ev.noteOff) voice.noteOff();
        if (ev.noteOn)  voice.noteOn(ev.freq, ev.accent, ev.slide, 1.0f);
        mono[(size_t)i] = voice.processSample();
    }

    writeFloatWavMono(out, mono, (int)sr);
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: acid_test <filter|voice|seq> <out.wav> [key=value ...]\n");
        return 1;
    }
    const std::string cmd = argv[1];
    const char* out = argv[2];

    Args g;
    g.parse(argc, argv, 3);

    if (cmd == "filter")
    {
        g.validate({"sr","seconds","src","oscHz","cutoff","res","drive","sweep","cutlo","cuthi"});
        return runFilter(g, out);
    }
    if (cmd == "voice")
    {
        g.validate({"sr","seconds","wave","cutoff","res","drive","envMod","decay","sustain",
                    "accentAmt","slideTime","gain","off","events"});
        return runVoice(g, out);
    }
    if (cmd == "seq")
    {
        g.validate({"sr","seconds","bpm","playing","rate","gate","swing","latch","root","wave",
                    "cutoff","res","drive","envMod","decay","sustain","accentAmt","slideTime",
                    "gain","on","pitches","accents","slides","songpos","loopbeats"});
        return runSeq(g, out);
    }

    std::fprintf(stderr, "unknown subcommand: %s\n", cmd.c_str());
    return 1;
}
