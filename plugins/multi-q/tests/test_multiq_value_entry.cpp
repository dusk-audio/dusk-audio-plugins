// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// #165 / #359: typing a value into a Multi-Q knob must never run a callback that
// has already been destroyed, must never put a non-finite value into a
// parameter, and must never write the value into a band other than the one it
// was typed for.
//
// Drives the real path end to end, on the real message loop: the processor and
// its editor, a double-click on knobs (the band detail panel's FREQ / GAIN / Q /
// PAN with every input, then every visible DuskSlider in every EQ mode),
// characters typed into the popup TextEditor, and then each way the field can
// close: Return, Escape, focus loss, two of them queued together, the host
// closing the editor window before or during the edit, a band change while the
// field is open, and a second double-click while the first field is still
// waiting for its deferred delete. JUCE dispatches all of these through
// TextEditor::handleCommandMessage, which calls onReturnKey / onFocusLost /
// onEscapeKey IN PLACE; the pre-0.10.8 ValueEditor cleared those std::functions
// from inside one of them (#165).
//
// What a run proves, and where:
//   * The wrong-band, non-finite-value, range and exact-value checks are
//     deterministic in any build.
//   * The #165 use-after-free is only PROVEN absent by an ASan build. Measured
//     with libc++ on macOS against the v0.10.7 sources: ASan reports a
//     heap-use-after-free on Return, Escape and focus loss; a plain macOS build
//     fails on the first trial's time-out instead (the stale pointer no longer
//     resolves, so the field is never deleted). Other allocators were not
//     measured; a plain build there may pass with the defect present.
//   * Registered with CTest on macOS and Linux (not Windows, see CMakeLists).
//     No CI job runs it.
//
// It never shows a window or takes keyboard focus (the editor is never added
// to the desktop; the last step checks both), and CTest gives it a private
// home so the editor's saved window size stays out of the user's settings
// (the folder actually used is printed first). macOS-specific: that private
// home is CFFIXED_USER_HOME (Linux: HOME; Windows: none, JUCE asks the shell
// for the AppData folder), and the plain-build time-out behaviour above. On
// Linux it needs an X display (exit 77 = skip).
//
// Output: one "<section>: N fields opened (expected N)" line per section, then
// "MultiQValueEntryTest: F fields opened and closed, C checks, X failures";
// exit 0 only when X is 0 and every section opened what it expected. Each
// failure is a "FAIL:" line on stderr. `--close N` runs one close path only
// (`--help` lists them).

#include "../MultiQ.h"
#include "../MultiQEditor.h"
#include "../BandDetailPanel.h"
#include "../BandStripComponent.h"
#include "DuskLookAndFeel.h"

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <typeinfo>
#include <vector>

namespace
{
int checks = 0, failures = 0, fieldsOpened = 0;
[[maybe_unused]] constexpr int skipReturnCode = 77;   // matches SKIP_RETURN_CODE in CMakeLists (Linux only)

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::fprintf (stderr, "FAIL: %s\n", what.toRawUTF8());
    }
}

bool near (double got, double want) { return std::abs (got - want) <= 1.0e-3 * juce::jmax (1.0, std::abs (want)); }

// Visible flags from `c` up to, but not including, `root`. The editor itself is
// never on screen here, so its own flag says nothing about what a user sees.
bool visibleBelow (juce::Component& c, juce::Component& root)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
    {
        if (p == &root)
            return true;
        if (! p->isVisible())
            return false;
    }
    return false;
}

template <typename T>
void collect (juce::Component& c, std::vector<T*>& out)
{
    for (auto* child : c.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (child))
            out.push_back (t);
        collect (*child, out);
    }
}

template <typename T>
T* findFirst (juce::Component& root)
{
    std::vector<T*> all;
    collect (root, all);
    return all.empty() ? nullptr : all.front();
}

// Every value-editor TextEditor that ValueEditor::popUp has parented to `top`
// for `slider`, newest last.
std::vector<juce::TextEditor*> valueEditorsFor (juce::Component& top, const juce::Slider& slider)
{
    std::vector<juce::TextEditor*> found;
    const auto tag = reinterpret_cast<juce::int64> (&slider);
    for (auto* c : top.getChildren())
        if (auto* te = dynamic_cast<juce::TextEditor*> (c))
            if (static_cast<juce::int64> (te->getProperties().getWithDefault ("dusk_valeditor_for", juce::var (juce::int64 (0)))) == tag)
                found.push_back (te);
    return found;
}

int countValueEditors (juce::Component& top)
{
    int n = 0;
    for (auto* c : top.getChildren())
        if (c->getProperties().contains ("dusk_valeditor_for"))
            ++n;
    return n;
}

juce::MouseEvent doubleClickOn (juce::Component& c)
{
    const auto now = juce::Time::getCurrentTime();
    const auto centre = c.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), centre, {},
                             juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation,
                             juce::MouseInputSource::defaultRotation,
                             juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY,
                             &c, &c, now, centre, now, 2, false);
}

void typeInto (juce::TextEditor& te, const juce::String& text)
{
    te.selectAll();   // setSelectAllWhenFocused: the user types over the pre-filled value
    if (text.isEmpty())
        te.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    for (auto ch : text)
        te.keyPressed (juce::KeyPress (0, juce::ModifierKeys(), ch));
}

enum class Close
{
    returnKey,
    escapeKey,
    focusLoss,
    returnThenFocusLoss,
    focusLossThenReturn,
    returnThenWindowClosed,   // host closes the editor before the posted Return is handled
    windowClosedWhileTyping,  // host closes the editor; the orphaned field then loses focus
    bandChangeThenReturn,     // the band selection changes under an open field
    secondDoubleClickWhileClosing
};

const char* closeName (Close c)
{
    switch (c)
    {
        case Close::returnKey:                     return "Return";
        case Close::escapeKey:                     return "Escape";
        case Close::focusLoss:                     return "focus loss";
        case Close::returnThenFocusLoss:           return "Return + focus loss";
        case Close::focusLossThenReturn:           return "focus loss + Return";
        case Close::returnThenWindowClosed:        return "Return, then window closed";
        case Close::windowClosedWhileTyping:       return "window closed while typing";
        case Close::bandChangeThenReturn:          return "band change, then Return";
        case Close::secondDoubleClickWhileClosing: return "second double-click while closing";
    }
    return "?";
}

const Close allCloses[] = { Close::returnKey, Close::escapeKey, Close::focusLoss,
                            Close::returnThenFocusLoss, Close::focusLossThenReturn,
                            Close::returnThenWindowClosed, Close::windowClosedWhileTyping,
                            Close::bandChangeThenReturn, Close::secondDoubleClickWhileClosing };
constexpr int numCloses = (int) std::size (allCloses);

// --close N runs only that close path (index into allCloses) in the sweeps and
// skips the sections that use fixed closes. Under ASan, which stops at the first
// error, that is how each path of a pre-0.10.8 build is seen on its own.
int onlyClose = -1;
bool closeSelected (int c) { return onlyClose < 0 || onlyClose == c; }

// Adversarial typed text: units, locale commas, signs, empty, overflow, and
// the literals JUCE's number parser maps to NaN and infinity.
const char* inputs[] = { "1k", "1.5 kHz", "250 Hz", "3dB", "-3.5 dB", "50%", "L50", "R100", "C",
                         "1,5", "-5", "0", "0.0001", "1e30", "-1e30", "1e400", "nan", "-nan",
                         "inf", "-inf", "", " ", "abc", "12abc", "--1", "1e-45",
                         "99999999999999999999999999999999999999999999" };
constexpr int numInputs = (int) std::size (inputs);

// The 1-based band a parameter belongs to ("band4_freq" -> 4), or 0.
int bandOfParameter (const juce::String& id)
{
    if (! id.startsWith ("band"))
        return 0;
    const auto digits = id.substring (4).initialSectionContainingOnly ("0123456789");
    return digits.isEmpty() ? 0 : digits.getIntValue();
}

//==============================================================================
// A tiny cooperative scheduler on the real message loop. Each step returns
// true when it is done; a step that returns false is polled again on the next
// timer tick, so asynchronous JUCE work (posted command messages, AsyncUpdater,
// callAsync deletes) runs between polls exactly as it would in a host. A step
// may add steps; those run right after it (used to expand a plan from what the
// editor actually shows).
class Runner : private juce::Timer
{
public:
    using Step = std::function<bool()>;

    void add (juce::String name, Step step)
    {
        (running ? inserted : steps).push_back ({ std::move (name), std::move (step) });
    }

    void start (std::function<void()> whenDone) { done = std::move (whenDone); startTimer (1); }

private:
    struct Entry { juce::String name; Step step; };
    std::deque<Entry> steps, inserted;
    std::function<void()> done;
    bool running = false;
    juce::uint32 waitingSince = 0;   // 0 = the front step has not had to wait yet

    void timerCallback() override
    {
        // Several steps per tick keeps the run short; a waiting step yields the tick.
        for (int i = 0; i < 64 && ! steps.empty(); ++i)
        {
            running = true;
            const bool finished = steps.front().step();
            running = false;
            if (! finished)
            {
                inserted.clear();   // a step that adds steps must finish in the same call
                // A field that never closes is a failure, and it poisons every
                // later trial (popUp re-focuses it instead of opening a new
                // one), so the run stops here rather than report a cascade.
                // Wall-clock, so a coarse timer (Windows ticks at ~16 ms) does
                // not stretch it.
                const auto now = juce::Time::getMillisecondCounter();
                if (waitingSince == 0)
                    waitingSince = now;
                else if (now - waitingSince > 5000)
                {
                    check (false, "timed out waiting: " + steps.front().name + " (run stopped)");
                    steps.clear();
                }
                return;
            }
            steps.pop_front();
            steps.insert (steps.begin(), inserted.begin(), inserted.end());
            inserted.clear();
            waitingSince = 0;
        }
        if (steps.empty())
        {
            stopTimer();
            if (done) done();
        }
    }
};

//==============================================================================
struct Fixture
{
    std::unique_ptr<MultiQ> proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    Fixture()
    {
        proc = std::make_unique<MultiQ>();
        auto layouts = proc->getBusesLayout();
        layouts.getMainInputChannelSet()  = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        proc->setBusesLayout (layouts);
        proc->setPlayConfigDetails (2, 2, 48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        openEditor();
    }

    ~Fixture() { closeEditor(); }

    void openEditor()
    {
        if (editor == nullptr)
            editor.reset (proc->createEditor());
    }

    void closeEditor()
    {
        if (editor != nullptr)
        {
            proc->editorBeingDeleted (editor.get());
            editor.reset();
        }
    }

    void setParam (const juce::String& id, float plain)
    {
        if (auto* p = proc->parameters.getParameter (id))
            p->setValueNotifyingHost (proc->parameters.getParameterRange (id).convertTo0to1 (plain));
    }

    double param (const juce::String& id) const { return proc->parameters.getRawParameterValue (id)->load(); }

    std::vector<float> snapshot() const
    {
        std::vector<float> values;
        for (auto* p : proc->getParameters())
            values.push_back (p->getValue());
        return values;
    }

    // Audio through the processor with whatever the edit left behind.
    bool processedAudioIsFinite()
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, 0.25f * std::sin (0.07f * (float) (i + phase)));
        phase += 512;
        juce::MidiBuffer midi;
        proc->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                if (! std::isfinite (buffer.getSample (ch, i)))
                    return false;
        return true;
    }

    // Draw the whole editor, so drawing code meets the edited values too.
    void paintEditor()
    {
        if (editor == nullptr)
            return;
        juce::Image image (juce::Image::ARGB, juce::jmax (1, editor->getWidth()), juce::jmax (1, editor->getHeight()), true);
        juce::Graphics g (image);
        editor->paintEntireComponent (g, false);
    }

    // The first parameter holding a non-finite value, or nullptr.
    juce::AudioProcessorParameter* firstNonFiniteParameter() const
    {
        for (auto* p : proc->getParameters())
            if (! std::isfinite (p->getValue()))
                return p;
        return nullptr;
    }

    // Select a band the way a click on the band strip does, so the editor and
    // the detail panel both follow.
    void selectBand (int index)
    {
        if (editor == nullptr || selectedBand() == index + 1)
            return;
        if (auto* strip = findFirst<BandStripComponent> (*editor))
        {
            // A fresh editor can leave the strip and the detail panel disagreeing;
            // the strip only reports a change, so make it one.
            if (strip->getSelectedBand() == index)
                strip->setSelectedBand (-1);
            strip->setSelectedBand (index);
        }
    }

    int selectedBand() const   // 1-based, 0 if unknown
    {
        if (editor != nullptr)
            if (auto* panel = findFirst<BandDetailPanel> (*editor))
                return panel->getSelectedBand() + 1;
        return 0;
    }

    int phase = 0;
};

void checkParametersFinite (Fixture& fx, const juce::String& name)
{
    auto* bad = fx.firstNonFiniteParameter();
    check (bad == nullptr, name + ": every parameter is finite"
                               + (bad != nullptr ? " (not '" + bad->getName (64) + "')" : juce::String()));
}

// No parameter of a band other than `typedBand` may have moved since `before`.
void checkNoOtherBandChanged (Fixture& fx, const std::vector<float>& before, int typedBand, const juce::String& name)
{
    const auto& params = fx.proc->getParameters();
    for (int i = 0; i < params.size() && i < (int) before.size(); ++i)
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]);
        if (withId == nullptr)
            continue;
        const int band = bandOfParameter (withId->paramID);
        if (band != 0 && band != typedBand && ! juce::exactlyEqual (params[i]->getValue(), before[(size_t) i]))
            check (false, name + ": typed for band " + juce::String (typedBand) + " but '"
                              + withId->paramID + "' changed");
    }
}

// Counts the fields a block of the plan opens, and fails unless it is exactly
// what the plan said: a section that silently skips is a failure.
void addSectionStart (Runner& runner, std::shared_ptr<int> start)
{
    runner.add ("section start", [start] { *start = fieldsOpened; return true; });
}

void addSectionEnd (Runner& runner, const juce::String& section, std::shared_ptr<int> start, int expected)
{
    runner.add (section + ": field count", [section, start, expected] {
        const int opened = fieldsOpened - *start;
        std::printf ("%s: %d fields opened (expected %d)\n", section.toRawUTF8(), opened, expected);
        check (expected > 0 && opened == expected,
               section + ": opened " + juce::String (opened) + " fields, expected " + juce::String (expected));
        return true;
    });
}

//==============================================================================
// One slider, one typed text, one way of closing the field.
void addSliderTrial (Runner& runner, Fixture& fx, std::function<juce::Slider*()> findSlider,
                     juce::String label, juce::String text, Close close,
                     std::function<void (double before, double after)> expect = {})
{
    struct State
    {
        juce::Component::SafePointer<juce::Slider> slider;
        juce::Component::SafePointer<juce::TextEditor> field;
        double before = 0.0;
        int typedBand = 0;
        std::vector<float> paramsBefore;
        bool failed = false;
    };
    auto st = std::make_shared<State>();
    const auto name = label + " <- \"" + text + "\" (" + closeName (close) + ")";

    runner.add (name + ": previous field gone", [&fx] {
        fx.openEditor();
        return countValueEditors (*fx.editor) == 0;
    });

    runner.add (name + ": open", [&fx, st, findSlider, name, text, close] {
        // A freshly opened editor (after a window-close trial) has no band
        // selected, which leaves the band detail knobs attached to nothing.
        if (fx.selectedBand() == 0)
            fx.selectBand (3);
        auto* slider = findSlider();
        check (slider != nullptr, name + ": the knob the plan names exists");
        if (slider == nullptr)
        {
            st->failed = true;
            return true;
        }
        st->slider = slider;
        st->before = slider->getValue();
        st->typedBand = fx.selectedBand();
        st->paramsBefore = fx.snapshot();

        auto& top = *fx.editor;
        const auto beforeCount = valueEditorsFor (top, *slider).size();
        static_cast<juce::Component&> (*slider).mouseDoubleClick (doubleClickOn (*slider));
        const auto editors = valueEditorsFor (top, *slider);
        check (editors.size() == beforeCount + 1, name + ": double-click opened a field");
        if (editors.empty())
        {
            st->failed = true;
            return true;
        }

        auto* te = editors.back();
        st->field = te;
        ++fieldsOpened;
        check (te->getText() == slider->getTextFromValue (slider->getValue()),
               name + ": field pre-filled with the displayed value");
        typeInto (*te, text);

        switch (close)
        {
            case Close::returnKey:
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                break;
            case Close::escapeKey:
                te->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                break;
            case Close::focusLoss:
                te->focusLost (juce::Component::focusChangedByMouseClick);
                break;
            case Close::returnThenFocusLoss:
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                te->focusLost (juce::Component::focusChangedByMouseClick);
                break;
            case Close::focusLossThenReturn:
                te->focusLost (juce::Component::focusChangedByMouseClick);
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                break;
            case Close::returnThenWindowClosed:
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                fx.closeEditor();   // the field is orphaned, its Return still queued
                break;
            case Close::windowClosedWhileTyping:
                fx.closeEditor();
                // A host tearing down the window removes the focused field from
                // its parent, which hands focus away: the orphan sees focusLost.
                if (auto* orphan = st->field.getComponent())
                    orphan->focusLost (juce::Component::focusChangedDirectly);
                break;
            case Close::bandChangeThenReturn:
                check (st->typedBand > 0, name + ": a band was selected when the field opened");
                fx.selectBand ((st->typedBand - 1 + 3) % 8);
                check (fx.selectedBand() == (st->typedBand - 1 + 3) % 8 + 1, name + ": the band selection really changed");
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                break;
            case Close::secondDoubleClickWhileClosing:
            {
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                // Let the Return be handled, then double-click again while the
                // first field is still waiting for its deferred delete.
                juce::MessageManager::callAsync ([&fx, st] {
                    auto* s = st->slider.getComponent();
                    if (s == nullptr || fx.editor == nullptr)
                        return;
                    static_cast<juce::Component&> (*s).mouseDoubleClick (doubleClickOn (*s));
                    auto again = valueEditorsFor (*fx.editor, *s);
                    if (! again.empty())
                    {
                        auto* second = again.back();
                        typeInto (*second, "2");
                        second->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                    }
                });
                break;
            }
        }
        return true;
    });

    runner.add (name + ": field closed and deleted", [&fx, st, close] {
        if (st->failed)
            return true;
        if (st->field != nullptr)
            return false;   // the deferred delete has not run yet
        if (close == Close::returnThenWindowClosed || close == Close::windowClosedWhileTyping)
            fx.openEditor();
        return fx.editor != nullptr && countValueEditors (*fx.editor) == 0;
    });

    runner.add (name + ": value checks", [&fx, st, name, close, expect, findSlider] {
        if (st->failed)
            return true;
        // The slider that was edited, or (after a window close) its replacement.
        auto* slider = st->slider.getComponent();
        const bool sameSlider = slider != nullptr;
        if (slider == nullptr)
            slider = findSlider();
        if (slider != nullptr)
        {
            const double after = slider->getValue();
            check (std::isfinite (after), name + ": slider value is finite");
            check (after >= slider->getMinimum() && after <= slider->getMaximum(),
                   name + ": slider value within range");
            if (sameSlider && close == Close::escapeKey)
                check (juce::exactlyEqual (after, st->before), name + ": Escape leaves the value alone");
            if (sameSlider && expect && (close == Close::returnKey || close == Close::focusLoss))
                expect (st->before, after);
        }
        checkNoOtherBandChanged (fx, st->paramsBefore, st->typedBand, name);
        checkParametersFinite (fx, name);
        check (fx.processedAudioIsFinite(), name + ": audio output is finite");
        fx.paintEditor();
        return true;
    });
}

// #359 follow-up: type into band 4's field, then select band 7 around the
// field's posted commit. Band 7 must never move. In the two orders a click or
// a key can produce (focus loss then select, select then Return) the value
// must land in band 4, exactly. The third order selects band 7 after the
// field's commit has run but before the slider's own asynchronous notification
// reaches its attachment; there band 4 may keep its old value (the typed value
// is lost, which is printed), but it must never take a third value.
enum class BandOrder { focusLossThenSelect, selectThenReturn, selectBetweenCommitAndNotify };

void addWrongBandTrial (Runner& runner, Fixture& fx, std::function<juce::Slider*()> findKnob,
                        juce::String label, std::function<juce::String (int)> paramFor,
                        juce::String text, double want, float typedStart, float otherStart,
                        BandOrder order)
{
    constexpr int typedBand = 4, otherBand = 7;   // 1-based; both have every knob attached
    const auto name = label + " typed for band 4, band 7 selected "
                    + (order == BandOrder::focusLossThenSelect ? "after the focus loss"
                       : order == BandOrder::selectThenReturn  ? "before Return"
                                                               : "between commit and notification");
    auto field = std::make_shared<juce::Component::SafePointer<juce::TextEditor>>();
    auto failed = std::make_shared<bool> (false);

    runner.add (name + ": previous field gone", [&fx] {
        fx.openEditor();
        return countValueEditors (*fx.editor) == 0;
    });

    runner.add (name + ": open", [&fx, findKnob, paramFor, name, text, typedStart, otherStart,
                                  order, field, failed] {
        fx.selectBand (typedBand - 1);
        fx.setParam (paramFor (typedBand), typedStart);
        fx.setParam (paramFor (otherBand), otherStart);
        auto* knob = findKnob();
        check (knob != nullptr && fx.selectedBand() == typedBand, name + ": band 4 knob found");
        if (knob == nullptr)
        {
            *failed = true;
            return true;
        }
        static_cast<juce::Component&> (*knob).mouseDoubleClick (doubleClickOn (*knob));
        auto editors = valueEditorsFor (*fx.editor, *knob);
        check (! editors.empty(), name + ": double-click opened a field");
        if (editors.empty())
        {
            *failed = true;
            return true;
        }
        auto* te = editors.back();
        *field = te;
        ++fieldsOpened;
        typeInto (*te, text);

        switch (order)
        {
            case BandOrder::focusLossThenSelect:
                // A click on another band: the field loses focus first (which
                // only posts its commit), then the click selects the band.
                te->focusLost (juce::Component::focusChangedByMouseClick);
                fx.selectBand (otherBand - 1);
                break;
            case BandOrder::selectThenReturn:
                fx.selectBand (otherBand - 1);
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                break;
            case BandOrder::selectBetweenCommitAndNotify:
                // Queued behind the posted Return: runs after the commit, ahead
                // of the slider update that the commit itself queues.
                te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                juce::MessageManager::callAsync ([&fx] { fx.selectBand (otherBand - 1); });
                break;
        }
        return true;
    });

    runner.add (name + ": field closed and deleted", [field, failed] {
        return *failed || field->getComponent() == nullptr;
    });

    runner.add (name + ": value checks", [&fx, findKnob, paramFor, name, want, typedStart, otherStart, order, failed] {
        if (*failed)
            return true;
        const double typed = fx.param (paramFor (typedBand));
        const double other = fx.param (paramFor (otherBand));
        if (order == BandOrder::selectBetweenCommitAndNotify)
        {
            std::printf ("%s: band 4 %s\n", name.toRawUTF8(),
                         near (typed, want) ? "took the typed value" : "kept its old value (typed value lost)");
            check (near (typed, want) || near (typed, typedStart),
                   name + ": band 4 holds its old or the typed value (got " + juce::String (typed) + ")");
        }
        else
            check (near (typed, want), name + ": band 4 holds the typed value (expected "
                                           + juce::String (want) + ", got " + juce::String (typed) + ")");
        check (near (other, otherStart), name + ": band 7 unchanged (expected "
                                       + juce::String (otherStart) + ", got " + juce::String (other) + ")");
        check (fx.selectedBand() == otherBand, name + ": band 7 is selected afterwards");
        if (auto* knob = findKnob())
            check (near (knob->getValue(), otherStart), name + ": the knob now shows band 7");
        return true;
    });
}

// The band strip's own double-click-to-edit labels (juce::Label, not
// ValueEditor). `tooltip` is "Frequency", "Gain" or "Q"; `band` is 1-based and
// must be a parametric band (2-7). Bands 1 and 8 show a slope box instead of a
// gain label, so the n-th "Gain" label belongs to band n + 1.
void addStripTrial (Runner& runner, Fixture& fx, juce::String tooltip, int band, juce::String text)
{
    const int column = tooltip == "Gain" ? band - 2 : band - 1;
    const auto name = "band strip " + tooltip + " (band " + juce::String (band) + ") <- \"" + text + "\"";
    auto field = std::make_shared<juce::Component::SafePointer<juce::TextEditor>>();

    // The strip refreshes its labels on a later message after every parameter
    // change, and Label::setText closes an open editor WITHOUT committing it.
    // Let the previous trial's refresh land first, or it would discard this one.
    auto ticks = std::make_shared<int> (0);
    runner.add (name + ": settle", [ticks] { return ++(*ticks) > 3; });

    runner.add (name + ": open", [&fx, tooltip, column, text, name, field] {
        fx.openEditor();
        auto* strip = findFirst<BandStripComponent> (*fx.editor);
        check (strip != nullptr, name + ": band strip found");
        if (strip == nullptr)
            return true;
        std::vector<juce::Label*> all, matching;
        collect (*strip, all);
        for (auto* l : all)
            if (l->getTooltip() == tooltip)
                matching.push_back (l);
        check (column < (int) matching.size(), name + ": label exists");
        if (column >= (int) matching.size())
            return true;
        auto* label = matching[(size_t) column];
        label->showEditor();
        auto* te = label->getCurrentTextEditor();
        check (te != nullptr, name + ": label opened its editor");
        if (te == nullptr)
            return true;
        *field = te;
        ++fieldsOpened;
        typeInto (*te, text);
        te->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        return true;
    });

    runner.add (name + ": closed", [field] { return field->getComponent() == nullptr; });

    runner.add (name + ": value checks", [&fx, name] {
        checkParametersFinite (fx, name);
        check (fx.processedAudioIsFinite(), name + ": audio output is finite");
        fx.paintEditor();
        return true;
    });
}

//==============================================================================
std::function<juce::Slider*()> detailKnob (Fixture& fx, const char* tooltipPrefix)
{
    return [&fx, tooltipPrefix]() -> juce::Slider* {
        if (fx.editor == nullptr)
            return nullptr;
        auto* panel = findFirst<BandDetailPanel> (*fx.editor);
        if (panel == nullptr)
            return nullptr;
        std::vector<DuskSlider*> knobs;
        collect (*panel, knobs);
        for (auto* k : knobs)
            if (k->getTooltip().startsWith (tooltipPrefix))
                return k;
        return nullptr;
    };
}

// A DuskSlider's identity across editor instances: its parent's type, name and
// tooltip, plus its position among sliders with the same three. Positions in
// the child list are not stable (panels call toFront()), these are.
juce::String sliderKey (juce::Component& root, DuskSlider& slider)
{
    auto base = [] (DuskSlider& s) {
        const auto* parent = s.getParentComponent();
        return juce::String (parent != nullptr ? typeid (*parent).name() : "?")
             + "|" + s.getName() + "|" + s.getTooltip().substring (0, 40);
    };
    std::vector<DuskSlider*> all;
    collect (root, all);
    int occurrence = 0;
    for (auto* s : all)
    {
        if (s == &slider)
            break;
        if (base (*s) == base (slider))
            ++occurrence;
    }
    return base (slider) + "#" + juce::String (occurrence);
}

std::function<juce::Slider*()> sliderByKey (Fixture& fx, juce::String key)
{
    return [&fx, key]() -> juce::Slider* {
        if (fx.editor == nullptr)
            return nullptr;
        std::vector<DuskSlider*> all;
        collect (*fx.editor, all);
        for (auto* s : all)
            if (sliderKey (*fx.editor, *s) == key)
                return s;
        return nullptr;
    };
}

void addModeSwitch (Runner& runner, Fixture& fx, int eqType)
{
    auto ticks = std::make_shared<int> (0);
    runner.add ("switch to EQ type " + juce::String (eqType), [&fx, eqType, ticks] {
        if (*ticks == 0)
            fx.setParam (ParamIDs::eqType, (float) eqType);
        return ++(*ticks) > 5;   // the editor applies the mode on a later message
    });
}

void addSelectBand (Runner& runner, Fixture& fx, int index)
{
    runner.add ("select band " + juce::String (index + 1), [&fx, index] {
        fx.selectBand (index);
        return true;
    });
}

// Every DuskSlider the editor shows in this mode, each closed every way. The
// set is read from the editor when the section starts, printed, and the
// section fails unless every one of them opened a field.
// How many DuskSliders the editor shows in each EQ mode, with band 4 selected
// before the switch. Pinned so a UI regression that hides a knob (or an
// enumeration that stops finding one) fails instead of shrinking the plan.
// Digital counts the detail panel's 9 knobs, its two match sliders and the
// legacy gain slider (both visible but 0 x 0, never laid out).
constexpr int kVisibleKnobsDigital = 12;
constexpr int kVisibleKnobsMatch   = 2;
constexpr int kVisibleKnobsBritish = 15;
constexpr int kVisibleKnobsTube    = 11;

int pinnedKnobCount (int eqType, const char*& constantName)
{
    switch (eqType)
    {
        case (int) EQType::Digital: constantName = "kVisibleKnobsDigital"; return kVisibleKnobsDigital;
        case (int) EQType::Match:   constantName = "kVisibleKnobsMatch";   return kVisibleKnobsMatch;
        case (int) EQType::British: constantName = "kVisibleKnobsBritish"; return kVisibleKnobsBritish;
        case (int) EQType::Tube:    constantName = "kVisibleKnobsTube";    return kVisibleKnobsTube;
        default:                    constantName = "?";                    return -1;
    }
}

void addModeSection (Runner& runner, Fixture& fx, int eqType)
{
    // The Digital count depends on the selected band (filters have no gain
    // knob), so fix it before the switch, as the pinned counts assume.
    addSelectBand (runner, fx, 3);
    addModeSwitch (runner, fx, eqType);
    runner.add ("mode " + juce::String (eqType) + ": expand", [&runner, &fx, eqType] {
        std::vector<DuskSlider*> all;
        collect (*fx.editor, all);
        std::vector<juce::String> keys;
        for (auto* s : all)
            if (visibleBelow (*s, *fx.editor))
            {
                keys.push_back (sliderKey (*fx.editor, *s));
                std::printf ("  mode %d knob: %s  bounds %s\n", eqType, keys.back().toRawUTF8(),
                             s->getBounds().toString().toRawUTF8());
            }

        const bool digital = eqType == (int) EQType::Digital;
        const auto section = "mode " + juce::String (eqType) + " (" + juce::String ((int) keys.size()) + " visible knobs)";
        const char* constantName = nullptr;
        const int pinned = pinnedKnobCount (eqType, constantName);
        check ((int) keys.size() == pinned,
               section + ": expected " + juce::String (pinned) + " visible knobs. If the UI changed on purpose, set "
                   + constantName + " in test_multiq_value_entry.cpp to the number of knobs listed above.");

        // Band changes only exist in Digital mode.
        auto runsHere = [digital] (int c) { return closeSelected (c) && (digital || allCloses[c] != Close::bandChangeThenReturn); };
        int closesHere = 0;
        for (int c = 0; c < numCloses; ++c)
            closesHere += runsHere (c) ? 1 : 0;
        if (closesHere == 0)
        {
            std::printf ("%s: skipped by --close\n", section.toRawUTF8());
            return true;
        }

        auto start = std::make_shared<int> (0);
        addSectionStart (runner, start);
        for (size_t k = 0; k < keys.size(); ++k)
            for (int c = 0; c < numCloses; ++c)
                if (runsHere (c))
                    addSliderTrial (runner, fx, sliderByKey (fx, keys[k]),
                                    "mode " + juce::String (eqType) + " " + keys[k],
                                    inputs[((int) k * 7 + c) % numInputs], allCloses[c]);
        addSectionEnd (runner, section, start, pinned * closesHere);
        return true;
    });
}

void buildPlan (Runner& runner, Fixture& fx)
{
    struct Knob { const char* tooltip; const char* label; };
    const Knob knobs[] = { { "Frequency:", "FREQ" }, { "Gain:", "GAIN" }, { "Q:", "Q" },
                           { "Pan band", "PAN" } };

    int closesSelected = 0;
    for (int c = 0; c < numCloses; ++c)
        closesSelected += closeSelected (c) ? 1 : 0;

    // 1. The reporter's controls, every input, every way of closing. Starts on
    //    a parametric band; the band-change closes then walk the selection.
    {
        auto start = std::make_shared<int> (0);
        addModeSwitch (runner, fx, (int) EQType::Digital);
        addSelectBand (runner, fx, 3);
        addSectionStart (runner, start);
        for (const auto& k : knobs)
            for (int c = 0; c < numCloses; ++c)
                if (closeSelected (c))
                    for (int i = 0; i < numInputs; ++i)
                        addSliderTrial (runner, fx, detailKnob (fx, k.tooltip), k.label, inputs[i], allCloses[c]);
        addSectionEnd (runner, "FREQ/GAIN/Q/PAN x every input x every close", start,
                       (int) std::size (knobs) * closesSelected * numInputs);
    }

    // 2. Typed text reaches the parameter as the user meant it (so the trials
    //    above cannot pass by never writing anything), and only in its own band.
    //    Uses fixed closes, so --close skips it.
    if (onlyClose < 0)
    {
        auto paramIs = [&fx] (juce::String id, double want) {
            return [&fx, id, want] (double, double) {
                const double got = fx.param (id);
                check (near (got, want), id + ": parameter expected " + juce::String (want) + ", got " + juce::String (got));
            };
        };
        auto start = std::make_shared<int> (0);
        addSelectBand (runner, fx, 3);
        addSectionStart (runner, start);
        addSliderTrial (runner, fx, detailKnob (fx, "Frequency:"), "FREQ", "1k",      Close::returnKey, paramIs (ParamIDs::bandFreq (4), 1000.0));
        addSliderTrial (runner, fx, detailKnob (fx, "Frequency:"), "FREQ", "1.5 kHz", Close::focusLoss, paramIs (ParamIDs::bandFreq (4), 1500.0));
        addSliderTrial (runner, fx, detailKnob (fx, "Gain:"),      "GAIN", "3dB",     Close::returnKey, paramIs (ParamIDs::bandGain (4), 3.0));
        addSliderTrial (runner, fx, detailKnob (fx, "Q:"),         "Q",    "2",       Close::focusLoss, paramIs (ParamIDs::bandQ (4), 2.0));
        addSliderTrial (runner, fx, detailKnob (fx, "Pan band"),   "PAN",  "-0.5",    Close::returnKey, paramIs (ParamIDs::bandPan (4), -0.5));
        addSliderTrial (runner, fx, detailKnob (fx, "Q:"),         "Q",    "nan",     Close::returnKey,
                        [] (double before, double after) { check (juce::exactlyEqual (after, before), "Q nan: value unchanged"); });

        for (auto order : { BandOrder::focusLossThenSelect, BandOrder::selectThenReturn,
                            BandOrder::selectBetweenCommitAndNotify })
        {
            addWrongBandTrial (runner, fx, detailKnob (fx, "Frequency:"), "FREQ", ParamIDs::bandFreq, "1k",   1000.0, 500.0f, 5000.0f, order);
            addWrongBandTrial (runner, fx, detailKnob (fx, "Gain:"),      "GAIN", ParamIDs::bandGain, "3dB",  3.0,    0.0f,  -6.0f,    order);
            addWrongBandTrial (runner, fx, detailKnob (fx, "Q:"),         "Q",    ParamIDs::bandQ,    "2",    2.0,    0.71f,  4.0f,    order);
            addWrongBandTrial (runner, fx, detailKnob (fx, "Pan band"),   "PAN",  ParamIDs::bandPan,  "-0.5", -0.5,   0.0f,   0.25f,   order);
        }
        addSectionEnd (runner, "exact values and wrong-band trials", start, 6 + 12);
    }
    else
        std::printf ("exact values and wrong-band trials: skipped by --close\n");

    // 3. Every visible knob in every mode (count derived from the editor).
    for (int eqType : { (int) EQType::Digital, (int) EQType::Match, (int) EQType::British, (int) EQType::Tube })
        addModeSection (runner, fx, eqType);

    // 4. The band strip's own labels (juce::Label, not ValueEditor; --close skips it).
    if (onlyClose < 0)
    {
        auto start = std::make_shared<int> (0);
        addModeSwitch (runner, fx, (int) EQType::Digital);
        addSectionStart (runner, start);
        for (const char* tooltip : { "Frequency", "Gain", "Q" })
            for (int i = 0; i < numInputs; ++i)
                addStripTrial (runner, fx, tooltip, 4, inputs[i]);
        addSectionEnd (runner, "band strip labels x every input", start, 3 * numInputs);
    }
    else
        std::printf ("band strip labels: skipped by --close\n");

    // 5. The run itself stayed off screen and out of the keyboard focus.
    runner.add ("no window, no focus", [&fx] {
        check (juce::Desktop::getInstance().getNumComponents() == 0, "no component was put on the desktop");
        check (fx.editor == nullptr || fx.editor->getPeer() == nullptr, "the editor has no window");
        check (juce::Component::getCurrentlyFocusedComponent() == nullptr, "nothing holds keyboard focus");
        return true;
    });
}
} // namespace

#if JUCE_MAC
namespace juce
{
// Defined in juce_MessageManager_mac.mm. JUCEApplicationBase::main calls it;
// without it NSApp is nil, runDispatchLoop() returns at once and nothing runs.
void initialiseNSApplication();
}
#endif

//==============================================================================
// A plain main with JUCE's GUI initialiser and its dispatch loop, so nothing
// depends on how juce_events was compiled (START_JUCE_APPLICATION needs
// JUCEApplicationBase::main, which a Windows build of the plug-in's shared code
// does not contain).
int main (int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--close" && i + 1 < argc)
            onlyClose = juce::String (argv[++i]).getIntValue();
        else
        {
            std::printf ("usage: MultiQValueEntryTest [--close N]\n  N:");
            for (int c = 0; c < numCloses; ++c)
                std::printf ("  %d = %s", c, closeName (allCloses[c]));
            std::printf ("\n");
            return 2;
        }
    }
    if (onlyClose >= numCloses)
    {
        std::printf ("--close must be 0..%d\n", numCloses - 1);
        return 2;
    }

   #if JUCE_LINUX
    if (juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isEmpty())
    {
        std::printf ("SKIP: no DISPLAY, the editor needs X\n");
        return skipReturnCode;
    }
   #endif

    juce::ScopedJuceInitialiser_GUI juceInit;
   #if JUCE_MAC
    juce::initialiseNSApplication();
   #endif

    // Where the editor will save its window size: CTest points this at a
    // private folder on macOS and Linux. Printed so a run can be checked.
    std::printf ("user data folder: %s\n",
                 juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getFullPathName().toRawUTF8());
    if (onlyClose >= 0)
        std::printf ("--close %d: %s only\n", onlyClose, closeName (allCloses[onlyClose]));

    bool planFinished = false;
    {
        Fixture fixture;
        Runner runner;   // declared after the fixture, so it stops first
        buildPlan (runner, fixture);
        runner.start ([&planFinished] {
            planFinished = true;
            juce::MessageManager::getInstance()->stopDispatchLoop();
        });
        juce::MessageManager::getInstance()->runDispatchLoop();
    }
    // A loop that returns early would otherwise pass with nothing tested.
    check (planFinished, "the message loop ran the whole plan");
    check (fieldsOpened > 0, "fields were opened at all");

    std::printf ("MultiQValueEntryTest: %d fields opened and closed, %d checks, %d failures\n",
                 fieldsOpened, checks, failures);
    return failures == 0 ? 0 : 1;
}
