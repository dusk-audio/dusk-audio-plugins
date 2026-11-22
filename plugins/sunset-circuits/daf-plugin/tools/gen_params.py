#!/usr/bin/env python3
# Generates MultiSynthParams.hpp for the DAF shell. The param order MUST match
# the core msynth::Param enum exactly (indices are shared 1:1).
#
#   gen_params.py            regenerate ../MultiSynthParams.hpp in place
#   gen_params.py --check    regenerate to memory and byte-compare against the
#                            committed header; exit 1 on drift (CI guard so a
#                            hand-edited header can never ship)
import argparse
import io
import math
import os
import struct
import sys

L, LOG, I, B = "LIN", "LOG", "INT", "BOOL"

# (EnumSuffix, symbol, "Name", min, max, default, kind)
P = []
def add(*a): P.append(a)

# --- Global ---
add("Mode","mode","Mode",0,5,0,I)
add("MasterTune","masterTune","Master Tune",-100,100,0,L)
add("MasterVol","masterVol","Master Volume",-60,18,0,L)
add("MasterPan","masterPan","Master Pan",-1,1,0,L)
add("StereoWidth","stereoWidth","Stereo Width",0,1,0.5,L)
add("Oversampling","oversampling","Oversampling",0,2,1,I)
add("AnalogAmt","analogAmt","Analog",0,1,0.2,L)
add("Vintage","vintage","Vintage",0,1,0,L)
# --- Oscillators ---
add("Osc1Wave","osc1Wave","Osc 1 Wave",0,4,0,I)
add("Osc1Detune","osc1Detune","Osc 1 Detune",-50,50,0,L)
add("Osc1PW","osc1PW","Osc 1 PW",0.05,0.95,0.5,L)
# Osc 1 / Osc 2 Level default to the preset baseline's 0.8 / 0.6 (2026-11-21;
# they were 1.0 / 0.8). Cosmos's mixer follows them and the other modes' barely
# do, so at the old values a fresh instance played Cosmos about 2 dB above the
# others while the factory sounds, built on the baseline, matched.
add("Osc1Level","osc1Level","Osc 1 Level",0,1,0.8,L)
add("Osc2Wave","osc2Wave","Osc 2 Wave",0,4,0,I)
add("Osc2Detune","osc2Detune","Osc 2 Detune",-50,50,7,L)
add("Osc2PW","osc2PW","Osc 2 PW",0.05,0.95,0.5,L)
add("Osc2Level","osc2Level","Osc 2 Level",0,1,0.6,L)
add("Osc2Semi","osc2Semi","Osc 2 Semi",-24,24,0,I)
add("Osc3Wave","osc3Wave","Osc 3 Wave",0,3,0,I)
add("Osc3Level","osc3Level","Osc 3 Level",0,1,0.5,L)
add("SubLevel","subLevel","Sub Level",0,1,0.5,L)
add("SubWave","subWave","Sub Wave",0,1,0,I)
add("NoiseLevel","noiseLevel","Noise Level",0,1,0,L)
# --- Filter + envelopes ---
add("FilterCutoff","filterCutoff","Filter Cutoff",20,20000,8000,LOG)
add("FilterRes","filterRes","Filter Resonance",0,1,0.3,L)
add("FilterHP","filterHP","Filter HP",20,2000,20,LOG)
add("FilterEnvAmt","filterEnvAmt","Filter Env Amt",-1,1,0.5,L)
add("AmpA","ampA","Amp Attack",0.001,10,0.01,LOG)
add("AmpD","ampD","Amp Decay",0.001,10,0.2,LOG)
add("AmpS","ampS","Amp Sustain",0,1,0.8,L)
add("AmpR","ampR","Amp Release",0.001,10,0.3,LOG)
add("AmpCurve","ampCurve","Amp Curve",0,3,3,I)
add("FiltA","filtA","Filter Attack",0.001,10,0.01,LOG)
add("FiltD","filtD","Filter Decay",0.001,10,0.3,LOG)
add("FiltS","filtS","Filter Sustain",0,1,0.4,L)
add("FiltR","filtR","Filter Release",0.001,10,0.5,LOG)
add("FiltCurve","filtCurve","Filter Curve",0,3,3,I)
# --- Mode-specific ---
add("CrossMod","crossMod","Cross Mod",0,1,0,L)
add("RingMod","ringMod","Ring Mod",0,1,0,L)
add("HardSync","hardSync","Hard Sync",0,1,0,B)
add("FMAmount","fmAmount","FM Amount",0,1,0,L)
add("PmFenvOscA","pmFenvOscA","PM FEnv-OscA",0,1,0,L)
add("PmFenvFilt","pmFenvFilt","PM FEnv-Filt",0,1,0,L)
add("PmOscBOscA","pmOscBOscA","PM OscB-OscA",0,1,0,L)
add("PmOscBPWM","pmOscBPWM","PM OscB-PW",0,1,0,L)
add("ShRate","shRate","S&H Rate",0.1,50,5.0,LOG)
add("CosmosChorus","cosmosChorus","Chorus Mode",0,3,3,I)
# --- LFOs ---
add("Lfo1Rate","lfo1Rate","LFO 1 Rate",0.01,50,1.0,LOG)
add("Lfo1Shape","lfo1Shape","LFO 1 Shape",0,4,0,I)
add("Lfo1Fade","lfo1Fade","LFO 1 Fade",0,5,0,L)
add("Lfo1Sync","lfo1Sync","LFO 1 Sync",0,1,0,B)
add("Lfo2Rate","lfo2Rate","LFO 2 Rate",0.01,50,0.5,LOG)
add("Lfo2Shape","lfo2Shape","LFO 2 Shape",0,4,0,I)
add("Lfo2Fade","lfo2Fade","LFO 2 Fade",0,5,0,L)
add("Lfo2Sync","lfo2Sync","LFO 2 Sync",0,1,0,B)
# --- Unison / porta / velocity ---
add("UnisonVoices","unisonVoices","Unison Voices",1,8,1,I)
add("UnisonDetune","unisonDetune","Unison Detune",0,50,10,L)
add("UnisonSpread","unisonSpread","Unison Spread",0,1,1.0,L)
add("PortaTime","portaTime","Portamento",0,2,0,L)
add("Legato","legato","Legato",0,1,0,B)
add("GlideMode","glideMode","Glide Mode",0,1,0,I)
add("VelSens","velSens","Velocity Sens",0,1,0.7,L)
add("VelCurve","velCurve","Vel Curve",0,3,0,I)
add("PbRange","pbRange","PB Range",1,24,2,I)
# --- Arp ---
add("ArpOn","arpOn","Arp On",0,1,0,B)
add("ArpMode","arpMode","Arp Mode",0,6,0,I)
add("ArpOctave","arpOctave","Arp Octave",1,4,1,I)
add("ArpRate","arpRate","Arp Rate",0,13,3,I)
add("ArpGate","arpGate","Arp Gate",0.01,1,0.5,L)
add("ArpSwing","arpSwing","Arp Swing",0,1,0,L)
add("ArpLatch","arpLatch","Arp Latch",0,1,0,B)
add("ArpVelMode","arpVelMode","Arp Vel Mode",0,2,0,I)
add("ArpFixedVel","arpFixedVel","Arp Fixed Vel",1,127,100,I)
for n in range(16):
    add(f"ArpStep{n}",f"arpStep{n}",f"Arp Step {n+1}",0,1,1,B)
# --- FX ---
add("DriveOn","driveOn","Drive On",0,1,0,B)
add("DriveType","driveType","Drive Type",0,2,0,I)
add("DriveAmt","driveAmt","Drive Amount",0,1,0.3,L)
add("DriveMix","driveMix","Drive Mix",0,1,1.0,L)
add("ChorusOn","chorusOn","Chorus On",0,1,0,B)
add("ChorusRate","chorusRate","Chorus Rate",0.1,10,0.8,LOG)
add("ChorusDepth","chorusDepth","Chorus Depth",0,1,0.5,L)
add("ChorusMix","chorusMix","Chorus Mix",0,1,0.5,L)
add("DelayOn","delayOn","Delay On",0,1,0,B)
add("DelaySync","delaySync","Delay Sync",0,1,1,B)
add("DelayTime","delayTime","Delay Time",1,2000,500,LOG)
add("DelayDiv","delayDiv","Delay Division",0,13,3,I)
add("DelayFB","delayFB","Delay Feedback",0,0.95,0.3,L)
add("DelayMix","delayMix","Delay Mix",0,1,0.3,L)
add("DelayPP","delayPP","Delay Ping-Pong",0,1,0,B)
add("DelayTape","delayTape","Delay Tape",0,1,0,B)
add("ReverbOn","reverbOn","Reverb On",0,1,0,B)
add("ReverbSize","reverbSize","Reverb Size",0,1,0.5,L)
add("ReverbDecay","reverbDecay","Reverb Decay",0.1,20,2.0,LOG)
add("ReverbDamp","reverbDamp","Reverb Damping",0,1,0.3,L)
add("ReverbMix","reverbMix","Reverb Mix",0,1,0.2,L)
add("ReverbPD","reverbPD","Reverb Pre-Delay",0,200,20,L)
# --- Mod matrix ---
for n in range(8): add(f"ModSrc{n}",f"modSrc{n}",f"Mod {n+1} Source",0,10,0,I)
# Max = msynth::kNumModDests - 1 (static_assert in MultiSynthPlugin.cpp). 12 -> 14
# when LFO1 Depth / LFO2 Depth were appended (issue #303). Saved sessions and
# presets store the plain index and are unaffected; a VST3 automation lane stores
# the normalized value (index / max), so lanes recorded on these eight parameters
# before the change now land on index round(old * 14 / 12) (manual).
for n in range(8): add(f"ModDst{n}",f"modDst{n}",f"Mod {n+1} Dest",0,14,0,I)
for n in range(8): add(f"ModAmt{n}",f"modAmt{n}",f"Mod {n+1} Amount",-1,1,0,L)
# --- Prism (4-op FM) ---
add("PrismAlgo","prismAlgo","Prism Algorithm",0,7,4,I)
add("PrismFB","prismFB","Prism Feedback",0,1,0,L)
# Prism operator levels follow the DX27/DX100 output-level law: 0.75 dB per
# 1/99 of the range, 74.25 dB in all, and a full-level modulator reaches four
# turns of phase deviation (FMEngine.hpp, updateGain). Until 2026-11 levels
# followed a 48 dB law with eight turns at full. A level written for that
# engine goes through one of two helpers, which keep the gain it had at full
# velocity on middle C (0 stays off):
#   from48()  a carrier: the same volume.
#   mod48()   an operator that modulates in its preset's algorithm: the same
#             depth, so 6.02 dB (the halved full-scale depth) higher on the law.
# In these algorithms no carrier also modulates, so each level is one or the
# other. The default levels assume the default algorithm (index 4), where
# operators 2 and 4 modulate and 1 and 3 are carriers.
def from48(x):
    return 0.0 if x <= 0.0 else 1.0 - (1.0 - x) * (48.0 / 74.25)
def mod48(x):
    y = 0.0 if x <= 0.0 else from48(x) + 20.0 * math.log10(2.0) / 74.25
    if y > 1.0:
        sys.exit(f"gen_params: modulator level {x} cannot keep its depth (needs {y:.4f} > 1)")
    return y
opdef_level = {1:1.0,2:mod48(0.5),3:from48(0.8),4:mod48(0.5)}
# Operator attacks follow the DX27/DX100's curve, an exponential in dB that is
# fast out of silence (FMEngine.hpp, FMRateEnvelope); until 2026-11 they were a
# squared ramp. A preset's old attack time goes through atk48(), which stretches
# it by 1.30: the factor at which the new curve follows the old ramp's rise most
# closely over its audible part (above -30 dB: mean -0.07 dB, rms 0.93 dB; keeping
# the half-amplitude moment instead, x 1.39, runs 0.85 dB quieter on average).
# The 1 ms minimum stays put.
def atk48(t):
    return t if t <= 0.001 else t * 1.3007
# Operator sustain follows the DX27/DX100's sustain level, 3.01 dB per 1/15 of
# the range and silence at 0 (FMEngine.hpp, FMRateEnvelope); until 2026-11 it
# was a linear amplitude in 1/63 steps. sus48() gives the knob value with the
# old level exactly (the new knob is continuous).
def sus48(x):
    x = round(x * 63.0) / 63.0          # the old engine's own 1/63 steps
    return 0.0 if x <= 0.0 else max(1e-4, 1.0 + 20.0 * math.log10(x) / 45.15)
# Operator decays now fall in a straight line in dB and stop at the sustain
# level, as on the DX27/DX100; until 2026-11 they approached it exponentially,
# slowing as they neared it, so above a sustain of zero a decay time meant
# something else. dec48(d, s) gives the decay time whose line follows the old
# curve most closely in dB (least squares over the part more than 0.3 dB above
# the sustain level, between d/2 and min(10 s, 40 d)); with sustain 0 the two
# are the same.
#
# The minimum is solved exactly, not searched for. The line max(sdb, -60 t u),
# u = 1/time, is linear in u while the same points sit above the floor, so the
# error is a quadratic in u on each stretch between two points' kinks and its
# minimum there is closed-form; the smallest of those is the answer. The
# golden-section search this replaced had two faults. It ended on comparisons
# of a flat error that rounding decides, so its last ~9 digits followed the
# platform's libm and the Python version (3.12 changed sum() to compensated
# summation), and the header came out different on macOS and Linux. And with a
# kink at every point the error has more than one minimum, so it could stop on
# a local one: the default (2.224 s, best fit 2.209 s) and Brass Machine's
# Op 2 Decay (1.645 s, best fit 1.635 s). Both land on the same step of the
# envelope's time grid either way, so no sound changed. Here a last-bit
# difference in libm moves the answer by a few ulps, far below the float32 the
# header stores. Plain running sums, not sum(), for the same reason.
def dec48(d, s):
    if not 0.0 < d <= 10.0:
        sys.exit(f"gen_params: dec48 converts the old decay times (at most 10 s), not {d}")
    s = round(s * 63.0) / 63.0
    if s <= 0.0:
        return d
    sdb = 20.0 * math.log10(s)
    pts = []
    for i in range(2000):
        t = 8.0 * d * i / 1999.0
        o = 20.0 * math.log10(s + (1.0 - s) * math.exp(-6.90775528 * t / d))
        if o > sdb + 0.3:
            pts.append((t, o))
    n = len(pts)
    c = -sdb / 60.0                     # a point is on the line while t u < c
    u_lo, u_hi = 1.0 / min(10.0, d * 40.0), 2.0 / d
    # floor[k]: the error of points k.. sitting on the sustain level
    floor = [0.0] * (n + 1)
    for i in range(n - 1, -1, -1):
        floor[i] = floor[i + 1] + (sdb - pts[i][1]) ** 2
    best_e, best_u = math.inf, u_lo
    stt = sto = soo = 0.0
    for k in range(1, n + 1):           # points 0..k-1 on the line
        t, o = pts[k - 1]
        stt += t * t
        sto += t * o
        soo += o * o
        lo = max(u_lo, c / pts[k][0] if k < n else 0.0)
        hi = min(u_hi, c / t if t > 0.0 else math.inf)
        if lo > hi:
            continue
        u = min(max(-sto / (60.0 * stt) if stt > 0.0 else lo, lo), hi)
        e = 3600.0 * stt * u * u + 120.0 * sto * u + soo + floor[k]
        if e < best_e:
            best_e, best_u = e, u
    return min(10.0, 1.0 / best_u)
# Key Scale follows the DX27/DX100's key level scaling (FMEngine.hpp, updateGain):
# |Key Scale| x 5.96 dB of attenuation at middle C, doubling every octave up
# (mirrored toward the low keys for a positive Key Scale). Until 2026-11 it
# multiplied the level before the level law. A preset's old Key Scale goes
# through ks48(), the new Key Scale with the old level drop from C4 to C6 (C4
# to C2 for a positive one), and its level through ksfix(), which adds back
# the new law's attenuation at C4; together they keep middle C and that
# drop. The old law's boost on the other side of C4 has no counterpart.
KS_DB_C4 = 0.0602 * 99.0
def ks48(x, k):
    if k == 0:
        return 0.0
    old = lambda n: 48.0 * (min(1.0, x * 2.0 ** (k * (n - 60) / 24.0)) - 1.0)
    if k < 0:
        return (old(84) - old(60)) / (3.0 * KS_DB_C4)
    return -(old(36) - old(60)) / (3.0 * KS_DB_C4)
def ksfix(level, ks):
    y = level + abs(ks) * KS_DB_C4 / 74.25
    if y > 1.0:
        sys.exit(f"gen_params: level {level} cannot absorb Key Scale {ks} (needs {y:.4f} > 1)")
    return y
# Feedback: operator 4's loop taps its output after level and envelope, with
# the DX27/DX100's measured step depths (FMEngine.hpp, kFeedbackTurns), so the
# old fixed depths do not carry over. Solid Bass (was 0.15, step 1) and Brass
# Machine (was 0.6, step 4) use step 7, whose depth at operator 4's full output
# comes closest to the old one (+1.7 dB and -4.1 dB); it now fades with that
# operator's envelope.
for op in (1,2,3,4):
    add(f"Op{op}Ratio",f"op{op}Ratio",f"Op {op} Ratio",0.25,16,1.0,LOG)
    add(f"Op{op}Fine",f"op{op}Fine",f"Op {op} Fine",-100,100,0,L)
    add(f"Op{op}Level",f"op{op}Level",f"Op {op} Level",0,1,opdef_level[op],L)
    add(f"Op{op}Vel",f"op{op}Vel",f"Op {op} Vel Sens",0,1,0,L)
    add(f"Op{op}KeyScale",f"op{op}KeyScale",f"Op {op} Key Scale",-1,1,0,L)
    add(f"Op{op}A",f"op{op}A",f"Op {op} Attack",0.001,10,atk48(0.005),LOG)
    add(f"Op{op}D",f"op{op}D",f"Op {op} Decay",0.001,100,dec48(0.4, 0.7),LOG)
    add(f"Op{op}S",f"op{op}S",f"Op {op} Sustain",0,1,sus48(0.7),L)
    add(f"Op{op}R",f"op{op}R",f"Op {op} Release",0.001,100,0.4,LOG)
# --- Acid ---
add("AcidAccentAmt","acidAccentAmt","Acid Accent",0,1,0.7,L)
add("AcidSlideTime","acidSlideTime","Acid Slide",10,200,60,LOG)
for n in range(16): add(f"SeqPitch{n}",f"seqPitch{n}",f"Seq Pitch {n+1}",-24,24,0,I)
for n in range(16): add(f"SeqAccent{n}",f"seqAccent{n}",f"Seq Accent {n+1}",0,1,0,B)
for n in range(16): add(f"SeqSlide{n}",f"seqSlide{n}",f"Seq Slide {n+1}",0,1,0,B)
# --- Appended after the 1.0.0-rc param freeze -------------------------------
# APPEND ONLY, never insert; and never RENAME a symbol. The DAF index IS the
# core msynth::Param index — host automation lanes and sessions key on it.
# .scpreset files key on the SYMBOL instead. A parameter added next to its
# topical siblings would renumber everything
# after it. Grouping lives in the UI, not in the index.
add("ArpAccentPattern","arpAccentPattern","Arp Accent Pattern",0,3,0,I)
add("PmFenvPWM","pmFenvPWM","PM FEnv-PW",0,1,0,L)
add("PmOscBFilt","pmOscBFilt","PM OscB-Filt",0,1,0,L)
add("ModFilterModel","modFilterModel","Modular Filter",0,1,0,I)
add("ModOsc2Osc1","modOsc2Osc1","Mod Osc2-Osc1",0,1,0,L)
add("ModOsc3Filter","modOsc3Filter","Mod Osc3-Filter",0,1,0,L)
# Oracle Wheel Mod (issue #303; core/WheelMod.hpp). Off by default: every
# destination switch defaults to 0, so existing presets and sessions render
# bit-identically. The rate range IS the measured exponential knob law (a LOG
# parameter, 0.03 Hz to 26.2 Hz); the other defaults are the reference panel's.
add("WheelLfoRate","wheelLfoRate","Wheel LFO Rate",0.03,26.2,0.8866,LOG)
add("WheelLfoSaw","wheelLfoSaw","Wheel LFO Saw",0,1,0,B)
add("WheelLfoTri","wheelLfoTri","Wheel LFO Triangle",0,1,1,B)
add("WheelLfoSquare","wheelLfoSquare","Wheel LFO Square",0,1,0,B)
add("WheelSourceMix","wheelSourceMix","Wheel Source Mix",0,1,0,L)
add("WheelNoiseSH","wheelNoiseSH","Wheel Noise/S&H",0,1,0,I)
add("WheelLowerLimit","wheelLowerLimit","Wheel Lower Limit",0,1,0,L)
add("WheelUpperLimit","wheelUpperLimit","Wheel Upper Limit",0,1,1,L)
add("WheelFreqA","wheelFreqA","Wheel Freq A",0,1,0,B)
add("WheelFreqB","wheelFreqB","Wheel Freq B",0,1,0,B)
add("WheelPwA","wheelPwA","Wheel PW A",0,1,0,B)
add("WheelPwB","wheelPwB","Wheel PW B",0,1,0,B)
add("WheelFilter","wheelFilter","Wheel Filter",0,1,0,B)
# Prism second decay and envelope rate scaling (issue #400), appended so every
# earlier index keeps its value. Decay 2's top, 100 s, is Hold (the sustain
# level holds, as before); Rate Scale 0..3 follows the DX27 / DX100's rate
# scaling, against middle C. Core: MultiSynthDSP.hpp, pOp1D2.
for op in range(1, 5):
    add(f"Op{op}D2",f"op{op}D2",f"Op {op} Decay 2",0.001,100,100,LOG)
    add(f"Op{op}RateScale",f"op{op}RateScale",f"Op {op} Rate Scale",0,3,0,I)

NCORE = len(P)
sym2enum = {p[1]: "kParam"+p[0] for p in P}

# ---------------- Parameter units (DAF Parameter::unit) ----------------------
# Emitted as a plain `const char*` per param and copied into Parameter::unit in
# initParameter(). Purely additive display metadata: it changes no index, no
# symbol, no range, and no host automation mapping.
#
# ONLY units whose numeric range is literally in that unit are listed. In
# particular there is deliberately NO "%" anywhere: every level/depth/mix
# parameter here has range 0..1, not 0..100, so tagging it "%" would make hosts
# render 0.8 as "0.8 %". Unit-less is correct for those. Dimensionless ratios
# (op ratios, resonance, gate, swing, width) are likewise left blank.
UNIT_S  = "s"    # seconds
UNIT_MS = "ms"   # milliseconds
UNIT_HZ = "Hz"
UNIT_DB = "dB"
UNIT_CT = "ct"   # cents
UNIT_ST = "st"   # semitones

UNITS = {}
def _units(unit, *syms):
    for s in syms:
        UNITS[s] = unit

_units(UNIT_DB, "masterVol")
_units(UNIT_HZ, "filterCutoff", "filterHP", "shRate", "lfo1Rate", "lfo2Rate",
       "chorusRate", "wheelLfoRate")
_units(UNIT_CT, "masterTune", "osc1Detune", "osc2Detune", "unisonDetune")
_units(UNIT_ST, "osc2Semi", "pbRange")
_units(UNIT_MS, "delayTime", "reverbPD", "acidSlideTime")
_units(UNIT_S,  "ampA", "ampD", "ampR", "filtA", "filtD", "filtR",
       "lfo1Fade", "lfo2Fade", "portaTime", "reverbDecay")
for _op in (1, 2, 3, 4):
    _units(UNIT_CT, f"op{_op}Fine")
    _units(UNIT_S,  f"op{_op}A", f"op{_op}D", f"op{_op}R", f"op{_op}D2")
for _n in range(16):
    _units(UNIT_ST, f"seqPitch{_n}")

# Every unit must name a real parameter — a typo would silently label nothing.
_unknown = sorted(set(UNITS) - {p[1] for p in P})
assert not _unknown, f"UNITS names non-existent params: {_unknown}"

# ---------------- Factory presets (from JUCE applyFactoryPreset) --------------
# Baseline applied to every preset (mirrors the reset block at the top of the
# JUCE switch). loadProgram resets all params to default, applies this, then the
# per-preset overrides. Oversampling forced to 1 (2x) here.
BASE = {
 # masterVol raised -6 -> 0 dB in Phase 5: the FourPoleOTA unity-gain fix removed
 # a spurious ~8-15 dB of passband gain, so the whole fleet needed level restored
 # to a usable instrument output (hottest preset still peaks below -1 dBFS).
 "masterVol":0, "osc1Wave":0, "osc2Wave":0, "osc1Detune":0, "osc2Detune":7,
 "osc1Level":0.8, "osc2Level":0.6, "noiseLevel":0, "filterCutoff":8000,
 "filterRes":0.3, "filterEnvAmt":0.5, "ampA":0.01, "ampD":0.2, "ampS":0.8,
 "ampR":0.3, "crossMod":0, "ringMod":0, "pmFenvOscA":0, "pmFenvFilt":0,
 "pmOscBOscA":0, "pmOscBPWM":0, "arpOn":0, "driveOn":0, "chorusOn":0,
 "delayOn":0, "reverbOn":0, "unisonVoices":1, "portaTime":0, "analogAmt":0.2,
 "vintage":0, "oversampling":1,
}

# The earlier Oracle Poly-Mod re-voice scaled filter-envelope -> filter by
# 4/14.3 to preserve the old cutoff depth. Stab Machine and Regal Brass also
# retain their fitted envelope -> pitch amounts, scaled by 34.7/33.908138
# after the pitch law was measured again. The brass and bell rows described
# below were voiced separately across the keyboard.
# Mono and Modular no longer count the noise in the mixer's level sum (#303,
# 2026-11-17), so noise stops turning the oscillators down. The four presets
# whose levels added up past 1 with their noise (Sub Thunder, Alien Transmission,
# Noise Sweep, Modular English Horn) had every level, noise included, divided by
# that old sum: the mix is then exactly what the old division made, and below 1,
# so nothing divides it now. They render as before to within float rounding.
PRESETS = [
 ("Neon Nights", {"masterVol":-1.5,"mode":0,"osc1Wave":0,"osc2Wave":4,"osc2Detune":8,"filterCutoff":2500,"filterRes":0.15,"filterEnvAmt":0.3,"ampA":0.4,"ampD":0.6,"ampS":0.85,"ampR":1.8,"cosmosChorus":3,"analogAmt":0.3,"reverbOn":1,"reverbSize":0.6,"reverbMix":0.2}),
 ("Glass Highway", {"masterVol":12.8,"mode":0,"filterCutoff":5000,"filterRes":0.35,"ampA":0.005,"ampD":0.25,"ampS":0.5,"ampR":0.6,"cosmosChorus":1,"arpOn":1,"arpRate":3,"arpGate":0.6,"delayOn":1,"delayMix":0.2}),
 ("Velvet Fog", {"masterVol":0.3,"mode":0,"filterCutoff":1200,"filterRes":0.25,"filterEnvAmt":0.15,"ampA":1.0,"ampS":0.9,"ampR":2.5,"cosmosChorus":2,"vintage":0.5}),
 ("Sunset Strip", {"mode":0,"osc2Detune":15,"filterCutoff":3500,"ampA":0.25,"ampS":0.8,"ampR":1.5,"cosmosChorus":3,"analogAmt":0.35}),
 ("Crystal Rain", {"masterVol":10.8,"mode":0,"filterCutoff":6000,"filterRes":0.2,"ampA":0.003,"ampD":0.12,"ampS":0.2,"ampR":0.35,"cosmosChorus":3,"arpOn":1,"arpRate":4,"arpGate":0.3,"delayOn":1,"delayMix":0.25,"reverbOn":1,"reverbMix":0.2}),
 # Detuned saws, a little mixer noise, and a room tail behind the horn swell.
 ("Brass Section", {"mode":1,"masterVol":6.3,"stereoWidth":0.22,"analogAmt":0.13,"velSens":0.4,
   "osc1Detune":7,"osc1Level":0.7,"osc2Detune":-9,"osc2Level":0.23,"noiseLevel":0.18,
   "filterCutoff":2565,"filterRes":0.27,"filterEnvAmt":1,
   "ampA":0.148,"ampD":0.976,"ampS":0.31,"ampR":1.03,
   "filtA":0.32,"filtD":0.12,"filtS":0.3,
   "reverbOn":1,"reverbDecay":4.5,"reverbMix":0.31,
   "modSrc0":7,"modDst0":5,"modAmt0":1,"modSrc1":7,"modDst1":5,"modAmt1":1}),
 ("Wooden Keys", {"masterVol":4.1,"mode":1,"filterCutoff":3000,"filterRes":0.2,"ampA":0.005,"ampD":0.4,"ampS":0.5,"ampR":0.3}),
 # A low square modulates the triangle. The long amplitude release and a
 # quiet fixed-time delay keep the inharmonic partials ringing after key-up.
 # Key tracking trims amplitude as the filter opens in the upper register.
 ("Poly Mod Bells", {"mode":1,"masterVol":11.5,"stereoWidth":0,"analogAmt":0.13,"velSens":0,
   "osc1Wave":2,"osc2Wave":1,"osc2Semi":-12,"osc2Detune":0,"osc2Level":0.3253,
   "filterCutoff":290,"filterRes":0.0207,"filterEnvAmt":0,"pmFenvOscA":0.1044,"pmOscBOscA":0.596,
   "ampA":0.006,"ampD":0.09,"ampS":0.62,"ampR":9,"ampCurve":0,
   "filtA":0.001,"filtS":1,"filtR":10,
   "delayOn":1,"delaySync":0,"delayTime":233,"delayFB":0.91,"delayMix":0.12,
   "modSrc0":7,"modDst0":5,"modAmt0":1,"modSrc1":7,"modDst1":5,"modAmt1":0.375,
   "modSrc2":7,"modDst2":7,"modAmt2":-0.55}),
 ("Dark Oracle", {"masterVol":2.9,"mode":1,"filterCutoff":1200,"filterRes":0.5,"filterEnvAmt":0.3,"ampA":0.5,"ampS":0.85,"ampR":1.5,"vintage":0.3,"pmFenvFilt":0.05594406}),
 ("Stab Machine", {"masterVol":6.7,"mode":1,"filterCutoff":4000,"filterEnvAmt":0.6,"ampA":0.001,"ampD":0.15,"ampS":0.0,"ampR":0.1,"pmFenvOscA":0.562844235}),
 ("Pulsing Darkness", {"masterVol":12.1,"mode":2,"osc1Wave":4,"osc2Wave":0,"osc2Semi":-12,"filterCutoff":600,"filterRes":0.55,"filterEnvAmt":0.5,"subLevel":0.9,"filtA":0.001,"filtD":0.15,"filtS":0.05,"arpOn":1,"arpRate":3,"vintage":0.3}),
 ("Acid Squelch", {"masterVol":14.7,"mode":2,"filterCutoff":400,"filterRes":0.85,"filterEnvAmt":0.95,"filtA":0.001,"filtD":0.15,"filtS":0.0,"subLevel":0.7,"portaTime":0.08,"ampD":0.3,"ampS":0.0,"ampR":0.15}),
 ("Screaming Lead", {"masterVol":-1.2,"mode":2,"filterCutoff":6000,"filterRes":0.3,"ampA":0.005,"portaTime":0.1,"driveOn":1,"driveAmt":0.4,"delayOn":1,"delayMix":0.2}),
 ("Sub Thunder", {"masterVol":10.9,"mode":2,"osc1Wave":3,"osc2Wave":3,"filterCutoff":400,"osc1Level":0.330578512,"osc2Level":0.247933884,"subLevel":0.41322314,"noiseLevel":0.00826446281}),
 ("Sync Sweep", {"masterVol":10.9,"mode":2,"osc2Semi":7,"filterCutoff":800,"filterRes":0.6,"filterEnvAmt":0.7,"filtA":0.01,"filtD":0.4,"ringMod":0.3,"delayOn":1,"delayMix":0.2}),
 # Flagship (design-doc mandate): Oracle mode, two slightly detuned saws, arp
 # Up-Down 1/8 with latch so holding a Cmaj-ish chord instantly plays the famous
 # 80s sci-fi title-sequence arpeggio. Filter ~2 kHz with slight env, a subtle
 # slow LFO on cutoff, and a touch of tempo-synced delay. Verified at 132 BPM
 # (grid dev / pitch cycle / centroid).
 ("Upside Down", {"masterVol":8.8,"mode":1,"osc1Wave":0,"osc2Wave":0,"osc1Detune":0,"osc2Detune":8,"osc2Level":0.8,
   "filterCutoff":2000,"filterRes":0.3,"filterEnvAmt":0.25,"filtA":0.01,"filtD":0.3,"filtS":0.5,
   "ampA":0.005,"ampD":0.3,"ampS":0.7,"ampR":0.4,
   "arpOn":1,"arpMode":2,"arpRate":3,"arpOctave":1,"arpGate":0.6,"arpLatch":1,
   "lfo1Rate":0.3,"lfo1Shape":0,"modSrc0":1,"modDst0":5,"modAmt0":0.15,
   "delayOn":1,"delaySync":1,"delayDiv":3,"delayMix":0.2,"delayFB":0.3,
   "reverbOn":1,"reverbSize":0.5,"reverbMix":0.15,"stereoWidth":0.7}),
 ("Sci-Fi Computer", {"masterVol":13.0,"mode":3,"fmAmount":0.6,"hardSync":1,"osc2Semi":19,"filterCutoff":3000,"ampA":0.001,"ampD":0.08,"ampS":0.0,"arpOn":1,"arpRate":4,"arpMode":4,"arpGate":0.2}),
 ("Horror Drone", {"masterVol":9.7,"mode":3,"ringMod":0.6,"fmAmount":0.2,"osc2Semi":-5,"osc3Wave":0,"osc3Level":0.5,"filterCutoff":800,"filterRes":0.65,"ampA":3.0,"ampS":1.0,"ampR":5.0,"reverbOn":1,"reverbDecay":10.0,"reverbMix":0.5,"vintage":0.7}),
 ("Voltage Ghost", {"masterVol":7.1,"mode":3,"fmAmount":0.8,"hardSync":1,"osc2Semi":12,"filterCutoff":2500,"filterEnvAmt":0.4,"filtA":0.5,"filtD":2.0,"filtS":0.2,"ampA":0.8,"ampR":3.0,"vintage":0.5,"delayOn":1,"delayTape":1,"delayMix":0.3}),
 ("Retro Sequence", {"masterVol":8.2,"mode":3,"filterCutoff":2500,"filterEnvAmt":0.7,"arpOn":1,"arpRate":3,"arpGate":0.5,"delayOn":1,"delayTape":1}),
 ("Midnight Drive", {"masterVol":-4.9,"mode":0,"filterCutoff":1800,"filterRes":0.35,"filterEnvAmt":0.25,"ampA":0.5,"ampS":0.85,"ampR":2.0,"cosmosChorus":2,"driveOn":1,"driveAmt":0.3,"delayOn":1,"delayTape":1,"delayMix":0.25,"stereoWidth":0.7}),
 ("Starfield", {"masterVol":8.6,"mode":0,"filterCutoff":7000,"ampA":0.003,"ampD":0.2,"ampS":0.3,"ampR":0.5,"cosmosChorus":3,"arpOn":1,"arpMode":4,"arpRate":4,"arpOctave":3,"arpGate":0.4,"delayOn":1,"delayMix":0.3,"reverbOn":1,"reverbMix":0.25,"stereoWidth":0.8}),
 # Single saw one octave down. Key tracking opens the filter and trims
 # amplitude so upper-register chords keep their headroom.
 ("Poly Brass", {"mode":1,"masterVol":8.5,"stereoWidth":0,"analogAmt":0.13,"velSens":0,
   "osc1Level":0,"osc2Semi":-12,"osc2Detune":0,"osc2Level":0.9,
   "filterCutoff":351,"filterRes":0.38,"filterEnvAmt":0.8,
   "ampA":0.0175,"ampD":0.105,"ampS":0.52,"ampR":0.133,
   "filtA":0.25,"filtD":0.35,"filtS":0,"filtR":0.16,
   "modSrc0":7,"modDst0":5,"modAmt0":1,"modSrc1":7,"modDst1":5,"modAmt1":1,
   "modSrc2":7,"modDst2":7,"modAmt2":-0.6}),
 # The triangle rings an octave up. The sustained filter envelope and its
 # matrix pitch route tune the quieter pulse two octaves above the triangle.
 ("Glass Bells", {"mode":1,"masterVol":4.4,"stereoWidth":0.59,"analogAmt":0.13,"velSens":0.4,
   "osc1Wave":4,"osc1PW":0.62,"osc1Detune":12,"osc1Level":0.157,"osc2Wave":2,"osc2Semi":12,"osc2Detune":2,"osc2Level":0.9,
   "filterCutoff":3200,"filterRes":0.47,"filterEnvAmt":0,"pmFenvOscA":1,
   "ampA":0.001,"ampD":1.53,"ampS":0.065,"ampR":1.22,
   "filtA":0.001,"filtS":1,"filtR":10,
   "reverbOn":1,"reverbSize":0.78,"reverbDecay":6,"reverbMix":0.17,
   "modSrc0":7,"modDst0":5,"modAmt0":1,"modSrc1":7,"modDst1":5,"modAmt1":1,"modSrc2":3,"modDst2":1,"modAmt2":1}),
 ("Acid Machine", {"masterVol":15.4,"mode":2,"osc1Wave":0,"filterCutoff":350,"filterRes":0.9,"filterEnvAmt":0.95,"filtA":0.001,"filtD":0.12,"filtS":0.0,"ampD":0.2,"ampS":0.0,"portaTime":0.06,"glideMode":1,"arpOn":1,"arpRate":3,"arpGate":0.6}),
 ("Thunder Sub", {"masterVol":12.0,"mode":2,"osc1Wave":3,"osc2Wave":3,"osc2Semi":-12,"filterCutoff":200,"filterRes":0.15,"subLevel":1.0,"subWave":1,"ampA":0.01,"ampS":1.0,"velCurve":1}),
 ("Voltage Seq", {"masterVol":9.7,"mode":3,"filterCutoff":2000,"filterRes":0.5,"filterEnvAmt":0.4,"shRate":8.0,"arpOn":1,"arpRate":3,"arpMode":0,"arpOctave":2,"delayOn":1,"delayMix":0.2}),
 ("Alien Transmission", {"masterVol":6.5,"mode":3,"fmAmount":0.7,"ringMod":0.4,"hardSync":1,"osc2Semi":7,"filterCutoff":4000,"filterRes":0.4,"ampA":0.3,"ampR":2.0,"osc1Level":0.4,"osc2Level":0.3,"osc3Level":0.25,"noiseLevel":0.05,"reverbOn":1,"reverbDecay":5.0,"reverbMix":0.4,"vintage":0.4}),
 ("Warm Keys", {"masterVol":9.9,"mode":0,"osc1Wave":2,"osc2Wave":3,"filterCutoff":4000,"ampA":0.005,"ampD":0.8,"ampS":0.3,"ampR":0.5,"cosmosChorus":1,"velCurve":2}),
 ("Analog Strings", {"masterVol":-1.6,"mode":1,"filterCutoff":3000,"filterRes":0.15,"ampA":0.8,"ampS":0.9,"ampR":1.5,"unisonVoices":4,"unisonDetune":12,"stereoWidth":0.7,"chorusOn":1,"chorusMix":0.3}),
 ("Wobble Bass", {"masterVol":8.8,"mode":2,"filterCutoff":600,"filterRes":0.6,"subLevel":0.8,"lfo1Rate":4.0,"lfo1Shape":0,"modSrc0":1,"modDst0":5,"modAmt0":0.6}),
 ("Tape Lead", {"masterVol":0.7,"mode":2,"filterCutoff":5000,"filterRes":0.25,"ampA":0.005,"portaTime":0.12,"glideMode":1,"driveOn":1,"driveAmt":0.25,"delayOn":1,"delayTape":1,"delayMix":0.25,"delayFB":0.4}),
 ("Drone Machine", {"masterVol":10.1,"mode":3,"fmAmount":0.3,"osc3Level":0.6,"osc3Wave":3,"filterCutoff":1200,"filterRes":0.55,"ampA":3.0,"ampS":1.0,"ampR":5.0,"lfo1Rate":0.1,"modSrc0":1,"modDst0":5,"modAmt0":0.4,"reverbOn":1,"reverbDecay":12.0,"reverbMix":0.5,"vintage":0.6}),
 ("Arp Factory", {"masterVol":9.2,"mode":0,"filterCutoff":4500,"filterRes":0.3,"filterEnvAmt":0.4,"filtD":0.2,"ampA":0.003,"ampD":0.15,"ampS":0.4,"ampR":0.3,"cosmosChorus":3,"arpOn":1,"arpMode":2,"arpRate":4,"arpOctave":3,"arpGate":0.5,"arpSwing":0.3,"delayOn":1,"delayMix":0.2}),
 ("Fat Fifth", {"masterVol":-1.6,"mode":1,"osc2Semi":7,"filterCutoff":3500,"filterRes":0.2,"ampA":0.01,"ampS":0.8,"unisonVoices":3,"unisonDetune":8,"stereoWidth":0.65}),
 ("Noise Sweep", {"masterVol":14.1,"mode":3,"osc1Level":0.0,"osc2Level":0.0,"osc3Level":0.333333333,"noiseLevel":0.666666667,"filterCutoff":500,"filterRes":0.7,"filterEnvAmt":0.8,"filtA":0.5,"filtD":2.0,"filtS":0.1,"ampA":0.3,"ampS":0.7,"ampR":3.0,"reverbOn":1,"reverbMix":0.4}),
 ("Init Cosmos", {"masterVol":0.1,"mode":0,"cosmosChorus":3}),
 ("Init Oracle", {"masterVol":0.5,"mode":1}),
 ("Init Mono", {"masterVol":5.1,"mode":2}),
 ("Init Modular", {"masterVol":4.4,"mode":3}),

 # ===================== Phase 5 new preset banks =========================
 # --- Prism (mode 4, 4-op FM) --------------------------------------------
 # Glass Keys: dual-stack tine e-piano (algo 4). Stack A = body (op2 1:1 -> op1
 # carrier); Stack B = tine (op4 ratio 14 fast-decay -> op3 carrier), keyScale
 # rolls the tine off up the keyboard. Percussive amp with a little sustain.
 ("Glass Keys", {"masterVol":5.0,"mode":4,"prismAlgo":4,"prismFB":0,
   "op1Ratio":1,"op1Level":from48(1.0),"op1A":0.001,"op1D":dec48(2.0, 0.4),"op1S":sus48(0.4),"op1R":0.4,
   "op2Ratio":1,"op2Level":mod48(0.45),"op2A":0.001,"op2D":dec48(1.2, 0.2),"op2S":sus48(0.2),"op2R":0.3,
   "op3Ratio":1,"op3Level":from48(0.85),"op3A":0.001,"op3D":dec48(1.5, 0.25),"op3S":sus48(0.25),"op3R":0.4,
   "op4Ratio":14,"op4Level":ksfix(mod48(0.7), ks48(0.7, -0.35)),"op4KeyScale":ks48(0.7, -0.35),"op4A":0.001,"op4D":dec48(0.12, 0.0),"op4S":sus48(0.0),"op4R":0.2,
   "filterCutoff":14000,"filterEnvAmt":0,"ampA":0.001,"ampD":2.5,"ampS":0.25,"ampR":0.5,
   "chorusOn":1,"chorusMix":0.25}),
 # Solid Bass: serial FM bass (algo 0, 4->3->2->1) with a touch of op4 feedback
 # for grit; filter-env pluck for punch.
 # Solid Bass: Master Volume -1.3 (was -1.0). The straight-line operator decay
 # (2026-11) lifts its first 50 ms 0.38 dB, which put the audit chord's peak at
 # -0.9 dBFS, past the -1 dBFS ceiling.
 ("Solid Bass", {"masterVol":1.4,"mode":4,"prismAlgo":0,"prismFB":1.0,
   "op1Ratio":1,"op1Level":from48(1.0),"op1A":0.001,"op1D":dec48(0.5, 0.6),"op1S":sus48(0.6),"op1R":0.2,
   "op2Ratio":1,"op2Level":mod48(0.5),"op2A":0.001,"op2D":dec48(0.3, 0.2),"op2S":sus48(0.2),"op2R":0.2,
   "op3Ratio":1,"op3Level":mod48(0.35),"op3A":0.001,"op3D":dec48(0.25, 0.1),"op3S":sus48(0.1),"op3R":0.2,
   "op4Ratio":2,"op4Level":mod48(0.4),"op4A":0.001,"op4D":dec48(0.2, 0.1),"op4S":sus48(0.1),"op4R":0.2,
   "filterCutoff":4000,"filterRes":0.2,"filterEnvAmt":0.35,"filtA":0.001,"filtD":0.3,"filtS":0.2,
   "ampA":0.001,"ampD":0.6,"ampS":0.6,"ampR":0.2}),
 # Crystal Bells: additive (algo 8) inharmonic partials with staggered decays
 # (higher partials die first) + long release; open filter, reverb tail.
 # Crystal Bells: ratios 2.765625, 5.40625 and 8.9375 are the 1/64 steps its
 # 2.76, 5.4 and 8.93 played at; the engine now resolves ratios to 1/4096.
 ("Crystal Bells", {"masterVol":10.9,"mode":4,"prismAlgo":7,"prismFB":0,
   "op1Ratio":1.0,"op1Level":from48(1.0),"op1A":0.001,"op1D":dec48(4.0, 0.0),"op1S":sus48(0.0),"op1R":3.0,
   "op2Ratio":2.765625,"op2Level":from48(0.5),"op2A":0.001,"op2D":dec48(3.0, 0.0),"op2S":sus48(0.0),"op2R":2.5,
   "op3Ratio":5.40625,"op3Level":from48(0.3),"op3A":0.001,"op3D":dec48(2.0, 0.0),"op3S":sus48(0.0),"op3R":1.8,
   "op4Ratio":8.9375,"op4Level":from48(0.15),"op4A":0.001,"op4D":dec48(1.2, 0.0),"op4S":sus48(0.0),"op4R":1.2,
   "filterCutoff":16000,"filterEnvAmt":0,"ampA":0.001,"ampD":5.0,"ampS":0.0,"ampR":3.5,
   "reverbOn":1,"reverbSize":0.7,"reverbDecay":4.0,"reverbMix":0.3}),
 # Brass Machine: serial FM (algo 0) with strong op4 self-feedback growl and a
 # brass-style attack swell on the amp + modulator.
 ("Brass Machine", {"masterVol":-4.6,"mode":4,"prismAlgo":0,"prismFB":1.0,
   "op1Ratio":1,"op1Level":from48(1.0),"op1A":atk48(0.06),"op1D":dec48(0.3, 0.8),"op1S":sus48(0.8),"op1R":0.3,
   "op2Ratio":1,"op2Level":mod48(0.7),"op2A":atk48(0.1),"op2D":dec48(0.4, 0.6),"op2S":sus48(0.6),"op2R":0.3,
   "op3Ratio":1,"op3Level":mod48(0.5),"op3A":atk48(0.08),"op3D":dec48(0.4, 0.5),"op3S":sus48(0.5),"op3R":0.3,
   "op4Ratio":1,"op4Level":mod48(0.6),"op4A":atk48(0.05),"op4D":dec48(0.3, 0.5),"op4S":sus48(0.5),"op4R":0.3,
   "filterCutoff":5000,"filterRes":0.1,"filterEnvAmt":0.4,"filtA":0.05,"filtD":0.4,"filtS":0.5,
   "ampA":0.08,"ampD":0.3,"ampS":0.75,"ampR":0.3}),

 # --- Acid (mode 5, diode-ladder box + 16-step sequencer) ----------------
 # Silver Squelch: classic saw line, high res, mid env; accents on 1/5/9/13,
 # slides into steps 4 and 8. A rolling one-bar 16th pattern.
 ("Silver Squelch", {"masterVol":10.0,"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":500,"filterRes":0.82,
   "filterEnvAmt":0.6,"ampD":0.3,"ampS":0.0,"acidAccentAmt":0.8,"acidSlideTime":60,
   "arpRate":4,"arpGate":0.55,
   "seqPitch2":12,"seqPitch5":3,"seqPitch7":12,"seqPitch10":7,"seqPitch12":5,"seqPitch14":-5,
   "seqAccent0":1,"seqAccent4":1,"seqAccent8":1,"seqAccent12":1,
   "seqSlide3":1,"seqSlide7":1}),
 # Rubber Bass: square wave, low res, tight fast decay -> round rubbery bounce.
 ("Rubber Bass", {"masterVol":6.2,"mode":5,"arpOn":1,"osc1Wave":1,"filterCutoff":420,"filterRes":0.3,
   "filterEnvAmt":0.5,"ampD":0.14,"ampS":0.0,"acidAccentAmt":0.5,"acidSlideTime":50,
   "arpRate":4,"arpGate":0.45,
   "seqPitch4":12,"seqPitch6":7,"seqPitch11":3,"seqPitch12":-5,
   "seqAccent0":1,"seqAccent6":1,"seqAccent10":1}),
 # Night Crawler: slow 1/8 dark pattern, heavy slide (long glide) for a
 # creeping, portamento-drenched bassline.
 ("Night Crawler", {"masterVol":9.9,"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":320,"filterRes":0.62,
   "filterEnvAmt":0.4,"ampD":0.45,"ampS":0.0,"acidAccentAmt":0.6,"acidSlideTime":150,
   "arpRate":3,"arpGate":0.75,
   "seqPitch2":-2,"seqPitch4":3,"seqPitch6":-5,"seqPitch9":5,"seqPitch11":-7,"seqPitch13":2,
   "seqSlide2":1,"seqSlide4":1,"seqSlide6":1,"seqSlide9":1,"seqSlide11":1,"seqSlide13":1,
   "seqAccent0":1,"seqAccent8":1}),
 # Screamer: near-self-oscillating res 0.95, maxed accent, drive on -> the
 # aggressive overdriven scream.
 ("Screamer", {"masterVol":0.3,"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":600,"filterRes":0.95,
   "filterEnvAmt":0.7,"ampD":0.25,"ampS":0.0,"acidAccentAmt":1.0,"acidSlideTime":55,
   "driveOn":1,"driveAmt":0.55,"driveType":0,"arpRate":4,"arpGate":0.5,
   "seqPitch3":12,"seqPitch7":12,"seqPitch9":7,"seqPitch11":10,"seqPitch15":-12,
   "seqAccent0":1,"seqAccent2":1,"seqAccent5":1,"seqAccent8":1,"seqAccent11":1,"seqAccent14":1,
   "seqSlide7":1,"seqSlide11":1}),

 # --- Flagship-quality patches across the remaining modes -----------------
 # Aurora Drift: huge Cosmos DCO pad, dual chorus (Both), slow swell, wide.
 ("Aurora Drift", {"masterVol":-3.8,"mode":0,"osc1Wave":0,"osc2Wave":4,"osc2Detune":12,"osc2PW":0.4,"subLevel":0.35,
   "filterCutoff":3000,"filterRes":0.2,"filterEnvAmt":0.2,"ampA":1.2,"ampD":1.0,"ampS":0.9,"ampR":3.0,
   "cosmosChorus":3,"reverbOn":1,"reverbSize":0.8,"reverbDecay":4.0,"reverbMix":0.3,"stereoWidth":0.9}),
 # Regal Brass: Oracle self-osc filter + poly-mod brass with 2-voice unison.
 ("Regal Brass", {"masterVol":-0.3,"mode":1,"osc2Semi":0,"filterCutoff":1800,"filterRes":0.35,"filterEnvAmt":0.6,
   "filtA":0.04,"filtD":0.3,"filtS":0.4,"ampA":0.02,"ampD":0.3,"ampS":0.8,"ampR":0.35,
   "pmFenvFilt":0.0979021,"pmFenvOscA":0.255838289,"unisonVoices":2,"unisonDetune":6,"stereoWidth":0.6}),
 # Siren Lead: Mono screaming sync lead (hard sync + osc2 up an octave), driven,
 # slap of delay, portamento glide.
 ("Siren Lead", {"masterVol":-0.4,"mode":2,"hardSync":1,"osc2Semi":12,"filterCutoff":4000,"filterRes":0.5,
   "filterEnvAmt":0.6,"filtA":0.01,"filtD":0.3,"filtS":0.3,"ampA":0.005,"ampS":0.8,"ampR":0.3,
   "portaTime":0.05,"driveOn":1,"driveAmt":0.4,"delayOn":1,"delaySync":1,"delayDiv":3,"delayMix":0.25,"delayFB":0.35}),
 # Nebula Static: Modular sci-fi texture — S&H modulating cutoff, third osc,
 # slow evolving pad, auto spring reverb + hall.
 ("Nebula Static", {"masterVol":10.9,"mode":3,"osc3Wave":3,"osc3Level":0.5,"shRate":6.0,
   "modSrc0":10,"modDst0":5,"modAmt0":0.6,"modSrc1":10,"modDst1":6,"modAmt1":0.3,
   "filterCutoff":1500,"filterRes":0.5,"filterEnvAmt":0.3,"ampA":0.6,"ampD":1.0,"ampS":0.8,"ampR":3.0,
   "reverbOn":1,"reverbSize":0.6,"reverbDecay":6.0,"reverbMix":0.35,"vintage":0.3}),
 # Glass Cathedral: Prism dual-stack pad — mellow FM, long amp swell, chorus +
 # big reverb for a glassy cathedral wash.
 ("Glass Cathedral", {"masterVol":1.4,"mode":4,"prismAlgo":4,"prismFB":0,
   "op1Ratio":1,"op1Level":from48(0.9),"op1A":atk48(0.8),"op1D":dec48(2.0, 0.7),"op1S":sus48(0.7),"op1R":2.5,
   "op2Ratio":2,"op2Level":mod48(0.35),"op2A":atk48(1.0),"op2D":dec48(2.0, 0.6),"op2S":sus48(0.6),"op2R":2.5,
   "op3Ratio":1,"op3Level":from48(0.7),"op3A":atk48(0.8),"op3D":dec48(2.0, 0.7),"op3S":sus48(0.7),"op3R":2.5,
   "op4Ratio":3,"op4Level":ksfix(mod48(0.25), ks48(0.25, -0.2)),"op4KeyScale":ks48(0.25, -0.2),"op4A":atk48(1.0),"op4D":dec48(2.0, 0.6),"op4S":sus48(0.6),"op4R":2.5,
   "filterCutoff":9000,"filterEnvAmt":0,"ampA":1.0,"ampD":2.0,"ampS":0.85,"ampR":3.5,
   "chorusOn":1,"chorusMix":0.3,"reverbOn":1,"reverbSize":0.85,"reverbDecay":6.0,"reverbMix":0.35,"stereoWidth":0.85}),
 # Neon Sequence: Acid + ping-pong tempo-delay groove; resonant saw line with a
 # pattern that steps around the root, delay widening the space.
 ("Neon Sequence", {"masterVol":11.0,"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":520,"filterRes":0.75,
   "filterEnvAmt":0.6,"ampD":0.3,"ampS":0.0,"acidAccentAmt":0.7,"acidSlideTime":60,
   "arpRate":4,"arpGate":0.5,
   "seqPitch1":12,"seqPitch4":7,"seqPitch6":3,"seqPitch9":10,"seqPitch12":5,"seqPitch14":-5,
   "seqAccent0":1,"seqAccent4":1,"seqAccent8":1,"seqAccent12":1,"seqSlide6":1,"seqSlide14":1,
   "delayOn":1,"delaySync":1,"delayDiv":3,"delayMix":0.3,"delayFB":0.4,"delayPP":1}),

 # --- Contributed patches (GitHub issue #303, by ZULU MATRIX) -------------
 # Credited in PRESET_AUTHORS below; the author is GitHub user 70MM13.
 # Converted from the author's .scpreset files: every row is a parameter whose
 # saved value differs from default + baseline, with the file's own decimal
 # text, so loadProgram() lands on the same float32 state the user-preset loader
 # produces from those files (knob residue such as 2.38418579e-07 included),
 # except where a later engine change needed a re-voice (noted per preset).
 # Operator levels are the general exception: like every preset's, they go
 # through from48() or mod48() (see opdef_level), so these keep their sound
 # under the DX level law and now differ from what the author's files load to.
 # Settings that are inert in the preset's engine (poly-mod on Modular, the
 # Prism operators on Modular, sync and poly-mod on Prism) are kept, so a mode
 # switch after loading behaves as it does after loading the author's file. All
 # three keep the 4x oversampling they were saved with (the bank default is 2x).
 # Oracle Trumpet: hard-synced brass with filter-envelope and Osc B poly-mod on
 # the filter; the mod wheel brings in LFO 1 vibrato (Mod Whl -> LFO1 Depth).
 # Its Poly-Mod filter amounts were rescaled to the measured laws (Voice.hpp,
 # ORACLE POLY-MOD LAWS): 1 x 4/14.3 and 0.625 x 3/3.1 give the same depths.
 # The author's file routes LFO 1 to Osc1 Pitch, for the sync master it then
 # was. Oracle's sync now runs from osc 2 to osc 1 (osc 2 is the master), and the
 # two oscillators are set identically, so the route moves to Osc2 Pitch: the
 # vibrato stays on the master and the sync on the steady oscillator.
 ("Oracle Trumpet", {"mode":1,"masterVol":0.2,"stereoWidth":0.49999997,"oversampling":2,
   "analogAmt":1,"osc1Level":1,"osc2Detune":0,"osc2Level":1,
   "filterCutoff":355.03714,"filterRes":0.37500006,"filterEnvAmt":1,
   "ampA":0.0479766354,"ampD":0.0626789629,"ampS":0.555000186,"ampR":0.121357851,
   "filtA":0.0480906405,"filtD":0.198207721,"filtS":0.404999912,"filtR":0.100000091,"filtCurve":2,
   "hardSync":1,"pmFenvFilt":0.2797203,
   "lfo1Rate":4.80566263,"lfo1Shape":1,"lfo1Fade":0.675000012,
   "lfo2Rate":2.0504818,"lfo2Shape":1,"lfo2Fade":1.32499993,
   "unisonVoices":2,"unisonDetune":1.25,"unisonSpread":0.324999988,
   "driveOn":1,"driveAmt":0.345000267,
   "chorusOn":1,"chorusRate":1.63339043,"chorusDepth":0.0200000592,"chorusMix":0.265000045,
   "delayOn":1,"delayFB":0.0569999926,"delayMix":0.0549999997,"delayPP":1,"delayTape":1,
   "reverbOn":1,"reverbSize":0,"reverbDecay":0.100000001,"reverbDamp":0,"reverbMix":0.0449999981,"reverbPD":0,
   "modSrc0":4,"modSrc1":1,"modDst0":13,"modDst1":2,"modAmt0":1,"modAmt1":1,
   "pmOscBFilt":0.6048388}),
 # Modular English Horn: triangle oscillators with sync, FM and ring mod into the
 # driven ladder, key-tracked cutoff, and mod-wheel vibrato from LFO 2.
 ("Modular English Horn", {"mode":3,"masterVol":9.9,"stereoWidth":0.780000031,"oversampling":2,
   "analogAmt":1,"osc1Wave":2,"osc1Level":0.326264271,"osc2Wave":2,"osc2Detune":2.38418579e-07,"osc2Level":0.326264271,
   "osc3Wave":3,"osc3Level":0.326264271,"noiseLevel":0.0212071866,
   "filterCutoff":532.145996,"filterRes":0.295000017,"filterEnvAmt":0.650000095,
   "ampA":0.115087986,"ampD":0.12506099,"ampS":0.295000166,"ampR":0.127077311,
   "filtA":1.65958917,"filtD":0.86520803,"filtS":0.350000083,"filtR":0.691831708,
   "ringMod":0.274999976,"hardSync":1,"fmAmount":0.155000314,"pmFenvFilt":1,"shRate":0.100000001,
   "lfo1Rate":3.56688786,"lfo1Shape":1,"lfo1Fade":0.675000012,
   "lfo2Rate":7.05028391,"lfo2Fade":1.32499993,
   "unisonDetune":1,"unisonSpread":0.225000009,
   "driveOn":1,"driveAmt":1,
   "chorusOn":1,"chorusRate":1.63339043,"chorusDepth":0.0200000592,"chorusMix":0.265000045,
   "delayFB":0.0569999926,"delayMix":0.0549999997,"delayPP":1,"delayTape":1,
   "reverbOn":1,"reverbSize":0,"reverbDecay":0.100000001,"reverbDamp":0,"reverbMix":0.0549999997,"reverbPD":53,
   "modSrc0":1,"modSrc1":7,"modSrc2":4,"modSrc3":2,"modSrc4":8,
   "modDst0":1,"modDst1":5,"modDst2":14,"modDst3":1,"modDst4":10,
   "modAmt0":0.020000089,"modAmt1":1,"modAmt2":0.50000006,"modAmt3":0.329999954,"modAmt4":-0.0899999291,
   "prismAlgo":5,"op1Ratio":1.5,"op1A":atk48(0.0125892526),"op1D":dec48(0.00954992697, 0.26000008),"op1S":sus48(0.26000008),"op1R":0.954992712,
   "op2Ratio":0.75,"op2Level":from48(0.440000057),"op2A":atk48(0.00478630001),"op4Ratio":5,
   "pmOscBFilt":0.62500006,"modOsc3Filter":0.0749998987}),
 # Prism Talking Keys: FM keys through a resonant, envelope-swept filter with
 # key-tracked cutoff; the mod wheel brings in LFO 1 vibrato.
 ("Prism Talking Keys", {"mode":4,"masterVol":17.2,"stereoWidth":1,"oversampling":2,
   "analogAmt":1,"osc1Level":1,"osc2Detune":0,"osc2Level":1,
   "filterCutoff":176.210617,"filterRes":0.734999955,"filterEnvAmt":1,
   "ampA":0.0796214044,"ampD":0.0626789629,"ampS":0.555000186,"ampR":0.201403946,"ampCurve":2,
   "filtA":0.0480906405,"filtD":0.198207721,"filtS":0.404999912,"filtR":0.100000091,"filtCurve":2,
   "hardSync":1,"pmFenvFilt":1,
   "lfo1Rate":4.22930241,"lfo1Fade":0.675000012,
   "lfo2Rate":2.0504818,"lfo2Shape":1,"lfo2Fade":1.32499993,
   "unisonVoices":2,"unisonDetune":2,"unisonSpread":0.35499993,
   "driveOn":1,"driveAmt":1,
   "chorusOn":1,"chorusRate":1.63339043,"chorusDepth":0.0200000592,"chorusMix":0.265000045,
   "delayOn":1,"delayFB":0.0569999926,"delayMix":0.0549999997,"delayPP":1,"delayTape":1,
   "reverbOn":1,"reverbSize":0,"reverbDecay":0.100000001,"reverbDamp":0,"reverbMix":0.0449999981,"reverbPD":0,
   "modSrc0":1,"modSrc1":7,"modSrc2":4,"modDst0":1,"modDst1":5,"modDst2":13,
   "modAmt0":0.7299999,"modAmt1":1,"modAmt2":0.379999995,
   "prismAlgo":5,"op1Ratio":1.5,"op1A":atk48(0.0125892526),"op1D":dec48(0.00954992697, 0.26000008),"op1S":sus48(0.26000008),"op1R":0.954992712,
   "op2Ratio":0.75,"op2Level":from48(0.440000057),"op2A":atk48(0.00478630001),"op4Ratio":5,
   "pmOscBFilt":0.62500006}),
]

# Preset credits: factory preset name -> author, emitted as FactoryPreset::author
# (nullptr for an uncredited preset). The preset menu lists a credited preset under
# its author's heading in its mode's column; the browser shows the author. Keyed
# by name so a credit cannot shift onto another preset if the bank is reordered;
# a name that is not in PRESETS stops generation, so a rename cannot drop one.
PRESET_AUTHORS = {
    "Oracle Trumpet":       "ZULU MATRIX",
    "Modular English Horn": "ZULU MATRIX",
    "Prism Talking Keys":   "ZULU MATRIX",
}
_unknown = sorted(set(PRESET_AUTHORS) - {nm for nm, _ in PRESETS})
if _unknown:
    sys.exit(f"gen_params: PRESET_AUTHORS names no such preset: {_unknown}")
for _nm, _by in PRESET_AUTHORS.items():
    if not _by or '"' in _by or "\\" in _by:
        sys.exit(f"gen_params: author for {_nm!r} must be non-empty, without quotes "
                 "or backslashes (it is emitted as a C string literal)")

def f32(v):
    """v rounded to the nearest float32, the value the C++ compiler stores."""
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]

def fl(v):
    """Format a number as a C++ float literal: the shortest decimal that is the
    same float32 (always has a decimal point or an exponent). Printing the
    double instead carried digits the float cannot hold, and the helpers above
    compute some values through libm, whose last bit differs between platforms:
    the same float printed differently on macOS and Linux and --check failed."""
    f = f32(v)
    if f == int(f):
        return f"{int(f)}.0f"
    for digits in range(1, 10):          # 9 significant digits always round-trip
        s = f"{f:.{digits}g}"
        if f32(s) == f:
            return f"{s}f"
    raise AssertionError(f"no 9-digit form of {f!r}")

out = io.StringIO()
w = out.write
w("// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).\n")
w("// Third-party components in the built plugins (DAF - ISC; Dear ImGui - MIT; and\n")
w("// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.\n")
w("//\n")
w("// MultiSynthParams.hpp - GENERATED by tools gen_params.py; do not hand-edit the\n")
w("// tables. One X-macro list (MSYNTH_PARAMS) drives the DAF param enum, the\n")
w("// initParameter() metadata, and the engine setter dispatch. The DAF parameter\n")
w("// index equals the core msynth::Param index 1:1 (verified by static_assert in\n")
w("// MultiSynthPlugin.cpp), so forwarding is a single dsp.setParameter(index,value).\n")
w("//\n")
w("// KIND: LIN linear float, LOG logarithmic float (min>0), INT integer choice,\n")
w("// BOOL on/off. Two output params (peak L/R for meters) follow the core params.\n")
w("// UNIT: display-only suffix (\"Hz\", \"dB\", \"ms\", \"s\", \"st\", \"ct\"), \"\" when the\n")
w("// value is dimensionless. Additive metadata only - it does not affect indices,\n")
w("// symbols, ranges or host automation mapping.\n\n")
w("#pragma once\n\n")
w("// X(EnumSuffix, symbol, Name, min, max, default, KIND, unit)\n")
w("#define MSYNTH_PARAMS(X) \\\n")
for i,p in enumerate(P):
    suf,sym,name,mn,mx,df,kind = p
    end = " \\\n" if i < len(P)-1 else "\n"
    unit = UNITS.get(sym, "")
    w(f'    X({suf}, "{sym}", "{name}", {fl(mn)}, {fl(mx)}, {fl(df)}, {kind}, "{unit}"){end}')
w("\n")
w("// Parameter index enum (kParam<Suffix>), matching the core order 1:1.\n")
w("enum ParamId\n{\n")
w("#define X(suf, sym, name, mn, mx, df, kind, unit) kParam##suf,\n")
w("    MSYNTH_PARAMS(X)\n")
w("#undef X\n")
w(f"    kNumCoreParams,                 // == msynth::kNumParams ({NCORE})\n")
w("    kParamOutLevelL = kNumCoreParams, // output: peak L (meter fallback)\n")
w("    kParamOutLevelR,                  // output: peak R\n")
w("    kParamCount\n};\n\n")

# Param metadata table for initParameter.
w("enum ParamKind { PK_LIN = 0, PK_LOG, PK_INT, PK_BOOL };\n")
w("struct ParamDef { const char* symbol; const char* name; float min, max, def; int kind; const char* unit; };\n")
w("static constexpr ParamDef kParamDefs[kNumCoreParams] =\n{\n")
w("#define LIN  PK_LIN\n#define LOG  PK_LOG\n#define INT  PK_INT\n#define BOOL PK_BOOL\n")
w("#define X(suf, sym, name, mn, mx, df, kind, unit) { sym, name, mn, mx, df, kind, unit },\n")
w("    MSYNTH_PARAMS(X)\n")
w("#undef X\n#undef LIN\n#undef LOG\n#undef INT\n#undef BOOL\n};\n\n")

# Factory presets.
w("// ---------------------------------------------------------------------------\n")
w(f"// Factory presets ({len(PRESETS)}) - static override table. loadProgram() resets every\n")
w("// param to its default, applies the shared baseline, then the per-preset\n")
w("// overrides. Presets ship at 2x oversampling (baseline oversampling=1; the\n")
w("// pitch bug is fixed so 2x is correct) unless their rows say otherwise: the\n")
w("// three contributed presets at the end keep the 4x they were made with.\n")
w("// Phase 5 re-voiced the original 40 (unity-gain filter fix + level/intent)\n")
w("// and appended new preset banks.\n")
w("// ---------------------------------------------------------------------------\n")
w("struct PresetRow { int index; float value; };\n\n")
def rows(name, d):
    items = ", ".join(f"{{ {sym2enum[k]}, {fl(v)} }}" for k,v in d.items())
    return f"static constexpr PresetRow {name}[] = {{ {items} }};\n"
w(rows("kPresetBaseline", BASE))
w("\n")
for i,(nm,d) in enumerate(PRESETS):
    var = f"kP{i:02d}"
    w(rows(var, d))
w("\n// author: who made the preset, for the UI's credit; nullptr when uncredited.\n")
w("struct FactoryPreset { const char* name; const PresetRow* rows; int nRows; const char* author; };\n")
w("static constexpr FactoryPreset kFactoryPresets[] =\n{\n")
for i,(nm,d) in enumerate(PRESETS):
    var = f"kP{i:02d}"
    by = f'"{PRESET_AUTHORS[nm]}"' if nm in PRESET_AUTHORS else "nullptr"
    w(f'    {{ "{nm}", {var}, (int)(sizeof({var})/sizeof(PresetRow)), {by} }},\n')
w("};\n")
w("static constexpr int kNumFactoryPresets = (int)(sizeof(kFactoryPresets)/sizeof(kFactoryPresets[0]));\n")
w("static constexpr int kBaselineRows = (int)(sizeof(kPresetBaseline)/sizeof(PresetRow));\n")

_out_path = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "MultiSynthParams.hpp"))
_text = out.getvalue()

_ap = argparse.ArgumentParser(description="Generate (or verify) MultiSynthParams.hpp")
_ap.add_argument("--check", action="store_true",
                 help="do not write; byte-compare the committed header against a "
                      "fresh generation and exit 1 on any drift")
_args = _ap.parse_args()

if _args.check:
    # Byte-exact comparison: the header is a build input, so ANY difference
    # (including a hand-edit that happens to be semantically equivalent) is a
    # failure — the generator is the single source of truth.
    try:
        with open(_out_path, "r", newline="") as f:
            _have = f.read()
    except OSError as e:
        print(f"gen_params --check: cannot read {_out_path}: {e}", file=sys.stderr)
        sys.exit(1)
    if _have == _text:
        print(f"gen_params --check: OK ({NCORE} params, {len(PRESETS)} presets, "
              f"{len(_text)} bytes)")
        sys.exit(0)
    print(f"gen_params --check: DRIFT — {_out_path} does not match gen_params.py "
          f"output ({len(_have)} bytes committed vs {len(_text)} generated).",
          file=sys.stderr)
    import difflib
    _diff = list(difflib.unified_diff(_have.splitlines(True), _text.splitlines(True),
                                      fromfile="committed", tofile="generated", n=1))
    sys.stderr.writelines(_diff[:60])
    if len(_diff) > 60:
        print(f"... ({len(_diff) - 60} more diff lines)", file=sys.stderr)
    print("Re-run tools/gen_params.py and commit the result.", file=sys.stderr)
    sys.exit(1)

with open(_out_path, "w", newline="") as f:
    f.write(_text)
print(f"core params = {NCORE}, presets = {len(PRESETS)}")
