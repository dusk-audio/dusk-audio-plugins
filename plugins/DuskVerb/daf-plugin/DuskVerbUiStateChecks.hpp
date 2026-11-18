// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later.
// DuskVerb's real-editor checks, run by the shared CLAP/X11 drag fixture.
#pragma once
#include "../core/DuskVerbParamTable.hpp"
#include "clap/ext/state.h"
#include <X11/keysym.h>
#include <algorithm>

static bool extraUiChecks(Fixture& fx, const clap_plugin_t* plugin,
                          const clap_plugin_params_t* params, Window window,
                          uint32_t width, uint32_t height)
{
    const auto click = [&](float x, float y) {
        Window child; int rx, ry;
        XTranslateCoordinates(fx.display, window, DefaultRootWindow(fx.display),
            int(width * x / 1200), int(height * y / 800), &rx, &ry, &child);
        XSetInputFocus(fx.display, window, RevertToParent, CurrentTime);
        XTestFakeMotionEvent(fx.display, -1, rx, ry, CurrentTime); fx.pump(2);
        XTestFakeButtonEvent(fx.display, 1, True, CurrentTime); fx.pump(2);
        XTestFakeButtonEvent(fx.display, 1, False, CurrentTime); fx.pump(4);
    };
    const auto key = [&](KeySym symbol) {
        const auto code = XKeysymToKeycode(fx.display, symbol);
        XTestFakeKeyEvent(fx.display, code, True, CurrentTime); fx.pump(1);
        XTestFakeKeyEvent(fx.display, code, False, CurrentTime); fx.pump(1);
    };
    const auto value = [&](clap_id id) {
        double result = -999; params->get_value(plugin, id, &result); return result;
    };
    const auto check = [](bool ok, const char* message) {
        if (!ok) std::fprintf(stderr, "FAIL: %s\n", message);
        return ok;
    };
    const auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!check(state != nullptr, "state extension for editor recall check")) return false;
    const auto snapshot = [&]() {
        std::string data;
        clap_ostream_t stream{}; stream.ctx = &data;
        stream.write = [](const clap_ostream_t* stream, const void* bytes, uint64_t count) -> int64_t {
            static_cast<std::string*>(stream->ctx)->append(static_cast<const char*>(bytes), count);
            return static_cast<int64_t>(count);
        };
        if (!state->save(plugin, &stream)) data.clear();
        return data;
    };
    click(569.5f, 24); // first factory preset
    const std::string saved = snapshot();
    const double savedDecay = value(duskverb::Decay);
    if (!check(saved.find("Vocal Plate") != std::string::npos, "factory navigation reaches Vocal Plate")) return false;

    // Parameter automation changes the edited marker, not the complete sound.
    // It must not cancel text the user is still entering.
    click(510, 100);
    key(XK_2); key(XK_period); key(XK_5); key(XK_s);
    clap_event_param_value_t event{};
    event.header.size = sizeof(event); event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.param_id = duskverb::BusMode; event.value = 1;
    event.note_id = event.port_index = event.channel = event.key = -1;
    clap_input_events_t events{}; events.ctx = &event;
    events.size = [](const clap_input_events_t*) -> uint32_t { return 1; };
    events.get = [](const clap_input_events_t* list, uint32_t i) -> const clap_event_header_t* {
        return i == 0 ? &static_cast<clap_event_param_value_t*>(list->ctx)->header : nullptr;
    };
    params->flush(plugin, &events, &fx.collected.iface);
    fx.pump(8); key(XK_Return); fx.pump(4);
    const double typed = duskverb::plainToHost(duskverb::paramDesc(duskverb::Decay), 2.5f);
    if (!check(std::abs(value(duskverb::Decay) - typed) < 1e-6,
               "automation preserves unfinished text; Enter commits 2.50 s")) return false;

    // A complete host restore wins over text typed for the outgoing sound.
    click(569.5f, 24);
    click(510, 100); key(XK_4); key(XK_period); key(XK_2); key(XK_s);
    struct ReadState { const std::string& bytes; size_t position = 0; } source{saved};
    clap_istream_t input{}; input.ctx = &source;
    input.read = [](const clap_istream_t* stream, void* bytes, uint64_t count) -> int64_t {
        auto& in = *static_cast<ReadState*>(stream->ctx);
        const size_t n = std::min<size_t>(count, in.bytes.size() - in.position);
        std::memcpy(bytes, in.bytes.data() + in.position, n); in.position += n;
        return static_cast<int64_t>(n);
    };
    if (!check(state->load(plugin, &input), "host restores the saved preset")) return false;
    fx.pump(8); key(XK_Return); fx.pump(4);
    if (!check(value(duskverb::Decay) == savedDecay,
               "host restore cancels unfinished entry for the previous preset")) return false;

    click(1076, 724); // BUS
    click(1076, 770); // MIX LOCK
    for (int i = 0; i < 24; ++i) click(569.5f, 24);
    if (!check(value(duskverb::Algorithm) == duskverb::kNonLinAlgorithm
               && snapshot().find("Nonlinear Huge") != std::string::npos,
               "all factory sounds, including the nonlinear presets, remain reachable")) return false;
    if (!check(value(duskverb::BusMode) == 1, "locked preset recall preserves Bus")) return false;
    click(611, 24); // INIT
    if (!check(value(duskverb::BusMode) == 1, "locked INIT preserves Bus")) return false;
    click(1076, 770); click(611, 24);
    if (!check(value(duskverb::BusMode) == 0, "unlocked INIT resets Bus")) return false;
    std::printf("PASS: typed entry, automation, host restore, factory navigation and Mix Lock.\n");
    return true;
}
