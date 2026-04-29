// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "FileBrowserPanel.h"

class CabBrowser : public FileBrowserPanel
{
public:
    CabBrowser() : FileBrowserPanel("Cabinet IRs", "*.wav;*.aiff;*.aif", "Select Cabinet IR or Folder") {}
};
