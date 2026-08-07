cmake_minimum_required(VERSION 3.22)

project(SoundEffect VERSION 0.1.0 LANGUAGES C CXX)

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
juce_add_plugin(SoundEffect
    COMPANY_NAME              "SoundEffect"
    PLUGIN_MANUFACTURER_CODE  Snde
    PLUGIN_CODE               Sfx1
    FORMATS                   Standalone VST3 AU
    IS_SYNTH                  FALSE       # loadable as audio effect OR instrument
    NEEDS_MIDI_INPUT          TRUE        # accepts MIDI for the instrument use case
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PRODUCT_NAME              "SoundEffect")

target_sources(SoundEffect PRIVATE
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
    Source/ui/CanvasComponent.cpp
    Source/ui/WindowComponent.cpp
    Source/ui/MixerComponent.cpp
    Source/ui/ViewPalette.cpp)

target_compile_definitions(SoundEffect PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_include_directories(SoundEffect PRIVATE Source)

# --- the standalone app's own window ------------------------------------------
# JUCE's stock standalone app draws its own title bar and hangs an "Options" button
# in it. The app's outer window is a macOS window and should wear macOS chrome, so
# we supply the JUCEApplication ourselves. This lives on the Standalone target only:
# the VST3 and AU builds have a host window and no business defining an app.
target_compile_definitions(SoundEffect_Standalone PRIVATE
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_sources(SoundEffect_Standalone PRIVATE Source/StandaloneApp.cpp)

target_link_libraries(SoundEffect PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
