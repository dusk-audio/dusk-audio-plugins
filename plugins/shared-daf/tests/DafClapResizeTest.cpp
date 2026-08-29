// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Guards the size contract an editor has with its host: that the aspect ratio we
// advertise is the one the editor is actually drawn to, and that a host is told
// when that ratio changes.
//
// Both halves shipped broken and neither was catchable by anything else we run.
//
//   1. WRONG ADVERTISED ASPECT. pugl stores ONE (width, height) pair as both the
//      minimum size and, with keepAspectRatio, the fixed aspect
//      (puglSetGeometryConstraints), and X11 advertises both out of it; DAF's
//      VST3 wrapper derives its ratio the same way. A minimum that is not exactly
//      on the design ratio therefore advertises the WRONG one. 4K EQ 2 shipped
//      560x373 against a 960x640 design (1.50134 vs 1.5), so hosts resolved its
//      window a pixel off and, once the ratio changed, drew it wider than the
//      window with the right-hand column cut off.
//
//   2. STALE HINTS. CLAP hosts cache get_resize_hints and keep enforcing the
//      cached aspect, including against a size the plugin itself asks for. An
//      editor that changes its design aspect must call resize_hints_changed
//      BEFORE request_resize or the host builds a window to the old ratio:
//      Bitwig answered a 960x516 request with a 902x601 container.
//
// The aspect check needs no interaction and runs for every plugin. The toggle
// checks run only where a plugin passes the coordinates of a control that
// changes the design size.
//
// Linux only, same as the drag guard: dlopen plus X11 plus XTest.

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
#include "clap/ext/timer-support.h"
#include "clap/factory/plugin-factory.h"

// --------------------------------------------------------------------------------------------------------------------
// minimal host
//
// This one honours request_resize, which is the interesting case: a host that
// refuses tells us nothing about whether the plugin asked for the right thing.
// Every call is stamped with a sequence number so "did the plugin renotify the
// hints BEFORE asking" is answerable rather than inferred.

static clap_id gTimerId = CLAP_INVALID_ID;
static Display* gDisplay = nullptr;
static Window gParent = None;

static uint32_t gSeq = 0;
static uint32_t gLastHintsChangedSeq = 0;
static bool gHintsEverChanged = false;

// Not "ResizeRequest": X11's X.h defines that as an event type (25), and a struct
// by that name expands to `struct 25 {`.
struct ResizeAsk {
    uint32_t seq;
    uint32_t width;
    uint32_t height;
};
static std::vector<ResizeAsk> gRequests;

static bool CLAP_ABI timerRegister(const clap_host_t*, uint32_t, clap_id* const id) { gTimerId = 1; *id = 1; return true; }
static bool CLAP_ABI timerUnregister(const clap_host_t*, clap_id) { gTimerId = CLAP_INVALID_ID; return true; }
static const clap_host_timer_support_t gTimer = { timerRegister, timerUnregister };

static void CLAP_ABI guiHintsChanged(const clap_host_t*)
{
    gLastHintsChangedSeq = ++gSeq;
    gHintsEverChanged = true;
}

static bool CLAP_ABI guiRequestResize(const clap_host_t*, const uint32_t w, const uint32_t h)
{
    gRequests.push_back({ ++gSeq, w, h });
    XResizeWindow(gDisplay, gParent, w, h);
    XSync(gDisplay, False);
    return true;
}

static bool CLAP_ABI guiRequestShow(const clap_host_t*) { return false; }
static bool CLAP_ABI guiRequestHide(const clap_host_t*) { return false; }
static void CLAP_ABI guiRequestClosed(const clap_host_t*, bool) {}
static const clap_host_gui_t gGui = { guiHintsChanged, guiRequestResize, guiRequestShow, guiRequestHide, guiRequestClosed };

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* const id)
{
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &gTimer;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gGui;
    return nullptr;
}
static void CLAP_ABI hostRequestRestart(const clap_host_t*) {}
static void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
static void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

static clap_host_t gHost = {
    CLAP_VERSION_INIT, nullptr,
    "DafClapResizeTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback
};

static const clap_plugin_t* gPlugin = nullptr;
static const clap_plugin_timer_support_t* gPluginTimer = nullptr;

static void pump(const int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        if (gPluginTimer != nullptr && gTimerId != CLAP_INVALID_ID)
            gPluginTimer->on_timer(gPlugin, gTimerId);
        XSync(gDisplay, False);
        struct timespec ts = { 0, 16 * 1000000L };
        nanosleep(&ts, nullptr);
    }
}

namespace {

struct Size { uint32_t w = 0, h = 0; };
struct Hints { bool preserve = false; uint32_t w = 0, h = 0; };

Size sizeOf(const clap_plugin_gui_t* const gui)
{
    Size s;
    gui->get_size(gPlugin, &s.w, &s.h);
    return s;
}

Hints hintsOf(const clap_plugin_gui_t* const gui)
{
    clap_gui_resize_hints_t raw = {};
    Hints h;
    if (gui->get_resize_hints(gPlugin, &raw))
    {
        h.preserve = raw.preserve_aspect_ratio;
        h.w = raw.aspect_ratio_width;
        h.h = raw.aspect_ratio_height;
    }
    return h;
}

// Exact, not approximate. The advertised pair and the editor's own size are both
// integers, and a ratio that truly matches satisfies the cross-product with no
// rounding at all. Comparing doubles with a tolerance is what let 1.50134 pass
// for 1.5 in the first place.
bool ratioMatches(const Size& size, const Hints& hints)
{
    return (uint64_t) size.w * hints.h == (uint64_t) size.h * hints.w;
}

void clickAt(const int x, const int y)
{
    XTestFakeMotionEvent(gDisplay, -1, x, y, CurrentTime);
    pump(3);
    XTestFakeButtonEvent(gDisplay, 1, True, CurrentTime);
    pump(3);
    XTestFakeButtonEvent(gDisplay, 1, False, CurrentTime);
    pump(25);
}

} // namespace

int main(const int argc, const char* const* const argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr,
                     "usage: %s <plugin.clap> [toggleX toggleY]\n"
                     "  toggleX/toggleY: a control that changes the editor's design size.\n", argv[0]);
        return 2;
    }

    const std::string path = argv[1];
    const bool haveToggle = argc >= 4;
    const int toggleX = haveToggle ? std::atoi(argv[2]) : 0;
    const int toggleY = haveToggle ? std::atoi(argv[3]) : 0;

    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }

    const clap_plugin_entry_t* const entry = (const clap_plugin_entry_t*) dlsym(lib, "clap_entry");
    if (entry == nullptr || ! entry->init(path.c_str()))
    {
        std::fprintf(stderr, "FAIL: no usable clap_entry\n");
        return 1;
    }

    const clap_plugin_factory_t* const factory =
        (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_descriptor_t* const desc = factory->get_plugin_descriptor(factory, 0);
    gPlugin = factory->create_plugin(factory, &gHost, desc->id);
    if (gPlugin == nullptr || ! gPlugin->init(gPlugin))
    {
        std::fprintf(stderr, "FAIL: create/init plugin\n");
        return 1;
    }

    const clap_plugin_gui_t* const gui =
        (const clap_plugin_gui_t*) gPlugin->get_extension(gPlugin, CLAP_EXT_GUI);
    gPluginTimer = (const clap_plugin_timer_support_t*) gPlugin->get_extension(gPlugin, CLAP_EXT_TIMER_SUPPORT);
    if (gui == nullptr || gPluginTimer == nullptr)
    {
        std::fprintf(stderr, "FAIL: plugin lacks the gui or timer-support extension\n");
        return 1;
    }

    gDisplay = XOpenDisplay(nullptr);
    if (gDisplay == nullptr)
    {
        std::fprintf(stderr, "FAIL: XOpenDisplay: no DISPLAY. Run this under xvfb-run.\n");
        return 1;
    }

    int a = 0, b = 0, c = 0, d = 0;
    if (! XTestQueryExtension(gDisplay, &a, &b, &c, &d))
    {
        std::fprintf(stderr, "FAIL: X server has no XTEST extension\n");
        return 1;
    }

    if (! gui->create(gPlugin, CLAP_WINDOW_API_X11, false))
    {
        std::fprintf(stderr, "FAIL: gui->create\n");
        return 1;
    }

    Size initial = sizeOf(gui);
    XSetWindowAttributes attr = {};
    attr.override_redirect = True;
    gParent = XCreateWindow(gDisplay, DefaultRootWindow(gDisplay), 0, 0, initial.w, initial.h, 0,
                            CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attr);
    XMapRaised(gDisplay, gParent);
    XSync(gDisplay, False);

    clap_window_t window = {};
    window.api = CLAP_WINDOW_API_X11;
    window.x11 = (uintptr_t) gParent;
    if (! gui->set_parent(gPlugin, &window) || ! gui->show(gPlugin))
    {
        std::fprintf(stderr, "FAIL: set_parent/show\n");
        return 1;
    }
    pump(30);

    initial = sizeOf(gui);
    const Hints initialHints = hintsOf(gui);

    std::printf("plugin      : %s\n", path.c_str());
    std::printf("editor      : %ux%u\n", initial.w, initial.h);
    std::printf("hints       : preserve_aspect=%d ratio=%u:%u\n",
                (int) initialHints.preserve, initialHints.w, initialHints.h);

    // ---- the advertised aspect must be the editor's own -----------------------

    if (initialHints.preserve)
    {
        if (initialHints.w == 0 || initialHints.h == 0)
        {
            std::fprintf(stderr, "\nFAIL: preserve_aspect_ratio is set but the ratio is %u:%u\n",
                         initialHints.w, initialHints.h);
            return 1;
        }

        if (! ratioMatches(initial, initialHints))
        {
            std::fprintf(stderr,
                         "\nFAIL: the advertised aspect is not the editor's own.\n"
                         "  editor %ux%u (%.5f) but hints say %u:%u (%.5f).\n"
                         "  pugl advertises the minimum size AS the fixed aspect, so the minimum has to\n"
                         "  sit exactly on the design ratio. Reduce designH/designW and round the\n"
                         "  minimum width to the nearest multiple of the denominator; a host resolving\n"
                         "  the ratio you advertise will otherwise size the window off-design and the\n"
                         "  editor gets letterboxed or clipped.\n",
                         initial.w, initial.h, (double) initial.w / initial.h,
                         initialHints.w, initialHints.h, (double) initialHints.w / initialHints.h);
            return 1;
        }
        std::printf("aspect      : advertised ratio matches the editor exactly\n");
    }
    else
    {
        std::printf("aspect      : no fixed aspect advertised; nothing to check\n");
    }

    if (! haveToggle)
    {
        std::printf("\nPASS: the editor advertises an aspect it actually has.\n");
        return 0;
    }

    // ---- toggling the design size -------------------------------------------

    gRequests.clear();
    gHintsEverChanged = false;

    clickAt(toggleX, toggleY);

    const Size toggled = sizeOf(gui);
    const Hints toggledHints = hintsOf(gui);

    if (toggled.w == initial.w && toggled.h == initial.h)
    {
        std::fprintf(stderr,
                     "\nFAIL: clicking (%d,%d) did not change the editor size (still %ux%u).\n"
                     "  Either that is no longer the control that collapses the design, or the resize\n"
                     "  never reached the host. Dump the editor with DUSK_UI_DRAG_DUMP=/tmp/e.ppm to\n"
                     "  find the control and update the coordinate in dusk_daf_add_resize_test().\n",
                     toggleX, toggleY, toggled.w, toggled.h);
        return 1;
    }

    std::printf("toggled     : %ux%u -> %ux%u, hints now %u:%u\n",
                initial.w, initial.h, toggled.w, toggled.h, toggledHints.w, toggledHints.h);

    if (toggledHints.preserve && ! ratioMatches(toggled, toggledHints))
    {
        std::fprintf(stderr,
                     "\nFAIL: after the toggle the advertised aspect is not the editor's own:\n"
                     "  editor %ux%u (%.5f) but hints say %u:%u (%.5f).\n",
                     toggled.w, toggled.h, (double) toggled.w / toggled.h,
                     toggledHints.w, toggledHints.h, (double) toggledHints.w / toggledHints.h);
        return 1;
    }

    if (gRequests.empty())
    {
        std::fprintf(stderr,
                     "\nFAIL: the editor changed size without asking the host to resize.\n"
                     "  The host window would keep its old size and the editor would sit in it clipped\n"
                     "  or surrounded by dead space.\n");
        return 1;
    }

    const ResizeAsk& request = gRequests.back();
    if (request.width != toggled.w || request.height != toggled.h)
    {
        std::fprintf(stderr,
                     "\nFAIL: the host was asked for %ux%u but the editor settled at %ux%u.\n",
                     request.width, request.height, toggled.w, toggled.h);
        return 1;
    }

    // The ordering is the whole point. A host resolves request_resize against the
    // hints it holds at that moment, so renotifying afterwards fixes the window
    // only on the NEXT resize -- which is exactly how a 960x516 request became a
    // 902x601 window built to the previous ratio.
    const bool ratioChanged = toggledHints.preserve
                           && (toggledHints.w != initialHints.w || toggledHints.h != initialHints.h);
    if (ratioChanged)
    {
        if (! gHintsEverChanged)
        {
            std::fprintf(stderr,
                         "\nFAIL: the aspect changed to %u:%u but resize_hints_changed() was never called.\n"
                         "  CLAP hosts cache get_resize_hints and keep enforcing the cached ratio.\n",
                         toggledHints.w, toggledHints.h);
            return 1;
        }
        if (gLastHintsChangedSeq > request.seq)
        {
            std::fprintf(stderr,
                         "\nFAIL: resize_hints_changed() came AFTER request_resize (seq %u vs %u).\n"
                         "  The host resolved the resize against the old ratio; notify first.\n",
                         gLastHintsChangedSeq, request.seq);
            return 1;
        }
        std::printf("hints       : renotified before the resize request (seq %u < %u)\n",
                    gLastHintsChangedSeq, request.seq);
    }

    // ---- and back again, repeatedly, with no drift ---------------------------

    for (int i = 0; i < 4; ++i)
    {
        clickAt(toggleX, toggleY);
        const Size now = sizeOf(gui);
        const Size want = (i % 2) == 0 ? initial : toggled;
        if (now.w != want.w || now.h != want.h)
        {
            std::fprintf(stderr,
                         "\nFAIL: toggle %d landed on %ux%u, expected %ux%u.\n"
                         "  The size drifts across toggles, which is what a quantised aspect ratio does:\n"
                         "  each round trip through the host loses a pixel or two.\n",
                         i + 2, now.w, now.h, want.w, want.h);
            return 1;
        }
    }
    std::printf("stability   : five toggles, no drift\n");

    gui->destroy(gPlugin);
    gPlugin->destroy(gPlugin);
    entry->deinit();

    std::printf("\nPASS: the advertised aspect is real, the host is renotified before each resize,\n"
                "      and the size returns exactly across toggles.\n");
    return 0;
}
