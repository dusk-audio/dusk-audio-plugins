// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 as a host builds it into its own binary: DAF's static target with MULTICOMP2_EMBEDDED_UI on every
// source, and the instance reached through MultiCompAccess.hpp. The plug-in is the production class
// (MultiCompPlugin.cpp and DAF's DafPluginMain.cpp are sources of this target, see CMakeLists.txt).
#include "DafPluginInternal.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

static_assert(DAF_UI_DEFAULT_WIDTH == 1120 && DAF_UI_DEFAULT_HEIGHT == 336,
              "a host that embeds the editor opens it at the canvas without the header");

// Heap allocations made while armed: multiCompResetHost() runs on a host's audio thread.
static std::atomic<bool> gCountAllocations{false};
static std::atomic<int> gAllocations{0};

void* operator new(std::size_t size)
{
    if (gCountAllocations.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace
{
using multicompp::ParamId;
constexpr uint32_t pid(ParamId p) { return static_cast<uint32_t>(p); }
constexpr uint32_t kBlock = 256;
constexpr double kSampleRate = 48000.0;

int failures = 0;
void check(bool ok, const char* name)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += ok ? 0 : 1;
}

std::unique_ptr<DAF_NAMESPACE::PluginExporter> makePlugin()
{
    DAF_NAMESPACE::d_nextBufferSize = kBlock;
    DAF_NAMESPACE::d_nextSampleRate = kSampleRate;
    return std::make_unique<DAF_NAMESPACE::PluginExporter>(nullptr, nullptr, nullptr, nullptr);
}

// The editor's face mask: every mode until the host narrows it, an empty mask is every mode again, and it restricts
// nothing but the editor: the Mode parameter and the state take a mode outside it.
void faceMask()
{
    const auto plugin = makePlugin();
    void* const instance = plugin->getInstancePointer();
    const uint32_t initial = multiCompGetHostFaces(instance);
    multiCompSetHostFaces(instance, 0x07u);
    const uint32_t strip = multiCompGetHostFaces(instance);
    multiCompSetHostFaces(instance, 1u << 3);
    const uint32_t bus = multiCompGetHostFaces(instance);
    multiCompSetHostFaces(instance, 1u << 7);
    const uint32_t mastering = multiCompGetHostFaces(instance);
    multiCompSetHostFaces(instance, 0u);
    const uint32_t empty = multiCompGetHostFaces(instance);
    multiCompSetHostFaces(instance, 0x107u);
    const uint32_t extraBit = multiCompGetHostFaces(instance);
    multiCompSetHostFaces(instance, 0xFFFFFF00u);
    const uint32_t noMode = multiCompGetHostFaces(instance);
    std::printf("host faces: default %#x, strip %#x, bus %#x, mastering %#x, empty %#x, 0x107 %#x, 0xFFFFFF00 %#x\n",
                initial, strip, bus, mastering, empty, extraBit, noMode);
    check(initial == 0xFFu, "a new instance offers all eight modes");
    check(strip == 0x07u && bus == 0x08u && mastering == 0x80u, "a face mask reads back as it was set");
    check(empty == 0xFFu && noMode == 0xFFu && extraBit == 0x07u,
          "a mask with none of the eight modes is every mode, and bits past them are dropped");

    multiCompSetHostFaces(nullptr, 0x07u);
    multiCompSetHostChannels(nullptr, 1);
    check(multiCompGetHostFaces(nullptr) == 0xFFu, "no instance: every mode, and the setters do nothing");

    const std::string unmasked = plugin->getStateValue("parameters").buffer();
    multiCompSetHostFaces(instance, 1u << 3);
    const bool sameState = unmasked == plugin->getStateValue("parameters").buffer();
    bool everyMode = true;
    for (int mode = 0; mode < 8; ++mode)
    {
        plugin->setParameterValue(pid(ParamId::Mode), static_cast<float>(mode));
        everyMode = everyMode && plugin->getParameterValue(pid(ParamId::Mode)) == static_cast<float>(mode);
    }
    check(sameState && everyMode, "the mask leaves the state and the Mode parameter alone");
}

struct Run { float peak[2]; bool finite; };

// Blocks of a 1 kHz tone through `channels` main channels and nothing else: every other port is a null pointer, so
// a read or a write past the layout faults.
Run runTone(DAF_NAMESPACE::PluginExporter& plugin, int channels, int blocks)
{
    std::vector<float> in[2], out[2];
    for (int ch = 0; ch < channels; ++ch) { in[ch].resize(kBlock); out[ch].assign(kBlock, 0.0f); }
    const float* inputs[4] = {in[0].data(), channels == 2 ? in[1].data() : nullptr, nullptr, nullptr};
    float* outputs[2] = {out[0].data(), channels == 2 ? out[1].data() : nullptr};
    Run result{{0.0f, 0.0f}, true};
    uint32_t n = 0;
    for (int block = 0; block < blocks; ++block)
    {
        for (uint32_t i = 0; i < kBlock; ++i, ++n)
        {
            const double phase = 2.0 * 3.14159265358979323846 * 1000.0 * n / kSampleRate;
            const float tone = 0.5f * static_cast<float>(std::sin(phase));
            for (int ch = 0; ch < channels; ++ch) in[ch][i] = tone;
        }
        plugin.run(inputs, outputs, kBlock);
        for (int ch = 0; ch < channels; ++ch)
            for (const float sample : out[ch])
            {
                result.finite = result.finite && std::isfinite(sample);
                if (block == blocks - 1) result.peak[ch] = std::max(result.peak[ch], std::fabs(sample));
            }
    }
    return result;
}

// multiCompSetHostChannels: a mono instance reads inputs[0] and writes outputs[0] alone in every mode, with the
// external key requested (a layout without one has none to read), and anything but 1 is stereo.
void hostChannels()
{
    const auto plugin = makePlugin();
    void* const instance = plugin->getInstancePointer();
    plugin->setParameterValue(pid(ParamId::ExternalSidechain), 1.0f);
    plugin->setParameterValue(pid(ParamId::OptoPeakReduction), 80.0f);

    multiCompSetHostChannels(instance, 1);
    for (int mode = 0; mode < 8; ++mode)
    {
        plugin->setParameterValue(pid(ParamId::Mode), static_cast<float>(mode));
        plugin->activate();
        const Run mono = runTone(*plugin, 1, 200);
        const float reduction = multiCompGetGainReduction(instance);
        const bool keyed = multiCompGetExternalSidechainActive(instance) > 0.5f;
        plugin->deactivate();
        std::printf("mono host layout mode=%d: output peak %.4f, gain reduction %.2f dB, external key %d\n", mode,
                    static_cast<double>(mono.peak[0]), static_cast<double>(reduction), keyed);
        check(mono.finite && mono.peak[0] > 0.01f && !keyed, "a mono host layout processes its one channel");
        if (mode == 0)
            check(std::fabs(reduction) > 1.0f && mono.peak[0] < 0.5f, "Opto compresses the mono channel");
    }

    for (const int channels : {2, 0, 7, -1})
    {
        multiCompSetHostChannels(instance, 1);
        multiCompSetHostChannels(instance, channels);
        plugin->setParameterValue(pid(ParamId::Mode), 3.0f);
        plugin->activate();
        const Run stereo = runTone(*plugin, 2, 100);
        const bool keyed = multiCompGetExternalSidechainActive(instance) > 0.5f;
        plugin->deactivate();
        std::printf("host layout %d: output peaks %.4f %.4f, external key %d\n", channels,
                    static_cast<double>(stereo.peak[0]), static_cast<double>(stereo.peak[1]), keyed);
        check(stereo.finite && stereo.peak[0] > 0.01f && stereo.peak[1] > 0.01f && !keyed,
              "any layout but 1 is stereo without a sidechain");
    }
}

struct Rendered { std::vector<float> left, right; };

// Stereo blocks through the instance, every output sample kept: a 1 kHz tone at `level` from phase zero, or silence.
Rendered render(DAF_NAMESPACE::PluginExporter& plugin, float level, int blocks)
{
    std::vector<float> in(kBlock), outLeft(kBlock), outRight(kBlock);
    const float* inputs[4] = {in.data(), in.data(), nullptr, nullptr};
    float* outputs[2] = {outLeft.data(), outRight.data()};
    Rendered result;
    uint32_t n = 0;
    for (int block = 0; block < blocks; ++block)
    {
        for (uint32_t i = 0; i < kBlock; ++i, ++n)
        {
            const double phase = 2.0 * 3.14159265358979323846 * 1000.0 * n / kSampleRate;
            in[i] = level * static_cast<float>(std::sin(phase));
        }
        plugin.run(inputs, outputs, kBlock);
        result.left.insert(result.left.end(), outLeft.begin(), outLeft.end());
        result.right.insert(result.right.end(), outRight.begin(), outRight.end());
    }
    return result;
}

float peakOf(const Rendered& r)
{
    float peak = 0.0f;
    for (const auto* channel : {&r.left, &r.right})
        for (const float sample : *channel) peak = std::max(peak, std::fabs(sample));
    return peak;
}

bool sameSamples(const Rendered& a, const Rendered& b) { return a.left == b.left && a.right == b.right; }

// multiCompResetHost between two run() calls, as a host that stops running a compressor switched off: the instance
// is where activate() puts it, with nothing allocated on the way and the reported latency where it was. Silence and
// a tone after it come out sample for sample as from a freshly activated instance: reset() and prepare() leave the
// same state, so neither comparison has a tolerance. That silence is exact zero from the first sample in FET Black,
// VCA and Bus. Opto at rest gives its own output for silence, 2e-15 at most (-293 dBFS), fresh or reset alike, so
// there the bound is 1e-12. Without the reset the delays still hold the tone.
void hostReset()
{
    static constexpr const char* const names[] = {"Opto", "FET Black", "VCA", "Bus"};
    gAllocations.store(0);
    gCountAllocations.store(true);
    void* (*volatile request)(std::size_t) = ::operator new;
    ::operator delete(request(8));
    gCountAllocations.store(false);
    check(gAllocations.load() == 1, "the allocation counter sees an allocation");
    for (int mode = 0; mode < 4; ++mode)
    {
        const auto engaged = [mode] {
            auto plugin = makePlugin();
            multiCompSetHostChannels(plugin->getInstancePointer(), 2);
            plugin->setParameterValue(pid(ParamId::Mode), static_cast<float>(mode));
            plugin->setParameterValue(pid(ParamId::OptoPeakReduction), 80.0f);
            plugin->activate();
            return plugin;
        };
        const auto hot = [](DAF_NAMESPACE::PluginExporter& plugin) { return render(plugin, 0.9f, 150); };

        const auto reset = engaged();
        void* const instance = reset->getInstancePointer();
        hot(*reset);
        const float reduction = multiCompGetGainReduction(instance);
        check(multiCompGetInputLevel(instance) > -60.0f && multiCompGetOutputLevel(instance) > -60.0f,
              "the tone registers on both level meters before reset");
        const uint32_t latency = reset->getLatency();
        gAllocations.store(0);
        gCountAllocations.store(true);
        multiCompResetHost(instance);
        gCountAllocations.store(false);
        const int allocations = gAllocations.load();
        check(multiCompGetInputLevel(instance) == -60.0f, "host reset immediately clears the input level");
        check(multiCompGetOutputLevel(instance) == -60.0f, "host reset immediately clears the output level");
        const bool atRest = multiCompGetGainReduction(instance) == 0.0f
            && multiCompGetOutputVuLevel(instance) == -120.0f && multiCompGetGainReductionNeedle(instance) == 0.0f
            && reset->getLatency() == latency;
        const Rendered quiet = render(*reset, 0.0f, 20);
        const float afterReset = peakOf(quiet);
        const bool latencyHeld = reset->getLatency() == latency;
        const bool quietAsFresh = sameSamples(quiet, render(*engaged(), 0.0f, 20));

        const auto running = engaged();
        hot(*running);
        const float withoutReset = peakOf(render(*running, 0.0f, 20));

        const auto again = engaged();
        hot(*again);
        multiCompResetHost(again->getInstancePointer());
        const Rendered resumed = render(*again, 0.5f, 60);
        const Rendered fresh = render(*engaged(), 0.5f, 60);
        const bool toneAsFresh = sameSamples(resumed, fresh);

        std::printf("host reset %s: gain reduction %.2f dB, %d allocations, latency %u; silence after it peaks at %g "
                    "(%g without), as a fresh instance's %d; a tone after it as a fresh instance's %d (peak %.4f)\n",
                    names[mode], static_cast<double>(reduction), allocations, latency,
                    static_cast<double>(afterReset), static_cast<double>(withoutReset), quietAsFresh, toneAsFresh,
                    static_cast<double>(peakOf(fresh)));
        check(std::fabs(reduction) > 1.0f, "the instance is compressing when it is reset");
        check(allocations == 0, "multiCompResetHost allocates nothing");
        check(atRest && latencyHeld, "the reset puts the meters at rest and leaves the reported latency");
        check(quietAsFresh && (mode == 0 ? afterReset < 1.0e-12f : afterReset == 0.0f),
              "silence after the reset is a fresh instance's silence from the first sample");
        check(withoutReset > 1.0e-4f, "without the reset the instance still holds the tone");
        check(toneAsFresh && peakOf(fresh) > 0.01f, "a tone after the reset runs as through a fresh instance");
    }
    multiCompResetHost(nullptr);
}
}

int main()
{
    faceMask();
    hostChannels();
    hostReset();
    std::printf("host access failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
