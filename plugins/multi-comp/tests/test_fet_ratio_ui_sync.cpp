#include <JuceHeader.h>

#include "../AnalogLookAndFeel.h"
#include "../UniversalCompressor.h"

#include <iostream>
#include <memory>

// Issue #326: the FET ratio buttons showed 4:1 every time the editor opened,
// whatever fet_ratio held, because nothing ever read the parameter back into
// them. Both FET modes share the buttons, so both are checked.
class MultiCompFetRatioSyncTest final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiCompFetRatioSyncTest"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void shutdown() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {}

    void initialise(const juce::String&) override
    {
        UniversalCompressor processor;
        auto& params = processor.getParameters();
        auto* mode = params.getParameter("mode");
        auto* ratio = params.getParameter("fet_ratio");

        if (mode == nullptr || ratio == nullptr)
        {
            fail("mode and fet_ratio parameters exist");
            finish();
            return;
        }

        constexpr int vintageFet = 1;
        constexpr int studioFet = 4;

        struct OpenCase { int mode; int ratio; const char* name; };
        for (const auto& c : { OpenCase { studioFet, 2, "Studio FET opens showing the stored 12:1" },
                               OpenCase { vintageFet, 3, "Vintage FET opens showing the stored 20:1" } })
        {
            setIndex(*mode, c.mode);
            setIndex(*ratio, c.ratio);

            auto editor = openEditor(processor);
            auto* buttons = editor != nullptr ? findRatioButtons(*editor) : nullptr;
            check(c.name, buttons != nullptr && buttons->getSelectedRatio() == c.ratio);
            closeEditor(processor, editor);
        }

        setIndex(*mode, studioFet);
        auto editor = openEditor(processor);
        auto* buttons = editor != nullptr ? findRatioButtons(*editor) : nullptr;

        if (buttons == nullptr)
        {
            fail("the editor has FET ratio buttons");
            closeEditor(processor, editor);
            finish();
            return;
        }

        // Host automation or a preset load while the window is open.
        setIndex(*ratio, 4);
        check("a host change while the editor is open moves the buttons", buttons->getSelectedRatio() == 4);

        // A click must reach the host as one complete gesture, or touch and
        // latch automation recording miss it.
        GestureCounter gestures;
        ratio->addListener(&gestures);
        buttons->setSelectedRatio(1, juce::sendNotificationSync);
        ratio->removeListener(&gestures);
        check("a click writes 8:1 to the parameter", getIndex(*ratio) == 1);
        check("a click is one complete host gesture", gestures.begins == 1 && gestures.ends == 1);

        // The issue's repro: close the window and open it again.
        closeEditor(processor, editor);
        editor = openEditor(processor);
        buttons = editor != nullptr ? findRatioButtons(*editor) : nullptr;
        check("reopening the editor keeps the clicked 8:1",
              buttons != nullptr && buttons->getSelectedRatio() == 1);
        closeEditor(processor, editor);

        finish();
    }

private:
    struct GestureCounter final : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool gestureIsStarting) override
        {
            ++(gestureIsStarting ? begins : ends);
        }

        int begins = 0;
        int ends = 0;
    };

    int failures = 0;

    static void setIndex(juce::RangedAudioParameter& parameter, int index)
    {
        parameter.setValueNotifyingHost(parameter.convertTo0to1(static_cast<float>(index)));
    }

    static int getIndex(const juce::RangedAudioParameter& parameter)
    {
        return juce::roundToInt(parameter.convertFrom0to1(parameter.getValue()));
    }

    static std::unique_ptr<juce::AudioProcessorEditor> openEditor(UniversalCompressor& processor)
    {
        return std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    }

    static void closeEditor(UniversalCompressor& processor, std::unique_ptr<juce::AudioProcessorEditor>& editor)
    {
        if (editor == nullptr)
            return;

        processor.editorBeingDeleted(editor.get());
        editor.reset();
    }

    static RatioButtonGroup* findRatioButtons(juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* group = dynamic_cast<RatioButtonGroup*>(child))
                return group;

            if (auto* group = findRatioButtons(*child))
                return group;
        }

        return nullptr;
    }

    void check(const char* name, bool condition)
    {
        std::cout << (condition ? "  PASS  " : "  FAIL  ") << name << '\n';

        if (!condition)
            ++failures;
    }

    void fail(const char* name)
    {
        check(name, false);
    }

    void finish()
    {
        std::cout << "\n  RESULT: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
        setApplicationReturnValue(failures == 0 ? 0 : 1);
        quit();
    }
};

START_JUCE_APPLICATION(MultiCompFetRatioSyncTest)
