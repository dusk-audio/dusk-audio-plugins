// DuskImGuiFont.hpp — crisp bold font loader for Dear ImGui panels.
//
// The DPF DearImGui wrapper rasterizes its default atlas at 13 px, so drawing
// text at any other size rescales bitmap glyphs and blurs. Load a real bold
// TTF at high resolution once in the UI constructor and draw with it. If no
// candidate font exists (minimal distro / macOS), returns nullptr and callers
// fall back to the default font — losing sharpness, never the text.
//
// Requires imgui.h to already be included by the translation unit (the DPF UI
// source includes it via DistrhoUI + the DearImGui custom-widget include).

#pragma once

#include <cstdio>

namespace duskdpf
{

// Loads a bold face at pixelSize into the ImGui atlas and builds it.
// Returns the ImFont* (or nullptr on failure). Call from the UI constructor.
inline ImFont* loadCrispFont(float pixelSize)
{
    ImGuiIO& io = ImGui::GetIO();
    static const char* kCandidates[] = {
        "/usr/share/fonts/truetype/LiberationSans-Bold.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
    };
    ImFont* font = nullptr;
    for (const char* path : kCandidates)
    {
        if (FILE* f = std::fopen(path, "rb"))
        {
            std::fclose(f);
            font = io.Fonts->AddFontFromFileTTF(path, pixelSize);
            break;
        }
    }
    if (font != nullptr)
        io.Fonts->Build();
    return font;
}

} // namespace duskdpf
