// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// EarlyField: DuskVerb 2's early-field module, wrapped around a shared engine without touching
// it. It adds the energy-placement and early-reflection mechanisms the classic hall prototype
// showed the shared engines lack (tools repo classic_lab/README.md): the same engines then keep
// every table their presets were tuned with.
//
//   engine input = (1 - inject) x in + inject x injector(in) / its RMS gain
//   wet out     += output stage (shelf (lowpass (direct x injector'(in) + reflections(in))))
//
// The injector is a run of discrete taps over [start, start + window]; its level slopes across
// the window by slopeDb after a fade-in over attackMs (-24 dB to 0). The onset burst is a few milliseconds of velvet pulses heard directly, the same on both sides: a peak
// the attack can land on without the low-frequency echo a cluster of same-sign reflection taps
// makes (eight taps inside 5 ms sum coherently below 1 kHz). The side burst is the same kind of
// pulse run, decaying 60 dB across its length after a fade-in, lowpassed (Linkwitz-Riley 4) and
// played anti-phase (+left, -right): the strong, short, out-of-phase low early reflection a room
// target opens with (Live Room's first 25 ms: lows at -0.9 correlation, 8 dB over DuskVerb's),
// which no width setting on the tank can give without also turning its tail anti-phase. Sent into the engine it
// moves where the engine's energy lands (a rising slope delays it, the direct path brings energy
// forward); heard directly, with the reflections, it gives the early field discrete arrivals.
// Each side has its own direct tap stream and the reflections alternate sides, so the field is
// as decorrelated as the engine's own. With sideOffsetMs the two sides instead play one stream a
// few ms apart: correlated, and in opposite phase where the offset is half a period, as a
// reference that taps one cluster twice is. The low shelf (off at 0 dB) takes the lows out of the
// direct taps and reflections, which leaves them to the engine.
//
// Tap layout: SplitMix64, two draws per tap (time jitter, sign), mirrored by taps_kernel() in
// the tools repo's classic_lab/earlyfield_screen.py so a layout screened there renders
// identically here: the same delays, the same gains to float rounding. The layout arithmetic is
// the model's, in double, so a delay rounds as the model's does.
//
// Both engines of a preset crossfade read one EarlyFieldInput: what came in is the same for
// both, so a field installed at a swap starts with the whole history instead of its taps coming
// in one by one as a ring of its own fills. write() takes a whole block before either field reads
// it, so the ring holds the longest delay plus the longest block. Sized to the delay alone, a tap
// reaching back nearly the whole ring read samples the block had just overwritten, and the output
// depended on the block size.
//
// The pre path (the injector) is mixed into the engine's input, so the engine's output stage
// covers it. The post path (direct taps and reflections) is rendered on its own: DuskVerbDSP runs
// it through the controls the engine's output stage applies (Dv2OutputShell) and crossfades it
// apart from the engines.
//
// Real time: EarlyFieldInput::prepare() allocates; everything else is audio-thread safe (fixed
// tables). A disabled config is never touched by the wet path, so presets without one stay
// bit-identical.

#pragma once

#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace duskverb
{

// Discrete reflections a field can place: the targets put up to ten distinct arrivals in their
// first 150 ms (the full_check early-reflection count), more than the four the field first had.
inline constexpr int kMaxEarlyReflections = 10;

struct EarlyFieldConfig
{
    bool  enabled   = false;
    float startMs   = 0.0f;      // after the input (the field has no predelay of its own)
    float windowMs  = 50.0f;
    int   taps      = 16;
    float slopeDb   = 0.0f;      // level change across the window, end minus start
    float attackMs  = 0.0f;      // fade-in from -24 dB
    float inject    = 0.0f;      // 0..1 of the engine's input taken through the injector
    float directDb  = -120.0f;   // injector heard at the output (peak tap); -120 is off
    float reflDb    = -120.0f;   // each reflection; -120 is off
    int   reflCount = 0;
    std::array<float, kMaxEarlyReflections> reflMs {};
    float lowpassHz = 8000.0f;   // direct and reflection paths
    float burstDb = -120.0f;     // onset burst, peak pulse; -120 is off
    float burstStartMs = 0.0f;   // after the input
    float burstMs = 4.0f;        // its length
    float burstLowpassHz = 12000.0f;
    float sideDb = -120.0f;      // side burst, peak pulse; -120 is off
    float sideStartMs = 15.0f;   // after the input
    float sideMs = 30.0f;        // its length (60 dB down at the end)
    float sideAttackMs = 2.0f;   // fade-in from -24 dB
    float sideLowpassHz = 250.0f;
    // Each reflection's level over reflDb, in dB (0: at reflDb), in reflMs' order: a reference's
    // repeats fall off from one to the next, which one level for all ten cannot follow.
    std::array<float, kMaxEarlyReflections> reflTrimDb {};
    // One pattern for both sides' direct taps (the right side's), the left side later by this many
    // ms, or the right side later by as many when negative; 0: each side its own pattern. A
    // reference whose two sides play one cluster a few ms apart is in opposite phase where that
    // offset is half a period (2.5 ms: 200 Hz) and in phase well below it, so its mono sum loses
    // level there. Two independent patterns are uncorrelated at every frequency and lose none.
    float sideOffsetMs = 0.0f;
    // Low shelf on the direct taps and the reflections (RBJ, slope 1): dB below shelfHz; 0: none.
    // Cutting the cluster's low frequencies lets the tank set the stereo width of the bass.
    float shelfDb = 0.0f;
    float shelfHz = 250.0f;
    // Give every tap negative polarity on the left and positive on the right.
    // Closely spaced taps then add at low frequencies on each side, putting the
    // lows in opposite phase across the stereo field.
    bool sidesOpposed = false;
    // Replace the direct cluster with one polarity-inverted return on each input channel.
    int invertedTapSamples = -1;
    unsigned reversedReflections = 0;   // bit r reverses the polarity of reflection r
};

// The input history the early fields read: one stereo ring for both engines of the pair.
class EarlyFieldInput
{
public:
    static constexpr float kMaxDelayMs = 400.0f;
    static constexpr float kSilenceFadeMs = 5.0f;    // into and out of silence (Freeze)

    // Allocates. maxBlockSize: the longest block write() will see.
    void prepare (double sampleRate, int maxBlockSize)
    {
        sr_ = sampleRate;
        maxDelay_ = static_cast<int> (std::lround (kMaxDelayMs * 1e-3 * sampleRate));
        len_ = maxDelay_ + std::max (1, maxBlockSize);
        fadeStep_ = static_cast<float> (1.0 / (kSilenceFadeMs * 1e-3 * sampleRate));
        for (auto& r : ring_) r.assign (static_cast<size_t> (len_), 0.0f);
        pos_ = blockStart_ = 0;
        fade_ = -1.0f;
    }

    void clear() noexcept
    {
        for (auto& r : ring_) std::fill (r.begin(), r.end(), 0.0f);
        pos_ = blockStart_ = 0;
        fade_ = -1.0f;
    }

    // One block in, before either field reads it. `silent` (Freeze) fades the input out over
    // kSilenceFadeMs, a raised cosine, and keeps it silent until `silent` clears, when it fades
    // back in: the fields then take nothing new and drain over their windows. In one step, the
    // edge would reach the output once per tap, each tap cutting a sustained input off (or in) in
    // turn. A fresh or cleared ring starts where `silent` puts it, with no fade.
    void write (const float* left, const float* right, int n, bool silent) noexcept
    {
        if (len_ == 0)
            return;
        const float target = silent ? 0.0f : 1.0f;
        if (fade_ < 0.0f)
            fade_ = target;
        auto& l = ring_[0];
        auto& r = ring_[1];
        int pos = pos_;
        for (int i = 0; i < n; ++i)
        {
            float g = fade_;
            if (fade_ != target)
            {
                fade_ = target > fade_ ? std::min (target, fade_ + fadeStep_) : std::max (target, fade_ - fadeStep_);
                g = 0.5f - 0.5f * std::cos (3.14159265f * fade_);
            }
            l[static_cast<size_t> (pos)] = g < 1.0f ? g * left[i] : left[i];
            r[static_cast<size_t> (pos)] = g < 1.0f ? g * right[i] : right[i];
            if (++pos == len_) pos = 0;
        }
        blockStart_ = pos_;
        pos_ = pos;
    }

    double sampleRate() const noexcept { return sr_; }
    int maxDelay() const noexcept { return maxDelay_; }

private:
    friend class EarlyField;
    double sr_ = 48000.0;
    std::array<std::vector<float>, 2> ring_;
    int len_ = 0, maxDelay_ = 0, pos_ = 0, blockStart_ = 0;
    float fade_ = -1.0f, fadeStep_ = 0.0f;           // where the fade is, 0 silent to 1 open; -1 unset
};

class EarlyField
{
public:
    static constexpr int   kMaxTaps        = 64;
    static constexpr int   kMaxReflections = kMaxEarlyReflections;
    static constexpr int   kMaxBurstTaps   = 32;        // 10.7 ms at the burst's density
    static constexpr double kBurstPerSecond = 3000.0;   // velvet density: a smooth, unpitched burst
    static constexpr double kSidePerSecond  = 2000.0;   // the side burst's density: 32 ms in kMaxTaps
    static constexpr float kMaxDelayMs     = EarlyFieldInput::kMaxDelayMs;
    static constexpr float kOffDb          = -120.0f;   // a path at this level is off: exactly silent
    static constexpr float kMaxSideOffsetMs = 20.0f;    // one side's direct taps behind the other's

    // The ranges a config keeps: every tap and reflection inside kMaxDelayMs. The voicing table
    // is checked against them when it compiles and the tuning hook refuses anything outside;
    // configure() still clamps what it cannot hold.
    static constexpr bool valid (const EarlyFieldConfig& c) noexcept
    {
        if (c.taps < 1 || c.taps > kMaxTaps || c.reflCount < 0 || c.reflCount > kMaxReflections
            || c.invertedTapSamples < -1 || c.reversedReflections >= (1u << kMaxReflections))
            return false;
        if (! (c.startMs >= 0.0f && c.windowMs >= 0.0f && c.startMs + c.windowMs <= kMaxDelayMs
               && c.attackMs >= 0.0f && c.attackMs <= kMaxDelayMs && c.slopeDb >= -60.0f && c.slopeDb <= 60.0f))
            return false;
        if (! (c.inject >= 0.0f && c.inject <= 1.0f && c.lowpassHz >= 20.0f && c.lowpassHz <= 48000.0f
               && c.directDb >= kOffDb && c.directDb <= 12.0f && c.reflDb >= kOffDb && c.reflDb <= 12.0f))
            return false;
        for (int r = 0; r < c.reflCount; ++r)
            if (! (c.reflMs[static_cast<size_t> (r)] >= 0.0f && c.reflMs[static_cast<size_t> (r)] <= kMaxDelayMs
                   && c.reflTrimDb[static_cast<size_t> (r)] >= -60.0f && c.reflTrimDb[static_cast<size_t> (r)] <= 24.0f
                   && c.reflDb + c.reflTrimDb[static_cast<size_t> (r)] <= 12.0f))
                return false;
        return c.burstDb >= kOffDb && c.burstDb <= 12.0f && c.burstStartMs >= 0.0f && c.burstMs >= 0.5f
            && c.burstMs * kBurstPerSecond * 1e-3 <= kMaxBurstTaps && c.burstStartMs + c.burstMs <= kMaxDelayMs
            && c.burstLowpassHz >= 20.0f && c.burstLowpassHz <= 48000.0f
            && c.sideDb >= kOffDb && c.sideDb <= 12.0f && c.sideStartMs >= 0.0f && c.sideMs >= 1.0f
            && c.sideMs * kSidePerSecond * 1e-3 <= kMaxTaps && c.sideStartMs + c.sideMs <= kMaxDelayMs
            && c.sideAttackMs >= 0.0f && c.sideAttackMs <= c.sideMs && c.sideLowpassHz >= 20.0f && c.sideLowpassHz <= 2000.0f
            && c.sideOffsetMs >= -kMaxSideOffsetMs && c.sideOffsetMs <= kMaxSideOffsetMs
            && c.startMs + c.windowMs + (c.sideOffsetMs < 0.0f ? -c.sideOffsetMs : c.sideOffsetMs) <= kMaxDelayMs
            && c.shelfDb >= -24.0f && c.shelfDb <= 12.0f && c.shelfHz >= 20.0f && c.shelfHz <= 2000.0f;
    }

    // The rate and the longest delay of the input this field reads; prepare that first.
    void prepare (const EarlyFieldInput& in) noexcept
    {
        sr_ = in.sampleRate();
        maxDelay_ = in.maxDelay();
        configure (cfg_);
        clearState();
    }

    void clearState() noexcept
    {
        for (auto& lp : lowpass_) lp.reset();
        for (auto& f : shelf_) f.reset();
        burstLowpass_.reset();
        for (auto& f : sideLowpass_) f.reset();
    }

    // The post path's state of `other`, for a field taking over from the one fading out: with
    // the input shared, that is all a field holds. A OnePoleLP copies whole, so this field's own
    // cutoff goes back on. The shelf and the two burst filters run only while their path is on,
    // so where the other field's is off its state is history from whenever it last ran: this
    // field's starts over instead (a recall after a long silence would otherwise replay it).
    void copyStateFrom (const EarlyField& other) noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            lowpass_[static_cast<size_t> (s)] = other.lowpass_[static_cast<size_t> (s)];
            lowpass_[static_cast<size_t> (s)].setCutoff (lowpassHz_, sr_);
            // The other field's shelf state under this field's own shelf, as the side burst's
            // lowpass below.
            if (other.shelfOn_)
            {
                const duskaudio::BiquadCoeffsDouble own = shelf_[static_cast<size_t> (s)].coeffs();
                shelf_[static_cast<size_t> (s)] = other.shelf_[static_cast<size_t> (s)];
                shelf_[static_cast<size_t> (s)].setCoeffs (own);
            }
            else
            {
                shelf_[static_cast<size_t> (s)].reset();
            }
        }
        if (other.burstGain_ > 0.0f)
        {
            burstLowpass_ = other.burstLowpass_;
            burstLowpass_.setCutoff (burstLowpassHz_, sr_);
        }
        else
        {
            burstLowpass_.reset();
        }
        for (int k = 0; k < 2; ++k)
        {
            if (other.sideGain_ > 0.0f)
            {
                const duskaudio::BiquadCoeffsDouble own = sideLowpass_[k].coeffs();
                sideLowpass_[k] = other.sideLowpass_[k];
                sideLowpass_[k].setCoeffs (own);
            }
            else
            {
                sideLowpass_[k].reset();
            }
        }
    }

    // False until prepare(): an unprepared field has no input to place its taps in.
    bool enabled() const noexcept { return cfg_.enabled && maxDelay_ > 0; }
    // An enabled field whose direct taps or reflections reach the output.
    bool postEnabled() const noexcept
    { return enabled() && (directGain_ > 0.0f || reflCount_ > 0 || burstGain_ > 0.0f || sideGain_ > 0.0f); }

    // Audio thread safe: fixed-size tables only. The state is left alone: a field that takes over
    // from another, or starts over, says so (copyStateFrom, clearState).
    void configure (const EarlyFieldConfig& c) noexcept
    {
        cfg_ = c;
        const int n = std::clamp (c.taps, 1, kMaxTaps);
        layout (injector_, n, 0x11ull);
        const float leftSign = c.sidesOpposed ? -1.0f : 0.0f, rightSign = c.sidesOpposed ? 1.0f : 0.0f;
        if (c.invertedTapSamples >= 0)
        {
            for (auto& direct : direct_)
            {
                direct.count = 1;
                direct.delay[0] = std::min (c.invertedTapSamples, maxDelay_);
                direct.gain[0] = -1.0f;
            }
        }
        else if (c.sideOffsetMs == 0.0f)
        {
            layout (direct_[0], n, 0x23ull, 0, leftSign);
            layout (direct_[1], n, 0x29ull, 0, rightSign);
        }
        else                                   // the right side's pattern on both, one side later
        {
            const int later = delaySamples (std::abs (static_cast<double> (c.sideOffsetMs)));
            layout (direct_[0], n, 0x29ull, c.sideOffsetMs > 0.0f ? later : 0, leftSign);
            layout (direct_[1], n, 0x29ull, c.sideOffsetMs < 0.0f ? later : 0, rightSign);
        }
        double e = 0.0;
        for (int k = 0; k < n; ++k)
            e += static_cast<double> (injector_.gain[static_cast<size_t> (k)]) * injector_.gain[static_cast<size_t> (k)];
        injectorNorm_ = static_cast<float> (1.0 / std::sqrt (std::max (e, 1e-12)));
        inject_     = std::clamp (c.inject, 0.0f, 1.0f);
        directGain_ = gain (c.directDb);
        reflGain_   = gain (c.reflDb);
        reflCount_  = reflGain_ > 0.0f ? std::clamp (c.reflCount, 0, kMaxReflections) : 0;
        for (int r = 0; r < kMaxReflections; ++r)
        {
            reflDelay_[static_cast<size_t> (r)] = delaySamples (c.reflMs[static_cast<size_t> (r)]);
            // Exactly reflGain_ at a trim of 0, so a row without trims plays as it always did.
            const float trim = c.reflTrimDb[static_cast<size_t> (r)];
            const float level = trim == 0.0f ? reflGain_ : reflGain_ * std::pow (10.0f, trim / 20.0f);
            const bool reverse = (c.reversedReflections & (1u << r)) != 0;
            const bool leftOpposed = c.sidesOpposed && r % 2 == 0;
            reflGainEach_[static_cast<size_t> (r)] = reverse != leftOpposed ? -level : level;
        }
        lowpassHz_ = std::min (c.lowpassHz, 0.45f * static_cast<float> (sr_));
        for (auto& lp : lowpass_)
            lp.setCutoff (lowpassHz_, sr_);
        shelfOn_ = c.shelfDb != 0.0f;
        const duskaudio::BiquadCoeffsDouble shelf = shelfOn_ ? lowShelf (sr_, std::min (c.shelfHz, 2000.0f), c.shelfDb)
                                                             : duskaudio::BiquadCoeffsDouble {};
        for (auto& f : shelf_) f.setCoeffs (shelf);
        burstGain_ = gain (c.burstDb);
        burstLayout (c.burstStartMs, c.burstMs);
        burstLowpassHz_ = std::min (c.burstLowpassHz, 0.45f * static_cast<float> (sr_));
        burstLowpass_.setCutoff (burstLowpassHz_, sr_);
        sideGain_ = gain (c.sideDb);
        sideLayout (c.sideStartMs, c.sideMs, c.sideAttackMs);
        const duskaudio::BiquadCoeffsDouble lp = butterworthLowpass (sr_, std::min (c.sideLowpassHz, 2000.0f));
        for (auto& f : sideLowpass_) f.setCoeffs (lp);
    }

    // In place on the engine's input, for the block `in` has just taken. Inject 0 leaves it alone.
    void preEngine (const EarlyFieldInput& in, float* left, float* right, int n) const noexcept
    {
        if (! (inject_ > 0.0f))
            return;
        const float keep = 1.0f - inject_, send = inject_ * injectorNorm_;
        int pos = in.blockStart_;
        for (int offset = 0; offset < n; offset += kTile)
        {
            const int count = std::min (n - offset, kTile);
            float taps[kTile];
            for (int side = 0; side < 2; ++side)
            {
                sum (in, side, pos, injector_, taps, count);
                float* io = (side == 0 ? left : right) + offset;
                for (int i = 0; i < count; ++i) io[i] = keep * io[i] + send * taps[i];
            }
            pos = advance (in, pos, count);
        }
    }

    void renderPost (const EarlyFieldInput& in, float* left, float* right, int n, bool monoInput = false) noexcept
    {
        int pos = in.blockStart_;
        for (int offset = 0; offset < n; offset += kTile)
        {
            const int count = std::min (n - offset, kTile);
            float burst[kTile] {}, sideBurst[kTile] {}, other[kTile];
            if (burstGain_ > 0.0f)
            {
                sum (in, 0, pos, burst_, burst, count);
                sum (in, 1, pos, burst_, other, count);
                for (int i = 0; i < count; ++i)
                    burst[i] = burstLowpass_.process (burstGain_ * 0.5f * (burst[i] + other[i]));
            }
            if (sideGain_ > 0.0f)
            {
                sum (in, 0, pos, side_, sideBurst, count);
                sum (in, 1, pos, side_, other, count);
                for (int i = 0; i < count; ++i)
                    sideBurst[i] = sideLowpass_[1].process (sideLowpass_[0].process (
                        sideGain_ * 0.5f * (sideBurst[i] + other[i])));
            }
            for (int ear = 0; ear < 2; ++ear)
            {
                float* out = (ear == 0 ? left : right) + offset;
                outputTaps (in, ear, ear, pos, out, count);
                if (monoInput)
                {
                    outputTaps (in, 1 - ear, ear, pos, other, count);
                    for (int i = 0; i < count; ++i) out[i] = 0.5f * (out[i] + other[i]);
                }
                for (int i = 0; i < count; ++i)
                {
                    float early = lowpass_[ear].process (out[i]);
                    if (shelfOn_) early = shelf_[ear].process (early);
                    out[i] = early + burst[i] + (ear == 0 ? sideBurst[i] : -sideBurst[i]);
                }
            }
            pos = advance (in, pos, count);
        }
    }

    // The configured layout: delays in samples, gains with the peak tap at 1. For the tests
    // and the check against the model.
    struct Layout
    {
        std::array<int, kMaxTaps>   delay {};
        std::array<float, kMaxTaps> gain {};
        int count = 0;
    };
    const Layout& injector() const noexcept { return injector_; }
    const Layout& direct (int side) const noexcept { return direct_[side != 0 ? 1 : 0]; }
    int reflectionDelay (int r) const noexcept
    { return reflDelay_[static_cast<size_t> (std::clamp (r, 0, kMaxReflections - 1))]; }
    const Layout& burst() const noexcept { return burst_; }
    const Layout& sideBurst() const noexcept { return side_; }

private:
    // SplitMix64 uniform in [0, 1): the stream classic_lab/earlyfield_screen.py mirrors.
    static double uniform (uint64_t& s) noexcept
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double> (z >> 11) * (1.0 / 9007199254740992.0);
    }

    // `later`: samples added to every delay, so one seed laid out twice gives the same pattern a
    // whole number of samples apart. `sign`: below or above 0, every tap takes that sign; at 0 each
    // draws its own. The draw is made either way, so the times do not depend on it.
    void layout (Layout& l, int n, uint64_t seed, int later = 0, float sign = 0.0f) const noexcept
    {
        const double start = cfg_.startMs, window = cfg_.windowMs;
        const double slope = std::clamp (cfg_.slopeDb, -60.0f, 60.0f), attack = cfg_.attackMs;
        uint64_t s = seed;
        float peak = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            const double jitter = uniform (s), signDraw = uniform (s);
            const double u = k == 0 ? 0.0 : std::clamp ((k + jitter - 0.5) / n, 0.0, 1.0);
            const double ramp = attack > 0.0 ? -24.0 * std::max (0.0, 1.0 - u * window / attack) : 0.0;
            const float g = static_cast<float> (std::pow (10.0, (slope * (u - 0.5) + ramp) / 20.0))
                          * (sign != 0.0f ? (sign < 0.0f ? -1.0f : 1.0f) : (signDraw < 0.5 ? -1.0f : 1.0f));
            l.delay[static_cast<size_t> (k)] = std::min (delaySamples (start + u * window) + later, maxDelay_);
            l.gain[static_cast<size_t> (k)] = g;
            peak = std::max (peak, std::abs (g));
        }
        for (int k = 0; k < n; ++k)
            l.gain[static_cast<size_t> (k)] /= std::max (peak, 1e-30f);
        l.count = n;
    }

    // The onset burst: one velvet pulse per 1 / kBurstPerSecond segment over [start, start + length],
    // SplitMix64 (seed 0x31, two draws per pulse: position in the segment, sign), decaying 20 dB
    // across the burst; peak pulse 1.
    void burstLayout (double startMs, double lengthMs) noexcept
    {
        const int n = std::clamp (static_cast<int> (std::lround (lengthMs * 1e-3 * kBurstPerSecond)), 1, kMaxBurstTaps);
        uint64_t s = 0x31ull;
        float peak = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            const double where = uniform (s), signDraw = uniform (s);
            const double u = (k + where) / n;                             // 0..1 across the burst
            const float g = static_cast<float> (std::pow (10.0, -20.0 * u / 20.0)) * (signDraw < 0.5 ? -1.0f : 1.0f);
            burst_.delay[static_cast<size_t> (k)] = delaySamples (startMs + u * lengthMs);
            burst_.gain[static_cast<size_t> (k)] = g;
            peak = std::max (peak, std::abs (g));
        }
        for (int k = 0; k < n; ++k)
            burst_.gain[static_cast<size_t> (k)] /= std::max (peak, 1e-30f);
        burst_.count = n;
    }

    // The side burst: one velvet pulse per 1 / kSidePerSecond segment over [start, start + length],
    // SplitMix64 (seed 0x37, two draws per pulse), a fade-in from -24 dB over attack, then 60 dB of
    // exponential decay across the length; peak pulse 1.
    void sideLayout (double startMs, double lengthMs, double attackMs) noexcept
    {
        const int n = std::clamp (static_cast<int> (std::lround (lengthMs * 1e-3 * kSidePerSecond)), 1, kMaxTaps);
        uint64_t s = 0x37ull;
        float peak = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            const double where = uniform (s), signDraw = uniform (s);
            const double u = (k + where) / n;
            const double tMs = u * lengthMs;
            const double ramp = attackMs > 0.0 ? -24.0 * std::max (0.0, 1.0 - tMs / attackMs) : 0.0;
            const float g = static_cast<float> (std::pow (10.0, (ramp - 60.0 * u) / 20.0)) * (signDraw < 0.5 ? -1.0f : 1.0f);
            side_.delay[static_cast<size_t> (k)] = delaySamples (startMs + tMs);
            side_.gain[static_cast<size_t> (k)] = g;
            peak = std::max (peak, std::abs (g));
        }
        for (int k = 0; k < n; ++k)
            side_.gain[static_cast<size_t> (k)] /= std::max (peak, 1e-30f);
        side_.count = n;
    }

    // RBJ second-order Butterworth low-pass in double; two in series make the side burst's
    // Linkwitz-Riley 4.
    static duskaudio::BiquadCoeffsDouble butterworthLowpass (double fs, double hz) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * hz / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;
        duskaudio::BiquadCoeffsDouble c;
        c.b0 = c.b2 = (1.0 - cw) * 0.5 / a0;
        c.b1 = (1.0 - cw) / a0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    // RBJ low shelf with shelf slope 1, in double: at 250 Hz and a high rate the poles sit too
    // close to z = 1 for float coefficients.
    static duskaudio::BiquadCoeffsDouble lowShelf (double fs, double hz, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * hz / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) * 0.70710678118654752;
        const double k = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) + (A - 1.0) * cw + k;
        duskaudio::BiquadCoeffsDouble c;
        c.b0 = A * ((A + 1.0) - (A - 1.0) * cw + k) / a0;
        c.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0;
        c.b2 = A * ((A + 1.0) - (A - 1.0) * cw - k) / a0;
        c.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0;
        c.a2 = ((A + 1.0) + (A - 1.0) * cw - k) / a0;
        return c;
    }

    static float gain (float db) noexcept { return db <= kOffDb ? 0.0f : std::pow (10.0f, db / 20.0f); }

    // lround halves away from zero, as the model's floor(x + 0.5) does for these positive times.
    int delaySamples (double ms) const noexcept
    {
        return static_cast<int> (std::clamp (std::lround (ms * 1e-3 * sr_), 0L, static_cast<long> (maxDelay_)));
    }

    static constexpr int kTile = 64;
    static int advance (const EarlyFieldInput& in, int pos, int count) noexcept
    {
        pos += count;
        return pos < in.len_ ? pos : pos - in.len_;
    }

    // The ring holds the complete input block plus its longest delay. Each tap spans at most
    // one wrap, so consecutive output frames can accumulate it with contiguous reads.
    static void addTap (const std::vector<float>& ring, int pos, int delay, float gain, float* out, int count) noexcept
    {
        const int size = static_cast<int> (ring.size());
        int read = pos - delay;
        if (read < 0) read += size;
        const int first = std::min (count, size - read);
        for (int i = 0; i < first; ++i) out[i] += gain * ring[read + i];
        for (int i = first; i < count; ++i) out[i] += gain * ring[i - first];
    }

    static void sum (const EarlyFieldInput& in, int side, int pos, const Layout& layout, float* out, int count) noexcept
    {
        std::fill_n (out, count, 0.0f);
        for (int tap = 0; tap < layout.count; ++tap)
            addTap (in.ring_[side], pos, layout.delay[tap], layout.gain[tap], out, count);
    }

    void outputTaps (const EarlyFieldInput& in, int input, int ear, int pos, float* out, int count) const noexcept
    {
        if (directGain_ > 0.0f)
        {
            sum (in, input, pos, direct_[ear], out, count);
            for (int i = 0; i < count; ++i) out[i] *= directGain_;
        }
        else std::fill_n (out, count, 0.0f);
        for (int r = ear; r < reflCount_; r += 2)
            addTap (in.ring_[input], pos, reflDelay_[r], reflGainEach_[r], out, count);
    }

    double sr_ = 48000.0;
    EarlyFieldConfig cfg_;
    int maxDelay_ = 0;
    Layout injector_, direct_[2];
    float injectorNorm_ = 1.0f, inject_ = 0.0f, directGain_ = 0.0f, reflGain_ = 0.0f;
    int reflCount_ = 0;
    std::array<int, kMaxReflections> reflDelay_ {};
    std::array<float, kMaxReflections> reflGainEach_ {};     // reflGain_ times each reflection's trim
    float lowpassHz_ = 8000.0f;
    duskaudio::OnePoleLP lowpass_[2];
    duskaudio::DoubleBiquad shelf_[2];
    bool shelfOn_ = false;
    Layout burst_;
    float burstGain_ = 0.0f, burstLowpassHz_ = 12000.0f;
    duskaudio::OnePoleLP burstLowpass_;
    Layout side_;
    float sideGain_ = 0.0f;
    duskaudio::DoubleBiquad sideLowpass_[2];
};

} // namespace duskverb
