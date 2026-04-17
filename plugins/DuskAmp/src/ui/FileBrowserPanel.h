// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class FileBrowserPanel : public juce::Component,
                         private juce::ListBoxModel
{
public:
    FileBrowserPanel(const juce::String& title, const juce::String& fileFilter,
                     const juce::String& dialogTitle);

    void setRootDirectory(const juce::File& dir);
    juce::File getRootDirectory() const;

    std::function<void(const juce::File&)> onFileSelected;

    void setLoadedFile(const juce::File& file);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void scanDirectory();

    juce::String fileFilter_;
    juce::String dialogTitle_;

    juce::ListBox fileList_{"", this};
    juce::TextButton browseButton_{"Browse"};
    juce::Label headerLabel_;

    juce::File rootDir_;
    juce::File loadedFile_;
    juce::Array<juce::File> files_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserPanel)
};
