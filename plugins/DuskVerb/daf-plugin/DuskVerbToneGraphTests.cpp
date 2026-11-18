// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The TONE graph's arithmetic (DuskVerbToneGraph.hpp) without a window: the frequency and
// multiplier mapping both ways, the curve against the damping profile it draws, which handle a
// point hits, and where a drag puts each parameter, ranges and dead parameters included.

#include "DuskVerbToneGraph.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace
{
int failures = 0;

void checkf(bool ok, const char* fmt, ...)
{
    if (ok) return;
    va_list args;
    va_start(args, fmt);
    std::printf("FAIL: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
    ++failures;
}

using duskverb::ui::ToneGraph;
using duskverb::ui::ToneHandle;
using duskverb::ui::ToneLive;
using duskverb::ui::ToneValues;

const ToneGraph graph(72.0f, 380.0f, 1128.0f, 470.0f);

ToneValues typical()
{
    ToneValues v;
    v.loCut = 40.0f; v.hiCut = 12000.0f;
    v.bass = 1.5f; v.mid = 1.0f; v.treble = 0.5f;
    v.lowX = 400.0f; v.highX = 4000.0f;
    return v;
}

void testMapping()
{
    for (float hz = 20.0f; hz <= 20000.0f; hz *= 1.3f)
        checkf(std::fabs(graph.hzOf(graph.xOf(hz)) / hz - 1.0f) < 1e-4f, "x round trip at %.1f Hz", hz);
    checkf(std::fabs(graph.xOf(20.0f) - graph.left()) < 1e-3f && std::fabs(graph.xOf(20000.0f) - graph.right()) < 1e-2f,
           "the axis spans 20 Hz to 20 kHz edge to edge");
    for (float m = ToneGraph::kMinMult; m <= ToneGraph::kMaxMult; m *= 1.2f)
        checkf(std::fabs(graph.multOf(graph.yOf(m)) / m - 1.0f) < 1e-4f, "y round trip at x%.3f", m);
    checkf(graph.yOf(1.0f) < graph.yOf(0.5f) && graph.yOf(2.0f) < graph.yOf(1.0f), "longer decay draws higher");
    checkf(graph.yOf(100.0f) == graph.top() && graph.yOf(0.0001f) == graph.bottom(), "the curve clamps to the plot");
}

void testCurve()
{
    const ToneValues v = typical();
    checkf(std::fabs(ToneGraph::multiplierAt(v, 30.0f) - v.bass) < 1e-4f, "deep bass draws Bass Multiply");
    checkf(std::fabs(ToneGraph::multiplierAt(v, 1265.0f) - v.mid) < 1e-4f, "mid band draws Mid Multiply");
    checkf(std::fabs(ToneGraph::multiplierAt(v, 18000.0f) - v.treble) < 1e-4f, "top octave draws Treble Multiply");
    const duskverb::DampingKnobs k { v.bass, v.mid, v.treble, v.lowX, v.highX };
    for (float hz = 20.0f; hz <= 20000.0f; hz *= 1.1f)
        checkf(ToneGraph::multiplierAt(v, hz) == std::exp(duskverb::logDecayProfile(k, hz)),
               "the curve is the damping profile itself at %.0f Hz", hz);
}

void testHitTest()
{
    const ToneValues v = typical();
    const ToneLive live;
    for (ToneHandle h : { ToneHandle::Bass, ToneHandle::Mid, ToneHandle::Treble })
    {
        const auto p = graph.handleAt(h, v);
        checkf(graph.hitTest(p.x, p.y, v, live) == h, "a handle's own centre hits it (%d)", (int)h);
        checkf(graph.hitTest(p.x + 8.0f, p.y - 6.0f, v, live) == h, "a near miss still grabs it (%d)", (int)h);
    }
    const float loX = graph.handleAt(ToneHandle::LoCut, v).x, hiX = graph.handleAt(ToneHandle::HiCut, v).x;
    checkf(graph.hitTest(loX + 3.0f, 460.0f, v, live) == ToneHandle::LoCut, "the Low Cut bar grabs along its length");
    checkf(graph.hitTest(hiX - 3.0f, 400.0f, v, live) == ToneHandle::HiCut, "the High Cut bar grabs along its length");
    checkf(graph.hitTest(600.0f, 380.0f, v, live) == ToneHandle::None, "open space grabs nothing");
    checkf(graph.hitTest(2000.0f, 420.0f, v, live) == ToneHandle::None, "off the graph grabs nothing");

    // A bar crossing a round handle: the handle wins, since it sits on the curve.
    ToneValues crossing = v;
    crossing.loCut = crossing.lowX;
    const auto bass = graph.handleAt(ToneHandle::Bass, crossing);
    checkf(graph.hitTest(bass.x, bass.y, crossing, live) == ToneHandle::Bass, "a round handle wins over a bar");

    // Dead parameters: a handle with nothing live is never hit; one live axis is enough.
    ToneLive deadBass = live;
    deadBass.bass = deadBass.lowX = false;
    const auto b = graph.handleAt(ToneHandle::Bass, v);
    checkf(graph.hitTest(b.x, b.y, v, deadBass) != ToneHandle::Bass, "a dead handle is never hit");
    ToneLive levelOnly = live;
    levelOnly.lowX = false;
    checkf(graph.hitTest(b.x, b.y, v, levelOnly) == ToneHandle::Bass, "a half-live handle still grabs");

    // Crossed crossovers leave no mid band, and no Mid handle to grab.
    ToneValues crossed = v;
    crossed.lowX = 3000.0f; crossed.highX = 2000.0f;
    const auto m = graph.handleAt(ToneHandle::Mid, crossed);
    checkf(graph.hitTest(m.x, m.y, crossed, live) != ToneHandle::Mid, "no mid band, no Mid handle");
}

void testDrag()
{
    const ToneValues v = typical();
    const ToneLive live;
    using namespace duskverb;

    // Small enough to stay inside both ranges (the range ends are checked below).
    const ToneValues moved = graph.dragged(ToneHandle::Bass, v, 60.0f, -8.0f, false, false, live);
    const auto p = graph.handleAt(ToneHandle::Bass, v);
    checkf(std::fabs(moved.lowX - graph.hzOf(p.x + 60.0f)) < 1e-2f, "Bass sideways moves Low Crossover");
    checkf(std::fabs(moved.bass - graph.multOf(p.y - 8.0f)) < 1e-5f, "Bass up raises Bass Multiply");
    checkf(moved.bass > v.bass && moved.lowX > v.lowX, "up and right means longer and higher");
    checkf(moved.mid == v.mid && moved.treble == v.treble && moved.highX == v.highX
           && moved.loCut == v.loCut && moved.hiCut == v.hiCut, "a Bass drag touches nothing else");

    const ToneValues freqOnly = graph.dragged(ToneHandle::Treble, v, -50.0f, 30.0f, true, false, live);
    checkf(freqOnly.treble == v.treble && freqOnly.highX < v.highX, "frequency-only holds the level");
    const ToneValues levelOnly = graph.dragged(ToneHandle::Treble, v, -50.0f, 30.0f, false, true, live);
    checkf(levelOnly.highX == v.highX && levelOnly.treble < v.treble, "level-only holds the frequency");

    const ToneValues mid = graph.dragged(ToneHandle::Mid, v, 200.0f, -10.0f, false, false, live);
    checkf(mid.lowX == v.lowX && mid.highX == v.highX && mid.mid > v.mid, "Mid moves only up and down");

    // Ranges: a drag far past an end parks the parameter on it.
    const ToneValues far = graph.dragged(ToneHandle::Bass, v, -5000.0f, -5000.0f, false, false, live);
    checkf(far.lowX == paramDesc(Crossover).min && far.bass == paramDesc(BassMult).max, "a drag parks on the range ends");
    const ToneValues trebleFar = graph.dragged(ToneHandle::Treble, v, 5000.0f, 5000.0f, false, false, live);
    checkf(trebleFar.highX == paramDesc(HighCrossover).max && trebleFar.treble == paramDesc(Damping).min,
           "Treble parks on its own range ends");
    const ToneValues hiCut = graph.dragged(ToneHandle::HiCut, v, -5000.0f, 0.0f, false, false, live);
    checkf(hiCut.hiCut == paramDesc(HiCut).min, "High Cut parks on its floor");

    // A dead parameter never moves, whatever the drag.
    ToneLive deadX = live;
    deadX.lowX = false;
    const ToneValues held = graph.dragged(ToneHandle::Bass, v, 80.0f, -15.0f, false, false, deadX);
    checkf(held.lowX == v.lowX && held.bass != v.bass, "a dead crossover holds while the live level moves");

    // A Low Cut below the axis sits on its left edge.
    ToneValues sub = v;
    sub.loCut = 5.0f;
    checkf(graph.handleAt(ToneHandle::LoCut, sub).x == graph.left(), "a cut below 20 Hz sits on the edge");
}
} // namespace

int main()
{
    testMapping();
    testCurve();
    testHitTest();
    testDrag();
    if (failures == 0) std::printf("DuskVerbToneGraphTests: all checks passed\n");
    else               std::printf("DuskVerbToneGraphTests: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
