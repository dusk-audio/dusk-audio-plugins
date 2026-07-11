#!/usr/bin/env python3
# Generates MultiSynthParams.hpp for the DPF shell. The param order MUST match
# the core msynth::Param enum exactly (indices are shared 1:1).
import io
import os

L, LOG, I, B = "LIN", "LOG", "INT", "BOOL"

# (EnumSuffix, symbol, "Name", min, max, default, kind)
P = []
def add(*a): P.append(a)

# --- Global ---
add("Mode","mode","Mode",0,5,0,I)
add("MasterTune","masterTune","Master Tune",-100,100,0,L)
add("MasterVol","masterVol","Master Volume",-60,6,0,L)
add("MasterPan","masterPan","Master Pan",-1,1,0,L)
add("StereoWidth","stereoWidth","Stereo Width",0,1,0.5,L)
add("Oversampling","oversampling","Oversampling",0,2,1,I)
add("AnalogAmt","analogAmt","Analog",0,1,0.2,L)
add("Vintage","vintage","Vintage",0,1,0,L)
# --- Oscillators ---
add("Osc1Wave","osc1Wave","Osc 1 Wave",0,4,0,I)
add("Osc1Detune","osc1Detune","Osc 1 Detune",-50,50,0,L)
add("Osc1PW","osc1PW","Osc 1 PW",0.05,0.95,0.5,L)
add("Osc1Level","osc1Level","Osc 1 Level",0,1,1.0,L)
add("Osc2Wave","osc2Wave","Osc 2 Wave",0,4,0,I)
add("Osc2Detune","osc2Detune","Osc 2 Detune",-50,50,7,L)
add("Osc2PW","osc2PW","Osc 2 PW",0.05,0.95,0.5,L)
add("Osc2Level","osc2Level","Osc 2 Level",0,1,0.8,L)
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
for n in range(8): add(f"ModDst{n}",f"modDst{n}",f"Mod {n+1} Dest",0,12,0,I)
for n in range(8): add(f"ModAmt{n}",f"modAmt{n}",f"Mod {n+1} Amount",-1,1,0,L)
# --- Prism (4-op FM) ---
add("PrismAlgo","prismAlgo","Prism Algorithm",0,7,4,I)
add("PrismFB","prismFB","Prism Feedback",0,1,0,L)
opdef_level = {1:1.0,2:0.5,3:0.8,4:0.5}
for op in (1,2,3,4):
    add(f"Op{op}Ratio",f"op{op}Ratio",f"Op {op} Ratio",0.25,16,1.0,LOG)
    add(f"Op{op}Fine",f"op{op}Fine",f"Op {op} Fine",-100,100,0,L)
    add(f"Op{op}Level",f"op{op}Level",f"Op {op} Level",0,1,opdef_level[op],L)
    add(f"Op{op}Vel",f"op{op}Vel",f"Op {op} Vel Sens",0,1,0,L)
    add(f"Op{op}KeyScale",f"op{op}KeyScale",f"Op {op} Key Scale",-1,1,0,L)
    add(f"Op{op}A",f"op{op}A",f"Op {op} Attack",0.001,10,0.005,LOG)
    add(f"Op{op}D",f"op{op}D",f"Op {op} Decay",0.001,10,0.4,LOG)
    add(f"Op{op}S",f"op{op}S",f"Op {op} Sustain",0,1,0.7,L)
    add(f"Op{op}R",f"op{op}R",f"Op {op} Release",0.001,10,0.4,LOG)
# --- Acid ---
add("AcidAccentAmt","acidAccentAmt","Acid Accent",0,1,0.7,L)
add("AcidSlideTime","acidSlideTime","Acid Slide",10,200,60,LOG)
for n in range(16): add(f"SeqPitch{n}",f"seqPitch{n}",f"Seq Pitch {n+1}",-24,24,0,I)
for n in range(16): add(f"SeqAccent{n}",f"seqAccent{n}",f"Seq Accent {n+1}",0,1,0,B)
for n in range(16): add(f"SeqSlide{n}",f"seqSlide{n}",f"Seq Slide {n+1}",0,1,0,B)

NCORE = len(P)
sym2enum = {p[1]: "kParam"+p[0] for p in P}
sym2def  = {p[1]: p[5] for p in P}

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

PRESETS = [
 ("Neon Nights", {"mode":0,"osc1Wave":0,"osc2Wave":4,"osc2Detune":8,"filterCutoff":2500,"filterRes":0.15,"filterEnvAmt":0.3,"ampA":0.4,"ampD":0.6,"ampS":0.85,"ampR":1.8,"cosmosChorus":3,"analogAmt":0.3,"reverbOn":1,"reverbSize":0.6,"reverbMix":0.2}),
 ("Glass Highway", {"mode":0,"filterCutoff":5000,"filterRes":0.35,"ampA":0.005,"ampD":0.25,"ampS":0.5,"ampR":0.6,"cosmosChorus":1,"arpOn":1,"arpRate":3,"arpGate":0.6,"delayOn":1,"delayMix":0.2}),
 ("Velvet Fog", {"mode":0,"filterCutoff":1200,"filterRes":0.25,"filterEnvAmt":0.15,"ampA":1.0,"ampS":0.9,"ampR":2.5,"cosmosChorus":2,"vintage":0.5}),
 ("Sunset Strip", {"mode":0,"osc2Detune":15,"filterCutoff":3500,"ampA":0.25,"ampS":0.8,"ampR":1.5,"cosmosChorus":3,"analogAmt":0.35}),
 ("Crystal Rain", {"mode":0,"filterCutoff":6000,"filterRes":0.2,"ampA":0.003,"ampD":0.12,"ampS":0.2,"ampR":0.35,"cosmosChorus":3,"arpOn":1,"arpRate":4,"arpGate":0.3,"delayOn":1,"delayMix":0.25,"reverbOn":1,"reverbMix":0.2}),
 ("Brass Section", {"mode":1,"filterCutoff":2000,"filterRes":0.4,"filterEnvAmt":0.7,"filtA":0.05,"filtD":0.3,"filtS":0.3,"ampA":0.01,"ampD":0.3,"ampS":0.7,"pmFenvFilt":0.3}),
 ("Wooden Keys", {"mode":1,"filterCutoff":3000,"filterRes":0.2,"ampA":0.005,"ampD":0.4,"ampS":0.5,"ampR":0.3}),
 ("Poly Mod Bells", {"mode":1,"osc2Wave":2,"osc2Semi":7,"pmOscBOscA":0.5,"pmFenvOscA":0.4,"filterCutoff":5000,"ampA":0.005,"ampD":1.0,"ampS":0.0,"ampR":1.5,"reverbOn":1,"reverbMix":0.35,"reverbDecay":3.0}),
 ("Dark Prophet", {"mode":1,"filterCutoff":1200,"filterRes":0.5,"filterEnvAmt":0.3,"ampA":0.5,"ampS":0.85,"ampR":1.5,"vintage":0.3,"pmFenvFilt":0.2}),
 ("Stab Machine", {"mode":1,"filterCutoff":4000,"filterEnvAmt":0.6,"ampA":0.001,"ampD":0.15,"ampS":0.0,"ampR":0.1,"pmFenvOscA":0.3}),
 ("Pulsing Darkness", {"mode":2,"osc1Wave":4,"osc2Wave":0,"osc2Semi":-12,"filterCutoff":600,"filterRes":0.55,"filterEnvAmt":0.5,"subLevel":0.9,"filtA":0.001,"filtD":0.15,"filtS":0.05,"arpOn":1,"arpRate":3,"vintage":0.3}),
 ("Acid Squelch", {"mode":2,"filterCutoff":400,"filterRes":0.85,"filterEnvAmt":0.95,"filtA":0.001,"filtD":0.15,"filtS":0.0,"subLevel":0.7,"portaTime":0.08,"ampD":0.3,"ampS":0.0,"ampR":0.15}),
 ("Screaming Lead", {"mode":2,"filterCutoff":6000,"filterRes":0.3,"ampA":0.005,"portaTime":0.1,"driveOn":1,"driveAmt":0.4,"delayOn":1,"delayMix":0.2}),
 ("Sub Thunder", {"mode":2,"osc1Wave":3,"osc2Wave":3,"filterCutoff":400,"subLevel":1.0,"noiseLevel":0.02}),
 ("Sync Sweep", {"mode":2,"osc2Semi":7,"filterCutoff":800,"filterRes":0.6,"filterEnvAmt":0.7,"filtA":0.01,"filtD":0.4,"ringMod":0.3,"delayOn":1,"delayMix":0.2}),
 # Flagship (design-doc mandate): Oracle mode, two slightly detuned saws, arp
 # Up-Down 1/8 with latch so holding a Cmaj-ish chord instantly plays the famous
 # 80s sci-fi title-sequence arpeggio. Filter ~2 kHz with slight env, a subtle
 # slow LFO on cutoff, and a touch of tempo-synced delay. Verified at 132 BPM in
 # docs/dpf-migration/09-multi-synth-presets.md (grid dev / pitch cycle / centroid).
 ("Upside Down", {"mode":1,"osc1Wave":0,"osc2Wave":0,"osc1Detune":0,"osc2Detune":8,"osc2Level":0.8,
   "filterCutoff":2000,"filterRes":0.3,"filterEnvAmt":0.25,"filtA":0.01,"filtD":0.3,"filtS":0.5,
   "ampA":0.005,"ampD":0.3,"ampS":0.7,"ampR":0.4,
   "arpOn":1,"arpMode":2,"arpRate":3,"arpOctave":1,"arpGate":0.6,"arpLatch":1,
   "lfo1Rate":0.3,"lfo1Shape":0,"modSrc0":1,"modDst0":5,"modAmt0":0.15,
   "delayOn":1,"delaySync":1,"delayDiv":3,"delayMix":0.2,"delayFB":0.3,
   "reverbOn":1,"reverbSize":0.5,"reverbMix":0.15,"stereoWidth":0.7}),
 ("Sci-Fi Computer", {"mode":3,"fmAmount":0.6,"hardSync":1,"osc2Semi":19,"filterCutoff":3000,"ampA":0.001,"ampD":0.08,"ampS":0.0,"arpOn":1,"arpRate":4,"arpMode":4,"arpGate":0.2}),
 ("Horror Drone", {"mode":3,"ringMod":0.6,"fmAmount":0.2,"osc2Semi":-5,"osc3Wave":0,"osc3Level":0.5,"filterCutoff":800,"filterRes":0.65,"ampA":3.0,"ampS":1.0,"ampR":5.0,"reverbOn":1,"reverbDecay":10.0,"reverbMix":0.5,"vintage":0.7}),
 ("Voltage Ghost", {"mode":3,"fmAmount":0.8,"hardSync":1,"osc2Semi":12,"filterCutoff":2500,"filterEnvAmt":0.4,"filtA":0.5,"filtD":2.0,"filtS":0.2,"ampA":0.8,"ampR":3.0,"vintage":0.5,"delayOn":1,"delayTape":1,"delayMix":0.3}),
 ("Retro Sequence", {"mode":3,"filterCutoff":2500,"filterEnvAmt":0.7,"arpOn":1,"arpRate":3,"arpGate":0.5,"delayOn":1,"delayTape":1}),
 ("Midnight Drive", {"mode":0,"filterCutoff":1800,"filterRes":0.35,"filterEnvAmt":0.25,"ampA":0.5,"ampS":0.85,"ampR":2.0,"cosmosChorus":2,"driveOn":1,"driveAmt":0.3,"delayOn":1,"delayTape":1,"delayMix":0.25,"stereoWidth":0.7}),
 ("Starfield", {"mode":0,"filterCutoff":7000,"ampA":0.003,"ampD":0.2,"ampS":0.3,"ampR":0.5,"cosmosChorus":3,"arpOn":1,"arpMode":4,"arpRate":4,"arpOctave":3,"arpGate":0.4,"delayOn":1,"delayMix":0.3,"reverbOn":1,"reverbMix":0.25,"stereoWidth":0.8}),
 ("Prophet Brass", {"mode":1,"filterCutoff":1500,"filterRes":0.3,"filterEnvAmt":0.6,"filtA":0.03,"filtD":0.25,"filtS":0.35,"ampA":0.01,"ampS":0.75,"pmFenvFilt":0.4,"pmFenvOscA":0.15,"velCurve":2}),
 ("Glass Bells", {"mode":1,"osc2Wave":2,"osc2Semi":19,"filterCutoff":6000,"ampA":0.001,"ampD":1.5,"ampS":0.0,"ampR":2.0,"pmOscBOscA":0.6,"pmFenvOscA":0.3,"reverbOn":1,"reverbDecay":4.0,"reverbMix":0.35}),
 ("Acid Machine", {"mode":2,"osc1Wave":0,"filterCutoff":350,"filterRes":0.9,"filterEnvAmt":0.95,"filtA":0.001,"filtD":0.12,"filtS":0.0,"ampD":0.2,"ampS":0.0,"portaTime":0.06,"glideMode":1,"arpOn":1,"arpRate":3,"arpGate":0.6}),
 ("Thunder Sub", {"mode":2,"osc1Wave":3,"osc2Wave":3,"osc2Semi":-12,"filterCutoff":200,"filterRes":0.15,"subLevel":1.0,"subWave":1,"ampA":0.01,"ampS":1.0,"velCurve":1}),
 ("Voltage Seq", {"mode":3,"filterCutoff":2000,"filterRes":0.5,"filterEnvAmt":0.4,"shRate":8.0,"arpOn":1,"arpRate":3,"arpMode":0,"arpOctave":2,"delayOn":1,"delayMix":0.2}),
 ("Alien Transmission", {"mode":3,"fmAmount":0.7,"ringMod":0.4,"hardSync":1,"osc2Semi":7,"filterCutoff":4000,"filterRes":0.4,"ampA":0.3,"ampR":2.0,"noiseLevel":0.1,"reverbOn":1,"reverbDecay":5.0,"reverbMix":0.4,"vintage":0.4}),
 ("Warm Keys", {"mode":0,"osc1Wave":2,"osc2Wave":3,"filterCutoff":4000,"ampA":0.005,"ampD":0.8,"ampS":0.3,"ampR":0.5,"cosmosChorus":1,"velCurve":2}),
 ("Analog Strings", {"mode":1,"filterCutoff":3000,"filterRes":0.15,"ampA":0.8,"ampS":0.9,"ampR":1.5,"unisonVoices":4,"unisonDetune":12,"stereoWidth":0.7,"chorusOn":1,"chorusMix":0.3}),
 ("Wobble Bass", {"mode":2,"filterCutoff":600,"filterRes":0.6,"subLevel":0.8,"lfo1Rate":4.0,"lfo1Shape":0,"modSrc0":1,"modDst0":5,"modAmt0":0.6}),
 ("Tape Lead", {"mode":2,"filterCutoff":5000,"filterRes":0.25,"ampA":0.005,"portaTime":0.12,"glideMode":1,"driveOn":1,"driveAmt":0.25,"delayOn":1,"delayTape":1,"delayMix":0.25,"delayFB":0.4}),
 ("Drone Machine", {"mode":3,"fmAmount":0.3,"osc3Level":0.6,"osc3Wave":3,"filterCutoff":1200,"filterRes":0.55,"ampA":3.0,"ampS":1.0,"ampR":5.0,"lfo1Rate":0.1,"modSrc0":1,"modDst0":5,"modAmt0":0.4,"reverbOn":1,"reverbDecay":12.0,"reverbMix":0.5,"vintage":0.6}),
 ("Arp Factory", {"mode":0,"filterCutoff":4500,"filterRes":0.3,"filterEnvAmt":0.4,"filtD":0.2,"ampA":0.003,"ampD":0.15,"ampS":0.4,"ampR":0.3,"cosmosChorus":3,"arpOn":1,"arpMode":2,"arpRate":4,"arpOctave":3,"arpGate":0.5,"arpSwing":0.3,"delayOn":1,"delayMix":0.2}),
 ("Fat Fifth", {"mode":1,"osc2Semi":7,"filterCutoff":3500,"filterRes":0.2,"ampA":0.01,"ampS":0.8,"unisonVoices":3,"unisonDetune":8,"stereoWidth":0.65}),
 ("Noise Sweep", {"mode":3,"osc1Level":0.0,"osc2Level":0.0,"noiseLevel":1.0,"filterCutoff":500,"filterRes":0.7,"filterEnvAmt":0.8,"filtA":0.5,"filtD":2.0,"filtS":0.1,"ampA":0.3,"ampS":0.7,"ampR":3.0,"reverbOn":1,"reverbMix":0.4}),
 ("Init Cosmos", {"mode":0,"cosmosChorus":3}),
 ("Init Oracle", {"mode":1}),
 ("Init Mono", {"mode":2}),
 ("Init Modular", {"mode":3}),

 # ===================== Phase 5 new preset banks =========================
 # --- Prism (mode 4, 4-op FM) --------------------------------------------
 # Glass Keys: dual-stack tine e-piano (algo 4). Stack A = body (op2 1:1 -> op1
 # carrier); Stack B = tine (op4 ratio 14 fast-decay -> op3 carrier), keyScale
 # rolls the tine off up the keyboard. Percussive amp with a little sustain.
 ("Glass Keys", {"mode":4,"prismAlgo":4,"prismFB":0,
   "op1Ratio":1,"op1Level":1.0,"op1A":0.001,"op1D":2.0,"op1S":0.4,"op1R":0.4,
   "op2Ratio":1,"op2Level":0.45,"op2A":0.001,"op2D":1.2,"op2S":0.2,"op2R":0.3,
   "op3Ratio":1,"op3Level":0.85,"op3A":0.001,"op3D":1.5,"op3S":0.25,"op3R":0.4,
   "op4Ratio":14,"op4Level":0.7,"op4KeyScale":-0.35,"op4A":0.001,"op4D":0.12,"op4S":0.0,"op4R":0.2,
   "filterCutoff":14000,"filterEnvAmt":0,"ampA":0.001,"ampD":2.5,"ampS":0.25,"ampR":0.5,
   "chorusOn":1,"chorusMix":0.25}),
 # Solid Bass: serial FM bass (algo 0, 4->3->2->1) with a touch of op4 feedback
 # for grit; filter-env pluck for punch.
 ("Solid Bass", {"mode":4,"prismAlgo":0,"prismFB":0.15,
   "op1Ratio":1,"op1Level":1.0,"op1A":0.001,"op1D":0.5,"op1S":0.6,"op1R":0.2,
   "op2Ratio":1,"op2Level":0.5,"op2A":0.001,"op2D":0.3,"op2S":0.2,"op2R":0.2,
   "op3Ratio":1,"op3Level":0.35,"op3A":0.001,"op3D":0.25,"op3S":0.1,"op3R":0.2,
   "op4Ratio":2,"op4Level":0.4,"op4A":0.001,"op4D":0.2,"op4S":0.1,"op4R":0.2,
   "filterCutoff":4000,"filterRes":0.2,"filterEnvAmt":0.35,"filtA":0.001,"filtD":0.3,"filtS":0.2,
   "ampA":0.001,"ampD":0.6,"ampS":0.6,"ampR":0.2}),
 # Crystal Bells: additive (algo 8) inharmonic partials with staggered decays
 # (higher partials die first) + long release; open filter, reverb tail.
 ("Crystal Bells", {"mode":4,"prismAlgo":7,"prismFB":0,
   "op1Ratio":1.0,"op1Level":1.0,"op1A":0.001,"op1D":4.0,"op1S":0.0,"op1R":3.0,
   "op2Ratio":2.76,"op2Level":0.5,"op2A":0.001,"op2D":3.0,"op2S":0.0,"op2R":2.5,
   "op3Ratio":5.4,"op3Level":0.3,"op3A":0.001,"op3D":2.0,"op3S":0.0,"op3R":1.8,
   "op4Ratio":8.93,"op4Level":0.15,"op4A":0.001,"op4D":1.2,"op4S":0.0,"op4R":1.2,
   "filterCutoff":16000,"filterEnvAmt":0,"ampA":0.001,"ampD":5.0,"ampS":0.0,"ampR":3.5,
   "reverbOn":1,"reverbSize":0.7,"reverbDecay":4.0,"reverbMix":0.3}),
 # Brass Machine: serial FM (algo 0) with strong op4 self-feedback growl and a
 # brass-style attack swell on the amp + modulator.
 ("Brass Machine", {"mode":4,"prismAlgo":0,"prismFB":0.6,
   "op1Ratio":1,"op1Level":1.0,"op1A":0.06,"op1D":0.3,"op1S":0.8,"op1R":0.3,
   "op2Ratio":1,"op2Level":0.7,"op2A":0.1,"op2D":0.4,"op2S":0.6,"op2R":0.3,
   "op3Ratio":1,"op3Level":0.5,"op3A":0.08,"op3D":0.4,"op3S":0.5,"op3R":0.3,
   "op4Ratio":1,"op4Level":0.6,"op4A":0.05,"op4D":0.3,"op4S":0.5,"op4R":0.3,
   "filterCutoff":5000,"filterRes":0.1,"filterEnvAmt":0.4,"filtA":0.05,"filtD":0.4,"filtS":0.5,
   "ampA":0.08,"ampD":0.3,"ampS":0.75,"ampR":0.3}),

 # --- Acid (mode 5, diode-ladder box + 16-step sequencer) ----------------
 # Silver Squelch: classic saw line, high res, mid env; accents on 1/5/9/13,
 # slides into steps 4 and 8. A rolling one-bar 16th pattern.
 ("Silver Squelch", {"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":500,"filterRes":0.82,
   "filterEnvAmt":0.6,"ampD":0.3,"ampS":0.0,"acidAccentAmt":0.8,"acidSlideTime":60,
   "arpRate":4,"arpGate":0.55,
   "seqPitch2":12,"seqPitch5":3,"seqPitch7":12,"seqPitch10":7,"seqPitch12":5,"seqPitch14":-5,
   "seqAccent0":1,"seqAccent4":1,"seqAccent8":1,"seqAccent12":1,
   "seqSlide3":1,"seqSlide7":1}),
 # Rubber Bass: square wave, low res, tight fast decay -> round rubbery bounce.
 ("Rubber Bass", {"mode":5,"arpOn":1,"osc1Wave":1,"filterCutoff":420,"filterRes":0.3,
   "filterEnvAmt":0.5,"ampD":0.14,"ampS":0.0,"acidAccentAmt":0.5,"acidSlideTime":50,
   "arpRate":4,"arpGate":0.45,
   "seqPitch4":12,"seqPitch6":7,"seqPitch11":3,"seqPitch12":-5,
   "seqAccent0":1,"seqAccent6":1,"seqAccent10":1}),
 # Night Crawler: slow 1/8 dark pattern, heavy slide (long glide) for a
 # creeping, portamento-drenched bassline.
 ("Night Crawler", {"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":320,"filterRes":0.62,
   "filterEnvAmt":0.4,"ampD":0.45,"ampS":0.0,"acidAccentAmt":0.6,"acidSlideTime":150,
   "arpRate":3,"arpGate":0.75,
   "seqPitch2":-2,"seqPitch4":3,"seqPitch6":-5,"seqPitch9":5,"seqPitch11":-7,"seqPitch13":2,
   "seqSlide2":1,"seqSlide4":1,"seqSlide6":1,"seqSlide9":1,"seqSlide11":1,"seqSlide13":1,
   "seqAccent0":1,"seqAccent8":1}),
 # Screamer: near-self-oscillating res 0.95, maxed accent, drive on -> the
 # aggressive overdriven scream.
 ("Screamer", {"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":600,"filterRes":0.95,
   "filterEnvAmt":0.7,"ampD":0.25,"ampS":0.0,"acidAccentAmt":1.0,"acidSlideTime":55,
   "driveOn":1,"driveAmt":0.55,"driveType":0,"arpRate":4,"arpGate":0.5,
   "seqPitch3":12,"seqPitch7":12,"seqPitch9":7,"seqPitch11":10,"seqPitch15":-12,
   "seqAccent0":1,"seqAccent2":1,"seqAccent5":1,"seqAccent8":1,"seqAccent11":1,"seqAccent14":1,
   "seqSlide7":1,"seqSlide11":1}),

 # --- Flagship-quality patches across the remaining modes -----------------
 # Aurora Drift: huge Cosmos DCO pad, dual chorus (Both), slow swell, wide.
 ("Aurora Drift", {"mode":0,"osc1Wave":0,"osc2Wave":4,"osc2Detune":12,"osc2PW":0.4,"subLevel":0.35,
   "filterCutoff":3000,"filterRes":0.2,"filterEnvAmt":0.2,"ampA":1.2,"ampD":1.0,"ampS":0.9,"ampR":3.0,
   "cosmosChorus":3,"reverbOn":1,"reverbSize":0.8,"reverbDecay":4.0,"reverbMix":0.3,"stereoWidth":0.9}),
 # Regal Brass: Oracle self-osc filter + poly-mod brass with 2-voice unison.
 ("Regal Brass", {"mode":1,"osc2Semi":0,"filterCutoff":1800,"filterRes":0.35,"filterEnvAmt":0.6,
   "filtA":0.04,"filtD":0.3,"filtS":0.4,"ampA":0.02,"ampD":0.3,"ampS":0.8,"ampR":0.35,
   "pmFenvFilt":0.35,"pmFenvOscA":0.1,"unisonVoices":2,"unisonDetune":6,"stereoWidth":0.6}),
 # Siren Lead: Mono screaming sync lead (hard sync + osc2 up an octave), driven,
 # slap of delay, portamento glide.
 ("Siren Lead", {"mode":2,"hardSync":1,"osc2Semi":12,"filterCutoff":4000,"filterRes":0.5,
   "filterEnvAmt":0.6,"filtA":0.01,"filtD":0.3,"filtS":0.3,"ampA":0.005,"ampS":0.8,"ampR":0.3,
   "portaTime":0.05,"driveOn":1,"driveAmt":0.4,"delayOn":1,"delaySync":1,"delayDiv":3,"delayMix":0.25,"delayFB":0.35}),
 # Nebula Static: Modular sci-fi texture — S&H modulating cutoff, third osc,
 # slow evolving pad, auto spring reverb + hall.
 ("Nebula Static", {"mode":3,"osc3Wave":3,"osc3Level":0.5,"shRate":6.0,
   "modSrc0":10,"modDst0":5,"modAmt0":0.6,"modSrc1":10,"modDst1":6,"modAmt1":0.3,
   "filterCutoff":1500,"filterRes":0.5,"filterEnvAmt":0.3,"ampA":0.6,"ampD":1.0,"ampS":0.8,"ampR":3.0,
   "reverbOn":1,"reverbSize":0.6,"reverbDecay":6.0,"reverbMix":0.35,"vintage":0.3}),
 # Glass Cathedral: Prism dual-stack pad — mellow FM, long amp swell, chorus +
 # big reverb for a glassy cathedral wash.
 ("Glass Cathedral", {"mode":4,"prismAlgo":4,"prismFB":0,
   "op1Ratio":1,"op1Level":0.9,"op1A":0.8,"op1D":2.0,"op1S":0.7,"op1R":2.5,
   "op2Ratio":2,"op2Level":0.35,"op2A":1.0,"op2D":2.0,"op2S":0.6,"op2R":2.5,
   "op3Ratio":1,"op3Level":0.7,"op3A":0.8,"op3D":2.0,"op3S":0.7,"op3R":2.5,
   "op4Ratio":3,"op4Level":0.25,"op4KeyScale":-0.2,"op4A":1.0,"op4D":2.0,"op4S":0.6,"op4R":2.5,
   "filterCutoff":9000,"filterEnvAmt":0,"ampA":1.0,"ampD":2.0,"ampS":0.85,"ampR":3.5,
   "chorusOn":1,"chorusMix":0.3,"reverbOn":1,"reverbSize":0.85,"reverbDecay":6.0,"reverbMix":0.35,"stereoWidth":0.85}),
 # Neon Sequence: Acid + ping-pong tempo-delay groove; resonant saw line with a
 # pattern that steps around the root, delay widening the space.
 ("Neon Sequence", {"mode":5,"arpOn":1,"osc1Wave":0,"filterCutoff":520,"filterRes":0.75,
   "filterEnvAmt":0.6,"ampD":0.3,"ampS":0.0,"acidAccentAmt":0.7,"acidSlideTime":60,
   "arpRate":4,"arpGate":0.5,
   "seqPitch1":12,"seqPitch4":7,"seqPitch6":3,"seqPitch9":10,"seqPitch12":5,"seqPitch14":-5,
   "seqAccent0":1,"seqAccent4":1,"seqAccent8":1,"seqAccent12":1,"seqSlide6":1,"seqSlide14":1,
   "delayOn":1,"delaySync":1,"delayDiv":3,"delayMix":0.3,"delayFB":0.4,"delayPP":1}),
]

def fl(v):
    """Format a number as a valid C++ float literal (always has a decimal)."""
    f = float(v)
    if f == int(f):
        return f"{int(f)}.0f"
    return f"{f!r}f"

out = io.StringIO()
w = out.write
w("// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).\n")
w("// Third-party components in the built plugins (DPF - ISC; Dear ImGui - MIT; and\n")
w("// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.\n")
w("//\n")
w("// MultiSynthParams.hpp - GENERATED by tools gen_params.py; do not hand-edit the\n")
w("// tables. One X-macro list (MSYNTH_PARAMS) drives the DPF param enum, the\n")
w("// initParameter() metadata, and the engine setter dispatch. The DPF parameter\n")
w("// index equals the core msynth::Param index 1:1 (verified by static_assert in\n")
w("// MultiSynthPlugin.cpp), so forwarding is a single dsp.setParameter(index,value).\n")
w("//\n")
w("// KIND: LIN linear float, LOG logarithmic float (min>0), INT integer choice,\n")
w("// BOOL on/off. Two output params (peak L/R for meters) follow the core params.\n\n")
w("#pragma once\n\n")
w("// X(EnumSuffix, symbol, Name, min, max, default, KIND)\n")
w("#define MSYNTH_PARAMS(X) \\\n")
for i,p in enumerate(P):
    suf,sym,name,mn,mx,df,kind = p
    end = " \\\n" if i < len(P)-1 else "\n"
    w(f'    X({suf}, "{sym}", "{name}", {fl(mn)}, {fl(mx)}, {fl(df)}, {kind}){end}')
w("\n")
w("// Parameter index enum (kParam<Suffix>), matching the core order 1:1.\n")
w("enum ParamId\n{\n")
w("#define X(suf, sym, name, mn, mx, df, kind) kParam##suf,\n")
w("    MSYNTH_PARAMS(X)\n")
w("#undef X\n")
w(f"    kNumCoreParams,                 // == msynth::kNumParams ({NCORE})\n")
w("    kParamOutLevelL = kNumCoreParams, // output: peak L (meter fallback)\n")
w("    kParamOutLevelR,                  // output: peak R\n")
w("    kParamCount\n};\n\n")

# Param metadata table for initParameter.
w("enum ParamKind { PK_LIN = 0, PK_LOG, PK_INT, PK_BOOL };\n")
w("struct ParamDef { const char* symbol; const char* name; float min, max, def; int kind; };\n")
w("static constexpr ParamDef kParamDefs[kNumCoreParams] =\n{\n")
w("#define LIN  PK_LIN\n#define LOG  PK_LOG\n#define INT  PK_INT\n#define BOOL PK_BOOL\n")
w("#define X(suf, sym, name, mn, mx, df, kind) { sym, name, mn, mx, df, kind },\n")
w("    MSYNTH_PARAMS(X)\n")
w("#undef X\n#undef LIN\n#undef LOG\n#undef INT\n#undef BOOL\n};\n\n")

# Factory presets.
w("// ---------------------------------------------------------------------------\n")
w(f"// Factory presets ({len(PRESETS)}) - static override table. loadProgram() resets every\n")
w("// param to its default, applies the shared baseline, then the per-preset\n")
w("// overrides. Every preset ships at 2x oversampling (baseline oversampling=1);\n")
w("// the pitch bug is fixed so 2x is correct. Phase 5 re-voiced the original 40\n")
w("// (unity-gain filter fix + level/intent) and appended new preset banks; see\n")
w("// docs/dpf-migration/09-multi-synth-presets.md for the per-preset change log.\n")
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
w("\nstruct FactoryPreset { const char* name; const PresetRow* rows; int nRows; };\n")
w("static constexpr FactoryPreset kFactoryPresets[] =\n{\n")
for i,(nm,d) in enumerate(PRESETS):
    var = f"kP{i:02d}"
    w(f'    {{ "{nm}", {var}, (int)(sizeof({var})/sizeof(PresetRow)) }},\n')
w("};\n")
w("static constexpr int kNumFactoryPresets = (int)(sizeof(kFactoryPresets)/sizeof(kFactoryPresets[0]));\n")
w("static constexpr int kBaselineRows = (int)(sizeof(kPresetBaseline)/sizeof(PresetRow));\n")

_out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "MultiSynthParams.hpp")
open(_out_path, "w").write(out.getvalue())
print(f"core params = {NCORE}, presets = {len(PRESETS)}")
