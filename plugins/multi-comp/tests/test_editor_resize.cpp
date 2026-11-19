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
        const auto initialBounds = editor->getBounds();

        // The constrainer the editor carries is the one JUCE answers a host's
        // size queries from, so it must never enlarge what the host asked for.
        // This assertion is the reverse of the one it replaces. That earlier
        // version had the host share the corner component's aspect constrainer
        // so a host edge drag came back proportional, which is right only for a
        // host that then resizes its window to the answer. REAPER on Linux keeps
        // its window where the user dragged it, so the corrected answer left the
        // editor overhanging the window and the face was clipped: issue #240,
        // reproduced by hand at 1057x474 and again at 625x340. The aspect ratio
        // now lives only on the corner component, where the plugin owns both
        // sides of the negotiation.
        auto* constrainer = editor->getConstrainer();
        check("editor exposes a resize constrainer", constrainer != nullptr);
        if (constrainer != nullptr)
        {
            check("host constrainer does not impose an aspect ratio",
                  std::abs(constrainer->getFixedAspectRatio()) < 0.0001);
            check("host constrainer imposes no minimum a host could violate",
                  constrainer->getMinimumWidth() <= 1 && constrainer->getMinimumHeight() <= 1);
        }

        // The issue's REAPER/Linux gesture: move the top edge down while
        // dragging the right edge out. The bottom edge remains anchored.
        editor->setBounds(0, 0, baseWidth, baseHeight);
        editor->setBoundsConstrained({ 0, 150, 1125, 350 });

        check("host-edge resize gives the editor exactly the requested bounds",
              editor->getWidth() == 1125 && editor->getHeight() == 350);
        check("top-edge resize keeps the bottom edge anchored", editor->getBottom() == baseHeight);

        // Nothing may hang outside those bounds, which is what the user sees as
        // a cut-off face.
        juce::Rectangle<int> face;
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
        {
            auto* child = editor->getChildComponent(i);

            if (child == nullptr || !child->isVisible()
                || dynamic_cast<juce::ResizableCornerComponent*>(child) != nullptr)
                continue;

            face = face.getUnion(child->getBounds());
        }

        check("the face stays inside the editor after a host-edge resize",
              face.getRight() <= editor->getWidth() && face.getBottom() <= editor->getHeight());

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
