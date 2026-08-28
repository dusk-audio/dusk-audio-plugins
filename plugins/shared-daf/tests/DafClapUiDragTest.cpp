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

    // A knob follows the pointer, so edits arrive throughout the gesture. A
    // button, a preset step or an INIT produces one burst at the press. Requiring
    // edits both at the halfway point and after it keeps "the drag works" from
    // being satisfiable by a click.
    bool looksLikeADrag() const
    {
        return midEdits > 0 && edits > midEdits;
    }
};

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

    XTestFakeMotionEvent(fx.display, -1, rootX, rootY, CurrentTime);
    fx.pump();

    XTestFakeButtonEvent(fx.display, 1, True, CurrentTime);
    fx.pump();

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
    result.edits         = countFor();
    result.midEdits      = midCount;
    result.gestureBegins = fx.collected.gestureBegins;
    return result;
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
    int hitStep = -6;
    DragResult phaseA = dragAt(fx, knobX, knobY, hitStep, None);
    if (phaseA.edits == 0 || ! phaseA.looksLikeADrag())
    {
        hitStep = 6;
        phaseA  = dragAt(fx, knobX, knobY, hitStep, None);
    }

    if (phaseA.edits == 0)
    {
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
