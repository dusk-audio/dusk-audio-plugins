/*
  ==============================================================================

    Convolution Reverb - IR Browser
    Category-based file browser for impulse responses
    Copyright (c) 2025 Dusk Audio

  ==============================================================================
*/

#include "IRBrowser.h"

//==============================================================================
// CategoryTreeItem Implementation
//==============================================================================
IRBrowser::CategoryTreeItem::CategoryTreeItem(const juce::String& name, const juce::File& dir, IRBrowser& browser)
    : categoryName(name), directory(dir), ownerBrowser(browser)
{
}

bool IRBrowser::CategoryTreeItem::mightContainSubItems()
{
    return directory.isDirectory();
}

void IRBrowser::CategoryTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height);

    // Clip to bounds to prevent any artifacts extending beyond item width
    g.reduceClipRegion(bounds);

    if (isSelected())
    {
        // Use rounded rectangle for selection to look cleaner
        g.setColour(ownerBrowser.highlightColour.withAlpha(0.35f));
        g.fillRoundedRectangle(bounds.reduced(2, 1).toFloat(), 3.0f);
    }

    // Folder icon
    auto iconBounds = bounds.removeFromLeft(height).reduced(4).toFloat();
    g.setColour(isSelected() ? ownerBrowser.highlightColour : ownerBrowser.dimTextColour);

    juce::Path folderPath;
    folderPath.addRoundedRectangle(iconBounds.getX(), iconBounds.getY() + iconBounds.getHeight() * 0.2f,
                                    iconBounds.getWidth(), iconBounds.getHeight() * 0.7f, 2.0f);
    folderPath.addRoundedRectangle(iconBounds.getX(), iconBounds.getY(),
                                    iconBounds.getWidth() * 0.4f, iconBounds.getHeight() * 0.25f, 1.0f);
    g.fillPath(folderPath);

    // Category name
    g.setColour(isSelected() ? ownerBrowser.textColour : ownerBrowser.dimTextColour);
    g.setFont(juce::Font(12.0f, isSelected() ? juce::Font::bold : juce::Font::plain));
    g.drawText(categoryName, bounds.reduced(4, 0), juce::Justification::centredLeft);
}

void IRBrowser::CategoryTreeItem::itemOpennessChanged(bool isNowOpen)
{
    if (isNowOpen && !hasScanned)
    {
        scanSubdirectories();
        hasScanned = true;
    }
}

void IRBrowser::CategoryTreeItem::itemSelectionChanged(bool isNowSelected)
{
    if (isNowSelected)
    {
        ownerBrowser.selectDirectory(directory);
    }
}

void IRBrowser::CategoryTreeItem::scanSubdirectories()
{
    // Find subdirectories
    juce::Array<juce::File> subdirs;

    for (const auto& entry : juce::RangedDirectoryIterator(directory, false, "*", juce::File::findDirectories))
    {
        subdirs.add(entry.getFile());
    }

    // Sort alphabetically
    subdirs.sort();

    // Add as child items
    for (const auto& subdir : subdirs)
    {
        addSubItem(new CategoryTreeItem(subdir.getFileName(), subdir, ownerBrowser));
    }
}

//==============================================================================
// IRBrowser Implementation
//==============================================================================
IRBrowser::IRBrowser()
{
    setupComponents();
    // Timer started in setRootDirectory after components are fully ready
}

IRBrowser::~IRBrowser()
{
    stopTimer();

    // Remove listener before destroying components
    if (fileList != nullptr)
        fileList->removeListener(this);

    // Clear TreeView root item before destroying tree
    if (categoryTree != nullptr)
        categoryTree->setRootItem(nullptr);
    rootItem = nullptr;

    // Clear in proper order to avoid dangling references
    fileList = nullptr;
    categoryTree = nullptr;

    // DirectoryContentsList must be destroyed before TimeSliceThread
    directoryContents = nullptr;
    fileFilter = nullptr;

    // Stop and destroy thread last
    if (directoryThread != nullptr)
    {
        directoryThread->stopThread(1000);
    }
    directoryThread = nullptr;
}

void IRBrowser::setupComponents()
{
    // Header label
    headerLabel = std::make_unique<juce::Label>("header", "IR BROWSER");
    headerLabel->setFont(juce::Font(11.0f, juce::Font::bold));
    headerLabel->setColour(juce::Label::textColourId, dimTextColour);
    headerLabel->setJustificationType(juce::Justification::centredLeft);  // Left-align since it has flexible width
    addAndMakeVisible(headerLabel.get());

    // Search box for filtering IRs with search icon in placeholder
    searchBox = std::make_unique<juce::TextEditor>("search");
    searchBox->setMultiLine(false);
    searchBox->setReturnKeyStartsNewLine(false);
    searchBox->setPopupMenuEnabled(false);
    // Unicode magnifying glass icon before "Search..."
    searchBox->setTextToShowWhenEmpty(juce::CharPointer_UTF8("\xf0\x9f\x94\x8d Search..."), dimTextColour);
    searchBox->setColour(juce::TextEditor::backgroundColourId, panelColour.darker(0.1f));
    searchBox->setColour(juce::TextEditor::textColourId, textColour);
    searchBox->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff353535));
    searchBox->setColour(juce::TextEditor::focusedOutlineColourId, highlightColour);
    searchBox->setFont(juce::Font(11.0f));
    searchBox->onTextChange = [this]()
    {
        applySearchFilter(searchBox->getText());
    };
    addAndMakeVisible(searchBox.get());

    // Browse button
    browseButton = std::make_unique<juce::TextButton>("...");
    browseButton->setTooltip("Browse for IR folder");
    browseButton->onClick = [this]()
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select IR Folder",
            rootDirectory.exists() ? rootDirectory : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*");

        auto safeThis = juce::Component::SafePointer<IRBrowser>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safeThis, chooser](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr)
                    return;

                auto result = fc.getResult();
                if (result.exists() && result.isDirectory())
                {
                    safeThis->setRootDirectory(result);
                }
            });
    };
    addAndMakeVisible(browseButton.get());

    // Refresh button
    refreshButton = std::make_unique<juce::TextButton>("Refresh");
    refreshButton->setTooltip("Refresh file list");
    refreshButton->onClick = [this]() { refreshFileList(); };
    addAndMakeVisible(refreshButton.get());

    // Directory thread for background file scanning
    directoryThread = std::make_unique<juce::TimeSliceThread>("IR Directory Scanner");
    directoryThread->startThread(juce::Thread::Priority::low);

    // File filter (common audio formats including Space Designer .SDIR)
    fileFilter = std::make_unique<juce::WildcardFileFilter>(
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3;*.sdir;*.WAV;*.AIFF;*.AIF;*.FLAC;*.OGG;*.MP3;*.SDIR",
        "*",
        "Audio Files");

    // Directory contents (initially empty)
    directoryContents = std::make_unique<juce::DirectoryContentsList>(fileFilter.get(), *directoryThread);

    // Category tree view
    categoryTree = std::make_unique<juce::TreeView>("Categories");
    categoryTree->setColour(juce::TreeView::backgroundColourId, backgroundColour);
    categoryTree->setColour(juce::TreeView::linesColourId, panelColour);
    categoryTree->setColour(juce::TreeView::selectedItemBackgroundColourId, juce::Colours::transparentBlack);  // We draw our own selection
    categoryTree->setColour(juce::TreeView::dragAndDropIndicatorColourId, highlightColour);
    categoryTree->setIndentSize(16);
    categoryTree->setDefaultOpenness(false);
    addAndMakeVisible(categoryTree.get());

    // File list
    fileList = std::make_unique<juce::FileListComponent>(*directoryContents);
    fileList->setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, highlightColour.withAlpha(0.45f));
    fileList->setColour(juce::DirectoryContentsDisplayComponent::textColourId, textColour);
    fileList->addListener(this);
    addAndMakeVisible(fileList.get());
}

void IRBrowser::setRootDirectory(const juce::File& directory)
{
    if (!directory.exists() || !directory.isDirectory())
        return;

    rootDirectory = directory;
    currentDirectory = directory;

    buildCategoryTree();

    if (directoryContents != nullptr)
        directoryContents->setDirectory(directory, true, true);

    // Start timer now that we have valid content
    if (!isTimerRunning())
        startTimerHz(2);
}

void IRBrowser::addListener(Listener* listener)
{
    listeners.add(listener);
}

void IRBrowser::removeListener(Listener* listener)
{
    listeners.remove(listener);
}

void IRBrowser::refreshFileList()
{
    if (directoryContents != nullptr && rootDirectory.exists())
        directoryContents->refresh();

    if (rootDirectory.exists())
        buildCategoryTree();
}

void IRBrowser::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    // Draw separator between tree and file list
    auto bounds = getLocalBounds();
    int treeHeight = bounds.getHeight() / 2;

    g.setColour(panelColour);
    g.drawHorizontalLine(30 + treeHeight, 0, static_cast<float>(getWidth()));
}

void IRBrowser::resized()
{
    auto bounds = getLocalBounds();

    // Header area with title and buttons
    auto headerArea = bounds.removeFromTop(22);
    // Buttons on right first, then label gets remaining space
    refreshButton->setBounds(headerArea.removeFromRight(50).reduced(2));
    browseButton->setBounds(headerArea.removeFromRight(25).reduced(2));
    headerLabel->setBounds(headerArea.reduced(2, 0));  // Label gets remaining width

    // Search box below header
    auto searchArea = bounds.removeFromTop(24);
    searchBox->setBounds(searchArea.reduced(2));

    bounds.removeFromTop(3); // Spacing

    // Split remaining space between tree and file list
    int treeHeight = bounds.getHeight() / 2;

    categoryTree->setBounds(bounds.removeFromTop(treeHeight));

    bounds.removeFromTop(2); // Separator spacing

    fileList->setBounds(bounds);
}

void IRBrowser::selectionChanged()
{
    // Called when file selection changes in the list
}

void IRBrowser::fileClicked(const juce::File& file, const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);

    if (file.existsAsFile())
    {
        listeners.call(&Listener::irFileSelected, file);
    }
}

void IRBrowser::fileDoubleClicked(const juce::File& file)
{
    if (file.isDirectory())
    {
        selectDirectory(file);
    }
    else if (file.existsAsFile())
    {
        listeners.call(&Listener::irFileSelected, file);
    }
}

void IRBrowser::browserRootChanged(const juce::File& newRoot)
{
    juce::ignoreUnused(newRoot);
}

void IRBrowser::timerCallback()
{
    // Periodic check - could be used to refresh if files change
}

void IRBrowser::buildCategoryTree()
{
    if (categoryTree == nullptr)
        return;

    categoryTree->setRootItem(nullptr);
    rootItem = nullptr;

    if (!rootDirectory.exists() || !rootDirectory.isDirectory())
        return;

    rootItem = std::make_unique<CategoryTreeItem>(rootDirectory.getFileName(), rootDirectory, *this);
    categoryTree->setRootItem(rootItem.get());
    rootItem->setOpen(true);
}

void IRBrowser::selectDirectory(const juce::File& dir)
{
    if (!dir.exists() || !dir.isDirectory())
        return;

    currentDirectory = dir;

    if (directoryContents != nullptr)
        directoryContents->setDirectory(dir, true, true);

    // Reapply search filter after directory change
    if (currentSearchFilter.isNotEmpty())
        applySearchFilter(currentSearchFilter);
}

void IRBrowser::applySearchFilter(const juce::String& filter)
{
    currentSearchFilter = filter.toLowerCase().trim();

    if (fileFilter == nullptr || directoryThread == nullptr)
        return;

    // Update the wildcard filter to include the search term
    // The filter will match files whose names contain the search string
    juce::String basePattern = "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3;*.sdir;*.WAV;*.AIFF;*.AIF;*.FLAC;*.OGG;*.MP3;*.SDIR";

    if (currentSearchFilter.isEmpty())
    {
        // Reset to default filter
        fileFilter = std::make_unique<juce::WildcardFileFilter>(
            basePattern,
            "*",
            "Audio Files");
    }
    else
    {
        // Create a filter that matches the search term in the filename
        // WildcardFileFilter doesn't support substring matching directly,
        // so we use a custom approach - add wildcards around the search term
        juce::String searchPattern = "*" + currentSearchFilter + "*";

        // Build pattern with search term for each extension
        juce::StringArray extensions;
        extensions.addTokens(basePattern, ";", "");

        juce::String filteredPattern;
        for (auto& ext : extensions)
        {
            // e.g., "*.wav" becomes "*search*.wav"
            juce::String extOnly = ext.fromFirstOccurrenceOf(".", false, false);
            if (filteredPattern.isNotEmpty())
                filteredPattern += ";";
            filteredPattern += searchPattern + "." + extOnly;
        }

        fileFilter = std::make_unique<juce::WildcardFileFilter>(
            filteredPattern,
            "*",
            "Audio Files matching: " + currentSearchFilter);
    }

    // Recreate directory contents with new filter
    directoryContents = std::make_unique<juce::DirectoryContentsList>(fileFilter.get(), *directoryThread);

    if (currentDirectory.exists())
        directoryContents->setDirectory(currentDirectory, true, true);

    // Update file list with new contents
    if (fileList != nullptr)
    {
        fileList->removeListener(this);
    }

    fileList = std::make_unique<juce::FileListComponent>(*directoryContents);
    fileList->setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, highlightColour.withAlpha(0.45f));
    fileList->setColour(juce::DirectoryContentsDisplayComponent::textColourId, textColour);
    fileList->addListener(this);
    addAndMakeVisible(fileList.get());

    resized();
}
