// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbToneGraph.hpp - the arithmetic behind the editor's TONE graph: relative decay
// across frequency (the damping card's three multipliers and two crossovers, through the
// profile in core/DuskVerbDampingProfile.hpp that the damping scaler applies to the engines)
// under the wet Low Cut and High Cut, with a handle per control. The graph draws the
// settings, not a measurement.
//
// Pure: no ImGui and no state. The editor draws and drives the graph through these
// functions, and DuskVerbToneGraphTests.cpp checks the mapping, the hit-testing and the drag
// arithmetic without a window.

#pragma once

#include "DuskLogFreqAxis.hpp"
#include "../core/DuskVerbDampingProfile.hpp"  // DampingKnobs, logDecayProfile
#include "../core/DuskVerbParamTable.hpp"      // the parameters' ranges

#include <algorithm>
#include <cmath>

namespace duskverb::ui
{

// The handles. Bass sits at (Low Crossover, Bass Multiply) and Treble at (High Crossover,
// Treble Multiply), so one drag moves a band's edge sideways and its decay up or down; Mid
// sits on the mid band between them and moves only up or down; the two cuts are vertical bars.
enum class ToneHandle { None, LoCut, Bass, Mid, Treble, HiCut };

// Plain parameter values: Hz and decay multipliers.
struct ToneValues
{
    float loCut = 20.0f, hiCut = 20000.0f;
    float bass = 1.0f, mid = 1.0f, treble = 1.0f;
    float lowX = 500.0f, highX = 4000.0f;
};

// Which parameters take input. A handle moves only what is live; a handle with nothing live is
// drawn dimmed and never hit.
struct ToneLive
{
    bool loCut = true, hiCut = true, bass = true, mid = true, treble = true, lowX = true, highX = true;
};

struct TonePoint { float x, y; };

class ToneGraph
{
public:
    // The vertical axis, log scale: Treble's 0.1x floor to a little over the bass and mid
    // 2.5x ceiling.
    static constexpr float kMinMult = 0.1f, kMaxMult = 3.0f;
    static constexpr float kHandleGrab = 12.0f;   // design px from a handle's centre
    static constexpr float kBarGrab = 7.0f;       // design px either side of a cut bar

    ToneGraph(float x0, float y0, float x1, float y1) noexcept
        : x0_(x0), y0_(y0), x1_(x1), y1_(y1) {}

    float left() const noexcept   { return x0_; }
    float top() const noexcept    { return y0_; }
    float right() const noexcept  { return x1_; }
    float bottom() const noexcept { return y1_; }
    const duskdaf::LogFreqAxis& axis() const noexcept { return axis_; }

    float xOf(float hz) const noexcept   { return x0_ + axis_.toNorm(hz) * (x1_ - x0_); }
    float hzOf(float x) const noexcept   { return axis_.fromNorm((x - x0_) / (x1_ - x0_)); }
    float yOf(float mult) const noexcept
    {
        const float m = std::clamp(mult, kMinMult, kMaxMult);
        return y1_ - (std::log(m) - logMin()) / (logMax() - logMin()) * (y1_ - y0_);
    }
    float multOf(float y) const noexcept
    {
        return std::exp(logMin() + (y1_ - y) / (y1_ - y0_) * (logMax() - logMin()));
    }

    // The relative decay the controls ask for at `hz` (1 = the Decay knob's own time).
    static float multiplierAt(const ToneValues& v, float hz) noexcept
    {
        const DampingKnobs knobs { v.bass, v.mid, v.treble, v.lowX, v.highX };
        return std::exp(logDecayProfile(knobs, hz));
    }

    // A mid band exists only between the two crossovers.
    static bool hasMidBand(const ToneValues& v) noexcept { return v.highX > v.lowX; }

    TonePoint handleAt(ToneHandle h, const ToneValues& v) const noexcept
    {
        switch (h)
        {
            case ToneHandle::LoCut:  return { cutX(v.loCut), y0_ };
            case ToneHandle::HiCut:  return { cutX(v.hiCut), y0_ };
            case ToneHandle::Bass:   return { xOf(v.lowX), yOf(v.bass) };
            case ToneHandle::Treble: return { xOf(v.highX), yOf(v.treble) };
            case ToneHandle::Mid:    return { xOf(std::sqrt(v.lowX * v.highX)), yOf(v.mid) };
            case ToneHandle::None:   break;
        }
        return { 0.0f, 0.0f };
    }

    static bool movable(ToneHandle h, const ToneValues& v, const ToneLive& live) noexcept
    {
        switch (h)
        {
            case ToneHandle::LoCut:  return live.loCut;
            case ToneHandle::HiCut:  return live.hiCut;
            case ToneHandle::Bass:   return live.bass || live.lowX;
            case ToneHandle::Treble: return live.treble || live.highX;
            case ToneHandle::Mid:    return live.mid && hasMidBand(v);
            case ToneHandle::None:   break;
        }
        return false;
    }

    // The handle under (x, y): the nearest round handle within its grab radius, else a cut bar
    // within its grab width. Round handles win because they sit on the curve the bars cross.
    ToneHandle hitTest(float x, float y, const ToneValues& v, const ToneLive& live) const noexcept
    {
        if (x < x0_ - kBarGrab || x > x1_ + kBarGrab || y < y0_ - kHandleGrab || y > y1_ + kHandleGrab)
            return ToneHandle::None;
        ToneHandle best = ToneHandle::None;
        float bestD2 = kHandleGrab * kHandleGrab;
        for (ToneHandle h : { ToneHandle::Bass, ToneHandle::Mid, ToneHandle::Treble })
        {
            if (! movable(h, v, live))
                continue;
            const TonePoint p = handleAt(h, v);
            const float d2 = (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y);
            if (d2 <= bestD2) { best = h; bestD2 = d2; }
        }
        if (best != ToneHandle::None)
            return best;
        float bestDx = kBarGrab;
        for (ToneHandle h : { ToneHandle::LoCut, ToneHandle::HiCut })
        {
            if (! movable(h, v, live))
                continue;
            const float dx = std::fabs(x - handleAt(h, v).x);
            if (dx <= bestDx) { best = h; bestDx = dx; }
        }
        return best;
    }

    // The values after dragging handle `h` by (dx, dy) design px from where it sat at `start`.
    // `frequencyOnly` holds the level, `levelOnly` the frequency; a dead parameter never moves.
    // Every value lands inside its parameter's range.
    ToneValues dragged(ToneHandle h, const ToneValues& start, float dx, float dy,
                       bool frequencyOnly, bool levelOnly, const ToneLive& live) const noexcept
    {
        ToneValues v = start;
        const TonePoint p = handleAt(h, start);
        const bool moveX = ! levelOnly, moveY = ! frequencyOnly;
        const auto hz = [&](int param) { return clampTo(param, hzOf(p.x + dx)); };
        const auto mult = [&](int param) { return clampTo(param, multOf(p.y + dy)); };
        switch (h)
        {
            case ToneHandle::LoCut:  if (moveX && live.loCut) v.loCut = hz(LoCut); break;
            case ToneHandle::HiCut:  if (moveX && live.hiCut) v.hiCut = hz(HiCut); break;
            case ToneHandle::Bass:
                if (moveX && live.lowX) v.lowX = hz(Crossover);
                if (moveY && live.bass) v.bass = mult(BassMult);
                break;
            case ToneHandle::Treble:
                if (moveX && live.highX) v.highX = hz(HighCrossover);
                if (moveY && live.treble) v.treble = mult(Damping);
                break;
            case ToneHandle::Mid:    if (moveY && live.mid) v.mid = mult(MidMult); break;
            case ToneHandle::None:   break;
        }
        return v;
    }

private:
    static float logMin() noexcept { return std::log(kMinMult); }
    static float logMax() noexcept { return std::log(kMaxMult); }
    static float clampTo(int param, float value) noexcept
    {
        return std::clamp(value, paramDesc(param).min, paramDesc(param).max);
    }
    // A cut below or above the axis (Low Cut reaches down to 5 Hz) sits on the graph's edge.
    float cutX(float hz) const noexcept { return std::clamp(xOf(hz), x0_, x1_); }

    float x0_, y0_, x1_, y1_;
    duskdaf::LogFreqAxis axis_ { 20.0f, 20000.0f };
};

} // namespace duskverb::ui
