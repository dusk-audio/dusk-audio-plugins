// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "DuskAccessBridge.hpp"
namespace duskaudio::chord { class ChordAnalyzerRuntime; }
// Non-audio operations only. The runtime serializes view/record/history access;
// the audio callback never takes its model mutex. Guard this weak bridge in UI.
DUSK_ACCESS_DECL(duskaudio::chord::ChordAnalyzerRuntime*, chordAnalyzerGetRuntime);
