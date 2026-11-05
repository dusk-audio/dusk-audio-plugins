// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// A window-free DAF host adapter runs the production editor and Dear ImGui's
// real pointer interaction. No widget drawing or gesture code is copied here.
#include "DearImGui/imgui.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define MULTICOMP_UI_TEST_HOST
#define START_NAMESPACE_DAF namespace DAF {
#define END_NAMESPACE_DAF }
#define DAF_UI_DEFAULT_WIDTH 1200
#define DAF_UI_DEFAULT_HEIGHT 480
using GLint = int;
constexpr int GL_VIEWPORT = 0;
inline void glGetIntegerv(int, GLint* p) { p[0] = p[1] = 0; p[2] = 1200; p[3] = 480; }
namespace DAF
{
struct TestInstance
{
    std::array<float, 256> parameters{};
    bool externalActive = false;
};
class UI
{
public:
    struct Event { uint32_t parameter; float value; };
    std::vector<Event> writes, gestures;
    // Also receives every gesture event, for a test that outlives the editor.
    std::vector<Event>* gestureLog = nullptr;
    TestInstance state;
    void* instance = nullptr;
    UI(uint32_t, uint32_t) {}
    virtual ~UI() = default;
    void draw() { onImGuiDisplay(); }
    void parameter(uint32_t p, float v) { state.parameters[p] = v; parameterChanged(p, v); }
    void restore(const char* key, const char* value) { stateChanged(key, value); }
    uint32_t width = 1200, height = 480;   // the editor window (Host sets a letterboxed one)
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    float getScaleFactor() const { return 1; }
    uint32_t sizeRequests = 0;
    void setSize(uint32_t, uint32_t) { ++sizeRequests; }
    void setGeometryConstraints(uint32_t, uint32_t, bool) {}
    void* getPluginInstancePointer() const { return instance; }
    void repaint() {}
    void editParameter(uint32_t p, bool begin)
    {
        gestures.push_back({p, begin ? 1.0f : 0.0f});
        if (gestureLog != nullptr) gestureLog->push_back({p, begin ? 1.0f : 0.0f});
    }
    void setParameterValue(uint32_t p, float value) { state.parameters[p] = value; writes.push_back({p, value}); }
protected:
    virtual void parameterChanged(uint32_t, float) {}
    virtual void stateChanged(const char*, const char*) {}
    virtual void programLoaded(uint32_t) {}
    virtual void uiIdle() {}
    virtual void onImGuiDisplay() {}
};
}

#ifndef MULTICOMP_UI_TEST_SOURCE
#define MULTICOMP_UI_TEST_SOURCE "MultiCompUI.cpp"
#endif
#include MULTICOMP_UI_TEST_SOURCE

float multiCompGetBusFadePosition(void*) noexcept { return 1; }
float multiCompGetGainReduction(void*) noexcept { return 0; }
float multiCompGetBandGainReduction(void*, int) noexcept { return 0; }
float multiCompGetInputLevel(void*) noexcept { return -60; }
float multiCompGetOutputLevel(void*) noexcept { return -60; }
float multiCompGetOutputVuLevel(void*) noexcept { return -60; }
float multiCompGetGainReductionNeedle(void*) noexcept { return 0; }
float multiCompGetVcaGainReductionNeedle(void*) noexcept { return 0; }
float multiCompGetStudioVcaInputLevel(void*) noexcept { return -60; }
float multiCompGetStudioVcaOutputLevel(void*) noexcept { return -60; }
float multiCompGetInputChannelLevel(void*, int) noexcept { return -60; }
float multiCompGetOutputChannelLevel(void*, int) noexcept { return -60; }
float multiCompGetBusMeterReading(void*) noexcept { return 0; }
float multiCompGetParameterValue(void* p, uint32_t index) noexcept
{
    return static_cast<DAF::TestInstance*>(p)->parameters[index];
}
// Deliberately independent of the requested External Sidechain parameter:
// a request without connected input ports still uses the internal detector.
float multiCompGetExternalSidechainActive(void* p) noexcept
{
    return p != nullptr && static_cast<DAF::TestInstance*>(p)->externalActive ? 1.0f : 0.0f;
}

namespace
{
using multicompp::ParamId;
constexpr uint32_t pid(ParamId p) { return static_cast<uint32_t>(p); }
constexpr uint32_t band(int b, int offset) { return multicompp::kBandBase + b * 8 + offset; }

struct Host
{
    struct Context
    {
        ImGuiContext* context = ImGui::CreateContext();
        Context()
        {
            ImGui::SetCurrentContext(context);
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr; io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(1200, 480); io.DeltaTime = 1.0f / 60.0f;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // as DAF runs ImGui (DearImGui.cpp)
        }
        ~Context() { ImGui::DestroyContext(context); }
    } context;
    DAF::MultiCompUI ui;
    explicit Host(int mode, uint32_t width = 1200, uint32_t height = 480)
    {
        ui.width = width;
        ui.height = height;
        ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        // In the plugin the host backend builds the font atlas. Without one, an editor that found no system font
        // (a bare Linux container) leaves it empty, and ImGui::NewFrame needs a built atlas: give it ImGui's own.
        // An editor that loaded its face built the atlas already and is left as it is.
        ImFontAtlas& fonts = *ImGui::GetIO().Fonts;
        if (!fonts.IsBuilt())
        {
            if (fonts.Fonts.empty()) fonts.AddFontDefault();
            fonts.Build();
        }
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            ui.state.parameters[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
                [](const multicompp::Param& p) { return multicompp::hostDefault(p); },
                [](const multicompp::BandParam& p, int) { return multicompp::hostDefault(p); });
        ui.parameter(pid(ParamId::Mode), static_cast<float>(mode));
        frame(); frame();
    }
    // The pointer at (x, y) in a face's own 1120 x 380 canvas, where the controls below were measured: each face
    // is still drawn there and scaled into the face area of the current canvas (MultiCompUI::beginLegacyFace).
    // A negative position stays off the window.
    void frame(float x = -100, float y = -100, bool down = false)
    {
        if (x >= 0.0f && y >= 0.0f)
        {
            x *= DAF::kLegacyFaceScale;
            y = DAF::kLegacyFaceY + (y - DAF::kLegacyFaceTopY) * DAF::kLegacyFaceScale;
        }
        canvas(x, y, down);
    }
    // The pointer at (x, y) on the 1200 x 480 canvas itself: the header, the mode row and the settings drawer.
    void canvas(float x = -100, float y = -100, bool down = false)
    {
        ImGui::SetCurrentContext(context.context);
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame(); ui.draw(); ImGui::Render();
    }
    void canvasClick(float x, float y) { canvas(x, y); canvas(x, y, true); canvas(x, y); }
    void canvasDrag(float x, float y)
    {
        canvas(x, y); canvas(x, y, true); canvas(x, y - 20, true); canvas(x, y - 20); canvas(-100, -100);
    }
    void click(float x, float y) { frame(x, y); frame(x, y, true); frame(x, y); }
    void hold(float x, float y) { frame(x, y); frame(x, y, true); frame(x, y - 20, true); }
    void drag(float x, float y) { hold(x, y); frame(x, y - 20); frame(); }
    int count(const std::vector<DAF::UI::Event>& events, uint32_t p, float v) const
    {
        int n = 0;
        for (const auto& event : events) if (event.parameter == p && event.value == v) ++n;
        return n;
    }
    int writes(uint32_t p) const
    {
        int n = 0;
        for (const auto& event : ui.writes) if (event.parameter == p) ++n;
        return n;
    }
    bool balanced(uint32_t p) const
    {
        return count(ui.gestures, p, 1) == 1 && count(ui.gestures, p, 0) == 1;
    }
};

int failures = 0;
void check(bool ok, const char* name)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += ok ? 0 : 1;
}

void hiddenSolo()
{
    for (int b = 0; b < 4; ++b)
    {
        Host h(7);
        h.ui.parameter(band(b, 6), 1);
        const uint32_t siblingSolo = band((b + 1) % 4, 6);
        h.ui.parameter(siblingSolo, 1);
        h.click(201.0f + 114.0f * b, 126);
        int soloAt = -1, disableAt = -1;
        for (size_t i = 0; i < h.ui.writes.size(); ++i)
        {
            const auto& e = h.ui.writes[i];
            if (e.parameter == band(b, 6) && e.value == 0) soloAt = static_cast<int>(i);
            if (e.parameter == band(b, 7) && e.value == 0) disableAt = static_cast<int>(i);
        }
        std::printf("solo band=%d clearAt=%d disableAt=%d\n", b, soloAt, disableAt);
        check(soloAt >= 0 && disableAt > soloAt && h.balanced(band(b, 6))
              && h.balanced(band(b, 7)) && h.writes(siblingSolo) == 0,
              "disabling a solo band clears its Solo and preserves sibling Solo");
    }
}

struct Control { int mode; uint32_t p; float x, y; };
constexpr std::array<Control, 19> controls{{
    {6, pid(ParamId::DigitalThreshold), 582, 180}, {6, pid(ParamId::DigitalRatio), 676, 180},
    {6, pid(ParamId::DigitalKnee), 770, 180}, {6, pid(ParamId::DigitalAttack), 864, 180},
    {6, pid(ParamId::DigitalRelease), 958, 180}, {6, pid(ParamId::DigitalLookahead), 582, 286},
    {6, pid(ParamId::DigitalMix), 676, 286}, {6, pid(ParamId::DigitalOutput), 770, 286},
    {6, pid(ParamId::SidechainHP), 492, 200},
    {7, band(0, 0), 674, 194}, {7, band(0, 1), 766, 194},
    {7, band(0, 2), 858, 194}, {7, band(0, 3), 950, 194},
    {7, band(0, 4), 720, 294}, {7, pid(ParamId::MbOutput), 812, 294},
    {7, pid(ParamId::MbMix), 904, 294}, {7, pid(ParamId::Crossover1), 258, 220},
    {7, pid(ParamId::Crossover2), 372, 220}, {7, pid(ParamId::Crossover3), 486, 220}
}};

// A knob of every hardware face, where the editor draws it at the default size (the faces' canvas sits 274 px
// higher on screen than its own coordinates). Each starts away from the top of its travel (Mix at 50 %), so the
// hold's upward drag writes. FET's Mix trim is one of four faces' controls for the same parameter: Opto, VCA and
// Studio VCA draw their own elsewhere, so a gesture must follow the control, not the parameter.
constexpr std::array<Control, 8> hardwareControls{{
    {0, pid(ParamId::OptoPeakReduction), 794, 250}, {1, pid(ParamId::FetInput), 177, 227},
    {2, pid(ParamId::VcaThreshold), 171, 204}, {3, pid(ParamId::BusThreshold), 409, 175},
    {4, pid(ParamId::FetAttack), 559, 164}, {4, pid(ParamId::FetBlueHeadroom), 955, 322},
    {5, pid(ParamId::StudioVcaRatio), 230, 155}, {1, pid(ParamId::Mix), 1010, 322}
}};

template <size_t N>
void gestures(const std::array<Control, N>& table, bool switched)
{
    for (const auto& c : table)
    {
      for (int destination = 0; destination < (switched ? 8 : 1); ++destination)
      {
        if (switched && destination == c.mode) continue;
        Host h(c.mode);
        if (c.p == pid(ParamId::DigitalMix) || c.p == pid(ParamId::MbMix) || c.p == pid(ParamId::Mix))
            h.ui.parameter(c.p, 50);
        h.hold(c.x, c.y);
        if (switched) h.ui.parameter(pid(ParamId::Mode), static_cast<float>(destination));
        h.frame(c.x, c.y - 20, true); h.frame(c.x, c.y - 20, true);
        h.frame(c.x, c.y - 20); h.frame();
        if (switched)
        {
            h.ui.parameter(pid(ParamId::Mode), static_cast<float>(c.mode));
            h.frame(); h.frame();
        }
        std::printf("gesture mode=%d p=%u switched=%d destination=%d begins=%d ends=%d writes=%d\n", c.mode, c.p,
                    switched, destination, h.count(h.ui.gestures, c.p, 1), h.count(h.ui.gestures, c.p, 0), h.writes(c.p));
        check(h.balanced(c.p) && h.writes(c.p) > 0,
              switched ? "mode change closes held gesture" : "ordinary drag writes and closes gesture");
      }
    }
}

// A hardware knob's gesture closes once its control is no longer drawn, as a Digital or Multiband control's does:
// host Mode automation hides it mid-drag, or the editor closes. A drag that stays on the knob keeps one gesture open
// across all its frames.
void hardwareHidden()
{
    const uint32_t mode = pid(ParamId::Mode), headroom = pid(ParamId::FetBlueHeadroom);
    const uint32_t attack = pid(ParamId::FetAttack);
    {
        // FET BLUE's Headroom trim is held; the host moves to FET (no trim there), then Opto, the pointer is
        // released there, and the host returns to FET BLUE.
        Host h(4); h.hold(955, 322);
        h.ui.parameter(mode, 1); h.frame(955, 297, true); h.frame(955, 292, true);
        h.ui.parameter(mode, 0); h.frame(955, 287, true); h.frame(955, 287); h.frame();
        h.ui.parameter(mode, 4); h.frame(); h.frame();
        std::printf("hidden Headroom trim begins=%d ends=%d writes=%d\n", h.count(h.ui.gestures, headroom, 1),
                    h.count(h.ui.gestures, headroom, 0), h.writes(headroom));
        check(h.balanced(headroom) && h.writes(headroom) > 0,
              "FET BLUE's Headroom trim closes its gesture when Mode automation hides it");
    }
    {
        // FET BLUE's Attack is held; the host moves to Opto.
        Host h(4); h.hold(559, 164);
        h.ui.parameter(mode, 0); h.frame(559, 139, true); h.frame(559, 134, true); h.frame(559, 134); h.frame();
        h.ui.parameter(mode, 4); h.frame(); h.frame();
        std::printf("hidden FET Attack begins=%d ends=%d writes=%d\n", h.count(h.ui.gestures, attack, 1),
                    h.count(h.ui.gestures, attack, 0), h.writes(attack));
        check(h.balanced(attack) && h.writes(attack) > 0,
              "FET Attack closes its gesture when Mode automation hides it");
    }
    {
        // A drag held over several frames: one gesture, open until the release.
        Host h(4); h.hold(559, 164);
        bool open = true;
        for (int f = 1; f <= 6; ++f)
        {
            h.frame(559, 144.0f - 4.0f * static_cast<float>(f), true);
            open = open && h.count(h.ui.gestures, attack, 1) == 1 && h.count(h.ui.gestures, attack, 0) == 0;
        }
        const int heldWrites = h.writes(attack);
        h.frame(559, 120); h.frame();
        std::printf("held FET Attack drag openThroughout=%d writes=%d begins=%d ends=%d\n", open, heldWrites,
                    h.count(h.ui.gestures, attack, 1), h.count(h.ui.gestures, attack, 0));
        check(open && heldWrites > 1 && h.balanced(attack),
              "a multi-frame drag of a hardware knob sends exactly one begin and one end");
    }
}

// Closing the editor mid-drag closes the held gesture: a Digital knob (closed already) and a hardware knob.
void editorClosedMidDrag()
{
    for (const Control& c : {controls[0], hardwareControls[4], hardwareControls[0]})
    {
        std::vector<DAF::UI::Event> events;
        {
            Host h(c.mode);
            h.ui.gestureLog = &events;
            h.hold(c.x, c.y);
        }
        int begins = 0, ends = 0;
        for (const auto& event : events)
            if (event.parameter == c.p) ++(event.value == 1 ? begins : ends);
        std::printf("editor closed mid-drag mode=%d p=%u begins=%d ends=%d\n", c.mode, c.p, begins, ends);
        check(begins == 1 && ends == 1, "closing the editor closes a held knob's gesture");
    }
}

void hiddenControls()
{
    // Host automation can hide the selected band while its threshold is held.
    Host h(7); h.hold(674, 194);
    h.ui.parameter(band(0, 7), 0);
    h.frame(674, 169, true); h.frame(674, 164, true); h.frame(674, 164); h.frame();
    std::printf("hidden band begins=%d ends=%d nextBandWrites=%d\n", h.count(h.ui.gestures, band(0, 0), 1),
                h.count(h.ui.gestures, band(0, 0), 0), h.writes(band(1, 0)));
    check(h.balanced(band(0, 0)) && h.writes(band(1, 0)) == 0,
          "hiding selected band closes gesture without editing replacement band");
    for (int mode : {6, 7})
    {
        Host out(mode);
        const uint32_t p = pid(mode == 6 ? ParamId::DigitalOutput : ParamId::MbOutput);
        const float x = mode == 6 ? 770 : 812, y = mode == 6 ? 286 : 294;
        out.hold(x, y); const int before = out.writes(p);
        out.ui.parameter(pid(ParamId::AutoMakeup), 1);
        out.frame(x, y - 25, true); out.frame(x, y - 30, true); out.frame(x, y - 30); out.frame();
        std::printf("disabled output mode=%d begins=%d ends=%d before=%d after=%d\n", mode,
                    out.count(out.ui.gestures, p, 1), out.count(out.ui.gestures, p, 0), before, out.writes(p));
        check(out.balanced(p) && before > 0 && before == out.writes(p),
              "Auto Makeup closes output gesture and prevents further writes");
        // The same with the pointer dragged off the knob first. ImGui drops a disabled active item only while the
        // pointer is over it, so off the knob it stayed active and kept writing after its gesture had closed, with
        // no gesture around the writes.
        Host far(mode);
        far.hold(x, y); far.frame(x, y - 90, true);
        const int held = far.writes(p);
        far.ui.parameter(pid(ParamId::AutoMakeup), 1);
        far.frame(x, y - 120, true); far.frame(x, y - 150, true); far.frame(x, y - 150); far.frame();
        std::printf("disabled output off the knob mode=%d begins=%d ends=%d before=%d after=%d\n", mode,
                    far.count(far.ui.gestures, p, 1), far.count(far.ui.gestures, p, 0), held, far.writes(p));
        check(far.balanced(p) && held > 0 && held == far.writes(p),
              "Auto Makeup closes output gesture and prevents further writes with the pointer off the knob");
    }
}

// VCA's PULL/SC switch, pressed in a fresh editor, puts the shared sidechain cutoff at 20 Hz: it engages the switch
// and stays close to transparent as the high-pass of the Opto, Bus and Digital faces. Its first value used to be the
// range's top, 500 Hz, which those faces then applied after a mode change.
void vcaPullScFirstValue()
{
    Host h(2);
    h.click(171, 320);
    const uint32_t p = pid(ParamId::SidechainHP);
    float hz = -1.0f;
    for (const auto& event : h.ui.writes)
        if (event.parameter == p) hz = multicompp::hostToPlain(multicompp::kParams[p], event.value);
    std::printf("VCA PULL/SC first press: SC HP %.2f Hz\n", static_cast<double>(hz));
    check(std::abs(hz - 20.0f) < 0.5f, "a fresh PULL/SC press engages the switch at a 20 Hz cutoff");
}

void outputAccess()
{
    for (int mode : {6, 7}) for (int automatic : {0, 1}) for (int request : {0, 1})
        for (int connected : {-1, 0, 1})
        {
            Host h(mode); const bool active = connected > 0;
            h.ui.state.externalActive = active;
            h.ui.instance = connected < 0 ? nullptr : &h.ui.state;
            h.ui.parameter(pid(ParamId::AutoMakeup), static_cast<float>(automatic));
            h.ui.parameter(pid(ParamId::ExternalSidechain), static_cast<float>(request));
            h.frame();
            const uint32_t p = pid(mode == 6 ? ParamId::DigitalOutput : ParamId::MbOutput);
            const bool external = request != 0 && (connected < 0 || active);
            const bool editable = !automatic || external;
            h.drag(mode == 6 ? 770 : 812, mode == 6 ? 286 : 294);
            const bool ok = editable ? h.writes(p) > 0 && h.balanced(p)
                                     : h.writes(p) == 0 && h.ui.gestures.empty();
            std::printf("output mode=%d auto=%d request=%d connection=%d editable=%d writes=%d\n",
                        mode, automatic, request, connected, editable, h.writes(p));
            check(ok, "output accessibility follows effective Auto Makeup");
        }
}

// A restored state reaches the editor as its text, and the plugin need not run what the text decodes to: DAF's LV2
// wrapper applies the ports a host restores with the state over it. The face follows the plugin when the editor can
// reach it, the text when it cannot. FET BLUE's Headroom trim is drawn only on its face.
void restoredFace()
{
    const uint32_t mode = pid(ParamId::Mode), headroom = pid(ParamId::FetBlueHeadroom);
    multicompp::StateValues saved{};
    for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        saved[static_cast<size_t>(i)] = multicompp::resolveParameter(i,
            [](const multicompp::Param& p) { return multicompp::hostDefault(p); },
            [](const multicompp::BandParam& p, int) { return multicompp::hostDefault(p); });
    struct Case { const char* name; float savedMode; bool reachable; float running; bool blueFace; };
    const Case cases[] = {
        {"FET saved, the plugin runs FET BLUE", 1, true, 4, true},
        {"FET BLUE saved, the plugin runs FET", 4, true, 1, false},
        {"FET BLUE saved, editor without the plugin", 4, false, 0, true},
    };
    for (const auto& c : cases)
    {
        // An open editor has read the plugin once (its first frame) before the host restores the session.
        Host h(c.reachable ? 4 : 1);
        if (c.reachable) { h.ui.instance = &h.ui.state; h.frame(); h.ui.state.parameters[mode] = c.running; }
        saved[mode] = c.savedMode;
        h.ui.restore("parameters", multicompp::encodeState(saved).c_str());
        h.frame(); h.drag(955, 322);
        std::printf("restored face %s: Headroom writes=%d\n", c.name, h.writes(headroom));
        check((h.writes(headroom) > 0) == c.blueFace, "a restored state's face follows the plugin");
    }
}

// The settings drawer, in canvas coordinates. Opened from the mode row in every mode, its controls take the pointer
// from the face under them (the face's disabled items kept the hover, and the drawer's buttons did nothing), and a
// click on the mode row leaves it above the face (the face's window, raised by the click, hid the drawer and took
// its pointer). CLOSE gives the face back.
void settingsDrawer()
{
    const uint32_t distortion = pid(ParamId::Distortion), linkMode = pid(ParamId::StereoLinkMode),
                   lowFreq = pid(ParamId::ScLowFreq), modeId = pid(ParamId::Mode);
    for (int mode = 0; mode < 8; ++mode)
    {
        Host h(mode);
        h.canvasClick(1134, 67);                          // SETTINGS
        h.canvasClick(418, 374);                          // DRIVE: SOFT
        h.canvasClick(243, 374);                          // STEREO: MID-SIDE
        h.canvasDrag(286, 230);                           // SIDECHAIN: LOW FREQ
        const int next = (mode + 1) % 8;
        h.canvasClick(16.0f + 104.0f * next + 50.0f, 67); // another mode, the drawer open
        h.canvasClick(418, 408);                          // DRIVE: HARD
        int others = 0;
        for (const auto& event : h.ui.writes)
            others += event.parameter != distortion && event.parameter != linkMode && event.parameter != lowFreq
                && event.parameter != modeId;
        const bool drive = h.count(h.ui.writes, distortion, 1) == 1 && h.count(h.ui.writes, distortion, 2) == 1;
        const bool link = h.count(h.ui.writes, linkMode, 1) == 1;
        const bool knob = h.writes(lowFreq) > 0 && h.balanced(lowFreq);
        const bool switched = h.count(h.ui.writes, modeId, static_cast<float>(next)) == 1;
        h.canvasClick(1120, 114);                         // CLOSE
        const int driveWrites = h.writes(distortion);
        h.canvasClick(418, 442);                          // where CLIP was
        const bool closed = h.writes(distortion) == driveWrites;
        std::printf("drawer mode=%d drive=%d link=%d knob=%d switched=%d others=%d closed=%d\n", mode, drive, link,
                    knob, switched, others, closed);
        check(drive && link && knob && switched && others == 0 && closed,
              "the settings drawer's controls answer in every mode and stay above the face");
    }
}

// The drawer against Escape, the external key and the resize grip, in canvas coordinates. With keyboard navigation
// on, as DAF runs ImGui, Escape that cancels a held knob or closes the preset list is ImGui's: it used to close the
// drawer too, since ImGui takes the press inside NewFrame, before the drawer sees it. TRUE PEAK in Multiband follows
// the key the plugin has, not the one requested (the DSP ignores it without one). The resize grip works with the
// drawer open, whose window covers the grip's corner.
void drawerKeysAndCorner()
{
    const uint32_t distortion = pid(ParamId::Distortion), lowFreq = pid(ParamId::ScLowFreq),
                   truePeak = pid(ParamId::TruePeakEnable), externalKey = pid(ParamId::ExternalSidechain);
    const auto escape = [](Host& h, float x, float y, bool down) {
        ImGui::SetCurrentContext(h.context.context);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
        h.canvas(x, y, down);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
        h.canvas(x, y, down);
    };
    // Open: a press on a DRIVE button that is not lit writes Distortion (CLIP, or SOFT once CLIP is lit).
    const auto drawerOpen = [distortion](Host& h) {
        const bool clip = h.ui.state.parameters[distortion] != 3.0f;
        const float value = clip ? 3.0f : 1.0f;
        const int before = h.count(h.ui.writes, distortion, value);
        h.canvasClick(418, clip ? 442 : 374);
        return h.count(h.ui.writes, distortion, value) > before;
    };

    Host held(2);
    held.canvasClick(1134, 67);                                   // SETTINGS
    held.canvas(286, 230); held.canvas(286, 230, true); held.canvas(286, 210, true);   // LOW FREQ held
    escape(held, 286, 210, true);
    held.canvas(286, 210); held.canvas();
    const bool heldBalanced = held.balanced(lowFreq);
    const bool openAfterHeld = drawerOpen(held);
    held.canvasClick(560, 24);                                    // the preset list
    ImGui::SetCurrentContext(held.context.context);
    const bool listOpened = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    escape(held, -100, -100, false);
    const bool listClosed = !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const bool openAfterList = drawerOpen(held);
    escape(held, -100, -100, false);
    const bool closedByEscape = !drawerOpen(held);
    std::printf("drawer Escape: knob held -> drawer open %d (gesture balanced %d); preset list %d/%d -> drawer open "
                "%d; Escape alone -> closed %d\n", openAfterHeld, heldBalanced, listOpened, listClosed,
                openAfterList, closedByEscape);
    check(openAfterHeld && heldBalanced, "Escape that cancels a held drawer knob leaves the drawer open");
    check(listOpened && listClosed && openAfterList, "Escape that closes the preset list leaves the drawer open");
    check(closedByEscape, "Escape on its own closes the drawer");

    Host key(7);
    key.ui.instance = &key.ui.state;
    key.ui.state.externalActive = false;
    key.ui.parameter(externalKey, 1.0f);
    key.canvasClick(1134, 67);
    key.canvasClick(716, 222);                                    // TRUE PEAK, key requested, none connected
    const int requestedOnly = key.writes(truePeak);
    key.ui.state.externalActive = true;
    key.canvas();
    key.canvasClick(716, 222);                                    // TRUE PEAK, key connected
    const int connected = key.writes(truePeak);
    std::printf("drawer TRUE PEAK in Multiband: key requested only -> %d writes, key connected -> %d\n",
                requestedOnly, connected);
    check(requestedOnly == 0 && connected > 0, "Multiband's TRUE PEAK follows the key the plugin has");

    Host grip(0);
    grip.canvasClick(1134, 67);
    grip.canvas(1193, 473); grip.canvas(1193, 473, true); grip.canvas(1233, 498, true); grip.canvas(1233, 498);
    grip.canvas();
    const uint32_t sized = grip.ui.sizeRequests;
    const bool openAfterGrip = drawerOpen(grip);
    std::printf("drawer resize grip: %u size requests, drawer open after %d\n", sized, openAfterGrip);
    check(sized > 0 && openAfterGrip, "the resize grip works with the drawer open, and leaves it open");

    // Letterboxed windows: the canvas is centred, and the grip stays in the window's own corner.
    for (const auto size : {std::array<uint32_t, 2>{{1400, 480}}, std::array<uint32_t, 2>{{1200, 600}}})
    {
        Host box(0, size[0], size[1]);
        const float ox = (static_cast<float>(size[0]) - 1200.0f) * 0.5f, oy = (static_cast<float>(size[1]) - 480.0f) * 0.5f;
        box.canvasClick(1134 + ox, 67 + oy);                      // SETTINGS
        const float cx = static_cast<float>(size[0]) - 7.0f, cy = static_cast<float>(size[1]) - 7.0f;
        box.canvas(cx, cy); box.canvas(cx, cy, true); box.canvas(cx + 40, cy + 25, true); box.canvas(cx + 40, cy + 25);
        box.canvas();
        // Open: DRIVE: CLIP, in this window's coordinates.
        const int clipBefore = box.count(box.ui.writes, distortion, 3);
        box.canvasClick(418 + ox, 442 + oy);
        const bool open = box.count(box.ui.writes, distortion, 3) > clipBefore;
        std::printf("drawer resize grip in a %ux%u window: %u size requests, drawer open after %d\n", size[0],
                    size[1], box.ui.sizeRequests, open);
        check(box.ui.sizeRequests > 0 && open, "the resize grip works with the drawer open in a letterboxed window");
    }

    // A press on the scrim in the frame the pointer arrives (from a drawer knob) closes the drawer.
    Host scrim(0);
    scrim.canvasClick(1134, 67);
    scrim.canvas(286, 230);                                       // over LOW FREQ
    scrim.canvas(8, 300, true); scrim.canvas(8, 300);              // to the scrim's margin and pressed, one frame
    scrim.canvas();
    const bool closedByScrim = !drawerOpen(scrim);
    std::printf("drawer scrim press in the pointer's first frame there: closed %d\n", closedByScrim);
    check(closedByScrim, "a press on the scrim closes the drawer in the pointer's first frame there");
}

void numericOwnership()
{
    Host h(7);
    h.click(674, 194); h.click(674, 194); h.frame();
    ImGui::GetIO().AddInputCharactersUTF8("-35"); h.frame(); h.frame();
    h.click(290, 152); h.frame();
    const int own = h.count(h.ui.writes, band(0, 0), -35);
    const int sibling = h.writes(band(1, 0));
    std::printf("numeric band change ownWrites=%d replacementWrites=%d\n", own, sibling);
    check(own == 1 && sibling == 0, "numeric entry commits to its original band before selection changes");
}
}

int main(int argc, char** argv)
{
    const char* only = argc > 1 ? argv[1] : "all";
    const auto run = [&](const char* name) { return std::strcmp(only, "all") == 0 || std::strcmp(only, name) == 0; };
    if (run("solo")) hiddenSolo();
    if (run("switch")) gestures(controls, true);
    if (run("drag")) gestures(controls, false);
    if (run("hardware-switch")) gestures(hardwareControls, true);
    if (run("hardware-drag")) gestures(hardwareControls, false);
    if (run("hardware-hidden")) hardwareHidden();
    if (run("editor-close")) editorClosedMidDrag();
    if (run("hidden")) hiddenControls();
    if (run("output")) outputAccess();
    if (run("pull-sc")) vcaPullScFirstValue();
    if (run("drawer")) settingsDrawer();
    if (run("drawer-keys")) drawerKeysAndCorner();
    if (run("numeric")) numericOwnership();
    if (run("restore")) restoredFace();
    std::printf("modern UI failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
