#include "FileBrowserPanel.h"
#include "DuskAmpLookAndFeel.h"

FileBrowserPanel::FileBrowserPanel(const juce::String& title, const juce::String& fileFilter,
                                   const juce::String& dialogTitle)
    : fileFilter_(fileFilter), dialogTitle_(dialogTitle)
{
    headerLabel_.setText(title, juce::dontSendNotification);
    headerLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    headerLabel_.setColour(juce::Label::textColourId, juce::Colour(DuskAmpLookAndFeel::kLabelText));
    addAndMakeVisible(headerLabel_);

    browseButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(DuskAmpLookAndFeel::kLabelText));
    browseButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(DuskAmpLookAndFeel::kGroupText));
    browseButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(DuskAmpLookAndFeel::kPanel));
    addAndMakeVisible(browseButton_);

    browseButton_.onClick = [this]
    {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            dialogTitle_,
            rootDir_.exists() ? rootDir_ : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            fileFilter_);

        auto safeThis = juce::Component::SafePointer<FileBrowserPanel>(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectDirectories,
            [safeThis](const juce::FileChooser& fc)
            {
                if (safeThis == nullptr) return;
                auto result = fc.getResult();
                if (result.existsAsFile())
                {
                    safeThis->setRootDirectory(result.getParentDirectory());
                    if (safeThis->onFileSelected)
                        safeThis->onFileSelected(result);
                }
                else if (result.isDirectory())
                {
                    safeThis->setRootDirectory(result);
                }
            });
    };

    fileList_.setRowHeight(20);
    fileList_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    fileList_.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    fileList_.setOutlineThickness(0);
    addAndMakeVisible(fileList_);
}

void FileBrowserPanel::setRootDirectory(const juce::File& dir)
{
    rootDir_ = dir;
    scanDirectory();
}

juce::File FileBrowserPanel::getRootDirectory() const
{
    return rootDir_;
}

void FileBrowserPanel::setLoadedFile(const juce::File& file)
{
    loadedFile_ = file;
    fileList_.repaint();
}

void FileBrowserPanel::resized()
{
    auto area = getLocalBounds();

    auto topBar = area.removeFromTop(24);
    browseButton_.setBounds(topBar.removeFromRight(60).reduced(1, 2));
    headerLabel_.setBounds(topBar);

    area.removeFromTop(2);
    fileList_.setBounds(area.reduced(1));
}

void FileBrowserPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Recessed slot background
    auto slotBounds = bounds.withTrimmedTop(26.0f);
    float cr = 4.0f;

    g.setColour(juce::Colour(0xff0e0e0e));
    g.fillRoundedRectangle(slotBounds, cr);

    // Inner shadow (top edge darker)
    g.setColour(juce::Colour(0x25000000));
    g.fillRoundedRectangle(slotBounds.withHeight(6.0f), cr);

    // Subtle border
    g.setColour(juce::Colour(DuskAmpLookAndFeel::kBorder).withAlpha(0.5f));
    g.drawRoundedRectangle(slotBounds.reduced(0.5f), cr, 0.5f);

    // Placeholder text when empty — larger and brighter for readability
    if (files_.isEmpty() && !loadedFile_.existsAsFile())
    {
        g.setColour(juce::Colour(DuskAmpLookAndFeel::kGroupText));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(dialogTitle_ + "...", slotBounds, juce::Justification::centred);
    }
}

int FileBrowserPanel::getNumRows()
{
    return files_.size();
}

void FileBrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= files_.size())
        return;

    const auto& file = files_[row];
    const bool isLoaded = (file == loadedFile_);

    if (selected)
    {
        // Subtle lighter background + crisp amber accent bar on left edge
        g.fillAll(juce::Colour(DuskAmpLookAndFeel::kPanelHi));
        g.setColour(juce::Colour(DuskAmpLookAndFeel::kAccent));
        g.fillRect(0, 0, 3, h);
    }
    else if (isLoaded)
    {
        // Loaded file: subtle amber tint + accent bar
        g.fillAll(juce::Colour(DuskAmpLookAndFeel::kAccent).withAlpha(0.08f));
        g.setColour(juce::Colour(DuskAmpLookAndFeel::kAccent).withAlpha(0.5f));
        g.fillRect(0, 0, 3, h);
    }

    // Text: amber for loaded/selected, light grey otherwise
    g.setFont(juce::FontOptions(11.0f));
    g.setColour((isLoaded || selected) ? juce::Colour(DuskAmpLookAndFeel::kAccent)
                                       : juce::Colour(DuskAmpLookAndFeel::kLabelText));
    g.drawText(file.getFileNameWithoutExtension(),
               8, 0, w - 14, h,
               juce::Justification::centredLeft, true);
}

void FileBrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < files_.size() && onFileSelected)
        onFileSelected(files_[row]);
}

void FileBrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < files_.size() && onFileSelected)
        onFileSelected(files_[row]);
}

void FileBrowserPanel::scanDirectory()
{
    files_.clear();

    if (rootDir_.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator(rootDir_, false, fileFilter_,
                                                                juce::File::findFiles))
        {
            files_.add(entry.getFile());
        }

        files_.sort();
    }

    fileList_.updateContent();
    fileList_.repaint();
}
