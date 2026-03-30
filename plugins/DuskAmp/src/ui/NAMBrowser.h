#pragma once
#include "FileBrowserPanel.h"

class NAMBrowser : public FileBrowserPanel
{
public:
    NAMBrowser() : FileBrowserPanel("NAM Models", "*.nam", "Select NAM Model or Folder") {}
};
