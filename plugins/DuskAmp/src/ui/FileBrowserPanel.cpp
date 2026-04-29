// SPDX-License-Identifier: GPL-3.0-or-later

#include "FileBrowserPanel.h"
#include "DuskAmpLookAndFeel.h"

FileBrowserPanel::FileBrowserPanel(const juce::String& title, const juce::String& fileFilter,
                                   const juce::String& dialogTitle)
    : fileFilter_(fileFilter), dialogTitle_(dialogTitle)
{
    headerLabel_.setText(title, juce::dontSendNotification);
    headerLabel_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    headerLabel_.setColour(juce::Label::textColourId, juce::Colour(DuskAmpLookAndFeel::kText));
    addAndMakeVisible(headerLabel_);

    browseButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(DuskAmpLookAndFeel::kText));
    browseButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(DuskAmpLookAndFeel::kText));
    browseButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(DuskAmpLookAndFeel::kBorder));
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
    fileList_.setColour(juce::ListBox::backgroundColourId, juce::Colour(DuskAmpLookAndFeel::kPanel));
    fileList_.setColour(juce::ListBox::outlineColourId, juce::Colour(DuskAmpLookAndFeel::kBorder));
    fileList_.setOutlineThickness(1);
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
    browseButton_.setBounds(topBar.removeFromRight(60).reduced(0, 1));
    headerLabel_.setBounds(topBar);

    area.removeFromTop(2);
    fileList_.setBounds(area);
}

void FileBrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(DuskAmpLookAndFeel::kPanel));
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
        g.fillAll(juce::Colour(DuskAmpLookAndFeel::kBorder));
    else if (isLoaded)
        g.fillAll(juce::Colour(DuskAmpLookAndFeel::kAccent).withAlpha(0.2f));

    g.setFont(juce::FontOptions(11.0f));
    g.setColour(isLoaded ? juce::Colour(DuskAmpLookAndFeel::kAccent) : juce::Colour(DuskAmpLookAndFeel::kText));
    g.drawText(file.getFileNameWithoutExtension(),
               6, 0, w - 12, h,
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
