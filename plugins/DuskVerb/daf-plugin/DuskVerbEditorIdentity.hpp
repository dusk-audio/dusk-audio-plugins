// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbEditorIdentity.hpp — which preset DuskVerb 2's editor says is loaded,
// and which user preset file its preset list highlights. Kept out of the ImGui
// file so DuskVerbEditorIdentityTests can hold the editor to these rules
// without a window.

#pragma once

#include "DuskVerbParams.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace duskverb::ui
{

struct EditorIdentity
{
    int         factory = -1;    // factory preset index, -1 for none
    std::string userName;        // user preset name, empty for none
    std::string userPath;        // the library file the preset list highlights
    std::string userPathName;    // the name userPath was loaded or saved under
    bool        modified = false;

    // Adopt the identity a state snapshot carries: its user preset name, else
    // the factory preset its name matches, else none.
    void adopt(const StateValues& state)
    {
        userName = state.userName;
        // The highlighted library file belongs to the name it was loaded or
        // saved under. Any other identity arriving here (a host restore, an A/B
        // swap, a factory program) is not that file, so stop highlighting it.
        if (userName != userPathName) userPath.clear();
        factory = -1;
        if (userName.empty())
        {
            const auto& presets = getFactoryPresets();
            for (size_t i = 0; i < presets.size(); ++i)
                if (state.presetName == presets[i].name) factory = static_cast<int>(i);
        }
        modified = state.edited;
    }

    // The format wrapper's programLoaded(index). The CLAP and VST3 wrappers call
    // it every time the editor opens, with their own cached program, which is 0
    // until the host picks one and is left alone by a user-preset restore or a
    // preset chosen in the editor. Trusting it drew the first factory preset's
    // name over every other identity on reopen. The plugin owns the identity
    // (loadProgram and setState both set it), so its snapshot, not the index, is
    // adopted. The editor passes no snapshot only when it has no bridge to the
    // plugin at all; then the index is all there is.
    void programLoaded(int index, const StateValues* snapshot)
    {
        if (snapshot != nullptr)
        {
            adopt(*snapshot);
            return;
        }
        if (index < 0 || index >= static_cast<int>(getFactoryPresets().size())) return;
        factory = index;
        userName.clear();
        userPath.clear();
        modified = false;
    }

    // A library file was loaded or saved. The identity it carries has just been
    // adopted, and the file becomes the highlighted one. The highlight follows
    // the name, so a file that loaded without one is not highlighted.
    void userFileOpened(std::string path)
    {
        if (userName.empty())
        {
            userPath.clear();
            return;
        }
        userPath = std::move(path);
        userPathName = userName;
    }

    // INIT, a factory recall, an import (a copy, not a library file) or the
    // highlighted file being deleted: nothing in the library is highlighted.
    void forgetUserFile() { userPath.clear(); }

    // The header combo's text: the factory name, else the user name, else the
    // nothing-loaded prompt, with " *" while edited.
    const char* preview(char* buf, size_t size) const
    {
        const auto& presets = getFactoryPresets();
        const char* name = nullptr;
        if (factory >= 0 && factory < static_cast<int>(presets.size()))
            name = presets[static_cast<size_t>(factory)].name;
        else if (!userName.empty())
            name = userName.c_str();
        if (name == nullptr) return "Presets...";
        std::snprintf(buf, size, "%s%s", name, modified ? " *" : "");
        return buf;
    }
};

} // namespace duskverb::ui
