cmake_minimum_required(VERSION 3.22)

project(SoundEffects VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clangd / clang-tidy

# --- JUCE ---------------------------------------------------------------------
# Pull JUCE at configure time. Pin a tag so builds are reproducible; bump when a
# newer JUCE is wanted. Alternatively, drop a JUCE checkout next to this file and
# swap FetchContent for add_subdirectory(JUCE).
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4          # last verified tag; update deliberately
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- the plugin (standalone + VST3 + AU) --------------------------------------
juce_add_plugin(SoundEffects
    COMPANY_NAME              "SoundEffects"
    PLUGIN_MANUFACTURER_CODE  Snde
    PLUGIN_CODE               Sfx1
    FORMATS                   Standalone VST3 AU
    IS_SYNTH                  FALSE       # loadable as audio effect OR instrument
    NEEDS_MIDI_INPUT          TRUE        # accepts MIDI for the instrument use case
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PRODUCT_NAME              "SoundEffects")

target_sources(SoundEffects PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/engine/SoundDocument.cpp
    Source/engine/Mixer.cpp
    Source/engine/Transport.cpp
    Source/engine/Engine.cpp
    Source/engine/EngineState.cpp
    Source/engine/Import.cpp
    Source/engine/Edit.cpp
    Source/engine/Resample.cpp
    # The effects runtime + one file per ported module. Kept in the plugin build
    # even before the panel uses it, so the app target and native/tests (which is
    # where the golden vectors run) cannot drift apart.
    Source/effects/Host.cpp
    Source/effects/Module.cpp
    Source/effects/Registry.cpp
    Source/effects/Skeleton.cpp
    # Bridges the effects layer to the Engine (Windows, RCU commit, spawned
    # documents). Not in native/tests: the golden vectors need no Engine.
    Source/effects/EffectRunner.cpp
    Source/effects/modules/Amplify.cpp
    Source/effects/modules/Simple.cpp
    Source/effects/modules/Smooth.cpp
    Source/effects/modules/Dither.cpp
    Source/effects/modules/Noise.cpp
    Source/effects/modules/Robotize.cpp
    Source/effects/modules/Fir.cpp
    Source/effects/modules/Iir.cpp
    Source/effects/modules/EchoDelay.cpp
    Source/effects/modules/Replicate.cpp
    Source/effects/modules/WeirdTempo.cpp
    Source/effects/modules/ZeroCrosser.cpp
    Source/effects/modules/Channels.cpp
    Source/effects/modules/Resampling.cpp
    Source/effects/modules/QuickMix.cpp
    Source/effects/modules/IterativeCrossfade.cpp
    Source/effects/modules/Keyboard.cpp
    Source/effects/modules/Flanger.cpp
    Source/effects/modules/Granular.cpp
    Source/effects/modules/SampleHose.cpp
    Source/effects/modules/Waveform.cpp
    Source/ui/Sys7.cpp
    Source/ui/EffectsPanel.cpp
    Source/ui/KeyboardArt.cpp
    Source/ui/KeyboardPalette.cpp
    Source/ui/CanvasComponent.cpp
    Source/ui/WindowComponent.cpp
    Source/ui/MixerComponent.cpp
    Source/ui/ViewPalette.cpp)

target_compile_definitions(SoundEffects PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_include_directories(SoundEffects PRIVATE Source)

# --- the standalone app's own window ------------------------------------------
# JUCE's stock standalone app draws its own title bar and hangs an "Options" button
# in it. The app's outer window is a macOS window and should wear macOS chrome, so
# we supply the JUCEApplication ourselves. This lives on the Standalone target only:
# the VST3 and AU builds have a host window and no business defining an app.
target_compile_definitions(SoundEffects_Standalone PRIVATE
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_sources(SoundEffects_Standalone PRIVATE Source/StandaloneApp.cpp)

# --- .sefx documents ----------------------------------------------------------
# Teach the Finder what a canvas is: an exported UTI plus the app's claim on it,
# so a .sefx has a name in the Get Info panel and opens SoundEffects on a
# double-click. See packaging/DocumentTypes.plist for what goes in and why it is
# not juce_add_plugin's DOCUMENT_EXTENSIONS.
#
# The splice is deliberate. JUCE writes each bundle's Info.plist at CONFIGURE
# time, from properties held on the shared target, and there is no per-format
# hook — so the only way to reach the standalone's plist and no other is to take
# the file JUCE just generated, add our keys, and point the target at the result.
# MACOSX_BUNDLE_INFO_PLIST is a plain CMake property, so the last write wins.
# Re-running CMake regenerates JUCE's file first, then this, so the two cannot
# drift; nothing here costs anything at build time.
if(APPLE)
    get_target_property(_se_juce_plist SoundEffects_Standalone MACOSX_BUNDLE_INFO_PLIST)
    file(READ "${_se_juce_plist}" _se_plist_text)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/packaging/DocumentTypes.plist" _se_doc_types)

    # Before the LAST </dict>, which is the top-level one whatever is nested above it.
    string(FIND "${_se_plist_text}" "</dict>" _se_close REVERSE)
    if(_se_close EQUAL -1)
        message(FATAL_ERROR "JUCE's generated Info.plist has no closing </dict>: ${_se_juce_plist}")
    endif()
    string(SUBSTRING "${_se_plist_text}" 0 ${_se_close} _se_head)
    string(SUBSTRING "${_se_plist_text}" ${_se_close} -1 _se_tail)

    set(_se_plist_out "${CMAKE_CURRENT_BINARY_DIR}/SoundEffects_Standalone_Info.plist")
    file(WRITE "${_se_plist_out}" "${_se_head}${_se_doc_types}${_se_tail}")
    set_target_properties(SoundEffects_Standalone PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${_se_plist_out}")
endif()

target_link_libraries(SoundEffects PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
