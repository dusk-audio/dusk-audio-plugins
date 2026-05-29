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

    // Prominent "currently loaded" indicator — bold accent text sitting on the
    // header bar between the title and the Browse button. Visible at a glance
    // without having to scan the file list. Empty until setLoadedFile() runs.
    loadedNameLabel_.setText("(none loaded)", juce::dontSendNotification);
    loadedNameLabel_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    loadedNameLabel_.setColour(juce::Label::textColourId,
                                juce::Colour(DuskAmpLookAndFeel::kAccent).withAlpha(0.55f));
    loadedNameLabel_.setJustificationType(juce::Justification::centredRight);
    loadedNameLabel_.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(loadedNameLabel_);

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
    if (file.existsAsFile())
    {
        loadedNameLabel_.setText(file.getFileNameWithoutExtension(),
                                  juce::dontSendNotification);
        loadedNameLabel_.setColour(juce::Label::textColourId,
                                    juce::Colour(DuskAmpLookAndFeel::kAccent));
        // Auto-scroll the list so the loaded row is visible (when present)
        for (int i = 0; i < files_.size(); ++i)
        {
            if (files_[i] == file)
            {
                fileList_.scrollToEnsureRowIsOnscreen(i);
                break;
            }
        }
    }
    else
    {
        loadedNameLabel_.setText("(none loaded)", juce::dontSendNotification);
        loadedNameLabel_.setColour(juce::Label::textColourId,
                                    juce::Colour(DuskAmpLookAndFeel::kAccent).withAlpha(0.55f));
    }
    fileList_.repaint();
}

void FileBrowserPanel::resized()
{
    auto area = getLocalBounds();

    // Header bar: [title fixed-width] [loaded name, flex] [Browse button]
    auto topBar = area.removeFromTop(24);
    browseButton_.setBounds(topBar.removeFromRight(60).reduced(0, 1));
    topBar.removeFromRight(4); // gap before browse
    auto titleArea = topBar.removeFromLeft(juce::jmin(110, topBar.getWidth() / 3));
    headerLabel_.setBounds(titleArea);
    loadedNameLabel_.setBounds(topBar);

    area.removeFromTop(2);
    fileList_.setBounds(area);
}

void FileBrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(DuskAmpLookAndFeel::kPanel));
}

void FileBrowserPanel::paintOverChildren(juce::Graphics& g)
{
    if (! files_.isEmpty())
        return;

    g.setColour(juce::Colour(DuskAmpLookAndFeel::kText).withAlpha(0.5f));
    g.setFont(juce::FontOptions(11.0f, juce::Font::italic));
    g.drawFittedText(emptyStateText_, fileList_.getBounds().reduced(8, 0),
                     juce::Justification::centred, 3);
}

int FileBrowserPanel::getNumRows()
{
    return files_.size();
}

static juce::String middleEllipsize (const juce::String& text, const juce::Font& font, int maxWidth)
{
    if (font.getStringWidth (text) <= maxWidth)
        return text;
    const juce::String ell = "...";
    const int ellW = font.getStringWidth (ell);
    if (maxWidth <= ellW)
        return ell;
    // Binary trim from the middle, keeping outer chars on each side.
    int leftLen = text.length() / 2;
    int rightLen = text.length() - leftLen;
    while (leftLen > 0 && rightLen > 0)
    {
        juce::String trial = text.substring (0, leftLen) + ell + text.substring (text.length() - rightLen);
        if (font.getStringWidth (trial) <= maxWidth)
            return trial;
        if (leftLen >= rightLen) --leftLen;
        else                     --rightLen;
    }
    return ell;
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

    const juce::Font font (juce::FontOptions (11.0f));
    g.setFont (font);
    g.setColour (isLoaded ? juce::Colour(DuskAmpLookAndFeel::kAccent) : juce::Colour(DuskAmpLookAndFeel::kText));
    const juce::String displayed = middleEllipsize (file.getFileNameWithoutExtension(), font, w - 12);
    g.drawText (displayed, 6, 0, w - 12, h, juce::Justification::centredLeft, false);
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
