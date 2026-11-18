// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Multi-Comp 2 as a host builds it into its own binary: DAF's static target with MULTICOMP2_EMBEDDED_UI on every
// source, and the instance reached through MultiCompAccess.hpp. The plug-in is the production class
// (MultiCompPlugin.cpp and DAF's DafPluginMain.cpp are sources of this target, see CMakeLists.txt).
#include "DafPluginInternal.hpp"
#include "MultiCompAccess.hpp"
#include "MultiCompParams.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static_assert(DAF_UI_DEFAULT_WIDTH == 1120 && DAF_UI_DEFAULT_HEIGHT == 336,
              "a host that embeds the editor opens it at the canvas without the header");

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
}

int main()
{
    faceMask();
    hostChannels();
    std::printf("host access failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
