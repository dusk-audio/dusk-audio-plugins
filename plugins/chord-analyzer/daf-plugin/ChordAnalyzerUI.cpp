// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components are attributed in shared-daf/THIRD_PARTY_LICENSES.md.
#include "DafUI.hpp"
#include "ChordAnalyzerAccess.hpp"
#include "ChordAnalyzerRuntime.hpp"
#include "ChordMidiExport.hpp"
#include "DuskImGuiFont.hpp"
#include "DuskImGuiTextInput.hpp"
#include "ChordFontRegular.inc"
#include "ChordFontSemiBold.inc"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

START_NAMESPACE_DAF
using namespace duskaudio::chord;
namespace
{
constexpr float designWidth = 960, designHeight = 720;
constexpr ImU32 ink = IM_COL32(239, 236, 226, 255);
constexpr ImU32 dim = IM_COL32(171, 169, 159, 255);
constexpr ImU32 amber = IM_COL32(255, 171, 65, 255);
constexpr ImU32 card = IM_COL32(29, 29, 32, 255);
constexpr ImU32 edge = IM_COL32(59, 58, 57, 255);
bool blackKey(int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
int staffPosition(int note)
{
    constexpr int degrees[] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    return (note / 12 - 1) * 7 + degrees[note % 12];
}
}

class ChordAnalyzerUI : public UI
{
public:
    ChordAnalyzerUI() : UI(DAF_UI_DEFAULT_WIDTH, DAF_UI_DEFAULT_HEIGHT)
    {
        for (unsigned i = 0; i < PARAM_COUNT; ++i) values[i] = parameterSpecs[i].initial;
        setGeometryConstraints(static_cast<uint32_t>(designWidth), static_cast<uint32_t>(designHeight), true);
        static constexpr float sizes[] = {10, 12, 14, 16, 18, 24, 32, 44};
        GLint maxTexture = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
        if (const char* forced = std::getenv("DUSK_GL_MAX_TEXTURE"))
            if (const int size = std::atoi(forced); size > 0) maxTexture = size;
        const duskdaf::EmbeddedFontRequest requests[] = {
            {kChordFontRegular, kChordFontRegular_len, sizes, 8},
            {kChordFontSemiBold, kChordFontSemiBold_len, sizes, 8}
        };
        duskdaf::CrispFontSet fonts[2];
        duskdaf::loadEmbeddedCrispFontSets(requests, 2, fonts,
            std::max(1.0f, static_cast<float>(getScaleFactor())), maxTexture);
        regular = fonts[0]; bold = fonts[1];
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < PARAM_COUNT && std::isfinite(value)) values[index] = snapParameter(index, value);
    }
    void uiFileBrowserSelected(const char* filename) override
    {
        if (filename && *filename && !pendingBytes.empty())
        {
            try
            {
                status = midi::writeFile(std::filesystem::u8path(filename), pendingBytes)
                       ? "MIDI clip saved." : "Could not write the MIDI file. Choose a writable location.";
            }
            catch (...) {status = "Could not use that file path.";}
        }
        pendingBytes.clear();
    }
    void onImGuiDisplay() override
    {
        scale = std::min(static_cast<float>(getWidth()) / designWidth,
                         static_cast<float>(getHeight()) / designHeight);
        origin = {(getWidth() - designWidth * scale) * 0.5f, (getHeight() - designHeight * scale) * 0.5f};
        auto* const engine = runtime();
        AnalyzerView view;
        if (engine)
        {
            view = engine->view();
            // Seed a newly opened editor once. Afterwards host callbacks update
            // the UI mirror, retaining local edits between notifications.
            if (!inputsInitialized)
            {
                for (unsigned i = 0; i < inputParameterCount; ++i) values[i] = engine->parameter(i);
                inputsInitialized = true;
            }
        }
        else
        {
            view.chord.name = ChordAnalyzerText::decodeLabel(static_cast<int>(values[PARAM_DETECTED_CHORD]));
            view.chord.isValid = values[PARAM_DETECTED_ROOT] != 0;
        }
        // A Return/Clear action can reset preview later in this same frame.
        // Keep the displayed chord/voicing alive until drawing has finished.
        const bool previewing = preview.has_value();
        auto shown = view.chord;
        std::vector<ChordSuggestionText> previewSuggestions;
        const auto* displayedSuggestions = &view.suggestions;
        if (previewing)
        {
            ChordAnalyzerText context;
            context.setKey(static_cast<int>(values[PARAM_KEY_ROOT]), values[PARAM_KEY_MODE] != 0);
            shown = context.analyze(preview->midiNotes);
            previewSuggestions = context.getSuggestions(shown,
                static_cast<SuggestionCategory>(static_cast<int>(values[PARAM_SUGGESTION_LEVEL])));
            displayedSuggestions = &previewSuggestions;
        }

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({static_cast<float>(getWidth()), static_cast<float>(getHeight())});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("ChordAnalyzer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar(2);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8 * scale, 5 * scale});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8 * scale, 6 * scale});
        ImGui::PushStyleColor(ImGuiCol_Text, ink);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(42, 42, 46, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 46, 43, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(92, 67, 35, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(123, 81, 34, 255));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, amber);
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(105, 71, 33, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(29, 29, 32, 255));
        ImFont* body = regular.pick(16 * scale);
        if (body) ImGui::PushFont(body);
        draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({0, 0}, {static_cast<float>(getWidth()), static_cast<float>(getHeight())}, IM_COL32(17, 17, 19, 255));
        text(24, 17, 28, amber, DAF_PLUGIN_NAME, true);
        text(772, 21, 18, dim, "DUSK AUDIO", true);
        draw->AddLine(point(20, 57), point(940, 57), edge);
        controls();
        panel(20, 113, 940, 237);
        std::string label = shown.name;
        if (values[PARAM_SHOW_INVERSIONS] != 0 && shown.slashBass) label += shown.extensions;
        if (label.empty() || label == "-") label = "Play some notes";
        auto* titleFont = bold.pick(44 * scale);
        const float titleWidth = titleFont ? titleFont->CalcTextSizeA(44 * scale, 10000, 0, label.c_str()).x / scale : 0;
        text(36, 127, titleWidth > 710 ? std::max(20.0f, 44 * 710 / titleWidth) : 44,
             shown.isValid ? ink : dim, label.c_str(), true);
        text(38, 186, 24, amber, shown.romanNumeral.c_str(), true);
        const auto function = shown.isValid ? ChordAnalyzerText::functionToString(shown.function) : std::string{};
        text(128, 191, 16, dim, function.c_str());
        std::string notes;
        const auto& displayNotes = previewing ? shown.midiNotes : view.notes;
        for (int note : displayNotes)
        {
            if (!notes.empty()) notes += "  ";
            notes += ChordAnalyzerText::noteToName(note);
        }
        draw->PushClipRect(point(36, 213), point(748, 234), true);
        text(38, 214, 14, dim, notes.empty() ? "MIDI input / all channels" : notes.c_str());
        draw->PopClipRect();
        ImGui::SetCursorScreenPos(point(36, 212)); ImGui::InvisibleButton("noteNames", {710 * scale, 22 * scale});
        if (!notes.empty()) tip(notes.c_str());
        text(770, 125, 12, amber, previewing ? "HISTORY PREVIEW" : "LIVE ANALYSIS", true);
        if (button("Save chord MIDI...", 770, 153, 154, 34, shown.isValid && !shown.midiNotes.empty()))
            save(midi::chordBytes(shown), "chord.mid");
        tip("Export this voicing as a one-bar MIDI clip at 120 BPM.");
        if (previewing && button("Return to live", 770, 195, 154, 28)) preview.reset();

        keyboard(displayNotes, previewing);
        staff(displayNotes);
        history(view, engine);
        suggestions(*displayedSuggestions);
        recording(view, engine);
        saveDialog();
        draw->PushClipRect(point(24, 702), point(895, 719), true);
        text(24, 704, 10, dim, status.empty() ? "Hover a control or suggestion for help." : status.c_str());
        draw->PopClipRect();
        char version[32];
        std::snprintf(version, sizeof(version), "%d.%d.%d", CA2_VERSION_MAJOR, CA2_VERSION_MINOR, CA2_VERSION_PATCH);
        text(908, 704, 10, dim, version);
        if (body) ImGui::PopFont();
        ImGui::PopStyleColor(8);
        ImGui::PopStyleVar(2);
        ImGui::End();
        textInputFocus.update(*this);
    }

private:
    ChordAnalyzerRuntime* runtime() const noexcept
    {
        if (chordAnalyzerGetRuntime)
            if (void* instance = getPluginInstancePointer()) return chordAnalyzerGetRuntime(instance);
        return nullptr;
    }
    ImVec2 point(float x, float y) const {return {origin.x + x * scale, origin.y + y * scale};}
    void text(float x, float y, float size, ImU32 color, const char* value, bool heavy = false)
    {
        auto* font = (heavy ? bold : regular).pick(size * scale);
        if (!font) font = ImGui::GetFont();
        draw->AddText(font, size * scale, point(x, y), color, value);
    }
    void panel(float x0, float y0, float x1, float y1)
    {
        draw->AddRectFilled(point(x0, y0), point(x1, y1), card, 7 * scale);
        draw->AddRect(point(x0, y0), point(x1, y1), edge, 7 * scale);
    }
    bool button(const char* label, float x, float y, float w, float h, bool enabled = true)
    {
        ImGui::SetCursorScreenPos(point(x, y));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(label, {w * scale, h * scale});
        ImGui::EndDisabled();
        return clicked;
    }
    void tip(const char* value)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(350 * scale);
            ImGui::TextUnformatted(value); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
    }
    void set(unsigned id, float value)
    {
        values[id] = snapParameter(id, value);
        editParameter(id, true); setParameterValue(id, values[id]); editParameter(id, false);
    }
    void combo(unsigned id, float x, float width, const char* help)
    {
        const auto labels = parameterChoices(id);
        int value = static_cast<int>(values[id]);
        ImGui::SetCursorScreenPos(point(x, 75)); ImGui::SetNextItemWidth(width * scale);
        ImGui::PushID(static_cast<int>(id));
        if (ImGui::Combo("##choice", &value, labels.labels, static_cast<int>(labels.count))) set(id, static_cast<float>(value));
        ImGui::PopID(); tip(help);
    }
    void controls()
    {
        text(24, 60, 10, dim, "KEY", true);
        text(268, 60, 10, dim, "SUGGESTIONS", true);
        combo(PARAM_KEY_ROOT, 24, 120, "Key root for Roman numerals and harmonic suggestions.");
        combo(PARAM_KEY_MODE, 154, 100, "Analyze harmony in a major or minor key.");
        combo(PARAM_SUGGESTION_LEVEL, 268, 225, "Basic: common progressions. Intermediate adds borrowed chords; Advanced adds chromatic choices.");
        bool inversions = values[PARAM_SHOW_INVERSIONS] != 0;
        ImGui::SetCursorScreenPos(point(520, 80));
        if (ImGui::Checkbox("Show inversions", &inversions)) set(PARAM_SHOW_INVERSIONS, inversions ? 1.0f : 0.0f);
        tip("Append the voiced bass note after a slash in the displayed chord name.");
        bool sustain = values[PARAM_RESPECT_SUSTAIN] != 0;
        ImGui::SetCursorScreenPos(point(730, 80));
        if (ImGui::Checkbox("Respect sustain", &sustain)) set(PARAM_RESPECT_SUSTAIN, sustain ? 1.0f : 0.0f);
        tip("CC64 holds released keys on each MIDI channel. Turning this off releases notes held only by the pedal.");
    }
    void keyboard(const std::vector<int>& notes, bool previewing)
    {
        panel(20, 248, 526, 404); text(34, 257, 12, dim, previewing ? "PREVIEW VOICING" : "HELD NOTES", true);
        int low = 48, high = 83;
        for (int n : notes) {low = std::min(low, (n / 12) * 12); high = std::max(high, std::min(127, (n / 12) * 12 + 11));}
        int whites = 0;
        for (int n = low; n <= high; ++n) if (!blackKey(n)) ++whites;
        const float width = 478.0f / static_cast<float>(whites);
        const auto active = [&](int n) {return std::find(notes.begin(), notes.end(), n) != notes.end();};
        int index = 0;
        for (int n = low; n <= high; ++n)
            if (!blackKey(n))
            {
                const float x = 34 + index++ * width;
                draw->AddRectFilled(point(x, 282), point(x + width - 1, 389), active(n) ? amber : IM_COL32(216, 213, 201, 255), 2 * scale);
                if (n % 12 == 0)
                {
                    const auto label = ChordAnalyzerText::noteToName(n);
                    text(x + 2, 372, std::min(10.0f, width * 0.55f), IM_COL32(40, 38, 34, 255), label.c_str());
                }
            }
        index = 0;
        for (int n = low; n <= high; ++n)
            if (!blackKey(n)) ++index;
            else
            {
                const float x = 34 + (index - 0.32f) * width;
                draw->AddRectFilled(point(x, 282), point(x + width * 0.64f, 348), active(n) ? IM_COL32(211, 126, 36, 255) : IM_COL32(19, 19, 21, 255), 2 * scale);
            }
    }
    void staff(const std::vector<int>& notes)
    {
        panel(538, 248, 940, 404); text(552, 257, 12, dim, "STAFF / C4 IS MIDDLE C", true);
        int low = 16, high = 40;
        for (int n : notes) {low = std::min(low, staffPosition(n) - 2); high = std::max(high, staffPosition(n) + 2);}
        const float step = 108.0f / static_cast<float>(high - low);
        const auto y = [&](int position) {return 387.0f - (position - low) * step;};
        for (int base : {18, 30}) for (int i = 0; i < 5; ++i)
            draw->AddLine(point(615, y(base + i * 2)), point(924, y(base + i * 2)), IM_COL32(108, 107, 102, 255));
        text(550, y(36) - 5, 12, dim, "Treble"); text(550, y(24) - 5, 12, dim, "Bass");
        const float spacing = std::min(32.0f, 284.0f / static_cast<float>(std::max<std::size_t>(1, notes.size())));
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const int n = notes[i], pos = staffPosition(n), base = pos >= 28 ? 30 : 18;
            const float x = 636 + static_cast<float>(i) * spacing;
            for (int ledger = base - 2; ledger >= pos; ledger -= 2)
                draw->AddLine(point(x - 7, y(ledger)), point(x + 7, y(ledger)), dim);
            for (int ledger = base + 10; ledger <= pos; ledger += 2)
                draw->AddLine(point(x - 7, y(ledger)), point(x + 7, y(ledger)), dim);
            const float radius = std::min(4.5f, step * 0.9f);
            draw->AddCircleFilled(point(x, y(pos)), radius * scale, amber);
            if (blackKey(n)) text(x - 13, y(pos) - 7, 12, dim, "#");
        }
    }
    void history(const AnalyzerView& view, ChordAnalyzerRuntime* engine)
    {
        panel(20, 415, 940, 497); text(34, 424, 12, dim, "RECENT CHORDS", true);
        if (button("Clear history", 830, 421, 96, 24, engine != nullptr)) {engine->clearHistory(); preview.reset();}
        ImGui::SetCursorScreenPos(point(32, 447));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12 * scale);
        ImGui::BeginChild("history", {896 * scale, 44 * scale}, false, ImGuiWindowFlags_HorizontalScrollbar);
        if (view.history.empty()) ImGui::TextDisabled("Distinct chords appear here as you play.");
        for (std::size_t i = 0; i < view.history.size(); ++i)
        {
            if (i != 0) ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button(view.history[i].name.c_str(), {0, 26 * scale})) preview = view.history[i];
            tip("Preview this chord and export its original voicing. Return to live follows incoming notes.");
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }
    void suggestions(const std::vector<ChordSuggestionText>& items)
    {
        text(24, 509, 12, dim, "TRY NEXT", true);
        for (std::size_t i = 0; i < 6; ++i)
        {
            const float x = 20 + static_cast<float>(i % 3) * 312, y = 531 + static_cast<float>(i / 3) * 53;
            ImGui::PushID(static_cast<int>(i));
            if (i < items.size())
            {
                const auto& suggestion = items[i];
                const auto label = suggestion.romanNumeral + "   " + suggestion.chordName;
                if (button(label.c_str(), x, y, 296, 43)) status = suggestion.reason;
                tip(suggestion.reason.c_str());
            }
            else button("-", x, y, 296, 43, false);
            ImGui::PopID();
        }
    }
    void recording(const AnalyzerView& view, ChordAnalyzerRuntime* engine)
    {
        draw->AddLine(point(20, 644), point(940, 644), edge);
        if (view.recording) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(145, 46, 38, 255));
        if (button(view.recording ? "Stop recording" : "Record", 20, 659, 132, 32, engine != nullptr))
        {
            if (view.recording) engine->stopRecording();
            else engine->startRecording(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }
        if (view.recording) ImGui::PopStyleColor();
        tip("Record distinct played chords. Stop includes the chord you are still holding.");
        if (button("Clear", 164, 659, 74, 32, engine != nullptr)) engine->clearRecording();
        tip("Clear the recorded progression. Recording remains armed if it is running.");
        if (button("Save progression MIDI...", 250, 659, 200, 32, engine && view.recordedEvents > 0))
        {
            const auto session = engine->session();
            save(midi::sessionBytes(session.events, session.tempoBPM), "chord_progression.mid");
        }
        tip("Export completed chords at the recording's captured tempo. Stop first to include the held final chord.");
        char summary[128];
        std::snprintf(summary, sizeof(summary), "%s  /  %d events  /  Host %.1f BPM", view.recording ? "RECORDING" : "READY", view.recordedEvents, view.tempoBPM);
        text(478, 666, 16, view.recording ? amber : dim, summary);
    }
    void save(midi::Bytes bytes, const char* defaultName)
    {
        if (bytes.empty()) {status = "No completed notes to export."; return;}
        pendingBytes = std::move(bytes);
        FileBrowserOptions options; options.saving = true; options.defaultName = defaultName; options.title = "Export MIDI clip";
        if (!openFileBrowser(options))
        {
            // X11's native fallback only opens files; without a desktop portal
            // it cannot save. Keep export usable there with an explicit path.
            try
            {
                std::error_code error;
                auto directory = std::filesystem::current_path(error);
                if (const char* home = std::getenv("HOME")) directory = std::filesystem::u8path(home);
                if (std::filesystem::is_directory(directory / "Documents", error)) directory /= "Documents";
                const auto utf8 = (directory / defaultName).u8string();
                if (utf8.size() < savePath.size()) std::snprintf(savePath.data(), savePath.size(), "%s", utf8.c_str());
                else savePath[0] = '\0';
            }
            catch (...) {savePath[0] = '\0';}
            overwrite = false; saveError.clear(); saveRequested = true;
        }
    }
    void saveDialog()
    {
        if (saveRequested) {ImGui::OpenPopup("Save MIDI clip"); saveRequested = false;}
        ImGui::SetNextWindowPos(point(480, 350), ImGuiCond_Appearing, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({640 * scale, 184 * scale}, ImGuiCond_Always);
        bool open = true;
        if (ImGui::BeginPopupModal("Save MIDI clip", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetCursorPos({16 * scale, 38 * scale});
            ImGui::TextUnformatted("Destination (.mid)");
            ImGui::SetCursorPos({16 * scale, 60 * scale});
            ImGui::SetNextItemWidth(608 * scale);
            const bool enter = ImGui::InputText("##destination", savePath.data(), savePath.size(),
                ImGuiInputTextFlags_EnterReturnsTrue | (overwrite ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_AutoSelectAll));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
            ImGui::SetCursorPos({16 * scale, 96 * scale});
            if (overwrite) ImGui::TextUnformatted("This file already exists. Replace it?");
            if (!saveError.empty()) ImGui::TextWrapped("%s", saveError.c_str());
            ImGui::SetCursorPos({16 * scale, 132 * scale});
            const bool clicked = ImGui::Button(overwrite ? "Replace file" : "Save", {120 * scale, 30 * scale});
            if (enter || clicked)
            {
                try
                {
                    const auto destination = std::filesystem::u8path(savePath.data());
                    std::error_code error;
                    const bool exists = std::filesystem::exists(destination, error);
                    if (destination.empty() || error || std::filesystem::is_directory(destination, error))
                        saveError = "Choose a writable file path.";
                    else if (exists && !overwrite) {overwrite = true; saveError.clear();}
                    else if (midi::writeFile(destination, pendingBytes))
                    {
                        pendingBytes.clear(); status = "MIDI clip saved."; ImGui::CloseCurrentPopup();
                    }
                    else saveError = "Could not write the MIDI file. Choose a writable location.";
                }
                catch (...) {saveError = "Could not use that file path.";}
            }
            if (overwrite)
            {
                ImGui::SameLine();
                if (ImGui::Button("Back", {100 * scale, 30 * scale})) overwrite = false;
            }
            ImGui::SetCursorPos({504 * scale, 132 * scale});
            if (ImGui::Button("Cancel", {120 * scale, 30 * scale}))
            {pendingBytes.clear(); ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        if (!open) pendingBytes.clear();
    }
    std::array<float, PARAM_COUNT> values{};
    std::optional<ChordDescription> preview;
    midi::Bytes pendingBytes;
    std::string status;
    std::array<char, 4096> savePath{};
    std::string saveError;
    bool saveRequested = false, overwrite = false;
    bool inputsInitialized = false;
    duskdaf::DuskImGuiTextInputFocus textInputFocus;
    duskdaf::CrispFontSet regular, bold;
    float scale = 1;
    ImVec2 origin{};
    ImDrawList* draw = nullptr;
};

UI* createUI() {return new ChordAnalyzerUI;}
END_NAMESPACE_DAF
