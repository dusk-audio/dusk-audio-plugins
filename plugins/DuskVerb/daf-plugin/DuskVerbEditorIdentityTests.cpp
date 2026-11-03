// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The editor's preset identity rules (DuskVerbEditorIdentity.hpp): what the
// header names after an editor reopens, a host restore or an A/B swap, and
// which library file the preset list highlights.
#include "DuskVerbEditorIdentity.hpp"

#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

bool previewIs(const duskverb::ui::EditorIdentity& id, const char* expected)
{
    char buf[96];
    const char* text = id.preview(buf, sizeof(buf));
    if (std::strcmp(text, expected) != 0) std::printf("preview: got \"%s\" expected \"%s\"\n", text, expected);
    return std::strcmp(text, expected) == 0;
}

duskverb::StateValues userSound(const char* name, bool edited = false)
{
    auto state = duskverb::makeDefaultState();
    state.userName = name;
    state.edited = edited;
    return state;
}

duskverb::StateValues factorySound(size_t index)
{
    auto state = duskverb::makeDefaultState();
    state.presetName = getFactoryPresets()[index].name;
    return state;
}
} // namespace

int main()
{
    using duskverb::ui::EditorIdentity;
    const auto& presets = getFactoryPresets();
    check(presets.size() > 5, "the factory list is large enough for these cases");

    // Editor reopen: the wrappers pass their cached program 0. With the plugin's
    // snapshot readable, the snapshot is the identity.
    {
        EditorIdentity id;
        const auto mine = userSound("Mine");
        id.programLoaded(0, &mine);
        check(id.factory == -1 && id.userName == "Mine" && previewIs(id, "Mine"),
              "reopen keeps a user preset instead of factory preset 0");

        const auto fifth = factorySound(5);
        id.programLoaded(0, &fifth);
        check(id.factory == 5 && id.userName.empty() && previewIs(id, presets[5].name),
              "reopen keeps the factory preset the plugin holds, not preset 0");

        const auto unnamed = duskverb::makeDefaultState();
        id.programLoaded(0, &unnamed);
        check(id.factory == -1 && id.userName.empty() && previewIs(id, "Presets..."),
              "reopen after INIT names nothing");

        const auto edited = userSound("Mine", true);
        id.programLoaded(0, &edited);
        check(previewIs(id, "Mine *"), "reopen keeps the edited marker");
    }

    // Without a bridge the wrapper's index is all there is.
    {
        EditorIdentity id;
        id.adopt(userSound("Mine", true));
        id.userFileOpened("/library/Mine.dvpreset");
        id.programLoaded(3, nullptr);
        check(id.factory == 3 && id.userName.empty() && id.userPath.empty() && !id.modified
                  && id.presetName == presets[3].name && previewIs(id, presets[3].name),
              "a host program change without a bridge adopts the index");
        id.programLoaded(static_cast<int>(presets.size()), nullptr);
        check(id.factory == 3, "an index past the end changes nothing");
        id.programLoaded(-1, nullptr);
        check(id.factory == 3, "a negative index changes nothing");
    }

    // A user preset names itself in the header but keeps the factory identity it
    // was made from, which is what the DSP's engine config (and so TONAL
    // CORRECTION's visibility) follows.
    {
        EditorIdentity id;
        auto mine = userSound("Mine");
        mine.presetName = presets[4].name;
        id.adopt(mine);
        check(id.factory == -1 && id.userName == "Mine" && id.presetName == presets[4].name,
              "a user preset keeps its factory identity");
        id.adopt(duskverb::makeDefaultState());
        check(id.presetName.empty(), "INIT clears the factory identity");
    }

    // The highlighted library file follows the name it was opened under.
    {
        EditorIdentity id;
        id.adopt(userSound("Foo"));
        id.userFileOpened("/library/Foo.dvpreset");
        check(id.userPath == "/library/Foo.dvpreset", "an opened file is highlighted");

        id.adopt(userSound("Foo", true));
        check(id.userPath == "/library/Foo.dvpreset" && previewIs(id, "Foo *"),
              "editing the opened preset keeps its file highlighted");

        id.adopt(userSound("Bar"));
        check(id.userPath.empty() && previewIs(id, "Bar"),
              "a host restore to another user preset drops the old highlight");

        id.adopt(userSound("Foo"));
        id.userFileOpened("/library/Foo.dvpreset");
        id.adopt(factorySound(2));
        check(id.userPath.empty() && id.factory == 2, "an A/B swap to a factory sound drops the highlight");

        id.adopt(userSound("Foo"));
        id.userFileOpened("/library/Foo.dvpreset");
        id.forgetUserFile();
        check(id.userPath.empty() && id.userName == "Foo", "forgetting the file keeps the sound's name");

        // A file that loaded without a name is not highlighted, so a factory
        // program (also unnamed) cannot inherit its highlight.
        id.adopt(duskverb::makeDefaultState());
        id.userFileOpened("/library/Unnamed.dvpreset");
        id.adopt(factorySound(1));
        check(id.userPath.empty(), "an unnamed file is never highlighted");
    }

    std::printf("DuskVerbEditorIdentityTests: %s (%d failures)\n",
                failures == 0 ? "all checks passed" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
