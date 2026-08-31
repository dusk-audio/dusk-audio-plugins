#include <JuceHeader.h>

#include "../UniversalCompressor.h"

#include <cmath>
#include <memory>

class MultiCompEditorResizeTest final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiCompEditorResizeTest"; }
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
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());

        if (editor == nullptr)
        {
            fail("processor did not create an editor");
            finish();
            return;
        }

        constexpr int baseWidth = 750;
        constexpr int baseHeight = 500;
        constexpr double expectedAspect = static_cast<double>(baseWidth) / baseHeight;
        const auto initialBounds = editor->getBounds();

        auto* constrainer = editor->getConstrainer();
        check("editor exposes a resize constrainer", constrainer != nullptr);
        if (constrainer != nullptr)
        {
            check("host constrainer advertises the editor aspect ratio",
                  std::abs(constrainer->getFixedAspectRatio() - expectedAspect) < 0.0001);
        }

        // Reproduce the issue's REAPER/Linux gesture: move the top edge down
        // while dragging the right edge out. The bottom edge remains anchored.
        editor->setBounds(0, 0, baseWidth, baseHeight);
        editor->setBoundsConstrained({ 0, 150, 1125, 350 });

        check("host-edge resize preserves proportional bounds",
              editor->getWidth() == 1125 && editor->getHeight() == 750);
        check("top-edge resize keeps the bottom edge anchored", editor->getBottom() == baseHeight);

        // EnhancedCompressorEditor persists its size on destruction. Restore
        // the value loaded at startup so this test does not alter user state.
        editor->setBounds(initialBounds);
        processor.editorBeingDeleted(editor.get());
        editor.reset();
        finish();
    }

private:
    int failures = 0;

    void check(const char*, bool condition)
    {
        if (!condition)
            ++failures;
    }

    void fail(const char*)
    {
        ++failures;
    }

    void finish()
    {
        setApplicationReturnValue(failures == 0 ? 0 : 1);
        quit();
    }
};

START_JUCE_APPLICATION(MultiCompEditorResizeTest)
