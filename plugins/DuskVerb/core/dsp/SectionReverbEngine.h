// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// SectionReverbEngine shares the early stages, modulation and output filters across seven models.
// The modulated hall uses nested allpasses. The auditorium has a separate modulated tail
// and applies its input diffusion after the spread taps.
//
//   input -> input diffusers (two or three allpasses per side, scaled by Diffusion; at 0 pure
//            delays; the line models' echo feedback is added ahead of them)
//         -> the model's early stage:
//              Room:    bass shelf (one-pole low-pass at the room bass crossover, scaled and added
//                       back), early pattern (a few hundred taps in four groups) -> early output;
//                       the early output, read after the reverb pre-delay (the right side 439
//                       samples at 44.1 kHz later), feeds the sections; Reverse plays the
//                       pattern back to front
//              Chamber, Vintage Plate, Hall: a long line per side. Five spread taps per side, at
//                       Spread times their offsets plus the pre-delay and levelled by Shape,
//                       feed the sections (the chamber and hall's fifth goes straight to the reverb
//                       output); reflections and echoes read the line into the early output,
//                       the echoes' feedback goes back to the input
//         -> sections chained into one loop (two in the room, six in the others). Per section,
//            per sample:
//              cross = one-pole (RT HiCut) of its injection
//              band  = one-pole of (g x + delta lowpass_bassXOV(x)), the pole the RT HiCut when the
//                      section's threshold is within the damping mode, else open; g and delta
//                      inverted in the first section
//              ring <- direct input + cross + band, then a lattice allpass inside the ring
//              the next section reads this ring one section length back
//         -> output taps per section (the raw kernel output or the lattice ring) on four lanes
//            whose |sin| gains rotate at the Spin rate; a lane at zero moves its taps by up to
//            Wander -> tail width rotation, output filter and level; the early output its own
//            filter and level
//
// Processing runs in 32-sample blocks behind a 32-sample buffer, so the output is 32 samples
// late. A changed control ramps per block, and a moved delay layout fades in stages, so
// automation never steps. A change of model starts the engine afresh.
//
// Threading: prepare() allocates and runs off the audio thread. configure(), clear() and
// process() are audio-thread safe: no allocation, no locks. Deterministic: clear() reseeds the
// generator, so a render repeats exactly.

#pragma once

#include "AuditoriumTail.hpp"
#include "NestedSection.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace duskverb
{

enum class SectionReverbModel : int { Room = 0, Chamber = 1, VintagePlate = 2, Hall = 3, StudioPlate = 4, ModulatedHall = 5, Auditorium = 6 };
inline constexpr int kSectionReverbModels = 7;

// An output filter: type 0 one-pole low-pass, 1 one-pole high-pass, 2 low-pass, 3 high-pass,
// 4 band-pass, 5 notch (2-5 biquads); the shelf blends the unfiltered signal back in
// (-96 dB: none).
struct SectionReverbFilter
{
    int type = 0;
    float hz = 20000.0f;
    float octaves = 1.0f;
    float shelfDb = -96.0f;
};

// A line model's reflection: the line read after delayMs (times the reflection time master), at
// gainDb, inverted when `inverted` is 1.
struct SectionReverbReflection
{
    float delayMs = 20.0f;
    int inverted = 0;
    float gainDb = -96.0f;
};

// A line model's echo: as a reflection, plus a feedback path back to the input.
struct SectionReverbEcho
{
    float delayMs = 0.0f;
    int gainInverted = 0;
    float gainDb = -96.0f;
    int feedbackInverted = 1;
    float feedbackDb = -13.0f;
};

// The engine's controls. Out-of-range values are clamped and stepped controls rounded. Every
// field is 4 bytes, so a configuration compares byte for byte.
struct SectionReverbConfig
{
    int model = 0;                   // SectionReverbModel

    float diffusion = 100.0f;        // percent
    float sizeMeters = 58.0f;        // 2..80
    float reverbTime = 0.8042f;      // seconds at the model's table size; the loop gain is set from it
    float bassRt = 2.0f;
    float bassXoverHz = 225.0f;
    float rtHiCutHz = 20000.0f;
    int   damping = 2;               // 0 light, 1 normal, 2 heavy
    int   tapSlope = 1;              // -10..10
    float spinHz = 0.0f;
    float wanderMs = 19.0f;
    float tailWidthDeg = 0.0f;
    float earlyLevelDb = -16.0f;
    SectionReverbFilter earlyFilter { 0, 20000.0f, 3.0f, -96.0f };
    float reverbLevelDb = -14.0f;
    SectionReverbFilter reverbFilter { 0, 19000.0f, 3.0f, -96.0f };

    float definition = 38.0f, chorusRate = 4.0f, chorusDepth = 58.0f;
    int chorusType = 0;

    // Room
    int   pattern = 0;               // index into kSectionPatterns
    float earlyPredelayMs = 0.0f;
    float reverbPredelayMs = 0.0f;   // scaled by roomScale
    float roomScale = 1.1f;          // 0.5..2 in steps of 0.1
    float roomBassXoverHz = 500.0f;
    float roomBassBoostDb = 3.0f;
    int   reverse = 0;               // 1: the early pattern back to front

    // Chamber, Vintage Plate, Hall
    float predelayMs = 20.0f;
    float shape = 16.0f;             // 0..64
    float spread = 30.0f;            // percent
    float reflectionTimePercent = 100.0f;    // 0..200
    std::array<SectionReverbReflection, 4> reflections {{ { 20.0f, 0, -96.0f }, { 30.0f, 0, -96.0f },
                                                         { 0.0f, 0, -96.0f }, { 0.0f, 0, -96.0f } }};   // L-L, R-R, L-R, R-L
    float echoTimePercent = 100.0f;          // 0..200
    float echoFeedbackPercent = 100.0f;      // 0..200
    std::array<SectionReverbEcho, 4> echoes {{ { 0.0f, 0, -96.0f, 1, -13.0f }, { 17.0f, 0, -96.0f, 0, -13.0f },
                                               { 151.0f, 0, -96.0f, 1, -23.5f }, { 134.0f, 0, -96.0f, 1, -23.5f } }};   // L 1, R 1, L 2, R 2
};

namespace sectionreverb
{

class OutputFilter
{
public:
    void setRate (const Rate& rate) noexcept;
    void set (const SectionReverbFilter& settings) noexcept;
    void reset() noexcept;
    void beginBlock() noexcept;   // once per block, after set()
    void process (float* buffer, int channel) noexcept;

private:
    struct SampleRamp
    {
        float target = 1.0f, current = 1.0f, step = 0.0f;
        int samplesLeft = 0;
        bool pending = false;
        void snap (float v) noexcept { target = current = v; step = 0.0f; samplesLeft = 0; pending = false; }
        void arm (float t, float rateMs, const Rate& rate) noexcept;
        void fillBlock (float* out) noexcept;
        void fillExact (float* out) const noexcept;
        bool settled() const noexcept { return samplesLeft == 0 && ! pending; }
    };
    void armCoeffs (bool immediate, bool refill) noexcept;

    Rate rate_;
    int type_ = 0, activeType_ = 0, typeState_ = 0;
    float freqTarget_ = 1000.0f, freqCurrent_ = 1000.0f, bwTarget_ = 1.0f, bwCurrent_ = 1.0f;
    float maxBw_ = 1.0f, shelfDb_ = 0.0f;
    bool primed_ = false, typePending_ = false;
    Ramp gainRamp_, onePoleRamp_, shelfRamp_;
    SampleRamp biquad_[5];
    float onePole_ = 0.0f, shelf_ = 1.0f, typeGain_ = 1.0f;
    float lp_[2] {}, x1_[2] {}, x2_[2] {}, y1_[2] {}, y2_[2] {};
    float coefBlock_[5][32] {};
};

// A reflection or echo voice: a delayed read of the long line, its level and feedback ramped, and
// a moved delay faded out and back in.
struct LineVoice
{
    std::uint32_t line = 0, outCh = 0, delay = 0;
    float gain = 0.0f, fb = 0.0f;
    std::uint32_t appliedDelay = 0, requestedDelay = 0;
    float previousGain = 0.0f, previousFeedback = 0.0f;
    Ramp direct, feedback;
    bool retime = false, fading = false, levelDirty = false, feedbackDirty = false;

    void sync (bool snap) noexcept;
    void endBlock (const Rate& rate, std::uint32_t length) noexcept;
};

} // namespace sectionreverb

class SectionReverbEngine
{
public:
    static constexpr int kBlock = 32;
    static constexpr int kLatencySamples = kBlock;
    static constexpr int kMaxSections = 6;
    static constexpr int kMaxSectionTaps = 9;
    static constexpr int kMaxDiffusers = 3;
    static constexpr int kMaxPatternTaps = 368;
    static constexpr int kSpreadTaps = 5;

    void prepare (double sampleRate);
    // Takes every value at once. Before audio has run since the last clear(), or when the model
    // changes, the configuration is installed outright; otherwise each changed value ramps or
    // fades in.
    void configure (const SectionReverbConfig& config) noexcept;
    void clear() noexcept;
    // In place is allowed (in == out).
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    const SectionReverbConfig& config() const noexcept { return config_; }

    // The reverb time a model's table gives at a loop-gain step (0..400), and the loop gain for a
    // reverb time, both at the model's table size (the Size its reverb time is read at).
    static float rtSecondsForStep (int model, int step) noexcept;
    static float rtGain (int model, float seconds) noexcept;

private:
    using Rate = sectionreverb::Rate;
    using Ramp = sectionreverb::Ramp;
    using Voice = sectionreverb::LineVoice;

    struct Allpass
    {
        std::vector<float> buffer;
        std::uint32_t length = 0, write = 0, oldRead = 0, newRead = 0;
    };
    struct EarlyTap { unsigned source, output, delay; float time, gain; };
    enum class PatternState { Active, FadeOut, Drain, Load, Map, Position, Enable, FadeIn };
    enum class SizeChange { Idle, FadeToNew, Wait, Adopt, Return, FadeToOld };
    enum class SpreadChange { Idle, FadeOut, Move, FadeIn, Shape };

    struct Section
    {
        sectionreverb::NestedSection nested;
        std::vector<float> ring, rawRing;
        std::uint32_t length = 0, write = 0, morphA = 0, morphB = 0, apIdx = 0, apDelay = 0;
        std::uint32_t maxDelay = 0, morphOffset = 0;
        std::uint32_t tapRead[kMaxSectionTaps] {};
        Ramp morphRamp, gain1, gain2, bandPole;
        float morph[kBlock] {};
        float gainFull1 = -0.5f, gainFull2 = 0.5f;
        float crossPole = 1.0f, lp = 0.0f, cross = 0.0f, band = 0.0f;
        int fadeState = 0;
        bool delayPending = false, gainPending = false;
        float delayScale = 0.0f, delayTarget = 0.0f;
    };
    struct Mod
    {
        int mode = 0, countdown = 0, period = 0, index = 0, sub = 0;
        bool armed = false, rowFlag = false;
        std::uint32_t kick = 0;
        float lane[4][kBlock] {};
        float rate = 0.0f, rateJitter = 0.0f, step = 0.0f, phase = 0.0f;
        float cur[4] {}, tgt[4] {};
    };
    struct SpreadSide
    {
        std::uint32_t tap[kSpreadTaps] {};
        float bus[kSpreadTaps][kBlock] {};
    };

    static SectionReverbConfig sanitized (const SectionReverbConfig& config) noexcept;
    void reset() noexcept;
    void apply (const SectionReverbConfig& next) noexcept;
    void processBlock() noexcept;
    bool auditoriumModel() const noexcept { return config_.model == static_cast<int> (SectionReverbModel::Auditorium); }
    bool nestedModel() const noexcept { return config_.model == static_cast<int> (SectionReverbModel::ModulatedHall); }
    int spreadTapCount() const noexcept { return auditoriumModel() ? 4 : kSpreadTaps; }
    bool roomModel() const noexcept { return config_.model == static_cast<int> (SectionReverbModel::Room); }

    // Input diffusers.
    void setStage1ReadHeads (bool snap) noexcept;
    void advanceStage1Size() noexcept;
    void stage1Block(const float input[2][kBlock]) noexcept;

    // Room: early pattern and the tail feed.
    unsigned roomPredelaySamples (bool early) const noexcept;
    void loadPattern() noexcept;
    void retargetEarly (unsigned group, unsigned initialLag = 0) noexcept;
    void advanceEarlyGroups() noexcept;
    void updateFeedDelay() noexcept;
    void beginEarlyBlock() noexcept;
    void earlyBlock() noexcept;

    // Chamber and Vintage Plate: the long line, its spread taps, reflections and echoes.
    unsigned linePredelaySamples() const noexcept;
    void setShapeTargets() noexcept;
    void positionSpreadTap (unsigned k) noexcept;
    void advanceSpread() noexcept;
    void updateVoices (bool snap) noexcept;
    void lineBlock(const float input[2][kBlock]) noexcept;

    // Tail.
    float sizeScale() const noexcept { return config_.sizeMeters * 0.25f; }
    float wanderSamples() const noexcept { return spinOn_ ? config_.wanderMs * rate_.srMs : 0.0f; }
    const float* tailInput (int section, int slot) const noexcept;
    void resetTail() noexcept;
    void updateLanes (bool snap) noexcept;
    void configureDamping (bool snap, bool updateCross) noexcept;
    void setTapSlope (float value) noexcept;
    void requestSize() noexcept;
    void triggerDelay (Section& x, int s) noexcept;
    void fadeBlock (Section& x, int s) noexcept;
    void swapMorph (Section& x) noexcept;
    void retargetGroup (int group, float scale, float wander) noexcept;
    int nextRandom() noexcept;
    float rand01() noexcept;
    void sineGen (Mod& mod, float degrees) noexcept;
    static void rampLanes (Mod& mod) noexcept;
    int seq1 (Mod& mod) noexcept;
    int seq2 (Mod& mod) noexcept;
    int seq3 (Mod& mod) noexcept;
    void seqFallback (Mod& mod) noexcept;
    int advanceSequencer (Mod& mod) noexcept;
    void sequencerBlock() noexcept;
    void tailBlock() noexcept;

    // Output.
    void armMix() noexcept;
    void mixBlock() noexcept;

    Rate rate_;
    sectionreverb::AuditoriumTail auditorium_;
    float previousWetPeak_ = 0.0f;
    SectionReverbConfig config_ {};
    bool ranSinceClear_ = false;
    bool prepared_ = false;

    float inBus_[2][kBlock] {}, outBus_[2][kBlock] {}, diffused_[2][kBlock] {};
    float early_[2][kBlock] {}, echoFeedback_[2][kBlock] {}, feed_[2][kBlock] {}, lane_[2][kBlock] {};
    int acc_ = 0;

    // Input diffusers.
    Allpass stage1_[2][kMaxDiffusers];
    Ramp diffusion_, stage1SizeBlend_;
    SizeChange stage1SizeState_ = SizeChange::Idle;
    bool stage1SizePending_ = false;

    // Room early pattern.
    std::array<std::vector<float>, 2> inputLine_, feedLine_;
    unsigned inputPos_ = 0, feedPos_ = 0, inputLength_ = 0, feedLength_ = 0;
    unsigned activeTaps_ = 0, patternLength_ = 0;
    float nominalLength_ = 0.0f;
    float groupGain_[4] {};
    unsigned phaseBlocks_ = 0, phasePeriod_ = 0;
    int sweepStep_ = -1;
    float sweepPhase_ = 0.0f;
    std::array<EarlyTap, kMaxPatternTaps> taps_ {};
    unsigned feedDelay_[2] {}, oldFeedDelay_[2] {};
    float low_[2] {};
    Ramp bassPole_, bassGain_, patternInputGain_, feedFade_;
    PatternState patternState_ = PatternState::Active;
    unsigned drainRemaining_ = 0;
    bool selectPending_ = false, tapsDirty_ = false, feedDirty_ = false;

    // Long line (Chamber, Vintage Plate).
    std::array<std::vector<float>, 2> lines_;
    std::uint32_t lineLength_ = 0, linePhase_ = 0;
    SpreadSide spread_[2];
    Ramp spreadGain_[kSpreadTaps];
    float shapeTarget_[kSpreadTaps] {};
    SpreadChange spreadState_ = SpreadChange::Idle;
    unsigned movingTap_ = 0;
    bool spreadDirty_ = false, shapeDirty_ = false;
    Voice reflections_[4], echoes_[4];

    // Tail.
    std::array<Section, kMaxSections> sections_ {};
    Mod mod_, secondary_;
    bool spinOn_ = false;
    std::uint32_t rng_ = 1u;
    Ramp gLane_, deltaLane_, xovLane_;
    float rtG_ = 0.0f, rtDelta_ = 0.0f;
    float readSizeScale_ = 0.0f;
    float tapWeights_[kMaxSectionTaps] {};
    float storedTapSlope_ = 0.0f;
    bool reapplyTapSlope_ = false;

    // Output.
    sectionreverb::OutputFilter earlyFilter_, reverbFilter_;
    Ramp earlyRamp_, reverbRamp_, mixSRamp_, twCRamp_, twSRamp_;
};

} // namespace duskverb
