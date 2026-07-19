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
    Source/engine/Import.cpp
    Source/engine/Edit.cpp
    Source/engine/Resample.cpp
    Source/ui/CanvasComponent.cpp
    Source/ui/WindowComponent.cpp
    Source/ui/MixerComponent.cpp)

target_compile_definitions(SoundEffect PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_include_directories(SoundEffect PRIVATE Source)

target_link_libraries(SoundEffect PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
