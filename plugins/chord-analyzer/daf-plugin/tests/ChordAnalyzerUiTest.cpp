// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Real CLAP editor + MIDI host. XTest drives controls and the no-portal save
// form; screenshots and actual exported MIDI prove the UI consumes live data.
#include "ChordMidiExport.hpp"
#include "ChordAnalyzerFileDropTarget.hpp"
#include <clap/entry.h>
#include <clap/plugin.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/timer-support.h>
#include <clap/ext/thread-check.h>
#include <clap/factory/plugin-factory.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <dlfcn.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace duskaudio::chord;
int checks = 0, failures = 0;
clap_id timerId = CLAP_INVALID_ID;
std::atomic<bool> callbackRequested{false};
thread_local bool audioThread = false;
void check(bool ok, const char* what)
{++checks; if (!ok) {++failures; std::fprintf(stderr, "FAIL: %s\n", what);}}
const clap_host_timer_support_t hostTimer {
    [](const clap_host_t*, uint32_t, clap_id* id) {timerId = *id = 1; return true;},
    [](const clap_host_t*, clap_id) {timerId = CLAP_INVALID_ID; return true;}
};
const clap_host_gui_t hostGui {
    [](const clap_host_t*) {}, [](const clap_host_t*, uint32_t, uint32_t) {return false;},
    [](const clap_host_t*) {return false;}, [](const clap_host_t*) {return false;}, [](const clap_host_t*, bool) {}
};
const clap_host_thread_check_t hostThreads {
    [](const clap_host_t*) {return !audioThread;}, [](const clap_host_t*) {return audioThread;}
};
const void* CLAP_ABI extension(const clap_host_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &hostTimer;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &hostGui;
    if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) return &hostThreads;
    return nullptr;
}
clap_host_t host {CLAP_VERSION_INIT, nullptr, "ChordAnalyzerUiTest", "Dusk Audio", "https://dusk-audio.github.io/", "1",
    extension, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {callbackRequested.store(true);}};
struct Events
{
    std::vector<clap_event_param_value_t> values;
    std::vector<clap_event_midi_t> midi;
    clap_input_events_t input {this,
        [](const clap_input_events_t* list) {const auto& e = *static_cast<Events*>(list->ctx); return static_cast<uint32_t>(e.values.size() + e.midi.size());},
        [](const clap_input_events_t* list, uint32_t i) -> const clap_event_header_t* {
            const auto& e = *static_cast<Events*>(list->ctx);
            return i < e.values.size() ? &e.values[i].header : &e.midi[i - e.values.size()].header;
        }};
};
struct Output
{
    std::vector<clap_event_param_value_t> echo;
    unsigned begins = 0, ends = 0;
    clap_output_events_t iface {this, [](const clap_output_events_t* list, const clap_event_header_t* event) {
        auto& e = *static_cast<Output*>(list->ctx);
        if (event->type == CLAP_EVENT_PARAM_VALUE) e.echo.push_back(*reinterpret_cast<const clap_event_param_value_t*>(event));
        if (event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN) ++e.begins;
        if (event->type == CLAP_EVENT_PARAM_GESTURE_END) ++e.ends;
        return true;
    }};
};
midi::Bytes read(const std::filesystem::path& file)
{std::ifstream stream(file, std::ios::binary); return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};}
ChordDescription chord(std::initializer_list<int> notes)
{ChordDescription out; out.isValid = true; out.midiNotes.assign(notes); return out;}

struct Fixture
{
    Display* display = nullptr;
    Window parent = 0;
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_gui_t* gui = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_timer_support_t* timer = nullptr;
    bool midiOnly = false;
    bool echoUiEdits = true;
    Output output;
    std::uint64_t frames = 0;
    std::array<std::array<float, 4096>, 2> inputAudio{}, outputAudio{};
    std::filesystem::path artifacts;
    void audio(Events events = {})
    {
        if (echoUiEdits)
        {events.values.insert(events.values.begin(), output.echo.begin(), output.echo.end()); output.echo.clear();}
        // Event-list pointers must refer to the moved/copy parameter, not a
        // default temporary's `this` stored in its aggregate initializer.
        events.input.ctx = &events;
        float* inputs[] = {inputAudio[0].data(), inputAudio[1].data()};
        float* outputs[] = {outputAudio[0].data(), outputAudio[1].data()};
        clap_audio_buffer_t in{inputs, nullptr, 2, 0, 0}, out{outputs, nullptr, 2, 0, 0};
        clap_event_transport_t transport{};
        transport.header = {sizeof(transport), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
        transport.flags = CLAP_TRANSPORT_HAS_TEMPO; transport.tempo = 180;
        clap_process_t process{};
        process.steady_time = static_cast<int64_t>(frames); process.frames_count = 4096;
        process.transport = &transport;
        process.audio_inputs = midiOnly ? nullptr : &in; process.audio_inputs_count = midiOnly ? 0 : 1;
        process.audio_outputs = midiOnly ? nullptr : &out; process.audio_outputs_count = midiOnly ? 0 : 1;
        process.in_events = &events.input; process.out_events = &output.iface;
        std::thread thread([&] {audioThread = true; plugin->process(plugin, &process);}); thread.join();
        frames += 4096;
    }
    void pump(int count = 4)
    {
        for (int i = 0; i < count; ++i)
        {
            if (callbackRequested.exchange(false)) plugin->on_main_thread(plugin);
            if (timerId != CLAP_INVALID_ID) timer->on_timer(plugin, timerId);
            audio(); XSync(display, False);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    void click(int x, int y)
    {
        XTestFakeMotionEvent(display, -1, x, y, CurrentTime); XSync(display, False); pump(2);
        XTestFakeButtonEvent(display, 1, True, CurrentTime); XSync(display, False); pump(2);
        XTestFakeButtonEvent(display, 1, False, CurrentTime); XSync(display, False); pump(5);
    }
    void key(KeySym symbol)
    {
        const auto code = XKeysymToKeycode(display, symbol);
        XTestFakeKeyEvent(display, code, True, CurrentTime); XTestFakeKeyEvent(display, code, False, CurrentTime);
        XSync(display, False); pump(2);
    }
    void typePath(const std::filesystem::path& file)
    {
        const auto ctrl = XKeysymToKeycode(display, XK_Control_L);
        XTestFakeKeyEvent(display, ctrl, True, CurrentTime); key(XK_a);
        XTestFakeKeyEvent(display, ctrl, False, CurrentTime); pump(2);
        for (unsigned char c : file.string())
        {
            const KeySym symbol = c == ' ' ? XK_space : static_cast<KeySym>(c);
            const auto code = XKeysymToKeycode(display, symbol);
            const bool shift = XkbKeycodeToKeysym(display, code, 0, 0) != symbol;
            const auto shiftCode = XKeysymToKeycode(display, XK_Shift_L);
            if (shift) XTestFakeKeyEvent(display, shiftCode, True, CurrentTime);
            XTestFakeKeyEvent(display, code, True, CurrentTime); XTestFakeKeyEvent(display, code, False, CurrentTime);
            if (shift) XTestFakeKeyEvent(display, shiftCode, False, CurrentTime);
            // ImGui deliberately trickles rapid key down/up transitions across
            // frames. Settle each character so Enter cannot race queued text.
            XSync(display, False); pump(2);
        }
        XSync(display, False); pump(3);
    }
    void moveAway() {XTestFakeMotionEvent(display, -1, 950, 710, CurrentTime); pump(4);}
    void dragPump(FileDropTarget& target, int count)
    {for (int i = 0; i < count; ++i) {pump(1); target.pump();}}
    void startDrag(FileDropTarget& target, int x, int y)
    {
        focus();
        XTestFakeMotionEvent(display, -1, x, y, CurrentTime); XSync(display, False); dragPump(target, 3);
        XTestFakeButtonEvent(display, 1, True, CurrentTime); XSync(display, False); dragPump(target, 3);
        for (int step = 1; step <= 20; ++step)
        {
            XTestFakeMotionEvent(display, -1, x + (1100 - x) * step / 20, y + (150 - y) * step / 20, CurrentTime);
            XSync(display, False); dragPump(target, 2);
        }
    }
    std::filesystem::path dropFile(int x, int y, const midi::Bytes& expected)
    {
        FileDropTarget target(display);
        const Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
        XSetSelectionOwner(display, clipboard, target.window, CurrentTime); XSync(display, False);
        startDrag(target, x, y);
        XTestFakeButtonEvent(display, 1, False, CurrentTime); XSync(display, False); dragPump(target, 20);
        check(target.enters == 1 && target.offersUri && target.positions > 0 && target.copyOnly,
              "real gesture advertises and negotiates a native file-URI copy");
        check(target.drops == 1 && target.transfers == 1, "one real gesture delivers one native file drop");
        const auto file = target.file();
        check(!file.empty() && read(file) == expected, "the destination reads the exact captured MIDI bytes");
        check(target.data.find("%20") != std::string::npos && target.data.find("%23") != std::string::npos
              && target.data.find("%3F") != std::string::npos && target.data.find("%25") != std::string::npos,
              "native URI encoding preserves reserved characters in the actual temporary file path");
        check(XGetSelectionOwner(display, target.selection) == None, "finished transfer releases its native selection");
        check(XGetSelectionOwner(display, clipboard) == target.window, "native dragging preserves the existing clipboard owner");
        return file;
    }
    void focus()
    {
        Window root, owner, *children = nullptr; unsigned count = 0;
        if (XQueryTree(display, parent, &root, &owner, &children, &count) && count)
            XSetInputFocus(display, children[0], RevertToParent, CurrentTime);
        if (children) XFree(children);
        XSync(display, False);
    }
    std::uint64_t pixels(int x, int y, unsigned w, unsigned h)
    {
        XImage* image = XGetImage(display, parent, x, y, w, h, AllPlanes, ZPixmap);
        if (!image) return 0;
        std::uint64_t hash = 14695981039346656037ULL;
        for (unsigned row = 0; row < h; ++row) for (unsigned col = 0; col < w; ++col)
            hash = (hash ^ XGetPixel(image, static_cast<int>(col), static_cast<int>(row))) * 1099511628211ULL;
        XDestroyImage(image); return hash;
    }
    void capture(const char* name)
    {
        moveAway();
        XImage* image = XGetImage(display, parent, 0, 0, 960, 720, AllPlanes, ZPixmap);
        check(image != nullptr, "real editor screenshot is readable"); if (!image) return;
        std::ofstream file(artifacts / name, std::ios::binary);
        file << "P6\n960 720\n255\n";
        for (int y = 0; y < 720; ++y) for (int x = 0; x < 960; ++x)
        {
            const auto p = XGetPixel(image, x, y);
            const char rgb[] = {static_cast<char>(p >> 16), static_cast<char>(p >> 8), static_cast<char>(p)};
            file.write(rgb, 3);
        }
        XDestroyImage(image);
    }
    clap_id id(const char* name)
    {
        for (uint32_t i = 0; i < params->count(plugin); ++i)
        {clap_param_info_t info{}; if (params->get_info(plugin, i, &info) && std::strcmp(info.name, name) == 0) return info.id;}
        return CLAP_INVALID_ID;
    }
    double value(const char* name)
    {double value = -1; check(params->get_value(plugin, id(name), &value), "named host parameter is readable"); return value;}
    std::uint64_t play(std::initializer_list<int> notes)
    {
        Events events;
        const auto add = [&](unsigned a, unsigned b, unsigned c) {
            clap_event_midi_t event{}; event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
            event.data[0] = static_cast<uint8_t>(a); event.data[1] = static_cast<uint8_t>(b); event.data[2] = static_cast<uint8_t>(c);
            events.midi.push_back(event);
        };
        add(0xb0, 120, 0); for (int note : notes) add(0x90, static_cast<unsigned>(note), 100);
        const auto start = frames; audio(std::move(events)); pump(8); return start;
    }
    void saveChord(const std::filesystem::path& destination, bool replace = false)
    {
        click(840, 169); pump(3); typePath(destination); key(XK_Return); pump(3);
        if (replace) click(225, 405);
    }
};

// Independently decode just SMF notes/tempo, accepting running status. The
// dedicated export transcript tests already pin every byte of writer behavior.
struct MidiNote {unsigned tick, pitch; bool on;};
std::vector<MidiNote> parse(const midi::Bytes& bytes, unsigned& tempo)
{
    std::vector<MidiNote> notes;
    if (bytes.size() < 26 || std::memcmp(bytes.data(), "MThd", 4)) return notes;
    std::size_t at = 22; unsigned tick = 0, status = 0;
    const auto var = [&] {
        unsigned value = 0;
        for (int i = 0; i < 4 && at < bytes.size(); ++i)
        {const unsigned b = bytes[at++]; value = (value << 7) | (b & 127); if (!(b & 128)) return value;}
        return value;
    };
    while (at < bytes.size())
    {
        tick += var(); if (at == bytes.size()) break;
        if (bytes[at] & 128) status = bytes[at++];
        if (status == 0xff)
        {
            if (at == bytes.size()) break;
            const auto type = bytes[at++]; const auto n = var(); if (n > bytes.size() - at) break;
            if (type == 0x51 && n == 3) tempo = (unsigned(bytes[at]) << 16) | (unsigned(bytes[at + 1]) << 8) | bytes[at + 2];
            at += n; status = 0;
        }
        else if ((status == 0x90 || status == 0x80) && at + 2 <= bytes.size())
        {notes.push_back({tick, bytes[at], status == 0x90}); at += 2;}
        else break;
    }
    return notes;
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    // Exercise the real no-portal path without opening a portal on the user's
    // desktop bus. All mouse/keyboard input belongs to the isolated X display.
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null", 1);
    Fixture f; f.artifacts = std::filesystem::absolute(argv[2]); std::filesystem::create_directories(f.artifacts);
    const auto dragDirectory = f.artifacts / std::filesystem::u8path("drag temp #?% \xc3\xa9");
    std::filesystem::create_directories(dragDirectory); setenv("TMPDIR", dragDirectory.c_str(), 1);
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry")) : nullptr;
    const bool loaded = entry && entry->init(argv[1]);
    check(loaded, "CLAP binary loads"); if (!loaded) return 1;
    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) return 1;
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
    f.midiOnly = std::strstr(descriptor->id, "midi") != nullptr;
    f.plugin = factory->create_plugin(factory, &host, descriptor->id);
    if (!f.plugin || !f.plugin->init(f.plugin)) return 1;
    f.params = static_cast<const clap_plugin_params_t*>(f.plugin->get_extension(f.plugin, CLAP_EXT_PARAMS));
    f.gui = static_cast<const clap_plugin_gui_t*>(f.plugin->get_extension(f.plugin, CLAP_EXT_GUI));
    f.timer = static_cast<const clap_plugin_timer_support_t*>(f.plugin->get_extension(f.plugin, CLAP_EXT_TIMER_SUPPORT));
    f.display = XOpenDisplay(nullptr);
    check(f.params && f.gui && f.timer && f.display, "GUI, host parameters, timers and isolated DISPLAY exist");
    if (!f.params || !f.gui || !f.timer || !f.display) return 1;
    check(f.plugin->activate(f.plugin, 48000, 1, 4096), "CLAP activates");
    {std::thread t([&] {audioThread = true; check(f.plugin->start_processing(f.plugin), "audio thread starts");}); t.join();}
    check(f.gui->create(f.plugin, CLAP_WINDOW_API_X11, false), "actual editor creates");
    uint32_t width = 0, height = 0; f.gui->get_size(f.plugin, &width, &height);
    check(width == 960 && height == 720, "editor matches design dimensions");
    XSetWindowAttributes attr{}; attr.override_redirect = True;
    f.parent = XCreateWindow(f.display, DefaultRootWindow(f.display), 0, 0, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attr);
    XMapRaised(f.display, f.parent);
    // DAF opens its own X connection; publish this host-owned parent before
    // the plugin creates a child/colormap against its XID.
    XSync(f.display, False);
    clap_window_t window{}; window.api = CLAP_WINDOW_API_X11; window.x11 = f.parent;
    check(f.gui->set_parent(f.plugin, &window) && f.gui->show(f.plugin), "actual editor embeds and shows");
    f.pump(20);
    f.focus();
    f.capture("empty.ppm");
    const auto emptyKeys = f.pixels(20, 248, 506, 156), emptyStaff = f.pixels(538, 248, 402, 156);
    const auto emptyHistory = f.pixels(20, 415, 920, 82), emptyRecording = f.pixels(20, 644, 920, 55);
    f.play({52, 55, 60}); f.capture("inversion.ppm");
    check(f.pixels(20, 248, 506, 156) != emptyKeys && f.pixels(538, 248, 402, 156) != emptyStaff,
          "real MIDI changes both keyboard and staff pixels");
    check(ChordAnalyzerText::decodeLabel(static_cast<int>(f.value("Detected Chord"))) == "C/E", "host observes voiced inversion");
    const auto inversion = f.pixels(36, 127, 710, 53);
    const auto checked = f.pixels(520, 78, 205, 30);
    f.echoUiEdits = false;
    f.click(531, 91); f.moveAway();
    check(f.value("Show Inversions") == 0 && f.pixels(520, 78, 205, 30) != checked,
          "DAF applies the GUI edit locally and the checkbox updates without a host echo");
    f.echoUiEdits = true; f.pump(6);
    check(f.value("Show Inversions") == 0 && f.output.begins == 1 && f.output.ends == 1,
          "checkbox sends a complete host gesture and its value reaches DSP");
    check(ChordAnalyzerText::decodeLabel(static_cast<int>(f.value("Detected Chord"))) == "C"
          && f.pixels(36, 127, 710, 53) != inversion, "inversion option updates host label and rendered title");

    const auto single = f.artifacts / "chord.mid";
    {std::ofstream file(single, std::ios::binary); file << "keep-me";}
    f.saveChord(single); f.capture("overwrite.ppm");
    check(read(single) == midi::Bytes({'k','e','e','p','-','m','e'}), "saving an existing file waits for explicit replacement");
    f.click(724, 405); check(read(single).size() == 7, "Cancel preserves the existing file");
    f.saveChord(single, true);
    check(read(single) == midi::chordBytes(chord({52, 55, 60})), "real Save chord action exports the exact played voicing");
    f.dropFile(120, 150, midi::chordBytes(chord({52,55,60})));
    {
        FileDropTarget rejected(f.display, false);
        f.startDrag(rejected, 120, 150);
        XTestFakeButtonEvent(f.display, 1, False, CurrentTime); XSync(f.display, False); f.dragPump(rejected, 12);
        check(rejected.enters == 1 && rejected.positions > 0 && rejected.drops == 0 && rejected.leaves == 1,
              "a native target rejection cancels the drop without blocking the editor");
        check(XGetSelectionOwner(f.display, rejected.selection) == None, "rejected drops release the selection");
    }
    {
        FileDropTarget cancelled(f.display);
        f.startDrag(cancelled, 120, 150);
        const auto escape = XKeysymToKeycode(f.display, XK_Escape);
        XTestFakeKeyEvent(f.display, escape, True, CurrentTime); XTestFakeKeyEvent(f.display, escape, False, CurrentTime);
        XTestFakeButtonEvent(f.display, 1, False, CurrentTime); XSync(f.display, False); f.dragPump(cancelled, 12);
        check(cancelled.enters == 1 && cancelled.drops == 0 && cancelled.leaves == 1,
              "Escape cancels an accepted native drag before it drops");
        check(XGetSelectionOwner(f.display, cancelled.selection) == None, "Escape releases native drag ownership");
    }
    f.dropFile(120, 150, midi::chordBytes(chord({52,55,60})));

    f.click(80, 675);
    const auto first = f.play({60, 64, 67});
    f.gui->hide(f.plugin); f.gui->destroy(f.plugin); XSync(f.display, False);
    const auto second = f.play({53, 56, 60, 63});
    check(f.gui->create(f.plugin, CLAP_WINDOW_API_X11, false), "editor reopens during an active recording");
    check(f.gui->set_parent(f.plugin, &window) && f.gui->show(f.plugin), "reopened editor reconnects to the live runtime");
    f.pump(8); f.focus();
    const auto third = f.play({55, 59, 62, 65});
    f.click(80, 675); f.capture("recorded.ppm");
    const auto sessionFile = f.artifacts / "progression.mid"; std::filesystem::remove(sessionFile);
    f.click(350, 675); f.typePath(sessionFile); f.key(XK_Return); f.pump(4);
    const auto sessionBytes = read(sessionFile); unsigned tempo = 0; const auto events = parse(sessionBytes, tempo);
    check(tempo == 333333 && events.size() == 22,
          "real Record/Stop/Save retains the chord played with the editor closed at captured 180 BPM");
    const std::array<std::vector<int>, 3> expected {{{60,64,67}, {53,56,60,63}, {55,59,62,65}}};
    const std::array<unsigned, 3> ticks {{0, static_cast<unsigned>(std::nearbyint((second-first) / 48000.0 * 3 * 480)),
        static_cast<unsigned>(std::nearbyint((third-first) / 48000.0 * 3 * 480))}};
    std::size_t on = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) for (int note : expected[i])
    {
        while (on < events.size() && !events[on].on) ++on;
        check(on < events.size() && events[on].pitch == static_cast<unsigned>(note) && events[on].tick == ticks[i],
              "exported progression preserves played pitches and audio-clock-relative beats"); ++on;
    }
    f.moveAway();
    const auto liveKeys = f.pixels(20, 248, 506, 156);
    const auto liveSuggestions = f.pixels(20, 509, 920, 120);
    f.dropFile(520, 675, sessionBytes);
    f.dropFile(42, 460, midi::chordBytes(chord({52,55,60})));
    f.moveAway();
    check(f.pixels(20, 248, 506, 156) == liveKeys && f.pixels(20, 509, 920, 120) == liveSuggestions,
          "a history drag exports its original voicing without becoming a preview click");
    f.click(42, 460); f.capture("preview.ppm");
    check(f.pixels(20, 248, 506, 156) != liveKeys, "history click previews its original note frame");
    check(f.pixels(20, 509, 920, 120) != liveSuggestions, "history preview recomputes suggestions for the previewed chord");
    const auto previewFile = f.artifacts / "preview.mid"; std::filesystem::remove(previewFile);
    f.saveChord(previewFile);
    check(read(previewFile) == midi::chordBytes(chord({52,55,60})), "history export preserves the original voiced chord");
    f.dropFile(120, 150, midi::chordBytes(chord({52,55,60})));
    f.click(840, 208); f.moveAway();
    check(f.pixels(20, 248, 506, 156) == liveKeys, "Return to live safely restores incoming notes");
    check(f.pixels(20, 509, 920, 120) == liveSuggestions, "Return to live restores live harmonic suggestions");
    f.click(875, 433); f.moveAway();
    check(f.pixels(20, 415, 920, 82) == emptyHistory, "Clear history reaches the shared runtime");
    f.click(196, 675); f.moveAway();
    check(f.pixels(20, 644, 920, 55) == emptyRecording, "Clear recording resets event count and export availability");
    f.capture("cleared.ppm");
    f.click(80, 88); f.key(XK_Down); f.key(XK_Return); f.pump(4);
    check(f.value("Key Root") == 1, "key-root selector sends the selected choice to the host");
    f.click(205, 88); f.key(XK_Down); f.key(XK_Return); f.pump(4);
    check(f.value("Key Mode") == 1, "mode selector sends Minor to the host");
    f.click(350, 88); f.key(XK_Up); f.key(XK_Return); f.pump(4);
    check(f.value("Suggestion Level") == 1, "suggestion selector sends the intermediate tier to the host");
    Events held;
    for (unsigned note : {55u, 59u, 62u, 65u})
    {
        clap_event_midi_t event{}; event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        event.data[0] = 0x80; event.data[1] = static_cast<uint8_t>(note); held.midi.push_back(event);
    }
    auto pedal = held.midi.front(); pedal.data[0] = 0xb0; pedal.data[1] = 64; pedal.data[2] = 127;
    held.midi.insert(held.midi.begin(), pedal);
    f.audio(std::move(held)); f.moveAway();
    check(f.pixels(20, 248, 506, 156) == liveKeys, "pedal holds the displayed notes after physical note-offs");
    f.click(741, 91); f.moveAway();
    check(f.value("Respect Sustain") == 0 && f.pixels(20, 248, 506, 156) == emptyKeys,
          "sustain option reaches MIDI processing and releases the displayed deferred keys");
    check(f.output.begins == 5 && f.output.ends == 5, "all five writable controls send balanced host gestures");
    const auto beforeAutomation = f.pixels(24, 75, 120, 27);
    Events automation; clap_event_param_value_t change{};
    change.header = {sizeof(change), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
    change.param_id = f.id("Key Root"); change.note_id = -1; change.port_index = -1; change.channel = -1; change.key = -1; change.value = 7;
    automation.values.push_back(change); f.audio(std::move(automation)); f.pump(8); f.moveAway();
    check(f.value("Key Root") == 7 && f.pixels(24, 75, 120, 27) != beforeAutomation,
          "host automation updates the rendered selector after local editing");
    check(f.output.begins == 5 && f.output.ends == 5, "host automation does not create extra UI gestures");
    {
        FileDropTarget empty(f.display);
        f.startDrag(empty, 120, 150);
        XTestFakeButtonEvent(f.display, 1, False, CurrentTime); XSync(f.display, False); f.dragPump(empty, 5);
        check(empty.enters == 0 && empty.drops == 0 && XGetSelectionOwner(f.display, empty.selection) == None,
              "an empty live display cannot start a native MIDI drag");
    }
    f.play({60,64,67});
    {
        FileDropTarget previewTarget(f.display, true, true);
        f.startDrag(previewTarget, 120, 150); f.dragPump(previewTarget, 12);
        const auto exported = previewTarget.file();
        const auto captured = midi::chordBytes(chord({60,64,67}));
        check(previewTarget.transfers == 1 && !exported.empty() && read(exported) == captured,
              "a native target can inspect the captured file before the button is released");
        f.play({62,65,69}); f.dragPump(previewTarget, 4);
        check(read(exported) == captured, "live note changes do not mutate the active drag's file");
        f.gui->hide(f.plugin); f.gui->destroy(f.plugin); f.dragPump(previewTarget, 5);
        check(previewTarget.drops == 0 && previewTarget.leaves == 1
              && XGetSelectionOwner(f.display, previewTarget.selection) == None,
              "closing the editor cancels the live native drag and releases its selection");
        check(!exported.empty() && read(exported) == captured,
              "the exported MIDI remains readable after its editor closes");
        const int grab = XGrabPointer(f.display, previewTarget.window, False, ButtonReleaseMask,
                                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        check(grab == GrabSuccess, "editor teardown releases the native pointer grab");
        if (grab == GrabSuccess) XUngrabPointer(f.display, CurrentTime);
        XTestFakeButtonEvent(f.display, 1, False, CurrentTime); XSync(f.display, False);
    }
    {std::thread t([&] {audioThread = true; f.plugin->stop_processing(f.plugin);}); t.join();}
    f.plugin->deactivate(f.plugin); f.plugin->destroy(f.plugin);
    XDestroyWindow(f.display, f.parent); XCloseDisplay(f.display); entry->deinit(); dlclose(library);
    std::printf("Chord Analyzer %s UI: %d checks, %d failures\n", f.midiOnly ? "MIDI" : "instrument", checks, failures);
    return failures == 0 ? 0 : 1;
}
