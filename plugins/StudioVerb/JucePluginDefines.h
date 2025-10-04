/*
  ==============================================================================

   Studio Verb Plugin Defines - Required for JUCE compilation

  ==============================================================================
*/

#pragma once

#define JucePlugin_Name                 "Studio Verb"
#define JucePlugin_Desc                 "Professional Reverb Plugin"
#define JucePlugin_Manufacturer         "Luna Co. Audio"
#define JucePlugin_ManufacturerWebsite  "https://lunaco.audio"
#define JucePlugin_ManufacturerEmail    "support@lunaco.audio"
#define JucePlugin_ManufacturerCode     0x4c75436f  // 'LuCo'
#define JucePlugin_PluginCode           0x53747556  // 'StuV'
#define JucePlugin_IsSynth              0
#define JucePlugin_WantsMidiInput       0
#define JucePlugin_ProducesMidiOutput   0
#define JucePlugin_IsMidiEffect         0
#define JucePlugin_EditorRequiresKeyboardFocus  0
#define JucePlugin_Version              1.0.0
#define JucePlugin_VersionCode          0x10000
#define JucePlugin_VersionString        "1.0.0"
#define JucePlugin_VSTUniqueID          JucePlugin_PluginCode
#define JucePlugin_VSTCategory          kPlugCategEffect
#define JucePlugin_Vst3Category         "Fx|Reverb"
#define JucePlugin_AUMainType           'aufx'
#define JucePlugin_AUSubType            JucePlugin_PluginCode
#define JucePlugin_AUExportPrefix       StudioVerbAU
#define JucePlugin_AUExportPrefixQuoted "StudioVerbAU"
#define JucePlugin_AUManufacturerCode   JucePlugin_ManufacturerCode
#define JucePlugin_CFBundleIdentifier   com.LunaCoAudio.StudioVerb
#define JucePlugin_RTASCategory         0
#define JucePlugin_RTASManufacturerCode JucePlugin_ManufacturerCode
#define JucePlugin_RTASProductId        JucePlugin_PluginCode
#define JucePlugin_RTASDisableBypass    0
#define JucePlugin_RTASDisableMultiMono 0
#define JucePlugin_AAXIdentifier        com.LunaCoAudio.StudioVerb
#define JucePlugin_AAXManufacturerCode  JucePlugin_ManufacturerCode
#define JucePlugin_AAXProductId         JucePlugin_PluginCode
#define JucePlugin_AAXCategory          2
#define JucePlugin_AAXDisableBypass     0
#define JucePlugin_AAXDisableMultiMono  0
#define JucePlugin_IAAType              0x61757278  // 'aurx'
#define JucePlugin_IAASubType           JucePlugin_PluginCode
#define JucePlugin_IAAName              "Luna Co. Audio: Studio Verb"
#define JucePlugin_VSTNumMidiInputs     0
#define JucePlugin_VSTNumMidiOutputs    0
#define JucePlugin_MaxNumInputChannels  2
#define JucePlugin_MaxNumOutputChannels 2
#define JucePlugin_PreferredChannelConfigurations {2, 2}