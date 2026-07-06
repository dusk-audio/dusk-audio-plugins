// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DuskImGuiFont.hpp — crisp bold font loader for Dear ImGui panels.
//
// The DPF DearImGui wrapper rasterizes its default atlas at one small size, so
// drawing text at any other size rescales bitmap glyphs and blurs. DPF's ImGui
// coordinate space is 1 unit = 1 physical pixel, so a label drawn at N units is
// N physical px tall; it is crisp only when an atlas glyph exists near N px.
//
// A single atlas can't be near-native for both ~9 px labels and ~26 px titles,
// so load the bold face at SEVERAL sizes once and let the panel pick the nearest
// per label (see DuskPanel::pickFont). No runtime atlas rebuild — that would
// need a backend texture re-upload the DPF wrapper doesn't do.
//
// Requires imgui.h to already be included by the translation unit.

#pragma once

#include <cmath>
#include <cstdio>

namespace duskdpf
{

// First installed bold candidate, or nullptr (minimal distro / macOS without
// the listed faces -> callers fall back to the ImGui default font).
inline const char* findCrispFontPath()
{
    static const char* kCandidates[] = {
        "/usr/share/fonts/truetype/LiberationSans-Bold.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
        "C:/Windows/Fonts/segoeuib.ttf",   // Windows: Segoe UI Bold
        "C:/Windows/Fonts/arialbd.ttf",    // Windows: Arial Bold (fallback)
    };
    for (const char* path : kCandidates)
        if (FILE* f = std::fopen(path, "rb")) { std::fclose(f); return path; }
    return nullptr;
}

// Loads a bold face at pixelSize into the ImGui atlas and builds it.
// Returns the ImFont* (or nullptr on failure). Kept for single-size callers.
inline ImFont* loadCrispFont(float pixelSize)
{
    const char* path = findCrispFontPath();
    if (path == nullptr) return nullptr;
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    ImFont* font = io.Fonts->AddFontFromFileTTF(path, pixelSize, &cfg);
    if (font != nullptr) io.Fonts->Build();
    return font;
}

// A set of the same bold face rasterized at several native pixel sizes.
struct CrispFontSet
{
    static constexpr int kMax = 10;
    ImFont* faces[kMax] = {};
    float   nativePx[kMax] = {};
    int     count = 0;

    ImFont* primary() const { return count > 0 ? faces[0] : nullptr; }

    // Nearest face to a requested draw size (physical px), so ImGui scales the
    // glyph by ~1x -> crisp. Returns nullptr if the set is empty.
    ImFont* pick(float px) const
    {
        int best = -1; float bestD = 1e30f;
        for (int i = 0; i < count; ++i)
        {
            const float d = std::fabs(nativePx[i] - px);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best >= 0 ? faces[best] : nullptr;
    }
};

// Load the bold face at each (designSize * scaleFactor) into one atlas + Build.
// designSizes should span the range of on-screen text sizes used by the UI.
inline CrispFontSet loadCrispFontSet(const float* designSizes, int n, float scaleFactor)
{
    CrispFontSet set;
    const char* path = findCrispFontPath();
    if (path == nullptr) return set; // empty -> caller falls back to default font

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    for (int i = 0; i < n && set.count < CrispFontSet::kMax; ++i)
    {
        const float px = designSizes[i] * scaleFactor;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(path, px, &cfg))
        {
            set.faces[set.count]    = f;
            set.nativePx[set.count] = px;
            ++set.count;
        }
    }
    if (set.count > 0) io.Fonts->Build();
    return set;
}

} // namespace duskdpf
