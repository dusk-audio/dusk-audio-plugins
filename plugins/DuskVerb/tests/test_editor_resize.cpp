#include <JuceHeader.h>

#include "../src/PluginProcessor.h"
#include "SupportersOverlay.h"

#include <cmath>
#include <memory>

namespace
{
juce::Component* findNamedComponent (juce::Component& parent, const juce::String& name)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent (i);
        if (child->getName() == name)
            return child;

        if (auto* match = findNamedComponent (*child, name))
            return match;
    }

    return nullptr;
}

juce::ResizableCornerComponent* findResizeHandle (juce::Component& parent)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* handle = dynamic_cast<juce::ResizableCornerComponent*> (parent.getChildComponent (i)))
            return handle;

    return nullptr;
}

bool supporterTextEntersFooter()
{
    constexpr int width = 600;
    constexpr int height = 300;
    constexpr int panelBottom = 270;
    constexpr int footerHeight = 75;

    SupportersOverlay overlay ("DuskVerb", "0.7.1");
    overlay.setActionLink ("Open crash log folder", [] {});
    overlay.setSize (width, height);

    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    overlay.paint (graphics);

    // At this height the last patron row crosses the content/footer boundary
    // in the broken implementation. Supporter names are bright neutral text;
    // the divider/background in this narrow strip stay below this threshold.
    for (int y = panelBottom - footerHeight; y < panelBottom - footerHeight + 11; ++y)
    {
        for (int x = 180; x < width - 180; ++x)
        {
            const auto colour = image.getPixelAt (x, y);
            const int red = colour.getRed();
            const int green = colour.getGreen();
            const int blue = colour.getBlue();
            if (red >= 100 && std::abs (red - green) <= 2 && std::abs (green - blue) <= 2)
                return true;
        }
    }

    return false;
}
}

class DuskVerbEditorResizeTest final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "DuskVerbEditorResizeTest"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void shutdown() override {}
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String&) override
    {
        DuskVerbProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

        if (editor == nullptr)
        {
            ++failures;
            finish();
            return;
        }

        constexpr int baseWidth = 1400;
        constexpr int baseHeight = 760;
        constexpr int minWidth = 980;
        constexpr int minHeight = 532;

        auto* freezeButton = findNamedComponent (*editor, "freeze");
        auto* resizeHandle = findResizeHandle (*editor);
        const auto initialBounds = editor->getBounds();
        check (freezeButton != nullptr);
        check (resizeHandle != nullptr);

        editor->setBounds (0, 0, baseWidth, baseHeight);
        const int baseFreezeHeight = freezeButton != nullptr ? freezeButton->getHeight() : 0;

        editor->setBoundsConstrained ({ 0, 0, minWidth, minHeight });
        check (editor->getWidth() == minWidth && editor->getHeight() == minHeight);
        check (freezeButton != nullptr && freezeButton->getHeight() < baseFreezeHeight);
        check (resizeHandle != nullptr
               && resizeHandle->getBounds() == juce::Rectangle<int> (minWidth - 16, minHeight - 16, 16, 16));

        check (! supporterTextEntersFooter());

        // DuskVerb persists its editor size on destruction. Restore the value
        // loaded at startup so this test never changes the user's preference.
        editor->setBounds (initialBounds);
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        finish();
    }

private:
    int failures = 0;

    void check (bool condition)
    {
        if (! condition)
            ++failures;
    }

    void finish()
    {
        setApplicationReturnValue (failures == 0 ? 0 : 1);
        quit();
    }
};

START_JUCE_APPLICATION (DuskVerbEditorResizeTest)
