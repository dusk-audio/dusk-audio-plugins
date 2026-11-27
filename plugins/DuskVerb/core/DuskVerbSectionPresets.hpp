// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).

#pragma once

#include "dsp/SectionReverbEngine.h"
#include <algorithm>
#include <string_view>

namespace duskverb
{
// Each section reverb preset pairs its engine configuration with the
// DuskVerb control values it belongs to. The controls move the configuration from there
// (DuskVerbDSP::pushSectionParameters), so the preset at its own values plays exactly this
// configuration. A preset without a row of the engine's model, or a session that switched another
// preset onto the engine, plays that model's first row.
struct SectionReverbRow
{
    std::string_view name;
    SectionReverbConfig config;
    float decay, size, predelayMs, damping, bassMult, crossover, modDepth, modRate, diffusion, erLevel, erSize;
    bool tuneEarlyBass = false;
    std::string_view previousName {};  // factory identity written by earlier builds
};

// The factory controls and their DSP anchors are derived together, including on
// hosts where the public control range is narrower than the internal range.
constexpr SectionReverbRow studioRow(const char* name, const char* previousName, SectionReverbConfig c)
{
    const bool room = c.model == static_cast<int>(SectionReverbModel::Room);
    const bool auditorium = c.model == static_cast<int>(SectionReverbModel::Auditorium);
    const float depth = auditorium ? c.chorusDepth / 100.0f : c.wanderMs / 24.0f;
    return {name, c, c.reverbTime, (c.sizeMeters - 2.0f) / 78.0f,
            room ? c.earlyPredelayMs : c.predelayMs, 1.0f, std::clamp(c.bassRt, 0.3f, 2.5f),
            std::clamp(c.bassXoverHz, 200.0f, 4000.0f), depth,
            std::max(0.1f, auditorium ? c.chorusRate : c.spinHz), c.diffusion / 100.0f,
            0.5f, room ? (c.roomScale - 0.5f) / 1.5f : c.spread / 100.0f, room, previousName};
}

inline constexpr SectionReverbRow kVocalHallRoom = [] {
    SectionReverbConfig c;
    c.model = static_cast<int>(SectionReverbModel::Room);
    c.pattern = 3;
    c.diffusion = 30.0f;
    c.reverbPredelayMs = 110.0f;
    c.roomScale = 0.9f;
    c.roomBassXoverHz = 75.0f;
    c.roomBassBoostDb = 8.0f;
    c.sizeMeters = 30.0f;
    c.reverbTime = 3.64475036f;
    c.bassRt = 1.5f;
    c.bassXoverHz = 700.0f;
    c.rtHiCutHz = 6500.0f;
    c.damping = 1;
    c.tapSlope = 0;
    c.spinHz = 2.1f;
    c.wanderMs = 8.0f;
    c.tailWidthDeg = 352.0f;
    c.earlyLevelDb = -22.0f;
    c.earlyFilter = { 0, 18000.0f, 2.2f, -96.0f };
    c.reverbLevelDb = -22.0f;
    c.reverbFilter = { 2, 6500.0f, 1.0f, -48.0f };
    auto row = studioRow("Vocal Hall", "", c);
    row.decay = 5.226f;  // internal anchor of the shared factory row and its saved sessions
    return row;
}();

inline constexpr SectionReverbRow kStudioPresets[] {
    studioRow("Warm Chamber", "Amber Chamber", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Chamber);
        c.sizeMeters = 19.5f; c.diffusion = 100.0f; c.reverbTime = 1.3970733f;
        c.bassRt = 1.75f; c.bassXoverHz = 600.0f; c.rtHiCutHz = 4500.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 2.1f; c.wanderMs = 5.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 0.0f; c.shape = 25.0f; c.spread = 38.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 100.0f;
        c.earlyLevelDb = -9.5f; c.earlyFilter = { 0, 7500.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -5.5f; c.reverbFilter = { 0, 7000.0f, 1.1f, -96.0f };
        c.reflections[0] = { 9.0f, 0, -2.0f };
        c.reflections[1] = { 12.0f, 0, 0.0f };
        c.echoes[0] = { 12.0f, 0, 0.0f, 1, -96.0f };
        c.echoes[1] = { 12.0f, 0, 0.0f, 1, -96.0f };
        return c;
    }()),
    studioRow("Medium Chamber", "Linen Chamber", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Chamber);
        c.sizeMeters = 24.0f; c.diffusion = 60.0f; c.reverbTime = 1.7763275f;
        c.bassRt = 1.5f; c.bassXoverHz = 325.0f; c.rtHiCutHz = 5500.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 0.0f; c.wanderMs = 21.6f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 0.0f; c.shape = 20.0f; c.spread = 27.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 100.0f;
        c.earlyLevelDb = -7.5f; c.earlyFilter = { 0, 18000.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -7.5f; c.reverbFilter = { 0, 10000.0f, 1.1f, -96.0f };
        c.reflections[0] = { 4.0f, 0, 0.0f };
        c.reflections[1] = { 6.0f, 0, 0.0f };
        c.echoes[0] = { 35.0f, 0, -96.0f, 1, -96.0f };
        c.echoes[1] = { 45.0f, 0, -96.0f, 1, -96.0f };
        return c;
    }()),
    studioRow("Dark Chamber", "Velvet Alcove", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Chamber);
        c.sizeMeters = 15.5f; c.diffusion = 75.0f; c.reverbTime = 1.1517881f;
        c.bassRt = 1.5f; c.bassXoverHz = 450.0f; c.rtHiCutHz = 3500.0f; c.damping = 2;
        c.tapSlope = 0; c.spinHz = 0.0f; c.wanderMs = 21.6f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 0.0f; c.shape = 16.0f; c.spread = 26.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 0.0f;
        c.earlyLevelDb = -7.0f; c.earlyFilter = { 0, 3500.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -4.0f; c.reverbFilter = { 2, 3250.0f, 1.1f, -96.0f };
        c.reflections[0] = { 4.0f, 0, 0.0f };
        c.reflections[1] = { 2.0f, 0, 0.0f };
        c.echoes[0] = { 5.0f, 0, -5.0f, 1, 0.0f };
        c.echoes[1] = { 7.0f, 0, -5.0f, 1, 0.0f };
        return c;
    }()),
    studioRow("Small Wood Room", "Timber Loft", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Auditorium);
        c.sizeMeters = 7.5f; c.diffusion = 6.0f; c.reverbTime = 0.60520834f;
        c.bassRt = 0.25f; c.bassXoverHz = 500.0f; c.rtHiCutHz = 1000.0f;
        c.tailWidthDeg = 80.0f;
        c.definition = 100.0f; c.chorusRate = 0.0f; c.chorusDepth = 0.0f; c.chorusType = 0;
        c.predelayMs = 0.0f; c.shape = 15.0f; c.spread = 31.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -96.0f; c.earlyFilter = { 0, 7000.0f, 2.7f, -96.0f };
        c.reverbLevelDb = 0.0f; c.reverbFilter = { 0, 20000.0f, 2.0f, -96.0f };
        c.reflections[0] = { 212.0f, 0, -66.0f };
        c.reflections[1] = { 140.0f, 0, -66.0f };
        return c;
    }()),
    studioRow("Vocal Concert Hall", "Choir Balcony", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Auditorium);
        c.sizeMeters = 63.0f; c.diffusion = 57.0f; c.reverbTime = 1.8218846f;
        c.bassRt = 1.25f; c.bassXoverHz = 900.0f; c.rtHiCutHz = 6000.0f;
        c.tailWidthDeg = 0.0f;
        c.definition = 20.0f; c.chorusRate = 3.0f; c.chorusDepth = 49.0f; c.chorusType = 0;
        c.predelayMs = 8.0f; c.shape = 8.0f; c.spread = 17.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -22.0f; c.earlyFilter = { 0, 12000.0f, 2.7f, -96.0f };
        c.reverbLevelDb = -1.0f; c.reverbFilter = { 0, 9000.0f, 2.0f, -96.0f };
        c.reflections[0] = { 21.0f, 0, -2.5f };
        c.reflections[1] = { 18.0f, 0, -2.5f };
        return c;
    }()),
    studioRow("Concert Hall", "Radio Auditorium", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Auditorium);
        c.sizeMeters = 46.0f; c.diffusion = 100.0f; c.reverbTime = 1.3948051f;
        c.bassRt = 1.2f; c.bassXoverHz = 800.0f; c.rtHiCutHz = 17000.0f;
        c.tailWidthDeg = 0.0f;
        c.definition = 0.0f; c.chorusRate = 0.0f; c.chorusDepth = 0.0f; c.chorusType = 0;
        c.predelayMs = 0.0f; c.shape = 0.0f; c.spread = 0.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -23.0f; c.earlyFilter = { 4, 1525.0f, 2.7f, -96.0f };
        c.reverbLevelDb = 0.0f; c.reverbFilter = { 4, 825.0f, 2.3f, -96.0f };
        c.reflections[0] = { 100.0f, 0, 0.0f };
        c.reflections[1] = { 100.0f, 0, 0.0f };
        return c;
    }()),
    studioRow("Wide Hall", "Gallery Hall", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Hall);
        c.sizeMeters = 35.0f; c.diffusion = 97.0f; c.reverbTime = 1.550008f;
        c.bassRt = 1.5f; c.bassXoverHz = 600.0f; c.rtHiCutHz = 4500.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 2.9f; c.wanderMs = 15.0f; c.tailWidthDeg = 338.0f;
        c.predelayMs = 14.0f; c.shape = 15.0f; c.spread = 51.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -1.5f; c.earlyFilter = { 0, 7500.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -5.5f; c.reverbFilter = { 0, 4750.0f, 1.1f, -96.0f };
        c.reflections[0] = { 22.0f, 0, -6.0f };
        c.reflections[1] = { 26.0f, 0, -6.0f };
        c.reflections[2] = { 22.0f, 1, -10.0f };
        c.reflections[3] = { 26.0f, 1, -10.0f };
        return c;
    }()),
    studioRow("Studio Hall", "Scoring Room", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Hall);
        c.sizeMeters = 14.0f; c.diffusion = 55.0f; c.reverbTime = 1.8166975f;
        c.bassRt = 1.25f; c.bassXoverHz = 225.0f; c.rtHiCutHz = 6000.0f; c.damping = 2;
        c.tapSlope = 0; c.spinHz = 2.5f; c.wanderMs = 7.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 5.0f; c.shape = 15.0f; c.spread = 47.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -3.0f; c.earlyFilter = { 0, 5000.0f, 1.1f, -66.0f };
        c.reverbLevelDb = -4.0f; c.reverbFilter = { 0, 4000.0f, 1.1f, -16.0f };
        c.reflections[0] = { 4.0f, 0, -9.0f };
        c.reflections[1] = { 10.0f, 0, -9.0f };
        c.reflections[2] = { 18.0f, 0, -11.0f };
        c.reflections[3] = { 20.0f, 0, -11.0f };
        return c;
    }()),
    studioRow("Large Warm Hall", "Distant Balcony", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Hall);
        c.sizeMeters = 32.0f; c.diffusion = 93.0f; c.reverbTime = 2.793532f;
        c.bassRt = 2.25f; c.bassXoverHz = 600.0f; c.rtHiCutHz = 4500.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 2.1f; c.wanderMs = 20.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 20.0f; c.shape = 32.0f; c.spread = 50.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -2.0f; c.earlyFilter = { 4, 950.0f, 1.7f, -10.0f };
        c.reverbLevelDb = -2.0f; c.reverbFilter = { 4, 950.0f, 1.7f, -30.0f };
        c.reflections[0] = { 34.0f, 0, -3.0f };
        c.reflections[1] = { 35.0f, 0, -3.0f };
        c.reflections[2] = { 49.0f, 0, -6.5f };
        c.reflections[3] = { 46.0f, 0, -6.5f };
        return c;
    }()),
    studioRow("Large Plate", "Silver Canopy", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::StudioPlate);
        c.sizeMeters = 35.5f; c.diffusion = 100.0f; c.reverbTime = 1.5925324f;
        c.bassRt = 1.0f; c.bassXoverHz = 300.0f; c.rtHiCutHz = 5625.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 0.0f; c.wanderMs = 1.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 10.0f; c.shape = 8.0f; c.spread = 45.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 0.0f;
        c.earlyLevelDb = -13.0f; c.earlyFilter = { 0, 3800.0f, 1.0f, -96.0f };
        c.reverbLevelDb = -7.0f; c.reverbFilter = { 0, 5000.0f, 1.0f, -96.0f };
        c.reflections[0] = { 7.0f, 0, -9.0f };
        c.reflections[1] = { 11.0f, 0, -9.0f };
        c.echoes[0] = { 17.0f, 0, -15.0f, 1, 0.0f };
        c.echoes[1] = { 25.0f, 0, -17.0f, 1, 0.0f };
        return c;
    }()),
    studioRow("Tight Plate", "Satin Foil", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::StudioPlate);
        c.sizeMeters = 23.0f; c.diffusion = 100.0f; c.reverbTime = 1.0831125f;
        c.bassRt = 0.75f; c.bassXoverHz = 230.0f; c.rtHiCutHz = 4800.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 1.0f; c.wanderMs = 1.2f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 0.0f; c.shape = 64.0f; c.spread = 8.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 0.0f;
        c.earlyLevelDb = -13.0f; c.earlyFilter = { 0, 8000.0f, 1.5f, -96.0f };
        c.reverbLevelDb = -5.0f; c.reverbFilter = { 0, 6625.0f, 2.0f, -96.0f };
        c.reflections[0] = { 13.0f, 0, -9.0f };
        c.reflections[1] = { 11.0f, 0, -9.0f };
        c.echoes[0] = { 8.0f, 0, -18.0f, 1, 0.0f };
        c.echoes[1] = { 9.0f, 0, -18.0f, 1, 0.0f };
        return c;
    }()),
    studioRow("Small Plate", "Pocket Silver", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::StudioPlate);
        c.sizeMeters = 20.0f; c.diffusion = 100.0f; c.reverbTime = 1.1017259f;
        c.bassRt = 1.0f; c.bassXoverHz = 300.0f; c.rtHiCutHz = 6625.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 0.0f; c.wanderMs = 1.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 0.0f; c.shape = 8.0f; c.spread = 7.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 0.0f;
        c.earlyLevelDb = -13.0f; c.earlyFilter = { 0, 4600.0f, 1.0f, -96.0f };
        c.reverbLevelDb = -7.0f; c.reverbFilter = { 0, 6625.0f, 0.1f, -96.0f };
        c.reflections[0] = { 3.0f, 0, -9.0f };
        c.reflections[1] = { 5.0f, 0, -9.0f };
        c.echoes[0] = { 9.0f, 0, -15.0f, 1, 0.0f };
        c.echoes[1] = { 17.0f, 0, -17.0f, 1, 0.0f };
        return c;
    }()),
    studioRow("Lush Hall Medium", "Drifting Arcade", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::ModulatedHall);
        c.sizeMeters = 37.5f; c.diffusion = 61.0f; c.reverbTime = 2.139446f;
        c.bassRt = 1.5f; c.bassXoverHz = 375.0f; c.rtHiCutHz = 6000.0f; c.damping = 2;
        c.tapSlope = -2; c.spinHz = 2.5f; c.wanderMs = 22.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 21.0f; c.shape = 11.0f; c.spread = 85.0f;
        c.reflectionTimePercent = 85.0f;
        c.earlyLevelDb = -1.0f; c.earlyFilter = { 0, 7000.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -3.0f; c.reverbFilter = { 0, 2000.0f, 1.1f, -10.0f };
        c.reflections[0] = { 31.0f, 0, -10.0f };
        c.reflections[1] = { 34.0f, 0, -10.0f };
        c.reflections[2] = { 43.0f, 0, -13.0f };
        c.reflections[3] = { 46.0f, 0, -13.0f };
        return c;
    }()),
    studioRow("Lush Hall Long", "Floating Gallery", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::ModulatedHall);
        c.sizeMeters = 32.0f; c.diffusion = 75.0f; c.reverbTime = 2.7095518f;
        c.bassRt = 1.75f; c.bassXoverHz = 450.0f; c.rtHiCutHz = 6000.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 2.5f; c.wanderMs = 22.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 21.0f; c.shape = 16.0f; c.spread = 50.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = -1.5f; c.earlyFilter = { 0, 7000.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -3.5f; c.reverbFilter = { 0, 7500.0f, 1.1f, -96.0f };
        c.reflections[0] = { 31.0f, 0, -10.0f };
        c.reflections[1] = { 34.0f, 0, -10.0f };
        c.reflections[2] = { 43.0f, 0, -13.0f };
        c.reflections[3] = { 46.0f, 0, -13.0f };
        return c;
    }()),
    studioRow("Lush Hall Dark", "Midnight Forum", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::ModulatedHall);
        c.sizeMeters = 40.0f; c.diffusion = 75.0f; c.reverbTime = 3.5706057f;
        c.bassRt = 1.75f; c.bassXoverHz = 375.0f; c.rtHiCutHz = 4500.0f; c.damping = 1;
        c.tapSlope = -2; c.spinHz = 2.5f; c.wanderMs = 22.0f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 25.0f; c.shape = 16.0f; c.spread = 57.0f;
        c.reflectionTimePercent = 100.0f;
        c.earlyLevelDb = 0.0f; c.earlyFilter = { 0, 7000.0f, 1.1f, -96.0f };
        c.reverbLevelDb = -1.0f; c.reverbFilter = { 4, 800.0f, 1.6f, -42.0f };
        c.reflections[0] = { 41.0f, 0, -10.0f };
        c.reflections[1] = { 42.0f, 0, -10.0f };
        c.reflections[2] = { 61.0f, 0, -13.0f };
        c.reflections[3] = { 68.0f, 0, -13.0f };
        return c;
    }()),
    studioRow("Tight Live Room", "Tracking Booth", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Room);
        c.sizeMeters = 34.5f; c.diffusion = 18.0f; c.reverbTime = 0.32142687f;
        c.bassRt = 2.0f; c.bassXoverHz = 450.0f; c.rtHiCutHz = 2500.0f; c.damping = 1;
        c.tapSlope = 10; c.spinHz = 0.0f; c.wanderMs = 19.0f; c.tailWidthDeg = 0.0f;
        c.pattern = 5;
        c.earlyPredelayMs = 0.0f; c.reverbPredelayMs = 0.0f; c.roomScale = 1.0f;
        c.roomBassXoverHz = 250.0f; c.roomBassBoostDb = -2.0f; c.reverse = 0;
        c.earlyLevelDb = -8.0f; c.earlyFilter = { 0, 3000.0f, 3.0f, -96.0f };
        c.reverbLevelDb = -25.0f; c.reverbFilter = { 0, 3500.0f, 2.0f, -96.0f };
        return c;
    }()),
    studioRow("Theater Room", "Screening Room", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Room);
        c.sizeMeters = 20.0f; c.diffusion = 10.0f; c.reverbTime = 1.8965441f;
        c.bassRt = 1.25f; c.bassXoverHz = 700.0f; c.rtHiCutHz = 1700.0f; c.damping = 2;
        c.tapSlope = 0; c.spinHz = 0.0f; c.wanderMs = 19.0f; c.tailWidthDeg = 0.0f;
        c.pattern = 6;
        c.earlyPredelayMs = 0.0f; c.reverbPredelayMs = 5.0f; c.roomScale = 0.8f;
        c.roomBassXoverHz = 75.0f; c.roomBassBoostDb = 8.0f; c.reverse = 0;
        c.earlyLevelDb = -12.0f; c.earlyFilter = { 0, 4500.0f, 3.0f, -96.0f };
        c.reverbLevelDb = -16.0f; c.reverbFilter = { 2, 4750.0f, 3.0f, -7.5f };
        return c;
    }()),
    studioRow("Dark Drum Room", "Shadow Kit Room", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::Room);
        c.sizeMeters = 65.0f; c.diffusion = 72.0f; c.reverbTime = 0.32142687f;
        c.bassRt = 1.0f; c.bassXoverHz = 2325.0f; c.rtHiCutHz = 4225.0f; c.damping = 2;
        c.tapSlope = 0; c.spinHz = 0.2f; c.wanderMs = 7.0f; c.tailWidthDeg = 0.0f;
        c.pattern = 7;
        c.earlyPredelayMs = 0.0f; c.reverbPredelayMs = 100.0f; c.roomScale = 2.0f;
        c.roomBassXoverHz = 700.0f; c.roomBassBoostDb = 3.0f; c.reverse = 0;
        c.earlyLevelDb = -10.0f; c.earlyFilter = { 0, 1700.0f, 3.0f, -96.0f };
        c.reverbLevelDb = -16.5f; c.reverbFilter = { 0, 1850.0f, 2.0f, -96.0f };
        return c;
    }()),
    studioRow("Vintage Plate", "Brushed Steel", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::VintagePlate);
        c.sizeMeters = 28.0f; c.diffusion = 100.0f; c.reverbTime = 1.2474989f;
        c.bassRt = 1.2f; c.bassXoverHz = 10000.0f; c.rtHiCutHz = 12000.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 0.7f; c.wanderMs = 3.3f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 0.0f; c.shape = 16.0f; c.spread = 71.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 100.0f;
        c.earlyLevelDb = -3.5f; c.earlyFilter = { 0, 7500.0f, 1.0f, -96.0f };
        c.reverbLevelDb = -3.5f; c.reverbFilter = { 0, 7500.0f, 0.1f, -96.0f };
        c.reflections[0] = { 20.0f, 0, -5.0f };
        c.reflections[1] = { 30.0f, 0, -5.0f };
        c.echoes[0] = { 0.0f, 0, -96.0f, 0, -96.0f };
        c.echoes[1] = { 0.0f, 0, -96.0f, 0, -96.0f };
        c.echoes[2] = { 0.0f, 0, -96.0f, 0, -23.5f };
        c.echoes[3] = { 0.0f, 0, -96.0f, 0, -30.0f };
        return c;
    }()),
    studioRow("Vintage Bright Plate", "Blue Nickel", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::VintagePlate);
        c.sizeMeters = 19.5f; c.diffusion = 100.0f; c.reverbTime = 1.1704861f;
        c.bassRt = 0.8f; c.bassXoverHz = 10000.0f; c.rtHiCutHz = 20000.0f; c.damping = 1;
        c.tapSlope = -1; c.spinHz = 2.0f; c.wanderMs = 3.3f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 38.0f; c.shape = 14.0f; c.spread = 25.0f;
        c.reflectionTimePercent = 84.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 100.0f;
        c.earlyLevelDb = -2.0f; c.earlyFilter = { 0, 6250.0f, 1.0f, -96.0f };
        c.reverbLevelDb = -2.0f; c.reverbFilter = { 0, 6250.0f, 0.1f, -96.0f };
        c.reflections[0] = { 38.0f, 0, -32.0f };
        c.reflections[1] = { 43.0f, 0, -32.0f };
        c.echoes[0] = { 350.0f, 0, -96.0f, 0, -40.0f };
        c.echoes[1] = { 500.0f, 0, -96.0f, 0, -40.0f };
        c.echoes[2] = { 1.0f, 0, -96.0f, 0, -96.0f };
        c.echoes[3] = { 1.0f, 0, -96.0f, 0, -96.0f };
        return c;
    }()),
    studioRow("Short Vocal Plate", "Ribbon Vocal", [] {
        SectionReverbConfig c;
        c.model = static_cast<int>(SectionReverbModel::VintagePlate);
        c.sizeMeters = 16.0f; c.diffusion = 100.0f; c.reverbTime = 0.5985331f;
        c.bassRt = 0.6f; c.bassXoverHz = 300.0f; c.rtHiCutHz = 16000.0f; c.damping = 1;
        c.tapSlope = 0; c.spinHz = 0.0f; c.wanderMs = 3.3f; c.tailWidthDeg = 0.0f;
        c.predelayMs = 10.0f; c.shape = 0.0f; c.spread = 0.0f;
        c.reflectionTimePercent = 100.0f; c.echoTimePercent = 100.0f; c.echoFeedbackPercent = 10.0f;
        c.earlyLevelDb = 0.0f; c.earlyFilter = { 4, 900.0f, 1.0f, -96.0f };
        c.reverbLevelDb = 0.0f; c.reverbFilter = { 4, 2600.0f, 1.7f, -18.5f };
        c.reflections[0] = { 20.0f, 0, -96.0f };
        c.reflections[1] = { 30.0f, 0, -96.0f };
        c.echoes[0] = { 350.0f, 0, -96.0f, 0, -9.0f };
        c.echoes[1] = { 500.0f, 0, -96.0f, 0, -8.5f };
        c.echoes[2] = { 0.0f, 0, -96.0f, 1, -23.5f };
        c.echoes[3] = { 134.0f, 0, -96.0f, 1, -23.5f };
        return c;
    }()),
};
} // namespace duskverb
