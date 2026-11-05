// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#include "NonLinBurstEngine.h"

#include <algorithm>
#include <cmath>

namespace duskverb
{

namespace
{

// Every constant below is the reference model's (proto.py), fitted to the reference with
// fit.py. Times are ms after the onset unless stated otherwise.
constexpr double kPi = 3.14159265358979323846;
constexpr double kOnsetMs = 13.5;
constexpr std::uint64_t kSeed = 0x4E4C32;          // "NL2"
constexpr std::uint64_t kChannelSeedStep = 7919;

constexpr std::array<double, 7> kRiseMs { 0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0 };
constexpr std::array<double, 7> kRiseDb { -12.949, 0.228, -7.779, -5.254, 0.016, -1.192, -0.713 };
constexpr std::array<double, 12> kKneeShapeMs { -40.0, -30.0, -20.0, -10.0, 0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0 };

// Knee shape (dB at each kKneeShapeMs), plateau slope (dB/s), fall (dB/s) and output gain (dB)
// as a * K^2 + b * K + c in the knee K (ms). Fitted on knees 100.5 to 283.5 ms; a knee outside
// that range keeps its own position but takes the shape of the nearest fitted end.
constexpr double kFitKneeLo = 100.5, kFitKneeHi = 283.5;
constexpr double kKneeShapePoly[12][3] = {
    { -9.0804e-05,   0.056643619,  -9.910446523 },
    { -0.000206749,  0.088118376,  -9.786953746 },
    { -0.000423413,  0.184906074, -24.322704861 },
    { -0.000488764,  0.235445535, -29.662537473 },
    { -0.000261818,  0.119815309, -21.905514713 },
    { -0.000195046,  0.150571128, -32.118900248 },
    { -0.000462406,  0.210662442, -36.27229478 },
    { -0.000584088,  0.272530231, -46.45988895 },
    { -6.4128e-05,   0.076166962, -34.485328297 },
    { -0.000450225,  0.235254764, -53.383600092 },
    { -0.000728069,  0.30532626,  -58.360324787 },
    {  0.00029592,  -0.04417095,  -38.933119455 },
};
constexpr double kPlateauPoly[3] = { -0.000150577, 0.419956785, -143.297241695 };
constexpr double kFallPoly[3]    = { -0.002549664, 0.66070403,   297.191970693 };
constexpr double kGainPoly[3]    = { -0.000180649, 0.077998441,  -27.391953199 };
constexpr double kFloorDb = -40.0;                  // the taps stop once the envelope falls to this

// Taps per second, linear between knots and constant after the last. Sparse early, densest
// late. A tap's gain is the envelope over sqrt(density / kDensRef).
constexpr std::array<double, 5> kDensMs { 0.0, 20.0, 60.0, 120.0, 250.0 };
constexpr std::array<double, 5> kDens   { 120.0, 120.0, 350.0, 800.0, 1200.0 };
constexpr double kDensRef = 1000.0;

// Probability that a tap reads the dark line: a bright start, dark by 80 ms.
constexpr std::array<double, 4> kDarkMs { 0.0, 15.0, 80.0, 200.0 };
constexpr std::array<double, 4> kDarkW  { 0.0, 0.0, 0.85, 0.9 };
constexpr double kDarkHz = 3000.0;

constexpr double kMultMs[2] = { 1.31, 2.27 };
constexpr float  kMultG = 0.4f;
constexpr double kMultStretchR = 0.137;             // the right channel's multiplier delays are longer

constexpr double kTailApMs[2][2]   = { { 3.17, 5.03 }, { 2.89, 5.41 } };
constexpr float  kTailApG          = 0.6f;
constexpr double kTailLineMs[2][4] = { { 7.31, 11.93, 17.11, 23.29 }, { 8.09, 12.71, 16.33, 21.89 } };
constexpr double kTailT60 = 0.22;
constexpr double kTailSendDb = -15.0;
constexpr double kTailDampHz = 9000.0;
constexpr float  kOutLpHz = 15000.0f;

template <size_t N>
double interp (double x, const std::array<double, N>& xs, const std::array<double, N>& ys) noexcept
{
    if (x <= xs[0]) return ys[0];
    for (size_t i = 1; i < N; ++i)
        if (x <= xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[N - 1];
}

double poly (const double (&c)[3], double x) noexcept { return (c[0] * x + c[1]) * x + c[2]; }

int samples (double ms, double sampleRate) noexcept
{
    return std::max (1, static_cast<int> (std::lround (ms * 1e-3 * sampleRate)));
}

// The engine's random stream; the reference model's SplitMix64 bit for bit.
struct SplitMix64
{
    std::uint64_t state;
    std::uint64_t next() noexcept
    {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() noexcept { return static_cast<double> (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// The envelope for one knee.
struct Envelope
{
    double knee, plateauDbS, fallDbS, gainDb;
    std::array<double, 12> shape;

    explicit Envelope (double kneeMs) noexcept : knee (kneeMs)
    {
        const double k = std::clamp (kneeMs, kFitKneeLo, kFitKneeHi);
        for (size_t j = 0; j < shape.size(); ++j)
            shape[j] = poly (kKneeShapePoly[j], k);
        plateauDbS = poly (kPlateauPoly, k);
        // Past the longest fitted knee, keep the plateau's total sag rather than its slope: a
        // longer plateau would otherwise sink tens of dB before its own knee.
        if (kneeMs > kFitKneeHi)
            plateauDbS *= (kFitKneeHi - kRiseMs.back()) / (kneeMs - kRiseMs.back());
        fallDbS    = poly (kFallPoly, k);
        gainDb     = poly (kGainPoly, k);
    }

    double endMs() const noexcept
    {
        const double last = shape.back() + plateauDbS * std::max (knee + kKneeShapeMs.back() - kRiseMs.back(), 0.0) * 1e-3;
        return knee + kKneeShapeMs.back() + std::max (last - kFloorDb, 0.0) / fallDbS * 1e3;
    }

    // dB re the plateau at t ms after the onset (t <= endMs()).
    double db (double t) const noexcept
    {
        const double base = t < kRiseMs.back() ? interp (t, kRiseMs, kRiseDb)
                                               : plateauDbS * (t - kRiseMs.back()) * 1e-3;
        const double tau = t - knee;
        double knee_ = 0.0;
        if (tau > kKneeShapeMs.back())
            knee_ = shape.back() - fallDbS * (tau - kKneeShapeMs.back()) * 1e-3;
        else if (tau >= kKneeShapeMs.front())
            knee_ = interp (tau, kKneeShapeMs, shape);
        return base + knee_;
    }
};

// t (ms) at which the cumulative tap count from the onset reaches `count`: the density is linear
// between its knots, so each segment's count is quadratic in t.
double invertDensity (double count) noexcept
{
    double total = 0.0;
    for (size_t i = 0; i + 1 < kDensMs.size(); ++i)
    {
        const double t0 = kDensMs[i], t1 = kDensMs[i + 1];
        const double r0 = kDens[i] * 1e-3, r1 = kDens[i + 1] * 1e-3;
        const double seg = 0.5 * (r0 + r1) * (t1 - t0);
        if (count <= total + seg)
        {
            const double c = count - total;
            const double k = (r1 - r0) / (t1 - t0);
            if (std::abs (k) < 1e-12)
                return t0 + c / r0;
            return t0 + (-r0 + std::sqrt (r0 * r0 + 2.0 * k * c)) / k;
        }
        total += seg;
    }
    return kDensMs.back() + (count - total) / (kDens.back() * 1e-3);
}

} // namespace

void NonLinBurstEngine::Tail::clear() noexcept
{
    in[0].clear();
    in[1].clear();
    for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
    pos.fill (0);
    lp.fill (0.0f);
}

float NonLinBurstEngine::Tail::process (float x) noexcept
{
    x = in[1].process (in[0].process (x));
    float o[4];
    for (size_t k = 0; k < 4; ++k)
        o[k] = line[k][static_cast<size_t> (pos[k])];
    const float s = 0.5f * (o[0] + o[1] + o[2] + o[3]);
    const float y = 0.5f * (o[0] - o[1] + o[2] - o[3]);
    for (size_t k = 0; k < 4; ++k)
    {
        lp[k] = (1.0f - dampC) * (o[k] - s) * gain[k] + dampC * lp[k];
        line[k][static_cast<size_t> (pos[k])] = lp[k] + x;
        if (++pos[k] == static_cast<int> (line[k].size())) pos[k] = 0;
    }
    return y;
}

void NonLinBurstEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_ = std::max (1, maxBlockSize);
    const double longest = kOnsetMs + kMaxPreDelayMs + Envelope (kMaxKneeMs).endMs();
    ringLen_ = static_cast<int> (std::ceil (longest * 1e-3 * sampleRate)) + maxBlock_ + 1;
    darkNorm_ = static_cast<float> (std::sqrt (sampleRate / (kPi * kDarkHz)));
    tailSend_ = static_cast<float> (std::pow (10.0, kTailSendDb / 20.0));
    acc_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    fadeAcc_.assign (static_cast<size_t> (maxBlock_), 0.0f);
    fadeLen_ = std::max (1, static_cast<int> (std::lround (kTapFadeMs * 1e-3 * sampleRate)));

    // The tap layout runs to the end of the longest burst: the envelope's end grows with the
    // knee, so every shorter knee's taps are a prefix of it.
    const double layoutEnd = Envelope (kMaxKneeMs).endMs();
    for (size_t ch = 0; ch < channels_.size(); ++ch)
    {
        Channel& c = channels_[ch];
        c.layout.clear();
        c.layout.reserve (static_cast<size_t> (kMaxTaps));
        SplitMix64 rng { kSeed + kChannelSeedStep * ch };
        for (int k = 0; k < kMaxTaps; ++k)
        {
            const double uPos = rng.uniform(), u1 = rng.uniform(), u2 = rng.uniform(), uDark = rng.uniform();
            const double t = k == 0 ? 0.0 : invertDensity (k + uPos);      // the first tap is the onset
            if (t > layoutEnd)
                break;
            LayoutTap tap;
            tap.t        = t;
            tap.densNorm = std::sqrt (interp (t, kDensMs, kDens) / kDensRef);
            tap.normal   = std::sqrt (-2.0 * std::log (1.0 - u1)) * std::cos (2.0 * kPi * u2);
            tap.dark     = uDark < interp (t, kDarkMs, kDarkW);
            c.layout.push_back (tap);
        }

        c.raw.assign (2 * static_cast<size_t> (ringLen_), 0.0f);
        c.dark.assign (2 * static_cast<size_t> (ringLen_), 0.0f);
        c.darkLp.setCutoff (static_cast<float> (kDarkHz), sampleRate);
        c.darkLp.reset();
        for (int i = 0; i < 2; ++i)
            c.mult[i].prepare (samples (kMultMs[i] * (1.0 + kMultStretchR * static_cast<double> (ch) * (i + 1)), sampleRate), kMultG);
        for (int i = 0; i < 2; ++i)
            c.tail.in[i].prepare (samples (kTailApMs[ch][i], sampleRate), kTailApG);
        for (size_t k = 0; k < 4; ++k)
        {
            const int d = samples (kTailLineMs[ch][k], sampleRate);
            c.tail.line[k].assign (static_cast<size_t> (d), 0.0f);
            c.tail.gain[k] = static_cast<float> (std::pow (10.0, -3.0 * d / sampleRate / kTailT60));
        }
        c.tail.dampC = static_cast<float> (std::exp (-2.0 * kPi * kTailDampHz / sampleRate));
        c.tail.clear();
        c.outLp.setCoeffs (duskaudio::Biquad::lowPass (sampleRate, kOutLpHz, 0.70710678f));
        c.outLp.reset();
        c.hfDamp.setCutoff (std::min (hfDampHz_, 0.45f * static_cast<float> (sampleRate)), sampleRate);
        c.hfDamp.reset();
    }
    writePos_ = 0;
    fadeRemaining_ = 0;
    fresh_ = true;
    gainsDirty_ = true;
    updateTaps();
    fresh_ = true;                      // the rings are empty: the first change has nothing to fade from
}

void NonLinBurstEngine::clear() noexcept
{
    for (Channel& c : channels_)
    {
        std::fill (c.raw.begin(), c.raw.end(), 0.0f);
        std::fill (c.dark.begin(), c.dark.end(), 0.0f);
        c.darkLp.reset();
        c.mult[0].clear();
        c.mult[1].clear();
        c.tail.clear();
        c.outLp.reset();
        c.hfDamp.reset();
    }
    writePos_ = 0;
    fadeRemaining_ = 0;
    fresh_ = true;
}

void NonLinBurstEngine::setKnee (float kneeMs) noexcept
{
    kneeMs = std::clamp (kneeMs, kMinKneeMs, kMaxKneeMs);
    // A gain rebuild costs about 40 us at the longest knee and a quarter millisecond of knee is
    // inaudible, so a moving knee rebuilds only in steps that size. Once the request repeats
    // (the knob or the automation has stopped) the exact value is applied, so the taps at rest
    // never depend on the path that led there.
    const bool atRest = kneeMs == requestedKneeMs_;
    requestedKneeMs_ = kneeMs;
    if (kneeMs != kneeMs_ && (atRest || std::abs (kneeMs - kneeMs_) >= 0.25f))
    {
        kneeMs_ = kneeMs;
        gainsDirty_ = true;
    }
}

float NonLinBurstEngine::kneeForDecaySeconds (float seconds) noexcept
{
    return std::clamp ((seconds * 1000.0f - 170.2f) / 1.0216f, kMinKneeMs, kMaxKneeMs);
}

float NonLinBurstEngine::hfDampHzForTreble (float trebleMultiply) noexcept
{
    if (! (trebleMultiply < 1.0f))
        return kHfDampOffHz;
    const float t = std::max (trebleMultiply, 0.1f);
    return 2000.0f * std::pow (10.0f, (t - 0.1f) / 0.9f);
}

void NonLinBurstEngine::setHfDamp (float hz) noexcept
{
    hz = std::clamp (hz, 200.0f, kHfDampOffHz);
    if (hz == hfDampHz_)
        return;
    hfDampHz_ = hz;
    for (Channel& c : channels_)
        c.hfDamp.setCutoff (std::min (hz, 0.45f * static_cast<float> (sampleRate_)), sampleRate_);
}

void NonLinBurstEngine::setPreDelay (float ms) noexcept
{
    ms = std::clamp (ms, 0.0f, kMaxPreDelayMs);
    if (ms != preDelayMs_)
    {
        preDelayMs_ = ms;
        delaysDirty_ = true;
    }
}

void NonLinBurstEngine::updateTaps() noexcept
{
    if (! (gainsDirty_ || delaysDirty_) || fadeRemaining_ > 0)
        return;                         // nothing to do, or a fade still running: wait for it
    const bool fade = ! fresh_;
    if (fade)
        for (Channel& c : channels_)
        {
            std::copy (c.taps.begin(), c.taps.begin() + c.numTaps, c.fadeTaps.begin());
            c.numFadeTaps = c.numTaps;
        }
    if (gainsDirty_)
        rebuildGains();                 // may change the tap count, so the delays follow
    placeTaps();
    gainsDirty_ = delaysDirty_ = false;
    fresh_ = false;
    fadeRemaining_ = fade ? fadeLen_ : 0;
}

// The envelope over the layout: every tap up to this knee's end, with the gain the reference
// model gives it (the same expression, term for term).
void NonLinBurstEngine::rebuildGains() noexcept
{
    const Envelope env (kneeMs_);
    const double end = env.endMs();
    for (Channel& c : channels_)
    {
        const int available = static_cast<int> (c.layout.size());
        int k = 0;
        for (; k < available; ++k)
        {
            const LayoutTap& l = c.layout[static_cast<size_t> (k)];
            if (l.t > end)
                break;
            const double level = std::pow (10.0, (env.db (l.t) + env.gainDb) / 20.0) / l.densNorm;
            Tap& tap = c.taps[static_cast<size_t> (k)];
            tap.gain = static_cast<float> (level * l.normal);
            tap.dark = l.dark;
        }
        c.numTaps = k;
    }
}

void NonLinBurstEngine::placeTaps() noexcept
{
    for (Channel& c : channels_)
        for (int k = 0; k < c.numTaps; ++k)
            c.taps[static_cast<size_t> (k)].delay = static_cast<int> (
                std::lround ((kOnsetMs + preDelayMs_ + c.layout[static_cast<size_t> (k)].t) * 1e-3 * sampleRate_));
}

void NonLinBurstEngine::sumTaps (const Channel& c, const Tap* taps, int count, int writePos, int ringLen,
                                 float* acc, int n) noexcept
{
    std::fill (acc, acc + n, 0.0f);
    for (int t = 0; t < count; ++t)
    {
        const Tap& tap = taps[t];
        int start = (writePos - tap.delay) % ringLen;
        if (start < 0) start += ringLen;
        const float* src = (tap.dark ? c.dark.data() : c.raw.data()) + start;
        const float g = tap.gain;
        for (int i = 0; i < n; ++i)
            acc[i] += g * src[i];
    }
}

void NonLinBurstEngine::processChannel (Channel& c, const float* in, float* out, int n) noexcept
{
    if (n <= 0 || n > maxBlock_)
        return;
    // Write the block into both rings first (twice each, ringLen_ apart, so any window of up to
    // maxBlock_ samples reads as one contiguous run), then read every tap as one run.
    for (int i = 0; i < n; ++i)
    {
        const int w = (writePos_ + i) % ringLen_;
        const float x = in[i];
        const float d = c.darkLp.process (x) * darkNorm_;
        c.raw[static_cast<size_t> (w)] = c.raw[static_cast<size_t> (w + ringLen_)] = x;
        c.dark[static_cast<size_t> (w)] = c.dark[static_cast<size_t> (w + ringLen_)] = d;
    }
    float* acc = acc_.data();
    sumTaps (c, c.taps.data(), c.numTaps, writePos_, ringLen_, acc, n);
    if (fadeRemaining_ > 0)
    {
        // Linear: the two sums are the same input through nearly the same taps, so their
        // amplitudes add; an equal-power law would bulge in the middle of the fade.
        float* old = fadeAcc_.data();
        sumTaps (c, c.fadeTaps.data(), c.numFadeTaps, writePos_, ringLen_, old, n);
        const float step = 1.0f / static_cast<float> (fadeLen_);
        const int fading = std::min (n, fadeRemaining_);
        for (int i = 0; i < fading; ++i)
        {
            const float g = static_cast<float> (fadeLen_ - fadeRemaining_ + i + 1) * step;
            acc[i] = old[i] + g * (acc[i] - old[i]);
        }
    }
    const bool damp = hfDampHz_ < kHfDampOffHz;
    for (int i = 0; i < n; ++i)
    {
        const float y = c.mult[1].process (c.mult[0].process (acc[i]));
        const float o = c.outLp.process (y + tailSend_ * c.tail.process (y));
        // The damping filter runs even while bypassed, so switching it in continues from the
        // signal instead of from whatever it held when it was last switched out.
        const float damped = c.hfDamp.process (o);
        out[i] = damp ? damped : o;
    }
}

void NonLinBurstEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
{
    if (numSamples <= 0 || ringLen_ == 0)
        return;
    updateTaps();
    for (int done = 0; done < numSamples; )
    {
        const int n = std::min (maxBlock_, numSamples - done);
        processChannel (channels_[0], inL + done, outL + done, n);
        processChannel (channels_[1], inR + done, outR + done, n);
        writePos_ = (writePos_ + n) % ringLen_;
        fadeRemaining_ = std::max (0, fadeRemaining_ - n);
        done += n;
    }
}

} // namespace duskverb
