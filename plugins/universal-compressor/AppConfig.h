/*
  AppConfig.h
  Build configuration for Universal Compressor
*/

#pragma once

//==============================================================================
// JUCE library configuration

#define JUCE_USE_WINRT_MIDI 0
#define JUCE_ASIO 0
#define JUCE_WASAPI 0
#define JUCE_DIRECTSOUND 0
#define JUCE_ALSA 1
#define JUCE_JACK 1
#define JUCE_PLUGINHOST_VST3 1
#define JUCE_PLUGINHOST_LV2 1
#define JUCE_PLUGINHOST_AU 0

#define JUCE_USE_CURL 0
#define JUCE_WEB_BROWSER 0

#define JUCE_DISPLAY_SPLASH_SCREEN 0
#define JUCE_REPORT_APP_USAGE 0

#define JUCE_USE_DARK_SPLASH_SCREEN 1
#define JUCE_MODAL_LOOPS_PERMITTED 0

#define JUCE_MODULE_AVAILABLE_juce_audio_basics          1
#define JUCE_MODULE_AVAILABLE_juce_audio_devices         1
#define JUCE_MODULE_AVAILABLE_juce_audio_formats         1
#define JUCE_MODULE_AVAILABLE_juce_audio_processors      1
#define JUCE_MODULE_AVAILABLE_juce_audio_utils           1
#define JUCE_MODULE_AVAILABLE_juce_core                  1
#define JUCE_MODULE_AVAILABLE_juce_data_structures       1
#define JUCE_MODULE_AVAILABLE_juce_dsp                   1
#define JUCE_MODULE_AVAILABLE_juce_events                1
#define JUCE_MODULE_AVAILABLE_juce_graphics              1
#define JUCE_MODULE_AVAILABLE_juce_gui_basics            1
#define JUCE_MODULE_AVAILABLE_juce_gui_extra             1

#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1

//==============================================================================
// Plugin settings

#ifndef  JucePlugin_Name
 #define JucePlugin_Name                   "Universal Compressor"
#endif

#ifndef  JucePlugin_PluginCode
 #define JucePlugin_PluginCode             0x556e436f  // 'UnCo'
#endif

#ifndef  JucePlugin_IsSynth
 #define JucePlugin_IsSynth                0
#endif

#ifndef  JucePlugin_WantsMidiInput
 #define JucePlugin_WantsMidiInput         0
#endif

#ifndef  JucePlugin_ProducesMidiOutput
 #define JucePlugin_ProducesMidiOutput     0
#endif

#ifndef  JucePlugin_IsMidiEffect
 #define JucePlugin_IsMidiEffect           0
#endif

#ifndef  JucePlugin_EditorRequiresKeyboardFocus
 #define JucePlugin_EditorRequiresKeyboardFocus  0
#endif

#ifndef  JucePlugin_Version
 #define JucePlugin_Version                1.0.0
#endif

#ifndef  JucePlugin_VersionCode
 #define JucePlugin_VersionCode            0x10000
#endif

#ifndef  JucePlugin_VersionString
 #define JucePlugin_VersionString          "1.0.0"
#endif

#ifndef  JucePlugin_VSTUniqueID
 #define JucePlugin_VSTUniqueID            JucePlugin_PluginCode
#endif

#ifndef  JucePlugin_Manufacturer
 #define JucePlugin_Manufacturer           "AudioEngineering"
#endif

#ifndef  JucePlugin_ManufacturerWebsite
 #define JucePlugin_ManufacturerWebsite    ""
#endif

#ifndef  JucePlugin_ManufacturerEmail
 #define JucePlugin_ManufacturerEmail      ""
#endif

#ifndef  JucePlugin_ManufacturerCode
 #define JucePlugin_ManufacturerCode       0x41754569  // 'AuEi'
#endif

#ifndef  JucePlugin_MaxNumInputChannels
 #define JucePlugin_MaxNumInputChannels    2
#endif

#ifndef  JucePlugin_MaxNumOutputChannels
 #define JucePlugin_MaxNumOutputChannels   2
#endif

#ifndef  JucePlugin_PreferredChannelConfigurations
 #define JucePlugin_PreferredChannelConfigurations  {1, 1}, {2, 2}
#endif

#ifndef  JucePlugin_Build_VST
 #define JucePlugin_Build_VST              0
#endif

#ifndef  JucePlugin_Build_VST3
 #define JucePlugin_Build_VST3             0
#endif

#ifndef  JucePlugin_Build_AU
 #define JucePlugin_Build_AU               0
#endif

#ifndef  JucePlugin_Build_LV2
 #define JucePlugin_Build_LV2              1
#endif

#ifndef  JucePlugin_Build_RTAS
 #define JucePlugin_Build_RTAS             0
#endif

#ifndef  JucePlugin_Build_AAX
 #define JucePlugin_Build_AAX              0
#endif

#ifndef  JucePlugin_Build_Standalone
 #define JucePlugin_Build_Standalone       1
#endif

#ifndef  JucePlugin_Enable_IAA
 #define JucePlugin_Enable_IAA             0
#endif