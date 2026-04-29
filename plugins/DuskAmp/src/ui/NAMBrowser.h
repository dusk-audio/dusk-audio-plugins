// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "FileBrowserPanel.h"

class NAMBrowser : public FileBrowserPanel
{
public:
    NAMBrowser() : FileBrowserPanel("NAM Models", "*.nam", "Select NAM Model or Folder") {}
};
