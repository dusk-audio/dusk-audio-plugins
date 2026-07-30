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
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",  // macOS 13+ (Arial lives here now)
        "/System/Library/Fonts/Supplemental/Verdana Bold.ttf",// macOS fallback bold face
        "/Library/Fonts/Arial Bold.ttf",                      // older macOS (pre-Catalina layout)
        "C:/Windows/Fonts/segoeuib.ttf",   // Windows: Segoe UI Bold
        "C:/Windows/Fonts/arialbd.ttf",    // Windows: Arial Bold (fallback)
    };
    for (const char* path : kCandidates)
        if (FILE* f = std::fopen(path, "rb")) { std::fclose(f); return path; }
    return nullptr;
}

// Codepoints rasterized into the atlas. ImGui's default range is Basic Latin +
// Latin-1 Supplement only, so anything above U+00FF renders as the face's fallback
// box or '?'. Plugin labels use U+2033 DOUBLE PRIME for inches — a label ENDING in a
// literal '"' emits invalid Turtle in the generated LV2 TTL (see the note on
// tmparams::kHeadWidth), so the typographic mark is not cosmetic, it is the only
// spelling that survives both the TTL generator and the UI. Listed as two extra
// codepoints rather than all of General Punctuation to keep the atlas small: this
// set is rasterized once per font size, and the sets run up to CrispFontSet::kMax.
// A face missing these degrades to the same fallback glyph as before — no regression.
inline const ImWchar* crispGlyphRanges()
{
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement (the ImGui default)
        0x2032, 0x2033,   // PRIME, DOUBLE PRIME (feet / inches)
        0,
    };
    return ranges;
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
    cfg.GlyphRanges = crispGlyphRanges();
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
    cfg.GlyphRanges = crispGlyphRanges();

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

// Memory-backed counterpart used by plugins that bundle an OFL face so their
// typography is identical on macOS, Windows and Linux. `ttfData` must remain
// alive for the lifetime of the ImGui atlas; generated static byte arrays are
// the intended input. FontDataOwnedByAtlas stays false so ImGui never attempts
// to free that static storage.
inline CrispFontSet loadEmbeddedCrispFontSet(
    const unsigned char* ttfData, unsigned int ttfSize,
    const float* designSizes, int n, float scaleFactor)
{
    CrispFontSet set;
    if (ttfData == nullptr || ttfSize == 0)
        return set;

    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < n && set.count < CrispFontSet::kMax; ++i)
    {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = false;
        cfg.FontDataOwnedByAtlas = false;
        cfg.GlyphRanges = crispGlyphRanges();

        const float px = designSizes[i] * scaleFactor;
        if (ImFont* f = io.Fonts->AddFontFromMemoryTTF(
                const_cast<unsigned char*>(ttfData), static_cast<int>(ttfSize),
                px, &cfg, crispGlyphRanges()))
        {
            set.faces[set.count]    = f;
            set.nativePx[set.count] = px;
            ++set.count;
        }
    }
    if (set.count > 0)
        io.Fonts->Build();
    return set;
}

} // namespace duskdpf
