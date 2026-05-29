#pragma once
#include "FileBrowserPanel.h"

class NAMBrowser : public FileBrowserPanel
{
public:
    NAMBrowser() : FileBrowserPanel("NAM Models", "*.nam", "Select NAM Model or Folder")
    {
        setEmptyStateText("No NAM profile loaded - click Browse to load a .nam file.");
    }
};
