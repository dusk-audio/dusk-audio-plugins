#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <map>
#include "dsp/DspUtils.h"

// Forward declaration — applyEngineConfig() needs to call SixAPTank-specific
// setters on DuskVerbEngine without dragging the engine header into every
// include of FactoryPresets.h.
class DuskVerbEngine;

// Factory presets — 16 hardware-anchored voicings.
// `algorithm` is the engine index (0..9) per AlgorithmConfig.h getAlgorithmConfig():
//   0 = Plate (Dattorro)
//   1 = Plate (Dattorro Vintage)
//   2 = High Density (6-AP / SixAPTank)
//   3 = Quad Room (QuadTank)
//   4 = Realistic Space (FDN)
//   5 = Spring Tank (6G15)
//   6 = Non-Linear (RMX16)
//   7 = Shimmer (Eno FDN)
//   8 = Vintage Tank (Figure-8)
//   9 = Reverse Room (Lexicon)
//
// IMPORTANT: presets are grouped CONTIGUOUSLY by category — the editor's
// dropdown adds a section heading whenever the category changes, so any
// re-ordering must keep the per-category blocks intact or duplicate
// "Plates" / "Halls" / "Rooms" headers will reappear.
struct FactoryPreset
{
    const char* name;
    const char* category;

    int   algorithm;
    float mix;
    bool  busMode;
    float predelay;
    int   predelaySync;     // 0 = Free, 1..6 = 1/32 .. 1/1
    float decay;
    float size;
    float modDepth;
    float modRate;
    float damping;
    float bassMult;
    float crossover;
    float diffusion;
    float erLevel;
    float erSize;
    float loCut;
    float hiCut;
    float width;
    bool  freeze;
    float gainTrim;
    // Trailing fields use defaults so the existing 16 brace-init literals stay
    // valid — older preset rows that don't include the new fields will pick
    // up the safe defaults below (mono_below=20=bypass, mid=1.0=natural,
    // high_xover=4000Hz=neutral, saturation=0=clean).
    float monoBelow      = 20.0f;
    float midMult        = 1.0f;     // 3-band mid multiplier (1.0 = natural)
    float highCrossover  = 4000.0f;  // mid↔high split (Hz)
    float saturation     = 0.0f;     // 0..1 drive softClip
    // Phase 1 post-tank high-shelf attenuation depth (dB, [-24, 0]).
    // Placed here (after saturation, before gateEnabled) so SHORT-form
    // preset rows can append it as a single trailing brace-init value
    // without writing out the sixAP + DPV intermediates. Default -12 dB
    // is the universal shelf depth that produced the net-zero gate delta
    // in Phase 1 audit; per-preset overrides via brace-init.
    float hiCutShelfGainDb = -12.0f;

    // Phase 2 modulation topology selector — NOT a struct field. Per-preset
    // mapping lives in PluginProcessor.cpp::FactoryPreset::applyEngineConfig
    // via a static name → topology map. Avoids breaking the FactoryPreset
    // aggregate-initializability that the brace-init preset list relies on.
    bool  gateEnabled    = true;     // NonLinear engine: true = gate active.
                                     // No-op on other engines but written
                                     // anyway so loading a preset always
                                     // sets the toggle to a known state.

    // SixAPTank-specific engine tunables. Defaults match the engine's
    // historical hardcoded constants — so any preset that doesn't override
    // these gets identical sound to before. Black Hole opts in to brighter,
    // denser values for external reference blackhole-character late-tail content.
    float sixAPDensityBaseline = 0.62f;
    float sixAPBloomCeiling    = 0.85f;
    float sixAPBloomStagger[6] = { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f };
    float sixAPEarlyMix        = 0.5f;
    float sixAPOutputTrim      = 1.3f;

    // In-loop bass-choke HPF cutoff (Hz). Only the legacy HighDensityPlate
    // engine used this; current engines ignore it. 20 Hz = effective
    // bypass. Placed last so existing brace-init preset rows don't need
    // to grow trailing arguments — every row picks up the default.
    float bassChoke            = 20.0f;

    // DattorroPlateVintage (algo 1) per-preset brightness + corrective EQ
    // controls. The only algo-1 factory preset (Vintage Vocal Plate)
    // brace-inits its own DPV values, so these struct defaults are
    // placeholders for any future algo-1 preset only.
    // Non-algo-1 presets ignore these (engine glue forwards to a no-op).
    float dpvHfShelfGainDb     = 22.0f;
    float dpvHfShelfFreqHz     = 6000.0f;
    float dpvStructHfDampHz    = 8000.0f;
    float dpvBoxCutGainDb      = -3.5f;
    float dpvBoxCutFreqHz      = 320.0f;
    float dpvBassShelfGainDb   = 0.0f;
    float dpvBassShelfFreqHz   = 180.0f;

    // FiveBandDamping (Phase 2, FDN/algo-4 only). Sentinel defaults of -1 for
    // the multipliers mean "inherit" (sub → bassMult, hi-mid → Treble/damping)
    // so every existing brace-init preset row — none of which reach these
    // trailing fields — stays bit-for-bit transparent (collapses to the legacy
    // 3-band). crossoverSub/Air default to boundaries that sit between
    // equal-gain bands at the inherited mults. Non-FDN engines ignore all four.
    float subMult      = -1.0f;   // <0 → inherit bassMult (transparent)
    float hiMidMult    = -1.0f;   // <0 → inherit Treble Multiply (transparent)
    float crossoverSub = 120.0f;  // sub ↔ low-mid boundary (Hz)
    float crossoverAir = 8000.0f; // hi-mid ↔ air boundary (Hz)

    // Block 2 feed-forward input energy makeup (dB). 0 = bypass; trailing
    // fields → existing brace-init rows default to bypass, zero edits.
    float inputSubGain = 0.0f;
    float inputMidGain = 0.0f;

    // Phase θ (2026-06-01): post-loop Tail Spin/Wander (FDN / ReverseRoom only).
    // Trailing fields → existing brace-init rows omit them → default depth 0 =
    // bit-exact bypass, every legacy preset unaffected.
    float tailSpinDepth = 0.0f;   // 0..1 modulation depth
    float tailSpinRate  = 1.0f;   // Hz, base spin rate

    // Phase γ (2026-05-29): per-preset post-tank band-trim region gains —
    // NOT struct fields (would break aggregate-init of every existing
    // brace-init preset row). Per-preset overrides live in PluginProcessor.cpp::
    // FactoryPreset::applyEngineConfig via a static name → 4-float-region
    // map (kPostBandTrimByName). Presets not in the map get an all-zero-
    // dB trim → bit-identical bypass.

    // Post-tank parametric EQ (4 bands × {freqHz, Q, gainDb}) — NOT a struct
    // field for the same reason as Phase 2 modulation topology: adding 12
    // array entries to FactoryPreset would break the aggregate-init of every
    // existing brace-init preset row that doesn't reach into DPV territory.
    // Per-preset overrides live in PluginProcessor.cpp::FactoryPreset::
    // applyEngineConfig via a static name → bands map (kPostTankEQByName).
    // Presets not in the map get an all-zero-gain EQ → bit-identical bypass.

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        auto setIfExists = [&apvts] (const juce::String& id, float v) {
            if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        if (auto* p = apvts.getParameter ("algorithm"))
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (algorithm)));
        if (auto* p = apvts.getParameter ("predelay_sync"))
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (predelaySync)));
        if (auto* p = apvts.getParameter ("bus_mode"))     p->setValueNotifyingHost (busMode     ? 1.0f : 0.0f);
        if (auto* p = apvts.getParameter ("freeze"))       p->setValueNotifyingHost (freeze      ? 1.0f : 0.0f);
        if (auto* p = apvts.getParameter ("gate_enabled")) p->setValueNotifyingHost (gateEnabled ? 1.0f : 0.0f);
        setIfExists ("mix",       mix);
        setIfExists ("predelay",  predelay);
        setIfExists ("decay",     decay);
        setIfExists ("size",      size);
        setIfExists ("mod_depth", modDepth);
        setIfExists ("mod_rate",  modRate);
        // Tail Spin/Wander — per-preset via name→map (struct fields are trailing,
        // so positional rows can't set them; same trap as kFiveBandByName).
        // Default depth 0 = bit-exact bypass everywhere not listed here.
        struct TailSpinOverride { float depth, rate; };
        static const std::map<juce::String, TailSpinOverride> kTailSpinByName = {
            // (Vocal Hall override REMOVED 2026-06-01: the depth-0.537 tail-spin
            //  scored 13 by satisfying the old envelope-AM rate gate, but it was
            //  a +-54% tremolo pump — audibly unusable in stereo. Reverted to
            //  depth 0 (smooth native delay-chorus); re-tuned via Mod Depth/Rate
            //  against the new pitch-chorus gate. Tail-spin stays dormant infra.)
        };
        float tsDepth = tailSpinDepth, tsRate = tailSpinRate;
        if (auto it = kTailSpinByName.find (juce::String (name)); it != kTailSpinByName.end())
        { tsDepth = it->second.depth; tsRate = it->second.rate; }
        setIfExists ("tail_spin_depth", tsDepth);
        setIfExists ("tail_spin_rate",  tsRate);
        // Phase 4 (option 2): early-field ER boost. Per-preset via name→map;
        // 1.0 (unlisted) → ×1.0 exact → bit-identical. >1 lets the parallel ER
        // own the 0-26 ms attack the FDN tank structurally can't supply.
        struct ERBoostOverride { float boost; };
        static const std::map<juce::String, ERBoostOverride> kERBoostByName = {
            // Vocal Hall: front-load campaign (2026-06-08). er_boost 7.07→3.0
            // paired with the tank-rebalance (tank_level 0.42) — the energy
            // front-load now comes from the tank/ER BALANCE, not raw ER boost.
            { "Vocal Hall", { 3.0f } },
        };
        float erBoost = 1.0f;
        if (auto it = kERBoostByName.find (juce::String (name)); it != kERBoostByName.end())
            erBoost = it->second.boost;
        setIfExists ("er_boost", erBoost);
        // Rising-onset ER peak time (ms). 0 (unlisted) → legacy first-tap
        // rolloff → bit-identical. Paired with er_boost to set the early field.
        struct ERRiseOverride { float ms; };
        static const std::map<juce::String, ERRiseOverride> kERRiseByName = {
            // Vocal Hall: 31.6→15 (front-load campaign) — closes attack_time +
            // onset_slope with the rebalanced early field.
            { "Vocal Hall", { 15.0f } },
        };
        float erRise = 0.0f;
        if (auto it = kERRiseByName.find (juce::String (name)); it != kERRiseByName.end())
            erRise = it->second.ms;
        setIfExists ("er_rise", erRise);

        // ── Energy-arrival front-load campaign (2026-06-08) ─────────────────
        // Per-preset front-load + stereo-image levers. Unlisted presets get the
        // bit-identical defaults (tank 1.0, decorr 0, shelves 0, neutral off,
        // split 0). Vocal Hall: tank-rebalance (0.42) front-loads energy to
        // match VVV (t50/first50 gates pass); er_decorr 0.6 + the ER-bus shelves
        // restore a VVV-like uniform stereo image (kills the anti-phase low the
        // tank cut would otherwise expose); er_level 0.79 sets the early deck.
        // See memory duskverb_energy_arrival_gate_and_wall.
        // tank_level, er_bus shelves, er_decorr have NO row field → set here.
        // er_level + mono_below ARE row fields (0.79 / 20 for VH) → set by the row.
        struct FrontLoadOverride { float tankLevel, erBusLow, erBusHigh, erDecorr, splitHz; };
        static const std::map<juce::String, FrontLoadOverride> kFrontLoadByName = {
            // VH split 300: the tank cut starved the LATE lows (boom/body 8
            // gates quiet) — below 300 Hz the tank stays unity, mid/high keep
            // the 0.42 front-load (attack/t50/first50 unaffected).
            // VH tank 0.42->0.50 (2026-06-10) paired with the softened pteq
            // 70 Hz cut: refills the boom/body late lows the deeper front-load
            // starved, 20 -> 16. split_hz probed (300/250: lows-exempt tank cut
            // over-corrects +7.5 dB and flips the tail dark via gain renorm —
            // 26/22, reverted to broadband 0).
            { "Vocal Hall", { 0.50f, 5.0f, 2.6f, 0.60f, 0.0f } },
            { "Cathedral Large Hall", { 1.0f, 0.0f, 0.0f, 0.60f, 0.0f } },
            { "Bright Hall", { 1.0f, 0.0f, 5.0f, 0.50f, 0.0f } },
        };
        if (auto it = kFrontLoadByName.find (juce::String (name)); it != kFrontLoadByName.end())
        {
            setIfExists ("tank_level",       it->second.tankLevel);
            setIfExists ("er_bus_low_gain",  it->second.erBusLow);
            setIfExists ("er_bus_high_gain", it->second.erBusHigh);
            setIfExists ("er_decorr",        it->second.erDecorr);
            setIfExists ("tank_split_hz",    it->second.splitHz);
        }
        else
        {
            // Reset to neutral so these don't latch across preset loads (only
            // the mapped presets set them; the other name-keyed maps below
            // already default-reset the same way). tank_level neutral is 1.0
            // (×1.0 bypass) — NOT 0.0, which would mute the late tank.
            setIfExists ("tank_level",       1.0f);
            setIfExists ("er_bus_low_gain",  0.0f);
            setIfExists ("er_bus_high_gain", 0.0f);
            setIfExists ("er_decorr",        0.0f);
            setIfExists ("tank_split_hz",    0.0f);
        }
        // Companion front-load lever — every preset ships this neutral, so
        // write it unconditionally on each load; otherwise a user tweak
        // latches across preset changes.
        setIfExists ("er_stereo_neutral", 0.0f);
        // Phase 4 (Change 2): HF cross-talk decorrelation depth. 0 (unlisted) →
        // no cross-feed → bit-identical. Per-preset from the cross-talk sweep.
        struct XTalkOverride { float depth; };
        static const std::map<juce::String, XTalkOverride> kXTalkByName = {
            // Calibrated by the cross-talk width sweep (2026-06-02).
            { "Vocal Hall", { 0.0041f } },
        };
        float xtalk = 0.0f;
        if (auto it = kXTalkByName.find (juce::String (name)); it != kXTalkByName.end())
            xtalk = it->second.depth;
        setIfExists ("xtalk", xtalk);
        // Phase 5 multiband: enable + per-band decays, per-preset. Default off /
        // 0 → single legacy tank → bit-identical. Populated post-sweep.
        struct MultibandOverride { bool enable; float lowSec, midSec, highSec; };
        static const std::map<juce::String, MultibandOverride> kMultibandByName = {
            // Calibrated by the per-band decay sweep (2026-06-02).
        };
        bool  mbEnable = false; float mbLo = 0.0f, mbMi = 0.0f, mbHi = 0.0f;
        if (auto it = kMultibandByName.find (juce::String (name)); it != kMultibandByName.end())
        { mbEnable = it->second.enable; mbLo = it->second.lowSec; mbMi = it->second.midSec; mbHi = it->second.highSec; }
        if (auto* p = apvts.getParameter ("mb_enable")) p->setValueNotifyingHost (mbEnable ? 1.0f : 0.0f);
        setIfExists ("mb_low_decay",  mbLo);
        setIfExists ("mb_mid_decay",  mbMi);
        setIfExists ("mb_high_decay", mbHi);
        // Per-band early-decay (edt) shaper — the dormant PerBandEDTShape, now
        // energy-conserving (boosts the band tail while cutting sustain so edt
        // lengthens with no spec cost). +attack = hold (longer edt), - = shorter.
        // All 0 (unlisted) → AttackRamp returns 1.0 → bit-identical bypass.
        struct EDTOverride { float subA, subT, lmA, lmT, mhA, mhT, airA, airT; };
        static const std::map<juce::String, EDTOverride> kEDTByName = {
            // Vocal Hall EDT override REMOVED 2026-06-04: the AttackRamp shaper
            // applies an envelope-relative per-sample gain. On DYNAMIC material
            // its envelope follower tracks the program's beat/transient envelope
            // (e.g. a 100 Hz twin-tone beat) → the gain amplitude-modulates at the
            // program rate → intermodulation distortion (twin-tone IMD 10.1% vs
            // ~5% with the shaper off, audibly "major distortion" on real vocals).
            // The 3 ms gain slew (DspUtils AttackRamp) tamed the steady single-tone
            // 2 kHz ripple but CANNOT reject program-rate beats without destroying
            // the EDT function. So the shaper is fundamentally unfit for dynamic
            // material; removed here. Cost: edt low/low_mid gates reopen (these
            // were gamed by the distortion). Ear-is-arbiter — clean beats gated.
        };
        float esA=0,esT=120,elA=0,elT=120,emA=0,emT=120,eaA=0,eaT=120;
        if (auto it = kEDTByName.find (juce::String (name)); it != kEDTByName.end())
        { const auto& e=it->second; esA=e.subA;esT=e.subT;elA=e.lmA;elT=e.lmT;emA=e.mhA;emT=e.mhT;eaA=e.airA;eaT=e.airT; }
        setIfExists ("edt_sub_attack_db", esA);     setIfExists ("edt_sub_tau_ms", esT);
        setIfExists ("edt_lowmid_attack_db", elA);  setIfExists ("edt_lowmid_tau_ms", elT);
        setIfExists ("edt_midhi_attack_db", emA);   setIfExists ("edt_midhi_tau_ms", emT);
        setIfExists ("edt_air_attack_db", eaA);     setIfExists ("edt_air_tau_ms", eaT);
        // QuadTank 5-band damping split (hi-mid 4-8k / air >8k). -1 (unlisted)
        // → inherit the legacy treble rate → bit-identical 3-band. Lets QuadTank
        // presets shorten the 8 k / 16 k tails independently of the centroid.
        struct QuadBandOverride { float hiMid, air; };
        static const std::map<juce::String, QuadBandOverride> kQuadBandByName = {
            // 79 Vocal Chamber (QuadTank) vs VVV — hi-mid+air split closes
            // cent_500 and pulls the 4-8 k tail in without darkening the mids
            // (the 3-band gHigh couldn't). 23->21.
            { "79 Vocal Chamber", { 0.18f, 0.5f } },
        };
        float qtHiMid = -1.0f, qtAir = -1.0f;
        if (auto it = kQuadBandByName.find (juce::String (name)); it != kQuadBandByName.end())
        { qtHiMid = it->second.hiMid; qtAir = it->second.air; }
        setIfExists ("qt_himid_mult", qtHiMid);
        setIfExists ("qt_air_mult",   qtAir);
        setIfExists ("damping",   damping);
        setIfExists ("bass_mult", bassMult);
        setIfExists ("mid_mult",  midMult);
        setIfExists ("crossover", crossover);
        setIfExists ("high_crossover", highCrossover);
        // FiveBandDamping. Base = sentinel-inherit (transparent: sub→bass,
        // hi-mid→treble). Per-preset NON-transparent values live in a
        // name→map — same pattern as kPostTankEQByName — so the fragile
        // aggregate-init rows (these struct fields sit last) stay untouched.
        struct FiveBandOverride { float sub, hiMid, xSub, xAir, inSub, inMid, inHigh, inLoopDb; };
        static const std::map<juce::String, FiveBandOverride> kFiveBandByName = {
            // Drum Plate (FDN) — direct-scoreboard + warm-start sweep vs VVV
            // anchor, 27→23. FiveBand mults + feed-forward Input Sub +2.02 dB:
            // restores the low-end body that read "weak" by ear (closes
            // ss-deep-sub / ss-sub). 1 kHz notch + mild sub-hot remain (FDN
            // steady-state limit — see commits 4876359/91da13e for the in-loop
            // peak fix that proved it can't be filled within stability).
            { "Drum Plate", { 0.5349f, 0.8907f, 67.45f, 15219.49f, 2.02f, 0.0f, 0.0f, 0.0f } },
            // Tiled Room (FDN) — scoreboard+warm-start vs VVV "Tiled Room", 47→28.
            { "Tiled Room", { 1.661f, 0.8853f, 43.26f, 10850.0f, 0.0346f, -1.87f, 0.0f, 0.0f } },
            { "Blade Runner 224", { 1.8467f, 0.2189f, 119.28f, 14310.79f, 1.6074f, 3.4473f, 0.0f, 0.1912f } },
            // Cathedral (AccurateHall since 2026-06-09): in-loop peak 1.4 -> 0.
            // Under the octave GEQ, in-loop gain at 1 kHz distorts that band's
            // accurate-RT decay (the calibrator oscillated +228/-56% at 1k).
            // Input makeup (pre-loop, level-only) kept.
            { "Cathedral Large Hall", { 1.827f, 0.8574f, 104.8f, 8400.0f, 2.657f, 2.079f, 0.0f, 0.0f } },
            // Vocal Hall (FDN) — 2026-06-07 co-tune. Sub 1.615 (lengthen T60 63),
            // Hi-Mid 0.577 (shorten T60 8k + decay-hi). xSub 120 / xAir 8000 as
            // shipped. No input makeup / in-loop peak. Pairs w/ row Treble 1.091
            // + pteq level comp to decouple level from the decay retune. 17->10.
            { "Vocal Hall", { 1.615f, 0.577f, 120.0f, 8000.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
            // Vocal Plate (FDN) — T60 decay tune 2026-06-08. PINS Hi-Mid 0.85 so the air
            // band (4k/8k) does NOT inherit the row Treble (0.60): without this entry the
            // sentinel sets fbHiMid=damping=0.60 → double HF damping → 4k/8k T60 -10%. Sub
            // 0.74 = the prior bass-inherit value (unchanged). xSub/xAir as shipped.
            { "Vocal Plate", { 0.74f, 0.85f, 120.0f, 8000.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
        };
        float fbSub   = subMult   >= 0.0f ? subMult   : bassMult;
        float fbHiMid = hiMidMult >= 0.0f ? hiMidMult : damping;
        float fbXSub  = crossoverSub;
        float fbXAir  = crossoverAir;
        float fbInSub = inputSubGain;
        float fbInMid = inputMidGain;
        float fbInHigh = 0.0f;
        float fbInLoopDb = 0.0f;
        if (auto it = kFiveBandByName.find (juce::String (name)); it != kFiveBandByName.end())
        {
            fbSub  = it->second.sub;   fbHiMid = it->second.hiMid;
            fbXSub = it->second.xSub;  fbXAir  = it->second.xAir;
            fbInSub = it->second.inSub; fbInMid = it->second.inMid;
            fbInHigh = it->second.inHigh;
            fbInLoopDb = it->second.inLoopDb;
        }
        setIfExists ("sub_mult",      fbSub);
        setIfExists ("hi_mid_mult",   fbHiMid);
        setIfExists ("crossover_sub", fbXSub);
        setIfExists ("crossover_air", fbXAir);
        // Low-Band Transient Shaper — Phase A: every preset bypasses (depth 0).
        // Phase B will set per-preset depths once the detector is wired.
        setIfExists ("transient_shaper", 0.0f);
        setIfExists ("shaper_time",      120.0f);
        setIfExists ("shaper_xover",     250.0f);
        setIfExists ("shaper_sens",      1.5f);
        // Block 2 input makeup — per-preset via kFiveBandByName (0 dB elsewhere).
        setIfExists ("input_sub_gain",   fbInSub);
        setIfExists ("input_mid_gain",   fbInMid);
        setIfExists ("input_high_gain",  fbInHigh);
        setIfExists ("saturation", saturation);
        setIfExists ("diffusion", diffusion);
        setIfExists ("er_level",  erLevel);
        setIfExists ("er_size",   erSize);
        setIfExists ("lo_cut",    loCut);
        setIfExists ("hi_cut",    hiCut);
        setIfExists ("width",     width);
        setIfExists ("gain_trim", gainTrim);
        setIfExists ("mono_below", monoBelow);
        // Partial mono-below: 1.0 = full mono (legacy). Vocal Hall uses 0.45 so
        // the lows match VVV's gentle decorrelation instead of full-mono (which
        // over-correlated broadband stereo_corr). Others stay full-mono.
        setIfExists ("mono_below_depth", juce::String (name) == "Vocal Hall" ? 0.45f : 1.0f);
        // DPV corrective EQ + brightness — only audible when algorithm=1
        // routes through DattorroPlateVintage. Other engines forward to
        // no-op setters; safe to set unconditionally.
        setIfExists ("dpv_hf_shelf_db",        dpvHfShelfGainDb);
        setIfExists ("dpv_hf_shelf_hz",        dpvHfShelfFreqHz);
        setIfExists ("dpv_struct_hf_damp_hz",  dpvStructHfDampHz);
        setIfExists ("dpv_box_cut_db",         dpvBoxCutGainDb);
        setIfExists ("dpv_box_cut_hz",         dpvBoxCutFreqHz);
        setIfExists ("dpv_bass_shelf_db",      dpvBassShelfGainDb);
        setIfExists ("dpv_bass_shelf_hz",      dpvBassShelfFreqHz);
        setIfExists ("hi_cut_shelf_db",        hiCutShelfGainDb);
        // Phase γ post-tank per-band trim — per-preset overrides live in
        // PluginProcessor.cpp::kPostBandTrimByName and are applied via
        // engine.setPostTankBandTrimGainDb from applyEngineConfig. Presets
        // not in that map keep the APVTS default (0 dB) → bit-identical
        // bypass on every region.

        // Phase ζ + η per-preset opt-ins disabled — debug pass identified
        // the root cause: ζ pre-Hadamard peaking at +10.88 dB DOES amplify
        // the impulse response massively (+94 dB at the 1k band, verified
        // 2026-05-29) but is partially capped by softClip on sustained
        // signals like sine1k. Sweep loss measurement included transient-
        // dependent gates that were optimistic about steady-state benefit.
        // Applied as-is, the impulse explosion broke 21+ transient gates
        // (snare RMS / spec_L1 / EDT / boom / etc.) for a +2 dB sine1k
        // gain. Net regression. ζ+η infrastructure remains live for future
        // presets where the impulse/sustained trade-off is favorable.
        //
        // Defensively write APVTS to engine-bypass defaults on every
        // preset apply so a prior preset's user-overridden values don't
        // carry through to the next preset swap.
        // In-loop peak: per-preset gain via kFiveBandByName (1 kHz / Q 2.5 when
        // engaged; engine hard-caps +2.0 dB for stability). 0 dB elsewhere.
        setIfExists ("in_loop_peak_hz",    1000.0f);
        setIfExists ("in_loop_peak_q",     fbInLoopDb > 0.0f ? 2.5f : 2.0f);
        setIfExists ("in_loop_peak_db",    fbInLoopDb);
        setIfExists ("bass_shelf_fast_fc",  400.0f);
        setIfExists ("bass_shelf_slow_fc",  200.0f);
        setIfExists ("bass_shelf_fast_db",    0.0f);
        setIfExists ("bass_shelf_slow_db",    0.0f);
        setIfExists ("bass_shelf_transition_ms", 100.0f);
    }

    // Apply engine-specific (non-APVTS) tunables. Currently only the
    // SixAPTank brightness/density fields. Defined out-of-line in
    // PluginProcessor.cpp where DuskVerbEngine's full type is visible.
    void applyEngineConfig (DuskVerbEngine& engine) const;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        // ── Vocal Plate (VVV anchor) ───────────────────────────────────────
        // Engine: FDN. Anchor: Valhalla Vintage Verb "Vocal Plate" preset
        // (Reverb Mode = Plate) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Plates,
        // has_dpv=False (FDN engine, DPV params stripped). 1300 trials.
        //   Stage 1 loss 4.50 (short plate envelope is noisier vs halls)
        //   Stage 2 loss 140.99 (per-band decay tight on plate)
        //   Stage 3 loss 2.13 (cleanest Stage 3 polish to date)
        //
        // Locked 2026-05-30: direct-scoreboard + warm-start FDN sweep,
        // 34 → 24 gate fails vs VVV "Vocal Plate" anchor (verified clean
        // render). Two sweeps (cold→24, warm+fresh-RNG→can't beat 24)
        // converge. Residual 24 is structural — HF T60 overshoot the single
        // trebleMult can't bend (16k wants 0.40s, FDN gives 0.59s) + a sub
        // energy/decay deficit — not closable by uniform FDN damping.
        { "Vocal Plate",          "Plates",
          10, 0.35f, false, 29.16f, 0,   // algo 10 = AccurateHall (2026-06-09): per-octave GEQ T60. T60 6/9->9/9, gain-matched full_check 25->14 vs FDN. Octave targets in kAccurateHallT60ByName.
          0.90f, 0.15f, 0.37f, 1.51f, 0.60f, 0.74f,  401.0f,  // T60 decay tune 2026-06-08 (gain-matched): Decay 1.02->0.90 + Treble 0.85->0.60 closes T60 6/9 (63/250/500/2k/4k/8k match VVV, tail_t60 within 3%). Hi-Mid PINNED 0.85 via kFiveBandByName so the air band isn't double-damped. Residual 125/1k/16k = single-octave anomalies (9-octave-vs-5-band wall).
          0.58f, 0.25f, 0.76f,  30.0f, 16667.0f, 1.02f, false, -1.00f,  // GainTrim -1.74->-1.0 (re-gain-matched to anchor noiseburst after the decay cut). Lo Cut 30: restores 20-100Hz low-end.
          /* mono */ 20.0f, /* mid */ 0.72f, /* highX */ 3817.0f, /* sat */ 0.03f },
        // ═══════════ PLATES ═══════════
        // ── Vintage Vocal Plate ──────────────────────────────────────────────
        // Anchor: vintage rack-style algorithmic reverb "VintagePlate / Vocal Plate"
        // (default factory preset, 01.Vocal Plates / 000.Vocal Plate).
        // Engine: DattorroPlateVintage (algo 1) — Dattorro tank + dedicated
        // pre-tank HF shelf + in-loop structural HF damping + post-tank
        // 320 Hz box cut. No other factory preset uses this engine.
        //
        // 2026-05-24 calibration vs vintage plate hardware anchor via host-saved .fxp loaded
        // through the harness (--load-state on the reference VST2 chunk format
        // — JUCE auto-unwraps the FPCh wrapper). Reference IR rendered
        // at 100 % wet (Mix=1.0 override on top of the fxp).
        //
        // v9 (2026-05-27): staged_tuner.py 3-stage CMA-ES sweep against the
        // Lex VVP .fxp anchor, 1300 trials, 6 workers. Architecture:
        //   Stage 1 (Spatial+Envelope): Size, Diffusion, Width, ModD, ModR,
        //                               Decay seeded from anchor RT60. Loss
        //                               = env_shape_L1 + stereo_gap + osc_p2p
        //   Stage 2 (Temporal+Decay): Decay, Mults, Crossovers, DPV HF Damp.
        //                             Loss = per-band EDT/t30/t60, low_mid×3.
        //   Stage 3 (Spectral+Polish): Lo/Hi Cut, Sat, Trim, DPV shelves
        //                              CLAMPED to [-6,+6] dB (no cheat path).
        // Result: 9 / 19 listening-relevant gates close. Remaining failures
        // are DOCUMENTED ENGINE CEILINGS — DattorroPlateVintage architecture
        // boundaries vs Lexicon Vintage Plate's MTDL topology:
        //   - cent_50  Δ -41 %   DPV can't push 50ms HF persistence to Lex's
        //                        5191 Hz centroid without the +12 dB HF Shelf
        //                        cheat that the v9 architecture forbids.
        //   - edt low_mid -87 %  Lex holds 250-500 Hz at 126 ms early-decay;
        //                        DPV's coupled HF-damping / decay structure
        //                        collapses to 16 ms. No knob bridges this.
        //   - osc P2P Δ -9.4 dB  Lex modulates loop topology (±22 dB envelope
        //                        pumping); DV uses per-line random-walk LFOs
        //                        (±13 dB). Architectural — not a tuner gap.
        // ALL TEMPORAL GATES (tail_t30, tail_t60, per-band decay sub..hi)
        // and STEADY-STATE LOW-MID PASS — the perceptual sweet spot the
        // earlier non-staged sweep was missing.
        // Gain Trim = 6.7 (user's ear-calibrated value, preserved over
        // optimizer's 9.49 — the +2.79 dB difference is the math-vs-perception
        // gap the snare-RMS gate caught on prior calibrations).
        // Tuned vs lex-vintage-vocal-plate 2026-05-31 (45→21 fails). DPV engine
        // (algo 1); Decay capped 3.0 s (Lexicon Vintage Plate "Vocal Plate" is a
        // short plate, tail ~1.05 s). NOTE: the win is from the 15 CORE params
        // only — the render harness does not expose the 7 DPV corrective-EQ
        // params as --param overrides, so --has-dpv sampled dead axes and those
        // row fields below are LEFT AT THEIR staged_tuner values. 350-trial +
        // 300-trial warm-started re-sweep both floor at 21. Remaining (cent -29%
        // dark, sine1k +5 dB hot, small T60 tilt, 12.9k spike) is DPV-vs-Lexicon.
        { "Vintage Vocal Plate",  "Plates",
          1,  0.5f,   true,  10.0f, 0,
          0.80466f, 0.80357f, 0.29369f, 1.64421f, 1.30000f, 1.38104f,  522.55f,
          0.24230f, 0.00f, 0.30f,  42.811f, 15000.0f, 0.81121f, false, 9.01827f,  // ACCURACY: Treble 1.366->1.30 + HiCut 7366->15000 brightens to the bright Lex anchor (cent -29%->-11%)
          /* mono */ 20.0f, /* mid */ 1.42055f, /* highX */ 7049.45f, /* sat */ 0.12959f,
          /* hiCutShelfGainDb */ -12.0f,
          /* gate */ true,
          /* sixAPDensityBaseline */ 0.62f, /* sixAPBloomCeiling */ 0.85f,
          /* sixAPBloomStagger    */ { 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f },
          /* sixAPEarlyMix        */ 0.5f,  /* sixAPOutputTrim    */ 1.3f,
          /* bassChoke            */ 20.0f,
          /* dpvHfShelfGainDb     */ 3.25f,       // DPV corrective EQ unchanged — the render
          /* dpvHfShelfFreqHz     */ 4049.0f,     // harness does NOT expose these as --param,
          /* dpvStructHfDampHz    */ 6605.0f,     // so the sweep's --has-dpv axes were no-ops;
          /* dpvBoxCutGainDb      */ -1.52f,      // the 45→21 win is from the CORE params only.
          /* dpvBoxCutFreqHz      */ 704.0f,
          /* dpvBassShelfGainDb   */ 0.82f,
          /* dpvBassShelfFreqHz   */ 89.7f },
        // ── Drum Plate (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "Drum Plate" preset (Reverb Mode = Plate,
        // HighShelf at max for bright top, HighCut ~6 kHz) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Plates,
        // has_dpv=False (FDN). 1300 trials.
        //   Stage 1 loss 1.05  (subtle envelope)
        //   Stage 2 loss 145.7 (per-band decay tight)
        //   Stage 3 loss 8.33  (clean polish)
        //
        // 12 / 40 gates fail.
        // Locked 2026-05-31 at the 23-fail floor vs VVV "Drum Plate" (from 27).
        // The win is feed-forward Input Sub +2.02 dB (in kFiveBandByName above):
        // it restored the low-end BODY that read "weak" by ear (ss-deep-sub/
        // ss-sub now pass). Residual 23 is the FDN steady-state limit — a
        // −5.86 dB 1 kHz null (un-fillable within loop stability: in-loop boost
        // capped +2.0, broad input bell disturbs broadband) + mild sub-hot
        // (+3.1, the cost of the +2.02 makeup). Row params unchanged from the
        // 27 baseline; the +2.02 makeup is the only delta.
        // Migrated FDN(4) -> AccurateHall(10) 2026-06-10: composite-exact octave
        // GEQ sets all nine octave T60s directly (kAccurateHallT60ByName) — the
        // old "T60-ceiling" verdict was an artifact of the leaky shelf cascade.
        { "Drum Plate",           "Plates",
          10, 0.42f, false, 12.0f, 0,
          2.263f, 0.337f, 0.373f, 0.119f, 1.296f, 0.723f,  98.99f,
          0.441f, 0.30f, 0.55f,  20.68f, 10078.6f, 1.100f, false, -5.15f,  // Width 0.934->1.10: was too correlated (corr +0.107 vs anchor -0.097); 1.10 closes all 4 width/corr gates (27->23).
          /* mono */ 20.0f, /* mid */ 0.690f, /* highX */ 7762.3f, /* sat */ 0.214f },
        // ═══════════ SPRINGS ═══════════
        // ── Surf '63 Spring ──────────────────────────────────────────────────
        // Engine: SpringEngine (algo 4). Reference: Fender 6G15 outboard
        // reverb unit driving a clean amp — Dick Dale "Misirlou" (1962),
        // every surf-rock tremolo-picked lead through 1962-65. Short-spring
        // tank, mild dispersion, classic 4 kHz spring rolloff. The hijacked
        // mod_depth (SPRING LEN) and mod_rate (DRIP) knobs control the
        // characteristic ambient wobble.
        { "Surf '63 Spring",      "Springs",
          5,  0.35f, false,  0.0f, 0,
          1.60f, 0.40f, 0.20f, 1.50f, 1.00f, 0.85f, 1000.0f,
          0.45f, 0.10f, 0.30f,  80.0f,  4000.0f, 1.10f, false, 2.5f,
          /* mono */ 20.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Bright Hall (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: Valhalla Vintage Verb "Bright Hall" factory
        // preset (Reverb Mode = Bright Hall, Color Mode = now) @ 100% wet.
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Halls.
        // 1300 trials, 6 workers. Anchor t60→knob 3.255 s, sigma 1.04×.
        //   Stage 1: loss 0.72   (Spatial + Mod + Mod Depth clamp [0,0.15])
        //   Stage 2: loss 55.15  (Decay + Mults + Crossovers, low_mid×3)
        //   Stage 3: loss 8.81   (Lo/Hi Cut + Sat + Trim, Hi Cut floor 4kHz)
        //
        // 7 / 40 gates fail — best result in the Halls category:
        //   - ALL 8 ss-band energies pass within ±1.5 dB.
        //   - cent_50 Δ -2.9 %, cent_500 Δ +11.6 % — both pass.
        //   - tail_t30 Δ -4.6 %, tail_t60 Δ -16.4 % (well under ±25 %).
        //   - env_p2p Δ -0.64 (dead-on), stereo_corr Δ +0.06 (dead-on).
        //   - env_shape_L1 2.94 dB (under ±3).
        //
        // Remaining 7 fails:
        //   - spec_L1 mean 3.21 dB (mid-band texture residue)
        //   - spec_L1 max 7.52 dB @ 1016 Hz (FDN mode)
        //   - edt low / edt mid (49 % / 32 % — FDN-vs-VVV EDT structural)
        //   - decay low / decay mid (+47 / -30 % — same structural)
        //   - osc P2P +4.5 dB (mod depth at 0.10 in clamp; FDN per-line LFO
        //     produces slightly heavier envelope ripple than VVV's slow drift)
        // v33 master REVERTED (2026-05-29): sweep was optimized against an
        // engine that had the ModulatedDamping coefficient-lerp instability
        // bug (raw (a1,a2) lerp pulled biquads outside stability triangle
        // twice per LFO cycle → user heard "garbage / mud / boom"). With
        // that bug now fixed via the 32-step lookup table, v33's sweep-
        // optimal params no longer balance — bass +4.25 dB hot at 63-125 Hz,
        // mids/highs -2 to -6 dB cold. Reverting to original v1 factory row
        // (Decay 4.31, Bass 1.38, Treble 1.29, Mid 0.68 — the 16-fail
        // baseline). Future sweeps run against the bug-fixed engine will
        // produce correctly-balanced params.
        // ── Bright Hall: V2.0 baseline on VintageTank (algo 8) ──────────────
        // Decay 5.0s targets VVV anchor 4.73s @ 1k T60 (round-trip math
        // empirically calibrated). 3-band damper voicing:
        //   Bass Mult 1.10 → +0.83 dB low-shelf @ 250 Hz  (lifts 63/125 ✓)
        //   Mid  Mult 1.20 → +1.58 dB peaking @ √(250·4000)=1k (lifts mid T60)
        //   Treble Mult 0.85 → -1.41 dB high-shelf @ 4 kHz (chokes 1k ring)
        //   Hi Cut 4000 Hz → 3-band damper highFc
        // Gain Trim +2.0 dB compensates for outputGain_ default 1.0
        // (previously baked into tap scale 0.74).
        // BH-5 single-axis revert on top of BH-4 (2026-05-30):
        //   lowCrossover 350 → 250 — reverts BH-3's failed bass/mid split
        //                              shift. The raise pushed 500 Hz +
        //                              1 kHz into the mid-band peaking
        //                              region (midFc = sqrt(350 * 7000) =
        //                              1565 Hz) where midMult=1.12 didn't
        //                              reach down far enough → both bands
        //                              flipped cold. Reverting restores
        //                              the BH-2 T60 setup where 8 of 9
        //                              bands were within ±5%. 250 Hz cold
        //                              is structural — not a crossover
        //                              boundary issue.
        // BH-FINAL on VintageTank algo=8 (locked 2026-05-30):
        //   erLevel 0.37 — BH-8 optimal ER profile. Closed env_shape_L1
        //                    + improved edt low 100-250 by 3.3x. BH-9
        //                    proved erLevel 0.37 ↔ 0.39 below measurement
        //                    resolution on sub-bass band → reverted to
        //                    0.37 to lock env_shape + edt low wins.
        //   Diffusion 0.58 — BH-8 nudge held.
        //   Sub-bass <100 -2.03 dB cold accepted as engine signature —
        //   not addressable via current parameter set. gainTrim trade
        //   would push sine1k over gate (zero-sum boundary).
        //
        // Net session: BH 25 → 10 fails (-15).
        // AccurateHall trial 2026-06-10 (P3): algo 8 -> 10 with a calibrated
        // nine-octave T60 map; composite-exact GEQ made the 7.3 s anchor lows
        // reachable (the old leaky cascade diverged >190 s). A/B'd against
        // the VintageTank baseline (18) — ship the winner.
        { "Bright Hall",          "Halls",
          10, 0.40f, false,  0.0f, 0,
          5.0580f, 0.93236f, 0.04761f, 1.45608f, 0.77929f, 0.93713f,  170.39f,
          0.90000f, 0.37f, 0.55f,  26.856f, 4554.46f, 1.00000f, false, 0.62933f,  // Diffusion 0.499->0.90 (manual 2026-06-07): scatters the 12.9k metallic modal ring (spec_L1 max 8.06->6.44), fixes cent_50 + sine1k loudness, halves pitch-chorus 7.5x->3.14x; n_fail 20->18. Width 0.944->1.00: closes residual global stereo_corr.
          /* mono */ 20.0f, /* mid */ 0.80743f, /* highX */ 6389.40f, /* sat */ 0.13963f,  // re-derived post Decay-calibration (honest Decay 5.06 s; was 10->17 fails on the recalibrated VintageTank)
          /* hiCutShelfGainDb */ -6.0f },  // AccurateHall trial: -2 brightened LATE HF level too (bloom 8-12k +6 dB, gain==decay==level) — kept at -6; early-HF dark is compensated post-tank (pteq 10 kHz boost).
        // ── Deep Blue REMOVED 2026-05-31 ──────────────────────────────────────
        // The SixAP "massive concert hall" Deep Blue was redundant: the hall
        // niche is covered by Cathedral Large Hall / Bright Hall / Vocal Hall /
        // Blade Runner 224, and the shimmer "Deep Blue Day" keeps the name theme.
        // ═══════════ HALLS ═══════════
        // ── Vocal Hall (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: Valhalla Vintage Verb "Vocal Hall" factory
        // preset @ 100% wet. Loaded into harness via .vpreset XML param
        // extraction (Concert Hall reverb mode + specific Decay/Size/Bass/
        // HighShelf/etc settings).
        //
        // v15 (2026-05-29): staged_tuner.py sweep with two new asymmetric
        // loss terms wired in response to v14 listening verdict ("boomy"):
        //   - boom_loss      late-window low-band integrated RMS (40-300 Hz
        //                    × 0.5-2 s peak-aligned), cubic-hot 6× / quad-cold.
        //                    v14 audition: +3.17 dB DV-hot in 500ms-1s × sub.
        //   - tail_mod_loss  per-band detrended Hilbert env std (asymmetric
        //                    DV-hot only). v14 mid 1-4k tail ripple std
        //                    4.15 dB vs Lex 0.95 dB — audible slow wobble.
        //
        // Result: 7 / 29 gate fails (was 9). Both audible defects CLOSED:
        //   - boom sub 40-100 500ms-1s:  +3.17 → +1.62 dB  ✓
        //   - ripple mid 1-4k:           +3.20 → +1.27 dB  ✓
        //   - cent_50 / cent_500:        -1.7 % / -2.2 %   ✓
        //   - All 8 SS bands             within ±0.77 dB   ✓
        //   - spec_L1 mean/max           1.38 / 3.75 dB    ✓
        //
        // Optimizer fixed boom + wobble via Decay Time 3.50 → 2.73 s
        // (toward anchor 2.83) + Hi Cut 11569 → 7179 Hz, NOT by lowering
        // Bass Multiply. Less HF feedback + shorter decay window = both
        // axes closed simultaneously.
        //
        // 7 open gates: 3 FDN architectural EDT signature (edt sub / low /
        // hi) shared with all Halls — Phase 3 TimeVaryingDamping target.
        // 2 bass-cold slight overcorrection (boom 100-300 1-2s -2.59 dB).
        // env_p2p +10.22 dB (DV transients punchier — audibility TBD).
        // env_shape_L1 3.27 dB (just over 3.0 gate, JND-marginal).
        // 2026-05-29 revert: v18-manual + v29 PostTankEQ pass produced an
        // audible chorus/wobble + bus-mode garbage on FDN. Rolled VH back
        // to v15 Optuna baseline (HEAD commit 54ef861 / 95558c8 lineage)
        // where the FDN path was stable.
        //
        // Step 1 retune on top of v15 baseline (2026-05-30): bump decay
        // 2.04 → 3.50 s. T60 was cold across every band (-47% bass to
        // -8% air) against /tmp/anchor_vh; the v15 row was tuned against
        // a different anchor with much shorter tail. Extending the decay
        // runway closes the T60 fails + the downstream decay-ratio + EDT
        // + boom + body cascades in one move.
        // Step 4-6 retune on top of Step 1-3 (2026-05-30):
        //   Damping (Treble Multiply)  0.86 → 0.55  — chokes 1-8 kHz tail
        //                                              that overshot +24..+44%
        //                                              after Step 1 decay bump.
        //                                              NOTE: Step 4 was a no-op
        //                                              until the Step 7
        //                                              setAirTrebleMultiply
        //                                              plumbing bug-fix went
        //                                              in (PluginProcessor +
        //                                              DuskVerbEngine).
        //   gainTrim                  -1.52 → -5.0 → -3.0 — Step 6 -5.0 over-
        //                                              corrected and pulled
        //                                              5 ss bands cold; Step 8
        //                                              eases back to the sweet
        //                                              spot between the two.
        // Step 9 retune on top of Step 7-8 (2026-05-30):
        //   Damping (Treble Multiply) 0.55 → 0.75 — relax the cliff that
        //                                            crashed 16 kHz to -24%
        //                                            cold and left a 12.9 kHz
        //                                            spike sticking out.
        //   Mid Multiply              1.13 → 0.85 — actively chokes 250 Hz to
        //                                            4 kHz where the treble
        //                                            damping shelf cannot
        //                                            reach (highCrossover
        //                                            sits at 8 kHz).
        //   Hi Cut                    5884 → 4500 — lowers output rolloff to
        //                                            squash the over-
        //                                            attenuated 12.9 kHz
        //                                            resonant mode at the
        //                                            spec_L1 max slot.
        // Step 10 retune on top of Step 9 (2026-05-30):
        //   midCrossover    396 → 600  — protects 500 Hz band from midMult
        //                                  cut by raising mid-band lower edge.
        //   highCrossover  8042 → 6000 — moves damping shelf knee lower so
        //                                  HF damping reaches into 4-8 kHz
        //                                  without leaving the air band
        //                                  starved.
        //   Damping         0.75 → 0.80 — slight relax now that
        //                                  highCrossover handles more of
        //                                  the upper-mid attenuation.
        //   Hi Cut          4500 → 5500 — recovers air band, centroid,
        //                                  ss hi 5-10k that Step 9 over-
        //                                  cooled.
        //   gainTrim       -3.00 → -2.00 — final broadband level lift
        //                                  toward anchor parity; sine1k
        //                                  was still +3.22 hot so net
        //                                  +1 dB keeps it in gate.
        // Step 11 retune on top of Step 10 (2026-05-30):
        //   Mid Multiply   0.85 → 0.78 — precise trim on 2k/4k/8k overshoot
        //                                  without disturbing 1 kHz node.
        //   gainTrim      -2.00 → -2.50 — corrects 4 hot RMS gates from
        //                                  Step 10 over-correction.
        //   Bass Multiply  1.50 → 1.30 — vents late sub-bass sustained
        //                                  energy without disturbing
        //                                  63/125 Hz initial decay.
        //   Diffusion      0.12 → 0.45 — raises early reflection density
        //                                  to close cold sub/low EDT gates
        //                                  (first 10-15 dB envelope).
        // Step 12 single-axis correction on top of Step 11 (2026-05-30):
        //   Bass Multiply  1.30 → 1.45 — split-the-difference restore of
        //                                  the low-end decay runway. Step 11
        //                                  drop to 1.30 killed 63/125/250/
        //                                  500/1k T60 (5 gates flipped cold)
        //                                  + decay sub -82.9% + body 125-250
        //                                  cold + low band level cold. 1.45
        //                                  recovers bass tails without
        //                                  going back to the 1.50 boom sub
        //                                  hot we had earlier.
        // Step 11 winners locked:
        //   Mid Multiply  0.78  — closed 2k + 4k T60.
        //   Diffusion     0.45  — closed mod bass + mod lowmid.
        //   gainTrim     -2.50  — keeps RMS gates in spec once bass
        //                          tail recovers.
        // Step 13 multi-axis polish on top of Step 12 (2026-05-30):
        //   Bass Multiply 1.45 → 1.40 — works with new 70 Hz PostTankEQ
        //                                 scoop to vent over-sustained sub
        //                                 without disturbing 125/250/500 Hz
        //                                 bands locked at passing.
        //   Mid Multiply  0.78 → 0.82 — pulls 1 kHz tail back from -7.9% to
        //                                 within gate.
        //   Damping       0.80 → 0.78 — slight relax to recover 16 kHz cold
        //                                 (-9.4%) + air band.
        //   Hi Cut        5500 → 6000 — opens air band; resolves cent_500
        //                                 cold (-17.8%) + ss hi 5-10k cold.
        // Step 14 precision polish on top of Step 13 (2026-05-30):
        //   Bass Multiply 1.40 → 1.42 — micro restore for 63 Hz (-12.1%)
        //                                 + 125 Hz (-5.9%) without
        //                                 disturbing higher bands.
        //   erLevel       0.45 → 0.65 — raises early reflection deck to
        //                                 close EDT sub/low/lowmid cold
        //                                 fails (first 10-15 dB envelope).
        //   erSize        0.55 → 0.45 — redistributes early-tap timeline
        //                                 to support new erLevel.
        { "Vocal Hall",           "Halls",
          10, 0.35f, false, 22.0f, 0,    // algo 10 = AccurateHall (2026-06-09): per-octave GEQ T60. T60 9/9, gain-matched full_check 22->20 vs FDN. Front-load levers carry over (post-tank, engine-agnostic).
          3.50f, 0.76f, 0.50390f, 0.78820f, 1.0840f, 1.3570f,  850.0f,  // FRONT-LOAD CAMPAIGN 2026-06-08: Treble 1.091->1.084, Bass 1.3042->1.357 (co-tuned with tank-rebalance). Decay/ModDepth/LowX unchanged. er_level + mono now set by kFrontLoadByName (0.79 / 20).
          0.77940f, 0.79000f, 0.44870f,  33.0f,  6000.0f, 0.96000f, false, 2.00f,  // erLevel 0.608->0.79 (front-load deck) + Width 0.995->0.96 + GainTrim -2.5->+2.0: with er_decorr 0.6 (kFrontLoadByName) the width family lands ~VVV; GainTrim + erLevel restore level/front-load after tank_level 0.42 cut.
          /* mono */ 20.0f, /* mid */ 0.7530f, /* highX */ 6000.0f, /* sat */ 0.0f },  // Mid 0.76->0.753. MonoBelow 150->20 (mono was correlating the low, fighting er_decorr's image fix). Sat 0.0 (clean highs).
        // ── Cathedral (VVV anchor) ─────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "CathedralLargeHall" preset (Reverb Mode
        // = Cathedral, ModDepth 75 %, HighShelf at 6 kHz, HighCut ~7 kHz).
        //
        // Locked 2026-05-30: direct-scoreboard + warm-start FDN sweep,
        // 33 → 27 gate fails vs VVV "CathedralLargeHall" anchor (verified via
        // --program). Ripple + bloom fully green. Residual 27 hits the SAME
        // structural wall as Vocal Plate — HF T60 overshoot the single
        // trebleMult can't bend (8k wants 1.79s, FDN gives 3.13s, +75%) + sub
        // energy/decay deficit. The FDN structural refactor targets exactly
        // this. See memory duskverb_tuning_method.
        // Migrated FDN(4) -> AccurateHall(10) 2026-06-09: the composite-exact
        // octave GEQ sets all nine octave T60s directly (kAccurateHallT60ByName),
        // closing the 9-vs-5 decay-coupling block the comment above describes.
        { "Cathedral Large Hall", "Halls",
          10, 0.45f, false, 20.88f, 0,
          3.00000f, 0.93880f, 0.38010f, 1.18680f, 1.40800f, 1.24610f,  223.90f,  // ACCURACY: Decay 3.315->3.00 matches anchor tail length (tail_t60 +26% -> ~0). ModDepth/Rate+BassMult prior.
          0.70090f, 0.48f, 0.36f,  40.730f, 4834.0f, 1.02400f, false, -7.90200f,
          /* mono */ 20.0f, /* mid */ 0.64240f, /* highX */ 5442.0f, /* sat */ 0.00126f,  // re-swept w/ FDN FiveBand+input-makeup axes 26->20 vs CathedralLargeHall (Decay->3.3s near ref 2.7)
          /* hiCutShelfGainDb */ -14.5f },
        // ── Blade Runner 224 ─────────────────────────────────────────────────
        // Anchor: Vangelis on the late-1970s digital hall hardware (Hall A / Constellation) —
        // "Tears in Rain" / "Memories of Green". Validated against the
        // Arturia Rev LX-24 "Large Hall" preset rendered through the same
        // noise-burst test signal.
        //
        // Architecture: Dattorro 2-AP (figure-8 cross-coupled topology, the
        // closest historical match to the 224's hardware tank).
        //
        // Reference targets from Arturia LX-24 measurement (BladeRunner
        // user preset rendered via Apple-native AU API on 2026-04-27):
        //   RT60               5.45 s
        //   Initial centroid   12 kHz at -16.7 dB (bright "reference hardware snap")
        //   Settled centroid   ~1.2 kHz by 1.5 s
        //   LR correlation     +0.00 (essentially mono-centered, natural)
        //
        // Validated 2026-04-27 against Arturia LX-24 BladeRunner preset
        // (rendered via direct AudioUnitSetParameter — Arturia's
        // setStateInformation is a no-op for non-JUCE state, verified by
        // post-load param dump). The actual user-tuned target measures:
        //   RT60 9.73 s, initial 12 kHz/-15.7 dB, LR mean ~0,
        //   LR stddev 0.028, mid-tail centroid 1700-2700 Hz.
        //
        // Tuning history:
        //   • Original (pre-2026-04-27): decay 10 / modDepth 0.35 / width 1.40
        //       — RT60 9.3 s but width=1.40 produced -0.33 LR anti-correlation
        //         (audible static phasiness)
        //   • Mis-targeted round (chased Arturia DEFAULTS, not user preset):
        //       decay → 4 / mod → 0.03 / damping → 1.0 / diffusion → 0.50
        //       — RT60 collapsed to 5 s, way short of true target
        //   • This round (chases REAL user-tuned Arturia):
        //       decay → 9 / diffusion → 0.85 (smoother sustained tail)
        //       Keeping width=1.0 (phasiness fix) + mod 0.03 / 0.45 Hz (tight
        //       LR jitter) + Hi Cut 10 kHz / damping 1.0 (HF preserved) +
        //       erLevel 1.0 (bright ER burst) + highX 5 kHz.
        //
        // 2026-05-25 re-anchor: prior preset used Dattorro (algo 0) at 9 s
        // decay — wrong engine for a Random Hall topology. Migrated to FDN
        // (algo 4) with Lex Random Hall "Large RHall 4" anchor params.
        //
        // Lex Large RHall 4 spec (path: LexRandomHall / 03.Large Halls /
        // 030.Large RHall 4.xml):
        //   Reverb_Time 5.47 s, Size 69 m, Diffusion 100 %, BassRT 1.5×,
        //   Bass_XOV 360 Hz, RT_HiCut 4500 Hz, Spin 2.2 Hz, Wander 22 ms,
        //   Predelay 25 ms.
        //
        // Defining 224 Random Hall character: dense diffusion + extended bass
        // + aggressive HF damp + heavy modulation (Wander 22 ms = LARGE per-
        // line wander, the "Random" in Random Hall).
        // Calibrated 2026-05-25: cent_50 −6.2 % vs Lex Large RHall 4
        // anchor (within strict ±10 %), cent_500 −15.4 % (FDN engine
        // ceiling on late-tail HF retention; Lex Random Hall's multi-tap
        // input carries HF energy further into the 500-1500 ms window
        // than FDN's modal density allows). Treble Multiply at max (1.50),
        // Hi Cut at max (20 kHz), High Crossover at 4500 Hz.
        // BR-9 sub-25 push on top of BR-8 (2026-05-30):
        //   gainTrim -9.40 → -9.50 — snap sine1k +2.08 dB into spec.
        //   damping/bassMult/midMult KEEP.
        //   PTEQ Band 3 -5.0 → -6.0 dB in PluginProcessor.cpp.
        { "Blade Runner 224",     "Halls",
          10, 0.45f, false, 25.0f, 0,    // algo 10 = AccurateHall (2026-06-09): per-octave GEQ T60. T60 8/9, gain-matched full_check 20->15 vs FDN. Extreme per-octave spread (9.8s low -> 1.3s 16k).
          13.6421f, 0.91981f, 0.33084f, 2.64911f, 0.86140f, 1.05238f,  330.94f,
          0.84476f, 0.00f, 0.50f, 56.210f, 14429.68f, 1.10000f, false, -3.93657f,  // HF damping re-swept (Treble/HiCut/FiveBand-HF): curb metallic 8k/16k ring, 21->19. Width 1.697->1.10: was over-wide/anti-phase (corr -0.37 vs anchor -0.149); 1.10 closes 3 width-band gates (26->23). Residual global stereo_corr is anchor's freq-dependent decorrelation, global Width can't match.
          /* mono */ 20.0f, /* mid */ 0.73614f, /* highX */ 3980.22f, /* sat */ 0.17579f,
          /* hiCutShelfGainDb */ -11.3f },  // RE-ANCHORED to VVV "Homestar Blade Runner" (Concert Hall, 10s, dark) — the prior lex-rhall-rhall4 anchor was WRONG (57->23 w/ FDN FiveBand+input-makeup axes)
        // ── 79 Vocal Chamber (VVV anchor) ──────────────────────────────────
        // Engine: QuadTank. Anchor: VVV "79 Vocal Chamber" preset (Reverb
        // Mode = Chamber1979) @ 100% wet.
        //
        // Tuned vs vvv-79vc 2026-05-31 (39→22 fails). Chamber1979 is a DARK
        // vocal chamber (air -72 dB, near-silent top) with a medium-long tail
        // (T60 ~3.5-5 s) — three axes had to be constrained or the optimizer
        // distorted the character: (1) an unconstrained bright local min hit 22
        // too but with cent_50 +53% / +25 dB air (a wrong, glassy chamber); (2)
        // forcing dark via Treble≤1.0 + HiCut≤9k restored cent_50 (✓ +8%) but
        // over-wide Width (1.97) went phasey (stereo_corr fail) and over-narrow
        // (0.55) lost decorrelation gates. Width [0.85,1.25] hit the anchor's
        // near-mono +0.07 correlation. Result: dark character + correct stereo +
        // tail (tail_t60 -7%) at 22. Remaining (cent_500/air still bright, T60
        // tilt, comb ripple) is the QuadTank vs Chamber1979 modal-density gap.
        { "79 Vocal Chamber",     "Chambers",
          3,  0.30f, false,  8.39f, 0,
          4.8190f, 0.76512f, 0.54685f, 1.74903f, 0.50150f, 0.94761f,  675.47f,
          0.52932f, 0.20f, 0.44f,  26.022f, 8021.01f, 0.97000f, false, -7.39142f,  // Width 1.136->0.97: baked value went anti-phase (stereo_corr -0.11); 0.97 lands anchor's near-mono +0.07 across all 3 width bands, 26->23
          /* mono */ 20.0f, /* mid */ 0.56053f, /* highX */ 5417.19f, /* sat */ 0.08377f,  // re-derived post Decay-calibration (honest Decay 4.82 s; was 22->24 fails)
          /* hiCutShelfGainDb */ -23.5f },
        // ═══════════ CHAMBERS ═══════════
        // ═══════════ ROOMS ═══════════
        // ── Small Drum Room (VVV anchor) ───────────────────────────────────
        // Engine: QuadTank. Anchor: VVV "Small Drum Room" preset (Reverb
        // Mode = Ambience, ModDepth = 100 %, HighCut low for dark room).
        //
        // v1 (2026-05-27): staged_tuner.py autonomous --category Rooms.
        // 1300 trials. Stage 1 1.28 / Stage 2 109.58 / Stage 3 60.02.
        // 24 / 40 gates fail — QuadTank vs VVV Ambience reverb mode + heavy
        // modulation is a wider gap than expected.
        // SDR-2 on top of SDR-1 (2026-05-30):
        //   size       0.25 → 0.10 — Size dominates loopLength in
        //                             gEffTarget = pow(10,-3*L/(decay*sr)).
        //                             SDR-1 missed this lever.
        //   decay      0.18 → 0.10 — paired floor drop; alone insufficient
        //                             at Size=0.25.
        //   modDepth   0.35 → 0.03 — REVERT. QuadTank LFOs modulate AP
        //                             coefficients (phase mod), not
        //                             envelope. Hilbert envelope FFT
        //                             gates can't see it → dead leverage.
        //   modRate    1.50 → 1.11 — REVERT same reason.
        //   hiCutShelf -6.0 — KEEP (air band recovery from SDR-1).
        // SDR-NL1 engine swap on top of SDR-4 (2026-05-30):
        //   algorithm  3 → 6 (QuadTank → NonLinear).
        //               Master engine audit verdict: NonLinear's feed-
        //               forward TDL topology delivered zero modal ripple
        //               (0.00 across all 4 bands) and lowest fail count
        //               (41) for this 56 ms anchor footprint.
        //   gainTrim  -2.13 → +6.87 — uniform +9 dB lift to compensate
        //               for the audit-measured ~9 dB level deficit
        //               (noiseburst -8.55, snare -9.12) on NonLinear.
        //   hiCutShelf -6.0 KEEP — air recovery still applies.
        //   QuadTank-era tunes (size 0.10, decay 0.10, bassMult 0.20,
        //   crossover 300) retained as starting point — may need
        //   NonLinear-specific retune in later steps.
        // SDR-NL3 dual-axis spectral flatten on top of SDR-NL2 (2026-05-30):
        //   gainTrim +3.87 → +6.20 — global lift to bring wings (deep sub
        //                              + air) back into passing range.
        //   PostTankEQ Band 2: 1500 Hz Q=0.5 -3.5 dB scoop (PluginProcessor.cpp)
        //                              counters the mid-bulge so 500 Hz to
        //                              4 kHz stays in spec under the +6.20
        //                              lift. Combined effect: net +6.20 dB
        //                              at wings, net +2.7 dB at mids.
        // SDR-DAT initial baseline (2026-05-30): repaired row after
        // botched harness substitution. Dattorro engine swap +18.5 dB lift.
        // SDR locked at NL7 (NonLinear) — best deterministic floor.
        // Engine swap audit + 8-gen autosweep across NL/Dat:
        //   QuadTank floor: 42 | NL7: 27 | Dat4: 35.
        // NL7 wins for this anchor (56 ms tail target).
        // SDR-DATTORRO re-engine (2026-06-01): NonLinear(6) -> Dattorro(0), 35 -> 23.
        // The old NonLinear pick (and the QuadTank/Dat audit before it) was made
        // against the BROKEN dry-only anchor. Re-auditing ALL engines vs the
        // corrected anchor showed every engine walls ~29-36 on the SAME 8 gates
        // (mod x4 + ripple x4) — but those gates were measuring the NOISEBURST
        // tail at 0.5-3s / 1-3s, which is digital silence (-118 dB) for a 0.4s
        // room: an FFT/std on the dither floor, not modulation. full_check now
        // measures ripple/mod on the SUSTAINED render (real steady-state) and
        // floor-skips boom windows below -90 dB (commit alongside this). On the
        // corrected gates Dattorro's dense allpass tail matches the anchor's
        // ripple/mod and lands at 23 (decay 0.40s, tail_t60 0.46s vs 0.41,
        // env_p2p +66 vs +68, peak 0.44 — honest, not gate-gamed). Residual 23
        // is T60-tilt (Dattorro's 3-band damping can't flatten the Lexicon
        // hall's even decay across 8 bands) + edt onset + air. Dattorro is
        // non-FDN, so no kFiveBandByName entry (FiveBand/makeup are no-ops).
        { "Small Drum Room",      "Rooms",
          0,  0.25f, false,  1.18f, 0,
          0.40083f, 0.32752f, 0.00430f, 0.23945f, 1.13952f, 1.12685f,  516.73f,
          0.84013f, 0.80f, 0.57f,  22.132f, 4799.3f, 1.00000f, false, 11.53915f,  // Width 1.125->1.00: was over-wide (bands -0.135 vs anchor ~0); 1.00 is local min (28->26).
          /* mono */ 20.0f, /* mid */ 1.21799f, /* highX */ 8771.6f, /* sat */ 0.06325f,  // Dattorro re-engine vs CORRECTED anchor (sustained-gate full_check) -> 23.
          /* hiCutShelfGainDb */ -4.50f },
        // ── Tiled Room (VVV anchor) ────────────────────────────────────────
        // Engine: FDN. Anchor: VVV "Tiled Room" preset (Reverb Mode =
        // Chamber, Size 0.107, EarlyDiffusion 0.35, LateDiffusion 0.5).
        //
        // Locked 2026-05-31: direct-scoreboard + warm-start FDN sweep (full
        // pipeline — FiveBand + input makeup + in-loop peak), 47→28 vs VVV
        // "Tiled Room". Extended params (Sub/Hi-Mid mult, crossovers, input
        // makeup, in-loop +1.32 dB) in kFiveBandByName above.
        // AccurateHall trial 2026-06-10: algo 4 -> 10, calibrated octave map;
        // in-loop peak 0.223 zeroed (distorts accurate-RT decay — Cathedral
        // lesson). A/B'd vs the FDN baseline (18).
        { "Tiled Room",           "Rooms",
          10, 0.30f, false,  8.20f, 0,
          0.5684f, 0.47940f, 0.43350f, 2.39800f, 0.68070f, 1.31900f,  173.10f,
          0.41390f, 0.46f, 0.40f,  34.100f, 7090.39f, 1.00000f, false, -0.39990f,  // Width 0.535->1.00: the prior [0.35,0.60] sweep OVERSHOT corr -0.34 -> +0.50 (now anchor is +0.007, DV was too narrow/correlated). 1.00 closes width mid+hi (23->21). Residual corr/width-low is global-vs-per-band mismatch.
          /* mono */ 20.0f, /* mid */ 1.33500f, /* highX */ 4276.0f, /* sat */ 0.15320f },  // RE-TUNED vs CORRECTED anchor -> 21. The prior vvv-tiled-room anchor was BROKEN (dry-only render, no reverb) so the old "25" was gate-gamed degenerate (clipped, no tail). Real anchor = VVV Tiled Room (Chamber mode ~0.8s); sane makeup, honest 21.
        // ── Ambience (VVV anchor) ──────────────────────────────────────────
        // Engine: QuadTank. Anchor: Valhalla Vintage Verb "Ambience" preset
        // (Reverb Mode = Ambience) @ 100% wet.
        //
        // Tuned vs vvv-ambience 2026-05-31 (43→18 fails). CRITICAL: the swept
        // Decay range had to be CAPPED at 3.0 s — VVV Ambience is very short
        // (tail_t60 1.14 s) and with the wide [0.2,30] range the optimizer gamed
        // noiseburst spec_L1 with a 4-6 s wash (31 fails but +200..+577% on every
        // T60 band — wrong character). Capped, it found the correct short room:
        // decay 1.47 s (tail_t60 1.05 s, Δ -8%), sparse diffusion 0.016 (pure
        // early-reflection ambient), Width 1.83 (the anchor itself is anti-
        // correlated -0.32, so wide PASSES stereo_corr here). Remaining fails
        // (cent_500 bright, QuadTank comb ripple, T60-low-band tilt) are the
        // QuadTank topology vs VVV's Ambience modal character.
        { "Ambience",             "Rooms",
          10, 0.40f, false,  2.91f, 0,   // algo 10 = AccurateHall (2026-06-09): per-octave GEQ T60. QuadTank floored 33 (8/9 T60 fail); AccurateHall closes all T60 → gain-matched full_check 33->21. QuadTank+GEQ (AccurateChamber) was tested + rejected (tone-wrecked). Octave targets in kAccurateHallT60ByName.
          1.3875f, 0.26283f, 0.17870f, 0.17487f, 0.98911f, 0.96938f,  327.98f,
          0.68861f, 0.89f, 0.56f,  30.214f, 15746.05f, 1.42044f, false, -0.29995f,
          /* mono */ 20.0f, /* mid */ 0.64948f, /* highX */ 5834.41f, /* sat */ 0.16156f },  // re-derived post Decay-calibration (honest Decay 1.39 s; was 18->20 fails)
        // ── 1981 Gated Snare ─────────────────────────────────────────────────
        // Engine: NonLinear v6 (algo 5) — TRUE STATIC FIR. The envelope
        // (attack ramp → flat plateau → mathematical cliff) is baked into
        // the per-tap gains, so EVERY input sample is convolved with the
        // same fixed-shape FIR. No trigger, no envelope follower — this is
        // the AMS RMX16 NonLin algorithm exactly as Hugh Padgham used it
        // on Phil Collins's "In The Air Tonight" (1981).
        //
        // FIR ENVELOPE PARAMETERS (re-purposed UI knobs on this engine —
        // see PluginEditor::applyEngineAccent for the visible labels):
        //   ATTACK    mod_depth 0.15  →   3 ms ramp up
        //   HOLD      decay 0.150     → 150 ms flat plateau
        //   RELEASE   mod_rate 2.30   →  15 ms hard cliff to silence
        //   DENSITY   diffusion 0.50  → 50% tap jitter / L-R decorrelation
        //
        // Other voicing:
        //   bass_mult 1.00       → neutral output gain
        //   hi_cut 14000         → bright snare crack passes through
        //   width 1.40           → wide stereo (per-tap L/R decorrelation)
        //   mono < 100 Hz        → tight bass focus
        //   saturation 0.20      → analog crunch (RMX16 had a noisy front-end)
        // v7: REAL hall + sidechain noise gate (Townhouse Studios technique).
        //   HALL: decay 1.5 s, size 0.70, bass 1.0, treble 0.80 (slight darkening)
        //   GATE: threshold -32 dB (mid 0.75), attack 1 ms (mod_depth 0.0),
        //         hold 150 ms (diffusion 0.30), release 210 ms (mod_rate 1.117)
        //   This is the "In The Air Tonight" Phil Collins/Padgham/Townhouse sound:
        //   thick hall bloom for 150 ms then a longer fade to silence.
        //
        // DESIGN-LED — NO valid external anchor (confirmed 2026-05-31). This is
        // an attempt at the Phil Collins "In The Air Tonight" gated-snare sound,
        // a famous RECORD PRODUCTION, not a reproducible plugin preset. It was
        // previously mis-anchored to VVV "84 Small Room", which is a tiny bright
        // NON-gated room (56 ms tail, +10..+28 dB hotter) — matching it would
        // un-gate the snare and destroy the preset. Do NOT score this against
        // 84 Small Room. The gate cliff IS the preset; tune by ear only.
        // (NB: the "In The Air Tonight" preset chases the same sound — overlap.)
        { "1981 Gated Snare",     "Rooms",
          6,  1.00f, false,  0.0f, 0,
          1.79f, 0.96f, 0.22f, 3.00f, 1.05f, 2.18f, 2197.0f,  // Decay re-pointed 1.67->1.79 to preserve underlying FDN RT60 after NonLinear Decay-calibration (gated; design-led)
          0.83f, 0.00f, 0.00f,  20.0f,  4976.0f, 1.14f, false, -16.6f,
          /* mono */ 100.0f, /* mid */ 0.98f, /* highX */ 5302.0f, /* sat */ 0.34f },
        // ── In The Air Tonight ──────────────────────────────────────────────
        // Engine: NonLinearEngine v7 (algo 5). Reference: Hugh Padgham's
        // Townhouse Studios technique on Phil Collins's "In The Air Tonight"
        // (1981) — the iconic gated snare sound. Big hall + sidechain
        // noise gate. Tuned by ear:
        //   HALL: decay 2.61 s, size 0.80, bass 1.10, treble 0.75 (slight darkness)
        //   GATE: enabled, threshold via mid (0.75), attack 8 ms (mod_depth 0.092),
        //         hold 250 ms (diffusion 0.50), release 150 ms (mod_rate 0.794)
        //   Mix 22 % — light send/return blend, dry-forward.
        { "In The Air Tonight",   "Rooms",
          6,  0.216f, false,  0.0f, 0,
          2.25f, 0.80f, 0.092f, 0.794f, 0.75f, 1.10f,  500.0f,  // Decay re-pointed 2.608->2.25 to preserve underlying FDN RT60 after NonLinear Decay-calibration (gated; design-led)
          0.50f, 0.00f, 0.30f,  60.0f, 10000.0f, 1.30f, false, 0.0f,
          /* mono */ 20.0f, /* mid */ 0.75f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Reverse Taps (vintage rack reverb) ────────────────────────────────────────────
        // Engine: NonLinear (algo 5) in REVERSE mode (diffusion 0.33-0.66
        // selects the reverse envelope). Anchor: vintage rack reverb "Reverse Taps"
        // (Bank P3, Post). Inverse algorithm — energy SWELLS UPWARD before
        // a hard cutoff.
        //   RT60 0.95 s (gate window length)   bass_mult 1.01 (flat)
        //   centroid 50ms 4.1 kHz   centroid 1s 1.7 kHz (darkens dramatically)
        //   shape: REVERSE — peak energy at end of window
        //   predelay 189 ms in the IR (the swell start) — but our preset's
        //   predelay slot is for the small offset before the reverse build
        //   begins; the engine handles the swell shape itself.
        //   LR correlation -0.21 (slightly anti-correlated for stereo width)
        // Most ambient of the gated presets — long swell + long fade.
        // v7: REAL hall + gate. The most ambient of the gated presets —
        // long hall + slow attack + max hold + long release = a swelling
        // wash that fades naturally rather than a hard cliff.
        //   HALL: decay 3.0 s, size 0.85, bass 1.0, treble 0.70
        //   GATE: threshold -32 dB (mid 0.75), attack 25 ms (mod_depth 0.49),
        //         hold 500 ms (diffusion 1.0, max), release 1500 ms (mod_rate 7.52)
        //
        // Tuned vs lex-reverse-1 2026-05-31 (54→34 fails). The prior "SHIP AS-IS
        // engine ceiling at 54" verdict was PREMATURE: ~20 of those fails were
        // the gate closing too early/hard (tail_t60 -86%, body -22dB, boom
        // window dead-silent, decay bands -80%), all tunable. The fix was a long
        // gate release — but the optimizer's Mod Rate axis (= NonLinear release)
        // was capped at 3.0 (a reverb-LFO ceiling); lifting it to the APVTS max
        // 10.0 let release land 9.70, with longer decay (18.8s) + max hold (0.93
        // diffusion) extending the tail to match. The remaining 34 ARE structural
        // to a tap-based gate vs the anchor's smooth backwards-convolution:
        // env_p2p +60 (hard gate cliff), comb ripple +9..+17 (discrete taps), no
        // tail chorus (mod-freq -85%), and a low/high T60 tilt that couples
        // against those. Verified: a 250-trial warm-started re-sweep beat 34 by 0.
        { "Reverse Taps",         "Rooms",
          9,  1.00f, false, 38.0f, 0,           // engine 9 = ReverseRoom; predelay 38ms (measured)
          6.9912f, 0.16498f, 0.13950f, 2.28741f, 0.79816f, 1.14756f,  566.85f,
          0.18188f, 0.00f, 0.30f,  40.596f, 5367.38f, 1.17197f, false, 0.89119f,
          /* mono */ 20.0f, /* mid */ 0.51963f, /* highX */ 8201.39f, /* sat */ 0.25533f },  // RE-ENGINED NonLinear->ReverseRoom: causal rising-ER replicates Lexicon Room "Reverse 1" (reverse-engineered from data). 32->20 fails; env_p2p cliff +60->+15 (smooth swell); tail_t60/cent/env_shape/osc now MATCH the reference
        // ── Mobius Pad ───────────────────────────────────────────────────────
        // Named after the Möbius Twist DSP (sign-inverted cross-feedback —
        // see SixAPTankEngine.cpp). Showcases the 6-AP engine's new
        // Möbius Twist + Bloom + Stereo Bind architecture. 5.5 s decay
        // positioned between Ambient Swell (8 s) and Infinite Blackhole
        // (18 s) so it stays playable for synth players who want infinite
        // stereo width without committing to film-score-length tails.
        // Perpetually anti-correlated stereo (won't collapse to mono on
        // long sustains), volume blooms 200-300 ms into the tail. For
        // sustained synth pads, ambient guitar swells, evolving textures.
        // (ASCII name — keeps shell + UTF-8 toolchain matching reliable.)
        { "Mobius Pad",           "Ambient",
          2,  0.45f, false, 45.0f, 0,
          3.89f, 0.90f, 0.25f, 0.35f, 0.45f, 1.50f,  500.0f,  // Decay re-pointed 5.50->3.89 to preserve ~3.9 s sound after SixAP Decay-calibration (no anchor; design-led)
          0.85f, 0.20f, 0.85f,  80.0f,  9000.0f, 1.50f, false, 4.5f,
          /* mono */ 80.0f, /* mid */ 1.20f, /* highX */ 3200.0f, /* sat */ 0.10f },
        // ═══════════ AMBIENT ═══════════
        // ── Black Hole ───────────────────────────────────────────────────────
        // Reference: external reference Shimmer "BlackHole" factory preset — one of
        // the 8 stock presets shipped with external reference. Huge dark deep ambient void;
        // signature external reference sound for cinematic sustains, drones, and synth pads.
        // Tuned against external reference reference render (DBG_BlackHole_*):
        //   • Algorithm 1 (SixAPTank, 6-allpass cascaded diffuser) —
        //     structurally matches external reference blackhole's Schroeder cascaded-allpass
        //     architecture. Algorithm 3 (FDN) had a 30 ms silence gap before
        //     the burst onset that didn't match external reference's continuous early rise.
        //   • Zero pre-delay — external reference blackhole has reverb starting immediately.
        //   • 14 s decay + size 0.95 for the slow tail decay (~4 dB/s).
        //   • Brightness profile (post-engine-tuning iteration):
        //     - damping (treble multiply) = 1.0 — treble decays at same rate
        //       as mid; air persists into deep tail to match external reference's broadband
        //       sustain. Earlier iteration (0.45) had treble decaying 2.2×
        //       faster, killing >6 kHz content by 1-2 s in.
        //     - midMult = 1.10 — mid decay 10% slower; presence band rings
        //       longer, adds "information" to the tail.
        //     - highCrossover = 8 kHz — pushes mid-shelf turnover above the
        //       presence peak; sparkle band stops getting damped early.
        //     - hiCut = 18 kHz (effectively bypassed) — external reference blackhole has
        //       full-bandwidth content above 12 kHz late in the tail; lower
        //       cuts (e.g. 6.5 kHz) collapsed the >12 kHz tail to silence.
        //     - modRate = 0.60 Hz — slight LFO movement adds shimmer; was
        //       0.40 Hz which felt too still.
        // Differs from "Infinite Blackhole" (modern multi-FX-anchored, 18 s, 6-AP
        // with heavy modulation) by being shorter, less modulated, and
        // tuned to external reference's specific factory voicing rather than modern multi-FX's.
        // Engine-config overrides (sixAP*) close the >12 kHz late-tail gap
        // (was 16 dB cold vs external reference) by injecting more bright ParallelDiffuser
        // content directly into the output (kEarlyMix 0.5→0.75) and letting
        // the density cascade ring denser at later stages (kBloomCeiling
        // 0.85→0.92, steeper stagger). These don't affect other SixAPTank
        // presets because they default to the historical hardcoded values.
        // Re-engined SixAP→Shimmer (algo 7) 2026-05-31: the anchor is the
        // Valhalla Shimmer "Black Hole" preset, a true octave-up shimmer (HF
        // sustaining ~9.6s @ 16k, rising centroid, swelling envelope). SixAP
        // has no pitch-regeneration path, so ~half its gate fails were
        // structurally unreachable. Shimmer engine ("8-ch Hadamard FDN + in-loop
        // granular pitch shifter") supplies the octave feedback. mod_depth 0.5
        // = +12 st; mod_rate maps to shimmer feedback (longer/darker than
        // Deep Blue Day for the huge "black hole" space).
        // Tuned vs valhalla-shimmer-black-hole 2026-05-31 (39→22 fails). Octave
        // PINNED at +12 (mod_depth 0.5) — letting the optimizer detune it spiked
        // inharmonic 12.9k content; dense diffusion (0.857) keeps the noiseburst
        // tail from collapsing. Low mod_rate (0.875 → low shimmer feedback) beat
        // higher rates: more feedback over-lengthened T60-16k AND worsened the
        // 12.9k image spike. Remaining fails (cent dark, T60-HF short, sine1k
        // notch) are the engine's single-image shifter vs Valhalla's broadband
        // multi-voice shimmer — structural, not tunable on this engine.
        // STRUCTURAL CEILING (2026-06-01): cent_50/500, T60 8k/16k and ss-air can
        // NOT be matched here. The GranularPitchShifter anti-aliases the octave-up
        // voice at fc = nyquist/pitchRatio = 12 kHz @48k (ShimmerEngine.cpp:56), so
        // ZERO shimmer energy reaches the 16 kHz band — centroid sticks ~3400 Hz
        // (anchor 6464) and T60-16k caps ~4 s (anchor 9.6 s) even with Treble/HiCut
        // maxed. Locked at the honest floor (20); the brightness gap needs an engine
        // fix (oversample the shifter / dedicated HF voice), not tuning. See memory.
        { "Black Hole",           "Ambient",
          7,  0.50f, false,   0.0f, 0,
          10.8728f, 0.56922f, 0.50890f, 1.43860f, 1.16880f, 0.53601f,  372.24f,  // ModDepth/Rate re-tuned vs honest (sustained) mod gate: 25->21
          0.85741f, 0.05f, 0.70f,  24.591f, 18926.8f, 1.26041f, false, 0.81969f,
          /* mono */ 60.0f, /* mid */ 0.75073f, /* highX */ 3390.34f, /* sat */ 0.38197f },
        // ── Cascading Heaven ─────────────────────────────────────────────
        // +24 semitones (two-octave stack) at ~57% feedback. No external reference factory
        // direct equivalent — kept as our differentiator. Lower feedback
        // than +12 (cascade builds 4× faster at 4× pitch ratio), longer
        // decay (6 s) for the stacked-octave swell, slightly darker hi-cut
        // (6 kHz) to keep the upper-octave stack from glassing up.
        { "Cascading Heaven",     "Shimmer",
          7,  0.361f, false,  60.0f, 0,
          6.00f, 0.85f, 1.00f, 2.705f, 0.95f, 1.10f,  800.0f,
          0.85f, 0.20f, 0.50f,  60.0f,  6000.0f, 1.40f, false, -3.0f,
          /* mono */ 60.0f, /* mid */ 1.00f, /* highX */ 4000.0f, /* sat */ 0.10f },
        // ── Deep Blue Day ────────────────────────────────────────────────
        // Reference: external reference Shimmer "DeepBlueDay" preset (named after the
        // Brian Eno track on *Apollo: Atmospheres and Soundtracks*). 80% wet,
        // +12 octave, ~45% feedback, very long sustained tail. Decay 10.3 s
        // + size 100% gives the long sustained character; lower feedback
        // (45%) keeps the cascade gentle so the long reverb dominates over
        // the pitched recirculation.
        // mod_depth 0.5 = +12 st; mod_rate 4.5 Hz maps to feedback ≈ 0.42.
        // Tuned vs valhalla-shimmer-deep-blue-day 2026-05-31 (43→29 fails). Same
        // shimmer recipe as Black Hole: octave PINNED at +12 (mod_depth 0.5),
        // dense diffusion (0.941), Width capped 1.3 (1.77 went anti-correlated /
        // phasey). Remaining fails (cent dark, 12.9k image spike, T60-HF short,
        // ss air, sine1k notch) are the single-image granular shifter vs
        // Valhalla's broadband multi-voice shimmer — structural on this engine.
        // STRUCTURAL CEILING (2026-06-01): same 12 kHz shifter AA cap as Black Hole
        // (ShimmerEngine.cpp:56, fc = nyquist/pitchRatio). cent ~3380 vs 6464, T60-16k
        // ~4.8 s vs 9.6 s — unmatchable by Hi Cut/Treble/HighX (a 120-trial sweep
        // confirmed flat). Locked at the honest floor (23); needs an engine fix.
        { "Deep Blue Day",        "Shimmer",
          7,  0.38f, false,  25.0f, 0,
          9.1423f, 0.59833f, 0.50f, 0.60458f, 1.40394f, 0.56879f,  408.59f,
          0.80742f, 0.20f, 0.50f,  26.925f, 11000.0f, 1.69030f, false, 0.31973f,
          /* mono */ 20.0f, /* mid */ 1.18105f, /* highX */ 9800.46f, /* sat */ 0.23195f },  // 29->27->23: Shimmer 2nd pitch voice (+24, fills 12-24k) + Hi Cut 4521->11000 so its HF reaches output (matches Valhalla broadband octave; the dark 4521 was choking the new top band)
    };
    return presets;
}
