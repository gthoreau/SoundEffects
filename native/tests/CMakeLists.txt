# The engine test build — deliberately its OWN project, with no JUCE.
#
# The effect DSP and the document model are host-agnostic pure C++17 (that is the
# rule for everything under Source/engine/), so testing them must not require
# fetching and building JUCE. This configures and builds in seconds, which is what
# makes it usable as the inner loop while porting 36 modules.
#
#   cmake -S native/tests -B native/tests/build -G Ninja
#   cmake --build native/tests/build
#   ./native/tests/build/effect-fixtures

cmake_minimum_required(VERSION 3.22)

project(SoundEffectsTests VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Source)

# Engine sources the DSP tests need. These are JUCE-free by construction; if a
# juce include ever creeps into one, this target is where it gets caught.
set(ENGINE_SOURCES
    ${ENGINE_DIR}/engine/SoundDocument.cpp
    ${ENGINE_DIR}/engine/Edit.cpp
    ${ENGINE_DIR}/engine/Resample.cpp
    ${ENGINE_DIR}/engine/RecordConvert.cpp)

# The effects runtime, plus one file per ported module. Modules are added here as
# the porting waves land — this list and Registry.cpp are the two places a new
# module gets mentioned.
set(EFFECT_SOURCES
    ${ENGINE_DIR}/effects/Host.cpp
    ${ENGINE_DIR}/effects/Module.cpp
    ${ENGINE_DIR}/effects/Registry.cpp
    ${ENGINE_DIR}/effects/Skeleton.cpp
    ${ENGINE_DIR}/effects/modules/Amplify.cpp
    ${ENGINE_DIR}/effects/modules/Simple.cpp
    ${ENGINE_DIR}/effects/modules/Smooth.cpp
    ${ENGINE_DIR}/effects/modules/Dither.cpp
    ${ENGINE_DIR}/effects/modules/Noise.cpp
    ${ENGINE_DIR}/effects/modules/Robotize.cpp
    ${ENGINE_DIR}/effects/modules/Fir.cpp
    ${ENGINE_DIR}/effects/modules/Iir.cpp
    ${ENGINE_DIR}/effects/modules/EchoDelay.cpp
    ${ENGINE_DIR}/effects/modules/Replicate.cpp
    ${ENGINE_DIR}/effects/modules/WeirdTempo.cpp
    ${ENGINE_DIR}/effects/modules/ZeroCrosser.cpp
    ${ENGINE_DIR}/effects/modules/Channels.cpp
    ${ENGINE_DIR}/effects/modules/Resampling.cpp
    ${ENGINE_DIR}/effects/modules/QuickMix.cpp
    ${ENGINE_DIR}/effects/modules/IterativeCrossfade.cpp
    ${ENGINE_DIR}/effects/modules/Keyboard.cpp
    ${ENGINE_DIR}/effects/modules/Flanger.cpp
    ${ENGINE_DIR}/effects/modules/Granular.cpp
    ${ENGINE_DIR}/effects/modules/SampleHose.cpp
    ${ENGINE_DIR}/effects/modules/Waveform.cpp)

add_executable(effect-fixtures
    run_fixtures.cpp
    FixtureFile.cpp
    ${ENGINE_SOURCES}
    ${EFFECT_SOURCES})

target_include_directories(effect-fixtures PRIVATE ${ENGINE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# The Engine's own tests: the desktop-wide mixer (gain / mute / the cumulative solo
# filter) and the rule that none of them stops a transport. Nothing to do with the
# effect fixtures, so it is its own binary — and JUCE-free for the same reason.
add_executable(engine-tests
    run_engine.cpp
    ${ENGINE_DIR}/engine/Engine.cpp
    ${ENGINE_DIR}/engine/Recorder.cpp
    ${ENGINE_DIR}/engine/Transport.cpp
    ${ENGINE_DIR}/engine/Mixer.cpp
    ${ENGINE_SOURCES})

target_include_directories(engine-tests PRIVATE ${ENGINE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# Save / load: the canvas round-trips through the state blob, and a blob we did not
# write is refused rather than believed. JUCE-free for the usual reason — the
# format belongs to the engine, not to the standalone or to any one host.
add_executable(state-tests
    run_state.cpp
    ${ENGINE_DIR}/engine/Engine.cpp
    ${ENGINE_DIR}/engine/EngineState.cpp
    ${ENGINE_DIR}/engine/Project.cpp
    ${ENGINE_DIR}/engine/Wav.cpp
    ${ENGINE_DIR}/engine/Transport.cpp
    ${ENGINE_DIR}/engine/Mixer.cpp
    ${ENGINE_SOURCES})

target_include_directories(state-tests PRIVATE ${ENGINE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(effect-fixtures PRIVATE /W4)
    target_compile_options(engine-tests    PRIVATE /W4)
    target_compile_options(state-tests     PRIVATE /W4)
else()
    target_compile_options(effect-fixtures PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(engine-tests    PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(state-tests     PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Run from the tests directory so the default fixture path resolves.
add_custom_target(check
    COMMAND effect-fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/effects
    DEPENDS effect-fixtures
    USES_TERMINAL)
