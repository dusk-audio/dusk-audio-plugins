// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: band dynamics. A band turns down by the same amount, and renders the
// same samples, whatever the host block size; one gain from the louder channel,
// whatever the phase between the channels; detection follows the band's routing
// (Left, Right, Mid, Side); RMS detection measures the band's RMS level over a
// 10 ms window, also when switched to while running with one or both channels
// loud; the gain follows the envelope on a 0.5 ms grid; a huge or NaN input
// sample does not stop the dynamics; and re-activation starts clean, also for
// a band whose dynamics are off at the time.
//
// Built twice: against the JUCE Multi-Q processor (MULTIQ_TEST_JUCE, through
// its APVTS parameters) and against the Multi-Q 2 plugin (through DAF's
// PluginExporter and its host parameters), so both products are held to the
// same numbers. Every check measures what a band does to a 1 kHz tone.
#if defined(MULTIQ_TEST_JUCE)
#include "../MultiQ.h"
#else
#include "../daf-plugin/MultiQPlugin.cpp"
#include "src/DafPluginInternal.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* what, double measured, double expected)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s (measured %.4f, expected %.4f)\n", what, measured, expected);
    }
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kToneHz = 1000.0;           // band 5's default frequency
constexpr int kBand = 5;                     // a parametric band, 1-based as in the UI
constexpr int kMaxBlock = 2048;              // every instance is prepared for this

// Routing choice indices, identical in both builds' parameter layouts.
enum Routing { Global = 0, Stereo = 1, Left = 2, Right = 3, Mid = 4, Side = 5 };

struct Settings
{
    float threshold = -30.0f, ratio = 4.0f, range = 12.0f;
    float attack = 10.0f, release = 100.0f;  // ms: the parameter defaults
    int routing = Global;
    bool rms = false;
    bool dynamics = true;
};

#if defined(MULTIQ_TEST_JUCE)
class Plugin
{
public:
    Plugin(double sr, const Settings& s) : rate(sr)
    {
        set(ParamIDs::bandDynEnabled(kBand), s.dynamics ? 1.0f : 0.0f);
        set(ParamIDs::bandDynThreshold(kBand), s.threshold);
        set(ParamIDs::bandDynRatio(kBand), s.ratio);
        set(ParamIDs::bandDynRange(kBand), s.range);
        set(ParamIDs::bandDynAttack(kBand), s.attack);
        set(ParamIDs::bandDynRelease(kBand), s.release);
        set(ParamIDs::bandChannelRouting(kBand), (float)s.routing);
        set(ParamIDs::dynDetectionMode, s.rms ? 1.0f : 0.0f);
        mq->prepareToPlay(sr, kMaxBlock);
    }
    void process(float* left, float* right, int frames)
    {
        float* channels[] = {left, right};
        juce::AudioBuffer<float> buffer(channels, 2, frames);
        juce::MidiBuffer midi;
        mq->processBlock(buffer, midi);
    }
    int latency() const { return mq->getLatencySamples(); }
    void reactivate() { mq->releaseResources(); mq->prepareToPlay(rate, kMaxBlock); }
    void setOversampling(int mode) { set(ParamIDs::hqEnabled, (float)mode); }
    void setDynamics(bool on) { set(ParamIDs::bandDynEnabled(kBand), on ? 1.0f : 0.0f); }
    void setRms(bool on) { set(ParamIDs::dynDetectionMode, on ? 1.0f : 0.0f); }

private:
    double rate;
    std::unique_ptr<MultiQ> mq = std::make_unique<MultiQ>();
    void set(const juce::String& id, float value)
    {
        auto* parameter = mq->parameters.getParameter(id);
        if (parameter == nullptr) { check(false, "requested JUCE parameter exists", 0, 1); return; }
        parameter->setValueNotifyingHost(mq->parameters.getParameterRange(id).convertTo0to1(value));
    }
};
#else
class Plugin
{
public:
    Plugin(double sr, const Settings& s)
    {
        // As a DAF wrapper does before it creates the plugin.
        DAF_NAMESPACE::d_nextBufferSize = (uint32_t)kMaxBlock;
        DAF_NAMESPACE::d_nextSampleRate = sr;
        plugin = std::make_unique<DAF_NAMESPACE::PluginExporter>(nullptr, nullptr, nullptr, nullptr);
        const int b = kBand - 1;
        plugin->setParameterValue(mqidx::dynEnabled(b), s.dynamics ? 1.0f : 0.0f);
        plugin->setParameterValue(mqidx::dynThreshold(b), s.threshold);
        plugin->setParameterValue(mqidx::dynRatio(b), s.ratio);
        plugin->setParameterValue(mqidx::dynRange(b), s.range);
        plugin->setParameterValue(mqidx::dynAttack(b), s.attack);
        plugin->setParameterValue(mqidx::dynRelease(b), s.release);
        plugin->setParameterValue(mqidx::routing(b), (float)s.routing);
        plugin->setParameterValue(kParamDynDetectionMode, s.rms ? 1.0f : 0.0f);
        plugin->setSampleRate(sr, true);
        plugin->setBufferSize((uint32_t)kMaxBlock, true);
        plugin->activate();
    }
    void process(float* left, float* right, int frames)
    {
        const float* in[] = {left, right};
        float* out[] = {left, right};
        plugin->run(in, out, (uint32_t)frames);
    }
    int latency() const { return (int)plugin->getLatency(); }
    void reactivate() { plugin->deactivate(); plugin->activate(); }
    void setOversampling(int mode) { plugin->setParameterValue(kParamHqEnabled, (float)mode); }
    void setDynamics(bool on) { plugin->setParameterValue(mqidx::dynEnabled(kBand - 1), on ? 1.0f : 0.0f); }
    void setRms(bool on) { plugin->setParameterValue(kParamDynDetectionMode, on ? 1.0f : 0.0f); }

private:
    std::unique_ptr<DAF_NAMESPACE::PluginExporter> plugin;
};
#endif

struct Signal { std::vector<float> l, r; };

// `lead` s of silence, `seconds` of a 1 kHz tone at the given peak levels (the
// right channel shifted by `phaseR`), then `tail` s of silence.
Signal tone(double sr, double lead, double seconds, double tail, double peakDbL, double peakDbR,
            double phaseR = 0.0)
{
    const int a = (int)(lead * sr), n = (int)(seconds * sr), total = a + n + (int)(tail * sr);
    Signal s{std::vector<float>((size_t)total, 0.0f), std::vector<float>((size_t)total, 0.0f)};
    const double gl = std::pow(10.0, peakDbL / 20.0), gr = std::pow(10.0, peakDbR / 20.0);
    for (int i = 0; i < n; ++i)
    {
        const double w = 2.0 * kPi * kToneHz * i / sr;
        s.l[(size_t)(a + i)] = (float)(gl * std::sin(w));
        s.r[(size_t)(a + i)] = (float)(gr * std::sin(w + phaseR));
    }
    return s;
}

// Runs `in` through the plugin in blocks taken in turn from `blocks`.
Signal run(Plugin& plugin, Signal in, const std::vector<int>& blocks)
{
    const int total = (int)in.l.size();
    size_t k = 0;
    for (int pos = 0; pos < total; k = (k + 1) % blocks.size())
    {
        const int n = std::min(blocks[k], total - pos);
        plugin.process(in.l.data() + pos, in.r.data() + pos, n);
        pos += n;
    }
    return in;
}

double rms(const std::vector<float>& v, int lo, int hi)
{
    double sum = 0.0;
    for (int i = lo; i < hi; ++i) sum += (double)v[(size_t)i] * v[(size_t)i];
    return std::sqrt(sum / std::max(1, hi - lo));
}

// Gain (dB) the plugin applied to input samples [lo, hi), output aligned by its latency.
double gainDb(const std::vector<float>& out, const std::vector<float>& in, int lo, int hi, int latency)
{
    return 20.0 * std::log10(rms(out, lo + latency, hi + latency) / rms(in, lo, hi));
}

struct Gain { double left, right; };

// Steady state: the last 100 ms of a 0.8 s tone.
Gain steady(double sr, const Settings& s, double peakDbL, double peakDbR, double phaseR = 0.0)
{
    Plugin plugin(sr, s);
    const Signal in = tone(sr, 0.1, 0.8, 0.2, peakDbL, peakDbR, phaseR);
    const Signal out = run(plugin, in, {512});
    const int lo = (int)(0.8 * sr), hi = (int)(0.9 * sr), lat = plugin.latency();
    return {gainDb(out.l, in.l, lo, hi, lat), gainDb(out.r, in.r, lo, hi, lat)};
}

const char* modeName(bool rms) { return rms ? "RMS " : "Peak"; }

void blockSizeIndependence(double sr, bool rmsMode)
{
    // A 300 ms burst, attack and release included, rendered at host blocks 32,
    // 128, 512, 2048 and an irregular split: every render must be the same
    // samples, bit for bit, on both channels.
    Settings s;
    s.rms = rmsMode;
    const Signal in = tone(sr, 0.1, 0.3, 0.3, -15.0, -15.0);
    const std::vector<std::vector<int>> splits = {
        {512}, {32}, {128}, {2048}, {1, 7, 64, 333, 2048, 5, 100, 1000, 17, 512, 3, 1500}};
    Signal reference;
    int identical = 0;
    for (size_t k = 0; k < splits.size(); ++k)
    {
        Plugin plugin(sr, s);
        const Signal out = run(plugin, in, splits[k]);
        if (k == 0)
        {
            reference = out;
            const int lo = (int)(0.1 * sr), hi = (int)(0.4 * sr);
            const double g = gainDb(out.l, in.l, lo, hi, plugin.latency());
            check(g < -3.0, "the burst is turned down", g, -3.0);
            continue;
        }
        const bool same = std::memcmp(out.l.data(), reference.l.data(), out.l.size() * sizeof(float)) == 0
                       && std::memcmp(out.r.data(), reference.r.data(), out.r.size() * sizeof(float)) == 0;
        identical += same ? 1 : 0;
        check(same, "a render at another host block split is the same samples, bit for bit", same, 1.0);
    }
    std::printf("  sr=%.0f %s: renders at blocks 32/128/2048/irregular identical to block 512: %d of 4\n",
                sr, modeName(rmsMode), identical);
}

void stereoLinking(double sr, bool rmsMode)
{
    // One gain for both channels, taken from the louder channel. The quiet
    // channel (-45 dBFS) is below the threshold on its own.
    Settings s;
    s.rms = rmsMode;
    const double ref = steady(sr, s, -15.0, -15.0).left;
    const Gain loudLeft = steady(sr, s, -15.0, -45.0);
    const Gain loudRight = steady(sr, s, -45.0, -15.0);
    std::printf("  sr=%.0f %s stereo: both %+.3f | loud L: L %+.3f R %+.3f | loud R: L %+.3f R %+.3f dB\n",
                sr, modeName(rmsMode), ref, loudLeft.left, loudLeft.right, loudRight.left, loudRight.right);
    check(ref < -3.0, "a loud band is turned down", ref, -3.0);
    check(std::abs(loudLeft.left - ref) <= 0.1, "a loud left channel is turned down as much as a loud stereo signal", loudLeft.left, ref);
    check(std::abs(loudLeft.right - loudLeft.left) <= 0.1, "the quiet right channel gets the same gain as the loud left one", loudLeft.right, loudLeft.left);
    check(std::abs(loudRight.right - ref) <= 0.1, "a loud right channel is turned down as much as a loud stereo signal", loudRight.right, ref);
    check(std::abs(loudRight.left - loudRight.right) <= 0.1, "the quiet left channel gets the same gain as the loud right one", loudRight.left, loudRight.right);
}

void phaseIndependence(double sr, bool rmsMode)
{
    // Equal levels, the right channel shifted by 90 degrees: the louder channel
    // is as loud as with both in phase, so the gain must be the same.
    Settings s;
    s.rms = rmsMode;
    const double inPhase = steady(sr, s, -15.0, -15.0).left;
    const Gain quadrature = steady(sr, s, -15.0, -15.0, kPi / 2.0);
    std::printf("  sr=%.0f %s phase: in phase %+.3f | 90 degrees L %+.3f R %+.3f dB\n",
                sr, modeName(rmsMode), inPhase, quadrature.left, quadrature.right);
    check(std::abs(quadrature.left - inPhase) <= 0.1, "a 90 degree phase offset between the channels leaves the gain unchanged", quadrature.left, inPhase);
    check(std::abs(quadrature.right - quadrature.left) <= 0.1, "both channels get one gain at a 90 degree offset", quadrature.right, quadrature.left);
}

void routing(double sr, bool rmsMode)
{
    Settings s;
    s.rms = rmsMode;
    const double ref = steady(sr, s, -15.0, -15.0).left;
    s.routing = Left;
    const Gain left = steady(sr, s, -15.0, -45.0);
    s.routing = Right;
    const Gain right = steady(sr, s, -45.0, -15.0);
    s.routing = Mid;
    const Gain mid = steady(sr, s, -15.0, -15.0);
    s.routing = Side;
    const Gain side = steady(sr, s, -15.0, -15.0, kPi);   // R = -L: a pure Side signal
    std::printf("  sr=%.0f %s routing: Left L %+.3f R %+.3f | Right L %+.3f R %+.3f | Mid %+.3f | Side %+.3f dB\n",
                sr, modeName(rmsMode), left.left, left.right, right.left, right.right, mid.left, side.left);
    check(std::abs(left.left - ref) <= 0.1, "a band routed Left turns the loud left channel down", left.left, ref);
    check(std::abs(left.right) <= 0.05, "a band routed Left leaves the right channel alone", left.right, 0.0);
    check(std::abs(right.right - ref) <= 0.1, "a band routed Right turns the loud right channel down", right.right, ref);
    check(std::abs(right.left) <= 0.05, "a band routed Right leaves the left channel alone", right.left, 0.0);
    check(std::abs(mid.left - ref) <= 0.1, "a band routed Mid turns a loud Mid signal down", mid.left, ref);
    check(std::abs(side.left - ref) <= 0.1, "a band routed Side turns a loud Side signal down", side.left, ref);
}

void rmsDetection(double sr)
{
    // A steady sine's RMS level is its peak level minus 3.01 dB. In RMS mode
    // the gain follows the static curve at that level: above the 6 dB soft
    // knee, reduction = (level - threshold) * (1 - 1 / ratio). The measured
    // error is under 0.02 dB at every rate.
    Settings s;
    const double peakGain = steady(sr, s, -15.0, -15.0).left;
    s.rms = true;
    const double rmsGain = steady(sr, s, -15.0, -15.0).left;
    const double rmsLevel = -15.0 - 10.0 * std::log10(2.0);
    const double expected = -(rmsLevel - s.threshold) * (1.0 - 1.0 / s.ratio);
    std::printf("  sr=%.0f detection: Peak %+.3f dB, RMS %+.3f dB (static curve at the RMS level %+.3f dB)\n",
                sr, peakGain, rmsGain, expected);
    check(std::abs(rmsGain - expected) <= 0.05, "RMS detection follows the static curve at the tone's RMS level", rmsGain, expected);
    // Peak detection follows the rectified signal in dB with the band's attack
    // and release, so on a steady sine it settles between the RMS level and the
    // crest (about 1 dB above the RMS level here): clearly more reduction.
    check(peakGain <= rmsGain - 0.3, "Peak detection turns the same tone down further than RMS (by 0.3 dB or more)",
          peakGain, rmsGain - 0.3);
}

// Time (ms) from a -6 dBFS burst's onset until the RMS-mode gain reaches 90 %
// of its final reduction. The RMS window sets it: the mean square of a burst
// rises as 1 - exp(-t / tau), and 90 % of the final reduction (in dB, ratio 4)
// needs 10 log10(1 - exp(-t / tau)) = -2.1 dB, t = 0.96 tau, plus about 4.4 ms
// for the detector, the 2 ms gain smoother, the 1 ms filter ramp, the 0.5 ms
// grid and the 1 ms measuring window. Measured: 14.0 ms with the 10 ms window
// at 44.1, 48 and 96 kHz; 10.0 ms with a 5 ms window, 8.5 ms with 3 ms, 36 ms
// with 50 ms. The 12 to 15 ms bound holds the window to about 8 to 11 ms.
// Attack 0.1 ms and range 24 dB keep the envelope and the range out of the
// way. `plugin` must be fresh or settled at silence.
double rmsRiseMs(Plugin& plugin, double sr)
{
    const Signal in = tone(sr, 0.1, 0.1, 0.05, -6.0, -6.0);
    const Signal out = run(plugin, in, {512});
    const int onset = (int)(0.1 * sr), lat = plugin.latency(), win = (int)std::lround(0.001 * sr);
    const double final = gainDb(out.l, in.l, onset + (int)(0.06 * sr), onset + (int)(0.09 * sr), lat);
    for (int t = 0; t < (int)(0.06 * sr); t += std::max(1, win / 4))
        if (gainDb(out.l, in.l, onset + t, onset + t + win, lat) <= 0.9 * final)
            return 1000.0 * (t + 0.5 * win) / sr;
    return 1000.0;
}

Settings rmsRiseSettings()
{
    Settings s;
    s.rms = true;
    s.attack = 0.1f;
    s.range = 24.0f;
    return s;
}

void rmsWindow(double sr)
{
    Plugin plugin(sr, rmsRiseSettings());
    const double t90 = rmsRiseMs(plugin, sr);
    std::printf("  sr=%.0f RMS window: 90 %% of the reduction after %.2f ms (10 ms window: 14.0)\n", sr, t90);
    check(t90 >= 12.0 && t90 <= 15.0, "RMS detection rises with a 10 ms window", t90, 14.0);
}

void oversamplingSwitch(double sr)
{
    // Turning Oversampling on while running must keep the RMS window at 10 ms
    // (the original Multi-Q runs its dynamics at the oversampled rate).
    Plugin plugin(sr, rmsRiseSettings());
    Signal silence = tone(sr, 0.2, 0.0, 0.0, -120.0, -120.0);
    run(plugin, silence, {512});
    plugin.setOversampling(1);
    silence = tone(sr, 0.5, 0.0, 0.0, -120.0, -120.0);
    run(plugin, silence, {512});
    const double t90 = rmsRiseMs(plugin, sr);
    std::printf("  sr=%.0f RMS window after switching to 2x: 90 %% after %.2f ms\n", sr, t90);
    check(t90 >= 12.0 && t90 <= 15.0, "the RMS window stays 10 ms after Oversampling changes", t90, 14.0);
}

// Samples from a -6 dBFS burst's onset at `onset` until the dynamics first
// change the output: the first sample that differs from the same render with
// the band's dynamics off. Peak mode, attack 0.1 ms, host block 2048.
int reaction(double sr, int onset)
{
    Settings on;
    on.attack = 0.1f;
    Settings off = on;
    off.dynamics = false;
    const int burst = (int)(0.03 * sr), total = onset + burst + (int)(0.02 * sr);
    Signal in{std::vector<float>((size_t)total, 0.0f), std::vector<float>((size_t)total, 0.0f)};
    for (int i = 0; i < burst; ++i)
        in.l[(size_t)(onset + i)] = in.r[(size_t)(onset + i)] =
            (float)(0.5 * std::sin(2.0 * kPi * kToneHz * i / sr));   // -6 dBFS
    Plugin withDynamics(sr, on), without(sr, off);
    const Signal a = run(withDynamics, in, {2048});
    const Signal b = run(without, in, {2048});
    for (size_t i = 0; i < a.l.size(); ++i)
        if (std::memcmp(&a.l[i], &b.l[i], sizeof(float)) != 0)
            return (int)i - onset;
    return -1;
}

void gridPeriod(double sr)
{
    // The dynamic-gain filter takes the envelope's gain every 0.5 ms, on a grid
    // that runs through the host blocks. A burst arriving anywhere within one
    // grid period waits for the next step, so over onsets 7 samples apart (all
    // positions within a period) the reaction spreads by one period less one
    // sample. A coarser grid, or block-rate updates, spread wider.
    const int period = (int)std::lround(0.0005 * sr);
    int lo = 1 << 30, hi = -1;
    for (int k = 0; k < 50; ++k)
    {
        const int r = reaction(sr, (int)(0.05 * sr) + 7 * k);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    std::printf("  sr=%.0f grid: reaction %d to %d samples after onset, spread %d (period %d)\n",
                sr, lo, hi, hi - lo, period);
    check(lo >= 0, "the dynamics react to every burst", lo, 0.0);
    check(hi - lo >= period - 2 && hi - lo <= period, "the gain steps on the 0.5 ms grid at host block 2048", hi - lo, period - 1);
}

void reactivation(double sr)
{
    // Turn a loud tone down, re-activate (a host does on every reset), then
    // play a quiet tone the dynamics leave alone: its first 2 ms must carry
    // no leftover reduction.
    Plugin plugin(sr, Settings{});
    const Signal loud = tone(sr, 0.0, 0.5, 0.0, -15.0, -15.0);
    run(plugin, loud, {512});
    plugin.reactivate();
    const Signal in = tone(sr, 0.0, 0.3, 0.0, -45.0, -45.0);
    const Signal out = run(plugin, in, {512});
    const int lat = plugin.latency(), ms = (int)std::lround(0.001 * sr);
    const double settled = gainDb(out.l, in.l, (int)(0.2 * sr), (int)(0.29 * sr), lat);
    const double first = gainDb(out.l, in.l, 0, ms, lat), second = gainDb(out.l, in.l, ms, 2 * ms, lat);
    std::printf("  sr=%.0f re-activation: first ms %+.3f, second ms %+.3f, settled %+.3f dB\n",
                sr, first, second, settled);
    check(std::abs(first - settled) <= 0.05 && std::abs(second - settled) <= 0.05,
          "the first 2 ms after re-activation carry no leftover reduction", std::min(first, second), settled);
}

void reactivationWithDynamicsOff(double sr)
{
    // As above, but the band's dynamics are switched off while it turns the
    // loud tone down, the plugin re-activates and runs with them off, and they
    // are switched on again for the quiet tone: its first 2 ms must not carry
    // the reduction from before the re-activation either.
    Plugin plugin(sr, Settings{});
    run(plugin, tone(sr, 0.0, 0.5, 0.0, -15.0, -15.0), {512});
    plugin.setDynamics(false);
    run(plugin, tone(sr, 0.0, 0.05, 0.0, -15.0, -15.0), {512});
    plugin.reactivate();
    run(plugin, tone(sr, 0.0, 0.1, 0.0, -45.0, -45.0), {512});
    plugin.setDynamics(true);
    const Signal in = tone(sr, 0.0, 0.3, 0.0, -45.0, -45.0);
    const Signal out = run(plugin, in, {512});
    const int lat = plugin.latency(), ms = (int)std::lround(0.001 * sr);
    const double settled = gainDb(out.l, in.l, (int)(0.2 * sr), (int)(0.29 * sr), lat);
    const double first = gainDb(out.l, in.l, 0, ms, lat), second = gainDb(out.l, in.l, ms, 2 * ms, lat);
    std::printf("  sr=%.0f dynamics switched on after a re-activation with them off: first ms %+.3f, "
                "second ms %+.3f, settled %+.3f dB\n", sr, first, second, settled);
    check(std::abs(first - settled) <= 0.05 && std::abs(second - settled) <= 0.05,
          "dynamics switched on after a re-activation start with no leftover reduction",
          std::min(first, second), settled);
}

void detectionSwitch(double sr)
{
    // Switch Detection Mode from Peak to RMS while a steady tone is being
    // turned down: about 1 s later the band must turn it down exactly as an
    // instance that ran RMS from the start (the RMS level is tracked all along,
    // and both channels' envelopes follow it).
    Plugin plugin(sr, Settings{});
    run(plugin, tone(sr, 0.0, 0.8, 0.0, -15.0, -15.0), {512});   // 800 whole cycles: the tone continues
    plugin.setRms(true);
    const Signal in = tone(sr, 0.0, 1.2, 0.0, -15.0, -15.0);
    const Signal out = run(plugin, in, {512});
    const int lat = plugin.latency();
    const double switched = gainDb(out.l, in.l, (int)(1.0 * sr), (int)(1.1 * sr), lat);
    Settings rms;
    rms.rms = true;
    const double fresh = steady(sr, rms, -15.0, -15.0).left;
    std::printf("  sr=%.0f Peak to RMS while running: %+.3f dB 1 s later, fresh RMS instance %+.3f dB\n",
                sr, switched, fresh);
    check(std::abs(switched - fresh) <= 0.05, "switching Peak to RMS while running settles on the RMS gain",
          switched, fresh);
}

void detectionSwitchOneChannel(double sr, bool loudRight)
{
    // The same switch while only one channel is loud and turned down. RMS
    // follows one envelope for both channels, which must start from the louder
    // channel's: the reduction may move toward the RMS gain (a little less
    // than Peak's on a steady tone) but must not drop out while the attack
    // builds it up again. Checked in 1 ms steps over the first 60 ms, against
    // the gain an instance that ran RMS from the start settles on.
    const double quiet = -120.0, loud = -15.0;
    const double dbL = loudRight ? quiet : loud, dbR = loudRight ? loud : quiet;
    Plugin plugin(sr, Settings{});
    run(plugin, tone(sr, 0.0, 0.8, 0.0, dbL, dbR), {512});   // 800 whole cycles: the tone continues
    plugin.setRms(true);
    const Signal in = tone(sr, 0.0, 0.3, 0.0, dbL, dbR);
    const Signal out = run(plugin, in, {512});
    const auto& x = loudRight ? in.r : in.l;
    const auto& y = loudRight ? out.r : out.l;
    const int lat = plugin.latency(), ms = (int)std::lround(0.001 * sr);
    double least = -1.0e9;   // the least reduction (highest gain) of any 1 ms window
    for (int t = 0; t + ms <= (int)(0.06 * sr); t += ms)
        least = std::max(least, gainDb(y, x, t, t + ms, lat));
    Settings rms;
    rms.rms = true;
    const Gain fresh = steady(sr, rms, dbL, dbR);
    const double settled = loudRight ? fresh.right : fresh.left;
    std::printf("  sr=%.0f Peak to RMS with only the %s channel loud: least reduction in the first 60 ms "
                "%+.3f dB, fresh RMS instance %+.3f dB\n", sr, loudRight ? "right" : "left ", least, settled);
    check(least <= settled + 0.05, "switching Peak to RMS keeps the louder channel's reduction", least, settled);
}

void badInput(double sr, bool rmsMode, float bad, int channel)
{
    // A steady loud tone; ten samples of `bad` replace the input on one
    // channel at 0.5 s. The output must stay finite, and 1 s later the band
    // must turn the tone down exactly as before (default 100 ms release).
    Settings s;
    s.rms = rmsMode;
    Plugin plugin(sr, s);
    Signal in = tone(sr, 0.0, 2.0, 0.0, -15.0, -15.0);
    const Signal clean = in;
    auto& hit = channel == 0 ? in.l : in.r;
    for (int i = 0; i < 10; ++i) hit[(size_t)(0.5 * sr) + (size_t)i] = bad;
    const Signal out = run(plugin, in, {512});
    bool finite = true;
    for (size_t i = 0; i < out.l.size(); ++i)
        finite &= std::isfinite(out.l[i]) && std::isfinite(out.r[i]);
    const int lat = plugin.latency();
    const double before = gainDb(out.l, clean.l, (int)(0.4 * sr), (int)(0.5 * sr) - lat, lat);
    const double after = gainDb(out.l, clean.l, (int)(1.5 * sr), (int)(1.6 * sr), lat);
    std::printf("  sr=%.0f %s %s burst on %s: gain before %+.3f, 1 s after %+.3f dB, output finite %d\n",
                sr, modeName(rmsMode), std::isnan(bad) ? "NaN  " : "1e21 ", channel == 0 ? "L" : "R",
                before, after, (int)finite);
    check(finite, "the output stays finite", finite, 1.0);
    check(std::abs(after - before) <= 0.1, "the dynamics recover within 1 s", after, before);
}
}

int main()
{
#if defined(MULTIQ_TEST_JUCE)
    juce::ScopedJuceInitialiser_GUI init;
    std::printf("Multi-Q (JUCE) band dynamics\n");
#else
    std::printf("Multi-Q 2 band dynamics\n");
#endif
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (bool rmsMode : {false, true})
        {
            blockSizeIndependence(sr, rmsMode);
            stereoLinking(sr, rmsMode);
            phaseIndependence(sr, rmsMode);
            routing(sr, rmsMode);
        }
        rmsDetection(sr);
        rmsWindow(sr);
        oversamplingSwitch(sr);
        gridPeriod(sr);
        reactivation(sr);
        reactivationWithDynamicsOff(sr);
        detectionSwitch(sr);
        for (bool loudRight : {true, false})
            detectionSwitchOneChannel(sr, loudRight);
        for (bool rmsMode : {false, true})
            for (float bad : {1.0e21f, std::numeric_limits<float>::quiet_NaN()})
                for (int channel : {0, 1})
                    badInput(sr, rmsMode, bad, channel);
    }
    std::printf("Multi-Q dynamics: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
