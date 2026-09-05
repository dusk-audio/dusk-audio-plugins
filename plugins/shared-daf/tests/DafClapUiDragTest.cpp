// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Guards the one thing every other gate we run is blind to: that a knob in the
// plugin's own editor still turns.
//
// pluginval runs with --skip-gui-tests, clap-validator never touches the UI, and
// the output-parameter harnesses drive processing with no editor open. So a
// DAF-Widgets bump that broke mouse dragging in every DAF plugin's editor went
// out through four green releases. This harness closes that hole: it opens the
// real editor from the built .clap, drives the real X server with synthetic
// pointer input, and asserts a parameter actually moved.
//
// TWO PHASES, because a drag can be broken in two different ways:
//
//   A. PLAIN DRAG. Press-drag-release on a knob and require an input parameter
//      to move. A UI that ignores the pointer entirely fails here.
//
//   B. DRAG ACROSS A FOCUS CHANGE. Repeat the winning drag, but take keyboard
//      focus away mid-gesture. This is the specific shape of dusk-audio/DAF-
//      Widgets' focus regression: the ImGui backend forwarded every focus-out to
//      io.AddFocusEvent(false), and ImGui answers that by calling
//      ClearInputMouse() at end of frame, dropping the held button. An editor
//      embedded in a DAW does not own keyboard focus the way a standalone window
//      does, so focus-out arrives during ordinary use and the drag dies at the
//      point the user starts moving. Phase A alone would not have caught it.
//
// The assertion is what a HOST would have been told, not what was drawn and not
// what get_value() reads back. A CLAP editor does not write its own DSP-side
// values: it reports edits to the host, which feeds them back in. In a bare
// harness nothing feeds them back, so get_value() sits still however far a knob
// is dragged, and asserting on it would call every working editor broken. This
// counts the CLAP_EVENT_PARAM_VALUE events the plugin pushes out instead, which
// is the same evidence a DAW acts on.
//
// The knob's position comes in on the command line rather than being hunted for.
// An earlier version swept a grid looking for anything that responded, which is
// tempting because it needs no per-plugin knowledge, but it is the wrong trade:
// a blind sweep eventually presses SAVE and blocks the run on a native file
// dialog, or presses INIT and reads the parameter reset as a successful drag.
// One coordinate per plugin costs a line in its CMakeLists and keeps the gesture
// aimed at something that is unambiguously a knob. If a redesign moves it, phase
// A fails with a message naming the coordinate to update.
//
// Set DUSK_UI_DRAG_DUMP=<file.ppm> to write out what the editor looked like,
// which is how you find the coordinate in the first place.
//
// Linux only: dlopen plus X11 plus XTest. That is also where CI runs the DAF
// gate suite. XTest drives the server the way real hardware does rather than
// pushing synthetic events at a window, so pugl sees ordinary input and nothing
// here depends on a toolkit honouring SendEvent.
//
// RUN IT HEADLESS, even on a workstation that has a display:
//
//     Xvfb :99 -screen 0 1600x1200x24 &
//     DISPLAY=:99 ctest --test-dir build -R UiDrag
//
// XTest delivers by screen position, and this harness's window is
// override-redirect and therefore unmanaged, so anything a desktop session puts
// on top of it takes the gesture instead. A GNOME Wayland session does exactly
// that: synthetic input raises a Remote Desktop permission prompt, and until
// someone approves it every run fails identically, blaming the plugin. The gate
// now detects that case and says so, but a headless display avoids it, matches
// CI, and does not fight the user for the pointer.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include "clap/entry.h"
#include "clap/plugin.h"
#include "clap/ext/gui.h"
#include "clap/ext/params.h"
#include "clap/ext/timer-support.h"
#include "clap/factory/plugin-factory.h"

// --------------------------------------------------------------------------------------------------------------------
// minimal host
//
// Unlike the output-parameter harnesses this host cannot answer every extension
// query with nullptr. DAF's CLAP editor is pumped exclusively by the host
// calling clap_plugin_timer_support.on_timer, which drives ClapUI::idleCallback
// and through it pugl's event loop (DafPluginCLAP.cpp). Without timer support
// registered the editor would never process a single pointer event and this
// harness would report a dead UI for every plugin, broken or not.

static clap_id gTimerId = CLAP_INVALID_ID;

static bool CLAP_ABI hostTimerRegister(const clap_host_t*, const uint32_t /* periodMs */, clap_id* const timerId)
{
    gTimerId = 1;
    *timerId = gTimerId;
    return true;
}

static bool CLAP_ABI hostTimerUnregister(const clap_host_t*, const clap_id timerId)
{
    if (timerId == gTimerId)
        gTimerId = CLAP_INVALID_ID;
    return true;
}

static const clap_host_timer_support_t gHostTimer = { hostTimerRegister, hostTimerUnregister };

// DAF asserts on a host with no CLAP_EXT_GUI before it will create an editor
// (DafPluginCLAP.cpp), so this is required rather than optional. Nothing here
// has to act on the callbacks: the editor is parented into a fixed-size window
// that no one resizes, and a plugin asking to be shown, hidden or closed during
// a scripted drag is not a request this harness needs to honour.
static void CLAP_ABI hostGuiResizeHintsChanged(const clap_host_t*) {}
static bool CLAP_ABI hostGuiRequestResize(const clap_host_t*, uint32_t, uint32_t) { return false; }
static bool CLAP_ABI hostGuiRequestShow(const clap_host_t*) { return false; }
static bool CLAP_ABI hostGuiRequestHide(const clap_host_t*) { return false; }
static void CLAP_ABI hostGuiRequestClosed(const clap_host_t*, bool) {}

static const clap_host_gui_t gHostGui = {
    hostGuiResizeHintsChanged, hostGuiRequestResize,
    hostGuiRequestShow, hostGuiRequestHide, hostGuiRequestClosed
};

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* const id)
{
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
        return &gHostTimer;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0)
        return &gHostGui;
    return nullptr;
}

static void CLAP_ABI hostRequestRestart(const clap_host_t*) {}
static void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
static void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

static clap_host_t gHostImpl = {
    CLAP_VERSION_INIT, nullptr,
    "DafClapUiDragTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback
};

// --------------------------------------------------------------------------------------------------------------------

static void sleepMs(const long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

namespace {

struct Param {
    clap_id id;
    std::string name;
    std::string module;
    uint32_t flags;
};

// DAF stamps CLAP_PARAM_IS_READONLY on output parameters and on the Reset
// designation. Reset is a trigger a drag must never be credited with moving, and
// it is separated by its module rather than by flags, exactly as
// DafClapOutputParamTest does. Keep the two rules identical: if one is wrong,
// both should be wrong in the same visible way rather than disagreeing.
bool isOutput(const Param& p)
{
    return (p.flags & CLAP_PARAM_IS_READONLY) != 0 && p.module != "daf_reset";
}

// What the host would receive from the editor. A CLAP plugin's UI does not write
// its own DSP-side parameter values: it tells the host, and the host feeds the
// change back in. So clap_plugin_params.get_value() stays put no matter how far
// a knob is dragged in a bare harness like this one, and asserting on it would
// report every working editor as broken. The events below are the actual
// evidence that a knob turned.
struct OutEvents {
    clap_output_events_t iface;
    std::vector<clap_id> paramValueIds;
    uint32_t gestureBegins = 0;

    OutEvents()
    {
        iface.ctx = this;
        iface.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t* const list, const clap_event_header_t* const event)
    {
        OutEvents* const self = static_cast<OutEvents*>(list->ctx);
        if (event->type == CLAP_EVENT_PARAM_VALUE)
            self->paramValueIds.push_back(((const clap_event_param_value_t*) event)->param_id);
        else if (event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
            ++self->gestureBegins;
        return true;
    }
};

struct EmptyInEvents {
    clap_input_events_t iface;

    EmptyInEvents()
    {
        iface.ctx = this;
        iface.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
        iface.get  = [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; };
    }
};

struct Fixture {
    const clap_plugin_t* plugin = nullptr;
    Window parent = None;              // the editor's host window
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_timer_support_t* timer = nullptr;
    Display* display = nullptr;
    std::vector<Param> inputs;
    OutEvents collected;

    // One "frame": give the editor a timer tick, then let the server settle. The
    // tick is what makes pugl read the events XTest just generated, so every
    // synthetic action must be followed by frames or it is never seen.
    void pump(const int frames = 4)
    {
        EmptyInEvents in;
        for (int i = 0; i < frames; ++i)
        {
            if (timer != nullptr && gTimerId != CLAP_INVALID_ID)
                timer->on_timer(plugin, gTimerId);

            // Drain whatever the editor wants to tell the host. A real host does
            // this from process(); flush() is the documented way to do it while
            // the plugin is not processing, which is the case here.
            params->flush(plugin, &in.iface, &collected.iface);

            XSync(display, False);
            sleepMs(16);
        }
    }

    // Pump frames until the editor has done something observable, rather than
    // for a fixed number of frames. A warm editor on an idle machine answers on
    // the first frame; a loaded one takes longer and is not wrong for it.
    template <typename Ready>
    bool pumpUntil(Ready ready, const int timeoutMs = 512)
    {
        // Measured against the clock, not counted in frames. A frame here is a
        // timer tick, a flush, an XSync and a 16 ms sleep, so on a loaded
        // machine it overruns its nominal 16 ms and a frame count would let the
        // wait run several times longer than the bound it advertises.
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        const auto elapsedMs = [&start]() {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            return (now.tv_sec - start.tv_sec) * 1000L
                 + (now.tv_nsec - start.tv_nsec) / 1000000L;
        };

        do
        {
            pump(1);
            if (ready()) return true;
        } while (elapsedMs() < timeoutMs);
        return false;
    }

    std::vector<double> snapshot() const
    {
        std::vector<double> values;
        values.reserve(inputs.size());
        for (const Param& p : inputs)
        {
            double v = 0.0;
            params->get_value(plugin, p.id, &v);
            values.push_back(v);
        }
        return values;
    }
};

struct DragResult {
    size_t edits = 0;           // parameter edits the host would have received
    size_t midEdits = 0;        // how many had arrived halfway through the gesture
    uint32_t gestureBegins = 0; // begin-edit gestures seen
    bool pointerBlocked = false;// another window sat above the editor

    // A knob follows the pointer, so edits arrive throughout the gesture. A
    // button, a preset step or an INIT produces one burst at the press. Requiring
    // edits both at the halfway point and after it keeps "the drag works" from
    // being satisfiable by a click.
    bool looksLikeADrag() const
    {
        return midEdits > 0 && edits > midEdits;
    }
};

// Which top-level window the pointer is actually over. XTest events are
// delivered by position, not by addressee, so this is the difference between
// driving the editor and driving whatever happens to be stacked above it.
Window topLevelUnderPointer(Display* const display)
{
    const Window root = DefaultRootWindow(display);
    Window reportedRoot = None, child = None;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    unsigned int mask = 0;
    if (! XQueryPointer(display, root, &reportedRoot, &child, &rootX, &rootY, &winX, &winY, &mask))
        return None;
    return child;
}

// Put the editor under the pointer and keep it there. Under Xvfb, where this
// gate runs in CI, nothing else is on screen and this is a no-op. On a desktop
// the test window is override-redirect and unmanaged, so anything the user (or
// the compositor) raises afterwards sits above it and silently swallows every
// synthetic click -- which reads as "the editor ignores input" for the whole
// process, retry included. Raising it back is cheap and turns an invisible
// environment failure into either a pass or a specific diagnostic.
bool raiseEditorUnderPointer(Fixture& fx, const Window parent, const int rootX, const int rootY)
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        XTestFakeMotionEvent(fx.display, -1, rootX, rootY, CurrentTime);
        XSync(fx.display, False);
        if (topLevelUnderPointer(fx.display) == parent)
            return true;

        XRaiseWindow(fx.display, parent);
        XSync(fx.display, False);
        fx.pump(1);
    }
    return topLevelUnderPointer(fx.display) == parent;
}

DragResult dragAt(Fixture& fx, const int rootX, const int rootY, const int stepDy, const Window focusThief)
{
    auto countFor = [&fx]() {
        // Only edits to input parameters count. Output parameters are meters and
        // must never be reported as edits at all -- that is what
        // DafClapOutputParamTest guards -- so crediting one here would let this
        // harness pass on a plugin whose knobs are dead but whose meters move.
        size_t n = 0;
        for (const clap_id id : fx.collected.paramValueIds)
            for (const Param& p : fx.inputs)
                if (p.id == id) { ++n; break; }
        return n;
    };

    fx.collected.paramValueIds.clear();
    fx.collected.gestureBegins = 0;

    const bool onTop = raiseEditorUnderPointer(fx, fx.parent, rootX, rootY);
    fx.pump();

    XTestFakeButtonEvent(fx.display, 1, True, CurrentTime);
    // The press is what the rest of the gesture hangs off, so wait for the
    // editor to acknowledge it instead of assuming four frames is enough.
    fx.pumpUntil([&] { return fx.collected.gestureBegins > 0 || countFor() > 0; });

    const int steps = 12;
    size_t midCount = 0;
    for (int i = 1; i <= steps; ++i)
    {
        if (focusThief != None && i == steps / 3)
        {
            XSetInputFocus(fx.display, focusThief, RevertToParent, CurrentTime);
            fx.pump();
        }

        XTestFakeMotionEvent(fx.display, -1, rootX, rootY + i * stepDy, CurrentTime);
        fx.pump(2);

        if (i == steps / 2)
            midCount = countFor();
    }

    XTestFakeButtonEvent(fx.display, 1, False, CurrentTime);
    fx.pump();

    DragResult result;
    // Checked at both ends of the gesture. Before, because a window that was
    // already above the editor takes the press; after, because one that raises
    // itself mid-gesture (a permission prompt appearing, a compositor putting a
    // dialog back on top) takes the rest of it, and a snapshot from before the
    // drag would have said everything was fine.
    //
    // The second check asks at the coordinate the gesture STARTED from, not
    // wherever it ended. A drag of 12 steps runs 72 px past its origin, so a
    // coordinate near the bottom of the editor ends with the pointer over the
    // root window, and asking there reports "something is above the editor"
    // for what is really a coordinate aimed off the knob -- replacing the one
    // message that would have said so.
    XTestFakeMotionEvent(fx.display, -1, rootX, rootY, CurrentTime);
    XSync(fx.display, False);
    result.pointerBlocked = ! onTop || topLevelUnderPointer(fx.display) != fx.parent;
    result.edits         = countFor();
    result.midEdits      = midCount;
    result.gestureBegins = fx.collected.gestureBegins;
    return result;
}

// How many distinct colours the editor is showing. A window that never draws is
// one flat colour, and a window that draws its background and nothing else is
// two -- which is exactly what a broken GL path produces, and exactly what this
// harness used to accept: the drag phases assert that parameters move, and an
// editor can answer pointer input perfectly while painting nothing.
//
// Sampled on a grid rather than per pixel: the question is "is there a UI here",
// and 8 distinct colours separates a real faceplate (thousands) from a blank or
// background-only window (one or two) with room to spare.
//
// kCaptureFailed is distinct from a count, because "X would not give us the
// pixels" and "the pixels are all one colour" are different findings and only
// the second one is about the plugin.
constexpr int kCaptureFailed = -1;

int distinctColours(Display* const display, const Window window,
                    const unsigned width, const unsigned height,
                    const unsigned cap)
{
    XImage* const img = XGetImage(display, window, 0, 0, width, height, AllPlanes, ZPixmap);
    if (img == nullptr)
        return kCaptureFailed;

    unsigned long seen[64];
    unsigned count = 0;
    for (unsigned y = 0; y < height && count < cap; y += 8)
        for (unsigned x = 0; x < width && count < cap; x += 8)
        {
            const unsigned long px = XGetPixel(img, (int) x, (int) y);
            bool known = false;
            for (unsigned i = 0; i < count; ++i)
                if (seen[i] == px) { known = true; break; }
            if (! known && count < 64)
                seen[count++] = px;
        }

    XDestroyImage(img);
    return (int) count;
}

// The editor renders into the child window the plugin creates under the host
// parent, so sample that directly rather than relying on the parent's capture
// including its inferior. In this fleet both are the same size and depth (24)
// and either works, but the child is the drawable whose contents are actually
// defined, and if a plugin ever presents a different visual there this keeps
// asking the right window.
Window renderedChildOf(Display* const display, const Window parent)
{
    Window root = None, up = None, *children = nullptr;
    unsigned int count = 0;
    if (! XQueryTree(display, parent, &root, &up, &children, &count))
        return None;

    Window found = None;
    for (unsigned int i = 0; i < count && found == None; ++i)
    {
        XWindowAttributes wa = {};
        if (XGetWindowAttributes(display, children[i], &wa) && wa.map_state == IsViewable)
            found = children[i];
    }
    if (children != nullptr)
        XFree(children);
    return found;
}

// Every failure below this point accuses the plugin of something specific: a
// dead input path, a coordinate aimed at a button, or the DAF-Widgets focus
// regression. None of those conclusions survive the pointer having been
// somewhere else, so each phase asks this first. Under Xvfb, where CI runs,
// nothing else is on screen and this never fires.
bool reportInterception(const DragResult& result, const char* const phase,
                        const int knobX, const int knobY)
{
    if (! result.pointerBlocked)
        return false;

    std::fprintf(stderr,
                 "\nFAIL (environment, during %s): another window was above the editor at (%d,%d),\n"
                 "  so the synthetic clicks went to it and not to the plugin. This says nothing\n"
                 "  about the plugin.\n"
                 "  On a GNOME Wayland session the usual culprit is the Remote Desktop\n"
                 "  permission prompt that XTest raises: until it is approved it sits above\n"
                 "  the editor and eats the gesture, and every run fails identically.\n"
                 "  Run the gate on a headless display instead, which is also what CI does:\n"
                 "    Xvfb :99 -screen 0 1600x1200x24 &  DISPLAY=:99 ctest -R UiDrag\n",
                 phase, knobX, knobY);
    return true;
}

} // namespace

int main(const int argc, const char* const* const argv)
{
    std::vector<const char*> positional;
    for (int i = 1; i < argc; ++i)
        positional.push_back(argv[i]);

    if (positional.size() < 3)
    {
        std::fprintf(stderr,
                     "usage: %s <plugin.clap> <knobX> <knobY>\n"
                     "  knobX/knobY: centre of a knob in editor coordinates. Dump the editor with\n"
                     "  DUSK_UI_DRAG_DUMP=/tmp/editor.ppm to find one.\n", argv[0]);
        return 2;
    }

    const std::string path = positional[0];
    const int knobX = std::atoi(positional[1]);
    const int knobY = std::atoi(positional[2]);

    // ---- load ---------------------------------------------------------------

    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }

    const clap_plugin_entry_t* const entry = (const clap_plugin_entry_t*) dlsym(lib, "clap_entry");
    if (entry == nullptr || ! entry->init(path.c_str()))
    {
        std::fprintf(stderr, "FAIL: no usable clap_entry in %s\n", path.c_str());
        return 1;
    }

    const clap_plugin_factory_t* const factory =
        (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (factory == nullptr || factory->get_plugin_count(factory) == 0)
    {
        std::fprintf(stderr, "FAIL: no plugin factory\n");
        return 1;
    }

    const clap_plugin_descriptor_t* const desc = factory->get_plugin_descriptor(factory, 0);
    const clap_plugin_t* const plugin = factory->create_plugin(factory, &gHostImpl, desc->id);
    if (plugin == nullptr || ! plugin->init(plugin))
    {
        std::fprintf(stderr, "FAIL: create/init plugin\n");
        return 1;
    }

    const clap_plugin_params_t* const paramsExt =
        (const clap_plugin_params_t*) plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    const clap_plugin_gui_t* const guiExt =
        (const clap_plugin_gui_t*) plugin->get_extension(plugin, CLAP_EXT_GUI);
    const clap_plugin_timer_support_t* const timerExt =
        (const clap_plugin_timer_support_t*) plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);

    if (paramsExt == nullptr || guiExt == nullptr || timerExt == nullptr)
    {
        std::fprintf(stderr, "FAIL: plugin lacks params, gui or timer-support extension\n");
        return 1;
    }

    Fixture fx;
    fx.plugin = plugin;
    fx.params = paramsExt;
    fx.timer  = timerExt;

    const uint32_t paramCount = paramsExt->count(plugin);
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        clap_param_info_t info = {};
        if (! paramsExt->get_info(plugin, i, &info))
        {
            std::fprintf(stderr, "FAIL: get_info(%u)\n", i);
            return 1;
        }
        const Param p = { info.id, info.name, info.module, info.flags };
        if (! isOutput(p))
            fx.inputs.push_back(p);
    }

    if (fx.inputs.empty())
    {
        std::fprintf(stderr, "FAIL: plugin exposes no input parameters; this test would pass vacuously\n");
        return 1;
    }

    // ---- display ------------------------------------------------------------

    fx.display = XOpenDisplay(nullptr);
    if (fx.display == nullptr)
    {
        std::fprintf(stderr, "FAIL: XOpenDisplay: no DISPLAY. Run this under xvfb-run.\n");
        return 1;
    }

    int xtestEventBase = 0, xtestErrorBase = 0, xtestMajor = 0, xtestMinor = 0;
    if (! XTestQueryExtension(fx.display, &xtestEventBase, &xtestErrorBase, &xtestMajor, &xtestMinor))
    {
        std::fprintf(stderr, "FAIL: X server has no XTEST extension; cannot synthesise input\n");
        return 1;
    }

    if (! guiExt->is_api_supported(plugin, CLAP_WINDOW_API_X11, false))
    {
        std::fprintf(stderr, "FAIL: plugin does not support the x11 gui api\n");
        return 1;
    }

    if (! guiExt->create(plugin, CLAP_WINDOW_API_X11, false))
    {
        std::fprintf(stderr, "FAIL: gui->create\n");
        return 1;
    }

    uint32_t width = 0, height = 0;
    if (! guiExt->get_size(plugin, &width, &height) || width == 0 || height == 0)
    {
        std::fprintf(stderr, "FAIL: gui->get_size\n");
        return 1;
    }

    Screen* const screen = DefaultScreenOfDisplay(fx.display);
    const Window root = RootWindowOfScreen(screen);

    // Position at the origin so root coordinates and editor coordinates differ by
    // a known offset. Nothing here runs under a window manager, so the request is
    // what we get; override_redirect says so explicitly rather than relying on it.
    XSetWindowAttributes attr = {};
    attr.override_redirect = True;
    attr.event_mask = StructureNotifyMask;
    const Window parent = XCreateWindow(fx.display, root, 0, 0, width, height, 0,
                                        CopyFromParent, InputOutput, CopyFromParent,
                                        CWOverrideRedirect | CWEventMask, &attr);
    XMapRaised(fx.display, parent);
    fx.parent = parent;

    // The thief only has to be focusable; it is never drawn over the editor.
    const Window thief = XCreateWindow(fx.display, root, (int) width + 32, 0, 32, 32, 0,
                                       CopyFromParent, InputOutput, CopyFromParent,
                                       CWOverrideRedirect | CWEventMask, &attr);
    XMapRaised(fx.display, thief);
    XSync(fx.display, False);

    clap_window_t hostWindow = {};
    hostWindow.api = CLAP_WINDOW_API_X11;
    hostWindow.x11 = (uintptr_t) parent;
    if (! guiExt->set_parent(plugin, &hostWindow))
    {
        std::fprintf(stderr, "FAIL: gui->set_parent\n");
        return 1;
    }

    if (! guiExt->show(plugin))
    {
        std::fprintf(stderr, "FAIL: gui->show\n");
        return 1;
    }

    // Let the editor come up and reach a steady state before anything is clicked.
    fx.pump(30);

    // DUSK_UI_DRAG_DEBUG dumps what the X server actually thinks exists. When a
    // drag moves nothing, the first question is always whether the editor's own
    // window is there, mapped and under the pointer, and guessing at that costs
    // far more than printing it.
    if (std::getenv("DUSK_UI_DRAG_DEBUG") != nullptr)
    {
        Window dummyRoot = None, dummyParent = None;
        Window* children = nullptr;
        unsigned int childCount = 0;
        if (XQueryTree(fx.display, parent, &dummyRoot, &dummyParent, &children, &childCount))
        {
            std::printf("debug       : editor window has %u child window(s) under the host parent\n", childCount);
            for (unsigned int i = 0; i < childCount; ++i)
            {
                XWindowAttributes wa = {};
                XGetWindowAttributes(fx.display, children[i], &wa);
                std::printf("debug       :   child 0x%lx %dx%d at (%d,%d) map_state=%d\n",
                            (unsigned long) children[i], wa.width, wa.height, wa.x, wa.y, wa.map_state);
            }
            if (children != nullptr)
                XFree(children);
        }

        std::printf("debug       : plugin registered a host timer: %s (id=%u)\n",
                    gTimerId != CLAP_INVALID_ID ? "yes" : "NO -- nothing is pumping the editor",
                    gTimerId);

        XWindowAttributes pa = {};
        XGetWindowAttributes(fx.display, parent, &pa);
        std::printf("debug       : host parent %dx%d at (%d,%d) map_state=%d\n",
                    pa.width, pa.height, pa.x, pa.y, pa.map_state);

        // Is anything actually being drawn? An editor that never renders has no
        // ImGui widget rectangles to hit-test against, and every drag would miss
        // for a reason that has nothing to do with input delivery.
        if (XImage* const img = XGetImage(fx.display, parent, 0, 0, width, height, AllPlanes, ZPixmap))
        {
            unsigned long first = XGetPixel(img, 0, 0);
            bool uniform = true;
            for (unsigned int y = 0; y < height && uniform; y += 8)
                for (unsigned int x = 0; x < width && uniform; x += 8)
                    if (XGetPixel(img, (int) x, (int) y) != first)
                        uniform = false;
            std::printf("debug       : editor pixels %s (first pixel 0x%lx)\n",
                        uniform ? "UNIFORM -- nothing is being rendered" : "vary -- the editor is drawing",
                        first);

            // DUSK_UI_DRAG_DUMP writes the editor as a PPM so a human (or a
            // model) can look at what the harness is dragging on. Worth keeping:
            // "the grid missed every knob" and "input never arrived" produce the
            // same failure message, and only a picture tells them apart.
            if (const char* const dump = std::getenv("DUSK_UI_DRAG_DUMP"))
            {
                if (FILE* const f = std::fopen(dump, "wb"))
                {
                    std::fprintf(f, "P6\n%u %u\n255\n", width, height);
                    for (unsigned int y = 0; y < height; ++y)
                        for (unsigned int x = 0; x < width; ++x)
                        {
                            const unsigned long px = XGetPixel(img, (int) x, (int) y);
                            const unsigned char rgb[3] = {
                                (unsigned char) ((px >> 16) & 0xff),
                                (unsigned char) ((px >> 8) & 0xff),
                                (unsigned char) (px & 0xff)
                            };
                            std::fwrite(rgb, 1, 3, f);
                        }
                    std::fclose(f);
                    std::printf("debug       : wrote %s\n", dump);
                }
            }

            XDestroyImage(img);
        }
    }

    std::printf("plugin      : %s\n", path.c_str());
    std::printf("editor      : %ux%u\n", width, height);
    std::printf("parameters  : %u (%zu input)\n", paramCount, fx.inputs.size());

    // ---- phase A: does the knob answer a drag at all? -----------------------

    // DUSK_UI_DRAG_HOLD=<seconds> keeps the editor open and pumping so another
    // process can inspect it (xwininfo, an XI2 selection probe, a screenshot).
    if (const char* const holdFor = std::getenv("DUSK_UI_DRAG_HOLD"))
    {
        const int seconds = std::atoi(holdFor);
        std::printf("debug       : holding the editor open for %ds (window 0x%lx)\n",
                    seconds, (unsigned long) parent);
        std::fflush(stdout);
        for (int i = 0; i < seconds * 60; ++i)
            fx.pump(1);
    }

    // Dragging up first, then down if that did not produce a clean gesture. A
    // knob already near the top of its range reports a few edits and then stops
    // when it saturates, which reads as "not progressive" rather than as silence,
    // so both outcomes have to fall through to the retry. Sunset Circuits' cutoff
    // knob does exactly this from its default preset.
    // ---- is there a UI at all? ---------------------------------------------
    //
    // Asserted before the drag, because every assertion after it is about input
    // and none of them notices an editor that paints nothing. A pugl or DGL
    // change that breaks the GL path leaves the drag working perfectly -- the
    // widgets are laid out, they hit-test, they emit parameter edits -- while
    // the user sees a blank window. That shipped past this harness once already
    // during a pugl rebase trial: all four gates green, two colours on screen.
    // Sample the editor's own window when it has one; fall back to the host
    // parent, which is what a plugin that draws directly into it presents.
    const Window drawn = renderedChildOf(fx.display, parent);
    const Window sampled = drawn != None ? drawn : parent;

    int colours = 0;
    fx.pumpUntil([&] {
        colours = distinctColours(fx.display, sampled, width, height, 8);
        return colours >= 8 || colours == kCaptureFailed;
    }, 3000);

    if (colours == kCaptureFailed)
    {
        // Not a verdict on the plugin: X refused to hand over the pixels, so
        // there is nothing to judge. Reported separately so it cannot be read
        // as "the editor is blank".
        XWindowAttributes pa = {}, ca = {};
        XGetWindowAttributes(fx.display, parent, &pa);
        if (drawn != None)
            XGetWindowAttributes(fx.display, drawn, &ca);
        std::fprintf(stderr,
                     "\nFAIL (environment): XGetImage could not capture the editor window 0x%lx\n"
                     "  (%dx%d depth %d; host parent 0x%lx %dx%d depth %d). Nothing here says whether\n"
                     "  the plugin draws -- the capture itself failed. An unmapped or resized window,\n"
                     "  or a visual this server will not read back, are the usual causes.\n",
                     (unsigned long) sampled, drawn != None ? ca.width : pa.width,
                     drawn != None ? ca.height : pa.height, drawn != None ? ca.depth : pa.depth,
                     (unsigned long) parent, pa.width, pa.height, pa.depth);
        return 1;
    }

    if (colours < 8)
    {
        std::fprintf(stderr,
                     "\nFAIL: the editor is not drawing. After three seconds of frames window 0x%lx is\n"
                     "  showing %d distinct colour(s); a drawn faceplate has thousands. The drag phases\n"
                     "  below would still pass, because widgets hit-test and emit parameter edits\n"
                     "  whether or not anything reaches the screen -- so this is checked first. Look at\n"
                     "  it with DUSK_UI_DRAG_DUMP=/tmp/editor.ppm; a framework bump is the usual cause.\n",
                     (unsigned long) sampled, colours);
        return 1;
    }

    int hitStep = -6;
    DragResult phaseA = dragAt(fx, knobX, knobY, hitStep, None);
    if (phaseA.edits == 0 || ! phaseA.looksLikeADrag())
    {
        hitStep = 6;
        phaseA  = dragAt(fx, knobX, knobY, hitStep, None);
    }

    if (phaseA.edits == 0)
    {
        if (reportInterception(phaseA, "phase A", knobX, knobY)) return 1;

        std::fprintf(stderr,
                     "\nFAIL (phase A): dragging at (%d,%d) produced no parameter edit, in either\n"
                     "  direction. Either the editor is ignoring pointer input, or there is no longer\n"
                     "  a knob at that coordinate. Dump the editor with DUSK_UI_DRAG_DUMP=/tmp/e.ppm,\n"
                     "  look at it, and either fix the input path or update the coordinate in the\n"
                     "  plugin's dusk_daf_add_ui_drag_test() call.\n",
                     knobX, knobY);
        return 1;
    }

    if (! phaseA.looksLikeADrag())
    {
        // A prompt that appears after the press takes the rest of the gesture,
        // which looks exactly like a button: edits at the start, none after.
        if (reportInterception(phaseA, "phase A", knobX, knobY)) return 1;

        std::fprintf(stderr,
                     "\nFAIL (phase A): the control at (%d,%d) emitted edits, but not progressively\n"
                     "  (%zu by the halfway point, %zu in total). That is the signature of a button or\n"
                     "  a preset step rather than a knob following the pointer. Aim the coordinate at\n"
                     "  a knob.\n",
                     knobX, knobY, phaseA.midEdits, phaseA.edits);
        return 1;
    }

    std::printf("\nphase A     : drag at (%d,%d) step %d produced %zu parameter edits (%zu by halfway), %u gesture(s)\n",
                knobX, knobY, hitStep, phaseA.edits, phaseA.midEdits, phaseA.gestureBegins);

    // ---- phase B: the same drag, across a focus change ----------------------
    //
    // Reversed direction so a knob left at the end of its travel by phase A still
    // has somewhere to go.

    const DragResult phaseB = dragAt(fx, knobX, knobY, -hitStep, thief);

    std::printf("phase B     : same drag with focus stolen mid-gesture produced %zu edits (%zu by halfway)\n",
                phaseB.edits, phaseB.midEdits);

    // Half of phase A is a deliberately loose bar. The failure this guards is
    // total -- the held button is dropped and the knob stops following the
    // pointer -- so the gate does not need to be tight, and a tight one would
    // flake on the frames legitimately lost while focus changes.
    const size_t required = (phaseA.edits + 1) / 2;
    if (phaseB.edits < required)
    {
        // Worth guarding hardest here: the message below names a specific
        // framework regression, and a stolen pointer produces the same shortfall.
        if (reportInterception(phaseB, "phase B", knobX, knobY)) return 1;

        std::fprintf(stderr,
                     "\nFAIL (phase B): the drag stopped working when focus changed mid-gesture.\n"
                     "  phase A produced %zu edits, phase B produced %zu, needed at least %zu.\n"
                     "  An editor embedded in a DAW does not hold keyboard focus, so this is what a\n"
                     "  user sees as \"the knobs do not turn\". Check what the ImGui backend does with\n"
                     "  focus events: io.AddFocusEvent(false) makes ImGui call ClearInputMouse() and\n"
                     "  drop the held mouse button (dusk-audio/plugins#233 follow-up).\n",
                     phaseA.edits, phaseB.edits, required);
        return 1;
    }

    guiExt->destroy(plugin);
    plugin->destroy(plugin);
    entry->deinit();

    XDestroyWindow(fx.display, thief);
    XDestroyWindow(fx.display, parent);
    XCloseDisplay(fx.display);

    std::printf("\nPASS: the editor answers a drag, and keeps answering it across a focus change.\n");
    return 0;
}
