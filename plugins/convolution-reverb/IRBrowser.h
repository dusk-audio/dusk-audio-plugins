/*
  ==============================================================================

    Convolution Reverb - IR Browser
    Category-based file browser for impulse responses
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class IRBrowser : public juce::Component,
                  private juce::FileBrowserListener,
                  private juce::Timer
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void irFileSelected(const juce::File& file) = 0;
    };

    IRBrowser();
    ~IRBrowser() override;

    void setRootDirectory(const juce::File& directory);
    juce::File getRootDirectory() const { return rootDirectory; }

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    void refreshFileList();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Set custom colors
    void setBackgroundColour(juce::Colour colour) { backgroundColour = colour; }
    void setTextColour(juce::Colour colour) { textColour = colour; }
    void setHighlightColour(juce::Colour colour) { highlightColour = colour; }

private:
    // FileBrowserListener
    void selectionChanged() override;
    void fileClicked(const juce::File& file, const juce::MouseEvent& e) override;
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File& newRoot) override;

    // Timer
    void timerCallback() override;

    // Category item for tree view
    class CategoryTreeItem : public juce::TreeViewItem
    {
    public:
        CategoryTreeItem(const juce::String& name, const juce::File& dir, IRBrowser& browser);

        bool mightContainSubItems() override;
        void paintItem(juce::Graphics& g, int width, int height) override;
        void itemOpennessChanged(bool isNowOpen) override;
        void itemSelectionChanged(bool isNowSelected) override;
        juce::String getUniqueName() const override { return categoryName; }

    private:
        juce::String categoryName;
        juce::File directory;
        IRBrowser& ownerBrowser;
        bool hasScanned = false;

        void scanSubdirectories();
    };

    // Components
    std::unique_ptr<juce::TreeView> categoryTree;
    std::unique_ptr<juce::FileListComponent> fileList;
    std::unique_ptr<juce::TimeSliceThread> directoryThread;
    std::unique_ptr<juce::DirectoryContentsList> directoryContents;
    std::unique_ptr<juce::WildcardFileFilter> fileFilter;

    // Root item for tree
    std::unique_ptr<CategoryTreeItem> rootItem;

    // State
    juce::File rootDirectory;
    juce::File currentDirectory;

    // Listeners
    juce::ListenerList<Listener> listeners;

    // Colors
    juce::Colour backgroundColour{0xff1a1a1a};
    juce::Colour textColour{0xffe0e0e0};
    juce::Colour highlightColour{0xff4a9eff};
    juce::Colour dimTextColour{0xff909090};
    juce::Colour panelColour{0xff2a2a2a};

    // Header label
    std::unique_ptr<juce::Label> headerLabel;

    // Search filter
    std::unique_ptr<juce::TextEditor> searchBox;
    juce::String currentSearchFilter;

    // Buttons
    std::unique_ptr<juce::TextButton> browseButton;
    std::unique_ptr<juce::TextButton> refreshButton;

    void setupComponents();
    void buildCategoryTree();
    void selectDirectory(const juce::File& dir);
    void applySearchFilter(const juce::String& filter);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRBrowser)
};
