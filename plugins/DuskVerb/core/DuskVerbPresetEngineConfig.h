// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Entry points into DuskVerbPresetEngineConfig.cpp that live outside
// FactoryPreset itself. Both builds (JUCE and DAF) link that one translation
// unit; see the .cpp header comment.

#pragma once

namespace duskverb
{

// Reads the DUSKVERB_* offline-sweep environment variables exactly once and
// caches the pointers. MUST be called on the message thread before audio
// starts (std::getenv is not real-time safe); FactoryPreset::applyEngineConfig
// then only loads the cached pointers from the audio thread.
void primeTuningEnvCache() noexcept;

// Per-preset PostTankEQ band centre frequencies and Qs. The four band GAINS are
// ordinary parameters; freq + Q come from the name-keyed table (or the
// DUSKVERB_PTEQ sweep override) and must be cached at preset-swap time so the
// gain edge-detect can re-issue setPostTankEQBand() with the same centres.
// `name` may be "" — that yields the default centres.
void resolvePteqFreqQ (const char* name, float fOut[4], float qOut[4]) noexcept;

} // namespace duskverb
