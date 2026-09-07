#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Resizable plugin UI helper. Call initialize() before setSize(),
// updateResizer() in resized(). Window size persists across sessions.
class ScalableEditorHelper
{
public:
    ScalableEditorHelper() = default;
    ~ScalableEditorHelper()
    {
        // The editor does not own a custom constrainer. Detach it while both
        // objects are still alive so the editor cannot retain a dangling
        // pointer during base-class teardown.
        resizer.reset();
        if (parentEditor != nullptr && parentEditor->getConstrainer() == &hostConstrainer)
        {
            parentEditor->setResizable(false, false);
            parentEditor->setConstrainer(nullptr);
        }
    }

    // uiVersion (default 0 = no version check, backwards-compatible) bumps
    // the persistence "uiVersion" key. If the stored uiVersion is lower
    // than the value passed here, loadStoredSize() resets the persisted
    // size to the new defaults. Bump this in any caller after a layout
    // change that requires existing sessions to pick up the new defaults.
    void initialize(juce::AudioProcessorEditor* editor,
                    juce::AudioProcessor* processor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight,
                    bool fixedAspectRatio = false,
                    int uiVersion = 0)
    {
        parentEditor = editor;
        audioProcessor = processor;
        persistenceEnabled = true;
        baseWidth = static_cast<float>(defaultWidth);
        baseHeight = static_cast<float>(defaultHeight);
        defaultW = defaultWidth;
        defaultH = defaultHeight;
        minW = minWidth;
        minH = minHeight;
        maxW = maxWidth;
        maxH = maxHeight;
        currentUiVersion = uiVersion;

        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
        constrainer.setFixedAspectRatio(fixedAspectRatio ? baseWidth / baseHeight : 0.0);
        configureHostConstrainer(maxWidth, maxHeight);

        loadStoredSize();

        resizer = std::make_unique<juce::ResizableCornerComponent>(editor, &constrainer);
        editor->addAndMakeVisible(resizer.get());
        resizer->setAlwaysOnTop(true);

        // The corner component keeps `constrainer`, with the comfort floor and
        // the fixed aspect ratio. The host gets `hostConstrainer`, which cannot
        // fight it: see configureHostConstrainer().
        editor->setResizable(true, false);
        editor->setConstrainer(&hostConstrainer);
    }

    // Overload without processor — no size persistence, fixed aspect ratio.
    void initialize(juce::AudioProcessorEditor* editor,
                    int defaultWidth, int defaultHeight,
                    int minWidth, int minHeight,
                    int maxWidth, int maxHeight)
    {
        parentEditor = editor;
        audioProcessor = nullptr;
        persistenceEnabled = false;
        baseWidth = static_cast<float>(defaultWidth);
        baseHeight = static_cast<float>(defaultHeight);
        defaultW = defaultWidth;
        defaultH = defaultHeight;
        minW = minWidth;
        minH = minHeight;
        maxW = maxWidth;
        maxH = maxHeight;
        storedWidth = defaultWidth;
        storedHeight = defaultHeight;

        constrainer.setMinimumSize(minWidth, minHeight);
        constrainer.setMaximumSize(maxWidth, maxHeight);
        constrainer.setFixedAspectRatio(baseWidth / baseHeight);
        configureHostConstrainer(maxWidth, maxHeight);

        resizer = std::make_unique<juce::ResizableCornerComponent>(editor, &constrainer);
        editor->addAndMakeVisible(resizer.get());
        resizer->setAlwaysOnTop(true);

        editor->setResizable(true, false);
        editor->setConstrainer(&hostConstrainer);
    }

    int getStoredWidth() const { return storedWidth; }
    int getStoredHeight() const { return storedHeight; }

    void updateResizer()
    {
        if (parentEditor == nullptr)
            return;

        if (resizer)
        {
            const int handleSize = 16;
            resizer->setBounds(parentEditor->getWidth() - handleSize,
                               parentEditor->getHeight() - handleSize,
                               handleSize, handleSize);
        }

        const float widthScale = static_cast<float>(parentEditor->getWidth()) / baseWidth;

        // An editor whose layout is tied to an aspect ratio has to scale by
        // whichever axis is the tighter fit, or a window shorter than the base
        // aspect lays the rows out at the width's scale and they overlap: knobs
        // over their labels, panel titles over the panel above. A host can hand
        // out any shape it likes (issue #240), so this is reachable in normal
        // use, and it is not the same thing as being clipped, which is why the
        // bounds checks did not see it.
        //
        // Editors initialised without a fixed aspect ratio grow their height
        // independently, by opening collapsible sections, so for those the
        // height ratio is not a scale at all and the width alone is right.
        if (constrainer.getFixedAspectRatio() > 0.0)
        {
            const float heightScale = static_cast<float>(parentEditor->getHeight()) / baseHeight;
            scaleFactor = juce::jmax(kMinimumScale, juce::jmin(widthScale, heightScale));
        }
        else
        {
            scaleFactor = juce::jmax(kMinimumScale, widthScale);
        }
    }

    float getScaleFactor() const { return scaleFactor; }

    int scaled(int value) const
    {
        return static_cast<int>(static_cast<float>(value) * scaleFactor);
    }

    float scaled(float value) const
    {
        return value * scaleFactor;
    }

    // The corner component's constrainer, carrying the comfort floor and the
    // fixed aspect ratio. This is not the constrainer the host sees.
    juce::ComponentBoundsConstrainer& getConstrainer() { return constrainer; }

private:
    // A host must always get back the size it asked for.
    //
    // JUCE answers a VST3 host's IPlugView::checkSizeConstraint from whatever
    // constrainer the editor carries: it clamps the request up to the minimum
    // size, then corrects it to the fixed aspect ratio, and sizes the editor to
    // that answer. A host that honours the answer resizes its window to match
    // and everything stays proportional, which is what the earlier fix for
    // issue #240 assumed when it gave the host the corner component's
    // constrainer. REAPER on Linux does not: it keeps its window at the size the
    // user dragged the frame to, so an answer larger than that window leaves the
    // editor overhanging it and the face is clipped. A comfort floor cannot help
    // either, because the host can always drag below it.
    //
    // So the host gets a constrainer that can never enlarge anything: a minimum
    // it cannot violate and no aspect correction. The maximum is kept, since
    // clamping downwards only ever leaves a smaller editor inside a larger
    // window, never an overhang. Fitting the face into those bounds is then the
    // layout's job, which every editor here already does: it scales its widgets
    // from updateResizer()'s factor and spans the panels across the editor, so
    // the face fills whatever window it is given instead of overhanging it.
    static constexpr int kHostMinimumSize = 1;

    // A host may drive the editor to a degenerate size mid-drag; keep the scale
    // positive so nothing downstream divides by zero.
    static constexpr float kMinimumScale = 0.01f;

    void configureHostConstrainer(int maxWidth, int maxHeight)
    {
        hostConstrainer.setMinimumSize(kHostMinimumSize, kHostMinimumSize);
        hostConstrainer.setMaximumSize(maxWidth, maxHeight);
        hostConstrainer.setFixedAspectRatio(0.0);
    }

    juce::AudioProcessorEditor* parentEditor = nullptr;
    juce::AudioProcessor* audioProcessor = nullptr;
    juce::ComponentBoundsConstrainer constrainer;
    juce::ComponentBoundsConstrainer hostConstrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    float baseWidth = 800.0f;
    float baseHeight = 600.0f;
    float scaleFactor = 1.0f;
    bool persistenceEnabled = true;

    int defaultW = 800;
    int defaultH = 600;
    int minW = 640;
    int minH = 480;
    int maxW = 1920;
    int maxH = 1200;
    int storedWidth = 800;
    int storedHeight = 600;
    int currentUiVersion = 0;   // 0 = no version gate (legacy callers)

    static constexpr const char* kWindowWidth = "windowWidth";
    static constexpr const char* kWindowHeight = "windowHeight";
    static constexpr const char* kUiVersion    = "uiVersion";

    void loadStoredSize()
    {
        storedWidth = defaultW;
        storedHeight = defaultH;

        if (!persistenceEnabled)
            return;

        auto* props = getAppProperties();
        if (props == nullptr)
            return;

        auto* userSettings = props->getUserSettings();
        if (userSettings == nullptr)
            return;

        juce::String prefix = getPluginPrefix();

        // UI version gate: if the caller passed a non-zero uiVersion and the
        // persisted version is older, the stored size is from a prior
        // layout and is no longer valid — fall back to defaults so the
        // user sees the new layout without having to manually resize.
        if (currentUiVersion > 0)
        {
            const int storedUiVersion = userSettings->getIntValue(prefix + kUiVersion, 0);
            if (storedUiVersion < currentUiVersion)
                return;   // storedWidth / Height already = defaults
        }

        storedWidth = userSettings->getIntValue(prefix + kWindowWidth, defaultW);
        storedHeight = userSettings->getIntValue(prefix + kWindowHeight, defaultH);

        storedWidth = juce::jlimit(minW, maxW, storedWidth);
        storedHeight = juce::jlimit(minH, maxH, storedHeight);

        // setSize() intentionally bypasses an editor constrainer. Normalise a
        // previously persisted free-aspect size here so an editor fixed by a
        // newer build cannot reopen in the clipped shape saved by an older one.
        if (constrainer.getFixedAspectRatio() > 0.0)
        {
            juce::Rectangle<int> storedBounds(0, 0, storedWidth, storedHeight);
            const juce::Rectangle<int> defaultBounds(0, 0, defaultW, defaultH);
            const juce::Rectangle<int> limits(0, 0, maxW, maxH);
            constrainer.checkBounds(storedBounds, defaultBounds, limits,
                                    false, false, true, true);
            storedWidth = storedBounds.getWidth();
            storedHeight = storedBounds.getHeight();
        }
    }

    void saveCurrentSize()
    {
        if (parentEditor == nullptr)
            return;

        if (!persistenceEnabled)
            return;

        auto* props = getAppProperties();
        if (props == nullptr)
            return;

        auto* userSettings = props->getUserSettings();
        if (userSettings == nullptr)
            return;

        juce::String prefix = getPluginPrefix();
        userSettings->setValue(prefix + kWindowWidth, parentEditor->getWidth());
        userSettings->setValue(prefix + kWindowHeight, parentEditor->getHeight());
        if (currentUiVersion > 0)
            userSettings->setValue(prefix + kUiVersion, currentUiVersion);
        props->saveIfNeeded();
    }

    juce::ApplicationProperties* getAppProperties()
    {
        static juce::ApplicationProperties appProps;
        static bool initialized = false;

        if (!initialized)
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "DuskAudio";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX
            options.folderName = ".config/DuskAudio";

            // Older builds stored settings in ~/DuskAudio; move them so
            // existing users keep their saved window sizes.
            auto oldFile = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                               .getChildFile("DuskAudio")
                               .getChildFile("DuskAudio.settings");
            auto newFile = options.getDefaultFile();
            if (oldFile.existsAsFile() && !newFile.exists())
            {
                newFile.getParentDirectory().createDirectory();
                if (oldFile.moveFileTo(newFile))
                    oldFile.getParentDirectory().deleteFile(); // rmdir: only removes ~/DuskAudio if now empty
            }
           #else
            options.folderName = "DuskAudio";
           #endif
            appProps.setStorageParameters(options);
            initialized = true;
        }

        return &appProps;
    }

    juce::String getPluginPrefix() const
    {
        if (audioProcessor != nullptr)
            return audioProcessor->getName() + "_";
        return "Plugin_";
    }

public:
    void saveSize()
    {
        saveCurrentSize();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalableEditorHelper)
};
